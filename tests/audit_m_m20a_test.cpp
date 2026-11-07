#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "butchery.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "harvest.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "messages.h"
#include "mongroup.h"
#include "monster.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "player_helpers.h"
#include "projectile.h"
#include "rng.h"
#include "string_formatter.h"
#include "type_id.h"
#include "value_ptr.h"

static const damage_type_id damage_bash( "bash" );

static const efftype_id effect_has_bag( "has_bag" );
static const efftype_id effect_invisibility( "invisibility" );
static const efftype_id effect_leashed( "leashed" );
static const efftype_id effect_monster_armor( "monster_armor" );
static const efftype_id effect_monster_saddled( "monster_saddled" );

static const fault_id fault_bionic_salvaged( "fault_bionic_salvaged" );

static const harvest_drop_type_id harvest_drop_bone( "bone" );

static const itype_id itype_bio_power_storage( "bio_power_storage" );
static const itype_id itype_horse_tack( "horse_tack" );
static const itype_id itype_kevlar_armor_horse( "kevlar_armor_horse" );
static const itype_id itype_knife_combat( "knife_combat" );
static const itype_id itype_rope_30( "rope_30" );
static const itype_id itype_saddlebag( "saddlebag" );
static const itype_id itype_scalpel( "scalpel" );

static const json_character_flag json_flag_DISTRIBUTED_DAMAGE( "DISTRIBUTED_DAMAGE" );

static const mongroup_id GROUP_AUDIT_M20A( "GROUP_AUDIT_M20A" );

static const mtype_id mon_audit_m20a_bash_number( "mon_audit_m20a_bash_number" );
static const mtype_id mon_audit_m20a_bash_zero( "mon_audit_m20a_bash_zero" );
static const mtype_id mon_audit_m20a_block_child( "mon_audit_m20a_block_child" );
static const mtype_id mon_audit_m20a_block_parent( "mon_audit_m20a_block_parent" );
static const mtype_id mon_audit_m20a_keeps_range( "mon_audit_m20a_keeps_range" );
static const mtype_id mon_audit_m20a_mount( "mon_audit_m20a_mount" );
static const mtype_id mon_audit_m20a_salvage( "mon_audit_m20a_salvage" );
static const mtype_id mon_audit_m20a_single_spawn( "mon_audit_m20a_single_spawn" );
static const mtype_id mon_audit_m20a_watcher( "mon_audit_m20a_watcher" );
static const mtype_id mon_deer( "mon_deer" );

static const proficiency_id proficiency_prof_butchering_basic( "prof_butchering_basic" );
static const proficiency_id proficiency_prof_skinning_basic( "prof_skinning_basic" );

static const skill_id skill_electronics( "electronics" );
static const skill_id skill_firstaid( "firstaid" );

static const ter_str_id ter_t_wall( "t_wall" );

static const trait_id trait_audit_m20a_distributed( "audit_m20a_distributed" );

namespace
{

JsonObject json_object( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json ).get_object();
    jo.allow_omitted_members();
    return jo;
}

bool has_message_containing( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

std::string save_monster( const monster &mon )
{
    std::ostringstream os;
    JsonOut jsout( os );
    mon.serialize( jsout );
    return os.str();
}

void build_wall();

// An open map with a wall at a fixed time; the avatar stands well away from the watchers.
void vision_world( const time_point &when )
{
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_avatar();
    get_avatar().setpos( get_map(), tripoint_bub_ms( 30, 30, 0 ) );
    build_wall();
    // twice, as vision tests do: the second build sees the light of the first
    set_time( when );
    set_time( when );
}

constexpr tripoint_bub_ms watcher_pos( 60, 60, 0 );
// in the open, 4 tiles south of the watcher
constexpr tripoint_bub_ms open_pos( 60, 64, 0 );
// 4 tiles east of the watcher, behind a wall
constexpr tripoint_bub_ms walled_pos( 64, 60, 0 );

void build_wall()
{
    map &here = get_map();
    for( int y = 57; y <= 63; ++y ) {
        here.ter_set( tripoint_bub_ms( 62, y, 0 ), ter_t_wall );
    }
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0 );
}

struct sight_result {
    bool open = false;
    bool walled = false;
};

// Whether a watcher of this type sees a target in the open and one behind the wall
sight_result watcher_sees( const std::string &watcher_type, bool invisible_targets )
{
    clear_creatures();
    map &here = get_map();
    monster &watcher = spawn_test_monster( watcher_type, watcher_pos );
    monster &in_open = spawn_test_monster( "mon_audit_m20a_watcher", open_pos );
    monster &behind_wall = spawn_test_monster( "mon_audit_m20a_watcher", walled_pos );
    if( invisible_targets ) {
        in_open.add_effect( effect_invisibility, 1_hours );
        behind_wall.add_effect( effect_invisibility, 1_hours );
    }
    sight_result ret;
    ret.open = watcher.sees( here, in_open );
    ret.walled = watcher.sees( here, behind_wall );
    clear_creatures();
    return ret;
}

} // namespace

// MONDT-B05: coverage / profile was integer division, so any cover the character didn't fully
// hide behind left them visible from 30 tiles; now the range falls with the share hidden.
TEST_CASE( "mondt-b05_partial_cover_shortens_the_sight_range", "[audit][vision]" )
{
    // crouching medium character: profile 60
    CHECK( partial_cover_sight_range( 45, creature_size::medium, true, false ) == 7 );
    CHECK( partial_cover_sight_range( 30, creature_size::medium, true, false ) == 15 );
    // cover taller than the profile hides them
    CHECK( partial_cover_sight_range( 70, creature_size::medium, true, false ) == 0 );
    // just under the profile: still seen, from 1 tile
    CHECK( partial_cover_sight_range( 59, creature_size::medium, true, false ) == 1 );
    // seen across z-levels, standing: profile 120
    CHECK( partial_cover_sight_range( 30, creature_size::medium, false, false ) == 22 );
    // prone: profile 33, kept fractional
    CHECK( partial_cover_sight_range( 30, creature_size::medium, false, true ) == 2 );
    // a large crouching character has a profile of 100
    CHECK( partial_cover_sight_range( 45, creature_size::large, true, false ) == 16 );
    // a huge one 400
    CHECK( partial_cover_sight_range( 100, creature_size::huge, true, false ) == 22 );
    // a tiny crouching one 30: any cover that counts hides them
    CHECK( partial_cover_sight_range( 30, creature_size::tiny, true, false ) == 0 );
    // more cover, never a longer range
    for( int coverage = 31; coverage <= 100; ++coverage ) {
        CAPTURE( coverage );
        CHECK( partial_cover_sight_range( coverage, creature_size::medium, false, false ) <=
               partial_cover_sight_range( coverage - 1, creature_size::medium, false, false ) );
    }
}

// MONDT-B06: the DISTRIBUTED_DAMAGE hit message had "%2$d" and no argument (a debugmsg on every
// ranged hit), and a shot with no shooter (NULL_SOURCE ammo) dereferenced the null source.
TEST_CASE( "mondt-b06_distributed_damage_ranged_hit_message", "[audit][combat]" )
{
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
        Messages::clear_messages();
    } );
    avatar &you = get_avatar();
    you.set_mutation( trait_audit_m20a_distributed );
    REQUIRE( you.has_flag( json_flag_DISTRIBUTED_DAMAGE ) );
    you.set_all_parts_hp_cur( 10000 );
    Messages::clear_messages();

    dealt_projectile_attack shot{};
    shot.proj.speed = 1000;
    shot.proj.range = 60;
    shot.proj.impact = damage_instance( damage_bash, 10.0f );
    // no shooter
    const std::string errors = capture_debugmsg_during( [&you, &shot]() {
        you.deal_projectile_attack( &get_map(), nullptr, shot, 0, true );
    } );
    CHECK( errors.empty() );
    const int total = shot.dealt_dam.total_damage();
    REQUIRE( total > 0 );
    CHECK( has_message_containing( string_format( "You were hit for %d total damage.", total ) ) );
}

// MONDT-B07: TRUESIGHT saw invisible creatures through walls, and INFRARED_VISION saw warm ones
// through walls; line of sight and range now apply to both.
TEST_CASE( "mondt-b07_true_sight_and_infrared_need_line_of_sight", "[audit][vision]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_map();
    } );

    SECTION( "true sight sees the invisible, but not through a wall" ) {
        vision_world( calendar::turn_zero + 12_hours );
        const sight_result plain = watcher_sees( "mon_audit_m20a_watcher", true );
        CHECK_FALSE( plain.open );
        CHECK_FALSE( plain.walled );
        const sight_result truesight = watcher_sees( "mon_audit_m20a_truesight", true );
        CHECK( truesight.open );
        CHECK_FALSE( truesight.walled );
    }
    SECTION( "infrared sees warm creatures in the dark, but not through a wall" ) {
        vision_world( calendar::turn_zero );
        const sight_result infrared = watcher_sees( "mon_audit_m20a_infrared", false );
        CHECK( infrared.open );
        CHECK_FALSE( infrared.walled );
    }
}

// MONDT-B08: a harvest entry's faults were removed, not added, for discrete and liquid drops,
// so a dissected power storage module came without the salvaged fault.
TEST_CASE( "mondt-b08_harvest_faults_are_added", "[audit][butchery]" )
{
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );
    map &here = get_map();
    Character &u = get_player_character();
    u.set_skill_level( skill_electronics, 10 );
    u.set_skill_level( skill_firstaid, 10 );
    item scalpel( itype_scalpel );
    REQUIRE( u.wield( scalpel ) );

    const tripoint_bub_ms pos( 60, 60, 0 );
    monster salvage( mon_audit_m20a_salvage, pos );
    const tripoint_abs_ms corpse_abs = salvage.pos_abs();
    here.i_clear( pos );
    salvage.die( &here, nullptr );
    u.move_to( corpse_abs );
    REQUIRE( u.pos_abs() == corpse_abs );
    REQUIRE( !here.i_at( pos ).empty() );
    item_location loc( map_cursor( u.pos_abs() ), &*here.i_at( pos ).begin() );
    REQUIRE( loc->is_corpse() );
    butchery_data bd( loc, butcher_type::DISSECT );
    butchery_activity_actor act( bd );
    act.calculate_butchery_data( u, bd );
    destroy_the_carcass( bd, u );

    int modules = 0;
    for( const item &it : here.i_at( pos ) ) {
        if( it.typeId() == itype_bio_power_storage ) {
            ++modules;
            CHECK( it.has_fault( fault_bionic_salvaged ) );
            CHECK( it.has_flag( flag_FILTHY ) );
        }
    }
    CHECK( modules == 1 );
}

// MONDT-B09: a field-dressed corpse gave 1 to 1.5 times the bones of a whole one on top of what
// dressing took; now half to all of them are left.
TEST_CASE( "mondt-b09_dressing_takes_bones_from_the_corpse", "[audit][butchery]" )
{
    clear_avatar();
    const Character &you = get_player_character();
    item dressed = item::make_corpse( mon_deer );
    dressed.set_flag( flag_FIELD_DRESS );
    const item whole = item::make_corpse( mon_deer );
    const mtype &mt = *whole.get_mtype();
    const harvest_drop_type &bone = harvest_drop_bone.obj();
    const int roll = 1000;
    int total = 0;
    const int tries = 200;
    // one seed for the whole run: the first draw after seeding with 4200, 4201... is nearly the
    // same number each time (the engine's output is close to linear in the seed)
    rng_set_engine_seed( 4200u );
    for( int i = 0; i < tries; ++i ) {
        const std::optional<int> taken = butchery_entry_yield( bone, roll, butcher_type::FIELD_DRESS,
                                         whole, mt, you );
        const std::optional<int> left = butchery_entry_yield( bone, roll, butcher_type::FULL, dressed,
                                        mt, you );
        REQUIRE( taken );
        REQUIRE( left );
        CHECK( *taken >= 0 );
        CHECK( *taken <= roll / 2 );
        CHECK( *left >= roll / 2 );
        CHECK( *left <= roll );
        total += *taken + *left;
    }
    // on average about what butchering the whole corpse gives, not half as much again
    const int full = *butchery_entry_yield( bone, roll, butcher_type::FULL, whole, mt, you );
    REQUIRE( full == roll );
    CHECK( total / tries > roll * 9 / 10 );
    CHECK( total / tries < roll * 11 / 10 );
}

// MONDT-B10: the butchery-time proficiency penalties were whole numbers, so skinning
// proficiency never mattered and partial butchering practice counted for little.
TEST_CASE( "mondt-b10_proficiencies_change_butchery_time", "[audit][butchery]" )
{
    clear_map();
    clear_npcs();
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    Character &you = get_player_character();
    item knife( itype_knife_combat );
    REQUIRE( you.wield( knife ) );
    const item corpse = item::make_corpse( mon_deer );

    const int skin_without = butcher_time_to_cut( you, corpse, butcher_type::SKIN );
    const int dress_without = butcher_time_to_cut( you, corpse, butcher_type::FIELD_DRESS );
    you.add_proficiency( proficiency_prof_skinning_basic, true );
    you.add_proficiency( proficiency_prof_butchering_basic, true );
    const int skin_with = butcher_time_to_cut( you, corpse, butcher_type::SKIN );
    const int dress_with = butcher_time_to_cut( you, corpse, butcher_type::FIELD_DRESS );
    CAPTURE( skin_without, skin_with, dress_without, dress_with );
    REQUIRE( skin_with > 0 );
    REQUIRE( dress_with > 0 );
    // skinning: 1.5 times as long without the proficiency
    CHECK( skin_without * 2 == skin_with * 3 );
    // field dressing: 3 times the time without either butchering proficiency, 1.5 times with
    // only the basic one
    CHECK( dress_without == dress_with * 2 );
}

// MONDT-B11: the storage item of mount_items went into the tack slot, overwriting the saddle
// and leaving the bag effect with no bag.
TEST_CASE( "mondt-b11_mount_items_fill_their_own_slots", "[audit][monster]" )
{
    const monster mount( mon_audit_m20a_mount );
    REQUIRE( mount.tied_item );
    REQUIRE( mount.tack_item );
    REQUIRE( mount.armor_item );
    REQUIRE( mount.storage_item );
    CHECK( mount.tied_item->typeId() == itype_rope_30 );
    CHECK( mount.tack_item->typeId() == itype_horse_tack );
    CHECK( mount.armor_item->typeId() == itype_kevlar_armor_horse );
    CHECK( mount.storage_item->typeId() == itype_saddlebag );
    CHECK( mount.has_effect( effect_leashed ) );
    CHECK( mount.has_effect( effect_monster_saddled ) );
    CHECK( mount.has_effect( effect_monster_armor ) );
    CHECK( mount.has_effect( effect_has_bag ) );
}

// MONDT-B12: the summoner wasn't saved, and only a character could be one.
TEST_CASE( "mondt-b12_summoner_survives_a_reload", "[audit][monster]" )
{
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_creatures();
    } );
    avatar &u = get_avatar();
    const tripoint_bub_ms here_pos = u.pos_bub();

    SECTION( "a character's summon" ) {
        monster &summon = spawn_test_monster( "mon_audit_m20a_watcher",
                                              here_pos + tripoint_rel_ms( 3, 0, 0 ) );
        summon.set_summoner( &u );
        REQUIRE( summon.get_summoner() == &u );
        const std::string saved = save_monster( summon );
        CHECK( saved.find( "\"summoner\"" ) != std::string::npos );
        monster loaded;
        loaded.deserialize( json_loader::from_string( saved ) );
        CHECK( loaded.get_summoner() == &u );
    }
    SECTION( "a monster's summon" ) {
        monster &caster = spawn_test_monster( "mon_audit_m20a_watcher",
                                              here_pos + tripoint_rel_ms( 0, 3, 0 ) );
        monster &summon = spawn_test_monster( "mon_audit_m20a_watcher",
                                              here_pos + tripoint_rel_ms( 3, 3, 0 ) );
        summon.set_summoner( &caster );
        CHECK( summon.get_summoner() == &caster );
        const std::string saved = save_monster( summon );
        monster loaded;
        loaded.deserialize( json_loader::from_string( saved ) );
        CHECK( loaded.get_summoner() == &caster );
    }
    SECTION( "a save without a summoner loads with none" ) {
        monster &plain = spawn_test_monster( "mon_audit_m20a_watcher",
                                             here_pos + tripoint_rel_ms( 3, 0, 0 ) );
        REQUIRE( plain.get_summoner() == nullptr );
        const std::string saved = save_monster( plain );
        CHECK( saved.find( "\"summoner\"" ) == std::string::npos );
        monster loaded;
        loaded.set_summoner( &u );
        loaded.deserialize( json_loader::from_string( saved ) );
        CHECK( loaded.get_summoner() == nullptr );
    }
}

// MONDT-B13: a definition that added to a group reset its is_animal and is_safe to false.
TEST_CASE( "mondt-b13_extending_a_group_keeps_its_settings", "[audit][monster]" )
{
    std::map<mongroup_id, MonsterGroup> &groups = MonsterGroupManager::Get_all_Groups();
    REQUIRE( groups.count( GROUP_AUDIT_M20A ) == 1 );
    const MonsterGroup saved = groups.at( GROUP_AUDIT_M20A );
    on_out_of_scope restore( [&saved]() {
        MonsterGroupManager::Get_all_Groups()[GROUP_AUDIT_M20A] = saved;
    } );
    REQUIRE( saved.is_animal );
    REQUIRE( saved.is_safe );

    MonsterGroupManager::LoadMonsterGroup( json_object(
            R"({ "id": "GROUP_AUDIT_M20A", "monsters": [ { "monster": "mon_audit_m20a_watcher", "weight": 2 } ] })" ) );
    {
        const MonsterGroup &extended = MonsterGroupManager::GetMonsterGroup( GROUP_AUDIT_M20A );
        CHECK( extended.is_animal );
        CHECK( extended.is_safe );
        CHECK( extended.monsters.size() == saved.monsters.size() + 1 );
        CHECK( MonsterGroupManager::is_animal( GROUP_AUDIT_M20A ) );
    }

    // saying so still changes it
    MonsterGroupManager::LoadMonsterGroup( json_object(
            R"({ "id": "GROUP_AUDIT_M20A", "is_animal": false })" ) );
    {
        const MonsterGroup &changed = MonsterGroupManager::GetMonsterGroup( GROUP_AUDIT_M20A );
        CHECK_FALSE( changed.is_animal );
        CHECK( changed.is_safe );
    }

    // and an override starts over from the defaults
    MonsterGroupManager::LoadMonsterGroup( json_object(
            R"({ "id": "GROUP_AUDIT_M20A", "override": true, "monsters": [ { "monster": "mon_audit_m20a_watcher" } ] })" ) );
    {
        const MonsterGroup &replaced = MonsterGroupManager::GetMonsterGroup( GROUP_AUDIT_M20A );
        CHECK_FALSE( replaced.is_animal );
        CHECK_FALSE( replaced.is_safe );
        CHECK( replaced.monsters.size() == 1 );
    }
}

// MONDT-B14: monster loader quirks.
TEST_CASE( "mondt-b14_block_inherits_what_it_leaves_out", "[audit][monster]" )
{
    const monster_block_data &parent = mon_audit_m20a_block_parent->block;
    const monster_block_data &child = mon_audit_m20a_block_child->block;
    REQUIRE( parent.chance == 50 );
    CHECK( child.chance == 80 );
    CHECK( child.effectiveness == 30 );
    CHECK( child.count == 2 );
}

TEST_CASE( "mondt-b14_upgrades_read_their_own_spawn_range", "[audit][monster]" )
{
    // the parent, mon_test_non_shearable, spawns its upgrades within 5 tiles
    REQUIRE( mon_audit_m20a_keeps_range->upgrade_multi_range );
    CHECK( *mon_audit_m20a_keeps_range->upgrade_multi_range == 5 );
    // "multiple_spawns": false drops the inherited range
    CHECK_FALSE( mon_audit_m20a_single_spawn->upgrade_multi_range );
}

TEST_CASE( "mondt-b14_swim_skill_is_range_checked", "[audit][monster]" )
{
    move_skills_data skills;
    const std::string errors = capture_debugmsg_during( [&skills]() {
        skills.load( json_object( R"({ "swim": 15 })" ) );
    } );
    CHECK( errors.find( "swim value out of range" ) != std::string::npos );
    REQUIRE( skills.swim );
    CHECK( *skills.swim == 10 );
}

TEST_CASE( "mondt-b14_bash_skill_number", "[audit][monster]" )
{
    const std::map<damage_type_id, int> thirty = { { damage_bash, 30 } };
    CHECK( mon_audit_m20a_bash_number->bash_skill == thirty );
    // 0 is calculated as if it weren't given
    CHECK( mon_audit_m20a_bash_zero->bash_skill == mon_audit_m20a_watcher->bash_skill );
}

TEST_CASE( "mondt-b14_monster_adjustment_refuses_unknown_names", "[audit][monster]" )
{
    CHECK_THROWS_AS( load_monster_adjustment( json_object(
                         R"({ "species": "ZOMBIE", "stat": { "name": "sped", "modifier": 2 } })" ) ),
                     JsonError );
    CHECK_THROWS_AS( load_monster_adjustment( json_object(
                         R"({ "species": "ZOMBIE", "special": "xray" })" ) ), JsonError );
}

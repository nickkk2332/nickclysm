#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "damage.h"
#include "dialogue.h"
#include "dispersion.h"
#include "explosion.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json.h"
#include "json_loader.h"
#include "magic_enchantment.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "martialarts.h"
#include "math_parser.h"
#include "melee.h"
#include "monster.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "weakpoint.h"

static const ammotype ammo_66mm( "66mm" );
static const ammotype ammo_9mm( "9mm" );
static const ammotype ammo_flammable( "flammable" );

static const bionic_id bio_armor_arms( "bio_armor_arms" );
static const bionic_id bio_cqb( "bio_cqb" );
static const bionic_id bio_power_storage( "bio_power_storage" );

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );
static const damage_type_id damage_heat( "heat" );
static const damage_type_id damage_stab( "stab" );

static const efftype_id effect_stunned( "stunned" );

static const itype_id itype_bot_manhack( "bot_manhack" );
static const itype_id itype_crossbow( "crossbow" );
static const itype_id itype_flamethrower( "flamethrower" );
static const itype_id itype_m79( "m79" );
static const itype_id itype_test_m18c_gun( "test_m18c_gun" );

static const json_character_flag json_flag_ARMORED_ARMS( "ARMORED_ARMS" );
static const json_character_flag json_flag_CQB_LEARNING( "CQB_LEARNING" );
static const json_character_flag json_flag_FLASH_RESIST_ERRATIC( "FLASH_RESIST_ERRATIC" );
static const json_character_flag json_flag_FLASH_RESIST_RETRACTABLE( "FLASH_RESIST_RETRACTABLE" );
static const json_character_flag json_flag_NO_MELEE_PRACTICE( "NO_MELEE_PRACTICE" );

static const matype_id style_biojutsu( "style_biojutsu" );

static const mtype_id mon_manhack( "mon_manhack" );

static const skill_id skill_bashing( "bashing" );
static const skill_id skill_cooking( "cooking" );
static const skill_id skill_melee( "melee" );
static const skill_id skill_unarmed( "unarmed" );

static const species_id species_ROBOT( "ROBOT" );

static const trait_id trait_PER_SLIME( "PER_SLIME" );
static const trait_id trait_PER_SLIME_OK( "PER_SLIME_OK" );

namespace
{
// A cleared map with nobody on it but the avatar, before and after
struct m18c_scene {
    m18c_scene() {
        reset();
    }
    ~m18c_scene() {
        reset();
    }
    static void reset() {
        clear_vehicles();
        clear_map_without_vision();
        clear_creatures();
        clear_npcs();
        clear_avatar();
    }
};
} // namespace

static double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// Switches the installed bionic on or off without its activation side effects (the CQB
// bionic's style menu, for the avatar), and updates the enchantments as activation does.
// Its flags are cached: only activate_bionic() and deactivate_bionic() update them.
static void power_bionic( Character &guy, const bionic_id &id, bool on )
{
    bool found = false;
    for( bionic &bio : *guy.my_bionics ) {
        if( bio.id == id ) {
            bio.powered = on;
            found = true;
        }
    }
    REQUIRE( found );
    guy.recalculate_enchantment_cache();
}

static bionic &installed_bionic( Character &guy, const bionic_id &id )
{
    for( bionic &bio : *guy.my_bionics ) {
        if( bio.id == id ) {
            return bio;
        }
    }
    FAIL( "bionic not installed" );
    return ( *guy.my_bionics )[0];
}

// CMBT-J08: the CQB bionic's "treat my skills as 5" was hardcoded at a dozen sites, some as a
// floor and some as an override that dropped a skilled fighter to 5. It is now the bionic's
// enchantment "skill_floor", part of the skill level wherever it is used. An NPC turns it on:
// the avatar would get the style menu.
TEST_CASE( "cmbt-j08_cqb_skill_floor_raises_low_skills_only", "[audit][melee][bionics]" )
{
    m18c_scene scene;
    avatar &u = get_avatar();
    npc &guy = spawn_npc( ( u.pos_bub() + tripoint( 3, 0, 0 ) ).xy(), "test_talker" );
    guy.add_bionic( bio_power_storage );
    guy.add_bionic( bio_cqb );
    guy.set_power_level( guy.get_max_power_level() );
    guy.set_skill_level( skill_melee, 0 );
    guy.set_skill_level( skill_unarmed, 1 );
    guy.set_skill_level( skill_bashing, 8 );
    guy.set_skill_level( skill_cooking, 0 );
    REQUIRE( guy.get_skill_level( skill_melee ) == 0 );
    REQUIRE_FALSE( guy.has_active_bionic( bio_cqb ) );
    CHECK_FALSE( guy.has_flag( json_flag_NO_MELEE_PRACTICE ) );

    REQUIRE( guy.activate_bionic( installed_bionic( guy, bio_cqb ) ) );
    REQUIRE( guy.has_active_bionic( bio_cqb ) );
    CHECK( guy.get_skill_level( skill_melee ) == 5 );
    CHECK( guy.get_skill_level( skill_unarmed ) == 5 );
    // a higher skill stays what it is
    CHECK( guy.get_skill_level( skill_bashing ) == 8 );
    // skills the bionic doesn't cover are untouched
    CHECK( guy.get_skill_level( skill_cooking ) == 0 );
    // what used to be checks of the bionic are its flags
    CHECK( guy.has_flag( json_flag_NO_MELEE_PRACTICE ) );
    CHECK( guy.has_flag( json_flag_CQB_LEARNING ) );

    REQUIRE( guy.deactivate_bionic( installed_bionic( guy, bio_cqb ) ) );
    REQUIRE_FALSE( guy.has_active_bionic( bio_cqb ) );
    CHECK( guy.get_skill_level( skill_melee ) == 0 );
    CHECK_FALSE( guy.has_flag( json_flag_CQB_LEARNING ) );
    CHECK_FALSE( guy.has_flag( json_flag_NO_MELEE_PRACTICE ) );
}

// CMBT-J08: attack speed was one of the sites where the bionic overrode the skill, so a
// melee 10 fighter attacked as slowly as a melee 5 one with it on.
TEST_CASE( "cmbt-j08_cqb_does_not_slow_a_skilled_fighter", "[audit][melee][bionics]" )
{
    m18c_scene scene;
    avatar &u = get_avatar();
    u.add_bionic( bio_cqb );
    power_bionic( u, bio_cqb, false );
    u.set_skill_level( skill_melee, 10 );
    const int speed_off = u.attack_speed( null_item_reference() );

    power_bionic( u, bio_cqb, true );
    CHECK( u.get_skill_level( skill_melee ) == 10 );
    CHECK( u.attack_speed( null_item_reference() ) == speed_off );

    // and an unskilled one does attack faster with it, as before
    power_bionic( u, bio_cqb, false );
    u.set_skill_level( skill_melee, 0 );
    const int unskilled_off = u.attack_speed( null_item_reference() );
    power_bionic( u, bio_cqb, true );
    CHECK( u.attack_speed( null_item_reference() ) < unskilled_off );
}

// CMBT-J08: "skill_floor" in enchantment JSON: the highest floor counts, after the skill adds;
// a missing floor leaves even a negative skill alone; an item's saved enchantment keeps it.
TEST_CASE( "cmbt-j08_enchantment_skill_floor", "[audit][magic][enchantment]" )
{
    const JsonObject jo = json_loader::from_string(
                              R"({ "id": "test_m18c_floor", "has": "WORN", "condition": "ALWAYS",)"
                              R"( "skills": [ { "value": "unarmed", "add": 1 } ],)"
                              R"( "skill_floor": { "melee": 5, "unarmed": 3 } })" );
    enchantment ench;
    ench.load( jo );
    const JsonObject jo_low = json_loader::from_string(
                                  R"({ "id": "test_m18c_floor_low", "has": "WORN", "condition": "ALWAYS",)"
                                  R"( "skill_floor": { "melee": 2 } })" );
    enchantment ench_low;
    ench_low.load( jo_low );

    enchant_cache cache;
    cache.force_add( ench_low );
    cache.force_add( ench );
    CHECK( cache.get_skill_floor( skill_melee ) == 5 );
    CHECK( cache.modify_value( skill_melee, 2.0 ) == 5 );
    CHECK( cache.modify_value( skill_melee, 7.0 ) == 7 );
    // 1 + 1 is below 3; 4 + 1 is above it
    CHECK( cache.modify_value( skill_unarmed, 1.0 ) == 3 );
    CHECK( cache.modify_value( skill_unarmed, 4.0 ) == 5 );
    CHECK( cache.get_skill_floor( skill_cooking ) == 0 );
    CHECK( cache.modify_value( skill_cooking, -1.0 ) == -1 );

    std::ostringstream os;
    JsonOut jsout( os );
    cache.serialize( jsout );
    const JsonObject saved = json_loader::from_string( os.str() );
    enchant_cache loaded;
    loaded.load( saved );
    CHECK( loaded.get_skill_floor( skill_melee ) == 5 );
    CHECK( loaded.get_skill_floor( skill_unarmed ) == 3 );
}

// CMBT-J08: the martial-art fields that named the alloy plating bionics are character flags now;
// the bionics give the flags and the old fields add them.
TEST_CASE( "cmbt-j08_arm_block_char_flags", "[audit][melee][martial_arts]" )
{
    m18c_scene scene;
    REQUIRE( style_biojutsu.is_valid() );
    CHECK( style_biojutsu->arm_block_char_flags.count( json_flag_ARMORED_ARMS ) == 1 );

    avatar &u = get_avatar();
    u.set_skill_level( skill_unarmed, 0 );
    u.martial_arts_data->add_martialart( style_biojutsu );
    u.martial_arts_data->set_style( style_biojutsu, true );
    REQUIRE( u.martial_arts_data->selected_style() == style_biojutsu );
    // the style's arm_block is at unarmed 2
    CHECK_FALSE( u.martial_arts_data->can_arm_block( u ) );

    u.add_bionic( bio_armor_arms );
    CHECK( u.has_flag( json_flag_ARMORED_ARMS ) );
    CHECK( u.martial_arts_data->can_arm_block( u ) );
}

// CMBT-J09: gun sounds came from a list of ammotypes in C++, now each gun's or ammunition
// type's "fire_sound"; the shipped ones are unchanged.
TEST_CASE( "cmbt-j09_fire_sounds_come_from_json", "[audit][ranged]" )
{
    const item launcher( itype_m79 );
    const item::sound_data thunk = launcher.gun_noise( false );
    CHECK( thunk.volume == 8 );
    CHECK( thunk.sound == "Thunk!" );

    const item flamer( itype_flamethrower );
    const item::sound_data fwoosh = flamer.gun_noise( false );
    CHECK( fwoosh.volume == 4 );
    CHECK( fwoosh.sound == "Fwoosh!" );

    const item bow( itype_crossbow );
    CHECK( bow.gun_noise( false ).sound == "thonk!" );

    // a gun's own sound comes first, and has its burst
    const item gun( itype_test_m18c_gun );
    const item::sound_data single = gun.gun_noise( false );
    CHECK( single.volume == 33 );
    CHECK( single.sound == "Zap!" );
    const item::sound_data burst = gun.gun_noise( true );
    CHECK( burst.volume == 33 );
    CHECK( burst.sound == "Zap-zap!" );
}

// CMBT-J09: the action time had no field; pyromania looked up its own list of ammotypes.
TEST_CASE( "cmbt-j09_cycle_time_and_pyromania_ammo_types", "[audit][ranged]" )
{
    const itype &gun = *itype_test_m18c_gun;
    REQUIRE( gun.gun );
    REQUIRE( gun.gun->cycle_time.has_value() );
    CHECK( *gun.gun->cycle_time == 50 );
    // without it the action flags decide, as before
    REQUIRE( itype_m79->gun );
    CHECK_FALSE( itype_m79->gun->cycle_time.has_value() );

    CHECK( ammo_flammable->satisfies_pyromania() );
    CHECK_FALSE( ammo_flammable->pyromania_message().empty() );
    CHECK( ammo_66mm->satisfies_pyromania() );
    CHECK( ammo_66mm->pyromania_message().empty() );
    CHECK_FALSE( ammo_9mm->satisfies_pyromania() );
}

// CMBT-J10: EOCs couldn't read or set to-hit, dodge, criticals, blocks and dodges left or recoil.
TEST_CASE( "cmbt-j10_combat_state_math_for_characters", "[audit][math_parser][melee]" )
{
    m18c_scene scene;
    avatar &u = get_avatar();
    restore_on_out_of_scope restore_blocks( u.blocks_left );
    restore_on_out_of_scope restore_recoil( u.recoil );
    const int dodges_before = u.get_dodges_left();
    on_out_of_scope restore_dodges( [&u, dodges_before]() {
        u.set_dodges_left( dodges_before );
    } );
    dialogue d( get_talker_for( u ), nullptr );

    CHECK( eval_math( d, "u_melee_to_hit()" ) == Approx( u.get_melee_hit_base() ) );
    CHECK( eval_math( d, "u_dodge()" ) == Approx( u.get_dodge() ) );

    // asking about criticals doesn't count as attacking in the melee statistics
    melee::clear_stats();
    const double crit = eval_math( d, "u_crit_chance('dodge': 0)" );
    CHECK( melee::melee_stats.crit_count == 0 );
    CHECK( melee::melee_stats.double_crit_count == 0 );
    CHECK( crit >= 0.0 );
    CHECK( crit <= 1.0 );
    CHECK( crit == Approx( u.crit_chance( u.get_melee_hit_base(), 0.0f, null_item_reference() ) ) );
    melee::clear_stats();

    eval_math( d, "u_blocks_left() = 3" );
    CHECK( u.blocks_left == 3 );
    eval_math( d, "u_blocks_left()++" );
    CHECK( u.blocks_left == 4 );
    eval_math( d, "u_blocks_left() = -2" );
    CHECK( u.blocks_left == 0 );

    eval_math( d, "u_dodges_left() = 2" );
    CHECK( u.get_dodges_left() == 2 );
    CHECK( eval_math( d, "u_dodges_left()" ) == 2 );

    eval_math( d, "u_recoil() = 0" );
    CHECK( u.recoil == 0.0 );
    eval_math( d, "u_recoil() = 99999" );
    CHECK( u.recoil == MAX_RECOIL );
    CHECK( eval_math( d, "u_recoil()" ) == MAX_RECOIL );

    CHECK( eval_math( d, "u_weapon_dispersion()" ) == 0 );
    item gun( itype_test_m18c_gun );
    REQUIRE( u.wield( gun ) );
    const item_location wielded = u.get_wielded_item();
    REQUIRE( wielded );
    const double dispersion = u.get_weapon_dispersion( *wielded ).avg();
    CHECK( dispersion > 0.0 );
    CHECK( eval_math( d, "u_weapon_dispersion()" ) == Approx( dispersion ) );
}

// CMBT-J10: the same functions on a monster talker read and set what monsters have, and give 0
// where they have nothing (the legacy val('dodge') is monster-safe too).
TEST_CASE( "cmbt-j10_combat_state_math_for_monsters", "[audit][math_parser][monster]" )
{
    m18c_scene scene;
    avatar &u = get_avatar();
    monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint( 3, 0, 0 ) );
    dialogue d( get_talker_for( zombie ), nullptr );

    CHECK( eval_math( d, "u_dodge()" ) == Approx( zombie.get_dodge() ) );
    CHECK( eval_math( d, "u_val('dodge')" ) == Approx( zombie.get_dodge() ) );
    CHECK( eval_math( d, "u_melee_to_hit()" ) == Approx( zombie.get_hit() ) );
    CHECK( eval_math( d, "u_crit_chance()" ) == 0 );
    CHECK( eval_math( d, "u_recoil()" ) == 0 );
    CHECK( eval_math( d, "u_dodges_left()" ) == 0 );
    CHECK( eval_math( d, "u_weapon_dispersion()" ) == 0 );

    eval_math( d, "u_blocks_left() = 1" );
    CHECK( zombie.blocks_left == 1 );
    CHECK( eval_math( d, "u_blocks_left()" ) == 1 );
}

// CMBT-J11: the EMP blast drained power and fried gear only when the avatar stood there.
TEST_CASE( "cmbt-j11_emp_blast_drains_an_npc", "[audit][explosion][npc]" )
{
    m18c_scene scene;
    avatar &u = get_avatar();
    const tripoint_bub_ms where = u.pos_bub() + tripoint( 3, 0, 0 );
    npc &guy = spawn_npc( where.xy(), "test_talker" );
    guy.add_bionic( bio_power_storage );
    guy.set_power_level( guy.get_max_power_level() );
    const units::energy before = guy.get_power_level();
    REQUIRE( before > 0_kJ );
    REQUIRE( get_creature_tracker().creature_at<Character>( guy.pos_bub() ) == &guy );
    const units::energy avatar_before = u.get_power_level();

    explosion_handler::emp_blast( guy.pos_bub() );
    // between a third of it and all of it, at most 1000 kJ
    CHECK( guy.get_power_level() < before );
    CHECK( u.get_power_level() == avatar_before );
}

// CMBT-J11: a robot's chance to be switched off by an EMP came only from its size; now
// "emp_deactivate_one_in". 1 always switches it off, 0 never (it is fried instead).
TEST_CASE( "cmbt-j11_emp_deactivate_one_in", "[audit][explosion][monster]" )
{
    m18c_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms where = u.pos_bub() + tripoint( 4, 0, 0 );

    SECTION( "always" ) {
        monster &bot = spawn_test_monster( "mon_test_m18c_emp_bot", where );
        REQUIRE( bot.has_flag( mon_flag_ELECTRONIC ) );
        REQUIRE( bot.type->emp_deactivate_one_in == 1 );
        explosion_handler::emp_blast( where );
        CHECK( get_creature_tracker().creature_at<monster>( where ) == nullptr );
        CHECK( here.has_items( where ) );
        bool dropped = false;
        for( const item &it : here.i_at( where ) ) {
            dropped = dropped || it.typeId() == itype_bot_manhack;
        }
        CHECK( dropped );
    }
    SECTION( "never" ) {
        monster &bot = spawn_test_monster( "mon_test_m18c_hardened_bot", where );
        REQUIRE( bot.type->emp_deactivate_one_in == 0 );
        const int hp_before = bot.get_hp();
        explosion_handler::emp_blast( where );
        monster *still = get_creature_tracker().creature_at<monster>( where );
        REQUIRE( still == &bot );
        CHECK( bot.get_hp() < hp_before );
        for( const item &it : here.i_at( where ) ) {
            CHECK( it.typeId() != itype_bot_manhack );
        }
    }
    // a shipped monster without the field keeps the size rule
    CHECK( mon_manhack->emp_deactivate_one_in == -1 );
}

// CMBT-J11: the flashbang checked the ROBOT species and two traits in C++. Robots now have
// FLASHBANGPROOF through their species, and the traits give flags.
TEST_CASE( "cmbt-j11_flashbang_immunity_is_data", "[audit][explosion]" )
{
    for( const mtype &type : MonsterGenerator::generator().get_all_mtypes() ) {
        if( type.in_species( species_ROBOT ) ) {
            CAPTURE( type.id.str() );
            CHECK( type.has_flag( mon_flag_FLASHBANGPROOF ) );
        }
    }
    CHECK( trait_PER_SLIME->flags.count( json_flag_FLASH_RESIST_ERRATIC ) == 1 );
    CHECK( trait_PER_SLIME_OK->flags.count( json_flag_FLASH_RESIST_RETRACTABLE ) == 1 );

    m18c_scene scene;
    avatar &u = get_avatar();
    const tripoint_bub_ms center = u.pos_bub() + tripoint( 6, 0, 0 );
    monster &zombie = spawn_test_monster( "mon_zombie", center + tripoint::north );
    monster &bot = spawn_test_monster( "mon_manhack", center + tripoint::south );
    REQUIRE( bot.type->in_species( species_ROBOT ) );
    explosion_handler::flashbang( center, true, 8 );
    CHECK( zombie.has_effect( effect_stunned ) );
    CHECK_FALSE( bot.has_effect( effect_stunned ) );
}

// CMBT-J12: melee critical bonuses, the weak point attack type and embedding were tied to the
// bash, cut and stab ids; they are damage type fields now, with the old numbers on those three.
TEST_CASE( "cmbt-j12_damage_type_combat_fields", "[audit][melee][damage]" )
{
    const melee_crit_data &bash = damage_bash->melee_crit;
    CHECK( bash.damage_mult == 0.5 );
    CHECK( bash.armor_ignored == 0.5 );
    CHECK( bash.arpen == 0 );
    const melee_crit_data &cut = damage_cut->melee_crit;
    CHECK( cut.damage_mult == 0.25 );
    CHECK( cut.armor_ignored == 0.25 );
    CHECK( cut.arpen == 5 );
    const melee_crit_data &stab = damage_stab->melee_crit;
    CHECK( stab.damage_mult == 0 );
    CHECK( stab.damage_mult_per_skill == 0.1 );
    CHECK( stab.armor_ignored == 0.34 );
    // other types get no critical bonus, as before
    CHECK( damage_heat->melee_crit.damage_mult == 0 );
    CHECK( damage_heat->melee_crit.armor_ignored == 0 );

    CHECK( damage_bash->embed_weight == -1 );
    CHECK( damage_cut->embed_weight == 0.5 );
    CHECK( damage_stab->embed_weight == 1 );
    CHECK( damage_heat->embed_weight == 0 );

    using attack_type = weakpoint_attack::attack_type;
    CHECK( weakpoint_attack::type_of_melee_attack( damage_instance( damage_bash, 10.0f ) ) ==
           attack_type::MELEE_BASH );
    CHECK( weakpoint_attack::type_of_melee_attack( damage_instance( damage_cut, 10.0f ) ) ==
           attack_type::MELEE_CUT );
    CHECK( weakpoint_attack::type_of_melee_attack( damage_instance( damage_stab, 10.0f ) ) ==
           attack_type::MELEE_STAB );
    CHECK( weakpoint_attack::type_of_melee_attack( damage_instance( damage_heat, 10.0f ) ) ==
           attack_type::NONE );
    CHECK( weakpoint_attack::type_of_melee_attack( damage_instance() ) == attack_type::NONE );
}

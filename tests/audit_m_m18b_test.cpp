#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ammo_effect.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "explosion.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "gun_mode.h"
#include "item.h"
#include "item_location.h"
#include "json_error.h"
#include "json_loader.h"
#include "magic_enchantment.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "martialarts.h"
#include "melee.h"
#include "monster.h"
#include "npc.h"
#include "options.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "ranged.h"
#include "rng.h"
#include "type_id.h"

// Regression tests for the combat chapter's medium findings CMBT-J02 to CMBT-J07
// (Audit/18-combat.md)

static const ammo_effect_str_id ammo_effect_EXPLOSIVE_60mmHE( "EXPLOSIVE_60mmHE" );
static const ammo_effect_str_id ammo_effect_EXPLOSIVE_60mmHE2( "EXPLOSIVE_60mmHE2" );

static const bodypart_str_id body_part_debug_tail( "debug_tail" );

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );
static const damage_type_id damage_heat( "heat" );

static const effect_on_condition_id effect_on_condition_EOC_TEST_M18B_A( "EOC_TEST_M18B_A" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_M18B_B( "EOC_TEST_M18B_B" );

static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_shortbow( "shortbow" );
static const itype_id itype_test_m18b_block_club( "test_m18b_block_club" );
static const itype_id itype_test_m18b_legacy_club( "test_m18b_legacy_club" );

static const json_character_flag json_flag_CANNOT_USE_FIREARMS( "CANNOT_USE_FIREARMS" );
static const json_character_flag json_flag_CANNOT_USE_RANGED( "CANNOT_USE_RANGED" );

static const mabuff_id ma_buff_test_m18b_cond_buff( "test_m18b_cond_buff" );

static const matec_id test_tec_m18b_durations( "test_tec_m18b_durations" );

static const matype_id test_style_m18b( "test_style_m18b" );
static const matype_id test_style_m18b_deleted( "test_style_m18b_deleted" );
static const matype_id test_style_m18b_extended( "test_style_m18b_extended" );
static const matype_id test_style_m18b_replaced( "test_style_m18b_replaced" );

static const mtype_id mon_zombie( "mon_zombie" );

static const skill_id skill_melee( "melee" );

static const trait_id trait_BRAWLER( "BRAWLER" );
static const trait_id trait_DEBUG_TAIL( "DEBUG_TAIL" );
static const trait_id trait_GUNSHY( "GUNSHY" );
static const trait_id trait_TEST_M18B_PAWS( "TEST_M18B_PAWS" );

namespace
{

// Keeps the events of one type sent while it is subscribed
struct event_catcher : event_subscriber {
    using event_subscriber::notify;
    explicit event_catcher( event_type watched ) : type( watched ) {
        get_event_bus().subscribe( this );
    }
    event_type type;
    std::vector<cata::event> seen;
    void notify( const cata::event &e ) override {
        if( e.type() == type ) {
            seen.push_back( e );
        }
    }
};

void load_technique( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    ma_technique tec;
    tec.load( jo, "audit_m_m18b_test" );
}

explosion_data load_explosion( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    explosion_data ex;
    ex.deserialize( jo );
    return ex;
}

// A fresh avatar on a fresh map, put back afterwards
struct m18b_world {
    restore_on_out_of_scope<time_point> turn{ calendar::turn };
    m18b_world() {
        clear_map_without_vision();
        clear_creatures();
        clear_npcs();
        clear_avatar();
        set_time_to_day();
    }
    ~m18b_world() {
        clear_creatures();
        clear_npcs();
        clear_avatar();
        clear_map_without_vision();
    }
};

// Wields the club, ready to block: at melee 10 the reaction roll always succeeds
void ready_to_block( avatar &you, const itype_id &club )
{
    you.remove_weapon();
    item weapon( club );
    REQUIRE( you.wield( weapon ) );
    you.set_skill_level( skill_melee, 10 );
}

// Blocks the hit, rested, and returns what is left of it
damage_instance block( avatar &you, monster &attacker, const damage_instance &hit, bool &blocked )
{
    you.set_stamina( you.get_stamina_max() );
    you.blocks_left = 10;
    bodypart_id bp_hit = body_part_arm_l;
    damage_instance dam = hit;
    blocked = you.block_hit( &attacker, bp_hit, dam );
    return dam;
}

damage_instance block_with( avatar &you, monster &attacker, const itype_id &club,
                            const damage_instance &hit, bool &blocked )
{
    ready_to_block( you, club );
    return block( you, attacker, hit, blocked );
}

} // namespace

// CMBT-J02: "aoe" was a free string, so a value the docs named ("aoe") made the technique never
// proc, without an error.
TEST_CASE( "cmbt-j02_unknown_aoe_is_a_load_error", "[audit][martial_arts]" )
{
    CHECK_THROWS_AS( load_technique( R"({ "name": "test", "aoe": "aoe" })" ), JsonError );
    CHECK_THROWS_AS( load_technique( R"({ "name": "test", "aoe": "cone" })" ), JsonError );
    CHECK_NOTHROW( load_technique( R"({ "name": "test", "aoe": "spin" })" ) );
    CHECK_NOTHROW( load_technique( R"({ "name": "test", "aoe": "wide" })" ) );
    CHECK_NOTHROW( load_technique( R"({ "name": "test", "aoe": "impale" })" ) );
}

// CMBT-J03: BRAWLER and GUNSHY were trait checks on avatar-only paths, so NPCs fired guns
// regardless, and nothing else could forbid guns.  The traits now grant character flags that
// every way to fire checks.
TEST_CASE( "cmbt-j03_ranged_bans_are_character_flags", "[audit][ranged]" )
{
    m18b_world world;
    avatar &you = get_avatar();
    const item pistol( itype_glock_19 );
    const item bow( itype_shortbow );
    REQUIRE( pistol.is_firearm() );
    REQUIRE( bow.is_gun() );
    REQUIRE_FALSE( bow.is_firearm() );
    REQUIRE( you.ranged_ban_reason( pistol, true ).empty() );

    SECTION( "GUNSHY refuses firearms, held or not, but not bows" ) {
        you.set_mutation( trait_GUNSHY );
        CHECK( you.has_flag( json_flag_CANNOT_USE_FIREARMS ) );
        CHECK_FALSE( you.ranged_ban_reason( pistol, true ).empty() );
        CHECK_FALSE( you.ranged_ban_reason( pistol, false ).empty() );
        CHECK( you.ranged_ban_reason( bow, true ).empty() );
    }
    SECTION( "BRAWLER refuses every gun" ) {
        you.set_mutation( trait_BRAWLER );
        CHECK( you.has_flag( json_flag_CANNOT_USE_RANGED ) );
        CHECK_FALSE( you.ranged_ban_reason( bow, true ).empty() );
        CHECK_FALSE( you.ranged_ban_reason( pistol, false ).empty() );
    }
    SECTION( "paws stop a firearm in the hands only" ) {
        you.set_mutation( trait_TEST_M18B_PAWS );
        CHECK_FALSE( you.ranged_ban_reason( pistol, true ).empty() );
        CHECK( you.ranged_ban_reason( pistol, false ).empty() );
        CHECK( you.ranged_ban_reason( bow, true ).empty() );
    }
    SECTION( "the fire checks refuse a wielded firearm to GUNSHY and to paws" ) {
        item gun( itype_glock_19 );
        REQUIRE( you.wield( gun ) );
        const item_location wielded = you.get_wielded_item();
        REQUIRE( wielded );
        for( const trait_id &trait : {
                 trait_GUNSHY, trait_TEST_M18B_PAWS
             } ) {
            CAPTURE( trait.str() );
            you.set_mutation( trait );
            const std::string reason = you.ranged_ban_reason( *wielded, true );
            REQUIRE_FALSE( reason.empty() );
            std::vector<std::string> messages;
            CHECK_FALSE( gunmode_checks_common( you, get_map(), messages,
                                                wielded->gun_current_mode() ) );
            CHECK( std::find( messages.begin(), messages.end(), reason ) != messages.end() );
            you.unset_mutation( trait );
        }
    }
    SECTION( "paws don't stop a gun the character doesn't hold" ) {
        you.set_mutation( trait_TEST_M18B_PAWS );
        item loose( itype_glock_19 );
        std::vector<std::string> messages;
        gunmode_checks_common( you, get_map(), messages, loose.gun_current_mode() );
        CHECK( std::find( messages.begin(), messages.end(),
                          you.ranged_ban_reason( loose, true ) ) == messages.end() );
    }
}

// CMBT-J03: an NPC with GUNSHY rated a gun as a gun
TEST_CASE( "cmbt-j03_npcs_respect_the_ranged_bans", "[audit][ranged][npc]" )
{
    m18b_world world;
    const item pistol( itype_glock_19 );
    standard_npc bold( "Bold", get_avatar().pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    clear_character( bold );
    standard_npc shy( "Shy", get_avatar().pos_bub() + tripoint_rel_ms( 5, 0, 0 ) );
    clear_character( shy );
    shy.set_mutation( trait_GUNSHY );
    CHECK( bold.ranged_ban_reason( pistol, true ).empty() );
    CHECK_FALSE( shy.ranged_ban_reason( pistol, true ).empty() );
    // The pistol is worth something as a gun, but to the gun-shy NPC only as a club
    CHECK( shy.gun_value( pistol, 15 ) > 0.0 );
    CHECK( shy.evaluate_weapon( pistol, true ) == Approx( shy.melee_value( pistol ) ) );
}

// CMBT-J04: dodges and blocks had no event, the melee events went out before the attack was
// resolved, gunfire had no shot count and monster attacks no event at all
TEST_CASE( "cmbt-j04_dodge_and_block_events", "[audit][event][melee]" )
{
    m18b_world world;
    avatar &you = get_avatar();
    monster &attacker = spawn_test_monster( mon_zombie.str(), you.pos_bub() + tripoint::east );

    SECTION( "a dodge, with the attacker" ) {
        event_catcher dodges( event_type::character_dodges );
        REQUIRE( you.can_try_dodge( true ).success() );
        you.on_dodge( &attacker, 5.0f );
        REQUIRE( dodges.seen.size() == 1 );
        CHECK( dodges.seen[0].get<cata_variant_type::character_id>( "character" ) == you.getID() );
    }
    SECTION( "a block, with what blocked it" ) {
        event_catcher blocks( event_type::character_blocks );
        bool blocked = false;
        const damage_instance left = block_with( you, attacker, itype_test_m18b_block_club,
                                     damage_instance( damage_bash, 20.0f ), blocked );
        REQUIRE( blocked );
        REQUIRE( blocks.seen.size() == 1 );
        const cata::event &e = blocks.seen[0];
        CHECK( e.get<cata_variant_type::character_id>( "character" ) == you.getID() );
        CHECK( e.get<cata_variant_type::itype_id>( "weapon" ) == itype_test_m18b_block_club );
        CHECK( e.get<cata_variant_type::body_part>( "bodypart" ) == body_part_arm_l );
        CHECK( e.get<cata_variant_type::int_>( "damage_blocked" ) ==
               static_cast<int>( std::round( 20.0f - left.total_damage() ) ) );
    }
}

TEST_CASE( "cmbt-j04_melee_hit_event_comes_with_the_result", "[audit][event][melee]" )
{
    m18b_world world;
    avatar &you = get_avatar();
    monster &target = spawn_test_monster( mon_zombie.str(), you.pos_bub() + tripoint::east );
    rng_set_engine_seed( 4242 );
    event_catcher hits( event_type::character_melee_hits );
    int hp_before = target.get_hp();
    for( int tries = 0; tries < 200 && hits.seen.empty(); ++tries ) {
        hp_before = target.get_hp();
        you.set_moves( 100 );
        you.set_stamina( you.get_stamina_max() );
        you.melee_attack( target, false );
    }
    REQUIRE( hits.seen.size() == 1 );
    const cata::event &e = hits.seen[0];
    CHECK( e.get<cata_variant_type::character_id>( "attacker" ) == you.getID() );
    CHECK( e.get<cata_variant_type::string>( "technique" ) == "tec_none" );
    CHECK( e.get<cata_variant_type::int_>( "damage" ) == hp_before - target.get_hp() );
    CHECK_FALSE( e.get<cata_variant_type::bool_>( "killed" ) );
}

TEST_CASE( "cmbt-j04_gunfire_and_monster_attack_events", "[audit][event]" )
{
    m18b_world world;
    avatar &you = get_avatar();

    SECTION( "a gunshot at nothing: one shot, no hit" ) {
        rng_set_engine_seed( 4242 );
        event_catcher fired( event_type::character_fires_gun );
        for( int tries = 0; tries < 20 && fired.seen.empty(); ++tries ) {
            arm_shooter( you, itype_glock_19 );
            you.recoil = 0;
            you.fire_gun( get_map(), you.pos_bub() + tripoint_rel_ms( 4, 0, 0 ), 1,
                          *you.get_wielded_item() );
        }
        REQUIRE( fired.seen.size() == 1 );
        const cata::event &e = fired.seen[0];
        CHECK( e.get<cata_variant_type::character_id>( "character" ) == you.getID() );
        CHECK( e.get<cata_variant_type::itype_id>( "gun" ) == itype_glock_19 );
        CHECK( e.get<cata_variant_type::int_>( "shots" ) == 1 );
        CHECK( e.get<cata_variant_type::int_>( "hits" ) == 0 );
        CHECK_FALSE( e.get<cata_variant_type::itype_id>( "ammo" ).is_null() );
    }
    SECTION( "a monster's melee attack on the avatar" ) {
        monster &zombie = spawn_test_monster( mon_zombie.str(), you.pos_bub() + tripoint::east );
        REQUIRE( zombie.sees( get_map(), you ) );
        event_catcher attacks( event_type::monster_melee_attacks_character );
        zombie.melee_attack( you, 1000.0f );
        REQUIRE( attacks.seen.size() == 1 );
        const cata::event &e = attacks.seen[0];
        CHECK( e.get<cata_variant_type::mtype_id>( "monster" ) == mon_zombie );
        CHECK( e.get<cata_variant_type::character_id>( "victim" ) == you.getID() );
        CHECK_FALSE( e.get<cata_variant_type::bool_>( "killed" ) );
    }
}

// CMBT-J05: the int shrapnel shorthand, the object form, the struct and u_explosion each had
// their own defaults; a distance factor of 1 or more was caught only when it went off
TEST_CASE( "cmbt-j05_explosion_defaults_agree", "[audit][explosion]" )
{
    explosion_data code_made;
    CHECK( code_made.distance_factor == Approx( EXPLOSION_DEFAULT_DISTANCE_FACTOR ) );
    CHECK( code_made.shrapnel.fragment_mass == Approx( SHRAPNEL_DEFAULT_FRAGMENT_MASS ) );

    const explosion_data from_int = load_explosion( R"({ "power": 10, "shrapnel": 100 })" );
    const explosion_data from_object = load_explosion(
                                           R"({ "power": 10, "shrapnel": { "casing_mass": 100 } })" );
    CHECK( from_int.distance_factor == Approx( EXPLOSION_DEFAULT_DISTANCE_FACTOR ) );
    CHECK( from_int.shrapnel.casing_mass == 100 );
    CHECK( from_int.shrapnel.fragment_mass == Approx( from_object.shrapnel.fragment_mass ) );
    CHECK( from_object.shrapnel.fragment_mass == Approx( SHRAPNEL_DEFAULT_FRAGMENT_MASS ) );

    CHECK_THROWS_AS( load_explosion( R"({ "power": 10, "distance_factor": 1.0 })" ), JsonError );
    CHECK_THROWS_AS( load_explosion( R"({ "power": 10, "distance_factor": -0.5 })" ), JsonError );
    CHECK_THROWS_AS( load_explosion(
                         R"({ "power": 10, "shrapnel": { "casing_mass": 10, "fragment_mass": 0 } })" ), JsonError );
    CHECK_NOTHROW( load_explosion( R"({ "power": 10, "distance_factor": 0 })" ) );

    // The two 60 mm shells that used the shorthand keep their fragments
    for( const ammo_effect_str_id &shell : {
             ammo_effect_EXPLOSIVE_60mmHE, ammo_effect_EXPLOSIVE_60mmHE2
         } ) {
        CAPTURE( shell.str() );
        CHECK( shell->aoe_explosion_data.shrapnel.fragment_mass == Approx( 0.005f ) );
    }
}

// CMBT-J05: the blast hit six fixed human parts, and monsters took untyped damage
TEST_CASE( "cmbt-j05_blast_hits_every_main_part", "[audit][explosion]" )
{
    m18b_world world;
    avatar &you = get_avatar();
    // Force 4 on the tile it goes off on, and too weak to matter one tile away
    const explosion_data blast( 60.0f, 0.2f );

    SECTION( "a human's six main parts, as before" ) {
        explosion_handler::_make_explosion( &get_map(), nullptr, you.pos_bub(), blast );
        const std::vector<bodypart_id> parts = you.get_all_body_parts(
                get_body_part_flags::only_main );
        CHECK( parts.size() == 6 );
        for( const bodypart_id &bp : parts ) {
            CAPTURE( bp.id().str() );
            CHECK( you.get_part_hp_cur( bp ) < you.get_part_hp_max( bp ) );
        }
    }
    SECTION( "and a tail" ) {
        you.toggle_trait( trait_DEBUG_TAIL );
        REQUIRE( you.has_part( body_part_debug_tail ) );
        const int tail_hp = you.get_part_hp_cur( body_part_debug_tail );
        explosion_handler::_make_explosion( &get_map(), nullptr, you.pos_bub(), blast );
        CHECK( you.get_part_hp_cur( body_part_debug_tail ) < tail_hp );
    }
    SECTION( "a monster takes it as bash damage" ) {
        monster &zombie = spawn_test_monster( mon_zombie.str(),
                                              you.pos_bub() + tripoint_rel_ms( 8, 0, 0 ) );
        event_catcher hurt( event_type::monster_takes_damage );
        const int hp = zombie.get_hp();
        explosion_handler::_make_explosion( &get_map(), nullptr, zombie.pos_bub(), blast );
        REQUIRE( hurt.seen.size() == 1 );
        CHECK( hurt.seen[0].get<cata_variant_type::damage_type_id>( "damage_type" ) == damage_bash );
        const int dealt = hp - zombie.get_hp();
        CHECK( dealt == hurt.seen[0].get<cata_variant_type::int_>( "damage" ) );
        // 2 to 3 times the force of 4, less some armor
        CHECK( dealt > 0 );
        CHECK( dealt <= 12 );
    }
}

// CMBT-J06: buffs had no condition; EOC lists appended under copy-from and couldn't be extended
// or deleted from; tech effect durations were raw turns; inline buffs lost their mod
TEST_CASE( "cmbt-j06_buff_condition_gates_the_buff", "[audit][martial_arts]" )
{
    m18b_world world;
    avatar &you = get_avatar();
    global_variables &globals = get_globals();
    on_out_of_scope forget( [&globals]() {
        globals.remove_global_value( "test_m18b_buff_on" );
    } );
    you.martial_arts_data->add_martialart( test_style_m18b );
    you.martial_arts_data->set_style( test_style_m18b, false );
    const ma_buff &buff = ma_buff_test_m18b_cond_buff.obj();
    REQUIRE( buff.has_condition );

    globals.set_global_value( "test_m18b_buff_on", 0 );
    CHECK_FALSE( buff.is_valid_character( you ) );
    you.martial_arts_data->ma_static_effects( you );
    CHECK_FALSE( you.has_effect( buff.get_effect_id() ) );

    globals.set_global_value( "test_m18b_buff_on", 1 );
    CHECK( buff.is_valid_character( you ) );
    you.martial_arts_data->ma_static_effects( you );
    CHECK( you.has_effect( buff.get_effect_id() ) );

    CHECK( buff.get_description().find( "says so" ) != std::string::npos );
    // The inline buff is the style's mod's
    REQUIRE_FALSE( buff.src.empty() );
    CHECK( buff.src.back().second == mod_id( "test_data" ) );
}

TEST_CASE( "cmbt-j06_martial_art_lists_follow_copy_from", "[audit][martial_arts]" )
{
    using eoc_list = std::vector<effect_on_condition_id>;
    CHECK( test_style_m18b->onhit_eocs == eoc_list{ effect_on_condition_EOC_TEST_M18B_A } );
    CHECK( test_style_m18b_extended->onhit_eocs == eoc_list{
        effect_on_condition_EOC_TEST_M18B_A, effect_on_condition_EOC_TEST_M18B_B
    } );
    CHECK( test_style_m18b_replaced->onhit_eocs == eoc_list{ effect_on_condition_EOC_TEST_M18B_B } );
    CHECK( test_style_m18b_deleted->onhit_eocs == eoc_list{ effect_on_condition_EOC_TEST_M18B_B } );
    // the buffs are inherited
    CHECK( test_style_m18b_extended->static_buffs == test_style_m18b->static_buffs );

    JsonObject jo = json_loader::from_string(
                        R"({ "name": "x", "description": "x", "initiate": [ "a", "b" ], )"
                        R"("autolearn": [ [ "melee", 3 ], [ "unarmed", 2 ] ] })" );
    jo.allow_omitted_members();
    martialart style;
    style.load( jo, "audit_m_m18b_test" );
    const std::vector<std::pair<std::string, int>> autolearn = { { "melee", 3 }, { "unarmed", 2 } };
    CHECK( style.autolearn_skills == autolearn );
}

TEST_CASE( "cmbt-j06_tech_effect_durations", "[audit][martial_arts]" )
{
    const std::vector<tech_effect_data> &effects = test_tec_m18b_durations->tech_effects;
    REQUIRE( effects.size() == 2 );
    // "3 s" and 5 turns
    CHECK( effects[0].duration == 3 );
    CHECK( effects[1].duration == 5 );
    CHECK( effects[0].chance == 100 );
    CHECK( effects[0].on_damage );
    CHECK_FALSE( effects[0].permanent );
}

// CMBT-J07: block strength came only from the WBLOCK_1/2/3 techniques, elemental damage was cut to
// a fifth by any item, and the thresholds were constants
TEST_CASE( "cmbt-j07_item_block_data", "[audit][melee]" )
{
    m18b_world world;
    avatar &you = get_avatar();
    monster &attacker = spawn_test_monster( mon_zombie.str(), you.pos_bub() + tripoint::east );
    CHECK( melee::blocking_ability( item( itype_test_m18b_block_club ) ) == 6 );
    CHECK( melee::blocking_ability( item( itype_test_m18b_legacy_club ) ) == 6 );
    CHECK( melee::wielded_blocking_ability( item( itype_test_m18b_block_club ) ) == 6 );

    damage_instance hit( damage_bash, 20.0f );
    hit.add_damage( damage_cut, 10.0f );
    hit.add_damage( damage_heat, 10.0f );
    bool blocked_new = false;
    bool blocked_legacy = false;
    const damage_instance with_new = block_with( you, attacker, itype_test_m18b_block_club, hit,
                                     blocked_new );
    const damage_instance with_legacy = block_with( you, attacker, itype_test_m18b_legacy_club, hit,
                                        blocked_legacy );
    REQUIRE( blocked_new );
    REQUIRE( blocked_legacy );
    // The same strength, so the same block of bash
    CHECK( with_new.type_damage( damage_bash ) == Approx( with_legacy.type_damage( damage_bash ) ) );
    CHECK( with_legacy.type_damage( damage_bash ) < 20.0f );
    // "effectiveness": { "cut": 0 }
    CHECK( with_new.type_damage( damage_cut ) == Approx( 10.0f ) );
    CHECK( with_legacy.type_damage( damage_cut ) < 10.0f );
    // "elemental_mult": 0.5, against the legacy fifth
    CHECK( with_new.type_damage( damage_heat ) == Approx( 5.0f ) );
    CHECK( with_legacy.type_damage( damage_heat ) == Approx( 2.0f ) );
}

TEST_CASE( "cmbt-j07_block_options_and_enchantments", "[audit][melee][magic]" )
{
    m18b_world world;
    avatar &you = get_avatar();
    monster &attacker = spawn_test_monster( mon_zombie.str(), you.pos_bub() + tripoint::east );
    const damage_instance hit( damage_bash, 20.0f );
    bool blocked = false;
    // Wielding recalculates the enchantments, so the values below are added after it
    ready_to_block( you, itype_test_m18b_block_club );
    on_out_of_scope recalc( [&you]() {
        you.recalculate_enchantment_cache();
    } );

    SECTION( "too little stamina" ) {
        const int min_stamina = get_option<int>( "BLOCK_MIN_STAMINA" );
        CHECK( min_stamina == 2000 );
        REQUIRE( min_stamina < you.get_stamina_max() );
        you.blocks_left = 10;
        you.set_stamina( min_stamina - 1 );
        bodypart_id bp_hit = body_part_arm_l;
        damage_instance dam = hit;
        CHECK_FALSE( you.block_hit( &attacker, bp_hit, dam ) );
        you.set_stamina( min_stamina );
        CHECK( you.block_hit( &attacker, bp_hit, dam ) );
    }
    SECTION( "BLOCK_EFFECTIVENESS raises the block score" ) {
        const damage_instance plain = block( you, attacker, hit, blocked );
        REQUIRE( blocked );
        REQUIRE( plain.type_damage( damage_bash ) > 0.0f );
        // a score of 40 or more blocks everything
        you.enchantment_cache->add_value_add( enchant_vals::mod::BLOCK_EFFECTIVENESS, 100.0f );
        const damage_instance strong = block( you, attacker, hit, blocked );
        REQUIRE( blocked );
        CHECK( strong.type_damage( damage_bash ) == Approx( 0.0f ) );
    }
    SECTION( "BLOCK_STAMINA_COST can make a block free" ) {
        block( you, attacker, hit, blocked );
        REQUIRE( blocked );
        CHECK( you.get_stamina() < you.get_stamina_max() );
        you.enchantment_cache->add_value_mult( enchant_vals::mod::BLOCK_STAMINA_COST, -1.0f );
        block( you, attacker, hit, blocked );
        REQUIRE( blocked );
        CHECK( you.get_stamina() == you.get_stamina_max() );
    }
}

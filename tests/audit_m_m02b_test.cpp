#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "clzones.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "effect_on_condition.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_jmath.h"
#include "math_parser_type.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "string_formatter.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const activity_id ACT_AUDIT_M02B( "ACT_AUDIT_M02B" );

static const effect_on_condition_id effect_on_condition_EOC_AUDIT_M02B_RUN_ZONES(
    "EOC_AUDIT_M02B_RUN_ZONES" );

static const efftype_id effect_npc_suspend( "npc_suspend" );

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_apple( "apple" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_test_9mm( "test_9mm" );
static const itype_id itype_test_battery_disposable( "test_battery_disposable" );
static const itype_id itype_test_glock( "test_glock" );
static const itype_id itype_test_rock( "test_rock" );

static const mfaction_str_id monfaction_bee( "bee" );

static const vpart_id vpart_controls( "controls" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_seat( "seat" );

static const vproto_id vehicle_prototype_none( "none" );

static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );
static const zone_type_id zone_type_LOOT_WOOD( "LOOT_WOOD" );

static double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

static std::string mutator_value( const_dialogue const &d, const std::string &mutator )
{
    const JsonObject jo = json_loader::from_string( string_format(
                              R"({ "s": { "mutator": "%s" } })", mutator ) );
    const str_or_var s = get_str_or_var( jo.get_member( "s" ), "s" );
    return s.evaluate( d );
}

static bool check_condition( const_dialogue const &d, const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition( d );
}

// MATH-J05: wetness could be added but not read, warmth() was read-only, and the current
// activity could only be checked, not read.
TEST_CASE( "math-j05_wetness_warmth_and_activity_of_a_character", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), nullptr );

    const int capacity = u.get_part_drench_capacity( body_part_torso );
    REQUIRE( capacity > 0 );
    CHECK( eval_math( d, "u_wetness('torso')" ) == 0 );
    eval_math( d, "u_wetness('torso') = 100" );
    CHECK( u.get_part_wetness( body_part_torso ) == capacity );
    CHECK( eval_math( d, "u_wetness('torso')" ) == Approx( 100 ) );
    eval_math( d, "u_wetness('torso') = 0" );
    CHECK( u.get_part_wetness( body_part_torso ) == 0 );
    // past what the part holds, or below dry, it stops there
    eval_math( d, "u_wetness('torso') = 500" );
    CHECK( u.get_part_wetness( body_part_torso ) == capacity );
    eval_math( d, "u_wetness('torso') = -20" );
    CHECK( u.get_part_wetness( body_part_torso ) == 0 );
    CHECK_THROWS_AS( eval_math( d, "u_wetness('audit_no_such_part')" ), math::runtime_error );

    // warmth() sets the part's current temperature, in its own unit
    eval_math( d, "u_warmth('torso') = 4000" );
    CHECK( u.get_part_temp_cur( body_part_torso ) == units::from_legacy_bodypart_temp( 4000 ) );
    eval_math( d, "u_warmth('torso') = 6000" );
    CHECK( u.get_part_temp_cur( body_part_torso ) == units::from_legacy_bodypart_temp( 6000 ) );

    // the activity's id as a string
    CHECK( mutator_value( d, "u_activity" ).empty() );
    u.assign_activity( ACT_AUDIT_M02B, 1000 );
    REQUIRE( u.activity.id() == ACT_AUDIT_M02B );
    CHECK( mutator_value( d, "u_activity" ) == "ACT_AUDIT_M02B" );
    u.cancel_activity();
    CHECK( mutator_value( d, "u_activity" ).empty() );
}

// MATH-J06: n_pain() = x and n_hp('torso') = x did nothing on a monster, monsters_nearby()
// counted hallucinations, could only tell friendly from not, and no EOC read a monster's
// own faction or whether it was a hallucination.
TEST_CASE( "math-j06_monster_pain_hp_faction_and_hallucination", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_creatures();
    } );
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint::east * 3 );
    dialogue d( get_talker_for( u ), get_talker_for( zed ) );

    eval_math( d, "n_pain() = 12" );
    CHECK( zed.get_pain() == 12 );
    eval_math( d, "n_pain() = 0" );
    CHECK( zed.get_pain() == 0 );
    REQUIRE( zed.get_hp_max() > 10 );
    eval_math( d, "n_hp('torso') = 10" );
    CHECK( zed.get_hp() == 10 );
    CHECK( eval_math( d, "n_hp('torso')" ) == 10 );

    CHECK( eval_math( d, "n_is_hallucination()" ) == 0 );
    CHECK( eval_math( d, "u_is_hallucination()" ) == 0 );
    zed.hallucination = true;
    CHECK( eval_math( d, "n_is_hallucination()" ) == 1 );
    zed.hallucination = false;

    // the monster's own faction, which can differ from its type's
    const std::string type_faction = zed.type->default_faction.str();
    CHECK( mutator_value( d, "npc_monster_faction" ) == type_faction );
    zed.faction = monfaction_bee;
    CHECK( mutator_value( d, "npc_monster_faction" ) == "bee" );
    CHECK( mutator_value( d, "u_monster_faction" ).empty() );
}

TEST_CASE( "math-j06_monsters_nearby_hallucinations_and_attitudes", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    on_out_of_scope cleanup( []() {
        clear_creatures();
    } );
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );
    // one that attacks the avatar, one that ignores it, and a hallucination
    monster &angry = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint::east * 3 );
    angry.anger = 100;
    angry.morale = 100;
    angry.aggro_character = true;
    monster &calm = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint::west * 3 );
    calm.aggro_character = false;
    monster &fake = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint::north * 3 );
    fake.hallucination = true;
    REQUIRE( angry.attitude_to( u ) == Creature::Attitude::HOSTILE );
    REQUIRE( calm.attitude_to( u ) == Creature::Attitude::NEUTRAL );

    CHECK( eval_math( d, "u_monsters_nearby('mon_zombie', 'attitude': 'both')" ) == 2 );
    CHECK( eval_math( d, "u_monsters_nearby('mon_zombie', 'attitude': 'both', "
                      "'allow_hallucinations': 1)" ) == 3 );
    CHECK( eval_math( d, "u_mon_species_nearby('ZOMBIE', 'allow_hallucinations': 1)" ) == 3 );
    // "hostile" is still anything not friendly; "enemy" and "neutral" go by the attitude
    CHECK( eval_math( d, "u_monsters_nearby('mon_zombie')" ) == 2 );
    CHECK( eval_math( d, "u_monsters_nearby('mon_zombie', 'attitude': 'enemy')" ) == 1 );
    CHECK( eval_math( d, "u_monsters_nearby('mon_zombie', 'attitude': 'neutral')" ) == 1 );
    calm.aggro_character = true;
    calm.anger = 100;
    calm.morale = 100;
    REQUIRE( calm.attitude_to( u ) == Creature::Attitude::HOSTILE );
    CHECK( eval_math( d, "u_monsters_nearby('mon_zombie', 'attitude': 'enemy')" ) == 2 );
    CHECK( eval_math( d, "u_monsters_nearby('mon_zombie', 'attitude': 'neutral')" ) == 0 );
    CHECK_THROWS_AS( eval_math( d, "u_monsters_nearby('attitude': 'grumpy')" ),
                     math::runtime_error );
}

// MATH-J07: an item's power counted a gun's rounds, val('count') couldn't be set, and there was
// no way to read an item's temperature, rot or loaded ammo.
TEST_CASE( "math-j07_item_power_count_rot_temperature_and_ammo", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint::east;
    on_out_of_scope cleanup( [spot]() {
        get_map().i_clear( spot );
    } );

    SECTION( "power is battery charge" ) {
        item gun( itype_test_glock );
        gun.ammo_set( gun.ammo_default() );
        const int rounds = gun.ammo_remaining();
        REQUIRE( rounds > 0 );
        item_location gun_loc = here.add_item_or_charges_ret_loc( spot, gun );
        REQUIRE( gun_loc );
        dialogue d( get_talker_for( u ), get_talker_for( gun_loc ) );
        CHECK( eval_math( d, "n_val('power')" ) == 0 );
        CHECK( eval_math( d, "n_val('power_max')" ) == 0 );
        CHECK( eval_math( d, "n_ammo_count()" ) == rounds );
        CHECK( mutator_value( d, "npc_loaded_ammo" ) == gun_loc->ammo_current().str() );

        item cell( itype_test_battery_disposable );
        cell.ammo_set( itype_battery, 100 );
        REQUIRE( cell.ammo_remaining() == 100 );
        item_location cell_loc = here.add_item_or_charges_ret_loc( spot, cell );
        REQUIRE( cell_loc );
        dialogue dc( get_talker_for( u ), get_talker_for( cell_loc ) );
        CHECK( eval_math( dc, "n_val('power')" ) == 100 );
        CHECK( eval_math( dc, "n_val('power_max')" ) == 300 );
        eval_math( dc, "n_val('power') = 250" );
        CHECK( cell_loc->ammo_remaining() == 250 );
        CHECK( cell_loc->ammo_current() == itype_battery );
        CHECK( eval_math( dc, "n_ammo_count()" ) == 250 );
    }

    SECTION( "count of a stack" ) {
        item_location rounds = here.add_item_or_charges_ret_loc( spot, item( itype_test_9mm,
                               calendar::turn, 10 ) );
        REQUIRE( rounds );
        REQUIRE( rounds->charges == 10 );
        dialogue d( get_talker_for( u ), get_talker_for( rounds ) );
        CHECK( eval_math( d, "n_val('count')" ) == 10 );
        eval_math( d, "n_val('count') = 4" );
        CHECK( rounds->charges == 4 );
        eval_math( d, "n_val('count') -= 10" );
        CHECK( rounds->charges == 1 );
        // an item that isn't counted by charges can't be set
        item_location cell = here.add_item_or_charges_ret_loc( spot,
                             item( itype_test_battery_disposable ) );
        REQUIRE( cell );
        REQUIRE_FALSE( cell->count_by_charges() );
        dialogue dc( get_talker_for( u ), get_talker_for( cell ) );
        CHECK( eval_math( dc, "n_val('count')" ) == 1 );
        CHECK_THROWS_AS( eval_math( dc, "n_val('count') = 3" ), math::runtime_error );
    }

    SECTION( "rot and temperature" ) {
        item_location apple = here.add_item_or_charges_ret_loc( spot, item( itype_apple ) );
        REQUIRE( apple );
        REQUIRE( apple->goes_bad() );
        REQUIRE( apple->has_temperature() );
        dialogue d( get_talker_for( u ), get_talker_for( apple ) );

        eval_math( d, "n_rot() = 3600" );
        CHECK( apple->get_rot() == 1_hours );
        CHECK( eval_math( d, "n_rot()" ) == 3600 );
        eval_math( d, "n_rot('format': 'relative') = 0.5" );
        CHECK( apple->get_relative_rot() == Approx( 0.5 ).margin( 0.01 ) );
        CHECK( eval_math( d, "n_rot('format': 'relative')" ) == Approx( 0.5 ).margin( 0.01 ) );
        CHECK_THROWS_AS( eval_math( d, "n_rot('format': 'stale')" ), math::runtime_error );

        eval_math( d, "n_item_temp() = 300" );
        CHECK( units::to_kelvin( apple->temperature ) == Approx( 300 ).margin( 0.01 ) );
        CHECK( eval_math( d, "n_item_temp()" ) == Approx( 300 ).margin( 0.01 ) );

        // a rock neither spoils nor keeps a temperature
        item_location rock = here.add_item_or_charges_ret_loc( spot, item( itype_test_rock ) );
        REQUIRE( rock );
        dialogue dr( get_talker_for( u ), get_talker_for( rock ) );
        CHECK( eval_math( dr, "n_rot()" ) == 0 );
        CHECK( eval_math( dr, "n_item_temp()" ) == 0 );
        // the avatar isn't an item
        CHECK_THROWS_AS( eval_math( dr, "u_rot()" ), math::runtime_error );
    }
}

// MATH-J08: a zone talker only had a position: no name, no variables, no type filter, and the
// documented variable zone_range was read only as a literal int.
TEST_CASE( "math-j08_zone_talker_name_vars_types_and_corners", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    avatar &u = get_avatar();
    zone_manager &mgr = zone_manager::get_manager();
    global_variables &globals = get_globals();
    on_out_of_scope cleanup( [&mgr, &globals]() {
        mgr.clear();
        mgr.cache_data();
        globals.clear_global_values();
    } );
    mgr.clear();
    globals.clear_global_values();

    const tripoint_abs_ms near_start = u.pos_abs() + tripoint::east * 2;
    const tripoint_abs_ms near_end = near_start + tripoint( 3, 1, 0 );
    mgr.add( "audit wood", zone_type_LOOT_WOOD, faction_your_followers, false, true, near_start,
             near_end, nullptr, true, &here );
    mgr.add( "audit far wood", zone_type_LOOT_WOOD, faction_your_followers, false, true,
             u.pos_abs() + tripoint::east * 30, u.pos_abs() + tripoint::east * 31, nullptr, true, &here );
    mgr.add( "audit unsorted", zone_type_LOOT_UNSORTED, faction_your_followers, false, true,
             near_start, near_end, nullptr, true, &here );

    // only the wood zones, and only those in range
    dialogue d( get_talker_for( u ), nullptr );
    globals.set_global_value( "audit_m02b_zone_range", 10 );
    effect_on_condition_EOC_AUDIT_M02B_RUN_ZONES->activate( d );
    CHECK( globals.get_global_value( "audit_m02b_zone_count" ) == 1 );
    CHECK( globals.get_global_value( "audit_m02b_zone_type" ) == "LOOT_WOOD" );
    CHECK( globals.get_global_value( "audit_m02b_zone_end_x" ) == 3 );
    globals.set_global_value( "audit_m02b_zone_range", 100 );
    effect_on_condition_EOC_AUDIT_M02B_RUN_ZONES->activate( d );
    CHECK( globals.get_global_value( "audit_m02b_zone_count" ) == 3 );

    // the zone keeps the variable, and saves it
    const zone_data *wood = nullptr;
    for( const zone_data &zone : mgr.get_zones() ) {
        if( zone.get_name() == "audit wood" ) {
            wood = &zone;
        }
    }
    REQUIRE( wood != nullptr );
    std::unique_ptr<const_talker> wood_talker = get_talker_for( *wood );
    CHECK( wood_talker->get_name() == "audit wood" );
    REQUIRE( wood->maybe_get_value( "audit_m02b_seen" ) != nullptr );
    CHECK( *wood->maybe_get_value( "audit_m02b_seen" ) == 7 );
    std::ostringstream saved;
    JsonOut jsout( saved );
    wood->serialize( jsout );
    zone_data loaded;
    loaded.deserialize( json_loader::from_string( saved.str() ) );
    REQUIRE( loaded.maybe_get_value( "audit_m02b_seen" ) != nullptr );
    CHECK( *loaded.maybe_get_value( "audit_m02b_seen" ) == 7 );

    // the conditions
    const_dialogue dz( get_talker_for( *wood ), nullptr );
    CHECK( check_condition( dz, R"({ "condition": "u_is_zone" })" ) );
    CHECK_FALSE( check_condition( d, R"({ "condition": "u_is_zone" })" ) );
    CHECK_FALSE( check_condition( d, R"({ "condition": "u_is_computer" })" ) );
    CHECK( mutator_value( d, "u_zone_type" ).empty() );
}

// MATH-J09: u_is_driven only looked at the avatar.
TEST_CASE( "math-j09_a_vehicle_an_npc_drives_is_driven", "[audit][math_parser][vehicle]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    clear_npcs();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint::east * 4;
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, spot, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) >= 0 );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_seat ) >= 0 );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_controls ) >= 0 );
    here.add_vehicle_to_cache( veh );
    npc &driver = spawn_npc( spot.xy(), "thug" );
    on_out_of_scope cleanup( [&driver]() {
        map &m = get_map();
        driver.controlling_vehicle = false;
        if( driver.in_vehicle ) {
            m.unboard_vehicle( driver.pos_bub() );
        }
        clear_npcs();
        clear_vehicles();
    } );
    REQUIRE( driver.pos_bub() == spot );
    here.board_vehicle( spot, &driver );
    REQUIRE( driver.in_vehicle );

    dialogue d( get_talker_for( *veh ), nullptr );
    CHECK_FALSE( check_condition( d, R"({ "condition": "u_is_driven" })" ) );
    driver.controlling_vehicle = true;
    CHECK( check_condition( d, R"({ "condition": "u_is_driven" })" ) );
}

// MATH-J10: the topics a conversation opens on were hardcoded; a JSON dialogue_topic_override
// adds one, and can turn a built-in rule off.
TEST_CASE( "math-j10_dialogue_topic_override_adds_and_replaces_topics", "[audit][npc]" )
{
    clear_avatar();
    clear_map();
    global_variables &globals = get_globals();
    globals.clear_global_values();
    on_out_of_scope cleanup( [&globals]() {
        globals.clear_global_values();
    } );
    avatar &u = get_avatar();
    standard_npc guy( "Talker", u.pos_bub() + tripoint::east * 2 );
    clear_character( guy );
    guy.add_effect( effect_npc_suspend, 1_hours );
    REQUIRE( guy.has_effect( effect_npc_suspend ) );
    std::unique_ptr<talker> guy_talker = get_talker_for( guy );

    const std::vector<std::string> plain = guy_talker->get_topics( false );
    CHECK( std::count( plain.begin(), plain.end(), "TALK_REBOOT" ) == 1 );
    CHECK( std::count( plain.begin(), plain.end(), "TALK_AUDIT_M02B_OVERRIDE" ) == 0 );

    // data/mods/TEST_DATA/audit_m_m02b.json: while this holds, its topic comes first and the
    // reboot rule is off
    globals.set_global_value( "audit_m02b_override", 1 );
    const std::vector<std::string> overridden = guy_talker->get_topics( false );
    REQUIRE_FALSE( overridden.empty() );
    CHECK( overridden.back() == "TALK_AUDIT_M02B_OVERRIDE" );
    CHECK( std::count( overridden.begin(), overridden.end(), "TALK_REBOOT" ) == 0 );
}

// MATH-J11: jmath functions took numbers only, couldn't be called for n, couldn't be assigned
// to, and weren't checked for parameters they don't have or names they hide.
TEST_CASE( "math-j11_jmath_strings_scopes_and_assignment", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    standard_npc guy( "Strong", u.pos_bub() + tripoint::east * 2 );
    clear_character( guy );
    u.set_str_base( 8 );
    guy.set_str_base( 12 );
    REQUIRE( u.get_str() == 8 );
    REQUIRE( guy.get_str() == 12 );
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), get_talker_for( guy ) );

    // string and array arguments
    CHECK( eval_math( d, "audit_m02b_stat('strength')" ) == 8 );
    CHECK( eval_math( d, "audit_m02b_len(['ab', 'cde'])" ) == 5 );
    // the scope swaps the talkers
    CHECK( eval_math( d, "audit_m02b_strength()" ) == 92 );
    CHECK( eval_math( d, "u_audit_m02b_strength()" ) == 92 );
    CHECK( eval_math( d, "n_audit_m02b_strength()" ) == 128 );
    CHECK( eval_math( d, "n_audit_m02b_stat('strength')" ) == 12 );
    // without a parenthesis it is still a variable
    CHECK( eval_math( d, "n_audit_m02b_strength" ) == 0 );

    // assignment through the "assign" body
    eval_math( d, "audit_m02b_stat('strength') = 10" );
    CHECK( u.get_str() == 10 );
    eval_math( d, "n_audit_m02b_stat('strength') += 2" );
    CHECK( guy.get_str() == 14 );
    CHECK( guy.get_str_base() == 14 );

    math_exp exp;
    // a function without "assign" can't be assigned to, and none takes kwargs
    CHECK_THROWS_AS( exp.parse( "audit_m02b_strength() = 3", false ), math::exception );
    CHECK_THROWS_AS( exp.parse( "audit_m02b_stat('strength', 'x': 1)", false ), math::exception );
    // without a beta talker, n_ is an error
    dialogue alone( get_talker_for( u ), nullptr );
    CHECK_THROWS_AS( eval_math( alone, "n_audit_m02b_strength()" ), math::runtime_error );
}

static void load_jmath( const std::string &id, const std::string &json )
{
    jmath_func f;
    f.id = jmath_func_id( id );
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    f.load( jo, "audit_m_m02b_test" );
}

TEST_CASE( "math-j11_jmath_load_checks", "[audit][math_parser]" )
{
    CHECK_NOTHROW( load_jmath( "audit_m02b_ok", R"({ "num_args": 2, "return": "_0 + _1" })" ) );
    // _1 of a one-argument function
    CHECK_THROWS_AS( load_jmath( "audit_m02b_bad", R"({ "num_args": 1, "return": "_0 + _1" })" ),
                     JsonError );
    CHECK_THROWS_AS( load_jmath( "audit_m02b_bad", R"({ "num_args": 0, "return": "1",
        "assign": "u_val('strength') = _val + _0" })" ), JsonError );
    // the name of a built-in function, a constant, a dialogue function or a scoped one
    for( const std::string &name : {
             std::string( "min" ), std::string( "pi" ), std::string( "skill" ), std::string( "u_skill" )
         } ) {
        CAPTURE( name );
        CHECK_THROWS_AS( load_jmath( name, R"({ "num_args": 0, "return": "1" })" ), JsonError );
    }
}

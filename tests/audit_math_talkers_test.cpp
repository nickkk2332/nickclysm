#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "clzones.h"
#include "computer.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "grapple.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "math_parser.h"
#include "math_parser_type.h"
#include "monster.h"
#include "npc.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "string_formatter.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_MATH_B05_GLOBAL( "EOC_AUDIT_MATH_B05_GLOBAL" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_MATH_B05_LOCAL( "EOC_AUDIT_MATH_B05_LOCAL" );

static const efftype_id effect_riding( "riding" );

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_test_rock( "test_rock" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_open_air( "t_open_air" );
static const ter_str_id ter_t_rock( "t_rock" );

static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_seat( "seat" );

static const vproto_id vehicle_prototype_none( "none" );

static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );

static double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// MATH-B03: the faction_*() functions dereferenced the null pointer faction_manager::get()
// returns for an unknown id, so a typo or a stale variable crashed the game.
TEST_CASE( "math-b03_faction_functions_reject_unknown_ids", "[audit][math_parser]" )
{
    clear_avatar();
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const std::vector<std::string> readable = { "faction_like", "faction_respect", "faction_trust",
                                                "faction_food_supply", "faction_wealth", "faction_power", "faction_size"
                                              };
    for( const std::string &fn : readable ) {
        CAPTURE( fn );
        CHECK_THROWS_AS( eval_math( d, fn + "('audit_no_such_faction')" ), math::runtime_error );
        if( fn != "faction_food_supply" ) {
            CHECK_THROWS_AS( eval_math( d, fn + "('audit_no_such_faction') = 5" ),
                             math::runtime_error );
        }
    }

    // A faction that exists still reads and writes.
    g->faction_manager_ptr->create_if_needed();
    faction *followers = g->faction_manager_ptr->get( faction_your_followers );
    REQUIRE( followers != nullptr );
    const restore_on_out_of_scope<int> restore_like( followers->likes_u );
    eval_math( d, "faction_like('your_followers') = 7" );
    CHECK( followers->likes_u == 7 );
    CHECK( eval_math( d, "faction_like('your_followers')" ) == 7 );
}

// MATH-B04: an assignment to time('now', 'unit':...) or calories('format':'percent') ignored the
// kwarg, so time('now', 'unit':'hours') += 1 set the calendar to about 2201 turns and
// u_calories('format':'percent') = 50 set 50 kcal.
TEST_CASE( "math-b04_assignments_honour_the_read_kwargs", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );

    SECTION( "time('now') in a unit" ) {
        const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
        calendar::turn = calendar::turn_zero + 10_days + 3_hours;
        eval_math( d, "time('now', 'unit':'hours') += 1" );
        CHECK( calendar::turn == calendar::turn_zero + 10_days + 4_hours );
        eval_math( d, "time('now', 'unit':'days') = 20" );
        CHECK( calendar::turn == calendar::turn_zero + 20_days );
        eval_math( d, "time('now') = 90" );
        CHECK( calendar::turn == calendar::turn_zero + 90_seconds );
        // Off a whole hour: 15:00:04 in hours, plus one, times 3600 lands a hair below 16:00:04.
        calendar::turn = calendar::turn_zero + 10_days + 15_hours + 4_seconds;
        eval_math( d, "time('now', 'unit':'hours') += 1" );
        CHECK( calendar::turn == calendar::turn_zero + 10_days + 16_hours + 4_seconds );
    }

    SECTION( "calories() as a percent" ) {
        const on_out_of_scope restore_avatar( []() {
            clear_avatar();
        } );
        const int healthy = u.get_healthy_kcal();
        REQUIRE( healthy > 0 );
        eval_math( d, "u_calories('format':'percent') = 50" );
        CHECK( u.get_stored_kcal() == Approx( healthy / 2.0 ).margin( 1 ) );
        CHECK( eval_math( d, "u_calories('format':'percent')" ) == Approx( 50 ).margin( 0.01 ) );
        eval_math( d, "u_calories('format':'percent') += 10" );
        CHECK( eval_math( d, "u_calories('format':'percent')" ) == Approx( 60 ).margin( 0.01 ) );
        // raw still means kcal
        eval_math( d, "u_calories() = 1000" );
        CHECK( u.get_stored_kcal() == 1000 );
    }
}

// MATH-B05: time_until_eoc() returned the turn the EOC was queued for, not the time left, found
// only the first of several queued runs, and couldn't see the EOCs queued on a character.
TEST_CASE( "math-b05_time_until_eoc_is_time_left", "[audit][math_parser][eoc]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    const restore_on_out_of_scope<queued_eocs> restore_global( g->queued_global_effect_on_conditions );
    const restore_on_out_of_scope<queued_eocs> restore_local( u.queued_effect_on_conditions );
    calendar::turn = calendar::turn_zero + 100_days;
    dialogue d( get_talker_for( u ), nullptr );

    const std::string global = "time_until_eoc('EOC_AUDIT_MATH_B05_GLOBAL', 'unit':'minutes')";
    CHECK( eval_math( d, global ) == -1 );
    effect_on_conditions::queue_effect_on_condition( 30_minutes,
            effect_on_condition_EOC_AUDIT_MATH_B05_GLOBAL, u, {} );
    effect_on_conditions::queue_effect_on_condition( 10_minutes,
            effect_on_condition_EOC_AUDIT_MATH_B05_GLOBAL, u, {} );
    CHECK( eval_math( d, global ) == Approx( 10 ) );
    calendar::turn += 4_minutes;
    CHECK( eval_math( d, global ) == Approx( 6 ) );

    // Not global: queued on the character.
    effect_on_conditions::queue_effect_on_condition( 20_minutes,
            effect_on_condition_EOC_AUDIT_MATH_B05_LOCAL, u, {} );
    CHECK( eval_math( d, "u_time_until_eoc('EOC_AUDIT_MATH_B05_LOCAL', 'unit':'minutes')" ) ==
           Approx( 20 ) );
    CHECK( eval_math( d, "time_until_eoc('EOC_AUDIT_MATH_B05_LOCAL')" ) == -1 );
    CHECK( eval_math( d, "u_time_until_eoc('EOC_AUDIT_MATH_B05_GLOBAL')" ) == -1 );
}

// MATH-B06, LOOP-B10: before 12:00 noon() went back from noon as far as it was ahead of it (at
// 10:00, 08:00), and time_until('noon') used the time_since formula, so both returned
// |time of day - 12 h|.
TEST_CASE( "math-b06_loop-b10_time_since_and_until_noon", "[audit][math_parser][calendar]" )
{
    clear_avatar();
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const time_point midnight = calendar::turn_zero + 50_days;
    const std::string since = "time_since('noon', 'unit':'hours')";
    const std::string until = "time_until('noon', 'unit':'hours')";

    calendar::turn = midnight + 10_hours;
    CHECK( noon( calendar::turn ) == midnight + 12_hours );
    CHECK( eval_math( d, since ) == Approx( 22 ) );
    CHECK( eval_math( d, until ) == Approx( 2 ) );

    calendar::turn = midnight + 14_hours;
    CHECK( noon( calendar::turn ) == midnight + 12_hours );
    CHECK( eval_math( d, since ) == Approx( 2 ) );
    CHECK( eval_math( d, until ) == Approx( 22 ) );

    calendar::turn = midnight + 12_hours;
    CHECK( eval_math( d, since ) == Approx( 0 ) );
    CHECK( eval_math( d, until ) == Approx( 0 ) );

    calendar::turn = midnight;
    CHECK( eval_math( d, since ) == Approx( 12 ) );
    CHECK( eval_math( d, until ) == Approx( 12 ) );
}

// MATH-B07: the documented 'allow_hallucinations' kwarg failed to load, and when the talker
// wasn't a character every character in the bubble was counted, whatever the radius.
TEST_CASE( "math-b07_characters_nearby_radius_and_kwargs", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map_without_vision();
    const on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_creatures();
        get_globals().remove_global_value( "audit_math_b07_loc" );
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_at = u.pos_bub() + tripoint::east * 10;
    monster &mon = spawn_test_monster( "mon_zombie", mon_at );
    spawn_npc( ( mon_at + tripoint::east * 5 ).xy(), "test_talker" );
    REQUIRE( rl_dist( u.pos_abs(), mon.pos_abs() ) == 10 );
    REQUIRE( g->get_characters_if( []( const Character & ) {
        return true;
    } ).size() == 2 );

    dialogue by_monster( get_talker_for( mon ), nullptr );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 3)" ) == 0 );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 7)" ) == 1 );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 12)" ) == 2 );
    CHECK( eval_math( by_monster, "u_characters_nearby('radius': 12, 'attitude': 'any')" ) == 2 );
    // A monster has no attitude to the characters to filter by.
    CHECK_THROWS_AS( eval_math( by_monster,
                                "u_characters_nearby('radius': 12, 'attitude': 'hostile')" ), math::runtime_error );
    // The global scope with a location filters by radius too.
    get_globals().set_global_value( "audit_math_b07_loc", here.get_abs( mon_at ) );
    CHECK( eval_math( by_monster,
                      "characters_nearby('radius': 3, 'location': audit_math_b07_loc)" ) == 0 );

    dialogue by_avatar( get_talker_for( u ), nullptr );
    CHECK( eval_math( by_avatar, "u_characters_nearby('radius': 30, 'allow_hallucinations': 1)" ) ==
           1 );
}

// MATH-B08: '%' was right-associative, so an incoming '*', '/' or '%' didn't pop it.
TEST_CASE( "math-b08_modulo_groups_left_to_right", "[audit][math_parser]" )
{
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    CHECK( eval_math( d, "10 % 4 * 2" ) == Approx( 4 ) );
    CHECK( eval_math( d, "20 % 6 % 4" ) == Approx( 2 ) );
    CHECK( eval_math( d, "17 % 10 / 2" ) == Approx( 3.5 ) );
    CHECK( eval_math( d, "2 * 10 % 4" ) == Approx( 0 ) );
    CHECK( eval_math( d, "10 % 4 + 1" ) == Approx( 3 ) );
    CHECK( eval_math( d, "10 % 2 ^ 3" ) == Approx( 2 ) );
}

// MATH-B09: item and computer talkers gave the avatar's overmap tile, and zone talkers the
// default {0,0,0}, instead of their own.
TEST_CASE( "math-b09_talkers_report_their_own_overmap_tile", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map_without_vision();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms away = u.pos_bub() + tripoint::east * 30 + tripoint::south * 30;
    REQUIRE( project_to<coords::omt>( here.get_abs( away ) ) != u.pos_abs_omt() );
    const on_out_of_scope cleanup( [away]() {
        get_map().i_clear( away );
    } );

    item_location rock = here.add_item_or_charges_ret_loc( away, item( itype_test_rock ) );
    REQUIRE( rock );
    CHECK( get_talker_for( rock )->pos_abs_omt() == project_to<coords::omt>( here.get_abs( away ) ) );

    computer terminal( "audit terminal", 0, here.get_abs( away ) );
    CHECK( get_talker_for( terminal )->pos_abs_omt() ==
           project_to<coords::omt>( here.get_abs( away ) ) );

    zone_data zone( "audit zone", zone_type_LOOT_UNSORTED, faction_your_followers, false, true,
                    here.get_abs( away ), here.get_abs( away + tripoint::east ) );
    CHECK( get_talker_for( zone )->pos_abs_omt() == project_to<coords::omt>( here.get_abs( away ) ) );
}

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_math_talkers_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition;
}

// MATH-B10: once npc_die removed the item, the item talker stayed in the dialogue, and later
// effects of the same EOC that read the item dereferenced a null pointer.
TEST_CASE( "math-b10_a_dead_item_talker_does_not_crash_later_effects", "[audit][math_parser][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms at = u.pos_bub() + tripoint::east;
    item_location rock = here.add_item_or_charges_ret_loc( at, item( itype_test_rock ) );
    REQUIRE( rock );
    dialogue d( get_talker_for( u ), get_talker_for( rock ) );

    load_effect( R"({ "effect": [ "npc_die" ] })" ).apply( d );
    REQUIRE_FALSE( rock );
    CHECK( here.i_at( at ).empty() );

    // What the talker answers itself (fixed before this finding)
    CHECK( d.actor( true )->get_name().empty() );
    load_effect( R"({ "effect": { "math": [ "n_audit_math_b10 = 1" ] } })" ).apply( d );
    CHECK( eval_math( d, "n_audit_math_b10" ) == 0 );

    // Math functions about an item that is gone are errors, not crashes.
    const std::vector<std::string> item_functions = {
        "n_damage_level()", "n_gun_damage('ALL')", "n_melee_damage('ALL')", "n_vitamin('calcium')"
    };
    for( const std::string &expr : item_functions ) {
        CAPTURE( expr );
        CHECK_THROWS_AS( eval_math( d, expr ), math::runtime_error );
    }
    // Conditions about it are false.
    CHECK_FALSE( load_condition( R"({ "condition": "npc_has_ammo" })" )( d ) );
    CHECK_FALSE( load_condition( R"({ "condition": "npc_is_rotten" })" )( d ) );
    // Effects that change it do nothing.
    load_effect( R"({ "effect": { "npc_set_fault": "fault_armor_lc_dented" } })" ).apply( d );
    load_effect( R"({ "effect": { "npc_set_random_fault_of_type": "mechanical_damage" } })" ).apply(
        d );
    CHECK_FALSE( rock );
}

// MATH-B10 with MATH-B09: the position of an item that is gone was the empty location's, which
// complained and gave INT_MIN, so npc_at_om_location and the like looked at an overmap there.
TEST_CASE( "math-b10_a_dead_item_has_no_position", "[audit][math_parser][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms at = u.pos_bub() + tripoint::east;
    item_location rock = here.add_item_or_charges_ret_loc( at, item( itype_test_rock ) );
    REQUIRE( rock );
    dialogue d( get_talker_for( u ), get_talker_for( rock ) );
    const std::string here_omt( oter_no_dir_or_connections( overmap_buffer.ter( u.pos_abs_omt() ) ) );
    const conditional_t::func at_om = load_condition( string_format(
                                          R"({ "condition": { "npc_at_om_location": "%s" } })", here_omt ) );
    const conditional_t::func near_om = load_condition( string_format(
                                            R"({ "condition": { "npc_near_om_location": "%s", "range": 1 } })", here_omt ) );
    const conditional_t::func safe = load_condition( R"({ "condition": "npc_at_safe_space" })" );
    REQUIRE( at_om( d ) );
    REQUIRE( near_om( d ) );
    REQUIRE( eval_math( d, "n_val('pos_x')" ) == here.get_abs( at ).x() );

    load_effect( R"({ "effect": [ "npc_die" ] })" ).apply( d );
    REQUIRE_FALSE( rock );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        CHECK_FALSE( at_om( d ) );
        CHECK_FALSE( near_om( d ) );
        CHECK_FALSE( safe( d ) );
        CHECK( d.actor( true )->pos_abs() == tripoint_abs_ms::invalid );
        for( const std::string axis : {
                 "x", "y", "z"
             } ) {
            CHECK_THROWS_AS( eval_math( d, "n_val('pos_" + axis + "')" ), math::runtime_error );
        }
        // An assignment to it does nothing.
        load_effect( R"({ "effect": { "math": [ "n_val('pos_z') = 1" ] } })" ).apply( d );
    } );
    CHECK( dmsg.empty() );
}

// MATH-B11: val('pos_x/y/z') assignment called Creature::setpos directly: the map didn't follow
// the avatar, a rider stayed boarded, a solid spot was entered, and monsters ignored it.
TEST_CASE( "math-b11_val_pos_moves_like_a_teleport", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_vehicles();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_abs_sm bubble = here.get_abs_sub();
    const tripoint_abs_ms start = u.pos_abs();
    const on_out_of_scope restore_bubble( [bubble, start]() {
        clear_vehicles();
        clear_creatures();
        map &here = get_map();
        here.load( bubble, false );
        get_avatar().setpos( here, here.get_bub( start ) );
    } );
    dialogue d( get_talker_for( u ), nullptr );

    SECTION( "the map follows the avatar" ) {
        eval_math( d, "u_val('pos_x') += 20" );
        CHECK( u.pos_abs() == start + tripoint::east * 20 );
        CHECK( here.get_abs_sub() == bubble + tripoint::east );
        CHECK( u.pos_bub().x() >= HALF_MAPSIZE_X );
        CHECK( u.pos_bub().x() < HALF_MAPSIZE_X + SEEX );
    }

    SECTION( "no move into solid rock" ) {
        // clear_map leaves the level below solid
        REQUIRE( here.impassable( u.pos_bub() + tripoint::below ) );
        eval_math( d, "u_val('pos_z') = -1" );
        CHECK( u.pos_abs() == start );
        CHECK_FALSE( u.is_dead_state() );
    }

    SECTION( "a rider gets off the vehicle" ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, u.pos_bub(), 0_degrees, 0,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) >= 0 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_seat ) >= 0 );
        here.add_vehicle_to_cache( veh );
        here.board_vehicle( u.pos_bub(), &u );
        REQUIRE( u.in_vehicle );
        REQUIRE( veh->get_riders().size() == 1 );

        eval_math( d, "u_val('pos_x') += 3" );
        CHECK( u.pos_abs() == start + tripoint::east * 3 );
        CHECK_FALSE( u.in_vehicle );
        CHECK( veh->get_riders().empty() );
    }

    SECTION( "a monster moves too" ) {
        monster &mon = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint::east * 5 );
        const tripoint_abs_ms mon_start = mon.pos_abs();
        dialogue with_monster( get_talker_for( u ), get_talker_for( mon ) );
        eval_math( with_monster, "n_val('pos_x') += 2" );
        CHECK( mon.pos_abs() == mon_start + tripoint::east * 2 );
    }
}

// MATH-B11 follow-up: teleport_to_point let go of holds and got off the vehicle before it checked
// the spot, and for a spot outside the bubble moved the bubble (dropping the mount) first, so a
// refused move still changed those.
TEST_CASE( "math-b11_a_refused_move_changes_nothing", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_vehicles();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_abs_sm bubble = here.get_abs_sub();
    const on_out_of_scope restore_bubble( [bubble]() {
        avatar &you = get_avatar();
        map &here = get_map();
        grapple::release_all( you );
        you.remove_effect( effect_riding );
        you.mounted_creature = nullptr;
        if( you.in_vehicle ) {
            here.unboard_vehicle( you.pos_bub() );
        }
        you.controlling_vehicle = false;
        clear_vehicles();
        clear_creatures();
        here.load( bubble, false );
        you.setpos( here, tripoint_bub_ms( 60, 60, 0 ) );
    } );
    dialogue d( get_talker_for( u ), nullptr );

    SECTION( "driving, into the rock below" ) {
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, u.pos_bub(), 0_degrees, 0,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) >= 0 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_seat ) >= 0 );
        here.add_vehicle_to_cache( veh );
        here.board_vehicle( u.pos_bub(), &u );
        u.controlling_vehicle = true;
        REQUIRE( u.in_vehicle );
        REQUIRE( here.impassable( u.pos_bub() + tripoint::below ) );
        const tripoint_abs_ms start = u.pos_abs();

        eval_math( d, "u_val('pos_z') -= 1" );
        CHECK( u.pos_abs() == start );
        CHECK( u.in_vehicle );
        CHECK( u.controlling_vehicle );
        CHECK( veh->get_riders().size() == 1 );
    }

    SECTION( "holding a monster, onto its tile" ) {
        monster &mon = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint::east );
        REQUIRE( grapple::start_hold( u, mon, 20, bodypart_str_id::NULL_ID().id() ) );
        REQUIRE( u.grabbed_creature() == &mon );
        const tripoint_abs_ms start = u.pos_abs();

        eval_math( d, "u_val('pos_x') += 1" );
        CHECK( u.pos_abs() == start );
        CHECK( u.grabbed_creature() == &mon );
        CHECK( u.is_grabbing_creature() );
    }

    SECTION( "mounted, at a wall outside the bubble" ) {
        monster &horse = spawn_test_monster( "mon_horse", u.pos_bub() + tripoint::west );
        u.mount_creature( horse );
        REQUIRE( u.is_mounted() );
        const monster *const mount = u.mounted_creature.get();
        const tripoint_abs_ms start = u.pos_abs();
        const tripoint_abs_ms far = start + tripoint::east * 200;
        REQUIRE_FALSE( here.inbounds( here.get_bub( far ) ) );
        // Make the far spot solid, and put it back afterwards.
        tinymap far_map;
        far_map.load( project_to<coords::omt>( far ), false );
        const tripoint_omt_ms far_at = far_map.get_omt( far );
        const ter_id far_ter = far_map.ter( far_at );
        const furn_id far_furn = far_map.furn( far_at );
        far_map.ter_set( far_at, ter_t_rock.id() );
        const on_out_of_scope restore_far( [far, far_ter, far_furn]() {
            tinymap m;
            m.load( project_to<coords::omt>( far ), false );
            m.ter_set( m.get_omt( far ), far_ter );
            m.furn_set( m.get_omt( far ), far_furn );
        } );

        // (mounting may have shifted the map already)
        const tripoint_abs_sm mounted_bubble = here.get_abs_sub();
        eval_math( d, "u_val('pos_x') += 200" );
        CHECK( u.pos_abs() == start );
        CHECK( here.get_abs_sub() == mounted_bubble );
        CHECK( u.is_mounted() );
        CHECK( u.mounted_creature.get() == mount );
    }
}

// MATH-B11 follow-up: the plain setpos this used to be made a character fall at once over open
// air; a teleport leaves it hanging until its own turn (or for good, for an NPC).
TEST_CASE( "math-b11_a_creature_moved_over_open_air_falls", "[audit][math_parser]" )
{
    clear_avatar();
    clear_map_without_vision();
    const on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_creatures();
        clear_map_without_vision();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms hole = u.pos_bub() + tripoint::east * 8;
    here.ter_set( hole, ter_t_open_air );
    here.ter_set( hole + tripoint::below, ter_t_floor );
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0, true );
    REQUIRE( here.is_open_air( hole ) );
    const tripoint_abs_ms landing = here.get_abs( hole + tripoint::below );

    SECTION( "an NPC" ) {
        npc &guy = spawn_npc( ( hole + tripoint::west * 3 ).xy(), "test_talker" );
        REQUIRE( guy.pos_bub() == hole + tripoint::west * 3 );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        eval_math( d, "n_val('pos_x') += 3" );
        CHECK( guy.pos_abs() == landing );
    }

    SECTION( "a monster" ) {
        monster &mon = spawn_test_monster( "mon_zombie", hole + tripoint::west * 3 );
        dialogue d( get_talker_for( u ), get_talker_for( mon ) );
        eval_math( d, "n_val('pos_x') += 3" );
        CHECK( mon.pos_abs() == landing );
    }
}

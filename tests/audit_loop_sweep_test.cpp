#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "do_turn.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "line.h"
#include "magic_enchantment.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "player_helpers.h"
#include "point.h"
#include "submap.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "weather_type.h"
#include "worldfactory.h"

// ---- LOOP-B06 ----

static const itype_id itype_bucket( "bucket" );
static const itype_id itype_maple_sap( "maple_sap" );

static const ter_str_id ter_t_tree_maple_tapped( "t_tree_maple_tapped" );

// The water temperature curve and the maple sap season count days from the start of spring
// (calendar::turn_zero), but were given days from Jan 1, 81 days later in the year. Water was
// warmest in mid-May and coldest in mid-November, and sap ran from Dec 7 to Feb 14.
static units::temperature water_temperature_at( const time_duration &since_spring )
{
    calendar::turn = calendar::turn_zero + calendar::year_length() + since_spring;
    // At midnight the daily swing adds nothing
    calendar::turn -= time_past_midnight( calendar::turn );
    return get_weather().get_cur_weather_gen().get_water_temperature();
}

TEST_CASE( "loop-b06_water_is_warmest_in_midsummer_and_coldest_in_midwinter",
           "[audit][weather][temperature]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    const time_duration season = calendar::season_length();
    // The yearly curve runs from 33.8 F to 75.2 F
    CHECK( units::to_fahrenheit( water_temperature_at( season * 3 / 2 ) ) ==
           Approx( 75.2 ).margin( 0.5 ) );
    CHECK( units::to_fahrenheit( water_temperature_at( season * 7 / 2 ) ) ==
           Approx( 33.8 ).margin( 0.5 ) );
}

// Sap a tapped maple makes in the day before the given time of year
static int sap_made_in_the_day_before( const time_duration &since_spring )
{
    clear_map();
    map &here = get_map();
    const time_point start = calendar::turn;
    // The next such time at least a day ahead, so every submap the load catches up moves forwards
    time_point when = calendar::turn_zero + since_spring;
    while( when < start + 1_days ) {
        when += calendar::year_length();
    }
    calendar::turn = when;

    const tripoint_bub_ms tree( 60, 60, 0 );
    here.ter_set( tree, ter_t_tree_maple_tapped );
    here.add_item( tree, item( itype_bucket ) );
    const tripoint_abs_omt tree_omt = project_to<coords::omt>( here.get_abs( tree ) );
    // The submaps of the tree's map square, which the load below catches up
    std::vector<submap *> submaps;
    for( int dx = 0; dx < SEEX * 2; dx += SEEX ) {
        for( int dy = 0; dy < SEEY * 2; dy += SEEY ) {
            const tripoint_abs_ms corner = project_to<coords::ms>( tree_omt ) + tripoint_rel_ms( dx, dy, 0 );
            tripoint_bub_ms p = here.get_bub( corner );
            point_sm_ms l;
            submaps.push_back( map_meddler::unsafe_get_submap_at( p, l ) );
        }
    }
    tripoint_bub_ms tree_sm_pos = tree;
    point_sm_ms l;
    map_meddler::unsafe_get_submap_at( tree_sm_pos, l )->last_touched = calendar::turn - 1_days;

    // Loading a map over the tree catches its submap up on the missed day, which makes the sap
    tinymap tm;
    tm.load( tree_omt, false );

    int sap = 0;
    for( const item &container : here.i_at( tree ) ) {
        for( const item *it : container.all_items_top() ) {
            if( it->typeId() == itype_maple_sap ) {
                sap += it->charges;
            }
        }
    }
    calendar::turn = start;
    for( submap *sm : submaps ) {
        sm->last_touched = start;
    }
    clear_map();
    return sap;
}

TEST_CASE( "loop-b06_maple_sap_runs_in_late_winter_and_early_spring", "[audit][map]" )
{
    const time_duration season = calendar::season_length();
    // Ten days into spring (around Mar 31) is in the season
    CHECK( sap_made_in_the_day_before( 10_days ) > 0 );
    // Ten days into winter (around Dec 31) is not
    CHECK( sap_made_in_the_day_before( season * 3 + 10_days ) == 0 );
}

// ---- LOOP-B08 ----

static const bionic_id bio_alarm( "bio_alarm" );

static const efftype_id effect_pet( "pet" );

// The power the motion alarm takes over the monsters' share of one turn
static units::energy alarm_power_used_in_one_turn()
{
    avatar &u = get_avatar();
    const units::energy before = u.get_power_level();
    monmove();
    return before - u.get_power_level();
}

// The motion alarm went off once for every monster near you each turn, pets and creatures
// that never move included, so a dog at your side woke you every turn.
TEST_CASE( "loop-b08_motion_alarm_goes_off_once_a_turn_for_strangers_that_move",
           "[audit][game_loop]" )
{
    clear_map();
    clear_avatar();
    restore_on_out_of_scope restore_turn( calendar::turn );
    set_time_to_day();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms center( 60, 60, 0 );
    u.setpos( here, center );
    u.set_max_power_level( 100_kJ );
    u.set_power_level( 100_kJ );
    give_and_activate_bionic( u, bio_alarm );
    u.recalculate_enchantment_cache();
    REQUIRE( u.enchantment_cache->modify_value( enchant_vals::mod::MOTION_ALARM, 0 ) == 5 );
    const units::energy one_alarm = bio_alarm->power_trigger;
    REQUIRE( one_alarm > 0_J );
    u.set_power_level( 100_kJ );

    SECTION( "two strangers walking up set it off once" ) {
        monster &east = spawn_test_monster( "mon_zombie", center + tripoint_rel_ms( 4, 0, 0 ) );
        monster &west = spawn_test_monster( "mon_zombie", center + tripoint_rel_ms( -4, 0, 0 ) );
        const tripoint_abs_ms east_before = east.pos_abs();
        const tripoint_abs_ms west_before = west.pos_abs();
        const units::energy used = alarm_power_used_in_one_turn();
        REQUIRE( east.pos_abs() != east_before );
        REQUIRE( west.pos_abs() != west_before );
        CHECK( used == one_alarm );
    }

    // A mount is always one of your pets, so this covers riding too
    SECTION( "a pet walking around does not" ) {
        monster &dog = spawn_test_monster( "mon_dog", center + tripoint_rel_ms( 3, 0, 0 ) );
        dog.friendly = -1;
        dog.add_effect( effect_pet, 1_turns, true );
        REQUIRE( dog.is_pet() );
        CHECK( alarm_power_used_in_one_turn() == 0_J );
    }

    SECTION( "a creature that stays put does not" ) {
        spawn_test_monster( "mon_test_speed_desc_base_immobile", center + tripoint_rel_ms( 2, 0, 0 ) );
        CHECK( alarm_power_used_in_one_turn() == 0_J );
    }

    clear_map();
    clear_avatar();
}

// ---- LOOP-B09 ----

static const weather_type_id weather_scarlet_dimension_storm( "scarlet_dimension_storm" );
static const weather_type_id weather_scarlet_storm_5( "scarlet_storm_5" );

// A plain number for a weather type's temperature_modifier was read in legacy body temperature
// units (0.002 K), so the scarlet storms' -2 to -30 chilled by 0.004 to 0.06 K.
TEST_CASE( "loop-b09_weather_temperature_modifier_is_in_degrees", "[audit][weather]" )
{
    CHECK( units::to_kelvin_delta( weather_scarlet_dimension_storm->temperature_modifier ) ==
           Approx( -20 ) );
    CHECK( units::to_kelvin_delta( weather_scarlet_storm_5->temperature_modifier ) ==
           Approx( -30 ) );

    const auto loaded_modifier = []( const std::string & value ) {
        const std::string json = R"({ "id": "test_loop_b09", "type": "weather_type", "name": "test",)"
                                 R"( "sym": "x", "ranged_penalty": 0, "sight_penalty": 1.0,)"
                                 R"( "light_modifier": 0, "priority": 0, "sound_attn": 0,)"
                                 R"( "dangerous": false, "precip": "none", "rains": false,)"
                                 R"( "temperature_modifier": )" + value + " }";
        JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        weather_type wt;
        wt.load( jo, "" );
        return units::to_kelvin_delta( wt.temperature_modifier );
    };
    // A plain number counts in degrees Celsius, like a weather generator's base_temperature
    CHECK( loaded_modifier( "-20" ) == Approx( -20 ) );
    CHECK( loaded_modifier( "-7.5" ) == Approx( -7.5 ) );
    CHECK( loaded_modifier( R"("-20 C")" ) == Approx( -20 ) );
    CHECK( loaded_modifier( R"("-500 mC")" ) == Approx( -0.5 ) );
}

// ---- LOOP-B11 ----

static const ter_str_id ter_t_flat_roof( "t_flat_roof" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_water_dp( "t_water_dp" );
static const ter_str_id ter_t_water_sh( "t_water_sh" );

// Knockback tested for water on the tile the target started on instead of the one it was
// moving through, and looked for walls on the map's own level instead of the target's.
TEST_CASE( "loop-b11_knockback_checks_the_tiles_the_target_moves_through", "[audit][game_loop]" )
{
    clear_map( -1, 1 );
    clear_avatar();
    map &here = get_map();
    get_avatar().setpos( here, tripoint_bub_ms( 30, 30, 0 ) );
    // Knocked from the west, the target slides three tiles east
    const tripoint_bub_ms start( 60, 60, 0 );
    const tripoint_rel_ms east( 1, 0, 0 );
    const tripoint_rel_ms up( 0, 0, 1 );

    SECTION( "out of shallow water onto dry ground" ) {
        here.ter_set( start, ter_t_water_sh );
        monster &pig = spawn_test_monster( "mon_pig", start );
        REQUIRE( pig.can_drown() );
        g->knockback( start - east, start, 3, 0, 1 );
        CHECK( !pig.is_dead() );
        CHECK( pig.pos_bub( here ) == start + east * 3 );
    }

    SECTION( "off dry ground into deep water" ) {
        for( int i = 1; i <= 3; i++ ) {
            here.ter_set( start + east * i, ter_t_water_dp );
        }
        monster &pig = spawn_test_monster( "mon_pig", start );
        REQUIRE( pig.can_drown() );
        g->knockback( start - east, start, 3, 0, 1 );
        CHECK( pig.is_dead() );
    }

    SECTION( "a water creature out of the water onto land" ) {
        here.ter_set( start, ter_t_water_dp );
        monster &eel = spawn_test_monster( "mon_fish_eel", start );
        REQUIRE( eel.has_flag( mon_flag_AQUATIC ) );
        g->knockback( start - east, start, 3, 0, 1 );
        CHECK( eel.is_dead() );
    }

    SECTION( "on the level above the map's own" ) {
        for( int i = 0; i <= 3; i++ ) {
            here.ter_set( start + up + east * i, ter_t_floor );
        }
        // A wall one level down, under the middle of the slide
        here.ter_set( start + east * 2, ter_t_wall );
        monster &zed = spawn_test_monster( "mon_zombie", start + up );
        g->knockback( start + up - east, start + up, 3, 0, 1 );
        CHECK( zed.pos_bub( here ) == start + up + east * 3 );
    }

    clear_map( -1, 1 );
}

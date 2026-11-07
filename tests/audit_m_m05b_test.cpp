#include <stddef.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "action.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "climbing.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "game_constants.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_defines.h"
#include "npc.h"
#include "omdata.h"
#include "overmap_ui.h"
#include "phase_change.h"
#include "player_helpers.h"
#include "point.h"
#include "regional_settings.h"
#include "scenario.h"
#include "skill.h"
#include "type_id.h"
#include "units.h"
#include "weather.h"
#include "weather_gen.h"
#include "weather_type.h"
#include "weighted_list.h"

static const climbing_aid_id climbing_aid_test_m05b_sheer_drop_aid( "test_m05b_sheer_drop_aid" );

static const efftype_id effect_glare( "glare" );
static const efftype_id effect_snow_glare( "snow_glare" );
static const efftype_id effect_test_m05b_glare_avatar( "test_m05b_glare_avatar" );
static const efftype_id effect_test_m05b_glare_other( "test_m05b_glare_other" );

static const json_character_flag json_flag_TEMPORARY_SHAPESHIFT_NO_HANDS(
    "TEMPORARY_SHAPESHIFT_NO_HANDS" );

static const npc_class_id NC_BANDIT( "NC_BANDIT" );

static const oter_str_id oter_field( "field" );
static const oter_str_id oter_forest( "forest" );
static const oter_str_id oter_forest_water( "forest_water" );

static const region_settings_id region_settings_default( "default" );

static const skill_id skill_survival( "survival" );
static const skill_id skill_test_m05b_capped_skill( "test_m05b_capped_skill" );

static const trait_id trait_TEST_M05B_NO_HANDS( "TEST_M05B_NO_HANDS" );
static const trait_id trait_TEST_M05B_RAPPELLER( "TEST_M05B_RAPPELLER" );
static const trait_id trait_TEST_M05B_SURE_FOOTED( "TEST_M05B_SURE_FOOTED" );
static const trait_id trait_TEST_M05B_VOW_TRAIT( "TEST_M05B_VOW_TRAIT" );

static const weather_generator_id weather_generator_test_m05b_custom_gen( "test_m05b_custom_gen" );
static const weather_generator_id
weather_generator_test_m05b_explicit_gen( "test_m05b_explicit_gen" );
static const weather_generator_id weather_generator_test_m05b_plain_gen( "test_m05b_plain_gen" );

static const weather_type_id weather_test_m05b_glare_weather( "test_m05b_glare_weather" );
static const weather_type_id weather_test_m05b_soaking_rain( "test_m05b_soaking_rain" );

// LOOP-J05: what a weather type does when it rains (rate, wetness, field decay) and its glare
// were a C++ switch on precip. Without the new fields they still follow precip.
TEST_CASE( "loop-j05_weather_types_without_the_new_fields_follow_precip", "[audit][weather]" )
{
    int checked = 0;
    for( const weather_type &wt : weather_types::get_all() ) {
        if( wt.precip_rate_override || wt.wetness_override || wt.field_decay_override ||
            wt.glare_override ) {
            continue;
        }
        CAPTURE( wt.id.str() );
        CHECK( wt.get_precip_rate() == precip_mm_per_hour( wt.precip ) );
        int wetness = 0;
        time_duration decay = 0_turns;
        if( wt.precip == precip_class::very_light ) {
            wetness = 5;
            decay = 5_turns;
        } else if( wt.precip == precip_class::light ) {
            wetness = 30;
            decay = 15_turns;
        } else if( wt.precip == precip_class::heavy ) {
            wetness = 60;
            decay = 45_turns;
        }
        CHECK( wt.get_wetness() == wetness );
        CHECK( wt.get_field_decay() == decay );
        const std::vector<weather_glare> &glare = wt.get_glare();
        REQUIRE( glare.size() == 2 );
        CHECK( glare[0].effect == effect_snow_glare );
        CHECK( glare[0].min_irradiance == irradiance::moderate );
        CHECK( static_cast<bool>( glare[0].condition ) );
        CHECK( glare[1].effect == effect_glare );
        CHECK( glare[1].min_irradiance == irradiance::high );
        CHECK( !glare[1].condition );
        for( const weather_glare &entry : glare ) {
            CHECK( entry.duration == 20_turns );
            CHECK( entry.repeat_duration == 10_turns );
        }
        ++checked;
    }
    CHECK( checked > 10 );
}

TEST_CASE( "loop-j05_weather_type_fields_override_precip", "[audit][weather]" )
{
    const weather_type &rain = weather_test_m05b_soaking_rain.obj();
    CHECK( rain.get_precip_rate() == Approx( 7.5 ) );
    CHECK( rain.get_wetness() == 100 );
    CHECK( rain.get_field_decay() == 2_seconds );
    CHECK( rain.get_glare().empty() );

    const weather_type &glaring = weather_test_m05b_glare_weather.obj();
    REQUIRE( glaring.get_glare().size() == 2 );
    CHECK( glaring.get_glare()[0].duration == 30_seconds );
    CHECK( glaring.get_glare()[0].repeat_duration == 4_seconds );
    CHECK( glaring.get_glare()[1].duration == 20_seconds );
    // No precipitation without a precip_rate
    CHECK( glaring.get_precip_rate() == 0.0 );
    CHECK( glaring.get_wetness() == 0 );
}

// Rain wet only the avatar ("Possible TODO, make npc/monsters affected") and glare reached only
// the avatar ("todo npcs").
TEST_CASE( "loop-j05_rain_and_glare_reach_npcs_too", "[audit][weather][npc]" )
{
    clear_map();
    clear_avatar();
    clear_npcs();
    restore_on_out_of_scope restore_turn( calendar::turn );
    const on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_avatar();
        clear_map();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    u.setpos( here, tripoint_bub_ms( 60, 60, 0 ) );
    npc &guy = spawn_npc( point_bub_ms( 66, 60 ), "thug" );
    clear_character( guy );
    REQUIRE( !here.is_roofed( guy.pos_bub( here ) ) );
    REQUIRE( !g->is_sheltered( guy.pos_bub( here ) ) );

    SECTION( "rain" ) {
        for( const bodypart_id &bp : u.get_all_body_parts() ) {
            u.set_part_wetness( bp, 0 );
        }
        for( const bodypart_id &bp : guy.get_all_body_parts() ) {
            guy.set_part_wetness( bp, 0 );
        }
        calendar::turn = calendar::turn_zero;
        for( int i = 0; i < 10; ++i ) {
            calendar::turn += 6_seconds;
            handle_weather_effects( weather_test_m05b_soaking_rain );
        }
        // wetness 100 soaks anyone without clothes every time
        CHECK( u.get_part_wetness( body_part_torso ) > 0 );
        CHECK( guy.get_part_wetness( body_part_torso ) > 0 );
        // The rain has no glare
        CHECK( !u.has_effect( effect_glare ) );
        CHECK( !guy.has_effect( effect_glare ) );
    }
    SECTION( "glare" ) {
        handle_weather_effects( weather_test_m05b_glare_weather );
        // The first entry is only for the avatar (its condition), the second for anyone
        CHECK( u.has_effect( effect_test_m05b_glare_avatar ) );
        CHECK( !u.has_effect( effect_test_m05b_glare_other ) );
        CHECK( guy.has_effect( effect_test_m05b_glare_other ) );
        CHECK( !guy.has_effect( effect_test_m05b_glare_avatar ) );
        CHECK( u.get_effect_dur( effect_test_m05b_glare_avatar, bodypart_id( "eyes" ) ) == 30_seconds );
        CHECK( guy.get_effect_dur( effect_test_m05b_glare_other, bodypart_id( "eyes" ) ) == 20_seconds );
    }
}

// LOOP-J06: the climate's shape was fixed in an anonymous namespace.
TEST_CASE( "loop-j06_climate_shape_defaults_are_the_old_constants", "[audit][weather]" )
{
    const weather_generator &plain = weather_generator_test_m05b_plain_gen.obj();
    CHECK( plain.seasonal_amplitude_K == 12 );
    CHECK( plain.daily_amplitude_K == 5 );
    CHECK( plain.daily_seasonal_range_K == 2 );
    CHECK( plain.coldest_hour == 5 );
    CHECK( plain.temperature_noise_K == 6 );
    CHECK( plain.seasonal_noise_magnitude == 0.5 );
    CHECK( !plain.underground_temperature_C );
    CHECK( plain.get_underground_temperature() == units::from_celsius( 11.1 ) );
    const std::array<std::array<int, 16>, 4> old_weights = { {
            { { 3, 3, 5, 8, 11, 10, 5, 2, 5, 6, 6, 5, 8, 10, 8, 6 } },
            { { 3, 4, 4, 8, 8, 9, 8, 3, 7, 8, 10, 7, 7, 7, 5, 3 } },
            { { 4, 6, 6, 7, 6, 5, 4, 3, 5, 6, 8, 8, 10, 10, 8, 5 } },
            { { 5, 3, 2, 3, 2, 2, 2, 2, 4, 6, 10, 8, 12, 19, 13, 9 } }
        }
    };
    CHECK( plain.wind_direction_weights == old_weights );

    // Writing the defaults out gives the same temperatures, to the bit
    const weather_generator &expl = weather_generator_test_m05b_explicit_gen.obj();
    for( int i = 0; i < 40; ++i ) {
        const time_point t = calendar::turn_zero + 7_hours * i + 13_minutes * i + 3_days * ( i % 7 ) * 11;
        const tripoint_abs_ms loc( 1000 * i, -700 * i, 0 );
        CAPTURE( i );
        CHECK( plain.get_weather_temperature( loc, t, 42 ) ==
               expl.get_weather_temperature( loc, t, 42 ) );
    }

    // The water follows the old New England formula
    restore_on_out_of_scope restore_turn( calendar::turn );
    for( int i = 0; i < 20; ++i ) {
        calendar::turn = calendar::turn_zero + 5_days * i + 5_hours * i;
        int season_length = to_days<int>( calendar::season_length() );
        if( season_length == 0 ) {
            season_length = 1;
        }
        const int day = to_days<int>( ( calendar::turn - calendar::turn_zero ) % calendar::year_length() );
        const int hour = hour_of_day<int>( calendar::turn );
        const double tau = 2 * M_PI;
        const float annual = 54.5 + 20.7 * std::sin( tau * ( day - season_length * 0.5 ) /
                             ( season_length * 4.0 ) );
        const float daily = 2.0 + 2.0 * std::sin( tau * ( hour - 6.0 ) / 24.0 );
        CAPTURE( i );
        CHECK( units::to_kelvin( plain.get_water_temperature() ) ==
               Approx( units::to_kelvin( units::from_fahrenheit( annual + daily ) ) ).margin( 0.001 ) );
    }
}

TEST_CASE( "loop-j06_a_weather_generator_sets_its_own_climate", "[audit][weather]" )
{
    const weather_generator &custom = weather_generator_test_m05b_custom_gen.obj();
    CHECK( custom.base_temperature == Approx( 26 ).margin( 0.001 ) );
    CHECK( custom.daily_amplitude_K == 4 );
    CHECK( custom.coldest_hour == 3 );
    // No seasons and no noise: 4 K below the base at 03:00, 4 K above twelve hours later
    for( int day = 0; day < 365; day += 30 ) {
        const time_point midnight = calendar::turn_zero + 1_days * day;
        const tripoint_abs_ms loc( 500 * day, 300, 0 );
        CAPTURE( day );
        CHECK( units::to_celsius( custom.get_weather_temperature( loc, midnight + 3_hours, 7 ) ) ==
               Approx( 22 ).margin( 0.01 ) );
        CHECK( units::to_celsius( custom.get_weather_temperature( loc, midnight + 15_hours, 7 ) ) ==
               Approx( 30 ).margin( 0.01 ) );
    }
    CHECK( units::to_celsius( custom.get_underground_temperature() ) == Approx( 15 ).margin( 0.001 ) );
    restore_on_out_of_scope restore_turn( calendar::turn );
    for( int i = 0; i < 10; ++i ) {
        calendar::turn = calendar::turn_zero + 37_days * i + 5_hours * i;
        CHECK( units::to_celsius( custom.get_water_temperature() ) == Approx( 27 ).margin( 0.01 ) );
    }
    // Only one compass point has any weight in spring
    for( int i = 0; i < 20; ++i ) {
        CHECK( custom.get_wind_direction( SPRING ) == 4 );
    }
    // The black list holds weather type ids, checked at load
    CHECK( std::find( custom.sorted_weather.begin(), custom.sorted_weather.end(),
                      weather_test_m05b_soaking_rain ) == custom.sorted_weather.end() );
    CHECK( std::find( custom.sorted_weather.begin(), custom.sorted_weather.end(),
                      WEATHER_CLEAR ) != custom.sorted_weather.end() );
    const weather_generator &plain = weather_generator_test_m05b_plain_gen.obj();
    CHECK( std::find( plain.sorted_weather.begin(), plain.sorted_weather.end(),
                      weather_test_m05b_soaking_rain ) != plain.sorted_weather.end() );
}

// The temperature sampler caches what depends only on the time; the noise's size now comes
// from the generator, and its answers must still be get_weather_temperature's, to the bit.
TEST_CASE( "loop-j06_the_temperature_sampler_matches_the_generator", "[audit][weather]" )
{
    std::vector<time_point> times;
    times.reserve( 12 );
    for( int i = 0; i < 12; ++i ) {
        times.push_back( calendar::turn_zero + 31_days * i + 8_hours );
    }
    for( const weather_generator_id &gen_id : {
             weather_generator_test_m05b_plain_gen, weather_generator_test_m05b_custom_gen
         } ) {
        const weather_generator &gen = gen_id.obj();
        const weather_temperature_sampler sampler( gen, times );
        for( size_t i = 0; i < times.size(); ++i ) {
            const tripoint_abs_ms loc( 2000 * static_cast<int>( i ), 1500, 0 );
            CAPTURE( gen_id.str() );
            CAPTURE( i );
            CHECK( sampler.temperature( i, loc, 99 ) == gen.get_weather_temperature( loc, times[i], 99 ) );
        }
    }
}

// Forests halved the wind by oter id; an overmap terrain can now set its own factor.
TEST_CASE( "loop-j06_overmap_terrain_wind_factor", "[audit][weather][overmap]" )
{
    CHECK( oter_forest->get_wind_factor() == 0.5f );
    CHECK( oter_forest_water->get_wind_factor() == 0.5f );
    CHECK( oter_field->get_wind_factor() == 1.0f );

    JsonObject jo = json_loader::from_string(
                        R"({ "name": "test windy hill", "sym": "^", "see_cost": "none", "wind_factor": 1.5, "uniform_terrain": "t_grass" })" );
    jo.allow_omitted_members();
    oter_type_t windy;
    windy.load( jo, "" );
    CHECK( oter_t( windy ).get_wind_factor() == 1.5f );
}

// LOOP-J07: action blocking was hardcoded per flag; a json_flag can now list blocked actions.
TEST_CASE( "loop-j07_json_flags_block_actions", "[audit][action]" )
{
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    avatar &u = get_avatar();
    CHECK( !action_blocked_by_flags( u, ACTION_PICKUP ) );
    CHECK( !action_blocked_by_flags( u, ACTION_FIRE ) );

    SECTION( "the handless shapeshift's list moved to flags.json unchanged" ) {
        u.set_mutation( trait_TEST_M05B_NO_HANDS );
        REQUIRE( u.has_flag( json_flag_TEMPORARY_SHAPESHIFT_NO_HANDS ) );
        // The actions the C++ table had
        const std::vector<action_id> was_blocked = {
            ACTION_OPEN, ACTION_CLOSE, ACTION_ADVANCEDINV, ACTION_PICKUP, ACTION_PICKUP_ALL,
            ACTION_GRAB, ACTION_HAUL, ACTION_HAUL_TOGGLE, ACTION_BUTCHER, ACTION_DROP,
            ACTION_DIR_DROP, ACTION_CRAFT, ACTION_RECRAFT, ACTION_LONGCRAFT, ACTION_DISASSEMBLE,
            ACTION_CONSTRUCT, ACTION_CONTROL_VEHICLE
        };
        for( const action_id act : was_blocked ) {
            CAPTURE( action_ident( act ) );
            CHECK( action_blocked_by_flags( u, act ).value_or( "" ) ==
                   "You can't do that while shapeshifted." );
        }
        // The fire action refuses handless characters in its own code
        CHECK( !action_blocked_by_flags( u, ACTION_FIRE ) );
        CHECK( !action_blocked_by_flags( u, ACTION_PEEK ) );
    }
    SECTION( "a mod's flag blocks only its own actions" ) {
        u.set_mutation( trait_TEST_M05B_VOW_TRAIT );
        CHECK( action_blocked_by_flags( u, ACTION_FIRE ).value_or( "" ) ==
               "You have sworn not to fight." );
        CHECK( action_blocked_by_flags( u, ACTION_THROW ).value_or( "" ) ==
               "You have sworn not to fight." );
        CHECK( !action_blocked_by_flags( u, ACTION_PICKUP ) );
    }
}

// LOOP-J08: there was no enchantment for the chance of slipping while climbing.
TEST_CASE( "loop-j08_climb_slip_chance_enchantment", "[audit][climbing]" )
{
    clear_map();
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    avatar &u = get_avatar();
    const float base = g->slip_down_chance( game::climb_maneuver::down, climbing_aid_id::NULL_ID(),
                                            false );
    REQUIRE( base > 0 );
    u.set_mutation( trait_TEST_M05B_SURE_FOOTED );
    u.recalculate_enchantment_cache();
    CHECK( g->slip_down_chance( game::climb_maneuver::down, climbing_aid_id::NULL_ID(), false ) ==
           Approx( base * 0.5 ) );
}

// Stairs that end in a sheer drop offered only hardcoded ways down; a climbing aid with
// "sheer_drop" is now offered too.
TEST_CASE( "loop-j08_sheer_drop_climbing_aids", "[audit][climbing]" )
{
    clear_map();
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    // Nothing in the game's own data is a sheer-drop aid, so the stairs work as they did
    for( const climbing_aid &aid : climbing_aid::get_all() ) {
        if( aid.id != climbing_aid_test_m05b_sheer_drop_aid ) {
            CAPTURE( aid.id.str() );
            CHECK( !aid.down.sheer_drop );
        }
    }
    avatar &u = get_avatar();
    CHECK( climbing_aid::get_sheer_drop_aid( u ) == nullptr );
    u.set_mutation( trait_TEST_M05B_RAPPELLER );
    const climbing_aid *aid = climbing_aid::get_sheer_drop_aid( u );
    REQUIRE( aid != nullptr );
    CHECK( aid->id == climbing_aid_test_m05b_sheer_drop_aid );
    CHECK( aid->down.easy_climb_back_up == 1 );
}

// LOOP-J09: random NPC spawning was all literals, and the starting NPC was hardcoded.
TEST_CASE( "loop-j09_random_npc_settings", "[audit][npc]" )
{
    const region_settings_random_npcs defaults;
    CHECK( defaults.radius == 90 );
    CHECK( defaults.crowding_factor == 0.8f );
    CHECK( defaults.classes.empty() );
    CHECK( !defaults.common_classes );
    CHECK( defaults.spawn_eocs.empty() );

    // The game's regions keep the old numbers
    const region_settings_random_npcs &vanilla = region_settings_default->random_npcs;
    CHECK( vanilla.radius == 90 );
    CHECK( vanilla.crowding_factor == 0.8f );
    CHECK( vanilla.classes.empty() );
    CHECK( !vanilla.common_classes );

    region_settings_random_npcs parsed;
    JsonObject jo = json_loader::from_string(
                        R"({ "radius": 60, "crowding_factor": 0.5, "classes": [ [ "NC_BANDIT", 3 ] ], "common_classes": true })" );
    parsed.deserialize( jo );
    CHECK( parsed.radius == 60 );
    CHECK( parsed.crowding_factor == 0.5f );
    REQUIRE( parsed.classes.size() == 1 );
    CHECK( *parsed.classes.pick() == NC_BANDIT );
    CHECK( parsed.classes.get_specific_weight( NC_BANDIT ) == 3 );
    CHECK( parsed.common_classes );
}

TEST_CASE( "loop-j09_scenario_starting_npc", "[audit][npc]" )
{
    // No scenario in the game's data changes its starting NPC
    for( const scenario &scen : scenario::get_all() ) {
        CAPTURE( scen.ident().str() );
        CHECK( !scen.starting_npc() );
    }
    scenario_starting_npc parsed;
    JsonObject jo = json_loader::from_string(
                        R"({ "classes": [ [ "NC_BANDIT", 1 ] ], "chat": "TALK_TEST_M05B_START" })" );
    parsed.deserialize( jo );
    REQUIRE( parsed.classes.size() == 1 );
    CHECK( *parsed.classes.pick() == NC_BANDIT );
    CHECK( parsed.chat == "TALK_TEST_M05B_START" );
    CHECK( parsed.eocs.empty() );
}

// LOOP-J10: MAX_SKILL capped every skill; a skill can now have a lower max_level.
TEST_CASE( "loop-j10_skill_max_level_caps_practice", "[audit][skill]" )
{
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    CHECK( skill_test_m05b_capped_skill->max_level() == 3 );
    CHECK( skill_survival->max_level() == MAX_SKILL );
    for( const Skill &sk : Skill::skills ) {
        if( sk.ident() != skill_test_m05b_capped_skill ) {
            CAPTURE( sk.ident().str() );
            CHECK( sk.max_level() == MAX_SKILL );
        }
    }

    avatar &u = get_avatar();
    const skill_id &capped = skill_test_m05b_capped_skill;
    u.set_skill_level( capped, 2 );
    u.set_knowledge_level( capped, 2 );
    u.set_focus( 100 );
    const int below_cap = u.get_skill_level_object( capped ).exercise( true );
    u.practice( capped, 50, 10, true );
    CHECK( u.get_skill_level_object( capped ).exercise( true ) > below_cap );
    CHECK( u.get_skill_level_object( capped ).level() == 2 );

    u.set_skill_level( capped, 3 );
    u.set_knowledge_level( capped, 3 );
    u.set_focus( 100 );
    const int at_cap = u.get_skill_level_object( capped ).exercise( true );
    CHECK( !u.practice( capped, 50, 10, true ) );
    CHECK( u.get_skill_level_object( capped ).exercise( true ) == at_cap );
    CHECK( u.get_skill_level_object( capped ).level() == 3 );
}

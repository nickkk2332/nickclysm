#include <cstddef>
#include <optional>
#include <sstream>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "weather.h"
#include "weather_gen.h"
#include "weather_type.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_CLEAR_WEATHER( "EOC_TEST_CLEAR_WEATHER" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_COLD_SNAP( "EOC_TEST_COLD_SNAP" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_FREEZE( "EOC_TEST_FREEZE" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_HOLD_WIND( "EOC_TEST_HOLD_WIND" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_SET_WEATHER( "EOC_TEST_SET_WEATHER" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_SET_WEATHER_FROM_VAR( "EOC_TEST_SET_WEATHER_FROM_VAR" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_WIND_GUST( "EOC_TEST_WIND_GUST" );

static const weather_type_id weather_portal_storm( "portal_storm" );

static void run_weather_eoc( const effect_on_condition_id &eoc )
{
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    eoc->activate( d );
}

// The generator rolls wind with rng, so seed it the same way before the game's own weather
// update and before asking for the expected values.
static constexpr unsigned int wind_seed = 4242;

// What the generator gives for now, before any override
static w_point generated_weather()
{
    return get_weather().get_cur_weather_gen().get_weather( get_avatar().pos_abs(), calendar::turn,
            g->get_seed() );
}

static w_point generated_weather_seeded()
{
    rng_set_engine_seed( wind_seed );
    return generated_weather();
}

static double global_number( const std::string &name )
{
    return get_globals().get_global_value( name ).dbl();
}

// LOOP-J01: EOCs could not set the weather, and weather('windpower') or weather('temperature')
// assignments were lost at the next weather change (wind never reached gameplay at all). Now
// set_weather and weather() assignments with a 'duration' hold across weather changes and saves.
TEST_CASE( "loop-j01_eoc_weather_overrides_hold_until_they_run_out", "[audit][weather][eoc]" )
{
    clear_avatar();
    get_globals().clear_global_values();
    weather_manager &weather = get_weather();
    // Declared first so it runs last, after the turn is restored
    on_out_of_scope reset_weather( [&weather]() {
        weather.clear_eoc_overrides();
        weather.set_nextweather( calendar::turn );
    } );
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::turn_zero + 40_days + 9_hours;
    weather.clear_eoc_overrides();
    weather.set_nextweather( calendar::turn );
    REQUIRE( weather.weather_id != weather_portal_storm );

    SECTION( "set_weather holds the weather type for its duration" ) {
        run_weather_eoc( effect_on_condition_EOC_TEST_SET_WEATHER );
        CHECK( weather.weather_id == weather_portal_storm );
        const double minutes_left = global_number( "test_weather_minutes_left" );
        CHECK( minutes_left > 0 );
        CHECK( minutes_left <= 120 );

        calendar::turn += 1_hours;
        weather.set_nextweather( calendar::turn );
        CHECK( weather.weather_id == weather_portal_storm );

        calendar::turn += 1_hours;
        weather.update_weather();
        CHECK( !weather.eoc_weather.value );
        CHECK( weather.weather_id != weather_portal_storm );
    }

    SECTION( "set_weather from a variable lasts until cleared" ) {
        dialogue d( get_talker_for( get_avatar() ), nullptr );
        d.set_value( "weather_type", std::string( "portal_storm" ) );
        effect_on_condition_EOC_TEST_SET_WEATHER_FROM_VAR->activate( d );
        CHECK( weather.weather_id == weather_portal_storm );

        calendar::turn += 10_days;
        weather.set_nextweather( calendar::turn );
        CHECK( weather.weather_id == weather_portal_storm );

        run_weather_eoc( effect_on_condition_EOC_TEST_CLEAR_WEATHER );
        CHECK( weather.weather_id != weather_portal_storm );
    }

    SECTION( "wind assigned with a duration reaches gameplay and outlasts weather changes" ) {
        run_weather_eoc( effect_on_condition_EOC_TEST_HOLD_WIND );
        CHECK( weather.windspeed == 40 );
        CHECK( weather.winddirection == 90 );

        weather.set_nextweather( calendar::turn );
        CHECK( weather.windspeed == 40 );
        CHECK( weather.weather_precise->windpower == Approx( 40 ) );
        CHECK( weather.winddirection == 90 );

        calendar::turn += 1_hours;
        rng_set_engine_seed( wind_seed );
        weather.update_weather();
        CHECK( !weather.eoc_windspeed.value );
        CHECK( weather.windspeed == static_cast<int>( generated_weather_seeded().windpower ) );
        CHECK( weather.winddirection == 90 );

        rng_set_engine_seed( wind_seed );
        run_weather_eoc( effect_on_condition_EOC_TEST_CLEAR_WEATHER );
        CHECK( weather.winddirection == generated_weather_seeded().winddirection );
    }

    SECTION( "wind assigned without a duration lasts until the weather changes" ) {
        run_weather_eoc( effect_on_condition_EOC_TEST_WIND_GUST );
        CHECK( weather.windspeed == 25 );
        CHECK( !weather.eoc_windspeed.value );

        rng_set_engine_seed( wind_seed );
        weather.set_nextweather( calendar::turn );
        CHECK( weather.windspeed == static_cast<int>( generated_weather_seeded().windpower ) );
    }

    SECTION( "a temperature offset shifts the generated temperature; a held temperature wins" ) {
        const double generated_k = units::to_kelvin( weather.temperature );
        REQUIRE( generated_k == Approx( units::to_kelvin( generated_weather().temperature ) ) );

        run_weather_eoc( effect_on_condition_EOC_TEST_COLD_SNAP );
        const double cold_k = units::to_kelvin( weather.temperature );
        CHECK( cold_k == Approx( generated_k - 15 ).margin( 0.01 ) );
        CHECK( global_number( "test_weather_offset" ) == Approx( -15 ) );

        calendar::turn += 6_hours;
        weather.set_nextweather( calendar::turn );
        const double later_k = units::to_kelvin( generated_weather().temperature );
        CHECK( units::to_kelvin( weather.temperature ) == Approx( later_k - 15 ).margin( 0.01 ) );

        run_weather_eoc( effect_on_condition_EOC_TEST_FREEZE );
        weather.set_nextweather( calendar::turn );
        CHECK( units::to_celsius( weather.temperature ) == Approx( -30 ).margin( 0.01 ) );

        calendar::turn += 3_hours;
        weather.update_weather();
        CHECK( !weather.eoc_temperature.value );
        const double last_k = units::to_kelvin( generated_weather().temperature );
        CHECK( units::to_kelvin( weather.temperature ) == Approx( last_k - 15 ).margin( 0.01 ) );
    }

    SECTION( "overrides are saved with the weather" ) {
        run_weather_eoc( effect_on_condition_EOC_TEST_SET_WEATHER );
        run_weather_eoc( effect_on_condition_EOC_TEST_HOLD_WIND );
        run_weather_eoc( effect_on_condition_EOC_TEST_COLD_SNAP );
        const time_point weather_end = weather.eoc_weather.end;

        std::ostringstream os;
        JsonOut jsout( os );
        weather_manager::serialize_all( jsout );
        weather.clear_eoc_overrides();
        JsonObject jo = json_loader::from_string( os.str() );
        jo.allow_omitted_members();
        weather_manager::unserialize_all( jo );

        REQUIRE( weather.eoc_weather.value );
        CHECK( *weather.eoc_weather.value == weather_portal_storm );
        CHECK( weather.eoc_weather.end == weather_end );
        REQUIRE( weather.eoc_windspeed.value );
        CHECK( *weather.eoc_windspeed.value == Approx( 40 ) );
        CHECK( weather.eoc_windspeed.end == calendar::turn + 1_hours );
        REQUIRE( weather.eoc_wind_direction.value );
        CHECK( *weather.eoc_wind_direction.value == 90 );
        CHECK( weather.eoc_wind_direction.end == calendar::turn_max );
        REQUIRE( weather.eoc_temperature_offset.value );
        CHECK( units::to_kelvin_delta( *weather.eoc_temperature_offset.value ) == Approx( -15 ) );
        CHECK( weather.eoc_temperature_offset.end == calendar::turn + 1_days );
        CHECK( !weather.eoc_temperature.value );

        // A saved weather type that no longer exists (its mod was removed) is dropped, since
        // nothing could ever end it; the other overrides still load
        std::string saved = os.str();
        const std::string saved_weather = R"("eoc_weather":"portal_storm")";
        const size_t at = saved.find( saved_weather );
        REQUIRE( at != std::string::npos );
        saved.replace( at, saved_weather.size(), R"("eoc_weather":"audit_removed_weather")" );
        weather.clear_eoc_overrides();
        JsonObject removed = json_loader::from_string( saved );
        removed.allow_omitted_members();
        const std::string dmsg = capture_debugmsg_during( [&removed]() {
            weather_manager::unserialize_all( removed );
        } );
        CHECK_THAT( dmsg, Catch::Contains( "audit_removed_weather" ) );
        CHECK( !weather.eoc_weather.value );
        CHECK( weather.eoc_weather.end == calendar::turn_max );
        CHECK( weather.eoc_windspeed.value );
    }
}

// LOOP-J01: names in the new weather effects are checked when the data loads
TEST_CASE( "loop-j01_weather_effects_reject_unknown_names_at_load", "[audit][weather][eoc]" )
{
    const JsonObject bad_aspect = json_loader::from_string(
                                      R"({ "effect": { "clear_weather_override": "rain" } })" );
    bad_aspect.allow_omitted_members();
    CHECK_THROWS_AS( talk_effect_t( bad_aspect, "effect", "audit_weather_eoc_test" ), JsonError );

    const JsonObject bad_type = json_loader::from_string(
                                    R"({ "effect": { "set_weather": "not_a_weather_type" } })" );
    const talk_effect_t effect( bad_type, "effect", "audit_weather_eoc_test" );
    const std::string dmsg = capture_debugmsg_during( []() {
        weather_types::check_consistency();
    } );
    CHECK_THAT( dmsg, Catch::Contains( "not_a_weather_type" ) );
}

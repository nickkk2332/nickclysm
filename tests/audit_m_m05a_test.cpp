#include <cstddef>
#include <string>
#include <string_view>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_type.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "weather.h"
#include "weather_gen.h"
#include "weather_type.h"

static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_open_air( "t_open_air" );

static const weather_type_id weather_drizzle( "drizzle" );
static const weather_type_id weather_snowing( "snowing" );
static const weather_type_id weather_sunny( "sunny" );

static double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// A weather generator's JSON, with these members after the mandatory ones
static std::string generator_json( const std::string &more )
{
    return R"({ "base_temperature": 6.5, "base_humidity": 70.0, "base_pressure": 1015.0,)"
           R"( "base_wind": 3.4)" + more + " }";
}

// Grass under open sky at p, so snow on its OMT counts there
static void open_ground_at( map &here, const tripoint_bub_ms &p )
{
    here.ter_set( p, ter_t_grass );
    here.ter_set( p + tripoint::above, ter_t_open_air );
}

// LOOP-J04: the snow-to-water ratio, melt rate, maximum depth and move costs were constants in
// C++. They are now the region's weather_generator "snow" settings; the defaults are the old
// constants, and no shipped region sets them.
TEST_CASE( "loop-j04_snow_settings_default_to_the_old_constants", "[audit][weather][snow]" )
{
    const snow_settings defaults;
    CHECK( defaults.water_ratio == Approx( 12.0 ) );
    CHECK( defaults.melt_rate == Approx( 2.0 ) );
    CHECK( defaults.max_depth == Approx( 800.0 ) );

    // Characters: +20 from 100 mm, +50 from 250 mm, +100 from 500 mm; monsters 1, 2 and 4
    CHECK( defaults.move_cost_at( 0.0 ) == nullptr );
    CHECK( defaults.move_cost_at( 99.9 ) == nullptr );
    const snow_move_cost *light = defaults.move_cost_at( 100.0 );
    REQUIRE( light != nullptr );
    CHECK( light->character == 20 );
    CHECK( light->monster == 1 );
    REQUIRE( defaults.move_cost_at( 249.9 ) == light );
    const snow_move_cost *medium = defaults.move_cost_at( 250.0 );
    REQUIRE( medium != nullptr );
    CHECK( medium->character == 50 );
    CHECK( medium->monster == 2 );
    const snow_move_cost *deep = defaults.move_cost_at( 500.0 );
    REQUIRE( deep != nullptr );
    CHECK( deep->character == 100 );
    CHECK( deep->monster == 4 );
    CHECK( defaults.move_cost_at( 800.0 ) == deep );

    // The region the tests run in keeps them
    clear_avatar();
    const snow_settings &region = get_weather().get_cur_weather_gen().snow;
    CHECK( region.water_ratio == Approx( defaults.water_ratio ) );
    CHECK( region.melt_rate == Approx( defaults.melt_rate ) );
    CHECK( region.max_depth == Approx( defaults.max_depth ) );
    REQUIRE( region.move_cost.size() == defaults.move_cost.size() );
    for( size_t i = 0; i < region.move_cost.size(); ++i ) {
        CAPTURE( i );
        CHECK( region.move_cost[i].depth == Approx( defaults.move_cost[i].depth ) );
        CHECK( region.move_cost[i].character == defaults.move_cost[i].character );
        CHECK( region.move_cost[i].monster == defaults.move_cost[i].monster );
    }
}

TEST_CASE( "loop-j04_weather_generator_reads_snow_settings", "[audit][weather][snow]" )
{
    weather_generator gen;
    {
        JsonObject jo = json_loader::from_string( generator_json( R"(,
            "snow": {
                "water_ratio": 10,
                "melt_rate": 3.5,
                "max_depth": 400,
                "move_cost": [
                    { "depth": 50, "character": 10, "monster": 1 },
                    { "depth": 300, "character": 40 }
                ]
            })" ) );
        gen.load( jo, "audit_m_m05a_test" );
    }
    CHECK( gen.snow.water_ratio == Approx( 10.0 ) );
    CHECK( gen.snow.melt_rate == Approx( 3.5 ) );
    CHECK( gen.snow.max_depth == Approx( 400.0 ) );
    REQUIRE( gen.snow.move_cost.size() == 2 );
    CHECK( gen.snow.move_cost_at( 49.0 ) == nullptr );
    const snow_move_cost *shallow = gen.snow.move_cost_at( 120.0 );
    REQUIRE( shallow != nullptr );
    CHECK( shallow->character == 10 );
    CHECK( shallow->monster == 1 );
    const snow_move_cost *deep = gen.snow.move_cost_at( 350.0 );
    REQUIRE( deep != nullptr );
    CHECK( deep->character == 40 );
    // An omitted cost is 0
    CHECK( deep->monster == 0 );

    // A copy-from child that names one setting keeps the others
    gen.was_loaded = true;
    {
        JsonObject jo = json_loader::from_string( R"({ "snow": { "melt_rate": 5 } })" );
        gen.load( jo, "audit_m_m05a_test" );
    }
    CHECK( gen.snow.melt_rate == Approx( 5.0 ) );
    CHECK( gen.snow.water_ratio == Approx( 10.0 ) );
    CHECK( gen.snow.max_depth == Approx( 400.0 ) );
    CHECK( gen.snow.move_cost.size() == 2 );

    // A generator without "snow" has the defaults
    weather_generator plain;
    {
        JsonObject jo = json_loader::from_string( generator_json( "" ) );
        plain.load( jo, "audit_m_m05a_test" );
    }
    CHECK( plain.snow.water_ratio == Approx( 12.0 ) );
    CHECK( plain.snow.max_depth == Approx( 800.0 ) );
    CHECK( plain.snow.move_cost.size() == 3 );

    // Rows out of order are a load error
    weather_generator unsorted;
    JsonObject bad = json_loader::from_string( generator_json( R"(,
        "snow": { "move_cost": [ { "depth": 300, "character": 40 }, { "depth": 100 } ] })" ) );
    bad.allow_omitted_members();
    CHECK_THROWS_AS( unsorted.load( bad, "audit_m_m05a_test" ), JsonError );
}

// A weather type's snow_rate sets how fast it piles up snow; without one, snow (precipitation
// that isn't rain) piles up at its precipitation rate times the region's water_ratio, as before.
TEST_CASE( "loop-j04_weather_type_snow_rate", "[audit][weather][snow]" )
{
    const snow_settings defaults;
    // Vanilla: heavy snow is 3 mm/h of water, 36 mm/h of snow; rain adds none; clear weather none
    CHECK( snow_mm_per_hour( *weather_snowing, defaults ) == Approx( 36.0 ) );
    CHECK( snow_mm_per_hour( *weather_drizzle, defaults ) == Approx( 0.0 ) );
    CHECK( snow_mm_per_hour( *weather_sunny, defaults ) == Approx( 0.0 ) );

    weather_type wt;
    {
        JsonObject jo = json_loader::from_string( R"({
            "id": "audit_m05a_wet_snow", "name": "Audit wet snow", "sym": "*",
            "ranged_penalty": 0, "sight_penalty": 1.0, "light_modifier": 0, "priority": 1,
            "sound_attn": 0, "dangerous": false, "precip": "light", "rains": true, "snow_rate": 7.5
        })" );
        wt.load( jo, "audit_m_m05a_test" );
    }
    REQUIRE( wt.snow_rate.has_value() );
    CHECK( *wt.snow_rate == Approx( 7.5 ) );
    // Given, it holds even for rain
    CHECK( snow_mm_per_hour( wt, defaults ) == Approx( 7.5 ) );
    wt.snow_rate.reset();
    CHECK( snow_mm_per_hour( wt, defaults ) == Approx( 0.0 ) );
    wt.rains = false;
    CHECK( snow_mm_per_hour( wt, defaults ) == Approx( 1.5 * 12.0 ) );
    snow_settings wet = defaults;
    wet.water_ratio = 5.0;
    CHECK( snow_mm_per_hour( wt, wet ) == Approx( 1.5 * 5.0 ) );
}

// EOCs read and set snow depth with weather('snow_depth'), at the avatar's OMT or at a
// 'location', and read the current snowfall with weather('snow_rate').
TEST_CASE( "loop-j04_weather_snow_depth_from_math", "[audit][weather][snow][math_parser]" )
{
    clear_avatar();
    clear_map();
    weather_manager &weather = get_weather();
    weather.snow_depth_map.clear();
    on_out_of_scope clear_snow( [&weather]() {
        weather.snow_depth_map.clear();
    } );
    restore_on_out_of_scope<weather_type_id> restore_weather( weather.weather_id );

    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const tripoint_abs_omt avatar_omt = get_avatar().pos_abs_omt();

    eval_math( d, "weather('snow_depth') = 300" );
    CHECK( weather.get_snow_depth_mm( avatar_omt ) == Approx( 300.0 ) );
    CHECK( eval_math( d, "weather('snow_depth')" ) == Approx( 300.0 ) );

    // Up to the region's maximum, and not below 0
    eval_math( d, "weather('snow_depth') = 5000" );
    CHECK( weather.get_snow_depth_mm( avatar_omt ) ==
           Approx( weather.get_cur_weather_gen().snow.max_depth ) );
    eval_math( d, "weather('snow_depth') = -10" );
    CHECK( weather.get_snow_depth_mm( avatar_omt ) == Approx( 0.0 ) );

    // Somewhere else
    const tripoint_abs_omt far_omt = avatar_omt + tripoint( 5, 0, 0 );
    d.set_value( "spot", project_to<coords::ms>( far_omt ) );
    eval_math( d, "weather('snow_depth', 'location': _spot) = 120" );
    CHECK( weather.get_snow_depth_mm( far_omt ) == Approx( 120.0 ) );
    CHECK( weather.get_snow_depth_mm( avatar_omt ) == Approx( 0.0 ) );
    CHECK( eval_math( d, "weather('snow_depth', 'location': _spot)" ) == Approx( 120.0 ) );
    CHECK( eval_math( d, "weather('snow_depth', 'location': 'u')" ) == Approx( 0.0 ) );
    // The ground's snow, on any level above it
    CHECK( weather.get_snow_depth_mm( far_omt + tripoint::above ) == Approx( 120.0 ) );

    weather.weather_id = weather_snowing;
    CHECK( eval_math( d, "weather('snow_rate')" ) == Approx( 36.0 ) );
    weather.weather_id = weather_sunny;
    CHECK( eval_math( d, "weather('snow_rate')" ) == Approx( 0.0 ) );

    // Snow on the ground is not an override, and the other aspects have no place
    CHECK_THROWS_AS( eval_math( d, "weather('snow_depth', 'duration': '1 h') = 5" ),
                     math::runtime_error );
    CHECK_THROWS_AS( eval_math( d, "weather('temperature', 'location': 'u')" ),
                     math::runtime_error );
}

// Movement: the region's table, the same costs as the old literals by default
TEST_CASE( "loop-j04_snow_move_costs_follow_the_region_table", "[audit][weather][snow]" )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    weather_manager &weather = get_weather();
    weather.snow_depth_map.clear();
    on_out_of_scope clean_up( [&weather]() {
        weather.snow_depth_map.clear();
        clear_creatures();
    } );

    map &here = get_map();
    Character &player = get_player_character();
    const tripoint_bub_ms player_pos = player.pos_bub();
    const tripoint_bub_ms mon_pos = player_pos + tripoint( 3, 0, 0 );
    const tripoint_bub_ms mon_dest = mon_pos + tripoint::east;
    open_ground_at( here, player_pos );
    open_ground_at( here, mon_pos );
    open_ground_at( here, mon_dest );
    here.build_map_cache( player_pos.z() );
    here.build_map_cache( player_pos.z() + 1 );
    monster &zombie = spawn_test_monster( "mon_zombie", mon_pos );
    REQUIRE( here.is_outside( player_pos ) );
    REQUIRE( here.is_outside( mon_pos ) );

    const auto set_depth = [&]( double depth ) {
        weather.set_snow_depth_mm( player.pos_abs_omt(), depth );
        weather.set_snow_depth_mm( zombie.pos_abs_omt(), depth );
    };
    const auto player_cost = [&]( double depth ) {
        set_depth( depth );
        return player.run_cost( 100 );
    };
    const auto zombie_cost = [&]( double depth ) {
        set_depth( depth );
        return zombie.calc_movecost( here, mon_pos, mon_dest );
    };

    const int player_bare = player_cost( 0.0 );
    CHECK( player_cost( 99.0 ) == player_bare );
    const int player_light = player_cost( 150.0 );
    CHECK( player_light > player_bare );
    CHECK( player_cost( 100.0 ) == player_light );
    const int player_medium = player_cost( 300.0 );
    CHECK( player_medium > player_light );
    CHECK( player_cost( 600.0 ) > player_medium );

    // A monster's cost is 25 move points per point of terrain cost
    const int zombie_bare = zombie_cost( 0.0 );
    CHECK( zombie_cost( 99.0 ) == zombie_bare );
    CHECK( zombie_cost( 150.0 ) - zombie_bare == 25 );
    CHECK( zombie_cost( 300.0 ) - zombie_bare == 50 );
    CHECK( zombie_cost( 600.0 ) - zombie_bare == 100 );
}

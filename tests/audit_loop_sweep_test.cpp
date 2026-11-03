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
#include "save_fault.h"
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

// ---- LOOP-B13 ----

// get_weather rolled the wind with the game's random numbers, so asking twice about the same
// time and place gave different wind (and so rain storms and fog could come and go), and every
// question could turn the live wind.
TEST_CASE( "loop-b13_generated_weather_depends_only_on_time_place_and_seed", "[audit][weather]" )
{
    const weather_generator &wgen = get_weather().get_cur_weather_gen();
    REQUIRE( wgen.base_wind > 0 );
    restore_on_out_of_scope restore_direction( weather_generator::current_winddir );
    weather_generator::current_winddir = 90;
    const tripoint_abs_ms where = get_avatar().pos_abs();
    const unsigned seed = 1234;
    int different = 0;
    for( int i = 0; i < 300; ++i ) {
        const time_point when = calendar::turn_zero + 7_hours * i + 13_minutes * ( i % 5 );
        const w_point first = wgen.get_weather( where, when, seed );
        const w_point again = wgen.get_weather( where, when, seed );
        if( first.windpower != again.windpower || first.winddirection != again.winddirection ) {
            different++;
        }
    }
    CHECK( different == 0 );
    // Asking about any time leaves the live wind's direction alone
    CHECK( weather_generator::current_winddir == 90 );
}

// ---- LOOP-B14 ----

namespace
{
// Makes the given save steps fail until it goes out of scope
class failing_save_steps
{
    public:
        explicit failing_save_steps( const std::set<std::string> &steps ) {
            save_fault::failing_steps() = steps;
            save_fault::started_steps().clear();
        }
        failing_save_steps( const failing_save_steps & ) = delete;
        failing_save_steps &operator=( const failing_save_steps & ) = delete;
        ~failing_save_steps() {
            save_fault::failing_steps().clear();
            save_fault::started_steps().clear();
        }
};

// Puts back everything a save writes: the test world's folder, its list of saves and the
// last-world record outside it. The test world is otherwise only saved when the run ends, and
// later tests rely on that.
class keep_save_files
{
    public:
        keep_save_files() : world( PATH_INFO::world_base_save_path().get_unrelative_path() ),
            saves( world_generator->active_world->world_saves ),
            last_world_name( world_generator->last_world_name ),
            last_character_name( world_generator->last_character_name ) {
            remember( world );
            remember( PATH_INFO::lastworld().get_unrelative_path() );
        }
        keep_save_files( const keep_save_files & ) = delete;
        keep_save_files &operator=( const keep_save_files & ) = delete;
        ~keep_save_files() {
            std::vector<std::filesystem::path> added;
            if( std::filesystem::exists( world ) ) {
                for( const auto &entry : std::filesystem::recursive_directory_iterator( world ) ) {
                    if( !dirs.count( entry.path() ) && !files.count( entry.path() ) ) {
                        added.push_back( entry.path() );
                    }
                }
            }
            const std::filesystem::path lastworld = PATH_INFO::lastworld().get_unrelative_path();
            if( !files.count( lastworld ) ) {
                added.push_back( lastworld );
            }
            for( const std::filesystem::path &path : added ) {
                std::error_code ec;
                std::filesystem::remove_all( path, ec );
            }
            for( const auto &file : files ) {
                std::ofstream out( file.first, std::ios::binary | std::ios::trunc );
                out << file.second;
            }
            world_generator->active_world->world_saves = saves;
            world_generator->last_world_name = last_world_name;
            world_generator->last_character_name = last_character_name;
        }
    private:
        void remember( const std::filesystem::path &path ) {
            if( std::filesystem::is_regular_file( path ) ) {
                std::ifstream in( path, std::ios::binary );
                std::ostringstream ss;
                ss << in.rdbuf();
                files[path] = ss.str();
            } else if( std::filesystem::is_directory( path ) ) {
                dirs.insert( path );
                for( const auto &entry : std::filesystem::directory_iterator( path ) ) {
                    remember( entry.path() );
                }
            }
        }
        std::filesystem::path world;
        std::map<std::filesystem::path, std::string> files;
        std::set<std::filesystem::path> dirs;
        std::vector<save_t> saves;
        std::string last_world_name;
        std::string last_character_name;
};
} // namespace

static bool save_step_started( const std::string &step )
{
    const std::vector<std::string> &started = save_fault::started_steps();
    return std::find( started.begin(), started.end(), step ) != started.end();
}

// game::save() wrote the player file first, so when a later step failed the world on disk
// was older than the player: items picked up since the last save existed twice on reload and
// items dropped were gone.
TEST_CASE( "loop-b14_a_failed_world_save_leaves_the_player_file_alone", "[audit][savegame]" )
{
    REQUIRE( !g->save_is_dirty );
    keep_save_files keep_files;
    failing_save_steps fail( { "maps" } );
    bool saved = true;
    const std::string message = capture_debugmsg_during( [&saved]() {
        saved = g->save();
    } );
    CHECK( !saved );
    CHECK( message.find( "game not saved" ) != std::string::npos );
    CHECK( save_step_started( "maps" ) );
    CHECK( !save_step_started( "player" ) );
}

// Achievements go to a folder shared by every world and the options record is only for
// debugging, but failing to write either one stopped the whole save.
TEST_CASE( "loop-b14_achievements_and_the_options_record_do_not_stop_a_save",
           "[audit][savegame]" )
{
    REQUIRE( !g->save_is_dirty );
    keep_save_files keep_files;
    failing_save_steps fail( { "achievements", "options_record" } );
    CHECK( g->save() );
    CHECK( save_step_started( "maps" ) );
    CHECK( save_step_started( "player" ) );
}

static const dimension_id dimension_scarlet( "scarlet" );

static const itype_id itype_rock( "rock" );

static const mtype_id mon_zombie( "mon_zombie" );

static const vproto_id vehicle_prototype_bicycle( "bicycle" );

// Dimension travel took the travelling items, vehicle and NPCs out of the world before it
// saved, and when the save failed it returned with them still out: they were lost.
TEST_CASE( "loop-b14_a_failed_dimension_travel_save_loses_nothing", "[audit][savegame]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot( 60, 60, 0 );
    u.setpos( here, spot );

    const tripoint_bub_ms rock_spot = spot + tripoint_rel_ms( 1, 0, 0 );
    item &rock = here.add_item( rock_spot, item( itype_rock ) );
    REQUIRE( rock.typeId() == itype_rock );
    const item_location rock_loc( map_cursor( here.get_abs( rock_spot ) ), &rock );
    const tripoint_bub_ms bike_spot = spot + tripoint_rel_ms( 0, 5, 0 );
    vehicle *bike = here.add_vehicle( vehicle_prototype_bicycle, bike_spot, 0_degrees, 0,
                                      veh_spawn_status::UNDAMAGED );
    REQUIRE( bike != nullptr );
    npc &guy = spawn_npc( ( spot + tripoint_rel_ms( 3, 0, 0 ) ).xy(), "test_talker" );
    const character_id guy_id = guy.getID();
    const tripoint_bub_ms zed_spot = spot + tripoint_rel_ms( -5, 0, 0 );
    spawn_test_monster( "mon_zombie", zed_spot );

    {
        failing_save_steps fail( { "maps" } );
        CHECK( !g->travel_to_dimension( dimension_scarlet, { &guy }, { rock_loc }, rock_spot, bike ) );
    }

    int rocks = 0;
    for( const item &it : here.i_at( rock_spot ) ) {
        if( it.typeId() == itype_rock ) {
            rocks++;
        }
    }
    CHECK( rocks == 1 );
    const optional_vpart_position bike_part = here.veh_at( bike_spot );
    CHECK( bike_part.has_value() );
    CHECK( g->find_npc( guy_id ) != nullptr );
    int zombies = 0;
    for( const monster &critter : g->all_monsters() ) {
        if( critter.type->id == mon_zombie && rl_dist( critter.pos_bub( here ), zed_spot ) <= 1 ) {
            zombies++;
        }
    }
    CHECK( zombies == 1 );

    clear_map();
    clear_avatar();
}

// ---- LOOP-B15 ----

static const efftype_id effect_test_loop_b15_weather_mark( "test_loop_b15_weather_mark" );

static const weather_type_id weather_test_loop_b15_reaching( "test_loop_b15_reaching_weather" );

// Weather passive effects reached every creature in the bubble, underground ones too, but only
// while the avatar was above ground and out of the water.
TEST_CASE( "loop-b15_weather_effects_reach_creatures_by_their_own_level", "[audit][weather]" )
{
    clear_map( -1, 1 );
    clear_avatar();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms surface( 60, 60, 0 );
    const tripoint_bub_ms cellar( 64, 60, -1 );
    u.setpos( here, surface + tripoint_rel_ms( -10, 0, 0 ) );
    here.ter_set( cellar, ter_t_floor );
    here.ter_set( cellar + tripoint_rel_ms( -2, 0, 0 ), ter_t_floor );
    monster &up_top = spawn_test_monster( "mon_zombie", surface );
    monster &down_below = spawn_test_monster( "mon_zombie", cellar );

    SECTION( "with the avatar on the surface" ) {
        u.setpos( here, surface + tripoint_rel_ms( -10, 0, 0 ) );
    }
    SECTION( "with the avatar underground" ) {
        u.setpos( here, cellar + tripoint_rel_ms( -2, 0, 0 ) );
    }
    handle_weather_effects( weather_test_loop_b15_reaching );
    CHECK( up_top.has_effect( effect_test_loop_b15_weather_mark ) );
    CHECK( !down_below.has_effect( effect_test_loop_b15_weather_mark ) );

    clear_map( -1, 1 );
    clear_avatar();
}

static const weather_type_id weather_drizzle( "drizzle" );

// Weather now runs wherever the avatar is, but the avatar's own share still waits for it to
// come out of the water, as it did when the game loop skipped all weather for a diver.
TEST_CASE( "loop-b15_rain_does_not_wet_an_avatar_under_water", "[audit][weather]" )
{
    clear_map();
    clear_avatar();
    restore_on_out_of_scope restore_turn( calendar::turn );
    map &here = get_map();
    avatar &u = get_avatar();
    u.setpos( here, tripoint_bub_ms( 60, 60, 0 ) );
    u.clear_worn();
    u.set_wielded_item( item() );
    const auto torso_wetness_after_a_drizzle = [&]() {
        for( const bodypart_id &bp : u.get_all_body_parts() ) {
            u.set_part_wetness( bp, 0 );
        }
        calendar::turn = calendar::turn_zero;
        for( int i = 0; i < 60; ++i ) {
            calendar::turn += 6_seconds;
            handle_weather_effects( weather_drizzle );
        }
        return u.get_part_wetness( body_part_torso );
    };
    REQUIRE( torso_wetness_after_a_drizzle() > 0 );
    u.set_underwater( true );
    CHECK( torso_wetness_after_a_drizzle() == 0 );
    u.set_underwater( false );
    clear_avatar();
}

// is_creature_outside (u_is_outside, npc_is_outside) looked at the map's own level, not the
// creature's.
TEST_CASE( "loop-b15_is_outside_looks_at_the_creature_s_own_level", "[audit][weather]" )
{
    clear_map( -1, 1 );
    map &here = get_map();
    // A roof deck on the level above an indoor floor
    const tripoint_bub_ms deck( 60, 60, 1 );
    here.ter_set( deck, ter_t_flat_roof );
    here.ter_set( deck + tripoint_rel_ms( 0, 0, -1 ), ter_t_floor );
    here.invalidate_map_cache( 0 );
    here.invalidate_map_cache( 1 );
    here.build_map_cache( 0, true );
    REQUIRE( here.get_abs_sub().z() == 0 );
    REQUIRE( here.is_outside( deck ) );
    REQUIRE( !here.is_outside( deck + tripoint_rel_ms( 0, 0, -1 ) ) );
    const monster &on_deck = spawn_test_monster( "mon_zombie", deck );
    CHECK( is_creature_outside( on_deck ) );
    clear_map( -1, 1 );
}

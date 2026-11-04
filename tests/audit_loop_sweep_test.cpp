#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
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
#include "filesystem.h"
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
#include "mapbuffer.h"
#include "mod_setting.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options_helpers.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "player_helpers.h"
#include "point.h"
#include "save_fault.h"
#include "submap.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"
#include "worldfactory.h"
#include "weather_gen.h"
#include "weather_type.h"
#include "worldfactory.h"

static const mod_setting_id mod_setting_test_ms_switch( "test_ms_switch" );

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
    restore_on_out_of_scope restore_turn( calendar::turn );
    const time_point start = calendar::turn;
    // The submaps of the tree's map square, which the load below catches up: put back as
    // touched at the real time, however the helper ends
    std::vector<submap *> submaps;
    const on_out_of_scope touch_back( [&submaps, start]() {
        for( submap *sm : submaps ) {
            sm->last_touched = start;
        }
        clear_map();
    } );
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
    avatar &u = get_avatar();
    // The bionic, its power and the monsters go however the test ends
    const on_out_of_scope cleanup( [&u]() {
        clear_map();
        clear_avatar();
        u.set_power_level( 0_kJ );
    } );
    set_time_to_day();
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

// The weather's temperature modifier was added on every level, so the scarlet storms' chill
// reached cellars and labs as well. It applies above ground only, indoors as well as out.
TEST_CASE( "loop-b09_a_storm_chills_the_surface_but_not_the_cellar",
           "[audit][weather][temperature]" )
{
    clear_map( -1, 0 );
    weather_manager &weather = get_weather();
    const on_out_of_scope clear_cache( [&weather]() {
        weather.clear_temp_cache();
    } );
    // A new game's first turn leaves out every local modifier
    restore_on_out_of_scope restore_new_game( g->new_game );
    g->new_game = false;
    scoped_weather_override storm( weather_scarlet_storm_5 );
    weather.clear_temp_cache();
    REQUIRE( weather.weather_id == weather_scarlet_storm_5 );
    REQUIRE( !weather.forced_temperature );
    const double generated_k = units::to_kelvin( weather.temperature );
    const double underground_k =
        units::to_kelvin( units::from_celsius( weather.get_cur_weather_gen().base_temperature ) );
    CHECK( units::to_kelvin( weather.get_temperature( tripoint_bub_ms( 60, 60, 0 ) ) ) ==
           Approx( generated_k - 30 ).margin( 0.01 ) );
    CHECK( units::to_kelvin( weather.get_temperature( tripoint_bub_ms( 60, 60, -1 ) ) ) ==
           Approx( underground_k ).margin( 0.01 ) );
}

// ---- LOOP-B11 ----

static const ter_str_id ter_t_flat_roof( "t_flat_roof" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_open_air( "t_open_air" );
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
// Sets which save steps fail and which pass without writing, until it goes out of scope
class save_steps
{
    public:
        save_steps( const std::set<std::string> &failing, const std::set<std::string> &skipped ) {
            save_fault::failing_steps() = failing;
            save_fault::skipped_steps() = skipped;
            save_fault::started_steps().clear();
        }
        save_steps( const save_steps & ) = delete;
        save_steps &operator=( const save_steps & ) = delete;
        ~save_steps() {
            save_fault::failing_steps().clear();
            save_fault::skipped_steps().clear();
            save_fault::started_steps().clear();
        }
};

// Puts back everything a save writes: the test world's folder, its list of saves and the
// last-world record outside it. The test world is otherwise only saved when the run ends, and
// later tests rely on that. The tests skip the world's own steps, so nothing should be left
// for this to do but the few small files a save always writes.
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

// A real save of the maps would drop the submaps outside the bubble from memory
static size_t submaps_in_memory()
{
    size_t count = 0;
    for( auto &submap_entry : MAPBUFFER ) {
        static_cast<void>( submap_entry );
        count++;
    }
    return count;
}

// The player's save files as they are on disk, the staged copy included
static std::string player_save_files()
{
    std::string state;
    const std::filesystem::path base = PATH_INFO::player_base_save_path().get_unrelative_path();
    for( const char *suffix : {
             ".sav", ".sav.staged", ".sav.zzip", ".sav.zzip.staged"
         } ) {
        std::filesystem::path path = base;
        path.concat( suffix ); // NOLINT(cata-u8-path)
        state += suffix;
        if( std::filesystem::exists( path ) ) {
            std::ifstream in( path, std::ios::binary );
            std::ostringstream ss;
            ss << in.rdbuf();
            state += ": " + ss.str() + "\n";
        } else {
            state += ": none\n";
        }
    }
    return state;
}

// Runs a save that is expected to fail, and returns its debug message
static std::string failed_save_message()
{
    bool saved = true;
    const std::string message = capture_debugmsg_during( [&saved]() {
        saved = g->save();
    } );
    CHECK( !saved );
    return message;
}

// game::save() wrote the player file first, so when a later step failed the world on disk
// was older than the player: items picked up since the last save existed twice on reload and
// items dropped were gone. Now the player file is written under a temporary name first and
// only takes the old one's place once the world is saved.
TEST_CASE( "loop-b14_a_failed_world_save_leaves_the_player_file_alone", "[audit][savegame]" )
{
    restore_on_out_of_scope restore_dirty( g->save_is_dirty );
    g->save_is_dirty = false;
    keep_save_files keep_files;
    const size_t submaps = submaps_in_memory();
    const std::string player_files = player_save_files();
    {
        // The player file is staged for real
        save_steps steps( { "maps" }, { "master", "dimension" } );
        CHECK( failed_save_message().find( "game not saved" ) != std::string::npos );
        CHECK( save_step_started( "player" ) );
        CHECK( save_step_started( "maps" ) );
    }
    // The staged file is gone and the old player file is as it was
    CHECK( player_save_files() == player_files );
    CHECK( submaps_in_memory() == submaps );
}

// A player file that couldn't be written (LOOP-B04: a save locked by a sync tool or an
// antivirus) failed only after the world was saved, leaving the world newer than the player.
TEST_CASE( "loop-b14_a_player_file_that_cannot_be_written_stops_the_save_before_the_world",
           "[audit][savegame]" )
{
    restore_on_out_of_scope restore_dirty( g->save_is_dirty );
    g->save_is_dirty = false;
    keep_save_files keep_files;
    const std::string player_files = player_save_files();
    {
        save_steps steps( { "player" }, { "master" } );
        CHECK( failed_save_message().find( "game not saved" ) != std::string::npos );
        CHECK( save_step_started( "player" ) );
        // The world is left as it was, matching the player file
        CHECK( !save_step_started( "dimension" ) );
        CHECK( !save_step_started( "maps" ) );
    }
    CHECK( player_save_files() == player_files );
}

// Achievements go to a folder shared by every world and the options record is only for
// debugging, but failing to write either one stopped the whole save.
TEST_CASE( "loop-b14_achievements_and_the_options_record_do_not_stop_a_save",
           "[audit][savegame]" )
{
    restore_on_out_of_scope restore_dirty( g->save_is_dirty );
    g->save_is_dirty = false;
    keep_save_files keep_files;
    const size_t submaps = submaps_in_memory();
    {
        save_steps steps( { "achievements", "options_record" },
        { "master", "player", "dimension", "maps" } );
        CHECK( g->save() );
        CHECK( save_step_started( "maps" ) );
        CHECK( save_step_started( "player" ) );
        CHECK( save_step_started( "achievements" ) );
    }
    CHECK( submaps_in_memory() == submaps );
}

// Mod settings changed in game (options menu, EOCs) are written to the world's options by the
// game save, as of it (doc/JSON/MOD_SETTINGS.md).  Until then the file keeps the values of the
// last save, whatever else saves it, so a game quit without saving doesn't keep them.
TEST_CASE( "mod_settings_changed_in_game_are_saved_with_the_game", "[mod_settings][savegame]" )
{
    restore_on_out_of_scope restore_dirty( g->save_is_dirty );
    g->save_is_dirty = false;
    keep_save_files keep_files;
    WORLD &world = *world_generator->active_world;
    const mod_setting_values values = world.mod_settings;
    const bool was_unsaved = mod_settings::have_unsaved_changes();
    on_out_of_scope restore_settings( [&world, &values, was_unsaved]() {
        world.mod_settings = values;
        if( !was_unsaved ) {
            mod_settings::mark_saved();
        }
    } );
    const auto on_disk = [&world]() {
        return read_entire_file( ( world.folder_path() /
                                   PATH_INFO::worldoptions() ).get_unrelative_path() );
    };
    world.mod_settings.clear();
    mod_settings::mark_saved();
    REQUIRE( world.save() );
    REQUIRE( on_disk().find( "test_ms_switch" ) == std::string::npos );

    REQUIRE( mod_settings::set( *mod_setting_test_ms_switch, true ).empty() );
    CHECK( mod_settings::have_unsaved_changes() );
    // the options menu saves the world when world options change: the file keeps the last save's
    REQUIRE( world.save() );
    CHECK( on_disk().find( "test_ms_switch" ) == std::string::npos );

    const std::set<std::string> skipped = {
        "master", "player", "dimension", "maps", "achievements", "options_record"
    };
    {
        // the game is saved when they can't be written; they wait for the next save
        save_steps steps( { "mod_settings" }, skipped );
        CHECK( g->save() );
        CHECK( save_step_started( "mod_settings" ) );
    }
    CHECK( mod_settings::have_unsaved_changes() );
    CHECK( on_disk().find( "test_ms_switch" ) == std::string::npos );
    {
        save_steps steps( {}, skipped );
        CHECK( g->save() );
        CHECK( save_step_started( "mod_settings" ) );
    }
    CHECK_FALSE( mod_settings::have_unsaved_changes() );
    CHECK( on_disk().find( "\"test_ms_switch\":true" ) != std::string::npos );
}

static const dimension_id dimension_scarlet( "scarlet" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_rock( "rock" );

static const mtype_id mon_zombie( "mon_zombie" );

static const vproto_id vehicle_prototype_bicycle( "bicycle" );

static int items_of_type_at( const tripoint_bub_ms &p, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            count++;
        }
    }
    return count;
}

// Dimension travel took the travelling items, vehicle and NPCs out of the world before it
// saved, and when the save failed it returned with them still out: they were lost. Now it saves
// before taking anything out, and if the save after that fails it puts everything back where it
// was: items on their own tiles, riders in their seats, monsters without new ones.
TEST_CASE( "loop-b14_a_failed_dimension_travel_save_loses_nothing", "[audit][savegame]" )
{
    clear_map();
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );
    keep_save_files keep_files;
    const size_t submaps = submaps_in_memory();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot( 60, 60, 0 );
    u.setpos( here, spot );

    // Items on two tiles, gathered around the spot
    const tripoint_bub_ms rock_spot = spot + tripoint_rel_ms( 1, 0, 0 );
    const tripoint_bub_ms plank_spot = spot + tripoint_rel_ms( -1, 1, 0 );
    item &rock = here.add_item( rock_spot, item( itype_rock ) );
    item &plank = here.add_item( plank_spot, item( itype_2x4 ) );
    REQUIRE( rock.typeId() == itype_rock );
    REQUIRE( plank.typeId() == itype_2x4 );
    const std::vector<item_location> items = {
        item_location( map_cursor( here.get_abs( rock_spot ) ), &rock ),
        item_location( map_cursor( here.get_abs( plank_spot ) ), &plank )
    };

    // A bicycle with the travelling NPC in the saddle
    const tripoint_bub_ms bike_spot = spot + tripoint_rel_ms( 0, 5, 0 );
    vehicle *bike = here.add_vehicle( vehicle_prototype_bicycle, bike_spot, 0_degrees, 0,
                                      veh_spawn_status::UNDAMAGED );
    REQUIRE( bike != nullptr );
    std::optional<tripoint_bub_ms> seat;
    for( const vpart_reference &vp : bike->get_avail_parts( VPFLAG_BOARDABLE ) ) {
        seat = vp.pos_bub( here );
        break;
    }
    REQUIRE( seat.has_value() );
    npc &rider = spawn_npc( ( spot + tripoint_rel_ms( 3, 3, 0 ) ).xy(), "test_talker" );
    here.board_vehicle( *seat, &rider );
    REQUIRE( rider.in_vehicle );
    const character_id rider_id = rider.getID();

    const tripoint_bub_ms zed_spot = spot + tripoint_rel_ms( -5, 0, 0 );
    spawn_test_monster( "mon_zombie", zed_spot );

    SECTION( "the save before the trip fails, so nothing is taken out" ) {
        save_steps steps( { "maps" }, { "dimension" } );
        CHECK( !g->travel_to_dimension( dimension_scarlet, { &rider }, items, spot, bike ) );
        CHECK( !save_step_started( "travel_save" ) );
    }
    SECTION( "the save without the travellers fails, so they are put back" ) {
        save_steps steps( { "travel_save" }, { "dimension", "maps" } );
        CHECK( !g->travel_to_dimension( dimension_scarlet, { &rider }, items, spot, bike ) );
        CHECK( save_step_started( "travel_save" ) );
    }

    CHECK( items_of_type_at( rock_spot, itype_rock ) == 1 );
    CHECK( items_of_type_at( plank_spot, itype_2x4 ) == 1 );
    CHECK( here.i_at( spot ).empty() );
    const optional_vpart_position bike_part = here.veh_at( bike_spot );
    REQUIRE( bike_part.has_value() );
    npc *const back = g->find_npc( rider_id );
    REQUIRE( back != nullptr );
    CHECK( back->in_vehicle );
    CHECK( back->pos_bub( here ) == *seat );
    const optional_vpart_position seat_part = here.veh_at( *seat );
    REQUIRE( seat_part.has_value() );
    CHECK( seat_part->vehicle().get_passenger( static_cast<int>( seat_part->part_index() ) ) == back );
    int zombies = 0;
    for( const monster &critter : g->all_monsters() ) {
        if( critter.type->id == mon_zombie && rl_dist( critter.pos_bub( here ), zed_spot ) <= 1 ) {
            zombies++;
        }
    }
    CHECK( zombies == 1 );
    CHECK( submaps_in_memory() == submaps );
}

// ---- LOOP-B15 ----

static const efftype_id effect_test_loop_b15_weather_mark( "test_loop_b15_weather_mark" );

static const weather_type_id weather_test_loop_b15_reaching( "test_loop_b15_reaching_weather" );

// Weather passive effects reached every creature in the bubble, underground ones too, but only
// while the avatar was above ground and out of the water. Now they reach the creatures above
// ground, and underground ones only under open sky, as rain does.
TEST_CASE( "loop-b15_weather_effects_reach_creatures_by_their_own_level", "[audit][weather]" )
{
    clear_map( -1, 1 );
    clear_avatar();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms surface( 60, 60, 0 );
    const tripoint_bub_ms cellar( 64, 60, -1 );
    const tripoint_bub_ms pit( 68, 60, -1 );
    u.setpos( here, surface + tripoint_rel_ms( -10, 0, 0 ) );
    here.ter_set( cellar, ter_t_floor );
    here.ter_set( cellar + tripoint_rel_ms( -2, 0, 0 ), ter_t_floor );
    // A pit: a floor one level down, open to the sky
    here.ter_set( pit, ter_t_floor );
    here.ter_set( pit + tripoint_rel_ms( 0, 0, 1 ), ter_t_open_air );
    REQUIRE( !here.is_roofed( pit ) );
    REQUIRE( here.is_roofed( cellar ) );
    monster &up_top = spawn_test_monster( "mon_zombie", surface );
    monster &down_below = spawn_test_monster( "mon_zombie", cellar );
    monster &in_the_pit = spawn_test_monster( "mon_zombie", pit );

    SECTION( "with the avatar on the surface" ) {
        u.setpos( here, surface + tripoint_rel_ms( -10, 0, 0 ) );
    }
    SECTION( "with the avatar underground" ) {
        u.setpos( here, cellar + tripoint_rel_ms( -2, 0, 0 ) );
    }
    handle_weather_effects( weather_test_loop_b15_reaching );
    CHECK( up_top.has_effect( effect_test_loop_b15_weather_mark ) );
    CHECK( !down_below.has_effect( effect_test_loop_b15_weather_mark ) );
    CHECK( in_the_pit.has_effect( effect_test_loop_b15_weather_mark ) );

    clear_map( -1, 1 );
    clear_avatar();
}

// The game loop ran the weather only while the avatar was above ground and out of the water,
// so while it was in a cellar nothing on the surface got the weather.
TEST_CASE( "loop-b15_the_game_loop_runs_the_weather_with_the_avatar_underground",
           "[audit][weather][game_loop]" )
{
    clear_map( -1, 1 );
    clear_avatar();
    map &here = get_map();
    avatar &u = get_avatar();
    weather_manager &weather = get_weather();
    override_option no_autosave( "AUTOSAVE", "false" );
    override_option normal_weather( "ETERNAL_WEATHER", "normal" );
    restore_on_out_of_scope restore_direction( weather_generator::current_winddir );
    const on_out_of_scope weather_back( [&weather]() {
        weather.eoc_weather.reset();
        weather.set_nextweather( calendar::turn );
    } );
    // The tests' game is a new one, so the turn doesn't advance the calendar (which would need
    // a game mode the tests don't have); it does all the rest
    restore_on_out_of_scope restore_new_game( g->new_game );
    restore_on_out_of_scope restore_turn( calendar::turn );
    const on_out_of_scope back_up( [&here]() {
        here.vertical_shift( 0 );
        clear_map( -1, 1 );
        clear_avatar();
    } );
    g->new_game = true;
    // Between minutes, hours and days, so no hourly or daily work runs in the turn
    calendar::turn = calendar::turn_zero + 2_days + 12_hours + 17_turns;
    // The weather as an EOC sets it: a new game's turn clears the debug override
    weather.eoc_weather.set( weather_test_loop_b15_reaching, calendar::turn_max );
    weather.set_nextweather( calendar::turn );
    REQUIRE( weather.weather_id == weather_test_loop_b15_reaching );

    const tripoint_bub_ms cellar( 60, 60, -1 );
    here.ter_set( cellar, ter_t_floor );
    u.setpos( here, cellar );
    // The bubble's own level follows the avatar down, as it does in the game
    here.vertical_shift( -1 );
    REQUIRE( here.get_abs_sub().z() == -1 );
    monster &up_top = spawn_test_monster( "mon_test_speed_desc_base_immobile",
                                          tripoint_bub_ms( 70, 60, 0 ) );
    // With no moves left the turn doesn't wait for the player's input
    u.set_moves( -1000 );
    g->do_turn();
    REQUIRE( weather.weather_id == weather_test_loop_b15_reaching );
    CHECK( up_top.has_effect( effect_test_loop_b15_weather_mark ) );
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

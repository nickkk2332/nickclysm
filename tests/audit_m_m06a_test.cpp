#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "enums.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "json_loader.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "mtype.h"
#include "player_helpers.h"
#include "point.h"
#include "string_formatter.h"
#include "submap.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "veh_appliance.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"

static const field_type_str_id field_fd_fire( "fd_fire" );
static const field_type_str_id field_fd_smoke( "fd_smoke" );
static const field_type_str_id field_test_fd_m06a_spawner( "test_fd_m06a_spawner" );

static const furn_str_id furn_f_chair( "f_chair" );
static const furn_str_id furn_f_test_m06a_cold_box( "f_test_m06a_cold_box" );

static const itype_id itype_bucket( "bucket" );
static const itype_id itype_felt_patch( "felt_patch" );
static const itype_id itype_maple_sap( "maple_sap" );
static const itype_id itype_sheet_cotton( "sheet_cotton" );
static const itype_id itype_tshirt( "tshirt" );
static const itype_id itype_water_clean( "water_clean" );

static const mtype_id mon_test_zombie( "mon_test_zombie" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_dirtmound( "t_dirtmound" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_ice_dp_thin( "t_ice_dp_thin" );
static const ter_str_id ter_t_rootcellar( "t_rootcellar" );
static const ter_str_id ter_t_stump( "t_stump" );
static const ter_str_id ter_t_test_m06a_hardy_shrub( "t_test_m06a_hardy_shrub" );
static const ter_str_id ter_t_test_m06a_spring( "t_test_m06a_spring" );
static const ter_str_id ter_t_test_m06a_tree( "t_test_m06a_tree" );
static const ter_str_id ter_t_tree_deadpine( "t_tree_deadpine" );
static const ter_str_id ter_t_tree_maple_tapped( "t_tree_maple_tapped" );
static const ter_str_id ter_t_tree_pine( "t_tree_pine" );
static const ter_str_id ter_t_water_dp( "t_water_dp" );

static const vpart_id vpart_ap_test_m06a_dryer( "ap_test_m06a_dryer" );

namespace
{

int test_zombies_on_map()
{
    int count = 0;
    for( const monster &critter : g->all_monsters() ) {
        if( critter.type->id == mon_test_zombie ) {
            count++;
        }
    }
    return count;
}

double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_m_m06a_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

// Charges of a liquid in the containers on a tile
int liquid_on( const tripoint_bub_ms &p, const itype_id &liquid )
{
    int charges = 0;
    for( const item &container : get_map().i_at( p ) ) {
        for( const item *it : container.all_items_top() ) {
            if( it->typeId() == liquid ) {
                charges += it->charges;
            }
        }
    }
    return charges;
}

// The submaps of the map square around p, which a load over it catches up
std::vector<submap *> submaps_of_omt( const tripoint_bub_ms &p )
{
    map &here = get_map();
    std::vector<submap *> submaps;
    const tripoint_abs_omt omt = project_to<coords::omt>( here.get_abs( p ) );
    for( int dx = 0; dx < SEEX * 2; dx += SEEX ) {
        for( int dy = 0; dy < SEEY * 2; dy += SEEY ) {
            const tripoint_abs_ms corner = project_to<coords::ms>( omt ) + tripoint_rel_ms( dx, dy, 0 );
            tripoint_bub_ms at = here.get_bub( corner );
            point_sm_ms l;
            submaps.push_back( map_meddler::unsafe_get_submap_at( at, l ) );
        }
    }
    return submaps;
}

// Loading a map over p catches its submap up on `elapsed` of missed time (map::actualize)
void catch_up( const tripoint_bub_ms &p, const time_duration &elapsed )
{
    tripoint_bub_ms at = p;
    point_sm_ms l;
    submap *sm = map_meddler::unsafe_get_submap_at( at, l );
    REQUIRE( sm != nullptr );
    sm->last_touched = calendar::turn - elapsed;
    std::unique_ptr<tinymap> tm = std::make_unique<tinymap>();
    tm->load( project_to<coords::omt>( get_map().get_abs( p ) ), false );
}

} // namespace

// MAP-B09: a field's monster_spawn_count was taken off twice per monster, so a count of 2 made
// one, and what fields (and rotting food) spawned only queued on the submap until the next map
// shift: a player who stayed put met them all at once on their first step across a submap
// edge. The count is now whole, and the game places the queued monsters every turn.
TEST_CASE( "map-b09_a_field_spawns_its_whole_count_and_they_appear_at_once", "[audit][map]" )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    map &here = get_map();
    const tripoint_bub_ms spot( 30, 30, 0 );
    const on_out_of_scope cleanup( [&here, spot]() {
        here.remove_field( spot, field_test_fd_m06a_spawner );
        clear_creatures();
        clear_map();
    } );
    REQUIRE( here.add_field( spot, field_test_fd_m06a_spawner, 1 ) );
    // process_fields leaves a newborn field (age 0) alone for a turn
    here.set_field_age( spot, field_test_fd_m06a_spawner, 1_turns );
    REQUIRE( test_zombies_on_map() == 0 );

    here.process_fields();
    // queued on the submap, as before
    CHECK( test_zombies_on_map() == 0 );
    // the turn places them (do_turn calls this right after the fields and items are processed)
    here.spawn_queued_monsters();
    CHECK( test_zombies_on_map() == 2 );
    for( const monster &critter : g->all_monsters() ) {
        CHECK( rl_dist( critter.pos_bub(), spot ) <= 4 );
    }

    // nothing is left in the queue to come out later
    here.spawn_queued_monsters();
    CHECK( test_zombies_on_map() == 2 );
}

// MAP-B10: frozen water remembers the water under it, so it can thaw. Building or smashing
// furniture on it (or smashing a vehicle or a web there) forgot that, and the tile stayed ice
// for good. It also froze and thawed only when its submap was loaded; the game now goes over
// the reality bubble once a day as well.
TEST_CASE( "map-b10_furniture_on_frozen_water_leaves_it_free_to_thaw", "[audit][map]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    clear_avatar();
    clear_map();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms ice( 40, 40, 0 );
    const tripoint_bub_ms ice_under_avatar( 43, 40, 0 );
    const on_out_of_scope cleanup( [&here, ice, ice_under_avatar]() {
        for( const tripoint_bub_ms &p : {
                 ice, ice_under_avatar
             } ) {
            here.furn_set( p, furn_str_id::NULL_ID() );
            here.ter_set( p, ter_t_floor );
            here.clear_original_terrain_at( p );
        }
        clear_avatar();
        clear_map();
    } );
    // two tiles of deep water the phase rules froze over
    for( const tripoint_bub_ms &p : {
             ice, ice_under_avatar
         } ) {
        here.ter_set( p, ter_t_ice_dp_thin );
        here.set_original_terrain_at( p, ter_t_water_dp );
        REQUIRE( here.has_original_terrain_at( p ) );
    }

    REQUIRE( here.furn_set( ice, furn_f_chair ) );
    CHECK( here.has_original_terrain_at( ice ) );
    here.destroy_furn( ice, true );
    CHECK( here.ter( ice ) == ter_t_ice_dp_thin );
    CHECK( here.get_original_terrain_at( ice ) == ter_t_water_dp );

    // a summer day: the daily pass thaws the ice, but not under the avatar
    u.setpos( here, ice_under_avatar );
    calendar::turn = calendar::turn_zero + calendar::year_length() +
                     calendar::season_length() * 3 / 2 + 8_hours;
    here.refresh_phase_changes();
    CHECK( here.ter( ice ) == ter_t_water_dp );
    CHECK_FALSE( here.has_original_terrain_at( ice ) );
    CHECK( here.ter( ice_under_avatar ) == ter_t_ice_dp_thin );
    CHECK( here.has_original_terrain_at( ice_under_avatar ) );
}

// MAP-J06: EOCs could not read or write a tile's radiation, set a field's intensity exactly
// (u_set_field only added to it), or read a field's age.
TEST_CASE( "map-j06_math_reads_and_sets_fields_and_radiation", "[audit][map][math_parser]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms spot( 50, 50, 0 );
    const on_out_of_scope cleanup( [&here, spot]() {
        here.clear_fields( spot );
        here.set_radiation( spot, 0 );
        clear_map();
    } );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    d.set_value( "spot", here.get_abs( spot ) );

    SECTION( "field_strength sets the intensity, and 0 removes the field" ) {
        REQUIRE( here.add_field( spot, field_fd_smoke, 2 ) );
        CHECK( eval_math( d, "field_strength('fd_smoke', 'location': _spot)" ) == 2 );
        eval_math( d, "field_strength('fd_smoke', 'location': _spot) = 1" );
        CHECK( here.get_field_intensity( spot, field_fd_smoke ) == 1 );
        eval_math( d, "field_strength('fd_smoke', 'location': _spot) += 1" );
        CHECK( here.get_field_intensity( spot, field_fd_smoke ) == 2 );
        eval_math( d, "field_strength('fd_smoke', 'location': _spot) = 0" );
        CHECK_FALSE( here.has_field_at( spot, field_fd_smoke ) );
        CHECK( eval_math( d, "field_strength('fd_smoke', 'location': _spot)" ) == 0 );
        eval_math( d, "field_strength('fd_smoke', 'location': _spot) = 2" );
        CHECK( here.get_field_intensity( spot, field_fd_smoke ) == 2 );
    }

    SECTION( "field_age reads and sets the age" ) {
        REQUIRE( here.add_field( spot, field_fd_smoke, 1 ) );
        here.set_field_age( spot, field_fd_smoke, 5_turns );
        CHECK( eval_math( d, "field_age('fd_smoke', 'location': _spot)" ) == 5 );
        eval_math( d, "field_age('fd_smoke', 'location': _spot) = 20" );
        CHECK( here.get_field_age( spot, field_fd_smoke ) == 20_turns );
        CHECK( eval_math( d, "field_age('fd_fire', 'location': _spot)" ) == -1 );
    }

    SECTION( "map_radiation reads and sets the tile's radiation" ) {
        here.set_radiation( spot, 0 );
        eval_math( d, "map_radiation('location': _spot) = 30" );
        CHECK( here.get_radiation( spot ) == 30 );
        eval_math( d, "map_radiation('location': _spot) += 20" );
        CHECK( here.get_radiation( spot ) == 50 );
        CHECK( eval_math( d, "map_radiation('location': _spot)" ) == 50 );
        eval_math( d, "map_radiation('location': _spot) = -5" );
        CHECK( here.get_radiation( spot ) == 0 );
    }

    SECTION( "u_set_field adds, sets or raises" ) {
        const auto set_field = [&d]( const std::string & mode, int intensity ) {
            load_effect( string_format(
                             R"({ "effect": [ { "u_set_field": "fd_smoke", "radius": 0, "intensity": %d, "mode": "%s", "target_var": { "context_val": "spot" } } ] })",
                             intensity, mode ) ).apply( d );
        };
        REQUIRE( here.add_field( spot, field_fd_smoke, 2 ) );
        set_field( "set", 1 );
        CHECK( here.get_field_intensity( spot, field_fd_smoke ) == 1 );
        set_field( "add", 1 );
        CHECK( here.get_field_intensity( spot, field_fd_smoke ) == 2 );
        set_field( "max", 1 );
        CHECK( here.get_field_intensity( spot, field_fd_smoke ) == 2 );
        set_field( "max", 3 );
        CHECK( here.get_field_intensity( spot, field_fd_smoke ) == 3 );
        set_field( "set", 0 );
        CHECK_FALSE( here.has_field_at( spot, field_fd_smoke ) );
        // nothing to raise or set to 0 adds nothing
        set_field( "set", 0 );
        CHECK_FALSE( here.has_field_at( spot, field_fd_smoke ) );
        CHECK_FALSE( here.has_field_at( spot, field_fd_fire ) );
    }
}

// MAP-J04: the time a tile spends away from the player only made sap on t_tree_maple_tapped,
// scorched a fixed list of plants, and only t_rootcellar kept items cool. The terrain's
// produces_liquid and rad_scorch, and an item_temperature on terrain or furniture, do that now.
TEST_CASE( "map-j04_terrain_makes_liquid_and_scorches_as_its_json_says", "[audit][map]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    restore_on_out_of_scope restore_turn( calendar::turn );
    const time_point start = calendar::turn;
    const tripoint_bub_ms spot( 60, 60, 0 );
    std::vector<submap *> submaps = submaps_of_omt( spot );
    const on_out_of_scope cleanup( [&submaps, &here, spot, start]() {
        for( submap *sm : submaps ) {
            sm->last_touched = start;
        }
        here.set_radiation( spot, 0 );
        clear_map();
    } );

    SECTION( "a year away fills a container with one whole span's worth" ) {
        here.ter_set( spot, ter_t_test_m06a_spring );
        here.add_item( spot, item( itype_bucket ) );
        calendar::turn = start + 2 * calendar::year_length();
        catch_up( spot, calendar::year_length() + 10_days );
        CHECK( liquid_on( spot, itype_water_clean ) == 8 );
    }

    SECTION( "the tapped maple makes its sap at the rate it always did" ) {
        here.ter_set( spot, ter_t_tree_maple_tapped );
        here.add_item( spot, item( itype_bucket ) );
        // ten days into spring is in the sap season
        time_point when = calendar::turn_zero + 10_days;
        while( when < start + 1_days ) {
            when += calendar::year_length();
        }
        calendar::turn = when;
        catch_up( spot, 1_days );
        // 224 charges over the last quarter of winter and the first half of spring
        const time_duration per_charge = 0.75 * calendar::season_length() / 224;
        const double expected = to_turns<double>( 1_days ) / to_turns<double>( per_charge );
        const int sap = liquid_on( spot, itype_maple_sap );
        CHECK( sap >= static_cast<int>( std::floor( expected ) ) );
        CHECK( sap <= static_cast<int>( std::ceil( expected ) ) );
    }

    SECTION( "radiation scorches terrain into its rad_scorch" ) {
        // plant, and what a day of strong radiation leaves of it
        const std::vector<std::pair<ter_str_id, ter_str_id>> plants = {
            { ter_t_test_m06a_tree, ter_t_grass },
            // its own id: immune
            { ter_t_test_m06a_hardy_shrub, ter_t_test_m06a_hardy_shrub },
            // the built-in rules are unchanged
            { ter_t_tree_pine, ter_t_tree_deadpine },
        };
        int days = 0;
        for( const std::pair<ter_str_id, ter_str_id> &plant : plants ) {
            CAPTURE( plant.first.str() );
            here.ter_set( spot, plant.first );
            here.set_radiation( spot, 100 );
            days += 2;
            calendar::turn = start + days * 1_days;
            catch_up( spot, 1_days );
            CHECK( here.ter( spot ) == plant.second );
        }
    }

    SECTION( "terrain and furniture keep items at their item_temperature" ) {
        CHECK( ( ter_t_rootcellar->item_temperature == temperature_flag::ROOT_CELLAR ) );
        CHECK( ( furn_f_test_m06a_cold_box->item_temperature == temperature_flag::FREEZER ) );
        CHECK_FALSE( ter_t_dirt->item_temperature.has_value() );
    }
}

// MAP-J05: every felled tree left a pine trunk and stump.
TEST_CASE( "map-j05_a_felled_tree_leaves_its_own_trunk_and_stump", "[audit][map]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    const on_out_of_scope cleanup( []() {
        clear_map();
    } );
    const tripoint_bub_ms tree( 30, 80, 0 );
    const tripoint_bub_ms pine( 30, 90, 0 );
    here.ter_set( tree, ter_t_test_m06a_tree );
    here.ter_set( pine, ter_t_tree_pine );

    here.cut_down_tree( tree, point_rel_ms::east );
    CHECK( here.ter( tree ) == ter_t_dirt );
    int trunk_tiles = 0;
    for( const tripoint_bub_ms &p : here.points_in_radius( tree, 5 ) ) {
        trunk_tiles += here.ter( p ) == ter_t_dirtmound ? 1 : 0;
    }
    CHECK( trunk_tiles > 0 );

    // a tree without "felled" leaves a stump as before
    here.cut_down_tree( pine, point_rel_ms::east );
    CHECK( here.ter( pine ) == ter_t_stump );
}

// MAP-J03: timed appliance cycles (the washing machine's) were C++ only. A CARGO part's
// "processing_cycle" now runs one from JSON: a start EOC that may refuse, the flags and item
// changes when it ends, and an EOC when it does.
TEST_CASE( "map-j03_a_json_processing_cycle_runs_and_finishes", "[audit][map][vehicle]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    const on_out_of_scope cleanup( []() {
        get_globals().clear_global_values();
        clear_vehicles();
        clear_map();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms( 1, 0, 0 );
    REQUIRE( place_appliance( here, spot, vpart_ap_test_m06a_dryer, u ) );
    const optional_vpart_position ovp = here.veh_at( spot );
    REQUIRE( ovp.has_value() );
    vehicle &veh = ovp->vehicle();
    const std::optional<vpart_reference> cargo = ovp->cargo();
    REQUIRE( cargo.has_value() );
    const int idx = cargo->part_index();
    REQUIRE( veh.part( idx ).info().processing_cycle.has_value() );
    CHECK_FALSE( processing_cycles::running( veh.part( idx ) ) );

    // an empty dryer doesn't start
    processing_cycles::toggle( veh, idx );
    CHECK_FALSE( processing_cycles::running( veh.part( idx ) ) );

    item shirt( itype_tshirt );
    shirt.set_flag( flag_FILTHY );
    REQUIRE( veh.add_item( here, veh.part( idx ), shirt ).has_value() );
    REQUIRE( veh.add_item( here, veh.part( idx ), item( itype_sheet_cotton ) ).has_value() );

    // its start EOC refuses until the global allows it
    processing_cycles::toggle( veh, idx );
    CHECK_FALSE( processing_cycles::running( veh.part( idx ) ) );
    CHECK( get_globals().maybe_get_global_value( "test_m06a_dryer_started" ) == nullptr );
    get_globals().set_global_value( "test_m06a_dryer_allowed", 1.0 );
    processing_cycles::toggle( veh, idx );
    CHECK( processing_cycles::running( veh.part( idx ) ) );
    REQUIRE( get_globals().maybe_get_global_value( "test_m06a_dryer_started" ) != nullptr );
    // what it holds is shut in while it runs
    CHECK( veh.part( idx ).is_cleaner_on() );

    calendar::turn += 5_minutes;
    processing_cycles::process( veh, idx );
    CHECK( processing_cycles::running( veh.part( idx ) ) );
    CHECK( get_globals().maybe_get_global_value( "test_m06a_dryer_done" ) == nullptr );

    calendar::turn += 5_minutes;
    processing_cycles::process( veh, idx );
    CHECK_FALSE( processing_cycles::running( veh.part( idx ) ) );
    CHECK_FALSE( veh.part( idx ).is_cleaner_on() );
    int shirts = 0;
    int patches = 0;
    for( const item &it : veh.get_items( veh.part( idx ) ) ) {
        if( it.typeId() == itype_tshirt ) {
            shirts++;
            CHECK_FALSE( it.has_flag( flag_FILTHY ) );
        }
        patches += it.typeId() == itype_felt_patch ? 1 : 0;
        CHECK( it.typeId() != itype_sheet_cotton );
    }
    CHECK( shirts == 1 );
    CHECK( patches == 1 );
    REQUIRE( get_globals().maybe_get_global_value( "test_m06a_dryer_done" ) != nullptr );
    const diag_value *part = get_globals().maybe_get_global_value( "test_m06a_dryer_part" );
    REQUIRE( part != nullptr );
    CHECK( part->str() == vpart_ap_test_m06a_dryer.str() );
}

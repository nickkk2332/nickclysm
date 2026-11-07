#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "itype.h"
#include "level_cache.h"
#include "lightmap.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "mdarray.h"
#include "monster.h"
#include "mtype.h"
#include "options_helpers.h"
#include "overmapbuffer.h"
#include "pathfinding.h"
#include "player_helpers.h"
#include "point.h"
#include "scent_map.h"
#include "simple_pathfinding.h"
#include "sounds.h"
#include "talker.h"
#include "teleport.h"
#include "type_id.h"
#include "weather.h"
#include "weather_type.h"

static const efftype_id effect_audit_m07a_teleport_lock( "audit_m07a_teleport_lock" );
static const efftype_id effect_sleep( "sleep" );

static const furn_str_id furn_f_barricade_road( "f_barricade_road" );
static const furn_str_id furn_f_wreckage( "f_wreckage" );
static const furn_str_id furn_test_f_audit_m07a_cat_flap( "test_f_audit_m07a_cat_flap" );

static const itype_id itype_audit_m07a_red_lamp( "audit_m07a_red_lamp" );

static const json_character_flag json_flag_DIMENSIONAL_ANCHOR( "DIMENSIONAL_ANCHOR" );

static const mtype_id mon_audit_m07a_anchored( "mon_audit_m07a_anchored" );
static const mtype_id mon_audit_m07a_glow( "mon_audit_m07a_glow" );
static const mtype_id mon_audit_m07a_large( "mon_audit_m07a_large" );
static const mtype_id mon_audit_m07a_path( "mon_audit_m07a_path" );
static const mtype_id mon_test_base( "mon_test_base" );
static const mtype_id mon_test_zombie( "mon_test_zombie" );

static const ter_str_id ter_t_flat_roof( "t_flat_roof" );
static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_window_open( "t_window_open" );

static const trait_id trait_TEST_AUDIT_M07A_ANCHOR( "TEST_AUDIT_M07A_ANCHOR" );

namespace
{

const std::function<void( size_t, size_t )> no_progress = []( size_t, size_t ) {};

double eval_math( dialogue &d, const std::string_view expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

// A global set by the test EOCs, which must have run
const diag_value &global_value( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    REQUIRE( value != nullptr );
    return *value;
}

// The test EOCs record only while this lives
struct eoc_listener {
    eoc_listener() {
        get_globals().clear_global_values();
        get_globals().set_global_value( "audit_m07a_listen", 1 );
    }
    ~eoc_listener() {
        get_globals().clear_global_values();
    }
};

// A wall across x = wall_x from y = 50 to y = 70, whose one gap at y = 60 holds the given
// terrain and furniture
tripoint_bub_ms wall_with_gap( int wall_x, const ter_str_id &gap_ter, const furn_str_id &gap_furn )
{
    map &here = get_map();
    for( int y = 50; y <= 70; ++y ) {
        here.ter_set( tripoint_bub_ms( wall_x, y, 0 ), ter_t_wall );
    }
    const tripoint_bub_ms gap( wall_x, 60, 0 );
    here.ter_set( gap, gap_ter );
    here.furn_set( gap, gap_furn );
    return gap;
}

// Whether a route from 3 tiles west of the gap to 3 tiles east of it, for a creature of this
// size, goes through the gap; the route must exist, around the wall if not through the gap
bool routes_through( const tripoint_bub_ms &gap, creature_size size )
{
    pathfinding_settings settings = mon_audit_m07a_large->path_settings;
    settings.size = size;
    const std::vector<tripoint_bub_ms> path = get_map().route( gap + tripoint_rel_ms( -3, 0, 0 ),
            pathfinding_target::point( gap + tripoint_rel_ms( 3, 0, 0 ) ), settings,
    []( const tripoint_bub_ms & ) {
        return false;
    } );
    REQUIRE_FALSE( path.empty() );
    return std::find( path.begin(), path.end(), gap ) != path.end();
}

void rebuild_lightmap( int zlev )
{
    map &here = get_map();
    for( int z = -2; z <= OVERMAP_HEIGHT; z++ ) {
        here.invalidate_map_cache( z );
    }
    here.invalidate_visibility_cache();
    here.build_map_cache( zlev );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( zlev );
    here.build_map_cache( zlev );
}

void place_floor_roofed( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    here.ter_set( pos, ter_t_floor );
    here.ter_set( pos + tripoint::above, ter_t_flat_roof );
}

light_color_rgb light_color_at( const tripoint_bub_ms &pos )
{
    return get_map().access_cache( pos.z() ).light_color_cache[pos.x()][pos.y()];
}

} // namespace

// SPAT-B11: the overmap search estimated every OMT left at 24, though a car pays 3 on a
// highway, so it ran greedily across the fields. It now takes the cheapest cost of the
// travel profile.
TEST_CASE( "spat-b11_overmap_path_estimate_uses_the_cheapest_cost",
           "[audit][pathfinding][overmap]" )
{
    CHECK( overmap_path_params::for_player().min_cost() == 24 );
    CHECK( overmap_path_params::for_npc().min_cost() == 24 );
    CHECK( overmap_path_params::for_land_vehicle( 1.0f, false, false ).min_cost() == 3 );
    CHECK( overmap_path_params::for_watercraft().min_cost() == 8 );
    CHECK( overmap_path_params::for_aircraft().min_cost() == 8 );
    // direct travel: log2 of 24 is 4.58, kept as 4
    CHECK( overmap_path_params::flatten_pathfinding_costs(
               overmap_path_params::for_player() ).min_cost() == 4 );

    // Fields (12) on row 0, from start to finish, and a road (3) on row 1 beside them
    using Point = tripoint_abs_omt;
    const Point start( 0, 0, 0 );
    const Point finish( 10, 0, 0 );
    const pf::omt_scoring_fn score = []( Point cur ) {
        if( cur.z() != 0 || cur.x() < 0 || cur.x() > 10 || cur.y() < 0 || cur.y() > 1 ) {
            return pf::omt_score::rejected;
        }
        return pf::omt_score( cur.y() == 1 ? 3 : 12 );
    };
    const pf::simple_path<Point> pth = pf::find_overmap_path( start, finish, 20, score, no_progress,
                                       std::nullopt, false, 3 );
    // half the start field, the road, half the finish field: 6 + 1 + 2 + 3 * 8 + 2 + 2 + 6
    CHECK( pth.cost == 43 );
    REQUIRE( pth.points.size() == 13 );
    CHECK( pth.points.front() == finish );
    CHECK( pth.points.back() == start );
    for( std::size_t i = 1; i + 1 < pth.points.size(); ++i ) {
        CAPTURE( i );
        CHECK( pth.points[i].y() == 1 );
    }
}

// SPAT-B11: a node found again at a lower cost had only its cost lowered, not queued again,
// so what came after it kept the old cost: this path was reported as costing 71.
TEST_CASE( "spat-b11_overmap_path_requeues_a_cheaper_node", "[audit][pathfinding][overmap]" )
{
    using Point = tripoint_abs_omt;
    // Costs by row (y) and column (x); -1 can't be crossed
    static const std::array<std::array<int, 5>, 3> costs = { {
            { { 2, 10, 2, 40, 40 } },
            { { 3, 3, 40, 3, 3 } },
            { { 3, -1, 3, 40, 10 } }
        }
    };
    const pf::omt_scoring_fn score = []( Point cur ) {
        if( cur.z() != 0 || cur.x() < 0 || cur.x() > 4 || cur.y() < 0 || cur.y() > 2 ) {
            return pf::omt_score::rejected;
        }
        const int cost = costs[cur.y()][cur.x()];
        return cost < 0 ? pf::omt_score::rejected : pf::omt_score( cost );
    };
    const Point start( 0, 0, 0 );
    const Point finish( 4, 0, 0 );
    const pf::simple_path<Point> pth = pf::find_overmap_path( start, finish, 10, score, no_progress,
                                       std::nullopt, false, 2 );
    // 1 + 10 + 2 + 28 (turning south in the 40) + 2 + 2 + 20
    CHECK( pth.cost == 65 );
    const std::vector<Point> expected = {
        Point( 4, 0, 0 ), Point( 4, 1, 0 ), Point( 3, 1, 0 ), Point( 3, 0, 0 ), Point( 2, 0, 0 ),
        Point( 1, 0, 0 ), Point( 0, 0, 0 )
    };
    CHECK( pth.points == expected );
}

// SPAT-J02: path_settings ignored the documented allow_unlock_doors and avoid_rough_terrain,
// climb_cost couldn't be set, and a monster's path size was never set, so the pathfinder let
// big monsters plan through gaps they can't fit.
TEST_CASE( "spat-j02_monster_path_settings_load_every_field", "[audit][pathfinding][monster]" )
{
    const pathfinding_settings &set = mon_audit_m07a_path->path_settings;
    CHECK( set.max_dist == 10 );
    CHECK( set.max_length == 50 );
    CHECK( set.allow_unlock_doors );
    CHECK( set.avoid_rough_terrain );
    // CLIMBS alone would make it 3
    CHECK( set.climb_cost == 7 );
    REQUIRE( set.size.has_value() );
    CHECK( *set.size == creature_size::huge );
    // The monster itself is 1 ml: tiny
    CHECK( mon_audit_m07a_path->size == creature_size::tiny );

    // Unset, the size is the monster's own, and nothing new is allowed
    const mtype &zed = *mon_test_zombie;
    REQUIRE( zed.path_settings.size.has_value() );
    CHECK( *zed.path_settings.size == zed.size );
    CHECK_FALSE( zed.path_settings.allow_unlock_doors );
    CHECK_FALSE( zed.path_settings.avoid_rough_terrain );
}

TEST_CASE( "spat-j02_large_monster_routes_around_an_open_window", "[audit][pathfinding][monster]" )
{
    clear_map();
    const tripoint_bub_ms window = wall_with_gap( 60, ter_t_window_open, furn_str_id::NULL_ID() );
    REQUIRE( get_map().has_flag_ter( ter_furn_flag::TFLAG_SMALL_PASSAGE, window ) );
    REQUIRE( mon_audit_m07a_large->path_settings.size == creature_size::large );

    CHECK_FALSE( routes_through( window, creature_size::large ) );
    CHECK( routes_through( window, creature_size::medium ) );
    clear_map();
}

// SPAT-J03: the pathfinding cache took CLIMBABLE, SHARP and SMALL_PASSAGE from the terrain
// only, and nothing could keep a size off a tile but SMALL_PASSAGE (large and up).
TEST_CASE( "spat-j03_furniture_flags_reach_the_pathfinding_cache", "[audit][pathfinding]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms barricade( 50, 50, 0 );
    const tripoint_bub_ms wreck( 52, 50, 0 );
    const tripoint_bub_ms flap( 54, 50, 0 );
    here.furn_set( barricade, furn_f_barricade_road );
    here.furn_set( wreck, furn_f_wreckage );
    here.furn_set( flap, furn_test_f_audit_m07a_cat_flap );
    REQUIRE( here.has_flag_furn( ter_furn_flag::TFLAG_CLIMBABLE, barricade ) );
    REQUIRE_FALSE( here.has_flag_ter( ter_furn_flag::TFLAG_CLIMBABLE, barricade ) );
    REQUIRE( here.has_flag_furn( ter_furn_flag::TFLAG_SHARP, wreck ) );
    REQUIRE_FALSE( here.has_flag_ter( ter_furn_flag::TFLAG_SHARP, wreck ) );

    const pathfinding_cache &cache = here.get_pathfinding_cache_ref( 0 );
    CHECK( cache.special[barricade.xy()].is_set( PathfindingFlag::Climbable ) );
    CHECK( cache.special[wreck.xy()].is_set( PathfindingFlag::Sharp ) );

    // A cat flap: small and tiny creatures fit through, nothing bigger
    const PathfindingFlags flap_flags = cache.special[flap.xy()];
    CHECK_FALSE( flap_flags.is_set( PathfindingFlag::RestrictSmall ) );
    CHECK( flap_flags.is_set( PathfindingFlag::RestrictMedium ) );
    CHECK( flap_flags.is_set( PathfindingFlag::RestrictLarge ) );
    CHECK( flap_flags.is_set( PathfindingFlag::RestrictHuge ) );
    CHECK( here.fits_creature_size( flap, creature_size::tiny ) );
    CHECK( here.fits_creature_size( flap, creature_size::small ) );
    CHECK_FALSE( here.fits_creature_size( flap, creature_size::medium ) );
    CHECK_FALSE( here.fits_creature_size( flap, creature_size::huge ) );
    // SMALL_PASSAGE still stands for "medium and smaller"
    REQUIRE( ter_t_window_open->has_flag( ter_furn_flag::TFLAG_SMALL_PASSAGE ) );
    REQUIRE( ter_t_window_open->max_fitting_size() == creature_size::medium );
    clear_map();
}

TEST_CASE( "spat-j03_only_creatures_that_fit_use_a_cat_flap", "[audit][pathfinding][monster]" )
{
    clear_map();
    clear_creatures();
    const tripoint_bub_ms flap = wall_with_gap( 70, ter_t_floor, furn_test_f_audit_m07a_cat_flap );

    // The pathfinder: its move cost is a floor's, so only the size keeps a creature out
    CHECK( routes_through( flap, creature_size::small ) );
    CHECK_FALSE( routes_through( flap, creature_size::medium ) );

    // Moving there, as for SMALL_PASSAGE
    monster &zed = spawn_test_monster( mon_test_zombie.str(), flap + tripoint_rel_ms::west );
    REQUIRE( zed.get_size() == creature_size::medium );
    CHECK_FALSE( zed.will_move_to( flap ) );
    monster &blob = spawn_test_monster( mon_test_base.str(), flap + tripoint_rel_ms::east );
    REQUIRE( blob.get_size() == creature_size::tiny );
    CHECK( blob.will_move_to( flap ) );
    clear_creatures();
    clear_map();
}

// SPAT-J04: only a Character's worn items and effects could anchor it; a trait, a bionic or
// a monster could not, nor could a TELEPORT_LOCK effect on a monster. And no event said a
// creature had teleported.
TEST_CASE( "spat-j04_anchors_hold_monsters_and_characters", "[audit][teleport]" )
{
    clear_map();
    clear_avatar();
    clear_creatures();
    clear_npcs();
    const eoc_listener listening;
    map &here = get_map();
    // both in the middle submap, so the map doesn't shift when the avatar lands
    const tripoint_bub_ms start( 61, 62, 0 );
    const tripoint_bub_ms target( 66, 62, 0 );

    SECTION( "a monster's TELEPORT_LOCK effect" ) {
        monster &zed = spawn_test_monster( mon_test_zombie.str(), start );
        zed.add_effect( effect_audit_m07a_teleport_lock, 1_hours );
        CHECK_FALSE( teleport::teleport_to_point( zed, target, true, false, false ) );
        CHECK( zed.pos_bub() == start );
        CHECK( get_globals().maybe_get_global_value( "audit_m07a_teleports" ) == nullptr );

        zed.remove_effect( effect_audit_m07a_teleport_lock );
        CHECK( teleport::teleport_to_point( zed, target, true, false, false ) );
        CHECK( zed.pos_bub() == target );
        CHECK( global_value( "audit_m07a_teleports" ).dbl() == 1 );
        CHECK( global_value( "audit_m07a_teleported_monster" ).dbl() == 1 );
        CHECK( global_value( "audit_m07a_teleport_from" ).tripoint() == here.get_abs( start ) );
        CHECK( global_value( "audit_m07a_teleport_to" ).tripoint() == here.get_abs( target ) );
    }

    SECTION( "the DIMENSIONAL_ANCHOR monster flag" ) {
        monster &anchored = spawn_test_monster( mon_audit_m07a_anchored.str(), start );
        CHECK_FALSE( teleport::teleport_to_point( anchored, target, true, false, false ) );
        CHECK( anchored.pos_bub() == start );
    }

    SECTION( "an anchored monster in the way" ) {
        avatar &u = get_avatar();
        u.setpos( here, start );
        monster &anchored = spawn_test_monster( mon_audit_m07a_anchored.str(), target );
        // not safe: anything else there would be telefragged
        CHECK_FALSE( teleport::teleport_to_point( u, target, false, false, false ) );
        CHECK( u.pos_bub() == start );
        CHECK( anchored.pos_bub() == target );
    }

    SECTION( "a DIMENSIONAL_ANCHOR trait" ) {
        avatar &u = get_avatar();
        u.setpos( here, start );
        u.set_mutation( trait_TEST_AUDIT_M07A_ANCHOR );
        REQUIRE( u.has_flag( json_flag_DIMENSIONAL_ANCHOR ) );
        CHECK_FALSE( teleport::teleport_to_point( u, target, true, false, false ) );
        CHECK( u.pos_bub() == start );

        u.unset_mutation( trait_TEST_AUDIT_M07A_ANCHOR );
        CHECK( teleport::teleport_to_point( u, target, true, false, false ) );
        CHECK( u.pos_bub() == target );
        CHECK( global_value( "audit_m07a_teleports" ).dbl() == 1 );
        CHECK( global_value( "audit_m07a_teleported_monster" ).dbl() == 0 );
    }
    clear_creatures();
    clear_avatar();
}

// SPAT-J05: hearing ran no JSON: no event said a character heard a sound, and a wake-up
// didn't say what woke the character.
TEST_CASE( "spat-j05_hearing_and_waking_send_events", "[audit][sound][event]" )
{
    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    calendar::turn = calendar::turn_zero + 1_days + 12_hours;
    const scoped_weather_override weather( WEATHER_CLEAR );
    clear_map();
    clear_avatar();
    clear_creatures();
    clear_npcs();
    const eoc_listener listening;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms at( 60, 60, 0 );
    u.setpos( here, at );
    sounds::reset_sounds();

    SECTION( "an alarm 10 tiles away" ) {
        const tripoint_bub_ms source = at + tripoint_rel_ms( 10, 0, 0 );
        sounds::sound( source, 60, sounds::sound_t::alarm, "a test alarm" );
        sounds::process_sound_markers( &u );
        CHECK( global_value( "audit_m07a_heard" ).dbl() == 1 );
        CHECK( global_value( "audit_m07a_heard_raw" ).dbl() == 60 );
        CHECK( global_value( "audit_m07a_heard_volume" ).dbl() ==
               60 - get_weather().weather_id->sound_attn - 10 );
        CHECK( global_value( "audit_m07a_heard_loc" ).tripoint() == here.get_abs( source ) );
    }

    SECTION( "a noise wakes the sleeper" ) {
        u.fall_asleep( 1_hours );
        REQUIRE( u.has_effect( effect_sleep ) );
        sounds::sound( at + tripoint_rel_ms( 2, 0, 0 ), 100, sounds::sound_t::alarm, "a test alarm" );
        sounds::process_sound_markers( &u );
        CHECK( global_value( "audit_m07a_wake_cause" ).str() == "noise" );
    }

    SECTION( "being hurt wakes the sleeper" ) {
        u.fall_asleep( 1_hours );
        REQUIRE( u.has_effect( effect_sleep ) );
        u.hurtall( 1, nullptr );
        CHECK( global_value( "audit_m07a_wake_cause" ).str() == "damage" );
    }

    SECTION( "the noise meter" ) {
        u.volume = 17;
        dialogue d( get_talker_for( u ), nullptr );
        CHECK( eval_math( d, "u_noise_level()" ) == 17 );
    }
    sounds::reset_sounds();
    clear_avatar();
}

// SPAT-J06: only terrain, furniture, fields and vehicle parts had a light colour; items and
// monsters lit everything white.
TEST_CASE( "spat-j06_items_and_monsters_light_in_their_colour", "[audit][light_color]" )
{
    REQUIRE( itype_audit_m07a_red_lamp->light_color.r == 1.0f );
    REQUIRE( itype_audit_m07a_red_lamp->light_color.b == 0.0f );
    REQUIRE( mon_audit_m07a_glow->light_color.b == 1.0f );
    REQUIRE( mon_audit_m07a_glow->light_color.r == 0.0f );

    const restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    clear_avatar();
    clear_creatures();
    clear_map_without_vision( -2, OVERMAP_HEIGHT );
    g->reset_light_level();
    calendar::turn = calendar::turn_zero;
    get_player_character().recalc_sight_limits();
    const scoped_weather_override weather( WEATHER_CLEAR );
    map &here = get_map();
    const tripoint_bub_ms you = get_player_character().pos_bub();

    SECTION( "a red lamp on the floor" ) {
        const tripoint_bub_ms lamp = you + tripoint_rel_ms( 4, 0, 0 );
        place_floor_roofed( lamp );
        here.add_item( lamp, item( itype_audit_m07a_red_lamp ) );
        rebuild_lightmap( 0 );
        const light_color_rgb color = light_color_at( lamp );
        CHECK( color.r > 0.0f );
        CHECK( color.g == 0.0f );
        CHECK( color.b == 0.0f );
    }

    SECTION( "a blue glowing monster" ) {
        const tripoint_bub_ms glow = you + tripoint_rel_ms( 0, 4, 0 );
        place_floor_roofed( glow );
        spawn_test_monster( mon_audit_m07a_glow.str(), glow );
        rebuild_lightmap( 0 );
        const light_color_rgb color = light_color_at( glow );
        CHECK( color.b > 0.0f );
        CHECK( color.r == 0.0f );
        CHECK( color.g == 0.0f );
    }
    clear_creatures();
    clear_map();
}

// SPAT-J07: no EOC could read or write scent.
TEST_CASE( "spat-j07_math_reads_and_sets_scent", "[audit][scent][math_parser]" )
{
    clear_map();
    clear_avatar();
    get_globals().clear_global_values();
    map &here = get_map();
    scent_map &scent = get_scent();
    scent.reset();
    const tripoint_bub_ms p( 65, 65, 0 );
    get_globals().set_global_value( "audit_m07a_scent_loc", here.get_abs( p ) );
    dialogue d( get_talker_for( get_avatar() ), nullptr );

    eval_math( d, "scent(audit_m07a_scent_loc) = 300" );
    CHECK( scent.get( p ) == 300 );
    CHECK( eval_math( d, "scent(audit_m07a_scent_loc)" ) == 300 );
    eval_math( d, "scent(audit_m07a_scent_loc) += 50" );
    CHECK( scent.get( p ) == 350 );
    // never below 0, nor above 10000
    eval_math( d, "scent(audit_m07a_scent_loc) -= 1000" );
    CHECK( scent.get( p ) == 0 );
    eval_math( d, "scent(audit_m07a_scent_loc) = 99999" );
    CHECK( scent.get( p ) == 10000 );

    scent.reset();
    get_globals().clear_global_values();
}

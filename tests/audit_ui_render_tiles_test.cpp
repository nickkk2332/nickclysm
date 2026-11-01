#if defined(TILES)

#include <memory>
#include <optional>
#include <ostream>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "cata_utility.h"
#include "coordinates.h"
#include "debug.h"
#include "filesystem.h"
#include "game.h"
#include "map.h"
#include "map_helpers_tests.h"
#include "map_memory.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "player_helpers.h"
#include "point.h"
#include "sdl_renderer_recovery.h"
#include "type_id.h"

static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_wall( "t_wall" );

// REND-B01: in non-isometric tiles the north edge of the prepared map-memory region
// was min( max_mm_reg.y, screen top ), so it started at the top row of the screen, not
// of the reality bubble. Seen tiles north of the screen were memorized into the shared
// out-of-region submap and lost.
TEST_CASE( "rend-b01_ortho_tiles_memorize_seen_terrain_north_of_the_screen",
           "[audit][tiles][map_memory]" )
{
    software_render_fixture fx;
    if( !fx.available() ) {
        WARN( "dummy SDL video backend unavailable; skipping" );
        return;
    }
    // A 5x5 tile viewport centred on the avatar in the middle of the bubble; the point
    // 20 tiles north is in the bubble, in plain view, and north of the screen.
    constexpr int view_tiles = 5;
    const tripoint_bub_ms player_pos( 65, 65, 0 );
    const tripoint_bub_ms north_point( 65, 45, 0 );
    restore_on_out_of_scope restore_posx( POSX );
    restore_on_out_of_scope restore_posy( POSY );
    POSX = view_tiles / 2;
    POSY = view_tiles / 2;

    clear_avatar();
    g->place_player( player_pos );
    build_test_map( ter_t_grass.id() );
    set_time_to_day();
    map &here = get_map();
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0, true );
    // set_time_to_day refreshes the visibility cache before the map cache is built
    here.invalidate_visibility_cache();
    here.update_visibility_cache( 0 );

    avatar &you = get_avatar();
    // Start with no memory region, so the draw prepares exactly the region it asks for.
    you.clear_map_memory();
    on_out_of_scope clear_memory( [&you]() {
        you.clear_map_memory();
    } );

    const std::shared_ptr<const tileset> ts =
        renderer_recovery_test_support::install_synthetic_bundle_with_highlight(
            "audit_rend_b01_ts", "color_pixel_darken", 1, 1 );
    REQUIRE( ts );
    const std::unique_ptr<cata_tiles> tiles =
        renderer_recovery_test_support::make_test_tiles( ts );
    REQUIRE_FALSE( tiles->is_isometric() );
    const auto draw = [&tiles, &player_pos]() {
        renderer_recovery_test_support::draw_test_map( *tiles, player_pos, view_tiles,
                view_tiles );
    };

    draw();
    here.ter_set( north_point, ter_t_wall.id() );
    draw();

    // Read the memory back through a region that holds north_point, not through the
    // shared out-of-region submap.
    you.prepare_map_memory_region( here.get_abs( north_point ), here.get_abs( player_pos ) );
    CHECK( you.get_memorized_tile( here.get_abs( north_point ) ).get_ter_id() ==
           ter_t_wall.str() );
}

// REND-B02: the tileset loader erased tiles without valid sprites from tile_ids, but
// left pointers to them in tile_ids_by_season, which every draw looks tiles up in.
TEST_CASE( "rend-b02_tileset_load_drops_season_lookups_of_invalid_tiles", "[audit][tiles]" )
{
    software_render_fixture fx;
    if( !fx.available() ) {
        WARN( "dummy SDL video backend unavailable; skipping" );
        return;
    }
    const std::string tileset_id = "audit_rend_b02_ts";
    const cata_path dir = PATH_INFO::user_dir_path() / "audit_rend_b02_tileset";
    const cata_path conf = dir / PATH_INFO::tileset_conf();
    const cata_path json = dir / "tile_config.json";
    const cata_path png = dir / "tiles.png";
    const auto cleanup = [&]() {
        TILESETS.erase( tileset_id );
        remove_file( conf );
        remove_file( json );
        remove_file( png );
        remove_directory( dir.get_unrelative_path() );
    };
    on_out_of_scope restore( cleanup );
    cleanup();

    REQUIRE( assure_dir_exist( dir ) );
    // 16x16 atlas of sixteen 4x4 sprites
    REQUIRE( copy_file( cata_path( cata_path::root_path::unknown,
                                   "tests/data/opaque_bounds_atlas.png" ), png ) );
    write_to_file( conf, []( std::ostream & os ) {
        os << "NAME: audit_rend_b02_ts\nJSON: tile_config.json\nTILESET: tiles.png\n";
    } );
    // Sprite 99 does not exist, so audit_bad and audit_mixed_season_winter are invalid.
    write_to_file( json, []( std::ostream & os ) {
        os << R"({"tile_info":[{"width":4,"height":4}],"tiles-new":[{"file":"tiles.png",)"
           << R"("tiles":[{"id":"unknown","fg":0},{"id":"audit_good","fg":1},)"
           << R"({"id":"audit_bad","fg":99},{"id":"audit_mixed","fg":2},)"
           << R"({"id":"audit_mixed_season_winter","fg":99}]}]})";
    } );
    TILESETS[tileset_id] = dir;

    const bool error_seen_before = debug_has_error_been_observed();
    const std::shared_ptr<const tileset> ts = renderer_recovery_test_support::fetch_cached_bundle(
                tileset_id, "color_pixel_darken", renderer_coordinator.instance_generation(),
                renderer_coordinator.textures_generation() );
    REQUIRE( ts );
    // The loader reports the invalid tiles as D_ERROR. They are expected here, so do not
    // let them fail the whole test run.
    CHECK( debug_has_error_been_observed() );
    if( !error_seen_before ) {
        debug_reset_error_observed();
    }
    REQUIRE( ts->get_tileset_id() == tileset_id );
    REQUIRE( ts->find_tile_type( "audit_good" ) != nullptr );
    REQUIRE( ts->find_tile_type( "audit_mixed" ) != nullptr );
    REQUIRE( ts->find_tile_type( "audit_bad" ) == nullptr );
    REQUIRE( ts->find_tile_type( "audit_mixed_season_winter" ) == nullptr );

    for( int s = 0; s < NUM_SEASONS; ++s ) {
        const season_type season = static_cast<season_type>( s );
        CAPTURE( s );
        CHECK( ts->find_tile_type_by_season( "audit_good", season ).has_value() );
        // The season lookup agrees with find_tile_type: the invalid tile is gone.
        CHECK_FALSE( ts->find_tile_type_by_season( "audit_bad", season ).has_value() );
        // An invalid seasonal variant falls back to the valid base tile.
        std::optional<tile_lookup_res> mixed =
            ts->find_tile_type_by_season( "audit_mixed", season );
        REQUIRE( mixed.has_value() );
        CHECK( mixed->id() == "audit_mixed" );
    }
}

#endif // TILES

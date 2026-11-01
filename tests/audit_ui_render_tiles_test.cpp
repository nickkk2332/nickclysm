#if defined(TILES)

#include <memory>
#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "coordinates.h"
#include "game.h"
#include "map.h"
#include "map_helpers_tests.h"
#include "map_memory.h"
#include "output.h"
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

#endif // TILES

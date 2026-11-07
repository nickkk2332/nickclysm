#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cached_options.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "options.h"
#include "options_helpers.h"
#include "tile_lookup_memo.h"

#if defined(TILES)
#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include "atlas_bake_plan.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_tiles.h"
#include "coordinates.h"
#include "enums.h"
#include "game.h"
#include "level_cache.h"
#include "lightmap.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mdarray.h"
#include "output.h"
#include "player_helpers.h"
#include "point.h"
#include "sdl_renderer_recovery.h"
#include "trap.h"
#include "type_id.h"
#include "weighted_list.h"

static const field_type_str_id field_fd_blood( "fd_blood" );

static const furn_str_id furn_f_chair( "f_chair" );

static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_wall( "t_wall" );

static const trap_str_id tr_bubblewrap( "tr_bubblewrap" );
#endif

// REND-P02, 2.0: the sprite lookup memo and the multitile links are an experimental option,
// off by default, in the tileset options of the graphics page.
TEST_CASE( "rend-p02_sprite_lookup_cache_option_is_experimental_and_off_by_default",
           "[audit][tiles]" )
{
    options_manager::cOpt probe = get_options().get_option( "TILE_LOOKUP_CACHE" );
    CHECK( probe.getType() == "bool" );
    CHECK( probe.getPage() == "graphics" );
    CHECK( string_starts_with( probe.getMenuText(), "Experimental" ) );
    // the default is false: a copy set to false is at its default, and set to true is not
    probe.setValue( "false" );
    CHECK( probe.isDefault() );
    probe.setValue( "true" );
    CHECK_FALSE( probe.isDefault() );
#if !defined(TILES)
    // only the tiles build draws sprites
    CHECK( probe.is_hidden() );
#endif
}

// REND-P02, 2.0: the renderer reads the option from the options cache, which the options
// menu refreshes when it saves, so a change needs no restart.
TEST_CASE( "rend-p02_sprite_lookup_cache_option_sets_the_renderer_flag", "[audit][tiles]" )
{
    on_out_of_scope refresh_cache( []() {
        options_manager::update_options_cache();
    } );
    {
        override_option on( "TILE_LOOKUP_CACHE", "true" );
        options_manager::update_options_cache();
        CHECK( tile_lookup_cache );
    }
    override_option off( "TILE_LOOKUP_CACHE", "false" );
    options_manager::update_options_cache();
    CHECK_FALSE( tile_lookup_cache );
}

// REND-P02, 2.0: the renderer turns its memo on and off with the option, and every change
// forgets what the memo held, so nothing remembered before the cache was turned off is
// used after it is turned on again.
TEST_CASE( "rend-p02_tile_lookup_memo_forgets_when_switched", "[audit][tiles]" )
{
    using memo_type = tile_lookup_memo<std::optional<int>, 4, 3>;
    memo_type memo;
    const std::shared_ptr<int> tileset = std::make_shared<int>( 1 );
    const std::string wall = "t_wall";
    const std::string no_variant;
    const auto fill = [&]() {
        memo.validate( tileset, 7 );
        memo.remember( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant, 5 );
        memo.remember_by_int_id( 0, 1, 12, tile_lookup_suffix::none, 0, no_variant, 6 );
        REQUIRE( memo.size() == 2 );
    };

    // off until the renderer turns it on
    CHECK_FALSE( memo.is_enabled() );
    CHECK( memo.set_enabled( true ) );
    CHECK( memo.is_enabled() );
    fill();

    SECTION( "unchanged: kept" ) {
        CHECK( memo.set_enabled( true ) );
        CHECK( memo.size() == 2 );
        CHECK( memo.find( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant ) != nullptr );
        CHECK( memo.find_by_int_id( 0, 1, 12, tile_lookup_suffix::none, 0, no_variant ) != nullptr );
    }
    SECTION( "turned off: forgotten" ) {
        CHECK_FALSE( memo.set_enabled( false ) );
        CHECK_FALSE( memo.is_enabled() );
        CHECK( memo.size() == 0 );
        CHECK( memo.find( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
        CHECK( memo.find_by_int_id( 0, 1, 12, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
        // and staying off keeps it empty
        CHECK_FALSE( memo.set_enabled( false ) );
        CHECK( memo.size() == 0 );
    }
    SECTION( "turned off and on again: still forgotten" ) {
        memo.set_enabled( false );
        CHECK( memo.set_enabled( true ) );
        CHECK( memo.size() == 0 );
        CHECK( memo.find( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
        // and it fills again
        fill();
    }
}

// REND-P02, 2.0: terrain, furniture, traps and fields are remembered by int id, without
// hashing the id string.  The key is the same as by string id with the int id in its place,
// and the two kinds of answer are kept apart.
TEST_CASE( "rend-p02_tile_lookup_memo_int_id_tables", "[audit][tiles]" )
{
    using memo_type = tile_lookup_memo<std::optional<int>, 4, 3>;
    memo_type memo;
    memo.set_enabled( true );
    const std::shared_ptr<int> tileset = std::make_shared<int>( 1 );
    memo.validate( tileset, 7 );
    const std::string no_variant;
    const auto find = [&]( size_t season, size_t category, int int_id, tile_lookup_suffix suffix,
    int intensity, std::string_view variant ) {
        return memo.find_by_int_id( season, category, int_id, suffix, intensity, variant );
    };

    CHECK( find( 0, 1, 3, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( memo.remember_by_int_id( 0, 1, 3, tile_lookup_suffix::none, 0, no_variant, 5 ) == 5 );
    // a miss is remembered too
    memo.remember_by_int_id( 0, 1, 3, tile_lookup_suffix::transparent, 0, no_variant,
                             std::nullopt );
    memo.remember_by_int_id( 0, 1, 3, tile_lookup_suffix::intensity, 2, no_variant, 9 );
    memo.remember_by_int_id( 0, 1, 3, tile_lookup_suffix::none, 0, "rusty", 6 );
    // a far larger int id grows the table
    memo.remember_by_int_id( 2, 2, 500, tile_lookup_suffix::none, 0, no_variant, 7 );
    REQUIRE( memo.size() == 5 );

    const std::optional<int> *hit = find( 0, 1, 3, tile_lookup_suffix::none, 0, no_variant );
    REQUIRE( hit != nullptr );
    CHECK( *hit == 5 );
    const std::optional<int> *miss = find( 0, 1, 3, tile_lookup_suffix::transparent, 0, no_variant );
    REQUIRE( miss != nullptr );
    CHECK_FALSE( miss->has_value() );
    const std::optional<int> *intense = find( 0, 1, 3, tile_lookup_suffix::intensity, 2, no_variant );
    REQUIRE( intense != nullptr );
    CHECK( *intense == 9 );
    const std::optional<int> *rusty = find( 0, 1, 3, tile_lookup_suffix::none, 0, "rusty" );
    REQUIRE( rusty != nullptr );
    CHECK( *rusty == 6 );
    const std::optional<int> *far = find( 2, 2, 500, tile_lookup_suffix::none, 0, no_variant );
    REQUIRE( far != nullptr );
    CHECK( *far == 7 );

    // every part of the key counts
    CHECK( find( 1, 1, 3, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( find( 0, 2, 3, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( find( 0, 1, 4, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( find( 0, 1, 2, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( find( 0, 1, 3, tile_lookup_suffix::intensity, 3, no_variant ) == nullptr );
    CHECK( find( 0, 1, 3, tile_lookup_suffix::none, 0, "clean" ) == nullptr );
    CHECK( find( 0, 1, 499, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    // apart from the answers by string id, whatever the string
    CHECK( memo.find( 0, 1, "3", tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    memo.remember( 0, 1, "t_dirt", tile_lookup_suffix::none, 0, no_variant, 8 );
    CHECK( *find( 0, 1, 3, tile_lookup_suffix::none, 0, no_variant ) == 5 );

    // keys out of range are answered but never remembered
    CHECK( memo.remember_by_int_id( 0, 1, -1, tile_lookup_suffix::none, 0, no_variant, 1 ) == 1 );
    CHECK( find( 0, 1, -1, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( memo.remember_by_int_id( 0, 1, memo_type::int_id_limit, tile_lookup_suffix::none, 0,
                                    no_variant, 2 ) == 2 );
    CHECK( find( 0, 1, memo_type::int_id_limit, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( memo.remember_by_int_id( 4, 0, 3, tile_lookup_suffix::none, 0, no_variant, 3 ) == 3 );
    CHECK( find( 4, 0, 3, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( memo.size() == 6 );

    SECTION( "game data reloaded: int ids may mean other ids now, so forgotten" ) {
        memo.validate( tileset, 8 );
        CHECK( memo.size() == 0 );
        CHECK( find( 0, 1, 3, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
        CHECK( find( 2, 2, 500, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
        // still on
        CHECK( memo.is_enabled() );
    }
    SECTION( "another tileset: forgotten" ) {
        const std::shared_ptr<int> other = std::make_shared<int>( 2 );
        memo.validate( other, 7 );
        CHECK( memo.size() == 0 );
        CHECK( find( 0, 1, 3, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    }
}

#if defined(TILES)
namespace
{

const tripoint_bub_ms player_pos( 65, 65, 0 );
constexpr int view_tiles = 9;
// the season suffixes, by season_type
const std::array<std::string, NUM_SEASONS> season_suffixes = { {
        "_season_spring", "_season_summer", "_season_autumn", "_season_winter"
    }
};

tile_type one_sprite_tile()
{
    tile_type tile;
    tile.fg.add( std::vector<int> { 0 }, 1 );
    return tile;
}

// grass, a chair, bubble wrap, blood with an intensity 1 sprite, and a wall drawn through
// subtiles, whose edge piece has a sprite for every season
std::vector<std::pair<std::string, tile_type>> cache_test_tiles()
{
    std::vector<std::pair<std::string, tile_type>> tiles;
    for( const std::string &id : {
             ter_t_grass.str(), furn_f_chair.str(), tr_bubblewrap.str(), field_fd_blood.str(),
             field_fd_blood.str() + "_int1"
         } ) {
        tiles.emplace_back( id, one_sprite_tile() );
    }
    tile_type wall = one_sprite_tile();
    wall.multitile = true;
    wall.rotates = true;
    for( const std::string key : {
             "center", "corner", "edge", "t_connection", "end_piece", "unconnected"
         } ) {
        wall.available_subtiles.push_back( key );
        tile_type subtile = one_sprite_tile();
        subtile.rotates = true;
        tiles.emplace_back( ter_t_wall.str() + "_" + key, subtile );
    }
    for( const std::string &suffix : season_suffixes ) {
        tile_type seasonal = one_sprite_tile();
        seasonal.rotates = true;
        tiles.emplace_back( ter_t_wall.str() + "_edge" + suffix, seasonal );
    }
    tiles.emplace_back( ter_t_wall.str(), wall );
    return tiles;
}

bool same_draws( const std::vector<drawn_sprite_record> &a,
                 const std::vector<drawn_sprite_record> &b )
{
    if( a.size() != b.size() ) {
        return false;
    }
    for( size_t i = 0; i < a.size(); ++i ) {
        const drawn_sprite_record &x = a[i];
        const drawn_sprite_record &y = b[i];
        if( x.id != y.id || x.light != y.light || x.anchor != y.anchor || x.tile != y.tile ||
            x.p != y.p || x.offset != y.offset || x.rota != y.rota || x.retract != y.retract ||
            x.loc_rand != y.loc_rand ) {
            return false;
        }
    }
    return true;
}

} // namespace

// REND-P02, 2.0: with the cache on, the renderer draws exactly what it draws with the cache
// off (the lookups as before REND-P02): the same sprites of the same tiles, in the same
// order, at the same places, turned and retracted the same, both while it fills the memo
// and when it answers from it.  The wall draws through subtiles, picked from the links
// without drawing the subtile id over again, and its edge piece has a sprite for this
// season.  Terrain, furniture, the trap and the fields are looked up by int id.
TEST_CASE( "rend-p02_sprite_lookup_cache_draws_what_the_plain_lookups_draw", "[audit][tiles]" )
{
    software_render_fixture fx;
    if( !fx.available() ) {
        WARN( "dummy SDL video backend unavailable; skipping" );
        return;
    }
    on_out_of_scope cleanup( []() {
        clear_map();
        options_manager::update_options_cache();
    } );
    override_option cache_option( "TILE_LOOKUP_CACHE", "false" );
    options_manager::update_options_cache();
    // The draw puts the avatar's tile at (POSX, POSY) of the view.  Only the game's UI
    // layout sets them, and the tests skip it, so they are 0 here: the view would start at
    // the avatar and show only the map south-east of it.  Centre it, as the game does.
    restore_on_out_of_scope restore_posx( POSX );
    restore_on_out_of_scope restore_posy( POSY );
    POSX = view_tiles / 2;
    POSY = view_tiles / 2;

    clear_map();
    clear_avatar();
    g->place_player( player_pos );
    build_test_map( ter_t_grass.id() );
    set_time_to_day();
    map &here = get_map();
    // In the part of the map the view draws (from three tiles west and north of the avatar
    // to four east and south) and all in the avatar's sight: a wall from (1, -2) east to
    // (3, -2) and on south to (3, -1), so it ends twice, runs straight once and turns once;
    // a chair; bubble wrap; and blood of intensity 1, which has its own sprite, and of
    // intensity 2, which falls back to the base one.
    const tripoint_bub_ms wall_west_end = player_pos + tripoint( 1, -2, 0 );
    const tripoint_bub_ms wall_edge = player_pos + tripoint( 2, -2, 0 );
    const tripoint_bub_ms wall_corner = player_pos + tripoint( 3, -2, 0 );
    const tripoint_bub_ms wall_south_end = player_pos + tripoint( 3, -1, 0 );
    const tripoint_bub_ms chair_pos = player_pos + tripoint( -2, 2, 0 );
    const tripoint_bub_ms trap_pos = player_pos + tripoint( -2, 0, 0 );
    const tripoint_bub_ms blood_1_pos = player_pos + tripoint( 0, 3, 0 );
    const tripoint_bub_ms blood_2_pos = player_pos + tripoint( 2, 2, 0 );
    const std::array<tripoint_bub_ms, 4> wall_pos = { {
            wall_west_end, wall_edge, wall_corner, wall_south_end
        }
    };
    for( const tripoint_bub_ms &p : wall_pos ) {
        here.ter_set( p, ter_t_wall.id() );
    }
    here.furn_set( chair_pos, furn_f_chair.id() );
    here.trap_set( trap_pos, tr_bubblewrap.id() );
    here.add_field( blood_1_pos, field_fd_blood.id(), 1 );
    here.add_field( blood_2_pos, field_fd_blood.id(), 2 );
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0 );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( 0 );

    // what the draw needs to draw each piece: the avatar sees its tile clearly (else the
    // tile gets a vision effect, or its memory), the trap is one the avatar sees, and each
    // field has the intensity its sprite is for
    const std::vector<tripoint_bub_ms> scene = {
        wall_west_end, wall_edge, wall_corner, wall_south_end, chair_pos, trap_pos, blood_1_pos,
        blood_2_pos
    };
    const level_cache &level = here.access_cache( player_pos.z() );
    const visibility_variables &vision = here.get_visibility_variables_cache();
    for( const tripoint_bub_ms &p : scene ) {
        const lit_level ll = level.visibility_cache[p.x()][p.y()];
        CAPTURE( p - player_pos );
        CAPTURE( static_cast<int>( ll ) );
        REQUIRE( here.get_visibility( ll, vision ) == visibility_type::CLEAR );
    }
    REQUIRE( here.tr_at( trap_pos ).can_see( trap_pos, get_avatar() ) );
    REQUIRE( here.get_field_intensity( blood_1_pos, field_fd_blood.id() ) == 1 );
    REQUIRE( here.get_field_intensity( blood_2_pos, field_fd_blood.id() ) == 2 );

    const std::shared_ptr<const tileset> ts = renderer_recovery_test_support::install_tiles_bundle(
                "audit_m_fu_render_ts", "color_pixel_darken", 1, 1, atlas_bake_plan{}, true,
                cache_test_tiles() );
    REQUIRE( ts );
    // the tiles the scene draws, each found with its one sprite
    const auto sprite_tile = [&ts]( const std::string & id ) {
        CAPTURE( id );
        const tile_type *tile = ts->find_tile_type( id );
        REQUIRE( tile != nullptr );
        REQUIRE( tile->fg.size() == 1 );
        return tile;
    };
    const size_t season = static_cast<size_t>( season_of_year( calendar::turn ) );
    const tile_type *wall = sprite_tile( ter_t_wall.str() );
    const tile_type *center = sprite_tile( ter_t_wall.str() + "_center" );
    const tile_type *seasonal_edge = sprite_tile( ter_t_wall.str() + "_edge" +
                                     season_suffixes[season] );
    const tile_type *corner = sprite_tile( ter_t_wall.str() + "_corner" );
    const tile_type *end_piece = sprite_tile( ter_t_wall.str() + "_end_piece" );
    const tile_type *chair = sprite_tile( furn_f_chair.str() );
    const tile_type *bubblewrap = sprite_tile( tr_bubblewrap.str() );
    const tile_type *blood = sprite_tile( field_fd_blood.str() );
    const tile_type *blood_1 = sprite_tile( field_fd_blood.str() + "_int1" );

    // the links: center (subtile 0) is the same in every season, edge (subtile 2) is the
    // season's own sprite, open (subtile 6) is not one of the wall's subtiles
    REQUIRE( wall->subtiles_by_season.size() == multitile_key_count * NUM_SEASONS );
    constexpr size_t seasons = NUM_SEASONS;
    for( size_t s = 0; s < seasons; ++s ) {
        const std::pair<const std::string *, const tile_type *> &c = wall->subtiles_by_season[s];
        REQUIRE( c.first != nullptr );
        CHECK( *c.first == ter_t_wall.str() + "_center" );
        CHECK( c.second == center );
        const std::pair<const std::string *, const tile_type *> &e =
            wall->subtiles_by_season[2 * seasons + s];
        REQUIRE( e.first != nullptr );
        CHECK( *e.first == ter_t_wall.str() + "_edge" + season_suffixes[s] );
        CHECK( e.second == ts->find_tile_type( ter_t_wall.str() + "_edge" + season_suffixes[s] ) );
        const std::pair<const std::string *, const tile_type *> &o =
            wall->subtiles_by_season[6 * seasons + s];
        CHECK( o.first == nullptr );
        CHECK( o.second == nullptr );
    }

    std::unique_ptr<cata_tiles> tiles = renderer_recovery_test_support::make_test_tiles( ts );
    REQUIRE_FALSE( tiles->is_isometric() );
    std::vector<drawn_sprite_record> log;
    renderer_recovery_test_support::log_draw_light( *tiles, &log );
    on_out_of_scope stop_logging( [&tiles]() {
        renderer_recovery_test_support::log_draw_light( *tiles, nullptr );
    } );
    const auto draw = [&]() {
        log.clear();
        tiles->set_draw_cache_dirty();
        renderer_recovery_test_support::draw_test_map( *tiles, player_pos, view_tiles, view_tiles );
        return log;
    };
    const auto set_cache = []( const bool on ) {
        get_options().get_option( "TILE_LOOKUP_CACHE" ).setValue( on ? "true" : "false" );
        options_manager::update_options_cache();
    };
    // whether `drawn` has a sprite at map tile `p`, and one of `tile` there; where the draw
    // puts a map tile is known once it has drawn a frame
    const auto drew_anything_at = [&tiles]( const std::vector<drawn_sprite_record> &drawn,
    const tripoint_bub_ms & p ) {
        const point screen = tiles->player_to_screen( p.xy() );
        for( const drawn_sprite_record &r : drawn ) {
            if( r.p == screen ) {
                return true;
            }
        }
        return false;
    };
    const auto drew_at = [&tiles]( const std::vector<drawn_sprite_record> &drawn,
    const tripoint_bub_ms & p, const tile_type * tile ) {
        const point screen = tiles->player_to_screen( p.xy() );
        for( const drawn_sprite_record &r : drawn ) {
            if( r.p == screen && r.tile == tile ) {
                return true;
            }
        }
        return false;
    };

    // the first frame memorizes what the avatar sees; later frames draw the same
    draw();
    const std::vector<drawn_sprite_record> plain = draw();
    REQUIRE_FALSE( plain.empty() );
    CHECK_FALSE( renderer_recovery_test_support::lookup_memo_enabled( *tiles ) );
    CHECK( renderer_recovery_test_support::lookup_memo_size( *tiles ) == 0 );
    // every piece is on the screen: its terrain at least is drawn there
    for( const tripoint_bub_ms &p : scene ) {
        CAPTURE( p - player_pos );
        REQUIRE( drew_anything_at( plain, p ) );
    }
    // the scene draws the pieces this test is about
    CHECK( drew_at( plain, wall_edge, seasonal_edge ) );
    CHECK( drew_at( plain, wall_corner, corner ) );
    CHECK( drew_at( plain, wall_west_end, end_piece ) );
    CHECK( drew_at( plain, wall_south_end, end_piece ) );
    CHECK( drew_at( plain, chair_pos, chair ) );
    CHECK( drew_at( plain, trap_pos, bubblewrap ) );
    CHECK( drew_at( plain, blood_1_pos, blood_1 ) );
    CHECK( drew_at( plain, blood_2_pos, blood ) );

    set_cache( true );
    const std::vector<drawn_sprite_record> filling = draw();
    CHECK( renderer_recovery_test_support::lookup_memo_enabled( *tiles ) );
    CHECK( renderer_recovery_test_support::lookup_memo_size( *tiles ) > 0 );
    const std::vector<drawn_sprite_record> remembered = draw();
    CHECK( same_draws( plain, filling ) );
    CHECK( same_draws( plain, remembered ) );

    set_cache( false );
    const std::vector<drawn_sprite_record> plain_again = draw();
    CHECK_FALSE( renderer_recovery_test_support::lookup_memo_enabled( *tiles ) );
    CHECK( renderer_recovery_test_support::lookup_memo_size( *tiles ) == 0 );
    CHECK( same_draws( plain, plain_again ) );
}
#endif // TILES

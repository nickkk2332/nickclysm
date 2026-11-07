#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_attire.h"
#include "color.h"
#include "coordinates.h"
#include "crafting_gui_helpers.h"
#include "item.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "temp_crafting_inventory.h"
#include "tile_lookup_memo.h"
#include "type_id.h"
#include "weather_type.h"

#if defined(TILES)
#include <ostream>

#include "cata_path.h"
#include "cata_tiles.h"
#include "cata_utility.h"
#include "debug.h"
#include "filesystem.h"
#include "options.h"
#include "path_info.h"
#include "sdl_renderer_recovery.h"
#endif

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_debug_backpack( "debug_backpack" );
static const itype_id itype_family_cookbook( "family_cookbook" );
static const itype_id itype_fat( "fat" );
static const itype_id itype_knife_hunting( "knife_hunting" );

static const recipe_id recipe_cudgel_test_no_tools( "cudgel_test_no_tools" );
static const recipe_id recipe_cudgel_test_steps_required_prof( "cudgel_test_steps_required_prof" );
static const recipe_id recipe_test_tallow( "test_tallow" );

static const skill_id skill_cooking( "cooking" );
static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_melee( "melee" );

namespace
{

// The avatar in a clear, lit map, with a backpack to put things in.
avatar &setup_crafter()
{
    clear_map_without_vision();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    clear_avatar();
    avatar &u = get_avatar();
    u.worn.wear_item( u, item( itype_debug_backpack ), false, false );
    u.set_skill_level( skill_cooking, 3 );
    u.set_skill_level( skill_fabrication, 2 );
    u.set_skill_level( skill_melee, 1 );
    u.invalidate_crafting_inventory();
    return u;
}

// The panel the cache gives for this selection; it must be what a fresh build gives.
recipe_panel cached_and_checked( recipe_panel_cache &cache, const recipe &rec, int batch_size,
                                 const recipe_panel_context &ctx )
{
    const availability avail( ctx.crafter, &rec, batch_size, ctx.camp_crafting,
                              ctx.inventory_override );
    const recipe_panel &cached = cache.get( rec, avail, batch_size, ctx );
    CHECK( cached == build_recipe_panel( rec, avail, batch_size, ctx ) );
    return cached;
}

} // namespace

// UIINV-P01: the crafting menu redraws about 100 times a second, and its info panel reran
// the time, chance, lighting, book, helper and requirement checks on every redraw.  They are
// now built once per selection into a recipe_panel, which the menu keeps until something it
// shows can have changed.
TEST_CASE( "uiinv-p01_cached_info_panel_matches_a_fresh_build_after_each_change",
           "[audit][crafting][gui]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    scoped_weather_override weather( WEATHER_CLEAR );
    avatar &u = setup_crafter();
    set_time( calendar::turn_zero + 9_hours + 30_minutes );
    u.i_add( item( itype_fat ) );
    u.i_add( item( itype_knife_hunting ) );
    u.invalidate_crafting_inventory();

    std::vector<Character *> group = { &u };
    std::map<const recipe *, availability> availability_cache;
    const recipe_panel_context ctx{ u, group, u.get_group_available_recipes(), availability_cache };
    recipe_panel_cache cache;
    const recipe &tallow = recipe_test_tallow.obj();
    const recipe &cudgel = recipe_cudgel_test_no_tools.obj();

    const recipe_panel first = cached_and_checked( cache, tallow, 1, ctx );
    REQUIRE( cache.builds() == 1 );
    CHECK( first.components.shown );
    CHECK( first.tools.shown );
    CHECK_FALSE( first.too_dark );

    SECTION( "redraws with nothing changed read the cached panel" ) {
        for( int redraw = 0; redraw < 10; ++redraw ) {
            cached_and_checked( cache, tallow, 1, ctx );
        }
        CHECK( cache.builds() == 1 );
    }

    SECTION( "another selection" ) {
        const recipe_panel other = cached_and_checked( cache, cudgel, 1, ctx );
        CHECK( cache.builds() == 2 );
        CHECK( other.result_name != first.result_name );
        cached_and_checked( cache, tallow, 1, ctx );
        CHECK( cache.builds() == 3 );
    }

    SECTION( "another batch size" ) {
        const recipe_panel batch = cached_and_checked( cache, tallow, 3, ctx );
        CHECK( cache.builds() == 2 );
        CHECK( batch.stats_plain != first.stats_plain );
        CHECK_FALSE( batch.components == first.components );
    }

    SECTION( "the inventory changes" ) {
        u.i_add( item( itype_fat ) );
        u.invalidate_crafting_inventory();
        const recipe_panel more_fat = cached_and_checked( cache, tallow, 1, ctx );
        CHECK( cache.builds() == 2 );
        // "(have 1)" became "(have 2)"
        CHECK_FALSE( more_fat.components == first.components );

        // a book with the recipe changes where the panel says it comes from
        REQUIRE_FALSE( u.knows_recipe( &tallow ) );
        item cookbook( itype_family_cookbook );
        u.identify( cookbook );
        u.i_add( cookbook );
        u.invalidate_crafting_inventory();
        const recipe_panel with_book = cached_and_checked( cache, tallow, 1, ctx );
        CHECK( cache.builds() == 3 );
        CHECK( with_book.source_text != more_fat.source_text );
    }

    SECTION( "night falls" ) {
        const recipe_panel day = cached_and_checked( cache, cudgel, 1, ctx );
        CHECK_FALSE( day.too_dark );
        set_time( calendar::turn_zero );
        const recipe_panel night = cached_and_checked( cache, cudgel, 1, ctx );
        CHECK( cache.builds() == 3 );
        CHECK( night.too_dark );
        CHECK( night.light_mult <= 0.0f );
    }

    SECTION( "another crafter" ) {
        npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
        const tripoint_abs_ms pos = guy.pos_abs();
        clear_character( guy );
        guy.setpos( pos );
        group.push_back( &guy );
        const recipe &steps = recipe_cudgel_test_steps_required_prof.obj();
        const recipe_panel mine = cached_and_checked( cache, steps, 1, ctx );
        CHECK( cache.builds() == 2 );
        CHECK( mine.steps.size() == steps.steps().size() );

        std::map<const recipe *, availability> npc_availability;
        const recipe_panel_context npc_ctx{ guy, group, guy.get_group_available_recipes(),
                                            npc_availability };
        const recipe_panel theirs = cached_and_checked( cache, steps, 1, npc_ctx );
        CHECK( cache.builds() == 3 );
        // the NPC has no fabrication skill, the avatar 2
        REQUIRE( !theirs.skills.empty() );
        REQUIRE( !mine.skills.empty() );
        CHECK( theirs.skills.front().level < mine.skills.front().level );
        cached_and_checked( cache, steps, 1, npc_ctx );
        CHECK( cache.builds() == 3 );
        // and back
        CHECK( cached_and_checked( cache, steps, 1, ctx ) == mine );
        CHECK( cache.builds() == 4 );
    }

    SECTION( "a camp inventory changes" ) {
        temp_crafting_inventory camp;
        camp.add_item_copy( item( itype_fat ) );
        std::map<const recipe *, availability> camp_availability;
        const recipe_panel_context camp_ctx{ u, group, u.get_group_available_recipes( &camp ),
                                             camp_availability, true, &camp };
        const recipe_panel before = cached_and_checked( cache, tallow, 1, camp_ctx );
        CHECK( cache.builds() == 2 );
        camp.add_item_copy( item( itype_fat ) );
        const recipe_panel after = cached_and_checked( cache, tallow, 1, camp_ctx );
        CHECK( cache.builds() == 3 );
        CHECK_FALSE( after.components == before.components );
    }

    SECTION( "invalidate rebuilds" ) {
        cache.invalidate();
        cached_and_checked( cache, tallow, 1, ctx );
        CHECK( cache.builds() == 2 );
    }
}

// UIINV-P01: the panel shows what the menu showed: the stats line, the details table and the
// requirement rows, available alternatives first.
TEST_CASE( "uiinv-p01_info_panel_contents", "[audit][crafting][gui]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    scoped_weather_override weather( WEATHER_CLEAR );
    avatar &u = setup_crafter();
    set_time( calendar::turn_zero + 9_hours + 30_minutes );
    u.i_add( item( itype_2x4 ) );
    u.invalidate_crafting_inventory();

    std::vector<Character *> group = { &u };
    std::map<const recipe *, availability> availability_cache;
    const recipe_panel_context ctx{ u, group, u.get_group_available_recipes(), availability_cache };
    const recipe &cudgel = recipe_cudgel_test_no_tools.obj();
    const availability avail( u, &cudgel );
    const recipe_panel panel = build_recipe_panel( cudgel, avail, 1, ctx );

    CHECK( panel.result_name == cudgel.result_name( true ) );
    CHECK( panel.stats_plain.find( "chance you can make it" ) != std::string::npos );
    CHECK( panel.stats_colored.find( "<color_" ) != std::string::npos );
    CHECK( panel.success == 100.f * u.recipe_success_chance( cudgel ) );
    CHECK( panel.total_time == to_string( time_duration::from_turns(
            u.expected_time_to_craft( cudgel ) / to_moves<int>( 1_turns ) ) ) );
    // fabrication 2/2 and melee 1/1
    REQUIRE( panel.skills.size() == 2 );
    CHECK( panel.skills[0].level == 2 );
    CHECK( panel.skills[0].required == 2 );
    CHECK( panel.skills[1].level == 1 );
    // one group, "2x4 or pool cue", the 2x4 at hand first
    REQUIRE( panel.components.groups.size() == 1 );
    const recipe_panel_group &group_shown = panel.components.groups[0];
    REQUIRE( group_shown.options.size() == 2 );
    CHECK( group_shown.options[0].text.find( item::nname( itype_2x4 ) ) != std::string::npos );
    CHECK( group_shown.options[0].color == c_green );
    CHECK( group_shown.options[1].color == c_dark_gray );
    CHECK_FALSE( panel.tools.shown );
    CHECK( panel.steps.empty() );
    // the avatar knows it, so no helper is listed
    CHECK( panel.known_by.empty() );
}

// UIINV-P01: the panel cache reads an inventory's generation to tell when its entries changed.
TEST_CASE( "uiinv-p01_crafting_inventory_generation", "[audit][crafting]" )
{
    temp_crafting_inventory a;
    temp_crafting_inventory b;
    const uint64_t a_empty = a.generation();
    a.add_item_copy( item( itype_fat ) );
    const uint64_t a_one = a.generation();
    CHECK( a_one != a_empty );
    b.add_item_copy( item( itype_fat ) );
    // unique across inventories
    CHECK( b.generation() != a_one );
    a.update_liq_container_count( itype_fat, 1 );
    CHECK( a.generation() != a_one );
    const uint64_t a_two = a.generation();
    a.clear();
    CHECK( a.generation() != a_two );
    const uint64_t a_cleared = a.generation();
    a = b;
    CHECK( a.generation() != a_cleared );
    const temp_crafting_inventory c( b );
    CHECK( c.generation() != b.generation() );
}

// REND-P02: the tiles renderer remembers what each sprite lookup resolved to, misses included,
// keyed by season, category, id, suffix, intensity and variant, and forgets it all when the
// tileset or the game data changes.  The memo is plain C++, so it is tested here; the renderer
// that uses it builds only with tiles.
TEST_CASE( "rend-p02_tile_lookup_memo_keys_and_invalidation", "[audit][tiles]" )
{
    using memo_type = tile_lookup_memo<std::optional<int>, 4, 3>;
    memo_type memo;
    std::shared_ptr<int> tileset = std::make_shared<int>( 1 );
    memo.validate( tileset, 7 );
    // the memo watches its tileset without keeping it alive
    CHECK( tileset.use_count() == 1 );

    const std::string wall = "t_wall";
    const std::string no_variant;
    const auto find = [&]( size_t season, size_t category, const std::string & id,
    tile_lookup_suffix suffix, int intensity, const std::string & variant ) {
        return memo.find( season, category, id, suffix, intensity, variant );
    };
    const auto fill = [&]() {
        memo.remember( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant, 5 );
        // a miss is remembered too
        memo.remember( 0, 1, wall, tile_lookup_suffix::transparent, 0, no_variant, std::nullopt );
        memo.remember( 0, 1, wall, tile_lookup_suffix::intensity, 2, no_variant, 9 );
        memo.remember( 0, 1, wall, tile_lookup_suffix::none, 0, "rusty", 6 );
    };

    CHECK( find( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( memo.remember( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant, 5 ) == 5 );
    memo.clear();
    memo.validate( tileset, 7 );
    fill();
    REQUIRE( memo.size() == 4 );

    const std::optional<int> *hit = find( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant );
    REQUIRE( hit != nullptr );
    CHECK( *hit == 5 );
    const std::optional<int> *miss = find( 0, 1, wall, tile_lookup_suffix::transparent, 0,
                                           no_variant );
    REQUIRE( miss != nullptr );
    CHECK_FALSE( miss->has_value() );
    const std::optional<int> *intense = find( 0, 1, wall, tile_lookup_suffix::intensity, 2,
                                        no_variant );
    REQUIRE( intense != nullptr );
    CHECK( *intense == 9 );
    const std::optional<int> *rusty = find( 0, 1, wall, tile_lookup_suffix::none, 0, "rusty" );
    REQUIRE( rusty != nullptr );
    CHECK( *rusty == 6 );

    // every part of the key counts
    CHECK( find( 1, 1, wall, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( find( 0, 2, wall, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( find( 0, 1, "t_floor", tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( find( 0, 1, wall, tile_lookup_suffix::intensity, 3, no_variant ) == nullptr );
    CHECK( find( 0, 1, wall, tile_lookup_suffix::none, 0, "clean" ) == nullptr );
    // keys out of range are never remembered
    CHECK( memo.remember( 4, 0, wall, tile_lookup_suffix::none, 0, no_variant, 1 ) == 1 );
    CHECK( find( 4, 0, wall, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    CHECK( memo.size() == 4 );

    SECTION( "same tileset and game data: kept" ) {
        memo.validate( tileset, 7 );
        CHECK( memo.size() == 4 );
        CHECK( find( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant ) != nullptr );
    }
    SECTION( "game data reloaded: forgotten" ) {
        memo.validate( tileset, 8 );
        CHECK( memo.size() == 0 );
        CHECK( find( 0, 1, wall, tile_lookup_suffix::none, 0, no_variant ) == nullptr );
    }
    SECTION( "another tileset: forgotten" ) {
        const std::shared_ptr<int> other = std::make_shared<int>( 2 );
        memo.validate( other, 7 );
        CHECK( memo.size() == 0 );
    }
    SECTION( "the tileset was destroyed: forgotten, even for one at the same address" ) {
        // stands for a new tileset allocated where the old one was: same address, and not
        // the object the memo watched
        const std::shared_ptr<int> same_address( std::shared_ptr<int>(), tileset.get() );
        tileset.reset();
        memo.validate( same_address, 7 );
        CHECK( memo.size() == 0 );
    }
}

#if defined(TILES)
// REND-P02: a multitile's subtile ids are linked once the tileset is loaded, so a draw picks
// the subtile without building its id.  A subtile dropped for having no valid sprite is not
// linked, and the draw takes the old path for it.
TEST_CASE( "rend-p02_tileset_links_multitile_subtiles", "[audit][tiles]" )
{
    software_render_fixture fx;
    if( !fx.available() ) {
        WARN( "dummy SDL video backend unavailable; skipping" );
        return;
    }
    const std::string tileset_id = "audit_rend_p02_ts";
    const cata_path dir = PATH_INFO::user_dir_path() / "audit_rend_p02_tileset";
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
        os << "NAME: audit_rend_p02_ts\nJSON: tile_config.json\nTILESET: tiles.png\n";
    } );
    // Sprite 99 does not exist, so the corner subtile is dropped.
    write_to_file( json, []( std::ostream & os ) {
        os << R"({"tile_info":[{"width":4,"height":4}],"tiles-new":[{"file":"tiles.png",)"
           << R"("tiles":[{"id":"unknown","fg":0},{"id":"audit_plain","fg":3},)"
           << R"({"id":"audit_wall","fg":1,"multitile":true,"additional_tiles":[)"
           << R"({"id":"center","fg":2},{"id":"edge","fg":4},{"id":"corner","fg":99}]}]}]})";
    } );
    TILESETS[tileset_id] = dir;

    const bool error_seen_before = debug_has_error_been_observed();
    const std::shared_ptr<const tileset> ts = renderer_recovery_test_support::fetch_cached_bundle(
                tileset_id, "color_pixel_darken", renderer_coordinator.instance_generation(),
                renderer_coordinator.textures_generation() );
    REQUIRE( ts );
    // The loader reports the invalid subtile as D_ERROR. It is expected here.
    CHECK( debug_has_error_been_observed() );
    if( !error_seen_before ) {
        debug_reset_error_observed();
    }

    const tile_type *wall = ts->find_tile_type( "audit_wall" );
    REQUIRE( wall != nullptr );
    REQUIRE( wall->multitile );
    // center is subtile 0, corner 1, edge 2
    REQUIRE( wall->subtile_ids[0] != nullptr );
    CHECK( *wall->subtile_ids[0] == "audit_wall_center" );
    CHECK( ts->find_tile_type( *wall->subtile_ids[0] ) != nullptr );
    CHECK( wall->subtile_ids[1] == nullptr );
    REQUIRE( wall->subtile_ids[2] != nullptr );
    CHECK( *wall->subtile_ids[2] == "audit_wall_edge" );
    for( size_t i = 3; i < wall->subtile_ids.size(); ++i ) {
        CHECK( wall->subtile_ids[i] == nullptr );
    }
    const tile_type *plain = ts->find_tile_type( "audit_plain" );
    REQUIRE( plain != nullptr );
    for( const std::string *linked : plain->subtile_ids ) {
        CHECK( linked == nullptr );
    }
}
#endif // TILES

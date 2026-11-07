#include "cata_catch.h"

#if defined(SDL_SOUND)
#include <string>

#include "cata_scope_helpers.h"
#include "debug.h"
#include "sound_backend.h"
#endif

#if defined(TILES)
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "atlas_bake_plan.h"
#include "avatar.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "cata_utility.h"
#include "coordinates.h"
#include "debug.h"
#include "filesystem.h"
#include "game.h"
#include "item.h"
#include "level_cache.h"
#include "lightmap.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mdarray.h"
#include "memory_fast.h"
#include "monster.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overlay_ordering.h"
#include "path_info.h"
#include "player_helpers.h"
#include "point.h"
#include "sdl_renderer_recovery.h"
#include "sdltiles.h"
#include "trap.h"
#include "type_id.h"
#include "weighted_list.h"

static const field_type_str_id field_fd_acid( "fd_acid" );
static const field_type_str_id field_fd_bile( "fd_bile" );
static const field_type_str_id field_fd_blood( "fd_blood" );

static const furn_str_id furn_f_chair( "f_chair" );
static const furn_str_id furn_f_table( "f_table" );

static const itype_id itype_rock( "rock" );

static const mtype_id mon_zombie( "mon_zombie" );

static const ter_str_id ter_t_grass( "t_grass" );

static const trap_str_id tr_bubblewrap( "tr_bubblewrap" );

namespace
{

const tripoint_bub_ms player_pos( 65, 65, 0 );
constexpr int view_tiles = 9;

// A tileset "audit_m31a_<name>" in the user directory, cut from the 16 4x4 sprites of
// opaque_bounds_atlas.png, registered in TILESETS while it lives.  `tiles` are the "tiles" of tile_config.json,
// `extra` more of its top-level members, and `layering` layering.json (none if empty).
class test_tileset_dir
{
    public:
        test_tileset_dir( const std::string &name, const std::string &tiles, const std::string &extra,
                          const std::string &layering )
            : id( "audit_m31a_" + name ), dir( PATH_INFO::user_dir_path() / id ) {
            remove_all();
            REQUIRE( assure_dir_exist( dir ) );
            REQUIRE( copy_file( cata_path( cata_path::root_path::unknown,
                                           "tests/data/opaque_bounds_atlas.png" ), dir / "tiles.png" ) );
            write_to_file( dir / PATH_INFO::tileset_conf(), [&]( std::ostream & os ) {
                os << "NAME: " << id << "\nJSON: tile_config.json\nTILESET: tiles.png\n";
                if( !layering.empty() ) {
                    os << "LAYERING: layering.json\n";
                }
            } );
            write_to_file( dir / "tile_config.json", [&]( std::ostream & os ) {
                os << R"({"tile_info":[{"width":4,"height":4}],"tiles-new":[{"file":"tiles.png",)"
                   << R"("tiles":[)" << tiles << "]}]" << extra << "}";
            } );
            if( !layering.empty() ) {
                write_to_file( dir / "layering.json", [&]( std::ostream & os ) {
                    os << layering;
                } );
            }
            TILESETS[id] = dir;
        }
        ~test_tileset_dir() {
            remove_all();
        }
        test_tileset_dir( const test_tileset_dir & ) = delete;
        test_tileset_dir &operator=( const test_tileset_dir & ) = delete;

        const std::string id;
    private:
        const cata_path dir;
        void remove_all() {
            TILESETS.erase( id );
            remove_file( dir / PATH_INFO::tileset_conf() );
            remove_file( dir / "tile_config.json" );
            remove_file( dir / "layering.json" );
            remove_file( dir / "tiles.png" );
            remove_directory( dir.get_unrelative_path() );
        }
};

// tile entries for `ids`, each drawing its own sprite of the atlas
std::string tile_entries( const std::vector<std::string> &ids )
{
    std::string entries;
    for( size_t i = 0; i < ids.size(); ++i ) {
        if( i > 0 ) {
            entries += ",";
        }
        entries += R"({"id":")" + ids[i] + R"(","fg":)" + std::to_string( i ) + "}";
    }
    return entries;
}

// A test map of grass in daylight, the avatar in the middle, and the view centred on it as
// the game centres it (the tests leave POSX and POSY at 0, which puts the avatar in the
// top left corner of the view)
class lit_test_map
{
    public:
        lit_test_map() : restore_posx( POSX ), restore_posy( POSY ) {
            POSX = view_tiles / 2;
            POSY = view_tiles / 2;
            clear_npcs();
            clear_creatures();
            clear_map();
            clear_avatar();
            g->place_player( player_pos );
            build_test_map( ter_t_grass.id() );
            set_time_to_day();
        }
        ~lit_test_map() {
            // the draws memorized what the avatar saw
            get_avatar().clear_map_memory();
            clear_npcs();
            clear_creatures();
            clear_map();
        }
        lit_test_map( const lit_test_map & ) = delete;
        lit_test_map &operator=( const lit_test_map & ) = delete;

        // rebuild the caches after the scene changed, and require the avatar to see `scene`
        // clearly (else a tile gets a vision effect, or its memory)
        static void update( const std::vector<tripoint_bub_ms> &scene ) {
            map &here = get_map();
            here.invalidate_map_cache( 0 );
            here.build_map_cache( 0 );
            here.invalidate_visibility_cache();
            here.update_visibility_cache( 0 );
            const level_cache &level = here.access_cache( player_pos.z() );
            const visibility_variables &vision = here.get_visibility_variables_cache();
            for( const tripoint_bub_ms &p : scene ) {
                const lit_level ll = level.visibility_cache[p.x()][p.y()];
                CAPTURE( p - player_pos );
                CAPTURE( static_cast<int>( ll ) );
                REQUIRE( here.get_visibility( ll, vision ) == visibility_type::CLEAR );
            }
        }
    private:
        restore_on_out_of_scope<int> restore_posx;
        restore_on_out_of_scope<int> restore_posy;
};

// draws frames of `tiles` and logs their sprites
class draw_logger
{
    public:
        explicit draw_logger( cata_tiles &tiles ) : tiles( tiles ) {
            renderer_recovery_test_support::log_draw_light( tiles, &log );
        }
        ~draw_logger() {
            renderer_recovery_test_support::log_draw_light( tiles, nullptr );
        }
        draw_logger( const draw_logger & ) = delete;
        draw_logger &operator=( const draw_logger & ) = delete;

        // the second of two frames: the first memorizes what the avatar sees
        const std::vector<drawn_sprite_record> &draw() {
            for( int i = 0; i < 2; ++i ) {
                log.clear();
                tiles.set_draw_cache_dirty();
                renderer_recovery_test_support::draw_test_map( tiles, player_pos, view_tiles, view_tiles );
            }
            return log;
        }
        // the sprites drawn at map tile `p` whose id starts with `prefix`
        std::vector<drawn_sprite_record> at( const tripoint_bub_ms &p,
                                             const std::string_view prefix ) const {
            const point screen = tiles.player_to_screen( p.xy() );
            std::vector<drawn_sprite_record> found;
            for( const drawn_sprite_record &r : log ) {
                if( r.p == screen && string_starts_with( r.id, prefix ) ) {
                    found.push_back( r );
                }
            }
            return found;
        }
        // the variant pick (loc_rand) of the sprite drawn at `p` whose id starts with `prefix`;
        // if it was drawn more than once (a z-level below shows through), each the same
        unsigned int pick_at( const tripoint_bub_ms &p, const std::string &prefix ) const {
            const std::vector<drawn_sprite_record> found = at( p, prefix );
            CAPTURE( prefix );
            CAPTURE( p - player_pos );
            REQUIRE_FALSE( found.empty() );
            for( const drawn_sprite_record &r : found ) {
                CHECK( r.loc_rand == found.front().loc_rand );
            }
            return found.front().loc_rand;
        }
    private:
        cata_tiles &tiles;
        std::vector<drawn_sprite_record> log;
};

// a tile with two weighted options, so its draw picks one by its seed
tile_type two_option_tile()
{
    tile_type tile;
    tile.fg.add( std::vector<int> { 0 }, 1 );
    tile.fg.add( std::vector<int> { 0 }, 1 );
    return tile;
}

} // namespace

// REND-B04: an idle animation's pick is done unsigned.  An int frame count plus the tile's
// offset went negative for about half of the map, and the negative remainder, stored
// unsigned, made pick_ent skip a frame and show the first twice.
TEST_CASE( "rend-b04_idle_animation_shows_every_frame_whatever_the_offset", "[audit][tiles]" )
{
    // offsets from both halves of the range, far enough from the end not to wrap here
    for( const unsigned int offset : {
             0U, 12345U, 0x7FFFFFFFU, 0x80000000U, 0xC0000001U, 0xFFFFF000U
         } ) {
        CAPTURE( offset );
        // three frames of weights 2, 1 and 3: a loop of 6 ticks
        weighted_int_list<int> frames;
        frames.add( 0, 2 );
        frames.add( 1, 1 );
        frames.add( 2, 3 );
        std::array<int, 3> shown = {};
        constexpr int loops = 50;
        for( int64_t tick = 0; tick < 6 * loops; ++tick ) {
            // a tick is 17 ms
            const unsigned int pick = idle_animation_pick( 6, 0, tick * 17 + 5, offset );
            REQUIRE( pick < 6 );
            const int *frame = frames.pick( pick );
            REQUIRE( frame != nullptr );
            shown[*frame]++;
        }
        // each frame for its weight in ticks, every loop
        CHECK( shown[0] == 2 * loops );
        CHECK( shown[1] == 1 * loops );
        CHECK( shown[2] == 3 * loops );
    }
}

// REND-B04: the loop is the fg's, or the bg's when the fg has one sprite or none.  A bg-only
// animated tile divided by zero, and so did a one-sprite tile without a bg (portraits).
TEST_CASE( "rend-b04_idle_animation_loop_length", "[audit][tiles]" )
{
    const unsigned int offset = 0x9E3779B9U;
    for( int64_t tick = 0; tick < 40; ++tick ) {
        CAPTURE( tick );
        const int64_t ms = tick * 17;
        // the fg's loop
        CHECK( idle_animation_pick( 3, 5, ms, offset ) < 3 );
        // the bg's loop, with one fg sprite or none
        CHECK( idle_animation_pick( 1, 4, ms, offset ) < 4 );
        CHECK( idle_animation_pick( 0, 4, ms, offset ) < 4 );
        // one sprite: always it
        CHECK( idle_animation_pick( 1, 0, ms, offset ) == 0 );
        CHECK( idle_animation_pick( 0, 1, ms, offset ) == 0 );
        // nothing to animate: the offset as it is
        CHECK( idle_animation_pick( 0, 0, ms, offset ) == offset );
    }
    // the loop advances a frame a tick
    const unsigned int first = idle_animation_pick( 4, 0, 0, offset );
    for( int64_t tick = 1; tick < 12; ++tick ) {
        CAPTURE( tick );
        CHECK( idle_animation_pick( 4, 0, tick * 17, offset ) ==
               static_cast<unsigned int>( ( first + tick ) % 4 ) );
    }
}

// REND-B03: layering.json field_variants.  The field's sprite is the drawn field's, not the
// tile's displayed field's; a field the context doesn't list is drawn as usual (it was
// hidden); flag contexts work for fields as they do for items; and the loader leaves out
// sprites without a weight above 0 and variants with no sprite left (a draw divided by zero).
TEST_CASE( "rend-b03_layering_field_variants", "[audit][tiles]" )
{
    software_render_fixture fx;
    if( !fx.available() ) {
        WARN( "dummy SDL video backend unavailable; skipping" );
        return;
    }
    const std::string tile_json = tile_entries( {
        "unknown", ter_t_grass.str(), furn_f_chair.str(), furn_f_table.str(), field_fd_blood.str(),
        field_fd_acid.str(), field_fd_bile.str(), "audit_chair_blood", "audit_flat_bile"
    } );
    // f_chair: blood only.  f_table: only variants without a weight above 0, left out.  The
    // FLAT_SURF flag (tables have it, chairs and grass don't): bile, its sprite listed twice
    const std::string layering =
        R"({"variants":[)"
        R"({"context":"f_chair","field_variants":[)"
        R"({"field":"fd_blood","sprite":[{"id":"audit_chair_blood","weight":1}]}]},)"
        R"({"context":"f_table","field_variants":[)"
        R"({"field":"fd_bile","sprite":[{"id":"audit_zero","weight":0},{"id":"audit_minus","weight":-2}]},)"
        R"({"field":"fd_acid","sprite":[]}]},)"
        R"({"context":["FLAT_SURF"],"field_variants":[)"
        R"({"field":"fd_bile","sprite":[{"id":"audit_flat_bile"},{"id":"audit_flat_bile","weight":2}]}]})"
        R"(]})";
    const test_tileset_dir dir( "b03_ts", tile_json, "", layering );

    const bool error_seen_before = debug_has_error_been_observed();
    const std::shared_ptr<const tileset> ts = renderer_recovery_test_support::fetch_cached_bundle(
                dir.id, "color_pixel_darken", renderer_coordinator.instance_generation(),
                renderer_coordinator.textures_generation() );
    REQUIRE( ts );
    // the two variants left out are logged as errors; expected here, so they must not fail
    // the whole test run
    CHECK( debug_has_error_been_observed() );
    if( !error_seen_before ) {
        debug_reset_error_observed();
    }
    REQUIRE( ts->get_tileset_id() == dir.id );

    const auto chair = ts->field_layer_data.find( furn_f_chair.str() );
    REQUIRE( chair != ts->field_layer_data.end() );
    REQUIRE( chair->second.size() == 1 );
    CHECK( chair->second[0].id == field_fd_blood.str() );
    CHECK( chair->second[0].total_weight == 1 );
    const auto table = ts->field_layer_data.find( furn_f_table.str() );
    REQUIRE( table != ts->field_layer_data.end() );
    CHECK( table->second.empty() );
    const auto flat = ts->field_layer_data.find( "FLAT_SURF" );
    REQUIRE( flat != ts->field_layer_data.end() );
    REQUIRE( flat->second.size() == 1 );
    CHECK( flat->second[0].id == field_fd_bile.str() );
    CHECK( flat->second[0].total_weight == 3 );
    REQUIRE( flat->second[0].sprite.size() == 1 );
    CHECK( flat->second[0].sprite.at( "audit_flat_bile" ) == 3 );

    lit_test_map test_map;
    map &here = get_map();
    // blood and acid on a chair, bile on a table, blood on grass
    const tripoint_bub_ms chair_pos = player_pos + tripoint( 2, 0, 0 );
    const tripoint_bub_ms table_pos = player_pos + tripoint( -2, 0, 0 );
    const tripoint_bub_ms grass_pos = player_pos + tripoint( 0, 2, 0 );
    here.furn_set( chair_pos, furn_f_chair.id() );
    here.furn_set( table_pos, furn_f_table.id() );
    here.add_field( chair_pos, field_fd_blood.id(), 1 );
    here.add_field( chair_pos, field_fd_acid.id(), 1 );
    here.add_field( table_pos, field_fd_bile.id(), 1 );
    here.add_field( grass_pos, field_fd_blood.id(), 1 );
    lit_test_map::update( { chair_pos, table_pos, grass_pos } );
    REQUIRE( here.get_field_intensity( chair_pos, field_fd_blood.id() ) == 1 );
    REQUIRE( here.get_field_intensity( chair_pos, field_fd_acid.id() ) == 1 );
    REQUIRE( here.get_field_intensity( table_pos, field_fd_bile.id() ) == 1 );
    REQUIRE( here.get_field_intensity( grass_pos, field_fd_blood.id() ) == 1 );

    const std::unique_ptr<cata_tiles> tiles = renderer_recovery_test_support::make_test_tiles( ts );
    REQUIRE_FALSE( tiles->is_isometric() );
    draw_logger frames( *tiles );
    frames.draw();

    // the chair: its blood sprite once, and the acid it doesn't list as acid
    CHECK( frames.at( chair_pos, "audit_chair_blood" ).size() == 1 );
    CHECK( frames.at( chair_pos, field_fd_acid.str() ).size() == 1 );
    CHECK( frames.at( chair_pos, field_fd_blood.str() ).empty() );
    // the table: its own context lists no variant left, so its FLAT_SURF flag's bile sprite
    CHECK( frames.at( table_pos, "audit_flat_bile" ).size() == 1 );
    CHECK( frames.at( table_pos, field_fd_bile.str() ).empty() );
    // grass: no context, the field's own sprite
    CHECK( frames.at( grass_pos, field_fd_blood.str() ).size() == 1 );
    CHECK( frames.at( grass_pos, "audit_" ).empty() );
}

// REND-B05: weighted variants are picked from a seed.  Items, traps, the player, NPCs and
// their overlays had none, so they always showed the first; a monster's was its address,
// new after every reload.  Now items and traps take their position, as terrain does, and
// characters and monsters who they are.
TEST_CASE( "rend-b05_weighted_variants_of_items_traps_characters_and_monsters",
           "[audit][tiles]" )
{
    software_render_fixture fx;
    if( !fx.available() ) {
        WARN( "dummy SDL video backend unavailable; skipping" );
        return;
    }
    lit_test_map test_map;
    map &here = get_map();
    const tripoint_bub_ms trap_pos = player_pos + tripoint( -2, 0, 0 );
    const tripoint_bub_ms item_pos = player_pos + tripoint( 0, 2, 0 );
    const tripoint_bub_ms npc_a_pos = player_pos + tripoint( 2, 0, 0 );
    const tripoint_bub_ms npc_a_moved = player_pos + tripoint( 2, 2, 0 );
    const tripoint_bub_ms npc_b_pos = player_pos + tripoint( -2, -2, 0 );
    const tripoint_bub_ms monster_pos = player_pos + tripoint( 0, -3, 0 );
    here.trap_set( trap_pos, tr_bubblewrap.id() );
    here.add_item( item_pos, item( itype_rock ) );
    npc &npc_a = spawn_npc( npc_a_pos.xy(), "test_talker" );
    npc &npc_b = spawn_npc( npc_b_pos.xy(), "test_talker" );
    REQUIRE( npc_a.pos_bub() == npc_a_pos );
    REQUIRE( npc_b.pos_bub() == npc_b_pos );
    monster &zombie = spawn_test_monster( mon_zombie.str(), monster_pos );
    lit_test_map::update( { trap_pos, item_pos, npc_a_pos, npc_a_moved, npc_b_pos, monster_pos } );
    REQUIRE( here.tr_at( trap_pos ).can_see( trap_pos, get_avatar() ) );

    std::vector<std::pair<std::string, tile_type>> tile_list;
    for( const std::string &id : {
             std::string( "unknown" ), ter_t_grass.str(), tr_bubblewrap.str(), itype_rock.str(),
             mon_zombie.str(), std::string( "npc_male" ), std::string( "npc_female" ),
             std::string( "player_male" ), std::string( "player_female" )
         } ) {
        tile_list.emplace_back( id, two_option_tile() );
    }
    const std::shared_ptr<const tileset> ts = renderer_recovery_test_support::install_tiles_bundle(
                "audit_m31a_b05_ts", "color_pixel_darken", 1, 1, atlas_bake_plan{}, false, tile_list );
    REQUIRE( ts );
    const std::unique_ptr<cata_tiles> tiles = renderer_recovery_test_support::make_test_tiles( ts );
    REQUIRE_FALSE( tiles->is_isometric() );
    draw_logger frames( *tiles );
    frames.draw();

    // the trap and the item are seeded by their absolute position, as the grass under them
    const unsigned int grass_at_trap = frames.pick_at( trap_pos, ter_t_grass.str() );
    CHECK( grass_at_trap != 0 );
    CHECK( frames.pick_at( trap_pos, tr_bubblewrap.str() ) == grass_at_trap );
    const unsigned int grass_at_item = frames.pick_at( item_pos, ter_t_grass.str() );
    CHECK( frames.pick_at( item_pos, itype_rock.str() ) == grass_at_item );
    CHECK( grass_at_item != grass_at_trap );

    // NPCs and the avatar by who they are
    const unsigned int npc_a_seed = frames.pick_at( npc_a_pos, "npc_" );
    const unsigned int npc_b_seed = frames.pick_at( npc_b_pos, "npc_" );
    const unsigned int avatar_seed = frames.pick_at( player_pos, "player_" );
    CHECK( npc_a_seed != 0 );
    CHECK( npc_b_seed != 0 );
    CHECK( npc_a_seed != npc_b_seed );
    CHECK( avatar_seed != npc_a_seed );
    CHECK( avatar_seed != npc_b_seed );
    const unsigned int zombie_seed = frames.pick_at( monster_pos, mon_zombie.str() );

    // an NPC that moves keeps its variant
    npc_a.setpos( here, npc_a_moved );
    lit_test_map::update( { npc_a_moved } );
    frames.draw();
    CHECK( frames.at( npc_a_pos, "npc_" ).empty() );
    CHECK( frames.pick_at( npc_a_moved, "npc_" ) == npc_a_seed );
    CHECK( frames.pick_at( npc_b_pos, "npc_" ) == npc_b_seed );

    // a monster keeps its variant when it is loaded again: a new monster object with the
    // persistent id the save kept
    const int64_t zombie_uid = zombie.persistent_id();
    g->remove_zombie( zombie );
    const shared_ptr_fast<monster> reloaded = make_shared_fast<monster>( mon_zombie );
    reloaded->restore_persistent_id( zombie_uid );
    REQUIRE( g->place_critter_at( reloaded, monster_pos ) != nullptr );
    REQUIRE( reloaded->persistent_id() == zombie_uid );
    lit_test_map::update( { monster_pos } );
    frames.draw();
    CHECK( frames.pick_at( monster_pos, mon_zombie.str() ) == zombie_seed );
}

// REND-B07: the tileset's "overlay_ordering" is kept with the tileset, and only the main
// (TILES) context publishes it, also from a bundle found in the cache.  Every context's load
// cleared the one global ordering and only a parse refilled it, so the portrait and overmap
// tilesets, loaded after the main one, left it empty.
TEST_CASE( "rend-b07_overlay_ordering_is_the_main_tilesets", "[audit][tiles]" )
{
    software_render_fixture fx;
    if( !fx.available() ) {
        WARN( "dummy SDL video backend unavailable; skipping" );
        return;
    }
    const std::string mutation = "AUDIT_M31A_B07_WINGS";
    const std::string tile_json = tile_entries( { "unknown", ter_t_grass.str() } );
    const test_tileset_dir ordered( "b07_ordered_ts", tile_json,
                                    R"(,"overlay_ordering":[{"id":")" + mutation + R"(","order":1234}])", "" );
    const test_tileset_dir unordered( "b07_unordered_ts", tile_json, "", "" );

    restore_on_out_of_scope<std::map<std::string, int>> restore_ordering(
                tileset_mutation_overlay_ordering );
    tileset_mutation_overlay_ordering.clear();
    REQUIRE( get_overlay_order_of_mutation( mutation ) == 9999 );

    // a CI runner whose test renderer can't load a tileset skips this test, without failing the
    // run for the errors the failed load logs
    const bool error_seen_before = debug_has_error_been_observed();
    const auto skip = [error_seen_before]( const std::string & why ) {
        WARN( why + "; skipping" );
        if( !error_seen_before ) {
            debug_reset_error_observed();
        }
    };
    const std::shared_ptr<const tileset> seed =
        renderer_recovery_test_support::install_synthetic_bundle_with_highlight(
            "audit_m31a_b07_seed_ts", "color_pixel_darken", 1, 1 );
    if( !seed ) {
        skip( "the test renderer can't make a tileset" );
        return;
    }
    const std::shared_ptr<cata_tiles> main_context =
        renderer_recovery_test_support::make_test_tiles( seed );
    const std::shared_ptr<cata_tiles> other_context =
        renderer_recovery_test_support::make_test_tiles( seed );
    const std::shared_ptr<cata_tiles> new_main_context =
        renderer_recovery_test_support::make_test_tiles( seed );
    restore_on_out_of_scope<std::shared_ptr<cata_tiles>> restore_main( closetilecontext );
    const auto load = []( cata_tiles & context, const std::string & id ) {
        context.load_tileset( id, /*precheck=*/false, /*force=*/false, /*pump_events=*/false,
                              /*terrain=*/false );
    };

    closetilecontext = main_context;
    try {
        load( *main_context, ordered.id );
    } catch( const std::exception &err ) {
        skip( std::string( "the test renderer can't load a tileset: " ) + err.what() );
        return;
    }
    if( !main_context->is_valid() ) {
        skip( "the test renderer can't load a tileset" );
        return;
    }
    CHECK( get_overlay_order_of_mutation( mutation ) == 1234 );

    // another context (distant, portrait or overmap) loading another tileset leaves it
    load( *other_context, unordered.id );
    CHECK( get_overlay_order_of_mutation( mutation ) == 1234 );
    // or binding the main context's tileset, found in the cache
    load( *other_context, ordered.id );
    CHECK( get_overlay_order_of_mutation( mutation ) == 1234 );

    // the main context publishes a tileset found in the cache too
    tileset_mutation_overlay_ordering.clear();
    closetilecontext = new_main_context;
    load( *new_main_context, ordered.id );
    CHECK( get_overlay_order_of_mutation( mutation ) == 1234 );

    // and a main tileset without an ordering has none
    load( *new_main_context, unordered.id );
    CHECK( get_overlay_order_of_mutation( mutation ) == 9999 );
}
#endif // TILES

#if defined(SDL_SOUND)
namespace
{
int music_finished_calls = 0;
void count_music_finished()
{
    ++music_finished_calls;
}
} // namespace

// REND-B06: the music track's stopped callback fires on the mixer's audio thread, where the
// finished callback (sdlsound's musicFinished) raced the main thread over the music state.
// The callback now only records that the track ended, and poll() (or poll_music(), which
// the input wait runs) runs the finished callback on the main thread, unless a play or stop
// came in between.
TEST_CASE( "rend-b06_music_finished_runs_on_the_main_thread", "[audit][sound_backend]" )
{
    // A CI runner without audio (no mixer can be opened, or it can't mix) skips this test,
    // without failing the run for the errors the failed start logs
    const bool error_seen_before = debug_has_error_been_observed();
    const auto skip = [error_seen_before]( const char *why ) {
        WARN( std::string( why ) + "; skipping" );
        if( !error_seen_before ) {
            debug_reset_error_observed();
        }
    };
    // a memory mixer mixes on the thread that asks it to, as a device's audio thread does
    if( !sound_backend::init( 44100, 2, 1024,
                              sound_backend::init_options{ /*memory_only=*/true } ) ) {
        skip( "no audio mixer could be opened" );
        return;
    }
    on_out_of_scope shut_down( []() {
        sound_backend::set_music_finished_cb( nullptr );
        sound_backend::shutdown();
    } );
    music_finished_calls = 0;
    sound_backend::set_music_finished_cb( &count_music_finished );
    sound_backend::music_source *const music = sound_backend::testing::make_synthetic_music();
    if( music == nullptr ) {
        skip( "no music track could be made for the mixer" );
        return;
    }
    // freed before the mixer shuts down
    on_out_of_scope free_music( [music]() {
        sound_backend::free_music( music );
    } );
    const auto play_until_it_ends = [music]() {
        REQUIRE( sound_backend::play_music( music, 0, 0 ) );
        // the one-frame track runs out inside a mix
        for( int i = 0; i < 8 && sound_backend::is_music_playing(); ++i ) {
            REQUIRE( sound_backend::testing::mix_memory_frames( 1024 ) );
        }
        REQUIRE_FALSE( sound_backend::is_music_playing() );
    };

    // the mixer must be able to mix at all
    if( !sound_backend::testing::mix_memory_frames( 1024 ) ) {
        skip( "the audio mixer can't mix" );
        return;
    }
    play_until_it_ends();
    // not from inside the mix
    CHECK( music_finished_calls == 0 );
    sound_backend::poll_music();
    CHECK( music_finished_calls == 1 );
    // once
    sound_backend::poll_music();
    sound_backend::poll();
    CHECK( music_finished_calls == 1 );

    // poll() runs it too
    play_until_it_ends();
    CHECK( music_finished_calls == 1 );
    sound_backend::poll();
    CHECK( music_finished_calls == 2 );

    // a stop between the end and the poll supersedes it
    play_until_it_ends();
    sound_backend::stop_music( 0 );
    sound_backend::poll_music();
    CHECK( music_finished_calls == 2 );

    // and so does a new track
    play_until_it_ends();
    REQUIRE( sound_backend::play_music( music, 0, 0 ) );
    sound_backend::poll_music();
    CHECK( music_finished_calls == 2 );
}
#endif // SDL_SOUND

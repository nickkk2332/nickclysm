#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "construction.h"
#include "coordinates.h"
#include "end_screen.h"
#include "fault.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "mod_manager.h"
#include "options_helpers.h"
#include "path_info.h"
#include "point.h"
#include "profession_group.h"
#include "submap.h"
#include "type_id.h"
#include "worldfactory.h"
#include "zzip.h"
#include "zzip_stack.h"

static const construction_str_id construction_test_constr_door_frame( "test_constr_door_frame" );
static const end_screen_id
end_screen_audit_data_b05_end_screen_child( "audit_data_b05_end_screen_child" );
static const fault_group_id
fault_group_audit_data_b05_fault_group_child( "audit_data_b05_fault_group_child" );
static const mod_id MOD_INFORMATION_dda( "dda" );
static const mod_id MOD_INFORMATION_test_data( "test_data" );
static const profession_group_id
profession_group_audit_data_b05_profession_group_child( "audit_data_b05_profession_group_child" );
static const ter_str_id ter_t_dirt( "t_dirt" );

// DATA-B04: a mod_migration with "new_id" inserted the new id and then erased through the
// old iterator, which then pointed at the new id: the renamed mod was dropped and the old
// id kept (or worse, after a reallocation).  TEST_DATA renames
// audit_data_b04_renamed_mod to test_data.
TEST_CASE( "data-b04_mod_migration_renames_the_mod_in_place", "[audit][mods]" )
{
    WORLD world( "audit data-b04" );
    std::vector<mod_id> &mods = world.active_mod_order;
    mods.clear();
    // room to grow, so that the old code had no reallocation to hide behind
    mods.reserve( 8 );
    mods.emplace_back( MOD_INFORMATION_dda );
    mods.emplace_back( "audit_data_b04_renamed_mod" );

    REQUIRE( assure_dir_exist( world.folder_path() ) );
    on_out_of_scope cleanup( [&]() {
        remove_file( world.folder_path() / "mods.json" );
        remove_directory( world.folder_path().get_unrelative_path() );
    } );
    world_generator->get_mod_manager().check_mods_list( &world );

    const std::vector<mod_id> expected = { MOD_INFORMATION_dda, MOD_INFORMATION_test_data };
    CHECK( mods == expected );
    // and the world's saved list
    std::vector<mod_id> saved;
    REQUIRE( read_from_file_json( world.folder_path() / "mods.json", [&]( const JsonValue & jv ) {
        jv.read( saved );
    } ) );
    CHECK( saved == expected );
}

// DATA-B05: these types were never finalized, which is where an object whose copy-from
// parent comes later is loaded: it was silently dropped.  In TEST_DATA each child comes
// before its parent.
TEST_CASE( "data-b05_copy_from_a_later_parent_loads", "[audit][mods]" )
{
    REQUIRE( profession_group_audit_data_b05_profession_group_child.is_valid() );
    CHECK( profession_group_audit_data_b05_profession_group_child->get_professions().size() == 1 );
    CHECK( end_screen_audit_data_b05_end_screen_child.is_valid() );
    CHECK( fault_group_audit_data_b05_fault_group_child.is_valid() );
}

namespace
{
// What saving the submap writes
JsonObject stored( const submap &sm )
{
    std::ostringstream os;
    JsonOut jsout( os );
    jsout.start_object();
    sm.store( jsout );
    jsout.end_object();
    JsonObject jo = json_loader::from_string( os.str() );
    jo.allow_omitted_members();
    return jo;
}
} // namespace

// DATA-B06: a uniform submap is saved as its terrain alone, but a partial construction (or
// a phase revert) didn't make it non-uniform, so it was lost on save: on a lake or in the
// air, a construction's progress and its components were gone.
TEST_CASE( "data-b06_construction_on_a_uniform_submap_is_saved", "[audit][mapbuffer]" )
{
    map &here = get_map();
    // A submap at the top of the sky, made uniform as open air and lake surfaces are
    // generated; put back as it was afterwards
    tripoint_bub_ms p( 3, 4, OVERMAP_HEIGHT );
    point_sm_ms l;
    submap *sm = map_meddler::unsafe_get_submap_at( p, l );
    REQUIRE( sm != nullptr );
    submap before = sm->get_revert_submap();
    const bool was_reverted = sm->reverted;
    submap blank;
    blank.set_all_ter( sm->get_ter( l ), true );
    sm->revert_submap( blank );
    on_out_of_scope restore( [&]() {
        here.partial_con_remove( p );
        here.clear_original_terrain_at( p );
        sm->revert_submap( before );
        sm->reverted = was_reverted;
    } );
    REQUIRE( sm->is_uniform() );

    SECTION( "partial construction" ) {
        partial_con con;
        con.id = construction_test_constr_door_frame.id();
        con.counter = 4321;
        here.partial_con_set( p, con );
        CHECK_FALSE( sm->is_uniform() );
        const JsonObject jo = stored( *sm );
        REQUIRE( jo.has_array( "partial_constructions" ) );
        CHECK_FALSE( jo.get_array( "partial_constructions" ).empty() );
    }
    SECTION( "phase revert" ) {
        here.set_original_terrain_at( p, ter_t_dirt.id() );
        CHECK_FALSE( sm->is_uniform() );
        const JsonObject jo = stored( *sm );
        REQUIRE( jo.has_array( "phase_reverts" ) );
        CHECK_FALSE( jo.get_array( "phase_reverts" ).empty() );
    }
}

namespace
{
// Runs fn on a new, empty, uncompressed world (not the active one), deleted afterwards
void in_audit_world( const std::string &name, const std::function<void( WORLD & )> &fn )
{
    WORLD *scratch = nullptr;
    {
        override_option compression( "WORLD_COMPRESSION2", "false" );
        scratch = world_generator->make_new_world( name,
                  world_generator->active_world->active_mod_order );
    }
    REQUIRE( scratch != nullptr );
    on_out_of_scope cleanup( [&]() {
        world_generator->delete_world( name, true );
    } );
    REQUIRE_FALSE( scratch->has_compression_enabled() );
    fn( *scratch );
}

void write_text( const cata_path &file, const std::string &text )
{
    REQUIRE( assure_dir_exist( file.parent_path() ) );
    write_to_file( file, [&]( std::ostream & os ) {
        os << text;
    } );
}

bool world_reads_compressed( const WORLD &world )
{
    // as a fresh load of the world would see it
    WORLD fresh( world.world_name );
    return fresh.has_compression_enabled();
}
} // namespace

// DATA-B07: a zzip whose dictionary is missing crashed on load
TEST_CASE( "data-b07_zzip_with_a_missing_dictionary_does_not_load", "[audit][zzip]" )
{
    const cata_path dir = PATH_INFO::savedir_path() / "audit data-b07 zzip";
    REQUIRE( assure_dir_exist( dir ) );
    on_out_of_scope cleanup( [&]() {
        std::error_code ec;
        std::filesystem::remove_all( dir.get_unrelative_path(), ec );
    } );
    const std::optional<zzip> z = zzip::load( ( dir / "audit.zzip" ).get_unrelative_path(),
                                  ( dir / "audit_missing.dict" ).get_unrelative_path() );
    CHECK_FALSE( z.has_value() );
}

// DATA-B07: decompressing deleted the dictionaries after the first dimension, so the next
// one couldn't be read: a crash, and a half-decompressed world.
TEST_CASE( "data-b07_world_with_dimensions_compresses_and_decompresses", "[audit][zzip]" )
{
    in_audit_world( "audit data-b07 round trip", []( WORLD & world ) {
        const cata_path root = world.folder_path();
        const cata_path dim_map = root / "dimensions" / "audit_dim" / "maps" / "0.0.0" / "0.0.0.map";
        const cata_path root_overmap = root / "o.0.0";
        write_text( dim_map, "audit dimension map" );
        write_text( root_overmap, "audit overmap" );

        REQUIRE( world.set_compression_enabled( true ) );
        CHECK( world_reads_compressed( world ) );
        CHECK_FALSE( file_exist( dim_map ) );
        CHECK_FALSE( file_exist( root_overmap ) );

        REQUIRE( world.set_compression_enabled( false ) );
        CHECK_FALSE( world_reads_compressed( world ) );
        CHECK( read_entire_file( dim_map.get_unrelative_path() ) == "audit dimension map" );
        CHECK( read_entire_file( root_overmap.get_unrelative_path() ) == "audit overmap" );
    } );
}

// DATA-B07: compressing copied the dictionaries in first and deleted each dimension's files
// once that dimension was done, so a failure later on left a world that reads as
// compressed, with some of its maps only in the uncompressed files it then ignores.
TEST_CASE( "data-b07_failed_compression_leaves_the_world_as_it_was", "[audit][zzip]" )
{
    in_audit_world( "audit data-b07 failure", []( WORLD & world ) {
        const cata_path root = world.folder_path();
        const cata_path dim_map = root / "dimensions" / "audit_dim" / "maps" / "0.0.0" / "0.0.0.map";
        const cata_path root_map = root / "maps" / "1.1.1" / "1.1.1.map";
        write_text( dim_map, "audit dimension map" );
        write_text( root_map, "audit map" );
        // The root's map segment can't be compressed: a folder is in the zzip's way
        REQUIRE( assure_dir_exist( root / "maps" / "1.1.1.zzip" ) );

        CHECK_FALSE( world.set_compression_enabled( true ) );
        CHECK_FALSE( world_reads_compressed( world ) );
        CHECK( file_exist( dim_map ) );
        CHECK( file_exist( root_map ) );
    } );
}

// DATA-B08: when one of a zzip_stack's zzips can't be opened (locked by another program, a
// full disk), its accessors dereferenced an empty optional.  Saving or loading map memory
// then crashed instead of failing.
TEST_CASE( "data-b08_zzip_stack_that_cannot_open_fails_cleanly", "[audit][zzip]" )
{
    const cata_path dir = PATH_INFO::savedir_path() / "audit data-b08";
    const cata_path stack_dir = dir / "stack";
    // a folder where the stack's newest zzip goes: it can't be opened
    REQUIRE( assure_dir_exist( stack_dir / "stack.hot.zzip" ) );
    on_out_of_scope cleanup( [&]() {
        std::error_code ec;
        std::filesystem::remove_all( dir.get_unrelative_path(), ec );
    } );

    std::shared_ptr<zzip_stack> z = zzip_stack::load( stack_dir.get_unrelative_path() );
    REQUIRE( z );
    const std::filesystem::path file = std::filesystem::u8path( "audit.txt" );
    CHECK_FALSE( z->add_file( file, "audit" ) );
    CHECK_FALSE( z->has_file( file ) );
    CHECK( z->get_file( file ).empty() );
    CHECK_FALSE( z->compact( 0.0 ) );
    CHECK_FALSE( zzip_stack::extract_to_folder( stack_dir.get_unrelative_path(),
                 ( dir / "out" ).get_unrelative_path() ) );
}

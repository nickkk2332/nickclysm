#include <algorithm>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "construction.h"
#include "coordinates.h"
#include "dependency_tree.h"
#include "end_screen.h"
#include "fault.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "mapbuffer.h"
#include "mod_manager.h"
#include "omdata.h"
#include "options_helpers.h"
#include "overmapbuffer.h"
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
static const mongroup_id GROUP_test_nested_packsize( "test_nested_packsize" );
static const mtype_id mon_test_CBM( "mon_test_CBM" );
static const oter_str_id oter_open_air( "open_air" );
static const profession_group_id
profession_group_audit_data_b05_profession_group_child( "audit_data_b05_profession_group_child" );
static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_open_air( "t_open_air" );

namespace
{
// Runs check_mods_list on a world with these mods, and returns its list and its saved list
std::pair<std::vector<mod_id>, std::vector<mod_id>> checked_mods_list(
            const std::vector<mod_id> &mods )
{
    WORLD world( "audit data-b04" );
    world.active_mod_order.clear();
    // room to grow, so that the old code had no reallocation to hide behind
    world.active_mod_order.reserve( 8 );
    world.active_mod_order.insert( world.active_mod_order.end(), mods.begin(), mods.end() );

    on_out_of_scope cleanup( [&]() {
        remove_file( world.folder_path() / "mods.json" );
        remove_directory( world.folder_path().get_unrelative_path() );
    } );
    REQUIRE( assure_dir_exist( world.folder_path() ) );
    world_generator->get_mod_manager().check_mods_list( &world );

    std::vector<mod_id> saved;
    read_from_file_optional_json( world.folder_path() / "mods.json", [&]( const JsonValue & jv ) {
        jv.read( saved );
    } );
    return { world.active_mod_order, saved };
}
} // namespace

// DATA-B04: a mod_migration with "new_id" inserted the new id and then erased through the
// old iterator, which then pointed at the new id: the renamed mod was dropped and the old
// id kept (or worse, after a reallocation).  TEST_DATA renames
// audit_data_b04_renamed_mod to test_data.
TEST_CASE( "data-b04_mod_migration_renames_the_mod_in_place", "[audit][mods]" )
{
    const std::vector<mod_id> expected = { MOD_INFORMATION_dda, MOD_INFORMATION_test_data };
    const auto [mods, saved] = checked_mods_list( { MOD_INFORMATION_dda,
                               mod_id( "audit_data_b04_renamed_mod" )
                                                  } );
    CHECK( mods == expected );
    CHECK( saved == expected );
}

// DATA-B04: the new id may have been renamed in its turn.  TEST_DATA renames
// audit_data_b04_chain_a to audit_data_b04_chain_b, and that to test_data.
TEST_CASE( "data-b04_mod_migration_follows_a_rename_of_a_rename", "[audit][mods]" )
{
    const std::vector<mod_id> expected = { MOD_INFORMATION_dda, MOD_INFORMATION_test_data };
    const auto [mods, saved] = checked_mods_list( { MOD_INFORMATION_dda,
                               mod_id( "audit_data_b04_chain_a" )
                                                  } );
    CHECK( mods == expected );
    CHECK( saved == expected );
}

// DATA-B04: renames that go round in a circle lead nowhere, and stop
TEST_CASE( "data-b04_mod_renames_in_a_circle_lead_nowhere", "[audit][mods]" )
{
    const mod_id a( "audit_data_b04_circle_a" );
    const mod_id b( "audit_data_b04_circle_b" );
    const mod_id c( "audit_data_b04_circle_c" );
    const std::map<mod_id, mod_id> renames = { { a, b }, { b, c }, { c, a } };
    CHECK_FALSE( mod_migrations::renamed_to( a, renames ).has_value() );
    CHECK_FALSE( mod_migrations::renamed_to( b, renames ).has_value() );
    // a chain ends at the last id, installed or not
    const std::map<mod_id, mod_id> chain = { { a, b }, { b, c } };
    CHECK( mod_migrations::renamed_to( a, chain ) == std::optional<mod_id>( c ) );
    CHECK_FALSE( mod_migrations::renamed_to( c, chain ).has_value() );
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
// Runs fn with a new, empty world active; the test world comes back after
void in_active_audit_world( const std::string &name, const std::function<void()> &fn )
{
    WORLD *const test_world = world_generator->active_world;
    WORLD *scratch = nullptr;
    {
        override_option compression( "WORLD_COMPRESSION2", "false" );
        scratch = world_generator->make_new_world( name, test_world->active_mod_order );
    }
    REQUIRE( scratch != nullptr );
    world_generator->set_active_world( scratch );
    on_out_of_scope restore( [&]() {
        world_generator->set_active_world( test_world );
        world_generator->delete_world( name, true );
    } );
    fn();
}
} // namespace

// DATA-B06: a uniform submap was saved as its terrain alone, so a partial construction, a
// pending spawn or a phase revert on it (on a lake, in the open air) was lost.  They are
// saved now, and the submap stays uniform: it costs no more than before.
TEST_CASE( "data-b06_uniform_submap_keeps_its_state_through_save_and_load",
           "[audit][mapbuffer]" )
{
    in_active_audit_world( "audit data-b06", []() {
        const tripoint_abs_sm addr = project_to<coords::sm>( tripoint_abs_omt( 40, 40, 1 ) );
        mapbuffer mb;
        // a uniform quad, as open air is generated
        for( int dx = 0; dx < 2; ++dx ) {
            for( int dy = 0; dy < 2; ++dy ) {
                std::unique_ptr<submap> fresh = std::make_unique<submap>();
                fresh->set_all_ter( ter_t_open_air.id(), true );
                REQUIRE( mb.add_submap( addr + tripoint_rel_sm( dx, dy, 0 ), fresh ) );
            }
        }
        submap *sm = mb.lookup_submap( addr );
        REQUIRE( sm != nullptr );
        partial_con con;
        con.id = construction_test_constr_door_frame.id();
        con.counter = 4321;
        sm->partial_constructions.emplace( tripoint_sm_ms( 3, 4, 1 ), con );
        sm->spawns.emplace_back( mon_test_CBM, 2, point_sm_ms( 5, 6 ) );
        sm->set_original_ter( point_sm_ms( 2, 2 ), ter_t_dirt.id() );
        REQUIRE( sm->is_uniform() );

        mb.save( true );
        REQUIRE( mb.begin() == mb.end() );
        submap *loaded = mb.lookup_submap( addr );
        REQUIRE( loaded != nullptr );
        CHECK( loaded->is_uniform() );
        CHECK( loaded->get_ter( point_sm_ms( 7, 7 ) ) == ter_t_open_air.id() );
        REQUIRE( loaded->partial_constructions.size() == 1 );
        CHECK( loaded->partial_constructions.begin()->second.counter == 4321 );
        REQUIRE( loaded->spawns.size() == 1 );
        CHECK( loaded->spawns.front().type == mon_test_CBM );
        CHECK( loaded->get_original_ter( point_sm_ms( 2, 2 ) ) == ter_t_dirt.id() );
        const submap *neighbour = mb.lookup_submap( addr + tripoint_rel_sm( 1, 1, 0 ) );
        REQUIRE( neighbour != nullptr );
        CHECK( neighbour->is_uniform() );

        // With nothing left on it, the saved quad goes: it is generated again
        loaded->partial_constructions.clear();
        loaded->spawns.clear();
        loaded->clear_original_ter( point_sm_ms( 2, 2 ) );
        mb.save( true );
        CHECK( mb.lookup_submap( addr ) == nullptr );
    } );
}

// DATA-B06: a spawn added to a uniform submap leaves it uniform.  Reproducing monsters add
// spawns that are used at once; a submap made non-uniform by one would be saved for good,
// and, in the air, would cost every turn in the populated z-level cache.
TEST_CASE( "data-b06_spawn_on_a_uniform_submap_leaves_it_uniform", "[audit][mapbuffer]" )
{
    clear_map();
    // Nothing outside the reality bubble stays buffered, and the tests never save maps
    MAPBUFFER.clear_outside_reality_bubble();
    const tripoint_abs_omt omt = project_to<coords::omt>( get_map().get_abs_sub() ) +
                                 tripoint_rel_omt( 12, 12, 0 );
    const oter_id original_oter = overmap_buffer.ter( omt );
    overmap_buffer.ter_set( omt, oter_open_air.id() );
    on_out_of_scope restore( [&]() {
        MAPBUFFER.clear_outside_reality_bubble();
        overmap_buffer.ter_set( omt, original_oter );
    } );

    tinymap tm;
    tm.load( omt, false );
    submap *sm = MAPBUFFER.lookup_submap( project_to<coords::sm>( omt ) );
    REQUIRE( sm != nullptr );
    REQUIRE( sm->is_uniform() );

    tm.place_spawns( GROUP_test_nested_packsize, 1, point_omt_ms( 3, 3 ), point_omt_ms( 3, 3 ),
                     omt.z(), 1.0f, true );
    REQUIRE_FALSE( sm->spawns.empty() );
    CHECK( sm->is_uniform() );
    CHECK( sm->holds_more_than_terrain() );

    // used at once, as a reproducing monster's: nothing is left to save
    sm->spawns.clear();
    CHECK( sm->is_uniform() );
    CHECK_FALSE( sm->holds_more_than_terrain() );
}

namespace
{
// A folder for a test's files, under one only these tests use.  Removed with everything in
// it when the test ends, whatever happened.
struct audit_scratch_folder {
    cata_path parent = PATH_INFO::user_dir_path() / "audit_sweep_tests";
    cata_path path;
    explicit audit_scratch_folder( const std::string &name ) : path( parent / name ) {
        remove();
    }
    ~audit_scratch_folder() {
        remove();
        std::error_code ec;
        // only if empty
        std::filesystem::remove( parent.get_unrelative_path(), ec );
    }
    void remove() const {
        std::error_code ec;
        std::filesystem::remove_all( path.get_unrelative_path(), ec );
    }
};

// Runs fn on a new, empty, uncompressed world (not the active one), deleted afterwards
void in_audit_world( const std::string &name, const std::function<void( WORLD & )> &fn )
{
    on_out_of_scope cleanup( [&]() {
        if( world_generator->get_world( name ) != nullptr ) {
            world_generator->delete_world( name, true );
        }
    } );
    WORLD *scratch = nullptr;
    {
        override_option compression( "WORLD_COMPRESSION2", "false" );
        scratch = world_generator->make_new_world( name,
                  world_generator->active_world->active_mod_order );
    }
    REQUIRE( scratch != nullptr );
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

// Everything in a folder, files and folders, by path
std::set<std::string> contents( const cata_path &folder )
{
    std::set<std::string> ret;
    for( const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator( folder.get_unrelative_path() ) ) {
        ret.insert( entry.path().lexically_relative( folder.get_unrelative_path() ).generic_u8string() );
    }
    return ret;
}
} // namespace

// DATA-B07: a zzip whose dictionary is missing crashed on load
TEST_CASE( "data-b07_zzip_with_a_missing_dictionary_does_not_load", "[audit][zzip]" )
{
    const audit_scratch_folder dir( "data-b07 zzip" );
    REQUIRE( assure_dir_exist( dir.path ) );
    const std::optional<zzip> z = zzip::load( ( dir.path / "audit.zzip" ).get_unrelative_path(),
                                  ( dir.path / "audit_missing.dict" ).get_unrelative_path() );
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
// compressed, with some of its maps only in the uncompressed files it then ignores.  Now a
// failure leaves the world as it was, without the zzips it had made.
TEST_CASE( "data-b07_failed_compression_leaves_the_world_as_it_was", "[audit][zzip]" )
{
    in_audit_world( "audit data-b07 failure", []( WORLD & world ) {
        const cata_path root = world.folder_path();
        const cata_path dim_map = root / "dimensions" / "audit_dim" / "maps" / "0.0.0" / "0.0.0.map";
        const cata_path root_overmap = root / "o.0.0";
        const cata_path root_map = root / "maps" / "1.1.1" / "1.1.1.map";
        write_text( dim_map, "audit dimension map" );
        write_text( root_overmap, "audit overmap" );
        write_text( root_map, "audit map" );
        // A zzip left over from an earlier attempt, which this one would have added to
        write_text( root / "dimensions" / "audit_dim" / "maps" / "0.0.0.zzip", "stale" );
        // The root's map segment can't be compressed: a folder is in the zzip's way
        REQUIRE( assure_dir_exist( root / "maps" / "1.1.1.zzip" ) );
        std::set<std::string> before = contents( root );
        // replaced by a fresh one, which then goes with the rest
        before.erase( "dimensions/audit_dim/maps/0.0.0.zzip" );

        CHECK_FALSE( world.set_compression_enabled( true ) );
        CHECK_FALSE( world_reads_compressed( world ) );
        CHECK( contents( root ) == before );
        CHECK( read_entire_file( dim_map.get_unrelative_path() ) == "audit dimension map" );
    } );
}

// DATA-B07: a failed decompression leaves the world compressed, without the files it had
// extracted
TEST_CASE( "data-b07_failed_decompression_leaves_the_world_as_it_was", "[audit][zzip]" )
{
    in_audit_world( "audit data-b07 failed decompression", []( WORLD & world ) {
        const cata_path root = world.folder_path();
        const cata_path dim_map = root / "dimensions" / "audit_dim" / "maps" / "0.0.0" / "0.0.0.map";
        write_text( dim_map, "audit dimension map" );
        write_text( root / "o.0.0", "audit overmap" );
        REQUIRE( world.set_compression_enabled( true ) );
        // The root's overmap can't be extracted: a folder is in its way.  The dimension is
        // extracted before.
        REQUIRE( assure_dir_exist( root / "o.0.0" ) );
        const std::set<std::string> before = contents( root );

        CHECK_FALSE( world.set_compression_enabled( false ) );
        CHECK( world_reads_compressed( world ) );
        CHECK( contents( root ) == before );
        CHECK_FALSE( file_exist( dim_map ) );
    } );
}

// DATA-B08: when one of a zzip_stack's zzips can't be opened (locked by another program, a
// full disk), its accessors dereferenced an empty optional.  Saving or loading map memory
// then crashed instead of failing.
TEST_CASE( "data-b08_zzip_stack_that_cannot_open_fails_cleanly", "[audit][zzip]" )
{
    const audit_scratch_folder dir( "data-b08" );
    const cata_path stack_dir = dir.path / "stack";
    // a folder where the stack's newest zzip goes: it can't be opened
    REQUIRE( assure_dir_exist( stack_dir / "stack.hot.zzip" ) );

    std::shared_ptr<zzip_stack> z = zzip_stack::load( stack_dir.get_unrelative_path() );
    REQUIRE( z );
    const std::filesystem::path file = std::filesystem::u8path( "audit.txt" );
    CHECK_FALSE( z->add_file( file, "audit" ) );
    CHECK_FALSE( z->has_file( file ) );
    CHECK( z->get_file( file ).empty() );
    CHECK_FALSE( z->compact( 0.0 ) );
    CHECK_FALSE( zzip_stack::extract_to_folder( stack_dir.get_unrelative_path(),
                 ( dir.path / "out" ).get_unrelative_path() ) );
}

// DATA-B09: dependencies were listed in depth-first pre-order, reversed, which isn't a valid
// load order: with M needing A and B, and A needing B, A came before B.
TEST_CASE( "data-b09_dependencies_come_after_their_own_dependencies", "[audit][mods]" )
{
    const mod_id core( "audit_core" );
    const mod_id a( "audit_a" );
    const mod_id b( "audit_b" );
    const mod_id m( "audit_m" );
    dependency_tree tree;
    tree.init( { { core, {} }, { b, { core } }, { a, { core, b } }, { m, { core, a, b } } } );

    const std::vector<mod_id> order = tree.get_dependencies_of_X_as_strings( m );
    REQUIRE( order.size() == 3 );
    const auto position = [&]( const mod_id & id ) {
        return std::distance( order.begin(), std::find( order.begin(), order.end(), id ) );
    };
    CHECK( position( core ) < position( b ) );
    CHECK( position( b ) < position( a ) );
}

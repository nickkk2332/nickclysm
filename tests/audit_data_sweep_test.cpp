#include <sstream>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
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
#include "point.h"
#include "profession_group.h"
#include "submap.h"
#include "type_id.h"
#include "worldfactory.h"

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

#include <string>
#include <vector>

#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "mod_manager.h"
#include "type_id.h"
#include "worldfactory.h"

static const mod_id MOD_INFORMATION_dda( "dda" );
static const mod_id MOD_INFORMATION_test_data( "test_data" );

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

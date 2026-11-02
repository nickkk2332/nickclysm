#include <map>
#include <vector>

#include "cata_catch.h"
#include "item_pocket.h"
#include "itype.h"
#include "type_id.h"
#include "units.h"

static const ammotype ammo_rock( "rock" );

static const itype_id itype_audit_pocket_delete_extend( "audit_pocket_delete_extend" );
static const itype_id itype_audit_pocket_inherit( "audit_pocket_inherit" );
static const itype_id itype_audit_pocket_parent( "audit_pocket_parent" );
static const itype_id itype_audit_pocket_proportional( "audit_pocket_proportional" );
static const itype_id itype_audit_pocket_relative( "audit_pocket_relative" );

// The max_item_length a pocket gets when its JSON gives none.
static units::length default_item_length( const units::volume &volume )
{
    const units::length length = units::default_length_from_volume( volume ) * M_SQRT2;
    return length;
}

// INV-J02: "pocket_data" could only be replaced whole, so a copy-from item that changed one
// pocket had to restate every pocket of its parent.  The pocket loader now patches the
// inherited pockets by "id" or index with relative, proportional, delete and extend.  On the
// old code, data/mods/TEST_DATA/audit_pocket_overrides.json fails to load.
TEST_CASE( "inv-j02_copy_from_items_patch_inherited_pockets", "[audit][pocket]" )
{
    const std::vector<pocket_data> &parent = itype_audit_pocket_parent->pockets;
    REQUIRE( parent.size() == 4 );
    REQUIRE( parent[0].pocket_id == "main" );
    REQUIRE( parent[1].pocket_id.empty() );
    REQUIRE( parent[1].max_item_length == default_item_length( 500_ml ) );

    SECTION( "without a patch every pocket is inherited" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_inherit->pockets;
        REQUIRE( pockets.size() == 4 );
        for( size_t i = 0; i < pockets.size(); ++i ) {
            CHECK( pockets[i] == parent[i] );
            CHECK( pockets[i].pocket_id == parent[i].pocket_id );
        }
    }
    SECTION( "relative adds to the pocket named by id or index" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_relative->pockets;
        REQUIRE( pockets.size() == 4 );
        CHECK( pockets[0].raw_volume_capacity == 3_liter );
        CHECK( pockets[0].moves == 80 );
        // an explicit max_item_length is kept
        CHECK( pockets[0].max_item_length == 40_cm );
        CHECK( pockets[1].max_contains_weight == 1500_gram );
        CHECK( pockets[1].raw_volume_capacity == 500_ml );
        CHECK( pockets[1].moves == 80 );
        CHECK( pockets[2] == parent[2] );
        CHECK( pockets[3].ammo_restriction.at( ammo_rock ) == 15 );
    }
    SECTION( "proportional \"*\" scales every pocket the member applies to" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_proportional->pockets;
        REQUIRE( pockets.size() == 4 );
        CHECK( pockets[0].raw_volume_capacity == 4_liter );
        CHECK( pockets[0].max_contains_weight == 6_kilogram );
        CHECK( pockets[0].max_item_length == 40_cm );
        CHECK( pockets[1].raw_volume_capacity == 1_liter );
        CHECK( pockets[1].max_contains_weight == 1500_gram );
        // a defaulted max_item_length follows the new volume
        CHECK( pockets[1].max_item_length == default_item_length( 1_liter ) );
        CHECK( pockets[2].raw_volume_capacity == 2_liter );
        CHECK( pockets[2].max_item_length == default_item_length( 2_liter ) );
        CHECK( pockets[2].insulation == Approx( 2.0f ) );
        CHECK( pockets[0].insulation == Approx( 1.0f ) );
        // the ammo pocket takes the weight but its size still comes from the ammo
        CHECK( pockets[3].max_contains_weight == 7500_gram );
        CHECK( pockets[3].raw_volume_capacity == parent[3].raw_volume_capacity );
        CHECK( pockets[3].ammo_restriction.at( ammo_rock ) == 10 );
    }
    SECTION( "delete removes pockets by id and index, extend appends new ones" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_delete_extend->pockets;
        REQUIRE( pockets.size() == 3 );
        CHECK( pockets[0].pocket_id == "main" );
        CHECK( pockets[0] == parent[0] );
        CHECK( pockets[1].pocket_id == "rocks" );
        CHECK( pockets[2].pocket_id == "hidden" );
        CHECK( pockets[2].raw_volume_capacity == 250_ml );
        CHECK( pockets[2].max_contains_weight == 500_gram );
    }
}

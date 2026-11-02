#include <string>
#include <vector>

#include "bodypart.h"
#include "cata_catch.h"
#include "flag.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "type_id.h"

static const itype_id itype_briefcase_smg( "briefcase_smg" );
static const itype_id itype_colt_army( "colt_army" );

// ITEM-B03: Item_factory::finalize_post() returned right after giving complex firearms their
// gun repair tools, so everything after it never ran for guns: the per-bodypart armor data
// of an ARMOR+GUN item was never built, and NO_UNLOAD/NO_RELOAD never reached the pockets.
TEST_CASE( "item-b03_complex_guns_finish_item_finalization", "[audit][item]" )
{
    // The MP5 briefcase is worn on the arms; without the armor data it covers nothing.
    const item briefcase( itype_briefcase_smg );
    REQUIRE( briefcase.is_gun() );
    REQUIRE( briefcase.is_armor() );
    CHECK( briefcase.get_coverage( body_part_arm_l.id() ) > 0 );
    CHECK( !briefcase.type->armor->data.empty() );

    // The cap and ball revolver can't be unloaded; its cylinder must know that.
    REQUIRE( itype_colt_army->gun );
    REQUIRE( itype_colt_army->has_flag( flag_NO_UNLOAD ) );
    REQUIRE( !itype_colt_army->pockets.empty() );
    for( const pocket_data &pocket : itype_colt_army->pockets ) {
        CHECK( pocket._no_unload );
    }
}

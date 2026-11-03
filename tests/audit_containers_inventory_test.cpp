#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <list>
#include <optional>
#include <vector>

#include "active_item_cache.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "enums.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "itype.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "options_helpers.h"
#include "pickup.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_audit_inv_b02_filter_rack( "audit_inv_b02_filter_rack" );
static const itype_id itype_gasfilter_med( "gasfilter_med" );

namespace
{

// Empties a map tile when it goes out of scope, so a failed REQUIRE leaves nothing behind.
struct tile_items_cleanup {
    tripoint_bub_ms pos;
    ~tile_items_cleanup() {
        get_map().i_clear( pos );
    }
};

} // namespace

// INV-B02: both restack() overloads skipped an item that isn't counted by charges in a
// MAGAZINE pocket with a bare `continue`, without moving on.  Two such items in a magazine
// pocket with no ammo_restriction (Aftershock's UICAS helmet takes gas filters) hung the game
// on the next insertion or contents change.
TEST_CASE( "inv-b02_magazine_pocket_restacks_items_not_counted_by_charges", "[audit][pocket]" )
{
    item rack( itype_audit_inv_b02_filter_rack );
    const item filter( itype_gasfilter_med );
    REQUIRE_FALSE( filter.count_by_charges() );

    rack.force_insert_item( filter, pocket_type::MAGAZINE );
    // Before the fix, this never returned.
    rack.force_insert_item( filter, pocket_type::MAGAZINE );
    CHECK( rack.all_items_top( pocket_type::MAGAZINE ).size() == 2 );

    // The other overload, through a normal insertion
    REQUIRE( rack.put_in( filter, pocket_type::MAGAZINE ).success() );
    rack.on_contents_changed();
    CHECK( rack.all_items_top( pocket_type::MAGAZINE ).size() == 3 );
}

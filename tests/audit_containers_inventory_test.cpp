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
static const itype_id itype_audit_inv_b03_fuel_burner( "audit_inv_b03_fuel_burner" );
static const itype_id itype_gas_charger_on( "gas_charger_on" );
static const itype_id itype_gasfilter_med( "gasfilter_med" );
static const itype_id itype_propane( "propane" );
static const itype_id itype_small_propane_tank( "small_propane_tank" );

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

// INV-B03: a fuel-burning UPS burnt the fuel in its magazine well at one unit per kJ, without
// its fuel_efficiency (the gas-powered UPS burnt about 7.5 times too much propane).  With an
// integral fuel magazine it credited the burnt fuel's whole energy, ignoring the efficiency,
// and read the pocket's front item after the last of the fuel was gone.
TEST_CASE( "inv-b03_fuel_burning_ups_burns_at_its_efficiency", "[audit][item]" )
{
    const tripoint_bub_ms pos( 60, 60, 0 );
    const units::energy per_unit = item( itype_propane ).fuel_energy();
    REQUIRE( per_unit > 0_J );

    SECTION( "fuel in a magazine well" ) {
        item charger( itype_gas_charger_on );
        const float efficiency = charger.type->tool->fuel_efficiency;
        REQUIRE( efficiency > 0.0f );
        REQUIRE( efficiency < 1.0f );
        item tank( itype_small_propane_tank );
        tank.ammo_set( itype_propane, 100 );
        REQUIRE( charger.put_in( tank, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( charger.ammo_remaining() == 100 );

        const units::energy wanted = 75_kJ;
        const units::energy got = charger.energy_consume( wanted, pos, nullptr, efficiency );
        CHECK( got == wanted );
        const int expected_burnt = static_cast<int>( std::ceil( units::to_joule( wanted ) /
                                   ( units::to_joule( per_unit ) * efficiency ) ) );
        CHECK( 100 - charger.ammo_remaining() == expected_burnt );
    }

    SECTION( "fuel in an integral magazine" ) {
        item burner( itype_audit_inv_b03_fuel_burner );
        const float efficiency = burner.type->tool->fuel_efficiency;
        REQUIRE( efficiency > 0.0f );
        REQUIRE( efficiency < 1.0f );
        burner.ammo_set( itype_propane, 1 );
        REQUIRE( burner.ammo_remaining() == 1 );

        // Less than one unit gives: the first request burns the only unit, the second finds
        // none and can only draw on what the first left over.
        const double unit_kj = units::to_kilojoule( per_unit ) * efficiency;
        const units::energy request = units::from_kilojoule( std::max<std::int64_t>( 1,
                                      static_cast<std::int64_t>( unit_kj * 0.8 ) ) );
        units::energy got = burner.energy_consume( request, pos, nullptr, efficiency );
        CHECK( got == request );
        CHECK( burner.ammo_remaining() == 0 );
        got += burner.energy_consume( request, pos, nullptr, efficiency );
        CHECK( units::to_kilojoule( got ) <= unit_kj );
    }
}

#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "enums.h"
#include "game_constants.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "type_id.h"
#include "units.h"

static const flag_id json_flag_FROZEN( "FROZEN" );

static const itype_id itype_jerky( "jerky" );

// ITEM-B01: process_temperature_rot replays time spent outside the reality bubble in
// 1-hour chunks and skips the heat-flow simulation for chunks older than 2 days.  Those
// chunks still called calc_rot(), which returns early on the item's own FROZEN flag, and
// that flag still described the item when the player left.  Food frozen in winter and
// left in the warm for weeks therefore only rotted during the last 2 days.
TEST_CASE( "item-b01_frozen_food_left_out_of_bubble_rots", "[audit][item][rot]" )
{
    clear_map();
    if( calendar::turn <= calendar::start_of_cataclysm ) {
        calendar::turn = calendar::start_of_cataclysm + 1_minutes;
    }
    map &here = get_map();

    item frozen( itype_jerky );
    frozen.set_item_temperature( units::from_celsius( -20 ) );
    REQUIRE( frozen.has_own_flag( json_flag_FROZEN ) );

    item thawed( itype_jerky );
    thawed.set_item_temperature( temperatures::normal );
    REQUIRE_FALSE( thawed.has_own_flag( json_flag_FROZEN ) );

    REQUIRE( frozen.get_rot() == 0_turns );
    REQUIRE( thawed.get_rot() == 0_turns );

    // Both sit in a heated spot (never below 70 F) for 20 days without being processed,
    // as if the player had left the area.
    calendar::turn += 20_days;
    frozen.process_temperature_rot( 1, tripoint_bub_ms::zero, here, nullptr,
                                    temperature_flag::HEATER );
    thawed.process_temperature_rot( 1, tripoint_bub_ms::zero, here, nullptr,
                                    temperature_flag::HEATER );

    // At 70 F or more food rots at least 1.2 hours per hour.
    REQUIRE( thawed.get_rot() > 20_days );
    // The frozen jerky thaws in the first warm hour, so it must rot about as much.  It used
    // to stay "frozen" for the first 18 days and rot only during the last 2.
    CHECK( to_turns<double>( frozen.get_rot() ) >= 0.9 * to_turns<double>( thawed.get_rot() ) );
    CHECK_FALSE( frozen.has_own_flag( json_flag_FROZEN ) );
}

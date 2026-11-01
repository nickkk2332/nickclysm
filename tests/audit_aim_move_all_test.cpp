#include <memory>
#include <string>

#include "advanced_inv.h"
#include "advanced_inv_area.h"
#include "advanced_inv_pane.h"
#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "uistate.h"

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_knife_combat( "knife_combat" );

static void recalc_both_panes( advanced_inventory &advinv )
{
    advinv.recalc_pane( advanced_inventory::side::left );
    advinv.recalc_pane( advanced_inventory::side::right );
}

// Leaves the source pane on the tile below the player and the destination pane
// inside the container lying on the tile north of the player.
static void open_north_container_as_destination( advanced_inventory &advinv )
{
    // Destination pane: the north tile.
    advinv.process_action( "ITEMS_N" );
    REQUIRE( advinv.get_pane( advinv.get_src() ).get_area() == AIM_NORTH );
    // Source pane: the tile below the player.
    advinv.process_action( "TOGGLE_TAB" );
    advinv.process_action( "ITEMS_CE" );
    REQUIRE( advinv.get_pane( advinv.get_src() ).get_area() == AIM_CENTER );
    recalc_both_panes( advinv );

    // Enter the container on the north tile, then make the other pane active again.
    advinv.process_action( "TOGGLE_TAB" );
    REQUIRE( advinv.get_pane( advinv.get_src() ).get_cur_item_ptr() != nullptr );
    advinv.process_action( "ITEMS_CONTAINER" );
    advinv.process_action( "TOGGLE_TAB" );
    recalc_both_panes( advinv );

    advanced_inventory_pane &dpane = advinv.get_pane( advinv.get_dest() );
    REQUIRE( advinv.get_pane( advinv.get_src() ).get_area() == AIM_CENTER );
    REQUIRE( dpane.get_area() == AIM_CONTAINER );
    REQUIRE( dpane.container );
    REQUIRE( dpane.container->typeId() == itype_backpack );
}

// Move All used to dereference the destination pane's container before checking that it
// still existed, crashing when the pane showed "Invalid container!".
TEST_CASE( "uiinv-b01_move_all_into_invalid_container_does_not_crash", "[audit]" )
{
    // AIM keeps its layout in the global uistate, start from the defaults and restore afterwards.
    restore_on_out_of_scope<advanced_inv_save_state> restore_aim_state( uistate.transfer_save );
    uistate.transfer_save = advanced_inv_save_state();
    // Reopening AIM must restore the last layout, not the default one.
    override_option last_layout( "OPEN_DEFAULT_ADV_INV", "false" );

    avatar &u = get_avatar();
    clear_avatar();
    clear_map_without_vision();
    map &here = get_map();
    const tripoint_bub_ms start( 60, 60, 0 );
    u.setpos( here, start );
    const tripoint_bub_ms container_pos = start + tripoint_rel_ms::north;
    here.add_item( container_pos, item( itype_backpack ) );
    here.add_item( start, item( itype_knife_combat ) );

    std::unique_ptr<advanced_inventory> advinv = std::make_unique<advanced_inventory>();
    advinv->init();
    open_north_container_as_destination( *advinv );

    SECTION( "the destination container is destroyed" ) {
        here.i_clear( container_pos );
    }
    SECTION( "the player walks away from the container and reopens AIM" ) {
        // Closing AIM saves the layout, including the destination container.
        advinv.reset();
        const tripoint_bub_ms new_pos = start + tripoint_rel_ms( 0, 2, 0 );
        u.setpos( here, new_pos );
        REQUIRE( u.pos_bub() == new_pos );
        here.add_item( new_pos, item( itype_knife_combat ) );
        advinv = std::make_unique<advanced_inventory>();
        advinv->init();
    }

    recalc_both_panes( *advinv );
    const advanced_inventory_pane &spane = advinv->get_pane( advinv->get_src() );
    const advanced_inventory_pane &dpane = advinv->get_pane( advinv->get_dest() );
    REQUIRE( spane.get_area() == AIM_CENTER );
    REQUIRE_FALSE( spane.items.empty() );
    // The pane is still a container view, but its container is gone.
    REQUIRE( dpane.get_area() == AIM_CONTAINER );
    REQUIRE_FALSE( dpane.container );

    advinv->process_action( "MOVE_ALL_ITEMS" );

    // Nothing is moved and no activity is started.
    CHECK_FALSE( u.activity );
    CHECK( here.i_at( u.pos_bub() ).size() == 1 );
}

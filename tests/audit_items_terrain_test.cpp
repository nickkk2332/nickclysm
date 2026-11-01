#include <string>

#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "enums.h"
#include "game_constants.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "map.h"
#include "map_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"

#if defined(LOCALIZE)
#include "translation_manager.h"
#include "translations.h"
#endif

static const flag_id json_flag_FROZEN( "FROZEN" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_9mmfmj( "9mmfmj" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_glocksear( "glocksear" );
static const itype_id itype_jerky( "jerky" );
static const itype_id itype_test_multimag_capacity_gunmod( "test_multimag_capacity_gunmod" );
static const itype_id itype_test_multimag_gun_integral_ammo( "test_multimag_gun_integral_ammo" );

static int count_items_at( map &here, const tripoint_bub_ms &pos )
{
    int total = 0;
    for( const item &it : here.i_at( pos ) ) {
        total += it.count();
    }
    return total;
}

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

#if defined(LOCALIZE)
// IINF-B02: is_gunmod_compatible compared the *translated* slot name with the English
// strings "magazine", "mechanism", "loading port", "bore" and "stock accessory".  In any
// translated game the "must be unloaded" guard never matched, so a mod that rebuilds the
// magazine well could be installed on a loaded gun and delete the magazine and its ammo.
TEST_CASE( "iinf-b02_gunmod_unload_check_ignores_language", "[audit][item][gunmod]" )
{
    const std::string must_unload = "must be unloaded before installing this mod";

    item gun( itype_glock_19 );
    item sear( itype_glocksear );
    REQUIRE( sear.type->gunmod->location.str() == "mechanism" );
    REQUIRE( gun.is_gunmod_compatible( sear ).success() );

    item mag( itype_glockmag );
    mag.ammo_set( mag.ammo_default() );
    REQUIRE( gun.put_in( mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.magazine_current() != nullptr );

    // English: the loaded gun refuses the mechanism mod.
    ret_val<void> english = gun.is_gunmod_compatible( sear );
    REQUIRE_FALSE( english.success() );
    REQUIRE( english.str() == must_unload );

    on_out_of_scope reset_language( []() {
        set_language( "en" );
    } );
    set_language( "ru" );
    TranslationManager::GetInstance().LoadDocuments( {
        "./data/mods/TEST_DATA/lang/mo/ru/LC_MESSAGES/TEST_DATA.mo"
    } );
    // TEST_DATA translates the "mechanism" slot name, so name() no longer returns the id.
    REQUIRE( sear.type->gunmod->location.name() != "mechanism" );

    ret_val<void> translated = gun.is_gunmod_compatible( sear );
    CHECK_FALSE( translated.success() );
    CHECK( translated.str() == must_unload );
}
#endif

// INV-B01: the ammo-restricted branch of item_pocket::overflow cut the excess off the
// stack but then moved the already-reduced stack out instead of the excess, so ammo was
// duplicated or destroyed.  It also compared against the raw ammo_restriction count and
// the raw derived volume, ignoring capacity_mods, so a boosted pocket dumped its ammo.
TEST_CASE( "inv-b01_ammo_pocket_overflow_moves_the_excess", "[audit][inventory]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos( 60, 60, 0 );
    REQUIRE( count_items_at( here, pos ) == 0 );

    SECTION( "overfilled magazine drops exactly the excess" ) {
        item mag( itype_glockmag );  // holds 15 rounds of 9mm
        REQUIRE( mag.ammo_remaining() == 0 );
        // Two different 9mm types, so they stay separate stacks: 14 + 10 = 24 rounds.
        mag.force_insert_item( item( itype_9mm, calendar::turn, 14 ), pocket_type::MAGAZINE );
        mag.force_insert_item( item( itype_9mmfmj, calendar::turn, 10 ), pocket_type::MAGAZINE );
        REQUIRE( mag.ammo_remaining() == 24 );

        mag.overflow( here, pos );

        CHECK( mag.ammo_remaining() == 15 );
        // Used to drop the 1 round left in the second stack, so 8 rounds vanished.
        CHECK( count_items_at( here, pos ) == 9 );
    }

    SECTION( "capacity_mods-boosted pocket keeps its boosted load" ) {
        item gun( itype_test_multimag_gun_integral_ammo );
        REQUIRE( gun.put_in( item( itype_test_multimag_capacity_gunmod ),
                             pocket_type::MOD ).success() );
        item_pocket *ammo_p = gun.pocket_by_id( "ammo" );
        REQUIRE( ammo_p != nullptr );
        const ammotype at = *ammo_p->ammo_types().begin();
        REQUIRE( ammo_p->ammo_capacity( at ) == 2 );
        REQUIRE( ammo_p->insert_item( item( itype_9mm, calendar::turn, 2 ) ).success() );
        REQUIRE( gun.ammo_remaining_in_pocket( "ammo" ) == 2 );

        gun.overflow( here, pos );

        // Used to fail the unscaled volume check and drop the whole stack.
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 2 );
        CHECK( count_items_at( here, pos ) == 0 );
    }
}

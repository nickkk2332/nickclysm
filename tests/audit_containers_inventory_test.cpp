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
#include "cata_scope_helpers.h"
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
#include "weather.h"

static const itype_id itype_audit_inv_b02_filter_rack( "audit_inv_b02_filter_rack" );
static const itype_id itype_audit_inv_b03_fuel_burner( "audit_inv_b03_fuel_burner" );
static const itype_id itype_audit_inv_b04_brick( "audit_inv_b04_brick" );
static const itype_id itype_audit_inv_b04_sack( "audit_inv_b04_sack" );
static const itype_id itype_audit_inv_b05_cold_bag( "audit_inv_b05_cold_bag" );
static const itype_id itype_audit_inv_b05_cool_box( "audit_inv_b05_cool_box" );
static const itype_id itype_audit_inv_b05_rotting_bag( "audit_inv_b05_rotting_bag" );
static const itype_id itype_audit_inv_b05_two_pocket_rig( "audit_inv_b05_two_pocket_rig" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_can_medium( "can_medium" );
static const itype_id itype_can_tuna( "can_tuna" );
static const itype_id itype_gas_charger_on( "gas_charger_on" );
static const itype_id itype_gasfilter_med( "gasfilter_med" );
static const itype_id itype_meat_canned( "meat_canned" );
static const itype_id itype_pebble( "pebble" );
static const itype_id itype_propane( "propane" );
static const itype_id itype_small_propane_tank( "small_propane_tank" );
static const itype_id itype_test_apple( "test_apple" );
static const itype_id itype_water_clean( "water_clean" );
static const itype_id itype_waterskin( "waterskin" );

static const quality_id qual_TEST_RESERVE_A( "TEST_RESERVE_A" );

static const trait_id trait_TEST_RESERVE_QUALITIES( "TEST_RESERVE_QUALITIES" );
static const trait_id trait_TEST_RESERVE_QUALITIES_2( "TEST_RESERVE_QUALITIES_2" );

namespace
{

// Empties a map tile when it goes out of scope, so a failed REQUIRE leaves nothing behind.
struct tile_items_cleanup {
    tripoint_bub_ms pos;
    ~tile_items_cleanup() {
        get_map().i_clear( pos );
    }
};

// Resets the avatar when it goes out of scope.
struct avatar_cleanup {
    ~avatar_cleanup() {
        clear_avatar();
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

    SECTION( "a small draw uses what is left over before burning more" ) {
        item burner( itype_audit_inv_b03_fuel_burner );
        const float efficiency = burner.type->tool->fuel_efficiency;
        burner.ammo_set( itype_propane, 2 );
        REQUIRE( burner.ammo_remaining() == 2 );
        const double unit_kj = units::to_kilojoule( per_unit ) * efficiency;
        // two draws that one unit covers
        const units::energy request = units::from_kilojoule( std::max<std::int64_t>( 1,
                                      static_cast<std::int64_t>( unit_kj * 0.4 ) ) );
        CHECK( burner.energy_consume( request, pos, nullptr, efficiency ) == request );
        CHECK( burner.ammo_remaining() == 1 );
        CHECK( burner.energy_consume( request, pos, nullptr, efficiency ) == request );
        CHECK( burner.ammo_remaining() == 1 );
    }

    SECTION( "a carried gas-powered UPS offers its fuel's energy" ) {
        clear_avatar();
        const avatar_cleanup reset_avatar;
        avatar &they = get_avatar();
        REQUIRE( they.wear_item( item( itype_backpack ), false ).has_value() );
        item charger( itype_gas_charger_on );
        const float efficiency = charger.type->tool->fuel_efficiency;
        item tank( itype_small_propane_tank );
        tank.ammo_set( itype_propane, 10 );
        REQUIRE( charger.put_in( tank, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( they.i_add( charger ) );
        REQUIRE( they.available_ups() > 0_J );

        // more than one kJ for each of its ten units of propane
        const double fuel_kj = units::to_kilojoule( per_unit ) * efficiency * 10;
        REQUIRE( fuel_kj > 20 );
        CHECK( units::to_kilojoule( they.available_ups() ) == Approx( fuel_kj ).margin( 1 ) );
        const units::energy wanted = 20_kJ;
        CHECK( they.consume_ups( wanted ) == wanted );
    }
}

// INV-B04: max_charges_by_parent_recursive() walked up the container chain but measured the
// innermost parent pocket at every level, so a nearly full outer container never limited what
// went into a soft container nested two levels down, and the next overflow() dropped items.
TEST_CASE( "inv-b04_parent_charge_limit_checks_every_parent_pocket", "[audit][item][pocket]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos( 60, 60, 0 );
    const tile_items_cleanup cleanup{ pos };

    // outer sack: a brick and the middle sack; middle sack: the (empty) inner sack
    item middle_proto( itype_audit_inv_b04_sack );
    REQUIRE( middle_proto.put_in( item( itype_audit_inv_b04_sack ),
                                  pocket_type::CONTAINER ).success() );
    item outer_proto( itype_audit_inv_b04_sack );
    REQUIRE( outer_proto.put_in( item( itype_audit_inv_b04_brick ),
                                 pocket_type::CONTAINER ).success() );
    REQUIRE( outer_proto.put_in( middle_proto, pocket_type::CONTAINER ).success() );

    item &outer = here.add_item( pos, outer_proto );
    const item_location outer_loc( map_cursor( pos ), &outer );
    item *middle = nullptr;
    for( item *it : outer.all_items_top() ) {
        if( it->typeId() == itype_audit_inv_b04_sack ) {
            middle = it;
        }
    }
    REQUIRE( middle != nullptr );
    const item_location middle_loc( outer_loc, middle );
    REQUIRE( middle->all_items_top().size() == 1 );
    const item_location inner_loc( middle_loc, middle->all_items_top().front() );

    // Room left in the outer sack's pocket, and in the middle sack's
    const units::volume outer_room = middle_loc.volume_capacity();
    const units::volume middle_room = inner_loc.volume_capacity();
    const item pebble( itype_pebble );
    REQUIRE( pebble.count_by_charges() );
    REQUIRE( pebble.charges_per_volume( outer_room, true ) <
             pebble.charges_per_volume( middle_room, true ) );

    const ret_val<int> limit = inner_loc.max_charges_by_parent_recursive( pebble );
    CHECK( limit.value() <= pebble.charges_per_volume( outer_room, true ) );
}

static const item_reference *find_apple_reference( const std::vector<item_reference> &refs )
{
    for( const item_reference &ref : refs ) {
        if( ref.item_ref && ref.item_ref->typeId() == itype_test_apple ) {
            return &ref;
        }
    }
    return nullptr;
}

// INV-B05: active_item_cache::add() appended every sibling pocket to one shared chain, so an
// item in a container's second pocket carried the first pocket too, and items on the map or in
// a vehicle took its spoil multiplier and insulation.  Nested pockets also multiplied their
// spoil multipliers, where a carried item gets the best (lowest) one.
TEST_CASE( "inv-b05_active_item_pocket_chain_holds_only_enclosing_pockets",
           "[audit][item][pocket]" )
{
    SECTION( "a sibling pocket is not in the chain" ) {
        item rig( itype_audit_inv_b05_two_pocket_rig );
        const std::vector<item_pocket *> pockets = rig.get_standard_pockets();
        REQUIRE( pockets.size() == 2 );
        REQUIRE( pockets[0]->spoil_multiplier() < 1.0f );
        REQUIRE( pockets[1]->spoil_multiplier() == 1.0f );
        // the first pocket only takes gas filters, so the apple goes in the second
        REQUIRE( rig.put_in( item( itype_test_apple ), pocket_type::CONTAINER ).success() );
        REQUIRE( pockets[0]->empty() );
        REQUIRE( pockets[1]->size() == 1 );

        active_item_cache cache;
        REQUIRE( cache.add( rig, point_rel_ms::zero ) );
        const std::vector<item_reference> refs = cache.get();
        const item_reference *apple = find_apple_reference( refs );
        REQUIRE( apple != nullptr );
        CHECK( apple->pocket_chain.size() == 1 );
        CHECK( apple->spoil_multiplier() == Approx( 1.0f ) );
    }

    SECTION( "nested pockets give the best spoil multiplier, as carried items get" ) {
        item box( itype_audit_inv_b05_cool_box );
        REQUIRE( box.put_in( item( itype_test_apple ), pocket_type::CONTAINER ).success() );
        item bag( itype_audit_inv_b05_cold_bag );
        REQUIRE( bag.put_in( box, pocket_type::CONTAINER ).success() );
        const float bag_mult = bag.get_standard_pockets().front()->spoil_multiplier();
        const float box_mult = box.get_standard_pockets().front()->spoil_multiplier();
        REQUIRE( bag_mult < box_mult );
        REQUIRE( box_mult < 1.0f );

        active_item_cache cache;
        REQUIRE( cache.add( bag, point_rel_ms::zero ) );
        const std::vector<item_reference> refs = cache.get();
        const item_reference *apple = find_apple_reference( refs );
        REQUIRE( apple != nullptr );
        CHECK( apple->pocket_chain.size() == 2 );
        CHECK( apple->spoil_multiplier() == Approx( bag_mult ) );
    }

    SECTION( "a pocket above 1 hastens rot when no pocket preserves" ) {
        item bag( itype_audit_inv_b05_rotting_bag );
        REQUIRE( bag.get_standard_pockets().front()->spoil_multiplier() == Approx( 1.5f ) );
        REQUIRE( bag.put_in( item( itype_test_apple ), pocket_type::CONTAINER ).success() );
        active_item_cache cache;
        REQUIRE( cache.add( bag, point_rel_ms::zero ) );
        const std::vector<item_reference> refs = cache.get();
        const item_reference *apple = find_apple_reference( refs );
        REQUIRE( apple != nullptr );
        CHECK( apple->spoil_multiplier() == Approx( 1.5f ) );
    }

    SECTION( "a pocket that preserves wins over one that hastens rot" ) {
        item box( itype_audit_inv_b05_cool_box );
        REQUIRE( box.put_in( item( itype_test_apple ), pocket_type::CONTAINER ).success() );
        item bag( itype_audit_inv_b05_rotting_bag );
        REQUIRE( bag.put_in( box, pocket_type::CONTAINER ).success() );
        const float box_mult = box.get_standard_pockets().front()->spoil_multiplier();
        active_item_cache cache;
        REQUIRE( cache.add( bag, point_rel_ms::zero ) );
        const std::vector<item_reference> refs = cache.get();
        const item_reference *apple = find_apple_reference( refs );
        REQUIRE( apple != nullptr );
        CHECK( apple->spoil_multiplier() == Approx( box_mult ) );
    }

    SECTION( "the same rule holds for a carried item" ) {
        CHECK( combine_spoil_multipliers( 1.0f, 1.5f ) == Approx( 1.5f ) );
        CHECK( combine_spoil_multipliers( 1.5f, 0.8f ) == Approx( 0.8f ) );
        CHECK( combine_spoil_multipliers( 0.8f, 1.5f ) == Approx( 0.8f ) );
        CHECK( combine_spoil_multipliers( 0.5f, 0.8f ) == Approx( 0.5f ) );

        // an apple in the rotting bag rots half again as fast as one in a plain sack
        restore_on_out_of_scope restore_turn( calendar::turn );
        restore_on_out_of_scope restore_temperature( get_weather().forced_temperature );
        get_weather().forced_temperature = units::from_celsius( 20 );
        if( calendar::turn <= calendar::start_of_cataclysm ) {
            calendar::turn = calendar::start_of_cataclysm + 1_minutes;
        }
        item fast( itype_audit_inv_b05_rotting_bag );
        item plain( itype_audit_inv_b04_sack );
        REQUIRE( plain.get_standard_pockets().front()->spoil_multiplier() == 1.0f );
        const std::vector<item *> bags = { &fast, &plain };
        for( item *bag : bags ) {
            REQUIRE( bag->put_in( item( itype_test_apple ), pocket_type::CONTAINER ).success() );
            bag->process( get_map(), nullptr, tripoint_bub_ms::zero );
        }
        calendar::turn += 2_hours;
        for( item *bag : bags ) {
            bag->process( get_map(), nullptr, tripoint_bub_ms::zero );
        }
        const time_duration plain_rot = plain.all_items_top().front()->get_rot();
        REQUIRE( plain_rot > 0_turns );
        CHECK( to_turns<double>( fast.all_items_top().front()->get_rot() ) ==
               Approx( 1.5 * to_turns<double>( plain_rot ) ).epsilon( 0.02 ) );
    }
}

namespace
{

// Clears the avatar's auto pickup rules when it goes out of scope.
struct autopickup_rules_cleanup {
    ~autopickup_rules_cleanup() {
        auto_pickup::player_settings &rules = get_auto_pickup();
        rules.clear_character_rules();
        // rebuilds the rule cache
        rules.check_item( "" );
    }
};

} // namespace

static void add_autopickup_rule( const item &what, bool include )
{
    auto_pickup::player_settings &rules = get_auto_pickup();
    rules.add_rule( &what, include );
    REQUIRE( rules.check_item( what ) ==
             ( include ? rule_state::WHITELISTED : rule_state::BLACKLISTED ) );
}

// INV-B06: auto pickup turned a whitelisted container out (everything not whitelisted from a
// soft one, liquids included) while selecting it, before the auto pickup limits and the check
// that the avatar could carry and stash it.  It ignored seals, poured out liquids, and could
// leave the emptied container behind.
TEST_CASE( "inv-b06_autopickup_leaves_containers_whole_unless_it_takes_them",
           "[audit][autopickup]" )
{
    clear_avatar();
    clear_map();
    const avatar_cleanup reset_avatar;
    const autopickup_rules_cleanup reset_rules;
    const override_option auto_pickup( "AUTO_PICKUP", "true" );
    const override_option adjacent( "AUTO_PICKUP_ADJACENT", "false" );
    const override_option owned( "AUTO_PICKUP_OWNED", "false" );
    const override_option weight_limit( "AUTO_PICKUP_WEIGHT_LIMIT", "0" );

    avatar &they = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms ground = they.pos_bub();
    const tile_items_cleanup cleanup{ ground };

    SECTION( "liquid stays in a whitelisted soft container, which is taken with it" ) {
        const override_option volume_limit( "AUTO_PICKUP_VOLUME_LIMIT", "0" );
        REQUIRE( they.wear_item( item( itype_backpack ), false ).has_value() );
        item skin( itype_waterskin );
        REQUIRE( skin.fill_with( item( itype_water_clean ), 4 ) == 4 );
        const item &placed = here.add_item( ground, skin );
        add_autopickup_rule( placed, true );

        Pickup::autopickup( ground );
        process_activity( they );

        CHECK( here.i_at( ground ).empty() );
        CHECK( they.charges_of( itype_water_clean ) == 4 );
    }

    SECTION( "a whitelisted sealed container is taken whole and stays sealed" ) {
        const override_option volume_limit( "AUTO_PICKUP_VOLUME_LIMIT", "0" );
        REQUIRE( they.wear_item( item( itype_backpack ), false ).has_value() );
        const item tuna( itype_can_tuna );
        item can( itype_can_medium );
        can.force_insert_item( tuna, pocket_type::CONTAINER );
        can.force_insert_item( item( itype_meat_canned ), pocket_type::CONTAINER );
        can.seal();
        REQUIRE( can.all_pockets_sealed() );
        const item &placed = here.add_item( ground, can );
        add_autopickup_rule( placed, true );
        add_autopickup_rule( tuna, false );

        Pickup::autopickup( ground );
        process_activity( they );

        CHECK( here.i_at( ground ).empty() );
        const std::vector<item *> cans = they.items_with( []( const item & it ) {
            return it.typeId() == itype_can_medium;
        } );
        REQUIRE( cans.size() == 1 );
        CHECK( cans.front()->all_pockets_sealed() );
        CHECK( cans.front()->all_items_top().size() == 2 );
    }

    SECTION( "a container over the auto pickup limits is left as it was" ) {
        // 50 ml: the empty sack alone is bigger
        const override_option volume_limit( "AUTO_PICKUP_VOLUME_LIMIT", "1" );
        REQUIRE( they.wear_item( item( itype_backpack ), false ).has_value() );
        item sack( itype_audit_inv_b04_sack );
        REQUIRE( sack.put_in( item( itype_pebble, calendar::turn, 1 ),
                              pocket_type::CONTAINER ).success() );
        const item &placed = here.add_item( ground, sack );
        REQUIRE( placed.volume() > 50_ml );
        add_autopickup_rule( placed, true );

        Pickup::autopickup( ground );
        process_activity( they );

        REQUIRE( here.i_at( ground ).size() == 1 );
        const item &left = *here.i_at( ground ).begin();
        CHECK( left.typeId() == itype_audit_inv_b04_sack );
        CHECK( left.all_items_top().size() == 1 );
    }

    SECTION( "a container the avatar can't stash is left as it was" ) {
        const override_option volume_limit( "AUTO_PICKUP_VOLUME_LIMIT", "0" );
        // no worn containers: nothing can be stashed
        item sack( itype_audit_inv_b04_sack );
        REQUIRE( sack.put_in( item( itype_pebble, calendar::turn, 1 ),
                              pocket_type::CONTAINER ).success() );
        const item &placed = here.add_item( ground, sack );
        REQUIRE_FALSE( they.can_stash_partial( placed, false ) );
        add_autopickup_rule( placed, true );

        Pickup::autopickup( ground );
        process_activity( they );

        REQUIRE( here.i_at( ground ).size() == 1 );
        const item &left = *here.i_at( ground ).begin();
        CHECK( left.typeId() == itype_audit_inv_b04_sack );
        CHECK( left.all_items_top().size() == 1 );
    }
}

// INV-B08: has_quality() and has_unreserved_quality() answered true for any mutation with the
// quality, whatever level and count were asked for.
TEST_CASE( "inv-b08_mutation_qualities_count_their_level_and_number", "[audit][inventory]" )
{
    clear_avatar();
    const avatar_cleanup reset_avatar;
    avatar &they = get_avatar();
    REQUIRE_FALSE( they.has_quality( qual_TEST_RESERVE_A ) );

    // TEST_RESERVE_A at level 1
    they.set_mutation( trait_TEST_RESERVE_QUALITIES );
    REQUIRE( they.has_trait( trait_TEST_RESERVE_QUALITIES ) );
    CHECK( they.has_quality( qual_TEST_RESERVE_A, 1 ) );
    CHECK( they.has_unreserved_quality( qual_TEST_RESERVE_A, 1 ) );
    CHECK_FALSE( they.has_quality( qual_TEST_RESERVE_A, 2 ) );
    CHECK_FALSE( they.has_unreserved_quality( qual_TEST_RESERVE_A, 2 ) );
    CHECK_FALSE( they.has_quality( qual_TEST_RESERVE_A, 1, 2 ) );
    CHECK_FALSE( they.has_unreserved_quality( qual_TEST_RESERVE_A, 1, 2 ) );

    // a second source of it
    they.set_mutation( trait_TEST_RESERVE_QUALITIES_2 );
    REQUIRE( they.has_trait( trait_TEST_RESERVE_QUALITIES_2 ) );
    CHECK( they.has_quality( qual_TEST_RESERVE_A, 1, 2 ) );
    CHECK( they.has_unreserved_quality( qual_TEST_RESERVE_A, 1, 2 ) );
    CHECK_FALSE( they.has_quality( qual_TEST_RESERVE_A, 1, 3 ) );
    CHECK_FALSE( they.has_unreserved_quality( qual_TEST_RESERVE_A, 1, 3 ) );
}

// INV-B09: better_pocket() returned the spoil multiplier comparison for any perishable item,
// even when both pockets had the same multiplier (the usual 1.0 and 1.0), so the later
// tie-breakers never ran for food: watertightness, encumbrance, ripoff, rigidity, room.
TEST_CASE( "inv-b09_equal_spoil_multipliers_fall_through_to_other_tie_breakers",
           "[audit][pocket]" )
{
    pocket_data data_a( pocket_type::CONTAINER );
    pocket_data data_b( pocket_type::CONTAINER );
    const item apple( itype_test_apple );
    REQUIRE( apple.is_comestible() );
    REQUIRE( apple.get_comestible()->spoils != 0_seconds );
    REQUIRE( apple.made_of( phase_id::SOLID ) );
    REQUIRE( data_a.spoil_multiplier == data_b.spoil_multiplier );

    // a solid is better kept out of a watertight pocket
    data_a.watertight = true;
    data_b.watertight = false;
    CHECK( item_pocket( &data_a ).better_pocket( item_pocket( &data_b ), apple ) );
    CHECK_FALSE( item_pocket( &data_b ).better_pocket( item_pocket( &data_a ), apple ) );

    // a lower spoil multiplier still comes first
    data_a.spoil_multiplier = 0.5f;
    CHECK_FALSE( item_pocket( &data_a ).better_pocket( item_pocket( &data_b ), apple ) );
    CHECK( item_pocket( &data_b ).better_pocket( item_pocket( &data_a ), apple ) );
}

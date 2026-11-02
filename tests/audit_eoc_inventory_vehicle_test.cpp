#include <list>
#include <memory>
#include <optional>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "player_helpers.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_apple( "apple" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bag_canvas( "bag_canvas" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_duffelbag( "duffelbag" );
static const itype_id itype_lighter( "lighter" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_water_clean( "water_clean" );

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition;
}

static double eval_math( dialogue &d, const std::string &expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

static item &wear_backpack( Character &you )
{
    std::optional<std::list<item>::iterator> worn = you.wear_item( item( itype_backpack ), false );
    REQUIRE( worn.has_value() );
    return **worn;
}

// The single item of the given type in the container pockets of a container.
static item &content_of( item &container, const itype_id &id )
{
    for( item *content : container.all_items_top( pocket_type::CONTAINER ) ) {
        if( content->typeId() == id ) {
            return *content;
        }
    }
    FAIL( "no " << id.str() << " in " << container.typeId().str() );
    return container;
}

// INV-J03: no math function exposed the carried weight, the carrying capacity or the free room
// in a character's pockets, so an EOC could not check whether the player can take a reward.
TEST_CASE( "inv-j03_math_reads_character_load_and_capacity", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    wear_backpack( u );
    // wearing does not refresh the carried weight cache; that is not what is tested here
    u.invalidate_weight_carried_cache();
    dialogue d( get_talker_for( u ), nullptr );

    const double carried_before = eval_math( d, "u_weight_carried()" );
    const double free_before = eval_math( d, "u_free_volume()" );
    const item rock( itype_rock );
    REQUIRE( u.i_add( rock ) );
    u.invalidate_weight_carried_cache();

    CHECK( eval_math( d, "u_weight_carried()" ) - carried_before ==
           Approx( units::to_milligram( rock.weight() ) ) );
    CHECK( eval_math( d, "u_weight_carried()" ) ==
           Approx( units::to_milligram( u.weight_carried() ) ) );
    CHECK( eval_math( d, "u_weight_capacity()" ) ==
           Approx( units::to_milligram( u.weight_capacity() ) ) );
    CHECK( eval_math( d, "u_weight_capacity()" ) > 0 );
    CHECK( free_before - eval_math( d, "u_free_volume()" ) ==
           Approx( units::to_milliliter( rock.volume() ) ) );
    CHECK( eval_math( d, "u_volume_capacity()" ) ==
           Approx( units::to_milliliter( u.volume_capacity() ) ) );
    CHECK( eval_math( d, "u_volume_capacity()" ) >= 25000 );
    CHECK( eval_math( d, "u_volume_carried()" ) ==
           Approx( units::to_milliliter( u.volume_carried() ) ) );
}

// INV-J03: an item talker could not report what its pockets hold or how much room is left.
TEST_CASE( "inv-j03_math_reads_container_contents", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    item &pack = wear_backpack( u );
    item_location pack_loc( u, &pack );
    dialogue d( get_talker_for( u ), get_talker_for( pack_loc ) );

    const double capacity = eval_math( d, "n_volume_capacity()" );
    CHECK( capacity == Approx( units::to_milliliter( pack.get_volume_capacity() ) ) );
    CHECK( eval_math( d, "n_contents_count()" ) == 0 );
    CHECK( eval_math( d, "n_contents_volume()" ) == 0 );
    CHECK( eval_math( d, "n_contents_weight()" ) == 0 );
    CHECK( eval_math( d, "n_remaining_volume()" ) == Approx( capacity ) );
    const double weight_capacity = eval_math( d, "n_weight_capacity()" );
    CHECK( weight_capacity == Approx( units::to_milligram( pack.get_total_weight_capacity() ) ) );
    CHECK( eval_math( d, "n_remaining_weight()" ) == Approx( weight_capacity ) );

    const item rock( itype_rock );
    const item ammo( itype_9mm, calendar::turn, 10 );
    for( int i = 0; i < 3; ++i ) {
        REQUIRE( pack.put_in( rock, pocket_type::CONTAINER ).success() );
    }
    REQUIRE( pack.put_in( ammo, pocket_type::CONTAINER ).success() );
    const units::volume stored_volume = rock.volume() * 3 + ammo.volume();
    const units::mass stored_weight = rock.weight() * 3 + ammo.weight();

    // a stack counted by charges counts each charge
    CHECK( eval_math( d, "n_contents_count()" ) == 13 );
    CHECK( eval_math( d, "n_contents_volume()" ) ==
           Approx( units::to_milliliter( stored_volume ) ) );
    CHECK( eval_math( d, "n_contents_weight()" ) ==
           Approx( units::to_milligram( stored_weight ) ) );
    CHECK( eval_math( d, "n_remaining_volume()" ) ==
           Approx( capacity - units::to_milliliter( stored_volume ) ) );
    CHECK( eval_math( d, "n_remaining_weight()" ) ==
           Approx( weight_capacity - units::to_milligram( stored_weight ) ) );
    // the same name on a character reads the character's pockets instead
    CHECK( eval_math( d, "u_volume_capacity()" ) ==
           Approx( units::to_milliliter( u.volume_capacity() ) ) );
}

// INV-J03: there were no conditions for container state or containment, and has_ammo and
// is_rotten only looked at the beta talker.
TEST_CASE( "inv-j03_container_conditions", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    item &pack = wear_backpack( u );
    item_location pack_loc( u, &pack );

    const conditional_t::func u_is_container = load_condition(
                R"({ "condition": "u_is_container" })" );
    const conditional_t::func npc_is_container = load_condition(
                R"({ "condition": "npc_is_container" })" );
    const conditional_t::func npc_is_container_empty = load_condition(
                R"({ "condition": "npc_is_container_empty" })" );
    const conditional_t::func npc_is_sealed = load_condition(
                R"({ "condition": "npc_is_sealed" })" );
    const conditional_t::func npc_contains_rock = load_condition(
                R"({ "condition": { "npc_contains_item": "rock" } })" );
    const conditional_t::func npc_contains_bag = load_condition(
                R"({ "condition": { "npc_contains_item": "bag_canvas" } })" );
    const conditional_t::func npc_contains_backpack = load_condition(
                R"({ "condition": { "npc_contains_item": "backpack" } })" );

    {
        dialogue d( get_talker_for( u ), get_talker_for( pack_loc ) );
        CHECK_FALSE( u_is_container( d ) );
        CHECK( npc_is_container( d ) );
        CHECK( npc_is_container_empty( d ) );
        CHECK_FALSE( npc_is_sealed( d ) );
        CHECK_FALSE( npc_contains_rock( d ) );
        // a container does not contain itself
        CHECK_FALSE( npc_contains_backpack( d ) );
    }

    REQUIRE( pack.put_in( item( itype_bag_canvas ), pocket_type::CONTAINER ).success() );
    item &bag = content_of( pack, itype_bag_canvas );
    REQUIRE( bag.put_in( item( itype_rock ), pocket_type::CONTAINER ).success() );
    item &rock = content_of( bag, itype_rock );
    item_location bag_loc( pack_loc, &bag );
    item_location rock_loc( bag_loc, &rock );

    {
        dialogue d( get_talker_for( u ), get_talker_for( pack_loc ) );
        CHECK_FALSE( npc_is_container_empty( d ) );
        CHECK( npc_contains_bag( d ) );
        // nested contents count too
        CHECK( npc_contains_rock( d ) );
    }
    {
        dialogue d( get_talker_for( rock_loc ), get_talker_for( u ) );
        CHECK_FALSE( load_condition( R"({ "condition": "u_is_container" })" )( d ) );
        // any enclosing container counts, not only the direct one
        CHECK( load_condition( R"({ "condition": { "u_is_contained_in": "bag_canvas" } })" )( d ) );
        CHECK( load_condition( R"({ "condition": { "u_is_contained_in": "backpack" } })" )( d ) );
        CHECK_FALSE( load_condition(
                         R"({ "condition": { "u_is_contained_in": "duffelbag" } })" )( d ) );
        // the beta talker is a character, which is not contained in anything
        CHECK_FALSE( load_condition(
                         R"({ "condition": { "npc_is_contained_in": "backpack" } })" )( d ) );
    }
    REQUIRE( itype_duffelbag.is_valid() );

    item bottle( itype_bottle_plastic );
    bottle.fill_with( item( itype_water_clean ) );
    REQUIRE( bottle.seal() );
    item_location bottle_loc = u.i_add( bottle );
    REQUIRE( bottle_loc );
    {
        dialogue d( get_talker_for( u ), get_talker_for( bottle_loc ) );
        CHECK( npc_is_sealed( d ) );
        CHECK( npc_is_container( d ) );
        CHECK_FALSE( npc_is_container_empty( d ) );
    }
}

// INV-J03: has_ammo and is_rotten only worked on the beta talker; they now come in u_/npc_
// pairs, and the old names keep meaning the beta talker.
TEST_CASE( "inv-j03_is_rotten_and_has_ammo_pairs", "[audit][eoc][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    wear_backpack( u );
    item apple( itype_apple );
    apple.set_relative_rot( 2.0 );
    item_location apple_loc = u.i_add( apple );
    REQUIRE( apple_loc );
    REQUIRE( apple_loc->rotten() );
    item_location loaded_loc = u.i_add( tool_with_ammo( itype_lighter, 20 ) );
    REQUIRE( loaded_loc );
    item empty_lighter( itype_lighter );
    empty_lighter.ammo_unset();
    item_location empty_loc = u.i_add( empty_lighter );
    REQUIRE( empty_loc );

    const conditional_t::func u_is_rotten = load_condition(
            R"({ "condition": "u_is_rotten" })" );
    const conditional_t::func npc_is_rotten = load_condition(
                R"({ "condition": "npc_is_rotten" })" );
    const conditional_t::func is_rotten = load_condition( R"({ "condition": "is_rotten" })" );
    const conditional_t::func u_has_ammo = load_condition( R"({ "condition": "u_has_ammo" })" );
    const conditional_t::func npc_has_ammo = load_condition(
                R"({ "condition": "npc_has_ammo" })" );
    const conditional_t::func has_ammo = load_condition( R"({ "condition": "has_ammo" })" );

    {
        dialogue d( get_talker_for( apple_loc ), get_talker_for( u ) );
        CHECK( u_is_rotten( d ) );
    }
    {
        dialogue d( get_talker_for( u ), get_talker_for( apple_loc ) );
        CHECK( npc_is_rotten( d ) );
        CHECK( is_rotten( d ) );
    }
    {
        dialogue d( get_talker_for( loaded_loc ), get_talker_for( u ) );
        CHECK( u_has_ammo( d ) );
    }
    {
        dialogue d( get_talker_for( empty_loc ), get_talker_for( u ) );
        CHECK_FALSE( u_has_ammo( d ) );
    }
    {
        dialogue d( get_talker_for( u ), get_talker_for( loaded_loc ) );
        CHECK( npc_has_ammo( d ) );
        CHECK( has_ammo( d ) );
    }
    {
        dialogue d( get_talker_for( u ), get_talker_for( empty_loc ) );
        CHECK_FALSE( npc_has_ammo( d ) );
        CHECK_FALSE( has_ammo( d ) );
    }
}

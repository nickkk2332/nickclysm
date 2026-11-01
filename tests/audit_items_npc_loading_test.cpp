#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const faction_id faction_free_merchants( "free_merchants" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_stick( "stick" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_items_npc_loading_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

static void consume_item_sum( avatar &u, const std::string &entries )
{
    const talk_effect_t effect = load_effect( R"({ "effect": { "u_consume_item_sum": )" + entries +
                                 " } }" );
    dialogue d( get_talker_for( u ), nullptr );
    effect.apply( d );
}

// EOC-B05: when a charge stack held more than was needed, the effect zeroed the amount it
// wanted before taking it off the stack, so nothing was consumed.
TEST_CASE( "eoc-b05_consume_item_sum_removes_partial_charges", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    u.wear_item( item( itype_backpack ), false );
    REQUIRE( item( itype_nail ).count_by_charges() );
    u.i_add( item( itype_nail, calendar::turn, 50 ) );
    REQUIRE( u.charges_of( itype_nail ) == 50 );

    consume_item_sum( u, R"([ { "item": "nail", "amount": 3 } ])" );
    CHECK( u.charges_of( itype_nail ) == 47 );
}

// EOC-B05: _has_items_sum treats each entry as a share of one requirement, but the effect took
// the full amount of every entry, so "3 nails or 3 rocks" cost 3 nails and 3 rocks.
TEST_CASE( "eoc-b05_consume_item_sum_takes_only_the_covered_share", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    u.wear_item( item( itype_backpack ), false );
    REQUIRE( item( itype_rock ).count_by_charges() );
    u.i_add( item( itype_nail, calendar::turn, 3 ) );
    u.i_add( item( itype_rock, calendar::turn, 3 ) );
    REQUIRE( u.charges_of( itype_nail ) == 3 );
    REQUIRE( u.charges_of( itype_rock ) == 3 );

    SECTION( "the first entry covers the whole requirement" ) {
        consume_item_sum( u, R"([ { "item": "nail", "amount": 3 },)"
                          R"( { "item": "rock", "amount": 3 } ])" );
        CHECK( u.charges_of( itype_nail ) == 0 );
        CHECK( u.charges_of( itype_rock ) == 3 );
    }
    SECTION( "entries add up as shares" ) {
        // 3 of 6 nails cover half the requirement, so 2 of 4 rocks finish it.
        consume_item_sum( u, R"([ { "item": "nail", "amount": 6 },)"
                          R"( { "item": "rock", "amount": 4 } ])" );
        CHECK( u.charges_of( itype_nail ) == 0 );
        CHECK( u.charges_of( itype_rock ) == 1 );
    }
}

// EOC-B05: _has_items_sum counts nearby items anyone may take, but the effect only took items
// owned by the talker's faction, so free items nearby passed the check and were never taken.
TEST_CASE( "eoc-b05_consume_item_sum_takes_what_has_items_sum_counts", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms free_spot = u.pos_bub() + tripoint::east;
    const tripoint_bub_ms owned_spot = u.pos_bub() + tripoint::west;
    for( int i = 0; i < 3; i++ ) {
        here.add_item_or_charges( free_spot, item( itype_stick ) );
        item theirs( itype_stick );
        theirs.set_owner( faction_free_merchants );
        here.add_item_or_charges( owned_spot, theirs );
    }
    REQUIRE( here.i_at( free_spot ).size() == 3 );
    REQUIRE( here.i_at( owned_spot ).size() == 3 );
    const conditional_t::func has_three = load_condition(
            R"({ "condition": { "u_has_items_sum": [ { "item": "stick", "amount": 3 } ] } })" );
    const conditional_t::func has_four = load_condition(
            R"({ "condition": { "u_has_items_sum": [ { "item": "stick", "amount": 4 } ] } })" );
    dialogue d( get_talker_for( u ), nullptr );
    // Only the three free sticks count.
    REQUIRE( has_three( d ) );
    REQUIRE_FALSE( has_four( d ) );

    consume_item_sum( u, R"([ { "item": "stick", "amount": 3 } ])" );
    CHECK( here.i_at( free_spot ).empty() );
    CHECK( here.i_at( owned_spot ).size() == 3 );
    CHECK_FALSE( has_three( d ) );
}

// EOC-B05: neither the effect nor the condition checked that the talker is a character.
TEST_CASE( "eoc-b05_item_sum_on_a_non_character_does_nothing", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    const tripoint_bub_ms spot = get_avatar().pos_bub() + tripoint::east * 2;
    monster &zombie = spawn_test_monster( "mon_zombie", spot );
    get_map().add_item_or_charges( spot, item( itype_stick ) );
    const std::string entries = R"([ { "item": "stick", "amount": 1 } ])";
    const talk_effect_t effect = load_effect( R"({ "effect": { "u_consume_item_sum": )" + entries +
                                 " } }" );
    const std::string sum = R"({ "condition": { "u_has_items_sum": )" + entries + " } }";
    const conditional_t::func has_one = load_condition( sum );
    dialogue d( get_talker_for( zombie ), nullptr );
    CHECK_FALSE( has_one( d ) );
    effect.apply( d );
    CHECK( get_map().i_at( spot ).size() == 1 );
}

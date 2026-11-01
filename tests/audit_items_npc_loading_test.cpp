#include <string>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "clzones.h"
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
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const faction_id faction_free_merchants( "free_merchants" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_grenade_act( "grenade_act" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_stick( "stick" );

static const ter_str_id ter_t_floor( "t_floor" );

static const zone_type_id zone_type_LOOT_SPARE_PARTS( "LOOT_SPARE_PARTS" );
static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );

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

// ACT-B03: a picked-up charge stack merges into the sorter's own stack of the same item, and
// drop-off then carried off the whole merged stack, the sorter's own charges included.
TEST_CASE( "act-b03_zone_sort_leaves_the_sorters_own_charges", "[audit][activities][zones]" )
{
    avatar &u = get_avatar();
    map &here = get_map();
    clear_avatar();
    clear_map_without_vision();
    zone_manager &zm = zone_manager::get_manager();
    zm.clear();

    const tripoint_bub_ms start_pos( 60, 60, 0 );
    u.setpos( here, start_pos );
    u.clear_destination();
    u.wear_item( item( itype_backpack ), false );
    u.i_add( item( itype_nail, calendar::turn, 50 ) );
    REQUIRE( u.charges_of( itype_nail ) == 50 );

    // The destination is out of reach, so the nails are carried rather than handed over.
    const tripoint_bub_ms src_pos = start_pos + tripoint::south;
    const tripoint_bub_ms dest_pos = start_pos + tripoint::east * 5;
    here.ter_set( src_pos, ter_t_floor );
    here.ter_set( dest_pos, ter_t_floor );
    zm.add( "Unsorted", zone_type_LOOT_UNSORTED, u.get_faction_id(), false, true,
            here.get_abs( src_pos ), here.get_abs( src_pos ) );
    zm.add( "Parts", zone_type_LOOT_SPARE_PARTS, u.get_faction_id(), false, true,
            here.get_abs( dest_pos ), here.get_abs( dest_pos ) );
    here.add_item_or_charges( src_pos, item( itype_nail, calendar::turn, 5 ) );
    REQUIRE( zm.get_near_zone_type_for_item( item( itype_nail ), here.get_abs( start_pos ) ) ==
             zone_type_LOOT_SPARE_PARTS );
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0, true );

    u.assign_activity( zone_sort_activity_actor() );
    CHECK( process_activity_with_travel( u, 1000 ) );

    int sorted = 0;
    for( const item &it : here.i_at( dest_pos ) ) {
        if( it.typeId() == itype_nail ) {
            sorted += it.charges;
        }
    }
    CHECK( here.i_at( src_pos ).empty() );
    CHECK( sorted == 5 );
    CHECK( u.charges_of( itype_nail ) == 50 );
    zm.clear();
}

// NPC-B04: the explosive scan only knew items with an "explosion" use action, but grenades,
// dynamite and C4 keep their blast in countdown_action, so NPCs never noticed a live one.
// The fuse was also read from charges instead of countdown_point.
TEST_CASE( "npc-b04_npcs_notice_a_live_grenade", "[audit][npc]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    npc &guy = spawn_npc( get_avatar().pos_bub().xy() + point::east * 5, "test_talker" );
    guy.regen_ai_cache();
    REQUIRE_FALSE( guy.in_harms_way() );

    item grenade( itype_grenade_act );
    REQUIRE( grenade.active );
    SECTION( "about to go off" ) {
        grenade.countdown_point = calendar::turn;
        here.add_item_or_charges( guy.pos_bub(), grenade );
        guy.regen_ai_cache();
        CHECK( guy.in_harms_way() );
    }
    SECTION( "long fuse" ) {
        grenade.countdown_point = calendar::turn + 1_hours;
        here.add_item_or_charges( guy.pos_bub(), grenade );
        guy.regen_ai_cache();
        CHECK_FALSE( guy.in_harms_way() );
    }
    // Don't leave a live grenade for later tests.
    here.i_clear( guy.pos_bub() );
}

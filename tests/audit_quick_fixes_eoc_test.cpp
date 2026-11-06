#include <cstddef>
#include <initializer_list>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_chatbin.h"
#include "dialogue_helpers.h"
#include "enum_conversions.h"
#include "event.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "mission.h"
#include "npc.h"
#include "path_info.h"
#include "pickup.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const fault_id fault_audit_j01_goo( "fault_audit_j01_goo" );
static const fault_id fault_audit_j01_slime( "fault_audit_j01_slime" );

static const itype_id itype_audit_j04_pebble( "audit_j04_pebble" );
static const itype_id itype_test_backpack( "test_backpack" );
static const itype_id itype_test_rock( "test_rock" );

static const mission_type_id
mission_TEST_MISSION_GOAL_CONDITION1( "TEST_MISSION_GOAL_CONDITION1" );
static const mission_type_id
mission_TEST_MISSION_GOAL_CONDITION2( "TEST_MISSION_GOAL_CONDITION2" );

namespace
{
talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_quick_fixes_eoc_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

void run_effect( dialogue &d, const std::string &json )
{
    load_effect( json ).apply( d );
}

// The error loading this effect throws, or "" if it loads
std::string load_error( const std::string &json )
{
    try {
        load_effect( json );
    } catch( const JsonError &e ) {
        return e.what();
    }
    return {};
}

bool message_seen( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}
} // namespace

// EOC-J02: var_val and math documented the g_ (global) and v_ (var_val) prefixes, but a
// var_val value "g_foo" read the global literally named "g_foo", "v_foo" didn't point anywhere,
// and math threw "Unknown scope g" on g_foo.
TEST_CASE( "eoc-j02_var_val_resolves_g_and_v_prefixes", "[audit][eoc][math_parser]" )
{
    clear_avatar();
    global_variables &globals = get_globals();
    on_out_of_scope cleanup( [&globals]() {
        for( const char *name : {
                 "audit_j02_target", "audit_j02_num", "g_audit_j02_target"
             } ) {
            globals.remove_global_value( name );
        }
    } );
    globals.set_global_value( "audit_j02_target", "found" );
    globals.set_global_value( "g_audit_j02_target", "the old literal name" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    d.set_value( "audit_ref", "g_audit_j02_target" );
    d.set_value( "audit_ref2", "v_audit_ref" );

    CHECK( read_var_value( var_info( var_type::var, "audit_ref" ), d ).str() == "found" );
    // v_ points at another var_val
    CHECK( read_var_value( var_info( var_type::var, "audit_ref2" ), d ).str() == "found" );

    // math reads and writes g_ variables in the global scope, and writes through v_ chains
    run_effect( d, R"({ "effect": { "math": [ "g_audit_j02_num = 5" ] } })" );
    REQUIRE( globals.maybe_get_global_value( "audit_j02_num" ) != nullptr );
    CHECK( globals.get_global_value( "audit_j02_num" ).dbl() == 5 );
    CHECK( load_condition( R"({ "condition": { "math": [ "g_audit_j02_num == 5" ] } })" )( d ) );
    run_effect( d, R"({ "effect": { "math": [ "v_audit_ref2 = 7" ] } })" );
    CHECK( globals.get_global_value( "audit_j02_target" ).dbl() == 7 );

    // a var_val that points at itself is stopped, not followed forever
    d.set_value( "audit_loop", "v_audit_loop" );
    std::string report = capture_debugmsg_during( [&d]() {
        CHECK( maybe_read_var_value( var_info( var_type::var, "audit_loop" ), d ) == nullptr );
    } );
    CHECK( report.find( "probably in a loop" ) != std::string::npos );
    report = capture_debugmsg_during( [&d]() {
        write_var_value( var_type::var, "audit_loop", &d, 3.0 );
    } );
    CHECK( report.find( "probably in a loop" ) != std::string::npos );
}

// EOC-J02: offer_mission is documented to take variable objects, but an object failed the load
// with "Invalid input for set_offer_mission".
TEST_CASE( "eoc-j02_offer_mission_takes_variable_objects", "[audit][eoc][mission]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        mission::clear_all();
        clear_map();
    } );
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
    const size_t offered = guy.chatbin.missions.size();
    dialogue d( get_talker_for( u ), get_talker_for( guy ) );
    d.set_value( "audit_mission", "TEST_MISSION_GOAL_CONDITION1" );
    const std::string from_variable =
        R"({ "effect": { "offer_mission": { "context_val": "audit_mission" } } })";

    run_effect( d, from_variable );
    REQUIRE( guy.chatbin.missions.size() == offered + 1 );
    CHECK( guy.chatbin.missions.back()->mission_id() == mission_TEST_MISSION_GOAL_CONDITION1 );

    run_effect( d, R"({ "effect": { "offer_mission":
        [ "TEST_MISSION_GOAL_CONDITION2", { "context_val": "audit_mission" } ] } })" );
    REQUIRE( guy.chatbin.missions.size() == offered + 3 );
    CHECK( guy.chatbin.missions[offered + 1]->mission_id() ==
           mission_TEST_MISSION_GOAL_CONDITION2 );

    // a variable that names no mission is reported, and offers nothing
    d.set_value( "audit_mission", "audit_no_such_mission" );
    const std::string report = capture_debugmsg_during( [&d, &from_variable]() {
        run_effect( d, from_variable );
    } );
    CHECK( report.find( "audit_no_such_mission" ) != std::string::npos );
    CHECK( guy.chatbin.missions.size() == offered + 3 );
}

// ITEM-J01, EOC-J02: set_fault's "message" (default true) took the holder from the item talker,
// which is never a character, so the fault's message never showed, even for a worn item.
TEST_CASE( "item-j01_set_fault_message_reaches_the_carrier", "[audit][eoc][item]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    item held( itype_test_rock );
    u.wield( held );
    item_location rock = u.get_wielded_item();
    REQUIRE( rock );
    dialogue d( get_talker_for( u ), get_talker_for( rock ) );
    Messages::clear_messages();
    on_out_of_scope clear( []() {
        Messages::clear_messages();
    } );

    run_effect( d, R"({ "effect": { "npc_set_fault": "fault_audit_j01_goo", "force": true } })" );
    CHECK( rock->has_fault( fault_audit_j01_goo ) );
    CHECK( message_seen( "Audit goo sticks to the" ) );

    Messages::clear_messages();
    rock->remove_fault( fault_audit_j01_goo );
    run_effect( d, R"({ "effect": { "npc_set_fault": "fault_audit_j01_goo", "force": true,
        "message": false } })" );
    CHECK( rock->has_fault( fault_audit_j01_goo ) );
    CHECK_FALSE( message_seen( "Audit goo sticks to the" ) );
}

// ITEM-J01: EOC faults were write-only: nothing could tell whether an item had a fault, or
// take one away.
TEST_CASE( "item-j01_faults_can_be_checked_and_removed", "[audit][eoc][item]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    item held( itype_test_rock );
    u.wield( held );
    item_location rock = u.get_wielded_item();
    REQUIRE( rock );
    dialogue d( get_talker_for( u ), get_talker_for( rock ) );
    const auto set_both = [&rock]() {
        rock->set_fault( fault_audit_j01_goo, true );
        rock->set_fault( fault_audit_j01_slime, true );
    };
    const conditional_t::func has_goo =
        load_condition( R"({ "condition": { "npc_has_fault": "fault_audit_j01_goo" } })" );
    const conditional_t::func has_coating =
        load_condition( R"({ "condition": { "npc_has_fault_of_type": "audit_j01_coating" } })" );

    CHECK_FALSE( has_goo( d ) );
    CHECK_FALSE( has_coating( d ) );
    set_both();
    CHECK( has_goo( d ) );
    CHECK( has_coating( d ) );
    // the avatar is no item: false, without an error
    CHECK_FALSE( load_condition(
                     R"({ "condition": { "u_has_fault": "fault_audit_j01_goo" } })" )( d ) );

    run_effect( d, R"({ "effect": { "npc_remove_fault": "fault_audit_j01_goo" } })" );
    CHECK_FALSE( rock->has_fault( fault_audit_j01_goo ) );
    CHECK( rock->has_fault( fault_audit_j01_slime ) );
    run_effect( d, R"({ "effect": { "npc_remove_fault_of_type": "audit_j01_coating" } })" );
    CHECK_FALSE( rock->has_fault_of_type( "audit_j01_coating" ) );

    // one at a time, or all of them
    set_both();
    run_effect( d, R"({ "effect": { "npc_remove_fault_of_type": "audit_j01_coating" } })" );
    CHECK( rock->has_fault( fault_audit_j01_goo ) != rock->has_fault( fault_audit_j01_slime ) );
    set_both();
    run_effect( d, R"({ "effect": { "npc_remove_fault_of_type": { "context_val": "audit_type" },
        "all": true } })" );
    // an unset variable is an empty type: nothing goes
    CHECK( rock->has_fault( fault_audit_j01_goo ) );
    d.set_value( "audit_type", "audit_j01_coating" );
    run_effect( d, R"({ "effect": { "npc_remove_fault_of_type": { "context_val": "audit_type" },
        "all": true } })" );
    CHECK_FALSE( rock->has_fault_of_type( "audit_j01_coating" ) );

    // misuse is reported, and changes nothing
    std::string report = capture_debugmsg_during( [&d]() {
        run_effect( d, R"({ "effect": { "u_remove_fault": "fault_audit_j01_goo" } })" );
    } );
    CHECK( report.find( "needs the alpha talker to be an item" ) != std::string::npos );
    report = capture_debugmsg_during( [&d]() {
        CHECK_FALSE( load_condition(
                         R"({ "condition": { "npc_has_fault": "audit_no_such_fault" } })" )( d ) );
    } );
    CHECK( report.find( "audit_no_such_fault" ) != std::string::npos );
}

// INV-J04: in a bulk load (the second of two stacking items from the same tile into the same
// container) the extra moves per distance were added to the picker's moves, not taken.
TEST_CASE( "inv-j04_bulk_pickup_charges_its_extra_moves", "[audit][eoc][pickup]" )
{
    const auto moves_spent = []( int pebbles, int extra_moves_per_distance ) {
        clear_map();
        clear_avatar();
        avatar &u = get_avatar();
        map &here = get_map();
        u.wear_item( item( itype_test_backpack ), false );
        // pebbles one tile away
        const tripoint_bub_ms spot = u.pos_bub() + point::east;
        std::vector<item_location> targets;
        std::vector<int> quantities;
        for( int i = 0; i < pebbles; ++i ) {
            item &pebble = here.add_item( spot, item( itype_audit_j04_pebble ) );
            targets.emplace_back( map_cursor( here.get_abs( spot ) ), &pebble );
            quantities.push_back( 0 );
        }
        Pickup::pick_info info( extra_moves_per_distance );
        bool stash_successful = true;
        u.set_moves( 100000 );
        Pickup::do_pickup( targets, quantities, false, stash_successful, info );
        CHECK( u.amount_of( itype_audit_j04_pebble ) == pebbles );
        return 100000 - u.get_moves();
    };
    on_out_of_scope restore( []() {
        clear_map();
        clear_avatar();
    } );
    // the second pebble is a bulk load: it costs less than the first, which pays the map's
    // handling penalty and the walk
    REQUIRE( moves_spent( 2, 0 ) < 2 * moves_spent( 1, 0 ) );
    // one tile away, both pebbles cost the extra moves once
    CHECK( moves_spent( 2, 100 ) - moves_spent( 2, 0 ) == 2 * 100 );
}

// INV-J04: after 200 ml the bulk load was restarted by replacing the whole pick_info, which
// dropped the limits and running totals too (EOC-B11 kept them by hand); now only the bulk
// load itself is reset.
TEST_CASE( "inv-j04_bulk_reset_keeps_limits_and_totals", "[audit][pickup]" )
{
    Pickup::pick_info info( 7, 300_ml, 400_gram );
    info.total_bulk_volume = 250_ml;
    info.src_type = item_location::type::map;
    info.picked_up_volume = 250_ml;
    info.picked_up_mass = 100_gram;
    info.reset_bulk_load();
    CHECK( info.total_bulk_volume == 0_ml );
    CHECK( info.src_type == item_location::type::invalid );
    CHECK_FALSE( info.dst );
    CHECK( info.extra_moves_per_distance == 7 );
    CHECK( info.max_volume == 300_ml );
    CHECK( info.max_mass == 400_gram );
    CHECK( info.picked_up_volume == 250_ml );
    CHECK( info.picked_up_mass == 100_gram );
}

// INV-J04: npc_pickup_items (or u_pickup_items run by an NPC) opened the avatar's pickup menu and
// then tripped ACT_PICKUP's avatar-only assertion, ending the game.  Both now refuse.
TEST_CASE( "inv-j04_pickup_items_refuses_a_non_avatar", "[audit][eoc][pickup]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_map();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
    item &pebble = here.add_item( guy.pos_bub(), item( itype_audit_j04_pebble ) );

    dialogue by_npc( get_talker_for( guy ), nullptr );
    std::string report = capture_debugmsg_during( [&by_npc]() {
        run_effect( by_npc,
                    R"({ "effect": { "u_pickup_items": { "global_val": "audit_spot" } } })" );
    } );
    CHECK( report.find( "only the avatar" ) != std::string::npos );
    dialogue with_npc( get_talker_for( u ), get_talker_for( guy ) );
    report = capture_debugmsg_during( [&with_npc]() {
        run_effect( with_npc,
                    R"({ "effect": { "npc_pickup_items": { "global_val": "audit_spot" } } })" );
    } );
    CHECK( report.find( "npc_pickup_items: only the avatar" ) != std::string::npos );

    // ACT_PICKUP on an NPC
    const item_location target( map_cursor( here.get_abs( guy.pos_bub() ) ), &pebble );
    pickup_activity_actor actor( { target }, { 0 }, std::nullopt, false );
    player_activity act( actor );
    report = capture_debugmsg_during( [&]() {
        actor.do_turn( act, guy );
    } );
    CHECK( report.find( "only the avatar can" ) != std::string::npos );
    CHECK( here.i_at( guy.pos_bub() ).size() == 1 );
    CHECK( u.amount_of( itype_audit_j04_pebble ) == 0 );
}

// EVT-J02: trigger_event's args are positional, in event.h's field order, but a wrong number of
// them was only found when the effect ran.  It now fails the load and lists the fields in order.
TEST_CASE( "evt-j02_trigger_event_checks_its_args_at_load", "[audit][event]" )
{
    // character_gains_effect: character, bodypart, effect, intensity
    const std::string error = load_error( R"({ "effect": {
        "trigger_event": "character_gains_effect",
        "args": [ 1, "onfire", "torso" ] } })" );
    CHECK( error.find( "takes 4 args" ) != std::string::npos );
    CHECK( error.find( "character (character_id), bodypart (body_part), effect (efftype_id), "
                       "intensity (int)" ) != std::string::npos );
    const std::string no_fields =
        load_error( R"({ "effect": { "trigger_event": "avatar_dies", "args": [ "x" ] } })" );
    CHECK( no_fields.find( "takes 0 args, in this order: none" ) != std::string::npos );
    CHECK( load_error( R"({ "effect": { "trigger_event": "character_gains_effect",
        "args": [ 1, "torso", "onfire", 1 ] } })" ).empty() );

    const cata::event::field_list_type fields = cata::event::get_field_list(
                event_type::character_loses_effect );
    REQUIRE( fields.size() == 3 );
    CHECK( fields[1].first == "bodypart" );
    CHECK( fields[2].first == "effect" );
}

// EVT-J03: the EVENT table in EFFECT_ON_CONDITION.md had drifted from event.h: missing or
// renamed fields (avatar_is_male, spell_class, reads_book's itype, victim_class), fields out of
// order (the effect events), and an event missing.  Every row must match event.h, in order.
TEST_CASE( "evt-j03_event_doc_table_matches_event_h", "[audit][event]" )
{
    const std::string doc = read_entire_file( PATH_INFO::datadir() +
                            "../doc/JSON/EFFECT_ON_CONDITION.md" );
    if( doc.empty() ) {
        WARN( "doc/JSON/EFFECT_ON_CONDITION.md not found next to the data folder; skipped" );
        return;
    }
    const std::size_t header = doc.find( "\n| EVENT " );
    REQUIRE( header != std::string::npos );
    // skip the header row and the separator row
    std::size_t pos = doc.find( '\n', doc.find( '\n', header + 1 ) + 1 );
    const std::regex field_re( R"re(\{\s*"(\w+)",)re" );
    std::set<std::string> documented;
    while( pos != std::string::npos && pos + 1 < doc.size() && doc[pos + 1] == '|' ) {
        const std::size_t end = doc.find( '\n', pos + 1 );
        const std::string row = doc.substr( pos + 1, end - pos - 1 );
        pos = end;
        // | name | description | context variables | talkers |
        std::vector<std::string> cells;
        std::size_t start = 1;
        for( std::size_t bar = row.find( '|', start ); bar != std::string::npos;
             bar = row.find( '|', start ) ) {
            cells.push_back( row.substr( start, bar - start ) );
            start = bar + 1;
        }
        REQUIRE( cells.size() >= 3 );
        const std::string name = std::regex_replace( cells[0], std::regex( R"(^\s+|\s+$)" ), "" );
        CAPTURE( name );
        CHECK( documented.insert( name ).second );
        const std::optional<event_type> type = io::string_to_enum_optional<event_type>( name );
        REQUIRE( type.has_value() );
        std::vector<std::string> doc_fields;
        const std::string &context_vars = cells[2];
        for( std::sregex_iterator it( context_vars.begin(), context_vars.end(), field_re ), last;
             it != last; ++it ) {
            doc_fields.push_back( ( *it )[1] );
        }
        std::vector<std::string> spec_fields;
        for( const std::pair<std::string, cata_variant_type> &field : cata::event::get_field_list(
                 *type ) ) {
            spec_fields.push_back( field.first );
        }
        CHECK( doc_fields == spec_fields );
    }
    for( int i = 0; i < static_cast<int>( event_type::num_event_types ); ++i ) {
        const std::string name = io::enum_to_string( static_cast<event_type>( i ) );
        CAPTURE( name );
        CHECK( documented.count( name ) == 1 );
    }
}

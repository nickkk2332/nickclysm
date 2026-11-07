#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_action.h"
#include "item_factory.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "json.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "type_id.h"

static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_USEJ02_ACTIVATE( "EOC_TEST_USEJ02_ACTIVATE" );

static const itype_id itype_lamp_oil( "lamp_oil" );
static const itype_id itype_match( "match" );
static const itype_id itype_ref_lighter( "ref_lighter" );
static const itype_id itype_ref_lighter_on( "ref_lighter_on" );
static const itype_id itype_test_usej02_dial( "test_usej02_dial" );
static const itype_id itype_test_usej02_dial_high( "test_usej02_dial_high" );
static const itype_id itype_test_usej02_dial_low( "test_usej02_dial_low" );
static const itype_id itype_test_usej02_staff( "test_usej02_staff" );
static const itype_id itype_test_usej02_staff_child( "test_usej02_staff_child" );

// The test world: a fixed time, an empty map and the avatar alone, no global variables.
static void usej02_clear_world()
{
    calendar::turn = calendar::turn_zero + 12_hours;
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    clear_avatar();
    get_globals().clear_global_values();
}

// A numeric character variable as math sees it (unset reads as 0).
static double usej02_char_var( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

static std::string usej02_global_str( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    return val == nullptr ? std::string() : val->str();
}

// The keys of a use action map, in order.
static std::vector<std::string> usej02_keys( const std::map<std::string, use_function> &uses )
{
    std::vector<std::string> keys;
    keys.reserve( uses.size() );
    for( const std::pair<const std::string, use_function> &use : uses ) {
        keys.push_back( use.first );
    }
    return keys;
}

static std::string usej02_serialized( const activity_actor &actor )
{
    std::ostringstream os;
    JsonOut jsout( os );
    actor.serialize( jsout );
    return os.str();
}

// USE-J02: use_methods, tick_action and ammo_scale were keyed by the action type, so a second
// cast_spell (or effect_on_conditions, or transform) action of an item was dropped with a "key
// already exists" error. Actions now have ids (their type by default) and are keyed by them.
TEST_CASE( "use-j02_two_actions_of_a_type_are_kept_by_id", "[audit][iuse]" )
{
    const itype &staff = *item::find_type( itype_test_usej02_staff );
    CHECK( usej02_keys( staff.use_methods ) == std::vector<std::string> {
        "usej02_cast_fire", "usej02_cast_heal", "usej02_mark_a", "usej02_mark_b"
    } );
    for( const std::pair<const std::string, use_function> &use : staff.use_methods ) {
        CAPTURE( use.first );
        CHECK( use.second.get_id() == use.first );
    }
    REQUIRE( staff.get_use( "usej02_cast_fire" ) != nullptr );
    REQUIRE( staff.get_use( "usej02_cast_heal" ) != nullptr );
    CHECK( staff.get_use( "usej02_cast_fire" )->get_type() == "cast_spell" );
    CHECK( staff.get_use( "usej02_cast_heal" )->get_type() == "cast_spell" );
    // Each has its own menu text
    CHECK( staff.get_use( "usej02_cast_fire" )->get_name() == "Cast test fire" );
    CHECK( staff.get_use( "usej02_cast_heal" )->get_name() == "Cast test heal" );
    CHECK( staff.get_use( "usej02_mark_a" )->get_name() == "Mark A" );
    CHECK( staff.get_use( "usej02_mark_b" )->get_name() == "Mark B" );
    // and its own ammo_scale, by id
    CHECK( staff.ammo_scale.count( "cast_spell" ) == 0 );
    CHECK( staff.get_ammo_scale( "usej02_cast_fire" ) == 1 );
    CHECK( staff.get_ammo_scale( "usej02_cast_heal" ) == 4 );
    CHECK( staff.get_ammo_scale( "usej02_mark_a" ) == 2 );
    CHECK( staff.get_ammo_scale( "usej02_mark_b" ) == 1 );
    // A type names the first action of that type, by id
    REQUIRE( staff.get_use( "cast_spell" ) != nullptr );
    CHECK( staff.get_use( "cast_spell" )->get_id() == "usej02_cast_fire" );
    CHECK( staff.get_ammo_scale( "cast_spell" ) == 1 );
    REQUIRE( staff.get_use( "effect_on_conditions" ) != nullptr );
    CHECK( staff.get_use( "effect_on_conditions" )->get_id() == "usej02_mark_a" );
    CHECK_FALSE( staff.can_use( "transform" ) );
}

TEST_CASE( "use-j02_each_spell_action_is_reached_by_its_id", "[audit][iuse][magic]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    usej02_clear_world();
    avatar &u = get_avatar();
    item_location staff = u.i_add( item( itype_test_usej02_staff ) );
    REQUIRE( staff );

    SECTION( "through invoke_item, each casting its own spell" ) {
        staff->ammo_set( itype_match, 20 );
        u.invoke_item( staff.get_item(), "usej02_cast_fire" );
        REQUIRE( u.activity.id() == ACT_SPELLCASTING );
        REQUIRE( process_activity_bounded( u, 10, 100 ) );
        CHECK( usej02_char_var( u, "usej02_fire" ) == 1 );
        CHECK( usej02_char_var( u, "usej02_heal" ) == 0 );
        CHECK( staff->ammo_remaining() == 19 );

        u.invoke_item( staff.get_item(), "usej02_cast_heal" );
        REQUIRE( u.activity.id() == ACT_SPELLCASTING );
        REQUIRE( process_activity_bounded( u, 10, 100 ) );
        CHECK( usej02_char_var( u, "usej02_fire" ) == 1 );
        CHECK( usej02_char_var( u, "usej02_heal" ) == 1 );
    }

    SECTION( "through u_activate, by id" ) {
        staff->ammo_set( itype_match, 20 );
        get_globals().set_global_value( "usej02_activate", "usej02_cast_heal" );
        dialogue d( get_talker_for( u ), get_talker_for( staff ) );
        CHECK( effect_on_condition_EOC_TEST_USEJ02_ACTIVATE->activate( d ) );
        REQUIRE( u.activity.id() == ACT_SPELLCASTING );
        REQUIRE( process_activity_bounded( u, 10, 100 ) );
        CHECK( usej02_char_var( u, "usej02_heal" ) == 1 );
        CHECK( usej02_char_var( u, "usej02_fire" ) == 0 );
    }

    SECTION( "each action needs the charges of its own ammo_scale" ) {
        // Three uses' worth: enough for the fire action (scale 1), not the heal one (scale 4)
        staff->ammo_set( itype_match, 3 );
        CHECK( staff->ammo_sufficient( &u, "usej02_cast_fire" ) );
        CHECK_FALSE( staff->ammo_sufficient( &u, "usej02_cast_heal" ) );
        CHECK_FALSE( u.invoke_item( staff.get_item(), "usej02_cast_heal" ) );
        CHECK( u.activity.is_null() );
        CHECK( usej02_char_var( u, "usej02_heal" ) == 0 );
        u.invoke_item( staff.get_item(), "usej02_cast_fire" );
        CHECK( u.activity.id() == ACT_SPELLCASTING );
    }
}

TEST_CASE( "use-j02_eoc_actions_see_their_own_id_as_method", "[audit][iuse][eoc]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    usej02_clear_world();
    avatar &u = get_avatar();
    global_variables &globvars = get_globals();
    item_location staff = u.i_add( item( itype_test_usej02_staff ) );
    REQUIRE( staff );
    staff->ammo_set( itype_match, 20 );

    SECTION( "invoked by id" ) {
        u.invoke_item( staff.get_item(), "usej02_mark_a" );
        CHECK( usej02_global_str( "usej02_method" ) == "usej02_mark_a" );
        // mark_a doesn't consume
        CHECK( staff->ammo_remaining() == 20 );
        u.invoke_item( staff.get_item(), "usej02_mark_b" );
        CHECK( usej02_global_str( "usej02_method" ) == "usej02_mark_b" );
        // mark_b consumes one use
        CHECK( staff->ammo_remaining() == 19 );
        CHECK( globvars.get_global_value( "usej02_marks" ) == 2 );
    }

    SECTION( "through u_activate, by id and by type" ) {
        globvars.set_global_value( "usej02_activate", "usej02_mark_b" );
        dialogue d( get_talker_for( u ), get_talker_for( staff ) );
        CHECK( effect_on_condition_EOC_TEST_USEJ02_ACTIVATE->activate( d ) );
        CHECK( usej02_global_str( "usej02_method" ) == "usej02_mark_b" );
        CHECK( staff->ammo_remaining() == 19 );
        // The type finds the first action of the type, which reports its own id
        globvars.set_global_value( "usej02_activate", "effect_on_conditions" );
        dialogue d2( get_talker_for( u ), get_talker_for( staff ) );
        CHECK( effect_on_condition_EOC_TEST_USEJ02_ACTIVATE->activate( d2 ) );
        CHECK( usej02_global_str( "usej02_method" ) == "usej02_mark_a" );
        CHECK( globvars.get_global_value( "usej02_marks" ) == 2 );
    }

    SECTION( "an action whose ammo_scale is not met isn't used" ) {
        // mark_a needs two uses' worth of charges
        staff->ammo_set( itype_match, 1 );
        CHECK_FALSE( u.invoke_item( staff.get_item(), "usej02_mark_a" ) );
        CHECK( usej02_global_str( "usej02_method" ).empty() );
        u.invoke_item( staff.get_item(), "usej02_mark_b" );
        CHECK( usej02_global_str( "usej02_method" ) == "usej02_mark_b" );
    }
}

TEST_CASE( "use-j02_transform_modes_spend_their_own_ammo_scale", "[audit][iuse]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    usej02_clear_world();
    avatar &u = get_avatar();

    SECTION( "low" ) {
        item_location dial = u.i_add( item( itype_test_usej02_dial ) );
        REQUIRE( dial );
        dial->ammo_set( itype_match, 20 );
        u.invoke_item( dial.get_item(), "usej02_low" );
        CHECK( dial->typeId() == itype_test_usej02_dial_low );
        CHECK( dial->ammo_remaining() == 19 );
    }
    SECTION( "high" ) {
        item_location dial = u.i_add( item( itype_test_usej02_dial ) );
        REQUIRE( dial );
        dial->ammo_set( itype_match, 20 );
        u.invoke_item( dial.get_item(), "usej02_high" );
        CHECK( dial->typeId() == itype_test_usej02_dial_high );
        CHECK( dial->ammo_remaining() == 17 );
    }
}

// The item action menu lists each action with an id of its own, under that id; not under its
// type as well.
TEST_CASE( "use-j02_item_action_menu_lists_each_action_by_id", "[audit][iuse]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    usej02_clear_world();
    avatar &u = get_avatar();
    item_location staff = u.i_add( item( itype_test_usej02_staff ) );
    REQUIRE( staff );
    staff->ammo_set( itype_match, 20 );

    const item_action_map actions = item_action_generator::generator().map_actions_to_items( u,
                                    std::vector<item *> { staff.get_item() }, false );
    const std::vector<std::string> ids = {
        "usej02_cast_fire", "usej02_cast_heal", "usej02_mark_a", "usej02_mark_b"
    };
    for( const std::string &id : ids ) {
        CAPTURE( id );
        const auto found = actions.find( id );
        REQUIRE( found != actions.end() );
        CHECK( found->second == staff.get_item() );
    }
    CHECK( actions.count( "cast_spell" ) == 0 );
    CHECK( actions.count( "effect_on_conditions" ) == 0 );
}

// copy-from with "extend" and "delete" of use actions by id. The fixture child deletes
// usej02_cast_heal by its id and usej02_mark_b by an object naming its id, and adds
// usej02_cast_third.
TEST_CASE( "use-j02_copy_from_extends_and_deletes_use_actions_by_id", "[audit][iuse]" )
{
    const itype &child = *item::find_type( itype_test_usej02_staff_child );
    CHECK( usej02_keys( child.use_methods ) == std::vector<std::string> {
        "usej02_cast_fire", "usej02_cast_third", "usej02_mark_a"
    } );
    REQUIRE( child.get_use( "usej02_cast_third" ) != nullptr );
    CHECK( child.get_use( "usej02_cast_third" )->get_type() == "cast_spell" );
    CHECK( child.get_use( "usej02_cast_third" )->get_name() == "Cast test third" );
    CHECK( child.get_ammo_scale( "usej02_cast_fire" ) == 1 );
    CHECK( child.get_ammo_scale( "usej02_cast_third" ) == 3 );
    CHECK( child.get_ammo_scale( "usej02_mark_a" ) == 2 );
    // A deleted action takes its ammo_scale with it
    CHECK( child.ammo_scale.count( "usej02_cast_heal" ) == 0 );
    CHECK_FALSE( child.can_use( "usej02_cast_heal" ) );
    CHECK_FALSE( child.can_use( "usej02_mark_b" ) );
}

TEST_CASE( "use-j02_use_action_id_is_checked_at_load", "[audit][iuse]" )
{
    std::map<std::string, int> scale;

    SECTION( "an id is the action's key, with its menu_text and ammo_scale" ) {
        const JsonObject jo = json_loader::from_string(
                                  R"({ "type": "transform", "id": "usej02_custom", "menu_text": "Custom", "ammo_scale": 5 })" );
        jo.allow_omitted_members();
        std::string type = "transform";
        const use_function use = Item_factory::read_use_function( jo, scale, type );
        REQUIRE( use.get_actor_ptr() != nullptr );
        CHECK( use.get_id() == "usej02_custom" );
        CHECK( use.get_type() == "transform" );
        CHECK( use.get_name() == "Custom" );
        CHECK( scale.at( "usej02_custom" ) == 5 );
        CHECK( scale.count( "transform" ) == 0 );
    }
    SECTION( "without an id, it is the type" ) {
        const JsonObject jo = json_loader::from_string( R"({ "type": "transform", "ammo_scale": 2 })" );
        jo.allow_omitted_members();
        std::string type = "transform";
        const use_function use = Item_factory::read_use_function( jo, scale, type );
        REQUIRE( use.get_actor_ptr() != nullptr );
        CHECK( use.get_id() == "transform" );
        CHECK( scale.at( "transform" ) == 2 );
    }
    SECTION( "the name of another use action type is refused" ) {
        const JsonObject jo = json_loader::from_string( R"({ "type": "transform", "id": "heal" })" );
        jo.allow_omitted_members();
        std::string type = "transform";
        CHECK_THROWS_WITH( Item_factory::read_use_function( jo, scale, type ),
                           Catch::Contains( "name of a use action type" ) );
    }
    SECTION( "an empty id is refused" ) {
        const JsonObject jo = json_loader::from_string( R"({ "type": "transform", "id": "" })" );
        jo.allow_omitted_members();
        std::string type = "transform";
        CHECK_THROWS_WITH( Item_factory::read_use_function( jo, scale, type ),
                           Catch::Contains( "can't be empty" ) );
    }
}

// An unchanged vanilla item with two use actions of different types: each keyed by its type,
// with its usual name, and used as before.
TEST_CASE( "use-j02_vanilla_multi_use_item_is_unchanged", "[audit][iuse]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    usej02_clear_world();
    const itype &lighter_type = *item::find_type( itype_ref_lighter );
    CHECK( usej02_keys( lighter_type.use_methods ) == std::vector<std::string> {
        "firestarter", "transform"
    } );
    for( const std::pair<const std::string, use_function> &use : lighter_type.use_methods ) {
        CAPTURE( use.first );
        CHECK( use.second.get_id() == use.first );
        CHECK( use.second.get_type() == use.first );
    }
    CHECK( lighter_type.get_use( "transform" )->get_name() == "Light up" );
    CHECK( lighter_type.get_use( "firestarter" )->get_name() == "Start a fire quickly" );
    CHECK( lighter_type.get_ammo_scale( "transform" ) == 1 );

    avatar &u = get_avatar();
    item_location lighter = u.i_add( item( itype_ref_lighter ) );
    REQUIRE( lighter );
    lighter->ammo_set( itype_lamp_oil, 10 );
    u.invoke_item( lighter.get_item(), "transform" );
    CHECK( lighter->typeId() == itype_ref_lighter_on );
    CHECK( lighter->ammo_remaining() == 9 );
}

// The first aid and fire starting activities remember which heal or firestarter action they
// use; saves from before ids have none, and get the item's action of that type.
TEST_CASE( "use-j02_activities_remember_the_action_id", "[audit][iuse][activity]" )
{
    SECTION( "first aid" ) {
        const firstaid_activity_actor bandage( 100, "test bandage", character_id( 1 ), false,
                                               "usej02_bandage" );
        CHECK_THAT( usej02_serialized( bandage ), Catch::Contains( R"("method":"usej02_bandage")" ) );
        JsonValue old_save = json_loader::from_string(
                                 R"({ "moves": 100, "name": "old bandage", "patientID": 1, "reopen_menu": true })" );
        const std::unique_ptr<activity_actor> loaded = firstaid_activity_actor::deserialize( old_save );
        REQUIRE( loaded );
        CHECK_THAT( usej02_serialized( *loaded ), Catch::Contains( R"("method":"heal")" ) );
    }
    SECTION( "starting a fire" ) {
        const fire_start_activity_actor spark( tripoint_abs_ms::zero, item_location::nowhere, 1, 100,
                                               false, "usej02_spark" );
        CHECK_THAT( usej02_serialized( spark ), Catch::Contains( R"("method":"usej02_spark")" ) );
        JsonValue old_save = json_loader::from_string(
                                 R"({ "fire_placement": [ 0, 0, 0 ], "potential_skill_gain": 1, "used_tinder": false })" );
        const std::unique_ptr<activity_actor> loaded = fire_start_activity_actor::deserialize( old_save );
        REQUIRE( loaded );
        CHECK_THAT( usej02_serialized( *loaded ), Catch::Contains( R"("method":"firestarter")" ) );
    }
}

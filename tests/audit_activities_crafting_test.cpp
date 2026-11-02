#include <string>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "type_id.h"

static const activity_id ACT_CRAFT( "ACT_CRAFT" );
static const activity_id ACT_GAME( "ACT_GAME" );
static const activity_id ACT_MEDITATE( "ACT_MEDITATE" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_TEST_HOOKS( "ACT_TEST_HOOKS" );
static const activity_id ACT_WAIT_NPC( "ACT_WAIT_NPC" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_cudgel( "cudgel" );
static const itype_id itype_debug_backpack( "debug_backpack" );
static const itype_id itype_test_audit_hook_widget( "test_audit_hook_widget" );

static const recipe_id recipe_cudgel_test_activity_hooks( "cudgel_test_activity_hooks" );
static const recipe_id recipe_cudgel_test_crafting_eocs( "cudgel_test_crafting_eocs" );

static const trait_id trait_DEBUG_CNF( "DEBUG_CNF" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    talk_effect_t effect( jo, "effect", "audit_activities_crafting_test" );
    finalize_conditions();
    return effect;
}

// ACT-J02: u_assign_activity always built a generic activity, and Character::assign_activity
// refuses (with a debugmsg) any activity that has an actor. The godco dialogue's ACT_WAIT_NPC
// and ACT_GAME, and every zone job, never started.
TEST_CASE( "act-j02_assign_activity_starts_actor_activities", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &u = get_avatar();

    SECTION( "waiting for the NPC you talk to, as the godco dialogue does" ) {
        npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "u_assign_activity": "ACT_WAIT_NPC", )"
                                         R"("duration": "30 minutes" } })" );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        effect.apply( d );
        CHECK( u.activity.id() == ACT_WAIT_NPC );
        CHECK( u.activity.moves_total == to_moves<int>( 30_minutes ) );
    }
    SECTION( "a game without a game item, as Jeremiah offers" ) {
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "u_assign_activity": "ACT_GAME", )"
                                         R"("duration": "45 minutes" } })" );
        dialogue d( get_talker_for( u ), nullptr );
        effect.apply( d );
        CHECK( u.activity.id() == ACT_GAME );
        CHECK( u.activity.moves_total == to_moves<int>( 45_minutes ) );
    }
    SECTION( "a zone job for an NPC" ) {
        npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "npc_assign_activity": )"
                                         R"("ACT_MULTIPLE_FARM" } })" );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        effect.apply( d );
        CHECK( guy.activity.id() == ACT_MULTIPLE_FARM );
    }
    SECTION( "an activity id from a variable" ) {
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "u_assign_activity": )"
                                         R"({ "context_val": "act" } } })" );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "act", ACT_MEDITATE.str() );
        effect.apply( d );
        CHECK( u.activity.id() == ACT_MEDITATE );
        CHECK( u.activity.moves_total == to_moves<int>( 20_minutes ) );
    }
    clear_avatar();
}

// ACT-J02: an activity whose actor needs more than a duration can't be started from an EOC.
// A plain id naming one used to load and then fail every time it ran; now it fails at load.
TEST_CASE( "act-j02_assign_activity_rejects_actors_it_cannot_start", "[audit][activity]" )
{
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "u_assign_activity": "ACT_READ" } })" ),
                     JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "u_assign_activity": "ACT_MULTIPLE_FARM", )"
                                  R"("duration": "1 hours" } })" ), JsonError );
    CHECK_NOTHROW( load_effect( R"({ "effect": { "u_assign_activity": "ACT_GENERIC_EOC", )"
                                R"("duration": "10 minutes" } })" ) );
    CHECK_NOTHROW( load_effect( R"({ "effect": { "u_assign_activity": "ACT_WAIT" } })" ) );
}

static void run_activity_to_the_end( Character &who, const activity_id &type )
{
    int guard = 0;
    while( who.activity.id() == type ) {
        REQUIRE( ++guard < 100000 );
        who.set_moves( 100 );
        who.activity.do_turn( who );
    }
}

// Crafts the recipe in the avatar's hands, with light and no failures.
static void craft_to_the_end( avatar &u, const recipe_id &rid, int batch_size = 1 )
{
    set_time( calendar::turn_zero + 12_hours );
    if( !u.has_trait( trait_DEBUG_CNF ) ) {
        u.toggle_trait( trait_DEBUG_CNF );
    }
    u.learn_recipe( &rid.obj() );
    u.make_craft( rid, batch_size );
    REQUIRE( u.activity.id() == ACT_CRAFT );
    run_activity_to_the_end( u, ACT_CRAFT );
}

// ACT-J03: activity types had only completion_eoc and do_turn_eoc, run with no context, and
// the completion EOC ran before the activity ended, so it couldn't see the result. There
// were no start or cancel hooks.
TEST_CASE( "act-j03_activity_start_completion_and_cancel_hooks", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    avatar &u = get_avatar();

    u.assign_activity( ACT_TEST_HOOKS, 1000 );
    CHECK( globvars.get_global_value( "act_hooks_start" ) == 1 );
    CHECK( globvars.get_global_value( "act_hooks_start_resume" ) == 0 );
    CHECK( globvars.get_global_value( "act_hooks_start_id" ) == "ACT_TEST_HOOKS" );

    SECTION( "completion runs once the activity has ended" ) {
        run_activity_to_the_end( u, ACT_TEST_HOOKS );
        CHECK( globvars.get_global_value( "act_hooks_done" ) == 1 );
        CHECK( globvars.get_global_value( "act_hooks_done_total" ) == 1000 );
        CHECK( !globvars.maybe_get_global_value( "act_hooks_done_while_active" ) );
        CHECK( !globvars.maybe_get_global_value( "act_hooks_cancel" ) );
    }
    SECTION( "canceling runs the cancel hook, not the completion one" ) {
        u.cancel_activity();
        CHECK( globvars.get_global_value( "act_hooks_cancel" ) == 1 );
        CHECK( globvars.get_global_value( "act_hooks_cancel_left" ) == 1000 );
        CHECK( !globvars.maybe_get_global_value( "act_hooks_done" ) );
    }
    clear_avatar();
}

// ACT-J03: a finished craft cleared its activity with cancel_activity(), so it was reported
// as canceled, and a zone job that ran out of work reported nothing at all.
// EOC_character_finished_activity_event_test copies the event into key1 to key3.
TEST_CASE( "act-j03_crafts_and_zone_jobs_report_completion", "[audit][activity]" )
{
    clear_avatar();
    clear_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    avatar &u = get_avatar();

    SECTION( "a finished craft" ) {
        get_map().add_item( u.pos_bub(), item( itype_2x4 ) );
        craft_to_the_end( u, recipe_cudgel_test_activity_hooks );
        CHECK( globvars.get_global_value( "key1" ) == "ACT_CRAFT" );
        CHECK( globvars.get_global_value( "key2" ) == 0 );
        CHECK( globvars.get_global_value( "key3" ) == "activity finished" );
    }
    SECTION( "a zone job with nothing to do" ) {
        u.assign_activity( multi_farm_activity_actor() );
        REQUIRE( u.activity.id() == ACT_MULTIPLE_FARM );
        u.set_moves( 100 );
        u.activity.do_turn( u );
        CHECK( u.activity.is_null() );
        CHECK( globvars.get_global_value( "key1" ) == "ACT_MULTIPLE_FARM" );
        CHECK( globvars.get_global_value( "key2" ) == 0 );
        CHECK( globvars.get_global_value( "key3" ) == "activity finished" );
    }
    clear_avatar();
}

static void load_recipe( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    recipe r;
    r.load( jo, "audit_activities_crafting_test" );
}

// CRAFT-J02: result_eocs ran batch_size times in one bare dialogue, with no recipe or batch
// context and no way to reach the item that was made.
TEST_CASE( "craft-j02_result_eocs_get_context_and_result_items", "[audit][crafting]" )
{
    clear_avatar();
    clear_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    here.add_item( u.pos_bub(), item( itype_2x4 ) );
    here.add_item( u.pos_bub(), item( itype_2x4 ) );

    craft_to_the_end( u, recipe_cudgel_test_crafting_eocs, 2 );

    // result_eocs: once per batch unit, as before, now with context
    CHECK( globvars.get_global_value( "craft_result_runs" ) == 2 );
    CHECK( globvars.get_global_value( "craft_result_index_sum" ) == 1 );
    CHECK( globvars.get_global_value( "craft_result_batch" ) == 2 );
    CHECK( globvars.get_global_value( "craft_result_recipe" ) == "cudgel_test_crafting_eocs" );
    // result_item_eocs: once per cudgel made, with the cudgel as beta
    CHECK( globvars.get_global_value( "craft_result_items" ) == 2 );
    int cudgels = 0;
    int marked = 0;
    const auto tally = [&cudgels, &marked]( const item & it ) {
        if( it.typeId() == itype_cudgel ) {
            ++cudgels;
            if( it.get_var( "made_by_eoc" ) == "yes" ) {
                ++marked;
            }
        }
    };
    for( const item &it : here.i_at( u.pos_bub() ) ) {
        tally( it );
    }
    for( const item *it : u.items_with( []( const item & it ) {
    return it.typeId() == itype_cudgel;
    } ) ) {
        tally( *it );
    }
    CHECK( cudgels == 2 );
    CHECK( marked == 2 );
    clear_avatar();
}

// CRAFT-J02: disassembly ran no EOC at all; result_eocs loaded on uncraft recipes and never ran.
TEST_CASE( "craft-j02_disassembly_runs_disassembly_eocs", "[audit][crafting]" )
{
    clear_avatar();
    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    avatar &u = get_avatar();
    u.worn.wear_item( u, item( itype_debug_backpack ), false, false );
    item_location widget = u.i_add( item( itype_test_audit_hook_widget ) );
    REQUIRE( widget );
    item_location in_progress = u.create_in_progress_disassembly( widget );
    REQUIRE( in_progress );

    u.complete_disassemble( in_progress,
                            recipe_dictionary::get_uncraft( itype_test_audit_hook_widget ) );

    CHECK( globvars.get_global_value( "disassembled" ) == 1 );
    CHECK( globvars.get_global_value( "disassembled_count" ) == 1 );
    CHECK( globvars.get_global_value( "disassembled_item" ) == "test_audit_hook_widget" );
    CHECK( globvars.get_global_value( "disassembled_recipe" ) == "test_audit_hook_widget" );
    clear_avatar();
}

// CRAFT-J02: EOC lists that can never run are load errors.
TEST_CASE( "craft-j02_crafting_eocs_that_never_run_fail_to_load", "[audit][crafting]" )
{
    CHECK_THROWS_AS( load_recipe( R"({ "type": "uncraft", "result": "2x4", "time": "1 s", )"
                                  R"("result_eocs": [ "EOC_TEST_CRAFT_RESULT" ] })" ), JsonError );
    CHECK_THROWS_AS( load_recipe( R"({ "type": "uncraft", "result": "2x4", "time": "1 s", )"
                                  R"("result_item_eocs": [ "EOC_TEST_CRAFT_RESULT_ITEM" ] })" ),
                     JsonError );
    CHECK_THROWS_AS( load_recipe( R"({ "type": "practice", "id": "audit_practice", )"
                                  R"("result_item_eocs": [ "EOC_TEST_CRAFT_RESULT_ITEM" ] })" ),
                     JsonError );
    CHECK_THROWS_AS( load_recipe( R"({ "type": "recipe", "id": "audit_no_result", )"
                                  R"("name": "nothing", "result_eocs": [ "EOC_TEST_CRAFT_RESULT" ], )"
                                  R"("result_item_eocs": [ "EOC_TEST_CRAFT_RESULT_ITEM" ] })" ),
                     JsonError );
}

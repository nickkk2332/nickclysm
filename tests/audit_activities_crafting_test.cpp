#include <functional>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "activity_actor_eoc.h"
#include "activity_handlers.h"
#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_attire.h"
#include "clone_ptr.h"
#include "clzones.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_loader.h"
#include "lightmap.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "talker.h"
#include "type_id.h"

static const activity_id ACT_CRAFT( "ACT_CRAFT" );
static const activity_id ACT_DISASSEMBLE( "ACT_DISASSEMBLE" );
static const activity_id ACT_GAME( "ACT_GAME" );
static const activity_id ACT_MEDITATE( "ACT_MEDITATE" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_TARGET_PRACTICE( "ACT_TARGET_PRACTICE" );
static const activity_id ACT_TEST_CONTEXT( "ACT_TEST_CONTEXT" );
static const activity_id ACT_TEST_HOOKS( "ACT_TEST_HOOKS" );
static const activity_id ACT_WAIT_NPC( "ACT_WAIT_NPC" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_ACT_HOOK_TARGET( "EOC_TEST_ACT_HOOK_TARGET" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_ASSIGN_WITH_CONTEXT( "EOC_TEST_ASSIGN_WITH_CONTEXT" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_LEARN_THE_FAIL_RECIPE( "EOC_TEST_LEARN_THE_FAIL_RECIPE" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_blindfold( "blindfold" );
static const itype_id itype_cudgel( "cudgel" );
static const itype_id itype_debug_backpack( "debug_backpack" );
static const itype_id itype_disassembly( "disassembly" );
static const itype_id itype_test_audit_hook_widget( "test_audit_hook_widget" );

static const quality_id qual_DIG( "DIG" );

static const recipe_id recipe_cudgel_test_activity_hooks( "cudgel_test_activity_hooks" );
static const recipe_id recipe_cudgel_test_craft_fail( "cudgel_test_craft_fail" );
static const recipe_id recipe_cudgel_test_crafting_eocs( "cudgel_test_crafting_eocs" );

static const trait_id trait_DEBUG_CNF( "DEBUG_CNF" );

static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );

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

// ACT-J02 (review): target practice asks the player for a gun, a round count and a target,
// and aims through the avatar (who.as_avatar(), null for an NPC). An EOC could give it to an
// NPC; now that is refused and reported.
TEST_CASE( "act-j02_assign_activity_refuses_avatar_only_activities_for_npcs",
           "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
    REQUIRE( guy.activity.is_null() );

    SECTION( "npc_assign_activity" ) {
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "npc_assign_activity": )"
                                         R"("ACT_TARGET_PRACTICE" } })" );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        const std::string msg = capture_debugmsg_during( [&]() {
            effect.apply( d );
        } );
        CHECK_THAT( msg, Catch::Matchers::Contains( "only the avatar" ) );
    }
    SECTION( "u_assign_activity with an NPC as u" ) {
        const talk_effect_t effect = load_effect(
                                         R"({ "effect": { "u_assign_activity": )"
                                         R"({ "context_val": "act" } } })" );
        dialogue d( get_talker_for( guy ), get_talker_for( u ) );
        d.set_value( "act", "ACT_TARGET_PRACTICE" );
        const std::string msg = capture_debugmsg_during( [&]() {
            effect.apply( d );
        } );
        CHECK_THAT( msg, Catch::Matchers::Contains( "only the avatar" ) );
    }
    CHECK( guy.activity.is_null() );
    CHECK( u.activity.is_null() );
    CHECK( activity_actors::eoc_start_needs_avatar( ACT_TARGET_PRACTICE ) );
    CHECK_FALSE( activity_actors::eoc_start_needs_avatar( ACT_MULTIPLE_FARM ) );
    clear_avatar();
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
        CHECK( globvars.get_global_value( "act_hooks_done_outcome" ) == "completed" );
        CHECK( globvars.get_global_value( "act_hooks_event_outcome" ) == "completed" );
        CHECK( globvars.get_global_value( "act_hooks_done_total" ) == 1000 );
        CHECK( !globvars.maybe_get_global_value( "act_hooks_done_while_active" ) );
        CHECK( !globvars.maybe_get_global_value( "act_hooks_cancel" ) );
    }
    SECTION( "canceling runs the cancel hook, not the completion one" ) {
        u.cancel_activity();
        CHECK( globvars.get_global_value( "act_hooks_cancel" ) == 1 );
        // kept in the backlog to resume, it was interrupted; otherwise canceled
        const bool kept = !u.backlog.empty() && u.backlog.front().id() == ACT_TEST_HOOKS;
        const std::string outcome = kept ? "interrupted" : "canceled";
        CHECK( globvars.get_global_value( "act_hooks_cancel_outcome" ) == outcome );
        CHECK( globvars.get_global_value( "act_hooks_event_outcome" ) == outcome );
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

// ACT-J03 (review): character_finished_activity used to come after the completion EOC, and
// shipped mods rely on that: XedraWood's spirit quest and MindOverMatter's vitaminosis trance
// end their effect in completion_eoc, and an event EOC takes an effect it still finds for a
// cancel. EOC_TEST_ACT_HOOKS_FINISHED records what the hooks had done when the event came.
TEST_CASE( "act-j03_end_hooks_run_before_the_finished_event", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    avatar &u = get_avatar();
    u.assign_activity( ACT_TEST_HOOKS, 1000 );

    SECTION( "a completion" ) {
        run_activity_to_the_end( u, ACT_TEST_HOOKS );
        REQUIRE( globvars.get_global_value( "act_hooks_done" ) == 1 );
        CHECK( globvars.get_global_value( "act_hooks_event_saw_done" ) == 1 );
    }
    SECTION( "a cancel" ) {
        u.cancel_activity();
        REQUIRE( globvars.get_global_value( "act_hooks_cancel" ) == 1 );
        CHECK( globvars.get_global_value( "act_hooks_event_saw_cancel" ) == 1 );
    }
    clear_avatar();
}

namespace
{
// A farm job whose one work spot is ready to work, so the scan stops there instead of
// running out of work.
class ready_farm_spot_actor : public multi_farm_activity_actor
{
    public:
        explicit ready_farm_spot_actor( const tripoint_abs_ms &spot ) : spot( spot ) {}
        std::unordered_set<tripoint_abs_ms> multi_activity_locations( Character & ) override {
            return { spot };
        }
        activity_reason_info multi_activity_can_do( Character &,
                const tripoint_bub_ms & ) override {
            return activity_reason_info::ok( do_activity_reason::NEEDS_TILLING );
        }
        std::unique_ptr<activity_actor> clone() const override {
            return std::make_unique<ready_farm_spot_actor>( *this );
        }
    private:
        tripoint_abs_ms spot;
};

// A farm job whose one work spot needs tilling, a job that takes a digging tool.
class untilled_farm_spot_actor : public multi_farm_activity_actor
{
    public:
        explicit untilled_farm_spot_actor( const tripoint_abs_ms &spot ) : spot( spot ) {}
        std::unordered_set<tripoint_abs_ms> multi_activity_locations( Character & ) override {
            return { spot };
        }
        activity_reason_info multi_activity_can_do( Character &,
                const tripoint_bub_ms & ) override {
            return activity_reason_info::fail( do_activity_reason::NEEDS_TILLING );
        }
        std::unique_ptr<activity_actor> clone() const override {
            return std::make_unique<untilled_farm_spot_actor>( *this );
        }
    private:
        tripoint_abs_ms spot;
};
} // namespace

// ACT-J03 (review): a zone job guessed that it was done whenever the scan ended without a new
// route, so a job that stopped early (too dark here, a fetch that gave up) was reported as
// completed. Only a scan that ran out of work completes it now; an early stop is a cancel, so
// mods that undo a start hook on the end event (MindOverMatter's and Aftershock's crafting
// blindness, Xedra Evolved's mining) still do.
TEST_CASE( "act-j03_zone_job_too_dark_to_work_does_not_complete", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    zone_manager &mgr = zone_manager::get_manager();
    on_out_of_scope clear_zones( [&mgr]() {
        mgr.clear();
        mgr.cache_data();
    } );
    mgr.clear();
    global_variables &globvars = get_globals();
    avatar &u = get_avatar();
    map &here = get_map();
    // off the usual test spots, so the zone scan's cache of sources can't match
    u.setpos( here, tripoint_bub_ms( 43, 47, 0 ) );
    const tripoint_abs_ms spot = here.get_abs( tripoint_bub_ms( 44, 47, 0 ) );
    mgr.add( "plot", zone_type_FARM_PLOT, u.get_faction_id(), false, true, spot, spot );

    u.wear_item( item( itype_blindfold ), false );
    REQUIRE( u.fine_detail_vision_mod() > LIGHT_AMBIENT_DIM );
    u.assign_activity( ready_farm_spot_actor( spot ) );
    REQUIRE( u.activity.id() == ACT_MULTIPLE_FARM );
    globvars.clear_global_values();

    u.set_moves( 100 );
    u.activity.do_turn( u );
    // "It is too dark to work here." ends the job, but the job isn't done: a cancel
    CHECK( u.activity.is_null() );
    CHECK( globvars.get_global_value( "key1" ) == "ACT_MULTIPLE_FARM" );
    CHECK( globvars.get_global_value( "key2" ) == 1 );
    CHECK( globvars.get_global_value( "key3" ) == "activity finished" );
    clear_avatar();
}

// ACT-J03 (review): a work spot skipped because the items or tools it needs aren't in reach
// still has work, but the scan then ran out of spots and the job was reported as completed.
TEST_CASE( "act-j03_zone_job_missing_its_tools_does_not_complete", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    zone_manager &mgr = zone_manager::get_manager();
    on_out_of_scope clear_zones( [&mgr]() {
        mgr.clear();
        mgr.cache_data();
    } );
    mgr.clear();
    global_variables &globvars = get_globals();
    avatar &u = get_avatar();
    map &here = get_map();
    // off the usual test spots, so the zone scan's cache of sources can't match
    u.setpos( here, tripoint_bub_ms( 41, 45, 0 ) );
    const tripoint_abs_ms spot = here.get_abs( tripoint_bub_ms( 42, 45, 0 ) );
    mgr.add( "plot", zone_type_FARM_PLOT, u.get_faction_id(), false, true, spot, spot );

    // no digging tool on the avatar or anywhere near
    REQUIRE( u.max_quality( qual_DIG ) < 1 );
    u.assign_activity( untilled_farm_spot_actor( spot ) );
    REQUIRE( u.activity.id() == ACT_MULTIPLE_FARM );
    globvars.clear_global_values();

    u.set_moves( 100 );
    u.activity.do_turn( u );
    // the job stops, but the plot still needs tilling: a cancel, not a completion
    CHECK( u.activity.is_null() );
    CHECK( globvars.get_global_value( "key1" ) == "ACT_MULTIPLE_FARM" );
    CHECK( globvars.get_global_value( "key2" ) == 1 );
    CHECK( globvars.get_global_value( "key3" ) == "activity finished" );
    clear_avatar();
}

// ACT-J03 (review): disassemble_activity_actor::start takes the item it works on off
// act.targets, so the hooks' beta was an item still queued, or none for a single item.
// EOC_TEST_ACT_HOOK_TARGET marks its beta.
TEST_CASE( "act-j03_disassembly_hooks_get_the_item_being_taken_apart", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    u.worn.wear_item( u, item( itype_debug_backpack ), false, false );
    item_location queued = u.i_add( item( itype_test_audit_hook_widget ) );
    item_location taken_apart_first = u.i_add( item( itype_test_audit_hook_widget ) );
    REQUIRE( queued );
    REQUIRE( taken_apart_first );

    effect_on_condition_id &start_eoc =
        const_cast<effect_on_condition_id &>( ACT_DISASSEMBLE->start_EOC );
    restore_on_out_of_scope<effect_on_condition_id> restore_start_eoc( start_eoc );
    start_eoc = effect_on_condition_EOC_TEST_ACT_HOOK_TARGET;

    // the last target is taken apart first
    player_activity act( disassemble_activity_actor( 100 ) );
    act.targets.push_back( queued );
    act.targets.push_back( taken_apart_first );
    u.assign_activity( act );
    REQUIRE( u.activity.id() == ACT_DISASSEMBLE );
    const disassemble_activity_actor *actor =
        dynamic_cast<const disassemble_activity_actor *>( u.activity.actor.get() );
    REQUIRE( actor != nullptr );
    REQUIRE( actor->target );
    CHECK( actor->target->typeId() == itype_disassembly );
    CHECK( actor->target->get_var( "act_hook_target" ) == "yes" );
    REQUIRE( queued );
    CHECK_FALSE( queued->has_var( "act_hook_target" ) );
    u.cancel_activity();
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
    // start_eocs ran once, when the craft started, with the craft in progress as beta
    CHECK( globvars.get_global_value( "craft_start_runs" ) == 1 );
    CHECK( globvars.get_global_value( "craft_start_batch" ) == 2 );
    CHECK( globvars.get_global_value( "craft_start_recipe" ) == "cudgel_test_crafting_eocs" );
    CHECK( globvars.get_global_value( "craft_start_had_beta" ) == 1 );
    // character_crafts_item, once for the batch
    CHECK( globvars.get_global_value( "crafted_events" ) == 1 );
    CHECK( globvars.get_global_value( "crafted_recipe" ) == "cudgel_test_crafting_eocs" );
    CHECK( globvars.get_global_value( "crafted_itype" ) == "cudgel" );
    CHECK( globvars.get_global_value( "crafted_quantity" ) == 2 );
    clear_avatar();
}

// CRAFT-J02: a craft that failed partway, losing components and progress, ran no EOC.
TEST_CASE( "craft-j02_fail_eocs_run_when_a_craft_fails", "[audit][crafting]" )
{
    clear_avatar();
    clear_map();
    set_time( calendar::turn_zero + 12_hours );
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    here.add_item( u.pos_bub(), item( itype_2x4 ) );
    // No DEBUG_CNF and no skill, against difficulty 10: the first step fails, and with one
    // component the failure takes it, leaving nothing to craft from
    u.learn_recipe( &recipe_cudgel_test_craft_fail.obj() );
    u.make_craft( recipe_cudgel_test_craft_fail, 1 );
    REQUIRE( u.activity.id() == ACT_CRAFT );
    run_activity_to_the_end( u, ACT_CRAFT );

    REQUIRE( globvars.maybe_get_global_value( "craft_fail_runs" ) != nullptr );
    CHECK( globvars.get_global_value( "craft_fail_runs" ).dbl() >= 1 );
    CHECK( globvars.get_global_value( "craft_fail_recipe" ) == "cudgel_test_craft_fail" );
    CHECK( globvars.get_global_value( "craft_fail_components_lost" ) == 1 );
    CHECK( globvars.get_global_value( "craft_fail_destroyed" ) == 1 );
    CHECK( globvars.get_global_value( "craft_fail_had_beta" ) == 1 );
    // nothing was made
    CHECK( !globvars.maybe_get_global_value( "crafted_events" ) );
    clear_avatar();
}

// CRAFT-J02: learning a recipe sent no event.
TEST_CASE( "craft-j02_learning_a_new_recipe_sends_an_event", "[audit][crafting]" )
{
    clear_avatar();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    avatar &u = get_avatar();
    REQUIRE_FALSE( u.knows_recipe( &recipe_cudgel_test_craft_fail.obj() ) );

    dialogue d( get_talker_for( u ), nullptr );
    effect_on_condition_EOC_TEST_LEARN_THE_FAIL_RECIPE->activate( d );
    CHECK( u.knows_recipe( &recipe_cudgel_test_craft_fail.obj() ) );
    CHECK( globvars.get_global_value( "learned_events" ) == 1 );
    CHECK( globvars.get_global_value( "learned_recipe" ) == "cudgel_test_craft_fail" );
    CHECK( globvars.get_global_value( "learned_how" ) == "eoc" );

    // a recipe already known isn't learned again
    effect_on_condition_EOC_TEST_LEARN_THE_FAIL_RECIPE->activate( d );
    CHECK( globvars.get_global_value( "learned_events" ) == 1 );
    clear_avatar();
}

// ACT-J03: u_assign_activity could give the activity nothing, so its EOCs couldn't tell what
// they were for. Its "context" now reaches the activity's EOCs, and is saved with it.
TEST_CASE( "act-j03_u_assign_activity_context_reaches_the_activity_eocs", "[audit][activity]" )
{
    clear_avatar();
    clear_map_without_vision();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    globvars.set_global_value( "act_ctx_source", std::string( "a global" ) );
    avatar &u = get_avatar();

    dialogue d( get_talker_for( u ), nullptr );
    effect_on_condition_EOC_TEST_ASSIGN_WITH_CONTEXT->activate( d );
    REQUIRE( u.activity.id() == ACT_TEST_CONTEXT );
    CHECK( globvars.get_global_value( "act_ctx_start_note" ) == "from the eoc" );
    CHECK( u.activity.eoc_context.count( "act_note" ) == 1 );

    SECTION( "the completion EOC gets it, evaluated when it was assigned" ) {
        globvars.set_global_value( "act_ctx_source", std::string( "changed since" ) );
        run_activity_to_the_end( u, ACT_TEST_CONTEXT );
        CHECK( globvars.get_global_value( "act_ctx_done_note" ) == "from the eoc" );
        CHECK( globvars.get_global_value( "act_ctx_done_global" ) == "a global" );
        CHECK( globvars.get_global_value( "act_ctx_done_outcome" ) == "completed" );
    }
    SECTION( "the cancel EOC gets it" ) {
        u.cancel_activity();
        CHECK( globvars.get_global_value( "act_ctx_cancel_note" ) == "from the eoc" );
    }
    SECTION( "it is saved with the activity" ) {
        std::ostringstream saved;
        JsonOut json( saved );
        u.activity.serialize( json );
        player_activity loaded;
        loaded.deserialize( json_loader::from_string( saved.str() ) );
        REQUIRE( loaded.eoc_context.count( "act_note" ) == 1 );
        CHECK( loaded.eoc_context.at( "act_note" ).str() == "from the eoc" );
    }
    // an activity assigned later, by anything else, gets none of it
    u.cancel_activity();
    u.assign_activity( ACT_TEST_HOOKS, 1000 );
    CHECK( u.activity.eoc_context.empty() );
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
    // and the event, after the EOCs
    CHECK( globvars.get_global_value( "disassembled_events" ) == 1 );
    CHECK( globvars.get_global_value( "disassembled_event_itype" ) == "test_audit_hook_widget" );
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

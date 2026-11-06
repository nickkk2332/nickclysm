#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_oracle.h"
#include "coordinates.h"
#include "errand.h"
#include "flexbuffer_json.h"
#include "itype.h"
#include "json_loader.h"
#include "map.h"
#include "npc.h"
#include "npc_ai_hooks_helpers.h"
#include "npc_ai_tuning.h"
#include "npc_arbitration.h"
#include "npc_class.h"
#include "npc_json_goal.h"
#include "npc_sleep.h"
#include "point.h"
#include "stomach.h"
#include "type_id.h"

// NPC-J10: the npc_class "ai_tuning": its defaults are the constants it replaced, a class's
// numbers change when its NPCs act, copies inherit them, and the load checks them.

static const npc_class_id NC_DOCTOR( "NC_DOCTOR" );
static const npc_class_id test_npc_ai_tuned( "test_npc_ai_tuned" );
static const npc_class_id test_npc_ai_tuned_child( "test_npc_ai_tuned_child" );

static void check_same_tuning( const npc_ai_tuning &a, const npc_ai_tuning &b )
{
    for( const char *goal : {
             "drink_water", "eat_food", "go_to_sleep", "regulate_temperature"
         } ) {
        CAPTURE( goal );
        CHECK( a.need( goal )->act == b.need( goal )->act );
        CHECK( a.need( goal )->insist == b.need( goal )->insist );
        CHECK( a.need( goal )->emergency == b.need( goal )->emergency );
    }
    CHECK( a.need_critical_urgency == b.need_critical_urgency );
    CHECK( a.duty == b.duty );
    CHECK( a.duty_max == b.duty_max );
    CHECK( a.follow_max == b.follow_max );
    CHECK( a.goto_order == b.goto_order );
    CHECK( a.camp_work == b.camp_work );
    CHECK( a.return_to_camp == b.return_to_camp );
    CHECK( a.free_time == b.free_time );
    CHECK( a.starvation_hunger == b.starvation_hunger );
    CHECK( a.camp_meal_kcal == b.camp_meal_kcal );
    CHECK( a.settle_interval == b.settle_interval );
    CHECK( a.settle_timeout == b.settle_timeout );
    CHECK( a.stall_wakes == b.stall_wakes );
    CHECK( a.stall_window == b.stall_window );
    CHECK( a.stall_duration == b.stall_duration );
}

TEST_CASE( "npc_ai_tuning_defaults_are_the_old_constants", "[npc][npc_ai_hooks][npc_tuning]" )
{
    const npc_ai_tuning &t = npc_ai_tuning::defaults();
    // the constants the code had
    CHECK( t.thirst_act_level() == 520 );
    CHECK( t.thirst_act_level() == npc_thirst_act_level );
    CHECK( t.drink_water.act == npc_thirst_act_level / 1200.0f );
    CHECK( t.drink_water.insist == 800.0f / 1200.0f );
    CHECK( t.drink_water.emergency == need_critical_urgency );
    CHECK( t.eat_food.act == food_act_level );
    CHECK( t.eat_food.insist == 0.4f );
    CHECK( t.eat_food.emergency == 0.6f );
    CHECK( t.tired_level() == static_cast<int>( sleepiness_levels::TIRED ) );
    CHECK( t.go_to_sleep.act == static_cast<int>( sleepiness_levels::TIRED ) / 1000.0f );
    CHECK( t.go_to_sleep.insist == static_cast<int>( sleepiness_levels::DEAD_TIRED ) / 1000.0f );
    CHECK( t.go_to_sleep.emergency == static_cast<int>( sleepiness_levels::EXHAUSTED ) / 1000.0f );
    CHECK( t.regulate_temperature.act == need_critical_urgency );
    CHECK( t.regulate_temperature.emergency == need_critical_urgency );
    CHECK( t.need_critical_urgency == 0.75f );
    CHECK( t.duty == 0.45f );
    CHECK( t.duty_max == 0.5f );
    CHECK( t.follow_max == 0.6f );
    CHECK( t.goto_order == 0.65f );
    CHECK( t.camp_work == 0.4f );
    CHECK( t.return_to_camp == 0.5f );
    CHECK( t.free_time == 0.35f );
    CHECK( t.starvation_hunger == 300 );
    CHECK( t.camp_meal_kcal == 500 );
    CHECK( t.settle_interval == 3_minutes );
    CHECK( t.settle_timeout == 30_minutes );
    CHECK( t.stall_wakes == 3 );
    CHECK( t.stall_window == 30_minutes );
    CHECK( t.stall_duration == 30_minutes );
    CHECK( t.problems().empty() );

    // no class but the test ones sets any
    for( const npc_class &cls : npc_class::get_all() ) {
        if( cls.id == test_npc_ai_tuned || cls.id == test_npc_ai_tuned_child ) {
            continue;
        }
        CAPTURE( cls.id.str() );
        check_same_tuning( cls.get_ai_tuning(), t );
    }
    // the floors before tuning
    const npc_permission_verdict discouraged{ npc_permission::discouraged, npc_obligation::none, 0.0f };
    CHECK( npc_need_floor( npc::need_goal_id::drink_water,
                           discouraged ) == Approx( 800.0f / 1200.0f ) );
    CHECK( npc_need_floor( npc::need_goal_id::eat_food, { npc_permission::emergency_only, npc_obligation::none, 0.0f } )
           == 0.6f );
}

TEST_CASE( "npc_ai_tuning_from_the_class_and_its_copies", "[npc][npc_ai_hooks][npc_tuning]" )
{
    const npc_ai_tuning &t = test_npc_ai_tuned->get_ai_tuning();
    CHECK( t.drink_water.act == 0.25f );
    CHECK( t.thirst_act_level() == 300 );
    CHECK( t.tired_level() == 100 );
    CHECK( t.need_critical_urgency == 0.5f );
    CHECK( t.goto_order == 0.7f );
    CHECK( t.free_time == 0.1f );
    CHECK( t.starvation_hunger == 100 );
    CHECK( t.camp_meal_kcal == 800 );
    CHECK( t.settle_interval == 1_minutes );
    CHECK( t.settle_timeout == 5_minutes );
    CHECK( t.stall_wakes == 2 );
    CHECK( t.stall_window == 10_minutes );
    CHECK( t.stall_duration == 1_hours );
    // a copy keeps what it doesn't change
    const npc_ai_tuning &child = test_npc_ai_tuned_child->get_ai_tuning();
    CHECK( child.goto_order == 0.8f );
    npc_ai_tuning expected = t;
    expected.goto_order = 0.8f;
    check_same_tuning( child, expected );
}

TEST_CASE( "npc_ai_tuning_changes_when_the_npc_acts", "[npc][npc_ai_hooks][npc_tuning]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    const npc_class_id vanilla = guy.myclass;
    const behavior::character_oracle_t oracle( &guy );
    const auto tuned = [&guy]( const bool on ) {
        guy.myclass = on ? test_npc_ai_tuned : NC_DOCTOR;
    };
    REQUIRE( vanilla == NC_DOCTOR );
    CHECK( &ai_tuning_of( guy ) == &vanilla->get_ai_tuning() );

    SECTION( "thirst" ) {
        guy.set_thirst( 400 );
        CHECK( oracle.needs_water_badly( "" ) == behavior::status_t::success );
        CHECK_FALSE( guy.intake_useful( npc::need_goal_id::drink_water ) );
        tuned( true );
        CHECK( oracle.needs_water_badly( "" ) == behavior::status_t::running );
        CHECK( guy.intake_useful( npc::need_goal_id::drink_water ) );
    }
    SECTION( "tiredness" ) {
        guy.set_sleepiness( 150 );
        CHECK( oracle.needs_sleep_badly( "" ) == behavior::status_t::success );
        tuned( true );
        CHECK( oracle.needs_sleep_badly( "" ) == behavior::status_t::running );
    }
    SECTION( "hunger" ) {
        // short of calories by 15%, counting what it has eaten: the default acts from 20%
        const int pending = guy.stomach.get_calories() + guy.guts.get_calories();
        guy.set_stored_kcal( guy.get_healthy_kcal() * 85 / 100 - pending );
        CHECK_FALSE( calories_short( guy ) );
        CHECK( oracle.needs_food_badly( "" ) == behavior::status_t::success );
        tuned( true );
        CHECK( calories_short( guy ) );
        CHECK( oracle.needs_food_badly( "" ) == behavior::status_t::running );
    }
    SECTION( "starving" ) {
        // burning stored calories, a meal on its way so not short of them
        guy.set_stored_kcal( guy.get_healthy_kcal() * 4 / 10 );
        guy.guts.mod_calories( guy.get_healthy_kcal() * 6 / 10 );
        guy.set_hunger( 200 );
        REQUIRE( guy.get_starvation() > base_metabolic_rate );
        CHECK( oracle.needs_food_badly( "" ) == behavior::status_t::success );
        tuned( true );
        REQUIRE_FALSE( calories_short( guy ) );
        CHECK( oracle.needs_food_badly( "" ) == behavior::status_t::running );
    }
    SECTION( "duty" ) {
        guy.set_mission( NPC_MISSION_GUARD );
        guy.guard_pos = guy.pos_abs();
        CHECK( oracle.duty_urgency( "" ) == 0.45f );
        tuned( true );
        CHECK( oracle.duty_urgency( "" ) == 0.2f );
        guy.guard_pos = guy.pos_abs() + tripoint( 20, 0, 0 );
        CHECK( oracle.duty_urgency( "" ) == 0.3f );
        tuned( false );
        CHECK( oracle.duty_urgency( "" ) == 0.5f );
        // an option away from the post waits for the need's emergency level
        const npc_need_view view = guy.need_view( npc::need_goal_id::drink_water,
                                   npc_option_cost::stay( guy.pos_abs() + tripoint( 10, 0, 0 ) ) );
        REQUIRE( view.verdict.permission == npc_permission::emergency_only );
        CHECK( view.floor == need_critical_urgency );
        tuned( true );
        CHECK( guy.need_view( npc::need_goal_id::drink_water,
                              npc_option_cost::stay( guy.pos_abs() + tripoint( 10, 0, 0 ) ) ).floor == 0.5f );
    }
    SECTION( "following and orders" ) {
        guy.set_attitude( NPCATT_FOLLOW );
        get_avatar().setpos( get_map(), npc_spot + tripoint( 40, 0, 0 ) );
        CHECK( oracle.npc_following_urgency( "" ) == 0.6f );
        tuned( true );
        CHECK( oracle.npc_following_urgency( "" ) == 0.4f );
        errand::set_walk( guy, errand::move_order( guy.pos_abs() + tripoint( 10, 0, 0 ) ) );
        CHECK( oracle.npc_goto_order_urgency( "" ) == 0.7f );
        tuned( false );
        CHECK( oracle.npc_goto_order_urgency( "" ) == 0.65f );
        errand::drop_walk( guy );
    }
    SECTION( "a critical need" ) {
        // a need at 0.6 against one pursued at 0.5, too close for the margin
        npc_pursuit current;
        current.goal = "eat_food";
        current.cost = npc_interrupt_cost::costly;
        current.selection = 0.5f;
        const npc_challenger thirst{ "drink_water", 0.6f };
        CHECK_FALSE( judge_interrupt( current, thirst ).interrupt );
        CHECK_FALSE( judge_interrupt( current, thirst,
                                      ai_tuning_of( guy ).need_critical_urgency ).interrupt );
        tuned( true );
        CHECK( judge_interrupt( current, thirst, ai_tuning_of( guy ).need_critical_urgency ).interrupt );
    }
    SECTION( "settling down to sleep" ) {
        npc_sleep_session ses;
        const npc_ai_tuning &vanilla_tuning = ai_tuning_of( guy );
        tuned( true );
        const npc_ai_tuning &t = ai_tuning_of( guy );
        ses.begin_settling( t );
        CHECK( ses.next_attempt == calendar::turn + 1_minutes );
        calendar::turn += 3_minutes;
        CHECK( ses.patience( t ) == 3 );
        CHECK( ses.patience( vanilla_tuning ) == 1 );
        // two wakes stall it, for an hour
        ses.note_wake( t );
        CHECK_FALSE( ses.stalled() );
        ses.begin_settling( t );
        ses.note_wake( t );
        CHECK( ses.stalled() );
        CHECK( ses.stalled_until == calendar::turn + 1_hours );
        // half way to tired, from its own tired
        CHECK( ses.wants_resume( 60, t ) );
        CHECK_FALSE( ses.wants_resume( 60, vanilla_tuning ) );
        npc_sleep_session plain;
        plain.begin_settling( vanilla_tuning );
        plain.note_wake( vanilla_tuning );
        plain.begin_settling( vanilla_tuning );
        plain.note_wake( vanilla_tuning );
        CHECK_FALSE( plain.stalled() );
    }
    tuned( false );
}

static void load_tuning( npc_ai_tuning &t, const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    t.load( jo );
}

TEST_CASE( "npc_ai_tuning_load_validation", "[npc][npc_ai_hooks][npc_tuning]" )
{
    npc_ai_tuning t;
    // what it gives changes, nothing else
    load_tuning( t, R"({ "needs": { "drink_water": { "act": 0.5 } }, "urgency": { "duty": 0.3 } })" );
    CHECK( t.drink_water.act == 0.5f );
    CHECK( t.drink_water.insist == npc_ai_tuning::defaults().drink_water.insist );
    CHECK( t.duty == 0.3f );
    CHECK( t.duty_max == 0.5f );
    CHECK( t.free_time == 0.35f );

    const auto rejects = []( const std::string & json, const std::string & why ) {
        CAPTURE( json );
        npc_ai_tuning fresh;
        CHECK_THROWS_WITH( load_tuning( fresh, json ), Catch::Matchers::Contains( why ) );
    };
    // levels that go down, or out of range
    rejects( R"({ "needs": { "eat_food": { "act": 0.5 } } })", "must not go down" );
    rejects( R"({ "needs": { "go_to_sleep": { "emergency": 0.3 } } })", "must not go down" );
    rejects( R"({ "needs": { "drink_water": { "emergency": 1.5 } } })", "not between 0 and 1" );
    rejects( R"({ "need_critical_urgency": -0.1 })", "not between 0 and 1" );
    rejects( R"({ "urgency": { "goto": 2 } })", "not between 0 and 1" );
    rejects( R"({ "urgency": { "duty": 0.6 } })", "above urgency.duty_max" );
    rejects( R"({ "needs": { "drink_blood": { "act": 0.1 } } })", "isn't a need" );
    rejects( R"({ "starvation_hunger": -1 })", "below 0" );
    rejects( R"({ "camp_meal_kcal": -5 })", "below 0" );
    rejects( R"({ "sleep": { "stall_wakes": 0 } })", "below 1" );
    rejects( R"({ "sleep": { "settle_timeout": "0 seconds" } })", "a turn or longer" );

    // through a class: the error stops its load
    npc_class cls;
    const JsonObject jo = json_loader::from_string(
                              R"({ "name": "x", "job_description": "y", "ai_tuning": { "needs": { "eat_food": { "insist": 0.9 } } } })" );
    jo.allow_omitted_members();
    CHECK_THROWS_WITH( cls.load( jo, "npc_ai_hooks_test" ),
                       Catch::Matchers::Contains( "must not go down" ) );
}

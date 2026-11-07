#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "achievement.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "cata_variant.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "effect_source.h"
#include "event.h"
#include "event_bus.h"
#include "event_statistics.h"
#include "event_subscriber.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "math_parser_type.h"
#include "monster.h"
#include "npc.h"
#include "past_achievements_info.h"
#include "path_info.h"
#include "player_helpers.h"
#include "point.h"
#include "stats_tracker.h"
#include "talker.h"
#include "type_id.h"

static const achievement_id achievement_achievement_arcade_mode( "achievement_arcade_mode" );
static const achievement_id achievement_achievement_kill_zombie( "achievement_kill_zombie" );
static const achievement_id achievement_audit_m03a_conduct( "audit_m03a_conduct" );
static const achievement_id achievement_audit_m03a_given( "audit_m03a_given" );
static const achievement_id achievement_audit_m03a_late( "audit_m03a_late" );
static const achievement_id achievement_audit_m03a_unlocker( "audit_m03a_unlocker" );
static const achievement_id achievement_audit_m03a_untracked( "audit_m03a_untracked" );

static const effect_on_condition_id
effect_on_condition_EOC_audit_m03a_fail( "EOC_audit_m03a_fail" );
static const effect_on_condition_id
effect_on_condition_EOC_audit_m03a_give( "EOC_audit_m03a_give" );

static const efftype_id effect_audit_m03a_decaying( "audit_m03a_decaying" );
static const efftype_id effect_audit_m03a_hooked( "audit_m03a_hooked" );

static const event_statistic_id event_statistic_num_broken_left_arm( "num_broken_left_arm" );

static const mtype_id mon_zombie( "mon_zombie" );

static const score_id score_audit_m03a_late_score( "audit_m03a_late_score" );
static const score_id score_audit_m03a_score( "audit_m03a_score" );

namespace
{
// A creature's numeric variable, 0 when it has none
double var_of( const Creature &c, const std::string &key )
{
    const diag_value *v = c.maybe_get_value( key );
    return v ? v->dbl() : 0.0;
}

double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// Puts back the game's statistics for these event types
struct event_stats_restorer {
    std::vector<std::pair<event_type, event_multiset>> saved;
    explicit event_stats_restorer( const std::vector<event_type> &types ) {
        for( event_type type : types ) {
            saved.emplace_back( type, get_stats().get_events( type ) );
        }
    }
    ~event_stats_restorer() {
        for( const std::pair<event_type, event_multiset> &events : saved ) {
            get_stats().get_events( events.first ) = events.second;
        }
    }
};

// Global variables are shared by every test: put back what was there
struct globals_restorer {
    global_variables::impl_t saved = get_globals().get_global_values();
    ~globals_restorer() {
        get_globals().set_global_values( saved );
    }
};

monster &spawn_zombie()
{
    return spawn_test_monster( mon_zombie.str(), get_avatar().pos_bub() + tripoint::east * 2 );
}
} // namespace

// EVT-J04: only characters' effects sent events, and effect types had no EOCs of their own, so
// nothing could react to a monster gaining or losing an effect.  apply_eocs,
// intensity_change_eocs and remove_eocs run for any creature, with the effect's cause as npc.
TEST_CASE( "evt-j04_effect_eocs_run_for_a_monster", "[audit][effect]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    monster &zed = spawn_zombie();
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_avatar();
    } );

    zed.add_effect( effect_source( &u ), effect_audit_m03a_hooked, 1_hours );
    REQUIRE( zed.has_effect( effect_audit_m03a_hooked ) );
    CHECK( var_of( zed, "audit_m03a_applied" ) == 1 );
    CHECK( var_of( zed, "audit_m03a_named" ) == 1 );
    CHECK( var_of( zed, "audit_m03a_new" ) == 1 );
    CHECK( var_of( zed, "audit_m03a_old" ) == 0 );
    CHECK( var_of( zed, "audit_m03a_changed" ) == 0 );
    // The avatar applied it
    CHECK( var_of( u, "audit_m03a_caused" ) == 1 );

    // Applied again, at a higher intensity
    zed.add_effect( effect_source( &u ), effect_audit_m03a_hooked, 1_hours, false, 3 );
    CHECK( zed.get_effect_int( effect_audit_m03a_hooked ) == 3 );
    CHECK( var_of( zed, "audit_m03a_applied" ) == 1 );
    CHECK( var_of( zed, "audit_m03a_changed" ) == 1 );
    CHECK( var_of( zed, "audit_m03a_new" ) == 3 );
    CHECK( var_of( zed, "audit_m03a_old" ) == 1 );
    // The same intensity again changes nothing
    zed.add_effect( effect_source( &u ), effect_audit_m03a_hooked, 1_hours, false, 3 );
    CHECK( var_of( zed, "audit_m03a_changed" ) == 1 );

    CHECK( zed.remove_effect( effect_audit_m03a_hooked ) );
    CHECK( var_of( zed, "audit_m03a_removed" ) == 1 );
    CHECK( var_of( zed, "audit_m03a_new" ) == 0 );
    CHECK( var_of( zed, "audit_m03a_old" ) == 3 );
    CHECK( var_of( u, "audit_m03a_caused" ) == 2 );
    // Nothing left to remove
    CHECK_FALSE( zed.remove_effect( effect_audit_m03a_hooked ) );
    CHECK( var_of( zed, "audit_m03a_removed" ) == 1 );
}

// EVT-J04: on a character, per body part; an effect with no cause has no npc
TEST_CASE( "evt-j04_effect_eocs_run_per_body_part", "[audit][effect]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );

    u.add_effect( effect_source::empty(), effect_audit_m03a_hooked, 1_hours, body_part_torso );
    CHECK( var_of( u, "audit_m03a_applied" ) == 1 );
    CHECK( var_of( u, "audit_m03a_on_torso" ) == 1 );
    CHECK( var_of( u, "audit_m03a_caused" ) == 0 );
    u.add_effect( effect_source::empty(), effect_audit_m03a_hooked, 1_hours, body_part_arm_l );
    CHECK( var_of( u, "audit_m03a_applied" ) == 2 );
    CHECK( var_of( u, "audit_m03a_on_torso" ) == 1 );

    // Removing it from every part loses it twice
    CHECK( u.remove_effect( effect_audit_m03a_hooked ) );
    CHECK_FALSE( u.has_effect( effect_audit_m03a_hooked ) );
    CHECK( var_of( u, "audit_m03a_removed" ) == 2 );
    CHECK( var_of( u, "audit_m03a_caused" ) == 0 );
}

// EVT-J04: a copy inherits the EOCs and can drop some; while effects are being processed the
// EOCs wait for the loop to end (one removing an effect could otherwise break it), then run
TEST_CASE( "evt-j04_effect_eocs_inherit_and_wait_for_processing", "[audit][effect]" )
{
    REQUIRE( effect_audit_m03a_decaying->apply_eocs.empty() );
    REQUIRE( effect_audit_m03a_decaying->intensity_change_eocs ==
             effect_audit_m03a_hooked->intensity_change_eocs );
    REQUIRE( effect_audit_m03a_decaying->remove_eocs == effect_audit_m03a_hooked->remove_eocs );
    REQUIRE( effect_audit_m03a_hooked->apply_eocs.size() == 1 );

    clear_avatar();
    clear_map();
    monster &zed = spawn_zombie();
    on_out_of_scope cleanup( []() {
        clear_creatures();
    } );

    zed.add_effect( effect_source::empty(), effect_audit_m03a_decaying, 1_hours, false, 3 );
    REQUIRE( zed.get_effect_int( effect_audit_m03a_decaying ) == 3 );
    CHECK( var_of( zed, "audit_m03a_applied" ) == 0 );
    // It loses one intensity a turn
    zed.process_effects();
    CHECK( zed.get_effect_int( effect_audit_m03a_decaying ) == 2 );
    CHECK( var_of( zed, "audit_m03a_changed" ) == 1 );
    CHECK( var_of( zed, "audit_m03a_new" ) == 2 );
    CHECK( var_of( zed, "audit_m03a_old" ) == 3 );

    // Held, an EOC waits for the last release
    Creature::hold_effect_eocs();
    Creature::hold_effect_eocs();
    zed.add_effect( effect_source::empty(), effect_audit_m03a_hooked, 1_hours );
    const double applied_while_held = var_of( zed, "audit_m03a_applied" );
    Creature::release_effect_eocs();
    const double applied_after_one_release = var_of( zed, "audit_m03a_applied" );
    Creature::release_effect_eocs();
    CHECK( applied_while_held == 0 );
    CHECK( applied_after_one_release == 0 );
    CHECK( var_of( zed, "audit_m03a_applied" ) == 1 );
}

// EVT-J04: broken_bone was sent only for the avatar, though it is documented for any
// character, so an NPC's broken limbs were invisible.  The avatar's statistics still count
// only the avatar's.
TEST_CASE( "evt-j04_npc_broken_bones_send_events", "[audit][event]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    event_stats_restorer restore_stats( { event_type::broken_bone } );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    npc &guy = spawn_npc( get_avatar().pos_bub().xy() + point::east * 2, "thug" );
    clear_character( guy );

    struct bone_events : event_subscriber {
        using event_subscriber::notify;
        std::vector<character_id> broken;
        void notify( const cata::event &e ) override {
            if( e.type() == event_type::broken_bone ) {
                broken.push_back( e.get<character_id>( "character" ) );
            }
        }
    } events;
    get_event_bus().subscribe( &events );

    const cata_variant before = get_stats().value_of( event_statistic_num_broken_left_arm );
    guy.set_part_hp_cur( body_part_arm_l, 0 );
    REQUIRE( events.broken.size() == 1 );
    CHECK( events.broken[0] == guy.getID() );
    // Already broken: nothing more
    guy.set_part_hp_cur( body_part_arm_l, 0 );
    CHECK( events.broken.size() == 1 );
    guy.mod_part_hp_cur( body_part_arm_r, -guy.get_part_hp_cur( body_part_arm_r ) );
    CHECK( events.broken.size() == 2 );
    CHECK( get_stats().value_of( event_statistic_num_broken_left_arm ) == before );
}

// EVT-J05: achievements and scores were fixed to those present when the game started, so a
// mod added to a save tracked none.  One with "allow_late_tracking" is tracked from its first
// load, counting everything since the game started.
TEST_CASE( "evt-j05_late_tracking_achievements_and_scores", "[audit][achievement]" )
{
    const auto ignore = []( const achievement *, bool ) {};
    const character_id u_id = get_avatar().getID();
    event_bus b;
    stats_tracker s;
    b.subscribe( &s );
    // A game that started before the test achievements and scores existed
    const JsonValue saved_stats = json_loader::from_string(
                                      R"({"data":{},"initial_scores":["score_broken_bones"]})" );
    s.deserialize( saved_stats.get_object() );
    b.send<event_type::game_avatar_new>( true, false, u_id, "Audit EVT-J05", "CUSTOM_PROFESSION" );
    b.send<event_type::character_kills_monster>( u_id, mon_zombie, 0 );

    std::vector<score_id> scores;
    for( const score *scr : s.valid_scores() ) {
        scores.push_back( scr->id );
    }
    const auto has_score = [&scores]( const score_id & id ) {
        return std::find( scores.begin(), scores.end(), id ) != scores.end();
    };
    CHECK( has_score( score_audit_m03a_late_score ) );
    CHECK_FALSE( has_score( score_audit_m03a_score ) );

    achievements_tracker a( s, ignore, ignore, true );
    b.subscribe( &a );
    const JsonValue saved_achievements = json_loader::from_string(
            R"({"enabled":true,"initial_achievements":["achievement_kill_zombie"],"achievements_status":{}})" );
    a.deserialize( saved_achievements.get_object() );
    CHECK( a.is_tracked( achievement_achievement_kill_zombie ) );
    CHECK( a.is_tracked( achievement_audit_m03a_late ) );
    CHECK_FALSE( a.is_tracked( achievement_audit_m03a_untracked ) );
    std::vector<achievement_id> valid;
    for( const achievement *ach : a.valid_achievements() ) {
        valid.push_back( ach->id );
    }
    CHECK( std::find( valid.begin(), valid.end(), achievement_audit_m03a_late ) != valid.end() );
    CHECK( std::find( valid.begin(), valid.end(), achievement_audit_m03a_untracked ) == valid.end() );

    // The kill before it was tracked counts; it is claimed when its statistic next changes
    CHECK( a.is_completed( achievement_audit_m03a_late ) == achievement_completion::pending );
    b.send<event_type::character_kills_monster>( u_id, mon_zombie, 0 );
    CHECK( a.is_completed( achievement_audit_m03a_late ) == achievement_completion::completed );

    // and the save says it's tracked
    std::ostringstream os;
    JsonOut jsout( os );
    a.serialize( jsout );
    CHECK( os.str().find( achievement_audit_m03a_late.str() ) != std::string::npos );

    // A tracker of no game yet gets nothing late
    achievements_tracker none( s, ignore, ignore, true );
    const JsonValue empty = json_loader::from_string(
                                R"({"enabled":true,"initial_achievements":[],"achievements_status":{}})" );
    none.deserialize( empty.get_object() );
    CHECK_FALSE( none.is_tracked( achievement_audit_m03a_late ) );
}

// EVT-J05: give_achievement silently did nothing for an achievement the game didn't track,
// and "completed" a conduct; there was no way to fail a conduct or read an achievement's state.
TEST_CASE( "evt-j05_give_achievement_fail_conduct_and_achievement_state",
           "[audit][achievement]" )
{
    clear_avatar();
    globals_restorer restore_globals;
    event_stats_restorer restore_stats( {
        event_type::player_gets_achievement, event_type::player_fails_conduct
    } );
    achievements_tracker &a = get_achievements();
    std::ostringstream saved_os;
    {
        JsonOut jsout( saved_os );
        a.serialize( jsout );
    }
    const std::string saved = saved_os.str();
    on_out_of_scope restore_achievements( [&a, &saved]() {
        a.clear();
        const JsonValue jv = json_loader::from_string( saved );
        a.deserialize( jv.get_object() );
    } );
    a.clear();
    {
        const JsonValue jv = json_loader::from_string(
                                 R"({"enabled":true,"initial_achievements":["audit_m03a_given","audit_m03a_conduct"],"achievements_status":{}})" );
        a.deserialize( jv.get_object() );
    }
    REQUIRE( a.is_tracked( achievement_audit_m03a_given ) );
    REQUIRE( a.is_tracked( achievement_audit_m03a_conduct ) );
    REQUIRE_FALSE( a.is_tracked( achievement_audit_m03a_untracked ) );

    const auto run = []( const effect_on_condition_id & eoc, const std::string & target ) {
        get_globals().set_global_value( "audit_m03a_target", target );
        return capture_debugmsg_during( [&eoc]() {
            dialogue d( get_talker_for( get_avatar() ), nullptr );
            eoc->activate( d );
        } );
    };
    const effect_on_condition_id &give = effect_on_condition_EOC_audit_m03a_give;
    const effect_on_condition_id &fail = effect_on_condition_EOC_audit_m03a_fail;
    dialogue d( get_talker_for( get_avatar() ), nullptr );

    // A conduct can't be given, nor an achievement failed, nor an unknown id either
    CHECK_FALSE( run( give, achievement_audit_m03a_conduct.str() ).empty() );
    CHECK( a.is_completed( achievement_audit_m03a_conduct ) == achievement_completion::pending );
    CHECK_FALSE( run( fail, achievement_audit_m03a_given.str() ).empty() );
    CHECK( a.is_completed( achievement_audit_m03a_given ) == achievement_completion::pending );
    CHECK_FALSE( run( give, "audit_m03a_no_such_achievement" ).empty() );
    CHECK_FALSE( run( fail, "audit_m03a_no_such_achievement" ).empty() );
    // One this game doesn't track is left alone, and it's not an error
    CHECK( run( give, achievement_audit_m03a_untracked.str() ).empty() );
    CHECK( a.is_completed( achievement_audit_m03a_untracked ) == achievement_completion::pending );

    CHECK( eval_math( d, "achievement_state('audit_m03a_given')" ) == 0 );
    CHECK( eval_math( d, "achievement_state('audit_m03a_conduct')" ) == 0 );
    CHECK( eval_math( d, "achievement_state('audit_m03a_untracked')" ) == -1 );
    CHECK_THROWS_AS( eval_math( d, "achievement_state('audit_m03a_no_such_achievement')" ),
                     math::runtime_error );

    CHECK( run( give, achievement_audit_m03a_given.str() ).empty() );
    CHECK( a.is_completed( achievement_audit_m03a_given ) == achievement_completion::completed );
    CHECK( run( fail, achievement_audit_m03a_conduct.str() ).empty() );
    CHECK( a.is_completed( achievement_audit_m03a_conduct ) == achievement_completion::failed );
    CHECK( eval_math( d, "achievement_state('audit_m03a_given')" ) == 1 );
    CHECK( eval_math( d, "achievement_state('audit_m03a_conduct')" ) == 2 );
    // Again: already done, nothing happens
    CHECK( run( give, achievement_audit_m03a_given.str() ).empty() );
    CHECK( run( fail, achievement_audit_m03a_conduct.str() ).empty() );
    CHECK( a.is_completed( achievement_audit_m03a_given ) == achievement_completion::completed );
}

// EVT-J05: EOCs couldn't read what past games completed, and the unlock of every profession
// and scenario was hard-coded to Arcade Mode's id; now any achievement can say so.
TEST_CASE( "evt-j05_past_achievements_and_unlocking_all_content", "[audit][achievement]" )
{
    CHECK( achievement_achievement_arcade_mode->unlocks_all_content() );
    CHECK_FALSE( achievement_achievement_kill_zombie->unlocks_all_content() );
    CHECK( achievement_audit_m03a_unlocker->unlocks_all_content() );

    const cata_path dir = PATH_INFO::achievementdir_path();
    assure_dir_exist( dir );
    const cata_path file = dir / "audit_m03a_past.json";
    // Remove the file even if a check below aborts the test, so it can't unlock anything in
    // later tests
    const auto remove_files = [file]() {
        remove_file( file );
        clear_past_achievements();
    };
    on_out_of_scope cleanup( remove_files );
    remove_files();

    clear_avatar();
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    CHECK( eval_math( d, "past_achievement('audit_m03a_unlocker')" ) == 0 );
    CHECK_THROWS_AS( eval_math( d, "past_achievement('audit_m03a_no_such_achievement')" ),
                     math::runtime_error );

    write_to_file( file, []( std::ostream & os ) {
        os << R"({"achievement_version":1,"achievements":["audit_m03a_unlocker"],)"
           << R"("avatar_name":"Audit EVT-J05"})";
    } );
    clear_past_achievements();
    CHECK( eval_math( d, "past_achievement('audit_m03a_unlocker')" ) == 1 );
    CHECK( get_past_achievements().unlocks_all_content() );
}

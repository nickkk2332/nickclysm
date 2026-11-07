#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item_group.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "mission.h"
#include "monster.h"
#include "monstergenerator.h"
#include "npc.h"
#include "npctalk.h"
#include "player_helpers.h"
#include "point.h"
#include "skill.h"
#include "talker.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M01B_J04_RECUR( "EOC_AUDIT_M01B_J04_RECUR" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M01B_J10_NPC_DEATH( "EOC_AUDIT_M01B_J10_NPC_DEATH" );

static const efftype_id effect_blind( "blind" );
static const efftype_id effect_downed( "downed" );

static const item_group_id Item_spawn_data_forest( "forest" );

static const mission_type_id mission_TEST_MISSION_FIND_ITEM( "TEST_MISSION_FIND_ITEM" );

static const trait_id trait_TOUGH( "TOUGH" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    talk_effect_t effect( jo, "effect", "audit_m_m01b_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

namespace
{
double global_number( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0.0;
}

// The avatar's EOC queue and inactive list, held aside (empty) while a test changes them.
class avatar_eoc_queue_aside
{
    public:
        avatar_eoc_queue_aside() {
            avatar &u = get_avatar();
            queue = std::move( u.queued_effect_on_conditions );
            u.queued_effect_on_conditions = queued_eocs();
            inactive.swap( u.inactive_effect_on_condition_vector );
        }
        ~avatar_eoc_queue_aside() {
            avatar &u = get_avatar();
            u.queued_effect_on_conditions = std::move( queue );
            u.inactive_effect_on_condition_vector = std::move( inactive );
        }
        avatar_eoc_queue_aside( const avatar_eoc_queue_aside & ) = delete;
        avatar_eoc_queue_aside &operator=( const avatar_eoc_queue_aside & ) = delete;
    private:
        queued_eocs queue;
        std::vector<effect_on_condition_id> inactive;
};

int count_in( const queued_eocs &queue, const effect_on_condition_id &eoc )
{
    return static_cast<int>( std::count_if( queue.list.begin(), queue.list.end(),
    [&eoc]( const queued_eoc & q ) {
        return q.eoc == eoc;
    } ) );
}

bool contains( const std::string &text, const std::string &part )
{
    return text.find( part ) != std::string::npos;
}
} // namespace

// EOC-J03: json_talk_topic::check_consistency was an empty TODO, so a response could lead to a
// topic that doesn't exist (or check a skill that doesn't), and nothing said so until a player
// picked it.
TEST_CASE( "eoc-j03_talk_topic_references_are_checked", "[audit][npc_talk]" )
{
    const JsonObject jo = json_loader::from_string( R"({
        "id": "TALK_AUDIT_M01B_J03", "type": "talk_topic", "dynamic_line": "Test.",
        "responses": [
          { "text": "Fine.", "topic": "TALK_DONE" },
          { "text": "Lost.", "topic": "TALK_AUDIT_M01B_NOWHERE" },
          { "text": "Refused.", "condition": { "u_has_cash": 5 },
            "failure_topic": "TALK_AUDIT_M01B_NO_FAILURE", "topic": "TALK_NONE" },
          { "text": "Tested.", "trial": { "type": "SKILL_CHECK", "difficulty": 1,
            "skill_required": "audit_m01b_no_skill" },
            "success": { "topic": "TALK_DONE" }, "failure": { "topic": "TALK_AUDIT_M01B_NO_TRIAL" } }
        ] })" );
    jo.allow_omitted_members();
    // not added to the loaded topics: only checked
    json_talk_topic topic;
    topic.load( jo, "audit_m_m01b_test" );
    finalize_conditions();
    const std::string messages = capture_debugmsg_during( [&topic]() {
        topic.check_consistency( "TALK_AUDIT_M01B_J03" );
    } );
    CHECK( contains( messages, "TALK_AUDIT_M01B_NOWHERE" ) );
    CHECK( contains( messages, "TALK_AUDIT_M01B_NO_FAILURE" ) );
    CHECK( contains( messages, "TALK_AUDIT_M01B_NO_TRIAL" ) );
    CHECK( contains( messages, "audit_m01b_no_skill" ) );
    CHECK_FALSE( contains( messages, "\"TALK_DONE\"" ) );
    CHECK_FALSE( contains( messages, "\"TALK_NONE\"" ) );
    // the built-in and mission topics count as existing
    CHECK( talk_topic_exists( "TALK_NONE" ) );
    CHECK( talk_topic_exists( "TALK_MISSION_OFFER" ) );
    CHECK_FALSE( talk_topic_exists( "TALK_AUDIT_M01B_NOWHERE" ) );
    // the topics the trial test in npc_talk_test leads to exist now
    CHECK( talk_topic_exists( "TALK_TEST_TRUE_CONDITION_NEXT" ) );
}

// EOC-J04: deactivate_condition is only read for a RECURRING EOC without a false_effect, and
// EOC check() was empty, so it was silently dead everywhere else.
TEST_CASE( "eoc-j04_dead_deactivate_condition_is_an_error", "[audit][eoc]" )
{
    const auto check_errors = []( const std::string & json ) {
        const JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        effect_on_condition eoc;
        eoc.load( jo, "audit_m_m01b_test" );
        finalize_conditions();
        return capture_debugmsg_during( [&eoc]() {
            eoc.check();
        } );
    };
    CHECK( contains( check_errors( R"({ "id": "EOC_AUDIT_M01B_J04_ACTIVATION",
        "condition": { "math": [ "1 == 0" ] }, "deactivate_condition": { "math": [ "1 == 1" ] },
        "effect": [ ] })" ), "EOC_AUDIT_M01B_J04_ACTIVATION" ) );
    CHECK( contains( check_errors(
                         R"({ "id": "EOC_AUDIT_M01B_J04_FALSE_EFFECT", "recurrence": "1 hours",
        "condition": { "math": [ "1 == 0" ] }, "deactivate_condition": { "math": [ "1 == 1" ] },
        "effect": [ ], "false_effect": [ ] })" ), "EOC_AUDIT_M01B_J04_FALSE_EFFECT" ) );
    CHECK( check_errors( R"({ "id": "EOC_AUDIT_M01B_J04_FINE", "recurrence": "1 hours",
        "condition": { "math": [ "1 == 0" ] }, "deactivate_condition": { "math": [ "1 == 1" ] },
        "effect": [ ] })" ).empty() );
    CHECK( capture_debugmsg_during( []() {
        effect_on_condition_EOC_AUDIT_M01B_J04_RECUR->check();
    } ).empty() );
}

// EOC-J04: a deactivated EOC came back only when a trait, bionic or the weather changed; now
// u_reactivate_eocs puts it back.
TEST_CASE( "eoc-j04_reactivate_eocs_puts_a_deactivated_eoc_back", "[audit][eoc]" )
{
    clear_avatar();
    get_globals().clear_global_values();
    avatar_eoc_queue_aside aside;
    avatar &u = get_avatar();
    const effect_on_condition_id &eoc = effect_on_condition_EOC_AUDIT_M01B_J04_RECUR;
    REQUIRE( eoc->type == eoc_type::RECURRING );
    u.inactive_effect_on_condition_vector.push_back( eoc );

    dialogue d( get_talker_for( u ), nullptr );
    const talk_effect_t reactivate = load_effect(
                                         R"({ "effect": { "u_reactivate_eocs": [ "EOC_AUDIT_M01B_J04_RECUR" ] } })" );
    reactivate.apply( d );
    CHECK( u.inactive_effect_on_condition_vector.empty() );
    CHECK( count_in( u.queued_effect_on_conditions, eoc ) == 1 );
    REQUIRE( d.maybe_get_value( "eocs_reactivated" ) != nullptr );
    CHECK( d.get_value( "eocs_reactivated" ).dbl() == 1 );
    // not deactivated now: left as it is
    reactivate.apply( d );
    CHECK( count_in( u.queued_effect_on_conditions, eoc ) == 1 );
    CHECK( d.get_value( "eocs_reactivated" ).dbl() == 0 );
}

// EOC-J05: foreach compared its type and target only when it ran, so a typo looped over
// nothing; and it could list only four kinds of ids.
TEST_CASE( "eoc-j05_foreach_rejects_unknown_lists", "[audit][eoc]" )
{
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "foreach": "idss", "var": { "context_val": "id" },
        "target": "bodypart", "effect": [ ] } })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "foreach": "ids", "var": { "context_val": "id" },
        "target": "bodyparts", "effect": [ ] } })" ), JsonError );
    // three Xedra Evolved healing EOCs wrote this, and healed nothing
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "foreach": "array", "var": { "context_val": "id" },
        "target": "bodypart", "effect": [ ] } })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "foreach": "u_things",
        "var": { "context_val": "id" }, "effect": [ ] } })" ), JsonError );
    CHECK_NOTHROW( load_effect( R"({ "effect": { "foreach": "npc_spells",
        "var": { "context_val": "id" }, "effect": [ ] } })" ) );
}

TEST_CASE( "eoc-j05_foreach_lists_more_ids_and_what_a_talker_has", "[audit][eoc]" )
{
    clear_avatar();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );
    const auto count_of = [&d]( const std::string & list, const std::string & target ) {
        get_globals().clear_global_values();
        get_globals().set_global_value( "audit_m01b_j05_group", Item_spawn_data_forest.str() );
        load_effect( R"({ "effect": { "foreach": ")" + list + R"(", "var": { "context_val": "id" }, )" +
                     target + R"( "effect": [ { "math": [ "audit_m01b_j05_n++" ] },
                     { "if": { "compare_string": [ "downed", { "context_val": "id" } ] },
                       "then": { "math": [ "audit_m01b_j05_downed++" ] } },
                     { "if": { "compare_string": [ "TOUGH", { "context_val": "id" } ] },
                       "then": { "math": [ "audit_m01b_j05_tough++" ] } } ] } })" ).apply( d );
        return static_cast<size_t>( global_number( "audit_m01b_j05_n" ) );
    };
    SECTION( "every loaded id of a new kind" ) {
        CHECK( count_of( "ids", R"("target": "skill",)" ) == Skill::skills.size() );
        CHECK( count_of( "ids", R"("target": "effect",)" ) == get_effect_types().size() );
        CHECK( count_of( "ids", R"("target": "monster",)" ) ==
               MonsterGenerator::generator().get_all_mtypes().size() );
    }
    SECTION( "an item group named by a variable" ) {
        CHECK( count_of( "item_group", R"("target": { "global_val": "audit_m01b_j05_group" },)" ) ==
               item_group::every_possible_item_from( Item_spawn_data_forest ).size() );
    }
    SECTION( "the talker's own effects and traits, each once" ) {
        u.add_effect( effect_downed, 1_minutes );
        u.add_effect( effect_blind, 1_minutes );
        u.set_mutation( trait_TOUGH );
        std::set<efftype_id> effects;
        for( const effect &eff : u.get_effects() ) {
            effects.insert( eff.get_id() );
        }
        CHECK( count_of( "u_effects", "" ) == effects.size() );
        CHECK( global_number( "audit_m01b_j05_downed" ) == 1 );
        CHECK( count_of( "u_traits", "" ) == u.get_mutations().size() );
        CHECK( global_number( "audit_m01b_j05_tough" ) == 1 );
    }
    get_globals().clear_global_values();
    clear_avatar();
}

// EOC-J06: the only way for JSON to raise an event was trigger_event with a hardcoded
// event_type; mods had no channel of their own.
TEST_CASE( "eoc-j06_custom_events_run_their_eocs", "[audit][eoc]" )
{
    clear_avatar();
    get_globals().clear_global_values();
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    CHECK( effect_on_conditions::custom_event_eocs( "audit_m01b_ping" ).size() == 2 );
    load_effect( R"({ "effect": { "trigger_custom_event": "audit_m01b_ping",
        "variables": { "amount": 5 } } })" ).apply( d );
    CHECK( global_number( "audit_m01b_j06_total" ) == 5 );
    CHECK( global_number( "audit_m01b_j06_second" ) == 1 );
    CHECK( global_number( "audit_m01b_j06_avatar" ) == 1 );
    // an event no EOC runs on does nothing
    CHECK( effect_on_conditions::custom_event_eocs( "audit_m01b_nobody" ).empty() );
    CHECK( capture_debugmsg_during( [&d]() {
        load_effect( R"({ "effect": { "trigger_custom_event": "audit_m01b_nobody" } })" ).apply( d );
    } ).empty() );
    CHECK( global_number( "audit_m01b_j06_second" ) == 1 );
    // an EOC takes one kind of event
    const JsonObject jo = json_loader::from_string( R"({ "id": "EOC_AUDIT_M01B_J06_BOTH",
        "eoc_type": "EVENT", "required_event": "game_start", "custom_event": "audit_m01b_ping",
        "effect": [ ] })" );
    jo.allow_omitted_members();
    effect_on_condition both;
    CHECK_THROWS_AS( both.load( jo, "audit_m_m01b_test" ), JsonError );
    get_globals().clear_global_values();
}

// EOC-J09: assign_mission always gave the mission without a giver, and nothing could tell
// whether a mission had been completed or failed.
TEST_CASE( "eoc-j09_assign_mission_giver_and_mission_status", "[audit][eoc]" )
{
    clear_avatar();
    clear_npcs();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
    dialogue d( get_talker_for( u ), get_talker_for( guy ) );
    const conditional_t::func active = load_condition(
                                           R"({ "condition": { "u_mission_status": "TEST_MISSION_FIND_ITEM", "status": "active" } })" );
    const conditional_t::func completed = load_condition(
            R"({ "condition": { "u_mission_status": "TEST_MISSION_FIND_ITEM", "status": "completed" } })" );
    const conditional_t::func failed = load_condition(
                                           R"({ "condition": { "u_mission_status": "TEST_MISSION_FIND_ITEM", "status": "failed" } })" );
    CHECK_FALSE( active( d ) );

    load_effect(
        R"({ "effect": { "assign_mission": "TEST_MISSION_FIND_ITEM", "giver": "npc" } })" ).apply(
            d );
    const std::vector<mission *> missions = u.get_active_missions();
    REQUIRE( missions.size() == 1 );
    mission *miss = missions.front();
    CHECK( miss->mission_id() == mission_TEST_MISSION_FIND_ITEM );
    CHECK( miss->get_npc_id() == guy.getID() );
    CHECK( std::count( guy.chatbin.missions_assigned.begin(), guy.chatbin.missions_assigned.end(),
                       miss ) == 1 );
    CHECK( active( d ) );
    CHECK_FALSE( completed( d ) );
    CHECK_FALSE( failed( d ) );

    miss->fail();
    CHECK_FALSE( active( d ) );
    CHECK_FALSE( completed( d ) );
    CHECK( failed( d ) );

    // a giver that isn't an NPC
    dialogue alone( get_talker_for( u ), nullptr );
    const std::string messages = capture_debugmsg_during( [&alone]() {
        load_effect(
            R"({ "effect": { "assign_mission": "TEST_MISSION_FIND_ITEM", "giver": "u" } })" ).apply(
                alone );
    } );
    CHECK( contains( messages, "isn't an NPC" ) );
    CHECK( u.get_active_missions().empty() );
    CHECK_THROWS_AS( load_condition(
                         R"({ "condition": { "u_mission_status": "TEST_MISSION_FIND_ITEM", "status": "done" } })" ),
                     JsonError );
    clear_avatar();
    clear_npcs();
}

// EOC-J10: PREVENT_DEATH EOCs had no beta, though AVATAR_DEATH ones get the killer.
TEST_CASE( "eoc-j10_prevent_death_eocs_see_the_killer", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    u.reset_death_state();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point::east );
    get_globals().set_global_value( "audit_m01b_j10_armed", 1 );
    u.set_all_parts_hp_cur( 0 );
    REQUIRE( u.is_dead_state() );
    CHECK_FALSE( u.finalize_death( &zed ) );
    CHECK_FALSE( u.is_dead_state() );
    CHECK( global_number( "audit_m01b_j10_killer_seen" ) == 1 );
    get_globals().clear_global_values();
    clear_avatar();
    u.reset_death_state();
    clear_creatures();
}

// EOC-J10 and EOC-J08: a monster's death EOC ran with it already dead, and u_prevent_death did
// nothing for monsters; a PREVENT_MONSTER_DEATH EOC now can stop the death.
TEST_CASE( "eoc-j10_prevent_monster_death", "[audit][eoc]" )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 2, 0 ) );

    get_globals().set_global_value( "audit_m01b_j10_armed", 1 );
    zed.die( &here, &u );
    CHECK_FALSE( zed.is_dead() );
    CHECK( zed.get_hp() >= 1 );
    CHECK( global_number( "audit_m01b_j10_monster_saves" ) == 1 );
    CHECK( global_number( "audit_m01b_j10_monster_killer" ) == 1 );

    // not prevented: it dies
    get_globals().set_global_value( "audit_m01b_j10_armed", 0 );
    zed.die( &here, nullptr );
    CHECK( zed.is_dead() );
    CHECK( global_number( "audit_m01b_j10_monster_saves" ) == 1 );
    get_globals().clear_global_values();
    clear_creatures();
}

// EOC-J10: an NPC's death EOCs came only from its template.
TEST_CASE( "eoc-j10_add_and_remove_death_eocs", "[audit][eoc]" )
{
    clear_avatar();
    clear_npcs();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
    const effect_on_condition_id &eoc = effect_on_condition_EOC_AUDIT_M01B_J10_NPC_DEATH;
    const auto has = [&guy, &eoc]() {
        return std::count( guy.death_eocs.begin(), guy.death_eocs.end(), eoc );
    };
    dialogue d( get_talker_for( guy ), nullptr );
    const talk_effect_t add = load_effect(
                                  R"({ "effect": { "u_add_death_eoc": "EOC_AUDIT_M01B_J10_NPC_DEATH" } })" );
    add.apply( d );
    add.apply( d );
    CHECK( has() == 1 );
    load_effect( R"({ "effect": { "u_remove_death_eoc": "EOC_AUDIT_M01B_J10_NPC_DEATH" } })" ).apply(
        d );
    CHECK( has() == 0 );
    // only NPC_DEATH EOCs (named by a variable, so it is checked when it runs)
    get_globals().set_global_value( "audit_m01b_j10_which",
                                    effect_on_condition_EOC_AUDIT_M01B_J04_RECUR.str() );
    const std::string messages = capture_debugmsg_during( [&d]() {
        load_effect(
            R"({ "effect": { "u_add_death_eoc": { "global_val": "audit_m01b_j10_which" } } })" ).apply(
                d );
    } );
    CHECK( contains( messages, "NPC_DEATH" ) );
    CHECK( std::count( guy.death_eocs.begin(), guy.death_eocs.end(),
                       effect_on_condition_EOC_AUDIT_M01B_J04_RECUR ) == 0 );

    add.apply( d );
    guy.die( &get_map(), nullptr );
    CHECK( global_number( "audit_m01b_j10_npc_deaths" ) == 1 );
    get_globals().clear_global_values();
    clear_npcs();
}

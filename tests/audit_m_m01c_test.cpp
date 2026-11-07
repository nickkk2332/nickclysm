#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "dialogue_chatbin.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "npctalk.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "skill.h"
#include "talker.h"
#include "type_id.h"

static const effect_on_condition_id effect_on_condition_EOC_M01C_CANCEL( "EOC_M01C_CANCEL" );
static const effect_on_condition_id effect_on_condition_EOC_M01C_LATER( "EOC_M01C_LATER" );
static const effect_on_condition_id
effect_on_condition_EOC_M01C_QUEUE_GLOBAL_KEPT( "EOC_M01C_QUEUE_GLOBAL_KEPT" );
static const effect_on_condition_id
effect_on_condition_EOC_M01C_QUEUE_KEPT( "EOC_M01C_QUEUE_KEPT" );
static const effect_on_condition_id
effect_on_condition_EOC_M01C_QUEUE_PLAIN( "EOC_M01C_QUEUE_PLAIN" );
static const effect_on_condition_id
effect_on_condition_EOC_M01C_SET_CHATBIN( "EOC_M01C_SET_CHATBIN" );
static const effect_on_condition_id
effect_on_condition_EOC_M01C_TIME_LEFT( "EOC_M01C_TIME_LEFT" );

static const matype_id style_test_style_m01c_training( "test_style_m01c_training" );
static const matype_id style_test_style_ma1( "test_style_ma1" );

static const npc_class_id test_m01c_stranger_class( "test_m01c_stranger_class" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_speech( "speech" );

static const spell_id spell_test_spell_m01c_training( "test_spell_m01c_training" );

namespace
{
// Holds the avatar's EOC queue and the game's aside, empty, while a test runs EOC queues, and
// puts them back afterwards.
class eoc_queues_aside
{
    public:
        eoc_queues_aside() {
            avatar &u = get_avatar();
            avatar_queue = std::move( u.queued_effect_on_conditions );
            u.queued_effect_on_conditions = queued_eocs();
            avatar_inactive.swap( u.inactive_effect_on_condition_vector );
            global_queue = std::move( g->queued_global_effect_on_conditions );
            g->queued_global_effect_on_conditions = queued_eocs();
            global_inactive.swap( g->inactive_global_effect_on_condition_vector );
        }
        ~eoc_queues_aside() {
            avatar &u = get_avatar();
            u.queued_effect_on_conditions = std::move( avatar_queue );
            u.inactive_effect_on_condition_vector = std::move( avatar_inactive );
            g->queued_global_effect_on_conditions = std::move( global_queue );
            g->inactive_global_effect_on_condition_vector = std::move( global_inactive );
        }
        eoc_queues_aside( const eoc_queues_aside & ) = delete;
        eoc_queues_aside &operator=( const eoc_queues_aside & ) = delete;
    private:
        queued_eocs avatar_queue;
        std::vector<effect_on_condition_id> avatar_inactive;
        queued_eocs global_queue;
        std::vector<effect_on_condition_id> global_inactive;
};

double marks( const Creature &who, const std::string &name )
{
    const diag_value *value = who.maybe_get_value( name );
    return value == nullptr ? 0 : value->dbl();
}

double global_number( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? 0 : value->dbl();
}

int count_in( const queued_eocs &queue, const effect_on_condition_id &eoc )
{
    int count = 0;
    for( const queued_eoc &queued : queue.list ) {
        if( queued.eoc == eoc ) {
            ++count;
        }
    }
    return count;
}

void run_eoc( const effect_on_condition_id &eoc, std::unique_ptr<talker> alpha,
              std::unique_ptr<talker> beta )
{
    dialogue d( std::move( alpha ), std::move( beta ) );
    eoc->activate( d );
}

talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    talk_effect_t effect( jo, "effect", "audit_m_m01c_test" );
    finalize_conditions();
    return effect;
}

talk_trial load_trial( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_trial trial( jo );
    finalize_conditions();
    return trial;
}

void remove_marks( Creature &who )
{
    for( const char *name : {
             "m01c_ran", "m01c_seen", "m01c_global_ran", "m01c_global_seen"
         } ) {
        who.remove_value( name );
    }
}
} // namespace

// EOC-J11: NPC lessons took and cost what C++ formulas said; martial arts and spells now take
// training_time and training_cost from JSON, and without them nothing changes.
TEST_CASE( "eoc-j11_training_time_and_cost_come_from_json", "[audit][npc]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    npc &teacher = spawn_npc( u.pos_bub().xy() + point( 2, 0 ), "test_talker" );
    const character_id teacher_id = teacher.getID();
    on_out_of_scope cleanup( [teacher_id]() {
        overmap_buffer.remove_npc( teacher_id );
        clear_npcs();
        clear_avatar();
    } );
    REQUIRE( !teacher.is_player_ally() );

    SECTION( "a style's own time and price" ) {
        CHECK( calc_ma_style_training_time( teacher, u, style_test_style_m01c_training ) ==
               90_minutes );
        CHECK( calc_ma_style_training_cost( teacher, u, style_test_style_m01c_training ) ==
               1234 + u.get_str() );
        // a style that doesn't say keeps the built-in ones
        CHECK( calc_ma_style_training_time( teacher, u, style_test_style_ma1 ) == 30_minutes );
        CHECK( calc_ma_style_training_cost( teacher, u, style_test_style_ma1 ) == 800 );
    }
    SECTION( "a spell's own time and price, by whether the student knows it" ) {
        teacher.magic->learn_spell( spell_test_spell_m01c_training, teacher, true );
        teacher.magic->get_spell( spell_test_spell_m01c_training ).set_level( teacher, 4 );
        REQUIRE( teacher.magic->get_spell( spell_test_spell_m01c_training ).get_level() == 4 );
        const int difficulty =
            teacher.magic->get_spell( spell_test_spell_m01c_training ).get_difficulty( u );
        REQUIRE( !u.magic->knows_spell( spell_test_spell_m01c_training ) );
        CHECK( calc_spell_training_time( teacher, u, spell_test_spell_m01c_training ) ==
               time_duration::from_seconds( 7200 + 60 * difficulty ) );
        CHECK( calc_spell_training_cost( teacher, u, spell_test_spell_m01c_training ) == 999 );

        u.magic->learn_spell( spell_test_spell_m01c_training, u, true );
        REQUIRE( u.magic->knows_spell( spell_test_spell_m01c_training ) );
        CHECK( calc_spell_training_time( teacher, u, spell_test_spell_m01c_training ) == 10_minutes );
        CHECK( calc_spell_training_cost( teacher, u, spell_test_spell_m01c_training ) == 400 );
    }
    SECTION( "a skill without them keeps the built-in formulas" ) {
        teacher.set_skill_level( skill_melee, 5 );
        u.set_skill_level( skill_melee, 0 );
        CHECK( calc_skill_training_time_char( teacher, u, skill_melee ) == 55_minutes );
        CHECK( calc_skill_training_cost_char( teacher, u, skill_melee ) == 1000 );
    }
    SECTION( "a skill reads them" ) {
        // A loaded skill is in every character's skill list, so this one is only parsed
        Skill parsed;
        const JsonObject jo = json_loader::from_string( R"({
            "name": "audit training skill", "description": "Parsed only.",
            "display_category": "display_melee", "sort_rank": 900100,
            "training_time": { "math": [ "60 * _level + 60 * _teacher_level" ] },
            "training_cost": { "math": [ "_knowledge_level + 7" ] } })" );
        parsed.load( jo, "audit_m_m01c_test" );
        finalize_conditions();
        REQUIRE( parsed.training_time() );
        REQUIRE( parsed.training_cost() );
        const_dialogue d( get_const_talker_for( u ), get_const_talker_for( teacher ) );
        d.set_value( "level", 2 );
        d.set_value( "teacher_level", 3 );
        d.set_value( "knowledge_level", 1 );
        CHECK( parsed.training_time()->evaluate( d ) == 5_minutes );
        CHECK( parsed.training_cost()->evaluate( d ) == 8 );
    }
}

// EOC-J11: a trial's difficulty was read with get_int, so it couldn't be a variable or math, and
// speech practice from a trial was fixed.
TEST_CASE( "eoc-j11_trial_difficulty_takes_math_and_practice_is_json", "[audit][npc_talk]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point( 2, 0 ), "test_talker" );
    const character_id guy_id = guy.getID();
    on_out_of_scope cleanup( [guy_id]() {
        get_globals().remove_global_value( "m01c_lie" );
        overmap_buffer.remove_npc( guy_id );
        clear_npcs();
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), get_talker_for( guy ) );

    SECTION( "a number, math and a variable give the same chance" ) {
        const talk_trial constant = load_trial( R"({ "type": "LIE", "difficulty": 30 })" );
        const talk_trial by_math = load_trial(
                                       R"({ "type": "LIE", "difficulty": { "math": [ "10 + 20" ] } })" );
        const talk_trial by_var = load_trial(
                                      R"({ "type": "LIE", "difficulty": { "global_val": "m01c_lie" } })" );
        get_globals().set_global_value( "m01c_lie", 30 );
        CHECK( constant.get_difficulty( d ) == 30 );
        CHECK( by_math.get_difficulty( d ) == 30 );
        CHECK( by_math.calc_chance( d ) == constant.calc_chance( d ) );
        CHECK( by_var.calc_chance( d ) == constant.calc_chance( d ) );
        get_globals().set_global_value( "m01c_lie", 55 );
        CHECK( by_var.get_difficulty( d ) == 55 );
    }
    SECTION( "a skill check against math" ) {
        u.set_skill_level( skill_melee, 3 );
        const talk_trial passes = load_trial( R"json({ "type": "SKILL_CHECK", "skill_required": "melee",
            "difficulty": { "math": [ "u_skill('melee')" ] } })json" );
        const talk_trial fails = load_trial( R"json({ "type": "SKILL_CHECK", "skill_required": "melee",
            "difficulty": { "math": [ "u_skill('melee') + 1" ] } })json" );
        CHECK( passes.get_difficulty( d ) == 3 );
        CHECK( passes.calc_chance( d ) == 100 );
        CHECK( fails.calc_chance( d ) == 0 );
    }
    SECTION( "speech_practice sets what a speech trial trains" ) {
        // chance 100, so every roll succeeds and the built-in amount is (100 - 100) / 10 = 0
        const talk_trial built_in = load_trial( R"({ "type": "LIE", "difficulty": 1000 })" );
        const talk_trial none = load_trial(
                                    R"({ "type": "LIE", "difficulty": 1000, "speech_practice": 0 })" );
        const talk_trial some = load_trial( R"json({ "type": "LIE", "difficulty": 1000,
            "speech_practice": { "math": [ "_success * (50 - _chance / 4)" ] } })json" );
        u.set_focus( 100 );
        const auto progress = [&u]() {
            const SkillLevel &level = u.get_skill_level_object( skill_speech );
            return level.level() * 100000 + level.exercise( true );
        };
        const int before = progress();
        REQUIRE( built_in.roll( d ) );
        CHECK( progress() == before );
        REQUIRE( none.roll( d ) );
        CHECK( progress() == before );
        REQUIRE( some.roll( d ) );
        CHECK( progress() > before );
    }
}

// EOC-J11: of an NPC's chatbin topics, only first_topic could be changed at run time.
TEST_CASE( "eoc-j11_set_chatbin_topic_changes_an_npcs_greetings", "[audit][npc_talk]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point( 2, 0 ), "test_talker" );
    const character_id guy_id = guy.getID();
    on_out_of_scope cleanup( [guy_id]() {
        overmap_buffer.remove_npc( guy_id );
        clear_npcs();
    } );
    dialogue d( get_talker_for( u ), get_talker_for( guy ) );
    d.set_value( "slot", std::string( "talk_wake_up" ) );
    d.set_value( "topic", std::string( "TALK_DONE" ) );
    effect_on_condition_EOC_M01C_SET_CHATBIN->activate( d );
    CHECK( guy.chatbin.talk_friend == "TALK_STRANGER_FRIENDLY" );
    CHECK( guy.chatbin.talk_wake_up == "TALK_DONE" );
    // a misspelt name written out is a load error
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "npc_set_chatbin_topic": "talk_frend", )"
                                  R"("topic": "TALK_DONE" } })" ), JsonError );
}

// EOC-J11: how a stranger opens was fixed personality thresholds; a class's stranger_topics now
// come first, and without one that holds the thresholds decide as before.
TEST_CASE( "eoc-j11_an_npc_class_picks_how_strangers_open", "[audit][npc_talk]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point( 2, 0 ), "test_talker" );
    const character_id guy_id = guy.getID();
    on_out_of_scope cleanup( [guy_id]() {
        get_globals().remove_global_value( "m01c_stranger" );
        overmap_buffer.remove_npc( guy_id );
        clear_npcs();
    } );
    const npc_class_id original_class = guy.myclass;
    const npc_attitude original_attitude = guy.get_attitude();

    const std::string built_in = guy.pick_talk_topic( u );
    const npc_attitude built_in_attitude = guy.get_attitude();
    guy.set_attitude( original_attitude );

    guy.myclass = test_m01c_stranger_class;
    // no rule holds
    CHECK( guy.pick_talk_topic( u ) == built_in );
    CHECK( guy.get_attitude() == built_in_attitude );
    guy.set_attitude( original_attitude );

    get_globals().set_global_value( "m01c_stranger", 2 );
    CHECK( guy.pick_talk_topic( u ) == guy.chatbin.talk_stranger_wary );
    CHECK( guy.get_attitude() == original_attitude );

    get_globals().set_global_value( "m01c_stranger", 1 );
    CHECK( guy.pick_talk_topic( u ) == guy.chatbin.talk_mug );
    CHECK( guy.get_attitude() == NPCATT_MUG );
    guy.set_attitude( original_attitude );
    guy.myclass = original_class;
}

// EOC-J12: a queued EOC kept only a character u: one queued for a monster, item or vehicle was
// dropped, and the npc was always lost.  With keep_talkers both are kept and found again.
TEST_CASE( "eoc-j12_keep_talkers_queues_for_a_monster_and_keeps_the_npc", "[audit][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    const eoc_queues_aside aside;
    restore_on_out_of_scope restore_turn( calendar::turn );
    avatar &u = get_avatar();
    on_out_of_scope cleanup( [&u]() {
        remove_marks( u );
        get_globals().remove_global_value( "m01c_time_left" );
        clear_creatures();
    } );
    monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    queued_eocs &game_queue = g->queued_global_effect_on_conditions;

    // without keep_talkers, it isn't queued for a monster, as before
    run_eoc( effect_on_condition_EOC_M01C_QUEUE_PLAIN, get_talker_for( zombie ), get_talker_for( u ) );
    CHECK( game_queue.list.empty() );
    CHECK( u.queued_effect_on_conditions.list.empty() );

    run_eoc( effect_on_condition_EOC_M01C_QUEUE_KEPT, get_talker_for( zombie ), get_talker_for( u ) );
    REQUIRE( game_queue.list.size() == 1 );
    CHECK( game_queue.list.front().alpha == "monster:" + std::to_string( zombie.persistent_id() ) );
    CHECK( game_queue.list.front().beta == std::to_string( u.getID().get_value() ) );
    CHECK( u.queued_effect_on_conditions.list.empty() );

    // u_time_until_eoc finds it for the zombie
    run_eoc( effect_on_condition_EOC_M01C_TIME_LEFT, get_talker_for( zombie ), nullptr );
    CHECK( global_number( "m01c_time_left" ) > 0 );

    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( marks( zombie, "m01c_ran" ) == 0 );
    calendar::turn += 3_turns;
    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( marks( zombie, "m01c_ran" ) == 1 );
    CHECK( marks( u, "m01c_seen" ) == 1 );
    CHECK( count_in( game_queue, effect_on_condition_EOC_M01C_LATER ) == 0 );
}

// Loading a game drops a queued EOC from the game's queue unless it's global; one queued for a
// kept u is there either way.
TEST_CASE( "eoc-j12_a_load_keeps_an_eoc_queued_for_a_monster", "[audit][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    const eoc_queues_aside aside;
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_creatures();
    } );
    monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    run_eoc( effect_on_condition_EOC_M01C_QUEUE_KEPT, get_talker_for( zombie ), nullptr );
    REQUIRE( count_in( g->queued_global_effect_on_conditions,
                       effect_on_condition_EOC_M01C_LATER ) == 1 );
    // (this also queues every recurring EOC; the queues are put back afterwards, unrun)
    effect_on_conditions::load_existing_character( u );
    CHECK( count_in( g->queued_global_effect_on_conditions,
                     effect_on_condition_EOC_M01C_LATER ) == 1 );
}

TEST_CASE( "eoc-j12_kept_talkers_that_are_gone", "[audit][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    const eoc_queues_aside aside;
    restore_on_out_of_scope restore_turn( calendar::turn );
    avatar &u = get_avatar();
    on_out_of_scope cleanup( [&u]() {
        remove_marks( u );
        clear_creatures();
    } );
    map &here = get_map();

    SECTION( "without its u it doesn't run" ) {
        monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
        run_eoc( effect_on_condition_EOC_M01C_QUEUE_KEPT, get_talker_for( zombie ), get_talker_for( u ) );
        REQUIRE( count_in( g->queued_global_effect_on_conditions,
                           effect_on_condition_EOC_M01C_LATER ) == 1 );
        zombie.die( &here, nullptr );
        calendar::turn += 3_turns;
        effect_on_conditions::process_effect_on_conditions( u );
        CHECK( marks( u, "m01c_seen" ) == 0 );
        CHECK( g->queued_global_effect_on_conditions.list.empty() );
    }
    SECTION( "without its npc it runs with none" ) {
        monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
        run_eoc( effect_on_condition_EOC_M01C_QUEUE_KEPT, get_talker_for( u ), get_talker_for( zombie ) );
        // the avatar's own queue, with the zombie kept as its npc
        REQUIRE( u.queued_effect_on_conditions.list.size() == 1 );
        CHECK( u.queued_effect_on_conditions.list.front().beta ==
               "monster:" + std::to_string( zombie.persistent_id() ) );
        // ...which a save keeps
        std::ostringstream saved;
        JsonOut jsout( saved );
        jsout.start_object();
        u.store( jsout );
        jsout.end_object();
        CHECK_THAT( saved.str(), Catch::Contains(
                        R"("beta":"monster:)" + std::to_string( zombie.persistent_id() ) + "\"" ) );

        zombie.die( &here, nullptr );
        calendar::turn += 3_turns;
        effect_on_conditions::process_effect_on_conditions( u );
        CHECK( marks( u, "m01c_ran" ) == 1 );
        CHECK( u.queued_effect_on_conditions.list.empty() );
    }
}

TEST_CASE( "eoc-j12_keep_talkers_on_a_global_eoc_keeps_its_u", "[audit][eoc]" )
{
    clear_map();
    clear_avatar();
    const eoc_queues_aside aside;
    restore_on_out_of_scope restore_turn( calendar::turn );
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point( 2, 0 ), "test_talker" );
    const character_id guy_id = guy.getID();
    on_out_of_scope cleanup( [&u, guy_id]() {
        remove_marks( u );
        overmap_buffer.remove_npc( guy_id );
        clear_npcs();
    } );

    run_eoc( effect_on_condition_EOC_M01C_QUEUE_GLOBAL_KEPT, get_talker_for( guy ),
             get_talker_for( u ) );
    REQUIRE( g->queued_global_effect_on_conditions.list.size() == 1 );
    CHECK( g->queued_global_effect_on_conditions.list.front().alpha ==
           std::to_string( guy_id.get_value() ) );
    calendar::turn += 3_turns;
    effect_on_conditions::process_effect_on_conditions( u );
    // the NPC is u, not the avatar
    CHECK( marks( guy, "m01c_global_ran" ) == 1 );
    CHECK( marks( u, "m01c_global_ran" ) == 0 );
    CHECK( marks( u, "m01c_global_seen" ) == 1 );
    remove_marks( guy );
}

TEST_CASE( "eoc-j12_cancel_queued_eocs_calls_off_its_talkers_runs", "[audit][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    const eoc_queues_aside aside;
    restore_on_out_of_scope restore_turn( calendar::turn );
    avatar &u = get_avatar();
    on_out_of_scope cleanup( [&u]() {
        remove_marks( u );
        get_globals().remove_global_value( "m01c_time_left" );
        clear_creatures();
    } );
    monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );

    // one for the zombie, one for the avatar
    run_eoc( effect_on_condition_EOC_M01C_QUEUE_KEPT, get_talker_for( zombie ), nullptr );
    run_eoc( effect_on_condition_EOC_M01C_QUEUE_PLAIN, get_talker_for( u ), nullptr );
    REQUIRE( count_in( g->queued_global_effect_on_conditions, effect_on_condition_EOC_M01C_LATER ) ==
             1 );
    REQUIRE( count_in( u.queued_effect_on_conditions, effect_on_condition_EOC_M01C_LATER ) == 1 );

    // the zombie's is called off, the avatar's isn't
    run_eoc( effect_on_condition_EOC_M01C_CANCEL, get_talker_for( zombie ), nullptr );
    run_eoc( effect_on_condition_EOC_M01C_TIME_LEFT, get_talker_for( zombie ), nullptr );
    CHECK( global_number( "m01c_time_left" ) == -1 );
    run_eoc( effect_on_condition_EOC_M01C_TIME_LEFT, get_talker_for( u ), nullptr );
    CHECK( global_number( "m01c_time_left" ) > 0 );

    calendar::turn += 3_turns;
    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( marks( zombie, "m01c_ran" ) == 0 );
    CHECK( marks( u, "m01c_ran" ) == 1 );
    CHECK( g->queued_global_effect_on_conditions.list.empty() );
    CHECK( u.queued_effect_on_conditions.list.empty() );

    // the avatar's own, called off before it's due, and not saved
    run_eoc( effect_on_condition_EOC_M01C_QUEUE_PLAIN, get_talker_for( u ), nullptr );
    const auto saved_queue = [&u]() {
        std::ostringstream saved;
        JsonOut jsout( saved );
        jsout.start_object();
        u.store( jsout );
        jsout.end_object();
        return saved.str();
    };
    CHECK_THAT( saved_queue(), Catch::Contains( R"("eoc":"EOC_M01C_LATER")" ) );
    run_eoc( effect_on_condition_EOC_M01C_CANCEL, get_talker_for( u ), nullptr );
    CHECK_THAT( saved_queue(), !Catch::Contains( R"("eoc":"EOC_M01C_LATER")" ) );
    calendar::turn += 3_turns;
    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( marks( u, "m01c_ran" ) == 1 );
    CHECK( u.queued_effect_on_conditions.list.empty() );

    // keep_talkers is only for queued EOCs
    CHECK_THROWS_AS( load_effect( R"({ "effect": { "run_eocs": "EOC_M01C_LATER", )"
                                  R"("keep_talkers": true } })" ), JsonError );
}

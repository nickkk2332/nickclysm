#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "math_parser_diag.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

static const skill_id skill_swimming( "swimming" );

// The messages check_literal_ids() gives for an expression parsed outside data loading
static std::vector<std::string> unknown_ids( const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.check_literal_ids();
}

static bool has_message( const std::vector<std::string> &messages, const std::string_view text )
{
    return std::any_of( messages.begin(), messages.end(), [&text]( std::string_view msg ) {
        return msg.find( text ) != std::string_view::npos;
    } );
}

// MATH-J04: ids written as literals in dialogue functions were never checked, so a typo such as
// u_has_trait('FEEBEL') loaded cleanly and was false forever.
TEST_CASE( "math-j04_literal_ids_that_name_nothing_are_reported", "[audit][math_parser]" )
{
    SECTION( "a trait, a skill and an effect that don't exist" ) {
        const std::vector<std::string> messages = unknown_ids(
                    "u_has_trait('AUDIT_M02A_NO_TRAIT') + n_skill('audit_m02a_no_skill') + "
                    "u_effect_intensity('audit_m02a_no_effect')" );
        CHECK( messages.size() == 3 );
        CHECK( has_message( messages, R"(has_trait(): unknown trait "AUDIT_M02A_NO_TRAIT")" ) );
        CHECK( has_message( messages, R"(skill(): unknown skill "audit_m02a_no_skill")" ) );
        CHECK( has_message( messages,
                            R"(effect_intensity(): unknown effect "audit_m02a_no_effect")" ) );
    }
    SECTION( "the second argument and a kwarg" ) {
        const std::vector<std::string> messages = unknown_ids(
                    "u_armor('bash', 'audit_m02a_no_part') + "
                    "u_effect_duration('bite', 'bodypart': 'audit_m02a_no_part_either')" );
        CHECK( messages.size() == 2 );
        CHECK( has_message( messages, R"(armor(): unknown body part "audit_m02a_no_part")" ) );
        CHECK( has_message( messages,
                            R"(effect_duration(): unknown body part "audit_m02a_no_part_either")" ) );
    }
    SECTION( "each monster of monsters_nearby()" ) {
        const std::vector<std::string> messages = unknown_ids(
                    "u_monsters_nearby('mon_zombie', 'audit_m02a_no_monster')" );
        REQUIRE( messages.size() == 1 );
        CHECK( has_message( messages,
                            R"(monsters_nearby(): unknown monster "audit_m02a_no_monster")" ) );
    }
    SECTION( "an id used twice is reported once, and the ids are checked only once" ) {
        math_exp exp;
        REQUIRE( exp.parse( "u_has_trait('AUDIT_M02A_NO_TRAIT') + n_has_trait('AUDIT_M02A_NO_TRAIT')" ) );
        CHECK( exp.check_literal_ids().size() == 1 );
        CHECK( exp.check_literal_ids().empty() );
    }
}

TEST_CASE( "math-j04_known_ids_and_special_values_are_fine", "[audit][math_parser]" )
{
    // ids that exist
    CHECK( unknown_ids( "u_has_trait('PSYCHOPATH') + u_skill('speech') + u_vitamin('calcium')" ).empty() );
    CHECK( unknown_ids( "u_armor('bash', 'torso') + u_effect_intensity('bite', 'bodypart': 'arm_l')" ).empty() );
    // values the functions take instead of an id
    CHECK( unknown_ids( "u_hp('ALL') + u_hp('ALL_MAJOR') + u_bionic_count('ANY')" ).empty() );
    CHECK( unknown_ids( "u_spell_level_sum('school': 'NONE') + u_spell_level('null')" ).empty() );
    // ids in variables are checked when the function runs
    CHECK( unknown_ids( "u_has_trait(u_audit_m02a_trait) + u_skill(_audit_m02a_skill)" ).empty() );
    // character flags need no definition, so has_flag() isn't checked
    CHECK( unknown_ids( "u_has_flag('AUDIT_M02A_ANY_FLAG')" ).empty() );
}

TEST_CASE( "math-j04_every_checked_function_exists", "[audit][math_parser]" )
{
    const std::map<std::string_view, dialogue_func> &funcs = get_all_diag_funcs();
    const std::vector<std::string_view> checked = diag_funcs_with_literal_ids();
    CHECK( checked.size() > 50 );
    for( const std::string_view name : checked ) {
        CAPTURE( name );
        CHECK( funcs.count( name ) == 1 );
    }
}

static std::string load_condition_errors( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    // Math is parsed, and its ids checked, after loading; outside data loading by hand.
    return capture_debugmsg_during( []() {
        finalize_conditions();
    } );
}

TEST_CASE( "math-j04_loading_reports_an_unknown_id_unless_optional", "[audit][math_parser]" )
{
    SECTION( "a typo is a load error that says what is wrong" ) {
        const std::string errors = load_condition_errors(
                                       R"json({ "condition": { "math": [ "u_has_trait('AUDIT_M02A_NO_TRAIT')" ] } })json" );
        CHECK( errors.find( R"(has_trait(): unknown trait "AUDIT_M02A_NO_TRAIT")" ) !=
               std::string::npos );
        CHECK( errors.find( "optional_ids" ) != std::string::npos );
    }
    SECTION( "known ids load quietly" ) {
        CHECK( load_condition_errors(
                   R"json({ "condition": { "math": [ "u_has_trait('PSYCHOPATH')" ] } })json" ).empty() );
    }
    SECTION( "optional_ids: another mod's id loads quietly" ) {
        CHECK( load_condition_errors(
                   R"json({ "condition": { "math": [ "u_has_trait('AUDIT_M02A_NO_TRAIT')" ], )json"
                   R"json("optional_ids": true } })json" ).empty() );
    }
}

// The talk trial of a JSON response, with its math parsed
static talk_trial load_trial( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_trial trial( jo );
    finalize_conditions();
    return trial;
}

// MATH-J03: a trial's difficulty was a plain number, and its "mod" a closed list of names.
TEST_CASE( "math-j03_trial_difficulty_can_be_math", "[audit][npc_talk]" )
{
    clear_avatar();
    clear_map();
    global_variables &globals = get_globals();
    globals.clear_global_values();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
    dialogue d( get_talker_for( u ), get_talker_for( guy ) );

    const talk_trial trial = load_trial(
                                 R"({ "type": "SKILL_CHECK", "skill_required": "swimming", )"
                                 R"("difficulty": { "math": [ "audit_m02a_difficulty" ] } })" );
    REQUIRE( d.const_actor( false )->get_skill_level( skill_swimming ) == 0 );
    globals.set_global_value( "audit_m02a_difficulty", 0.0 );
    CHECK( trial.get_difficulty( d ) == 0 );
    CHECK( trial.calc_chance( d ) == 100 );
    globals.set_global_value( "audit_m02a_difficulty", 2.0 );
    CHECK( trial.get_difficulty( d ) == 2 );
    CHECK( trial.calc_chance( d ) == 0 );

    // a plain number still works
    const talk_trial plain = load_trial(
                                 R"({ "type": "SKILL_CHECK", "skill_required": "swimming", "difficulty": 3 })" );
    CHECK( plain.get_difficulty( d ) == 3 );
    CHECK( plain.calc_chance( d ) == 0 );

    globals.clear_global_values();
    clear_npcs();
}

TEST_CASE( "math-j03_trial_mod_can_be_math", "[audit][npc_talk]" )
{
    clear_avatar();
    clear_map();
    global_variables &globals = get_globals();
    globals.clear_global_values();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "thug" );
    dialogue d( get_talker_for( u ), get_talker_for( guy ) );

    // the built-in part of a lie's chance, which the difficulty is added to
    const int base = d.const_actor( false )->trial_chance_mod( "lie" ) +
                     d.const_actor( true )->trial_chance_mod( "lie" );
    // a difficulty that puts the chance at 50, away from the 0 and 100 it is kept within
    globals.set_global_value( "audit_m02a_difficulty", static_cast<double>( 50 - base ) );

    const talk_trial no_mod = load_trial(
                                  R"({ "type": "LIE", "difficulty": { "math": [ "audit_m02a_difficulty" ] } })" );
    REQUIRE( no_mod.calc_chance( d ) == 50 );

    const talk_trial with_mod = load_trial(
                                    R"({ "type": "LIE", "difficulty": { "math": [ "audit_m02a_difficulty" ] }, )"
                                    R"("mod": [ [ "TRUST", 0 ], { "math": [ "audit_m02a_mod" ] } ] })" );
    CHECK( with_mod.modifiers.size() == 1 );
    CHECK( with_mod.math_modifiers.size() == 1 );
    globals.set_global_value( "audit_m02a_mod", 7.0 );
    CHECK( with_mod.calc_chance( d ) == 57 );
    globals.set_global_value( "audit_m02a_mod", -20.0 );
    CHECK( with_mod.calc_chance( d ) == 30 );
    // still kept within 0 and 100
    globals.set_global_value( "audit_m02a_mod", 500.0 );
    CHECK( with_mod.calc_chance( d ) == 100 );

    globals.clear_global_values();
    clear_npcs();
}

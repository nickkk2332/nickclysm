#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "json_loader.h"
#include "magic_enchantment.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "profession.h"
#include "proficiency.h"
#include "scenario.h"
#include "skill.h"
#include "talker.h"
#include "trait_group.h"
#include "type_id.h"

static const json_character_flag json_flag_TEST_PROG_J09_FLAG( "TEST_PROG_J09_FLAG" );

static const matype_id style_karate( "style_karate" );

static const mongroup_id GROUP_DOMESTIC( "GROUP_DOMESTIC" );
static const mongroup_id GROUP_ZOMBIE( "GROUP_ZOMBIE" );

static const mtype_id mon_cat( "mon_cat" );
static const mtype_id mon_dog( "mon_dog" );

static const profession_id profession_test_prog_j04_avatar_prof( "test_prog_j04_avatar_prof" );
static const profession_id profession_test_prog_j04_cash_prof( "test_prog_j04_cash_prof" );
static const profession_id profession_test_prog_j04_hobby( "test_prog_j04_hobby" );
static const profession_id profession_test_prog_j04_npc_prof( "test_prog_j04_npc_prof" );
static const profession_id profession_test_prog_j05_variant_prof( "test_prog_j05_variant_prof" );
static const profession_id profession_test_prog_j07_prof_child( "test_prog_j07_prof_child" );
static const profession_id profession_test_prog_j07_prof_parent( "test_prog_j07_prof_parent" );
static const profession_id profession_test_prog_j07_prof_replace( "test_prog_j07_prof_replace" );

static const proficiency_id proficiency_test_prog_j09_prof( "test_prog_j09_prof" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_test_prog_j08_skill( "test_prog_j08_skill" );

static const spell_id spell_test_ability_formula_spell( "test_ability_formula_spell" );
static const spell_id spell_test_ability_spell( "test_ability_spell" );

static const string_id<scenario> scenario_test_prog_j07_scen_child( "test_prog_j07_scen_child" );
static const string_id<scenario> scenario_test_prog_j07_scen_parent( "test_prog_j07_scen_parent" );
static const string_id<scenario>
scenario_test_prog_j07_scen_replace( "test_prog_j07_scen_replace" );

static const trait_group::Trait_group_tag Trait_group_test_prog_j07_group( "test_prog_j07_group" );
static const trait_group::Trait_group_tag
Trait_group_test_prog_j07_override( "test_prog_j07_override" );

static const trait_id trait_FLEET( "FLEET" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_hair_buzzcut( "hair_buzzcut" );

static double var_of( const Character &who, const std::string &name )
{
    const diag_value *val = who.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

static double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// ---- PROG-J04 ----

// A profession's EOCs ran for the avatar only (an NPC dropped every one not run_for_npcs,
// which only a global EOC can be), and a hobby's never ran at all.
TEST_CASE( "prog-j04_profession_and_hobby_eocs_run_for_npcs_that_take_them",
           "[audit][chargen][eoc]" )
{
    standard_npc guy( "PROG-J04" );
    clear_character( guy );
    REQUIRE( var_of( guy, "test_prog_j04_ran" ) == 0 );

    SECTION( "a profession with run_eocs_for_npcs" ) {
        guy.prof = &profession_test_prog_j04_npc_prof.obj();
        effect_on_conditions::load_new_character( guy );
        CHECK( var_of( guy, "test_prog_j04_ran" ) == 1 );
    }
    SECTION( "a profession without it, as before" ) {
        guy.prof = &profession_test_prog_j04_avatar_prof.obj();
        effect_on_conditions::load_new_character( guy );
        CHECK( var_of( guy, "test_prog_j04_ran" ) == 0 );
    }
    SECTION( "a hobby with it" ) {
        guy.hobbies.insert( &profession_test_prog_j04_hobby.obj() );
        effect_on_conditions::load_new_character( guy );
        CHECK( var_of( guy, "test_prog_j04_ran" ) == 1 );
    }
}

// A hobby's forbidden_traits, starting cash and trauma, martial arts and pets were read and
// ignored.
TEST_CASE( "prog-j04_hobbies_forbid_traits_and_give_cash_trauma_styles_and_pets",
           "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const int cash_before = u.cash;
    const std::vector<mtype_id> pets_before = u.starting_pets;
    const vproto_id vehicle_before = u.starting_vehicle;
    const std::optional<point_abs_om> origin_before = u.world_origin;
    const scenario *scenario_before = get_scenario();
    on_out_of_scope restore( [&]() {
        avatar &you = get_avatar();
        you.cash = cash_before;
        you.starting_pets = pets_before;
        you.starting_vehicle = vehicle_before;
        you.world_origin = origin_before;
        you.forget_all_recipes();
        set_scenario( scenario_before );
        clear_avatar();
    } );
    set_scenario( scenario::generic() );
    u.prof = &profession_test_prog_j04_cash_prof.obj();
    const auto only_illiterate = []( const mutation_branch & mb ) {
        return mb.id == trait_ILLITERATE;
    };
    REQUIRE( get_scenario()->traitquery( trait_ILLITERATE ) );

    CHECK_FALSE( u.forbids_trait_by_background( trait_ILLITERATE ) );
    CHECK( u.get_random_trait( only_illiterate ) == trait_ILLITERATE );

    u.hobbies.insert( &profession_test_prog_j04_hobby.obj() );
    CHECK( u.forbids_trait_by_background( trait_ILLITERATE ) );
    // random characters don't get it either
    CHECK( u.get_random_trait( only_illiterate ).is_empty() );

    const std::ptrdiff_t cats_before = std::count( u.starting_pets.begin(), u.starting_pets.end(),
                                       mon_cat );
    u.initialize( character_type::NOW );
    // added to the profession's
    CHECK( u.cash == 6000 );
    CHECK( u.get_trauma() == 30 );
    CHECK( u.martial_arts_data->has_martialart( style_karate ) );
    CHECK( std::count( u.starting_pets.begin(), u.starting_pets.end(), mon_cat ) == cats_before + 1 );
}

// ---- PROG-J05 ----

// forced_traits was documented as taking { "trait", "variant" } but was read as plain ids,
// and the variant of a profession's trait was dropped when the trait was added.
TEST_CASE( "prog-j05_forced_and_profession_traits_keep_their_variants", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const scenario *scenario_before = get_scenario();
    on_out_of_scope restore( [scenario_before]() {
        set_scenario( scenario_before );
        clear_avatar();
    } );

    SECTION( "a scenario's forced trait" ) {
        const scenario &scen = scenario_test_prog_j07_scen_parent.obj();
        REQUIRE( scen.forced_trait_variant( trait_hair_buzzcut ) == "blue" );
        set_scenario( &scen );
        u.prof = profession::generic();
        u.add_traits();
        CHECK( u.has_trait_variant( trait_and_var( trait_hair_buzzcut, "blue" ) ) );
    }
    SECTION( "a profession's trait" ) {
        set_scenario( scenario::generic() );
        REQUIRE( get_scenario()->get_locked_traits().empty() );
        u.prof = &profession_test_prog_j05_variant_prof.obj();
        u.add_traits();
        CHECK( u.has_trait_variant( trait_and_var( trait_hair_buzzcut, "green" ) ) );
    }
}

// ---- PROG-J06 ----

// EOCs could read and set only the practical level and experience.
TEST_CASE( "prog-j06_math_reads_and_sets_theory_rust_and_time_since_practice",
           "[audit][skill][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), nullptr );
    u.set_skill_level( skill_melee, 2 );
    REQUIRE( u.get_knowledge_level( skill_melee ) == 2 );

    CHECK( eval_math( d, "u_skill_knowledge('melee')" ) == 2 );
    eval_math( d, "u_skill_knowledge('melee') = 4" );
    CHECK( u.get_knowledge_level( skill_melee ) == 4 );
    // never below the practical level
    eval_math( d, "u_skill_knowledge('melee') = 1" );
    CHECK( u.get_knowledge_level( skill_melee ) == 2 );

    eval_math( d, "u_skill_knowledge('melee') = 4" );
    eval_math( d, "u_skill_knowledge_exp('melee', 'format': 'raw') = 1000" );
    CHECK( u.get_skill_level_object( skill_melee ).knowledgeExperience( true ) == 1000 );
    CHECK( eval_math( d, "u_skill_knowledge_exp('melee', 'format': 'raw')" ) == 1000 );
    // 100% of a level's experience is the next level
    eval_math( d, "u_skill_knowledge_exp('melee') = 100" );
    CHECK( u.get_knowledge_level( skill_melee ) == 5 );
    CHECK( u.get_skill_level_object( skill_melee ).knowledgeExperience( true ) == 0 );

    SkillLevel &level = u.get_skill_level_object( skill_melee );
    CHECK( eval_math( d, "u_skill_rust('melee', 'format': 'raw')" ) == 0 );
    eval_math( d, "u_skill_time_since_practice('melee') = 3600" );
    CHECK( level.lastPracticed() == calendar::turn - 1_hours );
    CHECK( eval_math( d, "u_skill_time_since_practice('melee', 'unit': 'hours')" ) ==
           Approx( 1.0 ) );
    // rust, with no grace period
    level.rust( 0, 1.0f, 0_turns );
    const int rust = level.rustAccumulator();
    REQUIRE( rust > 0 );
    CHECK( eval_math( d, "u_skill_rust('melee', 'format': 'raw')" ) == rust );
    CHECK( eval_math( d, "u_skill_rusty('melee')" ) == 1 );
    eval_math( d, "u_skill_time_since_practice('melee') = 0" );
    CHECK( level.lastPracticed() == calendar::turn );
}

// u_skill_exp could only set experience directly, past focus, catch-up and the skill's rules.
TEST_CASE( "prog-j06_skill_exp_can_practice", "[audit][skill][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), nullptr );
    const skill_id &skill = skill_test_prog_j08_skill;
    REQUIRE_FALSE( skill->training_consumes_focus() );

    u.set_focus( 100 );
    eval_math( d, "u_skill_exp('test_prog_j08_skill', 'format': 'raw', 'practice': true) += 1000" );
    const int practiced = u.get_skill_level_object( skill ).exercise( true );
    CHECK( practiced > 0 );

    // the same as practicing for it, from the same start (the theory it gained too, or the
    // practice would catch up to it faster)
    u.get_skill_level_object( skill ).set_exercise( 0, true );
    u.get_skill_level_object( skill ).set_knowledge_exercise( 0, true );
    u.set_focus( 100 );
    u.practice( skill, 10, MAX_SKILL, true, true );
    CHECK( u.get_skill_level_object( skill ).exercise( true ) == practiced );

    // a lower value practices nothing
    eval_math( d, "u_skill_exp('test_prog_j08_skill', 'format': 'raw', 'practice': true) -= 100" );
    CHECK( u.get_skill_level_object( skill ).exercise( true ) == practiced );
}

// gains_skill_level didn't say whether the practical or the theoretical level rose, theory
// that rose from practice sent nothing, and a level lost to rust sent nothing.
TEST_CASE( "prog-j06_skill_level_events_say_which_level", "[audit][skill][event]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        avatar &you = get_avatar();
        you.remove_value( "test_prog_j06_practical" );
        you.remove_value( "test_prog_j06_theoretical" );
        you.remove_value( "test_prog_j06_lost" );
        clear_avatar();
    } );
    u.remove_value( "test_prog_j06_practical" );
    u.remove_value( "test_prog_j06_theoretical" );
    const skill_id &skill = skill_test_prog_j08_skill;

    // a practical level-up that carries theory along is one practical event
    u.set_focus( 100 );
    u.practice( skill, 1000, MAX_SKILL, true );
    SkillLevel &level = u.get_skill_level_object( skill );
    REQUIRE( level.level() == 1 );
    REQUIRE( level.knowledgeLevel() == 1 );
    CHECK( var_of( u, "test_prog_j06_practical" ) == 1 );
    CHECK( var_of( u, "test_prog_j06_theoretical" ) == 0 );

    // theory 99% of the way to the next level rises from a little practice, by itself
    level.set_knowledge_exercise( 99 );
    u.set_focus( 100 );
    u.practice( skill, 10, MAX_SKILL, true );
    CHECK( level.level() == 1 );
    CHECK( level.knowledgeLevel() == 2 );
    CHECK( var_of( u, "test_prog_j06_theoretical" ) == 2 );
    CHECK( var_of( u, "test_prog_j06_practical" ) == 1 );
}

// ---- PROG-J07 ----

// A copy-from scenario couldn't change its dates, and surround_groups only ever added to the
// copied list.
TEST_CASE( "prog-j07_copied_scenarios_change_dates_and_surround_groups", "[audit][chargen]" )
{
    const scenario &parent = scenario_test_prog_j07_scen_parent.obj();
    const scenario &child = scenario_test_prog_j07_scen_child.obj();
    const scenario &replaced = scenario_test_prog_j07_scen_replace.obj();
    const int season_days = get_option<int>( "SEASON_LENGTH" );

    // summer to winter: two seasons later, the copied hour, day and year kept
    CHECK( child.start_of_game() - parent.start_of_game() == 1_days * season_days * 2 );
    CHECK( time_past_midnight( child.start_of_game() ) == 18_hours );
    CHECK( replaced.start_of_game() == parent.start_of_game() );

    REQUIRE( parent.surround_groups().size() == 1 );
    CHECK( parent.surround_groups()[0].first == GROUP_ZOMBIE );
    // delete, then extend
    REQUIRE( child.surround_groups().size() == 1 );
    CHECK( child.surround_groups()[0].first == GROUP_DOMESTIC );
    CHECK( child.surround_groups()[0].second == Approx( 10.0f ) );
    // given, it replaces the copied list
    REQUIRE( replaced.surround_groups().size() == 1 );
    CHECK( replaced.surround_groups()[0].first == GROUP_DOMESTIC );
    CHECK( replaced.surround_groups()[0].second == Approx( 5.0f ) );

    // a copy keeps the forced traits' variants (PROG-J05)
    CHECK( child.forced_trait_variant( trait_hair_buzzcut ) == "blue" );
}

static std::ptrdiff_t count_pets( const std::vector<mtype_id> &pets, const mtype_id &mon )
{
    return std::count( pets.begin(), pets.end(), mon );
}

// A copy-from profession added its pets, spells and EOCs to the copied ones, and couldn't
// remove any or change a spell's level.
TEST_CASE( "prog-j07_copied_professions_replace_extend_and_delete_lists", "[audit][chargen]" )
{
    const profession &parent = profession_test_prog_j07_prof_parent.obj();
    const profession &child = profession_test_prog_j07_prof_child.obj();
    const profession &replaced = profession_test_prog_j07_prof_replace.obj();

    CHECK( count_pets( parent.pets(), mon_dog ) == 2 );
    CHECK( count_pets( parent.pets(), mon_cat ) == 1 );
    CHECK( count_pets( child.pets(), mon_dog ) == 2 );
    CHECK( count_pets( child.pets(), mon_cat ) == 0 );
    CHECK( replaced.pets() == std::vector<mtype_id> { mon_cat } );

    const std::map<spell_id, int> child_spells = child.spells();
    REQUIRE( child_spells.size() == 2 );
    CHECK( child_spells.at( spell_test_ability_spell ) == 3 );
    CHECK( child_spells.at( spell_test_ability_formula_spell ) == 2 );
    const std::map<spell_id, int> replaced_spells = replaced.spells();
    REQUIRE( replaced_spells.size() == 1 );
    CHECK( replaced_spells.at( spell_test_ability_formula_spell ) == 5 );

    CHECK( parent.get_eocs().size() == 1 );
    CHECK( child.get_eocs().empty() );
    CHECK( replaced.get_eocs().size() == 1 );
}

// A second definition of a trait group always added to it; "override" replaces it.
TEST_CASE( "prog-j07_trait_groups_merge_unless_overridden", "[audit][chargen]" )
{
    CHECK( trait_group::group_contains_trait( Trait_group_test_prog_j07_group, trait_ILLITERATE ) );
    CHECK( trait_group::group_contains_trait( Trait_group_test_prog_j07_group, trait_FLEET ) );
    CHECK_FALSE( trait_group::group_contains_trait( Trait_group_test_prog_j07_override,
                 trait_ILLITERATE ) );
    CHECK( trait_group::group_contains_trait( Trait_group_test_prog_j07_override, trait_FLEET ) );
}

// ---- PROG-J08 ----

static void load_skill( const std::string &json, Skill &sk )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    sk.load( jo, "audit_m_m16b_test" );
}

// Every skill learned, caught up, read and rusted by the same hardcoded numbers.
TEST_CASE( "prog-j08_skills_load_their_own_progression_rules", "[audit][skill]" )
{
    const Skill &own = skill_test_prog_j08_skill.obj();
    CHECK( own.training_multiplier() == Approx( 2.0f ) );
    CHECK( own.book_learning_multiplier() == Approx( 0.5f ) );
    CHECK( own.catchup_multiplier() == Approx( 1.0f ) );
    CHECK( own.rusts() );
    CHECK( own.rust_grace_period() == 12_hours );
    CHECK( own.rust_multiplier() == Approx( 2.0f ) );
    CHECK( own.rust_max_levels() == Approx( 3.0f ) );

    // the built-in rules by default
    const Skill &melee = skill_melee.obj();
    CHECK( melee.training_multiplier() == 1.0f );
    CHECK( melee.book_learning_multiplier() == 1.0f );
    CHECK( melee.catchup_multiplier() == 1.0f );
    CHECK( melee.rusts() );
    CHECK( melee.rust_grace_period() == 24_hours );
    CHECK( melee.rust_multiplier() == 1.0f );
    CHECK( melee.rust_max_levels() == 3.0f );

    const std::string head = R"({ "name": "audit skill", "description": "x", )"
                             R"("display_category": "display_melee", "sort_rank": 1, )";
    Skill no_rust;
    load_skill( head + R"("rust": { "enabled": false, "max_levels": 1.5 } })", no_rust );
    CHECK_FALSE( no_rust.rusts() );
    CHECK( no_rust.rust_max_levels() == Approx( 1.5f ) );
    CHECK( no_rust.rust_grace_period() == 24_hours );
    Skill bad;
    CHECK_THROWS_AS( load_skill( head + R"("training_multiplier": -1 })", bad ), JsonError );
}

TEST_CASE( "prog-j08_training_and_book_multipliers", "[audit][skill]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );

    u.set_focus( 100 );
    u.practice( skill_melee, 10, MAX_SKILL, true );
    const int plain = u.get_skill_level_object( skill_melee ).exercise( true );
    REQUIRE( plain > 0 );
    u.set_focus( 100 );
    u.practice( skill_test_prog_j08_skill, 10, MAX_SKILL, true );
    CHECK( u.get_skill_level_object( skill_test_prog_j08_skill ).exercise( true ) == 2 * plain );

    // the same rolls (minimum and maximum equal), half the theory
    SkillLevel full;
    SkillLevel half;
    full.readBook( 5, 5, -1, 1.0f );
    half.readBook( 5, 5, -1, 0.5f );
    REQUIRE( full.knowledgeExperience( true ) > 0 );
    CHECK( half.knowledgeExperience( true ) * 2 == full.knowledgeExperience( true ) );
}

// Rust waited 24 hours, took the same amount and stopped at 3 levels for every skill.
TEST_CASE( "prog-j08_skill_rust_follows_the_skills_rules", "[audit][skill]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    restore_on_out_of_scope restore_turn( calendar::turn );
    on_out_of_scope restore( []() {
        get_avatar().remove_value( "test_prog_j06_lost" );
        clear_avatar();
    } );
    u.remove_value( "test_prog_j06_lost" );
    // a later midnight than the avatar's last update, when the daily rust runs
    calendar::turn = calendar::turn_zero + 1_days * ( to_days<int>( calendar::turn -
                     calendar::turn_zero ) + 2 );
    REQUIRE( calendar::once_every( 24_hours ) );
    u.recalculate_enchantment_cache();

    u.set_skill_level( skill_melee, 2 );
    u.set_skill_level( skill_test_prog_j08_skill, 2 );
    SkillLevel &melee = u.get_skill_level_object( skill_melee );
    SkillLevel &own = u.get_skill_level_object( skill_test_prog_j08_skill );
    melee.set_last_practiced( calendar::turn - 13_hours );
    own.set_last_practiced( calendar::turn - 13_hours );

    // update_body() rusts the skills once a day
    u.update_body();
    // 12 hours of grace for this skill, 24 for melee
    CHECK( melee.rustAccumulator() == 0 );
    CHECK( melee.level() == 2 );
    REQUIRE( own.rustAccumulator() > 0 );
    // rust ran out the level's experience: the level went down, with an event (PROG-J06)
    CHECK( own.level() == 1 );
    CHECK( var_of( u, "test_prog_j06_lost" ) == 1 );

    // twice what a skill at multiplier 1 loses
    SkillLevel plain;
    plain.level( 2 );
    plain.set_last_practiced( calendar::turn - 13_hours );
    plain.rust( 0, 1.0f, 12_hours );
    CHECK( own.rustAccumulator() == Approx( 2 * plain.rustAccumulator() ).margin( 2 ) );

    // max_levels: no more rust once that much has rusted away
    SkillLevel capped;
    capped.level( 2 );
    capped.set_last_practiced( calendar::turn - 2_days );
    capped.rust( 0, 1.0f, 24_hours, 0.01f );
    const int after_one = capped.rustAccumulator();
    REQUIRE( after_one > 0 );
    capped.rust( 0, 1.0f, 24_hours, 0.01f );
    CHECK( capped.rustAccumulator() == after_one );
    SkillLevel uncapped;
    uncapped.level( 2 );
    uncapped.set_last_practiced( calendar::turn - 2_days );
    uncapped.rust( 0, 1.0f, 24_hours );
    uncapped.rust( 0, 1.0f, 24_hours );
    CHECK( uncapped.rustAccumulator() > after_one );
}

// ---- PROG-J09 ----

static void load_proficiency( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    proficiency prof;
    prof.load( jo, "audit_m_m16b_test" );
}

// Proficiencies couldn't grant enchantments or flags, and get_proficiency_bonus() rounded a
// fractional bonus down to an int.
TEST_CASE( "prog-j09_proficiencies_grant_enchantments_flags_and_fractional_bonuses",
           "[audit][proficiency]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        avatar &you = get_avatar();
        you.lose_proficiency( proficiency_test_prog_j09_prof, true );
        you.recalculate_enchantment_cache();
        clear_avatar();
    } );
    u.recalculate_enchantment_cache();
    CHECK_FALSE( u.has_flag( json_flag_TEST_PROG_J09_FLAG ) );
    CHECK( u.get_proficiency_bonus( "archery", proficiency_bonus_type::strength ) == 0.0f );
    CHECK( u.calculate_by_enchantment( 0.0, enchant_vals::mod::SKILL_RUST_RESIST ) == Approx( 0.0 ) );

    u.add_proficiency( proficiency_test_prog_j09_prof, true );
    u.recalculate_enchantment_cache();
    CHECK( u.has_flag( json_flag_TEST_PROG_J09_FLAG ) );
    CHECK( u.count_flag( json_flag_TEST_PROG_J09_FLAG ) == 1 );
    CHECK( u.get_proficiency_bonus( "archery", proficiency_bonus_type::strength ) ==
           Approx( 0.5f ) );
    CHECK( u.calculate_by_enchantment( 0.0, enchant_vals::mod::SKILL_RUST_RESIST ) ==
           Approx( 100.0 ) );

    u.lose_proficiency( proficiency_test_prog_j09_prof, true );
    u.recalculate_enchantment_cache();
    CHECK_FALSE( u.has_flag( json_flag_TEST_PROG_J09_FLAG ) );
    CHECK( u.calculate_by_enchantment( 0.0, enchant_vals::mod::SKILL_RUST_RESIST ) == Approx( 0.0 ) );
}

// A bonuses key or type the game doesn't read loaded without a word and did nothing.
TEST_CASE( "prog-j09_unknown_bonus_keys_and_types_are_load_errors", "[audit][proficiency]" )
{
    const std::string head = R"({ "name": "audit proficiency", "description": "x", )"
                             R"("can_learn": false, "category": "prof_test_category", )";
    CHECK_NOTHROW( load_proficiency( head +
                                     R"("bonuses": { "athlete": [ { "type": "strength", "value": 0.5 } ] } })" ) );
    CHECK_THROWS_AS( load_proficiency( head +
                                       R"("bonuses": { "archer": [ { "type": "strength", "value": 1 } ] } })" ), JsonError );
    CHECK_THROWS_AS( load_proficiency( head +
                                       R"("bonuses": { "melee_attack": [ { "type": "strength", "value": 1 } ] } })" ),
                     JsonError );
}

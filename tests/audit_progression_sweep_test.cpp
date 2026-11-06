#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "addiction.h"
#include "avatar.h"
#include "bionics.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_creator_ui.h"
#include "debug.h"
#include "dialogue.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "math_parser.h"
#include "output.h"
#include "player_helpers.h"
#include "proficiency.h"
#include "profession.h"
#include "recipe.h"
#include "skill.h"
#include "string_formatter.h"
#include "trait_group.h"
#include "type_id.h"

// ---- PROG-B04 ----

static const addiction_id addiction_caffeine( "caffeine" );

static const bionic_id bio_armor_eyes( "bio_armor_eyes" );
static const bionic_id bio_carbon( "bio_carbon" );

static const itype_id itype_fun_survival( "fun_survival" );

static const profession_id profession_computer_literate( "computer_literate" );
static const profession_id profession_driving_license( "driving_license" );
static const profession_id profession_test_prog_b04_hobby( "test_prog_b04_hobby" );
static const profession_id profession_test_prog_b04_profession( "test_prog_b04_profession" );
static const profession_id profession_test_prog_b05_hobby( "test_prog_b05_hobby" );
static const profession_id profession_test_prog_b05_profession( "test_prog_b05_profession" );
static const profession_id profession_test_prog_b07_hobby( "test_prog_b07_hobby" );
static const profession_id profession_test_prog_b07_profession( "test_prog_b07_profession" );
static const profession_id profession_test_prog_b08_hobby( "test_prog_b08_hobby" );
static const profession_id profession_test_prog_b08_profession( "test_prog_b08_profession" );

static const proficiency_id proficiency_prof_knapping( "prof_knapping" );
static const proficiency_id proficiency_prof_knapping_speed( "prof_knapping_speed" );
static const proficiency_id proficiency_test_prog_b10_circle_a( "test_prog_b10_circle_a" );
static const proficiency_id proficiency_test_prog_b10_circle_b( "test_prog_b10_circle_b" );

static const recipe_id recipe_large_tent_kit_from_broken( "large_tent_kit_from_broken" );
static const recipe_id recipe_tent_kit_from_broken( "tent_kit_from_broken" );

static const skill_id skill_driving( "driving" );

static const trait_group::Trait_group_tag
Trait_group_BG_survival_story_UNIVERSAL( "BG_survival_story_UNIVERSAL" );
static const trait_group::Trait_group_tag
Trait_group_test_prog_b11_no_such_group( "test_prog_b11_no_such_group" );

static const trait_id trait_test_prog_b08_forbidden_trait( "test_prog_b08_forbidden_trait" );
static const trait_id trait_test_prog_b08_hobby_trait( "test_prog_b08_hobby_trait" );

static int installed_count( const Character &you, const bionic_id &bio )
{
    const std::vector<bionic_id> installed = you.get_bionics();
    return static_cast<int>( std::count( installed.begin(), installed.end(), bio ) );
}

// set_bionics_from_hobbies() returned at the first hobby CBM the character already had, so
// that hobby's later CBMs, and those of every hobby after it, were never installed.
TEST_CASE( "prog-b04_an_installed_hobby_cbm_skips_only_itself", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    u.hobbies.insert( &profession_test_prog_b04_hobby.obj() );
    u.add_bionic( bio_carbon );

    u.set_bionics_from_hobbies();

    CHECK( installed_count( u, bio_carbon ) == 1 );
    CHECK( u.has_bionic( bio_armor_eyes ) );
}

// The profession's CBMs went in without that check: one the character already had was
// installed again, or set off add_bionic()'s "already installed" error.
TEST_CASE( "prog-b04_a_profession_cbm_already_installed_is_not_installed_again",
           "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    u.prof = &profession_test_prog_b04_profession.obj();
    u.hobbies.insert( &profession_test_prog_b04_hobby.obj() );
    u.add_bionic( bio_carbon );

    Character &you = u;
    const std::string errors = capture_debugmsg_during( [&you]() {
        you.initialize( false );
    } );

    CHECK( errors.empty() );
    CHECK( installed_count( u, bio_carbon ) == 1 );
    CHECK( u.has_bionic( bio_armor_eyes ) );
}

// ---- PROG-B05 ----

// Profession and hobby recipes were looked up by their result item, so a recipe with an
// id_suffix taught the plain recipe for that item instead, and one with no result taught
// nothing.
TEST_CASE( "prog-b05_starting_recipes_are_the_recipes_listed", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    REQUIRE( recipe_tent_kit_from_broken->result().str() == "tent_kit" );
    REQUIRE( recipe_large_tent_kit_from_broken->result().str() == "large_tent_kit" );
    u.prof = &profession_test_prog_b05_profession.obj();
    u.hobbies.insert( &profession_test_prog_b05_hobby.obj() );

    Character &you = u;
    you.initialize( false );

    CHECK( u.knows_recipe( &recipe_tent_kit_from_broken.obj() ) );
    CHECK( u.knows_recipe( &recipe_large_tent_kit_from_broken.obj() ) );
}

// ---- PROG-B06 ----

// Book bonuses counted prerequisites again at every level of add() and every +=: Through the
// Lens gives Knapping 0.15 directly and 0.15 through Speed Knapping, which requires it, yet
// with the book in hand Knapping got several copies, worth about 0.28.
TEST_CASE( "prog-b06_a_book_counts_a_proficiency_once", "[audit][proficiency]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    REQUIRE( proficiency_prof_knapping_speed->required_proficiencies().count(
                 proficiency_prof_knapping ) == 1 );
    const item book( itype_fun_survival );

    const book_proficiency_bonuses own = book.get_book_proficiency_bonuses();
    CHECK( own.fail_factor( proficiency_prof_knapping ) == Approx( 0.15f ) );
    CHECK( own.time_factor( proficiency_prof_knapping ) == Approx( 0.15f ) );
    CHECK( own.fail_factor( proficiency_prof_knapping_speed ) == Approx( 0.15f ) );

    book_proficiency_bonuses summed;
    summed += own;
    CHECK( summed.fail_factor( proficiency_prof_knapping ) == Approx( 0.15f ) );

    item in_hand( itype_fun_survival );
    u.wield( in_hand );
    const book_proficiency_bonuses nearby = u.book_bonuses_nearby();
    CHECK( nearby.fail_factor( proficiency_prof_knapping ) == Approx( 0.15f ) );
    CHECK( nearby.fail_factor( proficiency_prof_knapping_speed ) == Approx( 0.15f ) );
}

// ---- PROG-B07 ----

static int addictions_of( const Character &you, const addiction_id &type )
{
    return static_cast<int>( std::count_if( you.addictions.begin(), you.addictions.end(),
    [&type]( const addiction & add ) {
        return add.type == type;
    } ) );
}

// The profession's and each hobby's addictions were all appended, so two of one type became
// two entries: only the first was ever sated, and the other stayed in withdrawal for good.
TEST_CASE( "prog-b07_profession_and_hobby_addictions_of_a_type_merge", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        get_avatar().addictions.clear();
        clear_avatar();
    } );
    u.addictions.clear();
    u.prof = &profession_test_prog_b07_profession.obj();
    u.hobbies.insert( &profession_test_prog_b07_hobby.obj() );

    Character &you = u;
    you.initialize( false );

    CHECK( addictions_of( u, addiction_caffeine ) == 1 );
    CHECK( u.addiction_level( addiction_caffeine ) == 30 );
}

// Characters made before the fix have both entries in their save; loading merges them.
TEST_CASE( "prog-b07_a_save_with_duplicate_addictions_loads_them_merged", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        get_avatar().addictions.clear();
        clear_avatar();
    } );
    addiction sated( addiction_caffeine, 10 );
    sated.sated = 2_hours;
    addiction withdrawn( addiction_caffeine, 30 );
    withdrawn.sated = -3_days;
    u.addictions = { sated, withdrawn };

    std::ostringstream os;
    JsonOut jsout( os );
    u.serialize( jsout );
    avatar loaded;
    loaded.deserialize( json_loader::from_string( os.str() ).get_object() );

    CHECK( addictions_of( loaded, addiction_caffeine ) == 1 );
    CHECK( loaded.addiction_level( addiction_caffeine ) == 30 );
    REQUIRE( !loaded.addictions.empty() );
    CHECK( loaded.addictions.front().sated == 2_hours );
}

// ---- PROG-B08 ----

// Picking another profession in the character creator kept the hobbies and traits the new
// profession excludes, and the excluded hobby dropped out of the list, so it couldn't be
// deselected either: its traits, spells and point cost stayed.
TEST_CASE( "prog-b08_a_new_profession_drops_the_hobbies_and_traits_it_excludes",
           "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    const profession &hobby = profession_test_prog_b08_hobby.obj();
    // As choosing the hobby in the creator does
    u.hobbies.insert( &hobby );
    u.toggle_trait_deps( trait_test_prog_b08_hobby_trait );
    u.toggle_trait_deps( trait_test_prog_b08_forbidden_trait );
    REQUIRE( u.has_trait( trait_test_prog_b08_hobby_trait ) );
    REQUIRE( u.has_trait( trait_test_prog_b08_forbidden_trait ) );

    SECTION( "a profession that excludes them" ) {
        u.prof = &profession_test_prog_b08_profession.obj();
        REQUIRE_FALSE( u.prof->allows_hobby( hobby.ident() ) );
        REQUIRE( u.prof->is_forbidden_trait( trait_test_prog_b08_forbidden_trait ) );

        const std::vector<std::string> dropped = char_creation::drop_what_profession_excludes( u );

        CHECK( dropped.size() == 2 );
        CHECK( u.hobbies.empty() );
        CHECK_FALSE( u.has_trait( trait_test_prog_b08_hobby_trait ) );
        CHECK_FALSE( u.has_trait( trait_test_prog_b08_forbidden_trait ) );
    }
    SECTION( "a profession that allows them" ) {
        u.prof = profession::generic();
        CHECK( char_creation::drop_what_profession_excludes( u ).empty() );
        CHECK( u.hobbies.count( &hobby ) == 1 );
        CHECK( u.has_trait( trait_test_prog_b08_hobby_trait ) );
        CHECK( u.has_trait( trait_test_prog_b08_forbidden_trait ) );
    }
}

// The default background hobbies were added whatever the profession allowed.
TEST_CASE( "prog-b08_the_default_background_leaves_out_excluded_hobbies", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    u.prof = &profession_test_prog_b08_profession.obj();
    REQUIRE_FALSE( u.prof->allows_hobby( profession_driving_license ) );

    u.add_default_background();

    CHECK( u.hobbies.count( &profession_driving_license.obj() ) == 0 );
    CHECK( u.hobbies.count( &profession_computer_literate.obj() ) == 1 );
}

// ---- PROG-B09 ----

// The skills tab added the hobby's level to the levels bought, but a hobby's skill is a floor:
// the character starts with the higher of bought + profession and the hobby's level.  The tab
// showed "Driving (2 + 2)" for a character that started with Driving 2.
TEST_CASE( "prog-b09_the_skills_tab_shows_the_level_the_character_starts_with",
           "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    const profession &license = profession_driving_license.obj();
    const profession::StartingSkillList license_skills = license.skills();
    REQUIRE( std::count( license_skills.begin(), license_skills.end(),
                         profession::StartingSkill( skill_driving, 2 ) ) == 1 );
    u.hobbies.insert( &license );
    const std::string name = skill_driving->name();
    const auto entry_for = [&u]( int bought ) {
        u.set_skill_level( skill_driving, bought );
        return remove_color_tags( get_skill_entry_text( skill_driving, u ) );
    };

    CHECK( entry_for( 0 ) == string_format( "%s (0 + 2)", name ) );
    CHECK( entry_for( 1 ) == string_format( "%s (1 + 1)", name ) );
    CHECK( entry_for( 2 ) == string_format( "%s (2)", name ) );
    CHECK( entry_for( 3 ) == string_format( "%s (3)", name ) );
}

// ---- PROG-B10 ----

static double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// Setting a proficiency to 100% directly from an EOC erased its progress and then learned it
// only if the character knew its prerequisites, so without them the character ended up with
// neither the proficiency nor the progress.  It now learns the prerequisites too, and one that
// was in progress keeps no progress entry beside being known.
TEST_CASE( "prog-b10_eoc_direct_100_percent_learns_the_proficiency", "[audit][proficiency]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), nullptr );
    REQUIRE_FALSE( u.has_proficiency( proficiency_prof_knapping ) );

    eval_math( d, "u_proficiency('prof_knapping_speed', 'format': 'percent', 'direct': true) = 50" );
    CHECK( u.get_proficiency_practice( proficiency_prof_knapping_speed ) == Approx( 0.5f ) );

    SECTION( "without its prerequisite" ) {
        eval_math( d, "u_proficiency('prof_knapping_speed', 'format': 'percent', 'direct': true) = 100" );
        CHECK( u.has_proficiency( proficiency_prof_knapping_speed ) );
        CHECK( u.has_proficiency( proficiency_prof_knapping ) );
    }
    SECTION( "with its prerequisite half learned" ) {
        eval_math( d, "u_proficiency('prof_knapping', 'format': 'percent', 'direct': true) = 50" );
        eval_math( d, "u_proficiency('prof_knapping_speed', 'format': 'percent', 'direct': true) = 100" );
        CHECK( u.has_proficiency( proficiency_prof_knapping_speed ) );
        CHECK( u.has_proficiency( proficiency_prof_knapping ) );
        CHECK( u.get_proficiency_practice( proficiency_prof_knapping ) == Approx( 1.0f ) );
    }
}

// Learning a proficiency with its prerequisites recursed through them with nothing to stop a
// circle, so two proficiencies that required each other overflowed the stack.
TEST_CASE( "prog-b10_a_circle_of_prerequisites_is_learned_once_round", "[audit][proficiency]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    REQUIRE( proficiency_test_prog_b10_circle_a->required_proficiencies().count(
                 proficiency_test_prog_b10_circle_b ) == 1 );
    REQUIRE( proficiency_test_prog_b10_circle_b->required_proficiencies().count(
                 proficiency_test_prog_b10_circle_a ) == 1 );

    SECTION( "set to 100% from an EOC" ) {
        dialogue d( get_talker_for( u ), nullptr );
        eval_math( d,
                   "u_proficiency('test_prog_b10_circle_a', 'format': 'percent', 'direct': true) = 100" );
    }
    SECTION( "granted with its prerequisites" ) {
        u.add_proficiency( proficiency_test_prog_b10_circle_a, false, true );
    }
    CHECK( u.has_proficiency( proficiency_test_prog_b10_circle_a ) );
    CHECK( u.has_proficiency( proficiency_test_prog_b10_circle_b ) );
}

// ---- PROG-B11 ----

// A nested trait group entry reported "unknown" when its group was valid, and nothing when it
// wasn't.
TEST_CASE( "prog-b11_a_nested_trait_group_is_checked_the_right_way_round", "[audit][mutation]" )
{
    const Trait_group_creator valid( Trait_group_BG_survival_story_UNIVERSAL, 100 );
    CHECK( capture_debugmsg_during( [&valid]() {
        valid.check_consistency();
    } ).empty() );

    const Trait_group_creator unknown( Trait_group_test_prog_b11_no_such_group, 100 );
    CHECK_FALSE( capture_debugmsg_during( [&unknown]() {
        unknown.check_consistency();
    } ).empty() );
}

// Creating from an unknown nested group left its id on the recursion list, so the parent
// popped the wrong id and a later entry naming the parent was reported as recursion.
TEST_CASE( "prog-b11_an_unknown_nested_group_leaves_the_recursion_list_as_it_was",
           "[audit][mutation]" )
{
    const Trait_group_creator unknown( Trait_group_test_prog_b11_no_such_group, 100 );
    Trait_creation_data::RecursionList rec;
    capture_debugmsg_during( [&unknown, &rec]() {
        unknown.create( rec );
    } );
    CHECK( rec.empty() );
}

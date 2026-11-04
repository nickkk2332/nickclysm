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
#include "debug.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "player_helpers.h"
#include "proficiency.h"
#include "profession.h"
#include "recipe.h"
#include "type_id.h"

// ---- PROG-B04 ----

static const bionic_id bio_armor_eyes( "bio_armor_eyes" );
static const bionic_id bio_carbon( "bio_carbon" );

static const profession_id profession_test_prog_b04_hobby( "test_prog_b04_hobby" );
static const profession_id profession_test_prog_b04_profession( "test_prog_b04_profession" );

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

static const profession_id profession_test_prog_b05_hobby( "test_prog_b05_hobby" );
static const profession_id profession_test_prog_b05_profession( "test_prog_b05_profession" );

static const recipe_id recipe_large_tent_kit_from_broken( "large_tent_kit_from_broken" );
static const recipe_id recipe_tent_kit_from_broken( "tent_kit_from_broken" );

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

static const itype_id itype_fun_survival( "fun_survival" );

static const proficiency_id proficiency_prof_knapping( "prof_knapping" );
static const proficiency_id proficiency_prof_knapping_speed( "prof_knapping_speed" );

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

static const addiction_id addiction_caffeine( "caffeine" );

static const profession_id profession_test_prog_b07_hobby( "test_prog_b07_hobby" );
static const profession_id profession_test_prog_b07_profession( "test_prog_b07_profession" );

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

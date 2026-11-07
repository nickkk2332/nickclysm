#include <algorithm>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_creator_ui.h"
#include "debug.h"
#include "enum_traits.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "itype.h"
#include "json_loader.h"
#include "mutation.h"
#include "npc.h"
#include "player_helpers.h"
#include "profession.h"
#include "proficiency.h"
#include "scenario.h"
#include "string_formatter.h"
#include "type_id.h"
#include "value_ptr.h"

static const flag_id json_flag_ENJOYED_BY_CANNIBALS( "ENJOYED_BY_CANNIBALS" );

static const itype_id itype_cookbook_human( "cookbook_human" );
static const itype_id itype_teleumbrella( "teleumbrella" );

static const json_character_flag json_flag_CANNIBAL( "CANNIBAL" );
static const json_character_flag json_flag_HATES_BOOKS( "HATES_BOOKS" );
static const json_character_flag json_flag_ILLITERATE( "ILLITERATE" );
static const json_character_flag json_flag_LOVES_BOOKS( "LOVES_BOOKS" );
static const json_character_flag
json_flag_NO_BOOK_COMPLEXITY_PENALTY( "NO_BOOK_COMPLEXITY_PENALTY" );
static const json_character_flag json_flag_SPIRITUAL( "SPIRITUAL" );

static const profession_id profession_churl( "churl" );
static const profession_id profession_driving_license( "driving_license" );
static const profession_id profession_test_prog_j01_chosen_hobby( "test_prog_j01_chosen_hobby" );
static const profession_id
profession_test_prog_j01_group_profession( "test_prog_j01_group_profession" );
static const profession_id profession_test_prog_j01_hobby( "test_prog_j01_hobby" );
static const profession_id
profession_test_prog_j01_none_profession( "test_prog_j01_none_profession" );
static const profession_id
profession_test_prog_j01_skip_profession( "test_prog_j01_skip_profession" );

static const proficiency_id proficiency_test_prog_j02_a( "test_prog_j02_a" );
static const proficiency_id proficiency_test_prog_j02_b( "test_prog_j02_b" );
static const proficiency_id proficiency_test_prog_j02_checked( "test_prog_j02_checked" );

static const string_id<scenario>
scenario_test_prog_j01_group_scenario( "test_prog_j01_group_scenario" );
static const string_id<scenario>
scenario_test_prog_j01_skip_scenario( "test_prog_j01_skip_scenario" );

static const trait_id trait_ALBINO( "ALBINO" );
static const trait_id trait_CANNIBAL( "CANNIBAL" );
static const trait_id trait_HATES_BOOKS( "HATES_BOOKS" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_LOVES_BOOKS( "LOVES_BOOKS" );
static const trait_id trait_PROF_DICEMASTER( "PROF_DICEMASTER" );
static const trait_id trait_SPIRITUAL( "SPIRITUAL" );
static const trait_id trait_test_prog_j01_hobby_trait( "test_prog_j01_hobby_trait" );
static const trait_id trait_test_prog_j03_book_fun( "test_prog_j03_book_fun" );
static const trait_id trait_test_prog_j03_cannibal( "test_prog_j03_cannibal" );
static const trait_id trait_test_prog_j03_illiterate( "test_prog_j03_illiterate" );
static const trait_id
trait_test_prog_j03_no_complexity_penalty( "test_prog_j03_no_complexity_penalty" );

namespace
{

bool lists_hobby( const std::vector<profession_id> &hobbies, const profession_id &hobby )
{
    return std::find( hobbies.begin(), hobbies.end(), hobby ) != hobbies.end();
}

// Loads `json` (a proficiency without "type") as if a mod no other definition came from had it,
// so that it may redefine an id the test data defines (two definitions from one mod are an
// error).  The test data has no broken proficiency: the load check would report it.
void define_proficiency( const std::string &json )
{
    static int mods = 0;
    proficiency::load_proficiencies( json_loader::from_string( json ),
                                     string_format( "audit_m_m16a_mod_%d", mods++ ) );
}

// test_prog_j02_b as the test data has it, or requiring test_prog_j02_a, which requires it
void define_prog_j02_b( bool closed )
{
    define_proficiency( string_format( R"({
        "id": "test_prog_j02_b",
        "category": "prof_test_category",
        "name": { "str": "PROG-J02 B" },
        "description": "Required by test_prog_j02_a.",
        "can_learn": true,
        "time_to_learn": "1 h"%s
    })", closed ? R"(, "required_proficiencies": [ "test_prog_j02_a" ])" : "" ) );
}

} // namespace

// ---- PROG-J01 ----

// SKIP_DEFAULT_BACKGROUND was read only from the scenario, though the docs and the professions
// that set it (the churl, several mod professions) put it on the profession, so a medieval churl
// still got a driving license; and the default hobbies were always adult_basic_background.
TEST_CASE( "prog-j01_the_profession_then_the_scenario_pick_the_default_background",
           "[audit][chargen]" )
{
    using char_creation::default_background_hobbies;
    const profession *unemployed = profession::generic();
    const profession *group_prof = &profession_test_prog_j01_group_profession.obj();
    const profession *skip_prof = &profession_test_prog_j01_skip_profession.obj();
    const profession *none_prof = &profession_test_prog_j01_none_profession.obj();
    const scenario *group_scen = &scenario_test_prog_j01_group_scenario.obj();
    const scenario *skip_scen = &scenario_test_prog_j01_skip_scenario.obj();
    const std::vector<profession_id> the_group = { profession_test_prog_j01_hobby };

    SECTION( "no group named: adult_basic_background" ) {
        const std::vector<profession_id> hobbies = default_background_hobbies( unemployed, nullptr );
        CHECK( lists_hobby( hobbies, profession_driving_license ) );
        CHECK_FALSE( lists_hobby( hobbies, profession_test_prog_j01_hobby ) );
    }
    SECTION( "the profession's flag" ) {
        CHECK( default_background_hobbies( skip_prof, nullptr ).empty() );
        CHECK( default_background_hobbies( skip_prof, group_scen ).empty() );
        REQUIRE( profession_churl->has_flag( "SKIP_DEFAULT_BACKGROUND" ) );
        CHECK( default_background_hobbies( &profession_churl.obj(), nullptr ).empty() );
    }
    SECTION( "the profession's group comes first" ) {
        CHECK( default_background_hobbies( group_prof, nullptr ) == the_group );
        CHECK( default_background_hobbies( group_prof, skip_scen ) == the_group );
        CHECK( default_background_hobbies( none_prof, group_scen ).empty() );
    }
    SECTION( "then the scenario's group or flag" ) {
        CHECK( default_background_hobbies( unemployed, group_scen ) == the_group );
        CHECK( default_background_hobbies( unemployed, skip_scen ).empty() );
    }
}

TEST_CASE( "prog-j01_the_avatar_gets_its_scenarios_default_background_npcs_dont",
           "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const scenario *before = get_scenario();
    on_out_of_scope restore( [before]() {
        set_scenario( before );
        clear_avatar();
    } );
    set_scenario( &scenario_test_prog_j01_group_scenario.obj() );

    u.prof = profession::generic();
    u.hobbies.clear();
    u.add_default_background();
    CHECK( u.hobbies.size() == 1 );
    CHECK( u.hobbies.count( &profession_test_prog_j01_hobby.obj() ) == 1 );

    // Random NPCs take their background from the generic scenario (PROG-B03)
    standard_npc guy( "Background" );
    guy.prof = profession::generic();
    guy.hobbies.clear();
    guy.add_default_background();
    CHECK( guy.hobbies.count( &profession_driving_license.obj() ) == 1 );
    CHECK( guy.hobbies.count( &profession_test_prog_j01_hobby.obj() ) == 0 );
}

// The default background was added once, before the creator opened, and choosing another
// profession never changed it.
TEST_CASE( "prog-j01_choosing_a_profession_swaps_its_default_background", "[audit][chargen]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const scenario *before = get_scenario();
    on_out_of_scope restore( [before]() {
        set_scenario( before );
        clear_avatar();
    } );
    set_scenario( scenario::generic() );
    const profession *unemployed = profession::generic();
    const profession *license = &profession_driving_license.obj();
    const profession *group_hobby = &profession_test_prog_j01_hobby.obj();
    const profession *chosen_hobby = &profession_test_prog_j01_chosen_hobby.obj();

    u.prof = unemployed;
    u.hobbies.clear();
    u.add_default_background();
    u.hobbies.insert( chosen_hobby );
    REQUIRE( u.hobbies.count( license ) == 1 );
    REQUIRE_FALSE( u.has_trait( trait_test_prog_j01_hobby_trait ) );

    // What the creator does on choosing a profession
    u.prof = &profession_test_prog_j01_group_profession.obj();
    char_creation::change_default_background( u, unemployed );
    CHECK( u.hobbies.count( license ) == 0 );
    CHECK( u.hobbies.count( group_hobby ) == 1 );
    CHECK( u.has_trait( trait_test_prog_j01_hobby_trait ) );
    CHECK( u.hobbies.count( chosen_hobby ) == 1 );

    const profession *group_prof = u.prof;
    u.prof = &profession_test_prog_j01_skip_profession.obj();
    char_creation::change_default_background( u, group_prof );
    CHECK( u.hobbies.count( group_hobby ) == 0 );
    CHECK_FALSE( u.has_trait( trait_test_prog_j01_hobby_trait ) );
    CHECK( u.hobbies.count( license ) == 0 );
    CHECK( u.hobbies.count( chosen_hobby ) == 1 );

    const profession *skip_prof = u.prof;
    u.prof = unemployed;
    char_creation::change_default_background( u, skip_prof );
    CHECK( u.hobbies.count( license ) == 1 );
    CHECK( u.hobbies.count( group_hobby ) == 0 );
    CHECK( u.hobbies.count( chosen_hobby ) == 1 );
}

// NO_BONUS_ITEMS was documented as a scenario flag but only worked on professions; the avatar's
// scenario now leaves the bonus items out through this.
TEST_CASE( "prog-j01_profession_items_can_leave_out_the_bonus_items", "[audit][chargen]" )
{
    const profession *unemployed = profession::generic();
    REQUIRE_FALSE( unemployed->has_flag( "NO_BONUS_ITEMS" ) );
    const std::vector<trait_id> albino = { trait_ALBINO };
    const auto has_umbrella = []( const std::list<item> &items ) {
        return std::any_of( items.begin(), items.end(), []( const item & it ) {
            return it.typeId() == itype_teleumbrella;
        } );
    };
    CHECK( has_umbrella( unemployed->items( true, albino ) ) );
    CHECK_FALSE( has_umbrella( unemployed->items( true, albino, false ) ) );
}

// ---- PROG-J02 ----

// Nothing checked proficiencies when loading: a circle of prerequisites overflowed the stack
// (learn() guards against that now, PROG-B10) and still left every proficiency in it impossible
// to practice.
TEST_CASE( "prog-j02_a_circle_of_prerequisites_is_reported", "[audit][proficiency]" )
{
    CHECK( proficiency::prerequisite_cycles().empty() );
    REQUIRE( proficiency_test_prog_j02_a->required_proficiencies().count(
                 proficiency_test_prog_j02_b ) == 1 );

    define_prog_j02_b( true );
    on_out_of_scope restore( []() {
        define_prog_j02_b( false );
    } );
    const std::vector<std::vector<proficiency_id>> cycles = proficiency::prerequisite_cycles();
    REQUIRE( cycles.size() == 1 );
    const std::vector<proficiency_id> &cycle = cycles.front();
    CHECK( cycle.size() == 3 );
    CHECK( cycle.front() == cycle.back() );
    CHECK( std::count( cycle.begin(), cycle.end(), proficiency_test_prog_j02_a ) >= 1 );
    CHECK( std::count( cycle.begin(), cycle.end(), proficiency_test_prog_j02_b ) >= 1 );

    const std::string msg = capture_debugmsg_during( []() {
        proficiency::check_all();
    } );
    CHECK( msg.find( "circle" ) != std::string::npos );
    CHECK( msg.find( "test_prog_j02_a" ) != std::string::npos );
}

TEST_CASE( "prog-j02_unknown_ids_and_a_missing_time_to_learn_are_reported",
           "[audit][proficiency]" )
{
    const std::string as_loaded = R"({
        "id": "test_prog_j02_checked",
        "category": "prof_test_category",
        "name": { "str": "PROG-J02 checked" },
        "description": "The test redefines it with what the load check reports.",
        "can_learn": true,
        "time_to_learn": "1 h"
    })";
    on_out_of_scope restore( [as_loaded]() {
        define_proficiency( as_loaded );
    } );
    CHECK( capture_debugmsg_during( []() {
        proficiency_test_prog_j02_checked->check();
    } ).empty() );

    define_proficiency( R"({
        "id": "test_prog_j02_checked",
        "category": "test_prog_j02_no_such_category",
        "name": { "str": "PROG-J02 checked" },
        "description": "The test redefines it with what the load check reports.",
        "can_learn": true,
        "required_proficiencies": [ "test_prog_j02_no_such_proficiency" ]
    })" );
    const std::string msg = capture_debugmsg_during( []() {
        proficiency_test_prog_j02_checked->check();
    } );
    CHECK( msg.find( "unknown category test_prog_j02_no_such_category" ) != std::string::npos );
    CHECK( msg.find( "requires unknown proficiency test_prog_j02_no_such_proficiency" ) !=
           std::string::npos );
    CHECK( msg.find( "no time_to_learn" ) != std::string::npos );
}

// ---- PROG-J03 ----

// The rules check flags now; the traits that had them hardcoded carry the flags, so they work as
// before.
TEST_CASE( "prog-j03_the_reading_traits_carry_the_reading_flags", "[audit][reading]" )
{
    CHECK( trait_ILLITERATE->flags.count( json_flag_ILLITERATE ) == 1 );
    CHECK( trait_LOVES_BOOKS->flags.count( json_flag_LOVES_BOOKS ) == 1 );
    CHECK( trait_HATES_BOOKS->flags.count( json_flag_HATES_BOOKS ) == 1 );
    CHECK( trait_PROF_DICEMASTER->flags.count( json_flag_NO_BOOK_COMPLEXITY_PENALTY ) == 1 );
    CHECK( trait_SPIRITUAL->flags.count( json_flag_SPIRITUAL ) == 1 );
    CHECK( trait_CANNIBAL->flags.count( json_flag_CANNIBAL ) == 1 );
    CHECK( item( itype_cookbook_human ).has_flag( json_flag_ENJOYED_BY_CANNIBALS ) );
}

// To Serve Man was fun only with the CANNIBAL trait, the PSYCHOPATH flag or the SAPIOVORE trait,
// while everything else about eating people checks the CANNIBAL flag: mod mutations with the flag
// (Xedra's vampires) enjoyed eating people but hated the cookbook.
TEST_CASE( "prog-j03_the_cannibal_flag_enjoys_to_serve_man", "[audit][reading]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    const item book( itype_cookbook_human );
    const int boring = u.book_fun_for( book, u );
    REQUIRE( boring < 0 );

    SECTION( "a mutation with the flag" ) {
        u.toggle_trait( trait_test_prog_j03_cannibal );
        REQUIRE( u.has_flag( json_flag_CANNIBAL ) );
        REQUIRE_FALSE( u.has_trait( trait_CANNIBAL ) );
        CHECK( u.book_fun_for( book, u ) == -boring );
    }
    SECTION( "the CANNIBAL trait, as before" ) {
        u.toggle_trait( trait_CANNIBAL );
        CHECK( u.book_fun_for( book, u ) == -boring );
    }
}

TEST_CASE( "prog-j03_the_illiterate_flag_stops_reading", "[audit][reading]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    const item book( itype_cookbook_human );
    REQUIRE( book.type->book->intel > 0 );
    const auto illiterate = [&u, &book]() {
        return static_cast<bool>( u.check_read_condition( book ) & read_condition_result::ILLITERATE );
    };
    REQUIRE_FALSE( illiterate() );

    SECTION( "a mutation with the flag" ) {
        u.toggle_trait( trait_test_prog_j03_illiterate );
        CHECK( illiterate() );
    }
    SECTION( "the ILLITERATE trait, as before" ) {
        u.toggle_trait( trait_ILLITERATE );
        CHECK( illiterate() );
    }
}

TEST_CASE( "prog-j03_book_fun_enchantment_and_complexity_flag", "[audit][reading]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    const item book( itype_cookbook_human );

    SECTION( "BOOK_FUN adds to every book's fun" ) {
        const int before = u.book_fun_for( book, u );
        u.toggle_trait( trait_test_prog_j03_book_fun );
        u.recalculate_enchantment_cache();
        CHECK( u.book_fun_for( book, u ) == before + 2 );
    }
    SECTION( "NO_BOOK_COMPLEXITY_PENALTY reads a book above one's intelligence no slower" ) {
        u.set_int_base( 1 );
        REQUIRE( book.type->book->intel > u.get_int() );
        const time_duration penalized = u.time_to_read( book, u );
        u.toggle_trait( trait_test_prog_j03_no_complexity_penalty );
        CHECK( u.time_to_read( book, u ) < penalized );
    }
}

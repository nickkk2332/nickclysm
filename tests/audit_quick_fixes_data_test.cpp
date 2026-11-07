#include <functional>
#include <list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "avatar.h"
#include "butchery.h"
#include "butchery_requirements.h"
#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "enum_conversions.h"
#include "flexbuffer_json.h"
#include "harvest.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "player_helpers.h"
#include "temp_crafting_inventory.h"
#include "timed_event.h"
#include "type_id.h"

static const butchery_requirements_id
butchery_requirements_audit_craft_j04_unknown_reqs( "audit_craft_j04_unknown_reqs" );
static const butchery_requirements_id
butchery_requirements_default( "default" );

static const harvest_id harvest_list_audit_craft_j04_harvest( "audit_craft_j04_harvest" );

static const requirement_id requirement_data_field_dress( "field_dress" );
static const requirement_id
requirement_data_field_dress_no_surface( "field_dress_no_surface" );

static const spell_id spell_audit_evt_j01_robot_attack( "audit_evt_j01_robot_attack" );
static const spell_id
spell_audit_evt_j01_upper_case_timed_event( "audit_evt_j01_upper_case_timed_event" );
static const spell_id spell_death_triffid_heart( "death_triffid_heart" );

namespace
{
bool contains( const std::string_view haystack, const std::string_view needle )
{
    return haystack.find( needle ) != std::string_view::npos;
}

std::string saved_timed_events()
{
    std::ostringstream os;
    JsonOut jsout( os );
    timed_event_manager::serialize_all( jsout );
    return os.str();
}

// Empties the timed event queue for a test, and puts it back afterwards
struct timed_events_restorer {
    std::string saved = saved_timed_events();
    timed_events_restorer() {
        get_timed_events() = timed_event_manager();
    }
    ~timed_events_restorer() noexcept( false ) {
        get_timed_events() = timed_event_manager();
        timed_event_manager::unserialize_all( json_loader::from_string( saved ).get_array() );
    }
};

// One size entry of a butchery_requirement, the same requirement for every butcher type
std::string butchery_size_entry( const std::string &req )
{
    std::string entry;
    for( int i = 0; i < static_cast<int>( butcher_type::NUM_TYPES ); ++i ) {
        entry += entry.empty() ? "{ " : ", ";
        entry += "\"" + io::enum_to_string( static_cast<butcher_type>( i ) ) + "\": \"" + req + "\"";
    }
    return entry + " }";
}

// The "requirements" array for one speed multiplier, with `sizes` size entries
std::string butchery_sizes( const std::string &req, int sizes )
{
    std::string array;
    for( int i = 0; i < sizes; ++i ) {
        array += array.empty() ? "[ " : ", ";
        array += butchery_size_entry( req );
    }
    return array + " ]";
}
} // namespace

// EVT-J01: the triffid heart's death spell had "effect_str": "ROOTS_DIE", but the timed_event
// effect only knew "roots_die", so it queued an inert NONE event and the roots never died.
TEST_CASE( "evt-j01_triffid_heart_death_spell_queues_roots_die", "[audit][magic][timed_event]" )
{
    clear_avatar();
    timed_events_restorer restore;
    avatar &u = get_avatar();
    const time_point cast_turn = calendar::turn;

    SECTION( "the triffid heart's death spell" ) {
        spell( spell_death_triffid_heart ).cast_spell_effect( u, u.pos_bub() );
    }
    SECTION( "a timed_event spell with an upper-case effect_str" ) {
        spell( spell_audit_evt_j01_upper_case_timed_event ).cast_spell_effect( u, u.pos_bub() );
    }

    const timed_event *roots_die = get_timed_events().get( timed_event_type::ROOTS_DIE );
    REQUIRE( roots_die != nullptr );
    // 60000 moves of duration
    CHECK( roots_die->when == cast_turn + 600_turns );
    CHECK_FALSE( get_timed_events().queued( timed_event_type::NONE ) );
    CHECK( get_timed_events().get_all().size() == 1 );
}

// MAGIC-J04: the timed_event names were matched case-sensitively, unknown names silently
// became an inert NONE event, and spell_type::check_consistency never checked them.  The docs
// listed robot_attack and wanted, which do nothing when they happen.
TEST_CASE( "magic-j04_timed_event_names_are_case_insensitive_and_checked_at_load",
           "[audit][magic][timed_event]" )
{
    CHECK( spell_effect::timed_event_from_effect_str( "roots_die" ) == timed_event_type::ROOTS_DIE );
    CHECK( spell_effect::timed_event_from_effect_str( "ROOTS_DIE" ) == timed_event_type::ROOTS_DIE );
    CHECK( spell_effect::timed_event_from_effect_str( "Artifact_Light" ) ==
           timed_event_type::ARTIFACT_LIGHT );
    CHECK( spell_effect::timed_event_from_effect_str( "dim" ) == timed_event_type::DIM );
    CHECK_FALSE( spell_effect::timed_event_from_effect_str( "robot_attack" ).has_value() );
    CHECK_FALSE( spell_effect::timed_event_from_effect_str( "wanted" ).has_value() );
    CHECK_FALSE( spell_effect::timed_event_from_effect_str( "none" ).has_value() );
    CHECK_FALSE( spell_effect::timed_event_from_effect_str( "" ).has_value() );

    // A throwaway spell, loaded once per test run (a second load from the same source throws)
    if( !spell_audit_evt_j01_robot_attack.is_valid() ) {
        JsonObject jo = json_loader::from_string( R"({ "id": "audit_evt_j01_robot_attack",
            "name": "audit spell", "description": "audit spell", "valid_targets": [ "self" ],
            "shape": "blast", "effect": "timed_event", "effect_str": "robot_attack" })" );
        spell_type::load_spell( jo, "" );
    }
    const std::string errors = capture_debugmsg_during( []() {
        spell_type::check_consistency();
    } );
    CAPTURE( errors );
    CHECK( contains( errors, R"(spell audit_evt_j01_robot_attack uses effect "timed_event")" ) );
    // The shipped and test timed_event spells pass
    CHECK_FALSE( contains( errors, "death_triffid_heart" ) );
    CHECK_FALSE( contains( errors, "audit_evt_j01_upper_case_timed_event" ) );
    CHECK_FALSE( contains( errors, "AEA_LIGHT" ) );
    CHECK_FALSE( contains( errors, "AEA_DIM" ) );

    // At cast time an unknown name is reported and queues nothing
    timed_events_restorer restore;
    avatar &u = get_avatar();
    const std::string cast_errors = capture_debugmsg_during( [&u]() {
        spell( spell_audit_evt_j01_robot_attack ).cast_spell_effect( u, u.pos_bub() );
    } );
    CHECK( contains( cast_errors, R"(unknown timed_event "robot_attack")" ) );
    CHECK( get_timed_events().get_all().empty() );
}

// DATA-J03: butchery_requirement loading only rejected too many size entries (and then just
// with a debugmsg), so a short array loaded and later crashed the butcher menu.
TEST_CASE( "data-j03_butchery_requirement_needs_one_entry_per_creature_size",
           "[audit][butchery]" )
{
    const auto load = []( int sizes ) {
        JsonObject jo = json_loader::from_string( R"({ "requirements": { "1.0": )" +
                        butchery_sizes( "field_dress", sizes ) + " } }" );
        butchery_requirements req;
        req.load( jo, "" );
    };
    CHECK_THROWS_WITH( load( 4 ),
                       Catch::Matchers::Contains( "has 4 entries, but it needs exactly 5" ) );
    CHECK_THROWS_WITH( load( 6 ),
                       Catch::Matchers::Contains( "has 6 entries, but it needs exactly 5" ) );
    CHECK_NOTHROW( load( 5 ) );
}

// CRAFT-J04: get_fastest_requirements used .at( size ) and dereferenced rbegin(), so an
// unknown butchery_requirements id (the factory's empty dummy) or a missing entry crashed.
TEST_CASE( "craft-j04_butchery_requirement_lookup_falls_back_instead_of_crashing",
           "[audit][butchery]" )
{
    clear_avatar();
    const avatar &u = get_avatar();
    const temp_crafting_inventory no_tools;

    SECTION( "nothing can be met: the requirement with the highest multiplier" ) {
        JsonObject jo = json_loader::from_string( R"({ "requirements": { "0.8": )" +
                        butchery_sizes( "field_dress", 5 ) + R"(, "1.3": )" +
                        butchery_sizes( "field_dress_no_surface", 5 ) + " } }" );
        butchery_requirements req;
        req.load( jo, "" );
        const std::pair<float, requirement_id> found = req.get_fastest_requirements( &u, no_tools,
                creature_size::huge, butcher_type::SKIN );
        CHECK( found.first == Approx( 1.3f ) );
        CHECK( found.second == requirement_data_field_dress_no_surface );
        CHECK( found.second != requirement_data_field_dress );
    }
    SECTION( "no requirement at all: a debugmsg and a null requirement" ) {
        const butchery_requirements empty;
        std::pair<float, requirement_id> found;
        const std::string errors = capture_debugmsg_during( [&]() {
            found = empty.get_fastest_requirements( &u, no_tools, creature_size::medium,
                                                    butcher_type::QUICK );
        } );
        CHECK( contains( errors, "has no requirement for creature size 3 and QUICK" ) );
        CHECK( found.first == Approx( 1.0f ) );
        CHECK( found.second.is_null() );
    }
}

// CRAFT-J04: butchery_requirements::check_consistency was never run, requirement ids in it were
// never checked, and nothing checked the butchery_requirements id a harvest list names.
TEST_CASE( "craft-j04_unknown_butchery_ids_are_reported_at_load", "[audit][butchery]" )
{
    // Throwaway definitions, loaded once per test run
    if( !butchery_requirements_audit_craft_j04_unknown_reqs.is_valid() ) {
        JsonObject jo = json_loader::from_string( R"({ "id": "audit_craft_j04_unknown_reqs",
            "requirements": { "1.0": )" + butchery_sizes( "audit_craft_j04_no_requirement", 5 ) +
                        " } }" );
        butchery_requirements::load_butchery_req( jo, "" );
    }
    if( !harvest_list_audit_craft_j04_harvest.is_valid() ) {
        JsonObject jo = json_loader::from_string( R"({ "id": "audit_craft_j04_harvest",
            "entries": [], "butchery_requirements": "audit_craft_j04_no_butchery" })" );
        harvest_list::load_harvest_list( jo, "" );
    }

    const std::string butchery_errors = capture_debugmsg_during( []() {
        butchery_requirements::check_consistency();
    } );
    CAPTURE( butchery_errors );
    CHECK( contains( butchery_errors, "butchery_requirement audit_craft_j04_unknown_reqs has "
                     R"(unknown requirement "audit_craft_j04_no_requirement")" ) );
    // The shipped requirements pass
    REQUIRE( butchery_requirements_default.is_valid() );
    CHECK_FALSE( contains( butchery_errors, "butchery_requirement default" ) );

    const std::string harvest_errors = capture_debugmsg_during( []() {
        harvest_list::check_consistency();
    } );
    CAPTURE( harvest_errors );
    CHECK( contains( harvest_errors, "Harvest id audit_craft_j04_harvest has unknown "
                     "butchery_requirements: audit_craft_j04_no_butchery" ) );
    CHECK_FALSE( contains( harvest_errors, "butchery_requirements: default" ) );
}

#include <map>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "json_error.h"
#include "json_loader.h"
#include "magic.h"
#include "magic_type.h"
#include "mongroup.h"
#include "player_helpers.h"
#include "type_id.h"

static const mongroup_id GROUP_AUDIT_MONDT_J01( "GROUP_AUDIT_MONDT_J01" );

static const mtype_id mon_zombie( "mon_zombie" );

static const trait_id trait_NIGHTVISION( "NIGHTVISION" );

// Loads a throwaway monster group from JSON. The caller removes it again with
// remove_audit_group() so it cannot leak into other tests.
static void load_audit_group( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    MonsterGroupManager::LoadMonsterGroup( jo );
}

static void remove_audit_group()
{
    MonsterGroupManager::Get_all_Groups().erase( GROUP_AUDIT_MONDT_J01 );
}

static std::string group_with_conditions( const std::string &conditions )
{
    return R"({ "id": "GROUP_AUDIT_MONDT_J01", "default": "mon_null", "monsters": [
        { "monster": "mon_zombie", "weight": 10, "conditions": )" + conditions + " } ] }";
}

// MONDT-J01: unknown "conditions" tags were ignored, so a typo such as "Night" made the
// entry spawn at all times instead of failing to load.
TEST_CASE( "mondt-j01_unknown_spawn_condition_fails_to_load", "[audit][monster]" )
{
    SECTION( "misspelled tag" ) {
        CHECK_THROWS_AS( load_audit_group( group_with_conditions( R"([ "Night" ])" ) ),
                         JsonError );
        remove_audit_group();
    }
    SECTION( "unknown tag next to valid ones" ) {
        CHECK_THROWS_AS( load_audit_group( group_with_conditions( R"([ "DAY", "SUMMMER" ])" ) ),
                         JsonError );
        remove_audit_group();
    }
    SECTION( "every valid tag still loads" ) {
        const std::string all_tags =
            R"([ "DAY", "NIGHT", "DUSK", "DAWN", "SPRING", "SUMMER", "AUTUMN", "WINTER" ])";
        CHECK_NOTHROW( load_audit_group( group_with_conditions( all_tags ) ) );
        CHECK( GROUP_AUDIT_MONDT_J01.obj().monsters.size() == 1 );
        remove_audit_group();
    }
}

// MONDT-J01: a monster group entry can be gated by a dialogue "condition" (u = avatar).
// The only entry has weight == freq_total, so it is always picked when its condition holds
// and the explicit null default is returned otherwise.
TEST_CASE( "mondt-j01_dialogue_condition_gates_spawn_entry", "[audit][monster]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    load_audit_group( R"({ "id": "GROUP_AUDIT_MONDT_J01", "default": "mon_null", "monsters": [
        { "monster": "mon_zombie", "weight": 10, "condition": { "u_has_trait": "NIGHTVISION" } }
    ] })" );
    const auto spawned = []() {
        std::vector<mtype_id> ids;
        for( int i = 0; i < 20; ++i ) {
            for( const MonsterGroupResult &r :
                 MonsterGroupManager::GetResultFromGroup( GROUP_AUDIT_MONDT_J01 ) ) {
                ids.push_back( r.id );
            }
        }
        return ids;
    };

    REQUIRE_FALSE( u.has_trait( trait_NIGHTVISION ) );
    for( const mtype_id &id : spawned() ) {
        CHECK( id.is_null() );
    }

    u.set_mutation( trait_NIGHTVISION );
    REQUIRE( u.has_trait( trait_NIGHTVISION ) );
    for( const mtype_id &id : spawned() ) {
        CHECK( id == mon_zombie );
    }
    remove_audit_group();
}

static bool contains( const std::string &haystack, const std::string &needle )
{
    return haystack.find( needle ) != std::string::npos;
}

// Loads a throwaway spell once per test run (a second load from the same source throws).
static void load_audit_spell( const std::string &id, const std::string &body )
{
    if( spell_id( id ).is_valid() ) {
        return;
    }
    JsonObject jo = json_loader::from_string( R"({ "id": ")" + id + R"(",
        "name": "audit spell", "description": "audit spell", "valid_targets": [ "self" ],
        "shape": "blast", )" + body + " }" );
    spell_type::load_spell( jo, "" );
}

// MAGIC-J03: spell_type::check_consistency only checked effect_str for summon_vehicle, so a
// wrong id (an EOC id given to "attack", a missing monster or EOC) only surfaced as a
// debugmsg when the spell was cast, and learn_spells / channel_data ids were never checked.
TEST_CASE( "magic-j03_spell_check_rejects_unknown_references", "[audit][magic]" )
{
    load_audit_spell( "audit_magic_j03_attack",
                      R"("effect": "attack", "effect_str": "EOC_audit_magic_j03_not_an_effect")" );
    load_audit_spell( "audit_magic_j03_summon",
                      R"("effect": "summon", "effect_str": "mon_audit_magic_j03_missing",
                         "flags": [ "PERMANENT" ])" );
    load_audit_spell( "audit_magic_j03_eoc",
                      R"("effect": "effect_on_condition", "effect_str": "EOC_audit_j03_gone")" );
    load_audit_spell( "audit_magic_j03_energy",
                      R"("effect": "recover_energy", "effect_str": "MANNA")" );
    load_audit_spell( "audit_magic_j03_learn",
                      R"("effect": "attack", "learn_spells": { "audit_magic_j03_no_spell": 1 })" );
    load_audit_spell( "audit_magic_j03_channel",
                      R"("effect": "attack", "channel_data": { "max_channel_turns": 2,
                         "channel_spell": "audit_magic_j03_no_channel",
                         "channel_end_spell": "audit_magic_j03_attack" })" );
    load_audit_spell( "audit_magic_j03_good",
                      R"("effect": "attack", "effect_str": "onfire",
                         "learn_spells": { "audit_magic_j03_attack": 2 })" );

    const std::string errors = capture_debugmsg_during( []() {
        spell_type::check_consistency();
    } );
    CAPTURE( errors );
    CHECK( contains( errors, "EOC_audit_magic_j03_not_an_effect" ) );
    CHECK( contains( errors, "mon_audit_magic_j03_missing" ) );
    CHECK( contains( errors, "EOC_audit_j03_gone" ) );
    CHECK( contains( errors, "MANNA" ) );
    CHECK( contains( errors, "audit_magic_j03_no_spell" ) );
    CHECK( contains( errors, "audit_magic_j03_no_channel" ) );
    // Valid references, and the shipped core and test spells, raise nothing.
    CHECK_FALSE( contains( errors, "audit_magic_j03_good" ) );
    CHECK_FALSE( contains( errors, "channel_end_spell" ) );
}

// MAGIC-J03: magic_type::check_consistency was never registered, and it dereferenced its
// formula ids without checking them; the energy vitamin and failure_eocs were never checked.
TEST_CASE( "magic-j03_magic_type_check_rejects_unknown_references", "[audit][magic]" )
{
    if( !magic_type_id( "audit_magic_j03_type" ).is_valid() ) {
        JsonObject jo = json_loader::from_string( R"({ "id": "audit_magic_j03_type",
            "energy_source": { "type": "VITAMIN", "vitamin": "audit_magic_j03_no_vitamin" },
            "casting_xp_formula_id": "audit_magic_j03_no_formula" })" );
        magic_type::load_magic_type( jo, "" );
    }
    const std::string errors = capture_debugmsg_during( []() {
        magic_type::check_consistency();
    } );
    CAPTURE( errors );
    CHECK( contains( errors, "audit_magic_j03_no_vitamin" ) );
    CHECK( contains( errors, R"(unknown jmath_function "audit_magic_j03_no_formula")" ) );
}

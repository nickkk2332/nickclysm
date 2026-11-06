#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "json_loader.h"
#include "magic.h"
#include "magic_type.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "mongroup.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "ret_val.h"
#include "type_id.h"

static const itype_id itype_match( "match" );
static const itype_id itype_test_eoc_use_rune( "test_eoc_use_rune" );

static const magic_type_id magic_audit_magic_j03_type( "audit_magic_j03_type" );

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
    // Group entries spawn only once the cataclysm has started, and the test before this one may
    // have left the clock earlier than that.
    restore_on_out_of_scope restore_turn( calendar::turn );
    if( calendar::turn <= calendar::start_of_cataclysm ) {
        calendar::turn = calendar::start_of_cataclysm + 1_minutes;
    }
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

static bool contains( const std::string_view haystack, const std::string_view needle )
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
    if( !magic_audit_magic_j03_type.is_valid() ) {
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

// USE-J01: the effect_on_conditions use action had no condition, its EOCs could neither
// choose how many charges to spend nor cancel (refunding the time), and the tile passed to
// it (u_activate's target_var) never reached them. The test rune's EOC_TEST_USE_RUNE spends
// a second, copies use_location to a global and sets _charges / _cancel from globals.
TEST_CASE( "use-j01_eoc_use_action_condition_charges_cancel_and_target", "[audit][iuse][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &u = get_avatar();
    map &here = get_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    globvars.set_global_value( "test_use_rune_runs", 0 );
    globvars.set_global_value( "test_use_rune_allowed", 1 );
    globvars.set_global_value( "test_use_rune_charges", 3 );
    globvars.set_global_value( "test_use_rune_cancel", 0 );

    item_location rune = u.i_add( item( itype_test_eoc_use_rune ) );
    REQUIRE( rune );
    rune->ammo_set( itype_match, 20 );
    REQUIRE( rune->ammo_remaining() == 20 );
    const std::string method = "effect_on_conditions";
    const tripoint_bub_ms target = u.pos_bub() + tripoint_rel_ms::east * 2;
    u.set_moves( 200 );

    SECTION( "the EOC picks the charges to spend and sees the target tile" ) {
        u.invoke_item( rune.get_item(), method, target );
        CHECK( globvars.get_global_value( "test_use_rune_runs" ) == 1 );
        CHECK( rune->ammo_remaining() == 17 );
        CHECK( u.get_moves() == 100 );
        const diag_value &loc = globvars.get_global_value( "test_use_rune_loc" );
        REQUIRE( loc.is_tripoint() );
        CHECK( loc.tripoint() == here.get_abs( target ) );
    }
    SECTION( "zero charges spends nothing but keeps the time spent" ) {
        globvars.set_global_value( "test_use_rune_charges", 0 );
        u.invoke_item( rune.get_item(), method, target );
        CHECK( globvars.get_global_value( "test_use_rune_runs" ) == 1 );
        CHECK( rune->ammo_remaining() == 20 );
        CHECK( u.get_moves() == 100 );
    }
    SECTION( "cancel spends nothing and refunds the time" ) {
        globvars.set_global_value( "test_use_rune_cancel", 1 );
        u.invoke_item( rune.get_item(), method, target );
        CHECK( globvars.get_global_value( "test_use_rune_runs" ) == 1 );
        CHECK( rune->ammo_remaining() == 20 );
        CHECK( u.get_moves() == 200 );
    }
    SECTION( "a false condition blocks the action with its failure message" ) {
        globvars.set_global_value( "test_use_rune_allowed", 0 );
        const use_function *use = rune->type->get_use( method );
        REQUIRE( use != nullptr );
        const ret_val<void> allowed = use->can_call( u, *rune, target );
        CHECK_FALSE( allowed.success() );
        CHECK( allowed.str() == "The rune is dormant." );
        u.invoke_item( rune.get_item(), method, target );
        CHECK( globvars.get_global_value( "test_use_rune_runs" ) == 0 );
        CHECK( rune->ammo_remaining() == 20 );
    }
}

// USE-J01 (review): a huge _charges went through lround and a cast to int, which wrapped to
// a negative number and then 0, so nothing was spent (or, with other values, a negative or
// overflowing qty reached item::activation_consume). It is now clamped with a debugmsg.
TEST_CASE( "use-j01_eoc_use_action_clamps_huge_charges", "[audit][iuse][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &u = get_avatar();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    globvars.set_global_value( "test_use_rune_runs", 0 );
    globvars.set_global_value( "test_use_rune_allowed", 1 );
    globvars.set_global_value( "test_use_rune_charges", 1e12 );
    globvars.set_global_value( "test_use_rune_cancel", 0 );

    item_location rune = u.i_add( item( itype_test_eoc_use_rune ) );
    REQUIRE( rune );
    rune->ammo_set( itype_match, 20 );
    REQUIRE( rune->ammo_remaining() == 20 );
    u.set_moves( 200 );
    const std::string msg = capture_debugmsg_during( [&]() {
        u.invoke_item( rune.get_item(), "effect_on_conditions", u.pos_bub() );
    } );
    CAPTURE( msg );
    CHECK( contains( msg, "_charges" ) );
    CHECK( globvars.get_global_value( "test_use_rune_runs" ) == 1 );
    // Clamped to 10000 uses: everything loaded is spent.
    CHECK( rune->ammo_remaining() == 0 );
}

// USE-J01 (review): a cancelled use returns std::nullopt and Character::invoke_item restores
// pre_obtain_moves, but NPCs pass the default -1, so their moves were set to -1 instead of
// refunded.
TEST_CASE( "use-j01_eoc_use_action_cancel_refunds_npc_moves", "[audit][iuse][eoc]" )
{
    clear_avatar();
    clear_map();
    npc &guy = spawn_npc( get_avatar().pos_bub().xy() + point( 3, 0 ), "test_talker" );
    clear_character( guy );
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    globvars.set_global_value( "test_use_rune_runs", 0 );
    globvars.set_global_value( "test_use_rune_allowed", 1 );
    globvars.set_global_value( "test_use_rune_charges", 3 );
    globvars.set_global_value( "test_use_rune_cancel", 1 );

    item_location rune = guy.i_add( item( itype_test_eoc_use_rune ) );
    REQUIRE( rune );
    rune->ammo_set( itype_match, 20 );
    REQUIRE( rune->ammo_remaining() == 20 );
    guy.set_moves( 200 );
    guy.invoke_item( rune.get_item(), "effect_on_conditions" );
    CHECK( globvars.get_global_value( "test_use_rune_runs" ) == 1 );
    CHECK( rune->ammo_remaining() == 20 );
    CHECK( guy.get_moves() == 200 );
}

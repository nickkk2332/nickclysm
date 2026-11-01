#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "character.h"
#include "creature.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "magic.h"
#include "npc.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "type_id.h"
#include "wound.h"

static const spell_id spell_audit_blood_magic_test( "audit_blood_magic_test" );

static const wound_type_id wound_bruise_bad( "bruise_bad" );

// ---- CHAR-B01 ----

// Leaves exactly `count` fresh bad bruises on the bodypart, and no other wounds.
static void reset_bad_bruises( bodypart &bp, int count )
{
    bp.get_wounds().clear();
    for( int i = 0; i < count; i++ ) {
        bp.add_wound( wound_bruise_bad );
    }
}

static int count_wounds_of_type( const bodypart &bp, const wound_type_id &type )
{
    int count = 0;
    for( const wound &wd : bp.get_wounds() ) {
        if( wd.type == type ) {
            count++;
        }
    }
    return count;
}

TEST_CASE( "char-b01_wound_progresses_at_most_once_per_hit", "[audit]" )
{
    clear_avatar();
    avatar &dummy = get_avatar();
    bodypart *arm = dummy.get_part( body_part_arm_l );
    REQUIRE( arm != nullptr );
    // bad bruises have two progression entries (70% and 50%), so both can succeed on one hit
    REQUIRE( wound_bruise_bad->wound_progression.size() >= 2 );

    // Each hit progresses with chance 1/2 * 70% * 50% = 17.5% through both entries, so 200
    // hits are practically certain to hit that case at least once without the fix.
    const int hits = 200;

    SECTION( "a single bad bruise progresses into at most one wound" ) {
        // Without the fix the second successful entry looked up the already-removed bruise,
        // got a nullptr back and dereferenced it.
        int bad_outcomes = 0;
        for( int i = 0; i < hits; i++ ) {
            reset_bad_bruises( *arm, 1 );
            arm->add_or_worsen_wound( wound_bruise_bad );
            const int total = static_cast<int>( arm->get_wounds().size() );
            const int bruises = count_wounds_of_type( *arm, wound_bruise_bad );
            // unchanged, progressed into exactly one wound, or a second bruise was added
            if( total < 1 || total > 2 || total - bruises > 1 ) {
                bad_outcomes++;
            }
        }
        CHECK( bad_outcomes == 0 );
    }

    SECTION( "one hit does not progress two bad bruises" ) {
        // Without the fix both progression entries succeeding consumed both bruises.
        int double_progressions = 0;
        for( int i = 0; i < hits; i++ ) {
            reset_bad_bruises( *arm, 2 );
            arm->add_or_worsen_wound( wound_bruise_bad );
            const int total = static_cast<int>( arm->get_wounds().size() );
            const int bruises = count_wounds_of_type( *arm, wound_bruise_bad );
            if( bruises < 1 || total - bruises > 1 ) {
                double_progressions++;
            }
        }
        CHECK( double_progressions == 0 );
    }
}

// ---- MAGIC-B01 ----

// No HP-cost spell is loaded in the test data, so define one here (once per run).
static void load_blood_magic_test_spell()
{
    if( spell_audit_blood_magic_test.is_valid() ) {
        return;
    }
    JsonObject obj = json_loader::from_string( R"({
        "id": "audit_blood_magic_test",
        "name": { "str": "Audit Blood Magic Test" },
        "description": "Costs HP to cast.",
        "valid_targets": [ "self" ],
        "effect": "attack",
        "shape": "blast",
        "damage_type": "none",
        "flags": [ "NO_HANDS", "NO_LEGS" ],
        "energy_source": "HP",
        "base_energy_cost": 35
    })" );
    spell_type::load_spell( obj, "" );
}

static void set_main_parts_hp( Character &guy, int hp )
{
    for( const bodypart_id &bp : guy.get_all_body_parts( get_body_part_flags::only_main ) ) {
        guy.set_part_hp_cur( bp, hp );
    }
}

static void check_main_parts_hp( const Character &guy, int hp )
{
    for( const bodypart_id &bp : guy.get_all_body_parts( get_body_part_flags::only_main ) ) {
        CAPTURE( bp.id().str() );
        CHECK( guy.get_part_hp_cur( bp ) == hp );
    }
}

TEST_CASE( "magic-b01_hp_cost_ignores_minor_body_parts", "[audit]" )
{
    load_blood_magic_test_spell();
    clear_avatar();
    avatar &dummy = get_avatar();
    const spell sp( spell_audit_blood_magic_test );
    REQUIRE( sp.energy_source() == magic_energy_type::hp );
    const int cost = sp.energy_cost( dummy );
    REQUIRE( cost == 35 );

    // every main part is at exactly the cost, so none of them can pay it
    set_main_parts_hp( dummy, cost );
    // ...but the minor parts (hands, eyes, ...), which blood magic cannot draw from, still are
    bool minor_part_above_cost = false;
    for( const bodypart_id &bp : dummy.get_all_body_parts() ) {
        if( dummy.get_part_hp_cur( bp ) > cost ) {
            minor_part_above_cost = true;
        }
    }
    REQUIRE( minor_part_above_cost );

    // without the fix the minor parts counted, the cast started, and paying for it
    // opened a body part menu with every entry disabled that could never be closed
    CHECK_FALSE( dummy.magic->has_enough_energy( dummy, sp ) );

    dummy.set_part_hp_cur( body_part_arm_r, cost + 1 );
    CHECK( dummy.magic->has_enough_energy( dummy, sp ) );
}

TEST_CASE( "magic-b01_blood_magic_cost_never_opens_unusable_menu", "[audit]" )
{
    load_blood_magic_test_spell();
    const spell sp( spell_audit_blood_magic_test );

    // Without the fix both sections below opened the modal body part menu
    // (which never returns when nothing is selectable, and must not run in tests at all).
    SECTION( "npc caster pays from its healthiest eligible limb without a menu" ) {
        standard_npc caster( "Blood Caster" );
        clear_character( caster, true );
        const int cost = sp.energy_cost( caster );
        REQUIRE( cost == 35 );
        set_main_parts_hp( caster, cost + 5 );
        caster.set_part_hp_cur( body_part_arm_r, cost + 15 );
        REQUIRE( caster.magic->has_enough_energy( caster, sp ) );

        sp.consume_spell_cost( caster, true );

        CHECK( caster.get_part_hp_cur( body_part_arm_r ) == 15 );
        for( const bodypart_id &bp : caster.get_all_body_parts( get_body_part_flags::only_main ) ) {
            if( bp != body_part_arm_r ) {
                CAPTURE( bp.id().str() );
                CHECK( caster.get_part_hp_cur( bp ) == cost + 5 );
            }
        }
    }

    SECTION( "npc caster with no limb able to pay draws nothing" ) {
        standard_npc caster( "Blood Caster" );
        clear_character( caster, true );
        const int cost = sp.energy_cost( caster );
        REQUIRE( cost == 35 );
        set_main_parts_hp( caster, cost );

        sp.consume_spell_cost( caster, true );

        check_main_parts_hp( caster, cost );
    }

    SECTION( "avatar with no limb able to pay (HP lost while casting) draws nothing" ) {
        clear_avatar();
        avatar &dummy = get_avatar();
        const int cost = sp.energy_cost( dummy );
        REQUIRE( cost == 35 );
        set_main_parts_hp( dummy, cost );

        sp.consume_spell_cost( dummy, true );

        check_main_parts_hp( dummy, cost );
    }
}

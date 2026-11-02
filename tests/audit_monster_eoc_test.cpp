#include <memory>
#include <optional>
#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "global_vars.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MON_CHECKS( "EOC_TEST_MON_CHECKS" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_MON_MELEE( "EOC_TEST_MON_MELEE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MON_PHASE_TWO( "EOC_TEST_MON_PHASE_TWO" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MON_REENABLE( "EOC_TEST_MON_REENABLE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MON_USE_SPECIAL( "EOC_TEST_MON_USE_SPECIAL" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MON_USE_UNOWNED( "EOC_TEST_MON_USE_UNOWNED" );

static const mfaction_str_id monfaction_bee( "bee" );

static const std::string special_name = "test_eoc_mark_target";

// The monster is alpha, the creature it acts on beta
static void run_monster_eoc( const effect_on_condition_id &eoc, monster &mon, Creature &other,
                             const std::string &other_mtype = "" )
{
    dialogue d( get_talker_for( mon ), get_talker_for( other ) );
    d.set_value( "other_mtype", other_mtype );
    eoc->activate( d );
}

// Whether monster AI's search for reachable creatures of this faction finds mon. The search
// starts from mon itself: a newly spawned monster always gets its zone filled in.
static bool reachable_in_faction( const monster &mon, const mfaction_id &faction )
{
    return get_creature_tracker().find_reachable( mon, [&faction]( const mfaction_id & f ) {
        return f == faction;
    }, [&mon]( const Creature * other ) {
        return other == &mon;
    } ) != nullptr;
}

static double monster_global( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    REQUIRE( value != nullptr );
    return value->dbl();
}

// MONAI-J01: u_attack did nothing for a monster (talker_monster had no attack_target), and EOCs
// couldn't tell a monster's type or control its special attacks or faction.
TEST_CASE( "monai-j01_eocs_can_drive_monster_attacks", "[audit][monster][eoc]" )
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    get_globals().clear_global_values();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_pos( 65, 65, 0 );
    u.setpos( here, mon_pos + tripoint_rel_ms::east );
    monster &mon = spawn_test_monster( "mon_test_eoc_attacker", mon_pos );
    mon.set_moves( 100 );

    SECTION( "u_attack naming a special attack uses it on beta, whatever its cooldown" ) {
        mon.set_special( special_name, 7 );
        run_monster_eoc( effect_on_condition_EOC_TEST_MON_USE_SPECIAL, mon, u );
        CHECK( monster_global( "test_mon_special_hit_avatar" ) == 1 );
        // Used, so its cooldown starts again
        CHECK( mon.special_cooldown( special_name ) == std::optional<int>( 10 ) );
    }

    SECTION( "u_attack can use a JSON monster attack the monster doesn't have" ) {
        REQUIRE( !mon.special_cooldown( "test_eoc_unowned_attack" ) );
        run_monster_eoc( effect_on_condition_EOC_TEST_MON_USE_UNOWNED, mon, u );
        CHECK( monster_global( "test_mon_unowned_hit_avatar" ) == 1 );
        // Not a basic melee attack instead, and no cooldown is kept for an attack it lacks
        CHECK( mon.get_moves() == 100 );
        CHECK( !mon.special_cooldown( "test_eoc_unowned_attack" ) );
    }

    SECTION( "u_attack naming anything else is a basic melee attack" ) {
        run_monster_eoc( effect_on_condition_EOC_TEST_MON_MELEE, mon, u );
        CHECK( mon.get_moves() == 70 );
        CHECK( get_globals().maybe_get_global_value( "test_mon_special_hit_avatar" ) == nullptr );
    }

    SECTION( "conditions, the cooldown math and special and faction effects" ) {
        mon.set_special( special_name, 4 );
        run_monster_eoc( effect_on_condition_EOC_TEST_MON_CHECKS, mon, u, "mon_test_zombie" );
        CHECK( monster_global( "test_mon_is_mtype" ) == 1 );
        // copy-from parents don't count
        CHECK( monster_global( "test_mon_is_other_mtype" ) == 0 );
        CHECK( monster_global( "test_mon_has_special" ) == 1 );
        CHECK( monster_global( "test_mon_cooldown" ) == 4 );
        CHECK( monster_global( "test_mon_missing_cooldown" ) == -1 );

        // Fill monster AI's cache of reachable creatures by faction
        get_creature_tracker().invalidate_reachability_cache();
        REQUIRE( reachable_in_faction( mon, mon.faction ) );

        run_monster_eoc( effect_on_condition_EOC_TEST_MON_PHASE_TWO, mon, u );
        CHECK( !mon.has_special( special_name ) );
        CHECK( mon.special_cooldown( special_name ) == std::optional<int>( 3 ) );
        CHECK( mon.faction.id() == monfaction_bee );
        // Other monsters see the new faction at once
        CHECK( reachable_in_faction( mon, monfaction_bee.id() ) );

        run_monster_eoc( effect_on_condition_EOC_TEST_MON_CHECKS, mon, u, "mon_test_eoc_attacker" );
        CHECK( monster_global( "test_mon_has_special" ) == 0 );
        CHECK( monster_global( "test_mon_is_other_mtype" ) == 1 );

        run_monster_eoc( effect_on_condition_EOC_TEST_MON_REENABLE, mon, u );
        CHECK( mon.has_special( special_name ) );
    }

    SECTION( "the conditions are false for a talker that isn't a monster" ) {
        dialogue d( get_talker_for( u ), get_talker_for( mon ) );
        d.set_value( "other_mtype", std::string( "mon_test_eoc_attacker" ) );
        effect_on_condition_EOC_TEST_MON_CHECKS->activate( d );
        CHECK( monster_global( "test_mon_is_mtype" ) == 0 );
        CHECK( monster_global( "test_mon_has_special" ) == 0 );
        CHECK( monster_global( "test_mon_cooldown" ) == -1 );
    }
}

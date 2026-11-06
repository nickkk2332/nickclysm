#include <optional>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "flag.h"
#include "item.h"
#include "item_location.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "martialarts.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"

static const bionic_id bio_claws( "bio_claws" );
static const bionic_id bio_power_storage( "bio_power_storage" );

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );

static const efftype_id effect_disarmed( "disarmed" );

static const json_character_flag json_flag_DISTRIBUTED_DAMAGE( "DISTRIBUTED_DAMAGE" );

static const matec_id DEF_DISARM( "DEF_DISARM" );

static const trait_id trait_SLIMESPAWNER( "SLIMESPAWNER" );
static const trait_id trait_TEST_MELEE_DAMAGE( "TEST_MELEE_DAMAGE" );
static const trait_id trait_TEST_POWER_TRICKLE( "TEST_POWER_TRICKLE" );
static const trait_id trait_WINGS_INSECT( "WINGS_INSECT" );
static const trait_id trait_WINGS_INSECT_active( "WINGS_INSECT_active" );

static int total_part_hp( const Character &who, const std::vector<bodypart_id> &parts )
{
    int total = 0;
    for( const bodypart_id &bp : parts ) {
        total += who.get_part_hp_cur( bp );
    }
    return total;
}

// HLTH-B01: DISTRIBUTED_DAMAGE divided the hit by every body entry (12 for a human or
// slime body) with integer division, so hits under 12 did nothing, and handed shares to
// minor parts whose HP never counts. The whole hit must now land on the main parts.
TEST_CASE( "hlth-b01_distributed_damage_spreads_whole_hit_over_main_parts",
           "[audit][health]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    dummy.set_mutation( trait_SLIMESPAWNER );
    REQUIRE( dummy.has_flag( json_flag_DISTRIBUTED_DAMAGE ) );

    const std::vector<bodypart_id> main_parts =
        dummy.get_all_body_parts( get_body_part_flags::only_main );
    const std::vector<bodypart_id> minor_parts =
        dummy.get_all_body_parts( get_body_part_flags::only_minor );
    REQUIRE( !main_parts.empty() );
    REQUIRE( !minor_parts.empty() );
    const int num_parts = static_cast<int>( dummy.get_all_body_parts().size() );

    // A hit smaller than the number of body parts, and one that is not a multiple of it.
    for( const int dam : {
             num_parts - 1, 2 * num_parts + 5
         } ) {
        CAPTURE( dam );
        const int main_before = total_part_hp( dummy, main_parts );
        const int minor_before = total_part_hp( dummy, minor_parts );
        dummy.apply_damage( nullptr, main_parts.front(), dam );
        CHECK( total_part_hp( dummy, main_parts ) == main_before - dam );
        CHECK( total_part_hp( dummy, minor_parts ) == minor_before );
    }
    clear_avatar();
}

// MUT-B02: activate_mutation() copied powered = true into my_mutations, but
// deactivate_mutation() only cleared cached_mutations, and the enchantment recalc read
// my_mutations. An ACTIVE enchantment's granted trait (Insect Wings -> buzzing) then
// stayed forever, and it was granted one recalc late on activation.
TEST_CASE( "mut-b02_deactivating_mutation_removes_active_enchantment_traits",
           "[audit][mutation]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    dummy.set_mutation( trait_WINGS_INSECT );
    REQUIRE( dummy.has_trait( trait_WINGS_INSECT ) );
    REQUIRE( !dummy.has_trait( trait_WINGS_INSECT_active ) );
    REQUIRE( dummy.can_power_mutation( trait_WINGS_INSECT ) );

    dummy.activate_mutation( trait_WINGS_INSECT );
    REQUIRE( dummy.has_active_mutation( trait_WINGS_INSECT ) );
    // Granted by the activation itself, not a turn later.
    CHECK( dummy.has_trait( trait_WINGS_INSECT_active ) );
    dummy.recalculate_enchantment_cache();
    CHECK( dummy.has_trait( trait_WINGS_INSECT_active ) );

    dummy.deactivate_mutation( trait_WINGS_INSECT );
    REQUIRE( !dummy.has_active_mutation( trait_WINGS_INSECT ) );
    CHECK( !dummy.has_trait( trait_WINGS_INSECT_active ) );
    // The per-turn recalc must not bring it back.
    dummy.recalculate_enchantment_cache();
    CHECK( !dummy.has_trait( trait_WINGS_INSECT_active ) );
    clear_avatar();
}

// MUT-B03: remove_bionic() never retracted an extended weapon bionic, so its NO_UNWIELD
// fake weapon stayed wielded with weapon_bionic_uid pointing at a removed bionic and
// could never be put away. uninstall_bionic() also retracted the installer's weapon
// bionics instead of the patient's.
TEST_CASE( "mut-b03_removing_extended_weapon_bionic_retracts_weapon", "[audit][bionics]" )
{
    clear_avatar();
    avatar &dummy = get_avatar();
    dummy.add_bionic( bio_power_storage );
    dummy.set_power_level( dummy.get_max_power_level() );
    dummy.add_bionic( bio_claws );
    const std::optional<bionic *> claws = dummy.find_bionic_by_type( bio_claws );
    REQUIRE( claws );
    REQUIRE( dummy.activate_bionic( **claws ) );
    REQUIRE( dummy.is_using_bionic_weapon() );
    REQUIRE( dummy.get_wielded_item() );
    REQUIRE( dummy.get_wielded_item()->has_flag( flag_NO_UNWIELD ) );

    SECTION( "the bionic is removed" ) {
        dummy.remove_bionic( **claws );
        REQUIRE_FALSE( dummy.has_bionic( bio_claws ) );
    }
    SECTION( "someone else starts removing a bionic" ) {
        standard_npc surgeon( "Surgeon" );
        dummy.uninstall_bionic( **claws, surgeon );
    }
    CHECK_FALSE( dummy.is_using_bionic_weapon() );
    CHECK( !dummy.get_wielded_item() );
    clear_avatar();
}

// MUT-B04: POWER_TRICKLE was only read in burn_fuel()'s branch for unpowered bionics, so
// it was applied once per passive CBM each turn, and never without one.
TEST_CASE( "mut-b04_power_trickle_applies_once_per_turn", "[audit][bionics]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    // TEST_POWER_TRICKLE adds 1000 mJ per second.
    dummy.set_mutation( trait_TEST_POWER_TRICKLE );
    const units::energy trickle = 1_J;

    SECTION( "with several passive bionics" ) {
        dummy.add_bionic( bio_power_storage );
        dummy.add_bionic( bio_power_storage );
        dummy.add_bionic( bio_power_storage );
    }
    SECTION( "with no bionics" ) {
        dummy.set_max_power_level_modifier( 100_kJ );
    }
    REQUIRE( dummy.get_max_power_level() >= 100_kJ );
    dummy.set_power_level( 0_kJ );

    dummy.suffer();
    CHECK( dummy.get_power_level() == trickle );
    dummy.suffer();
    CHECK( dummy.get_power_level() == trickle * 2 );
    clear_avatar();
}

// CMBT-B02: perform_technique() applied a tech_effect only when it had a condition that
// passed, so the 33 tech_effects without a "condition" (every disarm against monsters,
// most "downed" and "bleed" effects) were never applied.
TEST_CASE( "cmbt-b02_tech_effect_without_condition_is_applied", "[audit][melee]" )
{
    clear_avatar();
    clear_map();
    Character &dummy = get_player_character();
    const ma_technique &tech = DEF_DISARM.obj();
    REQUIRE( tech.tech_effects.size() == 1 );
    const tech_effect_data &eff = tech.tech_effects.front();
    REQUIRE( eff.id == effect_disarmed );
    REQUIRE_FALSE( eff.has_condition );
    REQUIRE( eff.chance == 100 );

    monster &zed = spawn_test_monster( "mon_zombie", dummy.pos_bub() + tripoint::east );
    REQUIRE_FALSE( zed.has_effect( effect_disarmed ) );
    // on_damage: the attack must carry damage for the effect to apply.
    damage_instance di( damage_bash, 10.0f );
    int move_cost = 100;
    item_location weapon = dummy.get_wielded_item();
    dummy.perform_technique( tech, zed, di, move_cost, weapon );
    CHECK( zed.has_effect( effect_disarmed ) );
    clear_map();
}

// CMBT-B03: the generic MELEE_DAMAGE value was only read for damage types missing from
// the attack, and only when that type also had its own bonus, so it never changed melee
// attacks against creatures.
TEST_CASE( "cmbt-b03_melee_damage_enchantment_modifies_melee_attacks", "[audit][melee]" )
{
    clear_avatar();
    Character &dummy = get_player_character();
    // TEST_MELEE_DAMAGE: MELEE_DAMAGE add 10, multiply 1.
    dummy.set_mutation( trait_TEST_MELEE_DAMAGE );

    SECTION( "single damage type" ) {
        const damage_instance modified =
            dummy.modify_damage_dealt_with_enchantments( damage_instance( damage_cut, 20.0f ) );
        // ( 20 + 10 ) * 2
        CHECK( modified.type_damage( damage_cut ) == Approx( 60.0f ) );
        CHECK( modified.total_damage() == Approx( 60.0f ) );
    }
    SECTION( "the flat bonus goes to the biggest damage type once" ) {
        damage_instance di( damage_cut, 20.0f );
        di.add_damage( damage_bash, 5.0f );
        const damage_instance modified = dummy.modify_damage_dealt_with_enchantments( di );
        CHECK( modified.type_damage( damage_cut ) == Approx( 60.0f ) );
        CHECK( modified.type_damage( damage_bash ) == Approx( 10.0f ) );
        CHECK( modified.total_damage() == Approx( 70.0f ) );
    }
    clear_avatar();
}

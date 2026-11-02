#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "cata_catch.h"
#include "flag.h"
#include "item.h"
#include "item_group.h"
#include "item_pocket.h"
#include "itype.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "relic.h"
#include "type_id.h"

static const fault_id fault_test_fault_mech_self_damaging( "test_fault_mech_self_damaging" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_briefcase_smg( "briefcase_smg" );
static const itype_id itype_colt_army( "colt_army" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_test_fault_self_damaging_rod( "test_fault_self_damaging_rod" );
static const itype_id itype_test_fault_two_mech_rod( "test_fault_two_mech_rod" );

static const relic_procgen_id relic_procgen_data_test_relic_hit_me( "test_relic_hit_me" );
static const relic_procgen_id
relic_procgen_data_test_relic_negative_cap( "test_relic_negative_cap" );
static const relic_procgen_id
relic_procgen_data_test_relic_negative_only( "test_relic_negative_only" );

static const spell_id spell_test_spell_pew( "test_spell_pew" );

// ITEM-B03: Item_factory::finalize_post() returned right after giving complex firearms their
// gun repair tools, so everything after it never ran for guns: the per-bodypart armor data
// of an ARMOR+GUN item was never built, and NO_UNLOAD/NO_RELOAD never reached the pockets.
TEST_CASE( "item-b03_complex_guns_finish_item_finalization", "[audit][item]" )
{
    // The MP5 briefcase is worn on the arms; without the armor data it covers nothing.
    const item briefcase( itype_briefcase_smg );
    REQUIRE( briefcase.is_gun() );
    REQUIRE( briefcase.is_armor() );
    CHECK( briefcase.get_coverage( body_part_arm_l.id() ) > 0 );
    CHECK( !briefcase.type->armor->data.empty() );

    // The cap and ball revolver can't be unloaded; its cylinder must know that.
    REQUIRE( itype_colt_army->gun );
    REQUIRE( itype_colt_army->has_flag( flag_NO_UNLOAD ) );
    REQUIRE( !itype_colt_army->pockets.empty() );
    for( const pocket_data &pocket : itype_colt_army->pockets ) {
        CHECK( pocket._no_unload );
    }
}

// ITEM-B04: mod_damage() rolled a mechanical fault for every i in 0..qty step damage_scale,
// inclusive, so one damage level (qty 1000) rolled twice.  An item already having a fault
// could also roll it again, and set_fault() applies the fault's instant damage through
// mod_damage(), so a fault with instant damage re-rolled itself until the stack overflowed.
TEST_CASE( "item-b04_one_damage_level_rolls_one_mechanical_fault", "[audit][item]" )
{
    // Both test faults are always eligible, so the second roll picked a new fault half the time.
    int items_with_two_faults = 0;
    for( int i = 0; i < 64; ++i ) {
        item rod( itype_test_fault_two_mech_rod );
        REQUIRE( rod.faults.empty() );
        rod.inc_damage();
        REQUIRE( rod.damage() == itype::damage_scale );
        CHECK( rod.faults.size() == 1 );
        if( rod.faults.size() > 1 ) {
            ++items_with_two_faults;
        }
    }
    CHECK( items_with_two_faults == 0 );
}

TEST_CASE( "item-b04_instant_damage_fault_does_not_reroll_itself", "[audit][item]" )
{
    // The rod's only mechanical fault has 1 instant damage: getting it damages the rod, which
    // rolled the same fault again, forever.  It must be set once.
    item rod( itype_test_fault_self_damaging_rod );
    REQUIRE( rod.faults.empty() );
    rod.inc_damage();
    CHECK( rod.has_fault( fault_test_fault_mech_self_damaging ) );
    CHECK( rod.faults.size() == 1 );
    CHECK( rod.damage() == itype::damage_scale + 1 );

    // A fault the item already has can't be set again without force.
    CHECK_FALSE( rod.can_have_fault( fault_test_fault_mech_self_damaging ) );
    CHECK_FALSE( rod.set_fault( fault_test_fault_mech_self_damaging ) );
    CHECK( rod.damage() == itype::damage_scale + 1 );
}

// ITEM-B06: Single_item_creator::every_item_min_max() read modifier->charges and
// modifier->count for count-by-charges items before checking that the optional modifier
// exists.  An entry written as a plain id, [id, prob] or {"item": x} has no modifier, so the
// min/max came from uninitialized storage (bash drops and the terrain bash description).
TEST_CASE( "item-b06_min_max_of_charges_item_without_modifier", "[audit][item]" )
{
    const int charges = itype_9mm->charges_default();
    REQUIRE( itype_9mm->count_by_charges() );
    REQUIRE( charges > 1 );

    SECTION( "certain entry spawns one default stack" ) {
        const Single_item_creator entry( itype_9mm.str(), Single_item_creator::S_ITEM, 100,
                                         "item-b06 test" );
        REQUIRE( !entry.modifier );
        const std::map<const itype *, std::pair<int, int>> min_max = entry.every_item_min_max();
        REQUIRE( min_max.size() == 1 );
        CHECK( min_max.begin()->first == &itype_9mm.obj() );
        CHECK( min_max.begin()->second == std::make_pair( charges, charges ) );
    }

    SECTION( "uncertain entry may spawn nothing" ) {
        const Single_item_creator entry( itype_9mm.str(), Single_item_creator::S_ITEM, 1,
                                         "item-b06 test" );
        REQUIRE( !entry.modifier );
        const std::map<const itype *, std::pair<int, int>> min_max = entry.every_item_min_max();
        REQUIRE( min_max.size() == 1 );
        CHECK( min_max.begin()->second == std::make_pair( 0, charges ) );
    }
}

// ITEM-B07: relic generation checked max_negative_power before adding the new attribute's
// power, so the total could overshoot the cap by one attribute and a cap of 0 still allowed
// one negative attribute.  A rejected pick changed nothing, so a ruleset whose candidates were
// all rejected looped forever.  passive_hit_me/hit_you values were never loaded either.
static int relic_negative_power( const relic &art, const relic_procgen_id &ruleset )
{
    int negative = 0;
    for( const enchant_cache &ench : art.get_proc_enchantments() ) {
        negative += std::min( 0, ruleset->power_level( ench ) );
    }
    return negative;
}

TEST_CASE( "item-b07_relic_negative_power_stays_within_cap", "[audit][item][relic]" )
{
    relic_procgen_data::generation_rules rules;
    rules.power_level = 500;
    rules.max_negative_power = -300;
    // each negative attribute is -250, so only one fits under -300
    for( int i = 0; i < 50; ++i ) {
        const relic art = relic_procgen_data_test_relic_negative_cap->generate( rules, itype_rock );
        CHECK( relic_negative_power( art, relic_procgen_data_test_relic_negative_cap ) >= -300 );
        CHECK( art.power_level( relic_procgen_data_test_relic_negative_cap ) >= 500 );
    }
}

TEST_CASE( "item-b07_relic_with_only_rejected_candidates_ends_empty", "[audit][item][relic]" )
{
    relic_procgen_data::generation_rules rules;
    rules.power_level = 500;
    rules.max_negative_power = 0;
    rules.max_attributes = 1;
    // the only candidate is negative and the cap is 0: nothing may be added, and generation
    // must give up instead of retrying forever
    const relic art = relic_procgen_data_test_relic_negative_only->generate( rules, itype_rock );
    CHECK( art.get_proc_enchantments().empty() );
    CHECK( relic_negative_power( art, relic_procgen_data_test_relic_negative_only ) == 0 );
}

TEST_CASE( "item-b07_relic_hit_me_values_are_loaded", "[audit][item][relic]" )
{
    relic_procgen_data::generation_rules rules;
    rules.power_level = 500;
    const relic art = relic_procgen_data_test_relic_hit_me->generate( rules, itype_rock );
    bool has_hit_me = false;
    for( const enchant_cache &ench : art.get_proc_enchantments() ) {
        for( const fake_spell &sp : ench.hit_me_effect ) {
            CHECK( sp.id == spell_test_spell_pew );
            has_hit_me = true;
        }
    }
    CHECK( has_hit_me );
    // each hit_me attribute is worth its 300 base power, so two attributes reach 500
    CHECK( art.get_proc_enchantments().size() <= 2 );
    CHECK( art.power_level( relic_procgen_data_test_relic_hit_me ) >= 500 );
}

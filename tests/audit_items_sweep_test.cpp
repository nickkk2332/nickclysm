#include <algorithm>
#include <climits>
#include <list>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "flag.h"
#include "global_vars.h"
#include "item.h"
#include "item_category.h"
#include "item_group.h"
#include "item_pocket.h"
#include "item_transformation.h"
#include "itype.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "relic.h"
#include "ret_val.h"
#include "type_id.h"

static const fault_id fault_test_fault_mech_self_damaging( "test_fault_mech_self_damaging" );

static const item_category_id
item_category_test_spawn_rate_category( "test_spawn_rate_category" );
static const item_category_id
item_category_test_spawn_rate_category_copy( "test_spawn_rate_category_copy" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_briefcase_smg( "briefcase_smg" );
static const itype_id itype_colt_army( "colt_army" );
static const itype_id itype_heavy_battery_cell( "heavy_battery_cell" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_test_fault_self_damaging_rod( "test_fault_self_damaging_rod" );
static const itype_id itype_test_fault_two_mech_rod( "test_fault_two_mech_rod" );
static const itype_id itype_test_glock( "test_glock" );
static const itype_id itype_test_multimag_gun_integral_ammo( "test_multimag_gun_integral_ammo" );
static const itype_id itype_test_quiver( "test_quiver" );
static const itype_id itype_test_transform_timed_baton( "test_transform_timed_baton" );

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

// total STRENGTH of an artifact's procgen enchantments
static double relic_strength( const relic &art )
{
    double strength = 0.0;
    for( const enchant_cache &ench : art.get_proc_enchantments() ) {
        strength += ench.get_value_add( enchant_vals::mod::STRENGTH );
    }
    return strength;
}

TEST_CASE( "item-b07_relic_negative_power_stays_within_cap", "[audit][item][relic]" )
{
    relic_procgen_data::generation_rules rules;
    rules.power_level = 500;
    rules.max_negative_power = -300;
    // each negative attribute is STRENGTH -1 worth -250, so only one fits under -300.  The old
    // check let a second one in (about 5 artifacts in 8).  All attributes share the HELD
    // condition and merge into one enchantment, whose power hides the negatives, so count the
    // STRENGTH they add instead.
    for( int i = 0; i < 50; ++i ) {
        const relic art =
            relic_procgen_data_test_relic_negative_cap->generate( rules, itype_rock );
        // at most one STRENGTH -1 (two would be -2)
        CHECK( relic_strength( art ) > -1.5 );
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
    SECTION( "a hit_me spell counts toward the enchantment's power" ) {
        enchant_cache ench;
        ench.add_hit_me( fake_spell( spell_test_spell_pew ) );
        // base_power 300 at spell level 0
        CHECK( relic_procgen_data_test_relic_hit_me->power_level( ench ) == 300 );
    }

    SECTION( "generated artifacts get hit_me attributes" ) {
        relic_procgen_data::generation_rules rules;
        rules.power_level = 500;
        int artifacts_with_hit_me = 0;
        for( int i = 0; i < 20; ++i ) {
            const relic art = relic_procgen_data_test_relic_hit_me->generate( rules, itype_rock );
            int hit_me_count = 0;
            for( const enchant_cache &ench : art.get_proc_enchantments() ) {
                for( const fake_spell &sp : ench.hit_me_effect ) {
                    CHECK( sp.id == spell_test_spell_pew );
                    ++hit_me_count;
                }
            }
            if( hit_me_count > 0 ) {
                ++artifacts_with_hit_me;
            }
            // each hit_me attribute is worth its 300 base power, so two of them reach 500
            CHECK( hit_me_count <= 2 );
            CHECK( art.power_level( relic_procgen_data_test_relic_hit_me ) >= 500 );
        }
        // an artifact only misses hit_me when both its picks are the weight-1 add attribute
        CHECK( artifacts_with_hit_me > 0 );
    }
}

// ITEM-B08: the countdown of a countdown_interval item is only checked while the item is
// active, but the item constructor never activated it (only SPAWN_ACTIVE did), and
// item_transformation::transform() overwrote the active state convert() had set.  Such
// items never counted down.  The constructor also started the countdown from the current
// turn instead of the item's birth.
TEST_CASE( "item-b08_countdown_items_are_active", "[audit][item]" )
{
    REQUIRE( itype_test_transform_timed_baton->countdown_interval == 1_seconds );
    REQUIRE( !itype_test_transform_timed_baton->has_flag( flag_SPAWN_ACTIVE ) );

    SECTION( "spawned countdown item" ) {
        const time_point birth = calendar::turn - 1_hours;
        const item baton( itype_test_transform_timed_baton, birth );
        CHECK( baton.active );
        CHECK( baton.countdown_point == birth + 1_seconds );
    }

    SECTION( "item transformed into a countdown item" ) {
        item rock( itype_rock );
        REQUIRE( !rock.active );
        item_transformation to_baton;
        to_baton.target = itype_test_transform_timed_baton;
        to_baton.transform( nullptr, rock );
        REQUIRE( rock.typeId() == itype_test_transform_timed_baton );
        CHECK( rock.active );
        CHECK( rock.countdown_point == calendar::turn + 1_seconds );
    }
}

// ITEM-B09: item category spawn rates lived only in a static map.  Rates set by the
// set_item_category_spawn_rates EOC were never saved, so they reverted on load (and leaked
// into the next game), and a copy-from override that didn't repeat spawn_rate reset the
// rate an earlier definition had set to 1.0.
TEST_CASE( "item-b09_item_category_spawn_rate_survives_override_and_save", "[audit][item]" )
{
    // A copy-from (including a same-id override in a later mod, which loads the same way)
    // keeps the source's rate.
    const item_category &cat = item_category_test_spawn_rate_category.obj();
    CHECK( cat.get_spawn_rate() == Approx( 0.25f ) );
    CHECK( item_category_test_spawn_rate_category_copy->get_spawn_rate() == Approx( 0.25f ) );

    global_variables &globals = get_globals();
    const global_variables::impl_t old_globals = globals.get_global_values();

    // the EOC sets the rate in game
    cat.set_spawn_rate( 2.0f );
    CHECK( cat.get_spawn_rate() == Approx( 2.0f ) );

    // it's saved with the game...
    std::ostringstream os;
    JsonOut jsout( os );
    jsout.start_object();
    globals.serialize( jsout );
    jsout.end_object();

    // ...a new game starts from the JSON rate...
    globals.clear_global_values();
    CHECK( cat.get_spawn_rate() == Approx( 0.25f ) );

    // ...and loading the save brings the EOC rate back
    globals.unserialize( json_loader::from_string( os.str() ).get_object() );
    CHECK( cat.get_spawn_rate() == Approx( 2.0f ) );

    globals.set_global_values( old_globals );
    CHECK( cat.get_spawn_rate() == Approx( 0.25f ) );
}

// IINF-B03: gun_recoil() divided recoil by the effective handling above 1, and multiplied it
// by 1 + handling at or below 1.  Recoil therefore doubled when handling reached exactly 1,
// and below 1 it shrank as handling got worse: arm strength 0 gave 1x recoil, 1 gave 2x.
// Handling summed below 0 by mods made pow() return NaN.
TEST_CASE( "iinf-b03_gun_recoil_never_rises_with_arm_strength", "[audit][item][gun]" )
{
    clear_avatar();
    avatar &guy = get_avatar();
    item gun( itype_test_glock );
    gun.ammo_set( gun.ammo_default() );
    REQUIRE( gun.ammo_remaining() > 0 );

    // The 600 g test glock has handling 10: arm strength 0 gives an effective handling of 0,
    // 1 gives exactly 1 and 2 or more gives 1.6.
    int previous = INT_MAX;
    for( int str = 0; str <= 10; ++str ) {
        guy.set_str_base( str );
        const int recoil = gun.gun_recoil( guy );
        CAPTURE( str, guy.get_arm_str(), recoil, previous );
        CHECK( recoil > 0 );
        CHECK( recoil <= previous );
        previous = recoil;
    }
    // the curve is continuous: handling 0 doubles the recoil, handling 1 leaves it as is
    guy.set_str_base( 0 );
    const int recoil_no_handling = gun.gun_recoil( guy );
    guy.set_str_base( 1 );
    const int recoil_handling_one = gun.gun_recoil( guy );
    CHECK( recoil_no_handling == 2 * recoil_handling_one );
}

// IINF-B04: item::on_wear() picked the side of a sided item by summing the character's
// cached encumbrance with the item on the left and then on the right.  The cache knew
// nothing of the new item or its side (it's recalculated after on_wear), so both sums were
// equal and every sided item went on the left: two quivers both ended up on the left leg.
TEST_CASE( "iinf-b04_second_sided_item_goes_on_the_free_side", "[audit][item][armor]" )
{
    clear_avatar();
    avatar &guy = get_avatar();

    std::optional<std::list<item>::iterator> first = guy.wear_item( item( itype_test_quiver ),
            false );
    REQUIRE( first.has_value() );
    const side first_side = ( *first )->get_side();
    REQUIRE( first_side != side::BOTH );

    std::optional<std::list<item>::iterator> second = guy.wear_item( item( itype_test_quiver ),
            false );
    REQUIRE( second.has_value() );
    CHECK( ( *second )->get_side() != side::BOTH );
    CHECK( ( *second )->get_side() != first_side );
}

// IINF-B05: for a MAGAZINE pocket, is_reloadable() asked the item-wide
// remaining_ammo_capacity(), which reports the first loaded magazine well's magazine when
// there is one.  A gun with an integral projectile magazine and a battery well therefore
// couldn't be reloaded while its battery was full, however empty the magazine was.
TEST_CASE( "iinf-b05_integral_magazine_reloadable_next_to_full_battery", "[audit][item][gun]" )
{
    item gun( itype_test_multimag_gun_integral_ammo );
    item battery( itype_heavy_battery_cell );
    battery.ammo_set( itype_battery );
    REQUIRE( battery.is_magazine_full() );
    REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );

    // the integral 9mm magazine is empty
    CHECK( gun.is_reloadable() );

    // with the magazine loaded too there's nothing left to reload
    REQUIRE( gun.put_in( item( itype_9mm, calendar::turn, 1 ),
                         pocket_type::MAGAZINE ).success() );
    CHECK_FALSE( gun.is_reloadable() );
}

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "magic.h"
#include "player_helpers.h"
#include "type_id.h"

static const spell_id spell_test_spell_enchant_other( "test_spell_enchant_other" );
static const spell_id spell_test_spell_enchant_power( "test_spell_enchant_power" );

static const trait_id trait_TEST_SPELL_POWER( "TEST_SPELL_POWER" );

struct expected_spell_values {
    int damage;
    int range;
    int aoe;
    int duration;
    int cost;
    int cast_time;
};

static void check_spell_values( const spell &sp, const Character &caster,
                                const expected_spell_values &expected )
{
    CAPTURE( sp.id().str() );
    CHECK( sp.damage( caster ) == expected.damage );
    CHECK( sp.range( caster ) == expected.range );
    CHECK( sp.aoe( caster ) == expected.aoe );
    CHECK( sp.duration( caster ) == expected.duration );
    CHECK( sp.energy_cost( caster ) == expected.cost );
    CHECK( sp.casting_time( caster ) == expected.cast_time );
}

static void give_spell_power( Character &you )
{
    you.set_mutation( trait_TEST_SPELL_POWER );
    you.recalculate_enchantment_cache();
    REQUIRE( you.has_trait( trait_TEST_SPELL_POWER ) );
}

// No enchantment value touched spell damage, range, area, duration, cost or casting time; mods
// rebuilt spell power from spellcasting_adjustment EOCs, which fake spells never saw
TEST_CASE( "magic-j02_spell_values_change_the_spells_their_filters_match",
           "[audit][magic][enchantments]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const spell probe( spell_test_spell_enchant_power );
    const spell other( spell_test_spell_enchant_other );

    // Without the enchantment both spells have their JSON values
    check_spell_values( probe, you, { 10, 4, 2, 100, 50, 100 } );
    check_spell_values( other, you, { 10, 4, 2, 100, 50, 100 } );

    give_spell_power( you );

    // The cold, SOMATIC test_magic_available probe: damage ( 10 + 5 ) * 2, range by spell id,
    // area by magic type, and the unfiltered duration, cost and casting time entries
    check_spell_values( probe, you, { 30, 7, 3, 50, 25, 50 } );
    // The heat spell matches only the spell_class area entry and the unfiltered entries
    check_spell_values( other, you, { 10, 4, 102, 50, 25, 50 } );
}

TEST_CASE( "magic-j02_spell_values_reach_fake_spells", "[audit][magic][enchantments]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    give_spell_power( you );

    // Item, enchantment and EOC casts build a fresh spell from a fake_spell
    const spell sp = fake_spell( spell_test_spell_enchant_power ).get_spell( you );
    CHECK( sp.damage( you ) == 30 );
    CHECK( sp.range( you ) == 7 );
}

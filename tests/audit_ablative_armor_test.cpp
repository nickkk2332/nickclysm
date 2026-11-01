#include <initializer_list>
#include <list>
#include <optional>

#include "bodypart.h"
#include "cata_catch.h"
#include "character.h"
#include "damage.h"
#include "item.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "subbodypart.h"
#include "type_id.h"

static const damage_type_id damage_bullet( "bullet" );

static const itype_id itype_ballistic_vest_esapi( "ballistic_vest_esapi" );
// Same as esapi_plate: inferred ceramic material (cover 100), 45% coverage of torso_upper
static const itype_id itype_test_plate( "test_plate" );

static const sub_bodypart_str_id sub_body_part_torso_upper( "torso_upper" );

static constexpr float bullet_damage = 200.0f;

// An inserted plate is applied once, by Character::ablative_armor_absorb. The carrier's own
// mitigation must not add the plate's materials a second time.
TEST_CASE( "iinf-b01_carrier_mitigation_excludes_ablative_plate", "[audit]" )
{
    const sub_bodypart_id torso_upper( sub_body_part_torso_upper );
    item plated( itype_ballistic_vest_esapi );
    REQUIRE( plated.put_in( item( itype_test_plate ), pocket_type::CONTAINER ).success() );
    const item empty( itype_ballistic_vest_esapi );

    // The plate covers torso_upper and the combined (display) material list includes it
    REQUIRE( empty.armor_made_of( torso_upper ).size() <
             plated.armor_made_of( torso_upper ).size() );

    // Fixed rolls, so the same carrier materials apply to both vests
    for( const int roll : {
             0, 50, 99
         } ) {
        CAPTURE( roll );
        damage_unit du_plated( damage_bullet, bullet_damage );
        damage_unit du_empty( damage_bullet, bullet_damage );
        plated.mitigate_damage( du_plated, torso_upper, roll );
        empty.mitigate_damage( du_empty, torso_upper, roll );
        // The carrier still protects on its own
        REQUIRE( du_empty.amount < bullet_damage );
        CHECK( du_plated.amount == Approx( du_empty.amount ) );
    }
}

// Wears a fresh plated vest and runs one hit with the given coverage roll through it the way
// outfit::absorb_damage does. Returns the damage left.
static float hit_worn_plated_vest( Character &guy, const int roll )
{
    const sub_bodypart_id torso_upper( sub_body_part_torso_upper );
    guy.clear_worn();
    item plated( itype_ballistic_vest_esapi );
    REQUIRE( plated.put_in( item( itype_test_plate ), pocket_type::CONTAINER ).success() );
    std::optional<std::list<item>::iterator> worn = guy.wear_item( plated, false );
    REQUIRE( worn.has_value() );
    item &vest = **worn;

    damage_unit du( damage_bullet, bullet_damage );
    guy.ablative_armor_absorb( du, vest, torso_upper, roll );
    guy.armor_absorb( du, vest, body_part_torso, torso_upper, roll );
    return du.amount;
}

// The plate protects only when the coverage roll hits it, and then only once.
TEST_CASE( "iinf-b01_ablative_plate_protects_once_and_only_when_hit", "[audit]" )
{
    clear_avatar();
    Character &guy = get_player_character();
    const sub_bodypart_id torso_upper( sub_body_part_torso_upper );

    REQUIRE( item( itype_test_plate ).get_coverage( torso_upper ) == 45 );
    REQUIRE( item( itype_ballistic_vest_esapi ).get_coverage( torso_upper,
             item::cover_type::COVER_RANGED ) == 100 );

    // Least damage that gets through the bare vest: all of its materials apply
    damage_unit du_bare( damage_bullet, bullet_damage );
    item( itype_ballistic_vest_esapi ).mitigate_damage( du_bare, torso_upper, 0 );
    REQUIRE( du_bare.amount < bullet_damage );

    // Damage left after the plate alone, as ablative_armor_absorb applies it
    damage_unit du_plate( damage_bullet, bullet_damage );
    item( itype_test_plate ).mitigate_damage( du_plate );
    REQUIRE( du_plate.amount < bullet_damage );

    // Least damage that gets through the plate plus the bare vest
    damage_unit du_plate_and_bare = du_plate;
    item( itype_ballistic_vest_esapi ).mitigate_damage( du_plate_and_bare, torso_upper, 0 );
    REQUIRE( du_plate_and_bare.amount > 0.0f );

    // The vest's own materials are rolled individually, so repeat with fresh items
    SECTION( "roll misses the plate but hits the vest" ) {
        for( int i = 0; i < 20; ++i ) {
            const float left = hit_worn_plated_vest( guy, 100 );
            // No better than the bare vest at its best
            CHECK( left >= du_bare.amount - 0.01f );
        }
    }
    SECTION( "roll hits the plate" ) {
        for( int i = 0; i < 20; ++i ) {
            const float left = hit_worn_plated_vest( guy, 1 );
            // The plate applied...
            CHECK( left <= du_plate.amount + 0.01f );
            // ...but only once
            CHECK( left >= du_plate_and_bare.amount - 0.01f );
        }
    }
}

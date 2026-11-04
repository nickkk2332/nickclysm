
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "item.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"

// ---- CHAR-B05 ----

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_in_pit( "in_pit" );
static const efftype_id effect_webbed( "webbed" );

static const itype_id itype_rock( "rock" );

static const trait_id trait_test_char_b05_no_carry( "test_char_b05_no_carry" );
static const trait_id trait_test_char_b05_wing_left( "test_char_b05_wing_left" );
static const trait_id trait_test_char_b05_wing_right( "test_char_b05_wing_right" );

static void give_wings( avatar &you )
{
    you.set_mutation( trait_test_char_b05_wing_left );
    you.set_mutation( trait_test_char_b05_wing_right );
}

// can_fly() began with move_effects(), so asking whether a character could fly rolled their
// escapes from being downed, webbed, snared, crushed, in a pit or grabbed, and freed them when
// a roll succeeded.
TEST_CASE( "char-b05_asking_whether_one_can_fly_frees_no_one", "[audit][character]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    give_wings( you );
    REQUIRE( you.can_fly() );

    for( const efftype_id &held : {
             effect_downed, effect_webbed, effect_in_pit
         } ) {
        CAPTURE( held.str() );
        you.add_effect( held, 1_hours, true );
        int flew = 0;
        for( int i = 0; i < 100; ++i ) {
            if( you.can_fly() ) {
                ++flew;
            }
        }
        CHECK( flew == 0 );
        CHECK( you.has_effect( held ) );
        you.remove_effect( held );
    }
    CHECK( you.can_fly() );
}

// The pit traps put the character in the pit before asking can_fly(), which rolled the pit
// escape: about one fall in five ended out of the pit at once, and wings slowed a fall only
// when that roll succeeded.
TEST_CASE( "char-b05_falling_into_a_pit_rolls_no_escape", "[audit][character]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    const tripoint_bub_ms pos = you.pos_bub();

    SECTION( "without wings, you stay in the pit" ) {
        int climbed_out = 0;
        for( int i = 0; i < 20; ++i ) {
            trapfunc::pit( pos, &you, nullptr );
            if( !you.has_effect( effect_in_pit ) ) {
                ++climbed_out;
            }
            you.remove_effect( effect_in_pit );
            you.set_all_parts_hp_to_max();
        }
        CHECK( climbed_out == 0 );
    }
    SECTION( "wings always slow the fall" ) {
        give_wings( you );
        int hurt = 0;
        for( int i = 0; i < 20; ++i ) {
            const int hp_before = you.get_hp();
            trapfunc::pit( pos, &you, nullptr );
            if( you.get_hp() < hp_before ) {
                ++hurt;
            }
            CHECK( you.has_effect( effect_in_pit ) );
            you.remove_effect( effect_in_pit );
            you.set_all_parts_hp_to_max();
        }
        CHECK( hurt == 0 );
    }
}

// can_fly() worked out the load as 100 * weight_carried() / weight_capacity(), which divided
// by zero for a winged character whose carrying capacity was 0.
TEST_CASE( "char-b05_can_fly_with_no_carrying_capacity", "[audit][character]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    give_wings( you );
    you.set_mutation( trait_test_char_b05_no_carry );
    REQUIRE( you.weight_capacity() == 0_gram );

    CHECK( you.can_fly() );
    item rock( itype_rock );
    you.wield( rock );
    REQUIRE( you.weight_carried() > 0_gram );
    CHECK_FALSE( you.can_fly() );
}

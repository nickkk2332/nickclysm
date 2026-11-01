#include <map>
#include <optional>

#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "type_id.h"
#include "units.h"
#include "weather.h"

static const move_mode_id move_mode_crouch( "crouch" );
static const move_mode_id move_mode_prone( "prone" );
static const move_mode_id move_mode_walk( "walk" );

// How much a single update_frostbite() call raises the frostbite timer of a dry, bare,
// freezing right hand at the given air temperature (F) and wind speed (mph).
// Low risk zone adds 3, medium risk adds 8, high risk adds 72.
static int frostbite_timer_gain( avatar &you, const int temp_f, const int windpower )
{
    get_weather().forced_temperature = units::from_fahrenheit( temp_f );
    const bodypart_id hand( body_part_hand_r );
    you.set_part_frostbite_timer( hand, 0 );
    you.set_part_wetness( hand, 0 );
    you.set_part_temp_cur( hand, BODYTEMP_FREEZING );
    std::map<bodypart_id, int> warmth_per_bp;
    warmth_per_bp[hand] = 0;
    you.update_frostbite( hand, windpower, warmth_per_bp );
    return you.get_part_frostbite_timer( hand );
}

TEST_CASE( "char-b02_frostbite_risk_rises_with_wind_and_cold", "[audit]" )
{
    avatar &you = get_avatar();
    clear_avatar();
    restore_on_out_of_scope restore_temperature( get_weather().forced_temperature );

    // Values are chosen away from the zone borders, so rounding the temperature to whole
    // degrees cannot move them into another zone.
    SECTION( "between 10F and -5F, wind raises the risk from low to medium" ) {
        const int calm = frostbite_timer_gain( you, 5, 0 );
        const int windy = frostbite_timer_gain( you, 5, 19 );
        CHECK( calm == 3 );
        CHECK( windy == 8 );
        CHECK( windy > calm );
    }
    SECTION( "below -5F in wind, colder air raises the risk from medium to high" ) {
        const int mild = frostbite_timer_gain( you, -10, 10 );
        const int bitter = frostbite_timer_gain( you, -40, 10 );
        CHECK( mild == 8 );
        CHECK( bitter == 72 );
        CHECK( bitter > mild );
    }
    SECTION( "below -5F, stronger wind raises the risk from medium to high" ) {
        const int breezy = frostbite_timer_gain( you, -10, 10 );
        const int gale = frostbite_timer_gain( you, -10, 50 );
        CHECK( breezy == 8 );
        CHECK( gale == 72 );
    }
}

TEST_CASE( "char-b03_forced_move_mode_change_is_kept", "[audit]" )
{
    avatar &you = get_avatar();
    clear_avatar();
    clear_map_without_vision();
    you.set_movement_mode( move_mode_walk );
    you.set_desired_movement_mode( move_mode_walk );
    REQUIRE( you.is_walking() );
    REQUIRE_FALSE( you.is_waiting_to_change_mode_mode() );

    SECTION( "a direct mode change also becomes the desired mode" ) {
        you.set_movement_mode( move_mode_crouch );
        REQUIRE( you.is_crouching() );
        CHECK( you.get_desired_move_mode() == move_mode_crouch );
        CHECK_FALSE( you.is_waiting_to_change_mode_mode() );
    }
    SECTION( "falling down on broken legs is not undone by the next action" ) {
        you.set_part_hp_cur( body_part_leg_l, 0 );
        REQUIRE_FALSE( you.enough_working_legs() );
        avatar_action::move( you, get_map(), tripoint_rel_ms::south );
        REQUIRE( you.is_prone() );
        CHECK( you.get_desired_move_mode() == move_mode_prone );
        // Otherwise the next action stands up again, paying for it, and falls on the next step
        CHECK_FALSE( you.is_waiting_to_change_mode_mode() );
    }
}

TEST_CASE( "char-b03_move_mode_toggles_cancel_a_pending_change", "[audit]" )
{
    avatar &you = get_avatar();
    clear_avatar();
    you.set_movement_mode( move_mode_walk );
    you.set_desired_movement_mode( move_mode_walk );
    REQUIRE( you.is_walking() );

    SECTION( "prone" ) {
        you.toggle_prone_mode_desired();
        REQUIRE( you.get_desired_move_mode() == move_mode_prone );
        // The change is still pending, so toggling again must cancel it
        REQUIRE( you.is_walking() );
        you.toggle_prone_mode_desired();
        CHECK( you.get_desired_move_mode() == move_mode_walk );
        CHECK_FALSE( you.is_waiting_to_change_mode_mode() );
    }
    SECTION( "crouch" ) {
        you.toggle_crouch_mode_desired();
        REQUIRE( you.get_desired_move_mode() == move_mode_crouch );
        REQUIRE( you.is_walking() );
        you.toggle_crouch_mode_desired();
        CHECK( you.get_desired_move_mode() == move_mode_walk );
        CHECK_FALSE( you.is_waiting_to_change_mode_mode() );
    }
}

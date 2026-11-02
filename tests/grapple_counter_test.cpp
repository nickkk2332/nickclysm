#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "effect.h"
#include "enum_conversions.h"
#include "grapple.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "martialarts.h"
#include "math_parser_diag_value.h"
#include "mattack_common.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

// Grab counters, and the ongrab / ongrabbed martial-art hooks.

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_hit_by_player( "hit_by_player" );
static const efftype_id effect_mabuff_test_buff_ongrab( "mabuff:test_buff_ongrab" );
static const efftype_id effect_mabuff_test_buff_ongrabbed( "mabuff:test_buff_ongrabbed" );
static const efftype_id effect_sleep( "sleep" );

static const faction_id faction_your_followers( "your_followers" );

static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static const matec_id test_tech_grab_counter( "test_tech_grab_counter" );
static const matec_id test_tm_strike( "test_tm_strike" );

static const matype_id test_style_grab_counter( "test_style_grab_counter" );
static const matype_id test_style_grab_hooks( "test_style_grab_hooks" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_unarmed( "unarmed" );

static constexpr tripoint_bub_ms you_pos{ 60, 60, 0 };

static void use_style( Character &who, const matype_id &style )
{
    who.martial_arts_data->add_martialart( style );
    who.martial_arts_data->set_style( style, false );
    REQUIRE( who.martial_arts_data->selected_style() == style );
}

// Steady hands: the counter lands on a creature that doesn't dodge.
static void make_fighter( Character &who )
{
    who.set_dex_base( 20 );
    who.set_skill_level( skill_melee, 10 );
    who.set_skill_level( skill_unarmed, 10 );
    who.set_stamina( who.get_stamina_max() );
    who.set_moves( 100 );
}

static avatar &setup_scene( const matype_id &style )
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    avatar &you = get_avatar();
    you.setpos( get_map(), you_pos );
    make_fighter( you );
    use_style( you, style );
    return you;
}

// A grabber that doesn't dodge and can be knocked about; its grab always lands.
static monster &spawn_grabber( const tripoint_bub_ms &pos )
{
    monster &grabber = spawn_test_monster( "mon_test_counter_grabber", pos );
    grabber.set_dest( get_avatar().pos_abs() );
    return grabber;
}

// The monster's grab attack on the avatar, as it makes it on its turn.
static void monster_grabs( monster &grabber )
{
    REQUIRE( grabber.attack_target() == &get_avatar() );
    const mtype_special_attack &grab = grabber.type->special_attacks.at( "grab" );
    REQUIRE( grab->call( grabber ) );
}

static bool is_held( const Creature &c )
{
    return c.has_effect_with_flag( json_flag_GRAB );
}

static bool has_message_containing( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

static double num_var( const Creature &c, const std::string &key )
{
    const diag_value *val = c.maybe_get_value( key );
    REQUIRE( val != nullptr );
    return val->dbl();
}

static std::string str_var( const Creature &c, const std::string &key )
{
    const diag_value *val = c.maybe_get_value( key );
    REQUIRE( val != nullptr );
    return val->str();
}

TEST_CASE( "grab_counter_answers_a_monster_grab", "[grapple][martial_arts][grab]" )
{
    avatar &you = setup_scene( test_style_grab_counter );
    monster &grabber = spawn_grabber( you_pos + tripoint::east );
    const tripoint_bub_ms start = grabber.pos_bub();

    monster_grabs( grabber );
    // The counter is an attack on the grabber
    CHECK( grabber.has_effect( effect_hit_by_player ) );
    // An attack can miss: it knocks the grabber back once it lands. (A monster that holds
    // someone doesn't grab again.)
    for( int tries = 0; tries < 20 && grabber.pos_bub() == start; ++tries ) {
        grapple::release_grabs_by( grabber );
        you.set_moves( 100 );
        monster_grabs( grabber );
    }
    REQUIRE( grabber.pos_bub() == start + tripoint::east );
    // Knocked away, the grabber has let go, on both sides
    CHECK_FALSE( is_held( you ) );
    CHECK_FALSE( you.has_effect( effect_grabbed ) );
    CHECK_FALSE( grabber.has_effect( effect_grabbing ) );
    CHECK( grabber.grabbed_limbs.empty() );
}

TEST_CASE( "grab_counter_answers_a_character_grab", "[grapple][martial_arts][grab]" )
{
    avatar &you = setup_scene( test_style_grab_counter );
    npc &guy = spawn_npc( ( you_pos + tripoint::east ).xy(), "test_talker" );
    guy.set_attitude( NPCATT_KILL );
    const tripoint_bub_ms start = guy.pos_bub();

    // Any hold a character establishes goes through grapple::start_hold
    for( int tries = 0; tries < 20 && guy.pos_bub() == start; ++tries ) {
        you.set_moves( 100 );
        grapple::end_hold( guy, &you );
        REQUIRE( grapple::start_hold( guy, you, 20, body_part_arm_l.id() ) );
    }
    REQUIRE( guy.pos_bub() == start + tripoint::east );
    // The hold ended on both sides
    CHECK( grapple::hold_effect( guy, you ) == nullptr );
    CHECK_FALSE( is_held( you ) );
    CHECK_FALSE( guy.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    CHECK( guy.grabbed_creature() == nullptr );
}

TEST_CASE( "grab_counter_not_for_a_grab_you_go_along_with", "[grapple][martial_arts][grab]" )
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    avatar &you = get_avatar();
    you.setpos( get_map(), you_pos );
    npc &guy = spawn_npc( ( you_pos + tripoint::east ).xy(), "test_talker" );
    // The counter is unarmed
    guy.remove_weapon();
    make_fighter( guy );
    use_style( guy, test_style_grab_counter );

    SECTION( "a follower lets the avatar grab it" ) {
        guy.set_fac( faction_your_followers );
        REQUIRE( guy.is_player_ally() );
        REQUIRE( grapple::grab_tolerated( you, guy ) );
        REQUIRE( grapple::start_hold( you, guy, 20, body_part_arm_l.id() ) );
        CHECK( guy.get_moves() == 100 );
        CHECK( grapple::hold_effect( you, guy ) != nullptr );
        CHECK( you.grabbed_creature() == &guy );
    }
    SECTION( "an enemy goes without a roll, but answers" ) {
        guy.set_attitude( NPCATT_KILL );
        REQUIRE( guy.is_enemy() );
        REQUIRE( grapple::grab_tolerated( you, guy ) );
        REQUIRE( grapple::start_hold( you, guy, 20, body_part_arm_l.id() ) );
        CHECK( guy.get_moves() < 100 );
    }
}

TEST_CASE( "grab_counter_needs_stamina_reach_and_wits", "[grapple][martial_arts][grab]" )
{
    avatar &you = setup_scene( test_style_grab_counter );

    SECTION( "too exhausted: a message, and no attack" ) {
        monster &grabber = spawn_grabber( you_pos + tripoint::east );
        you.set_stamina( you.get_stamina_max() / 4 );
        Messages::clear_messages();
        monster_grabs( grabber );
        CHECK( has_message_containing( "You try to counterattack, but you are too exhausted!" ) );
        CHECK_FALSE( grabber.has_effect( effect_hit_by_player ) );
        CHECK( is_held( you ) );
        CHECK( grabber.has_effect( effect_grabbing ) );
    }
    SECTION( "out of reach: nothing" ) {
        monster &far = spawn_grabber( you_pos + tripoint( 2, 0, 0 ) );
        you.on_grabbed( far, body_part_arm_l.id(), 20 );
        CHECK_FALSE( far.has_effect( effect_hit_by_player ) );
        CHECK( you.get_moves() == 100 );
    }
    SECTION( "asleep: nothing" ) {
        monster &grabber = spawn_grabber( you_pos + tripoint::east );
        you.add_effect( effect_sleep, 1_hours );
        REQUIRE( you.in_sleep_state() );
        // (Falling asleep took the moves)
        you.set_moves( 100 );
        you.on_grabbed( grabber, body_part_arm_l.id(), 20 );
        CHECK_FALSE( grabber.has_effect( effect_hit_by_player ) );
        CHECK( you.get_moves() == 100 );
    }
}

TEST_CASE( "grab_counter_is_its_own_kind_of_attack", "[grapple][martial_arts]" )
{
    avatar &you = setup_scene( test_style_grab_counter );
    monster &target = spawn_grabber( you_pos + tripoint::east );
    const auto why_not = [&]( const matec_id & tec, bool grab_counter ) {
        technique_query query;
        query.grab_counter = grab_counter;
        query.roll = false;
        return io::enum_to_string( you.check_technique( tec, target, you.used_weapon(), query ) );
    };
    CHECK( why_not( test_tech_grab_counter, false ) == "grab_counter_only" );
    CHECK( why_not( test_tech_grab_counter, true ) == "none" );
    CHECK( why_not( test_tm_strike, true ) == "not_grab_counter" );
    CHECK( test_tech_grab_counter->rejection_text( technique_rejection::grab_counter_only ) ==
           "grab counter, automatic" );
    // The automatic choice for a grab counter
    technique_query grab;
    grab.grab_counter = true;
    CHECK( std::get<0>( you.pick_technique( target, you.used_weapon(), grab ) ) ==
           test_tech_grab_counter );
    CHECK( std::get<0>( you.pick_technique( target, you.used_weapon(), false, false,
                                            false ) ) != test_tech_grab_counter );
}

TEST_CASE( "grab_counter_load_checks", "[grapple][martial_arts]" )
{
    SECTION( "the TEST_DATA grab counter passes" ) {
        CHECK( capture_debugmsg_during( []() {
            test_tech_grab_counter->check();
        } ).empty() );
    }
    SECTION( "two kinds of counter at once can never be used" ) {
        ma_technique tech;
        tech.id = matec_id( "test_tech_two_counters" );
        tech.dodge_counter = true;
        tech.grab_counter = true;
        CHECK_THAT( capture_debugmsg_during( [&tech]() {
            tech.check();
        } ), Catch::Matchers::Contains( "can never be used" ) );
    }
    SECTION( "a defensive grab counter does nothing" ) {
        ma_technique tech;
        tech.id = matec_id( "test_tech_defensive_grab_counter" );
        tech.defensive = true;
        tech.grab_counter = true;
        CHECK_THAT( capture_debugmsg_during( [&tech]() {
            tech.check();
        } ), Catch::Matchers::Contains( "no effect" ) );
    }
}

TEST_CASE( "grab_hooks_fire_with_the_hold_as_context", "[grapple][martial_arts][grab]" )
{
    avatar &you = setup_scene( test_style_grab_hooks );

    SECTION( "ongrabbed: a monster's grab attack" ) {
        monster &grabber = spawn_grabber( you_pos + tripoint::east );
        monster_grabs( grabber );
        REQUIRE( is_held( you ) );
        const bodypart_id held = you.get_effects_with_flag( json_flag_GRAB ).front().get().get_bp();
        CHECK( num_var( you, "test_ongrabbed_strength" ) == 20 );
        CHECK( str_var( you, "test_ongrabbed_part" ) == held.id().str() );
        CHECK( num_var( grabber, "test_grabbed_a_stylist" ) == 1 );
        CHECK( you.has_effect( effect_mabuff_test_buff_ongrabbed ) );
        CHECK_FALSE( you.has_effect( effect_mabuff_test_buff_ongrab ) );
    }
    SECTION( "ongrab: you get a hold on a monster" ) {
        monster &mon = spawn_test_monster( "mon_zombie", you_pos + tripoint::east );
        REQUIRE( grapple::start_hold( you, mon, 30, bodypart_str_id::NULL_ID().id() ) );
        CHECK( num_var( you, "test_ongrab_strength" ) == 30 );
        CHECK( num_var( mon, "test_grabbed_by_style" ) == 1 );
        CHECK( you.has_effect( effect_mabuff_test_buff_ongrab ) );
        CHECK_FALSE( you.has_effect( effect_mabuff_test_buff_ongrabbed ) );
    }
    SECTION( "both: a character grabs a character" ) {
        npc &guy = spawn_npc( ( you_pos + tripoint::east ).xy(), "test_talker" );
        // Style buffs need a style weapon or empty hands
        guy.remove_weapon();
        use_style( guy, test_style_grab_hooks );
        REQUIRE( grapple::start_hold( you, guy, 25, body_part_arm_l.id() ) );
        CHECK( num_var( you, "test_ongrab_strength" ) == 25 );
        CHECK( str_var( you, "test_ongrab_part" ) == "arm_l" );
        CHECK( num_var( guy, "test_grabbed_by_style" ) == 1 );
        CHECK( num_var( guy, "test_ongrabbed_strength" ) == 25 );
        CHECK( str_var( guy, "test_ongrabbed_part" ) == "arm_l" );
        CHECK( num_var( you, "test_grabbed_a_stylist" ) == 1 );
        CHECK( guy.has_effect( effect_mabuff_test_buff_ongrabbed ) );
    }
}

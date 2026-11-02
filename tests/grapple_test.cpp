#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "grapple.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "teleport.h"
#include "type_id.h"
#include "units.h"

// Grabs started by characters (src/grapple.h), ported from Cataclysm: The Last Generation.
// Every roll below is either skipped (a tolerated grab, a hold set up directly) or made at
// inputs where its chance is 0 or 1.

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_incorporeal( "incorporeal" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_test_no_grab( "test_no_grab" );
static const efftype_id effect_winded( "winded" );

static const faction_id faction_your_followers( "your_followers" );

static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static const itype_id itype_rock( "rock" );

static const limb_score_id limb_score_grip( "grip" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_unarmed( "unarmed" );

static constexpr tripoint_bub_ms grabber_pos{ 60, 60, 0 };

static avatar &setup_grapple_scene()
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    avatar &you = get_avatar();
    you.setpos( get_map(), grabber_pos );
    return you;
}

static const bodypart_id &no_part()
{
    static const bodypart_id null_part = bodypart_str_id::NULL_ID().id();
    return null_part;
}

// Whether grabber holds victim, as both sides see it.
static bool holds( const Character &grabber, const Creature &victim )
{
    return grabber.grabbed_creature() == &victim && grabber.is_grabbing_creature() &&
           grapple::hold_effect( grabber, victim ) != nullptr &&
           grabber.has_effect_with_flag( json_flag_GRAB_FILTER );
}

// Whether nothing is left of grabber's hold on victim, on either side.
static bool released( const Character &grabber, const Creature &victim )
{
    return grabber.grabbed_creature() == nullptr && !grabber.is_grabbing_creature() &&
           grapple::hold_effect( grabber, victim ) == nullptr &&
           !grabber.has_effect_with_flag( json_flag_GRAB_FILTER );
}

static monster &spawn_held( Character &you, const std::string &type, int strength = 20 )
{
    monster &mon = spawn_test_monster( type, you.pos_bub() + tripoint::east );
    REQUIRE( grapple::start_hold( you, mon, strength, no_part() ) );
    REQUIRE( holds( you, mon ) );
    return mon;
}

static void make_pet( monster &mon )
{
    mon.friendly = -1;
    mon.add_effect( effect_pet, 1_turns, true );
    REQUIRE( mon.is_pet() );
}

static void make_follower( npc &guy )
{
    guy.set_fac( faction_your_followers );
    REQUIRE( guy.is_player_ally() );
}

static void wait_past_min_hold( const Character &grabber, const Creature &victim )
{
    calendar::turn += time_duration::from_turns( grapple::min_hold_turns( grabber, victim ) + 1 );
}

// Phase 0: groundwork

TEST_CASE( "grapple_remove_grab_clears_the_grabbing_effect", "[grapple][grab]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_test_monster( "mon_debug_grabber", you.pos_bub() + tripoint::east );
    mon.add_grab( body_part_arm_l );
    mon.add_grab( body_part_torso );
    REQUIRE( mon.has_effect( effect_grabbing ) );

    mon.remove_grab( body_part_torso );
    CHECK( mon.has_effect( effect_grabbing ) );
    CHECK( mon.is_grabbing( body_part_arm_l ) );

    // The last limb let go of: the grab filter goes, instead of being added again.
    mon.remove_grab( body_part_arm_l );
    CHECK( !mon.has_effect( effect_grabbing ) );
    CHECK( !mon.is_grabbing( body_part_arm_l ) );
}

TEST_CASE( "grapple_held_monster_does_not_keep_its_own_grab_alive", "[grapple][grab]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint{ 5, 0, 0 } );
    mon.add_effect( effect_grabbed, 1_days, true, 10 );
    mon.add_effect( effect_grabbing, 1_days, true, 1 );

    SECTION( "nobody else is held: the grab filter goes" ) {
        mon.process_turn();
        CHECK( !mon.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    }
    SECTION( "a held neighbour keeps it" ) {
        you.setpos( get_map(), mon.pos_bub() + tripoint::west );
        you.add_effect( effect_grabbed, 1_days, body_part_torso, true, 10 );
        mon.process_turn();
        CHECK( mon.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    }
}

TEST_CASE( "grapple_monster_grab_at_range_is_not_an_orphan", "[grapple][grab][audit]" )
{
    // CHAR-B09: the strider grabs at range 2; its grab must not be dropped as an orphan.
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    monster &strider = spawn_test_monster( "mon_zombie_strider",
                                           you.pos_bub() + tripoint{ 2, 0, 0 } );
    REQUIRE( grapple::monster_grab_reach( strider ) >= 2 );
    strider.add_grab( body_part_arm_l );
    you.add_effect( effect_grabbed, 1_days, body_part_arm_l, true, 50 );

    // An attack only removes orphan grabs.
    you.try_remove_grab( true );
    CHECK( you.has_effect( effect_grabbed, body_part_arm_l ) );
    CHECK( grapple::monster_grabber( you, body_part_arm_l.id() ) == &strider );
    strider.process_turn();
    CHECK( strider.is_grabbing( body_part_arm_l ) );

    // Out of reach, it is an orphan after all.
    const int beyond_reach = grapple::monster_grab_reach( strider ) + 1;
    strider.setpos( here, you.pos_bub() + tripoint{ beyond_reach, 0, 0 } );
    you.try_remove_grab( true );
    CHECK( !you.has_effect( effect_grabbed, body_part_arm_l ) );
}

TEST_CASE( "grapple_character_grab_strength", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    REQUIRE( you.get_limb_score( limb_score_grip ) == Approx( 1.0f ) );
    REQUIRE( you.get_size() == creature_size::medium );

    SECTION( "a fresh survivor" ) {
        REQUIRE( you.get_arm_str() == 8 );
        // 1 + 1.0 * ( 8 + 0 + 0 + 3 )
        CHECK( grapple::grab_strength( you, you ) == 12 );
    }
    SECTION( "TLG's worked example" ) {
        you.set_str_base( 10 );
        you.set_skill_level( skill_unarmed, 2 );
        you.set_skill_level( skill_melee, 2 );
        REQUIRE( you.get_arm_str() == 10 );
        // 1 + 1.0 * ( 10 + 2 + 2 + 3 )
        CHECK( grapple::grab_strength( you, you ) == 18 );

        dialogue d( get_talker_for( you ), std::make_unique<talker>() );
        math_exp strength;
        REQUIRE( strength.parse( "u_val('grab_strength')" ) );
        CHECK( strength.eval( d ) == Approx( 18 ) );
        math_exp arm_strength;
        REQUIRE( arm_strength.parse( "u_arm_strength()" ) );
        CHECK( arm_strength.eval( d ) == Approx( 10 ) );

        // While holding something, it reads the strength of that hold.
        spawn_held( you, "mon_zombie", 33 );
        CHECK( strength.eval( d ) == Approx( 33 ) );
    }
}

// Phase 1: holding, and every way a hold ends

TEST_CASE( "grapple_hold_ends_on_every_release_path", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    restore_on_out_of_scope restore_calendar_turn( calendar::turn );
    monster &mon = spawn_held( you, "mon_zombie" );

    SECTION( "letting go" ) {
        you.release_grapple();
    }
    SECTION( "wielding an item" ) {
        REQUIRE( you.can_grapple_while_wielding() );
        item rock( itype_rock );
        REQUIRE( you.wield( rock ) );
        REQUIRE( !you.can_grapple_while_wielding() );
    }
    SECTION( "mounting a creature" ) {
        monster &horse = spawn_test_monster( "mon_horse", you.pos_bub() + tripoint::west );
        make_pet( horse );
        you.mount_creature( horse );
        REQUIRE( you.is_mounted() );
    }
    SECTION( "falling asleep" ) {
        you.fall_asleep( 1_hours );
        you.process_turn();
    }
    SECTION( "getting winded" ) {
        you.add_effect( effect_winded, 10_turns );
        grapple::process_turn( you );
    }
    SECTION( "stamina below the floor" ) {
        you.set_stamina( grapple::stamina_floor() / 2 );
        you.process_turn();
    }
    SECTION( "the grabber turns incorporeal" ) {
        you.add_effect( effect_incorporeal, 1_hours );
        grapple::process_turn( you );
    }
    SECTION( "the grabber is flung" ) {
        override_option no_animations( "ANIMATIONS", "false" );
        g->fling_creature( &you, units::from_degrees( 180 ), 20, false, true );
    }
    SECTION( "the victim is knocked back" ) {
        mon.knock_back_from( you.pos_bub() );
        REQUIRE( mon.pos_bub() != you.pos_bub() + tripoint::east );
    }
    SECTION( "the victim teleports" ) {
        const tripoint_bub_ms away = you.pos_bub() + tripoint{ 0, 5, 0 };
        REQUIRE( teleport::teleport_to_point( mon, away, true, false, false ) );
    }
    SECTION( "the victim is no longer adjacent" ) {
        mon.setpos( here, you.pos_bub() + tripoint{ 3, 0, 0 } );
        you.process_turn();
        // The victim drops its side as an orphan when it next tries to move.
        mon.move_effects( false );
        CHECK( !mon.has_effect_with_flag( json_flag_GRAB ) );
    }
    SECTION( "the victim dies" ) {
        mon.die( &here, nullptr );
    }
    CHECK( released( you, mon ) );
    CHECK( !mon.has_effect( effect_grabbed ) );
}

TEST_CASE( "grapple_hold_ends_when_an_npc_victim_dies", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    REQUIRE( grapple::start_hold( you, guy, 20, body_part_torso.id() ) );
    REQUIRE( holds( you, guy ) );
    guy.die( &here, nullptr );
    CHECK( released( you, guy ) );
}

TEST_CASE( "grapple_victim_removed_from_the_map_leaves_no_hold", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_held( you, "mon_zombie" );
    g->remove_zombie( mon );
    // Nothing dangles: the hold is simply gone, and the next turn drops our side of it.
    CHECK( you.grabbed_creature() == nullptr );
    CHECK( !you.is_grabbing_creature() );
    you.process_turn();
    CHECK( !you.has_effect_with_flag( json_flag_GRAB_FILTER ) );
}

TEST_CASE( "grapple_hold_survives_save_and_load", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_held( you, "mon_zombie", 27 );

    std::ostringstream saved;
    JsonOut jsout( saved );
    mon.serialize( jsout );
    mon.clear_effects();
    REQUIRE( you.grabbed_creature() == nullptr );

    const JsonValue jv = json_loader::from_string( saved.str() );
    mon.deserialize( jv.get_object() );
    CHECK( holds( you, mon ) );
    const effect *const hold = grapple::hold_effect( you, mon );
    REQUIRE( hold != nullptr );
    CHECK( hold->get_intensity() == 27 );
}

// Phase 2: the grab action

TEST_CASE( "grapple_refusals_change_nothing", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::east );
    const Creature *target = &zed;

    SECTION( "armed" ) {
        item rock( itype_rock );
        REQUIRE( you.wield( rock ) );
    }
    SECTION( "no working arm" ) {
        you.set_part_hp_cur( body_part_arm_l, 0 );
        you.set_part_hp_cur( body_part_arm_r, 0 );
        REQUIRE( you.get_working_arm_count() == 0 );
    }
    SECTION( "an effect with the NO_GRAB flag" ) {
        you.add_effect( effect_test_no_grab, 1_hours );
    }
    SECTION( "too little stamina" ) {
        you.set_stamina( grapple::stamina_floor() - 1 );
    }
    SECTION( "a GRAB_IMMUNE target" ) {
        target = &spawn_test_monster( "mon_test_grab_immune", you.pos_bub() + tripoint::south );
    }
    SECTION( "an incorporeal target (grab_condition)" ) {
        zed.add_effect( effect_incorporeal, 1_hours );
    }
    SECTION( "out of reach" ) {
        zed.setpos( here, you.pos_bub() + tripoint{ 2, 0, 0 } );
    }
    SECTION( "no free body part" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
        for( const bodypart_id &bp : guy.get_all_body_parts( get_body_part_flags::only_main ) ) {
            guy.add_effect( effect_grabbed, 1_days, bp, true, 10 );
        }
        REQUIRE( !grapple::free_grab_part( guy ).has_value() );
        target = &guy;
    }

    const int stamina = you.get_stamina();
    const int moves = you.get_moves();
    CHECK( grapple::grab_refusal( you, *target ).has_value() );
    CHECK( you.get_stamina() == stamina );
    CHECK( you.get_moves() == moves );
    CHECK( !you.has_effect_with_flag( json_flag_GRAB_FILTER ) );
    CHECK( grapple::hold_effect( you, *target ) == nullptr );
}

TEST_CASE( "grapple_tolerated_grab_sets_both_effects", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    you.set_str_base( 10 );
    you.set_skill_level( skill_unarmed, 2 );
    you.set_skill_level( skill_melee, 2 );
    const int stamina = you.get_stamina();
    const int moves = you.get_moves();

    SECTION( "a pet" ) {
        monster &cow = spawn_test_monster( "mon_cow", you.pos_bub() + tripoint::east );
        make_pet( cow );
        REQUIRE( grapple::grab_tolerated( you, cow ) );
        REQUIRE( !grapple::grab_refusal( you, cow ).has_value() );
        REQUIRE( grapple::attempt_grab( you, cow ) );
        CHECK( holds( you, cow ) );
        const effect *const hold = grapple::hold_effect( you, cow );
        REQUIRE( hold != nullptr );
        CHECK( hold->get_bp() == no_part() );
        CHECK( hold->get_intensity() == 18 );
        CHECK( hold->get_intensity() == grapple::grab_strength( you, cow ) );
    }
    SECTION( "a follower" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
        make_follower( guy );
        REQUIRE( grapple::grab_tolerated( you, guy ) );
        REQUIRE( !grapple::grab_refusal( you, guy ).has_value() );
        REQUIRE( grapple::attempt_grab( you, guy ) );
        CHECK( holds( you, guy ) );
        const effect *const hold = grapple::hold_effect( you, guy );
        REQUIRE( hold != nullptr );
        CHECK( hold->get_bp() == hold->get_bp()->main_part.id() );
        CHECK( hold->get_bp() != no_part() );
        CHECK( hold->get_intensity() == 18 );
    }
    // The grab costs a hundred moves plus an unarmed attack, and arm stamina.
    CHECK( you.get_moves() <= moves - 100 );
    CHECK( you.get_stamina() == stamina - grapple::arm_stamina_cost() );
}

TEST_CASE( "grapple_skips_body_parts_already_held", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    make_follower( guy );
    for( const bodypart_id &bp : guy.get_all_body_parts( get_body_part_flags::only_main ) ) {
        if( bp != body_part_torso.id() ) {
            guy.add_effect( effect_grabbed, 1_days, bp, true, 10 );
        }
    }
    const std::optional<bodypart_id> free_part = grapple::free_grab_part( guy );
    REQUIRE( free_part.has_value() );
    REQUIRE( *free_part == body_part_torso.id() );
    REQUIRE( grapple::attempt_grab( you, guy ) );
    const effect *const hold = grapple::hold_effect( you, guy );
    REQUIRE( hold != nullptr );
    CHECK( hold->get_bp() == body_part_torso.id() );
}

TEST_CASE( "grapple_second_grabber_wrests_a_held_monster_free", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    monster &cow = spawn_test_monster( "mon_cow", you.pos_bub() + tripoint::east );
    make_pet( cow );
    monster &grabber = spawn_test_monster( "mon_debug_grabber",
                                           cow.pos_bub() + tripoint::east );
    grabber.add_effect( effect_grabbing, 1_days, true, 1 );
    // A weak hold: half our strength beats it for sure.
    cow.add_effect( effect_grabbed, 1_days, true, 1 );
    REQUIRE( grapple::monster_grabber( cow, no_part() ) == &grabber );

    REQUIRE( grapple::attempt_grab( you, cow ) );
    CHECK( holds( you, cow ) );
    const effect *const hold = grapple::hold_effect( you, cow );
    REQUIRE( hold != nullptr );
    CHECK( hold->get_intensity() == grapple::grab_strength( you, cow ) );
}

TEST_CASE( "grapple_who_tolerates_a_grab_and_who_must_be_confirmed", "[grapple]" )
{
    using grapple::grab_confirmation;
    avatar &you = setup_grapple_scene();

    SECTION( "a neutral NPC: really attack?" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
        guy.set_attitude( NPCATT_NULL );
        REQUIRE( !guy.is_player_ally() );
        REQUIRE( !guy.is_enemy() );
        CHECK( !grapple::grab_tolerated( you, guy ) );
        CHECK( grapple::needs_confirmation( you, guy ) == grab_confirmation::attack_npc );
    }
    SECTION( "a follower" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
        make_follower( guy );
        CHECK( grapple::grab_tolerated( you, guy ) );
        CHECK( grapple::needs_confirmation( you, guy ) == grab_confirmation::none );
    }
    SECTION( "a hostile monster" ) {
        monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::east );
        REQUIRE( zed.attitude_to( you ) == Creature::Attitude::HOSTILE );
        CHECK( !grapple::grab_tolerated( you, zed ) );
        CHECK( grapple::needs_confirmation( you, zed ) == grab_confirmation::none );
    }
    SECTION( "a peaceful monster" ) {
        monster &cow = spawn_test_monster( "mon_cow", you.pos_bub() + tripoint::east );
        REQUIRE( cow.attitude_to( you ) != Creature::Attitude::HOSTILE );
        CHECK( !grapple::grab_tolerated( you, cow ) );
        CHECK( grapple::needs_confirmation( you, cow ) == grab_confirmation::anger_faction );
    }
    SECTION( "a pet" ) {
        monster &cow = spawn_test_monster( "mon_cow", you.pos_bub() + tripoint::east );
        make_pet( cow );
        CHECK( grapple::grab_tolerated( you, cow ) );
        CHECK( grapple::needs_confirmation( you, cow ) == grab_confirmation::none );
    }
}

TEST_CASE( "grapple_formulas", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    REQUIRE( you.get_size() == creature_size::medium );

    SECTION( "stamina the grabber spends per escape attempt" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
        REQUIRE( guy.get_size() == creature_size::medium );
        // max( 1, 100 - hold ) * ( max( 1, size difference ) + skill ), within 100 to 400
        CHECK( grapple::escape_stamina_cost( you, guy, 20, 3 ) == 320 );
        CHECK( grapple::escape_stamina_cost( you, guy, 90, 0 ) == 100 );
        CHECK( grapple::escape_stamina_cost( you, guy, 1, 10 ) == 400 );
        monster &hulk = spawn_test_monster( "mon_zombie_hulk", you.pos_bub() + tripoint::south );
        REQUIRE( hulk.get_size() == creature_size::huge );
        CHECK( grapple::escape_stamina_cost( you, hulk, 40, 1 ) == 60 * ( 2 + 1 ) );
    }
    SECTION( "minimum hold time" ) {
        // 1 + 100 / speed, rounded down
        REQUIRE( you.get_speed() == 100 );
        CHECK( grapple::min_hold_turns( you, you ) == 2 );
        monster &slow = spawn_test_monster( "mon_test_non_shearable",
                                            you.pos_bub() + tripoint::east );
        REQUIRE( slow.get_speed() == 50 );
        CHECK( grapple::min_hold_turns( you, slow ) == 3 );
        monster &cow = spawn_test_monster( "mon_cow", you.pos_bub() + tripoint::south );
        REQUIRE( cow.get_speed() == 130 );
        CHECK( grapple::min_hold_turns( you, cow ) == 1 );
    }
}

// Phase 3: how the held creature reacts

TEST_CASE( "grapple_held_monster_reactions", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    restore_on_out_of_scope restore_calendar_turn( calendar::turn );
    monster &cow = spawn_test_monster( "mon_cow", you.pos_bub() + tripoint::east );
    REQUIRE( cow.get_dodge() > 0.0f );
    REQUIRE( cow.type->has_anger_trigger( mon_trigger::HURT ) );
    // A hold of strength 1: every escape roll succeeds once the monster may roll at all.
    REQUIRE( grapple::start_hold( you, cow, 1, no_part() ) );
    const int stamina = you.get_stamina();
    const int anger = cow.anger;

    SECTION( "it can't dodge while held" ) {
        CHECK( cow.get_dodge() == 0.0f );
    }
    SECTION( "it can't break free inside the minimum hold time" ) {
        CHECK( !cow.move_effects( false ) );
        CHECK( holds( you, cow ) );
        CHECK( you.get_stamina() == stamina );
        CHECK( cow.anger > anger );
    }
    SECTION( "after that it breaks free, and the grabber pays for the struggle" ) {
        wait_past_min_hold( you, cow );
        const int cost = grapple::escape_stamina_cost( you, cow, 1,
                         static_cast<int>( std::max( cow.get_melee(), 0.0f ) ) );
        CHECK( cow.move_effects( false ) );
        CHECK( released( you, cow ) );
        CHECK( !cow.has_effect( effect_grabbed ) );
        CHECK( you.get_stamina() == stamina - cost );
        CHECK( cow.anger > anger );
    }
}

TEST_CASE( "grapple_followers_do_not_resist_the_avatar", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    restore_on_out_of_scope restore_calendar_turn( calendar::turn );
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    make_follower( guy );
    REQUIRE( grapple::start_hold( you, guy, 1, body_part_torso.id() ) );
    wait_past_min_hold( you, guy );
    const int stamina = you.get_stamina();
    CHECK( !guy.try_remove_grab() );
    CHECK( holds( you, guy ) );
    CHECK( you.get_stamina() == stamina );
}

TEST_CASE( "grapple_a_struggling_npc_tires_the_grabber", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    restore_on_out_of_scope restore_calendar_turn( calendar::turn );
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    guy.set_attitude( NPCATT_NULL );
    REQUIRE( !guy.is_player_ally() );
    REQUIRE( grapple::start_hold( you, guy, 1, body_part_torso.id() ) );

    // Inside the minimum hold time there is no struggle at all.
    int stamina = you.get_stamina();
    CHECK( !guy.try_remove_grab() );
    CHECK( holds( you, guy ) );
    CHECK( you.get_stamina() == stamina );

    wait_past_min_hold( you, guy );
    stamina = you.get_stamina();
    guy.try_remove_grab();
    CHECK( you.get_stamina() < stamina );
}

TEST_CASE( "grapple_player_held_by_an_npc_is_not_an_orphan", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    REQUIRE( grapple::start_hold( guy, you, 20, body_part_arm_l.id() ) );
    REQUIRE( holds( guy, you ) );

    // An attack only removes orphan grabs, and this one has a grabber.
    you.try_remove_grab( true );
    CHECK( you.has_effect( effect_grabbed, body_part_arm_l ) );
    // Inside the minimum hold time a struggle can't break it either.
    CHECK( !you.try_remove_grab() );
    CHECK( holds( guy, you ) );

    // With the NPC gone, it is an orphan.
    guy.setpos( here, you.pos_bub() + tripoint{ 4, 0, 0 } );
    you.try_remove_grab( true );
    CHECK( !you.has_effect( effect_grabbed, body_part_arm_l ) );
}

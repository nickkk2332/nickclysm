#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect.h"
#include "enums.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "grapple.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "math_parser.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "teleport.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

// Grabs started by characters (src/grapple.h), ported from Cataclysm: The Last Generation.
// Every roll below is either skipped (a tolerated grab, a hold set up directly) or made at
// inputs where its chance is 0 or 1.

static const character_modifier_id
character_modifier_melee_stamina_cost_mod( "melee_stamina_cost_mod" );

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_incorporeal( "incorporeal" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_test_no_grab( "test_no_grab" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_winded( "winded" );

static const faction_id faction_your_followers( "your_followers" );

static const furn_str_id furn_f_chair( "f_chair" );

static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static const itype_id itype_rock( "rock" );

static const limb_score_id limb_score_grip( "grip" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_throw( "throw" );
static const skill_id skill_unarmed( "unarmed" );

static const ter_str_id ter_t_water_dp( "t_water_dp" );

static const vproto_id vehicle_prototype_bicycle( "bicycle" );

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
    SECTION( "an enemy NPC: no question, but it doesn't go along with it" ) {
        npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
        guy.set_attitude( NPCATT_KILL );
        REQUIRE( guy.is_enemy() );
        CHECK( !grapple::grab_tolerated( you, guy ) );
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

// Phase 4: dragging

TEST_CASE( "grapple_drag_odds", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    REQUIRE( you.get_size() == creature_size::medium );
    // 50 kg, medium size
    monster &mon = spawn_test_monster( "mon_test_speed_desc_base", you.pos_bub() + tripoint::east );
    REQUIRE( units::to_kilogram( mon.get_weight() ) == Approx( 50.0 ) );

    SECTION( "TLG's formula: one time in ( weight / ( strength * size ) + 0.25 ) ^ 2" ) {
        REQUIRE( grapple::start_hold( you, mon, 20, no_part() ) );
        // 50 / ( 20 * 3 ) = 0.8333, ( 0.8333 + 0.25 ) ^ 2 = 1.1736
        CHECK( grapple::drag_chance( you, mon ) == Approx( 1 / 1.173611 ).epsilon( 1e-4 ) );
    }
    SECTION( "light enough is always dragged" ) {
        REQUIRE( grapple::start_hold( you, mon, 100, no_part() ) );
        CHECK( grapple::drag_chance( you, mon ) == Approx( 1.0 ) );
    }
    SECTION( "a pet is half as hard to drag" ) {
        REQUIRE( grapple::start_hold( you, mon, 10, no_part() ) );
        // 50 / 30 = 1.6667, ( 1.6667 + 0.25 ) ^ 2 = 3.6736
        CHECK( grapple::drag_chance( you, mon ) == Approx( 1 / 3.673611 ).epsilon( 1e-4 ) );
        make_pet( mon );
        // Half the odds: ( 0.8333 + 0.25 ) ^ 2 = 1.1736
        CHECK( grapple::drag_chance( you, mon ) == Approx( 1 / 1.173611 ).epsilon( 1e-4 ) );
    }
    SECTION( "the heaviest are dragged one time in a thousand" ) {
        monster &hulk = spawn_test_monster( "mon_zombie_hulk", you.pos_bub() + tripoint::west );
        REQUIRE( grapple::start_hold( you, hulk, 1, no_part() ) );
        CHECK( grapple::drag_chance( you, hulk ) == Approx( 0.001 ) );
    }
}

TEST_CASE( "grapple_drag_ally_discount", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    guy.set_attitude( NPCATT_NULL );
    REQUIRE( !guy.is_player_ally() );
    REQUIRE( grapple::start_hold( you, guy, 10, body_part_torso.id() ) );
    const double odds = units::to_kilogram( guy.get_weight() ) / ( 10.0 * 3 );
    const double stranger = grapple::drag_chance( you, guy );
    CHECK( stranger == Approx( 1 / std::clamp( std::pow( odds + 0.25, 2 ), 1.0, 1000.0 ) ) );

    // A follower walks along: the odds shrink by its move cost / 200.
    make_follower( guy );
    const int move_cost = guy.run_cost( 100, false );
    REQUIRE( move_cost < 200 );
    const double ally = grapple::drag_chance( you, guy );
    const double eased = odds * move_cost / 200.0;
    CHECK( ally == Approx( 1 / std::clamp( std::pow( eased + 0.25, 2 ), 1.0, 1000.0 ) ) );
    CHECK( ally > stranger );

    // Not while it sleeps.
    guy.fall_asleep( 1_hours );
    CHECK( grapple::drag_chance( you, guy ) == Approx( stranger ) );
}

TEST_CASE( "grapple_drag_across_a_map_shift_keeps_the_hold", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    const tripoint_abs_sm origin = here.get_abs_sub();
    on_out_of_scope recentre( [&]() {
        avatar &u = get_avatar();
        u.release_grapple();
        const tripoint_rel_sm off = origin - here.get_abs_sub();
        u.setpos( here, u.pos_bub() + tripoint_rel_ms( off.x() * SEEX, off.y() * SEEY, 0 ) );
        g->update_map( u );
    } );
    monster &zed = spawn_held( you, "mon_zombie", 100 );
    REQUIRE( grapple::drag_chance( you, zed ) == Approx( 1.0 ) );
    const tripoint_abs_ms start = you.pos_abs();
    const int moves = you.get_moves();

    // One step west crosses into the next submap: the map shifts to recentre on us.
    const tripoint_bub_ms west = you.pos_bub() + tripoint::west;
    REQUIRE( west.x() < HALF_MAPSIZE_X );
    CHECK( g->walk_move( west, false, false ) );
    CHECK( here.get_abs_sub() != origin );
    CHECK( you.pos_abs() == start + tripoint::west );
    CHECK( you.get_moves() < moves );
    // The zombie took the tile we left, in absolute terms, and we still hold it.
    CHECK( zed.pos_abs() == start );
    CHECK( holds( you, zed ) );
    you.process_turn();
    CHECK( holds( you, zed ) );
}

TEST_CASE( "grapple_drag_an_npc_onto_a_vehicle_boards_it", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    // A bicycle with its saddle under us.
    vehicle *const bike = here.add_vehicle( vehicle_prototype_bicycle, you.pos_bub(), 0_degrees,
                                            0, veh_spawn_status::UNDAMAGED );
    REQUIRE( bike != nullptr );
    here.board_vehicle( you.pos_bub(), &you );
    REQUIRE( you.in_vehicle );
    const tripoint_abs_ms saddle = you.pos_abs();
    npc &guy = spawn_npc( you.pos_bub().xy() + point::south, "thug" );
    make_follower( guy );
    REQUIRE( grapple::start_hold( you, guy, 100, body_part_torso.id() ) );
    REQUIRE( grapple::drag_chance( you, guy ) == Approx( 1.0 ) );

    CHECK( g->walk_move( you.pos_bub() + tripoint::north, false, false ) );
    CHECK( you.pos_abs() == saddle + tripoint::north );
    CHECK( !you.in_vehicle );
    CHECK( guy.pos_abs() == saddle );
    CHECK( guy.in_vehicle );
    CHECK( holds( you, guy ) );
}

TEST_CASE( "grapple_a_step_the_victim_cannot_follow_is_refused", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    const tripoint_abs_ms start = you.pos_abs();
    const int moves = you.get_moves();
    const int stamina = you.get_stamina();
    Creature *victim = nullptr;

    SECTION( "an immobile monster" ) {
        monster &mon = spawn_test_monster( "mon_test_speed_desc_base_immobile",
                                           you.pos_bub() + tripoint::east );
        REQUIRE( grapple::start_hold( you, mon, 100, no_part() ) );
        victim = &mon;
    }
    SECTION( "a tied monster" ) {
        monster &mon = spawn_held( you, "mon_zombie", 100 );
        mon.add_effect( effect_tied, 1_turns, true );
        victim = &mon;
    }
    SECTION( "a pet standing in the way" ) {
        monster &mon = spawn_held( you, "mon_zombie", 100 );
        monster &dog = spawn_test_monster( "mon_dog", you.pos_bub() + tripoint::west );
        make_pet( dog );
        victim = &mon;
    }
    REQUIRE( victim != nullptr );
    REQUIRE( holds( you, *victim ) );

    CHECK( g->walk_move( you.pos_bub() + tripoint::west, false, false ) );
    // Nothing happened: no step, no cost, and the hold is still there.
    CHECK( you.pos_abs() == start );
    CHECK( you.get_moves() == moves );
    CHECK( you.get_stamina() == stamina );
    CHECK( holds( you, *victim ) );
}

TEST_CASE( "grapple_no_swimming_while_holding", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    // A pet, so that safe mode has nothing to say about the step.
    monster &dog = spawn_held( you, "mon_dog", 100 );
    make_pet( dog );
    const tripoint_bub_ms pool = you.pos_bub() + tripoint::west;
    here.ter_set( pool, ter_t_water_dp.id() );
    const tripoint_abs_ms start = you.pos_abs();
    CHECK( !avatar_action::move( you, here, tripoint_rel_ms::west ) );
    CHECK( you.pos_abs() == start );
    CHECK( !you.is_swimming );
    CHECK( holds( you, dog ) );
}

TEST_CASE( "grapple_holding_a_creature_and_an_object_are_exclusive", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::east );
    const tripoint_bub_ms chair = you.pos_bub() + tripoint::west;
    here.furn_set( chair, furn_f_chair.id() );
    you.grab( object_type::FURNITURE, tripoint_rel_ms::west );
    const std::optional<std::string> refusal = grapple::grab_refusal( you, zed );
    REQUIRE( refusal.has_value() );
    CHECK( *refusal == "You're already holding on to something." );
    you.grab( object_type::NONE );
    CHECK( !grapple::grab_refusal( you, zed ).has_value() );
}

// Phase 5: throwing, and what game::fling_creature does for it

static const efftype_id effect_airborne( "airborne" );
static const efftype_id effect_beartrap( "beartrap" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_open_air( "t_open_air" );

static const trap_str_id tr_beartrap( "tr_beartrap" );

TEST_CASE( "grapple_a_fling_breaks_holds_both_ways", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    override_option no_animations( "ANIMATIONS", "false" );

    SECTION( "a creature held by a character is flung out of the hold" ) {
        // flvel / 2 beats the hold's strength every time.
        monster &zed = spawn_held( you, "mon_zombie", 10 );
        REQUIRE( g->fling_creature( &zed, 0_degrees, 50.0f, true, true ) );
        CHECK( released( you, zed ) );
        CHECK( !zed.has_effect( effect_grabbed ) );
    }
    SECTION( "a flung monster lets go of what it holds" ) {
        monster &grabber = spawn_test_monster( "mon_debug_grabber",
                                               you.pos_bub() + tripoint::east );
        grabber.add_grab( body_part_arm_l );
        you.add_effect( effect_grabbed, 1_days, body_part_arm_l, true, 10 );
        REQUIRE( grapple::monster_grabber( you, body_part_arm_l ) == &grabber );
        REQUIRE( g->fling_creature( &grabber, 0_degrees, 50.0f, true, true ) );
        CHECK( !you.has_effect( effect_grabbed, body_part_arm_l ) );
        CHECK( !grabber.has_effect( effect_grabbing ) );
        CHECK( !grabber.is_grabbing( body_part_arm_l ) );
    }
}

TEST_CASE( "grapple_flung_creatures_are_airborne_until_they_land", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    map &here = get_map();
    // Keep the avatar out of the way.
    you.setpos( here, tripoint_bub_ms( 60, 70, 0 ) );
    const tripoint_bub_ms start( 50, 60, 0 );
    monster &zed = spawn_test_monster( "mon_zombie", start );

    SECTION( "it flies over a gap instead of dropping into it" ) {
        here.ter_set( start + tripoint( 2, 0, 0 ), ter_t_open_air.id() );
        here.ter_set( start + tripoint( 2, 0, -1 ), ter_t_floor.id() );
        here.invalidate_map_cache( 0 );
        here.build_map_cache( 0, true );
        REQUIRE( g->fling_creature( &zed, 0_degrees, 40.0f, true, true ) );
        CHECK( zed.pos_bub() == start + tripoint( 4, 0, 0 ) );
    }
    SECTION( "it drops off a ledge where it lands" ) {
        for( int x = 3; x <= 5; ++x ) {
            here.ter_set( start + tripoint( x, 0, 0 ), ter_t_open_air.id() );
            here.ter_set( start + tripoint( x, 0, -1 ), ter_t_floor.id() );
        }
        here.invalidate_map_cache( 0 );
        here.build_map_cache( 0, true );
        here.build_map_cache( -1, true );
        REQUIRE( g->fling_creature( &zed, 0_degrees, 40.0f, true, true ) );
        CHECK( zed.pos_bub() == start + tripoint( 4, 0, -1 ) );
    }
    SECTION( "only the landing tile's trap goes off" ) {
        here.trap_set( start + tripoint( 2, 0, 0 ), tr_beartrap.id() );
        here.trap_set( start + tripoint( 4, 0, 0 ), tr_beartrap.id() );
        REQUIRE( g->fling_creature( &zed, 0_degrees, 40.0f, true, true ) );
        CHECK( zed.pos_bub() == start + tripoint( 4, 0, 0 ) );
        CHECK( here.tr_at( start + tripoint( 2, 0, 0 ) ).id == tr_beartrap );
        CHECK( here.tr_at( start + tripoint( 4, 0, 0 ) ).is_null() );
        CHECK( zed.has_effect( effect_beartrap ) );
    }
    CHECK( !zed.has_effect( effect_airborne ) );
}

TEST_CASE( "grapple_fling_options_are_off_by_default", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 60, 70, 0 ) );
    const tripoint_bub_ms start( 50, 60, 0 );

    SECTION( "a slow uncontrolled landing is soft only when asked" ) {
        npc &guy = spawn_npc( start.xy(), "test_talker" );
        guy.clear_worn();
        const int hp = guy.get_hp();
        fling_options soft;
        soft.soft_low_speed_landing = true;
        // Speed 20: the landing does rng( 20, 40 ) / 9, halved less 5: nothing.
        REQUIRE( g->fling_creature( &guy, 0_degrees, 20.0f, false, true, soft ) );
        CHECK( guy.pos_bub() == start + tripoint( 2, 0, 0 ) );
        CHECK( guy.get_hp() == hp );
    }
    SECTION( "a character in the way is hurt only when asked" ) {
        monster &zed = spawn_test_monster( "mon_zombie", start );
        npc &guy = spawn_npc( ( start + tripoint( 3, 0, 0 ) ).xy(), "test_talker" );
        guy.clear_worn();
        const int hp = guy.get_hp();
        REQUIRE( g->fling_creature( &zed, 0_degrees, 50.0f, true, true ) );
        // Stopped as if by a wall, and nobody else hurt.
        CHECK( zed.pos_bub() == start + tripoint( 2, 0, 0 ) );
        CHECK( guy.get_hp() == hp );

        zed.setpos( here, start );
        fling_options hit;
        hit.hit_characters = true;
        REQUIRE( g->fling_creature( &zed, 0_degrees, 50.0f, true, true, hit ) );
        CHECK( zed.pos_bub() == start + tripoint( 2, 0, 0 ) );
        // rng( 50, 100 ) / 6 to an unarmoured torso
        CHECK( guy.get_hp() < hp );
    }
}

// TLG's worked example: strength 10, unarmed and throwing 2. Rested.
static void make_thrower( avatar &you )
{
    you.set_str_base( 10 );
    you.set_skill_level( skill_unarmed, 2 );
    you.set_skill_level( skill_throw, 2 );
    REQUIRE( you.get_arm_str() == 10 );
    you.set_stamina( you.get_stamina_max() );
}

TEST_CASE( "grapple_throw_formulas", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    map &here = get_map();
    make_thrower( you );
    // 50 kg, medium size
    monster &mon = spawn_test_monster( "mon_test_speed_desc_base", you.pos_bub() + tripoint::east );
    REQUIRE( mon.get_size() == creature_size::medium );

    SECTION( "force" ) {
        // 2 * ( 10 * 1.1 + 2 * 2 + 2 / 4 )
        CHECK( grapple::throw_force( you, mon ) == Approx( 31.0f ) );
        CHECK( grapple::throw_range( 31.0f ) == 3 );
        // Strength over 10 counts extra: 2 * ( 15 * 1.25 + 4 + 0.5 )
        you.set_str_base( 15 );
        CHECK( grapple::throw_force( you, mon ) == Approx( 46.5f ) );
    }
    SECTION( "force against other sizes" ) {
        monster &squirrel = spawn_test_monster( "mon_squirrel", you.pos_bub() + tripoint::west );
        REQUIRE( squirrel.get_size() == creature_size::tiny );
        // 2 * ( 15.5 + 2 * ( 3 - 1 ) )
        CHECK( grapple::throw_force( you, squirrel ) == Approx( 39.0f ) );
        monster &hulk = spawn_test_monster( "mon_zombie_hulk", you.pos_bub() + tripoint::north );
        REQUIRE( hulk.get_size() == creature_size::huge );
        // 2 * ( 15.5 + 2 * ( 3 - 5 ) - 15 * 2 ) is below nothing
        CHECK( grapple::throw_force( you, hulk ) == Approx( 0.0f ) );
    }
    SECTION( "force in deep water is a quarter" ) {
        here.ter_set( mon.pos_bub(), ter_t_water_dp.id() );
        CHECK( grapple::throw_force( you, mon ) == Approx( 31.0f / 4 ) );
    }
    SECTION( "force scales with the distance chosen, and carries exactly that far" ) {
        CHECK( grapple::throw_force_at( 31.0f, 3 ) == Approx( 31.0f ) );
        CHECK( grapple::throw_force_at( 31.0f, 2 ) == Approx( 31.0f * 2 / 3 ) );
        CHECK( grapple::throw_force_at( 31.0f, 1 ) == Approx( 31.0f / 3 ) );
        CHECK( grapple::throw_force_at( 31.0f, 5 ) == Approx( 31.0f ) );
        for( const float full : {
                 20.0f, 31.0f, 39.9f, 60.0f, 99.0f
             } ) {
            for( int d = 1; d <= grapple::throw_range( full ); ++d ) {
                CAPTURE( full, d );
                CHECK( grapple::throw_range( grapple::throw_force_at( full, d ) ) == d );
            }
        }
    }
    SECTION( "stamina is weight-scaled, not a flat 50" ) {
        REQUIRE( you.get_modifier( character_modifier_melee_stamina_cost_mod ) == Approx( 1.0f ) );
        // ( 50 * 50 kg / ( 2 * 3 / 3 + ( 2 + 2 ) / 20 ) + 50 ) * 31 / 100
        CHECK( grapple::throw_stamina_cost( you, mon, 31.0f ) == 368 );
        // Twice the force, twice the cost.
        CHECK( grapple::throw_stamina_cost( you, mon, 62.0f ) == 736 );
        // At least 50, for a feeble shove.
        CHECK( grapple::throw_stamina_cost( you, mon, 1.0f ) == 50 );
        monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::west );
        REQUIRE( zed.get_weight() > mon.get_weight() );
        CHECK( grapple::throw_stamina_cost( you, zed, 31.0f ) >
               grapple::throw_stamina_cost( you, mon, 31.0f ) );
    }
    SECTION( "the word for it" ) {
        CHECK( grapple::throw_verb( you, mon, 25.0f ) == "shove" );
        CHECK( grapple::throw_verb( you, mon, 35.0f ) == "toss" );
        CHECK( grapple::throw_verb( you, mon, 50.0f ) == "throw" );
        CHECK( grapple::throw_verb( you, mon, 65.0f ) == "hurl" );
        const std::string launch = grapple::throw_verb( you, mon, 80.0f );
        CHECK( ( launch == "launch" || launch == "yeet" ) );
        CHECK( grapple::throw_verb_tier( you, mon, 80.0f ) == 4 );
    }
}

TEST_CASE( "grapple_who_minds_being_thrown", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "thug" );
    guy.set_attitude( NPCATT_NULL );
    REQUIRE( !guy.is_player_ally() );
    REQUIRE( !guy.is_enemy() );
    // A stranger minds anything over 24, a friend anything over 60.
    CHECK( !grapple::throw_angers( you, guy, 24.0f ) );
    CHECK( grapple::throw_angers( you, guy, 25.0f ) );
    make_follower( guy );
    CHECK( !grapple::throw_angers( you, guy, 60.0f ) );
    CHECK( grapple::throw_angers( you, guy, 61.0f ) );
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::west );
    CHECK( !grapple::throw_angers( you, zed, 99.0f ) );
}

TEST_CASE( "grapple_throw_refusals", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    monster &mon = spawn_held( you, "mon_test_speed_desc_base" );

    SECTION( "too weak" ) {
        // A fresh survivor's throw wouldn't carry past the next tile.
        REQUIRE( grapple::throw_range( grapple::throw_force( you, mon ) ) <= 1 );
        const std::optional<std::string> refusal = grapple::throw_refusal( you, mon );
        REQUIRE( refusal );
        CHECK( *refusal == "You can't muster the strength to throw the speed 100 monster." );
    }
    SECTION( "too tired" ) {
        make_thrower( you );
        you.set_stamina( 100 );
        const std::optional<std::string> refusal = grapple::throw_refusal( you, mon );
        REQUIRE( refusal );
        CHECK( *refusal == "You're too exhausted to throw the speed 100 monster." );
    }
    SECTION( "not through yourself, nor nowhere" ) {
        make_thrower( you );
        CHECK( !grapple::throw_refusal( you, mon ) );
        CHECK( grapple::throw_target_refusal( you, mon, you.pos_bub() + tripoint::west ) );
        CHECK( grapple::throw_target_refusal( you, mon, mon.pos_bub() ) );
        CHECK( !grapple::throw_target_refusal( you, mon, mon.pos_bub() + tripoint::east ) );
        CHECK( !grapple::throw_held_creature( you, mon, you.pos_bub() + tripoint::west ) );
        CHECK( holds( you, mon ) );
    }
}

TEST_CASE( "grapple_throw_lands_where_it_is_aimed", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    make_thrower( you );
    monster &mon = spawn_held( you, "mon_test_speed_desc_base" );
    const tripoint_bub_ms start = mon.pos_bub();
    const float full_force = grapple::throw_force( you, mon );
    REQUIRE( grapple::throw_range( full_force ) == 3 );
    // East (away from us), south, and diagonally: a fling counts diagonal steps as one tile.
    const tripoint_rel_ms aim = GENERATE( tripoint_rel_ms( 1, 0, 0 ), tripoint_rel_ms( 2, 0, 0 ),
                                          tripoint_rel_ms( 3, 0, 0 ), tripoint_rel_ms( 0, 3, 0 ),
                                          tripoint_rel_ms( 2, 2, 0 ), tripoint_rel_ms( 3, -3, 0 ) );
    CAPTURE( aim );
    const int distance = square_dist( aim.xy(), point_rel_ms::zero );
    const tripoint_bub_ms target = start + aim;
    const float force = grapple::throw_force_at( full_force, distance );
    const int stamina_cost = grapple::throw_stamina_cost( you, mon, force );
    const int move_cost = grapple::throw_move_cost( you );
    const int moves = you.get_moves();
    const int stamina = you.get_stamina();

    REQUIRE( grapple::throw_held_creature( you, mon, target ) );
    CHECK( mon.pos_bub() == target );
    CHECK( released( you, mon ) );
    CHECK( !mon.has_effect( effect_grabbed ) );
    CHECK( you.get_moves() == moves - move_cost );
    CHECK( you.get_stamina() == stamina - stamina_cost );
}

TEST_CASE( "grapple_thrown_creature_lands_on_traps_and_off_ledges", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    map &here = get_map();
    make_thrower( you );
    monster &mon = spawn_held( you, "mon_test_speed_desc_base" );
    const tripoint_bub_ms start = mon.pos_bub();
    const tripoint_bub_ms landing = start + tripoint( 3, 0, 0 );

    SECTION( "the trap where it lands goes off, not the one it started on" ) {
        here.trap_set( start, tr_beartrap.id() );
        here.trap_set( start + tripoint::east, tr_beartrap.id() );
        here.trap_set( landing, tr_beartrap.id() );
        REQUIRE( grapple::throw_held_creature( you, mon, landing ) );
        CHECK( mon.pos_bub() == landing );
        CHECK( mon.has_effect( effect_beartrap ) );
        CHECK( here.tr_at( landing ).is_null() );
        CHECK( here.tr_at( start ).id == tr_beartrap );
        CHECK( here.tr_at( start + tripoint::east ).id == tr_beartrap );
    }
    SECTION( "thrown off a ledge, it drops a level" ) {
        for( int x = 2; x <= 3; ++x ) {
            here.ter_set( start + tripoint( x, 0, 0 ), ter_t_open_air.id() );
            here.ter_set( start + tripoint( x, 0, -1 ), ter_t_floor.id() );
        }
        here.invalidate_map_cache( 0 );
        here.build_map_cache( 0, true );
        here.build_map_cache( -1, true );
        REQUIRE( grapple::throw_held_creature( you, mon, landing ) );
        CHECK( mon.pos_bub() == landing + tripoint::below );
    }
    CHECK( released( you, mon ) );
    CHECK( !mon.has_effect( effect_airborne ) );
}

TEST_CASE( "grapple_a_thrown_stranger_gets_angry", "[grapple]" )
{
    avatar &you = setup_grapple_scene();
    override_option no_animations( "ANIMATIONS", "false" );
    make_thrower( you );
    npc &guy = spawn_npc( you.pos_bub().xy() + point::east, "test_talker" );
    guy.set_attitude( NPCATT_NULL );
    REQUIRE( !guy.is_enemy() );
    REQUIRE( !guy.guaranteed_hostile() );
    // Angering the NPC sours its whole faction on the avatar, which outlives this test.
    faction *const fac = guy.get_faction();
    REQUIRE( fac != nullptr );
    const int likes_u = fac->likes_u;
    const int respects_u = fac->respects_u;
    const int trusts_u = fac->trusts_u;
    const on_out_of_scope restore_faction( [fac, likes_u, respects_u, trusts_u]() {
        fac->likes_u = likes_u;
        fac->respects_u = respects_u;
        fac->trusts_u = trusts_u;
    } );
    REQUIRE( grapple::start_hold( you, guy, 20, body_part_torso.id() ) );
    const tripoint_bub_ms target = guy.pos_bub() + tripoint( 3, 0, 0 );
    const float force = grapple::throw_force_at( grapple::throw_force( you, guy ), 3 );
    REQUIRE( grapple::throw_angers( you, guy, force ) );
    REQUIRE( grapple::throw_held_creature( you, guy, target ) );
    CHECK( released( you, guy ) );
    // npc::on_attacked: angry, or too scared to fight back.
    CHECK( guy.hit_by_player );
    CHECK( ( guy.get_attitude() == NPCATT_KILL || guy.get_attitude() == NPCATT_FLEE_TEMP ) );
}

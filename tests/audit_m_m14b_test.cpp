#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_variant.h"
#include "character.h"
#include "climbing.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "move_mode.h"
#include "mutation.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "projectile.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const climbing_aid_id climbing_aid_furn_CLIMBABLE( "furn_CLIMBABLE" );
static const climbing_aid_id climbing_aid_test_m14b_up_harness( "test_m14b_up_harness" );
static const climbing_aid_id climbing_aid_vehicle( "vehicle" );

static const damage_type_id damage_cut( "cut" );
static const damage_type_id damage_stab( "stab" );

static const efftype_id effect_blind( "blind" );
static const efftype_id effect_stunned( "stunned" );

static const itype_id itype_tshirt( "tshirt" );

static const move_mode_id move_mode_crouch( "crouch" );
static const move_mode_id move_mode_test_m14b_glide( "test_m14b_glide" );
static const move_mode_id move_mode_walk( "walk" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_open_air( "t_open_air" );

static const trait_id trait_CF_HAIR( "CF_HAIR" );
static const trait_id trait_QUILLS( "QUILLS" );
static const trait_id trait_SPINES( "SPINES" );
static const trait_id trait_TEST_M14B_CLIMBER( "TEST_M14B_CLIMBER" );
static const trait_id trait_TEST_M14B_GLIDER( "TEST_M14B_GLIDER" );
static const trait_id trait_TEST_M14B_HIT_ME( "TEST_M14B_HIT_ME" );
static const trait_id trait_TEST_M14B_NIMBLE( "TEST_M14B_NIMBLE" );
static const trait_id trait_THORNS( "THORNS" );

namespace
{
talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    talk_effect_t effect( jo, "effect", "audit_m_m14b_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

void run_effect( Character &who, const std::string &json )
{
    const talk_effect_t effect = load_effect( json );
    dialogue d( get_talker_for( who ), nullptr );
    effect.apply( d );
}

// The value of a math expression for the character, through a global variable
double math_value( Character &who, const std::string &expression )
{
    run_effect( who, R"({ "effect": { "math": [ "audit_m14b_result = )" + expression + R"(" ] } })" );
    const diag_value *value = get_globals().maybe_get_global_value( "audit_m14b_result" );
    return value == nullptr ? -12345.0 : value->dbl();
}

double num_var( const Creature &c, const std::string &key )
{
    const diag_value *val = c.maybe_get_value( key );
    return val == nullptr ? 0.0 : val->dbl();
}

std::string str_var( const Creature &c, const std::string &key )
{
    const diag_value *val = c.maybe_get_value( key );
    return val == nullptr || !val->is_str() ? std::string() : val->str();
}

// Keeps the events of one type sent while it lives
struct event_log : event_subscriber {
    using event_subscriber::notify;
    explicit event_log( event_type type ) : wanted( type ) {
        get_event_bus().subscribe( this );
    }
    void notify( const cata::event &e ) override {
        if( e.type() == wanted ) {
            seen.push_back( e );
        }
    }
    event_type wanted;
    std::vector<cata::event> seen;
};

void clear_scene()
{
    calendar::turn = calendar::turn_zero + 9_hours;
    clear_vehicles();
    clear_map_without_vision( -2, 1 );
    clear_creatures();
    clear_npcs();
    clear_avatar();
    get_globals().clear_global_values();
}
} // namespace

// CHAR-J07: a movement mode's JSON condition gates switching to it, its switch_cost adds to the
// moves a switch takes, and u_set_move_mode switches as choosing the mode would.
TEST_CASE( "char-j07_move_mode_condition_switch_cost_and_eoc_setter", "[audit][character]" )
{
    clear_scene();
    avatar &u = get_avatar();
    u.set_movement_mode( move_mode_walk );
    REQUIRE( u.current_movement_mode() == move_mode_walk );
    REQUIRE( move_mode_test_m14b_glide.is_valid() );

    SECTION( "the condition gates the switch" ) {
        CHECK_FALSE( u.can_switch_to( move_mode_test_m14b_glide ) );
        u.set_mutation( trait_TEST_M14B_GLIDER );
        CHECK( u.can_switch_to( move_mode_test_m14b_glide ) );
        // the built-in modes keep their rules
        CHECK( u.can_switch_to( move_mode_crouch ) );
    }
    SECTION( "switch_cost adds to the posture change" ) {
        // walking to a walking mode changes no posture
        CHECK( u.move_mode_switch_cost( move_mode_walk, move_mode_test_m14b_glide ) == 40 );
        CHECK( u.move_mode_switch_cost( move_mode_test_m14b_glide, move_mode_walk ) == 0 );
        CHECK( u.move_mode_switch_cost( move_mode_walk, move_mode_crouch ) == 50 );
    }
    SECTION( "MOVE_MODE_SWITCH_COST changes the cost" ) {
        u.set_mutation( trait_TEST_M14B_NIMBLE );
        u.recalculate_enchantment_cache();
        CHECK( u.move_mode_switch_cost( move_mode_walk, move_mode_crouch ) == 25 );
        u.unset_mutation( trait_TEST_M14B_NIMBLE );
        u.recalculate_enchantment_cache();
        CHECK( u.move_mode_switch_cost( move_mode_walk, move_mode_crouch ) == 50 );
    }
    SECTION( "u_set_move_mode obeys the condition, charges the moves and sends the event" ) {
        event_log changes( event_type::character_changes_move_mode );
        u.set_moves( 100 );
        run_effect( u, R"({ "effect": { "u_set_move_mode": "test_m14b_glide" } })" );
        CHECK( u.current_movement_mode() == move_mode_walk );
        CHECK( u.get_moves() == 100 );
        CHECK( changes.seen.empty() );

        u.set_mutation( trait_TEST_M14B_GLIDER );
        run_effect( u, R"({ "effect": { "u_set_move_mode": "test_m14b_glide" } })" );
        CHECK( u.current_movement_mode() == move_mode_test_m14b_glide );
        CHECK( u.get_moves() == 60 );
        REQUIRE( changes.seen.size() == 1 );
        CHECK( changes.seen[0].get<cata_variant_type::character_id>( "character" ) == u.getID() );
        CHECK( changes.seen[0].get<cata_variant_type::move_mode_id>( "old_mode" ) == move_mode_walk );
        CHECK( changes.seen[0].get<cata_variant_type::move_mode_id>( "new_mode" ) ==
               move_mode_test_m14b_glide );

        // already in it: nothing happens
        run_effect( u, R"({ "effect": { "u_set_move_mode": "test_m14b_glide" } })" );
        CHECK( u.get_moves() == 60 );
        CHECK( changes.seen.size() == 1 );
    }
    u.set_movement_mode( move_mode_walk );
    clear_scene();
}

// CHAR-J08: SPINES, QUILLS, THORNS and CF_HAIR are a mutation's "retaliation" now, with the
// numbers the C++ had.
TEST_CASE( "char-j08_retaliation_traits_are_data", "[audit][character]" )
{
    REQUIRE( trait_SPINES->retaliation.size() == 1 );
    const mutation_retaliation &spines = trait_SPINES->retaliation.front();
    CHECK( spines.damage_type == damage_stab );
    CHECK( spines.min_damage == 1 );
    CHECK( spines.max_damage == 8 );
    CHECK( spines.requires_uncovered );
    CHECK_FALSE( spines.unarmed_attacker_only );

    REQUIRE( trait_QUILLS->retaliation.size() == 1 );
    CHECK( trait_QUILLS->retaliation.front().damage_type == damage_stab );
    CHECK( trait_QUILLS->retaliation.front().max_damage == 20 );

    REQUIRE( trait_THORNS->retaliation.size() == 1 );
    const mutation_retaliation &thorns = trait_THORNS->retaliation.front();
    CHECK( thorns.damage_type == damage_cut );
    CHECK( thorns.min_damage == 1 );
    CHECK( thorns.max_damage == 4 );
    CHECK( thorns.unarmed_attacker_only );

    REQUIRE( trait_CF_HAIR->retaliation.size() == 1 );
    const mutation_retaliation &hair = trait_CF_HAIR->retaliation.front();
    CHECK( hair.max_damage == 0 );
    REQUIRE( hair.effects.size() == 2 );
    CHECK( hair.effects[0].effect == effect_stunned );
    CHECK( hair.effects[0].duration == 2_turns );
    CHECK( hair.effects[0].one_in == 1 );
    CHECK( hair.effects[1].effect == effect_blind );
    CHECK( hair.effects[1].one_in == 3 );
}

// CHAR-J08: the retaliation goes off on a melee hit to an uncovered part only.
TEST_CASE( "char-j08_retaliation_needs_an_uncovered_part", "[audit][character]" )
{
    clear_scene();
    avatar &u = get_avatar();
    map &here = get_map();
    u.set_mutation( trait_CF_HAIR );
    monster &attacker = spawn_test_monster( "mon_test_cmbt_post", u.pos_bub() + tripoint_rel_ms::east );

    SECTION( "bare skin" ) {
        u.on_hit( &here, &attacker, body_part_torso.id(), 0.0f, nullptr );
        CHECK( attacker.has_effect( effect_stunned ) );
    }
    SECTION( "covered" ) {
        REQUIRE( u.wear_item( item( itype_tshirt ), false ).has_value() );
        REQUIRE( u.wearing_something_on( body_part_torso.id() ) );
        u.on_hit( &here, &attacker, body_part_torso.id(), 0.0f, nullptr );
        CHECK_FALSE( attacker.has_effect( effect_stunned ) );
    }
    SECTION( "a ranged hit" ) {
        const dealt_projectile_attack shot{};
        u.on_hit( &here, &attacker, body_part_torso.id(), 0.0f, &shot );
        CHECK_FALSE( attacker.has_effect( effect_stunned ) );
    }
    clear_scene();
}

// CHAR-J08: a character's hit_me_effect skipped every ranged hit.  One with hit_types naming
// "ranged" goes off on them now; one without hit_types still only on melee hits.  Their EOCs
// get the kind of hit and the part hit.
TEST_CASE( "char-j08_hit_me_effect_on_ranged_hits_with_hit_types", "[audit][character][magic]" )
{
    clear_scene();
    avatar &u = get_avatar();
    map &here = get_map();
    u.set_mutation( trait_TEST_M14B_HIT_ME );
    u.recalculate_enchantment_cache();
    monster &attacker = spawn_test_monster( "mon_test_cmbt_post", u.pos_bub() + tripoint_rel_ms::east );

    SECTION( "a ranged hit" ) {
        const dealt_projectile_attack shot{};
        u.on_hit( &here, &attacker, body_part_arm_l.id(), 0.0f, &shot );
        CHECK( num_var( u, "test_m14b_ranged_hits" ) == 1 );
        CHECK( str_var( u, "test_m14b_ranged_type" ) == "ranged" );
        CHECK( num_var( u, "test_m14b_any_hits" ) == 0 );
    }
    SECTION( "a melee hit" ) {
        u.on_hit( &here, &attacker, body_part_arm_l.id(), 0.0f, nullptr );
        CHECK( num_var( u, "test_m14b_ranged_hits" ) == 0 );
        CHECK( num_var( u, "test_m14b_any_hits" ) == 1 );
        CHECK( str_var( u, "test_m14b_any_type" ) == "melee" );
        CHECK( str_var( u, "test_m14b_any_bodypart" ) == "arm_l" );
    }
    clear_scene();
}

// CHAR-J09: what effects did to blood pressure, heart rate and breathing is saved, the
// respiration rate is kept current, and math reads all three.
TEST_CASE( "char-j09_cardio_values_saved_updated_and_readable", "[audit][character][health]" )
{
    clear_scene();
    avatar &u = get_avatar();

    SECTION( "the effect mods survive a save" ) {
        u.set_bp_effect_mod( 300 );
        u.set_heartrate_effect_mod( -200 );
        u.set_respiration_effect_mod( 700 );
        std::ostringstream saved;
        JsonOut out( saved );
        u.serialize( out );
        std::unique_ptr<avatar> loaded = std::make_unique<avatar>();
        loaded->deserialize( json_loader::from_string( saved.str() ) );
        CHECK( loaded->get_bp_effect_mod() == 300 );
        CHECK( loaded->get_heartrate_effect_mod() == -200 );
        CHECK( loaded->get_respiration_effect_mod() == 700 );
    }
    SECTION( "the respiration rate follows its effect mod once a minute" ) {
        u.set_respiration_effect_mod( 5000 );
        calendar::turn = calendar::turn_zero + 10_hours;
        u.update_body( calendar::turn - 1_turns, calendar::turn );
        CHECK( u.get_respiration_rate() > 1.0f );
        CHECK( u.get_respiration_rate() ==
               Approx( 1.0f + u.get_respiration_effect_mod() * 0.0001f ).margin( 0.0001 ) );
    }
    SECTION( "math reads them" ) {
        u.avg_nat_bpm = 80;
        CHECK( u.heartrate_bpm_average() == static_cast<int>( 80 * u.get_heartrate_index() ) );
        CHECK( math_value( u, "u_heart_rate()" ) == u.heartrate_bpm_average() );
        CHECK( math_value( u, "u_blood_pressure()" ) == Approx( u.get_circulation() ) );
        CHECK( math_value( u, "u_respiration_rate()" ) == Approx( u.get_respiration_rate() ) );
    }
    u.set_bp_effect_mod( 0 );
    u.set_heartrate_effect_mod( 0 );
    u.set_respiration_effect_mod( 0 );
    clear_scene();
}

// CHAR-J10: math reads and writes a part's wetness, temperature and frostbite timer, and
// u_add_wet can wet named parts at once.
TEST_CASE( "char-j10_body_part_state_from_eocs", "[audit][character]" )
{
    clear_scene();
    avatar &u = get_avatar();

    SECTION( "wetness" ) {
        run_effect( u, R"({ "effect": { "math": [ "u_wetness('arm_l') = 50" ] } })" );
        CHECK( u.get_part_wetness_percentage( body_part_arm_l ) == Approx( 0.5 ).margin( 0.01 ) );
        CHECK( math_value( u, "u_wetness('arm_l')" ) == Approx( 50 ).margin( 1 ) );
        run_effect( u, R"({ "effect": { "math": [ "u_wetness('arm_l') = 150" ] } })" );
        CHECK( u.get_part_wetness( body_part_arm_l ) == u.get_part_drench_capacity( body_part_arm_l ) );
        run_effect( u, R"({ "effect": { "math": [ "u_wetness('arm_l') = 0" ] } })" );
        CHECK( u.get_part_wetness( body_part_arm_l ) == 0 );
    }
    SECTION( "temperature" ) {
        run_effect( u, R"({ "effect": { "math": [ "u_body_temp('torso') = 30" ] } })" );
        CHECK( units::to_celsius( u.get_part_temp_cur( body_part_torso ) ) == Approx( 30 ).margin( 0.01 ) );
        CHECK( math_value( u, "u_body_temp('torso')" ) == Approx( 30 ).margin( 0.01 ) );
    }
    SECTION( "frostbite" ) {
        run_effect( u, R"({ "effect": { "math": [ "u_frostbite('hand_r') = 120" ] } })" );
        CHECK( u.get_part_frostbite_timer( body_part_hand_r ) == 120 );
        CHECK( math_value( u, "u_frostbite('hand_r')" ) == 120 );
        run_effect( u, R"({ "effect": { "math": [ "u_frostbite('hand_r') = -5" ] } })" );
        CHECK( u.get_part_frostbite_timer( body_part_hand_r ) == 0 );
    }
    SECTION( "u_add_wet with bodypart wets only those parts, at once" ) {
        run_effect( u, R"({ "effect": { "u_add_wet": 50, "bodypart": [ "leg_l", "leg_r" ] } })" );
        CHECK( u.get_part_wetness_percentage( body_part_leg_l ) == Approx( 0.5 ).margin( 0.01 ) );
        CHECK( u.get_part_wetness_percentage( body_part_leg_r ) == Approx( 0.5 ).margin( 0.01 ) );
        CHECK( u.get_part_wetness( body_part_torso ) == 0 );
        CHECK( u.get_part_wetness( body_part_arm_l ) == 0 );
    }
    clear_scene();
}

// CHAR-J10: falling and NPC steps send events now.
TEST_CASE( "char-j10_fall_and_step_events", "[audit][character][event]" )
{
    clear_scene();
    map &here = get_map();
    const tripoint_bub_ms ground( 40, 40, 0 );
    const tripoint_bub_ms above( 40, 40, 1 );
    here.ter_set( ground, ter_t_floor );
    here.ter_set( above, ter_t_open_air );
    npc &guy = spawn_npc( point_bub_ms( 41, 40 ), "thug" );
    clear_character( guy );

    SECTION( "an NPC falls a story" ) {
        event_log falls( event_type::character_falls );
        guy.setpos( here, above, false );
        guy.gravity_check( &here );
        CHECK( guy.pos_bub( here ) == ground );
        REQUIRE( falls.seen.size() == 1 );
        CHECK( falls.seen[0].get<cata_variant_type::character_id>( "character" ) == guy.getID() );
        CHECK( falls.seen[0].get<cata_variant_type::int_>( "height" ) == 1 );
    }
    SECTION( "an NPC steps" ) {
        event_log steps( event_type::character_moves );
        guy.set_moves( 100 );
        const tripoint_bub_ms dest = guy.pos_bub( here ) + tripoint_rel_ms::north;
        here.ter_set( dest, ter_t_floor );
        guy.move_to( dest );
        REQUIRE( guy.pos_bub( here ) == dest );
        REQUIRE( steps.seen.size() == 1 );
        CHECK( steps.seen[0].get<cata_variant_type::character_id>( "character" ) == guy.getID() );
        CHECK( steps.seen[0].get<cata_variant_type::int_>( "z" ) == 0 );
    }
    clear_scene();
}

// CHAR-J11: a "down" with max_height 0 is disabled, as CLIMBING.md says; an aid can be for
// climbing up only; climbing up reads furn_CLIMBABLE's "up" block; a veh aid with an empty flag
// is any vehicle.
TEST_CASE( "char-j11_climbing_aid_up_blocks_and_disabled_down", "[audit][character]" )
{
    const climbing_aid &harness = climbing_aid_test_m14b_up_harness.obj();
    CHECK_FALSE( harness.down.enabled() );
    REQUIRE( harness.up.enabled() );
    CHECK( harness.up.move_cost == 300 );
    CHECK_FALSE( harness.up.needs_support );
    CHECK( harness.up.cost.kcal == 20 );

    // the move cost the C++ had: climbing cost + 500, or 1000 with nothing to brace against
    REQUIRE( climbing_aid_furn_CLIMBABLE->up.enabled() );
    CHECK( climbing_aid_furn_CLIMBABLE->up.move_cost == 500 );
    CHECK( climbing_aid_furn_CLIMBABLE->up.needs_support );
    CHECK( climbing_aid_vehicle->base_condition.flag.empty() );

    clear_scene();
    avatar &u = get_avatar();
    const tripoint_bub_ms above = u.pos_bub() + tripoint_rel_ms::above;
    get_map().ter_set( above, ter_t_open_air );

    SECTION( "without the trait no up aid is found" ) {
        const climbing_aid::condition_list conditions = climbing_aid::detect_conditions( u, above );
        CHECK( climbing_aid::get_safest_up( conditions ) == nullptr );
    }
    SECTION( "with it, the up-only aid is found for climbing up and never offered for down" ) {
        u.set_mutation( trait_TEST_M14B_CLIMBER );
        const climbing_aid::condition_list conditions = climbing_aid::detect_conditions( u, above );
        CHECK( climbing_aid::get_safest_up( conditions ) == &harness );
        CHECK( &climbing_aid::get_safest( conditions ) != &harness );
        for( const climbing_aid *aid : climbing_aid::list( conditions ) ) {
            CHECK( aid != &harness );
        }
        for( const climbing_aid *aid : climbing_aid::list_all( conditions ) ) {
            CHECK( aid != &harness );
        }
    }
    SECTION( "a down block with max_height 0 needs nothing else" ) {
        climbing_aid::down_t down;
        const JsonObject jo = json_loader::from_string( R"({ "max_height": 0 })" );
        down.deserialize( jo );
        CHECK_FALSE( down.enabled() );
    }
    clear_scene();
}

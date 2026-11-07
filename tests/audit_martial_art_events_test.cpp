#include <memory>
#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "explosion.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "projectile.h"
#include "ret_val.h"
#include "type_id.h"

static const damage_type_id damage_bash( "bash" );

static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_winded( "winded" );

static const matec_id test_tech_cmbt_j01_post_hit( "test_tech_cmbt_j01_post_hit" );

static const matype_id test_style_cmbt_j01( "test_style_cmbt_j01" );

static void use_event_style( Character &you )
{
    you.martial_arts_data->add_martialart( test_style_cmbt_j01 );
    you.martial_arts_data->set_style( test_style_cmbt_j01, false );
    REQUIRE( you.martial_arts_data->selected_style() == test_style_cmbt_j01 );
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

// The defensive martial-art EOCs used to run without the attacker, and ongethit was skipped
// entirely while winded, asleep or driving
TEST_CASE( "cmbt-j01_ongethit_and_ondodge_eocs_see_the_attacker", "[audit][martial_arts]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    use_event_style( you );
    monster &attacker = spawn_test_monster( "mon_test_non_shearable",
                                            you.pos_bub() + tripoint::east );

    SECTION( "ongethit runs while winded, with the attacker as npc_ and the hit as context" ) {
        you.add_effect( effect_winded, 10_turns );
        bodypart_id bp_hit = body_part_arm_l;
        damage_instance dam( damage_bash, 7.0f );
        // Too winded to block, but the hit still lands
        CHECK_FALSE( you.block_hit( &attacker, bp_hit, dam ) );
        CHECK( num_var( attacker, "cmbt_j01_hit_them" ) == 1 );
        CHECK( num_var( you, "cmbt_j01_gethit_damage" ) == Approx( 7.0 ) );
        CHECK( str_var( you, "cmbt_j01_gethit_bp" ) == "arm_l" );
    }

    SECTION( "ondodge runs with the attacker as npc_" ) {
        REQUIRE( you.can_try_dodge( true ).success() );
        you.on_dodge( &attacker, 5.0f );
        CHECK( num_var( attacker, "cmbt_j01_dodged" ) == 1 );
    }
}

// onhit and onkill used to run without the target, and techniques had no hook that runs after
// the hit is dealt
TEST_CASE( "cmbt-j01_onhit_onkill_and_post_hit_eocs_see_the_target_and_damage",
           "[audit][martial_arts]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    use_event_style( you );
    monster &target = spawn_test_monster( "mon_test_non_shearable",
                                          you.pos_bub() + tripoint::east );
    // The first hit that deals damage kills it
    target.set_hp( 1 );

    for( int i = 0; i < 200 && !target.is_dead_state(); ++i ) {
        you.set_moves( 100 );
        you.set_stamina( you.get_stamina_max() );
        you.melee_attack_abstract( target, false, test_tech_cmbt_j01_post_hit );
    }
    REQUIRE( target.is_dead_state() );

    // onhit ran after the killing hit, with the target as npc_ and the hit as context
    CHECK( num_var( target, "cmbt_j01_was_hit" ) == 1 );
    CHECK( num_var( you, "cmbt_j01_onhit_damage" ) > 0 );
    CHECK( str_var( you, "cmbt_j01_onhit_technique" ) == test_tech_cmbt_j01_post_hit.str() );
    // onkill ran with the victim as npc_
    CHECK( num_var( target, "cmbt_j01_killed_by_style" ) == 1 );
    // The technique's post_hit_eocs saw the damage dealt and the kill
    CHECK( num_var( target, "cmbt_j01_post_hit_damage" ) > 0 );
    CHECK( num_var( you, "cmbt_j01_post_hit_killed" ) == 1 );
    // The technique's own eocs, before the hit, got the attack's context too
    CHECK( str_var( you, "cmbt_j01_pre_hit_technique" ) == test_tech_cmbt_j01_post_hit.str() );
    CHECK( str_var( you, "cmbt_j01_pre_hit_style" ) == test_style_cmbt_j01.str() );
    CHECK_FALSE( str_var( you, "cmbt_j01_pre_hit_bp" ).empty() );
    CHECK( num_var( you, "cmbt_j01_pre_hit_damage" ) > 0 );
}

// CMBT-J01: ongethit only ran for melee hits; a bullet, a fragment or a blast that hit you ran
// nothing
TEST_CASE( "cmbt-j01_ongethit_eocs_run_for_projectiles_and_explosions", "[audit][martial_arts]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    use_event_style( you );
    you.set_all_parts_hp_cur( 10000 );
    // asleep, so the shot can't be dodged
    you.add_effect( effect_sleep, 1_hours );
    monster &attacker = spawn_test_monster( "mon_test_non_shearable",
                                            you.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );

    SECTION( "a projectile, with the shooter as npc_" ) {
        dealt_projectile_attack shot{};
        shot.proj.speed = 1000;
        shot.proj.range = 60;
        shot.proj.impact = damage_instance( damage_bash, 5.0f );
        you.deal_projectile_attack( &get_map(), &attacker, shot, 0, false );
        CHECK( str_var( you, "cmbt_j01_gethit_by" ) == "projectile" );
        CHECK( num_var( you, "cmbt_j01_gethits" ) == 1 );
        CHECK( num_var( attacker, "cmbt_j01_hit_them" ) == 1 );
        CHECK( num_var( you, "cmbt_j01_gethit_damage" ) > 0 );
        CHECK_FALSE( str_var( you, "cmbt_j01_gethit_bp" ).empty() );
    }

    SECTION( "an explosion, once" ) {
        explosion_handler::explosion( &attacker, you.pos_bub() + tripoint_rel_ms::east, 5.0f, 0.8f,
                                      false, 0 );
        explosion_handler::process_explosions();
        CHECK( str_var( you, "cmbt_j01_gethit_by" ) == "explosion" );
        CHECK( num_var( you, "cmbt_j01_gethits" ) == 1 );
    }
}

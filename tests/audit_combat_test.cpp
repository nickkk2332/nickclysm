#include <map>
#include <string>
#include <utility>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "faction.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "martialarts.h"
#include "math_parser_diag_value.h"
#include "melee.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "veh_type.h"

// Regression tests for the combat chapter of the engine audit (Audit/18-combat.md).

static const itype_id itype_test_cmbt_club( "test_cmbt_club" );

static const mabuff_id test_buff_cmbt_stacking( "test_buff_cmbt_stacking" );

static const move_mode_id move_mode_prone( "prone" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_unarmed( "unarmed" );

static const trait_id trait_TEST_CMBT_PSEUDOPOD( "TEST_CMBT_PSEUDOPOD" );

static const vproto_id vehicle_prototype_bicycle( "bicycle" );

namespace
{
// Clears the scene before a test and again after it, so nothing these tests spawn or change
// (creatures, NPCs, vehicles, items, the avatar, the melee statistics, the message log) leaks
// into later tests. Factions are put back too: the avatar hitting an NPC sours its whole
// faction, which would turn every later NPC of that faction hostile. So is the count of the
// avatar's fights, which an NPC's practice on it raises and clear_avatar() keeps.
struct combat_scene {
    struct opinion {
        int likes_u;
        int respects_u;
        int trusts_u;
    };
    std::map<faction_id, opinion> opinions;
    short avatar_fights = get_avatar().times_combatted_player;

    combat_scene() {
        for( const std::pair<const faction_id, faction> &fac : g->faction_manager_ptr->all() ) {
            opinions[fac.first] = { fac.second.likes_u, fac.second.respects_u, fac.second.trusts_u };
        }
        reset();
    }
    ~combat_scene() {
        reset();
        get_avatar().times_combatted_player = avatar_fights;
        for( const std::pair<const faction_id, opinion> &op : opinions ) {
            faction *const fac = g->faction_manager_ptr->get( op.first, false );
            if( fac != nullptr ) {
                fac->likes_u = op.second.likes_u;
                fac->respects_u = op.second.respects_u;
                fac->trusts_u = op.second.trusts_u;
            }
        }
    }
    static void reset() {
        clear_vehicles();
        clear_map_without_vision();
        clear_creatures();
        clear_npcs();
        clear_avatar();
        melee::clear_stats();
        Messages::clear_messages();
    }
};
} // namespace

// Steady hands: blows land on a creature that doesn't dodge.
static void make_fighter( Character &who )
{
    who.set_str_base( 12 );
    who.set_dex_base( 20 );
    who.set_skill_level( skill_melee, 10 );
    who.set_skill_level( skill_unarmed, 10 );
    who.set_stamina( who.get_stamina_max() );
    who.set_moves( 100 );
}

static void ready( Character &who, Creature &target )
{
    who.set_moves( 100 );
    who.set_stamina( who.get_stamina_max() );
    if( Character *const c = target.as_character() ) {
        // No dodging: the blow lands or misses on the attacker's roll alone
        c->set_dodges_left( 0 );
    }
}

// CMBT-B06: the vehicle's speed, in hundredths of a mph, came off the hit roll as is, so any
// swing from a moving vehicle missed. It now costs 1 accuracy per mph.
TEST_CASE( "cmbt-b06_moving_vehicle_melee_penalty_in_mph", "[audit][melee]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    map &here = get_map();
    vehicle *bike = here.add_vehicle( vehicle_prototype_bicycle, you.pos_bub( here ), 0_degrees,
                                      0, veh_spawn_status::UNDAMAGED );
    REQUIRE( bike != nullptr );
    REQUIRE( here.veh_at( you.pos_bub( here ) ) );

    const auto mean_hit_roll = [&you]() {
        constexpr int trials = 2000;
        double sum = 0.0;
        for( int i = 0; i < trials; ++i ) {
            sum += you.hit_roll();
        }
        return sum / trials;
    };
    bike->velocity = 0;
    const double still = mean_hit_roll();
    // 5 mph
    bike->velocity = 500;
    REQUIRE( bike->forward_velocity() == Approx( 500.0f ) );
    const double moving = mean_hit_roll();
    bike->velocity = 0;
    CAPTURE( still, moving );
    // 5 accuracy, 5 points of hit roll each
    CHECK( still - moving == Approx( 25.0 ).margin( 8.0 ) );
}

// Damage per landed blow on a sturdy post, lying prone
static double prone_damage_per_hit( avatar &you, monster &post )
{
    constexpr int attacks = 300;
    int total = 0;
    int hits = 0;
    for( int i = 0; i < attacks; ++i ) {
        ready( you, post );
        const int before = post.get_hp();
        you.melee_attack( post, false );
        const int dealt = before - post.get_hp();
        if( dealt > 0 ) {
            total += dealt;
            ++hits;
        }
        post.set_hp( post.get_hp_max() );
    }
    REQUIRE( hits > attacks / 2 );
    return static_cast<double>( total ) / hits;
}

// CMBT-B07: a missing else gave prone PSEUDOPOD_GRASP attackers both multipliers, 0.8 then 0.3,
// so they hit for 0.24x, less than anyone else on the ground (0.3x). They now get 0.8x.
TEST_CASE( "cmbt-b07_prone_pseudopod_damage_multiplier", "[audit][melee]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    make_fighter( you );
    you.set_str_base( 20 );
    // Unbreakable: worn down, a club does less damage, and the second run would swing a worse one
    item club( itype_test_cmbt_club );
    REQUIRE( you.wield( club ) );
    you.set_movement_mode( move_mode_prone );
    REQUIRE( you.is_on_ground() );
    monster &post = spawn_test_monster( "mon_test_cmbt_post", you.pos_bub() + tripoint::east );
    // No practice: the attacker's skill stays the same throughout
    post.times_combatted_player = 1000;

    const double plain = prone_damage_per_hit( you, post );
    you.set_mutation( trait_TEST_CMBT_PSEUDOPOD );
    you.set_movement_mode( move_mode_prone );
    REQUIRE( you.is_on_ground() );
    const double pseudopod = prone_damage_per_hit( you, post );
    CAPTURE( plain, pseudopod );
    // 0.8 against 0.3
    CHECK( pseudopod > 2.0 * plain );
}

// CMBT-B16: the accuracy bonus of a martial-art buff ignored its stacks, and every buff bonus was
// cut to a whole number before the stacks were counted.
TEST_CASE( "cmbt-b16_martial_art_buff_bonuses_stack_with_fractions", "[audit][martial_arts]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    const ma_buff &buff = test_buff_cmbt_stacking.obj();

    SECTION( "one stack" ) {
        buff.apply_buff( you );
        REQUIRE( you.get_effect_int( buff.get_effect_id() ) == 1 );
        CHECK( you.mabuff_tohit_bonus() == Approx( 1.5f ) );
        CHECK( you.mabuff_dodge_bonus() == Approx( 0.8f ) );
        // Whole-number bonuses drop the fraction once, after the stacks: 0.6 is 0
        CHECK( you.mabuff_block_bonus() == 0 );
    }
    SECTION( "three stacks" ) {
        for( int i = 0; i < 3; ++i ) {
            buff.apply_buff( you );
        }
        REQUIRE( you.get_effect_int( buff.get_effect_id() ) == 3 );
        CHECK( you.mabuff_tohit_bonus() == Approx( 4.5f ) );
        CHECK( you.mabuff_dodge_bonus() == Approx( 2.4f ) );
        // 1.8
        CHECK( you.mabuff_block_bonus() == 1 );
    }
}

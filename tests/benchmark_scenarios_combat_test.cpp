// Repeatable performance scenarios for combat: shot pellets, shrapnel, and how NPCs pick
// an attack. They follow tests/benchmark_scenarios_test.cpp: hidden ([.]) and only run
// when asked for, e.g.
//     tests/cata_test "[benchmark_scenario]"
// or through tools/benchmark/run_benchmarks.py. Each scenario builds a fixed, seeded setup
// (not timed), then times a fixed amount of work. After one untimed warm-up, it repeats
// that BENCH_REPS times (default 5) and prints one machine-readable line:
//     BENCH <name> median_ms=... min_ms=... max_ms=... reps=... work="..."
//
// Keep each scenario's work identical between versions of the code, so timings stay
// comparable. If you change a scenario, rename it so old results aren't compared with
// new ones.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "explosion.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "iuse_actor.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "npc_attack.h"
#include "options_helpers.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"
#include "weather.h"
#include "weather_type.h"

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_273x110flock( "273x110flock" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_brogyaga( "brogyaga" );
static const itype_id itype_grenade_act( "grenade_act" );
static const itype_id itype_knife_large( "knife_large" );
static const itype_id itype_machete( "machete" );
static const itype_id itype_modular_m16a4( "modular_m16a4" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_shot_bird( "shot_bird" );
static const itype_id itype_shotgun_s( "shotgun_s" );

static const mtype_id mon_zombie( "mon_zombie" );

static const spell_id spell_test_npc_cone_cold( "test_npc_cone_cold" );
static const spell_id spell_test_npc_fireball( "test_npc_fireball" );
static const spell_id spell_test_npc_lightning_bolt( "test_npc_lightning_bolt" );
static const spell_id spell_test_npc_magic_missile( "test_npc_magic_missile" );

static const string_id<npc_template> npc_template_test_talker( "test_talker" );

namespace
{

constexpr unsigned int scenario_seed = 4242;

int bench_reps()
{
    const char *env = std::getenv( "BENCH_REPS" );
    const int reps = env ? static_cast<int>( std::strtol( env, nullptr, 10 ) ) : 5;
    return std::max( reps, 1 );
}

// Runs setup (untimed) then work (timed) several times and prints the result line.
void run_scenario( const std::string &name, const std::string &work_description,
                   const std::function<void()> &setup, const std::function<void()> &work )
{
    std::vector<double> samples;
    const int reps = bench_reps();
    // One untimed warm-up repetition first, so cold caches and first-use allocations
    // don't inflate the first sample.
    for( int r = -1; r < reps; ++r ) {
        rng_set_engine_seed( scenario_seed );
        setup();
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        work();
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        if( r >= 0 ) {
            samples.push_back( std::chrono::duration<double, std::milli>( end - start ).count() );
        }
    }
    std::sort( samples.begin(), samples.end() );
    std::printf( "BENCH %s median_ms=%.2f min_ms=%.2f max_ms=%.2f reps=%d work=\"%s\"\n",
                 name.c_str(), samples[samples.size() / 2], samples.front(), samples.back(), reps,
                 work_description.c_str() );
    static_cast<void>( std::fflush( stdout ) );
}

// Fixed, mild weather, so results don't depend on the natural weather at the test map.
struct fixed_weather {
    scoped_weather_override weather{ WEATHER_CLEAR };
    fixed_weather() {
        weather.with_windspeed( 0 );
    }
};

const tripoint_bub_ms scenario_center( MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 );
// load_npcs() only loads NPCs near the player, who waits underground at (0, 0)
const tripoint_bub_ms npc_center( 50, 50, 0 );

// An NPC at the given place on a cleared map, in daylight, with the avatar out of the way.
npc &spawn_scenario_npc( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    clear_avatar();
    get_avatar().setpos( here, pos );
    clear_map_and_put_player_underground();
    clear_vehicles();
    const character_id guy_id = here.place_npc( pos.xy(), npc_template_test_talker );
    npc &guy = *g->find_npc( guy_id );
    clear_character( guy );
    guy.setpos( here, pos );
    g->load_npcs();
    REQUIRE( get_creature_tracker().creature_at<npc>( pos ) == &guy );
    set_time( calendar::turn_zero + 12_hours );
    return guy;
}

// A shot from a standard shooter, as the ranged balance tests fire them.
void fire_once( standard_npc &shooter, const itype_id &gun, const itype_id &ammo,
                const tripoint_bub_ms &target )
{
    arm_shooter( shooter, gun, {}, ammo );
    shooter.recoil = 0;
    shooter.fire_gun( get_map(), target, 1, *shooter.get_wielded_item() );
}

} // namespace

// CMBT-P01: a 27.3x110 flockshot round fires 24155 pellets, each walked along its own
// trajectory. One round at an unarmored target 6 tiles away (the pellets fly on past it),
// one at a target 2 tiles away whose armor stops every pellet that hits it.
TEST_CASE( "bench_shot_flockshot", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    std::unique_ptr<standard_npc> shooter;
    run_scenario( "shot_flockshot",
                  "2 rounds of 27.3x110 flockshot: at an unarmored target 6 tiles away, at an armored one 2 tiles away",
    [&shooter]() {
        clear_avatar();
        clear_map_and_put_player_underground();
        shooter = std::make_unique<standard_npc>( "Shooter", scenario_center,
                  std::vector<itype_id>(), 5, 10, 10, 10, 10 );
        spawn_test_monster( "mon_test_shotgun_0_bullet", scenario_center + tripoint_rel_ms( 6, 0, 0 ) );
        spawn_test_monster( "mon_test_shotgun_30_bullet",
                            scenario_center + tripoint_rel_ms( -2, 0, 0 ) );
    }, [&shooter]() {
        fire_once( *shooter, itype_brogyaga, itype_273x110flock,
                   scenario_center + tripoint_rel_ms( 6, 0, 0 ) );
        fire_once( *shooter, itype_brogyaga, itype_273x110flock,
                   scenario_center + tripoint_rel_ms( -2, 0, 0 ) );
    } );
    clear_creatures();
}

// CMBT-P01: the common case, 12 gauge birdshot (100 pellets) at a target 5 tiles away.
TEST_CASE( "bench_shot_birdshot", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    std::unique_ptr<standard_npc> shooter;
    run_scenario( "shot_birdshot", "30 rounds of 12 gauge birdshot at a target 5 tiles away",
    [&shooter]() {
        clear_avatar();
        clear_map_and_put_player_underground();
        shooter = std::make_unique<standard_npc>( "Shooter", scenario_center,
                  std::vector<itype_id>(), 5, 10, 10, 10, 10 );
        spawn_test_monster( "mon_test_shotgun_5_bullet", scenario_center + tripoint_rel_ms( 5, 0, 0 ) );
    }, [&shooter]() {
        for( int shot = 0; shot < 30; ++shot ) {
            fire_once( *shooter, itype_shotgun_s, itype_shot_bird,
                       scenario_center + tripoint_rel_ms( 5, 0, 0 ) );
        }
    } );
    clear_creatures();
}

// CMBT-P02: a grenade among armored zombies (bullet armor 51) that survive it, so every
// fragment that hits them is resolved against their armor: about 5400 for the one standing
// on the grenade, 170 for each of the 8 next to it and 40 for each of the 16 two tiles away.
TEST_CASE( "bench_shrapnel_armored", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    const item grenade( itype_grenade_act );
    const explosion_data &ex = dynamic_cast<const explosion_iuse *>
                               ( grenade.type->countdown_action.get_actor_ptr() )->explosion;
    run_scenario( "shrapnel_armored",
                  "1 grenade on an armored zombie, with 8 more 1 tile away and 16 more 2 tiles away",
    []() {
        clear_avatar();
        clear_map_and_put_player_underground();
        for( int dy = -2; dy <= 2; ++dy ) {
            for( int dx = -2; dx <= 2; ++dx ) {
                spawn_test_monster( "mon_zombie_armored", scenario_center + tripoint_rel_ms( dx, dy, 0 ) );
            }
        }
    }, [&ex]() {
        explosion_handler::explosion( nullptr, scenario_center, ex );
        explosion_handler::process_explosions();
    } );
    clear_creatures();
}

// NPC-P01, MAGIC-P02: an NPC caster rates each attack spell it knows on every combat move.
// A follower knowing four attack spells (a fireball-like blast, a cone and a line that
// leave fields, and a single-target bolt), with six zombies and the player in range.
TEST_CASE( "bench_npc_spell_eval", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    npc *caster = nullptr;
    monster *target = nullptr;
    run_scenario( "npc_spell_eval",
                  "20 attack choices of an NPC knowing 4 attack spells, 6 zombies and the player in range",
    [&caster, &target]() {
        npc &guy = spawn_scenario_npc( npc_center );
        guy.set_fac( faction_your_followers );
        guy.rules.clear_flag( ally_rule::use_silent );
        for( const spell_id &sp : {
                 spell_test_npc_fireball, spell_test_npc_cone_cold, spell_test_npc_lightning_bolt,
                 spell_test_npc_magic_missile
             } ) {
            guy.magic->learn_spell( sp, guy, true );
            guy.magic->get_spell( sp ).set_level( guy, 10 );
        }
        guy.magic->set_mana( 100000 );
        map &here = get_map();
        get_avatar().setpos( here, npc_center + tripoint_rel_ms( 7, 3, 0 ) );
        const std::vector<tripoint_rel_ms> offsets = { { 4, 1, 0 }, { 6, -2, 0 }, { 9, 2, 0 },
            { -5, 5, 0 }, { 12, -6, 0 }, { 3, 11, 0 }
        };
        target = nullptr;
        for( const tripoint_rel_ms &off : offsets ) {
            monster *zombie = g->place_critter_at( mon_zombie, npc_center + off );
            REQUIRE( zombie != nullptr );
            if( target == nullptr ) {
                target = zombie;
            }
        }
        here.build_map_cache( 0 );
        caster = &guy;
    }, [&caster, &target]() {
        for( int i = 0; i < 20; ++i ) {
            caster->evaluate_best_attack( target );
        }
        CHECK( caster->get_current_attack() );
    } );
    clear_creatures();
    clear_npcs();
    clear_avatar();
}

// NPC-P02: an NPC rates every weapon it carries on every combat move: a rifle with several
// fire modes, a shotgun, two melee weapons and rocks, with six zombies at 3 to 30 tiles.
TEST_CASE( "bench_npc_weapon_eval", "[.][benchmark_scenario]" )
{
    fixed_weather weather;
    npc *fighter = nullptr;
    monster *target = nullptr;
    run_scenario( "npc_weapon_eval",
                  "50 attack choices of an NPC with a rifle, a shotgun, 2 melee weapons and rocks, 6 zombies in range",
    [&fighter, &target]() {
        npc &guy = spawn_scenario_npc( npc_center );
        guy.set_attitude( NPCATT_KILL );
        guy.rules.set_flag( ally_rule::use_guns );
        guy.wear_item( item( itype_backpack ), false );
        item shotgun( itype_shotgun_s );
        shotgun.ammo_set( itype_shot_bird );
        guy.i_add( shotgun );
        guy.i_add( item( itype_knife_large ) );
        guy.i_add( item( itype_machete ) );
        guy.i_add( item( itype_rock, calendar::turn, 5 ) );
        arm_shooter( guy, itype_modular_m16a4 );
        const std::vector<tripoint_rel_ms> offsets = { { 3, 0, 0 }, { 5, 4, 0 }, { -8, 2, 0 },
            { 12, -9, 0 }, { -20, -15, 0 }, { 30, 8, 0 }
        };
        target = nullptr;
        for( const tripoint_rel_ms &off : offsets ) {
            monster *zombie = g->place_critter_at( mon_zombie, npc_center + off );
            REQUIRE( zombie != nullptr );
            if( target == nullptr ) {
                target = zombie;
            }
        }
        get_map().build_map_cache( 0 );
        fighter = &guy;
    }, [&fighter, &target]() {
        for( int i = 0; i < 50; ++i ) {
            fighter->evaluate_best_attack( target );
        }
        CHECK( fighter->get_current_attack() );
    } );
    clear_creatures();
    clear_npcs();
}

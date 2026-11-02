// Behavior pins for the combat performance fixes (CMBT-P01, CMBT-P02, NPC-P01/MAGIC-P02,
// NPC-P02). The statistical expectations were measured on the code before the fixes
// (seed 4242, run with AUDIT_PRINT_STATS=1 to print them); each tolerance is six standard
// errors of one run, a bit over four standard errors of the difference between two runs.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "enums.h"
#include "explosion.h"
#include "game.h"
#include "gun_mode.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse_actor.h"
#include "line.h"
#include "magic.h"
#include "magic_spell_effect_helpers.h"
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
#include "test_statistics.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "weather_type.h"

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_273x110flock( "273x110flock" );
static const itype_id itype_40x46mm_hornets_nest_410( "40x46mm_hornets_nest_410" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_brogyaga( "brogyaga" );
static const itype_id itype_grenade_act( "grenade_act" );
static const itype_id itype_helmet_army( "helmet_army" );
static const itype_id itype_kevlar( "kevlar" );
static const itype_id itype_knife_large( "knife_large" );
static const itype_id itype_m79( "m79" );
static const itype_id itype_modular_m16a4( "modular_m16a4" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_shot_00( "shot_00" );
static const itype_id itype_shot_bird( "shot_bird" );
static const itype_id itype_shotgun_s( "shotgun_s" );

static const mtype_id mon_zombie( "mon_zombie" );

// Magiclysm's attack spells that NPCs are given or cast, checked when the mod is loaded
// (--mods=magiclysm)
static const spell_id spell_cone_cold( "cone_cold" );
static const spell_id spell_fireball( "fireball" );
static const spell_id spell_gravity_well( "gravity_well" );
static const spell_id spell_lightning_bolt( "lightning_bolt" );
static const spell_id spell_magic_missile( "magic_missile" );
static const spell_id spell_test_area_blast_ignore_walls( "test_area_blast_ignore_walls" );
static const spell_id spell_test_area_cone_ignore_walls( "test_area_cone_ignore_walls" );
static const spell_id spell_test_area_wide_line( "test_area_wide_line" );
static const spell_id spell_test_npc_cone_cold( "test_npc_cone_cold" );
static const spell_id spell_test_npc_fireball( "test_npc_fireball" );
static const spell_id spell_test_npc_lightning_bolt( "test_npc_lightning_bolt" );
static const spell_id spell_test_npc_magic_missile( "test_npc_magic_missile" );
static const spell_id spell_test_spell_pew( "test_spell_pew" );

static const string_id<npc_template> npc_template_test_talker( "test_talker" );

static const trait_id trait_TEST_HALF_AVOID_FRIENDLY_FIRE( "TEST_HALF_AVOID_FRIENDLY_FIRE" );

static const ter_str_id ter_t_laminated_glass( "t_laminated_glass" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_window( "t_window" );

static const vproto_id vehicle_prototype_wheelchair( "wheelchair" );

namespace
{

struct outcome_stats {
    statistics<double> damage;
    statistics<bool> kill;
};

// Set AUDIT_PRINT_STATS to print the measured numbers, to record them from the old code.
void print_stats( const char *what, const statistics<double> &stat )
{
    if( std::getenv( "AUDIT_PRINT_STATS" ) ) {
        std::printf( "STATS %s n=%d avg=%.4f sd=%.4f se=%.4f\n", what, stat.n(), stat.avg(),
                     stat.stddev(), stat.stddev() / std::sqrt( stat.n() ) );
    }
}

void print_stats( const char *what, const statistics<bool> &stat )
{
    if( std::getenv( "AUDIT_PRINT_STATS" ) ) {
        std::printf( "STATS %s n=%d rate=%.4f\n", what, stat.n(), stat.avg() );
    }
}

// Mean damage within `margin` of the old code's mean, and the kill rate within
// `kill_margin` of the old kill rate.
void check_outcome( const outcome_stats &got, double mean, double margin, double kill_rate,
                    double kill_margin )
{
    print_stats( "damage", got.damage );
    print_stats( "kill", got.kill );
    CAPTURE( got.damage.n() );
    CAPTURE( got.damage.avg() );
    CAPTURE( got.damage.stddev() );
    CAPTURE( got.kill.avg() );
    CHECK( got.damage.avg() == Approx( mean ).margin( margin ) );
    CHECK( got.kill.avg() == Approx( kill_rate ).margin( kill_margin ) );
}

// One shot per trial from a standard shooter at a fresh monster, like the shot balance tests.
outcome_stats shoot_trials( const itype_id &gun, const itype_id &ammo, int range,
                            const std::string &mon_type, int hp, int trials )
{
    map &here = get_map();
    clear_map_without_vision();
    rng_set_engine_seed( 4242 );
    constexpr tripoint_bub_ms shooter_pos{ 60, 60, 0 };
    const tripoint_bub_ms mon_pos = shooter_pos + tripoint_rel_ms( range, 0, 0 );
    std::unique_ptr<standard_npc> shooter = std::make_unique<standard_npc>( "Shooter",
                                            shooter_pos, std::vector<itype_id>(), 5, 10, 10, 10, 10 );
    outcome_stats out;
    for( int i = 0; i < trials; ++i ) {
        clear_creatures();
        here.i_clear( mon_pos );
        arm_shooter( *shooter, gun, {}, ammo );
        shooter->recoil = 0;
        monster &mon = spawn_test_monster( mon_type, mon_pos, false );
        if( hp > 0 ) {
            mon.set_hp( hp );
        }
        const int before = mon.get_hp();
        shooter->fire_gun( here, mon_pos, 1, *shooter->get_wielded_item() );
        out.damage.add( before - mon.get_hp() );
        out.kill.add( mon.is_dead() || mon.get_hp() <= 0 );
    }
    clear_creatures();
    clear_items( 0 );
    return out;
}

const explosion_data &grenade_explosion()
{
    static const item grenade( itype_grenade_act );
    return dynamic_cast<const explosion_iuse *>
           ( grenade.type->countdown_action.get_actor_ptr() )->explosion;
}

// One grenade per trial, `distance` tiles from a fresh monster.
outcome_stats grenade_trials( const std::string &mon_type, int distance, int hp, int trials )
{
    map &here = get_map();
    clear_map_and_put_player_underground();
    rng_set_engine_seed( 4242 );
    const tripoint_bub_ms origin( 60, 60, 0 );
    const tripoint_bub_ms mon_pos = origin + tripoint_rel_ms( distance, 0, 0 );
    outcome_stats out;
    for( int i = 0; i < trials; ++i ) {
        clear_creatures();
        clear_fields( 0 );
        here.i_clear( mon_pos );
        monster &mon = spawn_test_monster( mon_type, mon_pos, false );
        if( hp > 0 ) {
            mon.set_hp( hp );
        }
        const int before = mon.get_hp();
        explosion_handler::explosion( nullptr, origin, grenade_explosion() );
        explosion_handler::process_explosions();
        out.damage.add( before - mon.get_hp() );
        out.kill.add( mon.is_dead() || mon.get_hp() <= 0 );
    }
    clear_creatures();
    clear_fields( 0 );
    clear_items( 0 );
    return out;
}

// An NPC in daylight on a cleared map, with the avatar underground below it.
npc &spawn_audit_npc( const tripoint_bub_ms &pos )
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

struct armored_npc_stats {
    statistics<double> hp_lost;
    statistics<bool> killed;
    statistics<double> armor_damage;
};

// One grenade per trial, `distance` tiles from an NPC in a kevlar vest and army helmet.
armored_npc_stats grenade_npc_trials( int distance, int trials )
{
    map &here = get_map();
    // load_npcs() only loads NPCs near the player, who waits underground at (0, 0)
    const tripoint_bub_ms npc_pos( 50, 50, 0 );
    const tripoint_bub_ms origin = npc_pos - tripoint_rel_ms( distance, 0, 0 );
    spawn_audit_npc( npc_pos );
    armored_npc_stats out;
    rng_set_engine_seed( 4242 );
    for( int i = 0; i < trials; ++i ) {
        clear_npcs();
        clear_fields( 0 );
        clear_items( 0 );
        const character_id guy_id = here.place_npc( npc_pos.xy(), npc_template_test_talker );
        npc &guy = *g->find_npc( guy_id );
        clear_character( guy );
        guy.setpos( here, npc_pos );
        g->load_npcs();
        REQUIRE( get_creature_tracker().creature_at<npc>( npc_pos ) == &guy );
        guy.wear_item( item( itype_kevlar ), false );
        guy.wear_item( item( itype_helmet_army ), false );
        int hp_before = 0;
        for( const bodypart_id &bp : guy.get_all_body_parts() ) {
            hp_before += guy.get_part_hp_cur( bp );
        }
        explosion_handler::explosion( nullptr, origin, grenade_explosion() );
        explosion_handler::process_explosions();
        int hp_after = 0;
        for( const bodypart_id &bp : guy.get_all_body_parts() ) {
            hp_after += std::max( 0, guy.get_part_hp_cur( bp ) );
        }
        int armor_damage = 0;
        for( const item_location &worn : guy.get_visible_worn_items() ) {
            armor_damage += worn->damage();
        }
        out.hp_lost.add( hp_before - hp_after );
        out.killed.add( guy.is_dead_state() );
        out.armor_damage.add( armor_damage );
    }
    clear_creatures();
    clear_npcs();
    clear_fields( 0 );
    clear_items( 0 );
    clear_avatar();
    return out;
}

struct scene_stats {
    statistics<double> first;
    statistics<bool> first_killed;
    statistics<double> second;
    statistics<double> friend_hurt;
    statistics<bool> window_broken;
    statistics<double> vehicle_damage;
};

// One shot per trial down a row: a follower 2 tiles out whom the shooter, also a follower,
// avoids hitting half the time; laminated glass at 3, which weakens each pellet through it;
// a plain window and a wheelchair just off the line at 5; a 100 HP target at 7 and another
// hostile right behind it, which pellets reach once the target is dead.
scene_stats scene_trials( const itype_id &gun, const itype_id &ammo, int trials )
{
    map &here = get_map();
    // load_npcs() only loads NPCs near the player, who waits underground at (0, 0)
    const tripoint_bub_ms shooter_pos( 36, 50, 0 );
    const tripoint_bub_ms friend_pos = shooter_pos + tripoint_rel_ms( 2, 0, 0 );
    const tripoint_bub_ms glass_pos = shooter_pos + tripoint_rel_ms( 3, 0, 0 );
    const tripoint_bub_ms window_pos = shooter_pos + tripoint_rel_ms( 5, 1, 0 );
    const tripoint_bub_ms vehicle_pos = shooter_pos + tripoint_rel_ms( 5, -1, 0 );
    const tripoint_bub_ms first_pos = shooter_pos + tripoint_rel_ms( 7, 0, 0 );
    const tripoint_bub_ms second_pos = shooter_pos + tripoint_rel_ms( 8, 0, 0 );
    spawn_audit_npc( friend_pos );
    std::unique_ptr<standard_npc> shooter = std::make_unique<standard_npc>( "Shooter",
                                            shooter_pos, std::vector<itype_id>(), 5, 10, 10, 10, 10 );
    shooter->set_fac( faction_your_followers );
    shooter->set_mutation( trait_TEST_HALF_AVOID_FRIENDLY_FIRE );
    shooter->recalculate_enchantment_cache();
    scene_stats out;
    rng_set_engine_seed( 4242 );
    for( int i = 0; i < trials; ++i ) {
        clear_creatures();
        clear_npcs();
        clear_vehicles();
        for( int x = 0; x <= 14; ++x ) {
            for( int y = -2; y <= 2; ++y ) {
                const tripoint_bub_ms p = shooter_pos + tripoint_rel_ms( x, y, 0 );
                here.i_clear( p );
                here.clear_fields( p );
            }
        }
        here.ter_set( glass_pos, ter_t_laminated_glass );
        here.ter_set( window_pos, ter_t_window );
        const character_id friend_id = here.place_npc( friend_pos.xy(), npc_template_test_talker );
        npc &buddy = *g->find_npc( friend_id );
        clear_character( buddy );
        buddy.setpos( here, friend_pos );
        buddy.set_fac( faction_your_followers );
        g->load_npcs();
        REQUIRE( get_creature_tracker().creature_at<npc>( friend_pos ) == &buddy );
        REQUIRE( buddy.attitude_to( *shooter ) == Creature::Attitude::FRIENDLY );
        vehicle *veh = here.add_vehicle( vehicle_prototype_wheelchair, vehicle_pos, 0_degrees, 0,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        int vehicle_hp = 0;
        for( const vpart_reference &vpr : veh->get_all_parts() ) {
            vehicle_hp += vpr.part().hp();
        }
        monster &first = spawn_test_monster( "mon_test_shotgun_0_bullet", first_pos, false );
        monster &second = spawn_test_monster( "mon_test_shotgun_0_bullet", second_pos, false );
        first.set_hp( 100 );
        second.set_hp( 2000 );
        int friend_hp = 0;
        for( const bodypart_id &bp : buddy.get_all_body_parts() ) {
            friend_hp += buddy.get_part_hp_cur( bp );
        }

        arm_shooter( *shooter, gun, {}, ammo );
        shooter->recoil = 0;
        shooter->fire_gun( here, first_pos, 1, *shooter->get_wielded_item() );

        out.first.add( 100 - first.get_hp() );
        out.first_killed.add( first.is_dead() || first.get_hp() <= 0 );
        out.second.add( 2000 - second.get_hp() );
        int friend_hp_after = 0;
        for( const bodypart_id &bp : buddy.get_all_body_parts() ) {
            friend_hp_after += buddy.get_part_hp_cur( bp );
        }
        // Capped: once hit, the friend can turn on the shooter, who then stops avoiding it,
        // and rarely takes the rest of the shot.
        out.friend_hurt.add( std::min( friend_hp - friend_hp_after, 100 ) );
        out.window_broken.add( here.ter( window_pos ) != ter_t_window );
        int vehicle_hp_after = 0;
        if( const optional_vpart_position vp = here.veh_at( vehicle_pos ) ) {
            for( const vpart_reference &vpr : vp->vehicle().get_all_parts() ) {
                vehicle_hp_after += vpr.part().hp();
            }
        }
        out.vehicle_damage.add( vehicle_hp - vehicle_hp_after );
    }
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    clear_avatar();
    clear_map_and_put_player_underground();
    return out;
}

void check_mean( const char *what, const statistics<double> &got, double mean, double margin )
{
    print_stats( what, got );
    CAPTURE( what );
    CAPTURE( got.n() );
    CAPTURE( got.avg() );
    CAPTURE( got.stddev() );
    CHECK( got.avg() == Approx( mean ).margin( margin ) );
}

void check_rate( const char *what, const statistics<bool> &got, double rate, double margin )
{
    print_stats( what, got );
    CAPTURE( what );
    CAPTURE( got.n() );
    CAPTURE( got.avg() );
    CHECK( got.avg() == Approx( rate ).margin( margin ) );
}

// The best of all_evaluations(), chosen the way evaluate() chooses: the first strictly
// better rating wins.
npc_attack_rating best_of( const std::vector<npc_attack_rating> &all, npc_attack_rating best )
{
    for( const npc_attack_rating &rating : all ) {
        if( rating > best ) {
            best = rating;
        }
    }
    return best;
}

// evaluate() must pick the same rating and target as a full scan of every tile in range
// (all_evaluations() keeps that scan), and draw the same random numbers.
// A throw's evaluate() also asks crafting_inventory(), which can draw a random number that
// all_evaluations() doesn't, so its random number check is left out.
void check_matches_full_scan( const npc_attack &attack, const npc &source,
                              const Creature *target, const npc_attack_rating &start,
                              unsigned int seed, bool same_rng = true )
{
    rng_set_engine_seed( seed );
    const npc_attack_rating fast = attack.evaluate( source, target );
    const cata_default_random_engine after_fast = rng_get_engine();
    rng_set_engine_seed( seed );
    const npc_attack_rating full = best_of( attack.all_evaluations( source, target ), start );
    const cata_default_random_engine after_full = rng_get_engine();
    CHECK( fast.value() == full.value() );
    CHECK( fast.target() == full.target() );
    if( same_rng ) {
        CHECK( after_fast == after_full );
    }
}

// A random fight around an NPC: some walls, zombies and a cow at random places, and the
// avatar on the same level as the NPC's ally (a follower) or a bystander.
struct random_fight {
    std::vector<monster *> zombies;
};

random_fight build_random_fight( npc &guy, unsigned int seed, int zombies, int walls )
{
    map &here = get_map();
    random_fight fight;
    rng_set_engine_seed( seed );
    const tripoint_bub_ms center = guy.pos_bub();
    for( int i = 0; i < walls; ++i ) {
        const tripoint_bub_ms p = center + tripoint_rel_ms( rng( -12, 12 ), rng( -12, 12 ), 0 );
        if( rl_dist( p, center ) > 1 ) {
            here.ter_set( p, ter_t_wall );
        }
    }
    const auto free_spot = [&]( int radius ) {
        for( int tries = 0; tries < 100; ++tries ) {
            const tripoint_bub_ms p = center + tripoint_rel_ms( rng( -radius, radius ),
                                      rng( -radius, radius ), 0 );
            if( p != center && here.passable( p ) &&
                get_creature_tracker().creature_at<Creature>( p ) == nullptr ) {
                return p;
            }
        }
        return center;
    };
    for( int i = 0; i < zombies; ++i ) {
        const tripoint_bub_ms p = free_spot( i % 2 == 0 ? 6 : 18 );
        if( p != center ) {
            fight.zombies.push_back( g->place_critter_at( mon_zombie, p ) );
        }
    }
    const tripoint_bub_ms cow = free_spot( 10 );
    if( cow != center ) {
        g->place_critter_at( mtype_id( "mon_cow" ), cow );
    }
    const tripoint_bub_ms ally = free_spot( 8 );
    if( ally != center ) {
        get_avatar().setpos( here, ally );
    }
    here.build_map_cache( 0 );
    return fight;
}

void learn_attack_spells( npc &guy, int level )
{
    for( const spell_id &sp : {
             spell_test_npc_fireball, spell_test_npc_cone_cold, spell_test_npc_lightning_bolt,
             spell_test_npc_magic_missile, spell_test_spell_pew
         } ) {
        guy.magic->learn_spell( sp, guy, true );
        guy.magic->get_spell( sp ).set_level( guy, level );
    }
    guy.magic->set_mana( 100000 );
}

// The spells to check, with Magiclysm's attack spells when that mod is loaded.
std::vector<spell_id> with_mod_spells( std::vector<spell_id> spells )
{
    for( const spell_id &sp : {
             spell_fireball, spell_cone_cold, spell_lightning_bolt, spell_magic_missile,
             spell_gravity_well
         } ) {
        if( sp.is_valid() ) {
            spells.push_back( sp );
        }
    }
    return spells;
}

void clean_up_fight()
{
    clear_creatures();
    clear_npcs();
    clear_avatar();
    clear_map_and_put_player_underground();
}

} // namespace

// CMBT-P01: the pellets of a shot are still resolved one by one against armor. Mean damage
// and kill rate of several loads against unarmored and armored targets match the old,
// pellet-by-pellet code.
TEST_CASE( "cmbt-p01_shot_outcomes_match_per_pellet_simulation", "[audit][ranged]" )
{
    SECTION( "birdshot (100 pellets) at an unarmored target 3 tiles away" ) {
        check_outcome( shoot_trials( itype_shotgun_s, itype_shot_bird, 3, "mon_test_shotgun_0_bullet", 0,
                                     1000 ), 19.12, 0.2, 0, 0.01 );
    }
    SECTION( "birdshot at a target whose armor stops every pellet" ) {
        check_outcome( shoot_trials( itype_shotgun_s, itype_shot_bird, 3, "mon_test_shotgun_1_bullet", 0,
                                     2000 ), 0.025, 0.021, 0, 0.01 );
    }
    SECTION( "00 buckshot (9 pellets) at an armored target 5 tiles away" ) {
        check_outcome( shoot_trials( itype_shotgun_s, itype_shot_00, 5, "mon_test_shotgun_10_bullet", 0,
                                     1000 ), 54.30, 1.4, 0, 0.01 );
    }
    SECTION( "00 buckshot kill rate against a wounded target" ) {
        check_outcome( shoot_trials( itype_shotgun_s, itype_shot_00, 5, "mon_test_shotgun_0_bullet", 150,
                                     3000 ), 152.52, 1.05, 0.875, 0.04 );
    }
    SECTION( "40mm bird's nest (528 pellets) at an unarmored target 4 tiles away" ) {
        check_outcome( shoot_trials( itype_m79, itype_40x46mm_hornets_nest_410, 4,
                                     "mon_test_shotgun_0_bullet", 0, 2000 ), 296.44, 2.2, 0.716, 0.061 );
    }
    SECTION( "flockshot (24155 pellets) at an unarmored target 3 tiles away" ) {
        check_outcome( shoot_trials( itype_brogyaga, itype_273x110flock, 3, "mon_test_shotgun_0_bullet",
                                     2000, 60 ), 608.0, 21, 0, 0.01 );
    }
    SECTION( "00 buckshot past a friend, through glass, by a window and a wheelchair at two hostiles" ) {
        const scene_stats got = scene_trials( itype_shotgun_s, itype_shot_00, 400 );
        check_mean( "first", got.first, 69.88, 5.3 );
        check_rate( "first_killed", got.first_killed, 0.06, 0.075 );
        check_mean( "second", got.second, 0.035, 0.3 );
        check_mean( "friend_hurt", got.friend_hurt, 13.9, 4.0 );
        check_rate( "window_broken", got.window_broken, 0.3425, 0.15 );
        // the weakened pellets don't get through the wheelchair's damage reduction
        check_mean( "vehicle_damage", got.vehicle_damage, 0, 0.5 );
    }
    SECTION( "40mm bird's nest past a friend, through glass, by a window and a wheelchair at two hostiles" ) {
        const scene_stats got = scene_trials( itype_m79, itype_40x46mm_hornets_nest_410, 300 );
        check_mean( "first", got.first, 92.36, 5.3 );
        check_rate( "first_killed", got.first_killed, 0.507, 0.18 );
        check_mean( "second", got.second, 2.7, 2.2 );
        check_mean( "friend_hurt", got.friend_hurt, 22.79, 6.2 );
        check_rate( "window_broken", got.window_broken, 0.91, 0.1 );
        check_mean( "vehicle_damage", got.vehicle_damage, 0, 0.5 );
    }
    SECTION( "flockshot at a target whose armor stops every pellet" ) {
        check_outcome( shoot_trials( itype_brogyaga, itype_273x110flock, 3, "mon_test_shotgun_1_bullet", 0,
                                     20 ), 0, 0.5, 0, 0.01 );
    }
}

// CMBT-P02: every fragment still hits on its own against armor. Mean damage and kill rate
// of a grenade against unarmored and armored targets match the old, hit-by-hit code.
TEST_CASE( "cmbt-p02_shrapnel_outcomes_match_per_fragment_simulation", "[audit][explosion]" )
{
    SECTION( "unarmored target 20 tiles away" ) {
        check_outcome( grenade_trials( "mon_test_shotgun_0_bullet", 20, 0, 1000 ), 20.01, 4.5, 0, 0.01 );
    }
    SECTION( "target with 30 bullet armor 3 tiles away, only critical fragments get through" ) {
        check_outcome( grenade_trials( "mon_test_shotgun_30_bullet", 3, 0, 300 ), 4.98, 0.35, 0, 0.01 );
    }
    SECTION( "armored zombie on the grenade, thousands of fragments hit it" ) {
        check_outcome( grenade_trials( "mon_zombie_armored", 0, 0, 100 ), 0.02, 0.15, 0, 0.01 );
    }
    SECTION( "armored zombie 1 tile away" ) {
        check_outcome( grenade_trials( "mon_zombie_armored", 1, 0, 100 ), 0, 0.15, 0, 0.01 );
    }
    SECTION( "NPC in a kevlar vest and helmet 6 tiles away" ) {
        const armored_npc_stats got = grenade_npc_trials( 6, 1000 );
        print_stats( "hp_lost", got.hp_lost );
        print_stats( "killed", got.killed );
        print_stats( "armor_damage", got.armor_damage );
        CAPTURE( got.hp_lost.avg() );
        CAPTURE( got.hp_lost.stddev() );
        CAPTURE( got.killed.avg() );
        CAPTURE( got.armor_damage.avg() );
        CAPTURE( got.armor_damage.stddev() );
        CHECK( got.hp_lost.avg() == Approx( 214.9 ).margin( 65 ) );
        CHECK( got.killed.avg() == Approx( 0.152 ).margin( 0.068 ) );
        CHECK( got.armor_damage.avg() == Approx( 970 ).margin( 191 ) );
    }
}

// NPC-P01, MAGIC-P02: rating an attack spell picks the same rating and target tile, with the
// same random numbers drawn, as rating every tile in range.
TEST_CASE( "npc-p01_spell_rating_matches_full_tile_scan", "[audit][npc_attack]" )
{
    scoped_weather_override weather( WEATHER_CLEAR );
    const tripoint_bub_ms center( 50, 50, 0 );
    for( const int level : {
             0, 10
         } ) {
        for( unsigned int layout = 1; layout <= 6; ++layout ) {
            CAPTURE( level );
            CAPTURE( layout );
            npc &guy = spawn_audit_npc( center );
            if( layout % 2 == 0 ) {
                guy.set_fac( faction_your_followers );
                guy.rules.clear_flag( ally_rule::use_silent );
            } else {
                guy.set_attitude( NPCATT_KILL );
            }
            if( layout % 3 == 0 ) {
                guy.rules.set_flag( ally_rule::avoid_friendly_fire );
            }
            learn_attack_spells( guy, level );
            const std::vector<spell_id> spells = with_mod_spells( {
                spell_test_npc_fireball, spell_test_npc_cone_cold, spell_test_npc_lightning_bolt,
                spell_test_npc_magic_missile, spell_test_spell_pew
            } );
            for( const spell_id &sp : spells ) {
                if( !guy.magic->knows_spell( sp ) ) {
                    guy.magic->learn_spell( sp, guy, true );
                    guy.magic->get_spell( sp ).set_level( guy, level );
                }
            }
            const random_fight fight = build_random_fight( guy, layout, 6, layout * 4 );
            const Creature *target = fight.zombies.empty() ? nullptr : fight.zombies.front();
            for( const spell_id &sp : spells ) {
                CAPTURE( sp.str() );
                check_matches_full_scan( npc_attack_spell( sp ), guy, target, npc_attack_rating(),
                                         1000 + layout );
            }
            clean_up_fight();
        }
    }
}

// MAGIC-P02 (with MAGIC-P04 and MAGIC-P05 where NPCs rate spells): the cached area calculator
// NPCs rate their spells with finds the same tiles, in the same order, as
// calculate_spell_effect_area(), for blasts, cones and lines, with walls in the way or
// ignored, near the map's edge, with either distance metric.
TEST_CASE( "magic-p02_spell_area_calculator_matches_spell_areas", "[audit][magic]" )
{
    restore_on_out_of_scope restore_trigdist( trigdist );
    map &here = get_map();
    for( const bool trig : {
             false, true
         } ) {
        trigdist = trig;
        for( unsigned int layout = 1; layout <= 2; ++layout ) {
            CAPTURE( trig );
            CAPTURE( layout );
            clear_map_and_put_player_underground();
            rng_set_engine_seed( layout );
            const tripoint_bub_ms center = layout == 1 ? tripoint_bub_ms( 50, 50, 0 ) :
                                           tripoint_bub_ms( 3, 40, 0 );
            for( int i = 0; i < 60; ++i ) {
                const tripoint_bub_ms p = center + tripoint_rel_ms( rng( -15, 15 ), rng( -15, 15 ), 0 );
                if( p != center && here.inbounds( p ) ) {
                    here.ter_set( p, ter_t_wall );
                }
            }
            for( int i = 0; i < 6; ++i ) {
                const tripoint_bub_ms p = center + tripoint_rel_ms( rng( -12, 12 ), rng( -12, 12 ), 0 );
                if( p != center && here.inbounds( p ) && here.passable( p ) &&
                    get_creature_tracker().creature_at<Creature>( p ) == nullptr ) {
                    g->place_critter_at( mon_zombie, p );
                }
            }
            get_avatar().setpos( here, center + tripoint_rel_ms( 2, 1, 0 ) );
            standard_npc caster( "Caster", center );
            const std::vector<spell_id> spells = with_mod_spells( {
                spell_test_npc_fireball, spell_test_npc_cone_cold, spell_test_npc_lightning_bolt,
                spell_test_npc_magic_missile, spell_test_spell_pew, spell_test_area_blast_ignore_walls,
                spell_test_area_cone_ignore_walls, spell_test_area_wide_line
            } );
            for( const spell_id &id : spells ) {
                CAPTURE( id.str() );
                spell sp( id );
                sp.set_level( caster, 10 );
                spell_area_calculator areas( sp, caster );
                REQUIRE( areas.usable() );
                int mismatches = 0;
                std::string first_mismatch;
                for( const tripoint_bub_ms &target : here.points_in_radius( center, sp.range( caster ) + 2 ) ) {
                    const std::set<tripoint_bub_ms> expected = calculate_spell_effect_area( sp, target, caster );
                    const std::vector<tripoint_bub_ms> &got = areas.area( target );
                    if( !std::equal( got.begin(), got.end(), expected.begin(), expected.end() ) ) {
                        if( mismatches++ == 0 ) {
                            std::ostringstream os;
                            os << "target " << target << ": " << got.size() << " tiles, expected " <<
                               expected.size();
                            first_mismatch = os.str();
                        }
                    }
                }
                CAPTURE( first_mismatch );
                CHECK( mismatches == 0 );
            }
        }
    }
    clear_creatures();
    clear_avatar();
    clear_map_and_put_player_underground();
}

// NPC-P02: rating a gun mode, a melee weapon or a throw picks the same rating and target as
// rating every tile in range.
TEST_CASE( "npc-p02_weapon_rating_matches_full_tile_scan", "[audit][npc_attack]" )
{
    scoped_weather_override weather( WEATHER_CLEAR );
    const tripoint_bub_ms center( 50, 50, 0 );
    for( unsigned int layout = 1; layout <= 6; ++layout ) {
        CAPTURE( layout );
        npc &guy = spawn_audit_npc( center );
        if( layout % 2 == 0 ) {
            guy.set_fac( faction_your_followers );
        } else {
            guy.set_attitude( NPCATT_KILL );
        }
        if( layout % 3 == 0 ) {
            guy.rules.set_flag( ally_rule::avoid_friendly_fire );
        }
        guy.rules.set_flag( ally_rule::use_guns );
        guy.wear_item( item( itype_backpack ), false );
        item_location knife = guy.i_add( item( itype_knife_large ) );
        item_location rocks = guy.i_add( item( itype_rock, calendar::turn, 5 ) );
        arm_shooter( guy, itype_modular_m16a4 );
        item_location gun = guy.get_wielded_item();
        const random_fight fight = build_random_fight( guy, 100 + layout, 8, layout * 4 );
        const Creature *target = fight.zombies.empty() ? nullptr : fight.zombies.front();
        const npc_attack_rating start( std::nullopt, guy.pos_bub() );
        for( const std::pair<const gun_mode_id, gun_mode> &mode : gun->gun_all_modes() ) {
            CAPTURE( mode.first.str() );
            check_matches_full_scan( npc_attack_gun( *gun, mode.second ), guy, target, start, 7 );
        }
        check_matches_full_scan( npc_attack_melee( *knife ), guy, target, start, 7 );
        check_matches_full_scan( npc_attack_melee( *gun ), guy, target, start, 7 );
        check_matches_full_scan( npc_attack_melee( null_item_reference() ), guy, target, start, 7 );
        check_matches_full_scan( npc_attack_throw( *rocks, nullptr, guy.speed_rating() ), guy, target,
                                 start, 7, false );
        clean_up_fight();
    }
}

namespace
{

struct chosen_attack {
    std::string kind;
    tripoint_rel_ms target;
    int value = 0;
};

chosen_attack choose_attack( npc &guy, const Creature *target, unsigned int seed )
{
    rng_set_engine_seed( seed );
    guy.evaluate_best_attack( target );
    chosen_attack chosen;
    const npc_attack *attack = guy.get_current_attack().get();
    if( dynamic_cast<const npc_attack_gun *>( attack ) ) {
        chosen.kind = "gun";
    } else if( dynamic_cast<const npc_attack_melee *>( attack ) ) {
        chosen.kind = "melee";
    } else if( dynamic_cast<const npc_attack_throw *>( attack ) ) {
        chosen.kind = "throw";
    } else if( dynamic_cast<const npc_attack_spell *>( attack ) ) {
        chosen.kind = "spell";
    } else if( attack != nullptr ) {
        chosen.kind = "other";
    }
    const npc_attack_rating &rating = guy.get_current_attack_evaluation();
    chosen.target = rating.target() - guy.pos_bub();
    chosen.value = rating.value().value_or( -1 );
    return chosen;
}

void check_choice( const chosen_attack &got, const std::string &kind,
                   const tripoint_rel_ms &target, int value )
{
    CAPTURE( got.kind );
    CAPTURE( got.target );
    CAPTURE( got.value );
    CHECK( got.kind == kind );
    CHECK( got.target == target );
    CHECK( got.value == value );
}

} // namespace

// NPC-P01, NPC-P02: the attack and target an NPC chooses in a few typical fights.
TEST_CASE( "npc-p02_npc_attack_choices", "[audit][npc_attack]" )
{
    scoped_weather_override weather( WEATHER_CLEAR );
    map &here = get_map();
    const tripoint_bub_ms center( 50, 50, 0 );
    npc &guy = spawn_audit_npc( center );
    const auto zombie_at = [&]( const tripoint_rel_ms & off ) {
        monster *zombie = g->place_critter_at( mon_zombie, center + off );
        REQUIRE( zombie != nullptr );
        return zombie;
    };
    const auto arm_fighter = [&]() {
        guy.rules.set_flag( ally_rule::use_guns );
        guy.wear_item( item( itype_backpack ), false );
        guy.i_add( item( itype_knife_large ) );
        guy.i_add( item( itype_rock, calendar::turn, 5 ) );
        arm_shooter( guy, itype_modular_m16a4 );
    };

    SECTION( "gun, melee or throw, against an adjacent zombie and one 6 tiles away" ) {
        guy.set_attitude( NPCATT_KILL );
        arm_fighter();
        monster *close = zombie_at( { 1, 0, 0 } );
        zombie_at( { 6, 0, 0 } );
        here.build_map_cache( 0 );
        check_choice( choose_attack( guy, close, 1 ), "gun", { 1, 0, 0 }, 221 );
    }
    SECTION( "zombies at 4, 9 and 15 tiles, aiming at the middle one" ) {
        guy.set_attitude( NPCATT_KILL );
        arm_fighter();
        zombie_at( { 4, 1, 0 } );
        monster *middle = zombie_at( { -9, 2, 0 } );
        zombie_at( { 3, -15, 0 } );
        here.build_map_cache( 0 );
        check_choice( choose_attack( guy, middle, 2 ), "gun", { -9, 2, 0 }, 300 );
    }
    SECTION( "a follower with the player in the line of fire" ) {
        guy.set_fac( faction_your_followers );
        guy.rules.set_flag( ally_rule::avoid_friendly_fire );
        guy.rules.clear_flag( ally_rule::use_silent );
        arm_fighter();
        get_avatar().setpos( here, center + tripoint_rel_ms( 3, 0, 0 ) );
        monster *behind = zombie_at( { 6, 0, 0 } );
        zombie_at( { 5, 4, 0 } );
        here.build_map_cache( 0 );
        check_choice( choose_attack( guy, behind, 3 ), "gun", { 6, 0, 0 }, 269 );
    }
    SECTION( "a follower casting a fireball with the player next to the zombies" ) {
        guy.set_fac( faction_your_followers );
        guy.rules.clear_flag( ally_rule::use_silent );
        guy.magic->learn_spell( spell_test_npc_fireball, guy, true );
        guy.magic->get_spell( spell_test_npc_fireball ).set_level( guy, 10 );
        guy.magic->set_mana( 100000 );
        get_avatar().setpos( here, center + tripoint_rel_ms( 9, 0, 0 ) );
        monster *near_ally = zombie_at( { 8, 0, 0 } );
        zombie_at( { 8, 1, 0 } );
        zombie_at( { -7, 3, 0 } );
        zombie_at( { -7, 4, 0 } );
        here.build_map_cache( 0 );
        check_choice( choose_attack( guy, near_ally, 4 ), "spell", { -11, 2, 0 }, 67 );
    }
    SECTION( "spells or a gun, with zombies near and far" ) {
        guy.set_attitude( NPCATT_KILL );
        arm_fighter();
        learn_attack_spells( guy, 10 );
        monster *near = zombie_at( { 2, 1, 0 } );
        zombie_at( { 7, -3, 0 } );
        zombie_at( { 8, -3, 0 } );
        zombie_at( { -12, 6, 0 } );
        here.build_map_cache( 0 );
        check_choice( choose_attack( guy, near, 5 ), "spell", { 8, -2, 0 }, 372 );
    }
    SECTION( "spells only, in a room with a doorway" ) {
        guy.set_attitude( NPCATT_KILL );
        learn_attack_spells( guy, 10 );
        for( int d = -4; d <= 4; ++d ) {
            for( const tripoint_rel_ms &off : {
                     tripoint_rel_ms( d, -4, 0 ), tripoint_rel_ms( d, 4, 0 ), tripoint_rel_ms( -4, d, 0 ),
                     tripoint_rel_ms( 4, d, 0 )
                 } ) {
                if( off != tripoint_rel_ms( 4, 0, 0 ) ) {
                    here.ter_set( center + off, ter_t_wall );
                }
            }
        }
        monster *outside = zombie_at( { 9, 1, 0 } );
        zombie_at( { 10, -2, 0 } );
        zombie_at( { -9, 0, 0 } );
        here.build_map_cache( 0 );
        check_choice( choose_attack( guy, outside, 6 ), "spell", { 3, 1, 0 }, 49 );
    }
    clean_up_fight();
}

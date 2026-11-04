#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_martial_arts.h"
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
#include "monstergenerator.h"
#include "mtype.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "veh_type.h"
#include "weakpoint.h"

// Regression tests for the combat chapter of the engine audit (Audit/18-combat.md).

static const efftype_id effect_hit_by_player( "hit_by_player" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_test_cmbt_mark( "test_cmbt_mark" );

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_club_wooden( "club_wooden" );
static const itype_id itype_glaive( "glaive" );
static const itype_id itype_spear_wood( "spear_wood" );
static const itype_id itype_test_cmbt_club( "test_cmbt_club" );
static const itype_id itype_test_cmbt_revolver( "test_cmbt_revolver" );
static const itype_id itype_test_cmbt_round_plain( "test_cmbt_round_plain" );
static const itype_id itype_test_cmbt_round_wild( "test_cmbt_round_wild" );
static const itype_id itype_test_cmbt_unwieldy( "test_cmbt_unwieldy" );

static const mabuff_id test_buff_cmbt_stacking( "test_buff_cmbt_stacking" );

static const matec_id test_cmbt_impale( "test_cmbt_impale" );
static const matec_id test_cmbt_wide( "test_cmbt_wide" );
static const matec_id test_tm_ammo( "test_tm_ammo" );
static const matec_id test_tm_disarm( "test_tm_disarm" );
static const matec_id test_tm_take( "test_tm_take" );

static const move_mode_id move_mode_prone( "prone" );

static const mtype_id mon_zombie( "mon_zombie" );
static const mtype_id mon_zombie_dog( "mon_zombie_dog" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_unarmed( "unarmed" );

static const trait_id trait_TEST_CMBT_HARDTOHIT( "TEST_CMBT_HARDTOHIT" );
static const trait_id trait_TEST_CMBT_HASTE( "TEST_CMBT_HASTE" );
static const trait_id trait_TEST_CMBT_HIT_YOU( "TEST_CMBT_HIT_YOU" );
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

// Attacks with a forced technique until a blow lands (a technique acts only on a hit). The melee
// statistics then describe that one attack, with whatever the technique added to it.
static void land_technique( Character &attacker, Creature &target, const matec_id &tec )
{
    for( int tries = 0; tries < 50; ++tries ) {
        melee::clear_stats();
        ready( attacker, target );
        attacker.melee_attack_abstract( target, false, tec );
        if( melee::get_stats().hit_count > 0 ) {
            return;
        }
    }
    FAIL( "the technique never landed" );
}

// Attacks without techniques until a blow lands
static void land_blow( Character &attacker, Creature &target )
{
    for( int tries = 0; tries < 50; ++tries ) {
        melee::clear_stats();
        ready( attacker, target );
        attacker.melee_attack( target, false );
        if( melee::get_stats().hit_count > 0 ) {
            return;
        }
    }
    FAIL( "no blow landed" );
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
    return val == nullptr ? 0.0 : val->dbl();
}

// CMBT-B04: weakpoints::select_weakpoint boosts the earlier weak points of the list with skill,
// so the list must run from the rarest up. Monster finalization merged weakpoint sets without
// sorting, so the eye of a zombie dog ("wps_eyes" last) got rarer as skill rose.
TEST_CASE( "cmbt-b04_monster_weakpoints_sorted_by_coverage", "[audit][weakpoint]" )
{
    const auto by_coverage = []( const weakpoint & a, const weakpoint & b ) {
        return a.coverage < b.coverage;
    };
    std::vector<std::string> unsorted;
    for( const mtype &type : MonsterGenerator::generator().get_all_mtypes() ) {
        const std::vector<weakpoint> &list = type.weakpoints.weakpoint_list;
        if( !std::is_sorted( list.begin(), list.end(), by_coverage ) ) {
            unsorted.push_back( type.id.str() );
        }
    }
    CAPTURE( unsorted.size() );
    CAPTURE( unsorted.empty() ? std::string() : unsorted.front() );
    CHECK( unsorted.empty() );

    // The eye (1%) of "wps_eyes", the last set, comes before the head (3%) of the first
    const std::vector<weakpoint> &dog = mon_zombie_dog->weakpoints.weakpoint_list;
    const auto index_of = [&dog]( const std::string & id ) {
        return std::find_if( dog.begin(), dog.end(), [&id]( const weakpoint & wp ) {
            return wp.id == id;
        } ) - dog.begin();
    };
    REQUIRE( index_of( "eye" ) < static_cast<std::ptrdiff_t>( dog.size() ) );
    REQUIRE( index_of( "head" ) < static_cast<std::ptrdiff_t>( dog.size() ) );
    CHECK( index_of( "eye" ) < index_of( "head" ) );
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

// CMBT-B08: Character::reach_attack asked "Your attack may cause accidental injury, continue?"
// whoever attacked, so an NPC spearing past the player's pet asked the player, and a "no" (or
// the test UI) called the NPC's attack off for free. Only the avatar is asked now.
TEST_CASE( "cmbt-b08_npc_reach_attack_through_a_pet_asks_nobody", "[audit][melee][npc_attack]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    const tripoint_bub_ms npc_pos = you.pos_bub() + tripoint( -2, 0, 0 );
    npc &guy = spawn_npc( npc_pos.xy(), "test_talker" );
    guy.remove_weapon();
    item spear( itype_spear_wood );
    REQUIRE( guy.wield( spear ) );
    // Unskilled: whatever stands in the way takes the blow
    guy.set_skill_level( skill_melee, 0 );
    monster &pet = spawn_test_monster( "mon_test_cmbt_post", you.pos_bub() + tripoint::west );
    pet.friendly = -1;
    pet.add_effect( effect_pet, 1_turns, true );

    guy.set_moves( 100 );
    guy.reach_attack( you.pos_bub() );
    CHECK( guy.get_moves() < 100 );
    CHECK( pet.has_effect( effect_hit_by_player ) );
}

// CMBT-B09: a disarm took any weapon, so an NPC's disarm dropped a deployed bionic blade or
// freed someone from handcuffs (both NO_UNWIELD).
TEST_CASE( "cmbt-b09_disarm_cannot_take_a_weapon_stuck_in_hand", "[audit][melee][martial_arts]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    npc &guy = spawn_npc( ( you.pos_bub() + tripoint::east ).xy(), "test_talker" );
    guy.remove_weapon();
    make_fighter( guy );
    item club( itype_club_wooden );
    REQUIRE( you.wield( club ) );

    SECTION( "a weapon that can leave the hands is knocked away" ) {
        land_technique( guy, you, test_tm_disarm );
        CHECK_FALSE( you.is_armed() );
    }
    SECTION( "a NO_UNWIELD weapon stays put" ) {
        you.get_wielded_item()->set_flag( flag_NO_UNWIELD );
        technique_query query;
        query.roll = false;
        CHECK( guy.check_technique( test_tm_disarm, you, guy.used_weapon(), query ) ==
               technique_rejection::target_unarmed );
        CHECK( guy.check_technique( test_tm_take, you, guy.used_weapon(), query ) ==
               technique_rejection::target_unarmed );
        // Forced, as by an EOC: the blow lands, the weapon stays
        land_technique( guy, you, test_tm_disarm );
        CHECK( you.is_armed() );
        land_technique( guy, you, test_tm_take );
        CHECK( you.is_armed() );
        CHECK_FALSE( guy.is_armed() );
        // "<npcname> disarms you!", "<npcname> disarms you and takes your weapon!"
        CHECK_FALSE( has_message_containing( "disarms you!" ) );
        CHECK_FALSE( has_message_containing( "takes your weapon" ) );
    }
}

// CMBT-B10: the hit roll came before the "too unwieldy" refusal (and the avatar's "this will take
// a long time" prompt), so an attack that never happened still used up the target's dodge, set
// off its dodge reactions and marked it as attacked.
TEST_CASE( "cmbt-b10_refused_attack_rolls_nothing", "[audit][melee]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    you.set_mutation( trait_TEST_CMBT_HASTE );
    you.recalculate_enchantment_cache();
    item weight( itype_test_cmbt_unwieldy );
    REQUIRE( you.wield( weight ) );
    const item &held = *you.get_wielded_item();
    REQUIRE( held.attack_time( you ) > you.attack_speed( held ) * 20 );

    SECTION( "a monster isn't marked as attacked" ) {
        monster &post = spawn_test_monster( "mon_test_cmbt_post", you.pos_bub() + tripoint::east );
        you.set_moves( 100 );
        CHECK_FALSE( you.melee_attack( post, true ) );
        CHECK_FALSE( post.has_effect( effect_hit_by_player ) );
        CHECK( melee::get_stats().attack_count == 0 );
        CHECK( you.get_moves() == 100 );
    }
    SECTION( "an NPC keeps its dodge" ) {
        npc &guy = spawn_npc( ( you.pos_bub() + tripoint::east ).xy(), "test_talker" );
        guy.set_dodges_left( guy.get_num_dodges() );
        const int dodges = guy.get_dodges_left();
        REQUIRE( dodges > 0 );
        you.set_moves( 100 );
        CHECK_FALSE( you.melee_attack( guy, true ) );
        CHECK( guy.get_dodges_left() == dodges );
    }
}

// CMBT-B11: area techniques picked their extra victims as the avatar sees them (hostile to the
// player), whoever attacked, and the tally ("%d enemies hit!") always said 0 and went to the
// target. They now pick the attacker's enemies and tell the attacker.
TEST_CASE( "cmbt-b11_aoe_technique_targets_the_attackers_enemies", "[audit][melee][technique]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    const tripoint_bub_ms pos = you.pos_bub();

    SECTION( "a hostile NPC's wide strike hits the player's side, not its own" ) {
        // The NPC strikes east at the avatar; its sweep covers the tiles north and south of it
        npc &guy = spawn_npc( ( pos + tripoint::west ).xy(), "test_talker" );
        npc &follower = spawn_npc( ( pos + tripoint::south ).xy(), "test_talker" );
        npc &buddy = spawn_npc( ( pos + tripoint::north ).xy(), "test_talker" );
        guy.set_attitude( NPCATT_KILL );
        buddy.set_attitude( NPCATT_KILL );
        follower.set_fac( faction_your_followers );
        follower.set_attitude( NPCATT_FOLLOW );
        REQUIRE( guy.attitude_to( follower ) == Creature::Attitude::HOSTILE );
        REQUIRE( guy.attitude_to( buddy ) != Creature::Attitude::HOSTILE );
        guy.remove_weapon();
        make_fighter( guy );

        land_technique( guy, you, test_cmbt_wide );
        CHECK( follower.has_effect( effect_hit_by_player ) );
        CHECK_FALSE( buddy.has_effect( effect_hit_by_player ) );
        // The tally is the attacker's: the player, struck, isn't told "0 enemies hit!"
        CHECK_FALSE( has_message_containing( "enemies hit!" ) );
        CHECK_FALSE( has_message_containing( "enemy hit!" ) );
    }
    SECTION( "the player's wide strike counts what it hit" ) {
        monster &front = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint::east );
        monster &side = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint::south_east );
        make_fighter( you );
        land_technique( you, front, test_cmbt_wide );
        CHECK( side.has_effect( effect_hit_by_player ) );
        CHECK( melee::get_stats().hit_count == 2 );
        CHECK( has_message_containing( "1 enemy hit!" ) );
    }
}

// The share of the avatar's blows that land on an NPC that can't dodge
static double hit_rate( avatar &you, npc &target )
{
    constexpr int attacks = 2000;
    melee::clear_stats();
    for( int i = 0; i < attacks; ++i ) {
        ready( you, target );
        target.set_all_parts_hp_to_max();
        you.melee_attack( target, false );
    }
    const melee_statistic_data stats = melee::get_stats();
    REQUIRE( stats.attack_count == attacks );
    return static_cast<double>( stats.hit_count ) / attacks;
}

// CMBT-B12: HARDTOHIT took the better of two dodge rolls, but the dodge roll has no randomness,
// so the flag did nothing against melee. The attacker now rolls to hit twice and keeps the worse.
TEST_CASE( "cmbt-b12_hardtohit_makes_melee_attackers_roll_twice", "[audit][melee]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    npc &target = spawn_npc( ( you.pos_bub() + tripoint::east ).xy(), "test_talker" );
    target.remove_weapon();
    // No practice: the attacker's skill stays the same throughout
    target.times_combatted_player = 1000;

    const double plain = hit_rate( you, target );
    target.set_mutation( trait_TEST_CMBT_HARDTOHIT );
    const double hard = hit_rate( you, target );
    CAPTURE( plain, hard );
    REQUIRE( plain > 0.3 );
    REQUIRE( plain < 0.95 );
    // Both rolls must hit, so the chance is squared
    CHECK( hard == Approx( plain * plain ).margin( 0.05 ) );
}

// CMBT-B13: an enchantment's hit_you_effect went off after every swing, misses included.
TEST_CASE( "cmbt-b13_hit_you_effect_only_on_a_hit", "[audit][melee]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    you.set_mutation( trait_TEST_CMBT_HIT_YOU );
    you.recalculate_enchantment_cache();

    SECTION( "a miss casts nothing" ) {
        monster &dodger = spawn_test_monster( "mon_test_cmbt_dodger", you.pos_bub() + tripoint::east );
        for( int i = 0; i < 10; ++i ) {
            ready( you, dodger );
            you.melee_attack( dodger, false );
        }
        REQUIRE( melee::get_stats().attack_count == 10 );
        REQUIRE( melee::get_stats().hit_count == 0 );
        CHECK( num_var( you, "test_cmbt_hit_you" ) == 0 );
    }
    SECTION( "a hit casts the spell" ) {
        monster &post = spawn_test_monster( "mon_test_cmbt_post", you.pos_bub() + tripoint::east );
        land_blow( you, post );
        CHECK( num_var( you, "test_cmbt_hit_you" ) == 1 );
    }
}

// CMBT-B14: melee weak point effects got the attack without its attacker, so their EOCs ran
// without an alpha talker and an instant kill credited nobody.
TEST_CASE( "cmbt-b14_melee_weakpoint_effects_know_the_attacker", "[audit][melee][weakpoint]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    make_fighter( you );

    SECTION( "the weak point's EOC runs with the attacker as alpha" ) {
        monster &mon = spawn_test_monster( "mon_test_cmbt_weakpoint", you.pos_bub() + tripoint::east );
        land_blow( you, mon );
        CHECK( num_var( you, "test_cmbt_wp_alpha" ) == 1 );
        CHECK( num_var( mon, "test_cmbt_wp_alpha" ) == 0 );
    }
    SECTION( "an instant kill is credited to the attacker" ) {
        monster &mon = spawn_test_monster( "mon_test_cmbt_weakpoint_deadly",
                                           you.pos_bub() + tripoint::east, false );
        land_blow( you, mon );
        REQUIRE( mon.is_dead() );
        CHECK( mon.get_killer() == &you );
    }
}

// Weak point effects (review of CMBT-B14): a missing "damage_required" or "duration" was read as
// ( 0, 0 ), not the defaults, so such an effect went off only on a hit that dealt no damage, and
// lasted no time.
TEST_CASE( "cmbt-weakpoint-defaults_effect_without_ranges_fires_and_lasts", "[audit][weakpoint]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    make_fighter( you );
    monster &mon = spawn_test_monster( "mon_test_cmbt_wp_defaults", you.pos_bub() + tripoint::east );
    land_blow( you, mon );
    CHECK( num_var( you, "test_cmbt_wp_defaults" ) == 1 );
    CHECK( mon.get_effect_dur( effect_test_cmbt_mark ) == 1_seconds );
}

// The brain's instant death needs a hit that does some damage (1% of the monster's HP).
TEST_CASE( "cmbt-weakpoint-defaults_brain_needs_a_damaging_hit", "[audit][weakpoint]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", you.pos_bub() + tripoint::east );
    const std::vector<weakpoint> &list = mon_zombie->weakpoints.weakpoint_list;
    const auto brain = std::find_if( list.begin(), list.end(), []( const weakpoint & wp ) {
        return wp.id == "brain";
    } );
    REQUIRE( brain != list.end() );
    weakpoint_attack attack;
    attack.source = &you;
    attack.target = &zed;

    brain->apply_effects( zed, 0, attack );
    CHECK_FALSE( zed.is_dead() );
    brain->apply_effects( zed, zed.get_hp_max() / 2, attack );
    CHECK( zed.is_dead() );
}

// CMBT-B15: NPC weapon scoring counted a loaded gun's rounds twice (in gun_damage() and again
// from the ammo), counted the ammo's dispersion twice, and gave the polearm penalty to every
// candidate when the NPC held a polearm, and to none otherwise.
TEST_CASE( "cmbt-b15_npc_weapon_values_count_each_thing_once", "[audit][npc_attack][ranged]" )
{
    combat_scene scene;
    avatar &you = get_avatar();

    SECTION( "a loaded gun is worth what an empty one is, with the same rounds" ) {
        item loaded( itype_test_cmbt_revolver );
        loaded.ammo_set( itype_test_cmbt_round_plain );
        REQUIRE( loaded.ammo_remaining() > 0 );
        const item empty( itype_test_cmbt_revolver );
        REQUIRE( empty.ammo_remaining() == 0 );
        REQUIRE( empty.ammo_default() == itype_test_cmbt_round_plain );
        CHECK( you.gun_value( loaded ) == Approx( you.gun_value( empty ) ) );
    }
    SECTION( "the rounds' dispersion counts once, scaled with the gun's" ) {
        // 15 + 0 and 15 + 7, divided by 18, both round to 1
        item plain( itype_test_cmbt_revolver );
        plain.ammo_set( itype_test_cmbt_round_plain );
        item wild( itype_test_cmbt_revolver );
        wild.ammo_set( itype_test_cmbt_round_wild );
        REQUIRE( plain.gun_dispersion() == wild.gun_dispersion() );
        CHECK( you.gun_value( plain ) == Approx( you.gun_value( wild ) ) );
    }
    SECTION( "the polearm penalty goes to a polearm, whatever is held" ) {
        const item club( itype_club_wooden );
        const item glaive( itype_glaive );
        REQUIRE( glaive.has_flag( flag_POLEARM ) );
        const double club_value = you.melee_value( club );
        const double glaive_value = you.melee_value( glaive );
        item held( itype_glaive );
        REQUIRE( you.wield( held ) );
        CHECK( you.melee_value( club ) == Approx( club_value ) );
        CHECK( you.melee_value( glaive ) == Approx( glaive_value ) );
        you.remove_weapon();
        item stick( itype_club_wooden );
        REQUIRE( you.wield( stick ) );
        CHECK( you.melee_value( glaive ) == Approx( glaive_value ) );
    }
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

// CMBT-B17: "impale" looked two tiles away for its second victim, then attacked it with
// melee_attack(), which refuses anything out of reach, so nothing behind the target was ever
// struck; its "right" tile was miscomputed (beside the attacker, or the target itself); and with
// no one to strike, a forced impale dereferenced a null pointer.
TEST_CASE( "cmbt-b17_impale_strikes_the_creature_behind", "[audit][melee][technique]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    make_fighter( you );
    const tripoint_bub_ms pos = you.pos_bub();

    SECTION( "east: the creature behind the target is struck" ) {
        monster &front = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint::east );
        monster &behind = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint( 2, 0, 0 ) );
        land_technique( you, front, test_cmbt_impale );
        CHECK( behind.has_effect( effect_hit_by_player ) );
        CHECK( melee::get_stats().attack_count == 2 );
        CHECK( has_message_containing( "1 enemy hit!" ) );
    }
    SECTION( "north: a creature beside the attacker is not struck" ) {
        monster &front = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint::north );
        monster &beside = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint::west );
        land_technique( you, front, test_cmbt_impale );
        CHECK_FALSE( beside.has_effect( effect_hit_by_player ) );
        CHECK( melee::get_stats().attack_count == 1 );
    }
    SECTION( "north-east: the target isn't struck twice" ) {
        monster &front = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint::north_east );
        land_technique( you, front, test_cmbt_impale );
        CHECK( melee::get_stats().attack_count == 1 );
    }
    SECTION( "north-east: the creature beyond the corner is struck" ) {
        monster &front = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint::north_east );
        monster &beyond = spawn_test_monster( "mon_test_cmbt_post", pos + tripoint( 1, -2, 0 ) );
        land_technique( you, front, test_cmbt_impale );
        CHECK( beyond.has_effect( effect_hit_by_player ) );
        CHECK( melee::get_stats().attack_count == 2 );
    }
}

TEST_CASE( "cmbt-b17_forced_techniques_with_nothing_to_use", "[audit][melee][technique]" )
{
    combat_scene scene;
    avatar &you = get_avatar();
    make_fighter( you );
    monster &post = spawn_test_monster( "mon_test_cmbt_post", you.pos_bub() + tripoint::north );

    SECTION( "an impale with no one behind the target" ) {
        land_technique( you, post, test_cmbt_impale );
        CHECK( melee::get_stats().attack_count == 1 );
    }
    SECTION( "a technique that needs ammo, unarmed" ) {
        REQUIRE_FALSE( you.is_armed() );
        land_technique( you, post, test_tm_ammo );
        CHECK( melee::get_stats().attack_count == 1 );
    }
}

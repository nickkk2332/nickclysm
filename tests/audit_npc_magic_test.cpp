#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "damage.h"
#include "debug.h"
#include "flag.h"
#include "game.h"
#include "gun_mode.h"
#include "item.h"
#include "item_location.h"
#include "magic.h"
#include "magic_spell_effect_helpers.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "npc_attack.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "projectile.h"
#include "type_id.h"

class Creature;

static const faction_id faction_your_followers( "your_followers" );

static const gun_mode_id gun_mode_BURST( "BURST" );

static const itype_id itype_modular_m16a4( "modular_m16a4" );
static const itype_id itype_shot_00( "shot_00" );
static const itype_id itype_u_shotgun( "u_shotgun" );

static const mtype_id mon_zombie( "mon_zombie" );

static const spell_id spell_boomer_claymore( "boomer_claymore" );
static const spell_id spell_debug_kill_spell( "debug_kill_spell" );
static const spell_id spell_spell_corrosive_spray( "spell_corrosive_spray" );
static const spell_id spell_weaponpod_saw( "weaponpod_saw" );

static const string_id<npc_template> npc_template_test_talker( "test_talker" );

static const ter_str_id ter_t_wall( "t_wall" );

static constexpr tripoint_bub_ms audit_npc_pos{ 50, 50, 0 };

// An NPC on an empty map, with the avatar out of the way underground.
static npc &audit_spawn_npc()
{
    map &here = get_map();
    clear_avatar();
    get_player_character().setpos( here, audit_npc_pos );
    clear_map_and_put_player_underground();
    clear_vehicles();
    creature_tracker &creatures = get_creature_tracker();
    REQUIRE( creatures.creature_at<Creature>( audit_npc_pos ) == nullptr );

    const character_id guy_id = here.place_npc( audit_npc_pos.xy(), npc_template_test_talker );
    npc &guy = *g->find_npc( guy_id );
    clear_character( guy );
    guy.setpos( here, audit_npc_pos );
    g->load_npcs();
    REQUIRE( creatures.creature_at<npc>( audit_npc_pos ) == &guy );
    return guy;
}

static monster &audit_spawn_zombie( const tripoint_bub_ms &pos )
{
    monster *zombie = g->place_critter_at( mon_zombie, pos );
    REQUIRE( zombie != nullptr );
    return *zombie;
}

// npc_attack_gun::use() ignored the gun mode it was evaluated with and called
// fire_gun( location ), which fires one shot of the wielded base gun: a burst mode fired
// one round, and an underbarrel gunmod mode fired the rifle instead of the gunmod.
TEST_CASE( "npc-b02_npc_gun_attack_fires_the_evaluated_mode", "[audit][npc_attack]" )
{
    npc &guy = audit_spawn_npc();
    arm_shooter( guy, itype_modular_m16a4 );
    item_location gun = guy.get_wielded_item();
    REQUIRE( gun );
    REQUIRE( gun->typeId() == itype_modular_m16a4 );
    // a jam would end the burst early
    gun->set_flag( flag_NEVER_JAMS );
    const int rifle_ammo = gun->ammo_remaining();
    REQUIRE( rifle_ammo >= 3 );

    // adjacent, so the NPC fires at once instead of aiming
    monster &zombie = audit_spawn_zombie( audit_npc_pos + tripoint::east );

    SECTION( "a burst mode fires its whole burst" ) {
        const std::map<gun_mode_id, gun_mode> modes = gun->gun_all_modes();
        const auto burst = modes.find( gun_mode_BURST );
        REQUIRE( burst != modes.end() );
        REQUIRE( burst->second.target == gun.get_item() );
        REQUIRE( burst->second.qty == 3 );
        REQUIRE( gun->gun_get_mode_id() != gun_mode_BURST );

        const npc_attack_gun attack( *gun, burst->second );
        attack.use( guy, zombie.pos_bub() );

        CHECK( guy.get_wielded_item()->ammo_remaining() == rifle_ammo - 3 );
        CHECK( guy.get_wielded_item()->gun_get_mode_id() == gun_mode_BURST );
    }

    SECTION( "a hallucination fakes the burst, capped by the ammo left" ) {
        guy.hallucination = true;
        REQUIRE( guy.is_hallucination() );
        REQUIRE( rifle_ammo >= 4 );
        const int unloaded = rifle_ammo - 4;
        REQUIRE( gun->ammo_consume( unloaded, guy.pos_bub(), &guy ) == unloaded );
        REQUIRE( gun->ammo_remaining() == 4 );

        const std::map<gun_mode_id, gun_mode> modes = gun->gun_all_modes();
        const auto burst = modes.find( gun_mode_BURST );
        REQUIRE( burst != modes.end() );
        REQUIRE( burst->second.qty == 3 );
        const npc_attack_gun attack( *gun, burst->second );

        // a full burst
        std::string dmsg = capture_debugmsg_during( [&]() {
            attack.use( guy, zombie.pos_bub() );
        } );
        CHECK( dmsg.empty() );
        CHECK( guy.get_wielded_item()->ammo_remaining() == 1 );

        // one round left: one shot, not a burst that runs dry with a debugmsg
        dmsg = capture_debugmsg_during( [&]() {
            attack.use( guy, zombie.pos_bub() );
        } );
        CHECK( dmsg.empty() );
        CHECK( guy.get_wielded_item()->ammo_remaining() == 0 );
    }

    SECTION( "an underbarrel gunmod mode fires the gunmod" ) {
        item shotgun( itype_u_shotgun );
        shotgun.ammo_set( itype_shot_00 );
        shotgun.set_flag( flag_NEVER_JAMS );
        REQUIRE( shotgun.ammo_remaining() == 2 );
        gun->force_insert_item( shotgun, pocket_type::MOD );

        const item *mod = nullptr;
        gun_mode mod_mode;
        for( const std::pair<const gun_mode_id, gun_mode> &m : gun->gun_all_modes() ) {
            if( m.second.target != gun.get_item() && m.second->typeId() == itype_u_shotgun ) {
                mod = m.second.target;
                mod_mode = m.second;
            }
        }
        REQUIRE( mod != nullptr );
        REQUIRE( mod->ammo_remaining() == 2 );

        const npc_attack_gun attack( *gun, mod_mode );
        attack.use( guy, zombie.pos_bub() );

        CHECK( mod->ammo_remaining() == 1 );
        CHECK( guy.get_wielded_item()->ammo_remaining() == rifle_ammo );
    }
}

// npc_attack_spell::can_use() rejected any spell louder than 4 for every NPC that has the
// use_silent rule, and the npc_follower_rules constructor gives that rule to every NPC.
// The gun checks only apply it to the player's followers, so hostile casters could never
// cast their stronger attack spells.
TEST_CASE( "npc-b03_hostile_npc_casts_loud_attack_spells", "[audit][npc_attack]" )
{
    npc &guy = audit_spawn_npc();
    guy.magic->learn_spell( spell_weaponpod_saw, guy, true );
    const spell &saw = guy.magic->get_spell( spell_weaponpod_saw );
    REQUIRE( saw.can_cast( guy ) );
    REQUIRE( saw.sound_volume( guy ) >= 5 );
    REQUIRE( guy.rules.has_flag( ally_rule::use_silent ) );

    monster &zombie = audit_spawn_zombie( audit_npc_pos + tripoint::east );
    const npc_attack_spell attack( spell_weaponpod_saw );

    SECTION( "a hostile NPC is not bound by the follower rule" ) {
        guy.set_attitude( NPCATT_KILL );
        REQUIRE( !guy.is_player_ally() );
        CHECK( attack.evaluate( guy, &zombie ).value() );
    }

    SECTION( "a follower told to stay silent still keeps quiet" ) {
        guy.set_fac( faction_your_followers );
        REQUIRE( guy.is_player_ally() );
        CHECK( !attack.evaluate( guy, &zombie ).value() );
    }
}

// calculate_spell_effect_area() stopped a blast spell's projectile on the last tile before
// a wall, but only used that epicenter for spells without an area: any real blast was
// centered on the original target, so a fireball aimed past a wall exploded behind it.
TEST_CASE( "magic-b02_blast_spell_explodes_short_of_a_wall", "[audit][magic]" )
{
    map &here = get_map();
    clear_avatar();
    clear_map();
    Character &caster = get_player_character();
    const tripoint_bub_ms caster_pos{ 60, 60, 0 };
    caster.setpos( here, caster_pos );

    // a wall 3 tiles east of the caster, much longer than the blast is wide
    const int wall_x = caster_pos.x() + 3;
    for( int dy = -10; dy <= 10; dy++ ) {
        here.ter_set( tripoint_bub_ms( wall_x, caster_pos.y() + dy, 0 ), ter_t_wall );
    }
    const tripoint_bub_ms behind_wall = caster_pos + tripoint_rel_ms( 6, 0, 0 );
    REQUIRE( here.impassable( tripoint_bub_ms( wall_x, caster_pos.y(), 0 ) ) );
    REQUIRE( !here.impassable( behind_wall ) );

    // a blast with a projectile (no NO_PROJECTILE flag) that can target the ground
    const spell sp( spell_spell_corrosive_spray );
    REQUIRE( sp.shape() == spell_shape::blast );
    REQUIRE( !sp.has_flag( spell_flag::NO_PROJECTILE ) );
    REQUIRE( sp.aoe( caster ) >= 1 );
    REQUIRE( sp.range( caster ) >= rl_dist( caster_pos, behind_wall ) );

    // the last tile before the wall
    const tripoint_bub_ms epicenter = caster_pos + tripoint_rel_ms( 2, 0, 0 );

    const std::set<tripoint_bub_ms> area = calculate_spell_effect_area( sp, behind_wall, caster );
    CHECK( area.count( epicenter ) == 1 );
    CHECK( area.count( behind_wall ) == 0 );
    for( const tripoint_bub_ms &p : area ) {
        CAPTURE( p );
        CHECK( p.x() < wall_x );
    }

    // A caster that isn't on the map, like the dummy a trap casts its spell with, has no
    // projectile path from where it stands: its blast still centers on the target.
    npc dummy;
    REQUIRE( !here.inbounds( dummy.pos_bub() ) );
    REQUIRE( sp.aoe( dummy ) >= 1 );
    const std::set<tripoint_bub_ms> dummy_area =
        calculate_spell_effect_area( sp, behind_wall, dummy );
    CHECK( dummy_area.count( behind_wall ) == 1 );
}

// damage_targets() passed the victim as the damage source on the multi-projectile,
// SPLIT_DAMAGE and PERCENTAGE_DAMAGE paths for characters, so the victim was its own
// attacker and killer: hurting or killing an NPC with such a spell (fireball is
// SPLIT_DAMAGE) had no consequences for the caster.
TEST_CASE( "magic-b03_split_and_percentage_spell_damage_credits_the_caster",
           "[audit][magic]" )
{
    npc &guy = audit_spawn_npc();
    avatar &caster = get_avatar();
    caster.setpos( get_map(), audit_npc_pos + tripoint::west );
    REQUIRE( !guy.guaranteed_hostile() );
    REQUIRE( !guy.hit_by_player );

    SECTION( "SPLIT_DAMAGE" ) {
        // a dodge only scales the damage down, but keep it out of the way
        guy.set_stamina( 0 );
        const spell sp( spell_boomer_claymore );
        REQUIRE( sp.has_flag( spell_flag::SPLIT_DAMAGE ) );
        REQUIRE( sp.damage( caster ) > 0 );
        const int hp_before = guy.get_hp();

        sp.cast_spell_effect( caster, guy.pos_bub() );

        REQUIRE( guy.get_hp() < hp_before );
        CHECK( guy.hit_by_player );
    }

    SECTION( "PERCENTAGE_DAMAGE" ) {
        const spell sp( spell_debug_kill_spell );
        REQUIRE( sp.has_flag( spell_flag::PERCENTAGE_DAMAGE ) );

        sp.cast_spell_effect( caster, guy.pos_bub() );

        REQUIRE( guy.is_dead_state() );
        CHECK( guy.get_killer() == &caster );
        CHECK( guy.hit_by_player );
    }

    SECTION( "a temporary caster is never kept as the killer" ) {
        // a spell cast with no caster (an item activated by nobody) is cast by a stack-local
        // fake avatar that is gone once the cast returns, before the NPC's death is handled
        const spell sp( spell_debug_kill_spell );

        sp.cast_spell_effect( guy.pos_bub() );

        REQUIRE( guy.is_dead_state() );
        CHECK( guy.get_killer() == nullptr );
    }
}

// min_pierce, pierce_increment and max_pierce (armor of the spell's damage type that it
// ignores) were loaded and documented, but get_damage_instance() never used them, so
// armor-piercing spells did full armor-mitigated damage.
TEST_CASE( "magic-b04_spell_pierce_becomes_armor_penetration", "[audit][magic]" )
{
    clear_avatar();
    Character &caster = get_player_character();
    // stab damage, min_pierce 3, pierce_increment 1, max_pierce 5, max_level 3
    spell sp( spell_weaponpod_saw );
    REQUIRE( sp.get_level() == 0 );

    const auto armor_penetration = [&sp, &caster]() {
        const damage_instance dmg = sp.get_damage_instance( caster );
        REQUIRE( dmg.damage_units.size() == 1 );
        return dmg.damage_units.front().res_pen;
    };

    CHECK( armor_penetration() == Approx( 3.0f ) );
    sp.set_level( caster, 1 );
    REQUIRE( sp.get_level() == 1 );
    CHECK( armor_penetration() == Approx( 4.0f ) );
    sp.set_level( caster, 3 );
    REQUIRE( sp.get_level() == 3 );
    // 3 + 3 * 1 is capped at max_pierce
    CHECK( armor_penetration() == Approx( 5.0f ) );

    // the projectile spell attacks hit with carries it too
    const dealt_projectile_attack atk =
        sp.get_projectile_attack( caster.pos_bub(), caster, caster );
    REQUIRE( atk.proj.impact.damage_units.size() == 1 );
    CHECK( atk.proj.impact.damage_units.front().res_pen == Approx( 5.0f ) );
}

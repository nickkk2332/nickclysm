#include <map>
#include <utility>

#include "cata_catch.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "flag.h"
#include "game.h"
#include "gun_mode.h"
#include "item.h"
#include "item_location.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "npc_attack.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "type_id.h"

class Creature;

static const faction_id faction_your_followers( "your_followers" );

static const gun_mode_id gun_mode_BURST( "BURST" );

static const itype_id itype_modular_m16a4( "modular_m16a4" );
static const itype_id itype_shot_00( "shot_00" );
static const itype_id itype_u_shotgun( "u_shotgun" );

static const mtype_id mon_zombie( "mon_zombie" );

static const spell_id spell_weaponpod_saw( "weaponpod_saw" );

static const string_id<npc_template> npc_template_test_talker( "test_talker" );

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

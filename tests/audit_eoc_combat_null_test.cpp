#include <memory>
#include <set>
#include <string>
#include <utility>

#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "projectile.h"
#include "string_formatter.h"
#include "talker.h"
#include "type_id.h"

static const ammo_effect_str_id ammo_effect_CORROSIVE( "CORROSIVE" );
static const ammo_effect_str_id ammo_effect_GENE_STING_BARB( "GENE_STING_BARB" );

static const field_type_str_id field_fd_acid( "fd_acid" );

static const mtype_id mon_zombie( "mon_zombie" );

static talk_effect_t load_test_effect( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    return talk_effect_t( jo, "effect", "audit_eoc_combat_null_test" );
}

// Runs u_spawn_monster with an empty id, which copies a hostile creature near the alpha talker
static void spawn_copy_hallucination( std::unique_ptr<talker> alpha, bool single_target,
                                      int target_range )
{
    const talk_effect_t effect = load_test_effect( string_format(
                                     R"({ "effect": { "u_spawn_monster": "", "single_target": %s, )"
                                     R"("hallucination_count": 1, "target_range": %d } })",
                                     single_target ? "true" : "false", target_range ) );
    dialogue d( std::move( alpha ), nullptr );
    effect.apply( d );
}

static int count_hallucinations_of( const mtype_id &type )
{
    int count = 0;
    for( monster &critter : g->all_monsters() ) {
        if( critter.is_hallucination() && critter.type->id == type ) {
            count++;
        }
    }
    return count;
}

static void setup_audit_map()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map_without_vision();
    clear_avatar();
    get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
}

static npc &spawn_hostile_npc( const point_bub_ms &where )
{
    npc &hostile = spawn_npc( where, "thug" );
    hostile.set_attitude( NPCATT_KILL );
    REQUIRE( get_avatar().attitude_to( hostile ) == Creature::Attitude::HOSTILE );
    return hostile;
}

// u_spawn_monster "" copies a random hostile creature near the talker; a hostile NPC used to be
// picked and dereferenced through as_monster(), which is null for an NPC.
TEST_CASE( "eoc-b01_spawn_monster_copy_ignores_hostile_npc", "[audit]" )
{
    setup_audit_map();
    avatar &u = get_avatar();
    spawn_hostile_npc( u.pos_bub().xy() + point( -5, 0 ) );

    SECTION( "only a hostile NPC in range: nothing is copied" ) {
        SECTION( "random pick from all hostiles" ) {
            spawn_copy_hallucination( get_talker_for( u ), false, 50 );
        }
        SECTION( "single_target" ) {
            spawn_copy_hallucination( get_talker_for( u ), true, 50 );
        }
        int hallucinations = 0;
        for( monster &critter : g->all_monsters() ) {
            if( critter.is_hallucination() ) {
                hallucinations++;
            }
        }
        CHECK( hallucinations == 0 );
    }

    SECTION( "hostile NPC and hostile monster in range: the monster is copied" ) {
        monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 5, 0 ) );
        REQUIRE( u.attitude_to( zed ) == Creature::Attitude::HOSTILE );
        SECTION( "random pick from all hostiles" ) {
            spawn_copy_hallucination( get_talker_for( u ), false, 50 );
        }
        SECTION( "single_target" ) {
            spawn_copy_hallucination( get_talker_for( u ), true, 50 );
        }
        CHECK( count_hallucinations_of( mon_zombie ) == 1 );
    }

    clear_creatures();
    clear_npcs();
}

// The search for a creature to copy is centred on the acting talker, not on the player.
TEST_CASE( "eoc-b01_spawn_monster_copy_searches_around_actor", "[audit]" )
{
    setup_audit_map();
    avatar &u = get_avatar();
    npc &actor = spawn_npc( u.pos_bub().xy() + point( 30, 0 ), "thug" );
    actor.set_attitude( NPCATT_NULL );
    monster &zed = spawn_test_monster( "mon_zombie", actor.pos_bub() + point( 3, 0 ) );
    REQUIRE( actor.attitude_to( zed ) == Creature::Attitude::HOSTILE );
    REQUIRE( rl_dist( u.pos_bub(), zed.pos_bub() ) > 10 );

    spawn_copy_hallucination( get_talker_for( actor ), false, 10 );

    CHECK( count_hallucinations_of( mon_zombie ) == 1 );

    clear_creatures();
    clear_npcs();
}

// apply_ammo_effects is called with a null source by SPECIAL_COOKOFF detonation and NULL_SOURCE
// projectiles; its spell and EoC branches used to dereference that null source.
TEST_CASE( "cmbt-b01_ammo_effects_without_source", "[audit]" )
{
    setup_audit_map();
    map &here = get_map();
    const tripoint_bub_ms impact = get_avatar().pos_bub() + point( 20, 0 );

    SECTION( "spell_data is cast without a caster" ) {
        REQUIRE( here.get_field_intensity( impact, field_fd_acid ) == 0 );
        apply_ammo_effects( nullptr, impact, { ammo_effect_CORROSIVE }, 1 );
        CHECK( here.get_field_intensity( impact, field_fd_acid ) > 0 );
    }

    SECTION( "eoc runs without a shooter and without a victim" ) {
        apply_ammo_effects( nullptr, impact, { ammo_effect_GENE_STING_BARB }, 1 );
        CHECK( get_creature_tracker().creature_at( impact ) == nullptr );
    }

    SECTION( "eoc runs without a shooter but with a victim" ) {
        monster &zed = spawn_test_monster( "mon_zombie", impact );
        apply_ammo_effects( nullptr, impact, { ammo_effect_GENE_STING_BARB }, 1 );
        CHECK( !zed.is_dead() );
    }

    clear_creatures();
    clear_map_without_vision();
}

// u_explosion's integer "shrapnel" form used to look for "casing_mass" instead and fail to load.
TEST_CASE( "cmbt-b01_explosion_integer_shrapnel_loads", "[audit]" )
{
    CHECK_NOTHROW( load_test_effect(
                       R"({ "effect": { "u_explosion": { "power": 0, "shrapnel": 7 } } })" ) );
}

// u_ranged_attack used to dereference a null Character for non-character attackers and an empty
// item_location when the attacker wields nothing.
TEST_CASE( "cmbt-b01_ranged_attack_without_character_or_gun", "[audit]" )
{
    setup_audit_map();
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 3, 0 ) );
    const talk_effect_t effect = load_test_effect( R"({ "effect": "u_ranged_attack" })" );

    SECTION( "monster attacker" ) {
        const int hp_before = u.get_hp();
        dialogue d( get_talker_for( zed ), get_talker_for( u ) );
        effect.apply( d );
        CHECK( u.get_hp() == hp_before );
    }

    SECTION( "character attacker wielding nothing" ) {
        u.remove_weapon();
        REQUIRE( !u.get_wielded_item() );
        const int hp_before = zed.get_hp();
        dialogue d( get_talker_for( u ), get_talker_for( zed ) );
        effect.apply( d );
        CHECK( zed.get_hp() == hp_before );
    }

    clear_creatures();
}

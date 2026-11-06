#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "character.h"
#include "character_attire.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "memory_fast.h"
#include "monster.h"
#include "npc.h"
#include "npc_attack.h"
#include "options_helpers.h"
#include "overmap_ui.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"

class Creature;

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_UPS_ON( "UPS_ON" );
static const itype_id itype_debug_backpack( "debug_backpack" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_laser_rifle( "laser_rifle" );

static const mtype_id mon_zombie( "mon_zombie" );

static const string_id<npc_template> npc_template_test_talker( "test_talker" );

static const weather_type_id weather_sunny( "sunny" );

static constexpr point_bub_ms audit_npc_start{ 50, 50 };
static constexpr tripoint_bub_ms audit_npc_start_tripoint{ audit_npc_start, 0 };

// A follower allowed to use loud guns, with a zombie 5 tiles away.
static npc &audit_spawn_gunner()
{
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    get_player_character().setpos( here, audit_npc_start_tripoint );
    clear_map_and_put_player_underground();
    clear_vehicles();
    REQUIRE( creatures.creature_at<Creature>( audit_npc_start_tripoint ) == nullptr );

    const character_id guy_id = here.place_npc( audit_npc_start, npc_template_test_talker );
    npc &guy = *g->find_npc( guy_id );
    clear_character( guy );
    guy.setpos( here, audit_npc_start_tripoint );
    g->load_npcs();
    REQUIRE( creatures.creature_at<npc>( audit_npc_start_tripoint ) != nullptr );

    guy.set_fac( faction_your_followers );
    guy.rules.set_flag( ally_rule::use_guns );
    guy.rules.clear_flag( ally_rule::use_silent );
    guy.worn.wear_item( guy, item( itype_debug_backpack ), false, false );
    REQUIRE( guy.is_wearing( itype_debug_backpack ) );
    return guy;
}

// Another follower, i.e. someone the gunner must not shoot through.
static npc &audit_spawn_buddy( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const character_id buddy_id = here.place_npc( pos.xy(), npc_template_test_talker );
    npc &buddy = *g->find_npc( buddy_id );
    clear_character( buddy );
    buddy.setpos( here, pos );
    g->load_npcs();
    REQUIRE( get_creature_tracker().creature_at<npc>( pos ) == &buddy );
    buddy.set_fac( faction_your_followers );
    return buddy;
}

static monster &audit_spawn_zombie()
{
    monster *zombie = g->place_critter_at( mon_zombie,
                                           audit_npc_start_tripoint + tripoint( 5, 0, 0 ) );
    REQUIRE( zombie != nullptr );
    return *zombie;
}

// npc_attack_gun::can_use() used to require item::has_ammo(), which is false for an
// empty gun, so the "reload first" branch of npc_attack_gun::use() was unreachable and
// an NPC with spare magazines clubbed enemies with its empty gun instead.
TEST_CASE( "npc-b01_npc_reloads_empty_gun_in_combat", "[audit][npc_attack]" )
{
    scoped_weather_override sunny_weather( weather_sunny );
    npc &guy = audit_spawn_gunner();

    guy.remove_weapon();
    guy.set_wielded_item( item( itype_glock_19 ) );
    item_location gun = guy.get_wielded_item();
    REQUIRE( gun );
    REQUIRE( gun->typeId() == itype_glock_19 );
    REQUIRE( gun->magazine_current() == nullptr );
    REQUIRE( gun->ammo_remaining() == 0 );

    monster &zombie = audit_spawn_zombie();

    SECTION( "with a loaded spare magazine the NPC picks the gun and reloads it" ) {
        item mag( itype_glockmag );
        mag.ammo_set( itype_9mm );
        REQUIRE( mag.ammo_remaining() > 0 );
        REQUIRE( guy.i_add( mag ) );
        REQUIRE( guy.can_reload_current() );

        // Nothing is targeted yet, so enough_time_to_reload() allows the reload.
        guy.evaluate_best_attack( &zombie );
        const std::shared_ptr<npc_attack> &attack = guy.get_current_attack();
        npc_attack_gun *gun_attack = dynamic_cast<npc_attack_gun *>( attack.get() );
        REQUIRE( gun_attack != nullptr );
        const npc_attack_rating &rating = guy.get_current_attack_evaluation();
        REQUIRE( rating.value() );
        CHECK( *rating.value() > 0 );
        CHECK( rating.target() == zombie.pos_bub() );

        gun_attack->use( guy, rating.target() );
        CHECK( guy.get_wielded_item()->ammo_remaining() > 0 );
    }

    SECTION( "without spare ammo the empty gun is not a ranged attack" ) {
        REQUIRE( !guy.can_reload_current() );
        npc_attack_gun gun_attack( *gun, gun->gun_current_mode() );
        CHECK( !gun_attack.evaluate( guy, &zombie ).value() );
    }
}

// The usual way a gun runs dry: it still holds its empty magazine and the NPC has an AI
// target. npc::enough_time_to_reload() used to cost that reload as a magazine's worth of
// loose rounds (15 x the gun's reload time), so against a zombie 5 tiles away the NPC
// never reloaded. do_reload() swaps in the spare magazine in about one reload time.
TEST_CASE( "npc-b01_npc_reloads_gun_shot_dry_while_targeted", "[audit][npc_attack]" )
{
    scoped_weather_override sunny_weather( weather_sunny );
    // Light the map before it is rebuilt, so the NPC can see the zombie and target it.
    set_time_to_day();
    npc &guy = audit_spawn_gunner();
    // With the player underground the follower would otherwise retreat towards them
    // instead of picking a target.
    guy.rules.clear_flag( ally_rule::follow_close );

    item glock( itype_glock_19 );
    REQUIRE( glock.put_in( item( itype_glockmag ), pocket_type::MAGAZINE_WELL ).success() );
    guy.remove_weapon();
    guy.set_wielded_item( glock );
    item_location gun = guy.get_wielded_item();
    REQUIRE( gun );
    REQUIRE( gun->magazine_current() != nullptr );
    REQUIRE( gun->magazine_current()->typeId() == itype_glockmag );
    REQUIRE( gun->ammo_remaining() == 0 );

    item spare( itype_glockmag );
    spare.ammo_set( itype_9mm );
    REQUIRE( spare.ammo_remaining() > 0 );
    REQUIRE( guy.i_add( spare ) );
    REQUIRE( guy.can_reload_current() );

    monster &zombie = audit_spawn_zombie();
    map &here = get_map();
    guy.recalc_sight_limits();
    REQUIRE( guy.sees( here, zombie ) );
    guy.regen_ai_cache();
    REQUIRE( guy.current_target() == static_cast<Creature *>( &zombie ) );

    CHECK( guy.enough_time_to_reload( *gun ) );
    // The gun is not empty() (it holds a magazine) but has no ammo_data(). The friendly
    // fire check, which evaluate() runs for a clear shot, used to dereference that.
    CHECK( guy.wont_hit_friend( zombie.pos_bub(), *gun, false ) );

    guy.evaluate_best_attack( &zombie );
    const std::shared_ptr<npc_attack> &attack = guy.get_current_attack();
    npc_attack_gun *gun_attack = dynamic_cast<npc_attack_gun *>( attack.get() );
    REQUIRE( gun_attack != nullptr );
    const npc_attack_rating &rating = guy.get_current_attack_evaluation();
    REQUIRE( rating.value() );
    CHECK( *rating.value() > 0 );
    CHECK( rating.target() == zombie.pos_bub() );

    gun_attack->use( guy, rating.target() );
    item_location reloaded = guy.get_wielded_item();
    REQUIRE( reloaded );
    CHECK( reloaded->ammo_remaining() > 0 );
}

// item::has_ammo() is also false for energy_drain guns with no magazine, so a UPS
// laser rifle was never usable as a ranged attack even with a charged UPS.
TEST_CASE( "npc-b01_npc_fires_ups_energy_gun", "[audit][npc_attack]" )
{
    scoped_weather_override sunny_weather( weather_sunny );
    // Light the map before it is rebuilt, so the NPC can see the ally in its line of fire.
    set_time_to_day();
    npc &guy = audit_spawn_gunner();

    guy.remove_weapon();
    guy.set_wielded_item( item( itype_laser_rifle ) );
    item_location gun = guy.get_wielded_item();
    REQUIRE( gun );
    REQUIRE( gun->typeId() == itype_laser_rifle );
    REQUIRE( !gun->has_ammo() );

    item ups( itype_UPS_ON );
    item ups_mag( ups.magazine_default() );
    ups_mag.ammo_set( ups_mag.ammo_default(), 400 );
    REQUIRE( ups.put_in( ups_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( guy.i_add( ups ) );
    REQUIRE( units::to_kilojoule( guy.available_ups() ) == 400 );
    REQUIRE( gun->ammo_sufficient( &guy ) );

    monster &zombie = audit_spawn_zombie();

    npc_attack_gun gun_attack( *gun, gun->gun_current_mode() );
    const npc_attack_rating rating = gun_attack.evaluate( guy, &zombie );
    REQUIRE( rating.value() );
    CHECK( *rating.value() > 0 );
    CHECK( rating.target() == zombie.pos_bub() );

    // An energy gun is empty(), and npc::wont_hit_friend() used to return true early for
    // any empty gun, so an ally with a UPS gun shot straight through its friends.
    npc &buddy = audit_spawn_buddy( audit_npc_start_tripoint + tripoint( 2, 0, 0 ) );
    map &here = get_map();
    guy.recalc_sight_limits();
    REQUIRE( guy.sees( here, buddy ) );
    guy.regen_ai_cache();
    const std::vector<weak_ptr_fast<Creature>> &friends = guy.get_cached_friends();
    REQUIRE( std::any_of( friends.begin(), friends.end(),
    [&buddy]( const weak_ptr_fast<Creature> &fr ) {
        return fr.lock().get() == static_cast<Creature *>( &buddy );
    } ) );
    item_location laser = guy.get_wielded_item();
    REQUIRE( laser );
    REQUIRE( laser->empty() );
    CHECK( !guy.wont_hit_friend( zombie.pos_bub(), *laser, false ) );
}

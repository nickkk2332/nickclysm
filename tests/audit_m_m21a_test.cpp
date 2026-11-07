#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "auto_pickup.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_attire.h"
#include "character_id.h"
#include "color.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "explosion.h"
#include "faction.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json_loader.h"
#include "line.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "npc_attack.h"
#include "npc_class.h"
#include "options_helpers.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "stomach.h"
#include "type_id.h"
#include "vitamin.h"

static const bionic_id bio_flashlight( "bio_flashlight" );

static const faction_id faction_your_followers( "your_followers" );

static const field_type_str_id field_fd_fire( "fd_fire" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_audit_m21a_hot_potato( "audit_m21a_hot_potato" );
static const itype_id itype_audit_m21a_trinket_cheap( "audit_m21a_trinket_cheap" );
static const itype_id itype_audit_m21a_trinket_dear( "audit_m21a_trinket_dear" );
static const itype_id itype_debug_backpack( "debug_backpack" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_machete( "machete" );

static const mtype_id mon_zombie( "mon_zombie" );

static const npc_class_id npc_class_audit_m21a_child( "audit_m21a_child" );
static const npc_class_id npc_class_audit_m21a_child_edit( "audit_m21a_child_edit" );
static const npc_class_id npc_class_audit_m21a_parent( "audit_m21a_parent" );

static const spell_id spell_test_spell_pew( "test_spell_pew" );
static const spell_id spell_test_spell_random_damage( "test_spell_random_damage" );

static const string_id<npc_template> npc_template_test_talker( "test_talker" );

static const ter_str_id ter_t_wall( "t_wall" );

static const vitamin_id vitamin_calcium( "calcium" );
static const vitamin_id vitamin_vitC( "vitC" );

static const weather_type_id weather_sunny( "sunny" );

static constexpr tripoint_bub_ms m21a_pos{ 50, 50, 0 };

// A lit, empty map with the avatar out of the way underground.
static void m21a_clear_world()
{
    clear_avatar();
    set_time_to_day();
    clear_map_and_put_player_underground();
    clear_vehicles();
}

// An NPC with no gear at pos.
static npc &m21a_place_npc( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    REQUIRE( creatures.creature_at<Creature>( pos ) == nullptr );
    const character_id guy_id = here.place_npc( pos.xy(), npc_template_test_talker );
    npc &guy = *g->find_npc( guy_id );
    clear_character( guy );
    guy.setpos( here, pos );
    g->load_npcs();
    REQUIRE( creatures.creature_at<npc>( pos ) == &guy );
    return guy;
}

static npc &m21a_place_follower( const tripoint_bub_ms &pos )
{
    npc &guy = m21a_place_npc( pos );
    guy.set_fac( faction_your_followers );
    REQUIRE( guy.is_player_ally() );
    return guy;
}

static monster &m21a_place_zombie( const tripoint_bub_ms &pos )
{
    monster *zombie = g->place_critter_at( mon_zombie, pos );
    REQUIRE( zombie != nullptr );
    return *zombie;
}

// A glock with a magazine holding the given number of rounds.
static item m21a_loaded_glock( const int rounds )
{
    item gun( itype_glock_19 );
    item mag( itype_glockmag );
    mag.ammo_set( itype_9mm, rounds );
    REQUIRE( gun.put_in( mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.ammo_remaining() == rounds );
    return gun;
}

namespace
{
struct m21a_event_log : event_subscriber {
    using event_subscriber::notify;
    std::vector<cata::event> events;
    void notify( const cata::event &e ) override {
        events.push_back( e );
    }
    std::vector<cata::event> of_type( const event_type type ) const {
        std::vector<cata::event> ret;
        for( const cata::event &e : events ) {
            if( e.type() == type ) {
                ret.push_back( e );
            }
        }
        return ret;
    }
};
} // namespace

// NPC-B05: the barehanded attack is npc_attack_melee( null_item_reference() ), rated by the
// null item's own melee damage (0) less a wield penalty (is_wielding() never matches the null
// item), so it was never chosen and an unarmed NPC stood "undecided" next to its enemy.
TEST_CASE( "npc-b05_unarmed_npc_punches", "[audit][npc_attack]" )
{
    m21a_clear_world();
    npc &guy = m21a_place_npc( m21a_pos );
    REQUIRE_FALSE( guy.is_armed() );
    monster &zombie = m21a_place_zombie( m21a_pos + tripoint::east );

    guy.evaluate_best_attack( &zombie );
    const std::shared_ptr<npc_attack> attack = guy.get_current_attack();
    REQUIRE( attack != nullptr );
    CHECK( dynamic_cast<const npc_attack_melee *>( attack.get() ) != nullptr );
    const npc_attack_rating rating = guy.get_current_attack_evaluation();
    REQUIRE( rating.value() );
    CHECK( *rating.value() > 0 );
    CHECK( rating.target() == zombie.pos_bub() );

    // and it punches instead of "wielding" its fists
    m21a_event_log log;
    get_event_bus().subscribe( &log );
    guy.set_moves( 100 );
    attack->use( guy, rating.target() );
    const std::vector<cata::event> swings = log.of_type(
            event_type::character_melee_attacks_monster );
    // a technique may add a follow-up swing
    REQUIRE_FALSE( swings.empty() );
    for( const cata::event &swing : swings ) {
        CHECK( swing.get<character_id>( "attacker" ) == guy.getID() );
        CHECK( swing.get<itype_id>( "weapon" ).is_null() );
    }
    CHECK_FALSE( guy.is_armed() );
}

// The punch mustn't win over a real weapon in hand.
TEST_CASE( "npc-b05_armed_npc_keeps_its_weapon", "[audit][npc_attack]" )
{
    m21a_clear_world();
    npc &guy = m21a_place_npc( m21a_pos );
    guy.set_wielded_item( item( itype_machete ) );
    REQUIRE( guy.get_wielded_item() );
    monster &zombie = m21a_place_zombie( m21a_pos + tripoint::east );

    guy.evaluate_best_attack( &zombie );
    const std::shared_ptr<npc_attack> attack = guy.get_current_attack();
    REQUIRE( attack != nullptr );
    const npc_attack_rating rating = guy.get_current_attack_evaluation();
    REQUIRE( rating.value() );

    m21a_event_log log;
    get_event_bus().subscribe( &log );
    guy.set_moves( 100 );
    attack->use( guy, rating.target() );
    const std::vector<cata::event> swings = log.of_type(
            event_type::character_melee_attacks_monster );
    REQUIRE_FALSE( swings.empty() );
    for( const cata::event &swing : swings ) {
        CHECK( swing.get<itype_id>( "weapon" ) == itype_machete );
    }
    REQUIRE( guy.get_wielded_item() );
    CHECK( guy.get_wielded_item()->typeId() == itype_machete );
}

// NPC-B06: evaluate_best_attack() sent opens_spellbook only when the NPC had no spells, so
// casters never got their EOC adjustments and every mundane NPC sent it on every decision.
TEST_CASE( "npc-b06_npc_casters_open_their_spellbook_once_a_turn", "[audit][npc_attack][magic]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    m21a_clear_world();
    npc &caster = m21a_place_npc( m21a_pos );
    caster.magic->learn_spell( spell_test_spell_pew, caster, true );
    REQUIRE( caster.magic->knows_spell( spell_test_spell_pew ) );
    npc &mundane = m21a_place_npc( m21a_pos + tripoint( 0, 3, 0 ) );
    REQUIRE( mundane.magic->spells().empty() );
    monster &zombie = m21a_place_zombie( m21a_pos + tripoint( 4, 0, 0 ) );

    m21a_event_log log;
    get_event_bus().subscribe( &log );
    const auto opened_by = [&log]( const npc & who ) {
        int count = 0;
        for( const cata::event &e : log.of_type( event_type::opens_spellbook ) ) {
            if( e.get<character_id>( "character" ) == who.getID() ) {
                ++count;
            }
        }
        return count;
    };

    caster.evaluate_best_attack( &zombie );
    CHECK( opened_by( caster ) == 1 );
    // again in the same turn: once a turn is enough
    caster.evaluate_best_attack( &zombie );
    CHECK( opened_by( caster ) == 1 );
    calendar::turn += 1_turns;
    caster.evaluate_best_attack( &zombie );
    CHECK( opened_by( caster ) == 2 );

    mundane.evaluate_best_attack( &zombie );
    mundane.evaluate_best_attack( &zombie );
    CHECK( opened_by( mundane ) == 0 );
}

// The blast radius of the test explosive, read as the NPC code reads it.
static int m21a_hot_potato_safe_range()
{
    const item potato( itype_audit_m21a_hot_potato );
    const explosion_iuse *actor = dynamic_cast<const explosion_iuse *>
                                  ( potato.type->countdown_action.get_actor_ptr() );
    REQUIRE( actor != nullptr );
    return actor->explosion.safe_range();
}

// NPC-B07: for an NPC_THROW_NOW item, npc_attack_throw::evaluate() returned the first tile of
// its range, always the north-west corner, whoever stood there.
TEST_CASE( "npc-b07_hot_potato_is_thrown_clear_of_the_thrower_and_allies", "[audit][npc_attack]" )
{
    scoped_weather_override sunny_weather( weather_sunny );
    m21a_clear_world();
    npc &guy = m21a_place_follower( m21a_pos );
    guy.set_wielded_item( item( itype_audit_m21a_hot_potato ) );
    item_location potato = guy.get_wielded_item();
    REQUIRE( potato );
    REQUIRE( potato->has_flag( flag_NPC_THROW_NOW ) );
    const int safe = m21a_hot_potato_safe_range();
    REQUIRE( safe >= 1 );
    const int range = guy.throw_range( *potato );
    REQUIRE( range > 2 * safe + 2 );
    const tripoint_bub_ms nw_corner = m21a_pos + tripoint( -range, -range, 0 );

    SECTION( "with an ally next to the north-west corner" ) {
        npc &buddy = m21a_place_follower( nw_corner + tripoint( 1, 1, 0 ) );
        const npc_attack_throw attack( *potato, nullptr, guy.speed_rating() );
        const npc_attack_rating rating = attack.evaluate( guy, nullptr );
        REQUIRE( rating.value() );
        const tripoint_bub_ms target = rating.target();
        CAPTURE( target, buddy.pos_bub() );
        CHECK( target != nw_corner );
        CHECK( rl_dist( target, buddy.pos_bub() ) >= safe );
        CHECK( rl_dist( target, guy.pos_bub() ) >= safe );
    }

    SECTION( "with an enemy in range" ) {
        monster &zombie = m21a_place_zombie( m21a_pos + tripoint( range - 2, 0, 0 ) );
        const npc_attack_throw attack( *potato, nullptr, guy.speed_rating() );
        const npc_attack_rating rating = attack.evaluate( guy, nullptr );
        REQUIRE( rating.value() );
        CAPTURE( rating.target(), zombie.pos_bub() );
        CHECK( rl_dist( rating.target(), zombie.pos_bub() ) < safe );
    }

    // never take the "find a clearer line" detour with it in hand
    SECTION( "it is thrown even when the line is blocked" ) {
        guy.rules.set_flag( ally_rule::avoid_friendly_fire );
        map &here = get_map();
        const tripoint_bub_ms wall_pos = m21a_pos + tripoint( 2, 0, 0 );
        const ter_id ground = here.ter( wall_pos );
        const on_out_of_scope restore_ground( [&here, wall_pos, ground]() {
            here.ter_set( wall_pos, ground );
        } );
        here.ter_set( wall_pos, ter_t_wall );
        REQUIRE( here.impassable( wall_pos ) );
        const npc_attack_throw attack( *potato, nullptr, guy.speed_rating() );
        guy.set_moves( 100 );
        attack.use( guy, m21a_pos + tripoint( 4, 0, 0 ) );
        CHECK_FALSE( guy.is_armed() );
        CHECK( guy.pos_bub() == m21a_pos );
    }
    // the inert test explosive may lie on the map
    clear_items( 0 );
}

// NPC-B08: npc_attack_gun::base_time_penalty() charged the "must wield it first" penalty to the
// gun already wielded, so a spare copy always rated higher: the NPC wielded it, and the next
// turn the other one, and never fired.
TEST_CASE( "npc-b08_npc_fires_its_wielded_gun_instead_of_swapping", "[audit][npc_attack]" )
{
    scoped_weather_override sunny_weather( weather_sunny );
    m21a_clear_world();
    npc &guy = m21a_place_follower( m21a_pos );
    guy.rules.set_flag( ally_rule::use_guns );
    guy.rules.clear_flag( ally_rule::use_silent );
    guy.worn.wear_item( guy, item( itype_debug_backpack ), false, false );
    guy.set_wielded_item( m21a_loaded_glock( 15 ) );
    item_location wielded = guy.get_wielded_item();
    REQUIRE( wielded );
    item_location spare = guy.i_add( m21a_loaded_glock( 5 ) );
    REQUIRE( spare );
    REQUIRE( spare->ammo_remaining() == 5 );
    monster &zombie = m21a_place_zombie( m21a_pos + tripoint::east );

    const npc_attack_gun wielded_attack( *wielded, wielded->gun_current_mode() );
    const npc_attack_gun spare_attack( *spare, spare->gun_current_mode() );
    const npc_attack_rating wielded_rating = wielded_attack.evaluate( guy, &zombie );
    const npc_attack_rating spare_rating = spare_attack.evaluate( guy, &zombie );
    REQUIRE( wielded_rating.value() );
    REQUIRE( spare_rating.value() );
    CHECK( *wielded_rating.value() > *spare_rating.value() );

    guy.evaluate_best_attack( &zombie );
    const std::shared_ptr<npc_attack> attack = guy.get_current_attack();
    REQUIRE( attack != nullptr );
    REQUIRE( dynamic_cast<const npc_attack_gun *>( attack.get() ) != nullptr );
    guy.set_moves( 100 );
    attack->use( guy, guy.get_current_attack_evaluation().target() );
    // it fired (or aimed) the gun in hand, and didn't swap in the spare
    REQUIRE( guy.get_wielded_item() );
    CHECK( guy.get_wielded_item()->ammo_remaining() > 5 );
}

// NPC-B09: with nobody hostile in sight, assess_danger() returned before committing the threat
// map, so nearby fire never counted and the last fight's threats stayed for good.
TEST_CASE( "npc-b09_threat_map_counts_fire_with_no_enemy_in_sight", "[audit][npc]" )
{
    m21a_clear_world();
    npc &guy = m21a_place_npc( m21a_pos );
    map &here = get_map();
    const tripoint_bub_ms fire_pos = m21a_pos + tripoint( 3, 0, 0 );
    REQUIRE( here.add_field( fire_pos, field_fd_fire, 1, 0_turns, false ) );
    const on_out_of_scope put_out( [&here, fire_pos]() {
        here.remove_field( fire_pos, field_fd_fire );
    } );

    guy.regen_ai_cache();
    REQUIRE( guy.current_target() == nullptr );
    const float with_fire = guy.get_ai_threat( direction::EAST );
    CHECK( with_fire > 0.0f );
    CHECK( guy.get_ai_threat( direction::WEST ) == 0.0f );

    // and it fades once the fire is gone
    here.remove_field( fire_pos, field_fd_fire );
    guy.regen_ai_cache();
    const float after = guy.get_ai_threat( direction::EAST );
    CHECK( after < with_fire );
    CHECK( after > 0.0f );
}

static void m21a_put_item( const tripoint_bub_ms &p, const itype_id &type )
{
    get_map().add_item_or_charges( p, item( type ) );
}

// NPC-B10: find_item() kept the last acceptable item it saw. Tiles are scanned nearest first,
// so that was the farthest one, however little it was worth.
TEST_CASE( "npc-b10_npc_fetches_the_best_item_not_the_farthest", "[audit][npc]" )
{
    scoped_weather_override sunny_weather( weather_sunny );
    m21a_clear_world();
    map &here = get_map();
    const tripoint_bub_ms near_pos = m21a_pos + tripoint::east;
    const tripoint_bub_ms far_pos = m21a_pos + tripoint( 4, 0, 0 );
    const auto prepare = [&here, near_pos, far_pos]( npc & guy ) {
        guy.worn.wear_item( guy, item( itype_debug_backpack ), false, false );
        here.invalidate_map_cache( 0 );
        here.build_map_cache( 0 );
        guy.recalc_sight_limits();
        REQUIRE( guy.sees( here, near_pos ) );
        REQUIRE( guy.sees( here, far_pos ) );
    };

    SECTION( "the dearer item is nearer" ) {
        npc &guy = m21a_place_npc( m21a_pos );
        m21a_put_item( near_pos, itype_audit_m21a_trinket_dear );
        m21a_put_item( far_pos, itype_audit_m21a_trinket_cheap );
        prepare( guy );
        guy.find_item();
        REQUIRE( guy.wanted_item );
        CHECK( guy.wanted_item->typeId() == itype_audit_m21a_trinket_dear );
        CHECK( guy.wanted_item_pos == near_pos );
    }
    SECTION( "the dearer item is farther" ) {
        npc &guy = m21a_place_npc( m21a_pos );
        m21a_put_item( near_pos, itype_audit_m21a_trinket_cheap );
        m21a_put_item( far_pos, itype_audit_m21a_trinket_dear );
        prepare( guy );
        guy.find_item();
        REQUIRE( guy.wanted_item );
        CHECK( guy.wanted_item->typeId() == itype_audit_m21a_trinket_dear );
        CHECK( guy.wanted_item_pos == far_pos );
    }
    SECTION( "of whitelisted items, the nearest" ) {
        npc &guy = m21a_place_follower( m21a_pos );
        REQUIRE_FALSE( guy.is_walking_with() );
        guy.rules.set_flag( ally_rule::allow_pick_up );
        guy.rules.pickup_whitelist->deserialize( json_loader::from_string(
                    R"([{ "rule": "*test trinket*", "active": true, "exclude": false }])" ).get_array() );
        REQUIRE( guy.has_item_whitelist() );
        m21a_put_item( near_pos, itype_audit_m21a_trinket_cheap );
        m21a_put_item( far_pos, itype_audit_m21a_trinket_dear );
        prepare( guy );
        guy.find_item();
        REQUIRE( guy.wanted_item );
        CHECK( guy.wanted_item_pos == near_pos );
    }
    clear_items( 0 );
}

// NPC-B11: npc_class load() set every bonus_* from the JSON, a missing one to 0, so a copy-from
// class lost the bonuses it didn't restate; a copied spell's level couldn't be changed either.
TEST_CASE( "npc-b11_npc_class_copy_keeps_what_it_does_not_restate", "[audit][npc]" )
{
    const npc_class &parent = npc_class_audit_m21a_parent.obj();
    CHECK( parent.roll_strength() == 7 );
    CHECK( parent.roll_dexterity() == 0 );
    CHECK( parent.roll_perception() == -2 );
    CHECK( parent.roll_bravery() == 3 );
    CHECK( parent._starting_spells.size() == 2 );

    const npc_class &child = npc_class_audit_m21a_child.obj();
    CHECK( child.roll_strength() == 7 );
    CHECK( child.roll_dexterity() == 1 );
    CHECK( child.roll_perception() == -2 );
    CHECK( child.roll_bravery() == 3 );
    CHECK( child._starting_spells == parent._starting_spells );
    CHECK( child.bionic_list == parent.bionic_list );

    const npc_class &edit = npc_class_audit_m21a_child_edit.obj();
    CHECK( edit.roll_strength() == 2 );
    CHECK( edit.roll_perception() == -2 );
    REQUIRE( edit._starting_spells.count( spell_test_spell_pew ) == 1 );
    CHECK( edit._starting_spells.at( spell_test_spell_pew ) == 5 );
    CHECK( edit._starting_spells.count( spell_test_spell_random_damage ) == 0 );
    CHECK( edit.bionic_list.count( bio_flashlight ) == 0 );
    CHECK( parent.bionic_list.count( bio_flashlight ) == 1 );

    const auto rejects = []( const std::string & json, const std::string & why ) {
        CAPTURE( json );
        npc_class fresh;
        const JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        CHECK_THROWS_WITH( fresh.load( jo, "audit_m21a" ), Catch::Matchers::Contains( why ) );
    };
    rejects( R"({ "name": "x", "job_description": "y", "work_hours": [ 25, 6 ] })",
             "work hours must be from 0 to 24" );
    rejects( R"({ "name": "x", "job_description": "y", "bionics": [ { "id": "bio_flashlight", "chance": 101 } ] })",
             "chance must be from 0 to 100" );
}

// NPC-B12: days of camp food were kcal / ( size * 288 ), the old per-day nutrition unit, about
// 10 times too many, and the "worst" vitamin was the best one.
TEST_CASE( "npc-b12_camp_food_supply_counts_days_of_3000_kcal", "[audit][faction]" )
{
    faction fac;
    REQUIRE( fac.id != faction_your_followers );
    fac.size = 2;
    nutrients food;
    // a week for two
    food.calories = static_cast<int64_t>( 7 * 2 * 3000 ) * 1000;
    food.set_vitamin( vitamin_calcium, 2 * vitamin_calcium->units_absorption_per_day() * 14 );
    food.set_vitamin( vitamin_vitC, vitamin_vitC->units_absorption_per_day() * 14 / 10 );
    fac.add_to_food_supply( { { calendar::turn_zero, food } } );
    REQUIRE( fac.food_supply().kcal() == 42000 );

    CHECK( fac.food_supply_text() == "Scrapping By" );
    CHECK( fac.food_supply_color() == c_yellow );
    // a size of 0 counts as one, instead of dividing by zero
    fac.size = 0;
    CHECK( fac.food_supply_text() == "Well-Stocked" );

    // 14 days of food: calcium for 28 days, vitamin C for 1.4
    const std::pair<nc_color, std::string> worst = fac.vitamin_stores( vitamin_type::VITAMIN );
    CAPTURE( worst.second );
    CHECK( worst.second.find( vitamin_vitC->name() ) != std::string::npos );
    CHECK( worst.first == c_red );
}

// NPC-B13: avoid_friendly_fire() moved away from the midpoint of its friends, and with none
// (a lone gunman behind a closed window) that midpoint divided 0 by 0.
TEST_CASE( "npc-b13_lone_npc_steps_to_a_clear_line_of_fire", "[audit][npc]" )
{
    scoped_weather_override sunny_weather( weather_sunny );
    m21a_clear_world();
    // set up as for npc-b01's gunner, which targets a zombie 5 tiles away
    npc &guy = m21a_place_follower( m21a_pos );
    guy.rules.set_flag( ally_rule::use_guns );
    guy.rules.clear_flag( ally_rule::use_silent );
    monster &zombie = m21a_place_zombie( m21a_pos + tripoint( 5, 0, 0 ) );
    map &here = get_map();
    guy.recalc_sight_limits();
    REQUIRE( guy.sees( here, zombie ) );
    guy.regen_ai_cache();
    REQUIRE( guy.current_target() == static_cast<Creature *>( &zombie ) );
    REQUIRE( guy.get_cached_friends().empty() );

    guy.set_moves( 100 );
    guy.avoid_friendly_fire();
    const tripoint_bub_ms now = guy.pos_bub();
    CHECK( rl_dist( now, m21a_pos ) == 1 );
    CHECK( rl_dist( now, zombie.pos_bub() ) == 4 );
    std::vector<tripoint_bub_ms> line = line_to( now, zombie.pos_bub() );
    line.pop_back();
    for( const tripoint_bub_ms &p : line ) {
        CHECK_FALSE( here.impassable( p ) );
        CHECK( get_creature_tracker().creature_at( p ) == nullptr );
    }
}

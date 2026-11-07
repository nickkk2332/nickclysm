#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "magic_spell_effect_helpers.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_iterator.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "options_helpers.h"
#include "overmap_ui.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const efftype_id effect_magic_channeling( "magic_channeling" );

static const mtype_id mon_test_non_shearable( "mon_test_non_shearable" );
static const mtype_id mon_zombie( "mon_zombie" );

static const spell_id spell_audit_m27a_capped_spell( "audit_m27a_capped_spell" );
static const spell_id spell_audit_m27a_channel( "audit_m27a_channel" );
// no JSON: a spell_migration replaces it (MAGIC-B07)
static const spell_id spell_audit_m27a_dropped_spell( "audit_m27a_dropped_spell" );
static const spell_id spell_audit_m27a_falling( "audit_m27a_falling" );
static const spell_id spell_audit_m27a_healing( "audit_m27a_healing" );
static const spell_id spell_audit_m27a_menu_spell( "audit_m27a_menu_spell" );
static const spell_id spell_audit_m27a_min_only( "audit_m27a_min_only" );
static const spell_id spell_audit_m27a_npc_bolt( "audit_m27a_npc_bolt" );
static const spell_id spell_audit_m27a_pain_split( "audit_m27a_pain_split" );
static const spell_id spell_audit_m27a_random_critter( "audit_m27a_random_critter" );
static const spell_id spell_audit_m27a_random_self( "audit_m27a_random_self" );
static const spell_id spell_audit_m27a_random_zap( "audit_m27a_random_zap" );
// no JSON: a spell_migration replaces it (MAGIC-B07)
static const spell_id spell_audit_m27a_removed_spell( "audit_m27a_removed_spell" );
static const spell_id spell_audit_m27a_rising( "audit_m27a_rising" );
static const spell_id spell_audit_m27a_summon( "audit_m27a_summon" );
static const spell_id spell_audit_m27a_swap( "audit_m27a_swap" );
static const spell_id spell_audit_m27a_weak_bolt( "audit_m27a_weak_bolt" );

static const trait_id trait_TEST_AUDIT_M27A_SPELL_TRAIT( "TEST_AUDIT_M27A_SPELL_TRAIT" );

static const weather_type_id weather_clear( "clear" );

namespace
{

// A cleared map with the avatar at @avatar_pos, nothing else on it.
void m27a_clear_world( const tripoint_bub_ms &avatar_pos )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    get_globals().clear_global_values();
    get_avatar().setpos( get_map(), avatar_pos );
}

npc &m27a_spawn_npc( const tripoint_bub_ms &pos )
{
    npc &guy = spawn_npc( pos.xy(), "test_talker" );
    clear_character( guy );
    guy.setpos( get_map(), pos );
    REQUIRE( get_creature_tracker().creature_at<npc>( pos ) == &guy );
    return guy;
}

monster &m27a_spawn_monster( const mtype_id &type, const tripoint_bub_ms &pos )
{
    monster *const mon = g->place_critter_at( type, pos );
    REQUIRE( mon != nullptr );
    return *mon;
}

bool avatar_was_told( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

// Who the character_forgets_spell events were about, in order
struct forget_recorder : public event_subscriber {
    std::vector<character_id> forgetters;

    forget_recorder() {
        get_event_bus().subscribe( this );
    }
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::character_forgets_spell ) {
            forgetters.push_back( e.get<cata_variant_type::character_id>( "character" ) );
        }
    }
};

int zaps_of( const Creature &critter )
{
    const diag_value *zapped = critter.maybe_get_value( "audit_m27a_zapped" );
    return zapped == nullptr ? 0 : static_cast<int>( zapped->dbl() );
}

} // namespace

// MAGIC-B05: damage() took the rising branch whenever min_damage >= 0, so a value that falls
// with level sat at max_damage from level 0; accuracy and damage over time had the same
// test, and field intensity always took the smaller value.
TEST_CASE( "magic-b05_falling_spell_values_stop_at_their_max", "[audit][magic]" )
{
    clear_avatar();
    avatar &u = get_avatar();

    SECTION( "values that fall with level start at their minimum and stop at their max" ) {
        const spell level_0( spell_audit_m27a_falling, 0, 0 );
        CHECK( level_0.damage( u ) == 10 );
        CHECK( level_0.accuracy( u ) == 10 );
        CHECK( level_0.damage_dot( u ) == Approx( 4.0 ) );
        CHECK( level_0.field_intensity( u ) == 5 );

        const spell level_1( spell_audit_m27a_falling, 0, 1 );
        CHECK( level_1.damage( u ) == 8 );
        CHECK( level_1.accuracy( u ) == 5 );
        CHECK( level_1.damage_dot( u ) == Approx( 3.0 ) );
        CHECK( level_1.field_intensity( u ) == 4 );

        const spell level_10( spell_audit_m27a_falling, 0, 10 );
        CHECK( level_10.damage( u ) == 2 );
        CHECK( level_10.accuracy( u ) == 0 );
        CHECK( level_10.damage_dot( u ) == Approx( 1.0 ) );
        CHECK( level_10.field_intensity( u ) == 1 );
    }

    SECTION( "rising values and growing healing are as before" ) {
        CHECK( spell( spell_audit_m27a_rising, 0, 0 ).damage( u ) == 2 );
        CHECK( spell( spell_audit_m27a_rising, 0, 3 ).damage( u ) == 8 );
        CHECK( spell( spell_audit_m27a_rising, 0, 10 ).damage( u ) == 10 );
        CHECK( spell( spell_audit_m27a_rising, 0, 0 ).field_intensity( u ) == 1 );
        CHECK( spell( spell_audit_m27a_rising, 0, 2 ).field_intensity( u ) == 3 );
        CHECK( spell( spell_audit_m27a_rising, 0, 10 ).field_intensity( u ) == 4 );

        CHECK( spell( spell_audit_m27a_healing, 0, 0 ).damage( u ) == -2 );
        CHECK( spell( spell_audit_m27a_healing, 0, 3 ).damage( u ) == -8 );
        CHECK( spell( spell_audit_m27a_healing, 0, 10 ).damage( u ) == -10 );
    }

    SECTION( "a spell with only min_damage deals it, as one with only min_range has that range" ) {
        CHECK( spell( spell_audit_m27a_min_only, 0, 0 ).damage( u ) == 7 );
        CHECK( spell( spell_audit_m27a_min_only, 0, 5 ).damage( u ) == 7 );
    }
}

// MAGIC-B06: `const int spell_block = block - accuracy > 0` made the margin 0 or 1, the
// formula (1 - 0.05 * x) / 3 shrank as the margin grew and went negative past 20, and the
// dodge part ran a real dodge_check() (uncanny dodge, dodge attempts, "conserve stamina").
TEST_CASE( "magic-b06_spell_defences_mitigate_by_their_margin", "[audit][magic]" )
{
    SECTION( "a defence not above the accuracy takes nothing off" ) {
        CHECK( spell_defence_mitigation( 10, 20, 20 ) == Approx( 0.0 ) );
        CHECK( spell_defence_mitigation( 20, 20, 20 ) == Approx( 0.0 ) );
    }
    SECTION( "the margin caps the roll" ) {
        CHECK( spell_defence_mitigation( 25, 20, 20 ) == Approx( 0.05 * 5 / 3.0 ) );
        CHECK( spell_defence_mitigation( 25, 20, 2 ) == Approx( 0.05 * 2 / 3.0 ) );
    }
    SECTION( "a large margin takes at most a third, never adding damage" ) {
        CHECK( spell_defence_mitigation( 50, 20, 20 ) == Approx( 1.0 / 3.0 ) );
        CHECK( spell_defence_mitigation( 200, -100, 20 ) == Approx( 1.0 / 3.0 ) );
        for( int roll = 1; roll <= 20; ++roll ) {
            for( int defence = -30; defence <= 60; defence += 5 ) {
                const double taken = spell_defence_mitigation( defence, 0, roll );
                CHECK( taken >= 0.0 );
                CHECK( taken <= 1.0 / 3.0 + 1e-9 );
            }
        }
    }
    SECTION( "a spell hit doesn't make its target try to dodge" ) {
        const tripoint_bub_ms here_pos( 60, 60, 0 );
        m27a_clear_world( here_pos );
        avatar &u = get_avatar();
        npc &caster = m27a_spawn_npc( here_pos + point( 3, 0 ) );
        const int dodges = u.get_dodges_left();
        const int free_dodges = u.get_free_dodges_left();
        int hp_before = 0;
        for( const bodypart_id &bp : u.get_all_body_parts( get_body_part_flags::only_main ) ) {
            hp_before += u.get_part_hp_cur( bp );
        }
        Messages::clear_messages();

        const spell bolt( spell_audit_m27a_weak_bolt );
        bolt.cast_all_effects( caster, u.pos_bub() );

        int hp_after = 0;
        for( const bodypart_id &bp : u.get_all_body_parts( get_body_part_flags::only_main ) ) {
            hp_after += u.get_part_hp_cur( bp );
        }
        // the bolt did hit
        CHECK( hp_after < hp_before );
        CHECK_FALSE( avatar_was_told( "conserve stamina" ) );
        CHECK( u.get_dodges_left() == dodges );
        CHECK( u.get_free_dodges_left() == free_dodges );
        Messages::clear_messages();
        clear_npcs();
    }
}

// MAGIC-B07: known_magic::deserialize dropped unknown spell ids before migrate_spells() ran,
// so a spell_migration from a removed spell never fired, and hotkeys and favorites stayed on
// the old id.
TEST_CASE( "magic-b07_spell_migration_from_a_removed_spell", "[audit][magic]" )
{
    REQUIRE_FALSE( spell_audit_m27a_removed_spell.is_valid() );
    REQUIRE_FALSE( spell_audit_m27a_dropped_spell.is_valid() );
    known_magic magic;
    magic.deserialize( json_loader::from_string( R"({
        "mana": 100,
        "spellbook": [
            { "id": "audit_m27a_removed_spell", "xp": 1234 },
            { "id": "audit_m27a_dropped_spell", "xp": 55 },
            { "id": "audit_m27a_capped_spell", "xp": 7 }
        ],
        "invlets": { "audit_m27a_removed_spell": 113 },
        "favorites": [ "audit_m27a_removed_spell", "audit_m27a_dropped_spell" ]
    })" ).get_object() );

    // the removed spell became its replacement, with its experience
    REQUIRE( magic.knows_spell( spell_audit_m27a_menu_spell ) );
    CHECK( magic.get_spell( spell_audit_m27a_menu_spell ).xp() == 1234 );
    CHECK_FALSE( magic.knows_spell( spell_audit_m27a_removed_spell ) );
    // a migration without "to" drops the spell
    CHECK_FALSE( magic.knows_spell( spell_audit_m27a_dropped_spell ) );
    // the others load as before
    REQUIRE( magic.knows_spell( spell_audit_m27a_capped_spell ) );
    CHECK( magic.get_spell( spell_audit_m27a_capped_spell ).xp() == 7 );
    CHECK( magic.spells().size() == 2 );
    // hotkey 'q' and the favorite follow the migrated spell
    CHECK( magic.get_invlet( spell_audit_m27a_menu_spell ) == 'q' );
    CHECK( magic.is_favorite( spell_audit_m27a_menu_spell ) );
    CHECK_FALSE( magic.is_favorite( spell_audit_m27a_dropped_spell ) );

    SECTION( "a spell already known under the new id isn't modified" ) {
        known_magic both;
        both.deserialize( json_loader::from_string( R"({
            "mana": 100,
            "spellbook": [
                { "id": "audit_m27a_removed_spell", "xp": 1234 },
                { "id": "audit_m27a_menu_spell", "xp": 9 }
            ]
        })" ).get_object() );
        REQUIRE( both.knows_spell( spell_audit_m27a_menu_spell ) );
        CHECK( both.get_spell( spell_audit_m27a_menu_spell ).xp() == 9 );
        CHECK( both.spells().size() == 1 );
    }
}

// MAGIC-B08: pain_split averaged over every body part, minor ones included, then set them
// all, so badly hurt limbs were healed far past what the spell moves around.
TEST_CASE( "magic-b08_pain_split_moves_damage_between_main_limbs", "[audit][magic]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    const spell split( spell_audit_m27a_pain_split );
    const std::vector<bodypart_str_id> limbs = { body_part_head, body_part_torso, body_part_arm_l,
                                                 body_part_arm_r, body_part_leg_l, body_part_leg_r
                                               };
    for( const bodypart_str_id &bp : limbs ) {
        REQUIRE( u.get_part_hp_max( bp ) >= 16 );
    }
    REQUIRE( u.get_part_hp_max( body_part_torso ) >= 40 );

    SECTION( "limbs that are hurt the same stay as they are" ) {
        for( const bodypart_str_id &bp : limbs ) {
            u.set_part_hp_cur( bp, 10 );
        }
        split.cast_all_effects( u, u.pos_bub() );
        for( const bodypart_str_id &bp : limbs ) {
            CAPTURE( bp.str() );
            CHECK( u.get_part_hp_cur( bp ) == 10 );
        }
    }
    SECTION( "the damage is shared out, the total is kept" ) {
        for( const bodypart_str_id &bp : limbs ) {
            u.set_part_hp_cur( bp, 10 );
        }
        u.set_part_hp_cur( body_part_torso, 40 );
        // ( 40 + 5 * 10 ) / 6
        split.cast_all_effects( u, u.pos_bub() );
        for( const bodypart_str_id &bp : limbs ) {
            CAPTURE( bp.str() );
            CHECK( u.get_part_hp_cur( bp ) == 15 );
        }
    }
    SECTION( "a broken limb stays broken and doesn't count" ) {
        for( const bodypart_str_id &bp : limbs ) {
            u.set_part_hp_cur( bp, 10 );
        }
        u.set_part_hp_cur( body_part_torso, 40 );
        u.set_part_hp_cur( body_part_arm_l, 0 );
        // ( 40 + 4 * 10 ) / 5
        split.cast_all_effects( u, u.pos_bub() );
        CHECK( u.get_part_hp_cur( body_part_arm_l ) == 0 );
        for( const bodypart_str_id &bp : limbs ) {
            if( bp != body_part_arm_l ) {
                CAPTURE( bp.str() );
                CHECK( u.get_part_hp_cur( bp ) == 16 );
            }
        }
    }
    clear_avatar();
}

// MAGIC-B09: a summon spell that may target "self" picked the caster's tile, and every
// tile it couldn't place a monster on raised the "failed to place monster" debugmsg.
TEST_CASE( "magic-b09_summons_skip_occupied_tiles_quietly", "[audit][magic]" )
{
    const tripoint_bub_ms center( 60, 60, 0 );
    m27a_clear_world( center );
    avatar &u = get_avatar();
    // nine monsters for a 3x3 area the caster stands in
    const spell summon( spell_audit_m27a_summon );
    REQUIRE( summon.damage( u ) == 9 );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        summon.cast_all_effects( u, u.pos_bub() );
    } );
    CHECK( dmsg.empty() );
    int placed = 0;
    for( const tripoint_bub_ms &p : get_map().points_in_radius( center, 1 ) ) {
        if( p != center ) {
            CAPTURE( p );
            CHECK( get_creature_tracker().creature_at<monster>( p ) != nullptr );
            placed++;
        }
    }
    CHECK( placed == 8 );
    CHECK( get_creature_tracker().creature_at<monster>( center ) == nullptr );
    CHECK( u.pos_bub() == center );
    clear_creatures();
}

// MAGIC-B10: each 3-minute tick regained floor( max mana / 160 ), so a pool under 160 never
// regenerated and one under 320 lost up to half its regeneration.
TEST_CASE( "magic-b10_low_mana_pools_regenerate_fully_in_eight_hours", "[audit][magic]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope restore_int( [&u]() {
        u.set_int_base( 8 );
    } );
    u.set_int_base( 1 );
    const int pool = u.magic->max_mana( u );
    CAPTURE( pool );
    REQUIRE( pool > 0 );
    REQUIRE( pool < 320 );
    u.magic->set_mana( 0 );
    // 8 hours of 3-minute ticks
    for( int tick = 0; tick < 160; ++tick ) {
        u.magic->update_mana( u, to_turns<float>( 3_minutes ) );
    }
    // flooring each tick regained 160 * floor( pool / 160 ), 160 for a pool of 300
    CHECK( u.magic->available_mana() >= pool - 1 );
    CHECK( u.magic->available_mana() <= pool );
    clear_avatar();
}

// MAGIC-B11: channel_magic() and break_channeling() followed magic->last_spell, which only
// the player's cast menu set and which wasn't saved: a channeling spell cast any other way
// channeled the last menu spell, or nothing, and a save stopped it.
TEST_CASE( "magic-b11_channeling_follows_the_spell_that_started_it", "[audit][magic]" )
{
    const tripoint_bub_ms center( 60, 60, 0 );
    m27a_clear_world( center );
    global_variables &globals = get_globals();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( [&u]() {
        u.remove_effect( effect_magic_channeling );
        get_globals().clear_global_values();
    } );
    // the spell menu last cast something else
    u.magic->learn_spell( spell_audit_m27a_menu_spell, u, true );
    u.magic->last_spell = spell_audit_m27a_menu_spell;

    // a channeling spell the avatar doesn't know (an item's, an EOC's), cast at level 4
    const spell channeled( spell_audit_m27a_channel, 0, 4 );
    REQUIRE( channeled.is_channeling_spell() );
    REQUIRE_FALSE( u.magic->knows_spell( spell_audit_m27a_channel ) );
    u.magic->start_channeling( channeled );
    u.add_effect( effect_magic_channeling, calendar::INDEFINITELY_LONG_DURATION );
    u.last_magic_target_pos = get_map().get_abs( u.pos_bub() );

    u.magic->channel_magic( u );
    CHECK( globals.get_global_value( "audit_m27a_channel_ticks" ) == 1 );
    CHECK( globals.get_global_value( "audit_m27a_channel_level" ) == 4 );

    // the channeling survives a save
    std::ostringstream saved;
    JsonOut jsout( saved );
    u.magic->serialize( jsout );
    u.magic = pimpl<known_magic>();
    u.magic->deserialize( json_loader::from_string( saved.str() ).get_object() );

    u.magic->channel_magic( u );
    CHECK( globals.get_global_value( "audit_m27a_channel_ticks" ) == 2 );
    CHECK( globals.get_global_value( "audit_m27a_channel_level" ) == 4 );

    // and its own interrupt spell goes off when it breaks
    REQUIRE( u.has_effect( effect_magic_channeling ) );
    u.magic->break_channeling( u );
    CHECK( globals.get_global_value( "audit_m27a_channel_interrupts" ) == 1 );
    CHECK_FALSE( u.has_effect( effect_magic_channeling ) );
    u.magic->channel_magic( u );
    CHECK( globals.get_global_value( "audit_m27a_channel_ticks" ) == 2 );
}

// MAGIC-B12: forget_spell() always told the player and sent the avatar's id,
// on_mutation_loss() didn't check the spell was still known, and on_mutation_gain() raised
// the spell past its max level.
TEST_CASE( "magic-b12_trait_spell_grants_belong_to_their_owner", "[audit][magic]" )
{
    const tripoint_bub_ms center( 60, 60, 0 );
    m27a_clear_world( center );
    npc &guy = m27a_spawn_npc( center + point( 2, 0 ) );

    // the trait grants five levels of a spell that has three
    guy.magic->on_mutation_gain( trait_TEST_AUDIT_M27A_SPELL_TRAIT, guy );
    REQUIRE( guy.magic->knows_spell( spell_audit_m27a_capped_spell ) );
    CHECK( guy.magic->get_spell( spell_audit_m27a_capped_spell ).get_level() == 3 );

    Messages::clear_messages();
    forget_recorder recorder;
    guy.magic->on_mutation_loss( trait_TEST_AUDIT_M27A_SPELL_TRAIT, guy );
    CHECK_FALSE( guy.magic->knows_spell( spell_audit_m27a_capped_spell ) );
    REQUIRE( recorder.forgetters.size() == 1 );
    CHECK( recorder.forgetters.front() == guy.getID() );
    CHECK_FALSE( avatar_was_told( "All knowledge of" ) );

    // losing it again, with the spell already gone, is quiet
    const std::string dmsg = capture_debugmsg_during( [&]() {
        guy.magic->on_mutation_loss( trait_TEST_AUDIT_M27A_SPELL_TRAIT, guy );
    } );
    CHECK( dmsg.empty() );
    CHECK( recorder.forgetters.size() == 1 );
    Messages::clear_messages();
    clear_npcs();
}

// MAGIC-B13: the NPC branch of select_target() rated the spell it looked up in the NPC's
// spellbook, so an NPC casting a spell it doesn't know (an EOC's targeted cast) got "Tried to
// get unknown spell" and cancelled; a monster always aimed at its own tile.
TEST_CASE( "magic-b13_targeted_casts_of_unknown_spells_aim_at_the_foe", "[audit][magic]" )
{
    scoped_weather_override weather( weather_clear );
    restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    const tripoint_bub_ms center( 60, 60, 0 );

    SECTION( "an npc aims a spell it doesn't know" ) {
        m27a_clear_world( center + point( -20, 0 ) );
        npc &guy = m27a_spawn_npc( center );
        monster &zombie = m27a_spawn_monster( mon_zombie, center + point( 3, 0 ) );
        set_time_to_day();
        REQUIRE( guy.sees( get_map(), zombie ) );
        REQUIRE( guy.attitude_to( zombie ) == Creature::Attitude::HOSTILE );
        spell bolt( spell_audit_m27a_npc_bolt );
        REQUIRE_FALSE( guy.magic->knows_spell( spell_audit_m27a_npc_bolt ) );
        std::optional<tripoint_bub_ms> target;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            target = bolt.select_target( &guy );
        } );
        CHECK( dmsg.empty() );
        REQUIRE( target.has_value() );
        CHECK( *target == zombie.pos_bub() );
    }
    SECTION( "a monster aims at what it is fighting" ) {
        m27a_clear_world( center + point( 3, 0 ) );
        avatar &u = get_avatar();
        monster &zombie = m27a_spawn_monster( mon_zombie, center );
        set_time_to_day();
        zombie.set_dest( u.pos_abs() );
        REQUIRE( zombie.attack_target() == &u );
        spell bolt( spell_audit_m27a_npc_bolt );
        const std::optional<tripoint_bub_ms> target = bolt.select_target( &zombie );
        REQUIRE( target.has_value() );
        CHECK( *target == u.pos_bub() );
    }
    clear_creatures();
    clear_npcs();
}

// MAGIC-B14: swap_pos() moved whatever creature_at() found at the target without checking
// there was one (a crash for an empty tile or a killed target), and two monsters were moved
// one by one onto each other's tile in the creature tracker.
TEST_CASE( "magic-b14_swap_pos_handles_empty_tiles_and_monster_casters", "[audit][magic]" )
{
    const tripoint_bub_ms center( 60, 60, 0 );
    m27a_clear_world( center );
    avatar &u = get_avatar();
    creature_tracker &creatures = get_creature_tracker();
    const spell swap( spell_audit_m27a_swap );

    SECTION( "an empty tile swaps nothing" ) {
        swap.cast_all_effects( u, center + point( 4, 0 ) );
        CHECK( u.pos_bub() == center );
    }
    SECTION( "two monsters change places" ) {
        const tripoint_bub_ms first_pos = center + point( -10, 5 );
        const tripoint_bub_ms second_pos = first_pos + point( 3, 0 );
        monster &first = m27a_spawn_monster( mon_test_non_shearable, first_pos );
        monster &second = m27a_spawn_monster( mon_test_non_shearable, second_pos );
        const std::string dmsg = capture_debugmsg_during( [&]() {
            swap.cast_all_effects( first, second_pos );
        } );
        CHECK( dmsg.empty() );
        CHECK( first.pos_bub() == second_pos );
        CHECK( second.pos_bub() == first_pos );
        CHECK( creatures.creature_at<monster>( second_pos ) == &first );
        CHECK( creatures.creature_at<monster>( first_pos ) == &second );
    }
    SECTION( "the avatar and a monster change places" ) {
        const tripoint_bub_ms mon_pos = center + point( 3, 0 );
        monster &mon = m27a_spawn_monster( mon_test_non_shearable, mon_pos );
        // absolute: the map may shift with the avatar
        const tripoint_abs_ms mon_abs = get_map().get_abs( mon_pos );
        const tripoint_abs_ms avatar_abs = u.pos_abs();
        swap.cast_all_effects( u, mon_pos );
        CHECK( u.pos_abs() == mon_abs );
        CHECK( mon.pos_abs() == avatar_abs );
        CHECK( creatures.creature_at<monster>( u.pos_bub() ) == nullptr );
        CHECK( creatures.creature_at<monster>( mon.pos_bub() ) == &mon );
    }
    clear_creatures();
}

// MAGIC-B15: only select_target() and sub-spells picked a target for RANDOM_TARGET spells:
// a cast at the caster's own tile (enchantments, relics, monsters' self casts, EOCs without
// a target) went off there. RANDOM_CRITTER alone did nothing.
TEST_CASE( "magic-b15_random_target_spells_pick_a_target_on_every_path", "[audit][magic]" )
{
    const tripoint_bub_ms center( 60, 60, 0 );
    m27a_clear_world( center );
    avatar &u = get_avatar();
    global_variables &globals = get_globals();

    SECTION( "a cast at the caster's tile zaps a creature in range" ) {
        monster &mon = m27a_spawn_monster( mon_test_non_shearable, center + point( 2, 0 ) );
        spell( spell_audit_m27a_random_zap ).cast_all_effects( u, u.pos_bub() );
        CHECK( zaps_of( mon ) == 1 );
        CHECK( zaps_of( u ) == 0 );
        CHECK( globals.get_global_value( "audit_m27a_zaps" ) == 1 );
    }
    SECTION( "RANDOM_CRITTER alone picks a creature, not the ground" ) {
        monster &mon = m27a_spawn_monster( mon_test_non_shearable, center + point( 0, 2 ) );
        spell( spell_audit_m27a_random_critter ).cast_all_effects( u, u.pos_bub() );
        CHECK( zaps_of( mon ) == 1 );
        CHECK( globals.get_global_value( "audit_m27a_zaps" ) == 1 );
    }
    SECTION( "with nothing else to hit, a spell that can only target its caster still does" ) {
        spell( spell_audit_m27a_random_self ).cast_all_effects( u, u.pos_bub() );
        CHECK( zaps_of( u ) == 1 );
    }
    SECTION( "with no valid target in range, nothing is hit" ) {
        spell( spell_audit_m27a_random_zap ).cast_all_effects( u, u.pos_bub() );
        CHECK( globals.maybe_get_global_value( "audit_m27a_zaps" ) == nullptr );
        CHECK( zaps_of( u ) == 0 );
    }
    SECTION( "a cast at a chosen target keeps it" ) {
        monster &chosen = m27a_spawn_monster( mon_test_non_shearable, center + point( 2, 0 ) );
        monster &other = m27a_spawn_monster( mon_test_non_shearable, center + point( -2, 0 ) );
        spell( spell_audit_m27a_random_zap ).cast_all_effects( u, chosen.pos_bub() );
        CHECK( zaps_of( chosen ) == 1 );
        CHECK( zaps_of( other ) == 0 );
    }
    clear_creatures();
    u.remove_value( "audit_m27a_zapped" );
    get_globals().clear_global_values();
}

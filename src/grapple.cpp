#include "grapple.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "effect_source.h"
#include "enums.h"
#include "game.h"
#include "game_constants.h"
#include "item_location.h"
#include "map.h"
#include "mapdata.h"
#include "math_parser_jmath.h"
#include "mattack_actors.h"
#include "mattack_common.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "point.h"
#include "rng.h"
#include "string_formatter.h"
#include "talker.h"
#include "translations.h"
#include "weighted_list.h"

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_incorporeal( "incorporeal" );
static const efftype_id effect_winded( "winded" );

static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static const jmath_func_id jmath_func_grapple_grab_strength( "grapple_grab_strength" );

// Finding a monster grabber, and how strong a character grabs

namespace
{

// Evaluate a grappling formula with u = grabber and n = target.
double eval_formula( const jmath_func_id &func, const Creature &grabber, const Creature &target,
                     const std::vector<double> &params = {} )
{
    const_dialogue d( get_const_talker_for( grabber ), get_const_talker_for( target ) );
    return params.empty() ? func->eval( d ) : func->eval( d, params );
}

} // namespace

int grapple::monster_grab_reach( const monster &mon )
{
    int reach = 0;
    for( const auto &special : mon.type->special_attacks ) {
        const melee_actor *const actor = dynamic_cast<const melee_actor *>( special.second.get() );
        if( actor != nullptr && actor->is_grab ) {
            reach = std::max( reach, actor->range );
        }
    }
    return reach;
}

monster *grapple::monster_grabber( const Creature &victim, const bodypart_id &bp )
{
    // A character victim's grabs are per limb; a monster victim has one grab on the null part.
    const bool limb_grab = !victim.is_monster();
    const tripoint_abs_ms victim_pos = victim.pos_abs();
    monster *best = nullptr;
    int best_dist = INT_MAX;
    bool best_targets_victim = false;
    for( monster &mon : g->all_monsters() ) {
        if( &mon == &victim || mon.is_dead() ||
            !mon.has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
            continue;
        }
        if( limb_grab && !mon.is_grabbing( bp.id() ) ) {
            continue;
        }
        const tripoint_abs_ms mon_pos = mon.pos_abs();
        if( std::abs( mon_pos.z() - victim_pos.z() ) > 1 ) {
            continue;
        }
        // Grab attacks reach past the adjacent tiles, and pulls and drags can end a z-level
        // away (CHAR-B09).
        const int dist = rl_dist( mon_pos.xy(), victim_pos.xy() );
        if( dist < 1 || dist > std::max( 1, monster_grab_reach( mon ) ) ) {
            continue;
        }
        const bool targets_victim = mon.attack_target() == &victim;
        if( best == nullptr || ( targets_victim && !best_targets_victim ) ||
            ( targets_victim == best_targets_victim && dist < best_dist ) ) {
            best = &mon;
            best_dist = dist;
            best_targets_victim = targets_victim;
        }
    }
    return best;
}

int grapple::grab_strength( const Character &grabber, const Creature &target )
{
    const double strength = eval_formula( jmath_func_grapple_grab_strength, grabber, target );
    return std::clamp( static_cast<int>( std::round( strength ) ), 1, 100 );
}

// Holding a creature

namespace
{

bool sourced_from( const effect &eff, const Character &grabber )
{
    const std::optional<character_id> source = eff.get_source().get_character_id();
    return source && source->is_valid() && *source == grabber.getID();
}

} // namespace

bool Character::can_grapple_while_wielding() const
{
    // TODO: when the martial-arts track lands, also allow a weapon that the active style lists in
    // its grapple_weapons.
    return !is_armed();
}

Creature *Character::grabbed_creature() const
{
    if( !has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
        return nullptr;
    }
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &p : here.points_in_radius( pos_bub( here ), 1, 1 ) ) {
        Creature *const victim = creatures.creature_at( p );
        if( victim != nullptr && victim != this && !victim->is_dead_state() &&
            grapple::in_reach( *this, *victim ) &&
            grapple::hold_effect( *this, *victim ) != nullptr ) {
            return victim;
        }
    }
    return nullptr;
}

bool Character::is_grabbing_creature() const
{
    return grabbed_creature() != nullptr;
}

void Character::release_grapple()
{
    Creature *const victim = grabbed_creature();
    if( victim != nullptr ) {
        add_msg_player_or_npc( _( "You release %s." ), _( "<npcname> releases %s." ),
                               victim->disp_name() );
    }
    grapple::end_hold( *this, victim );
}

bool grapple::in_reach( const Creature &grabber, const Creature &victim )
{
    const tripoint_abs_ms a = grabber.pos_abs();
    const tripoint_abs_ms b = victim.pos_abs();
    return square_dist( a.xy(), b.xy() ) == 1 && std::abs( a.z() - b.z() ) <= 1;
}

const effect *grapple::hold_effect( const Character &grabber, const Creature &victim )
{
    if( !grabber.getID().is_valid() ) {
        return nullptr;
    }
    for( const effect &eff : victim.get_effects_with_flag( json_flag_GRAB ) ) {
        if( sourced_from( eff, grabber ) ) {
            return &eff;
        }
    }
    return nullptr;
}

Character *grapple::character_grabber( const Creature &victim, const effect &eff )
{
    const std::optional<character_id> source = eff.get_source().get_character_id();
    if( !source || !source->is_valid() ) {
        return nullptr;
    }
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &p : here.points_in_radius( victim.pos_bub( here ), 1, 1 ) ) {
        Character *const guy = creatures.creature_at<Character>( p );
        if( guy == nullptr || guy == &victim || guy->getID() != *source ) {
            continue;
        }
        if( guy->is_dead_state() || !guy->has_effect_with_flag( json_flag_GRAB_FILTER ) ||
            !in_reach( *guy, victim ) ) {
            return nullptr;
        }
        return guy;
    }
    return nullptr;
}

bool grapple::start_hold( Character &grabber, Creature &victim, int strength,
                          const bodypart_id &bp )
{
    // add_effect would merge into an existing grab and keep its source, so refuse instead.
    if( victim.has_effect( effect_grabbed, bp ) ) {
        return false;
    }
    victim.add_effect( effect_source( &grabber ), effect_grabbed, 1_days, bp, true,
                       std::clamp( strength, 1, 100 ) );
    if( hold_effect( grabber, victim ) == nullptr ) {
        return false;
    }
    grabber.add_effect( effect_grabbing, 1_days, true, 1 );
    return true;
}

void grapple::end_hold( Character &grabber, Creature *victim )
{
    if( victim != nullptr ) {
        std::vector<std::pair<efftype_id, bodypart_id>> held;
        for( const effect &eff : victim->get_effects_with_flag( json_flag_GRAB ) ) {
            if( sourced_from( eff, grabber ) ) {
                held.emplace_back( eff.get_id(), eff.get_bp() );
            }
        }
        for( const std::pair<efftype_id, bodypart_id> &grab : held ) {
            victim->remove_effect( grab.first, grab.second );
        }
    }
    std::vector<efftype_id> filters;
    for( const effect &eff : grabber.get_effects_with_flag( json_flag_GRAB_FILTER ) ) {
        filters.push_back( eff.get_id() );
    }
    for( const efftype_id &id : filters ) {
        grabber.remove_effect( id );
    }
}

void grapple::release_holds_on( Creature &victim )
{
    std::vector<Character *> grabbers;
    for( const effect &eff : victim.get_effects_with_flag( json_flag_GRAB ) ) {
        if( Character *const guy = character_grabber( victim, eff ) ) {
            grabbers.push_back( guy );
        }
    }
    for( Character *guy : grabbers ) {
        end_hold( *guy, &victim );
    }
}

void grapple::release_all( Creature &c )
{
    if( Character *const guy = c.as_character() ) {
        if( guy->has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
            end_hold( *guy, guy->grabbed_creature() );
        }
    }
    release_holds_on( c );
}

int grapple::stamina_floor()
{
    return get_option<int>( "GRAPPLE_STAMINA_FLOOR" );
}

void grapple::process_turn( Character &grabber )
{
    if( !grabber.has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
        return;
    }
    Creature *const victim = grabber.grabbed_creature();
    if( victim == nullptr ) {
        // The victim moved away, was removed from the map, or something else took it from us:
        // drop our side.
        add_msg_debug( debugmode::DF_CHARACTER, "Orphan grabbing effect removed from %s.",
                       grabber.disp_name() );
        grabber.add_msg_if_player( m_info, _( "You lose your hold." ) );
        end_hold( grabber, nullptr );
        return;
    }
    if( grabber.has_effect( effect_incorporeal ) || victim->has_effect( effect_incorporeal ) ||
        grabber.has_effect( effect_winded ) || grabber.in_sleep_state() ) {
        grabber.release_grapple();
        return;
    }
    if( grabber.get_stamina() < stamina_floor() ) {
        grabber.add_msg_if_player( m_bad, _( "You're too exhausted to maintain your hold." ) );
        end_hold( grabber, victim );
    }
}

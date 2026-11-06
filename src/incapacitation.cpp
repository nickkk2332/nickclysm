#include "incapacitation.h"

#include <algorithm>
#include <vector>

#include "activity_type.h"
#include "character.h"
#include "creature.h"
#include "effect.h"
#include "effect_source.h"
#include "monster.h"
#include "player_activity.h"
#include "type_id.h"

namespace
{
const efftype_id effect_knocked_out( "knocked_out" );
const flag_id flag_knockout_immune( "KNOCKOUT_IMMUNE" );
const flag_id flag_knockout_vulnerable( "KNOCKOUT_VULNERABLE" );

bool flagged( const Creature &who, const flag_id &flag )
{
    return who.has_flag( flag ) || who.has_effect_with_flag( flag );
}
} // namespace

bool incapacitation::unconscious( const Creature &who )
{
    return who.has_effect( effect_knocked_out ) &&
           who.get_effect( effect_knocked_out ).get_duration() > 0_turns;
}

bool incapacitation::supported_physiology( const Creature &who )
{
    if( who.as_character() ) {
        return true;
    }
    const monster *mon = who.as_monster();
    if( !mon ) {
        return false;
    }
    if( flagged( who, flag_knockout_vulnerable ) ) {
        return true;
    }
    return mon->has_flag( mon_flag_str_id( "ANIMAL" ) ) &&
           mon->made_of( material_id( "flesh" ) ) &&
           !mon->has_flag( mon_flag_str_id( "ELECTRONIC" ) ) &&
           !mon->has_flag( mon_flag_str_id( "NOHEAD" ) ) &&
           !mon->in_species( species_id( "ZOMBIE" ) ) &&
           !mon->in_species( species_id( "NETHER" ) );
}

bool incapacitation::immune( const Creature &who )
{
    return flagged( who, flag_knockout_immune ) ||
           who.has_flag( flag_id( "CANNOT_GAIN_EFFECTS" ) );
}

void incapacitation::suppress_moves( Creature &who )
{
    who.set_moves( std::min( 0, who.get_moves() ) );
}

std::string incapacitation::eligibility( const Creature &target )
{
    if( target.is_dead_state() || target.is_hallucination() || target.is_fake() ) {
        return "unavailable";
    }
    if( immune( target ) ) {
        return "immune";
    }
    if( !supported_physiology( target ) ) {
        return "unsupported";
    }
    if( unconscious( target ) || target.has_effect( efftype_id( "sleep" ) ) ||
        target.has_effect( efftype_id( "narcosis" ) ) ) {
        return "already_incapacitated";
    }
    const Character *person = target.as_character();
    if( target.has_effect( efftype_id( "ridden" ) ) || target.has_effect( efftype_id( "riding" ) ) ||
        target.has_effect( efftype_id( "grabbing" ) ) ||
        ( person && ( person->in_vehicle || person->is_mounted() || person->grabbed_creature() ||
                      person->activity.id() == activity_id( "ACT_OPERATION" ) ) ) ) {
        return "unsupported";
    }
    if( target.is_immune_effect( effect_knocked_out ) ) {
        return "immune";
    }
    for( const effect &e : target.get_effects() ) {
        const std::vector<efftype_id> blocked = e.get_blocks_effects();
        if( std::find( blocked.begin(), blocked.end(), effect_knocked_out ) != blocked.end() ) {
            return "immune";
        }
    }
    return "eligible";
}

std::string incapacitation::apply( Creature &target, const Creature *source,
                                   time_duration duration, bool wake_on_damage )
{
    const std::string admission = eligibility( target );
    if( admission != "eligible" ) {
        return admission;
    }
    if( duration <= 0_turns || duration > 1_hours ) {
        return "invalid_duration";
    }
    target.add_effect( effect_source( source ), effect_knocked_out, duration, false,
                       wake_on_damage ? 1 : 2 );
    if( !unconscious( target ) ) {
        return target.is_dead_state() ? "unavailable" : "interrupted";
    }
    if( Character *person = target.as_character() ) {
        // The execution owns its own activity and releases it when the scene settles.
        if( !person->activity.is_null() && person->activity.id() != activity_id( "ACT_EXECUTION" ) ) {
            person->cancel_activity();
        }
        person->abort_automove();
        person->recalc_sight_limits();
        person->set_dodges_left( 0 );
        person->set_free_dodges_left( 0 );
    }
    suppress_moves( target );
    return unconscious( target ) && !target.is_dead_state() ? "applied" : "unavailable";
}

void incapacitation::wake_on_damage( Creature &target, int hp_lost )
{
    if( hp_lost <= 0 || !unconscious( target ) ||
        target.get_effect( effect_knocked_out ).get_intensity() != 1 ) {
        return;
    }
    target.get_effect( effect_knocked_out ).set_duration( 0_turns );
    suppress_moves( target );
    if( Character *person = target.as_character() ) {
        person->recalc_sight_limits();
    }
}

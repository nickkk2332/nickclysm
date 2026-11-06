#include "incapacitation.h"

#include <algorithm>
#include <functional>
#include <vector>

#include "calendar.h"
#include "character.h"
#include "creature.h"
#include "effect.h"
#include "effect_source.h"
#include "monster.h"
#include "player_activity.h"
#include "type_id.h"

static const activity_id ACT_EXECUTION( "ACT_EXECUTION" );
static const activity_id ACT_OPERATION( "ACT_OPERATION" );

static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_knocked_out( "knocked_out" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_riding( "riding" );
static const efftype_id effect_sleep( "sleep" );

static const flag_id json_flag_CANNOT_GAIN_EFFECTS( "CANNOT_GAIN_EFFECTS" );
static const flag_id json_flag_KNOCKOUT_IMMUNE( "KNOCKOUT_IMMUNE" );
static const flag_id json_flag_KNOCKOUT_VULNERABLE( "KNOCKOUT_VULNERABLE" );

static const material_id material_flesh( "flesh" );

static const mon_flag_str_id mon_flag_ANIMAL( "ANIMAL" );
static const mon_flag_str_id mon_flag_ELECTRONIC( "ELECTRONIC" );
static const mon_flag_str_id mon_flag_NOHEAD( "NOHEAD" );

static const species_id species_NETHER( "NETHER" );
static const species_id species_ZOMBIE( "ZOMBIE" );

namespace
{

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
    if( flagged( who, json_flag_KNOCKOUT_VULNERABLE ) ) {
        return true;
    }
    return mon->has_flag( mon_flag_ANIMAL ) &&
           mon->made_of( material_flesh ) &&
           !mon->has_flag( mon_flag_ELECTRONIC ) &&
           !mon->has_flag( mon_flag_NOHEAD ) &&
           !mon->in_species( species_ZOMBIE ) &&
           !mon->in_species( species_NETHER );
}

bool incapacitation::immune( const Creature &who )
{
    return flagged( who, json_flag_KNOCKOUT_IMMUNE ) ||
           who.has_flag( json_flag_CANNOT_GAIN_EFFECTS );
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
    if( unconscious( target ) || target.has_effect( effect_sleep ) ||
        target.has_effect( effect_narcosis ) ) {
        return "already_incapacitated";
    }
    const Character *person = target.as_character();
    if( target.has_effect( effect_ridden ) || target.has_effect( effect_riding ) ||
        target.has_effect( effect_grabbing ) ||
        ( person && ( person->in_vehicle || person->is_mounted() || person->grabbed_creature() ||
                      person->activity.id() == ACT_OPERATION ) ) ) {
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
        if( !person->activity.is_null() && person->activity.id() != ACT_EXECUTION ) {
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

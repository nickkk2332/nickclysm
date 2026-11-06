#include "power_motion.h"

#include <functional>
#include <memory>

#include "ability_runtime.h"
#include "cata_scope_helpers.h"
#include "creature.h"
#include "game.h"
#include "incapacitation.h"
#include "line.h"
#include "map.h"
#include "power_actor.h"
#include "type_id.h"

static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );

namespace
{
thread_local power_motion::flight_scope *current = nullptr;
thread_local int request_depth = 0;
} // namespace

power_motion::flight_scope::flight_scope( Creature &who, power_motion_result *receipt ) :
    previous( current ), target( who ), result( receipt )
{
    current = this;
}
power_motion::flight_scope::~flight_scope()
{
    current = previous;
}
bool power_motion::active( const Creature &target )
{
    return current && current->result && &current->target == &target;
}
Creature *power_motion::instigator( const Creature *target )
{
    return current && current->result && &current->target == target ?
           current->result->instigator.resolve() : nullptr;
}

power_motion_result power_motion::fling( const creature_ref &source_ref,
        const creature_ref &target_ref, int velocity, bool controlled, bool hit_characters,
        const std::string &cause )
{
    power_motion_result out;
    out.instigator = source_ref;
    out.target = target_ref;
    out.cause = cause;
    out.velocity = velocity;
    if( request_depth >= 8 ) {
        out.reason = "ancestry_budget";
        get_power_actors().record_motion( out );
        return out;
    }
    ++request_depth;
    const on_out_of_scope reset( []() {
        --request_depth;
    } );
    Creature *source = source_ref.resolve();
    Creature *target = target_ref.resolve();
    const auto source_hold = source ? g->shared_from( *source ) : nullptr;
    const auto target_hold = target ? g->shared_from( *target ) : nullptr;
    if( !source_hold || !target_hold || source == target || source->is_fake() || target->is_fake() ||
        source->is_hallucination() || target->is_hallucination() ) {
        out.reason = "participant_unavailable";
    } else if( g->abilities().restoring() || g->abilities().stopping() ) {
        out.reason = "world_transition";
    } else if( velocity < 10 || velocity > 300 ) {
        out.reason = "invalid_velocity";
    } else if( incapacitation::unconscious( *source ) || source->has_effect( effect_sleep ) ||
               source->has_effect( effect_narcosis ) || source->has_effect( effect_stunned ) ) {
        out.reason = "source_incapacitated";
    } else if( source->pos_abs().z() != target->pos_abs().z() ||
               !get_map().inbounds( source->pos_bub() ) || !get_map().inbounds( target->pos_bub() ) ) {
        out.reason = "unsupported_geometry";
    } else {
        fling_options options;
        options.hit_characters = hit_characters;
        options.motion = &out;
        try {
            out.accepted = g->fling_creature( target, coord_to_angle( source->pos_bub(), target->pos_bub() ),
                                              velocity, controlled, false, options );
        } catch( ... ) {
            out.reason = "callback_exception";
            get_power_actors().record_motion( out );
            throw;
        }
        if( out.reason.empty() ) {
            out.reason = out.accepted ? ( out.origin != out.destination ? "moved" : "blocked" ) : "resisted";
        }
    }
    get_power_actors().record_motion( out );
    return out;
}

bool power_motion::processing()
{
    // Legacy native flights are also unsafe places to save or remove actors.
    return current != nullptr;
}

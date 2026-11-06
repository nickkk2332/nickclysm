#include "avatar.h"

#include "cata_scope_helpers.h"
#include "creature_ref.h"
#include "effect_on_condition.h"
#include "event.h"
#include "event_bus.h"
#include "execution_fatal.h"
#include "execution_runtime.h"
#include "game.h"
#include "map.h"
#include "player_activity.h"

void avatar::reset_death_state()
{
    clear_killer();
    death_committed_ = false;
    death_finalized_ = false;
    death_presentation_ = death_presentation::pending;
    death_finalizing_ = false;
}

void avatar::die( map *here, Creature *source )
{
    if( death_finalizing_ || death_finalized_ ||
        execution_fatal::before_death( *this, here, source ) || death_committed_ ) {
        return;
    }
    death_committed_ = true;
    Character::die( here, source );
}

void avatar::prevent_death()
{
    if( death_finalized_ ) {
        return;
    }
    Character::prevent_death();
    death_committed_ = false;
    clear_killer();
}

bool avatar::finalize_death( Creature *source )
{
    if( this != &get_avatar() ) {
        return false;
    }
    if( death_finalized_ || death_finalizing_ ) {
        return death_finalized_;
    }
    if( execution_fatal::pending( *this ) || g->executions().restoring() ) {
        return false;
    }
    if( !is_dead_state() ) {
        reset_death_state();
        return false;
    }
    // Validate the old pointer before dereferencing it, and retain the original body through EOCs.
    Creature *loaded_source = source ? g->get_creature_if( [source]( const Creature & who ) {
        return &who == source;
    } ) : nullptr;
    const auto retained_source = loaded_source ? g->shared_from( *loaded_source ) : nullptr;
    const creature_ref source_identity = loaded_source ? creature_ref::bind( *loaded_source ) :
                                         creature_ref();
    const auto validate_source = [&]() {
        clear_killer();
        if( retained_source && source_identity.resolve_loaded() == retained_source.get() &&
            ( !retained_source->is_dead_state() || retained_source.get() == this ) ) {
            set_killer( retained_source.get() );
        }
    };
    death_finalizing_ = true;
    const on_out_of_scope leave( [&]() {
        death_finalizing_ = false;
    } );
    if( activity.id() == activity_id( "ACT_EXECUTION" ) ) {
        activity.set_to_null();
    }
    validate_source();
    effect_on_conditions::prevent_death();
    if( is_dead_state() ) {
        get_event_bus().send<event_type::character_dies>( getID() );
    }
    if( is_dead_state() ) {
        validate_source();
        effect_on_conditions::avatar_death();
    }
    if( !is_dead_state() ) {
        death_committed_ = false;
        clear_killer();
        recalc_sight_limits();
        return false;
    }
    validate_source();
    if( !death_committed_ ) {
        death_committed_ = true;
        Character::die( &get_map(), get_killer() );
    }
    if( !is_dead_state() ) {
        death_committed_ = false;
        clear_killer();
        recalc_sight_limits();
        return false;
    }
    // Final callbacks cannot reopen prevention or replay native death through reentry.
    validate_source();
    death_finalized_ = true;
    if( Creature *killer = get_killer(); killer && killer->as_character() ) {
        get_event_bus().send<event_type::character_kills_character>(
            killer->as_character()->getID(), getID(), get_name(), "" );
    }
    get_event_bus().send<event_type::avatar_dies>();
    return true;
}

void avatar::place_final_corpse( map &here )
{
    if( !death_finalized_ || death_finalizing_ ||
        death_presentation_ == death_presentation::corpse_placed ) {
        return;
    }
    death_finalizing_ = true;
    const on_out_of_scope leave( [&]() {
        death_finalizing_ = false;
    } );
    death_presentation_ = death_presentation::corpse_placed;
    if( in_vehicle ) {
        here.unboard_vehicle( pos_bub() );
    }
    place_corpse( &here );
}

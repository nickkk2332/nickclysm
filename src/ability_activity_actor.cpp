#include "ability_activity_actor.h"

#include "ability_runtime.h"
#include "character.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "incapacitation.h"
#include "json.h"
#include "player_activity.h"

static const activity_id ACT_ABILITY( "ACT_ABILITY" );

const activity_id &ability_activity_actor::get_type() const
{
    return ACT_ABILITY;
}

void ability_activity_actor::start( player_activity &act, Character & )
{
    act.moves_total = 1;
    act.moves_left = 1;
}

void ability_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( !g->abilities().owns_avatar_activity( instance ) ) {
        act.set_to_null();
        return;
    }
    who.set_moves( 0 );
}

void ability_activity_actor::finish( player_activity &act, Character & )
{
    act.set_to_null();
}

void ability_activity_actor::canceled( player_activity &, Character &who )
{
    g->abilities().cancel( instance, incapacitation::unconscious( who ) ?
                           "caster_incapacitated" : "player_cancelled" );
}

std::unique_ptr<activity_actor> ability_activity_actor::clone() const
{
    return std::make_unique<ability_activity_actor>( *this );
}

void ability_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "instance", instance );
    jsout.end_object();
}

std::unique_ptr<activity_actor> ability_activity_actor::deserialize( JsonValue &jsin )
{
    const JsonObject jo = jsin.get_object();
    int64_t id = 0;
    jo.read( "instance", id, true );
    return std::make_unique<ability_activity_actor>( id );
}

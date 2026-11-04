#include "execution_activity_actor.h"

#include "character.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json.h"
#include "player_activity.h"

const activity_id &execution_activity_actor::get_type() const
{
    static const activity_id id( "ACT_EXECUTION" );
    return id;
}

void execution_activity_actor::start( player_activity &act, Character & )
{
    act.moves_total = 1;
    act.moves_left = 1;
}

void execution_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( !g->executions().owns_avatar_activity( instance ) ) {
        act.set_to_null();
        return;
    }
    // The world-turn pump owns progress. This activity only consumes voluntary actions.
    who.set_moves( 0 );
}

void execution_activity_actor::finish( player_activity &act, Character & )
{
    act.set_to_null();
}

void execution_activity_actor::canceled( player_activity &, Character & )
{
    g->executions().cancel( instance, "player_cancelled" );
}

std::unique_ptr<activity_actor> execution_activity_actor::clone() const
{
    return std::make_unique<execution_activity_actor>( *this );
}

void execution_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "instance", instance );
    jsout.end_object();
}

std::unique_ptr<activity_actor> execution_activity_actor::deserialize( JsonValue &jsin )
{
    const JsonObject jo = jsin.get_object();
    int64_t id = 0;
    jo.read( "instance", id, true );
    return std::make_unique<execution_activity_actor>( id );
}

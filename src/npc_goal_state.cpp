#include "npc_goal_state.h"

#include "flexbuffer_json.h"
#include "json.h"

void npc_goal_state::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "goal", goal );
    jsout.member( "started", started );
    jsout.member( "turns_active", turns_active );
    jsout.member( "last_turn", last_turn );
    jsout.end_object();
}

void npc_goal_state::deserialize( const JsonObject &jo )
{
    jo.allow_omitted_members();
    *this = npc_goal_state();
    jo.read( "goal", goal );
    jo.read( "started", started );
    jo.read( "turns_active", turns_active );
    jo.read( "last_turn", last_turn );
    // resuming the goal is no change
    reported = goal.empty() ? "idle" : goal;
}

#include "npc_ai_hooks_helpers.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "condition.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

ai_hooks_world::ai_hooks_world()
{
    if( bubble != tripoint_abs_sm() ) {
        get_map().load( tripoint_abs_sm(), false );
    }
    clear_map();
    followers = get_avatar().follower_ids;
    get_avatar().setpos( get_map(), npc_spot + tripoint( -10, 0, 0 ) );
    for( const char *id : {
             "your_followers", "amf", "tacoma_commune", "debuggers", "test_npc_ai_deserters",
             "test_npc_ai_hawks", "test_npc_ai_doves"
         } ) {
        const std::map<faction_id, faction> &all = g->faction_manager_ptr->all();
        const auto found = all.find( faction_id( id ) );
        factions.emplace_back( faction_id( id ), found == all.end() ? std::nullopt :
                               std::optional<faction>( found->second ) );
    }
}

ai_hooks_world::~ai_hooks_world() noexcept( false )
{
    clear_npcs();
    for( const std::pair<faction_id, std::optional<faction>> &fac : factions ) {
        if( fac.second ) {
            *g->faction_manager_ptr->get( fac.first ) = *fac.second;
        } else {
            g->faction_manager_ptr->remove_faction( fac.first );
        }
    }
    avatar &u = get_avatar();
    u.follower_ids = followers;
    u.cash = cash;
    calendar::turn = turn;
    for( const std::string &name : hook_globals ) {
        get_globals().remove_global_value( name );
    }
    map &here = get_map();
    if( here.get_abs_sub() != bubble ) {
        here.load( bubble, false );
    }
    u.setpos( here, here.get_bub( avatar_at ) );
}

npc &hooks_npc()
{
    npc &guy = spawn_npc( npc_spot.xy(), "test_talker" );
    clear_character( guy, true );
    guy.setpos( get_map(), npc_spot );
    guy.set_attitude( NPCATT_NULL );
    guy.set_mission( NPC_MISSION_NULL );
    guy.guard_pos = std::nullopt;
    guy.clear_ai_guard_pos();
    guy.personality = npc_personality();
    guy.op_of_u = npc_opinion();
    get_map().build_map_cache( 0 );
    return guy;
}

npc_event_recorder::npc_event_recorder()
{
    get_event_bus().subscribe( this );
}

void npc_event_recorder::notify( const cata::event &e )
{
    switch( e.type() ) {
        case event_type::npc_attitude_changed:
        case event_type::npc_becomes_hostile:
        case event_type::npc_recruited:
        case event_type::npc_dismissed:
        case event_type::npc_mutinies:
            events.push_back( e );
            break;
        default:
            break;
    }
}

std::vector<cata::event> npc_event_recorder::of( const event_type type ) const
{
    std::vector<cata::event> ret;
    for( const cata::event &e : events ) {
        if( e.type() == type ) {
            ret.push_back( e );
        }
    }
    return ret;
}

bool test_condition( const std::string &condition, const_dialogue const &d )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
    finalize_conditions();
    return cond( d );
}

talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "npc_ai_hooks_test" );
    finalize_conditions();
    return ret;
}

void run_effect( const std::string &effect, dialogue &d )
{
    load_effect( effect ).apply( d );
}

double eval_math( const std::string_view expression, const_dialogue const &d )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

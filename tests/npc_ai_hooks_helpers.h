#pragma once
#ifndef CATA_TESTS_NPC_AI_HOOKS_HELPERS_H
#define CATA_TESTS_NPC_AI_HOOKS_HELPERS_H

// What the NPC AI hook tests share (npc_ai_events_test.cpp, npc_ai_eoc_test.cpp,
// npc_ai_tuning_test.cpp): the world they put back, their NPC, a recorder of the NPC events, and
// ways to run conditions, effects and math.  Their test classes, factions and EOCs are in
// data/mods/TEST_DATA/npc_ai_hooks.json.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_variant.h"
#include "character_id.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

inline const tripoint_bub_ms npc_spot( 60, 60, 0 );

inline const std::vector<std::string> hook_globals = {
    "test_ai_listen", "test_ai_attitude_changes", "test_ai_old", "test_ai_new", "test_ai_cause",
    "test_ai_u", "test_ai_mutiny_to", "test_ai_mutiny_u", "test_ai_var"
};

// What these tests change of the world the other tests share goes back as it was: the reality
// bubble and the avatar's place, the time, the avatar's followers and cash, the factions the
// tests touch and the test globals.  Their NPCs go.
struct ai_hooks_world {
    const tripoint_abs_sm bubble = get_map().get_abs_sub();
    const tripoint_abs_ms avatar_at = get_avatar().pos_abs();
    const time_point turn = calendar::turn;
    // as they are once the other tests' NPCs are gone
    std::set<character_id> followers;
    const int cash = get_avatar().cash;
    // each faction as it was, nullopt for one that didn't exist yet
    std::vector<std::pair<faction_id, std::optional<faction>>> factions;

    ai_hooks_world() {
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
    ai_hooks_world( const ai_hooks_world & ) = delete;
    ai_hooks_world &operator=( const ai_hooks_world & ) = delete;
    ~ai_hooks_world() {
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
};

// an NPC with no attitude, mission, post or opinion of the avatar, in an empty field
inline npc &hooks_npc()
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

// the NPC events, in the order they went out
struct npc_event_recorder : public event_subscriber {
    std::vector<cata::event> events;

    npc_event_recorder() {
        get_event_bus().subscribe( this );
    }
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
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
    std::vector<cata::event> of( const event_type type ) const {
        std::vector<cata::event> ret;
        for( const cata::event &e : events ) {
            if( e.type() == type ) {
                ret.push_back( e );
            }
        }
        return ret;
    }
};

inline std::string str_field( const cata::event &e, const std::string &field )
{
    return e.get<cata_variant_type::string>( field );
}

inline std::string global_string( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

inline double global_number( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? 0.0 : value->dbl();
}

inline bool test_condition( const std::string &condition, const_dialogue const &d )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
    finalize_conditions();
    return cond( d );
}

inline talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "npc_ai_hooks_test" );
    finalize_conditions();
    return ret;
}

inline void run_effect( const std::string &effect, dialogue &d )
{
    load_effect( effect ).apply( d );
}

inline double eval_math( const std::string &expression, const_dialogue const &d )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}


#endif // CATA_TESTS_NPC_AI_HOOKS_HELPERS_H

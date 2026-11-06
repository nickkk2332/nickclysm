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
#include <string_view>
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
    "test_ai_u", "test_ai_mutiny_to", "test_ai_mutiny_u", "test_ai_var", "test_ai_guard_recruits",
    "test_ai_flip", "test_ai_calm"
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

    ai_hooks_world();
    ai_hooks_world( const ai_hooks_world & ) = delete;
    ai_hooks_world &operator=( const ai_hooks_world & ) = delete;
    ~ai_hooks_world() noexcept( false );
};

// an NPC with no attitude, mission, post or opinion of the avatar, in an empty field
npc &hooks_npc();

// the NPC events, in the order they went out
struct npc_event_recorder : public event_subscriber {
    std::vector<cata::event> events;

    npc_event_recorder();
    using event_subscriber::notify;
    void notify( const cata::event &e ) override;
    std::vector<cata::event> of( event_type type ) const;
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

bool test_condition( const std::string &condition, const_dialogue const &d );
talk_effect_t load_effect( const std::string &effect );
void run_effect( const std::string &effect, dialogue &d );
double eval_math( std::string_view expression, const_dialogue const &d );

#endif // CATA_TESTS_NPC_AI_HOOKS_HELPERS_H

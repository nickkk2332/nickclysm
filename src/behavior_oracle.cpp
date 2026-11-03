#include "behavior_oracle.h"

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "behavior.h"
#include "character_oracle.h"
#include "debug.h"
#include "monster_oracle.h"

namespace behavior
{

status_t return_running( const oracle_t *, std::string_view )
{
    return status_t::running;
}

namespace
{

template<typename Oracle>
constexpr oracle_kind kind_of()
{
    if constexpr( std::is_same_v<Oracle, character_oracle_t> ) {
        return oracle_kind::character;
    } else {
        static_assert( std::is_same_v<Oracle, monster_oracle_t> );
        return oracle_kind::monster;
    }
}

// A member of a specialized oracle, called through any oracle. When the subject is another kind
// of oracle (a monster predicate in an NPC tree, or the reverse), it gives `fallback`, with one
// debugmsg per predicate, instead of calling the member on the wrong type.
template<typename Oracle, typename R>
std::function<R( const oracle_t *, std::string_view )>
checked( const std::string &name, R( Oracle::*fun )( std::string_view ) const, R fallback )
{
    std::shared_ptr<bool> reported = std::make_shared<bool>( false );
    return [name, fun, fallback, reported]( const oracle_t *subject, std::string_view argument ) {
        if( subject == nullptr ) {
            return fallback;
        }
        if( subject->kind() != kind_of<Oracle>() ) {
            if( !*reported ) {
                *reported = true;
                debugmsg( "Behavior predicate or score \"%s\" is for %s, and can't read this subject.",
                          name, kind_of<Oracle>() == oracle_kind::character ? "NPCs" : "monsters" );
            }
            return fallback;
        }
        return ( static_cast<const Oracle *>( subject )->*fun )( argument );
    };
}

using character_predicate = status_t ( character_oracle_t::* )( std::string_view ) const;
using monster_predicate = status_t ( monster_oracle_t::* )( std::string_view ) const;
using character_score = float ( character_oracle_t::* )( std::string_view ) const;

const std::vector<std::pair<std::string, character_predicate>> &character_predicates()
{
    static const std::vector<std::pair<std::string, character_predicate>> predicates = {
        { "npc_needs_temperature_regulation", &character_oracle_t::needs_temperature_regulation },
        { "npc_needs_water_badly", &character_oracle_t::needs_water_badly },
        { "npc_needs_food_badly", &character_oracle_t::needs_food_badly },
        { "npc_can_wear_warmer_clothes", &character_oracle_t::can_wear_warmer_clothes },
        { "npc_can_make_fire", &character_oracle_t::can_make_fire },
        { "npc_can_take_shelter", &character_oracle_t::can_take_shelter },
        { "npc_has_water", &character_oracle_t::has_water },
        { "npc_has_food", &character_oracle_t::has_food },
        { "npc_can_obtain_food", &character_oracle_t::can_obtain_food },
        { "npc_can_obtain_water", &character_oracle_t::can_obtain_water },
        { "npc_can_regulate_temperature", &character_oracle_t::can_regulate_temperature },
        { "npc_needs_sleep_badly", &character_oracle_t::needs_sleep_badly },
        { "npc_can_sleep", &character_oracle_t::can_sleep },
        { "npc_in_danger", &character_oracle_t::in_danger },
        { "npc_should_flee", &character_oracle_t::should_flee },
        { "npc_has_target", &character_oracle_t::has_target },
        { "npc_has_sound_alerts", &character_oracle_t::has_sound_alerts },
        { "npc_displaced_from_post", &character_oracle_t::displaced_from_post },
        { "npc_on_shift", &character_oracle_t::on_shift },
        { "npc_is_following", &character_oracle_t::npc_is_following },
        { "npc_should_embark", &character_oracle_t::npc_should_embark },
        { "npc_has_goto_order", &character_oracle_t::npc_has_goto_order },
        { "npc_has_camp_job", &character_oracle_t::has_camp_job },
        { "npc_is_away_from_camp", &character_oracle_t::is_away_from_camp },
        { "npc_is_camp_idle", &character_oracle_t::is_camp_idle }
    };
    return predicates;
}

const std::vector<std::pair<std::string, monster_predicate>> &monster_predicates()
{
    static const std::vector<std::pair<std::string, monster_predicate>> predicates = {
        { "monster_not_hallucination", &monster_oracle_t::not_hallucination },
        { "monster_items_available", &monster_oracle_t::items_available },
        { "monster_split_possible", &monster_oracle_t::split_possible },
        { "monster_adjacent_plants", &monster_oracle_t::adjacent_plants },
        { "monster_special_available", &monster_oracle_t::special_available }
    };
    return predicates;
}

const std::vector<std::pair<std::string, character_score>> &character_scores()
{
    static const std::vector<std::pair<std::string, character_score>> scores = {
        { "npc_thirst_urgency", &character_oracle_t::thirst_urgency },
        { "npc_hunger_urgency", &character_oracle_t::hunger_urgency },
        { "npc_temperature_urgency", &character_oracle_t::temperature_urgency },
        { "npc_sleepiness_urgency", &character_oracle_t::sleepiness_urgency },
        { "npc_duty_urgency", &character_oracle_t::duty_urgency },
        { "npc_following_urgency", &character_oracle_t::npc_following_urgency },
        { "npc_embark_urgency", &character_oracle_t::npc_embark_urgency },
        { "npc_goto_order_urgency", &character_oracle_t::npc_goto_order_urgency },
        { "npc_camp_work_urgency", &character_oracle_t::camp_work_urgency },
        { "npc_return_to_camp_urgency", &character_oracle_t::return_to_camp_urgency },
        { "npc_free_time_urgency", &character_oracle_t::free_time_urgency }
    };
    return scores;
}

std::unordered_map<std::string, std::function<status_t( const oracle_t *, std::string_view )>>
        all_predicates()
{
    std::unordered_map<std::string, std::function<status_t( const oracle_t *, std::string_view )>> ret;
    for( const auto &[name, fun] : character_predicates() ) {
        ret.emplace( name, checked( name, fun, status_t::failure ) );
    }
    for( const auto &[name, fun] : monster_predicates() ) {
        ret.emplace( name, checked( name, fun, status_t::failure ) );
    }
    return ret;
}

std::unordered_map<std::string, std::function<float( const oracle_t *, std::string_view )>>
        all_scores()
{
    std::unordered_map<std::string, std::function<float( const oracle_t *, std::string_view )>> ret;
    for( const auto &[name, fun] : character_scores() ) {
        ret.emplace( name, checked( name, fun, 0.0f ) );
    }
    return ret;
}

template<typename T>
bool lists( const std::vector<std::pair<std::string, T>> &list, const std::string &name )
{
    for( const auto &entry : list ) {
        if( entry.first == name ) {
            return true;
        }
    }
    return false;
}

} // namespace

std::unordered_map<std::string, std::function<status_t( const oracle_t *, std::string_view ) >>
        predicate_map = all_predicates();

std::unordered_map<std::string, std::function<float( const oracle_t *, std::string_view )>>
        score_predicate_map = all_scores();

// monster_special_available: "NAME" runs when the special attack is ready, "!NAME" also when the
// monster has no such special attack.
std::unordered_set<std::string> predicates_with_argument = { "monster_special_available" };

std::unordered_set<std::string> scores_with_argument;

oracle_kind predicate_kind( const std::string &name )
{
    if( lists( character_predicates(), name ) ) {
        return oracle_kind::character;
    }
    if( lists( monster_predicates(), name ) ) {
        return oracle_kind::monster;
    }
    return oracle_kind::any;
}

oracle_kind score_kind( const std::string &name )
{
    return lists( character_scores(), name ) ? oracle_kind::character : oracle_kind::any;
}

} // namespace behavior

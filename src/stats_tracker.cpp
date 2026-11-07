#include "stats_tracker.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_assert.h"
#include "debug.h"
#include "event_statistics.h"
#include "json.h"

static bool event_data_matches( const cata::event::data_type &data,
                                const cata::event::data_type &criteria )
{
    for( const auto &criterion : criteria ) {
        auto it = data.find( criterion.first );
        if( it == data.end() || it->second != criterion.second ) {
            return false;
        }
    }
    return true;
}

event_summary::event_summary() :
    count( 0 ),
    first( calendar::before_time_starts ),
    last( calendar::before_time_starts )
{
}

event_summary::event_summary( int c, time_point f, time_point l ) :
    count( c ),
    first( f ),
    last( l )
{
}

void event_summary::add( const cata::event &e )
{
    if( count == 0 ) {
        first = last = e.time();
    } else {
        last = std::max( last, e.time() );
    }
    ++count;
}

void event_summary::add( const event_summary &s )
{
    if( count == 0 ) {
        *this = s;
    } else {
        count += s.count;
        first = std::min( first, s.first );
        last = std::max( last, s.last );
    }
}

void event_summary::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "count", count );
    jsout.member( "first", first );
    jsout.member( "last", last );
    jsout.end_object();
}

void event_summary::deserialize( const JsonObject &jo )
{
    jo.read( "count", count, true );
    jo.read( "first", first, true );
    jo.read( "last", last, true );
}

void event_multiset::set_type( event_type type )
{
    // Used during stats_tracker deserialization to set the type
    cata_assert( type_ == event_type::num_event_types );
    type_ = type;
}

int event_multiset::count() const
{
    int total = 0;
    for( const auto &pair : summaries_ ) {
        total += pair.second.count;
    }
    return total;
}

int event_multiset::count( const cata::event::data_type &criteria ) const
{
    int total = 0;
    for( const auto &pair : summaries_ ) {
        if( event_data_matches( pair.first, criteria ) ) {
            total += pair.second.count;
        }
    }
    return total;
}

int event_multiset::total( const std::string &field ) const
{
    return total( field, {} );
}

int event_multiset::total( const std::string &field, const cata::event::data_type &criteria ) const
{
    int total = 0;
    for( const auto &pair : summaries_ ) {
        auto it = pair.first.find( field );
        if( it == pair.first.end() ) {
            continue;
        }
        if( event_data_matches( pair.first, criteria ) ) {
            total += pair.second.count * it->second.get<cata_variant_type::int_>();
        }
    }
    return total;
}

// Both are 0 when there are no events, and otherwise the smallest (largest) value seen
int event_multiset::minimum( const std::string &field ) const
{
    std::optional<int> minimum;
    for( const auto &pair : summaries_ ) {
        auto it = pair.first.find( field );
        if( it == pair.first.end() ) {
            continue;
        }
        const int potential = it->second.get<cata_variant_type::int_>();
        if( !minimum || potential < *minimum ) {
            minimum = potential;
        }
    }
    return minimum.value_or( 0 );
}

int event_multiset::maximum( const std::string &field ) const
{
    std::optional<int> maximum;
    for( const auto &pair : summaries_ ) {
        auto it = pair.first.find( field );
        if( it == pair.first.end() ) {
            continue;
        }
        const int potential = it->second.get<cata_variant_type::int_>();
        if( !maximum || potential > *maximum ) {
            maximum = potential;
        }
    }
    return maximum.value_or( 0 );
}

namespace
{
template<time_point event_summary::*Member>
struct compare_times {
    bool operator()( const event_multiset::summaries_type::value_type &l,
                     const event_multiset::summaries_type::value_type &r ) const {
        return l.second.*Member < r.second.*Member;
    }
};
} // namespace

std::optional<event_multiset::summaries_type::value_type> event_multiset::first() const
{
    auto minimum = std::min_element( summaries_.begin(), summaries_.end(),
                                     compare_times<&event_summary::first>() );
    if( minimum == summaries_.end() ) {
        return std::nullopt;
    }
    return *minimum;
}

std::optional<event_multiset::summaries_type::value_type> event_multiset::last() const
{
    auto minimum = std::max_element( summaries_.begin(), summaries_.end(),
                                     compare_times<&event_summary::last>() );
    if( minimum == summaries_.end() ) {
        return std::nullopt;
    }
    return *minimum;
}

void event_multiset::add( const cata::event &e )
{
    summaries_[e.data()].add( e );
}

void event_multiset::add( const summaries_type::value_type &e )
{
    summaries_[e.first].add( e.second );
}

base_watcher::~base_watcher()
{
    if( subscribed_to ) {
        subscribed_to->unwatch( this );
    }
}

void base_watcher::on_subscribe( stats_tracker *s )
{
    if( subscribed_to ) {
        debugmsg( "Subscribing a single base_watcher multiple times is not supported" );
    }
    subscribed_to = s;
}

void base_watcher::on_unsubscribe( stats_tracker *s )
{
    if( subscribed_to != s ) {
        debugmsg( "Unexpected notification of unsubscription from wrong stats_tracker" );
    } else {
        subscribed_to = nullptr;
    }
}

stats_tracker_state::~stats_tracker_state() = default;

const cata_variant &stats_tracker_multiset_state::get_value() const
{
    cata_fatal( "Trying to get a variant value from a multiset state" );
}

stats_tracker::~stats_tracker()
{
    unwatch_all();
}

event_multiset &stats_tracker::get_events( event_type type )
{
    return data.emplace( type, event_multiset( type ) ).first->second;
}

event_multiset stats_tracker::get_events(
    const string_id<event_transformation> &transform_id )
{
    return transform_id->value( *this );
}

cata_variant stats_tracker::value_of( const string_id<event_statistic> &stat )
{
    return stat->value( *this );
}

void stats_tracker::add_watcher( event_type type, event_multiset_watcher *watcher )
{
    event_type_watchers[type].insert( watcher );
    watcher->on_subscribe( this );
}

void stats_tracker::add_watcher( const string_id<event_transformation> &id,
                                 event_multiset_watcher *watcher )
{
    event_transformation_watchers[id].insert( watcher );
    watcher->on_subscribe( this );
    std::unique_ptr<stats_tracker_state> &state = event_transformation_states[ id ];
    if( !state ) {
        state = id->watch( *this );
    }
}

const cata_variant &stats_tracker::add_watcher(
    const string_id<event_statistic> &id, stat_watcher *watcher )
{
    stat_watchers[id].insert( watcher );
    watcher->on_subscribe( this );
    std::unique_ptr<stats_tracker_state> &state = stat_states[ id ];
    if( !state ) {
        state = id->watch( *this );
    }
    return state->get_value();
}

void stats_tracker::unwatch( base_watcher *watcher )
{
    // Use a slow O(n) approach for now; if it proves problematic we can build
    // an index, but that seems over-complex.
    auto erase_from = [watcher]( auto & map_of_watcher_sets ) {
        for( auto &p : map_of_watcher_sets ) {
            auto &set = p.second;
            if( set.erase( watcher ) ) {
                return true;
            }
        }
        return false;
    };

    if( erase_from( event_type_watchers ) ||
        erase_from( event_transformation_watchers ) ||
        erase_from( stat_watchers ) ) {
        return;
    }
    debugmsg( "unwatch for a watcher not found" );
}

void stats_tracker::transformed_set_changed( const string_id<event_transformation> &id,
        const cata::event &new_element )
{
    auto it = event_transformation_watchers.find( id );
    if( it != event_transformation_watchers.end() ) {
        it->second.send_to_all( &event_multiset_watcher::event_added, new_element, *this );
    }
}

void stats_tracker::transformed_set_changed( const string_id<event_transformation> &id,
        const event_multiset &new_value )
{
    auto it = event_transformation_watchers.find( id );
    if( it != event_transformation_watchers.end() ) {
        it->second.send_to_all( &event_multiset_watcher::events_reset, new_value, *this );
    }
}

void stats_tracker::stat_value_changed( const string_id<event_statistic> &id,
                                        const cata_variant &new_value )
{
    auto it = stat_watchers.find( id );
    if( it != stat_watchers.end() ) {
        it->second.send_to_all( &stat_watcher::new_value, new_value, *this );
    }
}

std::vector<const score *> stats_tracker::valid_scores() const
{
    std::vector<const score *> result;
    for( const score &scr : score::get_all() ) {
        // EVT-J05: or one that allows it, added to a game that started without it
        if( initial_scores.count( scr.id ) ||
            ( scr.allows_late_tracking() && !initial_scores.empty() ) ) {
            result.push_back( &scr );
        }
    }
    return result;
}

void stats_tracker::clear()
{
    unwatch_all();
    data.clear();
    event_transformation_states.clear();
    stat_states.clear();
    initial_scores.clear();
}

void stats_tracker::unwatch_all()
{
    auto unsub_all = [&]( auto & map_of_watcher_sets ) {
        for( auto const &p : map_of_watcher_sets ) {
            const auto &set = p.second;
            set.send_to_all( &base_watcher::on_unsubscribe, this );
        }
        map_of_watcher_sets.clear();
    };
    unsub_all( event_type_watchers );
    unsub_all( event_transformation_watchers );
    unsub_all( stat_watchers );
}

bool stats_tracker::records( const event_type type )
{
    // these would keep a summary per NPC and pair of goals, attitudes or movement modes, per mod
    // setting and pair of values, per tile destroyed or overmap tile generated, per teleport, sound
    // heard or step, per reload or unload, per armor hit or wound on each part, per bionic or trait
    // switched on or off, per monster special attack used, or per item type and
    // count moved, or per trade: kept for good, they would grow every save
    switch( type ) {
        case event_type::npc_goal_changed:
        case event_type::npc_attitude_changed:
        case event_type::mod_setting_changed:
        case event_type::tile_destroyed:
        case event_type::creature_teleported:
        case event_type::character_hears_sound:
        case event_type::omt_generated:
        case event_type::map_extra_spawned:
        case event_type::character_reloads_item:
        case event_type::character_unloads_item:
        case event_type::character_armor_damaged:
        case event_type::character_picks_up_item:
        case event_type::character_drops_item:
        case event_type::character_stores_item:
        case event_type::character_changes_move_mode:
        case event_type::character_moves:
        case event_type::character_gains_wound:
        case event_type::character_wound_progresses:
        case event_type::character_loses_wound:
        case event_type::activates_bionic:
        case event_type::deactivates_bionic:
        case event_type::activates_mutation:
        case event_type::deactivates_mutation:
        case event_type::monster_uses_special:
        case event_type::avatar_trades:
            return false;
        default:
            return true;
    }
}

const std::vector<std::string> &stats_tracker::unrecorded_fields( const event_type type )
{
    static const std::vector<std::string> none;
    static const std::vector<std::string> pos = { "pos" };
    static const std::vector<std::string> target = { "target" };
    // every monster type times every damage amount and type: thousands of summaries in a
    // long game, where there were a few hundred
    static const std::vector<std::string> monster_hurt = {
        "victim_type", "bodypart", "damage_type"
    };
    // CMBT-J04: one summary per amount of damage, for every hit and block, would grow every save
    static const std::vector<std::string> hit_damage = { "damage" };
    static const std::vector<std::string> damage_blocked = { "damage_blocked" };
    // every name, speed and damage of a collision: kept by type and part only
    static const std::vector<std::string> vehicle_hit = {
        "target_name", "velocity_delta", "damage"
    };
    // a summary per vehicle name (which players choose) would grow without end
    static const std::vector<std::string> vehicle_name = { "vehicle_name" };
    switch( type ) {
        case event_type::character_melee_hits:
        case event_type::monster_melee_attacks_character:
        case event_type::monster_melee_attacks_monster:
            return hit_damage;
        case event_type::character_blocks:
            return damage_blocked;
        case event_type::becomes_wanted:
        case event_type::character_smashes_tile:
        case event_type::character_triggers_trap:
        case event_type::cuts_tree:
            return pos;
        case event_type::character_casts_spell:
        case event_type::spellcasting_finish:
            return target;
        case event_type::monster_takes_damage:
            return monster_hurt;
        case event_type::vehicle_collides:
            return vehicle_hit;
        case event_type::vehicle_part_installed:
        case event_type::vehicle_part_removed:
        case event_type::vehicle_part_repaired:
        case event_type::vehicle_engine_started:
        case event_type::vehicle_part_toggled:
            return vehicle_name;
        default:
            return none;
    }
}

void stats_tracker::notify( const cata::event &sent )
{
    const event_type type = sent.type();
    if( !records( type ) ) {
        return;
    }
    const std::vector<std::string> &unrecorded = unrecorded_fields( type );
    std::optional<cata::event> trimmed;
    if( !unrecorded.empty() ) {
        cata::event::data_type kept = sent.data();
        for( const std::string &field : unrecorded ) {
            kept.erase( field );
        }
        trimmed.emplace( type, sent.time(), std::move( kept ) );
    }
    const cata::event &e = trimmed ? *trimmed : sent;
    get_events( type ).add( e );

    auto it = event_type_watchers.find( type );
    if( it != event_type_watchers.end() ) {
        it->second.send_to_all( &event_multiset_watcher::event_added, e, *this );
    }

    if( e.type() == event_type::game_start ) {
        cata_assert( initial_scores.empty() );
        for( const score &scr : score::get_all() ) {
            initial_scores.insert( scr.id );
        }
    }
}

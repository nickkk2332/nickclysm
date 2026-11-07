#include "effect_on_condition.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <ostream>
#include <queue>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "condition.h"
#include "creature.h"
#include "debug.h"
#include "debug_capture.h"
#include "event.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "generic_factory.h"
#include "item_location.h"
#include "math_parser_diag_value.h"
#include "mod_tracker.h"
#include "monster.h"
#include "npc.h"
#include "npctalk.h"
#include "profession.h"
#include "scenario.h"
#include "string_formatter.h"
#include "talker.h"
#include "type_id.h"

namespace io
{
    // *INDENT-OFF*
    template<>
    std::string enum_to_string<eoc_type>( eoc_type data )
    {
        switch ( data ) {
        case eoc_type::ACTIVATION: return "ACTIVATION";
        case eoc_type::RECURRING: return "RECURRING";
        case eoc_type::AVATAR_DEATH: return "AVATAR_DEATH";
        case eoc_type::NPC_DEATH: return "NPC_DEATH";
        case eoc_type::PREVENT_DEATH: return "PREVENT_DEATH";
        case eoc_type::EVENT: return "EVENT";
        case eoc_type::PREVENT_MONSTER_DEATH: return "PREVENT_MONSTER_DEATH";
        case eoc_type::NUM_EOC_TYPES: break;
        }
        cata_fatal( "Invalid eoc_type" );
    }    
    // *INDENT-ON*
} // namespace io

namespace
{
generic_factory<effect_on_condition>
effect_on_condition_factory( "effect_on_condition" );
} // namespace

template<>
const effect_on_condition &effect_on_condition_id::obj() const
{
    return effect_on_condition_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<effect_on_condition>::is_valid() const
{
    return effect_on_condition_factory.is_valid( *this );
}

static std::vector<std::pair<std::string, std::string>> pending_eoc_refs;
static std::vector<std::function<void()>> pending_load_checks;
// effect_on_condition_migration entries: what a saved queued or inactive EOC becomes; none: it
// is dropped
static std::map<effect_on_condition_id, std::optional<effect_on_condition_id>> eoc_migrations;

void effect_on_conditions::load_migration( const JsonObject &jo )
{
    const effect_on_condition_id from( jo.get_string( "from" ) );
    std::optional<effect_on_condition_id> to;
    if( jo.has_member( "to" ) ) {
        to = effect_on_condition_id( jo.get_string( "to" ) );
    }
    // A later migration of the same id replaces the earlier one
    eoc_migrations.insert_or_assign( from, to );
}

std::optional<effect_on_condition_id> effect_on_conditions::migrate_saved(
    const effect_on_condition_id &id )
{
    const auto it = eoc_migrations.find( id );
    if( it == eoc_migrations.end() ) {
        return id;
    }
    return it->second;
}

void effect_on_conditions::migrate_saved( std::vector<effect_on_condition_id> &eocs )
{
    if( eoc_migrations.empty() ) {
        return;
    }
    std::vector<effect_on_condition_id> migrated;
    migrated.reserve( eocs.size() );
    for( const effect_on_condition_id &id : eocs ) {
        if( std::optional<effect_on_condition_id> to = migrate_saved( id ) ) {
            migrated.push_back( *to );
        }
    }
    eocs = std::move( migrated );
}

namespace
{
// The EOCs that run on something the game looks up often, by what they run on: built from the
// loaded EOCs when first needed, and again after EOCs are loaded or reset.
struct eocs_by_trigger {
    bool built = false;
    // EVENT EOCs on a mod-defined event (EOC-J06), by its name
    std::map<std::string, std::vector<effect_on_condition_id>> custom_events;
    // EOC-J10
    std::vector<effect_on_condition_id> prevent_monster_death;
};

eocs_by_trigger &eocs_by_trigger_cache()
{
    static eocs_by_trigger cache;
    return cache;
}

void invalidate_eocs_by_trigger()
{
    eocs_by_trigger_cache() = eocs_by_trigger();
}

const eocs_by_trigger &get_eocs_by_trigger()
{
    eocs_by_trigger &cache = eocs_by_trigger_cache();
    if( !cache.built ) {
        for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
            if( eoc.type == eoc_type::EVENT && !eoc.custom_event.empty() ) {
                cache.custom_events[eoc.custom_event].push_back( eoc.id );
            } else if( eoc.type == eoc_type::PREVENT_MONSTER_DEATH ) {
                cache.prevent_monster_death.push_back( eoc.id );
            }
        }
        cache.built = true;
    }
    return cache;
}
} // namespace

void effect_on_conditions::add_load_check( std::function<void()> check )
{
    pending_load_checks.emplace_back( std::move( check ) );
}

void effect_on_conditions::check_consistency()
{
    effect_on_condition_factory.check();
    for( const auto &[eoc_str, context] : pending_eoc_refs ) {
        effect_on_condition_id eid( eoc_str );
        if( !eid.is_valid() ) {
            debugmsg( "EOC reference \"%s\" (loaded from %s) does not exist", eoc_str, context );
        }
    }
    pending_eoc_refs.clear();
    for( const std::function<void()> &check : pending_load_checks ) {
        check();
    }
    pending_load_checks.clear();
    for( const auto &[from, to] : eoc_migrations ) {
        if( to && !to->is_valid() ) {
            debugmsg( "effect_on_condition_migration from %s names an EOC that does not exist: %s",
                      from.str(), to->str() );
        }
    }
}

void effect_on_condition::load( const JsonObject &jo, std::string_view src )
{
    mandatory( jo, was_loaded, "id", id );
    optional( jo, was_loaded, "eoc_type", type, eoc_type::NUM_EOC_TYPES );
    if( jo.has_member( "recurrence" ) ) {
        if( type != eoc_type::NUM_EOC_TYPES && type != eoc_type::RECURRING ) {
            jo.throw_error( "A recurring effect_on_condition must be of type RECURRING." );
        }
        type = eoc_type::RECURRING;
        optional( jo, was_loaded, "recurrence", recurrence );
    }
    if( type == eoc_type::NUM_EOC_TYPES ) {
        type = eoc_type::ACTIVATION;
    }

    if( jo.has_member( "deactivate_condition" ) ) {
        read_condition( jo, "deactivate_condition", deactivate_condition, false );
        has_deactivate_condition = true;
    }
    if( jo.has_member( "condition" ) ) {
        read_condition( jo, "condition", condition, false );
        has_condition = true;
    }
    true_effect.load_effect( jo, "effect", std::string( src ) );

    if( jo.has_member( "false_effect" ) ) {
        false_effect.load_effect( jo, "false_effect", std::string( src ) );
        has_false_effect = true;
    }

    optional( jo, was_loaded, "run_for_npcs", run_for_npcs, false );
    optional( jo, was_loaded, "global", global, false );
    if( !global && run_for_npcs ) {
        jo.throw_error( "run_for_npcs should only be true for global effect_on_conditions." );
    }

    if( type == eoc_type::EVENT ) {
        // EOC-J06: a mod-defined event, which trigger_custom_event raises
        if( jo.has_member( "custom_event" ) ) {
            if( jo.has_member( "required_event" ) ) {
                jo.throw_error( R"(An EVENT effect_on_condition takes "required_event" or )"
                                R"("custom_event", not both.)" );
            }
            mandatory( jo, was_loaded, "custom_event", custom_event );
            if( custom_event.empty() ) {
                jo.throw_error_at( "custom_event", "custom_event can't be empty." );
            }
        } else if( !was_loaded || jo.has_member( "required_event" ) ) {
            mandatory( jo, was_loaded, "required_event", required_event );
            custom_event.clear();
        }
    }
}

effect_on_condition_id effect_on_conditions::load_inline_eoc( const JsonValue &jv,
        std::string_view src )
{
    if( jv.test_string() ) {
        std::string context = string_format( "%s (%s)", src, jv.get_root_source_path() );
        pending_eoc_refs.emplace_back( jv.get_string(), std::move( context ) );
        return effect_on_condition_id( jv.get_string() );
    } else if( jv.test_object() ) {
        effect_on_condition inline_eoc;
        inline_eoc.load( jv.get_object(), src );
        mod_tracker::assign_src( inline_eoc, src );
        effect_on_condition_factory.insert( inline_eoc );
        invalidate_eocs_by_trigger();
        return inline_eoc.id;
    } else {
        jv.throw_error( "effect_on_condition needs to be either a string or an effect_on_condition object." );
    }
}

static time_duration next_recurrence( const effect_on_condition_id &eoc, dialogue &d )
{
    return eoc->recurrence.evaluate( d );
}

void effect_on_conditions::load_new_character( Character &you )
{
    bool is_avatar = you.is_avatar();

    // npcs do not have scenario, so check for that
    const scenario *scen = get_scenario();
    if( scen ) {
        you.queue_effects( scen->eoc() );
    }

    const profession *prof = you.get_profession();
    if( prof ) {
        you.queue_effects( prof->get_eocs() );
    }

    for( const effect_on_condition &eoc : get_all() ) {
        if( eoc.type == eoc_type::RECURRING && ( ( is_avatar && eoc.global ) || !eoc.global ) ) {
            dialogue d( get_talker_for( you ), nullptr );
            queue_effect_on_condition( next_recurrence( eoc.id, d ), eoc.id, you, {} );
        }
    }

    process_effect_on_conditions( you );
}

static void process_new_eocs( queued_eocs &eoc_queue,
                              std::vector<effect_on_condition_id> &eoc_vector,
                              std::map<effect_on_condition_id, bool> &new_eocs, bool global_queue )
{
    queued_eocs temp_queued_eocs;
    while( !eoc_queue.empty() ) {
        // An EOC that is no longer loaded (its mod was removed) is dropped.  Check if EoC is moved
        // from global to local, or vice versa.  One queued for a u kept by run_eocs' keep_talkers is
        // on the game's queue either way.
        if( eoc_queue.top().eoc.is_valid() && ( global_queue == eoc_queue.top().eoc->global ||
                                                ( global_queue && !eoc_queue.top().alpha.empty() ) ) ) {
            if( !eoc_queue.top().cancelled ) {
                temp_queued_eocs.push( eoc_queue.top() );
            }
            new_eocs[eoc_queue.top().eoc] = false;
        }
        eoc_queue.pop();
    }
    eoc_queue = std::move( temp_queued_eocs );
    for( auto eoc = eoc_vector.begin();
         eoc != eoc_vector.end(); ) {
        // Check if EoC is moved from global to local, or vice versa
        if( !eoc->is_valid() || global_queue != ( *eoc )->global ) {
            eoc = eoc_vector.erase( eoc );
        } else {
            new_eocs[*eoc] = false;
            eoc++;
        }
    }
}

void effect_on_conditions::load_existing_character( Character &you )
{
    bool is_avatar = you.is_avatar();
    std::map<effect_on_condition_id, bool> new_eocs;
    for( const effect_on_condition &eoc : get_all() ) {
        if( eoc.type == eoc_type::RECURRING && ( is_avatar || !eoc.global ) ) {
            new_eocs[eoc.id] = true;
        }
    }
    process_new_eocs( you.queued_effect_on_conditions, you.inactive_effect_on_condition_vector,
                      new_eocs, false );
    if( is_avatar ) {
        process_new_eocs( g->queued_global_effect_on_conditions,
                          g->inactive_global_effect_on_condition_vector, new_eocs, true );
    }

    for( const std::pair<const effect_on_condition_id, bool> &eoc_pair : new_eocs ) {
        if( eoc_pair.second ) {
            dialogue d( get_talker_for( you ), nullptr );
            queue_effect_on_condition( next_recurrence( eoc_pair.first, d ),
                                       eoc_pair.first, you, {} );
        }
    }
}

void effect_on_conditions::queue_effect_on_condition( time_duration duration,
        effect_on_condition_id eoc, Character &you,
        global_variables::impl_t const &context )
{
    queued_eoc new_eoc = queued_eoc{ eoc, calendar::turn + duration, context };
    if( eoc->global ) {
        g->queued_global_effect_on_conditions.push( new_eoc );
    } else if( eoc->type == eoc_type::ACTIVATION || eoc->type == eoc_type::RECURRING ) {
        you.queued_effect_on_conditions.push( new_eoc );
    } else {
        debugmsg( "Invalid effect_on_condition and/or target.  EOC: %s", eoc->id.c_str() );
    }
}

void effect_on_conditions::queue_effect_on_condition( time_duration duration,
        effect_on_condition_id eoc, Character &you, global_variables::impl_t const &context,
        const std::string &alpha, const std::string &beta )
{
    queued_eoc new_eoc = queued_eoc{ eoc, calendar::turn + duration, context };
    new_eoc.alpha = alpha;
    new_eoc.beta = beta;
    if( eoc->global || !alpha.empty() ) {
        g->queued_global_effect_on_conditions.push( new_eoc );
    } else if( eoc->type == eoc_type::ACTIVATION || eoc->type == eoc_type::RECURRING ) {
        you.queued_effect_on_conditions.push( new_eoc );
    } else {
        debugmsg( "Invalid effect_on_condition and/or target.  EOC: %s", eoc->id.c_str() );
    }
}

// The character that has @p id now: control_npc moves characters' data between bodies.
static Character *character_with_id( const character_id &id )
{
    avatar &u = get_avatar();
    if( u.getID() == id ) {
        return &u;
    }
    return g->find_npc( id );
}

// The character whose queue holds @p it now, looking first at the one that has @p id.
static Character *character_holding( const queued_eocs::storage_iter &it, const character_id &id )
{
    std::vector<Character *> candidates;
    if( Character *by_id = character_with_id( id ) ) {
        candidates.push_back( by_id );
    }
    candidates.push_back( &get_avatar() );
    for( npc &guy : g->all_npcs() ) {
        candidates.push_back( &guy );
    }
    for( Character *candidate : candidates ) {
        const std::list<queued_eoc> &list = candidate->queued_effect_on_conditions.list;
        for( const queued_eoc &node : list ) {
            if( &node == &*it ) {
                return candidate;
            }
        }
    }
    return nullptr;
}

// @p owner is the character whose queue this is, if any.
static void process_eocs( queued_eocs &eoc_queue, std::vector<effect_on_condition_id> &eoc_vector,
                          dialogue &d, const Character *owner )
{
    static int reentrancy_depth = 0;
    ++reentrancy_depth;

    // Have to use a typedef because astyle does not settle on the spacing for the & when inline.
    using queue_t = std::vector<queued_eocs::storage_iter> &;
    queue_t eocs_to_queue = []() -> queue_t {
        static std::list<std::vector<queued_eocs::storage_iter>> cached_queues;
        if( reentrancy_depth < 0 )
        {
            debugmsg( "How can we unrecurse more than we recurse?" );
        }
        while( cached_queues.size() < static_cast<size_t>( reentrancy_depth ) )
        {
            cached_queues.emplace_back();
        }
        auto it = cached_queues.begin();
        std::advance( it, reentrancy_depth - 1 );
        return *it;
    }();

    on_out_of_scope cleanup{ [&] {
            --reentrancy_depth;
            eocs_to_queue.clear();
        } };

    // Take what is due before running any of it: an EOC queued during the pass with no delay,
    // such as one that queues itself again, waits for the next pass instead of making this one
    // endless.
    std::vector<queued_eocs::storage_iter> due;
    while( !eoc_queue.empty() &&
           eoc_queue.top().time <= calendar::turn ) {
        due.push_back( eoc_queue.queue.top() );
        eoc_queue.queue.pop();
    }

    // An EOC that takes control of an NPC (control_npc) swaps the owner's data, this queue and
    // its inactive list included, into another body; they are followed there.
    queued_eocs *queue = &eoc_queue;
    std::vector<effect_on_condition_id> *inactive = &eoc_vector;
    const character_id owner_id = owner != nullptr ? owner->getID() : character_id();
    for( size_t i = 0; i < due.size(); ++i ) {
        const queued_eocs::storage_iter &it = due[i];
        queued_eoc &top = *it;

        if( top.cancelled ) {
            queue->list.erase( it );
            continue;
        }
        // The u and npc run_eocs' keep_talkers kept, found again: without the u it doesn't run;
        // without the npc it runs with none, as without keep_talkers
        std::list<item_location> kept_items;
        std::optional<dialogue> kept_d;
        if( !top.alpha.empty() || !top.beta.empty() ) {
            std::unique_ptr<talker> alpha = top.alpha.empty() ? d.actor( false )->clone() :
                                            find_talker_by_handle( top.alpha, kept_items );
            if( !alpha ) {
                queue->list.erase( it );
                continue;
            }
            kept_d.emplace( std::move( alpha ), find_talker_by_handle( top.beta, kept_items ),
                            d.get_conditionals(), d.get_context() );
        }

        dialogue nested_d{ kept_d ? *kept_d : d };
        for( const auto &val : top.context ) {
            nested_d.set_value( val.first, val.second );
        }
        bool activated = top.eoc->activate( nested_d );
        const bool moved = owner != nullptr && owner->getID() != owner_id;
        // After a move, the entry is the other body's, and so is its next recurrence
        std::optional<dialogue> moved_d;
        std::optional<dialogue> moved_nested_d;
        if( moved ) {
            Character *moved_to = character_holding( it, owner_id );
            if( moved_to == nullptr ) {
                // The entries still due were taken off a priority queue that is now nowhere to
                // be found, so there is no putting them back
                debugmsg( "The queued EOCs of character %d moved to a body that can't be found while "
                          "EOC %s ran; %d of them due this turn are lost.", owner_id.get_value(),
                          top.eoc.str(), static_cast<int>( due.size() - i ) );
                return;
            }
            queue = &moved_to->queued_effect_on_conditions;
            inactive = &moved_to->inactive_effect_on_condition_vector;
            moved_d.emplace( get_talker_for( *moved_to ), nullptr, d.get_conditionals(), d.get_context() );
            moved_nested_d.emplace( *moved_d );
            for( const auto &val : top.context ) {
                moved_nested_d->set_value( val.first, val.second );
            }
        }
        dialogue &owner_d = moved_d ? *moved_d : d;
        dialogue &entry_d = moved_nested_d ? *moved_nested_d : nested_d;
        if( top.eoc->type == eoc_type::RECURRING ) {
            if( activated ) { // It worked so add it back
                it->time = calendar::turn + next_recurrence( top.eoc, owner_d );
                eocs_to_queue.emplace_back( it );
            } else {
                if( !top.eoc->check_deactivate(
                        entry_d ) ) { // It failed but shouldn't be deactivated so add it back
                    it->time = calendar::turn + next_recurrence( top.eoc, owner_d );
                    eocs_to_queue.emplace_back( it );
                } else { // It failed and should be deactivated for now
                    inactive->push_back( top.eoc );
                    queue->list.erase( it );
                }
            }
        } else {
            queue->list.erase( it );
        }
        if( moved ) {
            // The rest of the pass belongs to the old body, which runs it on its own turn
            eocs_to_queue.insert( eocs_to_queue.end(), due.begin() + i + 1, due.end() );
            break;
        }
    }
    for( queued_eocs::storage_iter &q_eoc : eocs_to_queue ) {
        queue->queue.emplace( q_eoc );
    }
}

void effect_on_conditions::process_effect_on_conditions( Character &you )
{
    dialogue d( get_talker_for( you ), nullptr );
    process_eocs( you.queued_effect_on_conditions, you.inactive_effect_on_condition_vector, d, &you );
    //only handle global eocs on the avatars turn
    if( you.is_avatar() ) {
        process_eocs( g->queued_global_effect_on_conditions, g->inactive_global_effect_on_condition_vector,
                      d, nullptr );
    }
}

static void process_reactivation( std::vector<effect_on_condition_id>
                                  &inactive_effect_on_condition_vector,
                                  queued_eocs &queued_effect_on_conditions, dialogue &d )
{
    std::vector<effect_on_condition_id> ids_to_reactivate;
    for( const effect_on_condition_id &eoc : inactive_effect_on_condition_vector ) {
        if( !eoc->check_deactivate( d ) ) {
            ids_to_reactivate.push_back( eoc );
        }
    }
    for( const effect_on_condition_id &eoc : ids_to_reactivate ) {
        queued_effect_on_conditions.push( queued_eoc{ eoc, calendar::turn + next_recurrence( eoc, d ), d.get_context() } );
        inactive_effect_on_condition_vector.erase( std::remove(
                    inactive_effect_on_condition_vector.begin(), inactive_effect_on_condition_vector.end(),
                    eoc ), inactive_effect_on_condition_vector.end() );
    }
}

void effect_on_conditions::process_reactivate( Character &you )
{
    dialogue d( get_talker_for( you ), nullptr );
    process_reactivation( you.inactive_effect_on_condition_vector, you.queued_effect_on_conditions, d );
}

void effect_on_conditions::process_reactivate()
{
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    process_reactivation( g->inactive_global_effect_on_condition_vector,
                          g->queued_global_effect_on_conditions, d );
}

namespace
{
// The EOCs running inside one another right now, outermost first, however each was reached
// (run_eocs, true_eocs, events, spells...); a dialogue's callstack text doesn't follow all of those.
std::vector<effect_on_condition_id> running_eocs;
// Set when a chain of EOCs went too deep: how many EOCs were running outside the chain.  Until it
// has returned to them, every EOC in the chain stops at once.
std::optional<size_t> stopping_above;
// Deeper than EOCs need to nest: content that repeats something should loop with run_eocs'
// "iterations" or "condition" instead of running itself again.  Each level takes about 2.5 KB of
// stack in a release build (more in a debug build), and a Windows program's main thread gets 1 MB.
constexpr int max_eoc_depth = 250;

void leave_eoc()
{
    running_eocs.pop_back();
    // Once the stopped chain has returned, EOCs run again
    if( stopping_above && running_eocs.size() <= *stopping_above ) {
        stopping_above.reset();
    }
}
} // namespace

bool effect_on_condition::activate( dialogue &d, bool require_callstack_check,
                                    const std::function<void( const dialogue & )> &on_done ) const
{
    if( stopping_above ) {
        return false;
    }
    const bool tracing = debug_menu::debug_capture::is_eoc_tracing();
    std::chrono::steady_clock::time_point trace_start;
    if( tracing ) {
        trace_start = std::chrono::steady_clock::now();
    }
    bool retval = false;
    if( require_callstack_check ) {
        d.amend_callstack( "EOC: " + id.str() );
    }
    if( running_eocs.size() >= static_cast<size_t>( max_eoc_depth ) ) {
        // The endless chain starts where this EOC first ran; whatever ran that goes on
        stopping_above = std::find( running_eocs.begin(), running_eocs.end(), id ) - running_eocs.begin();
        debugmsg( "EOC %s is nested %d deep, probably in an endless loop of EOCs running each other.  "
                  "That chain of EOCs was stopped.", id.str(), max_eoc_depth );
        return false;
    }
    running_eocs.push_back( id );
    on_out_of_scope leave( leave_eoc );
    // each version needs a copy of the dialogue to pass down
    dialogue d_eoc( d );
    if( !has_condition || condition( d_eoc ) ) {
        true_effect.apply( d_eoc );
        retval = true;
    } else if( has_false_effect ) {
        false_effect.apply( d_eoc );
    }
    if( on_done ) {
        on_done( d_eoc );
    }
    // This works because if global is true then this is recurring and thus should only ever be passed containing the player
    // Thus we just need to run the npcs.
    if( global && run_for_npcs ) {
        for( npc &guy : g->all_npcs() ) {
            dialogue d_npc( get_talker_for( guy ), nullptr, d.get_conditionals(), d.get_context() );
            if( !has_condition || condition( d_npc ) ) {
                true_effect.apply( d_npc );
            } else if( has_false_effect ) {
                false_effect.apply( d_npc );
            }
        }
    }
    if( tracing ) {
        // Read callstack depth at trace time regardless of require_callstack_check
        // (recursive EOC fires pass false but we still want depth > 0).
        const int trace_depth = static_cast<int>( d.get_callstack().size() );
        debug_menu::debug_capture::instance().push_eoc_trace(
            id.str(), retval,
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - trace_start ),
            trace_depth );
    }
    return retval;
}

bool effect_on_condition::activate_activation_only( dialogue &d, const std::string &text1,
        const std::string &text2, const std::string &text3, bool require_callstack_check ) const
{
    if( type == eoc_type::ACTIVATION ) {
        return activate( d, require_callstack_check );
    }
    debugmsg(
        "Must use an activation eoc for %s%s%s%s.  Otherwise, create a non-recurring effect_on_condition for this %s%swith its condition and effects, then have a recurring one queue it.",
        text1,
        text2.empty() ? "" :
        ".  If you don't want the effect_on_condition to happen on its own (without the ",
        text2,
        text2.empty() ? "" : "), remove the recurrence min and max.",
        text3,
        text3.empty() ? "" : " " );
    return false;
}

bool effect_on_condition::check_deactivate( const_dialogue const &d ) const
{
    if( !has_deactivate_condition || has_false_effect ) {
        return false;
    }
    return deactivate_condition( d );
}

bool effect_on_condition::test_condition( const_dialogue const &d ) const
{
    return !has_condition || condition( d );
}

void effect_on_condition::apply_true_effects( dialogue &d )  const
{
    true_effect.apply( d );
}

void effect_on_conditions::clear( Character &you )
{
    while( !you.queued_effect_on_conditions.empty() ) {
        you.queued_effect_on_conditions.pop();
    }
    you.inactive_effect_on_condition_vector.clear();
    while( !g->queued_global_effect_on_conditions.empty() ) {
        g->queued_global_effect_on_conditions.pop();
    }
    g->inactive_global_effect_on_condition_vector.clear();
}

void effect_on_conditions::write_eocs_to_file( Character &you )
{
    write_to_file( "eocs.output", [&you]( std::ostream & testfile ) {
        testfile << "Character Name: " + you.get_name() << std::endl;
        testfile << "id;timepoint;recurring" << std::endl;

        testfile << "queued eocs:" << std::endl;
        std::vector<queued_eoc> temp_queue;
        while( !you.queued_effect_on_conditions.empty() ) {
            temp_queue.push_back( you.queued_effect_on_conditions.top() );
            you.queued_effect_on_conditions.pop();
        }

        for( const queued_eoc &queue_entry : temp_queue ) {
            time_duration temp = queue_entry.time - calendar::turn;
            testfile << queue_entry.eoc.c_str() << ";" << to_string( temp ) << std::endl;
        }

        for( const queued_eoc &queued : temp_queue ) {
            you.queued_effect_on_conditions.push( queued );
        }

        testfile << "inactive eocs:" << std::endl;
        for( const effect_on_condition_id &eoc : you.inactive_effect_on_condition_vector ) {
            testfile << eoc.c_str() << std::endl;
        }

    }, "eocs test file" );
}

void effect_on_conditions::write_global_eocs_to_file( )
{
    write_to_file( "eocs.output", [&]( std::ostream & testfile ) {
        testfile << "global" << std::endl;
        testfile << "id;timepoint;recurring" << std::endl;

        testfile << "queued eocs:" << std::endl;
        std::vector<queued_eoc> temp_queue;
        while( !g->queued_global_effect_on_conditions.empty() ) {
            temp_queue.push_back( g->queued_global_effect_on_conditions.top() );
            g->queued_global_effect_on_conditions.pop();
        }

        for( const queued_eoc &queue_entry : temp_queue ) {
            time_duration temp = queue_entry.time - calendar::turn;
            testfile << queue_entry.eoc.c_str() << ";" << to_string( temp ) << std::endl;
        }

        for( const queued_eoc &queued : temp_queue ) {
            g->queued_global_effect_on_conditions.push( queued );
        }

        testfile << "inactive eocs:" << std::endl;
        for( const effect_on_condition_id &eoc : g->inactive_global_effect_on_condition_vector ) {
            testfile << eoc.c_str() << std::endl;
        }

    }, "eocs test file" );
}

// The avatar's killer, if it is still in the game.
static Creature *loaded_killer_of( avatar &player_character )
{
    Creature *klr = player_character.get_killer();
    return !klr ? klr : g->get_creature_if( [klr]( const Creature & c ) {
        return klr == &c;
    } );
}

void effect_on_conditions::prevent_death()
{
    avatar &player_character = get_avatar();
    // EOC-J10: the killer is the beta, as for AVATAR_DEATH
    Creature *klr = loaded_killer_of( player_character );
    dialogue d( get_talker_for( player_character ), klr == nullptr ? nullptr : get_talker_for( klr ) );
    for( const effect_on_condition &eoc : get_all() ) {
        if( eoc.type == eoc_type::PREVENT_DEATH ) {
            eoc.activate( d );
        }
        if( !player_character.is_dead_state() ) {
            player_character.clear_killer();
            break;
        }
    }
}

void effect_on_conditions::avatar_death()
{
    avatar &player_character = get_avatar();
    // Make sure the creature still exists in game
    Creature *klr = loaded_killer_of( player_character );
    dialogue d( get_talker_for( get_avatar() ), klr == nullptr ? nullptr : get_talker_for( klr ) );
    for( const effect_on_condition &eoc : get_all() ) {
        if( eoc.type == eoc_type::AVATAR_DEATH ) {
            eoc.activate( d );
        }
        // as documented: until the player isn't dead, which the death switch can make so
        if( !player_character.is_dead_state() ) {
            break;
        }
    }
}

bool effect_on_conditions::prevent_monster_death( monster &mon, Creature *killer )
{
    // a copy: the cache is rebuilt if EOCs are loaded
    const std::vector<effect_on_condition_id> eocs = get_eocs_by_trigger().prevent_monster_death;
    if( eocs.empty() ) {
        return false;
    }
    // only a u_prevent_death from these EOCs counts
    mon.death_prevented = false;
    dialogue d( get_talker_for( static_cast<Creature &>( mon ) ),
                killer == nullptr ? nullptr : get_talker_for( killer ) );
    for( const effect_on_condition_id &eoc : eocs ) {
        eoc->activate( d );
        // as for PREVENT_DEATH: until one of them prevented it (or one killed it already)
        if( mon.death_prevented || mon.death_finalized() ) {
            break;
        }
    }
    const bool prevented = mon.death_prevented && !mon.death_finalized();
    mon.death_prevented = false;
    return prevented;
}

const std::vector<effect_on_condition_id> &effect_on_conditions::custom_event_eocs(
    const std::string &name )
{
    static const std::vector<effect_on_condition_id> none;
    const std::map<std::string, std::vector<effect_on_condition_id>> &events =
                get_eocs_by_trigger().custom_events;
    const auto it = events.find( name );
    return it == events.end() ? none : it->second;
}

bool effect_on_conditions::reactivate( const effect_on_condition_id &eoc, Character &you,
                                       dialogue &d )
{
    std::vector<effect_on_condition_id> &inactive = eoc->global ?
            g->inactive_global_effect_on_condition_vector : you.inactive_effect_on_condition_vector;
    queued_eocs &queue = eoc->global ? g->queued_global_effect_on_conditions :
                         you.queued_effect_on_conditions;
    const auto it = std::find( inactive.begin(), inactive.end(), eoc );
    if( it == inactive.end() ) {
        return false;
    }
    inactive.erase( it );
    queue.push( queued_eoc{ eoc, calendar::turn + next_recurrence( eoc, d ), {} } );
    return true;
}

void effect_on_condition::finalize()
{
}

void effect_on_conditions::finalize_all()
{
    effect_on_condition_factory.finalize();
    invalidate_eocs_by_trigger();
}

void effect_on_condition::check() const
{
    // EOC-J04: deactivate_condition is only tested when a RECURRING EOC without a false_effect
    // fails its condition
    if( has_deactivate_condition && type != eoc_type::RECURRING ) {
        debugmsg( "effect_on_condition %s has a deactivate_condition, but it isn't RECURRING, so "
                  "the deactivate_condition is never tested.  Remove it.", id.str() );
    }
    if( has_deactivate_condition && has_false_effect ) {
        debugmsg( "effect_on_condition %s has both a deactivate_condition and a false_effect.  An "
                  "EOC with a false_effect is never deactivated, so the deactivate_condition is "
                  "never tested.", id.str() );
    }
}

const std::vector<effect_on_condition> &effect_on_conditions::get_all()
{
    return effect_on_condition_factory.get_all();
}

void effect_on_conditions::reset()
{
    effect_on_condition_factory.reset();
    pending_eoc_refs.clear();
    pending_load_checks.clear();
    invalidate_eocs_by_trigger();
    eoc_migrations.clear();
}

void effect_on_conditions::load( const JsonObject &jo, const std::string &src )
{
    effect_on_condition_factory.load( jo, src );
    invalidate_eocs_by_trigger();
}

void eoc_events::clear()
{
    has_cached = false;
    event_EOCs.clear();
}

void eoc_events::notify( const cata::event &e )
{
    notify( e, nullptr, nullptr );
}

void eoc_events::notify( const cata::event &e, std::unique_ptr<talker> alpha,
                         std::unique_ptr<talker> beta )
{
    if( !has_cached ) {

        // initialize all events to an empty vector
        for( event_type et = static_cast<event_type>( 0 ); et < event_type::num_event_types;
             et = static_cast<event_type>( static_cast<size_t>( et ) + 1 ) ) {

            event_EOCs[et] = std::vector<effect_on_condition>();
        }

        //create a cache for the specific types of EOC's so they aren't constantly all itterated through
        for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
            // one on a mod-defined event has no required_event (EOC-J06)
            if( eoc.type == eoc_type::EVENT && eoc.custom_event.empty() ) {
                event_EOCs[eoc.required_event].emplace_back( eoc );
            }
        }

        has_cached = true;
    }

    const std::vector<effect_on_condition> &eocs = event_EOCs[e.type()];
    if( eocs.empty() ) {
        return;
    }
    if( !alpha ) {
        // The alpha talker is the character the event is about.  An event that names no
        // character runs its EOCs on the avatar.  One that names a character who can't be
        // found (a spell cast with no caster, an NPC that isn't loaded) runs none, rather
        // than running them on the avatar.
        // TODO: refactor event_spec to take consistent inputs
        const std::vector<std::string> potential_alphas = { "avatar_id", "character", "attacker", "killer", "npc" };
        bool names_a_character = false;
        Creature *subject = nullptr;
        for( const std::string &potential_key : potential_alphas ) {
            cata_variant cv = e.get_variant_or_void( potential_key );
            if( cv != cata_variant() ) {
                names_a_character = true;
                character_id potential_id = cv.get<cata_variant_type::character_id>();
                if( potential_id.is_valid() ) {
                    avatar &u = get_avatar();
                    subject = potential_id == u.getID() ? static_cast<Creature *>( &u ) :
                              g->find_npc( potential_id );
                    // if we find a successful entry exit early
                    break;
                }
            }
        }
        if( subject ) {
            alpha = get_talker_for( subject );
        } else if( !names_a_character ) {
            alpha = get_talker_for( get_avatar() );
        } else {
            return;
        }
    }

    for( const effect_on_condition &eoc : eocs ) {
        dialogue d;
        global_variables::impl_t context;
        for( const auto &val : e.data() ) {
            context[val.first] = diag_value{ val.second };
        }

        d = dialogue( alpha->clone(), beta ? beta->clone() : nullptr, {}, context );

        eoc.activate( d );
    }
}

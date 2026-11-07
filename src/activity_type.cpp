#include "activity_type.h"

#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "activity_actor.h"
#include "activity_handlers.h"
#include "debug.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "sounds.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"

// activity_type functions
namespace
{
generic_factory<activity_type> activity_type_factory( "activity_type" );
} // namespace

/** @relates string_id */
template<>
const activity_type &string_id<activity_type>::obj() const
{
    return activity_type_factory.obj( *this );
}

template<>
bool string_id<activity_type>::is_valid() const
{
    return activity_type_factory.is_valid( *this );
}

namespace io
{
template<>
std::string enum_to_string<based_on_type>( based_on_type data )
{
    switch( data ) {
        // *INDENT-OFF*
        case based_on_type::TIME: return "time";
        case based_on_type::SPEED: return "speed";
        case based_on_type::NEITHER: return "neither";
        // *INDENT-ON*
        default:
            cata_fatal( "Invalid based_on_type in enum_to_string" );
    }
}

template<>
std::string enum_to_string<distraction_type>( distraction_type data )
{
    switch( data ) {
        // *INDENT-OFF*
        case distraction_type::noise: return "noise";
        case distraction_type::pain: return "pain";
        case distraction_type::attacked: return "attacked";
        case distraction_type::hostile_spotted_far: return "hostile_spotted_far";
        case distraction_type::hostile_spotted_near: return "hostile_spotted_near";
        case distraction_type::talked_to: return "talked_to";
        case distraction_type::asthma: return "asthma";
        case distraction_type::motion_alarm: return "motion_alarm";
        case distraction_type::weather_change: return "weather_change";
        case distraction_type::portal_storm_popup: return "portal_storm_popup";
        case distraction_type::eoc: return "eoc";
        case distraction_type::dangerous_field: return "dangerous_field";
        case distraction_type::hunger: return "hunger";
        case distraction_type::thirst: return "thirst";
        case distraction_type::temperature: return "temperature";
        case distraction_type::mutation: return "mutation";
        case distraction_type::oxygen: return "oxygen";
        case distraction_type::withdrawal: return "withdrawal";
        case distraction_type::craft_step_complete: return "craft_step_complete";
        // *INDENT-ON*
        default:
            cata_fatal( "Invalid distraction_type in enum_to_string" );
    }
}
}  // namespace io

void activity_type::load( const JsonObject &jo, std::string_view src )
{
    optional( jo, was_loaded, "rooted", rooted_, false );
    mandatory( jo, was_loaded, "verb", verb_ );
    optional( jo, was_loaded, "interruptable", interruptable_, true );
    optional( jo, was_loaded, "interruptable_with_kb", interruptable_with_kb_, true );
    optional( jo, was_loaded, "can_resume", can_resume_, true );
    optional( jo, was_loaded, "multi_activity", multi_activity_, false );
    optional( jo, was_loaded, "fetch_items_to_zone", fetch_items_to_zone_, true );
    optional( jo, was_loaded, "refuel_fires", refuel_fires, false );
    optional( jo, was_loaded, "auto_needs", auto_needs, false );
    optional( jo, was_loaded, "mute_npc_completion", mute_npc_completion, false );
    // An EOC id or an inline EOC object.
    if( jo.has_member( "start_eoc" ) ) {
        start_EOC = effect_on_conditions::load_inline_eoc( jo.get_member( "start_eoc" ), src );
    }
    if( jo.has_member( "completion_eoc" ) ) {
        completion_EOC = effect_on_conditions::load_inline_eoc( jo.get_member( "completion_eoc" ), src );
    }
    if( jo.has_member( "cancel_eoc" ) ) {
        cancel_EOC = effect_on_conditions::load_inline_eoc( jo.get_member( "cancel_eoc" ), src );
    }
    if( jo.has_member( "do_turn_eoc" ) ) {
        do_turn_EOC = effect_on_conditions::load_inline_eoc( jo.get_member( "do_turn_eoc" ), src );
    }
    optional( jo, was_loaded, "ignored_distractions", default_ignored_distractions_ );
    optional( jo, was_loaded, "based_on", based_on_ );
    mandatory( jo, was_loaded, "activity_level", activity_level, activity_level_reader{} );
}

void activity_type::load_all( const JsonObject &jo, const std::string &src )
{
    activity_type_factory.load( jo, src );
}

void activity_type::finalize_all()
{
    activity_type_factory.finalize();
}

// activity_migration entries: the type a saved activity becomes; none: the activity is dropped
static std::map<activity_id, std::optional<activity_id>> activity_migrations;

void activity_type::load_migration( const JsonObject &jo )
{
    const activity_id from( jo.get_string( "from" ) );
    std::optional<activity_id> to;
    if( jo.has_member( "to" ) ) {
        to = activity_id( jo.get_string( "to" ) );
    }
    // A later migration of the same id replaces the earlier one
    activity_migrations.insert_or_assign( from, to );
}

std::optional<activity_id> activity_type::migrate_saved( const activity_id &id )
{
    const auto it = activity_migrations.find( id );
    if( it == activity_migrations.end() ) {
        return id;
    }
    return it->second;
}

const std::vector<activity_type> &activity_type::get_all()
{
    return activity_type_factory.get_all();
}

void activity_type::check_consistency()
{
    for( const activity_type &act : activity_type_factory.get_all() ) {
        if( act.verb_.empty() ) {
            debugmsg( "%s doesn't have a verb", act.id.c_str() );
        }
        for( const effect_on_condition_id &eoc : {
                 act.start_EOC, act.completion_EOC, act.cancel_EOC, act.do_turn_EOC
             } ) {
            if( !eoc.is_null() && !eoc.is_valid() ) {
                debugmsg( "%s uses unknown effect_on_condition %s", act.id.c_str(), eoc.c_str() );
            }
        }
        const bool has_actor = activity_actors::deserialize_functions.find( act.id ) !=
                               activity_actors::deserialize_functions.end();
        const bool has_turn_func = activity_handlers::do_turn_functions.find( act.id ) !=
                                   activity_handlers::do_turn_functions.end();

        if( act.based_on_ == based_on_type::NEITHER && !( has_turn_func || has_actor ) ) {
            debugmsg( "%s needs a do_turn function or activity actor if it's not based on time or speed.",
                      act.id.c_str() );
        }
    }

    for( const auto &pair : activity_handlers::do_turn_functions ) {
        if( !pair.first.is_valid() ) {
            debugmsg( "The do_turn function %s doesn't correspond to a valid activity_type.",
                      pair.first.c_str() );
        }
    }

    for( const auto &pair : activity_handlers::finish_functions ) {
        if( !pair.first.is_valid() ) {
            debugmsg( "The finish_function %s doesn't correspond to a valid activity_type",
                      pair.first.c_str() );
        }
    }

    for( const auto &[from, to] : activity_migrations ) {
        if( to && !to->is_valid() ) {
            debugmsg( "activity_migration from %s names an activity that does not exist: %s",
                      from.str(), to->str() );
        }
    }
}

void activity_type::call_do_turn( player_activity *act, Character *you ) const
{
    const auto &pair = activity_handlers::do_turn_functions.find( id );
    if( pair != activity_handlers::do_turn_functions.end() ) {
        pair->second( act, you );
    }
}

bool activity_type::call_finish( player_activity *act, Character *you ) const
{
    const auto &pair = activity_handlers::finish_functions.find( id );
    if( pair != activity_handlers::finish_functions.end() ) {
        pair->second( act, you );
        // kill activity sounds at finish
        sfx::end_activity_sounds();
        return true;
    }

    return false;
}

void activity_type::reset()
{
    activity_type_factory.reset();
    activity_migrations.clear();
}

std::string activity_type::stop_phrase() const
{
    return string_format( _( "Stop %s?" ), verb_ );
}

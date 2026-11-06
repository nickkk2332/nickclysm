#include "message_log.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "flexbuffer_json.h"
#include "game.h"
#include "json.h"
#include "messages.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"

game_message::game_message( std::string &&msg, game_message_type t ) :
    message( std::move( msg ) ), timestamp_in_turns( calendar::turn ),
    timestamp_in_user_actions( g ? g->get_user_action_counter() : 0 ), type( t )
{
}

const time_point &game_message::turn() const
{
    return timestamp_in_turns;
}

std::string game_message::get_with_count() const
{
    if( count <= 1 ) {
        return message;
    }
    //~ Message %s on the message log was repeated %d times, e.g. "You hear a whack! x 12"
    return string_format( _( "%s x %d" ), message, count );
}

bool game_message::is_in_cooldown() const
{
    return cooldown_hidden;
}

bool game_message::is_new( const time_point &current ) const
{
    return turn() >= current;
}

bool game_message::is_recent( const time_point &current ) const
{
    return turn() + 5_turns >= current;
}

nc_color game_message::get_color( const time_point &current ) const
{
    return is_new( current ) ? msgtype_to_color( type, false ) :
           is_recent( current ) ? msgtype_to_color( type, true ) : c_dark_gray;
}

void game_message::deserialize( const JsonObject &obj )
{
    obj.read( "turn", timestamp_in_turns );
    message = obj.get_string( "message" );
    count = obj.get_int( "count" );
    type = static_cast<game_message_type>( obj.get_int( "type" ) );
    owner = obj.get_string( "owner", "" );
    group = obj.get_string( "group", "" );
    segment = obj.get_string( "segment", "" );
    separator = obj.get_string( "separator", " " );
    if( owner.empty() || group.empty() || segment.empty() ) {
        owner.clear();
        group.clear();
        segment.clear();
    }
}

void game_message::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "turn", timestamp_in_turns );
    jsout.member( "message", message );
    jsout.member( "count", count );
    jsout.member( "type", static_cast<int>( type ) );
    if( !group.empty() ) {
        jsout.member( "owner", owner );
        jsout.member( "group", group );
        jsout.member( "segment", segment );
        jsout.member( "separator", separator );
    }
    jsout.end_object();
}

std::string message_log::new_id()
{
    if( origin.empty() ) {
        // Identity is independent of gameplay RNG: observing narration cannot alter combat rolls.
        std::random_device entropy;
        std::ostringstream token;
        token << std::hex << std::setfill( '0' );
        for( int n = 0; n < 4; ++n ) {
            token << std::setw( 8 ) << static_cast<uint32_t>( entropy() );
        }
        origin = token.str();
    }
    if( next_id == std::numeric_limits<uint64_t>::max() ) {
        throw JsonError( "Message identity space exhausted" );
    }
    return origin + ":" + std::to_string( next_id++ );
}

Messages::owned_message_result message_log::add_owned( const std::string &owner,
        const std::string &anchor,
        std::string text, const std::string &separator, size_t limit )
{
    if( text.empty() || owner.empty() || !active ) {
        return {};
    }
    limit = std::max<size_t>( limit, 1 );
    while( messages.size() >= limit ) {
        messages.pop_front();
    }
    const bool found = !anchor.empty() && std::any_of( messages.begin(), messages.end(),
    [&]( const game_message & m ) {
        return m.owner == owner && m.group == anchor;
    } );
    game_message entry( std::move( text ), m_neutral );
    entry.owner = owner;
    entry.segment = new_id();
    entry.group = found ? anchor : entry.segment;
    entry.separator = separator;
    const Messages::owned_message_result result{ entry.group, entry.segment, found };
    messages.push_back( std::move( entry ) );
    return result;
}

std::vector<game_message> message_log::compact_messages( size_t count, bool count_hidden ) const
{
    std::vector<game_message> result;
    result.reserve( std::min( count, messages.size() ) );
    if( count == 0 ) {
        return result;
    }
    const auto joins = []( const game_message & older, const game_message & newer ) {
        return !older.group.empty() && older.group == newer.group && older.owner == newer.owner &&
               older.type == newer.type && !older.cooldown_hidden && !newer.cooldown_hidden;
    };
    size_t begin = messages.size();
    size_t groups = 0;
    while( begin > 0 ) {
        const size_t n = begin - 1;
        // A hidden message never joins another, so it is always a group of its own.
        const bool counted = count_hidden || !messages[n].cooldown_hidden;
        if( counted && ( begin == messages.size() || !joins( messages[n], messages[n + 1] ) ) ) {
            if( groups == count ) {
                break;
            }
            ++groups;
        }
        --begin;
    }
    for( size_t n = begin; n < messages.size(); ++n ) {
        const game_message &entry = messages[n];
        if( !result.empty() && joins( result.back(), entry ) ) {
            game_message &last = result.back();
            last.message += entry.separator + entry.message;
            last.timestamp_in_turns = entry.timestamp_in_turns;
            last.timestamp_in_user_actions = entry.timestamp_in_user_actions;
        } else {
            result.push_back( entry );
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> message_log::recent( size_t count,
        bool compact ) const
{
    const auto collect = [count]( const auto & entries ) {
        std::vector<std::pair<std::string, std::string>> result;
        const size_t begin = entries.size() - std::min( count, entries.size() );
        for( size_t n = begin; n < entries.size(); ++n ) {
            result.emplace_back( to_string_time_of_day( entries[n].timestamp_in_turns ),
                                 entries[n].get_with_count() );
        }
        return result;
    };
    return compact ? collect( compact_messages( count ) ) : collect( messages );
}

void message_log::clear()
{
    messages.clear();
    active = true;
}

void message_log::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "messages", messages );
    jsout.member( "curmes", curmes );
    jsout.member( "origin", origin );
    jsout.member( "next_id", std::to_string( next_id ) );
    jsout.end_object();
}

void message_log::deserialize( const JsonObject &obj )
{
    const std::string saved_origin = obj.get_string( "origin", "" );
    uint64_t saved_next = 1;
    try {
        const std::string value = obj.get_string( "next_id", "1" );
        size_t end = 0;
        saved_next = std::stoull( value, &end );
        if( end != value.size() || value.front() == '-' || saved_next == 0 ||
            saved_next == std::numeric_limits<uint64_t>::max() ) {
            obj.throw_error_at( "next_id", "invalid message identity counter" );
        }
    } catch( const std::invalid_argument & ) {
        obj.throw_error_at( "next_id", "invalid message identity counter" );
    } catch( const std::out_of_range & ) {
        obj.throw_error_at( "next_id", "invalid message identity counter" );
    }
    messages.clear();
    curmes = calendar::turn_zero;
    obj.read( "messages", messages );
    obj.read( "curmes", curmes );
    next_id = saved_origin == origin ? std::max( next_id, saved_next ) : saved_next;
    origin = saved_origin;
    for( const game_message &entry : messages ) {
        const std::string prefix = origin + ":";
        if( !origin.empty() && entry.segment.compare( 0, prefix.size(), prefix ) == 0 ) {
            try {
                size_t end = 0;
                const std::string suffix = entry.segment.substr( prefix.size() );
                const uint64_t id = std::stoull( suffix, &end );
                if( end != suffix.size() || id == std::numeric_limits<uint64_t>::max() ) {
                    obj.throw_error( "invalid owned message identity" );
                }
                next_id = std::max( next_id, id + 1 );
            } catch( const std::invalid_argument & ) {
                obj.throw_error( "invalid owned message identity" );
            } catch( const std::out_of_range & ) {
                obj.throw_error( "invalid owned message identity" );
            }
        }
    }
}

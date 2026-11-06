#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "coords_fwd.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "message_log.h"
#include "messages.h"
#include "options.h"

class Creature;
class JsonObject;
class JsonOut;
namespace debugmode
{
enum debug_filter : int;
}  // namespace debugmode
struct game_message_params;

namespace catacurses
{
class window;
}  // namespace catacurses

/**
 * For unit testing we just store all messages so they can be dumped in the
 * event of a test failure.
 */

static message_log messages;

std::vector<std::pair<std::string, std::string>> Messages::recent_messages( size_t count )
{
    return messages.recent( count == 0 ? messages.messages.size() : count );
}
bool Messages::has_debug_filter( debugmode::debug_filter )
{
    return true;
}
void Messages::add_msg( std::string m )
{
    // Like the game's log, a deactivated log (after the player's death) takes nothing new until
    // it is cleared.
    if( !m.empty() && messages.active ) {
        messages.messages.emplace_back( std::move( m ), m_neutral );
    }
}
void Messages::add_msg( const game_message_params &, std::string m )
{
    add_msg( std::move( m ) );
}
void Messages::clear_messages()
{
    messages.clear();
}
void Messages::deactivate()
{
    messages.active = false;
}
size_t Messages::size()
{
    return messages.messages.size();
}
bool Messages::has_undisplayed_messages()
{
    return false;
}
void Messages::display_messages() {}
void Messages::display_messages( const catacurses::window &, int, int, int, int ) {}
void Messages::serialize( JsonOut &json )
{
    json.member( "player_messages" );
    messages.serialize( json );
}
void Messages::deserialize( const JsonObject &json )
{
    if( json.has_member( "player_messages" ) ) {
        messages.deserialize( json.get_object( "player_messages" ) );
    } else {
        messages.clear();
    }
}
void add_msg( std::string m )
{
    Messages::add_msg( std::move( m ) );
}
void add_msg( const game_message_params &, std::string m )
{
    Messages::add_msg( std::move( m ) );
}
void add_msg_if_player_sees( const tripoint_bub_ms &, std::string m )
{
    Messages::add_msg( std::move( m ) );
}
void add_msg_if_player_sees( const Creature &, std::string m )
{
    Messages::add_msg( std::move( m ) );
}
void add_msg_if_player_sees( const tripoint_bub_ms &, const game_message_params &, std::string m )
{
    Messages::add_msg( std::move( m ) );
}
void add_msg_if_player_sees( const Creature &, const game_message_params &, std::string m )
{
    Messages::add_msg( std::move( m ) );
}

Messages::owned_message_result Messages::add_owned( const std::string &owner,
        const std::string &anchor,
        std::string text, const std::string &separator )
{
    return messages.add_owned( owner, anchor, std::move( text ), separator,
                               static_cast<size_t>( get_option<int>( "MESSAGE_LIMIT" ) ) );
}

std::vector<std::pair<std::string, std::string>> Messages::recent_messages_compact( size_t count )
{
    return messages.recent( count, true );
}

#include "dialogue_topic_override.h"

#include <algorithm>
#include <tuple>

#include "condition.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "npctalk.h"
#include "string_formatter.h"

namespace
{
generic_factory<dialogue_topic_override> &get_all_overrides()
{
    static generic_factory<dialogue_topic_override> factory( "dialogue_topic_override" );
    return factory;
}
} // namespace

/** @relates string_id */
template<>
const dialogue_topic_override &string_id<dialogue_topic_override>::obj() const
{
    return get_all_overrides().obj( *this );
}

/** @relates string_id */
template<>
bool string_id<dialogue_topic_override>::is_valid() const
{
    return get_all_overrides().is_valid( *this );
}

const std::vector<std::string_view> &dialogue_topic_overrides::builtin_rules()
{
    // talker_npc::get_topics: TALK_REBOOT, TALK_SEDATED, TALK_DEAF(_ANGRY), TALK_MUTE(_ANGRY),
    // TALK_CHURL(_ANGRY, _TRADE) and TALK_NPC_NOFACE
    static const std::vector<std::string_view> rules = {
        "reboot", "sedated", "deaf", "mute", "churl", "npc_noface"
    };
    return rules;
}

void dialogue_topic_override::load_override( const JsonObject &jo, const std::string &src )
{
    get_all_overrides().load( jo, src );
}

void dialogue_topic_override::load( const JsonObject &jo, std::string_view )
{
    if( jo.has_member( "condition" ) ) {
        read_condition( jo, "condition", condition, true );
    }
    optional( jo, was_loaded, "topic", topic );
    optional( jo, was_loaded, "priority", priority, 0 );
    optional( jo, was_loaded, "replaces", replaces );
    const std::vector<std::string_view> &rules = dialogue_topic_overrides::builtin_rules();
    for( const std::string &rule : replaces ) {
        if( std::find( rules.begin(), rules.end(), rule ) == rules.end() ) {
            jo.throw_error_at( "replaces", string_format(
                                   R"(Unknown built-in topic rule "%s" (the rules are reboot, sedated, deaf, mute, churl and npc_noface))",
                                   rule ) );
        }
    }
    if( topic.empty() && replaces.empty() ) {
        jo.throw_error( R"(A dialogue_topic_override needs a "topic", a "replaces", or both)" );
    }
}

void dialogue_topic_override::check() const
{
    if( !topic.empty() && !talk_topic_exists( topic ) ) {
        debugmsg( R"(dialogue_topic_override "%s" opens the topic "%s", which doesn't exist)",
                  id.str(), topic );
    }
}

void dialogue_topic_override::finalize_all()
{
    get_all_overrides().finalize();
}

void dialogue_topic_override::check_all()
{
    get_all_overrides().check();
}

void dialogue_topic_override::reset()
{
    get_all_overrides().reset();
}

const std::vector<dialogue_topic_override> &dialogue_topic_override::get_all()
{
    return get_all_overrides().get_all();
}

dialogue_topic_overrides::applied dialogue_topic_overrides::evaluate( const_dialogue const &d )
{
    std::vector<const dialogue_topic_override *> holding;
    for( const dialogue_topic_override &over : dialogue_topic_override::get_all() ) {
        if( !over.condition || over.condition( d ) ) {
            holding.push_back( &over );
        }
    }
    std::sort( holding.begin(), holding.end(), []( const dialogue_topic_override * a,
    const dialogue_topic_override * b ) {
        return std::forward_as_tuple( a->priority, a->id.str() ) <
               std::forward_as_tuple( b->priority, b->id.str() );
    } );
    applied ret;
    for( const dialogue_topic_override *over : holding ) {
        if( !over->topic.empty() ) {
            ret.topics.push_back( over->topic );
        }
        ret.replaced.insert( over->replaces.begin(), over->replaces.end() );
    }
    return ret;
}

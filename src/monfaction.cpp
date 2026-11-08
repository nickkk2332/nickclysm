#include "monfaction.h"

#include <list>
#include <map>
#include <set>
#include <utility>

#include "debug.h"
#include "debug_menu.h"
#include "enum_conversions.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "json.h"
#include "options.h"

// for legacy reasons "monfaction::id" is called "name" in json
static generic_factory<monfaction> faction_factory( "MONSTER_FACTION", "name" );

namespace
{
// MONDT-J12: (from, to) -> attitude, set by set_monfaction_attitude and saved with the game
std::map<std::pair<mfaction_str_id, mfaction_str_id>, mf_attitude> attitude_overrides;
} // namespace

namespace io
{
template<>
std::string enum_to_string<mf_attitude>( mf_attitude data )
{
    switch( data ) {
        case MFA_BY_MOOD:
            return "BY_MOOD";
        case MFA_NEUTRAL:
            return "NEUTRAL";
        case MFA_FRIENDLY:
            return "FRIENDLY";
        case MFA_HATE:
            return "HATE";
        case MFA_SIZE:
            break;
    }
    cata_fatal( "Invalid mf_attitude" );
}
} // namespace io

/** @relates int_id */
template<>
const monfaction &int_id<monfaction>::obj() const
{
    return faction_factory.obj( *this );
}

/** @relates int_id */
template<>
const string_id<monfaction> &int_id<monfaction>::id() const
{
    return faction_factory.convert( *this );
}

/** @relates string_id */
template<>
int_id<monfaction> string_id<monfaction>::id() const
{
    return faction_factory.convert( *this, mfaction_id( 0 ) );
}

/** @relates string_id */
template<>
const monfaction &string_id<monfaction>::obj() const
{
    return faction_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<monfaction>::is_valid() const
{
    return faction_factory.is_valid( *this );
}

/** @relates int_id */
template<>
int_id<monfaction>::int_id( const string_id<monfaction> &id ) : _id( id.id() ) {}

bool monfaction::is_root() const
{
    return id == base_faction;
}

mf_attitude monfaction::attitude( const mfaction_id &other ) const
{
    int idx = other.to_i();
    if( idx < 0 || idx >= static_cast<int>( attitude_vec.size() ) ) {
        debugmsg( "Invalid mfaction_id in the attitude check: %d", idx );
        return MFA_FRIENDLY;
    }
    return static_cast<mf_attitude>( attitude_vec[other.to_i()] );
}

std::optional<mf_attitude> monfaction::attitude_rec( const mfaction_str_id &other ) const
{
    const auto &found = attitude_map.find( other );
    if( found != attitude_map.end() ) {
        return found->second;
    }

    if( !other->is_root() ) {
        return attitude( other->base_faction );
    }

    return std::nullopt;
}

void monfactions::reset()
{
    faction_factory.reset();
}

void monfactions::load_monster_faction( const JsonObject &jo, const std::string &src )
{
    faction_factory.load( jo, src );
}

bool monfaction::detect_base_faction_cycle() const
{
    std::list<mfaction_str_id> parents_path;
    std::set<mfaction_str_id> parents;
    mfaction_str_id cur = id;

    while( !cur->is_root() ) {
        parents_path.push_back( cur );
        // cycle detected!
        if( parents.count( cur ) ) {
            debugmsg( "In MONSTER_FACTION '%s': detected a cycle in base_faction relations: %s",
            this -> id.str(), debug_menu::iterable_to_string( parents_path, " -> ", []( const auto & el ) {
                return el.str();
            } ) );
            return true;
        }
        parents.insert( cur );
        cur = cur->base_faction;
    }
    return false;
}

void monfaction::inherit_parent_attitude_rec(
    std::set<mfaction_str_id> &processed,
    std::map<mfaction_str_id, mf_attitude> &child_attitudes ) const
{
    // already processed, just populate the return list and exit
    if( !processed.count( id ) ) {
        if( !is_root() ) {
            base_faction->inherit_parent_attitude_rec( processed, child_attitudes );
        }
        // wish we had 'map::merge'
        for( const auto &ci : child_attitudes ) {
            attitude_map.emplace( ci.first, ci.second );
        }
    }
    for( const auto &att : attitude_map ) {
        // child attitude settings overwrite those of a parent
        child_attitudes[att.first] = att.second;
    }
    processed.insert( id );
}

void monfaction::populate_attitude_vec() const
{
    attitude_vec.clear();
    for( const monfaction &f : faction_factory.get_all() ) {
        const std::optional<mf_attitude> &attitude = attitude_rec( f.id );
        if( !attitude ) {
            debugmsg( "Invalid faction relations (no relation found): %s -> %s",
                      id.c_str(), f.id.c_str() );
        }
        attitude_vec.push_back( attitude.value_or( MFA_FRIENDLY ) );
    }
    attitude_vec.shrink_to_fit();
}

void monfaction::finalize()
{
    // `detect_base_faction_cycle` detects a cycle that is formed by valid `base_faction` relations.
    // it will produce a warning if cycle is detected
    if( detect_base_faction_cycle() ) {
        // break the cycle
        base_faction = mfaction_str_id::NULL_ID();
    }
    if( !ignores_npcs_if_option.empty() && ( !get_options().has_option( ignores_npcs_if_option ) ||
            get_options().get_option( ignores_npcs_if_option ).getType() != "bool" ) ) {
        debugmsg( "MONSTER_FACTION %s: ignores_npcs_if_option \"%s\" is not a boolean option.",
                  id.c_str(), ignores_npcs_if_option );
        ignores_npcs_if_option.clear();
    }
}

void monfaction::reset_attitude_map() const
{
    attitude_map.clear();
    for( const mfaction_str_id &mfac : _att_by_mood ) {
        attitude_map[mfac] = MFA_BY_MOOD;
    }
    for( const mfaction_str_id &mfac : _att_neutral ) {
        attitude_map[mfac] = MFA_NEUTRAL;
    }
    for( const mfaction_str_id &mfac : _att_friendly ) {
        attitude_map[mfac] = MFA_FRIENDLY;
    }
    for( const mfaction_str_id &mfac : _att_hate ) {
        attitude_map[mfac] = MFA_HATE;
    }
    // MONDT-J12: set_monfaction_attitude, as if this faction's JSON listed it
    for( const auto &ov : attitude_overrides ) {
        if( ov.first.first == id ) {
            attitude_map[ov.first.second] = ov.second;
        }
    }

    // by default faction is friendly to itself (don't overwrite if explicitly specified)
    attitude_map.emplace( id, MFA_FRIENDLY );
}

void monfaction::rebuild_attitudes()
{
    const std::vector<monfaction> &all = faction_factory.get_all();
    for( const monfaction &f : all ) {
        f.reset_attitude_map();
    }

    // adds attitudes_map collected from all `base_faction`
    std::set<mfaction_str_id> processed;
    for( const monfaction &f : all ) {
        std::map<mfaction_str_id, mf_attitude> accum;
        f.inherit_parent_attitude_rec( processed, accum );
    }

    // at this point all factions are loaded, populate the final attitude cache
    for( const monfaction &f : all ) {
        f.populate_attitude_vec();
    }
}

void monfactions::finalize()
{
    if( faction_factory.get_all().empty() ) {
        debugmsg( "No monster factions found." );
        return;
    }
    if( !mfaction_str_id::NULL_ID().is_valid() ) {
        debugmsg( "MONSTER_FACTION \"sentinel\" (entry with empty name) is not found in json." );
    }
    faction_factory.finalize();

    monfaction::rebuild_attitudes();
}

void monfactions::set_attitude_override( const mfaction_str_id &from, const mfaction_str_id &to,
        const std::optional<mf_attitude> &att )
{
    const std::pair<mfaction_str_id, mfaction_str_id> key( from, to );
    if( att ) {
        attitude_overrides[key] = *att;
    } else if( attitude_overrides.erase( key ) == 0 ) {
        return;
    }
    if( !faction_factory.get_all().empty() ) {
        monfaction::rebuild_attitudes();
    }
}

void monfactions::clear_attitude_overrides()
{
    if( attitude_overrides.empty() ) {
        return;
    }
    attitude_overrides.clear();
    if( !faction_factory.get_all().empty() ) {
        monfaction::rebuild_attitudes();
    }
}

std::optional<mf_attitude> monfactions::get_attitude_override( const mfaction_str_id &from,
        const mfaction_str_id &to )
{
    const auto found = attitude_overrides.find( std::make_pair( from, to ) );
    if( found == attitude_overrides.end() ) {
        return std::nullopt;
    }
    return found->second;
}

bool monfactions::parse_attitude( std::string_view name, std::optional<mf_attitude> &att )
{
    if( name == "DEFAULT" ) {
        att = std::nullopt;
        return true;
    }
    const std::optional<mf_attitude> parsed =
        io::string_to_enum_optional<mf_attitude>( std::string( name ) );
    if( !parsed ) {
        return false;
    }
    att = parsed;
    return true;
}

void monfactions::serialize_attitude_overrides( JsonOut &jsout )
{
    jsout.start_array();
    for( const auto &ov : attitude_overrides ) {
        jsout.start_object();
        jsout.member( "from", ov.first.first );
        jsout.member( "to", ov.first.second );
        jsout.member( "attitude", io::enum_to_string( ov.second ) );
        jsout.end_object();
    }
    jsout.end_array();
}

void monfactions::deserialize_attitude_overrides( const JsonValue &jv )
{
    attitude_overrides.clear();
    for( JsonObject jo : jv.get_array() ) {
        const mfaction_str_id from( jo.get_string( "from" ) );
        const mfaction_str_id to( jo.get_string( "to" ) );
        const std::string attitude = jo.get_string( "attitude" );
        std::optional<mf_attitude> att;
        // a faction from a mod that is no longer loaded drops its change
        if( from.is_valid() && to.is_valid() && parse_attitude( attitude, att ) && att ) {
            attitude_overrides[std::make_pair( from, to )] = *att;
        }
    }
    if( !faction_factory.get_all().empty() ) {
        monfaction::rebuild_attitudes();
    }
}

void monfaction::load( const JsonObject &jo, std::string_view )
{
    optional( jo, was_loaded, "base_faction", base_faction, mfaction_str_id() );
    optional( jo, was_loaded, "by_mood", _att_by_mood, string_id_reader<monfaction>() );
    optional( jo, was_loaded, "neutral", _att_neutral, string_id_reader<monfaction>() );
    optional( jo, was_loaded, "friendly", _att_friendly, string_id_reader<monfaction>() );
    optional( jo, was_loaded, "hate", _att_hate, string_id_reader<monfaction>() );
    optional( jo, was_loaded, "ignores_npcs_if_option", ignores_npcs_if_option );

    reset_attitude_map();
}

const std::vector<monfaction> &monfactions::get_all()
{
    return faction_factory.get_all();
}

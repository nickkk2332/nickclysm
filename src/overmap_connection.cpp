#include "overmap_connection.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

#include "cata_assert.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "overmap_location.h"

namespace
{

generic_factory<overmap_connection> connections( "overmap connection" );

} // namespace

static const std::unordered_map<std::string, overmap_connection::subtype::flag>
connection_subtype_flag_map = {
    { "ORTHOGONAL", overmap_connection::subtype::flag::orthogonal },
    { "PERPENDICULAR_CROSSING", overmap_connection::subtype::flag::perpendicular_crossing }
};

template<>
bool string_id<overmap_connection>::is_valid() const
{
    return connections.is_valid( *this );
}

template<>
const overmap_connection &string_id<overmap_connection>::obj() const
{
    return connections.obj( *this );
}

bool overmap_connection::subtype::allows_terrain( const int_id<oter_t> &oter ) const
{
    if( oter->type_is( terrain ) ) {
        return true;    // Can be built on similar terrains.
    }

    return std::any_of( locations.cbegin(),
    locations.cend(), [&oter]( const string_id<overmap_location> &elem ) {
        return elem->test( oter );
    } );
}

void overmap_connection::subtype::load( const JsonObject &jo )
{
    const auto flag_reader =
        make_flag_reader( connection_subtype_flag_map, "connection subtype flag" );

    mandatory( jo, false, "terrain", terrain );
    mandatory( jo, false, "locations", locations );

    optional( jo, false, "basic_cost", basic_cost, 0 );
    optional( jo, false, "flags", flags, flag_reader );
}

void overmap_connection::subtype::deserialize( const JsonObject &jo )
{
    load( jo );
}

const overmap_connection::subtype *overmap_connection::pick_subtype_for(
    const int_id<oter_t> &ground ) const
{
    if( !ground ) {
        return nullptr;
    }

    const size_t cache_index = ground.to_i();
    cata_assert( cache_index < cached_subtypes.size() );

    if( cached_subtypes[cache_index] ) {
        return cached_subtypes[cache_index].value;
    }

    const auto iter = std::find_if( subtypes.cbegin(),
    subtypes.cend(), [&ground]( const subtype & elem ) {
        return elem.allows_terrain( ground );
    } );

    const overmap_connection::subtype *result = iter != subtypes.cend() ? &*iter : nullptr;

    cached_subtypes[cache_index].value = result;
    cached_subtypes[cache_index].assigned = true;

    return result;
}

bool overmap_connection::has( const int_id<oter_t> &oter ) const
{
    return std::find_if( subtypes.cbegin(), subtypes.cend(), [&oter]( const subtype & elem ) {
        return oter->type_is( elem.terrain );
    } ) != subtypes.cend();
}

void overmap_connection::load( const JsonObject &jo, std::string_view )
{
    load_subtypes( jo );
    optional( jo, was_loaded, "special_target", special_target, special_connection_target::city );
}

// OMAP-J11: "subtypes" replaces the list; with copy-from, "extend": { "subtypes": [...] } appends
// subtypes (the first subtype that allows a terrain is used, so appended ones come last) and
// "delete": { "subtypes": [...] } removes those with the given terrain (and locations, if given).
void overmap_connection::load_subtypes( const JsonObject &jo )
{
    if( jo.has_member( "subtypes" ) ) {
        subtypes.clear();
        for( const JsonObject sjo : jo.get_array( "subtypes" ) ) {
            subtypes.emplace_back();
            subtypes.back().load( sjo );
        }
    } else if( !was_loaded ) {
        jo.throw_error( R"(missing mandatory member "subtypes")" );
    }
    if( !was_loaded ) {
        return;
    }
    if( jo.has_object( "extend" ) ) {
        JsonObject ext = jo.get_object( "extend" );
        ext.allow_omitted_members();
        if( ext.has_member( "subtypes" ) ) {
            for( const JsonObject sjo : ext.get_array( "subtypes" ) ) {
                subtypes.emplace_back();
                subtypes.back().load( sjo );
            }
        }
    }
    if( jo.has_object( "delete" ) ) {
        JsonObject del = jo.get_object( "delete" );
        del.allow_omitted_members();
        if( del.has_member( "subtypes" ) ) {
            for( const JsonObject sjo : del.get_array( "subtypes" ) ) {
                string_id<oter_type_t> terrain;
                mandatory( sjo, false, "terrain", terrain );
                std::optional<std::set<string_id<overmap_location>>> locations;
                if( sjo.has_member( "locations" ) ) {
                    locations.emplace();
                    sjo.read( "locations", *locations );
                }
                const size_t before = subtypes.size();
                subtypes.remove_if( [&terrain, &locations]( const subtype & st ) {
                    return st.terrain == terrain && ( !locations || st.locations == *locations );
                } );
                if( subtypes.size() == before ) {
                    sjo.throw_error( "no subtype of this connection matches this delete entry" );
                }
            }
        }
    }
}

void overmap_connection::check() const
{
    if( subtypes.empty() ) {
        debugmsg( "Overmap connection \"%s\" doesn't have subtypes.", id.c_str() );
    }
    for( const overmap_connection::subtype &subtype : subtypes ) {
        if( !subtype.terrain.is_valid() ) {
            debugmsg( "In overmap connection \"%s\", terrain \"%s\" is invalid.", id.c_str(),
                      subtype.terrain.c_str() );
        }
        for( const auto &location : subtype.locations ) {
            if( !location.is_valid() ) {
                debugmsg( "In overmap connection \"%s\", location \"%s\" is invalid.", id.c_str(),
                          location.c_str() );
            }
        }
    }
}

void overmap_connection::finalize()
{
    cached_subtypes.resize( overmap_terrains::get_all().size() );
}

void overmap_connections::load( const JsonObject &jo, const std::string &src )
{
    connections.load( jo, src );
}

void overmap_connections::finalize()
{
    connections.finalize();
}

void overmap_connections::check_consistency()
{
    connections.check();
}

void overmap_connections::reset()
{
    connections.reset();
}

string_id<overmap_connection> overmap_connections::guess_for( const int_id<oter_t> &oter_id )
{
    const auto &all = connections.get_all();
    const auto iter = std::find_if( all.cbegin(),
    all.cend(), [&oter_id]( const overmap_connection & elem ) {
        return elem.pick_subtype_for( oter_id ) != nullptr;
    } );

    return iter != all.cend() ? iter->id : string_id<overmap_connection>::NULL_ID();
}

string_id<overmap_connection> overmap_connections::guess_for( const int_id<oter_type_t> &oter_id )
{
    return guess_for( oter_id->get_first() );
}

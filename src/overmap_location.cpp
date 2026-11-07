#include "overmap_location.h"

#include <string>
#include <unordered_map>
#include <utility>

#include "debug.h"
#include "enum_conversions.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "omdata.h"
#include "rng.h"

namespace
{

generic_factory<overmap_location> locations( "overmap location" );

} // namespace

template<>
bool string_id<overmap_location>::is_valid() const
{
    return locations.is_valid( *this );
}

template<>
const overmap_location &string_id<overmap_location>::obj() const
{
    return locations.obj( *this );
}

bool overmap_location::test( const int_id<oter_t> &oter ) const
{
    return terrains.count( oter->get_type_id() );
}

oter_type_id overmap_location::get_random_terrain() const
{
    return random_entry( terrains );
}

void overmap_location::load( const JsonObject &jo, std::string_view )
{
    // OMAP-J11: readers, so that a copy-from can extend or delete flags and terrains.
    optional( jo, was_loaded, "flags", flags, string_reader{} );
    optional( jo, was_loaded, "terrains", terrains, string_id_reader<oter_type_t> {} );
    if( flags.empty() && terrains.empty() ) {
        jo.throw_error( "At least one flag or terrain must be specified." );
    }
}

const cata::flat_set<oter_type_str_id> &overmap_location::get_all_terrains() const
{
    return terrains;
}

void overmap_location::check() const
{
    for( const auto &element : terrains ) {
        if( !element.is_valid() ) {
            debugmsg( "In overmap location \"%s\", terrain \"%s\" is invalid.", id.c_str(), element.c_str() );
        }
    }
}

void overmap_location::finalize()
{
    const std::unordered_map<std::string, oter_flags> &oter_flags_map =
        io::get_enum_lookup_map<oter_flags>();
    for( const std::string &elem : flags ) {
        auto it = oter_flags_map.find( elem );
        // OMAP-J12: a flag that isn't an engine flag is a mod's own overmap terrain flag.
        // (A bool and a plain enum, not std::optional: GCC 9 LTO reports a false
        // maybe-uninitialized on the optional.)
        const bool engine_flag = it != oter_flags_map.end();
        const oter_flags check_flag = engine_flag ? it->second : oter_flags::num_oter_flags;
        for( const oter_t &ter_elem : overmap_terrains::get_all() ) {
            if( engine_flag ? ter_elem.has_flag( check_flag ) : ter_elem.has_flag( elem ) ) {
                terrains.insert( ter_elem.get_type_id() );
            }
        }
    }
}

void overmap_locations::load( const JsonObject &jo, const std::string &src )
{
    locations.load( jo, src );
}

void overmap_locations::check_consistency()
{
    locations.check();
}

void overmap_locations::reset()
{
    locations.reset();
}

const std::vector<overmap_location> &overmap_locations::get_all()
{
    return locations.get_all();
}

void overmap_locations::finalize()
{
    locations.finalize();
}

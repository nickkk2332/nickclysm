#include "butchery_requirements.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "butchery.h"
#include "cata_utility.h"
#include "creature.h"
#include "debug.h"
#include "enum_conversions.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "item.h"
#include "requirements.h"
#include "string_formatter.h"

namespace
{
generic_factory<butchery_requirements> butchery_req_factory( "butchery_requirements" );
} // namespace

template<>
const butchery_requirements &string_id<butchery_requirements>::obj() const
{
    return butchery_req_factory.obj( *this );
}

template<>
bool string_id<butchery_requirements>::is_valid() const
{
    return butchery_req_factory.is_valid( *this );
}

void butchery_requirements::load_butchery_req( const JsonObject &jo, const std::string &src )
{
    butchery_req_factory.load( jo, src );
}

void butchery_requirements::finalize_all()
{
    butchery_req_factory.finalize();
}

const std::vector<butchery_requirements> &butchery_requirements::get_all()
{
    return butchery_req_factory.get_all();
}

void butchery_requirements::reset()
{
    butchery_req_factory.reset();
}

bool butchery_requirements::is_valid() const
{
    return butchery_req_factory.is_valid( this->id );
}

void butchery_requirements::load( const JsonObject &jo, std::string_view )
{
    // One entry per creature size, from tiny to huge
    const int num_size_entries = static_cast<int>( creature_size::num_sizes ) - 1;
    for( const JsonMember member : jo.get_object( "requirements" ) ) {
        std::optional<double> mod_val = svtod( member.name() );
        if( !mod_val.has_value() ) {
            jo.throw_error( "Invalid key for requirements, cannot convert to float" );
        }
        float modifier = mod_val.value();
        const JsonArray size_entries = member.get_array();
        if( size_entries.size() != static_cast<size_t>( num_size_entries ) ) {
            member.throw_error( string_format(
                                    "butchery_requirement %s: \"%s\" has %d entries, but it needs exactly %d, one "
                                    "per creature size from tiny to huge", id.c_str(), member.name(),
                                    size_entries.size(), num_size_entries ) );
        }
        requirements.emplace( modifier, std::map<creature_size, std::map<butcher_type, requirement_id>> {} );

        int critter_size = static_cast<int>( creature_size::tiny );
        for( JsonObject butcher_jo : size_entries ) {
            requirements[modifier].emplace( static_cast<creature_size>( critter_size ),
                                            std::map<butcher_type, requirement_id> {} );

            for( int i_butchery_type = 0; i_butchery_type < static_cast<int>( butcher_type::NUM_TYPES );
                 i_butchery_type++ ) {
                butcher_type converted = static_cast<butcher_type>( i_butchery_type );
                requirements[modifier][static_cast<creature_size>( critter_size )][converted] =
                    requirement_id( butcher_jo.get_string( io::enum_to_string( converted ) ) );
            }

            critter_size++;
        }
    }
}

void butchery_requirements::check_consistency()
{
    for( const butchery_requirements &req : get_all() ) {
        if( req.requirements.empty() ) {
            debugmsg( "ERROR: butchery_requirement %s has no requirements", req.id.c_str() );
        }
        for( const auto &size_req : req.requirements ) {
            if( size_req.second.size() != static_cast<size_t>( creature_size::num_sizes - 1 ) ) {
                debugmsg( "ERROR: %s needs exactly %d entries to cover all creature sizes",
                          req.id.c_str(), static_cast<int>( creature_size::num_sizes ) - 1 );
            }
            for( const auto &butcher_req : size_req.second ) {
                if( butcher_req.second.size() != static_cast<size_t>( butcher_type::NUM_TYPES ) ) {
                    debugmsg( "ERROR: %s needs exactly %d entries to cover all butchery types",
                              req.id.c_str(), static_cast<int>( butcher_type::NUM_TYPES ) );
                }
                for( const auto &type_req : butcher_req.second ) {
                    if( !type_req.second.is_valid() ) {
                        debugmsg( "butchery_requirement %s has unknown requirement \"%s\" for %s at "
                                  "creature size %d", req.id.c_str(), type_req.second.c_str(),
                                  io::enum_to_string( type_req.first ),
                                  static_cast<int>( butcher_req.first ) );
                    }
                }
            }
        }
    }
}

// The requirement for one creature size and butcher type, or nullptr if there is none
static const requirement_id *find_requirement(
    const std::map<creature_size, std::map<butcher_type, requirement_id>> &by_size,
    const creature_size size, const butcher_type butcher )
{
    const auto size_iter = by_size.find( size );
    if( size_iter == by_size.end() ) {
        return nullptr;
    }
    const auto butcher_iter = size_iter->second.find( butcher );
    if( butcher_iter == size_iter->second.end() ) {
        return nullptr;
    }
    return &butcher_iter->second;
}

std::pair<float, requirement_id> butchery_requirements::get_fastest_requirements(
    const Character *actor, const temp_crafting_inventory &crafting_inv, creature_size size,
    butcher_type butcher ) const
{
    std::optional<std::pair<float, requirement_id>> last;
    for( const std::pair<const float, std::map<creature_size, std::map<butcher_type, requirement_id>>>
         &riter : requirements ) {
        const requirement_id *req = find_requirement( riter.second, size, butcher );
        if( req == nullptr ) {
            continue;
        }
        if( ( *req )->can_make_with_inventory( actor, crafting_inv, is_crafting_component ) ) {
            return std::make_pair( riter.first, *req );
        }
        last = std::make_pair( riter.first, *req );
    }
    // we didn't find anything we could "craft", so return the one with the highest multiplier
    if( last ) {
        return *last;
    }
    // Only an unknown id (the factory's empty dummy) or broken data gets here
    debugmsg( "butchery_requirement %s has no requirement for creature size %d and %s",
              id.c_str(), static_cast<int>( size ), io::enum_to_string( butcher ) );
    return std::make_pair( 1.0f, requirement_id::NULL_ID() );
}

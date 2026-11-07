#include "event_field_transformations.h"

#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "coordinates.h"
#include "flat_set.h" // IWYU pragma: keep
#include "itype.h"
#include "mapdata.h"
#include "mtype.h"
#include "mutation.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "point.h"
#include "type_id.h"

// The item category of an item type (the one its items are listed under), if it has one
static std::vector<cata_variant> category_of_itype( const cata_variant &v )
{
    const itype_id type = v.get<itype_id>();
    if( type.is_empty() || !type.is_valid() || !type->category_force.is_valid() ) {
        return {};
    }
    return { cata_variant::make<cata_variant_type::item_category_id>( type->category_force ) };
}

static std::vector<cata_variant> flags_of_furniture( const cata_variant &v )
{
    const furn_str_id furn = v.get<furn_str_id>();
    if( furn.is_empty() || !furn.is_valid() ) {
        return {};
    }
    std::vector<cata_variant> result;
    for( const std::string &s : furn->get_flags() ) {
        result.push_back( cata_variant::make<cata_variant_type::string>( s ) );
    }
    return result;
}

static std::vector<cata_variant> flags_of_monster( const cata_variant &v )
{
    const mtype_id type = v.get<mtype_id>();
    if( type.is_empty() || !type.is_valid() ) {
        return {};
    }
    std::vector<cata_variant> result;
    for( const mon_flag &flag : mon_flag::get_all() ) {
        if( type->has_flag( flag.id.id() ) ) {
            result.push_back( cata_variant::make<cata_variant_type::string>( flag.id.str() ) );
        }
    }
    return result;
}

static std::vector<cata_variant> flags_of_terrain_str( const cata_variant &v )
{
    const ter_str_id ter = v.get<ter_str_id>();
    if( ter.is_empty() || !ter.is_valid() ) {
        return {};
    }
    std::vector<cata_variant> result;
    for( const std::string &s : ter->get_flags() ) {
        result.push_back( cata_variant::make<cata_variant_type::string>( s ) );
    }
    return result;
}

static std::vector<cata_variant> materials_of_itype( const cata_variant &v )
{
    const itype_id type = v.get<itype_id>();
    if( type.is_empty() || !type.is_valid() ) {
        return {};
    }
    std::vector<cata_variant> result;
    for( const std::pair<const material_id, int> &mat : type->materials ) {
        result.push_back( cata_variant::make<cata_variant_type::material_id>( mat.first ) );
    }
    return result;
}

static std::vector<cata_variant> mutation_category_of_trait( const cata_variant &v )
{
    const trait_id trait = v.get<trait_id>();
    if( trait.is_empty() || !trait.is_valid() ) {
        return {};
    }
    std::vector<cata_variant> result;
    for( const mutation_category_id &category : trait->category ) {
        result.push_back( cata_variant::make<cata_variant_type::mutation_category_id>( category ) );
    }
    return result;
}

static std::vector<cata_variant> flags_of_itype( const cata_variant &v )
{
    const itype::FlagsSetType &flags = v.get<itype_id>()->get_flags();
    std::vector<cata_variant> result;
    result.reserve( flags.size() );
    for( const flag_id &s : flags ) {
        result.push_back( cata_variant::make<cata_variant_type::flag_id>( s ) );
    }
    return result;
}

static std::vector<cata_variant> flags_of_terrain( const cata_variant &v )
{
    const std::set<std::string> &flags = v.get<ter_id>()->get_flags();
    std::vector<cata_variant> result;
    result.reserve( flags.size() );
    for( const std::string &s : flags ) {
        result.push_back( cata_variant::make<cata_variant_type::string>( s ) );
    }
    return result;
}

static std::vector<cata_variant> is_mounted( const cata_variant &v )
{
    const mtype_id mount = v.get<mtype_id>();
    std::vector<cata_variant> result = { cata_variant( !mount.is_empty() ) };
    return result;
}

static std::vector<cata_variant> is_swimming_terrain( const cata_variant &v )
{
    const ter_id &ter = v.get<ter_id>();
    const bool swimming = ter->has_flag( ter_furn_flag::TFLAG_DEEP_WATER ) &&
                          ter->has_flag( ter_furn_flag::TFLAG_SWIMMABLE );
    std::vector<cata_variant> result = { cata_variant( swimming ) };
    return result;
}

static std::vector<cata_variant> math_abs( const cata_variant &v )
{
    std::vector<cata_variant> result = { cata_variant( std::abs( v.get<int>() ) ) };
    return result;
}

static std::vector<cata_variant> oter_type_of_oter( const cata_variant &v )
{
    const oter_id oter = v.get<oter_id>();
    std::vector<cata_variant> result = { cata_variant( oter->get_type_id() ) };
    return result;
}

// Statistics are rebuilt from the whole event history, on every game load among other times,
// so this only consults overmaps already in memory: it never loads or generates one. Statistics
// that need the special of an OMT the avatar entered read avatar_enters_omt's overmap_special.
static std::vector<cata_variant> overmap_special_at( const cata_variant &v )
{
    const tripoint_abs_omt p( v.get<tripoint>() );
    std::optional<overmap_special_id> special = overmap_buffer.overmap_special_at_if_loaded( p );
    if( special ) {
        return { cata_variant( *special ) };
    } else {
        return {};
    }
}

// The special itself, or nothing when it is empty (the OMT is in no special)
static std::vector<cata_variant> overmap_special_if_any( const cata_variant &v )
{
    if( v.get_string().empty() ) {
        return {};
    }
    return { v };
}

static std::vector<cata_variant> species_of_monster( const cata_variant &v )
{
    const std::set<species_id> &species = v.get<mtype_id>()->species;
    std::vector<cata_variant> result;
    result.reserve( species.size() );
    for( const species_id &s : species ) {
        result.emplace_back( s );
    }
    return result;
}

const std::unordered_map<std::string, event_field_transformation> event_field_transformations = {
    {
        "category_of_itype",
        { category_of_itype, cata_variant_type::item_category_id, { cata_variant_type::itype_id } }
    },
    {
        "flags_of_furniture",
        { flags_of_furniture, cata_variant_type::string, { cata_variant_type::furn_str_id } }
    },
    {
        "flags_of_monster",
        { flags_of_monster, cata_variant_type::string, { cata_variant_type::mtype_id } }
    },
    {
        "flags_of_terrain_str",
        { flags_of_terrain_str, cata_variant_type::string, { cata_variant_type::ter_str_id } }
    },
    {
        "materials_of_itype",
        { materials_of_itype, cata_variant_type::material_id, { cata_variant_type::itype_id } }
    },
    {
        "mutation_category_of_trait",
        {
            mutation_category_of_trait, cata_variant_type::mutation_category_id,
            { cata_variant_type::trait_id }
        }
    },
    {
        "flags_of_itype",
        { flags_of_itype, cata_variant_type::flag_id, { cata_variant_type::itype_id } }
    },
    {
        "flags_of_terrain",
        { flags_of_terrain, cata_variant_type::string, { cata_variant_type::ter_id } }
    },
    {
        "is_mounted",
        { is_mounted, cata_variant_type::bool_, { cata_variant_type::mtype_id } }
    },
    {
        "is_swimming_terrain",
        { is_swimming_terrain, cata_variant_type::bool_, { cata_variant_type::ter_id } }
    },
    {
        "math_abs",
        { math_abs, cata_variant_type::int_, { cata_variant_type::int_ } }
    },
    {
        "oter_type_of_oter",
        { oter_type_of_oter, cata_variant_type::oter_type_str_id, { cata_variant_type::oter_id } }
    },
    {
        "overmap_special_at",
        { overmap_special_at, cata_variant_type::overmap_special_id, { cata_variant_type::tripoint } }
    },
    {
        "overmap_special_if_any",
        {
            overmap_special_if_any, cata_variant_type::overmap_special_id,
            { cata_variant_type::overmap_special_id }
        }
    },
    {
        "species_of_monster",
        { species_of_monster, cata_variant_type::species_id, { cata_variant_type::mtype_id } }
    },
};

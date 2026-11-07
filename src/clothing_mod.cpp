#include "clothing_mod.h"

#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "damage.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "item.h"

namespace
{

generic_factory<clothing_mod> all_clothing_mods( "clothing mods" );

} // namespace

static std::map<clothing_mod_type, std::vector<clothing_mod>> clothing_mods_by_type;

/** @relates string_id */
template<>
bool string_id<clothing_mod>::is_valid() const
{
    return all_clothing_mods.is_valid( *this );
}

/** @relates string_id */
template<>
const clothing_mod &string_id<clothing_mod>::obj() const
{
    return all_clothing_mods.obj( *this );
}

void clothing_mod::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "flag", flag );
    mandatory( jo, was_loaded, "item", item_string );
    mandatory( jo, was_loaded, "implement_prompt", implement_prompt );
    mandatory( jo, was_loaded, "destroy_prompt", destroy_prompt );
    optional( jo, was_loaded, "restricted", restricted, false );

    for( const JsonObject mv_jo : jo.get_array( "mod_value" ) ) {
        mod_value mv;
        std::string temp_str;
        mandatory( mv_jo, was_loaded, "type", temp_str );
        // "encumbrance", "warmth" or a damage type.  "fire" is the old name of heat.
        if( temp_str == "encumbrance" ) {
            mv.type = clothing_mod_type_encumbrance;
        } else if( temp_str == "warmth" ) {
            mv.type = clothing_mod_type_warmth;
        } else {
            mv.type = clothing_mod_type_damage;
            mv.damage_type = damage_type_id( temp_str == "fire" ? "heat" : temp_str );
        }
        mandatory( mv_jo, was_loaded, "value", mv.value );
        optional( mv_jo, was_loaded, "round_up", mv.round_up );
        for( const JsonValue entry : mv_jo.get_array( "proportion" ) ) {
            const std::string &str = entry.get_string();
            if( str == "thickness" ) {
                mv.thickness_proportion = true;
            } else if( str == "coverage" ) {
                mv.coverage_proportion = true;
            } else {
                entry.throw_error( R"(Invalid value, valid are: "coverage" and "thickness")" );
            }
        }
        mod_values.push_back( mv );
    }
}

// The value of one mod_value on an item, scaled by its thickness and coverage if asked.
static float scaled_mod_value( const mod_value &mv, const item &it )
{
    float tmp = mv.value;
    if( mv.thickness_proportion ) {
        const int thickness = it.get_thickness();
        tmp *= thickness;
    }
    if( mv.coverage_proportion ) {
        tmp *= it.get_avg_coverage() / 100.0f;
    }
    if( mv.round_up ) {
        tmp = std::ceil( tmp );
    }
    return tmp;
}

float clothing_mod::get_mod_val( const clothing_mod_type &type, const item &it ) const
{
    float result = 0.0f;
    for( const mod_value &mv : mod_values ) {
        if( mv.type == type ) {
            result += scaled_mod_value( mv, it );
        }
    }
    return result;
}

float clothing_mod::get_damage_mod_val( const damage_type_id &dt, const item &it ) const
{
    float result = 0.0f;
    bool dedicated = false;
    for( const mod_value &mv : mod_values ) {
        if( mv.type == clothing_mod_type_damage && mv.damage_type == dt ) {
            result += scaled_mod_value( mv, it );
            dedicated = true;
        }
    }
    if( dedicated || dt.is_null() || !dt.is_valid() ) {
        return result;
    }
    // No value of its own: fall back on the type it's derived from, as materials do
    const std::pair<damage_type_id, float> &derived = dt->derived_from;
    if( derived.first.is_null() ) {
        return result;
    }
    for( const mod_value &mv : mod_values ) {
        if( mv.type == clothing_mod_type_damage && mv.damage_type == derived.first ) {
            result += scaled_mod_value( mv, it ) * derived.second;
        }
    }
    return result;
}

bool clothing_mod::has_mod_type( const clothing_mod_type &type ) const
{
    for( const mod_value &mv : mod_values ) {
        if( mv.type == type ) {
            return true;
        }
    }
    return false;
}

size_t clothing_mod::count()
{
    return all_clothing_mods.size();
}

void clothing_mods::load( const JsonObject &jo, const std::string &src )
{
    all_clothing_mods.load( jo, src );
}

void clothing_mod::finalize_all()
{
    all_clothing_mods.finalize();
    // Items read their mod values on demand: anything worked out before every mod was loaded
    // is dropped
    clothing_mods_by_type.clear();
    for( const clothing_mod &cm : all_clothing_mods.get_all() ) {
        for( const mod_value &mv : cm.mod_values ) {
            if( mv.type == clothing_mod_type_damage && !mv.damage_type.is_valid() ) {
                debugmsg( "clothing_mod %s has a mod_value of unknown type \"%s\": valid are "
                          "\"encumbrance\", \"warmth\" and damage types", cm.id.str(),
                          mv.damage_type.str() );
            }
        }
    }
}

void clothing_mods::reset()
{
    all_clothing_mods.reset();
    clothing_mods_by_type.clear();
}

const std::vector<clothing_mod> &clothing_mods::get_all()
{
    return all_clothing_mods.get_all();
}

const std::vector<clothing_mod> &clothing_mods::get_all_with( clothing_mod_type type )
{
    const auto iter = clothing_mods_by_type.find( type );
    if( iter != clothing_mods_by_type.end() ) {
        return iter->second;
    } else {
        // Build cache
        std::vector<clothing_mod> list;
        for( const clothing_mod &cm : get_all() ) {
            if( cm.has_mod_type( type ) ) {
                list.push_back( cm );
            }
        }
        clothing_mods_by_type.emplace( type, std::move( list ) );
        return clothing_mods_by_type[type];
    }
}

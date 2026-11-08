#pragma once
#ifndef CATA_SRC_CLOTHING_MOD_H
#define CATA_SRC_CLOTHING_MOD_H

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "translation.h"
#include "type_id.h"

class JsonObject;
class item;
template<typename T> struct enum_traits;

// What a clothing mod value changes: the armor's resistance to one damage type (see
// mod_value::damage_type), its encumbrance or its warmth.
enum clothing_mod_type : int {
    clothing_mod_type_damage,
    clothing_mod_type_encumbrance,
    clothing_mod_type_warmth,
    num_clothing_mod_types
};

inline auto  format_as( clothing_mod_type cmt )
{
    return static_cast<std::underlying_type_t<clothing_mod_type>>( cmt );
}

template<>
struct enum_traits<clothing_mod_type> {
    static constexpr clothing_mod_type last = clothing_mod_type::num_clothing_mod_types;
};

struct mod_value {
    clothing_mod_type type = clothing_mod_type::num_clothing_mod_types;
    // The damage type a clothing_mod_type_damage value protects against
    damage_type_id damage_type = damage_type_id::NULL_ID();
    float value = 0.0f;
    bool round_up = false;
    bool thickness_proportion = false;
    bool coverage_proportion = false;
};

struct clothing_mod {
    void load( const JsonObject &jo, std::string_view src );
    static void finalize_all();
    /** Encumbrance or warmth this mod adds to the item. */
    float get_mod_val( const clothing_mod_type &type, const item &it ) const;
    /**
     * Resistance to a damage type this mod adds to the item.  A damage type the mod has no
     * value for gets the value of the type it's derived from, times its factor (stab gets 0.8
     * of the cut value), as the resistance of materials does.
     */
    float get_damage_mod_val( const damage_type_id &dt, const item &it ) const;
    bool has_mod_type( const clothing_mod_type &type ) const;

    clothing_mod_id id;
    std::vector<std::pair<clothing_mod_id, mod_id>> src;
    bool was_loaded = false;

    flag_id flag;
    itype_id item_string;
    translation implement_prompt;
    translation destroy_prompt;
    std::vector< mod_value > mod_values;
    bool restricted = false;

    static size_t count();
};

namespace clothing_mods
{

void load( const JsonObject &jo, const std::string &src );
void reset();

const std::vector<clothing_mod> &get_all();
const std::vector<clothing_mod> &get_all_with( clothing_mod_type type );

} // namespace clothing_mods

#endif // CATA_SRC_CLOTHING_MOD_H

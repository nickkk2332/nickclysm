#pragma once
#ifndef CATA_SRC_MAGIC_TYPE_H
#define CATA_SRC_MAGIC_TYPE_H\


#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "color.h"
#include "dialogue_helpers.h"
#include "type_id.h"

class JsonObject;
class JsonOut;
template <typename T> struct enum_traits;

enum class magic_energy_type : int {
    hp,
    mana,
    stamina,
    bionic,
    vitamin,
    none,
    last
};

template<>
struct enum_traits<magic_energy_type> {
    static constexpr magic_energy_type last = magic_energy_type::last;
};

// MAGIC-J09: how the caster's encumbrance raises a spell's cost, casting time and failure chance.
// Encumbrance is the average of the limbs of a type; only the points above the free ones count.
struct spell_encumbrance {
    // energy cost: hands, for spells without NO_HANDS
    int hands_free = 5;
    // per point; by default 10, 1 for an hp and 100 for a stamina energy source
    std::optional<int> hands_cost;
    // casting time in moves per point: legs, for spells without NO_LEGS, and arms, for SOMATIC
    // spells
    int legs_free = 10;
    int legs_time = 3;
    int arms_free = 10;
    int arms_time = 2;
    // failure chance in percent per point: arms for SOMATIC and mouth for VERBAL spells
    float arms_fail = 0.5f;
    int mouth_free = 5;
    float mouth_fail = 1.0f;

    void load( const JsonObject &jo );
    int cost_per_hand_point( magic_energy_type source ) const;
};

class magic_type
{
    public:

        magic_type() = default;

        bool was_loaded = false;

        magic_type_id id;

        mod_id src_mod;

        static void load_magic_type( const JsonObject &jo, const std::string &src );
        static void finalize_all();
        void load( const JsonObject &jo, std::string_view src );
        void serialize( JsonOut &json ) const;

        // these two formulas should be the inverse of eachother.  The spell xp will break if this is not the case.
        std::optional<jmath_func_id> get_level_formula_id;
        std::optional<jmath_func_id> exp_for_level_formula_id;

        std::optional<jmath_func_id> casting_xp_formula_id;
        std::optional<jmath_func_id> failure_chance_formula_id;
        std::optional<magic_energy_type> energy_source;
        std::optional<vitamin_id> vitamin_energy_source_; // NOLINT(cata-serialize)
        std::optional<nc_color> energy_color_; // NOLINT(cata-serialize)
        std::set<std::string> cannot_cast_flags; // string flags
        std::optional<std::string> cannot_cast_message;
        std::optional<int> max_book_level;
        dbl_or_var failure_cost_percent;
        dbl_or_var failure_exp_percent;
        std::vector<effect_on_condition_id> failure_eocs;
        spell_encumbrance encumbrance; // NOLINT(cata-serialize)

        static const std::vector<magic_type> &get_all();
        static void check_consistency();
        static void reset_all();
    private:

};

#endif // CATA_SRC_MAGIC_TYPE_H

#pragma once
#ifndef CATA_SRC_SHEARING_H
#define CATA_SRC_SHEARING_H

#include <optional>
#include <set>
#include <vector>

#include "calendar.h"
#include "type_id.h"

class JsonObject;
class JsonValue;
class monster;

struct shearing_roll {
    itype_id result;
    int amount = 0;
};

struct shearing_entry {
    itype_id result;
    int amount          = 0;
    int random_min      = 0;
    int random_max      = 0;
    float ratio_mass    = 0.0f;
    float ratio_volume  = 0.0f;

    shearing_roll roll( const monster &mon ) const;

    bool was_loaded = false;
    void load( const JsonObject &jo );
    void deserialize( const JsonObject &jo ) {
        load( jo );
    }
};

class shearing_data
{
    public:
        shearing_data() = default;
        explicit shearing_data( std::vector<shearing_entry> &shearing_entries );

        bool valid() const {
            return valid_;
        }

        const std::vector<shearing_entry> &entries() const {
            return entries_;
        }

        std::vector<shearing_roll> roll_all( const monster &mon ) const;

        // Can the animal be sheared in this season?
        bool in_season( season_type season ) const {
            return seasons_.count( season ) > 0;
        }
        // How long until it can be sheared again: one season unless "regrow" says otherwise.
        time_duration regrow() const {
            return regrow_ ? *regrow_ : calendar::season_length();
        }
        // The SHEAR quality level needed.
        int quality_level() const {
            return quality_level_;
        }

        // An array of entries, or an object with "entries" and the rules above
        void deserialize( const JsonValue &jv );

    private:
        bool valid_ = false;
        std::vector<shearing_entry> entries_;
        std::set<season_type> seasons_ = { SPRING, SUMMER, AUTUMN };
        std::optional<time_duration> regrow_;
        int quality_level_ = 1;
};

#endif // CATA_SRC_SHEARING_H

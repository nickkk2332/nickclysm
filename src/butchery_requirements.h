#pragma once
#ifndef CATA_SRC_BUTCHERY_REQUIREMENTS_H
#define CATA_SRC_BUTCHERY_REQUIREMENTS_H

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "type_id.h"

class JsonObject;
class Character;
class temp_crafting_inventory;

enum class butcher_type : int;
enum class creature_size : int;

/**
 * Contains several requirements, each with a speed modifier.
 * The goal is to iterate from highest speed modifier to lowest
 * to find the first requirement satisfied to butcher a corpse.
 */
class butchery_requirements
{
    public:
        bool was_loaded = false;
        string_id<butchery_requirements> id;
        std::vector<std::pair<string_id<butchery_requirements>, mod_id>> src;

        // Returns the first requirement, by speed multiplier, that the actor can meet, else the one
        // with the highest multiplier.  If there is none for this size and butcher type (an unknown
        // id or broken data), shows a debugmsg and returns a null requirement with multiplier 1.
        std::pair<float, requirement_id> get_fastest_requirements(
            const Character *actor, const temp_crafting_inventory &crafting_inv, creature_size size,
            butcher_type butcher ) const;

        static void load_butchery_req( const JsonObject &jo, const std::string &src );
        static void finalize_all();
        void load( const JsonObject &jo, std::string_view );
        static const std::vector<butchery_requirements> &get_all();
        static void check_consistency();
        static void reset();
        bool is_valid() const;
    private:
        // int is speed bonus
        std::map<float, std::map<creature_size, std::map<butcher_type, requirement_id>>> requirements;
};

#endif // CATA_SRC_BUTCHERY_REQUIREMENTS_H

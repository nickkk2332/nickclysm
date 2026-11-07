#pragma once
#ifndef CATA_SRC_CLIMBING_H
#define CATA_SRC_CLIMBING_H

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "coordinates.h"
#include "point.h"
#include "translation.h"
#include "type_id.h"

class Character;
class JsonObject;

/**
* A "Climbing Aid" as defined here is any one trait, mutation, tool, furniture terrain, technique
* or any other affordance that facilitates movement up, down or across the environment.
* Availability of climbing aids is subject to various conditions; exactly one is used at a time.
*/

class climbing_aid
{
    public:
        enum class category {
            special = 0,
            ter_furn,
            veh,
            item,
            character,
            trait,
            last
        };

        struct condition {
            category    cat; // Called "type" in json
            std::string flag;
            int         uses_item = 0;
            int         range = 1;

            std::string category_string() const noexcept;

            void deserialize( const JsonObject &jo );
        };

        struct climb_cost {
            int pain = 0;
            int damage = 0;
            int kcal = 0;
            int thirst = 0;

            void deserialize( const JsonObject &jo );
            // the climber pays it: damage to the torso, pain, kcal and thirst
            void apply( Character &you ) const;
        };

        static void load_climbing_aid( const JsonObject &jo, const std::string &src );
        static void finalize_all();
        static void check_consistency();
        static void reset();
        void load( const JsonObject &jo, std::string_view );
        void finalize();


        using lookup = std::vector<std::unordered_multimap<std::string, const climbing_aid *> >;
        using condition_list = std::vector<condition>;

        using aid_list = std::vector<const climbing_aid *>;


        static const std::vector<climbing_aid> &get_all();
        // static void check_climbing_aid_consistency(); TODO
        bool was_loaded = false;


        static condition_list detect_conditions( Character &you, const tripoint_bub_ms &examp );

        static aid_list list( const condition_list &cond );
        static aid_list list_all( const condition_list &cond );

        static const climbing_aid &get_default();
        static const climbing_aid &get_safest( const condition_list &cond, bool no_deploy = true );
        /**
         * The safest aid with down.sheer_drop that the character brings (an ability, trait or item),
         * for stairs that end in a sheer drop; nullptr if there is none.
         */
        static const climbing_aid *get_sheer_drop_aid( Character &you );
        // The safest aid with an "up" block among those the conditions allow, or nullptr (CHAR-J11)
        static const climbing_aid *get_safest_up( const condition_list &cond );


        /**
        * Scans downwards from a tile to assess what lies between it and solid ground.
        */
        class fall_scan
        {
            public:
                explicit fall_scan( const tripoint_bub_ms &examp );

                tripoint_bub_ms examp; // Initial position of scan (usually a ledge / open air tile)
                int height; // Z-levels to "ground" based on here.valid_move
                int height_until_creature; // Z-levels below that are free of creatures
                int height_until_furniture; // Z-levels below that are free of furniture
                int height_until_vehicle; // Z-levels below that are free of furniture

                // fall_scan_t is "truthy" if falling is possible at all.
                explicit operator bool() const {
                    return height != 0;
                }

                tripoint_bub_ms pos_top() const {
                    return examp;
                }
                tripoint_bub_ms pos_bottom() const {
                    tripoint_bub_ms ret = examp;
                    ret.z() -= height;
                    return ret;
                }
                tripoint_bub_ms pos_just_below() const {
                    tripoint_bub_ms ret = examp;
                    ret.z() -= 1;
                    return ret;
                }
                tripoint_bub_ms pos_furniture_or_floor() const {
                    tripoint_bub_ms ret = examp;
                    ret.z() -= std::min( height, height_until_furniture + 1 );
                    return ret;
                }

                bool furn_just_below() const {
                    return height > 0 && height_until_furniture == 0;
                }
                bool furn_below() const {
                    return height > 0 && height_until_furniture != height;
                }
                bool veh_just_below() const {
                    return height > 0 && height_until_vehicle == 0;
                }
                bool veh_below() const {
                    return height > 0 && height_until_vehicle != height;
                }
                bool crea_just_below() const {
                    return height > 0 && height_until_creature == 0;
                }
                bool crea_below() const {
                    return height > 0 && height_until_creature != height;
                }
        };


        // Identity of the climbing aid, and index in get_all() list.
        climbing_aid_id id;
        std::vector<std::pair<climbing_aid_id, mod_id>> src;

        // Every climbing aid is based on one primary requirement without which it can't be used.
        condition base_condition;


        int slip_chance_mod = 0;

        // Rule for using this aid to climb down.
        struct down_t {
            bool was_loaded = false;

            // Set max_height to 0 to disallow climbing down with this aid.
            int         max_height = 1;

            // Set to number of Z-levels that will be trivial to climb back up.
            //    Usually set to zero or equal to max_height.
            int         easy_climb_back_up = 0;

            // Whether this aid can be used to climb down partway to ground.  Default true.
            //   Note that climbing down partway usually means the player will fall...
            bool        allow_remaining_height = true;

            // Whether this aid is offered when stairs lead down to a sheer drop.  Default false.
            bool        sheer_drop = false;

            // Menu text.  Menus normally display all deploy aids and the safest non-deploy aid.
            translation menu_text;
            translation menu_cant;
            int         menu_hotkey = 0;

            translation confirm_text;

            translation msg_before;
            translation msg_after;

            // Physical cost of using the aid and furniture created (these often go together).
            climb_cost  cost;
            furn_str_id deploy_furn;

            // CHAR-J11: 0 disables it, as documented
            bool enabled() const noexcept {
                return max_height > 0;
            }

            bool deploys_furniture() const noexcept {
                return !deploy_furn.is_empty();
            }

            void deserialize( const JsonObject &jo );
        } down;

        // Rule for using this aid to climb up a Z-level (a wall, furniture, a ledge above), where
        // there are no stairs or ladder (CHAR-J11).  An aid without one isn't used to climb up.
        struct up_t {
            bool was_loaded = false;

            // Moves added to the climbing cost of the terrain; twice this when nothing braces you
            int         move_cost = 500;
            // false: the aid alone lets you climb, with no walls or furniture to brace against
            bool        needs_support = true;
            climb_cost  cost;
            translation msg_after;

            bool enabled() const noexcept {
                return was_loaded;
            }

            void deserialize( const JsonObject &jo );
        } up;

};
#endif // CATA_SRC_CLIMBING_H


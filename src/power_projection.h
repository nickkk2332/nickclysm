#pragma once
#ifndef CATA_SRC_POWER_PROJECTION_H
#define CATA_SRC_POWER_PROJECTION_H

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "calendar.h"
#include "coordinates.h"
#include "creature_ref.h"
#include "damage.h"
#include "power.h"

class Creature;
class JsonObject;
class JsonOut;
struct power_projection;
using power_projection_id = string_id<power_projection>;

/** Saved power objects; these do not replace instantaneous firearm ballistics. */
struct power_projection {
    power_projection_id id;
    std::vector<std::pair<power_projection_id, mod_id>> src;
    bool was_loaded = false;
    translation name;
    int revision = 1;
    std::string kind = "projectile";
    int range = 10;
    int radius = 1;
    int speed = 1;
    int strength = 10;
    int duration_ticks = 30;
    int charge_ticks = 0;
    int interval_ticks = 1;
    int max_per_owner = 8;
    bool track_target = false;
    bool follow_owner = false;
    bool intercepts = false;
    bool contest = true;
    bool line_of_sight = true;
    bool interrupt_damage = false;
    bool interrupt_displacement = false;
    bool interrupt_incapacitated = false;
    std::string affects = "hostile";
    bodypart_str_id bodypart = bodypart_str_id( "torso" );
    damage_instance damage;
    power_cost start_cost;
    power_cost upkeep;
    std::set<std::string> tags;
    std::vector<effect_on_condition_id> on_start;
    std::vector<effect_on_condition_id> on_release;
    std::vector<effect_on_condition_id> on_enter;
    std::vector<effect_on_condition_id> on_stay;
    std::vector<effect_on_condition_id> on_exit;
    std::vector<effect_on_condition_id> on_hit;
    std::vector<effect_on_condition_id> on_clash;
    std::vector<effect_on_condition_id> on_end;
    global_variables::impl_t locals;
    void load( const JsonObject &jo, std::string_view source );
    void check() const;
};
namespace power_projections
{
void load( const JsonObject &jo, const std::string &source );
void reset();
void finalize();
void check();
}

struct power_projection_request {
    power_projection_id definition;
    creature_ref owner;
    creature_ref target;
    std::optional<tripoint_abs_ms> point;
    std::optional<int> strength;
    int64_t application = 0;
    global_variables::impl_t context;
};
struct power_projection_admission {
    int64_t id = 0;
    std::string reason;
};
struct power_projection_view {
    int64_t id = 0;
    int64_t root = 0;
    int64_t parent = 0;
    int depth = 0;
    int64_t application = 0;
    power_projection_id definition;
    creature_ref owner;
    creature_ref target;
    std::string kind;
    std::string state = "pending";
    std::string reason;
    tripoint_abs_ms origin;
    tripoint_abs_ms position;
    tripoint_abs_ms selected;
    time_point started = calendar::before_time_starts;
    time_point release = calendar::before_time_starts;
    time_point expires = calendar::before_time_starts;
    time_point next_pulse = calendar::before_time_starts;
    int initial_strength = 0;
    int strength = 0;
    int travelled = 0;
    int64_t tick_count = 0;
    int64_t paid_start = 0;
    int64_t paid_upkeep = 0;
    int64_t hp_loss = 0;
    bool members_truncated = false;
    std::vector<creature_ref> members;
    std::vector<tripoint_abs_ms> cells;
    global_variables::impl_t context;
};
struct power_projection_contact {
    int64_t projection = 0;
    int64_t other = 0;
    creature_ref target;
    tripoint_abs_ms position;
    std::string reason;
    int before = 0;
    int after = 0;
    int hp_loss = 0;
};
struct power_projection_tile {
    tripoint_abs_ms position;
    std::string kind;
    bool charging = false;
};

class power_projection_manager
{
    public:
        power_projection_manager();
        ~power_projection_manager();
        power_projection_manager( const power_projection_manager & ) = delete;
        power_projection_manager &operator=( const power_projection_manager & ) = delete;
        std::string check( const power_projection_request &request ) const;
        power_projection_admission create( const power_projection_request &request );
        bool end( int64_t id, const std::string &reason = "dispelled" );
        int dispel( const creature_ref &owner, const power_projection_id &filter = {}, const std::string
                    &tag = "" );
        void drain_starts();
        void process();
        void notify_damage( const Creature &who, int amount );
        void retire_actor( const Creature &who );
        void cancel_all( const std::string &reason );
        void clear();
        bool processing() const;
        double area_value( const Creature &target, const power_projection_id &filter,
                           bool strength = false, const std::optional<creature_ref> &owner = std::nullopt ) const;
        double owned_value( const Creature &owner, const power_projection_id &filter,
                            const std::string &field = "count" ) const;
        std::vector<power_projection_view> inspect() const;
        std::vector<power_projection_contact> contacts() const;
        std::vector<power_projection_tile> tiles() const;
        void serialize( JsonOut &out ) const;
        void deserialize( const JsonObject &jo );
    private:
        struct impl;
        std::unique_ptr<impl> data;
};
power_projection_manager &get_projections();

#endif // CATA_SRC_POWER_PROJECTION_H

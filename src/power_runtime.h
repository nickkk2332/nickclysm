#pragma once
#ifndef CATA_SRC_POWER_RUNTIME_H
#define CATA_SRC_POWER_RUNTIME_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "calendar.h"
#include "coordinates.h"
#include "creature_ref.h"
#include "power.h"

class Creature;
class JsonObject;
class JsonOut;
class power_grant_manager;

struct power_balance {
    std::string reason;
    int total = 0;
    int reserved = 0;
    int capacity = 0;
    int available() const;
};
struct power_charge_view {
    creature_ref actor;
    power_cooldown_id group;
    std::string reason;
    int charges = 0;
    int capacity = 0;
    int cooldown_remaining = 0;
    int recharge_remaining = 0;
};
struct power_pool_view {
    creature_ref actor;
    std::string resource;
    power_balance balance;
};
struct power_reservation_view {
    int64_t token = 0;
    creature_ref actor;
    std::string resource;
    int amount = 0;
    bool follows_avatar = false;
};
struct power_application_view {
    int64_t id = 0;
    int64_t root = 0;
    int64_t parent = 0;
    int depth = 0;
    power_application_id application;
    creature_ref owner;
    creature_ref target;
    std::string state;
    std::string reason;
    double magnitude = 0;
    tripoint_abs_ms origin;
    time_point started = calendar::before_time_starts;
    time_point expires = calendar::before_time_starts;
    time_point next_tick = calendar::before_time_starts;
    int64_t tick_count = 0;
    int64_t paid_start = 0;
    int64_t paid_upkeep = 0;
    global_variables::impl_t context;
};
struct power_application_request {
    power_application_id application;
    creature_ref owner;
    creature_ref target;
    std::optional<double> magnitude;
    global_variables::impl_t context;
};
struct power_application_admission {
    int64_t id = 0;
    std::string reason;
    bool accepted() const;
};
struct power_change {
    time_point when = calendar::before_time_starts;
    creature_ref actor;
    std::string kind;
    std::string name;
    std::string cause;
    int before = 0;
    int after = 0;
    int64_t token = 0;
};

/** Accounting and owned contributions. Queries never materialize a pool or run hooks. */
class power_manager
{
    public:
        power_manager();
        ~power_manager();
        power_manager( const power_manager & ) = delete;
        power_manager &operator=( const power_manager & ) = delete;
        power_balance balance( const Creature &actor, const std::string &resource,
                               int64_t except_reservation = 0 ) const;
        std::string check_cost( const Creature &actor, const std::string &resource, int amount,
                                int64_t reservation = 0 ) const;
        bool reserve( const Creature &actor, const std::string &resource, int amount, int64_t token );
        void release_reservation( int64_t token );
        bool reservation_matches( int64_t token, const creature_ref &actor,
                                  const std::string &resource, int amount ) const;
        bool spend( Creature &actor, const std::string &resource, int amount,
                    const std::string &cause, int64_t reservation = 0 );
        bool commit( Creature &actor, const std::string &resource, int amount, int64_t reservation,
                     const power_cooldown_id &group, int charge_cost, const std::string &cause );
        int modify( Creature &actor, const std::string &resource, int delta, const std::string &cause );
        power_charge_view charges( const Creature &actor, const power_cooldown_id &group ) const;
        std::string check_charge( const Creature &actor, const power_cooldown_id &group, int amount ) const;
        bool use_charge( const Creature &actor, const power_cooldown_id &group, int amount,
                         const std::string &cause );
        int recover_charge( const Creature &actor, const power_cooldown_id &group, int amount );
        std::string check_application( const power_application_request &request ) const;
        power_application_admission apply( const power_application_request &request );
        bool end( int64_t id, const std::string &reason = "dispelled" );
        int dispel( const creature_ref &target, const power_application_id &application,
                    const std::optional<creature_ref> &owner, const std::string &tag = "" );
        double application_value( const Creature &target, const power_application_id &application,
                                  bool magnitude = false, const std::optional<creature_ref> &owner = std::nullopt,
                                  const std::string &tag = "" ) const;
        bool owns_application( int64_t id, const creature_ref &owner ) const;
        power_grant_manager &grants();
        const power_grant_manager &grants() const;
        void notify_damage( const Creature &who, int amount );
        void retire_actor( const Creature &who );
        void drain_starts();
        void process();
        void cancel_all( const std::string &reason );
        void clear();
        bool processing() const;
        std::vector<power_application_view> inspect_applications() const;
        std::vector<power_pool_view> inspect_pools() const;
        std::vector<power_reservation_view> inspect_reservations() const;
        std::vector<power_charge_view> inspect_cooldowns() const;
        std::vector<power_change> history() const;
        void serialize( JsonOut &out ) const;
        void deserialize( const JsonObject &jo );
    private:
        struct impl;
        std::unique_ptr<impl> data;
};

power_manager &get_powers();

#endif // CATA_SRC_POWER_RUNTIME_H

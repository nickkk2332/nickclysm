#pragma once
#ifndef CATA_SRC_POWER_GRANT_H
#define CATA_SRC_POWER_GRANT_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "coords_fwd.h"
#include "creature_ref.h"
#include "power.h"
#include "type_id.h"

class Creature;
class JsonObject;
class JsonOut;
class item;
class map;
struct power_application_view;

struct power_grant_view {
    int64_t application = 0;
    power_application_id definition;
    creature_ref owner;
    creature_ref target;
    std::string state;
    std::string reason;
    bool form = false;
    size_t stowed = 0;
    int effects = 0;
    int equipment = 0;
    int restored = 0;
    int dropped = 0;
    std::vector<std::pair<int64_t, itype_id>> originals;
};
class power_grant_manager
{
    public:
        power_grant_manager();
        ~power_grant_manager();
        power_grant_manager( const power_grant_manager & ) = delete;
        power_grant_manager &operator=( const power_grant_manager & ) = delete;
        std::string check( const power_grant_definition &definition, const Creature &target ) const;
        std::string start( const power_application_view &application );
        void release( int64_t application, const std::string &reason );
        bool contains( int64_t application ) const;
        bool item_active( const item &value ) const;
        void process();
        void retire_actor( const Creature &actor );
        void clear();
        bool processing() const;
        std::vector<power_grant_view> inspect() const;
        void serialize( JsonOut &out ) const;
        void deserialize( const JsonObject &jo );
    private:
        struct impl;

        std::unique_ptr<impl> data;
        friend class power_grant_item_scope;
};

class power_grant_item_scope
{
    public:
        explicit power_grant_item_scope( bool enabled );
        ~power_grant_item_scope();
        power_grant_item_scope( const power_grant_item_scope & ) = delete;
        power_grant_item_scope &operator=( const power_grant_item_scope & ) = delete;
    private:
        power_grant_manager *manager = nullptr;
};
power_grant_manager &get_power_grants();
void expire_power_equipment( item &value, map &here, const tripoint_bub_ms &position );

#endif // CATA_SRC_POWER_GRANT_H

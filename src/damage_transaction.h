#pragma once
#ifndef CATA_SRC_DAMAGE_TRANSACTION_H
#define CATA_SRC_DAMAGE_TRANSACTION_H

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "creature_ref.h"
#include "global_vars.h"
#include "type_id.h"

class Creature;
class JsonObject;
class JsonOut;
struct damage_instance;

struct damage_rule_result {
    std::string rule;
    std::string stage;
    std::string reason;
    int before = 0;
    int after = 0;
    int absorbed = 0;
    int reflected = 0;
    std::map<std::string, int64_t> payments;
};

struct damage_component_view {
    damage_type_id type;
    float amount = 0;
    float armor_penetration = 0;
    float armor_multiplier = 1;
    float damage_multiplier = 1;
    float unconditional_multiplier = 1;
};

struct damage_transaction_view {
    int64_t id = 0;
    int64_t root = 0;
    int64_t parent = 0;
    int depth = 0;
    creature_ref source;
    creature_ref instigator;
    creature_ref original_source;
    creature_ref defender;
    bodypart_str_id bodypart;
    std::string state;
    std::string reason;
    int incoming = 0;
    int after_armor = 0;
    int outgoing = 0;
    int absorbed = 0;
    int reflected = 0;
    int hp_loss = 0;
    bool truncated = false;
    bool armor_evaluated = false;
    bool hp_committed = false;
    std::vector<damage_component_view> incoming_components;
    std::vector<damage_component_view> after_armor_components;
    std::vector<damage_component_view> outgoing_components;
    global_variables::impl_t context;
    std::vector<damage_rule_result> rules;
    std::vector<int64_t> children;
};

class damage_transaction_manager
{
    public:
        damage_transaction_manager();
        ~damage_transaction_manager();
        damage_transaction_manager( const damage_transaction_manager & ) = delete;
        damage_transaction_manager &operator=( const damage_transaction_manager & ) = delete;
        bool processing() const;
        void clear();
        void release_settled_sources();
        std::vector<damage_transaction_view> inspect_all() const;
        void serialize( JsonOut &jsout ) const;
        void deserialize( const JsonObject &jo );
    private:
        friend class damage_packet_scope;
        friend class damage_participant_guard;
        struct impl;
        std::unique_ptr<impl> data;
};

damage_transaction_manager &get_damage_transactions();

/** Explicit command/spell metadata. Empty scopes isolate unrelated callback casts. */
class damage_context_scope
{
    public:
        explicit damage_context_scope( global_variables::impl_t values = {}, bool trace = true,
                                       std::optional<creature_ref> instigator = std::nullopt );
        ~damage_context_scope();
        damage_context_scope( const damage_context_scope & ) = delete;
        damage_context_scope &operator=( const damage_context_scope & ) = delete;
        void set( global_variables::impl_t values, bool trace );
        std::optional<damage_transaction_view> result() const;
    private:
        friend class damage_packet_scope;
        global_variables::impl_t values;
        bool trace;
        std::optional<creature_ref> instigator;
        const damage_context_scope *previous;
        static thread_local const damage_context_scope *current;
        mutable int64_t first_packet = 0;
        mutable std::optional<damage_transaction_view> completed;
};

/** Retains actual participants through Character's post-damage work as well. */
class damage_participant_guard
{
    public:
        damage_participant_guard( Creature *source, Creature &target );
        ~damage_participant_guard();
        bool valid() const;
    private:
        struct impl;
        std::unique_ptr<impl> data;
};

class damage_packet_scope
{
    public:
        damage_packet_scope( Creature *source, Creature &defender, bodypart_id bp,
                             const damage_instance &packet,
                             const std::function<global_variables::impl_t()> &native_facts = {} );
        ~damage_packet_scope();
        damage_packet_scope( const damage_packet_scope & ) = delete;
        damage_packet_scope &operator=( const damage_packet_scope & ) = delete;
        bool enabled() const;
        bool valid();
        bool before_armor( damage_instance &packet );
        bool after_armor( damage_instance &packet );
        bool blocked( const damage_instance &packet ) const;
        void arm_hp_capture();
        void disarm_hp_capture();
        void finish( const damage_instance &packet );
    private:
        friend class damage_participant_guard;
        friend void record_damage_transaction_hp_loss( const Creature &, int );
        static bool interested( const Creature &defender );
        struct impl;
        std::unique_ptr<impl> data;
        static thread_local damage_packet_scope *current;
};

void record_damage_transaction_hp_loss( const Creature &target, int amount );

#endif // CATA_SRC_DAMAGE_TRANSACTION_H

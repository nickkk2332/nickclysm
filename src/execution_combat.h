#pragma once
#ifndef CATA_SRC_EXECUTION_COMBAT_H
#define CATA_SRC_EXECUTION_COMBAT_H

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "coordinates.h"
#include "creature_ref.h"
#include "global_vars.h"
#include "memory_fast.h"

class Creature;
class item;
struct dealt_damage_instance;

namespace execution_combat
{
enum class channel { character_melee, monster_melee };

/** Facts from one native primary contact; absent facts are not fabricated. */
struct attack_snapshot {
    uint64_t id = 0;
    channel producer = channel::character_melee;
    creature_ref actor;
    creature_ref target;
    std::string source_kind;
    std::string target_kind;
    std::optional<std::string> source_monster;
    std::optional<std::string> target_monster;
    std::optional<std::string> weapon;
    std::optional<int64_t> weapon_uid;
    std::set<std::string> weapon_categories;
    std::optional<std::string> weapon_skill;
    std::optional<std::string> style;
    std::optional<std::string> technique;
    bool hit = false;
    std::optional<bool> critical;
    std::optional<std::string> bodypart;
    std::optional<std::string> weakpoint;
    std::optional<int> hp_loss;
    std::map<std::string, int> damage;
    tripoint_abs_ms actor_position;
    tripoint_abs_ms target_position;
    tripoint_rel_ms direction;
    std::optional<std::string> terrain;
    std::optional<std::string> furniture;

    global_variables::impl_t context() const;
};

class melee_hit_scope;
class direct_damage_scope;
class damage_scope;
class primary_hit_scope;

/** Root attacks publish after all native callbacks return. Child attacks are suppressed. */
class attack_scope
{
    public:
        attack_scope( Creature &actor, Creature &target, channel producer );
        ~attack_scope() noexcept( false );
        attack_scope( const attack_scope & ) = delete;
        attack_scope &operator=( const attack_scope & ) = delete;
        void set_weapon( const item *weapon, const std::string &style,
                         const std::string &technique );
        void set_technique( const std::string &style, const std::string &technique );
        void resolve( bool hit, std::optional<bool> critical,
                      const dealt_damage_instance &dealt );
    private:
        friend class melee_hit_scope;
        friend class direct_damage_scope;
        friend class damage_scope;
        friend class primary_hit_scope;
        friend void record_hp_loss( const Creature &, int );
        attack_scope *previous = nullptr;
        bool enabled = false;
        bool resolved = false;
        bool armed = false;
        int exceptions = 0;
        attack_snapshot facts;
        shared_ptr_fast<Creature> source_hold;
        shared_ptr_fast<Creature> target_hold;
};

/** Producers mark their primary hit explicitly; mutation/retaliation hits are not substituted. */
class primary_hit_scope
{
    public:
        explicit primary_hit_scope( attack_scope &contact );
        ~primary_hit_scope();
        primary_hit_scope( const primary_hit_scope & ) = delete;
        primary_hit_scope &operator=( const primary_hit_scope & ) = delete;
    private:
        attack_scope &owner;
        bool previous = false;
};

/** Marks the native hit, excluding defensive callback contacts and redirected riders. */
class melee_hit_scope
{
    public:
        melee_hit_scope( const Creature *actor, const Creature &target );
        ~melee_hit_scope();
        melee_hit_scope( const melee_hit_scope & ) = delete;
        melee_hit_scope &operator=( const melee_hit_scope & ) = delete;
    private:
        friend class direct_damage_scope;
        melee_hit_scope *previous = nullptr;
        attack_scope *owner = nullptr;
};

/** Arms capture only for the direct deal_damage call after defensive callbacks. */
class direct_damage_scope
{
    public:
        direct_damage_scope();
        ~direct_damage_scope();
        direct_damage_scope( const direct_damage_scope & ) = delete;
        direct_damage_scope &operator=( const direct_damage_scope & ) = delete;
    private:
        attack_scope *previous = nullptr;
};

/** Damage nesting is also a barrier for melee spawned by projectile/spell callbacks. */
class damage_scope
{
    public:
        damage_scope( const Creature *source, const Creature &target );
        ~damage_scope();
        damage_scope( const damage_scope & ) = delete;
        damage_scope &operator=( const damage_scope & ) = delete;
        void set_weakpoint( const std::string &id );
        void arm_hp_capture();
        void disarm_hp_capture();
    private:
        friend void record_hp_loss( const Creature &, int );
        damage_scope *previous = nullptr;
        attack_scope *owner = nullptr;
        bool capture_hp = false;
};

class damage_barrier
{
    public:
        damage_barrier();
        ~damage_barrier();
        damage_barrier( const damage_barrier & ) = delete;
        damage_barrier &operator=( const damage_barrier & ) = delete;
};

void record_hp_loss( const Creature &target, int amount );
std::string channel_name( channel producer );
} // namespace execution_combat

#endif // CATA_SRC_EXECUTION_COMBAT_H

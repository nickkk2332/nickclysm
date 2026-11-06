#pragma once
#ifndef CATA_SRC_EXECUTION_COMBAT_H
#define CATA_SRC_EXECUTION_COMBAT_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "coordinates.h"
#include "creature_ref.h"
#include "global_vars.h"
#include "memory_fast.h"

class Creature;
class Character;
class item;
class map;
struct dealt_damage_instance;
struct dealt_projectile_attack;

namespace execution_combat
{
enum class channel { character_melee, monster_melee, projectile };

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
    std::optional<std::string> ammunition;
    std::optional<tripoint_abs_ms> projectile_origin;
    std::optional<std::string> style;
    std::optional<std::string> technique;
    bool hit = false;
    bool fatal = false;
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
class projectile_hit_scope;

/** Only the native flight claimed by an authored fling may attribute its impacts. */
class physical_flight_scope
{
    public:
        physical_flight_scope( const Creature &target, bool owned );
        ~physical_flight_scope();
        physical_flight_scope( const physical_flight_scope & ) = delete;
        physical_flight_scope &operator=( const physical_flight_scope & ) = delete;
    private:
        friend class impact_scope;
        physical_flight_scope *previous = nullptr;
        const Creature &target;
};

/** Native impact calls only: nested scripted damage keeps its explicit source semantics. */
class impact_scope
{
    public:
        explicit impact_scope( const Creature &target );
        ~impact_scope();
        impact_scope( const impact_scope & ) = delete;
        impact_scope &operator=( const impact_scope & ) = delete;
    private:
        friend bool is_impact_damage( const Creature *target );
        impact_scope *previous = nullptr;
        const Creature &target;
        damage_scope *baseline_damage = nullptr;
        int baseline_depth = 0;
};

/** One handheld firing action: first direct contact per victim, published after the burst. */
class projectile_fire_scope
{
    public:
        projectile_fire_scope( Character &actor, map &here, const item &gun );
        ~projectile_fire_scope() noexcept( false );
        projectile_fire_scope( const projectile_fire_scope & ) = delete;
        projectile_fire_scope &operator=( const projectile_fire_scope & ) = delete;
        void begin_round( const item &gun, const dealt_projectile_attack &attack );
    private:
        friend class attack_scope;
        friend class projectile_flight_scope;
        friend class projectile_hit_scope;
        struct impl;
        std::unique_ptr<impl> data;
        projectile_fire_scope *previous = nullptr;
        int exceptions = 0;
};

/** Only the outer native flight belongs to a firing action; bounces/children do not. */
class projectile_flight_scope
{
    public:
        projectile_flight_scope( const dealt_projectile_attack &attack, map *here,
                                 Creature *source, const tripoint_bub_ms &origin );
        ~projectile_flight_scope();
        projectile_flight_scope( const projectile_flight_scope & ) = delete;
        projectile_flight_scope &operator=( const projectile_flight_scope & ) = delete;
    private:
        friend class projectile_hit_scope;
        projectile_flight_scope *previous = nullptr;
        projectile_fire_scope *owner = nullptr;
        const dealt_projectile_attack *attack = nullptr;
        map *here = nullptr;
        tripoint_abs_ms origin;
};

/** Native ballistics grants one primary hit to this exact target, before virtual dispatch. */
class projectile_target_scope
{
    public:
        explicit projectile_target_scope( const Creature &target );
        ~projectile_target_scope();
        projectile_target_scope( const projectile_target_scope & ) = delete;
        projectile_target_scope &operator=( const projectile_target_scope & ) = delete;
    private:
        friend class projectile_hit_scope;
        projectile_target_scope *previous = nullptr;
        const Creature &target;
        bool consumed = false;
};

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
        void set_critical( bool critical );
        void resolve( bool hit, std::optional<bool> critical,
                      const dealt_damage_instance &dealt );
    private:
        friend class melee_hit_scope;
        friend class direct_damage_scope;
        friend class damage_scope;
        friend class primary_hit_scope;
        friend class projectile_hit_scope;
        friend void record_hp_loss( const Creature &, int );
        friend bool is_impact_damage( const Creature *target );
        attack_scope( Creature &actor, Creature &target, channel producer, bool projectile_primary );
        void reserve_fatal( const Creature &target );
        attack_scope *previous = nullptr;
        bool enabled = false;
        bool resolved = false;
        bool armed = false;
        int64_t fatal_instance = 0;
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
        friend class projectile_hit_scope;
        explicit direct_damage_scope( attack_scope *owner );
        attack_scope *previous = nullptr;
};

/** Captures the selected native projectile hit, excluding its callbacks and effects. */
class projectile_hit_scope
{
    public:
        projectile_hit_scope( Creature &target, Creature *source,
                              const dealt_projectile_attack &attack, map *here, bool critical );
        ~projectile_hit_scope() noexcept( false );
        projectile_hit_scope( const projectile_hit_scope & ) = delete;
        projectile_hit_scope &operator=( const projectile_hit_scope & ) = delete;
        direct_damage_scope capture_damage();
        void resolve( const dealt_damage_instance &dealt );
    private:
        std::unique_ptr<attack_scope> contact;
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
        void arm_hp_capture( bodypart_id bp, const dealt_damage_instance &dealt );
        void disarm_hp_capture();
    private:
        friend void record_hp_loss( const Creature &, int );
        friend bool is_impact_damage( const Creature *target );
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
bool attack_in_progress();
bool at_fatal_commit( const Creature &target );
bool is_impact_damage( const Creature *target );
std::string channel_name( channel producer );
} // namespace execution_combat

#endif // CATA_SRC_EXECUTION_COMBAT_H

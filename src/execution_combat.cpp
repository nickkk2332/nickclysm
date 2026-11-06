#include "execution_combat.h"
#include "power_motion.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_scope_helpers.h"
#include "creature.h"
#include "damage.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "line.h"
#include "map.h"
#include "mapdata.h"
#include "magic.h"
#include "monster.h"
#include "mtype.h"
#include "projectile.h"
#include "skill.h"

namespace execution_combat
{
namespace
{
thread_local attack_scope *current_attack = nullptr;
thread_local melee_hit_scope *current_hit = nullptr;
thread_local attack_scope *pending_damage = nullptr;
thread_local damage_scope *current_damage = nullptr;
thread_local int damage_depth = 0;
thread_local const Creature *fatal_commit_target = nullptr;
thread_local projectile_fire_scope *current_fire = nullptr;
thread_local projectile_flight_scope *current_flight = nullptr;
thread_local projectile_target_scope *current_projectile_target = nullptr;
thread_local physical_flight_scope *current_physical_flight = nullptr;
thread_local impact_scope *current_impact = nullptr;
thread_local effect_target_scope *current_effect_target = nullptr;

bool action_channel( channel producer )
{
    return producer == channel::projectile || producer == channel::thrown ||
           producer == channel::spell || producer == channel::monster_special;
}

std::string kind( const Creature &who )
{
    return who.is_avatar() ? "avatar" : who.is_npc() ? "npc" : "monster";
}

void capture_location( attack_snapshot &s, const Creature &actor, const Creature &target )
{
    s.actor_position = actor.pos_abs();
    s.target_position = target.pos_abs();
    const tripoint_abs_ms origin = s.projectile_origin.value_or( s.actor_position );
    s.direction = tripoint_rel_ms( sgn( s.target_position.x() - origin.x() ),
                                   sgn( s.target_position.y() - origin.y() ), 0 );
    const map &here = get_map();
    if( here.inbounds( target.pos_bub( here ) ) ) {
        s.terrain = here.ter( target.pos_bub( here ) ).id().str();
        s.furniture = here.furn( target.pos_bub( here ) ).id().str();
    }
}
} // namespace

physical_flight_scope::physical_flight_scope( const Creature &target, bool owned ) :
    previous( current_physical_flight ), target( target )
{
    current_physical_flight = owned ? this : nullptr;
}

physical_flight_scope::~physical_flight_scope()
{
    current_physical_flight = previous;
}

impact_scope::impact_scope( const Creature &target ) :
    previous( current_impact ), target( target ), baseline_damage( current_damage ),
    baseline_depth( damage_depth )
{
    current_impact = !previous && ( ( current_physical_flight &&
                                      &current_physical_flight->target == &target ) || power_motion::active( target ) ) ? this : nullptr;
}

impact_scope::~impact_scope()
{
    current_impact = previous;
}

bool is_impact_damage( const Creature *target )
{
    if( !current_impact || &current_impact->target != target ) {
        return false;
    }
    if( current_damage == current_impact->baseline_damage ) {
        return damage_depth == current_impact->baseline_depth + 1;
    }
    // Character::deal_damage has an additional outer barrier before the common
    // Creature damage scope. Attribute at that scope's entry, before callbacks;
    // nested damage has a different parent scope and cannot borrow this source.
    const int entry_depth = current_impact->baseline_depth + ( target->as_character() ? 2 : 1 );
    return current_damage && current_damage->previous == current_impact->baseline_damage &&
           ( damage_depth == entry_depth ||
             ( damage_depth == entry_depth + 1 && current_damage->capture_hp ) );
}

std::string channel_name( channel producer )
{
    switch( producer ) {
        case channel::character_melee:
            return "character_melee";
        case channel::monster_melee:
            return "monster_melee";
        case channel::projectile:
            return "projectile";
        case channel::thrown:
            return "thrown";
        case channel::spell:
            return "spell";
        case channel::monster_special:
            return "monster_special";
        case channel::count:
            break;
    }
    return "";
}

struct projectile_fire_scope::impl {
    struct held_contact {
        creature_ref identity;
        attack_snapshot facts;
        shared_ptr_fast<Creature> target;
        bool fatal_attempted = false;
        bool fatal_arbitrated = false;
        int64_t fatal_instance = 0;
        bool fatal_resolved = false;
    };
    shared_ptr_fast<Creature> actor;
    creature_ref identity;
    channel producer = channel::projectile;
    const spell *expected_effect = nullptr;
    bool effect_claimed = false;
    bool effect_active = false;
    const dealt_projectile_attack *expected_flight = nullptr;
    attack_snapshot round;
    std::vector<held_contact> contacts;
};

projectile_fire_scope::projectile_fire_scope( Creature &actor, map &here, channel producer ) :
    previous( current_fire ), exceptions( std::uncaught_exceptions() )
{
    current_fire = this;
    const on_out_of_scope restore_on_exception( [&]() {
        if( std::uncaught_exceptions() > exceptions ) {
            current_fire = previous;
        }
    } );
    if( previous || current_flight || current_attack || damage_depth != 0 || !g ||
        &here != &get_map() || g->executions().processing_actions() ||
        !execution_triggers::has_rules( producer ) || actor.is_fake() ||
        actor.is_hallucination() ||
        creature_ref::bind( actor ).resolve() != &actor ) {
        return;
    }
    const auto source = g->shared_from( actor );
    if( source ) {
        data = std::make_unique<impl>();
        data->actor = source;
        data->identity = creature_ref::bind( actor );
        data->producer = producer;
        data->round.producer = producer;
    }
}

projectile_fire_scope::projectile_fire_scope( Character &actor, map &here, const item &gun,
        channel producer ) : projectile_fire_scope( static_cast<Creature &>( actor ), here, producer )
{
    if( actor.is_mounted() || ( producer != channel::projectile && producer != channel::thrown ) ||
        ( producer == channel::projectile && actor.get_wielded_item().get_item() != &gun ) ) {
        data.reset();
    }
}

effect_scope::effect_scope( Creature &source, const spell &effect, bool direct_effect )
{
    if( direct_effect && current_fire && current_fire->data &&
        current_fire->data->expected_effect == &effect && !current_fire->data->effect_claimed &&
        current_fire->data->actor.get() == &source ) {
        owner = current_fire;
    } else {
        action.reset( new projectile_fire_scope( source, get_map(), channel::spell ) );
        owner = action.get();
        if( owner->data ) {
            owner->data->expected_effect = &effect;
            owner->data->round.spell_id = effect.id().str();
            owner->data->round.spell_level = effect.get_effective_level();
        }
    }
    if( direct_effect && owner->data ) {
        owner->data->effect_claimed = true;
        owner->data->effect_active = true;
    }
}

effect_scope::effect_scope( Creature &source, const std::string &monster_attack )
{
    action.reset( new projectile_fire_scope( source, get_map(), channel::monster_special ) );
    owner = action.get();
    if( owner->data ) {
        owner->data->round.monster_attack = monster_attack;
        owner->data->effect_active = true;
    }
}

effect_scope::~effect_scope() noexcept( false )
{
    if( owner && owner->data ) {
        owner->data->effect_active = false;
    }
    action = nullptr;
}

effect_target_scope::effect_target_scope( const Creature &target,
        const dealt_projectile_attack &attack, const spell &effect ) :
    previous( current_effect_target ), target( target ), attack( attack ), effect( effect )
{
    current_effect_target = this;
}

effect_target_scope::~effect_target_scope()
{
    current_effect_target = previous;
}

projectile_fire_scope::~projectile_fire_scope() noexcept( false )
{
    current_fire = previous;
    if( !data ) {
        return;
    }
    const bool completed = exceptions == 0 && std::uncaught_exceptions() == 0;
    // A fatal owner remains unready until all rounds, callbacks, ammo use and recoil finish.
    // Exceptional exits must settle injuries too, without running graph actions or UI.
    for( const impl::held_contact &contact : data->contacts ) {
        if( contact.fatal_instance ) {
            g->executions().release_fatal_attack( contact.fatal_instance,
                                                  completed && contact.fatal_resolved );
        }
    }
    if( !completed ) {
        return;
    }
    // Retain all original bodies through native ranged events and rule callbacks.
    for( const impl::held_contact &contact : data->contacts ) {
        if( !contact.fatal_arbitrated && contact.facts.id != 0 && !contact.facts.fatal ) {
            g->executions().on_attack_resolved( contact.facts );
        }
    }
}

void projectile_fire_scope::begin_round( const item &gun, const dealt_projectile_attack &attack )
{
    if( !data ) {
        return;
    }
    data->round = attack_snapshot();
    data->round.producer = data->producer;
    data->expected_flight = &attack;
    data->round.weapon = gun.typeId().str();
    data->round.weapon_uid = gun.uid().get_value();
    data->round.weapon_skill = data->producer == channel::thrown ? "throw" : gun.gun_skill().str();
    if( data->producer == channel::projectile && !gun.ammo_current().is_null() ) {
        data->round.ammunition = gun.ammo_current().str();
    }
    for( const weapon_category_id &category : gun.type->weapon_category ) {
        data->round.weapon_categories.insert( category.str() );
    }
}

projectile_flight_scope::projectile_flight_scope( const dealt_projectile_attack &attack,
        map *here, Creature *source, const tripoint_bub_ms &origin ) :
    previous( current_flight ), attack( &attack ), here( here )
{
    current_flight = this;
    if( !previous && !current_attack && damage_depth == 0 && current_fire &&
        current_fire->data && current_fire->data->actor.get() == source &&
        current_fire->data->identity.resolve() == source &&
        current_fire->data->expected_flight == &attack && here == &get_map() ) {
        owner = current_fire;
        owner->data->expected_flight = nullptr;
        this->origin = here->get_abs( origin );
    }
}

projectile_flight_scope::~projectile_flight_scope()
{
    current_flight = previous;
}

projectile_target_scope::projectile_target_scope( const Creature &target ) :
    previous( current_projectile_target ), target( target )
{
    current_projectile_target = this;
}

projectile_target_scope::~projectile_target_scope()
{
    current_projectile_target = previous;
}

global_variables::impl_t attack_snapshot::context() const
{
    global_variables::impl_t out;
    const auto put = [&]( const std::string & key, const auto & value ) {
        out[key] = diag_value{ value };
    };
    put( "attack_contact_id", std::to_string( id ) );
    put( "attack_channel", channel_name( producer ) );
    put( "attack_event", fatal ? "fatal_hit" : "attack_resolved" );
    put( "attack_fatal", fatal ? 1.0 : 0.0 );
    put( "attack_source_kind", source_kind );
    put( "attack_target_kind", target_kind );
    put( "attack_actor_position", actor_position );
    put( "attack_target_position", target_position );
    put( "hit", hit ? 1.0 : 0.0 );
    if( source_monster ) {
        put( "attack_source_monster", *source_monster );
    }
    if( target_monster ) {
        put( "attack_target_monster", *target_monster );
    }
    if( weapon ) {
        put( "weapon", *weapon );
    }
    if( weapon_uid ) {
        put( "weapon_uid", std::to_string( *weapon_uid ) );
    }
    if( weapon_skill ) {
        put( "weapon_skill", *weapon_skill );
    }
    if( ammunition ) {
        put( "ammunition", *ammunition );
    }
    if( spell_id ) {
        put( "spell", *spell_id );
    }
    if( spell_level ) {
        put( "spell_level", *spell_level );
    }
    if( monster_attack ) {
        put( "monster_attack", *monster_attack );
    }
    if( projectile_origin ) {
        put( "projectile_origin", *projectile_origin );
    }
    if( style ) {
        put( "style", *style );
    }
    if( technique ) {
        put( "technique", *technique );
    }
    if( critical ) {
        put( "crit", *critical ? 1.0 : 0.0 );
    }
    if( bodypart ) {
        put( "bp", *bodypart );
    }
    if( weakpoint ) {
        put( "weakpoint", *weakpoint );
    }
    if( hp_loss ) {
        put( "hp_loss", *hp_loss );
        double total = 0;
        for( const auto &entry : damage ) {
            put( "damage_" + entry.first, entry.second );
            total += entry.second;
        }
        put( "damage", total );
    }
    if( terrain ) {
        put( "attack_terrain", *terrain );
    }
    if( furniture ) {
        put( "attack_furniture", *furniture );
    }
    return out;
}

attack_scope::attack_scope( Creature &actor, Creature &target, channel producer ) :
    attack_scope( actor, target, producer, false )
{
}

attack_scope::attack_scope( Creature &actor, Creature &target, channel producer,
                            bool projectile_primary ) :
    previous( current_attack ), exceptions( std::uncaught_exceptions() )
{
    current_attack = this;
    const on_out_of_scope restore_on_exception( [&]() {
        if( std::uncaught_exceptions() > exceptions ) {
            current_attack = previous;
        }
    } );
    if( previous || ( !projectile_primary && ( damage_depth != 0 || current_fire || current_flight ||
                      action_channel( producer ) ) ) || !g || g->executions().processing_actions() ||
        !execution_triggers::has_rules( producer ) || actor.is_hallucination() ||
        target.is_hallucination() ) {
        return;
    }
    facts.actor = creature_ref::bind( actor );
    facts.target = creature_ref::bind( target );
    if( facts.actor.resolve() != &actor || facts.target.resolve() != &target ) {
        return;
    }
    source_hold = g->shared_from( actor );
    target_hold = g->shared_from( target );
    if( !source_hold || !target_hold ) {
        return;
    }
    enabled = true;
    facts.id = g->executions().next_attack_id();
    facts.producer = producer;
    facts.source_kind = kind( actor );
    facts.target_kind = kind( target );
    if( const monster *mon = actor.as_monster() ) {
        facts.source_monster = mon->type->id.str();
    }
    if( const monster *mon = target.as_monster() ) {
        facts.target_monster = mon->type->id.str();
    }
    capture_location( facts, actor, target );
}

attack_scope::~attack_scope() noexcept( false )
{
    current_attack = previous;
    if( action_channel( facts.producer ) ) {
        if( current_fire && current_fire->data ) {
            for( auto &contact : current_fire->data->contacts ) {
                if( contact.identity == facts.target ) {
                    const bool completed = resolved && exceptions == 0 && std::uncaught_exceptions() == 0;
                    if( fatal_instance ) {
                        contact.fatal_resolved = completed;
                        if( !completed ) {
                            g->executions().release_fatal_attack( fatal_instance, false );
                        }
                    }
                    if( enabled && completed && contact.facts.id == 0 ) {
                        contact.facts = facts;
                    }
                    break;
                }
            }
        }
        return;
    }
    if( fatal_instance != 0 && g ) {
        g->executions().release_fatal_attack( fatal_instance,
                                              resolved && exceptions == 0 && std::uncaught_exceptions() == 0 );
        return;
    }
    if( enabled && resolved && exceptions == 0 && std::uncaught_exceptions() == 0 ) {
        g->executions().on_attack_resolved( facts );
    }
}

void attack_scope::set_weapon( const item *weapon, const std::string &style,
                               const std::string &technique )
{
    if( !enabled ) {
        return;
    }
    facts.weapon = weapon ? weapon->typeId().str() : "null";
    facts.weapon_skill = weapon ? weapon->melee_skill().str() : "unarmed";
    if( weapon ) {
        facts.weapon_uid = weapon->uid().get_value();
        for( const weapon_category_id &category : weapon->type->weapon_category ) {
            facts.weapon_categories.insert( category.str() );
        }
    }
    facts.style = style;
    facts.technique = technique;
}

void attack_scope::set_technique( const std::string &style, const std::string &technique )
{
    if( enabled ) {
        facts.style = style;
        facts.technique = technique;
    }
}

void attack_scope::set_critical( bool critical )
{
    if( enabled ) {
        facts.critical = critical;
    }
}

void attack_scope::resolve( bool hit, std::optional<bool> critical,
                            const dealt_damage_instance &dealt )
{
    if( !enabled ) {
        return;
    }
    if( hit && !facts.hp_loss ) {
        // A redirected rider or an unsupported early-return path is not a hit on our bound target.
        enabled = false;
        return;
    }
    resolved = true;
    facts.hit = hit;
    facts.critical = critical;
    if( hit && facts.hp_loss ) {
        for( const auto &entry : dealt.dealt_dams ) {
            facts.damage[entry.first.str()] = std::max( 0, entry.second );
        }
        if( dealt.bp_hit ) {
            facts.bodypart = dealt.bp_hit.id().str();
        }
    }
}

melee_hit_scope::melee_hit_scope( const Creature *actor, const Creature &target ) :
    previous( current_hit )
{
    if( !previous && current_attack && current_attack->enabled && current_attack->armed &&
        !current_attack->resolved &&
        current_attack->source_hold.get() == actor && current_attack->target_hold.get() == &target ) {
        owner = current_attack;
    }
    current_hit = this;
}

melee_hit_scope::~melee_hit_scope()
{
    current_hit = previous;
}

direct_damage_scope::direct_damage_scope() : previous( pending_damage )
{
    pending_damage = current_hit ? current_hit->owner : nullptr;
}

direct_damage_scope::~direct_damage_scope()
{
    pending_damage = previous;
}

direct_damage_scope::direct_damage_scope( attack_scope *owner ) : previous( pending_damage )
{
    pending_damage = owner && owner->enabled && current_attack == owner ? owner : nullptr;
}

projectile_hit_scope::projectile_hit_scope( Creature &target, Creature *source,
        const dealt_projectile_attack &attack, map *here, bool critical )
{
    const bool spell_hit = current_fire && current_fire->data &&
                           current_fire->data->producer == channel::spell && current_fire->data->effect_active &&
                           current_effect_target && !current_effect_target->consumed &&
                           current_fire->data->expected_effect == &current_effect_target->effect &&
                           &current_effect_target->target == &target && &current_effect_target->attack == &attack &&
                           here == &get_map();
    if( !spell_hit && ( !current_flight || !current_flight->owner ||
                        current_flight->owner != current_fire || current_flight->attack != &attack ||
                        current_flight->here != here || current_attack || !current_projectile_target ||
                        current_projectile_target->consumed || &current_projectile_target->target != &target ||
                        current_fire->data->identity.resolve() != source || attack.shrapnel ||
                        attack.proj.proj_effects.count( ammo_effect_str_id( "MAGIC" ) ) ||
                        current_fire->data->actor.get() != source ) ) {
        return;
    }
    contact = attack_scope::action_hit( target, source );
    if( !contact ) {
        return;
    }
    if( spell_hit ) {
        current_effect_target->consumed = true;
    } else {
        current_projectile_target->consumed = true;
        contact->facts.projectile_origin = current_flight->origin;
    }
    contact->set_critical( critical );
    capture_location( contact->facts, *source, target );
}

std::unique_ptr<attack_scope> attack_scope::action_hit( Creature &target, Creature *source )
{
    if( !current_fire || !current_fire->data || current_attack || !source ||
        current_fire->data->identity.resolve() != source || current_fire->data->actor.get() != source ) {
        return nullptr;
    }
    auto &state = *current_fire->data;
    const creature_ref identity = creature_ref::bind( target );
    const auto previous = std::find_if( state.contacts.begin(), state.contacts.end(),
    [&]( const auto & candidate ) {
        return candidate.identity == identity;
    } );
    const bool first = previous == state.contacts.end();
    if( ( first && state.contacts.size() >= 64 ) ||
        ( !first && ( previous->fatal_attempted ||
                      !execution_triggers::has_fatal_rules( state.producer ) ) ) ) {
        return nullptr;
    }
    std::unique_ptr<attack_scope> contact( new attack_scope( *source, target, state.producer, true ) );
    if( !contact->enabled ) {
        return nullptr;
    }
    if( first ) {
        projectile_fire_scope::impl::held_contact held;
        held.identity = identity;
        held.target = contact->target_hold;
        state.contacts.push_back( std::move( held ) );
    }
    contact->facts.weapon = state.round.weapon;
    contact->facts.weapon_uid = state.round.weapon_uid;
    contact->facts.weapon_skill = state.round.weapon_skill;
    contact->facts.weapon_categories = state.round.weapon_categories;
    contact->facts.ammunition = state.round.ammunition;
    contact->facts.spell_id = state.round.spell_id;
    contact->facts.spell_level = state.round.spell_level;
    contact->facts.monster_attack = state.round.monster_attack;
    return contact;
}

effect_hit_scope::effect_hit_scope( Creature &target, Creature *source, const spell *effect )
{
    if( current_fire && current_fire->data && current_fire->data->effect_active &&
        ( current_fire->data->producer == channel::monster_special ||
          ( effect && current_fire->data->expected_effect == effect ) ) ) {
        contact = attack_scope::action_hit( target, source );
    }
}

effect_hit_scope::~effect_hit_scope() noexcept( false )
{
    contact = nullptr;
}

direct_damage_scope effect_hit_scope::capture_damage()
{
    return direct_damage_scope( contact.get() );
}

void effect_hit_scope::resolve( const dealt_damage_instance &dealt )
{
    if( contact ) {
        contact->resolve( true, std::nullopt, dealt );
    }
}

projectile_hit_scope::~projectile_hit_scope() noexcept( false )
{
    contact = nullptr;
}

direct_damage_scope projectile_hit_scope::capture_damage()
{
    return direct_damage_scope( contact.get() );
}

void projectile_hit_scope::resolve( const dealt_damage_instance &dealt )
{
    if( contact ) {
        contact->resolve( true, contact->facts.critical, dealt );
    }
}

damage_scope::damage_scope( const Creature *source, const Creature &target ) :
    previous( current_damage ), owner( std::exchange( pending_damage, nullptr ) )
{
    ++damage_depth;
    current_damage = this;
    if( owner && ( owner->source_hold.get() != source || owner->target_hold.get() != &target ) ) {
        owner = nullptr;
    }
    if( owner ) {
        owner->facts.hp_loss = 0;
        owner->facts.weakpoint.reset();
        capture_location( owner->facts, *source, target );
    }
}

damage_scope::~damage_scope()
{
    current_damage = previous;
    --damage_depth;
}

void damage_scope::set_weakpoint( const std::string &id )
{
    if( owner && !id.empty() ) {
        owner->facts.weakpoint = id;
    }
}

global_variables::impl_t damage_scope::context() const
{
    return owner ? owner->facts.context() : global_variables::impl_t();
}

void damage_scope::arm_hp_capture( bodypart_id bp, const dealt_damage_instance &dealt )
{
    capture_hp = true;
    armed_once = true;
    if( owner ) {
        owner->facts.hit = true;
        if( bp ) {
            owner->facts.bodypart = bp.id().str();
        }
        for( const auto &entry : dealt.dealt_dams ) {
            owner->facts.damage[entry.first.str()] = std::max( 0, entry.second );
        }
    }
}

void damage_scope::disarm_hp_capture()
{
    // A side effect of this hit, such as the first tick of the bleed it opened, took the target's
    // last HP before the native HP loss, which then skipped the dead target. The hit still killed.
    if( capture_hp && early_loss > 0 && owner && owner->target_hold &&
        owner->target_hold->is_dead_state() ) {
        capture( *owner->target_hold, early_loss );
    }
    capture_hp = false;
}

void damage_scope::capture( const Creature &target, int amount )
{
    capture_hp = false;
    if( !owner || owner->target_hold.get() != &target ) {
        return;
    }
    owner->facts.hp_loss = std::max( 0, amount );
    if( action_channel( owner->facts.producer ) ) {
        owner->facts.fatal = amount > 0 && target.is_dead_state();
    }
    if( amount > 0 && target.is_dead_state() && g && owner->fatal_instance == 0 ) {
        owner->reserve_fatal( target );
    }
}

void record_hp_loss( const Creature &target, int amount )
{
    if( !current_damage ) {
        return;
    }
    if( current_damage->capture_hp ) {
        current_damage->capture( target, amount );
    } else if( !current_damage->armed_once && amount > 0 && current_damage->owner &&
               current_damage->owner->target_hold.get() == &target ) {
        current_damage->early_loss += amount;
    }
}

void attack_scope::reserve_fatal( const Creature &target )
{
    projectile_fire_scope::impl::held_contact *projectile_contact = nullptr;
    if( action_channel( facts.producer ) ) {
        if( !current_fire || !current_fire->data ) {
            return;
        }
        for( auto &contact : current_fire->data->contacts ) {
            if( contact.identity == facts.target ) {
                projectile_contact = &contact;
                break;
            }
        }
        if( !projectile_contact || projectile_contact->fatal_attempted ) {
            return;
        }
        projectile_contact->fatal_attempted = true;
    }
    attack_snapshot fatal = facts;
    fatal.fatal = true;
    const Creature *previous = std::exchange( fatal_commit_target, &target );
    const on_out_of_scope restore( [&]() {
        fatal_commit_target = previous;
    } );
    fatal_instance = g->executions().on_fatal_hit( fatal );
    if( projectile_contact ) {
        projectile_contact->fatal_instance = fatal_instance;
        const auto report = g->executions().last_trigger_report();
        projectile_contact->fatal_arbitrated = report && report->attack == facts.id;
    }
    if( fatal_instance == 0 ) {
        const auto report = g->executions().last_trigger_report();
        if( report && report->attack == facts.id ) {
            enabled = false; // Preserve the fatal-stage refusal; do not republish as a living hit.
        }
    }
}

bool attack_in_progress()
{
    return current_attack != nullptr || damage_depth != 0 || current_fire || current_flight;
}

bool at_fatal_commit( const Creature &target )
{
    return fatal_commit_target == &target;
}

primary_hit_scope::primary_hit_scope( attack_scope &contact ) : owner( contact ),
    previous( contact.armed )
{
    owner.armed = owner.enabled && current_attack == &owner;
}

primary_hit_scope::~primary_hit_scope()
{
    owner.armed = previous;
}

damage_barrier::damage_barrier()
{
    ++damage_depth;
}

damage_barrier::~damage_barrier()
{
    --damage_depth;
}
} // namespace execution_combat

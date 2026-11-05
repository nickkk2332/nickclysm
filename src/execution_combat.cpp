#include "execution_combat.h"

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

std::string channel_name( channel producer )
{
    switch( producer ) {
        case channel::character_melee:
            return "character_melee";
        case channel::monster_melee:
            return "monster_melee";
        case channel::projectile:
            return "projectile";
    }
    return "";
}

struct projectile_fire_scope::impl {
    struct held_contact {
        attack_snapshot facts;
        shared_ptr_fast<Creature> target;
    };
    shared_ptr_fast<Creature> actor;
    creature_ref identity;
    const dealt_projectile_attack *expected_flight = nullptr;
    attack_snapshot round;
    std::vector<creature_ref> seen;
    std::vector<held_contact> contacts;
};

projectile_fire_scope::projectile_fire_scope( Character &actor, map &here, const item &gun ) :
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
        !execution_triggers::has_rules( channel::projectile ) || actor.is_fake() ||
        actor.is_hallucination() || actor.is_mounted() ||
        actor.get_wielded_item().get_item() != &gun ||
        creature_ref::bind( actor ).resolve() != &actor ) {
        return;
    }
    const auto source = g->shared_from( actor );
    if( source ) {
        data = std::make_unique<impl>();
        data->actor = source;
        data->identity = creature_ref::bind( actor );
    }
}

projectile_fire_scope::~projectile_fire_scope() noexcept( false )
{
    current_fire = previous;
    if( !data || exceptions != 0 || std::uncaught_exceptions() != 0 ) {
        return;
    }
    // Retain all original bodies through native ranged events and rule callbacks.
    for( const impl::held_contact &contact : data->contacts ) {
        if( !contact.facts.fatal ) {
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
    data->expected_flight = &attack;
    data->round.weapon = gun.typeId().str();
    data->round.weapon_uid = gun.uid().get_value();
    data->round.weapon_skill = gun.gun_skill().str();
    if( !gun.ammo_current().is_null() ) {
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
                      producer == channel::projectile ) ) || !g || g->executions().processing_actions() ||
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
    if( fatal_instance != 0 && g ) {
        g->executions().release_fatal_attack( fatal_instance,
                                              resolved && exceptions == 0 && std::uncaught_exceptions() == 0 );
        return;
    }
    if( enabled && resolved && exceptions == 0 && std::uncaught_exceptions() == 0 ) {
        if( facts.producer == channel::projectile ) {
            if( current_fire && current_fire->data ) {
                current_fire->data->contacts.push_back( { facts, target_hold } );
            }
        } else {
            g->executions().on_attack_resolved( facts );
        }
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
    if( !current_flight || !current_flight->owner ||
        current_flight->owner != current_fire || current_flight->attack != &attack ||
        current_flight->here != here || current_attack || !current_projectile_target ||
        current_projectile_target->consumed || &current_projectile_target->target != &target ||
        current_fire->data->identity.resolve() != source || attack.shrapnel ||
        attack.proj.proj_effects.count( ammo_effect_str_id( "MAGIC" ) ) ||
        current_fire->data->actor.get() != source ) {
        return;
    }
    auto &state = *current_fire->data;
    const creature_ref identity = creature_ref::bind( target );
    if( state.seen.size() >= 64 || std::find( state.seen.begin(), state.seen.end(), identity ) !=
        state.seen.end() ) {
        return;
    }
    contact.reset( new attack_scope( *source, target, channel::projectile, true ) );
    if( !contact->enabled ) {
        return;
    }
    current_projectile_target->consumed = true;
    state.seen.push_back( identity );
    contact->facts.weapon = state.round.weapon;
    contact->facts.weapon_uid = state.round.weapon_uid;
    contact->facts.weapon_skill = state.round.weapon_skill;
    contact->facts.weapon_categories = state.round.weapon_categories;
    contact->facts.ammunition = state.round.ammunition;
    contact->facts.projectile_origin = current_flight->origin;
    contact->set_critical( critical );
    capture_location( contact->facts, *source, target );
}

projectile_hit_scope::~projectile_hit_scope() noexcept( false )
{
    delete contact.release();
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

void damage_scope::arm_hp_capture( bodypart_id bp, const dealt_damage_instance &dealt )
{
    capture_hp = owner != nullptr;
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
    capture_hp = false;
}

void record_hp_loss( const Creature &target, int amount )
{
    if( current_damage && current_damage->capture_hp && current_damage->owner &&
        current_damage->owner->target_hold.get() == &target ) {
        current_damage->capture_hp = false;
        current_damage->owner->facts.hp_loss = std::max( 0, amount );
        if( current_damage->owner->facts.producer == channel::projectile ) {
            // This milestone never reserves a fatal projectile injury, including later rescues.
            current_damage->owner->facts.fatal = amount > 0 && target.is_dead_state();
            return;
        }
        if( amount > 0 && target.is_dead_state() && g &&
            current_damage->owner->fatal_instance == 0 ) {
            attack_snapshot facts = current_damage->owner->facts;
            facts.fatal = true;
            const Creature *previous = std::exchange( fatal_commit_target, &target );
            const on_out_of_scope restore( [&]() {
                fatal_commit_target = previous;
            } );
            current_damage->owner->fatal_instance = g->executions().on_fatal_hit( facts );
            if( current_damage->owner->fatal_instance == 0 ) {
                const auto report = g->executions().last_trigger_report();
                if( report && report->attack == facts.id ) {
                    // This contact already had its fatal-stage arbitration. Preserve its refusal
                    // report rather than republishing the same contact as a living hit afterward.
                    current_damage->owner->enabled = false;
                }
            }
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

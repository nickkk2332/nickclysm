#include "execution_combat.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "avatar.h"
#include "cata_scope_helpers.h"
#include "creature.h"
#include "damage.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "line.h"
#include "map.h"
#include "mapdata.h"
#include "monster.h"
#include "mtype.h"
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

std::string kind( const Creature &who )
{
    return who.is_avatar() ? "avatar" : who.is_npc() ? "npc" : "monster";
}

void capture_location( attack_snapshot &s, const Creature &actor, const Creature &target )
{
    s.actor_position = actor.pos_abs();
    s.target_position = target.pos_abs();
    s.direction = tripoint_rel_ms( sgn( s.target_position.x() - s.actor_position.x() ),
                                   sgn( s.target_position.y() - s.actor_position.y() ), 0 );
    const map &here = get_map();
    if( here.inbounds( target.pos_bub( here ) ) ) {
        s.terrain = here.ter( target.pos_bub( here ) ).id().str();
        s.furniture = here.furn( target.pos_bub( here ) ).id().str();
    }
}
} // namespace

std::string channel_name( channel producer )
{
    return producer == channel::character_melee ? "character_melee" : "monster_melee";
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
    previous( current_attack ), exceptions( std::uncaught_exceptions() )
{
    current_attack = this;
    if( previous || damage_depth != 0 || !g || g->executions().processing_actions() ||
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
    return current_attack != nullptr || damage_depth != 0;
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

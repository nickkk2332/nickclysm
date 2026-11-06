#include "ability_runtime.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>

#include "ability_activity_actor.h"
#include "activity_actor.h"
#include "avatar.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "clone_ptr.h"
#include "creature.h"
#include "damage_transaction.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "execution_combat.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "incapacitation.h"
#include "item.h"
#include "item_location.h"
#include "item_uid.h"
#include "json.h"
#include "line.h"
#include "magic.h"
#include "map.h"
#include "math_parser_diag_value.h"
#include "memory_fast.h"
#include "player_activity.h"
#include "power_actor.h"
#include "power_motion.h"
#include "power_projection.h"
#include "power_runtime.h"
#include "rng.h"
#include "spell_cast_context.h"
#include "talker.h"
#include "type_id.h"

static const efftype_id effect_stunned( "stunned" );

static const json_character_flag json_flag_CANNOT_ATTACK( "CANNOT_ATTACK" );

namespace
{
constexpr size_t max_active = 128;
constexpr size_t max_history = 32;
constexpr size_t max_contacts = 64;
constexpr int max_depth = 8;
constexpr int max_root_requests = 64;

struct ability_instance {
    ability_view view;
    int revision = 1;
    time_point last_tick = calendar::before_time_starts;
    int spell_xp = 0;
    int move_cost = 0;
    int energy_cost = 0;
    std::string energy;
    int charge_cost = 1;
    std::string cancel_reason;

    bool active() const {
        return view.state == "pending" || view.state == "windup" || view.state == "releasing";
    }
};

bool avatar_activity_is( int64_t id )
{
    const auto *actor = dynamic_cast<const ability_activity_actor *>
                        ( get_avatar().activity.actor.get() );
    return actor && actor->instance == id;
}

void release_activity( const ability_instance &instance )
{
    if( avatar_activity_is( instance.view.id ) ) {
        get_avatar().activity.set_to_null();
    }
}

shared_ptr_fast<Creature> hold( const creature_ref &ref )
{
    Creature *who = ref.resolve_loaded();
    return who ? g->shared_from( *who ) : nullptr;
}

std::string resource_check( const Creature &caster, const std::string &resource, int cost,
                            int64_t reservation = 0 )
{
    return get_powers().check_cost( caster, resource, cost, reservation );
}

std::string native_check( const ability_request &request, tripoint_abs_ms &destination )
{
    if( !request.ability.is_valid() ) {
        return "invalid_ability";
    }
    if( request.invocation != ability_invocation::action &&
        request.invocation != ability_invocation::effect_only ) {
        return "invalid_invocation";
    }
    const ability_definition &def = *request.ability;
    if( request.invocation == ability_invocation::effect_only && !def.allow_effect_only ) {
        return "effect_only_forbidden";
    }
    Creature *caster = request.caster.resolve();
    if( !caster || caster->is_fake() || caster->is_hallucination() ) {
        return "caster_unavailable";
    }
    if( incapacitation::unconscious( *caster ) || caster->has_effect( effect_stunned ) ||
        caster->has_flag( json_flag_CANNOT_ATTACK ) ) {
        return "caster_incapacitated";
    }
    const map &here = get_map();
    if( !here.inbounds( caster->pos_bub( here ) ) ) {
        return "caster_unloaded";
    }
    if( def.target == "creature" ) {
        const Creature *target = request.target.resolve();
        if( !target || target->is_fake() || target->is_hallucination() ) {
            return "target_unavailable";
        }
        destination = target->pos_abs();
    } else if( def.target == "self" ) {
        destination = caster->pos_abs();
    } else if( request.point ) {
        destination = *request.point;
    } else {
        return "missing_point";
    }
    if( !here.inbounds( here.get_bub( destination ) ) ) {
        return "target_unloaded";
    }
    if( rl_dist( caster->pos_abs(), destination ) > def.range ) {
        return "out_of_range";
    }
    if( def.line_of_sight &&
        !here.sees( caster->pos_bub( here ), here.get_bub( destination ), def.range ) ) {
        return "blocked";
    }
    if( !def.spell.is_empty() && !def.spell.is_valid() ) {
        return "invalid_spell";
    }
    for( const effect_on_condition_id &eoc : def.eocs ) {
        if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
            return "invalid_eoc";
        }
    }
    if( request.context.size() + def.locals.size() > 256 ) {
        return "context_budget";
    }
    return {};
}
} // namespace

struct ability_manager::impl {
    damage_transaction_manager damage;
    power_manager powers;
    power_actor_manager actors;
    power_projection_manager projections;
    std::map<int64_t, ability_instance> instances;
    std::map<int64_t, int> root_requests;
    int64_t next_id = 1;
    int64_t current = 0;
    bool pumping = false;
    bool restoring = false;
    bool stopping = false;

    ability_request as_request( const ability_instance &instance ) const {
        const ability_view &v = instance.view;
        return { v.ability, v.caster, v.target, v.selected, v.invocation, {} };
    }

    std::string invalid( const ability_instance &instance ) const {
        if( !instance.cancel_reason.empty() ) {
            return instance.cancel_reason;
        }
        const ability_view &v = instance.view;
        if( !v.ability.is_valid() || v.ability->revision != instance.revision ) {
            return "incompatible_definition";
        }
        tripoint_abs_ms ignored;
        const std::string reason = native_check( as_request( instance ), ignored );
        if( !reason.empty() ) {
            return reason;
        }
        const Creature &caster = *v.caster.resolve();
        if( v.ability->interrupt_displacement && caster.pos_abs() != v.origin ) {
            return "displaced";
        }
        if( v.invocation == ability_invocation::action ) {
            if( g->executions().controls( caster ) ) {
                return "caster_busy";
            }
            if( caster.is_avatar() && v.state == "windup" && v.windup_ticks > 0 &&
                !avatar_activity_is( v.id ) ) {
                return "activity_changed";
            }
            if( const Character *person = caster.as_character() ) {
                if( !person->activity.is_null() && !( caster.is_avatar() && avatar_activity_is( v.id ) ) ) {
                    return "caster_busy";
                }
            }
            if( v.energy_reserved && !powers.reservation_matches( v.id, v.caster, instance.energy,
                    instance.energy_cost ) ) {
                return "reservation_lost";
            }
            const std::string cost = resource_check( caster, instance.energy, instance.energy_cost,
                                     v.energy_reserved ? v.id : 0 );
            return cost.empty() ? powers.check_charge( caster, v.cooldown, instance.charge_cost ) : cost;
        }
        return {};
    }

    dialogue context( const ability_instance &instance, Creature *caster, Creature *target ) const {
        const ability_view &v = instance.view;
        dialogue d( caster ? get_talker_for( caster ) : nullptr,
                    target ? get_talker_for( target ) : nullptr, {}, v.context );
        d.set_value( "ability_instance", std::to_string( v.id ) );
        d.set_value( "ability", v.ability.str() );
        d.set_value( "ability_root", std::to_string( v.root ) );
        d.set_value( "ability_parent", std::to_string( v.parent ) );
        d.set_value( "ability_depth", v.depth );
        d.set_value( "ability_state", v.state );
        d.set_value( "ability_reason", v.reason );
        d.set_value( "ability_invocation",
                     v.invocation == ability_invocation::action ? "action" : "effect_only" );
        d.set_value( "ability_origin", v.origin );
        d.set_value( "ability_selected", v.selected );
        d.set_value( "ability_target_position",
                     v.released_at.value_or( target ? target->pos_abs() : v.selected ) );
        d.set_value( "ability_windup_ticks", v.windup_ticks );
        d.set_value( "ability_spell_level", v.spell_level );
        d.set_value( "ability_costs_committed", v.costs_committed ? 1.0 : 0.0 );
        d.set_value( "ability_paid_moves", v.paid_moves );
        d.set_value( "ability_paid_energy", v.paid_energy );
        d.set_value( "ability_paid_charges", v.paid_charges );
        d.set_value( "ability_energy_reserved", v.energy_reserved ? 1.0 : 0.0 );
        d.set_value( "ability_cooldown", v.cooldown.str() );
        d.set_value( "ability_effect_completed", v.effect_completed ? 1.0 : 0.0 );
        d.set_value( "ability_contact_count", v.contacts.size() );
        return d;
    }

    void callbacks( ability_instance &instance, const std::vector<effect_on_condition_id> &list,
                    bool terminal = false ) const {
        for( const effect_on_condition_id &id : list ) {
            if( !terminal && !invalid( instance ).empty() ) {
                return;
            }
            const auto caster = hold( instance.view.caster );
            const auto target = hold( instance.view.target );
            dialogue d = context( instance, caster.get(), target.get() );
            if( !id.is_valid() || id->type != ACTIVATION ) {
                if( !terminal ) {
                    instance.cancel_reason = "invalid_eoc";
                }
                continue;
            }
            id->activate( d, true, [&]( const dialogue & out ) {
                if( !instance.view.ability.is_valid() ) {
                    return;
                }
                for( const auto &local : instance.view.ability->locals ) {
                    if( const diag_value *value = out.maybe_get_value( local.first ) ) {
                        if( value->is_str() == local.second.is_str() && value->is_dbl() == local.second.is_dbl() ) {
                            instance.view.context[local.first] = *value;
                        } else if( !terminal ) {
                            instance.cancel_reason = "invalid_local";
                        }
                    }
                }
            } );
        }
    }

    void finish( ability_instance &instance, const std::string &reason ) {
        if( !instance.active() ) {
            return;
        }
        powers.release_reservation( instance.view.id );
        instance.view.energy_reserved = false;
        instance.view.state = reason.empty() ? "completed" : "interrupted";
        instance.view.reason = reason.empty() ? "released" : reason;
        release_activity( instance );
        if( instance.view.ability.is_valid() ) {
            const ability_definition &def = *instance.view.ability;
            callbacks( instance, reason.empty() ? def.on_complete : def.on_interrupt, true );
            callbacks( instance, def.on_end, true );
        }
    }

    void release( ability_instance &instance ) {
        const ability_definition &def = *instance.view.ability;
        callbacks( instance, def.before_release );
        std::string reason = invalid( instance );
        if( !reason.empty() ) {
            finish( instance, reason );
            return;
        }
        tripoint_abs_ms destination;
        native_check( as_request( instance ), destination );
        // Spell conditions can run EOC expressions. Evaluate only during real release,
        // then re-resolve and revalidate every participant before committing payment.
        if( !def.spell.is_empty() ) {
            const auto caster = hold( instance.view.caster );
            const auto target = hold( instance.view.target );
            spell effect( def.spell, instance.spell_xp );
            const spell_cast_context scope( effect, context( instance, caster.get(),
                                            target.get() ).get_context() );
            const bool valid_caster = effect.valid_caster_condition( *caster );
            reason = invalid( instance );
            if( !valid_caster || !reason.empty() ) {
                finish( instance, reason.empty() ? "spell_caster_rejected" : reason );
                return;
            }
            native_check( as_request( instance ), destination );
            const bool valid_target = effect.is_valid_target( *caster, get_map().get_bub( destination ) );
            reason = invalid( instance );
            if( !valid_target || !reason.empty() ) {
                finish( instance, reason.empty() ? "spell_target_rejected" : reason );
                return;
            }
            tripoint_abs_ms checked_destination;
            native_check( as_request( instance ), checked_destination );
            if( checked_destination != destination ) {
                finish( instance, "target_changed_during_validation" );
                return;
            }
        }
        // No callbacks between this final validation and the commit. Optional
        // claims coordinate managed consumers; external native drains can still
        // make a reserved windup unaffordable.
        reason = invalid( instance );
        if( !reason.empty() ) {
            finish( instance, reason );
            return;
        }
        native_check( as_request( instance ), destination );
        const auto caster = hold( instance.view.caster );
        ability_view &v = instance.view;
        v.released_at = destination;
        v.state = "releasing";
        release_activity( instance );
        if( v.invocation == ability_invocation::action ) {
            // All accounting checks precede payment; these operations dispatch
            // no author callbacks. Reservations, costs and charges commit once.
            if( !powers.commit( *caster, instance.energy, instance.energy_cost,
                                v.energy_reserved ? v.id : 0, v.cooldown, instance.charge_cost, "ability_release" ) ) {
                finish( instance, "accounting_refused" );
                return;
            }
            v.energy_reserved = false;
            v.costs_committed = true;
            v.paid_moves = instance.move_cost;
            v.paid_energy = instance.energy_cost;
            v.paid_charges = v.cooldown.is_empty() ? 0 : instance.charge_cost;
            caster->mod_moves( -instance.move_cost );
        }
        if( !def.spell.is_empty() ) {
            spell effect( def.spell, instance.spell_xp );
            const auto target = hold( v.target );
            const spell_cast_context scope( effect, context( instance, caster.get(),
                                            target.get() ).get_context() );
            effect.cast_all_effects( *caster, get_map().get_bub( destination ) );
        } else {
            // Release is committed. These are effect commands, not new full casts.
            // Later EOCs re-resolve actors; stop if a participant disappears.
            for( const effect_on_condition_id &eoc : def.eocs ) {
                const auto live_caster = hold( v.caster );
                const auto target = hold( v.target );
                if( !live_caster || live_caster->is_dead_state() ||
                    ( def.target == "creature" && ( !target || target->is_dead_state() ) ) ) {
                    finish( instance, "participant_lost_after_commit" );
                    return;
                }
                if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                    finish( instance, "invalid_eoc_after_commit" );
                    return;
                }
                dialogue d = context( instance, live_caster.get(), target.get() );
                eoc->activate( d );
            }
        }
        v.effect_completed = true;
        finish( instance, "" );
    }

    void begin( ability_instance &instance ) {
        const std::string reason = invalid( instance );
        if( !reason.empty() ) {
            finish( instance, reason );
            return;
        }
        ability_view &v = instance.view;
        // A callback-created request waits for a subsequent safe pump. Its sampled
        // duration is retained; windup begins when that pump admits it.
        v.started = calendar::turn;
        instance.last_tick = calendar::turn;
        v.due = v.started + time_duration::from_turns( v.windup_ticks );
        v.state = "windup";
        if( v.invocation == ability_invocation::action && v.windup_ticks > 0 ) {
            Creature &caster = *v.caster.resolve();
            if( caster.is_avatar() && !avatar_activity_is( v.id ) ) {
                get_avatar().assign_activity( ability_activity_actor( v.id ) );
            }
            caster.set_moves( std::min( caster.get_moves(), 0 ) );
        }
        callbacks( instance, v.ability->on_start );
        const std::string after = invalid( instance );
        if( !after.empty() ) {
            finish( instance, after );
        } else if( v.due <= calendar::turn ) {
            release( instance );
        }
    }

    void hold_pending_avatar() {
        // A callback may queue the next action after the pump's work list was
        // captured. Claim its activity before returning control to player input;
        // do not recursively execute its hooks in the current action.
        for( auto &entry : instances ) {
            ability_instance &i = entry.second;
            if( i.view.state != "pending" || !i.cancel_reason.empty() ||
                i.view.invocation != ability_invocation::action ||
                !( i.view.caster == creature_ref::bind( get_avatar() ) ) ) {
                continue;
            }
            if( get_avatar().activity.is_null() ) {
                get_avatar().assign_activity( ability_activity_actor( i.view.id ) );
            }
            if( avatar_activity_is( i.view.id ) ) {
                get_avatar().set_moves( std::min( get_avatar().get_moves(), 0 ) );
            }
        }
    }

    void trim() {
        size_t inactive = 0;
        for( const auto &entry : instances ) {
            inactive += !entry.second.active();
        }
        for( auto it = instances.begin(); it != instances.end() && inactive > max_history; ) {
            if( !it->second.active() ) {
                it = instances.erase( it );
                --inactive;
            } else {
                ++it;
            }
        }
        for( auto it = root_requests.begin(); it != root_requests.end(); ) {
            const int64_t root = it->first;
            const bool alive = std::any_of( instances.begin(), instances.end(), [root]( const auto & entry ) {
                return entry.second.active() && entry.second.view.root == root;
            } );
            if( !alive ) {
                it = root_requests.erase( it );
            } else {
                ++it;
            }
        }
    }

    void failed( int64_t id ) {
        auto it = instances.find( id );
        if( it != instances.end() ) {
            powers.release_reservation( id );
            it->second.view.energy_reserved = false;
            it->second.view.state = "failed";
            it->second.view.reason = "callback_failed";
            release_activity( it->second );
        }
    }
};

ability_manager::ability_manager() : data( std::make_unique<impl>() ) {}
ability_manager::~ability_manager() = default;

damage_transaction_manager &ability_manager::damage_transactions()
{
    return data->damage;
}

power_projection_manager &ability_manager::projections()
{
    return data->projections;
}

power_actor_manager &ability_manager::power_actors()
{
    return data->actors;
}

power_manager &ability_manager::powers()
{
    return data->powers;
}

std::string ability_manager::check( const ability_request &request ) const
{
    if( data->restoring || data->stopping ) {
        return "world_transition";
    }
    tripoint_abs_ms destination;
    const std::string reason = native_check( request, destination );
    if( !reason.empty() ) {
        return reason;
    }
    if( data->next_id >= std::numeric_limits<int64_t>::max() - 1 ) {
        return "id_exhausted";
    }
    const auto parent = data->instances.find( data->current );
    if( parent != data->instances.end() ) {
        const ability_view &p = parent->second.view;
        if( p.depth >= max_depth || data->root_requests.at( p.root ) >= max_root_requests ) {
            return "ancestry_budget";
        }
    }
    const auto &instances = data->instances;
    if( std::count_if( instances.begin(), instances.end(), []( const auto & entry ) {
    return entry.second.active();
    } ) >= static_cast<int>( max_active ) ) {
        return "instance_budget";
    }
    const Creature &caster = *request.caster.resolve();
    if( request.invocation == ability_invocation::action ) {
        if( controls( caster ) || g->executions().controls( caster ) ) {
            return "caster_busy";
        }
        if( const Character *person = caster.as_character(); person && !person->activity.is_null() ) {
            return "caster_busy";
        }
        const ability_definition &def = *request.ability;
        const std::string cost = resource_check( caster, def.energy, def.energy_cost );
        return cost.empty() ? data->powers.check_charge( caster, def.cooldown, def.charge_cost ) : cost;
    }
    return {};
}

ability_admission ability_manager::request( const ability_id &id, const dialogue &d,
        ability_invocation mode, std::optional<tripoint_abs_ms> point )
{
    ability_request request;
    request.ability = id;
    request.invocation = mode;
    request.point = point;
    request.context = d.get_context();
    if( d.has_actor( false ) ) {
        if( const Creature *caster = d.const_actor( false )->get_const_creature() ) {
            request.caster = creature_ref::bind( *caster );
        }
    }
    if( d.has_actor( true ) ) {
        if( const Creature *target = d.const_actor( true )->get_const_creature() ) {
            request.target = creature_ref::bind( *target );
        }
        if( const item_location *location = d.const_actor( true )->get_const_item() ) {
            if( const item *source_item = location->get_item() ) {
                request.context["ability_source_item"] = diag_value{ source_item->typeId().str() };
                request.context["ability_source_item_uid"] = diag_value{ std::to_string( source_item->uid().get_value() ) };
            }
        }
    }
    return this->request( request );
}

ability_admission ability_manager::request( const ability_request &request )
{
    const std::string reason = check( request );
    if( !reason.empty() ) {
        return { 0, reason };
    }
    if( data->next_id >= std::numeric_limits<int64_t>::max() - 1 ) {
        return { 0, "id_exhausted" };
    }
    ability_instance instance;
    ability_view &v = instance.view;
    const auto parent = data->instances.find( data->current );
    if( parent != data->instances.end() ) {
        const ability_view &p = parent->second.view;
        if( p.depth >= max_depth || data->root_requests.at( p.root ) >= max_root_requests ) {
            return { 0, "ancestry_budget" };
        }
        v.parent = p.id;
        v.root = p.root;
        v.depth = p.depth + 1;
    }
    v.id = data->next_id++;
    if( v.root == 0 ) {
        v.root = v.id;
    }
    const ability_definition &def = *request.ability;
    v.ability = request.ability;
    v.caster = request.caster;
    v.target = def.target == "self" ? request.caster : request.target;
    if( def.target == "point" ) {
        v.target = creature_ref();
    }
    v.invocation = request.invocation;
    v.state = "pending";
    v.origin = request.caster.resolve()->pos_abs();
    native_check( request, v.selected );
    v.started = calendar::turn;

    v.spell_level = def.spell_level;
    v.context = request.context;
    for( const auto &local : def.locals ) {
        v.context[local.first] = local.second;
    }
    instance.revision = def.revision;
    instance.last_tick = calendar::turn;
    instance.move_cost = def.move_cost;
    instance.energy = def.energy;
    instance.energy_cost = def.energy_cost;
    instance.charge_cost = def.charge_cost;
    v.cooldown = def.cooldown;
    if( !def.spell.is_empty() ) {
        spell effect( def.spell );
        instance.spell_xp = effect.exp_for_level( def.spell_level );
    }
    const int64_t id = v.id;
    if( request.invocation == ability_invocation::action && def.reserve_energy &&
        def.energy_cost > 0 ) {
        if( !data->powers.reserve( *request.caster.resolve(), def.energy, def.energy_cost, id ) ) {
            return { 0, "reservation_refused" };
        }
        v.energy_reserved = true;
    }
    bool stored = false;
    const on_out_of_scope release_unstored( [&]() {
        if( !stored ) {
            data->powers.release_reservation( id );
        }
    } );
    v.windup_ticks = request.invocation == ability_invocation::effect_only ? 0 :
                     ( def.windup_min == def.windup_max ? def.windup_min : rng( def.windup_min, def.windup_max ) );
    v.due = v.started + time_duration::from_turns( v.windup_ticks );
    ++data->root_requests[v.root];
    data->instances.emplace( id, std::move( instance ) );
    stored = true;
    if( !data->pumping ) {
        data->trim();
    }
    return { id, "accepted" };
}

bool ability_manager::cancel( int64_t id, const std::string &reason )
{
    const auto it = data->instances.find( id );
    if( it == data->instances.end() || !it->second.active() || it->second.view.state == "releasing" ) {
        return false;
    }
    if( it->second.cancel_reason.empty() ) {
        it->second.cancel_reason = reason.empty() ? "cancelled" : reason;
    }
    return true;
}

void ability_manager::cancel_all( const std::string &reason )
{
    const bool stopping = data->stopping;
    data->stopping = true;
    const on_out_of_scope restore( [&]() {
        data->stopping = stopping;
    } );
    for( auto &entry : data->instances ) {
        cancel( entry.first, reason );
    }
    data->powers.cancel_all( reason );
    data->actors.cancel_all( reason );
    data->projections.cancel_all( reason );
    if( !processing() ) {
        drain_starts();
    }
}

void ability_manager::clear()
{
    if( processing() ) {
        cancel_all( "cleared" );
        return;
    }
    for( const auto &entry : data->instances ) {
        release_activity( entry.second );
    }
    data->instances.clear();
    data->root_requests.clear();
    data->damage.clear();
    data->powers.clear();
    data->actors.clear();
    data->projections.clear();
    data->restoring = false;
}

void ability_manager::cancel_for_world_transition( const std::string &reason )
{
    if( processing() || restoring() || g->executions().processing_actions() ||
        execution_combat::attack_in_progress() ) {
        throw JsonError( "Cannot transition inside an ability or execution action" );
    }
    const bool stopping = data->stopping;
    data->stopping = true;
    const on_out_of_scope restore( [&]() {
        data->stopping = stopping;
    } );
    cancel_all( reason );
    // Ability terminal hooks can queue scenes. End those while ability starts
    // remain blocked; execution cancellation blocks its own callback starts too.
    g->executions().cancel_all( reason );
}

void ability_manager::reset_for_load()
{
    if( processing() ) {
        throw JsonError( "Cannot replace a session inside an ability callback" );
    }
    clear();
    data->restoring = true;
}

void ability_manager::end_restore()
{
    data->restoring = false;
    data->damage.release_settled_sources();
}

bool ability_manager::restoring() const
{
    return data->restoring;
}

bool ability_manager::stopping() const
{
    return data->stopping;
}

bool ability_manager::processing() const
{
    return data->pumping || data->damage.processing() || data->powers.processing() ||
           data->actors.processing() || data->projections.processing() || power_motion::processing();
}

void ability_manager::drain_starts()
{
    if( processing() || data->restoring || g->executions().processing_actions() ||
        execution_combat::attack_in_progress() ) {
        return;
    }
    data->powers.drain_starts();
    data->projections.drain_starts();
    data->pumping = true;
    const on_out_of_scope restore( [&]() {
        data->current = 0;
        data->pumping = false;
    } );
    std::vector<int64_t> pending;
    for( const auto &entry : data->instances ) {
        if( entry.second.active() && ( entry.second.view.state == "pending" ||
                                       !entry.second.cancel_reason.empty() ) ) {
            pending.push_back( entry.first );
        }
    }
    for( int64_t id : pending ) {
        data->current = id;
        ability_instance &instance = data->instances.at( id );
        try {
            if( !instance.cancel_reason.empty() ) {
                data->finish( instance, instance.cancel_reason );
            } else if( instance.last_tick != calendar::turn &&
                       instance.last_tick + 1_turns != calendar::turn ) {
                data->finish( instance, "start_delayed" );
            } else {
                data->begin( instance );
            }
        } catch( ... ) {
            data->failed( id );
            throw;
        }
    }
    data->current = 0;
    data->hold_pending_avatar();
    data->trim();
}

void ability_manager::process()
{
    if( processing() || restoring() || g->executions().processing_actions() ||
        execution_combat::attack_in_progress() ) {
        return;
    }
    data->actors.process();
    data->powers.process();
    data->projections.process();
    drain_starts();
    data->pumping = true;
    const on_out_of_scope restore( [&]() {
        data->current = 0;
        data->pumping = false;
    } );
    std::vector<int64_t> active;
    for( const auto &entry : data->instances ) {
        if( entry.second.view.state == "windup" ) {
            active.push_back( entry.first );
        }
    }
    for( int64_t id : active ) {
        data->current = id;
        ability_instance &instance = data->instances.at( id );
        try {
            const std::string reason = data->invalid( instance );
            if( !reason.empty() ) {
                data->finish( instance, reason );
            } else if( instance.last_tick == calendar::turn ) {
                continue;
            } else if( instance.last_tick + 1_turns != calendar::turn ) {
                data->finish( instance, "time_skipped" );
            } else {
                instance.last_tick = calendar::turn;
                if( instance.view.due <= calendar::turn ) {
                    data->release( instance );
                }
            }
        } catch( ... ) {
            data->failed( id );
            throw;
        }
    }
    data->current = 0;
    data->hold_pending_avatar();
    data->trim();
}

bool ability_manager::controls( const Creature &who ) const
{
    if( data->instances.empty() || !g->shared_from( who ) ) {
        return false;
    }
    const creature_ref ref = creature_ref::bind( who );
    return std::any_of( data->instances.begin(), data->instances.end(), [&]( const auto & entry ) {
        const ability_instance &i = entry.second;
        return ( i.view.state == "pending" || i.view.state == "windup" ) && i.cancel_reason.empty() &&
               i.view.invocation == ability_invocation::action && i.view.caster == ref;
    } );
}

bool ability_manager::owns_avatar_activity( int64_t id ) const
{
    const auto it = data->instances.find( id );
    return it != data->instances.end() &&
           it->second.view.caster == creature_ref::bind( get_avatar() ) &&
           ( it->second.view.state == "pending" || it->second.view.state == "windup" ) &&
           it->second.cancel_reason.empty() &&
           it->second.view.invocation == ability_invocation::action && avatar_activity_is( id );
}

void ability_manager::notify_damage( const Creature &who, int amount )
{
    data->powers.notify_damage( who, amount );
    data->projections.notify_damage( who, amount );
    if( amount <= 0 || data->instances.empty() ) {
        return;
    }
    const creature_ref ref = creature_ref::bind( who );
    for( auto &entry : data->instances ) {
        ability_instance &i = entry.second;
        if( ( i.view.state == "pending" || i.view.state == "windup" ) && i.view.caster == ref &&
            i.cancel_reason.empty() && i.view.ability.is_valid() && i.view.ability->interrupt_damage ) {
            i.cancel_reason = "damaged";
        }
    }
}

void ability_manager::record_contact( const execution_combat::attack_snapshot &contact )
{
    const auto it = data->instances.find( data->current );
    if( it == data->instances.end() || it->second.view.state != "releasing" ||
        !( it->second.view.caster == contact.actor ) ) {
        return;
    }
    ability_view &v = it->second.view;
    global_variables::impl_t facts = contact.context();
    for( ability_contact &observed : v.contacts ) {
        if( observed.target == contact.target &&
            observed.facts.at( "attack_contact_id" ) == facts.at( "attack_contact_id" ) ) {
            if( contact.fatal ) {
                observed.facts = std::move( facts );
            }
            return;
        }
    }
    if( v.contacts.size() == max_contacts ) {
        v.contacts_truncated = true;
    } else {
        v.contacts.push_back( { contact.target, std::move( facts ) } );
    }
}

std::optional<ability_view> ability_manager::inspect( int64_t id ) const
{
    const auto it = data->instances.find( id );
    return it == data->instances.end() ? std::nullopt : std::make_optional( it->second.view );
}

std::vector<ability_view> ability_manager::inspect_all() const
{
    std::vector<ability_view> out;
    for( const auto &entry : data->instances ) {
        out.push_back( entry.second.view );
    }
    return out;
}

void ability_manager::serialize( JsonOut &jsout ) const
{
    if( processing() || restoring() || execution_combat::attack_in_progress() ) {
        throw JsonError( "Cannot save an ability inside a partially committed action" );
    }
    jsout.start_object();
    jsout.member( "next_id", data->next_id );
    jsout.member( "damage_transactions", data->damage );
    jsout.member( "powers", data->powers );
    jsout.member( "power_actors", data->actors );
    jsout.member( "power_projections", data->projections );
    jsout.member( "roots" );
    jsout.start_array();
    for( const auto &entry : data->root_requests ) {
        jsout.start_object();
        jsout.member( "id", entry.first );
        jsout.member( "requests", entry.second );
        jsout.end_object();
    }
    jsout.end_array();
    jsout.member( "instances" );
    jsout.start_array();
    for( const auto &entry : data->instances ) {
        const ability_instance &i = entry.second;
        const ability_view &v = i.view;
        if( !i.active() ) {
            continue;
        }
        jsout.start_object();
        jsout.member( "id", v.id );
        jsout.member( "root", v.root );
        jsout.member( "parent", v.parent );
        jsout.member( "depth", v.depth );
        jsout.member( "ability", v.ability );
        jsout.member( "revision", i.revision );
        jsout.member( "caster", v.caster );
        jsout.member( "target", v.target );
        jsout.member( "invocation", v.invocation == ability_invocation::action ? "action" : "effect_only" );
        jsout.member( "state", v.state );
        jsout.member( "origin", v.origin );
        jsout.member( "selected", v.selected );
        jsout.member( "started", v.started );
        jsout.member( "due", v.due );
        jsout.member( "windup_ticks", v.windup_ticks );
        jsout.member( "last_tick", i.last_tick );
        jsout.member( "spell_xp", i.spell_xp );
        jsout.member( "spell_level", v.spell_level );
        jsout.member( "move_cost", i.move_cost );
        jsout.member( "energy", i.energy );
        jsout.member( "energy_cost", i.energy_cost );
        jsout.member( "energy_reserved", v.energy_reserved );
        jsout.member( "cooldown", v.cooldown );
        jsout.member( "charge_cost", i.charge_cost );
        jsout.member( "cancel_reason", i.cancel_reason );
        jsout.member( "context", v.context );
        jsout.end_object();
    }
    jsout.end_array();
    jsout.end_object();
}

void ability_manager::deserialize( const JsonObject &jo )
{
    if( processing() ) {
        jo.throw_error( "Cannot load abilities inside a callback" );
    }
    // Do not clear the avatar activity just restored by game loading.
    data->instances.clear();
    data->root_requests.clear();
    data->damage.clear();
    jo.read( "damage_transactions", data->damage );
    data->powers.clear();
    data->actors.clear();
    data->projections.clear();
    jo.read( "powers", data->powers );
    jo.read( "power_actors", data->actors );
    jo.read( "power_projections", data->projections );
    int64_t next = 1;
    jo.read( "next_id", next, true );
    if( next <= 0 || next >= std::numeric_limits<int64_t>::max() ) {
        jo.throw_error_at( "next_id", "invalid ability instance counter" );
    }
    data->next_id = std::max( data->next_id, next );
    for( const JsonObject root : jo.get_array( "roots" ) ) {
        int64_t id = 0;
        root.read( "id", id, true );
        const int requests = root.get_int( "requests" );
        if( id <= 0 || id >= next || requests < 1 || requests > max_root_requests ||
            data->root_requests.size() >= max_active || !data->root_requests.emplace( id, requests ).second ) {
            root.throw_error( "invalid or duplicate ability root budget" );
        }
    }
    for( const JsonObject saved : jo.get_array( "instances" ) ) {
        if( data->instances.size() >= max_active ) {
            saved.throw_error( "too many saved abilities" );
        }
        ability_instance i;
        ability_view &v = i.view;
        saved.read( "id", v.id, true );
        saved.read( "root", v.root, true );
        saved.read( "parent", v.parent, true );
        saved.read( "depth", v.depth, true );
        saved.read( "ability", v.ability, true );
        saved.read( "revision", i.revision, true );
        saved.read( "caster", v.caster, true );
        saved.read( "target", v.target, true );
        const std::string invocation = saved.get_string( "invocation" );
        v.invocation = invocation == "action" ? ability_invocation::action :
                       ability_invocation::effect_only;
        saved.read( "state", v.state, true );
        saved.read( "origin", v.origin, true );
        saved.read( "selected", v.selected, true );
        saved.read( "started", v.started, true );
        saved.read( "due", v.due, true );
        saved.read( "windup_ticks", v.windup_ticks, true );
        saved.read( "last_tick", i.last_tick, true );
        saved.read( "spell_xp", i.spell_xp, true );
        saved.read( "spell_level", v.spell_level, true );
        saved.read( "move_cost", i.move_cost, true );
        saved.read( "energy", i.energy, true );
        saved.read( "energy_cost", i.energy_cost, true );
        saved.read( "energy_reserved", v.energy_reserved );
        saved.read( "cooldown", v.cooldown );
        saved.read( "charge_cost", i.charge_cost );
        saved.read( "cancel_reason", i.cancel_reason );
        saved.read( "context", v.context );
        if( v.id <= 0 || v.id >= next || v.root <= 0 || v.root > v.id ||
            v.parent < 0 || v.parent >= v.id || v.depth < 0 || v.depth > max_depth ||
            ( ( v.parent == 0 ) != ( v.depth == 0 ) ) || ( v.depth == 0 && v.root != v.id ) ||
            ( v.parent != 0 && v.root > v.parent ) ||
            data->root_requests.count( v.root ) == 0 ||
            data->root_requests.at( v.root ) <= v.depth ||
            ( invocation != "action" && invocation != "effect_only" ) ||
            ( v.state != "pending" && v.state != "windup" ) ||
            v.windup_ticks < 0 || v.windup_ticks > 600 ||
            v.due != v.started + time_duration::from_turns( v.windup_ticks ) ||
            i.last_tick < v.started || i.last_tick > calendar::turn || i.last_tick > v.due ||
            i.spell_xp < 0 || v.spell_level < 0 || v.spell_level > 100 ||
            i.move_cost < 0 || i.move_cost > 1000000 || i.energy_cost < 0 || i.energy_cost > 1000000 ||
            i.energy.empty() || i.charge_cost < 1 || i.charge_cost > 1000 ||
            ( i.energy == "none" && i.energy_cost != 0 ) || v.context.size() > 256 ||
            !data->instances.emplace( v.id, std::move( i ) ).second ) {
            saved.throw_error( "invalid or duplicate saved ability" );
        }
    }
    for( const auto &entry : data->instances ) {
        const auto &i = entry.second;
        if( i.view.energy_reserved && ( i.view.invocation != ability_invocation::action ||
                                        !data->powers.reservation_matches( i.view.id, i.view.caster, i.energy, i.energy_cost ) ) ) {
            jo.throw_error( "saved ability has an invalid resource reservation" );
        }
    }
    for( power_reservation_view const &r : data->powers.inspect_reservations() ) {
        const auto i = data->instances.find( r.token );
        if( i == data->instances.end() || !i->second.view.energy_reserved ) {
            jo.throw_error( "saved power reservation has no owning ability" );
        }
    }

}

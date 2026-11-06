#include "execution_runtime.h"

#include <algorithm>
#include <climits>
#include <limits>
#include <map>
#include <utility>

#include "avatar.h"
#include "bodypart.h"
#include "character.h"
#include "creature.h"
#include "creature_ref.h"
#include "creature_tracker.h"
#include "damage.h"
#include "debug.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "effect_source.h"
#include "execution.h"
#include "execution_activity_actor.h"
#include "execution_combat.h"
#include "execution_trigger_state.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "mapdata.h"
#include "memory_fast.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "cata_scope_helpers.h"
#include "npc.h"
#include "options.h"
#include "player_activity.h"
#include "rng.h"
#include "talker.h"
#include "translations.h"

static const efftype_id effect_beartrap( "beartrap" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_riding( "riding" );
static const efftype_id effect_tied( "tied" );

static const json_character_flag json_flag_CANNOT_MOVE( "CANNOT_MOVE" );
static const flag_id json_flag_GLIDING( "GLIDING" );
static const flag_id json_flag_GRAB( "GRAB" );
static const json_character_flag json_flag_LEVITATION( "LEVITATION" );
static const json_character_flag json_flag_PHASE_MOVEMENT( "PHASE_MOVEMENT" );
static const json_character_flag json_flag_WALL_CLING( "WALL_CLING" );

namespace
{
constexpr size_t max_instances = 64;
constexpr size_t max_history = 64;
constexpr int immediate_budget = 32;

struct execution_instance {
    execution_view view;
    int revision = 1;
    creature_ref actor;
    creature_ref target;
    int64_t weapon = 0;
    tripoint_abs_ms origin_actor;
    tripoint_abs_ms origin_target;
    tripoint_abs_ms expected_actor;
    tripoint_abs_ms expected_target;
    tripoint_rel_ms backward;
    time_point started = calendar::turn;
    time_point last_tick = calendar::turn;
    uint64_t random_state = 0;
    bool entered = false;
    bool target_dead = false;
    int entries = 0;
    int occurrences = 0;
    int requested = 0;
    int remaining = 0;
    int moved = 0;
    std::string cancel_reason;
    std::map<std::string, std::string> results;
    std::map<std::string, int> message_choices;
    std::map<std::string, std::string> message_anchors;
    std::vector<execution_narration_segment> narration;

    bool active() const {
        return view.state == "pending" || view.state == "running";
    }

    uint64_t random() {
        // SplitMix64: the stream and all pending decisions are saved with the instance.
        uint64_t value = ( random_state += UINT64_C( 0x9e3779b97f4a7c15 ) );
        value = ( value ^ ( value >> 30 ) ) * UINT64_C( 0xbf58476d1ce4e5b9 );
        value = ( value ^ ( value >> 27 ) ) * UINT64_C( 0x94d049bb133111eb );
        return value ^ ( value >> 31 );
    }

    int roll( int minimum, int maximum ) {
        if( minimum == maximum ) {
            return minimum;
        }
        const uint64_t count = static_cast<uint64_t>( maximum - minimum ) + 1;
        const uint64_t limit = UINT64_MAX - UINT64_MAX % count;
        uint64_t value;
        do {
            value = random();
        } while( value >= limit );
        return minimum + static_cast<int>( value % count );
    }
};

const creature_ref &role_ref( const execution_instance &scene, const std::string &role )
{
    return role == "actor" ? scene.actor : scene.target;
}

shared_ptr_fast<Creature> hold( const creature_ref &ref )
{
    Creature *who = ref.resolve();
    return who ? g->shared_from( *who ) : nullptr;
}

bool avatar_activity_is( int64_t id )
{
    const auto *actor = dynamic_cast<const execution_activity_actor *>
                        ( get_avatar().activity.actor.get() );
    return actor && actor->instance == id;
}

bool needs_control( const execution_definition &def, bool actor )
{
    return ( actor ? def.control_actor : def.control_target ) != execution_control::free;
}

bool restrained( const Creature &who )
{
    if( who.has_effect_with_flag( json_flag_GRAB ) || who.has_effect( effect_tied ) ||
        who.has_effect( effect_beartrap ) || who.has_effect( effect_ridden ) ||
        who.has_effect( effect_riding ) ) {
        return true;
    }
    if( const Character *person = who.as_character() ) {
        if( person->in_vehicle || person->is_mounted() || person->grabbed_creature() ) {
            return true;
        }
    }
    return who.is_avatar() && get_avatar().get_grab_type() != object_type::NONE;
}

bool passable_step( const Creature &who, const tripoint_bub_ms &next )
{
    const map &here = get_map();
    const tripoint_bub_ms from = who.pos_bub( here );
    if( !here.inbounds( next ) || from.z() != next.z() || rl_dist( from, next ) != 1 ||
        !here.passable( next ) || get_creature_tracker().creature_at( next, true ) ) {
        return false;
    }
    const point_rel_ms d = next.xy() - from.xy();
    // Do not push a creature through the intersection of two solid corners.
    return d.x() == 0 || d.y() == 0 || here.passable( from + tripoint_rel_ms( d.x(), 0, 0 ) ) ||
           here.passable( from + tripoint_rel_ms( 0, d.y(), 0 ) );
}

bool unsupported( const tripoint_bub_ms &next )
{
    const map &here = get_map();
    return here.inbounds( next ) && here.is_open_air( next ) && !here.has_floor_or_support( next );
}

bool gravity_applies( Creature &who, const tripoint_bub_ms &next )
{
    const map &here = get_map();
    if( restrained( who ) || !unsupported( next ) ||
        !here.inbounds( next + tripoint_rel_ms::below ) ||
        get_creature_tracker().creature_at( next + tripoint_rel_ms::below, true ) ||
        who.has_effect_with_flag( json_flag_GLIDING ) ||
        who.has_effect_with_flag( json_flag_LEVITATION ) ) {
        return false;
    }
    if( const monster *mon = who.as_monster() ) {
        return !mon->flies();
    }
    if( Character *person = who.as_character() ) {
        return !person->can_fly() && !person->has_flag( json_flag_PHASE_MOVEMENT ) &&
               !person->has_flag( json_flag_LEVITATION ) &&
               !( person->has_flag( json_flag_WALL_CLING ) && here.is_wall_adjacent( next ) );
    }
    return false;
}

tripoint_rel_ms direction_for( const execution_instance &scene, const execution_action &action )
{
    const bool reverse = ( action.role == "actor" ) != ( action.direction == "forward" );
    return reverse ? -scene.backward : scene.backward;
}

void replace_name( std::string &text, const std::string &key, const std::string &name )
{
    size_t pos = 0;
    while( ( pos = text.find( key, pos ) ) != std::string::npos ) {
        text.replace( pos, key.size(), name );
        pos += name.size();
    }
}

bool observed( const Creature *who )
{
    return who && ( who->is_avatar() || get_avatar().sees( get_map(), *who ) );
}

void narrate( execution_instance &scene, const translation &text, const std::string &role,
              const execution_narration &options )
{
    const auto actor = hold( scene.actor );
    const auto target = hold( scene.target );
    const Creature *subject = role == "actor" ? actor.get() : target.get();
    const bool visible = observed( subject );
    const bool visual = options.perception != execution_narration::audience::audible && visible;
    const bool heard = !visual && subject &&
                       options.perception != execution_narration::audience::visible &&
                       get_avatar().can_hear( subject->pos_bub(), options.volume );
    if( ( !visual && !heard ) || !get_option<bool>( "EXECUTION_NARRATION" ) ) {
        return;
    }
    std::string line = heard &&
                       options.perception == execution_narration::audience::visible_or_audible ?
                       options.audible_text.translated() : text.translated();
    const auto name = []( const Creature * who ) {
        return observed( who ) ? who->get_name() :
               std::string( _( "someone" ) );
    };
    replace_name( line, "<actor_name>", name( actor.get() ) );
    replace_name( line, "<target_name>", name( target.get() ) );
    const std::string anchor = options.append_to.empty() ? "" :
                               scene.message_anchors[options.append_to];
    const Messages::owned_message_result emitted = Messages::add_owned(
                "execution:" + std::to_string( scene.view.id ), anchor, line, options.separator );
    if( emitted.group.empty() ) {
        return;
    }
    if( !options.entry.empty() ) {
        scene.message_anchors[options.entry] = emitted.group;
    }
    if( !options.append_to.empty() ) {
        scene.message_anchors[options.append_to] = emitted.group;
    }
    scene.narration.push_back( { emitted.segment, emitted.group, options.entry, role,
                                 visual ? "visible" : "audible", calendar::turn, line } );
    if( scene.narration.size() > 128 ) {
        scene.narration.erase( scene.narration.begin() );
    }
    if( scene.view.transcript.size() == 128 ) {
        scene.view.transcript.erase( scene.view.transcript.begin() );
    }
    scene.view.transcript.push_back( std::move( line ) );
}
} // namespace

struct execution_manager::impl {
    std::map<int64_t, execution_instance> instances;
    int64_t next_id = 1;
    bool processing = false;
    bool stopping = false;
    int64_t owned_action = 0;
    bool gravity_action = false;
    execution_trigger_state triggers;

    dialogue context( execution_instance &scene, const shared_ptr_fast<Creature> &actor,
                      const shared_ptr_fast<Creature> &target ) const {
        dialogue result( actor ? get_talker_for( actor.get() ) : nullptr,
                         target ? get_talker_for( target.get() ) : nullptr, {}, scene.view.context );
        result.set_value( "execution_instance", std::to_string( scene.view.id ) );
        result.set_value( "execution_definition", scene.view.definition.str() );
        result.set_value( "execution_step", scene.view.step );
        result.set_value( "execution_state", scene.view.state );
        result.set_value( "execution_reason", scene.view.reason );
        result.set_value( "execution_outcome", scene.view.outcome );
        result.set_value( "execution_elapsed", to_turns<double>( calendar::turn - scene.started ) );
        result.set_value( "execution_entry", scene.entries );
        result.set_value( "execution_occurrence", scene.occurrences );
        result.set_value( "execution_actor_origin", scene.origin_actor );
        result.set_value( "execution_target_origin", scene.origin_target );
        result.set_value( "execution_target_dead", scene.target_dead ? 1.0 : 0.0 );
        if( actor ) {
            result.set_value( "execution_actor_position", actor->pos_abs() );
        }
        if( target ) {
            result.set_value( "execution_target_position", target->pos_abs() );
        }
        return result;
    }

    void refresh_positions( execution_instance &scene ) {
        if( Creature *who = scene.actor.resolve() ) {
            scene.expected_actor = who->pos_abs();
        }
        if( Creature *who = scene.target.resolve() ) {
            scene.expected_target = who->pos_abs();
        }
    }

    std::string invalid( const execution_instance &scene, bool displacement ) const {
        if( !scene.view.definition.is_valid() ) {
            return "missing_definition";
        }
        const execution_definition &def = *scene.view.definition;
        if( def.revision != scene.revision || def.steps.count( scene.view.step ) == 0 ) {
            return "incompatible_definition";
        }
        Creature *actor = scene.actor.resolve();
        Creature *target = scene.target.resolve();
        if( !actor || ( !target && !scene.target_dead ) ) {
            return "participant_unavailable";
        }
        const map &here = get_map();
        if( !here.inbounds( actor->pos_bub( here ) ) ||
            ( target && !here.inbounds( target->pos_bub( here ) ) ) ) {
            return "participant_unloaded";
        }
        if( target && rl_dist( actor->pos_abs(), target->pos_abs() ) > def.max_distance ) {
            return "out_of_range";
        }
        if( displacement && def.interrupt_displacement &&
            ( ( needs_control( def, true ) && actor->pos_abs() != scene.expected_actor ) ||
              ( target && needs_control( def, false ) && target->pos_abs() != scene.expected_target ) ) ) {
            return "displaced";
        }
        if( def.require_weapon ) {
            const Character *person = actor->as_character();
            const item_location item = person ? person->get_wielded_item() : item_location();
            if( !item || item->uid().get_value() != scene.weapon ) {
                return "weapon_lost";
            }
        }
        if( scene.view.state != "pending" &&
            ( ( actor->is_avatar() && needs_control( def, true ) ) ||
              ( target && target->is_avatar() && needs_control( def, false ) ) ) &&
            !avatar_activity_is( scene.view.id ) ) {
            return "activity_replaced";
        }
        return {};
    }

    void callbacks( execution_instance &scene, const std::vector<effect_on_condition_id> &list,
                    bool terminal = false ) {
        for( const effect_on_condition_id &eoc : list ) {
            if( !terminal && !scene.cancel_reason.empty() ) {
                break;
            }
            // Keep the actual creatures alive through a callback that removes a tracker entry.
            const auto actor = hold( scene.actor );
            const auto target = hold( scene.target );
            dialogue d = context( scene, actor, target );
            if( eoc.is_valid() && eoc->type == eoc_type::ACTIVATION ) {
                eoc->activate( d, true, [&]( const dialogue & out ) {
                    // Only declared instance locals are writable outputs. Trigger facts stay snapshots.
                    for( const auto &local : scene.view.definition->locals ) {
                        if( const diag_value *value = out.maybe_get_value( local.first ) ) {
                            if( value->is_str() != local.second.is_str() ||
                                value->is_dbl() != local.second.is_dbl() ) {
                                if( !terminal ) {
                                    scene.cancel_reason = "invalid_parameter";
                                }
                                continue;
                            }
                            scene.view.context[local.first] = diag_value{ *value };
                        }
                    }
                } );
            }
            if( !terminal ) {
                const std::string reason = invalid( scene, false );
                if( !reason.empty() ) {
                    scene.cancel_reason = reason;
                    break;
                }
            }
        }
        refresh_positions( scene );
    }

    void release( execution_instance &scene, bool discard_moves = true ) {
        if( avatar_activity_is( scene.view.id ) ) {
            get_avatar().activity.set_to_null();
        }
        for( const auto *ref : {
                 &scene.actor, &scene.target
             } ) {
            if( Creature *who = ref->resolve() ) {
                const bool actor = ref == &scene.actor;
                if( discard_moves && scene.view.definition.is_valid() &&
                    needs_control( *scene.view.definition, actor ) ) {
                    who->set_moves( std::min( who->get_moves(), 0 ) );
                }
            }
        }
    }

    void finish( execution_instance &scene, const std::string &reason = {} ) {
        if( !scene.active() ) {
            return;
        }
        const bool was_running = scene.view.state == "running";
        scene.view.state = reason.empty() ? "completed" : "interrupted";
        scene.view.reason = reason;
        scene.view.context["execution_target_dead"] = diag_value{ scene.target_dead ? 1.0 : 0.0 };
        release( scene, was_running );
        if( scene.view.definition.is_valid() && scene.revision == scene.view.definition->revision ) {
            const execution_definition &def = *scene.view.definition;
            callbacks( scene, reason.empty() ? def.on_complete : def.on_interrupt, true );
            callbacks( scene, def.on_end, true );
        }
    }

    void trim_history() {
        size_t count = 0;
        for( const auto &entry : instances ) {
            count += !entry.second.active();
        }
        for( auto it = instances.begin(); it != instances.end() && count > max_history; ) {
            if( !it->second.active() ) {
                it = instances.erase( it );
                --count;
            } else {
                ++it;
            }
        }
    }

    void result( execution_instance &scene, const execution_action &action,
                 const std::string &status ) {
        scene.view.context["execution_result"] = diag_value{ status };
        if( !action.store_result.empty() ) {
            scene.results[action.store_result] = status;
            scene.view.context["execution_result_" + action.store_result] = diag_value{ status };
            scene.view.context["execution_moved_" + action.store_result] = diag_value{ scene.moved };
            scene.view.context["execution_requested_" + action.store_result] = diag_value{ scene.requested };
        }
    }

    bool move( execution_instance &scene, const execution_action &action ) {
        const auto who = hold( role_ref( scene, action.role ) );
        // Native damage events can remove the initiator. Keep attribution pointers
        // alive until traps, gravity, fields, and death processing have all returned.
        const auto actor = hold( scene.actor );
        if( !who || !actor ) {
            result( scene, action, "unavailable" );
            return true;
        }
        const tripoint_rel_ms offset = direction_for( scene, action );
        const tripoint_abs_ms before = who->pos_abs();
        map &here = get_map();
        const tripoint_bub_ms destination = who->pos_bub( here ) + offset;
        if( offset == tripoint_rel_ms::zero || restrained( *who ) ||
            who->has_flag( json_flag_CANNOT_MOVE ) ||
            ( who->as_monster() && who->as_monster()->has_flag( mon_flag_IMMOBILE ) ) ) {
            result( scene, action, "unsupported" );
            return true;
        }
        if( !passable_step( *who, destination ) ) {
            result( scene, action, "blocked" );
            return true;
        }
        // Swimming/drowning needs a separate action contract. Do not bypass its entry rules.
        if( here.has_flag( ter_furn_flag::TFLAG_DEEP_WATER, destination ) ) {
            result( scene, action, "unsupported" );
            return true;
        }
        if( unsupported( destination ) &&
            ( action.stop_before_unsupported || !gravity_applies( *who, destination ) ) ) {
            result( scene, action, "unsupported" );
            return true;
        }
        const int moves_before = who->get_moves();
        const tripoint_abs_ms intended = here.get_abs( destination );
        // The default setpos/knockback path falls immediately. Commit the checked tile
        // first so narration, cursor accounting, and attributed native gravity stay ordered.
        who->setpos( here, destination, false );
        if( who->pos_abs() != intended ) {
            result( scene, action, "blocked" );
            return true;
        }
        ++scene.moved;
        --scene.remaining;
        if( !action.on_moved.empty() ) {
            narrate( scene, action.on_moved, action.role, action.on_moved_narration );
        }
        here.creature_on_trap( *who );
        if( !who->is_dead_state() && role_ref( scene, action.role ).resolve() == who.get() ) {
            // Scope attribution only to native gravity/impact, not to unrelated environmental damage.
            gravity_action = true;
            who->gravity_check( &here );
            gravity_action = false;
            if( !who->is_dead_state() ) {
                here.creature_in_field( *who );
            }
        }
        if( action.role == "target" && who->is_dead_state() ) {
            scene.target_dead = true;
        }
        who->check_dead_state( &here );
        if( action.role == "target" ) {
            // Native death prevention can rescue a target.
            scene.target_dead = who->is_dead_state();
        }
        if( who->is_avatar() && !who->is_dead_state() ) {
            g->update_map( *who->as_character(), before.z() != who->pos_abs().z() );
        }
        who->set_moves( moves_before );
        scene.view.context["execution_fell"] = diag_value{ who->pos_abs().z() < before.z() ? 1.0 : 0.0 };
        scene.view.context["execution_position"] = diag_value{ who->pos_abs() };
        refresh_positions( scene );
        if( who->pos_abs() != intended ) {
            // A fall is an expected result; teleporting traps stop the current motion too.
            result( scene, action, who->pos_abs().z() < before.z() ? "moved" : "displaced" );
            return true;
        }
        if( scene.remaining == 0 || who->is_dead_state() ) {
            result( scene, action, "moved" );
            return true;
        }
        scene.view.due = calendar::turn + time_duration::from_turns( action.ticks_per_tile );
        result( scene, action, "moving" );
        return false;
    }

    void immediate( execution_instance &scene, const execution_action &action ) {
        if( action.type == execution_action::kind::none ) {
            return;
        }
        if( action.type == execution_action::kind::message ) {
            const int previous = scene.message_choices.count( scene.view.step ) ?
                                 scene.message_choices.at( scene.view.step ) : -1;
            int total = 0;
            for( size_t n = 0; n < action.messages.size(); ++n ) {
                if( !action.avoid_repeat || action.messages.size() == 1 || static_cast<int>( n ) != previous ) {
                    total += action.messages[n].first;
                }
            }
            int choice = scene.roll( 1, total );
            for( size_t n = 0; n < action.messages.size(); ++n ) {
                if( action.avoid_repeat && action.messages.size() > 1 && static_cast<int>( n ) == previous ) {
                    continue;
                }
                choice -= action.messages[n].first;
                if( choice <= 0 ) {
                    scene.message_choices[scene.view.step] = static_cast<int>( n );
                    narrate( scene, action.messages[n].second, action.role, action.narration );
                    break;
                }
            }
            result( scene, action, "selected" );
            return;
        }
        if( action.type == execution_action::kind::eoc ) {
            callbacks( scene, action.eocs );
            result( scene, action, "dispatched" );
            return;
        }
        const auto target = hold( role_ref( scene, action.role ) );
        const auto source = hold( action.role == "actor" ? scene.target : scene.actor );
        if( !target ) {
            result( scene, action, "unavailable" );
            return;
        }
        if( action.type == execution_action::kind::effect ) {
            target->add_effect( effect_source( source.get() ), action.effect,
                                time_duration::from_turns( action.duration ), false, action.intensity );
            result( scene, action, target->has_effect( action.effect ) ? "applied" : "immune" );
        } else if( action.type == execution_action::kind::damage ) {
            const bodypart_id bp = target->as_character() ? action.bodypart.id() : bodypart_id();
            const int before = target->get_hp( bp );
            const dealt_damage_instance dealt = target->deal_damage( source.get(), bp,
                                                damage_instance( action.damage_type, action.amount ) );
            scene.view.context["execution_damage"] = diag_value{ dealt.total_damage() };
            scene.view.context["execution_hp_change"] = diag_value{ target->get_hp( bp ) - before };
            target->check_dead_state( &get_map() );
            if( action.role == "target" ) {
                scene.target_dead = target->is_dead_state();
            }
            result( scene, action, "applied" );
        }
        refresh_positions( scene );
    }

    const execution_transition &select( execution_instance &scene, const execution_transition &t ) {
        switch( t.type ) {
            case execution_transition::kind::step:
                // Transitions belong to the loaded definition, never to a temporary.
                // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
                return t;
            case execution_transition::kind::condition: {
                const auto actor = hold( scene.actor );
                const auto target = hold( scene.target );
                const dialogue d = context( scene, actor, target );
                return select( scene, t.condition( d ) ? *t.yes : *t.no );
            }
            case execution_transition::kind::chance: {
                const double choice = static_cast<double>( scene.random() >> 11 ) * ( 1.0 / 9007199254740992.0 );
                return select( scene, choice < t.chance ? *t.yes : *t.no );
            }
            case execution_transition::kind::weighted: {
                int total = 0;
                for( const auto &entry : t.weighted ) {
                    total += entry.first;
                }
                int choice = scene.roll( 1, total );
                for( const auto &entry : t.weighted ) {
                    choice -= entry.first;
                    if( choice <= 0 ) {
                        return select( scene, *entry.second );
                    }
                }
                break;
            }
            case execution_transition::kind::result: {
                const auto value = scene.results.find( t.result );
                const auto branch = value == scene.results.end() ? t.cases.end() : t.cases.find( value->second );
                return select( scene, branch == t.cases.end() ? *t.no : *branch->second );
            }
        }
        // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
        return t;
    }

    void advance( execution_instance &scene ) {
        const execution_definition &def = *scene.view.definition;
        for( int budget = 0; budget < immediate_budget && scene.active() &&
             scene.view.due <= calendar::turn;
             ++budget ) {
            if( !scene.cancel_reason.empty() ) {
                finish( scene, scene.cancel_reason );
                return;
            }
            const std::string reason = invalid( scene, false );
            if( !reason.empty() ) {
                finish( scene, reason );
                return;
            }
            const execution_step &step = def.steps.at( scene.view.step );
            if( !scene.entered ) {
                if( ++scene.entries > def.max_step_entries ) {
                    finish( scene, "step_budget" );
                    return;
                }
                scene.entered = true;
                scene.occurrences = 0;
                scene.requested = scene.remaining = step.action.type == execution_action::kind::move ?
                                                    scene.roll( step.action.tiles.min, step.action.tiles.max ) : 0;
                scene.moved = 0;
                const int first = step.action.type == execution_action::kind::move ?
                                  step.action.first_after : step.repeats ? step.first_after : 0;
                scene.view.due = calendar::turn + time_duration::from_turns( first );
                if( scene.view.due > calendar::turn ) {
                    return;
                }
            }
            owned_action = scene.view.id;
            if( step.action.type == execution_action::kind::move ) {
                const bool complete = move( scene, step.action );
                owned_action = 0;
                if( !complete ) {
                    return;
                }
            } else {
                immediate( scene, step.action );
                owned_action = 0;
                ++scene.occurrences;
                if( scene.occurrences < step.repeat_count && scene.cancel_reason.empty() ) {
                    scene.view.due = calendar::turn + time_duration::from_turns( step.interval );
                    return;
                }
            }
            if( !scene.cancel_reason.empty() ) {
                finish( scene, scene.cancel_reason );
                return;
            }
            if( !step.end.empty() ) {
                scene.view.outcome = step.end;
                finish( scene );
                return;
            }
            const execution_transition &next = select( scene, *step.next );
            scene.view.step = next.step;
            scene.entered = false;
            scene.view.due = calendar::turn + time_duration::from_turns( scene.roll( next.delay.min,
                             next.delay.max ) );
        }
        // Exhausting immediate work yields to the next turn, with all selected state retained.
        if( scene.active() && scene.view.due <= calendar::turn ) {
            scene.view.due = calendar::turn + 1_turns;
        }
    }

    void begin( execution_instance &scene ) {
        const std::string reason = invalid( scene, false );
        if( !scene.cancel_reason.empty() || !reason.empty() ) {
            finish( scene, scene.cancel_reason.empty() ? reason : scene.cancel_reason );
            return;
        }
        const execution_definition &def = *scene.view.definition;
        for( bool actor : {
                 true, false
             } ) {
            if( !needs_control( def, actor ) ) {
                continue;
            }
            Creature *who = ( actor ? scene.actor : scene.target ).resolve();
            if( Character *person = who->as_character() ) {
                if( !person->activity.is_null() ) {
                    finish( scene, "busy" );
                    return;
                }
            }
        }
        scene.view.state = "running";
        scene.started = scene.last_tick = calendar::turn;
        for( bool actor : {
                 true, false
             } ) {
            if( needs_control( def, actor ) ) {
                Creature *who = ( actor ? scene.actor : scene.target ).resolve();
                who->set_moves( std::min( who->get_moves(), 0 ) );
                if( who->is_avatar() ) {
                    get_avatar().assign_activity( execution_activity_actor( scene.view.id ) );
                }
            }
        }
        owned_action = scene.view.id;
        callbacks( scene, def.on_start );
        owned_action = 0;
        advance( scene );
    }
};

execution_manager::execution_manager() : data( std::make_unique<impl>() )
{
}

execution_manager::~execution_manager() = default;

execution_start_result execution_manager::start( const execution_id &definition, const dialogue &d,
        std::optional<uint64_t> seed )
{
    return start_impl( definition, d, seed, false );
}

std::string execution_manager::check_start( const execution_id &definition, const dialogue &d )
{
    return start_impl( definition, d, std::nullopt, true ).reason;
}

bool execution_manager::processing_actions() const
{
    return data->processing || data->stopping || data->triggers.is_selecting();
}

uint64_t execution_manager::next_attack_id()
{
    return data->triggers.allocate_contact();
}

void execution_manager::on_attack_resolved( const execution_combat::attack_snapshot &snapshot )
{
    if( data->processing || data->stopping ) {
        return;
    }
    const execution_trigger_report report = data->triggers.consider( *this, snapshot );
    if( report.instance != 0 ) {
        data->instances.at( report.instance ).backward = snapshot.direction;
    }
}

std::optional<execution_trigger_report> execution_manager::last_trigger_report() const
{
    return data->triggers.inspect();
}

execution_start_result execution_manager::start_impl( const execution_id &definition,
        const dialogue &d,
        std::optional<uint64_t> seed, bool check_only )
{
    if( data->stopping ) {
        return { 0, "world_transition" };
    }
    if( !definition.is_valid() ) {
        return { 0, "invalid_definition" };
    }
    if( !d.has_actor( false ) || !d.has_actor( true ) ) {
        return { 0, "missing_participant" };
    }
    const Creature *actor = d.const_actor( false )->get_const_creature();
    const Creature *target = d.const_actor( true )->get_const_creature();
    if( !actor || !target || actor == target || actor->is_dead_state() || target->is_dead_state() ) {
        return { 0, "invalid_participant" };
    }
    const execution_definition &def = *definition;
    execution_instance scene;
    scene.actor = creature_ref::bind( *actor );
    scene.target = creature_ref::bind( *target );
    if( scene.actor.resolve() != actor || scene.target.resolve() != target ) {
        return { 0, "participant_unloaded" };
    }
    if( rl_dist( actor->pos_abs(), target->pos_abs() ) > def.max_distance ) {
        return { 0, "out_of_range" };
    }
    size_t active = 0;
    for( const auto &entry : data->instances ) {
        const execution_instance &other = entry.second;
        if( !other.active() || !other.view.definition.is_valid() ||
            other.revision != other.view.definition->revision ) {
            continue;
        }
        ++active;
        for( bool role : {
                 true, false
             } ) {
            if( !needs_control( def, role ) ) {
                continue;
            }
            const creature_ref &ref = role ? scene.actor : scene.target;
            if( ( ref == other.actor && needs_control( *other.view.definition, true ) ) ||
                ( ref == other.target && needs_control( *other.view.definition, false ) ) ) {
                return { 0, "participant_busy" };
            }
        }
    }
    if( active >= max_instances || d.get_context().size() > 256 ) {
        return { 0, "instance_budget" };
    }
    for( bool role : {
             true, false
         } ) {
        const Creature *who = role ? actor : target;
        if( needs_control( def, role ) ) {
            const Character *person = who->as_character();
            if( person && !person->activity.is_null() ) {
                return { 0, "busy" };
            }
            if( restrained( *who ) ) {
                return { 0, "restrained" };
            }
        }
    }
    if( const Character *person = actor->as_character() ) {
        if( const item_location weapon = person->get_wielded_item() ) {
            scene.weapon = weapon->uid().get_value();
            scene.view.context["execution_weapon_type"] = diag_value{ weapon->typeId().str() };
            scene.view.context["execution_weapon_uid"] = diag_value{ std::to_string( scene.weapon ) };
        }
    }
    if( const diag_value *reported = d.maybe_get_value( "weapon_uid" ) ) {
        if( !reported->is_str() ) {
            return { 0, "invalid_weapon" };
        }
        int64_t original = 0;
        try {
            size_t end = 0;
            original = std::stoll( reported->str(), &end );
            if( original <= 0 || end != reported->str().size() ) {
                return { 0, "invalid_weapon" };
            }
        } catch( const std::exception & ) {
            return { 0, "invalid_weapon" };
        }
        if( def.require_weapon && original != scene.weapon ) {
            return { 0, "weapon_lost" };
        }
        scene.weapon = original;
        scene.view.context["execution_weapon_uid"] = diag_value{ reported->str() };
        if( const diag_value *type = d.maybe_get_value( "weapon" ); type && type->is_str() ) {
            scene.view.context["execution_weapon_type"] = *type;
        }
    } else if( const diag_value *type = d.maybe_get_value( "weapon" );
               type && type->is_str() && type->str() == "null" ) {
        scene.weapon = 0;
        scene.view.context.erase( "execution_weapon_uid" );
        scene.view.context.erase( "execution_weapon_type" );
    }
    if( def.require_weapon && scene.weapon == 0 ) {
        return { 0, "weapon_required" };
    }
    if( data->next_id == std::numeric_limits<int64_t>::max() ) {
        return { 0, "instance_budget" };
    }
    scene.view.definition = definition;
    scene.view.state = "pending";
    scene.view.step = def.start;
    scene.view.due = calendar::turn;
    scene.revision = def.revision;
    scene.origin_actor = scene.expected_actor = actor->pos_abs();
    scene.origin_target = scene.expected_target = target->pos_abs();
    scene.backward = tripoint_rel_ms( sgn( target->pos_abs().x() - actor->pos_abs().x() ),
                                      sgn( target->pos_abs().y() - actor->pos_abs().y() ), 0 );
    for( const auto &value : d.get_context() ) {
        if( value.first.find( "execution_" ) != 0 ) {
            scene.view.context[value.first] = diag_value{ value.second };
        }
    }
    for( const auto &value : def.locals ) {
        if( scene.view.context.count( value.first ) == 0 ) {
            scene.view.context.emplace( value );
        } else {
            const diag_value &input = scene.view.context.at( value.first );
            if( input.is_dbl() != value.second.is_dbl() || input.is_str() != value.second.is_str() ) {
                return { 0, "invalid_parameter" };
            }
        }
    }
    if( scene.view.context.size() > 256 ) {
        return { 0, "instance_budget" };
    }
    if( check_only ) {
        return { 0, "eligible" };
    }
    scene.view.id = data->next_id++;
    scene.random_state = seed ? *seed : ( static_cast<uint64_t>( rng( 0, INT_MAX ) ) << 32 |
                                          static_cast<uint64_t>( rng( 0, INT_MAX ) ) );
    scene.random_state ^= static_cast<uint64_t>( scene.view.id );
    const int64_t id = scene.view.id;
    data->instances.emplace( id, std::move( scene ) );
    return { id, "queued" };
}

bool execution_manager::cancel( int64_t id, const std::string &reason )
{
    const auto it = data->instances.find( id );
    if( it == data->instances.end() || !it->second.active() ) {
        return false;
    }
    it->second.cancel_reason = reason.empty() ? "cancelled" : reason;
    return true;
}

void execution_manager::cancel_all( const std::string &reason )
{
    const bool was_stopping = data->stopping;
    const bool was_processing = data->processing;
    const on_out_of_scope restore_flags( [&]() {
        data->stopping = was_stopping;
        data->processing = was_processing;
    } );
    data->stopping = true;
    for( auto &entry : data->instances ) {
        if( entry.second.active() ) {
            entry.second.cancel_reason = reason;
        }
    }
    if( !data->processing ) {
        data->processing = true;
        for( auto &entry : data->instances ) {
            if( entry.second.active() ) {
                data->finish( entry.second, reason );
            }
        }
        data->processing = false;
        data->trim_history();
    }
    data->stopping = false;
}

void execution_manager::clear()
{
    if( data->processing ) {
        cancel_all( "cleared" );
        return;
    }
    for( auto &entry : data->instances ) {
        if( entry.second.active() ) {
            data->release( entry.second );
        }
    }
    data->instances.clear();
    data->triggers.clear();
}

void execution_manager::drain_starts()
{
    if( data->processing ) {
        return;
    }
    data->processing = true;
    const on_out_of_scope reset_pump( [&]() {
        if( data->owned_action != 0 ) {
            execution_instance &scene = data->instances.at( data->owned_action );
            scene.view.state = "interrupted";
            scene.view.reason = "callback_failed";
            data->release( scene );
        }
        data->owned_action = 0;
        data->gravity_action = false;
        data->processing = false;
    } );
    std::vector<int64_t> pending;
    for( const auto &entry : data->instances ) {
        if( entry.second.view.state == "pending" ||
            ( entry.second.active() && !entry.second.cancel_reason.empty() ) ) {
            pending.push_back( entry.first );
        }
    }
    for( int64_t id : pending ) {
        execution_instance &scene = data->instances.at( id );
        if( scene.view.state == "pending" ) {
            data->begin( scene );
        } else {
            data->finish( scene, scene.cancel_reason );
        }
    }
    data->processing = false;
    data->trim_history();
}

void execution_manager::process()
{
    if( data->processing ) {
        return;
    }
    drain_starts();
    data->processing = true;
    const on_out_of_scope reset_pump( [&]() {
        if( data->owned_action != 0 ) {
            execution_instance &scene = data->instances.at( data->owned_action );
            scene.view.state = "interrupted";
            scene.view.reason = "callback_failed";
            data->release( scene );
        }
        data->owned_action = 0;
        data->gravity_action = false;
        data->processing = false;
    } );
    std::vector<int64_t> active;
    for( const auto &entry : data->instances ) {
        if( entry.second.view.state == "running" ) {
            active.push_back( entry.first );
        }
    }
    for( int64_t id : active ) {
        execution_instance &scene = data->instances.at( id );
        if( !scene.cancel_reason.empty() ) {
            data->finish( scene, scene.cancel_reason );
            continue;
        }
        const std::string reason = data->invalid( scene, true );
        if( !reason.empty() ) {
            data->finish( scene, reason );
            continue;
        }
        const execution_definition &def = *scene.view.definition;
        if( calendar::turn - scene.started >= time_duration::from_turns( def.max_ticks ) ) {
            data->finish( scene, "timeout" );
            continue;
        }
        if( scene.last_tick == calendar::turn ) {
            continue;
        }
        if( scene.last_tick + 1_turns != calendar::turn ) {
            data->finish( scene, "time_skipped" );
            continue;
        }
        scene.last_tick = calendar::turn;
        data->owned_action = scene.view.id;
        data->callbacks( scene, def.on_tick );
        data->owned_action = 0;
        data->advance( scene );
    }
    data->processing = false;
    data->trim_history();
}

bool execution_manager::contains( const Creature &who ) const
{
    if( data->instances.empty() ) {
        return false;
    }
    const creature_ref ref = creature_ref::bind( who );
    for( const auto &entry : data->instances ) {
        if( entry.second.active() && ( entry.second.actor == ref || entry.second.target == ref ) ) {
            return true;
        }
    }
    return false;
}

bool execution_manager::controls( const Creature &who ) const
{
    if( data->instances.empty() ) {
        return false;
    }
    const creature_ref ref = creature_ref::bind( who );
    for( const auto &entry : data->instances ) {
        const execution_instance &scene = entry.second;
        if( !scene.active() || !scene.cancel_reason.empty() || !scene.view.definition.is_valid() ) {
            continue;
        }
        const execution_definition &def = *scene.view.definition;
        if( ( scene.actor == ref && needs_control( def, true ) ) ||
            ( scene.target == ref && needs_control( def, false ) ) ) {
            return true;
        }
    }
    return false;
}

bool execution_manager::owns_avatar_activity( int64_t id ) const
{
    const auto it = data->instances.find( id );
    if( it == data->instances.end() || !it->second.active() ||
        !it->second.cancel_reason.empty() || !it->second.view.definition.is_valid() ) {
        return false;
    }
    const creature_ref avatar = creature_ref::bind( get_avatar() );
    const execution_instance &scene = it->second;
    const execution_definition &def = *scene.view.definition;
    return ( scene.actor == avatar && needs_control( def, true ) ) ||
           ( scene.target == avatar && needs_control( def, false ) );
}

bool execution_manager::can_fall( int64_t id, const std::string &role ) const
{
    const auto it = data->instances.find( id );
    if( it == data->instances.end() || !it->second.active() ) {
        return false;
    }
    const execution_instance &scene = it->second;
    Creature *who = role_ref( scene, role ).resolve();
    execution_action action;
    action.role = role;
    const tripoint_rel_ms direction = direction_for( scene, action );
    if( !who || direction == tripoint_rel_ms::zero ) {
        return false;
    }
    const tripoint_bub_ms next = who->pos_bub() + direction;
    return passable_step( *who, next ) && gravity_applies( *who, next );
}

void execution_manager::notify_damage( const Creature &who, int amount )
{
    if( amount <= 0 || data->instances.empty() ) {
        return;
    }
    const creature_ref ref = creature_ref::bind( who );
    for( auto &entry : data->instances ) {
        execution_instance &scene = entry.second;
        if( scene.active() && scene.view.id != data->owned_action &&
            scene.view.definition.is_valid() && scene.view.definition->interrupt_damage &&
            ( scene.actor == ref || scene.target == ref ) ) {
            scene.cancel_reason = "damaged";
        }
    }
}

Creature *execution_manager::secondary_damage_source() const
{
    if( !data->gravity_action || data->owned_action == 0 ) {
        return nullptr;
    }
    const auto it = data->instances.find( data->owned_action );
    return it == data->instances.end() ? nullptr : it->second.actor.resolve();
}

std::optional<execution_view> execution_manager::inspect( int64_t id ) const
{
    const auto it = data->instances.find( id );
    if( it == data->instances.end() ) {
        return std::nullopt;
    }
    return it->second.view;
}

std::vector<execution_narration_segment> execution_manager::narration( int64_t id ) const
{
    const auto it = data->instances.find( id );
    return it == data->instances.end() ? std::vector<execution_narration_segment>() :
           it->second.narration;
}

void execution_narration_segment::serialize( JsonOut &out ) const
{
    out.start_object();
    out.member( "id", id );
    out.member( "group", group );
    out.member( "entry", entry );
    out.member( "role", role );
    out.member( "audience", audience );
    out.member( "when", when );
    out.member( "text", text );
    out.end_object();
}

void execution_narration_segment::deserialize( const JsonObject &obj )
{
    obj.read( "id", id, true );
    obj.read( "group", group, true );
    obj.read( "entry", entry );
    obj.read( "role", role, true );
    obj.read( "audience", audience, true );
    obj.read( "when", when, true );
    obj.read( "text", text, true );
}

void execution_manager::serialize( JsonOut &jsout ) const
{
    if( processing_actions() ) {
        throw JsonError( "Cannot save an execution inside a partially committed action" );
    }
    jsout.start_object();
    jsout.member( "next_id", data->next_id );
    jsout.member( "triggers", data->triggers );
    jsout.member( "instances" );
    jsout.start_array();
    for( const auto &entry : data->instances ) {
        const execution_instance &scene = entry.second;
        if( !scene.active() ) {
            continue;
        }
        jsout.start_object();
        jsout.member( "id", scene.view.id );
        jsout.member( "definition", scene.view.definition );
        jsout.member( "revision", scene.revision );
        jsout.member( "state", scene.view.state );
        jsout.member( "step", scene.view.step );
        jsout.member( "due", scene.view.due );
        jsout.member( "context", scene.view.context );
        jsout.member( "transcript", scene.view.transcript );
        jsout.member( "actor", scene.actor );
        jsout.member( "target", scene.target );
        jsout.member( "weapon", scene.weapon );
        jsout.member( "origin_actor", scene.origin_actor );
        jsout.member( "origin_target", scene.origin_target );
        jsout.member( "expected_actor", scene.expected_actor );
        jsout.member( "expected_target", scene.expected_target );
        jsout.member( "backward", scene.backward );
        jsout.member( "started", scene.started );
        jsout.member( "last_tick", scene.last_tick );
        jsout.member( "random", std::to_string( scene.random_state ) );
        jsout.member( "entered", scene.entered );
        jsout.member( "target_dead", scene.target_dead );
        jsout.member( "entries", scene.entries );
        jsout.member( "occurrences", scene.occurrences );
        jsout.member( "requested", scene.requested );
        jsout.member( "remaining", scene.remaining );
        jsout.member( "moved", scene.moved );
        jsout.member( "cancel_reason", scene.cancel_reason );
        jsout.member( "results", scene.results );
        jsout.member( "message_choices", scene.message_choices );
        jsout.member( "message_anchors", scene.message_anchors );
        jsout.member( "narration", scene.narration );
        if( scene.entered && scene.view.definition.is_valid() &&
            scene.view.definition->steps.count( scene.view.step ) != 0 ) {
            jsout.member( "action_kind", static_cast<int>( scene.view.definition->steps.at(
                              scene.view.step ).action.type ) );
        }
        jsout.end_object();
    }
    jsout.end_array();
    jsout.end_object();
}

void execution_manager::deserialize( const JsonObject &jo )
{
    // The game cleared the old session before restoring actors/activities. Do not erase
    // the just-restored avatar activity while reading its matching runtime record.
    data->instances.clear();
    data->triggers.clear();
    jo.read( "triggers", data->triggers );
    int64_t saved_next = 1;
    jo.read( "next_id", saved_next );
    if( saved_next <= 0 || saved_next == std::numeric_limits<int64_t>::max() ) {
        jo.throw_error_at( "next_id", "invalid execution instance counter" );
    }
    data->next_id = std::max( data->next_id, saved_next );
    for( const JsonObject saved : jo.get_array( "instances" ) ) {
        if( data->instances.size() >= max_instances ) {
            saved.throw_error( "too many saved executions" );
        }
        execution_instance scene;
        saved.read( "id", scene.view.id, true );
        saved.read( "definition", scene.view.definition, true );
        saved.read( "revision", scene.revision, true );
        saved.read( "state", scene.view.state, true );
        saved.read( "step", scene.view.step, true );
        saved.read( "due", scene.view.due, true );
        saved.read( "context", scene.view.context );
        saved.read( "transcript", scene.view.transcript );
        saved.read( "actor", scene.actor, true );
        saved.read( "target", scene.target, true );
        saved.read( "weapon", scene.weapon );
        saved.read( "origin_actor", scene.origin_actor );
        saved.read( "origin_target", scene.origin_target );
        saved.read( "expected_actor", scene.expected_actor );
        saved.read( "expected_target", scene.expected_target );
        saved.read( "backward", scene.backward );
        saved.read( "started", scene.started );
        saved.read( "last_tick", scene.last_tick );
        try {
            const std::string value = saved.get_string( "random" );
            size_t end = 0;
            scene.random_state = std::stoull( value, &end );
            if( end != value.size() ) {
                saved.throw_error_at( "random", "invalid saved RNG state" );
            }
        } catch( const std::invalid_argument & ) {
            saved.throw_error_at( "random", "invalid saved RNG state" );
        } catch( const std::out_of_range & ) {
            saved.throw_error_at( "random", "invalid saved RNG state" );
        }
        saved.read( "entered", scene.entered );
        saved.read( "target_dead", scene.target_dead );
        saved.read( "entries", scene.entries );
        saved.read( "occurrences", scene.occurrences );
        saved.read( "requested", scene.requested );
        saved.read( "remaining", scene.remaining );
        saved.read( "moved", scene.moved );
        saved.read( "cancel_reason", scene.cancel_reason );
        saved.read( "results", scene.results );
        saved.read( "message_choices", scene.message_choices );
        saved.read( "message_anchors", scene.message_anchors );
        saved.read( "narration", scene.narration );
        const int kind = saved.get_int( "action_kind", -1 );
        if( scene.view.id <= 0 || scene.view.id == std::numeric_limits<int64_t>::max() ||
            data->instances.count( scene.view.id ) || !scene.active() ) {
            saved.throw_error( "invalid or duplicate saved execution identity/state" );
        }
        if( !scene.view.definition.is_valid() || scene.view.definition->revision != scene.revision ||
            scene.view.definition->steps.count( scene.view.step ) == 0 ) {
            scene.cancel_reason = "incompatible_definition";
        } else {
            const execution_step &step = scene.view.definition->steps.at( scene.view.step );
            if( ( scene.entered && kind != static_cast<int>( step.action.type ) ) || scene.entries < 0 ||
                scene.entries > scene.view.definition->max_step_entries || scene.occurrences < 0 ||
                scene.occurrences > step.repeat_count || scene.requested < 0 || scene.requested > 16 ||
                scene.remaining < 0 || scene.remaining > scene.requested || scene.moved < 0 ||
                scene.moved > scene.requested || scene.backward.x() < -1 || scene.backward.x() > 1 ||
                scene.backward.y() < -1 || scene.backward.y() > 1 || scene.backward.z() != 0 ||
                ( scene.view.state == "pending" && ( scene.entered || scene.entries != 0 ) ) ||
                scene.view.transcript.size() > 128 || scene.view.context.size() > 256 ||
                scene.message_anchors.size() > 64 || scene.narration.size() > 128 ) {
                scene.cancel_reason = "invalid_saved_state";
            }
        }
        data->next_id = std::max( data->next_id, scene.view.id + 1 );
        data->instances.emplace( scene.view.id, std::move( scene ) );
    }
    // Actor/activity validation happens at the first safe pump, after all save records load.
}

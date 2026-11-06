#include "damage_transaction.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "ability_runtime.h"
#include "bodypart.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "creature.h"
#include "creature_tracker.h"
#include "damage.h"
#include "damage_rule.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "execution_fatal.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json.h"
#include "magic.h"
#include "memory_fast.h"
#include "talker.h"
#include "power_runtime.h"

namespace
{
constexpr int packet_limit = 1000000;
constexpr size_t component_limit = 32;
constexpr size_t rule_limit = 64;
constexpr int depth_limit = 8;
constexpr int family_limit = 64;

struct damage_family {
    int packets = 0;
};

struct reflected_packet {
    damage_instance packet;
    std::string rule;
};

creature_ref tracked_ref( const Creature *who )
{
    return who && g && g->shared_from( *who ) ? creature_ref::bind( *who ) : creature_ref();
}

std::string kind( const Creature *who )
{
    if( !who ) {
        return "environment";
    }
    if( who->is_avatar() ) {
        return "avatar";
    }
    if( who->is_npc() ) {
        return "npc";
    }
    return who->is_monster() ? "monster" : "untracked";
}

int effective( const damage_unit &unit, const Creature &defender )
{
    if( !unit.type.is_valid() ) {
        return 0;
    }
    if( defender.is_immune_damage( unit.type ) ) {
        return 0;
    }
    const float amount = unit.amount * unit.damage_multiplier * unit.unconditional_damage_mult;
    return amount > 0 && std::isfinite( amount ) && amount <= packet_limit ?
           static_cast<int>( amount ) : 0;
}

bool packet_valid( const damage_instance &packet )
{
    if( packet.damage_units.size() > component_limit ) {
        return false;
    }
    double total = 0;
    for( const damage_unit &unit : packet ) {
        const double amount = unit.amount * unit.damage_multiplier * unit.unconditional_damage_mult;
        if( !unit.type.is_valid() || !std::isfinite( amount ) || amount < 0 ||
            !std::isfinite( unit.amount ) || unit.amount < 0 ||
            !std::isfinite( unit.res_pen ) || !std::isfinite( unit.res_mult ) ||
            !std::isfinite( unit.unconditional_res_mult ) ||
            !std::isfinite( unit.damage_multiplier ) || unit.damage_multiplier < 0 ||
            !std::isfinite( unit.unconditional_damage_mult ) || unit.unconditional_damage_mult < 0 ) {
            return false;
        }
        total += amount;
    }
    return total <= packet_limit;
}

int total( const damage_instance &packet, const Creature &defender,
           const damage_rule *rule = nullptr )
{
    int result = 0;
    for( const damage_unit &unit : packet ) {
        if( !rule || rule->damage_types.empty() || rule->damage_types.count( unit.type ) ) {
            result += effective( unit, defender );
        }
    }
    return result;
}

bool set_effective( damage_unit &unit, int remaining, const Creature &defender )
{
    if( remaining <= 0 ) {
        unit.amount = 0;
        return true;
    }
    const double factor = static_cast<double>( unit.damage_multiplier ) *
                          unit.unconditional_damage_mult;
    unit.amount = static_cast<float>( remaining / factor );
    // Preserve native multipliers and critical/base-damage behavior, including
    // float truncation at the integer damage boundary.
    for( int i = 0; i < 8 && effective( unit, defender ) != remaining; ++i ) {
        unit.amount = std::nextafter( unit.amount, effective( unit, defender ) < remaining ?
                                      std::numeric_limits<float>::infinity() : 0.0f );
    }
    return effective( unit, defender ) == remaining;
}

int remove_points( damage_instance &packet, const Creature &defender, const damage_rule &rule,
                   int wanted, damage_instance *reflected = nullptr )
{
    std::vector<size_t> order;
    for( size_t i = 0; i < packet.damage_units.size(); ++i ) {
        if( rule.damage_types.empty() || rule.damage_types.count( packet.damage_units[i].type ) ) {
            order.push_back( i );
        }
    }
    std::stable_sort( order.begin(), order.end(), [&]( size_t lhs, size_t rhs ) {
        // NOLINTBEGIN(cata-use-localized-sorting)
        return packet.damage_units[lhs].type.str() <
               packet.damage_units[rhs].type.str();
        // NOLINTEND(cata-use-localized-sorting)
    } );
    int removed = 0;
    for( const size_t i : order ) {
        damage_unit &unit = packet.damage_units[i];
        const int before = effective( unit, defender );
        const int take = std::min( before, wanted - removed );
        if( take > 0 ) {
            if( !set_effective( unit, before - take, defender ) ) {
                return -1;
            }
            if( reflected ) {
                // This is a new packet, not another use of the original armor penetration.
                reflected->damage_units.emplace_back( unit.type, take );
            }
            removed += take;
        }
        if( removed == wanted ) {
            break;
        }
    }
    return removed;
}

std::string lineage_key( const damage_rule &rule, const creature_ref &defender )
{
    return rule.id.str() + "/" + defender.dimension.str() + "/" +
           std::to_string( static_cast<int>( defender.type ) ) + "/" + std::to_string( defender.id );
}

std::vector<damage_component_view> components( const damage_instance &packet )
{
    std::vector<damage_component_view> out;
    for( const damage_unit &unit : packet ) {
        if( out.size() == component_limit ) {
            break;
        }
        out.push_back( { unit.type, unit.amount, unit.res_pen, unit.res_mult,
                         unit.damage_multiplier, unit.unconditional_damage_mult } );
    }
    return out;
}
} // namespace

thread_local const damage_context_scope *damage_context_scope::current = nullptr;
thread_local damage_packet_scope *damage_packet_scope::current = nullptr;

damage_context_scope::damage_context_scope( global_variables::impl_t values, bool trace,
        std::optional<creature_ref> instigator ) :
    values( std::move( values ) ), trace( trace ), instigator( instigator ),
    previous( current )
{
    current = this;
}

damage_context_scope::~damage_context_scope()
{
    current = previous;
}

void damage_context_scope::set( global_variables::impl_t next, bool trace_packet )
{
    values = std::move( next );
    trace = trace_packet;
}

std::optional<damage_transaction_view> damage_context_scope::result() const
{
    return completed;
}

struct damage_transaction_manager::impl {
    struct killer_hold {
        creature_ref victim;
        shared_ptr_fast<Creature> victim_hold;
        shared_ptr_fast<Creature> source_hold;
    };
    int64_t next_id = 1;
    int active = 0;
    std::map<int64_t, damage_transaction_view> history;
    std::map<int64_t, const damage_transaction_view *> active_views;
    std::vector<killer_hold> killers;

    void retain( const creature_ref &victim, const shared_ptr_fast<Creature> &target,
                 const shared_ptr_fast<Creature> &source ) {
        if( !target || !source || !target->is_dead_state() || execution_fatal::finalized( *target ) ||
            execution_fatal::pending( *target ) || target->get_killer() != source.get() ||
            ( !source->is_dead_state() && g->shared_from( *source ) ) ) {
            return;
        }
        if( std::none_of( killers.begin(), killers.end(), [&]( const killer_hold & hold ) {
        return hold.victim == victim;
    } ) ) {
            killers.push_back( { victim, target, source } );
        }
    }
};

damage_transaction_manager::damage_transaction_manager() : data( std::make_unique<impl>() ) {}
damage_transaction_manager::~damage_transaction_manager() = default;

damage_transaction_manager &get_damage_transactions()
{
    return g->abilities().damage_transactions();
}

bool damage_transaction_manager::processing() const
{
    return data->active != 0;
}

void damage_transaction_manager::clear()
{
    if( processing() ) {
        throw JsonError( "Cannot clear an active damage transaction" );
    }
    data->history.clear();
    release_settled_sources();
}

void damage_transaction_manager::release_settled_sources()
{
    auto &holds = data->killers;
    holds.erase( std::remove_if( holds.begin(), holds.end(), []( const auto & hold ) {
        return !hold.victim_hold->is_dead_state() || execution_fatal::finalized( *hold.victim_hold ) ||
               hold.victim.resolve_loaded() != hold.victim_hold.get();
    } ), holds.end() );
}

std::vector<damage_transaction_view> damage_transaction_manager::inspect_all() const
{
    std::vector<damage_transaction_view> out;
    for( const auto &entry : data->history ) {
        out.push_back( entry.second );
    }
    for( const auto &entry : data->active_views ) {
        if( data->history.count( entry.first ) == 0 ) {
            out.push_back( *entry.second );
        }
    }
    std::sort( out.begin(), out.end(), []( const auto & lhs, const auto & rhs ) {
        return lhs.id < rhs.id;
    } );
    return out;
}

void damage_transaction_manager::serialize( JsonOut &jsout ) const
{
    if( processing() ) {
        throw JsonError( "Cannot save a partially committed damage transaction" );
    }
    if( std::any_of( data->killers.begin(), data->killers.end(), []( const auto & hold ) {
    return hold.victim_hold->is_dead_state() && !execution_fatal::finalized( *hold.victim_hold ) &&
               hold.victim.resolve_loaded() == hold.victim_hold.get();
    } ) ) {
        throw JsonError( "Attributed damage fatalities must finish native cleanup before saving" );
    }
    jsout.start_object();
    jsout.member( "next_id", data->next_id );
    jsout.end_object();
}

void damage_transaction_manager::deserialize( const JsonObject &jo )
{
    if( processing() ) {
        jo.throw_error( "Cannot load inside a damage transaction" );
    }
    int64_t next = 1;
    jo.read( "next_id", next, true );
    if( next <= 0 || next >= std::numeric_limits<int64_t>::max() ) {
        jo.throw_error_at( "next_id", "invalid damage transaction counter" );
    }
    data->next_id = std::max( next, data->next_id );
    data->history.clear();
    release_settled_sources();
}

struct damage_participant_guard::impl {
    damage_transaction_manager *manager = nullptr;
    bool source_bound = false;
    creature_ref source;
    creature_ref target;
    shared_ptr_fast<Creature> source_hold;
    shared_ptr_fast<Creature> target_hold;
};

bool damage_packet_scope::interested( const Creature &defender )
{
    return g && !g->abilities().restoring() &&
           ( current || ( damage_context_scope::current && damage_context_scope::current->trace ) ||
             damage_rules::may_intercept( defender ) );
}

damage_participant_guard::damage_participant_guard( Creature *source, Creature &target )
{
    if( !damage_packet_scope::interested( target ) ) {
        return;
    }
    data = std::make_unique<impl>();
    data->source = tracked_ref( source );
    data->target = tracked_ref( &target );
    data->source_hold = source ? g->shared_from( *source ) : nullptr;
    data->target_hold = g->shared_from( target );
    if( !data->target_hold ) {
        data.reset();
        return;
    }
    data->source_bound = data->source_hold && data->source.resolve_loaded() == data->source_hold.get();
    data->manager = &get_damage_transactions();
    ++data->manager->data->active;
}

damage_participant_guard::~damage_participant_guard()
{
    if( data ) {
        data->manager->data->retain( data->target, data->target_hold, data->source_hold );
        --data->manager->data->active;
    }
}

bool damage_participant_guard::valid() const
{
    return !data || ( ( !data->target_hold ||
                        data->target.resolve_loaded() == data->target_hold.get() ) &&
                      ( !data->source_bound || data->source.resolve_loaded() == data->source_hold.get() ) );
}

struct damage_packet_scope::impl {
    damage_transaction_manager *manager = nullptr;
    damage_packet_scope *previous = nullptr;
    Creature *source = nullptr;
    Creature *defender = nullptr;
    shared_ptr_fast<Creature> source_hold;
    shared_ptr_fast<Creature> defender_hold;
    damage_transaction_view view;
    std::shared_ptr<damage_family> family;
    std::set<std::string> lineage;
    std::vector<const damage_rule *> resolved_rules;
    std::vector<reflected_packet> reflections;
    std::set<damage_type_id> original_types;
    bool finished = false;
    bool primary_finished = false;
    bool capture_hp = false;
    bool modified = false;
    bool source_bound = false;
    std::string source_kind;
    std::string defender_kind;
    const damage_context_scope *command = nullptr;

    bool valid() {
        if( view.state == "rejected" ) {
            return false;
        }
        if( !primary_finished && !view.hp_committed && defender->is_dead_state() &&
            !g->executions().owns_fatal_gravity( *defender ) ) {
            view.reason = "defender_dead_before_commit";
            view.state = "rejected";
            return false;
        }
        if( view.defender.resolve_loaded() != defender ||
            ( source_bound && view.source.resolve_loaded() != source ) ) {
            view.reason = view.hp_committed ? "participant_unavailable_after_commit" :
                          "participant_unavailable";
            view.state = "rejected";
            return false;
        }
        return true;
    }

    dialogue context( const std::string &stage, const damage_instance &packet,
                      const damage_rule *rule = nullptr, const damage_rule_result *result = nullptr ) const {
        Creature *live_defender = view.defender.resolve_loaded();
        Creature *live_source = view.source.resolve_loaded();
        dialogue d( live_defender ? get_talker_for( live_defender ) : nullptr,
                    live_source ? get_talker_for( live_source ) : nullptr, {}, view.context );
        d.set_value( "damage_transaction", std::to_string( view.id ) );
        d.set_value( "damage_root", std::to_string( view.root ) );
        d.set_value( "damage_parent", std::to_string( view.parent ) );
        d.set_value( "damage_depth", view.depth );
        d.set_value( "damage_stage", stage );
        d.set_value( "damage_reason", view.reason );
        d.set_value( "damage_bodypart", view.bodypart.str() );
        d.set_value( "damage_source_kind", source_kind );
        d.set_value( "damage_defender_kind", defender_kind );
        d.set_value( "damage_source_present", live_source ? 1.0 : 0.0 );
        d.set_value( "damage_source_id", std::to_string( view.source.id ) );
        d.set_value( "damage_instigator_id", std::to_string( view.instigator.id ) );
        d.set_value( "damage_original_source_id", std::to_string( view.original_source.id ) );
        d.set_value( "damage_incoming", view.incoming );
        d.set_value( "damage_after_armor", view.after_armor );
        d.set_value( "damage_remaining", total( packet, *defender ) );
        d.set_value( "damage_absorbed", view.absorbed );
        d.set_value( "damage_reflected", view.reflected );
        d.set_value( "damage_hp_loss", view.hp_loss );
        d.set_value( "damage_rule", rule ? rule->id.str() : "" );
        if( result ) {
            d.set_value( "damage_rule_before", result->before );
            d.set_value( "damage_rule_after", result->after );
            d.set_value( "damage_rule_absorbed", result->absorbed );
            d.set_value( "damage_rule_reflected", result->reflected );
            std::set<std::string> paid_resources{ "mana", "stamina" };
            for( const auto &payment : result->payments ) {
                paid_resources.insert( payment.first );
            }
            for( const std::string &resource : paid_resources ) {
                const auto payment = result->payments.find( resource );
                d.set_value( "damage_rule_paid_" + resource,
                             payment == result->payments.end() ? 0.0 : static_cast<double>( payment->second ) );
            }
            d.set_value( "damage_rule_reason", result->reason );
        }
        std::map<damage_type_id, int> totals;
        for( const auto &type : original_types ) {
            totals[type] = 0;
        }
        for( const damage_unit &unit : packet ) {
            totals[unit.type] += effective( unit, *defender );
        }
        for( const auto &entry : totals ) {
            d.set_value( "damage_type_" + entry.first.str(), entry.second );
        }
        return d;
    }

    bool matches( const damage_rule &rule, const damage_instance &packet ) const {
        if( ( !rule.required_effect.is_empty() && !defender->has_effect( rule.required_effect ) ) ||
            ( !rule.required_application.is_empty() &&
              get_powers().application_value( *defender, rule.required_application ) == 0 ) ||
            ( !rule.required_area.is_empty() &&
              get_projections().area_value( *defender, rule.required_area ) == 0 ) ||
            ( !rule.defender_kinds.empty() && !rule.defender_kinds.count( defender_kind ) ) ||
            ( !rule.source_kinds.empty() && !rule.source_kinds.count( source_kind ) ) ||
            ( !rule.bodyparts.empty() && !rule.bodyparts.count( view.bodypart ) ) ) {
            return false;
        }
        if( !rule.spells.empty() ) {
            const auto it = view.context.find( "damage_spell" );
            if( it == view.context.end() || !rule.spells.count( spell_id( it->second.str() ) ) ) {
                return false;
            }
        }
        if( !rule.damage_types.empty() ) {
            if( rule.stage == "committed" ) {
                return std::any_of( original_types.begin(), original_types.end(), [&]( const auto & type ) {
                    return rule.damage_types.count( type ) != 0;
                } );
            }
            return std::any_of( packet.begin(), packet.end(), [&]( const damage_unit & unit ) {
                return rule.damage_types.count( unit.type ) != 0;
            } );
        }
        return true;
    }

    bool mutate( const damage_rule &rule, damage_instance &packet, damage_rule_result &result ) {
        for( const effect_on_condition_id &eoc : rule.mutate ) {
            dialogue d = context( rule.stage, packet, &rule );
            d.set_value( "damage_scale", 1.0 );
            d.set_value( "damage_limit", static_cast<double>( packet_limit ) );
            d.set_value( "damage_cancel", 0.0 );
            bool outputs_valid = true;
            double scale = 1;
            double cap = packet_limit;
            double cancel = 0;
            eoc->activate( d, true, [&]( const dialogue & out ) {
                for( const auto &field : {
                         std::make_pair( "damage_scale", &scale ),
                         std::make_pair( "damage_limit", &cap ), std::make_pair( "damage_cancel", &cancel )
                     } ) {
                    const diag_value *value = out.maybe_get_value( field.first );
                    if( !value || !value->is_dbl() || !std::isfinite( value->dbl() ) ) {
                        outputs_valid = false;
                    } else {
                        *field.second = value->dbl();
                    }
                }
            } );
            if( !valid() ) {
                return false;
            }
            if( !outputs_valid || scale < 0 || scale > 100 || cap < 0 || cap > packet_limit ||
                ( cancel != 0 && cancel != 1 ) ) {
                result.reason = "invalid_output";
                return false;
            }
            damage_instance candidate = packet;
            for( damage_unit &unit : candidate ) {
                if( rule.damage_types.empty() || rule.damage_types.count( unit.type ) ) {
                    unit.amount *= scale;
                }
            }
            if( !packet_valid( candidate ) ) {
                result.reason = "packet_limit";
                return false;
            }
            const int before = total( candidate, *defender, &rule );
            if( remove_points( candidate, *defender, rule, cancel ? before :
                               std::max( 0, before - static_cast<int>( cap ) ) ) < 0 ) {
                result.reason = "unrepresentable_edit";
                return false;
            }
            modified = modified || !( candidate == packet );
            packet = std::move( candidate );
        }
        return true;
    }

    int affordable( const damage_rule_action &action, int wanted, damage_rule_result &result ) const {
        if( action.resource == "none" ) {
            return wanted;
        }
        const power_balance balance = get_powers().balance( *defender, action.resource );
        if( !balance.reason.empty() ) {
            result.reason = balance.reason;
            return 0;
        }
        return std::min( wanted, balance.available() / action.cost_per_damage );
    }

    bool pay( const damage_rule_action &action, int points, damage_rule_result &result ) const {
        if( points == 0 || action.resource == "none" ) {
            return true;
        }
        const int paid = points * action.cost_per_damage;
        if( !get_powers().spend( *defender, action.resource, paid, "damage_defense" ) ) {
            result.reason = "resource_changed";
            return false;
        }
        result.payments[action.resource] += paid;
        return true;
    }

    void action( const damage_rule &rule, const damage_rule_action &action,
                 damage_instance &packet, damage_rule_result &result ) {
        dialogue d = context( rule.stage, packet, &rule );
        const double value = action.value.evaluate( d );
        if( !valid() ) {
            return;
        }
        if( !std::isfinite( value ) || value < 0 || value > packet_limit ||
            ( ( action.operation == "convert" || action.operation == "reflect" ) && value > 1 ) ||
            ( action.operation == "scale" && value > 100 ) ) {
            result.reason = "invalid_parameter";
            return;
        }
        const int before = total( packet, *defender, &rule );
        if( action.operation == "absorb" || action.operation == "reflect" ) {
            if( action.operation == "reflect" && ( !source_hold || !view.source.resolve() ||
                                                   !view.defender.resolve() ) ) {
                result.reason = "reflection_participant_unavailable";
                return;
            }
            const int wanted = action.operation == "absorb" ? std::min( before, static_cast<int>( value ) ) :
                               static_cast<int>( std::floor( before * value ) );
            const int points = affordable( action, wanted, result );
            if( points == 0 ) {
                if( result.reason.empty() ) {
                    result.reason = wanted == 0 ? "no_eligible_damage" : "insufficient_resource";
                }
                return;
            }
            // Work out the exact packet edit first. Payment and publication of
            // that edit are adjacent operations with no author callbacks between.
            damage_instance candidate = packet;
            damage_instance reflected;
            const int taken = remove_points( candidate, *defender, rule, points,
                                             action.operation == "reflect" ? &reflected : nullptr );
            if( taken < 0 || !packet_valid( candidate ) ) {
                result.reason = "unrepresentable_edit";
                return;
            }
            if( !pay( action, taken, result ) ) {
                return;
            }
            packet = std::move( candidate );
            modified = true;
            if( action.operation == "reflect" ) {
                result.reflected += taken;
                view.reflected += taken;
                reflections.push_back( { std::move( reflected ), rule.id.str() } );
            } else {
                result.absorbed += taken;
                view.absorbed += taken;
            }
            result.reason = "applied";
            return;
        }
        damage_instance candidate = packet;
        if( action.operation == "scale" ) {
            for( damage_unit &unit : candidate ) {
                if( rule.damage_types.empty() || rule.damage_types.count( unit.type ) ) {
                    unit.amount *= value;
                }
            }
        } else if( action.operation == "cap" || action.operation == "cancel" ) {
            if( remove_points( candidate, *defender, rule, action.operation == "cancel" ? before :
                               std::max( 0, before - static_cast<int>( value ) ) ) < 0 ) {
                result.reason = "unrepresentable_edit";
                return;
            }
        } else if( action.operation == "convert" ) {
            const size_t count = candidate.damage_units.size();
            for( size_t i = 0; i < count; ++i ) {
                if( candidate.damage_units[i].type != action.from ||
                    ( !rule.damage_types.empty() && !rule.damage_types.count( action.from ) ) ) {
                    continue;
                }
                damage_unit converted = candidate.damage_units[i];
                converted.type = action.to;
                converted.amount *= value;
                candidate.damage_units[i].amount *= 1.0 - value;
                candidate.damage_units.push_back( std::move( converted ) );
            }
        }
        if( !packet_valid( candidate ) ) {
            result.reason = "packet_limit";
            return;
        }
        packet = std::move( candidate );
        modified = true;
        result.reason = "applied";
    }

    bool stage( const std::string &stage, damage_instance &packet ) {
        if( !valid() ) {
            return false;
        }
        for( const damage_rule *rule : damage_rules::ordered() ) {
            if( rule->stage != stage || !matches( *rule, packet ) ) {
                continue;
            }
            if( view.rules.size() == rule_limit ) {
                view.truncated = true;
                break;
            }
            damage_rule_result result;
            result.rule = rule->id.str();
            result.stage = stage;
            result.before = total( packet, *defender );
            const std::string key = lineage_key( *rule, view.defender );
            if( lineage.count( key ) ) {
                result.reason = "ancestry_suppressed";
            } else {
                // Conditions are real dispatch, not previews. Mark the attempt
                // before evaluating a condition that may itself deal damage.
                lineage.insert( key );
                dialogue d = context( stage, packet, rule );
                const bool eligible = !rule->has_condition || rule->condition( d );
                if( !valid() ) {
                    result.reason = view.reason;
                } else if( !eligible ) {
                    result.reason = "condition_false";
                } else {
                    resolved_rules.push_back( rule );
                    if( mutate( *rule, packet, result ) ) {
                        for( const damage_rule_action &operation : rule->actions ) {
                            action( *rule, operation, packet, result );
                            if( !valid() ) {
                                break;
                            }
                        }
                    }
                    if( result.reason.empty() ) {
                        result.reason = "observed";
                    }
                }
            }
            result.after = total( packet, *defender );
            view.rules.push_back( std::move( result ) );
            if( !valid() ) {
                return false;
            }
        }
        return true;
    }

    void publish() {
        if( command && command->first_packet == view.id ) {
            command->completed = view;
        }
        manager->data->history[view.id] = view;
        while( manager->data->history.size() > 32 ) {
            manager->data->history.erase( manager->data->history.begin() );
        }
    }
};

damage_packet_scope::damage_packet_scope( Creature *source, Creature &defender, bodypart_id bp,
        const damage_instance &packet,
        const std::function<global_variables::impl_t()> &native_facts )
{
    if( !interested( defender ) || !g->shared_from( defender ) ) {
        return;
    }
    data = std::make_unique<impl>();
    data->manager = &get_damage_transactions();
    data->previous = current;
    data->source = source;
    data->defender = &defender;
    data->source_hold = source ? g->shared_from( *source ) : nullptr;
    data->defender_hold = g->shared_from( defender );
    data->source_kind = !source ? "environment" : data->source_hold ? kind( source ) : "untracked";
    data->defender_kind = kind( &defender );
    data->command = damage_context_scope::current;
    damage_transaction_view &v = data->view;
    if( data->manager->data->next_id >= std::numeric_limits<int64_t>::max() - 1 ) {
        throw JsonError( "Damage transaction identity counter exhausted" );
    }
    v.id = data->manager->data->next_id++;
    if( data->command && data->command->first_packet == 0 ) {
        data->command->first_packet = v.id;
    }
    v.source = tracked_ref( source );
    data->source_bound = data->source_hold && v.source.resolve_loaded() == source;
    v.instigator = damage_context_scope::current && damage_context_scope::current->instigator ?
                   *damage_context_scope::current->instigator : v.source;
    v.original_source = v.source;
    v.defender = tracked_ref( &defender );
    v.bodypart = bp.id();
    v.state = "resolving";
    v.incoming_components = components( packet );
    v.context = native_facts ? native_facts() : global_variables::impl_t();
    if( damage_context_scope::current ) {
        for( const auto &entry : damage_context_scope::current->values ) {
            v.context[entry.first] = entry.second;
        }
    }
    for( auto it = v.context.begin(); it != v.context.end(); ) {
        const std::string &key = it->first;
        if( key.compare( 0, 7, "damage_" ) == 0 && key != "damage_spell" && key != "damage_cause" &&
            key != "damage_reflection" && key != "damage_reflection_rule" ) {
            it = v.context.erase( it );
        } else {
            ++it;
        }
    }
    for( const damage_unit &unit : packet ) {
        data->original_types.insert( unit.type );
    }
    if( current && current->data ) {
        const impl &parent = *current->data;
        v.root = parent.view.root;
        v.parent = parent.view.id;
        v.depth = parent.view.depth + 1;
        v.original_source = parent.view.original_source;
        data->family = parent.family;
        data->lineage = parent.lineage;
        if( current->data->view.children.size() < family_limit ) {
            current->data->view.children.push_back( v.id );
        } else {
            current->data->view.truncated = true;
        }
    } else {
        v.root = v.id;
        data->family = std::make_shared<damage_family>();
    }
    ++data->manager->data->active;
    data->manager->data->active_views[v.id] = &v;
    current = this;
    if( v.depth > depth_limit || data->family->packets >= family_limit ) {
        v.state = "rejected";
        v.reason = "ancestry_budget";
    } else if( !packet_valid( packet ) || v.context.size() > 512 ) {
        v.state = "rejected";
        v.reason = "invalid_packet";
    } else {
        ++data->family->packets;
        v.incoming = total( packet, defender );
    }
}

damage_packet_scope::~damage_packet_scope()
{
    if( !data ) {
        return;
    }
    if( !data->finished ) {
        if( data->view.reason.empty() ) {
            data->view.reason = "callback_failed";
        }
        data->view.state = "failed";
        data->publish();
    }
    current = data->previous;
    data->manager->data->retain( data->view.defender, data->defender_hold, data->source_hold );
    data->manager->data->active_views.erase( data->view.id );
    --data->manager->data->active;
}

bool damage_packet_scope::enabled() const
{
    return static_cast<bool>( data );
}

bool damage_packet_scope::valid()
{
    return !data || data->valid();
}

bool damage_packet_scope::before_armor( damage_instance &packet )
{
    return !data || data->stage( "before_armor", packet );
}

bool damage_packet_scope::after_armor( damage_instance &packet )
{
    if( !data ) {
        return true;
    }
    if( !packet_valid( packet ) ) {
        data->view.state = "rejected";
        data->view.reason = "invalid_packet_after_armor";
        return false;
    }
    data->view.after_armor = total( packet, *data->defender );
    data->view.armor_evaluated = true;
    data->view.after_armor_components = components( packet );
    for( damage_unit const &unit : packet ) {
        data->original_types.insert( unit.type );
    }
    const bool result = data->stage( "after_armor", packet );
    if( data->modified ) {
        auto &units = packet.damage_units;
        units.erase( std::remove_if( units.begin(), units.end(), [&]( const damage_unit & unit ) {
            return effective( unit, *data->defender ) == 0;
        } ), units.end() );
    }
    return result;
}

bool damage_packet_scope::blocked( const damage_instance &packet ) const
{
    return data && data->modified && total( packet, *data->defender ) == 0;
}

void damage_packet_scope::arm_hp_capture()
{
    if( data ) {
        data->capture_hp = true;
    }
}

void damage_packet_scope::disarm_hp_capture()
{
    if( data ) {
        data->capture_hp = false;
    }
}

void record_damage_transaction_hp_loss( const Creature &target, int amount )
{
    damage_packet_scope *current = damage_packet_scope::current;
    if( current && current->data && current->data->capture_hp && current->data->defender == &target ) {
        // Consume before native HP events; their raw secondary damage is not the
        // original packet's HP commit, even when it targets the same creature.
        current->data->capture_hp = false;
        current->data->view.hp_committed = true;
        current->data->view.hp_loss += std::max( 0, amount );
    }
}

void damage_packet_scope::finish( const damage_instance &packet )
{
    if( !data || data->finished ) {
        return;
    }
    data->view.outgoing = total( packet, *data->defender );
    data->view.outgoing_components = components( packet );
    damage_instance observed = packet;
    const bool participants_valid = data->valid();
    data->primary_finished = true;
    if( participants_valid ) {
        data->stage( "committed", observed );
    }
    data->view.state = data->view.hp_committed || data->view.reason.empty() ? "committed" : "rejected";
    if( data->view.reason.empty() ) {
        data->view.reason = data->view.outgoing == 0 && data->modified ? "blocked" : "resolved";
    }
    // Observation happens after the primary HP capture, never as a hidden veto.
    for( const damage_rule *rule : data->resolved_rules ) {
        const auto found = std::find_if( data->view.rules.begin(),
        data->view.rules.end(), [&]( const auto & r ) {
            return r.rule == rule->id.str();
        } );
        for( const effect_on_condition_id &eoc : rule->on_resolved ) {
            if( !data->valid() ) {
                break;
            }
            dialogue d = data->context( "committed", packet, rule,
                                        found == data->view.rules.end() ? nullptr : &*found );
            eoc->activate( d );
        }
    }
    if( !data->valid() && data->view.hp_committed ) {
        data->view.state = "committed";
    }
    for( const reflected_packet &reflection : data->reflections ) {
        Creature *source = data->view.defender.resolve();
        Creature *target = data->view.source.resolve();
        if( !source || !target ) {
            continue;
        }
        const bodypart_id torso( "torso" );
        if( const Character *person = target->as_character(); person && !person->has_part( torso ) ) {
            continue;
        }
        global_variables::impl_t facts = data->view.context;
        facts["damage_reflection"] = diag_value{ 1.0 };
        facts["damage_reflection_rule"] = diag_value{ reflection.rule };
        const damage_context_scope context( std::move( facts ), true, data->view.defender );
        target->deal_damage( source, torso, reflection.packet );
    }
    data->finished = true;
    data->publish();
}

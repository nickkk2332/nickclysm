#include "execution_trigger_state.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cata_scope_helpers.h"
#include "creature.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "execution_combat.h"
#include "execution_trigger.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json.h"
#include "rng.h"
#include "talker.h"

execution_trigger_report execution_trigger_state::consider( execution_manager &manager,
        const execution_combat::attack_snapshot &s )
{
    if( selecting ) {
        execution_trigger_report report;
        report.attack = s.id;
        report.reason = "recursive_selection";
        return report;
    }
    last.emplace();
    execution_trigger_report &report = *last;
    report.attack = s.id;
    report.when = calendar::turn;
    report.actor = s.actor;
    report.target = s.target;
    report.facts = s.context();
    report.reason = "selection_aborted";
    selecting = true;
    const on_out_of_scope finish( [&]() {
        selecting = false;
        reports.push_back( report );
        if( reports.size() > 32 ) {
            reports.pop_front();
        }
    } );
    if( s.id == 0 || s.id == std::numeric_limits<uint64_t>::max() ) {
        report.reason = "invalid_contact";
        return report;
    }
    next_contact = std::max( next_contact, s.id + 1 );
    if( std::find( recent.begin(), recent.end(), s.id ) != recent.end() ) {
        report.reason = "duplicate_contact";
        return report;
    }
    recent.push_back( s.id );
    if( recent.size() > 128 ) {
        recent.pop_front();
    }
    Creature *actor = s.actor.resolve();
    Creature *target = s.fatal ? s.target.resolve_loaded() : s.target.resolve();
    if( !actor || !target || actor == target || actor->is_hallucination() ||
        target->is_hallucination() ) {
        report.reason = "participant_unavailable";
        return report;
    }
    const auto actor_hold = g->shared_from( *actor );
    // A raw fatal monster is excluded from the ordinary position lookup until reserved.
    const auto target_hold = s.fatal && target->as_monster() ?
                             get_creature_tracker().find_by_uid( s.target.id ) : g->shared_from( *target );
    if( !actor_hold || !target_hold ) {
        report.reason = "participant_unavailable";
        return report;
    }
    if( calendar::turn >= next_sweep ) {
        next_sweep = calendar::turn + time_duration::from_turns( 31536000 );
        for( auto it = cooldowns.begin(); it != cooldowns.end(); ) {
            if( it->second <= calendar::turn ) {
                it = cooldowns.erase( it );
            } else {
                next_sweep = std::min( next_sweep, it->second );
                ++it;
            }
        }
    }
    const auto rules = execution_triggers::candidates( s );
    report.considered = static_cast<int>( rules.size() );
    dialogue d( get_talker_for( actor ), get_talker_for( target ), {}, s.context() );
    d.set_value( "killed", 0.0 );
    d.set_value( "death_pending", 0.0 );
    const auto record = [&]( const execution_trigger & rule, const std::string & reason,
    std::optional<double> roll = std::nullopt ) {
        if( report.candidates.size() < 64 ) {
            report.candidates.push_back( { rule.id.str(), reason, rule.priority, rule.weight,
                                           rule.chance, roll } );
        }
    };
    size_t begin = 0;
    while( begin < rules.size() ) {
        size_t end = begin;
        while( end < rules.size() && rules[end]->priority == rules[begin]->priority ) {
            ++end;
        }
        std::vector<const execution_trigger *> passing;
        for( size_t n = begin; n < end; ++n ) {
            const execution_trigger &rule = *rules[n];
            d.set_value( "attack_trigger", rule.id.str() );
            std::string reason = rule.matches( s );
            if( reason.empty() && rule.cooldown_ticks > 0 ) {
                const std::string key = rule.cooldown_key( s );
                const auto cooldown = cooldowns.find( key );
                if( key.size() > 1024 ) {
                    reason = "cooldown_key_budget";
                } else if( cooldown != cooldowns.end() && cooldown->second > calendar::turn ) {
                    reason = "cooldown";
                } else if( cooldowns.size() >= 4096 && cooldown == cooldowns.end() ) {
                    reason = "cooldown_budget";
                }
            }
            if( reason.empty() ) {
                reason = s.fatal ? manager.check_fatal_start( rule.execution, d ) :
                         manager.check_start( rule.execution, d );
                if( reason == "eligible" ) {
                    reason.clear();
                }
            }
            if( reason.empty() && rule.condition && !rule.condition( d ) ) {
                reason = "condition";
            }
            std::optional<double> roll;
            if( reason.empty() ) {
                if( rule.chance == 0 ) {
                    reason = "chance_failed";
                } else if( rule.chance < 1 ) {
                    roll = rng_float( 0, 1 );
                    if( *roll >= rule.chance ) {
                        reason = "chance_failed";
                    }
                }
            }
            record( rule, reason.empty() ? "eligible" : reason, roll );
            if( reason.empty() ) {
                passing.push_back( &rule );
            }
        }
        if( !passing.empty() ) {
            int total = 0;
            for( const execution_trigger *rule : passing ) {
                total += rule->weight;
            }
            int choice = passing.size() == 1 ? 1 : rng( 1, total );
            report.total_weight = total;
            if( passing.size() > 1 ) {
                report.weighted_roll = choice;
            }
            const execution_trigger *selected = passing.front();
            for( const execution_trigger *rule : passing ) {
                choice -= rule->weight;
                if( choice <= 0 ) {
                    selected = rule;
                    break;
                }
            }
            d.set_value( "attack_trigger", selected->id.str() );
            const execution_start_result started = s.fatal ? manager.start_fatal( selected->execution, d ) :
                                                   manager.start( selected->execution, d );
            report.selected = selected->id.str();
            report.instance = started.id;
            report.reason = started.reason;
            for( auto &entry : report.candidates ) {
                if( entry.reason == "eligible" ) {
                    entry.reason = entry.rule == report.selected ? report.reason : "not_selected";
                }
            }
            if( started.id != 0 && selected->cooldown_ticks > 0 ) {
                const time_point until = calendar::turn + time_duration::from_turns( selected->cooldown_ticks );
                cooldowns[selected->cooldown_key( s )] = until;
                next_sweep = std::min( next_sweep, until );
            }
            for( size_t n = end; n < rules.size(); ++n ) {
                record( *rules[n], "lower_priority_not_evaluated" );
            }
            return report;
        }
        begin = end;
    }
    report.reason = "no_match";
    return report;
}

bool execution_trigger_state::is_selecting() const
{
    return selecting;
}

uint64_t execution_trigger_state::allocate_contact()
{
    if( next_contact == std::numeric_limits<uint64_t>::max() ) {
        throw JsonError( "Execution contact identity space exhausted" );
    }
    return next_contact++;
}

std::optional<execution_trigger_report> execution_trigger_state::inspect() const
{
    return last;
}

std::vector<execution_trigger_report> execution_trigger_state::history() const
{
    return { reports.begin(), reports.end() };
}

void execution_trigger_state::clear()
{
    cooldowns.clear();
    recent.clear();
    last.reset();
    reports.clear();
    selecting = false;
    next_sweep = calendar::before_time_starts;
}

void execution_trigger_state::serialize( JsonOut &out ) const
{
    out.start_object();
    out.member( "cooldowns", cooldowns );
    out.member( "next_contact", std::to_string( next_contact ) );
    out.end_object();
}

void execution_trigger_state::deserialize( const JsonObject &obj )
{
    clear();
    try {
        const std::string value = obj.get_string( "next_contact", "1" );
        size_t end = 0;
        const uint64_t saved = std::stoull( value, &end );
        if( end != value.size() || value.front() == '-' || saved == 0 ||
            saved == std::numeric_limits<uint64_t>::max() ) {
            obj.throw_error_at( "next_contact", "invalid saved execution contact counter" );
        }
        next_contact = std::max( next_contact, saved );
    } catch( const std::invalid_argument & ) {
        obj.throw_error_at( "next_contact", "invalid saved execution contact counter" );
    } catch( const std::out_of_range & ) {
        obj.throw_error_at( "next_contact", "invalid saved execution contact counter" );
    }
    obj.read( "cooldowns", cooldowns );
    if( cooldowns.size() > 4096 ) {
        obj.throw_error_at( "cooldowns", "too many execution trigger cooldowns" );
    }
    for( const auto &entry : cooldowns ) {
        if( entry.first.size() > 1024 || entry.second > calendar::turn +
            time_duration::from_turns( 31536000 ) ) {
            obj.throw_error_at( "cooldowns", "invalid saved execution trigger cooldown" );
        }
    }
}

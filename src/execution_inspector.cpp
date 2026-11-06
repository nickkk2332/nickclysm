#include "execution_inspector.h"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

#include "calendar.h"
#include "ability_runtime.h"
#include "cursesdef.h"
#include "damage_transaction.h"
#include "power_runtime.h"
#include "power_actor.h"
#include "power_projection.h"
#include "power_grant.h"
#include "execution_runtime.h"
#include "game.h"
#include "output.h"
#include "string_formatter.h"
#include "translations.h"
#include "uilist.h"

namespace
{
void values( std::ostream &out, const global_variables::impl_t &entries )
{
    for( const auto &entry : entries ) {
        out << entry.first << ": " << entry.second.to_string() << '\n';
    }
}

void show_detail( const std::string &text )
{
    uilist menu;
    menu.title = _( "Runtime details (read-only)" );
    for( const std::string &line : foldstring( text, std::max( 20, TERMX - 6 ) ) ) {
        menu.addentry( -1, true, -1, line.empty() ? " " : line );
    }
    // Navigation and selection only. No live advance, cancel, EOC or RNG calls.
    do {
        menu.query();
    } while( menu.ret >= 0 );
}
} // namespace

std::string execution_inspector::format_scene( const execution_inspection &scene )
{
    std::ostringstream out;
    const auto &view = scene.view;
    out << "Execution #" << view.id << " / " << view.definition.str() << '\n'
        << "state: " << view.state << "\nstep: " << view.step << "\nreason: " << view.reason
        << "\noutcome: " << view.outcome << "\n\nRuntime and participants\n";
    values( out, scene.details );
    out << "\nContact and local context\n";
    values( out, view.context );
    out << "\nRecorded choices (latest 128; fixed ranges do not draw RNG)\n";
    for( const auto &draw : scene.draws ) {
        out << "entry " << draw.entry << " / " << draw.step << " / " << draw.purpose << ": ["
            << draw.minimum << ", " << draw.maximum << "] -> " << draw.value
            << ( draw.sampled ? " (sampled)" : " (fixed)" ) << '\n';
    }
    out << "\nOwned narration\n";
    for( const auto &segment : scene.narration ) {
        out << to_turns<int>( segment.when - calendar::turn_zero ) << " / " << segment.entry
            << " / " << segment.role << " / " << segment.audience << ": " << segment.text << '\n';
    }
    out << "\nTranscript\n";
    for( const auto &line : view.transcript ) {
        out << line << '\n';
    }
    return out.str();
}

std::string execution_inspector::format_trigger( const execution_trigger_report &report )
{
    std::ostringstream out;
    out << "Contact #" << report.attack << " / turn "
        << to_turns<int>( report.when - calendar::turn_zero )
        << "\nactor UID: " << report.actor.id << " / " << report.actor.dimension.str()
        << "\ntarget UID: " << report.target.id << " / " << report.target.dimension.str()
        << "\nreason: " << report.reason << "\nselected: " << report.selected
        << "\ninstance: " << report.instance << "\nindexed candidates: " << report.considered
        << "\nrecorded candidates: " << report.candidates.size() << " (maximum 64)"
        << "\npassing weight: " << report.total_weight << "\nweighted draw: ";
    if( report.weighted_roll ) {
        out << *report.weighted_roll;
    } else {
        out << "not drawn";
    }
    out << "\n\nCaptured native facts\n";
    values( out, report.facts );
    out << "\nCandidate decisions (recorded, never reevaluated)\n";
    for( const auto &candidate : report.candidates ) {
        out << candidate.rule << ": " << candidate.reason << " / priority=" << candidate.priority
            << " / weight=" << candidate.weight << " / chance=" << candidate.chance << " / draw=";
        if( candidate.roll ) {
            out << *candidate.roll;
        } else {
            out << "not drawn";
        }
        out << '\n';
    }
    return out.str();
}

std::string execution_inspector::format_ability( const ability_view &v )
{
    std::ostringstream out;
    out << "Ability #" << v.id << " / " << v.ability.str()
        << "\nstate: " << v.state << "\nreason: " << v.reason
        << "\nroot: " << v.root << " / parent: " << v.parent << " / depth: " << v.depth
        << "\ninvocation: " << ( v.invocation == ability_invocation::action ? "action" : "effect_only" )
        << "\ncaster UID: " << v.caster.id << " / " << v.caster.dimension.str()
        << "\ntarget UID: " << v.target.id << " / " << v.target.dimension.str()
        << "\norigin: " << v.origin.to_string() << "\nselected: " << v.selected.to_string()
        << "\nreleased at: " << ( v.released_at ? v.released_at->to_string() : "not released" )
        << "\nstarted: " << to_turns<int>( v.started - calendar::turn_zero )
        << "\ndue: " << to_turns<int>( v.due - calendar::turn_zero )
        << "\nsampled windup ticks: " << v.windup_ticks << "\nspell level: " << v.spell_level
        << "\ncosts committed: " << v.costs_committed << "\npaid moves: " << v.paid_moves
        << "\npaid energy: " << v.paid_energy
        << "\nenergy reserved: " << v.energy_reserved << "\ncooldown group: " << v.cooldown.str()
        << "\npaid charges: " << v.paid_charges << "\neffect completed: " << v.effect_completed
        << "\ncontacts truncated: " << v.contacts_truncated << "\n\nRequest context and locals\n";
    values( out, v.context );
    out << "\nObserved primary native contacts\n";
    for( const auto &contact : v.contacts ) {
        out << "target UID: " << contact.target.id << " / " << contact.target.dimension.str() << '\n';
        values( out, contact.facts );
    }
    return out.str();
}

std::string execution_inspector::format_damage( const damage_transaction_view &v )
{
    std::ostringstream out;
    out << "Damage #" << v.id << "\nstate: " << v.state << "\nreason: " << v.reason
        << "\nroot: " << v.root << " / parent: " << v.parent << " / depth: " << v.depth
        << "\nsource UID: " << v.source.id << " / " << v.source.dimension.str()
        << "\ninstigator UID: " << v.instigator.id << " / " << v.instigator.dimension.str()
        << "\noriginal source UID: " << v.original_source.id << " / " << v.original_source.dimension.str()
        << "\ndefender UID: " << v.defender.id << " / " << v.defender.dimension.str()
        << "\nbodypart: " << v.bodypart.str() << "\nincoming budget: " << v.incoming
        << "\narmor evaluated: " << v.armor_evaluated << "\nafter armor budget: " << v.after_armor
        << "\noutgoing budget: " << v.outgoing << "\nabsorbed: " << v.absorbed
        << "\nreflected: " << v.reflected << "\nHP committed: " << v.hp_committed
        << "\nactual HP loss: " << v.hp_loss << "\nrecords truncated: " << v.truncated;
    const auto packet = [&]( const char *label, const auto & units ) {
        out << "\n\n" << label << '\n';
        for( const auto &unit : units ) {
            out << unit.type.str() << ": amount=" << unit.amount << " / penetration="
                << unit.armor_penetration << " / armor multiplier=" << unit.armor_multiplier
                << " / damage multiplier=" << unit.damage_multiplier
                << " / unconditional multiplier=" << unit.unconditional_multiplier << '\n';
        }
    };
    packet( "Incoming components", v.incoming_components );
    packet( "After armor components", v.after_armor_components );
    packet( "Outgoing components", v.outgoing_components );
    out << "\nRecorded rule decisions (never reevaluated)\n";
    for( const auto &rule : v.rules ) {
        out << rule.rule << " / " << rule.stage << ": " << rule.reason << " / "
            << rule.before << " -> " << rule.after << " / absorbed=" << rule.absorbed
            << " / reflected=" << rule.reflected;
        for( const auto &payment : rule.payments ) {
            out << " / paid " << payment.first << '=' << payment.second;
        }
        out << '\n';
    }
    out << "\nChildren:";
    for( const auto child : v.children ) {
        out << ' ' << child;
    }
    out << "\n\nCaptured context\n";
    values( out, v.context );
    return out.str();
}

std::string execution_inspector::format_application( const power_application_view &v )
{
    std::ostringstream out;
    out << "Application #" << v.id << " / " << v.application.str()
        << "\nstate: " << v.state << "\nreason: " << v.reason
        << "\nroot: " << v.root << " / parent: " << v.parent << " / depth: " << v.depth
        << "\nowner UID: " << v.owner.id << " / " << v.owner.dimension.str()
        << "\ntarget UID: " << v.target.id << " / " << v.target.dimension.str()
        << "\nmagnitude: " << v.magnitude << "\norigin: " << v.origin.to_string()
        << "\nstarted: " << to_turn<int>( v.started ) << "\nexpires: " << to_turn<int>( v.expires )
        << "\nnext tick: " << to_turn<int>( v.next_tick ) << "\ntick count: " << v.tick_count
        << "\npaid start: " << v.paid_start << "\npaid upkeep: " << v.paid_upkeep
        << "\n\nCaptured context and owned locals\n";
    values( out, v.context );
    return out.str();
}

std::string execution_inspector::format_power_accounts( const power_manager &powers )
{
    std::ostringstream out;
    out << "Custom pools (projected at current world time; read-only)\n";
    for( const auto &p : powers.inspect_pools() ) {
        out << p.actor.id << " / " << p.actor.dimension.str() << " / " << p.resource
            << ": total=" << p.balance.total << " / available=" << p.balance.available()
            << " / reserved=" << p.balance.reserved << " / capacity=" << p.balance.capacity
            << " / reason=" << p.balance.reason << '\n';
    }
    out << "\nAbility reservations (claims, not payment)\n";
    for( const auto &r : powers.inspect_reservations() ) {
        out << "ability #" << r.token << " / actor=" << r.actor.id << " / " << r.resource
            << " / amount=" << r.amount << '\n';
    }
    out << "\nCooldown groups and sequential charge recharge\n";
    for( const auto &c : powers.inspect_cooldowns() ) {
        out << c.actor.id << " / " << c.actor.dimension.str() << " / " << c.group.str()
            << ": charges=" << c.charges << '/' << c.capacity << " / cooldown=" << c.cooldown_remaining
            << " / next charge=" << c.recharge_remaining << " / reason=" << c.reason << '\n';
    }
    out << "\nLatest 64 accounting operations (no hook reevaluation)\n";
    for( const auto &change : powers.history() ) {
        out << to_turn<int>( change.when ) << " / actor=" << change.actor.id << " / " << change.kind
            << " / " << change.name << " / " << change.cause << ": " << change.before << " -> "
            << change.after << " / token=" << change.token << '\n';
    }
    return out.str();
}

std::string execution_inspector::format_grant( const power_grant_view &v )
{
    std::ostringstream out;
    out << "Grants / application #" << v.application << " / " << v.definition.str()
        << "\nstate=" << v.state << " / reason=" << v.reason
        << "\nowner=" << static_cast<int>( v.owner.type ) << ":" << v.owner.id
        << " @ " << v.owner.dimension.str()
        << "\ntarget=" << static_cast<int>( v.target.type ) << ":" << v.target.id
        << " @ " << v.target.dimension.str()
        << "\nform=" << v.form << " / effects installed=" << v.effects
        << " / equipment issued=" << v.equipment << "\nstowed=" << v.stowed
        << " / restored=" << v.restored << " / dropped=" << v.dropped << "\nStored originals:";
    for( const auto &original : v.originals ) {
        out << "\n" << original.first << " / " << original.second.str();
    }
    return out.str();
}

std::string execution_inspector::format_projection( const power_projection_view &v )
{
    std::ostringstream out;
    out << "Projection #" << v.id << " / " << v.definition.str() << " / " << v.kind
        << "\nstate=" << v.state << " / reason=" << v.reason << " / owner=" << v.owner.id
        << " / target=" << v.target.id << "\nroot=" << v.root << " / parent=" << v.parent
        << " / application=" << v.application << "\nstrength=" << v.strength << "/" << v.initial_strength
        << " / travelled=" << v.travelled << " / ticks=" << v.tick_count
        << "\nrelease=" << to_turn<int>( v.release ) << " / expires=" << to_turn<int>( v.expires )
        << " / next pulse=" << to_turn<int>( v.next_pulse )
        << "\npaid start=" << v.paid_start << " / upkeep=" << v.paid_upkeep << " / HP loss=" << v.hp_loss
        << "\nposition=" << v.position.to_string_writable() << " / members=" << v.members.size()
        << " / truncated=" << v.members_truncated << "\nCells:";
    for( const auto &point : v.cells ) {
        out << " " << point.to_string_writable();
    }
    out << "\nMembers:";
    for( const auto &ref : v.members ) {
        out << " " << ref.id;
    }
    return out.str();
}

void execution_inspector::show()
{
    while( true ) {
        std::vector<std::pair<std::string, std::string>> rows;
        for( const auto &bond : get_power_actors().inspect() ) {
            std::ostringstream detail;
            detail << "Bond #" << bond.id << " / " << bond.definition.str()
                   << "\nowner=" << bond.owner.id << " / summon=" << bond.summon.id
                   << " / target=" << bond.target.id << "\ncommand=" << bond.command
                   << " / state=" << bond.state << " / reason=" << bond.reason
                   << "\nexpires=" << to_turn<int>( bond.expires );
            rows.emplace_back( string_format( _( "Summon bond #%lld: %s [%s]" ), bond.id,
                                              bond.definition.str(), bond.command ), detail.str() );
        }
        for( const auto &motion : get_power_actors().motions() ) {
            std::ostringstream detail;
            detail << "Instigator=" << motion.instigator.id << " / moved=" << motion.target.id
                   << " / cause=" << motion.cause << "\nreason=" << motion.reason
                   << " / velocity=" << motion.velocity << " -> " << motion.remaining_velocity
                   << "\nHP loss=" << motion.hp_loss << " / fell=" << motion.fell
                   << " / landed=" << motion.landed << "\nPath:";
            for( const auto &point : motion.path ) {
                detail << " " << point.to_string_writable();
            }
            for( const auto &impact : motion.impacts ) {
                detail << "\n" << impact.kind << " @ " << impact.position.to_string_writable()
                       << " / collider=" << impact.collider.id << " / force=" << impact.force
                       << " / moved HP loss=" << impact.moved_hp_loss
                       << " / collider HP loss=" << impact.collider_hp_loss << " / broken=" << impact.broken;
            }
            rows.emplace_back( string_format( _( "Power movement: %s" ), motion.reason ), detail.str() );
        }
        for( const auto &grant : get_power_grants().inspect() ) {
            rows.emplace_back( string_format( _( "Power grants #%lld: %s [%s]" ), grant.application,
                                              grant.definition.str(), grant.state ), format_grant( grant ) );
        }
        for( const auto &projection : get_projections().inspect() ) {
            rows.emplace_back( string_format( _( "Projection #%lld: %s [%s]" ), projection.id,
                                              projection.definition.str(), projection.state ), format_projection( projection ) );
        }
        for( const auto &contact : get_projections().contacts() ) {
            std::ostringstream out;
            out << "Projection #" << contact.projection << " / other #" << contact.other
                << " / target=" << contact.target.id << " / " << contact.reason
                << " @ " << contact.position.to_string_writable() << "\nstrength=" << contact.before
                << " -> " << contact.after << " / HP loss=" << contact.hp_loss;
            rows.emplace_back( _( "Projection contact" ), out.str() );
        }
        const power_manager &powers = get_powers();
        for( const auto &application : powers.inspect_applications() ) {
            rows.emplace_back( string_format( _( "Application #%lld: %s [%s] %s" ), application.id,
                                              application.application.str(), application.state, application.reason ),
                               format_application( application ) );
        }
        if( !powers.inspect_pools().empty() || !powers.inspect_reservations().empty() ||
            !powers.inspect_cooldowns().empty() || !powers.history().empty() ) {
            rows.emplace_back( _( "Power resources, reservations and cooldowns" ),
                               format_power_accounts( powers ) );
        }

        for( const auto &damage : get_damage_transactions().inspect_all() ) {
            rows.emplace_back( string_format( _( "Damage #%lld: %s (%s)" ), damage.id,
                                              damage.state, damage.reason ), format_damage( damage ) );
        }
        for( const auto &ability : g->abilities().inspect_all() ) {
            rows.emplace_back( string_format( _( "Ability #%lld: %s [%s] %s" ), ability.id,
                                              ability.ability.str(), ability.state, ability.reason ), format_ability( ability ) );
        }
        for( const auto &scene : g->executions().inspect_all() ) {
            rows.emplace_back( string_format( _( "Scene #%lld: %s [%s] %s" ), scene.view.id,
                                              scene.view.definition.str(), scene.view.state, scene.view.step ), format_scene( scene ) );
        }
        const auto history = g->executions().trigger_history();
        for( auto it = history.rbegin(); it != history.rend(); ++it ) {
            rows.emplace_back( string_format( _( "Contact #%llu: %s (%s)" ), it->attack,
                                              it->selected, it->reason ), format_trigger( *it ) );
        }
        uilist menu;
        menu.title = _( "Execution and powers inspector (read-only)" );
        menu.text =
            _( "Active/recent applications, grants, resources, damage, abilities, scenes and contacts. Inspection does not run conditions, consume randomness, or advance time." );
        if( rows.empty() ) {
            menu.addentry( -1, false, -1,
                           _( "No recorded powers, damage, abilities, execution scenes or contacts." ) );
        }
        for( size_t n = 0; n < rows.size(); ++n ) {
            menu.addentry( static_cast<int>( n ), true, -1, rows[n].first );
        }
        menu.query();
        if( menu.ret < 0 ) {
            return;
        }
        show_detail( rows[menu.ret].second );
    }
}

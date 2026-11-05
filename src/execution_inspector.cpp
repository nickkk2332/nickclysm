#include "execution_inspector.h"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cursesdef.h"
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
    menu.title = _( "Execution details (read-only)" );
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

void execution_inspector::show()
{
    while( true ) {
        std::vector<std::pair<std::string, std::string>> rows;
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
        menu.title = _( "Execution inspector (read-only)" );
        menu.text =
            _( "Active/recent scenes and the latest 32 contacts. Inspection does not run conditions, consume randomness, or advance time." );
        if( rows.empty() ) {
            menu.addentry( -1, false, -1, _( "No recorded execution scenes or contacts." ) );
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

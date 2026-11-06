#include "power_projection.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

#include "ability_runtime.h"
#include "bodypart.h"
#include "cata_scope_helpers.h"
#include "creature.h"
#include "creature_tracker.h"
#include "damage_transaction.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "execution_combat.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "incapacitation.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "power_runtime.h"
#include "talker.h"

static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );

namespace
{
constexpr size_t max_active = 128;
constexpr size_t max_history = 32;
constexpr size_t max_members = 64;
constexpr int max_depth = 8;
constexpr int max_family = 64;
struct projection_state {
    power_projection_view view;
    int revision = 1;
    time_point last_tick = calendar::before_time_starts;
    std::string cancel_reason;
    bool live() const {
        return cancel_reason.empty() && ( view.state == "pending" || view.state == "charging" ||
                                          view.state == "active" );
    }
};
bool ref_less( const creature_ref &a, const creature_ref &b )
{
    // NOLINTBEGIN(cata-use-localized-sorting)
    return std::make_tuple( a.dimension.str(), a.type, a.id ) <
           std::make_tuple( b.dimension.str(), b.type, b.id );
    // NOLINTEND(cata-use-localized-sorting)
}
bool real( const Creature *who )
{
    return who && !who->is_fake() && !who->is_hallucination() &&
           get_map().inbounds( who->pos_bub() );
}
bool local_type( const diag_value &a, const diag_value &b )
{
    return a.data.index() == b.data.index() && ( !a.is_dbl() || std::isfinite( a.dbl() ) );
}
} // namespace

struct power_projection_manager::impl {
    std::map<int64_t, projection_state> objects;
    std::map<int64_t, int> roots;
    std::vector<power_projection_contact> history;
    int64_t next_id = 1;
    int64_t current = 0;
    bool busy = false;

    bool parent_live( const projection_state &p ) const {
        return p.view.application == 0 || get_powers().owns_application( p.view.application, p.view.owner );
    }
    std::string invalid( const projection_state &p ) const {
        if( !p.cancel_reason.empty() ) {
            return p.cancel_reason;
        }
        const power_projection_view &v = p.view;
        if( !v.definition.is_valid() || v.definition->revision != p.revision ||
            v.definition->kind != v.kind ) {
            return "incompatible_definition";
        }
        const power_projection &def = *v.definition;
        const Creature *owner = v.owner.resolve();
        if( !real( owner ) ) {
            return "owner_unavailable";
        }
        if( !parent_live( p ) ) {
            return "application_ended";
        }
        if( calendar::turn >= v.expires ) {
            return "expired";
        }
        if( v.state != "pending" && p.last_tick != calendar::turn &&
            p.last_tick + 1_turns != calendar::turn ) {
            return "time_skipped";
        }
        if( def.interrupt_displacement && owner->pos_abs() != v.origin ) {
            return "displaced";
        }
        if( def.interrupt_incapacitated && ( incapacitation::unconscious( *owner ) ||
                                             owner->has_effect( effect_stunned ) || owner->has_effect( effect_sleep ) ||
                                             owner->has_effect( effect_narcosis ) ) ) {
            return "incapacitated";
        }
        if( def.track_target && !real( v.target.resolve() ) ) {
            return "target_unavailable";
        }
        const tripoint_abs_ms anchor = def.follow_owner ? owner->pos_abs() : v.position;
        const tripoint_abs_ms selected = def.track_target ? v.target.resolve()->pos_abs() : v.selected;
        if( !get_map().inbounds( get_map().get_bub( anchor ) ) ||
            ( v.kind != "area" && ( anchor.z() != selected.z() ||
                                    !get_map().inbounds( get_map().get_bub( selected ) ) ) ) ) {
            return "geometry_unavailable";
        }
        return {};
    }
    std::vector<tripoint_abs_ms> geometry( const projection_state &p ) const {
        std::vector<tripoint_abs_ms> out;
        if( !p.live() || !invalid( p ).empty() ) {
            return out;
        }
        const power_projection_view &v = p.view;
        const power_projection &def = *v.definition;
        const map &here = get_map();
        const Creature *owner = v.owner.resolve();
        const tripoint_abs_ms origin = def.follow_owner ? owner->pos_abs() : v.position;
        const tripoint_abs_ms selected = def.track_target ? v.target.resolve()->pos_abs() : v.selected;
        if( v.kind == "area" ) {
            for( const auto &point : here.points_in_radius( here.get_bub( origin ), def.radius ) ) {
                if( here.inbounds( point ) && ( !def.line_of_sight ||
                                                here.sees( here.get_bub( origin ), point, def.radius ) ) ) {
                    out.push_back( here.get_abs( point ) );
                }
            }
        } else if( v.kind == "projectile" && v.state == "active" ) {
            out.push_back( v.position );
        } else {
            int count = 0;
            for( const auto &point : line_to( here.get_bub( origin ), here.get_bub( selected ) ) ) {
                if( ++count > def.range || !here.inbounds( point ) ) {
                    break;
                }
                out.push_back( here.get_abs( point ) );
                if( here.impassable( point ) ) {
                    break;
                }
            }
        }
        return out;
    }
    bool affects( const projection_state &p, const Creature &target ) const {
        const Creature *owner = p.view.owner.resolve();
        if( !owner || !real( &target ) || target.is_dead_state() ) {
            return false;
        }
        const std::string &filter = p.view.definition->affects;
        if( filter == "all" ) {
            return true;
        }
        const Creature::Attitude relation = owner->attitude_to( target );
        return filter == "friendly" ? &target == owner || relation == Creature::Attitude::FRIENDLY :
               &target != owner && relation == Creature::Attitude::HOSTILE;
    }
    void contact( power_projection_contact result ) {
        history.push_back( std::move( result ) );
        if( history.size() > 64 ) {
            history.erase( history.begin() );
        }
    }
    void callbacks( projection_state &p, const std::vector<effect_on_condition_id> &hooks,
                    const creature_ref &target = {}, const std::string &event = "",
                    const tripoint_abs_ms &point = tripoint_abs_ms(), int64_t other = 0,
                    int hp_loss = 0, bool terminal = false ) {
        for( const auto &eoc : hooks ) {
            if( !terminal && ( !p.live() || !invalid( p ).empty() ) ) {
                return;
            }
            Creature *source = p.view.owner.resolve();
            Creature *victim = target.resolve_loaded();
            const auto source_hold = source ? g->shared_from( *source ) : nullptr;
            const auto target_hold = victim ? g->shared_from( *victim ) : nullptr;
            if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                throw JsonError( "Invalid projection hook" );
            }
            dialogue d( source_hold ? get_talker_for( source_hold.get() ) : nullptr,
                        target_hold ? get_talker_for( target_hold.get() ) : nullptr, {}, p.view.context );
            const power_projection_view &v = p.view;
            d.set_value( "power_projection", std::to_string( v.id ) );
            d.set_value( "projection_definition", v.definition.str() );
            d.set_value( "projection_root", std::to_string( v.root ) );
            d.set_value( "projection_parent", std::to_string( v.parent ) );
            d.set_value( "projection_application", std::to_string( v.application ) );
            d.set_value( "projection_state", v.state );
            d.set_value( "projection_reason", p.cancel_reason.empty() ? v.reason : p.cancel_reason );
            d.set_value( "projection_event", event );
            d.set_value( "projection_position", point );
            d.set_value( "projection_origin", v.origin );
            d.set_value( "projection_selected", v.selected );
            d.set_value( "projection_other", std::to_string( other ) );
            d.set_value( "projection_strength", v.strength );
            d.set_value( "projection_tick", v.tick_count );
            d.set_value( "projection_hp_loss", hp_loss );
            d.set_value( "projection_owner_present", source_hold ? 1 : 0 );
            d.set_value( "projection_target_present", target_hold ? 1 : 0 );
            current = v.id;
            eoc->activate( d, true, [&]( const dialogue & done ) {
                for( const auto &entry : v.definition->locals ) {
                    const auto found = done.get_context().find( entry.first );
                    if( found != done.get_context().end() && local_type( found->second, entry.second ) ) {
                        p.view.context[entry.first] = found->second;
                    }
                }
            } );
        }
    }
    void mark( projection_state &p, const std::string &reason ) {
        if( p.live() ) {
            p.cancel_reason = reason.empty() ? "dispelled" : reason;
        }
    }
    void finish( projection_state &p ) {
        if( p.view.state == "ended" ) {
            return;
        }
        p.view.state = "ended";
        p.view.reason = p.cancel_reason.empty() ? "ended" : p.cancel_reason;
        p.view.cells.clear();
        const auto members = std::move( p.view.members );
        p.view.members.clear();
        if( !p.view.definition.is_valid() ) {
            return;
        }
        std::exception_ptr failure;
        for( const creature_ref &ref : members ) {
            try {
                callbacks( p, p.view.definition->on_exit, ref, "exit", p.view.position, 0, 0, true );
            } catch( ... ) {
                if( !failure ) {
                    failure = std::current_exception();
                }
            }
        }
        try {
            callbacks( p, p.view.definition->on_end, {}, "end", p.view.position, 0, 0, true );
        } catch( ... ) {
            if( !failure ) {
                failure = std::current_exception();
            }
        }
        if( failure ) {
            std::rethrow_exception( failure );
        }
    }
    void settle() {
        // Hooks can mark an earlier object too. Membership is already inactive;
        // another safe pump settles remaining terminal observations.
        for( auto &entry : objects ) {
            auto &p = entry.second;
            if( p.view.state != "ended" && !p.cancel_reason.empty() ) {
                finish( p );
            }
        }
    }
    void trim() {
        size_t count = std::count_if( objects.begin(), objects.end(), []( const auto & entry ) {
            return entry.second.view.state == "ended";
        } );
        for( auto it = objects.begin(); it != objects.end() && count > max_history; ) {
            if( it->second.view.state == "ended" ) {
                it = objects.erase( it );
                --count;
            } else {
                ++it;
            }
        }
        for( auto it = roots.begin(); it != roots.end(); ) {
            if( std::none_of( objects.begin(), objects.end(), [&]( const auto & entry ) {
            return entry.second.view.root == it->first;
        } ) ) {
                it = roots.erase( it );
            } else {
                ++it;
            }
        }
    }
    void sync( projection_state &p ) {
        if( !p.live() || !invalid( p ).empty() ) {
            return;
        }
        p.view.cells = geometry( p );
        if( p.view.kind != "area" || p.view.state != "active" ) {
            return;
        }
        std::vector<creature_ref> now;
        for( const Creature &who : g->all_creatures() ) {
            if( affects( p, who ) &&
                std::find( p.view.cells.begin(), p.view.cells.end(), who.pos_abs() ) != p.view.cells.end() ) {
                now.push_back( creature_ref::bind( who ) );
            }
        }
        std::sort( now.begin(), now.end(), ref_less );
        p.view.members_truncated = now.size() > max_members;
        if( now.size() > max_members ) {
            now.resize( max_members );
        }
        const auto previous = p.view.members;
        p.view.members.clear();
        for( const creature_ref &ref : previous ) {
            if( std::find( now.begin(), now.end(), ref ) != now.end() ) {
                p.view.members.push_back( ref );
            }
        }
        std::exception_ptr failure;
        for( const creature_ref &ref : previous ) {
            if( std::find( now.begin(), now.end(), ref ) == now.end() ) {
                try {
                    callbacks( p, p.view.definition->on_exit, ref, "exit", p.view.position, 0, 0, true );
                } catch( ... ) {
                    if( !failure ) {
                        failure = std::current_exception();
                    }
                }
            }
        }
        if( failure ) {
            std::rethrow_exception( failure );
        }
        for( creature_ref const &ref : now ) {
            if( !p.live() || !invalid( p ).empty() ) {
                break;
            }
            if( std::find( previous.begin(), previous.end(), ref ) == previous.end() ) {
                const Creature *who = ref.resolve();
                const auto cells = geometry( p );
                if( who && affects( p, *who ) &&
                    std::find( cells.begin(), cells.end(), who->pos_abs() ) != cells.end() ) {
                    p.view.members.push_back( ref );
                    callbacks( p, p.view.definition->on_enter, ref, "enter", who->pos_abs() );
                }
            }
        }
    }
    void release( projection_state &p ) {
        p.view.state = "active";
        callbacks( p, p.view.definition->on_release, {}, "release", p.view.position );
    }
    void begin( projection_state &p ) {
        if( p.last_tick != calendar::turn && p.last_tick + 1_turns != calendar::turn ) {
            mark( p, "start_delayed" );
            return;
        }
        p.view.state = p.view.definition->charge_ticks ? "charging" : "active";
        callbacks( p, p.view.definition->on_start, {}, "start", p.view.position );
        if( p.live() && p.view.state == "active" ) {
            release( p );
        }
    }
    bool opponents( const projection_state &a, const projection_state &b ) const {
        if( a.view.owner == b.view.owner ) {
            return false;
        }
        const Creature *first = a.view.owner.resolve();
        const Creature *second = b.view.owner.resolve();
        return first && second && ( first->attitude_to( *second ) == Creature::Attitude::HOSTILE ||
                                    second->attitude_to( *first ) == Creature::Attitude::HOSTILE );
    }
    bool can_clash( const projection_state &a, const projection_state &b ) const {
        return a.live() && b.live() && a.view.state == "active" && b.view.state == "active" &&
               invalid( a ).empty() && invalid( b ).empty() && a.view.definition->contest &&
               b.view.definition->contest &&
               ( a.view.kind != "area" || a.view.definition->intercepts ) &&
               ( b.view.kind != "area" || b.view.definition->intercepts ) &&
               ( a.view.kind != "area" || b.view.kind != "area" ) && opponents( a, b );
    }
    void clash( projection_state &a, projection_state &b, const tripoint_abs_ms &point ) {
        const int first = a.view.strength;
        const int second = b.view.strength;
        a.view.strength = std::max( 0, first - second );
        b.view.strength = std::max( 0, second - first );
        if( !a.view.strength ) {
            mark( a, "contested" );
        }
        if( !b.view.strength ) {
            mark( b, "contested" );
        }
        contact( { a.view.id, b.view.id, {}, point, "clash", first, a.view.strength, 0 } );
        contact( { b.view.id, a.view.id, {}, point, "clash", second, b.view.strength, 0 } );
        callbacks( a, a.view.definition->on_clash, {}, "clash", point, b.view.id, 0, true );
        callbacks( b, b.view.definition->on_clash, {}, "clash", point, a.view.id, 0, true );
    }
    void hit( projection_state &p, const creature_ref &ref, const tripoint_abs_ms &point ) {
        Creature *owner = p.view.owner.resolve();
        Creature *target = ref.resolve();
        const auto owner_hold = owner ? g->shared_from( *owner ) : nullptr;
        const auto target_hold = target ? g->shared_from( *target ) : nullptr;
        if( !p.live() || !invalid( p ).empty() || !owner_hold || !target_hold ||
            target->pos_abs() != point || !affects( p, *target ) ) {
            return;
        }
        const int hp = target->get_hp();
        if( !p.view.definition->damage.empty() ) {
            damage_instance packet = p.view.definition->damage;
            packet.mult_damage( static_cast<double>( p.view.strength ) / p.view.initial_strength );
            global_variables::impl_t context = p.view.context;
            context["power_projection"] = diag_value{ std::to_string( p.view.id ) };
            context["projection_definition"] = diag_value{ p.view.definition.str() };
            context["projection_position"] = diag_value{ point };
            const damage_context_scope scope( context, true, p.view.owner );
            try {
                target->deal_damage( owner, p.view.definition->bodypart.id(), packet );
                if( ref.resolve_loaded() == target ) {
                    target->check_dead_state( &get_map() );
                }
            } catch( ... ) {
                const int loss = std::max( 0, hp - target_hold->get_hp() );
                p.view.hp_loss += loss;
                contact( { p.view.id, 0, ref, point, "exception", p.view.strength, p.view.strength, loss } );
                throw;
            }
        }
        const int loss = std::max( 0, hp - target_hold->get_hp() );
        p.view.hp_loss += loss;
        contact( { p.view.id, 0, ref, point, "hit", p.view.strength, p.view.strength, loss } );
        callbacks( p, p.view.definition->on_hit, ref, "hit", point, 0, loss, true );
    }
    void fly( projection_state &p ) {
        const power_projection &def = *p.view.definition;
        for( int step = 0; step < def.speed && p.live() && invalid( p ).empty(); ++step ) {
            if( p.view.travelled >= def.range ) {
                mark( p, "range_exhausted" );
                break;
            }
            const tripoint_abs_ms aim = def.track_target ? p.view.target.resolve()->pos_abs() : p.view.selected;
            if( aim == p.view.position ) {
                mark( p, "reached" );
                break;
            }
            const auto path = line_to( get_map().get_bub( p.view.position ), get_map().get_bub( aim ) );
            if( path.empty() ) {
                mark( p, "reached" );
                break;
            }
            const auto next = path.front();
            if( !get_map().inbounds( next ) ) {
                mark( p, "geometry_unavailable" );
                break;
            }
            p.view.position = get_map().get_abs( next );
            ++p.view.travelled;
            p.view.cells = { p.view.position };
            for( auto &entry : objects ) {
                auto &other = entry.second;
                if( other.view.id == p.view.id || !can_clash( p, other ) ) {
                    continue;
                }
                const auto cells = geometry( other );
                if( std::find( cells.begin(), cells.end(), p.view.position ) != cells.end() ) {
                    clash( p, other, p.view.position );
                }
                if( !p.live() || !invalid( p ).empty() ) {
                    break;
                }
            }
            if( !p.live() || !invalid( p ).empty() ) {
                break;
            }
            const tripoint_bub_ms cell = get_map().get_bub( p.view.position );
            if( get_map().impassable( cell ) ) {
                contact( { p.view.id, 0, {}, p.view.position, "terrain", p.view.strength, p.view.strength, 0 } );
                callbacks( p, def.on_hit, {}, "terrain", p.view.position );
                mark( p, "terrain" );
                break;
            }
            Creature *who = get_creature_tracker().creature_at( cell );
            if( who && real( who ) && !who->is_dead_state() &&
                !( creature_ref::bind( *who ) == p.view.owner ) ) {
                const creature_ref ref = creature_ref::bind( *who );
                if( affects( p, *who ) ) {
                    hit( p, ref, p.view.position );
                } else {
                    callbacks( p, def.on_hit, ref, "blocked", p.view.position );
                }
                mark( p, "contact" );
                break;
            }
            const tripoint_abs_ms live_aim = def.track_target ? p.view.target.resolve()->pos_abs() :
                                             p.view.selected;
            if( p.view.position == live_aim ) {
                callbacks( p, def.on_hit, {}, "reached", p.view.position );
                mark( p, "reached" );
                break;
            }
        }
    }
    void pulse( projection_state &p ) {
        if( p.view.kind == "projectile" ) {
            fly( p );
            return;
        }
        const auto cells = geometry( p );
        std::vector<creature_ref> targets;
        for( const Creature &who : g->all_creatures() ) {
            if( affects( p, who ) && std::find( cells.begin(), cells.end(), who.pos_abs() ) != cells.end() ) {
                targets.push_back( creature_ref::bind( who ) );
            }
        }
        std::sort( targets.begin(), targets.end(), ref_less );
        if( targets.size() > max_members ) {
            targets.resize( max_members );
            p.view.members_truncated = true;
        }
        for( creature_ref const &ref : targets ) {
            Creature *who = ref.resolve();
            if( !p.live() || !invalid( p ).empty() ) {
                break;
            }
            const auto current_cells = geometry( p );
            if( !who ||
                std::find( current_cells.begin(), current_cells.end(), who->pos_abs() ) == current_cells.end() ) {
                continue;
            }
            const tripoint_abs_ms point = who->pos_abs();
            if( p.view.kind == "area" ) {
                callbacks( p, p.view.definition->on_stay, ref, "stay", point );
            }
            hit( p, ref, point );
        }
    }
    void sweep( bool begin_pending = true ) {
        std::vector<int64_t> ids;
        ids.reserve( objects.size() );
        for( const auto &entry : objects ) {
            ids.push_back( entry.first );
        }
        for( int64_t id : ids ) {
            auto &p = objects.at( id );
            current = id;
            if( p.view.state == "ended" ) {
                continue;
            }
            const std::string reason = invalid( p );
            if( !reason.empty() ) {
                mark( p, reason );
                continue;
            }
            if( p.view.state == "pending" ) {
                if( !begin_pending ) {
                    continue;
                }
                begin( p );
            }
            sync( p );
        }
    }
    void fail( int64_t id ) {
        auto it = objects.find( id );
        if( it != objects.end() && it->second.view.state != "ended" ) {
            mark( it->second, "callback_exception" );
            try {
                finish( it->second );
            } catch( ... ) { }
        }
    }
};

power_projection_manager::power_projection_manager() : data( std::make_unique<impl>() ) {}
power_projection_manager::~power_projection_manager() = default;
std::string power_projection_manager::check( const power_projection_request &request ) const
{
    if( g->abilities().restoring() || g->abilities().stopping() ) {
        return "world_transition";
    }
    if( !request.definition.is_valid() ) {
        return "invalid_definition";
    }
    const power_projection &def = *request.definition;
    const Creature *owner = request.owner.resolve();
    if( !real( owner ) ) {
        return "owner_unavailable";
    }
    if( def.track_target && !real( request.target.resolve() ) ) {
        return "target_unavailable";
    }
    if( def.track_target && request.point ) {
        return "tracked_point_conflict";
    }
    const Creature *target = request.target.resolve();
    const auto point = request.point.value_or( target ? target->pos_abs() : owner->pos_abs() );
    if( point.z() != owner->pos_abs().z() || rl_dist( owner->pos_abs(), point ) > def.range ||
        !get_map().inbounds( get_map().get_bub( point ) ) ||
        ( def.kind != "area" && point == owner->pos_abs() ) ) {
        return "invalid_geometry";
    }
    if( request.strength && ( *request.strength < 1 || *request.strength > 1000000 ) ) {
        return "invalid_strength";
    }
    if( request.application < 0 || ( request.application &&
                                     !get_powers().owns_application( request.application, request.owner ) ) ) {
        return "invalid_application";
    }
    if( request.context.size() + def.locals.size() > 256 ) {
        return "context_budget";
    }
    if( data->next_id >= std::numeric_limits<int64_t>::max() - 1 ) {
        return "id_exhausted";
    }
    size_t active = 0;
    size_t owned = 0;
    for( const auto &entry : data->objects ) {
        if( entry.second.view.state != "ended" ) {
            ++active;
            if( entry.second.view.owner == request.owner &&
                entry.second.view.definition == request.definition ) {
                ++owned;
            }
        }
    }
    if( active >= max_active || owned >= static_cast<size_t>( def.max_per_owner ) ) {
        return "instance_budget";
    }
    const auto parent = data->objects.find( data->current );
    if( parent != data->objects.end() ) {
        if( !parent->second.live() ) {
            return "terminal_parent";
        }
        if( parent->second.view.application && request.application &&
            parent->second.view.application != request.application ) {
            return "application_mismatch";
        }
        if( parent->second.view.depth >= max_depth ||
            data->roots.at( parent->second.view.root ) >= max_family ) {
            return "ancestry_budget";
        }
    }
    const std::string cost = get_powers().check_cost( *owner, def.start_cost.resource,
                             def.start_cost.amount );
    if( !cost.empty() ) {
        return cost;
    }
    return get_powers().balance( *owner, def.upkeep.resource ).reason;
}
power_projection_admission power_projection_manager::create( const power_projection_request
        &request )
{
    const std::string reason = check( request );
    if( !reason.empty() ) {
        return { 0, reason };
    }
    projection_state p;
    power_projection_view &v = p.view;
    const power_projection &def = *request.definition;
    v.id = data->next_id++;
    v.root = v.id;
    const auto parent = data->objects.find( data->current );
    if( parent != data->objects.end() ) {
        v.parent = parent->first;
        v.root = parent->second.view.root;
        v.depth = parent->second.view.depth + 1;
    }
    v.definition = request.definition;
    p.revision = def.revision;
    v.owner = request.owner;
    v.target = request.target;
    v.application = request.application ? request.application : parent != data->objects.end() ?
                    parent->second.view.application : 0;
    v.kind = def.kind;
    v.origin = request.owner.resolve()->pos_abs();
    v.selected = request.point.value_or( request.target.resolve() ? request.target.resolve()->pos_abs()
                                         : v.origin );
    v.position = def.kind == "area" ? v.selected : v.origin;
    v.initial_strength = v.strength = request.strength.value_or( def.strength );
    v.started = p.last_tick = calendar::turn;
    v.release = calendar::turn + time_duration::from_turns( def.charge_ticks );
    v.expires = calendar::turn + time_duration::from_turns( def.duration_ticks );
    v.next_pulse = calendar::turn + time_duration::from_turns( std::max( 1, def.charge_ticks ) );
    v.context = request.context;
    for( const auto &local : def.locals ) {
        v.context[local.first] = local.second;
    }
    if( !get_powers().spend( *request.owner.resolve(), def.start_cost.resource, def.start_cost.amount,
                             "projection_start" ) ) {
        return { 0, "accounting_refused" };
    }
    v.paid_start = def.start_cost.amount;
    if( !data->roots.count( v.root ) ) {
        data->roots[v.root] = 0;
    }
    ++data->roots[v.root];
    const int64_t id = v.id;
    data->objects.emplace( id, std::move( p ) );
    return { id, "accepted" };
}
bool power_projection_manager::end( int64_t id, const std::string &reason )
{
    const auto it = data->objects.find( id );
    if( it == data->objects.end() || !it->second.live() ) {
        return false;
    }
    data->mark( it->second, reason );
    return true;
}
int power_projection_manager::dispel( const creature_ref &owner, const power_projection_id &filter,
                                      const std::string &tag )
{
    int count = 0;
    for( auto &entry : data->objects ) {
        const power_projection_view &v = entry.second.view;
        if( v.owner == owner && ( filter.is_empty() || v.definition == filter ) &&
            ( tag.empty() || ( v.definition.is_valid() && v.definition->tags.count( tag ) ) ) ) {
            count += end( entry.first, "dispelled" ) ? 1 : 0;
        }
    }
    return count;
}
void power_projection_manager::drain_starts()
{
    if( data->busy || g->abilities().processing() || g->abilities().restoring() ||
        g->executions().processing_actions() || execution_combat::attack_in_progress() ) {
        return;
    }
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->current = 0;
        data->busy = false;
    } );
    try {
        data->sweep();
        data->settle();
        data->trim();
    } catch( ... ) {
        data->fail( data->current );
        throw;
    }
}
void power_projection_manager::process()
{
    if( data->busy || g->abilities().processing() || g->abilities().restoring() ||
        g->executions().processing_actions() || execution_combat::attack_in_progress() ) {
        return;
    }
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->current = 0;
        data->busy = false;
    } );
    try {
        data->sweep();
        std::vector<int64_t> pulses;
        std::vector<int64_t> active;
        for( auto &entry : data->objects ) {
            auto &p = entry.second;
            data->current = entry.first;
            if( !p.live() || !data->invalid( p ).empty() || p.last_tick == calendar::turn ) {
                continue;
            }
            if( p.last_tick + 1_turns != calendar::turn ) {
                data->mark( p, "time_skipped" );
                continue;
            }
            p.last_tick = calendar::turn;
            if( p.view.state == "charging" && calendar::turn >= p.view.release ) {
                data->release( p );
            }
            if( !p.live() || !data->invalid( p ).empty() || p.view.state != "active" ) {
                continue;
            }
            data->sync( p );
            if( calendar::turn >= p.view.next_pulse ) {
                const power_cost &cost = p.view.definition->upkeep;
                if( !get_powers().spend( *p.view.owner.resolve(), cost.resource, cost.amount,
                                         "projection_upkeep" ) ) {
                    data->mark( p, "upkeep_unaffordable" );
                    continue;
                }
                p.view.paid_upkeep += cost.amount;
                ++p.view.tick_count;
                p.view.next_pulse = calendar::turn + time_duration::from_turns( p.view.definition->interval_ticks );
                pulses.push_back( entry.first );
            }
        }
        for( const auto &entry : data->objects ) {
            if( entry.second.live() && entry.second.view.state == "active" &&
                data->invalid( entry.second ).empty() ) {
                active.push_back( entry.first );
            }
        }
        // Resolve every overlap before committing any pulse damage.
        for( size_t i = 0; i < active.size(); ++i ) {
            auto &a = data->objects.at( active[i] );
            for( size_t j = i + 1; j < active.size(); ++j ) {
                auto &b = data->objects.at( active[j] );
                if( !data->can_clash( a, b ) ) {
                    continue;
                }
                const auto first = data->geometry( a );
                const auto second = data->geometry( b );
                const auto overlap = std::find_if( first.begin(), first.end(), [&]( const auto & point ) {
                    return std::find( second.begin(), second.end(), point ) != second.end();
                } );
                if( overlap != first.end() ) {
                    data->clash( a, b, *overlap );
                }
            }
        }
        for( int64_t id : pulses ) {
            data->current = id;
            auto &p = data->objects.at( id );
            if( p.live() && data->invalid( p ).empty() ) {
                data->pulse( p );
            }
        }
        data->sweep( false );
        data->settle();
        data->trim();
    } catch( ... ) {
        data->fail( data->current );
        throw;
    }
}
void power_projection_manager::notify_damage( const Creature &who, int amount )
{
    if( amount <= 0 ) {
        return;
    }
    const creature_ref ref = creature_ref::bind( who );
    for( auto &entry : data->objects ) {
        auto &p = entry.second;
        if( p.live() && p.view.owner == ref && p.view.definition.is_valid() &&
            p.view.definition->interrupt_damage ) {
            data->mark( p, "damaged" );
        }
    }
}
void power_projection_manager::retire_actor( const Creature &who )
{
    const creature_ref ref = creature_ref::bind( who );
    for( auto &entry : data->objects ) {
        if( entry.second.view.owner == ref ) {
            data->mark( entry.second, "owner_unavailable" );
        }
    }
}
void power_projection_manager::cancel_all( const std::string &reason )
{
    for( auto &entry : data->objects ) {
        data->mark( entry.second, reason );
    }
    drain_starts();
}
void power_projection_manager::clear()
{
    if( data->busy ) {
        cancel_all( "cleared" );
        return;
    }
    data->objects.clear();
    data->roots.clear();
    data->history.clear();
}
bool power_projection_manager::processing() const
{
    return data->busy;
}

double power_projection_manager::area_value( const Creature &target,
        const power_projection_id &filter,
        bool strength, const std::optional<creature_ref> &owner ) const
{
    double value = 0;
    for( const auto &entry : data->objects ) {
        const auto &p = entry.second;
        if( p.view.kind != "area" || p.view.state != "active" || !p.live() ||
            ( !filter.is_empty() && filter != p.view.definition ) || ( owner && !( p.view.owner == *owner ) ) ||
            !data->invalid( p ).empty() || !data->affects( p, target ) ) {
            continue;
        }
        const auto cells = data->geometry( p );
        if( std::find( cells.begin(), cells.end(), target.pos_abs() ) != cells.end() ) {
            value += strength ? p.view.strength : 1;
        }
    }
    return value;
}
double power_projection_manager::owned_value( const Creature &owner,
        const power_projection_id &filter, const std::string &field ) const
{
    double result = 0;
    for( const auto &entry : data->objects ) {
        const auto &p = entry.second;
        if( p.live() && data->invalid( p ).empty() && p.view.owner.resolve() == &owner &&
            ( filter.is_empty() || filter == p.view.definition ) ) {
            result += field == "strength" ? p.view.strength : field == "charging" ?
                      ( p.view.state == "charging" ? 1 : 0 ) : 1;
        }
    }
    return result;
}
std::vector<power_projection_view> power_projection_manager::inspect() const
{
    std::vector<power_projection_view> result;
    for( const auto &entry : data->objects ) {
        power_projection_view view = entry.second.view;
        if( view.reason.empty() ) {
            view.reason = entry.second.cancel_reason;
        }
        view.cells = data->geometry( entry.second );
        result.push_back( std::move( view ) );
    }
    return result;
}
std::vector<power_projection_contact> power_projection_manager::contacts() const
{
    return data->history;
}
std::vector<power_projection_tile> power_projection_manager::tiles() const
{
    std::vector<power_projection_tile> result;
    std::set<tripoint_abs_ms> drawn;
    for( const auto &entry : data->objects ) {
        const auto &p = entry.second;
        for( const auto &point : data->geometry( p ) ) {
            const tripoint_abs_ms center = p.view.definition->follow_owner ? p.view.owner.resolve()->pos_abs() :
                                           p.view.position;
            if( p.view.kind == "area" && rl_dist( center, point ) != p.view.definition->radius ) {
                continue;
            }
            if( drawn.insert( point ).second ) {
                result.push_back( { point, p.view.kind, p.view.state != "active" } );
            }
        }
    }
    return result;
}

void power_projection_manager::serialize( JsonOut &out ) const
{
    if( data->busy ) {
        throw JsonError( "Cannot save inside a projection callback" );
    }
    out.start_object();
    out.member( "next_id", data->next_id );
    out.member( "roots" );
    out.start_array();
    for( const auto &entry : data->roots ) {
        out.start_object();
        out.member( "id", entry.first );
        out.member( "requests", entry.second );
        out.end_object();
    }
    out.end_array();
    out.member( "objects" );
    out.start_array();
    for( const auto &entry : data->objects ) {
        const auto &p = entry.second;
        const power_projection_view &v = p.view;
        if( v.state == "ended" ) {
            continue;
        }
        out.start_object();
        out.member( "id", v.id );
        out.member( "root", v.root );
        out.member( "parent", v.parent );
        out.member( "depth", v.depth );
        out.member( "definition", v.definition );
        out.member( "revision", p.revision );
        out.member( "owner", v.owner );
        out.member( "target", v.target );
        out.member( "application", v.application );
        out.member( "kind", v.kind );
        out.member( "state", v.state );
        out.member( "cancel_reason", p.cancel_reason );
        out.member( "origin", v.origin );
        out.member( "position", v.position );
        out.member( "selected", v.selected );
        out.member( "started", v.started );
        out.member( "release", v.release );
        out.member( "expires", v.expires );
        out.member( "next_pulse", v.next_pulse );
        out.member( "last_tick", p.last_tick );
        out.member( "initial_strength", v.initial_strength );
        out.member( "strength", v.strength );
        out.member( "travelled", v.travelled );
        out.member( "tick_count", v.tick_count );
        out.member( "paid_start", v.paid_start );
        out.member( "paid_upkeep", v.paid_upkeep );
        out.member( "hp_loss", v.hp_loss );
        out.member( "members", v.members );
        out.member( "members_truncated", v.members_truncated );
        out.member( "context", v.context );
        out.end_object();
    }
    out.end_array();
    out.end_object();
}
void power_projection_manager::deserialize( const JsonObject &jo )
{
    if( data->busy ) {
        jo.throw_error( "Cannot load inside a projection callback" );
    }
    int64_t next = 1;
    jo.read( "next_id", next, true );
    if( next <= 0 || next >= std::numeric_limits<int64_t>::max() ) {
        jo.throw_error( "Invalid projection counter" );
    }
    std::map<int64_t, int> roots;
    for( const JsonObject root : jo.get_array( "roots" ) ) {
        int64_t id = 0;
        root.read( "id", id, true );
        const int requests = root.get_int( "requests" );
        if( id <= 0 || id >= next || requests < 1 || requests > max_family ||
            roots.size() >= max_active + max_history || !roots.emplace( id, requests ).second ) {
            root.throw_error( "Invalid projection family budget" );
        }
    }
    std::map<int64_t, projection_state> objects;
    std::map<int64_t, int> families;
    for( const JsonObject obj : jo.get_array( "objects" ) ) {
        projection_state p;
        power_projection_view &v = p.view;
        obj.read( "id", v.id, true );
        obj.read( "root", v.root, true );
        obj.read( "parent", v.parent, true );
        obj.read( "depth", v.depth, true );
        obj.read( "definition", v.definition, true );
        obj.read( "revision", p.revision, true );
        obj.read( "owner", v.owner, true );
        obj.read( "target", v.target, true );
        obj.read( "application", v.application, true );
        obj.read( "kind", v.kind, true );
        obj.read( "state", v.state, true );
        obj.read( "cancel_reason", p.cancel_reason, true );
        obj.read( "origin", v.origin, true );
        obj.read( "position", v.position, true );
        obj.read( "selected", v.selected, true );
        obj.read( "started", v.started, true );
        obj.read( "release", v.release, true );
        obj.read( "expires", v.expires, true );
        obj.read( "next_pulse", v.next_pulse, true );
        obj.read( "last_tick", p.last_tick, true );
        obj.read( "initial_strength", v.initial_strength, true );
        obj.read( "strength", v.strength, true );
        obj.read( "travelled", v.travelled, true );
        obj.read( "tick_count", v.tick_count, true );
        obj.read( "paid_start", v.paid_start, true );
        obj.read( "paid_upkeep", v.paid_upkeep, true );
        obj.read( "hp_loss", v.hp_loss, true );
        obj.read( "members", v.members, true );
        obj.read( "members_truncated", v.members_truncated, true );
        obj.read( "context", v.context, true );
        if( objects.size() >= max_active || v.id <= 0 || v.id >= next || v.root <= 0 || v.root > v.id ||
            !roots.count( v.root ) || v.parent < 0 || v.parent >= v.id || v.depth < 0 || v.depth > max_depth ||
            ( v.parent == 0 ) != ( v.depth == 0 ) || ( v.depth == 0 && v.root != v.id ) || v.application < 0 ||
            v.owner.id <= 0 || v.owner.type == creature_ref::kind::none || p.revision < 1 ||
            p.revision > 1000000 ||
            ( v.kind != "projectile" && v.kind != "beam" && v.kind != "area" ) ||
            ( v.state != "pending" && v.state != "charging" && v.state != "active" ) ||
            v.initial_strength < 1 || v.initial_strength > 1000000 || v.strength < 0 ||
            v.strength > v.initial_strength ||
            ( v.strength == 0 && p.cancel_reason.empty() ) || v.travelled < 0 || v.travelled > 60 ||
            v.tick_count < 0 || v.tick_count > 1000000 || v.paid_start < 0 || v.paid_start > 1000000 ||
            v.paid_upkeep < 0 || v.paid_upkeep > 1000000000000LL || v.hp_loss < 0 ||
            v.hp_loss > 64000000000000LL ||
            v.started > v.release || v.release >= v.expires || v.expires - v.started > 1000000_turns ||
            p.last_tick < v.started || p.last_tick >= v.expires || v.next_pulse < v.started ||
            v.origin.z() != v.position.z() || v.origin.z() != v.selected.z() ||
            v.members.size() > max_members || ( v.kind != "area" && !v.members.empty() ) ||
            v.context.size() > 256 ||
            ++families[v.root] > roots.at( v.root ) ) {
            obj.throw_error( "Invalid projection state" );
        }
        std::set<std::tuple<std::string, creature_ref::kind, int64_t>> seen;
        for( creature_ref const &ref : v.members ) {
            if( ref.id <= 0 || ref.type == creature_ref::kind::none || ref.dimension != v.owner.dimension ||
                !seen.emplace( ref.dimension.str(), ref.type, ref.id ).second ) {
                obj.throw_error( "Invalid area membership" );
            }
        }
        if( v.target.type != creature_ref::kind::none && ( v.target.id <= 0 ||
                v.target.dimension != v.owner.dimension ) ) {
            obj.throw_error( "Invalid projection target" );
        }
        if( !objects.emplace( v.id, std::move( p ) ).second ) {
            obj.throw_error( "Duplicate projection handle" );
        }
    }
    for( const auto &entry : objects ) {
        const power_projection_view &v = entry.second.view;
        const auto parent = objects.find( v.parent );
        if( parent != objects.end() && ( parent->second.view.root != v.root ||
                                         parent->second.view.depth + 1 != v.depth ) ) {
            jo.throw_error( "Invalid projection ancestry" );
        }
    }
    data->objects = std::move( objects );
    data->roots = std::move( roots );
    data->history.clear();
    data->next_id = std::max( data->next_id, next );
}
power_projection_manager &get_projections()
{
    return g->abilities().projections();
}

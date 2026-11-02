#include "errand.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <utility>

#include "activity_handlers.h"
#include "avatar.h"
#include "character.h"
#include "contents_change_handler.h"
#include "creature.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "flag.h"
#include "item.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "npc.h"
#include "pathfinding.h"
#include "point.h"
#include "ret_val.h"
#include "talker.h"
#include "translations.h"
#include "vehicle.h"
#include "vehicle_selector.h"
#include "vpart_position.h"

static const json_character_flag json_flag_CANNOT_MOVE( "CANNOT_MOVE" );

static const move_mode_id move_mode_run( "run" );
static const move_mode_id move_mode_walk( "walk" );

namespace
{
int next_walk_serial()
{
    static int serial = 0;
    return ++serial;
}

const char *kind_name( const walk_kind kind )
{
    switch( kind ) {
        case walk_kind::eoc:
            return "eoc";
        case walk_kind::order:
            return "order";
        case walk_kind::pickup:
            return "pickup";
    }
    return "eoc";
}

walk_kind kind_from_name( const std::string &name )
{
    if( name == "order" ) {
        return walk_kind::order;
    }
    if( name == "pickup" ) {
        return walk_kind::pickup;
    }
    return walk_kind::eoc;
}

// another creature stands on the target: a walk with range 0 ends next to it (NPC-B14)
bool target_taken( const Character &who, const walk_order &w )
{
    const Creature *occupant = get_creature_tracker().creature_at( w.target );
    return occupant != nullptr && occupant != &who;
}

int effective_range( const Character &who, const walk_order &w )
{
    return w.range == 0 && target_taken( who, w ) ? 1 : w.range;
}

// takes who's walk from it, undoing what it set up: the avatar's auto-move, running
std::unique_ptr<walk_order> take_walk( Character &who )
{
    std::unique_ptr<walk_order> w = std::move( who.walk );
    if( !w ) {
        return w;
    }
    if( w->auto_move_end && who.is_avatar() && who.destination_point == w->auto_move_end ) {
        who.clear_destination();
    }
    if( w->switched_to_run && who.is_running() ) {
        who.set_movement_mode( move_mode_walk );
    }
    return w;
}

// the avatar walks by auto-move: a route to where the walk ends
bool start_auto_move( avatar &u, walk_order &w )
{
    std::optional<std::vector<tripoint_bub_ms>> route = errand::walk_route( u, w, false );
    if( !route ) {
        // auto-move stops at the creature in the way
        route = errand::walk_route( u, w, true );
    }
    if( !route || route->empty() ) {
        return false;
    }
    if( w.run && !u.is_running() && u.can_run() ) {
        u.set_movement_mode( move_mode_run );
        w.switched_to_run = u.is_running();
    }
    u.set_destination( *route );
    w.auto_move_end = get_map().get_abs( route->back() );
    return true;
}

bool on_the_ground( const item_location &loc )
{
    const item_location::type where = loc.where_recursive();
    return where == item_location::type::map || where == item_location::type::vehicle;
}

// from its own tile or one next to it, on its z level, unless the item is sealed in furniture
bool within_reach( const Character &who, const item_location &loc )
{
    map &here = get_map();
    const tripoint_abs_ms p = loc.pos_abs();
    return p.z() == who.posz() && square_dist( who.pos_abs(), p ) <= 1 &&
           here.accessible_items( here.get_bub( p ) );
}

// why who can't take loc: someone else carries it, or it's someone else's and theft isn't
// allowed; empty for an item who carries or one on the ground.  Reach is left to the caller.
std::string take_refusal( const Character &who, const item_location &loc, const bool allow_theft )
{
    if( loc.carrier() != nullptr ) {
        return loc.carrier() == &who ? std::string() : errand::reason::held;
    }
    if( !on_the_ground( loc ) ) {
        return errand::reason::gone;
    }
    if( !allow_theft && !loc->is_owned_by( who, true ) ) {
        return errand::reason::owned;
    }
    return std::string();
}

// take_refusal(), then the reach for one on the ground
std::string hand_refusal( const Character &who, const item_location &loc, const bool allow_theft )
{
    const std::string refusal = take_refusal( who, loc, allow_theft );
    if( refusal.empty() && loc.carrier() == nullptr && !within_reach( who, loc ) ) {
        return errand::reason::out_of_reach;
    }
    return refusal;
}

// The avatar's wield and wear ask before taking someone else's item.  Taking it from the
// ground here is the theft, with the usual reaction of whoever sees it, so nothing asks.
void settle_theft( Character &who, const item_location &loc )
{
    if( who.is_avatar() && loc.carrier() == nullptr ) {
        item_location taken = loc;
        taken->handle_pickup_ownership( who );
    }
}

// puts away what who wields, without the menu the avatar would get: in its pockets if they
// hold it, else at its feet; false if it can't let go of it
bool free_hands( Character &who )
{
    item_location weapon = who.get_wielded_item();
    if( !weapon ) {
        return true;
    }
    if( weapon->has_flag( flag_NO_UNWIELD ) ) {
        // only a bionic weapon lets go, back into its bionic; unwield() switches that off
        return who.unwield() && !who.get_wielded_item();
    }
    if( who.can_stash( *weapon ) ) {
        who.mod_moves( -who.item_handling_cost( *weapon ) );
        who.add_msg_player_or_npc( _( "You put away your %s." ), _( "<npcname> puts away their %s." ),
                                   weapon->tname() );
        who.i_add( who.remove_weapon(), true, nullptr, nullptr, false, false );
    } else {
        put_into_vehicle_or_drop( who, item_drop_reason::deliberate, { who.remove_weapon() } );
    }
    return true;
}

// the item with this uid among those at p, in containers too
item_location find_by_uid( const tripoint_bub_ms &p, const int64_t uid, const itype_id &type )
{
    map &here = get_map();
    item_location found;
    const std::function<bool( item_location )> search = [&]( item_location loc ) {
        if( loc->uid().get_value() == uid && loc->typeId() == type ) {
            found = loc;
            return true;
        }
        for( item *inner : loc->all_items_top() ) {
            if( search( item_location( loc, inner ) ) ) {
                return true;
            }
        }
        return false;
    };
    for( item &it : here.i_at( p ) ) {
        if( search( item_location( map_cursor( p ), &it ) ) ) {
            return found;
        }
    }
    if( const std::optional<vpart_reference> cargo = here.veh_at( p ).cargo() ) {
        for( item &it : cargo->items() ) {
            if( search( item_location( vehicle_cursor( cargo->vehicle(), cargo->part_index() ), &it ) ) ) {
                return found;
            }
        }
    }
    return item_location();
}
} // namespace

void walk_order::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "kind", kind_name( kind ) );
    jsout.member( "target", target );
    jsout.member( "range", range );
    jsout.member( "run", run );
    jsout.member( "interruptible", interruptible );
    jsout.member( "bash", bash );
    jsout.member( "started", started );
    if( give_up_at ) {
        jsout.member( "give_up_at", *give_up_at );
    }
    jsout.member( "true_eocs", true_eocs );
    jsout.member( "false_eocs", false_eocs );
    jsout.member( "context", context );
    if( kind == walk_kind::pickup ) {
        jsout.member( "item_uid", item_uid );
        jsout.member( "item_type", item_type );
        jsout.member( "allow_theft", allow_theft );
    }
    jsout.member( "last_progress", last_progress );
    jsout.member( "switched_to_run", switched_to_run );
    jsout.end_object();
}

void walk_order::deserialize( const JsonObject &jo )
{
    jo.allow_omitted_members();
    kind = kind_from_name( jo.get_string( "kind", "eoc" ) );
    jo.read( "target", target );
    jo.read( "range", range );
    jo.read( "run", run );
    jo.read( "interruptible", interruptible );
    jo.read( "bash", bash );
    jo.read( "started", started );
    if( jo.has_member( "give_up_at" ) ) {
        time_point at;
        jo.read( "give_up_at", at );
        give_up_at = at;
    }
    jo.read( "true_eocs", true_eocs );
    jo.read( "false_eocs", false_eocs );
    jo.read( "context", context );
    if( kind == walk_kind::pickup ) {
        // the location itself isn't kept: resolving a stale one complains
        jo.read( "item_uid", item_uid );
        jo.read( "item_type", item_type );
        jo.read( "allow_theft", allow_theft );
    }
    jo.read( "last_progress", last_progress );
    jo.read( "switched_to_run", switched_to_run );
    serial = next_walk_serial();
}

namespace errand
{

walk_order move_order( const tripoint_abs_ms &where )
{
    walk_order w;
    w.kind = walk_kind::order;
    w.target = where;
    w.bash = true;
    return w;
}

bool walk_arrived( const Character &who, const walk_order &w )
{
    const tripoint_abs_ms pos = who.pos_abs();
    if( pos.z() != w.target.z() ) {
        return false;
    }
    const int dist = square_dist( pos, w.target );
    return dist <= w.range || ( dist <= 1 && w.range == 0 && target_taken( who, w ) );
}

bool walk_end_tile( const Character &who, const walk_order &w, const tripoint_bub_ms &p )
{
    map &here = get_map();
    const tripoint_bub_ms target = here.get_bub( w.target );
    if( p.z() != target.z() || square_dist( p, target ) > effective_range( who, w ) ) {
        return false;
    }
    if( p == who.pos_bub( here ) ) {
        return true;
    }
    // a closed door it can open will do: it ends in the doorway
    if( !here.inbounds( p ) || ( !here.passable_through( p ) &&
                                 !here.open_door( who, p, !here.is_outside( who.pos_bub( here ) ), true ) ) ) {
        return false;
    }
    const Creature *occupant = get_creature_tracker().creature_at( p );
    if( occupant != nullptr && occupant != &who ) {
        return false;
    }
    const npc *guy = who.as_npc();
    return guy == nullptr || !guy->path_avoids( p, true );
}

std::optional<std::vector<tripoint_bub_ms>> walk_route( const Character &who,
        const walk_order &w, const bool ignore_creatures )
{
    map &here = get_map();
    const tripoint_bub_ms pos = who.pos_bub( here );
    const tripoint_bub_ms target = here.get_bub( w.target );
    if( !here.inbounds( pos ) || !here.inbounds( target ) ) {
        return std::nullopt;
    }
    if( walk_arrived( who, w ) ) {
        return std::vector<tripoint_bub_ms>();
    }
    const npc *guy = who.as_npc();
    const pathfinding_settings &settings = guy != nullptr ?
                                           guy->get_pathfinding_settings( !w.bash ) : who.get_pathfinding_settings();
    std::function<bool( const tripoint_bub_ms & )> avoid;
    if( guy != nullptr ) {
        avoid = [guy, ignore_creatures]( const tripoint_bub_ms & p ) {
            return guy->path_avoids( p, ignore_creatures );
        };
    } else if( ignore_creatures ) {
        avoid = []( const tripoint_bub_ms & ) {
            return false;
        };
    } else {
        avoid = who.get_path_avoid();
    }
    const auto route_to = [&]( const pathfinding_target & to ) {
        std::vector<tripoint_bub_ms> route = here.route( pos, to, settings, avoid );
        while( !route.empty() && route.front() == pos ) {
            route.erase( route.begin() );
        }
        // a straight line runs on to the target itself: the walk ends at its first tile in range
        for( size_t i = 0; i + 1 < route.size(); ++i ) {
            if( walk_end_tile( who, w, route[i] ) ) {
                route.resize( i + 1 );
                break;
            }
        }
        return route;
    };
    const int range = effective_range( who, w );
    // one search finds the nearest tile in range, but the pathfinder never avoids those, so it
    // may end where nobody can stand
    if( range > 0 ) {
        std::vector<tripoint_bub_ms> route = route_to( pathfinding_target::radius( target, range ) );
        if( route.empty() ) {
            // the most lenient search found nothing in range: neither will one per tile
            return std::nullopt;
        }
        if( walk_end_tile( who, w, route.back() ) ) {
            return route;
        }
    }
    std::vector<tripoint_bub_ms> ends;
    for( const tripoint_bub_ms &p : here.points_in_radius( target, range ) ) {
        if( walk_end_tile( who, w, p ) ) {
            ends.push_back( p );
        }
    }
    std::sort( ends.begin(), ends.end(), [&pos]( const tripoint_bub_ms & a,
    const tripoint_bub_ms & b ) {
        const int da = rl_dist( pos, a );
        const int db = rl_dist( pos, b );
        return da != db ? da < db : trig_dist( pos, a ) < trig_dist( pos, b );
    } );
    // the nearest few; the first with a route wins
    constexpr size_t max_tries = 8;
    for( size_t i = 0; i < ends.size() && i < max_tries; ++i ) {
        std::vector<tripoint_bub_ms> route = route_to( pathfinding_target::point( ends[i] ) );
        if( !route.empty() && route.back() == ends[i] ) {
            return route;
        }
    }
    return std::nullopt;
}

void set_walk( Character &who, const walk_order &w )
{
    take_walk( who );
    who.walk = std::make_unique<walk_order>( w );
    who.walk->serial = next_walk_serial();
}

void start_walk( Character &who, walk_order w )
{
    walk_order *cur = who.walk.get();
    const bool same_item = w.kind != walk_kind::pickup || ( cur != nullptr && cur->item == w.item );
    if( cur != nullptr && cur->kind == w.kind && cur->target == w.target && same_item ) {
        // sent again where it's going: the walk goes on, its time limit and progress kept
        cur->range = w.range;
        cur->interruptible = w.interruptible;
        cur->run = w.run;
        cur->true_eocs = std::move( w.true_eocs );
        cur->false_eocs = std::move( w.false_eocs );
        cur->context = std::move( w.context );
        cur->allow_theft = w.allow_theft;
        if( !cur->give_up_at ) {
            cur->give_up_at = w.give_up_at;
        }
        if( walk_arrived( who, *cur ) ) {
            end_walk( who, reason::arrived );
        }
        return;
    }
    w.started = calendar::turn;
    w.last_progress = calendar::turn;
    w.last_step = calendar::turn_zero;
    w.auto_move_end = std::nullopt;
    w.switched_to_run = false;
    w.stepped = false;
    set_walk( who, w );
    walk_order &walk = *who.walk;
    map &here = get_map();
    if( walk_arrived( who, walk ) ) {
        end_walk( who, reason::arrived );
        return;
    }
    if( !here.inbounds( here.get_bub( walk.target ) ) ) {
        end_walk( who, reason::unreachable );
        return;
    }
    if( !walk.give_up_at && ( who.has_flag( json_flag_CANNOT_MOVE ) ||
                              !walk_route( who, walk, true ) ) ) {
        end_walk( who, reason::unreachable );
        return;
    }
    if( avatar *u = who.as_avatar() ) {
        if( !start_auto_move( *u, walk ) ) {
            end_walk( who, reason::unreachable );
        }
    }
}

void end_walk( Character &who, const std::string &why )
{
    std::unique_ptr<walk_order> w = take_walk( who );
    if( !w ) {
        return;
    }
    switch( w->kind ) {
        case walk_kind::order:
            if( why != reason::arrived && why != reason::cancelled ) {
                if( const npc *guy = who.as_npc() ) {
                    guy->say( _( "I can't get there." ) );
                }
            }
            return;
        case walk_kind::pickup: {
            const item_location it = pickup_target( *w );
            if( why != reason::arrived ) {
                run_eocs( who, it, w->false_eocs, w->context, why );
                return;
            }
            const item_result got = pickup( who, it, w->allow_theft );
            if( got.ok() ) {
                run_eocs( who, got.loc, w->true_eocs, w->context, reason::done );
            } else {
                run_eocs( who, it, w->false_eocs, w->context, got.refusal );
            }
            return;
        }
        case walk_kind::eoc:
            run_eocs( who, item_location(), why == reason::arrived ? w->true_eocs : w->false_eocs,
                      w->context, why );
            return;
    }
}

void drop_walk( Character &who )
{
    take_walk( who );
}

bool cancel_walk( Character &who, const bool silent )
{
    if( !who.walk ) {
        return false;
    }
    if( silent ) {
        drop_walk( who );
    } else {
        end_walk( who, reason::cancelled );
    }
    return true;
}

item_location pickup_target( const walk_order &w )
{
    if( w.item.where() != item_location::type::invalid ) {
        // set by the effect this session: still valid, or gone
        return w.item ? w.item : item_location();
    }
    // loaded from a save: look for it where it was
    map &here = get_map();
    const tripoint_bub_ms p = here.get_bub( w.target );
    if( w.item_uid == 0 || !here.inbounds( p ) ) {
        return item_location();
    }
    return find_by_uid( p, w.item_uid, w.item_type );
}

void review_walk( Character &who )
{
    if( !who.walk ) {
        return;
    }
    if( walk_arrived( who, *who.walk ) ) {
        end_walk( who, reason::arrived );
    } else if( who.walk->give_up_at && calendar::turn >= *who.walk->give_up_at ) {
        end_walk( who, reason::timeout );
    }
}

void walk_passed_over( Character &who )
{
    if( who.walk && !who.walk->interruptible ) {
        end_walk( who, reason::interrupted );
    }
}

void avatar_turn( avatar &u )
{
    if( !u.walk ) {
        return;
    }
    walk_order &w = *u.walk;
    if( walk_arrived( u, w ) ) {
        end_walk( u, reason::arrived );
    } else if( w.give_up_at && calendar::turn >= *w.give_up_at ) {
        end_walk( u, reason::timeout );
    } else if( !w.auto_move_end ) {
        // loaded from a save: the auto-move wasn't
        if( !start_auto_move( u, w ) ) {
            end_walk( u, reason::unreachable );
        }
    } else if( !u.has_destination() || u.destination_point != w.auto_move_end ) {
        // stopped by a creature in the way, safe mode or the player, or sent elsewhere
        end_walk( u, reason::interrupted );
    }
}

item_result pickup( Character &who, const item_location &loc, const bool allow_theft )
{
    if( !loc ) {
        return { reason::gone, item_location() };
    }
    if( loc.carrier() == &who ) {
        return { std::string(), loc };
    }
    const std::string refusal = take_refusal( who, loc, allow_theft );
    if( !refusal.empty() ) {
        return { refusal, loc };
    }
    const item &it = *loc;
    if( it.made_of_from_type( phase_id::LIQUID ) || it.made_of_from_type( phase_id::GAS ) ) {
        return { reason::liquid, loc };
    }
    if( it.is_bucket_nonempty() ) {
        return { reason::would_spill, loc };
    }
    // the avatar's pickup goes up to what it can lift, an NPC's to what it can carry
    if( !who.can_pickWeight( it, who.is_npc() ) ) {
        return { reason::too_heavy, loc };
    }
    if( !who.can_stash( it ) ) {
        return { reason::no_room, loc };
    }
    // last, so that it's the only refusal a walk there can help with
    if( !within_reach( who, loc ) ) {
        return { reason::out_of_reach, loc };
    }
    const std::string name = it.tname();
    item_location source = loc;
    // costs what the avatar's pickup does; on_pickup() settles who owns it, a theft included
    item_location got = source.obtain( who );
    if( !got ) {
        return { reason::no_room, item_location() };
    }
    who.flag_encumbrance();
    who.invalidate_weight_carried_cache();
    who.add_msg_player_or_npc( _( "You pick up the %s." ), _( "<npcname> picks up the %s." ), name );
    return { std::string(), got };
}

item_result wield( Character &who, const item_location &loc, const bool allow_theft )
{
    if( !loc ) {
        return { reason::gone, item_location() };
    }
    if( who.is_wielding( *loc ) ) {
        return { std::string(), who.get_wielded_item() };
    }
    const std::string refusal = hand_refusal( who, loc, allow_theft );
    if( !refusal.empty() ) {
        return { refusal, loc };
    }
    const ret_val<void> can = who.can_wield( *loc );
    if( !can.success() ) {
        who.add_msg_if_player( m_info, "%s", can.str() );
        return { reason::cannot_wield, loc };
    }
    const item_location weapon = who.get_wielded_item();
    if( weapon && weapon->has_item( *loc ) ) {
        // putting the weapon away would move the item with it
        return { reason::cannot_wield, loc };
    }
    settle_theft( who, loc );
    if( !free_hands( who ) ) {
        return { reason::cannot_wield, loc };
    }
    // costs what wielding it costs from where it is
    if( !who.wield( loc ) ) {
        return { reason::cannot_wield, loc };
    }
    return { std::string(), who.get_wielded_item() };
}

item_result wear( Character &who, const item_location &loc, const bool allow_theft )
{
    if( !loc ) {
        return { reason::gone, item_location() };
    }
    if( who.is_worn( *loc ) ) {
        return { std::string(), loc };
    }
    const std::string refusal = hand_refusal( who, loc, allow_theft );
    if( !refusal.empty() ) {
        return { refusal, loc };
    }
    const ret_val<void> can = who.can_wear( *loc );
    if( !can.success() ) {
        who.add_msg_if_player( m_info, "%s", can.str() );
        return { reason::cannot_wear, loc };
    }
    settle_theft( who, loc );
    std::set<const item *> worn_before;
    for( const item_location &top : who.top_items_loc() ) {
        worn_before.insert( top.get_item() );
    }
    const itype_id type = loc->typeId();
    // costs what putting it on costs
    if( !who.wear( loc, true ) ) {
        return { reason::cannot_wear, loc };
    }
    for( const item_location &top : who.top_items_loc() ) {
        if( worn_before.count( top.get_item() ) == 0 && top->typeId() == type ) {
            return { std::string(), top };
        }
    }
    // worn, then taken off again by something it set off
    return { std::string(), item_location() };
}

item_result drop( Character &who, const item_location &loc )
{
    if( !loc ) {
        return { reason::gone, item_location() };
    }
    if( loc.carrier() != &who ) {
        return { reason::not_carried, loc };
    }
    ret_val<void> can = who.can_drop( *loc );
    const bool worn = who.is_worn( *loc );
    if( can.success() && worn ) {
        can = who.can_takeoff( *loc );
    }
    if( !can.success() ) {
        who.add_msg_if_player( m_info, "%s", can.str() );
        return { reason::cannot_drop, loc };
    }
    // the drop activity's cost and steps, done at once
    item_location source = loc;
    who.mod_moves( -source.obtain_cost( who ) );
    contents_change_handler handler;
    if( source.has_parent() ) {
        handler.unseal_pocket_containing( source );
    } else {
        who.set_check_encumbrance( true );
    }
    std::list<item> taken;
    if( worn ) {
        who.takeoff( source, &taken );
    } else {
        taken.push_back( who.i_rem( &*source ) );
    }
    handler.handle_by( who );
    map &here = get_map();
    const std::vector<item_location> dropped = put_into_vehicle_or_drop_ret_locs( who,
            item_drop_reason::deliberate, taken, &here, who.pos_bub( here ) );
    return { std::string(), dropped.empty() ? item_location() : dropped.front() };
}

void run_eocs( Character &who, const item_location &loc,
               const std::vector<effect_on_condition_id> &eocs,
               const global_variables::impl_t &context, const std::string &why )
{
    if( eocs.empty() ) {
        return;
    }
    // the item talker points at this one, so it lives as long as the dialogue
    item_location held = loc;
    dialogue d( get_talker_for( who ), held ? get_talker_for( held ) : nullptr, {}, context );
    d.set_value( "reason", why );
    for( const effect_on_condition_id &eoc : eocs ) {
        eoc->activate( d );
    }
}

} // namespace errand

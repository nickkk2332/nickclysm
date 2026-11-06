#include "iexamine_eoc.h"

#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "calendar.h"
#include "character.h"
#include "clone_ptr.h"
#include "coordinates.h"
#include "game.h"
#include "gates.h"
#include "iexamine.h"
#include "map.h"
#include "mapdata.h"
#include "player_activity.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "vehicle.h"
#include "vpart_position.h"

static const activity_id ACT_CLOSE_TILE( "ACT_CLOSE_TILE" );

namespace iexamine
{
namespace
{

// controls_gate asks whether to use the control; an NPC that was told to examine it does.
void npc_controls_gate( Character &who, const tripoint_bub_ms &pos )
{
    gates::open_gate( pos, who );
}

struct npc_examine_action {
    iexamine_examine_function examine;
    // what an NPC runs instead, or nullptr when the action itself asks an NPC nothing
    iexamine_examine_function for_npc;
};

// The built-in examine actions an NPC may use from an EOC. Each was read to make sure that, run
// for an NPC, it opens no menu, asks nothing and doesn't act on the avatar, and that what it does
// shows as a started activity or a changed tile (so examine_from_eoc can tell it did something):
// - controls_gate asks for confirmation, which npc_controls_gate skips; it starts the gate's
//   activity;
// - harvest_* only ask the avatar whether to pick (query_pick), then start the harvest activity;
// - harvest_plant_ex and clear_overgrown ask nobody, start an activity and change the furniture.
const std::vector<npc_examine_action> &npc_examine_actions()
{
    static const std::vector<npc_examine_action> actions = {
        { &iexamine::controls_gate, &npc_controls_gate },
        { &iexamine::harvest_furn, nullptr },
        { &iexamine::harvest_furn_nectar, nullptr },
        { &iexamine::harvest_ter, nullptr },
        { &iexamine::harvest_ter_nectar, nullptr },
        { &iexamine::harvest_plant_ex, nullptr },
        { &iexamine::clear_overgrown, nullptr },
    };
    return actions;
}

// What a built-in examine action can change: the tile, or the character's activity.
struct examine_state {
    ter_id ter;
    furn_id furn;
    activity_id activity;
    size_t backlog;

    examine_state( const Character &who, const tripoint_bub_ms &pos ) :
        ter( get_map().ter( pos ) ), furn( get_map().furn( pos ) ), activity( who.activity.id() ),
        backlog( who.backlog.size() ) {}

    // Whether `who` started an activity or the tile changed since `before`.
    bool changed_since( const examine_state &before, const Character &who ) const {
        return ter != before.ter || furn != before.furn ||
               ( !who.activity.is_null() && ( activity != before.activity || backlog != before.backlog ) );
    }
};

// A door, gate, hatch or anything else that opens and closes: opens it if it is closed and
// closes it if it is open. nullopt when it did; the reason otherwise. Not for vehicles.
std::optional<std::string> toggle_door( Character &who, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const bool inside = !here.is_outside( who.pos_bub( here ) );
    const std::string name = here.name( pos );
    if( here.ter( pos )->open || here.furn( pos )->open ) {
        if( !here.open_door( who, pos, inside, true ) ) {
            return string_format( _( "The %s can't be opened from here." ), name );
        }
        here.open_door( who, pos, inside );
        who.mod_moves( -to_moves<int>( 1_seconds ) );
        return std::nullopt;
    }
    // Closing nudges items aside and fails on creatures, as the close action does.
    who.assign_activity( close_tile_activity_actor( pos ) );
    if( who.activity.id() != ACT_CLOSE_TILE ) {
        return string_format( _( "The %s can't be closed now." ), name );
    }
    return std::nullopt;
}

// As game::examine (which is private) for the avatar, without picking items up and without the
// menus of a creature standing there: a vehicle, a construction, a trap, a computer, and then
// the furniture or else the terrain, with the warning about a faction's property. Why nothing
// was examined, or nullopt once something was (a menu cancelled there can't be told apart).
std::optional<std::string> avatar_examine( Character &you, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const std::string name = here.name( pos );
    if( const optional_vpart_position vp = here.veh_at( pos ) ) {
        if( you.is_mounted() ) {
            return std::string( _( "You cannot interact with a vehicle while mounted." ) );
        }
        if( !vp->vehicle().is_appliance() ) {
            vp->vehicle().interact_with( &here, pos, false );
        } else {
            g->exam_appliance( vp->vehicle(), vp->mount_pos() );
        }
        return std::nullopt;
    }
    iexamine::part_con( you, pos );
    here.tr_at( pos ).examine( pos );
    if( here.has_flag( ter_furn_flag::TFLAG_CONSOLE, pos ) && !you.is_mounted() ) {
        g->use_computer( pos );
        return std::nullopt;
    }
    const bool furniture = here.has_furn( pos );
    const map_data_common_t &examined = furniture ?
                                        static_cast<const map_data_common_t &>( here.furn( pos ).obj() ) :
                                        static_cast<const map_data_common_t &>( here.ter( pos ).obj() );
    if( you.is_mounted() ) {
        return string_format( _( "You can't examine the %s while mounted." ), name );
    }
    if( !examined.can_examine( pos ) ) {
        return string_format( _( "There is nothing to do with the %s." ), name );
    }
    if( !here.has_flag( "FREE_TO_EXAMINE", pos ) &&
        !g->warn_player_maybe_anger_local_faction( false, true ) ) {
        return string_format( _( "You leave the %s alone." ), name );
    }
    examined.examine( you, pos );
    return std::nullopt;
}

} // namespace

std::optional<std::string> examine_from_eoc( Character &who, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    if( !here.inbounds( pos ) ) {
        return std::string( _( "That place is too far away." ) );
    }
    const std::string name = here.name( pos );
    // As the examine action (choose_adjacent without vertical): never through a floor or a
    // ceiling.
    const tripoint_bub_ms from = who.pos_bub( here );
    if( from.z() != pos.z() || square_dist( from.xy(), pos.xy() ) > 1 ) {
        return string_format( _( "The %s is out of reach." ), name );
    }
    const bool vehicle_there = static_cast<bool>( here.veh_at( pos ) );
    const furn_t &furn = here.furn( pos ).obj();
    const ter_t &ter = here.ter( pos ).obj();
    if( !vehicle_there && ( ter.open || ter.close || furn.open || furn.close ) ) {
        return toggle_door( who, pos );
    }
    if( who.is_avatar() ) {
        return avatar_examine( who, pos );
    }
    if( vehicle_there ) {
        return string_format( _( "An NPC can't operate the %s from an EOC." ), name );
    }
    // As game::examine: the furniture, if there is some, or else the terrain.
    const map_data_common_t &examined = here.has_furn( pos ) ?
                                        static_cast<const map_data_common_t &>( furn ) :
                                        static_cast<const map_data_common_t &>( ter );
    // map_data_common_t::examine gives an NPC the first examine actor.
    const std::vector<cata::clone_ptr<iexamine_actor>> actors = examined.get_examine();
    if( !actors.empty() ) {
        if( actors.front()->type != "effect_on_condition" ) {
            return string_format( _( "Examining the %s opens a menu, which an NPC can't use." ),
                                  name );
        }
        actors.front()->call( who, pos );
        return std::nullopt;
    }
    if( !examined.can_examine( pos ) ) {
        return string_format( _( "There is nothing to do with the %s." ), name );
    }
    for( const npc_examine_action &action : npc_examine_actions() ) {
        if( examined.has_examine( action.examine ) ) {
            const examine_state before( who, pos );
            ( action.for_npc != nullptr ? action.for_npc : action.examine )( who, pos );
            if( !examine_state( who, pos ).changed_since( before, who ) ) {
                // Out of season, nothing to harvest, a control with no gate...
                return string_format( _( "Examining the %s did nothing." ), name );
            }
            return std::nullopt;
        }
    }
    return string_format( _( "Examining the %s opens a menu, which an NPC can't use." ), name );
}

} // namespace iexamine

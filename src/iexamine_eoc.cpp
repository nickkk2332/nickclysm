#include "iexamine_eoc.h"

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
#include "line.h"
#include "messages.h"
#include "map.h"
#include "mapdata.h"
#include "string_formatter.h"
#include "translations.h"
#include "trap.h"
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
// for an NPC, it opens no menu, asks nothing and doesn't act on the avatar:
// - controls_gate asks for confirmation, which npc_controls_gate skips;
// - harvest_* only ask the avatar whether to pick (query_pick), then start the harvest activity;
// - harvest_plant_ex, harvested_plant, clear_overgrown and open_safe ask nobody.
const std::vector<npc_examine_action> &npc_examine_actions()
{
    static const std::vector<npc_examine_action> actions = {
        { &iexamine::controls_gate, &npc_controls_gate },
        { &iexamine::harvest_furn, nullptr },
        { &iexamine::harvest_furn_nectar, nullptr },
        { &iexamine::harvest_ter, nullptr },
        { &iexamine::harvest_ter_nectar, nullptr },
        { &iexamine::harvest_plant_ex, nullptr },
        { &iexamine::harvested_plant, nullptr },
        { &iexamine::clear_overgrown, nullptr },
        { &iexamine::open_safe, nullptr },
    };
    return actions;
}

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
// the furniture or else the terrain, with the warning about a faction's property.
void avatar_examine( Character &you, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    if( const optional_vpart_position vp = here.veh_at( pos ) ) {
        if( you.is_mounted() ) {
            you.add_msg_if_player( m_warning, _( "You cannot interact with a vehicle while mounted." ) );
        } else if( !vp->vehicle().is_appliance() ) {
            vp->vehicle().interact_with( &here, pos, false );
            return;
        } else {
            g->exam_appliance( vp->vehicle(), vp->mount_pos() );
            return;
        }
    }
    iexamine::part_con( you, pos );
    here.tr_at( pos ).examine( pos );
    if( here.has_flag( ter_furn_flag::TFLAG_CONSOLE, pos ) && !you.is_mounted() ) {
        g->use_computer( pos );
        return;
    }
    const bool furniture = here.has_furn( pos );
    const map_data_common_t &examined = furniture ?
                                        static_cast<const map_data_common_t &>( here.furn( pos ).obj() ) :
                                        static_cast<const map_data_common_t &>( here.ter( pos ).obj() );
    if( furniture ? you.cant_do_mounted() : ( !examined.can_examine( pos ) || you.is_mounted() ) ) {
        return;
    }
    if( !here.has_flag( "FREE_TO_EXAMINE", pos ) &&
        !g->warn_player_maybe_anger_local_faction( false, true ) ) {
        return;
    }
    examined.examine( you, pos );
}

} // namespace

std::optional<std::string> examine_from_eoc( Character &who, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    if( !here.inbounds( pos ) ) {
        return std::string( _( "That place is too far away." ) );
    }
    const std::string name = here.name( pos );
    if( square_dist( who.pos_bub( here ), pos ) > 1 ) {
        return string_format( _( "The %s is out of reach." ), name );
    }
    const bool vehicle_there = static_cast<bool>( here.veh_at( pos ) );
    const furn_t &furn = here.furn( pos ).obj();
    const ter_t &ter = here.ter( pos ).obj();
    if( !vehicle_there && ( ter.open || ter.close || furn.open || furn.close ) ) {
        return toggle_door( who, pos );
    }
    if( who.is_avatar() ) {
        avatar_examine( who, pos );
        return std::nullopt;
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
            ( action.for_npc != nullptr ? action.for_npc : action.examine )( who, pos );
            return std::nullopt;
        }
    }
    return string_format( _( "Examining the %s opens a menu, which an NPC can't use." ), name );
}

} // namespace iexamine

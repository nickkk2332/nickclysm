#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "avatar.h"
#include "character.h"
#include "coordinates.h"
#include "effect.h"
#include "effect_source.h"
#include "game.h"
#include "line.h"
#include "map.h"
#include "math_parser_diag_value.h"
#include "talker_vehicle.h"
#include "tileray.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const itype_id fuel_type_animal( "animal" );

std::string talker_vehicle_const::disp_name() const
{
    return me_veh_const->disp_name();
}

std::string talker_vehicle_const::get_name() const
{
    return me_veh_const->name;
}

int talker_vehicle_const::posx( const map &here ) const
{
    return pos_bub( here ).x();
}

int talker_vehicle_const::posy( const map &here ) const
{
    return pos_bub( here ).y();
}

int talker_vehicle_const::posz() const
{
    return pos_abs().z();
}

tripoint_bub_ms talker_vehicle_const::pos_bub( const map &here ) const
{
    return me_veh_const->pos_bub( here );
}

tripoint_abs_ms talker_vehicle_const::pos_abs() const
{
    return me_veh_const->pos_abs();
}

tripoint_abs_omt talker_vehicle_const::pos_abs_omt() const
{
    return me_veh_const->pos_abs_omt();
}

diag_value const *talker_vehicle_const::maybe_get_value( const std::string &var_name ) const
{
    return me_veh_const->maybe_get_value( var_name );
}

void talker_vehicle::set_value( const std::string &var_name, diag_value const &value )
{
    me_veh->set_value( var_name, value );
}

void talker_vehicle::remove_value( const std::string &var_name )
{
    me_veh->remove_value( var_name );
}

void talker_vehicle::add_effect( const efftype_id &eff_id, const time_duration &dur,
                                 const std::string &, bool permanent, bool, int intensity )
{
    me_veh->add_effect( effect_source::empty(), eff_id, dur, permanent, intensity );
}

void talker_vehicle::remove_effect( const efftype_id &eff_id, const std::string & )
{
    me_veh->remove_effect( eff_id );
}

std::vector<std::string> talker_vehicle_const::get_topics( bool ) const
{
    return me_veh_const->chat_topics;
}

bool talker_vehicle_const::will_talk_to_u( const Character &you, bool ) const
{
    return !you.is_dead_state();
}

int talker_vehicle_const::get_weight() const
{
    return units::to_milligram( me_veh_const->total_mass( get_map() ) );
}

int talker_vehicle_const::get_unloaded_weight() const
{
    return units::to_milligram( me_veh_const->unloaded_mass() );
}

// vehicle::remote_controlled() also drops the remote link, with a message, when the avatar
// is out of range. A condition must not change the game, so check the range on its own.
static bool avatar_remote_controls( const vehicle &veh )
{
    if( g->remoteveh() != &veh ) {
        return false;
    }
    const tripoint_abs_ms avatar_pos = get_avatar().pos_abs();
    for( const vpart_reference &vp : veh.get_avail_parts( "REMOTE_CONTROLS" ) ) {
        if( rl_dist( avatar_pos, vp.pos_abs() ) <= 40 ) {
            return true;
        }
    }
    return false;
}

// vehicle::player_in_control() without its fallback to remote_controlled()
static bool avatar_at_controls( const vehicle &veh )
{
    if( veh.tags.count( "IN_CONTROL_OVERRIDE" ) ) {
        return true;
    }
    const map &here = get_map();
    const avatar &u = get_avatar();
    const optional_vpart_position vp = here.veh_at( u.pos_bub() );
    if( !vp || &vp->vehicle() != &veh || !u.controlling_vehicle ) {
        return false;
    }
    if( veh.part_with_feature( vp->part_index(), VPFLAG_CONTROLS, false ) >= 0 ) {
        return true;
    }
    return veh.part_with_feature( vp->mount_pos(), "CONTROL_ANIMAL", true ) >= 0 &&
           veh.has_engine_type( fuel_type_animal, false ) && veh.get_harnessed_animal( here );
}

bool talker_vehicle_const::is_driven() const
{
    const vehicle &veh = *me_veh_const;
    // autopilot, following and patrolling drive the vehicle without anyone at the controls
    if( veh.is_autodriving || veh.is_following || veh.is_patrolling ) {
        return true;
    }
    // player_in_control() ends in remote_controlled(), which drops the remote link when no
    // remote controls are in range; that can only happen while the link is to this vehicle
    if( g->remoteveh() != &veh ) {
        return veh.player_in_control( get_map(), get_avatar() );
    }
    return avatar_at_controls( veh ) || avatar_remote_controls( veh );
}

int talker_vehicle_const::get_vehicle_facing() const
{
    return std::lround( units::to_degrees( me_veh_const->face.dir() + 90_degrees ) ) % 360;
}

bool talker_vehicle_const::can_fly() const
{
    return me_veh_const->is_flyable();
}

bool talker_vehicle_const::is_flying() const
{
    return me_veh_const->is_flying_in_air();
}

bool talker_vehicle_const::can_float() const
{
    return me_veh_const->can_float( get_map() );
}

bool talker_vehicle_const::is_floating() const
{
    return me_veh_const->is_watercraft() && me_veh_const->can_float( get_map() );
}

int talker_vehicle_const::get_current_speed() const
{
    return me_veh_const->velocity;
}

int talker_vehicle_const::get_friendly_passenger_count() const
{
    return me_veh_const->get_passenger_count( false );
}

int talker_vehicle_const::get_hostile_passenger_count() const
{
    return me_veh_const->get_passenger_count( true );
}

bool talker_vehicle_const::has_part_flag( const std::string &flag, bool enabled ) const
{
    return me_veh_const->has_part( flag, enabled );
}

bool talker_vehicle_const::is_falling() const
{
    return me_veh_const->is_falling;
}

bool talker_vehicle_const::is_skidding() const
{
    return me_veh_const->skidding;
}

bool talker_vehicle_const::is_sinking() const
{
    return me_veh_const->is_in_water( true ) && !me_veh_const->can_float( get_map() );
}

bool talker_vehicle_const::is_on_rails() const
{
    return me_veh_const->can_use_rails( get_map() );
}

bool talker_vehicle_const::is_remote_controlled() const
{
    return avatar_remote_controls( *me_veh_const );
}

bool talker_vehicle_const::is_passenger( Character &you ) const
{
    return me_veh_const->is_passenger( you );
}

bool talker_vehicle_const::has_effect( const efftype_id &eff_id, const bodypart_id & ) const
{
    return me_veh_const->has_effect( eff_id );
}

effect talker_vehicle_const::get_effect( const efftype_id &eff_id, const bodypart_id & ) const
{
    auto effect = me_veh_const->effects.find( eff_id );
    if( effect != me_veh_const->effects.end() ) {
        return effect->second;
    }
    return effect::null_effect;
}

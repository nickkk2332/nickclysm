#include "coordinates.h"
#include "talker_zone.h"
#include "clzones.h"

tripoint_abs_ms talker_zone_const::pos_abs() const
{
    return me_zone_const->get_start_point();
}

tripoint_abs_omt talker_zone_const::pos_abs_omt() const
{
    return project_to<coords::omt>( pos_abs() );
}

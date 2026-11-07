#include "talker_zone.h"

#include <string>

#include "clzones.h"
#include "coordinates.h"
#include "math_parser_diag_value.h"

std::string talker_zone_const::disp_name() const
{
    return me_zone_const->get_name();
}

std::string talker_zone_const::get_name() const
{
    return me_zone_const->get_name();
}

tripoint_abs_ms talker_zone_const::pos_abs() const
{
    return me_zone_const->get_start_point();
}

tripoint_abs_omt talker_zone_const::pos_abs_omt() const
{
    return project_to<coords::omt>( pos_abs() );
}

diag_value const *talker_zone_const::maybe_get_value( const std::string &var_name ) const
{
    return me_zone_const->maybe_get_value( var_name );
}

void talker_zone::set_value( const std::string &var_name, diag_value const &value )
{
    me_zone->set_value( var_name, value );
}

void talker_zone::remove_value( const std::string &var_name )
{
    me_zone->remove_value( var_name );
}

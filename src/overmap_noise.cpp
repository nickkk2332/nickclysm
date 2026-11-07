#include <cmath>
#include <algorithm>

#include "flexbuffer_json.h"
#include "overmap_noise.h"
#include "simplexnoise.h"

namespace om_noise
{

float om_noise_params::at( const point_abs_omt &p, const float seed ) const
{
    const float r = scaled_octave_noise_3d( static_cast<float>( octaves ), persistence, scale, 0, 1,
                                            p.x(), p.y(), seed );
    // The vanilla exponents as the literals the layers were written with, so that the compiler
    // treats them as before (pow( r, 2.0f ) may become r * r) and the noise stays the same.
    if( exponent == 2.0f ) {
        return std::pow( r, 2.0f );
    } else if( exponent == 3.0f ) {
        return std::pow( r, 3.0f );
    }
    return std::pow( r, exponent );
}

void om_noise_params::deserialize( const JsonObject &jo )
{
    // Members left out keep their values (the layer's vanilla shape, or what a copy-from gave).
    jo.read( "octaves", octaves );
    jo.read( "persistence", persistence );
    jo.read( "scale", scale );
    jo.read( "exponent", exponent );
    if( octaves < 1 ) {
        jo.throw_error_at( "octaves", "octaves must be at least 1" );
    }
    if( scale <= 0.0f ) {
        jo.throw_error_at( "scale", "scale must be positive" );
    }
}

float om_noise_layer_forest::noise_at( const point_om_omt &local_omt_pos ) const
{
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    const float r = main.at( p, get_seed() );
    const float d = detail.at( p, get_seed() );
    return std::max( 0.0f, r - d * 0.5f );
}

float om_noise_layer_floodplain::noise_at( const point_om_omt &local_omt_pos ) const
{
    return params.at( global_omt_pos( local_omt_pos ), get_seed() );
}

float om_noise_layer_lake::noise_at( const point_om_omt &local_omt_pos ) const
{
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    float r = scaled_octave_noise_3d( 8, 0.5, 0.002, 0, 1, p.x(), p.y(), get_seed() );
    r = std::pow( r, 4.0f );
    return r;
}

float om_noise_layer_ocean::noise_at( const point_om_omt &local_omt_pos ) const
{
    // this is a duplicate of lake noise.  Changing it might cause artifacts if oceans
    // and lakes intersect.
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    float r = scaled_octave_noise_3d( 8, 0.5, 0.002, 0, 1, p.x(), p.y(), get_seed() );
    r = std::pow( r, 4.0f );
    return r;
}

} // namespace om_noise

#include <algorithm>

#include "coordinates.h"
#include "map.h"
#include "scent_block.h"
#include "scent_map.h"

scent_block::scent_block( const tripoint_bub_sm &sub, scent_map &scents )
    : origin( coords::project_to<coords::ms>( sub ) + point( -border, -border ) )
    , scents( scents )
    , modification_count( 0 )
{
    for( int x = 0; x < SEEX + 2 * border; ++x ) {
        for( int y = 0; y < SEEY + 2 * border; ++y ) {
            assignment[x][y] = { data_mode::NONE, 0, 0 };
        }
    }
}

void scent_block::commit_modifications()
{
    if( modification_count == 0 ) {
        return;
    }
    // The scent map is the player's level only; fields on other levels must not
    // write over it.
    if( origin.z() != get_map().get_abs_sub().z() ) {
        return;
    }
    for( int x = 0; x < SEEX + 2 * border; ++x ) {
        for( int y = 0; y < SEEY + 2 * border; ++y ) {
            const datum &dat = assignment[x][y];
            if( dat.mode == data_mode::NONE ) {
                continue;
            }
            tripoint_bub_ms p{ origin + tripoint( x, y, 0 ) };
            if( scents.inbounds( p ) ) {
                const int lowered = scents.get_unsafe( p ) - dat.subtract;
                scents.set_unsafe( p, std::max( dat.intensity, lowered ) );
            }
        }
    }
}

// We should be working entirely within the range, so don't range check here
void scent_block::apply_gas( const tripoint_bub_ms &p, const int nintensity )
{
    const point_rel_ms ndx = index( p );
    datum &dat = assignment[ndx.x()][ndx.y()];
    // The scent isn't known until the commit, so this composes with what is pending:
    // max( max( a, s - b ) - n, 0 ) == max( max( a - n, 0 ), s - ( b + n ) )
    dat.mode = data_mode::MODIFY;
    dat.intensity = std::max( 0, dat.intensity - nintensity );
    dat.subtract += nintensity;
    ++modification_count;
}

void scent_block::apply_slime( const tripoint_bub_ms &p, int intensity )
{
    const point_rel_ms ndx = index( p );
    datum &dat = assignment[ndx.x()][ndx.y()];
    // max( i, max( a, s - b ) ) == max( max( i, a ), s - b )
    dat.mode = data_mode::MODIFY;
    dat.intensity = std::max( dat.intensity, intensity );
    ++modification_count;
}

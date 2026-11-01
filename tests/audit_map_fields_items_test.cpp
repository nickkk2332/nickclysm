#include <string>

#include "calendar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "field.h"
#include "field_type.h"
#include "map.h"
#include "map_helpers.h"
#include "point.h"
#include "type_id.h"

static const field_type_str_id field_fd_blood( "fd_blood" );
static const field_type_str_id field_fd_smoke( "fd_smoke" );

static const ter_str_id ter_t_floor( "t_floor" );

// Rain ages outdoor fields by amount / decay_amount_factor. Before the fix the loop aged a
// copy of each field entry, so nothing on the map changed, and only the bubble's own level
// was looked at.
TEST_CASE( "map-b03_rain_ages_outdoor_fields_on_the_map", "[audit][map][weather]" )
{
    clear_map( -2, 1 );
    map &here = get_map();
    const tripoint_bub_ms outdoor( 30, 30, 0 );
    const tripoint_bub_ms indoor( 90, 90, 0 );
    const tripoint_bub_ms above( 30, 90, 1 );
    here.ter_set( indoor, ter_t_floor );
    here.build_map_cache( 0, true );
    REQUIRE( here.is_outside( outdoor ) );
    REQUIRE_FALSE( here.is_outside( indoor ) );
    REQUIRE( here.is_outside( above ) );

    REQUIRE( here.add_field( outdoor, field_fd_blood, 1, 0_turns ) );
    REQUIRE( here.add_field( indoor, field_fd_blood, 1, 0_turns ) );
    REQUIRE( here.add_field( above, field_fd_smoke, 1, 0_turns ) );
    REQUIRE( field_fd_blood->decay_amount_factor == 2 );
    REQUIRE( field_fd_smoke->decay_amount_factor == 5 );

    here.decay_fields_and_scent( 60_turns );

    CHECK( here.get_field_age( outdoor, field_fd_blood ) == 30_turns );
    CHECK( here.get_field_age( indoor, field_fd_blood ) == 0_turns );
    CHECK( here.get_field_age( above, field_fd_smoke ) == 12_turns );
}

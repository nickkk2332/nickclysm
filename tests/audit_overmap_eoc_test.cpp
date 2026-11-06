#include <memory>
#include <optional>
#include <string>

#include "avatar.h"
#include "cata_catch.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "effect_on_condition.h"
#include "global_vars.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_OVERMAP_WRITE( "EOC_TEST_OVERMAP_WRITE" );

static const overmap_special_id overmap_special_natural_spring( "natural_spring" );

// OMAP-J01: EOCs could barely read and couldn't write overmap state. They can now set
// terrain, place a special at a point, write notes, reveal partially, and check specials and
// seen levels.
TEST_CASE( "omap-j01_eocs_can_write_and_read_the_overmap", "[audit][eoc][overmap]" )
{
    clear_avatar();
    clear_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();

    avatar &u = get_avatar();
    const tripoint_abs_omt here = u.pos_abs_omt();
    const tripoint_abs_omt ter_omt = here + point( 10, 0 );
    const tripoint_abs_omt note_omt = here + point( 0, 10 );
    const tripoint_abs_omt reveal_omt = here + point( 40, 40 );
    const tripoint_abs_omt special_omt = here + point( 0, -10 );
    overmap_buffer.ter_set( ter_omt, oter_id( "forest" ) );
    overmap_buffer.set_seen( reveal_omt, om_vision_level::unseen );

    dialogue d( get_talker_for( u ), nullptr );
    write_var_value( var_type::context, "ter_loc", &d, project_to<coords::ms>( ter_omt ) );
    write_var_value( var_type::context, "note_loc", &d, project_to<coords::ms>( note_omt ) );
    write_var_value( var_type::context, "reveal_loc", &d, project_to<coords::ms>( reveal_omt ) );
    write_var_value( var_type::context, "special_loc", &d, project_to<coords::ms>( special_omt ) );
    effect_on_condition_EOC_TEST_OVERMAP_WRITE->activate( d );

    CHECK( overmap_buffer.ter( ter_omt ) == oter_id( "field" ) );

    REQUIRE( overmap_buffer.has_note( note_omt ) );
    CHECK( overmap_buffer.note( note_omt ) == "B:R;Bandit camp" );
    CHECK( overmap_buffer.is_marked_dangerous( note_omt ) );

    CHECK( overmap_buffer.seen( reveal_omt ) == om_vision_level::outlines );
    CHECK( globvars.get_global_value( "seen_outlines_check" ) == 1 );
    CHECK( !globvars.maybe_get_global_value( "seen_details_check" ) );

    CHECK( globvars.get_global_value( "overmap_special_placed" ) == 1 );
    CHECK( overmap_buffer.ter( special_omt ) == oter_id( "natural_spring_north" ) );
    CHECK( overmap_buffer.overmap_special_at( special_omt ) == overmap_special_natural_spring );
    CHECK( globvars.get_global_value( "in_special_check" ) == 1 );
}

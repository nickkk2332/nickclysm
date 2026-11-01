#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "global_vars.h"
#include "magic.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "type_id.h"

static const spell_id spell_test_eoc_spell_context( "test_eoc_spell_context" );

// MAGIC-J01: an "effect_on_condition" spell gave its EOC only spell_location, so the EOC
// couldn't tell which spell ran it or scale with the values computed for the cast.
TEST_CASE( "magic-j01_eoc_spells_receive_their_cast_values", "[audit][magic][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();

    avatar &u = get_avatar();
    spell sp( spell_test_eoc_spell_context );
    sp.set_level( u, 5 );
    REQUIRE( sp.get_effective_level() == 5 );
    sp.cast_all_effects( u, u.pos_bub() );

    CHECK( globvars.get_global_value( "spell_ctx_id" ) == "test_eoc_spell_context" );
    CHECK( globvars.get_global_value( "spell_ctx_damage_type" ) == "pure" );
    CHECK( globvars.get_global_value( "spell_ctx_level" ) == 5 );
    // 10 + 5 * 5
    CHECK( globvars.get_global_value( "spell_ctx_damage" ) == 35 );
    CHECK( globvars.get_global_value( "spell_ctx_dot" ) == sp.damage_dot( u ) );
    // 100 + 5 * 100 moves, as turns
    CHECK( globvars.get_global_value( "spell_ctx_duration" ) ==
           to_turns<int>( sp.duration_turns( u ) ) );
    CHECK( globvars.get_global_value( "spell_ctx_duration" ) == 6 );
    CHECK( globvars.get_global_value( "spell_ctx_aoe" ) == sp.aoe( u ) );
    // 5 + 5 * 1
    CHECK( globvars.get_global_value( "spell_ctx_range" ) == 10 );
}

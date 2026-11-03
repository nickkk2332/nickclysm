#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "game.h"
#include "global_vars.h"
#include "magic.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "player_helpers.h"
#include "type_id.h"

static const spell_id spell_test_fake_spell( "test_fake_spell" );

namespace
{
// Global variables are shared by every test: put back what was there
struct globals_restorer {
    global_variables::impl_t saved = get_globals().get_global_values();
    ~globals_restorer() {
        get_globals().set_global_values( saved );
    }
};
} // namespace

// EVT-B03: monster_takes_damage from nothing (bleeding, fire, a fall) was sent without
// talkers, so its EVENT EOCs ran with the avatar as the alpha talker instead of the monster.
TEST_CASE( "evt-b03_monster_hurt_by_nothing_is_the_alpha_talker", "[audit][event]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    globals_restorer restore_globals;
    on_out_of_scope cleanup( [&]() {
        u.remove_value( "audit_evt_b03_hurt" );
        clear_creatures();
    } );
    get_globals().set_global_value( "audit_evt_b03", 1 );

    monster &zombie = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    zombie.apply_damage( nullptr, bodypart_id( "torso" ), 1 );

    CHECK( zombie.maybe_get_value( "audit_evt_b03_hurt" ) != nullptr );
    CHECK( u.maybe_get_value( "audit_evt_b03_hurt" ) == nullptr );
}

// EVT-B03: a spell cast with no caster (by an item, say) sends character_casts_spell for an
// invalid character, and its EVENT EOCs ran on the avatar.  Now they don't run at all.
TEST_CASE( "evt-b03_spell_without_a_caster_runs_no_event_eocs", "[audit][event]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    globals_restorer restore_globals;
    on_out_of_scope cleanup( [&]() {
        u.remove_value( "audit_evt_b03_cast" );
    } );
    get_globals().set_global_value( "audit_evt_b03", 1 );

    const spell sp( spell_test_fake_spell );
    const tripoint_bub_ms target = u.pos_bub() + tripoint_rel_ms( 5, 5, 0 );
    sp.cast_all_effects( target );
    CHECK( u.maybe_get_value( "audit_evt_b03_cast" ) == nullptr );

    // A spell the avatar casts still runs them, on the avatar
    sp.cast_all_effects( u, target );
    CHECK( u.maybe_get_value( "audit_evt_b03_cast" ) != nullptr );
}

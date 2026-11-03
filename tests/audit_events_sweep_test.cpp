#include <sstream>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "explosion.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "player_helpers.h"
#include "string_formatter.h"
#include "timed_event.h"
#include "type_id.h"

static const itype_id itype_nail( "nail" );
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

namespace
{
std::string saved_timed_events()
{
    std::ostringstream os;
    JsonOut jsout( os );
    timed_event_manager::serialize_all( jsout );
    return os.str();
}

void load_timed_events( const std::string &saved )
{
    get_timed_events() = timed_event_manager();
    timed_event_manager::unserialize_all( json_loader::from_string( saved ).get_array() );
}

// Empties the timed event queue for a test, and puts it back afterwards
struct timed_events_restorer {
    std::string saved = saved_timed_events();
    timed_events_restorer() {
        get_timed_events() = timed_event_manager();
    }
    ~timed_events_restorer() {
        load_timed_events( saved );
    }
};
} // namespace

// EVT-B04: a mortar shell is a timed EXPLOSION event.  Its explosion data wasn't saved, so a
// save and load while the shell was in flight turned it into a dud.
TEST_CASE( "evt-b04_timed_explosion_keeps_its_blast_through_save_and_load",
           "[audit][timed_event]" )
{
    timed_events_restorer restore;
    const tripoint_abs_ms target = get_avatar().pos_abs() + tripoint_rel_ms( 30, 30, 0 );
    const explosion_data blast( 42.0f, 0.7f, true, shrapnel_data( 120, 0.05f, 30, itype_nail ) );
    get_timed_events().add( timed_event_type::EXPLOSION, calendar::turn + 1_hours, target, blast );

    load_timed_events( saved_timed_events() );

    const timed_event *shell = get_timed_events().get( timed_event_type::EXPLOSION );
    REQUIRE( shell != nullptr );
    CHECK( shell->map_square == target );
    const explosion_data &loaded = shell->expl_data;
    CHECK( loaded.power == Approx( 42.0f ) );
    CHECK( loaded.distance_factor == Approx( 0.7f ) );
    CHECK( loaded.max_noise == blast.max_noise );
    CHECK( loaded.fire );
    CHECK( loaded.shrapnel.casing_mass == 120 );
    CHECK( loaded.shrapnel.fragment_mass == Approx( 0.05f ) );
    CHECK( loaded.shrapnel.recovery == 30 );
    CHECK( loaded.shrapnel.drop == itype_nail );
}

// EVT-B04: a shell saved by an older build has no explosion data.  It still loads, as a dud.
TEST_CASE( "evt-b04_timed_explosion_from_an_older_save_loads", "[audit][timed_event]" )
{
    timed_events_restorer restore;
    load_timed_events( string_format(
                           R"([{"faction":-1,"map_point":[0,0,0],"map_square":[0,0,0],"strength":-1,)"
                           R"("string_id":"","type":%d,"when":%d,"key":"","revert":"t_null"}])",
                           static_cast<int>( timed_event_type::EXPLOSION ),
                           to_turns<int>( calendar::turn - calendar::turn_zero ) + 600 ) );
    const timed_event *shell = get_timed_events().get( timed_event_type::EXPLOSION );
    REQUIRE( shell != nullptr );
    CHECK( shell->expl_data.power < 0.0f );
}

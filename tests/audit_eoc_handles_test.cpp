#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "item_uid.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "player_helpers.h"
#include "point.h"
#include "string_formatter.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_HANDLE_STORE( "EOC_TEST_HANDLE_STORE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_HANDLE_USE( "EOC_TEST_HANDLE_USE" );

static const itype_id itype_test_rock( "test_rock" );

static const vproto_id vehicle_prototype_bicycle( "bicycle" );
static const vproto_id vehicle_prototype_none( "none" );

namespace
{
// Stores a handle to `who` in the global test_handle
std::string store_handle( std::unique_ptr<talker> who )
{
    dialogue d( std::move( who ), nullptr );
    effect_on_condition_EOC_TEST_HANDLE_STORE->activate( d );
    return get_globals().get_global_value( "test_handle" ).str();
}

// Runs an EOC on whatever the stored handle names, from the avatar
void use_handle()
{
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    effect_on_condition_EOC_TEST_HANDLE_USE->activate( d );
}

double marks( const diag_value *value )
{
    return value == nullptr ? 0 : value->dbl();
}

double misses()
{
    return marks( get_globals().maybe_get_global_value( "test_handle_misses" ) );
}

void clear_handle_globals()
{
    get_globals().remove_global_value( "test_handle" );
    get_globals().remove_global_value( "test_handle_misses" );
}
} // namespace

// EOC-J01: u_set_talker stored getID(), which only characters have, so for a monster, item or
// vehicle it stored "0", and run_eocs' alpha_talker/beta_talker could only find characters.
// The handle now names a monster, item or vehicle by its uid, wherever it has moved to.
TEST_CASE( "eoc-j01_a_stored_handle_finds_a_monster_after_it_moves", "[audit][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_handle_globals();
    on_out_of_scope cleanup( []() {
        clear_handle_globals();
        clear_creatures();
    } );
    map &here = get_map();
    const tripoint_bub_ms start = get_avatar().pos_bub() + tripoint_rel_ms( 3, 0, 0 );
    monster &zombie = spawn_test_monster( "mon_zombie", start );

    const std::string handle = store_handle( get_talker_for( zombie ) );
    CHECK( handle == "monster:" + std::to_string( zombie.persistent_id() ) );

    zombie.setpos( here, start + tripoint_rel_ms( 4, 2, 0 ) );
    use_handle();
    CHECK( marks( zombie.maybe_get_value( "handle_marks" ) ) == 1 );
    CHECK( misses() == 0 );

    // a dead monster isn't found, and the false_eocs run instead, with no error
    zombie.die( &here, nullptr );
    use_handle();
    CHECK( misses() == 1 );
}

TEST_CASE( "eoc-j01_a_stored_handle_finds_an_item_wherever_it_is", "[audit][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_handle_globals();
    on_out_of_scope cleanup( []() {
        clear_handle_globals();
        clear_map_without_vision();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms::east;
    item &rock = here.add_item( spot, item( itype_test_rock ) );
    const int64_t uid = rock.uid().get_value();
    REQUIRE( uid > 0 );
    item_location on_the_ground( map_cursor( here.get_abs( spot ) ), &rock );

    const tripoint_abs_ms where = here.get_abs( spot );
    const std::string handle = store_handle( get_talker_for( on_the_ground ) );
    CHECK( handle == string_format( "item:%d@map:%d,%d,%d", uid, where.x(), where.y(),
                                    where.z() ) );
    use_handle();
    CHECK( marks( rock.maybe_get_value( "handle_marks" ) ) == 1 );

    // Where the handle says it was is only where it's looked for first
    get_globals().set_global_value( "test_handle", string_format( "item:%d@map:%d,%d,%d", uid,
                                    where.x() + 5, where.y(), where.z() ) );
    use_handle();
    CHECK( marks( rock.maybe_get_value( "handle_marks" ) ) == 2 );
    CHECK( misses() == 0 );

    // gone, so the false_eocs run
    on_the_ground.remove_item();
    use_handle();
    CHECK( misses() == 1 );
}

TEST_CASE( "eoc-j01_a_stored_handle_finds_a_vehicle_and_survives_a_save", "[audit][eoc][vehicle]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_vehicles();
    clear_handle_globals();
    on_out_of_scope cleanup( []() {
        clear_handle_globals();
        clear_vehicles();
    } );
    map &here = get_map();
    vehicle *bike = here.add_vehicle( vehicle_prototype_bicycle,
                                      get_avatar().pos_bub() + tripoint_rel_ms( 5, 5, 0 ), 0_degrees, 0,
                                      veh_spawn_status::UNDAMAGED );
    REQUIRE( bike != nullptr );
    // nothing asked for its uid yet, so it has none and saves none
    CHECK( bike->assigned_persistent_id() == 0 );

    const std::string handle = store_handle( get_talker_for( *bike ) );
    REQUIRE( bike->assigned_persistent_id() > 0 );
    CHECK( handle == "vehicle:" + std::to_string( bike->assigned_persistent_id() ) );

    use_handle();
    CHECK( marks( bike->maybe_get_value( "handle_marks" ) ) == 1 );
    CHECK( misses() == 0 );

    // the uid is saved and loaded with the vehicle
    std::ostringstream saved;
    JsonOut json( saved );
    bike->serialize( json );
    vehicle loaded( vehicle_prototype_none );
    loaded.deserialize( json_loader::from_string( saved.str() ) );
    CHECK( loaded.assigned_persistent_id() == bike->assigned_persistent_id() );

    // a vehicle that's gone isn't found
    here.destroy_vehicle( bike );
    use_handle();
    CHECK( misses() == 1 );
}

TEST_CASE( "eoc-j01_a_character_handle_is_still_its_id", "[audit][eoc]" )
{
    clear_avatar();
    clear_handle_globals();
    on_out_of_scope cleanup( []() {
        clear_handle_globals();
    } );
    avatar &u = get_avatar();

    const std::string handle = store_handle( get_talker_for( u ) );
    CHECK( handle == std::to_string( u.getID().get_value() ) );
    use_handle();
    CHECK( marks( u.maybe_get_value( "handle_marks" ) ) == 1 );

    // a character that isn't there runs the false_eocs, as the docs say, with no error
    get_globals().set_global_value( "test_handle", std::string( "987654" ) );
    use_handle();
    CHECK( misses() == 1 );

    // a string that's no handle at all is an error
    get_globals().set_global_value( "test_handle", std::string( "not a handle" ) );
    const std::string error = capture_debugmsg_during( []() {
        use_handle();
    } );
    CHECK_THAT( error, Catch::Matchers::Contains( R"("not a handle" is not a talker)" ) );
}

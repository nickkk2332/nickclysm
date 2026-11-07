#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "dialogue.h"
#include "field_type.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "value_ptr.h"

static const bodypart_str_id body_part_big_head( "big_head" );
static const bodypart_str_id body_part_test_torso( "test_torso" );

static const efftype_id effect_cig( "cig" );
static const efftype_id effect_test_m13b_herb_puff( "test_m13b_herb_puff" );
static const efftype_id effect_test_m13b_recovering( "test_m13b_recovering" );
static const efftype_id effect_test_m13b_venom( "test_m13b_venom" );
static const efftype_id effect_weed_high( "weed_high" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_bandages( "bandages" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_cig_butt( "cig_butt" );
static const itype_id itype_lamp_oil( "lamp_oil" );
static const itype_id itype_laptop( "laptop" );
static const itype_id itype_oil_lamp( "oil_lamp" );
static const itype_id itype_test_m13b_antivenom( "test_m13b_antivenom" );
static const itype_id itype_test_m13b_herb_cig( "test_m13b_herb_cig" );
static const itype_id itype_test_m13b_herb_cig_lit( "test_m13b_herb_cig_lit" );
static const itype_id itype_test_m13b_part_heal( "test_m13b_part_heal" );
static const itype_id itype_test_m13b_quick_rod( "test_m13b_quick_rod" );

static const proficiency_id proficiency_prof_wound_care( "prof_wound_care" );
static const proficiency_id proficiency_prof_wound_care_expert( "prof_wound_care_expert" );

static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_survival( "survival" );

static const weather_type_id weather_clear( "clear" );

static double eval_math( dialogue &d, const std::string_view expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

static const heal_actor &heal_action_of( const item &it )
{
    const use_function *use = it.get_use( "heal" );
    REQUIRE( use != nullptr );
    const heal_actor *actor = dynamic_cast<const heal_actor *>( use->get_actor_ptr() );
    REQUIRE( actor != nullptr );
    return *actor;
}

static double var_of( const Character &who, const std::string &name )
{
    const diag_value *value = who.maybe_get_value( name );
    return value == nullptr ? 0.0 : value->dbl();
}

// USE-J10: an item talker had no way to read or write an item's active state, countdown or
// counter, and its power read any ammo as battery while setting it wrote battery over the
// item's own ammo (deleting a lamp's oil).
TEST_CASE( "use-j10_item_talker_state_and_ammo_functions", "[audit][eoc][items]" )
{
    clear_avatar();
    clear_map();
    const on_out_of_scope cleanup( []() {
        clear_map();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms::east;
    item &lamp = here.add_item( spot, item( itype_oil_lamp ) );
    REQUIRE_FALSE( lamp.is_null() );
    lamp.ammo_unset();
    REQUIRE( lamp.ammo_remaining() == 0 );
    item_location lamp_loc( map_cursor( spot ), &lamp );
    dialogue d( get_talker_for( u ), get_talker_for( lamp_loc ) );

    SECTION( "ammo of any type, and battery-only power" ) {
        eval_math( d, "n_item_ammo('lamp_oil') = 100" );
        CHECK( lamp.ammo_remaining() == 100 );
        CHECK( lamp.ammo_current() == itype_lamp_oil );
        CHECK( eval_math( d, "n_item_ammo('lamp_oil')" ) == 100 );
        eval_math( d, "n_item_ammo('lamp_oil') -= 30" );
        CHECK( lamp.ammo_remaining() == 70 );

        // lamp oil isn't power, and writing power leaves the oil alone
        CHECK( eval_math( d, "n_val('power')" ) == 0 );
        CHECK( eval_math( d, "n_val('power_max')" ) == 0 );
        eval_math( d, "n_val('power') = 10" );
        CHECK( lamp.ammo_remaining() == 70 );
        CHECK( lamp.ammo_current() == itype_lamp_oil );

        // an ammo the lamp can't take changes nothing
        eval_math( d, "n_item_ammo('battery') = 10" );
        CHECK( lamp.ammo_remaining() == 70 );

        // no more than the lamp holds, and 0 empties it
        eval_math( d, "n_item_ammo('lamp_oil') = 100000000" );
        CHECK( lamp.ammo_remaining() == lamp.ammo_capacity( itype_lamp_oil->ammo->type ) );
        eval_math( d, "n_item_ammo('lamp_oil') = 0" );
        CHECK( lamp.ammo_remaining() == 0 );
    }

    SECTION( "active, countdown and counter" ) {
        lamp.active = false;
        CHECK( eval_math( d, "n_item_active()" ) == 0 );
        eval_math( d, "n_item_active() = 1" );
        CHECK( lamp.active );
        CHECK( eval_math( d, "n_item_active()" ) == 1 );
        eval_math( d, "n_item_active() = 0" );
        CHECK_FALSE( lamp.active );

        // item_countdown() is ITEM-J04's: -1 without a countdown, and a negative value cancels it
        CHECK( eval_math( d, "n_item_countdown()" ) == -1 );
        eval_math( d, "n_item_countdown() = 30" );
        CHECK( lamp.countdown_point == calendar::turn + 30_seconds );
        // the countdown runs only on an active item
        CHECK( lamp.active );
        CHECK( eval_math( d, "n_item_countdown()" ) == 30 );
        eval_math( d, "n_item_countdown() = -1" );
        CHECK( lamp.countdown_point == calendar::turn_max );
        CHECK( eval_math( d, "n_item_countdown()" ) == -1 );

        eval_math( d, "n_item_counter() = 12" );
        CHECK( lamp.item_counter == 12 );
        CHECK( eval_math( d, "n_item_counter()" ) == 12 );
        eval_math( d, "n_item_counter() = -5" );
        CHECK( lamp.item_counter == 0 );
    }
}

TEST_CASE( "use-j10_item_power_is_battery_and_charges_are_writable", "[audit][eoc][items]" )
{
    clear_avatar();
    clear_map();
    const on_out_of_scope cleanup( []() {
        clear_map();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms::east;

    // a laptop holds its battery charge itself (a flashlight takes a battery cell)
    item &light = here.add_item( spot, item( itype_laptop ) );
    REQUIRE_FALSE( light.is_null() );
    light.ammo_set( itype_battery, 40 );
    REQUIRE( light.ammo_remaining() == 40 );
    item_location light_loc( map_cursor( spot ), &light );
    dialogue d( get_talker_for( u ), get_talker_for( light_loc ) );
    CHECK( eval_math( d, "n_val('power')" ) == 40 );
    CHECK( eval_math( d, "n_item_ammo('battery')" ) == 40 );
    eval_math( d, "n_val('power') = 25" );
    CHECK( light.ammo_remaining() == 25 );
    // item_charges() is ITEM-J04's: for an item not counted by charges, the ammo loaded in it
    CHECK( eval_math( d, "n_item_charges()" ) == 25 );

    item &rounds = here.add_item( spot, item( itype_9mm, calendar::turn, 50 ) );
    REQUIRE( rounds.charges == 50 );
    item_location rounds_loc( map_cursor( spot ), &rounds );
    dialogue d_rounds( get_talker_for( u ), get_talker_for( rounds_loc ) );
    CHECK( eval_math( d_rounds, "n_item_charges()" ) == 50 );
    // charge_count() on an item talker counts what the item holds, not the stack itself (INV-J07)
    CHECK( eval_math( d_rounds, "n_charge_count('9mm')" ) == 0 );
    eval_math( d_rounds, "n_item_charges() = 20" );
    CHECK( rounds.charges == 20 );
    // ITEM-J04's item_charges() keeps a stack at 1 or more: removing it is die()'s job
    eval_math( d_rounds, "n_item_charges() = 0" );
    CHECK( rounds.charges == 1 );
}

// USE-J08: healing items always used First Aid and the two wound care proficiencies, could cure
// only bite and infection, picked head and torso power by vanilla body part ids and ran nothing
// when done.
TEST_CASE( "use-j08_heal_action_skill_proficiencies_cures_and_finish_eocs", "[audit][iuse]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    const on_out_of_scope cleanup( [&u]() {
        u.remove_value( "m13b_healed" );
        u.remove_value( "m13b_healed_arm" );
        clear_avatar();
    } );

    SECTION( "the fields" ) {
        const item pad( itype_test_m13b_antivenom );
        const heal_actor &actor = heal_action_of( pad );
        CHECK( actor.skill == skill_survival );
        REQUIRE( actor.proficiencies.size() == 1 );
        CHECK( actor.get_move_cost( u ) == 1000 );
        u.add_proficiency( proficiency_prof_wound_care, true );
        CHECK( actor.get_move_cost( u ) == 250 );
        CHECK( actor.get_proficiency_bonus( u ) == Approx( 3.0f ) );

        const bodypart_id arm = body_part_arm_l.id();
        CHECK_FALSE( actor.can_cure( u, arm ) );
        // an effect on no body part is cured through any part
        u.add_effect( effect_test_m13b_venom, 10_minutes );
        REQUIRE( u.has_effect( effect_test_m13b_venom ) );
        CHECK( actor.can_cure( u, arm ) );
        item used( itype_test_m13b_antivenom );
        CHECK( actor.finish_using( u, u, used, arm ) == 1 );
        CHECK_FALSE( u.has_effect( effect_test_m13b_venom ) );
        CHECK( u.has_effect( effect_test_m13b_recovering ) );
        CHECK( var_of( u, "m13b_healed" ) == 1 );
        // the EOC got the treated part
        CHECK( var_of( u, "m13b_healed_arm" ) == 1 );
    }

    SECTION( "vanilla items keep First Aid and wound care" ) {
        const item bandage( itype_bandages );
        const heal_actor &actor = heal_action_of( bandage );
        CHECK( actor.skill == skill_firstaid );
        REQUIRE( actor.proficiencies.size() == 2 );
        REQUIRE( actor.bandages_power > 0 );
        REQUIRE( actor.bleed > 0 );
        u.set_skill_level( skill_firstaid, 2 );
        u.add_proficiency( proficiency_prof_wound_care, true );
        u.add_proficiency( proficiency_prof_wound_care_expert, true );
        // as the hardcoded formulas were: each tier halves the time, and adds 1 and 2 levels
        CHECK( actor.get_move_cost( u ) == actor.move_cost / 2 / 2 );
        const float level = u.get_skill_level( skill_firstaid ) + 1 + 2;
        CHECK( actor.get_bandaged_level( u ) ==
               static_cast<int>( std::round( actor.bandages_power + actor.bandages_scaling * level ) ) );
        const float bleed_level = u.get_skill_level( skill_firstaid ) / 2 + 1 + 2;
        CHECK( actor.get_stopbleed_level( u ) ==
               static_cast<int>( std::round( actor.bleed * ( 1.0f + bleed_level ) ) ) );
    }

    SECTION( "head and torso power go by the part's limb type" ) {
        const item salve( itype_test_m13b_part_heal );
        const heal_actor &actor = heal_action_of( salve );
        CHECK( actor.get_heal_value( u, body_part_head.id() ) == 2 );
        CHECK( actor.get_heal_value( u, body_part_torso.id() ) == 20 );
        CHECK( actor.get_heal_value( u, body_part_arm_l.id() ) == 10 );
        // a mutated head and a modded torso heal as a head and a torso
        CHECK( actor.get_heal_value( u, body_part_big_head.id() ) == 2 );
        CHECK( actor.get_heal_value( u, body_part_test_torso.id() ) == 20 );
    }
}

// USE-J09: SMOKING knew only four vanilla items, so a modded smokable was rejected, and a lit
// item made with transform burned out at once; a lit item's puffs were always cig or weed_high.
TEST_CASE( "use-j09_smoke_action_lights_a_modded_smokable_with_its_own_puffs", "[audit][iuse]" )
{
    clear_avatar();
    clear_map();
    scoped_weather_override weather( weather_clear );
    avatar &u = get_avatar();
    map &here = get_map();
    const on_out_of_scope cleanup( [&u]() {
        u.remove_value( "m13b_puffs" );
        u.remove_value( "m13b_burnt_out" );
        clear_avatar();
        clear_map();
    } );
    // something to light it with
    REQUIRE( here.add_field( u.pos_bub() + tripoint_rel_ms::east, fd_fire, 2, 0_turns, false ) );
    REQUIRE( here.has_nearby_fire( u.pos_bub() ) );
    u.set_hunger( 100 );
    u.set_thirst( 100 );

    item herb( itype_test_m13b_herb_cig );
    const use_function *smoke = herb.get_use( "smoke" );
    REQUIRE( smoke != nullptr );
    const std::optional<int> used = smoke->call( &u, herb, &here, u.pos_bub() );
    REQUIRE( used.has_value() );
    CHECK( *used == 1 );
    CHECK( u.get_hunger() == 99 );
    CHECK( u.get_thirst() == 101 );

    std::vector<item *> lit = u.items_with( []( const item & it ) {
        return it.typeId() == itype_test_m13b_herb_cig_lit;
    } );
    REQUIRE( lit.size() == 1 );
    item &cig = *lit[0];
    CHECK( u.is_worn( cig ) );
    CHECK( cig.active );
    CHECK( cig.item_counter == to_turns<int>( 2_minutes ) );
    // one at a time
    CHECK_FALSE( smoke->call( &u, herb, &here, u.pos_bub() ).has_value() );

    // its own puff, about one turn in ten
    cig.item_counter = 10000;
    for( int i = 0; i < 500 && var_of( u, "m13b_puffs" ) == 0; ++i ) {
        REQUIRE_FALSE( cig.process( here, &u, u.pos_bub() ) );
        REQUIRE( cig.typeId() == itype_test_m13b_herb_cig_lit );
    }
    CHECK( var_of( u, "m13b_puffs" ) == 1 );
    CHECK( u.get_effect_dur( effect_test_m13b_herb_puff ) == 20_seconds );
    CHECK_FALSE( u.has_effect( effect_cig ) );
    CHECK_FALSE( u.has_effect( effect_weed_high ) );

    // burning out runs its EOCs and leaves the butt
    cig.item_counter = 0;
    CHECK_FALSE( cig.process( here, &u, u.pos_bub() ) );
    CHECK( cig.typeId() == itype_cig_butt );
    CHECK( var_of( u, "m13b_burnt_out" ) == 1 );
}

// USE-J11: a C++ use function could take no JSON parameters. Those registered with some read
// them from their object-form use action (FISH_ROD's duration, MAKEMOUND's moves).
TEST_CASE( "use-j11_cpp_use_functions_read_declared_json_parameters", "[audit][iuse]" )
{
    // the fixture's { "type": "FISH_ROD", "duration": "1 h" } loaded: any other member would not
    const item rod( itype_test_m13b_quick_rod );
    CHECK( rod.get_use( "FISH_ROD" ) != nullptr );

    const JsonObject jo = json_loader::from_string(
                              R"({ "duration": "3 h", "moves": 7, "name": "deep" })" );
    iuse_params params;
    params.load( jo, {
        { "duration", iuse_params::kind::duration },
        { "moves", iuse_params::kind::integer },
        { "name", iuse_params::kind::string }
    } );
    CHECK( params.get_duration( "duration", 5_hours ) == 3_hours );
    CHECK( params.get_int( "moves", 18000 ) == 7 );
    CHECK( params.get_string( "name", "" ) == "deep" );

    // what the action doesn't set is the function's default
    const iuse_params none;
    CHECK( none.get_duration( "duration", 5_hours ) == 5_hours );
    CHECK( none.get_int( "moves", 18000 ) == 18000 );
    CHECK( none.get_string( "name", "shallow" ) == "shallow" );
}

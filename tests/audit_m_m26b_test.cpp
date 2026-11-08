#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "clzones.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "iexamine.h"
#include "item.h"
#include "item_location.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "mapdata.h"
#include "math_parser_diag_value.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "requirements.h"
#include "ret_val.h"
#include "talker.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "weather.h"

static const faction_id faction_free_merchants( "free_merchants" );
static const faction_id faction_your_followers( "your_followers" );

static const furn_str_id furn_f_metal_smoking_rack( "f_metal_smoking_rack" );
static const furn_str_id furn_f_metal_smoking_rack_active( "f_metal_smoking_rack_active" );
static const furn_str_id furn_f_smoking_rack( "f_smoking_rack" );
static const furn_str_id furn_f_smoking_rack_active( "f_smoking_rack_active" );
static const furn_str_id furn_f_test_m26b_rack( "f_test_m26b_rack" );
static const furn_str_id furn_f_test_m26b_rack_lit( "f_test_m26b_rack_lit" );

static const itype_id itype_charcoal( "charcoal" );
static const itype_id itype_detergent( "detergent" );
static const itype_id itype_jug_plastic( "jug_plastic" );
static const itype_id itype_liquid_soap( "liquid_soap" );
static const itype_id itype_log( "log" );
static const itype_id itype_meat( "meat" );
static const itype_id itype_meat_smoked( "meat_smoked" );
static const itype_id itype_soap( "soap" );
static const itype_id itype_stick_long( "stick_long" );
static const itype_id itype_test_m26b_cleanser( "test_m26b_cleanser" );
static const itype_id itype_water_clean( "water_clean" );

static const requirement_id requirement_data_test_m26b_wash_cleanser( "test_m26b_wash_cleanser" );
static const requirement_id requirement_data_wash_cleanser( "wash_cleanser" );
static const requirement_id requirement_data_wash_water( "wash_water" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_dirtmound( "t_dirtmound" );
static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_greenhouse( "t_greenhouse" );
static const ter_str_id ter_t_greenhouse_tilled( "t_greenhouse_tilled" );
static const ter_str_id ter_t_stump( "t_stump" );
static const ter_str_id ter_t_test_m26b_alien_soil( "t_test_m26b_alien_soil" );
static const ter_str_id ter_t_test_m26b_alien_tilled( "t_test_m26b_alien_tilled" );
static const ter_str_id ter_t_test_m26b_alien_trunk( "t_test_m26b_alien_trunk" );
static const ter_str_id ter_t_trunk( "t_trunk" );

static const zone_type_id zone_type_AUDIT_M26B_STORAGE( "AUDIT_M26B_STORAGE" );
static const zone_type_id zone_type_LOOT_AUDIT_M26B_DISPLAY( "LOOT_AUDIT_M26B_DISPLAY" );
static const zone_type_id zone_type_LOOT_FOOD( "LOOT_FOOD" );
static const zone_type_id zone_type_LOOT_WOOD( "LOOT_WOOD" );
static const zone_type_id zone_type_NO_NPC_PICKUP( "NO_NPC_PICKUP" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    talk_effect_t effect( jo, "effect", "audit_m_m26b_test" );
    finalize_conditions();
    return effect;
}

static bool check_condition( const_dialogue const &d, const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition( d );
}

static std::string mutator_value( const_dialogue const &d, const std::string &mutator )
{
    const JsonObject jo = json_loader::from_string( R"({ "s": { "mutator": ")" + mutator +
                          R"(" } })" );
    const str_or_var s = get_str_or_var( jo.get_member( "s" ), "s" );
    return s.evaluate( d );
}

static int count_items( map &here, const tripoint_bub_ms &pos, const itype_id &type )
{
    int count = 0;
    for( const item &it : here.i_at( pos ) ) {
        if( it.typeId() == type ) {
            count += it.count();
        }
    }
    return count;
}

static std::vector<itype_id> component_types( const requirement_data &req )
{
    std::vector<itype_id> ret;
    for( const std::vector<item_comp> &alternatives : req.get_components() ) {
        for( const item_comp &comp : alternatives ) {
            ret.push_back( comp.type );
        }
    }
    return ret;
}

static bool has_type( const std::vector<itype_id> &types, const itype_id &type )
{
    return std::find( types.begin(), types.end(), type ) != types.end();
}

// ACT-J08: washing checked and took exactly water/water_clean and soap/detergent/liquid_soap.
// It takes JSON requirements now: this was done as USE-J06 (m13a), whose wash_water and
// wash_cleanser requirements are the defaults and whose washing_requirements name the requirement
// each unit is. Checked here: the vanilla items, and that a mod's own cleanser requirement is what
// the check and the consume use.
TEST_CASE( "act-j08_washing_takes_the_json_requirements", "[audit][activity]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );
    // warm enough that the water stays liquid
    restore_on_out_of_scope restore_temperature( get_weather().forced_temperature );
    get_weather().forced_temperature = units::from_celsius( 20 );

    const std::vector<itype_id> water = component_types( requirement_data_wash_water.obj() );
    CHECK( has_type( water, itype_water_clean ) );
    const std::vector<itype_id> cleanser = component_types( requirement_data_wash_cleanser.obj() );
    CHECK( has_type( cleanser, itype_soap ) );
    CHECK( has_type( cleanser, itype_detergent ) );
    CHECK( has_type( cleanser, itype_liquid_soap ) );
    CHECK( has_type( component_types( requirement_data_test_m26b_wash_cleanser.obj() ),
                     itype_test_m26b_cleanser ) );

    // 8 units of water, 2 of the mod's cleanser
    washing_requirements required{ 8, 2, 100 };
    required.water_requirement = requirement_data_wash_water;
    required.cleanser_requirement = requirement_data_test_m26b_wash_cleanser;
    CHECK_FALSE( has_washing_supplies( u, required ) );

    item jug( itype_jug_plastic );
    REQUIRE( jug.put_in( item( itype_water_clean, calendar::turn, 12 ),
                         pocket_type::CONTAINER ).success() );
    here.add_item( u.pos_bub(), jug );
    u.invalidate_crafting_inventory();
    // water, but no cleanser yet
    CHECK_FALSE( has_washing_supplies( u, required ) );

    here.add_item( u.pos_bub(), item( itype_test_m26b_cleanser ) );
    here.add_item( u.pos_bub(), item( itype_test_m26b_cleanser ) );
    u.invalidate_crafting_inventory();
    REQUIRE( has_washing_supplies( u, required ) );

    consume_washing_supplies( u, required );
    u.invalidate_crafting_inventory();
    const temp_crafting_inventory &inv = u.crafting_inventory();
    CHECK( inv.charges_of( itype_water_clean ) == 4 );
    CHECK( inv.amount_of( itype_test_m26b_cleanser, false ) == 0 );
    CHECK_FALSE( has_washing_supplies( u, required ) );
}

// ACT-J09: EOCs could only read a zone's position. They can create, toggle and remove zones,
// test whether a place is in one, read a zone talker's faction and state, find a zone of any
// type with u_location_variable, and pick the faction whose zones u_run_fixed_zone_eocs runs.
TEST_CASE( "act-j09_eocs_create_test_toggle_and_remove_zones", "[audit][zones]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    zone_manager &mgr = zone_manager::get_manager();
    global_variables &globals = get_globals();
    on_out_of_scope cleanup( [&mgr, &globals]() {
        mgr.clear();
        mgr.cache_data();
        globals.clear_global_values();
    } );
    mgr.clear();
    mgr.cache_data();
    globals.clear_global_values();

    dialogue d( get_talker_for( u ), nullptr );
    const tripoint_abs_ms spot = u.pos_abs() + tripoint( 3, 0, 0 );
    const tripoint_abs_ms far_corner = spot + tripoint( 2, 1, 0 );
    d.set_value( "spot", spot );
    d.set_value( "far_corner", far_corner );
    d.set_value( "here", u.pos_abs() );

    // the corners in any order
    load_effect(
        R"({ "effect": { "create_zone": "LOOT_FOOD", "start": { "context_val": "far_corner" }, )"
        R"("end": { "context_val": "spot" }, "name": "audit pantry" } })" ).apply( d );
    // looked up again each time: adding a zone can move the others
    const auto find_pantry = [&mgr]() -> const zone_data * {
        for( const zone_data &zone : mgr.get_zones( faction_your_followers ) )
        {
            if( zone.get_name() == "audit pantry" ) {
                return &zone;
            }
        }
        return nullptr;
    };
    const zone_data *pantry = find_pantry();
    REQUIRE( pantry != nullptr );
    CHECK( pantry->get_type() == zone_type_LOOT_FOOD );
    CHECK( pantry->get_start_point() == spot );
    CHECK( pantry->get_end_point() == far_corner );

    const std::string in_pantry =
        R"({ "condition": { "map_in_zone": "LOOT_FOOD", "loc": { "context_val": "spot" } } })";
    CHECK( check_condition( d, in_pantry ) );
    CHECK( check_condition( d,
                            R"({ "condition": { "map_in_zone": "", "loc": { "context_val": "far_corner" } } })" ) );
    CHECK_FALSE( check_condition( d,
                                  R"({ "condition": { "map_in_zone": "LOOT_WOOD", "loc": { "context_val": "spot" } } })" ) );
    CHECK_FALSE( check_condition( d,
                                  R"({ "condition": { "map_in_zone": "LOOT_FOOD", "loc": { "context_val": "spot" }, )"
                                  R"("faction": "free_merchants" } })" ) );
    CHECK_FALSE( check_condition( d, R"({ "condition": { "u_in_zone": "" } })" ) );

    // a one-tile zone where the avatar stands, of the merchants
    load_effect( R"({ "effect": { "create_zone": "LOOT_WOOD", "start": { "context_val": "here" }, )"
                 R"("faction": "free_merchants" } })" ).apply( d );
    CHECK( check_condition( d,
                            R"({ "condition": { "u_in_zone": "LOOT_WOOD", "faction": "free_merchants" } })" ) );
    CHECK_FALSE( check_condition( d, R"({ "condition": { "u_in_zone": "LOOT_WOOD" } })" ) );

    // a zone of any type, found from the avatar
    load_effect( R"({ "effect": { "u_location_variable": { "context_val": "found" }, "zone": "", )"
                 R"("target_max_radius": 6 } })" ).apply( d );
    CHECK( check_condition( d,
                            R"({ "condition": { "map_in_zone": "", "loc": { "context_val": "found" } } })" ) );

    // the zone talker's faction and state
    pantry = find_pantry();
    REQUIRE( pantry != nullptr );
    {
        const_dialogue dz( get_talker_for( *pantry ), nullptr );
        CHECK( mutator_value( dz, "u_zone_faction" ) == "your_followers" );
        CHECK( check_condition( dz, R"({ "condition": "u_zone_enabled" })" ) );
        CHECK( mutator_value( d, "u_zone_faction" ).empty() );
        CHECK_FALSE( check_condition( d, R"({ "condition": "u_zone_enabled" })" ) );
    }

    // switched off, it doesn't count; switched on again, it does
    load_effect( R"({ "effect": { "set_zones_enabled": "LOOT_FOOD", "enabled": false, )"
                 R"("loc": { "context_val": "spot" } } })" ).apply( d );
    CHECK_FALSE( pantry->get_enabled() );
    CHECK_FALSE( check_condition( d, in_pantry ) );
    load_effect(
        R"({ "effect": { "set_zones_enabled": "", "loc": { "context_val": "spot" } } })" ).apply(
            d );
    CHECK( pantry->get_enabled() );
    CHECK( check_condition( d, in_pantry ) );

    // only the merchants' zone runs the EOC
    load_effect( R"({ "effect": { "u_run_fixed_zone_eocs": [ "EOC_AUDIT_M26B_COUNT_ZONE" ], )"
                 R"("faction": "free_merchants" } })" ).apply( d );
    CHECK( globals.get_global_value( "audit_m26b_zone_count" ) == 1 );
    CHECK( globals.get_global_value( "audit_m26b_zone_faction" ) == "free_merchants" );

    // removed; the merchants' zone stays
    load_effect( R"({ "effect": { "remove_zones": "", "loc": { "context_val": "spot" } } })" ).apply(
        d );
    CHECK_FALSE( check_condition( d, in_pantry ) );
    CHECK( mgr.get_zones( faction_your_followers ).empty() );
    CHECK( mgr.get_zones( faction_free_merchants ).size() == 1 );
}

// ACT-J10: only t_trunk and t_stump could be chopped, into fixed items and dirt; tilling always
// made a mound of dirt in 18000 moves, and planting always left dirt (or the greenhouse). The
// terrain's chop, till and plant_result say so now.
TEST_CASE( "act-j10_chop_till_and_plant_results_come_from_the_terrain", "[audit][activity]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint::east;
    on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );

    // the vanilla trunk and stump keep what they gave
    CHECK( ter_t_trunk->can_chop() );
    CHECK( ter_t_stump->can_chop() );
    CHECK_FALSE( ter_t_dirt->can_chop() );
    REQUIRE( ter_t_trunk->chop->result() == ter_t_dirt );
    REQUIRE( ter_t_stump->chop->result() == ter_t_dirt );

    SECTION( "a vanilla trunk" ) {
        REQUIRE( here.ter_set( spot, ter_t_trunk ) );
        u.assign_activity( chop_logs_activity_actor( 1, item_location() ) );
        u.activity.placement = here.get_abs( spot );
        REQUIRE( process_activity_bounded( u, 100, 1000 ) );
        CHECK( here.ter( spot ) == ter_t_dirt );
        const int logs = count_items( here, spot, itype_log );
        const int sticks = count_items( here, spot, itype_stick_long );
        CHECK( logs >= 2 );
        CHECK( logs <= 3 );
        CHECK( sticks >= 0 );
        CHECK( sticks <= 3 );
    }
    SECTION( "a mod's trunk" ) {
        REQUIRE( ter_t_test_m26b_alien_trunk->can_chop() );
        REQUIRE( here.ter_set( spot, ter_t_test_m26b_alien_trunk ) );
        u.assign_activity( chop_logs_activity_actor( 1, item_location() ) );
        u.activity.placement = here.get_abs( spot );
        REQUIRE( process_activity_bounded( u, 100, 1000 ) );
        CHECK( here.ter( spot ) == ter_t_grass );
        CHECK( count_items( here, spot, itype_test_m26b_cleanser ) == 2 );
        CHECK( count_items( here, spot, itype_log ) == 0 );
    }
    SECTION( "tilling" ) {
        CHECK( ter_t_greenhouse->till_result() == ter_t_greenhouse_tilled );
        CHECK( ter_t_dirt->till_result() == ter_t_dirtmound );
        CHECK( ter_t_dirt->till_moves() == 18000 );
        CHECK( ter_t_test_m26b_alien_soil->till_moves() == to_moves<int>( 10_minutes ) );

        REQUIRE( here.ter_set( spot, ter_t_test_m26b_alien_soil ) );
        u.assign_activity( churn_activity_actor( 1, item_location() ) );
        u.activity.placement = here.get_abs( spot );
        REQUIRE( process_activity_bounded( u, 100, 1000 ) );
        CHECK( here.ter( spot ) == ter_t_test_m26b_alien_tilled );
    }
    SECTION( "planting" ) {
        CHECK( ter_t_dirtmound->planted_terrain() == ter_t_dirt );
        CHECK( ter_t_greenhouse_tilled->planted_terrain() == ter_t_greenhouse );
        CHECK( ter_t_test_m26b_alien_tilled->planted_terrain() == ter_t_test_m26b_alien_soil );
    }
}

// ACT-J11: the smoking racks were five hardcoded furniture ids; a modded rack with the
// smoker_options action failed with a debugmsg. The furniture's "smoker" object names its lit
// and unlit forms and its capacity.
TEST_CASE( "act-j11_a_mod_smoking_rack_lights_and_goes_out", "[audit][examine]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint::east;
    on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );
    restore_on_out_of_scope restore_temperature( get_weather().forced_temperature );
    get_weather().forced_temperature = units::from_celsius( 20 );

    // the vanilla racks, as they were
    REQUIRE( furn_f_smoking_rack->smoker );
    CHECK( furn_f_smoking_rack->smoker->lit == furn_f_smoking_rack_active );
    CHECK( furn_f_smoking_rack->smoker->capacity == units::from_liter( 20 ) );
    REQUIRE( furn_f_smoking_rack_active->smoker );
    CHECK( furn_f_smoking_rack_active->smoker->unlit == furn_f_smoking_rack );
    REQUIRE( furn_f_metal_smoking_rack->smoker );
    CHECK( furn_f_metal_smoking_rack->smoker->lit == furn_f_metal_smoking_rack_active );
    CHECK( furn_f_metal_smoking_rack->smoker->capacity == units::from_liter( 15 ) );
    REQUIRE( furn_f_metal_smoking_rack_active->smoker );
    CHECK( furn_f_metal_smoking_rack_active->smoker->unlit == furn_f_metal_smoking_rack );

    using start_type = firestarter_actor::start_type;
    CHECK( firestarter_actor::furniture_start_type( furn_f_smoking_rack ) == start_type::SMOKER );
    CHECK( firestarter_actor::furniture_start_type( furn_f_test_m26b_rack ) == start_type::SMOKER );
    CHECK( firestarter_actor::furniture_start_type( furn_f_test_m26b_rack_lit ) ==
           start_type::FIRE );

    REQUIRE( here.furn_set( spot, furn_f_test_m26b_rack ) );
    here.add_item( spot, item( itype_charcoal, calendar::turn, 2000 ) );

    // more food than its 1 L
    units::volume food = 0_ml;
    for( int i = 0; i < 100 && food <= units::from_liter( 1 ); ++i ) {
        item meat( itype_meat, calendar::turn );
        food += meat.volume();
        here.add_item( spot, meat );
    }
    REQUIRE( food > units::from_liter( 1 ) );
    CHECK_FALSE( iexamine::smoker_prep( u, spot ) );

    here.i_clear( spot );
    here.add_item( spot, item( itype_charcoal, calendar::turn, 2000 ) );
    here.add_item( spot, item( itype_meat, calendar::turn ) );
    CHECK( iexamine::smoker_prep( u, spot ) );
    REQUIRE( iexamine::smoker_fire( u, spot ) );
    CHECK( here.furn( spot ) == furn_f_test_m26b_rack_lit );

    iexamine::on_smoke_out( here, spot, calendar::turn );
    CHECK( here.furn( spot ) == furn_f_test_m26b_rack );
    CHECK( count_items( here, spot, itype_meat_smoked ) == 1 );
}

// ACT-J12: where fetching and NPC traders take items from was every zone type whose id starts
// with LOOT. The zone type's loot_source says so now, with that as its default.
TEST_CASE( "act-j12_loot_source_decides_where_items_are_taken_from", "[audit][zones]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    avatar &u = get_avatar();
    zone_manager &mgr = zone_manager::get_manager();
    on_out_of_scope cleanup( [&mgr]() {
        mgr.clear();
        mgr.cache_data();
    } );
    mgr.clear();
    mgr.cache_data();

    CHECK( zone_type_LOOT_FOOD->loot_source );
    CHECK( zone_type_LOOT_WOOD->loot_source );
    CHECK_FALSE( zone_type_NO_NPC_PICKUP->loot_source );
    CHECK( zone_type_AUDIT_M26B_STORAGE->loot_source );
    CHECK_FALSE( zone_type_LOOT_AUDIT_M26B_DISPLAY->loot_source );

    const tripoint_abs_ms storage = u.pos_abs() + tripoint( 2, 0, 0 );
    const tripoint_abs_ms display = u.pos_abs() + tripoint( 0, 2, 0 );
    const tripoint_abs_ms food = u.pos_abs() + tripoint( -2, 0, 0 );
    mgr.add( "audit storage", zone_type_AUDIT_M26B_STORAGE, faction_your_followers, false, true,
             storage, storage, nullptr, true, &here );
    mgr.add( "audit display", zone_type_LOOT_AUDIT_M26B_DISPLAY, faction_your_followers, false,
             true, display, display, nullptr, true, &here );
    mgr.add( "audit food", zone_type_LOOT_FOOD, faction_your_followers, false, true, food, food,
             nullptr, true, &here );

    const std::unordered_set<tripoint_bub_ms> loot = mgr.get_point_set_loot( u.pos_abs(), 5,
            faction_your_followers );
    CHECK( loot.count( here.get_bub( storage ) ) == 1 );
    CHECK( loot.count( here.get_bub( food ) ) == 1 );
    CHECK( loot.count( here.get_bub( display ) ) == 0 );

    CHECK( mgr.get_zone_at( storage, true, faction_your_followers ) != nullptr );
    CHECK( mgr.get_zone_at( display, true, faction_your_followers ) == nullptr );
    CHECK( mgr.get_zone_at( display, false, faction_your_followers ) != nullptr );
}

#include <list>
#include <optional>
#include <string>

#include "activity_item_handling.h"
#include "avatar.h"
#include "ballistics.h"
#include "calendar.h"
#include "cata_catch.h"
#include "condition.h"
#include "damage.h"
#include "dialogue.h"
#include "dispersion.h"
#include "field_type.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapdata.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "pocket_type.h"
#include "projectile.h"
#include "talker.h"
#include "type_id.h"

TEST_CASE( "interactivity_drop_callbacks_use_the_actual_dropper_or_no_actor",
           "[audit][interactivity][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    map &here = get_map();
    avatar &u = get_avatar();
    u.set_value( "twelve_dropper", "avatar" );
    const tripoint_bub_ms destination = u.pos_bub() + tripoint_rel_ms::east;
    const item dropped( itype_id( "test_twelve_drop_item" ), calendar::turn );
    std::string expected;
    SECTION( "avatar drop" ) {
        expected = "avatar";
        drop_on_map( u, item_drop_reason::deliberate, { dropped }, &here, destination );
    }
    SECTION( "NPC drop" ) {
        expected = "npc";
        npc &other = spawn_npc( u.pos_bub().xy() + point_rel_ms( 3, 0 ), "thug" );
        other.set_value( "twelve_dropper", "npc" );
        drop_on_map( other, item_drop_reason::deliberate, { dropped }, &here, destination );
    }
    SECTION( "placement has no dropping character" ) {
        expected = "environment";
        here.add_item_or_charges( destination, dropped );
    }
    SECTION( "a projectile remembers its NPC thrower" ) {
        expected = "npc";
        npc &other = spawn_npc( u.pos_bub().xy() + point_rel_ms( 3, 0 ), "thug" );
        other.set_value( "twelve_dropper", "npc" );
        projectile proj;
        proj.range = 4;
        proj.speed = 10;
        proj.impact.add_damage( damage_type_id( "pure" ), 10 );
        proj.proj_effects.insert( ammo_effect_str_id( "NO_OVERSHOOT" ) );
        proj.set_drop( dropped );
        dealt_projectile_attack attack;
        projectile_attack( attack, proj, &here, other.pos_bub(), destination,
                           dispersion_sources( 0 ), &other );
        CHECK( attack.end_point == destination );
    }
    REQUIRE( here.i_at( destination ).size() == 1 );
    CHECK( here.i_at( destination ).only_item().get_var( "twelve_dropped" ) == "yes" );
    CHECK( get_globals().get_global_value( "twelve_dropper" ) == expected );
    CHECK( get_globals().get_global_value( "twelve_drop_location" ).tripoint() == here.get_abs(
               destination ) );
    CHECK( get_globals().get_global_value( "twelve_drop_item_location" ).tripoint() == here.get_abs(
               destination ) );
}

TEST_CASE( "interactivity_examine_context_identifies_the_dispatched_layer",
           "[audit][interactivity][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms destination = u.pos_bub() + tripoint_rel_ms::east;
    const ter_str_id terrain( "test_twelve_terrain" );
    const furn_str_id furniture( "test_twelve_furniture" );
    here.ter_set( destination, terrain );
    const bool npc_user = GENERATE( false, true );
    Character &examiner = npc_user ? static_cast<Character &>( spawn_npc(
                              u.pos_bub().xy() + point_rel_ms( 3, 0 ), "thug" ) ) : u;
    std::string expected;
    SECTION( "bare terrain" ) {
        expected = terrain.str();
        here.ter( destination )->examine( examiner, destination );
        CHECK( get_globals().get_global_value( "twelve_examined_examined_layer" ) == "terrain" );
    }
    SECTION( "furniture" ) {
        expected = furniture.str();
        here.furn_set( destination, furniture );
        here.furn( destination )->examine( examiner, destination );
        CHECK( get_globals().get_global_value( "twelve_examined_examined_layer" ) == "furniture" );
    }
    SECTION( "explicit terrain examination under furniture" ) {
        expected = terrain.str();
        here.furn_set( destination, furniture );
        here.ter( destination )->examine( examiner, destination );
        CHECK( get_globals().get_global_value( "twelve_examined_examined_layer" ) == "terrain" );
    }
    CHECK( get_globals().get_global_value( "twelve_examined_this" ) == expected );
    CHECK( get_globals().get_global_value( "twelve_examined_terrain" ) == terrain.str() );
    CHECK( get_globals().get_global_value( "twelve_examined_furniture" ) == here.furn(
               destination ).id().str() );
}

TEST_CASE( "interactivity_drop_callback_can_explicitly_consume_the_item",
           "[audit][interactivity][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_globals().clear_global_values();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms destination = u.pos_bub() + tripoint_rel_ms::east;
    const item dropped( itype_id( "test_twelve_consumed_drop_item" ), calendar::turn );
    const bool has_dropper = GENERATE( false, true );
    here.add_item_or_charges( destination, dropped, true, has_dropper ? &u : nullptr );
    CHECK( here.i_at( destination ).empty() );
    CHECK( get_globals().get_global_value( "twelve_drop_item_location" ).tripoint() == here.get_abs(
               destination ) );
}

TEST_CASE( "interactivity_falling_item_keeps_its_impact_damage", "[audit][interactivity][map]" )
{
    clear_avatar();
    clear_map_without_vision( -2, 1 );
    map &here = get_map();
    const tripoint_bub_ms above( 40, 40, 1 );
    const tripoint_bub_ms below( 40, 40, 0 );
    here.ter_set( above, ter_str_id( "t_open_air" ) );
    here.ter_set( below, ter_str_id( "t_floor" ) );
    item dropped( itype_id( "test_twelve_falling_item" ), calendar::turn );
    REQUIRE( dropped.resist( damage_type_id( "bash" ), true ) == 0 );
    here.add_item( above, dropped );
    here.process_falling();
    CHECK( here.i_at( above ).empty() );
    REQUIRE( here.i_at( below ).size() == 1 );
    CHECK( here.i_at( below ).only_item().damage() > 0 );
}

TEST_CASE( "interactivity_removed_ground_item_is_not_consumed_again_after_its_EOC",
           "[audit][interactivity][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms origin = u.pos_bub() + tripoint_rel_ms::east;
    const tripoint_bub_ms destination = origin + tripoint_rel_ms::east;
    get_globals().set_global_value( "twelve_item_destination", here.get_abs( destination ) );
    item &source = here.add_item( origin, item( itype_id( "test_twelve_relocating_item" ) ) );
    // The EOC teleports by making a new item and removing the original reference.
    const std::optional<int> consumed = source.type->invoke( &u, source, &here, origin );
    REQUIRE( consumed.has_value() );
    CHECK( *consumed == 0 );
    CHECK( here.i_at( origin ).empty() );
    REQUIRE( here.i_at( destination ).size() == 1 );
    CHECK( here.i_at( destination ).only_item().typeId() == itype_id( "test_twelve_relocating_item" ) );
}

TEST_CASE( "interactivity_weightless_falls_do_not_divide_by_zero_or_damage_items",
           "[audit][interactivity][map]" )
{
    clear_avatar();
    clear_map_without_vision( -2, 1 );
    map &here = get_map();
    const tripoint_bub_ms above( 40, 40, 1 );
    const tripoint_bub_ms below( 40, 40, 0 );
    here.ter_set( above, ter_str_id( "t_open_air" ) );
    here.ter_set( below, ter_str_id( "t_floor" ) );
    here.add_item( above, item( itype_id( "test_twelve_weightless_item" ), calendar::turn ) );
    here.process_falling();
    REQUIRE( here.i_at( below ).size() == 1 );
    CHECK( here.i_at( below ).only_item().damage() == 0 );
}

TEST_CASE( "interactivity_fall_destroys_a_broken_item_instead_of_placing_a_copy",
           "[audit][interactivity][map]" )
{
    clear_avatar();
    clear_map_without_vision( -2, 1 );
    map &here = get_map();
    const tripoint_bub_ms above( 40, 40, 1 );
    const tripoint_bub_ms below( 40, 40, 0 );
    here.ter_set( above, ter_str_id( "t_open_air" ) );
    here.ter_set( below, ter_str_id( "t_floor" ) );
    item dropped( itype_id( "test_twelve_falling_item" ), calendar::turn );
    dropped.set_damage( dropped.max_damage() );
    here.add_item( above, dropped );
    here.process_falling();
    CHECK( here.i_at( above ).empty() );
    CHECK( here.i_at( below ).empty() );
}

TEST_CASE( "interactivity_fall_merges_only_the_surviving_charges", "[audit][interactivity][map]" )
{
    clear_avatar();
    clear_map_without_vision( -2, 1 );
    map &here = get_map();
    const tripoint_bub_ms above( 40, 40, 1 );
    const tripoint_bub_ms below( 40, 40, 0 );
    here.ter_set( above, ter_str_id( "t_open_air" ) );
    here.ter_set( below, ter_str_id( "t_floor" ) );
    item dropped( itype_id( "test_twelve_falling_charges" ), calendar::turn );
    dropped.charges = 5;
    item existing = dropped;
    existing.charges = 10;
    REQUIRE( dropped.count_by_charges() );
    REQUIRE( dropped.resist( damage_type_id( "bash" ), true ) == 0 );
    here.add_item( below, existing );
    here.add_item( above, dropped );
    here.process_falling();
    CHECK( here.i_at( above ).empty() );
    REQUIRE( here.i_at( below ).size() == 1 );
    CHECK( here.i_at( below ).only_item().charges == 14 );
}

TEST_CASE( "interactivity_destroyed_falling_container_preserves_its_contents",
           "[audit][interactivity][map]" )
{
    clear_avatar();
    clear_map_without_vision( -2, 1 );
    map &here = get_map();
    const tripoint_bub_ms above( 40, 40, 1 );
    const tripoint_bub_ms below( 40, 40, 0 );
    here.ter_set( above, ter_str_id( "t_open_air" ) );
    here.ter_set( below, ter_str_id( "t_floor" ) );
    item dropped( itype_id( "test_twelve_falling_container" ), calendar::turn );
    REQUIRE( dropped.put_in( item( itype_id( "rock" ) ), pocket_type::CONTAINER ).success() );
    dropped.set_damage( dropped.max_damage() );
    here.add_item( above, dropped );
    here.process_falling();
    CHECK( here.i_at( above ).empty() );
    REQUIRE( here.i_at( below ).size() == 1 );
    CHECK( here.i_at( below ).only_item().typeId() == itype_id( "rock" ) );
}

TEST_CASE( "interactivity_emissions_use_the_emitter_not_the_avatar",
           "[audit][interactivity][map][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    map &here = get_map();
    avatar &u = get_avatar();
    u.set_value( "twelve_intensity", 3.0 );
    const tripoint_bub_ms destination( 40, 40, 0 );
    const emit_id emission( "test_twelve_emit" );
    const field_type_id smoke( "fd_smoke" );
    int expected = 1;
    SECTION( "environment" ) {
        here.emit_field( destination, emission );
    }
    SECTION( "NPC" ) {
        npc &source = spawn_npc( u.pos_bub().xy() + point_rel_ms( 3, 0 ), "thug" );
        source.set_value( "twelve_intensity", 2.0 );
        expected = 2;
        here.emit_field( destination, emission, 1.0f, &source );
    }
    SECTION( "monster" ) {
        monster &source = spawn_test_monster( "mon_zombie", u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
        source.set_value( "twelve_intensity", 2.0 );
        expected = 2;
        here.emit_field( destination, emission, 1.0f, &source );
    }
    SECTION( "beta EOC emitter becomes alpha for emission formulas" ) {
        npc &source = spawn_npc( u.pos_bub().xy() + point_rel_ms( 3, 0 ), "thug" );
        source.set_value( "twelve_intensity", 2.0 );
        expected = 2;
        dialogue d( get_talker_for( u ), get_talker_for( source ) );
        d.set_value( "test_emit_pos", here.get_abs( destination ) );
        const JsonObject jo = json_loader::from_string( R"({ "effect": {
            "npc_emit": "test_twelve_emit", "target_var": { "context_val": "test_emit_pos" }
        } })" );
        const talk_effect_t effect( jo, "effect", "audit_emission_context" );
        finalize_conditions();
        effect.apply( d );
    }
    SECTION( "two identical item emitters have independent state" ) {
        item &source = here.add_item( destination, item( itype_id( "test_twelve_emitter" ) ) );
        source.set_var( "twelve_intensity", 2.0 );
        source.type->drop_action.call( nullptr, source, &here, destination );
        CHECK( here.get_field_intensity( destination, smoke ) == 2 );
        here.remove_field( destination, smoke );
        source.set_var( "twelve_intensity", 1.0 );
        source.type->drop_action.call( nullptr, source, &here, destination );
    }
    CHECK( here.get_field_intensity( destination, smoke ) == expected );
}

#include <list>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_actor_eoc.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "ret_val.h"
#include "talker.h"
#include "type_id.h"
#include "veh_type.h"
#include "veh_utils.h"
#include "vehicle.h"

// ACT-J02: u_assign_activity starts more of the activities that need a target ("target_item",
// "target_loc", "target_monster"), with no menus.
// ACT-B02: the zone vehicle repair checks the tools and components the repair itself can reach.
// UIINV-B02: an inventory drag and drop moves the item through the insert activity.

static const activity_id ACT_BOLTCUTTING( "ACT_BOLTCUTTING" );
static const activity_id ACT_CLEAR_RUBBLE( "ACT_CLEAR_RUBBLE" );
static const activity_id ACT_DROP( "ACT_DROP" );
static const activity_id ACT_INSERT_ITEM( "ACT_INSERT_ITEM" );
static const activity_id ACT_MILK( "ACT_MILK" );
static const activity_id ACT_MOVE_ITEMS( "ACT_MOVE_ITEMS" );
static const activity_id ACT_PICKUP( "ACT_PICKUP" );
static const activity_id ACT_PLAY_WITH_PET( "ACT_PLAY_WITH_PET" );
static const activity_id ACT_PRYING( "ACT_PRYING" );
static const activity_id ACT_WIELD( "ACT_WIELD" );

static const efftype_id effect_pet( "pet" );

static const furn_str_id furn_f_rubble( "f_rubble" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_boltcutters( "boltcutters" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_jug_plastic( "jug_plastic" );
static const itype_id itype_milk_raw( "milk_raw" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_shovel( "shovel" );
static const itype_id itype_stick( "stick" );
static const itype_id
itype_test_audit_partial_two_pocket_bag( "test_audit_partial_two_pocket_bag" );

static const skill_id skill_mechanics( "mechanics" );

static const ter_str_id ter_t_chainfence( "t_chainfence" );
static const ter_str_id ter_t_chainfence_posts( "t_chainfence_posts" );
static const ter_str_id ter_t_wall( "t_wall" );

static const vpart_id vpart_test_audit_partial_hammer_frame( "test_audit_partial_hammer_frame" );

static const vproto_id vehicle_prototype_none( "none" );

namespace
{

talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "audit_partial_activities_test" );
    finalize_conditions();
    return ret;
}

// u_assign_activity with `fields`, and the test's true_eocs and false_eocs, run in `d`.
void assign( const std::string &fields, dialogue &d )
{
    load_effect( R"({ "u_assign_activity": )" + fields +
                 R"(, "true_eocs": [ "EOC_TEST_ERRAND_STARTED" ], )"
                 R"("false_eocs": [ "EOC_TEST_ERRAND_REFUSED" ] })" ).apply( d );
}

std::string global_string( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

// "started", "refused" or "" (neither ran)
std::string result()
{
    return global_string( "test_errand_result" );
}

std::string reason()
{
    return global_string( "test_errand_reason" );
}

void run_to_the_end( Character &who, const activity_id &type )
{
    int guard = 0;
    while( who.activity.id() == type ) {
        REQUIRE( ++guard < 100000 );
        who.set_moves( 100 );
        who.activity.do_turn( who );
        calendar::turn += 1_turns;
    }
}

// A clear map at noon, and the avatar with nothing on.
void setup()
{
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    clear_avatar();
    get_globals().clear_global_values();
    set_time( calendar::turn_zero + 12_hours );
}

// An NPC next to the avatar, with fixed stats and nothing on.
npc &spawn_helper()
{
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
    const tripoint_abs_ms pos = guy.pos_abs();
    clear_character( guy );
    guy.setpos( pos );
    return guy;
}

bool wield_new( Character &who, const itype_id &type )
{
    item it( type );
    return who.wield( it );
}

item_location on_ground( const tripoint_bub_ms &pos, const item &it )
{
    item &placed = get_map().add_item( pos, it );
    return item_location( map_cursor( pos ), &placed );
}

int count_of( const std::list<item *> &items, const itype_id &type )
{
    int count = 0;
    for( const item *it : items ) {
        if( it->typeId() == type ) {
            count += it->count_by_charges() ? it->charges : 1;
        }
    }
    return count;
}

int count_on_ground( const tripoint_bub_ms &pos, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( pos ) ) {
        if( it.typeId() == type ) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE( "act-j02_more_targeted_activities_are_checked_at_load", "[audit][activity]" )
{
    // A place, and maybe the tool.
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_PRYING" })" ),
                       Catch::Matchers::Contains( "ACT_PRYING needs \"target_loc\"" ) );
    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_PRYING", )"
                                R"("target_loc": { "context_val": "spot" } })" ) );
    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_BOLTCUTTING", "target_item": "npc", )"
                                R"("target_loc": { "context_val": "spot" } })" ) );
    // A monster: the other talker.
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_MILK" })" ),
                       Catch::Matchers::Contains( "ACT_MILK needs \"target_monster\"" ) );
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_MILK", "target_monster": "u" })" ),
                       Catch::Matchers::Contains( "the other talker" ) );
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_SHEARING", )"
                                    R"("target_monster": "npc", "target_item": "npc" })" ),
                       Catch::Matchers::Contains( "ACT_SHEARING takes no \"target_item\"" ) );
    CHECK_NOTHROW( load_effect(
                       R"({ "npc_assign_activity": "ACT_SHEARING", "target_monster": "u" })" ) );
    // Items.
    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_WIELD", "target_item": "npc" })" ) );
    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_MOVE_ITEMS", "target_item": "npc", )"
                                R"("target_loc": { "context_val": "spot" } })" ) );
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_MOVE_ITEMS", )"
                                    R"("target_item": "npc" })" ),
                       Catch::Matchers::Contains( "needs \"target_loc\"" ) );
    // The activities that took a target before take no monster.
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_READ", "target_item": "npc", )"
                                    R"("target_monster": "npc" })" ),
                       Catch::Matchers::Contains( "takes no target but" ) );
    for( const char *const type : {
             "ACT_CONSUME", "ACT_WIELD", "ACT_DROP", "ACT_PICKUP", "ACT_INSERT_ITEM", "ACT_PRYING",
             "ACT_LOCKPICK", "ACT_CHOP_TREE", "ACT_FISH", "ACT_FIRSTAID", "ACT_BUILD", "ACT_MILK",
             "ACT_SHEARING", "ACT_TENT_PLACE", "ACT_TENT_DECONSTRUCT", "ACT_CLEAR_RUBBLE",
             "ACT_HACKSAW", "ACT_BOLTCUTTING", "ACT_OXYTORCH", "ACT_GUNMOD_REMOVE", "ACT_TOOLMOD_ADD"
         } ) {
        CAPTURE( type );
        CHECK( activity_actors::takes_eoc_target( activity_id( type ) ) );
    }
}

TEST_CASE( "act-j02_eoc_wields_drops_and_moves_items", "[audit][activity]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    setup();
    avatar &u = get_avatar();
    npc &guy = spawn_helper();
    map &here = get_map();

    SECTION( "an NPC wields a stick from the ground next to it" ) {
        item_location stick = on_ground( guy.pos_bub() + tripoint_rel_ms::east, item( itype_stick ) );
        dialogue d( get_talker_for( guy ), get_talker_for( stick ) );
        assign( R"("ACT_WIELD", "target_item": "npc")", d );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_WIELD );
        run_to_the_end( guy, ACT_WIELD );
        REQUIRE( guy.get_wielded_item() );
        CHECK( guy.get_wielded_item()->typeId() == itype_stick );
    }
    SECTION( "wielding with something in hand is refused, without a menu" ) {
        REQUIRE( wield_new( u, itype_rock ) );
        item_location stick = on_ground( u.pos_bub(), item( itype_stick ) );
        dialogue d( get_talker_for( u ), get_talker_for( stick ) );
        assign( R"("ACT_WIELD", "target_item": "npc")", d );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "put away" ) );
        CHECK( u.activity.is_null() );
    }
    SECTION( "an NPC drops what it carries" ) {
        guy.wear_item( item( itype_backpack ), false );
        item_location rock = guy.i_add( item( itype_rock ) );
        REQUIRE( rock );
        REQUIRE( rock.carrier() == &guy );
        dialogue d( get_talker_for( guy ), get_talker_for( rock ) );
        assign( R"("ACT_DROP", "target_item": "npc")", d );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_DROP );
        run_to_the_end( guy, ACT_DROP );
        CHECK( count_on_ground( guy.pos_bub(), itype_rock ) == 1 );
        CHECK_FALSE( guy.has_amount( itype_rock, 1 ) );
    }
    SECTION( "the avatar moves an item to the tile next to it" ) {
        item_location rock = on_ground( u.pos_bub(), item( itype_rock ) );
        const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms::north;
        dialogue d( get_talker_for( u ), get_talker_for( rock ) );
        d.set_value( "spot", here.get_abs( spot ) );
        assign( R"("ACT_MOVE_ITEMS", "target_item": "npc", "target_loc": { "context_val": "spot" })",
                d );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_MOVE_ITEMS );
        run_to_the_end( u, ACT_MOVE_ITEMS );
        CHECK( count_on_ground( spot, itype_rock ) == 1 );
        CHECK( count_on_ground( u.pos_bub(), itype_rock ) == 0 );
    }
    SECTION( "the avatar puts an item into the container it wields" ) {
        REQUIRE( wield_new( u, itype_test_audit_partial_two_pocket_bag ) );
        item_location rock = on_ground( u.pos_bub(), item( itype_rock ) );
        dialogue d( get_talker_for( u ), get_talker_for( rock ) );
        assign( R"("ACT_INSERT_ITEM", "target_item": "npc")", d );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_INSERT_ITEM );
        run_to_the_end( u, ACT_INSERT_ITEM );
        CHECK( count_of( u.get_wielded_item()->all_items_top(), itype_rock ) == 1 );
        CHECK( count_on_ground( u.pos_bub(), itype_rock ) == 0 );
    }
    SECTION( "only the avatar picks up through ACT_PICKUP" ) {
        guy.wear_item( item( itype_backpack ), false );
        item_location rock = on_ground( guy.pos_bub(), item( itype_rock ) );
        dialogue d( get_talker_for( guy ), get_talker_for( rock ) );
        assign( R"("ACT_PICKUP", "target_item": "npc")", d );
        CHECK( result() == "refused" );
        CHECK( guy.activity.is_null() );
        CHECK( count_on_ground( guy.pos_bub(), itype_rock ) == 1 );
    }
    SECTION( "the avatar picks up an item next to it" ) {
        u.wear_item( item( itype_backpack ), false );
        item_location rock = on_ground( u.pos_bub() + tripoint_rel_ms::west, item( itype_rock ) );
        dialogue d( get_talker_for( u ), get_talker_for( rock ) );
        assign( R"("ACT_PICKUP", "target_item": "npc")", d );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_PICKUP );
        run_to_the_end( u, ACT_PICKUP );
        CHECK( u.has_amount( itype_rock, 1 ) );
    }
    get_globals().clear_global_values();
}

TEST_CASE( "act-j02_eoc_works_on_a_place", "[audit][activity]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    setup();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms::east;

    SECTION( "the avatar cuts a chain fence with the bolt cutters it is given" ) {
        here.ter_set( spot, ter_t_chainfence );
        REQUIRE( wield_new( u, itype_boltcutters ) );
        item_location cutters = u.get_wielded_item();
        dialogue d( get_talker_for( u ), get_talker_for( cutters ) );
        d.set_value( "spot", here.get_abs( spot ) );
        assign( R"("ACT_BOLTCUTTING", "target_item": "npc", "target_loc": { "context_val": "spot" })",
                d );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_BOLTCUTTING );
        run_to_the_end( u, ACT_BOLTCUTTING );
        CHECK( here.ter( spot ) == ter_t_chainfence_posts );
    }
    SECTION( "without a tool given, the best one carried is used" ) {
        here.ter_set( spot, ter_t_chainfence );
        REQUIRE( wield_new( u, itype_boltcutters ) );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "spot", here.get_abs( spot ) );
        assign( R"("ACT_BOLTCUTTING", "target_loc": { "context_val": "spot" })", d );
        CHECK( result() == "started" );
        CHECK( u.activity.id() == ACT_BOLTCUTTING );
    }
    SECTION( "the avatar clears rubble with a shovel" ) {
        here.furn_set( spot, furn_f_rubble );
        REQUIRE( wield_new( u, itype_shovel ) );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "spot", here.get_abs( spot ) );
        assign( R"("ACT_CLEAR_RUBBLE", "target_loc": { "context_val": "spot" })", d );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_CLEAR_RUBBLE );
        run_to_the_end( u, ACT_CLEAR_RUBBLE );
        CHECK_FALSE( here.has_furn( spot ) );
    }
    SECTION( "clearing rubble without a shovel is refused" ) {
        here.furn_set( spot, furn_f_rubble );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "spot", here.get_abs( spot ) );
        assign( R"("ACT_CLEAR_RUBBLE", "target_loc": { "context_val": "spot" })", d );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "shovel" ) );
        CHECK( here.has_furn( spot ) );
    }
    SECTION( "prying where there is nothing to pry is refused, with no debug message" ) {
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "spot", here.get_abs( spot ) );
        const std::string msg = capture_debugmsg_during( [&]() {
            assign( R"("ACT_PRYING", "target_loc": { "context_val": "spot" })", d );
        } );
        CHECK( msg.empty() );
        CHECK( result() == "refused" );
        CHECK( u.activity.id() != ACT_PRYING );
    }
    SECTION( "a place out of reach is refused" ) {
        here.ter_set( spot + tripoint_rel_ms::east, ter_t_chainfence );
        REQUIRE( wield_new( u, itype_boltcutters ) );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "spot", here.get_abs( spot + tripoint_rel_ms::east ) );
        assign( R"("ACT_BOLTCUTTING", "target_loc": { "context_val": "spot" })", d );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "out of reach" ) );
    }
    get_globals().clear_global_values();
}

TEST_CASE( "act-j02_eoc_works_on_a_monster", "[audit][activity]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    setup();
    avatar &u = get_avatar();
    monster &cow = spawn_test_monster( "mon_cow", u.pos_bub() + tripoint_rel_ms::east );
    cow.add_effect( effect_pet, 1_turns, true );
    cow.ammo[itype_milk_raw] = 4;

    SECTION( "the avatar plays with a pet" ) {
        dialogue d( get_talker_for( u ), get_talker_for( cow ) );
        assign( R"("ACT_PLAY_WITH_PET", "target_monster": "npc")", d );
        CHECK( result() == "started" );
        CHECK( u.activity.id() == ACT_PLAY_WITH_PET );
    }
    SECTION( "the avatar milks a pet into the jug it wields" ) {
        REQUIRE( wield_new( u, itype_jug_plastic ) );
        dialogue d( get_talker_for( u ), get_talker_for( cow ) );
        assign( R"("ACT_MILK", "target_monster": "npc")", d );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_MILK );
        run_to_the_end( u, ACT_MILK );
        // Not how much: the milking activity's own count of what it hands over is loose.
        CHECK( count_of( u.get_wielded_item()->all_items_top(), itype_milk_raw ) > 0 );
        CHECK( cow.ammo[itype_milk_raw] == 0 );
    }
    SECTION( "milking with no container is refused" ) {
        dialogue d( get_talker_for( u ), get_talker_for( cow ) );
        assign( R"("ACT_MILK", "target_monster": "npc")", d );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "container" ) );
        CHECK( cow.ammo[itype_milk_raw] == 4 );
    }
    SECTION( "a monster that isn't a pet is refused" ) {
        cow.remove_effect( effect_pet );
        dialogue d( get_talker_for( u ), get_talker_for( cow ) );
        assign( R"("ACT_PLAY_WITH_PET", "target_monster": "npc")", d );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "not a pet" ) );
        CHECK( u.activity.is_null() );
    }
    SECTION( "the other talker has to be a monster" ) {
        item_location rock = on_ground( u.pos_bub(), item( itype_rock ) );
        dialogue d( get_talker_for( u ), get_talker_for( rock ) );
        assign( R"("ACT_PLAY_WITH_PET", "target_monster": "npc")", d );
        CHECK( result() == "refused" );
        CHECK( u.activity.is_null() );
    }
    get_globals().clear_global_values();
    clear_creatures();
}

static constexpr tripoint_bub_ms repair_pos( 60, 60, 0 );

// A one-tile vehicle at repair_pos whose frame needs a hammer to repair, damaged by several
// levels, and the avatar next to it (west), skilled enough to repair it.
static vehicle_part &damaged_hammer_frame( vehicle *&veh_out )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    map &here = get_map();
    avatar &u = get_avatar();
    u.setpos( here, repair_pos + tripoint_rel_ms::west );
    u.set_skill_level( skill_mechanics, 2 );
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, repair_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    const int idx = veh->install_part( here, point_rel_ms::zero,
                                       vpart_test_audit_partial_hammer_frame );
    REQUIRE( idx != -1 );
    veh->refresh();
    here.add_vehicle_to_cache( veh );
    vehicle_part &frame = veh->part( idx );
    veh->set_hp( frame, frame.info().durability / 4, false, 0 );
    REQUIRE( frame.is_repairable() );
    REQUIRE( frame.get_base().repairable_levels() >= 2 );
    veh_out = veh;
    return frame;
}

// ACT-B02: the zone repair checked the tools and components within PICKUP_RANGE - 1 of the part,
// through walls, while repair_part() repairs with what the avatar reaches along a clear path
// within PICKUP_RANGE of where it stands. A job the repair can't do was taken (and failed over
// and over), and one it can do was sent to fetch what was already in reach.
TEST_CASE( "act-b02_zone_repair_checks_what_the_repair_can_reach", "[audit][activity]" )
{
    vehicle *veh = nullptr;
    vehicle_part &frame = damaged_hammer_frame( veh );
    avatar &u = get_avatar();
    map &here = get_map();
    multi_vehicle_repair_activity_actor zone_repair;

    SECTION( "a hammer near the part but walled off is not taken for one in reach" ) {
        // three tiles east of the part, in a closed ring of walls
        const tripoint_bub_ms hammer_pos = repair_pos + tripoint_rel_ms( 3, 0, 0 );
        for( const tripoint_bub_ms &p : here.points_in_radius( hammer_pos, 1 ) ) {
            if( p != hammer_pos ) {
                here.ter_set( p, ter_t_wall );
            }
        }
        here.add_item( hammer_pos, item( itype_hammer ) );
        u.invalidate_crafting_inventory();

        const activity_reason_info info = zone_repair.multi_activity_can_do( u, repair_pos );
        CHECK_FALSE( info.can_do );
        // fetching can't help: it counts the hammer as there already
        CHECK( info.reason != do_activity_reason::NEEDS_VEH_REPAIR );
        CHECK( u.activity_vehicle_part_index == -1 );
        // which is what the repair itself says
        CHECK_FALSE( veh_utils::repair_part( here, *veh, frame, u ) );
    }
    SECTION( "a hammer in the repairer's reach, but not the part's, is used" ) {
        // six tiles west of the avatar, seven from the part
        const tripoint_bub_ms hammer_pos = u.pos_bub() + tripoint_rel_ms( -6, 0, 0 );
        here.add_item( hammer_pos, item( itype_hammer ) );
        u.invalidate_crafting_inventory();

        const activity_reason_info info = zone_repair.multi_activity_can_do( u, repair_pos );
        CHECK( info.can_do );
        CHECK( info.reason == do_activity_reason::NEEDS_VEH_REPAIR );
        CHECK( veh_utils::repair_part( here, *veh, frame, u ) );
    }
    SECTION( "an NPC uses what lies behind the wall, as its repair does" ) {
        npc &guy = spawn_npc( ( repair_pos + tripoint_rel_ms::north ).xy(), "test_talker" );
        const tripoint_abs_ms pos = guy.pos_abs();
        clear_character( guy );
        guy.setpos( pos );
        guy.set_skill_level( skill_mechanics, 2 );
        const tripoint_bub_ms hammer_pos = repair_pos + tripoint_rel_ms( 3, 0, 0 );
        for( const tripoint_bub_ms &p : here.points_in_radius( hammer_pos, 1 ) ) {
            if( p != hammer_pos ) {
                here.ter_set( p, ter_t_wall );
            }
        }
        here.add_item( hammer_pos, item( itype_hammer ) );
        guy.invalidate_crafting_inventory();

        const activity_reason_info info = zone_repair.multi_activity_can_do( guy, repair_pos );
        CHECK( info.can_do );
        CHECK( veh_utils::repair_part( here, *veh, frame, guy ) );
        guy.activity_vehicle_part_index = -1;
    }
    // Don't leak the part index into later tests: NPC zone workers skip the player's index.
    u.activity_vehicle_part_index = -1;
    clear_npcs();
    clear_vehicles();
}

// UIINV-B02: a drag and drop in the inventory put the item in the pocket at once, with Character::
// store. It now starts the insert activity that AIM uses, which takes the move's time and puts the
// item, the whole stack, in the pocket chosen.
TEST_CASE( "uiinv-b02_drag_and_drop_moves_through_the_insert_activity", "[audit][inventory]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    setup();
    avatar &u = get_avatar();
    REQUIRE( wield_new( u, itype_test_audit_partial_two_pocket_bag ) );
    item_location bag = u.get_wielded_item();
    REQUIRE( bag->get_container_pockets().size() == 2 );
    // A rock in the first pocket: an insert into the best pocket would put a second rock there.
    REQUIRE( bag->get_container_pockets()[0]->insert_item( item( itype_rock ) ).success() );
    const auto rocks_in = [&bag]( const size_t pocket ) {
        return count_of( bag->get_container_pockets()[pocket]->all_items_top(), itype_rock );
    };

    SECTION( "an item from the ground goes into the pocket chosen, once the activity is done" ) {
        item_location rock = on_ground( u.pos_bub(), item( itype_rock ) );
        const ret_val<void> started = drag_drop_start_move( u, rock, bag, 1 );
        REQUIRE( started.success() );
        REQUIRE( u.activity.id() == ACT_INSERT_ITEM );
        CHECK( u.activity.moves_left > 0 );
        // nothing has moved yet
        CHECK( rocks_in( 1 ) == 0 );
        CHECK( count_on_ground( u.pos_bub(), itype_rock ) == 1 );
        run_to_the_end( u, ACT_INSERT_ITEM );
        CHECK( rocks_in( 0 ) == 1 );
        CHECK( rocks_in( 1 ) == 1 );
        CHECK( count_on_ground( u.pos_bub(), itype_rock ) == 0 );
    }
    SECTION( "an item moves between the pockets of the container holding it" ) {
        item *in_first = bag->get_container_pockets()[0]->all_items_top().front();
        const item_location rock( bag, in_first );
        REQUIRE( drag_drop_start_move( u, rock, bag, 1 ).success() );
        REQUIRE( u.activity.id() == ACT_INSERT_ITEM );
        run_to_the_end( u, ACT_INSERT_ITEM );
        CHECK( rocks_in( 0 ) == 0 );
        CHECK( rocks_in( 1 ) == 1 );
    }
    SECTION( "a pocket that can't hold the item starts nothing" ) {
        item_location big = on_ground( u.pos_bub(), item( itype_backpack ) );
        const ret_val<void> started = drag_drop_start_move( u, big, bag, 1 );
        CHECK_FALSE( started.success() );
        CHECK( u.activity.is_null() );
        CHECK( rocks_in( 1 ) == 0 );
    }
}

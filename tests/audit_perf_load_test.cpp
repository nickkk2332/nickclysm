#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "achievement.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "colony.h"
#include "coordinates.h"
#include "debug.h"
#include "event.h"
#include "event_bus.h"
#include "event_statistics.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "item_uid.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "mapbuffer.h"
#include "options_helpers.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "player_difficulty.h"
#include "pocket_type.h"
#include "point.h"
#include "power_network.h"
#include "ret_val.h"
#include "scenario.h"
#include "stats_tracker.h"
#include "string_formatter.h"
#include "submap.h"
#include "type_id.h"
#include "worldfactory.h"
#include "zzip.h"

static const achievement_id
achievement_achievement_reach_exodii_base( "achievement_reach_exodii_base" );
static const achievement_id
achievement_achievement_reach_refugee_center( "achievement_reach_refugee_center" );

static const event_statistic_id
event_statistic_last_overmap_special_avatar_entered( "last_overmap_special_avatar_entered" );
static const event_statistic_id
event_statistic_last_oter_type_avatar_entered( "last_oter_type_avatar_entered" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_jeans( "jeans" );
static const itype_id itype_lighter( "lighter" );
static const itype_id itype_scrap( "scrap" );
static const itype_id itype_test_backpack( "test_backpack" );
static const itype_id itype_test_duffelbag( "test_duffelbag" );
static const itype_id itype_test_mini_backpack( "test_mini_backpack" );
static const itype_id itype_test_multimag_aux_gun( "test_multimag_aux_gun" );
static const itype_id itype_test_multimag_gun_consume( "test_multimag_gun_consume" );
static const itype_id
itype_test_watertight_open_sealed_container_250ml( "test_watertight_open_sealed_container_250ml" );
static const itype_id itype_test_watertight_open_sealed_multipocket_container_2x250ml(
    "test_watertight_open_sealed_multipocket_container_2x250ml" );
static const itype_id itype_tshirt( "tshirt" );

static const oter_type_str_id oter_type_field( "field" );

static const overmap_special_id overmap_special_evac_center( "evac_center" );

static const ter_str_id ter_t_dirt( "t_dirt" );

namespace
{

std::string to_json( const item &it )
{
    std::ostringstream os;
    JsonOut jsout( os );
    it.serialize( jsout );
    return os.str();
}

item from_json( const std::string &json )
{
    item loaded;
    JsonValue jv = json_loader::from_string( json );
    loaded.deserialize( jv.get_object() );
    return loaded;
}

std::string to_json( const item_location &loc )
{
    std::ostringstream os;
    JsonOut jsout( os );
    loc.serialize( jsout );
    return os.str();
}

item_location location_from_json( const std::string &json )
{
    item_location loc;
    JsonValue jv = json_loader::from_string( json );
    JsonObject jo = jv;
    loc.deserialize( jo );
    return loc;
}

std::vector<itype_id> types_in( const item &it, pocket_type pk )
{
    std::vector<itype_id> result;
    for( const item *contained : it.all_items_top( pk ) ) {
        result.push_back( contained->typeId() );
    }
    return result;
}

const item *find_contained( const item &container, const itype_id &type )
{
    for( const item *it : container.all_items_ptr() ) {
        if( it->typeId() == type ) {
            return it;
        }
    }
    return nullptr;
}

// The uids of an item and everything in it
std::vector<int64_t> uids_in( const item &it )
{
    std::vector<int64_t> result = { it.uid().get_value() };
    for( const item *contained : it.all_items_ptr() ) {
        result.push_back( contained->uid().get_value() );
    }
    return result;
}

// Saved items with every uid ("uid" and "rle_uids") replaced by the next from next_uid
std::string renumber_uids( const std::string &json, int64_t &next_uid )
{
    static const std::regex uids( R"re("uid":\d+|"rle_uids":\[[0-9,]*\])re" );
    std::string result;
    std::string::const_iterator done = json.begin();
    for( std::sregex_iterator m( json.begin(), json.end(), uids ), end; m != end; ++m ) {
        result.append( done, ( *m )[0].first );
        const std::string found = ( *m )[0].str();
        if( found.rfind( "\"uid\"", 0 ) == 0 ) {
            result += "\"uid\":" + std::to_string( next_uid++ );
        } else {
            const size_t count = found == "\"rle_uids\":[]" ? 0 :
                                 std::count( found.begin(), found.end(), ',' ) + 1;
            result += "\"rle_uids\":[";
            for( size_t i = 0; i < count; ++i ) {
                result += ( i == 0 ? "" : "," ) + std::to_string( next_uid++ );
            }
            result += "]";
        }
        done = ( *m )[0].second;
    }
    result.append( done, json.end() );
    return result;
}

void send_game_start( event_bus &b )
{
    b.send<event_type::game_start>( "VERSION_STRING" );
    b.send<event_type::game_avatar_new>( /*is_new_game=*/true, /*is_debug=*/false,
            get_player_character().getID(), "Avatar name", "CUSTOM_PROFESSION" );
}

// OMTs in an overmap that is neither loaded nor saved
tripoint_abs_omt far_omt( int i )
{
    return tripoint_abs_omt( project_to<coords::omt>( point_abs_om( 70, 70 ) ), 0 ) +
           tripoint_rel_omt( 10 * i, 3, 0 );
}

} // namespace

// EVT-P01: last_overmap_special_avatar_entered used to find the special of every OMT the avatar
// had entered by looking it up in the overmap, loading (or generating) every overmap the
// avatar ever visited each time the statistic was rebuilt, as on every game load. Events saved
// before avatar_enters_omt recorded the special still load, and report no special.
TEST_CASE( "evt-p01_old_saved_omt_history_loads_without_loading_overmaps", "[audit][stats]" )
{
    const oter_id field( "field" );
    stats_tracker saved;
    time_point when = calendar::turn_zero;
    for( int i = 0; i < 5; ++i ) {
        when += 1_minutes;
        // An older build's event: no overmap_special field
        saved.notify( cata::event( event_type::avatar_enters_omt, when, cata::event::data_type{
            { "pos", cata_variant( far_omt( i ).raw() ) },
            { "oter_id", cata_variant( field ) }
        } ) );
    }
    std::ostringstream os;
    JsonOut jsout( os );
    saved.serialize( jsout );

    stats_tracker loaded;
    JsonValue jv = json_loader::from_string( os.str() );
    loaded.deserialize( jv.get_object() );

    const int overmaps_before = overmap_buffer.global_state.overmap_count;
    on_out_of_scope restore_overmaps( [&]() {
        if( overmap_buffer.global_state.overmap_count != overmaps_before ) {
            // only before the fix: put the harness's overmap buffer back
            overmap_buffer.clear();
            overmap_special_batch empty_specials( point_abs_om{} );
            overmap_buffer.create_custom_overmap( point_abs_om{}, empty_specials );
        }
    } );
    CHECK( loaded.get_events( event_type::avatar_enters_omt ).count() == 5 );
    CHECK( loaded.value_of( event_statistic_last_oter_type_avatar_entered ) ==
           cata_variant( oter_type_field ) );
    CHECK( loaded.value_of( event_statistic_last_overmap_special_avatar_entered ) == cata_variant() );
    CHECK( overmap_buffer.global_state.overmap_count == overmaps_before );
}


// EVT-P01: avatar_enters_omt now carries the special of the OMT, recorded when it is sent, so
// last_overmap_special_avatar_entered and the 16 achievements on it never need the overmap.
TEST_CASE( "evt-p01_entering_a_special_completes_its_achievement_without_its_overmap",
           "[audit][stats][achievement]" )
{
    const time_point old_turn = calendar::turn;
    on_out_of_scope restore_turn( [&]() {
        calendar::turn = old_turn;
    } );
    std::set<achievement_id> completed;
    event_bus b;
    stats_tracker s;
    b.subscribe( &s );
    achievements_tracker a( s, [&]( const achievement * ach, bool ) {
        completed.insert( ach->id );
    }, []( const achievement *, bool ) {}, true );
    b.subscribe( &a );
    send_game_start( b );
    const int overmaps_before = overmap_buffer.global_state.overmap_count;
    const oter_id field( "field" );

    b.send<event_type::avatar_enters_omt>( far_omt( 0 ).raw(), field, overmap_special_evac_center );
    CHECK( s.value_of( event_statistic_last_overmap_special_avatar_entered ) ==
           cata_variant( overmap_special_evac_center ) );
    CHECK( completed.count( achievement_achievement_reach_refugee_center ) == 1 );

    // an OMT in no special leaves the last special as it was
    calendar::turn += 1_minutes;
    b.send<event_type::avatar_enters_omt>( far_omt( 1 ).raw(), field, overmap_special_id() );
    CHECK( s.value_of( event_statistic_last_overmap_special_avatar_entered ) ==
           cata_variant( overmap_special_evac_center ) );
    CHECK( overmap_buffer.global_state.overmap_count == overmaps_before );
}

// EVT-P01: loading a save keeps the achievements it had, and a history from an older build
// (no special recorded) completes none: it counts as entering no special.
TEST_CASE( "evt-p01_loading_a_save_keeps_achievements_and_grants_none",
           "[audit][stats][achievement]" )
{
    const time_point old_turn = calendar::turn;
    on_out_of_scope restore_turn( [&]() {
        calendar::turn = old_turn;
    } );
    const oter_id field( "field" );
    std::string saved_stats;
    std::string saved_achievements;
    {
        event_bus b;
        stats_tracker s;
        b.subscribe( &s );
        achievements_tracker a( s, []( const achievement *, bool ) {},
        []( const achievement *, bool ) {}, true );
        b.subscribe( &a );
        send_game_start( b );
        // entered with an older build, which recorded no special
        for( int i = 1; i <= 3; ++i ) {
            calendar::turn += 1_minutes;
            b.send( cata::event( event_type::avatar_enters_omt, calendar::turn, cata::event::data_type{
                { "pos", cata_variant( far_omt( i ).raw() ) },
                { "oter_id", cata_variant( field ) }
            } ) );
        }
        calendar::turn += 1_minutes;
        b.send<event_type::avatar_enters_omt>( far_omt( 0 ).raw(), field, overmap_special_evac_center );
        REQUIRE( a.is_completed( achievement_achievement_reach_refugee_center ) ==
                 achievement_completion::completed );
        std::ostringstream stats_os;
        JsonOut stats_out( stats_os );
        s.serialize( stats_out );
        saved_stats = stats_os.str();
        std::ostringstream achievements_os;
        JsonOut achievements_out( achievements_os );
        a.serialize( achievements_out );
        saved_achievements = achievements_os.str();
    }

    const int overmaps_before = overmap_buffer.global_state.overmap_count;
    std::set<achievement_id> completed;
    stats_tracker s;
    achievements_tracker a( s, [&]( const achievement * ach, bool ) {
        completed.insert( ach->id );
    }, []( const achievement *, bool ) {}, true );
    JsonValue stats_json = json_loader::from_string( saved_stats );
    s.deserialize( stats_json.get_object() );
    JsonValue achievements_json = json_loader::from_string( saved_achievements );
    a.deserialize( achievements_json.get_object() );

    CHECK( completed.empty() );
    CHECK( a.is_completed( achievement_achievement_reach_refugee_center ) ==
           achievement_completion::completed );
    CHECK( a.is_completed( achievement_achievement_reach_exodii_base ) ==
           achievement_completion::pending );
    CHECK( s.get_events( event_type::avatar_enters_omt ).count() == 4 );
    CHECK( s.value_of( event_statistic_last_overmap_special_avatar_entered ) ==
           cata_variant( overmap_special_evac_center ) );
    CHECK( overmap_buffer.global_state.overmap_count == overmaps_before );
}

// INV-P01: item::deserialize copied each container's contents into place, so every contained
// item got a fresh uid on every load (and each was deep-copied once per nesting level).
TEST_CASE( "inv-p01_contained_items_keep_their_uids_through_save_and_load",
           "[audit][item][item_uid]" )
{
    item outer( itype_test_backpack );
    item inner( itype_test_mini_backpack );
    REQUIRE( inner.put_in( item( itype_tshirt ), pocket_type::CONTAINER ).success() );
    REQUIRE( outer.put_in( inner, pocket_type::CONTAINER ).success() );
    REQUIRE( outer.put_in( item( itype_jeans ), pocket_type::CONTAINER ).success() );

    const std::list<const item *> before = std::as_const( outer ).all_items_ptr();
    REQUIRE( before.size() == 3 );
    const item loaded = from_json( to_json( outer ) );
    const std::list<const item *> after = loaded.all_items_ptr();
    REQUIRE( after.size() == before.size() );

    CHECK( loaded.uid() == outer.uid() );
    auto b = before.begin();
    for( auto a = after.begin(); a != after.end(); ++a, ++b ) {
        CAPTURE( ( *a )->typeId().str() );
        CHECK( ( *a )->typeId() == ( *b )->typeId() );
        CHECK( ( *a )->uid() == ( *b )->uid() );
    }
}

// INV-P01: an item_location into a container inside a container is saved as a chain of
// parents ending with the contained item's uid. With fresh uids after loading, the uid was
// not found ("item_location UID not found in container contents") and the location was lost.
TEST_CASE( "inv-p01_item_location_into_a_nested_container_survives_save_and_load",
           "[audit][item][item_location]" )
{
    clear_map_without_vision();
    map &here = get_map();
    const tripoint_bub_ms pos( 60, 60, 0 );
    here.i_clear( pos );
    on_out_of_scope cleanup( [&]() {
        here.i_clear( pos );
    } );

    item outer( itype_test_backpack );
    item inner( itype_test_mini_backpack );
    REQUIRE( inner.put_in( item( itype_jeans ), pocket_type::CONTAINER ).success() );
    REQUIRE( inner.put_in( item( itype_tshirt ), pocket_type::CONTAINER ).success() );
    REQUIRE( outer.put_in( inner, pocket_type::CONTAINER ).success() );
    item &placed = here.add_item( pos, outer );

    item *inner_ptr = placed.all_items_top( pocket_type::CONTAINER ).front();
    REQUIRE( inner_ptr->typeId() == itype_test_mini_backpack );
    item *tshirt_ptr = nullptr;
    for( item *it : inner_ptr->all_items_top( pocket_type::CONTAINER ) ) {
        if( it->typeId() == itype_tshirt ) {
            tshirt_ptr = it;
        }
    }
    REQUIRE( tshirt_ptr != nullptr );
    // (copying an item_uid makes a new one)
    const int64_t tshirt_uid = tshirt_ptr->uid().get_value();
    const item_location outer_loc( map_cursor( pos ), &placed );
    const item_location inner_loc( outer_loc, inner_ptr );
    const item_location tshirt_loc( inner_loc, tshirt_ptr );
    const std::string saved_location = to_json( tshirt_loc );

    // Save and load the tile's items as a map save and load do: written as the submap writes
    // them, then read straight back into the map
    tripoint_bub_ms where = pos;
    point_sm_ms l;
    cata::colony<item> &tile_items = map_meddler::unsafe_get_submap_at( where, l )->get_items( l );
    REQUIRE( tile_items.size() == 1 );
    std::ostringstream os;
    JsonOut jsout( os );
    jsout.write( tile_items );
    tile_items.clear();
    JsonValue jv = json_loader::from_string( os.str() );
    REQUIRE( jv.read( tile_items ) );
    REQUIRE( tile_items.size() == 1 );

    const item_location restored = location_from_json( saved_location );
    REQUIRE( restored );
    CHECK( restored->typeId() == itype_tshirt );
    CHECK( restored->uid().get_value() == tshirt_uid );
    CHECK( restored.parent_item()->typeId() == itype_test_mini_backpack );
}

// INV-P01 fallbacks, kept as they were: an item that no longer fits its saved pocket (here
// one put there by force, as after a JSON change shrank the pocket) moves to the migration
// pocket, to spill later.
TEST_CASE( "inv-p01_item_too_big_for_its_saved_pocket_moves_to_migration",
           "[audit][item][pocket]" )
{
    item pack( itype_test_mini_backpack );
    REQUIRE( pack.put_in( item( itype_scrap ), pocket_type::CONTAINER ).success() );
    REQUIRE( pack.put_in( item( itype_lighter ), pocket_type::CONTAINER ).success() );
    item too_big( itype_test_duffelbag );
    REQUIRE_FALSE( pack.can_contain( too_big ).success() );
    pack.force_insert_item( too_big, pocket_type::CONTAINER );
    // put_in adds at the front
    REQUIRE( types_in( pack, pocket_type::CONTAINER ) ==
             std::vector<itype_id> { itype_lighter, itype_scrap, itype_test_duffelbag } );

    const item loaded = from_json( to_json( pack ) );
    CHECK( types_in( loaded, pocket_type::CONTAINER ) ==
           std::vector<itype_id> { itype_lighter, itype_scrap } );
    CHECK( types_in( loaded, pocket_type::MIGRATION ) ==
           std::vector<itype_id> { itype_test_duffelbag } );
    const item *moved = find_contained( loaded, itype_test_duffelbag );
    REQUIRE( moved != nullptr );
    CHECK( moved->uid() == find_contained( pack, itype_test_duffelbag )->uid() );
}

// A saved pocket whose type no longer matches the definition is set aside without
// advancing to the next pocket of the definition: the following saved pocket is matched
// against the same one. The set-aside items go into a pocket of their saved type if the item
// has one; here it has none, so they move to the migration pocket, with an error.
TEST_CASE( "inv-p01_saved_pocket_of_another_type_falls_back_as_before",
           "[audit][item][pocket]" )
{
    item can( itype_test_watertight_open_sealed_multipocket_container_2x250ml );
    std::vector<item_pocket *> pockets = can.get_container_pockets();
    REQUIRE( pockets.size() == 2 );
    REQUIRE( pockets[0]->insert_item( item( itype_scrap ) ).success() );
    REQUIRE( pockets[1]->insert_item( item( itype_lighter ) ).success() );
    std::string json = to_json( can );
    const std::string container_type = string_format( "\"pocket_type\":%d",
                                       static_cast<int>( pocket_type::CONTAINER ) );
    const size_t first_pocket = json.find( container_type );
    REQUIRE( first_pocket != std::string::npos );
    json.replace( first_pocket, container_type.size(),
                  string_format( "\"pocket_type\":%d", static_cast<int>( pocket_type::MAGAZINE ) ) );

    item loaded;
    const std::string error = capture_debugmsg_during( [&]() {
        loaded = from_json( json );
    } );
    CHECK_THAT( error, Catch::Matchers::Contains( "cannot fit into any pocket while loading" ) );
    const std::vector<item_pocket *> loaded_pockets = loaded.get_container_pockets();
    REQUIRE( loaded_pockets.size() == 2 );
    CHECK( loaded_pockets[0]->all_items_top().size() == 1 );
    CHECK( loaded_pockets[0]->all_items_top().front()->typeId() == itype_lighter );
    CHECK( loaded_pockets[1]->empty() );
    CHECK( types_in( loaded, pocket_type::MIGRATION ) == std::vector<itype_id> { itype_scrap } );
    CHECK( find_contained( loaded, itype_scrap )->uid() == find_contained( can, itype_scrap )->uid() );
}

// Saved pockets beyond the ones the definition now has: here the item lost its second pocket.
// That pocket's contents are set aside and go into a pocket of their type, ahead of what is
// there.
TEST_CASE( "inv-p01_saved_pockets_beyond_the_definition_are_reinserted",
           "[audit][item][pocket]" )
{
    item can( itype_test_watertight_open_sealed_multipocket_container_2x250ml );
    std::vector<item_pocket *> pockets = can.get_container_pockets();
    REQUIRE( pockets.size() == 2 );
    REQUIRE( pockets[0]->insert_item( item( itype_scrap ) ).success() );
    REQUIRE( pockets[1]->insert_item( item( itype_lighter ) ).success() );
    std::string json = to_json( can );
    const std::string two_pockets = string_format( "\"%s\"",
                                    itype_test_watertight_open_sealed_multipocket_container_2x250ml.str() );
    const size_t type_at = json.find( two_pockets );
    REQUIRE( type_at != std::string::npos );
    json.replace( type_at, two_pockets.size(), string_format( "\"%s\"",
                  itype_test_watertight_open_sealed_container_250ml.str() ) );

    const item loaded = from_json( json );
    REQUIRE( loaded.typeId() == itype_test_watertight_open_sealed_container_250ml );
    CHECK( types_in( loaded, pocket_type::CONTAINER ) ==
           std::vector<itype_id> { itype_lighter, itype_scrap } );
    CHECK( types_in( loaded, pocket_type::MIGRATION ).empty() );
    CHECK( find_contained( loaded, itype_lighter )->uid() == find_contained( can,
            itype_lighter )->uid() );
}

// INV-P01 follow-up: gun and tool mods were still copied by read_mods, so they and everything
// in them got new uids on every load, and a location into a mod was saved as nowhere. A location
// into the magazine of an underbarrel gun now survives a save and load.
TEST_CASE( "inv-p01_item_location_into_an_underbarrel_magazine_survives_save_and_load",
           "[audit][item][item_location]" )
{
    clear_map_without_vision();
    map &here = get_map();
    const tripoint_bub_ms pos( 60, 60, 0 );
    here.i_clear( pos );
    on_out_of_scope cleanup( [&]() {
        here.i_clear( pos );
    } );

    item mag( itype_glockmag );
    REQUIRE( mag.put_in( item( itype_9mm, calendar::turn, 5 ), pocket_type::MAGAZINE ).success() );
    item underbarrel( itype_test_multimag_aux_gun );
    REQUIRE( underbarrel.put_in( mag, pocket_type::MAGAZINE_WELL ).success() );
    item gun( itype_test_multimag_gun_consume );
    gun.force_insert_item( underbarrel, pocket_type::MOD );
    item &placed = here.add_item( pos, gun );

    item *mod = placed.all_items_top( pocket_type::MOD ).front();
    REQUIRE( mod->typeId() == itype_test_multimag_aux_gun );
    item *mag_ptr = mod->all_items_top( pocket_type::MAGAZINE_WELL ).front();
    REQUIRE( mag_ptr->typeId() == itype_glockmag );
    const int64_t mod_uid = mod->uid().get_value();
    const int64_t mag_uid = mag_ptr->uid().get_value();
    const item_location gun_loc( map_cursor( pos ), &placed );
    const item_location mod_loc( gun_loc, mod );
    const item_location mag_loc( mod_loc, mag_ptr );
    const std::string saved_location = to_json( mag_loc );

    // Save and load the tile's items as a map save and load do
    tripoint_bub_ms where = pos;
    point_sm_ms l;
    cata::colony<item> &tile_items = map_meddler::unsafe_get_submap_at( where, l )->get_items( l );
    REQUIRE( tile_items.size() == 1 );
    std::ostringstream os;
    JsonOut jsout( os );
    jsout.write( tile_items );
    tile_items.clear();
    JsonValue jv = json_loader::from_string( os.str() );
    REQUIRE( jv.read( tile_items ) );
    REQUIRE( tile_items.size() == 1 );

    const item_location restored = location_from_json( saved_location );
    REQUIRE( restored );
    CHECK( restored->typeId() == itype_glockmag );
    CHECK( restored->uid().get_value() == mag_uid );
    CHECK( restored->ammo_remaining() == 5 );
    REQUIRE( restored.parent_item() );
    CHECK( restored.parent_item()->typeId() == itype_test_multimag_aux_gun );
    CHECK( restored.parent_item()->uid().get_value() == mod_uid );
}

// INV-P01 follow-up, uid collisions: now that contained items keep their uids, a character made
// from a template (which restores every item's uid) shared all of them with every other
// character made from it, and with the game it was made from: in the same world, a new
// character picking up its predecessor's backpack would have had two items with each uid.
TEST_CASE( "inv-p01_characters_from_a_template_get_new_item_uids", "[audit][item][item_uid]" )
{
    const scenario *const old_scenario = get_scenario();
    const std::string name = "audit inv-p01 uids";
    on_out_of_scope cleanup( [&]() {
        set_scenario( old_scenario );
        remove_file( PATH_INFO::templatedir() + name + ".template" );
    } );
    REQUIRE( assure_dir_exist( PATH_INFO::templatedir() ) );

    avatar original;
    item pack( itype_test_backpack );
    REQUIRE( pack.put_in( item( itype_tshirt ), pocket_type::CONTAINER ).success() );
    original.wear_item( pack, false );
    original.set_wielded_item( item( itype_test_mini_backpack ) );
    std::set<int64_t> in_use;
    for( item_location &worn : original.top_items_loc() ) {
        for( int64_t uid : uids_in( *worn ) ) {
            in_use.insert( uid );
        }
    }
    in_use.insert( original.get_wielded_item()->uid().get_value() );
    REQUIRE( in_use.size() == 3 );
    original.save_template( name, pool_type::FREEFORM );

    // two characters from the template: all their items' uids are new
    for( int i = 0; i < 2; ++i ) {
        avatar from_template;
        pool_type pool = pool_type::TRANSFER;
        REQUIRE( from_template.load_template( name, pool ) );
        const std::vector<item_location> worn = from_template.top_items_loc();
        REQUIRE( worn.size() == 1 );
        REQUIRE( worn[0]->typeId() == itype_test_backpack );
        REQUIRE( find_contained( *worn[0], itype_tshirt ) != nullptr );
        REQUIRE( from_template.get_wielded_item() );
        std::vector<int64_t> loaded = uids_in( *worn[0] );
        loaded.push_back( from_template.get_wielded_item()->uid().get_value() );
        for( int64_t uid : loaded ) {
            CAPTURE( i, uid );
            CHECK( uid > 0 );
            CHECK( in_use.insert( uid ).second );
        }
    }
}

// INV-P01 follow-up, uid collisions: dimension travel saves the maps but not the master save
// that holds the uid counter, so after a crash the counter could be behind uids already saved
// in the maps. Reading a uid now keeps new uids above it, and the dimension data saved on travel
// carries the counter too.
TEST_CASE( "inv-p01_uids_read_from_a_save_keep_new_uids_above_them", "[audit][item][item_uid]" )
{
    // uids the counter has not reached, as in maps saved after the master save
    int64_t next_uid = generate_next_item_uid() + 100000;

    item pack( itype_test_backpack );
    REQUIRE( pack.put_in( item( itype_tshirt ), pocket_type::CONTAINER ).success() );
    const item loaded = from_json( renumber_uids( to_json( pack ), next_uid ) );
    const std::vector<int64_t> loaded_uids = uids_in( loaded );
    REQUIRE( loaded_uids.size() == 2 );
    CHECK( *std::max_element( loaded_uids.begin(), loaded_uids.end() ) == next_uid - 1 );
    CHECK( item( itype_scrap ).uid().get_value() > next_uid - 1 );

    // a run of identical items, saved as one item and the uids of the others
    next_uid += 100000;
    cata::colony<item> run;
    for( int i = 0; i < 3; ++i ) {
        run.insert( item( itype_scrap ) );
    }
    std::ostringstream os;
    JsonOut jsout( os );
    jsout.write( run );
    const std::string saved_run = renumber_uids( os.str(), next_uid );
    REQUIRE( saved_run.find( "rle_uids" ) != std::string::npos );
    cata::colony<item> loaded_run;
    JsonValue jv = json_loader::from_string( saved_run );
    REQUIRE( jv.read( loaded_run ) );
    REQUIRE( loaded_run.size() == 3 );
    for( const item &it : loaded_run ) {
        CHECK( it.uid().get_value() >= next_uid - 3 );
    }
    CHECK( item( itype_scrap ).uid().get_value() > next_uid - 1 );

    // the counter saved with the dimension data
    const int64_t saved_counter = next_uid + 100000;
    std::ostringstream dimension_os;
    JsonOut dimension_out( dimension_os );
    dimension_out.start_object();
    // what reading dimension data replaces, as it is
    dimension_out.member( "power_networks" );
    g->power_networks().serialize( dimension_out );
    dimension_out.member( "next_item_uid", saved_counter );
    dimension_out.end_object();
    g->unserialize_dimension_data( json_loader::from_string( dimension_os.str() ) );
    CHECK( generate_next_item_uid() >= saved_counter );
}

namespace
{

// Runs fn with a new, empty world active, compressed or not; the test world comes back after.
void in_scratch_world( bool compressed, const std::function<void()> &fn )
{
    WORLD *const test_world = world_generator->active_world;
    const std::string name = compressed ? "audit map-p03 compressed" : "audit map-p03 plain";
    WORLD *scratch = nullptr;
    {
        override_option compression( "WORLD_COMPRESSION2", compressed ? "true" : "false" );
        scratch = world_generator->make_new_world( name, test_world->active_mod_order );
    }
    REQUIRE( scratch != nullptr );
    world_generator->set_active_world( scratch );
    on_out_of_scope restore( [&]() {
        world_generator->set_active_world( test_world );
        world_generator->delete_world( name, true );
    } );
    REQUIRE( scratch->has_compression_enabled() == compressed );
    fn();
}

// The four submaps of an OMT: uniform rock, or with a dirt tile, so that the quad is saved
void add_quad( mapbuffer &mb, const tripoint_abs_omt &omt, bool uniform, bool reverted = false )
{
    const tripoint_abs_sm base = project_to<coords::sm>( omt );
    for( int dx = 0; dx < 2; ++dx ) {
        for( int dy = 0; dy < 2; ++dy ) {
            std::unique_ptr<submap> sm = std::make_unique<submap>();
            if( !uniform ) {
                sm->set_ter( point_sm_ms( 1, 1 ), ter_t_dirt.id() );
            }
            sm->reverted = reverted;
            REQUIRE( mb.add_submap( base + tripoint_rel_sm( dx, dy, 0 ), sm ) );
        }
    }
}

bool quad_loads( mapbuffer &mb, const tripoint_abs_omt &omt )
{
    const submap *sm = mb.lookup_submap( project_to<coords::sm>( omt ) + tripoint_rel_sm( 1, 1,
                                         0 ) );
    return sm != nullptr && !sm->is_uniform() && sm->get_ter( point_sm_ms( 1, 1 ) ) == ter_t_dirt.id();
}

cata_path segment_path( const tripoint_abs_omt &omt )
{
    const tripoint_abs_seg seg = project_to<coords::seg>( omt );
    return PATH_INFO::current_dimension_save_path() / "maps" /
           string_format( "%d.%d.%d", seg.x(), seg.y(), seg.z() );
}

// A quad as the save holds it, read without the mapbuffer, as an older build would
bool quad_file_holds_dirt( const tripoint_abs_omt &omt, bool compressed )
{
    const cata_path dir = segment_path( omt );
    const std::string name = string_format( "%d.%d.%d.map", omt.x(), omt.y(), omt.z() );
    std::string contents;
    if( compressed ) {
        cata_path zzip_name = dir;
        zzip_name += zzip_suffix;
        if( !file_exist( zzip_name ) ) {
            return false;
        }
        std::optional<zzip> z = zzip::load( zzip_name.get_unrelative_path(),
                                            ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
        if( !z || !z->has_file( std::filesystem::u8path( name ) ) ) {
            return false;
        }
        const std::vector<std::byte> bytes = z->get_file( std::filesystem::u8path( name ) );
        contents.assign( reinterpret_cast<const char *>( bytes.data() ), bytes.size() );
    } else {
        if( !file_exist( dir / name ) ) {
            return false;
        }
        contents = read_entire_file( ( dir / name ).get_unrelative_path() );
    }
    return contents.find( "t_dirt" ) != std::string::npos &&
           contents.find( "coordinates" ) != std::string::npos;
}

} // namespace

// MAP-P03: with compression on, saving opened (and so created) the segment's zzip for every
// quad, even the uniform ones that are not written.
TEST_CASE( "map-p03_saving_only_uniform_quads_creates_no_files", "[audit][map]" )
{
    const bool compressed = GENERATE( true, false );
    CAPTURE( compressed );
    in_scratch_world( compressed, [&]() {
        const tripoint_abs_omt omt( 40, 40, 5 );
        mapbuffer mb;
        add_quad( mb, omt, true );
        add_quad( mb, omt + tripoint_rel_omt( 1, 0, 0 ), true );
        mb.save( true );
        CHECK( mb.begin() == mb.end() );
        cata_path zzip_name = segment_path( omt );
        zzip_name += zzip_suffix;
        CHECK_FALSE( file_exist( zzip_name ) );
        CHECK_FALSE( dir_exist( segment_path( omt ).get_unrelative_path() ) );
        // and nothing to load: the uniform quads get regenerated
        CHECK( mb.lookup_submap( project_to<coords::sm>( omt ) ) == nullptr );
    } );
}

// MAP-P03: lookups of quads that are not on disk are answered from a listing of each map
// segment. The listing has to follow every save, delete and revert, through any mapbuffer, and
// a cleared mapbuffer (game load, dimension travel) must not keep it.
TEST_CASE( "map-p03_quads_load_after_saves_reverts_and_clears", "[audit][map]" )
{
    const bool compressed = GENERATE( true, false );
    CAPTURE( compressed );
    in_scratch_world( compressed, [&]() {
        // all in one segment, at one z-level
        const tripoint_abs_omt a( 40, 40, 0 );
        const tripoint_abs_omt b = a + tripoint_rel_omt( 1, 0, 0 );
        const tripoint_abs_omt c = a + tripoint_rel_omt( 2, 0, 0 );
        const tripoint_abs_omt d = a + tripoint_rel_omt( 3, 0, 0 );
        mapbuffer mb;

        // nothing saved yet
        CHECK_FALSE( mb.submap_exists( project_to<coords::sm>( a ) ) );
        CHECK_FALSE( quad_loads( mb, b ) );

        // a saved quad loads, the others still don't
        add_quad( mb, a, false );
        mb.save( true );
        REQUIRE( mb.begin() == mb.end() );
        CHECK( quad_file_holds_dirt( a, compressed ) );
        CHECK( quad_loads( mb, a ) );
        CHECK_FALSE( quad_loads( mb, b ) );
        CHECK( mb.submap_exists_approx( project_to<coords::sm>( a ) ) );

        // a quad saved after a miss loads too
        add_quad( mb, b, false );
        mb.save( true );
        CHECK( quad_file_holds_dirt( b, compressed ) );
        CHECK( quad_loads( mb, b ) );
        mb.save( true );

        // a quad that reverts to uniform is deleted, and is not loaded again
        add_quad( mb, c, false );
        mb.save( true );
        REQUIRE( quad_file_holds_dirt( c, compressed ) );
        add_quad( mb, c, true, true );
        mb.save( true );
        CHECK_FALSE( quad_file_holds_dirt( c, compressed ) );
        CHECK( mb.lookup_submap( project_to<coords::sm>( c ) ) == nullptr );
        // and saved again when it changes again
        add_quad( mb, c, false );
        mb.save( true );
        CHECK( quad_loads( mb, c ) );
        mb.save( true );

        // another mapbuffer writes d after this one saw it missing: the listings are shared,
        // so this one loads it
        CHECK_FALSE( quad_loads( mb, d ) );
        {
            mapbuffer other;
            add_quad( other, d, false );
            other.save( true );
        }
        CHECK( quad_loads( mb, d ) );
        // a cleared mapbuffer (game load, dimension travel) lists the segments again
        mb.clear();
        CHECK( quad_loads( mb, a ) );
        CHECK( quad_loads( mb, d ) );
    } );
}

// MAP-P03: worlds saved by older builds have a zzip for every segment the player saw, empty
// when all its quads were uniform. Such a segment reads as having no quads, and can be
// written to.
TEST_CASE( "map-p03_empty_segment_zzip_from_an_older_build_loads", "[audit][map]" )
{
    in_scratch_world( true, [&]() {
        const tripoint_abs_omt omt( 40, 40, -3 );
        cata_path zzip_name = segment_path( omt );
        zzip_name += zzip_suffix;
        assure_dir_exist( PATH_INFO::current_dimension_save_path() / "maps" );
        {
            std::optional<zzip> z = zzip::load( zzip_name.get_unrelative_path(),
                                                ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
            REQUIRE( z );
        }
        REQUIRE( file_exist( zzip_name ) );
        mapbuffer mb;
        CHECK_FALSE( mb.submap_exists( project_to<coords::sm>( omt ) ) );
        CHECK_FALSE( mb.submap_exists_approx( project_to<coords::sm>( omt ) ) );
        add_quad( mb, omt, false );
        mb.save( true );
        CHECK( quad_file_holds_dirt( omt, true ) );
        CHECK( quad_loads( mb, omt ) );
    } );
}

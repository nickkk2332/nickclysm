#include <set>
#include <sstream>
#include <string>

#include "achievement.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "coordinates.h"
#include "event.h"
#include "event_bus.h"
#include "event_statistics.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "json_loader.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "point.h"
#include "stats_tracker.h"
#include "type_id.h"

static const achievement_id
achievement_achievement_reach_exodii_base( "achievement_reach_exodii_base" );
static const achievement_id
achievement_achievement_reach_refugee_center( "achievement_reach_refugee_center" );

static const event_statistic_id
event_statistic_last_overmap_special_avatar_entered( "last_overmap_special_avatar_entered" );
static const event_statistic_id
event_statistic_last_oter_type_avatar_entered( "last_oter_type_avatar_entered" );

static const oter_type_str_id oter_type_field( "field" );

static const overmap_special_id overmap_special_evac_center( "evac_center" );

namespace
{

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

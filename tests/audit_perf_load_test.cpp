#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "achievement.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "coordinates.h"
#include "event.h"
#include "event_bus.h"
#include "event_statistics.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "json_loader.h"
#include "mapbuffer.h"
#include "options_helpers.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "point.h"
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

static const oter_type_str_id oter_type_field( "field" );

static const overmap_special_id overmap_special_evac_center( "evac_center" );

static const ter_str_id ter_t_dirt( "t_dirt" );

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
// segment. The listing has to follow every save, delete and revert, and a cleared mapbuffer
// (game load, dimension travel) must not keep it.
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

        // another mapbuffer writes d after this one saw it missing; once this one is
        // cleared, as on a game load or dimension travel, d loads
        CHECK_FALSE( quad_loads( mb, d ) );
        {
            mapbuffer other;
            add_quad( other, d, false );
            other.save( true );
        }
        mb.clear();
        CHECK( quad_loads( mb, d ) );
        CHECK( quad_loads( mb, a ) );
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

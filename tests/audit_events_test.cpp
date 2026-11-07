#include <cstddef>
#include <functional>
#include <sstream>
#include <string>

#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "character.h"
#include "character_id.h"
#include "event.h"
#include "event_bus.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "json_loader.h"
#include "past_achievements_info.h"
#include "path_info.h"
#include "stats_tracker.h"
#include "type_id.h"

static const achievement_id achievement_achievement_kill_zombie( "achievement_kill_zombie" );

// EVT-B02: statistics are saved keyed by event type name. A name this build does not
// know (an event removed or renamed since the save was written, or one from a newer
// build) used to throw io::InvalidEnumString out of the save/memorial loaders.
TEST_CASE( "evt-b02_stats_tracker_skips_unknown_event_types", "[audit][stats]" )
{
    stats_tracker s;
    event_bus b;
    b.subscribe( &s );
    const character_id u_id = get_player_character().getID();
    constexpr event_type ctd = event_type::character_takes_damage;
    b.send<ctd>( u_id, 10, bodypart_str_id::NULL_ID(), 0, damage_type_id() );
    b.send<ctd>( u_id, 5, bodypart_str_id::NULL_ID(), 0, damage_type_id() );
    REQUIRE( s.get_events( ctd ).count() == 2 );

    std::ostringstream os;
    JsonOut jsout( os );
    s.serialize( jsout );
    std::string saved = os.str();

    // Inject statistics for an event type that does not exist in this build.
    const std::string data_key = "\"data\":{";
    const size_t pos = saved.find( data_key );
    REQUIRE( pos != std::string::npos );
    saved.insert( pos + data_key.size(),
                  R"("audit_no_such_event_type":{"event_counts":[[{},{"count":3,"first":0,"last":0}]]},)" );

    stats_tracker loaded;
    const JsonValue jv = json_loader::from_string( saved );
    CHECK_NOTHROW( loaded.deserialize( jv.get_object() ) );
    // The known event type's statistics survive the unknown one.
    CHECK( loaded.get_events( ctd ).count() == 2 );
}

// EVT-B02: achievement files are shared by every world and game version. One file this
// build can't read used to throw out of past_achievements_info::load(), so files after
// it were never read.
TEST_CASE( "evt-b02_bad_achievements_file_does_not_stop_others", "[audit][stats]" )
{
    const cata_path dir = PATH_INFO::achievementdir_path();
    assure_dir_exist( dir );
    const cata_path bad_file = dir / "audit_evt_b02_bad.json";
    const cata_path good_file = dir / "audit_evt_b02_good.json";
    // Remove the files even if a check below aborts the test, so they can't leak into
    // later tests that load past achievements.
    const auto remove_files = [&]() {
        remove_file( bad_file );
        remove_file( good_file );
        clear_past_achievements();
    };
    on_out_of_scope cleanup( remove_files );
    remove_files();
    write_to_file( bad_file, []( std::ostream & os ) {
        os << R"({"achievement_version":99,"achievements":[],"avatar_name":"Nobody"})";
    } );
    write_to_file( good_file, []( std::ostream & os ) {
        os << R"({"achievement_version":1,"achievements":["achievement_kill_zombie"],)"
           << R"("avatar_name":"Audit Tester"})";
    } );

    // The file with an unknown version is skipped (with a log entry, not a popup) and the
    // other file still loads.
    CHECK( get_past_achievements().is_completed( achievement_achievement_kill_zombie ) );
}

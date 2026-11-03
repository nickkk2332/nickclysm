#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "achievement.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "diary.h"
#include "event.h"
#include "event_bus.h"
#include "event_statistics.h"
#include "event_subscriber.h"
#include "explosion.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "magic.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "path_info.h"
#include "player_helpers.h"
#include "stats_tracker.h"
#include "string_formatter.h"
#include "submap.h"
#include "talker.h"
#include "timed_event.h"
#include "type_id.h"

static const achievement_id achievement_achievement_arcade_mode( "achievement_arcade_mode" );
static const achievement_id achievement_achievement_kill_zombie( "achievement_kill_zombie" );
static const event_statistic_id event_statistic_audit_evt_b08_unique_z( "audit_evt_b08_unique_z" );
static const event_statistic_id event_statistic_max_move_z( "max_move_z" );
static const event_statistic_id event_statistic_min_move_z( "min_move_z" );
static const itype_id itype_nail( "nail" );
static const move_mode_id move_mode_walk( "walk" );
static const mtype_id mon_zombie( "mon_zombie" );
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

// EVT-B05: the diary read the names of the spells on its pages from the avatar's spellbook,
// so a spell the avatar had forgotten since raised "Tried to get unknown spell", for example
// in the diary written out on death.
TEST_CASE( "evt-b05_diary_lists_spells_the_avatar_has_forgotten", "[audit][diary]" )
{
    clear_avatar();
    REQUIRE_FALSE( get_avatar().magic->knows_spell( spell_test_fake_spell ) );

    diary d;
    d.deserialize( json_loader::from_string(
                       R"({"owner":"Audit EVT-B05","pages":[{"text":"","turn":0,"spells":{"test_fake_spell":2}}]})" ) );

    const cata_path dir = PATH_INFO::memorialdir_path();
    const bool had_dir = dir_exist( dir.get_unrelative_path() );
    REQUIRE( assure_dir_exist( dir ) );
    const cata_path file = dir / "Audit EVT-B05s_diary.txt";
    on_out_of_scope cleanup( [&]() {
        remove_file( file );
        if( !had_dir ) {
            remove_directory( dir.get_unrelative_path() );
        }
    } );

    // Writes the diary to the memorial folder; the question first is answered no in tests
    const std::string errors = capture_debugmsg_during( [&]() {
        d.death_entry();
    } );
    CHECK( errors.empty() );
    const std::string text = read_entire_file( file.get_unrelative_path() );
    CHECK( text.find( "test fake spell: 2" ) != std::string::npos );
}

namespace
{
// The achievements a tracker writes to the achievements folder
std::set<std::string> written_achievements( const achievements_tracker &a )
{
    std::ostringstream os;
    a.write_json_achievements( os, "Audit EVT-B07" );
    std::set<std::string> ret;
    const JsonObject jo = json_loader::from_string( os.str() );
    jo.allow_omitted_members();
    for( const std::string id : jo.get_array( "achievements" ) ) {
        ret.insert( id );
    }
    return ret;
}

void start_and_kill_a_zombie( event_bus &b, achievements_tracker &a, bool enabled )
{
    const character_id u_id = get_player_character().getID();
    b.send<event_type::game_start>( "audit" );
    b.send<event_type::game_avatar_new>( true, false, u_id, "Audit EVT-B07", "CUSTOM_PROFESSION" );
    a.set_enabled( enabled );
    b.send<event_type::character_kills_monster>( u_id, mon_zombie, 0 );
    REQUIRE( a.is_completed( achievement_achievement_kill_zombie ) ==
             achievement_completion::completed );
}
} // namespace

// EVT-B07: using the debug menu disables achievements, but those completed afterwards were
// still written to the achievements folder, which unlocks professions and scenarios in
// every world.
TEST_CASE( "evt-b07_achievements_earned_while_disabled_are_not_written",
           "[audit][achievement]" )
{
    const auto ignore = []( const achievement *, bool ) {};

    SECTION( "earned while enabled" ) {
        event_bus b;
        stats_tracker s;
        b.subscribe( &s );
        achievements_tracker a( s, ignore, ignore, true );
        b.subscribe( &a );
        start_and_kill_a_zombie( b, a, true );
        CHECK( written_achievements( a ).count( achievement_achievement_kill_zombie.str() ) );
    }

    SECTION( "earned while disabled" ) {
        event_bus b;
        stats_tracker s;
        b.subscribe( &s );
        achievements_tracker a( s, ignore, ignore, true );
        b.subscribe( &a );
        start_and_kill_a_zombie( b, a, false );
        // given from the debug menu on purpose
        a.report_achievement( &achievement_achievement_arcade_mode.obj(),
                              achievement_completion::completed );
        // and enabling them again doesn't count what was earned meanwhile
        a.set_enabled( true );

        std::set<std::string> written = written_achievements( a );
        CHECK_FALSE( written.count( achievement_achievement_kill_zombie.str() ) );
        CHECK( written.count( achievement_achievement_arcade_mode.str() ) );

        // also after a save and load
        std::ostringstream os;
        JsonOut jsout( os );
        a.serialize( jsout );
        stats_tracker s2;
        achievements_tracker loaded( s2, ignore, ignore, false );
        loaded.deserialize( json_loader::from_string( os.str() ).get_object() );
        written = written_achievements( loaded );
        CHECK_FALSE( written.count( achievement_achievement_kill_zombie.str() ) );
        CHECK( written.count( achievement_achievement_arcade_mode.str() ) );
    }
}

namespace
{
struct audit_stat_watcher : stat_watcher {
    void new_value( const cata_variant &v, stats_tracker & ) override {
        value = v;
    }
    cata_variant value;
};

void move_on_z( event_bus &b, int z )
{
    b.send<event_type::avatar_moves>( mtype_id::NULL_ID(), ter_str_id::NULL_ID(), move_mode_walk,
                                      false, z );
}
} // namespace

// EVT-B08: minimum and maximum started from 0, so a minimum of positive values (or a maximum
// of negative ones) read 0.
TEST_CASE( "evt-b08_minimum_and_maximum_start_from_the_first_event", "[audit][stats]" )
{
    stats_tracker s;
    event_bus b;
    b.subscribe( &s );
    audit_stat_watcher min_watcher;
    audit_stat_watcher max_watcher;
    min_watcher.value = s.add_watcher( event_statistic_min_move_z, &min_watcher );
    max_watcher.value = s.add_watcher( event_statistic_max_move_z, &max_watcher );
    // with no events they are 0
    CHECK( s.value_of( event_statistic_min_move_z ) == cata_variant( 0 ) );
    CHECK( min_watcher.value == cata_variant( 0 ) );

    SECTION( "above ground" ) {
        move_on_z( b, 3 );
        move_on_z( b, 5 );
        CHECK( s.get_events( event_type::avatar_moves ).minimum( "z" ) == 3 );
        CHECK( s.value_of( event_statistic_min_move_z ) == cata_variant( 3 ) );
        CHECK( min_watcher.value == cata_variant( 3 ) );
        CHECK( max_watcher.value == cata_variant( 5 ) );
    }
    SECTION( "below ground" ) {
        move_on_z( b, -2 );
        move_on_z( b, -4 );
        CHECK( s.get_events( event_type::avatar_moves ).maximum( "z" ) == -2 );
        CHECK( s.value_of( event_statistic_max_move_z ) == cata_variant( -2 ) );
        CHECK( max_watcher.value == cata_variant( -2 ) );
        CHECK( min_watcher.value == cata_variant( -4 ) );
    }
}

// EVT-B08: unique_value has a value while every event is the same.  The watched value (used
// by achievements) counted events instead, so it disagreed after two identical events, and
// after a load it no longer changed.
TEST_CASE( "evt-b08_unique_value_watched_and_computed_agree", "[audit][stats]" )
{
    const event_statistic_id &stat = event_statistic_audit_evt_b08_unique_z;
    stats_tracker s;
    event_bus b;
    b.subscribe( &s );
    audit_stat_watcher watcher;
    watcher.value = s.add_watcher( stat, &watcher );
    CHECK( watcher.value == cata_variant() );

    move_on_z( b, 2 );
    move_on_z( b, 2 );
    CHECK( s.value_of( stat ) == cata_variant( 2 ) );
    CHECK( watcher.value == cata_variant( 2 ) );

    // a save and load, then a different event
    std::ostringstream os;
    JsonOut jsout( os );
    s.serialize( jsout );
    stats_tracker loaded;
    loaded.deserialize( json_loader::from_string( os.str() ).get_object() );
    event_bus b2;
    b2.subscribe( &loaded );
    audit_stat_watcher loaded_watcher;
    loaded_watcher.value = loaded.add_watcher( stat, &loaded_watcher );
    CHECK( loaded_watcher.value == cata_variant( 2 ) );

    move_on_z( b2, 5 );
    CHECK( loaded.value_of( stat ) == cata_variant() );
    CHECK( loaded_watcher.value == cata_variant() );
}

// EVT-B09: REVERT_SUBMAP used the submap without checking that there was one.  Here there is
// none: nothing was ever generated this far away.
TEST_CASE( "evt-b09_revert_of_a_submap_that_is_not_there", "[audit][timed_event]" )
{
    timed_events_restorer restore;
    const tripoint_abs_ms far_away = get_avatar().pos_abs() + tripoint_rel_ms( 2400, 2400, 0 );
    get_timed_events().add( timed_event_type::REVERT_SUBMAP, calendar::turn, -1, far_away, 0, "",
                            submap(), "audit_evt_b09" );
    get_timed_events().process();
    CHECK_FALSE( get_timed_events().queued( timed_event_type::REVERT_SUBMAP ) );
}

// EVT-B09: timed events at a location didn't know their dimension, so after dimension travel
// they changed the same spot in the wrong dimension.  They now wait until the player is back.
TEST_CASE( "evt-b09_map_events_wait_for_their_dimension", "[audit][timed_event]" )
{
    timed_events_restorer restore;
    const dimension_id here = g->get_dimension_prefix();
    const dimension_id elsewhere( "audit_evt_b09_elsewhere" );
    const tripoint_abs_ms far_away = get_avatar().pos_abs() + tripoint_rel_ms( 2400, 2400, 0 );
    get_timed_events().add( timed_event_type::REVERT_SUBMAP, calendar::turn, -1, far_away, 0, "",
                            submap(), "audit_evt_b09" );
    timed_event *revert = get_timed_events().get( timed_event_type::REVERT_SUBMAP,
                          "audit_evt_b09" );
    REQUIRE( revert != nullptr );
    CHECK( revert->dimension == here );

    // queued in another dimension: it waits, also through a save and load
    revert->dimension = elsewhere;
    get_timed_events().process();
    load_timed_events( saved_timed_events() );
    revert = get_timed_events().get( timed_event_type::REVERT_SUBMAP, "audit_evt_b09" );
    REQUIRE( revert != nullptr );
    CHECK( revert->dimension == elsewhere );

    // back in its dimension, it happens
    revert->dimension = here;
    get_timed_events().process();
    CHECK_FALSE( get_timed_events().queued( timed_event_type::REVERT_SUBMAP ) );
}

// EVT-B09: events from saves before this have no dimension: they belong to the main one
TEST_CASE( "evt-b09_timed_event_from_an_older_save_is_in_the_main_dimension",
           "[audit][timed_event]" )
{
    timed_events_restorer restore;
    load_timed_events( string_format(
                           R"([{"faction":-1,"map_point":[0,0,0],"map_square":[0,0,0],"strength":-1,)"
                           R"("string_id":"","type":%d,"when":%d,"key":"","revert":"t_null"}])",
                           static_cast<int>( timed_event_type::REVERT_SUBMAP ),
                           to_turns<int>( calendar::turn - calendar::turn_zero ) + 600 ) );
    const timed_event *revert = get_timed_events().get( timed_event_type::REVERT_SUBMAP );
    REQUIRE( revert != nullptr );
    CHECK( revert->dimension == dimension_id( "default" ) );
}

namespace
{
talk_effect_t load_audit_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    return talk_effect_t( jo, "effect", "audit_events_sweep_test" );
}

// Gets an event made from strings, as trigger_event does, and what was reported
cata::event make_dyn_reporting( event_type type, std::vector<std::string> args,
                                std::string &report )
{
    cata::event e;
    report = capture_debugmsg_during( [&]() {
        e = cata::event::make_dyn( type, args );
    } );
    return e;
}
} // namespace

// EVT-B10: trigger_event stored its arguments without checking them; a value its field
// can't hold threw out of the game loop (std::stoi) when the event was read, after the
// statistics had saved it.
TEST_CASE( "evt-b10_dynamic_events_refuse_values_their_fields_cannot_hold", "[audit][event]" )
{
    std::string report;
    // avatar_moves: mount, terrain, movement_mode, underwater (bool), z (int)
    cata::event fine = make_dyn_reporting( event_type::avatar_moves,
    { "", "t_dirt", "walk", "1", "-3" }, report );
    CHECK( report.empty() );
    REQUIRE( fine.type() == event_type::avatar_moves );
    CHECK( fine.get<int>( "z" ) == -3 );
    CHECK( fine.get<bool>( "underwater" ) );

    const std::vector<std::vector<std::string>> bad_args = {
        { "", "t_dirt", "walk", "true", "0" },   // bool
        { "", "t_dirt", "walk", "0", "high" },   // int
        { "", "t_dirt", "walk", "0", "1e+06" },  // int
        { "", "t_dirt", "walk", "0", "1.5" },    // int
        { "", "audit_no_such_terrain", "walk", "0", "0" } // id
    };
    for( const std::vector<std::string> &args : bad_args ) {
        CAPTURE( args );
        const cata::event e = make_dyn_reporting( event_type::avatar_moves, args, report );
        CHECK( e.type() == event_type::num_event_types );
        CHECK( report.find( "Invalid value" ) != std::string::npos );
    }
    // character_kills_monster: killer (character_id), victim_type, exp
    const cata::event e = make_dyn_reporting( event_type::character_kills_monster,
    { "Bob", "mon_zombie", "0" }, report );
    CHECK( e.type() == event_type::num_event_types );
}

// EVT-B10: numbers were passed with six digits, so 1000000 became "1e+06" and read as 1
TEST_CASE( "evt-b10_trigger_event_passes_numbers_in_full", "[audit][event]" )
{
    struct event_catcher : event_subscriber {
        std::vector<cata::event> events;
        using event_subscriber::notify;
        void notify( const cata::event &e ) override {
            events.push_back( e );
        }
    } catcher;
    get_event_bus().subscribe( &catcher );
    // npc_goal_changed isn't kept by the statistics, so this leaves no trace there
    const talk_effect_t effect = load_audit_effect(
                                     R"({ "effect": { "trigger_event": "npc_goal_changed", "args": [ 1000000, "old", "new" ] } })" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    effect.apply( d );
    get_event_bus().unsubscribe( &catcher );
    REQUIRE( catcher.events.size() == 1 );
    CHECK( catcher.events[0].get<character_id>( "npc" ).get_value() == 1000000 );
}

// EVT-B10: only the game starts, loads, saves and ends games and awards achievements;
// trigger_event faking these broke statistics and achievements, or tripped an assertion.
TEST_CASE( "evt-b10_trigger_event_refuses_engine_only_events", "[audit][event]" )
{
    CHECK_THROWS_AS( load_audit_effect(
                         R"({ "effect": { "trigger_event": "game_start", "args": [ "audit" ] } })" ),
                     JsonError );
    CHECK_NOTHROW( load_audit_effect(
                       R"({ "effect": { "trigger_event": "u_var_changed", "args": [ "a", "b" ] } })" ) );
}

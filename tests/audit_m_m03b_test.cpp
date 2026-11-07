#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "condition.h"
#include "construction.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "dialogue.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "event_statistics.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "hash_utils.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "mission.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "stats_tracker.h"
#include "talker.h"
#include "timed_event.h"
#include "type_id.h"

static const activity_id ACT_BUILD( "ACT_BUILD" );

static const construction_str_id construction_test_constr_eoc_hooks( "test_constr_eoc_hooks" );

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );

static const event_statistic_id
event_statistic_audit_m03b_average_exp( "audit_m03b_average_exp" );
static const event_statistic_id
event_statistic_audit_m03b_distinct_victims( "audit_m03b_distinct_victims" );
static const event_statistic_id
event_statistic_audit_m03b_eaten_categories( "audit_m03b_eaten_categories" );
static const event_statistic_id
event_statistic_audit_m03b_eaten_materials( "audit_m03b_eaten_materials" );
static const event_statistic_id
event_statistic_audit_m03b_first_kill_time( "audit_m03b_first_kill_time" );
static const event_statistic_id
event_statistic_audit_m03b_last_kill_time( "audit_m03b_last_kill_time" );
static const event_statistic_id
event_statistic_audit_m03b_num_kills_clumsy( "audit_m03b_num_kills_clumsy" );

static const itype_id itype_apple( "apple" );
static const itype_id itype_test_rock( "test_rock" );

static const mission_type_id mission_TEST_MISSION_FIND_ITEM( "TEST_MISSION_FIND_ITEM" );

static const mtype_id mon_dog( "mon_dog" );
static const mtype_id mon_zombie( "mon_zombie" );

static const ter_str_id ter_t_wall( "t_wall" );

namespace
{
// What one event carried, and its talkers
struct caught_event {
    cata::event e;
    bool alpha_is_item = false;
    bool beta_is_item = false;
    std::string beta_itype;
    const Creature *alpha = nullptr;
    const Creature *beta = nullptr;
};

// Keeps the events of some types that go through the game's event bus while it lives
struct event_catcher : event_subscriber {
    std::vector<event_type> types;
    std::vector<caught_event> caught;
    explicit event_catcher( std::vector<event_type> t ) : types( std::move( t ) ) {
        get_event_bus().subscribe( this );
    }
    ~event_catcher() override {
        get_event_bus().unsubscribe( this );
    }
    event_catcher( const event_catcher & ) = delete;
    event_catcher &operator=( const event_catcher & ) = delete;
    bool wanted( const cata::event &e ) const {
        for( const event_type t : types ) {
            if( t == e.type() ) {
                return true;
            }
        }
        return false;
    }
    void notify( const cata::event &e ) override {
        if( wanted( e ) ) {
            caught_event c;
            c.e = e;
            caught.push_back( c );
        }
    }
    void notify( const cata::event &e, std::unique_ptr<talker> alpha,
                 std::unique_ptr<talker> beta ) override {
        if( !wanted( e ) ) {
            return;
        }
        caught_event c;
        c.e = e;
        if( alpha ) {
            c.alpha_is_item = alpha->get_item() != nullptr;
            c.alpha = alpha->get_creature();
        }
        if( beta ) {
            item_location *loc = beta->get_item();
            c.beta_is_item = loc != nullptr && *loc;
            if( c.beta_is_item ) {
                c.beta_itype = ( *loc )->typeId().str();
            }
            c.beta = beta->get_creature();
        }
        caught.push_back( c );
    }
    std::vector<caught_event> of( event_type t ) const {
        std::vector<caught_event> result;
        for( const caught_event &c : caught ) {
            if( c.e.type() == t ) {
                result.push_back( c );
            }
        }
        return result;
    }
};

// Puts back the game's statistics for these event types
struct event_stats_restorer {
    std::vector<std::pair<event_type, event_multiset>> saved;
    explicit event_stats_restorer( const std::vector<event_type> &types ) {
        for( event_type type : types ) {
            saved.emplace_back( type, get_stats().get_events( type ) );
        }
    }
    ~event_stats_restorer() {
        for( const std::pair<event_type, event_multiset> &events : saved ) {
            get_stats().get_events( events.first ) = events.second;
        }
    }
    event_stats_restorer( const event_stats_restorer & ) = delete;
    event_stats_restorer &operator=( const event_stats_restorer & ) = delete;
};

// Global variables are shared by every test: put back what was there
struct globals_restorer {
    global_variables::impl_t saved = get_globals().get_global_values();
    globals_restorer() = default;
    ~globals_restorer() {
        get_globals().set_global_values( saved );
    }
    globals_restorer( const globals_restorer & ) = delete;
    globals_restorer &operator=( const globals_restorer & ) = delete;
};

// Empties the timed event queue for a test, and puts it back afterwards
struct timed_events_restorer {
    std::string saved;
    timed_events_restorer() {
        std::ostringstream os;
        JsonOut jsout( os );
        timed_event_manager::serialize_all( jsout );
        saved = os.str();
        get_timed_events() = timed_event_manager();
    }
    // NOLINTNEXTLINE(bugprone-exception-escape): a save that can't be read back should end the test run
    ~timed_events_restorer() {
        get_timed_events() = timed_event_manager();
        timed_event_manager::unserialize_all( json_loader::from_string( saved ).get_array() );
    }
    timed_events_restorer( const timed_events_restorer & ) = delete;
    timed_events_restorer &operator=( const timed_events_restorer & ) = delete;
};

talk_effect_t load_m03b_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_m_m03b_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

conditional_t::func load_m03b_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}
} // namespace

// EVT-J06: a construction ends inside its activity's turn, so the activity never ran out of
// moves and its completion hooks (completion_eoc, character_finished_activity) never ran.
TEST_CASE( "evt-j06_finished_construction_completes_its_activity", "[audit][event]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    globals_restorer restore_globals;
    event_stats_restorer restore_stats( { event_type::character_finished_activity,
                                          event_type::character_finishes_construction,
                                          event_type::character_starts_activity } );
    on_out_of_scope cleanup( [&u]() {
        u.remove_value( "test_constr_allowed" );
        u.remove_value( "test_constr_turns" );
        u.remove_value( "test_constr_done" );
        u.remove_value( "test_constr_done_pos" );
        clear_map();
    } );
    const tripoint_bub_ms site = u.pos_bub() + tripoint::east;
    u.set_value( "test_constr_allowed", 1 );
    partial_con pc;
    pc.id = construction_test_constr_eoc_hooks.id();
    here.partial_con_set( site, pc );

    event_catcher events( { event_type::character_finished_activity } );
    u.assign_activity( build_construction_activity_actor( here.get_abs( site ) ) );
    REQUIRE( u.activity.id() == ACT_BUILD );
    REQUIRE( process_activity_bounded( u, 1000, 2000 ) );
    REQUIRE( u.maybe_get_value( "test_constr_done" ) != nullptr );

    int completed = 0;
    for( const caught_event &c : events.of( event_type::character_finished_activity ) ) {
        if( c.e.get<activity_id>( "activity" ) == ACT_BUILD ) {
            CHECK_FALSE( c.e.get<bool>( "canceled" ) );
            CHECK( c.e.get<std::string>( "outcome" ) == "completed" );
            ++completed;
        }
    }
    CHECK( completed == 1 );
}

// EVT-J06: monster::die only sent character_kills_monster, and only for a character's kill
TEST_CASE( "evt-j06_monster_dies_for_any_killer", "[audit][event]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    event_stats_restorer restore_stats( { event_type::monster_dies,
                                          event_type::character_kills_monster,
                                          event_type::monster_takes_damage } );
    on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_creatures();
        clear_map();
    } );
    event_catcher events( { event_type::monster_dies } );
    // not a follower: the avatar's kills and its followers' would count in the kill tracker
    npc &hunter = spawn_npc( ( u.pos_bub() + tripoint_rel_ms( -2, 0, 0 ) ).xy(), "test_talker" );
    REQUIRE_FALSE( hunter.is_player_ally() );

    monster &alone = spawn_test_monster( mon_zombie.str(), u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    monster &dog = spawn_test_monster( mon_dog.str(), u.pos_bub() + tripoint_rel_ms( 0, 3, 0 ) );
    monster &bitten = spawn_test_monster( mon_zombie.str(), u.pos_bub() + tripoint_rel_ms( 0, 4,
                                          0 ) );
    monster &shot = spawn_test_monster( mon_zombie.str(), u.pos_bub() + tripoint_rel_ms( 4, 0, 0 ) );

    alone.die( &here, nullptr );
    bitten.die( &here, &dog );
    shot.die( &here, &hunter );

    const std::vector<caught_event> died = events.of( event_type::monster_dies );
    REQUIRE( died.size() == 3 );
    // nothing killed it
    CHECK( died[0].e.get<mtype_id>( "victim_type" ) == mon_zombie );
    CHECK_FALSE( died[0].e.get<character_id>( "killer" ).is_valid() );
    CHECK( died[0].e.get<mtype_id>( "killer_type" ) == mtype_id::NULL_ID() );
    CHECK( died[0].alpha == &alone );
    CHECK( died[0].beta == nullptr );
    // a monster did
    CHECK_FALSE( died[1].e.get<character_id>( "killer" ).is_valid() );
    CHECK( died[1].e.get<mtype_id>( "killer_type" ) == mon_dog );
    CHECK( died[1].alpha == &bitten );
    CHECK( died[1].beta == &dog );
    // a character did
    CHECK( died[2].e.get<character_id>( "killer" ) == hunter.getID() );
    CHECK( died[2].e.get<mtype_id>( "killer_type" ) == mtype_id::NULL_ID() );
    CHECK( died[2].beta == &hunter );
}

// EVT-J06: character_dies carried no killer
TEST_CASE( "evt-j06_character_dies_names_its_killer", "[audit][event]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    event_stats_restorer restore_stats( { event_type::character_dies,
                                          event_type::character_kills_character } );
    on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_creatures();
        clear_map();
    } );
    event_catcher events( { event_type::character_dies } );

    npc &guy = spawn_npc( ( u.pos_bub() + tripoint_rel_ms( 2, 0, 0 ) ).xy(), "test_talker" );
    monster &zombie = spawn_test_monster( mon_zombie.str(), u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    const character_id guy_id = guy.getID();
    guy.set_part_hp_cur( body_part_head.id(), 0 );
    guy.die( &here, &zombie );

    const std::vector<caught_event> died = events.of( event_type::character_dies );
    REQUIRE( died.size() == 1 );
    CHECK( died[0].e.get<character_id>( "character" ) == guy_id );
    CHECK_FALSE( died[0].e.get<character_id>( "killer" ).is_valid() );
    CHECK( died[0].e.get<mtype_id>( "killer_type" ) == mon_zombie );
    CHECK( died[0].beta == &zombie );
}

// EVT-J06: missions ended without an event, so a mod couldn't react to missions it didn't own
TEST_CASE( "evt-j06_mission_ends_on_failure_and_success", "[audit][event]" )
{
    avatar &u = get_avatar();
    clear_character( u, true );
    u.reset_all_missions();
    mission::clear_all();
    event_stats_restorer restore_stats( { event_type::mission_ends } );
    on_out_of_scope cleanup( [&u]() {
        u.reset_all_missions();
        mission::clear_all();
        clear_character( u, true );
    } );
    event_catcher events( { event_type::mission_ends } );

    const auto assign_mission = [&u]() {
        mission *m = mission::reserve_new( mission_TEST_MISSION_FIND_ITEM, character_id() );
        if( m->get_assigned_player_id() == u.getID() ) {
            m->set_assigned_player_id( character_id( -2 ) );
        }
        m->assign( u );
        return m;
    };
    mission *failed = assign_mission();
    failed->fail();
    mission *done = assign_mission();
    item rock( itype_test_rock );
    u.wield( rock );
    done->wrap_up();

    const std::vector<caught_event> ended = events.of( event_type::mission_ends );
    REQUIRE( ended.size() == 2 );
    CHECK( ended[0].e.get<character_id>( "character" ) == u.getID() );
    CHECK( ended[0].e.get<std::string>( "mission" ) == mission_TEST_MISSION_FIND_ITEM.str() );
    CHECK_FALSE( ended[0].e.get<bool>( "success" ) );
    CHECK( ended[0].alpha == &u );
    CHECK( ended[1].e.get<bool>( "success" ) );
}

// EVT-J07: u_var_changed only came from u_add_var/npc_add_var, called every owner's variable
// the avatar's, and math writes sent nothing.  A watched variable sends var_changed for any
// write, with its owner.
TEST_CASE( "evt-j07_watched_variables_send_var_changed", "[audit][event]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    globals_restorer restore_globals;
    event_stats_restorer restore_stats( { event_type::var_changed, event_type::u_var_changed } );
    monster &zombie = spawn_test_monster( mon_zombie.str(), u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    on_out_of_scope cleanup( [&u]() {
        for( const char *name : {
                 "audit_m03b_watched", "audit_m03b_other", "audit_m03b_plain", "audit_m03b_loop"
             } ) {
            u.remove_value( name );
        }
        clear_creatures();
        clear_map();
    } );
    REQUIRE( get_globals().watched.count( "audit_m03b_watched" ) == 1 );
    event_catcher events( { event_type::var_changed, event_type::u_var_changed } );

    dialogue on_u( get_talker_for( u ), nullptr );
    load_m03b_effect( R"({ "effect": { "math": [ "u_audit_m03b_watched = 5" ] } })" ).apply( on_u );
    load_m03b_effect( R"({ "effect": { "math": [ "u_audit_m03b_other = 5" ] } })" ).apply( on_u );
    load_m03b_effect( R"({ "effect": { "math": [ "audit_m03b_watched = 7" ] } })" ).apply( on_u );
    dialogue on_zombie( get_talker_for( zombie ), nullptr );
    load_m03b_effect( R"({ "effect": { "u_add_var": "audit_m03b_watched", "value": "yes" } })" )
    .apply( on_zombie );

    const std::vector<caught_event> changed = events.of( event_type::var_changed );
    REQUIRE( changed.size() == 3 );
    // the avatar's, by math
    CHECK( changed[0].e.get<character_id>( "owner" ) == u.getID() );
    CHECK( changed[0].e.get<std::string>( "scope" ) == "character" );
    CHECK( changed[0].e.get<std::string>( "var" ) == "audit_m03b_watched" );
    CHECK( changed[0].e.get<std::string>( "value" ) == "5" );
    CHECK( changed[0].alpha == &u );
    // a global
    CHECK_FALSE( changed[1].e.get<character_id>( "owner" ).is_valid() );
    CHECK( changed[1].e.get<std::string>( "scope" ) == "global" );
    CHECK( changed[1].e.get<std::string>( "value" ) == "7" );
    // a monster's, by u_add_var
    CHECK( changed[2].e.get<std::string>( "scope" ) == "monster" );
    CHECK( changed[2].e.get<std::string>( "value" ) == "yes" );
    CHECK( changed[2].alpha == &zombie );
    // the monster's variable isn't the avatar's
    CHECK( events.of( event_type::u_var_changed ).empty() );

    // u_add_var on the avatar still sends u_var_changed
    load_m03b_effect( R"({ "effect": { "u_add_var": "audit_m03b_plain", "value": "x" } })" )
    .apply( on_u );
    const std::vector<caught_event> legacy = events.of( event_type::u_var_changed );
    REQUIRE( legacy.size() == 1 );
    CHECK( legacy[0].e.get<std::string>( "var" ) == "audit_m03b_plain" );
}

// EVT-J07: a var_changed EOC that writes the variable it watches would call itself forever
TEST_CASE( "evt-j07_var_changed_eoc_writing_its_variable_runs_once", "[audit][event]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    globals_restorer restore_globals;
    event_stats_restorer restore_stats( { event_type::var_changed } );
    on_out_of_scope cleanup( [&u]() {
        u.remove_value( "audit_m03b_loop" );
    } );
    get_globals().set_global_value( "audit_m03b_loop_runs", 0 );

    dialogue on_u( get_talker_for( u ), nullptr );
    load_m03b_effect( R"({ "effect": { "math": [ "u_audit_m03b_loop = 1" ] } })" ).apply( on_u );

    const diag_value *runs = get_globals().maybe_get_global_value( "audit_m03b_loop_runs" );
    REQUIRE( runs != nullptr );
    CHECK( runs->dbl() == 1 );
    const diag_value *value = u.maybe_get_value( "audit_m03b_loop" );
    REQUIRE( value != nullptr );
    CHECK( value->dbl() == 2 );
}

// EVT-J08: the damage events had no damage type, and monster_takes_damage no victim
TEST_CASE( "evt-j08_damage_events_name_the_damage_type", "[audit][event]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    event_stats_restorer restore_stats( { event_type::monster_takes_damage,
                                          event_type::character_takes_damage,
                                          event_type::character_heals_damage,
                                          event_type::monster_dies } );
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_avatar();
        clear_map();
    } );
    monster &zombie = spawn_test_monster( mon_zombie.str(), u.pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    // so that neither hit kills it (a weak point can multiply the 40), which would leave the
    // second hit without an event
    zombie.set_hp( 1000 );
    event_catcher events( { event_type::monster_takes_damage, event_type::character_takes_damage,
                            event_type::character_heals_damage
                          } );

    const bodypart_id torso = body_part_torso.id();
    zombie.deal_damage( nullptr, torso, damage_instance( damage_bash, 40 ) );
    const std::vector<caught_event> monster_hit = events.of( event_type::monster_takes_damage );
    zombie.apply_damage( nullptr, torso, 1 );
    u.deal_damage( nullptr, torso, damage_instance( damage_cut, 10 ) );
    const std::vector<caught_event> u_hit = events.of( event_type::character_takes_damage );
    u.apply_damage( nullptr, torso, 1 );

    REQUIRE( !monster_hit.empty() );
    CHECK( monster_hit.front().e.get<mtype_id>( "victim_type" ) == mon_zombie );
    CHECK( monster_hit.front().e.get<int>( "damage" ) > 0 );
    CHECK( monster_hit.front().e.get<damage_type_id>( "damage_type" ) == damage_bash );
    // damage of no type, applied directly
    const std::vector<caught_event> monster_hurt = events.of( event_type::monster_takes_damage );
    REQUIRE( monster_hurt.size() > monster_hit.size() );
    CHECK( monster_hurt.back().e.get<damage_type_id>( "damage_type" ).is_empty() );

    REQUIRE( !u_hit.empty() );
    CHECK( u_hit.front().e.get<int>( "damage" ) > 0 );
    CHECK( u_hit.front().e.get<damage_type_id>( "damage_type" ) == damage_cut );
    const std::vector<caught_event> u_hurt = events.of( event_type::character_takes_damage );
    REQUIRE( u_hurt.size() > u_hit.size() );
    CHECK( u_hurt.back().e.get<damage_type_id>( "damage_type" ).is_empty() );

    u.heal( torso, 2 );
    const std::vector<caught_event> healed = events.of( event_type::character_heals_damage );
    REQUIRE( healed.size() == 1 );
    CHECK( healed[0].e.get<bodypart_id>( "bodypart" ) == torso );
}

// EVT-J08: eating and consuming used a plain send, so their EOCs couldn't read the item
TEST_CASE( "evt-j08_eating_gives_the_item_as_beta_talker", "[audit][event]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    event_stats_restorer restore_stats( { event_type::character_eats_item,
                                          event_type::character_consumes_item } );
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    event_catcher events( { event_type::character_eats_item, event_type::character_consumes_item } );

    item apple( itype_apple );
    REQUIRE( u.consume( apple, true ) != trinary::NONE );

    const std::vector<caught_event> ate = events.of( event_type::character_eats_item );
    REQUIRE( ate.size() == 1 );
    CHECK( ate[0].beta_is_item );
    CHECK( ate[0].beta_itype == itype_apple.str() );
    CHECK( ate[0].alpha == &u );
    const std::vector<caught_event> consumed = events.of( event_type::character_consumes_item );
    REQUIRE( consumed.size() == 1 );
    CHECK( consumed[0].beta_is_item );
    CHECK( consumed[0].beta_itype == itype_apple.str() );
}

// EVT-J08: the new location fields reach EOCs, but the statistics leave them out: one
// summary per tile, kept for good, would grow every save
TEST_CASE( "evt-j08_statistics_leave_out_locations", "[audit][event]" )
{
    stats_tracker s;
    event_bus b;
    b.subscribe( &s );
    const character_id u_id = get_player_character().getID();
    b.send<event_type::character_smashes_tile>( u_id, ter_t_wall, furn_str_id::NULL_ID(),
            tripoint( 1, 2, 0 ) );
    b.send<event_type::character_smashes_tile>( u_id, ter_t_wall, furn_str_id::NULL_ID(),
            tripoint( 5, 6, 0 ) );
    const event_multiset &smashes = s.get_events( event_type::character_smashes_tile );
    CHECK( smashes.count() == 2 );
    REQUIRE( smashes.counts().size() == 1 );
    CHECK( smashes.counts().begin()->first.count( "pos" ) == 0 );
    CHECK( smashes.counts().begin()->first.count( "terrain" ) == 1 );

    // a statistic can't name a field it doesn't keep
    CHECK( stats_tracker::unrecorded_fields( event_type::cuts_tree ) ==
           std::vector<std::string> { "pos" } );
    CHECK( stats_tracker::unrecorded_fields( event_type::character_kills_monster ).empty() );
}

// EVT-J09: the statistics had no distinct count, average or time stats, and no way to
// filter by a monster's flags or an item's category or materials
TEST_CASE( "evt-j09_new_stat_types_and_transformations", "[audit][event][stats]" )
{
    restore_on_out_of_scope<time_point> restore_turn( calendar::turn );
    stats_tracker s;
    event_bus b;
    b.subscribe( &s );
    const character_id u_id = get_player_character().getID();
    const bool zombie_clumsy = mon_zombie->has_flag( mon_flag_CLUMSY_ATTACKS );
    const bool dog_clumsy = mon_dog->has_flag( mon_flag_CLUMSY_ATTACKS );
    // one of the two has the flag, so the count tells them apart
    REQUIRE( zombie_clumsy != dog_clumsy );

    CHECK( event_statistic_audit_m03b_first_kill_time->value( s ) == cata_variant( -1 ) );
    CHECK( event_statistic_audit_m03b_average_exp->value( s ) == cata_variant( 0 ) );

    // The values kept up to date as events come must be the ones counted from scratch
    struct watch_stat : stat_watcher {
        cata_variant value;
        void new_value( const cata_variant &v, stats_tracker & ) override {
            value = v;
        }
    };
    watch_stat distinct_watcher;
    watch_stat average_watcher;
    watch_stat first_watcher;
    watch_stat last_watcher;
    s.add_watcher( event_statistic_audit_m03b_distinct_victims, &distinct_watcher );
    s.add_watcher( event_statistic_audit_m03b_average_exp, &average_watcher );
    s.add_watcher( event_statistic_audit_m03b_first_kill_time, &first_watcher );
    s.add_watcher( event_statistic_audit_m03b_last_kill_time, &last_watcher );

    calendar::turn = calendar::turn_zero + 100_turns;
    b.send<event_type::character_kills_monster>( u_id, mon_zombie, 10 );
    calendar::turn = calendar::turn_zero + 300_turns;
    b.send<event_type::character_kills_monster>( u_id, mon_zombie, 20 );
    calendar::turn = calendar::turn_zero + 600_turns;
    b.send<event_type::character_kills_monster>( u_id, mon_dog, 31 );

    CHECK( event_statistic_audit_m03b_distinct_victims->value( s ) == cata_variant( 2 ) );
    // 61 / 3, rounded toward zero
    CHECK( event_statistic_audit_m03b_average_exp->value( s ) == cata_variant( 20 ) );
    CHECK( event_statistic_audit_m03b_first_kill_time->value( s ) == cata_variant( 100 ) );
    CHECK( event_statistic_audit_m03b_last_kill_time->value( s ) == cata_variant( 600 ) );
    CHECK( distinct_watcher.value == cata_variant( 2 ) );
    CHECK( average_watcher.value == cata_variant( 20 ) );
    CHECK( first_watcher.value == cata_variant( 100 ) );
    CHECK( last_watcher.value == cata_variant( 600 ) );
    CHECK( event_statistic_audit_m03b_num_kills_clumsy->value( s ) ==
           cata_variant( zombie_clumsy ? 2 : 1 ) );

    // an item's category and materials
    b.send<event_type::character_eats_item>( u_id, itype_apple );
    b.send<event_type::character_eats_item>( u_id, itype_apple );
    CHECK( event_statistic_audit_m03b_eaten_categories->value( s ) ==
           cata_variant::make<cata_variant_type::item_category_id>( itype_apple->category_force ) );
    CHECK( event_statistic_audit_m03b_eaten_materials->value( s ) ==
           cata_variant( static_cast<int>( itype_apple->materials.size() ) ) );
}

// EVT-J10: ROBOT_ATTACK, queued by an eyebot's photograph, did nothing when it came
TEST_CASE( "evt-j10_robot_attack_sends_becomes_wanted", "[audit][event]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    event_stats_restorer restore_stats( { event_type::becomes_wanted } );
    event_catcher events( { event_type::becomes_wanted } );
    const tripoint_abs_ms photographed = u.pos_abs() + tripoint_rel_ms( 2, 0, 0 );
    timed_event robots( timed_event_type::ROBOT_ATTACK, calendar::turn, 0, photographed, -1, "" );
    robots.actualize();

    const std::vector<caught_event> wanted = events.of( event_type::becomes_wanted );
    REQUIRE( wanted.size() == 1 );
    CHECK( wanted[0].e.get<character_id>( "character" ) == u.getID() );
    CHECK( wanted[0].e.get<tripoint>( "pos" ) == photographed.raw() );
}

// EVT-J10: EOCs could reschedule keyed timed events but not ask about them or cancel them
TEST_CASE( "evt-j10_eocs_query_and_cancel_keyed_timed_events", "[audit][event]" )
{
    timed_events_restorer restore_events;
    avatar &u = get_avatar();
    get_timed_events().add( timed_event_type::CUSTOM_LIGHT_LEVEL, calendar::turn + 1_hours, -1, 10,
                            "audit_m03b_key" );
    get_timed_events().add( timed_event_type::CUSTOM_LIGHT_LEVEL, calendar::turn + 2_hours, -1, 10,
                            "audit_m03b_key" );
    // one the game queues itself: no key
    get_timed_events().add( timed_event_type::DIM, calendar::turn + 1_hours );
    REQUIRE( get_timed_events().count_keyed( "audit_m03b_key" ) == 2 );

    dialogue d( get_talker_for( u ), nullptr );
    const conditional_t::func queued = load_m03b_condition(
                                           R"({ "condition": { "timed_event_queued": "audit_m03b_key" } })" );
    const conditional_t::func unkeyed = load_m03b_condition(
                                            R"({ "condition": { "timed_event_queued": "" } })" );
    CHECK( queued( d ) );
    CHECK_FALSE( unkeyed( d ) );

    load_m03b_effect( R"({ "effect": { "cancel_timed_events": "audit_m03b_key" } })" ).apply( d );
    CHECK_FALSE( queued( d ) );
    CHECK( get_timed_events().count_keyed( "audit_m03b_key" ) == 0 );
    // the unkeyed one is still there
    CHECK( get_timed_events().queued( timed_event_type::DIM ) );
    CHECK_FALSE( get_timed_events().queued( timed_event_type::CUSTOM_LIGHT_LEVEL ) );
    // an empty key cancels nothing
    load_m03b_effect( R"({ "effect": { "cancel_timed_events": "" } })" ).apply( d );
    CHECK( get_timed_events().queued( timed_event_type::DIM ) );
}

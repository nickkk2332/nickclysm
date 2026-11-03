#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapbuffer.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "npc.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "timed_event.h"
#include "type_id.h"
#include "viewer.h"

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_B07_REQUEUE( "EOC_AUDIT_B07_REQUEUE" );
static const effect_on_condition_id effect_on_condition_EOC_AUDIT_B14_LOOP( "EOC_AUDIT_B14_LOOP" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_B14_RECURSE( "EOC_AUDIT_B14_RECURSE" );

static const itype_id itype_test_rock( "test_rock" );

static const oter_str_id oter_field( "field" );

static const overmap_special_id overmap_special_test_audit_b08_shelter( "test_audit_b08_shelter" );

static talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_eoc_dialogue_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

static conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

namespace
{
// Holds the avatar's EOC queue and the global one aside, empty, while a test runs EOC queues,
// and puts them back afterwards.
class eoc_queues_aside
{
    public:
        eoc_queues_aside() {
            avatar &u = get_avatar();
            avatar_queue = std::move( u.queued_effect_on_conditions );
            u.queued_effect_on_conditions = queued_eocs();
            avatar_inactive.swap( u.inactive_effect_on_condition_vector );
            global_queue = std::move( g->queued_global_effect_on_conditions );
            g->queued_global_effect_on_conditions = queued_eocs();
            global_inactive.swap( g->inactive_global_effect_on_condition_vector );
        }
        ~eoc_queues_aside() {
            avatar &u = get_avatar();
            u.queued_effect_on_conditions = std::move( avatar_queue );
            u.inactive_effect_on_condition_vector = std::move( avatar_inactive );
            g->queued_global_effect_on_conditions = std::move( global_queue );
            g->inactive_global_effect_on_condition_vector = std::move( global_inactive );
        }
        eoc_queues_aside( const eoc_queues_aside & ) = delete;
        eoc_queues_aside &operator=( const eoc_queues_aside & ) = delete;
    private:
        queued_eocs avatar_queue;
        std::vector<effect_on_condition_id> avatar_inactive;
        queued_eocs global_queue;
        std::vector<effect_on_condition_id> global_inactive;
};

int count_in( const queued_eocs &queue, const effect_on_condition_id &eoc )
{
    return std::count_if( queue.list.begin(), queue.list.end(), [&eoc]( const queued_eoc & q ) {
        return q.eoc == eoc;
    } );
}

// The list's own count agrees with its nodes, and every node is in the priority queue once.
bool queue_is_whole( const queued_eocs &queue )
{
    return queue.list.size() == static_cast<size_t>( std::distance( queue.list.begin(),
            queue.list.end() ) ) && queue.list.size() == queue.queue.size();
}

double global_number( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value ? value->dbl() : 0.0;
}
} // namespace

// EOC-B06: test_eoc called the EOC's condition directly, and an EOC without one has an empty
// std::function, so the call threw std::bad_function_call.
TEST_CASE( "eoc-b06_test_eoc_on_an_eoc_without_a_condition", "[audit][eoc]" )
{
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const conditional_t::func no_condition = load_condition(
                R"({ "condition": { "test_eoc": "EOC_AUDIT_B06_NO_CONDITION" } })" );
    const conditional_t::func false_condition = load_condition(
                R"({ "condition": { "test_eoc": "EOC_AUDIT_B06_FALSE_CONDITION" } })" );
    CHECK( no_condition( d ) );
    CHECK_FALSE( false_condition( d ) );
    // Testing runs no effects
    CHECK_FALSE( get_globals().maybe_get_global_value( "audit_b06_ran" ) );
}

// EOC-B07: a queued EOC that queued itself again with no delay was popped again by the same
// pass of the queue, forever.  Here it stops itself after 50 runs; it should run once a pass.
TEST_CASE( "eoc-b07_requeue_with_no_delay_waits_for_the_next_pass", "[audit][eoc]" )
{
    const eoc_queues_aside aside;
    avatar &u = get_avatar();
    global_variables &globals = get_globals();
    globals.remove_global_value( "audit_b07_runs" );
    on_out_of_scope forget( [&globals]() {
        globals.remove_global_value( "audit_b07_runs" );
    } );

    effect_on_conditions::queue_effect_on_condition( 0_turns, effect_on_condition_EOC_AUDIT_B07_REQUEUE,
            u, {} );
    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( global_number( "audit_b07_runs" ) == 1 );
    effect_on_conditions::process_effect_on_conditions( u );
    CHECK( global_number( "audit_b07_runs" ) == 2 );
    // The next run is waiting in the queue
    CHECK( count_in( u.queued_effect_on_conditions, effect_on_condition_EOC_AUDIT_B07_REQUEUE ) == 1 );
    CHECK( queue_is_whole( u.queued_effect_on_conditions ) );
}

// EOC-B08 (1): copy_location generated a missing destination but looked the source up with
// lookup_submap, which is null for a place never generated, and dereferenced it.
TEST_CASE( "eoc-b08_copy_location_from_a_place_never_generated", "[audit][eoc]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_abs_omt home = project_to<coords::omt>( here.get_abs_sub() );
    // Well outside the reality bubble, in the same overmap
    const point_om_omt local = project_remain<coords::om>( home.xy() ).remainder;
    const point_rel_omt away( local.x() < OMAPX / 2 ? 30 : -30, local.y() < OMAPY / 2 ? 30 : -30 );
    const tripoint_abs_omt source = home + away;
    const tripoint_abs_omt dest = source + point_rel_omt( 1, 0 );
    const tripoint_abs_sm source_sm = project_to<coords::sm>( source );
    REQUIRE_FALSE( MAPBUFFER.submap_exists( source_sm ) );
    REQUIRE_FALSE( MAPBUFFER.submap_exists( project_to<coords::sm>( dest ) ) );

    // Plain fields generate nothing that outlives the submaps
    const oter_id old_source = overmap_buffer.ter( source );
    const oter_id old_dest = overmap_buffer.ter( dest );
    overmap_buffer.ter_set( source, oter_field.id() );
    overmap_buffer.ter_set( dest, oter_field.id() );
    global_variables &globals = get_globals();
    globals.set_global_value( "audit_b08_source", project_to<coords::ms>( source ) );
    globals.set_global_value( "audit_b08_dest", project_to<coords::ms>( dest ) );
    on_out_of_scope restore( [&]() {
        globals.remove_global_value( "audit_b08_source" );
        globals.remove_global_value( "audit_b08_dest" );
        overmap_buffer.ter_set( source, old_source );
        overmap_buffer.ter_set( dest, old_dest );
        MAPBUFFER.clear_outside_reality_bubble();
    } );

    const size_t events_before = get_timed_events().get_all().size();
    const talk_effect_t copy = load_effect(
                                   R"({ "effect": { "copy_location": { "global_val": "audit_b08_source" },
        "new_loc": { "global_val": "audit_b08_dest" }, "time_in_future": 0, "key": "audit_b08_copy" } })" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    copy.apply( d );
    CHECK( MAPBUFFER.submap_exists( source_sm ) );
    // One revert per submap of the tile
    CHECK( get_timed_events().get_all().size() == events_before + 4 );

    // Carry the copy out, which also takes the events off the queue
    get_timed_events().set_all( "audit_b08_copy", 0_turns );
    get_timed_events().process();
    CHECK( get_timed_events().get_all().size() == events_before );
}

// EOC-B08 (2): near_om_location checked FACTION_CAMP_START on each tile around the talker with
// the centre tile's mapgen arguments, so a camp start that depends on them was judged by the
// wrong ones.  A bare-bones camp can start anywhere, so the answer is always yes; the wrong
// arguments showed as errors about the shelter's parameters, or as an exception when the centre
// belonged to a special not generated yet.
TEST_CASE( "eoc-b08_near_om_location_reads_each_tiles_own_arguments", "[audit][eoc]" )
{
    const tripoint_abs_omt home = project_to<coords::omt>( get_map().get_abs_sub() );
    const point_om_omt local = project_remain<coords::om>( home.xy() ).remainder;
    const point_rel_omt away( local.x() < OMAPX / 2 ? 40 : -40, local.y() < OMAPY / 2 ? 20 : -20 );
    // An evac shelter on the first tile the search looks at
    const tripoint_abs_omt centre = home + away;
    const tripoint_abs_omt shelter = centre + point_rel_omt( -1, -1 );
    const oter_id old_shelter = overmap_buffer.ter( shelter );
    REQUIRE( overmap_buffer.place_special( *overmap_special_test_audit_b08_shelter, shelter,
                                           om_direction::type::north, false, true ) );
    std::optional<mapgen_arguments> *args = overmap_buffer.mapgen_args( shelter );
    REQUIRE( args != nullptr );
    on_out_of_scope restore( [&]() {
        args->reset();
        overmap_buffer.ter_set( shelter, old_shelter );
    } );

    // Someone standing next to it
    npc stander;
    stander.spawn_at_precise( project_to<coords::ms>( centre ) );
    dialogue d( get_talker_for( stander ), nullptr );
    const conditional_t::func near_camp_start = load_condition(
                R"({ "condition": { "u_near_om_location": "FACTION_CAMP_START", "range": 1 } })" );
    const auto check_quietly = [&]() {
        bool result = false;
        const std::string messages = capture_debugmsg_during( [&]() {
            result = near_camp_start( d );
        } );
        CHECK( messages.empty() );
        CHECK( result );
    };

    SECTION( "the shelter's layout is decided" ) {
        const std::unordered_map<std::string, cata_variant> layout = {
            { "shelter_layout", cata_variant::from_string( cata_variant_type::nested_mapgen_id, "24x24_shelter" ) }
        };
        *args = mapgen_arguments( layout );
        check_quietly();
    }
    SECTION( "not decided yet, as before the shelter is generated" ) {
        args->reset();
        check_quietly();
    }
}

// EOC-B08 (3): location_variable's npc search only asked for NPCs within one submap of the
// centre, and compared their reality-bubble position with a point of the map it searched.
TEST_CASE( "eoc-b08_location_variable_finds_npcs_across_its_whole_radius", "[audit][eoc]" )
{
    clear_map();
    avatar &u = get_avatar();
    const npc &guy = spawn_npc( u.pos_bub().xy() + point( 30, 0 ), "test_talker" );
    const character_id guy_id = guy.getID();
    const tripoint_abs_ms guy_pos = guy.pos_abs();
    global_variables &globals = get_globals();
    on_out_of_scope restore( [&]() {
        globals.remove_global_value( "audit_b08_npc_loc" );
        overmap_buffer.remove_npc( guy_id );
        clear_npcs();
    } );

    const talk_effect_t find_npc = load_effect(
                                       R"({ "effect": { "u_location_variable": { "global_val": "audit_b08_npc_loc" },
        "npc": "", "target_max_radius": 35 } })" );
    dialogue d( get_talker_for( u ), nullptr );
    find_npc.apply( d );
    const diag_value *found = globals.maybe_get_global_value( "audit_b08_npc_loc" );
    REQUIRE( found != nullptr );
    CHECK( found->tripoint() == guy_pos );
}

// EOC-B10: a failed response with a failure_topic was listed but could never be picked; and
// front-inserted responses (repeat responses) took the condition flags of the response that
// had been at their index.
TEST_CASE( "eoc-b10_response_flags_follow_their_responses", "[audit][npc_talk]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    u.i_add( item( itype_test_rock ) );
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), get_talker_for( u ) );
    d.add_topic( "TALK_AUDIT_B10" );
    d.gen_responses( d.topic_stack.back() );
    REQUIRE( d.responses.size() == d.response_condition_exists.size() );
    REQUIRE( d.responses.size() == d.response_condition_eval.size() );

    std::vector<std::string> texts;
    std::map<std::string, bool> selectable;
    std::map<std::string, std::string> next_topic;
    for( size_t i = 0; i < d.responses.size(); ++i ) {
        const std::string text = d.responses[i].truetext.translated();
        texts.push_back( text );
        selectable[text] = !d.response_condition_exists[i] || d.response_condition_eval[i];
        next_topic[text] = d.responses[i].success.next_topic.id;
    }
    CAPTURE( texts );
    REQUIRE( texts.size() == 6 );
    // Repeat responses go in front
    CHECK( texts[0] == "Repeated without a condition." );
    CHECK( texts[1] == "Repeated with a condition." );

    CHECK( selectable["Repeated without a condition."] );
    CHECK( selectable["Repeated with a condition."] );
    CHECK( selectable["Available."] );
    CHECK_FALSE( selectable["Shown though unavailable."] );
    // A failure explanation alone only explains
    CHECK_FALSE( selectable["*Never: Fails with an explanation."] );
    // A failure topic can be picked, and leads there
    CHECK( selectable["*Not now: Fails with a topic."] );
    CHECK( next_topic["*Not now: Fails with a topic."] == "TALK_AUDIT_B10_FAILED" );
}

// EOC-B11: max_mass was read as units::from_gram( has_float( "max_mass" ) ), a limit of 1 mg,
// and never marked as read; none of the three numbers took the documented variable objects.
TEST_CASE( "eoc-b11_pickup_items_reads_its_limits", "[audit][eoc]" )
{
    const std::string messages = capture_debugmsg_during( []() {
        load_effect( R"({ "effect": { "u_pickup_items": { "u_val": "audit_b11_loc" }, "max_mass": 500,
            "max_volume": { "global_val": "audit_b11_volume" },
            "extra_moves_per_item": { "math": [ "1 + 1" ] } } })" );
    } );
    CHECK( messages.empty() );
}

// EOC-B12: the whitelist_type and blacklist_type filters of u_pick_bodypart 'continue'd the
// inner loop over types, so they never left a body part out.
TEST_CASE( "eoc-b12_pick_bodypart_type_filters", "[audit][eoc]" )
{
    clear_avatar();
    global_variables &globals = get_globals();
    on_out_of_scope forget( [&globals]() {
        globals.remove_global_value( "audit_b12_bp" );
    } );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const auto picks = [&]( const std::string & filters ) {
        const talk_effect_t pick = load_effect(
                                       R"({ "effect": { "u_pick_bodypart": { "global_val": "audit_b12_bp" },
            "pick_random": true, )" + filters + " } }" );
        std::set<std::string> picked;
        for( int i = 0; i < 40; ++i ) {
            globals.remove_global_value( "audit_b12_bp" );
            pick.apply( d );
            const diag_value *bp = globals.maybe_get_global_value( "audit_b12_bp" );
            REQUIRE( bp != nullptr );
            picked.insert( bp->str() );
        }
        return picked;
    };

    const std::set<std::string> arms = picks( R"("whitelist_type": [ "arm" ])" );
    CHECK( arms == std::set<std::string> { "arm_l", "arm_r" } );
    const std::set<std::string> not_limbs = picks( R"("blacklist_type": [ "arm", "leg" ])" );
    CHECK( not_limbs == std::set<std::string> { "head", "torso" } );
}

// EOC-B13: effects and conditions dereferenced the talker's character, item, faction or body
// part without checking, so running them with another kind of talker crashed.
TEST_CASE( "eoc-b13_effects_with_the_wrong_talker_debugmsg_instead_of_crashing", "[audit][eoc]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 3, 0 ) );
    global_variables &globals = get_globals();
    on_out_of_scope restore( [&globals]() {
        globals.remove_global_value( "audit_b13_false_eocs" );
        clear_creatures();
        clear_avatar();
    } );

    // Runs the effect on a dialogue from monster to avatar; true when it logged a debugmsg
    const auto monster_runs = [&]( const std::string & json ) {
        const talk_effect_t effect = load_effect( R"({ "effect": )" + json + " }" );
        dialogue d( get_talker_for( zed ), get_talker_for( u ) );
        return !capture_debugmsg_during( [&]() {
            effect.apply( d );
        } ).empty();
    };
    CHECK( monster_runs( R"({ "u_lose_category": "MUTCAT_ALPHA" })" ) );
    CHECK( monster_runs( R"({ "u_lose_mutation_type": "hair_style" })" ) );
    CHECK( monster_runs( R"({ "u_add_wound": "arm_l", "wound_id": "test_wound_capped_pain" })" ) );
    CHECK( monster_runs(
               R"({ "u_remove_wound": "arm_l", "wound_id": [ "test_wound_capped_pain" ] })" ) );
    CHECK( monster_runs( R"({ "u_remove_all_wounds": "arm_l" })" ) );
    CHECK( monster_runs( R"({ "u_level_spell_class": "all" })" ) );
    CHECK( monster_runs( R"({ "u_forget_recipe": "CC_FOOD", "category": true })" ) );
    // The monster sells to the avatar
    CHECK( monster_runs( R"("u_bulk_trade_accept")" ) );

    SECTION( "a body part the character doesn't have" ) {
        const talk_effect_t wound = load_effect(
                                        R"({ "effect": { "u_add_wound": "test_tail", "wound_id": "test_wound_capped_pain" } })" );
        dialogue d( get_talker_for( u ), nullptr );
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            wound.apply( d );
        } ).empty() );
    }

    SECTION( "only the avatar opens a dialogue or takes control" ) {
        globals.remove_global_value( "audit_b13_false_eocs" );
        dialogue d( get_talker_for( zed ), get_talker_for( u ) );
        const talk_effect_t open = load_effect( R"({ "effect": { "open_dialogue": { "topic": "TALK_DONE",
            "false_eocs": [ "EOC_AUDIT_B13_MARK" ] } } })" );
        const talk_effect_t take_control = load_effect( R"({ "effect": "take_control",
            "false_eocs": [ "EOC_AUDIT_B13_MARK" ] })" );
        open.apply( d );
        take_control.apply( d );
        CHECK( global_number( "audit_b13_false_eocs" ) == 2 );
    }

    SECTION( "selling to a talker without a faction" ) {
        globals.remove_global_value( "audit_b13_false_eocs" );
        u.i_add( item( itype_test_rock ) );
        REQUIRE( u.has_amount( itype_test_rock, 1 ) );
        const talk_effect_t sell = load_effect(
                                       R"({ "effect": { "u_sell_item": "test_rock", "false_eocs": [ "EOC_AUDIT_B13_MARK" ] } })" );
        dialogue d( get_talker_for( u ), get_talker_for( zed ) );
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            sell.apply( d );
        } ).empty() );
        CHECK( u.has_amount( itype_test_rock, 1 ) );
        CHECK( global_number( "audit_b13_false_eocs" ) == 1 );
    }

    SECTION( "trading with a talker without a faction" ) {
        // A character that isn't the avatar sells, so nothing asks for confirmation
        npc seller;
        dialogue d( get_talker_for( seller ), get_talker_for( zed ) );
        d.cur_item = itype_test_rock;
        const talk_effect_t trade = load_effect( R"({ "effect": "u_bulk_trade_accept" })" );
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            trade.apply( d );
        } ).empty() );
    }

    SECTION( "faction trust needs a faction" ) {
        const talk_effect_t trust = load_effect( R"({ "effect": { "u_add_faction_trust": 5 } })" );
        dialogue d( get_talker_for( u ), get_talker_for( zed ) );
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            trust.apply( d );
        } ).empty() );
        const conditional_t::func has_trust = load_condition(
                R"({ "condition": { "u_has_faction_trust": 0 } })" );
        bool result = true;
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            result = has_trust( d );
        } ).empty() );
        CHECK_FALSE( result );
    }

    SECTION( "faults go on items" ) {
        dialogue d( get_talker_for( u ), nullptr );
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            load_effect( R"({ "effect": { "u_set_fault": "fault_gun_dirt" } })" ).apply( d );
        } ).empty() );
        CHECK_FALSE( capture_debugmsg_during( [&]() {
            load_effect( R"({ "effect": { "u_set_random_fault_of_type": "mechanical" } })" ).apply( d );
        } ).empty() );
    }
}

TEST_CASE( "eoc-b13_conditions_with_the_wrong_talker_are_false", "[audit][eoc]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 3, 0 ) );
    on_out_of_scope restore( []() {
        clear_creatures();
        clear_avatar();
    } );

    // Evaluates the condition on @p d; true when it was false and logged a debugmsg
    const auto false_with_message = []( const std::string & json, const_dialogue const & d ) {
        const conditional_t::func cond = load_condition( R"({ "condition": )" + json + " }" );
        bool result = true;
        const std::string messages = capture_debugmsg_during( [&]() {
            result = cond( d );
        } );
        CAPTURE( json, messages );
        return !result && !messages.empty();
    };
    const dialogue monster_first( get_talker_for( zed ), get_talker_for( u ) );
    const dialogue avatar_first( get_talker_for( u ), get_talker_for( zed ) );
    CHECK( false_with_message( R"({ "u_has_visible_trait": "ELFA_EARS" })", monster_first ) );
    CHECK( false_with_message( R"({ "u_has_visible_trait": "ELFA_EARS" })", avatar_first ) );
    CHECK( false_with_message( R"({ "u_has_profession": "unemployed" })", monster_first ) );
    CHECK( false_with_message( R"({ "u_bodypart_is_bleeding": "arm_l" })", monster_first ) );
    CHECK( false_with_message( R"({ "u_bodypart_is_bleeding": "test_tail" })", avatar_first ) );
    item_location pebble = u.i_add( item( itype_test_rock ) );
    CHECK( false_with_message( R"("u_is_outside")", dialogue( get_talker_for( pebble ), nullptr ) ) );

    SECTION( "the player can look at the tile of any talker" ) {
        item_location rock = u.i_add( item( itype_test_rock ) );
        const dialogue d( get_talker_for( u ), get_talker_for( rock ) );
        const conditional_t::func player_sees = load_condition( R"({ "condition": "player_see_npc" })" );
        bool sees = false;
        CHECK( capture_debugmsg_during( [&]() {
            sees = player_sees( d );
        } ).empty() );
        CHECK( sees == get_player_view().sees( get_map(), u.pos_bub() ) );
    }

    SECTION( "a monster's dodge" ) {
        global_variables &globals = get_globals();
        on_out_of_scope forget( [&globals]() {
            globals.remove_global_value( "audit_b13_dodge" );
        } );
        dialogue d( get_talker_for( zed ), nullptr );
        const talk_effect_t read_dodge = load_effect(
                                             R"json({ "effect": { "math": [ "audit_b13_dodge = u_val('dodge')" ] } })json" );
        read_dodge.apply( d );
        CHECK( global_number( "audit_b13_dodge" ) == Approx( zed.get_dodge() ) );
    }
}

// EOC-B14: run_eocs gives each run a new dialogue without the callstack, so an EOC that ran
// itself through it was never stopped and overflowed the stack.
TEST_CASE( "eoc-b14_endless_recursion_stops_with_one_message", "[audit][eoc]" )
{
    global_variables &globals = get_globals();
    on_out_of_scope forget( [&globals]() {
        globals.remove_global_value( "audit_b14_depth" );
    } );
    globals.remove_global_value( "audit_b14_depth" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const std::string messages = capture_debugmsg_during( [&]() {
        effect_on_condition_EOC_AUDIT_B14_RECURSE->activate( d );
    } );
    CAPTURE( messages );
    CHECK( global_number( "audit_b14_depth" ) > 100 );
    // Stopped once, not at every level on the way back out
    const std::string::size_type first = messages.find( "EOC EOC_AUDIT_B14_RECURSE is nested" );
    REQUIRE( first != std::string::npos );
    CHECK( messages.find( "is nested", messages.find( "is nested" ) + 1 ) == std::string::npos );

    // The next chain starts afresh
    globals.remove_global_value( "audit_b14_iterations" );
    on_out_of_scope forget_iterations( [&globals]() {
        globals.remove_global_value( "audit_b14_iterations" );
    } );
    CHECK( capture_debugmsg_during( [&]() {
        effect_on_condition_EOC_AUDIT_B14_LOOP->activate( d );
    } ).empty() );
    CHECK( global_number( "audit_b14_iterations" ) == 300 );
}

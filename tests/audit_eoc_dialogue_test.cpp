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
#include <tuple>
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
#include "dialogue_imgui.h"
#include "effect_on_condition.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "line.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "mapbuffer.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "omdata.h"
#include "overmap.h"
#include "overmap_test_helper.h"
#include "overmapbuffer.h"
#include "pickup.h"
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
effect_on_condition_EOC_AUDIT_B14_OUTER( "EOC_AUDIT_B14_OUTER" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_B14_RECURSE( "EOC_AUDIT_B14_RECURSE" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_B15_LATER( "EOC_AUDIT_B15_LATER" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_B15_RECURRING( "EOC_AUDIT_B15_RECURRING" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_B15_TAKE_CONTROL( "EOC_AUDIT_B15_TAKE_CONTROL" );
static const effect_on_condition_id effect_on_condition_EOC_HAUNTING_SPAWN( "EOC_HAUNTING_SPAWN" );

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_test_backpack( "test_backpack" );
static const itype_id itype_test_rock( "test_rock" );
static const itype_id itype_test_sheet_metal_small( "test_sheet_metal_small" );

static const mtype_id mon_XEDRA_officer( "mon_XEDRA_officer" );

static const oter_str_id oter_field( "field" );

static const overmap_special_id overmap_special_test_audit_b08_shelter( "test_audit_b08_shelter" );

static const ter_str_id ter_t_rock( "t_rock" );
static const ter_str_id ter_t_thconc_floor( "t_thconc_floor" );

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
        const overmap_with_local_coords placed = overmap_buffer.get_om_global( shelter );
        overmap_test_helper::forget_special_at( *placed.om, placed.local );
        CHECK_FALSE( overmap_buffer.overmap_special_at( shelter ).has_value() );
        CHECK( overmap_buffer.mapgen_args( shelter ) == nullptr );
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

// EOC-B15: process_eocs held the avatar's queue and an iterator into it while a queued EOC ran;
// take_control swapped the queue with the NPC's, and the pass then erased and re-queued nodes of
// the old body's list through the new body's queue.
TEST_CASE( "eoc-b15_take_control_from_a_queued_eoc_keeps_both_queues_whole", "[audit][eoc]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    const eoc_queues_aside aside;
    g->faction_manager_ptr->create_if_needed();
    npc &guy = spawn_npc( u.pos_bub().xy() + point( 2, 0 ), "test_talker" );
    guy.set_fac( faction_your_followers );
    REQUIRE( guy.is_player_ally() );
    const character_id avatar_id = u.getID();
    const character_id npc_id = guy.getID();
    global_variables &globals = get_globals();
    globals.set_global_value( "audit_b15_npc", std::to_string( npc_id.get_value() ) );
    globals.set_global_value( "audit_b15_active", 1 );
    on_out_of_scope restore( [&]() {
        // Back to the avatar's own body, and the NPC goes away
        if( u.getID() != avatar_id ) {
            npc *old_body = g->find_npc( avatar_id );
            if( old_body != nullptr ) {
                u.control_npc( *old_body );
            }
        }
        g->remove_npc_follower( npc_id );
        overmap_buffer.remove_npc( npc_id );
        clear_npcs();
        for( const char *name : {
                 "audit_b15_npc", "audit_b15_active", "audit_b15_recurring", "audit_b15_later"
             } ) {
            globals.remove_global_value( name );
        }
    } );

    // A recurring EOC first, then the takeover, then one more due in the same pass
    u.queued_effect_on_conditions.push( queued_eoc{ effect_on_condition_EOC_AUDIT_B15_RECURRING, calendar::turn - 2_turns, {} } );
    u.queued_effect_on_conditions.push( queued_eoc{ effect_on_condition_EOC_AUDIT_B15_TAKE_CONTROL, calendar::turn - 1_turns, {} } );
    u.queued_effect_on_conditions.push( queued_eoc{ effect_on_condition_EOC_AUDIT_B15_LATER, calendar::turn, {} } );
    effect_on_conditions::process_effect_on_conditions( u );

    REQUIRE( u.getID() == npc_id );
    const npc *old_body = g->find_npc( avatar_id );
    REQUIRE( old_body != nullptr );
    const queued_eocs &old_queue = old_body->queued_effect_on_conditions;
    CHECK( queue_is_whole( old_queue ) );
    CHECK( queue_is_whole( u.queued_effect_on_conditions ) );
    // The old body keeps its recurring EOC and the one the pass didn't get to
    CHECK( global_number( "audit_b15_recurring" ) == 1 );
    CHECK( count_in( old_queue, effect_on_condition_EOC_AUDIT_B15_RECURRING ) == 1 );
    CHECK( count_in( old_queue, effect_on_condition_EOC_AUDIT_B15_TAKE_CONTROL ) == 0 );
    CHECK( count_in( old_queue, effect_on_condition_EOC_AUDIT_B15_LATER ) == 1 );
    CHECK( global_number( "audit_b15_later" ) == 0 );
    CHECK( count_in( u.queued_effect_on_conditions,
                     effect_on_condition_EOC_AUDIT_B15_RECURRING ) == 0 );
}

// EOC-B09 follow-up: once talk_to stopped loading the opening topics twice, a TALK_NONE response
// in the topic a conversation started with emptied the topic stack and ended the conversation;
// vanilla responses such as the farmer's "Let's trade items" rely on coming back to that topic.
TEST_CASE( "eoc-b09_talk_none_stays_on_the_first_topic", "[audit][npc_talk]" )
{
    const auto back_from = []( const std::vector<std::string> &ids ) {
        std::vector<talk_topic> topic_stack;
        for( const std::string &id : ids ) {
            topic_stack.emplace_back( id );
        }
        go_back_from_topic( topic_stack );
        std::vector<std::string> left;
        for( const talk_topic &topic : topic_stack ) {
            left.push_back( topic.id );
        }
        return left;
    };
    using topics = std::vector<std::string>;
    CHECK( back_from( { "TALK_AUDIT_FIRST" } ) == topics{ "TALK_AUDIT_FIRST" } );
    CHECK( back_from( { "TALK_AUDIT_FIRST", "TALK_AUDIT_SECOND" } ) == topics{ "TALK_AUDIT_FIRST" } );
    // The topics of one category go back together, but never past the first topic
    CHECK( back_from( { "TALK_AUDIT_FIRST", "TALK_MISSION_OFFER", "TALK_MISSION_DESCRIBE" } ) ==
           topics{ "TALK_AUDIT_FIRST" } );
    CHECK( back_from( { "TALK_MISSION_START", "TALK_MISSION_OFFER" } ) ==
           topics{ "TALK_MISSION_START" } );
}

// EOC-B13 follow-up: bulk_trade_accept and bulk_donate gave the items to the buyer's character
// without checking there was one, so donating to a monster crashed.
TEST_CASE( "eoc-b13_bulk_donate_to_a_talker_that_isnt_a_character", "[audit][eoc]" )
{
    clear_map();
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 3, 0 ) );
    on_out_of_scope restore( []() {
        clear_creatures();
    } );
    // A character that isn't the avatar gives, so nothing asks for confirmation
    standard_npc giver( "Giver", u.pos_bub() + point( -2, 0 ) );
    giver.set_wielded_item( item( itype_test_rock ) );
    REQUIRE( giver.has_amount( itype_test_rock, 1 ) );
    dialogue d( get_talker_for( giver ), get_talker_for( zed ) );
    d.cur_item = itype_test_rock;
    const talk_effect_t donate = load_effect( R"({ "effect": "u_bulk_donate" })" );
    CHECK_FALSE( capture_debugmsg_during( [&]() {
        donate.apply( d );
    } ).empty() );
    CHECK( giver.has_amount( itype_test_rock, 1 ) );
}

// EOC-B14 follow-up: the stop flag stayed up until no EOC at all was running, so after an endless
// chain started inside another EOC (or a dialogue an EOC opened), every later EOC there did nothing.
TEST_CASE( "eoc-b14_eocs_after_a_stopped_chain_still_run", "[audit][eoc]" )
{
    global_variables &globals = get_globals();
    on_out_of_scope forget( [&globals]() {
        globals.remove_global_value( "audit_b14_depth" );
        globals.remove_global_value( "audit_b14_after" );
    } );
    globals.remove_global_value( "audit_b14_after" );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const std::string messages = capture_debugmsg_during( [&]() {
        effect_on_condition_EOC_AUDIT_B14_OUTER->activate( d );
    } );
    CAPTURE( messages );
    const std::string::size_type first = messages.find( "is nested" );
    CHECK( first != std::string::npos );
    CHECK( messages.find( "is nested", first + 1 ) == std::string::npos );
    // The EOC that started the endless chain ran inside another, which goes on
    CHECK( global_number( "audit_b14_after" ) == 1 );
}

namespace
{
// Runs @p eoc on the avatar; true when it logged nothing
bool runs_quietly( const effect_on_condition_id &eoc )
{
    REQUIRE( eoc.is_valid() );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const std::string messages = capture_debugmsg_during( [&]() {
        eoc->activate( d );
    } );
    CAPTURE( eoc.str(), messages );
    return messages.empty();
}
} // namespace

// EOC-B14 follow-up: Xedra Evolved's paraclesian spell-experience selectors picked any spell by
// weight and ran themselves again whenever the pick was unknown or at its cap.  Once every known
// spell was at its cap they never stopped, and before that a run could still go 250 deep.  They
// now weigh such spells 0.  Needs the mod: run with --mods=xedra_evolved "[xedra_evolved]".  Its only
// tags are the mods', so "[eoc]" or "[audit]" runs without the mods don't select it.
TEST_CASE( "eoc-b14_xedra_spell_selectors_dont_run_themselves_again", "[.][xedra_evolved]" )
{
    clear_avatar();
    on_out_of_scope restore( []() {
        clear_avatar();
    } );
    avatar &u = get_avatar();
    // Each selector, with one of its spells and that spell's cap
    const std::vector<std::tuple<std::string, std::string, int>> selectors = {
        { "ARVORE", "arvore_nurture_the_green", 15 },
        { "HOMULLUS", "homullus_summon_feral_spell", 35 },
        { "IERDE", "ierde_summon_earth_spirit", 35 },
        { "SALAMANDER", "salamander_make_fire", 15 },
        { "SYLPH", "ominous_wind", 20 },
        { "UNDINE", "grant_buoyancy", 20 },
    };
    for( const auto &[kind, spell_name, cap] : selectors ) {
        CAPTURE( kind );
        const effect_on_condition_id selector( "EOC_" + kind + "_SPELL_EXPERIENCE_INCREASER_SELECTOR" );
        const spell_id sp( spell_name );
        // No spell to improve: nothing happens
        CHECK( runs_quietly( selector ) );
        // One spell that can improve: it is picked every time
        u.magic->learn_spell( sp, u, true );
        const int xp_before = u.magic->get_spell( sp ).xp();
        CHECK( runs_quietly( selector ) );
        CHECK( u.magic->get_spell( sp ).xp() > xp_before );
        // At its cap, it can't
        u.magic->set_spell_level( sp, cap, &u );
        const int xp_at_cap = u.magic->get_spell( sp ).xp();
        CHECK( runs_quietly( selector ) );
        CHECK( u.magic->get_spell( sp ).xp() == xp_at_cap );
        u.magic->forget_spell( sp );
    }
}

// EOC-B14 follow-up: Magiclysm's and Mind Over Matter's item-count amendments took one item off
// per run and ran themselves again, once per item over the power available.  Needs the mods: run
// with --mods=magiclysm,mindovermatter "[mindovermatter]".
TEST_CASE( "eoc-b14_item_count_amendments_dont_run_themselves_again",
           "[.][magiclysm][mindovermatter]" )
{
    clear_avatar();
    global_variables &globals = get_globals();
    on_out_of_scope restore( [&globals]() {
        globals.remove_global_value( "audit_b14_items" );
        clear_avatar();
    } );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    const auto amended_count = [&]( const std::string & eoc, const std::string & power,
    const std::string & count ) {
        load_effect( R"({ "effect": [ { "math": [ ")" + power + R"( = 5.5" ] }, { "math": [ ")" + count +
                     R"( = 400" ] } ] })" ).apply( d );
        CHECK( runs_quietly( effect_on_condition_id( eoc ) ) );
        load_effect( R"({ "effect": { "math": [ "audit_b14_items = )" + count + R"(" ] } })" ).apply( d );
        return global_number( "audit_b14_items" );
    };
    CHECK( amended_count( "EOC_CONJURE_UPS_EFFECT_AMMEND_ITEM_COUNT",
                          "u_technomancer_conjure_ups_powering", "u_technomancer_conjure_ups_item_count" ) == 5 );
    CHECK( amended_count( "EOC_ELECTROKIN_PERSONAL_BATTERY_EFFECT_AMMEND_ITEM_COUNT",
                          "u_electrokin_personal_battery_power_count",
                          "u_electrokin_personal_battery_item_count" ) == 5 );
}

// EOC-B11 follow-up: once 200 ml had been picked up, Pickup::do_pickup reset its pick_info to
// stop bulk loading, and the limits u_pickup_items set (and the totals so far) went with it.
TEST_CASE( "eoc-b11_pickup_limits_hold_past_the_bulk_reset", "[audit][eoc]" )
{
    clear_map();
    clear_avatar();
    on_out_of_scope restore( []() {
        clear_map();
        clear_avatar();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    u.wear_item( item( itype_test_backpack ), false );
    // Three 250 ml, 250 g plates under the avatar, and room for two of them by weight
    const tripoint_bub_ms spot = u.pos_bub();
    std::vector<item_location> targets;
    std::vector<int> quantities;
    for( int i = 0; i < 3; ++i ) {
        item &plate = here.add_item( spot, item( itype_test_sheet_metal_small ) );
        targets.emplace_back( map_cursor( here.get_abs( spot ) ), &plate );
        quantities.push_back( 0 );
    }
    Pickup::pick_info info( 0, -1_ml, 600_gram );
    bool stash_successful = true;
    u.set_moves( 100000 );
    Pickup::do_pickup( targets, quantities, false, stash_successful, info );
    CHECK( u.amount_of( itype_test_sheet_metal_small ) == 2 );
    CHECK( here.i_at( spot ).size() == 1 );
}

// take_control's documented object form, { "take_control": { "true_eocs": ..., "false_eocs": ... } },
// read its lists from beside the object instead of inside it: they were ignored, and the object
// was reported as unread data.
TEST_CASE( "eoc_take_control_object_form_reads_its_lists", "[audit][eoc]" )
{
    clear_map();
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 3, 0 ) );
    global_variables &globals = get_globals();
    on_out_of_scope restore( [&globals]() {
        globals.remove_global_value( "audit_b13_false_eocs" );
        clear_creatures();
    } );
    globals.remove_global_value( "audit_b13_false_eocs" );
    const std::string messages = capture_debugmsg_during( [&]() {
        const talk_effect_t take_control = load_effect(
                                               R"({ "effect": { "take_control": { "false_eocs": [ "EOC_AUDIT_B13_MARK" ] } } })" );
        // Only the avatar takes control, so a monster gets the false_eocs
        dialogue d( get_talker_for( zed ), get_talker_for( u ) );
        take_control.apply( d );
    } );
    CHECK( messages.empty() );
    CHECK( global_number( "audit_b13_false_eocs" ) == 1 );
}

namespace
{
// Walls in every tile within @p radius of the avatar but its own, so nothing has room there
void wall_in_avatar( int radius )
{
    map &here = get_map();
    const tripoint_bub_ms centre = get_avatar().pos_bub();
    for( const tripoint_bub_ms &p : here.points_in_radius( centre, radius ) ) {
        if( p != centre ) {
            here.ter_set( p, ter_t_rock );
        }
    }
}

// Roofs every tile within @p radius of the avatar, or only the avatar's own tile
void roof_around_avatar( int radius )
{
    map &here = get_map();
    const tripoint_bub_ms centre = get_avatar().pos_bub();
    for( const tripoint_bub_ms &p : here.points_in_radius( centre, radius ) ) {
        here.ter_set( p, ter_t_thconc_floor );
    }
    here.set_outside_cache_dirty( centre.z() );
    here.build_outside_cache( centre.z() );
}
} // namespace

// The officer of the Xedra haunting looked for a spawn spot by running itself again until one
// worked; underground, where the haunting starts, most spots are solid rock.  It now tries up to
// 100 spots.
TEST_CASE( "eoc_haunting_spawn_gives_up_without_a_spot", "[audit][eoc]" )
{
    clear_map();
    clear_avatar();
    global_variables &globals = get_globals();
    on_out_of_scope restore( [&globals]() {
        globals.remove_global_value( "MERC_LOC" );
        clear_creatures();
        clear_map();
        clear_avatar();
    } );
    const auto officers = []() {
        int count = 0;
        for( const monster &critter : g->all_monsters() ) {
            count += critter.type->id == mon_XEDRA_officer ? 1 : 0;
        }
        return count;
    };
    SECTION( "nowhere to spawn: it stops quietly" ) {
        wall_in_avatar( 50 );
        CHECK( runs_quietly( effect_on_condition_EOC_HAUNTING_SPAWN ) );
        CHECK( officers() == 0 );
    }
    SECTION( "room to spawn: the officer comes" ) {
        CHECK( runs_quietly( effect_on_condition_EOC_HAUNTING_SPAWN ) );
        CHECK( officers() == 1 );
    }
    CHECK_FALSE( get_avatar().maybe_get_value( "haunting_spawn_retry" ) );
}

// Xedra Evolved's brownie blink ran itself again whenever the spot it picked was outdoors, so in a
// small building surrounded by open ground it never stopped.  It now tries up to 100 spots.  Needs
// the mod: run with --mods=xedra_evolved "[xedra_evolved]".
TEST_CASE( "eoc_brownie_blink_gives_up_without_an_indoor_spot", "[.][xedra_evolved]" )
{
    clear_map();
    clear_avatar();
    on_out_of_scope restore( []() {
        clear_map();
        clear_avatar();
    } );
    avatar &u = get_avatar();
    const effect_on_condition_id blink( "EOC_BROWNIE_INDOOR_BLINK" );
    const tripoint_bub_ms start = u.pos_bub();
    SECTION( "indoors with only open ground around: it stays put" ) {
        roof_around_avatar( 0 );
        REQUIRE_FALSE( get_map().is_outside( start ) );
        CHECK( runs_quietly( blink ) );
        CHECK( u.pos_bub() == start );
    }
    SECTION( "indoors everywhere: it slips 5 to 10 tiles away" ) {
        roof_around_avatar( 12 );
        CHECK( runs_quietly( blink ) );
        CHECK( rl_dist( u.pos_bub(), start ) >= 5 );
        CHECK( square_dist( u.pos_bub(), start ) <= 10 );
    }
    CHECK_FALSE( u.maybe_get_value( "brownie_blink_retry" ) );
}

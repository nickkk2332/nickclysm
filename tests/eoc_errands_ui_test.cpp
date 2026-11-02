#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "eoc_ui.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

// u_examine, u_show_text and run_eoc_checklist.

static const activity_id ACT_CLOSE_TILE( "ACT_CLOSE_TILE" );

static const furn_str_id furn_f_safe_c( "f_safe_c" );
static const furn_str_id furn_test_f_dead_gate_control( "test_f_dead_gate_control" );
static const furn_str_id furn_f_safe_o( "f_safe_o" );
static const furn_str_id furn_f_workbench( "f_workbench" );
static const furn_str_id furn_test_f_eoc( "test_f_eoc" );

static const ter_str_id ter_t_door_c( "t_door_c" );
static const ter_str_id ter_t_door_o( "t_door_o" );

namespace
{

talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "eoc_errands_ui_test" );
    finalize_conditions();
    return ret;
}

std::string global_string( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

double global_number( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? 0.0 : value->dbl();
}

npc &setup()
{
    clear_map();
    clear_avatar();
    get_globals().clear_global_values();
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
    clear_character( guy );
    return guy;
}

void teardown()
{
    get_globals().clear_global_values();
    eoc_ui::show_text_test_answer = nullptr;
    eoc_ui::checklist_test_answer = nullptr;
    clear_map();
    clear_avatar();
}

// `who` examines `pos`, with the test's true_eocs and false_eocs.
void examine( Character &who, const tripoint_bub_ms &pos )
{
    get_globals().clear_global_values();
    dialogue d( get_talker_for( who ), nullptr );
    d.set_value( "spot", get_map().get_abs( pos ) );
    load_effect( R"({ "u_examine": { "context_val": "spot" }, )"
                 R"("true_eocs": [ "EOC_TEST_ERRAND_STARTED" ], )"
                 R"("false_eocs": [ "EOC_TEST_ERRAND_REFUSED" ] })" ).apply( d );
}

std::string result()
{
    return global_string( "test_errand_result" );
}

std::string reason()
{
    return global_string( "test_errand_reason" );
}

} // namespace

TEST_CASE( "eoc_examine_by_an_npc", "[eoc][iexamine][errands_ui]" )
{
    npc &guy = setup();
    map &here = get_map();
    const tripoint_bub_ms spot = guy.pos_bub() + tripoint_rel_ms( 1, 0, 0 );

    SECTION( "furniture whose examine action is an EOC" ) {
        here.furn_set( spot, furn_test_f_eoc );
        examine( guy, spot );
        CHECK( result() == "started" );
        // EOC_test_f_eoc copies its context into globals.
        CHECK( global_string( "this" ) == "test_f_eoc" );
    }
    SECTION( "a door opens, then closes" ) {
        here.ter_set( spot, ter_t_door_c );
        examine( guy, spot );
        CHECK( result() == "started" );
        CHECK( here.ter( spot ) == ter_t_door_o );
        examine( guy, spot );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_CLOSE_TILE );
        int guard = 0;
        while( guy.activity.id() == ACT_CLOSE_TILE && ++guard < 1000 ) {
            guy.set_moves( 100 );
            guy.activity.do_turn( guy );
        }
        CHECK( here.ter( spot ) == ter_t_door_c );
    }
    SECTION( "furniture that opens opens like a door: an unlocked safe" ) {
        here.furn_set( spot, furn_f_safe_c );
        examine( guy, spot );
        CHECK( result() == "started" );
        CHECK( here.furn( spot ) == furn_f_safe_o );
    }
    SECTION( "an allowed action that does nothing runs false_eocs" ) {
        // controls_gate with no gate data: "Nothing happens."
        here.furn_set( spot, furn_test_f_dead_gate_control );
        examine( guy, spot );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "did nothing" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "a door right below is out of reach: not through the floor" ) {
        const tripoint_bub_ms below = guy.pos_bub() + tripoint_rel_ms( 0, 0, -1 );
        here.ter_set( below, ter_t_door_c );
        examine( guy, below );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "out of reach" ) );
        CHECK( here.ter( below ) == ter_t_door_c );
    }
    SECTION( "a built-in action with a menu is refused, and no menu opens" ) {
        here.furn_set( spot, furn_f_workbench );
        const std::string msg = capture_debugmsg_during( [&]() {
            examine( guy, spot );
        } );
        CHECK( msg.empty() );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "menu" ) );
        CHECK( here.furn( spot ) == furn_f_workbench );
    }
    SECTION( "nothing to examine" ) {
        examine( guy, spot );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "nothing to do" ) );
    }
    SECTION( "a tile out of reach" ) {
        const tripoint_bub_ms far = guy.pos_bub() + tripoint_rel_ms( 3, 0, 0 );
        here.furn_set( far, furn_test_f_eoc );
        examine( guy, far );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "out of reach" ) );
        CHECK( global_string( "this" ).empty() );
    }
    teardown();
}

TEST_CASE( "eoc_examine_by_the_avatar", "[eoc][iexamine][errands_ui]" )
{
    setup();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms( 0, 1, 0 );

    SECTION( "the normal examine runs" ) {
        here.furn_set( spot, furn_test_f_eoc );
        examine( u, spot );
        CHECK( result() == "started" );
        CHECK( global_string( "this" ) == "test_f_eoc" );
    }
    SECTION( "a door opens" ) {
        here.ter_set( spot, ter_t_door_c );
        examine( u, spot );
        CHECK( result() == "started" );
        CHECK( here.ter( spot ) == ter_t_door_o );
    }
    SECTION( "nothing to examine runs false_eocs" ) {
        examine( u, spot );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "nothing to do" ) );
    }
    SECTION( "the other talker's tile" ) {
        npc &guy = spawn_npc( spot.xy(), "test_talker" );
        here.furn_set( spot, furn_test_f_eoc );
        dialogue d( get_talker_for( u ), get_talker_for( guy ) );
        load_effect( R"({ "u_examine": "npc" })" ).apply( d );
        CHECK( global_string( "this" ) == "test_f_eoc" );
    }
    CHECK_THROWS_AS( load_effect( R"({ "u_examine": "u" })" ), JsonError );
    teardown();
}

TEST_CASE( "eoc_show_text", "[eoc][ui][errands_ui]" )
{
    npc &guy = setup();
    avatar &u = get_avatar();
    get_globals().set_global_value( "test_ui_text", std::string( "Day 3: <u_name> found a pencil." ) );
    const std::string effect =
        R"({ "u_show_text": { "title": "Journal of <u_name>", "text": { "global_val": "test_ui_text" }, )"
        R"("buttons": [ { "name": "Keep", "eocs": [ "EOC_TEST_UI_BUTTON_KEEP" ] }, )"
        R"({ "name": "Tear out the page", "eocs": [ "EOC_TEST_UI_BUTTON_TEAR" ], )"
        R"("condition": { "compare_string": [ "yes", { "global_val": "test_ui_allow_tools" } ] } } ] }, )"
        R"("false_eocs": [ "EOC_TEST_UI_NOT_SHOWN" ] })";
    std::string shown_title;
    std::string shown_text;
    std::vector<eoc_ui::text_button> shown_buttons;
    std::optional<size_t> answer;
    bool asked = false;
    eoc_ui::show_text_test_answer = [&]( const std::string & title, const std::string & text,
    const std::vector<eoc_ui::text_button> &buttons ) {
        asked = true;
        shown_title = title;
        shown_text = text;
        shown_buttons = buttons;
        return answer;
    };
    const auto run = [&]( const std::string & json, Character & alpha ) {
        dialogue d( get_talker_for( alpha ), get_talker_for( guy ) );
        load_effect( json ).apply( d );
    };

    SECTION( "the avatar sees the window, tags filled in, and a button runs its EOCs" ) {
        answer = 0;
        run( effect, u );
        REQUIRE( asked );
        CHECK( shown_title == "Journal of " + u.get_name() );
        CHECK( shown_text == "Day 3: " + u.get_name() + " found a pencil." );
        REQUIRE( shown_buttons.size() == 2 );
        CHECK( shown_buttons[0].name == "Keep" );
        CHECK( shown_buttons[0].enabled );
        // Its condition fails: greyed out.
        CHECK_FALSE( shown_buttons[1].enabled );
        CHECK( global_string( "test_ui_result" ) == "keep" );
    }
    SECTION( "a greyed-out button can't be pressed" ) {
        answer = 1;
        run( effect, u );
        CHECK( global_string( "test_ui_result" ).empty() );
    }
    SECTION( "once its condition holds, it can" ) {
        get_globals().set_global_value( "test_ui_allow_tools", std::string( "yes" ) );
        answer = 1;
        run( effect, u );
        CHECK( shown_buttons[1].enabled );
        CHECK( global_string( "test_ui_result" ) == "tear" );
    }
    SECTION( "closing the window runs nothing" ) {
        answer = std::nullopt;
        run( effect, u );
        CHECK( asked );
        CHECK( global_string( "test_ui_result" ).empty() );
    }
    SECTION( "hide_failing hides the button instead" ) {
        answer = 0;
        run( R"({ "u_show_text": { "text": "A note.", "hide_failing": true, )"
             R"("buttons": [ { "name": "Tear", "eocs": [ "EOC_TEST_UI_BUTTON_TEAR" ], )"
             R"("condition": { "compare_string": [ "yes", { "global_val": "test_ui_allow_tools" } ] } }, )"
             R"({ "name": "Keep", "eocs": [ "EOC_TEST_UI_BUTTON_KEEP" ] } ] } })", u );
        REQUIRE( shown_buttons.size() == 1 );
        CHECK( shown_buttons[0].name == "Keep" );
        CHECK( global_string( "test_ui_result" ) == "keep" );
    }
    SECTION( "text from a snippet" ) {
        run( R"({ "u_show_text": { "text": "test_eoc_show_text", "snippet": true } })", u );
        CHECK( shown_text == "A page about " + u.get_name() + "." );
        CHECK( shown_buttons.empty() );
    }
    SECTION( "an NPC sees nothing, and false_eocs run" ) {
        answer = 0;
        run( effect, guy );
        CHECK_FALSE( asked );
        CHECK( global_string( "test_ui_result" ) == "not shown" );
    }
    teardown();
}

TEST_CASE( "eoc_run_eoc_checklist", "[eoc][ui][errands_ui]" )
{
    npc &guy = setup();
    avatar &u = get_avatar();
    const std::string effect =
        R"({ "run_eoc_checklist": [ "EOC_TEST_UI_PACK_FOOD", "EOC_TEST_UI_PACK_WATER", "EOC_TEST_UI_PACK_TOOLS" ], )"
        R"("names": [ "Food", "Water", "Tools" ], "descriptions": [ "Two days", "Two liters", "A crowbar" ], )"
        R"("checked": [ true, true, false ], "min": 1, "max": 2, "title": "What does <u_name> bring?", )"
        R"("false_eocs": [ "EOC_TEST_UI_NOT_SHOWN" ] })";
    std::string shown_title;
    std::vector<eoc_ui::checklist_option> shown;
    int shown_min = -1;
    int shown_max = -1;
    std::optional<std::vector<size_t>> answer;
    bool asked = false;
    eoc_ui::checklist_test_answer = [&]( const std::string & title,
    const std::vector<eoc_ui::checklist_option> &options, int min, int max ) {
        asked = true;
        shown_title = title;
        shown = options;
        shown_min = min;
        shown_max = max;
        return answer;
    };
    const auto run = [&]( const std::string & json, Character & alpha ) {
        dialogue d( get_talker_for( alpha ), get_talker_for( guy ) );
        load_effect( json ).apply( d );
    };

    SECTION( "the ticked options run their EOCs" ) {
        answer = std::vector<size_t> { 0, 1 };
        run( effect, u );
        REQUIRE( asked );
        CHECK( shown_title == "What does " + u.get_name() + " bring?" );
        REQUIRE( shown.size() == 3 );
        CHECK( shown[0].name == "Food" );
        CHECK( shown[1].description == "Two liters" );
        CHECK( shown[0].checked );
        CHECK( shown[1].checked );
        CHECK_FALSE( shown[2].checked );
        // EOC_TEST_UI_PACK_TOOLS's condition fails: greyed out.
        CHECK_FALSE( shown[2].enabled );
        CHECK( shown_min == 1 );
        CHECK( shown_max == 2 );
        CHECK( global_number( "test_ui_food" ) == 1 );
        CHECK( global_number( "test_ui_water" ) == 1 );
        CHECK( global_number( "test_ui_tools" ) == 0 );
    }
    SECTION( "cancelling runs nothing" ) {
        answer = std::nullopt;
        run( effect, u );
        CHECK( asked );
        CHECK( global_number( "test_ui_food" ) == 0 );
        CHECK( global_number( "test_ui_water" ) == 0 );
    }
    SECTION( "a greyed-out option can't be ticked" ) {
        answer = std::vector<size_t> { 2 };
        run( effect, u );
        CHECK( global_number( "test_ui_tools" ) == 0 );
    }
    SECTION( "too many or too few can't be confirmed" ) {
        get_globals().set_global_value( "test_ui_allow_tools", std::string( "yes" ) );
        answer = std::vector<size_t> { 0, 1, 2 };
        run( effect, u );
        CHECK( shown[2].enabled );
        answer = std::vector<size_t> {};
        run( effect, u );
        CHECK( global_number( "test_ui_food" ) == 0 );
        CHECK( global_number( "test_ui_water" ) == 0 );
        CHECK( global_number( "test_ui_tools" ) == 0 );
        answer = std::vector<size_t> { 2 };
        run( effect, u );
        CHECK( global_number( "test_ui_tools" ) == 1 );
    }
    SECTION( "hide_failing hides the option, and variables reach the EOC" ) {
        answer = std::vector<size_t> { 1 };
        run( R"({ "run_eoc_checklist": [ "EOC_TEST_UI_PACK_TOOLS", "EOC_TEST_UI_PACK_VARIABLE", )"
             R"("EOC_TEST_UI_PACK_VARIABLE" ], "names": [ "Tools", "Food", "Water" ], )"
             R"("variables": [ { "what": "tools" }, { "what": "food" }, { "what": "water" } ], )"
             R"("hide_failing": true })", u );
        REQUIRE( shown.size() == 2 );
        CHECK( shown[0].name == "Food" );
        CHECK( shown[1].name == "Water" );
        CHECK( global_string( "test_ui_packed" ) == "water" );
        CHECK( global_number( "test_ui_packed_count" ) == 1 );
    }
    SECTION( "with an NPC as u, nothing is shown and false_eocs run" ) {
        answer = std::vector<size_t> { 0 };
        run( effect, guy );
        CHECK_FALSE( asked );
        CHECK( global_string( "test_ui_result" ) == "not shown" );
        CHECK( global_number( "test_ui_food" ) == 0 );
    }
    SECTION( "options are checked at load" ) {
        CHECK_THROWS_AS( load_effect( R"({ "run_eoc_checklist": [ "EOC_TEST_UI_PACK_FOOD" ], )"
                                      R"("names": [ "Food", "Water" ] })" ), JsonError );
        CHECK_THROWS_AS( load_effect( R"({ "run_eoc_checklist": [ "EOC_TEST_UI_PACK_FOOD" ], )"
                                      R"("checked": [ true, false ] })" ), JsonError );
        CHECK_THROWS_AS( load_effect( R"({ "run_eoc_checklist": [ "EOC_TEST_UI_PACK_FOOD" ], )"
                                      R"("max": 2 })" ), JsonError );
        CHECK_THROWS_AS( load_effect( R"({ "run_eoc_checklist": [ "EOC_TEST_UI_PACK_FOOD", )"
                                      R"("EOC_TEST_UI_PACK_WATER" ], "min": 2, "max": 1 })" ), JsonError );
        CHECK_THROWS_AS( load_effect( R"({ "run_eoc_checklist": [ "EOC_TEST_UI_PACK_FOOD" ], )"
                                      R"("keys": [ "ab" ] })" ), JsonError );
        CHECK_NOTHROW( load_effect( effect ) );
    }
    teardown();
}

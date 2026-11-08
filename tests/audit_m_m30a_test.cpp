#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "color.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "display.h"
#include "flexbuffer_json.h"
#include "input.h"
#include "input_enums.h"
#include "json.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "panels.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "widget.h"

static const oter_str_id oter_field( "field" );
static const oter_str_id oter_forest( "forest" );

static const trait_id trait_AUDIT_M30A_NO_CARRY( "AUDIT_M30A_NO_CARRY" );

static const widget_id widget_audit_m30a_clause_copy( "audit_m30a_clause_copy" );
static const widget_id widget_audit_m30a_clause_template( "audit_m30a_clause_template" );
static const widget_id widget_audit_m30a_no_such_widget( "audit_m30a_no_such_widget" );
static const widget_id widget_audit_m30a_other_sidebar( "audit_m30a_other_sidebar" );
static const widget_id widget_audit_m30a_row_a( "audit_m30a_row_a" );
static const widget_id widget_audit_m30a_row_b( "audit_m30a_row_b" );
static const widget_id widget_audit_m30a_section_1( "audit_m30a_section_1" );
static const widget_id widget_audit_m30a_section_2( "audit_m30a_section_2" );
static const widget_id widget_audit_m30a_section_end( "audit_m30a_section_end" );
static const widget_id widget_audit_m30a_sidebar( "audit_m30a_sidebar" );
static const widget_id widget_legacy_labels_sidebar( "legacy_labels_sidebar" );

namespace
{

// A widget loaded from JSON outside the widget list (no id or type members)
widget load_test_widget( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    widget w;
    w.load( jo, "audit_m_m30a_test" );
    // Math in the clause conditions is parsed after loading
    finalize_conditions();
    return w;
}

talk_effect_t load_test_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    talk_effect_t effect( jo, "effect", "audit_m_m30a_test" );
    finalize_conditions();
    return effect;
}

conditional_t::func load_test_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

std::string check_messages( const widget &w, bool drawn )
{
    return capture_debugmsg_during( [&w, drawn]() {
        w.check( drawn );
    } );
}

bool has_text( const std::string_view haystack, const std::string_view needle )
{
    return haystack.find( needle ) != std::string_view::npos;
}

} // namespace

// UIHUD-B04: the carry_weight widget var divided by weight_capacity(), which stacked negative
// CARRY_WEIGHT enchantments bring down to 0 (a crash); the text version already said 100%.
TEST_CASE( "uihud-b04_carry_weight_with_no_capacity_reads_full", "[audit][ui][widget]" )
{
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    avatar &u = get_avatar();
    widget carry;
    carry._var = widget_var::carry_weight;

    REQUIRE( u.weight_capacity() > 0_gram );
    CHECK( u.weight_carried_percent() == ( 100 * u.weight_carried() ) / u.weight_capacity() );
    CHECK( carry.get_var_value( u ) == u.weight_carried_percent() );

    u.set_mutation( trait_AUDIT_M30A_NO_CARRY );
    u.recalculate_enchantment_cache();
    REQUIRE( u.weight_capacity() == 0_gram );
    CHECK( u.weight_carried_percent() == 100 );
    CHECK( carry.get_var_value( u ) == 100 );
    CHECK( display::carry_weight_text_color( u ).first == "100%" );
}

// UIHUD-B05: a graph with one symbol (the default) divided by a depth of 0, and a pool graph
// with no width divided by 0.
TEST_CASE( "uihud-b05_degenerate_graphs_draw_without_dividing_by_zero", "[audit][ui][widget]" )
{
    widget one_symbol;
    one_symbol._style = "graph";
    one_symbol._symbols = "-";
    one_symbol._width = 4;
    one_symbol._var_max = 10;
    one_symbol._fill = "bucket";
    CHECK( one_symbol.graph( 5 ) == "----" );
    one_symbol._fill = "pool";
    CHECK( one_symbol.graph( 5 ) == "----" );

    widget no_width;
    no_width._style = "graph";
    no_width._symbols = "-=#";
    no_width._width = 0;
    no_width._var_max = 10;
    no_width._fill = "pool";
    CHECK( no_width.graph( 5 ).empty() );
    no_width._fill = "bucket";
    CHECK( no_width.graph( 5 ).empty() );

    // An ordinary graph draws as before
    widget normal;
    normal._style = "graph";
    normal._symbols = "-=#";
    normal._width = 2;
    normal._var_max = 4;
    normal._fill = "bucket";
    CHECK( normal.graph( 0 ) == "--" );
    CHECK( normal.graph( 3 ) == "#=" );
    CHECK( normal.graph( 4 ) == "##" );
    normal._fill = "pool";
    CHECK( normal.graph( 3 ) == "#=" );
}

// UIHUD-B06: the sidebar's overmap was cached by position, mission and width only, so hordes,
// notes and newly seen tiles went stale until the player left the overmap tile.
TEST_CASE( "uihud-b06_sidebar_overmap_cache_is_rebuilt_each_turn", "[audit][ui][overmap]" )
{
    SECTION( "the cache key has the turn and the height" ) {
        disp_overmap_cache cache;
        const tripoint_abs_omt center( 1, 2, 0 );
        const tripoint_abs_omt mission = tripoint_abs_omt::invalid;
        const time_point turn = calendar::turn_zero + 1_hours;
        cache.rebuild( center, mission, 5, 3, turn, "x" );
        CHECK( cache.is_valid_for( center, mission, 5, 3, turn ) );
        CHECK_FALSE( cache.is_valid_for( center, mission, 5, 3, turn + 1_turns ) );
        CHECK_FALSE( cache.is_valid_for( center, mission, 5, 4, turn ) );
        CHECK_FALSE( cache.is_valid_for( center, mission, 6, 3, turn ) );
        cache.invalidate();
        CHECK_FALSE( cache.is_valid_for( center, mission, 5, 3, turn ) );
    }

    SECTION( "a terrain change shows on the next turn" ) {
        clear_avatar();
        clear_map_without_vision();
        const time_point start = calendar::turn;
        avatar &u = get_avatar();
        const tripoint_abs_omt center = u.pos_abs_omt();
        const tripoint_abs_omt north = center + point::north;
        const oter_id old_ter = overmap_buffer.ter( north );
        const om_vision_level old_seen = overmap_buffer.seen( north );
        const on_out_of_scope restore( [&]() {
            calendar::turn = start;
            overmap_buffer.ter_set( north, old_ter );
            overmap_buffer.set_seen( north, old_seen );
            display::invalidate_overmap_cache();
        } );
        overmap_buffer.ter_set( north, oter_field.id() );
        overmap_buffer.set_seen( north, om_vision_level::full );
        display::invalidate_overmap_cache();
        const std::string before = display::colorized_overmap_text( u, 3, 3 );

        overmap_buffer.ter_set( north, oter_forest.id() );
        // Still cached within the turn
        CHECK( display::colorized_overmap_text( u, 3, 3 ) == before );
        calendar::turn += 1_turns;
        CHECK( display::colorized_overmap_text( u, 3, 3 ) != before );
    }
}

// UIHUD-B09: a saved layout this world doesn't have set the layout id "labels" (no such id)
// and skipped every other layout's settings, which the next save then dropped.
TEST_CASE( "uihud-b09_panel_options_keep_layouts_the_world_lacks", "[audit][ui][panels]" )
{
    const std::string saved = R"([ { "current_layout_id": "audit_m30a_missing_layout",
        "layouts": [ { "layout_id": "audit_m30a_missing_layout",
                       "panels": [ { "name": "Audit stats", "toggle": false } ] } ] } ])";
    panel_manager manager;
    const JsonValue jv = json_loader::from_string( saved );
    const JsonArray ja = jv;
    manager.deserialize( ja );
    CHECK( manager.get_current_layout_id() != "labels" );
    CHECK( manager.get_current_layout_id() != "audit_m30a_missing_layout" );
#if !defined(__ANDROID__)
    CHECK( manager.get_current_layout_id() == "legacy_labels_sidebar" );
#endif

    std::ostringstream out;
    JsonOut jsout( out );
    manager.serialize( jsout );
    const std::string written = out.str();
    CHECK( has_text( written, "audit_m30a_missing_layout" ) );
    CHECK( has_text( written, "Audit stats" ) );
}

// UIHUD-B10: saving the keybindings wrote every default binding into the user's file, where
// it replaced the defaults for good.
TEST_CASE( "uihud-b10_keybinding_save_writes_only_changes", "[audit][ui][input]" )
{
    using contexts = std::map<std::string, std::map<std::string, action_attributes>>;
    action_attributes key_a;
    key_a.input_events = { input_event( 'a', input_event_t::keyboard_char ) };
    action_attributes key_b;
    key_b.input_events = { input_event( 'b', input_event_t::keyboard_char ) };
    const action_attributes no_keys;

    contexts defaults;
    defaults["AUDIT_M30A"]["kept"] = key_a;
    defaults["AUDIT_M30A"]["rebound"] = key_a;
    defaults["AUDIT_M30A"]["unbound"] = key_a;
    contexts current = defaults;
    current["AUDIT_M30A"]["rebound"] = key_b;
    current["AUDIT_M30A"]["unbound"] = no_keys;
    current["AUDIT_M30A"]["added_locally"] = key_b;

    std::ostringstream out;
    JsonOut jsout( out );
    inp_mngr.serialize_changed_bindings( jsout, current, defaults );
    const JsonValue jv = json_loader::from_string( out.str() );
    const JsonArray ja = jv;
    std::set<std::string> written;
    for( JsonObject jo : ja ) {
        jo.allow_omitted_members();
        written.insert( jo.get_string( "id" ) );
    }
    const std::set<std::string> changed = { "added_locally", "rebound", "unbound" };
    CHECK( written == changed );
}

// UIHUD-J01: widgets had no load check; typos drew "???", fell back silently or crashed.
TEST_CASE( "uihud-j01_widget_load_check_reports_bad_widgets", "[audit][ui][widget]" )
{
    // Loaded, so that every member is set before the copies below
    const widget good = load_test_widget( R"({ "style": "text", "string": "fine" })" );
    REQUIRE( good._fill == "bucket" );
    REQUIRE( good._arrange == "columns" );
    REQUIRE( good._symbols == "-" );
    CHECK( check_messages( good, true ).empty() );

    widget bad_style = good;
    bad_style._style = "txet";
    CHECK( has_text( check_messages( bad_style, false ), "unknown style" ) );

    widget bad_fill = good;
    bad_fill._fill = "pol";
    CHECK( has_text( check_messages( bad_fill, false ), "unknown fill" ) );

    widget bad_arrange = good;
    bad_arrange._arrange = "row";
    CHECK( has_text( check_messages( bad_arrange, false ), "unknown arrange" ) );

    widget bad_mode = good;
    bad_mode._clause_mode = "frist";
    CHECK( has_text( check_messages( bad_mode, false ), "unknown clause_mode" ) );

    widget one_symbol_graph = good;
    one_symbol_graph._style = "graph";
    one_symbol_graph._width = 5;
    CHECK( has_text( check_messages( one_symbol_graph, false ), "fewer than 2 symbols" ) );

    widget breaks = good;
    breaks._colors = { c_red, c_green };
    breaks._breaks = { 10, 20 };
    CHECK( has_text( check_messages( breaks, false ), "breaks" ) );
    breaks._colors = { c_red, c_green, c_blue };
    breaks._breaks = { 50, 10 };
    CHECK( has_text( check_messages( breaks, false ), "ascending" ) );

    widget bad_graph = good;
    bad_graph._var = widget_var::body_graph;
    bad_graph._body_graph = "audit_m30a_no_such_body_graph";
    CHECK( has_text( check_messages( bad_graph, false ), "unknown body_graph" ) );

    // Only widgets a sidebar draws need their parts: others can be templates
    widget no_width = good;
    no_width._style = "graph";
    no_width._symbols = "-=";
    no_width._width = 0;
    CHECK( check_messages( no_width, false ).empty() );
    CHECK( has_text( check_messages( no_width, true ), "no width" ) );

    widget missing_child = good;
    missing_child._style = "layout";
    missing_child._widgets = { widget_audit_m30a_no_such_widget };
    CHECK( check_messages( missing_child, false ).empty() );
    CHECK( has_text( check_messages( missing_child, true ), "unknown widget" ) );

    widget template_child = good;
    template_child._style = "layout";
    template_child._widgets = { widget_audit_m30a_clause_template };
    CHECK( has_text( check_messages( template_child, true ), "clause template" ) );
}

// UIHUD-J02: tags were expanded only in the text of a clause that applies, without the
// "widget" context var, and numbers came out with six digits.
TEST_CASE( "uihud-j02_widget_text_tags", "[audit][ui][widget]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    u.set_value( "audit_m30a_big", 1234567 );
    u.set_value( "audit_m30a_frac", 1.5 );
    u.set_value( "audit_m30a_sym", "X" );

    SECTION( "default_clause" ) {
        widget w = load_test_widget( R"({ "style": "text",
            "clauses": [ { "text": "never", "condition": { "math": [ "0" ] } } ],
            "default_clause": { "text": "<u_val:audit_m30a_big> <u_val:audit_m30a_frac>", "parse_tags": true } })" );
        CHECK( w.value_string( 0 ) == "1234567 1.5" );
    }

    SECTION( "sym" ) {
        widget w = load_test_widget( R"({ "style": "symbol",
            "clauses": [ { "sym": "<u_val:audit_m30a_sym>", "parse_tags": true } ] })" );
        CHECK( w.value_string( 0 ) == "X" );
    }

    SECTION( "string" ) {
        widget w = load_test_widget( R"({ "style": "text", "string": "n=<u_val:audit_m30a_big>",
            "parse_tags": true })" );
        CHECK( w.value_string( 0 ) == "n=1234567" );
        widget plain = load_test_widget( R"({ "style": "text", "string": "n=<u_val:audit_m30a_big>" })" );
        CHECK( plain.value_string( 0 ) == "n=<u_val:audit_m30a_big>" );
    }

    SECTION( "the widget context var is the body part" ) {
        widget w = load_test_widget( R"({ "style": "text", "bodyparts": [ "torso" ],
            "clauses": [ { "text": "<context_val:widget>", "parse_tags": true } ] })" );
        CHECK( w.value_string( 0 ) == "torso" );
    }
}

// UIHUD-J03: a mod had to copy and extend each sidebar to add a section to it.
TEST_CASE( "uihud-j03_widgets_add_themselves_to_sidebars", "[audit][ui][widget]" )
{
    const std::vector<widget_id> sections = { widget_audit_m30a_row_a, widget_audit_m30a_row_b };

    std::vector<widget_id> rows = sections;
    widget::add_sidebar_sections( widget_audit_m30a_sidebar, rows );
    const std::vector<widget_id> expected = {
        widget_audit_m30a_row_a, widget_audit_m30a_section_1, widget_audit_m30a_section_2,
        widget_audit_m30a_row_b, widget_audit_m30a_section_end
    };
    CHECK( rows == expected );

    rows = sections;
    widget::add_sidebar_sections( widget_audit_m30a_other_sidebar, rows );
    const std::vector<widget_id> expected_other = {
        widget_audit_m30a_row_a, widget_audit_m30a_row_b, widget_audit_m30a_section_end
    };
    CHECK( rows == expected_other );

    // A section already in the sidebar isn't added twice
    rows = { widget_audit_m30a_section_end };
    widget::add_sidebar_sections( widget_audit_m30a_other_sidebar, rows );
    CHECK( rows.size() == 1 );

    // The test's sections stay out of the game's sidebars
    rows.clear();
    widget::add_sidebar_sections( widget_legacy_labels_sidebar, rows );
    CHECK( rows.empty() );

    // No sidebars listed: every sidebar
    widget_sidebar_section everywhere;
    CHECK( everywhere.applies_to( widget_legacy_labels_sidebar ) );
    CHECK( everywhere.applies_to( widget_audit_m30a_sidebar ) );
}

// UIHUD-J04: EOCs couldn't keep a note that was there, delete only their own note, or read
// notes.
TEST_CASE( "uihud-j04_eoc_overmap_notes", "[audit][eoc][overmap]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    const tripoint_abs_omt omt = u.pos_abs_omt() + point( 0, 12 );
    overmap_buffer.delete_note( omt );
    const on_out_of_scope cleanup( [omt]() {
        overmap_buffer.delete_note( omt );
    } );
    dialogue d( get_talker_for( u ), nullptr );
    write_var_value( var_type::context, "loc", &d, project_to<coords::ms>( omt ) );

    const talk_effect_t add_first = load_test_effect( R"({ "effect": { "add_overmap_note":
        { "context_val": "loc" }, "text": "First", "symbol": "F", "color": "R", "danger_radius": 1 } })" );
    const talk_effect_t add_second = load_test_effect( R"({ "effect": { "add_overmap_note":
        { "context_val": "loc" }, "text": "Second", "overwrite": false } })" );
    const talk_effect_t delete_second = load_test_effect( R"({ "effect": { "delete_overmap_note":
        { "context_val": "loc" }, "only_if_text": "Second" } })" );
    const talk_effect_t delete_first = load_test_effect( R"({ "effect": { "delete_overmap_note":
        { "context_val": "loc" }, "only_if_text": "First" } })" );
    const conditional_t::func has_note = load_test_condition(
            R"({ "condition": { "map_has_note": { "context_val": "loc" } } })" );
    const conditional_t::func has_first = load_test_condition(
            R"({ "condition": { "map_has_note": { "context_val": "loc" }, "text": "First" } })" );
    const conditional_t::func has_second = load_test_condition(
            R"({ "condition": { "map_has_note": { "context_val": "loc" }, "text": "Second" } })" );
    const conditional_t::func danger_one = load_test_condition(
            R"({ "condition": { "math": [ "map_note_danger(_loc) == 1" ] } })" );
    const conditional_t::func no_danger = load_test_condition(
            R"({ "condition": { "math": [ "map_note_danger(_loc) == -1" ] } })" );

    CHECK_FALSE( has_note( d ) );
    CHECK( no_danger( d ) );

    add_first.apply( d );
    REQUIRE( overmap_buffer.note( omt ) == "F:R;First" );
    CHECK( has_note( d ) );
    CHECK( has_first( d ) );
    CHECK_FALSE( has_second( d ) );
    CHECK( danger_one( d ) );

    // "overwrite": false keeps the note there
    add_second.apply( d );
    CHECK( overmap_buffer.note( omt ) == "F:R;First" );

    // "only_if_text" deletes only that note
    delete_second.apply( d );
    CHECK( overmap_buffer.has_note( omt ) );
    delete_first.apply( d );
    CHECK_FALSE( overmap_buffer.has_note( omt ) );
    CHECK( no_danger( d ) );
}

// UIHUD-J05: every true clause was shown, the threshold lookup was unused, and a copy had to
// repeat every clause to change one.
TEST_CASE( "uihud-j05_clause_modes_and_clause_ids", "[audit][ui][widget]" )
{
    const std::string clauses = R"("clauses": [ { "id": "low", "text": "low", "value": 0 },
        { "id": "high", "text": "high", "value": 20 }, { "id": "mid", "text": "mid", "value": 10 } ])";

    SECTION( "all" ) {
        widget w = load_test_widget( R"({ "style": "text", )" + clauses + " }" );
        CHECK( w.value_string( 15 ) == "low, high, mid" );
    }

    SECTION( "first" ) {
        widget w = load_test_widget( R"({ "style": "text", "clause_mode": "first", )" + clauses +
                                     " }" );
        CHECK( w.value_string( 15 ) == "low" );
    }

    SECTION( "threshold" ) {
        widget w = load_test_widget( R"({ "style": "text", "clause_mode": "threshold", )" + clauses +
                                     " }" );
        CHECK( w.value_string( 15 ) == "mid" );
        CHECK( w.value_string( 10 ) == "mid" );
        CHECK( w.value_string( 25 ) == "high" );
        CHECK( w.value_string( 0 ) == "low" );
        CHECK( w.value_string( -1 ).empty() );
    }

    SECTION( "a copy deletes, replaces and adds clauses by id, and changes breaks alone" ) {
        widget copy = widget_audit_m30a_clause_copy.obj();
        CHECK( copy.value_string( 0 ) == "B2, C, D" );
        CHECK( copy._colors.size() == 2 );
        const std::vector<int> breaks = { 75 };
        CHECK( copy._breaks == breaks );
    }
}

#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "input.h"
#include "input_context.h"
#include "input_enums.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "mod_keybinding.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "safemode_ui.h"
#include "talker.h"
#include "translation.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "weather.h"
#include "weather_type.h"
#include "widget.h"

static const itype_id itype_rock( "rock" );

static const mtype_id mon_audit_m30b_always( "mon_audit_m30b_always" );
static const mtype_id mon_chicken( "mon_chicken" );
static const mtype_id mon_zombie( "mon_zombie" );
static const mtype_id mon_zombie_fat( "mon_zombie_fat" );

static const vpart_id vpart_controls( "controls" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_seat( "seat" );

static const vproto_id vehicle_prototype_none( "none" );

static const widget_id widget_audit_m30b_aim_text( "audit_m30b_aim_text" );
static const widget_id widget_audit_m30b_no_such_slot( "audit_m30b_no_such_slot" );
static const widget_id widget_audit_m30b_num( "audit_m30b_num" );
static const widget_id widget_audit_m30b_slot( "audit_m30b_slot" );
static const widget_id widget_audit_m30b_vehicle_layout( "audit_m30b_vehicle_layout" );
static const widget_id widget_audit_m30b_who_text( "audit_m30b_who_text" );
static const widget_id widget_audit_m30b_wielded_charge( "audit_m30b_wielded_charge" );
static const widget_id
widget_audit_m30b_wielded_charge_hidden( "audit_m30b_wielded_charge_hidden" );
static const widget_id widget_audit_m30b_wielded_layout( "audit_m30b_wielded_layout" );
static const widget_id widget_audit_m30b_wielded_none_layout( "audit_m30b_wielded_none_layout" );

static bool contains( const std::string &text, const std::string &part )
{
    return text.find( part ) != std::string::npos;
}

static std::string shown( const widget_id &id, const Character &who )
{
    widget w = id.obj();
    return w.layout( who, 30 );
}

static bool check_condition( const_dialogue const &d, const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    return condition( d );
}

// UIHUD-J06: widget clauses and custom vars had no beta talker, so the HUD could not show the
// wielded item's or the driven vehicle's vars. "beta" gives them one; children inherit it.
TEST_CASE( "uihud-j06_widget_beta_talker", "[audit][widget]" )
{
    clear_avatar();
    clear_map_without_vision();
    clear_vehicles();
    set_time( calendar::turn_zero + 12_hours );
    on_out_of_scope cleanup( []() {
        clear_avatar();
        clear_creatures();
        clear_vehicles();
    } );
    avatar &ava = get_avatar();
    map &here = get_map();

    SECTION( "the wielded item, inherited by a child widget" ) {
        // Empty-handed: nothing to show
        REQUIRE_FALSE( ava.get_wielded_item() );
        CHECK( shown( widget_audit_m30b_wielded_layout, ava ).empty() );
        const std::string empty_charge = shown( widget_audit_m30b_wielded_charge, ava );
        CHECK( contains( empty_charge, "Charge" ) );
        CHECK_FALSE( contains( empty_charge, "0" ) );
        CHECK( shown( widget_audit_m30b_wielded_charge_hidden, ava ).empty() );

        REQUIRE( ava.wield( *ava.i_add( item( itype_rock ) ) ) );
        item_location wielded = ava.get_wielded_item();
        REQUIRE( wielded );
        get_talker_for( wielded )->set_value( "audit_m30b_charge", diag_value( 42.0 ) );

        CHECK( contains( shown( widget_audit_m30b_wielded_layout, ava ), "item" ) );
        CHECK( contains( shown( widget_audit_m30b_wielded_charge, ava ), "42" ) );
        CHECK( contains( shown( widget_audit_m30b_wielded_charge_hidden, ava ), "42" ) );
        // A child with "beta": "none" has none
        const std::string none = shown( widget_audit_m30b_wielded_none_layout, ava );
        CHECK( contains( none, "no" ) );
        CHECK_FALSE( contains( none, "yes" ) );
    }

    SECTION( "the vehicle you are in" ) {
        CHECK( shown( widget_audit_m30b_vehicle_layout, ava ).empty() );

        const tripoint_bub_ms spot = ava.pos_bub() + tripoint::east * 3;
        vehicle *veh = here.add_vehicle( vehicle_prototype_none, spot, 0_degrees, 0,
                                         veh_spawn_status::UNDAMAGED );
        REQUIRE( veh != nullptr );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) >= 0 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_seat ) >= 0 );
        REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_controls ) >= 0 );
        here.add_vehicle_to_cache( veh );
        on_out_of_scope unboard( [&ava, spot]() {
            if( ava.in_vehicle ) {
                get_map().unboard_vehicle( spot );
            }
        } );
        ava.setpos( here, spot );
        here.board_vehicle( spot, &ava );
        REQUIRE( ava.in_vehicle );

        CHECK( contains( shown( widget_audit_m30b_vehicle_layout, ava ), "vehicle" ) );
    }

    SECTION( "the creature you aimed at" ) {
        ava.last_target.reset();
        CHECK( shown( widget_audit_m30b_aim_text, ava ).empty() );

        monster &zed = spawn_test_monster( "mon_zombie", ava.pos_bub() + tripoint::east * 4 );
        REQUIRE( ava.sees( here, zed ) );
        ava.last_target = g->shared_from( zed );
        CHECK( contains( shown( widget_audit_m30b_aim_text, ava ), "monster" ) );
        ava.last_target.reset();
    }
}

// UIHUD-J09: widgets only rendered for the avatar. They are shown for any character now (the
// follower panel and the eat menu show the UI slots follower_needs and consume_menu_needs).
TEST_CASE( "uihud-j09_widgets_shown_for_another_character", "[audit][widget]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    on_out_of_scope cleanup( []() {
        get_talker_for( get_avatar() )->remove_value( "audit_m30b_num" );
        clear_npcs();
    } );
    avatar &ava = get_avatar();
    npc &guy = spawn_npc( ava.pos_bub().xy() + point::east * 2, "thug" );
    clear_character( guy );
    get_talker_for( ava )->set_value( "audit_m30b_num", diag_value( 3.0 ) );
    get_talker_for( guy )->set_value( "audit_m30b_num", diag_value( 7.0 ) );

    CHECK( contains( shown( widget_audit_m30b_who_text, ava ), "avatar" ) );
    CHECK( contains( shown( widget_audit_m30b_who_text, guy ), "npc" ) );
    CHECK( contains( shown( widget_audit_m30b_num, ava ), "3" ) );
    CHECK( contains( shown( widget_audit_m30b_num, guy ), "7" ) );

    const std::vector<std::string> lines = widget::slot_lines( widget_audit_m30b_slot, guy, 40 );
    REQUIRE( lines.size() == 2 );
    CHECK( contains( lines[0], "npc" ) );
    CHECK( contains( lines[1], "7" ) );
    // A slot no mod defines adds nothing
    CHECK( widget::slot_lines( widget_audit_m30b_no_such_slot, guy, 40 ).empty() );
}

namespace
{
struct spotted_events : event_subscriber {
    using event_subscriber::notify;
    std::vector<cata::event> seen;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::avatar_spots_hostile ) {
            seen.push_back( e );
        }
    }
};
} // namespace

// UIHUD-J07: safe mode rules matched only translated names, and spotting a hostile sent no event.
TEST_CASE( "uihud-j07_safe_mode_rules_by_id_and_species", "[audit][safemode]" )
{
    safemode &rules = get_safemode();
    on_out_of_scope cleanup( [&rules]() {
        rules.remove_rule( "id:mon_zombie", Creature::Attitude::HOSTILE );
        rules.remove_rule( "species:ZOMBIE", Creature::Attitude::HOSTILE );
        rules.remove_rule( "id:mon_zombie_fat", Creature::Attitude::HOSTILE );
    } );
    const std::string zombie = mon_zombie->nname();
    const std::string fat_zombie = mon_zombie_fat->nname();
    const std::string chicken = mon_chicken->nname();
    REQUIRE( rules.check_monster( zombie, Creature::Attitude::HOSTILE, 1 ) == rule_state::NONE );

    rules.add_rule( "id:mon_zombie", Creature::Attitude::HOSTILE, 0, rule_state::BLACKLISTED );
    CHECK( rules.check_monster( zombie, Creature::Attitude::HOSTILE, 1 ) == rule_state::BLACKLISTED );
    CHECK( rules.check_monster( chicken, Creature::Attitude::HOSTILE, 1 ) == rule_state::NONE );
    rules.remove_rule( "id:mon_zombie", Creature::Attitude::HOSTILE );
    CHECK( rules.check_monster( zombie, Creature::Attitude::HOSTILE, 1 ) == rule_state::NONE );

    rules.add_rule( "species:ZOMBIE", Creature::Attitude::HOSTILE, 0, rule_state::BLACKLISTED );
    CHECK( rules.check_monster( zombie, Creature::Attitude::HOSTILE, 1 ) == rule_state::BLACKLISTED );
    CHECK( rules.check_monster( fat_zombie, Creature::Attitude::HOSTILE,
                                1 ) == rule_state::BLACKLISTED );
    CHECK( rules.check_monster( chicken, Creature::Attitude::HOSTILE, 1 ) == rule_state::NONE );
    // A whitelist rule by id takes one back out
    rules.add_rule( "id:mon_zombie_fat", Creature::Attitude::HOSTILE, 0, rule_state::WHITELISTED );
    CHECK( rules.check_monster( fat_zombie, Creature::Attitude::HOSTILE,
                                1 ) == rule_state::WHITELISTED );
    CHECK( rules.check_monster( zombie, Creature::Attitude::HOSTILE, 1 ) == rule_state::BLACKLISTED );
}

TEST_CASE( "uihud-j07_spotting_a_hostile_sends_an_event", "[audit][safemode]" )
{
    clear_avatar();
    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    const safe_mode_type old_mode = g->safe_mode;
    on_out_of_scope cleanup( [old_mode]() {
        clear_creatures();
        g->mon_info_update();
        g->set_safe_mode( old_mode );
    } );
    REQUIRE( get_safemode().empty() );
    avatar &ava = get_avatar();
    const map &here = get_map();
    spotted_events events;
    get_event_bus().subscribe( &events );
    // Nothing in view: the count of hostiles starts at 0
    g->mon_info_update();
    REQUIRE( events.seen.empty() );
    const tripoint_bub_ms spot = ava.pos_bub() + tripoint::east * 5;

    SECTION( "a zombie" ) {
        monster &zed = spawn_test_monster( "mon_zombie", spot );
        REQUIRE( ava.sees( here, zed ) );
        g->mon_info_update();
        REQUIRE( events.seen.size() == 1 );
        const cata::event &e = events.seen.front();
        CHECK( e.get<character_id>( "character" ) == ava.getID() );
        CHECK( e.get<mtype_id>( "monster_id" ) == mon_zombie );
        CHECK( e.get<int>( "distance" ) == 5 );
        CHECK( e.get<int>( "hostiles" ) == 1 );
        // Still in view: nothing new
        g->mon_info_update();
        CHECK( events.seen.size() == 1 );
    }
    SECTION( "a zombie whose type safe mode ignores" ) {
        monster &zed = spawn_test_monster( "mon_audit_m30b_ignored", spot );
        REQUIRE( ava.sees( here, zed ) );
        g->mon_info_update();
        CHECK( events.seen.empty() );
    }
    SECTION( "a chicken whose type always stops safe mode" ) {
        monster &bird = spawn_test_monster( "mon_audit_m30b_always", spot );
        REQUIRE( ava.sees( here, bird ) );
        g->mon_info_update();
        REQUIRE( events.seen.size() == 1 );
        CHECK( events.seen.front().get<mtype_id>( "monster_id" ) == mon_audit_m30b_always );
    }
}

TEST_CASE( "uihud-j07_safe_mode_math", "[audit][safemode]" )
{
    const safe_mode_type old_mode = g->safe_mode;
    on_out_of_scope cleanup( [old_mode]() {
        g->set_safe_mode( old_mode );
    } );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    math_exp exp;
    REQUIRE( exp.parse( "safe_mode() = 0" ) );
    exp.eval( d );
    CHECK( g->safe_mode == SAFE_MODE_OFF );
    REQUIRE( exp.parse( "safe_mode() = 1" ) );
    exp.eval( d );
    CHECK( g->safe_mode == SAFE_MODE_ON );
    // Clamped to 0..2
    REQUIRE( exp.parse( "safe_mode() = 7" ) );
    exp.eval( d );
    CHECK( g->safe_mode == SAFE_MODE_STOP );
    REQUIRE( exp.parse( "safe_mode()" ) );
    CHECK( exp.eval( d ) == Approx( 2.0 ) );
}

// UIHUD-J08: no math or condition saw the sun, so sunlight traits could not be enchantments.
TEST_CASE( "uihud-j08_sun_irradiance_and_in_sunlight", "[audit][math_parser]" )
{
    clear_map_without_vision();
    clear_avatar();
    scoped_weather_override clear_weather( WEATHER_CLEAR );
    avatar &ava = get_avatar();
    dialogue d( get_talker_for( ava ), nullptr );
    math_exp sun;
    REQUIRE( sun.parse( "u_sun_irradiance()" ) );

    SECTION( "midday, outside" ) {
        set_time_to_day();
        REQUIRE( g->is_in_sunlight( ava.pos_bub() ) );
        CHECK( sun.eval( d ) == Approx( incident_sun_irradiance( WEATHER_CLEAR, calendar::turn ) ) );
        CHECK( sun.eval( d ) > irradiance::minimal );
        CHECK( check_condition( d, "{ \"condition\": \"u_in_sunlight\" }" ) );
    }
    SECTION( "midnight" ) {
        set_time( calendar::turn - time_past_midnight( calendar::turn ) );
        REQUIRE_FALSE( g->is_in_sunlight( ava.pos_bub() ) );
        CHECK( sun.eval( d ) <= irradiance::minimal );
        CHECK_FALSE( check_condition( d, "{ \"condition\": \"u_in_sunlight\" }" ) );
    }
}

// UIHUD-J10: mods could not add hotkeys, and no key ran an EOC.
TEST_CASE( "uihud-j10_mod_keybinding_runs_its_eoc", "[audit][input]" )
{
    clear_avatar();
    get_globals().clear_global_values();
    on_out_of_scope cleanup( []() {
        get_globals().clear_global_values();
    } );
    avatar &ava = get_avatar();

    const input_context ctxt = get_default_mode_input_context();
    CHECK( ctxt.is_registered_action( "AUDIT_M30B_HOTKEY" ) );
    CHECK( inp_mngr.has_basic_action( "AUDIT_M30B_HOTKEY", "DEFAULTMODE" ) );

    CHECK( mod_keybindings::run( "AUDIT_M30B_HOTKEY", ava ) );
    const diag_value *ran = get_globals().maybe_get_global_value( "audit_m30b_hotkey_ran" );
    REQUIRE( ran != nullptr );
    CHECK( ran->dbl() == Approx( 1.0 ) );

    // Its condition false: still the hotkey's key, but the EOC doesn't run
    get_globals().set_global_value( "audit_m30b_hotkey_blocked", 1.0 );
    CHECK( mod_keybindings::run( "AUDIT_M30B_HOTKEY", ava ) );
    ran = get_globals().maybe_get_global_value( "audit_m30b_hotkey_ran" );
    REQUIRE( ran != nullptr );
    CHECK( ran->dbl() == Approx( 1.0 ) );

    // Built-in actions and unknown ids are not mod hotkeys
    CHECK_FALSE( mod_keybindings::run( "pause", ava ) );
    CHECK_FALSE( mod_keybindings::run( "AUDIT_M30B_NO_SUCH_HOTKEY", ava ) );
}

TEST_CASE( "uihud-j10_mod_actions_leave_out_used_keys", "[audit][input]" )
{
    const std::string context = "DEFAULTMODE";
    const std::string first = "AUDIT_M30B_FIRST";
    const std::string second = "AUDIT_M30B_SECOND";
    on_out_of_scope cleanup( [&]() {
        inp_mngr.remove_mod_action( first, context );
        inp_mngr.remove_mod_action( second, context );
    } );
    input_event key( 1048001, input_event_t::keyboard_code );
    key.modifiers = { keymod_t::ctrl, keymod_t::alt, keymod_t::shift };
    const std::vector<input_event> keys = { key };

    inp_mngr.add_mod_action( first, context, no_translation( "first" ), keys );
    CHECK( inp_mngr.has_basic_action( first, context ) );
    CHECK( inp_mngr.get_input_for_action( first, context ) == keys );
    // The key is taken: the second action starts unbound
    inp_mngr.add_mod_action( second, context, no_translation( "second" ), keys );
    CHECK( inp_mngr.has_basic_action( second, context ) );
    CHECK( inp_mngr.get_input_for_action( second, context ).empty() );

    inp_mngr.remove_mod_action( first, context );
    inp_mngr.remove_mod_action( second, context );
    CHECK_FALSE( inp_mngr.has_basic_action( first, context ) );
    CHECK_FALSE( inp_mngr.has_basic_action( second, context ) );
}

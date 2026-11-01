#include <ostream>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "creature.h"
#include "enums.h"
#include "filesystem.h"
#include "path_info.h"
#include "player_helpers.h"
#include "safemode_ui.h"
#include "type_id.h"
#include "widget.h"
#include "worldfactory.h"

static const efftype_id effect_debugged( "debugged" );

static const widget_id widget_test_layout_cols_conditional_no_default(
    "test_layout_cols_conditional_no_default" );

// UIHUD-B01: safemode::save( true ) skipped writing the per-character rules unless both
// <char>.sav and <char>.sav.zzip existed. A world only ever writes one of the two, so the
// character's safemode rules were silently dropped on every save.
TEST_CASE( "uihud-b01_character_safemode_rules_are_saved", "[audit][safemode]" )
{
    const cata_path base = PATH_INFO::player_base_save_path();
    const cata_path sav = base + ".sav";
    const cata_path sav_zzip = base + ".sav" + zzip_suffix;
    const cata_path sfm = base + ".sfm.json";
    // The test world is only saved when the suite ends, so the character has no
    // compressed save. Give it an uncompressed one, as a world without compression has.
    REQUIRE_FALSE( file_exist( sav_zzip ) );
    const bool had_sav = file_exist( sav );

    safemode &sm = get_safemode();
    const auto cleanup = [&]() {
        sm.clear_character_rules();
        remove_file( sfm );
        if( !had_sav ) {
            remove_file( sav );
        }
    };
    on_out_of_scope restore( cleanup );
    cleanup();

    if( !had_sav ) {
        REQUIRE( assure_dir_exist( PATH_INFO::world_base_save_path() ) );
        write_to_file( sav, []( std::ostream & os ) {
            os << "{}";
        } );
    }
    REQUIRE( file_exist( sav ) );

    const std::string rule = "audit uihud-b01 monster";
    sm.add_rule( rule, Creature::Attitude::HOSTILE, 0, rule_state::BLACKLISTED );
    REQUIRE( sm.has_rule( rule, Creature::Attitude::HOSTILE ) );
    CHECK( sm.save_character() );
    CHECK( file_exist( sfm ) );

    // The rule survives a reload.
    sm.clear_character_rules();
    REQUIRE_FALSE( sm.has_rule( rule, Creature::Attitude::HOSTILE ) );
    sm.load_character();
    CHECK( sm.has_rule( rule, Creature::Attitude::HOSTILE ) );
}

// UIHUD-B02: a column layout driven by clauses, with no default_clause, has no widgets
// while none of its clauses apply. layout() then logged a debugmsg and divided the
// available width by zero.
TEST_CASE( "uihud-b02_conditional_column_layout_without_true_clause_is_empty",
           "[audit][widget]" )
{
    avatar &ava = get_avatar();
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    widget wgt = widget_test_layout_cols_conditional_no_default.obj();
    REQUIRE( wgt._style == "layout" );
    REQUIRE( wgt._arrange == "columns" );
    REQUIRE_FALSE( wgt._clauses.empty() );

    // No clause applies: the layout shows nothing.
    REQUIRE_FALSE( ava.has_effect( effect_debugged ) );
    CHECK( wgt.layout( ava, 36 ).empty() );

    // The clause applies: its widget is shown.
    ava.add_effect( effect_debugged, 1_hours );
    REQUIRE( ava.has_effect( effect_debugged ) );
    CHECK( wgt.layout( ava, 36 ).find( "Zero" ) != std::string::npos );
}

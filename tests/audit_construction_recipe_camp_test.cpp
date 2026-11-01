#include <algorithm>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "cata_catch.h"
#include "character.h"
#include "construction.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "type_id.h"

static const activity_id ACT_BUILD( "ACT_BUILD" );

static const construction_str_id construction_test_constr_eoc_hooks( "test_constr_eoc_hooks" );

static const proficiency_id proficiency_prof_blacksmithing( "prof_blacksmithing" );
static const proficiency_id proficiency_prof_carving( "prof_carving" );
static const proficiency_id proficiency_prof_closures( "prof_closures" );
static const proficiency_id proficiency_prof_leatherworking( "prof_leatherworking" );
static const proficiency_id proficiency_prof_metalworking( "prof_metalworking" );
static const proficiency_id proficiency_prof_tempering( "prof_tempering" );

static const recipe_id recipe_ammo_satchel_leather( "ammo_satchel_leather" );
static const recipe_id recipe_leatherbone_armor_cow( "leatherbone_armor_cow" );
static const recipe_id recipe_qt_sheet_metal_small( "qt_sheet_metal_small" );

static const ter_str_id ter_t_dirt( "t_dirt" );

// TERR-J01: constructions read no condition or EOC fields, and nothing was sent when one
// was finished, so a mod couldn't gate a construction on the builder or react to it.
TEST_CASE( "terr-j01_construction_condition_and_eoc_hooks", "[audit][construction]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    const construction &con = construction_test_constr_eoc_hooks.obj();
    const tripoint_bub_ms site = u.pos_bub() + tripoint::east;
    const tripoint_abs_ms site_abs = here.get_abs( site );
    REQUIRE( here.ter( site ) != ter_t_dirt );
    u.remove_value( "test_constr_turns" );
    u.remove_value( "test_constr_done" );
    u.remove_value( "test_constr_done_pos" );

    // The condition is checked with the builder as alpha.
    u.set_value( "test_constr_allowed", 0 );
    CHECK_FALSE( can_construct( con, site ) );
    u.set_value( "test_constr_allowed", 1 );
    CHECK( can_construct( con, site ) );
    npc &guy = spawn_npc( ( u.pos_bub() + tripoint::south ).xy(), "test_talker" );
    CHECK_FALSE( can_construct( con, site, guy ) );
    guy.set_value( "test_constr_allowed", 1 );
    CHECK( can_construct( con, site, guy ) );

    partial_con pc;
    pc.id = construction_test_constr_eoc_hooks.id();
    here.partial_con_set( site, pc );
    u.assign_activity( build_construction_activity_actor( site_abs ) );
    REQUIRE( u.activity.id() == ACT_BUILD );
    // The condition gates starting it, not the work already begun.
    u.set_value( "test_constr_allowed", 0 );
    REQUIRE( process_activity_bounded( u, 1000, 2000 ) );

    CHECK( here.ter( site ) == ter_t_dirt );
    const diag_value *turns = u.maybe_get_value( "test_constr_turns" );
    REQUIRE( turns != nullptr );
    CHECK( turns->dbl() >= 1 );
    const diag_value *done = u.maybe_get_value( "test_constr_done" );
    REQUIRE( done != nullptr );
    CHECK( done->str() == "test_constr_eoc_hooks" );
    const diag_value *done_pos = u.maybe_get_value( "test_constr_done_pos" );
    REQUIRE( done_pos != nullptr );
    REQUIRE( done_pos->is_tripoint() );
    CHECK( done_pos->tripoint() == site_abs );

    // character_finishes_construction carries the construction id and the tile.
    const diag_value *event_id = globvars.maybe_get_global_value( "test_constr_event_id" );
    REQUIRE( event_id != nullptr );
    CHECK( event_id->str() == "test_constr_eoc_hooks" );
    const diag_value *event_pos = globvars.maybe_get_global_value( "test_constr_event_pos" );
    REQUIRE( event_pos != nullptr );
    REQUIRE( event_pos->is_tripoint() );
    CHECK( event_pos->tripoint() == site_abs );
}

static int prof_count( const recipe &r, const proficiency_id &prof )
{
    return std::count_if( r.proficiencies.begin(), r.proficiencies.end(),
    [&prof]( const recipe_proficiency & rp ) {
        return rp.id == prof;
    } );
}

static void load_into( recipe &r, const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    r.load( jo, "dda" );
}

static const std::string craft_j01_base = R"({
    "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_base",
    "category": "CC_WEAPON", "subcategory": "CSC_WEAPON_BASHING",
    "skill_used": "fabrication", "difficulty": 1, "time": "1 m",
    "activity_level": "LIGHT_EXERCISE",
    "components": [ [ [ "2x4", 1 ] ] ],
    "proficiencies": [
        { "proficiency": "prof_metalworking" },
        { "proficiency": "prof_blacksmithing", "time_multiplier": 1.5 }
    ]
})";

// Loads child_json over a copy of the base recipe, as copy-from does.
static recipe load_craft_j01_child( const std::string &child_json )
{
    recipe r;
    load_into( r, craft_j01_base );
    r.was_loaded = true;
    load_into( r, child_json );
    return r;
}

// CRAFT-J01: recipe proficiencies were read with a plain read, so "extend" and "delete" of
// them were silently dropped and shipped recipes lost the proficiencies they meant to add.
TEST_CASE( "craft-j01_recipe_copy_from_extends_and_deletes_proficiencies",
           "[audit][crafting]" )
{
    SECTION( "shipped recipes get the proficiencies they extend" ) {
        const recipe &sheet = recipe_qt_sheet_metal_small.obj();
        CHECK( prof_count( sheet, proficiency_prof_tempering ) == 1 );
        CHECK( prof_count( sheet, proficiency_prof_metalworking ) == 1 );
        CHECK( prof_count( sheet, proficiency_prof_blacksmithing ) == 1 );
        const recipe &satchel = recipe_ammo_satchel_leather.obj();
        CHECK( prof_count( satchel, proficiency_prof_leatherworking ) == 1 );
        CHECK( prof_count( satchel, proficiency_prof_closures ) == 1 );
        // Extending one the parent already has replaces it instead of listing it twice.
        const recipe &bone = recipe_leatherbone_armor_cow.obj();
        CHECK( prof_count( bone, proficiency_prof_carving ) == 1 );
        CHECK( prof_count( bone, proficiency_prof_closures ) == 1 );
    }
    SECTION( "extend adds new ids and replaces the entry for a listed id" ) {
        const recipe r = load_craft_j01_child( R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_extend",
            "components": [ [ [ "2x4", 1 ] ] ],
            "extend": { "proficiencies": [
                { "proficiency": "prof_tempering" },
                { "proficiency": "prof_blacksmithing", "required": true }
            ] }
        })" );
        REQUIRE( r.proficiencies.size() == 3 );
        CHECK( r.proficiencies[0].id == proficiency_prof_metalworking );
        CHECK( r.proficiencies[1].id == proficiency_prof_blacksmithing );
        CHECK( r.proficiencies[1].required );
        CHECK( r.proficiencies[1].time_multiplier == 0.0f );
        CHECK( r.proficiencies[2].id == proficiency_prof_tempering );
        CHECK_FALSE( r.proficiencies[2].required );
    }
    SECTION( "delete removes the entries for the listed ids" ) {
        const recipe r = load_craft_j01_child( R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_delete",
            "components": [ [ [ "2x4", 1 ] ] ],
            "delete": { "proficiencies": [ "prof_metalworking" ] }
        })" );
        REQUIRE( r.proficiencies.size() == 1 );
        CHECK( r.proficiencies[0].id == proficiency_prof_blacksmithing );
        CHECK( r.proficiencies[0].time_multiplier == 1.5f );
        const recipe r2 = load_craft_j01_child( R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_delete_obj",
            "components": [ [ [ "2x4", 1 ] ] ],
            "delete": { "proficiencies": [ { "proficiency": "prof_blacksmithing" } ] }
        })" );
        REQUIRE( r2.proficiencies.size() == 1 );
        CHECK( r2.proficiencies[0].id == proficiency_prof_metalworking );
    }
    SECTION( "what can't be applied is a load error, not a silent no-op" ) {
        // Deleting a proficiency the parent doesn't have.
        CHECK_THROWS_AS( load_craft_j01_child( R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_bad_delete",
            "components": [ [ [ "2x4", 1 ] ] ],
            "delete": { "proficiencies": [ "prof_tempering" ] }
        })" ), JsonError );
        // Inline requirements are never inherited, so they can't be extended or deleted.
        CHECK_THROWS_AS( load_craft_j01_child( R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_bad_comps",
            "components": [ [ [ "2x4", 1 ] ] ],
            "extend": { "components": [ [ [ "rock", 1 ] ] ] }
        })" ), JsonError );
        CHECK_THROWS_AS( load_craft_j01_child( R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_bad_tools",
            "components": [ [ [ "2x4", 1 ] ] ],
            "delete": { "tools": [ "hammer" ] }
        })" ), JsonError );
        // Without copy-from there is nothing to extend.
        recipe fresh;
        CHECK_THROWS_AS( load_into( fresh, R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_craft_j01_no_parent",
            "category": "CC_WEAPON", "subcategory": "CSC_WEAPON_BASHING",
            "skill_used": "fabrication", "difficulty": 1, "time": "1 m",
            "activity_level": "LIGHT_EXERCISE",
            "components": [ [ [ "2x4", 1 ] ] ],
            "extend": { "proficiencies": [ { "proficiency": "prof_tempering" } ] }
        })" ), JsonError );
    }
}

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "basecamp.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "construction.h"
#include "coordinates.h"
#include "debug.h"
#include "faction.h"
#include "faction_camp.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "game_constants.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "math_parser_diag_value.h"
#include "memory_fast.h"
#include "mission_companion.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "type_id.h"

static const activity_id ACT_BUILD( "ACT_BUILD" );

static const construction_str_id construction_test_constr_eoc_hooks( "test_constr_eoc_hooks" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MISSION_DEATH( "EOC_TEST_MISSION_DEATH" );

static const faction_id faction_free_merchants( "free_merchants" );
static const faction_id faction_your_followers( "your_followers" );

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

// TERR-J01 (review): construction::check() didn't look at do_turn_eocs and post_eocs, so a
// misspelled EOC id only showed up when the construction was built.
TEST_CASE( "terr-j01_construction_eoc_ids_are_checked", "[audit][construction]" )
{
    construction con = construction_test_constr_eoc_hooks.obj();
    CHECK( capture_debugmsg_during( [&con]() {
        con.check();
    } ).empty() );

    con.do_turn_eocs.emplace_back( "audit_no_such_turn_eoc" );
    con.post_eocs.emplace_back( "audit_no_such_post_eoc" );
    const std::string msg = capture_debugmsg_during( [&con]() {
        con.check();
    } );
    CHECK_THAT( msg, Catch::Matchers::Contains( "audit_no_such_turn_eoc in do_turn_eocs" ) );
    CHECK_THAT( msg, Catch::Matchers::Contains( "audit_no_such_post_eoc in post_eocs" ) );
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

static std::string global_string( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    return val == nullptr ? std::string() : val->to_string();
}

// CAMP-J01: nothing was sent when companions left on or came back from missions, and a
// companion killed on a mission was erased with remove_npc instead of dying: no death EOCs,
// no character_dies, and it stayed a faction member and camp assignee.
TEST_CASE( "camp-j01_companion_missions_send_events_and_deaths_run_die",
           "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    map &here = get_map();
    const tripoint_bub_ms camp_mid{ MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 };
    const tripoint_abs_omt camp_omt = project_to<coords::omt>( here.get_abs( camp_mid ) );
    const tripoint_abs_ms camp_ms = project_to<coords::ms>( camp_omt );
    here.add_camp( camp_omt, "faction_camp" );
    std::optional<basecamp *> bcp = overmap_buffer.find_camp( camp_omt.xy() );
    REQUIRE( bcp );
    basecamp &camp = **bcp;
    on_out_of_scope cleanup( [&camp_omt]() {
        overmap_buffer.clear_camps( camp_omt.xy() );
    } );
    camp.set_owner( faction_your_followers );
    camp.define_camp( camp_omt, "faction_base_bare_bones_NPC_camp_0", false );

    npc &guy = spawn_npc( camp_mid.xy(), "test_talker" );
    clear_character( guy, true );
    guy.set_fac( faction_your_followers );
    const npc_ptr comp = overmap_buffer.find_npc( guy.getID() );
    REQUIRE( comp );
    const character_id comp_id = comp->getID();

    const mission_id gather{ Camp_Gather_Materials, "", {}, base_camps::base_dir };
    REQUIRE( camp.start_mission( gather, 3_hours, false, "", false, {},
                                 std::map<skill_id, int>(), LIGHT_EXERCISE, 0_hours, comp ) );
    CHECK( global_string( "test_mission_started" ) == "Camp_Gather_Materials" );
    const diag_value *started_pos = globvars.maybe_get_global_value( "test_mission_started_pos" );
    REQUIRE( started_pos != nullptr );
    REQUIRE( started_pos->is_tripoint() );
    CHECK( started_pos->tripoint() == camp_ms );
    // The companion is alpha of the event EOC.
    const diag_value *alpha_mark = comp->maybe_get_value( "test_mission_alpha" );
    REQUIRE( alpha_mark != nullptr );
    CHECK( alpha_mark->dbl() == 1 );

    SECTION( "companion back from the mission" ) {
        camp.finish_return( *comp, false, "returns.", "survival", 1 );
        CHECK( global_string( "test_mission_finished" ) == "Camp_Gather_Materials" );
        CHECK( global_string( "test_mission_outcome" ) == "returned" );
    }
    SECTION( "companion called back" ) {
        camp.finish_return( *comp, false, "responds.", "menial", 0, true );
        CHECK( global_string( "test_mission_outcome" ) == "canceled" );
    }
    SECTION( "companion killed on the mission dies like any NPC" ) {
        comp->death_eocs.push_back( effect_on_condition_EOC_TEST_MISSION_DEATH );
        camp.add_assignee( comp_id );
        REQUIRE( comp->assigned_camp );
        faction *followers = g->faction_manager_ptr->get( faction_your_followers );
        REQUIRE( followers != nullptr );
        REQUIRE( followers->members.count( comp_id ) == 1 );

        CHECK( talk_function::companion_mission_death( *comp ) );
        CHECK( comp->is_dead() );
        const diag_value *death_eoc = globvars.maybe_get_global_value( "test_mission_death_eoc" );
        REQUIRE( death_eoc != nullptr );
        CHECK( death_eoc->dbl() == 1 );
        CHECK( global_string( "test_mission_finished" ) == "Camp_Gather_Materials" );
        CHECK( global_string( "test_mission_outcome" ) == "died" );
        CHECK( followers->members.count( comp_id ) == 0 );
        CHECK_FALSE( comp->assigned_camp );
        CHECK( overmap_buffer.find_npc( comp_id ) == nullptr );
    }
    SECTION( "camp_taken_over says where the camp is" ) {
        camp.finish_return( *comp, false, "returns.", "survival", 1 );
        camp.handle_takeover_by( faction_free_merchants, false );
        const diag_value *taken_pos = globvars.maybe_get_global_value( "test_camp_taken_over_pos" );
        REQUIRE( taken_pos != nullptr );
        REQUIRE( taken_pos->is_tripoint() );
        CHECK( taken_pos->tripoint() == camp_ms );
    }
}

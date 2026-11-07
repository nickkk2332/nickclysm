#include <bitset>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "construction.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue_chatbin.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "messages.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "requirements.h"
#include "translation.h"
#include "trap.h"
#include "type_id.h"
#include "vehicle.h"
#include "vpart_position.h"

static const character_portrait_id
character_portrait_Named_NPC_luo_meizhen( "Named_NPC_luo_meizhen" );

static const construction_str_id construction_ap_box_battery_charger( "ap_box_battery_charger" );
static const construction_str_id
construction_audit_loading_appliance_twice( "audit_loading_appliance_twice" );
static const construction_str_id
construction_constr_concrete_ramp_high( "constr_concrete_ramp_high" );
static const construction_str_id
construction_constr_concrete_ramp_low( "constr_concrete_ramp_low" );
static const construction_str_id construction_constr_earth_ramp_high( "constr_earth_ramp_high" );
static const construction_str_id construction_constr_earth_ramp_low( "constr_earth_ramp_low" );
static const construction_str_id construction_constr_extract_clay( "constr_extract_clay" );
static const construction_str_id construction_constr_firewood_source( "constr_firewood_source" );
static const construction_str_id construction_constr_practice_target( "constr_practice_target" );
static const construction_str_id
construction_constr_repair_wood_stairs_up( "constr_repair_wood_stairs_up" );
static const construction_str_id
construction_constr_window_boarded_curtains( "constr_window_boarded_curtains" );
static const construction_str_id construction_constr_wood_stairs_up( "constr_wood_stairs_up" );
static const construction_str_id
construction_remove_scaffolding_pipe_down_above_updown( "remove_scaffolding_pipe_down_above_updown" );
static const construction_str_id
construction_remove_scaffolding_pipe_up( "remove_scaffolding_pipe_up" );

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_LOADING_DEATH_A( "EOC_AUDIT_LOADING_DEATH_A" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_LOADING_DEATH_B( "EOC_AUDIT_LOADING_DEATH_B" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_LOADING_DEATH_C( "EOC_AUDIT_LOADING_DEATH_C" );

static const faction_id faction_audit_loading_faction_child( "audit_loading_faction_child" );
static const faction_id faction_audit_loading_faction_early( "audit_loading_faction_early" );
static const faction_id faction_audit_loading_faction_parent( "audit_loading_faction_parent" );
static const faction_id
faction_audit_loading_no_such_faction( "audit_loading_no_such_faction" );
static const faction_id faction_free_merchants( "free_merchants" );
static const faction_id faction_lobby_beggars( "lobby_beggars" );
static const faction_id faction_your_followers( "your_followers" );

static const item_category_id item_category_guns( "guns" );
static const item_category_id item_category_tools( "tools" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_FMCNote( "FMCNote" );
static const itype_id itype_box_battery_charger( "box_battery_charger" );
static const itype_id itype_duct_tape( "duct_tape" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_plastic_chunk( "plastic_chunk" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_screwdriver( "screwdriver" );
static const itype_id itype_sheet( "sheet" );
static const itype_id itype_stick( "stick" );
static const itype_id itype_string_36( "string_36" );
static const itype_id itype_weed( "weed" );

static const mfaction_str_id monfaction_human( "human" );
static const mfaction_str_id monfaction_player( "player" );

static const mission_type_id mission_MISSION_GET_ANTIBIOTICS( "MISSION_GET_ANTIBIOTICS" );
static const mission_type_id mission_MISSION_GET_BLACK_BOX( "MISSION_GET_BLACK_BOX" );
static const mission_type_id mission_MISSION_GET_INHALER( "MISSION_GET_INHALER" );
static const mission_type_id
mission_MISSION_REFUGEE_Dana_Sourdough( "MISSION_REFUGEE_Dana_Sourdough" );

static const npc_class_id NC_BEGGAR_3( "NC_BEGGAR_3" );
static const npc_class_id NC_NONE( "NC_NONE" );

static const quality_id qual_COOK( "COOK" );
static const quality_id qual_CUT( "CUT" );
static const quality_id qual_SAW_W( "SAW_W" );

static const recipe_id recipe_cudgel_audit_loading_change( "cudgel_audit_loading_change" );
static const recipe_id recipe_cudgel_audit_loading_inherit( "cudgel_audit_loading_inherit" );

static const requirement_id
requirement_data_inline_recipe_brownie_weed( "inline_recipe_brownie_weed" );
static const requirement_id
requirement_data_inline_recipe_cudgel_audit_loading_self( "inline_recipe_cudgel_audit_loading_self" );
static const requirement_id
requirement_data_inline_recipe_minifridge( "inline_recipe_minifridge" );

static const snippet_id
snippet_epilogue_faction_free_merchants_0( "epilogue_faction_free_merchants_0" );
static const snippet_id
snippet_epilogue_faction_free_merchants_150( "epilogue_faction_free_merchants_150" );
static const snippet_id snippet_epilogue_faction_old_guard_0( "epilogue_faction_old_guard_0" );

static const string_id<npc_template>
npc_template_audit_loading_npc_child( "audit_loading_npc_child" );
static const string_id<npc_template>
npc_template_audit_loading_npc_early( "audit_loading_npc_early" );
static const string_id<npc_template>
npc_template_audit_loading_npc_from_abstract( "audit_loading_npc_from_abstract" );
static const string_id<npc_template>
npc_template_audit_loading_npc_parent( "audit_loading_npc_parent" );
static const string_id<npc_template> npc_template_refugee_DanaNunez( "refugee_DanaNunez" );
static const string_id<npc_template> npc_template_refugee_beggar3( "refugee_beggar3" );

static const ter_str_id ter_t_clay( "t_clay" );
static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_open_air( "t_open_air" );
static const ter_str_id ter_t_ramp_down_high( "t_ramp_down_high" );
static const ter_str_id ter_t_ramp_down_low( "t_ramp_down_low" );
static const ter_str_id ter_t_scaffolding_pipe_down( "t_scaffolding_pipe_down" );
static const ter_str_id ter_t_scaffolding_pipe_up( "t_scaffolding_pipe_up" );
static const ter_str_id ter_t_scaffolding_pipe_updown( "t_scaffolding_pipe_updown" );
static const ter_str_id ter_t_window_boarded( "t_window_boarded" );
static const ter_str_id ter_t_wood_stairs_down( "t_wood_stairs_down" );
static const ter_str_id ter_t_wood_stairs_up( "t_wood_stairs_up" );

static const trap_str_id tr_firewood_source( "tr_firewood_source" );
static const trap_str_id tr_practice_target( "tr_practice_target" );

using mission_list = std::vector<mission_type_id>;
using eoc_list = std::vector<effect_on_condition_id>;

// NPC-J02: an npc template loaded by hand and held an npc, which can't be copied, so it had no
// abstract, no copy-from of a template defined later, and no extend or delete.  It now loads
// through generic_factory.  On the old code data/mods/TEST_DATA/audit_partial_loading.json
// fails to load.
TEST_CASE( "npc-j02_npc_templates_load_through_generic_factory", "[audit][npc]" )
{
    SECTION( "shipped templates keep every field" ) {
        const npc_template &luo = npc_template_refugee_beggar3.obj();
        CHECK( luo.id == npc_template_refugee_beggar3 );
        CHECK( luo.name_unique.translated() == "Luo Meizhen" );
        CHECK( luo.name_suffix.translated() == "Mycologist" );
        CHECK( luo.temp_suffix.translated() == "Beggar" );
        CHECK( luo.gender_override == npc_template::gender::female );
        CHECK( luo.unique_portrait_filename == character_portrait_Named_NPC_luo_meizhen );
        CHECK( luo.myclass == NC_BEGGAR_3 );
        CHECK( luo.attitude == NPCATT_NULL );
        CHECK( luo.mission == NPC_MISSION_GUARD );
        CHECK( luo.fac_id == faction_lobby_beggars );
        CHECK( luo.chatbin.first_topic == "TALK_REFUGEE_BEGGAR_3" );
        CHECK( luo.chatbin.talk_friend == "TALK_FRIEND_Luo" );
        CHECK( luo.chatbin.talk_friend_guard == "TALK_FRIEND_GUARD_Luo" );
        // what the JSON doesn't give keeps its default
        CHECK( luo.chatbin.talk_leader == "TALK_LEADER" );
        CHECK( luo.snippets.snip_bye.translated() == "<end_talking_bye>" );
        CHECK( luo.miss_ids.empty() );
        CHECK( luo.death_eocs.empty() );
        CHECK( luo.age == 31 );
        CHECK( luo.height == 160 );
        CHECK( luo.str == 6 );
        CHECK( luo.dex == 7 );
        CHECK( luo.intl == 12 );
        CHECK( luo.per == 10 );
        REQUIRE( luo.personality.has_value() );
        CHECK( luo.personality->aggression == -10 );
        CHECK( luo.personality->bravery == -10 );
        CHECK( luo.personality->collector == 2 );
        CHECK( luo.personality->altruism == 1 );
        // "mission_offered" as a single string
        CHECK( npc_template_refugee_DanaNunez->miss_ids ==
               mission_list{ mission_MISSION_REFUGEE_Dana_Sourdough } );
    }
    SECTION( "a copy extends and deletes mission_offered and death_eocs" ) {
        const npc_template &parent = npc_template_audit_loading_npc_parent.obj();
        CHECK( parent.miss_ids == mission_list{ mission_MISSION_GET_INHALER,
                                                mission_MISSION_GET_ANTIBIOTICS } );
        CHECK( parent.death_eocs == eoc_list{ effect_on_condition_EOC_AUDIT_LOADING_DEATH_A,
                                              effect_on_condition_EOC_AUDIT_LOADING_DEATH_B } );
        const npc_template &child = npc_template_audit_loading_npc_child.obj();
        CHECK( child.miss_ids == mission_list{ mission_MISSION_GET_ANTIBIOTICS,
                                               mission_MISSION_GET_BLACK_BOX } );
        CHECK( child.death_eocs == eoc_list{ effect_on_condition_EOC_AUDIT_LOADING_DEATH_B,
                                             effect_on_condition_EOC_AUDIT_LOADING_DEATH_C } );
        CHECK( effect_on_condition_EOC_AUDIT_LOADING_DEATH_C.is_valid() );
        // the rest comes from the parent
        CHECK( child.myclass == NC_NONE );
        CHECK( child.chatbin.first_topic == "TALK_DONE" );
    }
    SECTION( "copy-from may name a template defined later, or an abstract one" ) {
        const npc_template &early = npc_template_audit_loading_npc_early.obj();
        CHECK( early.id == npc_template_audit_loading_npc_early );
        CHECK( early.name_suffix.translated() == "Early" );
        CHECK( early.mission == NPC_MISSION_GUARD );
        CHECK( early.str == 9 );
        CHECK( npc_template_audit_loading_npc_from_abstract->dex == 11 );
    }
}

static bool has_relation( const faction_template &fac, const std::string &other,
                          const npc_factions::relationship rel )
{
    const auto found = fac.relations.find( other );
    return found != fac.relations.end() && found->second.test( static_cast<size_t>( rel ) );
}

// NPC-J02: faction templates were a hand-made list: no abstract, no copy-from of a faction
// defined later, and extend and delete only on price_rules.  They now load through
// generic_factory (each template in an entry: a faction_template's id is the id of the live,
// saved faction), and epilogues and relations take extend and delete too.  On the old code
// data/mods/TEST_DATA/audit_partial_loading.json fails to load.
TEST_CASE( "npc-j02_faction_templates_load_through_generic_factory", "[audit][npc]" )
{
    SECTION( "shipped factions keep every field" ) {
        const faction_template *fm = faction_template::find( faction_free_merchants );
        REQUIRE( fm != nullptr );
        CHECK( fm->id == faction_free_merchants );
        CHECK( fm->get_name() == "The Free Merchants" );
        CHECK( fm->currency == itype_FMCNote );
        // five rules, then the 100% rule of the currency
        REQUIRE( fm->price_rules.size() == 6 );
        CHECK( fm->price_rules.back().itype == itype_FMCNote );
        CHECK( fm->epilogue_data.size() == 5 );
        CHECK( fm->relations.size() == 7 );
        CHECK( has_relation( *fm, "old_guard", npc_factions::relationship::watch_your_back ) );
        CHECK( fm->mon_faction == monfaction_human );
        CHECK( fm->likes_u == 30 );
        CHECK( fm->wealth == 75000000 );
        const faction_template *yours = faction_template::find( faction_your_followers );
        REQUIRE( yours != nullptr );
        CHECK( yours->mon_faction == monfaction_player );
        CHECK( yours->epilogue_data.size() == 5 );
        CHECK( faction_template::find( faction_audit_loading_no_such_faction ) == nullptr );
    }
    SECTION( "a copy extends and deletes price_rules, relations and epilogues" ) {
        const faction_template *parent = faction_template::find( faction_audit_loading_faction_parent );
        const faction_template *child = faction_template::find( faction_audit_loading_faction_child );
        REQUIRE( parent != nullptr );
        REQUIRE( child != nullptr );
        REQUIRE( parent->price_rules.size() == 2 );
        CHECK( parent->price_rules[1].category == item_category_tools );
        REQUIRE( child->price_rules.size() == 2 );
        CHECK( child->price_rules[0].itype == itype_FMCNote );
        CHECK( child->price_rules[1].category == item_category_guns );
        CHECK( child->price_rules[1].markup == 2.0 );

        CHECK( has_relation( *parent, "hells_raiders", npc_factions::relationship::kill_on_sight ) );
        CHECK( child->relations.count( "hells_raiders" ) == 0 );
        CHECK( has_relation( *child, "free_merchants", npc_factions::relationship::share_my_stuff ) );
        CHECK( has_relation( *child, "old_guard", npc_factions::relationship::watch_your_back ) );

        REQUIRE( parent->epilogue_data.size() == 2 );
        CHECK( parent->epilogue_data[1].epilogue == snippet_epilogue_faction_free_merchants_150 );
        REQUIRE( child->epilogue_data.size() == 2 );
        CHECK( child->epilogue_data[0].epilogue == snippet_epilogue_faction_free_merchants_0 );
        CHECK( child->epilogue_data[1].epilogue == snippet_epilogue_faction_old_guard_0 );

        CHECK( child->get_name() == "Audit Loading Parents" );
        CHECK( child->id == faction_audit_loading_faction_child );
    }
    SECTION( "copy-from may name a faction defined later" ) {
        const faction_template *early = faction_template::find( faction_audit_loading_faction_early );
        REQUIRE( early != nullptr );
        CHECK( early->id == faction_audit_loading_faction_early );
        CHECK( early->get_name() == "Audit Loading Late" );
        CHECK( early->likes_u == 8 );
    }
}

static bool group_has( const std::vector<std::vector<item_comp>> &groups, const size_t group,
                       const itype_id &type, const int count )
{
    if( group >= groups.size() ) {
        return false;
    }
    for( const item_comp &comp : groups[group] ) {
        if( comp.type == type && comp.count == count ) {
            return true;
        }
    }
    return false;
}

static bool any_tool( const std::vector<std::vector<tool_comp>> &groups, const itype_id &type )
{
    for( const std::vector<tool_comp> &group : groups ) {
        for( const tool_comp &tool : group ) {
            if( tool.type == type ) {
                return true;
            }
        }
    }
    return false;
}

static bool any_quality( const std::vector<std::vector<quality_requirement>> &groups,
                         const quality_id &type, const int level )
{
    for( const std::vector<quality_requirement> &group : groups ) {
        for( const quality_requirement &qual : group ) {
            if( qual.type == type && qual.level == level ) {
                return true;
            }
        }
    }
    return false;
}

static void load_recipe_json( recipe &r, const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    r.load( jo, "dda" );
}

// CRAFT-J01: a recipe with copy-from got none of its parent's inline components, tools and
// qualities, and "extend" or "delete" of them was a load error, so a mod had to restate the
// whole list to add or take away one entry.  On the old code
// data/mods/TEST_DATA/audit_partial_loading.json fails to load.
TEST_CASE( "craft-j01_copy_from_recipes_inherit_and_change_inline_requirements",
           "[audit][crafting]" )
{
    SECTION( "a copy inherits the groups it doesn't give" ) {
        const requirement_data &req = recipe_cudgel_audit_loading_inherit->simple_requirements();
        const auto &comps = req.get_components();
        REQUIRE( comps.size() == 2 );
        CHECK( group_has( comps, 0, itype_2x4, 1 ) );
        CHECK( group_has( comps, 1, itype_nail, 2 ) );
        CHECK( group_has( comps, 1, itype_rock, 1 ) );
        CHECK( any_tool( req.get_tools(), itype_hammer ) );
        CHECK( any_quality( req.get_qualities(), qual_CUT, 1 ) );
    }
    SECTION( "a copy replaces, extends and deletes groups" ) {
        const requirement_data &req = recipe_cudgel_audit_loading_change->simple_requirements();
        const auto &comps = req.get_components();
        // [2x4], [nail or rock] less nail, then [duct_tape]
        REQUIRE( comps.size() == 3 );
        CHECK( group_has( comps, 0, itype_2x4, 1 ) );
        REQUIRE( comps[1].size() == 1 );
        CHECK( group_has( comps, 1, itype_rock, 1 ) );
        CHECK( group_has( comps, 2, itype_duct_tape, 10 ) );
        CHECK( any_tool( req.get_tools(), itype_screwdriver ) );
        CHECK_FALSE( any_tool( req.get_tools(), itype_hammer ) );
        CHECK( any_quality( req.get_qualities(), qual_SAW_W, 1 ) );
        CHECK_FALSE( any_quality( req.get_qualities(), qual_CUT, 1 ) );
    }
    SECTION( "shipped copy-from recipes get what they did before" ) {
        // brownie_weed restates all three groups of brownie
        const requirement_data &weed = requirement_data_inline_recipe_brownie_weed.obj();
        REQUIRE( weed.get_components().size() == 7 );
        CHECK( group_has( weed.get_components(), 6, itype_weed, 5 ) );
        CHECK( weed.get_tools().size() == 1 );
        REQUIRE( weed.get_qualities().size() == 1 );
        CHECK( any_quality( weed.get_qualities(), qual_COOK, 3 ) );
        // minifridge gives no tools, and neither does minifreezer
        const requirement_data &fridge = requirement_data_inline_recipe_minifridge.obj();
        CHECK( fridge.get_tools().empty() );
        REQUIRE( fridge.get_components().size() == 7 );
        CHECK( group_has( fridge.get_components(), 6, itype_plastic_chunk, 8 ) );
        CHECK( fridge.get_qualities().size() == 5 );
    }
    SECTION( "a copy of itself replaces what it changes, an emptied group too" ) {
        const std::string base = R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_loading_self",
            "category": "CC_WEAPON", "subcategory": "CSC_WEAPON_BASHING",
            "skill_used": "fabrication", "difficulty": 1, "time": "1 m",
            "activity_level": "LIGHT_EXERCISE",
            "components": [ [ [ "2x4", 1 ] ], [ [ "rock", 1 ] ] ],
            "tools": [ [ [ "hammer", -1 ] ] ]
        })";
        recipe r;
        load_recipe_json( r, base );
        r.was_loaded = true;
        load_recipe_json( r, R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_loading_self",
            "delete": { "tools": [ "hammer" ], "components": [ "rock" ] }
        })" );
        const requirement_data &self = requirement_data_inline_recipe_cudgel_audit_loading_self.obj();
        CHECK( self.get_tools().empty() );
        REQUIRE( self.get_components().size() == 1 );
        CHECK( group_has( self.get_components(), 0, itype_2x4, 1 ) );
    }
    SECTION( "what can't be applied is a load error" ) {
        const std::string base = R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_loading_errors",
            "category": "CC_WEAPON", "subcategory": "CSC_WEAPON_BASHING",
            "skill_used": "fabrication", "difficulty": 1, "time": "1 m",
            "activity_level": "LIGHT_EXERCISE",
            "components": [ [ [ "2x4", 1 ] ] ]
        })";
        // Deleting an id no group has
        recipe copy;
        load_recipe_json( copy, base );
        copy.was_loaded = true;
        CHECK_THROWS_AS( load_recipe_json( copy, R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_loading_errors_child",
            "delete": { "qualities": [ "CUT" ] }
        })" ), JsonError );
        // Without copy-from there is nothing to extend
        recipe fresh;
        CHECK_THROWS_AS( load_recipe_json( fresh, R"({
            "type": "recipe", "result": "cudgel", "id_suffix": "audit_loading_errors_fresh",
            "category": "CC_WEAPON", "subcategory": "CSC_WEAPON_BASHING",
            "skill_used": "fabrication", "difficulty": 1, "time": "1 m",
            "activity_level": "LIGHT_EXERCISE",
            "components": [ [ [ "2x4", 1 ] ] ],
            "extend": { "components": [ [ [ "rock", 1 ] ] ] }
        })" ), JsonError );
    }
}

// Finishes con at site with u as the builder, as if all the work but the last turn were done.
static void finish_construction( avatar &u, const construction_str_id &con,
                                 const tripoint_bub_ms &site, const std::list<item> &components = {} )
{
    map &here = get_map();
    for( const std::pair<const skill_id, int> &skill : con->required_skills ) {
        u.set_skill_level( skill.first, skill.second );
    }
    partial_con pc;
    pc.id = con.id();
    pc.counter = 9999999;
    pc.components = components;
    here.partial_con_set( site, pc );
    u.assign_activity( build_construction_activity_actor( here.get_abs( site ) ) );
    REQUIRE( process_activity_bounded( u, 10, 100 ) );
}

static bool has_message( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

static int count_items( const tripoint_bub_ms &p, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            count += it.count();
        }
    }
    return count;
}

// TERR-J01: the vanilla constructions that set a fixed terrain above, set a trap, leave the
// curtains, use up a clay or sand deposit or place an appliance did it through hardcoded
// post_special functions.  They now use post_eocs, byproducts and "place_appliance", and
// building each must give what the post_special gave.  On the old code they have no post_eocs
// and construction has no place_appliance.
TEST_CASE( "terr-j01_vanilla_post_specials_moved_to_json_build_the_same",
           "[audit][construction]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::turn_zero + 1_days;
    clear_avatar();
    clear_map( -2, 1 );
    clear_vehicles();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms site = u.pos_bub() + tripoint::east;
    const tripoint_bub_ms above = site + tripoint::above;
    Messages::clear_messages();

    SECTION( "ramps and stairs set the tile above" ) {
        const auto check_above = [&]( const construction_str_id & con, const ter_str_id & top ) {
            CAPTURE( con.str() );
            CHECK( con->post_eocs.size() == 1 );
            here.ter_set( above, ter_t_open_air );
            finish_construction( u, con, site );
            CHECK( here.ter( site ) == ter_str_id( con->post_terrain ) );
            CHECK( here.ter( above ) == top );
        };
        check_above( construction_constr_concrete_ramp_low, ter_t_ramp_down_low );
        check_above( construction_constr_earth_ramp_low, ter_t_ramp_down_low );
        check_above( construction_constr_concrete_ramp_high, ter_t_ramp_down_high );
        check_above( construction_constr_earth_ramp_high, ter_t_ramp_down_high );
        check_above( construction_constr_wood_stairs_up, ter_t_wood_stairs_down );
        check_above( construction_constr_repair_wood_stairs_up, ter_t_wood_stairs_down );
        CHECK( here.ter( site ) == ter_t_wood_stairs_up );
    }
    SECTION( "removing scaffolding clears the tile above" ) {
        for( const construction_str_id &con : {
                 construction_remove_scaffolding_pipe_up, construction_remove_scaffolding_pipe_down_above_updown
             } ) {
            CAPTURE( con.str() );
            CHECK( con->post_eocs.size() == 1 );
            here.ter_set( site, con == construction_remove_scaffolding_pipe_up ? ter_t_scaffolding_pipe_up :
                          ter_t_scaffolding_pipe_updown );
            here.ter_set( above, ter_t_scaffolding_pipe_down );
            finish_construction( u, con, site );
            CHECK( here.ter( above ) == ter_t_open_air );
        }
    }
    SECTION( "the firewood and practice target marks set their trap on the tile" ) {
        finish_construction( u, construction_constr_firewood_source, site );
        CHECK( here.tr_at( site ).id == tr_firewood_source );
        here.remove_trap( site );
        finish_construction( u, construction_constr_practice_target, site );
        CHECK( here.tr_at( site ).id == tr_practice_target );
        here.remove_trap( site );
    }
    SECTION( "boarding up a curtained window leaves the curtains at the builder's feet" ) {
        here.i_clear( u.pos_bub() );
        finish_construction( u, construction_constr_window_boarded_curtains, site );
        CHECK( here.ter( site ) == ter_t_window_boarded );
        CHECK( count_items( u.pos_bub(), itype_nail ) == 4 );
        CHECK( count_items( u.pos_bub(), itype_sheet ) == 2 );
        CHECK( count_items( u.pos_bub(), itype_stick ) == 1 );
        CHECK( count_items( u.pos_bub(), itype_string_36 ) == 1 );
        CHECK( has_message( "After boarding up the window the curtains and curtain rod are left." ) );
    }
    SECTION( "extracting clay may use the deposit up and says what was gathered" ) {
        bool used_up = false;
        bool kept = false;
        for( int i = 0; i < 200 && !( used_up && kept ); ++i ) {
            here.ter_set( site, ter_t_clay );
            here.i_clear( u.pos_bub() );
            Messages::clear_messages();
            finish_construction( u, construction_constr_extract_clay, site );
            if( here.ter( site ) == ter_t_clay ) {
                kept = true;
                CHECK( has_message( "You gather some clay." ) );
            } else {
                REQUIRE( here.ter( site ) == ter_t_dirt );
                used_up = true;
                CHECK( has_message( "You gather some materials." ) );
            }
        }
        CHECK( kept );
        CHECK( used_up );
    }
    SECTION( "an appliance construction places its component as an appliance" ) {
        const construction &con = construction_ap_box_battery_charger.obj();
        CHECK( con.place_appliance );
        std::list<item> components;
        components.emplace_back( itype_box_battery_charger, calendar::turn );
        finish_construction( u, construction_ap_box_battery_charger, site, components );
        CHECK( here.partial_con_at( site ) == nullptr );
        const optional_vpart_position vp = here.veh_at( site );
        REQUIRE( vp.has_value() );
        CHECK( vp->vehicle().is_appliance() );
        clear_vehicles();
    }
    here.i_clear( u.pos_bub() );
}

// TERR-J01: "place_appliance" and the post_special done_appliance on one construction would
// place the appliance twice; that is a load error.
TEST_CASE( "terr-j01_place_appliance_with_done_appliance_is_reported", "[audit][construction]" )
{
    construction con;
    con.id = construction_audit_loading_appliance_twice;
    JsonObject jo = json_loader::from_string( R"({
        "group": "place_ap_box_battery_charger", "category": "APPLIANCE",
        "required_skills": [ [ "fabrication", 1 ] ], "time": "2 m",
        "components": [ [ [ "box_battery_charger", 1 ] ] ], "pre_special": "check_empty",
        "post_special": "done_appliance", "place_appliance": true
    })" );
    jo.allow_omitted_members();
    con.load( jo, "dda" );
    CHECK_THAT( capture_debugmsg_during( [&con]() {
        con.check();
    } ), Catch::Matchers::Contains( "place the appliance twice" ) );
}

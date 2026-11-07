#include <plf/list.h>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "basecamp.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "event.h"
#include "event_bus.h"
#include "faction.h"
#include "faction_camp.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_iterator.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "mission_companion.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "regional_settings.h"
#include "shop_cons_rate.h"
#include "stomach.h"
#include "talker.h"
#include "translation.h"
#include "type_id.h"
#include "value_ptr.h"
#include "visitable.h"

static const faction_id faction_free_merchants( "free_merchants" );
static const faction_id faction_your_followers( "your_followers" );

static const faction_mission_id faction_mission_camp_clearcut( "camp_clearcut" );
static const faction_mission_id faction_mission_camp_cut_logs( "camp_cut_logs" );
static const faction_mission_id faction_mission_camp_plant( "camp_plant" );
static const faction_mission_id faction_mission_camp_recruiting( "camp_recruiting" );
static const faction_mission_id faction_mission_camp_setup_hide_site( "camp_setup_hide_site" );
static const faction_mission_id faction_mission_camp_survey_field( "camp_survey_field" );
static const faction_mission_id
faction_mission_test_m22b_deadly_fishing( "test_m22b_deadly_fishing" );
static const faction_mission_id faction_mission_test_m22b_recruiting( "test_m22b_recruiting" );
static const faction_mission_id
faction_mission_test_m22b_regional_fishing( "test_m22b_regional_fishing" );
static const faction_mission_id faction_mission_test_m22b_safe_fishing( "test_m22b_safe_fishing" );
static const faction_mission_id
faction_mission_test_m22b_template_recruiting( "test_m22b_template_recruiting" );

static const flag_id json_flag_NO_CAMP_FARMING( "NO_CAMP_FARMING" );

static const furn_str_id furn_f_plant_seed( "f_plant_seed" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_fish( "fish" );
static const itype_id itype_fungal_seeds( "fungal_seeds" );
static const itype_id itype_marloss_seed( "marloss_seed" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_seed_cattail( "seed_cattail" );
static const itype_id itype_seed_hops( "seed_hops" );

static const npc_class_id NC_HUNTER( "NC_HUNTER" );

static const npc_template_id npc_template_test_talker( "test_talker" );

static const oter_str_id oter_field( "field" );

static const oter_type_str_id oter_type_field( "field" );
static const oter_type_str_id oter_type_forest( "forest" );
static const oter_type_str_id oter_type_forest_water( "forest_water" );
static const oter_type_str_id oter_type_rural_road( "rural_road" );
static const oter_type_str_id oter_type_rural_road_forest( "rural_road_forest" );

static const region_settings_id region_settings_default( "default" );

static const shopkeeper_blacklist_id
shopkeeper_blacklist_test_m22b_blacklist( "test_m22b_blacklist" );

static const skill_id skill_speech( "speech" );
static const skill_id skill_survival( "survival" );

static const ter_str_id ter_t_dirtmound( "t_dirtmound" );
static const ter_str_id ter_t_grass( "t_grass" );

static const vitamin_id vitamin_calcium( "calcium" );

static constexpr tripoint_bub_ms camp_mid{ MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 };
// the test dock's expansion
static constexpr point_rel_omt dock_dir{ 0, -1 };

namespace
{

// player-owned bare bones camp at mid-map; caller removes it with clear_camps
basecamp &place_m22b_camp( tripoint_abs_omt &camp_omt )
{
    map &here = get_map();
    camp_omt = project_to<coords::omt>( here.get_abs( camp_mid ) );
    here.add_camp( camp_omt, "faction_camp" );
    std::optional<basecamp *> bcp = overmap_buffer.find_camp( camp_omt.xy() );
    REQUIRE( bcp );
    basecamp &camp = **bcp;
    camp.set_owner( faction_your_followers );
    camp.define_camp( camp_omt, "faction_base_bare_bones_NPC_camp_0", false );
    return camp;
}

void stock_m22b_larder( faction &fac, const int kcal )
{
    fac.empty_food_supply();
    nutrients stock;
    stock.calories = kcal * 1000;
    fac.debug_food_supply().emplace_back( calendar::turn_zero, stock );
    REQUIRE( fac.food_supply().kcal() == kcal );
}

// follower who can be sent on camp missions, as the overmap buffer holds it
npc_ptr spawn_m22b_worker( const point_bub_ms &where )
{
    npc &guy = spawn_npc( where, "test_talker" );
    clear_character( guy, true );
    guy.set_fac( faction_your_followers );
    guy.stomach.empty();
    guy.guts.empty();
    const npc_ptr comp = overmap_buffer.find_npc( guy.getID() );
    REQUIRE( comp );
    return comp;
}

// everything of @ref type on the reality bubble's ground level except @ref skipped, and
// carried by the avatar
int count_back( const itype_id &type, const std::optional<tripoint_bub_ms> &skipped = std::nullopt )
{
    map &here = get_map();
    int found = 0;
    for( const tripoint_bub_ms &p : here.points_on_zlevel( 0 ) ) {
        if( skipped && p == *skipped ) {
            continue;
        }
        for( const item &it : here.i_at( p ) ) {
            if( it.typeId() == type ) {
                found += it.count();
            }
        }
    }
    get_avatar().visit_items( [&found, &type]( const item_location & it ) {
        if( it->typeId() == type ) {
            found += it->count();
        }
        return VisitResponse::NEXT;
    } );
    return found;
}

int count_at( const tripoint_bub_ms &p, const itype_id &type )
{
    int found = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            found += it.count();
        }
    }
    return found;
}

double global_number( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

std::string global_string( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    return val == nullptr ? std::string() : val->to_string();
}

// evaluates (or assigns with) @ref expression in @ref d
double run_math( dialogue &d, const std::string_view expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

} // namespace

// CAMP-J05: companions planted any seed on any dirt mound, although seed_cattail needs shallow
// water there, and only the hardcoded Marloss and fungal seeds were left out.  A plot takes only
// the seeds its ground suits, and seeds with NO_CAMP_FARMING are never planted; the rest come
// back to camp.
TEST_CASE( "camp-j05_camp_planting_respects_the_seeds_json", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    restore_on_out_of_scope restore_turn( calendar::turn );
    map &here = get_map();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22b_camp( camp_omt );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_m22b_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        comp->reset_companion_mission();
        comp->companion_mission_inv.clear();
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_m22b_larder( fac, 10000 );

    REQUIRE( item( itype_marloss_seed ).has_flag( json_flag_NO_CAMP_FARMING ) );
    REQUIRE( item( itype_fungal_seeds ).has_flag( json_flag_NO_CAMP_FARMING ) );
    REQUIRE_FALSE( item( itype_seed_hops ).has_flag( json_flag_NO_CAMP_FARMING ) );
    REQUIRE( item( itype_seed_cattail ).type->seed->required_terrain_flag ==
             ter_furn_flag::TFLAG_SHALLOW_WATER );

    // one dirt mound in the camp's own tile
    const tripoint_bub_ms plot = camp_mid + tripoint::west * 2;
    here.ter_set( plot, ter_t_dirtmound );
    const mission_id plant = faction_mission_camp_plant->mission_at( base_camps::base_dir );
    // the old code planted the last seed it was given, here the water seed
    item hops( itype_seed_hops );
    item cattail( itype_seed_cattail );
    item marloss( itype_marloss_seed );
    const std::vector<item *> equipment{ &marloss, &hops, &cattail };
    REQUIRE( camp.start_mission( plant, 3_minutes, true, "", false, equipment,
                                 std::map<skill_id, int>(), LIGHT_EXERCISE, 0_hours, comp ) );
    calendar::turn += 3_minutes;
    REQUIRE( camp.farm_return( plant, base_camps::base_dir ) );

    CHECK( here.furn( plot ) == furn_f_plant_seed );
    CHECK( count_at( plot, itype_seed_hops ) == 1 );
    CHECK( count_at( plot, itype_seed_cattail ) == 0 );
    CHECK( count_at( plot, itype_marloss_seed ) == 0 );
    // what wasn't planted is back at camp
    CHECK( count_back( itype_seed_cattail, plot ) == 1 );
    CHECK( count_back( itype_marloss_seed, plot ) == 1 );
    CHECK( count_back( itype_seed_hops, plot ) == 0 );
    CHECK( comp->companion_mission_inv.empty() );
}

// CAMP-J06: recruits were always classless random NPCs and the rules were C++.  A recruiting
// mission's "recruit" picks the class or template, and its bonuses add to the camp's score.
TEST_CASE( "camp-j06_recruiting_finds_the_json_classes_and_templates", "[audit][camp]" )
{
    const faction_mission &hunters = faction_mission_test_m22b_recruiting.obj();
    REQUIRE( hunters.behavior == Camp_Recruiting );
    REQUIRE( hunters.recruit.has_value() );
    CHECK( hunters.recruit->find_difficulty == 12 );
    // not given: the vanilla rules
    CHECK( hunters.recruit->find_skill == skill_survival );
    CHECK( hunters.recruit->convince_skill == skill_speech );
    CHECK( hunters.recruit->convince_difficulty == 19 );
    CHECK( hunters.recruit->join_difficulty == 10 );

    const npc_ptr hunter = talk_function::camp_new_recruit( *hunters.recruit );
    REQUIRE( hunter );
    CHECK( hunter->myclass == NC_HUNTER );

    const faction_mission &templated = faction_mission_test_m22b_template_recruiting.obj();
    REQUIRE( templated.recruit.has_value() );
    const npc_ptr tester = talk_function::camp_new_recruit( *templated.recruit );
    REQUIRE( tester );
    // the template made it a member of its faction; this one never joins the game
    on_out_of_scope leave_faction( [&tester]() {
        if( faction *fac = tester->get_faction() ) {
            fac->remove_member( tester->getID() );
        }
    } );
    CHECK( tester->idz == npc_template_test_talker );

    // vanilla recruiting has no "recruit": classless, as before
    const faction_mission &vanilla = faction_mission_camp_recruiting.obj();
    CHECK_FALSE( vanilla.recruit.has_value() );
    const npc_ptr anyone = talk_function::camp_new_recruit( camp_mission_recruiting() );
    REQUIRE( anyone );
    CHECK( anyone->myclass == npc_class_id::NULL_ID() );
    // the companion is picked by the social skill its description names
    CHECK( vanilla.skill_used == skill_speech );
}

TEST_CASE( "camp-j06_recruiting_bonuses_add_to_the_camp_score", "[audit][camp]" )
{
    clear_avatar();
    clear_map_without_vision();
    get_player_character().camps.clear();
    get_globals().clear_global_values();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22b_camp( camp_omt );
    on_out_of_scope cleanup( [&camp_omt]() {
        overmap_buffer.clear_camps( camp_omt.xy() );
        get_globals().clear_global_values();
    } );
    const faction_mission &vanilla = faction_mission_camp_recruiting.obj();
    const faction_mission &hunters = faction_mission_test_m22b_recruiting.obj();

    CHECK( camp.recruit_evaluation( hunters ) == camp.recruit_evaluation( vanilla ) );
    get_globals().set_global_value( "test_m22b_famous", 1 );
    CHECK( camp.recruit_evaluation( hunters ) == camp.recruit_evaluation( vanilla ) + 7 );
    // the description shows the mission's own time and positions
    const std::string desc = camp.recruit_description( hunters, 0 );
    CHECK( desc.find( "Time: " + to_string( 1_days ) ) != std::string::npos );
    CHECK( desc.find( "Positions: 0/1" ) != std::string::npos );
}

// CAMP-J08: EOCs could only read the larder.  faction_food_supply() can be assigned, and the
// camp_ functions read a camp's provides, expansions, assignees and workers.
TEST_CASE( "camp-j08_faction_food_supply_can_be_changed_by_math", "[audit][camp]" )
{
    clear_avatar();
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::turn_zero + 2_days;
    faction &fac = *get_player_character().get_faction();
    REQUIRE( fac.id == faction_your_followers );
    fac.empty_food_supply();
    on_out_of_scope cleanup( [&fac]() {
        fac.empty_food_supply();
    } );
    dialogue d( get_talker_for( get_avatar() ), nullptr );

    run_math( d, "faction_food_supply('your_followers') += 50000" );
    CHECK( fac.food_supply().calories == 50000 );
    CHECK( run_math( d, "faction_food_supply('your_followers')" ) == Approx( 50000 ) );
    run_math( d, "faction_food_supply('your_followers') -= 20000" );
    CHECK( fac.food_supply().calories == 30000 );
    // more than there is takes what there is
    run_math( d, "faction_food_supply('your_followers') -= 1000000" );
    CHECK( fac.food_supply().calories == 0 );
    run_math( d, "faction_food_supply('your_followers') = 40000" );
    CHECK( fac.food_supply().calories == 40000 );

    // food that goes bad
    run_math( d, "faction_food_supply('your_followers', 'expires_in': time('1 h')) += 10000" );
    CHECK( fac.food_supply().calories == 50000 );
    calendar::turn += 2_hours;
    CHECK( fac.food_supply().calories == 40000 );

    // a vitamin
    run_math( d, "faction_food_supply('your_followers', 'vitamin': 'calcium') = 120" );
    CHECK( fac.food_supply().get_vitamin( vitamin_calcium ) == 120 );
    CHECK( fac.food_supply().calories == 40000 );
}

TEST_CASE( "camp-j08_math_reads_a_camps_state", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    restore_on_out_of_scope restore_turn( calendar::turn );
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22b_camp( camp_omt );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_m22b_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        if( npc_ptr guy = overmap_buffer.find_npc( comp->getID() ) ) {
            guy->reset_companion_mission();
            guy->companion_mission_inv.clear();
            guy->assigned_camp.reset();
        }
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_m22b_larder( fac, 10000 );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    d.set_value( "camp", project_to<coords::ms>( camp_omt ) );

    CHECK( run_math( d, "camp_expansion_count('location': _camp)" ) == 0 );
    CHECK( run_math( d, "camp_provides('test_m22b_jetty', 'location': _camp)" ) == 0 );
    camp.add_expansion( "test_m22b_dock", camp_omt + dock_dir, dock_dir );
    CHECK( run_math( d, "camp_expansion_count('location': _camp)" ) == 1 );
    CHECK( run_math( d, "camp_provides('test_m22b_jetty', 'location': _camp)" ) == 2 );
    CHECK( run_math( d, "camp_provides('test_m22b_nothing', 'location': _camp)" ) == 0 );

    CHECK( run_math( d, "camp_assigned_npcs('location': _camp)" ) == 0 );
    camp.add_assignee( comp->getID() );
    CHECK( run_math( d, "camp_assigned_npcs('location': _camp)" ) == 1 );

    CHECK( run_math( d, "camp_workers('', 'location': _camp)" ) == 0 );
    const faction_mission &fishing = faction_mission_test_m22b_regional_fishing.obj();
    REQUIRE( camp.start_camp_mission( fishing, fishing.mission_at( dock_dir ), comp ) == comp );
    CHECK( run_math( d, "camp_workers('test_m22b_regional_fishing', 'location': _camp)" ) == 1 );
    CHECK( run_math( d, "camp_workers('', 'location': _camp)" ) == 1 );
    CHECK( run_math( d, "camp_workers('camp_foraging', 'location': _camp)" ) == 0 );

    // no camp there
    d.set_value( "nowhere", project_to<coords::ms>( camp_omt + tripoint( 20, 20, 0 ) ) );
    CHECK( run_math( d, "camp_expansion_count('location': _nowhere)" ) == 0 );
}

// CAMP-J09: what a gathering mission brought back and its encounters were C++.  Results can
// differ by region, and an encounter can be an EOC that decides whether the companion lives.
TEST_CASE( "camp-j09_regional_results_and_json_encounters", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22b_camp( camp_omt );
    camp.add_expansion( "test_m22b_dock", camp_omt + dock_dir, dock_dir );
    faction &fac = *get_player_character().get_faction();
    npc_ptr comp = spawn_m22b_worker( camp_mid.xy() + point::south );
    const character_id comp_id = comp->getID();
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp_id]() {
        if( npc_ptr guy = overmap_buffer.find_npc( comp_id ) ) {
            guy->reset_companion_mission();
            guy->companion_mission_inv.clear();
        }
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
        get_globals().clear_global_values();
    } );
    stock_m22b_larder( fac, 10000 );
    // the tests' world is the default region
    CAPTURE( overmap_buffer.get_settings( camp_omt ).id.str() );
    REQUIRE( overmap_buffer.get_settings( camp_omt ).id == region_settings_default );

    SECTION( "a camp in a region with its own results brings those back" ) {
        const faction_mission &fishing = faction_mission_test_m22b_regional_fishing.obj();
        CHECK( &fishing.results_at( camp_omt ) == &fishing.regional_results.at( region_settings_default ) );
        const mission_id id = fishing.mission_at( dock_dir );
        REQUIRE( camp.start_camp_mission( fishing, id, comp ) == comp );
        calendar::turn += 1_hours + 1_turns;
        camp.handle_mission( { id, true } );
        CHECK_FALSE( comp->has_companion_mission() );
        CHECK( count_back( itype_fish ) == 3 * item( itype_fish ).count() );
        CHECK( count_back( itype_2x4 ) == 0 );
    }
    SECTION( "an encounter EOC that the companion survives, then the results" ) {
        const faction_mission &fishing = faction_mission_test_m22b_safe_fishing.obj();
        const mission_id id = fishing.mission_at( dock_dir );
        REQUIRE( camp.start_camp_mission( fishing, id, comp ) == comp );
        calendar::turn += 1_hours + 1_turns;
        camp.handle_mission( { id, true } );
        CHECK( global_number( "test_m22b_safe_threat" ) == 4 );
        CHECK_FALSE( comp->has_companion_mission() );
        CHECK( count_back( itype_2x4 ) == 2 );
    }
    SECTION( "an encounter EOC that sets encounter_survived to 0 kills the companion" ) {
        const faction_mission &fishing = faction_mission_test_m22b_deadly_fishing.obj();
        const mission_id id = fishing.mission_at( dock_dir );
        REQUIRE( camp.start_camp_mission( fishing, id, comp ) == comp );
        calendar::turn += 1_hours + 1_turns;
        camp.handle_mission( { id, true } );
        CHECK( global_number( "test_m22b_deadly_encounter" ) == 1 );
        CHECK( global_string( "test_m22b_deadly_task" ) == "fishing in a storm" );
        CHECK( comp->is_dead() );
        CHECK( overmap_buffer.find_npc( comp_id ) == nullptr );
        CHECK( count_back( itype_2x4 ) == 0 );
    }
}

// CAMP-J10: logging, clear-cutting and hide sites only knew seven hardcoded forest types, and
// field surveys ten hardcoded terrains.  The missions name them in JSON, and a behavior that
// needs them is a load error without them.
TEST_CASE( "camp-j10_camp_mission_targets_come_from_json", "[audit][camp]" )
{
    const faction_mission &logs = faction_mission_camp_cut_logs.obj();
    CHECK( logs.target_terrains.size() == 7 );
    CHECK( logs.cleared_terrains.at( oter_type_forest ) == oter_type_field );
    CHECK( logs.cleared_terrains.at( oter_type_rural_road_forest ) == oter_type_rural_road );
    CHECK( faction_mission_camp_clearcut->target_terrains == logs.target_terrains );
    const std::vector<oter_type_str_id> &hide = faction_mission_camp_setup_hide_site->target_terrains;
    CHECK( hide.size() == 9 );
    CHECK( std::find( hide.begin(), hide.end(), oter_type_forest_water ) != hide.end() );
    const faction_mission &survey = faction_mission_camp_survey_field.obj();
    CHECK( survey.field_terrain == oter_field );
    CHECK( survey.field_terrains.size() == 10 );
    CHECK( std::find( survey.field_terrains.begin(), survey.field_terrains.end(),
                      ter_t_grass ) != survey.field_terrains.end() );

    faction_mission untargeted;
    JsonObject jo = json_loader::from_string(
                        R"({ "name": "Logs", "desc": "Logs.", "behavior": "cut_logs" })" ).get_object();
    jo.allow_omitted_members();
    untargeted.load( jo, "test" );
    const std::string errors = capture_debugmsg_during( [&untargeted]() {
        untargeted.check();
    } );
    CHECK( errors.find( "needs target_terrains" ) != std::string::npos );
}

// CAMP-J11: the companion_mission effect silently did nothing for a role it didn't know.
TEST_CASE( "camp-j11_unknown_companion_mission_role_is_reported", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    npc &guy = spawn_npc( camp_mid.xy() + point::east, "test_talker" );
    guy.companion_mission_role_id = "TEST_M22B_NO_SUCH_ROLE";
    const std::string errors = capture_debugmsg_during( [&guy]() {
        talk_function::companion_mission( guy );
    } );
    CHECK( errors.find( "TEST_M22B_NO_SUCH_ROLE" ) != std::string::npos );
    clear_npcs();
}

// CAMP-J12: shop and price rule conditions couldn't see the traded item, price rule values were
// fixed numbers, nothing followed a trade, and a typo'd id silently matched nothing.
TEST_CASE( "camp-j12_shop_conditions_see_the_traded_item", "[audit][npc]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    npc &trader = spawn_npc( camp_mid.xy() + point::east, "test_talker" );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    const shopkeeper_blacklist &blacklist = shopkeeper_blacklist_test_m22b_blacklist.obj();

    item plank( itype_2x4 );
    CHECK( blacklist.matches( plank, trader ) == nullptr );
    plank.set_owner( faction_free_merchants );
    const icg_entry *stolen = blacklist.matches( plank, trader );
    REQUIRE( stolen != nullptr );
    CHECK( stolen->message.translated() == "stolen" );

    item few_nails( itype_nail, calendar::turn, 5 );
    item many_nails( itype_nail, calendar::turn, 12 );
    REQUIRE( many_nails.count() == 12 );
    CHECK( blacklist.matches( few_nails, trader ) == nullptr );
    CHECK( blacklist.matches( many_nails, trader ) != nullptr );

    // the context the conditions see
    const const_dialogue d = icg_entry::trade_dialogue( many_nails, trader );
    CHECK( d.get_value( "trade_item" ).str() == "nail" );
    CHECK( d.get_value( "trade_item_count" ).dbl() == 12 );
    CHECK( d.get_value( "trade_item_rotten" ).dbl() == 0 );
}

TEST_CASE( "camp-j12_price_rule_values_can_be_math", "[audit][npc]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    npc &trader = spawn_npc( camp_mid.xy() + point::east, "test_talker" );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    JsonValue jv = json_loader::from_string(
                       R"({ "item": "nail", "markup": { "math": [ "_trade_item_count * 2" ] }, "premium": 3 })" );
    const faction_price_rule rule = faction_price_rules_reader::get_next( jv );
    // math in data is parsed once loading is done
    finalize_conditions();
    REQUIRE_FALSE( rule.markup.is_constant() );
    REQUIRE( rule.premium.is_constant() );
    const item nails( itype_nail, calendar::turn, 4 );
    CHECK( faction_price_rule::value_for( rule.markup, nails, trader ) == Approx( 8 ) );
    CHECK( faction_price_rule::value_for( rule.premium, nails, trader ) == Approx( 3 ) );
    CHECK_FALSE( rule.fixed_adj.has_value() );
}

TEST_CASE( "camp-j12_shop_ids_are_checked", "[audit][npc]" )
{
    const JsonObject jo = json_loader::from_string(
                              R"({ "item": "test_m22b_no_such_item", "category": "test_m22b_no_such_category", "group": "test_m22b_no_such_group" })" ).get_object();
    const icg_entry entry = icg_entry_reader::_part_get_next( jo );
    const std::string errors = capture_debugmsg_during( [&entry]() {
        entry.check( "test entry" );
    } );
    CHECK( errors.find( "test_m22b_no_such_item" ) != std::string::npos );
    CHECK( errors.find( "test_m22b_no_such_category" ) != std::string::npos );
    CHECK( errors.find( "test_m22b_no_such_group" ) != std::string::npos );
}

TEST_CASE( "camp-j12_avatar_trades_event_reaches_eocs", "[audit][npc]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_globals().clear_global_values();
    npc &trader = spawn_npc( camp_mid.xy() + point::east, "test_talker" );
    on_out_of_scope cleanup( []() {
        clear_npcs();
        get_globals().clear_global_values();
    } );
    get_event_bus().send_with_talker( &get_avatar(), &trader,
                                      cata::event::make<event_type::avatar_trades>( trader.getID(), trader.get_fac_id(), 150, 400 ) );
    CHECK( global_number( "test_m22b_trade_given" ) == 150 );
    CHECK( global_number( "test_m22b_trade_received" ) == 400 );
    CHECK( global_string( "test_m22b_trade_faction" ) == trader.get_fac_id().str() );
}

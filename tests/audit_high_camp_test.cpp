#include <plf/list.h>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "basecamp.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "faction.h"
#include "faction_camp.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "global_vars.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_iterator.h"
#include "map_scale_constants.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "memory_fast.h"
#include "mission_companion.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "skill.h"
#include "stomach.h"
#include "translation.h"
#include "type_id.h"

static const faction_id faction_your_followers( "your_followers" );

static const faction_mission_id faction_mission_camp_collect_firewood( "camp_collect_firewood" );
static const faction_mission_id faction_mission_camp_cut_logs( "camp_cut_logs" );
static const faction_mission_id faction_mission_camp_foraging( "camp_foraging" );
static const faction_mission_id faction_mission_camp_gathering( "camp_gathering" );
static const faction_mission_id faction_mission_camp_hunting( "camp_hunting" );
static const faction_mission_id faction_mission_camp_plant( "camp_plant" );
static const faction_mission_id faction_mission_camp_recruiting( "camp_recruiting" );
static const faction_mission_id faction_mission_camp_scouting( "camp_scouting" );
static const faction_mission_id faction_mission_camp_trapping( "camp_trapping" );
static const faction_mission_id
faction_mission_test_camp_deep_sea_fishing( "test_camp_deep_sea_fishing" );
static const faction_mission_id faction_mission_test_camp_fishing( "test_camp_fishing" );
static const faction_mission_id
faction_mission_test_camp_fishing_all_day( "test_camp_fishing_all_day" );

static const furn_str_id furn_f_plant_seed( "f_plant_seed" );

static const item_group_id
Item_spawn_data_foraging_faction_camp_summer( "foraging_faction_camp_summer" );
static const item_group_id
Item_spawn_data_foraging_faction_camp_winter( "foraging_faction_camp_winter" );
static const item_group_id Item_spawn_data_forest( "forest" );
static const item_group_id
Item_spawn_data_gathering_faction_camp_firewood( "gathering_faction_camp_firewood" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_fish( "fish" );
static const itype_id itype_seed_hops( "seed_hops" );

static const mongroup_id GROUP_CAMP_HUNTING( "GROUP_CAMP_HUNTING" );
static const mongroup_id GROUP_CAMP_HUNTING_LARGE( "GROUP_CAMP_HUNTING_LARGE" );
static const mongroup_id GROUP_CAMP_TRAPPING( "GROUP_CAMP_TRAPPING" );

static const skill_id skill_gun( "gun" );
static const skill_id skill_survival( "survival" );
static const skill_id skill_traps( "traps" );

static const ter_str_id ter_t_dirtmound( "t_dirtmound" );

static constexpr tripoint_bub_ms camp_mid{ MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 };
// the test fishing dock's expansion
static constexpr point_rel_omt dock_dir{ 0, -1 };

namespace
{

// player-owned bare bones camp at mid-map; caller removes it with clear_camps
basecamp &place_j02_camp( tripoint_abs_omt &camp_omt )
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

void stock_j02_larder( faction &fac, const int kcal )
{
    fac.empty_food_supply();
    nutrients stock;
    stock.calories = kcal * 1000;
    fac.debug_food_supply().emplace_back( calendar::turn_zero, stock );
    REQUIRE( fac.food_supply().kcal() == kcal );
}

// follower who can be sent on camp missions, as the overmap buffer holds it
npc_ptr spawn_j02_worker( const point_bub_ms &where )
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

// everything of @ref type on the reality bubble's ground level, by count
int count_on_map( const itype_id &type )
{
    map &here = get_map();
    int found = 0;
    for( const tripoint_bub_ms &p : here.points_on_zlevel( 0 ) ) {
        for( const item &it : here.i_at( p ) ) {
            if( it.typeId() == type ) {
                found += it.count();
            }
        }
    }
    return found;
}

// the first day from turn_zero on in @ref season
time_point first_day_of( const season_type season )
{
    time_point when = calendar::turn_zero;
    while( season_of_year( when ) != season ) {
        when += 1_days;
    }
    return when + 12_hours;
}

std::string global_string( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    return val == nullptr ? std::string() : val->to_string();
}

double global_number( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

// the menu entry of camp mission @ref type that sends (or, with @ref ret, recovers) a companion
const mission_entry *find_entry( const mission_data &missions, const faction_mission_id &type,
                                 const bool ret )
{
    for( const std::vector<mission_entry> &tab : missions.entries ) {
        for( const mission_entry &e : tab ) {
            if( e.id.id.type == type && e.id.ret == ret ) {
                return &e;
            }
        }
    }
    return nullptr;
}

mission_data offered_at( basecamp &camp, const point_rel_omt &dir )
{
    camp.reset_camp_workers();
    camp.form_crafting_inventory( get_map() );
    mission_data missions;
    camp.get_available_missions_by_dir( missions, dir );
    return missions;
}

// as the overmap loads an NPC: a new object from the save
npc_ptr saved_and_loaded( const npc_ptr &comp )
{
    std::ostringstream os;
    JsonOut jsout( os );
    comp->serialize( jsout );
    shared_ptr_fast<npc> loaded = make_shared_fast<npc>();
    loaded->deserialize( json_loader::from_string( os.str() ).get_object() );
    loaded->set_fac( loaded->get_fac_id() );
    REQUIRE( loaded->getID() == comp->getID() );
    overmap_buffer.remove_npc( comp->getID() );
    overmap_buffer.insert_npc( loaded );
    return loaded;
}

mission_id loaded_mission_id( const mission_id &saved )
{
    std::ostringstream os;
    JsonOut jsout( os );
    saved.serialize( jsout );
    mission_id loaded;
    loaded.deserialize( json_loader::from_string( os.str() ) );
    return loaded;
}

} // namespace

// CAMP-J02: camp missions were a closed C++ enum, and faction_mission only gave their text.
// A mission defined only in JSON is offered where the camp has its provide, sends a companion
// for its duration and feeds it for that, runs its EOCs, survives a save while out, and brings
// back its results (the item group of the season) and training when it is recovered on time.
TEST_CASE( "camp-j02_json_camp_mission_runs_from_its_data", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = first_day_of( SUMMER );
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_j02_camp( camp_omt );
    camp.add_expansion( "test_camp_fishing_dock", camp_omt + dock_dir, dock_dir );
    faction &fac = *get_player_character().get_faction();
    npc_ptr comp = spawn_j02_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        if( npc_ptr guy = overmap_buffer.find_npc( comp->getID() ) ) {
            guy->reset_companion_mission();
            guy->companion_mission_inv.clear();
        }
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
        get_globals().clear_global_values();
    } );
    stock_j02_larder( fac, 10000 );
    const faction_mission &fishing = faction_mission_test_camp_fishing.obj();
    REQUIRE( fishing.behavior == Camp_Generic );
    const mission_id fishing_id = fishing.mission_at( dock_dir );

    THEN( "it is offered only at the expansion with the fishing dock" ) {
        CHECK( find_entry( offered_at( camp, base_camps::base_dir ),
                           faction_mission_test_camp_fishing, false ) == nullptr );
        const mission_data at_dock = offered_at( camp, dock_dir );
        const mission_entry *start = find_entry( at_dock, faction_mission_test_camp_fishing, false );
        REQUIRE( start != nullptr );
        CHECK( start->name_display == "[N] Go Fishing" );
        CHECK( start->possible );
        CHECK( start->text.find( "Send a companion to fish from the dock." ) != std::string::npos );
        CHECK( start->text.find( "Positions: 0/2" ) != std::string::npos );
        // a provide amount above what the dock gives
        CHECK( find_entry( at_dock, faction_mission_test_camp_deep_sea_fishing, false ) == nullptr );
        CHECK( find_entry( at_dock, faction_mission_test_camp_fishing_all_day, false ) != nullptr );
    }

    const int prepaid = camp.time_to_food( 5_hours, LIGHT_EXERCISE );
    REQUIRE( prepaid > 0 );
    const float survival_before = comp->get_skill_level( skill_survival );
    REQUIRE( camp.start_camp_mission( fishing, fishing_id, comp ) == comp );
    const time_point departed = calendar::turn;

    THEN( "the companion is out for the duration and fed for it" ) {
        CHECK( is_equal( comp->get_companion_mission().miss_id, fishing_id ) );
        CHECK( comp->companion_mission_time_ret == departed + 5_hours );
        CHECK( comp->companion_mission_exertion == LIGHT_EXERCISE );
        CHECK( fac.food_supply().kcal() == 10000 - prepaid );
    }
    THEN( "its start_eocs ran with the companion as alpha, and the event names it" ) {
        CHECK( global_number( "test_camp_j02_started" ) == 1 );
        CHECK( global_string( "test_camp_j02_start_mission" ) == "test_camp_fishing" );
        const diag_value *alpha_mark = comp->maybe_get_value( "test_camp_j02_alpha" );
        REQUIRE( alpha_mark != nullptr );
        CHECK( alpha_mark->dbl() == 1 );
        CHECK( global_string( "test_camp_j02_event_mission" ) == "test_camp_fishing" );
    }
    THEN( "the board can't bring it back before its time, but has room for one more" ) {
        calendar::turn = departed + 4_hours;
        const mission_data at_dock = offered_at( camp, dock_dir );
        const mission_entry *recover = find_entry( at_dock, faction_mission_test_camp_fishing, true );
        REQUIRE( recover != nullptr );
        CHECK( recover->name_display == "[N] Recover Fisher" );
        CHECK_FALSE( recover->possible );
        const mission_entry *start = find_entry( at_dock, faction_mission_test_camp_fishing, false );
        REQUIRE( start != nullptr );
        CHECK( start->possible );
        CHECK( camp.handle_mission( { fishing_id, true } ) );
        CHECK( comp->has_companion_mission() );
        CHECK( count_on_map( itype_fish ) == 0 );
    }

    WHEN( "the game is saved and loaded while the companion is out" ) {
        const npc_ptr loaded = saved_and_loaded( comp );
        comp = loaded;
        CHECK( is_equal( loaded->get_companion_mission().miss_id, fishing_id ) );
        calendar::turn = departed + 5_hours + 1_turns;
        THEN( "it comes back on time with the summer catch, trained, and runs its return_eocs" ) {
            const mission_data at_dock = offered_at( camp, dock_dir );
            const mission_entry *recover = find_entry( at_dock, faction_mission_test_camp_fishing,
                                           true );
            REQUIRE( recover != nullptr );
            CHECK( recover->possible );
            REQUIRE( camp.handle_mission( { fishing_id, true } ) );
            CHECK_FALSE( loaded->has_companion_mission() );
            CHECK( count_on_map( itype_fish ) == 3 * item( itype_fish ).count() );
            CHECK( count_on_map( itype_2x4 ) == 0 );
            CHECK( loaded->get_skill_level( skill_survival ) > survival_before );
            CHECK( global_number( "test_camp_j02_returned" ) == 1 );
            CHECK( global_string( "test_camp_j02_return_mission" ) == "test_camp_fishing" );
            // fixed time: nothing more to pay
            CHECK( fac.food_supply().kcal() == 10000 - prepaid );
        }
    }
}

// CAMP-J02: a repeatable mission keeps working in cycles of its duration until called back,
// brings back the results of every cycle and pays for the time past its first.  Out of season
// it brings back its default item group.
TEST_CASE( "camp-j02_repeatable_json_camp_mission_brings_back_each_cycle", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = first_day_of( WINTER );
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_j02_camp( camp_omt );
    camp.add_expansion( "test_camp_fishing_dock", camp_omt + dock_dir, dock_dir );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_j02_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        comp->reset_companion_mission();
        comp->companion_mission_inv.clear();
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_j02_larder( fac, 10000 );
    const faction_mission &all_day = faction_mission_test_camp_fishing_all_day.obj();
    const mission_id all_day_id = all_day.mission_at( dock_dir );
    REQUIRE( camp.start_camp_mission( all_day, all_day_id, comp ) == comp );
    const int prepaid = camp.time_to_food( 2_hours, LIGHT_EXERCISE );
    CHECK( fac.food_supply().kcal() == 10000 - prepaid );

    calendar::turn += 6_hours;
    const mission_data at_dock = offered_at( camp, dock_dir );
    const mission_entry *recover = find_entry( at_dock, faction_mission_test_camp_fishing_all_day,
                                   true );
    REQUIRE( recover != nullptr );
    CHECK( recover->possible );
    REQUIRE( camp.handle_mission( { all_day_id, true } ) );
    CHECK_FALSE( comp->has_companion_mission() );
    // three cycles of two attempts, from the default group out of summer
    CHECK( count_on_map( itype_2x4 ) == 6 );
    CHECK( count_on_map( itype_fish ) == 0 );
    CHECK( fac.food_supply().kcal() == 10000 - camp.time_to_food( 6_hours, LIGHT_EXERCISE ) );
}

// CAMP-J02: the vanilla missions are offered from their JSON, and each mission's id carries its
// behaviour, so the camp board still runs the bespoke code: planting is planting.
TEST_CASE( "camp-j02_vanilla_camp_missions_are_offered_and_run_by_their_behavior",
           "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    restore_on_out_of_scope restore_turn( calendar::turn );
    map &here = get_map();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_j02_camp( camp_omt );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_j02_worker( camp_mid.xy() + point::south );
    on_out_of_scope cleanup( [&camp_omt, &fac, &comp]() {
        comp->reset_companion_mission();
        comp->companion_mission_inv.clear();
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_j02_larder( fac, 10000 );

    SECTION( "the bare bones camp offers its missions with their old names" ) {
        const mission_data base = offered_at( camp, base_camps::base_dir );
        const mission_entry *foraging = find_entry( base, faction_mission_camp_foraging, false );
        REQUIRE( foraging != nullptr );
        CHECK( foraging->name_display == "[B] Forage for plants" );
        CHECK( foraging->id.id.id == Camp_Generic );
        const mission_entry *logs = find_entry( base, faction_mission_camp_cut_logs, false );
        REQUIRE( logs != nullptr );
        CHECK( logs->name_display == "[B] Cut Logs" );
        CHECK( logs->id.id.id == Camp_Cut_Logs );
        const mission_entry *recruiting = find_entry( base, faction_mission_camp_recruiting, false );
        REQUIRE( recruiting != nullptr );
        CHECK( recruiting->name_display == "[B] Recruit Companions" );
        CHECK( recruiting->id.id.id == Camp_Recruiting );
        CHECK( recruiting->text.find( "Positions: 0/1" ) != std::string::npos );
        const mission_entry *scouting = find_entry( base, faction_mission_camp_scouting, false );
        REQUIRE( scouting != nullptr );
        CHECK( scouting->name_display == "[B] Scout Mission" );
        // the camp has no farm
        CHECK( find_entry( base, faction_mission_camp_plant, false ) == nullptr );
    }

    SECTION( "a planting mission is run by the planting code" ) {
        const tripoint_bub_ms plot = camp_mid + tripoint::west * 2;
        here.ter_set( plot, ter_t_dirtmound );
        const mission_id plant = faction_mission_camp_plant->mission_at( base_camps::base_dir );
        REQUIRE( plant.id == Camp_Plant );
        item seed( itype_seed_hops );
        const std::vector<item *> equipment{ &seed };
        REQUIRE( camp.start_mission( plant, 1_minutes, true, "", false, equipment,
                                     std::map<skill_id, int>(), MODERATE_EXERCISE, 0_hours, comp ) );
        calendar::turn += 1_minutes;
        REQUIRE( camp.handle_mission( { plant, true } ) );
        CHECK_FALSE( comp->has_companion_mission() );
        CHECK( here.furn( plot ) == furn_f_plant_seed );
    }
}

// CAMP-J02: missions in progress are saved by kind and, now, their camp mission id.  A
// companion saved out on a vanilla mission before camp missions had ids comes back as that
// mission, run by what its JSON now says.
TEST_CASE( "camp-j02_old_saves_bring_companions_back_from_vanilla_missions", "[audit][camp]" )
{
    const point_rel_omt dir = base_camps::base_dir;
    const mission_id foraging = loaded_mission_id( mission_id{ Camp_Foraging, "", {}, dir } );
    CHECK( foraging.type == faction_mission_camp_foraging );
    CHECK( foraging.id == Camp_Generic );
    CHECK( is_equal( foraging, faction_mission_camp_foraging->mission_at( dir ) ) );

    const mission_id logs = loaded_mission_id( mission_id{ Camp_Cut_Logs, "", {}, dir } );
    CHECK( logs.type == faction_mission_camp_cut_logs );
    CHECK( logs.id == Camp_Cut_Logs );
    CHECK( is_equal( logs, faction_mission_camp_cut_logs->mission_at( dir ) ) );

    // missions that aren't JSON camp missions keep no camp mission id
    const mission_id upgrade = loaded_mission_id( mission_id{ Camp_Upgrade, "faction_base_camp_1", {}, dir } );
    CHECK( upgrade.type.is_empty() );
    CHECK( upgrade.id == Camp_Upgrade );

    // a camp mission saved with its id keeps it
    const mission_id fishing = loaded_mission_id(
                                   faction_mission_test_camp_fishing->mission_at( dock_dir ) );
    CHECK( fishing.type == faction_mission_test_camp_fishing );
    CHECK( fishing.id == Camp_Generic );
}

// CAMP-J02: gathering, firewood, foraging, trapping and hunting moved from C++ to generic JSON
// missions.  Their data must be the numbers the code used, and their skill score the same
// formula, so the same companion brings back the same.
TEST_CASE( "camp-j02_vanilla_gathering_missions_keep_their_numbers", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    npc &guy = spawn_npc( point_bub_ms( camp_mid.xy() ), "test_talker" );
    clear_character( guy, true );
    guy.set_skill_level( skill_survival, 3 );
    guy.get_skill_level_object( skill_survival ).set_exercise( 40 );
    guy.set_skill_level( skill_traps, 2 );
    guy.get_skill_level_object( skill_traps ).set_exercise( 70 );
    guy.set_skill_level( skill_gun, 5 );
    guy.get_skill_level_object( skill_gun ).set_exercise( 30 );
    REQUIRE( guy.get_skill_level( skill_survival ) > 3.0f );

    const auto check_generic = []( const faction_mission & def, const time_duration & duration,
    float exertion, int attempts, int difficulty ) {
        CAPTURE( def.id.str() );
        CHECK( def.behavior == Camp_Generic );
        CHECK( def.repeatable );
        REQUIRE( def.duration );
        CHECK( *def.duration == duration );
        CHECK( def.exertion() == exertion );
        CHECK( def.results.attempts == attempts );
        CHECK( def.results.difficulty == difficulty );
        REQUIRE( def.encounter );
        REQUIRE( def.train );
        CHECK( def.train->difficulty == 1 );
    };
    const float survival_score = 2 * guy.get_skill_level( skill_survival ) + guy.get_per();

    const faction_mission &gathering = faction_mission_camp_gathering.obj();
    check_generic( gathering, 3_hours, LIGHT_EXERCISE, 6, 15 );
    CHECK( gathering.companion_skills() == std::map<skill_id, int> { { skill_survival, 0 } } );
    CHECK( gathering.results.item_group == Item_spawn_data_forest );
    CHECK( gathering.encounter->one_in == 20 );
    CHECK( gathering.encounter->favor == 2 );
    CHECK( gathering.encounter->threat == 10 );
    CHECK( gathering.train->skill == "gathering" );
    CHECK( gathering.results.skill_score.evaluate( guy ) == Approx( survival_score ) );
    CHECK( gathering.return_text() ==
           "returns from gathering materials carrying supplies and has a bit more experience…" );

    const faction_mission &firewood = faction_mission_camp_collect_firewood.obj();
    check_generic( firewood, 3_hours, LIGHT_EXERCISE, 6, 15 );
    CHECK( firewood.companion_skills() == std::map<skill_id, int> { { skill_survival, 0 } } );
    CHECK( firewood.results.item_group == Item_spawn_data_gathering_faction_camp_firewood );
    CHECK( firewood.encounter->one_in == 20 );
    CHECK( firewood.train->skill == "gathering" );
    CHECK( firewood.results.skill_score.evaluate( guy ) == Approx( survival_score ) );

    const faction_mission &foraging = faction_mission_camp_foraging.obj();
    check_generic( foraging, 4_hours, LIGHT_EXERCISE, 12, 15 );
    CHECK( foraging.results.item_group_in( SUMMER ) == Item_spawn_data_foraging_faction_camp_summer );
    CHECK( foraging.results.item_group_in( WINTER ) == Item_spawn_data_foraging_faction_camp_winter );
    CHECK( foraging.encounter->one_in == 15 );
    CHECK( foraging.encounter->favor == 2 );
    CHECK( foraging.encounter->threat == 10 );
    CHECK( foraging.encounter->task.translated() == "foraging for edible plants" );
    CHECK( foraging.train->skill == "gathering" );
    CHECK( foraging.results.skill_score.evaluate( guy ) == Approx( survival_score ) );

    const faction_mission &trapping = faction_mission_camp_trapping.obj();
    check_generic( trapping, 6_hours, LIGHT_EXERCISE, 4, 30 );
    CHECK( trapping.companion_skills() == std::map<skill_id, int> { { skill_traps, 0 } } );
    CHECK( trapping.results.monster_groups ==
           std::vector<mongroup_id> { GROUP_CAMP_HUNTING, GROUP_CAMP_TRAPPING } );
    CHECK( trapping.encounter->one_in == 15 );
    CHECK( trapping.encounter->favor == 1 );
    CHECK( trapping.encounter->threat == 10 );
    CHECK( trapping.train->skill == "trapping" );
    CHECK( trapping.results.skill_score.evaluate( guy ) ==
           Approx( 2 * guy.get_skill_level( skill_traps ) + guy.get_per() ) );

    const faction_mission &hunting = faction_mission_camp_hunting.obj();
    check_generic( hunting, 6_hours, MODERATE_EXERCISE, 2, 30 );
    CHECK( hunting.companion_skills() == std::map<skill_id, int> { { skill_gun, 0 } } );
    CHECK( hunting.results.monster_groups ==
           std::vector<mongroup_id> { GROUP_CAMP_HUNTING, GROUP_CAMP_HUNTING_LARGE } );
    CHECK( hunting.encounter->one_in == 10 );
    CHECK( hunting.encounter->favor == 0 );
    CHECK( hunting.encounter->threat == 12 );
    CHECK( hunting.train->skill == "hunting" );
    CHECK( hunting.results.skill_score.evaluate( guy ) ==
           Approx( 1.5 * guy.get_skill_level( skill_gun ) + guy.get_per() / 2.0 ) );

    // every attempt succeeds: a corpse from one of the groups (unless it rolls nothing)
    std::vector<item> found;
    basecamp::hunting_results( 100, hunting.results.monster_groups, 2, 30, found );
    CHECK( std::all_of( found.begin(), found.end(), []( const item & it ) {
        return it.is_corpse();
    } ) );
    found.clear();
    basecamp::search_results( 0, Item_spawn_data_forest, 6, 15, found );
    CHECK( found.empty() );
}

// CAMP-J02: a faction_mission names the behaviour that runs it; an unknown one is a load error.
TEST_CASE( "camp-j02_unknown_camp_mission_behavior_is_a_load_error", "[audit][camp]" )
{
    faction_mission def;
    JsonObject jo = json_loader::from_string(
                        R"({ "name": "Bad", "desc": "Bad.", "behavior": "levitating", "duration": "1 h" })" ).get_object();
    jo.allow_omitted_members();
    CHECK_THROWS_AS( def.load( jo, "test" ), JsonError );

    faction_mission menial;
    JsonObject menial_jo = json_loader::from_string(
                               R"({ "name": "Chores", "desc": "Chores.", "behavior": "menial", "duration": "2 h" })" ).get_object();
    menial_jo.allow_omitted_members();
    menial.load( menial_jo, "test" );
    CHECK( menial.behavior == Camp_Menial );
    // nothing given: no activity shown, fed as no exercise, one position
    CHECK( menial.activity_level == 0.0f );
    CHECK( menial.exertion() == NO_EXERCISE );
    CHECK( menial.positions == 1 );
    CHECK( menial.describe().find( "Activity level" ) == std::string::npos );
}

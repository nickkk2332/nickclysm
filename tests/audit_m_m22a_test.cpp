#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
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
#include "coordinates.h"
#include "debug.h"
#include "dialogue_chatbin.h"
#include "faction.h"
#include "faction_camp.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "game_constants.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "memory_fast.h"
#include "mission.h"
#include "mission_companion.h"
#include "monster.h"
#include "npc.h"
#include "npctrade.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "recipe.h"
#include "rng.h"
#include "skill.h"
#include "stomach.h"
#include "talker.h"
#include "trade_ui.h"
#include "type_id.h"
#include "visitable.h"

static const faction_id faction_your_followers( "your_followers" );

static const faction_mission_id faction_mission_test_camp_fishing( "test_camp_fishing" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_test_m22a_token_a( "test_m22a_token_a" );
static const itype_id itype_test_m22a_token_b( "test_m22a_token_b" );
static const itype_id itype_test_m22a_token_c( "test_m22a_token_c" );

static const mission_type_id
mission_TEST_MISSION_M22A_FIND_TOKENS( "TEST_MISSION_M22A_FIND_TOKENS" );
static const mission_type_id
mission_TEST_MISSION_M22A_RECRUIT_HUNTER( "TEST_MISSION_M22A_RECRUIT_HUNTER" );
static const mission_type_id
mission_TEST_MISSION_M22A_UPDATE_IN_SPECIAL( "TEST_MISSION_M22A_UPDATE_IN_SPECIAL" );
static const mission_type_id mission_TEST_MISSION_M22A_URGENT( "TEST_MISSION_M22A_URGENT" );

static const mtype_id mon_dog( "mon_dog" );
static const mtype_id mon_zombie( "mon_zombie" );

static const npc_class_id NC_HUNTER( "NC_HUNTER" );

static const recipe_id recipe_cudgel_test_m22a_camp( "cudgel_test_m22a_camp" );
static const recipe_id recipe_test_camp_m22a_garden( "test_camp_m22a_garden" );
static const recipe_id recipe_test_camp_m22a_shed( "test_camp_m22a_shed" );

static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_tailor( "tailor" );

static constexpr tripoint_bub_ms camp_mid{ MAPSIZE_X / 2, MAPSIZE_Y / 2, 0 };
// the test fishing dock's expansion (TEST_DATA/audit_high_camp.json)
static constexpr point_rel_omt dock_dir{ 0, -1 };
// where the tests build the test site of TEST_DATA/audit_m_m22a.json
static constexpr point_rel_omt site_dir{ 1, 0 };

namespace
{

// player-owned bare bones camp at mid-map; caller removes it with clear_camps
basecamp &place_m22a_camp( tripoint_abs_omt &camp_omt )
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

void stock_m22a_larder( faction &fac, const int kcal )
{
    fac.empty_food_supply();
    nutrients stock;
    stock.calories = kcal * 1000;
    fac.debug_food_supply().emplace_back( calendar::turn_zero, stock );
    REQUIRE( fac.food_supply().kcal() == kcal );
}

// follower who can be sent on camp missions, as the overmap buffer holds it
npc_ptr spawn_m22a_worker( const point_bub_ms &where )
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

// ends the companion's mission, wherever the overmap buffer holds it now
void call_off( const character_id &id )
{
    if( npc_ptr guy = overmap_buffer.find_npc( id ) ) {
        guy->reset_companion_mission();
        guy->companion_mission_inv.clear();
    }
}

mission_data offered_at( basecamp &camp, const point_rel_omt &dir )
{
    camp.reset_camp_workers();
    camp.form_crafting_inventory( get_map() );
    mission_data missions;
    camp.get_available_missions_by_dir( missions, dir );
    return missions;
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

// the menu entry that starts (or recovers the builders of) upgrade @ref bldg
const mission_entry *find_upgrade_entry( const mission_data &missions, const std::string &bldg,
        const bool ret )
{
    for( const std::vector<mission_entry> &tab : missions.entries ) {
        for( const mission_entry &e : tab ) {
            if( e.id.id.id == Camp_Upgrade && e.id.id.parameters == bldg && e.id.ret == ret ) {
                return &e;
            }
        }
    }
    return nullptr;
}

const basecamp_upgrade *find_upgrade( const std::vector<basecamp_upgrade> &upgrades,
                                      const recipe_id &bldg )
{
    for( const basecamp_upgrade &upgrade : upgrades ) {
        if( upgrade.bldg == bldg.str() ) {
            return &upgrade;
        }
    }
    return nullptr;
}

// everything of @ref type in the items, by count, with what they hold
int count_in( const std::vector<item> &items, const itype_id &type )
{
    int found = 0;
    for( const item &it : items ) {
        it.visit_items( [&found, &type]( const item * node, const item * ) {
            if( node->typeId() == type ) {
                found += node->count();
            }
            return VisitResponse::NEXT;
        } );
    }
    return found;
}

// everything of @ref type on the tiles of @ref site, by count, with what they hold
int count_at_site( const tripoint_abs_omt &site, const itype_id &type )
{
    map &here = get_map();
    const tripoint_bub_ms corner = here.get_bub( project_to<coords::ms>( site ) );
    int found = 0;
    for( int x = 0; x < SEEX * 2; x++ ) {
        for( int y = 0; y < SEEY * 2; y++ ) {
            for( const item &it : here.i_at( corner + tripoint_rel_ms( x, y, 0 ) ) ) {
                it.visit_items( [&found, &type]( const item * node, const item * ) {
                    if( node->typeId() == type ) {
                        found += node->count();
                    }
                    return VisitResponse::NEXT;
                } );
            }
        }
    }
    return found;
}

int count_carried( const Character &who, const itype_id &type )
{
    int found = 0;
    who.visit_items_raw( [&found, &type]( const item & it ) {
        if( it.typeId() == type ) {
            found += it.count();
        }
        return VisitResponse::NEXT;
    } );
    return found;
}

// how much @ref sk has been practiced at all
int practiced( const Character &who, const skill_id &sk )
{
    const SkillLevel &level = who.get_skill_level_object( sk );
    return level.level() * 1000000 + level.exercise( true ) + level.knowledgeExperience( true );
}

double global_number( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

mission *assign_m22a_mission( avatar &u, const mission_type_id &type )
{
    mission *m = mission::reserve_new( type, character_id() );
    if( m->get_assigned_player_id() == u.getID() ) {
        // see mission_goal_condition_test: some builds start with the avatar's id here
        m->set_assigned_player_id( character_id( -2 ) );
    }
    m->assign( u );
    return m;
}

} // namespace

// CAMP-B05: update_time_left set the Recover entry from the last companion in the list only,
// and the return picked anyone out longer than the mission's fixed minimum, ignoring each
// companion's own planned return.  One companion done is enough to offer the entry, whichever
// order they are listed in, and only the companions whose own time is up come back.
TEST_CASE( "camp-b05_companions_come_back_by_their_own_return_time", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22a_camp( camp_omt );
    camp.add_expansion( "test_camp_fishing_dock", camp_omt + dock_dir, dock_dir );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr first = spawn_m22a_worker( camp_mid.xy() + point::south );
    const npc_ptr second = spawn_m22a_worker( camp_mid.xy() + point::north );
    const character_id first_id = first->getID();
    const character_id second_id = second->getID();
    on_out_of_scope cleanup( [&camp_omt, &fac, first_id, second_id]() {
        call_off( first_id );
        call_off( second_id );
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
        get_globals().clear_global_values();
    } );
    stock_m22a_larder( fac, 10000 );
    const faction_mission &fishing = faction_mission_test_camp_fishing.obj();
    REQUIRE_FALSE( fishing.repeatable );
    const mission_id fishing_id = fishing.mission_at( dock_dir );
    REQUIRE( camp.start_camp_mission( fishing, fishing_id, first ) == first );
    REQUIRE( camp.start_camp_mission( fishing, fishing_id, second ) == second );
    const time_point departed = calendar::turn;
    // both are past the mission's 5 h; the planned returns differ, as a scouting route's do
    calendar::turn = departed + 6_hours;
    const time_point due = departed + 5_hours;
    const time_point not_yet = departed + 2_days;

    const auto recover_possible = [&camp]() {
        const mission_data at_dock = offered_at( camp, dock_dir );
        const mission_entry *recover = find_entry( at_dock, faction_mission_test_camp_fishing, true );
        REQUIRE( recover != nullptr );
        return recover->possible;
    };

    SECTION( "the board offers to recover whichever one is done" ) {
        first->companion_mission_time_ret = due;
        second->companion_mission_time_ret = not_yet;
        CHECK( recover_possible() );
        first->companion_mission_time_ret = not_yet;
        second->companion_mission_time_ret = due;
        CHECK( recover_possible() );
        first->companion_mission_time_ret = not_yet;
        CHECK_FALSE( recover_possible() );
    }
    SECTION( "only the one whose own time is up comes back" ) {
        first->companion_mission_time_ret = not_yet;
        second->companion_mission_time_ret = due;
        REQUIRE( camp.handle_mission( { fishing_id, true } ) );
        const npc_ptr first_now = overmap_buffer.find_npc( first_id );
        const npc_ptr second_now = overmap_buffer.find_npc( second_id );
        REQUIRE( first_now );
        REQUIRE( second_now );
        CHECK( first_now->has_companion_mission() );
        CHECK_FALSE( second_now->has_companion_mission() );
    }
}

// CAMP-B06: a request for nothing (a mission of no length, a recruit who wants no food) was
// a debugmsg, and with an empty larder the share of vitamins divided by its zero calories.
TEST_CASE( "camp-b06_camp_food_supply_with_nothing_asked_or_an_empty_larder", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22a_camp( camp_omt );
    faction &fac = *get_player_character().get_faction();
    restore_on_out_of_scope restore_likes( fac.likes_u );
    restore_on_out_of_scope restore_respects( fac.respects_u );
    restore_on_out_of_scope restore_trusts( fac.trusts_u );
    on_out_of_scope cleanup( [&camp_omt, &fac]() {
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    REQUIRE( fac.consumes_food );

    SECTION( "nothing asked is nothing served, quietly" ) {
        stock_m22a_larder( fac, 1000 );
        int served = -1;
        int served_for_no_time = -1;
        const std::string msgs = capture_debugmsg_during( [&]() {
            served = camp.camp_food_supply( 0 ).total().kcal();
            served_for_no_time = camp.camp_food_supply( 0_hours ).total().kcal();
        } );
        CHECK( msgs.empty() );
        CHECK( served == 0 );
        CHECK( served_for_no_time == 0 );
        CHECK( fac.food_supply().kcal() == 1000 );
    }
    SECTION( "a mission of no length sets off without a meal" ) {
        stock_m22a_larder( fac, 1000 );
        const npc_ptr comp = spawn_m22a_worker( camp_mid.xy() + point::south );
        const character_id comp_id = comp->getID();
        on_out_of_scope call_back( [comp_id]() {
            call_off( comp_id );
        } );
        const mission_id survey{ Camp_Survey_Field, "", {}, base_camps::base_dir };
        npc_ptr sent;
        const std::string msgs = capture_debugmsg_during( [&]() {
            sent = camp.start_mission( survey, 0_hours, true, "", false, {},
                                       std::map<skill_id, int>(), LIGHT_EXERCISE, 0_hours, comp );
        } );
        CHECK( msgs.empty() );
        CHECK( sent == comp );
        CHECK( fac.food_supply().kcal() == 1000 );
    }
    SECTION( "an empty larder serves nothing and costs standing" ) {
        fac.empty_food_supply();
        const int likes = fac.likes_u;
        const int respects = fac.respects_u;
        const int trusts = fac.trusts_u;
        int served = -1;
        const std::string msgs = capture_debugmsg_during( [&]() {
            served = camp.camp_food_supply( -5000 ).total().kcal();
        } );
        CHECK( msgs.empty() );
        CHECK( served == 0 );
        // 5000 kcal short: 5000 / 1250 and 5000 / 625
        CHECK( fac.likes_u == likes - 4 );
        CHECK( fac.respects_u == respects - 8 );
        CHECK( fac.trusts_u == trusts - 8 );
    }
}

// CAMP-B07: loot_building kept its loot in a std::set<item>, so equal-looking items (ten of a
// kind, two backpacks holding different things) came back as one and the rest vanished, and
// its "kill zombies" step killed any creature there, NPCs and pets included.
TEST_CASE( "camp-b07_looting_a_building_keeps_every_item_and_spares_friends", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_creatures();
    clear_map_without_vision();
    rng_set_engine_seed( 2207 );
    map &here = get_map();
    get_avatar().setpos( here, tripoint_bub_ms( 60, 60, 0 ) );
    // an overmap tile in the reality bubble, away from the avatar's
    const tripoint_abs_omt site = project_to<coords::omt>( here.get_abs( tripoint_bub_ms( 36, 36,
                                  0 ) ) );
    REQUIRE( site != get_avatar().pos_abs_omt() );
    const tripoint_bub_ms corner = here.get_bub( project_to<coords::ms>( site ) );
    REQUIRE( here.inbounds( corner ) );
    REQUIRE( here.inbounds( corner + tripoint_rel_ms( SEEX * 2 - 1, SEEY * 2 - 1, 0 ) ) );
    // loot_building marks the site looted; it stays what it is
    const oter_str_id site_terrain = overmap_buffer.ter( site ).id();
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_npcs();
    } );

    SECTION( "every item taken comes back" ) {
        for( int i = 0; i < 10; i++ ) {
            here.add_item( corner + tripoint_rel_ms( 3, 3, 0 ), item( itype_test_m22a_token_a ) );
        }
        item holds_b( itype_backpack );
        REQUIRE( holds_b.put_in( item( itype_test_m22a_token_b ), pocket_type::CONTAINER ).success() );
        item holds_c( itype_backpack );
        REQUIRE( holds_c.put_in( item( itype_test_m22a_token_c ), pocket_type::CONTAINER ).success() );
        here.add_item( corner + tripoint_rel_ms( 5, 5, 0 ), holds_b );
        here.add_item( corner + tripoint_rel_ms( 5, 5, 0 ), holds_c );
        REQUIRE( count_at_site( site, itype_test_m22a_token_a ) == 10 );
        REQUIRE( count_at_site( site, itype_test_m22a_token_b ) == 1 );
        REQUIRE( count_at_site( site, itype_test_m22a_token_c ) == 1 );

        const std::vector<item> taken = talk_function::loot_building( site, site_terrain );

        // what each item was taken is random; that nothing is lost is not
        CHECK( count_in( taken, itype_test_m22a_token_a ) +
               count_at_site( site, itype_test_m22a_token_a ) == 10 );
        CHECK( count_in( taken, itype_test_m22a_token_b ) +
               count_at_site( site, itype_test_m22a_token_b ) == 1 );
        CHECK( count_in( taken, itype_test_m22a_token_c ) +
               count_at_site( site, itype_test_m22a_token_c ) == 1 );
    }
    SECTION( "only hostile monsters are killed" ) {
        monster &zombie = spawn_test_monster( mon_zombie.str(), corner + tripoint_rel_ms( 4, 4, 0 ) );
        monster &dog = spawn_test_monster( mon_dog.str(), corner + tripoint_rel_ms( 8, 8, 0 ) );
        dog.friendly = -1;
        npc &guy = spawn_npc( ( corner + tripoint_rel_ms( 12, 12, 0 ) ).xy(), "test_talker" );
        guy.set_fac( faction_your_followers );
        REQUIRE( zombie.attitude_to( get_avatar() ) == Creature::Attitude::HOSTILE );
        REQUIRE( dog.attitude_to( get_avatar() ) != Creature::Attitude::HOSTILE );

        talk_function::loot_building( site, site_terrain );

        CHECK( zombie.is_dead() );
        CHECK_FALSE( dog.is_dead() );
        CHECK_FALSE( guy.is_dead() );
        CHECK_FALSE( get_avatar().is_dead_state() );
    }
}

// CAMP-B08: confirming on any follower with a companion mission opened a camp board where
// the follower was, founding a camp there for an outpost job.  Only a camp mission leads to
// a camp, and only to the one it is from.
TEST_CASE( "camp-b08_only_a_camp_mission_leads_to_a_camp", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    map &here = get_map();
    npc &guy = spawn_npc( camp_mid.xy() + point::south * 3, "test_talker" );
    guy.set_fac( faction_your_followers );
    const tripoint_abs_omt where = guy.pos_abs_omt();
    on_out_of_scope cleanup( [&guy, &where]() {
        guy.reset_companion_mission();
        overmap_buffer.clear_camps( where.xy() );
    } );
    REQUIRE_FALSE( overmap_buffer.find_camp( where.xy() ) );
    const size_t camps_before = get_player_character().camps.size();

    SECTION( "an outpost job" ) {
        guy.set_companion_mission( where, "FOREMAN", { Scavenging_Patrol_Job, "", {}, std::nullopt } );
        CHECK_FALSE( talk_function::camp_of_companion_mission( guy ) );
        CHECK_FALSE( talk_function::camp_board_for_companion( guy, false ) );
        CHECK_FALSE( overmap_buffer.find_camp( where.xy() ) );
        CHECK( get_player_character().camps.size() == camps_before );
    }
    SECTION( "a camp mission from a camp that isn't there" ) {
        guy.set_companion_mission( where, base_camps::id,
        { Camp_Gather_Materials, "", {}, base_camps::base_dir } );
        CHECK_FALSE( talk_function::camp_of_companion_mission( guy ) );
        CHECK_FALSE( talk_function::camp_board_for_companion( guy, true ) );
        CHECK_FALSE( overmap_buffer.find_camp( where.xy() ) );
        CHECK( get_player_character().camps.size() == camps_before );
    }
    SECTION( "a camp mission from a camp" ) {
        const tripoint_abs_omt camp_omt = project_to<coords::omt>( here.get_abs( camp_mid ) );
        here.add_camp( camp_omt, "faction_camp" );
        on_out_of_scope remove_camp( [camp_omt]() {
            overmap_buffer.clear_camps( camp_omt.xy() );
        } );
        const std::optional<basecamp *> camp = overmap_buffer.find_camp( camp_omt.xy() );
        REQUIRE( camp );
        guy.set_companion_mission( camp_omt, base_camps::id,
        { Camp_Gather_Materials, "", {}, base_camps::base_dir } );
        const std::optional<basecamp *> found = talk_function::camp_of_companion_mission( guy );
        REQUIRE( found );
        CHECK( *found == *camp );
    }
}

// CAMP-B09: selling some charges of a held stack took them by type from whichever stack of
// that type came first, so the picked (and priced) stack stayed and another one shrank.
TEST_CASE( "camp-b09_trading_charges_takes_them_from_the_picked_stack", "[audit][trade]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    avatar &you = get_avatar();
    npc &guy = spawn_npc( { 50, 50 }, "test_talker" );
    REQUIRE( you.wear_item( item( itype_backpack ), false ) );
    item plain( itype_nail, calendar::turn, 20 );
    item marked( itype_nail, calendar::turn, 20 );
    // the mark keeps the two stacks apart
    marked.set_var( "test_m22a_b09", 1 );
    item_location plain_loc = you.i_add( plain );
    item_location marked_loc = you.i_add( marked );
    REQUIRE( plain_loc );
    REQUIRE( marked_loc );
    REQUIRE( plain_loc.get_item() != marked_loc.get_item() );
    REQUIRE( plain_loc->charges == 20 );
    REQUIRE( marked_loc->charges == 20 );

    const bool sell_marked = GENERATE( false, true );
    CAPTURE( sell_marked );
    item_location &sold = sell_marked ? marked_loc : plain_loc;
    item_location &kept = sell_marked ? plain_loc : marked_loc;
    std::vector<std::pair<item_location, int>> try_trade;
    try_trade.emplace_back( sold, 5 );
    std::list<item_location *> from_map;
    const std::list<item> escrow = npc_trading::transfer_items( try_trade, you, guy, from_map, true );

    REQUIRE( escrow.size() == 1 );
    CHECK( escrow.front().charges == 5 );
    CHECK( escrow.front().has_var( "test_m22a_b09" ) == sell_marked );
    CHECK( sold->charges == 15 );
    CHECK( kept->charges == 20 );
    CHECK( count_carried( you, itype_nail ) == 35 );
}

// CAMP-B10: recruit_class was never loaded nor copied into the mission, so the goal was met by
// any following NPC of the null class (NC_NONE) and never by one of the class asked for.
TEST_CASE( "camp-b10_recruit_class_missions_want_that_class", "[audit][mission]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    avatar &u = get_avatar();
    u.setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
    u.reset_all_missions();
    mission::clear_all();
    on_out_of_scope cleanup( [&u]() {
        u.reset_all_missions();
        mission::clear_all();
        clear_npcs();
    } );
    REQUIRE( mission_TEST_MISSION_M22A_RECRUIT_HUNTER->recruit_class == NC_HUNTER );
    mission *m = assign_m22a_mission( u, mission_TEST_MISSION_M22A_RECRUIT_HUNTER );
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east * 2, "test_talker" );
    guy.myclass = npc_class_id::NULL_ID();
    guy.set_attitude( NPCATT_FOLLOW );
    REQUIRE( guy.get_attitude() == NPCATT_FOLLOW );

    CHECK_FALSE( m->is_complete( character_id() ) );
    guy.myclass = NC_HUNTER;
    CHECK( m->is_complete( character_id() ) );
}

// CAMP-B11: "exclude_seen" read a missing "exclude" member into "random"; it is cant_see.
// update_mapgen's om_special was never read, so a mission that gave one failed to load.
TEST_CASE( "camp-b11_mission_target_fields_are_read", "[audit][mission]" )
{
    const JsonValue jv = json_loader::from_string(
                             R"({ "om_terrain": "field", "exclude_seen": true })" );
    const mission_target_params params = mission_util::parse_mission_om_target( jv.get_object() );
    CHECK( params.cant_see );
    CHECK_FALSE( params.random );
    // TEST_DATA/audit_m_m22a.json: loading it reads its update_mapgen's om_special
    CHECK( mission_TEST_MISSION_M22A_UPDATE_IN_SPECIAL.is_valid() );
}

// CAMP-B12: MGOAL_FIND_ITEM_GROUP counted an item in a backpack twice (the crafting inventory
// already lists nested items, and the search looks inside containers again), then tried to
// take every match of one type.  Items count once, and turning in takes exactly count.
TEST_CASE( "camp-b12_find_item_group_counts_once_and_takes_count", "[audit][mission]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    avatar &u = get_avatar();
    u.reset_all_missions();
    mission::clear_all();
    on_out_of_scope cleanup( [&u]() {
        u.reset_all_missions();
        mission::clear_all();
    } );
    REQUIRE( u.wear_item( item( itype_backpack ), false ) );
    for( int i = 0; i < 2; i++ ) {
        REQUIRE( u.i_add( item( itype_test_m22a_token_a ) ) );
    }
    u.invalidate_crafting_inventory();
    REQUIRE( count_carried( u, itype_test_m22a_token_a ) == 2 );
    mission *m = assign_m22a_mission( u, mission_TEST_MISSION_M22A_FIND_TOKENS );

    // two in the backpack are two, not the three asked for
    CHECK_FALSE( m->is_complete( character_id() ) );

    REQUIRE( u.i_add( item( itype_test_m22a_token_a ) ) );
    for( int i = 0; i < 2; i++ ) {
        REQUIRE( u.i_add( item( itype_test_m22a_token_b ) ) );
    }
    u.invalidate_crafting_inventory();
    REQUIRE( m->is_complete( character_id() ) );
    m->wrap_up();
    // three of the five are taken, whichever type goes first
    CHECK( count_carried( u, itype_test_m22a_token_a ) +
           count_carried( u, itype_test_m22a_token_b ) == 2 );
}

// CAMP-B13: an upgrade's in_progress was worked out but never read, so the same blueprint (in
// another variant) or one it rules out could be started while it was being built.  It is
// worked out from the companions out building, and blocks those starts.
TEST_CASE( "camp-b13_an_upgrade_under_way_blocks_what_it_rules_out", "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22a_camp( camp_omt );
    camp.add_expansion( "test_camp_m22a_site", camp_omt + site_dir, site_dir );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_m22a_worker( camp_mid.xy() + point::south );
    const character_id comp_id = comp->getID();
    on_out_of_scope cleanup( [&camp_omt, &fac, comp_id]() {
        call_off( comp_id );
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
    } );
    stock_m22a_larder( fac, 10000 );
    camp.reset_camp_workers();
    camp.form_crafting_inventory( get_map() );
    {
        const std::vector<basecamp_upgrade> upgrades = camp.available_upgrades( site_dir );
        const basecamp_upgrade *shed = find_upgrade( upgrades, recipe_test_camp_m22a_shed );
        const basecamp_upgrade *garden = find_upgrade( upgrades, recipe_test_camp_m22a_garden );
        REQUIRE( shed != nullptr );
        REQUIRE( garden != nullptr );
        CHECK_FALSE( shed->in_progress );
        CHECK_FALSE( garden->in_progress );
    }

    const mapgen_arguments shed_args =
        recipe_test_camp_m22a_shed->blueprint_build_reqs().reqs_by_parameters.begin()->first;
    const mission_id building_shed{ Camp_Upgrade, recipe_test_camp_m22a_shed.str(), shed_args, site_dir };
    comp->set_companion_mission( camp_omt, base_camps::id, building_shed );
    comp->companion_mission_time = calendar::turn;
    comp->companion_mission_time_ret = calendar::turn + 1_hours;
    camp.reset_camp_workers();

    const std::vector<basecamp_upgrade> upgrades = camp.available_upgrades( site_dir );
    const basecamp_upgrade *shed = find_upgrade( upgrades, recipe_test_camp_m22a_shed );
    const basecamp_upgrade *garden = find_upgrade( upgrades, recipe_test_camp_m22a_garden );
    REQUIRE( shed != nullptr );
    REQUIRE( garden != nullptr );
    CHECK( shed->in_progress );
    CHECK( garden->in_progress );

    const mission_data at_site = offered_at( camp, site_dir );
    const mission_entry *start_garden = find_upgrade_entry( at_site,
                                        recipe_test_camp_m22a_garden.str(), false );
    REQUIRE( start_garden != nullptr );
    CHECK_FALSE( start_garden->possible );
    CHECK( start_garden->text.find( "already building" ) != std::string::npos );
    CHECK( find_upgrade_entry( at_site, recipe_test_camp_m22a_shed.str(), true ) != nullptr );
}

// CAMP-J03: camp crafting never ran a recipe's result_eocs and trained the "construction"
// practice whatever the recipe; upgrades trained it too, not the blueprint's skills.
TEST_CASE( "camp-j03_camp_crafts_and_upgrades_train_their_skills_and_run_result_eocs",
           "[audit][camp]" )
{
    clear_avatar();
    clear_npcs();
    clear_map_without_vision();
    get_player_character().camps.clear();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    tripoint_abs_omt camp_omt;
    basecamp &camp = place_m22a_camp( camp_omt );
    faction &fac = *get_player_character().get_faction();
    const npc_ptr comp = spawn_m22a_worker( camp_mid.xy() + point::south );
    const character_id comp_id = comp->getID();
    on_out_of_scope cleanup( [&camp_omt, &fac, comp_id]() {
        call_off( comp_id );
        overmap_buffer.clear_camps( camp_omt.xy() );
        fac.empty_food_supply();
        get_globals().clear_global_values();
    } );
    stock_m22a_larder( fac, 10000 );
    REQUIRE( practiced( *comp, skill_tailor ) == 0 );
    REQUIRE( practiced( *comp, skill_fabrication ) == 0 );

    SECTION( "a camp craft" ) {
        const mission_id crafting{ Camp_Crafting, recipe_cudgel_test_m22a_camp.str(), {}, base_camps::base_dir };
        comp->set_companion_mission( camp_omt, base_camps::id, crafting );
        comp->companion_mission_time = calendar::turn - 10_hours;
        comp->companion_mission_time_ret = calendar::turn - 1_hours;
        // as start_crafting leaves it, for a batch of two
        comp->set_value( "camp_craft_batch_size", 2 );
        camp.reset_camp_workers();

        REQUIRE( camp.handle_mission( { crafting, true } ) );

        const npc_ptr back = overmap_buffer.find_npc( comp_id );
        REQUIRE( back );
        CHECK_FALSE( back->has_companion_mission() );
        CHECK( practiced( *back, skill_tailor ) > 0 );
        CHECK( practiced( *back, skill_fabrication ) == 0 );
        CHECK( global_number( "test_m22a_crafted" ) == 2 );
        CHECK( global_number( "test_m22a_crafted_batch" ) == 2 );
        const diag_value *crafter = back->maybe_get_value( "test_m22a_crafter" );
        REQUIRE( crafter != nullptr );
        CHECK( crafter->dbl() == 1 );
        CHECK( back->maybe_get_value( "camp_craft_batch_size" ) == nullptr );
    }
    SECTION( "an upgrade" ) {
        camp.add_expansion( "test_camp_m22a_site", camp_omt + site_dir, site_dir );
        const mapgen_arguments shed_args =
            recipe_test_camp_m22a_shed->blueprint_build_reqs().reqs_by_parameters.begin()->first;
        const mission_id building_shed{ Camp_Upgrade, recipe_test_camp_m22a_shed.str(), shed_args, site_dir };
        comp->set_companion_mission( camp_omt, base_camps::id, building_shed );
        comp->companion_mission_time = calendar::turn - 10_hours;
        comp->companion_mission_time_ret = calendar::turn - 1_hours;
        camp.reset_camp_workers();

        REQUIRE( camp.handle_mission( { building_shed, true } ) );

        const npc_ptr back = overmap_buffer.find_npc( comp_id );
        REQUIRE( back );
        CHECK_FALSE( back->has_companion_mission() );
        CHECK( camp.has_provides( recipe_test_camp_m22a_shed.str(), site_dir ) );
        // the shed's blueprint_needs train tailoring, not its skill_used or "construction"
        CHECK( practiced( *back, skill_tailor ) > 0 );
        CHECK( practiced( *back, skill_fabrication ) == 0 );
        CHECK( global_number( "test_m22a_built" ) == 1 );
    }
}

// CAMP-J04: difficulty is optional, an urgent mission of difficulty 0 is pressed, and the
// load check reports unknown ids and fields a goal needs.
TEST_CASE( "camp-j04_mission_definitions_are_checked", "[audit][mission]" )
{
    CHECK( mission_TEST_MISSION_M22A_FIND_TOKENS->difficulty == 0 );

    const auto check_of = []( const std::string & json ) {
        const JsonValue jv = json_loader::from_string( json );
        mission_type m;
        m.id = mission_type_id( "TEST_M22A_INLINE_MISSION" );
        m.load( jv.get_object(), "test" );
        return capture_debugmsg_during( [&m]() {
            m.check();
        } );
    };
    CHECK( check_of( R"({ "name": "x", "value": 0, "goal": "MGOAL_GO_TO_TYPE" })" ).find(
               "needs a destination" ) != std::string::npos );
    CHECK( check_of( R"({ "name": "x", "value": 0, "goal": "MGOAL_KILL_MONSTER_TYPE",
                          "monster_type": "mon_test_m22a_no_such", "monster_kill_goal": 1 })" ).find(
               "undefined monster_type" ) != std::string::npos );
    CHECK( check_of( R"({ "name": "x", "value": 0, "goal": "MGOAL_KILL_MONSTER_TYPE",
                          "monster_type": "mon_zombie" })" ).find(
               "monster_kill_goal" ) != std::string::npos );
    CHECK( check_of( R"({ "name": "x", "value": 0, "goal": "MGOAL_NULL",
                          "followup": "MISSION_TEST_M22A_NO_SUCH" })" ).find(
               "undefined followup" ) != std::string::npos );
    CHECK( check_of( R"({ "name": "x", "value": 0, "goal": "MGOAL_KILL_MONSTER_TYPE",
                          "monster_type": "mon_zombie", "monster_kill_goal": 3 })" ).empty() );

    SECTION( "an urgent mission of difficulty 0 is pressed" ) {
        clear_avatar();
        clear_npcs();
        clear_map_without_vision();
        mission::clear_all();
        on_out_of_scope cleanup( []() {
            mission::clear_all();
            clear_npcs();
        } );
        get_avatar().setpos( get_map(), tripoint_bub_ms( 60, 60, 0 ) );
        npc &guy = spawn_npc( get_avatar().pos_bub().xy() + point::east * 2, "test_talker" );
        mission *urgent = mission::reserve_new( mission_TEST_MISSION_M22A_URGENT, guy.getID() );
        REQUIRE( urgent->get_type().urgent );
        REQUIRE( urgent->get_type().difficulty == 0 );
        guy.add_new_mission( urgent );
        const std::vector<std::string> topics = get_talker_for( guy )->get_topics( false );
        CHECK( std::find( topics.begin(), topics.end(),
                          guy.chatbin.talk_mission_describe_urgent ) != topics.end() );
        CHECK( guy.chatbin.mission_selected == urgent );
    }
}

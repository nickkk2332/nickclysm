#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "city.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "horde_map.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );

static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id bio_remote( "bio_remote" );
static const bionic_id test_bio_npcj01_high( "test_bio_npcj01_high" );
static const bionic_id test_bio_npcj01_low( "test_bio_npcj01_low" );
static const bionic_id test_bio_npcj01_spell( "test_bio_npcj01_spell" );
static const bionic_id test_bio_npcj01_zap( "test_bio_npcj01_zap" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MONAIJ01_CLEAR( "EOC_TEST_MONAIJ01_CLEAR" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MONAIJ01_DESTINATION( "EOC_TEST_MONAIJ01_DESTINATION" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_MONAIJ01_TARGET( "EOC_TEST_MONAIJ01_TARGET" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_OMAPJ01_CITY( "EOC_TEST_OMAPJ01_CITY" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_OMAPJ01_HORDE( "EOC_TEST_OMAPJ01_HORDE" );

static const itype_id itype_test_usej01_marker( "test_usej01_marker" );

static const oter_str_id oter_field( "field" );

static const trait_id trait_TEST_MUTJ02_REFUND( "TEST_MUTJ02_REFUND" );

// A numeric character variable as math sees it (unset reads as 0).
static double char_var( const Character &you, const std::string &name )
{
    const diag_value *val = you.maybe_get_value( name );
    return val == nullptr ? 0.0 : val->dbl();
}

static double global_num( const std::string &name )
{
    const diag_value *val = get_globals().maybe_get_global_value( name );
    REQUIRE( val != nullptr );
    return val->dbl();
}

// The overmap the OMAP-J01 tests use: away from the one the rest of the suite plays on.
static point_abs_om omapj01_overmap()
{
    return project_to<coords::om>( get_avatar().pos_abs_omt().xy() ) + point_rel_om( 5, -5 );
}

static tripoint_abs_omt omapj01_omt( const point_om_omt &p )
{
    return project_combine( omapj01_overmap(), tripoint_om_omt( p, 0 ) );
}

// OMAP-J01: closest_city had only the "known" filter, and math had no overmap function.
TEST_CASE( "omap-j01_closest_city_filters_and_city_math", "[audit][eoc][overmap]" )
{
    clear_avatar();
    clear_map();
    get_globals().clear_global_values();
    const point_abs_om om_pos = omapj01_overmap();
    overmap &om = overmap_buffer.get( om_pos );
    // The search looks at the overmaps around this one: only the test's cities may be found.
    std::vector<std::pair<overmap *, std::vector<city>>> saved_cities;
    for( int dx = -1; dx <= 1; ++dx ) {
        for( int dy = -1; dy <= 1; ++dy ) {
            if( overmap *near = overmap_buffer.get_existing( om_pos + point_rel_om( dx, dy ) ) ) {
                saved_cities.emplace_back( near, near->cities );
                near->cities.clear();
            }
        }
    }
    on_out_of_scope restore_cities( [&saved_cities]() {
        for( std::pair<overmap *, std::vector<city>> &saved : saved_cities ) {
            saved.first->cities = saved.second;
        }
    } );
    // 5, 30 and 60 overmap tiles from the search point at (90, 90); only the big town is known.
    om.cities.emplace_back( "Audit Smallville", point_om_omt( 95, 90 ), 2 );
    om.cities.emplace_back( "Audit Bigtown", point_om_omt( 120, 90 ), 10 );
    om.cities.emplace_back( "Audit Farburg", point_om_omt( 90, 150 ), 6 );
    overmap_buffer.set_seen( omapj01_omt( { 95, 90 } ), om_vision_level::unseen );
    overmap_buffer.set_seen( omapj01_omt( { 120, 90 } ), om_vision_level::full );
    overmap_buffer.set_seen( omapj01_omt( { 90, 150 } ), om_vision_level::unseen );

    const tripoint_abs_omt search = omapj01_omt( { 90, 90 } );
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    write_var_value( var_type::context, "search_loc", &d, project_to<coords::ms>( search ) );
    write_var_value( var_type::context, "other_loc", &d,
                     project_to<coords::ms>( search + point( 7, 0 ) ) );
    effect_on_condition_EOC_TEST_OMAPJ01_CITY->activate( d );

    // The default is still the closest known city, now stored as a map square location
    CHECK( global_num( "omapj01_known_size" ) == 10 );
    CHECK( global_num( "omapj01_known_distance" ) == 30 );
    const diag_value *known_loc = get_globals().maybe_get_global_value( "omapj01_known_loc" );
    REQUIRE( known_loc != nullptr );
    CHECK( known_loc->tripoint() == project_to<coords::ms>( omapj01_omt( { 120, 90 } ) ) );
    CHECK( global_num( "omapj01_any_size" ) == 2 );
    CHECK( global_num( "omapj01_any_distance" ) == 5 );
    CHECK( global_num( "omapj01_big_size" ) == 10 );
    CHECK( global_num( "omapj01_named_size" ) == 6 );
    CHECK( global_num( "omapj01_tiny_none" ) == 1 );
    CHECK( get_globals().maybe_get_global_value( "omapj01_tiny_found" ) == nullptr );
    CHECK( global_num( "omapj01_near_big_none" ) == 1 );

    CHECK( global_num( "omapj01_m_known_distance" ) == 30 );
    CHECK( global_num( "omapj01_m_any_distance" ) == 5 );
    CHECK( global_num( "omapj01_m_big_size" ) == 10 );
    CHECK( global_num( "omapj01_m_named_size" ) == 6 );
    CHECK( global_num( "omapj01_m_none_distance" ) == -1 );
    CHECK( global_num( "omapj01_m_none_size" ) == 0 );
    CHECK( global_num( "omapj01_m_omt_distance" ) == 7 );
}

// OMAP-J01: EOCs could only signal hordes; they can now place one and count its monsters.
TEST_CASE( "omap-j01_eocs_place_and_count_overmap_hordes", "[audit][eoc][overmap][hordes]" )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    get_globals().clear_global_values();
    overmap &om = overmap_buffer.get( omapj01_overmap() );
    const tripoint_abs_omt horde_omt = omapj01_omt( { 100, 100 } );
    // Open ground for the horde to stand on
    for( int dx = -2; dx <= 2; ++dx ) {
        for( int dy = -2; dy <= 2; ++dy ) {
            overmap_buffer.ter_set( horde_omt + point( dx, dy ), oter_field.id() );
        }
    }
    om.hordes.clear();
    on_out_of_scope clear_hordes( [&om]() {
        om.hordes.clear();
    } );
    REQUIRE( overmap_buffer.get_horde_size( horde_omt ) == 0 );

    dialogue d( get_talker_for( get_avatar() ), nullptr );
    write_var_value( var_type::context, "horde_loc", &d, project_to<coords::ms>( horde_omt ) );
    write_var_value( var_type::context, "near_loc", &d,
                     project_to<coords::ms>( horde_omt + point::east ) );
    effect_on_condition_EOC_TEST_OMAPJ01_HORDE->activate( d );

    CHECK( overmap_buffer.get_horde_size( horde_omt ) == 5 );
    CHECK( global_num( "omapj01_horde_here" ) == 5 );
    CHECK( global_num( "omapj01_horde_next" ) == 0 );
    CHECK( global_num( "omapj01_horde_around" ) == 5 );
    // Far from the loaded map, so still on the overmap
    CHECK( g->num_creatures() == 1 );
}

// Lets the monster plan and move for some turns, as the game's monster turn does.
static void run_monster_ai( monster &mon, int turns )
{
    for( int turn = 0; turn < turns; ++turn ) {
        calendar::turn += 1_turns;
        mon.mod_moves( mon.get_speed() );
        for( int step = 0; step < 10 && mon.get_moves() > 0 && !mon.is_dead(); ++step ) {
            mon.plan();
            mon.move();
        }
    }
}

static void run_monster_goal_eoc( const effect_on_condition_id &eoc, monster &mon,
                                  const std::string &loc_name, const tripoint_abs_ms &loc )
{
    dialogue d( get_talker_for( mon ), nullptr );
    write_var_value( var_type::context, loc_name, &d, loc );
    eoc->activate( d );
}

// MONAI-J01: monster::plan() picks a new destination and target every turn, so an EOC
// couldn't send a monster anywhere: the forced goal is now kept until it's over.
TEST_CASE( "monai-j01_forced_destination_and_target_outlast_the_monster_ai",
           "[audit][monster][eoc]" )
{
    clear_map();
    clear_vehicles();
    clear_avatar();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    globvars.set_global_value( "monaij01_range", 0 );
    globvars.set_global_value( "monaij01_duration", 0 );
    map &here = get_map();
    avatar &u = get_avatar();
    u.setpos( here, tripoint_bub_ms( 60, 60, 0 ) );
    set_time( calendar::turn_zero + 12_hours );
    monster &walker = spawn_test_monster( "mon_test_monaij01_walker", tripoint_bub_ms( 64, 60, 0 ) );
    // Left alone, it goes for the avatar
    REQUIRE( walker.sees( here, u ) );
    walker.plan();
    REQUIRE( walker.get_dest() == u.pos_abs() );

    SECTION( "it heads for a forced destination turn after turn" ) {
        const tripoint_abs_ms dest = here.get_abs( tripoint_bub_ms( 76, 60, 0 ) );
        run_monster_goal_eoc( effect_on_condition_EOC_TEST_MONAIJ01_DESTINATION, walker, "dest", dest );
        REQUIRE( walker.has_forced_goal() );
        const int dist_before = rl_dist( walker.pos_abs(), dest );
        run_monster_ai( walker, 8 );
        CHECK( walker.has_forced_goal() );
        CHECK( walker.get_dest() == dest );
        CHECK( rl_dist( walker.pos_abs(), dest ) <= dist_before - 5 );
        CHECK( rl_dist( walker.pos_abs(), u.pos_abs() ) > 4 );

        // Saved with the monster
        std::ostringstream saved;
        JsonOut jsout( saved );
        walker.serialize( jsout );
        monster loaded;
        loaded.deserialize( json_loader::from_string( saved.str() ).get_object() );
        CHECK( loaded.has_forced_goal() );
        CHECK( loaded.get_forced_destination() == std::optional<tripoint_abs_ms>( dest ) );

        // u_clear_monster_goal hands it back to its AI
        dialogue d( get_talker_for( walker ), nullptr );
        effect_on_condition_EOC_TEST_MONAIJ01_CLEAR->activate( d );
        CHECK_FALSE( walker.has_forced_goal() );
        walker.plan();
        CHECK( walker.get_dest() == u.pos_abs() );
    }

    SECTION( "the destination is forgotten once it's reached" ) {
        const tripoint_abs_ms dest = here.get_abs( tripoint_bub_ms( 66, 60, 0 ) );
        run_monster_goal_eoc( effect_on_condition_EOC_TEST_MONAIJ01_DESTINATION, walker, "dest", dest );
        run_monster_ai( walker, 4 );
        CHECK_FALSE( walker.has_forced_goal() );
        walker.plan();
        CHECK( walker.get_dest() == u.pos_abs() );
    }

    SECTION( "the destination is forgotten when its time is up" ) {
        globvars.set_global_value( "monaij01_duration", 3 );
        const tripoint_abs_ms dest = here.get_abs( tripoint_bub_ms( 90, 60, 0 ) );
        run_monster_goal_eoc( effect_on_condition_EOC_TEST_MONAIJ01_DESTINATION, walker, "dest", dest );
        run_monster_ai( walker, 2 );
        CHECK( walker.has_forced_goal() );
        run_monster_ai( walker, 2 );
        CHECK_FALSE( walker.has_forced_goal() );
        CHECK( walker.get_dest() != dest );
    }

    SECTION( "it follows a forced target until the target is gone" ) {
        // Another monster of its faction, which its AI would never go after
        monster &other = spawn_test_monster( "mon_test_monaij01_walker", tripoint_bub_ms( 64, 72, 0 ) );
        run_monster_goal_eoc( effect_on_condition_EOC_TEST_MONAIJ01_TARGET, walker, "target_loc",
                              other.pos_abs() );
        REQUIRE( walker.get_forced_target() == &other );
        const int dist_before = rl_dist( walker.pos_abs(), other.pos_abs() );
        run_monster_ai( walker, 5 );
        CHECK( walker.get_dest() == other.pos_abs() );
        CHECK( rl_dist( walker.pos_abs(), other.pos_abs() ) <= dist_before - 3 );

        other.die( &here, nullptr );
        g->cleanup_dead();
        walker.plan();
        CHECK_FALSE( walker.has_forced_goal() );
        CHECK( walker.get_dest() == u.pos_abs() );
    }
}

// MUT-J02: an activation EOC could cancel its mutation (turn it back off), but the cost
// stayed paid.
TEST_CASE( "mut-j02_cancelled_mutation_activation_refunds_its_cost", "[audit][mutations]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.set_mutation( trait_TEST_MUTJ02_REFUND );
    REQUIRE( me.can_power_mutation( trait_TEST_MUTJ02_REFUND ) );
    const int kcal_before = me.get_stored_kcal();

    me.set_value( "mutj02_refund_refuse", 1 );
    me.activate_mutation( trait_TEST_MUTJ02_REFUND );
    CHECK_FALSE( me.has_active_mutation( trait_TEST_MUTJ02_REFUND ) );
    // The EOC ran: only the cost is given back
    CHECK( char_var( me, "mutj02_refund_ran" ) == 1 );
    CHECK( me.get_stored_kcal() == kcal_before );

    // An activation that isn't cancelled still pays
    me.set_value( "mutj02_refund_refuse", 0 );
    me.activate_mutation( trait_TEST_MUTJ02_REFUND );
    CHECK( me.has_active_mutation( trait_TEST_MUTJ02_REFUND ) );
    CHECK( me.get_stored_kcal() == kcal_before - 100 );
    clear_avatar();
}

// MUT-J02: bio_remote said "You activate your Remote Controller." before its menu, and stayed
// activated (its activation EOCs ran) when the menu was cancelled.
TEST_CASE( "mut-j02_bio_remote_asks_before_it_activates", "[audit][bionics]" )
{
    clear_avatar();
    avatar &me = get_avatar();
    me.add_bionic( bio_power_storage );
    me.set_power_level( me.get_max_power_level() );
    me.add_bionic( bio_remote );
    const std::optional<bionic *> remote = me.find_bionic_by_type( bio_remote );
    REQUIRE( remote );
    bionic_menus::remote_test_choice = []() {
        return -1;
    };
    on_out_of_scope reset_choice( []() {
        bionic_menus::remote_test_choice = nullptr;
    } );
    const units::energy power_before = me.get_power_level();
    Messages::clear_messages();

    CHECK_FALSE( me.activate_bionic( **remote ) );
    CHECK_FALSE( ( *remote )->powered );
    CHECK( me.get_power_level() == power_before );
    bool activated_message = false;
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        activated_message |= msg.second.find( "You activate your" ) != std::string::npos;
    }
    CHECK_FALSE( activated_message );
    Messages::clear_messages();
    clear_avatar();
}

// NPC-J01: NPCs switched on the CBMs of one npc_use in installation order, whatever they
// were worth, and a spell CBM did nothing for them.
TEST_CASE( "npc-j01_npc_switches_on_the_most_important_cbm_first", "[audit][bionics][npc]" )
{
    standard_npc guy( "Priority" );
    guy.add_bionic( bio_power_storage );
    guy.add_bionic( test_bio_npcj01_low );
    guy.add_bionic( test_bio_npcj01_high );
    REQUIRE( test_bio_npcj01_high->npc_use == bionic_npc_use::combat );
    REQUIRE( test_bio_npcj01_high->npc_use_priority == 10 );
    REQUIRE( test_bio_npcj01_low->npc_use_priority == 0 );
    // Power for only one of them
    guy.set_power_level( 1_kJ );

    guy.activate_combat_cbms();
    CHECK( guy.has_active_bionic( test_bio_npcj01_high ) );
    CHECK_FALSE( guy.has_active_bionic( test_bio_npcj01_low ) );
}

TEST_CASE( "npc-j01_npc_casts_its_spell_cbm", "[audit][bionics][npc][magic]" )
{
    clear_map();
    clear_avatar();
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point_rel_ms( 3, 0 ), "test_talker" );
    const tripoint_bub_ms guy_pos = guy.pos_bub();
    clear_character( guy );
    guy.setpos( get_map(), guy_pos );
    guy.add_bionic( bio_power_storage );
    guy.set_power_level( guy.get_max_power_level() );

    SECTION( "through its npc_use, at itself" ) {
        guy.add_bionic( test_bio_npcj01_spell );
        const units::energy power_before = guy.get_power_level();
        guy.activate_combat_cbms();
        REQUIRE( guy.activity.id() == ACT_SPELLCASTING );
        CHECK( guy.get_power_level() == power_before - 1_kJ );
        REQUIRE( process_activity_bounded( guy, 10, 100 ) );
        CHECK( char_var( guy, "npcj01_spell_cast" ) == 1 );
    }

    SECTION( "with no creature it can cast at, it isn't activated or paid for" ) {
        guy.add_bionic( test_bio_npcj01_zap );
        const units::energy power_before = guy.get_power_level();
        CHECK_FALSE( guy.activate_bionic_by_id( test_bio_npcj01_zap ) );
        CHECK( guy.get_power_level() == power_before );
        CHECK( guy.activity.is_null() );
        CHECK( char_var( guy, "npcj01_spell_cast" ) == 0 );
    }
}

// USE-J01: the effect_on_conditions use action's EOCs didn't know which use action ran or
// where the item was.
TEST_CASE( "use-j01_eoc_use_action_gets_method_and_item_pos", "[audit][iuse][eoc]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    const std::string method = "effect_on_conditions";

    SECTION( "a carried item is where its user is, wherever it's used" ) {
        item_location marker = u.i_add( item( itype_test_usej01_marker ) );
        REQUIRE( marker );
        const tripoint_bub_ms target = u.pos_bub() + tripoint_rel_ms::east * 2;
        u.invoke_item( marker.get_item(), method, target );
        CHECK( globvars.get_global_value( "usej01_method" ).str() == method );
        CHECK( globvars.get_global_value( "usej01_item_pos" ).tripoint() == u.pos_abs() );
        CHECK( globvars.get_global_value( "usej01_use_location" ).tripoint() ==
               here.get_abs( target ) );
    }

    SECTION( "an item on the ground is where it lies" ) {
        const tripoint_bub_ms spot = u.pos_bub() + tripoint_rel_ms::north;
        item &placed = here.add_item( spot, item( itype_test_usej01_marker ) );
        const use_function *use = placed.type->get_use( method );
        REQUIRE( use != nullptr );
        use->call( &u, placed, &here, spot );
        CHECK( globvars.get_global_value( "usej01_method" ).str() == method );
        CHECK( globvars.get_global_value( "usej01_item_pos" ).tripoint() == here.get_abs( spot ) );
    }
}

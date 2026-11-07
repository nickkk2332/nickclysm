#include <functional>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_attire.h"
#include "condition.h"
#include "coordinates.h"
#include "craft_command.h"
#include "crafting.h"
#include "crafting_enums.h"
#include "debug.h"
#include "dialogue.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "item_wakeup.h"
#include "itype.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "talker.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"

static const faction_id faction_no_faction( "no_faction" );

static const flag_id json_flag_FILTHY( "FILTHY" );

static const furn_str_id furn_f_test_m25a_counter( "f_test_m25a_counter" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_anvil( "anvil" );
static const itype_id itype_brick_oven_pseudo( "brick_oven_pseudo" );
static const itype_id itype_butchery_tree_pseudo( "butchery_tree_pseudo" );
static const itype_id itype_chisel( "chisel" );
static const itype_id itype_cotton_patchwork( "cotton_patchwork" );
static const itype_id itype_debug_backpack( "debug_backpack" );
static const itype_id itype_press( "press" );
static const itype_id itype_splinter( "splinter" );
static const itype_id itype_string_36( "string_36" );
static const itype_id itype_test_m25a_arc_welder( "test_m25a_arc_welder" );
static const itype_id itype_test_m25a_pair( "test_m25a_pair" );
static const itype_id itype_test_m25a_paper_book( "test_m25a_paper_book" );
static const itype_id itype_test_m25a_sealed_book( "test_m25a_sealed_book" );
static const itype_id itype_test_m25a_shot( "test_m25a_shot" );
static const itype_id itype_test_m25a_stone_book( "test_m25a_stone_book" );
static const itype_id itype_test_m25a_twin( "test_m25a_twin" );
static const itype_id itype_test_m25a_welded( "test_m25a_welded" );
static const itype_id itype_test_m25a_widget_a( "test_m25a_widget_a" );
static const itype_id itype_test_m25a_widget_b( "test_m25a_widget_b" );
static const itype_id itype_test_rag_bundle( "test_rag_bundle" );
static const itype_id itype_thread( "thread" );
static const itype_id itype_welder( "welder" );

static const quality_id qual_CUT( "CUT" );
static const quality_id qual_HAMMER( "HAMMER" );
static const quality_id qual_PULL( "PULL" );
static const quality_id qual_SAW_M_FINE( "SAW_M_FINE" );
static const quality_id qual_SEW( "SEW" );

static const recipe_id recipe_cudgel_audit_m25a_ruin( "cudgel_audit_m25a_ruin" );
static const recipe_id recipe_cudgel_test_timeout_recipe( "cudgel_test_timeout_recipe" );
static const recipe_id recipe_test_m25a_conditional( "test_m25a_conditional" );
// Named here so it is interned first: the string-intern order used to pick the disassembly.
static const recipe_id recipe_test_m25a_pair_late( "test_m25a_pair_late" );
static const recipe_id recipe_test_m25a_twin_primary( "test_m25a_twin_primary" );

static const ter_str_id ter_t_brick_oven( "t_brick_oven" );
static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_test_m25a_anvil_rock( "t_test_m25a_anvil_rock" );
static const ter_str_id ter_t_test_m25a_bare_tree( "t_test_m25a_bare_tree" );
static const ter_str_id ter_t_tree( "t_tree" );

namespace
{

bool has_message_containing( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

int count_on_tile( const tripoint_bub_ms &p, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            count += it.count();
        }
    }
    return count;
}

// A craft of `rec` on the map at `origin`, in its unattended step 1, overdue to be ruined.
item_location overdue_craft( const recipe &rec, const tripoint_bub_ms &origin )
{
    map &here = get_map();
    item ingredient( itype_2x4, calendar::turn );
    item placed( &rec, 1, ingredient );
    item &on_map = here.add_item( origin, placed );
    REQUIRE( on_map.is_craft() );
    on_map.set_current_step( 1 );
    on_map.set_passive_started_at( calendar::turn );
    on_map.set_ready_at( calendar::turn + 10_minutes );
    on_map.set_fail_at( calendar::turn + 25_minutes );
    on_map.set_crafter_id( get_avatar().getID() );
    on_map.set_step_plans( std::vector<attention_plan>( 2 ) );
    return item_location( map_cursor( here.get_abs( origin ) ), &on_map );
}

int quality_level_in( const requirement_data &reqs, const quality_id &qual )
{
    for( const std::vector<quality_requirement> &group : reqs.get_qualities() ) {
        for( const quality_requirement &q : group ) {
            if( q.type == qual ) {
                return q.level;
            }
        }
    }
    return 0;
}

bool needs_tool( const requirement_data &reqs, const itype_id &tool )
{
    for( const std::vector<tool_comp> &group : reqs.get_tools() ) {
        for( const tool_comp &t : group ) {
            if( t.type == tool ) {
                return true;
            }
        }
    }
    return false;
}

temp_crafting_inventory inventory_around( const tripoint_bub_ms &origin )
{
    temp_crafting_inventory inv;
    inv.form_from_map( &get_map(), origin, 1, nullptr, false );
    return inv;
}

} // namespace

// CRAFT-B02: an unattended craft left past max_time and its grace vanished with no word.
TEST_CASE( "craft-b02_a_ruined_unattended_craft_is_announced_and_runs_on_ruin",
           "[audit][crafting]" )
{
    clear_avatar();
    clear_map();
    Messages::clear_messages();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    on_out_of_scope cleanup( [&globvars]() {
        globvars.clear_global_values();
        Messages::clear_messages();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );
    u.setpos( here, origin );

    SECTION( "on_ruin leaves its result, tells its message and runs its EOCs" ) {
        item_location loc = overdue_craft( recipe_cudgel_audit_m25a_ruin.obj(), origin );
        craft_actualize_scheduled( *loc, item_wakeup_kind::fail_check,
                                   calendar::turn + 30_minutes, loc );
        CHECK( loc.get_item() == nullptr );
        CHECK( count_on_tile( origin, itype_splinter ) == 2 );
        CHECK( has_message_containing( "The audit cudgel cracked as it cured." ) );
        CHECK( globvars.get_global_value( "audit_m25a_ruin_step" ) == 1 );
        CHECK( globvars.get_global_value( "audit_m25a_ruin_batch" ) == 1 );
    }
    SECTION( "without on_ruin the crafter is still told" ) {
        item_location loc = overdue_craft( recipe_cudgel_test_timeout_recipe.obj(), origin );
        craft_actualize_scheduled( *loc, item_wakeup_kind::fail_check,
                                   calendar::turn + 30_minutes, loc );
        CHECK( loc.get_item() == nullptr );
        CHECK( has_message_containing( "was ruined while unattended" ) );
    }
}

// CRAFT-B02: recipe_step::load allowed omitted members, so a typo in a step was ignored.
TEST_CASE( "craft-b02_step_members_are_all_read", "[audit][crafting]" )
{
    const auto load_steps = []( const std::string & step ) {
        JsonObject jo = json_loader::from_string(
                            R"({ "type": "recipe", "result": "cudgel", "id_suffix": "audit_m25a_probe", )"
                            R"("category": "CC_WEAPON", "subcategory": "CSC_WEAPON_BASHING", )"
                            R"("components": [ [ [ "2x4", 1 ] ] ], "steps": [ )"
                            R"({ "name": "Shape", "time": "5 m", "activity_level": "LIGHT_EXERCISE" }, )" +
                            step + " ] }" );
        jo.allow_omitted_members();
        recipe r;
        r.load( jo, "dda" );
    };
    const std::string typo = capture_debugmsg_during( [&]() {
        load_steps( R"({ "name": "Cure", "time": "10 m", "activity_level": "NO_EXERCISE", )"
                    R"("attention": "unattended", "max_tme": "20 m" })" );
    } );
    CHECK( typo.find( "max_tme" ) != std::string::npos );

    CHECK_THROWS_AS( load_steps( R"({ "name": "Cure", "time": "10 m", )"
                                 R"("activity_level": "NO_EXERCISE", "attention": "unattended", )"
                                 R"("on_ruin": { "result": "splinter" } })" ), JsonError );
}

// CRAFT-B03: a reversible step recipe's disassembly summed the craft's steps.
TEST_CASE( "craft-b03_a_reversible_step_recipe_disassembles_in_one_go", "[audit][crafting]" )
{
    clear_avatar();
    const avatar &u = get_avatar();

    const recipe &with_time = recipe_dictionary::get_uncraft( itype_test_m25a_widget_a );
    REQUIRE( static_cast<bool>( with_time ) );
    CHECK_FALSE( with_time.has_steps() );
    // the craft takes 30 m; reversible gives 7 m
    CHECK( with_time.time_to_craft_moves( u, {}, recipe_time_flag::ignore_proficiencies ) ==
           to_moves<int64_t>( 7_minutes ) );

    const recipe &no_time = recipe_dictionary::get_uncraft( itype_test_m25a_widget_b );
    REQUIRE( static_cast<bool>( no_time ) );
    CHECK_FALSE( no_time.has_steps() );
    // 10 m and 5 m worked; the 30 m unattended step is not
    CHECK( no_time.time_to_craft_moves( u, {}, recipe_time_flag::ignore_proficiencies ) ==
           to_moves<int64_t>( 15_minutes ) );
    // its requirements are still those of the steps
    CHECK( !no_time.disassembly_requirements().get_components().empty() );
}

// CRAFT-B04 and CRAFT-J07: substitutes went into the first quality group as alternatives, emptied
// groups stayed, and the table of tools and qualities was hardcoded.
TEST_CASE( "craft-b04_craft-j07_disassembly_substitutes_are_groups_of_their_own",
           "[audit][crafting]" )
{
    // vanilla's table, now in JSON
    REQUIRE( itype_welder->disassembly_sub );
    REQUIRE( itype_press->disassembly_sub );
    REQUIRE( qual_SEW->disassembly_sub );
    REQUIRE( itype_test_m25a_arc_welder->disassembly_sub );

    // HAMMER 1, SEW 2, CUT 1 and a mod's welder: HAMMER 1, CUT 2 (the higher), SAW_M_FINE 2
    const requirement_data welded =
        recipe_dictionary::get_uncraft( itype_test_m25a_welded ).disassembly_requirements();
    const std::vector<std::vector<quality_requirement>> &groups = welded.get_qualities();
    CHECK( groups.size() == 3 );
    for( const std::vector<quality_requirement> &group : groups ) {
        CAPTURE( group.size() );
        CHECK( group.size() == 1 );
    }
    CHECK( quality_level_in( welded, qual_HAMMER ) == 1 );
    CHECK( quality_level_in( welded, qual_CUT ) == 2 );
    CHECK( quality_level_in( welded, qual_SAW_M_FINE ) == 2 );
    CHECK( quality_level_in( welded, qual_SEW ) == 0 );
    CHECK_FALSE( needs_tool( welded, itype_test_m25a_arc_welder ) );

    // fire is listed before the press, and goes with it all the same
    const requirement_data shot =
        recipe_dictionary::get_uncraft( itype_test_m25a_shot ).disassembly_requirements();
    CHECK( shot.get_tools().empty() );
    CHECK( quality_level_in( shot, qual_PULL ) == 1 );
}

// CRAFT-B05: the components a crafted item stored replaced the adjusted copy, so a filthy
// item gave clean parts that still had their old owner.
TEST_CASE( "craft-b05_disassembly_adjusts_the_stored_components", "[audit][crafting]" )
{
    clear_avatar();
    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    u.worn.wear_item( u, item( itype_debug_backpack ), false, false );
    REQUIRE( u.get_faction() != nullptr );

    item bundle( itype_test_rag_bundle );
    item patch( itype_cotton_patchwork );
    patch.set_owner( faction_no_faction );
    for( int i = 0; i < 3; ++i ) {
        bundle.components.add( patch );
    }
    item cord( itype_string_36 );
    bundle.components.add( cord );
    bundle.set_flag( json_flag_FILTHY );
    item_location target = u.i_add( bundle );
    REQUIRE( target );
    item_location in_progress = u.create_in_progress_disassembly( target );
    REQUIRE( in_progress );

    clear_map_without_vision();
    set_time( calendar::turn_zero + 12_hours );
    u.complete_disassemble( in_progress, recipe_dictionary::get_uncraft( itype_test_rag_bundle ) );

    int patches = 0;
    for( const item &it : get_map().i_at( u.pos_bub() ) ) {
        if( it.typeId() != itype_cotton_patchwork ) {
            continue;
        }
        ++patches;
        CHECK( it.is_filthy() );
        CHECK( it.get_owner() == u.get_faction()->id );
    }
    // the stored three, not the recipe's hundred; difficulty 0 and no damage lose none
    CHECK( patches == 3 );
    clear_avatar();
}

// CRAFT-B06: consume_tools over reachable points drew a both-pools selection from each pool
// in full, since the player's draw did not count down what the map then took.
TEST_CASE( "craft-b06_a_both_pools_tool_draw_takes_the_amount_once", "[audit][crafting]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );
    u.setpos( here, origin );
    const tripoint_bub_ms storage = origin + tripoint::east * 3;

    u.set_wielded_item( item( itype_thread, calendar::turn, 5 ) );
    here.add_item( storage, item( itype_thread, calendar::turn, 5 ) );
    REQUIRE( u.charges_of( itype_thread ) == 5 );
    REQUIRE( count_on_tile( storage, itype_thread ) == 5 );

    comp_selection<tool_comp> sel;
    sel.use_from = usage_from::both;
    sel.comp = tool_comp( itype_thread, 8 );
    u.consume_tools( here, sel, 1, std::vector<tripoint_bub_ms> { storage } );

    // 8 of 10, from the hands first
    CHECK( u.charges_of( itype_thread ) + count_on_tile( storage, itype_thread ) == 2 );
    CHECK( u.charges_of( itype_thread ) == 0 );
    CHECK( count_on_tile( storage, itype_thread ) == 2 );
    clear_avatar();
}

// CRAFT-B07: the crafting search's `l:` filter called std::stoi on every digit typed, and a
// long number threw std::out_of_range out of the crafting menu.
TEST_CASE( "craft-b07_difficulty_search_takes_long_numbers", "[audit][crafting]" )
{
    const recipe &rec = recipe_cudgel_test_timeout_recipe.obj();
    REQUIRE( rec.difficulty == 1 );
    recipe_subset subset;
    subset.include( &rec );
    const auto found = [&subset]( const std::string_view txt ) {
        std::vector<const recipe *> res;
        CHECK_NOTHROW( res = subset.search( txt, recipe_subset::search_type::difficulty ) );
        return res.size();
    };
    CHECK( found( "1" ) == 1 );
    CHECK( found( "99999999999" ) == 0 );
    CHECK( found( "1~99999999999" ) == 1 );
    CHECK( found( "99999999999~0" ) == 1 );
    CHECK( found( "2~3" ) == 0 );
    CHECK( found( "~" ) == 1 );
    CHECK( found( "x" ) == 1 );
}

// CRAFT-J03: every book disassembled into paper, a stone tablet too, and there was no opt-out.
TEST_CASE( "craft-j03_only_paper_books_disassemble_into_paper", "[audit][crafting]" )
{
    CHECK( static_cast<bool>( recipe_dictionary::get_uncraft( itype_test_m25a_paper_book ) ) );
    CHECK( item( itype_test_m25a_paper_book ).is_disassemblable() );
    CHECK_FALSE( static_cast<bool>( recipe_dictionary::get_uncraft( itype_test_m25a_stone_book ) ) );
    CHECK_FALSE( item( itype_test_m25a_stone_book ).is_disassemblable() );
    CHECK_FALSE( static_cast<bool>( recipe_dictionary::get_uncraft( itype_test_m25a_sealed_book ) ) );
    CHECK_FALSE( item( itype_test_m25a_sealed_book ).is_disassemblable() );
}

// CRAFT-J05: unchecked recipe flags, and the disassembly of a result with several reversible
// recipes was whichever came first in string-intern order.
TEST_CASE( "craft-j05_recipe_flags_and_disassembly_source", "[audit][crafting]" )
{
    CHECK( is_known_recipe_flag( "BLIND_EASY" ) );
    CHECK( is_known_recipe_flag( "NO_FOOD_REQ" ) );
    CHECK( is_known_recipe_flag( "MAP_MIRROR_VERTICAL_IF_SW" ) );
    CHECK( is_known_recipe_flag( "MAP_ROTATE_90_IF_" ) );
    CHECK_FALSE( is_known_recipe_flag( "BLIND_EAZY" ) );
    CHECK_FALSE( is_known_recipe_flag( "MAP_ROTATE_45" ) );
    CHECK_FALSE( is_known_recipe_flag( "MAP_ROTATE_90_IF_UP" ) );
    // every recipe loaded passes the checks
    CHECK( capture_debugmsg_during( []() {
        recipe_dictionary::check_consistency();
    } ).empty() );

    // "primary" wins although it was loaded second
    CHECK( recipe_dictionary::get_uncraft( itype_test_m25a_twin ).ident().str() ==
           recipe_test_m25a_twin_primary.str() );
    // without one, the first loaded, though the other id was interned first (above)
    CHECK( recipe_dictionary::get_uncraft( itype_test_m25a_pair ).ident().str() !=
           recipe_test_m25a_pair_late.str() );
    CHECK( recipe_dictionary::get_uncraft( itype_test_m25a_pair ).ident().str() ==
           "test_m25a_pair" );
}

// CRAFT-J06: u_know_recipe always asked the avatar, there was no npc_know_recipe, and a recipe
// could not be gated on game state.
TEST_CASE( "craft-j06_know_recipe_asks_the_talker_and_recipes_take_a_condition",
           "[audit][crafting]" )
{
    clear_avatar();
    global_variables &globvars = get_globals();
    globvars.clear_global_values();
    on_out_of_scope cleanup( [&globvars]() {
        globvars.clear_global_values();
    } );
    avatar &u = get_avatar();
    const recipe &rec = recipe_test_m25a_conditional.obj();
    u.forget_recipe( &rec );
    standard_npc guy( "Scholar" );
    clear_character( guy );
    guy.learn_recipe( &rec );
    REQUIRE( guy.knows_recipe( &rec ) );
    REQUIRE_FALSE( u.knows_recipe( &rec ) );

    const auto condition = []( const std::string & json ) {
        const JsonObject jo = json_loader::from_string( json );
        conditional_t::func cond;
        read_condition( jo, "condition", cond, false );
        finalize_conditions();
        return cond;
    };
    const conditional_t::func u_knows =
        condition( R"({ "condition": { "u_know_recipe": "test_m25a_conditional" } })" );
    const conditional_t::func npc_knows =
        condition( R"({ "condition": { "npc_know_recipe": "test_m25a_conditional" } })" );

    const dialogue talk( get_talker_for( u ), get_talker_for( guy ) );
    CHECK_FALSE( u_knows( talk ) );
    CHECK( npc_knows( talk ) );
    // an EOC run on the NPC asks the NPC
    const dialogue on_npc( get_talker_for( guy ), nullptr );
    CHECK( u_knows( on_npc ) );

    // "condition" gates the recipe for its crafter, and says why
    CHECK( rec.has_condition() );
    CHECK_FALSE( rec.meets_condition( u ) );
    CHECK_FALSE( rec.character_meets_requirements( u ) );
    CHECK( rec.condition_fail_text() == "Not until you are allowed to." );
    globvars.set_global_value( "audit_m25a_may_craft", 1 );
    CHECK( rec.meets_condition( u ) );
    CHECK( rec.character_meets_requirements( u ) );
    // a recipe without one is unaffected
    CHECK( recipe_cudgel_test_timeout_recipe->meets_condition( u ) );
    clear_avatar();
}

// CRAFT-J08: terrain could lend no crafting tool but trees' and the brick oven's (hardcoded),
// and furniture only one.
TEST_CASE( "craft-j08_terrain_and_furniture_lend_crafting_pseudo_items", "[audit][crafting]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );

    here.ter_set( origin, ter_t_test_m25a_anvil_rock );
    CHECK( inventory_around( origin ).has_tools( itype_anvil, 1 ) );

    here.ter_set( origin, ter_t_dirt );
    here.furn_set( origin, furn_f_test_m25a_counter );
    CHECK( inventory_around( origin ).has_tools( itype_chisel, 1 ) );
    here.furn_set( origin, furn_str_id::NULL_ID() );

    // the brick oven's tool is in its JSON now, and trees keep theirs unless JSON says not
    here.ter_set( origin, ter_t_brick_oven );
    CHECK( inventory_around( origin ).has_tools( itype_brick_oven_pseudo, 1 ) );
    here.ter_set( origin, ter_t_tree );
    CHECK( inventory_around( origin ).has_tools( itype_butchery_tree_pseudo, 1 ) );
    here.ter_set( origin, ter_t_test_m25a_bare_tree );
    CHECK_FALSE( inventory_around( origin ).has_tools( itype_butchery_tree_pseudo, 1 ) );
    clear_map();
}

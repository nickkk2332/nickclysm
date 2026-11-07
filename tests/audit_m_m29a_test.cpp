#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "activity_type.h"
#include "advanced_inv.h"
#include "advanced_inv_area.h"
#include "advanced_inv_listitem.h"
#include "advanced_inv_pane.h"
#include "avatar.h"
#include "bonuses.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_attire.h"
#include "coordinates.h"
#include "crafting_gui_helpers.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_category.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "mapdata.h"
#include "math_parser.h"
#include "npc.h"
#include "options_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "skill.h"
#include "talker.h"
#include "type_id.h"
#include "uistate.h"
#include "value_ptr.h"
#include "weather_type.h"

static const activity_id ACT_UNLOAD( "ACT_UNLOAD" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_M29A_MAP_OFFER( "EOC_TEST_M29A_MAP_OFFER" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_M29A_OFFER( "EOC_TEST_M29A_OFFER" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_M29A_OPEN_CRAFTING( "EOC_TEST_M29A_OPEN_CRAFTING" );

static const efftype_id effect_test_m29a_lenses( "test_m29a_lenses" );
static const efftype_id effect_transition_contacts( "transition_contacts" );

static const faction_id faction_robofac( "robofac" );

static const furn_str_id furn_test_f_m29a_bench( "test_f_m29a_bench" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_556( "556" );
static const itype_id itype_apple( "apple" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_glasses_reading( "glasses_reading" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_tshirt( "tshirt" );

static const recipe_id recipe_cudgel_test_m29a_condition( "cudgel_test_m29a_condition" );
static const recipe_id recipe_cudgel_test_m29a_no_npc( "cudgel_test_m29a_no_npc" );
static const recipe_id recipe_cudgel_test_m29a_stat( "cudgel_test_m29a_stat" );

static const skill_id skill_electronics( "electronics" );
static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_tailor( "tailor" );
static const skill_id skill_test_m29a_skill( "test_m29a_skill" );

static const trait_id trait_HYPEROPIC( "HYPEROPIC" );

namespace
{

// AIM keeps its layout in the global uistate: start from the defaults, restore afterwards
// (with the menu it asks to reopen after a move).
struct aim_fixture {
    restore_on_out_of_scope<advanced_inv_save_state> restore_aim_state{ uistate.transfer_save };
    restore_on_out_of_scope<std::optional<std::function<void()>>> restore_open_menu{ uistate.open_menu };
    std::unique_ptr<advanced_inventory> advinv;

    aim_fixture() {
        uistate.transfer_save = advanced_inv_save_state();
        advinv = std::make_unique<advanced_inventory>();
        advinv->init();
    }

    advanced_inventory_pane &src() {
        return advinv->get_pane( advinv->get_src() );
    }
    advanced_inventory_pane &dest() {
        return advinv->get_pane( advinv->get_dest() );
    }
    void show( const aim_location src_area, const aim_location dest_area ) {
        src().set_area( advinv->get_one_square( src_area ) );
        dest().set_area( advinv->get_one_square( dest_area ) );
        recalc();
        REQUIRE( src().get_area() == src_area );
        REQUIRE( dest().get_area() == dest_area );
    }
    void recalc() {
        advinv->recalc_pane( advanced_inventory::side::left );
        advinv->recalc_pane( advanced_inventory::side::right );
    }
    // the cursor on the first item of the source pane of this type
    void select( const itype_id &type ) {
        advanced_inventory_pane &pane = src();
        for( size_t i = 0; i < pane.items.size(); ++i ) {
            if( pane.items[i].items.front()->typeId() == type ) {
                pane.index = static_cast<int>( i );
                REQUIRE( pane.get_cur_item_ptr() != nullptr );
                return;
            }
        }
        FAIL( "no " << type.str() << " in the source pane" );
    }
};

void reset_world()
{
    clear_map_without_vision();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    clear_avatar();
}

// Every test changes the avatar or the map: leave them clean for the next one.
struct world_cleanup {
    world_cleanup() {
        reset_world();
    }
    ~world_cleanup() {
        reset_world();
    }
    world_cleanup( const world_cleanup & ) = delete;
    world_cleanup &operator=( const world_cleanup & ) = delete;
};

// The avatar's THIEF_MODE, put back when the test ends.
struct thief_mode_restore {
    std::optional<diag_value> old;
    thief_mode_restore() {
        if( const diag_value *v = get_avatar().maybe_get_value( "THIEF_MODE" ) ) {
            old = *v;
        }
    }
    ~thief_mode_restore() {
        if( old ) {
            get_avatar().set_value( "THIEF_MODE", *old );
        } else {
            get_avatar().remove_value( "THIEF_MODE" );
        }
    }
    thief_mode_restore( const thief_mode_restore & ) = delete;
    thief_mode_restore &operator=( const thief_mode_restore & ) = delete;
};

class collating_preset : public inventory_selector_preset
{
    public:
        collating_preset() {
            _collate_entries = true;
        }
};

double eval_math( const_dialogue const &d, const std::string_view expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

const recipe *first_recipe_with_skill( const skill_id &skill )
{
    for( const auto &entry : recipe_dict ) {
        const recipe &r = entry.second;
        if( r.skill_used == skill && !r.is_nested() && !r.is_practice() && !r.obsolete ) {
            return &r;
        }
    }
    return nullptr;
}

} // namespace

// UIINV-B03: moving a worn item to the inventory in AIM took it off for 0 moves; the take off
// menu charges the obtain cost.
TEST_CASE( "uiinv-b03_aim_take_off_costs_moves", "[audit][inventory][aim]" )
{
    world_cleanup cleanup;
    avatar &u = get_avatar();
    REQUIRE( u.wear_item( item( itype_backpack ), false ).has_value() );
    REQUIRE( u.wear_item( item( itype_tshirt ), false ).has_value() );
    REQUIRE( u.is_wearing( itype_tshirt ) );
    int expected_cost = 0;
    for( const item_location &worn : u.top_items_loc() ) {
        if( worn->typeId() == itype_tshirt ) {
            expected_cost = worn.obtain_cost( u );
        }
    }
    REQUIRE( expected_cost > 0 );

    aim_fixture aim;
    aim.show( AIM_WORN, AIM_INVENTORY );
    aim.select( itype_tshirt );
    u.set_moves( 1000 );
    aim.advinv->process_action( "MOVE_SINGLE_ITEM" );

    CHECK_FALSE( u.is_wearing( itype_tshirt ) );
    CHECK( u.has_item_with( []( const item & it ) {
        return it.typeId() == itype_tshirt;
    } ) );
    CHECK( u.get_moves() == 1000 - expected_cost );
}

// UIINV-B05: AIM's unload in a container pane always said "Nothing to unload".
TEST_CASE( "uiinv-b05_aim_unload_in_a_container_pane", "[audit][inventory][aim]" )
{
    world_cleanup cleanup;
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms north = u.pos_bub() + tripoint_rel_ms::north;
    item mag( itype_stanag30 );
    mag.ammo_set( itype_556, 10 );
    item pack( itype_backpack );
    REQUIRE( pack.put_in( mag, pocket_type::CONTAINER ).success() );
    here.add_item( north, pack );

    aim_fixture aim;
    aim.show( AIM_NORTH, AIM_CENTER );
    aim.select( itype_backpack );
    aim.advinv->process_action( "ITEMS_CONTAINER" );
    aim.recalc();
    REQUIRE( aim.src().get_area() == AIM_CONTAINER );
    REQUIRE( aim.src().container );
    aim.select( itype_stanag30 );

    aim.advinv->process_action( "UNLOAD_CONTAINER" );
    CHECK( u.activity.id() == ACT_UNLOAD );
    u.cancel_activity();
}

// UIINV-B06: unloading asked "steal?" only from the unload menu's Enter key; now every way of
// unloading (Character::unload) asks, and an honest avatar takes nothing.
TEST_CASE( "uiinv-b06_unloading_asks_before_stealing", "[audit][inventory]" )
{
    world_cleanup cleanup;
    avatar &u = get_avatar();
    map &here = get_map();
    thief_mode_restore restore_thief;
    u.set_value( "THIEF_MODE", std::string( "THIEF_HONEST" ) );
    const tripoint_bub_ms north = u.pos_bub() + tripoint_rel_ms::north;

    SECTION( "a container owned by another faction" ) {
        item pack( itype_backpack );
        REQUIRE( pack.put_in( item( itype_apple ), pocket_type::CONTAINER ).success() );
        pack.set_owner( faction_robofac );
        item &placed = here.add_item_or_charges( north, pack );
        REQUIRE_FALSE( placed.is_owned_by( u, true ) );
        item_location loc( map_cursor( north ), &placed );
        CHECK_FALSE( u.unload( loc ) );
        CHECK_FALSE( u.activity );
        CHECK_FALSE( placed.is_container_empty() );
    }
    SECTION( "a magazine owned by another faction" ) {
        item mag( itype_stanag30 );
        mag.ammo_set( itype_556, 10 );
        mag.set_owner( faction_robofac );
        item &placed = here.add_item_or_charges( north, mag );
        item_location loc( map_cursor( north ), &placed );
        CHECK_FALSE( u.unload( loc ) );
        CHECK_FALSE( u.activity );
        CHECK( placed.ammo_remaining() == 10 );
    }
    SECTION( "a container nobody owns is unloaded as before" ) {
        item pack( itype_backpack );
        REQUIRE( pack.put_in( item( itype_apple ), pocket_type::CONTAINER ).success() );
        item &placed = here.add_item_or_charges( north, pack );
        item_location loc( map_cursor( north ), &placed );
        CHECK( u.unload( loc ) );
        CHECK( u.activity.id() == ACT_UNLOAD );
        u.cancel_activity();
    }
}

// UIINV-B07: AIM's sort by category ordered the categories by pointer (JSON load order), not by
// sort_rank as the inventory menu does: clothing (19) came before food (-18).
TEST_CASE( "uiinv-b07_aim_category_sort_uses_sort_rank", "[audit][inventory][aim]" )
{
    world_cleanup cleanup;
    map &here = get_map();
    const tripoint_bub_ms pos = get_avatar().pos_bub();
    here.add_item( pos, item( itype_tshirt ) );
    here.add_item( pos, item( itype_apple ) );
    here.add_item( pos, item( itype_stanag30 ) );
    here.add_item( pos, item( itype_2x4 ) );

    aim_fixture aim;
    aim.show( AIM_CENTER, AIM_NORTH );
    aim.src().sortby = SORTBY_CATEGORY;
    aim.recalc();
    const std::vector<advanced_inv_listitem> &items = aim.src().items;
    REQUIRE( items.size() == 4 );
    for( size_t i = 1; i < items.size(); ++i ) {
        CAPTURE( items[i - 1].cat->get_id().str(), items[i].cat->get_id().str() );
        CHECK_FALSE( *items[i].cat < *items[i - 1].cat );
    }
    int apple_at = -1;
    int tshirt_at = -1;
    for( size_t i = 0; i < items.size(); ++i ) {
        if( items[i].id == itype_apple ) {
            apple_at = static_cast<int>( i );
        } else if( items[i].id == itype_tshirt ) {
            tshirt_at = static_cast<int>( i );
        }
    }
    CHECK( apple_at < tshirt_at );
}

// UIINV-B08: highlighting a hidden copy of a collated group, and the Eat menu's "next copy",
// dereferenced the group's header without checking it was shown: a filter that matches one copy
// but not the first one hides the header.
TEST_CASE( "uiinv-b08_collation_with_a_filtered_header", "[audit][inventory]" )
{
    world_cleanup cleanup;
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub();
    item alpha( itype_apple );
    alpha.set_var( "item_label", std::string( "alpha" ) );
    item omega( itype_apple );
    omega.set_var( "item_label", std::string( "omega" ) );
    // something else either filter shows, so the column stays on screen
    item plank( itype_2x4 );
    plank.set_var( "item_label", std::string( "alpha omega" ) );
    here.add_item( pos, alpha );
    here.add_item( pos, omega );
    here.add_item( pos, plank );
    std::vector<item_location> locs;
    for( item &it : here.i_at( pos ) ) {
        if( it.typeId() == itype_apple ) {
            locs.emplace_back( map_cursor( pos ), &it );
        }
    }
    REQUIRE( locs.size() == 2 );

    collating_preset preset;
    inventory_selector selector( u, preset );
    selector.add_map_items( pos );
    // Laying it out collates the two apples under one header.
    REQUIRE( selector.highlight_one_of( { locs[0] } ) );
    const inventory_entry &header = selector.get_highlighted();
    REQUIRE( header.is_collation_header() );
    const item_location tip = header.collation_meta->tip;
    const item_location other = tip == locs[0] ? locs[1] : locs[0];
    const std::string other_label = other->get_var( "item_label" );

    SECTION( "highlighting the hidden copy" ) {
        selector.set_filter( other_label );
        // the header doesn't match the filter, and the group is collapsed
        CHECK_FALSE( selector.highlight_one_of( { other }, true ) );
    }
    SECTION( "the next copy of an expanded group" ) {
        REQUIRE( selector.highlight_one_of( { other }, true ) );
        REQUIRE( selector.get_highlighted().is_collation_entry() );
        selector.set_filter( other_label );
        REQUIRE( selector.highlight_one_of( { other } ) );
        REQUIRE( selector.get_highlighted().any_item() == other );
        CHECK_FALSE( selector.get_collation_next() );
    }
}

// UIINV-J01: the EOC item menus take a hint, a none message, denials and columns; manual_mult
// passes the amount chosen as _count. The menus can't run in a test: check that the fields load
// (an unread member would be a load error).
TEST_CASE( "uiinv-j01_eoc_item_menu_fields_load", "[audit][inventory][eoc]" )
{
    CHECK( effect_on_condition_EOC_TEST_M29A_OFFER.is_valid() );
    CHECK( effect_on_condition_EOC_TEST_M29A_MAP_OFFER.is_valid() );
}

// UIINV-J02: an EOC can open the crafting menu, a workbench can start it on a search, and math
// can ask whether a recipe can be crafted now.
TEST_CASE( "uiinv-j02_crafting_menu_from_json", "[audit][crafting][eoc]" )
{
    CHECK( effect_on_condition_EOC_TEST_M29A_OPEN_CRAFTING.is_valid() );
    REQUIRE( furn_test_f_m29a_bench->workbench );
    CHECK( furn_test_f_m29a_bench->workbench->recipe_filter == "p:fabrication" );

    restore_on_out_of_scope restore_turn( calendar::turn );
    scoped_weather_override weather( WEATHER_CLEAR );
    world_cleanup cleanup;
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    const recipe &rec = recipe_cudgel_test_m29a_no_npc.obj();
    const_dialogue d( get_const_talker_for( u ), nullptr );

    // doesn't know it
    CHECK( eval_math( d, "u_can_craft('cudgel_test_m29a_no_npc')" ) == 0 );
    u.learn_recipe( &rec, true );
    // no 2x4
    CHECK( eval_math( d, "u_can_craft('cudgel_test_m29a_no_npc')" ) == 0 );
    u.i_add( item( itype_2x4 ) );
    u.invalidate_crafting_inventory();
    CHECK( u.can_craft_now( rec ) );
    CHECK( eval_math( d, "u_can_craft('cudgel_test_m29a_no_npc')" ) == 1 );
    // one 2x4 isn't enough for two
    CHECK( eval_math( d, "u_can_craft('cudgel_test_m29a_no_npc', 'batch': 2)" ) == 0 );

    SECTION( "an NPC is refused a NO_NPC_CRAFT recipe" ) {
        npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
        clear_character( guy );
        guy.learn_recipe( &rec, true );
        guy.i_add( item( itype_2x4 ) );
        guy.invalidate_crafting_inventory();
        CHECK_FALSE( guy.can_craft_now( rec ) );
    }
}

// UIINV-J03: a recipe's JSON condition gates it everywhere, NO_NPC_CRAFT keeps NPCs from it, and
// the theoretical knowledge it needs is "knowledge_ratio" of its difficulty.
TEST_CASE( "uiinv-j03_recipe_condition_and_knowledge_ratio", "[audit][crafting]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    scoped_weather_override weather( WEATHER_CLEAR );
    world_cleanup cleanup;
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    on_out_of_scope forget_var( []() {
        get_avatar().remove_value( "test_m29a_may_craft" );
    } );
    const recipe &rec = recipe_cudgel_test_m29a_condition.obj();
    u.learn_recipe( &rec, true );
    u.i_add( item( itype_2x4 ) );
    u.invalidate_crafting_inventory();
    REQUIRE( rec.hide_if_condition_fails() );
    CHECK( rec.condition_fail_text() == "Only the chosen may craft this." );

    std::vector<Character *> group = { &u };
    std::map<const recipe *, availability> availability_cache;
    const recipe_subset available = u.get_group_available_recipes();
    const recipe_panel_context ctx{ u, group, available, availability_cache };

    SECTION( "the condition fails" ) {
        CHECK_FALSE( rec.meets_condition( u ) );
        CHECK_FALSE( u.can_start_craft( &rec, recipe_filter_flags::none, 1 ) );
        CHECK_FALSE( u.can_craft_now( rec ) );
        const availability avail( u, &rec );
        CHECK_FALSE( avail.can_craft_recipe );
        // the panel lists the condition's message in red (CRAFT-J06's line)
        const recipe_panel failing = build_recipe_panel( rec, avail, 1, ctx );
        CHECK( std::any_of( failing.character_requirements.begin(),
        failing.character_requirements.end(), [&rec]( const recipe_panel_text & line ) {
            return line.text == rec.condition_fail_text() && line.color == c_red;
        } ) );
        // hidden from the menu's list
        const recipe_list_data listed = build_recipe_list( { &rec }, false, true, u, false, nullptr,
                                        false, false, availability_cache, available );
        CHECK( listed.entries.empty() );
        CHECK( listed.num_hidden == 0 );
    }
    SECTION( "the condition holds" ) {
        u.set_value( "test_m29a_may_craft", 1 );
        CHECK( rec.meets_condition( u ) );
        CHECK( u.can_start_craft( &rec, recipe_filter_flags::none, 1 ) );
        const availability avail( u, &rec );
        CHECK( avail.can_craft_recipe );
        const recipe_panel holding = build_recipe_panel( rec, avail, 1, ctx );
        CHECK( std::none_of( holding.character_requirements.begin(),
        holding.character_requirements.end(), [&rec]( const recipe_panel_text & line ) {
            return line.text == rec.condition_fail_text();
        } ) );
        const recipe_list_data listed = build_recipe_list( { &rec }, false, true, u, false, nullptr,
                                        false, false, availability_cache, available );
        CHECK( listed.entries.size() == 1 );
    }
    SECTION( "knowledge_ratio 0.5 of difficulty 5" ) {
        CHECK( rec.required_knowledge_level( u ) == 2 );
        u.set_knowledge_level( skill_fabrication, 1 );
        CHECK_FALSE( availability( u, &rec ).crafter_has_primary_skill );
        u.set_knowledge_level( skill_fabrication, 2 );
        CHECK( availability( u, &rec ).crafter_has_primary_skill );
    }
    SECTION( "recipes without knowledge_ratio need 80% as before" ) {
        int checked = 0;
        for( const auto &entry : recipe_dict ) {
            const recipe &r = entry.second;
            if( r.ident() == rec.ident() || r.skill_used.is_null() ) {
                continue;
            }
            CAPTURE( r.ident().str() );
            CHECK( r.required_knowledge_level( u ) == static_cast<int>( r.get_difficulty( u ) * 0.8f ) );
            if( ++checked >= 200 ) {
                break;
            }
        }
        CHECK( checked > 0 );
    }
    SECTION( "NO_NPC_CRAFT" ) {
        std::string reason;
        CHECK_FALSE( recipe_cudgel_test_m29a_no_npc->npc_can_craft( reason ) );
        CHECK_FALSE( reason.empty() );
        std::string no_reason;
        CHECK( rec.npc_can_craft( no_reason ) );
    }
}

// UIINV-J04: a skill's vision_penalty and crafting stat replace the hardcoded electronics and
// tailoring penalties and INT / 4; the crafting menu reads the same values as the roll (it
// showed a penalty to a crafter wearing transition contact lenses).
TEST_CASE( "uiinv-j04_vision_penalty_and_crafting_stat_from_skills", "[audit][crafting]" )
{
    CHECK( skill_electronics->vision_penalty() == 2.0f );
    CHECK( skill_tailor->vision_penalty() == 1.0f );
    CHECK( skill_fabrication->vision_penalty() == 0.0f );
    // shipped skills keep the old rule: intelligence / 4
    for( const Skill &sk : Skill::skills ) {
        CAPTURE( sk.ident().str() );
        CHECK( sk.crafting_stat() == STAT_INT );
        CHECK( sk.crafting_stat_divisor() == 4.0f );
    }
    CHECK( skill_test_m29a_skill->vision_penalty() == 3.0f );
    CHECK( skill_test_m29a_skill->crafting_stat() == STAT_PER );
    CHECK( skill_test_m29a_skill->crafting_stat_divisor() == 2.0f );

    restore_on_out_of_scope restore_turn( calendar::turn );
    scoped_weather_override weather( WEATHER_CLEAR );
    world_cleanup cleanup;
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    u.toggle_trait( trait_HYPEROPIC );
    REQUIRE( u.has_uncorrected_farsight() );
    const recipe &stat_rec = recipe_cudgel_test_m29a_stat.obj();
    const recipe *electronic = first_recipe_with_skill( skill_electronics );
    REQUIRE( electronic != nullptr );

    std::vector<Character *> group = { &u };
    std::map<const recipe *, availability> availability_cache;
    const recipe_subset available = u.get_group_available_recipes();
    const recipe_panel_context ctx{ u, group, available, availability_cache };

    SECTION( "farsighted without glasses" ) {
        CHECK( u.crafting_vision_penalty( stat_rec ) == 3.0f );
        CHECK( u.crafting_vision_penalty( *electronic ) == 2.0f );
        const recipe_panel panel = build_recipe_panel( stat_rec, availability( u, &stat_rec ), 1, ctx );
        CHECK( panel.has_farsight );
        CHECK( panel.vision_penalty == 3.0f );
    }
    SECTION( "glasses, contacts, transition contacts and FIX_FARSIGHT lenses all correct it" ) {
        SECTION( "reading glasses" ) {
            REQUIRE( u.wear_item( item( itype_glasses_reading ), false ).has_value() );
        }
        SECTION( "transition contacts" ) {
            u.add_effect( effect_transition_contacts, 1_hours );
        }
        SECTION( "an effect with the FIX_FARSIGHT flag" ) {
            u.add_effect( effect_test_m29a_lenses, 1_hours );
        }
        CHECK_FALSE( u.has_uncorrected_farsight() );
        CHECK( u.crafting_vision_penalty( stat_rec ) == 0.0f );
        CHECK( u.crafting_vision_penalty( *electronic ) == 0.0f );
        const recipe_panel panel = build_recipe_panel( stat_rec, availability( u, &stat_rec ), 1, ctx );
        CHECK_FALSE( panel.has_farsight );
        CHECK( panel.vision_penalty == 0.0f );
    }
    SECTION( "the roll takes exactly the skill's penalty" ) {
        const float without_glasses = u.get_recipe_weighted_skill_average( *electronic );
        REQUIRE( u.wear_item( item( itype_glasses_reading ), false ).has_value() );
        const float with_glasses = u.get_recipe_weighted_skill_average( *electronic );
        CHECK( without_glasses == Approx( with_glasses - 2.0f ) );
    }
    SECTION( "the crafting stat" ) {
        u.set_int_base( 10 );
        u.set_per_base( 7 );
        u.reset_stats();
        REQUIRE( u.get_int() == 10 );
        REQUIRE( u.get_per() == 7 );
        CHECK( u.crafting_stat_bonus( *electronic ) == Approx( 2.5f ) );
        CHECK( u.crafting_stat_bonus( stat_rec ) == Approx( 3.5f ) );
        const recipe_panel panel = build_recipe_panel( stat_rec, availability( u, &stat_rec ), 1, ctx );
        CHECK( panel.crafting_stat_name == "perception" );
        CHECK( panel.intelligence == 7 );
        // against perception 8 / 2
        CHECK( panel.int_delta == Approx( -0.5f ) );
    }
}

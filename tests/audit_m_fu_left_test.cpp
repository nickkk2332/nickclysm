#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "coordinates.h"
#include "damage.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "mutation.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "ret_val.h"
#include "stomach.h"
#include "type_id.h"

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );

static const efftype_id effect_hunger_famished( "hunger_famished" );

static const flag_id json_flag_FROZEN( "FROZEN" );
static const flag_id json_flag_NO_INGEST( "NO_INGEST" );

static const furn_str_id furn_f_test_fu_left_crate( "f_test_fu_left_crate" );

static const itype_id itype_apple( "apple" );
static const itype_id itype_corpse( "corpse" );
static const itype_id itype_crowbar( "crowbar" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_knife_huge( "knife_huge" );
static const itype_id itype_neccowafers( "neccowafers" );
static const itype_id itype_pan( "pan" );
static const itype_id itype_pot( "pot" );
static const itype_id itype_saw( "saw" );
static const itype_id itype_screwdriver( "screwdriver" );
static const itype_id itype_test_fu_left_bad_drink( "test_fu_left_bad_drink" );
static const itype_id itype_test_fu_left_bad_food( "test_fu_left_bad_food" );
static const itype_id itype_test_fu_left_good_food( "test_fu_left_good_food" );
static const itype_id itype_wrench( "wrench" );

static const json_character_flag json_flag_EATDEAD_DIET( "EATDEAD_DIET" );
static const json_character_flag json_flag_SAPROPHAGE_DIET( "SAPROPHAGE_DIET" );
static const json_character_flag json_flag_SAPROVORE_DIET( "SAPROVORE_DIET" );

static const morale_type morale_sweettooth( "morale_sweettooth" );

static const mtype_id mon_test_fu_left_corpse( "mon_test_fu_left_corpse" );
static const mtype_id mon_test_fu_left_remove_corpse( "mon_test_fu_left_remove_corpse" );
static const mtype_id mon_test_fu_left_target( "mon_test_fu_left_target" );

static const oter_str_id oter_test_fu_left_start( "test_fu_left_start" );

static const trait_id trait_CARNIVORE( "CARNIVORE" );
static const trait_id trait_EATDEAD( "EATDEAD" );
static const trait_id trait_GOURMAND( "GOURMAND" );
static const trait_id trait_M_DEPENDENT( "M_DEPENDENT" );
static const trait_id trait_PICKYEATER( "PICKYEATER" );
static const trait_id trait_PROBOSCIS( "PROBOSCIS" );
static const trait_id trait_PROJUNK( "PROJUNK" );
static const trait_id trait_SAPROPHAGE( "SAPROPHAGE" );
static const trait_id trait_SAPROVORE( "SAPROVORE" );
static const trait_id trait_TEST_FU_LEFT_EAT_EOC( "TEST_FU_LEFT_EAT_EOC" );
static const trait_id trait_TEST_FU_LEFT_FOOD_FUN( "TEST_FU_LEFT_FOOD_FUN" );
static const trait_id trait_TEST_FU_LEFT_REFUSE_FIRST( "TEST_FU_LEFT_REFUSE_FIRST" );
static const trait_id trait_TEST_FU_LEFT_REFUSE_LAST( "TEST_FU_LEFT_REFUSE_LAST" );
static const trait_id trait_TEST_FU_LEFT_SAPROPHAGE_DIET( "TEST_FU_LEFT_SAPROPHAGE_DIET" );
static const trait_id trait_TEST_FU_LEFT_SAPROVORE_DIET( "TEST_FU_LEFT_SAPROVORE_DIET" );

namespace
{
// The refusal message, or "" when the food can be eaten
std::string refusal( const Character &you, const item &food )
{
    const ret_val<edible_rating> ret = you.can_eat( food );
    return ret.success() ? std::string() : ret.str();
}

// Gives an OMT another terrain for the length of a test.
class omt_terrain_override
{
    public:
        omt_terrain_override( const tripoint_abs_omt &where, const oter_str_id &ter ) :
            omt( where ), saved( overmap_buffer.ter( where ) ) {
            overmap_buffer.ter_set( omt, ter.id() );
        }
        ~omt_terrain_override() {
            overmap_buffer.ter_set( omt, saved );
        }
        omt_terrain_override( const omt_terrain_override & ) = delete;
        omt_terrain_override &operator=( const omt_terrain_override & ) = delete;
    private:
        tripoint_abs_omt omt;
        oter_id saved;
};

// Keeps every monster_takes_damage event sent while it is subscribed
struct monster_damage_events : event_subscriber {
    using event_subscriber::notify;
    std::vector<cata::event> seen;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::monster_takes_damage ) {
            seen.push_back( e );
        }
    }
};

// The damage type a monster_takes_damage event reports
damage_type_id damage_type_of( const cata::event &e )
{
    return e.get<cata_variant_type::damage_type_id>( "damage_type" );
}

// The item types on a tile, sorted
std::vector<itype_id> types_at( map &here, const tripoint_bub_ms &p )
{
    std::vector<itype_id> ret;
    for( const item &it : here.i_at( p ) ) {
        ret.push_back( it.typeId() );
    }
    std::sort( ret.begin(), ret.end() );
    return ret;
}

// The corpse lying at p, or nullptr
const item *corpse_at( map &here, const tripoint_bub_ms &p )
{
    for( const item &it : here.i_at( p ) ) {
        if( it.typeId() == itype_corpse ) {
            return &it;
        }
    }
    return nullptr;
}
} // namespace

// HLTH-J03: PICKYEATER refused food and drink by taste in C++; it is two eat_conditions now, with
// the same messages, checked last as before.
TEST_CASE( "hlth-j03_picky_eater_refuses_by_taste_from_json", "[audit][food][mutations]" )
{
    avatar dummy;
    dummy.set_body();
    dummy.set_thirst( 0 );
    const item bad_food( itype_test_fu_left_bad_food );
    const item bad_drink( itype_test_fu_left_bad_drink );
    const item good_food( itype_test_fu_left_good_food );
    REQUIRE( dummy.fun_for( bad_food ).first < 0 );
    REQUIRE( dummy.fun_for( bad_drink ).first < 0 );
    REQUIRE( dummy.fun_for( good_food ).first > 0 );
    REQUIRE( refusal( dummy, bad_food ).empty() );
    REQUIRE( refusal( dummy, bad_drink ).empty() );

    dummy.toggle_trait( trait_PICKYEATER );
    REQUIRE( dummy.has_trait( trait_PICKYEATER ) );
    CHECK( trait_PICKYEATER->eat_conditions.size() == 2 );

    SECTION( "food that tastes bad, unless famished" ) {
        CHECK( dummy.can_eat( bad_food ).value() == INEDIBLE_MUTATION );
        CHECK( refusal( dummy, bad_food ) == "You deserve better food than this." );
        dummy.add_effect( effect_hunger_famished, 1_hours );
        CHECK( refusal( dummy, bad_food ).empty() );
    }
    SECTION( "a drink that tastes bad, unless thirsty" ) {
        CHECK( dummy.can_eat( bad_drink ).value() == INEDIBLE_MUTATION );
        CHECK( refusal( dummy, bad_drink ) == "You are not going to drink this." );
        dummy.set_thirst( 240 );
        CHECK( refusal( dummy, bad_drink ).empty() );
    }
    SECTION( "food that tastes good" ) {
        CHECK( refusal( dummy, good_food ).empty() );
    }
}

// HLTH-J03: GOURMAND's taste change was C++; it is a food_fun now, and any mutation can have one.
TEST_CASE( "hlth-j03_food_fun_changes_taste_like_gourmand_did", "[audit][food][mutations]" )
{
    avatar dummy;
    dummy.set_body();
    const item bad_food( itype_test_fu_left_bad_food );
    const item good_food( itype_test_fu_left_good_food );
    // Plain taste: the cap is 6 times bad taste and 3 times good taste
    REQUIRE( dummy.fun_for( bad_food, true ) == std::make_pair( -10, -60 ) );
    REQUIRE( dummy.fun_for( good_food, true ) == std::make_pair( 6, 18 ) );

    SECTION( "GOURMAND halves bad taste with a cap of the old taste, and makes good taste "
             "1.5 times better with 3 times the cap" ) {
        dummy.toggle_trait( trait_GOURMAND );
        REQUIRE( dummy.has_trait( trait_GOURMAND ) );
        REQUIRE( trait_GOURMAND->food_fun.has_value() );
        CHECK( dummy.fun_for( bad_food, true ) == std::make_pair( -5, -10 ) );
        CHECK( dummy.fun_for( good_food, true ) == std::make_pair( 9, 54 ) );
    }
    SECTION( "a JSON-only food_fun without negative_max keeps the cap of bad taste" ) {
        dummy.toggle_trait( trait_TEST_FU_LEFT_FOOD_FUN );
        REQUIRE( dummy.has_trait( trait_TEST_FU_LEFT_FOOD_FUN ) );
        CHECK( dummy.fun_for( bad_food, true ) == std::make_pair( -5, -60 ) );
        CHECK( dummy.fun_for( good_food, true ) == std::make_pair( 12, 18 ) );
    }
}

// HLTH-J03: an eat_condition's "checked" puts it where the hardcoded check it replaced was, so the
// same message wins when several checks refuse the food.
TEST_CASE( "hlth-j03_eat_condition_stages_keep_the_old_message_order", "[audit][food][mutations]" )
{
    avatar dummy;
    dummy.set_body();
    item frozen_food( itype_test_fu_left_bad_food );
    frozen_food.set_flag( json_flag_FROZEN );
    const std::string frozen_message =
        "It's frozen solid.  You must defrost it before you can eat it.";
    REQUIRE( refusal( dummy, frozen_food ) == frozen_message );

    SECTION( "\"first\" comes before the frozen check, \"last\" after every check" ) {
        dummy.toggle_trait( trait_TEST_FU_LEFT_REFUSE_FIRST );
        CHECK( refusal( dummy, frozen_food ) == "Test: refused first." );
        dummy.toggle_trait( trait_TEST_FU_LEFT_REFUSE_FIRST );
        dummy.toggle_trait( trait_TEST_FU_LEFT_REFUSE_LAST );
        REQUIRE( dummy.has_trait( trait_TEST_FU_LEFT_REFUSE_LAST ) );
        CHECK( refusal( dummy, frozen_food ) == frozen_message );
        CHECK( refusal( dummy, item( itype_test_fu_left_bad_food ) ) == "Test: refused last." );
    }
    SECTION( "M_DEPENDENT still refuses frozen food for not being Mycus" ) {
        dummy.toggle_trait( trait_M_DEPENDENT );
        REQUIRE( dummy.has_trait( trait_M_DEPENDENT ) );
        CHECK( dummy.can_eat( frozen_food ).value() == INEDIBLE_MUTATION );
        CHECK( refusal( dummy, frozen_food ) == "We can't eat that.  It's not right for us." );
    }
    SECTION( "PROBOSCIS still refuses solid food before a carnivore's diet does" ) {
        const item apple( itype_apple );
        dummy.toggle_trait( trait_CARNIVORE );
        REQUIRE( refusal( dummy, apple ) == "Eww.  Inedible plant stuff!" );
        dummy.toggle_trait( trait_PROBOSCIS );
        REQUIRE( dummy.has_trait( trait_PROBOSCIS ) );
        CHECK( refusal( dummy, apple ) == "Ugh, you can't drink that!" );
        CHECK( refusal( dummy, item( itype_test_fu_left_bad_drink ) ).empty() );
    }
}

// HLTH-J03: PROJUNK's sweet tooth was C++; mutations have eat_eocs now, run with the eater as u_
// and the food as npc_ when food is swallowed.
TEST_CASE( "hlth-j03_eat_eocs_run_when_food_is_swallowed", "[audit][food][mutations][eoc]" )
{
    avatar dummy;
    dummy.set_body();

    SECTION( "a JSON-only eat_eoc sees the food as npc_, but not food that isn't swallowed" ) {
        dummy.toggle_trait( trait_TEST_FU_LEFT_EAT_EOC );
        REQUIRE( dummy.has_trait( trait_TEST_FU_LEFT_EAT_EOC ) );
        item food( itype_test_fu_left_good_food );
        dummy.modify_morale( food );
        const diag_value *ate = dummy.maybe_get_value( "fu_left_ate" );
        REQUIRE( ate != nullptr );
        CHECK( ate->dbl() == 1 );
        CHECK( dummy.maybe_get_value( "fu_left_ate_food" ) != nullptr );
        food.set_flag( json_flag_NO_INGEST );
        dummy.modify_morale( food );
        CHECK( dummy.maybe_get_value( "fu_left_ate" )->dbl() == 1 );
    }
    SECTION( "PROJUNK's sweet tooth" ) {
        dummy.toggle_trait( trait_PROJUNK );
        REQUIRE( dummy.has_trait( trait_PROJUNK ) );
        REQUIRE_FALSE( trait_PROJUNK->eat_eocs.empty() );
        item junk( itype_neccowafers );
        dummy.clear_morale();
        dummy.modify_morale( junk );
        CHECK( dummy.has_morale( morale_sweettooth ) == 5 );
    }
}

// HLTH-J03: SAPROPHAGE, SAPROVORE and EATDEAD were trait ids in C++; their diets are character
// flags now, which any mutation can have.
TEST_CASE( "hlth-j03_rot_diets_come_from_character_flags", "[audit][food][mutations]" )
{
    CHECK( trait_SAPROPHAGE->flags.count( json_flag_SAPROPHAGE_DIET ) == 1 );
    CHECK( trait_SAPROVORE->flags.count( json_flag_SAPROVORE_DIET ) == 1 );
    CHECK( trait_EATDEAD->flags.count( json_flag_EATDEAD_DIET ) == 1 );

    avatar dummy;
    dummy.set_body();
    const item fresh( itype_test_fu_left_good_food );
    item rotten( itype_test_fu_left_good_food );
    rotten.set_relative_rot( 1.2 );
    REQUIRE( rotten.rotten() );
    const int fresh_kcal = dummy.compute_effective_nutrients( fresh ).kcal();
    REQUIRE( fresh_kcal > 0 );
    // Rot takes calories and taste, and the eat menu warns about it
    REQUIRE( dummy.compute_effective_nutrients( rotten ).kcal() < fresh_kcal );
    REQUIRE( dummy.fun_for( rotten, true ).first < dummy.fun_for( fresh, true ).first );
    REQUIRE( dummy.will_eat( rotten ).value() == ROTTEN );

    SECTION( "SAPROVORE_DIET: rot doesn't spoil the taste or warn" ) {
        dummy.toggle_trait( trait_TEST_FU_LEFT_SAPROVORE_DIET );
        REQUIRE( dummy.has_flag( json_flag_SAPROVORE_DIET ) );
        CHECK( dummy.fun_for( rotten, true ) == dummy.fun_for( fresh, true ) );
        CHECK( dummy.will_eat( rotten ).success() );
        CHECK( dummy.compute_effective_nutrients( rotten ).kcal() < fresh_kcal );
    }
    SECTION( "SAPROPHAGE_DIET: all the calories of rotten food, and fresh food upsets it" ) {
        dummy.toggle_trait( trait_TEST_FU_LEFT_SAPROPHAGE_DIET );
        REQUIRE( dummy.has_flag( json_flag_SAPROPHAGE_DIET ) );
        CHECK( dummy.compute_effective_nutrients( rotten ).kcal() == fresh_kcal );
        CHECK( dummy.fun_for( rotten, true ) == dummy.fun_for( fresh, true ) );
        CHECK( dummy.will_eat( rotten ).success() );
        CHECK( dummy.will_eat( fresh ).value() == ALLERGY_WEAK );
    }
}

// LOOP-J02: starting a game sent avatar_enters_omt but never ran the start OMT's entry_eoc.
TEST_CASE( "loop-j02_game_start_runs_the_start_omt_entry_eoc", "[audit][overmap][eoc]" )
{
    clear_avatar();
    clear_map_without_vision();
    global_variables &globals = get_globals();
    globals.clear_global_values();
    on_out_of_scope cleanup( []() {
        get_globals().clear_global_values();
    } );
    const tripoint_abs_omt start = get_avatar().pos_abs_omt();
    const omt_terrain_override set_start( start, oter_test_fu_left_start );

    // What game::start_game calls, once, for the OMT the avatar starts in
    cata_event_dispatch::avatar_starts_in_omt();

    REQUIRE( globals.get_global_value( "fu_left_start_entries" ) == 1 );
    CHECK( globals.maybe_get_global_value( "fu_left_start_exits" ) == nullptr );
    CHECK( globals.get_global_value( "fu_left_start_omt_pos" ).tripoint() ==
           project_to<coords::ms>( start ) );
    CHECK( globals.get_global_value( "fu_left_start_entered" ).str() ==
           oter_test_fu_left_start.str() );
    // No OMT was left
    CHECK( globals.get_global_value( "fu_left_start_left_empty" ) == 1 );
}

// MONDT-J05: the death EOC ran before the corpse existed, so it couldn't change the corpse.
TEST_CASE( "mondt-j05_death_eoc_runs_after_the_corpse_is_made", "[audit][monster][eoc]" )
{
    clear_map_without_vision();
    clear_creatures();
    global_variables &globals = get_globals();
    globals.remove_global_value( "fu_left_corpse_loc" );
    on_out_of_scope cleanup( []() {
        get_globals().remove_global_value( "fu_left_corpse_loc" );
        clear_creatures();
        clear_map_without_vision();
    } );
    map &here = get_map();
    const tripoint_bub_ms where = get_avatar().pos_bub() + tripoint_rel_ms( 3, 0, 0 );

    SECTION( "it can change the corpse, found at corpse_loc" ) {
        monster &mon = spawn_test_monster( mon_test_fu_left_corpse.str(), where );
        const tripoint_abs_ms death_loc = mon.pos_abs();
        // 1 HP of overkill: a whole corpse, not a splattered one
        mon.apply_damage( nullptr, body_part_torso.id(), mon.get_hp() + 1 );
        mon.die( &here, nullptr );

        const diag_value *corpse_loc = globals.maybe_get_global_value( "fu_left_corpse_loc" );
        REQUIRE( corpse_loc != nullptr );
        CHECK( corpse_loc->tripoint() == death_loc );
        const item *corpse = corpse_at( here, here.get_bub( death_loc ) );
        REQUIRE( corpse != nullptr );
        CHECK( corpse->maybe_get_value( "fu_left_tagged" ) != nullptr );
    }
    SECTION( "npc_die with remove_corpse still stops the corpse" ) {
        monster &mon = spawn_test_monster( mon_test_fu_left_remove_corpse.str(), where );
        const tripoint_abs_ms death_loc = mon.pos_abs();
        mon.apply_damage( nullptr, body_part_torso.id(), mon.get_hp() + 1 );
        mon.die( &here, nullptr );
        CHECK( corpse_at( here, here.get_bub( death_loc ) ) == nullptr );
    }
}

// MONDT-J05: monster_takes_damage had only the damage and whether the monster died.
TEST_CASE( "mondt-j05_monster_takes_damage_has_damage_type_and_body_part",
           "[audit][monster][event]" )
{
    clear_map_without_vision();
    clear_creatures();
    global_variables &globals = get_globals();
    const auto forget = []() {
        global_variables &g_vars = get_globals();
        g_vars.remove_global_value( "fu_left_watch_damage" );
        g_vars.remove_global_value( "fu_left_damage_type" );
        g_vars.remove_global_value( "fu_left_bodypart" );
    };
    forget();
    on_out_of_scope cleanup( [&forget]() {
        forget();
        clear_creatures();
        clear_map_without_vision();
    } );
    monster &mon = spawn_test_monster( mon_test_fu_left_target.str(),
                                       get_avatar().pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    const bodypart_id torso = body_part_torso.id();
    monster_damage_events events;
    get_event_bus().subscribe( &events );

    SECTION( "an attack reports the type that dealt the most, also to EVENT EOCs" ) {
        globals.set_global_value( "fu_left_watch_damage", 1 );
        damage_instance hit( damage_bash, 2 );
        hit.add_damage( damage_cut, 6 );
        mon.deal_damage( nullptr, torso, hit );
        REQUIRE( events.seen.size() == 1 );
        CHECK( damage_type_of( events.seen[0] ) == damage_cut );
        CHECK( events.seen[0].get<cata_variant_type::body_part>( "bodypart" ) == torso );
        CHECK( globals.get_global_value( "fu_left_damage_type" ).str() == damage_cut.str() );
        CHECK( globals.get_global_value( "fu_left_bodypart" ).str() == body_part_torso.str() );
    }
    SECTION( "a fall is bash damage" ) {
        mon.impact( 10, mon.pos_bub() );
        REQUIRE( events.seen.size() == 1 );
        CHECK( damage_type_of( events.seen[0] ) == damage_bash );
    }
    SECTION( "damage with no type, like bleeding, has an empty one" ) {
        mon.apply_damage( nullptr, torso, 1 );
        REQUIRE( events.seen.size() == 1 );
        CHECK( damage_type_of( events.seen[0] ).str().empty() );
        CHECK( events.seen[0].get<cata_variant_type::body_part>( "bodypart" ) == torso );
    }
}

// Upstream CleverRaven/Cataclysm-DDA#89419 (ported in "Port upstream #89419"): dragging a
// container onto a tile with items was meant to swap the two tiles' items, but lost them instead.
TEST_CASE( "upstream-89419_dragged_furniture_swaps_the_items_on_both_tiles",
           "[audit][map][furniture]" )
{
    clear_avatar();
    clear_map_without_vision();
    on_out_of_scope cleanup( []() {
        get_avatar().grab( object_type::NONE );
        clear_map_without_vision();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms start( 60, 60, 0 );
    u.setpos( here, start );
    const tripoint_bub_ms furn_pos = start + tripoint_rel_ms::east;
    const tripoint_bub_ms dest = furn_pos + tripoint_rel_ms::south;
    here.furn_set( furn_pos, furn_f_test_fu_left_crate );
    REQUIRE( here.furn( furn_pos ) == furn_f_test_fu_left_crate );

    std::vector<itype_id> in_crate = {
        itype_hammer, itype_screwdriver, itype_wrench, itype_knife_huge
    };
    std::vector<itype_id> on_dest = { itype_pot, itype_pan, itype_saw, itype_crowbar };
    for( const itype_id &id : in_crate ) {
        here.add_item( furn_pos, item( id ) );
    }
    for( const itype_id &id : on_dest ) {
        here.add_item( dest, item( id ) );
    }
    std::sort( in_crate.begin(), in_crate.end() );
    std::sort( on_dest.begin(), on_dest.end() );
    REQUIRE( types_at( here, furn_pos ) == in_crate );
    REQUIRE( types_at( here, dest ) == on_dest );

    u.grab( object_type::FURNITURE, tripoint_rel_ms::east );
    REQUIRE( u.get_grab_type() == object_type::FURNITURE );
    // Sideways: the crate moves south and the avatar stays
    g->grabbed_furn_move( tripoint_rel_ms::south );

    REQUIRE( here.furn( dest ) == furn_f_test_fu_left_crate );
    CHECK_FALSE( here.has_furn( furn_pos ) );
    CHECK( types_at( here, dest ) == in_crate );
    CHECK( types_at( here, furn_pos ) == on_dest );
}

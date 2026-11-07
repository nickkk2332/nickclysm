#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "construction.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "iexamine.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "requirements.h"
#include "rng.h"
#include "trap.h"
#include "type_id.h"

static const activity_id ACT_CHOP_PLANKS( "ACT_CHOP_PLANKS" );

static const construction_str_id construction_audit_m28a_child_bare( "audit_m28a_child_bare" );
static const construction_str_id
construction_audit_m28a_child_changed( "audit_m28a_child_changed" );
static const construction_str_id
construction_audit_m28a_child_replaced( "audit_m28a_child_replaced" );
static const construction_str_id construction_audit_m28a_parent( "audit_m28a_parent" );
static const construction_str_id construction_constr_chop_trunk( "constr_chop_trunk" );
static const construction_str_id construction_constr_dig_downstair( "constr_dig_downstair" );
static const construction_str_id construction_constr_mine_upstair( "constr_mine_upstair" );

static const furn_str_id furn_f_bathtub( "f_bathtub" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_canteen( "canteen" );
static const itype_id itype_cash_card( "cash_card" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_clean( "water_clean" );

static const quality_id qual_SAW_W( "SAW_W" );

static const requirement_id
requirement_data_inline_construction_audit_m28a_child_bare( "inline_construction_audit_m28a_child_bare" );
static const requirement_id
requirement_data_inline_construction_audit_m28a_child_changed( "inline_construction_audit_m28a_child_changed" );
static const requirement_id
requirement_data_inline_construction_audit_m28a_child_replaced( "inline_construction_audit_m28a_child_replaced" );

static const skill_id skill_fabrication( "fabrication" );

static const ter_str_id ter_t_open_air( "t_open_air" );
static const ter_str_id ter_t_rock( "t_rock" );

static const trait_id trait_STOCKY_TROGLO( "STOCKY_TROGLO" );

static const trap_str_id test_m28a_rearming_eoc_trap( "test_m28a_rearming_eoc_trap" );
static const trap_str_id test_m28a_unconsumed_eoc_trap( "test_m28a_unconsumed_eoc_trap" );

namespace
{

int count_items( const tripoint_bub_ms &p, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            count += it.count();
        }
    }
    return count;
}

struct needs_spent {
    int kcal = 0;
    int thirst = 0;
    int sleepiness = 0;
};

// What finishing con at site costs the avatar, with or without an underground trait
needs_spent dig_cost( const construction_str_id &con, const tripoint_bub_ms &site,
                      const bool troglo )
{
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();
    if( troglo ) {
        u.set_mutation( trait_STOCKY_TROGLO );
    }
    REQUIRE( u.has_trait( trait_STOCKY_TROGLO ) == troglo );
    // neither lava nor water next to it, which would stop the dig
    here.ter_set( site, ter_t_rock );
    here.ter_set( site + tripoint::above, ter_t_open_air );
    here.ter_set( site + tripoint::below, ter_t_rock );
    const int kcal = u.get_stored_kcal();
    const int thirst = u.get_thirst();
    const int sleepiness = u.get_sleepiness();
    REQUIRE( con->post_specials.size() == 1 );
    con->post_specials.front()( site, u );
    needs_spent spent;
    spent.kcal = kcal - u.get_stored_kcal();
    spent.thirst = u.get_thirst() - thirst;
    spent.sleepiness = u.get_sleepiness() - sleepiness;
    return spent;
}

void load_construction_json( construction &con, const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    con.load( jo, "dda" );
}

void give_cash_card( avatar &u, const int cents )
{
    u.remove_weapon();
    item card( itype_cash_card );
    card.ammo_set( card.ammo_default(), cents );
    u.set_wielded_item( card );
    REQUIRE( u.charges_of( itype_cash_card ) == cents );
}

} // namespace

// TERR-B03: the extra cost of digging stairs and mining up or down was paid by the characters
// with the underground traits, not by everyone else.
TEST_CASE( "terr-b03_underground_traits_make_digging_less_tiring", "[audit][construction]" )
{
    clear_map( -2, 1 );
    const on_out_of_scope cleanup( []() {
        clear_avatar();
        clear_map( -2, 1 );
    } );
    const tripoint_bub_ms site = get_avatar().pos_bub() + tripoint::east;

    SECTION( "mining up" ) {
        const needs_spent plain = dig_cost( construction_constr_mine_upstair, site, false );
        const needs_spent troglo = dig_cost( construction_constr_mine_upstair, site, true );
        CHECK( plain.kcal == 174 + 9 * 15 );
        CHECK( plain.thirst == 20 + 15 );
        CHECK( plain.sleepiness == 25 + 15 );
        CHECK( troglo.kcal == 174 );
        CHECK( troglo.thirst == 20 );
        CHECK( troglo.sleepiness == 25 );
    }
    SECTION( "digging a stairway down" ) {
        const needs_spent plain = dig_cost( construction_constr_dig_downstair, site, false );
        const needs_spent troglo = dig_cost( construction_constr_dig_downstair, site, true );
        CHECK( plain.kcal == 43 + 9 * 10 );
        CHECK( plain.thirst == 5 + 10 );
        CHECK( plain.sleepiness == 10 + 10 );
        CHECK( troglo.kcal == 43 );
        CHECK( troglo.thirst == 5 );
        CHECK( troglo.sleepiness == 10 );
    }
}

// TERR-B04: a container holding liquid in a bathtub of water made the tub "empty" (and filling
// it deleted the water), and pouring into a keg with anything else in it failed.
TEST_CASE( "terr-b04_keg_liquid_ignores_containers_lying_in_it", "[audit][examine]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms tub = get_avatar().pos_bub() + tripoint::east;
    const on_out_of_scope cleanup( [&here, tub]() {
        here.i_clear( tub );
        here.furn_set( tub, furn_str_id::NULL_ID() );
    } );
    here.furn_set( tub, furn_f_bathtub );
    REQUIRE( iexamine::has_keg( tub ) );
    item canteen( itype_canteen );
    REQUIRE( canteen.fill_with( item( itype_water_clean ), 4 ) == 4 );

    SECTION( "the water poured in counts, the canteen of water lying in it doesn't" ) {
        here.add_item( tub, item( itype_water, calendar::turn, 400 ) );
        here.add_item( tub, canteen );
        REQUIRE( here.i_at( tub ).size() == 2 );
        item *stored = iexamine::keg_liquid( tub );
        REQUIRE( stored != nullptr );
        CHECK( stored->typeId() == itype_water );
        CHECK( stored->charges == 400 );

        item more( itype_water, calendar::turn, 8 );
        CHECK( iexamine::pour_into_keg( tub, more, true ) );
        CHECK( more.charges == 0 );
        stored = iexamine::keg_liquid( tub );
        REQUIRE( stored != nullptr );
        CHECK( stored->charges == 408 );
        CHECK( here.i_at( tub ).size() == 2 );

        item other( itype_water_clean, calendar::turn, 8 );
        CHECK_FALSE( iexamine::pour_into_keg( tub, other, true ) );
        CHECK( other.charges == 8 );
    }
    SECTION( "a tub holding only a canteen is empty and takes liquid" ) {
        here.add_item( tub, canteen );
        CHECK( iexamine::keg_liquid( tub ) == nullptr );
        item water( itype_water, calendar::turn, 8 );
        CHECK( iexamine::pour_into_keg( tub, water, true ) );
        CHECK( water.charges == 0 );
        const item *stored = iexamine::keg_liquid( tub );
        REQUIRE( stored != nullptr );
        CHECK( stored->typeId() == itype_water );
        CHECK( stored->charges == 8 );
        CHECK( here.i_at( tub ).size() == 2 );
    }
}

// TERR-B07: the trunk's 2-3 logs each started a plank cut that replaced the one before, so one
// log was cut, and always by the avatar, wherever they were.
TEST_CASE( "terr-b07_trunk_planks_are_cut_by_the_builder_from_every_log",
           "[audit][construction]" )
{
    clear_avatar();
    clear_map();
    clear_npcs();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
        clear_npcs();
        clear_map();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms site = u.pos_bub() + tripoint::east;
    const construction &con = construction_constr_chop_trunk.obj();
    REQUIRE( con.post_specials.size() == 1 );
    const int moves_per_log = to_moves<int>( 20_minutes );

    SECTION( "an NPC builder cuts them, not the avatar" ) {
        npc &guy = spawn_npc( ( u.pos_bub() + tripoint( 3, 0, 0 ) ).xy(), "test_talker" );
        guy.set_skill_level( skill_fabrication, 10 );
        const tripoint_bub_ms guy_pos = guy.pos_bub();
        here.i_clear( guy_pos );
        here.i_clear( u.pos_bub() );
        rng_set_engine_seed( 2828 );

        con.post_specials.front()( site, guy );
        CHECK( !u.activity );
        REQUIRE( guy.activity.id() == ACT_CHOP_PLANKS );
        CHECK( ( guy.activity.moves_total == 2 * moves_per_log ||
                 guy.activity.moves_total == 3 * moves_per_log ) );

        // the last move of the cut
        guy.activity.moves_left = 1;
        guy.set_moves( 100 );
        guy.activity.do_turn( guy );
        CHECK( !guy.activity );
        // with 10 fabrication a log gives 9 or 10 planks: more than 10 means more than one log
        CHECK( count_items( guy_pos, itype_2x4 ) > 10 );
        CHECK( count_items( u.pos_bub(), itype_2x4 ) == 0 );
    }
    SECTION( "the avatar builder cuts all the logs in one activity" ) {
        con.post_specials.front()( site, u );
        REQUIRE( u.activity.id() == ACT_CHOP_PLANKS );
        CHECK( ( u.activity.moves_total == 2 * moves_per_log ||
                 u.activity.moves_total == 3 * moves_per_log ) );
        CHECK( u.backlog.empty() );
        u.activity.set_to_null();
    }
}

// TERR-B08: an eocs trap was always removed after its EOCs ran, so UNCONSUMED did nothing and a
// trap the EOCs set at trap_location was removed at once.
TEST_CASE( "terr-b08_eocs_traps_honour_unconsumed_and_keep_a_trap_the_eoc_set",
           "[audit][trap]" )
{
    clear_avatar();
    clear_map();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint::east;
    const on_out_of_scope cleanup( [&here, spot]() {
        here.remove_trap( spot );
        get_globals().clear_global_values();
    } );

    SECTION( "an UNCONSUMED trap stays and triggers again" ) {
        here.trap_set( spot, test_m28a_unconsumed_eoc_trap.id() );
        for( int i = 0; i < 2; ++i ) {
            here.tr_at( spot ).trigger( spot, u );
        }
        CHECK( here.tr_at( spot ).id == test_m28a_unconsumed_eoc_trap );
        CHECK( get_globals().get_global_value( "m28a_trap_hits" ) == 2 );
    }
    SECTION( "a trap the EOCs set where the trap was stays" ) {
        here.trap_set( spot, test_m28a_rearming_eoc_trap.id() );
        here.tr_at( spot ).trigger( spot, u );
        CHECK( get_globals().get_global_value( "m28a_trap_rearms" ) == 1 );
        CHECK( here.tr_at( spot ).id == test_m28a_unconsumed_eoc_trap );
    }
}

// TERR-B09: tripwire, goo, sinkhole and glow on a ridden mount acted on the avatar, even when an
// NPC rode it: the avatar was thrown off a mount they weren't on, moved, or irradiated.
TEST_CASE( "terr-b09_mount_trap_effects_hit_the_npc_rider_not_the_avatar", "[audit][trap]" )
{
    clear_avatar();
    clear_map();
    clear_creatures();
    clear_npcs();
    const on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_creatures();
        clear_avatar();
        clear_map();
    } );
    avatar &u = get_avatar();
    const tripoint_bub_ms u_pos = u.pos_bub();
    const tripoint_bub_ms mount_pos = u_pos + tripoint( 5, 0, 0 );
    monster &horse = spawn_test_monster( "mon_horse", mount_pos );
    npc &rider = spawn_npc( ( u_pos + tripoint( 5, 2, 0 ) ).xy(), "test_talker" );
    rider.mount_creature( horse );
    REQUIRE( rider.is_mounted() );
    REQUIRE( horse.mounted_player == &rider );
    REQUIRE( rider.pos_bub() == mount_pos );

    SECTION( "goo throws the rider off" ) {
        trapfunc::goo( mount_pos, &horse, nullptr );
        CHECK( !rider.is_mounted() );
        CHECK( u.pos_bub() == u_pos );
    }
    SECTION( "a sinkhole throws the rider off" ) {
        trapfunc::sinkhole( mount_pos, &horse, nullptr );
        CHECK( !rider.is_mounted() );
        CHECK( u.pos_bub() == u_pos );
    }
    SECTION( "a tripwire moves the rider with the mount" ) {
        trapfunc::tripwire( mount_pos, &horse, nullptr );
        CHECK( u.pos_bub() == u_pos );
        CHECK( rider.is_mounted() );
        CHECK( rider.pos_abs() == horse.pos_abs() );
        CHECK( rl_dist( horse.pos_bub(), mount_pos ) == 1 );
    }
    SECTION( "glow irradiates the rider" ) {
        rng_set_engine_seed( 2829 );
        const int u_rad = u.get_rad();
        const int rider_rad = rider.get_rad();
        bool irradiated = false;
        for( int i = 0; i < 200 && !irradiated; ++i ) {
            trapfunc::glow( mount_pos, &horse, nullptr );
            irradiated = rider.get_rad() > rider_rad;
            // the glow burns the mount too
            horse.set_hp( horse.get_hp_max() );
        }
        CHECK( irradiated );
        CHECK( u.get_rad() == u_rad );
    }
}

// TERR-B11: on a vending machine a price equal to the cards' balance was free, and on a
// networked one the bank paid all of a price the cards couldn't cover.
TEST_CASE( "terr-b11_vending_machine_bills_the_cards_then_the_bank", "[audit][examine]" )
{
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    avatar &u = get_avatar();

    SECTION( "a price equal to the card's balance empties the card" ) {
        give_cash_card( u, 900 );
        CHECK( iexamine::vending_pay( u, 900, false ) );
        CHECK( u.charges_of( itype_cash_card ) == 0 );
    }
    SECTION( "the bank pays only what the card can't" ) {
        give_cash_card( u, 900 );
        u.cash = 200;
        CHECK( iexamine::vending_pay( u, 1000, true ) );
        CHECK( u.charges_of( itype_cash_card ) == 0 );
        CHECK( u.cash == 100 );
    }
    SECTION( "the card pays all it can cover, the bank nothing" ) {
        give_cash_card( u, 900 );
        u.cash = 200;
        CHECK( iexamine::vending_pay( u, 500, true ) );
        CHECK( u.charges_of( itype_cash_card ) == 400 );
        CHECK( u.cash == 200 );
    }
    SECTION( "what neither can cover is refused and nothing is taken" ) {
        give_cash_card( u, 900 );
        u.cash = 5000;
        CHECK_FALSE( iexamine::vending_pay( u, 1000, false ) );
        u.cash = 50;
        CHECK_FALSE( iexamine::vending_pay( u, 1000, true ) );
        CHECK( u.charges_of( itype_cash_card ) == 900 );
        CHECK( u.cash == 50 );
    }
}

// TERR-B12: a construction with copy-from rebuilt its components, tools and qualities from its
// own JSON only, so a copy that didn't repeat them was free to build.
TEST_CASE( "terr-b12_construction_copy_from_keeps_the_parents_requirements",
           "[audit][construction]" )
{
    construction parent;
    parent.id = construction_audit_m28a_parent;
    load_construction_json( parent, R"({
        "group": "place_ap_box_battery_charger", "category": "OTHER",
        "required_skills": [ [ "fabrication", 1 ] ], "time": "2 m",
        "components": [ [ [ "2x4", 2 ] ] ],
        "tools": [ [ [ "hammer", -1 ] ] ],
        "qualities": [ [ { "id": "SAW_W", "level": 1 } ] ],
        "pre_special": "check_empty"
    })" );
    const auto copy_of_parent = [&parent]( const construction_str_id & id ) {
        construction child = parent;
        child.id = id;
        child.was_loaded = true;
        return child;
    };

    // a copy that gives none of them inherits all three
    construction bare = copy_of_parent( construction_audit_m28a_child_bare );
    load_construction_json( bare, R"({ "time": "3 m" })" );
    CHECK( bare.requirements == requirement_data_inline_construction_audit_m28a_child_bare );
    const requirement_data &bare_req = bare.requirements.obj();
    REQUIRE( bare_req.get_components().size() == 1 );
    CHECK( bare_req.get_components()[0][0].type == itype_2x4 );
    CHECK( bare_req.get_components()[0][0].count == 2 );
    REQUIRE( bare_req.get_tools().size() == 1 );
    CHECK( bare_req.get_tools()[0][0].type == itype_hammer );
    REQUIRE( bare_req.get_qualities().size() == 1 );
    CHECK( bare_req.get_qualities()[0][0].type == qual_SAW_W );

    // what it gives replaces the inherited list
    construction replaced = copy_of_parent( construction_audit_m28a_child_replaced );
    load_construction_json( replaced, R"({ "components": [ [ [ "rock", 3 ] ] ] })" );
    const requirement_data &replaced_req =
        requirement_data_inline_construction_audit_m28a_child_replaced.obj();
    REQUIRE( replaced_req.get_components().size() == 1 );
    CHECK( replaced_req.get_components()[0][0].type == itype_rock );
    CHECK( replaced_req.get_tools().size() == 1 );
    CHECK( replaced_req.get_qualities().size() == 1 );

    // "extend" adds a group, "delete" takes ids out
    construction changed = copy_of_parent( construction_audit_m28a_child_changed );
    load_construction_json( changed, R"({
        "extend": { "components": [ [ [ "nail", 4 ] ] ] },
        "delete": { "tools": [ "hammer" ] }
    })" );
    const requirement_data &changed_req =
        requirement_data_inline_construction_audit_m28a_child_changed.obj();
    REQUIRE( changed_req.get_components().size() == 2 );
    CHECK( changed_req.get_components()[0][0].type == itype_2x4 );
    CHECK( changed_req.get_components()[1][0].type == itype_nail );
    CHECK( changed_req.get_components()[1][0].count == 4 );
    CHECK( changed_req.get_tools().empty() );
    CHECK( changed_req.get_qualities().size() == 1 );
}

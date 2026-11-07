#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "debug.h"
#include "effect.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "monster.h"
#include "mutation.h"
#include "npc.h"
#include "npc_attack.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"

static const bionic_id bio_laser( "bio_laser" );
static const bionic_id bio_power_storage( "bio_power_storage" );

static const efftype_id effect_audit_hlth_j02_stale_lists( "audit_hlth_j02_stale_lists" );
static const efftype_id effect_cureall( "cureall" );
static const efftype_id effect_test_grapple_focus( "test_grapple_focus" );
static const efftype_id effect_test_grapple_mark( "test_grapple_mark" );

static const faction_id faction_your_followers( "your_followers" );

static const flag_id json_flag_HERBIVORE_DIET( "HERBIVORE_DIET" );

static const itype_id itype_apple( "apple" );
static const itype_id itype_egg_chicken( "egg_chicken" );
static const itype_id itype_meat_cooked( "meat_cooked" );

static const matec_id test_cmbt_b02( "test_cmbt_b02" );

static const mtype_id mon_zombie( "mon_zombie" );

static const skill_id skill_gun( "gun" );
static const skill_id skill_melee( "melee" );
static const skill_id skill_pistol( "pistol" );
static const skill_id skill_rifle( "rifle" );
static const skill_id skill_unarmed( "unarmed" );

static const string_id<npc_template> npc_template_test_talker( "test_talker" );

static const ter_str_id ter_t_water_dp( "t_water_dp" );
static const ter_str_id ter_t_water_sh( "t_water_sh" );

static const trait_id trait_RUMINANT( "RUMINANT" );
static const trait_id trait_VEGAN( "VEGAN" );

namespace
{
// Counts character_dies by the character it names
struct death_counter : event_subscriber {
    using event_subscriber::notify;
    std::map<character_id, int> dies;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::character_dies ) {
            dies[e.get<cata_variant_type::character_id>( "character" )]++;
        }
    }
};
} // namespace

// EVT-B01: the follower death switch stopped working (the takeover was refused while the
// death was being finalized), and before that the body left behind sent character_dies a
// second time when it died as an NPC
TEST_CASE( "evt-b01_the_death_switch_takes_a_follower_and_reports_one_death",
           "[audit][events][death]" )
{
    clear_avatar();
    clear_map();
    override_option deathcam( "DEATHCAM", "never" );
    avatar &u = get_avatar();
    npc &follower = spawn_npc( u.pos_bub().xy() + point_rel_ms::east, "test_talker" );
    follower.set_fac( faction_your_followers );
    REQUIRE( follower.is_player_ally() );
    const character_id old_body = u.getID();
    const character_id new_body = follower.getID();
    global_variables &globals = get_globals();
    globals.set_global_value( "test_evt_b01_armed", 1 );
    globals.set_global_value( "test_evt_b01_npc", std::to_string( new_body.get_value() ) );
    death_counter deaths;
    get_event_bus().subscribe( &deaths );
    on_out_of_scope cleanup( [&deaths]() {
        get_event_bus().unsubscribe( &deaths );
        get_globals().clear_global_values();
        clear_npcs();
        clear_avatar();
    } );

    u.set_part_hp_cur( body_part_head.id(), 0 );
    REQUIRE( u.is_dead_state() );
    // the death switch moves the player into the follower: not dead any more
    CHECK_FALSE( u.finalize_death( nullptr ) );
    REQUIRE( u.getID() == new_body );
    CHECK_FALSE( u.is_dead_state() );
    CHECK( deaths.dies[old_body] == 1 );

    // the body left behind dies as an NPC, without being reported again
    g->cleanup_dead();
    CHECK( g->find_npc( old_body ) == nullptr );
    CHECK( deaths.dies[old_body] == 1 );
    CHECK( deaths.dies[new_body] == 0 );
}

// NPC-B01: an NPC drew its CBM gun, then never fired it: can_wield() refuses the NO_UNWIELD
// pseudo-gun it holds, so its attacks weren't rated
TEST_CASE( "npc-b01_an_npc_fires_its_cbm_gun", "[audit][npc_attack][bionics]" )
{
    clear_avatar();
    clear_map_and_put_player_underground();
    clear_vehicles();
    map &here = get_map();
    const tripoint_bub_ms start( 50, 50, 0 );
    const character_id guy_id = here.place_npc( start.xy(), npc_template_test_talker );
    npc &guy = *g->find_npc( guy_id );
    clear_character( guy );
    guy.setpos( here, start );
    g->load_npcs();
    on_out_of_scope cleanup( []() {
        clear_npcs();
        clear_creatures();
    } );
    guy.set_fac( faction_your_followers );
    guy.rules.set_flag( ally_rule::use_guns );
    guy.rules.clear_flag( ally_rule::use_silent );
    // it draws the CBM gun only if it rates it above its empty hands
    guy.set_skill_level( skill_gun, 5 );
    guy.set_skill_level( skill_pistol, 5 );
    guy.set_skill_level( skill_rifle, 5 );
    guy.remove_weapon();
    guy.add_bionic( bio_power_storage );
    guy.add_bionic( bio_laser );
    guy.set_power_level( guy.get_max_power_level() );

    guy.check_or_use_weapon_cbm( bio_laser );
    REQUIRE( guy.is_using_bionic_weapon() );
    const item_location gun = guy.get_wielded_item();
    REQUIRE( gun );
    REQUIRE( gun->is_gun() );
    REQUIRE_FALSE( guy.can_wield( *gun ).success() );
    REQUIRE( gun->ammo_sufficient( &guy ) );

    monster *zombie = g->place_critter_at( mon_zombie,
                                           start + tripoint_rel_ms( 5, 0, 0 ) );
    REQUIRE( zombie != nullptr );
    guy.evaluate_best_attack( zombie );
    const std::shared_ptr<npc_attack> &attack = guy.get_current_attack();
    REQUIRE( attack );
    CHECK( dynamic_cast<const npc_attack_gun *>( attack.get() ) != nullptr );
}

// CMBT-B02: a technique effect that needs the hit to do damage ("on_damage") was applied when
// the damage before armor wasn't 0, so it landed on a target its armor fully protected
TEST_CASE( "cmbt-b02_technique_effects_that_need_damage_wait_for_armor", "[audit][combat]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    you.set_str_base( 12 );
    you.set_dex_base( 20 );
    you.set_skill_level( skill_melee, 10 );
    you.set_skill_level( skill_unarmed, 10 );
    you.reset_stats();
    on_out_of_scope cleanup( []() {
        clear_creatures();
    } );
    const auto attack_until_hit = [&you]( monster & target ) {
        // the on_damage: false effect marks the first blow that lands
        for( int tries = 0; tries < 100 && !target.has_effect( effect_test_grapple_focus ); ++tries ) {
            you.set_moves( 100 );
            you.set_stamina( you.get_stamina_max() );
            you.melee_attack_abstract( target, false, test_cmbt_b02 );
        }
        REQUIRE( target.has_effect( effect_test_grapple_focus ) );
    };

    SECTION( "armor that stops the blow stops the effect" ) {
        monster &post = spawn_test_monster( "mon_test_cmbt_b02_armored_post",
                                            you.pos_bub() + tripoint_rel_ms::east );
        const int hp = post.get_hp();
        attack_until_hit( post );
        CHECK( post.get_hp() == hp );
        CHECK_FALSE( post.has_effect( effect_test_grapple_mark ) );
    }
    SECTION( "a blow that gets through applies it" ) {
        monster &post = spawn_test_monster( "mon_test_cmbt_post",
                                            you.pos_bub() + tripoint_rel_ms::east );
        const int hp = post.get_hp();
        attack_until_hit( post );
        CHECK( post.get_hp() < hp );
        CHECK( post.has_effect( effect_test_grapple_mark ) );
    }
}

// LOOP-B11: an avatar knocked back flew over deep water as over dry land, landing anywhere and
// staying dry; the water check looked at the starting tile
TEST_CASE( "loop-b11_the_avatar_knocked_into_deep_water_lands_in_it", "[audit][game_loop]" )
{
    clear_map( -1, 1 );
    clear_avatar();
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms start( 60, 60, 0 );
    const tripoint_rel_ms east( 1, 0, 0 );
    u.setpos( here, start );
    const tripoint_abs_sm bubble = here.get_abs_sub();

    SECTION( "deep water ends the flight, and the avatar is in it" ) {
        here.ter_set( start + east * 2, ter_t_water_dp );
        here.ter_set( start + east * 3, ter_t_water_dp );
        const int moves = u.get_moves();
        g->knockback( start - east, start, 3, 0, 1 );
        CHECK( u.pos_bub() == start + east * 2 );
        CHECK( u.is_swimming );
        CHECK( u.get_part_wetness( body_part_torso ) > 0 );
        // an involuntary landing: no move cost, and the map isn't moved under the caller
        CHECK( u.get_moves() == moves );
        CHECK( here.get_abs_sub() == bubble );
    }
    SECTION( "shallow water doesn't stop it" ) {
        here.ter_set( start + east * 2, ter_t_water_sh );
        g->knockback( start - east, start, 3, 0, 1 );
        CHECK( u.pos_bub() == start + east * 3 );
        CHECK_FALSE( u.is_swimming );
    }
    clear_avatar();
}

// HLTH-J03: VEGAN and RUMINANT were hardcoded in C++; they are data now, and behave as before
TEST_CASE( "hlth-j03_vegan_and_ruminant_diets_come_from_their_traits", "[audit][health]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );

    SECTION( "vegan, through its eat_condition" ) {
        you.toggle_trait( trait_VEGAN );
        const ret_val<edible_rating> egg = you.can_eat( item( itype_egg_chicken ) );
        CHECK_FALSE( egg.success() );
        CHECK( egg.value() == INEDIBLE_MUTATION );
        CHECK( egg.str() == "You're still not going to eat animal products." );
        CHECK( you.can_eat( item( itype_apple ) ).success() );
    }
    SECTION( "ruminant, through the HERBIVORE_DIET flag" ) {
        you.toggle_trait( trait_RUMINANT );
        CHECK( you.has_flag( json_flag_HERBIVORE_DIET ) );
        const ret_val<edible_rating> meat = you.can_eat( item( itype_meat_cooked ) );
        CHECK_FALSE( meat.success() );
        CHECK( meat.str() == "The thought of eating that makes you feel sick." );
    }
}

// HLTH-J02: an effect's resist, remove and block lists weren't checked at load, and shipped
// data had ids in them that point at nothing (vanilla's cureall removed a "stung" effect that
// doesn't exist)
TEST_CASE( "hlth-j02_unknown_ids_in_effect_lists_are_reported", "[audit][effect]" )
{
    const JsonObject stale = json_loader::from_string( R"({
        "id": "audit_hlth_j02_stale_lists",
        "resist_traits": [ "AUDIT_NO_SUCH_TRAIT" ], "resist_effects": [ "audit_no_such_resist" ],
        "removes_effects": [ "audit_no_such_removal" ], "blocks_effects": [ "audit_no_such_block" ]
    })" );
    load_effect_type( stale, "audit_hlth_j02" );
    const std::string errors = capture_debugmsg_during( []() {
        effect_audit_hlth_j02_stale_lists->verify();
    } );
    CHECK_THAT( errors,
                Catch::Matchers::Contains( "unknown trait AUDIT_NO_SUCH_TRAIT in resist_traits" ) );
    CHECK_THAT( errors, Catch::Matchers::Contains( "audit_no_such_resist in resist_effects" ) );
    CHECK_THAT( errors, Catch::Matchers::Contains( "audit_no_such_removal in removes_effects" ) );
    CHECK_THAT( errors, Catch::Matchers::Contains( "audit_no_such_block in blocks_effects" ) );
    // the fixed shipped effect
    CHECK( capture_debugmsg_during( []() {
        effect_cureall->verify();
    } ).empty() );
}

// MUT-J01: a trait's categories weren't checked at load; core Mycus traits and several mods used
// categories that don't exist, which mutating ignored and the genemill choked on
TEST_CASE( "mut-j01_every_trait_category_exists", "[audit][mutations]" )
{
    for( const mutation_branch &branch : mutation_branch::get_all() ) {
        CAPTURE( branch.id.str() );
        for( const mutation_category_id &cat : branch.category ) {
            CAPTURE( cat.str() );
            CHECK( mutation_category_is_valid( cat ) );
        }
    }
}

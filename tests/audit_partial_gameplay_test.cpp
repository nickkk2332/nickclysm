#include <optional>
#include <sstream>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "melee.h"
#include "messages.h"
#include "monster.h"
#include "mutation.h"
#include "options_helpers.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "projectile.h"
#include "type_id.h"

static const bionic_id test_bio_gp_mutb05_blocker( "test_bio_gp_mutb05_blocker" );
static const bionic_id test_bio_gp_mutb10_master( "test_bio_gp_mutb10_master" );
static const bionic_id test_bio_mutb10_teacher( "test_bio_mutb10_teacher" );

static const efftype_id effect_mutation_internal_damage( "mutation_internal_damage" );

static const itype_id itype_rock( "rock" );
static const itype_id itype_shotgun_s( "shotgun_s" );
static const itype_id itype_test_gp_useb08_keepsake( "test_gp_useb08_keepsake" );
static const itype_id itype_test_gp_useb08_pebble( "test_gp_useb08_pebble" );
static const itype_id itype_test_gp_useb08_token( "test_gp_useb08_token" );

static const mutation_category_id
mutation_category_AUDIT_GP_MUTB05_ALONE( "AUDIT_GP_MUTB05_ALONE" );
static const mutation_category_id mutation_category_AUDIT_GP_MUTB05_PAIR( "AUDIT_GP_MUTB05_PAIR" );
static const mutation_category_id mutation_category_AUDIT_GP_MUTB05_TRIO( "AUDIT_GP_MUTB05_TRIO" );

static const skill_id skill_melee( "melee" );
static const skill_id skill_throw( "throw" );
static const skill_id skill_unarmed( "unarmed" );

static const spell_id spell_test_spell_pew( "test_spell_pew" );

static const trait_id trait_TEST_GP_CMBTB13_ANY_HIT( "TEST_GP_CMBTB13_ANY_HIT" );
static const trait_id trait_TEST_GP_CMBTB13_MELEE_HIT( "TEST_GP_CMBTB13_MELEE_HIT" );
static const trait_id trait_TEST_GP_MUTB05_BEHIND( "TEST_GP_MUTB05_BEHIND" );
static const trait_id trait_TEST_GP_MUTB05_BEHIND_TOO( "TEST_GP_MUTB05_BEHIND_TOO" );
static const trait_id trait_TEST_GP_MUTB05_OPEN( "TEST_GP_MUTB05_OPEN" );
static const trait_id trait_THRESH_AUDIT_GP_MUTB05( "THRESH_AUDIT_GP_MUTB05" );

static double num_var( const Creature &c, const std::string &key )
{
    const diag_value *val = c.maybe_get_value( key );
    return val == nullptr ? 0.0 : val->dbl();
}

static std::string str_var( const Creature &c, const std::string &key )
{
    const diag_value *val = c.maybe_get_value( key );
    return val == nullptr || !val->is_str() ? std::string() : val->str();
}

// The avatar alone on a cleared map, at a fixed time.
static void clear_scene()
{
    calendar::turn = calendar::turn_zero + 9_hours;
    clear_vehicles();
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_avatar();
    melee::clear_stats();
    Messages::clear_messages();
}

// MUT-B05: a random mutation still picked traits behind a threshold the character lacks. Each
// such pick failed at the threshold check, after mutate_towards had already acted on a CBM that
// conflicts with the trait: it strained against it (internal damage) or destroyed it, for a
// trait the character can't get.
TEST_CASE( "mut-b05_traits_behind_a_missing_threshold_are_not_picked", "[audit][mutation]" )
{
    clear_scene();
    const override_option no_selector( "SHOW_MUTATION_SELECTOR", "false" );
    avatar &you = get_avatar();
    REQUIRE_FALSE( you.has_trait( trait_THRESH_AUDIT_GP_MUTB05 ) );

    SECTION( "the only candidate is behind the threshold: the attempt does nothing" ) {
        you.add_bionic( test_bio_gp_mutb05_blocker );
        for( int i = 0; i < 20; ++i ) {
            you.mutate_category( mutation_category_AUDIT_GP_MUTB05_ALONE, false, true );
        }
        CHECK_FALSE( you.has_trait( trait_TEST_GP_MUTB05_BEHIND ) );
        CHECK( you.has_bionic( test_bio_gp_mutb05_blocker ) );
        CHECK_FALSE( you.has_effect( effect_mutation_internal_damage ) );
    }
    SECTION( "an open candidate is always the one picked" ) {
        you.add_bionic( test_bio_gp_mutb05_blocker );
        int gained = 0;
        for( int i = 0; i < 20; ++i ) {
            you.mutate_category( mutation_category_AUDIT_GP_MUTB05_PAIR, false, true );
            if( you.has_trait( trait_TEST_GP_MUTB05_OPEN ) ) {
                ++gained;
                you.unset_mutation( trait_TEST_GP_MUTB05_OPEN );
            }
        }
        CHECK( gained == 20 );
        CHECK_FALSE( you.has_trait( trait_TEST_GP_MUTB05_BEHIND ) );
        CHECK( you.has_bionic( test_bio_gp_mutb05_blocker ) );
        CHECK_FALSE( you.has_effect( effect_mutation_internal_damage ) );
    }
    SECTION( "past the threshold the trait can be picked" ) {
        you.set_mutation( trait_THRESH_AUDIT_GP_MUTB05 );
        you.mutate_category( mutation_category_AUDIT_GP_MUTB05_ALONE, false, true );
        CHECK( you.has_trait( trait_TEST_GP_MUTB05_BEHIND ) );
    }
    SECTION( "mutation keeps its pace: as many attempts come to nothing as before" ) {
        // Two of three candidates are behind the threshold. The old draw (two picks without
        // putting back) came to nothing when both picks were blocked ones: 2/3 * 1/2 = 1/3.
        int stalled = 0;
        const int attempts = 300;
        for( int i = 0; i < attempts; ++i ) {
            you.mutate_category( mutation_category_AUDIT_GP_MUTB05_TRIO, false, true );
            if( you.has_trait( trait_TEST_GP_MUTB05_OPEN ) ) {
                you.unset_mutation( trait_TEST_GP_MUTB05_OPEN );
            } else {
                ++stalled;
            }
        }
        CHECK_FALSE( you.has_trait( trait_TEST_GP_MUTB05_BEHIND ) );
        CHECK_FALSE( you.has_trait( trait_TEST_GP_MUTB05_BEHIND_TOO ) );
        // mean 100, standard deviation about 8
        CHECK( stalled > 60 );
        CHECK( stalled < 140 );
    }
    clear_scene();
}

// USE-B08: "consume": true on an effect_on_conditions use action spent nothing from an item
// with no charges (activation_consume takes qty * ammo_required(), 0 for a plain item), so
// only SINGLE_USE items were used up.
TEST_CASE( "use-b08_consume_uses_up_an_item_with_no_charges", "[audit][iuse_actor]" )
{
    clear_scene();
    avatar &you = get_avatar();
    map &here = get_map();

    SECTION( "a carried item" ) {
        item_location token = you.i_add( item( itype_test_gp_useb08_token ) );
        REQUIRE( token );
        REQUIRE( you.has_amount( itype_test_gp_useb08_token, 1 ) );
        CHECK( you.invoke_item( token.get_item(), "effect_on_conditions" ) );
        CHECK( num_var( you, "test_gp_useb08_uses" ) == 1 );
        CHECK_FALSE( you.has_amount( itype_test_gp_useb08_token, 1 ) );
    }
    SECTION( "an item used where it lies" ) {
        const tripoint_bub_ms spot = you.pos_bub() + tripoint_rel_ms::east;
        item &token = here.add_item( spot, item( itype_test_gp_useb08_token ) );
        REQUIRE( here.i_at( spot ).size() == 1 );
        CHECK( you.invoke_item( &token, "effect_on_conditions", spot ) );
        CHECK( num_var( you, "test_gp_useb08_uses" ) == 1 );
        CHECK( here.i_at( spot ).empty() );
    }
    SECTION( "one of a stack counted by charges" ) {
        item_location pebbles = you.i_add( item( itype_test_gp_useb08_pebble, calendar::turn, 3 ) );
        REQUIRE( pebbles );
        REQUIRE( pebbles->charges == 3 );
        CHECK_FALSE( you.invoke_item( pebbles.get_item(), "effect_on_conditions" ) );
        CHECK( num_var( you, "test_gp_useb08_uses" ) == 1 );
        REQUIRE( pebbles );
        CHECK( pebbles->charges == 2 );
    }
    SECTION( "an action without consume leaves it" ) {
        item_location keepsake = you.i_add( item( itype_test_gp_useb08_keepsake ) );
        REQUIRE( keepsake );
        CHECK_FALSE( you.invoke_item( keepsake.get_item(), "effect_on_conditions" ) );
        CHECK( num_var( you, "test_gp_useb08_uses" ) == 1 );
        CHECK( you.has_amount( itype_test_gp_useb08_keepsake, 1 ) );
    }
    clear_scene();
}

// MUT-B10: a CBM that raised a spell the character already knew left it at the raised level
// when it was removed.
TEST_CASE( "mut-b10_removing_a_cbm_takes_back_the_spell_experience_it_added",
           "[audit][bionics][cbm][magic]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.magic->learn_spell( spell_test_spell_pew, you, true );
    REQUIRE( you.magic->knows_spell( spell_test_spell_pew ) );
    spell &pew = you.magic->get_spell( spell_test_spell_pew );
    pew.set_level( you, 1 );
    const int known_xp = pew.xp();

    SECTION( "the raise goes, experience gained since stays" ) {
        const bionic_uid uid = you.add_bionic( test_bio_mutb10_teacher );
        REQUIRE( you.magic->get_spell( spell_test_spell_pew ).get_level() == 2 );
        you.magic->get_spell( spell_test_spell_pew ).gain_exp( you, 10 );
        you.remove_bionic( **you.find_bionic_by_uid( uid ) );
        REQUIRE( you.magic->knows_spell( spell_test_spell_pew ) );
        CHECK( you.magic->get_spell( spell_test_spell_pew ).xp() == known_xp + 10 );
        CHECK( you.magic->get_spell( spell_test_spell_pew ).get_level() == 1 );
    }
    SECTION( "another installed CBM teaching it higher keeps it there" ) {
        const bionic_uid teacher = you.add_bionic( test_bio_mutb10_teacher );
        const bionic_uid master = you.add_bionic( test_bio_gp_mutb10_master );
        REQUIRE( you.magic->get_spell( spell_test_spell_pew ).get_level() == 4 );
        you.remove_bionic( **you.find_bionic_by_uid( teacher ) );
        CHECK( you.magic->get_spell( spell_test_spell_pew ).get_level() == 4 );
        you.remove_bionic( **you.find_bionic_by_uid( master ) );
        CHECK( you.magic->get_spell( spell_test_spell_pew ).xp() == known_xp );
        CHECK( you.magic->get_spell( spell_test_spell_pew ).get_level() == 1 );
    }
    SECTION( "the record is saved with the CBM" ) {
        const bionic_uid uid = you.add_bionic( test_bio_mutb10_teacher );
        const bionic &installed = **you.find_bionic_by_uid( uid );
        REQUIRE( installed.raised_spell_xp.count( spell_test_spell_pew ) == 1 );
        std::ostringstream saved;
        JsonOut jsout( saved );
        installed.serialize( jsout );
        bionic loaded;
        loaded.deserialize( json_loader::from_string( saved.str() ) );
        CHECK( loaded.raised_spell_xp == installed.raised_spell_xp );
    }
    SECTION( "a CBM from before the record leaves the raised level" ) {
        const bionic_uid uid = you.add_bionic( test_bio_mutb10_teacher );
        bionic &installed = **you.find_bionic_by_uid( uid );
        installed.raised_spell_xp.clear();
        you.remove_bionic( installed );
        CHECK( you.magic->get_spell( spell_test_spell_pew ).get_level() == 2 );
    }
    clear_avatar();
}

namespace
{
// A target that never dodges and outlasts every test hit, next to the avatar.
monster &hit_target( const avatar &you )
{
    return spawn_test_monster( "mon_test_cmbt_post", you.pos_bub() + tripoint_rel_ms::east );
}

void steady_hands( avatar &you )
{
    you.set_str_base( 20 );
    you.set_dex_base( 20 );
    you.set_per_base( 20 );
    you.reset_stats();
    you.set_skill_level( skill_melee, 10 );
    you.set_skill_level( skill_unarmed, 10 );
    you.set_skill_level( skill_throw, 10 );
}

void ready( avatar &you )
{
    you.set_moves( 1000 );
    you.set_stamina( you.get_stamina_max() );
    you.recoil = 0;
}
} // namespace

// CMBT-B13: hit_you_effect went off on melee hits only; guns and thrown weapons never cast it.
// It now goes off for every kind of hit, telling its EOCs which ("hit_type"), unless it is
// marked melee-only.
TEST_CASE( "cmbt-b13_ranged_and_thrown_hits_cast_hit_you_effect", "[audit][ranged][magic]" )
{
    clear_scene();
    avatar &you = get_avatar();
    steady_hands( you );
    you.set_mutation( trait_TEST_GP_CMBTB13_ANY_HIT );
    you.set_mutation( trait_TEST_GP_CMBTB13_MELEE_HIT );
    you.recalculate_enchantment_cache();
    monster &target = hit_target( you );

    SECTION( "a thrown hit" ) {
        int hits = 0;
        for( int tries = 0; tries < 100 && hits == 0; ++tries ) {
            ready( you );
            const dealt_projectile_attack thrown = you.throw_item( target.pos_bub(), item( itype_rock ) );
            const auto hit = thrown.targets_hit.find( &target );
            if( hit != thrown.targets_hit.end() && hit->second.first > 0 ) {
                ++hits;
            }
        }
        REQUIRE( hits == 1 );
        CHECK( num_var( you, "test_gp_cmbtb13_hits" ) == 1 );
        CHECK( str_var( you, "test_gp_cmbtb13_type" ) == "thrown" );
        CHECK( num_var( you, "test_gp_cmbtb13_melee_hits" ) == 0 );
    }
    SECTION( "a gunshot: once a shot, however many pellets hit" ) {
        for( int tries = 0; tries < 100 && num_var( you, "test_gp_cmbtb13_hits" ) == 0; ++tries ) {
            arm_shooter( you, itype_shotgun_s );
            ready( you );
            you.fire_gun( get_map(), target.pos_bub(), 1, *you.get_wielded_item() );
        }
        CHECK( num_var( you, "test_gp_cmbtb13_hits" ) == 1 );
        CHECK( str_var( you, "test_gp_cmbtb13_type" ) == "ranged" );
        CHECK( num_var( you, "test_gp_cmbtb13_melee_hits" ) == 0 );
    }
    SECTION( "a melee hit" ) {
        for( int tries = 0; tries < 100 && melee::get_stats().hit_count == 0; ++tries ) {
            ready( you );
            you.melee_attack( target, false );
        }
        REQUIRE( melee::get_stats().hit_count == 1 );
        CHECK( num_var( you, "test_gp_cmbtb13_hits" ) == 1 );
        CHECK( str_var( you, "test_gp_cmbtb13_type" ) == "melee" );
        CHECK( num_var( you, "test_gp_cmbtb13_melee_hits" ) == 1 );
    }
    clear_scene();
}

// CMBT-B13: "hit_types" is checked at load.
TEST_CASE( "cmbt-b13_hit_types_must_name_a_kind_of_hit", "[audit][magic]" )
{
    fake_spell sp;
    sp.load( json_loader::from_string(
                 R"({ "id": "test_gp_cmbtb13_record", "hit_types": [ "ranged", "thrown" ] })" ) );
    CHECK_FALSE( sp.hit_types[hit_type::melee] );
    CHECK( sp.hit_types[hit_type::ranged] );
    CHECK( sp.hit_types[hit_type::thrown] );

    fake_spell plain;
    plain.load( json_loader::from_string( R"({ "id": "test_gp_cmbtb13_record" })" ) );
    CHECK( plain.hit_types == fake_spell::hit_types_default );

    fake_spell none;
    CHECK_THROWS_AS( none.load( json_loader::from_string(
                                    R"({ "id": "test_gp_cmbtb13_record", "hit_types": [ ] })" ) ), JsonError );
    fake_spell wrong;
    CHECK_THROWS_AS( wrong.load( json_loader::from_string(
                                     R"({ "id": "test_gp_cmbtb13_record", "hit_types": [ "kick" ] })" ) ), JsonError );
}

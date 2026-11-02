#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "martialarts.h"
#include "monster.h"
#include "npc.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

// Martial-arts side of the grappling port from Cataclysm-TLG (PRs #281 and #428 by worm-girl):
// fallback and basic techniques, tech effects on the attacker, grapple-gated techniques and
// the per-style grapple_weapons list.  A "hold" is set up directly with the grab effects: GRAB
// (`grabbed`) on the held creature and GRAB_FILTER (`grabbing`) on the one holding it.

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_test_basic_fallback( "test_basic_fallback" );
static const efftype_id effect_test_grapple_focus( "test_grapple_focus" );
static const efftype_id effect_test_grapple_mark( "test_grapple_mark" );

static const itype_id itype_chain( "chain" );
static const itype_id itype_club_wooden( "club_wooden" );
static const itype_id itype_knife_combat( "knife_combat" );

static const json_character_flag json_flag_MA_BASIC_FALLBACK( "MA_BASIC_FALLBACK" );

static const matec_id tech_base_headbutt( "tech_base_headbutt" );
static const matec_id tech_base_kick( "tech_base_kick" );
static const matec_id tech_base_punch( "tech_base_punch" );
static const matec_id test_tech_grapple_basic( "test_tech_grapple_basic" );
static const matec_id test_tech_grapple_knife( "test_tech_grapple_knife" );
static const matec_id test_tech_grapple_strike( "test_tech_grapple_strike" );
static const matec_id test_tech_self_effects( "test_tech_self_effects" );

static const matype_id style_fencing( "style_fencing" );
static const matype_id style_judo( "style_judo" );
static const matype_id style_none( "style_none" );
static const matype_id test_style_grapple( "test_style_grapple" );

static const skill_id skill_unarmed( "unarmed" );

static const weapon_category_id weapon_category_KNIVES( "KNIVES" );

static constexpr tripoint_bub_ms dude_pos( HALF_MAPSIZE_X, HALF_MAPSIZE_Y, 0 );

static void use_style( Character &dude, const matype_id &style )
{
    dude.martial_arts_data->add_martialart( style );
    dude.martial_arts_data->set_style( style, false );
}

// dude holds target: GRAB on the target, GRAB_FILTER on dude
static void hold( Character &dude, Creature &target )
{
    target.add_effect( effect_grabbed, 1_days );
    dude.add_effect( effect_grabbing, 1_days );
}

static std::set<matec_id> ids_of( const std::vector<technique_pools::entry> &pool )
{
    std::set<matec_id> ret;
    for( const technique_pools::entry &e : pool ) {
        ret.insert( std::get<0>( e ) );
    }
    return ret;
}

static bool usable( const Character &dude, const matec_id &tec, const Creature &target,
                    bool crit = false )
{
    return dude.evaluate_technique( tec, target, dude.used_weapon(), crit ).has_value();
}

static technique_pools pools_against( const Character &dude, const Creature &target )
{
    return dude.get_technique_pools( target, dude.used_weapon(), false, false, false );
}

TEST_CASE( "grapple_ma_fallbacks_only_with_MA_BASIC_FALLBACK", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    use_style( dude, test_style_grapple );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );
    const std::set<matec_id> strike = { test_tech_grapple_strike };
    const std::set<matec_id> punch_and_kick = { tech_base_punch, tech_base_kick };

    GIVEN( "the style's static buff has not been applied: no MA_BASIC_FALLBACK" ) {
        REQUIRE_FALSE( dude.has_flag( json_flag_MA_BASIC_FALLBACK ) );
        const technique_pools pools = pools_against( dude, target );
        THEN( "punch and kick compete with the style's technique, as in vanilla" ) {
            CHECK( pools.fallbacks.empty() );
            const std::set<matec_id> all = {
                test_tech_grapple_strike, tech_base_punch, tech_base_kick
            };
            CHECK( ids_of( pools.possible ) == all );
            CHECK( ids_of( pools.winning_pool() ) == ids_of( pools.possible ) );
        }
        WHEN( "the style's technique is not possible" ) {
            target.add_effect( effect_downed, 1_days );
            THEN( "punch and kick are still ordinary techniques" ) {
                const technique_pools downed = pools_against( dude, target );
                CHECK( downed.fallbacks.empty() );
                CHECK( ids_of( downed.possible ) == punch_and_kick );
            }
        }
    }

    GIVEN( "the style's static buff grants MA_BASIC_FALLBACK" ) {
        dude.martial_arts_data->ma_static_effects( dude );
        REQUIRE( dude.has_flag( json_flag_MA_BASIC_FALLBACK ) );
        THEN( "punch and kick are fallbacks, and the style's technique wins" ) {
            const technique_pools pools = pools_against( dude, target );
            CHECK( ids_of( pools.possible ) == strike );
            CHECK( ids_of( pools.fallbacks ) == punch_and_kick );
            CHECK( ids_of( pools.winning_pool() ) == strike );
            // The only entry in the winning pool, so the pick is certain
            CHECK( std::get<0>( dude.pick_technique( target, dude.used_weapon(), false, false,
                                false ) ) == test_tech_grapple_strike );
        }
        WHEN( "nothing else is possible" ) {
            target.add_effect( effect_downed, 1_days );
            THEN( "the fallbacks are used" ) {
                const technique_pools pools = pools_against( dude, target );
                CHECK( pools.possible.empty() );
                CHECK( ids_of( pools.winning_pool() ) == punch_and_kick );
                const matec_id picked = std::get<0>( dude.pick_technique( target,
                                                     dude.used_weapon(), false, false, false ) );
                CHECK( punch_and_kick.count( picked ) == 1 );
            }
        }
    }
}

TEST_CASE( "grapple_ma_fallbacks_stay_usable_unarmed_in_a_weapon_style",
           "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    use_style( dude, style_fencing );
    REQUIRE( dude.martial_arts_data->selected_strictly_melee() );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );

    CHECK_FALSE( usable( dude, tech_base_punch, target ) );
    // The flag can come from an effect (or a mutation) as well as from a style's buff
    dude.add_effect( effect_test_basic_fallback, 1_days );
    REQUIRE( dude.has_flag( json_flag_MA_BASIC_FALLBACK ) );
    CHECK( usable( dude, tech_base_punch, target ) );
    // A wielded weapon the style can't use is never overridden
    item club( itype_club_wooden );
    REQUIRE( dude.wield( club ) );
    CHECK_FALSE( usable( dude, tech_base_punch, target ) );
}

TEST_CASE( "grapple_ma_basic_techniques_join_the_winning_pool", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    use_style( dude, test_style_grapple );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );
    // The basic technique needs a stunned target
    target.add_effect( effect_stunned, 1_days );

    SECTION( "without MA_BASIC_FALLBACK a basic technique is an ordinary one" ) {
        const technique_pools pools = pools_against( dude, target );
        CHECK( ids_of( pools.basics ) == std::set<matec_id> { test_tech_grapple_basic } );
        CHECK( ids_of( pools.winning_pool() ) == std::set<matec_id> {
            test_tech_grapple_strike, test_tech_grapple_basic, tech_base_punch, tech_base_kick
        } );
    }
    SECTION( "with MA_BASIC_FALLBACK" ) {
        dude.martial_arts_data->ma_static_effects( dude );
        REQUIRE( dude.has_flag( json_flag_MA_BASIC_FALLBACK ) );
        WHEN( "an ordinary technique is possible" ) {
            THEN( "the basic joins it and the fallbacks stay out" ) {
                const technique_pools pools = pools_against( dude, target );
                CHECK( ids_of( pools.winning_pool() ) ==
                       std::set<matec_id> { test_tech_grapple_strike, test_tech_grapple_basic } );
            }
        }
        WHEN( "only the basic and the fallbacks are possible" ) {
            target.add_effect( effect_downed, 1_days );
            THEN( "the basic does not keep the fallbacks out, and joins them" ) {
                const technique_pools pools = pools_against( dude, target );
                CHECK( pools.possible.empty() );
                const std::set<matec_id> expected = {
                    test_tech_grapple_basic, tech_base_punch, tech_base_kick
                };
                CHECK( ids_of( pools.winning_pool() ) == expected );
            }
        }
    }
}

TEST_CASE( "grapple_ma_base_techniques_unchanged_without_the_flag", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    use_style( dude, style_none );
    dude.set_skill_level( skill_unarmed, 3 );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );
    REQUIRE_FALSE( dude.has_flag( json_flag_MA_BASIC_FALLBACK ) );

    SECTION( "punch and kick are ordinary techniques" ) {
        const technique_pools pools = pools_against( dude, target );
        CHECK( pools.fallbacks.empty() );
        const std::set<matec_id> punch_and_kick = { tech_base_punch, tech_base_kick };
        CHECK( ids_of( pools.possible ) == punch_and_kick );
    }
    SECTION( "headbutt only while an arm or a hand is grabbed" ) {
        CHECK_FALSE( usable( dude, tech_base_headbutt, target ) );
        hold( dude, target );
        CHECK_FALSE( usable( dude, tech_base_headbutt, target ) );
        dude.remove_effect( effect_grabbing );
        dude.add_effect( effect_grabbed, 1_days, body_part_arm_r );
        CHECK( usable( dude, tech_base_headbutt, target ) );
    }
    SECTION( "kicks while holding someone" ) {
        hold( dude, target );
        CHECK( usable( dude, tech_base_kick, target ) );
    }
}

TEST_CASE( "grapple_ma_base_techniques_follow_TLG_rules_with_the_flag", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    use_style( dude, test_style_grapple );
    dude.set_skill_level( skill_unarmed, 3 );
    dude.martial_arts_data->ma_static_effects( dude );
    REQUIRE( dude.has_flag( json_flag_MA_BASIC_FALLBACK ) );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );

    SECTION( "no headbutt outside a grapple with the target" ) {
        CHECK_FALSE( usable( dude, tech_base_headbutt, target ) );
        // An arm held by someone else is not a grapple with this target
        dude.add_effect( effect_grabbed, 1_days, body_part_arm_r );
        CHECK_FALSE( usable( dude, tech_base_headbutt, target ) );
        CHECK( usable( dude, tech_base_kick, target ) );
    }
    SECTION( "holding the target: headbutt, punch, but no kick" ) {
        hold( dude, target );
        CHECK( usable( dude, tech_base_headbutt, target ) );
        CHECK( usable( dude, tech_base_punch, target ) );
        CHECK_FALSE( usable( dude, tech_base_kick, target ) );
        // Kicks are never in the pool while holding
        target.add_effect( effect_downed, 1_days );
        CHECK( ids_of( pools_against( dude, target ).winning_pool() ) ==
               std::set<matec_id> { tech_base_headbutt, tech_base_punch } );
    }
    SECTION( "held by the target: headbutt and kick" ) {
        dude.add_effect( effect_grabbed, 1_days, body_part_arm_r );
        target.add_effect( effect_grabbing, 1_days );
        CHECK( usable( dude, tech_base_headbutt, target ) );
        CHECK( usable( dude, tech_base_kick, target ) );
    }
}

TEST_CASE( "grapple_ma_tech_effects_on_self_with_intensity", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );

    const ma_technique &tech = test_tech_self_effects.obj();
    REQUIRE( tech.tech_effects.size() == 3 );
    // "target": "self", TLG's "hit_self": true, and "target": "target"
    CHECK( tech.tech_effects[0].on_self );
    CHECK( tech.tech_effects[1].on_self );
    CHECK_FALSE( tech.tech_effects[2].on_self );

    damage_instance di;
    int move_cost = 100;
    item_location weapon = dude.get_wielded_item();
    dude.perform_technique( tech, target, di, move_cost, weapon );

    CHECK( dude.get_effect_int( effect_test_grapple_focus ) == 3 );
    CHECK( dude.get_effect_int( effect_test_grapple_mark ) == 2 );
    CHECK( target.get_effect_int( effect_test_grapple_mark ) == 4 );
    CHECK_FALSE( target.has_effect( effect_test_grapple_focus ) );
}

TEST_CASE( "grapple_ma_tech_effect_target_load_errors", "[grapple][martial_arts]" )
{
    const auto load_fails = []( const std::string & json ) {
        const JsonObject jo = json_loader::from_string( json );
        bool threw = false;
        try {
            load_tech_effect_data( jo );
        } catch( const JsonError & ) {
            threw = true;
        }
        jo.allow_omitted_members();
        return threw;
    };
    CHECK( load_fails( R"({ "id": "downed", "target": "attacker" })" ) );
    CHECK( load_fails( R"({ "id": "downed", "target": "target", "hit_self": true })" ) );
    CHECK( load_fails( R"({ "id": "downed", "intensity": -1 })" ) );

    const JsonObject jo = json_loader::from_string(
                              R"({ "id": "downed", "target": "self", "hit_self": true,
                                   "intensity": 2 })" );
    const tech_effect_data ted = load_tech_effect_data( jo );
    CHECK( ted.on_self );
    CHECK( ted.intensity == 2 );
}

TEST_CASE( "grapple_ma_knife_technique_needs_a_hold_and_a_knife", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    use_style( dude, test_style_grapple );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );

    item knife( itype_knife_combat );
    item club( itype_club_wooden );
    REQUIRE( dude.wield( knife ) );
    CHECK_FALSE( usable( dude, test_tech_grapple_knife, target ) );
    hold( dude, target );
    CHECK( usable( dude, test_tech_grapple_knife, target ) );

    REQUIRE( dude.wield( club ) );
    CHECK_FALSE( usable( dude, test_tech_grapple_knife, target ) );
    const item dropped = dude.remove_weapon();
    REQUIRE( dropped.typeId() == itype_club_wooden );
    REQUIRE_FALSE( dude.is_armed() );
    CHECK_FALSE( usable( dude, test_tech_grapple_knife, target ) );
}

TEST_CASE( "grapple_ma_style_grapple_weapons", "[grapple][martial_arts]" )
{
    const item knife( itype_knife_combat );
    const item chain( itype_chain );
    const item club( itype_club_wooden );
    REQUIRE( knife.typeId()->weapon_category.count( weapon_category_KNIVES ) == 1 );
    REQUIRE( club.typeId()->weapon_category.count( weapon_category_KNIVES ) == 0 );

    SECTION( "the TEST_DATA style allows its category and its listed item" ) {
        const martialart &style = test_style_grapple.obj();
        CHECK( style.grapple_allows_weapon( knife ) );
        CHECK( style.grapple_allows_weapon( chain ) );
        CHECK_FALSE( style.grapple_allows_weapon( club ) );
        CHECK( style.grapple_allows_weapon( null_item_reference() ) );
    }
    SECTION( "vanilla styles allow no weapon" ) {
        CHECK_FALSE( style_none->grapple_allows_weapon( knife ) );
        CHECK_FALSE( style_judo->grapple_allows_weapon( knife ) );
    }
    SECTION( "\"any\" allows every weapon" ) {
        ma_grapple_weapons any;
        any.any = true;
        CHECK( any.allows( club ) );
    }
    SECTION( "the Character wrapper uses the selected style" ) {
        standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
        clear_character( dude, true );
        CHECK_FALSE( dude.style_allows_grapple_weapon( knife ) );
        use_style( dude, test_style_grapple );
        CHECK( dude.style_allows_grapple_weapon( knife ) );
        CHECK_FALSE( dude.style_allows_grapple_weapon( club ) );
    }
    SECTION( "loading from JSON" ) {
        ma_grapple_weapons loaded;
        const JsonObject jo = json_loader::from_string(
                                  R"({ "categories": [ "KNIVES" ], "items": [ "chain" ] })" );
        loaded.deserialize( jo );
        CHECK( loaded.categories == std::set<weapon_category_id> { weapon_category_KNIVES } );
        CHECK( loaded.items == std::set<itype_id> { itype_chain } );
        CHECK_FALSE( loaded.any );
    }
}

TEST_CASE( "grapple_ma_load_checks", "[grapple][martial_arts]" )
{
    SECTION( "unknown ids in grapple_weapons are load errors" ) {
        martialart style;
        style.id = matype_id( "test_style_bad_grapple_weapons" );
        style.grapple_weapons.categories = { weapon_category_id( "NOT_A_WEAPON_CATEGORY" ) };
        style.grapple_weapons.items = { itype_id( "not_an_item_type" ) };
        const std::string msg = capture_debugmsg_during( [&style]() {
            style.check();
        } );
        CHECK_THAT( msg, Catch::Matchers::Contains( "NOT_A_WEAPON_CATEGORY" ) );
        CHECK_THAT( msg, Catch::Matchers::Contains( "not_an_item_type" ) );
    }
    SECTION( "the TEST_DATA style and its techniques pass" ) {
        CHECK( capture_debugmsg_during( []() {
            test_style_grapple->check();
            test_tech_grapple_knife->check();
            test_tech_self_effects->check();
        } ).empty() );
    }
    SECTION( "fallback on a technique attacks never use is an error" ) {
        ma_technique tech;
        tech.id = matec_id( "test_tech_defensive_fallback" );
        tech.defensive = true;
        tech.fallback = true;
        CHECK_THAT( capture_debugmsg_during( [&tech]() {
            tech.check();
        } ), Catch::Matchers::Contains( "no effect" ) );
    }
}


#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "enum_conversions.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "martialarts.h"
#include "monster.h"
#include "npc.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"

// The manual technique menu (Phase 9 of the TLG grappling port; the menu idea is from
// Cataclysm-BN PR #9108). Character::check_technique gives the reason a technique can't be used,
// for both the automatic choice and the menu.

static const bionic_id bio_cqb( "bio_cqb" );

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );

static const itype_id itype_club_wooden( "club_wooden" );
static const itype_id itype_knife_combat( "knife_combat" );

static const matec_id test_tech_grapple_knife( "test_tech_grapple_knife" );
static const matec_id test_tm_ammo( "test_tm_ammo" );
static const matec_id test_tm_block_counter( "test_tm_block_counter" );
static const matec_id test_tm_buff( "test_tm_buff" );
static const matec_id test_tm_crit( "test_tm_crit" );
static const matec_id test_tm_defensive( "test_tm_defensive" );
static const matec_id test_tm_disarm( "test_tm_disarm" );
static const matec_id test_tm_dodge_counter( "test_tm_dodge_counter" );
static const matec_id test_tm_downed_only( "test_tm_downed_only" );
static const matec_id test_tm_dummy( "test_tm_dummy" );
static const matec_id test_tm_flag( "test_tm_flag" );
static const matec_id test_tm_knife( "test_tm_knife" );
static const matec_id test_tm_no_desc( "test_tm_no_desc" );
static const matec_id test_tm_reach( "test_tm_reach" );
static const matec_id test_tm_skill( "test_tm_skill" );
static const matec_id test_tm_spin( "test_tm_spin" );
static const matec_id test_tm_strike( "test_tm_strike" );
static const matec_id test_tm_tail( "test_tm_tail" );
static const matec_id test_tm_take( "test_tm_take" );
static const matec_id test_tm_unlikely( "test_tm_unlikely" );
static const matec_id test_tm_wall( "test_tm_wall" );
static const matec_id test_tm_weapon( "test_tm_weapon" );

static const matype_id test_style_grapple( "test_style_grapple" );
static const matype_id test_style_technique_menu( "test_style_technique_menu" );
static const matype_id test_style_technique_pick( "test_style_technique_pick" );

static const skill_id skill_unarmed( "unarmed" );

static constexpr tripoint_bub_ms dude_pos( HALF_MAPSIZE_X, HALF_MAPSIZE_Y, 0 );

static void use_style( Character &dude, const matype_id &style )
{
    dude.martial_arts_data->add_martialart( style );
    dude.martial_arts_data->set_style( style, false );
}

static technique_rejection why_not( const Character &dude, const matec_id &tec,
                                    const Creature &target, technique_query query = {} )
{
    // As the menu asks: nothing random
    query.roll = false;
    return dude.check_technique( tec, target, dude.used_weapon(), query );
}

TEST_CASE( "grapple_tm_rejection_reasons", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    dude.set_skill_level( skill_unarmed, 4 );
    use_style( dude, test_style_technique_menu );
    // A feral with a pipe: it has a weapon
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );
    REQUIRE( target.has_weapon() );
    const auto rejection = []( technique_rejection why ) {
        return io::enum_to_string( why );
    };

    SECTION( "the technique's own rules" ) {
        CHECK( rejection( why_not( dude, test_tm_strike, target ) ) == "none" );
        CHECK( rejection( why_not( dude, test_tm_dummy, target ) ) == "dummy" );
        CHECK( rejection( why_not( dude, test_tm_defensive, target ) ) == "defensive" );
        CHECK( rejection( why_not( dude, test_tm_downed_only, target ) ) == "condition" );
        CHECK( rejection( why_not( dude, test_tm_no_desc, target ) ) == "condition" );
        CHECK( rejection( why_not( dude, test_tm_wall, target ) ) == "needs_wall" );
        CHECK( rejection( why_not( dude, test_tm_reach, target ) ) == "reach_only" );
        CHECK( rejection( why_not( dude, test_tm_dodge_counter, target ) ) ==
               "dodge_counter_only" );
        CHECK( rejection( why_not( dude, test_tm_block_counter, target ) ) ==
               "block_counter_only" );
        CHECK( rejection( why_not( dude, test_tm_crit, target ) ) == "crit_only" );
        CHECK( rejection( why_not( dude, test_tm_ammo, target ) ) == "needs_ammo" );
        CHECK( rejection( why_not( dude, test_tm_spin, target ) ) == "no_aoe_targets" );
        CHECK( rejection( why_not( dude, test_tm_tail, target ) ) == "no_attack_vector" );
    }
    SECTION( "the kind of attack" ) {
        technique_query crit;
        crit.crit = true;
        CHECK( rejection( why_not( dude, test_tm_strike, target, crit ) ) == "not_on_crit" );
        CHECK( rejection( why_not( dude, test_tm_crit, target, crit ) ) == "none" );
        technique_query dodge;
        dodge.dodge_counter = true;
        CHECK( rejection( why_not( dude, test_tm_strike, target, dodge ) ) ==
               "not_dodge_counter" );
        CHECK( rejection( why_not( dude, test_tm_dodge_counter, target, dodge ) ) == "none" );
        technique_query block;
        block.block_counter = true;
        CHECK( rejection( why_not( dude, test_tm_strike, target, block ) ) ==
               "not_block_counter" );
        CHECK( rejection( why_not( dude, test_tm_block_counter, target, block ) ) == "none" );
        dude.set_moves( -10000 );
        CHECK( rejection( why_not( dude, test_tm_block_counter, target, block ) ) == "too_slow" );
        dude.set_moves( 100 );
        dude.reach_attacking = true;
        CHECK( rejection( why_not( dude, test_tm_strike, target ) ) == "not_in_reach" );
        CHECK( rejection( why_not( dude, test_tm_reach, target ) ) == "none" );
        dude.reach_attacking = false;
    }
    SECTION( "the target's weapon" ) {
        CHECK( rejection( why_not( dude, test_tm_disarm, target ) ) == "none" );
        CHECK( rejection( why_not( dude, test_tm_take, target ) ) == "none" );
        monster &zombie = spawn_test_monster( "mon_zombie", dude_pos + tripoint::west );
        REQUIRE_FALSE( zombie.has_weapon() );
        CHECK( rejection( why_not( dude, test_tm_disarm, zombie ) ) == "target_unarmed" );
        CHECK( rejection( why_not( dude, test_tm_take, zombie ) ) == "target_unarmed" );
        item knife( itype_knife_combat );
        dude.wield( knife );
        CHECK( rejection( why_not( dude, test_tm_take, target ) ) == "hands_full" );
    }
    SECTION( "the requirements" ) {
        CHECK( rejection( why_not( dude, test_tm_skill, target ) ) == "skill" );
        CHECK( rejection( why_not( dude, test_tm_buff, target ) ) == "buffs" );
        CHECK( rejection( why_not( dude, test_tm_flag, target ) ) == "character_flags" );
        CHECK( rejection( why_not( dude, test_tm_weapon, target ) ) == "needs_weapon" );
        item club( itype_club_wooden );
        dude.wield( club );
        CHECK( rejection( why_not( dude, test_tm_strike, target ) ) == "unarmed_only" );
        CHECK( rejection( why_not( dude, test_tm_weapon, target ) ) == "none" );
        CHECK( rejection( why_not( dude, test_tm_knife, target ) ) == "weapon_category" );
        // A style that only knows knives: the club doesn't suit it
        use_style( dude, test_style_grapple );
        target.add_effect( effect_grabbed, 1_days );
        dude.add_effect( effect_grabbing, 1_days );
        CHECK( rejection( why_not( dude, test_tech_grapple_knife, target ) ) == "wrong_weapon" );
    }
    SECTION( "a negative weighting is rolled only when the attack is chosen automatically" ) {
        CHECK( rejection( why_not( dude, test_tm_unlikely, target ) ) == "none" );
        technique_query rolled;
        rolled.roll = true;
        CHECK( rejection( dude.check_technique( test_tm_unlikely, target, dude.used_weapon(),
                                                rolled ) ) == "weighting_roll" );
    }
    SECTION( "without roll, the answer doesn't depend on the dice" ) {
        for( const matec_id &tec : dude.martial_arts_data->get_all_techniques( dude.used_weapon(),
                dude ) ) {
            CAPTURE( tec.str() );
            rng_set_engine_seed( 1 );
            const technique_rejection first = why_not( dude, tec, target );
            for( unsigned int seed = 2; seed <= 20; seed++ ) {
                rng_set_engine_seed( seed );
                CHECK( why_not( dude, tec, target ) == first );
            }
        }
    }
}

TEST_CASE( "grapple_tm_rejection_texts", "[grapple][martial_arts]" )
{
    // A condition shows the technique's condition_desc, without its bullet and colors
    CHECK( test_tm_downed_only->rejection_text( technique_rejection::condition ) ==
           "Only works on a downed target" );
    CHECK( test_tm_no_desc->rejection_text( technique_rejection::condition ) ==
           "conditions not met" );
    CHECK( test_tm_strike->rejection_text( technique_rejection::none ).empty() );
    for( int i = 1; i < static_cast<int>( technique_rejection::last ); i++ ) {
        const technique_rejection why = static_cast<technique_rejection>( i );
        CAPTURE( io::enum_to_string( why ) );
        CHECK_FALSE( test_tm_strike->rejection_text( why ).empty() );
    }
}

// The automatic choice is unchanged by the refactor into check_technique. Two checks:
// - the verdicts of evaluate_technique in a normal hit, a crit, a dodge counter and a block
//   counter (each from the same seed) match the ones recorded with the code before it;
// - evaluate_technique gives the same tuple, and leaves the random numbers in the same state,
//   as the code before it, kept below as a reference, for many seeds.

// The requirement check before the refactor (ma_requirements::is_valid_character)
static bool reference_is_valid_character( const ma_requirements &reqs, const Character &u,
        bool ignore_style_weapon_rule )
{
    if( !reqs.buff_requirements_satisfied( u ) ) {
        return false;
    }
    bool cqb = u.has_active_bionic( bio_cqb );
    const item_location weapon = u.get_wielded_item();
    bool melee_style = !ignore_style_weapon_rule && u.martial_arts_data->selected_strictly_melee();
    bool is_armed = u.is_armed();
    bool forced_unarmed = u.martial_arts_data->selected_force_unarmed();
    bool weapon_ok = reqs.melee_allowed && weapon && reqs.is_valid_weapon( *weapon );
    bool style_weapon = weapon && u.martial_arts_data->selected_has_weapon( weapon->typeId() );
    bool all_weapons = u.martial_arts_data->selected_allow_all_weapons();
    bool melee_ok = weapon_ok && ( style_weapon || all_weapons );
    bool valid_unarmed = !melee_style && reqs.unarmed_allowed && !is_armed;
    bool valid_melee = !reqs.strictly_unarmed && ( forced_unarmed || melee_ok );
    if( !valid_unarmed && !valid_melee ) {
        return false;
    }
    for( const auto &pr : reqs.min_skill ) {
        if( ( cqb ? 5 : static_cast<int>( u.get_skill_level( pr.first ) ) ) < pr.second ) {
            return false;
        }
    }
    if( !reqs.req_char_flags.empty() ) {
        bool has_flag = false;
        for( const json_character_flag &flag : reqs.req_char_flags ) {
            if( u.has_flag( flag ) ) {
                has_flag = true;
            }
        }
        if( !has_flag ) {
            return false;
        }
    }
    for( const json_character_flag &flag : reqs.req_char_flags_all ) {
        if( !u.has_flag( flag ) ) {
            return false;
        }
    }
    for( const json_character_flag &flag : reqs.forbidden_char_flags ) {
        if( u.has_flag( flag ) ) {
            return false;
        }
    }
    if( !reqs.weapon_categories_allowed.empty() ) {
        bool valid_weap_cat = false;
        for( const weapon_category_id &w_cat : reqs.weapon_categories_allowed ) {
            const item_location used = u.used_weapon();
            if( used && used->typeId()->weapon_category.count( w_cat ) > 0 ) {
                valid_weap_cat = true;
            }
        }
        if( !valid_weap_cat ) {
            return false;
        }
    }
    return true;
}

// Character::evaluate_technique before the refactor, its debug messages left out
static std::optional<technique_pools::entry> reference_evaluate( const Character &you,
        const matec_id &tec_id, const Creature &t, const item_location &weap, bool crit,
        bool dodge_counter, bool block_counter )
{
    bool is_loaded = weap && weap->is_magazine_full();
    if( tec_id->dummy || tec_id->defensive ) {
        return std::nullopt;
    }
    if( tec_id->has_condition ) {
        const_dialogue d( get_const_talker_for( you ), get_const_talker_for( t ) );
        if( !tec_id->condition( d ) ) {
            return std::nullopt;
        }
    }
    if( tec_id->wall_adjacent && !get_map().is_wall_adjacent( you.pos_bub() ) ) {
        return std::nullopt;
    }
    if( !( tec_id->reach_ok || tec_id->reach_tec ) && you.reach_attacking ) {
        return std::nullopt;
    }
    if( tec_id->reach_tec && !you.reach_attacking ) {
        return std::nullopt;
    }
    if( dodge_counter != tec_id->dodge_counter || block_counter != tec_id->block_counter ) {
        return std::nullopt;
    }
    if( tec_id->block_counter || tec_id->dodge_counter ) {
        item &used_weap = you.used_weapon() ? *you.used_weapon() : null_item_reference();
        float move_cost = you.attack_speed( used_weap );
        move_cost *= tec_id->move_cost_multiplier( you );
        move_cost += tec_id->move_cost_penalty( you );
        float move_mult = you.exertion_adjusted_move_multiplier( EXTRA_EXERCISE );
        move_cost *= ( 1.0f / move_mult );
        if( you.get_moves() + you.get_speed() - move_cost < 0 ) {
            return std::nullopt;
        }
    }
    if( !tec_id->crit_ok && ( crit != tec_id->crit_tec ) ) {
        return std::nullopt;
    }
    if( tec_id->needs_ammo && !is_loaded ) {
        return std::nullopt;
    }
    if( tec_id->disarms && !t.has_weapon() ) {
        return std::nullopt;
    }
    if( tec_id->take_weapon && ( you.has_weapon() || !t.has_weapon() ) ) {
        return std::nullopt;
    }
    if( !tec_id->aoe.empty() ) {
        // valid_aoe_technique is private: ask check_technique, which gets this far too, about
        // the area, and keep the random numbers out of it
        const cata_default_random_engine saved = rng_get_engine();
        technique_query query;
        query.crit = crit;
        query.dodge_counter = dodge_counter;
        query.block_counter = block_counter;
        query.roll = false;
        const bool no_targets = you.check_technique( tec_id, t, weap, query ) ==
                                technique_rejection::no_aoe_targets;
        rng_get_engine() = saved;
        if( no_targets ) {
            return std::nullopt;
        }
    }
    const bool as_fallback = tec_id->is_fallback_for( you );
    if( !as_fallback && tec_id->weighting < 0 && !one_in( std::abs( tec_id->weighting ) ) ) {
        return std::nullopt;
    }
    if( reference_is_valid_character( tec_id->reqs, you, false ) ||
        ( as_fallback && !you.is_armed() &&
          reference_is_valid_character( tec_id->reqs, you, true ) ) ) {
        const std::optional<std::pair<attack_vector_id, sub_bodypart_str_id>> vector =
                    you.martial_arts_data->choose_attack_vector( you, tec_id );
        if( vector ) {
            return std::make_tuple( tec_id, vector->first, vector->second );
        }
    }
    return std::nullopt;
}

// What evaluate_technique accepts in the four kinds of attack, sorted by technique id
static std::vector<std::string> verdicts( const Character &dude, const Creature &target )
{
    std::set<std::string> all;
    for( const matec_id &id : dude.martial_arts_data->get_all_techniques( dude.used_weapon(),
            dude ) ) {
        all.insert( id.str() );
    }
    std::vector<std::string> ret;
    for( const std::string &tec : all ) {
        std::string verdict = tec + ":";
        for( int kind = 0; kind < 4; kind++ ) {
            rng_set_engine_seed( 4242 );
            const bool usable = dude.evaluate_technique( matec_id( tec ), target,
                                dude.used_weapon(), kind == 1, kind == 2, kind == 3 ).has_value();
            verdict += usable ? '1' : '0';
        }
        ret.push_back( verdict );
    }
    return ret;
}

// evaluate_technique and the reference agree, random numbers included
static void same_as_reference( const Character &dude, const Creature &target )
{
    for( const matec_id &tec : dude.martial_arts_data->get_all_techniques( dude.used_weapon(),
            dude ) ) {
        for( int kind = 0; kind < 4; kind++ ) {
            for( unsigned int seed = 1; seed <= 12; seed++ ) {
                CAPTURE( tec.str(), kind, seed );
                rng_set_engine_seed( seed );
                const bool crit = kind == 1;
                const bool dodge = kind == 2;
                const bool block = kind == 3;
                const std::optional<technique_pools::entry> expected = reference_evaluate(
                            dude, tec, target, dude.used_weapon(), crit, dodge, block );
                const cata_default_random_engine expected_rng = rng_get_engine();
                rng_set_engine_seed( seed );
                const std::optional<technique_pools::entry> got = dude.evaluate_technique( tec,
                        target, dude.used_weapon(), crit, dodge, block );
                CHECK( got == expected );
                CHECK( rng_get_engine() == expected_rng );
            }
        }
    }
}

// Recorded with the code before the refactor
static const std::vector<std::pair<std::string, std::vector<std::string>>> expected_verdicts = {
    {
        "pick", {
            "tech_base_headbutt:0000", "tech_base_kick:1100", "tech_base_punch:1100",
            "test_tm_crit:0100", "test_tm_downed_only:0000", "test_tm_often:1100",
            "test_tm_rare:1000", "test_tm_strike:1000", "test_tm_take:1000"
        }
    },
    {
        "pick_downed", {
            "tech_base_headbutt:0000", "tech_base_kick:1100", "tech_base_punch:1100",
            "test_tm_crit:0100", "test_tm_downed_only:1000", "test_tm_often:1100",
            "test_tm_rare:1000", "test_tm_strike:1000", "test_tm_take:1000"
        }
    },
    {
        "menu", {
            "tech_base_headbutt:0000", "tech_base_kick:1100", "tech_base_punch:1100",
            "test_tm_ammo:0000", "test_tm_block_counter:0001", "test_tm_buff:0000",
            "test_tm_crit:0100", "test_tm_defensive:0000", "test_tm_disarm:1000",
            "test_tm_dodge_counter:0010", "test_tm_downed_only:0000", "test_tm_dummy:0000",
            "test_tm_flag:0000", "test_tm_knife:0000", "test_tm_no_desc:0000",
            "test_tm_often:1100", "test_tm_rare:1000", "test_tm_reach:0000",
            "test_tm_skill:0000", "test_tm_spin:0000", "test_tm_strike:1000",
            "test_tm_tail:0000", "test_tm_take:1000", "test_tm_unlikely:0000",
            "test_tm_wall:0000", "test_tm_weapon:0000"
        }
    },
    {
        "menu_knife", {
            "tech_base_headbutt:0000", "tech_base_kick:0000", "tech_base_punch:0000",
            "test_tm_ammo:0000", "test_tm_block_counter:0000", "test_tm_buff:0000",
            "test_tm_crit:0000", "test_tm_defensive:0000", "test_tm_disarm:0000",
            "test_tm_dodge_counter:0000", "test_tm_downed_only:0000", "test_tm_dummy:0000",
            "test_tm_flag:0000", "test_tm_knife:1000", "test_tm_no_desc:0000",
            "test_tm_often:0000", "test_tm_rare:0000", "test_tm_reach:0000",
            "test_tm_skill:0000", "test_tm_spin:0000", "test_tm_strike:0000",
            "test_tm_tail:0000", "test_tm_take:0000", "test_tm_unlikely:0000",
            "test_tm_wall:0000", "test_tm_weapon:1000"
        }
    },
    {
        "grapple", {
            "tech_base_headbutt:0000", "tech_base_kick:1100", "tech_base_punch:1100",
            "test_tech_grapple_basic:0000", "test_tech_grapple_knife:0000",
            "test_tech_grapple_strike:1000"
        }
    },
    {
        "grapple_downed", {
            "tech_base_headbutt:0000", "tech_base_kick:1100", "tech_base_punch:1100",
            "test_tech_grapple_basic:0000", "test_tech_grapple_knife:0000",
            "test_tech_grapple_strike:0000"
        }
    },
};

TEST_CASE( "grapple_tm_automatic_choice_unchanged", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    dude.set_skill_level( skill_unarmed, 4 );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );

    std::map<std::string, std::vector<std::string>> got;
    const auto record = [&]( const std::string & name ) {
        got[name] = verdicts( dude, target );
        same_as_reference( dude, target );
    };
    use_style( dude, test_style_technique_pick );
    record( "pick" );
    target.add_effect( effect_downed, 1_days );
    record( "pick_downed" );
    target.remove_effect( effect_downed );

    use_style( dude, test_style_technique_menu );
    record( "menu" );
    item knife( itype_knife_combat );
    dude.wield( knife );
    record( "menu_knife" );
    dude.remove_weapon();
    dude.reach_attacking = true;
    same_as_reference( dude, target );
    dude.reach_attacking = false;
    dude.set_moves( -10000 );
    same_as_reference( dude, target );
    dude.set_moves( 100 );

    // MA_BASIC_FALLBACK from the style's static buff: punch and kick are fallbacks
    use_style( dude, test_style_grapple );
    dude.martial_arts_data->ma_static_effects( dude );
    record( "grapple" );
    target.add_effect( effect_downed, 1_days );
    record( "grapple_downed" );
    target.remove_effect( effect_downed );
    target.add_effect( effect_grabbed, 1_days );
    dude.add_effect( effect_grabbing, 1_days );
    item club( itype_club_wooden );
    dude.wield( club );
    same_as_reference( dude, target );

    for( const std::pair<std::string, std::vector<std::string>> &expected : expected_verdicts ) {
        CAPTURE( expected.first );
        CHECK( got[expected.first] == expected.second );
    }
}

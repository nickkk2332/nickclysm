#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "avatar.h"
#include "avatar_action.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_martial_arts.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enum_conversions.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "martialarts.h"
#include "math_parser_diag_value.h"
#include "melee.h"
#include "messages.h"
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

static const attack_vector_id vector_punch( "vector_punch" );

static const bionic_id bio_cqb( "bio_cqb" );

static const effect_on_condition_id effect_on_condition_EOC_TEST_TM_U_ATTACK(
    "EOC_TEST_TM_U_ATTACK" );

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_hit_by_player( "hit_by_player" );

static const itype_id itype_club_wooden( "club_wooden" );
static const itype_id itype_knife_combat( "knife_combat" );
static const itype_id itype_spear_wood( "spear_wood" );

static const matec_id tech_base_punch( "tech_base_punch" );
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

static const skill_id skill_melee( "melee" );
static const skill_id skill_unarmed( "unarmed" );

static const trait_id trait_BOMBADIER_BLAST( "BOMBADIER_BLAST" );

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

// ---- Manual combat mode and the technique menu ----

namespace
{
// Answers the technique menu, which can't be shown in tests, and counts how often it opened
class menu_answer
{
    public:
        using answer_fn =
            std::function<technique_choice( const std::vector<melee::technique_menu_entry> & )>;
        explicit menu_answer( answer_fn answer ) {
            melee::technique_menu_test_answer = [this, answer](
            const std::vector<melee::technique_menu_entry> &entries ) {
                calls++;
                last_entries = entries;
                return answer( entries );
            };
        }
        ~menu_answer() {
            melee::technique_menu_test_answer = nullptr;
        }
        menu_answer( const menu_answer & ) = delete;
        menu_answer &operator=( const menu_answer & ) = delete;
        int calls = 0;
        std::vector<melee::technique_menu_entry> last_entries;
};

technique_choice answer_cancel( const std::vector<melee::technique_menu_entry> & )
{
    return technique_choice( technique_choice::mode::cancel );
}

technique_choice answer_basic( const std::vector<melee::technique_menu_entry> & )
{
    return technique_choice( technique_choice::mode::basic );
}

menu_answer::answer_fn answer_technique( const matec_id &tec )
{
    return [tec]( const std::vector<melee::technique_menu_entry> & ) {
        return technique_choice( technique_choice::mode::chosen, tec );
    };
}
} // namespace

// The avatar unarmed in the test style with every rejection, in manual combat mode
static avatar &manual_fighter()
{
    clear_avatar();
    avatar &you = get_avatar();
    you.setpos( get_map(), dude_pos );
    you.set_skill_level( skill_unarmed, 4 );
    use_style( you, test_style_technique_menu );
    you.manual_combat_mode = true;
    you.set_moves( 100 );
    return you;
}

static const melee::technique_menu_entry *menu_line( const
        std::vector<melee::technique_menu_entry> &entries, const matec_id &tec )
{
    for( const melee::technique_menu_entry &entry : entries ) {
        if( entry.tec == tec ) {
            return &entry;
        }
    }
    return nullptr;
}

static bool messages_have( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg : Messages::recent_messages( 0 ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

TEST_CASE( "grapple_tm_manual_combat_mode_is_saved", "[grapple][martial_arts]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.manual_combat_mode = true;
    const on_out_of_scope manual_off( []() {
        get_avatar().manual_combat_mode = false;
    } );
    std::ostringstream os;
    JsonOut jsout( os );
    you.serialize( jsout );
    const std::string saved = os.str();
    REQUIRE( saved.find( "\"manual_combat_mode\":true" ) != std::string::npos );

    avatar loaded;
    loaded.deserialize( json_loader::from_string( saved ).get_object() );
    CHECK( loaded.manual_combat_mode );

    // A save from before the mode existed loads with it off
    std::string old_save = saved;
    const std::string member = "\"manual_combat_mode\":true,";
    old_save.erase( old_save.find( member ), member.size() );
    avatar old;
    old.manual_combat_mode = true;
    old.deserialize( json_loader::from_string( old_save ).get_object() );
    CHECK_FALSE( old.manual_combat_mode );
}

TEST_CASE( "grapple_tm_menu_lines", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    avatar &you = manual_fighter();
    const on_out_of_scope manual_off( []() {
        get_avatar().manual_combat_mode = false;
    } );
    monster &target = spawn_test_monster( "mon_feral_human_pipe", dude_pos + tripoint::east );
    const std::vector<melee::technique_menu_entry> entries = melee::technique_menu_entries( you,
            target, you.used_weapon() );

    const melee::technique_menu_entry *strike = menu_line( entries, test_tm_strike );
    REQUIRE( strike != nullptr );
    CHECK( strike->selectable );
    CHECK_FALSE( strike->crit_only );
    CHECK( strike->reason.empty() );
    CHECK( strike->group == melee::technique_menu_group::style );
    CHECK_FALSE( strike->description.empty() );

    // A crit-only technique can be attempted
    const melee::technique_menu_entry *crit = menu_line( entries, test_tm_crit );
    REQUIRE( crit != nullptr );
    CHECK( crit->selectable );
    CHECK( crit->crit_only );
    CHECK( crit->reason.find( "needs a crit" ) != std::string::npos );

    // Greyed out with the reason; a condition shows its condition_desc
    const melee::technique_menu_entry *downed = menu_line( entries, test_tm_downed_only );
    REQUIRE( downed != nullptr );
    CHECK_FALSE( downed->selectable );
    CHECK( io::enum_to_string( downed->why_not ) == "condition" );
    CHECK( downed->reason == "Only works on a downed target" );
    CHECK( downed->description.find( "Not now:" ) != std::string::npos );
    // Counters and defensive techniques are listed but can't be chosen
    for( const matec_id &tec : {
             test_tm_dodge_counter, test_tm_block_counter, test_tm_defensive, test_tm_dummy
         } ) {
        const melee::technique_menu_entry *entry = menu_line( entries, tec );
        CAPTURE( tec.str() );
        REQUIRE( entry != nullptr );
        CHECK_FALSE( entry->selectable );
        CHECK_FALSE( entry->reason.empty() );
    }
    CHECK( menu_line( entries, test_tm_dodge_counter )->reason == "dodge counter, automatic" );
    // The weighting isn't rolled: a technique that rarely comes up can be chosen
    CHECK( menu_line( entries, test_tm_unlikely )->selectable );

    // Body techniques are the unarmed basics
    const melee::technique_menu_entry *punch = menu_line( entries, tech_base_punch );
    REQUIRE( punch != nullptr );
    CHECK( punch->group == melee::technique_menu_group::unarmed );

    // By group, and in a group what can be chosen first, then what can be attempted
    for( size_t i = 1; i < entries.size(); i++ ) {
        const melee::technique_menu_entry &a = entries[i - 1];
        const melee::technique_menu_entry &b = entries[i];
        CAPTURE( a.name, b.name );
        CHECK( a.group <= b.group );
        if( a.group == b.group ) {
            const int rank_a = a.selectable ? ( a.crit_only ? 1 : 0 ) : 2;
            const int rank_b = b.selectable ? ( b.crit_only ? 1 : 0 ) : 2;
            CHECK( rank_a <= rank_b );
        }
    }

    SECTION( "mutation attacks are listed, never selectable" ) {
        you.set_mutation( trait_BOMBADIER_BLAST );
        const std::vector<melee::technique_menu_entry> with_mutation =
            melee::technique_menu_entries( you, target, you.used_weapon() );
        const melee::technique_menu_entry &last = with_mutation.back();
        CHECK( last.group == melee::technique_menu_group::mutation );
        CHECK_FALSE( last.selectable );
        CHECK( last.reason.find( "automatic" ) != std::string::npos );
    }
    SECTION( "a limited menu lists only those techniques" ) {
        const std::vector<matec_id> only = { test_tm_strike, test_tm_downed_only };
        const std::vector<melee::technique_menu_entry> limited =
            melee::technique_menu_entries( you, target, you.used_weapon(), &only );
        REQUIRE( limited.size() == 2 );
        CHECK( limited[0].tec == test_tm_strike );
        CHECK( limited[1].tec == test_tm_downed_only );
    }
}

// The harness reseeds the random numbers as each SECTION starts: the state to compare with is
// taken inside the section.
TEST_CASE( "grapple_tm_cancelling_the_menu_costs_nothing", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    avatar &you = manual_fighter();
    const on_out_of_scope manual_off( []() {
        get_avatar().manual_combat_mode = false;
    } );
    monster &zed = spawn_test_monster( "mon_zombie", dude_pos + tripoint::east );
    const int hp = zed.get_hp();
    const int moves = you.get_moves();
    melee::clear_stats();
    // Attacks, then checks that nothing happened. "Rolls nothing": the random number engine is
    // exactly where it was, so no hit, crit or anything else was rolled.
    const auto cancelled = [&]( const std::function<void()> &attack ) {
        const cata_default_random_engine before = rng_get_engine();
        attack();
        CHECK( rng_get_engine() == before );
        CHECK( you.get_moves() == moves );
        CHECK( melee::get_stats().attack_count == 0 );
        CHECK( zed.get_hp() == hp );
        CHECK_FALSE( zed.has_effect( effect_hit_by_player ) );
        CHECK( you.pos_bub() == dude_pos );
    };

    SECTION( "an attack, the menu answered by cancelling" ) {
        menu_answer answer( answer_cancel );
        cancelled( [&]() {
            CHECK_FALSE( you.melee_attack( zed, true ) );
        } );
        CHECK( answer.calls == 1 );
    }
    SECTION( "moving into the monster, the menu answered by cancelling" ) {
        menu_answer answer( answer_cancel );
        cancelled( [&]() {
            avatar_action::move( you, get_map(), tripoint_rel_ms::east );
        } );
        CHECK( answer.calls == 1 );
    }
    SECTION( "with no answer: the menu cancels itself in tests" ) {
        cancelled( [&]() {
            CHECK_FALSE( you.melee_attack( zed, true ) );
        } );
    }
}

TEST_CASE( "grapple_tm_reach_attack_asks_before_rolling", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    avatar &you = manual_fighter();
    const on_out_of_scope manual_off( []() {
        get_avatar().manual_combat_mode = false;
    } );
    item spear( itype_spear_wood );
    you.wield( spear );
    const tripoint_bub_ms there = dude_pos + tripoint( 2, 0, 0 );
    spawn_test_monster( "mon_zombie", there );
    const int moves = you.get_moves();

    SECTION( "cancelled: nothing rolled or spent" ) {
        menu_answer answer( answer_cancel );
        const cata_default_random_engine before = rng_get_engine();
        you.reach_attack( there );
        CHECK( answer.calls == 1 );
        CHECK( rng_get_engine() == before );
        CHECK( you.get_moves() == moves );
    }
    SECTION( "answered: one menu, then the attack" ) {
        menu_answer answer( answer_basic );
        you.reach_attack( there );
        CHECK( answer.calls == 1 );
        CHECK( you.get_moves() < moves );
        // The menu checked the techniques as for a reach attack
        CHECK( menu_line( answer.last_entries, test_tm_strike )->reason ==
               "not in a reach attack" );
        CHECK( menu_line( answer.last_entries, test_tm_reach )->reason != "reach attacks only" );
    }
    CHECK_FALSE( you.reach_attacking );
}

TEST_CASE( "grapple_tm_menu_only_for_the_players_own_attacks", "[grapple][martial_arts]" )
{
    clear_map();
    set_time_to_day();
    avatar &you = manual_fighter();
    const on_out_of_scope manual_off( []() {
        get_avatar().manual_combat_mode = false;
    } );
    monster &zed = spawn_test_monster( "mon_zombie", dude_pos + tripoint::east );
    map &here = get_map();
    here.invalidate_visibility_cache();
    here.build_map_cache( dude_pos.z() );
    const int moves = you.get_moves();
    menu_answer answer( answer_basic );

    SECTION( "the avatar's attack asks" ) {
        CHECK( you.melee_attack( zed, true ) );
        CHECK( answer.calls == 1 );
    }
    SECTION( "moving into a monster asks" ) {
        avatar_action::move( you, here, tripoint_rel_ms::east );
        CHECK( answer.calls == 1 );
    }
    SECTION( "not out of manual combat mode" ) {
        you.manual_combat_mode = false;
        CHECK( you.melee_attack( zed, true ) );
        CHECK( answer.calls == 0 );
    }
    SECTION( "not for autoattack" ) {
        REQUIRE( you.sees( here, zed ) );
        avatar_action::autoattack( you, here );
        CHECK( answer.calls == 0 );
        CHECK( you.get_moves() < moves );
    }
    SECTION( "not for an EOC's u_attack" ) {
        dialogue d( get_talker_for( you ), get_talker_for( zed ) );
        effect_on_condition_EOC_TEST_TM_U_ATTACK->activate( d );
        CHECK( answer.calls == 0 );
        CHECK( you.get_moves() < moves );
    }
    SECTION( "not for an NPC" ) {
        standard_npc dude( "TestCharacter", dude_pos + tripoint( 2, 0, 0 ), {}, 0, 8, 8, 8, 8 );
        dude.set_moves( 100 );
        CHECK( dude.melee_attack( zed, true ) );
        CHECK( answer.calls == 0 );
    }
    SECTION( "not while auto-moving" ) {
        you.set_destination( { dude_pos + tripoint::north, dude_pos + tripoint( 0, -2, 0 ) } );
        REQUIRE( you.is_auto_moving() );
        CHECK( you.melee_attack( zed, true ) );
        CHECK( answer.calls == 0 );
        you.clear_destination();
    }
    SECTION( "not inside a suppressor, and not for an attack without techniques" ) {
        {
            const melee::technique_menu_suppressor no_menu;
            CHECK( you.melee_attack( zed, true ) );
        }
        you.set_moves( 100 );
        CHECK( you.melee_attack( zed, false ) );
        CHECK( answer.calls == 0 );
    }
}

// Attacks again and again (full health, moves and stamina each time) until a test passes on a
// hit, or gives up; returns whether a hit came
static bool attack_until( avatar &you, monster &zed, int tries,
                          const std::function<bool( bool crit )> &on_hit )
{
    for( int i = 0; i < tries; i++ ) {
        zed.set_hp( zed.get_hp_max() );
        zed.clear_effects();
        you.set_moves( 100 );
        you.set_stamina( you.get_stamina_max() );
        Messages::clear_messages();
        const melee_statistic_data before = melee::get_stats();
        you.melee_attack( zed, true );
        const melee_statistic_data after = melee::get_stats();
        if( after.hit_count > before.hit_count &&
            on_hit( after.actual_crit_count > before.actual_crit_count ) ) {
            return true;
        }
    }
    return false;
}

TEST_CASE( "grapple_tm_chosen_technique_is_used", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    avatar &you = manual_fighter();
    const on_out_of_scope manual_off( []() {
        get_avatar().manual_combat_mode = false;
    } );
    you.set_skill_level( skill_melee, 10 );
    monster &zed = spawn_test_monster( "mon_zombie", dude_pos + tripoint::east );
    melee::clear_stats();

    SECTION( "a technique" ) {
        menu_answer answer( answer_technique( test_tm_strike ) );
        CHECK( attack_until( you, zed, 50, []( bool ) {
            return messages_have( "You tm-strike" );
        } ) );
    }
    SECTION( "a crit-only technique without a crit is a basic attack" ) {
        menu_answer answer( answer_technique( test_tm_crit ) );
        CHECK( attack_until( you, zed, 50, []( bool crit ) {
            return !crit && messages_have( "Not a critical hit: no TM Crit Strike this time." ) &&
                   !messages_have( "You tm-crit" );
        } ) );
    }
    SECTION( "a basic attack, unarmed, is a punch or a kick" ) {
        menu_answer answer( answer_basic );
        CHECK( attack_until( you, zed, 50, []( bool ) {
            return messages_have( "You punch" ) || messages_have( "You kick" );
        } ) );
    }
}

TEST_CASE( "grapple_tm_forced_technique_gets_attack_vector", "[grapple][martial_arts]" )
{
    clear_map_without_vision();
    standard_npc dude( "TestCharacter", dude_pos, {}, 0, 8, 8, 8, 8 );
    clear_character( dude, true );
    use_style( dude, test_style_technique_menu );

    const std::tuple<matec_id, attack_vector_id, sub_bodypart_str_id> strike =
        dude.forced_technique_attack( test_tm_strike );
    CHECK( std::get<0>( strike ) == test_tm_strike );
    CHECK( std::get<1>( strike ) == vector_punch );
    CHECK( !std::get<2>( strike ).is_null() );
    // No technique: no vector
    CHECK( std::get<1>( dude.forced_technique_attack( tec_none ) ).is_null() );
}

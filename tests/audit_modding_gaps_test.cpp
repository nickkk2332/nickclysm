#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapgen_primitives.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "math_parser_type.h"
#include "mongroup.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"

static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id test_bio_mutj03_toggle( "test_bio_mutj03_toggle" );

static const bodypart_str_id body_part_test_tail( "test_tail" );

static const effect_on_condition_id
effect_on_condition_EOC_TEST_DAMAGE_ALL_WHEELS( "EOC_TEST_DAMAGE_ALL_WHEELS" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_DAMAGE_WHEELS( "EOC_TEST_DAMAGE_WHEELS" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_FREE_BIONIC( "EOC_TEST_FREE_BIONIC" );
static const effect_on_condition_id effect_on_condition_EOC_TEST_GROW_TAIL( "EOC_TEST_GROW_TAIL" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_LOSE_BODY_PART( "EOC_TEST_LOSE_BODY_PART" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_OWN_VEHICLE( "EOC_TEST_OWN_VEHICLE" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_PAID_BIONIC( "EOC_TEST_PAID_BIONIC" );

static const efftype_id effect_test_grapple_mark( "test_grapple_mark" );

static const faction_id faction_free_merchants( "free_merchants" );
static const faction_id faction_test_npc_ai_doves( "test_npc_ai_doves" );
static const faction_id faction_test_npc_ai_hawks( "test_npc_ai_hawks" );

static const itype_id itype_gloves_leather( "gloves_leather" );

static const mfaction_str_id monfaction_animal( "animal" );
static const mfaction_str_id monfaction_zombie( "zombie" );

static const mongroup_id GROUP_TEST_SPAWN_DATA( "GROUP_TEST_SPAWN_DATA" );
static const mongroup_id GROUP_TEST_SPAWN_LOC( "GROUP_TEST_SPAWN_LOC" );

static const mtype_id mon_null( "mon_null" );
static const mtype_id mon_test_cmbt_post( "mon_test_cmbt_post" );

static const spell_id spell_test_spell_enchant_other( "test_spell_enchant_other" );
static const spell_id spell_test_spell_enchant_power( "test_spell_enchant_power" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_grass( "t_grass" );

static const trait_id trait_TEST_SPELL_TRICKS( "TEST_SPELL_TRICKS" );

static const vproto_id vehicle_prototype_car( "car" );

namespace
{
double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

std::string global_text( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

double global_number( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? 0.0 : value->dbl();
}
} // namespace

// CHAR-J01: no EOC could give or take a body part; only mutations and enchantments changed the
// body, and anything else was undone at the next rebuild
TEST_CASE( "char-j01_eocs_add_and_lose_body_parts", "[audit][character][eoc]" )
{
    clear_avatar();
    clear_map();
    get_globals().clear_global_values();
    on_out_of_scope cleanup( []() {
        clear_avatar();
        get_globals().clear_global_values();
    } );
    avatar &u = get_avatar();
    dialogue d( get_talker_for( u ), nullptr );

    REQUIRE_FALSE( u.has_part( body_part_test_tail.id() ) );
    effect_on_condition_EOC_TEST_GROW_TAIL->activate( d );
    CHECK( u.has_part( body_part_test_tail.id() ) );
    CHECK( global_number( "test_body_changed" ) == 1 );
    // the body is rebuilt from the anatomy and enchantments every turn; the tail stays
    u.recalculate_enchantment_cache();
    CHECK( u.has_part( body_part_test_tail.id() ) );
    // a second tail can't be added
    effect_on_condition_EOC_TEST_GROW_TAIL->activate( d );
    CHECK( global_text( "test_body_refused" ).find( "already" ) != std::string::npos );

    SECTION( "losing both arms takes the hands, and the gloves fall off" ) {
        REQUIRE( u.wear_item( item( itype_gloves_leather ), false ) );
        get_globals().set_global_value( "test_body_part", std::string( "arm_l" ) );
        effect_on_condition_EOC_TEST_LOSE_BODY_PART->activate( d );
        CHECK_FALSE( u.has_part( body_part_arm_l.id() ) );
        CHECK_FALSE( u.has_part( body_part_hand_l.id() ) );
        // the gloves still cover the right hand
        CHECK( u.is_wearing( itype_gloves_leather ) );
        get_globals().set_global_value( "test_body_part", std::string( "arm_r" ) );
        effect_on_condition_EOC_TEST_LOSE_BODY_PART->activate( d );
        CHECK_FALSE( u.has_part( body_part_hand_r.id() ) );
        CHECK_FALSE( u.is_wearing( itype_gloves_leather ) );
        int on_the_ground = 0;
        for( const item &it : get_map().i_at( u.pos_bub() ) ) {
            on_the_ground += it.typeId() == itype_gloves_leather ? 1 : 0;
        }
        CHECK( on_the_ground == 1 );
        // still gone after a rebuild
        u.recalculate_enchantment_cache();
        CHECK_FALSE( u.has_part( body_part_arm_l.id() ) );
    }
    SECTION( "the root part and vital parts can't be lost" ) {
        get_globals().set_global_value( "test_body_part", std::string( "head" ) );
        effect_on_condition_EOC_TEST_LOSE_BODY_PART->activate( d );
        CHECK( u.has_part( body_part_head.id() ) );
        CHECK( global_text( "test_body_refused" ).find( "root" ) != std::string::npos );
        get_globals().set_global_value( "test_body_part", std::string( "torso" ) );
        effect_on_condition_EOC_TEST_LOSE_BODY_PART->activate( d );
        CHECK( u.has_part( body_part_torso.id() ) );
        CHECK( global_text( "test_body_refused" ).find( "vital" ) != std::string::npos );
    }
    SECTION( "a lost part comes back with u_add_body_part" ) {
        get_globals().set_global_value( "test_body_part", std::string( "test_tail" ) );
        effect_on_condition_EOC_TEST_LOSE_BODY_PART->activate( d );
        CHECK_FALSE( u.has_part( body_part_test_tail.id() ) );
        effect_on_condition_EOC_TEST_GROW_TAIL->activate( d );
        CHECK( u.has_part( body_part_test_tail.id() ) );
    }
}

// MUT-J03: EOC math had no bionic functions, and an EOC could only switch a bionic by paying
// its power cost
TEST_CASE( "mut-j03_bionic_math_and_activation_without_cost", "[audit][bionics][math_parser]" )
{
    clear_avatar();
    get_globals().clear_global_values();
    on_out_of_scope cleanup( []() {
        clear_avatar();
        get_globals().clear_global_values();
    } );
    avatar &u = get_avatar();
    u.add_bionic( test_bio_mutj03_toggle );
    u.set_power_level( 0_kJ );
    dialogue d( get_talker_for( u ), nullptr );

    CHECK( eval_math( d, "u_bionic_count('test_bio_mutj03_toggle')" ) == 1 );
    CHECK( eval_math( d, "u_bionic_count('ANY')" ) == u.num_bionics() );
    CHECK( eval_math( d, "bionic_cost('test_bio_mutj03_toggle')" ) == 1000000 );
    CHECK( eval_math( d, "bionic_cost('test_bio_mutj03_toggle', 'type': 'deactivate')" ) == 0 );

    // with no power, paying for it fails, and ignoring the cost doesn't
    effect_on_condition_EOC_TEST_PAID_BIONIC->activate( d );
    CHECK( global_number( "test_bionic_refused" ) == 1 );
    CHECK( eval_math( d, "u_has_active_bionic('test_bio_mutj03_toggle')" ) == 0 );
    effect_on_condition_EOC_TEST_FREE_BIONIC->activate( d );
    CHECK( global_number( "test_bionic_switched" ) == 1 );
    CHECK( eval_math( d, "u_has_active_bionic('test_bio_mutj03_toggle')" ) == 1 );
    CHECK( u.get_power_level() == 0_kJ );

    // shorting it out switches it off
    eval_math( d, "u_bionic_incapacitated('test_bio_mutj03_toggle') = 5" );
    CHECK( eval_math( d, "u_bionic_incapacitated('test_bio_mutj03_toggle')" ) == 5 );
    CHECK( eval_math( d, "u_has_active_bionic('test_bio_mutj03_toggle')" ) == 0 );

    // stored power, in millijoules, kept within the capacity
    u.add_bionic( bio_power_storage );
    eval_math( d, "u_bionic_power() = 5000" );
    CHECK( u.get_power_level() == 5_J );
    CHECK( eval_math( d, "u_bionic_power_max()" ) == units::to_millijoule( u.get_max_power_level() ) );
    eval_math( d, "u_bionic_power() = u_bionic_power_max() * 10" );
    CHECK( u.get_power_level() == u.get_max_power_level() );

    CHECK_THROWS_AS( eval_math( d, "u_bionic_count('bio_no_such_bionic')" ), math::runtime_error );
}

// MAGIC-J02: an enchantment's spell_values couldn't change a spell's fail chance, difficulty,
// casting XP or sound, and EOC math couldn't read them
TEST_CASE( "magic-j02_spell_values_change_fail_difficulty_xp_and_sound",
           "[audit][magic][enchantments]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const spell probe( spell_test_spell_enchant_power );
    const spell other( spell_test_spell_enchant_other );
    const int difficulty = probe.get_difficulty( you );
    const int sound = probe.sound_volume( you );
    const int other_exp = other.casting_exp( you );
    const int other_difficulty = other.get_difficulty( you );

    you.set_mutation( trait_TEST_SPELL_TRICKS );
    you.recalculate_enchantment_cache();
    REQUIRE( you.has_trait( trait_TEST_SPELL_TRICKS ) );

    CHECK( probe.get_difficulty( you ) == difficulty + 5 );
    CHECK( probe.sound_volume( you ) == sound + 7 );
    CHECK( probe.spell_fail( you ) == Approx( std::min( 1.0f,
            probe.spell_fail_unenchanted( you ) + 0.25f ) ) );
    // EXP is filtered to the other spell, and doubles what a cast teaches
    CHECK( other.casting_exp( you ) == 2 * other_exp );
    CHECK( other.get_difficulty( you ) == other_difficulty );

    dialogue d( get_talker_for( you ), nullptr );
    CHECK( eval_math( d, "u_spell_fail('test_spell_enchant_power')" ) ==
           Approx( probe.spell_fail( you ) ) );
    CHECK( eval_math( d, "u_spell_sound('test_spell_enchant_power')" ) == sound + 7 );
    CHECK( eval_math( d, "u_spell_casting_exp('test_spell_enchant_other')" ) == 2 * other_exp );
    CHECK( eval_math( d, "u_spell_energy_cost('test_spell_enchant_power')" ) ==
           probe.energy_cost( you ) );
    CHECK( eval_math( d, "u_spell_cast_time('test_spell_enchant_power')" ) ==
           probe.casting_time( you ) );
    // it doesn't know the spell
    CHECK( eval_math( d, "u_spell_exp_to_next_level('test_spell_enchant_power')" ) == -1 );
    CHECK_THROWS_AS( eval_math( d, "u_spell_fail('no_such_spell')" ), math::runtime_error );
    clear_avatar();
}

// VEH-J01: no EOC could damage a vehicle's parts or change who owns it
TEST_CASE( "veh-j01_eocs_damage_vehicle_parts_and_set_the_owner", "[audit][vehicle][eoc]" )
{
    clear_avatar();
    clear_map();
    clear_vehicles();
    get_globals().clear_global_values();
    on_out_of_scope cleanup( []() {
        clear_vehicles();
        get_globals().clear_global_values();
    } );
    map &here = get_map();
    vehicle *car = here.add_vehicle( vehicle_prototype_car, tripoint_bub_ms( 60, 60, 0 ),
                                     0_degrees, 0, veh_spawn_status::UNDAMAGED );
    REQUIRE( car != nullptr );
    const int parts = car->part_count();
    int wheels = 0;
    const auto broken_wheels = [car]() {
        int broken = 0;
        for( int idx = 0; idx < car->part_count(); idx++ ) {
            const vehicle_part &vp = car->part( idx );
            broken += vp.info().has_flag( "WHEEL" ) && vp.is_broken() ? 1 : 0;
        }
        return broken;
    };
    for( int idx = 0; idx < car->part_count(); idx++ ) {
        wheels += car->part( idx ).info().has_flag( "WHEEL" ) ? 1 : 0;
    }
    REQUIRE( wheels > 2 );
    REQUIRE( broken_wheels() == 0 );

    dialogue d( get_talker_for( *car ), nullptr );
    effect_on_condition_EOC_TEST_DAMAGE_WHEELS->activate( d );
    CHECK( global_number( "test_veh_damaged" ) == 2 );
    CHECK( global_number( "test_veh_broken" ) == 2 );
    CHECK( global_number( "test_veh_destroyed" ) == 0 );
    CHECK( broken_wheels() == 2 );

    // without "tear_off", the wheels already broken are left alone
    effect_on_condition_EOC_TEST_DAMAGE_ALL_WHEELS->activate( d );
    CHECK( global_number( "test_veh_damaged" ) == wheels - 2 );
    CHECK( broken_wheels() == wheels );
    CHECK( car->part_count() == parts );

    effect_on_condition_EOC_TEST_OWN_VEHICLE->activate( d );
    CHECK( car->get_owner() == faction_free_merchants );
    CHECK( global_number( "test_veh_owned" ) == 1 );
}

// NPC-J05: no condition read the relation between the two talkers' factions
TEST_CASE( "npc-j05_has_fac_relation_reads_the_talkers_factions", "[audit][npc][eoc]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    const avatar &u = get_avatar();
    npc &hawk = spawn_npc( u.pos_bub().xy() + point_rel_ms( 2, 0 ), "test_talker" );
    npc &dove = spawn_npc( u.pos_bub().xy() + point_rel_ms( -2, 0 ), "test_talker" );
    hawk.set_fac( faction_test_npc_ai_hawks );
    dove.set_fac( faction_test_npc_ai_doves );

    const dialogue d( get_talker_for( hawk ), get_talker_for( dove ) );
    const auto holds = [&d]( const std::string & json ) {
        const JsonObject jo = json_loader::from_string( json );
        conditional_t::func condition;
        read_condition( jo, "condition", condition, false );
        return condition( d );
    };
    // the hawks kill the doves on sight, and not the other way round
    CHECK( holds( R"({ "condition": { "u_has_fac_relation": "kill on sight" } })" ) );
    CHECK_FALSE( holds( R"({ "condition": { "npc_has_fac_relation": "kill on sight" } })" ) );
    CHECK_FALSE( holds( R"({ "condition": { "u_has_fac_relation": "share my stuff" } })" ) );
    CHECK_THROWS( holds( R"({ "condition": { "u_has_fac_relation": "be friends" } })" ) );
}

// MONDT-J01: a monster group entry's spawn_data had only ammo, and its conditions couldn't see
// where the monsters would spawn
TEST_CASE( "mondt-j01_group_spawn_data_gives_effects_variables_and_faction",
           "[audit][monster][mongroup]" )
{
    // a group entry spawns only after the cataclysm started; a test before may have left the
    // clock earlier
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::start_of_cataclysm + 1_days;
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_creatures();
    } );
    const std::vector<MonsterGroupResult> results =
        MonsterGroupManager::GetResultFromGroup( GROUP_TEST_SPAWN_DATA );
    REQUIRE( results.size() == 1 );
    const spawn_data &data = results.front().data;
    REQUIRE( data.effects.size() == 1 );

    SECTION( "the spawned monster gets them" ) {
        REQUIRE( results.front().id == mon_test_cmbt_post );
        monster &placed = spawn_test_monster( "mon_test_cmbt_post", tripoint_bub_ms( 65, 65, 0 ) );
        // what map::spawn_monsters_submap does once it has placed the monster
        data.apply( placed );
        monster *spawned = &placed;
        CHECK( spawned->get_effect_int( effect_test_grapple_mark ) == 2 );
        const diag_value *tag = spawned->maybe_get_value( "test_tag" );
        REQUIRE( tag != nullptr );
        CHECK( tag->str() == "cult" );
        const diag_value *level = spawned->maybe_get_value( "test_level" );
        REQUIRE( level != nullptr );
        CHECK( level->dbl() == 3 );
        CHECK( spawned->faction == monfaction_animal.id() );
    }
    SECTION( "a pending spawn keeps them in a save" ) {
        std::ostringstream saved;
        JsonOut json( saved );
        data.serialize_saved( json );
        spawn_data loaded;
        loaded.deserialize_saved( json_loader::from_string( saved.str() ) );
        REQUIRE( loaded.effects.size() == 1 );
        CHECK( loaded.effects.front().id == effect_test_grapple_mark );
        CHECK( loaded.effects.front().intensity.val == 2 );
        CHECK( loaded.str_variables == data.str_variables );
        CHECK( loaded.num_variables == data.num_variables );
        CHECK( loaded.faction == monfaction_animal );
        CHECK_FALSE( loaded.empty() );
    }
    SECTION( "a mapgen piece's spawn data goes on top of the entry's" ) {
        spawn_data piece;
        piece.faction = monfaction_zombie;
        piece.str_variables["test_tag"] = "piece";
        const spawn_data merged = merged_spawn_data( data, piece );
        CHECK( merged.faction == monfaction_zombie );
        CHECK( merged.str_variables.at( "test_tag" ) == "piece" );
        CHECK( merged.num_variables.at( "test_level" ) == 3 );
        CHECK( merged.effects.size() == 1 );
    }
}

TEST_CASE( "mondt-j01_group_conditions_see_the_spawn_location", "[audit][monster][mongroup]" )
{
    // a group entry spawns only after the cataclysm started; a test before may have left the
    // clock earlier
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::start_of_cataclysm + 1_days;
    clear_avatar();
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms floor_tile( 70, 70, 0 );
    const tripoint_bub_ms grass_tile( 72, 70, 0 );
    here.ter_set( floor_tile, ter_t_floor );
    here.ter_set( grass_tile, ter_t_grass );

    const auto spawned_at = [&here]( const tripoint_bub_ms & where ) {
        const mongroup_spawn_location_scope spawning_at( here.get_abs( where ) );
        const std::vector<MonsterGroupResult> results =
            MonsterGroupManager::GetResultFromGroup( GROUP_TEST_SPAWN_LOC );
        REQUIRE( results.size() == 1 );
        return results.front().id;
    };
    CHECK( spawned_at( floor_tile ) == mon_test_cmbt_post );
    CHECK( spawned_at( grass_tile ) == mon_null );
}

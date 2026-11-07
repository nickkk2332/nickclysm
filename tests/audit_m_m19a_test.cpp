#include <cstddef>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "avatar.h"
#include "behavior.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "enums.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mattack_common.h"
#include "math_parser_diag_value.h"
#include "monattack.h"
#include "monster.h"
#include "monster_oracle.h"
#include "mtype.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "sounds.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather_type.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_M19A_TURN_COST( "EOC_TEST_M19A_TURN_COST" );

static const efftype_id effect_pacified( "pacified" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_test_m19a_keen_ears( "test_m19a_keen_ears" );
static const efftype_id effect_test_m19a_silent_steps( "test_m19a_silent_steps" );

static const itype_id itype_test_m19a_wood( "test_m19a_wood" );
static const itype_id itype_test_m19a_wood_steel( "test_m19a_wood_steel" );

static const mtype_id mon_test_m19a_base( "mon_test_m19a_base" );
static const mtype_id mon_test_m19a_biosig( "mon_test_m19a_biosig" );
static const mtype_id mon_test_m19a_ears_good( "mon_test_m19a_ears_good" );
static const mtype_id mon_test_m19a_ears_x3( "mon_test_m19a_ears_x3" );
static const mtype_id mon_test_m19a_nose_child( "mon_test_m19a_nose_child" );
static const mtype_id mon_test_m19a_nose_keen( "mon_test_m19a_nose_keen" );
static const mtype_id mon_test_m19a_nose_set( "mon_test_m19a_nose_set" );
static const mtype_id mon_test_m19a_poly( "mon_test_m19a_poly" );
static const mtype_id mon_test_m19a_poly_result( "mon_test_m19a_poly_result" );
static const mtype_id mon_test_m19a_poly_target( "mon_test_m19a_poly_target" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_water_sh( "t_water_sh" );

static const vproto_id vehicle_prototype_test_m19a_board( "test_m19a_board" );

namespace
{
// Every test starts from, and leaves, an empty map in daylight and clear weather
struct m19a_scene {
    restore_on_out_of_scope<time_point> turn{ calendar::turn };
    scoped_weather_override weather{ WEATHER_CLEAR };

    m19a_scene() {
        get_globals().clear_global_values();
        clear_avatar();
        clear_map();
        clear_creatures();
        clear_npcs();
        clear_vehicles();
        sounds::reset_sounds();
        set_time_to_day();
    }
    ~m19a_scene() {
        get_globals().clear_global_values();
        clear_creatures();
        clear_vehicles();
        clear_map();
        sounds::reset_sounds();
        clear_avatar();
    }
};

double global_number( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    REQUIRE( value != nullptr );
    return value->dbl();
}

std::string global_string( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    REQUIRE( value != nullptr );
    return value->str();
}

bool has_global( const std::string &name )
{
    return get_globals().maybe_get_global_value( name ) != nullptr;
}

// Saved JSON text with the value of the member key (a number or null) replaced
std::string with_member( std::string text, const std::string &key, const std::string &value )
{
    const std::string quoted = "\"" + key + "\"";
    const size_t at = text.find( quoted );
    REQUIRE( at != std::string::npos );
    const size_t colon = text.find( ':', at + quoted.size() );
    REQUIRE( colon != std::string::npos );
    const size_t end = text.find_first_of( ",}", colon + 1 );
    REQUIRE( end != std::string::npos );
    text.replace( colon + 1, end - colon - 1, value );
    return text;
}
} // namespace

// MONAI-B04: polymorph_special loaded its condition but never tested it (Xedra's stray soul
// turned into a wendi as soon as its cooldown ran out), and could need no target.
TEST_CASE( "monai-b04_polymorph_tests_its_condition_and_target", "[audit][monster]" )
{
    m19a_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_pos( 50, 50, 0 );
    u.setpos( here, mon_pos + tripoint::east );

    SECTION( "a false condition stops the change" ) {
        monster &mon = spawn_test_monster( "mon_test_m19a_poly", mon_pos );
        mon.set_moves( 100 );
        const mattack_actor &poly = *mon.type->special_attacks.at( "test_m19a_poly" );
        CHECK( !poly.call( mon ) );
        CHECK( mon.type->id == mon_test_m19a_poly );

        get_globals().set_global_value( "test_m19a_poly_allowed", 1 );
        CHECK( poly.call( mon ) );
        CHECK( mon.type->id == mon_test_m19a_poly_result );
    }

    SECTION( "allow_no_target false waits for a target, which the condition sees as npc" ) {
        monster &mon = spawn_test_monster( "mon_test_m19a_poly_target", mon_pos );
        mon.set_moves( 100 );
        const mattack_actor &poly = *mon.type->special_attacks.at( "test_m19a_poly_target" );
        mon.unset_dest();
        REQUIRE( mon.attack_target() == nullptr );
        CHECK( !poly.call( mon ) );
        CHECK( mon.type->id == mon_test_m19a_poly_target );

        mon.set_dest( u.pos_abs() );
        REQUIRE( mon.attack_target() == &u );
        CHECK( poly.call( mon ) );
        CHECK( mon.type->id == mon_test_m19a_poly_result );
    }
}

// MONAI-B05: a new monster was last updated at turn_zero and had its biosignature timer set
// to -1, so the first on_load() (map and overmap spawns call it) caught up ~60 days: every
// special cooldown went to 0, and up to 51 droppings appeared at once.
TEST_CASE( "monai-b05_new_monster_does_not_catch_up_from_turn_zero", "[audit][monster]" )
{
    m19a_scene scene;
    map &here = get_map();
    calendar::turn = calendar::turn_zero + 60_days + 12_hours;
    const tripoint_bub_ms pos( 50, 50, 0 );
    get_avatar().setpos( here, pos + point( 0, 10 ) );

    SECTION( "on_load of a freshly spawned monster" ) {
        monster &mon = spawn_test_monster( "mon_test_m19a_biosig", pos );
        mon.set_special( "TEST_COOLDOWN", 15 );
        mon.on_load();
        CHECK( mon.special_cooldown( "TEST_COOLDOWN" ) == std::optional<int>( 15 ) );
        CHECK( here.i_at( pos ).empty() );

        // The first dropping comes one interval later
        calendar::turn += 1_days;
        mon.try_biosignature();
        CHECK( here.i_at( pos ).size() == 1 );
    }

    SECTION( "a monster saved with no timer yet, or with the old -1" ) {
        const monster fresh( mon_test_m19a_biosig, pos );
        std::ostringstream os;
        JsonOut jsout( os );
        fresh.serialize( jsout );
        for( const std::string &saved_timer : {
                 std::string( "null" ), std::string( "-1" )
             } ) {
            CAPTURE( saved_timer );
            monster loaded;
            loaded.deserialize( json_loader::from_string( with_member( os.str(), "biosig_timer",
                                saved_timer ) ) );
            REQUIRE( loaded.pos_bub() == pos );
            loaded.try_biosignature();
            CHECK( here.i_at( pos ).empty() );
            here.i_clear( pos );
        }
    }
}

// MONAI-B06: knock_back_to killed every AQUATIC monster (they can't drown, and the test was
// "didn't drown and is AQUATIC"), and put a monster knocked toward the player on the player.
TEST_CASE( "monai-b06_knockback_spares_swimmers_and_bounces_off_the_player", "[audit][monster]" )
{
    m19a_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms start( 40, 40, 0 );

    SECTION( "a fish knocked through water lives, and onto land dies" ) {
        u.setpos( here, start + point( 0, 10 ) );
        here.ter_set( start, ter_t_water_sh );
        here.ter_set( start + tripoint::east, ter_t_water_sh );
        here.ter_set( start + point( 2, 0 ), ter_t_floor );
        monster &fish = spawn_test_monster( "mon_test_m19a_fish", start );

        fish.knock_back_to( start + tripoint::east );
        CHECK( !fish.is_dead() );
        CHECK( fish.pos_bub() == start + tripoint::east );

        fish.knock_back_to( start + point( 2, 0 ) );
        CHECK( fish.is_dead() );
    }

    SECTION( "a monster knocked toward the player bounces off" ) {
        u.setpos( here, start + tripoint::east );
        monster &zed = spawn_test_monster( "mon_test_m19a_base", start );

        zed.knock_back_to( start + tripoint::east );
        CHECK( zed.pos_bub() == start );
        CHECK( zed.has_effect( effect_stunned ) );
        CHECK( u.pos_bub() == start + tripoint::east );
    }
}

// MONAI-B08: every species with a baby type got character-aggressive when its baby was near
// the player, without the PLAYER_NEAR_BABY trigger; and the baby only counted when it was
// nearer to the player than its parent.  (Also the guard that never returned early,
// MONAI-P02, and the baby's distance overwriting the parent's target distance.)
TEST_CASE( "monai-b08_only_a_protective_parent_is_angered_near_its_baby", "[audit][monster]" )
{
    m19a_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms you_pos( 50, 50, 0 );
    u.setpos( here, you_pos );

    // Whether the mother, at mother_offset from the player, gets character-aggressive in its
    // plan when its cub is at cub_offset; and how much angrier it gets
    const auto angered = [&here, &u, &you_pos]( const std::string & mother_id,
    const point & mother_offset, const point & cub_offset ) {
        clear_creatures();
        monster &cub = spawn_test_monster( "mon_test_m19a_cub", you_pos + cub_offset );
        monster &mother = spawn_test_monster( mother_id, you_pos + mother_offset );
        REQUIRE( mother.sees( here, u ) );
        REQUIRE( cub.sees( here, u ) );
        REQUIRE( !mother.aggro_character );
        const int anger_before = mother.anger;
        mother.plan();
        return std::make_pair( mother.aggro_character, mother.anger - anger_before );
    };

    CHECK( angered( "mon_test_m19a_mother_calm", point( 3, 0 ), point( 1, 0 ) ) ==
           std::make_pair( false, 0 ) );
    CHECK( angered( "mon_test_m19a_mother_protective", point( 3, 0 ), point( 1, 0 ) ) ==
           std::make_pair( true, 8 ) );
    // The cub is farther from the player than its mother
    CHECK( angered( "mon_test_m19a_mother_protective", point( -1, 0 ), point( 2, 0 ) ) ==
           std::make_pair( true, 8 ) );
    // Too far from the player to be threatened
    CHECK( angered( "mon_test_m19a_mother_protective", point( 3, 0 ), point( 0, 5 ) ) ==
           std::make_pair( false, 0 ) );
}

// MONAI-B09: absorb_items kept absorb_ml_per_hp in a function-local static, so the first type
// to absorb anything set the rate of every type; and the behavior tree's items_available saw
// an item with a forbidden material as available.
TEST_CASE( "monai-b09_absorb_items_rate_per_type_and_one_material_rule", "[audit][monster]" )
{
    m19a_scene scene;
    map &here = get_map();
    const tripoint_bub_ms pos( 50, 50, 0 );
    get_avatar().setpos( here, pos + point( 0, 10 ) );
    REQUIRE( units::to_milliliter( item( itype_test_m19a_wood ).volume() ) == 2000 );

    SECTION( "each type gains HP at its own rate" ) {
        const auto hp_gained = [&here, &pos]( const std::string & id ) {
            clear_creatures();
            here.i_clear( pos );
            monster &mon = spawn_test_monster( id, pos );
            mon.set_moves( 100 );
            here.add_item( pos, item( itype_test_m19a_wood ) );
            const int hp_before = mon.get_hp();
            REQUIRE( mattack::absorb_items( &mon ) );
            CHECK( here.i_at( pos ).empty() );
            return mon.get_hp() - hp_before;
        };
        CHECK( hp_gained( "mon_test_m19a_absorb_100" ) == 20 );
        CHECK( hp_gained( "mon_test_m19a_absorb_1000" ) == 2 );
        CHECK( hp_gained( "mon_test_m19a_absorb_100" ) == 20 );
    }

    SECTION( "an item with a forbidden material is never absorbed, nor seen as available" ) {
        monster &no_steel = spawn_test_monster( "mon_test_m19a_absorb_no_steel", pos );
        CHECK( !no_steel.can_absorb_item( item( itype_test_m19a_wood_steel ) ) );
        CHECK( no_steel.can_absorb_item( item( itype_test_m19a_wood ) ) );

        const behavior::monster_oracle_t oracle( &no_steel );
        here.add_item( pos, item( itype_test_m19a_wood_steel ) );
        CHECK( oracle.items_available( "" ) == behavior::status_t::failure );
        here.add_item( pos, item( itype_test_m19a_wood ) );
        CHECK( oracle.items_available( "" ) == behavior::status_t::running );
    }

    SECTION( "the forbidden list wins over the allowed one" ) {
        monster &wood_only = spawn_test_monster( "mon_test_m19a_absorb_wood", pos );
        CHECK( !wood_only.can_absorb_item( item( itype_test_m19a_wood_steel ) ) );
        CHECK( wood_only.can_absorb_item( item( itype_test_m19a_wood ) ) );
    }
}

// MONAI-B10: monster::move() shoved a vehicle on every square it considered, before choosing
// one, even while pacified.  It now shoves once, on the square it steps to.
TEST_CASE( "monai-b10_push_veh_monster_shoves_only_the_vehicle_on_its_step",
           "[audit][monster][vehicle]" )
{
    m19a_scene scene;
    map &here = get_map();
    const tripoint_bub_ms mon_pos( 40, 40, 0 );
    const tripoint_bub_ms veh_pos = mon_pos + tripoint::east;
    get_avatar().setpos( here, mon_pos + point( 0, 10 ) );
    vehicle *veh = here.add_vehicle( vehicle_prototype_test_m19a_board, veh_pos, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( here.veh_at( veh_pos ).obstacle_at_part().has_value() );
    monster &pusher = spawn_test_monster( "mon_test_m19a_pusher", mon_pos );
    pusher.set_moves( 100 );

    SECTION( "the vehicle in the way is shoved aside, and the monster steps in" ) {
        pusher.set_dest( here.get_abs( mon_pos + point( 4, 0 ) ) );
        pusher.move();
        CHECK( !here.veh_at( veh_pos ).has_value() );
        CHECK( here.veh_at( veh_pos + tripoint::east ).has_value() );
        CHECK( pusher.pos_bub() == veh_pos );
    }

    SECTION( "a pacified monster doesn't shove" ) {
        pusher.add_effect( effect_pacified, 1_hours );
        pusher.set_dest( here.get_abs( mon_pos + point( 4, 0 ) ) );
        pusher.move();
        CHECK( here.veh_at( veh_pos ).has_value() );
        CHECK( veh->velocity == 0 );
        CHECK( !veh->skidding );
        CHECK( pusher.pos_bub() != veh_pos );
    }

    SECTION( "the vehicle the monster is heading for isn't shoved away" ) {
        pusher.set_dest( here.get_abs( veh_pos ) );
        pusher.move();
        CHECK( here.veh_at( veh_pos ).has_value() );
        CHECK( veh->velocity == 0 );
        CHECK( !veh->skidding );
    }
}

// MONAI-J02: an EOC attack with range 1 reached any target in sight, cost no moves, and
// could only keep or drop its target; turn_cost only worked on characters.
TEST_CASE( "monai-j02_eoc_attack_range_move_cost_and_optional_target", "[audit][monster][eoc]" )
{
    m19a_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_pos( 50, 50, 0 );
    u.setpos( here, mon_pos + point( 3, 0 ) );
    monster &mon = spawn_test_monster( "mon_test_m19a_eoc_user", mon_pos );
    mon.set_dest( u.pos_abs() );
    REQUIRE( mon.attack_target() == &u );
    mon.set_moves( 100 );
    const mattack_actor &melee = *mon.type->special_attacks.at( "test_m19a_eoc_melee" );
    const mattack_actor &target_optional = *mon.type->special_attacks.at( "test_m19a_eoc_optional" );

    SECTION( "range 1 is melee range, and the attack takes its move_cost" ) {
        CHECK( !melee.call( mon ) );
        CHECK( !has_global( "test_m19a_eoc_with_target" ) );

        u.setpos( here, mon_pos + tripoint::east );
        mon.set_dest( u.pos_abs() );
        CHECK( melee.call( mon ) );
        CHECK( global_number( "test_m19a_eoc_with_target" ) == 1 );
        CHECK( mon.get_moves() == 50 );
    }

    SECTION( "target_optional uses a target in range, and runs without one" ) {
        CHECK( target_optional.call( mon ) );
        CHECK( global_number( "test_m19a_eoc_with_target" ) == 1 );
        // No move_cost given: free, as EOC attacks were
        CHECK( mon.get_moves() == 100 );

        mon.unset_dest();
        CHECK( target_optional.call( mon ) );
        CHECK( global_number( "test_m19a_eoc_no_target" ) == 1 );

        // Beyond its range 3 the target is left out
        u.setpos( here, mon_pos + point( 5, 0 ) );
        mon.set_dest( u.pos_abs() );
        REQUIRE( mon.attack_target() == &u );
        CHECK( target_optional.call( mon ) );
        CHECK( global_number( "test_m19a_eoc_no_target" ) == 2 );
        CHECK( global_number( "test_m19a_eoc_with_target" ) == 1 );
    }

    SECTION( "turn_cost takes moves from a monster" ) {
        dialogue d( get_talker_for( mon ), nullptr );
        effect_on_condition_EOC_TEST_M19A_TURN_COST->activate( d );
        CHECK( mon.get_moves() == 0 );
    }
}

// MONAI-J03: leap and gun conditions had no npc, melee EOCs got only the total damage and
// nothing on a miss, and cooldowns read the avatar wherever it was.
TEST_CASE( "monai-j03_attack_eocs_see_the_target", "[audit][monster][eoc]" )
{
    m19a_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_pos( 50, 50, 0 );
    u.setpos( here, mon_pos + point( 0, 10 ) );

    SECTION( "a cooldown reads the target, not the avatar" ) {
        u.set_value( "test_m19a_cd", 100 );
        monster &mon = spawn_test_monster( "mon_test_m19a_cd_user", mon_pos );
        // No target at spawn: npc's values read as 0, so it's rng( 0, 0 + 2 )
        const std::optional<int> at_spawn = mon.special_cooldown( "test_m19a_eoc_cd" );
        REQUIRE( at_spawn.has_value() );
        CHECK( *at_spawn <= 2 );

        monster &other = spawn_test_monster( "mon_test_m19a_dodger", mon_pos + tripoint::east );
        other.set_value( "test_m19a_cd", 5 );
        mon.set_dest( other.pos_abs() );
        REQUIRE( mon.attack_target() == &other );
        mon.reset_special( "test_m19a_eoc_cd" );
        CHECK( mon.special_cooldown( "test_m19a_eoc_cd" ) == std::optional<int>( 7 ) );

        mon.unset_dest();
        mon.reset_special( "test_m19a_eoc_cd" );
        CHECK( mon.special_cooldown( "test_m19a_eoc_cd" ) == std::optional<int>( 2 ) );
    }

    SECTION( "a leap's condition and landing EOC see the target" ) {
        u.setpos( here, mon_pos + point( 5, 0 ) );
        monster &leaper = spawn_test_monster( "mon_test_m19a_leaper", mon_pos );
        leaper.set_dest( u.pos_abs() );
        leaper.set_moves( 100 );
        REQUIRE( leaper.attack_target() == &u );
        const mattack_actor &leap = *leaper.type->special_attacks.at( "test_m19a_leap" );

        CHECK( leap.call( leaper ) );
        CHECK( rl_dist( leaper.pos_bub(), u.pos_bub() ) < 5 );
        CHECK( global_number( "test_m19a_landed" ) == 1 );
    }

    SECTION( "melee EOCs get the body part and each damage type, and a miss runs its own" ) {
        monster &striker = spawn_test_monster( "mon_test_m19a_striker", mon_pos );
        monster &dodger = spawn_test_monster( "mon_test_m19a_dodger", mon_pos + tripoint::east );
        striker.set_dest( dodger.pos_abs() );
        REQUIRE( striker.attack_target() == &dodger );

        const mattack_actor &sure = *striker.type->special_attacks.at( "test_m19a_sure_strike" );
        striker.set_moves( 100 );
        REQUIRE( sure.call( striker ) );
        CHECK( global_number( "test_m19a_hit_cut" ) > 0 );
        CHECK( global_number( "test_m19a_hit_cut" ) == global_number( "test_m19a_hit_damage" ) );
        CHECK( !global_string( "test_m19a_hit_bp" ).empty() );
        CHECK( !has_global( "test_m19a_misses" ) );

        // A dodge roll of 200 (dodge 40) against an accuracy 0 roll (0 +- 25): a miss
        rng_set_engine_seed( 4242 );
        const mattack_actor &strike = *striker.type->special_attacks.at( "test_m19a_strike" );
        for( int i = 0; i < 10 && !has_global( "test_m19a_misses" ); i++ ) {
            striker.set_moves( 100 );
            REQUIRE( strike.call( striker ) );
        }
        CHECK( global_number( "test_m19a_misses" ) >= 1 );
        CHECK( !global_string( "test_m19a_miss_bp" ).empty() );
    }
}

// MONAI-J04: hearing, smell and footsteps were flags only, and the HEARING_MULT and
// FOOTSTEP_NOISE enchantments didn't reach monsters.  Also a small QUIETMOVES monster's
// footsteps came to -1.
TEST_CASE( "monai-j04_hearing_smell_and_footsteps_from_json_and_enchantments",
           "[audit][monster]" )
{
    m19a_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms pos( 50, 50, 0 );

    SECTION( "hearing" ) {
        u.setpos( here, pos + point( 0, 10 ) );
        const tripoint_bub_ms source = pos + point( 5, 0 );
        // The urge to follow a provocative sound of 10 from 5 tiles away: what it hears
        // (6 times as much with GOODHEARING)
        const auto urge = [&source]( monster & mon ) {
            REQUIRE( mon.wandf == 0 );
            mon.hear_sound( source, 10, 5, true );
            return mon.wandf;
        };
        CHECK( urge( spawn_test_monster( "mon_test_m19a_base", pos ) ) == 5 );
        CHECK( urge( spawn_test_monster( "mon_test_m19a_ears_x3", pos + point( 0, 2 ) ) ) == 25 );
        CHECK( urge( spawn_test_monster( "mon_test_m19a_ears_good", pos + point( 0, 4 ) ) ) == 90 );
        CHECK( mon_test_m19a_ears_x3->get_hearing_mult() == 3.0f );
        CHECK( mon_test_m19a_ears_good->get_hearing_mult() == 2.0f );
        CHECK( mon_test_m19a_base->get_hearing_mult() == 1.0f );

        monster &keen = spawn_test_monster( "mon_test_m19a_base", pos + point( 0, 6 ) );
        keen.add_effect( effect_test_m19a_keen_ears, 1_hours );
        // Rebuilds the enchantments from the effects
        keen.process_effects();
        CHECK( urge( keen ) == 15 );
    }

    SECTION( "smell" ) {
        CHECK( mon_test_m19a_base->get_scent_min() == 10 );
        CHECK( mon_test_m19a_base->get_scent_max() == 200 );
        CHECK( mon_test_m19a_nose_keen->get_scent_min() == 1 );
        CHECK( mon_test_m19a_nose_keen->get_scent_max() == 400 );
        CHECK( mon_test_m19a_nose_set->get_scent_min() == 5 );
        CHECK( mon_test_m19a_nose_set->get_scent_max() == 300 );
        // copy-from keeps the min it doesn't set
        CHECK( mon_test_m19a_nose_child->get_scent_min() == 5 );
        CHECK( mon_test_m19a_nose_child->get_scent_max() == 250 );
    }

    SECTION( "footsteps" ) {
        u.setpos( here, pos );
        // One step, as the avatar next to it hears it: the volume meter shows a sound on an
        // adjacent tile, less 1 for the distance
        const auto step_noise = [&u]( monster & mon ) {
            sounds::reset_sounds();
            u.volume = 0;
            mon.made_footstep = false;
            mon.footsteps( mon.pos_bub() );
            sounds::process_sound_markers( &u );
            return u.volume;
        };
        monster &plain = spawn_test_monster( "mon_test_m19a_base", pos + tripoint::east );
        CHECK( step_noise( plain ) == 5 );
        CHECK( step_noise( spawn_test_monster( "mon_test_m19a_steps_set", pos + tripoint::west ) ) == 8 );
        CHECK( step_noise( spawn_test_monster( "mon_test_m19a_steps_small_quiet",
                                               pos + tripoint::north ) ) == 0 );

        plain.add_effect( effect_test_m19a_silent_steps, 1_hours );
        plain.process_effects();
        CHECK( step_noise( plain ) == 0 );
    }
}

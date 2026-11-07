#include <array>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "field_type.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_field.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_iterator.h"
#include "map_scale_constants.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "options_helpers.h"
#include "overmap_ui.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"
#include "units.h"

static const field_type_str_id field_audit_high_fields_conveyor( "audit_high_fields_conveyor" );
static const field_type_str_id field_audit_high_fields_hellfire( "audit_high_fields_hellfire" );
static const field_type_str_id field_audit_high_fields_hot_air1( "audit_high_fields_hot_air1" );
static const field_type_str_id field_audit_high_fields_hot_air2( "audit_high_fields_hot_air2" );
static const field_type_str_id field_audit_high_fields_hot_air3( "audit_high_fields_hot_air3" );
static const field_type_str_id field_audit_high_fields_smoke( "audit_high_fields_smoke" );
static const field_type_str_id field_audit_high_fields_sparks( "audit_high_fields_sparks" );
static const field_type_str_id field_audit_high_fields_tar( "audit_high_fields_tar" );

static const furn_str_id furn_f_ash( "f_ash" );
static const furn_str_id furn_f_hay( "f_hay" );

static const itype_id itype_ash( "ash" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_test_2x4( "test_2x4" );

static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_open_air( "t_open_air" );

static const weather_type_id weather_clear( "clear" );

namespace
{

// The fields a fire run uses: the fire, its smoke and its three hot airs
struct fire_fields {
    field_type_id fire;
    field_type_id smoke;
    std::array<field_type_id, 3> hot_air;
};

// Where a fire run left things, tile by tile
struct fire_outcome {
    std::vector<int> fire;
    std::vector<int> smoke;
    std::vector<int> hot_air;
    std::vector<std::string> furniture;
    int ash = 0;

    bool operator==( const fire_outcome &rhs ) const {
        return fire == rhs.fire && smoke == rhs.smoke && hot_air == rhs.hot_air &&
               furniture == rhs.furniture && ash == rhs.ash;
    }
};

void clear_every_field()
{
    for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
        clear_fields( z );
    }
}

// Burns a 7x7 patch of hay from its middle for 300 turns, from the same seed and turn
fire_outcome run_fire( const fire_fields &fields, const tripoint_bub_ms &center )
{
    map &here = get_map();
    // Far from the fire, and the same at the start of every run
    clear_avatar();
    get_avatar().setpos( here, tripoint_bub_ms( 20, 20, 0 ) );
    clear_every_field();
    for( int z = -1; z <= 1; ++z ) {
        clear_items( z );
    }
    for( const tripoint_bub_ms &p : here.points_in_radius( center, 8 ) ) {
        here.ter_set( p, ter_t_dirt );
        here.furn_set( p, furn_str_id::NULL_ID() );
        here.ter_set( p + tripoint_rel_ms::above, ter_t_open_air );
    }
    for( const tripoint_bub_ms &p : here.points_in_radius( center, 3 ) ) {
        here.furn_set( p, furn_f_hay );
    }
    calendar::turn = calendar::turn_zero + 30_days;
    rng_set_engine_seed( 4242 );
    REQUIRE( here.add_field( center, fields.fire, 3, 0_turns, false ) );
    for( int i = 0; i < 300; ++i ) {
        calendar::turn += 1_turns;
        here.process_fields();
    }

    fire_outcome out;
    for( int z = 0; z <= 1; ++z ) {
        for( const tripoint_bub_ms &flat : here.points_in_radius( center, 8 ) ) {
            const tripoint_bub_ms p( flat.xy(), center.z() + z );
            out.fire.push_back( here.get_field_intensity( p, fields.fire ) );
            out.smoke.push_back( here.get_field_intensity( p, fields.smoke ) );
            int hot = 0;
            for( const field_type_id &air : fields.hot_air ) {
                hot = hot * 4 + here.get_field_intensity( p, air );
            }
            out.hot_air.push_back( hot );
            out.furniture.push_back( here.furn( p ).id().str() );
            for( const item &it : here.i_at( p ) ) {
                if( it.typeId() == itype_ash ) {
                    out.ash += it.count();
                }
            }
        }
    }
    return out;
}

int count_items( const tripoint_bub_ms &p, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            ++count;
        }
    }
    return count;
}

// HP of every body part of the avatar
std::map<bodypart_id, int> part_hp( const Character &you )
{
    std::map<bodypart_id, int> hp;
    for( const bodypart_id &bp : you.get_all_body_parts() ) {
        hp[bp] = you.get_part_hp_cur( bp );
    }
    return hp;
}

// Puts the avatar at p with no fields around, with full HP and nothing worn
avatar &avatar_alone_at( const tripoint_bub_ms &p )
{
    map &here = get_map();
    clear_every_field();
    clear_avatar();
    avatar &u = get_avatar();
    u.setpos( here, p );
    return u;
}

// The avatar's HP after standing in a field three times, from a fixed seed
std::map<bodypart_id, int> hp_after_contact( const field_type_id &type, int intensity )
{
    map &here = get_map();
    const tripoint_bub_ms p( 60, 60, 0 );
    avatar &u = avatar_alone_at( p );
    REQUIRE( here.add_field( p, type, intensity, 0_turns, false ) );
    rng_set_engine_seed( 1234 );
    for( int i = 0; i < 3; ++i ) {
        here.creature_in_field( u );
    }
    return part_hp( u );
}

// A test zombie's HP lost to standing in a field three times, from a fixed seed
int monster_hp_lost_to_contact( const field_type_id &type, int intensity )
{
    map &here = get_map();
    const tripoint_bub_ms p( 60, 60, 0 );
    avatar_alone_at( p + tripoint_rel_ms( 10, 10, 0 ) );
    clear_creatures();
    monster &zombie = spawn_test_monster( "mon_test_zombie", p );
    const int full_hp = zombie.get_hp();
    REQUIRE( here.add_field( p, type, intensity, 0_turns, false ) );
    rng_set_engine_seed( 1234 );
    for( int i = 0; i < 3; ++i ) {
        here.creature_in_field( zombie );
    }
    return full_hp - zombie.get_hp();
}

const diag_value &tar_global( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    REQUIRE( value != nullptr );
    return *value;
}

} // namespace

// MAP-J02: fire's spreading, burning, smoke, ash, hot air and decay were C++ keyed to fd_fire.
// They are the "fire" and "fungicide" processors, "decay_mode": "fire" and per-intensity
// hot_air_field now. A test field picking them in JSON burns a hay patch exactly as fd_fire
// does from the same seed: the same tiles on fire, smoked and heated, the same ash.
TEST_CASE( "map-j02_a_json_fire_field_burns_and_spreads_like_fd_fire", "[audit][field]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    clear_map_without_vision( -2, 2 );
    clear_avatar();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    scoped_weather_override weather( weather_clear );
    weather.with_windspeed( 0 );
    map &here = get_map();
    const tripoint_bub_ms center( 90, 90, 0 );
    REQUIRE( here.inbounds( center + tripoint_rel_ms( 8, 8, 1 ) ) );

    const fire_outcome vanilla = run_fire( { fd_fire.id(), fd_smoke.id(),
        { fd_hot_air1.id(), fd_hot_air2.id(), fd_hot_air3.id() } }, center );
    const fire_outcome json = run_fire( { field_audit_high_fields_hellfire.id(),
                                          field_audit_high_fields_smoke.id(),
    { field_audit_high_fields_hot_air1.id(), field_audit_high_fields_hot_air2.id(), field_audit_high_fields_hot_air3.id() }
                                        }, center );
    clear_every_field();

    // The fire did something: it spread or left ash, and made hot air
    int burnt = 0;
    int hot = 0;
    for( size_t i = 0; i < vanilla.fire.size(); ++i ) {
        burnt += vanilla.fire[i] > 0 || vanilla.furniture[i] == furn_f_ash.str();
        hot += vanilla.hot_air[i] > 0;
    }
    CHECK( burnt > 1 );
    CHECK( hot > 0 );

    CHECK( json.fire == vanilla.fire );
    CHECK( json.smoke == vanilla.smoke );
    CHECK( json.hot_air == vanilla.hot_air );
    CHECK( json.furniture == vanilla.furniture );
    CHECK( json.ash == vanilla.ash );
}

// MAP-J02: the vanilla fields' processors are JSON now; finalize() resolves them, so field
// processing does no lookups by name.
TEST_CASE( "map-j02_vanilla_fields_pick_their_processors_in_json", "[audit][field]" )
{
    const auto types = []( const field_type_str_id & fd ) {
        std::vector<field_processor_type> out;
        for( const field_processor_def &def : fd->processor_defs ) {
            out.push_back( def.type );
        }
        return out;
    };
    CHECK( types( fd_fire ) == std::vector<field_processor_type> { field_processor_type::fire, field_processor_type::fungicide } );
    CHECK( types( fd_acid ) == std::vector<field_processor_type> { field_processor_type::fall } );
    CHECK( types( fd_electricity ) == std::vector<field_processor_type> { field_processor_type::arc } );
    CHECK( types( fd_electricity_unlit ) == std::vector<field_processor_type> { field_processor_type::arc } );
    CHECK( types( fd_fire_vent ) == std::vector<field_processor_type> { field_processor_type::vent_cycle } );
    CHECK( types( fd_last_known ) == std::vector<field_processor_type> { field_processor_type::last_known } );
    CHECK( fd_fire->decay_mode == field_decay_mode::fire );
    CHECK( fd_acid->decay_mode == field_decay_mode::exponential );
    CHECK( fd_fire->get_intensity_level( 2 ).hot_air_field == fd_hot_air3 );
    CHECK( fd_fire->heat_radiation( 2 ) == 2 );

    // Each processor JSON picked runs, after the ones other members imply (fd_fire has none)
    REQUIRE( fd_fire->get_processors().size() == 2 );
    const field_processor_def &fire = fd_fire->get_processors()[0].def;
    CHECK( fire.type == field_processor_type::fire );
    CHECK( fire.spread_chance == 25 );
    CHECK( fire.smoke_field_id == fd_smoke.id() );
    CHECK( fire.fuel_field_id == fd_web.id() );
    CHECK( fd_extinguisher->get_processors().back().def.field_ids ==
           std::vector<field_type_id> { fd_fire.id() } );
    // An arc with no field of its own spreads as itself
    CHECK( fd_electricity->get_processors().back().def.field_id == fd_electricity.id() );
    CHECK( fd_flame_burst->get_processors().back().def.field_id == fd_fire_vent.id() );

    // The opaque variant links both ways
    CHECK( fd_electricity->variant_for( false ) == fd_electricity_unlit.id() );
    CHECK( fd_electricity->variant_for( true ) == fd_electricity.id() );
    CHECK( fd_electricity_unlit->variant_for( true ) == fd_electricity.id() );
    CHECK( fd_electricity_unlit->variant_for( false ) == fd_electricity_unlit.id() );
    CHECK_FALSE( fd_fire->has_transparency_variants() );

    // "linear_half_life" is the older spelling of the linear decay mode
    const auto load = []( const std::string & json ) {
        JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        field_type ft;
        ft.load( jo, "" );
        return ft;
    };
    CHECK( load( R"({ "intensity_levels": [ { "name": "x" } ], "linear_half_life": true })" ).decay_mode
           == field_decay_mode::linear );
    CHECK( load( R"({ "intensity_levels": [ { "name": "x" } ], "decay_mode": "linear" })" ).decay_mode
           == field_decay_mode::linear );
    CHECK_THROWS_AS( load( R"({ "intensity_levels": [ { "name": "x" } ], "processors": [ "burn" ] })" ),
                     JsonError );
    CHECK_THROWS_AS( load(
                         R"({ "intensity_levels": [ { "name": "x" } ], "processors": [ "extinguish" ] })" ), JsonError );
    CHECK_THROWS_AS( load(
                         R"({ "intensity_levels": [ { "name": "x" } ], "contact_processors": [ "lava" ] })" ), JsonError );
    CHECK_THROWS_AS( load(
                         R"({ "intensity_levels": [ { "name": "x" } ], "linear_half_life": true, "decay_mode": "fire" })" ),
                     JsonError );
}

// MAP-J02: creature contact was a chain of id checks in player_in_field and monster_in_field.
// Each is a contact processor now, picked in JSON. The vanilla fields hurt as before: a small
// fire doesn't, a raging one burns everything but the head of someone standing in it, acid burns
// the feet, electricity the whole body. A test field reusing a processor hurts exactly as the
// vanilla field does from the same seed. Feet and hands have no HP of their own: what burns
// them comes off the leg or arm they belong to.
TEST_CASE( "map-j02_vanilla_contact_damage_is_unchanged_and_reusable", "[audit][field]" )
{
    clear_map_without_vision();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    const bodypart_id head( "head" );
    const bodypart_id torso( "torso" );
    const bodypart_id arm_l( "arm_l" );
    const bodypart_id leg_l( "leg_l" );
    const bodypart_id leg_r( "leg_r" );
    const std::map<bodypart_id, int> full = part_hp( avatar_alone_at( tripoint_bub_ms( 60, 60,
                                            0 ) ) );

    SECTION( "fire" ) {
        // A small fire never burns
        CHECK( hp_after_contact( fd_fire.id(), 1 ) == full );
        const std::map<bodypart_id, int> raging = hp_after_contact( fd_fire.id(), 3 );
        CHECK( raging.at( head ) == full.at( head ) );
        CHECK( raging.at( torso ) < full.at( torso ) );
        CHECK( raging.at( arm_l ) < full.at( arm_l ) );
        CHECK( raging.at( leg_l ) < full.at( leg_l ) );
        CHECK( raging.at( leg_r ) < full.at( leg_r ) );
        CHECK( hp_after_contact( field_audit_high_fields_hellfire.id(), 3 ) == raging );
        CHECK( hp_after_contact( field_audit_high_fields_hellfire.id(), 1 ) == full );

        const int zombie_burn = monster_hp_lost_to_contact( fd_fire.id(), 3 );
        CHECK( zombie_burn > 0 );
        CHECK( monster_hp_lost_to_contact( field_audit_high_fields_hellfire.id(), 3 ) == zombie_burn );
    }

    SECTION( "acid" ) {
        const std::map<bodypart_id, int> burnt = hp_after_contact( fd_acid.id(), 3 );
        CHECK( burnt.at( leg_l ) < full.at( leg_l ) );
        CHECK( burnt.at( leg_r ) < full.at( leg_r ) );
        CHECK( burnt.at( torso ) == full.at( torso ) );
        CHECK( burnt.at( head ) == full.at( head ) );
        CHECK( burnt.at( arm_l ) == full.at( arm_l ) );
    }

    SECTION( "electricity" ) {
        const std::map<bodypart_id, int> shocked = hp_after_contact( fd_electricity.id(), 4 );
        CHECK( shocked.at( torso ) < full.at( torso ) );
        CHECK( shocked.at( head ) < full.at( head ) );
        CHECK( hp_after_contact( field_audit_high_fields_sparks.id(), 4 ) == shocked );

        const int zombie_shock = monster_hp_lost_to_contact( fd_electricity.id(), 5 );
        CHECK( zombie_shock > 0 );
        CHECK( monster_hp_lost_to_contact( field_audit_high_fields_sparks.id(), 5 ) == zombie_shock );
    }
    clear_every_field();
    clear_creatures();
}

// MAP-J02: map::add_field turns fd_electricity into fd_electricity_unlit on a tile that isn't
// transparent (thick smoke, say), but contact damage checked only fd_electricity, so electricity
// in smoke never hurt anyone. The unlit field has the electricity contact processor now.
TEST_CASE( "map-j02_electricity_in_smoke_hurts", "[audit][field]" )
{
    clear_map_without_vision();
    clear_npcs();
    clear_vehicles();
    map &here = get_map();
    const tripoint_bub_ms p( 60, 60, 0 );
    avatar &u = avatar_alone_at( p );
    clear_creatures();
    const bodypart_id torso( "torso" );
    const int torso_hp = u.get_part_hp_cur( torso );

    // On a transparent tile the unlit field is placed as the lit one
    const tripoint_bub_ms clear_tile = p + tripoint_rel_ms( 5, 0, 0 );
    REQUIRE( here.add_field( clear_tile, fd_electricity_unlit, 3, 0_turns, false ) );
    CHECK( here.get_field( clear_tile, fd_electricity ) != nullptr );
    CHECK( here.get_field( clear_tile, fd_electricity_unlit ) == nullptr );

    const tripoint_bub_ms zombie_tile = p + tripoint_rel_ms( 0, 5, 0 );
    monster &zombie = spawn_test_monster( "mon_test_zombie", zombie_tile );
    const int zombie_hp = zombie.get_hp();
    for( const tripoint_bub_ms &smoky : {
             p, zombie_tile
         } ) {
        REQUIRE( here.add_field( smoky, fd_smoke, 3, 0_turns, false ) );
    }
    here.build_map_cache( p.z(), true );
    REQUIRE_FALSE( here.is_transparent( p ) );
    REQUIRE_FALSE( here.is_transparent( zombie_tile ) );

    for( const tripoint_bub_ms &smoky : {
             p, zombie_tile
         } ) {
        REQUIRE( here.add_field( smoky, fd_electricity, 8, 0_turns, false ) );
        CHECK( here.get_field( smoky, fd_electricity ) == nullptr );
        REQUIRE( here.get_field( smoky, fd_electricity_unlit ) != nullptr );
    }
    here.creature_in_field( u );
    CHECK( u.get_part_hp_cur( torso ) < torso_hp );
    here.creature_in_field( zombie );
    CHECK( zombie.get_hp() < zombie_hp );

    clear_every_field();
    clear_creatures();
}

// MAP-J02: has_nearby_fire (crafting and cooking) and the heat a fire radiates looked only for
// fd_fire. They take any field with has_fire whose intensity radiates heat ("heat_radiation",
// fd_fire's intensity), so a mod's fire counts; fd_fire_vent, which has has_fire but radiates
// nothing, still doesn't.
TEST_CASE( "map-j02_a_json_fire_is_a_usable_fire", "[audit][field]" )
{
    clear_map_without_vision();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    map &here = get_map();
    const tripoint_bub_ms p( 60, 60, 0 );
    avatar &u = avatar_alone_at( p );
    const tripoint_bub_ms next_to = p + tripoint_rel_ms::east;
    REQUIRE_FALSE( here.has_nearby_fire( p ) );

    REQUIRE( here.add_field( next_to, fd_fire_vent, 1, 0_turns, false ) );
    CHECK_FALSE( here.has_nearby_fire( p ) );
    here.remove_field( next_to, fd_fire_vent );

    REQUIRE( here.add_field( next_to, fd_fire, 2, 0_turns, false ) );
    CHECK( here.has_nearby_fire( p ) );
    const float vanilla_heat = units::to_kelvin_delta( get_heat_radiation( u.pos_bub() ) );
    CHECK( vanilla_heat > 0.0f );
    here.remove_field( next_to, fd_fire );
    CHECK_FALSE( here.has_nearby_fire( p ) );

    REQUIRE( here.add_field( next_to, field_audit_high_fields_hellfire, 2, 0_turns, false ) );
    CHECK( here.has_nearby_fire( p ) );
    CHECK( here.has_nearby_fire( next_to, 0 ) );
    CHECK( units::to_kelvin_delta( get_heat_radiation( u.pos_bub() ) ) == vanilla_heat );
    clear_every_field();
}

// MAP-J02: fd_push_items pushed only rocks, by id. The push_items processor takes the items
// to push; fd_push_items lists rocks.
TEST_CASE( "map-j02_push_items_pushes_the_items_it_lists", "[audit][field]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );
    clear_map_without_vision();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    map &here = get_map();
    avatar_alone_at( tripoint_bub_ms( 60, 60, 0 ) );
    const tripoint_bub_ms from( 30, 30, 0 );
    const tripoint_bub_ms to = from + tripoint_rel_ms::east;

    const auto push = [&]( const field_type_id & type ) {
        clear_every_field();
        clear_items( 0 );
        REQUIRE( here.add_field( from, type, 1, 1_turns, false ) );
        REQUIRE( here.add_field( to, type, 1, 1_turns, false ) );
        here.add_item_or_charges( from, item( itype_rock ) );
        here.add_item_or_charges( from, item( itype_test_2x4 ) );
        // Only items at least a turn old are pushed
        calendar::turn += 1_turns;
        here.process_fields();
    };

    push( fd_push_items.id() );
    CHECK( count_items( to, itype_rock ) == 1 );
    CHECK( count_items( from, itype_test_2x4 ) == 1 );

    push( field_audit_high_fields_conveyor.id() );
    CHECK( count_items( to, itype_test_2x4 ) == 1 );
    CHECK( count_items( from, itype_rock ) == 1 );
    clear_every_field();
    clear_items( 0 );
}

// MAP-J02: a field could only apply effects. Each intensity takes "contact_damage" (a damage
// type and amount on the parts touching the ground, all of them or a list, scaled for monsters)
// and an "eoc_on_contact" run with the creature as u and loc, field and intensity as context.
TEST_CASE( "map-j02_contact_damage_and_eoc_on_contact", "[audit][field][eoc]" )
{
    clear_map_without_vision();
    clear_npcs();
    clear_vehicles();
    map &here = get_map();
    const tripoint_bub_ms p( 60, 60, 0 );
    avatar &u = avatar_alone_at( p );
    clear_creatures();
    get_globals().clear_global_values();
    const bodypart_id torso( "torso" );
    const bodypart_id leg_l( "leg_l" );
    const bodypart_id head( "head" );
    const std::map<bodypart_id, int> full = part_hp( u );

    REQUIRE( here.add_field( p, field_audit_high_fields_tar, 1, 0_turns, false ) );
    here.creature_in_field( u );
    std::map<bodypart_id, int> hp = part_hp( u );
    // the ground-contact parts are the feet, whose damage comes off the legs
    CHECK( hp.at( leg_l ) < full.at( leg_l ) );
    CHECK( hp.at( torso ) == full.at( torso ) );
    CHECK( hp.at( head ) == full.at( head ) );
    CHECK( tar_global( "audit_high_fields_tar_count" ).dbl() == 1 );
    CHECK( tar_global( "audit_high_fields_tar_intensity" ).dbl() == 1 );
    CHECK( tar_global( "audit_high_fields_tar_monster" ).dbl() == 0 );
    CHECK( tar_global( "audit_high_fields_tar_loc" ).tripoint() == here.get_abs( p ) );
    CHECK( tar_global( "audit_high_fields_tar_field" ).str() == field_audit_high_fields_tar.str() );

    // The second level hits only the torso, and inherits the EOC
    here.set_field_intensity( p, field_audit_high_fields_tar, 2 );
    here.creature_in_field( u );
    const std::map<bodypart_id, int> boiled = part_hp( u );
    CHECK( boiled.at( torso ) < full.at( torso ) );
    CHECK( boiled.at( leg_l ) == hp.at( leg_l ) );
    CHECK( tar_global( "audit_high_fields_tar_count" ).dbl() == 2 );
    CHECK( tar_global( "audit_high_fields_tar_intensity" ).dbl() == 2 );

    // A monster takes the damage times monster_mult, and is u in the EOC
    const tripoint_bub_ms zombie_tile = p + tripoint_rel_ms( 0, 5, 0 );
    monster &zombie = spawn_test_monster( "mon_test_zombie", zombie_tile );
    const int zombie_hp = zombie.get_hp();
    REQUIRE( here.add_field( zombie_tile, field_audit_high_fields_tar, 1, 0_turns, false ) );
    here.creature_in_field( zombie );
    const int burnt_hp = zombie.get_hp();
    // 4 heat times a monster_mult of 2, less any heat armor
    CHECK( burnt_hp < zombie_hp );
    CHECK( burnt_hp >= zombie_hp - 8 );
    CHECK( tar_global( "audit_high_fields_tar_monster" ).dbl() == 1 );
    CHECK( tar_global( "audit_high_fields_tar_loc" ).tripoint() == here.get_abs( zombie_tile ) );
    // monster_mult 0: no damage to monsters, but the EOC still runs
    here.set_field_intensity( zombie_tile, field_audit_high_fields_tar, 2 );
    here.creature_in_field( zombie );
    CHECK( zombie.get_hp() == burnt_hp );
    CHECK( tar_global( "audit_high_fields_tar_count" ).dbl() == 4 );

    clear_every_field();
    clear_creatures();
    get_globals().clear_global_values();
}

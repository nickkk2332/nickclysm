#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "field_type.h"
#include "game.h"
#include "generic_factory.h"
#include "global_vars.h"
#include "item.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "options.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "sounds.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_type.h"

static const field_type_str_id field_test_fd_sound_damper( "test_fd_sound_damper" );

static const furn_str_id furn_test_f_sound_baffle( "test_f_sound_baffle" );

static const ter_str_id ter_t_brick_wall( "t_brick_wall" );
static const ter_str_id ter_t_dirt( "t_dirt" );
static const ter_str_id ter_t_door_c( "t_door_c" );
static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_window( "t_window" );
static const ter_str_id ter_test_t_sound_baffle( "test_t_sound_baffle" );
static const ter_str_id ter_test_t_soundproof_wall( "test_t_soundproof_wall" );

static const trap_str_id tr_dormant_corpse( "tr_dormant_corpse" );

static const vpart_id vpart_board( "board" );
static const vpart_id vpart_door( "door" );
static const vpart_id vpart_frame( "frame" );
static const vpart_id vpart_test_vp_soundproof_board( "test_vp_soundproof_board" );
static const vpart_id vpart_test_vp_soundproof_door( "test_vp_soundproof_door" );
static const vpart_id vpart_windshield( "windshield" );

static const vproto_id vehicle_prototype_none( "none" );

namespace
{

// A sound at source, heard 10 tiles east at listener; mid is halfway between.
const tripoint_bub_ms source( 60, 66, 0 );
const tripoint_bub_ms mid( 65, 66, 0 );
const tripoint_bub_ms listener( 70, 66, 0 );

// The vertical part of sound_distance before the external options, kept to compare with
int old_sound_distance( const tripoint_bub_ms &a, const tripoint_bub_ms &b )
{
    const int lower_z = std::min( a.z(), b.z() );
    const int upper_z = std::max( a.z(), b.z() );
    const int vertical_displacement = upper_z - lower_z;
    int vertical_attenuation = vertical_displacement;
    if( lower_z < 0 && vertical_displacement > 0 ) {
        vertical_attenuation += 4;
        const int underground_displacement = std::min( -lower_z, vertical_displacement );
        vertical_attenuation += ( underground_displacement - 1 ) * 20;
    }
    vertical_attenuation *= 5;
    return rl_dist( a.xy(), b.xy() ) + vertical_attenuation;
}

struct sound_scene {
    restore_on_out_of_scope<time_point> turn{ calendar::turn };
    scoped_weather_override weather{ WEATHER_CLEAR };

    sound_scene() {
        calendar::turn = calendar::turn_zero + 1_days + 12_hours;
        get_globals().clear_global_values();
        clear_avatar();
        clear_map();
        clear_creatures();
        clear_npcs();
        clear_vehicles();
        sounds::reset_sounds();
        Messages::clear_messages();
        // out of the way of the line, so it neither blocks nor makes the sounds
        get_avatar().setpos( get_map(), tripoint_bub_ms( 20, 20, 0 ) );
    }
    ~sound_scene() {
        sounds::reset_sounds();
        Messages::clear_messages();
        clear_creatures();
        clear_vehicles();
        // leave no muffling terrain, furniture or field for the next test
        clear_map();
    }
};

// What a test zombie at `at` makes of a shout of `volume` at source: the volume it
// follows (its wander urge), 0 if it heard nothing
int zombie_hears( const tripoint_bub_ms &at, int volume )
{
    clear_creatures();
    sounds::reset_sounds();
    monster &zed = spawn_test_monster( "mon_test_zombie", at );
    REQUIRE( zed.wandf == 0 );
    sounds::sound( source, volume, sounds::sound_t::speech, "a test shout" );
    sounds::process_sounds();
    const int heard = zed.wandf;
    clear_creatures();
    return heard;
}

bool has_message_containing( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

// Whether the avatar, standing at source, hears a bang of `volume` at listener
bool avatar_hears_bang( int volume )
{
    avatar &u = get_avatar();
    u.setpos( get_map(), source );
    sounds::reset_sounds();
    Messages::clear_messages();
    sounds::sound( listener, volume, sounds::sound_t::combat, "a test bang" );
    sounds::process_sound_markers( &u );
    const bool heard = has_message_containing( "a test bang" );
    Messages::clear_messages();
    return heard;
}

bool is_test_id( const std::string &id )
{
    return id.rfind( "test_", 0 ) == 0;
}

vehicle &place_hull( map &here, const vpart_id &hull )
{
    vehicle *veh = here.add_vehicle( vehicle_prototype_none, mid, 0_degrees, 0,
                                     veh_spawn_status::UNDAMAGED );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, vpart_frame ) != -1 );
    REQUIRE( veh->install_part( here, point_rel_ms::zero, hull ) != -1 );
    here.add_vehicle_to_cache( veh );
    REQUIRE( here.veh_at( mid ) );
    return *veh;
}

} // namespace

// SPAT-J01: sound_distance() was distance plus a hardcoded vertical penalty, so a zombie
// heard a shout through a bunker wall as well as across an open field.
TEST_CASE( "spat-j01_monster_hears_less_through_muffling_terrain", "[audit][sound][monster]" )
{
    sound_scene scene;
    map &here = get_map();
    const int volume = 40 - get_weather().weather_id->sound_attn;
    const int distance = rl_dist( source, listener );

    SECTION( "open ground: volume minus distance" ) {
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        CHECK( zombie_hears( listener, 40 ) == volume - distance );
    }
    SECTION( "a baffle floor between takes its sound_attenuation off" ) {
        here.ter_set( mid, ter_test_t_sound_baffle );
        CHECK( here.sound_attenuation_between( source, listener ) == 10 );
        CHECK( zombie_hears( listener, 40 ) == volume - distance - 10 );
    }
    SECTION( "furniture adds to the terrain under it" ) {
        here.ter_set( mid, ter_test_t_sound_baffle );
        here.furn_set( mid, furn_test_f_sound_baffle );
        CHECK( here.sound_attenuation_at( mid ) == 17 );
        CHECK( zombie_hears( listener, 40 ) == volume - distance - 17 );
    }
    SECTION( "a field muffles by its intensity" ) {
        here.add_field( mid, field_test_fd_sound_damper, 3 );
        CHECK( here.sound_attenuation_at( mid ) == 12 );
        CHECK( zombie_hears( listener, 40 ) == volume - distance - 12 );
    }
    SECTION( "a soundproof wall leaves nothing to hear" ) {
        here.ter_set( mid, ter_test_t_soundproof_wall );
        CHECK( here.sound_attenuation_between( source, listener ) == 50 );
        CHECK( zombie_hears( listener, 40 ) == 0 );
        // a louder sound gets through, muffled
        CHECK( zombie_hears( listener, 90 ) == 90 - get_weather().weather_id->sound_attn -
               distance - 50 );
    }
    SECTION( "two walls add up" ) {
        here.ter_set( mid, ter_test_t_soundproof_wall );
        here.ter_set( mid + point::east, ter_test_t_soundproof_wall );
        CHECK( here.sound_attenuation_between( source, listener ) == 100 );
        // a caller that only needs to know it reaches 30 stops after the first wall
        CHECK( here.sound_attenuation_between( source, listener, 30 ) == 50 );
    }
    SECTION( "the sound's own tile and the listener's don't count" ) {
        here.ter_set( mid, ter_test_t_soundproof_wall );
        CHECK( here.sound_attenuation_between( mid, listener ) == 0 );
        CHECK( here.sound_attenuation_between( source, mid ) == 0 );
        CHECK( here.sound_attenuation_between( mid + point::west, mid + point::east ) == 50 );
    }
}

TEST_CASE( "spat-j01_soundproof_wall_shields_a_sound_trap", "[audit][sound][trap]" )
{
    sound_scene scene;
    map &here = get_map();
    const tripoint_bub_ms corpse = source + point( 6, 0 );
    // 40 at 6 tiles is past the trap's top threshold (20) in the open: certain to go off
    SECTION( "open ground" ) {
        here.trap_set( corpse, tr_dormant_corpse.id() );
        sounds::sound( source, 40, sounds::sound_t::combat, "a gunshot" );
        sounds::process_sounds();
        CHECK( here.tr_at( corpse ).is_null() );
    }
    SECTION( "behind a soundproof wall" ) {
        here.ter_set( source + point( 3, 0 ), ter_test_t_soundproof_wall );
        here.trap_set( corpse, tr_dormant_corpse.id() );
        sounds::sound( source, 40, sounds::sound_t::combat, "a gunshot" );
        sounds::process_sounds();
        CHECK( here.tr_at( corpse ).id == tr_dormant_corpse );
    }
}

// The avatar's hearing (messages, markers, waking up) and can_hear() checks go through
// the same muffling.
TEST_CASE( "spat-j01_avatar_hears_less_through_muffling_terrain", "[audit][sound][avatar]" )
{
    sound_scene scene;
    map &here = get_map();
    avatar &u = get_avatar();
    u.setpos( here, source );

    SECTION( "open ground" ) {
        CHECK( avatar_hears_bang( 25 ) );
        CHECK( u.can_hear( listener, 25 ) );
    }
    SECTION( "through a baffle floor, still heard" ) {
        here.ter_set( mid, ter_test_t_sound_baffle );
        CHECK( avatar_hears_bang( 25 ) );
        CHECK( u.can_hear( listener, 25 ) );
        CHECK_FALSE( u.can_hear( listener, 15 ) );
    }
    SECTION( "through a soundproof wall, not heard" ) {
        here.ter_set( mid, ter_test_t_soundproof_wall );
        CHECK_FALSE( avatar_hears_bang( 25 ) );
        CHECK_FALSE( u.can_hear( listener, 25 ) );
        CHECK( avatar_hears_bang( 80 ) );
    }
    SECTION( "sound effects play quieter" ) {
        const int open = sfx::get_heard_volume( listener );
        here.ter_set( mid, ter_test_t_sound_baffle );
        CHECK( sfx::get_heard_volume( listener ) < open );
    }
}

TEST_CASE( "spat-j01_sound_cache_follows_terrain_changes", "[audit][sound][map]" )
{
    sound_scene scene;
    map &here = get_map();
    here.ter_set( mid, ter_test_t_soundproof_wall );
    REQUIRE( here.sound_attenuation_between( source, listener ) == 50 );
    REQUIRE( here.sound_muffled_on_level( 0 ) );

    SECTION( "ter_set removes the wall" ) {
        here.ter_set( mid, ter_t_grass );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        CHECK_FALSE( here.sound_muffled_on_level( 0 ) );
        here.ter_set( mid, ter_test_t_soundproof_wall );
        CHECK( here.sound_attenuation_between( source, listener ) == 50 );
    }
    SECTION( "bashing breaks the wall" ) {
        here.bash( mid, 1000 );
        REQUIRE( here.ter( mid ) == ter_t_dirt );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        CHECK_FALSE( here.sound_muffled_on_level( 0 ) );
    }
    SECTION( "furniture comes and goes" ) {
        const tripoint_bub_ms other = mid + point::east;
        here.furn_set( other, furn_test_f_sound_baffle );
        CHECK( here.sound_attenuation_between( source, listener ) == 57 );
        here.furn_set( other, furn_str_id::NULL_ID() );
        CHECK( here.sound_attenuation_between( source, listener ) == 50 );
    }
    SECTION( "a field grows and goes" ) {
        const tripoint_bub_ms other = mid + point::east;
        here.add_field( other, field_test_fd_sound_damper, 1 );
        CHECK( here.sound_attenuation_between( source, listener ) == 53 );
        here.set_field_intensity( other, field_test_fd_sound_damper, 2 );
        CHECK( here.sound_attenuation_between( source, listener ) == 56 );
        here.remove_field( other, field_test_fd_sound_damper );
        CHECK( here.sound_attenuation_between( source, listener ) == 50 );
    }
    SECTION( "a rebuilt level gives the same answer" ) {
        here.invalidate_map_cache( 0 );
        CHECK( here.sound_attenuation_between( source, listener ) == 50 );
    }
}

TEST_CASE( "spat-j01_vehicle_hull_muffles_sound", "[audit][sound][vehicle]" )
{
    sound_scene scene;
    map &here = get_map();

    SECTION( "a board" ) {
        vehicle &veh = place_hull( here, vpart_test_vp_soundproof_board );
        CHECK( here.sound_attenuation_between( source, listener ) == 30 );
        CHECK( zombie_hears( listener, 60 ) == 60 - get_weather().weather_id->sound_attn - 10 - 30 );
        // a broken board muffles nothing
        const int board = veh.part_with_feature( point_rel_ms::zero, VPFLAG_OPAQUE, false );
        REQUIRE( board >= 0 );
        item broken = veh.part( board ).get_base();
        broken.set_damage( broken.max_damage() );
        veh.part( board ).set_base( std::move( broken ) );
        REQUIRE( veh.part( board ).is_broken() );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
    }
    SECTION( "a door, only while closed" ) {
        vehicle &veh = place_hull( here, vpart_test_vp_soundproof_door );
        const int door = veh.part_with_feature( point_rel_ms::zero, VPFLAG_OPENABLE, true );
        REQUIRE( door >= 0 );
        CHECK( here.sound_attenuation_between( source, listener ) == 30 );
        veh.open( here, door );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        veh.close( here, door );
        CHECK( here.sound_attenuation_between( source, listener ) == 30 );
    }
}

// Shipped data sets no sound_attenuation, so every sound reaches exactly who it reached
// before: the same volume through vanilla walls, doors and vehicles as across open ground.
TEST_CASE( "spat-j01_vanilla_content_muffles_nothing", "[audit][sound]" )
{
    for( const ter_t &ter : mapdata::terrain_data.get_all() ) {
        if( ter.sound_attenuation != 0 ) {
            CHECK( is_test_id( ter.id.str() ) );
        }
    }
    for( const furn_t &furn : mapdata::furniture_data.get_all() ) {
        if( furn.sound_attenuation != 0 ) {
            CHECK( is_test_id( furn.id.str() ) );
        }
    }
    for( const field_type &fd : field_types::get_all() ) {
        if( fd.muffles_sound ) {
            CHECK( is_test_id( fd.id.str() ) );
        }
    }
    for( const vpart_info &vpi : vehicles::parts::get_all() ) {
        if( vpi.sound_attenuation != 0 ) {
            CHECK( is_test_id( vpi.id.str() ) );
        }
    }

    sound_scene scene;
    map &here = get_map();
    const int volume = 40 - get_weather().weather_id->sound_attn;
    const int expected = volume - old_sound_distance( source, listener );

    SECTION( "a brick wall" ) {
        here.ter_set( mid, ter_t_brick_wall );
        CHECK_FALSE( here.sound_muffled_on_level( 0 ) );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        CHECK( zombie_hears( listener, 40 ) == expected );
    }
    SECTION( "a closed door" ) {
        here.ter_set( mid, ter_t_door_c );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        CHECK( zombie_hears( listener, 40 ) == expected );
    }
    SECTION( "a closed window" ) {
        here.ter_set( mid, ter_t_window );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        CHECK( zombie_hears( listener, 40 ) == expected );
    }
    // the vehicle parts are walked here, since TEST_DATA has parts that muffle
    for( const vpart_id &hull : {
             vpart_board, vpart_door, vpart_windshield
         } ) {
        SECTION( "a vehicle " + hull.str() ) {
            place_hull( here, hull );
            CHECK( here.sound_attenuation_between( source, listener ) == 0 );
            CHECK( zombie_hears( listener, 40 ) == expected );
        }
    }
}

namespace
{
// override_option restores an option from its text value, which external options don't have,
// so it would leave them at 0 for every test after; this keeps the number itself.
class override_external_int_option
{
    public:
        override_external_int_option( const std::string &name, const int value ) : name( name ),
            old_value( get_option<int>( name ) ) {
            set( value );
        }
        override_external_int_option( const override_external_int_option & ) = delete;
        override_external_int_option &operator=( const override_external_int_option & ) = delete;
        ~override_external_int_option() {
            set( old_value );
        }
    private:
        void set( const int value ) const {
            get_options().get_option( name ).setValue( value );
            options_manager::update_options_cache();
        }
        std::string name;
        int old_value;
};
} // namespace

TEST_CASE( "spat-j01_vertical_sound_options_default_to_the_old_constants", "[audit][sound]" )
{
    CHECK( get_option<int>( "SOUND_Z_LEVEL_MULT" ) == 5 );
    CHECK( get_option<int>( "SOUND_UNDERGROUND_MULT" ) == 5 );
    CHECK( get_option<int>( "SOUND_DEEP_UNDERGROUND_MULT" ) == 21 );
    for( int za = -OVERMAP_DEPTH; za <= OVERMAP_HEIGHT; ++za ) {
        for( int zb = -OVERMAP_DEPTH; zb <= OVERMAP_HEIGHT; ++zb ) {
            const tripoint_bub_ms a( 60, 66, za );
            const tripoint_bub_ms b( 63, 70, zb );
            CAPTURE( za, zb );
            CHECK( sounds::sound_distance( a, b ) == old_sound_distance( a, b ) );
        }
    }

    const tripoint_bub_ms ground( 60, 66, 0 );
    SECTION( "SOUND_Z_LEVEL_MULT scales every level" ) {
        override_external_int_option opt( "SOUND_Z_LEVEL_MULT", 10 );
        CHECK( sounds::sound_distance( ground, ground + tripoint::above ) == 10 );
        CHECK( sounds::sound_distance( ground, ground + tripoint::below ) == 50 );
    }
    SECTION( "the underground multipliers at 1 make underground like the surface" ) {
        override_external_int_option shallow( "SOUND_UNDERGROUND_MULT", 1 );
        override_external_int_option deep( "SOUND_DEEP_UNDERGROUND_MULT", 1 );
        CHECK( sounds::sound_distance( ground, ground + tripoint( 0, 0, -3 ) ) == 15 );
        CHECK( sounds::sound_distance( ground, ground + tripoint( 0, 0, 3 ) ) == 15 );
    }
}

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "butchery.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "enum_conversions.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "harvest.h"
#include "iexamine.h"
#include "iexamine_actors.h"
#include "item.h"
#include "itype.h"
#include "iuse_actor.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapdata.h"
#include "messages.h"
#include "mtype.h"
#include "output.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "skill.h"
#include "string_formatter.h"
#include "type_id.h"
#include "units.h"

static const furn_str_id furn_f_arcfurnace_empty( "f_arcfurnace_empty" );
static const furn_str_id furn_f_arcfurnace_full( "f_arcfurnace_full" );
static const furn_str_id furn_f_autoclave( "f_autoclave" );
static const furn_str_id furn_f_autoclave_full( "f_autoclave_full" );
static const furn_str_id furn_f_kiln_empty( "f_kiln_empty" );
static const furn_str_id furn_f_kiln_full( "f_kiln_full" );
static const furn_str_id furn_f_kiln_metal_empty( "f_kiln_metal_empty" );
static const furn_str_id furn_f_kiln_metal_full( "f_kiln_metal_full" );
static const furn_str_id furn_f_kiln_portable_empty( "f_kiln_portable_empty" );
static const furn_str_id furn_f_kiln_portable_full( "f_kiln_portable_full" );
static const furn_str_id furn_f_smoking_rack( "f_smoking_rack" );
static const furn_str_id furn_f_stook_empty( "f_stook_empty" );
static const furn_str_id furn_f_stook_full( "f_stook_full" );
static const furn_str_id furn_f_test_audit_high_dryer( "f_test_audit_high_dryer" );
static const furn_str_id furn_f_test_audit_high_dryer_full( "f_test_audit_high_dryer_full" );

static const harvest_drop_type_id harvest_drop_blood( "blood" );
static const harvest_drop_type_id harvest_drop_bone( "bone" );
static const harvest_drop_type_id harvest_drop_flesh( "flesh" );
static const harvest_drop_type_id harvest_drop_mutagen( "mutagen" );
static const harvest_drop_type_id harvest_drop_offal( "offal" );
static const harvest_drop_type_id harvest_drop_skin( "skin" );
static const harvest_drop_type_id harvest_drop_test_audit_high_fat( "test_audit_high_fat" );
static const harvest_drop_type_id harvest_drop_test_audit_high_plain( "test_audit_high_plain" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_bio_power_storage( "bio_power_storage" );
static const itype_id itype_bone( "bone" );
static const itype_id itype_charcoal( "charcoal" );
static const itype_id itype_chem_carbide( "chem_carbide" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_splinter( "splinter" );
static const itype_id itype_stick( "stick" );
static const itype_id itype_unfinished_charcoal( "unfinished_charcoal" );
static const itype_id itype_wheat_stalks( "wheat_stalks" );

static const mtype_id mon_chicken( "mon_chicken" );
static const mtype_id mon_deer( "mon_deer" );
static const mtype_id mon_test_audit_high_fatty( "mon_test_audit_high_fatty" );
static const mtype_id mon_test_bovine( "mon_test_bovine" );
static const mtype_id mon_zombie( "mon_zombie" );

static const proficiency_id proficiency_prof_butchering_adv( "prof_butchering_adv" );
static const proficiency_id proficiency_prof_butchering_basic( "prof_butchering_basic" );
static const proficiency_id proficiency_prof_skinning_adv( "prof_skinning_adv" );
static const proficiency_id proficiency_prof_skinning_basic( "prof_skinning_basic" );

static const skill_id skill_fabrication( "fabrication" );

static const ter_str_id ter_t_thconc_floor( "t_thconc_floor" );

static constexpr tripoint_bub_ms process_pos( 60, 60, 0 );

namespace
{

// What a process left on its tile, and what it said.
struct tile_state {
    bool result = false;
    furn_id furn;
    std::vector<std::string> items;
    std::vector<std::string> messages;

    bool operator==( const tile_state &rhs ) const {
        return result == rhs.result && furn == rhs.furn && items == rhs.items &&
               messages == rhs.messages;
    }
};

std::string describe( const tile_state &st )
{
    std::string ret = string_format( "result %d, furniture %s, items:", st.result ? 1 : 0,
                                     st.furn.id().str() );
    for( const std::string &it : st.items ) {
        ret += " [" + it + "]";
    }
    ret += ", messages:";
    for( const std::string &msg : st.messages ) {
        ret += " \"" + msg + "\"";
    }
    return ret;
}

std::vector<std::string> take_messages()
{
    std::vector<std::string> ret;
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        ret.push_back( msg.second );
    }
    Messages::clear_messages();
    return ret;
}

tile_state tile_at( bool result )
{
    map &here = get_map();
    tile_state st;
    st.result = result;
    st.furn = here.furn( process_pos );
    for( const item &it : here.i_at( process_pos ) ) {
        std::string flags;
        for( const flag_id &f : {
                 flag_NO_STERILE, flag_NO_PACKED, flag_FILTHY
             } ) {
            if( it.has_flag( f ) ) {
                flags += " " + f.str();
            }
        }
        st.items.push_back( string_format( "%s x%d born %d%s", it.typeId().str(), it.charges,
                                           to_turns<int>( it.birthday() - calendar::turn_zero ), flags ) );
    }
    st.messages = take_messages();
    return st;
}

Character &setup_process_test()
{
    clear_map();
    clear_avatar();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    get_globals().clear_global_values();
    set_time( calendar::turn_zero + 30_days + 12_hours );
    Character &you = get_player_character();
    you.setpos( get_map(), process_pos + point::west );
    Messages::clear_messages();
    return you;
}

// Puts `items` (made `age` ago) on the tile of furniture `furn`.
void fill_tile( const furn_str_id &furn, const std::vector<item> &items )
{
    map &here = get_map();
    here.i_clear( process_pos );
    here.furn_set( process_pos, furn );
    for( const item &it : items ) {
        here.add_item( process_pos, it );
    }
    Messages::clear_messages();
}

item made_ago( const itype_id &type, const time_duration &age, int charges = -1 )
{
    item it( type, calendar::turn - age );
    if( charges > 0 ) {
        it.charges = charges;
    }
    return it;
}

void examine_tile( Character &you )
{
    get_map().furn( process_pos ).obj().examine( you, process_pos );
}

const process_start_examine_actor &start_actor( const furn_str_id &furn )
{
    for( const cata::clone_ptr<iexamine_actor> &act : furn.obj().get_examine_actors() ) {
        if( const process_start_examine_actor *process =
                dynamic_cast<const process_start_examine_actor *>( act.get() ) ) {
            return *process;
        }
    }
    FAIL( "no process_start on " << furn.str() );
    static const process_start_examine_actor none;
    return none;
}

bool has_message_containing( const std::vector<std::string> &messages, const std::string &text )
{
    for( const std::string &msg : messages ) {
        if( msg.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

} // namespace

// The kilns used to be hardcoded; their process_start must do what kiln_fire (still there for
// mods) does, for the same contents and skill. The fabrication skill has progress towards the
// next level, which the old code counted.
TEST_CASE( "terr-j02_kiln_start_matches_old_code", "[audit][examine]" )
{
    Character &you = setup_process_test();
    you.set_skill_level( skill_fabrication, 3 );
    you.get_skill_level_object( skill_fabrication ).set_exercise( 30 );
    REQUIRE( you.get_skill_level( skill_fabrication ) == Approx( 3.3 ) );

    const std::vector<furn_str_id> kilns = { furn_f_kiln_empty, furn_f_kiln_metal_empty, furn_f_kiln_portable_empty };
    const std::vector<std::vector<itype_id>> fills = {
        { itype_2x4, itype_2x4, itype_stick, itype_splinter },
        { itype_bone, itype_2x4 },
        { },
        { itype_2x4, itype_rock },
        { itype_charcoal, itype_2x4 },
        { itype_2x4, itype_charcoal },
    };
    for( const furn_str_id &kiln : kilns ) {
        for( const std::vector<itype_id> &fill : fills ) {
            std::vector<item> items;
            for( const itype_id &type : fill ) {
                items.push_back( made_ago( type, 2_days ) );
            }
            fill_tile( kiln, items );
            const bool old_result = iexamine::kiln_fire( you, process_pos );
            const tile_state old_state = tile_at( old_result );

            fill_tile( kiln, items );
            const bool new_result = start_actor( kiln ).fire( you, process_pos );
            const tile_state new_state = tile_at( new_result );
            CAPTURE( kiln.str(), describe( old_state ), describe( new_state ) );
            CHECK( old_state == new_state );
        }
    }
    // the first fill fired
    fill_tile( furn_f_kiln_empty, { made_ago( itype_2x4, 1_days ) } );
    REQUIRE( start_actor( furn_f_kiln_empty ).fire( you, process_pos ) );
    CHECK( get_map().furn( process_pos ) == furn_f_kiln_full );
}

TEST_CASE( "terr-j02_kiln_and_arc_furnace_finish_match_old_code", "[audit][examine]" )
{
    Character &you = setup_process_test();
    struct finish_case {
        furn_str_id full;
        itype_id content;
        int charges;
        void ( *old_finish )( Character &, const tripoint_bub_ms & );
    };
    const std::vector<finish_case> processes = {
        { furn_f_kiln_full, itype_unfinished_charcoal, 37, &iexamine::kiln_full },
        { furn_f_kiln_metal_full, itype_unfinished_charcoal, 37, &iexamine::kiln_full },
        { furn_f_kiln_portable_full, itype_unfinished_charcoal, 37, &iexamine::kiln_full },
        { furn_f_kiln_full, itype_charcoal, 37, &iexamine::kiln_full },
        { furn_f_arcfurnace_full, itype_chem_carbide, -1, &iexamine::arcfurnace_full },
    };
    const std::vector<time_duration> ages = { 0_turns, 1_hours, 90_minutes, 100_minutes, 115_minutes, 2_hours, 23_hours, 23_hours + 40_minutes, 23_hours + 55_minutes, 24_hours, 30_hours };
    for( const finish_case &proc : processes ) {
        for( const time_duration &age : ages ) {
            const std::vector<item> items = { made_ago( proc.content, age, proc.charges ) };
            fill_tile( proc.full, items );
            proc.old_finish( you, process_pos );
            const tile_state old_state = tile_at( false );

            fill_tile( proc.full, items );
            examine_tile( you );
            const tile_state new_state = tile_at( false );
            CAPTURE( proc.full.str(), to_string( age ), describe( old_state ), describe( new_state ) );
            CHECK( old_state == new_state );
        }
        // empty
        fill_tile( proc.full, {} );
        proc.old_finish( you, process_pos );
        const tile_state old_state = tile_at( false );
        fill_tile( proc.full, {} );
        examine_tile( you );
        const tile_state new_state = tile_at( false );
        CAPTURE( proc.full.str(), describe( old_state ), describe( new_state ) );
        CHECK( old_state == new_state );
    }
    // a finished arc furnace turns into the appliance-making furniture, as before
    fill_tile( furn_f_arcfurnace_full, { made_ago( itype_chem_carbide, 3_hours ) } );
    examine_tile( you );
    CHECK( get_map().furn( process_pos ) == furn_f_arcfurnace_empty );
}

TEST_CASE( "terr-j02_stook_matches_old_code", "[audit][examine]" )
{
    Character &you = setup_process_test();
    map &here = get_map();
    here.set_outside_cache_dirty( process_pos.z() );
    here.build_outside_cache( process_pos.z() );
    REQUIRE( here.is_outside( process_pos ) );

    SECTION( "drying" ) {
        for( const time_duration &age : {
                 10_hours, 335_hours, 336_hours, 500_hours
             } ) {
            const std::vector<item> items = { made_ago( itype_wheat_stalks, age ) };
            fill_tile( furn_f_stook_full, items );
            iexamine::stook_full( you, process_pos );
            const tile_state old_state = tile_at( false );

            fill_tile( furn_f_stook_full, items );
            examine_tile( you );
            const tile_state new_state = tile_at( false );
            CAPTURE( to_string( age ), describe( old_state ), describe( new_state ) );
            CHECK( old_state == new_state );
        }
    }
    SECTION( "setting up, up to the question" ) {
        const std::vector<std::vector<item>> fills = {
            { made_ago( itype_wheat_stalks, 1_hours ) },
            { made_ago( itype_wheat_stalks, 1_hours ), made_ago( itype_rock, 1_hours ) },
            { },
        };
        for( const std::vector<item> &items : fills ) {
            // query_yn answers no in tests
            fill_tile( furn_f_stook_empty, items );
            iexamine::stook_empty( you, process_pos );
            const tile_state old_state = tile_at( false );

            fill_tile( furn_f_stook_empty, items );
            examine_tile( you );
            const tile_state new_state = tile_at( false );
            CAPTURE( describe( old_state ), describe( new_state ) );
            CHECK( old_state == new_state );
        }
    }
    SECTION( "setting up indoors" ) {
        REQUIRE( here.ter_set( process_pos, ter_t_thconc_floor ) );
        here.set_outside_cache_dirty( process_pos.z() );
        here.build_outside_cache( process_pos.z() );
        REQUIRE_FALSE( here.is_outside( process_pos ) );
        const std::vector<item> items = { made_ago( itype_wheat_stalks, 1_hours ) };
        fill_tile( furn_f_stook_empty, items );
        iexamine::stook_empty( you, process_pos );
        const tile_state old_state = tile_at( false );

        fill_tile( furn_f_stook_empty, items );
        examine_tile( you );
        const tile_state new_state = tile_at( false );
        CAPTURE( describe( old_state ), describe( new_state ) );
        CHECK( old_state == new_state );
        CHECK( has_message_containing( new_state.messages, "set up outside" ) );
    }
    SECTION( "setting up, answered yes" ) {
        const item grain = made_ago( itype_wheat_stalks, 5_hours );
        fill_tile( furn_f_stook_empty, { grain } );
        REQUIRE( start_actor( furn_f_stook_empty ).fire( you, process_pos ) );
        const tile_state st = tile_at( true );
        CHECK( st.furn == furn_f_stook_full );
        CHECK( st.messages == std::vector<std::string> { "You set up the stook and leave it to dry." } );
        // the grain dries from its harvest, as before
        REQUIRE( get_map().i_at( process_pos ).size() == 1 );
        CHECK( get_map().i_at( process_pos ).begin()->birthday() == grain.birthday() );
    }
}

TEST_CASE( "terr-j02_autoclave_matches_old_code", "[audit][examine]" )
{
    Character &you = setup_process_test();
    item packed( itype_bio_power_storage, calendar::turn );
    packed.set_flag( flag_NO_STERILE );
    item unpacked = packed;
    unpacked.set_flag( flag_NO_PACKED );
    item filthy = unpacked;
    filthy.set_flag( flag_FILTHY );

    SECTION( "running" ) {
        const std::vector<std::vector<item>> fills = {
            { packed },
            { unpacked },
            { packed, unpacked },
            { packed, made_ago( itype_2x4, 1_hours ) },
            { },
        };
        for( const std::vector<item> &contents : fills ) {
            for( const time_duration &age : {
                     10_minutes, 89_minutes, 2_hours
                 } ) {
                std::vector<item> items;
                for( item it : contents ) {
                    it.set_birthday( calendar::turn - age );
                    items.push_back( it );
                }
                fill_tile( furn_f_autoclave_full, items );
                iexamine::autoclave_full( you, process_pos );
                const tile_state old_state = tile_at( false );

                fill_tile( furn_f_autoclave_full, items );
                examine_tile( you );
                const tile_state new_state = tile_at( false );
                CAPTURE( to_string( age ), describe( old_state ), describe( new_state ) );
                CHECK( old_state == new_state );
            }
        }
    }
    SECTION( "starting, up to the question" ) {
        const std::vector<std::vector<item>> fills = {
            { },
            { packed, made_ago( itype_2x4, 1_hours ) },
            { filthy },
        };
        for( const std::vector<item> &items : fills ) {
            fill_tile( furn_f_autoclave, items );
            iexamine::autoclave_empty( you, process_pos );
            const tile_state old_state = tile_at( false );

            fill_tile( furn_f_autoclave, items );
            examine_tile( you );
            const tile_state new_state = tile_at( false );
            CAPTURE( describe( old_state ), describe( new_state ) );
            CHECK( old_state == new_state );
        }
    }
    SECTION( "one filthy CBM among clean ones stops it" ) {
        // The old code only refused when every CBM was filthy, then marked the filthy one sterile.
        fill_tile( furn_f_autoclave, { packed, filthy } );
        examine_tile( you );
        const tile_state st = tile_at( false );
        CHECK( st.furn == furn_f_autoclave );
        CHECK( has_message_containing( st.messages, "Some of those CBMs are filthy" ) );
    }
}

TEST_CASE( "terr-j02_firestarters_light_process_furniture", "[audit][examine]" )
{
    using start_type = firestarter_actor::start_type;
    CHECK( firestarter_actor::furniture_start_type( furn_f_kiln_empty ) == start_type::PROCESS );
    CHECK( firestarter_actor::furniture_start_type( furn_f_kiln_metal_empty ) == start_type::PROCESS );
    CHECK( firestarter_actor::furniture_start_type( furn_f_kiln_portable_empty ) ==
           start_type::PROCESS );
    CHECK( firestarter_actor::furniture_start_type( furn_f_kiln_full ) == start_type::FIRE );
    CHECK( firestarter_actor::furniture_start_type( furn_f_smoking_rack ) == start_type::SMOKER );
    CHECK( firestarter_actor::furniture_start_type( furn_f_test_audit_high_dryer ) ==
           start_type::FIRE );
    CHECK( process_start_examine_actor::firestarter_process( furn_f_stook_empty.obj() ) == nullptr );
}

TEST_CASE( "terr-j02_process_furniture_from_json_only", "[audit][examine]" )
{
    Character &you = setup_process_test();
    map &here = get_map();
    on_out_of_scope clear_globals( []() {
        get_globals().clear_global_values();
    } );
    global_variables &globals = get_globals();

    SECTION( "refusals" ) {
        fill_tile( furn_f_test_audit_high_dryer, { made_ago( itype_rock, 1_hours ) } );
        examine_tile( you );
        tile_state st = tile_at( false );
        CHECK( st.furn == furn_f_test_audit_high_dryer );
        CHECK( has_message_containing( st.messages, "The test dryer won't take" ) );

        fill_tile( furn_f_test_audit_high_dryer, { made_ago( itype_charcoal, 1_hours, 5 ), made_ago( itype_stick, 1_hours ) } );
        examine_tile( you );
        st = tile_at( false );
        CHECK( has_message_containing( st.messages, "The test dryer is full of" ) );

        fill_tile( furn_f_test_audit_high_dryer, {} );
        examine_tile( you );
        CHECK( tile_at( false ).messages == std::vector<std::string> { "The test dryer is empty." } );

        // under 2 L the yield is 0 %
        fill_tile( furn_f_test_audit_high_dryer, { made_ago( itype_splinter, 1_hours ) } );
        examine_tile( you );
        st = tile_at( false );
        CHECK( st.furn == furn_f_test_audit_high_dryer );
        CHECK( st.messages == std::vector<std::string> { "Too little for the test dryer." } );
        CHECK( globals.maybe_get_global_value( "test_audit_high_dryer_started" ) == nullptr );
    }
    SECTION( "a whole batch" ) {
        const std::vector<item> items = { made_ago( itype_stick, 1_hours ), made_ago( itype_2x4, 1_hours ) };
        units::volume total = 0_ml;
        for( const item &it : items ) {
            total += it.volume();
        }
        REQUIRE( total >= 2_liter );
        const int charges = item::find_type( itype_unfinished_charcoal )->charges_per_volume(
                                50 * total / 100 );
        REQUIRE( charges > 0 );

        fill_tile( furn_f_test_audit_high_dryer, items );
        examine_tile( you );
        tile_state st = tile_at( false );
        CHECK( st.furn == furn_f_test_audit_high_dryer_full );
        CHECK( st.messages == std::vector<std::string> { "The test dryer starts." } );
        REQUIRE( here.i_at( process_pos ).size() == 1 );
        const item &batch = *here.i_at( process_pos ).begin();
        CHECK( batch.typeId() == itype_unfinished_charcoal );
        CHECK( batch.charges == charges );
        CHECK( globals.get_global_value( "test_audit_high_dryer_started" ) == 1 );

        // 3 hours, from math
        examine_tile( you );
        st = tile_at( false );
        CHECK( st.furn == furn_f_test_audit_high_dryer_full );
        CHECK( st.messages == std::vector<std::string> { "The test dryer hums.", "The test dryer needs 3 hours more." } );
        CHECK( globals.maybe_get_global_value( "test_audit_high_dryer_done" ) == nullptr );

        const units::volume batch_volume = batch.volume();
        here.i_at( process_pos ).begin()->set_birthday( calendar::turn - 3_hours );
        examine_tile( you );
        st = tile_at( false );
        CHECK( st.furn == furn_f_test_audit_high_dryer );
        const int made = item::find_type( itype_charcoal )->charges_per_volume( batch_volume );
        CHECK( st.messages == std::vector<std::string> { "The test dryer hums.", string_format( "The test dryer made %d charcoal.", made ) } );
        REQUIRE( here.i_at( process_pos ).size() == 1 );
        CHECK( here.i_at( process_pos ).begin()->typeId() == itype_charcoal );
        CHECK( globals.get_global_value( "test_audit_high_dryer_done" ) == 1 );
    }
}

namespace
{

// The butchery rules as they were before they moved to harvest_drop_type JSON, keyed on the five
// drop type ids. These are the expected values.
int old_corpse_damage_effect( int weight, const std::string &entry_type, int damage_level )
{
    const float slight_damage = 0.9f;
    const float damage = 0.75f;
    const float high_damage = 0.5f;
    const int destroyed = 0;

    switch( damage_level ) {
        case 2:
            if( entry_type == "offal" ) {
                return std::round( weight * damage );
            }
            if( entry_type == "skin" ) {
                return std::round( weight * damage );
            }
            if( entry_type == "flesh" ) {
                return std::round( weight * slight_damage );
            }
            break;
        case 3:
            if( entry_type == "offal" ) {
                return destroyed;
            }
            if( entry_type == "skin" ) {
                return std::round( weight * high_damage );
            }
            if( entry_type == "bone" ) {
                return std::round( weight * slight_damage );
            }
            if( entry_type == "flesh" ) {
                return std::round( weight * damage );
            }
            break;
        case 4:
            if( entry_type == "offal" ) {
                return destroyed;
            }
            if( entry_type == "skin" ) {
                return destroyed;
            }
            if( entry_type == "bone" ) {
                return std::round( weight * damage );
            }
            if( entry_type == "flesh" ) {
                return std::round( weight * high_damage );
            }
            break;
        default:
            break;
    }
    return weight;
}

std::optional<int> old_entry_yield( const std::string &type, int roll, butcher_type action,
                                    const item &corpse_item, const mtype &mt, const Character &you )
{
    if( corpse_item.has_flag( flag_GIBBED ) && ( type == "flesh" || type == "bone" ) ) {
        roll /= 2;
    }
    if( ( corpse_item.has_flag( flag_SKINNED ) || corpse_item.has_flag( flag_QUARTERED ) ) &&
        type == "skin" ) {
        return std::nullopt;
    }
    const double butch_basic = you.get_proficiency_practice( proficiency_prof_butchering_basic );
    const double skin_basic = you.get_proficiency_practice( proficiency_prof_skinning_basic );
    const double skin_adv = you.get_proficiency_practice( proficiency_prof_skinning_adv );
    const double penalty_small = 0.15;
    const double penalty_big = 2;
    if( type == "flesh" || type == "offal" ) {
        roll /= 1 + ( penalty_small * ( 1 - butch_basic ) );
    }
    if( type == "skin" ) {
        roll /= 1 + ( penalty_big * ( 1 - skin_basic ) );
        roll /= 1 + ( penalty_small * ( 1 - skin_adv ) );
    }
    if( action == butcher_type::QUICK ) {
        if( type == "flesh" ) {
            roll /= 4;
        } else if( type == "bone" ) {
            roll /= 2;
        } else if( mt.size >= creature_size::medium && type == "skin" ) {
            roll /= 2;
        } else if( type == "offal" ) {
            roll /= 5;
        } else {
            return std::nullopt;
        }
    }
    if( action == butcher_type::DISMEMBER ) {
        roll = 0;
    }
    if( action == butcher_type::FIELD_DRESS ) {
        if( type == "bone" ) {
            roll = rng( 0, roll / 2 );
        }
        if( type == "flesh" || type == "skin" ) {
            return std::nullopt;
        }
    }
    if( action == butcher_type::BLEED && type != "blood" ) {
        return std::nullopt;
    }
    if( action == butcher_type::SKIN ) {
        if( type != "skin" ) {
            return std::nullopt;
        }
        if( corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) ) {
            roll = rng( 0, roll );
        }
    }
    if( ( action == butcher_type::FULL || action == butcher_type::QUICK ) &&
        corpse_item.has_flag( flag_FIELD_DRESS ) ) {
        if( type == "offal" ) {
            return std::nullopt;
        }
        if( type == "bone" ) {
            roll = ( roll / 2 ) + rng( roll / 2, roll );
        }
    }
    if( ( action == butcher_type::FULL || action == butcher_type::QUICK ) &&
        corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) ) {
        if( type == "offal" ) {
            return std::nullopt;
        }
        if( type == "bone" ) {
            roll = ( roll / 2 ) + rng( roll / 2, roll );
        }
        if( type == "flesh" || type == "skin" ) {
            roll = rng( 0, roll );
        }
    }
    if( corpse_item.has_flag( flag_QUARTERED ) ) {
        roll /= 4;
    }
    return roll;
}

bool old_has_target( const item &corpse, butcher_type action )
{
    const mtype *dead_mon = corpse.get_mtype();
    bool has_blood = false;
    bool has_skin = false;
    bool has_organs = false;
    for( const harvest_entry &entry : dead_mon->harvest.obj() ) {
        if( entry.type == harvest_drop_skin && !corpse.has_flag( flag_SKINNED ) ) {
            has_skin = true;
        }
        if( entry.type == harvest_drop_offal && !( corpse.has_flag( flag_QUARTERED ) ||
                corpse.has_flag( flag_FIELD_DRESS ) || corpse.has_flag( flag_FIELD_DRESS_FAILED ) ) ) {
            has_organs = true;
        }
        if( entry.type == harvest_drop_blood && dead_mon->bleed_rate > 0 &&
            !( corpse.has_flag( flag_QUARTERED ) || corpse.has_flag( flag_FIELD_DRESS ) ||
               corpse.has_flag( flag_FIELD_DRESS_FAILED ) || corpse.has_flag( flag_BLED ) ) ) {
            has_blood = true;
        }
    }
    switch( action ) {
        case butcher_type::FIELD_DRESS:
            return has_organs;
        case butcher_type::SKIN:
            return has_skin;
        case butcher_type::BLEED:
            return has_blood;
        default:
            return true;
    }
}

const std::vector<butcher_type> &yield_actions()
{
    static const std::vector<butcher_type> actions = {
        butcher_type::QUICK, butcher_type::FULL, butcher_type::FIELD_DRESS, butcher_type::SKIN,
        butcher_type::BLEED, butcher_type::DISMEMBER, butcher_type::DISSECT
    };
    return actions;
}

// Corpse states earlier butchery or a death leaves.
const std::vector<std::vector<flag_id>> &corpse_states()
{
    static const std::vector<std::vector<flag_id>> states = {
        { },
        { flag_GIBBED },
        { flag_FIELD_DRESS },
        { flag_FIELD_DRESS_FAILED },
        { flag_SKINNED },
        { flag_QUARTERED, flag_SKINNED, flag_FIELD_DRESS },
        { flag_BLED },
        { flag_GIBBED, flag_FIELD_DRESS_FAILED },
        { flag_FIELD_DRESS, flag_FIELD_DRESS_FAILED },
    };
    return states;
}

} // namespace

TEST_CASE( "mondt-j02_builtin_drop_types_yield_as_the_old_rules", "[audit][butchery]" )
{
    clear_avatar();
    Character &you = get_player_character();
    const std::vector<harvest_drop_type_id> types = { harvest_drop_flesh, harvest_drop_bone, harvest_drop_skin, harvest_drop_offal, harvest_drop_blood };
    const std::vector<int> rolls = { 0, 1, 2, 3, 5, 7, 10, 13, 40, 99, 333, 4567, 123457 };

    const item small_corpse = item::make_corpse( mon_chicken );
    const item big_corpse = item::make_corpse( mon_deer );
    REQUIRE( small_corpse.get_mtype()->size < creature_size::medium );
    REQUIRE( big_corpse.get_mtype()->size >= creature_size::medium );

    for( int proficiency_level = 0; proficiency_level < 3; ++proficiency_level ) {
        if( proficiency_level == 1 ) {
            you.add_proficiency( proficiency_prof_butchering_basic, true );
            you.add_proficiency( proficiency_prof_skinning_basic, true );
        } else if( proficiency_level == 2 ) {
            you.add_proficiency( proficiency_prof_skinning_adv, true );
        }
        for( const item *base : {
                 &small_corpse, &big_corpse
             } ) {
            for( const std::vector<flag_id> &state : corpse_states() ) {
                item corpse = *base;
                for( const flag_id &f : state ) {
                    corpse.set_flag( f );
                }
                const mtype &mt = *corpse.get_mtype();
                for( const harvest_drop_type_id &type : types ) {
                    for( butcher_type action : yield_actions() ) {
                        for( int roll : rolls ) {
                            const unsigned int seed = 1000u + roll;
                            rng_set_engine_seed( seed );
                            const std::optional<int> got = butchery_entry_yield( type.obj(), roll, action, corpse, mt, you );
                            rng_set_engine_seed( seed );
                            const std::optional<int> expected = old_entry_yield( type.str(), roll, action, corpse, mt, you );
                            CAPTURE( proficiency_level, mt.id.str(), type.str(), io::enum_to_string( action ), roll );
                            CAPTURE( enumerate_as_string( state.begin(), state.end(), []( const flag_id & f ) {
                                return f.str();
                            } ) );
                            CHECK( got == expected );
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE( "mondt-j02_corpse_damage_yields_as_the_old_rules", "[audit][butchery]" )
{
    const std::vector<harvest_drop_type_id> types = { harvest_drop_flesh, harvest_drop_bone, harvest_drop_skin, harvest_drop_offal, harvest_drop_blood, harvest_drop_mutagen };
    const std::vector<int> weights = { 0, 1, 2, 3, 5, 15, 333, 1001, 81500, 40000001 };
    for( const harvest_drop_type_id &type : types ) {
        for( int level = 0; level <= 5; ++level ) {
            for( int weight : weights ) {
                CAPTURE( type.str(), level, weight );
                CHECK( type->corpse_damage_yield( weight, level ) ==
                       old_corpse_damage_effect( weight, type.str(), level ) );
            }
        }
    }
}

TEST_CASE( "mondt-j02_butchery_menu_and_splatter_as_the_old_rules", "[audit][butchery]" )
{
    for( const mtype_id &mon : {
             mon_chicken, mon_deer, mon_zombie, mon_test_bovine
         } ) {
        for( const std::vector<flag_id> &state : corpse_states() ) {
            item corpse = item::make_corpse( mon );
            for( const flag_id &f : state ) {
                corpse.set_flag( f );
            }
            for( butcher_type action : {
                     butcher_type::FIELD_DRESS, butcher_type::SKIN, butcher_type::BLEED, butcher_type::FULL
                 } ) {
                CAPTURE( mon.str(), io::enum_to_string( action ) );
                CHECK( butchery_has_target( corpse, action ) == old_has_target( corpse, action ) );
            }
        }
        CHECK( butchery_needs_field_dressing( *mon ) ==
               mon->harvest->has_entry_type( harvest_drop_offal ) );
    }
    // only flesh and bones survived a splatter
    CHECK( harvest_drop_flesh->survives_splatter() );
    CHECK( harvest_drop_bone->survives_splatter() );
    CHECK_FALSE( harvest_drop_skin->survives_splatter() );
    CHECK_FALSE( harvest_drop_offal->survives_splatter() );
    CHECK_FALSE( harvest_drop_blood->survives_splatter() );
    CHECK_FALSE( harvest_drop_mutagen->survives_splatter() );
}

namespace
{

// The old proficiency training of butchery, keyed on the drop type ids.
void old_train( Character &you, const mtype &mt, butcher_type action,
                const time_duration &moves_total )
{
    const bool meat = mt.harvest->has_entry_type( harvest_drop_flesh ) ||
                      mt.harvest->has_entry_type( harvest_drop_offal );
    const bool skin = mt.harvest->has_entry_type( harvest_drop_skin );
    const auto train = [&you]( const proficiency_id & basic, const proficiency_id & adv,
    const time_duration & amount ) {
        you.practice_proficiency( you.has_proficiency( basic ) ? adv : basic, amount );
    };
    if( action == butcher_type::FULL && meat ) {
        train( proficiency_prof_butchering_basic, proficiency_prof_butchering_adv, moves_total * 0.4 );
    }
    if( action == butcher_type::FULL && skin ) {
        train( proficiency_prof_skinning_basic, proficiency_prof_skinning_adv, moves_total * 0.4 );
    }
    if( action == butcher_type::QUICK && meat ) {
        train( proficiency_prof_butchering_basic, proficiency_prof_butchering_adv, moves_total * 0.7 );
    }
    if( action == butcher_type::QUICK && skin ) {
        train( proficiency_prof_skinning_basic, proficiency_prof_skinning_adv, moves_total * 0.15 );
    }
    if( action == butcher_type::FIELD_DRESS && meat ) {
        train( proficiency_prof_butchering_basic, proficiency_prof_butchering_adv, moves_total );
    }
    if( action == butcher_type::SKIN && skin ) {
        train( proficiency_prof_skinning_basic, proficiency_prof_skinning_adv, moves_total );
    }
}

std::vector<time_duration> practiced_times( const Character &you )
{
    std::vector<time_duration> ret;
    for( const proficiency_id &prof : {
             proficiency_prof_butchering_basic, proficiency_prof_butchering_adv,
             proficiency_prof_skinning_basic, proficiency_prof_skinning_adv
         } ) {
        ret.push_back( you.get_proficiency_practiced_time( prof ) );
    }
    return ret;
}

} // namespace

TEST_CASE( "mondt-j02_butchery_trains_proficiencies_as_before", "[audit][butchery]" )
{
    REQUIRE( mon_deer->harvest->has_entry_type( harvest_drop_skin ) );
    for( const mtype_id &mon : {
             mon_chicken, mon_deer, mon_zombie, mon_test_bovine
         } ) {
        for( butcher_type action : yield_actions() ) {
            for( bool knows_basic : {
                     false, true
                 } ) {
                const auto run = [&]( bool old ) {
                    clear_avatar();
                    Character &you = get_player_character();
                    // both runs start alike: practice scales by focus and drains it
                    you.set_focus( 100 );
                    rng_set_engine_seed( 4242 );
                    if( knows_basic ) {
                        you.add_proficiency( proficiency_prof_butchering_basic, true );
                        you.add_proficiency( proficiency_prof_skinning_basic, true );
                    }
                    if( old ) {
                        old_train( you, *mon, action, 75_minutes );
                    } else {
                        butchery_train_proficiencies( you, *mon, action, 75_minutes );
                    }
                    return practiced_times( you );
                };
                const std::vector<time_duration> expected = run( true );
                const std::vector<time_duration> got = run( false );
                CAPTURE( mon.str(), io::enum_to_string( action ), knows_basic );
                CHECK( got == expected );
            }
        }
    }
}

TEST_CASE( "mondt-j02_modded_drop_type_has_its_own_yields", "[audit][butchery]" )
{
    clear_avatar();
    Character &you = get_player_character();
    const harvest_drop_type &fat = harvest_drop_test_audit_high_fat.obj();
    const harvest_drop_type &plain = harvest_drop_test_audit_high_plain.obj();
    const item corpse = item::make_corpse( mon_test_audit_high_fatty );
    const mtype &mt = *corpse.get_mtype();

    // no butchering_basic: the yield is divided by 1 + 1 * (1 - 0)
    CHECK( butchery_entry_yield( fat, 100, butcher_type::QUICK, corpse, mt, you ) == 25 );
    // and the FULL corpse rule triples it
    CHECK( butchery_entry_yield( fat, 100, butcher_type::FULL, corpse, mt, you ) == 150 );
    CHECK( butchery_entry_yield( fat, 100, butcher_type::SKIN, corpse, mt, you ) == 37 );
    // DISSECT: 0 + a random share between 1 and 1
    CHECK( butchery_entry_yield( fat, 100, butcher_type::DISSECT, corpse, mt, you ) == 50 );
    CHECK_FALSE( butchery_entry_yield( fat, 100, butcher_type::FIELD_DRESS, corpse, mt, you ) );
    CHECK_FALSE( butchery_entry_yield( fat, 100, butcher_type::BLEED, corpse, mt, you ) );
    CHECK( butchery_entry_yield( fat, 100, butcher_type::DISMEMBER, corpse, mt, you ) == std::nullopt );

    item gibbed = corpse;
    gibbed.set_flag( flag_GIBBED );
    CHECK( butchery_entry_yield( fat, 100, butcher_type::QUICK, gibbed, mt, you ) == 6 );
    item dressed = corpse;
    dressed.set_flag( flag_FIELD_DRESS );
    CHECK_FALSE( butchery_entry_yield( fat, 100, butcher_type::QUICK, dressed, mt, you ) );

    you.add_proficiency( proficiency_prof_butchering_basic, true );
    CHECK( butchery_entry_yield( fat, 100, butcher_type::QUICK, corpse, mt, you ) == 50 );

    CHECK( fat.corpse_damage_yield( 100, 0 ) == 100 );
    CHECK( fat.corpse_damage_yield( 100, 1 ) == 50 );
    // past the end of the array
    CHECK( fat.corpse_damage_yield( 100, 4 ) == 50 );
    CHECK( fat.survives_splatter() );

    // a drop type without rules keeps the rules every drop type had: FULL, FIELD_DRESS and DISSECT
    // at full yield, DISMEMBER at 0, the others nothing; corpse damage and gibbing do nothing
    CHECK( butchery_entry_yield( plain, 100, butcher_type::FULL, gibbed, mt, you ) == 100 );
    CHECK( butchery_entry_yield( plain, 100, butcher_type::FIELD_DRESS, corpse, mt, you ) == 100 );
    CHECK( butchery_entry_yield( plain, 100, butcher_type::DISSECT, corpse, mt, you ) == 100 );
    CHECK( butchery_entry_yield( plain, 100, butcher_type::DISMEMBER, corpse, mt, you ) == 0 );
    CHECK_FALSE( butchery_entry_yield( plain, 100, butcher_type::QUICK, corpse, mt, you ) );
    CHECK_FALSE( butchery_entry_yield( plain, 100, butcher_type::SKIN, corpse, mt, you ) );
    CHECK( plain.corpse_damage_yield( 100, 4 ) == 100 );

    // the modded type makes skinning available; the creature has no organs or blood type
    CHECK( butchery_has_target( corpse, butcher_type::SKIN ) );
    CHECK_FALSE( butchery_has_target( corpse, butcher_type::FIELD_DRESS ) );
    CHECK_FALSE( butchery_has_target( corpse, butcher_type::BLEED ) );
    CHECK_FALSE( butchery_needs_field_dressing( mt ) );

    // as much as practicing skinning for half the time
    clear_avatar();
    get_player_character().practice_proficiency( proficiency_prof_skinning_basic, 30_minutes );
    const std::vector<time_duration> expected = practiced_times( get_player_character() );
    clear_avatar();
    butchery_train_proficiencies( get_player_character(), mt, butcher_type::SKIN, 1_hours );
    CHECK( practiced_times( get_player_character() ) == expected );
    CHECK( expected[2] > 0_turns );
}

TEST_CASE( "mondt-j02_drop_type_rules_reject_bad_json", "[audit][butchery]" )
{
    const auto load = []( const std::string & json ) {
        harvest_drop_type type;
        JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        type.load( jo, "test" );
    };
    CHECK_NOTHROW( load(
                       R"({ "action_yield": { "QUICK": 0.5, "SKIN": { "mult": 0.25, "min_size": "SMALL" } } })" ) );
    CHECK_THROWS( load( R"({ "action_yield": { "QUICKLY": 0.5 } })" ) );
    CHECK_THROWS( load( R"({ "enables_action": [ "QUICK" ] })" ) );
    CHECK_THROWS( load( R"({ "action_yield": { "FULL": { "random": [ 1, 0 ] } } })" ) );
    CHECK_THROWS( load( R"({ "action_yield": { "FULL": { "min_size": "ENORMOUS" } } })" ) );
    CHECK_THROWS( load( R"({ "corpse_flag_yield": [ { "flags": [ "SKINNED" ] } ] })" ) );
}

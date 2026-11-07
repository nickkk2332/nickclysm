#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "butchery.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "faction_camp.h"
#include "field_type.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "global_vars.h"
#include "harvest.h"
#include "init.h"
#include "item.h"
#include "json_loader.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapdata.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "options_helpers.h"
#include "path_info.h"
#include "player_helpers.h"
#include "point.h"
#include "skill.h"
#include "sounds.h"
#include "speech.h"
#include "string_formatter.h"
#include "translation.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "weather.h"
#include "weather_type.h"

static const bionic_id bio_creeyncaggii( "bio_creeyncaggii" );
static const bionic_id bio_dex_enhancer( "bio_dex_enhancer" );
static const bionic_id bio_eye_enhancer( "bio_eye_enhancer" );
static const bionic_id bio_int_enhancer( "bio_int_enhancer" );
static const bionic_id bio_metabolics( "bio_metabolics" );
static const bionic_id bio_nl_blood_factory( "bio_nl_blood_factory" );
static const bionic_id bio_power_storage( "bio_power_storage" );
static const bionic_id bio_speed( "bio_speed" );
static const bionic_id bio_str_enhancer( "bio_str_enhancer" );
static const bionic_id bio_taser( "bio_taser" );
static const bionic_id bio_torsionratchet( "bio_torsionratchet" );

static const faction_mission_id faction_mission_camp_survey_expansion( "camp_survey_expansion" );
static const faction_mission_id faction_mission_camp_survey_field( "camp_survey_field" );

static const harvest_drop_type_id harvest_drop_bionic( "bionic" );
static const harvest_drop_type_id harvest_drop_bionic_group( "bionic_group" );
static const harvest_drop_type_id harvest_drop_mutagen( "mutagen" );
static const harvest_drop_type_id harvest_drop_mutagen_group( "mutagen_group" );

static const json_character_flag json_flag_BIONIC_NPC_USABLE( "BIONIC_NPC_USABLE" );

static const mtype_id mon_zombie( "mon_zombie" );

static const skill_id skill_survival( "survival" );

static const ter_str_id ter_t_brick_wall( "t_brick_wall" );
static const ter_str_id ter_t_concrete_wall( "t_concrete_wall" );
static const ter_str_id ter_t_curtains( "t_curtains" );
static const ter_str_id ter_t_door_b( "t_door_b" );
static const ter_str_id ter_t_door_boarded( "t_door_boarded" );
static const ter_str_id ter_t_door_c( "t_door_c" );
static const ter_str_id ter_t_door_frame( "t_door_frame" );
static const ter_str_id ter_t_door_glass_c( "t_door_glass_c" );
static const ter_str_id ter_t_door_locked( "t_door_locked" );
static const ter_str_id ter_t_door_metal_c( "t_door_metal_c" );
static const ter_str_id ter_t_door_metal_locked( "t_door_metal_locked" );
static const ter_str_id ter_t_door_metal_o( "t_door_metal_o" );
static const ter_str_id ter_t_door_o( "t_door_o" );
static const ter_str_id ter_t_palisade( "t_palisade" );
static const ter_str_id ter_t_rdoor_c( "t_rdoor_c" );
static const ter_str_id ter_t_rdoor_o( "t_rdoor_o" );
static const ter_str_id ter_t_rock( "t_rock" );
static const ter_str_id ter_t_rock_wall( "t_rock_wall" );
static const ter_str_id ter_t_sconc_wall( "t_sconc_wall" );
static const ter_str_id ter_t_scrap_wall( "t_scrap_wall" );
static const ter_str_id ter_t_soil( "t_soil" );
static const ter_str_id ter_t_strconc_wall( "t_strconc_wall" );
static const ter_str_id ter_t_wall( "t_wall" );
static const ter_str_id ter_t_wall_glass( "t_wall_glass" );
static const ter_str_id ter_t_wall_log( "t_wall_log" );
static const ter_str_id ter_t_wall_metal( "t_wall_metal" );
static const ter_str_id ter_t_wall_w( "t_wall_w" );
static const ter_str_id ter_t_wall_wood( "t_wall_wood" );
static const ter_str_id ter_t_window( "t_window" );
static const ter_str_id ter_t_window_boarded( "t_window_boarded" );
static const ter_str_id ter_t_window_domestic( "t_window_domestic" );
static const ter_str_id ter_t_window_empty( "t_window_empty" );
static const ter_str_id ter_t_window_frame( "t_window_frame" );
static const ter_str_id ter_t_window_open( "t_window_open" );

static const vpart_id vpart_board( "board" );
static const vpart_id vpart_cloth_flap_door( "cloth_flap_door" );
static const vpart_id vpart_clothboard( "clothboard" );
static const vpart_id vpart_door( "door" );
static const vpart_id vpart_door_opaque( "door_opaque" );
static const vpart_id vpart_halfboard( "halfboard" );
static const vpart_id vpart_hatch( "hatch" );
static const vpart_id vpart_hdboard( "hdboard" );
static const vpart_id vpart_hdstowboard( "hdstowboard" );
static const vpart_id vpart_reinforced_windshield( "reinforced_windshield" );
static const vpart_id vpart_stowboard( "stowboard" );
static const vpart_id vpart_v_curtain( "v_curtain" );
static const vpart_id vpart_windshield( "windshield" );
static const vpart_id vpart_woodboard( "woodboard" );

namespace
{

bool is_test_id( const std::string &id )
{
    return id.rfind( "test_", 0 ) == 0;
}

// A sound at source, heard 10 tiles east at listener; mid is halfway between.
const tripoint_bub_ms source( 60, 66, 0 );
const tripoint_bub_ms mid( 65, 66, 0 );
const tripoint_bub_ms listener( 70, 66, 0 );

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
        // out of the way of the line, so it neither blocks nor makes the sounds
        get_avatar().setpos( get_map(), tripoint_bub_ms( 20, 20, 0 ) );
    }
    ~sound_scene() {
        sounds::reset_sounds();
        clear_creatures();
        clear_vehicles();
        clear_map();
    }
};

// What a test zombie at listener makes of a shout of 40 at source: the volume it follows
int zombie_hears_shout()
{
    clear_creatures();
    sounds::reset_sounds();
    monster &zed = spawn_test_monster( "mon_test_zombie", listener );
    REQUIRE( zed.wandf == 0 );
    sounds::sound( source, 40, sounds::sound_t::speech, "a test shout" );
    sounds::process_sounds();
    const int heard = zed.wandf;
    clear_creatures();
    return heard;
}

// override_option restores an option from its text value, which external options don't have;
// this keeps the number itself, and refreshes the cached copy of the option.
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

// Exposes the loader every data file goes through.
class content_test_loader : public DynamicDataLoader
{
    public:
        using DynamicDataLoader::load_object;
};

// Loads `json` as if a mod no other definition came from had it.
void load_from_later_mod( const std::string &json )
{
    static int mods = 0;
    content_test_loader loader;
    loader.load_object( json_loader::from_string( json ),
                        string_format( "audit_m_fu_content_mod_%d", mods++ ) );
}

} // namespace

// SPAT-J01-values: shipped walls, doors, windows and vehicle hulls muffle sound by what they
// are made of; open and broken ones, furniture and fields muffle nothing.
TEST_CASE( "spat-j01-values_vanilla_walls_doors_and_windows_muffle_by_material",
           "[audit][sound]" )
{
    const std::vector<std::pair<ter_str_id, int>> terrain = {
        { ter_t_rock, 20 }, { ter_t_soil, 20 }, { ter_t_strconc_wall, 20 },
        { ter_t_concrete_wall, 15 }, { ter_t_sconc_wall, 15 }, { ter_t_brick_wall, 15 },
        { ter_t_rock_wall, 15 }, { ter_t_rdoor_c, 15 },
        { ter_t_wall_metal, 12 }, { ter_t_scrap_wall, 12 },
        { ter_t_wall_log, 10 }, { ter_t_palisade, 10 },
        { ter_t_wall, 8 }, { ter_t_wall_w, 8 }, { ter_t_wall_wood, 8 },
        { ter_t_door_metal_c, 8 }, { ter_t_door_metal_locked, 8 },
        { ter_t_window_boarded, 6 }, { ter_t_door_boarded, 6 },
        { ter_t_door_c, 5 }, { ter_t_door_locked, 5 },
        { ter_t_window, 4 }, { ter_t_window_domestic, 4 }, { ter_t_curtains, 4 },
        { ter_t_door_glass_c, 4 }, { ter_t_wall_glass, 4 },
        { ter_t_door_o, 0 }, { ter_t_door_b, 0 }, { ter_t_door_metal_o, 0 }, { ter_t_rdoor_o, 0 },
        { ter_t_door_frame, 0 }, { ter_t_window_open, 0 }, { ter_t_window_empty, 0 },
        { ter_t_window_frame, 0 }
    };
    for( const std::pair<ter_str_id, int> &ter : terrain ) {
        CAPTURE( ter.first.str() );
        CHECK( ter.first->sound_attenuation == ter.second );
    }
    const std::vector<std::pair<vpart_id, int>> parts = {
        { vpart_hdboard, 10 }, { vpart_hdstowboard, 10 },
        { vpart_board, 6 }, { vpart_stowboard, 6 }, { vpart_woodboard, 6 },
        { vpart_door, 5 }, { vpart_door_opaque, 5 }, { vpart_hatch, 5 },
        { vpart_reinforced_windshield, 5 }, { vpart_windshield, 3 },
        { vpart_clothboard, 1 }, { vpart_cloth_flap_door, 1 },
        { vpart_halfboard, 0 }, { vpart_v_curtain, 0 }
    };
    for( const std::pair<vpart_id, int> &part : parts ) {
        CAPTURE( part.first.str() );
        CHECK( part.first->sound_attenuation == part.second );
    }

    // opening a door, window or curtain never makes it muffle more
    for( const ter_t &ter : mapdata::terrain_data.get_all() ) {
        if( !ter.open.is_empty() && ter.open.is_valid() ) {
            CAPTURE( ter.id.str(), ter.open.str() );
            CHECK( ter.open->sound_attenuation <= ter.sound_attenuation );
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
}

// SPAT-J01-values: the external option SOUND_ATTENUATION_PERCENT scales what muffles a sound;
// at 0 nothing does.
TEST_CASE( "spat-j01-values_sound_attenuation_percent_scales_muffling", "[audit][sound]" )
{
    REQUIRE( get_option<int>( "SOUND_ATTENUATION_PERCENT" ) == 100 );
    sound_scene scene;
    map &here = get_map();
    const int open_ground = 40 - get_weather().weather_id->sound_attn - rl_dist( source, listener );
    here.ter_set( mid, ter_t_brick_wall );
    REQUIRE( here.sound_attenuation_between( source, listener ) == 15 );

    SECTION( "100: as the JSON says" ) {
        CHECK( here.sound_attenuation_at( mid ) == 15 );
        CHECK( zombie_hears_shout() == open_ground - 15 );
    }
    SECTION( "50: half, rounded down" ) {
        override_external_int_option percent( "SOUND_ATTENUATION_PERCENT", 50 );
        CHECK( here.sound_attenuation_between( source, listener ) == 7 );
        CHECK( here.sound_attenuation_at( mid ) == 7 );
        CHECK( zombie_hears_shout() == open_ground - 7 );
    }
    SECTION( "200: double" ) {
        override_external_int_option percent( "SOUND_ATTENUATION_PERCENT", 200 );
        CHECK( here.sound_attenuation_between( source, listener ) == 30 );
        // a caller that only needs to know it reaches 20 gets the scaled sum it stopped at
        here.ter_set( mid + point::east, ter_t_brick_wall );
        CHECK( here.sound_attenuation_between( source, listener ) == 60 );
        CHECK( here.sound_attenuation_between( source, listener, 20 ) == 30 );
    }
    SECTION( "0: muffling off" ) {
        override_external_int_option percent( "SOUND_ATTENUATION_PERCENT", 0 );
        CHECK( here.sound_attenuation_between( source, listener ) == 0 );
        CHECK( here.sound_attenuation_at( mid ) == 0 );
        CHECK_FALSE( here.sound_muffled_on_level( 0 ) );
        CHECK( zombie_hears_shout() == open_ground );
    }
    CHECK( get_option<int>( "SOUND_ATTENUATION_PERCENT" ) == 100 );
}

// NPC-J01-content: the stat-enhancer, power and combat CBMs NPCs could use got an npc_use.
TEST_CASE( "npc-j01-content_stat_and_combat_cbms_have_an_npc_use", "[audit][bionics][npc]" )
{
    struct expected_use {
        bionic_id id;
        bionic_npc_use when;
        int priority;
    };
    const std::vector<expected_use> uses = {
        { bio_speed, bionic_npc_use::combat, 10 },
        { bio_str_enhancer, bionic_npc_use::combat, 5 },
        { bio_dex_enhancer, bionic_npc_use::combat, 5 },
        { bio_eye_enhancer, bionic_npc_use::combat, 5 },
        { bio_int_enhancer, bionic_npc_use::upkeep, -10 },
        { bio_creeyncaggii, bionic_npc_use::upkeep, 10 },
        { bio_metabolics, bionic_npc_use::upkeep, 5 },
        { bio_torsionratchet, bionic_npc_use::upkeep, 5 },
        { bio_nl_blood_factory, bionic_npc_use::sleep, 0 },
        { bio_taser, bionic_npc_use::melee, 0 }
    };
    for( const expected_use &use : uses ) {
        CAPTURE( use.id.str() );
        CHECK( use.id->has_flag( json_flag_BIONIC_NPC_USABLE ) );
        CHECK( use.id->npc_use == use.when );
        CHECK( use.id->npc_use_priority == use.priority );
    }

    // The mods' CBMs, read from their files (only core and test data load here)
    const std::map<std::string, std::map<std::string, std::string>> mod_uses = {
        {
            "aftershock_exoplanet", {
                { "bio_cold_absorber", "combat" }, { "afs_bio_blade_fire", "melee" },
                { "afs_bio_blade_electric", "melee" }
            }
        },
        { "MindOverMatter", { { "bio_metabolics_fifth_sun", "upkeep" } } }
    };
    for( const auto &mod : mod_uses ) {
        const cata_path file = mod.first == "MindOverMatter" ?
                               PATH_INFO::datadir_path() / "mods" / mod.first / "bionics.json" :
                               PATH_INFO::datadir_path() / "mods" / mod.first / "player" / "bionics.json";
        const JsonValue jv = json_loader::from_path( file );
        std::set<std::string> found;
        for( JsonObject jo : jv.get_array() ) {
            jo.allow_omitted_members();
            if( !jo.has_string( "id" ) ) {
                continue;
            }
            const auto expected = mod.second.find( jo.get_string( "id" ) );
            if( expected == mod.second.end() ) {
                continue;
            }
            CAPTURE( expected->first );
            found.insert( expected->first );
            std::string when;
            if( jo.has_object( "npc_use" ) ) {
                JsonObject use = jo.get_object( "npc_use" );
                use.allow_omitted_members();
                when = use.get_string( "when" );
            } else {
                when = jo.get_string( "npc_use", "" );
            }
            CHECK( when == expected->second );
        }
        CHECK( found.size() == mod.second.size() );
    }
}

// NPC-J01-content: an NPC switches its combat CBMs on for a fight, the highest priority first,
// so with power for only one the synaptic accelerator wins over the muscle augmentation.
TEST_CASE( "npc-j01-content_npc_switches_on_its_combat_cbms_by_priority",
           "[audit][bionics][npc]" )
{
    standard_npc guy( "Augmented" );
    clear_character( guy );
    guy.add_bionic( bio_power_storage );
    // installed first, so without the priorities it would come first
    guy.add_bionic( bio_str_enhancer );
    guy.add_bionic( bio_speed );
    REQUIRE( bio_speed->power_activate == 10_J );
    REQUIRE( bio_str_enhancer->power_activate == 5_J );

    SECTION( "power for both" ) {
        guy.set_power_level( guy.get_max_power_level() );
        guy.activate_combat_cbms();
        CHECK( guy.has_active_bionic( bio_speed ) );
        CHECK( guy.has_active_bionic( bio_str_enhancer ) );
        guy.deactivate_combat_cbms();
        CHECK_FALSE( guy.has_active_bionic( bio_speed ) );
        CHECK_FALSE( guy.has_active_bionic( bio_str_enhancer ) );
    }
    SECTION( "power for one" ) {
        guy.set_power_level( 12_J );
        guy.activate_combat_cbms();
        CHECK( guy.has_active_bionic( bio_speed ) );
        CHECK_FALSE( guy.has_active_bionic( bio_str_enhancer ) );
        guy.deactivate_combat_cbms();
        CHECK_FALSE( guy.has_active_bionic( bio_speed ) );
    }
}

// MONDT-J02-content: bionic and mutagen drops came from field dressing at full yield, and then
// again from full butchery of the same corpse (Magiclysm's adrenal glands). Now field dressing
// gives none of them; full butchery and dissection give them all, dismembering destroys them.
TEST_CASE( "mondt-j02-content_implants_and_organs_skip_field_dressing", "[audit][butchery]" )
{
    clear_avatar();
    const Character &you = get_player_character();
    const item corpse = item::make_corpse( mon_zombie );
    const mtype &mt = *corpse.get_mtype();
    item dressed = corpse;
    dressed.set_flag( flag_FIELD_DRESS );
    for( const harvest_drop_type_id &type : {
             harvest_drop_bionic, harvest_drop_bionic_group, harvest_drop_mutagen,
             harvest_drop_mutagen_group
         } ) {
        CAPTURE( type.str() );
        const harvest_drop_type &drop = type.obj();
        CHECK_FALSE( butchery_entry_yield( drop, 100, butcher_type::FIELD_DRESS, corpse, mt, you ) );
        CHECK( butchery_entry_yield( drop, 100, butcher_type::FULL, corpse, mt, you ) == 100 );
        CHECK( butchery_entry_yield( drop, 100, butcher_type::FULL, dressed, mt, you ) == 100 );
        CHECK( butchery_entry_yield( drop, 100, butcher_type::DISSECT, corpse, mt, you ) == 100 );
        CHECK( butchery_entry_yield( drop, 100, butcher_type::DISMEMBER, corpse, mt, you ) == 0 );
        CHECK_FALSE( butchery_entry_yield( drop, 100, butcher_type::QUICK, corpse, mt, you ) );
        CHECK_FALSE( butchery_entry_yield( drop, 100, butcher_type::SKIN, corpse, mt, you ) );
        CHECK_FALSE( butchery_entry_yield( drop, 100, butcher_type::BLEED, corpse, mt, you ) );
    }
}

// CAMP-J02-survey: the two survey missions picked their companion by marksmanship.
TEST_CASE( "camp-j02-survey_survey_missions_pick_by_survival", "[audit][camp]" )
{
    for( const faction_mission_id &mission : {
             faction_mission_camp_survey_field, faction_mission_camp_survey_expansion
         } ) {
        CAPTURE( mission.str() );
        CHECK( mission->companion_skills() == std::map<skill_id, int> { { skill_survival, 0 } } );
    }
}

// DATA-J02-speech: vanilla speech lines had no ids, so mods could only add to them. Each has a
// unique id speech_<first speaker>_<n>, n counting that speaker's lines in file order, and a
// later mod can change or remove one.
TEST_CASE( "data-j02-speech_vanilla_lines_have_stable_unique_ids", "[audit][json]" )
{
    const JsonValue jv = json_loader::from_path( PATH_INFO::datadir_path() / "json" /
                         "speech.json" );
    std::map<std::string, int> counts;
    std::set<std::string> ids;
    std::string first_id;
    std::string first_speakers;
    std::string first_sound;
    int first_volume = 0;
    for( JsonObject jo : jv.get_array() ) {
        jo.allow_omitted_members();
        REQUIRE( jo.get_string( "type" ) == "speech" );
        REQUIRE( jo.has_string( "id" ) );
        const std::string id = jo.get_string( "id" );
        const std::vector<std::string> speakers = jo.get_as_string_array( "speaker" );
        REQUIRE_FALSE( speakers.empty() );
        CAPTURE( id );
        CHECK( ids.insert( id ).second );
        CHECK( id == string_format( "speech_%s_%d", speakers.front(), ++counts[speakers.front()] ) );
        if( first_id.empty() ) {
            first_id = id;
            for( const std::string &speaker : speakers ) {
                first_speakers += ( first_speakers.empty() ? "\"" : ", \"" ) + speaker + "\"";
            }
            first_sound = jo.get_string( "sound" );
            first_volume = jo.get_int( "volume" );
        }
    }
    REQUIRE( ids.size() > 500 );

    const std::string retarget =
        R"({ "type": "speech", "id": "%s", "copy-from": "%s", "speaker": [ %s ] })";
    const on_out_of_scope restore( [&]() {
        load_from_later_mod( string_format( retarget, first_id, first_id, first_speakers ) );
    } );
    // given to another speaker, it keeps what it says
    load_from_later_mod( string_format( retarget, first_id, first_id, "\"audit_m_fu_speaker\"" ) );
    CHECK( get_speech( "audit_m_fu_speaker" ).text.translated() == first_sound );
    CHECK( get_speech( "audit_m_fu_speaker" ).volume == first_volume );
    // taken from every speaker, nobody says it
    load_from_later_mod( string_format( retarget, first_id, first_id, "" ) );
    CHECK( get_speech( "audit_m_fu_speaker" ).volume == 0 );
    CHECK( get_speech( "audit_m_fu_speaker" ).text.translated() == "INVALID SPEECH" );
}

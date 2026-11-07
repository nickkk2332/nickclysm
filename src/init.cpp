#include "init.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ability.h"
#include "ability_runtime.h"
#include "achievement.h"
#include "activity_type.h"
#include "addiction.h"
#include "ammo.h"
#include "ammo_effect.h"
#include "anatomy.h"
#include "ascii_art.h"
#include "behavior.h"
#include "bionics.h"
#include "bodygraph.h"
#include "bodypart.h"
#include "butchery_requirements.h"
#include "cata_assert.h"
#include "cata_scope_helpers.h"
#include "character_modifier.h"
#include "city.h"
#include "climbing.h"
#include "clothing_mod.h"
#include "clzones.h"
#include "condition.h"
#include "construction.h"
#include "construction_category.h"
#include "construction_group.h"
#include "crafting_gui.h"
#include "creature.h"
#include "damage.h"
#include "damage_rule.h"
#include "debug.h"
#include "dialogue.h"
#include "disease.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "emit.h"
#include "end_screen.h"
#include "event_statistics.h"
#include "execution.h"
#include "execution_runtime.h"
#include "execution_trigger.h"
#include "faction.h"
#include "faction_camp.h"
#include "fault.h"
#include "field_type.h"
#include "filesystem.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "gates.h"
#include "global_vars.h"
#include "harvest.h"
#include "help.h"
#include "input.h"
#include "item_action.h"
#include "item_category.h"
#include "item_factory.h"
#include "itype.h"
#include "json_loader.h"
#include "loading_ui.h"
#include "lru_cache.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "magic_ter_furn_transform.h"
#include "magic_type.h"
#include "map_accessories.h"
#include "map_extras.h"
#include "mapdata.h"
#include "mapgen.h"
#include "mapgen_post_process.h"
#include "martialarts.h"
#include "material.h"
#include "math_parser_jmath.h"
#include "mission.h"
#include "mod_manager.h"
#include "mod_setting.h"
#include "mod_tileset.h"
#include "monfaction.h"
#include "mongroup.h"
#include "monstergenerator.h"
#include "mood_face.h"
#include "morale_types.h"
#include "move_mode.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "npc_class.h"
#include "npc_decision_category.h"
#include "npc_json_goal.h"
#include "omdata.h"
#include "options.h"
#include "overlay_ordering.h"
#include "overmap.h"
#include "overmap_connection.h"
#include "overmap_location.h"
#include "overmap_map_data_cache.h"
#include "overmap_worldgen.h"
#include "power.h"
#include "power_actor.h"
#include "power_projection.h"
#include "profession.h"
#include "profession_group.h"
#include "proficiency.h"
#include "recipe_dictionary.h"
#include "recipe_groups.h"
#include "regional_settings.h"
#include "relic.h"
#include "requirements.h"
#include "rotatable_symbols.h"
#include "scenario.h"
#include "scent_map.h"
#include "shop_cons_rate.h"
#include "skill.h"
#include "sounds.h"
#include "speech.h"
#include "speed_description.h"
#include "start_location.h"
#include "string_formatter.h"
#include "subbodypart.h"
#include "test_data.h"
#include "text_snippets.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "veh_type.h"
#include "vehicle_group.h"
#include "vehicle_part_location.h"
#include "vitamin.h"
#include "weakpoint.h"
#include "weather_gen.h"
#include "weather_type.h"
#include "widget.h"
#include "worldfactory.h"
#include "wound.h"

#if defined(TILES)
#include "sdltiles.h"
#endif

namespace
{
// When each source first loaded each object id. A copy-from whose parent isn't loaded yet is
// deferred and replayed at finalize, after every mod has loaded; the replay must not
// overwrite a definition of the same id that a later load made.
struct load_history {
    // source -> a small number standing for it
    std::unordered_map<std::string, int> src_index;
    // object_keys() key -> ( source index, load number ) of each source's first load of it
    std::unordered_map<std::string, std::vector<std::pair<int, uint64_t>>> loaded_by;
    uint64_t next_load = 0;

    void clear() {
        src_index.clear();
        loaded_by.clear();
        next_load = 0;
    }
};

load_history object_load_history;

// Set while load_deferred() replays objects whose copy-from parent wasn't loaded yet: a replay
// is the same definition again, not a second one from its source.
bool replaying_deferred = false;

// One key per object id ("type\nid"), or one for the abstract id.
std::vector<std::string> object_keys( const JsonObject &jo )
{
    std::vector<std::string> keys;
    if( !jo.has_string( "type" ) ) {
        return keys;
    }
    const std::string prefix = jo.get_string( "type" ) + '\n';
    if( jo.has_string( "id" ) ) {
        keys.push_back( prefix + jo.get_string( "id" ) );
    } else if( jo.has_array( "id" ) ) {
        for( const JsonValue id : jo.get_array( "id" ) ) {
            if( id.test_string() ) {
                keys.push_back( prefix + id.get_string() );
            }
        }
    } else if( jo.has_string( "abstract" ) ) {
        keys.push_back( prefix + "abstract\n" + jo.get_string( "abstract" ) );
    }
    return keys;
}

// Whether another source loaded every id `jo` defines after `src` deferred it, other than
// through an object still waiting in [pending, pending_end). Replaying `jo` would then
// overwrite that later definition.
bool superseded_by_later_load( const JsonObject &jo, const std::string &src,
                               DynamicDataLoader::deferred_json::const_iterator pending,
                               DynamicDataLoader::deferred_json::const_iterator pending_end )
{
    const load_history &history = object_load_history;
    const auto own_src = history.src_index.find( src );
    const std::vector<std::string> keys = object_keys( jo );
    if( own_src == history.src_index.end() || keys.empty() ) {
        return false;
    }
    for( const std::string &key : keys ) {
        const auto loaded = history.loaded_by.find( key );
        if( loaded == history.loaded_by.end() ) {
            return false;
        }
        const std::vector<std::pair<int, uint64_t>> &loads = loaded->second;
        const auto own_load = std::find_if( loads.begin(), loads.end(),
        [&]( const std::pair<int, uint64_t> &load ) {
            return load.first == own_src->second;
        } );
        if( own_load == loads.end() ) {
            return false;
        }
        const bool superseded = std::any_of( loads.begin(), loads.end(),
        [&]( const std::pair<int, uint64_t> &later ) {
            if( later.second <= own_load->second ) {
                return false;
            }
            // a later definition that is deferred too is replayed after this one
            return std::none_of( pending, pending_end,
            [&]( const std::pair<JsonObject, std::string> &other ) {
                const auto other_src = history.src_index.find( other.second );
                if( other_src == history.src_index.end() || other_src->second != later.first ) {
                    return false;
                }
                const std::vector<std::string> other_keys = object_keys( other.first );
                return std::find( other_keys.begin(), other_keys.end(), key ) != other_keys.end();
            } );
        } );
        if( !superseded ) {
            return false;
        }
    }
    return true;
}
} // namespace

DynamicDataLoader::DynamicDataLoader()
{
    initialize();
}

DynamicDataLoader::~DynamicDataLoader() = default;

DynamicDataLoader &DynamicDataLoader::get_instance()
{
    static DynamicDataLoader theDynamicDataLoader;
    return theDynamicDataLoader;
}

void DynamicDataLoader::load_object( const JsonObject &jo, const std::string &src,
                                     const cata_path &base_path,
                                     const cata_path &full_path )
{
    const std::string type = jo.get_string( "type" );
    const t_type_function_map::iterator it = type_function_map.find( type );
    if( it == type_function_map.end() ) {
        jo.throw_error_at( "type", "unrecognized JSON object" );
    }
    load_history &history = object_load_history;
    const int src_index = history.src_index.try_emplace( src,
                          static_cast<int>( history.src_index.size() ) ).first->second;
    const uint64_t load = history.next_load++;
    // These types don't record where each id came from (no mod_tracker "src"), so two
    // definitions of one id from the same source are reported here (NPC-J02).
    static const std::unordered_set<std::string> untracked_types = { "faction", "npc" };
    const bool report_duplicates = !replaying_deferred && untracked_types.count( type ) > 0;
    for( std::string &key : object_keys( jo ) ) {
        const std::string id = report_duplicates ? key.substr( key.rfind( '\n' ) + 1 ) : "";
        std::vector<std::pair<int, uint64_t>> &loads = history.loaded_by[std::move( key )];
        const bool first_from_src = std::none_of( loads.begin(), loads.end(),
        [src_index]( const std::pair<int, uint64_t> &earlier ) {
            return earlier.first == src_index;
        } );
        if( first_from_src ) {
            loads.emplace_back( src_index, load );
        } else if( report_duplicates ) {
            debugmsg( "%s \"%s\" has two definitions from the same source (%s)", type, id, src );
        }
    }
    it->second( jo, src, base_path, full_path );
}

struct DynamicDataLoader::cached_streams {
    lru_cache<std::string, shared_ptr_fast<std::istringstream>> cache;
};

shared_ptr_fast<std::istream> DynamicDataLoader::get_cached_stream( const std::string &path )
{
    cata_assert( !finalized &&
                 "Cannot open data file after finalization." );
    cata_assert( stream_cache &&
                 "Stream cache is only available during finalization" );
    shared_ptr_fast<std::istringstream> cached = stream_cache->cache.get( path, nullptr );
    // Create a new stream if the file is not opened yet, or if some code is still
    // using the previous stream (in such case, `cached` and `stream_cache` have
    // two references to the stream, hence the test for > 2).
    if( !cached ) {
        cached = make_shared_fast<std::istringstream>( read_entire_file( path ) );
    } else if( cached.use_count() > 2 ) {
        cached = make_shared_fast<std::istringstream>( cached->str() );
    }
    stream_cache->cache.insert( 8, path, cached );
    return cached;
}

void DynamicDataLoader::load_deferred( deferred_json &data )
{
    restore_on_out_of_scope restore_replaying( replaying_deferred );
    replaying_deferred = true;
    while( !data.empty() ) {
        const size_t n = data.size();
        auto it = data.begin();
        for( size_t idx = 0; idx != n; ++idx ) {
            try {
                const JsonObject &jo = it->first;
                if( superseded_by_later_load( jo, it->second, std::next( it ), data.end() ) ) {
                    // A later load redefined it; loading it now would undo that.
                    jo.allow_omitted_members();
                } else {
                    load_object( jo, it->second );
                }
            } catch( const JsonError &err ) {
                debugmsg( "(json-error)\n%s", err.what() );
            }
            ++it;
            inp_mngr.pump_events();
        }
        data.erase( data.begin(), it );
        if( data.size() == n ) {
            // A copy-from target among the stuck objects is part of a cycle or chain; any
            // other target was never loaded.
            std::unordered_set<std::string> stuck_ids;
            for( const auto &elem : data ) {
                for( const std::string &key : object_keys( elem.first ) ) {
                    stuck_ids.insert( key.substr( key.rfind( '\n' ) + 1 ) );
                }
            }
            for( const auto &elem : data ) {
                try {
                    const JsonObject &jo = elem.first;
                    if( jo.has_string( "copy-from" ) ) {
                        const std::string parent = jo.get_string( "copy-from" );
                        const bool stuck = stuck_ids.count( parent ) != 0;
                        jo.throw_error_at( "copy-from", "copy-from target \"" + parent + "\" " +
                                           ( stuck ? "could not be loaded" : "was not found" ) +
                                           ", this object is discarded" );
                    }
                    jo.throw_error( "JSON contains circular dependency, this object is discarded" );
                } catch( const JsonError &err ) {
                    debugmsg( "(json-error)\n%s", err.what() );
                }
                inp_mngr.pump_events();
            }
            data.clear();
            return; // made no progress on this cycle so abort
        }
    }
}

static void load_ignored_type( const JsonObject &jo )
{
    // This does nothing!
    // This function is used for types that are to be ignored
    // (for example for testing or for unimplemented types)
    jo.allow_omitted_members();
}

void DynamicDataLoader::add( const std::string &type,
                             const std::function<void( const JsonObject &, const std::string &, const cata_path &, const cata_path & )>
                             &f )
{
    const auto pair = type_function_map.emplace( type, f );
    if( !pair.second ) {
        debugmsg( "tried to insert a second handler for type %s into the DynamicDataLoader", type.c_str() );
    }
}

void DynamicDataLoader::add( const std::string &type,
                             const std::function<void( const JsonObject &, const std::string &, const std::string &, const std::string & )>
                             &f )
{
    add( type, [f]( const JsonObject & obj, const std::string & src, const cata_path & base_path,
    const cata_path & full_path ) {
        f( obj, src, base_path.generic_u8string(), full_path.generic_u8string() );
    } );
}

void DynamicDataLoader::add( const std::string &type,
                             const std::function<void( const JsonObject &, const std::string & )> &f )
{
    add( type, [f]( const JsonObject & obj, const std::string & src, const cata_path &,
    const cata_path & ) {
        f( obj, src );
    } );
}

void DynamicDataLoader::add( const std::string &type,
                             const std::function<void( const JsonObject & )> &f )
{
    add( type, [f]( const JsonObject & obj, std::string_view,  const cata_path &,
    const cata_path & ) {
        f( obj );
    } );
}

void DynamicDataLoader::initialize()
{
    // Initialize loading data that must be in place before any loading functions are called
    init_mapdata();

    // all of the applicable types that can be loaded, along with their loading functions
    // Add to this as needed with new StaticFunctionAccessors or new ClassFunctionAccessors for new applicable types
    // Static Function Access
    add( "WORLD_OPTION", &load_world_option );
    add( "mod_setting", &mod_settings::load );
    add( "EXTERNAL_OPTION", &load_external_option );
    add( "option_slider", &option_slider::load_option_sliders );
    add( "json_flag", &json_flag::load_all );
    add( "jmath_function", &jmath_func::load_func );
    add( "var_migration", &global_variables::load_migrations );
    add( "connect_group", &connect_group::load );
    add( "fault", &faults::load_fault );
    add( "pp_generator", &pp_generators::load );
    add( "fault_fix", &faults::load_fix );
    add( "fault_group", &faults::load_group );
    add( "relic_procgen_data", &relic_procgen_data::load_relic_procgen_data );
    add( "effect_on_condition", &effect_on_conditions::load );
    add( "execution", &executions::load );
    add( "ability", &abilities::load );
    add( "damage_rule", &damage_rules::load );
    add( "power_resource", &powers::load_resource );
    add( "power_cooldown", &powers::load_cooldown );
    add( "power_application", &powers::load_application );
    add( "power_summon", &power_summons::load );
    add( "power_projection", &power_projections::load );
    add( "execution_trigger", &execution_triggers::load );
    add( "field_type", &field_types::load );
    add( "field_type_migration", &field_type_migrations::load );
    add( "weather_type", &weather_types::load );
    add( "weather_generator", &weather_generator::load_weather_generator );
    add( "ammo_effect", &ammo_effects::load );
    add( "emit", &emit::load_emit );
    add( "activity_type", &activity_type::load_all );
    add( "addiction_type", &add_type::load_add_types );
    add( "movement_mode", &move_mode::load_move_mode );
    add( "vitamin", &vitamin::load_vitamin );
    add( "material", &materials::load );
    add( "bash_damage_profile", &bash_damage_profile::load_all );
    add( "bionic", &bionic_data::load_bionic );
    add( "bionic_migration", &bionic_data::load_bionic_migration );
    add( "profession", &profession::load_profession );
    add( "profession_blacklist", &profession_blacklist::load_profession_blacklist );
    add( "profession_group", &profession_group::load_profession_group );
    add( "profession_item_substitutions", &profession::load_item_substitutions );
    add( "proficiency", &proficiency::load_proficiencies );
    add( "proficiency_category", &proficiency_category::load_proficiency_categories );
    add( "proficiency_migration", &proficiency_migration::load );
    add( "speed_description", &speed_description::load_speed_descriptions );
    add( "mood_face", &mood_face::load_mood_faces );
    add( "skill", &Skill::load_skill );
    add( "skill_display_type", &SkillDisplayType::load );
    add( "dream", &dream::load );
    add( "mutation_category", &mutation_category_trait::load );
    add( "mutation_type", &load_mutation_type );
    add( "mutation", &mutation_branch::load_trait );
    add( "furniture", &load_furniture );
    add( "terrain", &load_terrain );
    add( "ter_furn_migration", &ter_furn_migrations::load );
    add( "monstergroup", &MonsterGroupManager::LoadMonsterGroup );
    add( "MONSTER_BLACKLIST", &MonsterGroupManager::LoadMonsterBlacklist );
    add( "MONSTER_WHITELIST", &MonsterGroupManager::LoadMonsterWhitelist );
    add( "speech", &load_speech );
    add( "ammunition_type", &ammunition_type::load_ammunition_type );
    add( "start_location", &start_locations::load );
    add( "scenario", &scenario::load_scenario );
    add( "SCENARIO_BLACKLIST", &scen_blacklist::load_scen_blacklist );
    add( "shopkeeper_blacklist", &shopkeeper_blacklist::load_blacklist );
    add( "shopkeeper_whitelist", &shopkeeper_whitelist::load_whitelist );
    add( "shopkeeper_consumption_rates", &shopkeeper_cons_rates::load_rate );
    add( "enchantment", &enchantment::load_enchantment );
    add( "hit_range", &Creature::load_hit_range );
    add( "scent_type", &scent_type::load_scent_type );
    add( "disease_type", &disease_type::load_disease_type );
    add( "ascii_art", &ascii_art::load_ascii_art );
    add( "end_screen", &end_screen::load_end_screen );

    // json/colors.json would be listed here, but it's loaded before the others (see init_colors())
    // Non Static Function Access
    add( "snippet", []( const JsonObject & jo, const std::string & src ) {
        SNIPPET.load_snippet( jo, src );
    } );
    add( "help", []( const JsonObject & jo, const std::string & src ) {
        get_help().load( jo, src );
    } );
    add( "item_group", []( const JsonObject & jo ) {
        item_controller->load_item_group( jo );
    } );
    add( "trait_group", []( const JsonObject & jo ) {
        mutation_branch::load_trait_group( jo );
    } );
    add( "item_action", []( const JsonObject & jo ) {
        item_action_generator::generator().load_item_action( jo );
    } );

    add( "vehicle_part",  &vehicles::parts::load );
    add( "vehicle_part_category",  &vpart_category::load_all );
    add( "vehicle_part_location",  &vpart_location::load_vehicle_part_locations );
    add( "vehicle_part_migration", &vpart_migration::load );
    add( "vehicle", &vehicles::load_prototype );
    add( "vehicle_group",  &VehicleGroup::load );
    add( "vehicle_placement",  &VehiclePlacement::load );
    add( "vehicle_spawn",  &VehicleSpawn::load );

    add( "requirement", []( const JsonObject & jo ) {
        requirement_data::load_requirement( jo, string_id<requirement_data>::NULL_ID(), true );
    } );
    add( "trap", &trap::load_trap );
    add( "trap_migration", &trap_migrations::load );

    add( "ITEM", &items::load );
    add( "ITEM_CATEGORY", &item_category::load_item_cat );

    add( "MIGRATION", []( const JsonObject & jo ) {
        item_controller->load_migration( jo );
    } );

    add( "charge_removal_blacklist", load_charge_removal_blacklist );
    add( "temperature_removal_blacklist", load_temperature_removal_blacklist );
    add( "test_data", &test_data::load );

    add( "MONSTER", []( const JsonObject & jo, const std::string & src ) {
        MonsterGenerator::generator().load_monster( jo, src );
    } );
    add( "SPECIES", []( const JsonObject & jo, const std::string & src ) {
        MonsterGenerator::generator().load_species( jo, src );
    } );
    add( "monster_flag", &mon_flag::load_mon_flags );

    add( "LOOT_ZONE", &zone_type::load_zones );
    add( "monster_adjustment", &load_monster_adjustment );
    add( "recipe_category", &load_recipe_category );
    add( "recipe",  &recipe_dictionary::load_recipe );
    add( "uncraft", &recipe_dictionary::load_uncraft );
    add( "practice", &recipe_dictionary::load_practice );
    add( "nested_category", &recipe_dictionary::load_nested_category );
    add( "recipe_group",  &recipe_group::load );

    add( "tool_quality", &quality::load_static );
    add( "technique", &load_technique );
    add( "weapon_category", &weapon_category::load_weapon_categories );
    add( "martial_art", &load_martial_art );
    add( "climbing_aid", &climbing_aid::load_climbing_aid );
    add( "attack_vector", &attack_vector::load_attack_vectors );
    add( "effect_type", &load_effect_type );
    add( "effect_migration", &effect_migration::load );
    add( "oter_id_migration", &overmap::load_oter_id_migration );
    add( "camp_migration", &overmap::load_oter_id_camp_migration );
    add( "overmap_terrain", &overmap_terrains::load );
    add( "oter_vision", &oter_vision::load_oter_vision );
    add( "construction_category", &construction_categories::load );
    add( "construction_group", &construction_groups::load );
    add( "construction", &load_construction );
    add( "mapgen", &load_mapgen );
    add( "overmap_land_use_code", &overmap_land_use_codes::load );
    add( "overmap_connection", &overmap_connections::load );
    add( "overmap_location", &overmap_locations::load );
    add( "city", &city::load_city );
    add( "overmap_special", &overmap_specials::load );
    add( "overmap_special_migration", &overmap_special_migration::load_migrations );
    add( "city_building", &city_buildings::load );
    add( "map_extra", &MapExtras::load );
    add( "omt_placeholder", &map_data_placeholders::load );

    add( "region_settings_river", &region_settings_river::load_region_settings_river );
    add( "region_settings_lake", &region_settings_lake::load_region_settings_lake );
    add( "region_settings_ocean", &region_settings_ocean::load_region_settings_ocean );
    add( "region_settings_ravine", &region_settings_ravine::load_region_settings_ravine );
    add( "region_settings_forest", &region_settings_forest::load_region_settings_forest );
    add( "region_settings_highway", &region_settings_highway::load_region_settings_highway );
    add( "region_settings_forest_trail",
         &region_settings_forest_trail::load_region_settings_forest_trail );
    add( "region_settings_city",
         &region_settings_city::load_region_settings_city );
    add( "region_settings_terrain_furniture",
         &region_settings_terrain_furniture::load_region_settings_terrain_furniture );
    add( "region_terrain_furniture",
         &region_terrain_furniture::load_region_terrain_furniture );
    add( "region_settings_forest_mapgen",
         &region_settings_forest_mapgen::load_region_settings_forest_mapgen );
    add( "region_settings_map_extras",
         &region_settings_map_extras::load_region_settings_map_extras );
    add( "forest_biome_component",
         &forest_biome_component::load_forest_biome_feature );
    add( "forest_biome_mapgen",
         &forest_biome_mapgen::load_forest_biome_mapgen );
    add( "map_extra_collection",
         &map_extra_collection::load_map_extra_collection );
    add( "region_settings", &region_settings::load_region_settings );
    add( "dimension", &dimension_world::load_dimensions );
    add( "dimension_region_layout", &dimension_region_layout::load_dimension_regions );

    add( "ITEM_BLACKLIST", []( const JsonObject & jo ) {
        item_controller->load_item_blacklist( jo );
    } );
    add( "TRAIT_BLACKLIST", []( const JsonObject & jo ) {
        mutation_branch::load_trait_blacklist( jo );
    } );

    add( "TRAIT_MIGRATION", []( const JsonObject & jo ) {
        mutation_branch::load_trait_migration( jo );
    } );

    // loaded earlier.
    add( "colordef", &load_ignored_type );
    // mod information, ignored, handled by the mod manager
    add( "MOD_INFO", &load_ignored_type );

    add( "faction", &faction_template::load );
    add( "npc", &npc_template::load_npc_template );
    add( "npc_class", &npc_class::load_npc_class );
    add( "talk_topic", &load_talk_topic );
    add( "behavior", &behavior::load_behavior );
    add( "npc_decision_category", &npc_decision_category::load_category );
    add( "npc_need", &npc_custom_needs::load );

    add( "MONSTER_FACTION", &monfactions::load_monster_faction );

    add( "sound_effect", &sfx::load_sound_effects );
    add( "sound_effect_preload", &sfx::load_sound_effect_preload );
    add( "playlist", &sfx::load_playlist );

    add( "gate", &gates::load );
    add( "overlay_order", &load_overlay_ordering );
    add( "mission_definition", []( const JsonObject & jo, const std::string & src ) {
        mission_type::load_mission_type( jo, src );
    } );
    add( "butchery_requirement", &butchery_requirements::load_butchery_req );
    add( "harvest_drop_type", &harvest_drop_type::load_harvest_drop_types );
    add( "harvest", &harvest_list::load_harvest_list );
    add( "monster_attack", []( const JsonObject & jo, const std::string & src ) {
        MonsterGenerator::generator().load_monster_attack( jo, src );
    } );
    add( "palette", mapgen_palette::load );
    add( "rotatable_symbol", &rotatable_symbols::load );
    add( "limb_score", &limb_score::load_limb_scores );
    add( "character_mod", &character_modifier::load_character_modifiers );
    add( "body_part", &body_part_type::load_bp );
    add( "sub_body_part", &sub_body_part_type::load_bp );
    add( "body_graph", &bodygraph::load_bodygraphs );
    add( "anatomy", &anatomy::load_anatomy );
    add( "morale_type", &morale_type_data::load_type );
    add( "SPELL", &spell_type::load_spell );
    add( "spell_migration", &spell_migration::load );
    add( "magic_type", &magic_type::load_magic_type );
    add( "clothing_mod", &clothing_mods::load );
    add( "ter_furn_transform", &ter_furn_transform::load_transform );
    add( "event_transformation", &event_transformation::load_transformation );
    add( "event_statistic", &event_statistic::load_statistic );
    add( "score", &score::load_score );
    add( "achievement", &achievement::load_achievement );
    add( "conduct", &achievement::load_achievement );
    add( "widget", &widget::load_widget );
    add( "weakpoint_set", &weakpoints::load_weakpoint_sets );
    add( "damage_type", &damage_type::load_damage_types );
    add( "damage_info_order", &damage_info_order::load_damage_info_orders );
    add( "wound", &wound_type::load_wounds );
    add( "wound_fix", &wound_fix::load_wound_fixes );
    add( "mod_migration", &mod_migrations::load );
    add( "faction_mission", &faction_mission::load_faction_missions );
#if defined(TILES)
    add( "mod_tileset", &load_mod_tileset );
#else
    // Dummy function
    add( "mod_tileset", load_ignored_type );
#endif
}

void DynamicDataLoader::load_data_from_path( const cata_path &path, const std::string &src )
{
    cata_assert( !finalized &&
                 "Can't load additional data after finalization.  Must be unloaded first." );
    // We assume that each folder is consistent in itself,
    // and all the previously loaded folders.
    // E.g. the core might provide a vpart "frame-x"
    // the first loaded mode might provide a vehicle that uses that frame
    // But not the other way round.

    std::vector<cata_path> files;
    if( dir_exist( path.get_unrelative_path() ) ) {
        const std::vector<cata_path> dir_files = get_files_from_path( ".json", path, true, true );
        files.insert( files.end(), dir_files.begin(), dir_files.end() );
    } else if( file_exist( path.get_unrelative_path() ) ) {
        files.emplace_back( path );
    }

    // iterate over each file
    for( const cata_path &file : files ) {
        try {
            // parse it
            JsonValue jsin = json_loader::from_path( file );
            load_all_from_json( jsin, src, path, file );
        } catch( const JsonError &err ) {
            throw std::runtime_error( err.what() );
        }
    }
}

void DynamicDataLoader::load_mod_data_from_path( const cata_path &path, const std::string &src )
{
    cata_assert( !finalized &&
                 "Can't load additional data after finalization.  Must be unloaded first." );
    // We assume that each folder is consistent in itself,
    // and all the previously loaded folders.
    // E.g. the core might provide a vpart "frame-x"
    // the first loaded mode might provide a vehicle that uses that frame
    // But not the other way round.

    // iterate over each file
    for( const cata_path &file : mod_data_files( path ) ) {
        try {
            // parse it
            JsonValue jsin = json_loader::from_path( file );
            load_all_from_json( jsin, src, path, file );
        } catch( const JsonError &err ) {
            throw std::runtime_error( err.what() );
        }
    }
}

std::vector<cata_path> DynamicDataLoader::mod_data_files( const cata_path &path )
{
    std::vector<cata_path> files;
    // if give path is a directory
    if( dir_exist( path.get_unrelative_path() ) ) {
        const std::vector<cata_path> dir_files = get_files_from_path_with_path_exclusion( ".json",
                "mod_interactions", path, true, true );
        files.insert( files.end(), dir_files.begin(), dir_files.end() );
        // if given path is an individual file
    } else if( file_exist( path.get_unrelative_path() ) ) {
        files.emplace_back( path );
    }
    return files;
}

std::vector<std::pair<mod_id, cata_path>> DynamicDataLoader::mod_interaction_files(
        const cata_path &path, const std::vector<mod_id> &mods )
{
    std::multimap<mod_id, cata_path> files;

    if( dir_exist( path.get_unrelative_path() ) ) {

        // obtain folders within mod_interactions to see if they match loaded mod ids
        const std::vector<cata_path> interaction_folders = get_directories( path, false );

        for( const cata_path &f : interaction_folders ) {
            const mod_id associated_mod = mod_id( f.get_unrelative_path().filename().string() );
            bool is_mod_loaded = std::find( mods.begin(), mods.end(), associated_mod ) != mods.end();

            if( is_mod_loaded ) {
                const std::vector<cata_path> interaction_files = get_files_from_path( ".json", f, true, true );
                for( const cata_path &path : interaction_files ) {
                    files.emplace( associated_mod, path );
                }
            }
        }
    }
    return std::vector<std::pair<mod_id, cata_path>>( files.begin(), files.end() );
}

void DynamicDataLoader::load_mod_interaction_files_from_path( const cata_path &path,
        const std::string &src )
{
    cata_assert( !finalized &&
                 "Can't load additional data after finalization.  Must be unloaded first." );

    const std::vector<mod_id> &loaded_mods = world_generator->active_world->active_mod_order;
    // iterate over each file
    for( const std::pair<mod_id, cata_path> &file : mod_interaction_files( path, loaded_mods ) ) {
        try {
            // parse it
            JsonValue jsin = json_loader::from_path( file.second );
            load_all_from_json( jsin, string_format( "%s#%s", src, file.first.str() ), path, file.second );
        } catch( const JsonError &err ) {
            throw std::runtime_error( err.what() );
        }
    }
}

void DynamicDataLoader::for_each_object( const JsonValue &jsin,
        const std::function<void( const JsonObject & )> &f )
{
    if( jsin.test_object() ) {
        // a single object
        f( jsin.get_object() );
    } else if( jsin.test_array() ) {
        JsonArray ja = jsin.get_array();
        // each object until array close
        for( JsonObject jo : ja ) {
            f( jo );
        }
    } else {
        // not an object or an array?
        jsin.throw_error( "expected object or array" );
    }
}

void DynamicDataLoader::load_all_from_json( const JsonValue &jsin, const std::string &src,
        const cata_path &base_path, const cata_path &full_path )
{
    // find type and dispatch each object
    for_each_object( jsin, [&]( const JsonObject & jo ) {
        load_object( jo, src, base_path, full_path );
    } );
    inp_mngr.pump_events();
}

void DynamicDataLoader::unload_data()
{
    finalized = false;
    object_load_history.clear();

    achievement::reset();
    activity_type::reset();
    add_type::reset();
    ammo_effects::reset();
    ammunition_type::reset();
    anatomy::reset();
    ascii_art::reset();
    attack_vector::reset();
    bash_damage_profile::reset();
    behavior::reset();
    npc_json_goals::reset();
    npc_custom_needs::reset();
    npc_decision_category::reset();
    body_part_type::reset();
    butchery_requirements::reset();
    sub_body_part_type::reset();
    bodygraph::reset();
    climbing_aid::reset();
    weapon_category::reset();
    clear_techniques_and_martial_arts();
    character_modifier::reset();
    clothing_mods::reset();
    construction_categories::reset();
    construction_groups::reset();
    damage_type::reset();
    damage_info_order::reset();
    dimension_world::reset();
    dimension_region_layout::reset();
    disease_type::reset();
    dreams.clear();
    emit::reset();
    end_screen::reset();
    get_help().reset();
    enchantment::reset();
    event_statistic::reset();
    effect_on_conditions::reset();
    executions::reset();
    abilities::reset();
    damage_rules::reset();
    powers::reset();
    execution_triggers::reset();
    if( g ) {
        g->executions().clear();
        g->abilities().clear();
    }
    event_transformation::reset();
    faction_mission::reset();
    faction_template::reset();
    faults::reset();
    pp_generators::reset();
    field_types::reset();
    field_type_migrations::reset();
    gates::reset();
    // var_migration entries, which are applied to the saves being loaded
    get_globals().migrations.clear();
    harvest_drop_type::reset();
    harvest_list::reset();
    item_category::reset();
    items::reset();
    reset_removal_blacklists();
    jmath_func::reset();
    json_flag::reset();
    connect_group::reset();
    limb_score::reset();
    mapgen_palette::reset();
    materials::reset();
    mission_type::reset();
    mod_migrations::reset();
    move_mode::reset();
    monfactions::reset();
    mon_flag::reset();
    MonsterGenerator::generator().reset();
    MonsterGroupManager::ClearMonsterGroups();
    morale_type_data::reset();
    mutation_branch::reset_all();
    mutation_category_trait::reset();
    mutations_category.clear();
    reset_mutation_types();
    npc_class::reset_npc_classes();
    npc_template::reset();
    option_slider::reset();
    mod_settings::reset();
    overmap_connections::reset();
    overmap_land_use_codes::reset();
    overmap_locations::reset();
    oter_vision::reset();
    city::reset();
    overmap_specials::reset();
    overmap_special_migration::reset();
    overmap_terrains::reset();
    overmap::reset_oter_id_migrations();
    overmap::reset_oter_id_camp_migrations();
    map_data_placeholders::reset();
    profession::reset();
    profession_blacklist::reset();
    profession_group::reset();
    proficiency::reset();
    proficiency_category::reset();
    proficiency_migration::reset();
    mood_face::reset();
    speed_description::reset();
    quality::reset();
    region_settings_river::reset();
    region_settings_lake::reset();
    region_settings_ocean::reset();
    region_settings_ravine::reset();
    region_settings_forest::reset();
    region_settings_highway::reset();
    region_settings_forest_trail::reset();
    region_settings_city::reset();
    region_settings_terrain_furniture::reset();
    region_terrain_furniture::reset();
    region_settings_forest_mapgen::reset();
    region_settings_map_extras::reset();
    forest_biome_component::reset();
    forest_biome_mapgen::reset();
    map_extra_collection::reset();
    region_settings::reset();
    reset_monster_adjustment();
    recipe_dictionary::reset();
    recipe_group::reset();
    relic_procgen_data::reset();
    requirement_data::reset();
    reset_bionics();
    reset_constructions();
    reset_effect_types();
    effect_migration::reset();
    reset_furn_ter();
    reset_mapgens();
    MapExtras::clear();
    reset_mod_tileset();
    reset_overlay_ordering();
    reset_recipe_categories();
    reset_scenarios_blacklist();
    reset_speech();
    rotatable_symbols::reset();
    scenario::reset();
    scent_type::reset();
    score::reset();
    shopkeeper_blacklist::reset();
    shopkeeper_whitelist::reset();
    shopkeeper_cons_rates::reset();
    Skill::reset();
    SNIPPET.clear_snippets();
    magic_type::reset_all();
    spell_type::reset_all();
    spell_migration::reset();
    start_locations::reset();
    ter_furn_migrations::reset();
    ter_furn_transform::reset();
    trap::reset();
    trap_migrations::reset();
    unload_talk_topics();
    VehicleGroup::reset();
    VehiclePlacement::reset();
    VehicleSpawn::reset();
    vehicles::reset_prototypes();
    vitamin::reset();
    vehicles::parts::reset();
    vpart_category::reset();
    vpart_location::reset();
    vpart_migration::reset();
    wound_type::reset();
    wound_fix::reset();
    weakpoints::reset();
    weather_generator::reset();
    weather_types::reset();
    widget::reset();
    zone_type::reset();
}

// void DynamicDataLoader::finalize_loaded_data()
// {
//     // Create a dummy that will not display anything
//     // TODO: Make it print to stdout?
//     background_pane bg;
//     finalize_loaded_data( );
// }

void DynamicDataLoader::finalize_loaded_data()
{
    cata_assert( !finalized && "Can't finalize the data twice." );
    cata_assert( !stream_cache && "Expected stream cache to be null before finalization" );

    on_out_of_scope reset_stream_cache( [this]() {
        stream_cache.reset();
    } );
    stream_cache = std::make_unique<cached_streams>();

    using named_entry = std::pair<std::string, std::function<void()>>;
    const std::vector<named_entry> entries = {{
            { _( "Flags" ), &json_flag::finalize_all },
            { _( "Option sliders" ), &option_slider::finalize_all },
            { _( "Executions" ), &executions::finalize },
            { _( "Abilities" ), &abilities::finalize },
            { _( "Damage rules" ), &damage_rules::finalize },
            { _( "Power ownership" ), &powers::finalize },
            { _( "Execution triggers" ), &execution_triggers::finalize },
            { _( "Mod settings" ), &mod_settings::finalize_all },
            { _( "Addictions" ), &add_type::finalize_all },
            { _( "ASCII Art" ), &ascii_art::finalize_all },
            { _( "Bash damage profiles" ), &bash_damage_profile::finalize_all },
            { _( "Body parts" ), &body_part_type::finalize_all },
            { _( "Sub body parts" ), &sub_body_part_type::finalize_all },
            { _( "Body graphs" ), &bodygraph::finalize_all },
            { _( "Bionics" ), &bionic_data::finalize_bionic },
            { _( "Butchery Requirements" ), &butchery_requirements::finalize_all },
            { _( "Character Modifiers" ), &character_modifier::finalize_all },
            { _( "Clothing Mods" ), &clothing_mod::finalize_all },
            { _( "Construction Categories" ), &construction_categories::finalize },
            { _( "Construction Groups" ), &construction_groups::finalize },
            { _( "Crafting Categories" ), &crafting_category::finalize_all },
            { _( "Damage Types" ), &damage_type::finalize_all },
            { _( "Damage info orders" ), &damage_info_order::finalize_all },
            { _( "Dimension region layouts" ), &dimension_region_layout::finalize_all },
            { _( "Dimensions" ), &dimension_world::finalize_all },
            { _( "Diseases" ), &disease_type::finalize_all },
            { _( "Weather types" ), &weather_types::finalize_all },
            { _( "Weather generators" ), &weather_generator::finalize_all },
            { _( "NPC templates" ), &npc_template::finalize_all },
            { _( "Effect on conditions" ), &effect_on_conditions::finalize_all },
            { _( "Field types" ), &field_types::finalize_all },
            { _( "Ammo effects" ), &ammo_effects::finalize_all },
            { _( "Emissions" ), &emit::finalize },
            { _( "End screens" ), &end_screen::finalize_all },
            { _( "Enchantments" ), &enchantment::finalize_all },
            { _( "Event Statistics" ), &event_statistic::finalize_all },
            { _( "Event Transformations" ), &event_transformation::finalize_all },
            { _( "Faults" ), &faults::finalize },
            { _( "Post-process generators" ), &pp_generators::finalize },
            { _( "Furniture" ), &finalize_furniture },
            { _( "Gates" ), &gates::finalize },
            { _( "Harvest Drop Types" ), &harvest_drop_type::finalize_all },
            { _( "Item Categories" ), &item_category::finalize_all },
            { _( "Materials" ), &material_type::finalize_all },
            { _( "Items" ), &items::finalize_all },
            { _( "Limb Scores" ), &limb_score::finalize_all },
            {
                _( "Crafting requirements" ), []()
                {
                    requirement_data::finalize();
                }
            },
            { _( "Vehicle part categories" ), &vpart_category::finalize_all },
            { _( "Vehicle parts" ), &vehicles::parts::finalize },
            { _( "Traps" ), &trap::finalize_all },
            { _( "Terrain" ), &set_ter_ids },
            { _( "Furniture" ), &set_furn_ids },
            { _( "Overmap land use codes" ), &overmap_land_use_codes::finalize },
            { _( "Overmap terrain" ), &overmap_terrains::finalize },
            { _( "Overmap connections" ), &overmap_connections::finalize },
            { _( "Overmap specials" ), &overmap_specials::finalize },
            { _( "Overmap locations" ), &overmap_locations::finalize },
            { _( "Cities" ), &city::finalize_all },
            { _( "Math functions" ), &jmath_func::finalize_all },
            { _( "Start locations" ), &start_locations::finalize_all },
            { _( "Vehicle part locations" ), &vpart_location::finalize_all },
            { _( "Vehicle part migrations" ), &vpart_migration::finalize },
            { _( "Vehicle prototypes" ), &vehicles::finalize_prototypes },
            { _( "Map Extras" ), &map_extra::finalize_all },
            { _( "Magic Types" ), &magic_type::finalize_all },
            { _( "Martial Arts" ), &martialart::finalize_all },
            { _( "Martial Art Techniques" ), &ma_technique::finalize_all },
            { _( "Monster Flags" ), &mon_flag::finalize_all },
            { _( "Mood Faces" ), &mood_face::finalize_all },
            { _( "Morale Types" ), &morale_type_data::finalize_all },
            { _( "Mapgen weights" ), &calculate_mapgen_weights },
            { _( "Mapgen parameters" ), &overmap_specials::finalize_mapgen_parameters },
            // before the behaviors, which add a leaf for each need
            { _( "NPC needs" ), &npc_custom_needs::finalize },
            { _( "Behaviors" ), &behavior::finalize },
            { _( "NPC decision categories" ), &npc_decision_category::finalize_all },
            {
                _( "Monster types" ), []()
                {
                    set_mon_flag_ids();
                    MonsterGenerator::generator().finalize_mtypes();
                }
            },
            { _( "Monster groups" ), &MonsterGroupManager::FinalizeMonsterGroups },
            { _( "Monster factions" ), &monfactions::finalize },
            { _( "Factions" ), &npc_factions::finalize },
            { _( "Move modes" ), &move_mode::finalize_all },
            { _( "Activities" ), &activity_type::finalize_all },
            { _( "Constructions" ), &finalize_constructions },
            { _( "Crafting recipes" ), &recipe_dictionary::finalize },
            { _( "Recipe groups" ), &recipe_group::check },
            { _( "Martial arts" ), &finalize_martial_arts },
            { _( "Scenarios" ), &scenario::finalize_all },
            { _( "Spells" ), &spell_type::finalize_all },
            { _( "Climbing aids" ), &climbing_aid::finalize_all },
            { _( "NPC classes" ), &npc_class::finalize_all },
            { _( "Overmap Vision Levels" ), &oter_vision::finalize_all },
            { _( "Overmap Special Migrations" ), &overmap_special_migration::finalize_all },
            { _( "Overmap placeholders" ), &map_data_placeholders::finalize },
            { _( "Professions" ), &profession::finalize_all },
            { _( "Profession groups" ), &profession_group::finalize_all },
            { _( "Proficiencies" ), &proficiency::finalize_all },
            { _( "Proficiency Categories" ), &proficiency_category::finalize_all },
            { _( "Qualities" ), &quality::finalize_all },
            { _( "Recipe Groups" ), &recipe_group::finalize },
            { _( "Region Settings" ), &region_settings::finalize_all },
            { _( "Relic Procedural Generations" ), &relic_procgen_data::finalize_all },
            { _( "Speed Descriptions" ), &speed_description::finalize_all },
            { _( "Species" ), &species_type::finalize_all },
            { _( "Scent Types" ), &scent_type::finalize_all },
            { _( "Scores" ), &score::finalize_all },
            { _( "Shopkeeper Blacklists" ), &shopkeeper_blacklist::finalize_all },
            { _( "Shopkeeper Whitelists" ), &shopkeeper_whitelist::finalize_all },
            { _( "Shopkeeper Consumption Rates" ), &shopkeeper_cons_rates::finalize_all },
            { _( "Terrain" ), &finalize_terrain },
            { _( "Terrain/Furniture Transforms" ), &ter_furn_transform::finalize_all },
            { _( "Missions" ), &mission_type::finalize_all },
            { _( "Faction missions" ), &faction_mission::finalize_all },
            { _( "Harvest lists" ), &harvest_list::finalize_all },
            { _( "Anatomies" ), &anatomy::finalize_all },
            { _( "Mutations" ), &mutation_branch::finalize_all },
            { _( "Achievements" ), &achievement::finalize_all },
            { _( "Widgets" ), &widget::finalize_all },
            { _( "Weakpoint Families" ), &weakpoints::finalize_all },
            { _( "Weapon Categories" ), &weapon_category::finalize_all },
            { _( "Wounds" ), &wound_type::finalize_all },
            { _( "Wound Fixes" ), &wound_fix::finalize_all },
            { _( "Zone Types" ), &zone_type::finalize_all },
            // after terrain, furniture, field types and vehicle parts
            { _( "Sound muffling" ), &sounds::finalize_muffling },
#if defined(TILES)
            { _( "Tileset" ), &load_tileset },
#endif
            { _( "Math expressions" ), &finalize_conditions },
        }
    };

    for( const named_entry &e : entries ) {
        loading_ui::show( _( "Finalizing" ), e.first );
        e.second();
    }
    // every deferred object has been replayed
    object_load_history.clear();

    if( !get_option<bool>( "SKIP_VERIFICATION" ) ) {
        check_consistency();
    }
    finalized = true;
}

void DynamicDataLoader::check_consistency()
{
    using named_entry = std::pair<std::string, std::function<void()>>;
    const std::vector<named_entry> entries = {{
            { _( "Flags" ), &json_flag::check_consistency },
            { _( "Option sliders" ), &option_slider::check_consistency },
            { _( "Mod settings" ), &mod_settings::check_consistency },
            {
                _( "Crafting requirements" ), []()
                {
                    requirement_data::check_consistency();
                }
            },
            { _( "Vitamins" ), &vitamin::check_consistency },
            { _( "Weather types" ), &weather_types::check_consistency },
            { _( "Weapon categories" ), &weapon_category::verify_weapon_categories },
            { _( "Effect on conditions" ), &effect_on_conditions::check_consistency },
            { _( "Field types" ), &field_types::check_consistency },
            { _( "Field type migrations" ), &field_type_migrations::check },
            { _( "Ammo effects" ), &ammo_effects::check_consistency },
            { _( "Emissions" ), &emit::check_consistency },
            { _( "Effect types" ), &effect_type::check_consistency },
            { _( "Effect migration" ), &effect_migration::check },
            { _( "Activities" ), &activity_type::check_consistency },
            { _( "Addiction types" ), &add_type::check_add_types },
            { _( "Bash damage profiles" ), &bash_damage_profile::check_all },
            { _( "Items" ), &items::check_consistency },
            { _( "Materials" ), &materials::check },
            { _( "Faults" ), &faults::check_consistency },
            { _( "Post-process generators" ), &pp_generators::check_consistency },
            { _( "Proficiency migration" ), &proficiency_migration::check },
            { _( "Vehicle parts" ), &vehicles::parts::check },
            { _( "Vehicle part locations" ), &vpart_location::check_all },
            { _( "Vehicle part migrations" ), &vpart_migration::check },
            { _( "Mapgen definitions" ), &check_mapgen_definitions },
            { _( "Mapgen palettes" ), &mapgen_palette::check_definitions },
            {
                _( "Monster types" ), []()
                {
                    MonsterGenerator::generator().check_monster_definitions();
                }
            },
            { _( "Monster groups" ), &MonsterGroupManager::check_group_definitions },
            { _( "Furniture and terrain" ), &check_furniture_and_terrain },
            { _( "Furniture and terrain migrations" ), &ter_furn_migrations::check },
            { _( "Constructions" ), &check_constructions },
            { _( "Crafting recipes" ), &recipe_dictionary::check_consistency },
            { _( "Professions" ), &profession::check_definitions },
            { _( "Profession groups" ), &profession_group::check_profession_group_consistency },
            { _( "Martial arts" ), &check_martialarts },
            { _( "Martial arts techniques" ), &ma_technique::verify_ma_techniques },
            { _( "Climbing aid" ), &climbing_aid::check_consistency },
            { _( "Mutations" ), &mutation_branch::check_consistency },
            { _( "Mutation categories" ), &mutation_category_trait::check_consistency },
            { _( "Mod migrations" ), &mod_migrations::check },
            { _( "Overmap land use codes" ), &overmap_land_use_codes::check_consistency },
            { _( "Overmap connections" ), &overmap_connections::check_consistency },
            { _( "Overmap terrain" ), &overmap_terrains::check_consistency },
            { _( "Overmap terrain vision" ), &oter_vision::check_oter_vision },
            { _( "Overmap locations" ), &overmap_locations::check_consistency },
            { _( "Cities" ), &city::check_consistency },
            { _( "Overmap specials" ), &overmap_specials::check_consistency },
            { _( "Map extras" ), &MapExtras::check_consistency },
            { _( "Scenarios" ), &scenario::check_all },
            { _( "Shop rates" ), &shopkeeper_cons_rates::check_all },
            { _( "Start locations" ), &start_locations::check_consistency },
            { _( "Ammunition types" ), &ammunition_type::check_consistency },
            { _( "Traps" ), &trap::check_consistency },
            { _( "Trap migrations" ), &trap_migrations::check },
            { _( "Bionics" ), &bionic_data::check_bionic_consistency },
            { _( "Gates" ), &gates::check },
            { _( "NPC classes" ), &npc_class::check_consistency },
            { _( "Behaviors" ), &behavior::check_consistency },
            { _( "NPC decision categories" ), &npc_decision_category::check_consistency },
            { _( "NPC needs" ), &npc_custom_needs::check_consistency },
            { _( "Mission types" ), &mission_type::check_consistency },
            {
                _( "Item actions" ), []()
                {
                    item_action_generator::generator().check_consistency();
                }
            },
            { _( "Butchery requirements" ), &butchery_requirements::check_consistency },
            { _( "Harvest lists" ), &harvest_list::check_consistency },
            { _( "NPC templates" ), &npc_template::check_consistency },
            { _( "Body parts" ), &body_part_type::check_consistency },
            { _( "Body graphs" ), &bodygraph::check_all },
            { _( "Anatomies" ), &anatomy::check_consistency },
            { _( "Magic types" ), &magic_type::check_consistency },
            { _( "Spells" ), &spell_type::check_consistency },
            { _( "Enchantments" ), &enchantment::check_consistency },
            { _( "Terrain/Furniture Transforms" ), &ter_furn_transform::check_all },
            { _( "Executions" ), &executions::check },
            { _( "Abilities" ), &abilities::check },
            { _( "Damage rules" ), &damage_rules::check },
            { _( "Power ownership" ), &powers::check },
            { _( "Execution triggers" ), &execution_triggers::check },
            { _( "Spell migration" ), &spell_migration::check },
            { _( "Transformations" ), &event_transformation::check_consistency },
            { _( "Statistics" ), &event_statistic::check_consistency },
            { _( "Scent types" ), &scent_type::check_scent_consistency },
            { _( "Scores" ), &score::check_consistency },
            { _( "Achievements" ), &achievement::check_consistency },
            { _( "Disease types" ), &disease_type::check_disease_consistency },
            { _( "Factions" ), &faction_template::check_consistency },
            { _( "Damage types" ), &damage_type::check },
            { _( "Wounds" ), &wound_type::check_consistency },
            { _( "Wound fixes" ), &wound_fix::check_consistency },
            { _( "Faction missions" ), &faction_mission::check_consistency },
            { _( "Relic Procedural Generations" ), &relic_procgen_data::check_consistency },
            { _( "Skills" ), &Skill::check_consistency }
        }
    };

    for( const named_entry &e : entries ) {
        loading_ui::show( _( "Verifying" ), e.first );
        e.second();
    }
}

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_path.h"
#include "dialogue.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "init.h"
#include "json_loader.h"
#include "mutation.h"
#include "npctalk.h"
#include "path_info.h"
#include "player_helpers.h"
#include "skill.h"
#include "speech.h"
#include "string_formatter.h"
#include "translation.h"
#include "type_id.h"
#include "vehicle_group.h"
#include "vitamin.h"

static const mutation_category_id mutation_category_AUDIT_HF_CAT_CHILD( "AUDIT_HF_CAT_CHILD" );
static const mutation_category_id
mutation_category_AUDIT_HF_CAT_OVERRIDE( "AUDIT_HF_CAT_OVERRIDE" );
static const mutation_category_id mutation_category_AUDIT_HF_CAT_PARENT( "AUDIT_HF_CAT_PARENT" );

static const skill_id skill_audit_hf_skill_child( "audit_hf_skill_child" );
static const skill_id skill_audit_hf_skill_override( "audit_hf_skill_override" );
static const skill_id skill_audit_hf_skill_parent( "audit_hf_skill_parent" );

static const vgroup_id VehicleGroup_audit_hf_vg_child( "audit_hf_vg_child" );
static const vgroup_id VehicleGroup_audit_hf_vg_merged( "audit_hf_vg_merged" );
static const vgroup_id VehicleGroup_audit_hf_vg_override( "audit_hf_vg_override" );
static const vgroup_id VehicleGroup_audit_hf_vg_parent( "audit_hf_vg_parent" );
static const vgroup_id VehicleGroup_car( "car" );

static const vitamin_id vitamin_audit_hf_vit_child( "audit_hf_vit_child" );
static const vitamin_id vitamin_audit_hf_vit_override( "audit_hf_vit_override" );
static const vitamin_id vitamin_audit_hf_vit_parent( "audit_hf_vit_parent" );

static const vproto_id vehicle_prototype_bicycle( "bicycle" );
static const vproto_id vehicle_prototype_car( "car" );
static const vproto_id vehicle_prototype_motorcycle( "motorcycle" );

namespace
{
// Exposes the loader every data file goes through, and the types it knows.
class factory_test_loader : public DynamicDataLoader
{
    public:
        using DynamicDataLoader::load_object;

        std::set<std::string> registered_types() const {
            std::set<std::string> types;
            for( const auto &entry : type_function_map ) {
                types.insert( entry.first );
            }
            return types;
        }
};

// Loads `json` as if a mod no other definition came from had it, so that it may redefine an
// id the test data defines (two definitions from one mod are an error).
void load_from_later_mod( const std::string &json )
{
    static int mods = 0;
    factory_test_loader loader;
    loader.load_object( json_loader::from_string( json ),
                        string_format( "audit_high_factory_mod_%d", mods++ ) );
}

const dream *find_dream( const std::string &id )
{
    for( const dream &d : dream::get_all() ) {
        if( d.id.str() == id ) {
            return &d;
        }
    }
    return nullptr;
}

std::vector<std::string> response_texts( const std::string &topic_id )
{
    std::vector<std::string> texts;
    const json_talk_topic *topic = get_talk_topic( topic_id );
    if( topic != nullptr ) {
        for( const json_talk_response &r : topic->get_responses() ) {
            texts.push_back( r.get_actual_response().truetext.translated() );
        }
    }
    return texts;
}
} // namespace

// DATA-J02: vitamins were rebuilt from scratch and a second definition was an error, so a mod
// couldn't change one member of a vanilla vitamin.  On the old code
// data/mods/TEST_DATA/audit_high_factory.json fails to load.
TEST_CASE( "data-j02_vitamins_support_copy_from", "[audit][json]" )
{
    const vitamin &parent = vitamin_audit_hf_vit_parent.obj();
    const vitamin &child = vitamin_audit_hf_vit_child.obj();
    SECTION( "a copy keeps what it doesn't change" ) {
        CHECK( child.name() == "audit child vitamin" );
        CHECK( child.max() == 200 );
        CHECK( parent.max() == 100 );
        CHECK( child.type() == vitamin_type::COUNTER );
        CHECK( child.rate() == 1_days );
        CHECK( child.severity( 55 ) == 1 );
    }
    SECTION( "extend and delete change the copied flags" ) {
        CHECK( parent.has_flag( "AUDIT_HF_A" ) );
        CHECK_FALSE( child.has_flag( "AUDIT_HF_A" ) );
        CHECK( child.has_flag( "AUDIT_HF_B" ) );
        CHECK( child.has_flag( "NO_DISPLAY" ) );
    }
    SECTION( "a later mod's copy of the same id changes one member" ) {
        load_from_later_mod( R"({ "type": "vitamin", "id": "audit_hf_vit_override",
            "copy-from": "audit_hf_vit_override", "max": 300 })" );
        const vitamin &changed = vitamin_audit_hf_vit_override.obj();
        CHECK( changed.max() == 300 );
        CHECK( changed.name() == "audit overridden vitamin" );
        CHECK( changed.rate() == 1_days );
        CHECK( changed.has_flag( "OBSOLETE" ) );
        REQUIRE( vitamin::all().count( vitamin_audit_hf_vit_override ) == 1 );
        CHECK( vitamin::all().at( vitamin_audit_hf_vit_override ).max() == 300 );
    }
}

// DATA-J02: a skill was rebuilt from scratch; a second definition replaced it and moved it to
// the end of the skill list.  The move is kept, so the skill list keeps its old order.
TEST_CASE( "data-j02_skills_support_copy_from", "[audit][json]" )
{
    const Skill &parent = skill_audit_hf_skill_parent.obj();
    const Skill &child = skill_audit_hf_skill_child.obj();
    SECTION( "a copy keeps what it doesn't change" ) {
        CHECK( child.name() == "audit child skill" );
        CHECK( child.description() == parent.description() );
        CHECK( child.get_sort_rank() == 900002 );
        CHECK( child.companion_combat_rank_factor() == 3 );
        CHECK( child.display_category() == parent.display_category() );
        // "time_to_attack" changes only the members it names
        CHECK( child.time_to_attack().min_time == 10 );
        CHECK( child.time_to_attack().base_time == 30 );
        CHECK( child.time_to_attack().time_reduction_per_level == 2 );
    }
    SECTION( "extend and delete change the copied lists" ) {
        CHECK( parent.is_combat_skill() );
        CHECK_FALSE( child.is_combat_skill() );
        CHECK( child.is_contextual_skill() );
        clear_avatar();
        avatar &u = get_avatar();
        CHECK( parent.can_chr_use( u ) );
        // the child requires FLEET, which the avatar lacks
        CHECK_FALSE( child.can_chr_use( u ) );
    }
    SECTION( "a later mod's copy of the same id changes one member" ) {
        load_from_later_mod( R"({ "type": "skill", "id": "audit_hf_skill_override",
            "copy-from": "audit_hf_skill_override", "sort_rank": 900004 })" );
        const Skill &changed = skill_audit_hf_skill_override.obj();
        CHECK( changed.get_sort_rank() == 900004 );
        CHECK( changed.name() == "audit overridden skill" );
        CHECK( changed.companion_survival_rank_factor() == 4 );
        CHECK( changed.is_contextual_skill() );
    }
    SECTION( "contextual skills stay out of the skill list" ) {
        CHECK( std::none_of( Skill::skills.begin(), Skill::skills.end(), []( const Skill & sk ) {
            return sk.is_contextual_skill();
        } ) );
    }
}

// DATA-J02: the vanilla skills keep the order of data/json/skills.json at the front of the skill
// list (no mod redefines a vanilla skill), as when every definition was appended.
TEST_CASE( "data-j02_vanilla_skills_keep_their_order", "[audit][json]" )
{
    std::vector<skill_id> vanilla;
    const JsonValue jv = json_loader::from_path( PATH_INFO::jsondir() / "skills.json" );
    for( JsonObject jo : jv.get_array() ) {
        jo.allow_omitted_members();
        if( jo.get_string( "type" ) != "skill" ) {
            continue;
        }
        const skill_id id( jo.get_string( "id" ) );
        if( !id->is_contextual_skill() ) {
            vanilla.push_back( id );
        }
    }
    REQUIRE( vanilla.size() <= Skill::skills.size() );
    for( size_t i = 0; i < vanilla.size(); ++i ) {
        CHECK( Skill::skills[i].ident() == vanilla[i] );
    }
}

// DATA-J02: a mutation category was rebuilt from scratch, so a mod changing its threshold had to
// restate the whole category.
TEST_CASE( "data-j02_mutation_categories_support_copy_from", "[audit][json]" )
{
    const mutation_category_trait &parent =
        mutation_category_trait::get_category( mutation_category_AUDIT_HF_CAT_PARENT );
    const mutation_category_trait &child =
        mutation_category_trait::get_category( mutation_category_AUDIT_HF_CAT_CHILD );
    SECTION( "a copy keeps what it doesn't change" ) {
        CHECK( child.name() == "audit child category" );
        CHECK( child.mutagen_message() == parent.mutagen_message() );
        CHECK( child.base_removal_chance == 50 );
        CHECK( child.wip );
        CHECK( child.threshold_min == 1500 );
        CHECK( parent.threshold_min == 1000 );
    }
    SECTION( "a later mod's copy of the same id changes one member" ) {
        load_from_later_mod( R"({ "type": "mutation_category", "id": "AUDIT_HF_CAT_OVERRIDE",
            "copy-from": "AUDIT_HF_CAT_OVERRIDE", "threshold_min": 1234 })" );
        const mutation_category_trait &changed =
            mutation_category_trait::get_category( mutation_category_AUDIT_HF_CAT_OVERRIDE );
        CHECK( changed.threshold_min == 1234 );
        CHECK( changed.name() == "audit overridden category" );
        CHECK( changed.base_removal_cost_mul == Approx( 2.5 ) );
        const auto &all = mutation_category_trait::get_all();
        REQUIRE( all.count( mutation_category_AUDIT_HF_CAT_OVERRIDE ) == 1 );
        CHECK( all.at( mutation_category_AUDIT_HF_CAT_OVERRIDE ).threshold_min == 1234 );
    }
}

// DATA-J02: a vehicle group could only be added to; it now takes copy-from, extend, delete and
// "override", and a second definition without copy-from still adds to the group.
TEST_CASE( "data-j02_vehicle_groups_support_copy_from", "[audit][json]" )
{
    using protos = std::vector<vproto_id>;
    SECTION( "a copy takes extend and delete" ) {
        CHECK( VehicleGroup_audit_hf_vg_parent->all_possible_results() ==
               protos{ vehicle_prototype_car, vehicle_prototype_bicycle } );
        CHECK( VehicleGroup_audit_hf_vg_child->all_possible_results() ==
               protos{ vehicle_prototype_car, vehicle_prototype_motorcycle } );
    }
    SECTION( "a second definition adds to the group, unless it overrides it" ) {
        CHECK( VehicleGroup_audit_hf_vg_merged->all_possible_results() ==
               protos{ vehicle_prototype_car, vehicle_prototype_bicycle } );
        CHECK( VehicleGroup_audit_hf_vg_override->all_possible_results() ==
               protos{ vehicle_prototype_bicycle } );
    }
    SECTION( "a later mod's definition can delete a vehicle and add another" ) {
        load_from_later_mod( R"({ "type": "vehicle_group", "id": "audit_hf_vg_override",
            "delete": { "vehicles": [ "bicycle" ] }, "vehicles": [ [ "car", 5 ] ] })" );
        CHECK( VehicleGroup_audit_hf_vg_override->all_possible_results() ==
               protos{ vehicle_prototype_car } );
        load_from_later_mod( R"({ "type": "vehicle_group", "id": "audit_hf_vg_override",
            "override": true, "vehicles": [ [ "bicycle", 20 ] ] })" );
        CHECK( VehicleGroup_audit_hf_vg_override->all_possible_results() ==
               protos{ vehicle_prototype_bicycle } );
    }
    SECTION( "every vehicle prototype is still a group of its own" ) {
        CHECK( VehicleGroup_car->all_possible_results() == protos{ vehicle_prototype_car } );
    }
}

// DATA-J02: the vanilla vehicle groups hold what their definitions in vehicle_groups.json list,
// every definition of an id adding to the group (mods add theirs after these).
TEST_CASE( "data-j02_vanilla_vehicle_groups_are_unchanged", "[audit][json]" )
{
    std::map<std::string, std::vector<vproto_id>> vanilla;
    const JsonValue jv = json_loader::from_path( PATH_INFO::jsondir() / "vehicle_groups.json" );
    for( JsonObject jo : jv.get_array() ) {
        jo.allow_omitted_members();
        if( jo.get_string( "type" ) != "vehicle_group" ) {
            continue;
        }
        std::vector<vproto_id> &listed = vanilla[jo.get_string( "id" )];
        for( JsonArray pair : jo.get_array( "vehicles" ) ) {
            listed.emplace_back( pair.get_string( 0 ) );
        }
    }
    REQUIRE( !vanilla.empty() );
    for( const std::pair<const std::string, std::vector<vproto_id>> &group : vanilla ) {
        if( vproto_id( group.first ).is_valid() ) {
            // a prototype of the same id adds itself too
            continue;
        }
        CAPTURE( group.first );
        const std::vector<vproto_id> results = vgroup_id( group.first )->all_possible_results();
        REQUIRE( results.size() >= group.second.size() );
        CHECK( std::equal( group.second.begin(), group.second.end(), results.begin() ) );
    }
}

// DATA-J02: dreams had no ids, so nothing could change or remove one; they now may have one, the
// vanilla ones do, and a dream without one is still added.
TEST_CASE( "data-j02_dreams_support_copy_from", "[audit][json]" )
{
    const dream *parent = find_dream( "audit_hf_dream_parent" );
    const dream *child = find_dream( "audit_hf_dream_child" );
    REQUIRE( parent != nullptr );
    REQUIRE( child != nullptr );
    SECTION( "a copy keeps what it doesn't change, and takes extend and delete" ) {
        CHECK( child->category == parent->category );
        CHECK( child->strength == 98 );
        CHECK( parent->messages() == std::vector<std::string> { "audit dream a", "audit dream b" } );
        CHECK( child->messages() == std::vector<std::string> { "audit dream b", "audit dream c" } );
    }
    SECTION( "a dream without an id is added" ) {
        const auto anonymous = std::find_if( dream::get_all().begin(), dream::get_all().end(),
        []( const dream & d ) {
            return d.strength == 97;
        } );
        REQUIRE( anonymous != dream::get_all().end() );
        CHECK( anonymous->messages() == std::vector<std::string> { "audit dream without id" } );
    }
}

// DATA-J02: the vanilla dreams got ids; they load as before, in the same order.
TEST_CASE( "data-j02_vanilla_dreams_are_unchanged", "[audit][json]" )
{
    const JsonValue jv = json_loader::from_path( PATH_INFO::jsondir() / "dreams.json" );
    size_t i = 0;
    for( JsonObject jo : jv.get_array() ) {
        jo.allow_omitted_members();
        REQUIRE( i < dream::get_all().size() );
        const dream &d = dream::get_all()[i++];
        CHECK( d.id.str() == jo.get_string( "id" ) );
        CHECK( d.category.str() == jo.get_string( "category" ) );
        CHECK( d.strength == jo.get_int( "strength" ) );
        CHECK( d.messages().size() == jo.get_array( "messages" ).size() );
        CHECK( d.id.str() == string_format( "dream_%s_%d", d.category.str(), d.strength ) );
    }
    CHECK( i > 0 );
}

// DATA-J02: speech lines had no ids either; one with an id takes copy-from, extend and delete.
TEST_CASE( "data-j02_speech_supports_copy_from", "[audit][json]" )
{
    SECTION( "a copy keeps what it doesn't change, and takes extend and delete" ) {
        // speaker a was deleted from the copy, so it has only the parent's line
        for( int i = 0; i < 10; ++i ) {
            CHECK( get_speech( "audit_hf_speaker_a" ).volume == 10 );
        }
        const SpeechBubble &copied = get_speech( "audit_hf_speaker_c" );
        CHECK( copied.volume == 20 );
        CHECK( copied.text.translated() == "audit parent line" );
    }
    SECTION( "a line without an id is added" ) {
        CHECK( get_speech( "audit_hf_speaker_d" ).text.translated() == "audit line without id" );
    }
    SECTION( "a later mod's copy of the same id changes one member" ) {
        load_from_later_mod( R"({ "type": "speech", "id": "audit_hf_speech_child",
            "copy-from": "audit_hf_speech_child", "volume": 30 })" );
        const SpeechBubble &changed = get_speech( "audit_hf_speaker_c" );
        CHECK( changed.volume == 30 );
        CHECK( changed.text.translated() == "audit parent line" );
        load_from_later_mod( R"({ "type": "speech", "id": "audit_hf_speech_child",
            "copy-from": "audit_hf_speech_child", "volume": 20 })" );
    }
}

// DATA-J02: a talk topic could only gain responses, and each definition without a
// portrait_override cleared the portrait an earlier one gave.
TEST_CASE( "data-j02_talk_topics_delete_and_replace_responses", "[audit][npc]" )
{
    SECTION( "delete removes responses by text or by topic" ) {
        CHECK( response_texts( "TALK_AUDIT_HF_DELETE" ) ==
               std::vector<std::string> { "Keep me.", "Added later." } );
    }
    SECTION( "replace_responses drops the earlier responses" ) {
        CHECK( response_texts( "TALK_AUDIT_HF_REPLACE" ) == std::vector<std::string> { "Only me." } );
    }
    SECTION( "a definition without portrait_override keeps the earlier portrait" ) {
        const json_talk_topic *topic = get_talk_topic( "TALK_AUDIT_HF_DELETE" );
        REQUIRE( topic != nullptr );
        REQUIRE( topic->get_portrait_override().has_value() );
        CHECK( topic->get_portrait_override()->str() == "audit_hf_portrait" );
    }
    SECTION( "deleting a response the topic doesn't have is an error" ) {
        const JsonObject jo = json_loader::from_string( R"({ "id": "TALK_AUDIT_HF_REPLACE",
            "delete": { "responses": [ { "text": "Not there." } ] }, "responses": [] })" );
        CHECK_THROWS_AS( load_talk_topic( jo, "audit_high_factory_mod" ), JsonError );
        jo.allow_omitted_members();
        CHECK( response_texts( "TALK_AUDIT_HF_REPLACE" ) == std::vector<std::string> { "Only me." } );
    }
}

// DATA-J02: JSON_INHERITANCE.md listed some types that took copy-from, "non-exhaustive", and
// told modders to search the C++.  Its table now lists every type, and must say what
// DynamicDataLoader::inheritance_support() says, which must list every type the loader knows.
TEST_CASE( "data-j02_inheritance_table_matches_the_loader", "[audit][json]" )
{
    const std::map<std::string, json_inheritance> &support =
        DynamicDataLoader::inheritance_support();
    SECTION( "every loadable type is classified, and only those" ) {
        const factory_test_loader loader;
        std::set<std::string> classified;
        for( const std::pair<const std::string, json_inheritance> &entry : support ) {
            classified.insert( entry.first );
        }
        CHECK( loader.registered_types() == classified );
    }
    SECTION( "the documented table says the same" ) {
        const std::string doc = read_entire_file( PATH_INFO::datadir() +
                                "../doc/JSON/JSON_INHERITANCE.md" );
        if( doc.empty() ) {
            WARN( "doc/JSON/JSON_INHERITANCE.md not found next to the data folder; skipped" );
            return;
        }
        const std::map<std::string, json_inheritance> names = {
            { "copy-from", json_inheritance::copy_from },
            { "partial", json_inheritance::partial },
            { "merges", json_inheritance::merges },
            { "none", json_inheritance::none },
        };
        std::map<std::string, json_inheritance> documented;
        std::istringstream lines( doc );
        std::string line;
        while( std::getline( lines, line ) ) {
            // | `type` | support |
            if( line.rfind( "| `", 0 ) != 0 ) {
                continue;
            }
            const size_t type_end = line.find( '`', 3 );
            const size_t cell = line.find( "| ", type_end );
            const size_t cell_end = line.find( " |", cell + 2 );
            REQUIRE( cell_end != std::string::npos );
            const std::string type = line.substr( 3, type_end - 3 );
            if( type == "type" ) {
                continue;
            }
            const auto level = names.find( line.substr( cell + 2, cell_end - cell - 2 ) );
            CAPTURE( line );
            REQUIRE( level != names.end() );
            CHECK( documented.emplace( type, level->second ).second );
        }
        CHECK( documented == support );
    }
    SECTION( "the types moved onto generic_factory are listed as such" ) {
        for( const char *type : {
                 "vitamin", "skill", "mutation_category", "dream", "speech", "npc", "faction",
                 "effect_type", "activity_type"
             } ) {
            CAPTURE( type );
            CHECK( support.at( type ) == json_inheritance::copy_from );
        }
        CHECK( support.at( "vehicle_group" ) == json_inheritance::partial );
        CHECK( support.at( "talk_topic" ) == json_inheritance::merges );
    }
}

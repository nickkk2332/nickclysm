#include <stddef.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "generic_factory.h"
#include "global_vars.h"
#include "item.h"
#include "json.h"
#include "json_loader.h"
#include "math_parser_diag_value.h"
#include "mission.h"
#include "monster.h"
#include "mtype.h"
#include "player_activity.h"
#include "profession.h"
#include "scenario.h"
#include "type_id.h"
#include "weighted_list.h"

static const activity_id ACT_AUDIT_M04A_GONE( "ACT_AUDIT_M04A_GONE" );
static const activity_id ACT_AUDIT_M04A_OLD( "ACT_AUDIT_M04A_OLD" );
static const activity_id ACT_TIDY_UP( "ACT_TIDY_UP" );
static const activity_id ACT_WAIT( "ACT_WAIT" );

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M04A_GONE( "EOC_AUDIT_M04A_GONE" );
static const effect_on_condition_id effect_on_condition_EOC_AUDIT_M04A_NEW( "EOC_AUDIT_M04A_NEW" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M04A_NOT_LOADED( "EOC_AUDIT_M04A_NOT_LOADED" );
static const effect_on_condition_id effect_on_condition_EOC_AUDIT_M04A_OLD( "EOC_AUDIT_M04A_OLD" );

static const itype_id itype_hammer( "hammer" );

static const mission_type_id mission_AUDIT_M04A_CHILD( "AUDIT_M04A_CHILD" );
static const mission_type_id mission_AUDIT_M04A_READY( "AUDIT_M04A_READY" );
static const mission_type_id mission_AUDIT_M04A_WAITING( "AUDIT_M04A_WAITING" );

static const mtype_id mon_audit_m04a_gone( "mon_audit_m04a_gone" );
static const mtype_id mon_audit_m04a_old( "mon_audit_m04a_old" );
static const mtype_id mon_zombie( "mon_zombie" );

static const profession_id profession_cop( "cop" );
static const profession_id profession_unemployed( "unemployed" );

static const string_id<scenario> scenario_evacuee( "evacuee" );
static const string_id<scenario> scenario_largebuilding( "largebuilding" );
static const string_id<scenario> scenario_refugeecenter( "refugeecenter" );

static JsonObject json_object( const std::string &json )
{
    return json_loader::from_string( json ).get_object();
}

// DATA-J04: the mandatory() without a reader refused relative, proportional, extend and delete
// even when the object had copy-from, so a copy couldn't adjust an inherited mandatory number.
TEST_CASE( "data-j04_a_copy_adjusts_an_inherited_mandatory_member", "[audit][json]" )
{
    SECTION( "mandatory() itself" ) {
        int value = 10;
        mandatory( json_object( R"({ "relative": { "value": 2 } })" ), true, "value", value );
        CHECK( value == 12 );
        mandatory( json_object( R"({ "proportional": { "value": 1.5 } })" ), true, "value", value );
        CHECK( value == 18 );
        // inherited and not given: kept
        mandatory( json_object( "{}" ), true, "value", value );
        CHECK( value == 18 );
        // not inherited: still required, and still takes no relative
        CHECK_THROWS_AS( mandatory( json_object( R"({ "relative": { "value": 2 } })" ), false,
                                    "value", value ), JsonError );
    }
    SECTION( "a mission copied with relative difficulty and proportional value" ) {
        generic_factory<mission_type> factory( "audit mission type" );
        on_out_of_scope reset_factory( [&factory]() {
            factory.reset();
        } );
        factory.load( json_object( R"({ "id": "AUDIT_M04A_BASE", "name": { "str": "base" },
            "goal": "MGOAL_NULL", "difficulty": 3, "value": 500 })" ), "test" );
        factory.load( json_object( R"({ "id": "AUDIT_M04A_CHILD", "copy-from": "AUDIT_M04A_BASE",
            "relative": { "difficulty": 2 }, "proportional": { "value": 1.5 } })" ), "test" );
        REQUIRE( factory.is_valid( mission_AUDIT_M04A_CHILD ) );
        const mission_type &child = factory.obj( mission_AUDIT_M04A_CHILD );
        CHECK( child.difficulty == 5 );
        CHECK( child.value == 750 );
    }
}

// DATA-J06: load_is_bool tested a load() with no arguments, so it was always false, and a
// mission whose inline update_mapgen named something not loaded yet was inserted half-loaded
// instead of being loaded again once everything was there.
TEST_CASE( "data-j06_a_mission_waiting_on_a_forward_reference_is_deferred", "[audit][json]" )
{
    STATIC_REQUIRE( generic_factory<mission_type>::load_is_bool<mission_type>::value );
    STATIC_REQUIRE_FALSE(
        generic_factory<effect_on_condition>::load_is_bool<effect_on_condition>::value );

    generic_factory<mission_type> factory( "audit mission type" );
    // the deferred object is dropped with the factory
    on_out_of_scope reset_factory( [&factory]() {
        factory.reset();
    } );
    factory.load( json_object( R"({ "id": "AUDIT_M04A_WAITING", "name": { "str": "waiting" },
        "goal": "MGOAL_NULL", "difficulty": 1, "value": 1,
        "start": { "update_mapgen": { "set": [
            { "point": "terrain", "id": "t_audit_m04a_not_loaded", "x": 1, "y": 1 } ] } },
        "monster_kill_goal": 7 })" ), "test" );
    CHECK_FALSE( factory.is_valid( mission_AUDIT_M04A_WAITING ) );

    factory.load( json_object( R"({ "id": "AUDIT_M04A_READY", "name": { "str": "ready" },
        "goal": "MGOAL_NULL", "difficulty": 1, "value": 1,
        "start": { "update_mapgen": { "set": [
            { "point": "terrain", "id": "t_floor", "x": 1, "y": 1 } ] } },
        "monster_kill_goal": 7 })" ), "test" );
    REQUIRE( factory.is_valid( mission_AUDIT_M04A_READY ) );
    // the members after the phases were read too
    CHECK( factory.obj( mission_AUDIT_M04A_READY ).monster_kill_goal == 7 );
}

// DATA-J05: var_migration only renamed creature and global variables, renamed instead of erasing
// without "to", and had no scope or prefix.  Fixtures in data/mods/TEST_DATA/audit_m_m04a.json.
TEST_CASE( "data-j05_var_migrations_rename_or_erase_saved_variables", "[audit][json]" )
{
    const global_variables &globals = get_globals();
    SECTION( "rename, erase, prefix and scope" ) {
        global_variables::impl_t vars;
        vars.emplace( "audit_m04a_old_var", diag_value( 1.0 ) );
        vars.emplace( "audit_m04a_pre_a", diag_value( 2.0 ) );
        vars.emplace( "audit_m04a_pre_b", diag_value( 3.0 ) );
        vars.emplace( "audit_m04a_doomed_var", diag_value( 4.0 ) );
        vars.emplace( "audit_m04a_kept", diag_value( 5.0 ) );
        global_variables::impl_t character_vars = vars;
        globals.migrate_vars( character_vars, var_scope::character );
        // the old_var migration is for items only
        CHECK( character_vars.count( "audit_m04a_old_var" ) == 1 );
        CHECK( character_vars.count( "audit_m04a_new_var" ) == 0 );
        CHECK( character_vars.count( "audit_m04a_pre_a" ) == 0 );
        CHECK( character_vars.count( "audit_m04a_post_a" ) == 1 );
        CHECK( character_vars.count( "audit_m04a_post_b" ) == 1 );
        CHECK( character_vars.count( "audit_m04a_doomed_var" ) == 0 );
        CHECK( character_vars.count( "NULL_VALUE" ) == 0 );
        CHECK( character_vars.count( "audit_m04a_kept" ) == 1 );
        CHECK( character_vars.size() == 4 );

        global_variables::impl_t item_vars = vars;
        globals.migrate_vars( item_vars, var_scope::item );
        CHECK( item_vars.count( "audit_m04a_old_var" ) == 0 );
        CHECK( item_vars.count( "audit_m04a_new_var" ) == 1 );
    }
    SECTION( "a variable already saved under the new name keeps its value" ) {
        global_variables::impl_t vars;
        vars.emplace( "audit_m04a_pre_a", diag_value( 2.0 ) );
        vars.emplace( "audit_m04a_post_a", diag_value( 9.0 ) );
        globals.migrate_vars( vars, var_scope::global );
        REQUIRE( vars.size() == 1 );
        CHECK( vars.at( "audit_m04a_post_a" ) == diag_value( 9.0 ) );
    }
    SECTION( "item variables are migrated when an item loads" ) {
        item hammer( itype_hammer );
        hammer.set_var( "audit_m04a_old_var", 5 );
        // removed by the vanilla var_migrations that replaced a list in the code
        hammer.set_var( "item_label_type", "x" );
        hammer.set_var( "audit_m04a_kept", 6 );
        std::ostringstream os;
        JsonOut jsout( os );
        hammer.serialize( jsout );
        item loaded;
        loaded.deserialize( json_object( os.str() ) );
        CHECK_FALSE( loaded.has_var( "audit_m04a_old_var" ) );
        CHECK( loaded.has_var( "audit_m04a_new_var" ) );
        CHECK_FALSE( loaded.has_var( "item_label_type" ) );
        CHECK( loaded.has_var( "audit_m04a_kept" ) );
    }
    SECTION( "an unknown scope is an error" ) {
        CHECK_THROWS_AS( global_variables::load_migrations( json_object(
                             R"({ "from": "audit_m04a_never", "scope": [ "nowhere" ] })" ), "test" ), JsonError );
        CHECK( globals.migrations.count( "audit_m04a_never" ) == 0 );
    }
}

// DATA-J07: extend of a map key already there threw, extend ran before delete so the pair
// couldn't replace an entry, and containers took no proportional (nor weighted lists relative).
TEST_CASE( "data-j07_extend_delete_relative_proportional_on_containers", "[audit][json]" )
{
    SECTION( "map: extend replaces, delete and extend replace, relative and proportional" ) {
        const generic_map_reader<std::string, int> reader;
        std::map<std::string, int> m = { { "a", 1 }, { "b", 2 } };
        optional( json_object( R"({ "extend": { "m": { "a": 5, "c": 3 } } })" ), true, "m", m,
                  reader );
        CHECK( m == std::map<std::string, int> { { "a", 5 }, { "b", 2 }, { "c", 3 } } );
        optional( json_object( R"({ "delete": { "m": { "c": 0 } }, "extend": { "m": { "c": 7 } } })" ),
                  true, "m", m, reader );
        CHECK( m.at( "c" ) == 7 );
        optional( json_object( R"({ "relative": { "m": { "a": 3 } } })" ), true, "m", m, reader );
        CHECK( m.at( "a" ) == 8 );
        optional( json_object( R"({ "proportional": { "m": 2 } })" ), true, "m", m, reader );
        CHECK( m == std::map<std::string, int> { { "a", 16 }, { "b", 4 }, { "c", 14 } } );
        optional( json_object( R"({ "proportional": { "m": { "b": 0.5 } } })" ), true, "m", m, reader );
        CHECK( m == std::map<std::string, int> { { "a", 16 }, { "b", 2 }, { "c", 14 } } );
        CHECK_THROWS_AS( optional( json_object( R"({ "proportional": { "m": { "z": 0.5 } } })" ),
                                   true, "m", m, reader ), JsonError );
    }
    SECTION( "set: delete and extend of the same flag keep it" ) {
        std::set<std::string> flags = { "A", "B" };
        optional( json_object( R"({ "delete": { "f": [ "A" ] }, "extend": { "f": [ "A", "C" ] } })" ),
                  true, "f", flags, string_reader{} );
        CHECK( flags == std::set<std::string> { "A", "B", "C" } );
    }
    SECTION( "weighted list: relative and proportional on weights" ) {
        const weighted_string_id_reader<std::string, int> reader( 1 );
        weighted_int_list<std::string> w;
        w.add( "a", 10 );
        w.add( "b", 4 );
        optional( json_object( R"({ "relative": { "w": { "a": 5 } } })" ), true, "w", w, reader );
        CHECK( w.get_specific_weight( "a" ) == 15 );
        optional( json_object( R"({ "proportional": { "w": 2 } })" ), true, "w", w, reader );
        CHECK( w.get_specific_weight( "a" ) == 30 );
        CHECK( w.get_specific_weight( "b" ) == 8 );
        optional( json_object( R"({ "proportional": { "w": { "b": 0.5 } } })" ), true, "w", w, reader );
        CHECK( w.get_specific_weight( "b" ) == 4 );
        CHECK( w.get_weight() == 34 );
        // a weight taken to 0 removes the entry
        optional( json_object( R"({ "relative": { "w": { "b": -4 } } })" ), true, "w", w, reader );
        CHECK( w.get_specific_weight( "b" ) == 0 );
        CHECK( w.get_weight() == 30 );
    }
}

namespace
{
// Holds the avatar's EOC queues and the global ones aside while a test runs, and puts them back
class m04a_eoc_queues_aside
{
    public:
        m04a_eoc_queues_aside() {
            avatar &u = get_avatar();
            avatar_queue = std::move( u.queued_effect_on_conditions );
            u.queued_effect_on_conditions = queued_eocs();
            avatar_inactive.swap( u.inactive_effect_on_condition_vector );
            global_queue = std::move( g->queued_global_effect_on_conditions );
            g->queued_global_effect_on_conditions = queued_eocs();
            global_inactive.swap( g->inactive_global_effect_on_condition_vector );
        }
        ~m04a_eoc_queues_aside() {
            avatar &u = get_avatar();
            u.queued_effect_on_conditions = std::move( avatar_queue );
            u.inactive_effect_on_condition_vector = std::move( avatar_inactive );
            g->queued_global_effect_on_conditions = std::move( global_queue );
            g->inactive_global_effect_on_condition_vector = std::move( global_inactive );
        }
        m04a_eoc_queues_aside( const m04a_eoc_queues_aside & ) = delete;
        m04a_eoc_queues_aside &operator=( const m04a_eoc_queues_aside & ) = delete;
    private:
        queued_eocs avatar_queue;
        std::vector<effect_on_condition_id> avatar_inactive;
        queued_eocs global_queue;
        std::vector<effect_on_condition_id> global_inactive;
};

bool queue_has( queued_eocs queue, const effect_on_condition_id &id )
{
    while( !queue.empty() ) {
        if( queue.top().eoc == id ) {
            return true;
        }
        queue.pop();
    }
    return false;
}
} // namespace

// DATA-J08: monsters, activities and EOCs had no JSON migration, and a queued EOC that was no
// longer loaded was dereferenced (a debugmsg) before its validity was checked.
TEST_CASE( "data-j08_eoc_migration_and_stale_queued_eocs", "[audit][json][eoc]" )
{
    CHECK( effect_on_conditions::migrate_saved( effect_on_condition_EOC_AUDIT_M04A_OLD ) ==
           effect_on_condition_EOC_AUDIT_M04A_NEW );
    CHECK_FALSE( effect_on_conditions::migrate_saved( effect_on_condition_EOC_AUDIT_M04A_GONE ) );
    CHECK( effect_on_conditions::migrate_saved( effect_on_condition_EOC_AUDIT_M04A_NEW ) ==
           effect_on_condition_EOC_AUDIT_M04A_NEW );
    std::vector<effect_on_condition_id> inactive = {
        effect_on_condition_EOC_AUDIT_M04A_OLD, effect_on_condition_EOC_AUDIT_M04A_GONE,
        effect_on_condition_EOC_AUDIT_M04A_NEW
    };
    effect_on_conditions::migrate_saved( inactive );
    CHECK( inactive == std::vector<effect_on_condition_id> {
        effect_on_condition_EOC_AUDIT_M04A_NEW, effect_on_condition_EOC_AUDIT_M04A_NEW
    } );

    // A queued EOC that isn't loaded (its mod was removed) is dropped without a debugmsg
    const m04a_eoc_queues_aside aside;
    avatar &u = get_avatar();
    REQUIRE_FALSE( effect_on_condition_EOC_AUDIT_M04A_NOT_LOADED.is_valid() );
    u.queued_effect_on_conditions.push( queued_eoc{ effect_on_condition_EOC_AUDIT_M04A_NOT_LOADED,
                                        calendar::turn, {} } );
    g->queued_global_effect_on_conditions.push( queued_eoc{ effect_on_condition_EOC_AUDIT_M04A_NOT_LOADED,
            calendar::turn, {} } );
    effect_on_conditions::load_existing_character( u );
    CHECK_FALSE( queue_has( u.queued_effect_on_conditions,
                            effect_on_condition_EOC_AUDIT_M04A_NOT_LOADED ) );
    CHECK_FALSE( queue_has( g->queued_global_effect_on_conditions,
                            effect_on_condition_EOC_AUDIT_M04A_NOT_LOADED ) );
}

TEST_CASE( "data-j08_monster_migration_renames_or_removes", "[audit][json][monster]" )
{
    CHECK( monster_migrations::migrate( mon_audit_m04a_old ) == mon_zombie );
    CHECK( monster_migrations::migrate( mon_zombie ) == mon_zombie );
    CHECK_FALSE( monster_migrations::removes( mon_audit_m04a_old ) );
    CHECK( monster_migrations::removes( mon_audit_m04a_gone ) );

    const monster zombie( mon_zombie );
    std::ostringstream os;
    JsonOut jsout( os );
    zombie.serialize( jsout );
    const std::string saved = os.str();
    const std::string type_key = "\"typeid\":\"mon_zombie\"";
    const size_t at = saved.find( type_key );
    REQUIRE( at != std::string::npos );

    std::string renamed = saved;
    renamed.replace( at, type_key.size(), "\"typeid\":\"mon_audit_m04a_old\"" );
    const JsonObject renamed_jo = json_object( renamed );
    CHECK_FALSE( monster_migrations::removes( renamed_jo ) );
    monster loaded;
    loaded.deserialize( renamed_jo );
    CHECK( loaded.type->id == mon_zombie );

    std::string removed = saved;
    removed.replace( at, type_key.size(), "\"typeid\":\"mon_audit_m04a_gone\"" );
    // the loaders skip it; its members count as read
    CHECK( monster_migrations::removes( json_object( removed ) ) );
}

TEST_CASE( "data-j08_activity_migration_renames_or_drops", "[audit][json][activity]" )
{
    CHECK( activity_type::migrate_saved( ACT_AUDIT_M04A_OLD ) == ACT_WAIT );
    CHECK( activity_type::migrate_saved( ACT_WAIT ) == ACT_WAIT );
    CHECK_FALSE( activity_type::migrate_saved( ACT_AUDIT_M04A_GONE ) );
    // the obsolete activities the code listed are vanilla activity_migrations now
    CHECK_FALSE( activity_type::migrate_saved( ACT_TIDY_UP ) );

    player_activity dropped;
    dropped.deserialize( json_object( R"({ "type": "ACT_AUDIT_M04A_GONE", "moves_left": 10 })" ) );
    CHECK( dropped.is_null() );
    player_activity obsolete;
    obsolete.deserialize( json_object( R"({ "type": "ACT_TIDY_UP", "moves_left": 10 })" ) );
    CHECK( obsolete.is_null() );
}

// DATA-J09: each SCENARIO_BLACKLIST and profession_blacklist replaced every earlier one, so a
// mod's blacklist unhid the base game's.
TEST_CASE( "data-j09_scenario_and_profession_blacklists_combine", "[audit][json]" )
{
    SECTION( "blacklists add up" ) {
        scen_blacklist lists;
        lists.load( json_object( R"({ "subtype": "blacklist", "scenarios": [ "evacuee" ] })" ), "test" );
        lists.load( json_object( R"({ "subtype": "blacklist", "scenarios": [ "largebuilding" ] })" ),
                    "test" );
        lists.finalize();
        CHECK( lists.scenarios.count( scenario_evacuee ) == 1 );
        CHECK( lists.scenarios.count( scenario_largebuilding ) == 1 );
        CHECK( lists.scenarios.count( scenario_refugeecenter ) == 0 );
    }
    SECTION( "a whitelist wins over blacklists, before or after it" ) {
        scen_blacklist lists;
        lists.load( json_object( R"({ "subtype": "blacklist", "scenarios": [ "evacuee" ] })" ), "test" );
        lists.load( json_object(
                        R"({ "subtype": "whitelist", "scenarios": [ "evacuee", "largebuilding" ] })" ), "test" );
        lists.load( json_object( R"({ "subtype": "blacklist", "scenarios": [ "largebuilding" ] })" ),
                    "test" );
        lists.finalize();
        CHECK( lists.scenarios.count( scenario_evacuee ) == 0 );
        CHECK( lists.scenarios.count( scenario_largebuilding ) == 0 );
        CHECK( lists.scenarios.count( scenario_refugeecenter ) == 1 );
    }
    SECTION( "a later whitelist replaces an earlier one" ) {
        scen_blacklist lists;
        lists.load( json_object( R"({ "subtype": "whitelist", "scenarios": [ "evacuee" ] })" ), "test" );
        lists.load( json_object( R"({ "subtype": "whitelist", "scenarios": [ "largebuilding" ] })" ),
                    "test" );
        lists.finalize();
        CHECK( lists.scenarios.count( scenario_evacuee ) == 1 );
        CHECK( lists.scenarios.count( scenario_largebuilding ) == 0 );
    }
    SECTION( "profession blacklists add up, and a whitelist wins" ) {
        profession_blacklist lists;
        lists.load( json_object( R"({ "subtype": "blacklist", "professions": [ "cop" ] })" ), "test" );
        lists.load( json_object( R"({ "subtype": "blacklist", "professions": [ "unemployed" ] })" ),
                    "test" );
        CHECK( lists.hides( profession_cop ) );
        CHECK( lists.hides( profession_unemployed ) );
        lists.load( json_object( R"({ "subtype": "whitelist", "professions": [ "cop" ] })" ), "test" );
        lists.load( json_object( R"({ "subtype": "blacklist", "professions": [ "cop" ] })" ), "test" );
        CHECK_FALSE( lists.hides( profession_cop ) );
        CHECK( lists.hides( profession_unemployed ) );
    }
}

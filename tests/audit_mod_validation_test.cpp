#include <map>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "flexbuffer_json.h"
#include "json_error.h"
#include "json_loader.h"
#include "mongroup.h"
#include "player_helpers.h"
#include "type_id.h"

static const mongroup_id GROUP_AUDIT_MONDT_J01( "GROUP_AUDIT_MONDT_J01" );

static const mtype_id mon_zombie( "mon_zombie" );

static const trait_id trait_NIGHTVISION( "NIGHTVISION" );

// Loads a throwaway monster group from JSON. The caller removes it again with
// remove_audit_group() so it cannot leak into other tests.
static void load_audit_group( const std::string &json )
{
    JsonObject jo = json_loader::from_string( json );
    MonsterGroupManager::LoadMonsterGroup( jo );
}

static void remove_audit_group()
{
    MonsterGroupManager::Get_all_Groups().erase( GROUP_AUDIT_MONDT_J01 );
}

static std::string group_with_conditions( const std::string &conditions )
{
    return R"({ "id": "GROUP_AUDIT_MONDT_J01", "default": "mon_null", "monsters": [
        { "monster": "mon_zombie", "weight": 10, "conditions": )" + conditions + " } ] }";
}

// MONDT-J01: unknown "conditions" tags were ignored, so a typo such as "Night" made the
// entry spawn at all times instead of failing to load.
TEST_CASE( "mondt-j01_unknown_spawn_condition_fails_to_load", "[audit][monster]" )
{
    SECTION( "misspelled tag" ) {
        CHECK_THROWS_AS( load_audit_group( group_with_conditions( R"([ "Night" ])" ) ),
                         JsonError );
        remove_audit_group();
    }
    SECTION( "unknown tag next to valid ones" ) {
        CHECK_THROWS_AS( load_audit_group( group_with_conditions( R"([ "DAY", "SUMMMER" ])" ) ),
                         JsonError );
        remove_audit_group();
    }
    SECTION( "every valid tag still loads" ) {
        const std::string all_tags =
            R"([ "DAY", "NIGHT", "DUSK", "DAWN", "SPRING", "SUMMER", "AUTUMN", "WINTER" ])";
        CHECK_NOTHROW( load_audit_group( group_with_conditions( all_tags ) ) );
        CHECK( GROUP_AUDIT_MONDT_J01.obj().monsters.size() == 1 );
        remove_audit_group();
    }
}

// MONDT-J01: a monster group entry can be gated by a dialogue "condition" (u = avatar).
// The only entry has weight == freq_total, so it is always picked when its condition holds
// and the explicit null default is returned otherwise.
TEST_CASE( "mondt-j01_dialogue_condition_gates_spawn_entry", "[audit][monster]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    load_audit_group( R"({ "id": "GROUP_AUDIT_MONDT_J01", "default": "mon_null", "monsters": [
        { "monster": "mon_zombie", "weight": 10, "condition": { "u_has_trait": "NIGHTVISION" } }
    ] })" );
    const auto spawned = []() {
        std::vector<mtype_id> ids;
        for( int i = 0; i < 20; ++i ) {
            for( const MonsterGroupResult &r :
                 MonsterGroupManager::GetResultFromGroup( GROUP_AUDIT_MONDT_J01 ) ) {
                ids.push_back( r.id );
            }
        }
        return ids;
    };

    REQUIRE_FALSE( u.has_trait( trait_NIGHTVISION ) );
    for( const mtype_id &id : spawned() ) {
        CHECK( id.is_null() );
    }

    u.set_mutation( trait_NIGHTVISION );
    REQUIRE( u.has_trait( trait_NIGHTVISION ) );
    for( const mtype_id &id : spawned() ) {
        CHECK( id == mon_zombie );
    }
    remove_audit_group();
}

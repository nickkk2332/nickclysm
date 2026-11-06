#include <map>
#include <string>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "debug.h"
#include "effect.h"
#include "enums.h"
#include "faction.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "init.h"
#include "item_pocket.h"
#include "pocket_type.h"
#include "itype.h"
#include "json_error.h"
#include "json_loader.h"
#include "mod_tracker.h"
#include "npc.h"
#include "type_id.h"
#include "units.h"

static const ammotype ammo_rock( "rock" );

static const efftype_id effect_audit_effect_abstract( "audit_effect_abstract" );
static const efftype_id effect_audit_effect_base( "audit_effect_base" );
static const efftype_id effect_audit_effect_child( "audit_effect_child" );
static const efftype_id effect_audit_effect_from_abstract( "audit_effect_from_abstract" );
static const efftype_id effect_audit_effect_late_child( "audit_effect_late_child" );
static const efftype_id effect_audit_rt_duplicate( "audit_rt_duplicate" );
static const efftype_id effect_audit_rt_ench( "audit_rt_ench" );
static const efftype_id effect_audit_rt_late_child( "audit_rt_late_child" );
static const efftype_id
effect_effect_portal_storm_dungeon_time_tracker( "effect_portal_storm_dungeon_time_tracker" );
static const efftype_id effect_took_antihistamine( "took_antihistamine" );
static const efftype_id effect_took_flumed( "took_flumed" );

static const faction_id faction_audit_faction_child( "audit_faction_child" );
static const faction_id faction_audit_faction_parent( "audit_faction_parent" );
static const faction_id faction_audit_rt_faction( "audit_rt_faction" );

static const flag_id json_flag_AUDIT_IMMUNE_A( "AUDIT_IMMUNE_A" );
static const flag_id json_flag_AUDIT_IMMUNE_B( "AUDIT_IMMUNE_B" );
static const flag_id json_flag_EFFECT_LIMB_SCORE_MOD( "EFFECT_LIMB_SCORE_MOD" );

static const itype_id itype_FMCNote( "FMCNote" );
static const itype_id itype_audit_pocket_delete_extend( "audit_pocket_delete_extend" );
static const itype_id itype_audit_pocket_inherit( "audit_pocket_inherit" );
static const itype_id itype_audit_pocket_parent( "audit_pocket_parent" );
static const itype_id itype_audit_pocket_proportional( "audit_pocket_proportional" );
static const itype_id itype_audit_pocket_relative( "audit_pocket_relative" );

static const npc_class_id NC_NONE( "NC_NONE" );

static const string_id<npc_template> npc_template_audit_npc_child( "audit_npc_child" );

static const string_id<npc_template> npc_template_audit_npc_parent( "audit_npc_parent" );

static const string_id<npc_template> npc_template_audit_rt_npc( "audit_rt_npc" );

static const string_id<npc_template>
npc_template_audit_rt_npc_bad_attitude( "audit_rt_npc_bad_attitude" );

// The max_item_length a pocket gets when its JSON gives none.
static units::length default_item_length( const units::volume &volume )
{
    const units::length length = units::default_length_from_volume( volume ) * M_SQRT2;
    return length;
}

namespace
{
// Exposes the loader every data file goes through.
class audit_data_loader : public DynamicDataLoader
{
    public:
        using DynamicDataLoader::load_object;
};
} // namespace

// Item_factory::finalize_pre appends a MIGRATION pocket to every item after its JSON pockets.
static bool ends_with_migration_pocket( const std::vector<pocket_data> &pockets )
{
    return !pockets.empty() && pockets.back().type == pocket_type::MIGRATION;
}

// INV-J02: "pocket_data" could only be replaced whole, so a copy-from item that changed one
// pocket had to restate every pocket of its parent.  The pocket loader now patches the
// inherited pockets by "id" or index with relative, proportional, delete and extend.  On the
// old code, data/mods/TEST_DATA/audit_pocket_overrides.json fails to load.
TEST_CASE( "inv-j02_copy_from_items_patch_inherited_pockets", "[audit][pocket]" )
{
    // Four pockets from JSON plus the MIGRATION pocket finalize appends.
    const std::vector<pocket_data> &parent = itype_audit_pocket_parent->pockets;
    REQUIRE( parent.size() == 5 );
    REQUIRE( ends_with_migration_pocket( parent ) );
    REQUIRE( parent[0].pocket_id == "main" );
    REQUIRE( parent[1].pocket_id.empty() );
    REQUIRE( parent[1].max_item_length == default_item_length( 500_ml ) );

    SECTION( "without a patch every pocket is inherited" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_inherit->pockets;
        REQUIRE( pockets.size() == 5 );
        for( size_t i = 0; i < pockets.size(); ++i ) {
            CHECK( pockets[i] == parent[i] );
            CHECK( pockets[i].pocket_id == parent[i].pocket_id );
        }
    }
    SECTION( "relative adds to the pocket named by id or index" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_relative->pockets;
        REQUIRE( pockets.size() == 5 );
        CHECK( ends_with_migration_pocket( pockets ) );
        CHECK( pockets[0].raw_volume_capacity == 3_liter );
        CHECK( pockets[0].moves == 80 );
        // an explicit max_item_length is kept
        CHECK( pockets[0].max_item_length == 40_cm );
        CHECK( pockets[1].max_contains_weight == 1500_gram );
        CHECK( pockets[1].raw_volume_capacity == 500_ml );
        CHECK( pockets[1].moves == 80 );
        CHECK( pockets[2] == parent[2] );
        CHECK( pockets[3].ammo_restriction.at( ammo_rock ) == 15 );
    }
    SECTION( "proportional \"*\" scales every pocket the member applies to" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_proportional->pockets;
        REQUIRE( pockets.size() == 5 );
        CHECK( ends_with_migration_pocket( pockets ) );
        CHECK( pockets[0].raw_volume_capacity == 4_liter );
        CHECK( pockets[0].max_contains_weight == 6_kilogram );
        CHECK( pockets[0].max_item_length == 40_cm );
        CHECK( pockets[1].raw_volume_capacity == 1_liter );
        CHECK( pockets[1].max_contains_weight == 1500_gram );
        // a defaulted max_item_length follows the new volume
        CHECK( pockets[1].max_item_length == default_item_length( 1_liter ) );
        CHECK( pockets[2].raw_volume_capacity == 2_liter );
        CHECK( pockets[2].max_item_length == default_item_length( 2_liter ) );
        CHECK( pockets[2].insulation == Approx( 2.0f ) );
        CHECK( pockets[0].insulation == Approx( 1.0f ) );
        // the ammo pocket takes the weight but its size still comes from the ammo
        CHECK( pockets[3].max_contains_weight == 7500_gram );
        CHECK( pockets[3].raw_volume_capacity == parent[3].raw_volume_capacity );
        CHECK( pockets[3].ammo_restriction.at( ammo_rock ) == 10 );
    }
    SECTION( "delete removes pockets by id and index, extend appends new ones" ) {
        const std::vector<pocket_data> &pockets = itype_audit_pocket_delete_extend->pockets;
        // 4 inherited - 2 deleted + 1 extended, then the MIGRATION pocket
        REQUIRE( pockets.size() == 4 );
        CHECK( ends_with_migration_pocket( pockets ) );
        CHECK( pockets[0].pocket_id == "main" );
        CHECK( pockets[0] == parent[0] );
        CHECK( pockets[1].pocket_id == "rocks" );
        CHECK( pockets[2].pocket_id == "hidden" );
        CHECK( pockets[2].raw_volume_capacity == 250_ml );
        CHECK( pockets[2].max_contains_weight == 500_gram );
    }
}

// HLTH-J02: load_effect_type built every effect_type from scratch, so effect types had no
// copy-from, extend or delete, and a second definition silently replaced the first.  On the old
// code data/mods/TEST_DATA/audit_effect_overrides.json fails to load.
TEST_CASE( "hlth-j02_effect_types_support_copy_from", "[audit][effect]" )
{
    const effect_type &base_type = effect_audit_effect_base.obj();
    const effect_type &child_type = effect_audit_effect_child.obj();
    const effect base( &base_type );
    const effect child( &child_type );

    SECTION( "absent members are inherited" ) {
        CHECK( child.get_max_duration() == 1_hours );
        CHECK( child.get_int_decay_tick() == 30 );
        CHECK( child_type.has_flag( json_flag_EFFECT_LIMB_SCORE_MOD ) );
        CHECK( child_type.get_mod_value( "STR", mod_action::MIN, 0, 1 ) == Approx( -1.0 ) );
        CHECK( child.disp_name() == "audit child" );
        CHECK( base.disp_name() == "audit base" );
    }
    SECTION( "relative adds to the parent's value" ) {
        CHECK( base.get_max_intensity() == 5 );
        CHECK( child.get_max_intensity() == 7 );
    }
    SECTION( "a bare rating re-rates the inherited apply message" ) {
        CHECK( base_type.get_rating( 1 ) == m_bad );
        CHECK( child_type.get_rating( 1 ) == m_good );
    }
    SECTION( "extend and delete change the inherited lists" ) {
        CHECK( base.get_resist_effects() == std::vector<efftype_id> { effect_took_flumed } );
        CHECK( child.get_resist_effects() ==
               std::vector<efftype_id> { effect_took_flumed, effect_took_antihistamine } );
        CHECK( base.get_removes_effects() ==
               std::vector<efftype_id> { effect_took_flumed, effect_took_antihistamine } );
        CHECK( child.get_removes_effects() ==
               std::vector<efftype_id> { effect_took_antihistamine } );
        CHECK( base_type.immune_flags.size() == 1 );
        CHECK( child_type.immune_flags.count( json_flag_AUDIT_IMMUNE_A ) == 1 );
        CHECK( child_type.immune_flags.count( json_flag_AUDIT_IMMUNE_B ) == 1 );
        REQUIRE( base_type.enchantments.size() == 1 );
        REQUIRE( child_type.enchantments.size() == 2 );
        CHECK( child_type.enchantments[0] == base_type.enchantments[0] );
        CHECK( child_type.enchantments[1].is_valid() );
        CHECK( child_type.enchantments[1] != base_type.enchantments[0] );
    }
    SECTION( "an abstract parent is copied but not registered" ) {
        const effect from_abstract( &effect_audit_effect_from_abstract.obj() );
        CHECK( from_abstract.get_max_intensity() == 3 );
        CHECK( from_abstract.get_int_add_val() == 2 );
        CHECK_FALSE( effect_audit_effect_abstract.is_valid() );
    }
    SECTION( "a child loads once its later parent does" ) {
        REQUIRE( effect_audit_effect_late_child.is_valid() );
        const effect late_child( &effect_audit_effect_late_child.obj() );
        CHECK( late_child.get_max_intensity() == 4 );
        CHECK( late_child.disp_name() == "audit late child" );
    }
}

// HLTH-J02: effects.json defined the portal storm dungeon tracker twice; the second one, which
// silently won, had no max_intensity, so the tracker could never pass intensity 1.  It must also
// rise once per EOC_PORTAL_STORM_DUNGEON_CRAWLING_TRACKER check (every 120 turns), or the
// check skips values and some of its switch cases never match.
TEST_CASE( "hlth-j02_portal_storm_tracker_keeps_its_intensity_cap", "[audit][effect]" )
{
    const effect tracker( &effect_effect_portal_storm_dungeon_time_tracker.obj() );
    CHECK( tracker.get_max_intensity() == 250000 );
    CHECK( tracker.get_int_decay_tick() == 120 );
}

// HLTH-J02: a child whose parent loads later was replayed from a copy of its JSON object, which
// had forgotten that the data loader read "type", so every replay logged "Unread data".
TEST_CASE( "hlth-j02_waiting_child_loads_without_errors", "[audit][effect]" )
{
    audit_data_loader loader;
    const std::string msg = capture_debugmsg_during( [&]() {
        loader.load_object( json_loader::from_string( R"({ "type": "effect_type",
            "id": "audit_rt_late_child", "copy-from": "audit_rt_late_parent",
            "max_intensity": 2 })" ), "audit_mod_a" );
        loader.load_object( json_loader::from_string(
                                R"({ "type": "effect_type", "id": "audit_rt_late_parent" })" ),
                            "audit_mod_a" );
    } );
    CHECK( msg.empty() );
    REQUIRE( effect_audit_rt_late_child.is_valid() );
    const effect late_child( &effect_audit_rt_late_child.obj() );
    CHECK( late_child.get_max_intensity() == 2 );
}

// HLTH-J02: extended inline enchantments were named after the list's size, so an "extend" after
// a "delete" reused the name of an enchantment the effect still had and overwrote it.
TEST_CASE( "hlth-j02_extended_inline_enchantments_get_unused_names", "[audit][effect]" )
{
    const std::string ench = R"({ "values": [ { "value": "SPEED", "add": %d } ] })";
    const JsonObject first = json_loader::from_string( string_format(
                                 R"({ "id": "audit_rt_ench", "enchantments": [ %s, %s ] })",
                                 string_format( ench, -1 ), string_format( ench, -2 ) ) );
    load_effect_type( first, "audit_mod_a" );
    REQUIRE( effect_audit_rt_ench->enchantments.size() == 2 );
    const enchantment_id doomed = effect_audit_rt_ench->enchantments[0];
    const enchantment_id kept = effect_audit_rt_ench->enchantments[1];
    const JsonObject trimmed = json_loader::from_string( string_format(
                                   R"({ "id": "audit_rt_ench", "copy-from": "audit_rt_ench",
            "delete": { "enchantments": [ "%s" ] } })", doomed.str() ) );
    load_effect_type( trimmed, "audit_mod_b" );
    const JsonObject extended = json_loader::from_string( string_format(
                                    R"({ "id": "audit_rt_ench", "copy-from": "audit_rt_ench",
            "extend": { "enchantments": [ %s ] } })", string_format( ench, -3 ) ) );
    load_effect_type( extended, "audit_mod_c" );
    const std::vector<enchantment_id> &enchantments = effect_audit_rt_ench->enchantments;
    REQUIRE( enchantments.size() == 2 );
    CHECK( enchantments[0] == kept );
    CHECK( enchantments[1] != kept );
}

// HLTH-J02: an unknown top-level "rating" threw std::bad_optional_access instead of a JSON
// error, and a second definition from the same mod went unreported.
TEST_CASE( "hlth-j02_effect_type_load_errors", "[audit][effect]" )
{
    SECTION( "an unknown rating is a JSON error" ) {
        const JsonObject jo = json_loader::from_string(
                                  R"({ "id": "audit_rt_bad_rating", "rating": "awful" })" );
        CHECK_THROWS_AS( load_effect_type( jo, "test_data" ), JsonError );
        jo.allow_omitted_members();
    }
    SECTION( "two definitions from one mod are an error; another mod may override" ) {
        const std::string json = R"({ "id": "audit_rt_duplicate", "max_intensity": 2 })";
        const JsonObject first = json_loader::from_string( json );
        load_effect_type( first, "audit_mod_a" );
        const JsonObject second = json_loader::from_string( json );
        CHECK_THROWS_AS( load_effect_type( second, "audit_mod_a" ), mod_error );
        second.allow_omitted_members();
        const JsonObject later = json_loader::from_string( R"({ "id": "audit_rt_duplicate",
            "copy-from": "audit_rt_duplicate", "relative": { "max_intensity": 3 } })" );
        load_effect_type( later, "audit_mod_b" );
        const effect overridden( &effect_audit_rt_duplicate.obj() );
        CHECK( overridden.get_max_intensity() == 5 );
    }
}

// NPC-J02: npc templates and factions had no copy-from.  On the old code
// data/mods/TEST_DATA/audit_npc_overrides.json fails to load.
TEST_CASE( "npc-j02_npc_and_faction_copy_from", "[audit][npc]" )
{
    SECTION( "an npc template copies its parent and changes what it gives" ) {
        const npc_template &parent = npc_template_audit_npc_parent.obj();
        const npc_template &child = npc_template_audit_npc_child.obj();
        CHECK( parent.guy.get_fac_id() == faction_audit_faction_parent );
        CHECK( child.guy.get_fac_id() == faction_audit_faction_child );
        CHECK( child.name_suffix.translated() == "Child" );
        CHECK( child.guy.myclass == NC_NONE );
        CHECK( child.guy.mission == NPC_MISSION_GUARD );
        CHECK( child.guy.chatbin.first_topic == "TALK_DONE" );
        CHECK( child.str == 12 );
    }
    SECTION( "a faction copies its parent and changes what it gives" ) {
        faction *child = g->faction_manager_ptr->get( faction_audit_faction_child );
        REQUIRE( child != nullptr );
        CHECK( child->get_name() == "Audit Children" );
        CHECK( child->likes_u == 10 );
        CHECK( child->power == 25 );
        CHECK( child->wealth == 1000 );
        CHECK( child->currency == itype_FMCNote );
    }
}

// NPC-J02: a mod redefining an npc template was ignored (std::map::emplace kept the first),
// and a redefined faction was half applied: faction_manager refreshed it from the first
// definition.  Two definitions of one id from the same mod went unreported.
TEST_CASE( "npc-j02_later_npc_and_faction_definitions_win", "[audit][npc]" )
{
    audit_data_loader loader;
    SECTION( "a later mod's npc template replaces the earlier one" ) {
        const std::string npc_json = R"({ "type": "npc", "id": "audit_rt_npc", "class": "NC_NONE",
            "attitude": 0, "chat": "TALK_DONE", "name_suffix": "%s" })";
        loader.load_object( json_loader::from_string( string_format( npc_json, "First" ) ),
                            "audit_mod_a" );
        loader.load_object( json_loader::from_string( string_format( npc_json, "Second" ) ),
                            "audit_mod_b" );
        CHECK( npc_template_audit_rt_npc->name_suffix.translated() == "Second" );
    }
    SECTION( "a later mod's faction replaces the earlier one" ) {
        const std::string faction_json = R"({ "type": "faction", "id": "audit_rt_faction",
            "name": "%s", "likes_u": %d, "respects_u": 0, "known_by_u": true, "size": 1,
            "power": 1, "wealth": 0, "description": "A faction for the override test." })";
        loader.load_object( json_loader::from_string( string_format( faction_json, "First", 1 ) ),
                            "audit_mod_a" );
        loader.load_object( json_loader::from_string( string_format( faction_json, "Second", 2 ) ),
                            "audit_mod_b" );
        faction *fac = g->faction_manager_ptr->get( faction_audit_rt_faction );
        REQUIRE( fac != nullptr );
        CHECK( fac->get_name() == "Second" );
        CHECK( fac->likes_u == 2 );
    }
    SECTION( "two definitions from one mod are reported" ) {
        const std::string npc_json = R"({ "type": "npc", "id": "audit_rt_npc_twice",
            "class": "NC_NONE", "attitude": 0, "chat": "TALK_DONE" })";
        const std::string msg = capture_debugmsg_during( [&]() {
            loader.load_object( json_loader::from_string( npc_json ), "audit_mod_c" );
            loader.load_object( json_loader::from_string( npc_json ), "audit_mod_c" );
        } );
        CHECK( msg.find( "two definitions" ) != std::string::npos );
    }
}

// NPC-J02 (review): an unknown "attitude" was reported and set to NPCATT_NULL, then set again
// from the raw number, so the template kept the invalid attitude.
TEST_CASE( "npc-j02_invalid_npc_attitude_becomes_null", "[audit][npc]" )
{
    audit_data_loader loader;
    const std::string msg = capture_debugmsg_during( [&]() {
        loader.load_object( json_loader::from_string( R"({ "type": "npc",
            "id": "audit_rt_npc_bad_attitude", "class": "NC_NONE", "attitude": 99,
            "chat": "TALK_DONE" })" ), "audit_mod_a" );
    } );
    CHECK( msg.find( "invalid attitude enum 99" ) != std::string::npos );
    CHECK( npc_template_audit_rt_npc_bad_attitude->guy.get_attitude() == NPCATT_NULL );
}

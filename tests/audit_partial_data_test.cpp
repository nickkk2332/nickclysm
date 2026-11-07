#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "debug.h"
#include "effect.h"
#include "effect_source.h"
#include "field_type.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "itype.h"
#include "json_loader.h"
#include "magic_enchantment.h"
#include "magic_ter_furn_transform.h"
#include "map.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "mapgen.h"
#include "mutation.h"
#include "npc.h"
#include "omdata.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "profession.h"
#include "scenario.h"
#include "scent_map.h"
#include "string_id.h"
#include "type_id.h"

static const efftype_id effect_audit_partial_data_chances( "audit_partial_data_chances" );

static const field_type_str_id
field_audit_partial_data_wide_scent_gas( "audit_partial_data_wide_scent_gas" );
static const field_type_str_id field_fd_smoke( "fd_smoke" );

static const furn_str_id furn_f_vitrified_doll( "f_vitrified_doll" );
static const furn_str_id furn_f_vitrified_sofa( "f_vitrified_sofa" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_heavy_battery_cell( "heavy_battery_cell" );
static const itype_id itype_robofac30( "robofac30" );
static const itype_id itype_robofac_gun( "robofac_gun" );
static const itype_id itype_test_multimag_gun_integral_ammo( "test_multimag_gun_integral_ammo" );

static const oter_str_id oter_audit_partial_data_var_pred_omt( "audit_partial_data_var_pred_omt" );
static const oter_str_id oter_field( "field" );
static const oter_str_id oter_forest( "forest" );
static const oter_str_id oter_road_nesw( "road_nesw" );
static const oter_str_id oter_road_nesw_manhole( "road_nesw_manhole" );

static const overmap_special_id overmap_special_test_audit_road_shack( "test_audit_road_shack" );

static const profession_id profession_driving_license( "driving_license" );

static const string_id<scenario>
scenario_audit_partial_data_one_hobby( "audit_partial_data_one_hobby" );

static const ter_furn_transform_id
ter_furn_transform_imperfect_doll_to_couch( "imperfect_doll_to_couch" );

static bool mentions( const std::string_view messages, const std::string_view what )
{
    return messages.find( what ) != std::string_view::npos;
}

// What a 24x24 nested mapgen made from the json members in @p members reports when checked
static std::string nested_mapgen_check_messages( const std::string &members )
{
    JsonObject jo = json_loader::from_string( R"({ "mapgensize": [ 24, 24 ], )" + members + "}" );
    jo.allow_omitted_members();
    mapgen_function_json_nested func( std::move( jo ), "audit partial data mapgen" );
    func.setup();
    func.finalize_parameters();
    return capture_debugmsg_during( [&func]() {
        func.check();
    } );
}

// MGEN-B03: remove_npcs never checked its class, so a misspelled NPC template silently removed
// nobody. It is checked now; the robofac intercom's save migration, which removes NPCs of
// classes that no longer exist, says so with "obsolete_class".
TEST_CASE( "mgen-b03_remove_npcs_checks_its_class", "[audit][mapgen]" )
{
    const std::string unknown = nested_mapgen_check_messages(
                                    R"("remove_npcs": [ { "class": "audit_no_such_npc", "x": 0, "y": 0 } ])" );
    CAPTURE( unknown );
    CHECK( mentions( unknown, "audit_no_such_npc" ) );

    const std::string obsolete = nested_mapgen_check_messages(
                                     R"("remove_npcs": [ { "class": "audit_no_such_npc", "obsolete_class": true,
                                         "x": 0, "y": 0 } ])" );
    CAPTURE( obsolete );
    CHECK( obsolete.empty() );

    const std::string known = nested_mapgen_check_messages(
                                  R"("remove_npcs": [ { "class": "test_npc_trader", "x": 0, "y": 0 },
                                      { "unique_id": "audit_unique", "x": 0, "y": 0 } ])" );
    CAPTURE( known );
    CHECK( known.empty() );

    // An existing class marked obsolete is a mistake too
    const std::string not_obsolete = nested_mapgen_check_messages(
                                         R"("remove_npcs": [ { "class": "test_npc_trader", "obsolete_class": true,
                                             "x": 0, "y": 0 } ])" );
    CAPTURE( not_obsolete );
    CHECK( mentions( not_obsolete, "test_npc_trader" ) );
}

// MGEN-B06: the check that a mapgen expecting a predecessor runs on terrain flagged
// REQUIRES_PREDECESSOR only looked at mapgens with a constant weight, so roads (whose weight
// is a global variable) were never checked; road_nesw_manhole, which shares the four-way road's
// mapgen, lacked the flag.
TEST_CASE( "mgen-b06_variable_weight_mapgen_is_checked_against_its_terrain", "[audit][mapgen]" )
{
    const std::string key = oter_audit_partial_data_var_pred_omt.str();
    // Its own terrain has REQUIRES_PREDECESSOR; a field has not.
    const std::string own = capture_debugmsg_during( [&key]() {
        check_mapgen_consistent_with( key, oter_audit_partial_data_var_pred_omt.obj() );
    } );
    CAPTURE( own );
    CHECK( own.empty() );
    const std::string other = capture_debugmsg_during( [&key]() {
        check_mapgen_consistent_with( key, oter_field.obj() );
    } );
    CAPTURE( other );
    CHECK( mentions( other, "expects a predecessor terrain" ) );

    CHECK( oter_road_nesw_manhole->has_flag( oter_flags::requires_predecessor ) );
}

// MGEN-B06: with REQUIRES_PREDECESSOR the manhole would have pushed the road it replaces as its
// predecessor, generating the four-way road twice. It takes over the road's predecessor instead.
TEST_CASE( "mgen-b06_manhole_keeps_the_predecessor_of_the_road_it_replaces", "[audit][overmap]" )
{
    const tripoint_om_omt p( 20, 20, 0 );
    std::unique_ptr<overmap> om = std::make_unique<overmap>( point_abs_om::zero );
    om->ter_set( p, oter_forest.id() );
    om->ter_set( p, oter_road_nesw.id() );
    REQUIRE( om->predecessors( p ).size() == 1 );

    om->ter_set( p, oter_road_nesw_manhole.id() );
    const std::vector<oter_id> under_manhole = om->predecessors( p );
    REQUIRE( under_manhole.size() == 1 );
    CHECK( under_manhole[0].id() == oter_forest );

    // And a road laid back over the manhole doesn't stack the manhole either
    om->ter_set( p, oter_road_nesw.id() );
    const std::vector<oter_id> under_road = om->predecessors( p );
    REQUIRE( under_road.size() == 1 );
    CHECK( under_road[0].id() == oter_forest );
}

// DATA-B05: mutation types and the charge and temperature removal blacklists were never reset,
// so a mod's entries carried over to the next world loaded in the same session. Unloading the
// data resets them now (DynamicDataLoader::unload_data).
TEST_CASE( "data-b05_mutation_types_and_removal_blacklists_reset", "[audit][mods]" )
{
    // Restores every mutation type a loaded mutation uses, the only ones anything checks
    on_out_of_scope restore_types( []() {
        std::set<std::string> used;
        for( const mutation_branch &mut : mutation_branch::get_all() ) {
            used.insert( mut.types.begin(), mut.types.end() );
        }
        for( const std::string &type : used ) {
            load_mutation_type( json_loader::from_string( R"({ "id": ")" + type + R"(" })" ) );
        }
    } );
    load_mutation_type( json_loader::from_string( R"({ "id": "audit_partial_data_b05_type" })" ) );
    REQUIRE( mutation_type_exists( "audit_partial_data_b05_type" ) );
    REQUIRE( mutation_type_exists( "BIOLUM" ) );
    reset_mutation_types();
    CHECK_FALSE( mutation_type_exists( "audit_partial_data_b05_type" ) );
    CHECK_FALSE( mutation_type_exists( "BIOLUM" ) );

    // A plank can't have charges: loading one with 3 moves the 2 extra planks into its migration
    // pocket, unless the plank is on the charge removal blacklist. (A rock is sling ammo, so it
    // may have charges and never takes this path.)
    const auto planks_migrated = []() {
        item plank;
        plank.deserialize( json_loader::from_string(
                               R"({ "typeid": "2x4", "charges": 3 })" ).get_object() );
        REQUIRE( plank.typeId() == itype_2x4 );
        return plank.all_items_top( pocket_type::MIGRATION ).size();
    };
    on_out_of_scope empty_blacklists( []() {
        reset_removal_blacklists();
    } );
    REQUIRE( planks_migrated() == 2 );
    load_charge_removal_blacklist( json_loader::from_string(
                                       R"({ "type": "charge_removal_blacklist", "list": [ "2x4" ] })" ), "" );
    REQUIRE( planks_migrated() == 0 );
    reset_removal_blacklists();
    CHECK( planks_migrated() == 2 );
}

// SPAT-B10: scent_neutralization always reached exactly one tile out, and a field that doesn't
// spread as a gas, which never neutralizes, loaded without a word.
TEST_CASE( "spat-b10_scent_neutralization_radius_and_load_check", "[audit][scent][field]" )
{
    CHECK( field_fd_smoke->scent_neutralization_radius == 1 );
    REQUIRE( field_audit_partial_data_wide_scent_gas->scent_neutralization_radius == 2 );

    clear_map();
    clear_avatar();
    map &here = get_map();
    scent_map &scent = get_scent();
    scent.reset();
    on_out_of_scope reset_scent( [&scent]() {
        scent.reset();
    } );
    const tripoint_bub_ms p( 65, 65, 0 );
    const tripoint_bub_ms two_out = p + tripoint_rel_ms( 2, 0, 0 );
    const tripoint_bub_ms three_out = p + tripoint_rel_ms( 0, 3, 0 );
    // All in the same submap
    REQUIRE( coords::project_to<coords::sm>( two_out ) == coords::project_to<coords::sm>( p ) );
    REQUIRE( coords::project_to<coords::sm>( three_out ) == coords::project_to<coords::sm>( p ) );
    scent.set( p, 100 );
    scent.set( two_out, 100 );
    scent.set( three_out, 100 );
    // Intensity 1 never spreads, so only this field lowers the scent
    REQUIRE( here.add_field( p, field_audit_partial_data_wide_scent_gas.id(), 1, 1_turns ) );
    here.process_fields();
    CHECK( scent.get( p ) == 95 );
    CHECK( scent.get( two_out ) == 95 );
    CHECK( scent.get( three_out ) == 100 );

    const auto check_field = []( const std::string & json ) {
        JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        field_type ft;
        ft.load( jo, "" );
        return capture_debugmsg_during( [&ft]() {
            ft.check();
        } );
    };
    const std::string still = check_field( R"({ "intensity_levels": [
        { "name": "audit still gas", "scent_neutralization": 3 } ], "phase": "gas" })" );
    CAPTURE( still );
    CHECK( mentions( still, "scent_neutralization" ) );
    const std::string too_wide = check_field( R"({ "intensity_levels": [
        { "name": "audit wide gas", "scent_neutralization": 3 } ], "phase": "gas",
        "percent_spread": 10, "scent_neutralization_radius": 9 })" );
    CAPTURE( too_wide );
    CHECK( mentions( too_wide, "scent_neutralization_radius 9" ) );
    const std::string spreading = check_field( R"({ "intensity_levels": [
        { "name": "audit gas", "scent_neutralization": 3 } ], "phase": "gas",
        "percent_spread": 10 })" );
    CAPTURE( spreading );
    CHECK( spreading.empty() );
}

// PROG-B03: random NPCs drew their profession from the generic scenario but their hobbies from
// the player's scenario, so the player's hobby limits (a mod's challenge scenario, say) applied
// to every NPC. Both come from the generic scenario now; the avatar keeps its own.
TEST_CASE( "prog-b03_npc_hobbies_come_from_the_generic_scenario", "[audit][chargen]" )
{
    clear_avatar();
    const scenario *before = get_scenario();
    on_out_of_scope restore( [before]() {
        set_scenario( before );
        clear_avatar();
    } );
    REQUIRE( scenario_audit_partial_data_one_hobby.is_valid() );
    set_scenario( &scenario_audit_partial_data_one_hobby.obj() );
    REQUIRE( get_scenario()->permitted_hobbies( false ) ==
             std::vector<profession_id> { profession_driving_license } );

    standard_npc guy( "Audit" );
    // randomize() gives a random NPC its profession first; a bare standard_npc has none
    guy.prof = profession::generic();
    std::set<profession_id> npc_hobbies;
    for( int i = 0; i < 50; ++i ) {
        guy.randomize_hobbies();
        for( const profession *hobby : guy.hobbies ) {
            npc_hobbies.insert( hobby->ident() );
        }
    }
    npc_hobbies.erase( profession_driving_license );
    // One in 6 rolls gives no hobby, and only one hobby in many is the driving license
    CHECK_FALSE( npc_hobbies.empty() );

    avatar &you = get_avatar();
    for( int i = 0; i < 20; ++i ) {
        you.randomize_hobbies();
        for( const profession *hobby : you.hobbies ) {
            CHECK( hobby->ident() == profession_driving_license );
        }
    }
}

// OMAP-B02: a mission target that is part of an overmap special was only looked for in overmaps
// already made, even on the retry that may make new ones. A search allowed to make overmaps now
// makes the missing ones next to the origin's, nearest first, when no existing one holds the
// special, and only those within its range.
TEST_CASE( "omap-b02_special_search_generates_the_overmaps_next_to_the_origin",
           "[audit][overmap]" )
{
    clear_avatar();
    clear_map();
    const point_abs_om near_avatar = coords::project_to<coords::om>( get_avatar().pos_abs_omt().xy() );
    omt_find_params params;
    params.types = { { "cabin", ot_match_type::type } };
    params.om_special = overmap_special_test_audit_road_shack;
    params.search_range = 3;

    // Making an overmap normally also makes neighbours, to hold the specials that didn't fit, so
    // the overmaps here are made with no specials: only the search can make others.
    const auto make_bare = []( const point_abs_om & where ) -> overmap & {
        overmap_special_batch no_specials( where );
        overmap_buffer.create_custom_overmap( where, no_specials );
        overmap *made = overmap_buffer.get_existing( where );
        REQUIRE( made != nullptr );
        return *made;
    };

    SECTION( "a match on an existing overmap keeps the search from making any" ) {
        const point_abs_om home_pos = near_avatar + point_rel_om( 31, -29 );
        const point_abs_om east_pos = home_pos + point_rel_om( 1, 0 );
        REQUIRE( overmap_buffer.get_existing( east_pos ) == nullptr );
        overmap &home = make_bare( home_pos );
        REQUIRE( overmap_buffer.get_existing( east_pos ) == nullptr );
        const tripoint_om_omt shack_local( OMAPX - 3, 90, 0 );
        home.place_special_forced( overmap_special_test_audit_road_shack, shack_local,
                                   om_direction::type::north );
        const tripoint_abs_omt origin = project_combine( home_pos, tripoint_om_omt( OMAPX - 1, 90,
                                        0 ) );
        params.existing_only = false;
        const int overmaps_before = overmap_buffer.get_overmap_count();
        CHECK( overmap_buffer.find_closest( origin, params ) ==
               project_combine( home_pos, shack_local ) );
        CHECK( overmap_buffer.get_overmap_count() == overmaps_before );
        CHECK( overmap_buffer.get_existing( east_pos ) == nullptr );
    }

    SECTION( "with no match the overmaps within range are made" ) {
        const point_abs_om home_pos = near_avatar + point_rel_om( -33, 27 );
        const point_abs_om east_pos = home_pos + point_rel_om( 1, 0 );
        const point_abs_om west_pos = home_pos + point_rel_om( -1, 0 );
        REQUIRE( overmap_buffer.get_existing( east_pos ) == nullptr );
        REQUIRE( overmap_buffer.get_existing( west_pos ) == nullptr );
        make_bare( home_pos );
        REQUIRE( overmap_buffer.get_existing( east_pos ) == nullptr );
        REQUIRE( overmap_buffer.get_existing( west_pos ) == nullptr );
        // On the east edge, so only the east overmap is within 3 OMTs
        const tripoint_abs_omt east_edge = project_combine( home_pos, tripoint_om_omt( OMAPX - 1,
                                           90, 0 ) );

        params.existing_only = true;
        const int overmaps_before = overmap_buffer.get_overmap_count();
        CHECK( overmap_buffer.find_closest( east_edge, params ).is_invalid() );
        CHECK( overmap_buffer.get_overmap_count() == overmaps_before );
        CHECK( overmap_buffer.get_existing( east_pos ) == nullptr );

        // The shack never spawns on its own, so there is still nothing to find, but the east
        // overmap has been made (making it may make others: specials that didn't fit go next door)
        params.existing_only = false;
        CHECK( overmap_buffer.find_closest( east_edge, params ).is_invalid() );
        CHECK( overmap_buffer.get_existing( east_pos ) != nullptr );

        // The random pick (find_all) does the same from the west edge
        const tripoint_abs_omt west_edge = project_combine( home_pos, tripoint_om_omt( 0, 90, 0 ) );
        CHECK( overmap_buffer.find_all( west_edge, params ).empty() );
        CHECK( overmap_buffer.get_existing( west_pos ) != nullptr );
    }
}

// IINF-B05: ammo_remaining() added every loaded magazine together, so a multimag gun's battery
// charges counted as rounds. With several ammo pockets only the asked-for ammo counts; a gun
// with one magazine counts it whatever the gun's own (possibly placeholder) ammo types say.
TEST_CASE( "iinf-b05_ammo_remaining_counts_only_the_asked_for_ammo", "[audit][item][gun]" )
{
    item gun( itype_test_multimag_gun_integral_ammo );
    item battery( itype_heavy_battery_cell );
    battery.ammo_set( itype_battery );
    const int charge = battery.ammo_remaining();
    REQUIRE( charge > 1 );
    REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );
    // No 9mm loaded: the battery's charge isn't rounds
    CHECK( gun.ammo_remaining() == 0 );
    REQUIRE( gun.put_in( item( itype_9mm, calendar::turn, 1 ), pocket_type::MAGAZINE ).success() );
    CHECK( gun.ammo_remaining() == 1 );
    // The battery itself still holds its charge
    CHECK( gun.ammo_remaining_in_pocket( "power" ) == charge );

    // A plain gun with a magazine is unchanged
    item glock( itype_glock_19 );
    item glock_mag( itype_glockmag );
    REQUIRE( glock_mag.put_in( item( itype_9mm, calendar::turn, 15 ),
                               pocket_type::MAGAZINE ).success() );
    REQUIRE( glock.put_in( glock_mag, pocket_type::MAGAZINE_WELL ).success() );
    CHECK( glock.ammo_remaining() == 15 );

    // The robofac gun's own ammo type is a placeholder; its one magazine still counts
    item robofac_gun( itype_robofac_gun );
    item robofac_mag( itype_robofac30 );
    robofac_mag.ammo_set( robofac_mag.ammo_default() );
    const int rounds = robofac_mag.ammo_remaining();
    REQUIRE( rounds > 0 );
    REQUIRE( robofac_gun.put_in( robofac_mag, pocket_type::MAGAZINE_WELL ).success() );
    CHECK( robofac_gun.ammo_remaining() == rounds );
}

// HLTH-B05: a chance an effect gives was truncated to an int, and a top of 0 with no bot made
// x_in_y( mod, 0 ), which always fires: a chance of 0 (or one that worked out below 1) fired
// every time. A given chance now works as written: 0 or less never fires, a positive one is at
// least 1. Only a chance left out fires every time.
TEST_CASE( "hlth-b05_effect_chances_work_as_written", "[audit][health]" )
{
    const effect eff( effect_source::empty(), &effect_audit_partial_data_chances.obj(), 1_hours,
                      bodypart_str_id::NULL_ID(), false, 1, calendar::turn );
    int pain = 0;
    int hurt = 0;
    int stim = 0;
    int thirst = 0;
    const int rolls = 200;
    for( int i = 1; i <= rolls; ++i ) {
        const time_point when = calendar::turn_zero + time_duration::from_turns( i );
        pain += eff.activated( when, "PAIN", 1, false, 1.0 ) ? 1 : 0;
        hurt += eff.activated( when, "HURT", 1, false, 1.0 ) ? 1 : 0;
        stim += eff.activated( when, "STIM", 1, false, 1.0 ) ? 1 : 0;
        thirst += eff.activated( when, "THIRST", 1, false, 1.0 ) ? 1 : 0;
    }
    // "pain_chance": [ 0 ] never
    CHECK( pain == 0 );
    // "thirst_chance": [ -1 ] never, as vanilla uses it
    CHECK( thirst == 0 );
    // No stim chance: every time
    CHECK( stim == rolls );
    // 0.5 in 2 is 1 in 2, not 0 in 2
    CHECK( hurt > rolls / 4 );
    CHECK( hurt < rolls * 3 / 4 );

    CHECK( eff.get_percentage( "PAIN", 1, false ) == 0.0 );
    CHECK( eff.get_percentage( "THIRST", 1, false ) == 0.0 );
    CHECK( eff.get_percentage( "STIM", 1, false ) == Approx( 100.0 ) );
    CHECK( eff.get_percentage( "HURT", 1, false ) == Approx( 50.0 ) );
}

// MAGIC-J03 / MAGIC-J06: ter_furn_transform had no check, so a wrong id or a flag nothing has
// only failed when the transform ran, or never matched; shipped data had furniture listed as
// terrain (the vitrified farm's doll and bookcase never changed).
TEST_CASE( "magic-j03_ter_furn_transform_check_reports_unknown_ids", "[audit][magic]" )
{
    JsonObject jo = json_loader::from_string( R"({ "id": "audit_partial_data_transform",
        "terrain": [
          { "result": "t_audit_no_result", "valid_terrain": [ "t_audit_no_terrain" ],
            "valid_flags": [ "AUDIT_NO_TERRAIN_FLAG" ] },
          { "result": "t_dirt", "valid_terrain": [ "t_grass" ], "valid_flags": [ "DIGGABLE" ] } ],
        "furniture": [ { "result": "f_audit_no_result", "valid_furniture": [ "f_null" ],
            "valid_flags": [ "AUDIT_NO_FURNITURE_FLAG" ] } ],
        "trap": [ { "result": "tr_audit_no_result", "valid_trap": [ "tr_null" ],
            "valid_flags": [ "AUDIT_NO_JSON_FLAG" ] } ],
        "field": [ { "result": "fd_fire", "valid_field": [ "fd_smoke" ] } ] })" );
    jo.allow_omitted_members();
    ter_furn_transform transform;
    transform.load( jo, "" );
    const std::set<std::string> terrain_flags{ "DIGGABLE" };
    const std::set<std::string> furniture_flags{ "TRANSPARENT" };
    const std::string errors = capture_debugmsg_during( [&]() {
        transform.check( terrain_flags, furniture_flags );
    } );
    CAPTURE( errors );
    CHECK( mentions( errors, "t_audit_no_result" ) );
    CHECK( mentions( errors, "t_audit_no_terrain" ) );
    CHECK( mentions( errors, "AUDIT_NO_TERRAIN_FLAG" ) );
    CHECK( mentions( errors, "f_audit_no_result" ) );
    CHECK( mentions( errors, "AUDIT_NO_FURNITURE_FLAG" ) );
    CHECK( mentions( errors, "tr_audit_no_result" ) );
    CHECK( mentions( errors, "AUDIT_NO_JSON_FLAG" ) );
    CHECK_FALSE( mentions( errors, "t_dirt" ) );
    CHECK_FALSE( mentions( errors, "t_grass" ) );
    CHECK_FALSE( mentions( errors, "DIGGABLE" ) );
    CHECK_FALSE( mentions( errors, "fd_" ) );

    // Shipped transforms pass, and the vitrified doll turns into a sofa
    const std::string shipped = capture_debugmsg_during( []() {
        ter_furn_transform::check_all();
    } );
    CAPTURE( shipped );
    CHECK( shipped.empty() );
    clear_map();
    clear_avatar();
    map &here = get_map();
    const tripoint_bub_ms p( 60, 60, 0 );
    here.furn_set( p, furn_f_vitrified_doll );
    ter_furn_transform_imperfect_doll_to_couch->transform( here, p );
    CHECK( here.furn( p ).id() == furn_f_vitrified_sofa );
}

// MAGIC-J03: enchantments checked only their spell_values filters, so an unknown mutation,
// effect, skill, body part, damage type or spell (a typo such as "computers" or "head" in the
// shipped mods) did nothing, or failed only when the enchantment applied.
TEST_CASE( "magic-j03_enchantment_check_reports_unknown_references", "[audit][magic]" )
{
    JsonObject jo = json_loader::from_string( R"({ "id": "audit_partial_data_enchantment",
        "mutations": [ "AUDIT_NO_MUTATION" ],
        "ench_effects": [ { "effect": "audit_no_effect", "intensity": 1 } ],
        "emitter": "emit_audit_no_emitter",
        "skills": [ { "value": "audit_no_skill", "add": 1 } ],
        "encumbrance_modifier": [ { "part": "audit_no_part", "add": 1 }, { "part": "all", "add": 1 } ],
        "melee_damage_bonus": [ { "type": "audit_no_damage", "add": 1 } ],
        "incoming_damage_mod": [ { "type": "heat", "multiply": -0.1 } ],
        "hit_you_effect": [ { "id": "audit_no_hit_spell" } ],
        "intermittent_activation": { "effects": [ { "frequency": "1 hours",
            "spell_effects": [ { "id": "audit_no_intermittent_spell" } ] } ] },
        "modified_bodyparts": [ { "gain": "audit_no_gained_part" } ] })" );
    jo.allow_omitted_members();
    enchantment ench;
    ench.load( jo, "" );
    const std::string errors = capture_debugmsg_during( [&ench]() {
        ench.check();
    } );
    CAPTURE( errors );
    for( const char *bad : {
             "AUDIT_NO_MUTATION", "audit_no_effect", "emit_audit_no_emitter", "audit_no_skill",
             "audit_no_part", "audit_no_damage", "audit_no_hit_spell", "audit_no_intermittent_spell",
             "audit_no_gained_part"
         } ) {
        CAPTURE( bad );
        CHECK( mentions( errors, bad ) );
    }
    // "all" stands for every body part
    CHECK_FALSE( mentions( errors, R"("all")" ) );
    CHECK_FALSE( mentions( errors, "heat" ) );

    // Shipped enchantments, inline ones included, pass
    const std::string shipped = capture_debugmsg_during( []() {
        enchantment::check_consistency();
    } );
    CAPTURE( shipped );
    CHECK( shipped.empty() );
}

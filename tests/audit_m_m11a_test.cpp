#include <list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "character_attire.h"
#include "clothing_mod.h"
#include "damage.h"
#include "dialogue.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "json.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "npc.h"
#include "output.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"

static const ammotype ammo_battery( "battery" );
static const ammotype ammo_test_9mm( "test_9mm" );

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );
static const damage_type_id damage_heat( "heat" );
static const damage_type_id damage_stab( "stab" );

static const flag_id json_flag_TEST_M11A_PADDED( "TEST_M11A_PADDED" );
static const flag_id json_flag_leather_padded( "leather_padded" );

static const itype_id itype_arredondo_chute( "arredondo_chute" );
static const itype_id itype_arredondo_chute_benelli_sa( "arredondo_chute_benelli_sa" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_brass_catcher( "brass_catcher" );
static const itype_id itype_cheek_pad( "cheek_pad" );
static const itype_id itype_hand_crossbow( "hand_crossbow" );
static const itype_id itype_stock_none( "stock_none" );
static const itype_id itype_test_9mm_ammo( "test_9mm_ammo" );
static const itype_id itype_test_armor_chitin( "test_armor_chitin" );
static const itype_id itype_test_m11a_bare_power_armor( "test_m11a_bare_power_armor" );
static const itype_id itype_test_m11a_bionic_tool( "test_m11a_bionic_tool" );
static const itype_id itype_test_m11a_cold_gun( "test_m11a_cold_gun" );
static const itype_id itype_test_m11a_free_mechanism( "test_m11a_free_mechanism" );
static const itype_id itype_test_m11a_gun( "test_m11a_gun" );
static const itype_id itype_test_m11a_info_mod( "test_m11a_info_mod" );
static const itype_id itype_test_m11a_lamp( "test_m11a_lamp" );
static const itype_id itype_test_m11a_locked_mechanism( "test_m11a_locked_mechanism" );
static const itype_id itype_test_m11a_odd_mod( "test_m11a_odd_mod" );
static const itype_id itype_test_m11a_prone_mod( "test_m11a_prone_mod" );
static const itype_id itype_test_m11a_quiet_lamp( "test_m11a_quiet_lamp" );
static const itype_id itype_test_m11a_sealant( "test_m11a_sealant" );
static const itype_id itype_test_m11a_small_gun( "test_m11a_small_gun" );
static const itype_id itype_test_m11a_suit( "test_m11a_suit" );
static const itype_id itype_test_m11a_suit_engaged( "test_m11a_suit_engaged" );
static const itype_id itype_test_m11a_template( "test_m11a_template" );
static const itype_id itype_test_m11a_template_child( "test_m11a_template_child" );
static const itype_id itype_test_m11a_unload_mod( "test_m11a_unload_mod" );
static const itype_id itype_test_m11a_vest( "test_m11a_vest" );
static const itype_id itype_test_power_armor( "test_power_armor" );

static const move_mode_id move_mode_prone( "prone" );
static const move_mode_id move_mode_walk( "walk" );

static double eval_math( dialogue &d, const std::string_view expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

static std::string info_str( const item &it, const std::vector<iteminfo_parts> &part_flags )
{
    std::vector<iteminfo> info_v;
    const iteminfo_query query_v( part_flags );
    it.info( info_v, &query_v, 1 );
    return format_item_info( info_v, {} );
}

static bool contains( const std::string &haystack, const std::string_view needle )
{
    return haystack.find( needle ) != std::string::npos;
}

// The single worn item of a type
static item &wear( Character &you, const itype_id &id )
{
    std::optional<std::list<item>::iterator> worn = you.wear_item( item( id ), false );
    REQUIRE( worn.has_value() );
    const std::list<item>::iterator it = *worn;
    return *it;
}

// IINF-B06: get_min_str() read the avatar's archery proficiency and posture for everyone, and
// a bionic powered item's capacity was always the avatar's bionic power.
TEST_CASE( "iinf-b06_min_str_and_bionic_capacity_follow_the_holder", "[audit][item][gun]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    standard_npc guy( "Holder" );

    item gun( itype_test_m11a_gun );
    REQUIRE( gun.put_in( item( itype_test_m11a_prone_mod ), pocket_type::MOD ).success() );
    const int base = gun.type->min_str;
    REQUIRE( base > 0 );

    SECTION( "the prone modifier of a gunmod counts only for whoever is prone" ) {
        u.set_movement_mode( move_mode_prone );
        REQUIRE( u.is_prone() );
        REQUIRE_FALSE( guy.is_prone() );
        CHECK( gun.get_min_str( u ) == base + 3 );
        CHECK( gun.get_min_str( guy ) == base );
        // The no-argument form is the avatar's (item info)
        CHECK( gun.get_min_str() == base + 3 );

        guy.set_str_base( base );
        u.set_str_base( base );
        REQUIRE( guy.get_str() == base );
        REQUIRE( u.get_str() == base );
        CHECK( guy.meets_stat_requirements( gun ) );
        CHECK_FALSE( u.meets_stat_requirements( gun ) );
        u.set_movement_mode( move_mode_walk );
    }

    SECTION( "a bionic powered tool holds its holder's bionic power" ) {
        const item tool( itype_test_m11a_bionic_tool );
        u.set_max_power_level( 100_kJ );
        guy.set_max_power_level( 300_kJ );
        CHECK( tool.ammo_capacity( ammo_battery, false, &guy ) == 300 );
        CHECK( tool.ammo_capacity( ammo_battery, false, &u ) == 100 );
        // Without a holder, the avatar's (item info)
        CHECK( tool.ammo_capacity( ammo_battery ) == 100 );
        u.set_max_power_level( 0_kJ );
    }
    clear_avatar();
}

// IINF-J01: u_armor() read only worn armor, while combat also counts armor bonuses, bionics,
// mutations and the body part itself; n_armor() on an item was always 0.
TEST_CASE( "iinf-j01_armor_math_counts_what_protects_in_combat", "[audit][eoc][armor]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    const bodypart_id torso = body_part_torso.id();
    item &vest = wear( u, itype_test_m11a_vest );
    u.set_armor_res_bonus( 5, damage_bash );

    const float worn = u.worn.damage_resist( damage_bash, torso );
    REQUIRE( u.get_armor_type( damage_bash, torso ) == static_cast<int>( worn ) + 5 );

    dialogue d( get_talker_for( u ), nullptr );
    CHECK( eval_math( d, "u_armor('bash', 'torso')" ) == u.get_armor_type( damage_bash, torso ) );
    CHECK( eval_math( d, "u_armor('bash', 'torso', 'source': 'all')" ) ==
           u.get_armor_type( damage_bash, torso ) );
    CHECK( eval_math( d, "u_armor('bash', 'torso', 'source': 'worn')" ) == Approx( worn ) );

    // An item: what it gives its wearer there, for a coverage roll
    item_location vest_loc( u, &vest );
    dialogue di( get_talker_for( u ), get_talker_for( vest_loc ) );
    CHECK( eval_math( di, "n_armor('bash', 'torso')" ) ==
           Approx( vest.resist( damage_bash, false, torso ) ) );
    CHECK( eval_math( di, "n_armor('bash', 'torso', 'roll': 99)" ) ==
           Approx( vest.resist( damage_bash, false, torso, 99 ) ) );
    CHECK( eval_math( di, "n_armor('bash', 'torso')" ) > 0 );
    // It doesn't cover the head
    CHECK( eval_math( di, "n_armor('bash', 'head')" ) == 0 );

    u.set_armor_res_bonus( 0, damage_bash );
    clear_avatar();
}

// IINF-J02: clothing mod bonuses were cached by the sewing action only, so a mod flag set any
// other way (itemgroup, EOC) gave nothing, and old values stayed after a rebalance.
TEST_CASE( "iinf-j02_clothing_mod_bonus_follows_the_flag", "[audit][item][armor]" )
{
    // Coverage and encumbrance are worked out for the avatar's body
    clear_avatar();
    item vest( itype_test_m11a_vest );
    const float plain_cut = vest.resist( damage_cut );
    const int plain_encumbrance = vest.get_avg_encumber( get_avatar() );
    REQUIRE( vest.get_clothing_mod_val_for_damage_type( damage_cut ) == 0.0f );

    vest.set_flag( json_flag_leather_padded );
    CHECK( vest.get_clothing_mod_val_for_damage_type( damage_cut ) > 0.0f );
    CHECK( vest.resist( damage_cut ) > plain_cut );
    CHECK( vest.get_clothing_mod_val( clothing_mod_type_encumbrance ) > 0.0f );
    CHECK( vest.get_avg_encumber( get_avatar() ) > plain_encumbrance );

    vest.unset_flag( json_flag_leather_padded );
    CHECK( vest.get_clothing_mod_val_for_damage_type( damage_cut ) == 0.0f );
    CHECK( vest.resist( damage_cut ) == Approx( plain_cut ) );
    CHECK( vest.get_avg_encumber( get_avatar() ) == plain_encumbrance );

    SECTION( "the values older versions saved in item vars are dropped on load" ) {
        vest.set_var( "clothing_mod_cut", 7.0 );
        vest.set_var( "clothing_mod_warmth", 7.0 );
        std::ostringstream os;
        JsonOut jsout( os );
        vest.serialize( jsout );
        item loaded;
        loaded.deserialize( json_loader::from_string( os.str() ).get_object() );
        CHECK_FALSE( loaded.has_var( "clothing_mod_cut" ) );
        CHECK_FALSE( loaded.has_var( "clothing_mod_warmth" ) );
        CHECK( loaded.get_clothing_mod_val_for_damage_type( damage_cut ) == 0.0f );
    }
}

// IINF-J03: the "When active" armor info converted to "<id>_on", only for power armor and
// rm13_armor; the filter protection line looked for use actions gas masks don't have; and only
// ids ending in _on could drop " (active)".
TEST_CASE( "iinf-j03_active_armor_preview_and_active_suffix", "[audit][item][armor]" )
{
    clear_avatar();
    const std::vector<iteminfo_parts> protection = { iteminfo_parts::ARMOR_PROTECTION };

    // An explicit active form, on armor that isn't power armor
    CHECK( contains( info_str( item( itype_test_m11a_suit ), protection ), "When active" ) );
    // The active form itself (it inherits the field) shows no preview of itself
    CHECK_FALSE( contains( info_str( item( itype_test_m11a_suit_engaged ), protection ),
                           "When active" ) );
    // Power armor without an active form: no preview, and no "Missing item definition"
    CHECK_FALSE( contains( info_str( item( itype_test_m11a_bare_power_armor ), protection ),
                           "When active" ) );
    // Power armor with a transform use action keeps its preview
    CHECK( contains( info_str( item( itype_test_power_armor ), protection ), "When active" ) );

    // A filter mask shows what a filter gives
    CHECK( contains( info_str( item( itype_test_armor_chitin ), protection ),
                     "Protection when active" ) );
    CHECK_FALSE( contains( info_str( item( itype_test_m11a_vest ), protection ),
                           "Protection when active" ) );

    item lamp( itype_test_m11a_lamp );
    lamp.active = true;
    CHECK( contains( lamp.tname(), "(active)" ) );
    item quiet( itype_test_m11a_quiet_lamp );
    quiet.active = true;
    CHECK_FALSE( contains( quiet.tname(), "(active)" ) );
}

// IINF-J04: gunmod info hid many loaded fields, gun info hid the heat numbers, and the two
// blacklist labels were swapped.
TEST_CASE( "iinf-j04_gun_and_gunmod_info_show_their_fields", "[audit][item][gun]" )
{
    clear_avatar();
    const item mod( itype_test_m11a_info_mod );

    const std::string slots = info_str( mod, { iteminfo_parts::GUNMOD_BLACKLIST_SLOT } );
    CHECK( contains( slots, "Incompatible with slots" ) );
    CHECK( contains( slots, "mechanism" ) );
    const std::string mods = info_str( mod, { iteminfo_parts::GUNMOD_BLACKLIST_MOD } );
    CHECK( contains( mods, "Incompatible with gunmods" ) );
    CHECK( contains( mods, "test m11a monopod" ) );

    CHECK( contains( info_str( mod, { iteminfo_parts::GUNMOD_FIRE_MODES } ),
                     "Adds firing modes: auto (4)" ) );
    CHECK( contains( info_str( mod, { iteminfo_parts::GUNMOD_AMMO_TO_FIRE } ),
                     "Ammunition per shot modifier" ) );
    const std::string heat = info_str( mod, { iteminfo_parts::GUNMOD_HEAT } );
    CHECK( contains( heat, "Heat per shot modifier" ) );
    CHECK( contains( heat, "Cooling multiplier" ) );
    CHECK( contains( heat, "Overheat threshold modifier" ) );
    CHECK_FALSE( contains( heat, "Heat per shot multiplier" ) );
    const std::string install = info_str( mod, { iteminfo_parts::GUNMOD_INSTALL_TIME } );
    CHECK( contains( install, "Install time" ) );
    CHECK( contains( install, to_string( 30_seconds ) ) );

    item gun( itype_test_m11a_gun );
    gun.ammo_set( itype_test_9mm_ammo, 10 );
    const std::string gun_heat = info_str( gun, { iteminfo_parts::GUN_DAMAGE, iteminfo_parts::GUN_HEAT } );
    CHECK( contains( gun_heat, "Heat per shot" ) );
    CHECK( contains( gun_heat, "Cooling per turn" ) );
    CHECK( contains( gun_heat, "Overheat threshold" ) );

    // A gun that doesn't overheat has no heat block
    item cold( itype_test_m11a_cold_gun );
    REQUIRE( cold.type->gun->overheat_threshold <= 0.0 );
    cold.ammo_set( itype_test_9mm_ammo, 10 );
    CHECK_FALSE( contains( info_str( cold, { iteminfo_parts::GUN_DAMAGE, iteminfo_parts::GUN_HEAT } ),
                           "Heat per shot" ) );
}

// IINF-J05: gunmod compatibility hardcoded item ids (hand crossbow, waterproofing, brass
// catcher, stock_none, the speedloader chute) and the locations that need an unloaded gun.
TEST_CASE( "iinf-j05_gunmod_compatibility_is_data_driven", "[audit][item][gun]" )
{
    item gun( itype_test_m11a_gun );

    SECTION( "a gun flag blacklists a mod, with its message" ) {
        const ret_val<void> ret = gun.is_gunmod_compatible( item( itype_test_m11a_sealant ) );
        CHECK_FALSE( ret.success() );
        CHECK( ret.str() == "is already sealed" );
    }

    SECTION( "a template can't be installed, the mod made from it can" ) {
        CHECK_FALSE( gun.is_gunmod_compatible( item( itype_test_m11a_template ) ).success() );
        CHECK( gun.is_gunmod_compatible( item( itype_test_m11a_template_child ) ).success() );
    }

    SECTION( "the unloaded requirement" ) {
        gun.ammo_set( itype_test_9mm_ammo, 5 );
        REQUIRE( gun.ammo_remaining() == 5 );
        // set on an accessory
        CHECK_FALSE( gun.is_gunmod_compatible( item( itype_test_m11a_unload_mod ) ).success() );
        // by default for a mechanism mod
        CHECK_FALSE( gun.is_gunmod_compatible( item( itype_test_m11a_locked_mechanism ) ).success() );
        CHECK( item( itype_test_m11a_locked_mechanism ).type->gunmod->needs_unloaded() );
        // unless the mod says otherwise
        CHECK( gun.is_gunmod_compatible( item( itype_test_m11a_free_mechanism ) ).success() );
        // an accessory never needed it
        CHECK( gun.is_gunmod_compatible( item( itype_test_m11a_prone_mod ) ).success() );

        gun.ammo_unset();
        CHECK( gun.is_gunmod_compatible( item( itype_test_m11a_unload_mod ) ).success() );
        CHECK( gun.is_gunmod_compatible( item( itype_test_m11a_locked_mechanism ) ).success() );
    }

    SECTION( "a gun can take only mods that also fit a gun type" ) {
        const item small_gun( itype_test_m11a_small_gun );
        // Made for this gun by name, but not for pistols
        const ret_val<void> odd = small_gun.is_gunmod_compatible( item( itype_test_m11a_odd_mod ) );
        CHECK_FALSE( odd.success() );
        CHECK( odd.str() == "isn't big enough to use that mod" );
        CHECK( small_gun.is_gunmod_compatible( item( itype_test_m11a_prone_mod ) ).success() );
    }

    SECTION( "the vanilla rules moved to data" ) {
        CHECK( itype_hand_crossbow->gun->required_mod_targets.count( "pistol" ) == 1 );
        CHECK( itype_brass_catcher->gunmod->blacklist_gun_flags.count( flag_RELOAD_EJECT ) == 1 );
        CHECK( itype_cheek_pad->gunmod->blacklist_mod.count( itype_stock_none ) == 1 );
        CHECK_FALSE( itype_arredondo_chute->gunmod->installable );
        CHECK( itype_arredondo_chute_benelli_sa->gunmod->installable );
    }
}

// IINF-J06: clothing mods knew five damage types only, with no derived_from fallback, so
// padding never helped against stab or a modded damage type.
TEST_CASE( "iinf-j06_clothing_mods_take_any_damage_type", "[audit][item][armor]" )
{
    item vest( itype_test_m11a_vest );
    const int thickness = static_cast<int>( vest.get_thickness() );
    REQUIRE( thickness > 0 );

    SECTION( "values of their own, and the old name of heat" ) {
        vest.set_flag( json_flag_TEST_M11A_PADDED );
        CHECK( vest.get_clothing_mod_val_for_damage_type( damage_stab ) == Approx( 2.0f ) );
        CHECK( vest.get_clothing_mod_val_for_damage_type( damage_cut ) == Approx( 1.0f ) );
        CHECK( vest.get_clothing_mod_val_for_damage_type( damage_heat ) == Approx( 3.0f ) );
        CHECK( vest.get_clothing_mod_val_for_damage_type( damage_bash ) == 0.0f );
    }

    SECTION( "a derived type falls back on its parent" ) {
        // Leather padding has cut but no stab value: stab gets its share of the cut value
        vest.set_flag( json_flag_leather_padded );
        const std::pair<damage_type_id, float> &derived = damage_stab->derived_from;
        REQUIRE( derived.first == damage_cut );
        const float cut = vest.get_clothing_mod_val_for_damage_type( damage_cut );
        REQUIRE( cut > 0.0f );
        CHECK( vest.get_clothing_mod_val_for_damage_type( damage_stab ) ==
               Approx( cut * derived.second ) );
    }
}

// IINF-J07: EOCs had no gun or ammo functions, and writing n_val('power') on a gun loaded with
// bullets unloaded it.
TEST_CASE( "iinf-j07_gun_and_ammo_math", "[audit][eoc][gun]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    u.wear_item( item( itype_backpack ), false );
    item_location gun = u.i_add( item( itype_test_m11a_gun ) );
    REQUIRE( gun );
    gun->ammo_set( itype_test_9mm_ammo, 5 );
    REQUIRE( gun->ammo_remaining() == 5 );
    dialogue d( get_talker_for( u ), get_talker_for( gun ) );

    SECTION( "setting power on a gun with bullets leaves it loaded" ) {
        eval_math( d, "n_val('power') = 0" );
        CHECK( gun->ammo_remaining() == 5 );
    }

    SECTION( "ammo remaining and capacity" ) {
        CHECK( eval_math( d, "n_ammo_remaining()" ) == 5 );
        CHECK( eval_math( d, "n_ammo_capacity('')" ) == 10 );
        CHECK( eval_math( d, "n_ammo_capacity('test_9mm')" ) == gun->ammo_capacity( ammo_test_9mm ) );
        eval_math( d, "n_ammo_remaining() = 8" );
        CHECK( gun->ammo_remaining() == 8 );
        eval_math( d, "n_ammo_remaining() += 50" );
        CHECK( gun->ammo_remaining() == 10 );
        eval_math( d, "n_ammo_remaining() = 0" );
        CHECK( gun->ammo_remaining() == 0 );
        // Empty, it reloads with its default ammo
        eval_math( d, "n_ammo_remaining() = 3" );
        CHECK( gun->ammo_remaining() == 3 );
        CHECK( gun->ammo_current() == itype_test_9mm_ammo );
    }

    SECTION( "gunmods" ) {
        CHECK( eval_math( d, "n_has_gunmod('test_m11a_prone_mod')" ) == 0 );
        REQUIRE( gun->put_in( item( itype_test_m11a_prone_mod ), pocket_type::MOD ).success() );
        CHECK( eval_math( d, "n_has_gunmod('test_m11a_prone_mod')" ) == 1 );
    }

    SECTION( "heat" ) {
        CHECK( eval_math( d, "n_gun_heat()" ) == 0 );
        eval_math( d, "n_gun_heat() = 20" );
        CHECK( gun->get_var( "gun_heat", 0.0 ) == Approx( 20.0 ) );
        eval_math( d, "n_gun_heat() -= 50" );
        CHECK( gun->get_var( "gun_heat", 0.0 ) == Approx( 0.0 ) );
    }
    clear_avatar();
}

// IINF-J08: reloading, unloading, installing or removing a gunmod and damaging armor sent no
// event.
TEST_CASE( "iinf-j08_gun_and_armor_events", "[audit][event][gun][armor]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    global_variables &globals = get_globals();
    globals.clear_global_values();

    SECTION( "gunmod installed, then removed" ) {
        u.wear_item( item( itype_backpack ), false );
        item_location gun = u.i_add( item( itype_test_m11a_gun ) );
        item_location mod = u.i_add( item( itype_test_m11a_prone_mod ) );
        REQUIRE( gun );
        REQUIRE( mod );
        REQUIRE( gun->is_gunmod_compatible( *mod ).success() );

        // A sure success: 100% odds, no risk, no tool
        gunmod_add_activity_actor actor( 0, "" );
        player_activity act( actor );
        act.targets.emplace_back( gun );
        act.targets.emplace_back( mod );
        act.values = { 0, 100, 0, 0 };
        actor.finish( act, u );

        item *installed = gun->gunmod_find( itype_test_m11a_prone_mod );
        REQUIRE( installed != nullptr );
        CHECK( globals.get_global_value( "test_m11a_install_gun" ) == "test_m11a_gun" );
        CHECK( globals.get_global_value( "test_m11a_install_gunmod" ) == "test_m11a_prone_mod" );
        CHECK( globals.get_global_value( "test_m11a_install_success" ) == 1 );

        gunmod_remove_activity_actor::gunmod_remove( u, gun, *installed );
        CHECK( gun->gunmod_find( itype_test_m11a_prone_mod ) == nullptr );
        CHECK( globals.get_global_value( "test_m11a_remove_gun" ) == "test_m11a_gun" );
        CHECK( globals.get_global_value( "test_m11a_remove_gunmod" ) == "test_m11a_prone_mod" );
    }

    SECTION( "reload" ) {
        u.wear_item( item( itype_backpack ), false );
        u.i_add( item( itype_test_9mm_ammo, calendar::turn_zero, 10 ) );
        item_location gun = u.i_add( item( itype_test_m11a_gun ) );
        REQUIRE( gun );
        REQUIRE( gun->ammo_remaining() == 0 );
        item::reload_option opt = u.select_ammo( gun );
        REQUIRE( opt );
        u.assign_activity( reload_activity_actor( std::move( opt ) ) );
        if( !!u.activity ) {
            process_activity( u );
        }
        REQUIRE( gun->ammo_remaining() > 0 );
        CHECK( globals.get_global_value( "test_m11a_reload_itype" ) == "test_m11a_gun" );
        CHECK( globals.get_global_value( "test_m11a_reload_ammo" ) == "test_9mm_ammo" );
        CHECK( globals.get_global_value( "test_m11a_reload_remaining" ) == gun->ammo_remaining() );

        REQUIRE( u.unload( gun, true ) );
        CHECK( gun->ammo_remaining() == 0 );
        CHECK( globals.get_global_value( "test_m11a_unload_itype" ) == "test_m11a_gun" );
    }

    SECTION( "worn armor damaged by a hit" ) {
        // Nothing else worn, so only the vest can be damaged
        item &vest = wear( u, itype_test_m11a_vest );
        const bodypart_id torso = body_part_torso.id();
        // A hit damages armor at most 80% of the time: try until one does
        for( int i = 0; i < 200 && vest.damage() == 0; ++i ) {
            damage_unit du( damage_bash, 30.0f );
            std::list<item> remains;
            bool destroyed = false;
            u.worn.absorb_damage( u, du, torso, remains, destroyed );
            REQUIRE_FALSE( destroyed );
        }
        REQUIRE( vest.damage() > 0 );
        CHECK( globals.get_global_value( "test_m11a_damaged_itype" ) == "test_m11a_vest" );
        CHECK( globals.get_global_value( "test_m11a_damaged_type" ) == "bash" );
    }

    globals.clear_global_values();
    clear_avatar();
}

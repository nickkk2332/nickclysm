#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "butchery.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "dialogue.h"
#include "effect_source.h"
#include "harvest.h"
#include "horde_map.h"
#include "item.h"
#include "item_location.h"
#include "line.h"
#include "magic.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "map_selector.h"
#include "messages.h"
#include "mondefense.h"
#include "monster.h"
#include "mtype.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "point.h"
#include "projectile.h"
#include "shearing.h"
#include "type_id.h"

static const damage_type_id damage_acid( "acid" );
static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );
static const damage_type_id damage_electric( "electric" );
static const damage_type_id damage_heat( "heat" );

static const harvest_drop_type_id harvest_drop_bionic( "bionic" );
static const harvest_drop_type_id harvest_drop_bionic_group( "bionic_group" );
static const harvest_drop_type_id harvest_drop_flesh( "flesh" );
static const harvest_drop_type_id harvest_drop_mutagen( "mutagen" );
static const harvest_drop_type_id harvest_drop_mutagen_group( "mutagen_group" );
static const harvest_drop_type_id harvest_drop_test_m20b_dissect_only( "test_m20b_dissect_only" );

static const itype_id itype_id_military( "id_military" );
static const itype_id itype_id_science( "id_science" );
static const itype_id itype_scalpel( "scalpel" );

static const material_id material_steel( "steel" );
static const material_id material_stone( "stone" );

static const mon_flag_str_id mon_flag_HORDE_ROAMER( "HORDE_ROAMER" );

static const mtype_id mon_dog( "mon_dog" );
static const mtype_id mon_horse( "mon_horse" );
static const mtype_id mon_leech_radio( "mon_leech_radio" );
static const mtype_id mon_mech_recon( "mon_mech_recon" );
static const mtype_id mon_sheep( "mon_sheep" );
static const mtype_id mon_test_m20b_defender( "mon_test_m20b_defender" );
static const mtype_id mon_test_m20b_mech( "mon_test_m20b_mech" );
static const mtype_id mon_test_m20b_mount( "mon_test_m20b_mount" );
static const mtype_id mon_test_m20b_roamer( "mon_test_m20b_roamer" );
static const mtype_id mon_test_m20b_sheep( "mon_test_m20b_sheep" );
static const mtype_id mon_test_m20b_tissue( "mon_test_m20b_tissue" );
static const mtype_id mon_test_m20b_zapper( "mon_test_m20b_zapper" );
static const mtype_id mon_zombie( "mon_zombie" );

static const skill_id skill_electronics( "electronics" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_melee( "melee" );
static const skill_id skill_survival( "survival" );

static const spell_id spell_test_m20b_pull( "test_m20b_pull" );

namespace
{
// A creature's numeric variable, 0 when it has none
double var_of( const Creature &c, const std::string &key )
{
    const diag_value *v = c.maybe_get_value( key );
    return v ? v->dbl() : 0.0;
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
} // namespace

// longpull divided by the puller's STR or grab_strength, which can be 0, and a pull_target
// spell pulled with the caster's own strength whatever the spell's power.
TEST_CASE( "mondt-b15_longpull_survives_no_strength_and_uses_spell_damage",
           "[audit][monster][magic]" )
{
    clear_map();
    clear_avatar();
    clear_creatures();
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_avatar();
    } );
    map &here = get_map();
    avatar &you = get_avatar();
    // 81.5 kg
    monster &beast = spawn_test_monster( mon_test_m20b_tissue.str(),
                                         you.pos_bub() + tripoint( 4, 0, 0 ) );
    set_time_to_day();
    REQUIRE( you.sees( here, beast ) );
    REQUIRE( rl_dist( you.pos_bub(), beast.pos_bub() ) == 4 );
    Creature &puller = you;

    SECTION( "a strength of 0 counts as 1" ) {
        // one pull in ( 81 / 3 )^2 at most 1000, so either outcome
        puller.longpull( "test", beast.pos_bub(), 0 );
        const int dist = rl_dist( you.pos_bub(), beast.pos_bub() );
        CHECK( ( dist == 1 || dist == 4 ) );
    }
    SECTION( "a strength of 1000 always pulls" ) {
        puller.longpull( "test", beast.pos_bub(), 1000 );
        CHECK( rl_dist( you.pos_bub(), beast.pos_bub() ) == 1 );
    }
    SECTION( "a pull_target spell with damage pulls with that strength" ) {
        const spell sp( spell_test_m20b_pull );
        REQUIRE( sp.damage( you ) == 1000 );
        spell_effect::pull_to_caster( sp, you, beast.pos_bub() );
        CHECK( rl_dist( you.pos_bub(), beast.pos_bub() ) == 1 );
    }
}

// Every butchery added the tool's quality to the skill (BUTCHER 20 for a knife), so survival
// hardly mattered; only dissection is meant to count its fine cutting tool as skill.
TEST_CASE( "mondt-b16_butchery_counts_tool_quality_as_skill_only_for_dissection",
           "[audit][butchery]" )
{
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    Character &you = get_player_character();

    for( butcher_type action : {
             butcher_type::QUICK, butcher_type::FULL, butcher_type::FIELD_DRESS, butcher_type::SKIN,
             butcher_type::BLEED, butcher_type::DISMEMBER
         } ) {
        CAPTURE( static_cast<int>( action ) );
        CHECK( butchery_dissect_skill_level( you, 20, harvest_drop_flesh, action ) == 0 );
    }
    CHECK( butchery_dissect_skill_level( you, 20, harvest_drop_flesh, butcher_type::DISSECT ) == 20 );

    you.set_skill_level( skill_survival, 6 );
    CHECK( butchery_dissect_skill_level( you, 20, harvest_drop_flesh, butcher_type::FULL ) == 6 );
    CHECK( butchery_dissect_skill_level( you, 2, harvest_drop_flesh, butcher_type::DISSECT ) == 8 );

    // the average of the drop type's harvest skills
    you.set_skill_level( skill_electronics, 4 );
    you.set_skill_level( skill_firstaid, 2 );
    CHECK( butchery_dissect_skill_level( you, 20, harvest_drop_bionic, butcher_type::FULL ) == 3 );
    CHECK( butchery_dissect_skill_level( you, 2, harvest_drop_bionic, butcher_type::DISSECT ) == 4 );
}

// The yield weights are external options (BUTCHERY_YIELD_*_WEIGHT); the defaults give exactly
// the numbers of the old literals 0.5, 0.3 and 0.2.
TEST_CASE( "mondt-b16_butchery_yield_weights_are_options", "[audit][butchery]" )
{
    for( const int skill : {
             0, 2, 5, 10, 12
         } ) {
        for( const int dex : {
                 4, 8, 20
             } ) {
            for( const int tool : {
                     0, 20, 50
                 } ) {
                CAPTURE( skill, dex, tool );
                const double old_mult = 0.5 * std::clamp( skill / 10.0, 0.0, 1.0 ) +
                                        0.3 * std::clamp( ( tool + 50.0 ) / 100.0, 0.0, 1.0 ) +
                                        0.2 * std::clamp( dex / 20.0, 0.0, 1.0 );
                CHECK( butchery_dissect_yield_mult( skill, dex, tool ) == old_mult );
            }
        }
    }
    CHECK( butchery_dissect_yield_mult( 0, 8, 20 ) == Approx( 0.29 ) );
    {
        override_option skill_weight( "BUTCHERY_YIELD_SKILL_WEIGHT", "0.25" );
        override_option tool_weight( "BUTCHERY_YIELD_TOOL_WEIGHT", "0.55" );
        CHECK( butchery_dissect_yield_mult( 0, 8, 20 ) == Approx( 0.465 ) );
        CHECK( butchery_dissect_yield_mult( 10, 8, 20 ) == Approx( 0.715 ) );
    }
    CHECK( butchery_dissect_yield_mult( 0, 8, 20 ) == Approx( 0.29 ) );
}

// dissect_only was loaded and never read.
TEST_CASE( "mondt-j04_dissect_only_drops_come_only_from_dissection", "[audit][butchery]" )
{
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    Character &you = get_player_character();
    const item corpse = item::make_corpse( mon_test_m20b_tissue );
    const mtype &mt = *corpse.get_mtype();

    const harvest_drop_type &only = harvest_drop_test_m20b_dissect_only.obj();
    REQUIRE( only.dissect_only() );
    for( butcher_type action : {
             butcher_type::QUICK, butcher_type::FULL, butcher_type::FIELD_DRESS, butcher_type::SKIN,
             butcher_type::BLEED, butcher_type::DISMEMBER
         } ) {
        CAPTURE( static_cast<int>( action ) );
        CHECK_FALSE( butchery_entry_yield( only, 5, action, corpse, mt, you ) );
    }
    CHECK( butchery_entry_yield( only, 5, butcher_type::DISSECT, corpse, mt, you ) == 5 );

    // The vanilla organ and implant types aren't dissect_only: the harvest lists of Magiclysm and
    // Aftershock still give them from full butchery, as before.
    for( const harvest_drop_type_id &type : {
             harvest_drop_bionic, harvest_drop_bionic_group, harvest_drop_mutagen,
             harvest_drop_mutagen_group
         } ) {
        CAPTURE( type.str() );
        CHECK_FALSE( type->dissect_only() );
        CHECK( butchery_entry_yield( type.obj(), 5, butcher_type::FULL, corpse, mt, you ) == 5 );
        CHECK( butchery_entry_yield( type.obj(), 5, butcher_type::DISSECT, corpse, mt, you ) == 5 );
    }
}

// msg_dissect_fail and msg_dissect_success had no caller.
TEST_CASE( "mondt-j04_dissection_shows_the_drop_types_dissect_message", "[audit][butchery]" )
{
    clear_map();
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_map();
        clear_avatar();
    } );
    map &here = get_map();
    Character &you = get_player_character();
    // dissection needs a fine cutting tool
    item scalpel( itype_scalpel );
    REQUIRE( you.wield( scalpel ) );
    const tripoint_bub_ms pos = you.pos_bub();
    here.i_clear( pos );
    item &corpse = here.add_item( pos, item::make_corpse( mon_test_m20b_tissue ) );
    REQUIRE( !corpse.is_null() );
    item_location loc( map_cursor( you.pos_abs() ), &corpse );

    Messages::clear_messages();
    // Its flesh entry always comes to nothing: flesh's msg_dissect_fail says so
    butchery_data bd( loc, butcher_type::DISSECT );
    butchery_drops_harvest( bd, you );
    CHECK( has_message_containing( "damaged tissue" ) );
    here.i_clear( pos );
}

// Only ZOMBIE or FERAL monsters (with HEARS) joined the moving overmap hordes.
TEST_CASE( "mondt-j06_horde_roamers_join_the_moving_hordes", "[audit][hordes]" )
{
    // from the ZOMBIE species
    CHECK( mon_zombie->has_flag( mon_flag_HORDE_ROAMER ) );
    CHECK_FALSE( mon_dog->has_flag( mon_flag_HORDE_ROAMER ) );
    REQUIRE( mon_test_m20b_roamer->has_flag( mon_flag_HORDE_ROAMER ) );

    horde_map hordes;
    hordes.set_location( point_abs_om( 42, 42 ) );
    const auto spawn_at = [&hordes]( const mtype_id & id, const int x ) {
        const tripoint_om_ms local( x, 30, 0 );
        hordes.spawn_entity( project_combine( hordes.get_location(), local ), id );
        return project_to<coords::sm>( local );
    };
    const tripoint_om_sm zombie_sm = spawn_at( mon_zombie, 30 );
    const tripoint_om_sm roamer_sm = spawn_at( mon_test_m20b_roamer, 60 );
    const tripoint_om_sm dog_sm = spawn_at( mon_dog, 90 );

    CHECK( hordes.entity_group_at( zombie_sm, horde_map_flavors::idle ).size() == 1 );
    CHECK( hordes.entity_group_at( roamer_sm, horde_map_flavors::idle ).size() == 1 );
    CHECK( hordes.entity_group_at( roamer_sm, horde_map_flavors::immobile ).empty() );
    CHECK( hordes.entity_group_at( dog_sm, horde_map_flavors::idle ).empty() );
    CHECK( hordes.entity_group_at( dog_sm, horde_map_flavors::immobile ).size() == 1 );
}

// special_when_hit took one built-in defense; it now takes a list, with EOCs.
TEST_CASE( "mondt-j07_special_when_hit_takes_several_defenses_and_eocs", "[audit][monster]" )
{
    clear_map();
    clear_avatar();
    clear_creatures();
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_avatar();
    } );
    avatar &you = get_avatar();

    SECTION( "EOC defenses get the attacker, the body part and the kind of hit" ) {
        REQUIRE( mon_test_m20b_defender->special_defenses.size() == 3 );
        monster &beast = spawn_test_monster( mon_test_m20b_defender.str(),
                                             you.pos_bub() + tripoint::east );

        beast.special_defense( &you, body_part_torso.id(), nullptr );
        CHECK( var_of( beast, "test_m20b_hits" ) == 1 );
        CHECK( var_of( beast, "test_m20b_melee" ) == 1 );
        CHECK( var_of( beast, "test_m20b_ranged" ) == 0 );
        CHECK( var_of( beast, "test_m20b_torso" ) == 1 );
        CHECK( var_of( you, "test_m20b_attacked" ) == 1 );

        // the melee-only one isn't tried for a projectile
        const dealt_projectile_attack shot{};
        beast.special_defense( &you, body_part_torso.id(), &shot );
        CHECK( var_of( beast, "test_m20b_hits" ) == 2 );
        CHECK( var_of( beast, "test_m20b_melee" ) == 1 );
        CHECK( var_of( beast, "test_m20b_ranged" ) == 1 );
        CHECK( var_of( beast, "test_m20b_torso" ) == 2 );

        // no attacker, as being held by nothing in particular
        beast.special_defense( nullptr, bodypart_str_id::NULL_ID().id(), nullptr );
        CHECK( var_of( beast, "test_m20b_hits" ) == 3 );
        CHECK( var_of( beast, "test_m20b_melee" ) == 2 );
        CHECK( var_of( beast, "test_m20b_torso" ) == 2 );
        CHECK( var_of( you, "test_m20b_attacked" ) == 2 );
    }
    SECTION( "built-in defenses in both forms" ) {
        const mtype &zapper = *mon_test_m20b_zapper;
        REQUIRE( zapper.special_defenses.size() == 1 );
        CHECK( zapper.special_defenses[0].defense == &mdefense::zapback );
        CHECK( zapper.special_defenses[0].chance == 75 );
        CHECK( monster( mon_test_m20b_zapper ).is_immune_damage( damage_electric ) );

        // the array form, on a monster with no ELECTRIC flag
        REQUIRE( mon_leech_radio->special_defenses.size() == 1 );
        CHECK( mon_leech_radio->special_defenses[0].defense == &mdefense::zapback );
        CHECK( mon_leech_radio->special_defenses[0].chance == 100 );
        CHECK( monster( mon_leech_radio ).is_immune_damage( damage_electric ) );

        // without special_when_hit: NONE, rolled for on every hit as before
        REQUIRE( mon_dog->special_defenses.size() == 1 );
        CHECK( mon_dog->special_defenses[0].defense == &mdefense::none );
        CHECK( mon_dog->special_defenses[0].chance == 0 );
        CHECK_FALSE( monster( mon_dog ).is_immune_damage( damage_electric ) );
    }
}

// Riding needed survival 1 saddled or 4 without, and a mount one size larger, for every mount.
TEST_CASE( "mondt-j08_mount_rules_come_from_json", "[audit][monster][mount]" )
{
    const mon_mount_rules &horse = mon_horse->mount_rules;
    CHECK( horse.skill == skill_survival );
    CHECK( horse.saddled_level == 1 );
    CHECK( horse.unsaddled_level == 4 );
    CHECK( horse.tack_level == 1 );
    CHECK( horse.size_difference == 1 );

    // the fields left out keep the defaults
    const mon_mount_rules &rules = mon_test_m20b_mount->mount_rules;
    CHECK( rules.skill == skill_melee );
    CHECK( rules.saddled_level == 1 );
    CHECK( rules.unsaddled_level == 3 );
    CHECK( rules.tack_level == 1 );
    CHECK( rules.size_difference == 1 );

    clear_map();
    clear_avatar();
    clear_creatures();
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_avatar();
    } );
    avatar &you = get_avatar();
    monster &steed = spawn_test_monster( mon_test_m20b_mount.str(), you.pos_bub() + tripoint::east );
    steed.friendly = -1;
    // survival doesn't help on this one
    you.set_skill_level( skill_survival, 10 );
    CHECK_FALSE( you.can_mount( steed ) );
    you.set_skill_level( skill_melee, 3 );
    CHECK( you.can_mount( steed ) );
}

// Shearing was blocked in winter, regrew in a season and needed SHEAR 1, for every animal.
TEST_CASE( "mondt-j08_shearing_rules_come_from_json", "[audit][monster][shearing]" )
{
    const shearing_data &sheep = mon_sheep->shearing;
    REQUIRE( sheep.valid() );
    CHECK( sheep.in_season( SPRING ) );
    CHECK( sheep.in_season( SUMMER ) );
    CHECK( sheep.in_season( AUTUMN ) );
    CHECK_FALSE( sheep.in_season( WINTER ) );
    CHECK( sheep.regrow() == calendar::season_length() );
    CHECK( sheep.quality_level() == 1 );

    const shearing_data &winter = mon_test_m20b_sheep->shearing;
    REQUIRE( winter.valid() );
    CHECK( winter.entries().size() == 1 );
    CHECK( winter.in_season( WINTER ) );
    CHECK_FALSE( winter.in_season( SPRING ) );
    CHECK_FALSE( winter.in_season( SUMMER ) );
    CHECK_FALSE( winter.in_season( AUTUMN ) );
    CHECK( winter.regrow() == 2_days );
    CHECK( winter.quality_level() == 2 );
}

// Activating a rideable mech always used up a military ID card.
TEST_CASE( "mondt-j08_mech_activation_item_comes_from_json", "[audit][monster]" )
{
    CHECK( mon_mech_recon->mech_activation_item == itype_id_military );
    CHECK( mon_test_m20b_mech->mech_activation_item == itype_id_science );
}

// Pain per damage type, setting on fire, zapping and the steel and stone immunity to heat were
// hardcoded per damage type id.
TEST_CASE( "mondt-j10_damage_type_side_effects_come_from_json", "[audit][damage]" )
{
    CHECK( damage_bash->pain_divisor == 5.0 );
    CHECK( damage_acid->pain_divisor == 3.0 );
    CHECK( damage_cut->pain_divisor == 4.0 );
    CHECK( damage_heat->ignites );
    CHECK_FALSE( damage_cut->ignites );
    CHECK( damage_electric->electrocutes );
    CHECK_FALSE( damage_heat->electrocutes );
    const std::vector<material_id> &heat_proof = damage_heat->mon_immune_materials;
    CHECK( std::count( heat_proof.begin(), heat_proof.end(), material_steel ) == 1 );
    CHECK( std::count( heat_proof.begin(), heat_proof.end(), material_stone ) == 1 );

    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    Character &you = get_player_character();
    // damages that divide exactly, so no random rounding of the pain
    const std::vector<std::pair<damage_type_id, int>> cases = {
        { damage_bash, 20 }, { damage_cut, 20 }, { damage_acid, 21 }, { damage_electric, 20 }
    };
    for( const std::pair<damage_type_id, int> &c : cases ) {
        CAPTURE( c.first.str() );
        int damage = 0;
        int pain = 0;
        you.deal_damage_handle_type( effect_source::empty(), damage_unit( c.first, c.second ),
                                     body_part_arm_l.id(), damage, pain );
        CHECK( damage == c.second );
        CHECK( pain == static_cast<int>( c.second / c.first->pain_divisor ) );
    }

    CHECK( monster( mon_test_m20b_zapper ).is_immune_damage( damage_heat ) );
    CHECK_FALSE( monster( mon_test_m20b_defender ).is_immune_damage( damage_heat ) );
}

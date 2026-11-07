#include <initializer_list>
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
#include "cata_variant.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "json_error.h"
#include "json_loader.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "magic_ter_furn_transform.h"
#include "magic_type.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player_helpers.h"
#include "point.h"
#include "stats_tracker.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_AUDIT_M27B_FAIL_EOC( "AUDIT_M27B_FAIL_EOC" );
static const effect_on_condition_id
effect_on_condition_AUDIT_M27B_TYPE_FAIL_EOC( "AUDIT_M27B_TYPE_FAIL_EOC" );

static const efftype_id effect_audit_m27b_source_effect( "audit_m27b_source_effect" );

static const field_type_str_id field_fd_blood( "fd_blood" );
static const field_type_str_id field_fd_fire( "fd_fire" );
static const field_type_str_id field_fd_smoke( "fd_smoke" );

static const itype_id itype_audit_m27b_kill_counter( "audit_m27b_kill_counter" );
static const itype_id itype_test_boxing_gloves( "test_boxing_gloves" );

static const magic_type_id magic_audit_m27b_heavy_magic_copy( "audit_m27b_heavy_magic_copy" );

static const mtype_id mon_test_non_shearable( "mon_test_non_shearable" );

static const spell_id spell_audit_m27b_cost_default( "audit_m27b_cost_default" );
static const spell_id spell_audit_m27b_cost_heavy( "audit_m27b_cost_heavy" );
static const spell_id spell_audit_m27b_flags_child( "audit_m27b_flags_child" );
static const spell_id spell_audit_m27b_learned_spell( "audit_m27b_learned_spell" );
static const spell_id spell_audit_m27b_level_formula( "audit_m27b_level_formula" );
static const spell_id spell_audit_m27b_level_formula_copy( "audit_m27b_level_formula_copy" );
static const spell_id spell_audit_m27b_monster_cast( "audit_m27b_monster_cast" );
static const spell_id spell_audit_m27b_owner_max_level( "audit_m27b_owner_max_level" );
static const spell_id spell_audit_m27b_stats_spell( "audit_m27b_stats_spell" );
static const spell_id spell_audit_m27b_teacher_spell( "audit_m27b_teacher_spell" );

static const ter_furn_transform_id
ter_furn_transform_audit_m27b_transform( "audit_m27b_transform" );

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_grass( "t_grass" );
static const ter_str_id ter_t_sand( "t_sand" );

namespace
{

// A spell at a level, built without the level-up events
spell spell_at_level( const spell_id &id, const int level, const int adjustment = 0 )
{
    spell sp( id );
    sp.set_exp( sp.exp_for_level( level ) );
    sp.set_temp_level_adjustment( adjustment );
    return sp;
}

void load_enchantment_json( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    enchantment ench;
    ench.load( jo );
}

void load_relic_enchantment_json( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    enchant_cache ench;
    ench.load( jo );
}

double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

struct spell_event_listener : public event_subscriber {
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        switch( e.type() ) {
            case event_type::character_levels_spell:
                level_ups.push_back( e.get<cata_variant_type::character_id>( "character" ) );
                break;
            case event_type::player_levels_spell:
                ++player_level_ups;
                break;
            case event_type::monster_casts_spell:
                monster_casts.push_back( e );
                break;
            case event_type::character_casts_spell:
                character_casts.push_back( e );
                break;
            default:
                break;
        }
    }
    std::vector<character_id> level_ups;
    int player_level_ups = 0;
    std::vector<cata::event> monster_casts;
    std::vector<cata::event> character_casts;
};

// Puts back the game's statistics of the spell events these tests send
struct spell_stats_restorer {
    std::vector<std::pair<event_type, event_multiset>> saved;
    spell_stats_restorer() {
        for( const event_type type : {
                 event_type::character_casts_spell, event_type::character_levels_spell,
                 event_type::player_levels_spell, event_type::character_learns_spell
             } ) {
            saved.emplace_back( type, get_stats().get_events( type ) );
        }
    }
    ~spell_stats_restorer() {
        for( const std::pair<event_type, event_multiset> &events : saved ) {
            get_stats().get_events( events.first ) = events.second;
        }
    }
};

} // namespace

// MAGIC-B16: a value listed twice in one enchantment counted only the first time (the hound
// ichor's seven ARTIFACT_RESONANCE adds gave only the torso's); now it fails to load.
TEST_CASE( "magic-b16_repeated_enchantment_values_fail_to_load", "[audit][magic]" )
{
    CHECK_THROWS_AS( load_enchantment_json( R"({ "id": "audit_m27b_dup", "values": [
        { "value": "SPEED", "add": 1 }, { "value": "SPEED", "add": 2 } ] })" ), JsonError );
    CHECK_THROWS_AS( load_enchantment_json( R"({ "id": "audit_m27b_dup", "skills": [
        { "value": "melee", "multiply": 1 }, { "value": "melee", "multiply": 2 } ] })" ),
                     JsonError );
    CHECK_THROWS_AS( load_enchantment_json( R"({ "id": "audit_m27b_dup", "ench_effects": [
        { "effect": "audit_m27b_source_effect", "intensity": 1 },
        { "effect": "audit_m27b_source_effect", "intensity": 2 } ] })" ), JsonError );
    // a relic's inline enchantment as well
    CHECK_THROWS_AS( load_relic_enchantment_json( R"({ "values": [
        { "value": "SPEED", "add": 1 }, { "value": "SPEED", "add": 2 } ] })" ), JsonError );
    // an add and a multiply of the same value are two entries
    CHECK_NOTHROW( load_enchantment_json( R"({ "id": "audit_m27b_dup", "values": [
        { "value": "SPEED", "add": 1 }, { "value": "SPEED", "multiply": 0.5 } ] })" ) );
}

// MAGIC-B16: a copy-from child lost its parent's "has" and "condition", couldn't change an
// inherited value, and couldn't extend or delete hit_you_effect.
TEST_CASE( "magic-b16_enchantment_copy_from_keeps_and_overrides", "[audit][magic]" )
{
    clear_avatar();
    const avatar &u = get_avatar();
    enchantment ench;
    {
        const JsonObject parent = json_loader::from_string( R"({ "id": "audit_m27b_parent",
            "has": "WORN", "condition": "ACTIVE",
            "values": [ { "value": "SPEED", "add": 5 }, { "value": "STRENGTH", "add": 2 } ],
            "ench_effects": [ { "effect": "audit_m27b_source_effect", "intensity": 1 } ],
            "hit_you_effect": [ { "id": "audit_m27b_learned_spell" }, { "id": "audit_m27b_teacher_spell" } ] })" );
        ench.load( parent );
    }
    // as generic_factory loads a copy: the parent's copy, loaded again with the child's JSON
    ench.was_loaded = true;
    {
        const JsonObject child = json_loader::from_string( R"({ "id": "audit_m27b_child",
            "values": [ { "value": "SPEED", "add": 10 } ],
            "ench_effects": [ { "effect": "audit_m27b_source_effect", "intensity": 3 } ],
            "delete": { "hit_you_effect": [ "audit_m27b_learned_spell" ] },
            "extend": { "hit_you_effect": [ { "id": "audit_m27b_stats_spell" } ] } })" );
        ench.load( child );
    }
    CHECK( ench.active_conditions.first == enchantment::has::WORN );
    CHECK( ench.active_conditions.second == enchantment::condition::ACTIVE );
    CHECK( ench.get_value_add( enchant_vals::mod::SPEED, u ) == Approx( 10 ) );
    CHECK( ench.get_value_add( enchant_vals::mod::STRENGTH, u ) == Approx( 2 ) );
    REQUIRE( ench.ench_effects.count( effect_audit_m27b_source_effect ) == 1 );
    CHECK( ench.ench_effects.at( effect_audit_m27b_source_effect ) == 3 );
    std::vector<spell_id> hit_you;
    hit_you.reserve( ench.hit_you_effect.size() );
    for( const fake_spell &sp : ench.hit_you_effect ) {
        hit_you.push_back( sp.id );
    }
    CHECK( hit_you == std::vector<spell_id> { spell_audit_m27b_teacher_spell, spell_audit_m27b_stats_spell } );
}

// MAGIC-B16: "extend" and "delete" of a copy-from spell's flags were ignored (Magiclysm's
// horizon vortex spells never got RANDOM_DAMAGE), and a copy-from magic_type's
// cannot_cast_flags couldn't be extended.
TEST_CASE( "magic-b16_copy_from_spells_and_magic_types_extend_and_delete_flags",
           "[audit][magic]" )
{
    const spell child( spell_audit_m27b_flags_child );
    CHECK( child.has_flag( spell_flag::RANDOM_DAMAGE ) );
    CHECK( child.has_flag( "RANDOM_DAMAGE" ) );
    CHECK( child.has_flag( spell_flag::SOMATIC ) );
    CHECK_FALSE( child.has_flag( spell_flag::VERBAL ) );
    CHECK_FALSE( child.has_flag( "VERBAL" ) );

    const magic_type &copy = magic_audit_m27b_heavy_magic_copy.obj();
    CHECK( copy.cannot_cast_flags ==
           std::set<std::string> { "NO_PSIONICS", "NO_SPELLCASTING" } );
    // what it doesn't give it inherits
    CHECK( copy.failure_eocs.size() == 1 );
    CHECK( copy.encumbrance.hands_cost == std::optional<int>( 100 ) );
}

// MAGIC-J05: only the avatar sent level-up events, so an NPC never learned a spell's
// learn_spells, and NPC spellbook adjustments were capped at the avatar's max level.
TEST_CASE( "magic-j05_npcs_level_spells_and_learn_follow_up_spells", "[audit][magic][npc]" )
{
    const spell_stats_restorer restore_stats;
    clear_avatar();
    clear_map();
    clear_npcs();
    npc &guy = spawn_npc( point_bub_ms( 30, 30 ), "test_talker" );
    on_out_of_scope cleanup( []() {
        clear_npcs();
    } );
    guy.magic->learn_spell( spell_audit_m27b_teacher_spell, guy, true );
    REQUIRE( guy.magic->knows_spell( spell_audit_m27b_teacher_spell ) );
    REQUIRE_FALSE( guy.magic->knows_spell( spell_audit_m27b_learned_spell ) );

    spell_event_listener listener;
    get_event_bus().subscribe( &listener );
    spell &sp = guy.magic->get_spell( spell_audit_m27b_teacher_spell );
    sp.gain_level( guy );
    // learned at level 2
    CHECK_FALSE( guy.magic->knows_spell( spell_audit_m27b_learned_spell ) );
    sp.gain_level( guy );
    get_event_bus().unsubscribe( &listener );

    CHECK( sp.get_level() == 2 );
    CHECK( guy.magic->knows_spell( spell_audit_m27b_learned_spell ) );
    CHECK_FALSE( get_avatar().magic->knows_spell( spell_audit_m27b_learned_spell ) );
    CHECK( listener.level_ups == std::vector<character_id> { guy.getID(), guy.getID() } );
    CHECK( listener.player_level_ups == 0 );
}

TEST_CASE( "magic-j05_npc_spellbook_adjustments_use_the_npcs_max_level", "[audit][magic][npc]" )
{
    const spell_stats_restorer restore_stats;
    clear_avatar();
    clear_map();
    clear_npcs();
    npc &guy = spawn_npc( point_bub_ms( 30, 30 ), "test_talker" );
    on_out_of_scope cleanup( []() {
        get_avatar().remove_value( "audit_m27b_cap" );
        clear_npcs();
    } );
    // the spell's max level is its caster's audit_m27b_cap variable
    guy.set_value( "audit_m27b_cap", 4 );
    get_avatar().set_value( "audit_m27b_cap", 20 );
    guy.magic->learn_spell( spell_audit_m27b_owner_max_level, guy, true );
    spell &sp = guy.magic->get_spell( spell_audit_m27b_owner_max_level );
    sp.set_exp( sp.exp_for_level( 3 ) );
    REQUIRE( sp.get_level() == 3 );
    sp.set_temp_level_adjustment( 10 );

    guy.magic->evaluate_opens_spellbook_data( guy );
    // 3 + 10, capped at the NPC's 4 (the avatar's cap would have given 13)
    CHECK( sp.get_effective_level() == 4 );
}

// MAGIC-J06: ter_furn_transform entries had no chance, and a field kept its old intensity.
TEST_CASE( "magic-j06_transform_entries_have_a_chance_and_field_intensity", "[audit][magic]" )
{
    clear_avatar();
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms grass( 40, 40, 0 );
    const tripoint_bub_ms sand( 42, 40, 0 );
    const tripoint_bub_ms fire( 44, 40, 0 );
    const tripoint_bub_ms blood( 46, 40, 0 );
    here.ter_set( grass, ter_t_grass );
    here.ter_set( sand, ter_t_sand );
    here.ter_set( fire, ter_t_floor );
    here.ter_set( blood, ter_t_floor );
    REQUIRE( here.add_field( fire, field_fd_fire.id(), 3 ) );
    REQUIRE( here.add_field( blood, field_fd_blood.id(), 1 ) );

    const ter_furn_transform &transform = ter_furn_transform_audit_m27b_transform.obj();
    for( const tripoint_bub_ms &p : {
             grass, sand, fire, blood
         } ) {
        transform.transform( here, p );
    }

    // chance 0: never
    CHECK( here.ter( grass ).id() == ter_t_grass );
    // chance 100: always
    CHECK( here.ter( sand ).id() == ter_t_floor );
    // the fire became smoke at intensity 1
    CHECK( here.get_field_intensity( fire, field_fd_fire.id() ) == 0 );
    CHECK( here.get_field_intensity( fire, field_fd_smoke.id() ) == 1 );
    // blood turned into blood keeps its field, at the entry's intensity
    CHECK( here.get_field_intensity( blood, field_fd_blood.id() ) == 3 );
    clear_map();
}

// MAGIC-J07: spell formulas had no spell context, so content wrote u_spell_level('own_id'),
// which is wrong for casters that don't know the spell, monsters and copy-from spells.
TEST_CASE( "magic-j07_spell_formulas_read_their_own_level", "[audit][magic]" )
{
    const spell_stats_restorer restore_stats;
    clear_avatar();
    clear_map();
    clear_creatures();
    avatar &u = get_avatar();
    // min_damage is _spell_level * 3 + 1
    REQUIRE_FALSE( u.magic->knows_spell( spell_audit_m27b_level_formula ) );
    CHECK( spell_at_level( spell_audit_m27b_level_formula, 4 ).damage( u ) == 13 );
    // the level it is cast at, temporary adjustments included
    CHECK( spell_at_level( spell_audit_m27b_level_formula, 4, 2 ).damage( u ) == 19 );

    // a copy reads its own level, not its parent's
    u.magic->learn_spell( spell_audit_m27b_level_formula, u, true );
    u.magic->get_spell( spell_audit_m27b_level_formula ).set_exp( 0 );
    CHECK( spell_at_level( spell_audit_m27b_level_formula_copy, 5 ).damage( u ) == 16 );

    // a monster caster
    monster &mon = spawn_test_monster( mon_test_non_shearable.str(), tripoint_bub_ms( 50, 50, 0 ) );
    CHECK( spell_at_level( spell_audit_m27b_level_formula, 4 ).damage( mon ) == 13 );
    clear_creatures();
    clear_avatar();
}

// MAGIC-J08: an item's enchantment couldn't read the item, nor an effect's its intensity.
TEST_CASE( "magic-j08_enchantments_read_their_source", "[audit][magic]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    u.recalculate_enchantment_cache();
    REQUIRE( u.enchantment_cache->get_value_add( enchant_vals::mod::SPEED ) == Approx( 0 ) );

    SECTION( "an item's variables" ) {
        item counter( itype_audit_m27b_kill_counter );
        counter.set_var( "audit_m27b_kills", 12.0 );
        REQUIRE( static_cast<bool>( u.i_add( counter ) ) );
        u.recalculate_enchantment_cache();
        // n_audit_m27b_kills
        CHECK( u.enchantment_cache->get_value_add( enchant_vals::mod::SPEED ) == Approx( 12 ) );
    }
    SECTION( "an effect's intensity" ) {
        u.add_effect( effect_audit_m27b_source_effect, 1_hours, false, 3 );
        REQUIRE( u.get_effect_int( effect_audit_m27b_source_effect ) == 3 );
        u.recalculate_enchantment_cache();
        // _source_intensity * 2
        CHECK( u.enchantment_cache->get_value_add( enchant_vals::mod::SPEED ) == Approx( 6 ) );
        dialogue d( get_talker_for( u ), nullptr );
        // MAGIC-J10: the cached values in math
        CHECK( eval_math( d, "u_enchantment_value('SPEED')" ) == Approx( 6 ) );
        CHECK( eval_math( d, "u_enchantment_value('SPEED', 'type': 'multiply')" ) == Approx( 0 ) );
    }
    clear_avatar();
    u.recalculate_enchantment_cache();
}

// MAGIC-J09: the encumbrance penalties on spells were constants; a magic_type can set them.
TEST_CASE( "magic-j09_magic_types_set_the_encumbrance_penalties", "[audit][magic]" )
{
    // the defaults are the old constants
    const spell_encumbrance defaults;
    CHECK( defaults.hands_free == 5 );
    CHECK( defaults.cost_per_hand_point( magic_energy_type::mana ) == 10 );
    CHECK( defaults.cost_per_hand_point( magic_energy_type::hp ) == 1 );
    CHECK( defaults.cost_per_hand_point( magic_energy_type::stamina ) == 100 );
    CHECK( defaults.legs_free == 10 );
    CHECK( defaults.legs_time == 3 );
    CHECK( defaults.arms_free == 10 );
    CHECK( defaults.arms_time == 2 );
    CHECK( defaults.arms_fail == Approx( 0.5 ) );
    CHECK( defaults.mouth_free == 5 );
    CHECK( defaults.mouth_fail == Approx( 1.0 ) );

    clear_avatar();
    avatar &u = get_avatar();
    const spell plain( spell_audit_m27b_cost_default );
    const spell heavy( spell_audit_m27b_cost_heavy );
    REQUIRE( plain.energy_cost( u ) == 10 );
    REQUIRE( heavy.energy_cost( u ) == 10 );

    u.wear_item( item( itype_test_boxing_gloves ), false );
    const int hands = u.avg_encumb_of_limb_type( bp_type::hand );
    REQUIRE( hands > 5 );
    // 10 a point above 5
    CHECK( plain.energy_cost( u ) == 10 + 10 * ( hands - 5 ) );
    // audit_m27b_heavy_magic: 100 a point above 0
    CHECK( heavy.energy_cost( u ) == 10 + 100 * hands );
    clear_avatar();
}

// MAGIC-J10: EOCs couldn't read a spell's damage, range, area, duration or max level, cast
// events had no level, monster casts sent no event, and only magic types had failure EOCs.
TEST_CASE( "magic-j10_spell_values_in_math", "[audit][magic][math_parser]" )
{
    const spell_stats_restorer restore_stats;
    clear_avatar();
    avatar &u = get_avatar();
    u.magic->learn_spell( spell_audit_m27b_stats_spell, u, true );
    spell &sp = u.magic->get_spell( spell_audit_m27b_stats_spell );
    sp.set_exp( sp.exp_for_level( 4 ) );
    REQUIRE( sp.get_level() == 4 );

    dialogue d( get_talker_for( u ), nullptr );
    // 10 + 4 * 5
    CHECK( eval_math( d, "u_spell_damage('audit_m27b_stats_spell')" ) == Approx( 30 ) );
    // 5 + 4 * 1
    CHECK( eval_math( d, "u_spell_range('audit_m27b_stats_spell')" ) == Approx( 9 ) );
    CHECK( eval_math( d, "u_spell_aoe('audit_m27b_stats_spell')" ) == Approx( sp.aoe( u ) ) );
    // 300 moves
    CHECK( eval_math( d, "u_spell_duration('audit_m27b_stats_spell')" ) == Approx( 3 ) );
    CHECK( eval_math( d, "u_spell_max_level('audit_m27b_stats_spell')" ) == Approx( 20 ) );
    clear_avatar();
}

TEST_CASE( "magic-j10_cast_events_carry_the_level_and_monsters_send_one", "[audit][magic]" )
{
    const spell_stats_restorer restore_stats;
    clear_avatar();
    clear_map();
    clear_creatures();
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    monster &mon = spawn_test_monster( mon_test_non_shearable.str(), tripoint_bub_ms( 50, 50, 0 ) );
    const spell sp = spell_at_level( spell_audit_m27b_monster_cast, 3 );

    spell_event_listener listener;
    get_event_bus().subscribe( &listener );
    sp.cast_all_effects( mon, mon.pos_bub() );
    sp.cast_all_effects( u, u.pos_bub() );
    get_event_bus().unsubscribe( &listener );

    REQUIRE( listener.monster_casts.size() == 1 );
    const cata::event &monster_cast = listener.monster_casts.front();
    CHECK( monster_cast.get<cata_variant_type::mtype_id>( "monster_type" ) ==
           mon_test_non_shearable );
    CHECK( monster_cast.get<cata_variant_type::spell_id>( "spell" ) == spell_audit_m27b_monster_cast );
    CHECK( monster_cast.get<cata_variant_type::int_>( "level" ) == 3 );
    CHECK( monster_cast.get<cata_variant_type::int_>( "damage" ) == 4 );
    REQUIRE( listener.character_casts.size() == 1 );
    CHECK( listener.character_casts.front().get<cata_variant_type::int_>( "level" ) == 3 );
    // the spell's effect ran for both
    CHECK( get_globals().get_global_value( "audit_m27b_monster_cast_ran" ) == 1 );
    // every monster's every cast would grow the saved statistics
    CHECK_FALSE( stats_tracker::records( event_type::monster_casts_spell ) );
    CHECK( stats_tracker::unrecorded_fields( event_type::character_casts_spell ) ==
           std::vector<std::string> { "target", "level" } );

    get_globals().clear_global_values();
    clear_creatures();
    clear_avatar();
}

TEST_CASE( "magic-j10_spells_have_failure_eocs_of_their_own", "[audit][magic]" )
{
    const std::vector<effect_on_condition_id> eocs =
        spell( spell_audit_m27b_cost_heavy ).get_failure_eoc_ids();
    // the magic type's first
    CHECK( eocs == std::vector<effect_on_condition_id> {
        effect_on_condition_AUDIT_M27B_TYPE_FAIL_EOC, effect_on_condition_AUDIT_M27B_FAIL_EOC
    } );
    CHECK( spell( spell_audit_m27b_cost_default ).get_failure_eoc_ids().empty() );
}

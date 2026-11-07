#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "effect_source.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monfaction.h"
#include "monster.h"
#include "monster_oracle.h"
#include "mtype.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_EOC_TEST_MON_USE_SPECIAL( "EOC_TEST_MON_USE_SPECIAL" );

static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_dripping_mechanical_fluid( "dripping_mechanical_fluid" );
static const efftype_id effect_poison( "poison" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_tpollen( "tpollen" );
static const efftype_id effect_venom_dmg( "venom_dmg" );
static const efftype_id effect_venom_player1( "venom_player1" );

static const mfaction_str_id monfaction_nether_player_hate( "nether_player_hate" );

static const mtype_id mon_fungaloid( "mon_fungaloid" );
static const mtype_id mon_skitterbot( "mon_skitterbot" );
static const mtype_id mon_test_eoc_attacker( "mon_test_eoc_attacker" );
static const mtype_id mon_test_m19b_bee( "mon_test_m19b_bee" );
static const mtype_id mon_test_m19b_calm( "mon_test_m19b_calm" );
static const mtype_id mon_test_m19b_grown( "mon_test_m19b_grown" );
static const mtype_id mon_test_m19b_grows( "mon_test_m19b_grows" );
static const mtype_id mon_test_m19b_immune( "mon_test_m19b_immune" );
static const mtype_id mon_test_m19b_layer( "mon_test_m19b_layer" );
static const mtype_id mon_test_m19b_triggers( "mon_test_m19b_triggers" );
static const mtype_id mon_test_m19b_triggers_child( "mon_test_m19b_triggers_child" );
static const mtype_id mon_test_m19b_vulnerable( "mon_test_m19b_vulnerable" );
static const mtype_id mon_triffid( "mon_triffid" );
static const mtype_id mon_zombie( "mon_zombie" );

static const species_id species_PLANT( "PLANT" );
static const species_id species_ROBOT( "ROBOT" );
static const species_id species_ZOMBIE( "ZOMBIE" );

static const trait_id trait_BEE( "BEE" );
static const trait_id trait_FLOWERS( "FLOWERS" );
static const trait_id trait_MYCUS_FRIEND( "MYCUS_FRIEND" );
static const trait_id trait_PHEROMONE_INSECT( "PHEROMONE_INSECT" );
static const trait_id trait_TERRIFYING( "TERRIFYING" );
static const trait_id trait_TEST_M19B_ANGERING( "TEST_M19B_ANGERING" );
static const trait_id trait_TEST_M19B_CALMING( "TEST_M19B_CALMING" );
static const trait_id trait_TEST_M19B_IGNORED_BY_FLAG( "TEST_M19B_IGNORED_BY_FLAG" );
static const trait_id trait_TEST_M19B_SEQUENCE( "TEST_M19B_SEQUENCE" );

namespace
{

// Records the events sent, with their alpha monster and whether beta was the avatar
struct m19b_event_catcher : event_subscriber {
    struct caught {
        cata::event e;
        monster *alpha = nullptr;
        bool beta_is_avatar = false;
    };
    std::vector<caught> events;

    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        events.push_back( { e, nullptr, false } );
    }
    void notify( const cata::event &e, std::unique_ptr<talker> alpha,
                 std::unique_ptr<talker> beta ) override {
        events.push_back( { e, alpha ? alpha->get_monster() : nullptr,
                            beta &&beta->get_character() == &get_avatar()
                          } );
    }
    const caught *find( event_type type ) const {
        for( const caught &c : events ) {
            if( c.e.type() == type ) {
                return &c;
            }
        }
        return nullptr;
    }
};

double m19b_global( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? 0.0 : value->dbl();
}

void set_m19b_global( const std::string &name, double value )
{
    get_globals().set_global_value( name, diag_value( value ) );
}

// Whether loading a monster with these extra members fails
bool monster_load_fails( const std::string &members )
{
    mtype loaded;
    JsonObject jo = json_loader::from_string( R"({ "name": { "str": "m19b load test" }, )"
                    R"("default_faction": "zombie", "symbol": "Z", )" + members + " }" );
    jo.allow_omitted_members();
    try {
        loaded.load( jo, "audit_m_m19b_test" );
    } catch( const JsonError & ) {
        return true;
    }
    return false;
}

} // namespace

// MONAI-J05: no EOC could react to a monster evolving, giving birth or using a special attack.
TEST_CASE( "monai-j05_monster_upgrade_birth_and_special_events", "[audit][monster][event]" )
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    get_globals().clear_global_values();
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::turn_zero + 1_hours;
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_map_without_vision();
        get_globals().clear_global_values();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_pos( 65, 65, 0 );
    m19b_event_catcher caught;
    get_event_bus().subscribe( &caught );

    SECTION( "growing up sends monster_upgrades, with the grown monster as alpha" ) {
        monster &mon = spawn_test_monster( "mon_test_m19b_grows", mon_pos );
        REQUIRE( mon.can_upgrade() );
        mon.allow_upgrade();
        mon.try_upgrade( false );
        REQUIRE( mon.type->id == mon_test_m19b_grown );
        const m19b_event_catcher::caught *grew = caught.find( event_type::monster_upgrades );
        REQUIRE( grew != nullptr );
        CHECK( grew->e.get<mtype_id>( "old_type" ) == mon_test_m19b_grows );
        CHECK( grew->e.get<mtype_id>( "new_type" ) == mon_test_m19b_grown );
        CHECK( grew->alpha == &mon );
    }

    SECTION( "laying eggs sends monster_reproduces, with the parent as alpha" ) {
        monster &mon = spawn_test_monster( "mon_test_m19b_layer", mon_pos );
        // Only half the tries are by a female: try until one lays, a bounded number of times
        rng_set_engine_seed( 4242 );
        const m19b_event_catcher::caught *laid = nullptr;
        for( int i = 0; i < 64 && laid == nullptr; ++i ) {
            mon.set_baby_timer( calendar::turn - 1_turns );
            mon.try_reproduce();
            laid = caught.find( event_type::monster_reproduces );
        }
        REQUIRE( laid != nullptr );
        CHECK( laid->e.get<mtype_id>( "monster_type" ) == mon_test_m19b_layer );
        CHECK( laid->e.get<int>( "count" ) == 1 );
        CHECK( laid->alpha == &mon );
    }

    SECTION( "a special attack that worked sends monster_uses_special, with its target as beta" ) {
        u.setpos( here, mon_pos + tripoint_rel_ms::east );
        monster &mon = spawn_test_monster( "mon_test_eoc_attacker", mon_pos );
        mon.set_moves( 100 );
        dialogue d( get_talker_for( mon ), get_talker_for( u ) );
        effect_on_condition_EOC_TEST_MON_USE_SPECIAL->activate( d );
        REQUIRE( m19b_global( "test_mon_special_hit_avatar" ) == 1 );
        const m19b_event_catcher::caught *used = caught.find( event_type::monster_uses_special );
        REQUIRE( used != nullptr );
        CHECK( used->e.get<mtype_id>( "monster_type" ) == mon_test_eoc_attacker );
        CHECK( used->e.get<std::string>( "attack" ) == "test_eoc_mark_target" );
        CHECK( used->alpha == &mon );
        CHECK( used->beta_is_avatar );
    }
}

// MONAI-J06: the bee, mycus, pheromone, flower and terrifying rules were C++; mutations now give
// them, and any mod can, with "monster_relations".
TEST_CASE( "monai-j06_mutation_monster_relations", "[audit][monster][mutation]" )
{
    clear_avatar();
    avatar &u = get_avatar();

    SECTION( "the vanilla rules, now in the mutations, give the old attitudes" ) {
        monster bee( mon_test_m19b_bee );
        bee.anger = 25;
        bee.morale = 50;
        REQUIRE( bee.attitude( &u ) == MATT_ATTACK );

        u.toggle_trait( trait_PHEROMONE_INSECT );
        REQUIRE( u.has_trait( trait_PHEROMONE_INSECT ) );
        // 25 - 20: below 10, it only follows
        CHECK( bee.attitude( &u ) == MATT_FOLLOW );

        u.toggle_trait( trait_FLOWERS );
        // 25 - 10 for the flowers, then the pheromones, as the anger is still 10 or more
        CHECK( bee.attitude( &u ) == MATT_IGNORE );
        bee.anger = 15;
        // 15 - 10 is below 10: the pheromones don't calm it further (flowers come first)
        CHECK( bee.attitude( &u ) == MATT_FOLLOW );

        u.toggle_trait( trait_BEE );
        CHECK( bee.attitude( &u ) == MATT_FRIEND );
    }

    SECTION( "terrifying lowers every monster's morale by 10" ) {
        monster bee( mon_test_m19b_bee );
        bee.anger = 100;
        bee.morale = 5;
        REQUIRE( bee.attitude( &u ) == MATT_ATTACK );
        u.toggle_trait( trait_TERRIFYING );
        // morale -5 with anger 100 and full HP: it follows instead
        CHECK( bee.attitude( &u ) == MATT_FOLLOW );
    }

    SECTION( "mycus friends are friends of fungi" ) {
        monster fungaloid( mon_fungaloid );
        fungaloid.aggro_character = true;
        fungaloid.anger = 100;
        fungaloid.morale = 100;
        REQUIRE( fungaloid.attitude( &u ) != MATT_FRIEND );
        u.toggle_trait( trait_MYCUS_FRIEND );
        CHECK( fungaloid.attitude( &u ) == MATT_FRIEND );
    }

    SECTION( "a relation by monster flag can make the monster ignore the character" ) {
        monster calm( mon_test_m19b_calm );
        calm.aggro_character = true;
        calm.anger = 100;
        calm.morale = 100;
        monster bee( mon_test_m19b_bee );
        bee.anger = 100;
        bee.morale = 100;
        u.toggle_trait( trait_TEST_M19B_IGNORED_BY_FLAG );
        CHECK( calm.attitude( &u ) == MATT_IGNORE );
        // without the flag
        CHECK( bee.attitude( &u ) == MATT_ATTACK );
    }

    SECTION( "relations apply in order, each seeing the anger left by the ones before" ) {
        monster calm( mon_test_m19b_calm );
        calm.aggro_character = true;
        calm.morale = 50;
        u.toggle_trait( trait_TEST_M19B_SEQUENCE );
        // 25 is at most 30: +40, and then 65 is at least 50: morale -200
        calm.anger = 25;
        CHECK( calm.attitude( &u ) == MATT_FLEE );
        // neither applies
        calm.anger = 35;
        CHECK( calm.attitude( &u ) == MATT_ATTACK );
    }

    SECTION( "only an angering anger relation lets a monster see a character it ignores" ) {
        monster calm( mon_test_m19b_calm );
        REQUIRE_FALSE( calm.aggro_character );
        calm.anger = 100;
        calm.morale = 100;
        u.toggle_trait( trait_TEST_M19B_CALMING );
        // a calming relation used to make it weigh the character anyway
        CHECK( calm.attitude( &u ) == MATT_IGNORE );
        u.toggle_trait( trait_TEST_M19B_CALMING );
        u.toggle_trait( trait_TEST_M19B_ANGERING );
        CHECK( calm.attitude( &u ) == MATT_ATTACK );
    }

    SECTION( "the portal storm faction says which option makes it ignore NPCs" ) {
        CHECK( monfaction_nether_player_hate.obj().ignores_npcs_if_option == "PORTAL_STORM_IGNORE_NPC" );
    }
}

// MONAI-J07: poison and venom immunities were a C++ table keyed on species, and an effect's own
// immune_flags were never checked for those effects.
TEST_CASE( "monai-j07_effect_immunities_from_json", "[audit][monster][effect]" )
{
    SECTION( "the vanilla immunities are in species.json" ) {
        monster zed( mon_zombie );
        REQUIRE( zed.in_species( species_ZOMBIE ) );
        CHECK( zed.type->effect_immunity_chances.at( effect_poison ) == 100 );
        CHECK( zed.is_immune_effect( effect_poison ) );
        CHECK_FALSE( zed.is_immune_effect( effect_venom_dmg ) );
        // venom_player1 names VENOM_IMMUNE, which zombies have from their species
        CHECK( zed.is_immune_effect( effect_venom_player1 ) );

        monster triffid( mon_triffid );
        REQUIRE( triffid.in_species( species_PLANT ) );
        CHECK( triffid.is_immune_effect( effect_tpollen ) );
        CHECK_FALSE( zed.is_immune_effect( effect_tpollen ) );

        monster robot( mon_skitterbot );
        REQUIRE( robot.in_species( species_ROBOT ) );
        CHECK( robot.type->bleed_effect == effect_dripping_mechanical_fluid );
        CHECK( zed.type->bleed_effect.is_null() );
    }

    SECTION( "of several species the highest chance counts, and the monster's own entries win" ) {
        const mtype &immune = mon_test_m19b_immune.obj();
        // 40 from one species, 100 from the other
        CHECK( immune.effect_immunity_chances.at( effect_poison ) == 100 );
        CHECK( immune.effect_immunity_chances.at( effect_stunned ) == 30 );
        CHECK( immune.bleed_effect == effect_dripping_mechanical_fluid );
        CHECK( monster( mon_test_m19b_immune ).is_immune_effect( effect_poison ) );

        const mtype &vulnerable = mon_test_m19b_vulnerable.obj();
        CHECK( vulnerable.effect_immunity_chances.at( effect_poison ) == 0 );
        CHECK( vulnerable.effect_immunity_chances.count( effect_stunned ) == 0 );
        monster mon( mon_test_m19b_vulnerable );
        CHECK_FALSE( mon.is_immune_effect( effect_poison ) );
        // a human body that would be downed
        CHECK( mon.is_immune_effect( effect_downed ) );
        CHECK_FALSE( monster( mon_test_m19b_immune ).is_immune_effect( effect_downed ) );
    }

    SECTION( "a monster bleeds its species' bleed effect" ) {
        clear_map_without_vision();
        clear_creatures();
        on_out_of_scope cleanup( []() {
            clear_creatures();
        } );
        monster &mon = spawn_test_monster( "mon_test_m19b_immune", tripoint_bub_ms( 65, 65, 0 ) );
        mon.make_bleed( effect_source::empty(), 10_turns );
        CHECK( mon.has_effect( effect_dripping_mechanical_fluid ) );
        CHECK_FALSE( mon.has_effect( effect_bleed ) );
    }
}

// MONAI-J08: trigger strengths, ranges and caps were fixed in C++, and no trigger could read a
// condition.
TEST_CASE( "monai-j08_trigger_numbers_and_conditions_from_json", "[audit][monster]" )
{
    SECTION( "a trigger's object form sets its numbers for that list only" ) {
        const mtype &trig = mon_test_m19b_triggers.obj();
        CHECK( trig.trigger_amount( mon_trigger_kind::anger, mon_trigger::HOSTILE_CLOSE, 5 ) == 8 );
        CHECK( trig.trigger_range( mon_trigger_kind::anger, mon_trigger::HOSTILE_CLOSE, 5 ) == 7 );
        CHECK( trig.trigger_cap( mon_trigger_kind::anger, mon_trigger::HOSTILE_CLOSE, 30 ) == 50 );
        CHECK( trig.trigger_amount( mon_trigger_kind::fear, mon_trigger::HOSTILE_CLOSE, 5 ) == 5 );
        CHECK( trig.trigger_amount( mon_trigger_kind::fear, mon_trigger::FIRE, 5 ) == 9 );
        CHECK( trig.trigger_range( mon_trigger_kind::fear, mon_trigger::FIRE, 3 ) == 2 );
        CHECK( trig.has_anger_trigger( mon_trigger::HURT ) );
        CHECK( trig.has_anger_trigger( mon_trigger::CONDITION ) );
        CHECK( trig.get_condition_triggers( mon_trigger_kind::anger ).size() == 1 );
        CHECK( trig.get_condition_triggers( mon_trigger_kind::fear ).size() == 1 );
        CHECK( trig.get_condition_triggers( mon_trigger_kind::placate ).size() == 1 );
    }

    SECTION( "extend replaces a trigger's numbers, delete removes the trigger and its numbers" ) {
        const mtype &child = mon_test_m19b_triggers_child.obj();
        CHECK( child.trigger_amount( mon_trigger_kind::anger, mon_trigger::HOSTILE_CLOSE, 5 ) == 3 );
        CHECK( child.trigger_range( mon_trigger_kind::anger, mon_trigger::HOSTILE_CLOSE, 5 ) == 5 );
        CHECK_FALSE( child.has_fear_trigger( mon_trigger::FIRE ) );
        CHECK( child.trigger_amount( mon_trigger_kind::fear, mon_trigger::FIRE, 5 ) == 5 );
        CHECK_FALSE( child.has_fear_trigger( mon_trigger::CONDITION ) );
        CHECK( child.get_condition_triggers( mon_trigger_kind::fear ).empty() );
        CHECK( child.get_condition_triggers( mon_trigger_kind::anger ).size() == 1 );
    }

    SECTION( "condition triggers change anger and morale while true, with the target as beta" ) {
        clear_map_without_vision();
        clear_creatures();
        clear_avatar();
        on_out_of_scope cleanup( []() {
            clear_creatures();
        } );
        monster &mon = spawn_test_monster( "mon_test_m19b_triggers", tripoint_bub_ms( 65, 65, 0 ) );
        mon.anger = 20;
        mon.morale = 20;
        REQUIRE_FALSE( mon.aggro_character );
        // no target: the anger condition (has_beta) is false; the placating one is always true
        // and the fearful one never
        mon.process_condition_triggers( nullptr );
        CHECK( mon.anger == 19 );
        CHECK( mon.morale == 20 );
        CHECK_FALSE( mon.aggro_character );
        // with a target: + (2 + 3), - 1
        mon.process_condition_triggers( &get_avatar() );
        CHECK( mon.anger == 23 );
        CHECK( mon.morale == 20 );
        CHECK( mon.aggro_character );
    }

    SECTION( "settings a trigger doesn't read, and CONDITION without its object, are load errors" ) {
        CHECK_FALSE( monster_load_fails(
                         R"("anger_triggers": [ { "id": "PLAYER_CLOSE", "amount": 5 } ])" ) );
        CHECK( monster_load_fails( R"("anger_triggers": [ { "id": "HURT", "amount": 5 } ])" ) );
        CHECK( monster_load_fails( R"("fear_triggers": [ { "id": "MATING_SEASON", "amount": 5 } ])" ) );
        CHECK( monster_load_fails( R"("placate_triggers": [ { "id": "PLAYER_CLOSE", "cap": 5 } ])" ) );
        CHECK( monster_load_fails( R"("anger_triggers": [ "CONDITION" ])" ) );
        CHECK( monster_load_fails( R"("anger_triggers": [ { "id": "CONDITION", "amount": 5 } ])" ) );
        CHECK( monster_load_fails(
                   R"("fear_triggers": [ { "id": "CONDITION", "condition": "has_beta", "amount": 5, "aggro_character": true } ])" ) );
    }
}

// MONAI-J09: a monster's goals came only from nodes named like its special attacks, a goal could
// only name a special attack, and its conditions had no target.
TEST_CASE( "monai-j09_monster_behavior_field_and_goals", "[audit][monster][behavior]" )
{
    clear_map_without_vision();
    clear_creatures();
    clear_avatar();
    get_globals().clear_global_values();
    on_out_of_scope cleanup( []() {
        clear_creatures();
        get_globals().clear_global_values();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms mon_pos( 65, 65, 0 );
    // far from the monster, so that it has no target
    u.setpos( here, mon_pos + tripoint_rel_ms( 20, 0, 0 ) );
    monster &mon = spawn_test_monster( "mon_test_m19b_goals", mon_pos );
    mon.unset_dest();
    REQUIRE( mon.attack_target() == nullptr );
    const auto goal = [&mon]() {
        behavior::monster_oracle_t oracle( &mon );
        behavior::tree goals;
        goals.add( mon.type->get_goals() );
        return goals.tick( &oracle );
    };

    SECTION( "the behavior nodes come first, in their order" ) {
        CHECK( goal() == "idle" );
        set_m19b_global( "test_m19b_goal_allowed", 1 );
        CHECK( goal() == "eoc:EOC_TEST_M19B_GOAL" );
        set_m19b_global( "test_m19b_goal_special", 1 );
        CHECK( goal() == "special:test_eoc_mark_target" );
    }

    SECTION( "an eoc goal runs the EOC as the monster acts" ) {
        set_m19b_global( "test_m19b_goal_allowed", 1 );
        mon.set_moves( 100 );
        mon.move();
        CHECK( m19b_global( "test_m19b_goal_ran" ) == 1 );
    }

    SECTION( "conditions and the EOC see the monster's attack target as npc" ) {
        u.setpos( here, mon_pos + tripoint_rel_ms::east );
        mon.set_dest( u.pos_abs() );
        REQUIRE( mon.attack_target() == &u );
        CHECK( goal() == "eoc:EOC_TEST_M19B_TARGET" );
        mon.set_moves( 100 );
        mon.move();
        CHECK( m19b_global( "test_m19b_target_ran" ) == 1 );
    }

    SECTION( "a special goal uses that special attack, whatever its cooldown, which restarts" ) {
        u.setpos( here, mon_pos + tripoint_rel_ms::east );
        mon.set_dest( u.pos_abs() );
        REQUIRE( mon.attack_target() == &u );
        set_m19b_global( "test_m19b_goal_special", 1 );
        mon.set_special( "test_eoc_mark_target", 5 );
        REQUIRE( goal() == "special:test_eoc_mark_target" );
        mon.set_moves( 100 );
        mon.move();
        // test_eoc_mark_target's EOC saw the avatar as its target
        CHECK( m19b_global( "test_mon_special_hit_avatar" ) == 1 );
        CHECK( mon.special_cooldown( "test_eoc_mark_target" ) == std::optional<int>( 10 ) );
    }
}

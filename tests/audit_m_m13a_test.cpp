#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_variant.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "requirements.h"
#include "ret_val.h"
#include "type_id.h"
#include "units.h"

static const efftype_id effect_test_m13a_ailment( "test_m13a_ailment" );
static const efftype_id effect_test_m13a_drugged( "test_m13a_drugged" );

static const field_type_str_id field_fd_weedsmoke( "fd_weedsmoke" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_detergent( "detergent" );
static const itype_id itype_liquid_soap( "liquid_soap" );
static const itype_id itype_soap( "soap" );
static const itype_id itype_test_m13a_quick_pill( "test_m13a_quick_pill" );
static const itype_id itype_test_m13a_soapstone( "test_m13a_soapstone" );
static const itype_id itype_test_m13a_washboard( "test_m13a_washboard" );
static const itype_id itype_test_m13a_widget( "test_m13a_widget" );
static const itype_id itype_tshirt( "tshirt" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_clean( "water_clean" );

static const requirement_id requirement_data_test_m13a_soapstone_cleanser(
    "test_m13a_soapstone_cleanser" );
static const requirement_id requirement_data_wash_cleanser( "wash_cleanser" );
static const requirement_id requirement_data_wash_water( "wash_water" );

static const trait_id trait_LIGHTWEIGHT( "LIGHTWEIGHT" );
static const trait_id trait_TEST_M13A_DRUG_SENSITIVE( "TEST_M13A_DRUG_SENSITIVE" );
static const trait_id trait_TOLERANCE( "TOLERANCE" );

static double num_var( const Creature &c, const std::string &key )
{
    const diag_value *val = c.maybe_get_value( key );
    return val == nullptr ? 0.0 : val->dbl();
}

// The avatar alone on a cleared map, at a fixed time.
static void clear_scene()
{
    calendar::turn = calendar::turn_zero + 9_hours;
    clear_vehicles();
    clear_map();
    clear_creatures();
    clear_npcs();
    clear_avatar();
    Messages::clear_messages();
}

// A consume_drug use action loaded from json
static consume_drug_iuse load_drug( const std::string &json )
{
    consume_drug_iuse drug;
    const JsonObject jo = json_loader::from_string( json );
    jo.allow_omitted_members();
    drug.load( jo, "audit_m_m13a_test" );
    return drug;
}

// USE-J05: how long a drug's effects last was scaled for two vanilla traits by name, so no mod
// trait, CBM or item could change it. It is the DRUG_DURATION enchantment value now, and the two
// traits give it in their JSON, with the same numbers.
TEST_CASE( "use-j05_drug_durations_follow_the_drug_duration_enchantment", "[audit][iuse_actor]" )
{
    clear_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    const consume_drug_iuse drug = load_drug(
                                       R"({ "activation_message": "You take the test drug.", )"
                                       R"("effects": [ { "id": "test_m13a_drugged", "duration": "10 m" } ] })" );
    item pill( itype_test_m13a_quick_pill );
    const auto dose = [&]() {
        you.remove_effect( effect_test_m13a_drugged );
        REQUIRE( drug.use( &you, pill, &here, you.pos_bub() ) == 1 );
        return you.get_effect_dur( effect_test_m13a_drugged );
    };

    SECTION( "no trait" ) {
        CHECK( dose() == 10_minutes );
    }
    SECTION( "Substance Tolerance, as before: x0.8" ) {
        you.set_mutation( trait_TOLERANCE );
        REQUIRE( you.has_trait( trait_TOLERANCE ) );
        CHECK( dose() == 8_minutes );
    }
    SECTION( "Lightweight, as before: x1.2" ) {
        you.set_mutation( trait_LIGHTWEIGHT );
        REQUIRE( you.has_trait( trait_LIGHTWEIGHT ) );
        CHECK( dose() == 12_minutes );
    }
    SECTION( "a mod's trait" ) {
        you.set_mutation( trait_TEST_M13A_DRUG_SENSITIVE );
        REQUIRE( you.has_trait( trait_TEST_M13A_DRUG_SENSITIVE ) );
        CHECK( dose() == 20_minutes );
    }
    clear_scene();
}

// USE-J05: stat_adjustments took any string and failed only when the drug was taken, and str,
// dex, per, int and the combat bonuses are reset every turn, so adjusting them did nothing.
TEST_CASE( "use-j05_drug_stat_adjustments_must_be_stats_that_last", "[audit][iuse_actor]" )
{
    CHECK_NOTHROW( load_drug(
                       R"({ "stat_adjustments": { "hunger": -10, "pain": 2, "moves": -50 } })" ) );
    CHECK_THROWS_AS( load_drug( R"({ "stat_adjustments": { "str": 2 } })" ), JsonError );
    CHECK_THROWS_AS( load_drug( R"({ "stat_adjustments": { "speed": 10 } })" ), JsonError );
    CHECK_THROWS_AS( load_drug( R"({ "stat_adjustments": { "strength": 2 } })" ), JsonError );
    CHECK_THROWS_AS( load_drug( R"({ "field_count": -1 })" ), JsonError );
    CHECK_THROWS_AS( load_drug( R"({ "consume_time": "-1 s" })" ), JsonError );
}

// USE-J05: drugs can remove effects, place their fields as they say, and take their own time.
TEST_CASE( "use-j05_drugs_remove_effects_place_fields_and_take_their_time",
           "[audit][iuse_actor]" )
{
    clear_scene();
    avatar &you = get_avatar();
    map &here = get_map();
    item pill( itype_test_m13a_quick_pill );

    SECTION( "remove_effects" ) {
        const consume_drug_iuse drug = load_drug(
                                           R"({ "activation_message": "You take the test drug.", )"
                                           R"("remove_effects": [ "test_m13a_ailment" ] })" );
        you.add_effect( effect_test_m13a_ailment, 1_hours );
        REQUIRE( you.has_effect( effect_test_m13a_ailment ) );
        REQUIRE( drug.use( &you, pill, &here, you.pos_bub() ) == 1 );
        CHECK_FALSE( you.has_effect( effect_test_m13a_ailment ) );
    }
    SECTION( "one field on the user's own tile" ) {
        const consume_drug_iuse drug = load_drug(
                                           R"({ "activation_message": "You take the test drug.", )"
                                           R"("fields_produced": { "fd_weedsmoke": 2 }, "field_count": 1, "field_radius": 0 })" );
        REQUIRE( here.get_field( you.pos_bub(), field_fd_weedsmoke ) == nullptr );
        REQUIRE( drug.use( &you, pill, &here, you.pos_bub() ) == 1 );
        CHECK( here.get_field( you.pos_bub(), field_fd_weedsmoke ) != nullptr );
    }
    SECTION( "consume_time" ) {
        CHECK( you.get_consume_time( pill ) == 2_minutes );
    }
    clear_scene();
}

// USE-J06: washing named its water and cleanser items in C++. They are the wash_water and
// wash_cleanser requirements now, with the same items and the same amounts.
TEST_CASE( "use-j06_washing_keeps_its_amounts_and_items", "[audit][iuse]" )
{
    const washing_requirements litre = washing_requirements_for_volume( 1_liter );
    CHECK( litre.water == 8 );
    CHECK( litre.cleanser == 1 );
    CHECK( litre.time == to_moves<int>( 40_seconds ) );
    CHECK( litre.water_requirement == requirement_data_wash_water );
    CHECK( litre.cleanser_requirement == requirement_data_wash_cleanser );
    const washing_requirements small = washing_requirements_for_volume( 300_ml );
    CHECK( small.water == 3 );
    CHECK( small.cleanser == 1 );
    CHECK( small.time == to_moves<int>( 10_seconds ) );

    const auto alternatives = []( const requirement_id & id ) {
        std::set<itype_id> found;
        for( const std::vector<item_comp> &group : id->get_components() ) {
            for( const item_comp &comp : group ) {
                CHECK( comp.count == 1 );
                found.insert( comp.type );
            }
        }
        return found;
    };
    CHECK( requirement_data_wash_water->get_components().size() == 1 );
    CHECK( alternatives( requirement_data_wash_water ) ==
           std::set<itype_id> { itype_water, itype_water_clean } );
    CHECK( requirement_data_wash_cleanser->get_components().size() == 1 );
    CHECK( alternatives( requirement_data_wash_cleanser ) ==
           std::set<itype_id> { itype_soap, itype_detergent, itype_liquid_soap } );
}

// USE-J06: the washing activity checks and uses up what its requirements say.
TEST_CASE( "use-j06_washing_uses_up_the_water_and_cleanser", "[audit][iuse][activity]" )
{
    clear_scene();
    avatar &you = get_avatar();
    REQUIRE( you.wear_item( item( itype_backpack ), false ).has_value() );
    item bottle( itype_bottle_plastic );
    REQUIRE( bottle.put_in( item( itype_water_clean, calendar::turn, 2 ),
                            pocket_type::CONTAINER ).success() );
    REQUIRE( you.i_add( bottle ) );
    REQUIRE( you.i_add( item( itype_soap, calendar::turn, 2 ) ) );
    item shirt( itype_tshirt );
    shirt.set_flag( flag_FILTHY );
    item_location filthy = you.i_add( shirt );
    REQUIRE( filthy );
    REQUIRE( you.charges_of( itype_water_clean ) == 2 );
    REQUIRE( you.charges_of( itype_soap ) == 2 );
    you.invalidate_crafting_inventory();

    washing_requirements needed = washing_requirements_for_volume( 250_ml );
    REQUIRE( needed.water == 2 );
    REQUIRE( needed.cleanser == 1 );
    CHECK( has_washing_supplies( you, needed ) );

    drop_locations to_wash;
    to_wash.emplace_back( filthy, 1 );
    you.assign_activity( wash_activity_actor( to_wash, needed ) );
    CHECK( process_activity_bounded( you, 100, 10000 ) );
    REQUIRE( filthy );
    CHECK_FALSE( filthy->has_flag( flag_FILTHY ) );
    CHECK( you.charges_of( itype_water_clean ) == 0 );
    CHECK( you.charges_of( itype_soap ) == 1 );
    you.invalidate_crafting_inventory();
    CHECK_FALSE( has_washing_supplies( you, needed ) );
    clear_scene();
}

// USE-J06: a wash activity saved before it named its requirements washes with the WASH_* use
// actions' water and cleanser.
TEST_CASE( "use-j06_old_saved_washing_uses_the_default_water_and_cleanser",
           "[audit][iuse][activity]" )
{
    JsonValue saved = json_loader::from_string(
                          R"({ "to_wash": [], "time": 1000, "water": 2, "cleanser": 1 })" );
    const std::unique_ptr<activity_actor> actor = wash_activity_actor::deserialize( saved );
    REQUIRE( actor );
    std::ostringstream os;
    JsonOut jsout( os );
    actor->serialize( jsout );
    CAPTURE( os.str() );
    const JsonObject resaved = json_loader::from_string( os.str() );
    resaved.allow_omitted_members();
    CHECK( resaved.get_string( "water_requirement" ) == "wash_water" );
    CHECK( resaved.get_string( "cleanser_requirement" ) == "wash_cleanser" );
}

// USE-J06: the wash use action sets what it washes, its own water and cleanser (or none) and how
// much of each.
TEST_CASE( "use-j06_a_wash_action_sets_its_own_water_and_cleanser", "[audit][iuse_actor]" )
{
    clear_scene();
    const use_function *use = itype_test_m13a_washboard->get_use( "wash" );
    REQUIRE( use != nullptr );
    const wash_actor *wash = dynamic_cast<const wash_actor *>( use->get_actor_ptr() );
    REQUIRE( wash != nullptr );
    CHECK( wash->rules.soft );
    CHECK_FALSE( wash->rules.hard );
    CHECK( wash->rules.cleanser == requirement_data_test_m13a_soapstone_cleanser );
    const washing_requirements needed = wash->rules.requirements_for( 1_liter );
    CHECK( needed.water == 0 );
    CHECK( needed.cleanser == 2 );
    CHECK( needed.time == to_moves<int>( 40_seconds ) );

    avatar &you = get_avatar();
    REQUIRE( you.wear_item( item( itype_backpack ), false ).has_value() );
    REQUIRE( you.i_add( item( itype_test_m13a_soapstone ) ) );
    you.invalidate_crafting_inventory();
    // no water anywhere: the washboard needs none, but one soapstone is short
    CHECK_FALSE( has_washing_supplies( you, needed ) );
    REQUIRE( you.i_add( item( itype_test_m13a_soapstone ) ) );
    you.invalidate_crafting_inventory();
    CHECK( has_washing_supplies( you, needed ) );
    consume_washing_supplies( you, needed );
    CHECK( you.amount_of( itype_test_m13a_soapstone ) == 0 );
    clear_scene();
}

namespace
{
// Keeps the character_activates_item events sent while it is subscribed
struct activation_catcher : event_subscriber {
    std::vector<cata::event> activations;
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::character_activates_item ) {
            activations.push_back( e );
        }
    }
};
} // namespace

// USE-J07: activating an item sent no event, so EVENT EOCs and achievements couldn't react to
// item use. Character::invoke_item sends character_activates_item for a use that goes through.
TEST_CASE( "use-j07_activating_an_item_sends_character_activates_item", "[audit][iuse][event]" )
{
    clear_scene();
    avatar &you = get_avatar();
    item_location widget = you.i_add( item( itype_test_m13a_widget ) );
    REQUIRE( widget );
    activation_catcher catcher;

    SECTION( "a use that goes through" ) {
        get_event_bus().subscribe( &catcher );
        CHECK_FALSE( you.invoke_item( widget.get_item(), "test_m13a_press" ) );
        get_event_bus().unsubscribe( &catcher );
        CHECK( num_var( you, "test_m13a_uses" ) == 1 );
        REQUIRE( catcher.activations.size() == 1 );
        const cata::event &e = catcher.activations.front();
        CHECK( e.get<character_id>( "character" ) == you.getID() );
        CHECK( e.get<itype_id>( "itype" ) == itype_test_m13a_widget );
        CHECK( e.get<cata_variant_type::string>( "method" ) == "test_m13a_press" );
        CHECK( e.get<int>( "charges" ) == 0 );
        // the fixture's EVENT EOC reacted, with the event's context values
        CHECK( num_var( you, "test_m13a_activations" ) == 1 );
        CHECK( num_var( you, "test_m13a_activated_charges" ) == 0 );
    }
    SECTION( "a cancelled use" ) {
        get_event_bus().subscribe( &catcher );
        CHECK_FALSE( you.invoke_item( widget.get_item(), "test_m13a_refuse" ) );
        get_event_bus().unsubscribe( &catcher );
        CHECK( catcher.activations.empty() );
        CHECK( num_var( you, "test_m13a_activations" ) == 0 );
    }
    clear_scene();
}

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "character_id.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "fault.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_group.h"
#include "item_location.h"
#include "itype.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "math_parser_diag_value.h"
#include "mtype.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "relic.h"
#include "ret_val.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "weighted_list.h"

static const activity_id ACT_MEND_ITEM( "ACT_MEND_ITEM" );

static const ammotype ammo_9mm( "9mm" );

static const effect_on_condition_id
effect_on_condition_EOC_audit_m10a_eat_a( "EOC_audit_m10a_eat_a" );
static const effect_on_condition_id
effect_on_condition_EOC_audit_m10a_eat_b( "EOC_audit_m10a_eat_b" );

static const fault_fix_id fault_fix_audit_m10a_oil_squeak( "audit_m10a_oil_squeak" );

static const fault_id fault_audit_m10a_rattle( "fault_audit_m10a_rattle" );
static const fault_id fault_audit_m10a_squeak( "fault_audit_m10a_squeak" );
static const fault_id fault_engine_starter( "fault_engine_starter" );

static const flag_id json_flag_AUDIT_M10A_SLOW_ROT( "AUDIT_M10A_SLOW_ROT" );

static const item_group_id Item_spawn_data_audit_m10a_conditional( "audit_m10a_conditional" );
static const item_group_id
Item_spawn_data_audit_m10a_conditional_pick( "audit_m10a_conditional_pick" );
static const item_group_id Item_spawn_data_audit_m10a_trimmed( "audit_m10a_trimmed" );

static const itype_id itype_1cyl_combustion( "1cyl_combustion" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_audit_m10a_brine_active( "audit_m10a_brine_active" );
static const itype_id itype_audit_m10a_crock( "audit_m10a_crock" );
static const itype_id itype_audit_m10a_cured( "audit_m10a_cured" );
static const itype_id itype_audit_m10a_degrading_child( "audit_m10a_degrading_child" );
static const itype_id itype_audit_m10a_degrading_grandchild( "audit_m10a_degrading_grandchild" );
static const itype_id itype_audit_m10a_degrading_rock( "audit_m10a_degrading_rock" );
static const itype_id itype_audit_m10a_fault_cleared( "audit_m10a_fault_cleared" );
static const itype_id itype_audit_m10a_fault_extended( "audit_m10a_fault_extended" );
static const itype_id itype_audit_m10a_fault_parent( "audit_m10a_fault_parent" );
static const itype_id itype_audit_m10a_fault_replaced( "audit_m10a_fault_replaced" );
static const itype_id itype_audit_m10a_fault_trimmed( "audit_m10a_fault_trimmed" );
static const itype_id itype_audit_m10a_fault_ungrouped( "audit_m10a_fault_ungrouped" );
static const itype_id itype_audit_m10a_relic( "audit_m10a_relic" );
static const itype_id itype_audit_m10a_snack( "audit_m10a_snack" );
static const itype_id itype_audit_m10a_snack_more( "audit_m10a_snack_more" );
static const itype_id itype_audit_m10a_snack_other( "audit_m10a_snack_other" );
static const itype_id itype_audit_m10a_snack_plain( "audit_m10a_snack_plain" );
static const itype_id itype_coilgun( "coilgun" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_hk_g80( "hk_g80" );
static const itype_id itype_mp3_reverberation( "mp3_reverberation" );
static const itype_id itype_rock( "rock" );

static const mtype_id mon_audit_m10a_quick_riser( "mon_audit_m10a_quick_riser" );
static const mtype_id mon_zombie( "mon_zombie" );

namespace
{
talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_m_m10a_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

conditional_t::func load_condition( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition;
}

void run_effect( dialogue &d, const std::string &json )
{
    load_effect( json ).apply( d );
}

bool math_true( const_dialogue const &d, const std::string &expression )
{
    return load_condition( R"({ "condition": { "math": [ ")" + expression + R"(" ] } })" )( d );
}

void run_math( dialogue &d, const std::string &expression )
{
    run_effect( d, R"({ "effect": { "math": [ ")" + expression + R"(" ] } })" );
}

std::set<fault_id> faults_of( const itype_id &id )
{
    std::set<fault_id> ret;
    for( const std::pair<fault_id, int> &f : id->faults ) {
        ret.insert( f.first );
    }
    return ret;
}

int count_of( const std::vector<item> &items, const itype_id &id )
{
    return static_cast<int>( std::count_if( items.begin(), items.end(), [&id]( const item & it ) {
        return it.typeId() == id;
    } ) );
}

// a crock with a 5 charge stack of brine that is about to turn solid
item crock_of_brine()
{
    item crock( itype_audit_m10a_crock );
    REQUIRE( crock.put_in( item( itype_audit_m10a_brine_active, calendar::turn, 5 ),
                           pocket_type::CONTAINER ).success() );
    return crock;
}

item &only_content( item &container )
{
    std::list<item *> contents = container.all_items_top( pocket_type::CONTAINER );
    REQUIRE( contents.size() == 1 );
    return *contents.front();
}

void check_five_cured( item &container )
{
    const std::list<item *> contents = container.all_items_top( pocket_type::CONTAINER );
    CHECK( contents.size() == 5 );
    for( const item *it : contents ) {
        CHECK( it->typeId() == itype_audit_m10a_cured );
        CHECK( it->count() == 1 );
    }
}

struct artifact_events : event_subscriber {
    using event_subscriber::notify;
    std::vector<itype_id> itypes;
    std::vector<character_id> characters;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::activates_artifact ) {
            itypes.push_back( e.get<cata_variant_type::itype_id>( "itype" ) );
            characters.push_back( e.get<cata_variant_type::character_id>( "character" ) );
        }
    }
};

// removes the global variables these tests set, now and when the test ends
on_out_of_scope forget_globals( const std::vector<std::string> &names )
{
    const auto forget = [names]() {
        for( const std::string &name : names ) {
            get_globals().remove_global_value( name );
        }
    };
    forget();
    return on_out_of_scope( forget );
}
} // namespace

// ITEM-B10: convert() compared count_by_charges() after it had already switched the type, so the
// charges of an item that changes between counted by charges and not were never fixed up.
TEST_CASE( "item-b10_convert_fixes_charges_when_counting_changes", "[audit][item]" )
{
    item brine( itype_audit_m10a_brine_active, calendar::turn, 5 );
    REQUIRE( brine.count_by_charges() );
    brine.convert( itype_audit_m10a_cured );
    CHECK_FALSE( brine.count_by_charges() );
    CHECK( brine.charges == itype_audit_m10a_cured->charges_default() );

    // the other way a single item becomes a stack of the default size, not of 0
    item bit( itype_audit_m10a_cured );
    bit.convert( itype_audit_m10a_brine_active );
    REQUIRE( bit.count_by_charges() );
    CHECK( bit.charges == itype_audit_m10a_brine_active->charges_default() );
    CHECK( bit.charges >= 1 );
}

// ITEM-B10: a stack counted by charges whose countdown turned it into a type that isn't became a
// single item: 5 charges of brine became 1 cured bit. Now each charge becomes an item.
TEST_CASE( "item-b10_stack_splits_when_its_countdown_makes_it_solid_in_a_pocket",
           "[audit][item]" )
{
    clear_map();
    clear_avatar();
    item crock = crock_of_brine();
    item &brine = only_content( crock );
    REQUIRE( brine.count_by_charges() );
    REQUIRE( brine.charges == 5 );
    REQUIRE( brine.active );
    brine.countdown_point = calendar::turn;

    // processing an item processes what its pockets hold (the carried-item path)
    crock.process( get_map(), nullptr, get_avatar().pos_bub() );

    check_five_cured( crock );
}

TEST_CASE( "item-b10_stack_splits_when_its_countdown_makes_it_solid_on_the_map",
           "[audit][item]" )
{
    clear_map();
    clear_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 );
    item &placed = here.add_item( pos, crock_of_brine() );
    REQUIRE( placed.typeId() == itype_audit_m10a_crock );
    item &brine = only_content( placed );
    REQUIRE( brine.charges == 5 );
    REQUIRE( brine.active );
    brine.countdown_point = calendar::turn;

    // map items are processed one by one, each knowing the container it is in
    here.process_items();

    check_five_cured( placed );
}

// ITEM-J02: degradation_multiplier was ignored (forced to 0) for everything but vehicle parts,
// and under copy-from a child's multiplier was applied on top of its parent's.
TEST_CASE( "item-j02_degradation_multiplier_is_honoured_and_not_compounded",
           "[audit][item]" )
{
    // 50 increments at a multiplier of 1
    CHECK( itype_audit_m10a_degrading_rock->degrade_increments() == 25 );
    CHECK( itype_audit_m10a_degrading_child->degrade_increments() == 100 );
    CHECK( itype_audit_m10a_degrading_grandchild->degrade_increments() == 100 );
    // without the field, items that aren't vehicle parts still don't degrade
    CHECK( itype_rock->degrade_increments() == 0 );

    item degrading( itype_audit_m10a_degrading_rock );
    degrading.set_degradation( 1000 );
    CHECK( degrading.degradation() == 1000 );
}

// ITEM-J03: "faults" only ever appended to what copy-from inherited, so "faults": [] removed
// nothing, and extend and delete were not read.
TEST_CASE( "item-j03_faults_replace_extend_and_delete_under_copy_from", "[audit][item]" )
{
    const std::set<fault_id> both = { fault_audit_m10a_squeak, fault_audit_m10a_rattle };
    const std::set<fault_id> squeak = { fault_audit_m10a_squeak };
    const std::set<fault_id> rattle = { fault_audit_m10a_rattle };
    CHECK( faults_of( itype_audit_m10a_fault_parent ) == both );
    CHECK( faults_of( itype_audit_m10a_fault_cleared ).empty() );
    CHECK( faults_of( itype_audit_m10a_fault_replaced ) == rattle );
    CHECK( faults_of( itype_audit_m10a_fault_trimmed ) == rattle );
    CHECK( faults_of( itype_audit_m10a_fault_ungrouped ) == squeak );
    CHECK( faults_of( itype_audit_m10a_fault_extended ) == rattle );

    // vanilla: "faults": [] now drops the powder-gun and electronics faults these inherited
    CHECK( faults_of( itype_coilgun ).empty() );
    CHECK( faults_of( itype_hk_g80 ).empty() );
    CHECK( faults_of( itype_mp3_reverberation ).empty() );
    // while the engines that "extend" keep their parent's faults besides their own
    const std::set<fault_id> engine = faults_of( itype_1cyl_combustion );
    CHECK( engine.count( fault_engine_starter ) == 1 );
    CHECK( engine.size() > 1 );
}

TEST_CASE( "item-j03_consumption_eocs_replace_extend_and_delete", "[audit][item]" )
{
    using eoc_list = std::vector<effect_on_condition_id>;
    REQUIRE( itype_audit_m10a_snack->comestible );
    CHECK( itype_audit_m10a_snack->comestible->consumption_eocs ==
           eoc_list{ effect_on_condition_EOC_audit_m10a_eat_a } );
    CHECK( itype_audit_m10a_snack_plain->comestible->consumption_eocs.empty() );
    CHECK( itype_audit_m10a_snack_more->comestible->consumption_eocs ==
           eoc_list{ effect_on_condition_EOC_audit_m10a_eat_a, effect_on_condition_EOC_audit_m10a_eat_b } );
    CHECK( itype_audit_m10a_snack_other->comestible->consumption_eocs ==
           eoc_list{ effect_on_condition_EOC_audit_m10a_eat_b } );
}

// ITEM-J04: EOCs could not read or write an item's own charges or ammo.
TEST_CASE( "item-j04_math_reads_and_writes_item_charges", "[audit][item][eoc][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();

    // a stack counted by charges: its size, never below 1
    item ammo( itype_9mm, calendar::turn, 10 );
    u.remove_weapon();
    REQUIRE( u.wield( ammo ) );
    item_location held = u.get_wielded_item();
    REQUIRE( held );
    dialogue d( get_talker_for( u ), get_talker_for( held ) );
    CHECK( math_true( d, "n_item_charges() == 10" ) );
    run_math( d, "n_item_charges() = 3" );
    CHECK( held->charges == 3 );
    run_math( d, "n_item_charges() = 0" );
    CHECK( held->charges == 1 );

    // a magazine: the ammo loaded in it, up to its capacity
    item mag( itype_glockmag );
    mag.ammo_set( itype_9mm, 5 );
    u.remove_weapon();
    REQUIRE( u.wield( mag ) );
    item_location held_mag = u.get_wielded_item();
    REQUIRE( held_mag );
    REQUIRE( held_mag->ammo_remaining() == 5 );
    dialogue md( get_talker_for( u ), get_talker_for( held_mag ) );
    CHECK( math_true( md, "n_item_charges() == 5" ) );
    run_math( md, "n_item_charges() = 7" );
    CHECK( held_mag->ammo_remaining() == 7 );
    run_math( md, "n_item_charges() = 100000" );
    CHECK( held_mag->ammo_remaining() == held_mag->ammo_capacity( ammo_9mm ) );
    run_math( md, "n_item_charges() = 0" );
    CHECK( held_mag->ammo_remaining() == 0 );
}

// ITEM-J04: nor its temperature, freshness, wetness, relic charges or countdown.
TEST_CASE( "item-j04_math_reads_and_writes_item_state", "[audit][item][eoc][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();

    item food( itype_audit_m10a_cured );
    u.remove_weapon();
    REQUIRE( u.wield( food ) );
    item_location held = u.get_wielded_item();
    REQUIRE( held );
    REQUIRE( held->has_temperature() );
    REQUIRE( held->goes_bad() );
    dialogue d( get_talker_for( u ), get_talker_for( held ) );
    run_math( d, "n_item_temperature() = 300" );
    CHECK( units::to_kelvin( held->temperature ) == Approx( 300 ) );
    // math has no &&: two comparisons
    CHECK( math_true( d, "n_item_temperature() > 299" ) );
    CHECK( math_true( d, "n_item_temperature() < 301" ) );
    run_math( d, "n_item_freshness() = 0.5" );
    CHECK( held->get_relative_rot() == Approx( 0.5 ) );
    CHECK( math_true( d, "n_item_freshness() > 0.49" ) );
    CHECK( math_true( d, "n_item_freshness() < 0.51" ) );
    run_math( d, "n_item_wetness() = 40" );
    CHECK( held->wetness == 40 );
    CHECK( math_true( d, "n_item_wetness() == 40" ) );

    // an item without a tracked temperature reads 0; one that isn't a relic has no charges
    item widget( itype_audit_m10a_fault_parent );
    u.remove_weapon();
    REQUIRE( u.wield( widget ) );
    item_location held_widget = u.get_wielded_item();
    REQUIRE( held_widget );
    dialogue wd( get_talker_for( u ), get_talker_for( held_widget ) );
    CHECK( math_true( wd, "n_item_temperature() == 0" ) );
    CHECK( math_true( wd, "n_item_relic_charges() == 0" ) );

    // a countdown: none is -1; setting one makes the item active
    CHECK( math_true( wd, "n_item_countdown() == -1" ) );
    run_math( wd, "n_item_countdown() = 600" );
    CHECK( held_widget->countdown_point == calendar::turn + 600_seconds );
    CHECK( held_widget->active );
    CHECK( math_true( wd, "n_item_countdown() == 600" ) );
    run_math( wd, "n_item_countdown() = -1" );
    CHECK( held_widget->countdown_point == calendar::turn_max );

    // relic charges, no more than the maximum
    item relic( itype_audit_m10a_relic );
    u.remove_weapon();
    REQUIRE( u.wield( relic ) );
    item_location held_relic = u.get_wielded_item();
    REQUIRE( held_relic );
    REQUIRE( held_relic->is_relic() );
    dialogue rd( get_talker_for( u ), get_talker_for( held_relic ) );
    CHECK( math_true( rd, "n_item_relic_charges() == 0" ) );
    run_math( rd, "n_item_relic_charges() = 2" );
    CHECK( held_relic->relic_data->charges() == 2 );
    run_math( rd, "n_item_relic_charges() = 9" );
    CHECK( held_relic->relic_data->charges() == 3 );
}

// ITEM-J04: transform_item worked only on the beta talker.
TEST_CASE( "item-j04_u_transform_item_transforms_the_alpha_item", "[audit][item][eoc]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    item widget( itype_audit_m10a_fault_parent );
    u.remove_weapon();
    REQUIRE( u.wield( widget ) );
    item_location held = u.get_wielded_item();
    REQUIRE( held );

    dialogue by_item( get_talker_for( held ), nullptr );
    run_effect( by_item, R"({ "effect": { "u_transform_item": "audit_m10a_crock" } })" );
    CHECK( held->typeId() == itype_audit_m10a_crock );

    // the older spelling still transforms the beta talker
    dialogue with_item( get_talker_for( u ), get_talker_for( held ) );
    run_effect( with_item, R"({ "effect": { "transform_item": "audit_m10a_fault_parent" } })" );
    CHECK( held->typeId() == itype_audit_m10a_fault_parent );
    run_effect( with_item, R"({ "effect": { "npc_transform_item": "audit_m10a_crock" } })" );
    CHECK( held->typeId() == itype_audit_m10a_crock );
}

// ITEM-J05: activates_artifact was documented and defined, but nothing sent it.
TEST_CASE( "item-j05_relic_activation_sends_activates_artifact", "[audit][item][event]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    item relic( itype_audit_m10a_relic );
    u.remove_weapon();
    REQUIRE( u.wield( relic ) );
    item_location held = u.get_wielded_item();
    REQUIRE( held );
    REQUIRE( held->is_relic() );

    artifact_events listener;
    get_event_bus().subscribe( &listener );
    on_out_of_scope unsubscribe( [&listener]() {
        get_event_bus().unsubscribe( &listener );
    } );
    held->use_relic( u, u.pos_bub() );

    REQUIRE( listener.itypes.size() == 1 );
    CHECK( listener.itypes.front() == itype_audit_m10a_relic );
    CHECK( listener.characters.front() == u.getID() );
}

// ITEM-J05: relics only recharged in hardcoded ways; recharge_type condition recharges while an
// EOC's condition holds.
TEST_CASE( "item-j05_relic_recharges_while_its_eoc_condition_holds", "[audit][item][eoc]" )
{
    clear_avatar();
    on_out_of_scope cleanup = forget_globals( { "audit_m10a_recharge", "audit_m10a_recharge_ran" } );
    avatar &u = get_avatar();
    item relic( itype_audit_m10a_relic );
    u.remove_weapon();
    REQUIRE( u.wield( relic ) );
    item_location held = u.get_wielded_item();
    REQUIRE( held );
    REQUIRE( held->relic_data->charges() == 0 );

    held->process_relic( &u, u.pos_bub() );
    CHECK( held->relic_data->charges() == 0 );

    get_globals().set_global_value( "audit_m10a_recharge", 1.0 );
    held->process_relic( &u, u.pos_bub() );
    CHECK( held->relic_data->charges() == 1 );
    // only the condition is used, the EOC doesn't run
    CHECK( get_globals().maybe_get_global_value( "audit_m10a_recharge_ran" ) == nullptr );
}

// ITEM-J06: corpse shelf life, rotting away and revival timing were C++ constants.
TEST_CASE( "item-j06_corpse_timings_come_from_the_monster", "[audit][item][monster]" )
{
    clear_map();
    clear_avatar();
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::start_of_cataclysm + 30_days;
    map &here = get_map();
    const tripoint_bub_ms pos = get_avatar().pos_bub() + tripoint_rel_ms( 2, 0, 0 );

    CHECK( mon_zombie->corpse_shelf_life == 24_hours );
    CHECK( mon_zombie->corpse_rot_away == 10_days );
    CHECK( mon_zombie->revive_min_age == 6_hours );
    CHECK( mon_zombie->revive_guaranteed == 48_hours );
    CHECK( mon_audit_m10a_quick_riser->revive_min_age == 1_hours );
    CHECK( mon_audit_m10a_quick_riser->revive_guaranteed == 2_hours );

    item zombie = item::make_corpse( mon_zombie, calendar::turn - 3_hours );
    item riser = item::make_corpse( mon_audit_m10a_quick_riser, calendar::turn - 3_hours );
    REQUIRE( zombie.is_corpse() );
    REQUIRE( riser.is_corpse() );
    CHECK( zombie.get_shelf_life() == 24_hours );
    CHECK( riser.get_shelf_life() == 12_hours );

    // 3 hours old: too young for a zombie (6 h), past sure revival for the riser (2 h).
    // make_corpse makes 1 in 20 corpses wait for the player to come close: not these.
    zombie.unset_flag( flag_REVIVE_SPECIAL );
    riser.unset_flag( flag_REVIVE_SPECIAL );
    REQUIRE( zombie.can_revive() );
    REQUIRE( riser.can_revive() );
    CHECK_FALSE( zombie.ready_to_revive( here, pos ) );
    CHECK( riser.ready_to_revive( here, pos ) );

    // corpses that can't revive rot away after their monster's corpse_rot_away
    zombie.set_flag( flag_FIELD_DRESS );
    riser.set_flag( flag_FIELD_DRESS );
    REQUIRE_FALSE( zombie.can_revive() );
    REQUIRE_FALSE( riser.can_revive() );
    // 5 shelf lives: 120 hours for the zombie, 60 for the riser
    zombie.set_relative_rot( 5.0 );
    riser.set_relative_rot( 5.0 );
    CHECK_FALSE( zombie.has_rotten_away() );
    CHECK( riser.has_rotten_away() );
}

// ITEM-J06: the FIELD_DRESS, MUSHY and IRRADIATED rot multipliers were hardcoded; they are now
// the flags' rot_multiplier, which any flag can have.
TEST_CASE( "item-j06_flag_rot_multipliers", "[audit][item]" )
{
    CHECK( flag_IRRADIATED->rot_multiplier() == Approx( 0.25 ) );
    CHECK( flag_MUSHY->rot_multiplier() == Approx( 3.0 ) );
    CHECK( flag_FIELD_DRESS->rot_multiplier() == Approx( 0.75 ) );

    item normal( itype_audit_m10a_cured );
    item slow( itype_audit_m10a_cured );
    item irradiated( itype_audit_m10a_cured );
    slow.set_flag( json_flag_AUDIT_M10A_SLOW_ROT );
    irradiated.set_flag( flag_IRRADIATED );
    const units::temperature warm = units::from_celsius( 20 );
    for( item *it : {
             &normal, &slow, &irradiated
         } ) {
        it->calc_rot( warm, 1.0f, 2_hours );
    }
    const double normal_rot = to_turns<double>( normal.get_rot() );
    REQUIRE( normal_rot > 100 );
    CHECK( to_turns<double>( slow.get_rot() ) == Approx( normal_rot * 0.5 ).epsilon( 0.01 ) );
    CHECK( to_turns<double>( irradiated.get_rot() ) == Approx( normal_rot * 0.25 ).epsilon( 0.01 ) );
}

// ITEM-J07: item group entries could only be gated by real-world holidays.
TEST_CASE( "item-j07_item_group_entry_conditions", "[audit][item]" )
{
    clear_avatar();
    on_out_of_scope cleanup = forget_globals( { "audit_m10a_spawn" } );

    std::vector<item> spawned = item_group::items_from( Item_spawn_data_audit_m10a_conditional,
                                calendar::turn );
    CHECK( count_of( spawned, itype_audit_m10a_cured ) == 0 );
    CHECK( count_of( spawned, itype_audit_m10a_crock ) == 1 );
    CHECK( item_group::items_from( Item_spawn_data_audit_m10a_conditional_pick,
                                   calendar::turn ).empty() );

    get_globals().set_global_value( "audit_m10a_spawn", 1.0 );
    spawned = item_group::items_from( Item_spawn_data_audit_m10a_conditional, calendar::turn );
    CHECK( count_of( spawned, itype_audit_m10a_cured ) == 1 );
    CHECK( count_of( spawned, itype_audit_m10a_crock ) == 1 );
    const std::vector<item> picked = item_group::items_from(
                                         Item_spawn_data_audit_m10a_conditional_pick,
                                         calendar::turn );
    CHECK( count_of( picked, itype_audit_m10a_cured ) == 1 );
}

// ITEM-J07: a group extended through copy-from could not lose any of its entries.
TEST_CASE( "item-j07_item_group_delete", "[audit][item]" )
{
    clear_avatar();
    // the deleted nested group would spawn a cured bit while this is set
    on_out_of_scope cleanup = forget_globals( { "audit_m10a_spawn" } );
    get_globals().set_global_value( "audit_m10a_spawn", 1.0 );

    const std::vector<item> spawned = item_group::items_from( Item_spawn_data_audit_m10a_trimmed,
                                      calendar::turn );
    CHECK( count_of( spawned, itype_audit_m10a_cured ) == 0 );
    CHECK( count_of( spawned, itype_audit_m10a_crock ) == 1 );
    CHECK( count_of( spawned, itype_audit_m10a_relic ) == 1 );
    CHECK( spawned.size() == 2 );
}

// ITEM-J08: fault fixes had no condition and ran no EOC.
TEST_CASE( "item-j08_fault_fix_condition_and_effect_on_conditions", "[audit][item][eoc]" )
{
    clear_avatar();
    on_out_of_scope cleanup = forget_globals( { "audit_m10a_can_oil", "audit_m10a_oiled" } );
    avatar &u = get_avatar();
    item widget( itype_audit_m10a_fault_parent );
    u.remove_weapon();
    REQUIRE( u.wield( widget ) );
    REQUIRE( u.get_wielded_item() );
    u.get_wielded_item()->set_fault( fault_audit_m10a_squeak, true );
    REQUIRE( u.get_wielded_item()->has_fault( fault_audit_m10a_squeak ) );
    REQUIRE( fault_audit_m10a_squeak->get_fixes().count( fault_fix_audit_m10a_oil_squeak ) == 1 );

    // while the condition is false the fix isn't offered, so there is nothing to start
    u.mend_item( u.get_wielded_item(), false );
    CHECK( u.activity.id() != ACT_MEND_ITEM );

    get_globals().set_global_value( "audit_m10a_can_oil", 1.0 );
    u.mend_item( u.get_wielded_item(), false );
    REQUIRE( u.activity.id() == ACT_MEND_ITEM );
    process_activity( u );
    CHECK_FALSE( u.get_wielded_item()->has_fault( fault_audit_m10a_squeak ) );
    // the EOC ran with the item as npc (its wetness is 0)
    const diag_value *oiled = get_globals().maybe_get_global_value( "audit_m10a_oiled" );
    REQUIRE( oiled != nullptr );
    CHECK( oiled->dbl() == Approx( 1 ) );
}

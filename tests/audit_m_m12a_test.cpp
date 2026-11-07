#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "math_parser.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "stats_tracker.h"
#include "talker.h"
#include "type_id.h"

static const flag_id json_flag_BINDS_HANDS( "BINDS_HANDS" );

static const itype_id itype_audit_m12a_bead( "audit_m12a_bead" );
static const itype_id itype_audit_m12a_box( "audit_m12a_box" );
static const itype_id itype_audit_m12a_inner_box( "audit_m12a_inner_box" );
static const itype_id itype_audit_m12a_sealed_pouch( "audit_m12a_sealed_pouch" );
static const itype_id itype_audit_m12a_small_box( "audit_m12a_small_box" );
static const itype_id itype_audit_m12a_trinket( "audit_m12a_trinket" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_e_handcuffs( "e_handcuffs" );
static const itype_id itype_knife_hunting( "knife_hunting" );
static const itype_id itype_sheath( "sheath" );

namespace
{

talk_effect_t load_m12a_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_m_m12a_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
}

void run_m12a_effect( dialogue &d, const std::string &json )
{
    load_m12a_effect( json ).apply( d );
}

bool m12a_condition( dialogue &d, const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    conditional_t::func condition;
    read_condition( jo, "condition", condition, false );
    finalize_conditions();
    return condition( d );
}

double m12a_math( dialogue &d, const std::string_view expression )
{
    math_exp exp;
    REQUIRE( exp.parse( expression ) );
    return exp.eval( d );
}

// How many items of a type lie on a tile
int count_on_tile( const tripoint_bub_ms &pos, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( pos ) ) {
        if( it.typeId() == type ) {
            count += it.count();
        }
    }
    return count;
}

// Wears a backpack so the avatar has room for what it picks up or is given
void wear_m12a_backpack( avatar &u )
{
    const std::optional<std::list<item>::iterator> worn = u.wear_item( item( itype_backpack ),
            false );
    REQUIRE( worn.has_value() );
}

struct m12a_event_record {
    event_type type = event_type::num_event_types;
    itype_id itype = itype_id::NULL_ID();
    int count = 0;
    itype_id container = itype_id::NULL_ID();
    // the beta talker's item, if it was one
    itype_id beta_item = itype_id::NULL_ID();
};

struct m12a_event_catcher : event_subscriber {
    std::vector<m12a_event_record> events;
    using event_subscriber::notify;
    void notify( const cata::event & ) override {}
    void notify( const cata::event &e, std::unique_ptr<talker>,
                 std::unique_ptr<talker> beta ) override {
        const event_type t = e.type();
        if( t != event_type::character_picks_up_item && t != event_type::character_drops_item &&
            t != event_type::character_stores_item ) {
            return;
        }
        m12a_event_record rec;
        rec.type = t;
        rec.itype = e.get<itype_id>( "itype" );
        rec.count = e.get<int>( "count" );
        if( t == event_type::character_stores_item ) {
            rec.container = e.get<itype_id>( "container" );
        }
        if( beta ) {
            if( item_location *loc = beta->get_item() ) {
                if( *loc ) {
                    rec.beta_item = ( *loc )->typeId();
                }
            }
        }
        events.push_back( rec );
    }
};

} // namespace

// INV-J05: the doc table named the effect u_unset_flag and left out that a consumed container
// drops its contents (the rest of the bug was fixed with EOC-B05).  The contents land where the
// container was.
TEST_CASE( "inv-j05_consume_item_sum_drops_a_consumed_containers_contents",
           "[audit][inventory][eoc]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_avatar();
        clear_map();
    } );
    avatar &u = get_avatar();
    item box( itype_audit_m12a_box );
    REQUIRE( box.put_in( item( itype_audit_m12a_trinket ), pocket_type::CONTAINER ).success() );
    REQUIRE( u.wield( box ) );
    REQUIRE( u.amount_of( itype_audit_m12a_box ) == 1 );

    dialogue d( get_talker_for( u ), nullptr );
    run_m12a_effect( d, R"({ "effect": { "u_consume_item_sum": [
        { "item": "audit_m12a_box", "amount": 1 } ] } })" );

    CHECK( u.amount_of( itype_audit_m12a_box ) == 0 );
    CHECK( u.amount_of( itype_audit_m12a_trinket ) == 0 );
    CHECK( count_on_tile( u.pos_bub(), itype_audit_m12a_trinket ) == 1 );
}

// INV-J06: OBTAIN_COST_MULTIPLIER was overwritten for holsters, so drawing from a sheath or
// holster ignored it, and storing had no enchantment at all.  The electronic handcuffs' 4x
// handling cost was tied to their id; it is now the BINDS_HANDS flag.
TEST_CASE( "inv-j06_holster_draw_and_store_follow_their_enchantments", "[audit][inventory]" )
{
    clear_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    avatar &u = get_avatar();
    const std::optional<std::list<item>::iterator> worn = u.wear_item( item( itype_sheath ),
            false );
    REQUIRE( worn.has_value() );
    const std::list<item>::iterator sheath_it = *worn;
    item_location sheath_loc( u, &*sheath_it );
    REQUIRE( sheath_loc->put_in( item( itype_knife_hunting ), pocket_type::CONTAINER ).success() );
    item_location knife_loc( sheath_loc, &sheath_loc->only_item() );

    const int base_draw = knife_loc.obtain_cost( u );
    const int base_retrieve = u.item_retrieve_cost( *knife_loc, *sheath_loc, false, 20 );
    const int base_store = u.item_store_cost( *knife_loc, *sheath_loc, false, 20 );
    CAPTURE( base_draw, base_retrieve, base_store );
    REQUIRE( base_draw >= 10 );
    REQUIRE( base_retrieve >= 10 );
    REQUIRE( base_store >= 10 );

    set_single_trait( u, "audit_m12a_quick_hands" );
    u.recalculate_enchantment_cache();
    // multiply -0.5 halves them; the results are truncated
    CHECK( knife_loc.obtain_cost( u ) == Approx( base_draw * 0.5 ).margin( 1 ) );
    CHECK( u.item_retrieve_cost( *knife_loc, *sheath_loc, false, 20 ) ==
           Approx( base_retrieve * 0.5 ).margin( 1 ) );
    CHECK( u.item_store_cost( *knife_loc, *sheath_loc, false, 20 ) ==
           Approx( base_store * 0.5 ).margin( 1 ) );

    clear_character( u );
    u.recalculate_enchantment_cache();
    const int free_handling = u.item_handling_cost( item( itype_audit_m12a_trinket ), false, 20 );
    item cuffs( itype_e_handcuffs );
    CHECK( cuffs.has_flag( json_flag_BINDS_HANDS ) );
    REQUIRE( u.wield( cuffs ) );
    CHECK( u.item_handling_cost( item( itype_audit_m12a_trinket ), false, 20 ) ==
           4 * free_handling );
}

// INV-J07: an item talker answered every inventory query with 0 or false and ignored every
// inventory effect.  It now answers for what the item holds.
TEST_CASE( "inv-j07_item_talker_answers_for_its_contents", "[audit][inventory][eoc]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_avatar();
        clear_map();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub() + tripoint_rel_ms::east;

    item box( itype_audit_m12a_box );
    for( int i = 0; i < 3; ++i ) {
        REQUIRE( box.put_in( item( itype_audit_m12a_trinket ), pocket_type::CONTAINER ).success() );
    }
    item beads( itype_audit_m12a_bead );
    beads.charges = 20;
    REQUIRE( box.put_in( beads, pocket_type::CONTAINER ).success() );
    item inner( itype_audit_m12a_inner_box );
    for( int i = 0; i < 2; ++i ) {
        REQUIRE( inner.put_in( item( itype_audit_m12a_trinket ), pocket_type::CONTAINER ).success() );
    }
    REQUIRE( box.put_in( inner, pocket_type::CONTAINER ).success() );
    item &placed = here.add_item( pos, box );
    item_location box_loc( map_cursor( here.get_abs( pos ) ), &placed );
    REQUIRE( box_loc );

    dialogue d( get_talker_for( u ), get_talker_for( box_loc ) );
    // nested contents count, the item itself doesn't
    CHECK( m12a_math( d, "n_item_count('audit_m12a_trinket')" ) == 5 );
    CHECK( m12a_math( d, "n_charge_count('audit_m12a_bead')" ) == 20 );
    CHECK( m12a_condition( d, R"({ "condition": { "npc_has_item": "audit_m12a_inner_box" } })" ) );
    CHECK_FALSE( m12a_condition( d, R"({ "condition": { "npc_has_item": "audit_m12a_box" } })" ) );
    CHECK( m12a_condition( d,
                           R"({ "condition": { "npc_has_items": { "item": "audit_m12a_trinket", "count": 5 } } })" ) );
    CHECK_FALSE( m12a_condition( d,
                                 R"({ "condition": { "npc_has_items": { "item": "audit_m12a_trinket", "count": 6 } } })" ) );
    CHECK( m12a_condition( d,
                           R"({ "condition": { "npc_has_items": { "item": "audit_m12a_bead", "charges": 20 } } })" ) );
    CHECK( m12a_condition( d, R"({ "condition": { "npc_has_item_with_flag": "TRADER_AVOID" } })" ) );
    CHECK_FALSE( m12a_condition( d,
                                 R"({ "condition": { "npc_has_item_with_flag": "BINDS_HANDS" } })" ) );

    SECTION( "run_inv_eocs offers what it holds" ) {
        get_globals().clear_global_values();
        on_out_of_scope clear_globals( []() {
            get_globals().clear_global_values();
        } );
        dialogue from_box( get_talker_for( box_loc ), nullptr );
        run_m12a_effect( from_box, R"({ "effect": { "u_run_inv_eocs": "all",
            "search_data": [ { "id": "audit_m12a_trinket" } ],
            "true_eocs": [ "audit_m12a_count_contents" ] } })" );
        CHECK( m12a_math( from_box, "g_audit_m12a_seen" ) == 5 );
    }

    SECTION( "remove_item_with takes what it holds" ) {
        run_m12a_effect( d, R"({ "effect": { "npc_remove_item_with": "audit_m12a_trinket" } })" );
        CHECK( m12a_math( d, "n_item_count('audit_m12a_trinket')" ) == 0 );
        CHECK( m12a_math( d, "n_charge_count('audit_m12a_bead')" ) == 20 );
        CHECK( box_loc->has_item_with( []( const item & it ) {
            return it.typeId() == itype_audit_m12a_inner_box;
        } ) );
    }
}

// INV-J07: u_spawn_item on an item talker puts the items in it (what doesn't fit lands on its
// tile), and with "container" the items that don't fit are given separately instead of lost.
TEST_CASE( "inv-j07_spawn_item_fills_item_talkers_and_containers", "[audit][inventory][eoc]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_avatar();
        clear_map();
    } );
    avatar &u = get_avatar();
    map &here = get_map();

    SECTION( "into an item talker" ) {
        const tripoint_bub_ms pos = u.pos_bub() + tripoint_rel_ms::east;
        item &placed = here.add_item( pos, item( itype_audit_m12a_small_box ) );
        item_location box_loc( map_cursor( here.get_abs( pos ) ), &placed );
        REQUIRE( box_loc );
        dialogue d( get_talker_for( box_loc ), nullptr );
        run_m12a_effect( d, R"({ "effect": { "u_spawn_item": "audit_m12a_trinket", "count": 3,
            "suppress_message": true } })" );
        // the box holds two; the third is on the box's tile
        CHECK( box_loc->amount_of( itype_audit_m12a_trinket ) == 2 );
        CHECK( count_on_tile( pos, itype_audit_m12a_trinket ) == 1 );
    }

    SECTION( "in a container that is too small" ) {
        wear_m12a_backpack( u );
        dialogue d( get_talker_for( u ), nullptr );
        run_m12a_effect( d, R"({ "effect": { "u_spawn_item": "audit_m12a_trinket", "count": 3,
            "container": "audit_m12a_small_box", "suppress_message": true } })" );
        const std::vector<item *> boxes = u.items_with( []( const item & it ) {
            return it.typeId() == itype_audit_m12a_small_box;
        } );
        REQUIRE( boxes.size() == 1 );
        CHECK( boxes.front()->amount_of( itype_audit_m12a_trinket ) == 2 );
        // three copies, not one with 3 charges; none is lost
        CHECK( u.amount_of( itype_audit_m12a_trinket ) +
               count_on_tile( u.pos_bub(), itype_audit_m12a_trinket ) == 3 );
    }
}

// INV-J08: picking up, dropping and storing items sent no event.
TEST_CASE( "inv-j08_pickup_drop_and_store_send_events", "[audit][inventory][event]" )
{
    clear_avatar();
    clear_map();
    on_out_of_scope cleanup( []() {
        clear_avatar();
        clear_map();
    } );
    avatar &u = get_avatar();
    map &here = get_map();
    // one summary per item type and count would grow every save
    CHECK_FALSE( stats_tracker::records( event_type::character_picks_up_item ) );
    CHECK_FALSE( stats_tracker::records( event_type::character_drops_item ) );
    CHECK_FALSE( stats_tracker::records( event_type::character_stores_item ) );
    m12a_event_catcher catcher;
    get_event_bus().subscribe( &catcher );
    on_out_of_scope unsubscribe( [&catcher]() {
        get_event_bus().unsubscribe( &catcher );
    } );

    SECTION( "storing in a sheath" ) {
        const std::optional<std::list<item>::iterator> worn = u.wear_item( item( itype_sheath ),
                false );
        REQUIRE( worn.has_value() );
        const std::list<item>::iterator sheath_it = *worn;
        item knife( itype_knife_hunting );
        REQUIRE( u.wield( knife ) );
        item_location wielded = u.get_wielded_item();
        REQUIRE( wielded );
        // avatar::store( JsonOut & ) hides Character's
        Character &guy = u;
        guy.store( *sheath_it, *wielded, false, 0, pocket_type::CONTAINER, false );
        CHECK( sheath_it->amount_of( itype_knife_hunting ) == 1 );
        REQUIRE( catcher.events.size() == 1 );
        CHECK( catcher.events[0].type == event_type::character_stores_item );
        CHECK( catcher.events[0].itype == itype_knife_hunting );
        CHECK( catcher.events[0].count == 1 );
        CHECK( catcher.events[0].container == itype_sheath );
        CHECK( catcher.events[0].beta_item == itype_sheath );
    }

    SECTION( "picking up and dropping" ) {
        wear_m12a_backpack( u );
        item &trinket = here.add_item( u.pos_bub(), item( itype_audit_m12a_trinket ) );
        const std::vector<item_location> targets = {
            item_location( map_cursor( u.pos_abs() ), &trinket )
        };
        u.set_moves( 100 );
        u.assign_activity( pickup_activity_actor( targets, { 0 }, u.pos_bub(), false ) );
        u.activity.do_turn( u );
        REQUIRE( u.amount_of( itype_audit_m12a_trinket ) == 1 );
        REQUIRE( catcher.events.size() == 1 );
        CHECK( catcher.events[0].type == event_type::character_picks_up_item );
        CHECK( catcher.events[0].itype == itype_audit_m12a_trinket );
        CHECK( catcher.events[0].count == 1 );
        CHECK( catcher.events[0].beta_item == itype_audit_m12a_trinket );

        catcher.events.clear();
        u.cancel_activity();
        item_location carried;
        u.visit_items( [&carried]( const item_location & loc ) {
            if( loc->typeId() == itype_audit_m12a_trinket ) {
                carried = loc;
                return VisitResponse::ABORT;
            }
            return VisitResponse::NEXT;
        } );
        REQUIRE( carried );
        u.set_moves( 100 );
        u.drop( carried, u.pos_bub() );
        u.activity.do_turn( u );
        CHECK( count_on_tile( u.pos_bub(), itype_audit_m12a_trinket ) == 1 );
        REQUIRE( catcher.events.size() == 1 );
        CHECK( catcher.events[0].type == event_type::character_drops_item );
        CHECK( catcher.events[0].itype == itype_audit_m12a_trinket );
        CHECK( catcher.events[0].count == 1 );
        CHECK( catcher.events[0].beta_item == itype_audit_m12a_trinket );
    }
}

// INV-J09: sealed_data read only spoil_multiplier, though the docs said a sealed pocket
// overrides the unsealed one.  It can now also set watertight, airtight, transparent,
// insulation and moves, which apply only while the pocket is sealed.
TEST_CASE( "inv-j09_sealed_data_overrides_apply_while_sealed", "[audit][pocket]" )
{
    item pouch( itype_audit_m12a_sealed_pouch );
    REQUIRE( pouch.put_in( item( itype_audit_m12a_trinket ), pocket_type::CONTAINER ).success() );
    std::vector<item_pocket *> pockets = pouch.get_container_pockets();
    REQUIRE( pockets.size() == 1 );
    item_pocket &pocket = *pockets.front();
    // item::seal() wants a full container; the pocket seals whenever it holds something
    REQUIRE_FALSE( pocket.sealed() );
    CHECK_FALSE( pocket.watertight() );
    REQUIRE( pocket.seal() );
    REQUIRE( pocket.sealed() );
    CHECK( pocket.watertight() );
    CHECK( pocket.airtight() );
    CHECK( pocket.transparent() );
    CHECK( pocket.insulation() == Approx( 3.0f ) );
    CHECK( pocket.moves() == 400 );
    CHECK( pocket.spoil_multiplier() == Approx( 0.5f ) );

    pocket.unseal();
    REQUIRE_FALSE( pocket.sealed() );
    CHECK_FALSE( pocket.watertight() );
    CHECK_FALSE( pocket.airtight() );
    CHECK_FALSE( pocket.transparent() );
    CHECK( pocket.insulation() == Approx( 1.0f ) );
    CHECK( pocket.moves() == 100 );
    CHECK( pocket.spoil_multiplier() == Approx( 1.0f ) );

    // members left out keep the pocket's own values; moves must be at least 1
    sealable_data only_spoil;
    only_spoil.load( json_loader::from_string( R"({ "spoil_multiplier": 0.0 })" ) );
    CHECK_FALSE( only_spoil.watertight.has_value() );
    CHECK_FALSE( only_spoil.moves.has_value() );
    sealable_data no_moves;
    CHECK_THROWS_AS( no_moves.load( json_loader::from_string( R"({ "moves": 0 })" ) ), JsonError );
}

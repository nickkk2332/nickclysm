#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_variant.h"
#include "clone_ptr.h"
#include "computer.h"
#include "condition.h"
#include "construction.h"
#include "coordinates.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "iexamine.h"
#include "iexamine_actors.h"
#include "item.h"
#include "item_location.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "mapdata.h"
#include "mapgen_functions.h"
#include "mapgendata.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "ret_val.h"
#include "talker.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"

static const construction_str_id construction_ap_box_battery_charger( "ap_box_battery_charger" );
static const construction_str_id construction_app_wall_wiring( "app_wall_wiring" );
static const construction_str_id
construction_constr_deconstruct_simple( "constr_deconstruct_simple" );
static const construction_str_id
construction_constr_test_m28b_other_appliance( "constr_test_m28b_other_appliance" );
static const construction_str_id construction_constr_veh( "constr_veh" );

static const damage_type_id damage_cut( "cut" );

static const effect_on_condition_id
effect_on_condition_EOC_LIXA_UNFOLD_ENTRY_SETUP_2( "EOC_LIXA_UNFOLD_ENTRY_SETUP_2" );
static const effect_on_condition_id
effect_on_condition_EOC_PORTAL_STORM_DUNGEON( "EOC_PORTAL_STORM_DUNGEON" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_M28B_COUNT_B( "EOC_TEST_M28B_COUNT_B" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_M28B_INLINE_LOGIN( "EOC_TEST_M28B_INLINE_LOGIN" );
static const effect_on_condition_id
effect_on_condition_EOC_TEST_M28B_OPTION( "EOC_TEST_M28B_OPTION" );

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_beartrap( "beartrap" );
static const itype_id itype_box_battery_charger( "box_battery_charger" );
static const itype_id itype_dimensional_anchor_on( "dimensional_anchor_on" );
static const itype_id itype_id_science( "id_science" );
static const itype_id itype_rock( "rock" );

static const ter_str_id ter_t_card_science( "t_card_science" );
static const ter_str_id ter_t_door_metal_c( "t_door_metal_c" );
static const ter_str_id ter_t_door_metal_locked( "t_door_metal_locked" );
static const ter_str_id ter_t_test_m28b_gated( "t_test_m28b_gated" );
static const ter_str_id ter_t_test_m28b_gated_msg( "t_test_m28b_gated_msg" );
static const ter_str_id ter_t_test_m28b_npc_choice( "t_test_m28b_npc_choice" );
static const ter_str_id ter_t_test_m28b_reader_hack( "t_test_m28b_reader_hack" );
static const ter_str_id ter_t_test_m28b_reader_noquery( "t_test_m28b_reader_noquery" );
static const ter_str_id ter_t_test_m28b_reader_query( "t_test_m28b_reader_query" );

static const trap_str_id tr_LIXA_unfold_entry_2( "tr_LIXA_unfold_entry_2" );
static const trap_str_id tr_beartrap( "tr_beartrap" );
static const trap_str_id tr_caltrops( "tr_caltrops" );
static const trap_str_id tr_portal_dungeon_entrance( "tr_portal_dungeon_entrance" );
static const trap_str_id tr_telepad( "tr_telepad" );
static const trap_str_id tr_test_m28b_beartrap( "tr_test_m28b_beartrap" );
static const trap_str_id tr_test_m28b_caltrops( "tr_test_m28b_caltrops" );
static const trap_str_id tr_test_m28b_plate( "tr_test_m28b_plate" );
static const trap_str_id tr_test_m28b_plate_child( "tr_test_m28b_plate_child" );
static const trap_str_id tr_test_m28b_plate_inert( "tr_test_m28b_plate_inert" );
static const trap_str_id tr_test_m28b_plate_kept( "tr_test_m28b_plate_kept" );

static const update_mapgen_id update_mapgen_test_m28b_computer( "test_m28b_computer" );

namespace
{

void reset_world()
{
    clear_map();
    clear_avatar();
    clear_creatures();
    clear_npcs();
    clear_vehicles();
    get_globals().clear_global_values();
    Messages::clear_messages();
}

double global_num( const std::string &name )
{
    const diag_value *v = get_globals().maybe_get_global_value( name );
    return v != nullptr && v->is_dbl() ? v->dbl() : 0.0;
}

std::string global_str( const std::string &name )
{
    const diag_value *v = get_globals().maybe_get_global_value( name );
    return v != nullptr && !v->is_dbl() && !v->is_empty() ? v->str() : std::string();
}

bool has_message( const std::string &text )
{
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        if( msg.second.find( text ) != std::string::npos ) {
            return true;
        }
    }
    return false;
}

int count_items( const tripoint_bub_ms &p, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( p ) ) {
        if( it.typeId() == type ) {
            count += it.count();
        }
    }
    return count;
}

// Keeps the events of some types that go through the game's event bus while it lives
struct event_catcher : event_subscriber {
    std::vector<event_type> types;
    std::vector<cata::event> caught;
    explicit event_catcher( std::vector<event_type> t ) : types( std::move( t ) ) {
        get_event_bus().subscribe( this );
    }
    ~event_catcher() override {
        get_event_bus().unsubscribe( this );
    }
    event_catcher( const event_catcher & ) = delete;
    event_catcher &operator=( const event_catcher & ) = delete;
    void keep( const cata::event &e ) {
        for( const event_type t : types ) {
            if( t == e.type() ) {
                caught.push_back( e );
                return;
            }
        }
    }
    void notify( const cata::event &e ) override {
        keep( e );
    }
    void notify( const cata::event &e, std::unique_ptr<talker>, std::unique_ptr<talker> ) override {
        keep( e );
    }
    std::vector<cata::event> of( const event_type t ) const {
        std::vector<cata::event> found;
        for( const cata::event &e : caught ) {
            if( e.type() == t ) {
                found.push_back( e );
            }
        }
        return found;
    }
};

const cardreader_examine_actor *cardreader_of( const ter_str_id &ter )
{
    for( const cata::clone_ptr<iexamine_actor> &actor : ter->get_examine_actors() ) {
        if( const cardreader_examine_actor *reader =
                dynamic_cast<const cardreader_examine_actor *>( actor.get() ) ) {
            return reader;
        }
    }
    return nullptr;
}

// Finishes con at site with u as the builder, as if all the work but the last turn were done.
void finish_construction( avatar &u, const construction_str_id &con, const tripoint_bub_ms &site,
                          const std::list<item> &components )
{
    map &here = get_map();
    for( const std::pair<const skill_id, int> &skill : con->required_skills ) {
        u.set_skill_level( skill.first, skill.second );
    }
    partial_con pc;
    pc.id = con.id();
    pc.counter = 9999999;
    pc.components = components;
    here.partial_con_set( site, pc );
    u.assign_activity( build_construction_activity_actor( here.get_abs( site ) ) );
    REQUIRE( process_activity_bounded( u, 10, 100 ) );
}

JsonObject json_object( const std::string &json )
{
    return json_loader::from_string( json ).get_object();
}

} // namespace

// TERR-J03: the EOC examine actor had no condition (the tile always counted as examinable and
// its menu entry was never greyed out), and an NPC always ran the first actor.
TEST_CASE( "terr-j03_eoc_examine_actor_condition_and_npc_choice", "[audit][examine]" )
{
    reset_world();
    on_out_of_scope cleanup( []() {
        reset_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint::east;

    SECTION( "without a message the action can't be used while the condition fails" ) {
        here.ter_set( spot, ter_t_test_m28b_gated );
        const ter_t &ter = here.ter( spot ).obj();
        CHECK_FALSE( ter.can_examine( spot ) );
        ter.examine( u, spot );
        CHECK( global_num( "m28b_a" ) == 0 );
        get_globals().set_global_value( "m28b_allow", 1.0 );
        CHECK( ter.can_examine( spot ) );
        ter.examine( u, spot );
        CHECK( global_num( "m28b_a" ) == 1 );
    }
    SECTION( "with a message the tile can be examined, and it says why nothing happens" ) {
        here.ter_set( spot, ter_t_test_m28b_gated_msg );
        const ter_t &ter = here.ter( spot ).obj();
        CHECK( ter.can_examine( spot ) );
        ter.examine( u, spot );
        CHECK( global_num( "m28b_a" ) == 0 );
        CHECK( has_message( "You feel nothing." ) );
    }
    SECTION( "an NPC uses the first action it may use" ) {
        here.ter_set( spot, ter_t_test_m28b_npc_choice );
        npc &guy = spawn_npc( u.pos_bub().xy() + point::south, "thug" );
        clear_character( guy );
        const ter_t &ter = here.ter( spot ).obj();
        const iexamine_actor *chosen = ter.npc_examine_actor( guy, spot );
        REQUIRE( chosen != nullptr );
        CHECK( chosen == ter.get_examine_actors()[1].get() );
        ter.examine( guy, spot );
        CHECK( global_num( "m28b_a" ) == 0 );
        CHECK( global_num( "m28b_b" ) == 1 );
    }
    SECTION( "an NPC can't use an action whose condition it doesn't meet" ) {
        here.ter_set( spot, ter_t_test_m28b_gated );
        npc &guy = spawn_npc( u.pos_bub().xy() + point::south, "thug" );
        clear_character( guy );
        const ter_t &ter = here.ter( spot ).obj();
        CHECK( ter.npc_examine_actor( guy, spot ) == nullptr );
        ter.examine( guy, spot );
        CHECK( global_num( "m28b_a" ) == 0 );
        get_globals().set_global_value( "m28b_allow", 1.0 );
        CHECK( ter.npc_examine_actor( guy, spot ) != nullptr );
    }
}

// TERR-J04: "query": false still asked; the card reader's hack had no difficulty or EOC of its
// own (the rest of the hack is ACT-J06's, see audit_m_m26a_test.cpp).
TEST_CASE( "terr-j04_card_reader_query_hack_difficulty_and_eocs", "[audit][examine]" )
{
    reset_world();
    on_out_of_scope cleanup( []() {
        reset_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms reader = u.pos_bub() + tripoint( 3, 0, 0 );
    const tripoint_bub_ms door = reader + tripoint::east;
    u.wear_item( item( itype_backpack ), false );
    u.i_add( item( itype_id_science ) );
    REQUIRE( u.has_amount( itype_id_science, 1 ) );
    here.ter_set( door, ter_t_door_metal_locked );

    SECTION( "query false: the card is used without asking" ) {
        here.ter_set( reader, ter_t_test_m28b_reader_noquery );
        here.ter( reader ).obj().examine( u, reader );
        CHECK( here.ter( door ) == ter_t_door_metal_c );
        CHECK( has_message( "m28b doors open." ) );
    }
    SECTION( "query true: the question comes first (and tests answer no)" ) {
        here.ter_set( reader, ter_t_test_m28b_reader_query );
        here.ter( reader ).obj().examine( u, reader );
        CHECK( here.ter( door ) == ter_t_door_metal_locked );
    }
    SECTION( "hack_difficulty and hack_eocs" ) {
        here.ter_set( reader, ter_t_test_m28b_reader_hack );
        const cardreader_examine_actor *actor = cardreader_of( ter_t_test_m28b_reader_hack );
        REQUIRE( actor != nullptr );
        CHECK( actor->get_hack_difficulty() == 9 );
        const cardreader_examine_actor *vanilla = cardreader_of( ter_t_card_science );
        REQUIRE( vanilla != nullptr );
        CHECK( vanilla->get_hack_difficulty() == 6 );
        hacking_activity_actor::hack_success( u, reader );
        CHECK( here.ter( door ) == ter_t_door_metal_c );
        CHECK( global_num( "m28b_hacked" ) == 1 );
    }
}

// TERR-J04 (found by ACT-J06): the hacking activity was saved as null but loaded as an object,
// so a game saved during a hack didn't load.
TEST_CASE( "terr-j04_a_game_saved_during_a_hack_loads", "[audit][activities]" )
{
    reset_world();
    on_out_of_scope cleanup( []() {
        reset_world();
    } );
    avatar &u = get_avatar();

    std::ostringstream os;
    JsonOut jsout( os );
    hacking_activity_actor( item_location() ).serialize( jsout );
    CHECK( os.str().find( "tool" ) != std::string::npos );
    JsonValue saved = json_loader::from_string( os.str() );
    CHECK( hacking_activity_actor::deserialize( saved ) != nullptr );

    // as saved before
    JsonValue old = json_loader::from_string( "null" );
    std::unique_ptr<activity_actor> from_old = hacking_activity_actor::deserialize( old );
    REQUIRE( from_old != nullptr );
    // without its tool the hack ends at once
    player_activity act( *from_old );
    act.placement = get_map().get_abs( u.pos_bub() );
    from_old->finish( act, u );
    CHECK( act.is_null() );
    CHECK( has_message( "You cannot hack this." ) );
}

// TERR-J05: the components of a done_appliance or done_vehicle construction were kept only in
// the APPLIANCE category or for constr_veh; the darkness rule was skipped by group id.
TEST_CASE( "terr-j05_appliance_construction_of_any_category_keeps_its_component",
           "[audit][construction]" )
{
    CHECK( construction_constr_test_m28b_other_appliance->uses_components_after_completion() );
    CHECK( construction_constr_veh->uses_components_after_completion() );
    CHECK( construction_ap_box_battery_charger->uses_components_after_completion() );
    CHECK_FALSE( construction_app_wall_wiring->uses_components_after_completion() );
    CHECK( construction_constr_deconstruct_simple->dark_craftable );

    reset_world();
    on_out_of_scope cleanup( []() {
        reset_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms site = u.pos_bub() + tripoint::east;
    std::list<item> components;
    components.emplace_back( itype_box_battery_charger, calendar::turn );
    finish_construction( u, construction_constr_test_m28b_other_appliance, site, components );
    CHECK( here.partial_con_at( site ) == nullptr );
    const optional_vpart_position vp = here.veh_at( site );
    REQUIRE( vp.has_value() );
    CHECK( vp->vehicle().is_appliance() );
}

// TERR-J06: the action checks of "eocs" and "spell_data" never failed, and a copy-from child
// ran its parent's eocs too.
TEST_CASE( "terr-j06_trap_eocs_replace_on_copy_and_need_the_eocs_action", "[audit][traps]" )
{
    CHECK( tr_portal_dungeon_entrance->eocs == std::vector<effect_on_condition_id> {
        effect_on_condition_EOC_PORTAL_STORM_DUNGEON
    } );
    CHECK( tr_LIXA_unfold_entry_2->eocs == std::vector<effect_on_condition_id> {
        effect_on_condition_EOC_LIXA_UNFOLD_ENTRY_SETUP_2
    } );
    CHECK( tr_test_m28b_plate_child->eocs == std::vector<effect_on_condition_id> {
        effect_on_condition_EOC_TEST_M28B_COUNT_B
    } );
    CHECK( tr_test_m28b_plate_child->item_eocs.empty() );
    // another action drops them
    CHECK( tr_test_m28b_plate_inert->eocs.empty() );
    CHECK( tr_test_m28b_plate_inert->item_eocs.empty() );

    const auto load_trap = []( const std::string & extra ) {
        trap t;
        JsonObject jo = json_object( R"({ "id": "tr_test_m28b_bad", "name": "bad", "color": "red",
            "symbol": "^", "visibility": 1, "avoidance": 1, "difficulty": 1, )" + extra + " }" );
        jo.allow_omitted_members();
        t.load( jo, "dda" );
    };
    CHECK_THROWS_AS( load_trap( R"("action": "blade", "eocs": [ "EOC_TEST_M28B_COUNT_A" ])" ),
                     JsonError );
    CHECK_THROWS_AS( load_trap( R"("action": "blade", "item_eocs": [ "EOC_TEST_M28B_COUNT_A" ])" ),
                     JsonError );
    CHECK_THROWS_AS( load_trap( R"("action": "eocs", "spell_data": { "id": "test_spell_pew" })" ),
                     JsonError );
}

// TERR-J07: an item or a sound did nothing to an eocs trap, disarming had no hooks and a hardcoded
// telepad rule, no condition read traps, and a monster setting one off sent no event.
TEST_CASE( "terr-j07_trap_item_triggers_disarm_hooks_map_trap_id_and_event", "[audit][traps]" )
{
    reset_world();
    on_out_of_scope cleanup( []() {
        reset_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint( 2, 0, 0 );

    SECTION( "an item sets off item_eocs, without an alpha talker, and uses the trap up" ) {
        here.trap_set( spot, tr_test_m28b_plate.id() );
        item rock( itype_rock );
        here.tr_at( spot ).trigger( spot, rock );
        CHECK( global_num( "m28b_item_runs" ) == 1 );
        CHECK( global_str( "m28b_item" ) == "rock" );
        CHECK( global_str( "m28b_item_trap" ) == "tr_test_m28b_plate" );
        // the creature EOC doesn't run
        CHECK( global_num( "m28b_a" ) == 0 );
        CHECK( here.tr_at( spot ).is_null() );
    }
    SECTION( "a sound sets them off too; UNCONSUMED keeps the trap" ) {
        here.trap_set( spot, tr_test_m28b_plate_kept.id() );
        here.tr_at( spot ).trigger( spot );
        CHECK( global_num( "m28b_item_runs" ) == 1 );
        CHECK( global_str( "m28b_item" ).empty() );
        CHECK( here.tr_at( spot ).id == tr_test_m28b_plate_kept );
    }
    SECTION( "without item_eocs an item does nothing to an eocs trap" ) {
        here.trap_set( spot, tr_test_m28b_plate_child.id() );
        item rock( itype_rock );
        here.tr_at( spot ).trigger( spot, rock );
        CHECK( global_num( "m28b_item_runs" ) == 0 );
        CHECK( global_num( "m28b_b" ) == 0 );
        CHECK( here.tr_at( spot ).id == tr_test_m28b_plate_child );
    }
    SECTION( "disarm condition and disarm eocs" ) {
        here.trap_set( spot, tr_test_m28b_plate.id() );
        const trap &plate = here.tr_at( spot );
        CHECK_FALSE( plate.can_try_disarm( u, spot ) );
        get_globals().set_global_value( "m28b_allow", 1.0 );
        CHECK( plate.can_try_disarm( u, spot ) );
        plate.run_eocs( plate.disarm_eocs, &u, here.get_abs( spot ) );
        CHECK( global_num( "m28b_disarmed" ) == 1 );
        CHECK( global_str( "m28b_disarmed_trap" ) == "tr_test_m28b_plate" );
        // vanilla traps have no condition
        here.trap_set( spot, tr_beartrap.id() );
        CHECK( here.tr_at( spot ).can_try_disarm( u, spot ) );
    }
    SECTION( "a dimensional anchor makes the telepad easy to take down, from its JSON" ) {
        here.trap_set( spot, tr_telepad.id() );
        const trap &pad = here.tr_at( spot );
        CHECK_FALSE( pad.easy_take_down( u, spot ) );
        u.wear_item( item( itype_dimensional_anchor_on ), false );
        REQUIRE( u.worn_with_flag( flag_DIMENSIONAL_ANCHOR ) );
        CHECK( pad.easy_take_down( u, spot ) );
    }
    SECTION( "map_trap_id" ) {
        here.trap_set( spot, tr_test_m28b_plate.id() );
        std::function<bool( const_dialogue const & )> cond;
        const JsonObject jo = json_object(
                                  R"({ "condition": { "map_trap_id": "tr_test_m28b_plate", "loc": { "context_val": "loc" } } })" );
        read_condition( jo, "condition", cond, false );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "loc", here.get_abs( spot ) );
        CHECK( cond( d ) );
        d.set_value( "loc", here.get_abs( spot + tripoint::east ) );
        CHECK_FALSE( cond( d ) );
    }
    SECTION( "a monster that sets off a trap sends monster_triggers_trap" ) {
        event_catcher events( { event_type::monster_triggers_trap, event_type::character_triggers_trap } );
        monster &mon = spawn_test_monster( "mon_test_non_shearable", spot );
        here.trap_set( spot, tr_test_m28b_plate.id() );
        here.tr_at( spot ).trigger( spot, mon );
        // its creature EOC ran
        CHECK( global_num( "m28b_a" ) == 1 );
        const std::vector<cata::event> caught = events.of( event_type::monster_triggers_trap );
        REQUIRE( caught.size() == 1 );
        CHECK( caught[0].get<cata_variant_type::trap_str_id>( "trap" ) == tr_test_m28b_plate );
        CHECK( caught[0].get<cata_variant_type::tripoint>( "pos" ) == here.get_abs( spot ).raw() );
        CHECK( events.of( event_type::character_triggers_trap ).empty() );
    }
}

// TERR-J08: the built-in actions' damage, tetanus odds, break weight and self-disarm chance were
// fixed in C++, and a bear trap set off by an item always gave back a vanilla bear trap.
TEST_CASE( "terr-j08_trap_actions_read_their_numbers_from_json", "[audit][traps]" )
{
    reset_world();
    on_out_of_scope cleanup( []() {
        reset_world();
    } );
    map &here = get_map();
    avatar &u = get_avatar();
    const tripoint_bub_ms spot = u.pos_bub() + tripoint( 2, 0, 0 );

    SECTION( "a monster caught by a bear trap takes the trap's own damage" ) {
        // pure damage: no armor, and this monster has no weak points
        monster &mon = spawn_test_monster( "mon_test_non_shearable", spot );
        here.trap_set( spot, tr_test_m28b_beartrap.id() );
        const int hp = mon.get_hp();
        here.tr_at( spot ).trigger( spot, mon );
        CHECK( mon.get_hp() == hp - 7 );
        CHECK( here.tr_at( spot ).is_null() );
    }
    SECTION( "an item that sets a bear trap off gives back the trap's drops" ) {
        here.trap_set( spot, tr_test_m28b_beartrap.id() );
        item rock( itype_rock );
        here.tr_at( spot ).trigger( spot, rock );
        CHECK( count_items( spot, itype_rock ) == 1 );
        CHECK( count_items( spot, itype_beartrap ) == 0 );
        // the vanilla one gives back a bear trap, as before
        here.i_clear( spot );
        here.trap_set( spot, tr_beartrap.id() );
        here.tr_at( spot ).trigger( spot, rock );
        CHECK( count_items( spot, itype_beartrap ) == 1 );
    }
    SECTION( "the values load, and vanilla traps keep their actions' own" ) {
        const trap &cal = tr_test_m28b_caltrops.obj();
        REQUIRE( cal.tuning.damage.size() == 1 );
        CHECK( cal.tuning.damage[0].type == damage_cut );
        CHECK( cal.tuning.damage[0].min == 1 );
        CHECK( cal.tuning.damage[0].max == 2 );
        CHECK( cal.tuning.tetanus_chance == 9 );
        CHECK( cal.tuning.tetanus_chance_resistant == 99 );
        CHECK( cal.tuning.break_weight == 50_kilogram );
        CHECK( cal.tuning.self_disarm_chance == 5 );
        const trap &vanilla = tr_caltrops.obj();
        CHECK( vanilla.tuning.damage.empty() );
        CHECK_FALSE( vanilla.tuning.tetanus_chance.has_value() );
        CHECK_FALSE( vanilla.tuning.break_weight.has_value() );
        CHECK_FALSE( vanilla.tuning.self_disarm_chance.has_value() );
    }
}

// TERR-J09: a computer option or failure could only be a hardcoded action, and mapgen "eocs"
// replaced the whole terminal.
TEST_CASE( "terr-j09_computer_options_and_failures_take_eocs", "[audit][computer]" )
{
    const std::vector<effect_on_condition_id> option_eoc = { effect_on_condition_EOC_TEST_M28B_OPTION };

    SECTION( "from JSON" ) {
        const computer_option opt = computer_option::from_json( json_object(
                                        R"({ "name": "Vent", "security": 3, "eocs": [ "EOC_TEST_M28B_OPTION" ] })" ) );
        CHECK( opt.action == COMPACT_NULL );
        CHECK( opt.security == 3 );
        CHECK( opt.eocs == option_eoc );
        const computer_option both = computer_option::from_json( json_object(
                                         R"({ "name": "Unlock", "action": "unlock", "eocs": [ "EOC_TEST_M28B_OPTION" ] })" ) );
        CHECK( both.action == COMPACT_UNLOCK );
        CHECK( both.eocs == option_eoc );
        const computer_failure fail = computer_failure::from_json( json_object(
                                          R"({ "eocs": [ "EOC_TEST_M28B_OPTION" ] })" ) );
        CHECK( fail.type == COMPFAIL_NULL );
        CHECK( fail.eocs == option_eoc );
        // neither an action nor eocs
        CHECK_THROWS_AS( computer_option::from_json( json_object( R"({ "name": "Nothing" })" ) ),
                         JsonError );
        // an inline EOC that is already defined (mapgen JSON set up again) is not added twice
        const computer_option again = computer_option::from_json( json_object(
                                          R"({ "name": "Again", "eocs": [ { "id": "EOC_TEST_M28B_OPTION", "effect": [] } ] })" ) );
        CHECK( again.eocs == option_eoc );
    }
    SECTION( "saved and loaded; older saves have none" ) {
        computer comp( "m28b", 0, tripoint_abs_ms() );
        computer_option opt( "Run", COMPACT_NULL, 0 );
        opt.eocs = option_eoc;
        comp.add_option( opt );
        computer_failure fail( COMPFAIL_NULL );
        fail.eocs = option_eoc;
        comp.add_failure( fail );
        comp.add_eoc( effect_on_condition_EOC_TEST_M28B_OPTION );
        comp.eocs_on_login = true;
        std::ostringstream os;
        JsonOut jsout( os );
        comp.serialize( jsout );
        computer loaded( "", 0, tripoint_abs_ms() );
        loaded.deserialize( json_loader::from_string( os.str() ) );
        REQUIRE( loaded.options.size() == 1 );
        CHECK( loaded.options[0].eocs == option_eoc );
        REQUIRE( loaded.failures.size() == 1 );
        CHECK( loaded.failures[0].eocs == option_eoc );
        CHECK( loaded.eocs_on_login );

        computer_option old;
        old.deserialize( json_object( R"({ "name": "Open", "action": "open", "security": 0 })" ) );
        CHECK( old.action == COMPACT_OPEN );
        CHECK( old.eocs.empty() );
        computer old_comp( "", 0, tripoint_abs_ms() );
        old_comp.deserialize( json_loader::from_string(
                                  R"({ "name": "Old", "eocs": [ "EOC_TEST_M28B_OPTION" ], "location": [ 0, 0, 0 ] })" ) );
        CHECK_FALSE( old_comp.eocs_on_login );
    }
    SECTION( "from mapgen" ) {
        clear_map();
        on_out_of_scope cleanup( []() {
            clear_map();
        } );
        map &here = get_map();
        mapgendata md( here, mapgendata::dummy_settings );
        REQUIRE( run_mapgen_update_func( update_mapgen_test_m28b_computer, md, false ).success() );
        const computer *comp = here.computer_at( tripoint_bub_ms( 3, 3, 0 ) );
        REQUIRE( comp != nullptr );
        CHECK( comp->eocs_on_login );
        REQUIRE( comp->eocs.size() == 2 );
        CHECK( comp->eocs[0] == effect_on_condition_EOC_TEST_M28B_OPTION );
        CHECK( comp->eocs[1] == effect_on_condition_EOC_TEST_M28B_INLINE_LOGIN );
        CHECK( comp->eocs[1].is_valid() );
        REQUIRE( comp->options.size() == 2 );
        CHECK( comp->options[0].action == COMPACT_NULL );
        CHECK( comp->options[0].security == 1 );
        CHECK( comp->options[0].eocs == option_eoc );
        CHECK( comp->options[1].action == COMPACT_UNLOCK );
        CHECK( comp->options[1].eocs == option_eoc );
        REQUIRE( comp->failures.size() == 2 );
        CHECK( comp->failures[0].type == COMPFAIL_NULL );
        CHECK( comp->failures[0].eocs == option_eoc );
        CHECK( comp->failures[1].type == COMPFAIL_ALARM );
        CHECK( comp->failures[1].eocs.empty() );
        const computer *plain = here.computer_at( tripoint_bub_ms( 5, 3, 0 ) );
        REQUIRE( plain != nullptr );
        CHECK_FALSE( plain->eocs_on_login );
        REQUIRE( plain->options.size() == 1 );
        CHECK( plain->options[0].eocs.empty() );
    }
}

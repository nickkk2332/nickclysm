#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "butchery.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "field_type.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_location.h"
#include "iuse.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_selector.h"
#include "material.h"
#include "math_parser_diag_value.h"
#include "monexamine.h"
#include "monfaction.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "shearing.h"
#include "stats_tracker.h"
#include "talker.h"
#include "type_id.h"

static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M20C_MON_TYPE( "EOC_AUDIT_M20C_MON_TYPE" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M20C_VAR_ATTITUDE( "EOC_AUDIT_M20C_VAR_ATTITUDE" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M20C_YRAX_HATES_MIGO( "EOC_AUDIT_M20C_YRAX_HATES_MIGO" );
static const effect_on_condition_id
effect_on_condition_EOC_AUDIT_M20C_YRAX_MIGO_DEFAULT( "EOC_AUDIT_M20C_YRAX_MIGO_DEFAULT" );

static const efftype_id effect_pet( "pet" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_riding( "riding" );

static const itype_id itype_dogfood( "dogfood" );
static const itype_id itype_knife_combat( "knife_combat" );

static const material_id material_bone( "bone" );
static const material_id material_cotton( "cotton" );
static const material_id material_flesh( "flesh" );
static const material_id material_hflesh( "hflesh" );
static const material_id material_iflesh( "iflesh" );
static const material_id material_kevlar( "kevlar" );
static const material_id material_mi_go_flesh( "mi-go_flesh" );
static const material_id material_paper( "paper" );
static const material_id material_powder( "powder" );
static const material_id material_steel( "steel" );
static const material_id material_stone( "stone" );
static const material_id material_test_m20c_chitin( "test_m20c_chitin" );
static const material_id material_veggy( "veggy" );
static const material_id material_wood( "wood" );
static const material_id material_wool( "wool" );

static const mfaction_str_id monfaction_mi_go( "mi-go" );
static const mfaction_str_id monfaction_test_m20c_migo_child( "test_m20c_migo_child" );
static const mfaction_str_id monfaction_test_m20c_yrax_child( "test_m20c_yrax_child" );
static const mfaction_str_id monfaction_test_m20c_yrax_own( "test_m20c_yrax_own" );
static const mfaction_str_id monfaction_yrax( "yrax" );

static const mtype_id mon_deer( "mon_deer" );
static const mtype_id mon_dog_thing( "mon_dog_thing" );
static const mtype_id mon_horse( "mon_horse" );
static const mtype_id mon_snail_river( "mon_snail_river" );
static const mtype_id mon_test_m20c_bloodless( "mon_test_m20c_bloodless" );
static const mtype_id mon_test_m20c_chitin( "mon_test_m20c_chitin" );
static const mtype_id mon_test_m20c_fields( "mon_test_m20c_fields" );
static const mtype_id mon_test_m20c_pet( "mon_test_m20c_pet" );

namespace
{
// What one event carried, and its beta talker
struct caught_event {
    cata::event e;
    bool beta_is_item = false;
    bool beta_is_corpse = false;
    bool beta_quartered = false;
    const monster *beta_monster = nullptr;
};

// Keeps the events of one type that go through the game's event bus while it lives
struct event_catcher : event_subscriber {
    event_type type;
    std::vector<caught_event> caught;
    explicit event_catcher( event_type t ) : type( t ) {
        get_event_bus().subscribe( this );
    }
    ~event_catcher() override {
        get_event_bus().unsubscribe( this );
    }
    event_catcher( const event_catcher & ) = delete;
    event_catcher &operator=( const event_catcher & ) = delete;
    void notify( const cata::event &e ) override {
        if( e.type() == type ) {
            caught_event c;
            c.e = e;
            caught.push_back( c );
        }
    }
    void notify( const cata::event &e, std::unique_ptr<talker>,
                 std::unique_ptr<talker> beta ) override {
        if( e.type() != type ) {
            return;
        }
        caught_event c;
        c.e = e;
        if( beta ) {
            const item_location *loc = beta->get_const_item();
            c.beta_is_item = loc != nullptr && *loc;
            if( c.beta_is_item ) {
                c.beta_is_corpse = ( *loc )->is_corpse();
                c.beta_quartered = ( *loc )->has_flag( flag_QUARTERED );
            }
            c.beta_monster = beta->get_const_monster();
        }
        caught.push_back( c );
    }
};

// Puts back the game's statistics for this event type
struct event_stats_restorer {
    event_type type;
    event_multiset saved;
    explicit event_stats_restorer( event_type t ) : type( t ), saved( get_stats().get_events( t ) ) {}
    ~event_stats_restorer() {
        get_stats().get_events( type ) = saved;
    }
    event_stats_restorer( const event_stats_restorer & ) = delete;
    event_stats_restorer &operator=( const event_stats_restorer & ) = delete;
};

// Global variables are shared by every test: put back what was there
struct globals_restorer {
    global_variables::impl_t saved = get_globals().get_global_values();
    globals_restorer() = default;
    ~globals_restorer() {
        get_globals().set_global_values( saved );
    }
    globals_restorer( const globals_restorer & ) = delete;
    globals_restorer &operator=( const globals_restorer & ) = delete;
};

mf_attitude attitude( const mfaction_str_id &from, const mfaction_str_id &to )
{
    return from.obj().attitude( to.id() );
}
} // namespace

// MONDT-J11: a monster's blood and gibs came only from hardcoded flags, materials and the
// MOLLUSK species.  blood_field and gib_field set them, and a species' gibs come before the
// materials.
TEST_CASE( "mondt-j11_blood_and_gib_fields_from_json", "[audit][monster]" )
{
    const mtype &fields = mon_test_m20c_fields.obj();
    CHECK( fields.bloodType().id() == fd_bile );
    CHECK( fields.gibType().id() == fd_gibs_veggy );

    // "fd_null" is no blood, not "work it out" (it is WARM flesh, which bleeds fd_blood)
    const mtype &bloodless = mon_test_m20c_bloodless.obj();
    CHECK( bloodless.bloodType().id() == fd_null );
    CHECK( bloodless.gibType().id() == fd_gibs_veggy );

    // flesh, but its species' gibs win; its blood is still worked out (WARM flesh)
    const mtype &pet = mon_test_m20c_pet.obj();
    CHECK( pet.gibType().id() == fd_gibs_insect );
    CHECK( pet.bloodType().id() == fd_blood );

    // MOLLUSK's gibs are data now, with the same result as the old special case
    CHECK( mon_snail_river->gibType().id() == fd_gibs_invertebrate );
}

// MONDT-J11: the creature material sets were fixed C++ lists.  They come from the materials'
// creature_flesh, breathes and creature_flammability, with the vanilla ones as before.
TEST_CASE( "mondt-j11_creature_material_sets_from_json", "[audit][monster]" )
{
    for( const material_id &m : {
             material_flesh, material_iflesh
         } ) {
        CAPTURE( m.str() );
        CHECK( Creature::cmat_flesh.count( m ) == 1 );
        CHECK( Creature::cmat_fleshnveg.count( m ) == 1 );
    }
    CHECK( Creature::cmat_fleshnveg.count( material_veggy ) == 1 );
    CHECK( Creature::cmat_flesh.count( material_veggy ) == 0 );
    // copy-from flesh, but kept out as before
    CHECK( Creature::cmat_flesh.count( material_mi_go_flesh ) == 0 );
    CHECK( Creature::cmat_fleshnveg.count( material_mi_go_flesh ) == 0 );
    CHECK( Creature::cmat_flesh.count( material_hflesh ) == 0 );
    CHECK( Creature::cmat_flesh.count( material_bone ) == 0 );

    for( const material_id &m : {
             material_paper, material_powder, material_wood, material_cotton, material_wool
         } ) {
        CAPTURE( m.str() );
        CHECK( Creature::cmat_flammable.count( m ) == 1 );
        CHECK( Creature::cmat_flameres.count( m ) == 0 );
    }
    for( const material_id &m : {
             material_stone, material_kevlar, material_steel
         } ) {
        CAPTURE( m.str() );
        CHECK( Creature::cmat_flameres.count( m ) == 1 );
        CHECK( Creature::cmat_flammable.count( m ) == 0 );
    }
    CHECK( Creature::cmat_flammable.count( material_flesh ) == 0 );
    CHECK( Creature::cmat_flameres.count( material_bone ) == 0 );

    // a modded material
    CHECK( material_test_m20c_chitin->creature_flesh() );
    CHECK_FALSE( material_test_m20c_chitin->breathes() );
    CHECK( material_test_m20c_chitin->get_creature_flammability() == creature_flammability::resistant );
    CHECK( Creature::cmat_flesh.count( material_test_m20c_chitin ) == 1 );
    CHECK( Creature::cmat_fleshnveg.count( material_test_m20c_chitin ) == 0 );
    CHECK( Creature::cmat_flameres.count( material_test_m20c_chitin ) == 1 );
    const monster bug( mon_test_m20c_chitin );
    CHECK( bug.made_of_any( Creature::cmat_flesh ) );
    CHECK( bug.made_of_any( Creature::cmat_flameres ) );
    CHECK_FALSE( bug.made_of_any( Creature::cmat_fleshnveg ) );
    CHECK_FALSE( bug.made_of_any( Creature::cmat_flammable ) );
}

// MONDT-J12: nothing could change how one monster faction treats another.
// set_monfaction_attitude does, for the factions based on it too, and it is saved.
TEST_CASE( "mondt-j12_set_monfaction_attitude", "[audit][monster][eoc]" )
{
    monfactions::clear_attitude_overrides();
    const on_out_of_scope clear_overrides( []() {
        monfactions::clear_attitude_overrides();
    } );
    const globals_restorer restore_globals;
    avatar &u = get_avatar();

    REQUIRE( attitude( monfaction_yrax, monfaction_mi_go ) == MFA_NEUTRAL );
    REQUIRE( attitude( monfaction_test_m20c_yrax_child, monfaction_mi_go ) == MFA_NEUTRAL );
    REQUIRE( attitude( monfaction_test_m20c_yrax_own, monfaction_mi_go ) == MFA_FRIENDLY );
    REQUIRE( attitude( monfaction_yrax, monfaction_test_m20c_migo_child ) == MFA_NEUTRAL );
    const mf_attitude migo_to_yrax = attitude( monfaction_mi_go, monfaction_yrax );

    dialogue d( get_talker_for( u ), nullptr );
    effect_on_condition_EOC_AUDIT_M20C_YRAX_HATES_MIGO->activate( d );
    CHECK( attitude( monfaction_yrax, monfaction_mi_go ) == MFA_HATE );
    // a faction based on yrax follows, unless its own JSON says otherwise
    CHECK( attitude( monfaction_test_m20c_yrax_child, monfaction_mi_go ) == MFA_HATE );
    CHECK( attitude( monfaction_test_m20c_yrax_own, monfaction_mi_go ) == MFA_FRIENDLY );
    // factions based on the mi-go are treated as the mi-go are
    CHECK( attitude( monfaction_yrax, monfaction_test_m20c_migo_child ) == MFA_HATE );
    // one way only
    CHECK( attitude( monfaction_mi_go, monfaction_yrax ) == migo_to_yrax );
    CHECK( monfactions::get_attitude_override( monfaction_yrax, monfaction_mi_go ) == MFA_HATE );

    SECTION( "saved with the game" ) {
        std::ostringstream os;
        JsonOut jsout( os );
        monfactions::serialize_attitude_overrides( jsout );
        monfactions::clear_attitude_overrides();
        CHECK( attitude( monfaction_yrax, monfaction_mi_go ) == MFA_NEUTRAL );
        CHECK( attitude( monfaction_test_m20c_yrax_child, monfaction_mi_go ) == MFA_NEUTRAL );
        const JsonValue jv = json_loader::from_string( os.str() );
        monfactions::deserialize_attitude_overrides( jv );
        CHECK( attitude( monfaction_yrax, monfaction_mi_go ) == MFA_HATE );
        CHECK( attitude( monfaction_test_m20c_yrax_child, monfaction_mi_go ) == MFA_HATE );
    }

    SECTION( "a save from before has none" ) {
        const JsonValue jv = json_loader::from_string( "[]" );
        monfactions::deserialize_attitude_overrides( jv );
        CHECK( attitude( monfaction_yrax, monfaction_mi_go ) == MFA_NEUTRAL );
    }

    SECTION( "DEFAULT goes back to the JSON" ) {
        effect_on_condition_EOC_AUDIT_M20C_YRAX_MIGO_DEFAULT->activate( d );
        CHECK( attitude( monfaction_yrax, monfaction_mi_go ) == MFA_NEUTRAL );
        CHECK( attitude( monfaction_test_m20c_yrax_child, monfaction_mi_go ) == MFA_NEUTRAL );
        CHECK( attitude( monfaction_mi_go, monfaction_yrax ) == migo_to_yrax );
        CHECK_FALSE( monfactions::get_attitude_override( monfaction_yrax, monfaction_mi_go ) );
    }

    SECTION( "from variables" ) {
        get_globals().set_global_value( "audit_m20c_from", std::string( "test_m20c_yrax_own" ) );
        get_globals().set_global_value( "audit_m20c_attitude", std::string( "BY_MOOD" ) );
        effect_on_condition_EOC_AUDIT_M20C_VAR_ATTITUDE->activate( d );
        CHECK( attitude( monfaction_test_m20c_yrax_own, monfaction_mi_go ) == MFA_BY_MOOD );
        CHECK( attitude( monfaction_yrax, monfaction_mi_go ) == MFA_HATE );
    }

    SECTION( "attitude names" ) {
        std::optional<mf_attitude> att;
        CHECK( monfactions::parse_attitude( "NEUTRAL", att ) );
        CHECK( att == MFA_NEUTRAL );
        CHECK( monfactions::parse_attitude( "DEFAULT", att ) );
        CHECK_FALSE( att );
        CHECK_FALSE( monfactions::parse_attitude( "ANGRY", att ) );
    }
}

// MONDT-J12: an EOC could read a monster's type only through monsters_nearby() at radius 0
TEST_CASE( "mondt-j12_monster_type_mutator", "[audit][monster][eoc]" )
{
    clear_map();
    clear_avatar();
    const globals_restorer restore_globals;
    monster &pet = spawn_test_monster( mon_test_m20c_pet.str(), get_avatar().pos_bub() +
                                       tripoint::east );
    dialogue d( get_talker_for( pet ), nullptr );
    effect_on_condition_EOC_AUDIT_M20C_MON_TYPE->activate( d );
    CHECK( get_globals().get_global_value( "audit_m20c_type" ) == mon_test_m20c_pet.str() );

    // "" for a talker that isn't a monster
    dialogue du( get_talker_for( get_avatar() ), nullptr );
    effect_on_condition_EOC_AUDIT_M20C_MON_TYPE->activate( du );
    const diag_value *type = get_globals().maybe_get_global_value( "audit_m20c_type" );
    CHECK( ( type == nullptr || *type == "" ) );
    clear_creatures();
}

// MONDT-J13: quartering sent no character_butchered_corpse, and the event had no corpse
// talker, nor a way to tell a fatal failure.
TEST_CASE( "mondt-j13_butchery_event_for_every_outcome_with_the_corpse", "[audit][butchery]" )
{
    clear_map();
    clear_avatar();
    const on_out_of_scope clean_map( []() {
        clear_map();
    } );
    const event_stats_restorer restore_stats( event_type::character_butchered_corpse );
    avatar &u = get_avatar();
    item knife( itype_knife_combat );
    u.wield( knife );
    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub();
    event_catcher events( event_type::character_butchered_corpse );

    SECTION( "quartering" ) {
        item &corpse = here.add_item( pos, item::make_corpse( mon_deer ) );
        REQUIRE( corpse.is_corpse() );
        item_location loc( map_cursor( u.pos_abs() ), &corpse );
        const butchery_data bd( loc, butcher_type::QUARTER );
        destroy_the_carcass( bd, u );
        REQUIRE( events.caught.size() == 1 );
        const caught_event &c = events.caught[0];
        CHECK( c.e.get<std::string>( "butcher_type" ) == "ACT_QUARTER" );
        CHECK( c.e.get<bool>( "success" ) );
        CHECK( c.e.get<mtype_id>( "monster_id" ) == mon_deer );
        CHECK( c.e.get<character_id>( "character" ) == u.getID() );
        // the corpse, already quartered
        CHECK( c.beta_is_corpse );
        CHECK( c.beta_quartered );
    }

    SECTION( "a butchery that uses the corpse up" ) {
        rng_set_engine_seed( 20 );
        item &corpse = here.add_item( pos, item::make_corpse( mon_deer ) );
        item_location loc( map_cursor( u.pos_abs() ), &corpse );
        butchery_data bd( loc, butcher_type::QUICK );
        butchery_activity_actor act( bd );
        act.calculate_butchery_data( u, bd );
        destroy_the_carcass( bd, u );
        // success or a fatal failure, the event has the corpse, removed afterwards
        REQUIRE( events.caught.size() == 1 );
        const caught_event &c = events.caught[0];
        CHECK( c.e.get<std::string>( "butcher_type" ) == "ACT_QUICK" );
        CHECK( c.beta_is_corpse );
        bool corpse_left = false;
        for( const item &it : here.i_at( pos ) ) {
            corpse_left |= it.is_corpse();
        }
        CHECK_FALSE( corpse_left );
    }
}

// MONDT-J14: petfood taming was instant and unconditional, and mon_dog_thing was special-cased
// by id.  petfood takes a condition, a chance, EOCs and bite_on_feed, and sends monster_tamed.
TEST_CASE( "mondt-j14_petfood_condition_chance_eocs_and_event", "[audit][monster]" )
{
    clear_map();
    clear_avatar();
    const on_out_of_scope clean_creatures( []() {
        clear_creatures();
    } );
    const globals_restorer restore_globals;
    get_globals().clear_global_values();
    const event_stats_restorer restore_stats( event_type::monster_tamed );
    avatar &u = get_avatar();
    event_catcher tamed( event_type::monster_tamed );

    CHECK( mon_dog_thing->petfood.bite_on_feed );
    CHECK_FALSE( mon_deer->petfood.bite_on_feed );

    SECTION( "a condition, EOCs and the event" ) {
        monster &pet = spawn_test_monster( mon_test_m20c_pet.str(), u.pos_bub() + tripoint::east );
        item food( itype_dogfood );
        u.wield( food );
        REQUIRE( iuse::feed_pet( u, *u.get_wielded_item(), pet ) );
        // the condition is false: it takes nothing
        CHECK( pet.friendly == 0 );
        CHECK_FALSE( pet.has_effect( effect_pet ) );
        CHECK( u.get_wielded_item() );
        CHECK( get_globals().get_global_value( "audit_m20c_failed" ) == 1 );
        CHECK( tamed.caught.empty() );

        get_globals().set_global_value( "audit_m20c_allow", 1 );
        REQUIRE( u.get_wielded_item() );
        REQUIRE( iuse::feed_pet( u, *u.get_wielded_item(), pet ) );
        CHECK( pet.friendly == -1 );
        CHECK( pet.has_effect( effect_pet ) );
        CHECK_FALSE( u.get_wielded_item() );
        CHECK( get_globals().get_global_value( "audit_m20c_tamed" ) == 1 );
        // the monster was beta
        REQUIRE( pet.maybe_get_value( "audit_m20c_owner" ) != nullptr );
        CHECK( pet.get_value( "audit_m20c_owner" ) == 1 );
        REQUIRE( tamed.caught.size() == 1 );
        CHECK( tamed.caught[0].e.get<std::string>( "method" ) == "petfood" );
        CHECK( tamed.caught[0].e.get<mtype_id>( "monster_id" ) == mon_test_m20c_pet );
        CHECK( tamed.caught[0].beta_monster == &pet );
    }

    SECTION( "a chance of 0 never tames" ) {
        monster &wary = spawn_test_monster( "mon_test_m20c_wary", u.pos_bub() + tripoint::east );
        for( int i = 0; i < 3; ++i ) {
            item food( itype_dogfood );
            u.wield( food );
            REQUIRE( iuse::feed_pet( u, *u.get_wielded_item(), wary ) );
            // the food is eaten all the same
            CHECK_FALSE( u.get_wielded_item() );
        }
        CHECK( wary.friendly == 0 );
        CHECK_FALSE( wary.has_effect( effect_pet ) );
        CHECK( get_globals().get_global_value( "audit_m20c_failed" ) == 3 );
        CHECK( tamed.caught.empty() );
    }

    SECTION( "a biter is never tamed by food" ) {
        monster &biter = spawn_test_monster( "mon_test_m20c_biter", u.pos_bub() + tripoint::east );
        item food( itype_dogfood );
        u.wield( food );
        REQUIRE( iuse::feed_pet( u, *u.get_wielded_item(), biter ) );
        CHECK( biter.friendly == 0 );
        CHECK_FALSE( biter.has_effect( effect_pet ) );
        CHECK( get_globals().get_global_value( "audit_m20c_failed" ) == 1 );
        CHECK( tamed.caught.empty() );
    }

    SECTION( "a monster that takes no pet food" ) {
        monster &deer = spawn_test_monster( mon_deer.str(), u.pos_bub() + tripoint::east );
        item food( itype_dogfood );
        u.wield( food );
        CHECK_FALSE( iuse::feed_pet( u, *u.get_wielded_item(), deer ) );
    }
}

// MONDT-J14: the pet menu was closed C++.  pet_interactions on the monster type and its species
// add entries, with a condition, run with the player as alpha and the pet as beta.
TEST_CASE( "mondt-j14_pet_interactions_from_json", "[audit][monster]" )
{
    clear_map();
    clear_avatar();
    const on_out_of_scope clean_creatures( []() {
        clear_creatures();
    } );
    const globals_restorer restore_globals;
    get_globals().clear_global_values();
    avatar &u = get_avatar();
    monster &pet = spawn_test_monster( mon_test_m20c_pet.str(), u.pos_bub() + tripoint::east );

    const auto offered_ids = [&]() {
        std::vector<std::string> ids;
        for( const pet_interaction *interaction : monexamine::offered_pet_interactions( pet, u ) ) {
            ids.push_back( interaction->id );
        }
        return ids;
    };
    // brush's condition is false; the species' brush is left out, as the type has one
    const std::vector<std::string> without_brush = { "groom", "pat" };
    CHECK( offered_ids() == without_brush );
    get_globals().set_global_value( "audit_m20c_brush_ok", 1 );
    const std::vector<std::string> with_brush = { "brush", "groom", "pat" };
    CHECK( offered_ids() == with_brush );

    const std::vector<const pet_interaction *> offered = monexamine::offered_pet_interactions( pet,
            u );
    REQUIRE( offered.size() == 3 );
    CHECK( offered[0]->key == 'B' );
    CHECK( offered[1]->key == -1 );

    monexamine::run_pet_interaction( *offered[1], pet, u );
    REQUIRE( pet.maybe_get_value( "audit_m20c_groomed" ) != nullptr );
    CHECK( pet.get_value( "audit_m20c_groomed" ) == 1 );
    REQUIRE( u.maybe_get_value( "audit_m20c_groomer" ) != nullptr );
    CHECK( u.get_value( "audit_m20c_groomer" ) == 1 );
    u.remove_value( "audit_m20c_groomer" );

    monexamine::run_pet_interaction( *offered[2], pet, u );
    CHECK( get_globals().get_global_value( "audit_m20c_patted" ) == 1 );
    CHECK( get_globals().maybe_get_global_value( "audit_m20c_species_brushed" ) == nullptr );
}

// MONDT-J14: no event told that a character mounted a monster or sheared one
TEST_CASE( "mondt-j14_mount_and_shear_events", "[audit][monster]" )
{
    clear_map();
    clear_avatar();
    clear_npcs();
    const on_out_of_scope clean( []() {
        clear_creatures();
        clear_npcs();
    } );
    avatar &u = get_avatar();

    SECTION( "mounting" ) {
        const event_stats_restorer restore_stats( event_type::character_mounts_monster );
        event_catcher mounts( event_type::character_mounts_monster );
        const tripoint_bub_ms pos = u.pos_bub();
        monster &horse = spawn_test_monster( mon_horse.str(), pos + tripoint( 3, 3, 0 ) );
        npc &rider = spawn_npc( ( pos + tripoint( 3, 2, 0 ) ).xy(), "test_talker" );
        rider.mount_creature( horse );
        REQUIRE( rider.is_mounted() );
        REQUIRE( mounts.caught.size() == 1 );
        CHECK( mounts.caught[0].e.get<character_id>( "character" ) == rider.getID() );
        CHECK( mounts.caught[0].e.get<mtype_id>( "monster_id" ) == mon_horse );
        CHECK( mounts.caught[0].beta_monster == &horse );
        // get off without dismount(), which asks where
        rider.remove_effect( effect_riding );
        horse.remove_effect( effect_ridden );
        horse.mounted_player = nullptr;
        horse.mounted_player_id = character_id();
        rider.mounted_creature = nullptr;
    }

    SECTION( "shearing" ) {
        const event_stats_restorer restore_stats( event_type::monster_sheared );
        event_catcher sheared( event_type::monster_sheared );
        monster &sheep = spawn_test_monster( "mon_test_shearable", u.pos_bub() + tripoint::east );
        REQUIRE( sheep.type->shearing.valid() );
        shearing_activity_actor actor( sheep.pos_bub(), false );
        player_activity act( actor );
        actor.finish( act, u );
        REQUIRE( sheared.caught.size() == 1 );
        CHECK( sheared.caught[0].e.get<character_id>( "character" ) == u.getID() );
        CHECK( sheared.caught[0].beta_monster == &sheep );
    }
}

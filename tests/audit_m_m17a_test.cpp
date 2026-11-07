#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cata_catch.h"
#include "character.h"
#include "character_martial_arts.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "field_type.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_iterator.h"
#include "monster.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "point.h"
#include "talker.h"
#include "type_id.h"

static const bionic_id test_bio_m17a_style_core( "test_bio_m17a_style_core" );
static const bionic_id test_bio_m17a_style_core_b( "test_bio_m17a_style_core_b" );
static const bionic_id test_bio_m17a_style_switch( "test_bio_m17a_style_switch" );
static const bionic_id test_bio_m17a_style_switch_b( "test_bio_m17a_style_switch_b" );
static const bionic_id test_bio_m17a_style_upgrade( "test_bio_m17a_style_upgrade" );
static const bionic_id test_bio_m17a_surgery( "test_bio_m17a_surgery" );
static const bionic_id test_bio_m17a_toggle( "test_bio_m17a_toggle" );

static const itype_id itype_test_bio_m17a_surgery( "test_bio_m17a_surgery" );

static const matype_id style_muay_thai( "style_muay_thai" );
static const matype_id style_none( "style_none" );

static const mtype_id mon_player_blob( "mon_player_blob" );

static const mutation_category_id mutation_category_REMOVAL_TEST( "REMOVAL_TEST" );

static const ter_str_id ter_t_wall( "t_wall" );

static const trait_id trait_ACIDBLOOD( "ACIDBLOOD" );
static const trait_id trait_BURROW( "BURROW" );
static const trait_id trait_BURROWLARGE( "BURROWLARGE" );
static const trait_id trait_ECHOLOCATION( "ECHOLOCATION" );
static const trait_id trait_GASTROPOD_EXTREMITY2( "GASTROPOD_EXTREMITY2" );
static const trait_id trait_GASTROPOD_EXTREMITY3( "GASTROPOD_EXTREMITY3" );
static const trait_id trait_GLASSJAW( "GLASSJAW" );
static const trait_id trait_LONG_TONGUE2( "LONG_TONGUE2" );
static const trait_id trait_M_BLOOM( "M_BLOOM" );
static const trait_id trait_M_FERTILE( "M_FERTILE" );
static const trait_id trait_M_PROVENANCE( "M_PROVENANCE" );
static const trait_id trait_NAUSEA( "NAUSEA" );
static const trait_id trait_SLIMESPAWNER( "SLIMESPAWNER" );
static const trait_id trait_SNAIL_TRAIL( "SNAIL_TRAIL" );
static const trait_id trait_TEST_M17A_BASE( "TEST_M17A_BASE" );
static const trait_id trait_TEST_M17A_BLOOD_O( "TEST_M17A_BLOOD_O" );
static const trait_id trait_TEST_M17A_CANCELED( "TEST_M17A_CANCELED" );
static const trait_id trait_TEST_M17A_EVOLVED( "TEST_M17A_EVOLVED" );
static const trait_id trait_TEST_M17A_FRAGILE_ARM( "TEST_M17A_FRAGILE_ARM" );
static const trait_id trait_TEST_M17A_SCAR( "TEST_M17A_SCAR" );
static const trait_id trait_TEST_M17A_SLOT_A( "TEST_M17A_SLOT_A" );
static const trait_id trait_TEST_M17A_SLOT_B( "TEST_M17A_SLOT_B" );
static const trait_id trait_TEST_M17A_SPLITTER( "TEST_M17A_SPLITTER" );
static const trait_id trait_TEST_M17A_TOGGLE( "TEST_M17A_TOGGLE" );
static const trait_id trait_TEST_M17A_WEAVER( "TEST_M17A_WEAVER" );
static const trait_id trait_THRESH_TEST_REMOVAL( "THRESH_TEST_REMOVAL" );
static const trait_id trait_TREE_COMMUNION( "TREE_COMMUNION" );
static const trait_id trait_VOMITOUS( "VOMITOUS" );
static const trait_id trait_WEB_WEAVER( "WEB_WEAVER" );

static const vitamin_id vitamin_mutagen_test_removal( "mutagen_test_removal" );

namespace
{
// Every event sent while it exists
struct m17a_event_log : public event_subscriber {
    m17a_event_log() {
        get_event_bus().subscribe( this );
    }
    ~m17a_event_log() override {
        get_event_bus().unsubscribe( this );
    }
    m17a_event_log( const m17a_event_log & ) = delete;
    m17a_event_log &operator=( const m17a_event_log & ) = delete;
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        events.push_back( e );
    }
    int count( const event_type type ) const {
        return static_cast<int>( std::count_if( events.begin(), events.end(),
        [type]( const cata::event & e ) {
            return e.type() == type;
        } ) );
    }
    int count_trait( const event_type type, const trait_id &trait ) const {
        return static_cast<int>( std::count_if( events.begin(), events.end(),
        [&]( const cata::event & e ) {
            return e.type() == type && e.get<trait_id>( "trait" ) == trait;
        } ) );
    }
    int count_bionic( const event_type type, const bionic_id &bio ) const {
        return static_cast<int>( std::count_if( events.begin(), events.end(),
        [&]( const cata::event & e ) {
            return e.type() == type && e.get<bionic_id>( "bionic" ) == bio;
        } ) );
    }
    std::vector<cata::event> events;
};
} // namespace

static talk_effect_t load_m17a_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_m_m17a_test" );
    finalize_conditions();
    return effect;
}

static void apply_m17a_effect( Character &who, const std::string &json )
{
    const talk_effect_t effect = load_m17a_effect( json );
    dialogue d( get_talker_for( who ), nullptr );
    effect.apply( d );
}

static void remove_m17a_bionic( Character &you, const bionic_id &bid )
{
    const std::optional<bionic *> bio = you.find_bionic_by_type( bid );
    REQUIRE( bio );
    you.remove_bionic( **bio );
    REQUIRE_FALSE( you.has_bionic( bid ) );
}

// MUT-J05: a bionic's martial arts styles only worked while it was on, and only for bio_cqb, so
// Xedra's Muay Thai datacore (removed by its own activation) taught nothing. learned_ma_styles
// are learned on install and, if the bionic taught them, forgotten on removal.
TEST_CASE( "mut-j05_learned_ma_styles_come_and_go_with_the_cbm",
           "[audit][bionics][martial_arts]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    REQUIRE_FALSE( you.martial_arts_data->has_martialart( style_muay_thai ) );

    SECTION( "taught by the CBM" ) {
        you.add_bionic( test_bio_m17a_style_core );
        CHECK( you.martial_arts_data->has_martialart( style_muay_thai ) );
        you.martial_arts_data->set_style( style_muay_thai );
        REQUIRE( you.martial_arts_data->selected_style() == style_muay_thai );
        remove_m17a_bionic( you, test_bio_m17a_style_core );
        CHECK_FALSE( you.martial_arts_data->has_martialart( style_muay_thai ) );
        CHECK( you.martial_arts_data->selected_style() == style_none );
    }
    SECTION( "known before the CBM" ) {
        you.martial_arts_data->add_martialart( style_muay_thai );
        you.add_bionic( test_bio_m17a_style_core );
        remove_m17a_bionic( you, test_bio_m17a_style_core );
        CHECK( you.martial_arts_data->has_martialart( style_muay_thai ) );
    }
    SECTION( "taught by two CBMs" ) {
        you.add_bionic( test_bio_m17a_style_core );
        you.add_bionic( test_bio_m17a_style_core_b );
        remove_m17a_bionic( you, test_bio_m17a_style_core );
        CHECK( you.martial_arts_data->has_martialart( style_muay_thai ) );
        remove_m17a_bionic( you, test_bio_m17a_style_core_b );
        CHECK_FALSE( you.martial_arts_data->has_martialart( style_muay_thai ) );
    }
    SECTION( "taken over by an upgrade" ) {
        you.add_bionic( test_bio_m17a_style_core );
        apply_m17a_effect( you, R"({ "effect": { "u_add_bionic": "test_bio_m17a_style_upgrade", )"
                           R"("as_surgery": true } })" );
        CHECK_FALSE( you.has_bionic( test_bio_m17a_style_core ) );
        REQUIRE( you.has_bionic( test_bio_m17a_style_upgrade ) );
        CHECK( you.martial_arts_data->has_martialart( style_muay_thai ) );
        remove_m17a_bionic( you, test_bio_m17a_style_upgrade );
        CHECK_FALSE( you.martial_arts_data->has_martialart( style_muay_thai ) );
    }
    clear_avatar();
}

// MUT-J05: only bio_cqb dropped its style when switched off; another bionic with known_ma_styles
// left the character fighting in a style it no longer knew.
TEST_CASE( "mut-j05_switching_off_a_style_cbm_drops_its_style",
           "[audit][bionics][martial_arts]" )
{
    clear_avatar();
    // An NPC: switching one on doesn't open the avatar's style menu.
    standard_npc guy( "Fighter", get_avatar().pos_bub() + point::east );
    clear_character( guy );
    const bionic_uid uid = guy.add_bionic( test_bio_m17a_style_switch );
    const auto the_cbm = [&]() -> bionic & {
        const std::optional<bionic *> bio = guy.find_bionic_by_uid( uid );
        REQUIRE( bio );
        return * *bio;
    };
    REQUIRE( guy.activate_bionic( the_cbm() ) );
    REQUIRE( the_cbm().powered );
    guy.martial_arts_data->set_style( style_muay_thai, true );
    REQUIRE( guy.martial_arts_data->selected_style() == style_muay_thai );

    SECTION( "the character doesn't know it otherwise" ) {
        REQUIRE( guy.deactivate_bionic( the_cbm() ) );
        CHECK( guy.martial_arts_data->selected_style() == style_none );
    }
    SECTION( "the character knows it" ) {
        guy.martial_arts_data->add_martialart( style_muay_thai );
        REQUIRE( guy.deactivate_bionic( the_cbm() ) );
        CHECK( guy.martial_arts_data->selected_style() == style_muay_thai );
    }
    SECTION( "another active CBM knows it" ) {
        const bionic_uid other_uid = guy.add_bionic( test_bio_m17a_style_switch_b );
        REQUIRE( guy.activate_bionic( **guy.find_bionic_by_uid( other_uid ) ) );
        REQUIRE( guy.deactivate_bionic( the_cbm() ) );
        CHECK( guy.martial_arts_data->selected_style() == style_muay_thai );
    }
    clear_avatar();
}

// MUT-J06: the built-in activations, blood type and head HP were keyed to trait ids in C++;
// the shipped traits now name them in JSON and must keep them.
TEST_CASE( "mut-j06_shipped_traits_keep_their_builtin_behaviour", "[audit][mutation]" )
{
    using mba = mutation_builtin_activation;
    CHECK( trait_WEB_WEAVER->builtin_activation == mba::web );
    CHECK( trait_SNAIL_TRAIL->builtin_activation == mba::sludge_trail );
    CHECK( trait_LONG_TONGUE2->builtin_activation == mba::long_pull );
    CHECK( trait_GASTROPOD_EXTREMITY2->builtin_activation == mba::long_pull );
    CHECK( trait_GASTROPOD_EXTREMITY3->builtin_activation == mba::long_pull );
    CHECK( trait_BURROW->builtin_activation == mba::burrow );
    CHECK( trait_BURROWLARGE->builtin_activation == mba::burrow );
    CHECK( trait_SLIMESPAWNER->builtin_activation == mba::slime_split );
    CHECK( trait_NAUSEA->builtin_activation == mba::vomit );
    CHECK( trait_VOMITOUS->builtin_activation == mba::vomit );
    CHECK( trait_M_FERTILE->builtin_activation == mba::spores );
    CHECK( trait_M_BLOOM->builtin_activation == mba::blossoms );
    CHECK( trait_M_PROVENANCE->builtin_activation == mba::spores_and_blossoms );
    CHECK( trait_ECHOLOCATION->builtin_activation == mba::echolocation );
    CHECK( trait_TREE_COMMUNION->builtin_activation == mba::tree_communion );
    REQUIRE( trait_ACIDBLOOD->sets_blood_type );
    CHECK( *trait_ACIDBLOOD->sets_blood_type == blood_type::blood_acid );
    REQUIRE( trait_GLASSJAW->bodypart_hp_multiplier.count( body_part_head ) == 1 );
    CHECK( trait_GLASSJAW->bodypart_hp_multiplier.at( body_part_head ) == Approx( 0.8 ) );
    CHECK( trait_GLASSJAW->bodypart_hp_multiplier.size() == 1 );
}

// The friendly slime microbians next to the character
static int m17a_slimes_around( const Character &you )
{
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    int slimes = 0;
    for( const tripoint_bub_ms &p : here.points_in_radius( you.pos_bub(), 1 ) ) {
        const monster *const mon = creatures.creature_at<monster>( p );
        if( mon && mon->type->id == mon_player_blob && mon->friendly != 0 ) {
            ++slimes;
        }
    }
    return slimes;
}

// MUT-J06: a modded trait can split off a slime as SLIMESPAWNER does; with no room for it, the
// thirst the activation cost used to be kept.
TEST_CASE( "mut-j06_modded_trait_splits_off_a_slime", "[audit][mutation]" )
{
    clear_map();
    clear_creatures();
    clear_avatar();
    avatar &you = get_avatar();
    you.set_mutation( trait_TEST_M17A_SPLITTER );
    you.set_thirst( 0 );

    SECTION( "with room" ) {
        you.activate_mutation( trait_TEST_M17A_SPLITTER );
        CHECK( m17a_slimes_around( you ) == 1 );
        CHECK( you.get_thirst() == 50 );
    }
    SECTION( "hemmed in" ) {
        map &here = get_map();
        for( const tripoint_bub_ms &p : here.points_in_radius( you.pos_bub(), 1 ) ) {
            if( p != you.pos_bub() ) {
                here.ter_set( p, ter_t_wall.id() );
            }
        }
        you.activate_mutation( trait_TEST_M17A_SPLITTER );
        CHECK( m17a_slimes_around( you ) == 0 );
        CHECK( you.get_thirst() == 0 );
        CHECK_FALSE( you.has_active_mutation( trait_TEST_M17A_SPLITTER ) );
    }
    clear_creatures();
    clear_avatar();
    clear_map();
}

// MUT-J06: a modded trait can weave webs as WEB_WEAVER does, while it is on.
TEST_CASE( "mut-j06_modded_trait_weaves_webs", "[audit][mutation]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = you.pos_bub();
    you.set_mutation( trait_TEST_M17A_WEAVER );
    REQUIRE( here.get_field( spot, fd_web.id() ) == nullptr );
    REQUIRE_FALSE( you.has_active_builtin_activation( mutation_builtin_activation::web ) );

    you.activate_mutation( trait_TEST_M17A_WEAVER );
    CHECK( here.get_field( spot, fd_web.id() ) != nullptr );
    CHECK( you.has_active_builtin_activation( mutation_builtin_activation::web ) );
    CHECK_FALSE( you.has_active_builtin_activation( mutation_builtin_activation::sludge_trail ) );

    you.deactivate_mutation( trait_TEST_M17A_WEAVER );
    CHECK_FALSE( you.has_active_builtin_activation( mutation_builtin_activation::web ) );
    here.remove_field( spot, fd_web.id() );
    clear_avatar();
    clear_map();
}

// MUT-J06: ACIDBLOOD's blood type was keyed to its id; a trait's "blood_type" now sets it, and
// losing it gives another trait's blood type or a random natural one.
TEST_CASE( "mut-j06_blood_type_follows_the_traits", "[audit][mutation]" )
{
    clear_avatar();
    avatar &you = get_avatar();

    you.set_mutation( trait_ACIDBLOOD );
    CHECK( you.my_blood_type == blood_type::blood_acid );

    you.set_mutation( trait_TEST_M17A_BLOOD_O );
    CHECK( you.my_blood_type == blood_type::blood_O );
    you.unset_mutation( trait_TEST_M17A_BLOOD_O );
    CHECK( you.my_blood_type == blood_type::blood_acid );

    you.unset_mutation( trait_ACIDBLOOD );
    CHECK( you.my_blood_type != blood_type::blood_acid );
    clear_avatar();
}

// MUT-J06: GLASSJAW's lower head HP was keyed to its id; "bodypart_hp_multiplier" does it for
// any trait and body part.
TEST_CASE( "mut-j06_bodypart_hp_multiplier_lowers_that_part", "[audit][mutation]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    const int head = you.get_part_hp_max( body_part_head );
    const int arm = you.get_part_hp_max( body_part_arm_l );
    const int torso = you.get_part_hp_max( body_part_torso );
    REQUIRE( head > 1 );
    REQUIRE( arm > 1 );

    you.set_mutation( trait_GLASSJAW );
    CHECK( you.get_part_hp_max( body_part_head ) == static_cast<int>( head * 0.8 ) );
    CHECK( you.get_part_hp_max( body_part_torso ) == torso );

    you.set_mutation( trait_TEST_M17A_FRAGILE_ARM );
    CHECK( you.get_part_hp_max( body_part_arm_l ) == static_cast<int>( arm * 0.5 ) );
    CHECK( you.get_part_hp_max( body_part_head ) == static_cast<int>( head * 0.8 ) );

    you.unset_mutation( trait_GLASSJAW );
    you.unset_mutation( trait_TEST_M17A_FRAGILE_ARM );
    CHECK( you.get_part_hp_max( body_part_head ) == head );
    CHECK( you.get_part_hp_max( body_part_arm_l ) == arm );
    clear_avatar();
}

// MUT-J07: gains_mutation was only sent for a new trait, never for one evolved from another,
// so an EOC waiting for an evolved trait never ran.
TEST_CASE( "mut-j07_evolving_a_trait_sends_gains_mutation", "[audit][mutation][event]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    m17a_event_log log;

    SECTION( "a new trait" ) {
        REQUIRE( you.mutate_towards( trait_TEST_M17A_SLOT_A ) );
        CHECK( log.count_trait( event_type::gains_mutation, trait_TEST_M17A_SLOT_A ) == 1 );
    }
    SECTION( "an evolution" ) {
        you.set_mutation( trait_TEST_M17A_BASE );
        REQUIRE( you.mutate_towards( trait_TEST_M17A_EVOLVED ) );
        REQUIRE( you.has_trait( trait_TEST_M17A_EVOLVED ) );
        REQUIRE_FALSE( you.has_trait( trait_TEST_M17A_BASE ) );
        CHECK( log.count( event_type::evolves_mutation ) == 1 );
        CHECK( log.count_trait( event_type::loses_mutation, trait_TEST_M17A_BASE ) == 1 );
        CHECK( log.count_trait( event_type::gains_mutation, trait_TEST_M17A_EVOLVED ) == 1 );
    }
    clear_avatar();
}

// MUT-J07: crossing a threshold sent no gains_mutation for the threshold trait.
TEST_CASE( "mut-j07_crossing_a_threshold_sends_gains_mutation", "[audit][mutation][event]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    you.mutation_category_level[mutation_category_REMOVAL_TEST] = 30;
    you.vitamin_set( vitamin_mutagen_test_removal, 1000 );
    m17a_event_log log;

    you.cross_threshold( mutation_category_REMOVAL_TEST );
    REQUIRE( you.has_trait( trait_THRESH_TEST_REMOVAL ) );
    CHECK( log.count_trait( event_type::gains_mutation, trait_THRESH_TEST_REMOVAL ) == 1 );
    CHECK( log.count( event_type::crosses_mutation_threshold ) == 1 );
    clear_avatar();
}

// MUT-J07: u_add_trait and u_lose_trait never sent the mutation events; with "send_event" they
// do, and without it they still don't (a haircut isn't a mutation).
TEST_CASE( "mut-j07_trait_effects_send_events_when_asked", "[audit][mutation][event][eoc]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    m17a_event_log log;

    SECTION( "without send_event" ) {
        apply_m17a_effect( you, R"({ "effect": { "u_add_trait": "TEST_M17A_SLOT_A" } })" );
        REQUIRE( you.has_trait( trait_TEST_M17A_SLOT_A ) );
        apply_m17a_effect( you, R"({ "effect": { "u_lose_trait": "TEST_M17A_SLOT_A" } })" );
        REQUIRE_FALSE( you.has_trait( trait_TEST_M17A_SLOT_A ) );
        CHECK( log.count( event_type::gains_mutation ) == 0 );
        CHECK( log.count( event_type::loses_mutation ) == 0 );
    }
    SECTION( "with send_event" ) {
        apply_m17a_effect( you,
                           R"({ "effect": { "u_add_trait": "TEST_M17A_SLOT_A", "send_event": true } })" );
        REQUIRE( you.has_trait( trait_TEST_M17A_SLOT_A ) );
        CHECK( log.count_trait( event_type::gains_mutation, trait_TEST_M17A_SLOT_A ) == 1 );
        // Already had: nothing gained
        apply_m17a_effect( you,
                           R"({ "effect": { "u_add_trait": "TEST_M17A_SLOT_A", "send_event": true } })" );
        CHECK( log.count( event_type::gains_mutation ) == 1 );
        // Takes the slot of the first one
        apply_m17a_effect( you,
                           R"({ "effect": { "u_add_trait": "TEST_M17A_SLOT_B", "send_event": true } })" );
        REQUIRE( you.has_trait( trait_TEST_M17A_SLOT_B ) );
        REQUIRE_FALSE( you.has_trait( trait_TEST_M17A_SLOT_A ) );
        CHECK( log.count_trait( event_type::loses_mutation, trait_TEST_M17A_SLOT_A ) == 1 );
        CHECK( log.count_trait( event_type::gains_mutation, trait_TEST_M17A_SLOT_B ) == 1 );

        apply_m17a_effect( you,
                           R"({ "effect": { "u_lose_trait": "TEST_M17A_SLOT_B", "send_event": true } })" );
        REQUIRE_FALSE( you.has_trait( trait_TEST_M17A_SLOT_B ) );
        CHECK( log.count_trait( event_type::loses_mutation, trait_TEST_M17A_SLOT_B ) == 1 );
        // Not had: nothing lost
        apply_m17a_effect( you,
                           R"({ "effect": { "u_lose_trait": "TEST_M17A_SLOT_B", "send_event": true } })" );
        CHECK( log.count( event_type::loses_mutation ) == 2 );
    }
    clear_avatar();
}

// MUT-J07: u_add_bionic and u_lose_bionic skipped what an operation does; "as_surgery" does it.
TEST_CASE( "mut-j07_bionic_effects_can_work_as_surgery", "[audit][bionics][event][eoc]" )
{
    clear_map();
    clear_avatar();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms spot = you.pos_bub();
    const auto cbms_on_the_ground = [&]() {
        int found = 0;
        for( const item &it : here.i_at( spot ) ) {
            if( it.typeId() == itype_test_bio_m17a_surgery ) {
                ++found;
            }
        }
        return found;
    };
    you.set_mutation( trait_TEST_M17A_CANCELED );
    m17a_event_log log;

    SECTION( "plain" ) {
        apply_m17a_effect( you, R"({ "effect": { "u_add_bionic": "test_bio_m17a_surgery" } })" );
        REQUIRE( you.has_bionic( test_bio_m17a_surgery ) );
        CHECK( you.has_trait( trait_TEST_M17A_CANCELED ) );
        CHECK( log.count( event_type::installs_cbm ) == 0 );

        apply_m17a_effect( you, R"({ "effect": { "u_lose_bionic": "test_bio_m17a_surgery" } })" );
        REQUIRE_FALSE( you.has_bionic( test_bio_m17a_surgery ) );
        CHECK_FALSE( you.has_trait( trait_TEST_M17A_SCAR ) );
        CHECK( log.count( event_type::removes_cbm ) == 0 );
        CHECK( cbms_on_the_ground() == 0 );
    }
    SECTION( "as surgery" ) {
        apply_m17a_effect( you, R"({ "effect": { "u_add_bionic": "test_bio_m17a_surgery", )"
                           R"("as_surgery": true } })" );
        REQUIRE( you.has_bionic( test_bio_m17a_surgery ) );
        CHECK_FALSE( you.has_trait( trait_TEST_M17A_CANCELED ) );
        CHECK( log.count_bionic( event_type::installs_cbm, test_bio_m17a_surgery ) == 1 );

        apply_m17a_effect( you, R"({ "effect": { "u_lose_bionic": "test_bio_m17a_surgery", )"
                           R"("as_surgery": true } })" );
        REQUIRE_FALSE( you.has_bionic( test_bio_m17a_surgery ) );
        CHECK( you.has_trait( trait_TEST_M17A_SCAR ) );
        CHECK( log.count_bionic( event_type::removes_cbm, test_bio_m17a_surgery ) == 1 );
        CHECK( cbms_on_the_ground() == 1 );
    }
    here.i_clear( spot );
    clear_avatar();
    clear_map();
}

// MUT-J07: there was no event for switching a bionic or a mutation on or off.
TEST_CASE( "mut-j07_switching_bionics_and_traits_sends_events",
           "[audit][bionics][mutation][event]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    m17a_event_log log;

    SECTION( "a bionic" ) {
        const bionic_uid uid = you.add_bionic( test_bio_m17a_toggle );
        REQUIRE( you.activate_bionic( **you.find_bionic_by_uid( uid ) ) );
        CHECK( log.count_bionic( event_type::activates_bionic, test_bio_m17a_toggle ) == 1 );
        CHECK( log.count( event_type::deactivates_bionic ) == 0 );
        REQUIRE( you.deactivate_bionic( **you.find_bionic_by_uid( uid ) ) );
        CHECK( log.count_bionic( event_type::deactivates_bionic, test_bio_m17a_toggle ) == 1 );
        CHECK( log.count( event_type::activates_bionic ) == 1 );
    }
    SECTION( "a trait" ) {
        you.set_mutation( trait_TEST_M17A_TOGGLE );
        you.activate_mutation( trait_TEST_M17A_TOGGLE );
        REQUIRE( you.has_active_mutation( trait_TEST_M17A_TOGGLE ) );
        CHECK( log.count_trait( event_type::activates_mutation, trait_TEST_M17A_TOGGLE ) == 1 );
        CHECK( log.count( event_type::deactivates_mutation ) == 0 );
        you.deactivate_mutation( trait_TEST_M17A_TOGGLE );
        REQUIRE_FALSE( you.has_active_mutation( trait_TEST_M17A_TOGGLE ) );
        CHECK( log.count_trait( event_type::deactivates_mutation, trait_TEST_M17A_TOGGLE ) == 1 );
    }
    SECTION( "a trait the character doesn't have" ) {
        you.activate_mutation( trait_TEST_M17A_TOGGLE );
        you.deactivate_mutation( trait_TEST_M17A_TOGGLE );
        CHECK( log.count( event_type::activates_mutation ) == 0 );
        CHECK( log.count( event_type::deactivates_mutation ) == 0 );
    }
    clear_avatar();
}

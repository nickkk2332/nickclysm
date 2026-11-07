#include <functional>
#include <memory>
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
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_source.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "morale_types.h"
#include "options.h"
#include "player_helpers.h"
#include "point.h"
#include "stomach.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "vitamin.h"
#include "wound.h"

static const efftype_id effect_adrenaline( "adrenaline" );
static const efftype_id effect_audit_m15a_flag_b( "audit_m15a_flag_b" );
static const efftype_id effect_audit_m15a_symptom( "audit_m15a_symptom" );
static const efftype_id effect_audit_m15a_ticking( "audit_m15a_ticking" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_sleep( "sleep" );

static const json_character_flag json_flag_AUDIT_M15A_B( "AUDIT_M15A_B" );

static const morale_type morale_audit_m15a_perm( "audit_m15a_perm" );
static const morale_type morale_audit_m15a_temp( "audit_m15a_temp" );
static const morale_type morale_morale_perm_optimist( "morale_perm_optimist" );
static const morale_type morale_morale_wet( "morale_wet" );

static const wound_type_id wound_audit_m15a_bleeds( "audit_m15a_bleeds" );
static const wound_type_id wound_audit_m15a_dry( "audit_m15a_dry" );
static const wound_type_id wound_audit_m15a_heals( "audit_m15a_heals" );
static const wound_type_id wound_audit_m15a_slow( "audit_m15a_slow" );
static const wound_type_id wound_audit_m15a_whitelisted( "audit_m15a_whitelisted" );
static const wound_type_id wound_audit_m15a_worse( "audit_m15a_worse" );
static const wound_type_id wound_audit_m15a_worsens( "audit_m15a_worsens" );
static const wound_type_id wound_bleeding_cut_medium( "bleeding_cut_medium" );

namespace
{
// A creature's numeric variable, 0 when it has none
double var_of( const Creature &c, const std::string &key )
{
    const diag_value *v = c.maybe_get_value( key );
    return v ? v->dbl() : 0.0;
}

double eval_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    return exp.eval( d );
}

// An assignment, "f() = x"
void assign_math( dialogue &d, const std::string_view expr )
{
    math_exp exp;
    REQUIRE( exp.parse( expr ) );
    exp.eval( d );
}

talk_effect_t load_effect( const std::string &json )
{
    const JsonObject jo = json_loader::from_string( json );
    talk_effect_t effect( jo, "effect", "audit_m_m15a_test" );
    // Math is parsed after loading; outside data loading that has to be done by hand.
    finalize_conditions();
    return effect;
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

// Sets an external option for the test and puts it back (override_option can't, for these)
class override_external_int_option
{
    public:
        override_external_int_option( const std::string &name, const int value ) : name( name ),
            old_value( get_option<int>( name ) ) {
            set( value );
        }
        override_external_int_option( const override_external_int_option & ) = delete;
        override_external_int_option &operator=( const override_external_int_option & ) = delete;
        ~override_external_int_option() {
            set( old_value );
        }
    private:
        void set( const int value ) const {
            get_options().get_option( name ).setValue( value );
            options_manager::update_options_cache();
        }
        std::string name;
        int old_value;
};

// Runs update_wounds one second at a time until the wound is gone, at most `limit` times
int seconds_until_gone( Character &u, const bodypart_id &bp, const wound_type_id &wd,
                        const int limit )
{
    for( int i = 1; i <= limit; ++i ) {
        u.update_wounds( 1_turns );
        if( !u.has_wound( wd, bp.id() ) ) {
            return i;
        }
    }
    return -1;
}
} // namespace

// HLTH-J04: wounds sent no events, so EOCs couldn't react to a wound being gained, getting
// worse or healing.
TEST_CASE( "hlth-j04_wound_events", "[audit][wound][event]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    event_catcher events( { event_type::character_gains_wound, event_type::character_wound_progresses,
                            event_type::character_loses_wound } );
    const bodypart_id arm_l( body_part_arm_l );

    SECTION( "gained, then healed by itself" ) {
        u.apply_wound( arm_l, wound_audit_m15a_heals );
        const std::vector<cata::event> gained = events.of( event_type::character_gains_wound );
        REQUIRE( gained.size() == 1 );
        CHECK( gained[0].get<cata_variant_type::character_id>( "character" ) == u.getID() );
        CHECK( gained[0].get<cata_variant_type::body_part>( "bodypart" ) == arm_l );
        CHECK( gained[0].get<cata_variant_type::string>( "wound" ) == "audit_m15a_heals" );

        // 3 seconds to heal at a lifestyle of 0, faster or slower with a better or worse one
        CHECK( seconds_until_gone( u, arm_l, wound_audit_m15a_heals, 30 ) > 0 );
        const std::vector<cata::event> lost = events.of( event_type::character_loses_wound );
        REQUIRE( lost.size() == 1 );
        CHECK( lost[0].get<cata_variant_type::string>( "wound" ) == "audit_m15a_heals" );
        CHECK( lost[0].get<cata_variant_type::bool_>( "healed" ) );
    }

    SECTION( "progressed by its timer, then removed" ) {
        u.apply_wound( arm_l, wound_audit_m15a_worsens );
        CHECK( seconds_until_gone( u, arm_l, wound_audit_m15a_worsens, 50 ) > 0 );
        CHECK( u.has_wound( wound_audit_m15a_worse, arm_l.id() ) );
        const std::vector<cata::event> worse = events.of( event_type::character_wound_progresses );
        REQUIRE( worse.size() == 1 );
        CHECK( worse[0].get<cata_variant_type::string>( "wound" ) == "audit_m15a_worsens" );
        CHECK( worse[0].get<cata_variant_type::string>( "new_wound" ) == "audit_m15a_worse" );
        CHECK( events.of( event_type::character_loses_wound ).empty() );

        // Removed (treated, or by an EOC), not healed
        u.remove_wounds_of_type( arm_l, wound_audit_m15a_worse );
        CHECK_FALSE( u.has_wound( wound_audit_m15a_worse ) );
        const std::vector<cata::event> lost = events.of( event_type::character_loses_wound );
        REQUIRE( lost.size() == 1 );
        CHECK( lost[0].get<cata_variant_type::string>( "wound" ) == "audit_m15a_worse" );
        CHECK_FALSE( lost[0].get<cata_variant_type::bool_>( "healed" ) );
    }

    SECTION( "u_add_wound and u_remove_wound send them too" ) {
        const talk_effect_t add = load_effect(
                                      R"({ "effect": { "u_add_wound": "arm_l", "wound_id": "audit_m15a_slow" } })" );
        const talk_effect_t remove = load_effect(
                                         R"({ "effect": { "u_remove_wound": "arm_l", "wound_id": [ "audit_m15a_slow" ] } })" );
        dialogue d( get_talker_for( u ), nullptr );
        add.apply( d );
        CHECK( u.has_wound( wound_audit_m15a_slow, arm_l.id() ) );
        CHECK( events.of( event_type::character_gains_wound ).size() == 1 );
        remove.apply( d );
        CHECK_FALSE( u.has_wound( wound_audit_m15a_slow ) );
        CHECK( events.of( event_type::character_loses_wound ).size() == 1 );
    }
}

// HLTH-J04: whitelist_flags needed every flag (documented as any of them), whether a wound bleeds
// was decided only by its blood vitamins, and nothing a wound refers to was checked at load.
TEST_CASE( "hlth-j04_wound_flags_bleeding_and_load_checks", "[audit][wound]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );

    // One of the two flags is enough
    CHECK_FALSE( wound_audit_m15a_whitelisted->allowed_by_whitelist_flags( u ) );
    u.add_effect( effect_audit_m15a_flag_b, 1_hours );
    REQUIRE( u.has_flag( json_flag_AUDIT_M15A_B ) );
    CHECK( wound_audit_m15a_whitelisted->allowed_by_whitelist_flags( u ) );
    // No whitelist: anyone
    CHECK( wound_audit_m15a_heals->allowed_by_whitelist_flags( u ) );

    // "bleeding" overrides the vitamins; without it, they decide as before
    CHECK( wound_audit_m15a_bleeds->is_bleeding_wound() );
    CHECK_FALSE( wound_audit_m15a_dry->is_bleeding_wound() );
    CHECK_FALSE( wound_audit_m15a_heals->is_bleeding_wound() );
    CHECK( wound_bleeding_cut_medium->is_bleeding_wound() );

    // Built inline (not in TEST_DATA, which loads with every run): every bad reference reported
    const JsonObject jo = json_loader::from_string( R"({
        "name": { "str_sp": "broken wound" },
        "description": "Refers to things that don't exist.",
        "damage_types": [ "audit_m15a_no_such_damage" ],
        "damage_required": [ 10, 1 ],
        "limb_scores": [ { "score": "audit_m15a_no_such_score", "value": 0.5 } ],
        "mod_values": [
          { "type": "VITAMIN", "vitamin_id": "audit_m15a_no_such_vitamin", "rate": "1 seconds", "amount": -1 },
          { "type": "VITAMIN", "rate": "1 seconds", "amount": -1 },
          { "type": "PAIN", "vitamin_id": "blood", "rate": "1 seconds", "amount": 1 }
        ],
        "wound_progression": [ { "id": "audit_m15a_no_such_wound", "chance": 10 } ],
        "progress_timer": { "id": "audit_m15a_no_such_timer_wound", "time": [ "1 seconds", "2 seconds" ] }
    })" );
    wound_type broken;
    broken.id = wound_type_id( "audit_m15a_broken" );
    broken.load( jo, "audit_m_m15a_test" );
    const std::string messages = capture_debugmsg_during( [&broken]() {
        broken.check();
    } );
    CAPTURE( messages );
    CHECK( messages.find( "audit_m15a_no_such_damage" ) != std::string::npos );
    CHECK( messages.find( "damage_required [10, 1]" ) != std::string::npos );
    CHECK( messages.find( "audit_m15a_no_such_score" ) != std::string::npos );
    CHECK( messages.find( "audit_m15a_no_such_vitamin" ) != std::string::npos );
    CHECK( messages.find( "without vitamin_id" ) != std::string::npos );
    CHECK( messages.find( "vitamin_id blood" ) != std::string::npos );
    CHECK( messages.find( "audit_m15a_no_such_wound" ) != std::string::npos );
    CHECK( messages.find( "audit_m15a_no_such_timer_wound" ) != std::string::npos );

    // The shipped and test wounds are clean
    for( const wound_type &wt : wound_type::get_all() ) {
        CAPTURE( wt.id.str() );
        CHECK( capture_debugmsg_during( [&wt]() {
            wt.check();
        } ).empty() );
    }
}

// HLTH-J05: a vitamin's decays_into, its min and max and its disease ranges weren't checked.
TEST_CASE( "hlth-j05_vitamin_load_checks", "[audit][vitamin]" )
{
    const JsonObject jo = json_loader::from_string( R"({
        "vit_type": "counter",
        "min": 10,
        "max": 5,
        "rate": "1 h",
        "decays_into": [ [ "audit_m15a_no_such_vitamin", 1 ], [ "audit_m15a_self", 2 ] ],
        "disease_excess": [ [ 100, 200 ] ]
    })" );
    vitamin broken;
    broken.id = vitamin_id( "audit_m15a_self" );
    broken.load( jo, "audit_m_m15a_test" );
    const std::string messages = capture_debugmsg_during( [&broken]() {
        broken.check();
    } );
    CAPTURE( messages );
    CHECK( messages.find( "min (10) above its max (5)" ) != std::string::npos );
    CHECK( messages.find( "unknown vitamin audit_m15a_no_such_vitamin" ) != std::string::npos );
    CHECK( messages.find( "decays into itself" ) != std::string::npos );
    CHECK( messages.find( "can never be reached" ) != std::string::npos );

    // Every loaded vitamin passes (the beta blocker's unreachable intensities are gone)
    for( const std::pair<const vitamin_id, vitamin> &v : vitamin::all() ) {
        CAPTURE( v.first.str() );
        CHECK( capture_debugmsg_during( [&v]() {
            v.second.check();
        } ).empty() );
    }
}

// HLTH-J06: the overdoses that kill outright had their lethal points fixed in the code.
TEST_CASE( "hlth-j06_lethal_overdoses_are_external_options", "[audit][character]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    REQUIRE( get_option<int>( "OVERDOSE_STIM_HIGH" ) == 250 );
    REQUIRE( get_option<int>( "OVERDOSE_ADRENALINE_MINUTES" ) == 50 );
    REQUIRE( get_option<int>( "OVERDOSE_DRUNK_INTENSITY" ) == 5 );

    SECTION( "a stimulant level above the option's" ) {
        u.set_stim( 260 );
        {
            override_external_int_option high( "OVERDOSE_STIM_HIGH", 300 );
            u.check_needs_extremes();
            CHECK( u.get_part_hp_cur( body_part_torso ) > 0 );
        }
        // The default, 250, as before
        u.check_needs_extremes();
        CHECK( u.get_part_hp_cur( body_part_torso ) == 0 );
    }

    SECTION( "adrenaline, with the death turned off and then on" ) {
        u.add_effect( effect_adrenaline, 60_minutes );
        REQUIRE( u.get_effect_dur( effect_adrenaline ) > 50_minutes );
        {
            override_external_int_option off( "OVERDOSE_ADRENALINE_MINUTES", 0 );
            u.check_needs_extremes();
            CHECK( u.get_part_hp_cur( body_part_torso ) > 0 );
        }
        u.check_needs_extremes();
        CHECK( u.get_part_hp_cur( body_part_torso ) == 0 );
    }
}

// HLTH-J07: an effect type couldn't run EOCs on a schedule; content polled with recurring EOCs.
TEST_CASE( "hlth-j07_effect_tick_eocs", "[audit][effect]" )
{
    REQUIRE( effect_audit_m15a_ticking->tick_eocs.size() == 1 );
    REQUIRE( effect_audit_m15a_ticking->tick_eocs_interval == 3_turns );
    clear_avatar();
    clear_map();
    restore_on_out_of_scope restore_turn( calendar::turn );
    calendar::turn = calendar::turn_zero + 1_days;
    avatar &u = get_avatar();
    monster &zed = spawn_test_monster( "mon_zombie", u.pos_bub() + point( 5, 0 ) );
    on_out_of_scope cleanup( []() {
        clear_creatures();
        clear_avatar();
    } );

    zed.add_effect( effect_source::empty(), effect_audit_m15a_ticking, 1_hours, false, 1 );
    // Every 3 turns since it began: turns 3, 6 and 9 of 10
    for( int i = 0; i < 10; ++i ) {
        calendar::turn += 1_turns;
        zed.process_effects();
    }
    CHECK( var_of( zed, "audit_m15a_ticks" ) == 3 );
    CHECK( var_of( zed, "audit_m15a_tick_intensity" ) == 1 );

    // Once it's gone, no more
    zed.remove_effect( effect_audit_m15a_ticking );
    for( int i = 0; i < 6; ++i ) {
        calendar::turn += 1_turns;
        zed.process_effects();
    }
    CHECK( var_of( zed, "audit_m15a_ticks" ) == 3 );
}

// HLTH-J08: a morale type's "permanent" was loaded but never read: only 8 ids hardcoded in C++
// could be given permanently, and no math read one morale type's value.
TEST_CASE( "hlth-j08_permanent_morale_types_from_json", "[audit][morale]" )
{
    CHECK( morale_morale_perm_optimist->is_permanent() );
    CHECK_FALSE( morale_morale_wet->is_permanent() );

    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), nullptr );

    u.add_morale( morale_audit_m15a_perm, 5, 5, 0_turns, 0_turns );
    CHECK( u.has_morale( morale_audit_m15a_perm ) == 5 );
    CHECK( eval_math( d, "u_morale_value('audit_m15a_perm')" ) == 5 );

    const std::string refused = capture_debugmsg_during( [&u]() {
        u.add_morale( morale_audit_m15a_temp, 5, 5, 0_turns, 0_turns );
    } );
    CHECK( refused.find( "non-permanent" ) != std::string::npos );
    CHECK( u.has_morale( morale_audit_m15a_temp ) == 0 );
    CHECK( eval_math( d, "u_morale_value('audit_m15a_temp')" ) == 0 );

    u.add_morale( morale_audit_m15a_temp, -7, -20, 1_hours, 30_minutes );
    CHECK( eval_math( d, "u_morale_value('audit_m15a_temp')" ) == -7 );
}

// HLTH-J09: a disease_type could only be caught from food, and its body parts weren't checked.
TEST_CASE( "hlth-j09_expose_to_disease_from_an_eoc", "[audit][disease]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    const talk_effect_t expose = load_effect(
                                     R"({ "effect": { "u_expose_to_disease": "audit_m15a_disease" } })" );
    dialogue d( get_talker_for( u ), nullptr );
    expose.apply( d );
    const bodypart_id torso( body_part_torso );
    REQUIRE( u.has_effect( effect_audit_m15a_symptom, torso ) );
    CHECK( u.get_effect_int( effect_audit_m15a_symptom, torso ) == 2 );
    CHECK( u.get_effect_dur( effect_audit_m15a_symptom, torso ) == 1_hours );
}

// HLTH-J10: EOCs couldn't read the stomach, a wound's healing or pain, or wake a character.
TEST_CASE( "hlth-j10_stomach_and_wound_math", "[audit][math_parser]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    dialogue d( get_talker_for( u ), nullptr );

    u.stomach.mod_calories( 300 );
    u.stomach.mod_quench( 20 );
    u.guts.mod_calories( 120 );
    CHECK( eval_math( d, "u_stomach('kcal')" ) == u.stomach.get_calories() );
    CHECK( eval_math( d, "u_stomach('kcal', 'organ': 'guts')" ) == u.guts.get_calories() );
    CHECK( eval_math( d, "u_stomach('water')" ) == units::to_milliliter( u.stomach.get_water() ) );
    CHECK( eval_math( d, "u_stomach('volume')" ) == units::to_milliliter( u.stomach.contains() ) );
    CHECK( eval_math( d, "u_stomach('capacity')" ) ==
           units::to_milliliter( u.stomach.capacity( u ) ) );
    CHECK( eval_math( d, "u_stomach('volume', 'organ': 'guts')" ) ==
           units::to_milliliter( u.guts.contains() ) );
    CHECK( u.stomach.get_calories() > 0 );

    const bodypart_id arm_l( body_part_arm_l );
    CHECK( eval_math( d, "u_wound_healing('audit_m15a_slow')" ) == -1 );
    CHECK( eval_math( d, "u_wound_pain('audit_m15a_slow')" ) == -1 );
    u.apply_wound( arm_l, wound_audit_m15a_slow );
    CHECK( eval_math( d, "u_wound_healing('audit_m15a_slow')" ) == Approx( 0 ) );
    CHECK( eval_math( d, "u_wound_pain('audit_m15a_slow', 'bodypart': 'arm_l')" ) == 4 );
    CHECK( eval_math( d, "u_wound_healing('audit_m15a_slow', 'bodypart': 'arm_r')" ) == -1 );

    assign_math( d, "u_wound_healing('audit_m15a_slow') = 50" );
    CHECK( eval_math( d, "u_wound_healing('audit_m15a_slow')" ) == Approx( 50 ).margin( 0.01 ) );
    // Its pain lessens as it heals
    CHECK( eval_math( d, "u_wound_pain('audit_m15a_slow')" ) == 2 );

    // Healed in full, it goes on the next update, as healed
    event_catcher events( { event_type::character_loses_wound } );
    assign_math( d, "u_wound_healing('audit_m15a_slow', 'bodypart': 'arm_l') = 100" );
    u.update_wounds( 1_turns );
    CHECK_FALSE( u.has_wound( wound_audit_m15a_slow ) );
    const std::vector<cata::event> lost = events.of( event_type::character_loses_wound );
    REQUIRE( lost.size() == 1 );
    CHECK( lost[0].get<cata_variant_type::bool_>( "healed" ) );
}

TEST_CASE( "hlth-j10_wake_up_from_an_eoc", "[audit][npc_talk]" )
{
    clear_avatar();
    avatar &u = get_avatar();
    on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    const talk_effect_t wake = load_effect( R"({ "effect": "u_wake_up" })" );
    dialogue d( get_talker_for( u ), nullptr );
    event_catcher events( { event_type::character_wakes_up } );

    // Under anesthesia, nothing wakes it
    u.fall_asleep( 2_hours );
    u.add_effect( effect_narcosis, 1_hours );
    REQUIRE( u.get_effect_dur( effect_sleep ) > 0_turns );
    wake.apply( d );
    CHECK( u.get_effect_dur( effect_sleep ) > 0_turns );
    CHECK( events.caught.empty() );

    // Otherwise it wakes up, with the event
    u.remove_effect( effect_narcosis );
    wake.apply( d );
    CHECK( u.get_effect_dur( effect_sleep ) == 0_turns );
    REQUIRE( events.caught.size() == 1 );
    CHECK( events.caught[0].get<cata_variant_type::character_id>( "character" ) == u.getID() );
}

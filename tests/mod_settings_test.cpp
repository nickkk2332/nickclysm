#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "condition.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "json_error.h"
#include "json_loader.h"
#include "math_parser.h"
#include "math_parser_type.h"
#include "mod_setting.h"
#include "options.h"
#include "path_info.h"
#include "stats_tracker.h"
#include "talker.h"
#include "type_id.h"
#include "worldfactory.h"

// DATA-J01: mods offer players settings, kept per world (doc/JSON/MOD_SETTINGS.md).  The menus
// need a display, so these tests cover the data behind them.

static const mod_id MOD_INFORMATION_dda( "dda" );
static const mod_id MOD_INFORMATION_test_data( "test_data" );
static const mod_id MOD_INFORMATION_test_mod_settings( "test_mod_settings" );

static const mod_setting_id mod_setting_test_ms_choice( "test_ms_choice" );
static const mod_setting_id mod_setting_test_ms_fixed( "test_ms_fixed" );
static const mod_setting_id mod_setting_test_ms_number( "test_ms_number" );
static const mod_setting_id mod_setting_test_ms_switch( "test_ms_switch" );
static const mod_setting_id mod_setting_test_ms_text( "test_ms_text" );

static const effect_on_condition_id effect_on_condition_test_ms_reads( "test_ms_reads" );
static const effect_on_condition_id effect_on_condition_test_ms_writes( "test_ms_writes" );

namespace
{
// The active world's mod settings, and whether they need saving, as they were
class world_settings_restorer
{
    public:
        world_settings_restorer() : saved( world_generator->active_world->mod_settings ),
            was_unsaved( mod_settings::have_unsaved_changes() ) {}
        ~world_settings_restorer() {
            world_generator->active_world->mod_settings = saved;
            if( !was_unsaved ) {
                mod_settings::mark_saved();
            }
        }
        world_settings_restorer( const world_settings_restorer & ) = delete;
        world_settings_restorer &operator=( const world_settings_restorer & ) = delete;
    private:
        mod_setting_values saved;
        bool was_unsaved;
};

// Global variables as they were
class globals_restorer
{
    public:
        explicit globals_restorer( const std::vector<std::string> &names ) {
            for( const std::string &name : names ) {
                const diag_value *value = get_globals().maybe_get_global_value( name );
                saved[name] = value ? std::optional<diag_value>( *value ) : std::nullopt;
            }
        }
        ~globals_restorer() {
            for( const auto &[name, value] : saved ) {
                if( value ) {
                    get_globals().set_global_value( name, *value );
                } else {
                    get_globals().remove_global_value( name );
                }
            }
        }
        globals_restorer( const globals_restorer & ) = delete;
        globals_restorer &operator=( const globals_restorer & ) = delete;
    private:
        std::map<std::string, std::optional<diag_value>> saved;
};

// The mod_setting_changed events, in order
struct setting_event_recorder : public event_subscriber {
    std::vector<cata::event> events;

    setting_event_recorder() {
        get_event_bus().subscribe( this );
    }
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::mod_setting_changed ) {
            events.push_back( e );
        }
    }
};

std::string event_field_of( const cata::event &e, const std::string &name )
{
    return e.get<cata_variant_type::string>( name );
}

std::string global_string( const std::string &name )
{
    const diag_value *value = get_globals().maybe_get_global_value( name );
    return value ? value->to_string() : std::string();
}

std::set<std::string> ids_of( const mod_settings_preview &preview )
{
    std::set<std::string> ids;
    for( const mod_setting &s : preview.settings ) {
        ids.insert( s.id.str() );
    }
    return ids;
}

void load_setting( const std::string &json, const std::string &src )
{
    const JsonObject jo = json_loader::from_string( json );
    try {
        mod_settings::load( jo, src );
    } catch( ... ) {
        jo.allow_omitted_members();
        throw;
    }
}

dialogue avatar_dialogue()
{
    return dialogue( get_talker_for( get_avatar() ), nullptr, {} );
}

void run_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t fx( jo, "effect", "mod_settings_test" );
    finalize_conditions();
    dialogue d = avatar_dialogue();
    fx.apply( d );
}

conditional_t::func load_condition( const std::string &condition )
{
    const JsonObject jo = json_loader::from_string( R"({ "condition": )" + condition + " }" );
    jo.allow_omitted_members();
    conditional_t::func cond;
    read_condition( jo, "condition", cond, false );
    finalize_conditions();
    return cond;
}

bool check_condition( const std::string &condition )
{
    const dialogue d = avatar_dialogue();
    return load_condition( condition )( d );
}

// What the load checks of a condition or an effect report
std::string load_check_errors( const std::function<void()> &load )
{
    load();
    finalize_conditions();
    return capture_debugmsg_during( []() {
        effect_on_conditions::check_consistency();
    } );
}

double setting_number( const mod_setting_id &id )
{
    return *id->as_number( mod_settings::get( *id ) );
}

std::string setting_text( const mod_setting_id &id )
{
    return id->as_text( mod_settings::get( *id ) );
}
} // namespace

TEST_CASE( "mod_setting_load_errors", "[mod_settings]" )
{
    SECTION( "the default lies in range and on a step" ) {
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "number", "min": 0, "max": 1, "default": 2 })", "test_mod_a" ), JsonError );
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "number", "min": 0, "max": 1, "step": 0.25, "default": 0.3 })", "test_mod_a" ),
                         JsonError );
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "number", "min": 0, "max": 1, "step": 0, "default": 0 })", "test_mod_a" ),
                         JsonError );
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "number", "min": 1, "max": 1, "default": 1 })", "test_mod_a" ), JsonError );
    }
    SECTION( "choice ids are unique, and the default is one of them" ) {
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "choice", "default": "a",
            "choices": [ { "id": "a", "name": "A" }, { "id": "a", "name": "B" } ] })",
                                       "test_mod_a" ),
                         JsonError );
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "choice", "default": "c", "choices": [ { "id": "a", "name": "A" } ] })",
                                       "test_mod_a" ), JsonError );
    }
    SECTION( "text is one line of at most 200 characters" ) {
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "text", "max_length": 201 })", "test_mod_a" ), JsonError );
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "text", "default": "two\nlines" })", "test_mod_a" ), JsonError );
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "text", "max_length": 3, "default": "four" })", "test_mod_a" ), JsonError );
    }
    SECTION( "the kind is one of four" ) {
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_bad", "name": "x",
            "kind": "slider" })", "test_mod_a" ), JsonError );
    }
    SECTION( "another mod changes a setting only with copy-from" ) {
        CHECK_THROWS_AS( load_setting( R"({ "type": "mod_setting", "id": "test_ms_switch",
            "name": "x", "kind": "bool" })", "test_mod_a" ), JsonError );
    }
    // none of them was added
    CHECK_FALSE( mod_setting_id( "test_bad" ).is_valid() );
    CHECK( mod_setting_test_ms_switch->name.translated() == "Test switch" );

    SECTION( "math rejects unknown ids, and text, which isn't a number" ) {
        math_exp exp;
        CHECK_THROWS_AS( exp.parse( "mod_setting('test_ms_nope')", false ), math::exception );
        CHECK_THROWS_AS( exp.parse( "mod_setting('test_ms_text') + 1", false ), math::exception );
        CHECK( exp.parse( "mod_setting('test_ms_number') + 1", false ) );
        // a variable id is checked when it runs
        CHECK( exp.parse( "mod_setting(_which)", false ) );
    }
    SECTION( "conditions and the mutator report unknown ids and the wrong kind" ) {
        const std::string unknown = load_check_errors( []() {
            load_condition( R"({ "mod_setting_on": "test_ms_nope" })" );
        } );
        CHECK( unknown.find( "No mod defines the setting \"test_ms_nope\"" ) != std::string::npos );
        const std::string wrong_kind = load_check_errors( []() {
            load_condition( R"({ "mod_setting_on": "test_ms_number" })" );
        } );
        CHECK( wrong_kind.find( "needs a switch" ) != std::string::npos );
        const std::string unknown_choice = load_check_errors( []() {
            load_condition( R"({ "mod_setting_is": [ "test_ms_choice", "plenty" ] })" );
        } );
        CHECK( unknown_choice.find( "\"plenty\" isn't a choice" ) != std::string::npos );
        const std::string mutator = load_check_errors( []() {
            const JsonObject jo = json_loader::from_string(
                                      R"({ "s": { "mutator": "mod_setting", "id": "test_ms_nope" } })" );
            str_or_var s = get_str_or_var( jo.get_member( "s" ), "s" );
        } );
        CHECK( mutator.find( "No mod defines the setting \"test_ms_nope\"" ) != std::string::npos );
        // the ones in data/mods/TEST_DATA/mod_settings.json load without a word
        CHECK( load_check_errors( []() {
            load_condition( R"({ "mod_setting_is": [ "test_ms_text", "anything" ] })" );
        } ).empty() );
    }
}

TEST_CASE( "mod_setting_writes_need_eoc_writable", "[mod_settings]" )
{
    SECTION( "math assigning a literal id that isn't writable is a load error" ) {
        math_exp exp;
        CHECK_THROWS_AS( exp.parse( "mod_setting('test_ms_fixed') = 1", false ), math::exception );
        CHECK_THROWS_AS( exp.parse( "mod_setting('test_ms_fixed') += 1", false ), math::exception );
        CHECK( exp.parse( "mod_setting('test_ms_number') = 2", false ) );
        // reading it is fine
        CHECK( exp.parse( "mod_setting('test_ms_fixed') == 1", false ) );
    }
    SECTION( "so is set_mod_setting with a literal id" ) {
        const std::string fixed = load_check_errors( []() {
            const JsonObject jo = json_loader::from_string(
                                      R"({ "effect": { "set_mod_setting": "test_ms_fixed", "value": false } })" );
            talk_effect_t fx( jo, "effect", "mod_settings_test" );
        } );
        CHECK( fixed.find( "EOCs can't change \"test_ms_fixed\"" ) != std::string::npos );
        const std::string bad_choice = load_check_errors( []() {
            const JsonObject jo = json_loader::from_string(
                                      R"({ "effect": { "set_mod_setting": "test_ms_choice", "value": "plenty" } })" );
            talk_effect_t fx( jo, "effect", "mod_settings_test" );
        } );
        CHECK( bad_choice.find( "\"plenty\" isn't a choice" ) != std::string::npos );
        CHECK( load_check_errors( []() {
            const JsonObject jo = json_loader::from_string(
                                      R"({ "effect": { "set_mod_setting": "test_ms_choice", "value": "rare" } })" );
            talk_effect_t fx( jo, "effect", "mod_settings_test" );
        } ).empty() );
    }
    SECTION( "a variable id that isn't writable is a debugmsg when it runs" ) {
        world_settings_restorer restore_settings;
        globals_restorer restore_globals( { "test_ms_which" } );
        get_globals().set_global_value( "test_ms_which", "test_ms_fixed" );
        const std::string msg = capture_debugmsg_during( []() {
            run_effect( R"({ "set_mod_setting": { "global_val": "test_ms_which" }, "value": false })" );
        } );
        CHECK( msg.find( "EOCs can't change \"test_ms_fixed\"" ) != std::string::npos );
        CHECK( std::get<bool>( mod_settings::get( *mod_setting_test_ms_fixed ) ) );
        const std::string math_msg = capture_debugmsg_during( []() {
            run_effect( R"({ "math": [ "mod_setting(test_ms_which) = 0" ] })" );
        } );
        CHECK( math_msg.find( "EOCs can't change \"test_ms_fixed\"" ) != std::string::npos );
        CHECK( std::get<bool>( mod_settings::get( *mod_setting_test_ms_fixed ) ) );
    }
}

TEST_CASE( "mod_settings_saved_with_the_world_without_defaults", "[mod_settings]" )
{
    WORLD world( "mod settings test world" );
    // before the check, so a run that stopped half way leaves nothing to the next
    on_out_of_scope remove_world( [&world]() {
        std::filesystem::remove_all( world.folder_path().get_unrelative_path() );
    } );
    REQUIRE_FALSE( dir_exist( world.folder_path().get_unrelative_path() ) );

    std::vector<const mod_setting *> settings;
    for( const mod_setting &s : mod_settings::get_all() ) {
        settings.push_back( &s );
    }
    mod_settings_editor editor( settings, world.mod_settings );
    CHECK( editor.set( *mod_setting_test_ms_switch, true ).empty() );
    CHECK( editor.set( *mod_setting_test_ms_number, 2.5 ).empty() );
    CHECK( editor.set( *mod_setting_test_ms_choice, std::string( "rare" ) ).empty() );
    CHECK( editor.set( *mod_setting_test_ms_text, std::string( "hi there" ) ).empty() );
    // at its default: not stored
    CHECK( editor.set( *mod_setting_test_ms_fixed, true ).empty() );
    // a setting set back to its default is dropped
    CHECK( editor.set( *mod_setting_test_ms_number, 3.0 ).empty() );
    CHECK( editor.set( *mod_setting_test_ms_number, 1.0 ).empty() );
    // the value of a mod no longer loaded stays
    world.mod_settings.set( "removed_mod_setting", 7.0 );

    CHECK( world.mod_settings.all().size() == 4 );
    CHECK( world.mod_settings.find( "test_ms_number" ) == nullptr );
    CHECK( world.mod_settings.find( "test_ms_fixed" ) == nullptr );
    // a world option the settings' element sits among
    world.WORLD_OPTIONS["SPAWN_DENSITY"].setValue( 0.5f );

    REQUIRE( world.save() );
    const std::string saved = read_entire_file( ( world.folder_path() /
                              PATH_INFO::worldoptions() ).get_unrelative_path() );
    CHECK( saved.find( "\"mod_settings\"" ) != std::string::npos );
    CHECK( saved.find( "\"test_ms_switch\":true" ) != std::string::npos );
    CHECK( saved.find( "test_ms_number" ) == std::string::npos );
    CHECK( saved.find( "test_ms_fixed" ) == std::string::npos );

    WORLD reloaded( "mod settings test world" );
    REQUIRE( reloaded.load_options() );
    CHECK( reloaded.mod_settings == world.mod_settings );
    const mod_setting_value *text = reloaded.mod_settings.find( "test_ms_text" );
    REQUIRE( text != nullptr );
    CHECK( std::get<std::string>( *text ) == "hi there" );
    const mod_setting_value *removed = reloaded.mod_settings.find( "removed_mod_setting" );
    REQUIRE( removed != nullptr );
    CHECK( std::get<double>( *removed ) == Approx( 7.0 ) );
    // and the world options still load around them
    CHECK( reloaded.WORLD_OPTIONS["SPAWN_DENSITY"].value_as<float>() == Approx( 0.5 ) );

    // A build without mod settings reads every element as an option, with a "name" and a
    // "value", and skips an option it doesn't know.  The settings' element must be one, or
    // that build throws, resets every world option and saves over the file.
    const JsonValue file = json_loader::from_path( world.folder_path() / PATH_INFO::worldoptions() );
    int settings_elements = 0;
    for( JsonObject jo : file.get_array() ) {
        jo.allow_omitted_members();
        const std::string name = get_options().migrateOptionName( jo.get_string( "name" ) );
        const std::string value = get_options().migrateOptionValue( jo.get_string( "name" ),
                                  jo.get_string( "value" ) );
        if( jo.has_object( "mod_settings" ) ) {
            ++settings_elements;
            CHECK( name == "__mod_settings" );
            CHECK( value.empty() );
            CHECK_FALSE( get_options().has_option( name ) );
        }
    }
    CHECK( settings_elements == 1 );

    // a world with no settings changed writes nothing for them
    world.mod_settings.clear();
    REQUIRE( world.save() );
    CHECK( read_entire_file( ( world.folder_path() / PATH_INFO::worldoptions() ).get_unrelative_path()
                           ).find( "mod_settings" ) == std::string::npos );
}

TEST_CASE( "mod_settings_read_by_math_conditions_and_text", "[mod_settings]" )
{
    world_settings_restorer restore_settings;
    globals_restorer restore_globals( { "test_ms_text_copy", "test_ms_choice_index", "test_ms_copy" } );
    world_generator->active_world->mod_settings.clear();

    // the defaults, with nothing stored
    CHECK( setting_number( mod_setting_test_ms_number ) == Approx( 1.0 ) );
    CHECK( setting_number( mod_setting_test_ms_switch ) == 0 );
    CHECK( setting_number( mod_setting_test_ms_fixed ) == 1 );
    // a choice reads as its index
    CHECK( setting_number( mod_setting_test_ms_choice ) == 1 );
    CHECK_FALSE( mod_setting_test_ms_text->as_number( mod_settings::get(
                     *mod_setting_test_ms_text ) ) );

    dialogue d = avatar_dialogue();
    effect_on_condition_test_ms_reads->activate( d );
    CHECK( global_string( "test_ms_text_copy" ) == "hello" );
    CHECK( get_globals().get_global_value( "test_ms_choice_index" ).dbl() == 1 );

    mod_setting_values &values = world_generator->active_world->mod_settings;
    values.set( "test_ms_switch", true );
    values.set( "test_ms_number", 2.5 );
    values.set( "test_ms_choice", std::string( "common" ) );
    values.set( "test_ms_text", std::string( "a mod's text" ) );

    math_exp exp;
    REQUIRE( exp.parse( "mod_setting('test_ms_number') * 2 + mod_setting('test_ms_switch')" ) );
    CHECK( exp.eval( d ) == Approx( 6.0 ) );
    REQUIRE( exp.parse( "mod_setting('test_ms_choice')" ) );
    CHECK( exp.eval( d ) == Approx( 2.0 ) );

    CHECK( check_condition( R"({ "mod_setting_on": "test_ms_switch" })" ) );
    CHECK( check_condition( R"({ "mod_setting_on": "test_ms_fixed" })" ) );
    CHECK( check_condition( R"({ "mod_setting_is": [ "test_ms_choice", "common" ] })" ) );
    CHECK_FALSE( check_condition( R"({ "mod_setting_is": [ "test_ms_choice", "rare" ] })" ) );
    CHECK( check_condition( R"({ "mod_setting_is": [ "test_ms_text", "a mod's text" ] })" ) );

    // a text setting read into a string variable
    run_effect( R"({ "set_string_var": { "mutator": "mod_setting", "id": "test_ms_text" },
        "target_var": { "global_val": "test_ms_copy" } })" );
    CHECK( global_string( "test_ms_copy" ) == "a mod's text" );

    // a stored value the setting no longer allows reads as the default
    values.set( "test_ms_choice", std::string( "gone" ) );
    CHECK( setting_text( mod_setting_test_ms_choice ) == "normal" );
    values.set( "test_ms_number", 99.0 );
    CHECK( setting_number( mod_setting_test_ms_number ) == Approx( 4.0 ) );
}

TEST_CASE( "mod_settings_pre_read_finds_the_selected_mods_settings", "[mod_settings]" )
{
    // what the full load has of test_data
    std::set<std::string> test_data_ids;
    for( const mod_setting &s : mod_settings::get_all() ) {
        if( s.owner() == MOD_INFORMATION_test_data ) {
            test_data_ids.insert( s.id.str() );
        }
    }
    REQUIRE( test_data_ids.size() == 5 );

    SECTION( "no mods, or only core content: nothing" ) {
        const mod_settings_preview none = pre_read_mod_settings( {} );
        CHECK( none.settings.empty() );
        const mod_settings_preview dda = pre_read_mod_settings( { MOD_INFORMATION_dda } );
        CHECK( dda.settings.empty() );
        CHECK( dda.world_option_defaults.empty() );
        CHECK( dda.errors.empty() );
    }
    SECTION( "test_data alone: its settings, as the full load has them" ) {
        const mod_settings_preview preview = pre_read_mod_settings( { MOD_INFORMATION_test_data } );
        CHECK( preview.errors.empty() );
        CHECK( ids_of( preview ) == test_data_ids );
        const mod_setting *number = preview.find( "test_ms_number" );
        REQUIRE( number != nullptr );
        CHECK( number->owner() == MOD_INFORMATION_test_data );
        CHECK( std::get<double>( number->default_value ) == Approx( 1.0 ) );
        CHECK( number->step == Approx( 0.25 ) );
        CHECK( number->group.translated() == "Numbers" );
        CHECK( preview.world_option_defaults.empty() );
    }
    SECTION( "the test mod alone: its settings, slider and WORLD_OPTION, nothing of test_data" ) {
        const mod_settings_preview preview =
            pre_read_mod_settings( { MOD_INFORMATION_test_mod_settings } );
        CHECK( preview.errors.empty() );
        CHECK( ids_of( preview ) == std::set<std::string> { "test_pre_switch", "test_pre_number" } );
        REQUIRE( preview.sliders.size() == 1 );
        CHECK( preview.sliders[0].id.str() == "test_pre_slider" );
        CHECK( preview.sliders[0].count() == 2 );
        REQUIRE( preview.world_option_defaults.count( "SPAWN_DENSITY" ) == 1 );
        CHECK( std::stod( preview.world_option_defaults.at( "SPAWN_DENSITY" ) ) == Approx( 2.0 ) );
        // the mod's slider comes after core's
        const std::vector<const option_slider *> sliders = preview_sliders( preview, "WORLDGEN" );
        REQUIRE( sliders.size() >= 2 );
        CHECK( sliders.front()->id.str() == "world_difficulty" );
        CHECK( sliders.back()->id.str() == "test_pre_slider" );
        // only core's otherwise
        for( const option_slider *s : preview_sliders( pre_read_mod_settings( {} ), "WORLDGEN" ) ) {
            CHECK( s->id.str() != "test_pre_slider" );
        }
    }
    SECTION( "both: their settings, and the test mod's mod_interactions for test_data" ) {
        const mod_settings_preview preview = pre_read_mod_settings( {
            MOD_INFORMATION_test_data, MOD_INFORMATION_test_mod_settings
        } );
        CHECK( preview.errors.empty() );
        std::set<std::string> expected = test_data_ids;
        expected.insert( { "test_pre_switch", "test_pre_number", "test_pre_with_test_data" } );
        CHECK( ids_of( preview ) == expected );
        // changed by copy-from, still test_data's
        const mod_setting *number = preview.find( "test_ms_number" );
        REQUIRE( number != nullptr );
        CHECK( std::get<double>( number->default_value ) == Approx( 2.0 ) );
        CHECK( number->owner() == MOD_INFORMATION_test_data );
        CHECK( number->name.translated() == "Test number" );
        const mod_setting *interaction = preview.find( "test_pre_with_test_data" );
        REQUIRE( interaction != nullptr );
        CHECK( interaction->owner() == MOD_INFORMATION_test_mod_settings );
    }
    SECTION( "a mod_reader reads again the same" ) {
        mod_settings_reader reader;
        const std::set<std::string> first = ids_of( reader.read( {
            MOD_INFORMATION_test_data, MOD_INFORMATION_test_mod_settings
        } ) );
        CHECK( ids_of( reader.read( { MOD_INFORMATION_test_mod_settings } ) ) ==
               std::set<std::string> { "test_pre_switch", "test_pre_number" } );
        CHECK( ids_of( reader.read( {
            MOD_INFORMATION_test_data, MOD_INFORMATION_test_mod_settings
        } ) ) == first );
    }
}

TEST_CASE( "mod_settings_kept_across_mod_list_changes", "[mod_settings]" )
{
    WORLD world( "mod settings test world" );
    const mod_settings_preview none;
    const mod_settings_preview test_mod = pre_read_mod_settings( { MOD_INFORMATION_test_mod_settings } );
    const mod_settings_preview both = pre_read_mod_settings( {
        MOD_INFORMATION_test_data, MOD_INFORMATION_test_mod_settings
    } );
    const mod_settings_preview test_data = pre_read_mod_settings( { MOD_INFORMATION_test_data } );

    // a value a copied world brought, for a mod it no longer has
    world.mod_settings.set( "removed_mod_setting", 3.0 );
    update_world_mod_settings( world, none, test_mod );
    mod_settings_editor first( test_mod.all_settings(), world.mod_settings );
    REQUIRE( first.set( *test_mod.find( "test_pre_switch" ), true ).empty() );

    // test_data added: the test mod's value stays
    update_world_mod_settings( world, test_mod, both );
    CHECK( world.mod_settings.find( "test_pre_switch" ) != nullptr );
    mod_settings_editor second( both.all_settings(), world.mod_settings );
    REQUIRE( second.set( *both.find( "test_ms_switch" ), true ).empty() );
    REQUIRE( second.set( *both.find( "test_pre_with_test_data" ), std::string( "set" ) ).empty() );

    // the test mod removed: its values go, test_data's stay
    update_world_mod_settings( world, both, test_data );
    CHECK( world.mod_settings.find( "test_pre_switch" ) == nullptr );
    CHECK( world.mod_settings.find( "test_pre_with_test_data" ) == nullptr );
    CHECK( world.mod_settings.find( "test_ms_switch" ) != nullptr );
    CHECK( world.mod_settings.find( "removed_mod_setting" ) != nullptr );

    SECTION( "the settings tab shows them per mod, then per group" ) {
        const std::vector<mod_settings::section> sections =
            mod_settings::make_sections( both.all_settings() );
        REQUIRE( sections.size() == 4 );
        CHECK( sections[0].mod == MOD_INFORMATION_test_data );
        CHECK( sections[0].group.empty() );
        CHECK( sections[0].settings.size() == 4 );
        CHECK( sections[1].mod == MOD_INFORMATION_test_data );
        CHECK( sections[1].group.translated() == "Numbers" );
        CHECK( sections[1].title() == "TESTING DATA: Numbers" );
        CHECK( sections[2].mod == MOD_INFORMATION_test_mod_settings );
        CHECK( sections[2].group.empty() );
        CHECK( sections[3].group.translated() == "Group A" );
    }
    SECTION( "in game, a setting fixed once the world exists is shown locked" ) {
        mod_settings_editor in_game;
        CHECK( in_game.in_game() );
        CHECK_FALSE( in_game.locked_reason( *mod_setting_test_ms_fixed ).empty() );
        CHECK( in_game.locked_reason( *mod_setting_test_ms_switch ).empty() );
        // at world creation, every setting can be picked
        CHECK( second.locked_reason( *both.find( "test_ms_fixed" ) ).empty() );
    }
}

TEST_CASE( "mod_setting_changed_fires_once_with_both_values", "[mod_settings][event]" )
{
    world_settings_restorer restore_settings;
    globals_restorer restore_globals( { "test_ms_listen", "test_ms_changes", "test_ms_changed_id",
                                        "test_ms_changed_old", "test_ms_changed_new"
                                      } );
    world_generator->active_world->mod_settings.clear();
    get_globals().set_global_value( "test_ms_listen", 1.0 );
    const int recorded_before = get_stats().get_events( event_type::mod_setting_changed ).count();
    setting_event_recorder rec;

    // as the in-game menu changes it
    mod_settings_editor in_game;
    CHECK( in_game.set( *mod_setting_test_ms_number, 2.5 ).empty() );
    REQUIRE( rec.events.size() == 1 );
    CHECK( event_field_of( rec.events[0], "id" ) == "test_ms_number" );
    CHECK( event_field_of( rec.events[0], "old_value" ) == "1.00" );
    CHECK( event_field_of( rec.events[0], "new_value" ) == "2.50" );
    // EVENT EOCs get them as context variables
    CHECK( get_globals().get_global_value( "test_ms_changes" ).dbl() == 1 );
    CHECK( global_string( "test_ms_changed_id" ) == "test_ms_number" );
    CHECK( global_string( "test_ms_changed_old" ) == "1.00" );
    CHECK( global_string( "test_ms_changed_new" ) == "2.50" );
    CHECK( mod_settings::have_unsaved_changes() );

    // the same value again: nothing changed, nothing sent
    CHECK( in_game.set( *mod_setting_test_ms_number, 2.5 ).empty() );
    CHECK( rec.events.size() == 1 );

    // as an EOC changes them
    rec.events.clear();
    dialogue d = avatar_dialogue();
    effect_on_condition_test_ms_writes->activate( d );
    REQUIRE( rec.events.size() == 4 );
    CHECK( event_field_of( rec.events[0], "id" ) == "test_ms_switch" );
    CHECK( event_field_of( rec.events[0], "old_value" ) == "false" );
    CHECK( event_field_of( rec.events[0], "new_value" ) == "true" );
    CHECK( event_field_of( rec.events[1], "new_value" ) == "rare" );
    CHECK( event_field_of( rec.events[2], "old_value" ) == "hello" );
    CHECK( event_field_of( rec.events[2], "new_value" ) == "changed" );
    CHECK( event_field_of( rec.events[3], "old_value" ) == "2.50" );
    CHECK( event_field_of( rec.events[3], "new_value" ) == "3.50" );
    CHECK( get_globals().get_global_value( "test_ms_changes" ).dbl() == 5 );

    // statistics don't keep them
    CHECK( get_stats().get_events( event_type::mod_setting_changed ).count() == recorded_before );
    CHECK_FALSE( stats_tracker::records( event_type::mod_setting_changed ) );
}

TEST_CASE( "mod_setting_writes_are_clamped_snapped_and_checked", "[mod_settings]" )
{
    world_settings_restorer restore_settings;
    world_generator->active_world->mod_settings.clear();

    SECTION( "numbers are clamped and snapped to the step" ) {
        run_effect( R"({ "math": [ "mod_setting('test_ms_number') = 10" ] })" );
        CHECK( setting_number( mod_setting_test_ms_number ) == Approx( 4.0 ) );
        run_effect( R"({ "math": [ "mod_setting('test_ms_number') = -3" ] })" );
        CHECK( setting_number( mod_setting_test_ms_number ) == Approx( 0.25 ) );
        run_effect( R"({ "math": [ "mod_setting('test_ms_number') = 1.1" ] })" );
        CHECK( setting_number( mod_setting_test_ms_number ) == Approx( 1.0 ) );
        run_effect( R"({ "set_mod_setting": "test_ms_number", "value": 1.2 })" );
        CHECK( setting_number( mod_setting_test_ms_number ) == Approx( 1.25 ) );
        run_effect( R"({ "math": [ "mod_setting('test_ms_number') += 0.25" ] })" );
        CHECK( setting_number( mod_setting_test_ms_number ) == Approx( 1.5 ) );
        CHECK( setting_text( mod_setting_test_ms_number ) == "1.50" );
        // the default is dropped from the world
        run_effect( R"({ "set_mod_setting": "test_ms_number", "value": 1 })" );
        CHECK( world_generator->active_world->mod_settings.find( "test_ms_number" ) == nullptr );
    }
    SECTION( "an unknown choice is rejected" ) {
        run_effect( R"({ "set_mod_setting": "test_ms_choice", "value": "common" })" );
        REQUIRE( setting_text( mod_setting_test_ms_choice ) == "common" );
        const std::string msg = capture_debugmsg_during( []() {
            run_effect(
                R"({ "set_mod_setting": "test_ms_choice", "value": { "global_val": "test_ms_nothing", "default": "plenty" } })" );
        } );
        CHECK( msg.find( "\"plenty\" isn't a choice of mod setting \"test_ms_choice\"" ) !=
               std::string::npos );
        CHECK( setting_text( mod_setting_test_ms_choice ) == "common" );
        // by index, as math reads it
        run_effect( R"({ "math": [ "mod_setting('test_ms_choice') = 0" ] })" );
        CHECK( setting_text( mod_setting_test_ms_choice ) == "rare" );
        const std::string out_of_range = capture_debugmsg_during( []() {
            run_effect( R"({ "math": [ "mod_setting('test_ms_choice') = 3" ] })" );
        } );
        CHECK( out_of_range.find( "isn't the index of a choice" ) != std::string::npos );
        CHECK( setting_text( mod_setting_test_ms_choice ) == "rare" );
    }
    SECTION( "text is kept to one line of 200 characters" ) {
        const std::string long_text( 250, 'x' );
        run_effect( R"({ "set_mod_setting": "test_ms_text", "value": ")" + long_text + R"(" })" );
        CHECK( setting_text( mod_setting_test_ms_text ) == std::string( 200, 'x' ) );
        // characters, not bytes
        std::string accents;
        for( int i = 0; i < 210; ++i ) {
            accents += "é";
        }
        run_effect( R"({ "set_mod_setting": "test_ms_text", "value": ")" + accents + R"(" })" );
        CHECK( utf8_to_utf32( setting_text( mod_setting_test_ms_text ) ).size() == 200 );
        run_effect( R"({ "set_mod_setting": "test_ms_text", "value": "two\nlines\r\nhere" })" );
        CHECK( setting_text( mod_setting_test_ms_text ) == "two lines here" );
        // a number becomes text
        run_effect( R"({ "set_mod_setting": "test_ms_text", "value": 7 })" );
        CHECK( setting_text( mod_setting_test_ms_text ) == "7" );
    }
    SECTION( "a switch takes true and false, or a number" ) {
        run_effect( R"({ "set_mod_setting": "test_ms_switch", "value": true })" );
        CHECK( setting_number( mod_setting_test_ms_switch ) == 1 );
        run_effect( R"({ "math": [ "mod_setting('test_ms_switch') = 0" ] })" );
        CHECK( setting_number( mod_setting_test_ms_switch ) == 0 );
    }
}

TEST_CASE( "world_option_changes_a_default", "[mod_settings][options]" )
{
    const auto read = []( const std::string & json ) {
        const JsonObject jo = json_loader::from_string( json );
        // "type" is the loader's
        jo.allow_omitted_members();
        return read_world_option( jo );
    };
    SECTION( "it names a world option and a valid default" ) {
        const std::pair<std::string, std::string> option =
            read( R"({ "type": "WORLD_OPTION", "name": "SPAWN_DENSITY", "default": 2 })" );
        CHECK( option.first == "SPAWN_DENSITY" );
        CHECK( std::stod( option.second ) == Approx( 2.0 ) );
        CHECK( read( R"({ "type": "WORLD_OPTION", "name": "ETERNAL_SEASON", "default": true })" ).second
               == "true" );
        CHECK_THROWS_AS( read( R"({ "type": "WORLD_OPTION", "name": "SPAWN_DENSITY", "default": 100 })" ),
                         JsonError );
        CHECK_THROWS_AS( read( R"({ "type": "WORLD_OPTION", "name": "NO_SUCH_OPTION", "default": 1 })" ),
                         JsonError );
        // not a world option
        CHECK_THROWS_AS( read( R"({ "type": "WORLD_OPTION", "name": "AUTOSAVE", "default": true })" ),
                         JsonError );
        // the old shape forced options on
        CHECK_THROWS_AS( read( R"({ "type": "WORLD_OPTION", "options": [ "ETERNAL_SEASON" ] })" ),
                         JsonError );
    }
    SECTION( "a new world takes it, unless the option was changed" ) {
        WORLD world( "mod settings test world" );
        options_manager::cOpt &density = world.WORLD_OPTIONS["SPAWN_DENSITY"];
        REQUIRE( density.isDefault() );
        const float builtin = density.value_as<float>();
        const mod_settings_preview none;
        const mod_settings_preview test_mod =
            pre_read_mod_settings( { MOD_INFORMATION_test_mod_settings } );
        update_world_mod_settings( world, none, test_mod );
        CHECK( density.value_as<float>() == Approx( 2.0 ) );
        CHECK( density.isDefault() );
        CHECK( density.getDefaultText().find( "2.00" ) != std::string::npos );
        // the mod deselected: back to the built-in default
        update_world_mod_settings( world, test_mod, none );
        CHECK( density.value_as<float>() == Approx( builtin ) );
        // a value the player picked stays
        density.setValue( 0.5f );
        update_world_mod_settings( world, none, test_mod );
        CHECK( density.value_as<float>() == Approx( 0.5 ) );
        CHECK_FALSE( density.isDefault() );
    }
    SECTION( "loading it changes the loaded world's default, never its value" ) {
        options_manager::options_container &options = world_generator->active_world->WORLD_OPTIONS;
        const options_manager::options_container saved = options;
        on_out_of_scope restore_options( [&options, &saved]() {
            options = saved;
        } );
        const float before = get_option<float>( "SPAWN_DENSITY" );
        const JsonObject jo = json_loader::from_string(
                                  R"({ "type": "WORLD_OPTION", "name": "SPAWN_DENSITY", "default": 3 })" );
        jo.allow_omitted_members();
        load_world_option( jo );
        CHECK( get_option<float>( "SPAWN_DENSITY" ) == Approx( before ) );
        CHECK( options["SPAWN_DENSITY"].getDefaultText().find( "3.00" ) != std::string::npos );
    }
}

TEST_CASE( "mod_setting_arrow_keys_step_in_the_settings_own_numbers", "[mod_settings]" )
{
    const auto setting_from = []( const std::string & json ) {
        mod_setting setting;
        const JsonObject jo = json_loader::from_string( json );
        setting.load( jo, "test" );
        return setting;
    };
    const auto number = []( const mod_setting_value & v ) {
        return std::get<double>( v );
    };

    // in float, 0.6 and a step of 0.1 went past 0.7, and the menu wrapped to 0
    const mod_setting tenths = setting_from(
                                   R"({ "name": "x", "kind": "number", "min": 0, "max": 0.7, "step": 0.1 })" );
    CHECK( number( tenths.stepped( 0.6, 1 ) ) == 0.7 );
    CHECK( number( tenths.stepped( 0.7, 1 ) ) == 0.0 );
    CHECK( number( tenths.stepped( 0.0, -1 ) ) == 0.7 );
    // every value on the way up, then around
    mod_setting_value v = 0.0;
    for( int i = 1; i <= 7; ++i ) {
        v = tenths.stepped( v, 1 );
        CHECK( number( v ) == Approx( i * 0.1 ) );
    }
    CHECK( number( tenths.stepped( v, 1 ) ) == 0.0 );

    // a max off the steps: the last step is the highest one under it
    const mod_setting threes = setting_from(
                                   R"({ "name": "x", "kind": "number", "min": 0, "max": 10, "step": 3 })" );
    CHECK( number( threes.stepped( 6.0, 1 ) ) == 9.0 );
    CHECK( number( threes.stepped( 9.0, 1 ) ) == 0.0 );
    CHECK( number( threes.stepped( 0.0, -1 ) ) == 9.0 );

    const mod_setting choice = setting_from( R"({ "name": "x", "kind": "choice",
        "choices": [ { "id": "a", "name": "A" }, { "id": "b", "name": "B" }, { "id": "c", "name": "C" } ] })" );
    CHECK( std::get<std::string>( choice.stepped( std::string( "a" ), 1 ) ) == "b" );
    CHECK( std::get<std::string>( choice.stepped( std::string( "c" ), 1 ) ) == "a" );
    CHECK( std::get<std::string>( choice.stepped( std::string( "a" ), -1 ) ) == "c" );

    const mod_setting toggle = setting_from( R"({ "name": "x", "kind": "bool" })" );
    CHECK( std::get<bool>( toggle.stepped( false, 1 ) ) );
    CHECK_FALSE( std::get<bool>( toggle.stepped( true, -1 ) ) );
}

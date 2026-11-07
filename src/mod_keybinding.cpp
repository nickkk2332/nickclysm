#include "mod_keybinding.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "action.h"
#include "character.h"
#include "condition.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "input.h"
#include "input_context.h"
#include "string_id.h"
#include "talker.h"
#include "translation.h"
#include "type_id.h"

namespace
{
// Mod hotkeys belong to the main game
const std::string hotkey_category( "DEFAULTMODE" );

struct mod_keybinding {
    string_id<mod_keybinding> id;
    bool was_loaded = false;

    translation name;
    std::vector<input_event> keys;
    effect_on_condition_id eoc;
    bool has_condition = false;
    std::function<bool( const_dialogue const & )> condition;

    void load( const JsonObject &jo, std::string_view src );
};

generic_factory<mod_keybinding> hotkey_factory( "keybinding" );

void mod_keybinding::load( const JsonObject &jo, std::string_view src )
{
    if( jo.get_string( "category", hotkey_category ) != hotkey_category ) {
        jo.throw_error_at( "category", "mod keybindings only work in \"DEFAULTMODE\", the main game" );
    }
    mandatory( jo, was_loaded, "name", name );
    if( jo.has_array( "bindings" ) ) {
        keys.clear();
        for( const JsonObject binding : jo.get_array( "bindings" ) ) {
            for( const input_event &key : inp_mngr.read_binding( binding ) ) {
                keys.push_back( key );
            }
        }
    }
    if( jo.has_member( "effect_on_condition" ) ) {
        eoc = effect_on_conditions::load_inline_eoc( jo.get_member( "effect_on_condition" ), src );
    } else if( !was_loaded ) {
        jo.throw_error( "missing mandatory member \"effect_on_condition\"" );
    }
    if( jo.has_member( "condition" ) ) {
        read_condition( jo, "condition", condition, false );
        has_condition = true;
    }
}
} // namespace

void mod_keybindings::load( const JsonObject &jo, const std::string &src )
{
    hotkey_factory.load( jo, src );
}

void mod_keybindings::finalize()
{
    hotkey_factory.finalize();
    for( const mod_keybinding &hotkey : hotkey_factory.get_all() ) {
        if( look_up_action( hotkey.id.str() ) == ACTION_NULL &&
            !inp_mngr.has_basic_action( hotkey.id.str(), hotkey_category ) ) {
            inp_mngr.add_mod_action( hotkey.id.str(), hotkey_category, hotkey.name, hotkey.keys );
        }
    }
}

void mod_keybindings::check()
{
    for( const mod_keybinding &hotkey : hotkey_factory.get_all() ) {
        if( look_up_action( hotkey.id.str() ) != ACTION_NULL ) {
            debugmsg( "keybinding %s has the id of a built-in action; it is ignored", hotkey.id.str() );
        }
        if( !hotkey.eoc.is_valid() ) {
            debugmsg( "keybinding %s runs the unknown effect_on_condition %s", hotkey.id.str(),
                      hotkey.eoc.str() );
        }
    }
}

void mod_keybindings::reset()
{
    for( const mod_keybinding &hotkey : hotkey_factory.get_all() ) {
        inp_mngr.remove_mod_action( hotkey.id.str(), hotkey_category );
    }
    hotkey_factory.reset();
}

void mod_keybindings::register_actions( input_context &ctxt )
{
    for( const mod_keybinding &hotkey : hotkey_factory.get_all() ) {
        if( look_up_action( hotkey.id.str() ) == ACTION_NULL ) {
            ctxt.register_action( hotkey.id.str(), hotkey.name );
        }
    }
}

bool mod_keybindings::run( const std::string &action, Character &you )
{
    if( action.empty() || hotkey_factory.get_all().empty() ) {
        return false;
    }
    const string_id<mod_keybinding> id( action );
    if( !hotkey_factory.is_valid( id ) || look_up_action( action ) != ACTION_NULL ) {
        return false;
    }
    const mod_keybinding &hotkey = hotkey_factory.obj( id );
    dialogue d( get_talker_for( you ), nullptr );
    if( !hotkey.has_condition || hotkey.condition( d ) ) {
        hotkey.eoc->activate( d );
    }
    return true;
}

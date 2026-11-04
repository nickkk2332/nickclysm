#include "mod_setting.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <locale>
#include <set>
#include <sstream>
#include <unordered_map>
#include <variant>

#include "catacharset.h"
#include "debug.h"
#include "event.h"
#include "event_bus.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "init.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "mod_manager.h"
#include "string_formatter.h"
#include "translations.h"
#include "worldfactory.h"

namespace
{
generic_factory<mod_setting> mod_setting_factory( "mod setting" );
// The active world's values as of the last game save, while it has changes since: what its
// options file keeps until the next game save writes the changes
std::optional<mod_setting_values> saved_values;

const std::vector<std::pair<std::string, mod_setting::kind_t>> &kind_names()
{
    static const std::vector<std::pair<std::string, mod_setting::kind_t>> names = {
        { "bool", mod_setting::kind_t::boolean },
        { "number", mod_setting::kind_t::number },
        { "choice", mod_setting::kind_t::choice },
        { "text", mod_setting::kind_t::text },
    };
    return names;
}

// Decimals that show @p x exactly, up to 6
int decimals_of( double x )
{
    double scaled = std::fabs( x );
    for( int digits = 0; digits < 6; ++digits ) {
        if( std::fabs( scaled - std::round( scaled ) ) < 1e-6 ) {
            return digits;
        }
        scaled *= 10.0;
    }
    return 6;
}

double round_to( double x, int digits )
{
    const double scale = std::pow( 10.0, digits );
    return std::round( x * scale ) / scale;
}

std::string format_number( double x, int digits )
{
    std::ostringstream out;
    out.imbue( std::locale::classic() );
    out.setf( std::ios::fixed, std::ios::floatfield );
    out.precision( digits );
    out << x;
    return out.str();
}

// Line breaks and other control characters become spaces
std::string one_line( const std::string &text )
{
    std::string ret;
    ret.reserve( text.size() );
    for( size_t i = 0; i < text.size(); ++i ) {
        const unsigned char ch = static_cast<unsigned char>( text[i] );
        if( ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n' ) {
            continue;
        }
        ret += ch < 0x20 || ch == 0x7f ? ' ' : text[i];
    }
    return ret;
}

int character_count( const std::string &text )
{
    return static_cast<int>( utf8_to_utf32( text ).size() );
}

std::string first_characters( const std::string &text, int count )
{
    std::u32string chars = utf8_to_utf32( text );
    if( static_cast<int>( chars.size() ) <= count ) {
        return text;
    }
    chars.resize( count );
    return utf32_to_utf8( chars );
}

bool same_value( const mod_setting_value &a, const mod_setting_value &b, double step )
{
    if( std::holds_alternative<double>( a ) && std::holds_alternative<double>( b ) ) {
        return std::fabs( std::get<double>( a ) - std::get<double>( b ) ) <= step * 1e-6;
    }
    return a == b;
}

void store( mod_setting_values &values, const mod_setting &setting,
            const mod_setting_value &value )
{
    if( setting.is_default( value ) ) {
        values.erase( setting.id.str() );
    } else {
        values.set( setting.id.str(), value );
    }
}

WORLD *active_world()
{
    return world_generator ? world_generator->active_world : nullptr;
}

[[noreturn]] void throw_at( const JsonObject &jo, const std::string &member,
                            const std::string &message )
{
    if( jo.has_member( member ) ) {
        jo.throw_error_at( member, message );
    }
    jo.throw_error( message );
}
} // namespace

/** @relates string_id */
template<>
const mod_setting &string_id<mod_setting>::obj() const
{
    return mod_setting_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<mod_setting>::is_valid() const
{
    return mod_setting_factory.is_valid( *this );
}

// mod_setting_values

const mod_setting_value *mod_setting_values::find( const std::string &id ) const
{
    const auto it = values.find( id );
    return it == values.end() ? nullptr : &it->second;
}

void mod_setting_values::set( const std::string &id, const mod_setting_value &value )
{
    values[id] = value;
}

void mod_setting_values::erase( const std::string &id )
{
    values.erase( id );
}

void mod_setting_values::clear()
{
    values.clear();
}

bool mod_setting_values::empty() const
{
    return values.empty();
}

void mod_setting_values::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    for( const auto &[id, value] : values ) {
        jsout.member( id );
        std::visit( [&jsout]( const auto & v ) {
            jsout.write( v );
        }, value );
    }
    jsout.end_object();
}

void mod_setting_values::deserialize( const JsonObject &jo )
{
    values.clear();
    for( const JsonMember &member : jo ) {
        if( member.test_bool() ) {
            values[member.name()] = member.get_bool();
        } else if( member.test_number() ) {
            values[member.name()] = member.get_float();
        } else if( member.test_string() ) {
            values[member.name()] = member.get_string();
        }
        // anything else isn't a setting value and is dropped
    }
}

// mod_setting

void mod_setting::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "name", name );
    optional( jo, was_loaded, "description", description );
    optional( jo, was_loaded, "group", group );
    optional( jo, was_loaded, "changeable_in_game", changeable_in_game, true );
    optional( jo, was_loaded, "eoc_writable", eoc_writable, false );

    // kind-specific members start afresh for a new setting, or one whose kind an override changes
    bool new_kind = !was_loaded;
    if( !was_loaded || jo.has_member( "kind" ) ) {
        const std::string kind_str = jo.get_string( "kind" );
        const auto found = std::find_if( kind_names().begin(), kind_names().end(),
        [&kind_str]( const std::pair<std::string, kind_t> &k ) {
            return k.first == kind_str;
        } );
        if( found == kind_names().end() ) {
            jo.throw_error_at( "kind", string_format(
                                   "unknown kind \"%s\": a mod setting is a bool, number, choice or text", kind_str ) );
        }
        new_kind = new_kind || found->second != kind;
        kind = found->second;
    }

    switch( kind ) {
        case kind_t::boolean:
            if( jo.has_member( "default" ) ) {
                default_value = jo.get_bool( "default" );
            } else if( new_kind ) {
                default_value = false;
            }
            break;
        case kind_t::number:
            mandatory( jo, !new_kind, "min", min );
            mandatory( jo, !new_kind, "max", max );
            optional( jo, !new_kind, "step", step, 1.0 );
            if( jo.has_member( "default" ) ) {
                default_value = jo.get_float( "default" );
            } else if( new_kind ) {
                default_value = min;
            }
            break;
        case kind_t::choice:
            if( jo.has_array( "choices" ) ) {
                choices.clear();
                for( const JsonObject cjo : jo.get_array( "choices" ) ) {
                    choice_t c;
                    mandatory( cjo, false, "id", c.id );
                    mandatory( cjo, false, "name", c.name );
                    choices.push_back( c );
                }
            } else if( new_kind ) {
                jo.throw_error( "a choice mod setting needs \"choices\"" );
            }
            if( jo.has_member( "default" ) ) {
                default_value = jo.get_string( "default" );
            } else if( new_kind && !choices.empty() ) {
                default_value = choices.front().id;
            }
            break;
        case kind_t::text:
            optional( jo, !new_kind, "max_length", max_length, max_text_length );
            if( jo.has_member( "default" ) ) {
                default_value = jo.get_string( "default" );
            } else if( new_kind ) {
                default_value = std::string();
            }
            break;
    }

    // what the setting ends up as, whichever object gave each part
    switch( kind ) {
        case kind_t::boolean:
            break;
        case kind_t::number: {
            if( !( step > 0.0 ) ) {
                throw_at( jo, "step", "the step of a number mod setting must be more than 0" );
            }
            if( !( max > min ) ) {
                throw_at( jo, "max", string_format(
                              "the max of a number mod setting (%g) must be more than its min (%g)", max, min ) );
            }
            const double def = std::get<double>( default_value );
            const double steps = ( def - min ) / step;
            if( def < min || def > max || std::fabs( steps - std::round( steps ) ) > 1e-6 ) {
                throw_at( jo, "default", string_format(
                              "the default %g isn't between %g and %g on a step of %g", def, min, max, step ) );
            }
            break;
        }
        case kind_t::choice: {
            if( choices.empty() ) {
                throw_at( jo, "choices", "a choice mod setting needs at least one choice" );
            }
            std::set<std::string> ids;
            for( const choice_t &c : choices ) {
                if( c.id.empty() ) {
                    throw_at( jo, "choices", "a choice's id can't be empty" );
                }
                if( !ids.insert( c.id ).second ) {
                    throw_at( jo, "choices", string_format( "the choice id \"%s\" is used twice", c.id ) );
                }
            }
            if( choice_index( std::get<std::string>( default_value ) ) < 0 ) {
                throw_at( jo, "default", string_format( "the default \"%s\" isn't one of the choices",
                                                        std::get<std::string>( default_value ) ) );
            }
            break;
        }
        case kind_t::text: {
            if( max_length < 1 || max_length > max_text_length ) {
                throw_at( jo, "max_length", string_format(
                              "the max_length of a text mod setting must be between 1 and %d", max_text_length ) );
            }
            const std::string &def = std::get<std::string>( default_value );
            if( one_line( def ) != def ) {
                throw_at( jo, "default", "the default of a text mod setting must be one line" );
            }
            if( character_count( def ) > max_length ) {
                throw_at( jo, "default", string_format(
                              "the default is longer than the max_length of %d characters", max_length ) );
            }
            break;
        }
    }
}

void mod_setting::check() const
{
}

mod_id mod_setting::owner() const
{
    if( src.empty() ) {
        return mod_id();
    }
    // mod_interactions files load as "mod#other mod"
    const std::string &first = src.front().second.str();
    return mod_id( first.substr( 0, first.find( '#' ) ) );
}

std::string mod_setting::kind_name() const
{
    for( const std::pair<std::string, kind_t> &k : kind_names() ) {
        if( k.second == kind ) {
            return k.first;
        }
    }
    return "";
}

int mod_setting::choice_index( std::string_view choice_id ) const
{
    for( size_t i = 0; i < choices.size(); ++i ) {
        if( choices[i].id == choice_id ) {
            return static_cast<int>( i );
        }
    }
    return -1;
}

std::optional<mod_setting_value> mod_setting::validate( const mod_setting_value &value,
        std::string *error ) const
{
    const auto fail = [&]( const std::string & why ) -> std::optional<mod_setting_value> {
        if( error != nullptr )
        {
            *error = why;
        }
        return std::nullopt;
    };
    switch( kind ) {
        case kind_t::boolean:
            if( const bool *b = std::get_if<bool>( &value ) ) {
                return *b;
            }
            if( const double *d = std::get_if<double>( &value ) ) {
                return *d != 0.0;
            }
            if( const std::string *s = std::get_if<std::string>( &value ) ) {
                if( *s == "true" || *s == "false" ) {
                    return *s == "true";
                }
            }
            return fail( string_format( "mod setting \"%s\" is a switch: true or false, or 1 or 0",
                                        id.str() ) );
        case kind_t::number: {
            const double *d = std::get_if<double>( &value );
            if( d == nullptr || std::isnan( *d ) ) {
                return fail( string_format( "mod setting \"%s\" is a number", id.str() ) );
            }
            const double clamped = std::clamp( *d, min, max );
            const double max_steps = std::floor( ( max - min ) / step + 1e-9 );
            const double steps = std::clamp( std::round( ( clamped - min ) / step ), 0.0, max_steps );
            const int digits = std::max( decimals_of( step ), decimals_of( min ) );
            return round_to( min + steps * step, digits );
        }
        case kind_t::choice: {
            if( const std::string *s = std::get_if<std::string>( &value ) ) {
                if( choice_index( *s ) >= 0 ) {
                    return *s;
                }
                return fail( string_format( "\"%s\" isn't a choice of mod setting \"%s\"", *s,
                                            id.str() ) );
            }
            if( const double *d = std::get_if<double>( &value ) ) {
                // a choice by its index, as math reads it
                if( *d >= 0 && *d < static_cast<double>( choices.size() ) && *d == std::floor( *d ) ) {
                    return choices[static_cast<size_t>( *d )].id;
                }
                return fail( string_format( "%g isn't the index of a choice of mod setting \"%s\"", *d,
                                            id.str() ) );
            }
            return fail( string_format( "mod setting \"%s\" is a choice: a choice id or its index",
                                        id.str() ) );
        }
        case kind_t::text:
            if( const std::string *s = std::get_if<std::string>( &value ) ) {
                return first_characters( one_line( *s ), max_length );
            }
            return fail( string_format( "mod setting \"%s\" is text", id.str() ) );
    }
    return fail( "unknown kind of mod setting" );
}

mod_setting_value mod_setting::sanitize( const mod_setting_value &value ) const
{
    return validate( value ).value_or( default_value );
}

bool mod_setting::is_default( const mod_setting_value &value ) const
{
    return same_value( value, default_value, step );
}

std::optional<double> mod_setting::as_number( const mod_setting_value &value ) const
{
    const mod_setting_value v = sanitize( value );
    switch( kind ) {
        case kind_t::boolean:
            return std::get<bool>( v ) ? 1.0 : 0.0;
        case kind_t::number:
            return std::get<double>( v );
        case kind_t::choice:
            return choice_index( std::get<std::string>( v ) );
        case kind_t::text:
            return std::nullopt;
    }
    return std::nullopt;
}

std::string mod_setting::as_text( const mod_setting_value &value ) const
{
    const mod_setting_value v = sanitize( value );
    if( const bool *b = std::get_if<bool>( &v ) ) {
        return *b ? "true" : "false";
    }
    if( const double *d = std::get_if<double>( &v ) ) {
        return format_number( *d, std::max( decimals_of( step ), decimals_of( min ) ) );
    }
    return std::get<std::string>( v );
}

mod_setting_value mod_setting::stepped( const mod_setting_value &value, int direction ) const
{
    const mod_setting_value current = sanitize( value );
    switch( kind ) {
        case kind_t::boolean:
            return !std::get<bool>( current );
        case kind_t::number: {
            // in steps from min, in double, so the last step reaches max
            const double max_steps = std::floor( ( max - min ) / step + 1e-9 );
            const double steps = std::round( ( std::get<double>( current ) - min ) / step ) + direction;
            const double wrapped = steps > max_steps ? 0.0 : steps < 0.0 ? max_steps : steps;
            return sanitize( min + wrapped * step );
        }
        case kind_t::choice: {
            const int count = static_cast<int>( choices.size() );
            const int index = ( choice_index( std::get<std::string>( current ) ) + direction + count ) %
                              count;
            return choices[index].id;
        }
        case kind_t::text:
            return current;
    }
    return current;
}

// mod_settings

void mod_settings::load( const JsonObject &jo, const std::string &src )
{
    // An id another mod defined can only be changed with copy-from, as other types are
    if( jo.has_string( "id" ) && !jo.has_member( "copy-from" ) ) {
        const mod_setting_id id( jo.get_string( "id" ) );
        if( mod_setting_factory.is_valid( id ) ) {
            jo.throw_error_at( "id", string_format(
                                   "mod setting \"%s\" is already defined by %s.  To change it, use \"copy-from\": \"%s\"",
                                   id.str(), id->owner().str(), id.str() ) );
        }
    }
    mod_setting_factory.load( jo, src );
}

void mod_settings::finalize_all()
{
    mod_setting_factory.finalize();
}

void mod_settings::check_consistency()
{
    mod_setting_factory.check();
}

void mod_settings::reset()
{
    mod_setting_factory.reset();
    saved_values.reset();
}

bool mod_settings::have_unsaved_changes()
{
    return saved_values.has_value();
}

void mod_settings::mark_saved()
{
    saved_values.reset();
}

const mod_setting_values &mod_settings::values_to_save( const WORLD &world )
{
    if( saved_values && &world == active_world() ) {
        return *saved_values;
    }
    return world.mod_settings;
}

bool mod_settings::save_with_game()
{
    WORLD *world = active_world();
    if( world == nullptr || !saved_values ) {
        return true;
    }
    std::optional<mod_setting_values> before = std::move( saved_values );
    saved_values.reset();
    if( world->save() ) {
        return true;
    }
    saved_values = std::move( before );
    return false;
}

const std::vector<mod_setting> &mod_settings::get_all()
{
    return mod_setting_factory.get_all();
}

mod_setting_value mod_settings::get( const mod_setting &setting )
{
    const WORLD *world = active_world();
    const mod_setting_value *value = world ? world->mod_settings.find( setting.id.str() ) : nullptr;
    return value ? setting.sanitize( *value ) : setting.default_value;
}

std::string mod_settings::set( const mod_setting &setting, const mod_setting_value &value )
{
    WORLD *world = active_world();
    if( world == nullptr ) {
        return string_format( "can't set mod setting \"%s\" with no world loaded", setting.id.str() );
    }
    std::string error;
    const std::optional<mod_setting_value> valid = setting.validate( value, &error );
    if( !valid ) {
        return error;
    }
    const mod_setting_value old = get( setting );
    mod_setting_values after = world->mod_settings;
    store( after, setting, *valid );
    if( after != world->mod_settings ) {
        // the first change since the last game save: the file keeps what was saved
        if( !saved_values ) {
            saved_values = world->mod_settings;
        }
        world->mod_settings = std::move( after );
    }
    // the world's values are set before the event, so its EOCs read the new one
    if( !same_value( old, *valid, setting.step ) ) {
        get_event_bus().send<event_type::mod_setting_changed>( setting.id.str(),
                setting.as_text( old ), setting.as_text( *valid ) );
    }
    return "";
}

std::string mod_settings::section::title() const
{
    const std::string mod_name = mod.is_valid() ? mod->name() : mod.str();
    if( group.empty() ) {
        return mod_name;
    }
    //~ A mod's settings that its JSON puts in a group: the mod's name, then the group's
    return string_format( pgettext( "mod settings", "%1$s: %2$s" ), mod_name, group.translated() );
}

std::vector<mod_settings::section> mod_settings::make_sections(
    const std::vector<const mod_setting *> &settings )
{
    std::vector<mod_id> mods;
    for( const mod_setting *s : settings ) {
        if( std::find( mods.begin(), mods.end(), s->owner() ) == mods.end() ) {
            mods.push_back( s->owner() );
        }
    }
    std::vector<section> ret;
    for( const mod_id &mod : mods ) {
        // the ungrouped settings, then each group in the order it first appears
        std::vector<std::string> groups( 1 );
        for( const mod_setting *s : settings ) {
            const std::string group = s->group.translated();
            if( s->owner() == mod && std::find( groups.begin(), groups.end(), group ) == groups.end() ) {
                groups.push_back( group );
            }
        }
        for( const std::string &group : groups ) {
            section sec;
            sec.mod = mod;
            for( const mod_setting *s : settings ) {
                if( s->owner() == mod && s->group.translated() == group ) {
                    sec.group = s->group;
                    sec.settings.push_back( s );
                }
            }
            if( !sec.settings.empty() ) {
                ret.push_back( sec );
            }
        }
    }
    return ret;
}

// mod_settings_editor

mod_settings_editor::mod_settings_editor()
{
    std::vector<const mod_setting *> settings;
    for( const mod_setting &s : mod_settings::get_all() ) {
        settings.push_back( &s );
    }
    sections_ = mod_settings::make_sections( settings );
}

mod_settings_editor::mod_settings_editor( const std::vector<const mod_setting *> &settings,
        mod_setting_values &values ) : sections_( mod_settings::make_sections( settings ) ),
    values_( &values )
{
}

std::string mod_settings_editor::locked_reason( const mod_setting &setting ) const
{
    if( in_game() && !setting.changeable_in_game ) {
        return _( "This setting is chosen when the world is created.  Its mod doesn't let it change "
                  "afterwards." );
    }
    return "";
}

mod_setting_value mod_settings_editor::get( const mod_setting &setting ) const
{
    if( values_ == nullptr ) {
        return mod_settings::get( setting );
    }
    const mod_setting_value *value = values_->find( setting.id.str() );
    return value ? setting.sanitize( *value ) : setting.default_value;
}

std::string mod_settings_editor::set( const mod_setting &setting, const mod_setting_value &value )
{
    if( values_ == nullptr ) {
        return mod_settings::set( setting, value );
    }
    std::string error;
    const std::optional<mod_setting_value> valid = setting.validate( value, &error );
    if( !valid ) {
        return error;
    }
    store( *values_, setting, *valid );
    return "";
}

// world creation

const mod_setting *mod_settings_preview::find( std::string_view id ) const
{
    for( const mod_setting &s : settings ) {
        if( s.id.str() == id ) {
            return &s;
        }
    }
    return nullptr;
}

std::vector<const mod_setting *> mod_settings_preview::all_settings() const
{
    std::vector<const mod_setting *> ret;
    ret.reserve( settings.size() );
    for( const mod_setting &s : settings ) {
        ret.push_back( &s );
    }
    return ret;
}

namespace
{
// Whether a data file can hold an object read for world creation.  Reading the text is much
// cheaper than parsing it, and most files hold none.
bool may_hold_settings( const cata_path &file )
{
    const std::string text = read_entire_file( file.get_unrelative_path() );
    return text.find( "mod_setting" ) != std::string::npos ||
           text.find( "option_slider" ) != std::string::npos ||
           text.find( "WORLD_OPTION" ) != std::string::npos;
}

// Reads the objects of a preview from the files of the selected mods, as the loader would load
// them: copy-from waits for its parent, a second definition of an id needs copy-from.
class preview_builder
{
    public:
        mod_settings_preview preview;

        void read_file( const cata_path &file, const std::string &src ) {
            try {
                const JsonValue jsin = json_loader::from_path( file );
                DynamicDataLoader::for_each_object( jsin, [&]( const JsonObject & jo ) {
                    read_object( jo, src );
                } );
            } catch( const JsonError &err ) {
                preview.errors.emplace_back( err.what() );
            }
        }

        // Reads the objects whose copy-from parent came later
        void finish() {
            while( !deferred.empty() ) {
                std::vector<std::pair<JsonObject, std::string>> waiting;
                waiting.swap( deferred );
                for( const std::pair<JsonObject, std::string> &w : waiting ) {
                    read_object( w.first, w.second );
                }
                if( deferred.size() == waiting.size() ) {
                    for( const std::pair<JsonObject, std::string> &w : deferred ) {
                        w.first.allow_omitted_members();
                        preview.errors.push_back( string_format(
                                                      "%s \"%s\" (from %s) copies from \"%s\", which doesn't exist",
                                                      w.first.get_string( "type" ), w.first.get_string( "id", "" ), w.second,
                                                      w.first.get_string( "copy-from" ) ) );
                    }
                    deferred.clear();
                }
            }
        }

    private:
        std::vector<std::pair<JsonObject, std::string>> deferred;
        std::unordered_map<std::string, mod_setting> setting_abstracts;

        void read_object( const JsonObject &jo, const std::string &src ) {
            if( !jo.has_string( "type" ) ) {
                jo.allow_omitted_members();
                return;
            }
            const std::string type = jo.get_string( "type" );
            try {
                if( type == "mod_setting" ) {
                    read_setting( jo, src );
                } else if( type == "option_slider" ) {
                    read_slider( jo, src );
                } else if( type == "WORLD_OPTION" ) {
                    const std::pair<std::string, std::string> option = read_world_option( jo );
                    preview.world_option_defaults.insert_or_assign( option.first, option.second );
                } else {
                    jo.allow_omitted_members();
                }
            } catch( const JsonError &err ) {
                jo.allow_omitted_members();
                preview.errors.emplace_back( err.what() );
            }
        }

        mod_setting *find_setting( const std::string &id ) {
            for( mod_setting &s : preview.settings ) {
                if( s.id.str() == id ) {
                    return &s;
                }
            }
            return nullptr;
        }

        void read_setting( const JsonObject &jo, const std::string &src ) {
            mod_setting def;
            if( jo.has_string( "copy-from" ) ) {
                const std::string parent = jo.get_string( "copy-from" );
                const mod_setting *base = find_setting( parent );
                const auto abstract = setting_abstracts.find( parent );
                if( base != nullptr ) {
                    def = *base;
                } else if( abstract != setting_abstracts.end() ) {
                    def = abstract->second;
                } else {
                    jo.allow_omitted_members();
                    deferred.emplace_back( jo, src );
                    return;
                }
                def.was_loaded = true;
            }
            if( jo.has_string( "abstract" ) ) {
                const std::string abstract_id = jo.get_string( "abstract" );
                def.id = mod_setting_id( abstract_id );
                def.load( jo, src );
                setting_abstracts[abstract_id] = def;
                return;
            }
            const std::string id = jo.get_string( "id" );
            mod_setting *existing = find_setting( id );
            if( existing != nullptr && !jo.has_member( "copy-from" ) ) {
                jo.throw_error_at( "id", string_format(
                                       "mod setting \"%s\" is already defined by %s.  To change it, use \"copy-from\": \"%s\"",
                                       id, existing->owner().str(), id ) );
            }
            if( existing != nullptr && !existing->src.empty() &&
                existing->src.back().second.str() == src ) {
                jo.throw_error_at( "id", string_format(
                                       "%s (mod setting) has two definitions from the same source (%s)!", id, src ) );
            }
            if( def.id.str() != id ) {
                def.src.clear();
            }
            def.id = mod_setting_id( id );
            def.src.emplace_back( def.id, mod_id( src ) );
            def.load( jo, src );
            if( existing != nullptr ) {
                *existing = def;
            } else {
                preview.settings.push_back( def );
            }
        }

        void read_slider( const JsonObject &jo, const std::string &src ) {
            option_slider def;
            if( jo.has_string( "copy-from" ) ) {
                const option_slider_id parent( jo.get_string( "copy-from" ) );
                const auto base = std::find_if( preview.sliders.begin(), preview.sliders.end(),
                [&parent]( const option_slider & s ) {
                    return s.id == parent;
                } );
                if( base != preview.sliders.end() ) {
                    def = *base;
                } else if( parent.is_valid() ) {
                    // core's, loaded at the main menu
                    def = parent.obj();
                } else {
                    jo.allow_omitted_members();
                    deferred.emplace_back( jo, src );
                    return;
                }
                def.was_loaded = true;
            }
            def.id = option_slider_id( jo.get_string( "id" ) );
            def.load( jo, src );
            def.finalize();
            const auto existing = std::find_if( preview.sliders.begin(), preview.sliders.end(),
            [&def]( const option_slider & s ) {
                return s.id == def.id;
            } );
            if( existing != preview.sliders.end() ) {
                *existing = def;
            } else {
                preview.sliders.push_back( def );
            }
        }
};
} // namespace

const mod_settings_reader::mod_files &mod_settings_reader::files_of( const mod_id &mod )
{
    const auto found = scanned.find( mod );
    if( found != scanned.end() ) {
        return found->second;
    }
    mod_files &files = scanned[mod];
    for( const cata_path &file : DynamicDataLoader::mod_data_files( mod->path ) ) {
        if( may_hold_settings( file ) ) {
            files.data.push_back( file );
        }
    }
    // the interactions with every mod; read() keeps those with selected mods
    const std::vector<mod_id> every_mod = world_generator->get_mod_manager().all_mods();
    for( const std::pair<mod_id, cata_path> &file : DynamicDataLoader::mod_interaction_files(
             mod->path / "mod_interactions", every_mod ) ) {
        if( may_hold_settings( file.second ) ) {
            files.interactions.push_back( file );
        }
    }
    return files;
}

mod_settings_preview mod_settings_reader::read( const std::vector<mod_id> &mods )
{
    preview_builder builder;
    // in the order the loader loads them: every mod's files, then their mod_interactions
    for( const mod_id &mod : mods ) {
        if( !mod.is_valid() ) {
            continue;
        }
        for( const cata_path &file : files_of( mod ).data ) {
            builder.read_file( file, mod.str() );
        }
    }
    for( const mod_id &mod : mods ) {
        if( !mod.is_valid() ) {
            continue;
        }
        for( const std::pair<mod_id, cata_path> &file : files_of( mod ).interactions ) {
            if( std::find( mods.begin(), mods.end(), file.first ) != mods.end() ) {
                builder.read_file( file.second, string_format( "%s#%s", mod.str(), file.first.str() ) );
            }
        }
    }
    builder.finish();
    return std::move( builder.preview );
}

mod_settings_preview pre_read_mod_settings( const std::vector<mod_id> &mods )
{
    return mod_settings_reader().read( mods );
}

void update_world_mod_settings( WORLD &world, const mod_settings_preview &before,
                                const mod_settings_preview &after )
{
    for( const mod_setting &s : before.settings ) {
        if( after.find( s.id.str() ) == nullptr ) {
            world.mod_settings.erase( s.id.str() );
        }
    }
    get_options().apply_world_option_defaults( world.WORLD_OPTIONS, after.world_option_defaults );
}

std::vector<const option_slider *> preview_sliders( const mod_settings_preview &preview,
        const std::string &context )
{
    const auto from_preview = [&preview]( const option_slider_id & id ) -> const option_slider * {
        for( const option_slider &s : preview.sliders )
        {
            if( s.id == id ) {
                return &s;
            }
        }
        return nullptr;
    };
    std::vector<const option_slider *> ret;
    for( const option_slider &core : option_slider::get_all() ) {
        const option_slider *changed = from_preview( core.id );
        const option_slider *slider = changed != nullptr ? changed : &core;
        if( slider->context() == context ) {
            ret.push_back( slider );
        }
    }
    for( const option_slider &s : preview.sliders ) {
        if( !s.id.is_valid() && s.context() == context ) {
            ret.push_back( &s );
        }
    }
    return ret;
}

std::pair<std::string, std::string> read_world_option( const JsonObject &jo )
{
    if( jo.has_member( "options" ) ) {
        jo.throw_error_at( "options",
                           "WORLD_OPTION takes \"name\" and \"default\": it changes the default of a world "
                           "option instead of forcing it on (doc/JSON/MOD_SETTINGS.md)" );
    }
    const std::string name = jo.get_string( "name" );
    options_manager &opts = get_options();
    if( !opts.has_option( name ) ) {
        jo.throw_error_at( "name", string_format( "there's no option \"%s\"", name ) );
    }
    const std::string type = opts.get_option( name ).getType();
    std::string value;
    if( type == "bool" ) {
        value = jo.get_bool( "default" ) ? "true" : "false";
    } else if( type == "int" || type == "int_map" ) {
        value = std::to_string( jo.get_int( "default" ) );
    } else if( type == "float" ) {
        value = format_number( jo.get_float( "default" ), 6 );
    } else {
        value = jo.get_string( "default" );
    }
    const std::string error = opts.world_option_value_error( name, value );
    if( !error.empty() ) {
        jo.throw_error_at( jo.has_member( "default" ) ? "default" : "name", error );
    }
    return { name, value };
}

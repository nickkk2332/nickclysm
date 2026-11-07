#include "speech.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "rng.h"
#include "string_formatter.h"

namespace
{
generic_factory<speech_line> speech_factory( "speech" );
// How many lines without an id have loaded, to name each one
int anonymous_lines = 0;
} // namespace

static SpeechBubble nullSpeech = { no_translation( "INVALID SPEECH" ), 0 };

// A copy (or a later mod's "copy-from" of the same id) keeps every member it doesn't name
// (DATA-J02).
void speech_line::load( const JsonObject &jo, std::string_view )
{
    optional( jo, was_loaded, "speaker", speakers, string_reader{} );
    optional( jo, was_loaded, "sound", bubble.text );
    mandatory( jo, was_loaded, "volume", bubble.volume );
}

void load_speech( const JsonObject &jo, const std::string &src )
{
    if( jo.has_member( "id" ) || jo.has_member( "abstract" ) ) {
        speech_factory.load( jo, src );
        return;
    }
    if( jo.has_member( "copy-from" ) ) {
        jo.throw_error_at( "copy-from", "a speech line needs an \"id\" to copy from another" );
    }
    // Without an id, as speech always was: added, and no later JSON can change it.
    speech_line anonymous;
    anonymous.id = string_id<speech_line>( string_format( "<speech without id %d>",
                                           anonymous_lines++ ) );
    anonymous.load( jo, src );
    speech_factory.insert( anonymous );
}

void finalize_speech()
{
    speech_factory.finalize();
}

void reset_speech()
{
    speech_factory.reset();
    anonymous_lines = 0;
}

const SpeechBubble &get_speech( const std::string &label )
{
    // Each speaker's lines, in the order they were first defined; rebuilt when the factory
    // changes.
    static std::map<std::string, std::vector<SpeechBubble>> by_speaker;
    static generic_factory<speech_line>::Version built;
    if( !speech_factory.is_valid( built ) ) {
        by_speaker.clear();
        for( const speech_line &line : speech_factory.get_all() ) {
            for( const std::string &speaker : line.speakers ) {
                by_speaker[speaker].push_back( line.bubble );
            }
        }
        built = speech_factory.get_version();
    }

    const auto speech_type = by_speaker.find( label );
    if( speech_type == by_speaker.end() || speech_type->second.empty() ) {
        // Bad lookup, return a fake sound, also warn?
        return nullSpeech;
    }

    return random_entry_ref( speech_type->second );
}

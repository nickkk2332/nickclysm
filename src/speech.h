#pragma once
#ifndef CATA_SRC_SPEECH_H
#define CATA_SRC_SPEECH_H

#include <set>
#include <string>
#include <string_view>

#include "string_id.h"
#include "translation.h"

class JsonObject;

struct SpeechBubble {
    translation text;
    int volume = 0;
};

/** One "speech" object: a line that each of its speakers can say. */
struct speech_line {
    // A line without an "id" in its JSON gets one no JSON can name, so it can't be changed.
    string_id<speech_line> id;
    bool was_loaded = false;
    std::set<std::string> speakers;
    SpeechBubble bubble;

    /** Called by the speech factory: reads one definition, or the members a copy changes */
    void load( const JsonObject &jo, std::string_view src );
};

/** Loads one "speech" object: through the speech factory if it has an id or is abstract. */
void load_speech( const JsonObject &jo, const std::string &src );
/** Loads the lines whose "copy-from" parent was defined later */
void finalize_speech();
void reset_speech();
const SpeechBubble &get_speech( const std::string &label );

#endif // CATA_SRC_SPEECH_H

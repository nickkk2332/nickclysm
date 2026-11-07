#pragma once
#ifndef CATA_SRC_AMMO_H
#define CATA_SRC_AMMO_H

#include <optional>
#include <string>

#include "translation.h"
#include "type_id.h"

class JsonObject;

// How a gun sounds when it fires (CMBT-J09): the gun's "fire_sound", else that of the first of
// its ammunition types (by id) that has one, else the usual sound for its loudness and ammo effects
struct gun_fire_sound {
    // A fixed volume: the gun's and ammo's loudness and its mods (suppressors) don't change it.
    // Unset: the loudness.
    std::optional<int> volume;
    // What is heard; empty: the usual sound for the volume and the ammo's effects
    translation message;
    // What a burst sounds like; empty: message
    translation burst_message;

    void load( const JsonObject &jo );
};

class ammunition_type
{
        friend class DynamicDataLoader;
    public:
        ammunition_type();

        std::string name() const;

        const itype_id &default_ammotype() const {
            return default_ammotype_;
        }

        const std::optional<gun_fire_sound> &fire_sound() const {
            return fire_sound_;
        }
        // Firing a gun of this type satisfies a pyromaniac (CMBT-J09)
        bool satisfies_pyromania() const {
            return satisfies_pyromania_;
        }
        // What the pyromaniac feels then, %s being the gun; empty: the usual message
        const translation &pyromania_message() const {
            return pyromania_message_;
        }

    private:
        translation name_;
        itype_id default_ammotype_;
        std::optional<gun_fire_sound> fire_sound_;
        bool satisfies_pyromania_ = false;
        translation pyromania_message_;

        static void load_ammunition_type( const JsonObject &jsobj );
        static void reset();
        static void check_consistency();
};

#endif // CATA_SRC_AMMO_H

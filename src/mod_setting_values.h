#pragma once
#ifndef CATA_SRC_MOD_SETTING_VALUES_H
#define CATA_SRC_MOD_SETTING_VALUES_H

#include <map>
#include <string>
#include <variant>

class JsonObject;
class JsonOut;

// The value of a mod setting (doc/JSON/MOD_SETTINGS.md): a switch, a number, or the id of a
// choice or a text
using mod_setting_value = std::variant<bool, double, std::string>;

/**
 * A world's mod setting values, by setting id.  Only values that differ from the setting's
 * default are kept, so a mod that changes a default changes it for every world that never set
 * the setting.  Values of settings no loaded mod defines stay, for when the mod is back.
 */
class mod_setting_values
{
    public:
        const mod_setting_value *find( const std::string &id ) const;
        void set( const std::string &id, const mod_setting_value &value );
        void erase( const std::string &id );
        void clear();
        bool empty() const;
        const std::map<std::string, mod_setting_value> &all() const {
            return values;
        }

        // A JSON object of setting id to value
        void serialize( JsonOut &jsout ) const;
        void deserialize( const JsonObject &jo );

        bool operator==( const mod_setting_values &rhs ) const {
            return values == rhs.values;
        }
        bool operator!=( const mod_setting_values &rhs ) const {
            return !operator==( rhs );
        }
    private:
        std::map<std::string, mod_setting_value> values;
};

#endif // CATA_SRC_MOD_SETTING_VALUES_H

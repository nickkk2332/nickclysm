#pragma once
#ifndef CATA_SRC_GLOBAL_VARS_H
#define CATA_SRC_GLOBAL_VARS_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

#include "math_parser_diag_value.h"

#include "json.h"

// The kinds of saved variables a var_migration can apply to
enum class var_scope : int {
    global,
    character,
    monster,
    item,
    vehicle,
    computer
};

// A var_migration: renames or erases saved dialogue/EOC variables when a save loads
struct var_migration {
    // The new name; none: the variable is erased
    std::optional<std::string> to;
    // "from" is a prefix: every variable whose name starts with it is renamed (keeping the rest
    // of its name) or erased
    bool prefix = false;
    // The kinds of variables it applies to; empty: all of them
    std::set<var_scope> scopes;

    bool applies_to( var_scope scope ) const {
        return scopes.empty() || scopes.count( scope ) != 0;
    }
};

class global_variables
{
    public:
        using impl_t = std::unordered_map<std::string, diag_value>;

        // Methods for setting/getting misc key/value pairs.
        void set_global_value( const std::string &key, diag_value value ) {
            global_values[ key ] = std::move( value );
        }

        template <typename... Args>
        void set_global_value( const std::string &key, Args... args ) {
            set_global_value( key, diag_value{ std::forward<Args>( args )... } );
        }

        void remove_global_value( const std::string &key ) {
            global_values.erase( key );
        }

        diag_value const *maybe_get_global_value( const std::string &key ) const {
            return _common_maybe_get_value( key, global_values );
        }

        diag_value const &get_global_value( const std::string &key ) const {
            return _common_get_value( key, global_values );
        }

        static diag_value const *_common_maybe_get_value( const std::string &key, const impl_t &cont ) {
            auto it = cont.find( key );
            return it == cont.end() ? nullptr : &it->second;
        }

        static diag_value const &_common_get_value( const std::string &key, const impl_t &cont ) {
            static diag_value const null_val;
            diag_value const *ret = _common_maybe_get_value( key, cont );
            return ret ? *ret : null_val;
        }

        impl_t &get_global_values() {
            return global_values;
        }

        impl_t const &get_global_values() const {
            return global_values;
        }

        void clear_global_values() {
            global_values.clear();
        }

        void set_global_values( impl_t input ) {
            global_values = std::move( input );
        }
        void unserialize( const JsonObject &jo );
        void serialize( JsonOut &jsout ) const;

        // var_migration entries by their "from"; a later one with the same "from" replaces it
        std::map<std::string, var_migration> migrations; // NOLINT(cata-serialize)
        static void load_migrations( const JsonObject &jo, std::string_view src );
        // The variables a watched_variable entry names: writing one sends var_changed
        std::set<std::string> watched; // NOLINT(cata-serialize)
        static void load_watched( const JsonObject &jo, std::string_view src );
        // Renames or erases the variables in @p vars as the var_migrations for @p scope say
        void migrate_vars( impl_t &vars, var_scope scope ) const;

    private:
        impl_t global_values;
};
global_variables &get_globals();

#endif // CATA_SRC_GLOBAL_VARS_H

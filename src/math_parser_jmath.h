#pragma once
#ifndef CATA_SRC_MATH_PARSER_JMATH_H
#define CATA_SRC_MATH_PARSER_JMATH_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "math_parser.h"
#include "type_id.h"

class JsonObject;
struct const_dialogue;
struct dialogue;
struct diag_value;

struct jmath_func {
    jmath_func_id id;
    std::vector<std::pair<jmath_func_id, mod_id>> src;
    bool was_loaded = false;
    int num_params{};
    // the ids the function names may not be loaded (another mod's), so aren't checked (MATH-J04)
    bool optional_ids = false;

    double eval( const_dialogue const &d ) const;
    double eval( const_dialogue const &d, std::vector<double> const &params ) const;
    // MATH-J11: the arguments may be strings or arrays; with scope 'n' the body runs with u and
    // n swapped, so n_my_func() is my_func() seen from the beta talker
    double eval( const_dialogue const &d, std::vector<diag_value> const &params, char scope ) const;
    // runs the "assign" body, with _val the value assigned
    void assign( dialogue &d, std::vector<diag_value> const &params, char scope, double val ) const;
    bool can_assign() const {
        return has_assign;
    }

    void load( const JsonObject &jo, std::string_view src );
    static void load_func( const JsonObject &jo, std::string const &src );
    static void finalize_all();
    static void reset();
    static const std::vector<jmath_func> &get_all();
    void finalize();

    mutable std::string _str;
    mutable math_exp _exp;
    bool has_assign = false;
    mutable std::string _assign_str;
    mutable math_exp _assign_exp;
};

#endif // CATA_SRC_MATH_PARSER_JMATH_H

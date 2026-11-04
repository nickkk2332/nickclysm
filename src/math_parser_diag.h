#pragma once
#ifndef CATA_SRC_MATH_PARSER_DIAG_H
#define CATA_SRC_MATH_PARSER_DIAG_H

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "math_parser_diag_value.h"

struct const_dialogue;
struct dialogue;

struct diag_kwargs {
    using impl_t = std::map<std::string, diag_value>;

    impl_t kwargs;

    template<typename T = std::monostate>
    diag_value kwarg_or( std::string const &key, T const &default_value = {} ) const {
        if( auto it = kwargs.find( key ); it != kwargs.end() ) {
            return it->second;
        }
        return diag_value{ default_value };
    }
};
struct dialogue_func {
    using fe_t = double( * )( const_dialogue const &, char, std::vector<diag_value> const &,
                              diag_kwargs const & );
    using fa_t = void( * )( double val, dialogue &, char, std::vector<diag_value> const &,
                            diag_kwargs const & );

    // Checks the arguments written as literals when the expression is parsed (unknown ones are
    // empty); throws math::syntax_error to reject them.
    using fc_t = void( * )( std::vector<diag_value> const & );

    using kwargs_t = std::vector<std::string_view>;

    dialogue_func( std::string_view sc_, int n_, fe_t fe_, fa_t fa_ = {}, kwargs_t kw_ = {},
                   fc_t fc_ = {}, fc_t fca_ = {} )
        : scopes( sc_ ), num_params( n_ ), fe( fe_ ), fa( fa_ ), kwargs( std::move( kw_ ) ),
          fc( fc_ ), fca( fca_ )
    {}
    std::string_view scopes;
    int num_params{};

    fe_t fe;
    fa_t fa;

    kwargs_t kwargs;

    fc_t fc;
    // Like fc, for when the function is the target of an assignment
    fc_t fca;
};
using pdiag_func = dialogue_func const *;

std::map<std::string_view, dialogue_func> const &get_all_diag_funcs();

#endif // CATA_SRC_MATH_PARSER_DIAG_H

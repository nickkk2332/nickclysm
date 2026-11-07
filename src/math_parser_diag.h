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

// An argument of a dialogue function written as a literal string that names an id, as in
// has_trait('FEEBLE'). Checked once all data has loaded, as nothing else would notice a typo.
struct diag_literal_id {
    // the function and what the id names, for messages: "has_trait", "trait"
    std::string_view func;
    std::string_view kind;
    std::string id;
    // whether the id names something loaded, or is a value the function takes instead ('ALL')
    bool ( *is_known )( std::string const & ) = nullptr;
};

// Appends to @p ids the literal id arguments of a call to the dialogue function @p func, as the
// parser has them (@p params and @p kwargs hold the literals; other arguments are empty).
void collect_literal_ids( std::string_view func, std::vector<diag_value> const &params,
                          diag_kwargs const &kwargs, std::vector<diag_literal_id> &ids );

// The dialogue functions whose literal arguments are checked as ids
std::vector<std::string_view> diag_funcs_with_literal_ids();

#endif // CATA_SRC_MATH_PARSER_DIAG_H

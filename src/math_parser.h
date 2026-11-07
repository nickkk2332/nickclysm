#pragma once
#ifndef CATA_SRC_MATH_PARSER_H
#define CATA_SRC_MATH_PARSER_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "math_parser_type.h"

struct dialogue;
struct const_dialogue;

class math_exp
{
    public:
        class math_exp_impl;

        ~math_exp();
        math_exp();
        math_exp( math_exp const &other );
        math_exp( math_exp &&/* other */ ) noexcept;
        math_exp &operator=( math_exp const &other );
        math_exp &operator=( math_exp &&/* other */ ) noexcept;
        explicit math_exp( math_exp_impl impl_ );

        bool parse( std::string_view str, bool handle_errors = true );
        double eval( dialogue &d ) const;
        double eval( const_dialogue const &d ) const;

        math_type_t get_type() const;

        // MATH-J04: a message for each id that a dialogue function names as a literal, as in
        // has_trait('FEEBLE'), and that names nothing loaded; ask once all data has loaded. The
        // ids are forgotten after: they need checking only once.
        std::vector<std::string> check_literal_ids();

    private:
        std::unique_ptr<math_exp_impl> impl;
};

#endif // CATA_SRC_MATH_PARSER_H

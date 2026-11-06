#pragma once
#ifndef CATA_SRC_POWER_MATH_H
#define CATA_SRC_POWER_MATH_H

#include "math_parser_diag.h"

namespace power_math
{
double resource( const const_dialogue &d, char scope, const std::vector<diag_value> &params,
                 const diag_kwargs &kwargs );
double cooldown( const const_dialogue &d, char scope, const std::vector<diag_value> &params,
                 const diag_kwargs &kwargs );
double application( const const_dialogue &d, char scope, const std::vector<diag_value> &params,
                    const diag_kwargs &kwargs );
double ability( const const_dialogue &d, char scope, const std::vector<diag_value> &params,
                const diag_kwargs &kwargs );
double summon( const const_dialogue &d, char scope, const std::vector<diag_value> &params,
               const diag_kwargs &kwargs );
double projection( const const_dialogue &d, char scope, const std::vector<diag_value> &params,
                   const diag_kwargs &kwargs );
double area( const const_dialogue &d, char scope, const std::vector<diag_value> &params,
             const diag_kwargs &kwargs );
} // namespace power_math

#endif // CATA_SRC_POWER_MATH_H

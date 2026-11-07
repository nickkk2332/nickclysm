#include "math_parser_jmath.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "math_parser.h"
#include "math_parser_diag.h"
#include "math_parser_diag_value.h"
#include "math_parser_func.h"
#include "math_parser_lex.h"
#include "math_parser_type.h"
#include "string_formatter.h"
#include "talker.h"

namespace
{
generic_factory<jmath_func> &get_all_jmath_func()
{
    static generic_factory<jmath_func> jmath_func_factory( "jmath_function" );
    return jmath_func_factory;
}
} // namespace

/** @relates string_id */
template <>
jmath_func const &string_id<jmath_func>::obj() const
{
    return get_all_jmath_func().obj( *this );
}

/** @relates string_id */
template <>
bool string_id<jmath_func>::is_valid() const
{
    return get_all_jmath_func().is_valid( *this );
}

void jmath_func::reset()
{
    get_all_jmath_func().reset();
}

std::vector<jmath_func> const &jmath_func::get_all()
{
    return get_all_jmath_func().get_all();
}

void jmath_func::load_func( const JsonObject &jo, std::string const &src )
{
    get_all_jmath_func().load( jo, src );
}

namespace
{
// MATH-J11: the highest positional parameter (_0, _1, ...) a body uses, or -1 for none
int highest_param( std::string_view body )
{
    int highest = -1;
    try {
        math::lexer lex( body );
        for( math::token const &tok : lex.tokens() ) {
            if( tok.type != math::token_t::id || tok.str.size() < 2 || tok.str[0] != '_' ) {
                continue;
            }
            int n = 0;
            bool digits = true;
            for( std::size_t i = 1; i < tok.str.size(); i++ ) {
                if( tok.str[i] < '0' || tok.str[i] > '9' ) {
                    digits = false;
                    break;
                }
                n = n * 10 + ( tok.str[i] - '0' );
            }
            if( digits && n > highest ) {
                highest = n;
            }
        }
    } catch( math::exception const & ) {
        // a body that doesn't lex is reported when it is parsed
    }
    return highest;
}
} // namespace

void jmath_func::load( JsonObject const &jo, std::string_view /*src*/ )
{
    optional( jo, was_loaded, "num_args", num_params );
    optional( jo, was_loaded, "return", _str );
    optional( jo, was_loaded, "optional_ids", optional_ids, false );
    // MATH-J11: an optional body that makes the function an assignment target; _val is the value
    if( jo.has_member( "assign" ) ) {
        _assign_str = jo.get_string( "assign" );
        has_assign = !_assign_str.empty();
    }

    // The parser looks for built-in functions and constants first, then for jmath functions,
    // then for dialogue functions, so a jmath function with any of their names is ignored or
    // hides the other
    std::string const idstr = id.str();
    auto const &diag_funcs = get_all_diag_funcs();
    bool shadows = diag_funcs.count( idstr ) > 0;
    for( math_func const &f : functions ) {
        shadows = shadows || f.symbol == idstr;
    }
    for( math_const const &c : constants ) {
        shadows = shadows || c.symbol == idstr;
    }
    // "u_skill" would call this function instead of the dialogue function skill()
    if( idstr.size() > 2 && idstr[1] == '_' &&
        diag_funcs.count( std::string_view( idstr ).substr( 2 ) ) > 0 ) {
        shadows = true;
    }
    if( shadows ) {
        jo.throw_error( string_format(
                            R"(jmath function "%s" shadows a built-in function with the same name.  You must rename it.)",
                            idstr ) );
    }

    // _N past the last parameter would read 0, or a caller's parameter when calls are nested
    const int highest = std::max( highest_param( _str ), highest_param( _assign_str ) );
    if( highest >= num_params ) {
        jo.throw_error( string_format(
                            R"(jmath function "%s" uses _%d but takes %d argument(s) ("num_args"))",
                            idstr, highest, num_params ) );
    }
}

void jmath_func::finalize_all()
{
    get_all_jmath_func().finalize();
    // u_foo( ... ) calls foo() for the u talker, unless a function is named u_foo
    for( jmath_func const &f : get_all() ) {
        std::string const &idstr = f.id.str();
        if( idstr.size() > 2 && idstr[1] == '_' && ( idstr[0] == 'u' || idstr[0] == 'n' ) &&
            jmath_func_id( idstr.substr( 2 ) ).is_valid() ) {
            debugmsg( R"(jmath function "%s" hides the scoped call of jmath function "%s".  Rename it.)",
                      idstr, idstr.substr( 2 ) );
        }
    }
}

void jmath_func::finalize()
{
    _exp.parse( _str );
    _str.clear();
    if( has_assign ) {
        _assign_exp.parse( _assign_str );
        if( _assign_exp.get_type() != math_type_t::assign ) {
            debugmsg( R"(The "assign" of jmath function "%s" must be an assignment, like "u_foo = _val")",
                      id.str() );
        }
        _assign_str.clear();
    }
    if( !optional_ids ) {
        // MATH-J04: checked with the EOCs, once martial arts have made their buff effects
        effect_on_conditions::add_load_check( [fid = id]() {
            if( !fid.is_valid() ) {
                return;
            }
            for( math_exp *exp : {
                     &fid->_exp, &fid->_assign_exp
                 } ) {
                for( const std::string &err : exp->check_literal_ids() ) {
                    debugmsg( R"(jmath function "%s": %s.  If the id is another mod's, add "optional_ids": true.)",
                              fid.str(), err );
                }
            }
        } );
    }
}

double jmath_func::eval( const_dialogue const &d ) const
{
    return _exp.eval( d );
}

double jmath_func::eval( const_dialogue const &d, std::vector<double> const &params ) const
{
    const_dialogue d_next( d );
    for( std::vector<double>::size_type i = 0; i < params.size(); i++ ) {
        d_next.set_value( std::to_string( i ), params[i] );
    }

    return eval( d_next );
}

namespace
{
void set_params( const_dialogue &d, std::vector<diag_value> const &params )
{
    for( std::vector<diag_value>::size_type i = 0; i < params.size(); i++ ) {
        d.set_value( std::to_string( i ), params[i] );
    }
}

void check_scope( const_dialogue const &d, char scope, std::string const &id )
{
    if( scope == 'n' && !d.has_beta ) {
        throw math::runtime_error( R"(n_%s(): there is no beta talker)", id );
    }
}
} // namespace

double jmath_func::eval( const_dialogue const &d, std::vector<diag_value> const &params,
                         char scope ) const
{
    check_scope( d, scope, id.str() );
    if( scope != 'n' ) {
        const_dialogue d_next( d );
        set_params( d_next, params );
        return eval( d_next );
    }
    // n_my_func(): the body sees the beta talker as u and the alpha talker as n
    const_dialogue d_next( d.const_actor( true )->const_clone(),
                           d.has_alpha ? d.const_actor( false )->const_clone() : nullptr,
                           d.get_conditionals(), d.get_context() );
    set_params( d_next, params );
    return eval( d_next );
}

void jmath_func::assign( dialogue &d, std::vector<diag_value> const &params, char scope,
                         double val ) const
{
    if( !has_assign ) {
        throw math::runtime_error( R"(jmath function "%s" can't be assigned to: it has no "assign")",
                                   id.str() );
    }
    check_scope( d, scope, id.str() );
    std::unique_ptr<dialogue> d_next;
    if( scope != 'n' ) {
        d_next = std::make_unique<dialogue>( d );
    } else {
        d_next = std::make_unique<dialogue>( d.actor( true )->clone(),
                                             d.has_alpha ? d.actor( false )->clone() : nullptr,
                                             d.get_conditionals(), d.get_context() );
    }
    set_params( *d_next, params );
    d_next->set_value( "val", val );
    _assign_exp.eval( *d_next );
}

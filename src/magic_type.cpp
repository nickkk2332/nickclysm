#include "magic_type.h"

#include "debug.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "json.h"
#include "math_parser_jmath.h"

// LOADING
// magic_type

namespace
{
generic_factory<magic_type> magic_type_factory( "magic_type" );
} // namespace

template<>
const magic_type &string_id<magic_type>::obj() const
{
    return magic_type_factory.obj( *this );
}

template<>
bool string_id<magic_type>::is_valid() const
{
    return magic_type_factory.is_valid( *this );
}

void magic_type::load_magic_type( const JsonObject &jo, const std::string &src )
{
    magic_type_factory.load( jo, src );
}

void magic_type::finalize_all()
{
    magic_type_factory.finalize();
}


void magic_type::load( const JsonObject &jo, std::string_view src )
{
    src_mod = mod_id( src );

    optional( jo, was_loaded, "get_level_formula_id", get_level_formula_id );
    optional( jo, was_loaded, "exp_for_level_formula_id", exp_for_level_formula_id );
    if( ( get_level_formula_id.has_value() && !exp_for_level_formula_id.has_value() ) ||
        ( !get_level_formula_id.has_value() && exp_for_level_formula_id.has_value() ) ) {
        debugmsg( "magic_type id:%s has a get_level_formula_id or exp_for_level_formula_id but not the other!  This breaks the calculations for xp/level!",
                  id.c_str() );
    }
    optional( jo, was_loaded, "casting_xp_formula_id", casting_xp_formula_id );
    optional( jo, was_loaded, "failure_chance_formula_id", failure_chance_formula_id );
    if( jo.has_string( "energy_source" ) ) {
        mandatory( jo, was_loaded, "energy_source", energy_source );
    } else if( jo.has_object( "energy_source" ) ) {
        const JsonObject jo_energy = jo.get_object( "energy_source" );
        mandatory( jo_energy, was_loaded, "type", energy_source );
        optional( jo_energy, was_loaded, "vitamin", vitamin_energy_source_ );
        optional( jo_energy, was_loaded, "color", energy_color_, nc_color_reader{}, c_cyan );
    }
    // MAGIC-B16: a string or an array; a copy-from child's list replaces its parent's, and
    // "extend" and "delete" change it
    optional( jo, was_loaded, "cannot_cast_flags", cannot_cast_flags, string_reader{} );
    optional( jo, was_loaded, "cannot_cast_message", cannot_cast_message );
    optional( jo, was_loaded, "max_book_level", max_book_level );
    optional( jo, was_loaded, "failure_cost_percent", failure_cost_percent, 0.0f );
    optional( jo, was_loaded, "failure_exp_percent", failure_exp_percent, 0.2f );

    // MAGIC-B16: a copy-from child used to keep its parent's failure_eocs whatever it gave
    if( jo.has_member( "failure_eocs" ) ) {
        failure_eocs.clear();
        for( JsonValue jv : jo.get_array( "failure_eocs" ) ) {
            failure_eocs.emplace_back( effect_on_conditions::load_inline_eoc( jv, src ) );
        }
    }

    if( jo.has_object( "encumbrance" ) ) {
        encumbrance.load( jo.get_object( "encumbrance" ) );
    }
}

void spell_encumbrance::load( const JsonObject &jo )
{
    // members it doesn't give keep their values: the defaults, or a copy-from parent's
    optional( jo, true, "hands_free", hands_free );
    optional( jo, true, "hands_cost", hands_cost );
    optional( jo, true, "legs_free", legs_free );
    optional( jo, true, "legs_time", legs_time );
    optional( jo, true, "arms_free", arms_free );
    optional( jo, true, "arms_time", arms_time );
    optional( jo, true, "arms_fail", arms_fail );
    optional( jo, true, "mouth_free", mouth_free );
    optional( jo, true, "mouth_fail", mouth_fail );
}

int spell_encumbrance::cost_per_hand_point( const magic_energy_type source ) const
{
    if( hands_cost.has_value() ) {
        return *hands_cost;
    }
    switch( source ) {
        case magic_energy_type::hp:
            return 1;
        case magic_energy_type::stamina:
            return 100;
        default:
            return 10;
    }
}

void magic_type::serialize( JsonOut &json ) const
{
    json.start_object();

    json.member( "type", "magic_type" );
    json.member( "id", id );
    json.member( "src_mod", src_mod );
    json.member( "get_level_formula_id", get_level_formula_id );
    json.member( "exp_for_level_formula_id", exp_for_level_formula_id );
    json.member( "casting_xp_formula_id", casting_xp_formula_id );
    json.member( "failure_chance_formula_id", failure_chance_formula_id );
    json.member( "energy_source", energy_source );
    json.member( "cannot_cast_flags", cannot_cast_flags, std::set<std::string> {} );
    json.member( "cannot_cast_message", cannot_cast_message );
    json.member( "max_book_level", max_book_level );
    json.member( "failure_cost_percent", static_cast<float>( failure_cost_percent.constant() ),
                 0.0f );
    json.member( "failure_exp_percent", static_cast<float>( failure_exp_percent.constant() ),
                 0.2f );
    json.member( "failure_eocs", failure_eocs, std::vector<effect_on_condition_id> {} );

    json.end_object();
}

// Checks that a formula exists and takes the number of arguments the engine passes it.
static void check_magic_type_formula( const magic_type &m_t, const char *member,
                                      const std::optional<jmath_func_id> &formula, int params )
{
    if( !formula.has_value() ) {
        return;
    }
    if( !formula->is_valid() ) {
        debugmsg( R"(magic_type %s has unknown jmath_function "%s" as %s)", m_t.id.c_str(),
                  formula->c_str(), member );
    } else if( formula.value()->num_params != params ) {
        debugmsg( "ERROR: %s %s has params that != %d!", m_t.id.c_str(), member, params );
    }
}

void magic_type::check_consistency()
{
    for( const magic_type &m_t : get_all() ) {
        check_magic_type_formula( m_t, "exp_for_level_formula_id",
                                  m_t.exp_for_level_formula_id, 1 );
        check_magic_type_formula( m_t, "get_level_formula_id", m_t.get_level_formula_id, 1 );
        check_magic_type_formula( m_t, "casting_xp_formula_id", m_t.casting_xp_formula_id, 0 );
        check_magic_type_formula( m_t, "failure_chance_formula_id",
                                  m_t.failure_chance_formula_id, 0 );
        if( m_t.vitamin_energy_source_.has_value() && !m_t.vitamin_energy_source_->is_valid() ) {
            debugmsg( R"(magic_type %s has unknown energy_source vitamin "%s")", m_t.id.c_str(),
                      m_t.vitamin_energy_source_->c_str() );
        }
        for( const effect_on_condition_id &eoc : m_t.failure_eocs ) {
            if( !eoc.is_valid() ) {
                debugmsg( R"(magic_type %s has unknown failure_eocs entry "%s")", m_t.id.c_str(),
                          eoc.c_str() );
            }
        }
    }
}

const std::vector<magic_type> &magic_type::get_all()
{
    return magic_type_factory.get_all();
}

void magic_type::reset_all()
{
    magic_type_factory.reset();
}

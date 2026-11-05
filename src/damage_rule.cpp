#include "damage_rule.h"

#include <algorithm>

#include "condition.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"

namespace
{
generic_factory<damage_rule> factory( "damage rule" );
std::vector<const damage_rule *> sorted_rules;

void read_eocs( const JsonObject &jo, const std::string &key, const std::string &source,
                std::vector<effect_on_condition_id> &out )
{
    if( !jo.has_member( key ) ) {
        return;
    }
    out.clear();
    for( JsonValue value : jo.get_array( key ) ) {
        if( out.size() == 16 ) {
            jo.throw_error_at( key, "at most 16 EOCs per damage hook" );
        }
        out.push_back( effect_on_conditions::load_inline_eoc( value, source ) );
    }
}

void check_kinds( const JsonObject &jo, const std::string &member,
                  const std::set<std::string> &kinds, bool source )
{
    for( const std::string &kind : kinds ) {
        if( kind != "avatar" && kind != "npc" && kind != "monster" &&
            !( source && ( kind == "environment" || kind == "untracked" ) ) ) {
            jo.throw_error_at( member, "unknown creature kind" );
        }
    }
}
} // namespace

template<>
const damage_rule &damage_rule_id::obj() const
{
    return factory.obj( *this );
}

template<>
bool damage_rule_id::is_valid() const
{
    return factory.is_valid( *this );
}

void damage_rule::load( const JsonObject &jo, std::string_view source )
{
    stage = jo.get_string( "stage", stage );
    if( stage != "before_armor" && stage != "after_armor" && stage != "committed" ) {
        jo.throw_error_at( "stage", "expected before_armor, after_armor or committed" );
    }
    priority = jo.get_int( "priority", priority );
    if( priority < -1000000 || priority > 1000000 ) {
        jo.throw_error_at( "priority", "priority must be between -1000000 and 1000000" );
    }
    jo.read( "required_effect", required_effect );
    jo.read( "defender_kinds", defender_kinds );
    jo.read( "source_kinds", source_kinds );
    jo.read( "damage_types", damage_types );
    jo.read( "bodyparts", bodyparts );
    jo.read( "spells", spells );
    check_kinds( jo, "defender_kinds", defender_kinds, false );
    check_kinds( jo, "source_kinds", source_kinds, true );
    if( jo.has_member( "condition" ) ) {
        has_condition = true;
        read_condition( jo, "condition", condition, false );
    }
    read_eocs( jo, "mutate", std::string( source ), mutate );
    read_eocs( jo, "on_resolved", std::string( source ), on_resolved );
    if( jo.has_member( "actions" ) ) {
        actions.clear();
        for( const JsonObject action : jo.get_array( "actions" ) ) {
            if( actions.size() == 16 ) {
                jo.throw_error_at( "actions", "at most 16 actions per damage rule" );
            }
            damage_rule_action out;
            out.operation = action.get_string( "operation" );
            if( out.operation == "scale" || out.operation == "cap" || out.operation == "reflect" ) {
                out.value = get_dbl_or_var( action, "value", true );
                if( out.operation == "reflect" ) {
                    out.resource = action.get_string( "resource", "none" );
                    out.cost_per_damage = action.get_int( "cost_per_damage", 1 );
                    if( ( out.resource != "mana" && out.resource != "stamina" && out.resource != "none" ) ||
                        out.cost_per_damage < 1 || out.cost_per_damage > 1000000 ) {
                        action.throw_error( "invalid reflection resource or cost" );
                    }
                }
            } else if( out.operation == "convert" ) {
                out.value = get_dbl_or_var( action, "fraction", false, 1.0 );
                out.from = damage_type_id( action.get_string( "from" ) );
                out.to = damage_type_id( action.get_string( "to" ) );
                if( stage != "before_armor" ) {
                    action.throw_error( "conversion is only supported before armor" );
                }
            } else if( out.operation == "absorb" ) {
                out.resource = action.get_string( "resource" );
                out.cost_per_damage = action.get_int( "cost_per_damage", 1 );
                if( out.resource != "mana" && out.resource != "stamina" && out.resource != "none" ) {
                    action.throw_error_at( "resource", "supported resources: mana, stamina, none" );
                }
                if( out.cost_per_damage < 1 || out.cost_per_damage > 1000000 ) {
                    action.throw_error_at( "cost_per_damage", "cost must be between 1 and 1000000" );
                }
                out.value = get_dbl_or_var( action, "maximum", out.resource == "none", 1000000.0 );
            } else if( out.operation == "cancel" ) {
                out.value = get_dbl_or_var( action, "value", false, 0.0 );
            } else {
                action.throw_error_at( "operation", "unknown damage operation" );
            }
            actions.push_back( std::move( out ) );
        }
    }
    if( stage == "committed" && ( !actions.empty() || !mutate.empty() ) ) {
        jo.throw_error( "committed hooks observe results; they cannot edit the committed packet" );
    }
    if( actions.empty() && mutate.empty() && on_resolved.empty() ) {
        jo.throw_error( "a damage rule needs an action or hook" );
    }
}

void damage_rule::check() const
{
    if( !required_effect.is_empty() && !required_effect.is_valid() ) {
        debugmsg( "Damage rule %s has invalid effect %s", id.str(), required_effect.str() );
    }
    for( const auto &type : damage_types ) {
        if( !type.is_valid() ) {
            debugmsg( "Damage rule %s has invalid damage type %s", id.str(), type.str() );
        }
    }
    for( const auto &bp : bodyparts ) {
        if( !bp.is_valid() ) {
            debugmsg( "Damage rule %s has invalid body part %s", id.str(), bp.str() );
        }
    }
    for( const auto &sp : spells ) {
        if( !sp.is_valid() ) {
            debugmsg( "Damage rule %s has invalid spell %s", id.str(), sp.str() );
        }
    }
    for( const auto &action : actions ) {
        if( action.operation == "convert" && ( !action.from.is_valid() || !action.to.is_valid() ) ) {
            debugmsg( "Damage rule %s has an invalid conversion", id.str() );
        }
    }
    for( const auto *list : {
             &mutate, &on_resolved
         } ) {
        for( const auto &eoc : *list ) {
            if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                debugmsg( "Damage rule %s needs ACTIVATION EOC %s", id.str(), eoc.str() );
            }
        }
    }
}

void damage_rules::load( const JsonObject &jo, const std::string &source )
{
    factory.load( jo, source );
}

void damage_rules::reset()
{
    sorted_rules.clear();
    factory.reset();
}

void damage_rules::finalize()
{
    factory.finalize();
    sorted_rules.clear();
    for( const damage_rule &rule : factory.get_all() ) {
        sorted_rules.push_back( &rule );
    }
    std::sort( sorted_rules.begin(), sorted_rules.end(), []( const auto * lhs, const auto * rhs ) {
        return lhs->priority != rhs->priority ? lhs->priority > rhs->priority : lhs->id.str() <
               rhs->id.str();
    } );
}

void damage_rules::check()
{
    factory.check();
}

bool damage_rules::may_intercept( const Creature &defender )
{
    for( const damage_rule *rule : sorted_rules ) {
        if( rule->required_effect.is_empty() || defender.has_effect( rule->required_effect ) ) {
            return true;
        }
    }
    return false;
}

const std::vector<const damage_rule *> &damage_rules::ordered()
{
    return sorted_rules;
}

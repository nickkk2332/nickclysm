#include "execution.h"

#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

#include "condition.h"
#include "debug.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"

namespace
{
generic_factory<execution_definition> factory( "execution" );

execution_range read_range( const JsonObject &jo, const std::string &key, int fallback,
                            int minimum, int maximum )
{
    execution_range result{ fallback, fallback };
    if( jo.has_array( key ) ) {
        JsonArray values = jo.get_array( key );
        if( values.size() != 2 ) {
            jo.throw_error_at( key, "expected an integer or a two-integer inclusive range" );
        }
        result = { values.get_int( 0 ), values.get_int( 1 ) };
    } else if( jo.has_member( key ) ) {
        result.min = result.max = jo.get_int( key );
    }
    if( result.min < minimum || result.max < result.min || result.max > maximum ) {
        jo.throw_error_at( key, "range is reversed or outside the supported bounds" );
    }
    return result;
}

int read_integer( const JsonObject &jo, const std::string &key, int fallback, int low, int high )
{
    const int value = jo.get_int( key, fallback );
    if( value < low || value > high ) {
        jo.throw_error_at( key, "integer outside supported bounds" );
    }
    return value;
}

void load_eocs( const JsonObject &jo, const std::string &key, const std::string &source,
                std::vector<effect_on_condition_id> &out )
{
    if( jo.has_member( key ) ) {
        out.clear();
        for( JsonValue value : jo.get_array( key ) ) {
            out.push_back( effect_on_conditions::load_inline_eoc( value, source ) );
        }
    }
}

std::shared_ptr<execution_transition> load_transition( const JsonValue &value, int depth = 0 )
{
    if( depth > 8 ) {
        value.throw_error( "execution transitions may nest at most eight levels" );
    }
    auto out = std::make_shared<execution_transition>();
    if( value.test_string() ) {
        out->step = value.get_string();
        return out;
    }
    JsonObject jo = value.get_object();
    const int forms = jo.has_member( "step" ) + jo.has_member( "if" ) + jo.has_member( "chance" ) +
                      jo.has_member( "weighted" ) + jo.has_member( "result" );
    if( forms != 1 ) {
        jo.throw_error( "expected exactly one execution transition form" );
    }
    if( jo.has_member( "step" ) ) {
        out->step = jo.get_string( "step" );
        out->delay = read_range( jo, "after_ticks", 0, 0, 600 );
    } else if( jo.has_member( "if" ) || jo.has_member( "chance" ) ) {
        if( jo.has_member( "if" ) ) {
            out->type = execution_transition::kind::condition;
            read_condition( jo, "if", out->condition, false );
        } else {
            out->type = execution_transition::kind::chance;
            out->chance = jo.get_float( "chance" );
            if( !std::isfinite( out->chance ) || out->chance < 0 || out->chance > 1 ) {
                jo.throw_error_at( "chance", "probability must be finite and between zero and one" );
            }
        }
        out->yes = load_transition( jo.get_member( "then" ), depth + 1 );
        out->no = load_transition( jo.get_member( "else" ), depth + 1 );
    } else if( jo.has_member( "weighted" ) ) {
        out->type = execution_transition::kind::weighted;
        int total = 0;
        for( JsonObject entry : jo.get_array( "weighted" ) ) {
            const int weight = entry.get_int( "weight" );
            if( weight < 0 || weight > 1000000 || total > 1000000 - weight ) {
                entry.throw_error_at( "weight", "invalid weight or total exceeds 1000000" );
            }
            total += weight;
            out->weighted.emplace_back( weight, load_transition( entry.get_member( "to" ), depth + 1 ) );
        }
        if( total == 0 || out->weighted.size() > 64 ) {
            jo.throw_error_at( "weighted", "expected 1–64 entries with positive total weight" );
        }
    } else {
        out->type = execution_transition::kind::result;
        out->result = jo.get_string( "result" );
        for( const JsonMember entry : jo.get_object( "cases" ) ) {
            out->cases.emplace( entry.name(), load_transition( entry, depth + 1 ) );
        }
        out->no = load_transition( jo.get_member( "otherwise" ), depth + 1 );
    }
    return out;
}

execution_narration load_narration( const JsonObject &jo )
{
    execution_narration out;
    const std::string audience = jo.get_string( "audience", "visible" );
    if( audience == "audible" ) {
        out.perception = execution_narration::audience::audible;
    } else if( audience == "visible_or_audible" ) {
        out.perception = execution_narration::audience::visible_or_audible;
        if( !jo.read( "audible_text", out.audible_text ) || out.audible_text.empty() ) {
            jo.throw_error_at( "audible_text", "visible_or_audible requires a nonempty hearing fallback" );
        }
    } else if( audience != "visible" && audience != "visible_action" ) {
        jo.throw_error_at( "audience", "expected visible, visible_action, audible or visible_or_audible" );
    }
    if( out.perception != execution_narration::audience::visible ) {
        out.volume = read_integer( jo, "volume", 0, 1, 200 );
    } else if( jo.has_member( "volume" ) ) {
        jo.throw_error_at( "volume", "volume requires an audible audience" );
    }
    if( jo.has_member( "audible_text" ) &&
        out.perception != execution_narration::audience::visible_or_audible ) {
        jo.throw_error_at( "audible_text", "audible_text requires visible_or_audible" );
    }
    out.entry = jo.get_string( "entry", "" );
    out.append_to = jo.get_string( "append_to", "" );
    if( ( jo.has_member( "entry" ) && out.entry.empty() ) || out.entry.size() > 64 ) {
        jo.throw_error_at( "entry", "expected a nonempty entry name of at most 64 bytes" );
    }
    if( ( jo.has_member( "append_to" ) && out.append_to.empty() ) || out.append_to.size() > 64 ) {
        jo.throw_error_at( "append_to", "expected a nonempty entry name of at most 64 bytes" );
    }
    out.separator = jo.get_string( "separator", " " );
    if( out.separator != "" && out.separator != " " && out.separator != "\n" ) {
        jo.throw_error_at( "separator", "expected an empty string, a space or a newline" );
    }
    return out;
}

void load_messages( const JsonObject &jo, execution_action &out )
{
    if( jo.has_member( "text" ) == jo.has_member( "pool" ) ) {
        jo.throw_error( "a message requires exactly one of text or pool" );
    }
    if( jo.has_member( "text" ) ) {
        translation text;
        jo.read( "text", text );
        out.messages.emplace_back( 1, std::move( text ) );
    } else {
        const JsonObject pool = jo.get_object( "pool" );
        out.avoid_repeat = pool.get_bool( "avoid_immediate_repeat", false );
        int total = 0;
        for( JsonObject entry : pool.get_array( "entries" ) ) {
            const int weight = entry.get_int( "weight", 1 );
            translation text;
            entry.read( "text", text, true );
            if( weight < 1 || weight > 1000000 || total > 1000000 - weight ) {
                entry.throw_error_at( "weight", "message weights must be positive and total at most 1000000" );
            }
            total += weight;
            out.messages.emplace_back( weight, std::move( text ) );
        }
        if( out.messages.empty() || out.messages.size() > 64 ) {
            pool.throw_error_at( "entries", "expected 1–64 messages" );
        }
    }
    out.narration = load_narration( jo );
}

execution_action load_action( const JsonObject &jo, const std::string &source )
{
    execution_action out;
    const std::string op = jo.get_string( "op" );
    if( op == "message" ) {
        out.type = execution_action::kind::message;
        load_messages( jo, out );
    } else if( op == "move" ) {
        out.type = execution_action::kind::move;
        out.direction = jo.get_string( "direction", "backward" );
        if( out.direction != "backward" && out.direction != "forward" ) {
            jo.throw_error_at( "direction", "expected backward or forward" );
        }
        out.tiles = read_range( jo, "tiles", 1, 1, 16 );
        out.ticks_per_tile = read_integer( jo, "ticks_per_tile", 1, 1, 600 );
        out.first_after = read_integer( jo, "first_step_after_ticks", 1, 0, 600 );
        out.stop_before_unsupported = jo.get_bool( "stop_before_unsupported", true );
        if( jo.get_string( "collision", "stop" ) != "stop" ) {
            jo.throw_error_at( "collision", "only stop-on-collision movement is supported" );
        }
        if( jo.has_member( "narration" ) ) {
            const JsonObject message = jo.get_object( "narration" ).get_object( "on_moved" );
            message.read( "text", out.on_moved, true );
            out.on_moved_narration = load_narration( message );
        }
    } else if( op == "eoc" ) {
        out.type = execution_action::kind::eoc;
        load_eocs( jo, "eocs", source, out.eocs );
        if( out.eocs.empty() ) {
            jo.throw_error_at( "eocs", "an eoc action requires at least one activation EOC" );
        }
    } else if( op == "effect" ) {
        out.type = execution_action::kind::effect;
        jo.read( "effect", out.effect, true );
        out.duration = read_integer( jo, "duration_ticks", 1, 1, 600 );
        out.intensity = read_integer( jo, "intensity", 1, 1, 100 );
    } else if( op == "damage" ) {
        out.type = execution_action::kind::damage;
        jo.read( "damage_type", out.damage_type, true );
        out.amount = read_integer( jo, "amount", 0, 0, 100000 );
        out.bodypart = bodypart_str_id( jo.get_string( "bodypart", "torso" ) );
    } else {
        jo.throw_error_at( "op", "unsupported execution action" );
    }
    out.role = jo.get_string( "role", "target" );
    if( out.role != "actor" && out.role != "target" ) {
        jo.throw_error_at( "role", "expected actor or target" );
    }
    out.store_result = jo.get_string( "store_result", "" );
    return out;
}

execution_control read_control( const JsonObject &jo, const std::string &key,
                                execution_control fallback )
{
    if( !jo.has_member( key ) ) {
        return fallback;
    }
    const std::string value = jo.get_string( key );
    if( value == "free" ) {
        return execution_control::free;
    }
    if( value == "held" ) {
        return execution_control::held;
    }
    if( value != "scripted" ) {
        jo.throw_error_at( key, "expected free, held or scripted" );
    }
    return execution_control::scripted;
}

void check_transition( const execution_transition &t, const execution_definition &def )
{
    if( t.type == execution_transition::kind::step && def.steps.count( t.step ) == 0 ) {
        throw std::runtime_error( "unknown step '" + t.step + "'" );
    }
    if( t.yes ) {
        check_transition( *t.yes, def );
    }
    if( t.no ) {
        check_transition( *t.no, def );
    }
    for( const auto &entry : t.weighted ) {
        check_transition( *entry.second, def );
    }
    for( const auto &entry : t.cases ) {
        check_transition( *entry.second, def );
    }
}
} // namespace

template<>
const execution_definition &execution_id::obj() const
{
    return factory.obj( *this );
}

template<>
bool execution_id::is_valid() const
{
    return factory.is_valid( *this );
}

void execution_definition::load( const JsonObject &jo, std::string_view source )
{
    mandatory( jo, was_loaded, "name", name );
    revision = read_integer( jo, "revision", revision, 1, 1000000 );
    max_ticks = read_integer( jo, "max_ticks", max_ticks, 1, 600 );
    max_step_entries = read_integer( jo, "max_step_entries", max_step_entries, 1, 256 );
    max_distance = read_integer( jo, "max_distance", max_distance, 1, 60 );
    require_weapon = jo.get_bool( "require_weapon", require_weapon );
    if( jo.has_member( "required_roles" ) ) {
        const std::set<std::string> roles = jo.get_tags<std::string>( "required_roles" );
        if( roles != std::set<std::string> { "actor", "target" } ) {
            jo.throw_error_at( "required_roles", "this milestone requires actor and target" );
        }
    }
    if( jo.has_object( "control" ) ) {
        const JsonObject control = jo.get_object( "control" );
        control_actor = read_control( control, "actor", control_actor );
        control_target = read_control( control, "target", control_target );
    }
    if( jo.has_object( "interruptions" ) ) {
        const JsonObject policy = jo.get_object( "interruptions" );
        const auto read_policy = [&]( const std::string & key, bool previous ) {
            const std::string value = policy.get_string( key, previous ? "interrupt" : "continue" );
            if( value != "interrupt" && value != "continue" ) {
                policy.throw_error_at( key, "expected interrupt or continue" );
            }
            return value == "interrupt";
        };
        interrupt_damage = read_policy( "external_damage", interrupt_damage );
        interrupt_displacement = read_policy( "displacement", interrupt_displacement );
    }
    if( jo.has_object( "locals" ) ) {
        locals.clear();
        for( const JsonMember member : jo.get_object( "locals" ) ) {
            if( member.name().find( "execution_" ) == 0 ) {
                member.throw_error( "execution_ is reserved for runtime context" );
            }
            if( member.test_string() ) {
                locals.emplace( member.name(), member.get_string() );
            } else {
                locals.emplace( member.name(), member.get_float() );
            }
        }
    }
    load_eocs( jo, "on_start", std::string( source ), on_start );
    load_eocs( jo, "on_tick", std::string( source ), on_tick );
    load_eocs( jo, "on_complete", std::string( source ), on_complete );
    load_eocs( jo, "on_interrupt", std::string( source ), on_interrupt );
    load_eocs( jo, "on_end", std::string( source ), on_end );
    if( locals.size() > 64 ) {
        jo.throw_error_at( "locals", "at most 64 execution locals are supported" );
    }
    mandatory( jo, was_loaded, "start", start );
    if( jo.has_member( "steps" ) ) {
        steps.clear();
        for( const JsonMember member : jo.get_object( "steps" ) ) {
            const JsonObject step = member.get_object();
            execution_step out;
            if( step.has_member( "action" ) ) {
                out.action = load_action( step.get_object( "action" ), std::string( source ) );
            }
            if( step.has_member( "repeat" ) ) {
                if( out.action.type == execution_action::kind::move ) {
                    step.throw_error_at( "repeat", "movement already has a saved per-tile cursor" );
                }
                out.repeats = true;
                const JsonObject repeat = step.get_object( "repeat" );
                out.repeat_count = read_integer( repeat, "count", 1, 1, 32 );
                out.interval = read_integer( repeat, "interval_ticks", 1, 1, 600 );
                out.first_after = read_integer( repeat, "first_after_ticks", 1, 0, 600 );
            }
            if( step.has_member( "next" ) == step.has_member( "end" ) ) {
                step.throw_error( "an execution step requires exactly one of next or end" );
            }
            if( step.has_member( "next" ) ) {
                out.next = load_transition( step.get_member( "next" ) );
            } else {
                out.end = step.get_string( "end" );
                if( out.end.empty() ) {
                    step.throw_error_at( "end", "the outcome label cannot be empty" );
                }
            }
            steps.emplace( member.name(), std::move( out ) );
        }
    }
    try {
        std::set<std::string> entries;
        std::set<std::string> appends;
        if( steps.empty() || steps.size() > 128 || steps.count( start ) == 0 ) {
            throw std::runtime_error( "expected 1–128 steps and a valid start step" );
        }
        for( const auto &entry : steps ) {
            if( entry.second.next ) {
                check_transition( *entry.second.next, *this );
            }
            const execution_action &a = entry.second.action;
            for( const execution_narration *n : {
                     &a.narration, &a.on_moved_narration
                 } ) {
                if( !n->entry.empty() ) {
                    entries.insert( n->entry );
                }
                if( !n->append_to.empty() ) {
                    appends.insert( n->append_to );
                }
            }
            if( a.type == execution_action::kind::move &&
                ( a.role == "actor" ? control_actor : control_target ) != execution_control::scripted ) {
                throw std::runtime_error( "move actions require scripted control of their role" );
            }
        }
        if( entries.size() > 64 ) {
            throw std::runtime_error( "at most 64 named narration entries are supported" );
        }
        for( const std::string &anchor : appends ) {
            if( entries.count( anchor ) == 0 ) {
                throw std::runtime_error( "unknown narration append target '" + anchor + "'" );
            }
        }
    } catch( const std::runtime_error &error ) {
        jo.throw_error( error.what() );
    }
}

void execution_definition::check() const
{
    const auto check_eocs = [&]( const std::vector<effect_on_condition_id> &list ) {
        for( const effect_on_condition_id &eoc : list ) {
            if( !eoc.is_valid() || eoc->type != eoc_type::ACTIVATION ) {
                debugmsg( "Execution %s requires an activation EOC: %s", id.str(), eoc.str() );
            }
        }
    };
    for( const auto *list : {
             &on_start, &on_tick, &on_complete, &on_interrupt, &on_end
         } ) {
        check_eocs( *list );
    }
    for( const auto &entry : steps ) {
        const execution_action &a = entry.second.action;
        check_eocs( a.eocs );
        if( a.type == execution_action::kind::effect && !a.effect.is_valid() ) {
            debugmsg( "Execution %s has invalid effect %s", id.str(), a.effect.str() );
        }
        if( a.type == execution_action::kind::damage &&
            ( !a.damage_type.is_valid() || !a.bodypart.is_valid() ) ) {
            debugmsg( "Execution %s has an invalid damage type or body part", id.str() );
        }
    }
}

void executions::load( const JsonObject &jo, const std::string &source )
{
    factory.load( jo, source );
}

void executions::reset()
{
    factory.reset();
}

void executions::check()
{
    factory.check();
}

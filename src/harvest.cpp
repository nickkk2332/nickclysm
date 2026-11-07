#include "harvest.h"

#include <stddef.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <string>

#include "butchery.h"
#include "creature.h"
#include "debug.h"
#include "enum_conversions.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "item.h"
#include "item_group.h"
#include "output.h"
#include "rng.h"
#include "string_formatter.h"
#include "text_snippets.h"

static const butchery_requirements_id butchery_requirements_default( "default" );

static const itype_id itype_ruined_chunks( "ruined_chunks" );

static const skill_id skill_survival( "survival" );

namespace
{
generic_factory<harvest_drop_type> harvest_drop_type_factory( "harvest_drop_type" );
generic_factory<harvest_list> harvest_list_factory( "harvest_list" );
} //namespace

/** @relates string_id */
template<>
const harvest_drop_type &string_id<harvest_drop_type>::obj() const
{
    return harvest_drop_type_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<harvest_drop_type>::is_valid() const
{
    return harvest_drop_type_factory.is_valid( *this );
}

translation harvest_drop_type::field_dress_msg( bool succeeded ) const
{
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    return SNIPPET.random_from_category(
               succeeded ? msg_fielddress_success : msg_fielddress_fail ).value_or( translation() );
}

translation harvest_drop_type::butcher_msg( bool succeeded ) const
{
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    return SNIPPET.random_from_category(
               succeeded ? msg_butcher_success : msg_butcher_fail ).value_or( translation() );
}

translation harvest_drop_type::dissect_msg( bool succeeded ) const
{
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    return SNIPPET.random_from_category(
               succeeded ? msg_dissect_success : msg_dissect_fail ).value_or( translation() );
}

/** @relates string_id */
template<>
const harvest_list &string_id<harvest_list>::obj() const
{
    return harvest_list_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<harvest_list>::is_valid() const
{
    return harvest_list_factory.is_valid( *this );
}

harvest_list::harvest_list() : id( harvest_id::NULL_ID() ) {}

std::string harvest_list::message() const
{
    return SNIPPET.expand( message_.translated() );
}

bool harvest_list::is_null() const
{
    return id == harvest_id::NULL_ID();
}

namespace
{
butcher_type butcher_type_from_string( const std::string &name, const JsonValue &jv )
{
    for( int i = 0; i < static_cast<int>( butcher_type::NUM_TYPES ); ++i ) {
        const butcher_type action = static_cast<butcher_type>( i );
        if( io::enum_to_string( action ) == name ) {
            return action;
        }
    }
    jv.throw_error( string_format( "Unknown butchery action \"%s\"", name ) );
}

// Rounds down, but treats a product a hair below a whole number (0.3 * 10) as that number.
int round_down_yield( double amount )
{
    return static_cast<int>( amount + ( amount >= 0 ? 1e-6 : -1e-6 ) );
}
} // namespace

int harvest_yield::apply( int amount ) const
{
    int ret = round_down_yield( amount * mult );
    if( random ) {
        ret += rng( round_down_yield( amount * random_min ), round_down_yield( amount * random_max ) );
    }
    return ret;
}

void harvest_yield::deserialize( const JsonValue &jv )
{
    if( jv.test_float() ) {
        mult = jv.get_float();
        return;
    }
    JsonObject jo = jv.get_object();
    optional( jo, false, "mult", mult, 1.0 );
    if( jo.has_array( "random" ) ) {
        JsonArray ja = jo.get_array( "random" );
        if( ja.size() != 2 ) {
            ja.throw_error( "\"random\" needs two numbers, the lowest and the highest share" );
        }
        random = true;
        random_min = ja.get_float( 0 );
        random_max = ja.get_float( 1 );
        if( random_min > random_max ) {
            ja.throw_error( "The lowest share of \"random\" is above the highest" );
        }
    }
    if( jo.has_string( "min_size" ) ) {
        const std::string size = jo.get_string( "min_size" );
        const auto found = Creature::size_map.find( size );
        if( found == Creature::size_map.end() ) {
            jo.throw_error_at( "min_size", string_format( "Unknown creature size \"%s\"", size ) );
        }
        min_size = found->second;
    }
}

bool harvest_corpse_rule::applies( butcher_type action, const item &corpse ) const
{
    if( !actions.empty() && std::find( actions.begin(), actions.end(), action ) == actions.end() ) {
        return false;
    }
    return flags.empty() || std::any_of( flags.begin(), flags.end(), [&corpse]( const flag_id & f ) {
        return corpse.has_flag( f );
    } );
}

void harvest_corpse_rule::deserialize( const JsonObject &jo )
{
    optional( jo, false, "flags", flags );
    actions.clear();
    if( jo.has_member( "actions" ) ) {
        for( const std::string &name : jo.get_as_string_array( "actions" ) ) {
            actions.push_back( butcher_type_from_string( name, jo.get_member( "actions" ) ) );
        }
    }
    optional( jo, false, "skip", skip, false );
    if( jo.has_member( "yield" ) ) {
        if( skip ) {
            jo.throw_error_at( "yield", "A corpse rule with \"skip\" has no yield" );
        }
        yield.deserialize( jo.get_member( "yield" ) );
    } else if( !skip ) {
        jo.throw_error( "A corpse rule needs a \"yield\" or \"skip\": true" );
    }
}

void harvest_proficiency_penalty::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "proficiency", proficiency );
    mandatory( jo, false, "penalty", penalty );
}

void harvest_proficiency_training::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "proficiencies", proficiencies );
    if( proficiencies.empty() ) {
        jo.throw_error_at( "proficiencies", "Needs at least one proficiency" );
    }
    time_fraction.clear();
    for( const JsonMember member : jo.get_object( "time_fraction" ) ) {
        time_fraction[butcher_type_from_string( member.name(), member )] = member.get_float();
    }
}

const harvest_yield *harvest_drop_type::action_yield( butcher_type action ) const
{
    if( !has_action_yield_ ) {
        // The rules of a drop type without its own, as they were before drop types had rules.
        static const harvest_yield full;
        static const harvest_yield none = [] {
            harvest_yield ret;
            ret.mult = 0.0;
            return ret;
        }();
        switch( action ) {
            case butcher_type::FULL:
            case butcher_type::FIELD_DRESS:
            case butcher_type::DISSECT:
                return &full;
            case butcher_type::DISMEMBER:
                return &none;
            default:
                return nullptr;
        }
    }
    const auto found = action_yield_.find( action );
    return found == action_yield_.end() ? nullptr : &found->second;
}

int harvest_drop_type::corpse_damage_yield( int weight, int damage_level ) const
{
    if( corpse_damage_yield_.empty() || damage_level < 0 ) {
        return weight;
    }
    const size_t level = std::min( static_cast<size_t>( damage_level ),
                                   corpse_damage_yield_.size() - 1 );
    const float mult = corpse_damage_yield_[level];
    if( mult == 1.0f ) {
        // Untouched, even past the precision of a float.
        return weight;
    }
    return std::round( weight * mult );
}

bool harvest_drop_type::enables_action( butcher_type action ) const
{
    return enables_actions_.count( action ) > 0;
}

void harvest_drop_type::check_consistency()
{
    for( const harvest_drop_type &type : get_all() ) {
        for( const harvest_corpse_rule &rule : type.corpse_flag_yield_ ) {
            for( const flag_id &flag : rule.flags ) {
                if( !flag.is_valid() ) {
                    debugmsg( "harvest_drop_type %s has a corpse rule with unknown flag %s",
                              type.id.str(), flag.str() );
                }
            }
        }
        for( const harvest_proficiency_penalty &penalty : type.proficiency_penalties_ ) {
            if( !penalty.proficiency.is_valid() ) {
                debugmsg( "harvest_drop_type %s has a penalty for unknown proficiency %s",
                          type.id.str(), penalty.proficiency.str() );
            }
        }
        for( const harvest_proficiency_training &training : type.trains_proficiencies_ ) {
            for( const proficiency_id &prof : training.proficiencies ) {
                if( !prof.is_valid() ) {
                    debugmsg( "harvest_drop_type %s trains unknown proficiency %s", type.id.str(),
                              prof.str() );
                }
            }
        }
    }
}

const harvest_drop_type &harvest_entry::drop_type() const
{
    static const harvest_drop_type default_rules;
    return type.is_valid() ? type.obj() : default_rules;
}

bool harvest_list::has_entry_type( const harvest_drop_type_id &type ) const
{
    for( const harvest_entry &entry : entries() ) {
        if( entry.type == type ) {
            return true;
        }
    }
    return false;
}

void harvest_drop_type::load_harvest_drop_types( const JsonObject &jo, const std::string &src )
{
    harvest_drop_type_factory.load( jo, src );
}

void harvest_drop_type::finalize_all()
{
    harvest_drop_type_factory.finalize();
}

void harvest_drop_type::reset()
{
    harvest_drop_type_factory.reset();
}

void harvest_drop_type::load( const JsonObject &jo, std::string_view )
{
    harvest_skills.clear();
    optional( jo, was_loaded, "group", is_group_, false );
    optional( jo, was_loaded, "dissect_only", dissect_only_, false );
    optional( jo, was_loaded, "msg_fielddress_fail", msg_fielddress_fail, "" );
    optional( jo, was_loaded, "msg_fielddress_success", msg_fielddress_success, "" );
    optional( jo, was_loaded, "msg_butcher_fail", msg_butcher_fail, "" );
    optional( jo, was_loaded, "msg_butcher_success", msg_butcher_success, "" );
    optional( jo, was_loaded, "msg_dissect_fail", msg_dissect_fail, "" );
    optional( jo, was_loaded, "msg_dissect_success", msg_dissect_success, "" );
    if( jo.has_string( "harvest_skills" ) ) {
        skill_id sk;
        mandatory( jo, was_loaded, "harvest_skills", sk );
        harvest_skills.emplace_back( sk );
    } else if( jo.has_member( "harvest_skills" ) ) {
        optional( jo, was_loaded, "harvest_skills", harvest_skills );
    } else {
        harvest_skills.emplace_back( skill_survival );
    }

    if( jo.has_object( "action_yield" ) ) {
        has_action_yield_ = true;
        action_yield_.clear();
        for( const JsonMember member : jo.get_object( "action_yield" ) ) {
            harvest_yield yield;
            yield.deserialize( member );
            action_yield_[butcher_type_from_string( member.name(), member )] = yield;
        }
    } else if( jo.has_member( "action_yield" ) ) {
        jo.throw_error_at( "action_yield", "Expected an object of butchery actions" );
    }
    optional( jo, was_loaded, "corpse_damage_yield", corpse_damage_yield_ );
    optional( jo, was_loaded, "gib_yield", gib_yield_, 1.0 );
    optional( jo, was_loaded, "survives_splatter", survives_splatter_, false );
    if( jo.has_member( "enables_action" ) ) {
        enables_actions_.clear();
        for( const std::string &name : jo.get_as_string_array( "enables_action" ) ) {
            const butcher_type action = butcher_type_from_string( name, jo.get_member( "enables_action" ) );
            if( action != butcher_type::FIELD_DRESS && action != butcher_type::SKIN &&
                action != butcher_type::BLEED ) {
                jo.throw_error_at( "enables_action",
                                   "Only FIELD_DRESS, SKIN and BLEED need a drop type to be available" );
            }
            enables_actions_.insert( action );
        }
    }
    optional( jo, was_loaded, "corpse_flag_yield", corpse_flag_yield_ );
    optional( jo, was_loaded, "proficiency_penalties", proficiency_penalties_ );
    optional( jo, was_loaded, "trains_proficiencies", trains_proficiencies_ );
}

const std::vector<harvest_drop_type> &harvest_drop_type::get_all()
{
    return harvest_drop_type_factory.get_all();
}

void harvest_entry::load( const JsonObject &jo )
{
    mandatory( jo, was_loaded, "drop", drop );

    optional( jo, was_loaded, "type", type, harvest_drop_type_id::NULL_ID() );
    optional( jo, was_loaded, "base_num", base_num, { 1.0f, 1.0f } );
    optional( jo, was_loaded, "scale_num", scale_num, { 0.0f, 0.0f } );
    optional( jo, was_loaded, "max", max, 1000 );
    optional( jo, was_loaded, "mass_ratio", mass_ratio, 0.00f );
    optional( jo, was_loaded, "flags", flags );
    optional( jo, was_loaded, "faults", faults );
}

void harvest_entry::deserialize( const JsonObject &jo )
{
    load( jo );
}

bool harvest_entry::operator==( const harvest_entry &rhs ) const
{
    return drop == rhs.drop;
}

namespace
{
class harvest_entry_reader : public generic_typed_reader<harvest_entry_reader>
{
    public:
        harvest_entry get_next( const JsonValue &jv ) const {
            JsonObject jo = jv.get_object();
            harvest_entry ret;
            mandatory( jo, false, "drop", ret.drop );

            optional( jo, false, "type", ret.type, harvest_drop_type_id::NULL_ID() );
            optional( jo, false, "base_num", ret.base_num, { 1.0f, 1.0f } );
            optional( jo, false, "scale_num", ret.scale_num, { 0.0f, 0.0f } );
            optional( jo, false, "max", ret.max, 1000 );
            optional( jo, false, "mass_ratio", ret.mass_ratio, 0.00f );
            optional( jo, false, "flags", ret.flags );
            optional( jo, false, "faults", ret.faults );
            return ret;
        }
};
} // namespace

void harvest_list::finalize()
{
    std::transform( entries_.begin(), entries_.end(), std::inserter( names_, names_.begin() ),
    []( const harvest_entry & entry ) {
        return item::type_is_defined( itype_id( entry.drop ) ) ?
               item::nname( itype_id( entry.drop ) ) : "";
    } );
}

void harvest_list::finalize_all()
{
    harvest_list_factory.finalize();
}

void harvest_list::load( const JsonObject &obj, std::string_view )
{
    mandatory( obj, was_loaded, "id", id );
    mandatory( obj, was_loaded, "entries", entries_, harvest_entry_reader{} );

    optional( obj, was_loaded, "butchery_requirements", butchery_requirements_,
              butchery_requirements_default );
    optional( obj, was_loaded, "message", message_ );
    optional( obj, was_loaded, "leftovers", leftovers, itype_ruined_chunks );
}

void harvest_list::load_harvest_list( const JsonObject &jo, const std::string &src )
{
    harvest_list_factory.load( jo, src );
}

const std::vector<harvest_list> &harvest_list::get_all()
{
    return harvest_list_factory.get_all();
}

void harvest_list::check_consistency()
{
    for( const harvest_list &hl : get_all() ) {
        const std::string hl_id = hl.id.c_str();
        auto error_func = [&]( const harvest_entry & entry ) {
            std::string errorlist;
            if( entry.type.is_null() ) {
                // Type id is null
                if( item::type_is_defined( itype_id( entry.drop ) ) ) {
                    return errorlist;
                }
                errorlist += "null type";
            } else if( !entry.type.is_valid() ) {
                // Type id is invalid
                errorlist += "invalid type \"" + entry.type.str() + "\"";
            } else if( ( entry.type->is_item_group() &&
                         !item_group::group_is_defined( item_group_id( entry.drop ) ) ) ||
                       ( !entry.type->is_item_group() && !item::type_is_defined( itype_id( entry.drop ) ) ) ) {
                // Specified as item_group but no such group exists, or
                // specified as single itype but no such itype exists
                errorlist += entry.drop;
            }
            return errorlist;
        };
        const std::string errors = enumerate_as_string( hl.entries_.begin(), hl.entries_.end(),
                                   error_func );
        if( !errors.empty() ) {
            debugmsg( "Harvest list %s has invalid entry: %s", hl_id, errors );
        }
        if( !hl.leftovers.is_valid() ) {
            debugmsg( "Harvest id %s has invalid leftovers: %s", hl.id.c_str(), hl.leftovers.c_str() );
        }
        if( !hl.butchery_requirements_.is_valid() ) {
            debugmsg( "Harvest id %s has unknown butchery_requirements: %s", hl.id.c_str(),
                      hl.butchery_requirements_.c_str() );
        }
    }
}

void harvest_list::reset()
{
    harvest_list_factory.reset();
}

std::vector<harvest_entry>::const_iterator harvest_list::begin() const
{
    return entries().begin();
}

std::vector<harvest_entry>::const_iterator harvest_list::end() const
{
    return entries().end();
}

std::vector<harvest_entry>::const_reverse_iterator harvest_list::rbegin() const
{
    return entries().rbegin();
}

std::vector<harvest_entry>::const_reverse_iterator harvest_list::rend() const
{
    return entries().rend();
}

std::string harvest_list::describe( int at_skill ) const
{
    if( empty() ) {
        return "";
    }

    return enumerate_as_string( entries().begin(), entries().end(),
    [at_skill]( const harvest_entry & en ) {
        float min_f = en.base_num.first;
        float max_f = en.base_num.second;
        if( at_skill >= 0 ) {
            min_f += en.scale_num.first * at_skill;
            max_f += en.scale_num.second * at_skill;
        } else {
            max_f = en.max;
        }
        // TODO: Avoid repetition here by making a common harvest drop function
        int max_drops = std::min<int>( en.max, std::round( std::max( 0.0f, max_f ) ) );
        int min_drops = std::max<int>( 0.0f, std::round( std::min( min_f, max_f ) ) );
        if( max_drops <= 0 ) {
            return std::string();
        }

        std::string ss;
        ss += "<bold>" + item::nname( itype_id( en.drop ), max_drops ) + "</bold>";
        // If the number is unspecified, just list the type
        if( max_drops >= 1000 && min_drops <= 0 ) {
            return ss;
        }
        ss += ": ";
        if( min_drops == max_drops ) {
            ss += string_format( "<stat>%d</stat>", min_drops );
        } else if( max_drops < 1000 ) {
            ss += string_format( "<stat>%d-%d</stat>", min_drops, max_drops );
        } else {
            ss += string_format( "<stat>%d+</stat>", min_drops );
        }

        return ss;
    } );
}

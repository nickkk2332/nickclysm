#include "vitamin.h"

#include <algorithm>
#include <cstddef>
#include <map>

#include "calendar.h"
#include "debug.h"
#include "enum_conversions.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "options.h"
#include "units.h"
#include "units_utility.h"

namespace
{
generic_factory<vitamin> vitamin_factory( "vitamin" );
} // namespace

/** @relates string_id */
template<>
bool string_id<vitamin>::is_valid() const
{
    return vitamin_factory.is_valid( *this );
}

/** @relates string_id */
template<>
const vitamin &string_id<vitamin>::obj() const
{
    return vitamin_factory.obj( *this );
}

int vitamin::severity( int qty ) const
{
    for( int i = 0; i != static_cast<int>( disease_.size() ); ++i ) {
        if( ( qty >= disease_[ i ].first && qty <= disease_[ i ].second ) ||
            ( qty <= disease_[ i ].first && qty >= disease_[ i ].second ) ) {
            return i + 1;
        }
    }
    for( int i = 0; i != static_cast<int>( disease_excess_.size() ); ++i ) {
        if( ( qty >= disease_excess_[ i ].first && qty <= disease_excess_[ i ].second ) ||
            ( qty <= disease_excess_[ i ].first && qty >= disease_excess_[ i ].second ) ) {
            return -i - 1;
        }
    }
    return 0;
}

void vitamin::load_vitamin( const JsonObject &jo, const std::string &src_name )
{
    vitamin_factory.load( jo, src_name );
}

// [ [ low, high ], ... ]: one range of quantities per intensity
static void read_disease_ranges( const JsonObject &jo, const std::string_view member,
                                 std::vector<std::pair<int, int>> &ranges )
{
    if( !jo.has_member( member ) ) {
        return;
    }
    ranges.clear();
    for( JsonArray e : jo.get_array( member ) ) {
        ranges.emplace_back( e.get_int( 0 ), e.get_int( 1 ) );
    }
}

// A copy (or a later mod's "copy-from" of the same id) keeps every member it doesn't name
// (DATA-J02).
void vitamin::load( const JsonObject &jo, std::string_view )
{
    optional( jo, was_loaded, "name", name_ );
    optional( jo, was_loaded, "deficiency", deficiency_, efftype_id::NULL_ID() );
    optional( jo, was_loaded, "excess", excess_, efftype_id::NULL_ID() );
    mandatory( jo, was_loaded, "min", min_ );
    optional( jo, was_loaded, "max", max_, 0 );
    if( jo.has_member( "rate" ) || !was_loaded ) {
        rate_ = read_from_json_string<time_duration>( jo.get_member( "rate" ), time_duration::units );
    }

    if( jo.has_string( "weight_per_unit" ) ) {
        weight_per_unit = read_from_json_string( jo.get_member( "weight_per_unit" ),
                          vitamin_units::mass_units );
    }

    if( jo.has_string( "vit_type" ) ) {
        type_ = jo.get_enum_value<vitamin_type>( "vit_type" );
    } else if( !was_loaded ) {
        jo.throw_error_at( "vit_type", "vitamin must have a vitamin type" );
    }

    read_disease_ranges( jo, "disease", disease_ );
    read_disease_ranges( jo, "disease_excess", disease_excess_ );

    if( jo.has_member( "decays_into" ) ) {
        decays_into_.clear();
        for( JsonArray e : jo.get_array( "decays_into" ) ) {
            decays_into_.emplace_back( vitamin_id( e.get_string( 0 ) ), e.get_int( 1 ) );
        }
    }

    optional( jo, was_loaded, "flags", flags_, string_reader{} );
}

void vitamin::finalize_all()
{
    vitamin_factory.finalize();
}

const std::map<vitamin_id, vitamin> &vitamin::all()
{
    // By id, as they always were; rebuilt when the factory changes.
    static std::map<vitamin_id, vitamin> by_id;
    static generic_factory<vitamin>::Version built;
    if( !vitamin_factory.is_valid( built ) ) {
        by_id.clear();
        for( const vitamin &vit : vitamin_factory.get_all() ) {
            by_id.emplace( vit.id, vit );
        }
        built = vitamin_factory.get_version();
    }
    return by_id;
}

void vitamin::check_consistency()
{
    for( const vitamin &v : vitamin_factory.get_all() ) {
        v.check();
    }
}

void vitamin::check() const
{
    if( !( deficiency_.is_null() || deficiency_.is_valid() ) ) {
        debugmsg( "vitamin %s has unknown deficiency %s", id.c_str(), deficiency_.c_str() );
    }
    if( !( excess_.is_null() || excess_.is_valid() ) ) {
        debugmsg( "vitamin %s has unknown excess %s", id.c_str(), excess_.c_str() );
    }
    // HLTH-J05: the rest of what a vitamin refers to and the ranges it sets
    if( min_ > max_ ) {
        debugmsg( "vitamin %s has a min (%d) above its max (%d)", id.c_str(), min_, max_ );
    }
    for( const std::pair<vitamin_id, int> &into : decays_into_ ) {
        if( into.first == id ) {
            debugmsg( "vitamin %s decays into itself", id.c_str() );
        } else if( !into.first.is_valid() ) {
            debugmsg( "vitamin %s decays into unknown vitamin %s", id.c_str(), into.first.c_str() );
        }
    }
    // A level stays within [min, max], so an intensity whose range lies outside it is never
    // reached (the order of the two bounds of a range doesn't matter)
    const auto check_ranges = [this]( const std::vector<std::pair<int, int>> &ranges,
    const char *member ) {
        for( size_t i = 0; i < ranges.size(); ++i ) {
            const int low = std::min( ranges[i].first, ranges[i].second );
            const int high = std::max( ranges[i].first, ranges[i].second );
            if( high < min_ || low > max_ ) {
                debugmsg( "vitamin %s: intensity %d of its \"%s\" ([%d, %d]) is outside its "
                          "range [%d, %d] and can never be reached", id.c_str(),
                          static_cast<int>( i ) + 1, member, ranges[i].first, ranges[i].second,
                          min_, max_ );
            }
        }
    };
    check_ranges( disease_, "disease" );
    check_ranges( disease_excess_, "disease_excess" );
}

void vitamin::reset()
{
    vitamin_factory.reset();
}

float vitamin::RDA_to_default( int percent ) const
{
    // if not a vitamin it's in Units and doesn't need conversion
    if( type_ != vitamin_type::VITAMIN ) {
        return percent;
    }
    return 24_hours * ( percent / 100.0f ) / rate_;
}

int vitamin::units_absorption_per_day() const
{
    return ( 24_hours / rate_ );
}

int vitamin::units_from_mass( vitamin_units::mass val ) const
{
    if( !weight_per_unit.has_value() ) {
        debugmsg( "Tried to convert vitamin in mass to units, but %s doesn't support mass for vitamins",
                  id.str() );
        return 1;
    }
    return val / *weight_per_unit;
}

std::pair<std::string, std::string> vitamin::mass_str_from_units( int units ) const
{
    if( !weight_per_unit.has_value() || !get_option<bool>( "SHOW_VITAMIN_MASS" ) ) {
        return {"", ""};
    }
    return weight_to_string( units * *weight_per_unit );
}

namespace io
{
template<>
std::string enum_to_string<vitamin_type>( vitamin_type data )
{
    switch( data ) {
        case vitamin_type::VITAMIN:
            return "vitamin";
        case vitamin_type::TOXIN:
            return "toxin";
        case vitamin_type::DRUG:
            return "drug";
        case vitamin_type::COUNTER:
            return "counter";
        case vitamin_type::num_vitamin_types:
            break;
    }
    cata_fatal( "Invalid vitamin_type" );
}
} // namespace io

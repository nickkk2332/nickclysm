#include "shearing.h"

#include <algorithm>
#include <string>
#include <vector>

#include "calendar.h"
#include "enum_conversions.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "monster.h"
#include "output.h"
#include "rng.h"
#include "string_formatter.h"
#include "units.h"

shearing_roll shearing_entry::roll( const monster &mon ) const
{
    shearing_roll roll;
    roll.result = result;

    roll.amount += amount;

    if( random_max ) {
        roll.amount += rng( random_min, random_max );
    }

    if( ratio_mass ) {
        float weight = units::to_kilogram( mon.get_weight() );
        roll.amount += static_cast<int>( ratio_mass * weight );
    }

    if( ratio_volume ) {
        float volume = units::to_liter( mon.get_volume() );
        roll.amount += static_cast<int>( ratio_volume * volume );
    }

    return roll;
}

void shearing_entry::load( const JsonObject &jo )
{
    mandatory( jo, was_loaded, "result", result );

    optional( jo, was_loaded, "ratio_mass", ratio_mass, numeric_bound_reader<float> {0.f} );

    optional( jo, was_loaded, "ratio_volume", ratio_volume, numeric_bound_reader<float> {0.f} );

    if( jo.has_int( "amount" ) ) {
        mandatory( jo, was_loaded, "amount", amount, numeric_bound_reader<int> {0} );
    } else if( jo.has_array( "amount" ) ) {
        std::vector<int> amount_random = jo.get_int_array( "amount" );
        random_min = std::max( 0, amount_random[0] );
        random_max = std::max( 0, amount_random[1] );
        if( random_min > random_max ) {
            std::swap( random_min, random_max );
        }
    }
}

// "SPRING", "summer"...
static season_type read_season( const JsonValue &jv )
{
    const std::string name = to_lower_case( jv.get_string() );
    for( int season = SPRING; season < NUM_SEASONS; ++season ) {
        if( io::enum_to_string( static_cast<season_type>( season ) ) == name ) {
            return static_cast<season_type>( season );
        }
    }
    jv.throw_error( string_format( "invalid season \"%s\"", name ) );
}

// Rules not given keep what they were (from copy-from, or the defaults).
void shearing_data::deserialize( const JsonValue &jv )
{
    if( jv.test_object() ) {
        JsonObject jo = jv.get_object();
        mandatory( jo, false, "entries", entries_ );
        if( jo.has_member( "seasons" ) ) {
            seasons_.clear();
            for( const JsonValue jv_season : jo.get_array( "seasons" ) ) {
                seasons_.insert( read_season( jv_season ) );
            }
        }
        if( jo.has_member( "regrow" ) ) {
            time_duration regrow;
            mandatory( jo, false, "regrow", regrow );
            regrow_ = regrow;
        }
        if( jo.has_member( "quality_level" ) ) {
            mandatory( jo, false, "quality_level", quality_level_, numeric_bound_reader<int> { 1 } );
        }
    } else {
        jv.read( entries_, true );
    }
    valid_ = !entries_.empty();
}

shearing_data::shearing_data( std::vector<shearing_entry> &shearing_entries )
{
    if( !shearing_entries.empty() ) {
        entries_ = shearing_entries;
        valid_ = true;
    }
}

std::vector<shearing_roll> shearing_data::roll_all( const monster &mon ) const
{
    std::vector<shearing_roll> rolls;
    rolls.reserve( entries_.size() );

    for( const shearing_entry &entry : entries_ ) {
        rolls.push_back( entry.roll( mon ) );
    }

    return rolls;
}

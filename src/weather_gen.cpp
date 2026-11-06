#include "weather_gen.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <string>

#include "avatar.h"
#include "cata_utility.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "generic_factory.h"
#include "math_defines.h"
#include "phase_change.h"
#include "pimpl.h"
#include "point.h"
#include "rng.h"
#include "simplexnoise.h"
#include "talker.h"
#include "translation.h"
#include "weather.h"
#include "weather_sample_count.h"
#include "weather_type.h"

namespace
{
constexpr double tau = 2 * M_PI;
constexpr double coldest_hour = 5;
// Out of 24 hours
constexpr double daily_magnitude_K = 5;
// Greatest absolute change from a day's average temperature, in kelvins
// Daily variation on temperatures averages from 12.9-16.6F (7.16-9.22) with a stdev. of 5-7F depending on the day of the year.
constexpr double daily_seasonal_range_K = 2.0;
// Increases daily temperature variation by this much at the coldest time of the year, decreases it this much at the warmest time of the year, In kelvins.
constexpr double seasonality_magnitude_K = 12;
// Greatest absolute change from the year's average temperature, in kelvins
// Mean normal monthly temperatures over the year is 30.47-73.86F (-0.85-23.25C)
// Annual recorded minimum to maximum temperatures range from ~10-90F (-17.78-37.78C), lows of 0F & highs of 100F happen every few years. Any and all noise should stay bound within this range.
constexpr double noise_magnitude_K = 6;
// Greatest absolute change from a day's average through noise, in kelvins
constexpr double seasonal_noise_magnitude = 0.5;
// Multiplier for noise magnitude that varies with the season, 0.5 means noise is 1.5x as strong in midwinter and 1.0x as strong in midsummer.
} //namespace

weather_generator::weather_generator() = default;
int weather_generator::current_winddir = 1000;

namespace
{
std::atomic<uint64_t> weather_samples{ 0 };
} // namespace

uint64_t weather_samples_taken()
{
    return weather_samples.load( std::memory_order_relaxed );
}

namespace
{
generic_factory<weather_generator> weather_generator_factory( "weather_generator" );
} // namespace
template<>
const weather_generator &string_id<weather_generator>::obj() const
{
    return weather_generator_factory.obj( *this );
}
template<>
bool string_id<weather_generator>::is_valid() const
{
    return weather_generator_factory.is_valid( *this );
}
void weather_generator::load_weather_generator( const JsonObject &jo,
        const std::string &src )
{
    weather_generator_factory.load( jo, src );
}
void weather_generator::reset()
{
    weather_generator_factory.reset();
}
void weather_generator::finalize()
{
    sort_weather();
}
void weather_generator::finalize_all()
{
    weather_generator_factory.finalize();
}
namespace
{
struct weather_gen_common {
    double x = 0;
    double y = 0;
    double z = 0;
    double cyf = 0;
    unsigned modSEED = 0u;
    season_type season = season_type::SPRING;
};
} // namespace

namespace
{
// splitmix64's finaliser: every bit of the input reaches every bit of the output
uint64_t mix_bits( uint64_t z )
{
    z += 0x9e3779b97f4a7c15ULL;
    z = ( z ^ ( z >> 30 ) ) * 0xbf58476d1ce4e5b9ULL;
    z = ( z ^ ( z >> 27 ) ) * 0x94d049bb133111ebULL;
    return z ^ ( z >> 31 );
}

// Uniform draws in [0, 1) that depend only on the seed, the map square and the turn, so the
// generator gives the same wind every time it is asked about the same time and place.
class wind_draws
{
    public:
        wind_draws( unsigned seed, const tripoint_abs_ms &location, const time_point &t ) {
            const point_abs_omt omt = project_to<coords::omt>( location.xy() );
            state = mix_bits( seed );
            state = mix_bits( state ^ static_cast<uint32_t>( omt.x() ) );
            state = mix_bits( state ^ static_cast<uint32_t>( omt.y() ) );
            state = mix_bits( state ^ static_cast<uint64_t>( to_turn<int64_t>( t ) ) );
        }
        double next() {
            state = mix_bits( state );
            // The top 53 bits, as a fraction of 2^53
            return static_cast<double>( state >> 11 ) / 9007199254740992.0;
        }
    private:
        uint64_t state = 0;
};

// How often the wind blows from each of the 16 compass points, by season
std::array<int, 16> wind_direction_weights( const season_type season )
{
    //TODO: Unhardcode?
    switch( season ) {
        case SPRING:
            return { { 3, 3, 5, 8, 11, 10, 5, 2, 5, 6, 6, 5, 8, 10, 8, 6 } };
        case SUMMER:
            return { { 3, 4, 4, 8, 8, 9, 8, 3, 7, 8, 10, 7, 7, 7, 5, 3 } };
        case AUTUMN:
            return { { 4, 6, 6, 7, 6, 5, 4, 3, 5, 6, 8, 8, 10, 10, 8, 5 } };
        case WINTER:
            return { { 5, 3, 2, 3, 2, 2, 2, 2, 4, 6, 10, 8, 12, 19, 13, 9 } };
        default:
            return { { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
    }
}

// The compass point (0 to 15) a draw in [0, 1) picks, with the season's chances
int wind_direction_from_draw( const season_type season, const double draw )
{
    const std::array<int, 16> weights = wind_direction_weights( season );
    int total = 0;
    for( const int w : weights ) {
        total += w;
    }
    double left = draw * total;
    for( size_t i = 0; i < weights.size(); i++ ) {
        left -= weights[i];
        if( left < 0 ) {
            return static_cast<int>( i );
        }
    }
    return static_cast<int>( weights.size() ) - 1;
}
} // namespace

// The parts of weather_gen_common that depend on the place.
static void set_common_location( weather_gen_common &result, const tripoint_abs_ms &location,
                                 unsigned seed )
{
    // Integer x position / widening factor of the Perlin function.
    result.x = location.x() / 2000.0;
    // Integer y position / widening factor of the Perlin function.
    result.y = location.y() / 2000.0;
    // Limit the random seed during noise calculation, a large value flattens the noise generator to zero
    // Windows has a rand limit of 32768, other operating systems can have higher limits
    result.modSEED = seed % SIMPLEX_NOISE_RANDOM_SEED_LIMIT;
}

// The parts of weather_gen_common that depend on the time.
static void set_common_time( weather_gen_common &result, const time_point &real_t )
{
    season_effective_time t( real_t );
    // Integer turn / widening factor of the Perlin function.
    result.z = to_days<double>( real_t - calendar::turn_zero );
    const double year_fraction( time_past_new_year( t.t ) /
                                calendar::year_length() ); // [0,1)
    // We add an offset to line up `cyf` so that 1 is at
    // midwinter and -1 at midsummer. (Cataclsym DDA years
    // start season_length - turn_zero_offset days into winter.
    // Gregorian years start when winter starts.)
    // LATER: Do gregorian years start on winter? They start ~10 days into winter...
    const double days_year_start_to_midwinter = to_days<double>( ( calendar::season_length() / 2 ) -
            ( calendar::season_length() - calendar::turn_zero_offset() ) );
    const double offset = days_year_start_to_midwinter / to_days<double>( calendar::year_length() );
    result.cyf = std::cos( tau * ( year_fraction - offset ) ); // [-1, 1]
    result.season = season_of_year( t.t );
}

static weather_gen_common get_common_data( const tripoint_abs_ms &location,
        const time_point &real_t,
        unsigned seed )
{
    weather_gen_common result;
    set_common_location( result, location, seed );
    set_common_time( result, real_t );
    return result;
}

namespace
{
// The terms of the temperature that depend only on the time: the noise-free baseline and how
// strongly the noise counts.
struct weather_temperature_terms {
    double baseline = 0;
    double noise_scale = 0;
};
} // namespace

static weather_temperature_terms weather_temperature_terms_from_common_data(
    const weather_generator &wg, const weather_gen_common &common, const season_effective_time &t )
{
    const double seasonality = -common.cyf;
    // -1 in midwinter, +1 in midsummer
    const season_type season = common.season;
    const double dayFraction = time_past_midnight( t.t ) / 1_days;
    const double dayv = std::cos( tau * ( dayFraction + .5 - coldest_hour / 24 ) );
    // -1 at coldest_hour, +1 twelve hours later

    // manually specified seasonal temp variation from region_settings.json
    const std::array<int, 4> seasonal_temp_mod = {
        wg.spring_temp_manual_mod, wg.summer_temp_manual_mod, wg.autumn_temp_manual_mod,
        wg.winter_temp_manual_mod
    };
    weather_temperature_terms terms;
    terms.baseline =
        wg.base_temperature +
        seasonal_temp_mod[season] +
        dayv * ( daily_magnitude_K + daily_seasonal_range_K * ( -seasonality + 1 ) / 2 ) +
        seasonality * seasonality_magnitude_K;
    terms.noise_scale = 1 + ( 1 + -seasonality ) * seasonal_noise_magnitude / 2;
    return terms;
}

static units::temperature weather_temperature_from_terms( const weather_temperature_terms &terms,
        const weather_gen_common &common )
{
    const double T = terms.baseline + raw_noise_4d( common.x, common.y, common.z, common.modSEED ) *
                     terms.noise_scale * noise_magnitude_K;

    return units::from_celsius( T );
}

static units::temperature weather_temperature_from_common_data( const weather_generator &wg,
        const weather_gen_common &common, const season_effective_time &t )
{
    return weather_temperature_from_terms( weather_temperature_terms_from_common_data( wg, common, t ),
                                           common );
}

weather_temperature_sampler::weather_temperature_sampler( const weather_generator &wgen,
        const std::vector<time_point> &times )
{
    samples.reserve( times.size() );
    for( const time_point &t : times ) {
        weather_gen_common common;
        set_common_time( common, t );
        const weather_temperature_terms terms = weather_temperature_terms_from_common_data( wgen, common,
                                                season_effective_time( t ) );
        samples.push_back( { common.z, terms.baseline, terms.noise_scale } );
    }
}

units::temperature weather_temperature_sampler::temperature( size_t i,
        const tripoint_abs_ms &location, unsigned seed ) const
{
    weather_gen_common common;
    set_common_location( common, location, seed );
    common.z = samples[i].z;
    weather_temperature_terms terms;
    terms.baseline = samples[i].baseline;
    terms.noise_scale = samples[i].noise_scale;
    return weather_temperature_from_terms( terms, common );
}

units::temperature weather_generator::get_weather_temperature(
    const tripoint_abs_ms &location, const time_point &real_t, unsigned seed ) const
{
    return weather_temperature_from_common_data( *this, get_common_data( location, real_t, seed ),
            season_effective_time( real_t ) );
}
w_point weather_generator::get_weather( const tripoint_abs_ms &location, const time_point &real_t,
                                        unsigned seed ) const
{
    weather_samples.fetch_add( 1, std::memory_order_relaxed );
    season_effective_time t( real_t );
    const weather_gen_common common = get_common_data( location, real_t, seed );

    const double x( common.x );
    const double y( common.y );
    const double z( common.z );

    const unsigned modSEED = common.modSEED;
    const double cyf( common.cyf );
    const double seasonality = -common.cyf;
    // -1 in midwinter, +1 in midsummer
    const season_type season = common.season;

    // Noise factors
    const units::temperature T( weather_temperature_from_common_data( *this, common, t ) );
    double W( raw_noise_4d( x / 2.5, y / 2.5, z / 200, modSEED ) * 10.0 );

    // Humidity variation
    double mod_h( 0 );
    if( season == WINTER ) {
        mod_h += winter_humidity_manual_mod;
    } else if( season == SPRING ) {
        mod_h += spring_humidity_manual_mod;
    } else if( season == SUMMER ) {
        mod_h += summer_humidity_manual_mod;
    } else if( season == AUTUMN ) {
        mod_h += autumn_humidity_manual_mod;
    }
    // Relative humidity, a percentage.
    double H = std::min( 100., std::max( 0.,
                                         base_humidity + mod_h + 100 * (
                                                 .15 * seasonality +
                                                 raw_noise_4d( x, y, z, modSEED + 101 ) *
                                                 .2 * ( -seasonality + 2 ) ) ) );

    // Pressure
    double P =
        base_pressure +
        raw_noise_4d( x, y, z, modSEED + 211 ) *
        15 * ( -seasonality + 2 );

    // Wind power. Its random parts come from the seed, place and time, so asking about the same
    // time and place again gives the same wind, and asking changes nothing.
    wind_draws draw( seed, location, real_t );
    const double variation_draw = draw.next();
    const double gust_draw = draw.next();
    const double peaks_draw = draw.next();
    const double direction_draw = draw.next();
    const double variation = base_wind_season_variation == 0 ? 0 :
                             cyf * base_wind_season_variation * 2.0 * variation_draw;
    // As rng( 1, 2 ) and rng( 9, base_wind_distrib_peaks ) were, which swaps a reversed range
    const int gust = gust_draw < 0.5 ? 1 : 2;
    const int peaks_low = std::min( 9, base_wind_distrib_peaks );
    const int peaks_high = std::max( 9, base_wind_distrib_peaks );
    const int peaks = peaks_low + std::min( peaks_high - peaks_low,
                                            static_cast<int>( peaks_draw * ( peaks_high - peaks_low + 1 ) ) );
    W = std::max( 0, static_cast<int>( base_wind * gust / std::pow( ( P + W ) / 1014.78,
                                       peaks ) + variation ) );

    // The direction this time and place brings. The live wind keeps its own direction and only
    // takes this one now and then, in weather_manager::update_weather().
    const int winddir = convert_winddir( wind_direction_from_draw( season, direction_draw ) );
    std::string wind_desc = get_wind_desc( W );
    return w_point{ T, H, P, W, wind_desc, winddir, t, location };
}

weather_type_id weather_generator::get_weather_conditions( const tripoint_abs_ms &location,
        const time_point &t, unsigned seed ) const
{
    w_point w( get_weather( location, t, seed ) );
    weather_type_id wt = get_weather_conditions( w );
    return wt;
}

weather_type_id weather_generator::get_weather_conditions( const w_point &w ) const
{
    // We're being asked for the weather condition given a set of parameters (humidity, pressure, etc),
    // but the dialogue condition system which drives that logic has no way for us to provide them
    // directly; it can only reference the current game state. Until it's overhauled, we'll just hack
    // the current game state while checking the conditions.
    const weather_manager &game_weather = get_weather_const();
    w_point original_weather_precise = *game_weather.weather_precise;
    *game_weather.weather_precise = w;
    weather_type_id current_conditions = WEATHER_CLEAR;
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    d.set_value( "weather_location", w.location );

    for( const weather_type_id &type : sorted_weather ) {
        bool required_weather = type->required_weathers.empty();
        if( !required_weather ) {
            for( const weather_type_id &weather : type->required_weathers ) {
                if( weather == current_conditions ) {
                    required_weather = true;
                    break;
                }
            }
        }

        if( required_weather && type->condition( d ) ) {
            current_conditions = type;
            continue;
        }
    }

    // Cleanup our conditional hack.
    *game_weather.weather_precise = original_weather_precise;
    return current_conditions;
}

int weather_generator::get_wind_direction( const season_type season ) const
{
    return wind_direction_from_draw( season, rng_float( 0, 1 ) );
}

int weather_generator::convert_winddir( const int inputdir ) const
{
    // Convert from discrete distribution output to angle
    float finputdir = inputdir * 22.5f;
    return static_cast<int>( finputdir );
}

units::temperature weather_generator::get_water_temperature() const
{
    /**
    WATER TEMPERATURE
    source : http://echo2.epfl.ch/VICAIRE/mod_2/chapt_5/main.htm
    source : http://www.grandriver.ca/index/document.cfm?Sec=2&Sub1=7&sub2=1
    **/

    season_effective_time t( calendar::turn );
    int season_length = to_days<int>( calendar::season_length() );
    // Days since the start of spring (turn_zero), where the yearly curve below starts.
    // time_past_new_year() counts from Jan 1, 81 days later.
    int day = to_days<int>( ( t.t - calendar::turn_zero ) % calendar::year_length() );
    int hour = hour_of_day<int>( t.t );

    float water_temperature = 0;

    if( season_length == 0 ) {
        season_length = 1;
    }

    // Temperature varies between 33.8F and 75.2F depending on the time of year. Day = 0 corresponds to the start of spring.
    float annual_mean_water_temperature = 54.5 + 20.7 * std::sin( tau * ( day - season_length * 0.5 ) /
                                          ( season_length * 4.0 ) );
    // Temperature varies between +2F and -2F depending on the time of day. Hour = 0 corresponds to midnight.
    float daily_water_temperature_variation = 2.0 + 2.0 * std::sin( tau * ( hour - 6.0 ) / 24.0 );

    water_temperature = annual_mean_water_temperature + daily_water_temperature_variation;

    return units::from_fahrenheit( water_temperature );
}

void weather_generator::test_weather( unsigned seed ) const
{
    // Outputs a Cata year's worth of weather data to a CSV file.
    // Usage:
    // weather_generator WEATHERGEN; // Instantiate the class.
    // WEATHERGEN.test_weather(); // Runs this test.
    write_to_file( "weather.output", [&]( std::ostream & testfile ) {
        testfile <<
                 "|;year;season;day;hour;minute;temperature(F);humidity(%);pressure(mB);weatherdesc;windspeed(mph);winddirection"
                 << std::endl;

        const time_point begin = calendar::turn;
        const time_point end = begin + 2 * calendar::year_length();
        for( time_point i = begin; i < end; i += 20_minutes ) {
            w_point w = get_weather( tripoint_abs_ms::zero, i, seed );
            weather_type_id conditions = get_weather_conditions( w );

            int year = to_turns<int>( i - calendar::turn_zero ) / to_turns<int>
                       ( calendar::year_length() ) + 1;
            const int hour = hour_of_day<int>( i );
            const int minute = minute_of_hour<int>( i );
            int day;
            if( calendar::eternal_season() ) {
                day = to_days<int>( time_past_new_year( i ) );
            } else {
                day = day_of_season<int>( i );
            }
            testfile << "|;" << year << ";" << season_of_year( i ) << ";" << day << ";" << hour << ";" << minute
                     << ";" << units::to_fahrenheit( w.temperature ) << ";" << w.humidity << ";" << w.pressure << ";" <<
                     conditions->name << ";"
                     <<
                     w.windpower << ";" << w.winddirection << std::endl;
        }

    }, "weather test file" );
}

void weather_generator::sort_weather()
{
    sorted_weather.clear();
    for( const weather_type &wt : weather_types::get_all() ) {
        // if we have a white list, only add those, if we have a black list, add all but those
        if( weather_white_list.empty() ) {
            if( std::find( weather_black_list.begin(), weather_black_list.end(),
                           wt.id.c_str() ) == weather_black_list.end() ) {
                sorted_weather.push_back( wt.id );
            }
        } else if( std::find( weather_white_list.begin(), weather_white_list.end(),
                              wt.id.c_str() ) != weather_white_list.end() || wt.id == WEATHER_CLEAR ) {
            sorted_weather.push_back( wt.id );
        }
    }
    std::sort( sorted_weather.begin(), sorted_weather.end(), []( const weather_type_id & a,
    const weather_type_id & b ) {
        return a->priority < b->priority;
    } );
}

void weather_generator::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "base_temperature", base_temperature );
    mandatory( jo, was_loaded, "base_humidity", base_humidity );
    mandatory( jo, was_loaded, "base_pressure", base_pressure );
    mandatory( jo, was_loaded, "base_wind", base_wind );
    optional( jo, was_loaded, "base_wind_distrib_peaks", base_wind_distrib_peaks );
    optional( jo, was_loaded, "base_wind_season_variation", base_wind_season_variation );
    optional( jo, was_loaded, "summer_temp_manual_mod", summer_temp_manual_mod, 0 );
    optional( jo, was_loaded, "spring_temp_manual_mod", spring_temp_manual_mod, 0 );
    optional( jo, was_loaded, "autumn_temp_manual_mod", autumn_temp_manual_mod, 0 );
    optional( jo, was_loaded, "winter_temp_manual_mod", winter_temp_manual_mod, 0 );
    optional( jo, was_loaded, "spring_humidity_manual_mod", spring_humidity_manual_mod, 0 );
    optional( jo, was_loaded, "summer_humidity_manual_mod", summer_humidity_manual_mod, 0 );
    optional( jo, was_loaded, "autumn_humidity_manual_mod", autumn_humidity_manual_mod, 0 );
    optional( jo, was_loaded, "winter_humidity_manual_mod", winter_humidity_manual_mod, 0 );
    optional( jo, was_loaded, "weather_black_list", weather_black_list );
    optional( jo, was_loaded, "weather_white_list", weather_white_list );
    if( !weather_black_list.empty() && !weather_white_list.empty() ) {
        jo.throw_error( "weather_black_list and weather_white_list are mutually exclusive" );
    }
}

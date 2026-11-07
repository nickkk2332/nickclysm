#pragma once
#ifndef CATA_SRC_WEATHER_GEN_H
#define CATA_SRC_WEATHER_GEN_H

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "coordinates.h"
#include "point.h"
#include "type_id.h"
#include "units.h"

class JsonObject;

struct w_point {
    units::temperature temperature = 0_K;
    double humidity = 0;
    double pressure = 0;
    double windpower = 0;
    std::string wind_desc;
    int winddirection = 0;
    season_effective_time time;
    tripoint_abs_ms location;
};

class weather_generator
{
    public:
        weather_generator_id id = weather_generator_id::NULL_ID();

        // Average temperature
        double base_temperature = 0;
        // Average humidity
        double base_humidity = 0;
        // Average atmospheric pressure
        double base_pressure = 0;
        //Average yearly windspeed
        double base_wind = 0;
        //How much the wind peaks above average
        int base_wind_distrib_peaks = 0;
        // TODO: Store as arrays?
        int summer_temp_manual_mod = 0;
        int spring_temp_manual_mod = 0;
        int autumn_temp_manual_mod = 0;
        int winter_temp_manual_mod = 0;
        int spring_humidity_manual_mod = 0;
        int summer_humidity_manual_mod = 0;
        int autumn_humidity_manual_mod = 0;
        int winter_humidity_manual_mod = 0;
        //How much the wind follows seasonal variation ( lower means more change )
        int base_wind_season_variation = 0;
        // The shape of the climate. The defaults are the New England climate the game was made for.
        // Greatest change from the year's average temperature, midwinter to midsummer (K)
        double seasonal_amplitude_K = 12;
        // Greatest change from a day's average temperature (K), and how much more of it at midwinter
        // (and less at midsummer)
        double daily_amplitude_K = 5;
        double daily_seasonal_range_K = 2;
        // Hour of the day (0-24) that is coldest
        double coldest_hour = 5;
        // Greatest change from a day's average through noise (K), and how much stronger the noise
        // is in midwinter than in midsummer (0.5: 1.5 times)
        double temperature_noise_K = 6;
        double seasonal_noise_magnitude = 0.5;
        // Temperature underground (C); unset follows base_temperature
        std::optional<double> underground_temperature_C;
        // Water: a yearly curve (mean and amplitude) plus a daily one that is 0 at dawn and twice
        // the daily amplitude in the afternoon, all in Fahrenheit as the formula was written
        double water_yearly_mean_F = 54.5;
        double water_seasonal_amplitude_F = 20.7;
        double water_daily_amplitude_F = 2.0;
        // How often the wind blows from each of the 16 compass points (N, NNE ... NNW), by season
        std::array<std::array<int, 16>, 4> wind_direction_weights = { {
                { { 3, 3, 5, 8, 11, 10, 5, 2, 5, 6, 6, 5, 8, 10, 8, 6 } },
                { { 3, 4, 4, 8, 8, 9, 8, 3, 7, 8, 10, 7, 7, 7, 5, 3 } },
                { { 4, 6, 6, 7, 6, 5, 4, 3, 5, 6, 8, 8, 10, 10, 8, 5 } },
                { { 5, 3, 2, 3, 2, 2, 2, 2, 4, 6, 10, 8, 12, 19, 13, 9 } }
            }
        };
        static int current_winddir;
        std::vector<weather_type_id> weather_black_list;
        std::vector<weather_type_id> weather_white_list;
        /** All the current weather types based on white or black list and sorted by load order */
        std::vector<weather_type_id> sorted_weather;
        weather_generator();

        /**
         * Input should be an absolute position in the map square system (the one used
         * by the @ref map). You can use @ref map::getabs to get an absolute position from a
         * relative position (relative to the map you called getabs on).
         */
        w_point get_weather( const tripoint_abs_ms &, const time_point &, unsigned ) const;
        weather_type_id get_weather_conditions( const tripoint_abs_ms &, const time_point &,
                                                unsigned seed ) const;
        weather_type_id get_weather_conditions( const w_point & ) const;
        int get_wind_direction( season_type ) const;
        int convert_winddir( int ) const;
        units::temperature get_water_temperature() const;
        units::temperature get_underground_temperature() const;
        void test_weather( unsigned seed ) const;
        void sort_weather();
        units::temperature get_weather_temperature( const tripoint_abs_ms &, const time_point &,
                unsigned ) const;

        bool was_loaded = false;
        void load( const JsonObject &jo, std::string_view );
        void finalize();
        static void load_weather_generator( const JsonObject &jo, const std::string &src );
        static void reset();
        static void finalize_all();
};

#endif // CATA_SRC_WEATHER_GEN_H

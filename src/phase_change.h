#pragma once
#ifndef CATA_SRC_PHASE_CHANGE_H
#define CATA_SRC_PHASE_CHANGE_H

#include <cstddef>
#include <vector>

#include "calendar.h"
#include "coords_fwd.h"
#include "units.h"

class weather_generator;

// weather_generator::get_weather_temperature at a fixed list of times, for many places: the
// part of the work that depends only on the time (the season, the time of day and the
// noise-free baseline) is done once per time rather than once per place. The answers are the
// same as get_weather_temperature's, to the bit.
class weather_temperature_sampler
{
    public:
        weather_temperature_sampler( const weather_generator &wgen, const std::vector<time_point> &times );
        // wgen.get_weather_temperature( location, times[i], seed )
        units::temperature temperature( size_t i, const tripoint_abs_ms &location, unsigned seed ) const;

    private:
        struct sample {
            double z;
            double baseline;
            double noise_scale;
        };
        std::vector<sample> samples;
        // The generator's temperature_noise, the same for every time
        double noise_magnitude = 0;
};

// While one of these is alive, map::temp_based_phase_change_at works out the temperatures it
// freezes and thaws water by (a 10-day average at 08:00) with a weather_temperature_sampler,
// built once. A scope must not span a change of turn, game options or weather generator: the
// phase pass map::loadn runs over the submaps it loads is the intended scope.
class scoped_phase_temperatures
{
    public:
        explicit scoped_phase_temperatures( const weather_generator &wgen );
        ~scoped_phase_temperatures();
        scoped_phase_temperatures( const scoped_phase_temperatures & ) = delete;
        scoped_phase_temperatures &operator=( const scoped_phase_temperatures & ) = delete;
};

#endif // CATA_SRC_PHASE_CHANGE_H

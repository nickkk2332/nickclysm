#pragma once
#ifndef CATA_SRC_WEATHER_SAMPLE_COUNT_H
#define CATA_SRC_WEATHER_SAMPLE_COUNT_H

#include <cstdint>

// How many times weather_generator::get_weather() has run. Only tests read it, to check
// whether something sampled the weather.
uint64_t weather_samples_taken();

#endif // CATA_SRC_WEATHER_SAMPLE_COUNT_H

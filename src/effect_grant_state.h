#pragma once
#ifndef CATA_SRC_EFFECT_GRANT_STATE_H
#define CATA_SRC_EFFECT_GRANT_STATE_H

#include <map>
#include <optional>

#include "effect.h"

struct owned_effect_value {
    int intensity = 1;
    effect_source source;
};
struct owned_effect_state {
    std::optional<effect> native;
    std::map<int64_t, owned_effect_value> sources;
};

#endif // CATA_SRC_EFFECT_GRANT_STATE_H

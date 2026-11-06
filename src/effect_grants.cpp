#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "effect.h"
#include "effect_grant_state.h"
#include "effect_source.h"
#include "flexbuffer_json.h"
#include "int_id.h"
#include "json.h"

void effect::detach_owned()
{
    if( owned && owned.use_count() != 1 ) {
        owned = std::make_shared<owned_effect_state>( *owned );
    }
}

void effect::refresh_owned()
{
    if( !owned ) {
        return;
    }
    int64_t total = 0;
    duration = 1_turns;
    permanent = true;
    expires_at.reset();
    if( owned->native ) {
        total = owned->native->get_duration() > 0_turns ? owned->native->get_intensity() : 0;
        duration = std::max( duration, owned->native->get_duration() );
        if( owned->native->get_duration() > 0_turns ) {
            source = owned->native->get_source();
            start_time = owned->native->get_start_time();
        } else if( !owned->sources.empty() ) {
            source = owned->sources.begin()->second.source;
        }
    } else if( !owned->sources.empty() ) {
        source = owned->sources.begin()->second.source;
    }
    for( const auto &entry : owned->sources ) {
        total += entry.second.intensity;
    }
    intensity = static_cast<int>( std::clamp<int64_t>( total, 0, get_max_intensity() ) );
}

void effect::add_owned( int64_t token, int value, const effect_source &owner, bool keep_native )
{
    if( !owned ) {
        auto state = std::make_shared<owned_effect_state>();
        if( keep_native ) {
            state->native = *this;
        }
        owned = std::move( state );
    }
    detach_owned();
    owned->sources[token] = { value, owner };
    refresh_owned();
}

bool effect::remove_owned( int64_t token )
{
    if( !owned || !owned->sources.count( token ) ) {
        return get_duration() > 0_turns;
    }
    detach_owned();
    owned->sources.erase( token );
    if( owned->sources.empty() ) {
        if( owned->native ) {
            const effect restored = *owned->native;
            *this = restored;
            return get_duration() > 0_turns;
        }
        owned.reset();
        duration = 0_turns;
        intensity = 0;
        permanent = false;
        return false;
    }
    refresh_owned();
    return true;
}

bool effect::has_owned() const
{
    return owned && !owned->sources.empty();
}

std::vector<int64_t> effect::owned_tokens() const
{
    std::vector<int64_t> result;
    if( owned ) {
        for( const auto &entry : owned->sources ) {
            result.push_back( entry.first );
        }
    }
    return result;
}

void effect::merge_native( const effect_source &owner, const time_duration &dur,
                           bool perm, int value, bool alert )
{
    detach_owned();
    if( !owned->native ) {
        owned->native.emplace( owner, eff_type, perm ? std::max( dur, 1_turns ) : dur,
                               bp, perm, value, calendar::turn );
    } else {
        effect &base = *owned->native;
        base.mod_duration( dur * base.get_dur_add_perc() / 100 );
        if( base.get_int_dur_factor() == 0_turns ) {
            if( value > 0 ) {
                base.set_intensity( value, alert );
            } else if( base.get_int_add_val() != 0 ) {
                base.mod_intensity( base.get_int_add_val(), alert );
            }
        }
        base.clamp_intensity();
    }
    refresh_owned();
}

void effect::serialize_owned( JsonOut &out ) const
{
    if( !owned ) {
        return;
    }
    out.member( "owned_sources" );
    out.start_array();
    for( const auto &entry : owned->sources ) {
        out.start_object();
        out.member( "token", entry.first );
        out.member( "intensity", entry.second.intensity );
        out.member( "source", entry.second.source );
        out.end_object();
    }
    out.end_array();
    if( owned->native ) {
        out.member( "native_base", *owned->native );
    }
}

void effect::deserialize_owned( const JsonObject &jo )
{
    owned.reset();
    if( !jo.has_member( "owned_sources" ) ) {
        return;
    }
    auto state = std::make_shared<owned_effect_state>();
    for( const JsonObject entry : jo.get_array( "owned_sources" ) ) {
        int64_t token = 0;
        entry.read( "token", token, true );
        owned_effect_value value;
        entry.read( "intensity", value.intensity, true );
        entry.read( "source", value.source, true );
        if( token <= 0 || token >= std::numeric_limits<int64_t>::max() ||
            value.intensity < 1 || value.intensity > 1000000 || value.intensity > get_max_intensity() ||
            state->sources.size() >= 128 || !state->sources.emplace( token, value ).second ) {
            entry.throw_error( "Invalid owned effect source" );
        }
    }
    if( state->sources.empty() || get_id().str() == "knocked_out" ) {
        jo.throw_error( "Invalid owned effect" );
    }
    if( jo.has_object( "native_base" ) ) {
        const JsonObject base = jo.get_object( "native_base" );
        if( base.has_member( "owned_sources" ) || base.has_member( "native_base" ) ) {
            base.throw_error( "Nested owned effects are not supported" );
        }
        state->native.emplace();
        state->native->deserialize( base );
        if( state->native->get_id() != get_id() || state->native->get_bp() != get_bp() ) {
            base.throw_error( "Owned effect base must match its aggregate" );
        }
    }
    owned = std::move( state );
    refresh_owned();
}

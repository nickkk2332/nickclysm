#include "projection_eoc.h"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

#include "condition.h"
#include "creature.h"
#include "creature_tracker.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "generic_factory.h"
#include "map.h"
#include "power_projection.h"
#include "talker.h"

namespace
{
std::string role( const JsonObject &jo, const std::string &key, const std::string &fallback )
{
    const auto out = jo.get_string( key, fallback );
    if( out != "u" && out != "npc" ) {
        jo.throw_error_at( key, "expected u or npc" );
    }
    return out;
}
creature_ref actor( dialogue &d, const std::string &role )
{
    Creature *who = d.has_actor( role == "npc" ) ? d.actor( role == "npc" )->get_creature() : nullptr;
    return who && !who->is_fake() && !who->is_hallucination() && g->shared_from( *who ).get() == who ?
           creature_ref::bind( *who ) : creature_ref();
}
std::optional<int64_t> handle( const std::string &text )
{
    try {
        size_t used = 0;
        const auto id = std::stoll( text, &used );
        if( used == text.size() && id >= 0 ) {
            return id;
        }
    } catch( const std::invalid_argument & ) {
    } catch( const std::out_of_range & ) {
    }
    return std::nullopt;
}
}
std::function<void( dialogue & )> projection_eoc::create( const JsonObject &jo,
        std::string_view member, std::string_view )
{
    const auto args = jo.get_object( member );
    const auto id = get_str_or_var( args.get_member( "id" ), "id" );
    const auto owner = role( args, "owner", "u" );
    const auto target = role( args, "target", "npc" );
    const bool targeted = args.get_bool( "targeted", false );
    std::optional<var_info> loc;
    optional( args, false, "loc", loc );
    if( targeted && loc ) {
        args.throw_error( "targeted and loc are mutually exclusive" );
    }
    std::optional<dbl_or_var> strength;
    if( args.has_member( "strength" ) ) {
        strength = get_dbl_or_var( args, "strength", true );
    }
    str_or_var application;
    optional( args, false, "application", application, "0" );
    return [id, owner, target, targeted, loc, strength, application]( dialogue & d ) {
        power_projection_request request;
        request.owner = actor( d, owner );
        request.target = actor( d, target );
        request.definition = power_projection_id( id.evaluate( d ) );
        request.context = d.get_context();
        d.set_value( "power_projection", "0" );
        const auto parent = handle( application.evaluate( d ) );
        if( !parent ) {
            d.set_value( "reason", "invalid_application" );
            return;
        }
        request.application = *parent;
        if( strength ) {
            const double value = strength->evaluate( d );
            if( !std::isfinite( value ) || value < 1 || value > 1000000 || std::floor( value ) != value ) {
                d.set_value( "reason", "invalid_strength" );
                return;
            }
            request.strength = static_cast<int>( value );
        }
        if( loc ) {
            request.point = read_var_value( *loc, d ).tripoint();
        }
        if( targeted ) {
            Creature *source = request.owner.resolve();
            if( !source || !source->is_avatar() ) {
                d.set_value( "reason", "interactive_target_requires_avatar" );
                return;
            }
            const auto point = g->look_around();
            if( !point ) {
                d.set_value( "reason", "target_cancelled" );
                return;
            }
            Creature *who = get_creature_tracker().creature_at( *point );
            request.target = who ? creature_ref::bind( *who ) : creature_ref();
            if( !request.definition.is_valid() || !request.definition->track_target ) {
                request.point = get_map().get_abs( *point );
            }
        }
        const auto out = get_projections().create( request );
        d.set_value( "power_projection", std::to_string( out.id ) );
        d.set_value( "reason", out.reason );
    };
}
std::function<void( dialogue & )> projection_eoc::end( const JsonObject &jo,
        std::string_view member, std::string_view )
{
    const auto id = get_str_or_var( jo.get_member( member ), member );
    return [id]( dialogue & d ) {
        const auto value = handle( id.evaluate( d ) );
        const bool ended = value && get_projections().end( *value );
        d.set_value( "reason", ended ? "dispelled" : "not_found" );
    };
}
std::function<void( dialogue & )> projection_eoc::dispel( const JsonObject &jo,
        std::string_view member, std::string_view )
{
    const auto args = jo.get_object( member );
    const auto owner = role( args, "owner", "u" );
    str_or_var id;
    str_or_var tag;
    optional( args, false, "id", id, "" );
    optional( args, false, "tag", tag, "" );
    return [owner, id, tag]( dialogue & d ) {
        const auto ref = actor( d, owner );
        const auto filter = power_projection_id( id.evaluate( d ) );
        const auto tag_filter = tag.evaluate( d );
        const int count = ref.resolve() ? get_projections().dispel( ref, filter, tag_filter ) : 0;
        d.set_value( "power_projections_dispelled", count );
        d.set_value( "reason", count ? "dispelled" : "not_found" );
    };
}

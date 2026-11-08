#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "creature.h"
#include "effect.h"
#include "effect_source.h"
#include "event.h"
#include "event_bus.h"
#include "flat_set.h"
#include "pimpl.h"
#include "type_id.h"

static const json_character_flag json_flag_CANNOT_GAIN_EFFECTS( "CANNOT_GAIN_EFFECTS" );

std::string Creature::check_effect_grant( const efftype_id &id, bodypart_id bp,
        int intensity ) const
{
    if( !id.is_valid() || id.str() == "knocked_out" || intensity < 1 || intensity > 1000000 ||
        intensity > effect( &*id ).get_max_intensity() ) {
        return "invalid_effect_grant";
    }
    if( bp != bodypart_str_id::NULL_ID() && ( !bp.is_valid() || !has_part( bp ) ) ) {
        return "bodypart_unavailable";
    }
    if( is_immune_effect( id ) || has_flag( json_flag_CANNOT_GAIN_EFFECTS ) ) {
        return "effect_immune";
    }
    if( bp != bodypart_str_id::NULL_ID() ) {
        if( id->get_main_parts() ) {
            bp = bp->main_part;
        }
        for( const auto &flag : id->immune_bp_flags ) {
            if( bp->has_flag( flag ) ) {
                return "effect_immune";
            }
        }
    } else if( id->get_main_parts() ) {
        return "bodypart_required";
    }
    for( const auto &entry : *effects ) {
        for( const auto &part : entry.second ) {
            const auto &blocked = part.second.get_blocks_effects();
            if( std::find( blocked.begin(), blocked.end(), id ) != blocked.end() ) {
                return "effect_blocked";
            }
        }
    }
    return {};
}

bool Creature::add_effect_grant( int64_t token, const effect_source &source,
                                 const efftype_id &id, bodypart_id bp, int intensity )
{
    if( token <= 0 || !check_effect_grant( id, bp, intensity ).empty() ) {
        return false;
    }
    if( id->get_main_parts() ) {
        bp = bp->main_part;
    }
    auto &parts = ( *effects )[id];
    auto found = parts.find( bp );
    const bool existed = found != parts.end();
    if( !existed ) {
        found = parts.emplace( bp, effect( source, &*id, 1_turns, bp.id(), true,
                                           intensity, calendar::turn ) ).first;
    }
    effect &value = found->second;
    const auto sources = value.owned_tokens();
    if( sources.size() >= 128 && std::find( sources.begin(), sources.end(), token ) == sources.end() ) {
        return false;
    }
    const int previous = existed ? value.get_intensity() : 0;
    value.add_owned( token, intensity, source, existed );
    const int now = value.get_intensity();
    effect snapshot = value;
    on_effect_int_change( id, now, bp );
    if( Character *person = as_character() ) {
        if( !existed ) {
            get_event_bus().send<event_type::character_gains_effect>( person->getID(), bp.id(), id, now );
            if( is_avatar() ) {
                id->add_apply_msg( now );
            }
        } else if( previous != now ) {
            get_event_bus().send<event_type::character_effect_intensity_changed>( person->getID(), bp.id(), id,
                    now );
        }
    }
    const auto present = [&]() {
        const auto tokens = get_effect( id, bp ).owned_tokens();
        return std::find( tokens.begin(), tokens.end(), token ) != tokens.end();
    };
    if( !existed && present() ) {
        process_one_effect( snapshot, true );
    }
    // EVT-J04: last, as the EOCs may change this creature's effects
    if( !existed && present() ) {
        run_effect_eocs( effect_eoc_hook::apply, *id, bp, now, 0, source );
    } else if( existed && previous != now ) {
        run_effect_eocs( effect_eoc_hook::intensity_change, *id, bp, now, previous, source );
    }
    return present();
}

void Creature::remove_effect_grant( int64_t token )
{
    std::vector<std::pair<efftype_id, bodypart_id>> cells;
    for( const auto &entry : *effects ) {
        for( const auto &part : entry.second ) {
            const auto tokens = part.second.owned_tokens();
            if( std::find( tokens.begin(), tokens.end(), token ) != tokens.end() ) {
                cells.emplace_back( entry.first, part.first );
            }
        }
    }
    for( const auto &cell : cells ) {
        const auto found = effects->find( cell.first );
        if( found == effects->end() ) {
            continue;
        }
        const auto part = found->second.find( cell.second );
        if( part == found->second.end() ) {
            continue;
        }
        effect &value = part->second;
        const int before = value.get_intensity();
        if( !value.remove_owned( token ) ) {
            remove_effect( cell.first, cell.second );
        } else if( before != value.get_intensity() ) {
            const int after = value.get_intensity();
            const effect_source source = value.get_source();
            on_effect_int_change( cell.first, after, cell.second );
            if( Character *person = as_character() ) {
                get_event_bus().send<event_type::character_effect_intensity_changed>( person->getID(),
                        cell.second.id(), cell.first, after );
            }
            run_effect_eocs( effect_eoc_hook::intensity_change, *cell.first, cell.second, after,
                             before, source );
        }
    }
}

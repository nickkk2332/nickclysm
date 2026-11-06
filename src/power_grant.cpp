#include "power_grant.h"

#include <algorithm>
#include <list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

#include "ability_runtime.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "creature.h"
#include "effect.h"
#include "effect_source.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "json.h"
#include "map.h"
#include "map_selector.h"
#include "mapbuffer.h"
#include "memory_fast.h"
#include "power_runtime.h"
#include "submap.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "vehicle_selector.h"

namespace
{
constexpr size_t record_limit = 160;
struct grant_record {
    power_grant_view view;
    tripoint_abs_ms position;
    time_point updated = calendar::before_time_starts;
    std::list<item> garments;
    std::optional<item> weapon;
};
bool marked( const item &value, int64_t application, const dimension_id &dimension )
{
    return value.get_var( "power_grant_application", "" ) == std::to_string( application ) &&
           value.get_var( "power_grant_dimension", "" ) == dimension.str();
}
void mark( item &value, const power_application_view &source )
{
    value.set_var( "power_grant_application", std::to_string( source.id ) );
    value.set_var( "power_grant_dimension", source.target.dimension.str() );
    value.ethereal = true;
    value.set_var( "ethereal", 1 );
    for( item *nested : value.all_items_ptr() ) {
        nested->set_var( "power_grant_application", std::to_string( source.id ) );
        nested->set_var( "power_grant_dimension", source.target.dimension.str() );
        nested->ethereal = true;
        nested->set_var( "ethereal", 1 );
    }
}
} // namespace

struct power_grant_manager::impl {
    std::map<int64_t, grant_record> records;
    bool busy = false;
    int item_dispatch = 0;

    bool live( const grant_record &record ) const {
        return record.view.state == "active" &&
               get_powers().owns_application( record.view.application, record.view.owner );
    }
    void erase_equipment( const grant_record &record ) {
        struct entry {
            item_location location;
            tripoint_abs_ms position;
            creature_ref carrier;
        };
        std::vector<entry> locations;
        std::vector<shared_ptr_fast<Creature>> holders;
        const auto collect = [&]( const item_location & location, const creature_ref & carrier ) {
            if( marked( *location, record.view.application, record.view.target.dimension ) ) {
                locations.push_back( { location, location.pos_abs(), carrier } );
                return VisitResponse::SKIP;
            }
            return VisitResponse::NEXT;
        };
        for( Creature &who : g->all_creatures() ) {
            if( Character *person = who.as_character() ) {
                holders.push_back( g->shared_from( who ) );
                const creature_ref ref = creature_ref::bind( who );
                person->visit_items( [&]( const item_location & location ) {
                    return collect( location, ref );
                } );
            }
        }
        // Discover real locations without mutating iterators. Expiration hooks may
        // subsequently remove a vehicle, relocate the avatar or transfer an item.
        // Read in-memory backing submaps in the loaded bubble, including newly
        // dirtied active caches. Do not load or simulate offscreen submaps.
        for( const auto &cached : MAPBUFFER ) {
            if( !get_map().inbounds( cached.first ) || !cached.second || cached.second->active_items.empty() ) {
                continue;
            }
            const auto origin = project_to<coords::ms>( cached.first );
            for( int x = 0; x < SEEX; ++x ) {
                for( int y = 0; y < SEEY; ++y ) {
                    const tripoint_bub_ms point = get_map().get_bub( origin + tripoint_rel_ms( x, y, 0 ) );
                    map_cursor( point ).visit_items( [&]( const item_location & location ) {
                        return collect( location, {} );
                    } );
                }
            }
        }
        for( wrapped_vehicle const &wrapped : get_map().get_vehicles() ) {
            for( const vpart_reference &part : wrapped.v->get_any_parts( "CARGO" ) ) {
                vehicle_cursor( *wrapped.v, part.part_index() ).visit_items( [&]( const item_location & location ) {
                    return collect( location, {} );
                } );
            }
        }
        for( entry &value : locations ) {
            const auto available = [&]() {
                if( value.carrier.type != creature_ref::kind::none ) {
                    const Creature *who = value.carrier.resolve_loaded();
                    return who && get_map().inbounds( who->pos_bub() );
                }
                return get_map().inbounds( get_map().get_bub( value.position ) );
            };
            if( !available() || !value.location ||
                !marked( *value.location, record.view.application, record.view.target.dimension ) ) {
                continue;
            }
            const tripoint_bub_ms point = value.location.pos_bub( get_map() );
            if( !get_map().inbounds( point ) ) {
                continue;
            }
            expire_power_equipment( *value.location, get_map(), point );
            if( available() && value.location &&
                get_map().inbounds( value.location.pos_bub( get_map() ) ) &&
                marked( *value.location, record.view.application, record.view.target.dimension ) ) {
                value.location.remove_item();
            }
        }
    }
    void cleanup( grant_record &record ) {
        Creature *target = record.view.target.resolve_loaded();
        const auto hold = target ? g->shared_from( *target ) : nullptr;
        if( target ) {
            record.position = target->pos_abs();
            target->remove_effect_grant( record.view.application );
        }
        if( record.view.equipment ) {
            erase_equipment( record );
        }
        // Missing actors cannot be replaced by the current occupant of their tile.
        // Once its saved position is loaded, remaining real gear can be returned
        // to the ground there; otherwise retain a saved cleanup tombstone.
        if( record.garments.empty() && !record.weapon ) {
            record.view.state = "ended";
            return;
        }
        const auto ready = [&]() {
            if( const Creature *current = record.view.target.resolve_loaded() ) {
                record.position = current->pos_abs();
            }
            return record.view.target.dimension == g->get_dimension_prefix() &&
                   get_map().inbounds( get_map().get_bub( record.position ) );
        };
        if( !ready() ) {
            return;
        }
        const auto return_item = [&]( item & value, bool worn ) {
            Creature *current = record.view.target.resolve();
            const auto current_hold = current ? g->shared_from( *current ) : nullptr;
            Character *person = current ? current->as_character() : nullptr;
            bool restored = false;
            try {
                restored = person &&
                           ( worn ? person->wear_power_item( value ) : person->wield_power_item( value ) );
            } catch( ... ) {
                if( value.uid().get_value() == 0 ) {
                    ++record.view.restored;
                }
                throw;
            }
            if( restored ) {
                ++record.view.restored;
            } else {
                const tripoint_bub_ms where = person ? person->pos_bub() : get_map().get_bub( record.position );
                ++record.view.dropped;
                get_map().add_item_or_charges( where, std::move( value ) );
            }
        };
        while( !record.garments.empty() ) {
            if( !ready() ) {
                return;
            }
            item value = std::move( record.garments.front() );
            record.garments.pop_front();
            return_item( value, true );
        }
        if( record.weapon ) {
            if( !ready() ) {
                return;
            }
            item value = std::move( *record.weapon );
            record.weapon.reset();
            return_item( value, false );
        }
        record.view.state = "ended";
    }
    void trim() {
        size_t terminal = std::count_if( records.begin(), records.end(), []( const auto & entry ) {
            return entry.second.view.state == "ended";
        } );
        for( auto it = records.begin(); it != records.end() && terminal > 32; ) {
            if( it->second.view.state == "ended" ) {
                it = records.erase( it );
                --terminal;
            } else {
                ++it;
            }
        }
    }
};

power_grant_manager::power_grant_manager() : data( std::make_unique<impl>() ) {}
power_grant_manager::~power_grant_manager() = default;

std::string power_grant_manager::check( const power_grant_definition &def,
                                        const Creature &target ) const
{
    if( def.empty() ) {
        return {};
    }
    if( !get_map().inbounds( target.pos_bub() ) ) {
        return "target_unavailable";
    }
    if( data->records.size() >= record_limit ) {
        return "grant_budget";
    }
    std::set<std::pair<efftype_id, bodypart_id>> cells;
    for( const power_effect_grant &grant : def.effects ) {
        const std::string reason = target.check_effect_grant( grant.effect, grant.bodypart.id(),
                                   grant.intensity );
        if( !reason.empty() ) {
            return reason;
        }
        const auto bp = grant.effect->get_main_parts() ? grant.bodypart->main_part : grant.bodypart.id();
        if( !cells.emplace( grant.effect, bp ).second ) {
            return "duplicate_effect_grant";
        }
    }
    if( !def.equipment.empty() || def.form ) {
        const Character *person = target.as_character();
        if( !person ) {
            return "equipment_requires_character";
        }
        if( def.form ) {
            for( const auto &entry : data->records ) {
                if( entry.second.view.form && entry.second.view.state != "ended" &&
                    entry.second.view.target.resolve_loaded() == &target ) {
                    return "form_conflict";
                }
            }
            const std::string reason = person->check_power_stow( def.stow_worn, def.stow_weapon );
            if( !reason.empty() ) {
                return reason;
            }
        }
        for( const power_equipment_grant &grant : def.equipment ) {
            if( !grant.item.is_valid() || ( grant.slot == "worn" && !grant.item->armor ) ||
                grant.item->count_by_charges() || grant.item->comestible || grant.item->gun || grant.item->tool ) {
                return "invalid_equipment_grant";
            }
            if( grant.slot == "wielded" && person->get_wielded_item() && !def.stow_weapon ) {
                return "weapon_occupied";
            }
        }
    }
    return {};
}

std::string power_grant_manager::start( const power_application_view &application )
{
    if( processing() ) {
        return "grant_reentrancy";
    }
    const power_grant_definition &def = application.application->grants;
    Creature *target = application.target.resolve();
    const auto hold = target ? g->shared_from( *target ) : nullptr;
    if( !target || def.empty() ) {
        return target ? "" : "target_unavailable";
    }
    const std::string reason = check( def, *target );
    if( !reason.empty() ) {
        return reason;
    }
    grant_record record;
    record.view.application = application.id;
    record.view.definition = application.application;
    record.view.owner = application.owner;
    record.view.target = application.target;
    record.view.state = "active";
    record.view.form = def.form;
    record.position = target->pos_abs();
    record.updated = calendar::turn;
    auto inserted = data->records.emplace( application.id, std::move( record ) );
    if( !inserted.second || data->busy ) {
        return "grant_reentrancy";
    }
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->busy = false;
    } );
    auto &state = inserted.first->second;
    Character *person = target->as_character();
    if( def.form ) {
        person->stow_power_gear( def.stow_worn, def.stow_weapon, state.garments, state.weapon );
    }
    for( const power_effect_grant &grant : def.effects ) {
        if( !data->live( state ) || application.target.resolve() != target ) {
            return "application_ended";
        }
        if( !target->add_effect_grant( application.id, effect_source( application.owner.resolve() ),
                                       grant.effect, grant.bodypart.id(), grant.intensity ) ) {
            return "effect_refused";
        }
        ++state.view.effects;
    }
    for( const power_equipment_grant &grant : def.equipment ) {
        if( !data->live( state ) || application.target.resolve() != target ) {
            return "application_ended";
        }
        item value( grant.item, calendar::turn );
        mark( value, application );
        // Register before native wear/wield callbacks so exception cleanup sees it.
        ++state.view.equipment;
        try {
            if( !( grant.slot == "worn" ? person->wear_power_item( value ) : person->wield_power_item(
                       value ) ) ) {
                --state.view.equipment;
                return "equipment_refused";
            }
        } catch( ... ) {
            if( value.uid().get_value() > 0 ) {
                --state.view.equipment;
            }
            throw;
        }
    }
    return {};
}

void power_grant_manager::release( int64_t application, const std::string &reason )
{
    const auto found = data->records.find( application );
    if( found == data->records.end() ) {
        return;
    }
    if( found->second.view.state == "ended" ) {
        if( found->second.view.reason == "application_ended" ) {
            found->second.view.reason = reason;
        }
        return;
    }
    found->second.view.state = "cleanup";
    found->second.view.reason = reason;
    if( processing() ) {
        return;
    }
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->busy = false;
    } );
    data->cleanup( found->second );
    data->trim();
}

bool power_grant_manager::contains( int64_t application ) const
{
    const auto found = data->records.find( application );
    return found != data->records.end() && found->second.view.state == "active";
}

bool power_grant_manager::item_active( const item &value ) const
{
    if( g->abilities().restoring() ) {
        return true;
    }
    const std::string token = value.get_var( "power_grant_application", "" );
    // Compare exact strings; do not lose 64-bit precision through numeric vars.
    for( const auto &entry : data->records ) {
        if( token == std::to_string( entry.first ) && marked( value, entry.first,
                entry.second.view.target.dimension ) ) {
            return data->live( entry.second );
        }
    }
    return false;
}

void power_grant_manager::retire_actor( const Creature &actor )
{
    for( auto &entry : data->records ) {
        if( entry.second.view.target.type != creature_ref::kind::none &&
            entry.second.view.target == creature_ref::bind( actor ) ) {
            entry.second.position = actor.pos_abs();
        }
    }
}

void power_grant_manager::process()
{
    if( processing() ) {
        return;
    }
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->busy = false;
    } );
    std::vector<int64_t> ids;
    for( const auto &entry : data->records ) {
        ids.push_back( entry.first );
    }
    for( int64_t id : ids ) {
        auto &record = data->records.at( id );
        if( record.view.state == "ended" ) {
            continue;
        }
        if( !data->live( record ) ) {
            record.view.state = "cleanup";
            if( record.view.reason.empty() ) {
                record.view.reason = "application_ended";
            }
            data->cleanup( record );
            continue;
        }
        Creature *who = record.view.target.resolve();
        const auto hold = who ? g->shared_from( *who ) : nullptr;
        if( !who ) {
            continue;
        }
        record.position = who->pos_abs();
        if( record.updated == calendar::turn ) {
            continue;
        }
        record.updated = calendar::turn;
        Character *person = who->as_character();
        for( auto it = record.garments.begin(); it != record.garments.end(); ) {
            if( it->process( get_map(), person, who->pos_bub() ) ) {
                it->spill_contents( &get_map(), who->pos_bub() );
                it = record.garments.erase( it );
            } else {
                ++it;
            }
            if( !data->live( record ) || record.view.target.resolve() != who ) {
                break;
            }
        }
        if( record.weapon && data->live( record ) && record.view.target.resolve() == who &&
            record.weapon->process( get_map(), person, who->pos_bub() ) ) {
            record.weapon->spill_contents( &get_map(), who->pos_bub() );
            record.weapon.reset();
        }
    }
    // Reconcile native layers after load/unload, including orphaned older saves.
    std::vector<std::pair<creature_ref, int64_t>> orphans;
    for( const Creature &who : g->all_creatures() ) {
        std::set<int64_t> tokens;
        for( const effect &value : who.get_effects() ) {
            for( int64_t token : value.owned_tokens() ) {
                tokens.insert( token );
            }
        }
        for( int64_t token : tokens ) {
            const auto found = data->records.find( token );
            if( found == data->records.end() || !data->live( found->second ) ||
                found->second.view.target.resolve_loaded() != &who ) {
                orphans.emplace_back( creature_ref::bind( who ), token );
            }
        }
    }
    for( const auto &orphan : orphans ) {
        Creature *who = orphan.first.resolve_loaded();
        const auto hold = who ? g->shared_from( *who ) : nullptr;
        if( who ) {
            who->remove_effect_grant( orphan.second );
        }
    }
    data->trim();
}

void power_grant_manager::clear()
{
    if( processing() ) {
        throw JsonError( "Cannot replace grants inside native callbacks" );
    }
    data->records.clear();
}

bool power_grant_manager::processing() const
{
    return data->busy || data->item_dispatch > 0;
}

std::vector<power_grant_view> power_grant_manager::inspect() const
{
    std::vector<power_grant_view> out;
    for( const auto &entry : data->records ) {
        power_grant_view view = entry.second.view;
        view.stowed = entry.second.garments.size() + ( entry.second.weapon ? 1 : 0 );
        for( const item &value : entry.second.garments ) {
            view.originals.emplace_back( value.uid().get_value(), value.typeId() );
        }
        if( entry.second.weapon ) {
            view.originals.emplace_back( entry.second.weapon->uid().get_value(),
                                         entry.second.weapon->typeId() );
        }
        out.push_back( std::move( view ) );
    }
    return out;
}

void power_grant_manager::serialize( JsonOut &out ) const
{
    if( processing() ) {
        throw JsonError( "Cannot save grants inside native callbacks" );
    }
    out.start_object();
    out.member( "records" );
    out.start_array();
    for( const auto &entry : data->records ) {
        const auto &r = entry.second;
        if( r.view.state == "ended" ) {
            continue;
        }
        out.start_object();
        out.member( "application", entry.first );
        out.member( "definition", r.view.definition );
        out.member( "owner", r.view.owner );
        out.member( "target", r.view.target );
        out.member( "state", r.view.state );
        out.member( "reason", r.view.reason );
        out.member( "form", r.view.form );
        out.member( "position", r.position );
        out.member( "updated", r.updated );
        out.member( "effects", r.view.effects );
        out.member( "equipment", r.view.equipment );
        out.member( "restored", r.view.restored );
        out.member( "dropped", r.view.dropped );
        out.member( "garments", r.garments );
        if( r.weapon ) {
            out.member( "weapon", *r.weapon );
        }
        out.end_object();
    }
    out.end_array();
    out.end_object();
}

void power_grant_manager::deserialize( const JsonObject &jo )
{
    if( processing() ) {
        jo.throw_error( "Cannot replace grants inside native callbacks" );
    }
    std::map<int64_t, grant_record> records;
    std::set<int64_t> held;
    for( const JsonObject obj : jo.get_array( "records" ) ) {
        grant_record r;
        obj.read( "application", r.view.application, true );
        obj.read( "definition", r.view.definition, true );
        obj.read( "owner", r.view.owner, true );
        obj.read( "target", r.view.target, true );
        obj.read( "state", r.view.state, true );
        obj.read( "reason", r.view.reason, true );
        obj.read( "form", r.view.form, true );
        obj.read( "position", r.position, true );
        obj.read( "updated", r.updated, true );
        obj.read( "effects", r.view.effects, true );
        obj.read( "equipment", r.view.equipment, true );
        obj.read( "restored", r.view.restored, true );
        obj.read( "dropped", r.view.dropped, true );
        obj.read( "garments", r.garments, true );
        if( obj.has_member( "weapon" ) ) {
            r.weapon.emplace();
            obj.read( "weapon", *r.weapon, true );
        }
        if( records.size() >= record_limit || r.view.application <= 0 ||
            r.view.application >= std::numeric_limits<int64_t>::max() ||
            r.view.owner.id <= 0 || r.view.target.id <= 0 ||
            r.view.owner.type == creature_ref::kind::none || r.view.target.type == creature_ref::kind::none ||
            r.view.owner.dimension.is_empty() || r.view.owner.dimension != r.view.target.dimension ||
            ( ( r.view.form || r.view.equipment ) && r.view.target.type != creature_ref::kind::character ) ||
            ( r.view.state != "active" && r.view.state != "cleanup" ) ||
            r.view.effects < 0 || r.view.effects > 16 || r.view.equipment < 0 || r.view.equipment > 8 ||
            r.view.restored < 0 || r.view.restored > 65 || r.view.dropped < 0 || r.view.dropped > 65 ||
            r.view.restored + r.view.dropped + r.garments.size() + ( r.weapon ? 1 : 0 ) > 65 ||
            r.updated > calendar::turn || ( !r.view.form && ( !r.garments.empty() || r.weapon ) ) ||
            r.garments.size() > 64 ) {
            obj.throw_error( "Invalid power grant state" );
        }
        const auto validate = [&]( const item & value ) {
            if( value.is_null() || value.uid().get_value() <= 0 ||
                !held.insert( value.uid().get_value() ).second || value.has_var( "power_grant_application" ) ) {
                obj.throw_error( "Invalid or duplicate stowed item" );
            }
            for( const item *nested : value.all_items_ptr() ) {
                if( nested->uid().get_value() <= 0 || !held.insert( nested->uid().get_value() ).second ||
                    nested->has_var( "power_grant_application" ) ) {
                    obj.throw_error( "Invalid or duplicate stowed contents" );
                }
            }
        };
        for( const item &value : r.garments ) {
            validate( value );
        }
        if( r.weapon ) {
            validate( *r.weapon );
        }
        if( !records.emplace( r.view.application, std::move( r ) ).second ) {
            obj.throw_error( "Duplicate power grant application" );
        }
    }
    data->records = std::move( records );
}

power_grant_manager &get_power_grants()
{
    return get_powers().grants();
}

void expire_power_equipment( item &value, map &here, const tripoint_bub_ms &position )
{
    const auto life = value.get_safe_reference();
    const std::string token = value.get_var( "power_grant_application", "" );
    const std::string dimension = value.get_var( "power_grant_dimension", "" );
    std::list<item> minted;
    int count = std::numeric_limits<int>::max();
    value.remove_internal( [&]( item & nested ) {
        return nested.get_var( "power_grant_application", "" ) == token &&
               nested.get_var( "power_grant_dimension", "" ) == dimension;
    }, count, minted );
    for( item &nested : minted ) {
        expire_power_equipment( nested, here, position );
    }
    if( life ) {
        life->spill_contents( &here, position );
    }
}

power_grant_item_scope::power_grant_item_scope( bool enabled )
{
    if( enabled && g ) {
        manager = &get_power_grants();
        if( manager->data->item_dispatch >= 32 ) {
            throw JsonError( "Projected item dispatch ancestry exceeded" );
        }
        ++manager->data->item_dispatch;
    }
}

power_grant_item_scope::~power_grant_item_scope()
{
    if( manager ) {
        --manager->data->item_dispatch;
    }
}

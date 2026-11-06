#include "power_actor.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

#include "ability_ai.h"
#include "cata_scope_helpers.h"
#include "creature_tracker.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "execution_combat.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "generic_factory.h"
#include "incapacitation.h"
#include "json.h"
#include "map.h"
#include "monster.h"
#include "mtype.h"
#include "power_runtime.h"
#include "talker.h"

namespace
{
generic_factory<power_summon> definitions( "power summon" );
constexpr size_t max_bonds = 128;
constexpr size_t max_history = 32;
struct bond {
    power_summon_view view;
    int revision = 1;
};
int bounded( const JsonObject &jo, const std::string_view key, int fallback, int low, int high )
{
    const int value = jo.get_int( key, fallback );
    if( value < low || value > high ) {
        jo.throw_error_at( key, "integer outside supported bounds" );
    }
    return value;
}
void hooks( const JsonObject &jo, const std::string_view key, const std::string_view source,
            std::vector<effect_on_condition_id> &out )
{
    if( !jo.has_member( key ) ) {
        return;
    }
    out.clear();
    for( const JsonValue &value : jo.get_array( key ) ) {
        if( out.size() >= 16 ) {
            jo.throw_error_at( key, "at most 16 summon hooks" );
        }
        out.push_back( effect_on_conditions::load_inline_eoc( value, source ) );
    }
}
bool valid_order( const std::string &order )
{
    return order == "follow" || order == "guard" || order == "attack";
}
bool available( const Creature *who )
{
    return who && !who->is_fake() && !who->is_hallucination() &&
           get_map().inbounds( who->pos_bub() );
}
} // namespace

template<> const power_summon &power_summon_id::obj() const
{
    return definitions.obj( *this );
}
template<> bool power_summon_id::is_valid() const
{
    return definitions.is_valid( *this );
}
void power_summon::load( const JsonObject &jo, std::string_view source )
{
    mandatory( jo, was_loaded, "name", name );
    mandatory( jo, was_loaded, "monster", monster );
    revision = bounded( jo, "revision", revision, 1, 1000000 );
    duration_ticks = bounded( jo, "duration_ticks", duration_ticks, 1, 1000000 );
    max_per_owner = bounded( jo, "max_per_owner", max_per_owner, 1, 16 );
    range = bounded( jo, "range", range, 1, 30 );
    leash = bounded( jo, "leash", leash, 1, 60 );
    jo.read( "abilities", abilities );
    if( abilities.size() > 16 ) {
        jo.throw_error_at( "abilities", "at most 16 candidate abilities" );
    }
    hooks( jo, "on_spawn", std::string( source ), on_spawn );
    hooks( jo, "on_command", std::string( source ), on_command );
    hooks( jo, "on_end", std::string( source ), on_end );
}
void power_summon::check() const
{
    if( !monster.is_valid() ) {
        debugmsg( "power summon %s has invalid monster %s", id.str(), monster.str() );
    }
    for( const auto &ability : abilities ) {
        if( !ability.is_valid() || ability->ai_intent.empty() ) {
            debugmsg( "power summon %s needs an ability with AI intent: %s", id.str(), ability.str() );
        }
    }
    for( const auto *list : {
             &on_spawn, &on_command, &on_end
         } ) {
        for( const auto &eoc : *list ) {
            if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                debugmsg( "power summon %s needs ACTIVATION hooks", id.str() );
            }
        }
    }
}
void power_summons::load( const JsonObject &jo, const std::string &source )
{
    definitions.load( jo, source );
}
void power_summons::reset()
{
    definitions.reset();
}
void power_summons::finalize()
{
    definitions.finalize();
}
void power_summons::check()
{
    definitions.check();
}

struct power_actor_manager::impl {
    std::map<int64_t, bond> bonds;
    std::vector<power_motion_result> motion_history;
    int64_t next_id = 1;
    bool busy = false;

    const bond *find( const Creature &who ) const {
        for( const auto &entry : bonds ) {
            if( entry.second.view.state != "ended" && entry.second.view.summon.resolve_loaded() == &who ) {
                return &entry.second;
            }
        }
        return nullptr;
    }
    void callbacks( bond &b, const std::vector<effect_on_condition_id> &list ) {
        for( const auto &eoc : list ) {
            Creature *owner = b.view.owner.resolve();
            Creature *summon = b.view.summon.resolve();
            const auto owner_hold = owner ? g->shared_from( *owner ) : nullptr;
            const auto summon_hold = summon ? g->shared_from( *summon ) : nullptr;
            if( !eoc.is_valid() || eoc->type != ACTIVATION ) {
                throw JsonError( "Invalid power summon lifecycle EOC" );
            }
            if( b.view.state != "ended" && ( !owner_hold || !summon_hold ) ) {
                b.view.state = "ending";
                b.view.reason = "participant_unavailable";
                return;
            }
            dialogue d( owner_hold ? get_talker_for( owner_hold.get() ) : nullptr,
                        summon_hold ? get_talker_for( summon_hold.get() ) : nullptr );
            d.set_value( "power_bond", std::to_string( b.view.id ) );
            d.set_value( "power_summon", b.view.definition.str() );
            d.set_value( "power_summon_command", b.view.command );
            d.set_value( "power_summon_reason", b.view.reason );
            d.set_value( "power_owner_present", owner_hold ? 1 : 0 );
            d.set_value( "power_summon_present", summon_hold ? 1 : 0 );
            if( const Creature *target = b.view.target.resolve() ) {
                d.set_value( "power_command_target", target->pos_abs() );
            }
            eoc->activate( d );
            if( b.view.state == "ending" ) {
                return;
            }
        }
    }
    void finish( bond &b ) {
        // An unloaded summon retains a tombstone until it returns. Its native
        // finite lifespan also prevents permanent survivors outside this manager.
        Creature *summon = b.view.summon.resolve_loaded();
        if( !summon && b.view.reason != "summon_dead" ) {
            return;
        }
        const auto hold = summon ? g->shared_from( *summon ) : nullptr;
        b.view.state = "ended";
        const on_out_of_scope cleanup( [&]() {
            if( hold && b.view.summon.resolve_loaded() == hold.get() ) {
                get_powers().retire_actor( *hold );
                if( const monster *mon = hold->as_monster() ) {
                    get_creature_tracker().remove( *mon );
                }
            }
        } );
        if( b.view.definition.is_valid() ) {
            callbacks( b, b.view.definition->on_end );
        }
    }
    void trim() {
        size_t ended = std::count_if( bonds.begin(), bonds.end(), []( const auto & entry ) {
            return entry.second.view.state == "ended";
        } );
        for( auto it = bonds.begin(); it != bonds.end() && ended > max_history; ) {
            if( it->second.view.state == "ended" ) {
                it = bonds.erase( it );
                --ended;
            } else {
                ++it;
            }
        }
    }
};

power_actor_manager::power_actor_manager() : data( std::make_unique<impl>() ) {}
power_actor_manager::~power_actor_manager() = default;

power_summon_admission power_actor_manager::summon( const power_summon_id &id,
        const creature_ref &owner_ref, std::optional<tripoint_abs_ms> point )
{
    if( data->busy ) {
        return { 0, "callback_busy", {} };
    }
    data->trim();
    if( g->abilities().restoring() || g->abilities().stopping() ) {
        return { 0, "world_transition", {} };
    }
    if( !id.is_valid() || !id->monster.is_valid() ) {
        return { 0, "invalid_definition", {} };
    }
    Creature *owner = owner_ref.resolve();
    const auto owner_hold = owner ? g->shared_from( *owner ) : nullptr;
    if( !available( owner ) || !owner_hold ) {
        return { 0, "owner_unavailable", {} };
    }
    if( data->find( *owner ) ) {
        return { 0, "summon_cannot_own", {} };
    }
    const power_summon &def = *id;
    size_t count = 0;
    size_t total = 0;
    for( const auto &entry : data->bonds ) {
        const power_summon_view &v = entry.second.view;
        if( v.state != "ended" ) {
            ++total;
            if( v.owner == owner_ref && v.definition == id ) {
                ++count;
            }
        }
    }
    if( total >= max_bonds || count >= static_cast<size_t>( def.max_per_owner ) ) {
        return { 0, "summon_budget", {} };
    }
    if( data->next_id >= std::numeric_limits<int64_t>::max() - 1 ) {
        return { 0, "id_exhausted", {} };
    }
    map &here = get_map();
    const auto free_tile = [&]( const tripoint_bub_ms & p ) {
        return here.inbounds( p ) && here.passable( p ) && !here.is_open_air( p ) &&
               !here.veh_at( p ) && !get_creature_tracker().creature_at( p );
    };
    if( !point ) {
        for( const auto &p : here.points_in_radius( owner->pos_bub(), 1 ) ) {
            if( free_tile( p ) ) {
                point = here.get_abs( p );
                break;
            }
        }
    }
    if( !point || point->z() != owner->pos_abs().z() ||
        rl_dist( owner->pos_abs(), *point ) > def.range || !free_tile( here.get_bub( *point ) ) ) {
        return { 0, "invalid_spawn_tile", {} };
    }
    monster *mon = g->place_critter_at( def.monster, here.get_bub( *point ) );
    if( !mon ) {
        return { 0, "placement_failed", {} };
    }
    mon->no_extra_death_drops = true;
    mon->no_corpse_quiet = true;
    mon->set_summon_time( time_duration::from_turns( def.duration_ticks ) );
    mon->set_summoner( owner );
    if( const monster *source = owner->as_monster() ) {
        mon->friendly = source->friendly;
        mon->faction = source->faction;
    } else {
        // Bond-aware attitude below resolves the actual owner. Do not make an
        // enemy NPC's familiar universally friendly to the avatar.
        mon->friendly = 0;
    }
    bond b;
    b.revision = def.revision;
    b.view.id = data->next_id++;
    b.view.definition = id;
    b.view.owner = owner_ref;
    b.view.summon = creature_ref::bind( *mon );
    b.view.guard = mon->pos_abs();
    b.view.expires = calendar::turn + time_duration::from_turns( def.duration_ticks );
    const int64_t handle = b.view.id;
    bond &stored = data->bonds.emplace( handle, std::move( b ) ).first->second;
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->busy = false;
    } );
    try {
        data->callbacks( stored, def.on_spawn );
    } catch( ... ) {
        stored.view.reason = "callback_exception";
        stored.view.state = "ending";
        try {
            data->finish( stored );
        } catch( ... ) { }
        throw;
    }
    return { handle, "accepted", stored.view.summon };
}

std::string power_actor_manager::command( int64_t id, const creature_ref &owner_ref,
        const std::string &order, const creature_ref &target_ref, std::optional<tripoint_abs_ms> point )
{
    if( data->busy ) {
        return "callback_busy";
    }
    if( g->abilities().restoring() || g->abilities().stopping() ) {
        return "world_transition";
    }
    const auto it = data->bonds.find( id );
    if( it == data->bonds.end() || it->second.view.state != "active" ) {
        return "not_found";
    }
    bond &b = it->second;
    Creature *owner = b.view.owner.resolve();
    Creature *summon = b.view.summon.resolve();
    if( !( b.view.owner == owner_ref ) ) {
        return "wrong_owner";
    }
    if( !available( owner ) || !available( summon ) ) {
        return "participant_unavailable";
    }
    if( !b.view.definition.is_valid() || b.revision != b.view.definition->revision ||
        calendar::turn >= b.view.expires ) {
        return "expired";
    }
    if( !valid_order( order ) ) {
        return "invalid_command";
    }
    if( order == "attack" ) {
        Creature *target = target_ref.resolve();
        if( !available( target ) || target == owner || target == summon ||
            owner->attitude_to( *target ) != Creature::Attitude::HOSTILE ) {
            return "invalid_target";
        }
        if( rl_dist( owner->pos_abs(), target->pos_abs() ) > b.view.definition->leash ||
            !summon->sees( get_map(), *target ) ) {
            return "target_out_of_reach";
        }
    }
    if( order == "guard" && point &&
        ( !get_map().inbounds( get_map().get_bub( *point ) ) || point->z() != summon->pos_abs().z() ||
          rl_dist( owner->pos_abs(), *point ) > b.view.definition->leash ) ) {
        return "invalid_guard_tile";
    }
    b.view.command = order;
    b.view.target = order == "attack" ? target_ref : creature_ref();
    if( order == "guard" ) {
        b.view.guard = point.value_or( summon->pos_abs() );
    }
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->busy = false;
    } );
    try {
        data->callbacks( b, b.view.definition->on_command );
    } catch( ... ) {
        b.view.state = "ending";
        b.view.reason = "callback_exception";
        try {
            data->finish( b );
        } catch( ... ) { }
        throw;
    }
    return "commanded";
}

bool power_actor_manager::end( int64_t id, const std::string &reason )
{
    auto it = data->bonds.find( id );
    if( it == data->bonds.end() || it->second.view.state != "active" ) {
        return false;
    }
    it->second.view.state = "ending";
    it->second.view.reason = reason;
    return true;
}
int power_actor_manager::dismiss( const creature_ref &owner, const power_summon_id &filter )
{
    int count = 0;
    for( auto &entry : data->bonds ) {
        if( entry.second.view.owner == owner &&
            ( filter.is_empty() || entry.second.view.definition == filter ) ) {
            count += end( entry.first, "dismissed" ) ? 1 : 0;
        }
    }
    return count;
}
void power_actor_manager::process()
{
    if( data->busy || g->abilities().restoring() || g->abilities().processing() ||
        g->executions().processing_actions() || execution_combat::attack_in_progress() ) {
        return;
    }
    data->busy = true;
    const on_out_of_scope reset( [&]() {
        data->busy = false;
    } );
    for( auto &entry : data->bonds ) {
        bond &b = entry.second;
        power_summon_view &v = b.view;
        if( v.state == "active" ) {
            Creature *owner = v.owner.resolve();
            Creature *summon = v.summon.resolve();
            if( !v.definition.is_valid() || b.revision != v.definition->revision ) {
                end( v.id, "incompatible_definition" );
            } else if( calendar::turn >= v.expires ) {
                end( v.id, "expired" );
            } else if( !available( owner ) ) {
                end( v.id, "owner_unavailable" );
            } else if( !available( summon ) ) {
                end( v.id, "summon_unavailable" );
            } else if( rl_dist( owner->pos_abs(), summon->pos_abs() ) > v.definition->leash ) {
                end( v.id, "leash_broken" );
            }
        }
        if( v.state == "ending" ) {
            data->finish( b );
        }
    }
    data->trim();
}
void power_actor_manager::cancel_all( const std::string &reason )
{
    for( auto &entry : data->bonds ) {
        end( entry.first, reason );
    }
    process();
}
void power_actor_manager::clear()
{
    if( data->busy ) {
        cancel_all( "cleared" );
        return;
    }
    data->bonds.clear();
    data->motion_history.clear();
}
bool power_actor_manager::processing() const
{
    return data->busy;
}
void power_actor_manager::retire_actor( const Creature &actor )
{
    const creature_ref ref = creature_ref::bind( actor );
    for( auto &entry : data->bonds ) {
        if( entry.second.view.summon == ref ) {
            if( entry.second.view.state == "ending" ) {
                entry.second.view.reason = "summon_dead";
            }
            end( entry.first, "summon_dead" );
        } else if( entry.second.view.owner == ref ) {
            end( entry.first, "owner_dead" );
        }
    }
}
std::optional<creature_ref> power_actor_manager::owner_of( const Creature &summon ) const
{
    const bond *b = data->find( summon );
    return b ? std::make_optional( b->view.owner ) : std::nullopt;
}

std::optional<Creature::Attitude> power_actor_manager::attitude( const Creature &summon,
        const Creature &other ) const
{
    const bond *b = data->find( summon );
    if( !b ) {
        return std::nullopt;
    }
    const Creature *owner = b->view.owner.resolve();
    if( &summon == &other || owner == &other ) {
        return Creature::Attitude::FRIENDLY;
    }
    if( b->view.state != "active" || !owner || calendar::turn >= b->view.expires ) {
        return Creature::Attitude::NEUTRAL;
    }
    // No nested owners are admitted or loaded, so this cannot recurse through bonds.
    if( const bond *other_bond = data->find( other ) ) {
        if( other_bond->view.owner == b->view.owner ) {
            return Creature::Attitude::FRIENDLY;
        }
        if( const Creature *other_owner = other_bond->view.owner.resolve() ) {
            return owner->attitude_to( *other_owner );
        }
        return Creature::Attitude::NEUTRAL;
    }
    return owner->attitude_to( other );
}

bool power_actor_manager::plan( monster &summon )
{
    const bond *found = data->find( summon );
    if( !found ) {
        return false;
    }
    const power_summon_view &v = found->view;
    Creature *owner = v.owner.resolve();
    if( v.state != "active" || !available( owner ) || !v.definition.is_valid() ||
        found->revision != v.definition->revision ||
        calendar::turn >= v.expires ||
        rl_dist( owner->pos_abs(), summon.pos_abs() ) > v.definition->leash ) {
        summon.unset_dest();
        return true;
    }
    if( v.command == "attack" ) {
        Creature *target = v.target.resolve();
        if( available( target ) && owner->attitude_to( *target ) == Creature::Attitude::HOSTILE &&
            rl_dist( owner->pos_abs(), target->pos_abs() ) <= v.definition->leash &&
            summon.sees( get_map(), *target ) ) {
            summon.set_dest( target->pos_abs() );
            return true;
        }
    }
    const tripoint_abs_ms destination = v.command == "guard" ? v.guard : owner->pos_abs();
    if( rl_dist( summon.pos_abs(), destination ) <= ( v.command == "guard" ? 0 : 1 ) ) {
        summon.unset_dest();
    } else {
        summon.set_dest( destination );
    }
    return true;
}

bool power_actor_manager::take_turn( monster &summon )
{
    const bond *b = data->find( summon );
    if( !b ) {
        return false;
    }
    plan( summon );
    if( b->view.state != "active" || !b->view.owner.resolve() ||
        calendar::turn >= b->view.expires || !b->view.definition.is_valid() ||
        !summon.has_dest() ) {
        summon.set_moves( 0 );
        return true;
    }
    const Creature *target = b->view.target.resolve();
    if( b->view.command == "attack" && target && summon.get_dest() == target->pos_abs() &&
        !incapacitation::unconscious( summon ) ) {
        const auto request = ability_ai::choose( b->view.definition->abilities,
                             b->view.summon, b->view.target );
        if( request && g->abilities().request( *request ).accepted() ) {
            summon.set_moves( 0 );
            return true;
        }
    }
    return false; // Native monster pathfinding and melee spend the ordinary moves.
}

void power_actor_manager::record_motion( const power_motion_result &result )
{
    data->motion_history.push_back( result );
    if( data->motion_history.size() > max_history ) {
        data->motion_history.erase( data->motion_history.begin() );
    }
}
std::vector<power_motion_result> power_actor_manager::motions() const
{
    return data->motion_history;
}
std::vector<power_summon_view> power_actor_manager::inspect() const
{
    std::vector<power_summon_view> out;
    for( const auto &entry : data->bonds ) {
        out.push_back( entry.second.view );
    }
    return out;
}
void power_actor_manager::serialize( JsonOut &out ) const
{
    if( data->busy ) {
        throw JsonError( "Cannot save inside a summon callback" );
    }
    out.start_object();
    out.member( "next_id", data->next_id );
    out.member( "bonds" );
    out.start_array();
    for( const auto &entry : data->bonds ) {
        const bond &b = entry.second;
        const power_summon_view &v = b.view;
        if( v.state == "ended" ) {
            continue;
        }
        out.start_object();
        out.member( "id", v.id );
        out.member( "definition", v.definition );
        out.member( "revision", b.revision );
        out.member( "owner", v.owner );
        out.member( "summon", v.summon );
        out.member( "target", v.target );
        out.member( "command", v.command );
        out.member( "guard", v.guard );
        out.member( "expires", v.expires );
        out.member( "state", v.state );
        out.member( "reason", v.reason );
        out.end_object();
    }
    out.end_array();
    out.end_object();
}
void power_actor_manager::deserialize( const JsonObject &jo )
{
    if( data->busy ) {
        jo.throw_error( "Cannot load inside a summon callback" );
    }
    std::map<int64_t, bond> loaded;
    int64_t next = 1;
    jo.read( "next_id", next, true );
    if( next <= 0 || next >= std::numeric_limits<int64_t>::max() ) {
        jo.throw_error( "Invalid bond counter" );
    }
    std::set<std::pair<std::string, int64_t>> summons;
    std::vector<creature_ref> owners;
    for( const JsonObject obj : jo.get_array( "bonds" ) ) {
        bond b;
        power_summon_view &v = b.view;
        obj.read( "id", v.id, true );
        obj.read( "definition", v.definition, true );
        obj.read( "revision", b.revision, true );
        obj.read( "owner", v.owner, true );
        obj.read( "summon", v.summon, true );
        obj.read( "target", v.target, true );
        obj.read( "command", v.command, true );
        obj.read( "guard", v.guard, true );
        obj.read( "expires", v.expires, true );
        obj.read( "state", v.state, true );
        obj.read( "reason", v.reason, true );
        if( v.id <= 0 || v.id >= next || b.revision <= 0 || b.revision > 1000000 ||
            v.owner.id <= 0 || v.owner.type == creature_ref::kind::none ||
            v.summon.type != creature_ref::kind::monster || v.summon.id <= 0 ||
            v.owner == v.summon || v.owner.dimension != v.summon.dimension ||
            ( v.state != "active" && v.state != "ending" ) || !valid_order( v.command ) ||
            ( v.command == "attack" && ( v.target.type == creature_ref::kind::none || v.target.id <= 0 ||
                                         v.target.dimension != v.owner.dimension ) ) ||
            loaded.size() >= max_bonds || !summons.emplace( v.summon.dimension.str(), v.summon.id ).second ||
            !loaded.emplace( v.id, b ).second ) {
            obj.throw_error( "Invalid or duplicate summon bond" );
        }
        owners.push_back( v.owner );
    }
    for( creature_ref const &owner : owners ) {
        if( owner.type == creature_ref::kind::monster && summons.count( { owner.dimension.str(), owner.id } ) ) {
            jo.throw_error( "A bonded summon cannot own another bond" );
        }
    }
    data->bonds = std::move( loaded );
    data->motion_history.clear();
    data->next_id = std::max( data->next_id, next );
}

power_actor_manager &get_power_actors()
{
    return g->abilities().power_actors();
}

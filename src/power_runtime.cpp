#include "power_runtime.h"
#include "power_actor.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "ability_runtime.h"
#include "avatar.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "creature.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "execution_combat.h"
#include "execution_fatal.h"
#include "execution_runtime.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "incapacitation.h"
#include "json.h"
#include "magic.h"
#include "memory_fast.h"
#include "talker.h"

namespace
{
constexpr int value_limit = 1000000;
constexpr size_t account_limit = 4096;
constexpr size_t active_limit = 128;
constexpr int family_limit = 64;
constexpr int depth_limit = 8;
using account_key = std::tuple<std::string, int, int64_t, std::string>;

account_key key( const creature_ref &actor, const std::string &name, bool avatar = false )
{
    return { avatar ? "" : actor.dimension.str(), static_cast<int>( actor.type ), actor.id, name };
}

bool real_actor( const Creature &actor )
{
    return g && g->shared_from( actor ) && !actor.is_dead_state() && !actor.is_fake() &&
           !actor.is_hallucination();
}

bool valid_ref( const creature_ref &ref )
{
    return ref.id > 0 && ref.type != creature_ref::kind::none && !ref.dimension.is_empty();
}

int64_t elapsed( time_point later, time_point earlier )
{
    return std::max<int64_t>( 0, to_turn<int64_t>( later ) - to_turn<int64_t>( earlier ) );
}

time_point after( time_point from, int64_t ticks )
{
    return time_point::from_turn( static_cast<int>( std::min<int64_t>(
                                      std::numeric_limits<int>::max(), to_turn<int64_t>( from ) + ticks ) ) );
}

int remaining( time_point due )
{
    return static_cast<int>( std::min<int64_t>( value_limit,
                             std::max<int64_t>( 0, to_turn<int64_t>( due ) - to_turn<int64_t>( calendar::turn ) ) ) );
}

Creature *account_actor( const creature_ref &ref, bool avatar )
{
    if( avatar ) {
        return ref.type == creature_ref::kind::character &&
               ref.id == get_avatar().getID().get_value() ? &get_avatar() : nullptr;
    }
    return ref.resolve();
}

struct pool_state {
    creature_ref actor;
    std::string resource;
    int amount = 0;
    time_point updated = calendar::turn_zero;
    bool follows_avatar = false;
};
struct charge_state {
    creature_ref actor;
    power_cooldown_id group;
    int amount = 0;
    time_point ready = calendar::turn_zero;
    time_point next = calendar::before_time_starts;
    bool follows_avatar = false;
};
struct application_state {
    power_application_view view;
    int revision = 1;
    time_point last_tick = calendar::before_time_starts;
    std::string cancel_reason;
    bool live() const {
        return view.state == "pending" || view.state == "active";
    }
};

pool_state project( pool_state p, const power_resource &def )
{
    p.amount = std::clamp( p.amount, 0, def.capacity );
    const int64_t cycles = elapsed( calendar::turn, p.updated ) / def.regen_ticks;
    if( def.regen_amount > 0 && cycles > 0 && p.amount < def.capacity ) {
        const int64_t needed = ( def.capacity - p.amount + def.regen_amount - 1 ) / def.regen_amount;
        p.amount += static_cast<int>( std::min( needed, cycles ) * def.regen_amount );
        p.amount = std::min( p.amount, def.capacity );
    }
    p.updated = p.amount == def.capacity ? calendar::turn : after( p.updated,
                cycles * def.regen_ticks );
    return p;
}

charge_state project( charge_state c, const power_cooldown &def )
{
    c.amount = std::clamp( c.amount, 0, def.max_charges );
    if( c.amount == def.max_charges ) {
        c.next = calendar::before_time_starts;
        return c;
    }
    if( c.amount < def.max_charges && c.next != calendar::before_time_starts &&
        calendar::turn >= c.next ) {
        const int64_t count = std::min<int64_t>( def.max_charges - c.amount,
                              1 + elapsed( calendar::turn, c.next ) / def.recharge_ticks );
        c.amount += static_cast<int>( count );
        c.next = c.amount == def.max_charges ? calendar::before_time_starts :
                 after( c.next, count * def.recharge_ticks );
    }
    return c;
}
} // namespace

int power_balance::available() const
{
    return std::max( 0, total - reserved );
}
bool power_application_admission::accepted() const
{
    return id != 0;
}

struct power_manager::impl {
    std::map<account_key, pool_state> pools;
    std::map<account_key, charge_state> cooldowns;
    std::map<int64_t, power_reservation_view> reservations;
    std::map<int64_t, application_state> applications;
    std::map<int64_t, int> root_requests;
    std::deque<power_change> changes;
    int64_t next_id = 1;
    int64_t current = 0;
    bool pumping = false;
    bool stopping = false;

    void record( const creature_ref &actor, const std::string &kind, const std::string &name,
                 const std::string &cause, int before, int after, int64_t token = 0 ) {
        changes.push_back( { calendar::turn, actor, kind, name, cause, before, after, token } );
        while( changes.size() > 64 ) {
            changes.pop_front();
        }
    }

    std::string invalid( const application_state &i ) const {
        const auto &v = i.view;
        if( !i.cancel_reason.empty() ) {
            return i.cancel_reason;
        }
        if( !v.application.is_valid() || v.application->revision != i.revision ) {
            return "incompatible_definition";
        }
        if( v.state == "active" && v.expires != calendar::before_time_starts &&
            calendar::turn >= v.expires ) {
            return "expired";
        }
        if( elapsed( calendar::turn, i.last_tick ) > 1 || calendar::turn < i.last_tick ) {
            return v.state == "pending" ? "start_delayed" : "time_skipped";
        }
        Creature *owner = v.owner.resolve();
        Creature *target = v.target.resolve();
        if( !owner || !real_actor( *owner ) ) {
            return "owner_unavailable";
        }
        if( !target || !real_actor( *target ) ) {
            return "target_unavailable";
        }
        const auto &def = *v.application;
        if( def.interrupt_incapacitated && ( incapacitation::unconscious( *owner ) ||
                                             owner->has_effect( efftype_id( "stunned" ) ) ) ) {
            return "owner_incapacitated";
        }
        if( def.interrupt_displacement && owner->pos_abs() != v.origin ) {
            return "owner_displaced";
        }
        if( v.parent != 0 ) {
            const auto parent = applications.find( v.parent );
            if( parent == applications.end() || !parent->second.live() ||
                !parent->second.cancel_reason.empty() || !invalid( parent->second ).empty() ) {
                return "parent_ended";
            }
        }
        return {};
    }

    dialogue context( const application_state &i, Creature *owner, Creature *target ) const {
        const auto &v = i.view;
        dialogue d( owner ? get_talker_for( owner ) : nullptr,
                    target ? get_talker_for( target ) : nullptr, {}, v.context );
        d.set_value( "power_application", std::to_string( v.id ) );
        d.set_value( "power_application_definition", v.application.str() );
        d.set_value( "power_application_root", std::to_string( v.root ) );
        d.set_value( "power_application_parent", std::to_string( v.parent ) );
        d.set_value( "power_application_depth", v.depth );
        d.set_value( "power_application_state", v.state );
        d.set_value( "power_application_reason", v.reason );
        d.set_value( "power_application_magnitude", v.magnitude );
        d.set_value( "power_application_ticks", static_cast<double>( v.tick_count ) );
        d.set_value( "power_application_paid_start", static_cast<double>( v.paid_start ) );
        d.set_value( "power_application_paid_upkeep", static_cast<double>( v.paid_upkeep ) );
        d.set_value( "power_owner_present", owner ? 1.0 : 0.0 );
        d.set_value( "power_target_present", target ? 1.0 : 0.0 );
        return d;
    }

    void callbacks( application_state &i, const std::vector<effect_on_condition_id> &list,
                    bool terminal = false ) {
        for( const auto &id : list ) {
            if( !terminal && !invalid( i ).empty() ) {
                return;
            }
            Creature *owner = i.view.owner.resolve_loaded();
            Creature *target = i.view.target.resolve_loaded();
            const auto owner_hold = owner ? g->shared_from( *owner ) : nullptr;
            const auto target_hold = target ? g->shared_from( *target ) : nullptr;
            dialogue d = context( i, owner_hold.get(), target_hold.get() );
            if( !id.is_valid() || id->type != ACTIVATION ) {
                if( !terminal ) {
                    i.cancel_reason = "invalid_eoc";
                }
                continue;
            }
            id->activate( d, true, [&]( const dialogue & out ) {
                if( !i.view.application.is_valid() ) {
                    return;
                }
                for( const auto &local : i.view.application->locals ) {
                    const diag_value *value = out.maybe_get_value( local.first );
                    if( value && value->is_dbl() == local.second.is_dbl() &&
                        value->is_str() == local.second.is_str() &&
                        ( !value->is_dbl() || std::isfinite( value->dbl() ) ) ) {
                        i.view.context[local.first] = *value;
                    } else if( value && !terminal ) {
                        i.cancel_reason = "invalid_local";
                    }
                }
            } );
        }
    }

    void finish( application_state &i, const std::string &reason ) {
        if( !i.live() ) {
            return;
        }
        i.view.state = "ended";
        i.view.reason = reason;
        // Owned children become invisible to queries immediately, even before
        // their own cleanup hooks can be pumped. End hooks cannot recreate them.
        std::set<int64_t> ended{ i.view.id };
        for( auto &entry : applications ) {
            if( entry.second.live() && ended.count( entry.second.view.parent ) ) {
                if( entry.second.cancel_reason.empty() ) {
                    entry.second.cancel_reason = "parent_ended";
                }
                ended.insert( entry.first );
            }
        }
        if( i.view.application.is_valid() ) {
            callbacks( i, i.view.application->on_end, true );
        }
    }

    void failed( application_state &i ) {
        try {
            finish( i, "callback_failed" );
        } catch( ... ) {
            // The original callback exception remains authoritative; cleanup
            // state is already terminal and will never replay on load.
        }
        i.view.state = "failed";
        i.view.reason = "callback_failed";
    }

    void settle_cancelled() {
        for( size_t budget = 0; budget < active_limit; ++budget ) {
            const auto it = std::find_if( applications.begin(), applications.end(), []( const auto & entry ) {
                return entry.second.live() && !entry.second.cancel_reason.empty();
            } );
            if( it == applications.end() ) {
                break;
            }
            current = it->first;
            try {
                finish( it->second, it->second.cancel_reason );
            } catch( ... ) {
                failed( it->second );
                throw;
            }
        }
    }

    void trim() {
        size_t ended = 0;
        for( const auto &entry : applications ) {
            ended += !entry.second.live();
        }
        for( auto it = applications.begin(); it != applications.end() && ended > 32; ) {
            if( !it->second.live() ) {
                it = applications.erase( it );
                --ended;
            } else {
                ++it;
            }
        }
        for( auto it = root_requests.begin(); it != root_requests.end(); ) {
            const bool active = std::any_of( applications.begin(), applications.end(), [&]( const auto & e ) {
                return e.second.live() && e.second.view.root == it->first;
            } );
            if( active ) {
                ++it;
            } else {
                it = root_requests.erase( it );
            }
        }
    }
};

power_manager::power_manager() : data( std::make_unique<impl>() ) {}
power_manager::~power_manager() = default;
power_manager &get_powers()
{
    return g->abilities().powers();
}

power_balance power_manager::balance( const Creature &actor, const std::string &resource,
                                      int64_t except ) const
{
    power_balance out;
    if( !real_actor( actor ) ) {
        out.reason = "participant_unavailable";
        return out;
    }
    const creature_ref ref = creature_ref::bind( actor );
    if( resource == "none" ) {
        return out;
    }
    if( resource == "mana" || resource == "stamina" ) {
        const Character *person = actor.as_character();
        if( !person || ( resource == "stamina" && person->is_npc() ) ) {
            out.reason = "unsupported_resource";
            return out;
        }
        out.total = std::max( 0, resource == "mana" ? person->magic->available_mana() :
                              person->get_stamina() );
        out.capacity = std::max( 0, resource == "mana" ? person->magic->max_mana( *person ) :
                                 person->get_stamina_max() );
    } else {
        const power_resource_id id( resource );
        if( !id.is_valid() || ( !id->actor_kinds.empty() &&
                                !id->actor_kinds.count( powers::actor_kind( actor ) ) ) ) {
            out.reason = "unsupported_resource";
            return out;
        }
        const auto it = data->pools.find( key( ref, resource, actor.is_avatar() ) );
        if( it == data->pools.end() && data->pools.size() >= account_limit ) {
            out.reason = "pool_budget";
            return out;
        }
        out.total = it == data->pools.end() ? id->initial : project( it->second, *id ).amount;
        out.capacity = id->capacity;
    }
    for( const auto &entry : data->reservations ) {
        const auto &r = entry.second;
        if( r.token != except &&
            key( r.actor, r.resource, r.follows_avatar ) == key( ref, resource, actor.is_avatar() ) ) {
            out.reserved += r.amount;
        }
    }
    return out;
}

std::string power_manager::check_cost( const Creature &actor, const std::string &resource,
                                       int amount,
                                       int64_t reservation ) const
{
    if( amount < 0 || amount > value_limit || ( resource == "none" && amount != 0 ) ) {
        return "invalid_cost";
    }
    if( reservation != 0 && ( reservation < 0 || !real_actor( actor ) ||
                              !reservation_matches( reservation, creature_ref::bind( actor ), resource, amount ) ) ) {
        return "reservation_lost";
    }
    if( amount == 0 ) {
        return {};
    }
    const power_balance b = balance( actor, resource, reservation );
    return !b.reason.empty() ? b.reason : b.available() >= amount ? "" : "insufficient_energy";
}

bool power_manager::reserve( const Creature &actor, const std::string &resource, int amount,
                             int64_t token )
{
    if( token <= 0 || amount <= 0 || data->reservations.size() >= 256 ||
        data->reservations.count( token ) || !check_cost( actor, resource, amount ).empty() ) {
        return false;
    }
    const creature_ref ref = creature_ref::bind( actor );
    if( resource != "mana" && resource != "stamina" ) {
        const auto k = key( ref, resource, actor.is_avatar() );
        const power_resource_id def( resource );
        const auto it = data->pools.find( k );
        data->pools[k] = it == data->pools.end() ?
                         pool_state{ ref, resource, def->initial, calendar::turn, actor.is_avatar() } :
                         project( it->second, *def );
    }
    data->reservations.emplace( token, power_reservation_view{ token, ref, resource, amount, actor.is_avatar() } );
    data->record( ref, "reservation", resource, "reserve", 0, amount, token );
    return true;
}

void power_manager::release_reservation( int64_t token )
{
    const auto it = data->reservations.find( token );
    if( it != data->reservations.end() ) {
        const auto &r = it->second;
        data->record( r.actor, "reservation", r.resource, "release", r.amount, 0, token );
        data->reservations.erase( it );
    }
}

bool power_manager::reservation_matches( int64_t token, const creature_ref &actor,
        const std::string &resource, int amount ) const
{
    const auto it = data->reservations.find( token );
    return it != data->reservations.end() && it->second.actor == actor &&
           it->second.resource == resource && it->second.amount == amount;
}

bool power_manager::spend( Creature &actor, const std::string &resource, int amount,
                           const std::string &cause, int64_t reservation )
{
    if( !check_cost( actor, resource, amount, reservation ).empty() ) {
        return false;
    }
    if( amount == 0 ) {
        return true;
    }
    const creature_ref ref = creature_ref::bind( actor );
    const power_balance before = balance( actor, resource, reservation );
    if( resource == "mana" ) {
        actor.as_character()->magic->set_mana( before.total - amount );
    } else if( resource == "stamina" ) {
        actor.as_character()->set_stamina( before.total - amount );
    } else {
        const power_resource_id def( resource );
        const auto k = key( ref, resource, actor.is_avatar() );
        const auto it = data->pools.find( k );
        pool_state p = it == data->pools.end() ? pool_state{ ref, resource, def->initial, calendar::turn, actor.is_avatar() } :
                       project( it->second, *def );
        p.actor = ref;
        p.follows_avatar = actor.is_avatar();
        p.amount -= amount;
        data->pools[k] = p;
    }
    release_reservation( reservation );
    data->record( ref, "resource", resource, cause, before.total, before.total - amount, reservation );
    return true;
}

bool power_manager::commit( Creature &actor, const std::string &resource, int amount,
                            int64_t reservation, const power_cooldown_id &group,
                            int charge_cost, const std::string &cause )
{
    if( !check_cost( actor, resource, amount, reservation ).empty() ||
        !check_charge( actor, group, charge_cost ).empty() ) {
        return false;
    }
    return spend( actor, resource, amount, cause, reservation ) &&
           use_charge( actor, group, charge_cost, cause );
}

int power_manager::modify( Creature &actor, const std::string &resource, int delta,
                           const std::string &cause )
{
    const auto b = balance( actor, resource );
    if( !b.reason.empty() || resource == "none" || delta < -value_limit || delta > value_limit ) {
        return 0;
    }
    // Reservations protect the spendable portion. Capacity reductions never
    // force an explicit debit to discard unrelated existing native resources.
    const int64_t desired = static_cast<int64_t>( b.total ) + delta;
    const int next = delta < 0 ? std::max( b.total - b.available(),
                                           static_cast<int>( std::max<int64_t>( 0, desired ) ) ) :
                     std::max( b.total, static_cast<int>( std::min<int64_t>( b.capacity, desired ) ) );
    const creature_ref ref = creature_ref::bind( actor );
    if( resource == "mana" ) {
        actor.as_character()->magic->set_mana( next );
    } else if( resource == "stamina" ) {
        actor.as_character()->set_stamina( next );
    } else {
        const auto k = key( ref, resource, actor.is_avatar() );
        const power_resource_id def( resource );
        const auto it = data->pools.find( k );
        pool_state p = it == data->pools.end() ? pool_state{ ref, resource, def->initial, calendar::turn, actor.is_avatar() } :
                       project( it->second, *def );
        p.actor = ref;
        p.follows_avatar = actor.is_avatar();
        p.amount = next;
        if( p.amount == def->capacity ) {
            p.updated = calendar::turn;
        }
        data->pools[k] = p;
    }
    data->record( ref, "resource", resource, cause, b.total, next );
    return next - b.total;
}

power_charge_view power_manager::charges( const Creature &actor,
        const power_cooldown_id &group ) const
{
    power_charge_view out;
    out.group = group;
    if( !real_actor( actor ) || !group.is_valid() ) {
        out.reason = !group.is_valid() ? "invalid_cooldown" : "participant_unavailable";
        return out;
    }
    out.actor = creature_ref::bind( actor );
    const auto it = data->cooldowns.find( key( out.actor, group.str(), actor.is_avatar() ) );
    if( it == data->cooldowns.end() && data->cooldowns.size() >= account_limit ) {
        out.reason = "cooldown_budget";
        return out;
    }
    const charge_state c = it == data->cooldowns.end() ?
                           charge_state{ out.actor, group, group->initial_charges, calendar::turn,
                                         group->initial_charges < group->max_charges ?
                                         after( calendar::turn, group->recharge_ticks ) : calendar::before_time_starts, actor.is_avatar() } :
                           project( it->second, *group );
    out.charges = c.amount;
    out.capacity = group->max_charges;
    out.cooldown_remaining = remaining( c.ready );
    out.recharge_remaining = c.next == calendar::before_time_starts ? 0 : remaining( c.next );
    return out;
}

std::string power_manager::check_charge( const Creature &actor, const power_cooldown_id &group,
        int amount ) const
{
    if( group.is_empty() ) {
        return {};
    }
    if( amount < 1 || amount > 1000 ) {
        return "invalid_charge_cost";
    }
    const auto c = charges( actor, group );
    return !c.reason.empty() ? c.reason : c.cooldown_remaining > 0 ? "cooldown" :
           c.charges < amount ? "no_charges" : "";
}

bool power_manager::use_charge( const Creature &actor, const power_cooldown_id &group, int amount,
                                const std::string &cause )
{
    if( group.is_empty() ) {
        return true;
    }
    if( !check_charge( actor, group, amount ).empty() ) {
        return false;
    }
    const creature_ref ref = creature_ref::bind( actor );
    const auto k = key( ref, group.str(), actor.is_avatar() );
    const auto it = data->cooldowns.find( k );
    charge_state c = it == data->cooldowns.end() ? charge_state{ ref, group, group->initial_charges,
                 calendar::turn, calendar::before_time_starts, actor.is_avatar() } :
                 project( it->second, *group );
    c.actor = ref;
    c.follows_avatar = actor.is_avatar();
    const int before = c.amount;
    c.amount -= amount;
    c.ready = after( calendar::turn, group->cooldown_ticks );
    if( c.next == calendar::before_time_starts ) {
        c.next = after( calendar::turn, group->recharge_ticks );
    }
    data->cooldowns[k] = c;
    data->record( ref, "charges", group.str(), cause, before, c.amount );
    return true;
}

int power_manager::recover_charge( const Creature &actor, const power_cooldown_id &group,
                                   int amount )
{
    if( amount < 0 || amount > 1000 || !charges( actor, group ).reason.empty() ) {
        return 0;
    }
    const creature_ref ref = creature_ref::bind( actor );
    const auto k = key( ref, group.str(), actor.is_avatar() );
    const auto it = data->cooldowns.find( k );
    charge_state c = it == data->cooldowns.end() ? charge_state{ ref, group, group->initial_charges,
                 calendar::turn, after( calendar::turn, group->recharge_ticks ), actor.is_avatar() } :
                 project( it->second, *group );
    c.actor = ref;
    c.follows_avatar = actor.is_avatar();
    const int before = c.amount;
    c.amount = std::min( group->max_charges, before + amount );
    if( c.amount == group->max_charges ) {
        c.next = calendar::before_time_starts;
    }
    data->cooldowns[k] = c;
    data->record( ref, "charges", group.str(), "recover", before, c.amount );
    return c.amount - before;
}

std::string power_manager::check_application( const power_application_request &request ) const
{
    if( data->stopping || g->abilities().stopping() || g->abilities().restoring() ) {
        return "world_transition";
    }
    if( !request.application.is_valid() ) {
        return "invalid_application";
    }
    const Creature *owner = request.owner.resolve();
    const Creature *target = request.target.resolve();
    if( !owner || !target || !real_actor( *owner ) || !real_actor( *target ) ) {
        return "participant_unavailable";
    }
    const auto &def = *request.application;
    if( request.context.size() + def.locals.size() > 256 ) {
        return "context_budget";
    }
    if( request.magnitude && ( !std::isfinite( *request.magnitude ) || *request.magnitude < 0 ||
                               *request.magnitude > value_limit ) ) {
        return "invalid_magnitude";
    }
    if( data->next_id >= std::numeric_limits<int64_t>::max() - 1 ) {
        return "id_exhausted";
    }
    size_t active = 0;
    int owned = 0;
    for( const auto &entry : data->applications ) {
        if( entry.second.live() ) {
            ++active;
            owned += entry.second.view.owner == request.owner &&
                     entry.second.view.application == request.application;
        }
    }
    if( active >= active_limit || owned >= def.max_per_owner ) {
        return "instance_budget";
    }
    const auto parent = data->applications.find( data->current );
    if( parent != data->applications.end() ) {
        if( !parent->second.live() || !parent->second.cancel_reason.empty() ||
            !data->invalid( parent->second ).empty() ) {
            return "parent_ended";
        }
        if( parent->second.view.depth >= depth_limit ||
            data->root_requests.at( parent->second.view.root ) >= family_limit ) {
            return "ancestry_budget";
        }
    }
    if( def.interrupt_incapacitated && ( incapacitation::unconscious( *owner ) ||
                                         owner->has_effect( efftype_id( "stunned" ) ) ) ) {
        return "owner_incapacitated";
    }
    const std::string upkeep_support = check_cost( *owner, def.upkeep.resource, def.upkeep.amount );
    if( !upkeep_support.empty() && upkeep_support != "insufficient_energy" ) {
        return upkeep_support;
    }
    return check_cost( *owner, def.start_cost.resource, def.start_cost.amount );
}

power_application_admission power_manager::apply( const power_application_request &request )
{
    const std::string reason = check_application( request );
    if( !reason.empty() ) {
        return { 0, reason };
    }
    application_state i;
    auto &v = i.view;
    v.id = data->next_id++;
    v.application = request.application;
    v.owner = request.owner;
    v.target = request.target;
    v.magnitude = request.magnitude.value_or( request.application->magnitude );
    const auto parent = data->applications.find( data->current );
    if( parent != data->applications.end() ) {
        v.parent = parent->first;
        v.root = parent->second.view.root;
        v.depth = parent->second.view.depth + 1;
    } else {
        v.root = v.id;
    }
    ++data->root_requests[v.root];
    v.state = "pending";
    v.origin = request.owner.resolve()->pos_abs();
    v.started = calendar::turn;
    v.context = request.context;
    for( const auto &local : request.application->locals ) {
        v.context[local.first] = local.second;
    }
    i.revision = request.application->revision;
    i.last_tick = calendar::turn;
    const int64_t id = v.id;
    data->applications.emplace( id, std::move( i ) );
    if( !data->pumping ) {
        data->trim();
    }
    return { id, "accepted" };
}

bool power_manager::end( int64_t id, const std::string &reason )
{
    const auto it = data->applications.find( id );
    if( it == data->applications.end() || !it->second.live() ) {
        return false;
    }
    if( it->second.cancel_reason.empty() ) {
        it->second.cancel_reason = reason.empty() ? "dispelled" : reason;
    }
    // Descendant requests/queries must not observe a still-live owned child.
    std::set<int64_t> ended{ id };
    for( auto &entry : data->applications ) {
        if( entry.second.live() && ended.count( entry.second.view.parent ) ) {
            if( entry.second.cancel_reason.empty() ) {
                entry.second.cancel_reason = "parent_ended";
            }
            ended.insert( entry.first );
        }
    }
    return true;
}

int power_manager::dispel( const creature_ref &target, const power_application_id &application,
                           const std::optional<creature_ref> &owner, const std::string &tag )
{
    int count = 0;
    for( const auto &entry : data->applications ) {
        const auto &i = entry.second;
        if( i.live() && i.cancel_reason.empty() && i.view.target == target &&
            ( application.is_empty() || i.view.application == application ) &&
            ( !owner || i.view.owner == *owner ) &&
            ( tag.empty() || ( i.view.application.is_valid() && i.view.application->tags.count( tag ) ) ) ) {
            count += end( entry.first );
        }
    }
    return count;
}

double power_manager::application_value( const Creature &target,
        const power_application_id &application,
        bool magnitude, const std::optional<creature_ref> &owner, const std::string &tag ) const
{
    if( data->applications.empty() || !real_actor( target ) ) {
        return 0;
    }
    double result = 0;
    const auto ref = creature_ref::bind( target );
    for( const auto &entry : data->applications ) {
        const auto &i = entry.second;
        if( i.view.state == "active" && i.cancel_reason.empty() && i.view.target == ref &&
            i.view.application.is_valid() && i.view.application->revision == i.revision &&
            ( i.view.expires == calendar::before_time_starts || calendar::turn < i.view.expires ) &&
            ( application.is_empty() || i.view.application == application ) &&
            ( !owner || i.view.owner == *owner ) &&
            ( tag.empty() || i.view.application->tags.count( tag ) ) && data->invalid( i ).empty() ) {
            result += magnitude ? i.view.magnitude : 1;
        }
    }
    return result;
}

void power_manager::notify_damage( const Creature &who, int amount )
{
    if( amount <= 0 || data->applications.empty() ) {
        return;
    }
    const auto ref = creature_ref::bind( who );
    for( const auto &entry : data->applications ) {
        if( entry.second.live() && entry.second.view.owner == ref &&
            entry.second.view.application.is_valid() && entry.second.view.application->interrupt_damage ) {
            end( entry.first, "damaged" );
        }
    }
}

void power_manager::retire_actor( const Creature &who )
{
    get_power_actors().retire_actor( who );
    if( !execution_fatal::finalized( who ) ) {
        return;
    }
    const auto ref = creature_ref::bind( who );
    for( auto it = data->pools.begin(); it != data->pools.end(); ) {
        if( key( ref, it->second.resource, who.is_avatar() ) == it->first ) {
            it = data->pools.erase( it );
        } else {
            ++it;
        }
    }
    for( auto it = data->cooldowns.begin(); it != data->cooldowns.end(); ) {
        if( key( ref, it->second.group.str(), who.is_avatar() ) == it->first ) {
            it = data->cooldowns.erase( it );
        } else {
            ++it;
        }
    }
    for( auto it = data->reservations.begin(); it != data->reservations.end(); ) {
        if( key( ref, it->second.resource, who.is_avatar() ) ==
            key( it->second.actor, it->second.resource, it->second.follows_avatar ) ) {
            const int64_t token = it->first;
            ++it;
            release_reservation( token );
        } else {
            ++it;
        }
    }
    // No callbacks during native death cleanup; the next safe pump owns them.
    for( const auto &entry : data->applications ) {
        if( entry.second.live() && entry.second.view.owner == ref ) {
            end( entry.first, "owner_unavailable" );
        } else if( entry.second.live() && entry.second.view.target == ref ) {
            end( entry.first, "target_unavailable" );
        }
    }
}

bool power_manager::processing() const
{
    return data->pumping;
}

void power_manager::drain_starts()
{
    if( data->pumping || g->abilities().processing() || g->abilities().restoring() ||
        g->executions().processing_actions() ||
        execution_combat::attack_in_progress() ) {
        return;
    }
    data->pumping = true;
    const on_out_of_scope restore( [&]() {
        data->pumping = false;
        data->current = 0;
    } );
    std::vector<int64_t> pending;
    for( const auto &entry : data->applications ) {
        if( entry.second.live() && ( entry.second.view.state == "pending" ||
                                     !entry.second.cancel_reason.empty() ) ) {
            pending.push_back( entry.first );
        }
    }
    for( int64_t id : pending ) {
        data->current = id;
        auto &i = data->applications.at( id );
        try {
            const std::string reason = data->invalid( i );
            if( !reason.empty() ) {
                data->finish( i, reason );
            } else if( elapsed( calendar::turn, i.last_tick ) > 1 || calendar::turn < i.last_tick ) {
                data->finish( i, "start_delayed" );
            } else {
                const auto &def = *i.view.application;
                Creature &owner = *i.view.owner.resolve();
                const std::string upkeep_support = check_cost( owner, def.upkeep.resource, def.upkeep.amount );
                const std::string start_cost = check_cost( owner, def.start_cost.resource, def.start_cost.amount );
                if( ( !upkeep_support.empty() && upkeep_support != "insufficient_energy" ) ||
                    !start_cost.empty() ) {
                    data->finish( i, !start_cost.empty() ? start_cost : upkeep_support );
                    continue;
                }
                if( !spend( owner, def.start_cost.resource, def.start_cost.amount, "application_start" ) ) {
                    data->finish( i, "resource_changed" );
                    continue;
                }
                i.view.paid_start = def.start_cost.amount;
                i.view.state = "active";
                i.view.started = calendar::turn;
                i.last_tick = calendar::turn;
                i.view.expires = def.duration_ticks > 0 ? after( calendar::turn, def.duration_ticks ) :
                                 calendar::before_time_starts;
                i.view.next_tick = after( calendar::turn, def.interval_ticks );
                data->callbacks( i, def.on_start );
                const std::string after_start = data->invalid( i );
                if( !after_start.empty() ) {
                    data->finish( i, after_start );
                }
            }
        } catch( ... ) {
            data->failed( i );
            throw;
        }
    }
    data->settle_cancelled();
    data->trim();
}

void power_manager::process()
{
    if( data->pumping || g->abilities().processing() || g->abilities().restoring() ||
        g->executions().processing_actions() ||
        execution_combat::attack_in_progress() ) {
        return;
    }
    drain_starts();
    data->pumping = true;
    const on_out_of_scope restore( [&]() {
        data->pumping = false;
        data->current = 0;
    } );
    std::vector<int64_t> active;
    for( const auto &entry : data->applications ) {
        if( entry.second.view.state == "active" ) {
            active.push_back( entry.first );
        }
    }
    for( int64_t id : active ) {
        data->current = id;
        auto &i = data->applications.at( id );
        try {
            const std::string reason = data->invalid( i );
            if( !reason.empty() ) {
                data->finish( i, reason );
            } else if( i.last_tick == calendar::turn ) {
                continue;
            } else if( elapsed( calendar::turn, i.last_tick ) != 1 || calendar::turn < i.last_tick ) {
                data->finish( i, "time_skipped" );
            } else if( i.view.expires != calendar::before_time_starts && calendar::turn >= i.view.expires ) {
                data->finish( i, "expired" );
            } else {
                i.last_tick = calendar::turn;
                if( calendar::turn >= i.view.next_tick ) {
                    if( i.view.tick_count >= 1000000000 ) {
                        data->finish( i, "tick_budget" );
                        continue;
                    }
                    const auto &def = *i.view.application;
                    Creature &owner = *i.view.owner.resolve();
                    const std::string upkeep_cost = check_cost( owner, def.upkeep.resource, def.upkeep.amount );
                    if( !upkeep_cost.empty() ||
                        !spend( owner, def.upkeep.resource, def.upkeep.amount, "application_upkeep" ) ) {
                        data->finish( i, upkeep_cost == "insufficient_energy" ? "upkeep_unaffordable" :
                                      !upkeep_cost.empty() ? upkeep_cost : "resource_changed" );
                    } else {
                        i.view.paid_upkeep += def.upkeep.amount;
                        ++i.view.tick_count;
                        i.view.next_tick = after( calendar::turn, def.interval_ticks );
                        data->callbacks( i, def.on_tick );
                        const std::string after_tick = data->invalid( i );
                        if( !after_tick.empty() ) {
                            data->finish( i, after_tick );
                        }
                    }
                }
            }
        } catch( ... ) {
            data->failed( i );
            throw;
        }
    }
    data->settle_cancelled();
    data->trim();
}

void power_manager::cancel_all( const std::string &reason )
{
    const bool stopping = data->stopping;
    data->stopping = true;
    const on_out_of_scope restore( [&]() {
        data->stopping = stopping;
    } );
    for( const auto &entry : data->applications ) {
        end( entry.first, reason );
    }
    if( !data->pumping && !g->abilities().processing() ) {
        drain_starts();
    }
}

void power_manager::clear()
{
    if( data->pumping || g->abilities().processing() ) {
        throw JsonError( "Cannot clear power ownership inside a callback" );
    }
    data->applications.clear();
    data->root_requests.clear();
    data->pools.clear();
    data->cooldowns.clear();
    data->reservations.clear();
    data->changes.clear();
}

std::vector<power_application_view> power_manager::inspect_applications() const
{
    std::vector<power_application_view> out;
    for( const auto &entry : data->applications ) {
        auto v = entry.second.view;
        if( v.reason.empty() ) {
            v.reason = entry.second.cancel_reason;
        }
        out.push_back( std::move( v ) );
    }
    return out;
}
std::vector<power_pool_view> power_manager::inspect_pools() const
{
    std::vector<power_pool_view> out;
    for( const auto &entry : data->pools ) {
        const auto &p = entry.second;
        const Creature *actor = account_actor( p.actor, p.follows_avatar );
        if( actor ) {
            out.push_back( { creature_ref::bind( *actor ), p.resource, balance( *actor, p.resource ) } );
        } else {
            out.push_back( { p.actor, p.resource, { "actor_unloaded", p.amount, 0, 0 } } );
        }
    }
    return out;
}
std::vector<power_reservation_view> power_manager::inspect_reservations() const
{
    std::vector<power_reservation_view> out;
    for( const auto &entry : data->reservations ) {
        out.push_back( entry.second );
    }
    return out;
}
std::vector<power_charge_view> power_manager::inspect_cooldowns() const
{
    std::vector<power_charge_view> out;
    for( const auto &entry : data->cooldowns ) {
        const auto &c = entry.second;
        const Creature *actor = account_actor( c.actor, c.follows_avatar );
        out.push_back( actor ? charges( *actor, c.group ) :
                       power_charge_view{ c.actor, c.group, "actor_unloaded", c.amount, 0, 0, 0 } );
    }
    return out;
}
std::vector<power_change> power_manager::history() const
{
    return { data->changes.begin(), data->changes.end() };
}

void power_manager::serialize( JsonOut &out ) const
{
    if( data->pumping || g->abilities().processing() ) {
        throw JsonError( "Cannot save partially applied power ownership" );
    }
    for( const auto &entry : data->applications ) {
        const auto &v = entry.second.view;
        if( entry.second.live() && v.parent &&
            ( !data->applications.count( v.parent ) || !data->applications.at( v.parent ).live() ) ) {
            throw JsonError( "Owned power cleanup must finish before saving" );
        }
    }
    out.start_object();
    out.member( "next_id", data->next_id );
    out.member( "pools" );
    out.start_array();
    for( const auto &entry : data->pools ) {
        const auto &p = entry.second;
        out.start_object();
        out.member( "actor", p.actor );
        out.member( "resource", p.resource );
        out.member( "amount", p.amount );
        out.member( "updated", p.updated );
        out.member( "avatar", p.follows_avatar );
        out.end_object();
    }
    out.end_array();
    out.member( "cooldowns" );
    out.start_array();
    for( const auto &entry : data->cooldowns ) {
        const auto &c = entry.second;
        out.start_object();
        out.member( "actor", c.actor );
        out.member( "group", c.group );
        out.member( "amount", c.amount );
        out.member( "ready", c.ready );
        out.member( "next", c.next );
        out.member( "avatar", c.follows_avatar );
        out.end_object();
    }
    out.end_array();
    out.member( "reservations" );
    out.start_array();
    for( const auto &entry : data->reservations ) {
        const auto &r = entry.second;
        out.start_object();
        out.member( "token", r.token );
        out.member( "actor", r.actor );
        out.member( "resource", r.resource );
        out.member( "amount", r.amount );
        out.member( "avatar", r.follows_avatar );
        out.end_object();
    }
    out.end_array();
    out.member( "roots" );
    out.start_array();
    for( const auto &entry : data->root_requests ) {
        out.start_object();
        out.member( "id", entry.first );
        out.member( "requests", entry.second );
        out.end_object();
    }
    out.end_array();
    out.member( "applications" );
    out.start_array();
    for( const auto &entry : data->applications ) {
        const auto &i = entry.second;
        const auto &v = i.view;
        if( !i.live() ) {
            continue;
        }
        out.start_object();
        out.member( "id", v.id );
        out.member( "root", v.root );
        out.member( "parent", v.parent );
        out.member( "depth", v.depth );
        out.member( "application", v.application );
        out.member( "revision", i.revision );
        out.member( "owner", v.owner );
        out.member( "target", v.target );
        out.member( "state", v.state );
        out.member( "magnitude", v.magnitude );
        out.member( "origin", v.origin );
        out.member( "started", v.started );
        out.member( "expires", v.expires );
        out.member( "next_tick", v.next_tick );
        out.member( "last_tick", i.last_tick );
        out.member( "tick_count", v.tick_count );
        out.member( "paid_start", v.paid_start );
        out.member( "paid_upkeep", v.paid_upkeep );
        out.member( "context", v.context );
        out.member( "cancel_reason", i.cancel_reason );
        out.end_object();
    }
    out.end_array();
    out.end_object();
}

void power_manager::deserialize( const JsonObject &jo )
{
    if( data->pumping || g->abilities().processing() ) {
        jo.throw_error( "Cannot load power ownership inside a callback" );
    }
    auto next = std::make_unique<impl>();
    jo.read( "next_id", next->next_id, true );
    if( next->next_id < 1 || next->next_id >= std::numeric_limits<int64_t>::max() ) {
        jo.throw_error_at( "next_id", "invalid power application counter" );
    }
    next->next_id = std::max( next->next_id, data->next_id );
    for( const JsonObject saved : jo.get_array( "pools" ) ) {
        pool_state p;
        saved.read( "actor", p.actor, true );
        saved.read( "resource", p.resource, true );
        saved.read( "amount", p.amount, true );
        saved.read( "updated", p.updated, true );
        saved.read( "avatar", p.follows_avatar, true );
        if( !valid_ref( p.actor ) || ( p.follows_avatar &&
                                       p.actor.type != creature_ref::kind::character ) || p.resource.empty() || p.resource == "none" ||
            p.resource == "mana" || p.resource == "stamina" || p.amount < 0 || p.amount > value_limit ||
            p.updated > calendar::turn || next->pools.size() >= account_limit ||
            !next->pools.emplace( key( p.actor, p.resource, p.follows_avatar ), p ).second ) {
            saved.throw_error( "invalid or duplicate power pool" );
        }
    }
    for( const JsonObject saved : jo.get_array( "cooldowns" ) ) {
        charge_state c;
        saved.read( "actor", c.actor, true );
        saved.read( "group", c.group, true );
        saved.read( "amount", c.amount, true );
        saved.read( "ready", c.ready, true );
        saved.read( "next", c.next, true );
        saved.read( "avatar", c.follows_avatar, true );
        if( !valid_ref( c.actor ) || ( c.follows_avatar &&
                                       c.actor.type != creature_ref::kind::character ) || c.group.is_empty() || c.amount < 0 ||
            c.amount > 1000 ||
            elapsed( c.ready, calendar::turn ) > value_limit ||
            ( c.next != calendar::before_time_starts && elapsed( c.next, calendar::turn ) > value_limit ) ||
            next->cooldowns.size() >= account_limit ||
            !next->cooldowns.emplace( key( c.actor, c.group.str(), c.follows_avatar ), c ).second ) {
            saved.throw_error( "invalid or duplicate power cooldown" );
        }
    }
    for( const JsonObject saved : jo.get_array( "reservations" ) ) {
        power_reservation_view r;
        saved.read( "token", r.token, true );
        saved.read( "actor", r.actor, true );
        saved.read( "resource", r.resource, true );
        saved.read( "amount", r.amount, true );
        saved.read( "avatar", r.follows_avatar, true );
        if( r.token < 1 || ( r.follows_avatar && r.actor.type != creature_ref::kind::character ) ||
            !valid_ref( r.actor ) || r.resource.empty() || r.resource == "none" ||
            r.amount < 1 || r.amount > value_limit || next->reservations.size() >= 256 ||
            !next->reservations.emplace( r.token, r ).second ) {
            saved.throw_error( "invalid or duplicate power reservation" );
        }
    }
    for( const JsonObject saved : jo.get_array( "roots" ) ) {
        int64_t id = 0;
        saved.read( "id", id, true );
        const int count = saved.get_int( "requests" );
        if( id < 1 || id >= next->next_id || count < 1 || count > family_limit ||
            next->root_requests.size() >= active_limit || !next->root_requests.emplace( id, count ).second ) {
            saved.throw_error( "invalid application family budget" );
        }
    }
    for( const JsonObject saved : jo.get_array( "applications" ) ) {
        application_state i;
        auto &v = i.view;
        saved.read( "id", v.id, true );
        saved.read( "root", v.root, true );
        saved.read( "parent", v.parent, true );
        saved.read( "depth", v.depth, true );
        saved.read( "application", v.application, true );
        saved.read( "revision", i.revision, true );
        saved.read( "owner", v.owner, true );
        saved.read( "target", v.target, true );
        saved.read( "state", v.state, true );
        saved.read( "magnitude", v.magnitude, true );
        saved.read( "origin", v.origin, true );
        saved.read( "started", v.started, true );
        saved.read( "expires", v.expires, true );
        saved.read( "next_tick", v.next_tick, true );
        saved.read( "last_tick", i.last_tick, true );
        saved.read( "tick_count", v.tick_count, true );
        saved.read( "paid_start", v.paid_start, true );
        saved.read( "paid_upkeep", v.paid_upkeep, true );
        saved.read( "context", v.context, true );
        saved.read( "cancel_reason", i.cancel_reason );
        if( v.id < 1 || v.id >= next->next_id || v.root < 1 || v.root > v.id ||
            v.parent < 0 || v.parent >= v.id || v.depth < 0 || v.depth > depth_limit ||
            ( ( v.parent == 0 ) != ( v.depth == 0 ) ) || ( v.depth == 0 && v.root != v.id ) ||
            ( v.parent != 0 && v.root > v.parent ) || !next->root_requests.count( v.root ) ||
            next->root_requests.at( v.root ) <= v.depth || !valid_ref( v.owner ) || !valid_ref( v.target ) ||
            v.application.is_empty() || i.revision < 1 || i.revision > value_limit ||
            ( v.state != "pending" && v.state != "active" ) || !std::isfinite( v.magnitude ) ||
            v.magnitude < 0 || v.magnitude > value_limit || i.last_tick > calendar::turn ||
            i.last_tick < v.started || v.tick_count < 0 || v.tick_count > 1000000000 ||
            v.paid_start < 0 || v.paid_start > value_limit || v.paid_upkeep < 0 ||
            v.paid_upkeep > 1000000000000000LL || v.context.size() > 256 ||
            ( v.state == "active" && ( v.next_tick <= i.last_tick ||
                                       elapsed( v.next_tick, i.last_tick ) > 3600 ||
                                       ( v.expires != calendar::before_time_starts && v.expires <= v.started ) ) ) ||
            next->applications.size() >= active_limit ||
            !next->applications.emplace( v.id, std::move( i ) ).second ) {
            saved.throw_error( "invalid or duplicate saved power application" );
        }
    }
    for( const auto &entry : next->applications ) {
        const auto &v = entry.second.view;
        if( v.parent && ( !next->applications.count( v.parent ) ||
                          next->applications.at( v.parent ).view.root != v.root ||
                          next->applications.at( v.parent ).view.depth != v.depth - 1 ) ) {
            jo.throw_error( "saved application has an invalid parent" );
        }
    }
    data = std::move( next );
}

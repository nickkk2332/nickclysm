#include "npc_attack.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "cata_utility.h"
#include "character.h"
#include "creature.h"
#include "creature_tracker.h"
#include "damage.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "explosion.h"
#include "flag.h"
#include "game.h"
#include "gun_mode.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "magic.h"
#include "magic_spell_effect_helpers.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "memory_fast.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "pimpl.h"
#include "point.h"
#include "projectile.h"
#include "ranged.h"
#include "ret_val.h"
#include "rng.h"
#include "talker.h"
#include "temp_crafting_inventory.h"
#include "value_ptr.h"

static const bionic_id bio_hydraulics( "bio_hydraulics" );

static const json_character_flag json_flag_CANNOT_MOVE( "CANNOT_MOVE" );
static const json_character_flag json_flag_SUBTLE_SPELL( "SUBTLE_SPELL" );

namespace npc_attack_constants
{
// if you are attacking your target, multiply potential by this number
static const float target_modifier = 1.5f;
// if you kill the creature, multiply the potential by this number
static const float kill_modifier = 1.5f;
// the amount of penalty if the npc has to change what it's wielding
// update this number and comment when that is no longer a flat -15 moves
static const int base_time_penalty = 3;
// we want this out of our hands, pronto.
// give a large buff to the attack value so it prioritizes this
static const int base_throw_now = 10000;
} // namespace npc_attack_constants

// TODO: make a better, more generic "check if this projectile is blocked" function
// TODO: put this in a namespace for reuse
static bool has_obstruction( const tripoint_bub_ms &from, const tripoint_bub_ms &to,
                             bool check_ally = false )
{
    std::vector<tripoint_bub_ms> line = line_to( from, to );
    // @to is what we want to hit. we don't need to check for obstruction there.
    line.pop_back();
    const map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &line_point : line ) {
        if( here.impassable( line_point ) || ( check_ally && creatures.creature_at( line_point ) ) ) {
            return true;
        }
    }
    return false;
}

static bool can_move( const npc &source )
{
    return !source.in_vehicle && source.rules.engagement != combat_engagement::NO_MOVE;
}

static bool can_move_melee( const npc &source )
{
    return can_move( source ) && source.rules.engagement != combat_engagement::FREE_FIRE &&
           source.rules.engagement != combat_engagement::NO_MOVE && !source.has_flag( json_flag_CANNOT_MOVE );
}

// gun_range() and gun_damage() only count loaded ammo. An empty wielded gun can
// still be rated (npc_attack_gun::use() reloads it first), so rate it by its default ammo.
static itype_id unloaded_gun_ammo_estimate( const item &gun )
{
    if( gun.has_ammo() ) {
        return itype_id::NULL_ID();
    }
    const itype_id ammo = gun.ammo_default();
    if( ammo.is_null() || !ammo->ammo ) {
        return itype_id::NULL_ID();
    }
    return ammo;
}

static int gun_range_estimate( const item &gun )
{
    const itype_id ammo = unloaded_gun_ammo_estimate( gun );
    if( ammo.is_null() || !gun.is_gun() ) {
        return gun.gun_range();
    }
    // Same as item::gun_range( true ), with the default ammo in place of the loaded ammo.
    int range = gun.type->gun->range + ammo->ammo->range;
    float range_multiplier = ammo->ammo->range_multiplier;
    for( const item *mod : gun.gunmods() ) {
        range += mod->type->gunmod->range;
        range_multiplier *= mod->type->gunmod->range_multiplier;
    }
    return clamp( static_cast<int>( range * range_multiplier ), 0, RANGE_HARD_CAP );
}

// npc_attack_gun keeps the evaluated gun_mode but not its id, so look the id up.
static void set_gun_mode( item &gun, const gun_mode &mode )
{
    for( const std::pair<const gun_mode_id, gun_mode> &m : gun.gun_all_modes() ) {
        if( m.second.target == mode.target && m.second.qty == mode.qty &&
            m.second.flags == mode.flags && m.second.tname() == mode.tname() ) {
            gun.gun_set_mode( m.first );
            return;
        }
    }
}

// Every tile of a points_in_radius() range where creature_tracker::creature_at() could find a
// creature, in the order the range visits its tiles. Looping over these instead of the whole
// range finds the same creatures in the same order, without asking about every empty tile.
static std::vector<tripoint_bub_ms> creature_tiles_in( const tripoint_range<tripoint_bub_ms>
        &range )
{
    const tripoint_bub_ms &lo = range.min();
    const tripoint_bub_ms &hi = range.max();
    std::vector<tripoint_bub_ms> tiles;
    const auto add = [&]( const tripoint_bub_ms & p ) {
        if( p.x() >= lo.x() && p.x() <= hi.x() && p.y() >= lo.y() && p.y() <= hi.y() &&
            p.z() >= lo.z() && p.z() <= hi.z() ) {
            tiles.push_back( p );
        }
    };
    // creature_at() finds monsters, the avatar and the active NPCs
    for( const shared_ptr_fast<monster> &mon : get_creature_tracker().get_monsters_list() ) {
        add( mon->pos_bub() );
    }
    for( const npc &guy : g->all_npcs() ) {
        add( guy.pos_bub() );
    }
    add( get_avatar().pos_bub() );
    // a range visits x fastest, then y, then z
    std::sort( tiles.begin(), tiles.end(), []( const tripoint_bub_ms & a, const tripoint_bub_ms & b ) {
        return std::make_tuple( a.z(), a.y(), a.x() ) < std::make_tuple( b.z(), b.y(), b.x() );
    } );
    tiles.erase( std::unique( tiles.begin(), tiles.end() ), tiles.end() );
    return tiles;
}

// npc_attack_spell::evaluate() over the same tiles in the same order, with the same ratings
// and the same random numbers drawn, but each map and creature question asked once, and a
// tile whose area can't hold a creature rated without working out its area. Returns nothing
// when only the full scan gives the same answer: the spell's area, range or field value draw
// random numbers, or its targets have a condition.
static std::optional<npc_attack_rating> rate_spell_quickly( const spell &attack_spell,
        const npc &source, const Creature *target, const int time_penalty )
{
    spell_area_calculator areas( attack_spell, source );
    if( !areas.usable() ) {
        return std::nullopt;
    }
    const spell_type &type = attack_spell.id().obj();
    cata_default_random_engine &engine = rng_get_engine();

    // what each empty tile of the area is worth when the spell leaves a field
    const bool has_field = type.field.has_value();
    double field_value = 0.0;
    if( has_field ) {
        // NOLINTNEXTLINE(cata-determinism)
        const cata_default_random_engine before = engine;
        const_dialogue d( get_const_talker_for( source ), nullptr );
        field_value = static_cast<double>( attack_spell.field_intensity( source ) ) /
                      static_cast<double>( type.field_chance.evaluate( d ) ) / 2.0;
        if( engine != before ) {
            engine = before;
            return std::nullopt;
        }
    }
    // spell::dps() ignores its target: unless it draws random numbers (random damage), it is
    // the same for every creature
    std::optional<float> fixed_dps;
    {
        // NOLINTNEXTLINE(cata-determinism)
        const cata_default_random_engine before = engine;
        const float dps = attack_spell.dps( source, source );
        if( engine == before ) {
            fixed_dps = dps;
        } else {
            engine = before;
        }
    }

    const map &here = get_map();
    const tripoint_bub_ms char_pos = source.pos_bub();
    const tripoint_range<tripoint_bub_ms> in_range = here.points_in_radius( char_pos,
            areas.range() );
    // spell::targetable_locations(). Its checks have no side effects, so the cheapest goes first.
    const bool select_ground = attack_spell.is_valid_target( spell_target::ground );
    const bool ignore_walls = attack_spell.has_flag( spell_flag::NO_PROJECTILE );
    const auto targetable = [&]( const tripoint_bub_ms & query ) {
        return areas.valid_target( query ) &&
               ( ignore_walls || areas.passable_line( char_pos, query ) ) &&
               ( select_ground || source.sees( here, query ) );
    };

    struct critter_view {
        Creature::Attitude att;
        bool seen;
    };
    std::unordered_map<const Creature *, critter_view> critters;
    // npc_attack_spell::evaluate_tripoint()
    const auto rate_tile = [&]( const tripoint_bub_ms & location ) {
        double total_potential = 0;
        for( const tripoint_bub_ms &potential_target : areas.area( location ) ) {
            Creature *critter = areas.creature_at( potential_target );
            if( !critter ) {
                // no critter? no damage! however, we assume fields are worth something
                if( has_field ) {
                    total_potential += field_value;
                }
                continue;
            }
            auto known = critters.find( critter );
            if( known == critters.end() ) {
                known = critters.emplace( critter, critter_view{
                    source.attitude_to( *critter ), source.sees( here, *critter ) } ).first;
            }
            const Creature::Attitude att = known->second.att;
            int damage = 0;
            if( known->second.seen ) {
                damage = fixed_dps ? *fixed_dps : attack_spell.dps( source, *critter );
            }
            const int distance_to_me = rl_dist( char_pos, potential_target );
            const bool friendly_fire = att == Creature::Attitude::FRIENDLY &&
                                       !source.rules.has_flag( ally_rule::avoid_friendly_fire );
            int attitude_mult = 3;
            if( att == Creature::Attitude::FRIENDLY ) {
                attitude_mult = -10;
            } else if( att == Creature::Attitude::NEUTRAL || friendly_fire ) {
                attitude_mult = -1;
            }
            int potential = damage * attitude_mult - distance_to_me + 1;
            if( target && critter == target ) {
                potential *= npc_attack_constants::target_modifier;
            }
            if( damage >= critter->get_hp() ) {
                potential *= npc_attack_constants::kill_modifier;
                if( att == Creature::Attitude::NEUTRAL ) {
                    potential = std::abs( potential );
                }
                const Creature *source_ptr = &source;
                if( att == Creature::Attitude::FRIENDLY || critter == source_ptr ) {
                    return npc_attack_rating( std::nullopt, location );
                }
            }
            total_potential += potential;
        }
        return npc_attack_rating( static_cast<int>( std::round( total_potential ) ), location );
    };

    // Without a field, a tile whose area holds no creature rates 0 and draws no random
    // numbers. A blast's area (or a one-tile area) only reaches aoe tiles from where it
    // lands, which is the targeted tile itself for any tile the caster can target.
    const bool one_tile = areas.aoe() < 1 && attack_spell.shape() != spell_shape::line;
    const bool skip_empty = field_value == 0.0 &&
                            ( one_tile || attack_spell.shape() == spell_shape::blast );
    const int reach = one_tile ? 0 : std::max( areas.aoe(), 0 );
    const tripoint_bub_ms &lo = in_range.min();
    const int width = in_range.max().x() - lo.x() + 1;
    const int height = in_range.max().y() - lo.y() + 1;
    std::vector<uint8_t> near_creature;
    if( skip_empty && width > 0 && height > 0 ) {
        near_creature.assign( static_cast<size_t>( width ) * height, 0 );
        const tripoint_range<tripoint_bub_ms> wider( in_range.min() - tripoint_rel_ms( reach, reach, 0 ),
                in_range.max() + tripoint_rel_ms( reach, reach, 0 ) );
        for( const tripoint_bub_ms &c : creature_tiles_in( wider ) ) {
            for( int x = std::max( c.x() - reach, lo.x() ); x <= std::min( c.x() + reach, lo.x() + width - 1 );
                 ++x ) {
                for( int y = std::max( c.y() - reach, lo.y() );
                     y <= std::min( c.y() + reach, lo.y() + height - 1 ); ++y ) {
                    near_creature[static_cast<size_t>( y - lo.y() ) * width + ( x - lo.x() )] = 1;
                }
            }
        }
    }

    npc_attack_rating effectiveness;
    bool rated_empty_area = false;
    for( const tripoint_bub_ms &query : in_range ) {
        if( skip_empty &&
            !near_creature[static_cast<size_t>( query.y() - lo.y() ) * width + ( query.x() - lo.x() )] ) {
            // Every such tile rates 0 less the time penalty, so after the first one the
            // others can't be strictly better.
            if( rated_empty_area || !targetable( query ) ) {
                continue;
            }
            rated_empty_area = true;
            npc_attack_rating effectiveness_at_point( 0, query );
            effectiveness_at_point -= time_penalty;
            if( effectiveness_at_point > effectiveness ) {
                effectiveness = effectiveness_at_point;
            }
            continue;
        }
        if( !targetable( query ) ) {
            continue;
        }
        npc_attack_rating effectiveness_at_point = rate_tile( query );
        effectiveness_at_point -= time_penalty;
        if( effectiveness_at_point > effectiveness ) {
            effectiveness = effectiveness_at_point;
        }
    }
    return effectiveness;
}

bool npc_attack_rating::operator>( const npc_attack_rating &rhs ) const
{
    if( !rhs._value ) {
        return true;
    }
    if( !_value ) {
        return false;
    }
    return *_value > *rhs._value;
}

bool npc_attack_rating::operator>( const int rhs ) const
{
    if( !_value ) {
        return false;
    }
    return *_value > rhs;
}

bool npc_attack_rating::operator<( const int rhs ) const
{
    if( !_value ) {
        return true;
    }
    return *_value < rhs;
}

npc_attack_rating npc_attack_rating::operator-=( const int rhs )
{
    if( _value ) {
        *_value -= rhs;
    }
    return *this;
}

// NPC-J06: whether a spell's "npc_use" condition holds, u being the caster and npc the creature
// (its target, the one it is cast on), if any
static bool npc_use_condition_holds( const spell_type &type, const npc &source,
                                     const Creature *other )
{
    if( !type.has_npc_condition ) {
        return true;
    }
    if( other == nullptr ) {
        const_dialogue d( get_const_talker_for( source ), nullptr );
        return type.npc_condition( d );
    }
    const_dialogue d( get_const_talker_for( source ), get_const_talker_for( *other ) );
    return type.npc_condition( d );
}

// NPC-J06: where the caster casts a self or ally spell to reach this creature, if it can: on
// the creature, or for the caster itself and a spell cast on the ground (a summons), on the
// first free tile next to it
static std::optional<tripoint_bub_ms> support_spell_target( const spell &sp, const npc &source,
        const Creature &patient )
{
    const map &here = get_map();
    const tripoint_bub_ms at = patient.pos_bub();
    if( &patient != &source ) {
        if( !sp.is_target_in_range( source, at ) || !source.sees( here, patient ) ||
            ( !sp.has_flag( spell_flag::NO_PROJECTILE ) && has_obstruction( source.pos_bub(), at ) ) ||
            !sp.is_valid_target( source, at ) ) {
            return std::nullopt;
        }
        return at;
    }
    if( sp.is_valid_target( source, at ) ) {
        return at;
    }
    if( sp.is_valid_target( spell_target::ground ) ) {
        creature_tracker &creatures = get_creature_tracker();
        for( const tripoint_bub_ms &p : here.points_in_radius( at, 1 ) ) {
            if( p != at && sp.is_target_in_range( source, p ) && here.passable( p ) &&
                creatures.creature_at( p ) == nullptr && sp.is_valid_target( source, p ) ) {
                return p;
            }
        }
    }
    return std::nullopt;
}

// NPC-J06: a self or ally spell is worth its "npc_use" priority, less the time casting it takes,
// cast on the first creature it suits: the caster, then (ally) the friends it sees
static npc_attack_rating rate_support_spell( const spell &sp, const npc &source,
        const int time_penalty )
{
    const spell_type &type = sp.id().obj();
    std::vector<shared_ptr_fast<Creature>> friends;
    if( type.npc_role == spell_npc_role::ally ) {
        for( const weak_ptr_fast<Creature> &buddy : source.get_cached_friends() ) {
            if( shared_ptr_fast<Creature> guy = buddy.lock() ) {
                friends.push_back( guy );
            }
        }
    }
    std::vector<const Creature *> patients{ &source };
    for( const shared_ptr_fast<Creature> &guy : friends ) {
        if( std::find( patients.begin(), patients.end(), guy.get() ) == patients.end() ) {
            patients.push_back( guy.get() );
        }
    }
    npc_attack_rating best;
    for( const Creature *patient : patients ) {
        const std::optional<tripoint_bub_ms> at = support_spell_target( sp, source, *patient );
        if( !at || !npc_use_condition_holds( type, source, patient ) ) {
            continue;
        }
        const_dialogue d( get_const_talker_for( source ), get_const_talker_for( *patient ) );
        npc_attack_rating rating( static_cast<int>( std::round( type.npc_priority.evaluate( d ) ) ),
                                  *at );
        rating -= time_penalty;
        if( rating > best ) {
            best = rating;
        }
    }
    return best;
}

void npc_attack_spell::use( npc &source, const tripoint_bub_ms &location ) const
{
    spell &sp = source.magic->get_spell( attack_spell_id );
    if( source.has_weapon() && !source.get_wielded_item()->has_flag( flag_MAGIC_FOCUS ) &&
        !sp.has_flag( spell_flag::NO_HANDS ) && !source.has_flag( json_flag_SUBTLE_SPELL ) ) {
        source.unwield();
    }
    add_msg_debug( debugmode::debug_filter::DF_NPC, "%s is casting %s", source.disp_name(), sp.name() );
    source.cast_spell( sp, false, location );
}

npc_attack_rating npc_attack_spell::evaluate( const npc &source,
        const Creature *target ) const
{
    const spell &attack_spell = source.magic->get_spell( attack_spell_id );
    npc_attack_rating effectiveness;
    if( !can_use( source ) ) {
        return effectiveness;
    }
    const int time_penalty = base_time_penalty( source );
    const spell_type &type = attack_spell.id().obj();
    if( type.npc_role == spell_npc_role::self || type.npc_role == spell_npc_role::ally ) {
        return rate_support_spell( attack_spell, source, time_penalty );
    }
    if( !npc_use_condition_holds( type, source, target ) ) {
        return effectiveness;
    }
    if( std::optional<npc_attack_rating> quick = rate_spell_quickly( attack_spell, source, target,
            time_penalty ) ) {
        return *quick;
    }
    const std::vector<tripoint_bub_ms> targetable_points = attack_spell.targetable_locations( source );
    for( const tripoint_bub_ms &targetable_point : targetable_points ) {
        npc_attack_rating effectiveness_at_point = evaluate_tripoint( source, target, targetable_point );
        effectiveness_at_point -= time_penalty;
        if( effectiveness_at_point > effectiveness ) {
            effectiveness = effectiveness_at_point;
        }
    }
    return effectiveness;
}

std::vector<npc_attack_rating> npc_attack_spell::all_evaluations( const npc &source,
        const Creature *target ) const
{
    const spell &attack_spell = source.magic->get_spell( attack_spell_id );
    std::vector<npc_attack_rating> effectiveness;
    if( !can_use( source ) ) {
        return effectiveness;
    }
    int time_penalty = this->base_time_penalty( source );
    const spell_type &type = attack_spell.id().obj();
    if( type.npc_role == spell_npc_role::self || type.npc_role == spell_npc_role::ally ) {
        const npc_attack_rating support = rate_support_spell( attack_spell, source, time_penalty );
        if( support.value() ) {
            effectiveness.push_back( support );
        }
        return effectiveness;
    }
    if( !npc_use_condition_holds( type, source, target ) ) {
        return effectiveness;
    }
    const std::vector<tripoint_bub_ms> targetable_points = attack_spell.targetable_locations( source );
    for( const tripoint_bub_ms &targetable_point : targetable_points ) {
        npc_attack_rating effectiveness_at_point = evaluate_tripoint( source, target, targetable_point );
        effectiveness_at_point -= time_penalty;
        effectiveness.push_back( effectiveness_at_point );
    }
    return effectiveness;
}

bool npc_attack_spell::can_use( const npc &source ) const
{
    const spell &attack_spell = source.magic->get_spell( attack_spell_id );
    // NPC-J06: an "npc_use" of "never" keeps NPCs from casting it
    if( attack_spell_id->npc_role == spell_npc_role::never ) {
        return false;
    }
    // missing components or energy or something
    return attack_spell.can_cast( source ) &&
           // use the same rules as silent guns: every NPC has the flag, but only followers obey it
           !( source.is_player_ally() && source.rules.has_flag( ally_rule::use_silent ) &&
              attack_spell.sound_volume( source ) >= 5 );
}

int npc_attack_spell::base_time_penalty( const npc &source ) const
{
    const spell &attack_spell = source.magic->get_spell( attack_spell_id );
    int time_penalty = 0;
    if( source.has_weapon() && !source.get_wielded_item()->has_flag( flag_MAGIC_FOCUS ) &&
        !attack_spell.has_flag( spell_flag::NO_HANDS ) && !source.has_flag( json_flag_SUBTLE_SPELL ) ) {
        time_penalty += npc_attack_constants::base_time_penalty;
    }
    // costs a point per second spent.
    time_penalty += std::round( attack_spell.casting_time( source ) * source.speed_rating() ) / 100.0f;
    return time_penalty;
}

npc_attack_rating npc_attack_spell::evaluate_tripoint(
    const npc &source, const Creature *target, const tripoint_bub_ms &location ) const
{
    const map &here = get_map();

    const spell &attack_spell = source.magic->get_spell( attack_spell_id );

    double total_potential = 0;

    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &potential_target : calculate_spell_effect_area( attack_spell, location,
            source ) ) {
        Creature *critter = creatures.creature_at( potential_target );

        if( !critter ) {
            // no critter? no damage! however, we assume fields are worth something
            if( attack_spell_id->field ) {
                const_dialogue d( get_const_talker_for( source ), nullptr );
                total_potential += static_cast<double>( attack_spell.field_intensity( source ) ) /
                                   static_cast<double>( attack_spell_id->field_chance.evaluate( d ) ) / 2.0;
            }
            continue;
        }

        const Creature::Attitude att = source.attitude_to( *critter );
        int damage = 0;
        if( source.sees( here, *critter ) ) {
            damage = attack_spell.dps( source, *critter );
        }
        const int distance_to_me = rl_dist( source.pos_bub(), potential_target );
        const bool friendly_fire = att == Creature::Attitude::FRIENDLY &&
                                   !source.rules.has_flag( ally_rule::avoid_friendly_fire );
        int attitude_mult = 3;
        if( att == Creature::Attitude::FRIENDLY ) {
            attitude_mult = -10;
        } else if( att == Creature::Attitude::NEUTRAL || friendly_fire ) {
            // hitting a neutral creature isn't exactly desired, but it's a lot less than a friendly.
            // if friendly fire is on, we don't care too much, though if an available hit doesn't damage them it would be better.
            attitude_mult = -1;
        }
        int potential = damage * attitude_mult - distance_to_me + 1;
        if( target && critter == target ) {
            potential *= npc_attack_constants::target_modifier;
        }
        if( damage >= critter->get_hp() ) {
            potential *= npc_attack_constants::kill_modifier;
            if( att == Creature::Attitude::NEUTRAL ) {
                // we don't care if we kill a neutral creature in one hit
                potential = std::abs( potential );
            }
            const Creature *source_ptr = &source;
            if( att == Creature::Attitude::FRIENDLY || critter == source_ptr ) {
                // however we under no circumstances want to kill an ally (or ourselves!)
                return npc_attack_rating( std::nullopt, location );
            }
        }
        total_potential += potential;
    }
    return npc_attack_rating( static_cast<int>( std::round( total_potential ) ), location );
}

// TODO: Give bashing weapons a better
// rating against armored targets
static double melee_damage_per_second( const item &weapon, const npc &source )
{
    double damage{ weapon.base_damage_melee().total_damage() };
    if( weapon.is_null() ) {
        // NPC-B05: a fist has no damage of its own, so a punch rated 0 and was never chosen.
        // Rate it by what it deals on average: strength, unarmed skill, martial arts.
        damage_instance punch;
        source.roll_all_damage( false, punch, true, weapon, attack_vector_id::NULL_ID(),
                                sub_bodypart_str_id::NULL_ID(), nullptr, body_part_torso );
        damage = punch.total_damage();
    }
    damage *= 100.0 / weapon.attack_time( source );
    return damage;
}

// NPC-B05: the barehanded attack is the null item, which is_wielding() (an address compare)
// never matches, so it is "in hand" when nothing is wielded.
static bool holds_melee_weapon( const npc &source, const item &weapon )
{
    return weapon.is_null() ? !source.is_armed() : source.is_wielding( weapon );
}

static npc_attack_rating rate_melee_critter( const npc &source, const Creature *target,
        Creature *critter, double damage, int reach_range );

void npc_attack_melee::use( npc &source, const tripoint_bub_ms &location ) const
{
    if( !holds_melee_weapon( source, weapon ) ) {
        // for the barehanded attack this puts the wielded item away
        if( !source.wield( weapon ) ) {
            debugmsg( "ERROR: npc tried to equip a weapon it couldn't wield" );
        }
        return;
    }
    Creature *critter = get_creature_tracker().creature_at( location );
    if( !critter ) {
        debugmsg( "ERROR: npc tried to attack null critter" );
        return;
    }
    int target_distance = rl_dist( source.pos_bub(), location );
    if( !source.is_adjacent( critter, true ) ) {
        if( target_distance <= weapon.reach_range( source ).first ) {
            add_msg_debug( debugmode::debug_filter::DF_NPC, "%s is attempting a reach attack",
                           source.disp_name() );
            // check for friendlies in the line of fire
            std::vector<tripoint_bub_ms> path = line_to( source.pos_bub(), location );
            path.pop_back(); // Last point is the target
            bool can_attack = true;
            for( const tripoint_bub_ms &path_point : path ) {
                Creature *inter = get_creature_tracker().creature_at( path_point );
                if( inter != nullptr && source.attitude_to( *inter ) == Creature::Attitude::FRIENDLY ) {
                    add_msg_debug( debugmode::debug_filter::DF_NPC, "%s aborted a reach attack; ally in the way",
                                   source.disp_name() );
                    can_attack = false;
                    break;
                }
            }
            if( can_attack ) {
                source.reach_attack( location );
            } else if( can_move_melee( source ) ) {
                source.avoid_friendly_fire();
            } else {
                source.look_for_player( get_player_character() );
            }
        } else {
            source.update_path( location );
            if( source.path.size() > 1 && !source.has_flag( json_flag_CANNOT_MOVE ) ) {
                bool clear_path = can_move_melee( source );
                if( clear_path && source.mem_combat.formation_distance == -1 ) {
                    source.move_to_next();
                    add_msg_debug( debugmode::DF_NPC_MOVEAI,
                                   "<color_light_gray>%s has no nearby ranged allies.  Going for attack.</color>", source.name );
                } else if( clear_path && source.mem_combat.formation_distance > target_distance ) {
                    source.move_to_next();
                    add_msg_debug( debugmode::DF_NPC_MOVEAI,
                                   "<color_light_gray>%s is at least %i away from ranged allies, enemy within %i.  Going for attack.</color>",
                                   source.name, source.mem_combat.formation_distance, target_distance );
                } else if( clear_path &&
                           source.mem_combat.formation_distance > source.closest_enemy_to_friendly_distance() ) {
                    source.move_to_next();
                    //add_msg_debug( debugmode::DF_NPC_MOVEAI,
                    //               "<color_light_gray>%s is at least %i away from allies, enemy within %i of ally.  Going for attack.</color>",
                    //               source.name, source.mem_combat.formation_distance, source.closest_enemy_to_friendly_distance() );
                } else if( source.mem_combat.formation_distance <= source.mem_combat.engagement_distance ) {
                    add_msg_debug( debugmode::DF_NPC_MOVEAI,
                                   "<color_light_gray>%s can't path to melee target, and is staying close to ranged allies.  Stay in place.</color>",
                                   source.name );
                    source.move_pause();
                } else {
                    add_msg_debug( debugmode::DF_NPC_MOVEAI,
                                   "<color_light_gray>%s can't path to melee target, and is not staying close to ranged allies.  Get close to player.</color>",
                                   source.name );
                    source.look_for_player( get_player_character() );
                }
            } else if( source.path.size() == 1 ) {
                if( critter != nullptr ) {
                    if( source.can_use_offensive_cbm() ) {
                        source.activate_bionic_by_id( bio_hydraulics );
                    }
                    add_msg_debug( debugmode::debug_filter::DF_NPC, "%s is attempting a melee attack",
                                   source.disp_name() );
                    source.melee_attack( *critter, true );
                }
            } else if( !source.has_flag( json_flag_CANNOT_MOVE ) ) {
                source.look_for_player( get_player_character() );
            } else {
                source.move_pause();
            }
        }
    } else if( source.mem_combat.formation_distance != -1 &&
               source.mem_combat.formation_distance <= target_distance &&
               rng( NPC_PERSONALITY_MIN, NPC_PERSONALITY_MAX ) > source.personality.aggression ) {
        add_msg_debug( debugmode::DF_NPC_MOVEAI,
                       "<color_light_gray>%s decided to fall back to formation with allies.</color>", source.name );
        source.look_for_player( get_player_character() );
    } else {
        // may wish to add a break here to see if target is out of formation with allies, and consider running back.
        add_msg_debug( debugmode::debug_filter::DF_NPC, "%s is attempting a melee attack",
                       source.disp_name() );
        source.melee_attack( *critter, true );
    }
}

tripoint_range<tripoint_bub_ms> npc_attack_melee::targetable_points( const npc &source ) const
{
    return get_map().points_in_radius( source.pos_bub(), 8 );
}

npc_attack_rating npc_attack_melee::evaluate( const npc &source,
        const Creature *target ) const
{
    npc_attack_rating effectiveness( std::nullopt, source.pos_bub() );
    if( !can_use( source ) ) {
        return effectiveness;
    }
    const int time_penalty = base_time_penalty( source );
    const double damage = melee_damage_per_second( weapon, source );
    const int reach_range = weapon.reach_range( source ).first;
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &targetable_point : creature_tiles_in( targetable_points( source ) ) ) {
        if( Creature *critter = creatures.creature_at( targetable_point ) ) {
            if( source.attitude_to( *critter ) != Creature::Attitude::HOSTILE ) {
                // no point in swinging a sword at a friendly!
                continue;
            }
            npc_attack_rating effectiveness_at_point = rate_melee_critter( source, target, critter, damage,
                    reach_range );
            effectiveness_at_point -= time_penalty;
            if( effectiveness_at_point > effectiveness ) {
                effectiveness = effectiveness_at_point;
            }
        }
    }
    return effectiveness;
}

std::vector<npc_attack_rating> npc_attack_melee::all_evaluations( const npc &source,
        const Creature *target ) const
{
    std::vector<npc_attack_rating> effectiveness;
    if( !can_use( source ) ) {
        return effectiveness;
    }
    const int time_penalty = base_time_penalty( source );
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &targetable_point : targetable_points( source ) ) {
        if( Creature *critter = creatures.creature_at( targetable_point ) ) {
            if( source.attitude_to( *critter ) != Creature::Attitude::HOSTILE ) {
                // no point in swinging a sword at a friendly!
                continue;
            }
            npc_attack_rating effectiveness_at_point = evaluate_critter( source, target, critter );
            effectiveness_at_point -= time_penalty;
            effectiveness.push_back( effectiveness_at_point );
        }
    }
    return effectiveness;
}

bool npc_attack_melee::can_use( const npc &source ) const
{
    if( holds_melee_weapon( source, weapon ) ) {
        return true;
    }
    if( weapon.is_null() ) {
        // punching means putting the wielded item away, which a bionic weapon or a
        // NO_UNWIELD item doesn't allow
        return source.can_unwield( *source.get_wielded_item() ).success();
    }
    // can't attack with something you can't wield
    return source.can_wield( weapon ).success();
}

int npc_attack_melee::base_time_penalty( const npc &source ) const
{
    return holds_melee_weapon( source, weapon ) ? 0 : npc_attack_constants::base_time_penalty;
}

npc_attack_rating npc_attack_melee::evaluate_critter( const npc &source,
        const Creature *target, Creature *critter ) const
{
    return rate_melee_critter( source, target, critter, melee_damage_per_second( weapon, source ),
                               weapon.reach_range( source ).first );
}

// npc_attack_melee::evaluate_critter(), with what only depends on the weapon worked out
// beforehand.
static npc_attack_rating rate_melee_critter( const npc &source, const Creature *target,
        Creature *critter, const double damage, const int reach_range )
{
    if( !critter ) {
        return npc_attack_rating{};
    }

    // Can't melee creatures that are underwater beneath a solid surface
    if( critter->is_underwater() ) {
        const map &here = get_map();
        if( here.has_flag( ter_furn_flag::TFLAG_SWIM_UNDER, critter->pos_bub() ) ) {
            return npc_attack_rating{};
        }
    }

    const int distance_to_me = clamp( rl_dist( source.pos_bub(), critter->pos_bub() ) - reach_range, 0,
                                      10 );
    // Multiplier of 0.5f to 1.5f based on distance
    const float distance_multiplier = 1.5f - distance_to_me * 0.1f;
    double potential = damage * distance_multiplier;

    if( damage >= critter->get_hp() ) {
        potential *= npc_attack_constants::kill_modifier;
    }
    if( target && target->pos_bub() == critter->pos_bub() ) {
        potential *= npc_attack_constants::target_modifier;
    }

    return npc_attack_rating( static_cast<int>( std::round( potential ) ), critter->pos_bub() );
}

void npc_attack_gun::use( npc &source, const tripoint_bub_ms &location ) const
{
    if( !source.is_wielding( gun ) ) {
        if( !source.wield( gun ) ) {
            debugmsg( "ERROR: npc tried to equip a weapon it couldn't wield" );
        }
        return;
    }

    // The evaluated mode fires either the base gun or an underbarrel gunmod.
    item &firing_gun = *gunmode.target;
    if( &firing_gun != &gun && !firing_gun.ammo_sufficient( &source ) ) {
        // can_use() rejects an empty gunmod, and only the base gun is reloaded here.
        source.move_pause();
        return;
    }

    if( &firing_gun == &gun && !gun.ammo_sufficient( &source ) ) {
        // todo: make gun an item_location instead of getting wielded item here
        // but since wielding is required before this, it should be fine
        source.do_reload( source.get_wielded_item() );
        add_msg_debug( debugmode::debug_filter::DF_NPC, "%s is reloading %s", source.disp_name(),
                       gun.display_name() );
        return;
    }

    if( has_obstruction( source.pos_bub(), location, false ) ||
        ( source.rules.has_flag( ally_rule::avoid_friendly_fire ) &&
          !source.wont_hit_friend( location, gun, false ) ) ) {
        if( can_move( source ) ) {
            source.avoid_friendly_fire();
        } else {
            source.move_pause();
        }
        return;
    }

    const Target_attributes target( source.pos_bub(), location );
    const aim_mods_cache aim_cache = source.gen_aim_mods_cache( gun );

    // Only aim if we aren't in risk of being hit
    // TODO: Get distance to closest enemy
    if( target.range > 1 && source.aim_per_move( gun, source.recoil, target, aim_cache ) > 0 &&
        source.confident_gun_mode_range( gunmode, source.recoil ) < target.range ) {
        add_msg_debug( debugmode::debug_filter::DF_NPC, "%s is aiming", source.disp_name() );
        source.aim( Target_attributes( source.pos_bub(), location ) );
    } else {
        if( &firing_gun == &gun ) {
            // as the avatar does: recoil and multimag ammo use the gun's current mode
            set_gun_mode( gun, gunmode );
        }
        // fire the evaluated mode's burst, not one shot of the wielded base gun
        if( source.is_hallucination() ) {
            // pretend_fire doesn't cap the burst by the ammo left, so cap it as fire_gun does
            const int shots = std::min( gunmode.qty,
                                        firing_gun.shots_remaining( get_map(), &source ) );
            if( shots > 0 ) {
                source.pretend_fire( &source, shots, firing_gun );
            }
        } else {
            source.fire_gun( get_map(), location, gunmode.qty, firing_gun );
        }
        add_msg_debug( debugmode::debug_filter::DF_NPC, "%s fires %s", source.disp_name(),
                       firing_gun.display_name() );
    }
}

bool npc_attack_gun::can_use( const npc &source ) const
{
    // CMBT-J03: the gun would be in the NPC's hands
    if( !source.ranged_ban_reason( *gunmode, true ).empty() ) {
        return false;
    }
    if( gunmode.target != &gun ) {
        // An underbarrel gunmod fires its own ammo, and use() only reloads the base gun.
        return ( source.is_wielding( gun ) || source.can_wield( gun ).success() ) &&
               gunmode->ammo_sufficient( &source );
    }
    // can't attack with something you can't wield.
    if( !source.is_wielding( *gunmode ) && !source.can_wield( *gunmode ).success() ) {
        return false;
    }
    // ammo_sufficient() goes through shots_remaining(), so it also counts
    // UPS/bionic power for energy_drain guns that have no magazine.
    if( gun.ammo_sufficient( &source ) ) {
        return true;
    }
    // An empty wielded gun is still usable: use() reloads it first.
    // can_reload_current() is only non-const because select_ammo() is.
    return source.is_wielding( gun ) && const_cast<npc &>( source ).can_reload_current() &&
           source.enough_time_to_reload( gun );
}

int npc_attack_gun::base_time_penalty( const npc &source ) const
{
    const item &weapon = *gunmode;
    int time_penalty = 0;
    // NPC-B08: the penalty is for a gun that must be wielded first; it was added to the
    // wielded one, so a spare copy always won and the NPC swapped guns every turn. An
    // underbarrel mode's gunmod is never wielded itself, so ask about the base gun.
    if( !source.is_wielding( gun ) ) {
        time_penalty += npc_attack_constants::base_time_penalty;
    }
    // we want the need to reload a gun cumulative with needing to wield the gun
    if( !weapon.ammo_sufficient( &source ) ) {
        time_penalty += npc_attack_constants::base_time_penalty;
    }
    return time_penalty;
}

// The damage of a gun mode's burst. use() only reloads the base gun, not an underbarrel gunmod,
// so only the base gun is rated with its default ammo when empty.
static int gun_attack_damage( const gun_mode &gunmode, const item &base_gun )
{
    const item &gun = *gunmode.target;
    const itype_id reload_ammo = &gun == &base_gun ? unloaded_gun_ammo_estimate( gun ) :
                                 itype_id::NULL_ID();
    const damage_instance gun_damage = reload_ammo.is_null() ? gun.gun_damage() :
                                       gun.gun_damage( reload_ammo );
    return gun_damage.total_damage() * gunmode.qty;
}

static npc_attack_rating rate_gun_tile( const npc &source, const Creature *target,
                                        const tripoint_bub_ms &location, const item &gun, int damage );

tripoint_range<tripoint_bub_ms> npc_attack_gun::targetable_points( const npc &source ) const
{
    const item &weapon = *gunmode;
    // use() only reloads the base gun, so an empty gunmod mode keeps its real (zero) range.
    const int range = &weapon == &gun ? gun_range_estimate( weapon ) : weapon.gun_range();
    return get_map().points_in_radius( source.pos_bub(), range );
}

npc_attack_rating npc_attack_gun::evaluate(
    const npc &source, const Creature *target ) const
{
    npc_attack_rating effectiveness( std::nullopt, source.pos_bub() );
    if( !can_use( source ) ) {
        return effectiveness;
    }
    const int time_penalty = base_time_penalty( source );
    const int damage = gun_attack_damage( gunmode, gun );
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &targetable_point : creature_tiles_in( targetable_points( source ) ) ) {
        if( creatures.creature_at( targetable_point ) ) {
            npc_attack_rating effectiveness_at_point = rate_gun_tile( source, target, targetable_point,
                    *gunmode.target, damage );
            effectiveness_at_point -= time_penalty;
            if( effectiveness_at_point > effectiveness ) {
                effectiveness = effectiveness_at_point;
            }
        }
    }
    return effectiveness;
}

std::vector<npc_attack_rating> npc_attack_gun::all_evaluations( const npc &source,
        const Creature *target ) const
{
    std::vector<npc_attack_rating> effectiveness;
    if( !can_use( source ) ) {
        return effectiveness;
    }
    const int time_penalty = base_time_penalty( source );
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &targetable_point : targetable_points( source ) ) {
        if( creatures.creature_at( targetable_point ) ) {
            npc_attack_rating effectiveness_at_point = evaluate_tripoint( source, target,
                    targetable_point );
            effectiveness_at_point -= time_penalty;
            effectiveness.push_back( effectiveness_at_point );
        }
    }
    return effectiveness;
}

npc_attack_rating npc_attack_gun::evaluate_tripoint(
    const npc &source, const Creature *target, const tripoint_bub_ms &location ) const
{
    return rate_gun_tile( source, target, location, *gunmode.target,
                          gun_attack_damage( gunmode, gun ) );
}

// npc_attack_gun::evaluate_tripoint(), with the damage of the gun mode worked out beforehand.
static npc_attack_rating rate_gun_tile( const npc &source, const Creature *target,
                                        const tripoint_bub_ms &location, const item &gun, const int damage )
{
    double potential = damage;

    const Creature *critter = get_creature_tracker().creature_at( location );
    if( !critter ) {
        // TODO: AOE ammo effects
        return npc_attack_rating( std::nullopt, location );
    }

    const Creature::Attitude att = source.attitude_to( *critter );
    if( att != Creature::Attitude::HOSTILE ) {
        // No point in attacking neutral/friendly targets
        return npc_attack_rating( std::nullopt, location );
    }

    const bool avoids_friendly_fire = source.rules.has_flag( ally_rule::avoid_friendly_fire );
    const int distance_to_me = rl_dist( location, source.pos_bub() );

    // Make attacks that involve moving to find clear LOS slightly less likely
    if( has_obstruction( source.pos_bub(), location, avoids_friendly_fire ) ) {
        potential *= 0.9f;
    } else if( avoids_friendly_fire &&
               !source.wont_hit_friend( location, gun, false ) ) {
        potential *= 0.95f;
    }

    // Prefer targets not directly next to you
    potential *= distance_to_me > 2 ? 1.15f : 0.85f;

    if( damage >= critter->get_hp() ) {
        potential *= npc_attack_constants::kill_modifier;
    }
    if( target && target->pos_bub() == critter->pos_bub() ) {
        potential *= npc_attack_constants::target_modifier;
    }
    return npc_attack_rating( static_cast<int>( std::round( potential ) ), location );
}

void npc_attack_activate_item::use( npc &source, const tripoint_bub_ms &/*location*/ ) const
{
    if( !source.wield( activatable_item ) ) {
        debugmsg( "%s can't wield %s it tried to activate", source.disp_name(),
                  activatable_item.display_name() );
        return;
    }
    // npc::wield may invalidate activatable_item's reference
    source.activate_item( *source.get_wielded_item() );
}

bool npc_attack_activate_item::can_use( const npc &source ) const
{
    if( !activatable_item.has_flag( flag_NPC_ACTIVATE ) ) {
        //TODO: make this more complete. only does BOMB type items
        return false;
    }
    const bool can_use_grenades = !source.is_hallucination() && ( !source.is_player_ally() ||
                                  source.rules.has_flag( ally_rule::use_grenades ) );
    return can_use_grenades;
}

npc_attack_rating npc_attack_activate_item::evaluate(
    const npc &source, const Creature * /*target*/ ) const
{
    if( !can_use( source ) ) {
        return npc_attack_rating( std::nullopt, source.pos_bub() );
    }
    // until we have better logic for grenades it's better to keep this as a last resort...
    const int emergency = source.emergency() ? 1 : 0;
    return npc_attack_rating( emergency, source.pos_bub() );
}

std::vector<npc_attack_rating> npc_attack_activate_item::all_evaluations( const npc &source,
        const Creature * /*target*/ ) const
{
    std::vector<npc_attack_rating> effectiveness;
    if( !can_use( source ) ) {
        return effectiveness;
    }
    // until we have better logic for grenades it's better to keep this as a last resort...
    const int emergency = source.emergency() ? 1 : 0;
    effectiveness.emplace_back( emergency, source.pos_bub() );
    return effectiveness;
}

// One of the thrown item, as it's thrown.
static item single_throw( const item &thrown_item )
{
    item single_item( thrown_item );
    if( single_item.count_by_charges() ) {
        single_item.charges = 1;
    }
    return single_item;
}

static npc_attack_rating rate_throw_tile( const npc &source, const Creature *target,
        const tripoint_bub_ms &location, const item &thrown_item, const item &single_item,
        float throw_mult );

// The blast radius of an explosive, as npc::find_dangerous_explosives() reads it: an explosion
// countdown_action (grenades, dynamite) or an "explosion" use action. 0 for anything else.
static int explosive_safe_range( const item &it )
{
    const explosion_iuse *actor = dynamic_cast<const explosion_iuse *>
                                  ( it.type->countdown_action.get_actor_ptr() );
    if( actor == nullptr ) {
        if( const use_function *use = it.type->get_use( "explosion" ) ) {
            actor = dynamic_cast<const explosion_iuse *>( use->get_actor_ptr() );
        }
    }
    return actor == nullptr ? 0 : actor->explosion.safe_range();
}

// Where something thrown from `from` at `to` comes down: the last tile before a wall in the way.
static tripoint_bub_ms throw_landing_point( const tripoint_bub_ms &from,
        const tripoint_bub_ms &to )
{
    const map &here = get_map();
    tripoint_bub_ms landing = from;
    for( const tripoint_bub_ms &p : line_to( from, to ) ) {
        if( here.impassable( p ) ) {
            break;
        }
        landing = p;
    }
    return landing;
}

// NPC-B07: where to get rid of an NPC_THROW_NOW item. This used to return the first tile of
// the range, which is always its north-west corner, whoever stood there. Each tile in range is
// rated instead, by where the item would come down:
// - farther from the thrower is better, and a clear line is worth 100, as before;
// - every tile the thrower or an ally would be inside the blast radius costs 50;
// - every tile an enemy would be inside it is worth 10, for up to 3 enemies;
// - with the avoid_friendly_fire rule, a throw that could hit a friend costs 150.
static npc_attack_rating rate_hot_potato( const npc &source, const item &thrown_item,
        const tripoint_range<tripoint_bub_ms> &in_range, const bool avoids_friendly_fire )
{
    const tripoint_bub_ms pos = source.pos_bub();
    const int safe_range = explosive_safe_range( thrown_item );
    std::vector<tripoint_bub_ms> allies;
    std::vector<tripoint_bub_ms> enemies;
    if( safe_range > 0 ) {
        const tripoint_rel_ms margin( safe_range, safe_range, 0 );
        const tripoint_range<tripoint_bub_ms> blast_area( in_range.min() - margin,
                in_range.max() + margin );
        creature_tracker &creatures = get_creature_tracker();
        for( const tripoint_bub_ms &p : creature_tiles_in( blast_area ) ) {
            const Creature *critter = creatures.creature_at( p );
            if( critter == nullptr || critter == &source ) {
                continue;
            }
            const Creature::Attitude att = source.attitude_to( *critter );
            if( att == Creature::Attitude::FRIENDLY ) {
                allies.push_back( p );
            } else if( att == Creature::Attitude::HOSTILE ) {
                enemies.push_back( p );
            }
        }
    }
    // how many tiles inside the blast radius `who` would be
    const auto inside_blast = [safe_range]( const tripoint_bub_ms & landing,
    const tripoint_bub_ms & who ) {
        return std::max( 0, safe_range - rl_dist( landing, who ) );
    };

    npc_attack_rating best( std::nullopt, pos );
    std::vector<int> enemy_depths;
    enemy_depths.reserve( enemies.size() );
    for( const tripoint_bub_ms &potential : in_range ) {
        const tripoint_bub_ms landing = throw_landing_point( pos, potential );
        int result = npc_attack_constants::base_throw_now + rl_dist( landing, pos );
        if( !has_obstruction( pos, potential, avoids_friendly_fire ) ) {
            // More likely to pick a target tile that isn't obstructed
            result += 100;
        }
        if( safe_range > 0 ) {
            result -= 50 * inside_blast( landing, pos );
            for( const tripoint_bub_ms &ally : allies ) {
                result -= 50 * inside_blast( landing, ally );
            }
            enemy_depths.clear();
            for( const tripoint_bub_ms &enemy : enemies ) {
                enemy_depths.push_back( inside_blast( landing, enemy ) );
            }
            std::sort( enemy_depths.begin(), enemy_depths.end(), std::greater<>() );
            for( size_t i = 0; i < enemy_depths.size() && i < 3; ++i ) {
                result += 10 * enemy_depths[i];
            }
        }
        // the friend check is the costly part, and can only lower the rating
        if( best > result ) {
            continue;
        }
        if( avoids_friendly_fire && !source.wont_hit_friend( potential, thrown_item, true ) ) {
            result -= 150;
        }
        // still well above any other attack, so the item always goes
        npc_attack_rating rated( std::max( result, npc_attack_constants::base_throw_now / 2 ),
                                 potential );
        if( rated > best ) {
            best = rated;
        }
    }
    return best;
}

void npc_attack_throw::use( npc &source, const tripoint_bub_ms &location ) const
{
    if( !source.is_wielding( thrown_item ) ) {
        if( !source.wield( thrown_item ) ) {
            debugmsg( "ERROR: npc tried to equip a weapon it couldn't wield" );
        }
        return;
    }

    // NPC-B07: a live explosive is thrown at once, to the tile evaluate() picked for it, even
    // if the line isn't clear: sidestepping to get a better line can last until it goes off
    if( !thrown_item.has_flag( flag_NPC_THROW_NOW ) &&
        ( has_obstruction( source.pos_bub(), location, false ) ||
          ( source.rules.has_flag( ally_rule::avoid_friendly_fire ) &&
            !source.wont_hit_friend( location, thrown_item, true ) ) ) ) {
        if( can_move( source ) ) {
            source.avoid_friendly_fire();
        } else {
            source.move_pause();
        }
        return;
    }

    item_location weapon = source.get_wielded_item();
    add_msg_debug( debugmode::debug_filter::DF_NPC, "%s throws the %s", source.disp_name(),
                   weapon->display_name() );
    item thrown( *weapon );
    if( weapon->count_by_charges() && weapon->charges > 1 ) {
        weapon->mod_charges( -1 );
        thrown.charges = 1;
    } else {
        source.remove_weapon();
    }
    source.throw_item( location, thrown );
}

bool npc_attack_throw::can_use( const npc &source ) const
{
    // Don't throw anything if we're hallucination
    // TODO: make an analogue of pretend_fire function
    if( source.is_hallucination() ) {
        return false;
    }

    if( !source.is_wielding( thrown_item ) && !source.can_wield( thrown_item ).success() ) {
        return false;
    }

    item single_item( thrown_item );
    if( single_item.count_by_charges() ) {
        single_item.charges = 1;
    }

    // Always allow throwing items that are flagged as throw now to
    // get rid of dangerous items ASAP, even if ranged attacks aren't allowed
    if( thrown_item.has_flag( flag_NPC_THROW_NOW ) ) {
        return true;
    }

    if( source.is_player_ally() && !source.rules.has_flag( ally_rule::use_guns ) ) {
        return false;
    }

    // Items flagged as NPC_THROWN are always allowed
    if( thrown_item.has_flag( flag_NPC_THROWN ) ) {
        return true;
    }

    bool throwable = source.throw_range( single_item ) > 0 && !source.is_worn( thrown_item ) &&
                     !thrown_item.has_flag( flag_NPC_ACTIVATE );
    throwable = throwable && !thrown_item.is_gun() && !thrown_item.is_armor() &&
                !thrown_item.is_comestible() && !thrown_item.is_magazine() && !thrown_item.is_tool();
    // TODO: Better choose what should be thrown
    return throwable;
}

int npc_attack_throw::base_penalty( const npc &source ) const
{
    item single_item( thrown_item );
    if( single_item.count_by_charges() ) {
        single_item.charges = 1;
    }
    const int time_penalty = source.is_wielding( single_item ) ? 0 :
                             npc_attack_constants::base_time_penalty;

    return time_penalty;
}

tripoint_range<tripoint_bub_ms> npc_attack_throw::targetable_points( const npc &source ) const
{
    item single_item( thrown_item );
    if( single_item.count_by_charges() ) {
        single_item.charges = 1;
    }
    const int range = source.throw_range( single_item );
    return get_map().points_in_radius( source.pos_bub(), range );
}

npc_attack_rating npc_attack_throw::evaluate(
    const npc &source, const Creature *target ) const
{
    npc_attack_rating effectiveness( std::nullopt, source.pos_bub() );
    if( !can_use( source ) ) {
        // please don't throw your pants...
        return effectiveness;
    }
    const temp_crafting_inventory &available_weapons = source.crafting_inventory( tripoint_bub_ms::zero,
            -1 );
    if( &thrown_item == best_weapon &&
        available_weapons.amount_of( thrown_item.typeId() ) <= 1 &&
        available_weapons.charges_of( thrown_item.typeId() ) <= 1 ) {
        // Don't throw if it's the best individual killy-thing we've got
        return effectiveness;
    }
    const int penalty = base_penalty( source );
    const bool throw_now = thrown_item.has_flag( flag_NPC_THROW_NOW );
    // TODO: Should this be a field to cache the result?
    const bool avoids_friendly_fire = source.rules.has_flag( ally_rule::avoid_friendly_fire );
    if( throw_now ) {
        // hot potato! HOT POTATO!
        return rate_hot_potato( source, thrown_item, targetable_points( source ), avoids_friendly_fire );
    }

    const item single_item = single_throw( thrown_item );
    const float throw_mult = throw_cost( source, single_item ) * source_speed_rating / 100.0f;
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &potential : creature_tiles_in( targetable_points( source ) ) ) {
        if( Creature *critter = creatures.creature_at( potential ) ) {
            if( source.attitude_to( *critter ) != Creature::Attitude::HOSTILE ) {
                // no point in friendly fire!
                continue;
            }
            npc_attack_rating effectiveness_at_point = rate_throw_tile( source, target, potential,
                    thrown_item, single_item, throw_mult );
            effectiveness_at_point -= penalty;
            if( effectiveness_at_point > effectiveness ) {
                effectiveness = effectiveness_at_point;
            }
        }
    }
    return effectiveness;
}

std::vector<npc_attack_rating> npc_attack_throw::all_evaluations( const npc &source,
        const Creature *target ) const
{
    std::vector<npc_attack_rating> effectiveness;
    if( !can_use( source ) ) {
        // please don't throw your pants...
        return effectiveness;
    }
    const int penalty = base_penalty( source );
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &potential : targetable_points( source ) ) {
        if( Creature *critter = creatures.creature_at( potential ) ) {
            if( source.attitude_to( *critter ) != Creature::Attitude::HOSTILE ) {
                // no point in friendly fire!
                continue;
            }
            npc_attack_rating effectiveness_at_point = evaluate_tripoint( source, target,
                    potential );
            effectiveness_at_point -= penalty;
            effectiveness.push_back( effectiveness_at_point );
        }
    }
    return effectiveness;
}

npc_attack_rating npc_attack_throw::evaluate_tripoint(
    const npc &source, const Creature *target, const tripoint_bub_ms &location ) const
{
    const item single_item = single_throw( thrown_item );
    return rate_throw_tile( source, target, location, thrown_item, single_item,
                            throw_cost( source, single_item ) * source_speed_rating / 100.0f );
}

// npc_attack_throw::evaluate_tripoint(), with the throw's speed worked out beforehand.
static npc_attack_rating rate_throw_tile( const npc &source, const Creature *target,
        const tripoint_bub_ms &location, const item &thrown_item, const item &single_item,
        const float throw_mult )
{
    if( has_obstruction( source.pos_bub(), location ) ) {
        return npc_attack_rating( std::nullopt, location );
    }

    Creature::Attitude att = Creature::Attitude::NEUTRAL;
    const Creature *critter = get_creature_tracker().creature_at( location );
    if( critter ) {
        att = source.attitude_to( *critter );
    }

    if( att != Creature::Attitude::HOSTILE ) {
        // No point in throwing stuff at neutral/friendly targets
        return npc_attack_rating( std::nullopt, location );
    }

    if( source.rules.has_flag( ally_rule::avoid_friendly_fire ) &&
        !source.wont_hit_friend( location, thrown_item, true ) ) {
        // Avoid friendy fire
        return npc_attack_rating( std::nullopt, location );
    }

    const int damage = source.thrown_item_total_damage_raw( single_item );
    float dps = damage / throw_mult;
    const int distance_to_me = rl_dist( location, source.pos_bub() );
    float suitable_item_mult = -0.15f;
    if( distance_to_me > 1 ) {
        if( thrown_item.has_flag( flag_NPC_THROWN ) ) {
            suitable_item_mult = 0.08f * std::min( distance_to_me - 1, 5 );
        }
    } else {
        suitable_item_mult = -0.35f;
    }

    double potential = dps * ( 1.0 + suitable_item_mult );
    if( potential > 0.0f && critter && damage >= critter->get_hp() ) {
        potential *= npc_attack_constants::kill_modifier;
    }
    if( potential > 0.0f && target && critter && target->pos_bub() == critter->pos_bub() ) {
        potential *= npc_attack_constants::target_modifier;
    }
    return npc_attack_rating( static_cast<int>( std::round( potential ) ), location );
}

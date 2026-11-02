#include "ballistics.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ammo_effect.h"
#include "character.h"
#include "coordinates.h"
#include "crafting.h"
#include "creature.h"
#include "creature_tracker.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "enums.h"
#include "explosion.h"
#include "field.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "line.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "options.h"
#include "point.h"
#include "projectile.h"
#include "ranged.h"
#include "rng.h"
#include "sounds.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "vpart_position.h"

static const ammo_effect_str_id ammo_effect_ACT_ON_RANGED_HIT( "ACT_ON_RANGED_HIT" );
static const ammo_effect_str_id ammo_effect_BOUNCE( "BOUNCE" );
static const ammo_effect_str_id ammo_effect_BURST( "BURST" );
static const ammo_effect_str_id ammo_effect_DRAW_AS_LINE( "DRAW_AS_LINE" );
static const ammo_effect_str_id ammo_effect_EXCESS_PEN( "EXCESS_PEN" );
static const ammo_effect_str_id ammo_effect_HEAVY_HIT( "HEAVY_HIT" );
static const ammo_effect_str_id ammo_effect_JET( "JET" );
static const ammo_effect_str_id ammo_effect_MUZZLE_SMOKE( "MUZZLE_SMOKE" );
static const ammo_effect_str_id ammo_effect_NO_EMBED( "NO_EMBED" );
static const ammo_effect_str_id ammo_effect_NO_ITEM_DAMAGE( "NO_ITEM_DAMAGE" );
static const ammo_effect_str_id ammo_effect_NO_OVERSHOOT( "NO_OVERSHOOT" );
static const ammo_effect_str_id ammo_effect_NO_PENETRATE_OBSTACLES( "NO_PENETRATE_OBSTACLES" );
static const ammo_effect_str_id ammo_effect_NULL_SOURCE( "NULL_SOURCE" );
static const ammo_effect_str_id ammo_effect_SHATTER_SELF( "SHATTER_SELF" );
static const ammo_effect_str_id ammo_effect_STREAM( "STREAM" );
static const ammo_effect_str_id ammo_effect_STREAM_BIG( "STREAM_BIG" );
static const ammo_effect_str_id ammo_effect_STREAM_TINY( "STREAM_TINY" );
static const ammo_effect_str_id ammo_effect_TANGLE( "TANGLE" );
static const ammo_effect_str_id ammo_effect_WIDE( "WIDE" );

static const damage_type_id damage_bash( "bash" );
static const damage_type_id damage_cut( "cut" );
static const damage_type_id damage_stab( "stab" );

static const itype_id itype_glass_shard( "glass_shard" );

static const json_character_flag json_flag_HARDTOHIT( "HARDTOHIT" );

static void drop_or_embed_projectile( map *here, const dealt_projectile_attack &attack,
                                      projectile &proj_arg )
{
    const projectile &proj = attack.proj;
    const item &drop_item = proj.get_drop();
    const auto &effects = proj.proj_effects;
    if( drop_item.is_null() ) {
        return;
    }

    const tripoint_bub_ms &pt = attack.end_point;

    if( effects.count( ammo_effect_SHATTER_SELF ) ) {
        // Drop the contents, not the thrown item
        add_msg_if_player_sees( pt, _( "The %s shatters!" ), drop_item.tname() );

        // copies the drop item to spill the contents
        item( drop_item ).spill_contents( here, pt );

        // TODO: Non-glass breaking
        // TODO: Wine glass breaking vs. entire sheet of glass breaking
        // TODO: Refine logic to allow for overlapping maps.
        if( here == &reality_bubble() ) {
            sounds::sound( pt, 16, sounds::sound_t::combat, _( "glass breaking!" ), false, "bullet_hit",
                           "hit_glass" );
        }

        const units::mass shard_mass = itype_glass_shard->weight;
        const int max_nb_of_shards = floor( to_gram( drop_item.type->weight ) / to_gram( shard_mass ) );
        //between half and max_nb_of_shards-1 will be usable
        const int nb_of_dropped_shard = std::max( 0, rng( max_nb_of_shards / 2, max_nb_of_shards - 1 ) );
        //feel free to remove this msg_debug
        /*add_msg_debug( "Shattered %s dropped %i shards out of a max of %i, based on mass %i g",
                       drop_item.tname(), nb_of_dropped_shard, max_nb_of_shards - 1, to_gram( drop_item.type->weight ) );*/

        for( int i = 0; i < nb_of_dropped_shard; ++i ) {
            item shard( itype_glass_shard );
            //actual dropping of shards
            here->add_item_or_charges( pt, shard );
        }

        return;
    }

    if( effects.count( ammo_effect_BURST ) ) {
        // Drop the contents, not the thrown item
        add_msg_if_player_sees( reality_bubble().get_bub( here->get_abs( pt ) ), _( "The %s bursts!" ),
                                drop_item.tname() );

        // copies the drop item to spill the contents
        item( drop_item ).spill_contents( here, pt );

        // TODO: Sound
        return;
    }

    // Copy the item
    item dropped_item = drop_item;

    monster *mon = dynamic_cast<monster *>( attack.last_hit_critter );

    // We can only embed in monsters
    bool mon_there = mon != nullptr && !mon->is_dead_state();
    // And if we actually want to embed
    bool embed = mon_there && effects.count( ammo_effect_NO_EMBED ) == 0 &&
                 effects.count( ammo_effect_TANGLE ) == 0;
    // Don't embed in small creatures
    if( embed ) {
        const creature_size critter_size = mon->get_size();
        const units::volume vol = dropped_item.volume();
        embed = embed && ( critter_size > creature_size::tiny || vol < 250_ml );
        embed = embed && ( critter_size > creature_size::small || vol < 500_ml );
        // And if we deal enough damage
        // Item volume bumps up the required damage too
        // FIXME: Hardcoded damage types
        embed = embed &&
                ( attack.dealt_dam.type_damage( damage_cut ) / 2 ) +
                attack.dealt_dam.type_damage( damage_stab ) >
                attack.dealt_dam.type_damage( damage_bash ) +
                vol * 3 / 250_ml + rng( 0, 5 );
    }

    if( embed ) {
        if( proj_arg.proj_effects.count( ammo_effect_BOUNCE ) ) {
            proj_arg.proj_effects.erase( ammo_effect_BOUNCE );
        }
        mon->add_item( dropped_item );
        add_msg_if_player_sees( pt, _( "The %1$s embeds in %2$s!" ),
                                dropped_item.tname(), mon->disp_name() );
    } else {
        bool do_drop = true;
        // monsters that are able to be tied up will store the item another way
        // see monexamine.cpp tie_or_untie()
        // if they aren't friendly they will try and break out of the net/bolas/lasso
        // players and NPCs just get the downed effect, and item is dropped.
        // TODO: storing the item on player until they recover from downed
        if( effects.count( ammo_effect_TANGLE ) && mon_there ) {
            do_drop = false;
        }
        if( effects.count( ammo_effect_ACT_ON_RANGED_HIT ) ) {
            // Don't drop if it exploded
            do_drop = !dropped_item.activate_thrown( attack.end_point );
        }

        map &here = get_map();
        if( do_drop ) {
            craft_relocated( here.add_item_or_charges_ret_loc( attack.end_point, dropped_item ) );
        }

        if( effects.count( ammo_effect_HEAVY_HIT ) ) {
            if( here.has_flag( ter_furn_flag::TFLAG_LIQUID, pt ) ) {
                sounds::sound( pt, 10, sounds::sound_t::combat, _( "splash!" ), false, "bullet_hit", "hit_water" );
            } else {
                sounds::sound( pt, 8, sounds::sound_t::combat, _( "thud." ), false, "bullet_hit", "hit_wall" );
            }
        }

        const trap &tr = here.tr_at( pt );
        if( tr.triggered_by_item( dropped_item ) ) {
            tr.trigger( pt, dropped_item );
        }
    }
}

static size_t blood_trail_len( int damage )
{
    if( damage > 50 ) {
        return 3;
    } else if( damage > 20 ) {
        return 2;
    } else if( damage > 0 ) {
        return 1;
    }
    return 0;
}

// Whether the pellets of a shot after the first can fly together (see fly_pellets_together()):
// nothing happens where a pellet stops (no item to drop, no explosion, no ammo effect that
// does anything there or along its path), and pellets stop where they hit (no bouncing or
// over-penetration).
static bool pellets_can_fly_together( const projectile &proj )
{
    if( !proj.get_drop().is_null() || proj.get_custom_explosion().power > 0.0f ) {
        return false;
    }
    for( const ammo_effect_str_id &id : proj.proj_effects ) {
        if( id == ammo_effect_BOUNCE || id == ammo_effect_EXCESS_PEN || id == ammo_effect_STREAM ||
            id == ammo_effect_STREAM_TINY || id == ammo_effect_STREAM_BIG || id == ammo_effect_JET ) {
            return false;
        }
        const ammo_effect &ae = id.obj();
        if( ae.do_flashbang || ae.do_emp_blast || ae.foamcrete_build ||
            ae.aoe_explosion_data.power > 0.0f || !ae.aoe_field_types.empty() ||
            !ae.trail_field_types.empty() || !ae.aoe_effects.empty() || !ae.spell_data.empty() ||
            !ae.eoc.empty() ) {
            return false;
        }
    }
    return true;
}

// Whether map::shoot() reduces nothing for this projectile on an empty tile: every damage
// unit counts for something and none for less than nothing.
static bool keeps_full_damage_through_air( const projectile &proj )
{
    const damage_instance &impact = proj.multishot ? proj.shot_impact : proj.impact;
    float total = 0.0f;
    for( const damage_unit &dam : impact ) {
        const float amount = dam.amount * dam.damage_multiplier * dam.unconditional_damage_mult +
                             dam.res_pen * dam.res_mult * dam.unconditional_res_mult;
        if( amount < 0.0f ) {
            return false;
        }
        total += amount;
    }
    return total > 0.0f;
}

namespace
{

// What projectile_attack() knows about a shot when its first pellet has flown.
struct pellet_shot {
    map *here;
    dealt_projectile_attack &attack;
    const projectile &proj_arg;
    const tripoint_bub_ms &source;
    const tripoint_bub_ms &target_arg;
    // where the shot goes after a wild miss
    const tripoint_bub_ms &target;
    // the first pellet's trajectory, from the source
    const std::vector<tripoint_bub_ms> &trajectory;
    const tripoint_bub_ms &first_p;
    double range;
    double extend_to_range;
    bool no_overshoot;
    double aim_missed_by;
    Creature *origin;
    const vehicle *in_veh;
    const weakpoint_attack &wp_attack;
    bool null_source;
    bool no_item_damage;
    bool is_bullet;
    bool print_messages;
    bool multishot;
    double point_blank_rescale;
};

} // namespace

// Whether a pellet with full damage that flies through tp does nothing there, draws no
// random numbers and isn't changed: no creature to hit, nothing to shoot through or smash.
static bool quiet_tile( const pellet_shot &shot, const tripoint_bub_ms &tp, const int distance,
                        const bool hit_items )
{
    map &here = *shot.here;
    Creature *critter = get_creature_tracker().creature_at( tp );
    if( critter == shot.origin ) {
        critter = nullptr;
    }
    const monster *mon = dynamic_cast<const monster *>( critter );
    if( mon != nullptr && mon->digging() && distance > 1 ) {
        critter = nullptr;
    }
    if( critter != nullptr || !here.passable( tp ) ) {
        return false;
    }
    const optional_vpart_position vp = here.veh_at( tp );
    if( shot.in_veh != nullptr && veh_pointer_or_null( vp ) == shot.in_veh ) {
        // a turret doesn't shoot its own vehicle
        return true;
    }
    return !vp && !here.furn( tp )->shoot && !here.ter( tp )->shoot &&
           here.field_at( tp ).field_count() == 0 && !( hit_items && here.has_items( tp ) );
}

// The pellets of a shot after the first (CMBT-P01). Each pellet draws its spread as
// projectile_attack() draws it, then pellets with the same trajectory fly together, all
// pellets a tile at a time. Where a tile is quiet for a group (see quiet_tile()), nothing
// happens to any of its pellets, so the group passes it at once. Anywhere else (a creature,
// an obstacle, a field, items to smash) each pellet is resolved on its own, as before, in the
// order the pellets were fired: each hit still rolls to hit and goes through armor on its
// own. Pellets that come out of a tile changed (slowed by an obstacle) carry their own
// projectile from then on.
//
// What differs from flying the pellets one after the other: the random numbers come in a
// different order; each trajectory is traced when the second pellet flies, not when its own
// pellet does (it differs only if an earlier pellet of the same shot has changed what blocks
// sight on the way); and of a run of out-of-bounds pellets only the first reports it.
static void fly_pellets_together( const pellet_shot &shot, const int count )
{
    map *here = shot.here;
    dealt_projectile_attack &attack = shot.attack;
    const projectile &proj_arg = shot.proj_arg;
    const tripoint_bub_ms &source = shot.source;
    creature_tracker &creatures = get_creature_tracker();

    struct pellet_path {
        std::vector<tripoint_bub_ms> points;
        size_t length = 0;
        tripoint_bub_ms target_c;
        // the projectile its pellets carry until they come out of a tile changed
        projectile state;
        std::vector<int> pellets;
        int flying = 0;
        // some of its pellets carry their own projectile, so they fly one by one
        bool one_by_one = false;
        tripoint_bub_ms prev;
        // its pellets that have a held_prev
        std::vector<int> held;
    };
    struct pellet {
        double spread = 0.0;
        int path = 0;
        int own_state = -1;
        bool flying = true;
        // Where the pellet was before the tiles it passed over without touching (the
        // creature was underwater, was the avatar under its own turret, or was a friend not
        // to shoot), when that isn't where its path was, and the step it last did so.
        std::optional<tripoint_bub_ms> held_prev;
        size_t held_step = 0;
    };
    std::vector<pellet_path> paths;
    std::map<tripoint_bub_ms, int> path_to;
    int unspread_path = -1;
    std::vector<pellet> pellets( count );
    std::vector<projectile> own_states;

    const auto add_path = [&]( std::vector<tripoint_bub_ms> &&points,
    const tripoint_bub_ms & target_c ) {
        pellet_path path;
        path.points = std::move( points );
        // Range can be 0
        path.length = path.points.size();
        while( path.length > 0 && rl_dist( source, path.points[path.length - 1] ) > proj_arg.range ) {
            --path.length;
        }
        path.target_c = target_c;
        path.state = proj_arg;
        path.state.multishot = shot.multishot;
        path.one_by_one = !keeps_full_damage_through_air( path.state );
        path.prev = source;
        paths.push_back( std::move( path ) );
        return static_cast<int>( paths.size() ) - 1;
    };

    for( int k = 0; k < count; ++k ) {
        pellet &pl = pellets[k];
        tripoint_bub_ms target_c = shot.target;
        bool spread_out = false;
        // calculate dispersion from shot_spread
        if( proj_arg.shot_spread > 0 ) {
            double sample = rng_normal( -proj_arg.shot_spread, proj_arg.shot_spread );
            sample = sample >= 0 ? sample : -sample;
            units::angle arc = std::min( units::from_arcmin( sample ), 30_degrees );
            pl.spread = tan( arc / 2 ) * 2.0;
            double max_spread = pl.spread * ( shot.trajectory.size() - 1 );
            if( max_spread >= 1.0 ) {
                double dx = target_c.x() - source.x();
                double dy = target_c.y() - source.y();
                units::angle d = units::atan2( dy, dx );
                d += ( one_in( 2 ) ? 1 : -1 ) * arc;
                target_c.x() = std::clamp( source.x() + roll_remainder( shot.range * cos( d ) ),
                                           0, MAPSIZE_X - 1 );
                target_c.y() = std::clamp( source.y() + roll_remainder( shot.range * sin( d ) ),
                                           0, MAPSIZE_Y - 1 );
                if( target_c == source ) {
                    target_c.x() = source.x() + sgn( dx );
                    target_c.y() = source.y() + sgn( dy );
                }
                spread_out = true;
            }
        }
        if( !spread_out ) {
            if( unspread_path < 0 ) {
                std::vector<tripoint_bub_ms> points = shot.trajectory;
                unspread_path = add_path( std::move( points ), target_c );
            }
            pl.path = unspread_path;
        } else if( const auto known = path_to.find( target_c ); known != path_to.end() ) {
            pl.path = known->second;
        } else {
            std::vector<tripoint_bub_ms> points = here->find_clear_path( shot.first_p, target_c );
            // point-blank tile should be the same
            points.insert( points.begin(), shot.first_p );
            points.insert( points.begin(), source );
            if( !shot.no_overshoot && shot.range < shot.extend_to_range ) {
                std::vector<tripoint_bub_ms> extension = continue_line( points,
                        shot.extend_to_range - shot.range );
                points.reserve( points.size() + extension.size() );
                points.insert( points.end(), extension.begin(), extension.end() );
            }
            pl.path = add_path( std::move( points ), target_c );
            path_to.emplace( target_c, pl.path );
        }
        paths[pl.path].pellets.push_back( k );
        paths[pl.path].flying++;
    }

    // What projectile_attack() leaves in the attack comes from the last pellet (where it
    // stopped, what it hit) and from the last pellet to hit anything.
    const int last = count - 1;
    tripoint_bub_ms last_end = attack.end_point;
    Creature *last_hit = attack.last_hit_critter;
    int last_hitter = -1;
    bool hit_headshot = attack.headshot;
    double hit_missed_by = attack.missed_by;
    dealt_damage_instance hit_dealt = attack.dealt_dam;

    // attack.proj is the projectile of the pellet in flight; this says whose it holds
    int held_state = INT_MIN;
    const auto state_key = [&]( const pellet & pl ) {
        return pl.own_state >= 0 ? pl.own_state : -1 - pl.path;
    };
    const auto hold = [&]( const pellet & pl ) {
        const int key = state_key( pl );
        if( key != held_state ) {
            attack.proj = pl.own_state >= 0 ? own_states[pl.own_state] : paths[pl.path].state;
            held_state = key;
        }
    };
    // the last pellet's projectile, when it changed on the tile it stopped on
    std::optional<projectile> last_state;
    const auto stop = [&]( const int k, pellet & pl, const tripoint_bub_ms & end, Creature * hit ) {
        pl.flying = false;
        paths[pl.path].flying--;
        if( k == last ) {
            last_end = end;
            last_hit = hit;
        }
    };
    // projectile_attack()'s prev_point for this pellet
    const auto pellet_prev = [&]( const pellet & pl ) {
        return pl.held_prev ? *pl.held_prev : paths[pl.path].prev;
    };
    // where a pellet that flew all of its trajectory stops
    const auto path_end = [&]( const pellet & pl ) {
        const pellet_path &path = paths[pl.path];
        if( path.length < 2 ) {
            return attack.end_point;
        }
        const tripoint_bub_ms &end = path.points[path.length - 1];
        return here->impassable( end ) ? pellet_prev( pl ) : end;
    };

    // One pellet through a tile that isn't quiet, as projectile_attack() flies each pellet.
    const auto fly_through = [&]( const int k, const size_t i ) {
        pellet &pl = pellets[k];
        pellet_path &path = paths[pl.path];
        const tripoint_bub_ms &tp = path.points[i];
        const int distance = rl_dist( source, tp );
        hold( pl );
        projectile &proj = attack.proj;
        bool has_momentum = true;
        bool changed = false;
        const bool hit_items = !shot.no_item_damage && tp == path.target_c;

        Creature *critter = creatures.creature_at( tp );
        if( shot.origin == critter ) {
            // No hitting self with "weird" attacks.
            critter = nullptr;
        }
        monster *mon = dynamic_cast<monster *>( critter );
        // ignore non-point-blank digging targets (since they are underground)
        if( mon != nullptr && mon->digging() && distance > 1 ) {
            critter = nullptr;
        }
        // Reset hit critter from the last iteration
        attack.last_hit_critter = nullptr;
        double cur_missed_by = std::min( 1.0, shot.aim_missed_by + pl.spread * std::max( distance - 1,
                                         0 ) );
        // unintentional hit on something other than our actual target
        if( critter != nullptr && tp != shot.target_arg ) {
            cur_missed_by = std::max( rng_float( 0.1, 1.5 - shot.aim_missed_by ) /
                                      critter->ranged_target_size(), 0.4 );
        }

        // projectile_attack() goes on to the next tile without moving prev_point
        const auto pass_over = [&]() {
            if( !pl.held_prev ) {
                pl.held_prev = path.prev;
                path.held.push_back( k );
            }
            pl.held_step = i;
        };
        const auto keep_state = [&]() {
            if( !changed ) {
                return;
            }
            if( pl.own_state < 0 ) {
                pl.own_state = static_cast<int>( own_states.size() );
                own_states.push_back( attack.proj );
            } else {
                own_states[pl.own_state] = attack.proj;
            }
            held_state = pl.own_state;
            path.one_by_one = true;
        };
        const auto shoot = [&]() {
            const double it = here->shoot( tp, proj, hit_items );
            if( it != 1.0 ) {
                changed = true;
                held_state = INT_MIN;
            }
            if( it > 0 ) {
                if( proj.count > 1 ) {
                    proj.shot_impact.mult_damage( it );
                } else {
                    proj.impact.mult_damage( it );
                }
            }
            return it;
        };

        if( critter != nullptr && cur_missed_by < 1.0 ) {
            // Prevent shots from hitting underwater creatures that are beneath surface (except thin ice);
            // treat the shot as hitting the terrain instead.
            if( critter->is_underwater() &&
                here->has_flag( ter_furn_flag::TFLAG_SWIM_UNDER, critter->pos_bub() ) &&
                !here->has_flag( ter_furn_flag::TFLAG_THIN_ICE, critter->pos_bub() ) ) {
                shoot();
                if( shot.origin != nullptr && shot.origin->is_avatar() ) {
                    add_msg( m_info, _( "Your shot is blocked by the surface." ) );
                }
                keep_state();
                pass_over();
                return;
            }
            if( shot.in_veh != nullptr && veh_pointer_or_null( here->veh_at( tp ) ) == shot.in_veh &&
                critter->is_avatar() ) {
                pass_over();
                return;
            }
            // avoid friendly fire
            if( critter->attitude_to( *shot.origin ) == Creature::Attitude::FRIENDLY &&
                shot.origin->check_avoid_friendly_fire() ) {
                pass_over();
                return;
            }
            critter->deal_projectile_attack( here, shot.null_source ? nullptr : shot.origin, attack,
                                             cur_missed_by, shot.print_messages, shot.wp_attack );

            if( critter->is_npc() ) {
                critter->as_npc()->on_attacked( *shot.origin );
            }

            // Critter can still dodge the projectile
            // In this case hit_critter won't be set
            if( attack.last_hit_critter != nullptr ) {
                const field_type_id blood_type = critter->bloodType();
                if( blood_type ) {
                    const size_t bt_len = blood_trail_len( attack.dealt_dam.total_damage() );
                    if( bt_len > 0 ) {
                        const tripoint_bub_ms &dest = move_along_line( tp, path.points, bt_len );
                        here->add_splatter_trail( blood_type, tp, dest );
                    }
                }
                if( here == &reality_bubble() ) {
                    sfx::do_projectile_hit( *attack.last_hit_critter );
                }
                has_momentum = false;
                // on-hit effects for inflicted damage types
                for( const std::pair<const damage_type_id, int> &dt : attack.dealt_dam.dealt_dams ) {
                    dt.first->onhit_effects( shot.origin, attack.last_hit_critter );
                }
                if( k > last_hitter ) {
                    last_hitter = k;
                    hit_headshot = attack.headshot;
                    hit_missed_by = attack.missed_by;
                    hit_dealt = attack.dealt_dam;
                }
            }
        } else if( shot.in_veh != nullptr && veh_pointer_or_null( here->veh_at( tp ) ) == shot.in_veh ) {
            // Don't do anything, especially don't call map::shoot as this would damage the vehicle
        } else if( shoot() <= 0 ) {
            has_momentum = false;
        }

        if( !has_momentum || ( !shot.is_bullet && here->impassable( tp ) ) ) {
            if( k == last && changed ) {
                last_state = attack.proj;
            }
            stop( k, pl, here->impassable( tp ) ? pellet_prev( pl ) : tp, attack.last_hit_critter );
            return;
        }
        keep_state();
    };

    size_t longest = 0;
    for( const pellet_path &path : paths ) {
        longest = std::max( longest, path.length );
    }
    // Paths on the same tile in a step share its quiet check.
    struct quiet_check {
        tripoint_bub_ms tp;
        bool hit_items = false;
        int distance = 0;
        bool quiet = false;
        std::vector<int> paths;
    };
    std::vector<quiet_check> checks;
    // the pellets to fly through their tile this step, first fired first
    std::priority_queue<int, std::vector<int>, std::greater<>> pending;
    // the paths whose pellets moved on this step
    std::vector<int> moved;
    for( size_t i = 1; i < longest; ++i ) {
        checks.clear();
        moved.clear();
        for( size_t p = 0; p < paths.size(); ++p ) {
            pellet_path &path = paths[p];
            if( path.flying == 0 ) {
                continue;
            }
            if( i >= path.length ) {
                // the end of the trajectory
                for( const int k : path.pellets ) {
                    if( pellets[k].flying ) {
                        stop( k, pellets[k], path_end( pellets[k] ), nullptr );
                    }
                }
                continue;
            }
            const tripoint_bub_ms &tp = path.points[i];
            if( !here->inbounds( tp ) ) {
                debugmsg( "Shot along %s out-of-bounds", tp.to_string() );
                for( const int k : path.pellets ) {
                    if( pellets[k].flying ) {
                        stop( k, pellets[k], pellet_prev( pellets[k] ), nullptr );
                    }
                }
                continue;
            }
            const int distance = rl_dist( source, tp );
            // no spread at point-blank, skip point-blank calculate
            if( distance <= 1 ) {
                path.state.shot_impact.mult_damage( shot.point_blank_rescale );
                for( const int k : path.pellets ) {
                    if( pellets[k].flying && pellets[k].own_state >= 0 ) {
                        own_states[pellets[k].own_state].shot_impact.mult_damage( shot.point_blank_rescale );
                    }
                }
                held_state = INT_MIN;
                moved.push_back( static_cast<int>( p ) );
                continue;
            }
            if( shot.in_veh != nullptr ) {
                const optional_vpart_position other = here->veh_at( tp );
                if( shot.in_veh == veh_pointer_or_null( other ) && other->is_inside() ) {
                    // Turret is on the roof and can't hit anything inside
                    continue;
                }
            }
            moved.push_back( static_cast<int>( p ) );
            if( !path.one_by_one ) {
                const bool hit_items = !shot.no_item_damage && tp == path.target_c;
                auto check = std::find_if( checks.begin(), checks.end(), [&]( const quiet_check & c ) {
                    return c.tp == tp && c.hit_items == hit_items;
                } );
                if( check == checks.end() ) {
                    checks.push_back( { tp, hit_items, distance, quiet_tile( shot, tp, distance, hit_items ), {} } );
                    check = checks.end() - 1;
                }
                if( check->quiet ) {
                    check->paths.push_back( static_cast<int>( p ) );
                    continue;
                }
            }
            for( const int k : path.pellets ) {
                if( pellets[k].flying ) {
                    pending.push( k );
                }
            }
        }
        // in the order the pellets were fired, as projectile_attack() flies them
        while( !pending.empty() ) {
            const int k = pending.top();
            pending.pop();
            fly_through( k, i );
            // What happened there can reach other tiles of this step (a creature spawned or
            // moved there, a field, items): the pellets fired after this one that are still
            // to pass such a tile now fly through it one by one.
            for( quiet_check &check : checks ) {
                if( check.quiet && !quiet_tile( shot, check.tp, check.distance, check.hit_items ) ) {
                    check.quiet = false;
                    for( const int p : check.paths ) {
                        for( const int later : paths[p].pellets ) {
                            if( later > k && pellets[later].flying ) {
                                pending.push( later );
                            }
                        }
                    }
                }
            }
        }
        for( const int p : moved ) {
            pellet_path &path = paths[p];
            path.prev = path.points[i];
            // pellets that didn't pass over this tile moved their prev_point here
            path.held.erase( std::remove_if( path.held.begin(), path.held.end(), [&]( const int k ) {
                pellet &pl = pellets[k];
                if( pl.flying && pl.held_step == i ) {
                    return false;
                }
                pl.held_prev.reset();
                return true;
            } ), path.held.end() );
        }
    }
    for( pellet_path &path : paths ) {
        for( const int k : path.pellets ) {
            if( pellets[k].flying ) {
                stop( k, pellets[k], path_end( pellets[k] ), nullptr );
            }
        }
    }

    if( last_state ) {
        attack.proj = *last_state;
    } else {
        attack.proj = pellets[last].own_state >= 0 ? own_states[pellets[last].own_state] :
                      paths[pellets[last].path].state;
    }
    attack.end_point = last_end;
    attack.last_hit_critter = last_hit;
    attack.headshot = hit_headshot;
    attack.missed_by = hit_missed_by;
    attack.dealt_dam = hit_dealt;
}

projectile_attack_aim projectile_attack_roll( const dispersion_sources &dispersion, double range,
        double target_size )
{
    projectile_attack_aim aim;

    // dispersion is a measure of the dispersion of shots due to the gun + shooter characteristics
    // i.e. it is independent of any particular shot

    // shot_dispersion is the actual dispersion for this particular shot, i.e.
    // the error angle between where the shot was aimed and where this one actually went
    // NB: some cases pass dispersion == 0 for a "never misses" shot e.g. bio_magnet,
    aim.dispersion = dispersion.roll();
    add_msg_debug( debugmode::DF_BALLISTIC, "Dispersion rolled / max : %f / %f; %f / %f degrees",
                   aim.dispersion, dispersion.max(), aim.dispersion / 60.0, dispersion.max() / 60.0 );

    // an isosceles triangle is formed by the intended and actual target tiles
    aim.missed_by_tiles = iso_tangent( range, units::from_arcmin( aim.dispersion ) );

    // fraction we missed a monster target by (0.0 = perfect hit, 1.0 = miss)
    if( target_size > 0.0 ) {
        aim.missed_by = std::min( 1.0, aim.missed_by_tiles / target_size );
    } else {
        // Special case 0 size targets, just to be safe from 0.0/0.0 NaNs
        aim.missed_by = 1.0;
    }

    return aim;
}

void projectile_attack( dealt_projectile_attack &attack, const projectile &proj_arg,
                        const tripoint_bub_ms &source, const tripoint_bub_ms &target_arg,
                        const dispersion_sources &dispersion, Creature *origin, const vehicle *in_veh,
                        const weakpoint_attack &wp_attack )
{
    projectile_attack( attack, proj_arg, &get_map(), source, target_arg, dispersion, origin, in_veh,
                       wp_attack );
}

void projectile_attack( dealt_projectile_attack &attack, const projectile &proj_arg,
                        map *here, const tripoint_bub_ms &source, const tripoint_bub_ms &target_arg,
                        const dispersion_sources &dispersion, Creature *origin, const vehicle *in_veh,
                        const weakpoint_attack &wp_attack )
{
    const bool do_animation = get_option<bool>( "ANIMATION_PROJECTILES" );

    double range = rl_dist( source, target_arg );

    creature_tracker &creatures = get_creature_tracker();
    Creature *target_critter = creatures.creature_at( target_arg );
    double target_size;

    if( target_critter != nullptr ) {
        const monster *mon = target_critter->as_monster();
        if( mon && proj_arg.proj_effects.count( ammo_effect_WIDE ) ) {
            // ammo with ammo_effect_WIDE ignores mon_flag_HARDTOSHOOT
            target_size = occupied_tile_fraction( mon->get_size() );
        } else {
            target_size = target_critter->ranged_target_size();
        }
    } else {
        target_size = here->ranged_target_size( target_arg );
    }
    projectile_attack_aim aim = projectile_attack_roll( dispersion, range, target_size );

    if( target_critter && target_critter->as_character() &&
        target_critter->as_character()->has_flag( json_flag_HARDTOHIT ) ) {

        projectile_attack_aim lucky_aim = projectile_attack_roll( dispersion, range, target_size );
        // if the target's lucky they're more likely to be missed
        if( lucky_aim.missed_by > aim.missed_by ) {
            aim = lucky_aim;
        }
    }

    // TODO: move to-hit roll back in here

    attack.proj = proj_arg;
    attack.last_hit_critter = nullptr;
    attack.dealt_dam = dealt_damage_instance();
    attack.end_point = source;
    attack.missed_by = aim.missed_by;

    // No suicidal shots
    if( source == target_arg ) {
        debugmsg( "Projectile_attack targeted own square." );
        return;
    }

    projectile &proj = attack.proj;
    const auto &proj_effects = proj.proj_effects;

    const bool stream = proj_effects.count( ammo_effect_STREAM ) > 0 ||
                        proj_effects.count( ammo_effect_STREAM_TINY ) > 0 ||
                        proj_effects.count( ammo_effect_STREAM_BIG ) > 0 ||
                        proj_effects.count( ammo_effect_JET ) > 0;
    const char bullet = stream ? '#' : '*';
    const bool no_item_damage = proj_effects.count( ammo_effect_NO_ITEM_DAMAGE ) > 0;
    const bool do_draw_line = proj_effects.count( ammo_effect_DRAW_AS_LINE ) > 0;
    const bool null_source = proj_effects.count( ammo_effect_NULL_SOURCE ) > 0;
    // Determines whether it can penetrate obstacles
    const bool is_bullet = proj_arg.speed >= 200 &&
                           !proj_effects.count( ammo_effect_NO_PENETRATE_OBSTACLES );

    // If we were targeting a tile rather than a monster, don't overshoot
    // Unless the target was a wall, then we are aiming high enough to overshoot
    const bool no_overshoot = proj_effects.count( ammo_effect_NO_OVERSHOOT ) ||
                              ( creatures.creature_at( here->get_abs( target_arg ) ) == nullptr && here->passable( target_arg ) );

    double extend_to_range = no_overshoot ? range : proj_arg.range;

    tripoint_bub_ms target = target_arg;
    std::vector<tripoint_bub_ms> trajectory;
    if( aim.missed_by_tiles >= 1.0 ) {
        // We missed enough to target a different tile
        double dx = target_arg.x() - source.x();
        double dy = target_arg.y() - source.y();
        units::angle rad = units::atan2( dy, dx );

        // cap wild misses at +/- 30 degrees
        units::angle dispersion_angle =
            std::min( units::from_arcmin( aim.dispersion ), 30_degrees );
        rad += ( one_in( 2 ) ? 1 : -1 ) * dispersion_angle;

        // TODO: This should also represent the miss on z axis
        const int offset = std::min<int>( range, std::sqrt( aim.missed_by_tiles ) );
        int new_range = no_overshoot ?
                        range + rng( -offset, offset ) :
                        rng( range - offset, proj_arg.range );
        new_range = std::max( new_range, 1 );

        target.x() = std::clamp( source.x() + roll_remainder( new_range * cos( rad ) ), 0, MAPSIZE_X - 1 );
        target.y() = std::clamp( source.y() + roll_remainder( new_range * sin( rad ) ), 0, MAPSIZE_Y - 1 );

        if( target == source ) {
            target.x() = source.x() + sgn( dx );
            target.y() = source.y() + sgn( dy );
        }

        // Don't extend range further, miss here can mean hitting the ground near the target
        range = rl_dist( source, target );
        extend_to_range = range;

        if( reality_bubble().inbounds( here->get_abs( target ) ) ) {
            const tripoint_bub_ms bub_target = reality_bubble().get_bub( here->get_abs( target ) );
            sfx::play_variant_sound( "bullet_hit", "hit_wall", sfx::get_heard_volume( bub_target ),
                                     sfx::get_heard_angle( bub_target ) );
        }
        // TODO: Z dispersion
    }

    //Use find clear path to draw the trajectory with optimal initial tile offsets.
    trajectory = here->find_clear_path( source, target );

    add_msg_debug( debugmode::DF_BALLISTIC,
                   "missed_by_tiles: %.2f; missed_by: %.2f; target (orig/hit): %s/%s",
                   aim.missed_by_tiles, aim.missed_by, target_arg.to_string_writable(),
                   target.to_string_writable() );

    // Trace the trajectory, doing damage in order
    tripoint_bub_ms &tp = attack.end_point;

    // Add the first point to the trajectory
    trajectory.insert( trajectory.begin(), source );

    static emit_id muzzle_smoke( "emit_smaller_smoke_plume" );
    if( proj_effects.count( ammo_effect_MUZZLE_SMOKE ) ) {
        here->emit_field( trajectory.front(), muzzle_smoke );
    }

    if( !no_overshoot && range < extend_to_range ) {
        // Continue line is very "stiff" when the original range is short
        // TODO: Make it use a more distant point for more realistic extended lines
        std::vector<tripoint_bub_ms> trajectory_extension = continue_line( trajectory,
                extend_to_range - range );
        trajectory.reserve( trajectory.size() + trajectory_extension.size() );
        trajectory.insert( trajectory.end(), trajectory_extension.begin(), trajectory_extension.end() );
    }

    bool first = true;
    bool print_messages = true;
    bool multishot = false;
    // If the proj hits a ter/furn at point-blank and is not stopped, rescale shot_impact for all projs.
    double point_blank_rescale = 1.0;
    tripoint_bub_ms first_p = trajectory[1];
    // After the first pellet, the pellets of a shot that stays on its z-level fly together
    // when nothing happens where they stop (see fly_pellets_together()).
    const bool pellets_together = proj_arg.count > 1 && source.z() == target.z() &&
                                  pellets_can_fly_together( proj_arg );
    for( int j = 0; j < proj.count; ++j ) {
        if( !first && pellets_together ) {
            const pellet_shot shot{ here, attack, proj_arg, source, target_arg, target, trajectory,
                                    first_p, range, extend_to_range, no_overshoot, aim.missed_by, origin, in_veh,
                                    wp_attack, null_source, no_item_damage, is_bullet, print_messages, multishot,
                                    point_blank_rescale };
            fly_pellets_together( shot, proj.count - j );
            break;
        }
        tripoint_bub_ms prev_point = source;
        proj = proj_arg;
        proj.multishot = multishot;
        tripoint_bub_ms target_c = target;
        std::vector<tripoint_bub_ms> t_copy = trajectory;
        double spread = 0;
        // calculate dispersion from shot_spread
        if( proj.shot_spread > 0 ) {
            double sample = rng_normal( -proj.shot_spread, proj.shot_spread );
            sample = sample >= 0 ? sample : -sample;
            units::angle arc = std::min( units::from_arcmin( sample ), 30_degrees );
            spread = tan( arc / 2 ) * 2.0;
            double max_spread = spread * ( t_copy.size() - 1 );
            if( max_spread >= 1.0 ) {
                double dx = target_c.x() - source.x();
                double dy = target_c.y() - source.y();
                units::angle d = units::atan2( dy, dx );
                d += ( one_in( 2 ) ? 1 : -1 ) * arc;
                target_c.x() = std::clamp( source.x() + roll_remainder( range * cos( d ) ),
                                           0, MAPSIZE_X - 1 );
                target_c.y() = std::clamp( source.y() + roll_remainder( range * sin( d ) ),
                                           0, MAPSIZE_Y - 1 );
                if( target_c == source ) {
                    target_c.x() = source.x() + sgn( dx );
                    target_c.y() = source.y() + sgn( dy );
                }
                t_copy = here->find_clear_path( first_p, target_c );
                // point-blank tile should be the same
                t_copy.insert( t_copy.begin(), first_p );
                t_copy.insert( t_copy.begin(), source );
                if( !no_overshoot && range < extend_to_range ) {
                    std::vector<tripoint_bub_ms> extension = continue_line( t_copy,
                            extend_to_range - range );
                    t_copy.reserve( t_copy.size() + extension.size() );
                    t_copy.insert( t_copy.end(), extension.begin(), extension.end() );
                }
            }
            add_msg_debug( debugmode::DF_BALLISTIC,
                           "shot_spread: %d; spread roll/max_spread: %.2f/%.2f; target (orig/hit): %s/%s",
                           proj.shot_spread, spread, max_spread,
                           target_arg.to_string_writable(), target_c.to_string_writable() );
        }
        // Range can be 0
        size_t traj_len = t_copy.size();
        while( traj_len > 0 && rl_dist( source, t_copy[traj_len - 1] ) > proj_arg.range ) {
            --traj_len;
        }

        const float projectile_skip_multiplier = 0.1f;
        // Randomize the skip so that bursts look nicer
        int projectile_skip_calculation = range * projectile_skip_multiplier;
        int projectile_skip_current_frame = rng( 0, projectile_skip_calculation );
        bool has_momentum = true;

        for( size_t i = 1; i < traj_len && ( has_momentum || stream ); ++i ) {
            tp = t_copy[i];
            if( !here->inbounds( tp ) ) {
                debugmsg( "Shot along %s out-of-bounds", tp.to_string() );
                break;
            }
            int distance = rl_dist( source, tp );
            // no spread at point-blank, skip point-blank calculate
            if( !first && distance <= 1 ) {
                prev_point = tp;
                proj.shot_impact.mult_damage( point_blank_rescale );
                continue;
            }

            if( tp.z() != prev_point.z() ) {
                tripoint_bub_ms floor1 = prev_point;
                tripoint_bub_ms floor2 = tp;

                if( floor1.z() < floor2.z() ) {
                    floor1 += tripoint::above;
                } else {
                    floor2 += tripoint::above;
                }
                // We only stop the bullet if there are two floors in a row
                // this allow the shooter to shoot adjacent enemies from rooftops.
                if( here->has_floor_or_water( floor1 ) && here->has_floor_or_water( floor2 ) ) {
                    // Currently strictly no shooting through floor
                    // TODO: Bash the floor
                    tp = prev_point;
                    traj_len = --i;
                    break;
                }
            }

            // Drawing the bullet uses player g->u, and not player p, because it's drawn
            // relative to YOUR position, which may not be the gunman's position.
            if( first && do_animation && !do_draw_line ) {
                // TODO: Make this draw thrown item/launched grenade/arrow
                if( projectile_skip_current_frame >= projectile_skip_calculation ) {
                    // TODO: Refine so overlapping maps are handled.
                    if( here == &reality_bubble() ) {
                        g->draw_bullet( tp, static_cast<int>( i ), trajectory, bullet );
                    }
                    projectile_skip_current_frame = 0;
                    // If we missed recalculate the skip factor so they spread out.
                    projectile_skip_calculation =
                        std::max( static_cast<size_t>( range ), i ) * projectile_skip_multiplier;
                } else {
                    projectile_skip_current_frame++;
                }
            }

            if( in_veh != nullptr ) {
                const optional_vpart_position other = here->veh_at( tp );
                if( in_veh == veh_pointer_or_null( other ) && other->is_inside() ) {
                    // Turret is on the roof and can't hit anything inside
                    continue;
                }
            }

            Creature *critter = creatures.creature_at( tp );
            if( origin == critter ) {
                // No hitting self with "weird" attacks.
                critter = nullptr;
            }

            monster *mon = dynamic_cast<monster *>( critter );
            // ignore non-point-blank digging targets (since they are underground)
            if( mon != nullptr && mon->digging() &&
                distance > 1 ) {
                critter = nullptr;
                mon = nullptr;
            }

            // Reset hit critter from the last iteration
            attack.last_hit_critter = nullptr;

            // If we shot us a monster...
            // TODO: add size effects to accuracy
            // If there's a monster in the path of our bullet, and either our aim was true,
            //  OR it's not the monster we were aiming at and we were lucky enough to hit it
            double cur_missed_by = std::min( 1.0, aim.missed_by + spread * std::max( distance - 1, 0 ) );

            // unintentional hit on something other than our actual target
            // don't re-roll for the actual target, we already decided on a missed_by value for that
            // at the start, misses should stay as misses
            if( critter != nullptr && tp != target_arg ) {
                // Unintentional hit
                cur_missed_by = std::max( rng_float( 0.1, 1.5 - aim.missed_by ) /
                                          critter->ranged_target_size(), 0.4 );
            }
            // If the attack is shot, once we're past point-blank,
            // don't print normal hit msg.
            if( first && proj.count > 1 && distance > 1 ) {
                multishot = true;
                proj.multishot = true;
                print_messages = false;
            }

            if( critter != nullptr && cur_missed_by < 1.0 ) {
                // Prevent shots from hitting underwater creatures that are beneath surface (except thin ice);
                // treat the shot as hitting the terrain instead.
                if( critter->is_underwater() &&
                    here->has_flag( ter_furn_flag::TFLAG_SWIM_UNDER, critter->pos_bub() ) &&
                    !here->has_flag( ter_furn_flag::TFLAG_THIN_ICE, critter->pos_bub() ) ) {
                    double it = here->shoot( tp, proj, !no_item_damage && tp == target_c );
                    if( it > 0 ) {
                        if( proj.count > 1 ) {
                            proj.shot_impact.mult_damage( it );
                            if( first && distance <= 1 ) {
                                point_blank_rescale = it;
                            }
                        } else {
                            proj.impact.mult_damage( it );
                        }
                    }
                    if( origin != nullptr && origin->is_avatar() ) {
                        add_msg( m_info, _( "Your shot is blocked by the surface." ) );
                    }
                    continue;
                }
                if( in_veh != nullptr && veh_pointer_or_null( here->veh_at( tp ) ) == in_veh &&
                    critter->is_avatar() ) {
                    // Turret either was aimed by the player (who is now ducking) and shoots from above
                    // Or was just IFFing, giving lots of warnings and time to get out of the line of fire
                    continue;
                }
                // avoid friendly fire
                if( critter->attitude_to( *origin ) == Creature::Attitude::FRIENDLY &&
                    origin->check_avoid_friendly_fire() ) {
                    continue;
                }
                critter->deal_projectile_attack( here, null_source ? nullptr : origin, attack, cur_missed_by,
                                                 print_messages, wp_attack );

                if( critter->is_npc() ) {
                    critter->as_npc()->on_attacked( *origin );
                }

                // Critter can still dodge the projectile
                // In this case hit_critter won't be set
                if( attack.last_hit_critter != nullptr ) {
                    const field_type_id blood_type = critter->bloodType();
                    if( blood_type ) {
                        const size_t bt_len = blood_trail_len( attack.dealt_dam.total_damage() );
                        if( bt_len > 0 ) {
                            const tripoint_bub_ms &dest = move_along_line( tp, t_copy, bt_len );
                            here->add_splatter_trail( blood_type, tp, dest );
                        }
                    }
                    // TODO: Refine to account for overlapping maps
                    if( here == &reality_bubble() ) {
                        sfx::do_projectile_hit( *attack.last_hit_critter );
                    }
                    // If the projectile has excess penetration, and would one-shot the enemy, make it continue through the trajectory.
                    // TODO: Add creature armor and armor penetration to condition.
                    // TODO: Extend trajectory for targeted monster being first monster hit.
                    // TODO: Take into account creature physicality, in a more realistic way.
                    if( proj.proj_effects.count( ammo_effect_EXCESS_PEN ) &&
                        ( attack.dealt_dam.total_damage() >= attack.last_hit_critter->get_hp_max() ) ) {
                        // Reduces the projectile's damage based on the creature's size
                        // Numbers are roughly based on civil war journals, where a cannon shot would penetrate through about 3-4 people.
                        double size_mult =
                            0.8 * ( 1 / ( static_cast<double>( attack.last_hit_critter->get_size() ) ) );
                        proj.shot_impact.mult_damage( size_mult, true );
                    } else {
                        has_momentum = false;
                    }
                    // on-hit effects for inflicted damage types
                    for( const std::pair<const damage_type_id, int> &dt : attack.dealt_dam.dealt_dams ) {
                        dt.first->onhit_effects( origin, attack.last_hit_critter );
                    }
                }
            } else if( in_veh != nullptr && veh_pointer_or_null( here->veh_at( tp ) ) == in_veh ) {
                // Don't do anything, especially don't call map::shoot as this would damage the vehicle
            } else {
                double it = here->shoot( tp, proj, !no_item_damage && tp == target_c );
                if( it > 0 ) {
                    // even if it's a shot, as we will never use impact after point-blank,
                    // we should always tweak shot_impact instead of impact.
                    if( proj.count > 1 ) {
                        proj.shot_impact.mult_damage( it );
                        if( first && distance <= 1 ) {
                            point_blank_rescale = it;
                        }
                    } else {
                        proj.impact.mult_damage( it );
                    }
                } else {
                    has_momentum = false;
                }
            }
            if( ( !has_momentum || traj_len == size_t( 2 ) ) && proj.count > 1 && distance <= 1 ) {
                // Track that we hit an obstacle while wadded up,
                // to cancel out of applying the other projectiles.
                proj.count = 1;
            }

            if( ( !has_momentum || !is_bullet ) && here->impassable( tp ) ) {
                // Don't let flamethrowers go through walls
                // TODO: Let them go through bars
                traj_len = i;
                break;
            }
            prev_point = tp;
        }
        // Done with the trajectory!
        if( first && do_animation && do_draw_line && traj_len > 2 ) {
            t_copy.erase( t_copy.begin() );
            t_copy.resize( traj_len-- );
            g->draw_line( tp, t_copy );
            g->draw_bullet( tp, static_cast<int>( traj_len-- ), t_copy, bullet );
        }

        if( here->impassable( tp ) ) {
            tp = prev_point;
        }

        drop_or_embed_projectile( here, attack, proj );

        int dealt_damage = attack.dealt_dam.total_damage();
        apply_ammo_effects( null_source ? nullptr : origin, tp, proj.proj_effects, dealt_damage );
        const explosion_data &expl = proj.get_custom_explosion();
        if( expl.power > 0.0f ) {
            explosion_handler::explosion( null_source ? nullptr : origin, here, tp,
                                          proj.get_custom_explosion() );
        }
        first = false;
    }

    // TODO: Move this outside now that we have hit point in return values?
    if( proj.proj_effects.count( ammo_effect_BOUNCE ) ) {
        Creature *mon_ptr = g->get_creature_if( [&]( const Creature & z ) {
            if( &z == origin ) {
                return false;
            }
            // search for creatures in radius 4 around impact site
            if( rl_dist( z.pos_bub( *here ), tp ) <= 4 &&
                here->sees( z.pos_bub( *here ), tp, -1, true, los_trace::physical ) ) {
                // don't hit targets that have already been hit
                for( auto it : attack.targets_hit ) {
                    if( &z == it.first ) {
                        return false;
                    }
                }
                return true;
            }
            return false;
        } );
        if( mon_ptr ) {
            Creature &z = *mon_ptr;
            attack.targets_hit[&z].first += 0;
            add_msg( _( "The attack bounced to %s!" ), z.get_name() );
            projectile_attack( attack, proj, here, tp, z.pos_bub(), dispersion, origin, in_veh );
            // TODO: Refine to handle overlapping maps
            if( here == &reality_bubble() ) {
                sfx::play_variant_sound( "fire_gun", "bio_lightning_tail",
                                         sfx::get_heard_volume( z.pos_bub() ), sfx::get_heard_angle( z.pos_bub() ) );
            }
        }
    }
}

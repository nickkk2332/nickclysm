#include "grapple.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "dialogue.h"
#include "effect.h"
#include "effect_on_condition.h"
#include "effect_source.h"
#include "enums.h"
#include "game.h"
#include "game_constants.h"
#include "item_location.h"
#include "map.h"
#include "mapdata.h"
#include "math_parser_jmath.h"
#include "mattack_actors.h"
#include "mattack_common.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "point.h"
#include "rng.h"
#include "string_formatter.h"
#include "talker.h"
#include "translations.h"
#include "weighted_list.h"

static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );

static const jmath_func_id jmath_func_grapple_grab_strength( "grapple_grab_strength" );

// Finding a monster grabber, and how strong a character grabs

namespace
{

// Evaluate a grappling formula with u = grabber and n = target.
double eval_formula( const jmath_func_id &func, const Creature &grabber, const Creature &target,
                     const std::vector<double> &params = {} )
{
    const_dialogue d( get_const_talker_for( grabber ), get_const_talker_for( target ) );
    return params.empty() ? func->eval( d ) : func->eval( d, params );
}

} // namespace

int grapple::monster_grab_reach( const monster &mon )
{
    int reach = 0;
    for( const auto &special : mon.type->special_attacks ) {
        const melee_actor *const actor = dynamic_cast<const melee_actor *>( special.second.get() );
        if( actor != nullptr && actor->is_grab ) {
            reach = std::max( reach, actor->range );
        }
    }
    return reach;
}

monster *grapple::monster_grabber( const Creature &victim, const bodypart_id &bp )
{
    // A character victim's grabs are per limb; a monster victim has one grab on the null part.
    const bool limb_grab = !victim.is_monster();
    const tripoint_abs_ms victim_pos = victim.pos_abs();
    monster *best = nullptr;
    int best_dist = INT_MAX;
    bool best_targets_victim = false;
    for( monster &mon : g->all_monsters() ) {
        if( &mon == &victim || mon.is_dead() ||
            !mon.has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
            continue;
        }
        if( limb_grab && !mon.is_grabbing( bp.id() ) ) {
            continue;
        }
        const tripoint_abs_ms mon_pos = mon.pos_abs();
        if( std::abs( mon_pos.z() - victim_pos.z() ) > 1 ) {
            continue;
        }
        // Grab attacks reach past the adjacent tiles, and pulls and drags can end a z-level
        // away (CHAR-B09).
        const int dist = rl_dist( mon_pos.xy(), victim_pos.xy() );
        if( dist < 1 || dist > std::max( 1, monster_grab_reach( mon ) ) ) {
            continue;
        }
        const bool targets_victim = mon.attack_target() == &victim;
        if( best == nullptr || ( targets_victim && !best_targets_victim ) ||
            ( targets_victim == best_targets_victim && dist < best_dist ) ) {
            best = &mon;
            best_dist = dist;
            best_targets_victim = targets_victim;
        }
    }
    return best;
}

int grapple::grab_strength( const Character &grabber, const Creature &target )
{
    const double strength = eval_formula( jmath_func_grapple_grab_strength, grabber, target );
    return std::clamp( static_cast<int>( std::round( strength ) ), 1, 100 );
}

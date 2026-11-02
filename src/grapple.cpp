#include "grapple.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bionics.h"
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
#include "field_type.h"
#include "game.h"
#include "game_constants.h"
#include "item_location.h"
#include "line.h"
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
#include "text_snippets.h"
#include "tileray.h"
#include "translation.h"
#include "translations.h"
#include "units.h"
#include "veh_type.h"
#include "vpart_position.h"
#include "weighted_list.h"

static const bionic_id bio_railgun( "bio_railgun" );

static const character_modifier_id
character_modifier_melee_stamina_cost_mod( "melee_stamina_cost_mod" );

static const efftype_id effect_cramped_space( "cramped_space" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_harnessed( "harnessed" );
static const efftype_id effect_incorporeal( "incorporeal" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_winded( "winded" );

static const effect_on_condition_id effect_on_condition_grab_condition( "grab_condition" );

static const flag_id json_flag_CANNOT_MOVE( "CANNOT_MOVE" );
static const flag_id json_flag_GRAB( "GRAB" );
static const flag_id json_flag_GRAB_FILTER( "GRAB_FILTER" );
static const flag_id json_flag_NO_GRAB( "NO_GRAB" );

static const jmath_func_id jmath_func_grapple_drag_odds( "grapple_drag_odds" );
static const jmath_func_id jmath_func_grapple_escape_stamina_cost( "grapple_escape_stamina_cost" );
static const jmath_func_id jmath_func_grapple_grab_strength( "grapple_grab_strength" );
static const jmath_func_id jmath_func_grapple_min_hold_turns( "grapple_min_hold_turns" );
static const jmath_func_id jmath_func_grapple_throw_force( "grapple_throw_force" );
static const jmath_func_id
jmath_func_grapple_throw_stamina_cost( "grapple_throw_stamina_cost" );
static const jmath_func_id jmath_func_grapple_throw_verb( "grapple_throw_verb" );

static const species_id species_CYBORG( "CYBORG" );
static const species_id species_ROBOT( "ROBOT" );
static const species_id species_ROBOT_FLYING( "ROBOT_FLYING" );

static const trait_id trait_CLAWS_TENTACLE( "CLAWS_TENTACLE" );
static const trait_id trait_PAWS_LARGE( "PAWS_LARGE" );
static const trait_id trait_THRESH_FISH( "THRESH_FISH" );

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

// Holding a creature

namespace
{

bool sourced_from( const effect &eff, const Character &grabber )
{
    const std::optional<character_id> source = eff.get_source().get_character_id();
    return source && source->is_valid() && *source == grabber.getID();
}

} // namespace

bool Character::can_grapple_while_wielding() const
{
    // TODO: when the martial-arts track lands, also allow a weapon that the active style lists in
    // its grapple_weapons.
    return !is_armed();
}

Creature *Character::grabbed_creature() const
{
    if( !has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
        return nullptr;
    }
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &p : here.points_in_radius( pos_bub( here ), 1, 1 ) ) {
        Creature *const victim = creatures.creature_at( p );
        if( victim != nullptr && victim != this && !victim->is_dead_state() &&
            grapple::in_reach( *this, *victim ) &&
            grapple::hold_effect( *this, *victim ) != nullptr ) {
            return victim;
        }
    }
    return nullptr;
}

bool Character::is_grabbing_creature() const
{
    return grabbed_creature() != nullptr;
}

void Character::release_grapple()
{
    Creature *const victim = grabbed_creature();
    if( victim != nullptr ) {
        add_msg_player_or_npc( _( "You release %s." ), _( "<npcname> releases %s." ),
                               victim->disp_name() );
    }
    grapple::end_hold( *this, victim );
}

bool grapple::in_reach( const Creature &grabber, const Creature &victim )
{
    const tripoint_abs_ms a = grabber.pos_abs();
    const tripoint_abs_ms b = victim.pos_abs();
    return square_dist( a.xy(), b.xy() ) == 1 && std::abs( a.z() - b.z() ) <= 1;
}

const effect *grapple::hold_effect( const Character &grabber, const Creature &victim )
{
    if( !grabber.getID().is_valid() ) {
        return nullptr;
    }
    for( const effect &eff : victim.get_effects_with_flag( json_flag_GRAB ) ) {
        if( sourced_from( eff, grabber ) ) {
            return &eff;
        }
    }
    return nullptr;
}

Character *grapple::character_grabber( const Creature &victim, const effect &eff )
{
    const std::optional<character_id> source = eff.get_source().get_character_id();
    if( !source || !source->is_valid() ) {
        return nullptr;
    }
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    for( const tripoint_bub_ms &p : here.points_in_radius( victim.pos_bub( here ), 1, 1 ) ) {
        Character *const guy = creatures.creature_at<Character>( p );
        if( guy == nullptr || guy == &victim || guy->getID() != *source ) {
            continue;
        }
        if( guy->is_dead_state() || !guy->has_effect_with_flag( json_flag_GRAB_FILTER ) ||
            !in_reach( *guy, victim ) ) {
            return nullptr;
        }
        return guy;
    }
    return nullptr;
}

bool grapple::start_hold( Character &grabber, Creature &victim, int strength,
                          const bodypart_id &bp )
{
    // add_effect would merge into an existing grab and keep its source, so refuse instead.
    if( victim.has_effect( effect_grabbed, bp ) ) {
        return false;
    }
    victim.add_effect( effect_source( &grabber ), effect_grabbed, 1_days, bp, true,
                       std::clamp( strength, 1, 100 ) );
    if( hold_effect( grabber, victim ) == nullptr ) {
        return false;
    }
    grabber.add_effect( effect_grabbing, 1_days, true, 1 );
    return true;
}

void grapple::end_hold( Character &grabber, Creature *victim )
{
    if( victim != nullptr ) {
        std::vector<std::pair<efftype_id, bodypart_id>> held;
        for( const effect &eff : victim->get_effects_with_flag( json_flag_GRAB ) ) {
            if( sourced_from( eff, grabber ) ) {
                held.emplace_back( eff.get_id(), eff.get_bp() );
            }
        }
        for( const std::pair<efftype_id, bodypart_id> &grab : held ) {
            victim->remove_effect( grab.first, grab.second );
        }
    }
    std::vector<efftype_id> filters;
    for( const effect &eff : grabber.get_effects_with_flag( json_flag_GRAB_FILTER ) ) {
        filters.push_back( eff.get_id() );
    }
    for( const efftype_id &id : filters ) {
        grabber.remove_effect( id );
    }
}

void grapple::release_holds_on( Creature &victim )
{
    std::vector<Character *> grabbers;
    for( const effect &eff : victim.get_effects_with_flag( json_flag_GRAB ) ) {
        if( Character *const guy = character_grabber( victim, eff ) ) {
            grabbers.push_back( guy );
        }
    }
    for( Character *guy : grabbers ) {
        end_hold( *guy, &victim );
    }
}

void grapple::release_all( Creature &c )
{
    if( Character *const guy = c.as_character() ) {
        if( guy->has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
            end_hold( *guy, guy->grabbed_creature() );
        }
    }
    release_holds_on( c );
}

void grapple::break_hold( Creature &victim, const efftype_id &id, const bodypart_id &bp )
{
    if( !victim.has_effect( id, bp ) ) {
        return;
    }
    if( Character *const guy = character_grabber( victim, victim.get_effect( id, bp ) ) ) {
        end_hold( *guy, &victim );
        return;
    }
    // A monster's grab on a character is per limb; on a monster, its persistence check
    // notices that the grab is gone.
    if( !victim.is_monster() ) {
        if( monster *const mon = monster_grabber( victim, bp ) ) {
            mon->remove_grab( bp.id() );
        }
    }
    victim.remove_effect( id, bp );
}

void grapple::release_grabs_by( monster &mon )
{
    if( !mon.has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
        return;
    }
    map &here = get_map();
    creature_tracker &creatures = get_creature_tracker();
    const int reach = std::max( 1, monster_grab_reach( mon ) );
    for( const tripoint_bub_ms &p : here.points_in_radius( mon.pos_bub( here ), reach, 1 ) ) {
        Creature *const victim = creatures.creature_at( p );
        if( victim == nullptr || victim == &mon ) {
            continue;
        }
        std::vector<std::pair<efftype_id, bodypart_id>> held;
        for( const effect &eff : victim->get_effects_with_flag( json_flag_GRAB ) ) {
            if( character_grabber( *victim, eff ) == nullptr &&
                monster_grabber( *victim, eff.get_bp() ) == &mon ) {
                held.emplace_back( eff.get_id(), eff.get_bp() );
            }
        }
        for( const std::pair<efftype_id, bodypart_id> &grab : held ) {
            victim->remove_effect( grab.first, grab.second );
        }
    }
    const std::unordered_set<bodypart_str_id> limbs = mon.grabbed_limbs;
    for( const bodypart_str_id &bp : limbs ) {
        mon.remove_grab( bp );
    }
    std::vector<efftype_id> filters;
    for( const effect &eff : mon.get_effects_with_flag( json_flag_GRAB_FILTER ) ) {
        filters.push_back( eff.get_id() );
    }
    for( const efftype_id &id : filters ) {
        mon.remove_effect( id );
    }
}

int grapple::stamina_floor()
{
    return get_option<int>( "GRAPPLE_STAMINA_FLOOR" );
}

void grapple::process_turn( Character &grabber )
{
    if( !grabber.has_effect_with_flag( json_flag_GRAB_FILTER ) ) {
        return;
    }
    Creature *const victim = grabber.grabbed_creature();
    if( victim == nullptr ) {
        // The victim moved away, was removed from the map, or something else took it from us:
        // drop our side.
        add_msg_debug( debugmode::DF_CHARACTER, "Orphan grabbing effect removed from %s.",
                       grabber.disp_name() );
        grabber.add_msg_if_player( m_info, _( "You lose your hold." ) );
        end_hold( grabber, nullptr );
        return;
    }
    if( grabber.has_effect( effect_incorporeal ) || victim->has_effect( effect_incorporeal ) ||
        grabber.has_effect( effect_winded ) || grabber.in_sleep_state() ) {
        grabber.release_grapple();
        return;
    }
    if( grabber.get_stamina() < stamina_floor() ) {
        grabber.add_msg_if_player( m_bad, _( "You're too exhausted to maintain your hold." ) );
        end_hold( grabber, victim );
    }
}

// The grab action

namespace
{

// Every body part a grab can take, mapped to its main part and weighted by hit size like
// Creature::random_body_part( true ). Parts somebody already holds are left out.
weighted_float_list<bodypart_id> grab_part_weights( const Creature &target )
{
    weighted_float_list<bodypart_id> weights;
    for( const bodypart_id &bp : target.get_all_body_parts() ) {
        const bodypart_id main_part = bp->main_part.id();
        if( !target.has_effect_with_flag( json_flag_GRAB, main_part ) ) {
            weights.add( main_part, bp->hit_size );
        }
    }
    return weights;
}

} // namespace

int grapple::arm_stamina_cost()
{
    return get_option<int>( "GRAPPLE_ARM_STAMINA_COST" );
}

std::optional<bodypart_id> grapple::free_grab_part( const Creature &target )
{
    const weighted_float_list<bodypart_id> weights = grab_part_weights( target );
    const bodypart_id *const picked = weights.pick();
    if( picked == nullptr ) {
        return std::nullopt;
    }
    return *picked;
}

std::optional<std::string> grapple::grab_refusal( const Character &grabber,
        const Creature &target )
{
    map &here = get_map();
    if( &target == &grabber ) {
        return std::string( _( "You get a hold of yourself." ) );
    }
    if( !in_reach( grabber, target ) ) {
        return string_format( _( "You can't reach %s." ), target.disp_name() );
    }
    if( grabber.is_underwater() != target.is_underwater() &&
        ( here.has_flag( ter_furn_flag::TFLAG_SWIM_UNDER, grabber.pos_bub( here ) ) ||
          here.has_flag( ter_furn_flag::TFLAG_SWIM_UNDER, target.pos_bub( here ) ) ) ) {
        return std::string( _( "You can't grab through the floor." ) );
    }
    if( !grabber.can_grapple_while_wielding() ) {
        return std::string( _( "You'll need your hands free for grappling." ) );
    }
    if( grabber.get_working_arm_count() < 1 || grabber.has_flag( json_flag_NO_GRAB ) ) {
        return std::string( _( "You can't manage that in your current condition." ) );
    }
    if( grabber.get_stamina() < stamina_floor() ) {
        return std::string( _( "You're too exhausted to wrestle anything." ) );
    }
    const monster *const mon = target.as_monster();
    if( mon != nullptr && mon->has_flag( mon_flag_GRAB_IMMUNE ) ) {
        return string_format( _( "You can't grab %s." ), target.disp_name() );
    }
    if( const Creature *const held = grabber.grabbed_creature() ) {
        return string_format( _( "You're already holding %s." ), held->disp_name() );
    }
    // Holding a creature and holding furniture or a vehicle are exclusive.
    const avatar *const you = grabber.as_avatar();
    if( you != nullptr && you->get_grab_type() != object_type::NONE ) {
        return std::string( _( "You're already holding on to something." ) );
    }
    const_dialogue d( get_const_talker_for( grabber ), get_const_talker_for( target ) );
    if( !effect_on_condition_grab_condition->test_condition( d ) ) {
        return string_format( _( "You can't get a grip on %s." ), target.disp_name() );
    }
    if( mon == nullptr && grab_part_weights( target ).get_weight() <= 0 ) {
        return string_format( _( "There's nowhere left to grab on %s." ), target.disp_name() );
    }
    return std::nullopt;
}

bool grapple::grab_tolerated( const Character &grabber, const Creature &target )
{
    // These rules are written from the avatar's side. Other grabbers always roll.
    if( !grabber.is_avatar() ) {
        return false;
    }
    // Friends tolerate a grab and enemies are already angry. Neutral NPCs only put up with it
    // when they're helpless.
    if( const npc *const guy = target.as_npc() ) {
        return guy->is_player_ally() || guy->is_enemy() || guy->has_effect( effect_narcosis ) ||
               !guy->enough_working_legs();
    }
    // You can grab your own pets.
    if( const monster *const mon = target.as_monster() ) {
        return mon->is_pet();
    }
    return false;
}

grapple::grab_confirmation grapple::needs_confirmation( const Character &grabber,
        const Creature &target )
{
    if( !grabber.is_avatar() || grab_tolerated( grabber, target ) ) {
        return grab_confirmation::none;
    }
    if( target.is_npc() ) {
        return grab_confirmation::attack_npc;
    }
    const monster *const mon = target.as_monster();
    if( mon != nullptr && mon->attitude_to( grabber ) != Creature::Attitude::HOSTILE ) {
        return grab_confirmation::anger_faction;
    }
    return grab_confirmation::none;
}

bool grapple::attempt_grab( Character &grabber, Creature &target )
{
    map &here = get_map();
    const bool tolerate = grab_tolerated( grabber, target );
    const int strength = grab_strength( grabber, target );

    // Grabbing on is lighter work than striking: weariness is taken at the grappling level.
    const float weary_mult = grabber.exertion_adjusted_move_multiplier( exertion_level() );
    grabber.mod_moves( -100 - static_cast<int>( grabber.attack_speed( null_item_reference() ) /
                       weary_mult ) );
    grabber.burn_energy_arms( -arm_stamina_cost() );

    if( monster *const z = target.as_monster() ) {
        float accuracy_penalty = 0.0f;
        if( z->has_flag( mon_flag_AQUATIC ) ) {
            const tripoint_bub_ms pos = grabber.pos_bub( here );
            const bool in_water = here.has_flag( ter_furn_flag::TFLAG_SWIMMABLE, pos ) ||
                                  here.has_flag( ter_furn_flag::TFLAG_DEEP_WATER, pos );
            const bool hand_fisher = grabber.has_trait( trait_PAWS_LARGE ) ||
                                     grabber.has_trait( trait_CLAWS_TENTACLE ) ||
                                     ( grabber.has_trait( trait_THRESH_FISH ) && in_water );
            if( !hand_fisher ) {
                if( one_in( 5 ) ) {
                    grabber.add_msg_if_player( m_warning,
                                               _( "Grabbing onto %s is particularly difficult." ),
                                               z->disp_name() );
                }
                accuracy_penalty = -8.0f;
            }
        }
        const int hitspread = z->deal_melee_attack( &grabber,
                              static_cast<int>( grabber.hit_roll() + accuracy_penalty ) );
        if( hitspread < 0 && !tolerate ) {
            grabber.add_msg_player_or_npc(
                m_warning, _( "You reach for %s, but fail to make contact!" ),
                _( "<npcname> reaches for %s, but fails to make contact!" ), z->disp_name() );
            return false;
        }
        // Someone else already holds it. Pull it free as an exclusive monster grab would, or fail.
        if( z->has_effect_with_flag( json_flag_GRAB ) ) {
            int held_strength = 0;
            std::vector<std::pair<efftype_id, bodypart_id>> held;
            for( const effect &eff : z->get_effects_with_flag( json_flag_GRAB ) ) {
                held_strength = std::max( held_strength, eff.get_intensity() );
                held.emplace_back( eff.get_id(), eff.get_bp() );
            }
            if( !x_in_y( strength / 2.0f, held_strength ) ) {
                grabber.add_msg_if_player( m_bad,
                                           _( "%s is held too tightly for you to get a grip." ),
                                           z->disp_name( true ) );
                return false;
            }
            for( const std::pair<efftype_id, bodypart_id> &grab : held ) {
                z->remove_effect( grab.first, grab.second );
            }
            grabber.add_msg_if_player( m_good, _( "You wrest %s from another hold." ),
                                       z->disp_name() );
        }
        if( rng( 0, 100 ) <= static_cast<int>( z->type->def_chance ) ) {
            z->type->sp_defense( *z, &grabber, nullptr );
        }
        if( z->is_dead_state() || grabber.is_dead_state() || !in_reach( grabber, *z ) ||
            !start_hold( grabber, *z, strength, bodypart_str_id::NULL_ID().id() ) ) {
            grabber.add_msg_if_player( m_bad, _( "You can't get a grip on %s." ), z->disp_name() );
            return false;
        }
        grabber.add_msg_player_or_npc( m_good, _( "You grab %s." ), _( "<npcname> grabs %s." ),
                                       z->disp_name() );
        return true;
    }

    npc *const guy = target.as_npc();
    if( !tolerate ) {
        const int hitspread = target.deal_melee_attack( &grabber,
                              static_cast<int>( grabber.hit_roll() ) );
        if( hitspread < 0 ) {
            grabber.add_msg_player_or_npc(
                m_warning, _( "You reach for %s, but fail to make contact!" ),
                _( "<npcname> reaches for %s, but fails to make contact!" ), target.disp_name() );
            if( guy != nullptr ) {
                guy->on_attacked( grabber );
            }
            return false;
        }
    }
    // Characters are held by a limb. Skip the limbs somebody already holds.
    const std::optional<bodypart_id> bp = free_grab_part( target );
    if( !bp || !start_hold( grabber, target, strength, *bp ) ) {
        grabber.add_msg_if_player( m_bad, _( "You can't get a grip on %s." ), target.disp_name() );
        return false;
    }
    //~ %1$s: the creature grabbed, %2$s: its body part
    grabber.add_msg_player_or_npc( m_good, _( "You grab %1$s by the %2$s." ),
                                   _( "<npcname> grabs %1$s by the %2$s." ), target.disp_name(),
                                   body_part_name( *bp ) );
    if( guy != nullptr && !tolerate ) {
        guy->on_attacked( grabber );
    }
    return true;
}

// Struggling free

int grapple::escape_stamina_cost( const Character &grabber, const Creature &target,
                                  int hold_strength, int escape_skill )
{
    const double cost = eval_formula( jmath_func_grapple_escape_stamina_cost, grabber, target, {
        static_cast<double>( hold_strength ), static_cast<double>( escape_skill )
    } );
    return std::max( 0, static_cast<int>( std::round( cost ) ) );
}

int grapple::min_hold_turns( const Character &grabber, const Creature &target )
{
    const double turns = eval_formula( jmath_func_grapple_min_hold_turns, grabber, target );
    return std::max( 1, static_cast<int>( std::round( turns ) ) );
}

bool grapple::within_min_hold( const Character &grabber, const Creature &target,
                               const effect &eff )
{
    // Turn order would otherwise let a creature break a character's grab on the turn it lands.
    const int held_turns = to_turns<int>( calendar::turn - eff.get_start_time() );
    return held_turns <= min_hold_turns( grabber, target );
}

// Exertion

float grapple::exertion_level()
{
    // TLG uses EXPLOSIVE_EXERCISE; the optional exertion-level track decides whether we do too.
    return EXTRA_EXERCISE;
}

// Dragging

double grapple::drag_chance( const Character &grabber, const Creature &victim )
{
    const effect *const hold = hold_effect( grabber, victim );
    const int strength = hold != nullptr ? hold->get_intensity() : grab_strength( grabber, victim );
    // An awake ally, or an NPC walking with us, moves along and makes it easier: TLG's discount
    // by its own move cost. TLG read the grabber's move cost there; it is the ally's that counts.
    int helper_move_cost = 0;
    if( const npc *const guy = victim.as_npc() ) {
        if( ( guy->is_walking_with() || guy->is_player_ally() ) && !guy->in_sleep_state() ) {
            helper_move_cost = std::max( 1, guy->run_cost( 100, false ) );
        }
    }
    // TLG meant to halve the odds for a pet, but tested the grabber for being one.
    const monster *const mon = victim.as_monster();
    const bool pet = mon != nullptr && mon->is_pet();
    const double chance = eval_formula( jmath_func_grapple_drag_odds, grabber, victim, {
        static_cast<double>( strength ), units::to_kilogram( victim.get_weight() ),
        static_cast<double>( helper_move_cost ), pet ? 1.0 : 0.0
    } );
    return std::clamp( chance, 0.0, 1.0 );
}

std::optional<std::string> grapple::drag_refusal( const Character &grabber,
        const Creature &victim, const tripoint_bub_ms &from, const tripoint_bub_ms &to )
{
    map &here = get_map();
    const monster *const mon = victim.as_monster();
    if( victim.has_effect( effect_tied ) || victim.has_effect( effect_harnessed ) ||
        victim.has_effect( effect_ridden ) || victim.has_flag( json_flag_CANNOT_MOVE ) ||
        ( mon != nullptr && mon->has_flag( mon_flag_IMMOBILE ) ) ) {
        return string_format( _( "%s is immobile and cannot be moved." ),
                              victim.disp_name( true ) );
    }
    // Walking into a held pet swaps places with it: that needs no drag.
    const Creature *const in_the_way = get_creature_tracker().creature_at( to );
    if( in_the_way != nullptr && in_the_way != &victim && in_the_way != &grabber ) {
        //~ %1$s: the creature in the way, %2$s: the creature held
        return string_format( _( "You can't push past %1$s while holding %2$s." ),
                              in_the_way->disp_name(), victim.disp_name() );
    }
    // A hold across a ramp: the step back up or down would need a ramp of its own.
    const tripoint_bub_ms at = victim.pos_bub( here );
    if( at.z() != from.z() ) {
        return string_format( _( "You can't drag %s up or down from there." ), victim.disp_name() );
    }
    const std::vector<field_type_id> fields = here.get_impassable_field_type_ids_at( from );
    if( !here.passable( from ) || ( !fields.empty() && !victim.is_immune_fields( fields ) ) ||
        ( here.has_flag_ter( ter_furn_flag::TFLAG_SMALL_PASSAGE, from ) &&
          victim.get_size() > creature_size::medium ) ||
        ( mon != nullptr && mon->is_aquatic_danger( from ) ) ) {
        return string_format( _( "You can't drag %s there." ), victim.disp_name() );
    }
    return std::nullopt;
}

bool grapple::drag_to( Creature &victim, const tripoint_abs_ms &dest )
{
    map &here = get_map();
    const tripoint_bub_ms to = here.get_bub( dest );
    const Creature *const there = get_creature_tracker().creature_at( dest, true );
    if( there != nullptr && there != &victim ) {
        return false;
    }
    if( monster *const mon = victim.as_monster() ) {
        // Pulled, not walking: it goes where it's dragged and keeps its own moves.
        const int moves = mon->get_moves();
        mon->move_to( to, /*force=*/true, /*step_on_critter=*/false );
        mon->set_moves( moves );
        return !mon->is_dead_state() && mon->pos_abs() == dest;
    }
    npc *const guy = victim.as_npc();
    if( guy == nullptr ) {
        return false;
    }
    // npc::move_to is the NPC's own walk, which a held NPC can't take; these are its steps.
    const tripoint_bub_ms old_pos = guy->pos_bub( here );
    if( guy->in_vehicle ) {
        here.unboard_vehicle( old_pos );
    }
    guy->setpos( here, to );
    if( here.veh_at( to ).part_with_feature( VPFLAG_BOARDABLE, true ) ) {
        here.board_vehicle( to, guy );
    }
    here.creature_on_trap( *guy );
    if( !guy->is_dead_state() ) {
        here.creature_in_field( *guy );
    }
    if( !guy->is_dead_state() && guy->will_be_cramped_in_vehicle_tile( here, dest ) ) {
        guy->add_effect( effect_cramped_space, 2_turns, true );
    }
    return !guy->is_dead_state() && guy->pos_abs() == dest;
}

void grapple::drag_after_step( Character &grabber, const tripoint_abs_ms &victim_at,
                               const tripoint_abs_ms &from )
{
    // Look the creature up again: the step may have shifted the map, or ended the hold.
    Creature *const victim = get_creature_tracker().creature_at( victim_at );
    if( victim == nullptr || victim == &grabber || hold_effect( grabber, *victim ) == nullptr ||
        grabber.pos_abs() == from ) {
        return;
    }
    if( victim->pos_abs() != from ) {
        drag_to( *victim, from );
    }
    if( victim->is_dead_state() || !in_reach( grabber, *victim ) ) {
        grabber.add_msg_if_player( m_bad, _( "You lose your hold on %s." ), victim->disp_name() );
        end_hold( grabber, victim );
    }
}

// Throwing a held creature

namespace
{

// The bio_railgun's magnetic push against machines. jmath can't read bionics or species, so it
// is worked out here and handed to grapple_throw_force as a bonus.
int railgun_bonus( const Character &thrower, const Creature &victim )
{
    if( !thrower.has_active_bionic( bio_railgun ) ) {
        return 0;
    }
    if( victim.in_species( species_ROBOT ) || victim.in_species( species_ROBOT_FLYING ) ) {
        return 16;
    }
    return victim.in_species( species_CYBORG ) ? 8 : 0;
}

} // namespace

float grapple::throw_force( const Character &thrower, const Creature &victim )
{
    map &here = get_map();
    const bool in_deep_water = here.has_flag( ter_furn_flag::TFLAG_DEEP_WATER,
                               victim.pos_bub( here ) );
    const double force = eval_formula( jmath_func_grapple_throw_force, thrower, victim, {
        in_deep_water ? 1.0 : 0.0, static_cast<double>( railgun_bonus( thrower, victim ) )
    } );
    return std::max( 0.0f, static_cast<float>( force ) );
}

int grapple::throw_range( float force )
{
    // As game::fling_creature counts it.
    return std::max( 0, static_cast<int>( force / 10 ) );
}

float grapple::throw_force_at( float full_force, int distance )
{
    const int range = throw_range( full_force );
    if( range < 1 || distance < 1 ) {
        return 0.0f;
    }
    distance = std::min( distance, range );
    // full_force / range is at least 10, so this flies at least distance tiles; the floor keeps
    // rounding from making it fall one short.
    return std::max( full_force * distance / range, 10.0f * distance );
}

int grapple::throw_stamina_cost( const Character &thrower, const Creature &victim, float force )
{
    const double cost = eval_formula( jmath_func_grapple_throw_stamina_cost, thrower, victim, {
        units::to_kilogram( victim.get_weight() ), static_cast<double>( force ),
        static_cast<double>( thrower.get_modifier( character_modifier_melee_stamina_cost_mod ) )
    } );
    return std::max( 0, static_cast<int>( std::round( cost ) ) );
}

int grapple::throw_move_cost( const Character &thrower )
{
    const float weary_mult = thrower.exertion_adjusted_move_multiplier( exertion_level() );
    return 100 + static_cast<int>( thrower.attack_speed( null_item_reference() ) / weary_mult );
}

int grapple::throw_verb_tier( const Character &thrower, const Creature &victim, float force )
{
    const double tier = eval_formula( jmath_func_grapple_throw_verb, thrower, victim, {
        static_cast<double>( force )
    } );
    return std::max( 0, static_cast<int>( std::round( tier ) ) );
}

std::string grapple::throw_verb( const Character &thrower, const Creature &victim, float force,
                                 std::optional<unsigned int> seed )
{
    const std::string category = string_format( "grapple_throw_verb_%d",
                                 throw_verb_tier( thrower, victim, force ) );
    const std::optional<translation> word = seed ?
                                            SNIPPET.random_from_category( category, *seed ) :
                                            SNIPPET.random_from_category( category );
    return word ? word->translated() : std::string( _( "throw" ) );
}

std::vector<tripoint_bub_ms> grapple::throw_path( const tripoint_bub_ms &from,
        const tripoint_bub_ms &target, int distance )
{
    // The steps game::fling_creature takes.
    std::vector<tripoint_bub_ms> path;
    if( from.xy() == target.xy() ) {
        return path;
    }
    tileray dir( coord_to_angle( from, target ) );
    tripoint_bub_ms at = from;
    for( int i = 0; i < distance; ++i ) {
        dir.advance();
        at += tripoint_rel_ms( dir.dx(), dir.dy(), 0 );
        path.push_back( at );
    }
    return path;
}

std::optional<std::string> grapple::throw_refusal( const Character &thrower,
        const Creature &victim )
{
    const float force = throw_force( thrower, victim );
    const int cost = throw_stamina_cost( thrower, victim, force );
    const int size_gap = static_cast<int>( victim.get_size() ) -
                         static_cast<int>( thrower.get_size() );
    // TLG: a throw must carry past the next tile, and a creature only a little larger.
    if( throw_range( force ) <= 1 || size_gap > thrower.get_arm_str() / 10 ||
        thrower.get_stamina_max() < cost ) {
        return string_format( _( "You can't muster the strength to throw %s." ),
                              victim.disp_name() );
    }
    if( thrower.get_stamina() < cost ) {
        return string_format( _( "You're too exhausted to throw %s." ), victim.disp_name() );
    }
    return std::nullopt;
}

std::optional<std::string> grapple::throw_target_refusal( const Character &thrower,
        const Creature &victim, const tripoint_bub_ms &target )
{
    map &here = get_map();
    const tripoint_bub_ms from = victim.pos_bub( here );
    if( target.xy() == from.xy() ) {
        return string_format( _( "Throw %s where?" ), victim.disp_name() );
    }
    if( target.z() != from.z() ) {
        return std::string( _( "You can only throw it across, not up or down." ) );
    }
    const std::vector<tripoint_bub_ms> path = throw_path( from, target, 1 );
    if( !path.empty() && path.front() == thrower.pos_bub( here ) ) {
        return string_format( _( "You can't throw %s through yourself." ), victim.disp_name() );
    }
    return std::nullopt;
}

bool grapple::throw_angers( const Character &thrower, const Creature &victim, float force )
{
    const npc *const guy = victim.as_npc();
    if( guy == nullptr ) {
        return false;
    }
    if( force > get_option<int>( "GRAPPLE_THROW_ANGERS_ANY" ) ) {
        return true;
    }
    // A stranger minds being thrown about even gently; friends and foes put up with more.
    return thrower.is_avatar() && !guy->is_enemy() && !guy->is_player_ally() &&
           force > get_option<int>( "GRAPPLE_THROW_ANGERS_NEUTRAL" );
}

bool grapple::break_free_to_throw( Character &thrower, const Creature &victim )
{
    if( !victim.has_effect_with_flag( json_flag_GRAB_FILTER ) ||
        !thrower.has_effect_with_flag( json_flag_GRAB ) ) {
        return true;
    }
    if( thrower.try_remove_grab() ) {
        return true;
    }
    thrower.mod_moves( -throw_move_cost( thrower ) );
    return false;
}

bool grapple::throw_held_creature( Character &thrower, Creature &victim,
                                   const tripoint_bub_ms &target )
{
    map &here = get_map();
    if( hold_effect( thrower, victim ) == nullptr ||
        throw_target_refusal( thrower, victim, target ) ) {
        return false;
    }
    const tripoint_bub_ms from = victim.pos_bub( here );
    const float full_force = throw_force( thrower, victim );
    // A fling takes one tile per step, diagonals included: count them the same way.
    const int distance = std::min( square_dist( from.xy(), target.xy() ),
                                   throw_range( full_force ) );
    if( distance < 1 ) {
        return false;
    }
    // A shorter throw is a gentler one, and lands where it was aimed.
    const float force = throw_force_at( full_force, distance );
    const bool railgun = railgun_bonus( thrower, victim ) > 0;
    int stamina_cost = throw_stamina_cost( thrower, victim, force );
    if( railgun ) {
        // TLG: the repulsor takes some of the work.
        stamina_cost = static_cast<int>( std::round( stamina_cost * 0.9 ) );
    }
    const bool angers = throw_angers( thrower, victim, force );

    // A monster takes it as an attack.
    if( monster *const mon = victim.as_monster(); mon != nullptr && mon->friendly == 0 ) {
        if( mon->type->has_anger_trigger( mon_trigger::HURT ) ) {
            mon->anger += 15;
            if( !thrower.is_fake() ) {
                mon->aggro_character = true;
            }
        }
        if( mon->type->has_fear_trigger( mon_trigger::HURT ) ) {
            mon->morale -= 15;
        }
    }

    end_hold( thrower, &victim );
    const std::string verb = throw_verb( thrower, victim, force );
    if( railgun ) {
        thrower.mod_power_level( -bio_railgun->power_trigger );
        //~ %1$s: shove, toss, throw, hurl or launch, %2$s: the creature thrown
        thrower.add_msg_if_player( m_good,
                                   _( "You feel your magnetic repulsor kick in as you %1$s %2$s!" ),
                                   verb, victim.disp_name() );
    } else {
        //~ %1$s: shove, toss, throw, hurl or launch, %2$s: the creature thrown
        thrower.add_msg_if_player( m_good, _( "You %1$s %2$s!" ), verb, victim.disp_name() );
    }
    thrower.add_msg_if_npc( _( "<npcname> throws %s!" ), victim.disp_name() );

    // TLG's throw: a soft landing when slow, and a character in the way is hit.
    fling_options options;
    options.soft_low_speed_landing = true;
    options.hit_characters = true;
    g->fling_creature( &victim, coord_to_angle( from, target ), force, false, false, options );
    if( angers && !victim.is_dead_state() ) {
        if( npc *const guy = victim.as_npc() ) {
            guy->on_attacked( thrower );
        }
    }

    thrower.set_activity_level( exertion_level() );
    thrower.mod_moves( -throw_move_cost( thrower ) );
    thrower.burn_energy_arms( -stamina_cost );
    return true;
}

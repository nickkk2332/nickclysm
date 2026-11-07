#include "trap.h"

#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "event.h"
#include "event_bus.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "item.h"
#include "map.h"
#include "map_iterator.h"
#include "messages.h"
#include "point.h"
#include "rng.h"
#include "string_formatter.h"
#include "talker.h"
#include "translations.h"

static const flag_id json_flag_ECHOLOCATION_DETECTABLE( "ECHOLOCATION_DETECTABLE" );
static const flag_id json_flag_SONAR_DETECTABLE( "SONAR_DETECTABLE" );
static const flag_id json_flag_UNCONSUMED( "UNCONSUMED" );

static const proficiency_id proficiency_prof_spotting( "prof_spotting" );
static const proficiency_id proficiency_prof_traps( "prof_traps" );
static const proficiency_id proficiency_prof_trapsetting( "prof_trapsetting" );

static const skill_id skill_traps( "traps" );

static const update_mapgen_id update_mapgen_none( "none" );

namespace
{

generic_factory<trap> trap_factory( "trap" );

} // namespace

/** @relates string_id */
template<>
inline bool int_id<trap>::is_valid() const
{
    return trap_factory.is_valid( *this );
}

/** @relates int_id */
template<>
const trap &int_id<trap>::obj() const
{
    return trap_factory.obj( *this );
}

/** @relates int_id */
template<>
const string_id<trap> &int_id<trap>::id() const
{
    return trap_factory.convert( *this );
}

/** @relates string_id */
template<>
int_id<trap> string_id<trap>::id() const
{
    return trap_factory.convert( *this, tr_null );
}

/** @relates string_id */
template<>
int_id<trap>::int_id( const string_id<trap> &id ) : _id( id.id() )
{
}

/** @relates string_id */
template<>
const trap &string_id<trap>::obj() const
{
    return trap_factory.obj( *this );
}

/** @relates int_id */
template<>
bool string_id<trap>::is_valid() const
{
    return trap_factory.is_valid( *this );
}

static std::vector<const trap *> funnel_traps;

const std::vector<const trap *> &trap::get_funnels()
{
    return funnel_traps;
}

static std::vector<const trap *> sound_triggered_traps;

const std::vector<const trap *> &trap::get_sound_triggered_traps()
{
    return sound_triggered_traps;
}

size_t trap::count()
{
    return trap_factory.size();
}

void trap::load_trap( const JsonObject &jo, const std::string &src )
{
    trap_factory.load( jo, src );
}

void trap::load( const JsonObject &jo, std::string_view src )
{
    mandatory( jo, was_loaded, "id", id );
    mandatory( jo, was_loaded, "name", name_ );
    mandatory( jo, was_loaded, "color", color, nc_color_reader{} );
    mandatory( jo, was_loaded, "symbol", sym, one_char_symbol_reader );
    mandatory( jo, was_loaded, "visibility", visibility );
    mandatory( jo, was_loaded, "avoidance", avoidance );
    mandatory( jo, was_loaded, "difficulty", difficulty );

    optional( jo, was_loaded, "memorial_male", memorial_male );
    optional( jo, was_loaded, "memorial_female", memorial_female );

    optional( jo, was_loaded, "trigger_message_u", trigger_message_u );
    optional( jo, was_loaded, "trigger_message_npc", trigger_message_npc );

    // Require either none, or both
    if( !!memorial_male != !!memorial_female ) {
        jo.throw_error_at(
            id.str(),
            "Only one gender of memorial message specified for trap " + id.str() + ", but none "
            "or both required." );
    }

    optional( jo, was_loaded, "flags", _flags );
    optional( jo, was_loaded, "trap_radius", trap_radius, 0 );
    // TODO: Is there a generic_factory version of this?
    if( jo.has_string( "action" ) ) {
        action_name = jo.get_string( "action" );
        act = trap_function_from_string( action_name );
        // Only "eocs" runs eocs: a copy-from child with another action doesn't keep its parent's
        if( action_name != "eocs" ) {
            eocs.clear();
            item_eocs.clear();
        }
    } else if( !was_loaded ) {
        jo.throw_error( "Missing mandatory member \"action\"" );
    }
    optional( jo, was_loaded, "map_regen", map_regen, update_mapgen_none );
    optional( jo, was_loaded, "benign", benign, false );
    optional( jo, was_loaded, "always_invisible", always_invisible, false );
    optional( jo, was_loaded, "funnel_radius", funnel_radius_mm, 0 );
    optional( jo, was_loaded, "comfort", comfort, 0 );
    int legacy_floor_bedding_warmth = units::to_legacy_bodypart_temp_delta( floor_bedding_warmth );
    optional( jo, was_loaded, "floor_bedding_warmth", legacy_floor_bedding_warmth, 0 );
    floor_bedding_warmth = units::from_legacy_bodypart_temp_delta( legacy_floor_bedding_warmth );
    // The action is the one read above or the copy-from parent's. (Every action is a function
    // pointer of the same type, so comparing the std::function target types never told them
    // apart.)
    if( jo.has_member( "spell_data" ) ) {
        if( action_name != "spell" ) {
            jo.throw_error_at( "spell_data",
                               R"(Can't use "spell_data" without specifying "action": "spell")" );
        }
        optional( jo, was_loaded, "spell_data", spell_data );
    }
    for( const char *member : {
             "eocs", "item_eocs"
         } ) {
        if( jo.has_member( member ) && action_name != "eocs" ) {
            jo.throw_error_at( member, string_format(
                                   R"(Can't use "%s" without specifying "action": "eocs")", member ) );
        }
    }
    // A list given here replaces the copy-from parent's instead of adding to it.
    const auto load_eocs = [&jo, src]( const std::string & member,
    std::vector<effect_on_condition_id> &list ) {
        if( !jo.has_member( member ) ) {
            return;
        }
        list.clear();
        for( JsonValue jv : jo.get_array( member ) ) {
            list.push_back( effect_on_conditions::load_inline_eoc( jv, src ) );
        }
    };
    load_eocs( "eocs", eocs );
    load_eocs( "item_eocs", item_eocs );
    load_eocs( "disarm_eocs", disarm_eocs );
    load_eocs( "disarm_fail_eocs", disarm_fail_eocs );
    if( jo.has_member( "disarm_condition" ) ) {
        read_condition( jo, "disarm_condition", disarm_condition, true );
    }
    optional( jo, was_loaded, "disarm_condition_msg", disarm_condition_msg,
              to_translation( "That %s looks too dangerous to mess with.  Best leave it alone." ) );
    if( jo.has_member( "easy_take_down_condition" ) ) {
        read_condition( jo, "easy_take_down_condition", easy_take_down_condition, false );
    }
    optional( jo, was_loaded, "trigger_damage", tuning.damage );
    optional( jo, was_loaded, "tetanus_chance", tuning.tetanus_chance );
    optional( jo, was_loaded, "tetanus_chance_resistant", tuning.tetanus_chance_resistant );
    optional( jo, was_loaded, "break_weight", tuning.break_weight );
    optional( jo, was_loaded, "self_disarm_chance", tuning.self_disarm_chance );
    optional( jo, was_loaded, "trigger_weight", trigger_weight );
    optional( jo, was_loaded, "sound_threshold", sound_threshold );
    optional( jo, was_loaded, "drops", components );
    optional( jo, was_loaded, "vehicle_data", vehicle_data );
}

void vehicle_handle_trap_data::deserialize( const JsonObject &jo )
{
    optional( jo, false, "do_explosion", do_explosion, false );
    optional( jo, false, "is_falling", is_falling, false );
    optional( jo, false, "chance", chance, 100 );
    optional( jo, false, "damage", damage, 0 );
    optional( jo, false, "shrapnel", shrapnel, 0 );
    optional( jo, false, "sound_volume", sound_volume, 0 );
    optional( jo, false, "sound", sound );
    optional( jo, false, "sound_type", sound_type, "" );
    optional( jo, false, "sound_variant", sound_variant, "" );
    optional( jo, false, "spawn_items", spawn_items, named_pair_reader<itype_id, double> { "id", "chance", 1.0 } );
    optional( jo, false, "set_trap", set_trap, trap_str_id::NULL_ID() );
    if( set_trap != trap_str_id::NULL_ID() ) {
        remove_trap = false;
    } else {
        optional( jo, false, "remove_trap", remove_trap, false );
    }
}

void trap_damage_roll::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "damage_type", type );
    if( jo.has_member( "amount" ) ) {
        mandatory( jo, false, "amount", min );
        max = min;
    } else {
        mandatory( jo, false, "min", min );
        mandatory( jo, false, "max", max );
    }
}

void trap::comp::deserialize( const JsonValue &jv )
{
    if( jv.test_object() ) {
        JsonObject jo = jv.get_object();
        mandatory( jo, false, "item", item_type );
        optional( jo, false, "quantity", quantity, 1 );
        optional( jo, false, "charges", charges, 1 );
    } else {
        jv.read( item_type, true );
        quantity = 1;
        charges = 1;
    }
}

std::string trap::name() const
{
    return name_.translated();
}

update_mapgen_id trap::map_regen_target() const
{
    return map_regen;
}

void trap::reset()
{
    funnel_traps.clear();
    sound_triggered_traps.clear();
    trap_factory.reset();
}

bool trap::is_trivial_to_spot() const
{
    // @TODO technically the trap may not be detected even with visibility == 0, see trap::detect_trap
    return visibility <= 0 && !is_always_invisible();
}

// SONAR refers to ground-penetrating sonar and detects traps buried in the ground
bool trap::detected_by_ground_sonar() const
{
    return has_flag( json_flag_SONAR_DETECTABLE );
}

// Echolocation refers to both bat-style echolocation and underwater SONAR, and
// detects traps which are solid and unburied objects, aboveground or underwater.
// Isn't fine enough to detect very small traps ie caltrops
bool trap::detected_by_echolocation() const
{
    return has_flag( json_flag_ECHOLOCATION_DETECTABLE );
}

bool trap::detect_trap( const tripoint_bub_ms &pos, const Character &p ) const
{
    // * Buried landmines, the silent killer, have a visibility of 10.
    // Assuming no knowledge of traps or proficiencies, and average per/int (8 each),
    // most characters will get a mean_roll of 6.5 against a trap that is one tile away.
    // With a std deviation of 3, that leaves a ~12% chance of spotting a landmine when you are next to it (per turn).
    // This gets worse if you are sleep deprived, or can't see as well.
    // Obviously it rapidly gets better as your skills improve, with most of the improvement coming from proficiencies.

    // Devices skill is helpful for spotting traps
    const float traps_skill_level = p.get_skill_level( skill_traps );

    // Perception is the main stat for spotting traps, int helps a bit.
    // In this case, stats are more important than skills.
    const float weighted_stat_average = ( 4.0f * p.get_per() + p.get_int() ) / 5.0f;

    // Eye encumbrance will penalize spotting
    const float encumbrance_penalty = p.encumb( bodypart_id( "eyes" ) ) / 10.0f;

    // The further away the trap is, the harder it is to spot.
    // Subtract 1 so that we don't get an unfair penalty when not quite on top of the trap.
    const int distance_penalty = rl_dist( p.pos_bub(), pos ) - 1;

    int proficiency_effect = -1;
    // Without at least a basic traps proficiency, your skill level is effectively three levels lower.
    if( p.has_proficiency( proficiency_prof_traps ) ) {
        proficiency_effect += 1;
        // If you have the basic traps prof, negate the above penalty
    }
    if( p.has_proficiency( proficiency_prof_spotting ) ) {
        proficiency_effect += 3;
        // If you have the spotting proficiency, add a whopping 9 effective skill levels.
    }
    if( p.has_proficiency( proficiency_prof_trapsetting ) ) {
        proficiency_effect += 1;
        // Knowing how to set effective traps gives you a considerable bonus to spotting them as well.
    }

    // For every 1000 points of sleep deprivation, reduce your roll by 1.
    // As of this writing, sleep deprivation passively increases at the rate of 1 point per minute.
    const float sleepiness_penalty = p.get_sleep_deprivation() / 1000.0f;

    const float mean_roll = weighted_stat_average + ( traps_skill_level / 3.0f ) +
                            proficiency_effect - distance_penalty - sleepiness_penalty - encumbrance_penalty;

    const int roll = std::round( normal_roll( mean_roll, 3 ) );

    add_msg_debug( debugmode::DF_CHARACTER,
                   "Character %s rolling to detect trap %s. Actual roll: %i, average roll: %f, roll required to detect: %i.",
                   p.get_name(), name(), roll, mean_roll, visibility );

    return roll > visibility;
}

// Whether or not, in the current state, the player can see the trap.
bool trap::can_see( const tripoint_bub_ms &pos, const Character &p ) const
{
    if( is_null() ) {
        // There is no trap at all, so logically one can not see it.
        return false;
    }
    if( is_always_invisible() ) {
        return false;
    }
    return visibility < 0 || p.knows_trap( pos );
}

void trap::trigger( const tripoint_bub_ms &pos ) const
{
    if( is_null() ) {
        return;
    }
    // Set off by a sound
    if( trigger_item_eocs( pos, nullptr ) ) {
        return;
    }
    act( pos, nullptr, nullptr );
}

bool trap::trigger_item_eocs( const tripoint_bub_ms &pos, const item *it ) const
{
    if( item_eocs.empty() ) {
        return false;
    }
    map &here = get_map();
    const tripoint_abs_ms loc = here.get_abs( pos );
    // As a spell trap: removed first, so an EOC can set a new trap there and a teleport
    // doesn't leave the bubble coordinates stale
    if( !has_flag( json_flag_UNCONSUMED ) ) {
        here.remove_trap( pos );
    }
    for( const effect_on_condition_id &eoc : item_eocs ) {
        dialogue d( nullptr, nullptr );
        d.set_value( "trap", id.str() );
        d.set_value( "trap_location", loc );
        d.set_value( "trigger_item", it == nullptr ? std::string() : it->typeId().str() );
        eoc->activate( d );
    }
    return true;
}

void trap::run_eocs( const std::vector<effect_on_condition_id> &list, Creature *who,
                     const tripoint_abs_ms &pos ) const
{
    for( const effect_on_condition_id &eoc : list ) {
        std::unique_ptr<talker> alpha;
        if( who != nullptr ) {
            alpha = get_talker_for( who );
        }
        dialogue d( std::move( alpha ), nullptr );
        d.set_value( "trap", id.str() );
        d.set_value( "trap_location", pos );
        eoc->activate( d );
    }
}

bool trap::can_try_disarm( Character &who, const tripoint_bub_ms &pos ) const
{
    if( !disarm_condition ) {
        return true;
    }
    dialogue d( get_talker_for( who ), nullptr );
    d.set_value( "trap", id.str() );
    d.set_value( "trap_location", get_map().get_abs( pos ) );
    return disarm_condition( d );
}

bool trap::easy_take_down( Character &who, const tripoint_bub_ms &pos ) const
{
    if( easy_take_down() ) {
        return true;
    }
    if( !easy_take_down_condition ) {
        return false;
    }
    dialogue d( get_talker_for( who ), nullptr );
    d.set_value( "trap", id.str() );
    d.set_value( "trap_location", get_map().get_abs( pos ) );
    return easy_take_down_condition( d );
}

void trap::spawn_drops( map &m, const tripoint_bub_ms &p ) const
{
    for( const trap::comp &i : components ) {
        m.spawn_item( p, i.item_type, i.quantity, i.charges );
    }
}

void trap::trigger( const tripoint_bub_ms &pos, Creature &creature ) const
{
    trigger( pos, &creature, nullptr );
}

void trap::trigger( const tripoint_bub_ms &pos, item &item ) const
{
    trigger( pos, nullptr, &item );
}

void trap::trigger( const tripoint_bub_ms &pos, Creature *creature, item *item ) const
{
    if( is_null() ) {
        return;
    }
    const bool is_real_creature = creature != nullptr && !creature->is_hallucination();
    if( !is_real_creature && item != nullptr && trigger_item_eocs( pos, item ) ) {
        return;
    }
    if( is_real_creature || item != nullptr ) {
        bool triggered = act( pos, creature, item );
        if( triggered && is_real_creature ) {
            if( Character *ch = creature->as_character() ) {
                get_event_bus().send<event_type::character_triggers_trap>( ch->getID(), id,
                        get_map().get_abs( pos ).raw() );
            } else if( creature->is_monster() ) {
                // the monster is the alpha talker
                const cata::event triggered_by_monster =
                    cata::event::make<event_type::monster_triggers_trap>( id,
                            get_map().get_abs( pos ).raw() );
                get_event_bus().send_with_talker( creature, static_cast<Creature *>( nullptr ),
                                                  triggered_by_monster );
            }
        }
    }
}

bool trap::is_null() const
{
    return loadid == tr_null;
}

bool trap::triggered_by_item( const item &itm ) const
{
    return !is_null() && itm.weight() >= trigger_weight;
}

bool trap::is_funnel() const
{
    return !is_null() && funnel_radius_mm > 0;
}

bool trap::has_sound_trigger() const
{
    const bool has_sound_thresh = sound_threshold.first > 0 && sound_threshold.second > 0;
    return !is_null() && has_sound_thresh;
}

bool trap::triggered_by_sound( int vol, int dist ) const
{
    const int volume = vol - dist;
    // now determine sound threshold probabilities
    // linear model: 0% below sound_min, 25% at sound_min, 100% at sound_max
    const int sound_min = sound_threshold.first;
    const int sound_max = sound_threshold.second;
    const int sound_range = sound_max - sound_min;
    if( volume < sound_min ) {
        return false;
    }
    int sound_chance = 100;
    if( sound_range > 0 ) {
        sound_chance = 25 + ( 75 * ( volume - sound_min ) / sound_range );
    }
    //debugmsg("Sound chance: %d%%", sound_chance);
    return !is_null() && ( rng( 0, 100 ) <= sound_chance );
}

void trap::on_disarmed( map &m, const tripoint_bub_ms &p ) const
{
    for( const trap::comp &i : components ) {
        m.spawn_item( p.xy(), i.item_type, i.quantity, i.charges );
    }
    for( const tripoint_bub_ms &dest : m.points_in_radius( p, trap_radius ) ) {
        m.remove_trap( dest );
    }
}

//////////////////////////
// convenient int-lookup names for hard-coded functions
trap_id tr_null;

void trap::check_consistency()
{
    for( const trap &t : trap_factory.get_all() ) {
        for( const trap::comp &i : t.components ) {
            if( !item::type_is_defined( i.item_type ) ) {
                debugmsg( "trap %s has unknown item as component %s", t.id.str(), i.item_type.str() );
            }
        }
        if( t.sound_threshold.first > t.sound_threshold.second ) {
            debugmsg( "trap %s has higher min sound threshold than max and can never trigger", t.id.str() );
        }
        if( ( t.sound_threshold.first > 0 ) != ( t.sound_threshold.second > 0 ) ) {
            debugmsg( "trap %s has bad sound threshold of 0 and will trigger on anything", t.id.str() );
        }
        for( const trap_damage_roll &roll : t.tuning.damage ) {
            if( !roll.type.is_valid() ) {
                debugmsg( "trap %s has unknown damage type %s in trigger_damage", t.id.str(),
                          roll.type.str() );
            }
            if( roll.min < 0 || roll.min > roll.max ) {
                debugmsg( "trap %s has a trigger_damage of %s from %d to %d", t.id.str(),
                          roll.type.str(), roll.min, roll.max );
            }
        }
        const trap_trigger_tuning &tuning = t.tuning;
        const bool tuned = !tuning.damage.empty() || tuning.tetanus_chance ||
                           tuning.tetanus_chance_resistant || tuning.break_weight ||
                           tuning.self_disarm_chance;
        static const std::set<std::string> tuned_actions = {
            "beartrap", "board", "caltrops", "caltrops_glass"
        };
        if( tuned && tuned_actions.count( t.action_name ) == 0 ) {
            debugmsg( "trap %s sets trigger_damage, tetanus_chance, break_weight or "
                      "self_disarm_chance, which its action \"%s\" doesn't use", t.id.str(),
                      t.action_name );
        }
        if( tuning.tetanus_chance.value_or( 0 ) < 0 ||
            tuning.tetanus_chance_resistant.value_or( 0 ) < 0 ||
            tuning.self_disarm_chance.value_or( 0 ) < 0 ||
            tuning.break_weight.value_or( 1_gram ) <= 0_gram ) {
            debugmsg( "trap %s has a negative tetanus_chance or self_disarm_chance, or a "
                      "break_weight of 0 or less", t.id.str() );
        }
    }
}

bool trap::easy_take_down() const
{
    return avoidance == 0 && difficulty == 0;
}

void trap::set_trap_data( itype_id trap_item_type_id )
{
    trap_item_type = trap_item_type_id;
}

bool trap::can_not_be_disarmed() const
{
    return difficulty >= 99;
}

void trap::finalize()
{
    // We need to set int ids manually now
    loadid = id.id();
    if( is_funnel() ) {
        funnel_traps.push_back( this );
    }
    if( has_sound_trigger() ) {
        sound_triggered_traps.push_back( this );
    }
}

void trap::finalize_all()
{
    trap_factory.finalize();
    tr_null = trap_str_id::NULL_ID().id();
}

std::string trap::debug_describe() const
{
    return string_format( _( "Visible: %d\nAvoidance: %d\nDifficulty: %d\nBenign: %s" ), visibility,
                          avoidance, difficulty, is_benign() ? _( "Yes" ) : _( "No" ) );
}

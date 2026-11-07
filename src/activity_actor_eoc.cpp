// Starting activities that need a target from EOCs: u_assign_activity's "target_item",
// "target_loc", "target_monster", "recipe" and "butcher_type" (ACT-J02). The activities that need
// nothing but a duration are started by eoc_starters in activity_actor.cpp.

#include "activity_actor_eoc.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "basecamp.h"
#include "bodypart.h"
#include "butchery.h"
#include "butchery_requirements.h"
#include "calendar.h"
#include "cata_utility.h"
#include "character.h"
#include "character_martial_arts.h"
#include "construction.h"
#include "construction_group.h"
#include "coordinates.h"
#include "craft_command.h"
#include "craft_prompts.h"
#include "creature.h"
#include "creature_tracker.h"
#include "enums.h"
#include "faction.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "handle_liquid.h"
#include "harvest.h"
#include "iexamine.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "map.h"
#include "map_iterator.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "martialarts.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "pimpl.h"
#include "player_activity.h"
#include "pocket_type.h"
#include "point.h"
#include "proficiency.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "string_formatter.h"
#include "temp_crafting_inventory.h"
#include "translations.h"
#include "type_id.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"

static const activity_id ACT_BASH( "ACT_BASH" );
static const activity_id ACT_BINDER_COPY_RECIPE( "ACT_BINDER_COPY_RECIPE" );
static const activity_id ACT_BLEED( "ACT_BLEED" );
static const activity_id ACT_BOLTCUTTING( "ACT_BOLTCUTTING" );
static const activity_id ACT_BUILD( "ACT_BUILD" );
static const activity_id ACT_BUTCHER( "ACT_BUTCHER" );
static const activity_id ACT_BUTCHER_FULL( "ACT_BUTCHER_FULL" );
static const activity_id ACT_CHOP_LOGS( "ACT_CHOP_LOGS" );
static const activity_id ACT_CHOP_TREE( "ACT_CHOP_TREE" );
static const activity_id ACT_CLEAR_RUBBLE( "ACT_CLEAR_RUBBLE" );
static const activity_id ACT_CONSUME( "ACT_CONSUME" );
static const activity_id ACT_CRACKING( "ACT_CRACKING" );
static const activity_id ACT_CRAFT( "ACT_CRAFT" );
static const activity_id ACT_CRAFT_WAIT( "ACT_CRAFT_WAIT" );
static const activity_id ACT_DISABLE( "ACT_DISABLE" );
static const activity_id ACT_DISASSEMBLE( "ACT_DISASSEMBLE" );
static const activity_id ACT_DISMEMBER( "ACT_DISMEMBER" );
static const activity_id ACT_DISSECT( "ACT_DISSECT" );
static const activity_id ACT_DROP( "ACT_DROP" );
static const activity_id ACT_FERTILIZE_PLANT( "ACT_FERTILIZE_PLANT" );
static const activity_id ACT_FIELD_DRESS( "ACT_FIELD_DRESS" );
static const activity_id ACT_FIRSTAID( "ACT_FIRSTAID" );
static const activity_id ACT_FISH( "ACT_FISH" );
static const activity_id ACT_FORAGE( "ACT_FORAGE" );
static const activity_id ACT_GUNMOD_REMOVE( "ACT_GUNMOD_REMOVE" );
static const activity_id ACT_HACKING( "ACT_HACKING" );
static const activity_id ACT_HACKSAW( "ACT_HACKSAW" );
static const activity_id ACT_HARVEST( "ACT_HARVEST" );
static const activity_id ACT_INSERT_ITEM( "ACT_INSERT_ITEM" );
static const activity_id ACT_JACKHAMMER( "ACT_JACKHAMMER" );
static const activity_id ACT_LOCKPICK( "ACT_LOCKPICK" );
static const activity_id ACT_MILK( "ACT_MILK" );
static const activity_id ACT_MOP( "ACT_MOP" );
static const activity_id ACT_MOVE_ITEMS( "ACT_MOVE_ITEMS" );
static const activity_id ACT_OXYTORCH( "ACT_OXYTORCH" );
static const activity_id ACT_PICKAXE( "ACT_PICKAXE" );
static const activity_id ACT_PICKUP( "ACT_PICKUP" );
static const activity_id ACT_PLANT_SEED( "ACT_PLANT_SEED" );
static const activity_id ACT_PLAY_WITH_PET( "ACT_PLAY_WITH_PET" );
static const activity_id ACT_PRYING( "ACT_PRYING" );
static const activity_id ACT_PULP( "ACT_PULP" );
static const activity_id ACT_QUARTER( "ACT_QUARTER" );
static const activity_id ACT_READ( "ACT_READ" );
static const activity_id ACT_RELOAD( "ACT_RELOAD" );
static const activity_id ACT_SHEARING( "ACT_SHEARING" );
static const activity_id ACT_SKIN( "ACT_SKIN" );
static const activity_id ACT_TENT_DECONSTRUCT( "ACT_TENT_DECONSTRUCT" );
static const activity_id ACT_TENT_PLACE( "ACT_TENT_PLACE" );
static const activity_id ACT_TOOLMOD_ADD( "ACT_TOOLMOD_ADD" );
static const activity_id ACT_UNLOAD( "ACT_UNLOAD" );
static const activity_id ACT_VEHICLE_FOLD( "ACT_VEHICLE_FOLD" );
static const activity_id ACT_WEAR( "ACT_WEAR" );
static const activity_id ACT_WIELD( "ACT_WIELD" );

static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_earphones( "earphones" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_sheared( "sheared" );
static const efftype_id effect_tied( "tied" );


static const flag_id json_flag_MOP( "MOP" );

static const itype_id itype_disassembly( "disassembly" );
static const itype_id itype_paper( "paper" );

static const json_character_flag json_flag_SAFECRACK_NO_TOOL( "SAFECRACK_NO_TOOL" );

static const proficiency_id proficiency_prof_dissect_humans( "prof_dissect_humans" );

static const quality_id qual_AXE( "AXE" );
static const quality_id qual_BUTCHER( "BUTCHER" );
static const quality_id qual_CUT_FINE( "CUT_FINE" );
static const quality_id qual_DIG( "DIG" );
static const quality_id qual_GLARE( "GLARE" );
static const quality_id qual_HACK( "HACK" );
static const quality_id qual_LOCKPICK( "LOCKPICK" );
static const quality_id qual_PRY( "PRY" );
static const quality_id qual_PRYING_NAIL( "PRYING_NAIL" );
static const quality_id qual_SAW_M( "SAW_M" );
static const quality_id qual_SHEAR( "SHEAR" );

static const skill_id skill_survival( "survival" );

static const ter_str_id ter_t_stump( "t_stump" );
static const ter_str_id ter_t_trunk( "t_trunk" );

static const trait_id trait_BURROW( "BURROW" );
static const trait_id trait_BURROWLARGE( "BURROWLARGE" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );

namespace activity_actors
{
namespace
{

using refusal = std::optional<std::string>;

// "butcher_type" names, and the activity each one runs as.
const std::vector<std::pair<std::string, butcher_type>> &butcher_type_names()
{
    static const std::vector<std::pair<std::string, butcher_type>> names = {
        { "quick", butcher_type::QUICK },
        { "full", butcher_type::FULL },
        { "field_dress", butcher_type::FIELD_DRESS },
        { "skin", butcher_type::SKIN },
        { "quarter", butcher_type::QUARTER },
        { "dismember", butcher_type::DISMEMBER },
        { "dissect", butcher_type::DISSECT },
        { "bleed", butcher_type::BLEED },
    };
    return names;
}

// The butchery activities other than ACT_BUTCHER name their kind of butchery themselves.
const std::unordered_map<activity_id, butcher_type> &butchery_activities()
{
    static const std::unordered_map<activity_id, butcher_type> acts = {
        { ACT_BLEED, butcher_type::BLEED },
        { ACT_BUTCHER_FULL, butcher_type::FULL },
        { ACT_DISMEMBER, butcher_type::DISMEMBER },
        { ACT_DISSECT, butcher_type::DISSECT },
        { ACT_FIELD_DRESS, butcher_type::FIELD_DRESS },
        { ACT_QUARTER, butcher_type::QUARTER },
        { ACT_SKIN, butcher_type::SKIN },
    };
    return acts;
}

// Whether `who` is now doing one of `types`: the activity started and its actor didn't stop it.
bool now_doing( const Character &who, const std::vector<activity_id> &types )
{
    return std::find( types.begin(), types.end(), who.activity.id() ) != types.end();
}

// Whether `a` is `b` or next to it on the same level: as examine and the crafting menu count
// reach, never through a floor or a ceiling.
bool within_reach( const tripoint_abs_ms &a, const tripoint_abs_ms &b )
{
    return a.z() == b.z() && square_dist( a.xy(), b.xy() ) <= 1;
}

std::string did_not_start( const item &it )
{
    return string_format( _( "Working on the %s stopped as soon as it started." ), it.tname() );
}

// Why `who` can't work on `it` where it is; nullopt if they can. An item counts as within reach
// when `who` carries it, or when it lies (or sits in a container or vehicle) on `who`'s tile or
// next to it. With `must_carry`, only the first.
refusal reach_refusal( const Character &who, const item_location &it, bool must_carry )
{
    if( !it || it.get_item() == nullptr ) {
        return std::string( _( "The item is gone." ) );
    }
    const Character *carrier = it.carrier();
    if( carrier == &who ) {
        return std::nullopt;
    }
    if( carrier != nullptr ) {
        return string_format( _( "%1$s is carrying the %2$s." ), carrier->get_name(), it->tname() );
    }
    if( must_carry ) {
        return string_format( _( "The %s has to be carried." ), it->tname() );
    }
    if( !within_reach( who.pos_abs(), it.pos_abs() ) ) {
        return string_format( _( "The %s is out of reach." ), it->tname() );
    }
    return std::nullopt;
}

refusal ownership_refusal( const Character &who, const item &it )
{
    if( !it.is_owned_by( who, true ) ) {
        return string_format( _( "The %1$s belongs to %2$s." ), it.tname(), it.get_owner_name() );
    }
    return std::nullopt;
}

// --- The fields each activity takes ---

std::string quoted_list( const std::vector<std::string> &names )
{
    std::string ret;
    for( const std::string &name : names ) {
        if( !ret.empty() ) {
            ret += ", ";
        }
        ret += "\"" + name + "\"";
    }
    return ret;
}

std::vector<std::string> given_names( const eoc_target_fields &given )
{
    std::vector<std::string> names;
    if( given.item ) {
        names.emplace_back( "target_item" );
    }
    if( given.loc ) {
        names.emplace_back( "target_loc" );
    }
    if( given.recipe ) {
        names.emplace_back( "recipe" );
    }
    if( given.butcher_type ) {
        names.emplace_back( "butcher_type" );
    }
    if( given.creature ) {
        names.emplace_back( "target_monster" );
    }
    return names;
}

// Only "target_item", which it needs.
refusal fields_item_only( const eoc_target_fields &given )
{
    if( !given.item ) {
        return std::string( "needs \"target_item\"" );
    }
    if( given.loc || given.recipe || given.butcher_type || given.creature ) {
        return std::string( "takes no target but \"target_item\"" );
    }
    return std::nullopt;
}

// ACT_BUTCHER: "target_item" and an optional "butcher_type".
refusal fields_butcher( const eoc_target_fields &given )
{
    if( !given.item ) {
        return std::string( "needs \"target_item\" (the corpse)" );
    }
    if( given.loc || given.recipe || given.creature ) {
        return std::string( R"(takes no target but "target_item" and "butcher_type")" );
    }
    return std::nullopt;
}

// ACT_CRAFT: a "recipe" to start, with an optional "target_loc" to put it; or the
// "target_item" of a craft in progress to go on with.
refusal fields_craft( const eoc_target_fields &given )
{
    if( given.butcher_type ) {
        return std::string( "takes no \"butcher_type\"" );
    }
    if( given.creature ) {
        return std::string( "takes no \"target_monster\"" );
    }
    if( given.item == given.recipe ) {
        return std::string( "needs either \"recipe\" (a new craft) or \"target_item\" (a craft in "
                            "progress), not both" );
    }
    if( given.item && given.loc ) {
        return std::string( "takes \"target_loc\" only with \"recipe\": a craft in progress stays "
                            "where it is" );
    }
    return std::nullopt;
}

// --- Starting each activity ---

// Whether butchering `monster` would make check_anger_empathetic_npcs_with_cannibalism ask the
// avatar first.
bool butchery_would_ask( const Character &you, const mtype_id &monster )
{
    if( !you.is_avatar() ) {
        return false;
    }
    if( you.empathizes_with_monster( monster ) ) {
        return true;
    }
    const map &here = get_map();
    for( npc &guy : g->all_npcs() ) {
        if( guy.is_active() && guy.sees( here, you ) && guy.empathizes_with_monster( monster ) ) {
            return true;
        }
    }
    return false;
}

// Why `you` can't start `action` on `corpse_item` now; nullopt if they can. It checks, without
// asking or showing anything, what set_up_butchery refuses once the activity runs (the tools,
// the requirements and the corpse's state; please keep the two in step), and what the butchery
// menu checks (light, morale, something to remove). Without it, an NPC told to butcher could
// get set_up_butchery's popup.
refusal butchery_refusal( Character &you, const item &corpse_item, const butcher_type action )
{
    const mtype *const corpse = corpse_item.get_mtype();
    if( !corpse_item.is_corpse() || corpse == nullptr ) {
        return string_format( _( "The %s is not a corpse." ), corpse_item.tname() );
    }
    if( you.controlling_vehicle ) {
        return std::string( _( "You can't butcher while driving!" ) );
    }
    if( !you.has_morale_to_craft() ) {
        return std::string( _( "Not in the mood to butcher." ) );
    }
    if( action != butcher_type::DISMEMBER && you.fine_detail_vision_mod() > 4 ) {
        return std::string( _( "It's too dark to butcher." ) );
    }

    // As set_up_butchery.
    const int factor = you.max_quality( action == butcher_type::DISSECT ? qual_CUT_FINE :
                                        qual_BUTCHER, PICKUP_RANGE );
    if( action == butcher_type::DISSECT && factor <= 0 ) {
        return std::string( _( "None of your tools are sharp and precise enough to do that." ) );
    }
    if( action != butcher_type::DISSECT && factor == INT_MIN ) {
        return std::string( _( "None of your cutting tools are suitable for butchering." ) );
    }
    const requirement_id req = corpse->harvest->get_butchery_requirements().get_fastest_requirements(
                                   &you, you.crafting_inventory(), corpse->size, action ).second;
    if( !req->can_make_with_inventory( &you, you.crafting_inventory( you.pos_bub(), PICKUP_RANGE ),
                                       is_crafting_component ) ) {
        return std::string( _( "You can't butcher this; you are missing some tools." ) );
    }
    const bool dressed = corpse_item.has_flag( flag_FIELD_DRESS ) ||
                         corpse_item.has_flag( flag_FIELD_DRESS_FAILED );
    if( action == butcher_type::BLEED && ( dressed || corpse_item.has_flag( flag_BLED ) ||
                                           corpse_item.has_flag( flag_QUARTERED ) ) ) {
        return std::string( _( "This corpse has already been bled." ) );
    }
    if( action == butcher_type::DISSECT && ( corpse_item.has_flag( flag_QUARTERED ) ||
            corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) ) ) {
        return std::string( _( "It would be futile to dissect this badly damaged corpse." ) );
    }
    if( action == butcher_type::FIELD_DRESS && dressed ) {
        return std::string( _( "This corpse is already field dressed." ) );
    }
    if( action == butcher_type::SKIN && corpse_item.has_flag( flag_SKINNED ) ) {
        return std::string( _( "This corpse is already skinned." ) );
    }
    if( action == butcher_type::QUARTER ) {
        if( corpse->size == creature_size::tiny ) {
            return std::string( _( "This corpse is too small to quarter without damaging." ) );
        }
        if( corpse_item.has_flag( flag_QUARTERED ) ) {
            return std::string( _( "This is already quartered." ) );
        }
        if( !dressed && butchery_needs_field_dressing( *corpse ) ) {
            return std::string( _( "You need to perform field dressing before quartering." ) );
        }
    }

    // As butcher_submenu: something to remove.
    const bool has_blood = butchery_has_target( corpse_item, butcher_type::BLEED );
    const bool has_skin = butchery_has_target( corpse_item, butcher_type::SKIN );
    const bool has_organs = butchery_has_target( corpse_item, butcher_type::FIELD_DRESS );
    if( action == butcher_type::FIELD_DRESS && !has_organs ) {
        return string_format( _( "The %s has no organs." ), corpse_item.tname() );
    }
    if( action == butcher_type::SKIN && !has_skin ) {
        return string_format( _( "The %s has no skin." ), corpse_item.tname() );
    }
    if( action == butcher_type::BLEED && !has_blood ) {
        return string_format( _( "The %s has no blood." ), corpse_item.tname() );
    }

    // check_anger_empathetic_npcs_with_cannibalism asks the avatar to confirm.
    const bool trained_dissection = action == butcher_type::DISSECT &&
                                    you.has_proficiency( proficiency_prof_dissect_humans );
    if( action != butcher_type::DISMEMBER && !trained_dissection &&
        butchery_would_ask( you, corpse->id ) ) {
        return string_format( _( "Butchering the %s would need your confirmation." ),
                              corpse_item.tname() );
    }
    return std::nullopt;
}

butcher_type butcher_type_for( const activity_id &type, const eoc_activity_target &target )
{
    const auto fixed = butchery_activities().find( type );
    if( fixed != butchery_activities().end() ) {
        return fixed->second;
    }
    if( target.butcher_type ) {
        for( const std::pair<std::string, butcher_type> &name : butcher_type_names() ) {
            if( name.first == *target.butcher_type ) {
                return name.second;
            }
        }
    }
    return butcher_type::QUICK;
}

refusal start_butcher( const activity_id &type, Character &who,
                       const eoc_activity_target &target )
{
    item_location corpse = target.item;
    if( refusal no = reach_refusal( who, corpse, false ) ) {
        return no;
    }
    if( corpse.where() != item_location::type::map ) {
        return string_format( _( "The %s has to be on the ground to butcher it." ), corpse->tname() );
    }
    map &here = get_map();
    if( here.has_flag( ter_furn_flag::TFLAG_SEALED, corpse.pos_bub( here ) ) ) {
        return string_format( _( "The %s is sealed in." ), corpse->tname() );
    }
    const butcher_type action = butcher_type_for( type, target );
    if( refusal no = butchery_refusal( who, *corpse, action ) ) {
        return no;
    }
    const butchery_activity_actor actor( butchery_data( corpse, action ) );
    const activity_id runs_as = actor.get_type();
    who.assign_activity( actor );
    if( !now_doing( who, { runs_as } ) ) {
        return did_not_start( *corpse );
    }
    return std::nullopt;
}

refusal start_craft( const activity_id &, Character &who, const eoc_activity_target &target )
{
    map &here = get_map();
    if( target.item ) {
        // Go on with a craft in progress, where it is.
        item_location craft = target.item;
        if( refusal no = reach_refusal( who, craft, false ) ) {
            return no;
        }
        if( !craft->is_craft() ) {
            return string_format( _( "The %s is not a craft in progress." ), craft->tname() );
        }
        const recipe &making = craft->get_making();
        if( !who.has_morale_to_craft() ) {
            return std::string( _( "Not in the mood to craft." ) );
        }
        if( who.lighting_craft_speed_multiplier( making ) <= 0.0f ) {
            return std::string( _( "It's too dark to craft." ) );
        }
        const requirement_data continue_reqs = craft->get_continue_reqs();
        if( !continue_reqs.is_empty() &&
            !continue_reqs.can_make_with_inventory( &who, who.crafting_inventory(),
                    making.get_component_filter(), 1 ) ) {
            return string_format( _( "The components to go on with the %s aren't at hand." ),
                                  craft->tname() );
        }
        const std::string name = craft->tname();
        {
            // Going on can ask to consume missing components and to choose tools again.
            const craft_prompt_suppressor no_prompts;
            who.assign_activity( craft_activity_actor( craft, false ) );
        }
        if( !now_doing( who, { ACT_CRAFT, ACT_CRAFT_WAIT } ) ) {
            return string_format( _( "Working on the %s stopped as soon as it started." ), name );
        }
        return std::nullopt;
    }

    const recipe &making = target.recipe->obj();
    if( making.is_null() || making.obsolete || making.is_practice() ) {
        return string_format( _( "%s can't be crafted." ), target.recipe->str() );
    }
    if( !who.has_recipe( &making ) ) {
        return string_format( _( "%1$s doesn't know how to make %2$s." ), who.get_name(),
                              making.result_name() );
    }
    if( !making.character_has_required_proficiencies( who ) ) {
        return string_format( _( "%1$s lacks the proficiencies to make %2$s." ), who.get_name(),
                              making.result_name() );
    }
    if( !making.character_meets_requirements( who ) ) {
        return string_format( _( "%1$s doesn't meet the requirements to make %2$s." ),
                              who.get_name(), making.result_name() );
    }
    if( !who.has_morale_to_craft() ) {
        return std::string( _( "Not in the mood to craft." ) );
    }
    if( who.lighting_craft_speed_multiplier( making ) <= 0.0f ) {
        return std::string( _( "It's too dark to craft." ) );
    }
    // As the crafting menu answers its own questions: rotten components are fine, favorited
    // ones are not.
    if( !who.can_start_craft( &making, recipe_filter_flags::no_favorite, 1 ) ) {
        return string_format( _( "The components or tools to make %s aren't at hand." ),
                              making.result_name() );
    }
    std::optional<tripoint_bub_ms> place;
    if( target.loc ) {
        const tripoint_bub_ms pos = here.get_bub( *target.loc );
        if( !here.inbounds( pos ) || !within_reach( who.pos_abs(), *target.loc ) ) {
            return std::string( _( "The place to craft at is out of reach." ) );
        }
        if( !here.can_put_items_ter_furn( pos ) ) {
            return string_format( _( "Nothing can be put on the %s." ), here.name( pos ) );
        }
        place = pos;
    }
    {
        const craft_prompt_suppressor no_prompts;
        craft_command command( &making, 1, false, &who, place );
        command.execute();
    }
    if( !now_doing( who, { ACT_CRAFT, ACT_CRAFT_WAIT } ) ) {
        return string_format( _( "Crafting %s stopped as soon as it started." ),
                              making.result_name() );
    }
    return std::nullopt;
}

refusal start_disassemble( const activity_id &, Character &who,
                           const eoc_activity_target &target )
{
    item_location it = target.item;
    if( refusal no = reach_refusal( who, it, false ) ) {
        return no;
    }
    const item &obj = *it;
    const ret_val<void> can = who.can_disassemble( obj, who.crafting_inventory() );
    if( !can.success() ) {
        return std::string( can.c_str() );
    }
    // What Character::disassemble asks about.
    if( obj.is_favorite ) {
        return string_format( _( "The %s is a favorite." ), obj.tname() );
    }
    if( it.carrier() != nullptr && it.carrier()->is_worn( obj ) ) {
        return string_format( _( "The %s is being worn." ), obj.tname() );
    }
    if( refusal no = ownership_refusal( who, obj ) ) {
        return no;
    }
    // A stack counted by charges is taken apart whole, as "disassemble all" does.
    const bool in_progress = obj.typeId() == itype_disassembly;
    const recipe &dis = in_progress ? obj.get_making() :
                        recipe_dictionary::get_uncraft( obj.typeId() );
    const int num_dis = obj.count_by_charges() ? obj.charges : 1;
    const int num = in_progress ? obj.get_making_batch_size() : num_dis;
    const int64_t craft_moves = dis.time_to_craft_moves( who, {},
                                recipe_time_flag::ignore_proficiencies );
    player_activity act( disassemble_activity_actor( static_cast<int>( num * craft_moves ) ) );
    act.targets.emplace_back( it );
    // index: no recursive disassembly; position: how many of the stack
    act.index = false;
    act.position = num_dis;
    act.values.push_back( 1 );
    const std::string name = obj.tname();
    {
        // Starting places the disassembly, which may ask where.
        const craft_prompt_suppressor no_prompts;
        who.assign_activity( act );
    }
    if( !now_doing( who, { ACT_DISASSEMBLE } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    dis.apply_negative_morale_mods( who );
    return std::nullopt;
}

refusal start_read( const activity_id &, Character &who, const eoc_activity_target &target )
{
    item_location book = target.item;
    if( refusal no = reach_refusal( who, book, false ) ) {
        return no;
    }
    if( !book->is_book() ) {
        return string_format( _( "The %s is not good reading material." ), book->tname() );
    }
    if( book->get_use( "learn_spell" ) != nullptr ) {
        return string_format( _( "The %s is studied by using it, not by reading it." ),
                              book->tname() );
    }
    std::vector<std::string> reasons;
    const Character *reader = who.get_book_reader( *book, reasons );
    if( reader == nullptr ) {
        return reasons.empty() ? string_format( _( "%s can't read that." ), who.get_name() ) :
               reasons.front();
    }
    if( book->type->can_use( "MA_MANUAL" ) ) {
        // The actor ends at once for an NPC: they can't learn from manuals yet.
        if( !who.is_avatar() ) {
            return std::string( _( "Only the avatar can train from a martial arts manual." ) );
        }
        if( who.martial_arts_data->has_martialart( martial_art_learned_from( *book->type ) ) ) {
            return std::string( _( "You already know all this book has to teach." ) );
        }
        if( who.get_stamina() < who.get_stamina_max() / 10 ) {
            return std::string( _( "You are too exhausted to train martial arts." ) );
        }
    }
    // Read once, as the reading menu's first choice. An NPC times its reading its own way.
    const npc *as_npc = who.as_npc();
    const time_duration time_taken = as_npc != nullptr ? as_npc->time_to_read( *book, *reader ) :
                                     who.time_to_read( *book, *reader );
    item_location ereader;
    who.assign_activity( read_activity_actor( time_taken, book, ereader, false ) );
    if( !now_doing( who, { ACT_READ } ) ) {
        return did_not_start( *book );
    }
    return std::nullopt;
}

refusal start_reload( const activity_id &, Character &who, const eoc_activity_target &target )
{
    item_location it = target.item;
    if( !it || it.get_item() == nullptr ) {
        return std::string( _( "The item is gone." ) );
    }
    // As the reload action: the item has to be carried, unless it is used where it stands.
    if( refusal no = reach_refusal( who, it, !it->has_flag( flag_ALLOWS_REMOTE_USE ) ) ) {
        return no;
    }
    // For holsters and ammo pouches, reload what they hold.
    if( it->type->can_use( "holster" ) && it->num_item_stacks() == 1 ) {
        it = item_location( it, &it->only_item() );
    }
    if( !who.can_reload( *it ) ) {
        return string_format( _( "The %s can't be reloaded now." ), it->tname() );
    }
    std::vector<item::reload_option> options;
    who.list_ammo( it, options, false );
    // Frozen ammo would ask to be crushed first.
    options.erase( std::remove_if( options.begin(), options.end(),
    []( const item::reload_option & opt ) {
        return !opt.ammo || opt.ammo->is_frozen_liquid();
    } ), options.end() );
    if( options.empty() ) {
        return string_format( _( "Nothing at hand reloads the %s." ), it->tname() );
    }
    // The quickest, then the fullest, as an NPC chooses.
    const auto best = std::min_element( options.begin(), options.end(),
    []( const item::reload_option & lhs, const item::reload_option & rhs ) {
        if( lhs.moves() != rhs.moves() ) {
            return lhs.moves() < rhs.moves();
        }
        return lhs.ammo->ammo_remaining() > rhs.ammo->ammo_remaining();
    } );
    const std::string name = it->tname();
    const int extra_moves = it->get_var( "dirt", 0 ) > 7800 ? 2500 : 0;
    who.assign_activity( reload_activity_actor( std::move( *best ), extra_moves ) );
    if( !now_doing( who, { ACT_RELOAD } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    return std::nullopt;
}

// Whether unloading `obj` would pour out liquid, which asks the player where to.
bool unload_pours_liquid( const item &obj )
{
    for( const pocket_type pocket : {
             pocket_type::CONTAINER, pocket_type::MAGAZINE_WELL, pocket_type::MAGAZINE
         } ) {
        for( const item *contained : obj.all_items_top( pocket ) ) {
            if( contained->made_of( phase_id::LIQUID ) ) {
                return true;
            }
        }
    }
    return obj.ammo_remaining() > 0 && !obj.ammo_current().is_null() &&
           item( obj.ammo_current() ).made_of( phase_id::LIQUID );
}

// How many sources Character::unload would ask the player to choose between.
int unload_choices( const item &obj )
{
    int choices = 0;
    for( const item *mod : obj.gunmods() ) {
        if( ( mod->is_gun() && !mod->has_flag( flag_NO_UNLOAD ) &&
              ( mod->magazine_current() || mod->ammo_remaining() > 0 || mod->casings_count() > 0 ) ) ||
            ( mod->has_flag( flag_BRASS_CATCHER ) && !mod->is_container_empty() ) ) {
            ++choices;
        }
    }
    if( obj.is_magazine() ) {
        return choices + 1;
    }
    choices += static_cast<int>( obj.magazines_current().size() );
    if( obj.uses_firing_requirements() ) {
        for( const item_pocket *pocket : obj.get_pockets( []( const item_pocket & p ) {
        return p.is_type( pocket_type::MAGAZINE );
        } ) ) {
            const std::list<const item *> held = pocket->all_items_top();
            if( std::any_of( held.begin(), held.end(), []( const item * e ) {
            return !e->has_flag( flag_CASING ) && e->is_ammo();
            } ) ) {
                ++choices;
            }
        }
    }
    return std::max( choices, 1 );
}

refusal start_unload( const activity_id &, Character &who, const eoc_activity_target &target )
{
    item_location it = target.item;
    if( refusal no = reach_refusal( who, it, false ) ) {
        return no;
    }
    item &obj = *it;
    const std::string name = obj.tname();
    if( unload_pours_liquid( obj ) ) {
        return string_format( _( "Unloading the %s would pour out liquid." ), name );
    }
    // What Character::unload refuses or asks about.
    if( obj.is_container() ) {
        if( obj.empty() ) {
            return string_format( _( "The %s is already empty!" ), name );
        }
        if( !obj.can_unload() ) {
            return string_format( _( "The %s can't be unloaded in its current state!" ), name );
        }
    } else {
        if( !obj.has_flag( flag_BRASS_CATCHER ) ) {
            if( ( !obj.is_magazine() && !obj.uses_magazine() ) || obj.has_flag( flag_NO_UNLOAD ) ) {
                return string_format( _( "You can't unload a %s!" ), name );
            }
            if( !obj.magazine_current() && obj.ammo_remaining() <= 0 && obj.casings_count() <= 0 ) {
                return string_format( _( "The %s isn't loaded." ), name );
            }
        }
        if( unload_choices( obj ) > 1 ) {
            return string_format( _( "The %s has several things to unload; which one is a "
                                     "choice." ), name );
        }
    }
    // A container or a magazine is emptied over time; a gun or tool gives up its magazine or
    // ammo at once, with no activity.
    const bool takes_time = obj.is_container() || obj.is_magazine();
    if( !who.unload( it ) ) {
        return string_format( _( "Unloading the %s failed." ), name );
    }
    if( takes_time && !now_doing( who, { ACT_UNLOAD } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    return std::nullopt;
}

refusal start_wear( const activity_id &, Character &who, const eoc_activity_target &target )
{
    item_location it = target.item;
    if( refusal no = reach_refusal( who, it, false ) ) {
        return no;
    }
    if( who.is_worn( *it ) ) {
        return string_format( _( "%1$s is already wearing the %2$s." ), who.get_name(), it->tname() );
    }
    const ret_val<void> can = who.can_wear( *it );
    if( !can.success() ) {
        return std::string( can.c_str() );
    }
    // The wear activity asks the avatar before taking what isn't theirs.
    if( refusal no = ownership_refusal( who, *it ) ) {
        return no;
    }
    const std::string name = it->tname();
    who.assign_activity( wear_activity_actor( { it }, { 0 } ) );
    if( !now_doing( who, { ACT_WEAR } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    return std::nullopt;
}

// --- The fields of the other activities ---

// What an activity does with each target field.
enum class takes : int {
    no,
    may,
    must
};

// Why `given` doesn't fit an activity that takes `field` as `how`; nullopt if it does.
refusal field_error( const char *field, const takes how, const bool given )
{
    if( how == takes::must && !given ) {
        return string_format( "needs \"%s\"", field );
    }
    if( how == takes::no && given ) {
        return string_format( "takes no \"%s\"", field );
    }
    return std::nullopt;
}

// An activity that takes "target_item", "target_loc" and "target_monster" as given, and no
// "recipe" or "butcher_type".
template<takes Item, takes Loc, takes Monster>
refusal fields_of( const eoc_target_fields &given )
{
    if( refusal no = field_error( "target_item", Item, given.item ) ) {
        return no;
    }
    if( refusal no = field_error( "target_loc", Loc, given.loc ) ) {
        return no;
    }
    if( refusal no = field_error( "target_monster", Monster, given.creature ) ) {
        return no;
    }
    if( refusal no = field_error( "recipe", takes::no, given.recipe ) ) {
        return no;
    }
    return field_error( "butcher_type", takes::no, given.butcher_type );
}

// --- Places, tools and monsters ---

// The tile at "target_loc", when `who` can work on it: next to them on the same level, or (with
// `own_tile`) their own tile. Sets `pos`.
refusal tile_refusal( const Character &who, const eoc_activity_target &target, const bool own_tile,
                      tripoint_bub_ms &pos )
{
    map &here = get_map();
    if( !target.loc ) {
        return std::string( _( "There is no place to work at." ) );
    }
    pos = here.get_bub( *target.loc );
    if( !here.inbounds( pos ) || !within_reach( who.pos_abs(), *target.loc ) ) {
        return std::string( _( "The place to work at is out of reach." ) );
    }
    if( !own_tile && *target.loc == who.pos_abs() ) {
        return string_format( _( "%s can't do that on their own tile." ), who.get_name() );
    }
    return std::nullopt;
}

// The tool `who` works with: the "target_item", which has to be carried, or else the best one
// they carry. `rating` is how good an item is for the work, 0 or less if it can't do it.
refusal pick_tool( Character &who, const eoc_activity_target &target,
                   const std::function<int( const item & )> &rating, const std::string &none,
                   item_location &tool )
{
    if( target.item ) {
        if( refusal no = reach_refusal( who, target.item, true ) ) {
            return no;
        }
        if( rating( *target.item ) <= 0 ) {
            return string_format( _( "The %s can't do that." ), target.item->tname() );
        }
        tool = target.item;
        return std::nullopt;
    }
    item *best = nullptr;
    int best_rating = 0;
    for( item *it : who.items_with( [&rating]( const item & i ) {
    return rating( i ) > 0;
    } ) ) {
        const int r = rating( *it );
        if( r > best_rating ) {
            best = it;
            best_rating = r;
        }
    }
    if( best == nullptr ) {
        return none;
    }
    tool = item_location( who, best );
    return std::nullopt;
}

// How good `it` is as a tool with the use action `use`: its `quality`, at least 1; 0 if it has no
// such use action.
int tool_rating( const item &it, const std::string &use, const quality_id &quality )
{
    if( !it.type->can_use( use ) ) {
        return 0;
    }
    return std::max( 1, it.get_quality( quality ) );
}

// Why `who` can't work on the monster that is the other talker; nullopt if they can. It has to be
// next to them, on the same level.
refusal monster_refusal( const Character &who, const monster *mon )
{
    if( mon == nullptr || mon->is_dead() ) {
        return std::string( _( "The creature is gone." ) );
    }
    if( !within_reach( who.pos_abs(), mon->pos_abs() ) || mon->pos_abs() == who.pos_abs() ) {
        return string_format( _( "The %s is out of reach." ), mon->get_name() );
    }
    return std::nullopt;
}

refusal started_or_not( const Character &who, const std::vector<activity_id> &types,
                        const std::string &what )
{
    if( !now_doing( who, types ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), what );
    }
    return std::nullopt;
}

// --- Items ---

refusal start_consume( const activity_id &, Character &who, const eoc_activity_target &target )
{
    item_location food = target.item;
    if( refusal no = reach_refusal( who, food, false ) ) {
        return no;
    }
    if( !food->is_comestible() ) {
        return string_format( _( "The %s is not something to eat or drink." ), food->tname() );
    }
    if( food->is_medication() && !who.can_use_heal_item( *food ) ) {
        return string_format( _( "%1$s's biology is not compatible with the %2$s." ), who.get_name(),
                              food->tname() );
    }
    // Without asking: anything the avatar would be asked about (rotten, allergy, too full...)
    // is a refusal.
    const ret_val<edible_rating> edible = who.will_eat( *food, false );
    if( !edible.success() ) {
        return edible.str();
    }
    if( refusal no = ownership_refusal( who, *food ) ) {
        return no;
    }
    // As the consume activity: an NPC eats only what serves a need.
    if( const npc *guy = who.as_npc() ) {
        if( !guy->may_eat_from( food ) ) {
            return string_format( _( "%1$s has no need of the %2$s now." ), who.get_name(),
                                  food->tname() );
        }
    }
    const std::string name = food->tname();
    who.assign_activity( consume_activity_actor( food ) );
    return started_or_not( who, { ACT_CONSUME }, name );
}

refusal start_drop( const activity_id &, Character &who, const eoc_activity_target &target )
{
    item_location it = target.item;
    if( !it || it.get_item() == nullptr ) {
        return std::string( _( "The item is gone." ) );
    }
    if( it.carrier() != &who ) {
        return string_format( _( "%1$s isn't carrying the %2$s." ), who.get_name(), it->tname() );
    }
    tripoint_bub_ms pos = who.pos_bub();
    if( target.loc ) {
        if( refusal no = tile_refusal( who, target, true, pos ) ) {
            return no;
        }
        if( !get_map().can_put_items( pos ) ) {
            return string_format( _( "Nothing can be put on the %s." ), get_map().name( pos ) );
        }
    }
    ret_val<void> can = who.can_drop( *it );
    if( can.success() && who.is_worn( *it ) ) {
        can = who.can_takeoff( *it );
    }
    if( !can.success() ) {
        return std::string( can.c_str() );
    }
    const std::string name = it->tname();
    const int count = it->count_by_charges() ? it->charges : 1;
    who.assign_activity( drop_activity_actor( { drop_or_stash_item_info( it, count ) },
                         pos - who.pos_bub(), false ) );
    return started_or_not( who, { ACT_DROP }, name );
}

refusal start_firstaid( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // As the healing item's use: on the talker, with what it carries.
    item_location med = target.item;
    if( refusal no = reach_refusal( who, med, true ) ) {
        return no;
    }
    item *used = med->get_usable_item( "heal" );
    const use_function *heal_use = used == nullptr ? nullptr : used->get_use( "heal" );
    const heal_actor *actor = heal_use == nullptr ? nullptr :
                              dynamic_cast<const heal_actor *>( heal_use->get_actor_ptr() );
    if( actor == nullptr ) {
        return string_format( _( "The %s doesn't heal." ), med->tname() );
    }
    if( who.cant_do_underwater( false ) || who.cant_do_mounted( false ) ) {
        return string_format( _( "%s can't do that here." ), who.get_name() );
    }
    if( med->is_filthy() ) {
        return std::string( _( "You can't use filthy items for healing." ) );
    }
    if( !who.can_use_heal_item( *used ) ) {
        return string_format( _( "%1$s's biology is not compatible with the %2$s." ), who.get_name(),
                              med->tname() );
    }
    // The part an NPC would choose: the most hurt one the item helps (the avatar would be asked).
    bodypart_id healed = bodypart_str_id::NULL_ID();
    int highest = 0;
    for( const bodypart_id &part : who.get_all_body_parts( get_body_part_flags::only_main ) ) {
        int damage = 0;
        if( ( !who.has_effect( effect_bandaged, part ) && actor->bandages_power > 0 ) ||
            ( !who.has_effect( effect_disinfected, part ) && actor->disinfectant_power > 0 ) ) {
            damage += who.get_part_hp_max( part ) - who.get_part_hp_cur( part );
            damage += actor->infect * who.get_effect_dur( effect_infected, part ) / 10_minutes;
        }
        if( who.get_effect_int( effect_bleed, part ) > 5 && actor->bleed > 0 ) {
            damage += actor->bleed * who.get_effect_dur( effect_bleed, part ) / 5_minutes;
        }
        if( who.has_effect( effect_bite, part ) && actor->disinfectant_power > 0 ) {
            damage += actor->bite * who.get_effect_dur( effect_bite, part ) / 1_minutes;
        }
        damage += static_cast<int>( actor->cure_urgency( who, part ) );
        if( damage > highest ) {
            highest = damage;
            healed = part;
        }
    }
    if( healed == bodypart_str_id::NULL_ID() ) {
        return string_format( _( "The %1$s can't help %2$s now." ), med->tname(), who.get_name() );
    }
    // As heal_actor::use: each of the action's proficiencies (wound care) cuts the time.
    const int cost = actor->get_move_cost( who );
    const std::string name = med->tname();
    who.assign_activity( firstaid_activity_actor( cost, name, who.getID(), false,
                         heal_use->get_id() ) );
    if( !now_doing( who, { ACT_FIRSTAID } ) ) {
        return did_not_start( *med );
    }
    who.activity.targets.emplace_back( med );
    who.activity.str_values.emplace_back( healed.id().str() );
    return std::nullopt;
}

refusal start_gunmod_remove( const activity_id &, Character &who,
                             const eoc_activity_target &target )
{
    // The item is the gunmod, on a gun the talker carries.
    item_location mod = target.item;
    if( refusal no = reach_refusal( who, mod, true ) ) {
        return no;
    }
    if( !mod->is_gunmod() || !mod.has_parent() || !mod.parent_item()->is_gun() ) {
        return string_format( _( "The %s is not a mod on a gun." ), mod->tname() );
    }
    item_location gun = mod.parent_item();
    const std::vector<item *> mods = gun->gunmods();
    const auto found = std::find( mods.begin(), mods.end(), mod.get_item() );
    if( found == mods.end() || mod->is_irremovable() ) {
        return string_format( _( "The %s can't be removed." ), mod->tname() );
    }
    // What the gunmod removal menu turns down.
    const std::string location = mod->type->gunmod->location.str();
    if( ( location == "magazine" || location == "mechanism" || location == "loading port" ||
          location == "bore" ) && ( gun->ammo_remaining() > 0 || gun->magazine_current() ) ) {
        return string_format( _( "The %s must be unloaded before removing this mod." ), gun->tname() );
    }
    for( const std::pair<const gunmod_location, int> &slot : mod->type->gunmod->add_mod ) {
        if( slot.second > 0 && gun->get_free_mod_locations( slot.first ) <= 0 ) {
            return string_format( _( "The %s has mods attached." ), mod->tname() );
        }
    }
    // Unloading the mod could ask what to do with its ammo.
    if( !mod->has_flag( flag_BRASS_CATCHER ) && mod->ammo_remaining() > 0 ) {
        return string_format( _( "The %s has to be unloaded first." ), mod->tname() );
    }
    const std::string name = mod->tname();
    // As Character::gunmod_remove: half the time of installing it.
    const int moves = to_moves<int>( mod->type->gunmod->install_time ) / 2;
    who.assign_activity( gunmod_remove_activity_actor( moves, gun,
                         static_cast<int>( found - mods.begin() ) ) );
    return started_or_not( who, { ACT_GUNMOD_REMOVE }, name );
}

refusal start_insert( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // The item goes into the container the talker wields.
    item_location it = target.item;
    if( refusal no = reach_refusal( who, it, false ) ) {
        return no;
    }
    item_location container = who.get_wielded_item();
    if( !container || container->get_container_pockets().empty() ) {
        return string_format( _( "%s isn't wielding a container to put it in." ), who.get_name() );
    }
    if( container == it || it.eventually_contains( container ) ) {
        return std::string( _( "You can't put an item inside itself." ) );
    }
    if( container->has_flag( flag_NO_RELOAD ) ) {
        return string_format( _( "Nothing can be put into the %s." ), container->tname() );
    }
    if( it->is_bucket_nonempty() ) {
        return string_format( _( "The %s would spill." ), it->tname() );
    }
    const ret_val<void> fits = it->count_by_charges() ?
                               container->can_contain_partial_directly( *it ) :
                               container->can_contain_directly( *it );
    if( !fits.success() ) {
        return string_format( _( "The %1$s doesn't fit in the %2$s." ), it->tname(),
                              container->tname() );
    }
    if( refusal no = ownership_refusal( who, *it ) ) {
        return no;
    }
    const std::string name = it->tname();
    const int count = it->count_by_charges() ? it->charges : 1;
    who.assign_activity( insert_item_activity_actor( container, { { it, count } } ) );
    return started_or_not( who, { ACT_INSERT_ITEM }, name );
}

refusal start_move_items( const activity_id &, Character &who,
                          const eoc_activity_target &target )
{
    // The item is moved to "target_loc", the talker's tile or one next to it, as AIM moves it.
    item_location it = target.item;
    if( refusal no = reach_refusal( who, it, false ) ) {
        return no;
    }
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( it.where_recursive() != item_location::type::character && it.pos_bub( here ) == pos ) {
        return string_format( _( "The %s is there already." ), it->tname() );
    }
    if( who.is_worn( *it ) ) {
        return string_format( _( "The %s is being worn." ), it->tname() );
    }
    if( it->made_of_from_type( phase_id::LIQUID ) || it->made_of_from_type( phase_id::GAS ) ) {
        return string_format( _( "The %s can't be carried over." ), it->tname() );
    }
    if( !here.can_put_items( pos ) ) {
        return string_format( _( "Nothing can be put on the %s." ), here.name( pos ) );
    }
    if( refusal no = ownership_refusal( who, *it ) ) {
        return no;
    }
    const std::string name = it->tname();
    const bool to_vehicle = here.veh_at( pos ).cargo().has_value();
    who.assign_activity( move_items_activity_actor( { it }, { 0 }, to_vehicle,
                         pos - who.pos_bub() ) );
    return started_or_not( who, { ACT_MOVE_ITEMS }, name );
}

refusal start_pickup( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // The pickup activity only works for the avatar; u_pickup_item works for anyone.
    if( !who.is_avatar() ) {
        return string_format( _( "%s can't pick things up that way." ), who.get_name() );
    }
    item_location it = target.item;
    if( refusal no = reach_refusal( who, it, false ) ) {
        return no;
    }
    if( it.carrier() != nullptr ) {
        return string_format( _( "%1$s is carrying the %2$s already." ), who.get_name(), it->tname() );
    }
    const item &obj = *it;
    // What the pickup would ask about or leave.
    if( obj.made_of_from_type( phase_id::LIQUID ) || obj.made_of_from_type( phase_id::GAS ) ||
        obj.is_frozen_liquid() ) {
        return string_format( _( "The %s can't be picked up." ), obj.tname() );
    }
    if( obj.is_bucket_nonempty() ) {
        return string_format( _( "The %s would spill." ), obj.tname() );
    }
    if( !who.can_pickWeight_partial( obj, false ) ) {
        return string_format( _( "The %s is too heavy." ), obj.tname() );
    }
    if( !who.can_stash_partial( obj ) ) {
        return string_format( _( "There's no room for the %s." ), obj.tname() );
    }
    if( refusal no = ownership_refusal( who, obj ) ) {
        return no;
    }
    const std::string name = obj.tname();
    who.assign_activity( pickup_activity_actor( { it }, { 0 }, who.pos_bub(), false ) );
    return started_or_not( who, { ACT_PICKUP }, name );
}

refusal start_toolmod_add( const activity_id &, Character &who,
                           const eoc_activity_target &target )
{
    // The item is the toolmod, put on the tool the talker wields.
    item_location mod = target.item;
    if( refusal no = reach_refusal( who, mod, true ) ) {
        return no;
    }
    if( !mod->is_toolmod() ) {
        return string_format( _( "The %s is not a tool modification." ), mod->tname() );
    }
    item_location tool = who.get_wielded_item();
    if( !tool ) {
        return string_format( _( "%s isn't wielding a tool to modify." ), who.get_name() );
    }
    // As the toolmod's use action picks the tool.
    const bool power_clash = ( mod->has_flag( flag_USE_UPS ) ||
                               mod->has_flag( flag_USES_BIONIC_POWER ) ) &&
                             ( tool->has_flag( flag_IS_UPS ) || tool->has_flag( flag_USE_UPS ) ||
                               tool->has_flag( flag_USES_BIONIC_POWER ) );
    const bool compatible = std::any_of( mod->type->mod->acceptable_ammo.begin(),
    mod->type->mod->acceptable_ammo.end(), [&tool]( const ammotype & at ) {
        return tool->is_tool() && tool->type->tool->ammo_id.count( at ) != 0;
    } );
    if( power_clash || !compatible || !tool->is_tool() || !tool->toolmods().empty() ||
        tool->has_flag( flag_INTEGRATED ) ) {
        return string_format( _( "The %1$s doesn't fit the %2$s." ), mod->tname(), tool->tname() );
    }
    if( tool->magazine_current() || tool->ammo_remaining() > 0 ) {
        return string_format( _( "The %s has to be unloaded first." ), tool->tname() );
    }
    if( !who.can_use( *mod, *tool ) ) {
        return string_format( _( "%1$s can't use the %2$s." ), who.get_name(), mod->tname() );
    }
    // Installing it for good asks first.
    if( mod->is_irremovable() ) {
        return string_format( _( "The %s would be installed for good, which needs your "
                                 "confirmation." ), mod->tname() );
    }
    const std::string name = mod->tname();
    who.assign_activity( toolmod_add_activity_actor( 1_minutes, tool, mod ) );
    return started_or_not( who, { ACT_TOOLMOD_ADD }, name );
}

refusal start_wield( const activity_id &, Character &who, const eoc_activity_target &target )
{
    item_location it = target.item;
    if( refusal no = reach_refusal( who, it, false ) ) {
        return no;
    }
    if( who.is_wielding( *it ) ) {
        return string_format( _( "%1$s is already wielding the %2$s." ), who.get_name(), it->tname() );
    }
    const ret_val<void> can = who.can_wield( *it );
    if( !can.success() ) {
        return std::string( can.c_str() );
    }
    // Wielding asks what to do with the weapon in hand, and the avatar before taking what isn't
    // theirs. u_wield_item puts the weapon away by itself.
    if( const item_location weapon = who.get_wielded_item() ) {
        return string_format( _( "%1$s has to put away the %2$s first." ), who.get_name(),
                              weapon->tname() );
    }
    if( refusal no = ownership_refusal( who, *it ) ) {
        return no;
    }
    const std::string name = it->tname();
    who.assign_activity( wield_activity_actor( it, 0 ) );
    return started_or_not( who, { ACT_WIELD }, name );
}

// --- Places ---

// As the axe's use actions: the time to chop with `tool`.
int chop_moves( const Character &who, const item &tool )
{
    const int quality = tool.get_quality( qual_AXE );
    // regular tools by strength, powered ones by dexterity
    const int attr = tool.has_flag( flag_POWERED ) ? who.get_dex() : who.get_arm_str();
    const int moves = to_moves<int>( time_duration::from_minutes( 60 - attr ) /
                                     std::pow( 2, quality - 1 ) );
    const int helpers = who.get_num_crafting_helpers( 3 );
    return static_cast<int>( moves * ( 1.0f - helpers / 10.0f ) );
}

// What iexamine::portable_structure finds for the tent at `pos`: the item it packs into (a
// damaged one if it is), and the tent's radius. Nullopt if it's no tent.
std::optional<std::pair<itype_id, int>> tent_at( const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const furn_str_id fid = here.furn( pos ).id();
    if( !fid->has_examine( iexamine::portable_structure ) ) {
        return std::nullopt;
    }
    const itype_id &iid = fid->deployed_item;
    if( !item::type_is_defined( iid ) ) {
        return std::nullopt;
    }
    for( const auto &method : item::find_type( iid )->use_methods ) {
        const deploy_tent_actor *actor = dynamic_cast<const deploy_tent_actor *>
                                         ( method.second.get_actor_ptr() );
        if( actor == nullptr || !( actor->floor_center ? *actor->floor_center == fid :
                                   actor->floor == fid ) ) {
            continue;
        }
        if( actor->check_intact( pos ) ) {
            return std::make_pair( iid, actor->radius );
        }
        if( !actor->broken_type ) {
            return std::nullopt;
        }
        return std::make_pair( *actor->broken_type, actor->radius );
    }
    const std::optional<map_furn_bash_info> &bash = fid->bash;
    return std::make_pair( iid, std::max( 1, bash ? bash->collapse_radius : 0 ) );
}

refusal start_build( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Goes on with the construction in progress at "target_loc".
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    const partial_con *pc = get_map().partial_con_at( pos );
    if( pc == nullptr ) {
        return std::string( _( "There is no construction in progress there." ) );
    }
    const construction &built = pc->id.obj();
    if( who.fine_detail_vision_mod() > 4 ) {
        return std::string( _( "It is too dark to construct right now." ) );
    }
    if( !who.meets_skill_requirements( built ) ) {
        return string_format( _( "%1$s lacks the skills to work on the %2$s." ), who.get_name(),
                              built.group->name() );
    }
    who.assign_activity( build_construction_activity_actor( *target.loc ) );
    return started_or_not( who, { ACT_BUILD }, built.group->name() );
}

refusal start_chop_logs( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // As the axe's use: a tree trunk or a stump next to the talker.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( here.ter( pos ) != ter_t_trunk && here.ter( pos ) != ter_t_stump ) {
        return string_format( _( "There is no tree trunk to chop at the %s." ), here.name( pos ) );
    }
    if( who.cant_do_mounted( false ) ) {
        return string_format( _( "%s can't do that while mounted." ), who.get_name() );
    }
    item_location tool;
    if( refusal no = pick_tool( who, target, []( const item & it ) {
    return tool_rating( it, "CHOP_LOGS", qual_AXE );
    }, string_format( _( "%s has nothing to chop wood with." ), who.get_name() ), tool ) ) {
        return no;
    }
    const std::string name = here.name( pos );
    who.assign_activity( chop_logs_activity_actor( chop_moves( who, *tool ), tool ) );
    if( !now_doing( who, { ACT_CHOP_LOGS } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    who.activity.placement = *target.loc;
    return std::nullopt;
}

// Why the avatar would be warned before angering the faction whose camp it is in; nullopt if not.
refusal faction_warning( Character &who )
{
    if( !who.is_avatar() ) {
        return std::nullopt;
    }
    basecamp *camp = basecamp::any_claim_at( who.pos_abs_omt() );
    if( camp == nullptr || camp->allowed_access_by( who, false ) ||
        camp->get_owner()->guaranteed_hostile() ) {
        return std::nullopt;
    }
    return string_format( _( "This is %s's territory; doing that would need your confirmation." ),
                          camp->get_owner()->get_name() );
}

refusal start_chop_tree( const activity_id &, Character &who, const eoc_activity_target &target )
{
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( !here.has_flag( ter_furn_flag::TFLAG_TREE, pos ) ) {
        return string_format( _( "There is no tree to chop down at the %s." ), here.name( pos ) );
    }
    if( who.cant_do_mounted( false ) ) {
        return string_format( _( "%s can't do that while mounted." ), who.get_name() );
    }
    if( refusal no = faction_warning( who ) ) {
        return no;
    }
    item_location tool;
    if( refusal no = pick_tool( who, target, []( const item & it ) {
    return tool_rating( it, "CHOP_TREE", qual_AXE );
    }, string_format( _( "%s has nothing to chop a tree down with." ), who.get_name() ), tool ) ) {
        return no;
    }
    const std::string name = here.name( pos );
    // The tree falls where nobody is, without asking.
    who.assign_activity( chop_tree_activity_actor( chop_moves( who, *tool ), tool, true ) );
    if( !now_doing( who, { ACT_CHOP_TREE } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    who.activity.placement = *target.loc;
    return std::nullopt;
}

refusal start_clear_rubble( const activity_id &, Character &who,
                            const eoc_activity_target &target )
{
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( !here.furn( pos )->has_examine( iexamine::rubble ) ) {
        return string_format( _( "There is no rubble to clear at the %s." ), here.name( pos ) );
    }
    // As examining the rubble.
    int moves;
    if( who.has_quality( qual_DIG, 3 ) || who.has_trait( trait_BURROW ) ||
        who.has_trait( trait_BURROWLARGE ) ) {
        moves = to_moves<int>( 1_minutes );
    } else if( who.has_quality( qual_DIG, 2 ) ) {
        moves = to_moves<int>( 2_minutes );
    } else {
        return string_format( _( "%s has no shovel." ), who.get_name() );
    }
    if( here.veh_at( pos ) || here.can_see_trap_at( pos, who ) ||
        get_creature_tracker().creature_at( pos ) != nullptr ) {
        return string_format( _( "Clearing the %s there would need your confirmation." ),
                              here.furnname( pos ) );
    }
    const std::string name = here.furnname( pos );
    who.assign_activity( clear_rubble_activity_actor( moves ) );
    if( !now_doing( who, { ACT_CLEAR_RUBBLE } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    who.activity.placement = *target.loc;
    return std::nullopt;
}

refusal start_fertilize( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // The item is the fertilizer, put on the plant at "target_loc".
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    item_location fertilizer = target.item;
    if( refusal no = reach_refusal( who, fertilizer, true ) ) {
        return no;
    }
    const ret_val<void> can = iexamine::can_fertilize( who, pos, fertilizer->typeId() );
    if( !can.success() ) {
        return std::string( can.c_str() );
    }
    const std::string name = get_map().name( pos );
    who.assign_activity( fertilize_plant_activity_actor( 10_seconds, pos, fertilizer->typeId() ) );
    return started_or_not( who, { ACT_FERTILIZE_PLANT }, name );
}

refusal start_fish( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // As the fishing rod's use: water next to the talker, for 5 hours.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( !here.has_flag( ter_furn_flag::TFLAG_FISHABLE, pos ) ) {
        return string_format( _( "%s can't fish there." ), who.get_name() );
    }
    if( who.cant_do_mounted( false ) ) {
        return string_format( _( "%s can't do that while mounted." ), who.get_name() );
    }
    item_location rod;
    if( refusal no = pick_tool( who, target, []( const item & it ) {
    return it.type->can_use( "FISH_ROD" ) ? 1 : 0;
    }, string_format( _( "%s has no fishing rod." ), who.get_name() ), rod ) ) {
        return no;
    }
    const std::string name = rod->tname();
    who.assign_activity( fish_activity_actor( rod, g->get_fishable_locations_abs( MAX_VIEW_DISTANCE,
                         pos ), 5_hours ) );
    return started_or_not( who, { ACT_FISH }, name );
}

refusal start_hacking( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Hacks the card reader, gas pump or electronic gun safe at "target_loc", as examining it
    // does when the talker has a hacking tool.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    const furn_t &furn = here.furn( pos ).obj();
    const ter_t &ter = here.ter( pos ).obj();
    if( !ter.has_examine( iexamine::pay_gas ) && !furn.has_examine( iexamine::pay_gas ) &&
        !ter.has_examine( "cardreader" ) && !furn.has_examine( "cardreader" ) &&
        !ter.has_examine( iexamine::gunsafe_el ) && !furn.has_examine( iexamine::gunsafe_el ) ) {
        return string_format( _( "There is nothing to hack at the %s." ), here.name( pos ) );
    }
    if( who.has_trait( trait_ILLITERATE ) ) {
        return string_format( _( "%s can't read." ), who.get_name() );
    }
    item_location tool;
    if( refusal no = pick_tool( who, target, [&who]( const item & it ) {
    return it.ammo_sufficient( &who ) ? it.get_quality( qual_HACK ) : 0;
    }, string_format( _( "%s has no hacking tool with enough charges." ), who.get_name() ),
    tool ) ) {
        return no;
    }
    const std::string name = here.name( pos );
    // As examining: starting uses a charge of the tool.
    tool->consume_tool_uses( 1, here, tool.pos_bub( here ), &who );
    who.assign_activity( hacking_activity_actor( tool ) );
    if( !now_doing( who, { ACT_HACKING } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    who.activity.placement = *target.loc;
    return std::nullopt;
}

refusal start_harvest( const activity_id &, Character &who, const eoc_activity_target &target )
{
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    const harvest_id harvest = get_map().get_harvest( pos );
    if( harvest.is_null() || harvest->empty() ) {
        return std::string( _( "Nothing can be harvested there now." ) );
    }
    const std::string name = get_map().name( pos );
    who.assign_activity( harvest_activity_actor( *target.loc ) );
    return started_or_not( who, { ACT_HARVEST }, name );
}

refusal start_lockpick( const activity_id &, Character &who, const eoc_activity_target &target )
{
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    const optional_vpart_position vpart = here.veh_at( pos );
    if( !here.has_flag( ter_furn_flag::TFLAG_PICKABLE, pos ) &&
        !( vpart && vpart->vehicle().next_part_to_unlock( vpart->part_index() ) >= 0 ) ) {
        return string_format( _( "The %s can't be picked." ), here.name( pos ) );
    }
    if( who.cant_do_mounted( false ) ) {
        return string_format( _( "%s can't do that while mounted." ), who.get_name() );
    }
    item_location pick;
    if( refusal no = pick_tool( who, target, []( const item & it ) {
    return tool_rating( it, "PICK_LOCK", qual_LOCKPICK );
    }, string_format( _( "%s has no lockpick." ), who.get_name() ), pick ) ) {
        return no;
    }
    const std::string name = here.name( pos );
    who.assign_activity( lockpick_activity_actor::use_item( pick, *target.loc, who ) );
    return started_or_not( who, { ACT_LOCKPICK }, name );
}

refusal start_mine( const activity_id &type, Character &who, const eoc_activity_target &target )
{
    // As the pickaxe's or jackhammer's use.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    const bool jackhammer = type == ACT_JACKHAMMER;
    item_location tool;
    if( refusal no = pick_tool( who, target, [jackhammer, &who]( const item & it ) {
    return it.type->can_use( jackhammer ? "JACKHAMMER" : "PICKAXE" ) &&
               it.ammo_sufficient( &who ) ? 1 : 0;
    }, string_format( _( "%s has nothing to dig with." ), who.get_name() ), tool ) ) {
        return no;
    }
    if( who.cant_do_mounted( false ) || who.cant_do_underwater( false ) ) {
        return string_format( _( "%s can't do that here." ), who.get_name() );
    }
    if( here.impassable_field_at( pos ) ) {
        return string_format( _( "%s can't get through what is there." ), who.get_name() );
    }
    const bool mineable_furn = here.has_flag_furn( ter_furn_flag::TFLAG_MINEABLE, pos );
    if( !mineable_furn && !here.has_flag_ter( ter_furn_flag::TFLAG_MINEABLE, pos ) ) {
        return string_format( _( "The %s can't be dug through." ), here.name( pos ) );
    }
    if( here.veh_at( pos ) ) {
        return std::string( _( "There's a vehicle in the way!" ) );
    }
    if( jackhammer && here.has_flag_ter( ter_furn_flag::TFLAG_WALL, pos ) ) {
        return string_format( _( "You can't mine a wall with a %s!" ), tool->tname() );
    }
    time_duration digging_time = 30_minutes;
    if( here.has_flag( ter_furn_flag::TFLAG_FLAT, pos ) ) {
        digging_time /= 2;
    }
    const std::string name = mineable_furn ? here.furnname( pos ) : here.tername( pos );
    if( jackhammer ) {
        who.assign_activity( jackhammer_activity_actor( tool, *target.loc, digging_time ) );
    } else {
        who.assign_activity( pickaxe_activity_actor( tool, *target.loc, digging_time ) );
    }
    return started_or_not( who, { type }, name );
}

refusal start_plant_seed( const activity_id &, Character &who,
                          const eoc_activity_target &target )
{
    // The item is the seed, planted in the dirt mound at "target_loc", as examining it does.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    item_location seed = target.item;
    if( refusal no = reach_refusal( who, seed, true ) ) {
        return no;
    }
    if( !seed->is_seed() ) {
        return string_format( _( "The %s is not a seed." ), seed->tname() );
    }
    map &here = get_map();
    if( !here.furn( pos )->has_examine( iexamine::dirtmound ) &&
        !here.ter( pos )->has_examine( iexamine::dirtmound ) ) {
        return string_format( _( "Nothing can be planted in the %s." ), here.name( pos ) );
    }
    if( !here.i_at( pos ).empty() ) {
        return std::string( _( "Something's lying there…" ) );
    }
    const itype_id seed_id = seed->typeId();
    const ret_val<void> warm = warm_enough_to_plant( pos, seed_id );
    if( !warm.success() ) {
        return std::string( warm.c_str() );
    }
    if( !here.has_flag_ter_or_furn( seed_id->seed->required_terrain_flag, pos ) ) {
        return std::string( _( "This type of seed can not be planted in this location." ) );
    }
    const std::string name = seed->tname();
    who.assign_activity( plant_seed_activity_actor( *target.loc, seed_id, 30_seconds ) );
    return started_or_not( who, { ACT_PLANT_SEED }, name );
}

refusal start_pulp( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Smashes the corpses at "target_loc" that could revive.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    bool any = false;
    const pulp_data ability = g->calculate_character_ability_to_pulp( who );
    for( const item &corpse : get_map().i_at( pos ) ) {
        if( !corpse.is_corpse() || !corpse.can_revive() || corpse.get_mtype() == nullptr ) {
            continue;
        }
        const mtype &type = *corpse.get_mtype();
        if( !g->can_pulp_acid_corpse( who, type ) ) {
            return string_format( _( "Pulping the %s would splash acid." ), corpse.tname() );
        }
        const pulp_data pd = g->calculate_pulpability( who, type, ability );
        if( !g->can_pulp_corpse( pd ) ) {
            return string_format( _( "%1$s can't pulp the %2$s." ), who.get_name(), corpse.tname() );
        }
        // The avatar is asked first about a pulping that takes long.
        if( who.is_avatar() && pd.time_to_pulp > 600 ) {
            return string_format( _( "Pulping the %s would take long, which needs your "
                                     "confirmation." ), corpse.tname() );
        }
        any = true;
    }
    if( !any ) {
        return std::string( _( "There is nothing to pulp there." ) );
    }
    who.assign_activity( pulp_activity_actor( *target.loc ) );
    return started_or_not( who, { ACT_PULP }, get_map().name( pos ) );
}

refusal start_tent_deconstruct( const activity_id &, Character &who,
                                const eoc_activity_target &target )
{
    // "target_loc" is the tent's center, as examining it.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    const std::optional<std::pair<itype_id, int>> tent = tent_at( pos );
    if( !tent ) {
        return std::string( _( "There is no tent to take down there." ) );
    }
    const std::string name = item::nname( tent->first );
    who.assign_activity( tent_deconstruct_activity_actor( to_moves<int>( 20_minutes ), tent->second,
                         pos, tent->first ) );
    return started_or_not( who, { ACT_TENT_DECONSTRUCT }, name );
}

refusal start_tent_place( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // The item is the tent, put up around "target_loc", which is its center: as the tent's use,
    // radius + 1 tiles away from the talker in one of the eight directions.
    item_location tent = target.item;
    if( refusal no = reach_refusal( who, tent, true ) ) {
        return no;
    }
    const use_function *use = tent->get_use( "deploy_tent" );
    const deploy_tent_actor *actor = use == nullptr ? nullptr :
                                     dynamic_cast<const deploy_tent_actor *>( use->get_actor_ptr() );
    if( actor == nullptr ) {
        return string_format( _( "The %s is not a tent." ), tent->tname() );
    }
    if( who.cant_do_mounted( false ) ) {
        return string_format( _( "%s can't do that while mounted." ), who.get_name() );
    }
    map &here = get_map();
    const tripoint_bub_ms center = here.get_bub( *target.loc );
    const tripoint_rel_ms offset = center - who.pos_bub();
    const int step = actor->radius + 1;
    if( offset.z() != 0 || ( offset.x() != 0 && std::abs( offset.x() ) != step ) ||
        ( offset.y() != 0 && std::abs( offset.y() ) != step ) || offset.xy() == point_rel_ms::zero ) {
        return string_format( _( "The %1$s goes up %2$d tiles away, straight or diagonally." ),
                              tent->tname(), step );
    }
    const tripoint_rel_ms direction( offset.x() / step, offset.y() / step, 0 );
    for( const tripoint_bub_ms &dest : here.points_in_radius( center, actor->radius ) ) {
        if( !here.inbounds( dest ) || here.veh_at( dest ) ||
            get_creature_tracker().creature_at( dest ) != nullptr || here.impassable( dest ) ||
            !here.has_flag( ter_furn_flag::TFLAG_FLAT, dest ) || here.has_furn( dest ) ) {
            return string_format( _( "There is no room for the %s there." ), tent->tname() );
        }
    }
    const item it = *tent;
    who.assign_activity( tent_placement_activity_actor( to_moves<int>( 20_minutes ), direction,
                         actor->radius, it, actor->wall, actor->floor, actor->floor_center, actor->door_closed ) );
    if( !now_doing( who, { ACT_TENT_PLACE } ) ) {
        return did_not_start( it );
    }
    // As the tent's use: the tent is in the activity now, and back on the ground if it's
    // cancelled.
    tent.remove_item();
    return std::nullopt;
}

// The furniture's data for `type`'s work at `pos` (prying, cutting bolts, sawing or torching
// metal), or else the terrain's; null if it can't be done there.
const activity_data_common *tile_work_data( const activity_id &type, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const activity_data_common *data = nullptr;
    if( here.has_furn( pos ) ) {
        const furn_t &furn = *here.furn( pos );
        data = type == ACT_PRYING ? furn.prying.get() : type == ACT_BOLTCUTTING ? furn.boltcut.get() :
               type == ACT_HACKSAW ? furn.hacksaw.get() : furn.oxytorch.get();
    } else if( !here.ter( pos )->is_null() ) {
        const ter_t &ter = *here.ter( pos );
        data = type == ACT_PRYING ? ter.prying.get() : type == ACT_BOLTCUTTING ? ter.boltcut.get() :
               type == ACT_HACKSAW ? ter.hacksaw.get() : ter.oxytorch.get();
    }
    return data != nullptr && data->valid() ? data : nullptr;
}

refusal start_tile_tool( const activity_id &type, Character &who,
                         const eoc_activity_target &target )
{
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    const activity_data_common *work = tile_work_data( type, pos );
    std::string use = "OXYTORCH";
    quality_id quality;
    if( type == ACT_PRYING ) {
        use = "CROWBAR";
        quality = qual_PRY;
    } else if( type == ACT_BOLTCUTTING ) {
        use = "BOLTCUTTERS";
    } else if( type == ACT_HACKSAW ) {
        use = "HACKSAW";
        quality = qual_SAW_M;
    }
    if( work == nullptr ) {
        return string_format( _( "%1$s can't do that to the %2$s." ), who.get_name(), here.name( pos ) );
    }
    if( who.cant_do_mounted( false ) ) {
        return string_format( _( "%s can't do that while mounted." ), who.get_name() );
    }
    const bool prying = type == ACT_PRYING;
    item_location tool;
    if( refusal no = pick_tool( who, target, [prying, &use, &quality]( const item & it ) {
    if( prying && it.type->can_use( "CROWBAR_WEAK" ) ) {
            return std::max( 1, it.get_quality( qual_PRY ) );
        }
        return quality.is_empty() ? ( it.type->can_use( use ) ? 1 : 0 ) :
               tool_rating( it, use, quality );
    }, string_format( _( "%s has no tool for that." ), who.get_name() ), tool ) ) {
        return no;
    }
    if( prying ) {
        const pry_data &pry = work->prying_data();
        if( pry.prying_nails ? !tool->has_quality( qual_PRYING_NAIL ) :
            tool->get_quality( qual_PRY ) < pry.prying_level ) {
            return string_format( _( "%1$s can't get sufficient leverage with the %2$s." ),
                                  who.get_name(), tool->tname() );
        }
    } else if( type == ACT_HACKSAW && tool->get_quality( qual_SAW_M ) < 2 ) {
        return string_format( _( "The %s isn't sharp enough to cut metal." ), tool->tname() );
    } else if( type == ACT_OXYTORCH ) {
        if( !who.has_quality( qual_GLARE, 1 ) ) {
            return string_format( _( "%s needs welding goggles to do that." ), who.get_name() );
        }
        // The torch's activity asks before starting a job it hasn't the fuel for.
        if( !tool->ammo_sufficient( &who, to_moves<int>( work->duration() ) / 100 ) ) {
            return string_format( _( "The %s doesn't have enough charges to complete the job." ),
                                  tool->tname() );
        }
    }
    const std::string name = here.name( pos );
    if( type == ACT_PRYING ) {
        who.assign_activity( prying_activity_actor( pos, tool ) );
    } else if( type == ACT_BOLTCUTTING ) {
        who.assign_activity( boltcutting_activity_actor( pos, tool ) );
    } else if( type == ACT_HACKSAW ) {
        who.assign_activity( hacksaw_activity_actor( pos, tool ) );
    } else {
        who.assign_activity( oxytorch_activity_actor( pos, tool ) );
    }
    return started_or_not( who, { type }, name );
}

// --- Monsters ---

// As the pet menu, which these actions are on: the monster is a pet, and `who` isn't riding.
refusal pet_refusal( const Character &who, const monster &mon )
{
    if( !mon.has_effect( effect_pet ) ) {
        return string_format( _( "The %s is not a pet." ), mon.get_name() );
    }
    if( who.is_mounted() ) {
        return string_format( _( "%s can't do that while mounted." ), who.get_name() );
    }
    return std::nullopt;
}

refusal start_milk( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Milks the monster into the container the talker wields, as examining the monster does
    // (the other talker is the monster, so it can't be the container too).
    monster *mon = target.creature;
    if( refusal no = monster_refusal( who, mon ) ) {
        return no;
    }
    if( refusal no = pet_refusal( who, *mon ) ) {
        return no;
    }
    if( !mon->has_flag( mon_flag_MILKABLE ) || mon->type->starting_ammo.empty() ) {
        return string_format( _( "The %s can't be milked." ), mon->get_name() );
    }
    const itype_id milked = mon->type->starting_ammo.begin()->first;
    const auto milkable = mon->ammo.find( milked );
    if( milkable == mon->ammo.end() || milkable->second <= 0 ) {
        return string_format( _( "The %s has no more milk." ), mon->get_name() );
    }
    item_location container = who.get_wielded_item();
    if( !container ) {
        return string_format( _( "%s isn't wielding a container to milk into." ), who.get_name() );
    }
    const item milk( milked, calendar::turn, milkable->second );
    const int fits = container->get_remaining_capacity_for_liquid( milk, true );
    if( fits <= 0 ) {
        return string_format( _( "The %1$s can't hold the %2$s." ), container->tname(), milk.tname() );
    }
    if( refusal no = ownership_refusal( who, *container ) ) {
        return no;
    }
    // As examining the monster: half a minute a unit, and the monster is tied for the time.
    const int moves_per_unit = to_moves<int>( time_duration::from_minutes( 1.0 / 2 ) );
    const int moves = std::min( fits, milkable->second ) * moves_per_unit;
    liquid_dest_opt dest;
    dest.dest_opt = LD_ITEM;
    dest.item_loc = container;
    const bool temp_tie = !mon->has_effect( effect_tied );
    if( temp_tie ) {
        mon->add_effect( effect_tied, 1_turns, true );
    }
    const std::string name = mon->get_name();
    who.assign_activity( milk_activity_actor( moves, moves_per_unit, mon->pos_abs(), dest,
                         temp_tie ) );
    if( !now_doing( who, { ACT_MILK } ) ) {
        if( temp_tie ) {
            mon->remove_effect( effect_tied );
        }
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    return std::nullopt;
}

refusal start_play_with_pet( const activity_id &, Character &who,
                             const eoc_activity_target &target )
{
    monster *mon = target.creature;
    if( refusal no = monster_refusal( who, mon ) ) {
        return no;
    }
    if( refusal no = pet_refusal( who, *mon ) ) {
        return no;
    }
    if( !mon->has_flag( mon_flag_CANPLAY ) ) {
        return string_format( _( "The %s doesn't play." ), mon->get_name() );
    }
    const std::string name = mon->get_name();
    who.assign_activity( play_with_pet_activity_actor( name, mon->type->petfood.pet ) );
    return started_or_not( who, { ACT_PLAY_WITH_PET }, name );
}

refusal start_shearing( const activity_id &, Character &who, const eoc_activity_target &target )
{
    monster *mon = target.creature;
    if( refusal no = monster_refusal( who, mon ) ) {
        return no;
    }
    if( refusal no = pet_refusal( who, *mon ) ) {
        return no;
    }
    if( !mon->shearable() ) {
        return string_format( _( "The %s has nothing to shear." ), mon->get_name() );
    }
    if( season_of_year( calendar::turn ) == WINTER ) {
        return std::string( _( "This animal would freeze if you shear it during the winter." ) );
    }
    if( mon->has_effect( effect_sheared ) ) {
        return std::string( _( "This animal is not ready to be sheared again yet." ) );
    }
    if( who.max_quality( qual_SHEAR ) <= 0 ) {
        return string_format( _( "%s has no shearing tool." ), who.get_name() );
    }
    // As examining the monster: it is tied for the time unless it was already.
    const bool was_tied = mon->has_effect( effect_tied );
    if( !was_tied ) {
        mon->add_effect( effect_tied, 1_turns, true );
    }
    const std::string name = mon->get_name();
    who.assign_activity( shearing_activity_actor( mon->pos_bub(), !was_tied ) );
    if( !now_doing( who, { ACT_SHEARING } ) ) {
        if( !was_tied ) {
            mon->remove_effect( effect_tied );
        }
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    return std::nullopt;
}

refusal start_bash( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Smashes what is at "target_loc" until it breaks, as the smash action keeps smashing. The
    // activity smashes as the avatar, so only the avatar can be given it.
    if( !who.is_avatar() ) {
        return string_format( _( "%s can't smash things that way." ), who.get_name() );
    }
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( !here.is_bashable( pos ) && !here.veh_at( pos ) ) {
        return string_format( _( "There is nothing to smash at the %s." ), here.name( pos ) );
    }
    if( refusal no = faction_warning( who ) ) {
        return no;
    }
    const std::string name = here.name( pos );
    who.assign_activity( bash_activity_actor( pos ) );
    return started_or_not( who, { ACT_BASH }, name );
}

refusal start_crack( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // As examining a locked safe, with a listening tool.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( !here.furn( pos )->has_examine( iexamine::safe ) ) {
        return string_format( _( "There is no locked safe at the %s." ), here.name( pos ) );
    }
    if( !who.has_flag( json_flag_SAFECRACK_NO_TOOL ) && !who.cache_has_item_with( flag_SAFECRACK ) ) {
        return string_format( _( "%s has nothing to listen to the lock with." ), who.get_name() );
    }
    if( who.is_deaf() ) {
        return string_format( _( "%s can't crack a safe while deaf." ), who.get_name() );
    }
    if( who.has_effect( effect_earphones ) ) {
        return string_format( _( "%s can't crack a safe while listening to music." ), who.get_name() );
    }
    const std::string name = here.furnname( pos );
    who.assign_activity( safecracking_activity_actor( pos ) );
    return started_or_not( who, { ACT_CRACKING }, name );
}

refusal start_fold( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Folds the vehicle at "target_loc", as its menu does.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    const optional_vpart_position vp = get_map().veh_at( pos );
    if( !vp ) {
        return std::string( _( "There is no vehicle there." ) );
    }
    const vehicle &veh = vp->vehicle();
    if( !veh.is_foldable() ) {
        return string_format( _( "The %s can't be folded." ), veh.name );
    }
    const std::string name = veh.name;
    who.assign_activity( vehicle_folding_activity_actor( veh ) );
    return started_or_not( who, { ACT_VEHICLE_FOLD }, name );
}

refusal start_forage( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Forages through the shrub at "target_loc", as examining it does.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, false, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( !here.ter( pos )->has_examine( iexamine::shrub_wildveggies ) ) {
        return string_format( _( "There is nothing to forage at the %s." ), here.name( pos ) );
    }
    // The avatar is asked first when something more interesting may be there.
    if( who.is_avatar() && ( !here.i_at( pos ).empty() || here.veh_at( pos ) ||
                             here.can_see_trap_at( pos, who ) || get_creature_tracker().creature_at( pos ) != nullptr ) ) {
        return string_format( _( "Foraging through the %s would need your confirmation." ),
                              here.tername( pos ) );
    }
    int move_cost = 100000 / ( 2 * who.get_skill_level( skill_survival ) + 5 );
    move_cost /= rng( std::max( 4, who.get_per() ), 4 + who.get_per() * 2 );
    const std::string name = here.tername( pos );
    who.assign_activity( forage_activity_actor( move_cost ) );
    if( !now_doing( who, { ACT_FORAGE } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    who.activity.placement = *target.loc;
    who.activity.auto_resume = true;
    return std::nullopt;
}

refusal start_mop( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Mops the spill at "target_loc" with the mop the talker carries, as the zone job does.
    tripoint_bub_ms pos;
    if( refusal no = tile_refusal( who, target, true, pos ) ) {
        return no;
    }
    map &here = get_map();
    if( !here.terrain_moppable( pos ) ) {
        return string_format( _( "There is nothing to mop at the %s." ), here.name( pos ) );
    }
    if( !who.cache_has_item_with( json_flag_MOP ) ) {
        return string_format( _( "%s has no mop." ), who.get_name() );
    }
    const std::string name = here.name( pos );
    who.assign_activity( mop_activity_actor( 15 ) );
    if( !now_doing( who, { ACT_MOP } ) ) {
        return string_format( _( "Working on the %s stopped as soon as it started." ), name );
    }
    who.activity.placement = *target.loc;
    return std::nullopt;
}

// ACT_BINDER_COPY_RECIPE: "target_item" (the recipe book) and "recipe".
refusal fields_binder( const eoc_target_fields &given )
{
    if( !given.item || !given.recipe ) {
        return std::string( R"(needs "target_item" (the recipe book) and "recipe")" );
    }
    if( given.loc || given.butcher_type || given.creature ) {
        return std::string( R"(takes no target but "target_item" and "recipe")" );
    }
    return std::nullopt;
}

refusal start_binder_copy( const activity_id &, Character &who,
                           const eoc_activity_target &target )
{
    // As the recipe book's "copy a recipe" use, with the recipe given rather than chosen.
    item_location binder = target.item;
    if( refusal no = reach_refusal( who, binder, true ) ) {
        return no;
    }
    if( !binder->type->can_use( "BINDER_ADD_RECIPE" ) ) {
        return string_format( _( "The %s is not a recipe book to copy into." ), binder->tname() );
    }
    if( who.cant_do_mounted( false ) || who.is_underwater() ) {
        return string_format( _( "%s can't write here." ), who.get_name() );
    }
    const recipe &rec = target.recipe->obj();
    if( who.fine_detail_vision_mod() > 4.0f ) {
        return std::string( _( "It's too dark to write!" ) );
    }
    const temp_crafting_inventory &crafting_inv = who.crafting_inventory();
    const std::vector<const item *> writing_tools = crafting_inv.items_with( [&who](
    const item & it ) {
        return it.has_flag( flag_WRITE_MESSAGE ) && it.ammo_sufficient( &who );
    } );
    if( writing_tools.empty() ) {
        return string_format( _( "%s has nothing to write with." ), who.get_name() );
    }
    if( !who.get_recipes_from_books( crafting_inv ).contains( &rec ) ) {
        return string_format( _( "No book at hand has the recipe for %s." ), rec.result_name() );
    }
    int pages = bookbinder_copy_activity_actor::pages_for_recipe( rec );
    if( !who.has_charges( itype_paper, pages ) ) {
        return string_format( _( "%s doesn't have enough paper to copy this recipe." ),
                              who.get_name() );
    }
    if( binder->remaining_ammo_capacity() < pages ) {
        return string_format( _( "The %s can not fit this recipe." ), binder->tname() );
    }
    if( binder->get_saved_recipes().count( rec.ident() ) != 0 ) {
        return string_format( _( "The %1$s already has a recipe for %2$s." ), binder->tname(),
                              rec.result_name() );
    }
    // As the use: the writing tools last for every page.
    for( const item *it : writing_tools ) {
        if( !it->needs_charges_to_use() ) {
            pages = 0;
            break;
        }
        pages -= it->uses_firing_requirements() ? it->tool_uses_remaining_local() :
                 it->ammo_remaining() / it->ammo_required();
        if( pages <= 0 ) {
            break;
        }
    }
    if( pages > 0 ) {
        return string_format( _( "%s's writing tools do not have enough charges." ), who.get_name() );
    }
    const std::string name = binder->tname();
    who.assign_activity( bookbinder_copy_activity_actor( binder, rec.ident() ) );
    return started_or_not( who, { ACT_BINDER_COPY_RECIPE }, name );
}

refusal start_disable( const activity_id &, Character &who, const eoc_activity_target &target )
{
    // Deactivates the robot or turret, as examining it does. Whether it can be is the avatar's
    // skill and knowledge, so only the avatar can be given it; reprogramming a manhack is a
    // choice, and isn't done.
    monster *mon = target.creature;
    if( refusal no = monster_refusal( who, mon ) ) {
        return no;
    }
    if( !who.is_avatar() ) {
        return string_format( _( "%1$s can't deactivate the %2$s." ), who.get_name(), mon->get_name() );
    }
    if( !disable_activity_actor::can_disable_or_reprogram( *mon ) ||
        mon->type->revert_to_itype.is_empty() ) {
        return string_format( _( "The %s can't be deactivated." ), mon->get_name() );
    }
    const std::string name = mon->get_name();
    who.assign_activity( disable_activity_actor( mon->pos_bub(),
                         disable_activity_actor::get_disable_turns(), false ) );
    return started_or_not( who, { ACT_DISABLE }, name );
}

struct target_starter {
    refusal( *fields_error )( const eoc_target_fields & );
    refusal( *start )( const activity_id &, Character &, const eoc_activity_target & );
};

// Activities u_assign_activity starts with a target. Please keep this sorted, and the list in
// doc/JSON/EFFECT_ON_CONDITION.md (activities with a target, and those that can't be started) in
// step with it.
const std::unordered_map<activity_id, target_starter> &target_starters()
{
    // the place, and optionally the tool as "target_item"
    const auto tool_at_place = &fields_of<takes::may, takes::must, takes::no>;
    const auto place_only = &fields_of<takes::no, takes::must, takes::no>;
    const auto item_and_place = &fields_of<takes::must, takes::must, takes::no>;
    const auto item_maybe_place = &fields_of<takes::must, takes::may, takes::no>;
    const auto monster_only = &fields_of<takes::no, takes::no, takes::must>;
    static const std::unordered_map<activity_id, target_starter> starters = {
        { ACT_BASH, { place_only, &start_bash } },
        { ACT_BINDER_COPY_RECIPE, { &fields_binder, &start_binder_copy } },
        { ACT_BLEED, { &fields_item_only, &start_butcher } },
        { ACT_BOLTCUTTING, { tool_at_place, &start_tile_tool } },
        { ACT_BUILD, { place_only, &start_build } },
        { ACT_BUTCHER, { &fields_butcher, &start_butcher } },
        { ACT_BUTCHER_FULL, { &fields_item_only, &start_butcher } },
        { ACT_CHOP_LOGS, { tool_at_place, &start_chop_logs } },
        { ACT_CHOP_TREE, { tool_at_place, &start_chop_tree } },
        { ACT_CLEAR_RUBBLE, { place_only, &start_clear_rubble } },
        { ACT_CONSUME, { &fields_item_only, &start_consume } },
        { ACT_CRACKING, { place_only, &start_crack } },
        { ACT_CRAFT, { &fields_craft, &start_craft } },
        { ACT_DISABLE, { monster_only, &start_disable } },
        { ACT_DISASSEMBLE, { &fields_item_only, &start_disassemble } },
        { ACT_DISMEMBER, { &fields_item_only, &start_butcher } },
        { ACT_DISSECT, { &fields_item_only, &start_butcher } },
        { ACT_DROP, { item_maybe_place, &start_drop } },
        { ACT_FERTILIZE_PLANT, { item_and_place, &start_fertilize } },
        { ACT_FIELD_DRESS, { &fields_item_only, &start_butcher } },
        { ACT_FIRSTAID, { &fields_item_only, &start_firstaid } },
        { ACT_FISH, { tool_at_place, &start_fish } },
        { ACT_FORAGE, { place_only, &start_forage } },
        { ACT_GUNMOD_REMOVE, { &fields_item_only, &start_gunmod_remove } },
        { ACT_HACKING, { tool_at_place, &start_hacking } },
        { ACT_HACKSAW, { tool_at_place, &start_tile_tool } },
        { ACT_HARVEST, { place_only, &start_harvest } },
        { ACT_INSERT_ITEM, { &fields_item_only, &start_insert } },
        { ACT_JACKHAMMER, { tool_at_place, &start_mine } },
        { ACT_LOCKPICK, { tool_at_place, &start_lockpick } },
        { ACT_MILK, { monster_only, &start_milk } },
        { ACT_MOP, { place_only, &start_mop } },
        { ACT_MOVE_ITEMS, { item_and_place, &start_move_items } },
        { ACT_OXYTORCH, { tool_at_place, &start_tile_tool } },
        { ACT_PICKAXE, { tool_at_place, &start_mine } },
        { ACT_PICKUP, { &fields_item_only, &start_pickup } },
        { ACT_PLANT_SEED, { item_and_place, &start_plant_seed } },
        { ACT_PLAY_WITH_PET, { monster_only, &start_play_with_pet } },
        { ACT_PRYING, { tool_at_place, &start_tile_tool } },
        { ACT_PULP, { place_only, &start_pulp } },
        { ACT_QUARTER, { &fields_item_only, &start_butcher } },
        { ACT_READ, { &fields_item_only, &start_read } },
        { ACT_RELOAD, { &fields_item_only, &start_reload } },
        { ACT_SHEARING, { monster_only, &start_shearing } },
        { ACT_SKIN, { &fields_item_only, &start_butcher } },
        { ACT_TENT_DECONSTRUCT, { place_only, &start_tent_deconstruct } },
        { ACT_TENT_PLACE, { item_and_place, &start_tent_place } },
        { ACT_TOOLMOD_ADD, { &fields_item_only, &start_toolmod_add } },
        { ACT_UNLOAD, { &fields_item_only, &start_unload } },
        { ACT_VEHICLE_FOLD, { place_only, &start_fold } },
        { ACT_WEAR, { &fields_item_only, &start_wear } },
        { ACT_WIELD, { &fields_item_only, &start_wield } },
    };
    return starters;
}

} // namespace

bool takes_eoc_target( const activity_id &type )
{
    return target_starters().count( type ) != 0;
}

std::optional<std::string> eoc_target_fields_error( const activity_id &type,
        const eoc_target_fields &given )
{
    const auto it = target_starters().find( type );
    if( it == target_starters().end() ) {
        if( !given.any() ) {
            return std::nullopt;
        }
        std::vector<std::string> takes;
        for( const auto &starter : target_starters() ) {
            takes.push_back( starter.first.str() );
        }
        std::sort( takes.begin(), takes.end() );
        return string_format( "%s takes no %s; only these activities take a target: %s",
                              type.str(), quoted_list( given_names( given ) ),
                              string_join( takes, ", " ) );
    }
    if( refusal error = it->second.fields_error( given ) ) {
        return type.str() + " " + *error;
    }
    return std::nullopt;
}

bool is_eoc_butcher_type( const std::string &name )
{
    const std::vector<std::pair<std::string, butcher_type>> &names = butcher_type_names();
    return std::any_of( names.begin(), names.end(),
    [&name]( const std::pair<std::string, butcher_type> &entry ) {
        return entry.first == name;
    } );
}

std::optional<std::string> start_with_target( const activity_id &type, Character &who,
        const eoc_activity_target &target )
{
    const auto it = target_starters().find( type );
    if( it == target_starters().end() ) {
        return string_format( "%s takes no target", type.str() );
    }
    return it->second.start( type, who, target );
}

} // namespace activity_actors

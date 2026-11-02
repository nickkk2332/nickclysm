// Starting activities that need a target from EOCs: u_assign_activity's "target_item",
// "target_loc", "recipe" and "butcher_type" (ACT-J02). The activities that need nothing but a
// duration are started by eoc_starters in activity_actor.cpp.

#include "activity_actor_eoc.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "butchery.h"
#include "butchery_requirements.h"
#include "cata_utility.h"
#include "character.h"
#include "character_martial_arts.h"
#include "craft_command.h"
#include "craft_prompts.h"
#include "enums.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "harvest.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "line.h"
#include "map.h"
#include "mapdata.h"
#include "martialarts.h"
#include "mtype.h"
#include "npc.h"
#include "player_activity.h"
#include "proficiency.h"
#include "pocket_type.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "ret_val.h"
#include "string_formatter.h"
#include "translations.h"

static const activity_id ACT_BLEED( "ACT_BLEED" );
static const activity_id ACT_BUTCHER( "ACT_BUTCHER" );
static const activity_id ACT_BUTCHER_FULL( "ACT_BUTCHER_FULL" );
static const activity_id ACT_CRAFT( "ACT_CRAFT" );
static const activity_id ACT_CRAFT_WAIT( "ACT_CRAFT_WAIT" );
static const activity_id ACT_DISASSEMBLE( "ACT_DISASSEMBLE" );
static const activity_id ACT_DISMEMBER( "ACT_DISMEMBER" );
static const activity_id ACT_DISSECT( "ACT_DISSECT" );
static const activity_id ACT_FIELD_DRESS( "ACT_FIELD_DRESS" );
static const activity_id ACT_QUARTER( "ACT_QUARTER" );
static const activity_id ACT_READ( "ACT_READ" );
static const activity_id ACT_RELOAD( "ACT_RELOAD" );
static const activity_id ACT_SKIN( "ACT_SKIN" );
static const activity_id ACT_UNLOAD( "ACT_UNLOAD" );
static const activity_id ACT_WEAR( "ACT_WEAR" );

static const harvest_drop_type_id harvest_drop_blood( "blood" );
static const harvest_drop_type_id harvest_drop_offal( "offal" );
static const harvest_drop_type_id harvest_drop_skin( "skin" );

static const itype_id itype_disassembly( "disassembly" );

static const proficiency_id proficiency_prof_dissect_humans( "prof_dissect_humans" );

static const quality_id qual_BUTCHER( "BUTCHER" );
static const quality_id qual_CUT_FINE( "CUT_FINE" );

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
    return names;
}

// Only "target_item", which it needs.
refusal fields_item_only( const eoc_target_fields &given )
{
    if( !given.item ) {
        return std::string( "needs \"target_item\"" );
    }
    if( given.loc || given.recipe || given.butcher_type ) {
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
    if( given.loc || given.recipe ) {
        return std::string( "takes no target but \"target_item\" and \"butcher_type\"" );
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
        if( !dressed && corpse->harvest->has_entry_type( harvest_drop_offal ) ) {
            return std::string( _( "You need to perform field dressing before quartering." ) );
        }
    }

    // As butcher_submenu: something to remove.
    bool has_blood = false;
    bool has_skin = false;
    bool has_organs = false;
    for( const harvest_entry &entry : corpse->harvest.obj() ) {
        if( entry.type == harvest_drop_skin && !corpse_item.has_flag( flag_SKINNED ) ) {
            has_skin = true;
        }
        if( entry.type == harvest_drop_offal && !( corpse_item.has_flag( flag_QUARTERED ) ||
                dressed ) ) {
            has_organs = true;
        }
        if( entry.type == harvest_drop_blood && corpse->bleed_rate > 0 &&
            !( corpse_item.has_flag( flag_QUARTERED ) || dressed ||
               corpse_item.has_flag( flag_BLED ) ) ) {
            has_blood = true;
        }
    }
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
        who.assign_activity( craft_activity_actor( craft, false ) );
        if( !now_doing( who, { ACT_CRAFT, ACT_CRAFT_WAIT } ) ) {
            return did_not_start( *craft );
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
    if( book->type->use_methods.count( "MA_MANUAL" ) != 0 ) {
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

struct target_starter {
    refusal( *fields_error )( const eoc_target_fields & );
    refusal( *start )( const activity_id &, Character &, const eoc_activity_target & );
};

// Activities u_assign_activity starts with a target. Please keep this sorted.
const std::unordered_map<activity_id, target_starter> &target_starters()
{
    static const std::unordered_map<activity_id, target_starter> starters = {
        { ACT_BLEED, { &fields_item_only, &start_butcher } },
        { ACT_BUTCHER, { &fields_butcher, &start_butcher } },
        { ACT_BUTCHER_FULL, { &fields_item_only, &start_butcher } },
        { ACT_CRAFT, { &fields_craft, &start_craft } },
        { ACT_DISASSEMBLE, { &fields_item_only, &start_disassemble } },
        { ACT_DISMEMBER, { &fields_item_only, &start_butcher } },
        { ACT_DISSECT, { &fields_item_only, &start_butcher } },
        { ACT_FIELD_DRESS, { &fields_item_only, &start_butcher } },
        { ACT_QUARTER, { &fields_item_only, &start_butcher } },
        { ACT_READ, { &fields_item_only, &start_read } },
        { ACT_RELOAD, { &fields_item_only, &start_reload } },
        { ACT_SKIN, { &fields_item_only, &start_butcher } },
        { ACT_UNLOAD, { &fields_item_only, &start_unload } },
        { ACT_WEAR, { &fields_item_only, &start_wear } },
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

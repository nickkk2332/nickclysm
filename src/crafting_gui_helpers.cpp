#include "crafting_gui_helpers.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bonuses.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_id.h"
#include "coordinates.h"
#include "crafting.h"
#include "crafting_enums.h"
#include "display.h"
#include "flag.h"
#include "flat_set.h"
#include "game_constants.h"
#include "item.h"
#include "item_factory.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "localized_comparator.h"
#include "magic_type.h"
#include "mutation.h"
#include "output.h"
#include "proficiency.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "skill.h"
#include "string_formatter.h"
#include "temp_crafting_inventory.h"
#include "translation_cache.h"
#include "translations.h"
#include "type_id.h"
#include "uistate.h"
#include "vitamin.h"

bool cannot_gain_skill_or_prof( const Character &crafter, const recipe &recp )
{
    if( recp.skill_used &&
        static_cast<int>( crafter.get_skill_level( recp.skill_used ) ) <= recp.get_skill_cap() ) {
        return false;
    }
    for( const proficiency_id &prof : recp.used_proficiencies() ) {
        if( !crafter.has_proficiency( prof ) ) {
            return false;
        }
    }
    return true;
}

const book_proficiency_bonuses &availability::get_book_bonuses() const
{
    if( !cached_book_bonuses ) {
        cached_book_bonuses = crafter.book_bonuses_nearby();
    }
    return *cached_book_bonuses;
}

float availability::get_proficiency_time_maluses() const
{
    if( proficiency_time_maluses < 0 ) {
        proficiency_time_maluses = rec->proficiency_time_maluses(
                                       crafter, get_book_bonuses() );
    }

    return proficiency_time_maluses;
}

float availability::get_max_proficiency_time_maluses() const
{
    if( max_proficiency_time_maluses < 0 ) {
        max_proficiency_time_maluses = rec->max_proficiency_time_maluses( crafter );
    }

    return max_proficiency_time_maluses;
}

float availability::get_proficiency_skill_maluses() const
{
    if( proficiency_skill_maluses < 0 ) {
        proficiency_skill_maluses = rec->proficiency_skill_maluses(
                                        crafter, get_book_bonuses() );
    }

    return proficiency_skill_maluses;
}

float availability::get_max_proficiency_skill_maluses() const
{
    if( max_proficiency_skill_maluses < 0 ) {
        max_proficiency_skill_maluses = rec->max_proficiency_skill_maluses( crafter );
    }

    return max_proficiency_skill_maluses;
}

namespace
{
// all the craftability answer needs before any inventory query
struct recipe_gate {
    bool has_all_skills = false;
    bool crafter_has_primary_skill = false;
    bool has_proficiencies = false;
    bool base_requirements = false;
    bool npc_cannot_craft = false;
};
} // namespace

static recipe_gate gate_recipe( const Character &crafter, const recipe &recp,
                                bool camp_crafting )
{
    recipe_gate g;
    if( recp.skill_used.is_null() ) {
        g.has_all_skills = true;
        g.crafter_has_primary_skill = true;
    } else {
        const float difficulty = recp.get_difficulty( crafter );
        g.has_all_skills = crafter.get_skill_level( recp.skill_used ) >= difficulty;
        g.crafter_has_primary_skill = crafter.get_knowledge_level( recp.skill_used ) >=
                                      recp.required_knowledge_level( crafter );
    }
    g.has_proficiencies = recp.character_has_required_proficiencies( crafter );
    // character_meets_requirements() includes the recipe's JSON condition (CRAFT-J06)
    g.base_requirements = ( !recp.is_practice() || g.has_all_skills ) && g.has_proficiencies &&
                          recp.character_meets_requirements( crafter );
    std::string npc_reason;
    g.npc_cannot_craft = !camp_crafting && crafter.is_npc() && !recp.npc_can_craft( npc_reason );
    return g;
}

static const temp_crafting_inventory &inventory_for( const Character &crafter, bool camp_crafting,
        temp_crafting_inventory *inventory_override )
{
    return camp_crafting ? *inventory_override : crafter.crafting_inventory();
}

static craft_flags flags_for( bool camp_crafting )
{
    return camp_crafting ? craft_flags::none : craft_flags::start_only;
}

static bool craftable_now( Character &crafter, const recipe &recp, const recipe_gate &g,
                           int batch_size, bool camp_crafting,
                           temp_crafting_inventory *inventory_override )
{
    if( g.npc_cannot_craft || !g.base_requirements ) {
        return false;
    }
    if( recp.is_nested() ) {
        return availability::check_can_craft_nested( crafter, recp, camp_crafting,
                inventory_override );
    }
    return recp.deduped_requirements().can_make_with_inventory(
               &crafter, inventory_for( crafter, camp_crafting, inventory_override ),
               recp.get_component_filter( recipe_filter_flags::none ), batch_size,
               flags_for( camp_crafting ) );
}

availability::availability( Character &_crafter, const recipe *recp, int batch_size,
                            bool camp_crafting, temp_crafting_inventory *inventory_override ) :
    crafter( _crafter )
{
    rec = recp;
    inv_override = inventory_override;

    const temp_crafting_inventory &inv = inventory_for( crafter, camp_crafting, inventory_override );

    const craft_flags flag = flags_for( camp_crafting );

    const bool is_nested = recp->is_nested();
    const bool is_practice = recp->is_practice();

    const recipe_gate gate = gate_recipe( crafter, *recp, camp_crafting );
    has_all_skills = gate.has_all_skills;
    crafter_has_primary_skill = gate.crafter_has_primary_skill;
    has_proficiencies = gate.has_proficiencies;

    can_craft_recipe = craftable_now( crafter, *recp, gate, batch_size, camp_crafting,
                                      inventory_override );

    would_use_rotten = false;
    would_use_favorite = false;

    if( can_craft_recipe && !is_nested ) {
        const deduped_requirement_data &req_data = recp->deduped_requirements();

        const auto no_rotten_filter = recp->get_component_filter( recipe_filter_flags::no_rotten );
        const auto no_favorite_filter = recp->get_component_filter( recipe_filter_flags::no_favorite );

        would_use_rotten =
            !req_data.can_make_with_inventory(
                &crafter, inv, no_rotten_filter,
                batch_size, flag
            );

        would_use_favorite =
            !req_data.can_make_with_inventory(
                &crafter, inv, no_favorite_filter,
                batch_size, flag
            );
    }

    apparently_craftable = false;

    if( !can_craft_recipe &&
        !is_nested &&
        !gate.npc_cannot_craft &&
        gate.base_requirements ) {

        const auto all_items_filter = recp->get_component_filter( recipe_filter_flags::none );

        apparently_craftable =
            recp->simple_requirements().can_make_with_inventory(
                &crafter, inv, all_items_filter,
                batch_size, flag
            );
    }

    useless_practice = is_practice && cannot_gain_skill_or_prof( crafter, *recp );
    is_nested_category = is_nested;

    // Idk whats going on here so i will leave it for now
    for( const auto &[skill, skill_lvl] : recp->required_skills ) {
        if( crafter.get_skill_level( skill ) < skill_lvl ) {
            has_all_skills = false;
            break;
        }
    }
}

nc_color availability::selected_color() const
{
    if( !can_craft_recipe && is_nested_category ) {
        return h_blue;
    } else if( !can_craft_recipe ) {
        return h_dark_gray;
    } else if( !crafter_has_primary_skill && is_nested_category ) {
        return h_magenta;
    } else if( !crafter_has_primary_skill ) {
        return h_light_red;
    } else if( is_nested_category ) {
        return h_light_blue;
    } else if( would_use_rotten || useless_practice ) {
        return has_all_skills ? h_brown : h_red;
    } else if( would_use_favorite ) {
        return has_all_skills ? h_pink : h_red;
    } else {
        return has_all_skills ? h_white : h_yellow;
    }
}

nc_color availability::color( bool ignore_missing_skills ) const
{
    if( !can_craft_recipe && is_nested_category ) {
        return c_blue;
    } else if( !can_craft_recipe ) {
        return c_dark_gray;
    } else if( !crafter_has_primary_skill && is_nested_category ) {
        return c_magenta;
    } else if( !crafter_has_primary_skill ) {
        return c_light_red;
    } else if( is_nested_category ) {
        return c_light_blue;
    } else if( would_use_rotten || useless_practice ) {
        return has_all_skills || ignore_missing_skills ? c_brown : c_red;
    } else if( would_use_favorite ) {
        return has_all_skills ? c_pink : c_red;
    } else {
        return has_all_skills || ignore_missing_skills ? c_white : c_yellow;
    }
}

bool availability::check_can_craft_nested( Character &_crafter, const recipe &r,
        bool camp_crafting, temp_crafting_inventory *inventory_override )
{
    for( const recipe_id &nested_r : r.nested_category_data ) {
        const recipe &child = nested_r.obj();
        if( craftable_now( _crafter, child, gate_recipe( _crafter, child, camp_crafting ), 1,
                           camp_crafting, inventory_override ) ) {
            return true;
        }
    }
    return false;
}

const availability &cached_availability( std::map<const recipe *, availability> &cache,
        Character &crafter, const recipe &rec, bool camp_crafting,
        temp_crafting_inventory *inventory_override )
{
    return cache.try_emplace( &rec, crafter, &rec, 1, camp_crafting,
                              inventory_override ).first->second;
}

craft_confirm_result can_start_craft(
    const recipe &rec,
    const availability &avail,
    const Character &crafter,
    int batch_size )
{
    if( !avail.can_craft_recipe || !avail.crafter_has_primary_skill ) {
        return craft_confirm_result::cannot_craft;
    }
    if( rec.makes_amount() * batch_size > MAX_ITEM_IN_SQUARE ) {
        return craft_confirm_result::too_many_results;
    }
    if( crafter.lighting_craft_speed_multiplier( rec ) <= 0.0f ) {
        return craft_confirm_result::too_dark;
    }
    return craft_confirm_result::ok;
}

bool recipe_sort_compare(
    const recipe *a, const recipe *b,
    const availability &avail_a, const availability &avail_b,
    const Character &crafter, const crafting_cost_context &ctx,
    bool a_read, bool b_read,
    bool unread_first )
{
    if( unread_first ) {
        if( a_read != b_read ) {
            return !a_read;
        }
    }
    if( avail_a.can_craft_recipe != avail_b.can_craft_recipe ) {
        return avail_a.can_craft_recipe;
    }
    if( b->difficulty != a->difficulty ) {
        return b->difficulty < a->difficulty;
    }
    const std::string a_name = a->result_name();
    const std::string b_name = b->result_name();
    if( a_name != b_name ) {
        return localized_compare( a_name, b_name );
    }
    return b->time_to_craft( crafter, ctx ) < a->time_to_craft( crafter, ctx );
}

static std::string craft_success_chance_string( const recipe &recp, const Character &guy )
{
    float chance = 100.f * ( 1.f - guy.recipe_success_chance( recp ) );
    std::string color;
    if( chance > 75 ) {
        color = "yellow";
    } else if( chance > 50 ) {
        color = "light_gray";
    } else if( chance > 25 ) {
        color = "green";
    } else {
        color = "cyan";
    }

    return string_format( _( "Minor Failure Chance: <color_%s>%2.2f%%</color>" ), color, chance );
}

static std::string cata_fail_chance_string( const recipe &recp, const Character &guy )
{
    float chance = 100.f * guy.item_destruction_chance( recp );
    std::string color;
    if( chance > 50 ) {
        color = "i_red";
    } else if( chance > 20 ) {
        color = "red";
    } else if( chance > 5 ) {
        color = "yellow";
    } else {
        color = "light_gray";
    }

    return string_format( _( "Catastrophic Failure Chance: <color_%s>%2.2f%%</color>" ), color,
                          chance );
}

std::vector<std::string> recipe_info(
    const recipe &recp,
    const availability &avail,
    Character &guy,
    std::string_view qry_comps,
    const int batch_size,
    const int fold_width,
    const nc_color &color,
    const std::vector<Character *> &crafting_group )
{
    std::ostringstream oss;
    oss << string_format( _( "Crafter: %s\n" ), guy.name_and_maybe_activity() );

    oss << string_format( _( "Primary skill: %s\n" ), recp.primary_skill_string( guy ) );
    if( !avail.crafter_has_primary_skill ) {
        if( recp.is_practice() ) {
            oss << _( "<color_red>Crafter cannot practice this because they "
                      "lack the theoretical knowledge for it.</color>\n" );
        } else {
            oss << _( "<color_red>Crafter cannot craft this because they "
                      "lack the theoretical knowledge to understand the recipe.</color>\n" );
        }
    }

    if( !recp.required_skills.empty() ) {
        oss << string_format( _( "Other skills: %s\n" ), recp.required_skills_string( guy ) );
    }

    const std::string req_profs = recp.required_proficiencies_string( &guy );
    if( !req_profs.empty() ) {
        oss << string_format( _( "Proficiencies Required: %s\n" ), req_profs );
    }
    const std::string used_profs = recp.used_proficiencies_string( &guy );
    if( !used_profs.empty() ) {
        oss << string_format( _( "Proficiencies Used: %s\n" ), used_profs );
    }
    const std::string missing_profs = recp.missing_proficiencies_string( &guy );
    if( !missing_profs.empty() ) {
        oss << string_format( _( "Proficiencies Missing: %s\n" ), missing_profs );
    }

    oss << craft_success_chance_string( recp, guy ) << "\n";
    oss << cata_fail_chance_string( recp, guy ) << "\n";

    if( !recp.is_nested() ) {
        const int expected_turns = guy.expected_time_to_craft( recp, batch_size )
                                   / to_moves<int>( 1_turns );
        oss << string_format( _( "Time to complete: <color_cyan>%s</color>\n" ),
                              to_string( time_duration::from_turns( expected_turns ) ) );

    }

    const std::string batch_savings = recp.batch_savings_string();
    if( !batch_savings.empty() ) {
        oss << string_format( _( "Batch time savings: <color_cyan>%s</color>\n" ), batch_savings );
    }

    oss << string_format( _( "Activity level: <color_cyan>%s</color>\n" ),
                          display::activity_level_str( recp.exertion_level() ) );

    const int makes = recp.makes_amount();
    if( makes > 1 ) {
        oss << string_format( _( "Recipe makes: <color_cyan>%d</color>\n" ), makes );
    }

    oss << string_format( _( "Craftable in the dark?  <color_cyan>%s</color>\n" ),
                          recp.has_flag( "BLIND_EASY" ) ? _( "Easy" ) :
                          recp.has_flag( "BLIND_HARD" ) ? _( "Hard" ) :
                          _( "Impossible" ) );

    const temp_crafting_inventory &crafting_inv = avail.inv_override ? *avail.inv_override :
            guy.crafting_inventory();
    if( recp.result() ) {
        const int nearby_amount = crafting_inv.count_item( recp.result() );
        std::string nearby_string;
        if( nearby_amount == 0 ) {
            nearby_string = "<color_light_gray>0</color>";
        } else if( nearby_amount > 9000 ) {
            // at some point you get too many to count at a glance and just know you have a lot
            nearby_string = _( "<color_red>It's Over 9000!!!</color>" );
        } else {
            nearby_string = string_format( "<color_yellow>%d</color>", nearby_amount );
        }
        oss << string_format( _( "Nearby: %s\n" ), nearby_string );
    }

    const bool can_craft_this = avail.can_craft_recipe;
    if( can_craft_this && avail.would_use_rotten ) {
        oss << _( "<color_red>Will use rotten ingredients</color>\n" );
    }
    if( can_craft_this && avail.would_use_favorite ) {
        oss << _( "<color_red>Will use favorited ingredients</color>\n" );
    }
    const bool too_complex = recp.deduped_requirements().is_too_complex();
    if( can_craft_this && too_complex ) {
        oss << _( "Due to the complex overlapping requirements, this "
                  "recipe <color_yellow>may appear to be craftable "
                  "when it is not</color>.\n" );
    }
    std::string reason;
    bool npc_cant = avail.crafter.is_npc() && !recp.npc_can_craft( reason ) && !avail.inv_override ;
    if( !can_craft_this && avail.apparently_craftable && !recp.is_nested() && !npc_cant ) {
        oss << _( "<color_red>Cannot be crafted because the same item is needed "
                  "for multiple components.</color>\n" );
    }

    if( !can_craft_this && npc_cant ) {
        oss << colorize( reason, c_red ) << "\n";
    }
    if( !can_craft_this && !recp.meets_condition( avail.crafter ) ) {
        oss << colorize( recp.condition_fail_text(), c_red ) << "\n";
    }

    const bool disp_prof_msg = avail.has_proficiencies && !recp.is_nested();
    const float time_maluses = avail.get_proficiency_time_maluses();
    const float max_time_malus = avail.get_max_proficiency_time_maluses();
    const float skill_maluses = avail.get_proficiency_skill_maluses();
    const float max_skill_malus = avail.get_max_proficiency_skill_maluses();
    if( disp_prof_msg && time_maluses < max_time_malus && skill_maluses < max_skill_malus ) {
        oss << string_format( _( "<color_green>This recipe will be %.2fx faster than normal, "
                                 "and your effective skill will be %.2f levels higher than normal, because of "
                                 "the proficiencies the crafter has.</color>\n" ),
                              max_time_malus / time_maluses, max_skill_malus - skill_maluses );
    } else if( disp_prof_msg && time_maluses < max_time_malus ) {
        oss << string_format( _( "<color_green>This recipe will be %.2fx faster than normal, "
                                 "because of the proficiencies the crafter has.</color>\n" ), max_time_malus / time_maluses );
    } else if( disp_prof_msg && skill_maluses < max_skill_malus ) {
        oss << string_format(
                _( "<color_green>Your effective skill will be %.2f levels higher than normal, "
                   "because of the proficiencies the crafter has.</color>\n" ), max_skill_malus - skill_maluses );
    }
    if( !can_craft_this && !avail.has_proficiencies ) {
        oss << _( "<color_red>Cannot be crafted because the crafter lacks"
                  " the required proficiencies.</color>\n" );
    }

    if( recp.has_byproducts() ) {
        oss << _( "Byproducts:\n" );
        for( const std::pair<const itype_id, int> &bp : recp.get_byproducts() ) {
            const itype *t = item::find_type( bp.first );
            int amount = bp.second * batch_size;
            if( t->count_by_charges() ) {
                oss << string_format( "> %s (%d)\n", t->nname( 1 ), amount );
            } else {
                oss << string_format( "> %d %s\n", amount,
                                      t->nname( static_cast<unsigned int>( amount ) ) );
            }
        }
    }

    std::vector<std::string> result = foldstring( oss.str(), fold_width );

    if( !recp.is_nested() ) {
        const requirement_data &req = recp.simple_requirements();
        const std::vector<std::string> tools = req.get_folded_tools_list(
                &guy, fold_width, color, crafting_inv, batch_size );
        const std::vector<std::string> comps = req.get_folded_components_list(
                &guy, fold_width, color, crafting_inv, recp.get_component_filter(), batch_size, qry_comps );
        result.insert( result.end(), tools.begin(), tools.end() );
        result.insert( result.end(), comps.begin(), comps.end() );
    }

    oss = std::ostringstream();
    if( !guy.knows_recipe( &recp ) ) {
        oss << _( "Recipe not memorized yet\n" );
        const std::set<itype_id> books_with_recipe = guy.get_books_for_recipe( crafting_inv, &recp );
        if( !books_with_recipe.empty() ) {
            const std::string enumerated_books = enumerate_as_string( books_with_recipe,
            []( const itype_id & type_id ) {
                return colorize( item::nname( type_id ), c_cyan );
            } );
            oss << string_format( _( "Written in: %s\n" ), enumerated_books );
        }
        std::vector<const Character *> knowing_helpers;
        for( const Character *helper : crafting_group ) {
            // guy.getID() != helper->getID(): guy doesn't know the recipe anyway, but this should be faster
            if( guy.getID() != helper->getID() && helper->knows_recipe( &recp ) ) {
                knowing_helpers.push_back( helper );
            }
        }
        if( !knowing_helpers.empty() ) {
            const std::string enumerated_helpers = enumerate_as_string( knowing_helpers,
            []( const Character * helper ) {
                return colorize( helper->is_avatar() ? _( "You" ) : helper->get_name(), c_cyan );
            } );
            oss << string_format( _( "Known by: %s\n" ), enumerated_helpers );
        }
    }
    std::vector<std::string> tmp = foldstring( oss.str(), fold_width );
    result.insert( result.end(), tmp.begin(), tmp.end() );

    return result;
}

std::string practice_recipe_description( const recipe &recp,
        const Character &crafter )
{
    std::ostringstream oss;
    oss << recp.get_description( crafter ) << "\n\n";
    if( !recp.practice_data ) {
        return oss.str();
    }
    if( recp.practice_data->min_difficulty != recp.practice_data->max_difficulty ) {
        std::string txt = string_format( _( "Difficulty range: %d to %d" ),
                                         recp.practice_data->min_difficulty, recp.practice_data->max_difficulty );
        oss << txt << "\n";
    }
    if( recp.skill_used ) {
        const int player_skill_level = crafter.get_all_skills().get_skill_level( recp.skill_used );
        if( player_skill_level < recp.practice_data->min_difficulty ) {
            std::string txt = string_format(
                                  _( "The crafter does not possess the minimum <color_cyan>%s</color> skill level required to practice this." ),
                                  recp.skill_used->name() );
            txt = string_format( "<color_red>%s</color>", txt );
            oss << txt << "\n";
        }
        if( recp.practice_data->skill_limit != MAX_SKILL ) {
            std::string txt = string_format(
                                  _( "This practice action will not increase your <color_cyan>%s</color> skill above %d." ),
                                  recp.skill_used->name(), recp.practice_data->skill_limit );
            if( player_skill_level >= recp.practice_data->skill_limit ) {
                txt = string_format( "<color_brown>%s</color>", txt );
            }
            oss << txt << "\n";
        }
    }
    return oss.str();
}

static bool query_is_yes( std::string_view query )
{
    const std::string_view subquery = query.substr( 2 );

    return subquery == "yes" || subquery == "y" || subquery == "1" ||
           subquery == "true" || subquery == "t" || subquery == "on" ||
           subquery == _( "yes" );
}

recipe_subset filter_recipes( const recipe_subset &available_recipes,
                              std::string_view qry, const Character &crafter,
                              const std::function<void( size_t, size_t )> &progress_callback )
{
    size_t qry_begin = 0;
    size_t qry_end = 0;
    recipe_subset filtered_recipes = available_recipes;
    do {
        // Find next ','
        qry_end = qry.find_first_of( ',', qry_begin );

        std::string qry_filter_str = trim( qry.substr( qry_begin, qry_end - qry_begin ) );
        // Process filter
        if( qry_filter_str.size() > 2 && qry_filter_str[1] == ':' ) {
            switch( qry_filter_str[0] ) {
                case 't':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::tool, progress_callback );
                    break;

                case 'c':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::component, progress_callback );
                    break;

                case 's':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::skill, progress_callback );
                    break;

                case 'p':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::primary_skill, progress_callback );
                    break;

                case 'Q':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::quality, progress_callback );
                    break;

                case 'q':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::quality_result, progress_callback );
                    break;

                case 'L':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::length, progress_callback );
                    break;

                case 'V':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::volume, progress_callback );
                    break;

                case 'M':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::mass, progress_callback );
                    break;

                case 'v':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::covers, progress_callback );
                    break;

                case 'e':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::layer, progress_callback );
                    break;

                case 'd':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::description_result, progress_callback );
                    break;

                case 'm': {
                    // get_learned_recipes lists NO nested_recipes
                    const recipe_subset &learned = crafter.get_learned_recipes();
                    recipe_subset temp_subset;
                    if( query_is_yes( qry_filter_str ) ) {
                        temp_subset = available_recipes.intersection( learned );
                    } else {
                        // nested_recipes cannot be learned so don't show them
                        temp_subset = available_recipes.difference( learned )
                                      .difference( recipe_dict.all_nested() );
                    }
                    filtered_recipes = filtered_recipes.intersection( temp_subset );
                    break;
                }

                case 'P':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::proficiency, progress_callback );
                    break;

                case 'b':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ), crafter,
                                       recipe_subset::search_type::book, progress_callback );
                    break;

                case 'l':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::difficulty, progress_callback );
                    break;

                case 'r': {
                    // TODO: this replaces filtered_recipes instead of intersecting,
                    // which breaks comma-chaining when r: appears after other filters.
                    // Prior filters in the chain are lost. Fix in a separate PR.
                    recipe_subset result;
                    for( const itype *e : item_controller->all() ) {
                        if( lcmatch( e->nname( 1 ), qry_filter_str.substr( 2 ) ) ) {
                            result.include( recipe_subset( available_recipes,
                                                           available_recipes.recipes_that_produce( e->get_id() ) ) );
                        }
                    }
                    filtered_recipes = result;

                    break;
                }

                case 'a':
                    filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 2 ),
                                       recipe_subset::search_type::activity_level, progress_callback );
                    break;
                default:
                    break;
            }
        } else if( qry_filter_str.size() > 1 && qry_filter_str[0] == '-' ) {
            filtered_recipes = filtered_recipes.reduce( qry_filter_str.substr( 1 ),
                               recipe_subset::search_type::exclude_name, progress_callback );
        } else {
            filtered_recipes = filtered_recipes.reduce( qry_filter_str,
                               recipe_subset::search_type::name,
                               std::function<void( size_t, size_t )> {} );
        }

        qry_begin = qry_end + 1;
    } while( qry_end != std::string::npos );
    return filtered_recipes;
}

const std::vector<SearchPrefix> prefixes = {
    //~ Example result description search term
    { 'q', to_translation( "metal sawing" ), to_translation( "<color_cyan>quality</color> of resulting item" ) },
    { 'd', to_translation( "reach attack" ), to_translation( "<color_cyan>full description</color> of resulting item (slow)" ) },
    { 'c', to_translation( "plank" ), to_translation( "<color_cyan>component</color> required to craft" ) },
    { 'p', to_translation( "tailoring" ), to_translation( "<color_cyan>primary skill</color> used to craft" ) },
    { 's', to_translation( "food handling" ), to_translation( "<color_cyan>any skill</color> used to craft" ) },
    { 'Q', to_translation( "fine bolt turning" ), to_translation( "<color_cyan>quality</color> required to craft" ) },
    { 't', to_translation( "soldering iron" ), to_translation( "<color_cyan>tool</color> required to craft" ) },
    { 'm', to_translation( "yes" ), to_translation( "recipe <color_cyan>memorized</color> (or not)" ) },
    { 'P', to_translation( "Blacksmithing" ), to_translation( "<color_cyan>proficiency</color> used to craft" ) },
    { 'b', to_translation( "chemistry textbook" ), to_translation( "<color_cyan>source</color> of the recipe" ) },
    { 'l', to_translation( "5" ), to_translation( "<color_cyan>difficulty</color> of the recipe as a number or range" ) },
    { 'r', to_translation( "buttermilk" ), to_translation( "recipe's (<color_cyan>by</color>)<color_cyan>products</color>" ) },
    { 'L', to_translation( "122 cm" ), to_translation( "result can contain item of <color_cyan>length</color>" ) },
    { 'V', to_translation( "450 ml" ), to_translation( "result can contain item of <color_cyan>volume</color>" ) },
    { 'M', to_translation( "250 kg" ), to_translation( "result can contain item of <color_cyan>mass</color>" ) },
    { 'v', to_translation( "head" ), to_translation( "<color_cyan>body part</color> the result covers" ) },
    { 'e', to_translation( "close to skin" ), to_translation( "<color_cyan>layer</color> the result covers" ) },
    { 'a', to_translation( "brisk" ), to_translation( "recipe's <color_cyan>activity level</color>" ) }
};

const translation filter_help_start = to_translation(
        "The default is to search result names.  Some single-character prefixes "
        "can be used with a colon <color_red>:</color> to search in other ways.  Additional filters "
        "are separated by commas <color_red>,</color>.\n"
        "Filtering by difficulty can accept range; "
        "<color_yellow>l</color><color_white>:5~10</color> for all recipes from difficulty 5 to 10.\n"
        "\n\n"
        "<color_white>Examples:</color>\n" );

// --- Recipe result info ---

static void insert_block_separator( std::vector<iteminfo> &info_vec,
                                    const std::string &title, int panel_width )
{
    info_vec.emplace_back( "DESCRIPTION", "--" );
    info_vec.emplace_back( "DESCRIPTION", std::string( center_text_pos( title, 0,
                           panel_width ), ' ' ) +
                           "<bold>" + title + "</bold>" );
    info_vec.emplace_back( "DESCRIPTION", "--" );
}

static void result_item_header( item &dummy_item, int quantity_per_batch,
                                std::vector<iteminfo> &info, const std::string &classification,
                                bool uses_charges, const recipe &rec, int batch_size,
                                const std::string &description = {} )
{
    int total_quantity = quantity_per_batch * batch_size;
    if( uses_charges ) {
        std::string display_name = ( description.empty() ? dummy_item.display_name() : description );
        dummy_item.charges = total_quantity;
        info.emplace_back( "DESCRIPTION",
                           "<bold>" + classification + ": </bold>" + display_name );
        dummy_item.charges /= total_quantity;
    } else {
        std::string display_name = ( description.empty() ? dummy_item.display_name(
                                         total_quantity ) : description );
        info.emplace_back( "DESCRIPTION",
                           "<bold>" + classification + ": </bold>" + display_name +
                           ( total_quantity == 1 ? "" : string_format( " (%d)", total_quantity ) ) );
    }
    if( dummy_item.has_flag( flag_VARSIZE ) &&
        dummy_item.has_flag( flag_FIT ) ) {
        std::vector<std::vector<item_comp> > item_component_reqs =
            rec.simple_requirements().get_components();
        bool has_varsize_components = false;
        for( const std::vector<item_comp> &component_options : item_component_reqs ) {
            for( const item_comp &component : component_options ) {
                const itype *type = item::find_type( component.type );
                if( type->has_flag( flag_VARSIZE ) ) {
                    has_varsize_components = true;
                    break;
                }
            }
            if( has_varsize_components ) {
                break;
            }
        }
        if( has_varsize_components ) {
            info.emplace_back( "DESCRIPTION",
                               _( "<bold>Note:</bold> If crafted from poorly-fitting components, the resulting item may also be poorly-fitted." ) );
        }
    }
}

static void result_item_details( item &dummy_item, int quantity_per_batch,
                                 std::vector<iteminfo> &details_info, const std::string &classification,
                                 bool uses_charges, const recipe &rec, int batch_size,
                                 const std::string &description = {},
                                 const iteminfo_query *query = nullptr )
{
    std::vector<iteminfo> temp_info;
    int total_quantity = quantity_per_batch * batch_size;
    result_item_header( dummy_item, quantity_per_batch, details_info, classification, uses_charges,
                        rec, batch_size, description );
    const iteminfo_query *q = query ? query : &iteminfo_query::all;
    if( uses_charges ) {
        dummy_item.charges *= total_quantity;
        dummy_item.info( temp_info, q, 1 );
        dummy_item.charges /= total_quantity;
    } else {
        dummy_item.info( temp_info, q, total_quantity );
    }
    details_info.insert( std::end( details_info ), std::begin( temp_info ), std::end( temp_info ) );
}

static void result_byproducts( const recipe &rec,
                               std::vector<iteminfo> &summary_info, std::vector<iteminfo> &details_info,
                               int batch_size, int panel_width )
{
    const std::string byproduct_string = _( "Byproduct" );

    for( const std::pair<const itype_id, int> &bp : rec.get_byproducts() ) {
        insert_block_separator( details_info, byproduct_string, panel_width );
        item dummy_item = item( bp.first );
        bool uses_charges = dummy_item.count_by_charges();
        result_item_header( dummy_item, bp.second, summary_info, _( "With byproduct" ), uses_charges,
                            rec, batch_size );
        result_item_details( dummy_item, bp.second, details_info, byproduct_string, uses_charges,
                             rec, batch_size );
    }
}

item get_recipe_result_item( const recipe &rec, Character &crafter )
{
    item dummy_result = item( rec.result(), calendar::turn, item::default_charges_tag{} );
    if( !rec.variant().empty() ) {
        dummy_result.set_itype_variant( rec.variant() );
    }
    if( dummy_result.has_flag( flag_VARSIZE ) && !dummy_result.has_flag( flag_FIT ) ) {
        item::sizing general_fit = dummy_result.get_sizing( crafter );
        if( general_fit == item::sizing::small_sized_small_char ||
            general_fit == item::sizing::human_sized_human_char ||
            general_fit == item::sizing::big_sized_big_char ||
            general_fit == item::sizing::ignore ) {
            dummy_result.set_flag( flag_FIT );
        }
    }
    if( dummy_result.count_by_charges() ) {
        dummy_result.charges = 1;
    }
    dummy_result.set_var( "recipe_exemplar", rec.ident().str() );
    return dummy_result;
}

// Build an iteminfo_query that hides sections irrelevant for crafting decisions,
// based on the result item type.  Always hidden: price, disassembly,
// "you know nothing you could craft with it".  Conditionally hidden: melee stats
// (unless it's a weapon), gun stats (unless it's a gun), armor stats (unless armor).
static const iteminfo_query &crafting_query_for_item( const item &it )
{
    // Sections always hidden during crafting
    static const std::vector<iteminfo_parts> always_hide = {
        iteminfo_parts::BASE_PRICE,
        iteminfo_parts::BASE_BARTER,
        iteminfo_parts::DESCRIPTION,
        iteminfo_parts::DESCRIPTION_COMPONENTS_DISASSEMBLE,
        iteminfo_parts::DESCRIPTION_CONTENTS,
        iteminfo_parts::DESCRIPTION_APPLICABLE_RECIPES,
        iteminfo_parts::DESCRIPTION_REPAIREDWITH,
    };

    static const std::vector<iteminfo_parts> melee_parts = {
        iteminfo_parts::BASE_DAMAGE,
        iteminfo_parts::BASE_TOHIT,
        iteminfo_parts::BASE_MOVES,
        iteminfo_parts::BASE_DPS,
        iteminfo_parts::BASE_STAMINA,
        iteminfo_parts::BASE_DPSTAM,
        iteminfo_parts::DESCRIPTION_MELEEDMG,
        iteminfo_parts::DESCRIPTION_MELEEDMG_CRIT,
        iteminfo_parts::DESCRIPTION_MELEEDMG_TYPES,
        iteminfo_parts::DESCRIPTION_MELEEDMG_MOVES,
        iteminfo_parts::DESCRIPTION_TECHNIQUES,
        iteminfo_parts::DESCRIPTION_APPLICABLEMARTIALARTS,
    };

    // Default: hide always_hide + melee
    static const iteminfo_query q_default = [&]() {
        std::vector<iteminfo_parts> hide = always_hide;
        hide.insert( hide.end(), melee_parts.begin(), melee_parts.end() );
        return iteminfo_query( iteminfo_query::all & ~iteminfo_query( hide ) );
    }
    ();

    // Weapons: hide always_hide only (keep melee)
    static const iteminfo_query q_melee = iteminfo_query(
            iteminfo_query::all & ~iteminfo_query( always_hide ) );

    if( it.is_melee() ) {
        return q_melee;
    }
    return q_default;
}

std::vector<iteminfo> recipe_result_info( const recipe &rec, Character &crafter,
        int batch_size, int panel_width, bool full_info )
{
    std::vector<iteminfo> info;
    std::vector<iteminfo> details_info;

    item dummy_result = get_recipe_result_item( rec, crafter );
    std::string result_description;
    if( dummy_result.is_null() ) {
        result_description = rec.get_description( crafter );
    }
    bool result_uses_charges = dummy_result.count_by_charges();
    int const makes_amount = rec.makes_amount();
    item dummy_container;

    const iteminfo_query *query = full_info ? nullptr : &crafting_query_for_item( dummy_result );

    const std::string result_string = _( "Result" );
    const std::string recipe_output_string = _( "Recipe Outputs" );
    const std::string recipe_result_string = _( "Recipe Result" );
    const std::string container_string = _( "Container" );
    const std::string in_container_string = _( "In sealed container" );
    const std::string container_info_string = _( "Container Information" );

    if( rec.container_id() == itype_id::NULL_ID() && !rec.has_byproducts() ) {
        insert_block_separator( details_info, recipe_result_string, panel_width );
        result_item_details( dummy_result, makes_amount, details_info, result_string,
                             result_uses_charges, rec, batch_size, result_description, query );
    } else {
        insert_block_separator( info, recipe_output_string, panel_width );
        if( rec.container_id() != itype_id::NULL_ID() ) {
            dummy_container = item( rec.container_id(), calendar::turn, item::default_charges_tag{} );
            result_item_header( dummy_result, makes_amount, info, recipe_result_string,
                                result_uses_charges, rec, batch_size );
            result_item_header( dummy_container, 1, info, in_container_string, false, rec, batch_size );
            insert_block_separator( details_info, recipe_result_string, panel_width );
            result_item_details( dummy_result, makes_amount, details_info, recipe_result_string,
                                 result_uses_charges, rec, batch_size, {}, query );
            insert_block_separator( details_info, container_info_string, panel_width );
            result_item_details( dummy_container, 1, details_info, container_string, false,
                                 rec, batch_size, {}, query );
        } else {
            result_item_header( dummy_result, makes_amount, info, recipe_result_string,
                                result_uses_charges, rec, batch_size );
            insert_block_separator( details_info, recipe_result_string, panel_width );
            result_item_details( dummy_result, makes_amount, details_info, recipe_result_string,
                                 result_uses_charges, rec, batch_size, {}, query );
        }
        if( rec.has_byproducts() ) {
            result_byproducts( rec, info, details_info, batch_size, panel_width );
        }
        info.emplace_back( "DESCRIPTION", "  " );
    }
    info.insert( std::end( info ), std::begin( details_info ), std::end( details_info ) );
    return info;
}

// --- Recipe list pipeline ---

static void recursively_expand_recipes( std::vector<const recipe *> &current,
                                        std::vector<int> &indent,
                                        std::map<const recipe *, availability> &availability_cache, int i,
                                        Character &crafter, bool unread_recipes_first, bool highlight_unread_recipes,
                                        const recipe_subset &available_recipes, const std::set<recipe_id> &hidden_recipes,
                                        bool camp_crafting, temp_crafting_inventory *inventory_override )
{
    std::vector<const recipe *> tmp;
    for( const recipe_id &nested : current[i]->nested_category_data ) {

        if( available_recipes.contains( &nested.obj() )
            && hidden_recipes.find( nested ) == hidden_recipes.end()
          ) {
            tmp.push_back( &nested.obj() );
            indent.insert( indent.begin() + i + 1, indent[i] + 2 );
            cached_availability( availability_cache, crafter, nested.obj(), camp_crafting,
                                 inventory_override );
        }
    }

    const bool want_unread = highlight_unread_recipes && unread_recipes_first;
    const crafting_cost_context sort_ctx{ crafter.book_bonuses_nearby(), {} };
    std::stable_sort( tmp.begin(), tmp.end(), [
                       &crafter, &availability_cache, want_unread, &sort_ctx
    ]( const recipe * const a, const recipe * const b ) {
        const bool a_read = !want_unread || uistate.read_recipes.count( a->ident() );
        const bool b_read = !want_unread || uistate.read_recipes.count( b->ident() );
        return recipe_sort_compare( a, b,
                                    availability_cache.at( a ), availability_cache.at( b ),
                                    crafter, sort_ctx, a_read, b_read, want_unread );
    } );

    current.insert( current.begin() + i + 1, tmp.begin(), tmp.end() );
}

// TODO: Make this more efficient
static void expand_recipes( std::vector<const recipe *> &current,
                            std::vector<int> &indent,
                            std::map<const recipe *, availability> &availability_cache,
                            Character &crafter, bool unread_recipes_first, bool highlight_unread_recipes,
                            const recipe_subset &available_recipes, const std::set<recipe_id> &hidden_recipes,
                            bool camp_crafting, temp_crafting_inventory *inventory_override )
{
    for( size_t i = 0; i < current.size(); ++i ) {
        if( current[i]->is_nested()
            && uistate.expanded_recipes.find( current[i]->ident() ) != uistate.expanded_recipes.end()
          ) {
            recursively_expand_recipes( current, indent, availability_cache, i, crafter,
                                        unread_recipes_first, highlight_unread_recipes, available_recipes,
                                        hidden_recipes, camp_crafting, inventory_override );
        }
    }
}

std::string list_nested( Character &crafter, const recipe *rec,
                         const recipe_subset &available_recipes,
                         int indent_level )
{
    std::string description;
    availability avail( crafter, rec );
    if( rec->is_nested() ) {
        description += colorize( std::string( indent_level,
                                              ' ' ) + rec->result_name() + ":\n", avail.color() );
        for( const recipe_id &r : rec->nested_category_data ) {
            description += list_nested( crafter, &r.obj(), available_recipes, indent_level + 2 );
        }
    } else if( available_recipes.contains( rec ) ) {
        description += colorize( std::string( indent_level,
                                              ' ' ) + rec->result_name() + "\n", avail.color() );
    }

    return description;
}

recipe_list_data build_recipe_list(
    std::vector<const recipe *> picking,
    bool skip_hidden_filter,
    bool skip_sort,
    Character &crafter,
    bool camp_crafting,
    temp_crafting_inventory *inventory_override,
    bool highlight_unread,
    bool unread_first,
    std::map<const recipe *, availability> &availability_cache,
    const recipe_subset &available_recipes )
{
    recipe_list_data result;

    // UIINV-J03: recipes hidden by their JSON condition aren't listed, not even as hidden ones
    picking.erase( std::remove_if( picking.begin(), picking.end(),
    [&crafter]( const recipe * r ) {
        return r->hide_if_condition_fails() && !r->meets_condition( crafter );
    } ), picking.end() );

    if( skip_hidden_filter ) {
        result.entries = std::move( picking );
    } else {
        for( const recipe *r : picking ) {
            if( uistate.hidden_recipes.find( r->ident() ) != uistate.hidden_recipes.end() ) {
                continue;
            }
            // Hide nested groups with no available child recipes
            if( r->is_nested() ) {
                bool has_child = false;
                for( const recipe_id &child : r->nested_category_data ) {
                    if( available_recipes.contains( &child.obj() ) ) {
                        has_child = true;
                        break;
                    }
                }
                if( !has_child ) {
                    continue;
                }
            }
            result.entries.push_back( r );
        }
        result.num_hidden = picking.size() - result.entries.size();
    }

    // Cache availability on first display
    for( const recipe *e : result.entries ) {
        cached_availability( availability_cache, crafter, *e, camp_crafting, inventory_override );
    }

    if( !skip_sort ) {
        const bool want_unread = highlight_unread && unread_first;
        const crafting_cost_context sort_ctx{ crafter.book_bonuses_nearby(), {} };
        std::stable_sort( result.entries.begin(), result.entries.end(), [
                       &crafter, &availability_cache, want_unread, &sort_ctx
        ]( const recipe * const a, const recipe * const b ) {
            const bool a_read = !want_unread || uistate.read_recipes.count( a->ident() );
            const bool b_read = !want_unread || uistate.read_recipes.count( b->ident() );
            return recipe_sort_compare( a, b,
                                        availability_cache.at( a ), availability_cache.at( b ),
                                        crafter, sort_ctx, a_read, b_read, want_unread );
        } );
    }

    // Set up indents and expand nested categories (must happen after sort)
    result.indent.assign( result.entries.size(), 0 );
    expand_recipes( result.entries, result.indent, availability_cache, crafter,
                    unread_first, highlight_unread, available_recipes, uistate.hidden_recipes,
                    camp_crafting, inventory_override );

    // Build the parallel availability vector
    result.available.reserve( result.entries.size() );
    std::transform( result.entries.begin(), result.entries.end(),
    std::back_inserter( result.available ), [&]( const recipe * e ) {
        return availability_cache.at( e );
    } );

    return result;
}

std::string approx_craft_time( int turns )
{
    if( turns <= 0 ) {
        return _( "no time" );
    }
    const int min = turns / 60;
    const int hr = min / 60;

    if( min == 0 ) {
        return _( "under a minute" );
    }
    if( min < 5 ) {
        const int m = ( turns + 59 ) / 60;
        return string_format( n_gettext( "about %d minute", "about %d minutes", m ), m );
    }
    if( min < 30 ) {
        const int m = ( ( min + 4 ) / 5 ) * 5;
        return string_format( n_gettext( "about %d minute", "about %d minutes", m ), m );
    }
    if( min < 60 ) {
        const int m = ( ( min + 9 ) / 10 ) * 10;
        if( m >= 60 ) {
            return _( "about 1 hour" );
        }
        return string_format( n_gettext( "about %d minute", "about %d minutes", m ), m );
    }
    if( hr < 3 ) {
        const int total_q = ( min + 14 ) / 15;
        const int h = total_q / 4;
        const int q = total_q % 4;
        if( q == 0 ) {
            return string_format( n_gettext( "about %d hour", "about %d hours", h ), h );
        }
        if( q == 2 ) {
            return string_format( _( "about %d.5 hours" ), h );
        }
        return string_format( _( "about %d h %d min" ), h, q * 15 );
    }
    if( hr < 6 ) {
        const int half_h = ( min + 29 ) / 30;
        const int h = half_h / 2;
        if( half_h % 2 ) {
            return string_format( _( "about %d.5 hours" ), h );
        }
        return string_format( n_gettext( "about %d hour", "about %d hours", h ), h );
    }
    if( hr < 18 ) {
        const int h = ( min + 59 ) / 60;
        return string_format( n_gettext( "about %d hour", "about %d hours", h ), h );
    }
    if( hr < 48 ) {
        const int h = ( ( hr + 3 ) / 4 ) * 4;
        if( h <= 24 ) {
            return _( "about a day" );
        }
        return string_format( _( "about %d hours" ), h );
    }
    const int d = ( hr + 23 ) / 24;
    return string_format( n_gettext( "about %d day", "about %d days", d ), d );
}

nc_color activity_level_color( float exertion )
{
    if( exertion >= 6.0f ) {
        return c_red;
    } else if( exertion >= 4.0f ) {
        return c_yellow;
    } else if( exertion >= 2.0f ) {
        return c_light_gray;
    }
    return c_green;
}

const std::string &find_tool_group_name( const std::vector<tool_comp> &alts )
{
    static std::map<std::vector<itype_id>, std::string> cache;
    static bool built = false;
    static const std::string empty;
    if( !built ) {
        for( const auto &pair : requirement_data::all() ) {
            const requirement_data &rdata = pair.second;
            if( !rdata.has_display_name() ) {
                continue;
            }
            for( const auto &tg : rdata.get_tools() ) {
                std::vector<itype_id> ids;
                ids.reserve( tg.size() );
                for( const tool_comp &tc : tg ) {
                    ids.push_back( tc.type );
                }
                std::sort( ids.begin(), ids.end() );
                cache.try_emplace( std::move( ids ), rdata.display_name() );
            }
        }
        built = true;
    }
    std::vector<itype_id> ids;
    ids.reserve( alts.size() );
    for( const tool_comp &tc : alts ) {
        ids.push_back( tc.type );
    }
    std::sort( ids.begin(), ids.end() );
    auto it = cache.find( ids );
    return it != cache.end() ? it->second : empty;
}

// --- Recipe info panel ---

bool recipe_panel_text::operator==( const recipe_panel_text &rhs ) const
{
    return text == rhs.text && color == rhs.color;
}

bool recipe_panel_group::operator==( const recipe_panel_group &rhs ) const
{
    return index == rhs.index && label == rhs.label && options == rhs.options;
}

bool recipe_panel_requirement::operator==( const recipe_panel_requirement &rhs ) const
{
    return shown == rhs.shown && qualities == rhs.qualities && groups == rhs.groups;
}

bool recipe_panel_step::operator==( const recipe_panel_step &rhs ) const
{
    return name == rhs.name && time == rhs.time && batch_note == rhs.batch_note &&
           activity == rhs.activity && activity_color == rhs.activity_color &&
           unattended == rhs.unattended && proficiencies == rhs.proficiencies && tools == rhs.tools;
}

bool recipe_panel_skill::operator==( const recipe_panel_skill &rhs ) const
{
    return name == rhs.name && level == rhs.level && required == rhs.required &&
           suffix == rhs.suffix && color == rhs.color;
}

bool recipe_panel_proficiency::operator==( const recipe_panel_proficiency &rhs ) const
{
    return name == rhs.name && required == rhs.required && known == rhs.known &&
           time_multiplier == rhs.time_multiplier && skill_penalty == rhs.skill_penalty;
}

bool recipe_panel::operator==( const recipe_panel &rhs ) const
{
    return result_name == rhs.result_name && source_text == rhs.source_text &&
           description == rhs.description && stats_plain == rhs.stats_plain &&
           stats_colored == rhs.stats_colored && too_dark == rhs.too_dark &&
           required_proficiencies == rhs.required_proficiencies && too_complex == rhs.too_complex &&
           npc_cannot_craft == rhs.npc_cannot_craft && npc_reason == rhs.npc_reason &&
           batch_savings == rhs.batch_savings && base_time == rhs.base_time &&
           skills == rhs.skills && proficiencies == rhs.proficiencies &&
           morale_mult == rhs.morale_mult && light_mult == rhs.light_mult &&
           pain_mult == rhs.pain_mult && limb_mult == rhs.limb_mult && mut_mult == rhs.mut_mult &&
           mut_skill_bonus == rhs.mut_skill_bonus && intelligence == rhs.intelligence &&
           crafting_stat_name == rhs.crafting_stat_name &&
           int_delta == rhs.int_delta && has_farsight == rhs.has_farsight &&
           vision_penalty == rhs.vision_penalty && total_time == rhs.total_time &&
           success == rhs.success && destruction == rhs.destruction && darkness == rhs.darkness &&
           nearby == rhs.nearby && practice_requirement == rhs.practice_requirement &&
           practice_gains == rhs.practice_gains &&
           practice_gains_color == rhs.practice_gains_color &&
           practice_trains == rhs.practice_trains && single_step_name == rhs.single_step_name &&
           character_requirements == rhs.character_requirements &&
           components == rhs.components && tools == rhs.tools && steps == rhs.steps &&
           resources_shown == rhs.resources_shown && resources == rhs.resources &&
           known_by == rhs.known_by && nested == rhs.nested;
}

namespace
{
constexpr std::array<scaling_stat, 4> character_requirement_display_order = {
    STAT_STR, STAT_DEX, STAT_INT, STAT_PER
};

std::string character_stat_name( const scaling_stat stat )
{
    switch( stat ) {
        case STAT_STR:
            return _( "strength" );
        case STAT_DEX:
            return _( "dexterity" );
        case STAT_INT:
            return _( "intelligence" );
        case STAT_PER:
            return _( "perception" );
        default:
            return "<-error->";
    }
}

std::string character_requirement_text( const scaling_stat stat, const int requirement,
                                        const int current )
{
    const std::string name = character_stat_name( stat );
    std::string text = string_format( "%s %d", name, requirement );
    if( current < requirement ) {
        //~ Shown after an unmet crafting stat requirement. %1$s: stat name, %2$d: current stat value
        text += string_format( _( " (current %1$s %2$d)" ), name, current );
    }
    return text;
}

// The "Components:" block.  The colours read the availability can_make_with_inventory()
// leaves on each component, so it runs first.  As the menu always has, the components are
// checked for the player character.
recipe_panel_requirement panel_components( const requirement_data &req,
        const temp_crafting_inventory &crafting_inv,
        const std::function<bool( const item & )> &filter, int batch_size, bool need_full_magazine )
{
    recipe_panel_requirement block;
    const requirement_data::alter_item_comp_vector &comp_groups = req.get_components();
    if( comp_groups.empty() ) {
        return block;
    }
    block.shown = true;

    req.can_make_with_inventory( &get_player_character(), crafting_inv, filter );

    // how many of a given component the player has on hand
    const auto avail_count = [&crafting_inv, &filter]( const item_comp & ic ) -> int {
        if( item::count_by_charges( ic.type ) )
        {
            return crafting_inv.charges_of( ic.type, INT_MAX, filter );
        }
        return crafting_inv.amount_of( ic.type, false, INT_MAX, filter );
    };
    const auto component_text = [batch_size, need_full_magazine]
    ( const item_comp & comp, const int available ) {
        std::string text = comp.to_string( batch_size, available );
        if( need_full_magazine && item( comp.type ).is_magazine() ) {
            //~ Appended to the name of a battery or ammunition magazine in the crafting menu.  It means the component must be completely charged or loaded.
            text += _( " (full)" );
        }
        return text;
    };

    for( int gi = 0; gi < static_cast<int>( comp_groups.size() ); ++gi ) {
        const std::vector<item_comp> &comp_alts = comp_groups[gi];
        if( comp_alts.empty() ) {
            continue;
        }
        // Available components first, so they aren't hidden behind "or N more".
        // Order: has enough > has some > has none
        std::vector<std::pair<const item_comp *, int>> ranked;
        ranked.reserve( comp_alts.size() );
        bool any_available = false;
        for( const item_comp &ic : comp_alts ) {
            const bool has = ic.has( &get_player_character(), crafting_inv, filter, batch_size );
            any_available = any_available || has;
            ranked.emplace_back( &ic, has ? 0 : avail_count( ic ) > 0 ? 1 : 2 );
        }
        std::stable_sort( ranked.begin(), ranked.end(),
                          []( const std::pair<const item_comp *, int> &a,
        const std::pair<const item_comp *, int> &b ) {
            return a.second < b.second;
        } );

        recipe_panel_group &group = block.groups.emplace_back();
        group.index = gi;
        for( const std::pair<const item_comp *, int> &alt : ranked ) {
            const item_comp &ic = *alt.first;
            group.options.push_back( {
                component_text( ic, avail_count( ic ) ),
                ic.get_color( &get_player_character(), any_available, crafting_inv, filter, batch_size )
            } );
        }
    }
    return block;
}

// The "Tools:" block of `req`.  `group_offset` is added to the tool groups' indices.
recipe_panel_requirement panel_tools( const requirement_data &req,
                                      const temp_crafting_inventory &crafting_inv, int batch_size, int group_offset )
{
    recipe_panel_requirement block;
    const requirement_data::alter_tool_comp_vector &tool_groups = req.get_tools();
    const requirement_data::alter_quali_req_vector &qual_groups = req.get_qualities();
    if( tool_groups.empty() && qual_groups.empty() ) {
        return block;
    }
    block.shown = true;

    // one line per quality group, coloured by availability
    for( const std::vector<quality_requirement> &qual_alts : qual_groups ) {
        std::vector<bool> has;
        has.reserve( qual_alts.size() );
        for( const quality_requirement &qr : qual_alts ) {
            has.push_back( qr.has( &get_player_character(), crafting_inv, return_true<item>, 1 ) );
        }
        const bool any_has = std::find( has.begin(), has.end(), true ) != has.end();
        std::vector<std::string> alternatives;
        alternatives.reserve( qual_alts.size() );
        for( size_t i = 0; i < qual_alts.size(); ++i ) {
            const nc_color col = has[i] ? c_green : any_has ? c_dark_gray : c_red;
            alternatives.emplace_back( colorize( qual_alts[i].to_string( 1 ), col ) );
        }
        block.qualities.push_back( string_join( alternatives, _( " OR " ) ) );
    }

    // tool groups, with the names of named requirements, available tools first
    for( size_t gi = 0; gi < tool_groups.size(); ++gi ) {
        const std::vector<tool_comp> &alts = tool_groups[gi];
        if( alts.empty() ) {
            continue;
        }
        const std::string &group_name = find_tool_group_name( alts );

        // one alternative per tool type
        bool any_available = false;
        std::set<itype_id> seen;
        std::vector<std::pair<const tool_comp *, bool>> unique_alts;
        for( const tool_comp &tc : alts ) {
            if( seen.insert( tc.type ).second ) {
                const bool has = tc.has( &get_player_character(), crafting_inv, return_true<item>,
                                         batch_size );
                unique_alts.emplace_back( &tc, has );
                any_available = any_available || has;
            }
        }
        std::stable_partition( unique_alts.begin(), unique_alts.end(),
        []( const std::pair<const tool_comp *, bool> &tc ) {
            return tc.second;
        } );

        recipe_panel_group &group = block.groups.emplace_back();
        group.index = group_offset + static_cast<int>( gi );
        group.label = string_format( _( "• %s: " ), !group_name.empty() ? group_name :
                                     _( "One of" ) );
        for( const std::pair<const tool_comp *, bool> &tc : unique_alts ) {
            group.options.push_back( {
                tc.first->to_string( batch_size ),
                tc.second ? c_green : any_available ? c_dark_gray : c_red
            } );
        }
    }
    return block;
}
} // namespace

recipe_panel build_recipe_panel( const recipe &recp, const availability &avail, int batch_size,
                                 const recipe_panel_context &ctx )
{
    Character &crafter = ctx.crafter;
    recipe_panel panel;
    const bool nested = recp.is_nested();
    const bool knows = crafter.knows_recipe( &recp );
    const temp_crafting_inventory &crafting_inv = avail.inv_override ? *avail.inv_override :
            crafter.crafting_inventory();

    panel.result_name = recp.result_name( true );

    // Each computed once: the summary line and the details table both show them.
    std::optional<crafting_cost_context> cost_ctx;
    float success_chance = 1.0f;
    int expected_turns = 0;
    if( !nested ) {
        cost_ctx = crafting_cost_context::for_recipe( crafter, recp );
        success_chance = crafter.recipe_success_chance( recp );
        expected_turns = static_cast<int>( crafter.expected_time_to_craft( recp, batch_size,
                                           *cost_ctx ) / to_moves<int>( 1_turns ) );
    }

    // source attribution
    if( !knows ) {
        const std::set<itype_id> books = crafter.get_books_for_recipe( crafting_inv, &recp );
        if( !books.empty() ) {
            panel.source_text = string_format( _( "as written in %s" ), item::nname( *books.begin() ) );
            const int extra = static_cast<int>( books.size() ) - 1;
            if( extra > 0 ) {
                panel.source_text += string_format( _( ", and %d more" ), extra );
            }
        }
    }
    if( panel.source_text.empty() && !nested ) {
        // Confident phrases for >=60% success, uncertain for <60%
        const size_t idx = std::hash<std::string> {}( recp.ident().str() );
        if( success_chance >= 0.6f ) {
            static const std::array<std::string, 2> confident = {{
                    translate_marker( "(…as you remember it)" ),
                    translate_marker( "(…as you recall it)" ),
                }
            };
            panel.source_text = _( confident[idx % confident.size()] );
        } else {
            static const std::array<std::string, 2> uncertain = {{
                    translate_marker( "(…as you imagine it)" ),
                    translate_marker( "(…as you see it)" ),
                }
            };
            panel.source_text = _( uncertain[idx % uncertain.size()] );
        }
    }

    // item flavor text
    if( nested || recp.is_practice() ) {
        panel.description = recp.get_description( crafter );
    } else if( recp.result() ) {
        if( !recp.variant().empty() ) {
            item temp_result( recp.result() );
            temp_result.set_itype_variant( recp.variant() );
            panel.description = temp_result.variant_description();
        }
        if( panel.description.empty() ) {
            panel.description = item::find_type( recp.result() )->description.translated();
        }
        if( panel.description == "generic item template" ) {
            panel.description.clear();
        }
    }

    // natural-language stats
    if( !nested ) {
        const float success = 100.f * success_chance;
        const nc_color success_col = success < 25.f ? c_red :
                                     success < 50.f ? c_yellow :
                                     success < 75.f ? c_light_gray : c_green;
        const nc_color activity_col = activity_level_color( recp.exertion_level() );
        const std::string activity_lc = lowercase_first_letter( display::activity_level_str(
                                            recp.exertion_level() ) );
        const std::string chance_str = string_format( "~%.0f%%", success );
        const std::string time_str = approx_craft_time( expected_turns );
        const std::string activity_str = string_format( _( "%s activity" ), activity_lc );
        const int total_yield = recp.makes_amount() * batch_size;
        // Single format string so translators can reorder parts.
        if( total_yield > 1 ) {
            //~ %1$s: success chance, %2$s: yield count, %3$s: time, %4$s: activity level
            const std::string fmt = _( "%1$s chance to yield %2$s in %3$s of %4$s" );
            const std::string yield_as_string = recp.result()->item_measure_prefix( total_yield );
            panel.stats_plain = string_format( fmt, chance_str, yield_as_string, time_str,
                                               activity_str );
            panel.stats_colored = string_format( fmt,
                                                 colorize( chance_str, success_col ),
                                                 colorize( yield_as_string, c_light_blue ),
                                                 colorize( time_str, c_cyan ),
                                                 colorize( activity_str, activity_col ) );
        } else {
            //~ %1$s: success chance, %2$s: time, %3$s: activity level
            const std::string fmt = _( "%1$s chance you can make it in %2$s of %3$s" );
            panel.stats_plain = string_format( fmt, chance_str, time_str, activity_str );
            panel.stats_colored = string_format( fmt,
                                                 colorize( chance_str, success_col ),
                                                 colorize( time_str, c_cyan ),
                                                 colorize( activity_str, activity_col ) );
        }
    }

    // warnings
    for( const proficiency_id &prof : recp.required_proficiencies() ) {
        panel.required_proficiencies.push_back( prof->name() );
    }
    if( !nested ) {
        panel.too_complex = recp.deduped_requirements().is_too_complex();
        panel.npc_cannot_craft = avail.crafter.is_npc()
                                 && !recp.npc_can_craft( panel.npc_reason )
                                 && !avail.inv_override;
    }
    if( batch_size > 1 ) {
        panel.batch_savings = recp.batch_savings_string();
    }

    // details table
    if( !nested ) {
        const int64_t base_moves = recp.time_to_craft_moves(
                                       crafter, {}, recipe_time_flag::ignore_proficiencies );
        panel.base_time = to_string( time_duration::from_moves( base_moves ) );

        const auto skill_row = [&]( const skill_id & sk, int required_level,
        const std::string & suffix ) {
            const int level = crafter.get_skill_level( sk );
            const int delta = level - required_level;
            const nc_color col = delta >= 2 ? c_green : delta >= 0 ? c_light_gray :
                                 delta >= -2 ? c_yellow : c_red;
            panel.skills.push_back( { sk->name(), level, required_level, suffix, col } );
        };
        if( recp.skill_used ) {
            std::string range;
            if( recp.is_practice() && recp.practice_data &&
                recp.practice_data->min_difficulty != recp.practice_data->max_difficulty ) {
                range = string_format( "(%d-%d)",
                                       recp.practice_data->min_difficulty,
                                       recp.practice_data->max_difficulty );
            }
            skill_row( recp.skill_used, recp.get_difficulty( crafter ), range );
        }
        for( const auto &secondary : recp.required_skills ) {
            skill_row( secondary.first, secondary.second, {} );
        }

        // the crafter's group is looked up once, and only if a proficiency needs it
        std::optional<std::vector<Character *>> helpers;
        const auto crafter_or_helper_has_proficiency = [&]( const proficiency_id & prof ) {
            if( avail.crafter.has_proficiency( prof ) ) {
                return true;
            }
            if( !helpers ) {
                helpers = avail.crafter.get_crafting_group();
            }
            return std::any_of( helpers->begin(), helpers->end(), [&prof]( const Character * helper ) {
                return helper->has_proficiency( prof );
            } );
        };
        for( const recipe_proficiency &prof : recp.get_proficiencies() ) {
            panel.proficiencies.push_back( {
                prof.id->name(), prof.required, crafter_or_helper_has_proficiency( prof.id ),
                prof.time_multiplier, prof.skill_penalty
            } );
        }

        panel.morale_mult = crafter.morale_crafting_speed_multiplier( recp );
        panel.light_mult = crafter.lighting_craft_speed_multiplier( recp );
        panel.pain_mult = crafter.pain_crafting_speed_multiplier( recp );
        panel.limb_mult = crafter.limb_score_crafting_speed_multiplier( recp );
        panel.mut_mult = crafter.mut_crafting_speed_multiplier( recp );
        for( const trait_id &mut : crafter.get_functioning_mutations() ) {
            for( const std::pair<const skill_id, int> &bonus : mut->craft_skill_bonus ) {
                if( recp.skill_used == bonus.first ) {
                    panel.mut_skill_bonus += bonus.second;
                }
            }
        }
        // UIINV-J04: what the roll adds, against an average stat of 8
        const Skill &crafting_skill = recp.skill_used.obj();
        panel.crafting_stat_name = character_stat_name( crafting_skill.crafting_stat() );
        panel.intelligence = crafter.get_primary_stat_value( crafting_skill.crafting_stat() );
        panel.int_delta = crafter.crafting_stat_bonus( recp ) -
                          8.0f / crafting_skill.crafting_stat_divisor();
        panel.vision_penalty = crafter.crafting_vision_penalty( recp );
        panel.has_farsight = panel.vision_penalty > 0.0f;
        panel.too_dark = panel.light_mult <= 0.0f;

        panel.total_time = to_string( time_duration::from_turns( expected_turns ) );
        panel.success = 100.f * success_chance;
        panel.destruction = 100.f * crafter.item_destruction_chance( recp );
        panel.darkness = recp.has_flag( "BLIND_EASY" ) ? _( "easy" ) :
                         recp.has_flag( "BLIND_HARD" ) ? _( "hard" ) :
                         _( "impossible" );
        if( recp.result() ) {
            panel.nearby = crafter.crafting_inventory().count_item( recp.result() );
        }

        if( recp.is_practice() && recp.practice_data && recp.skill_used ) {
            const int skill_lvl = crafter.get_skill_level( recp.skill_used );
            if( skill_lvl < recp.practice_data->min_difficulty ) {
                panel.practice_requirement = string_format(
                                                 _( "Requires minimum %s skill of %d (you have %d)" ),
                                                 recp.skill_used->name(),
                                                 recp.practice_data->min_difficulty,
                                                 skill_lvl );
            }
            panel.practice_gains = string_format( _( "This practice will train %s" ),
                                                  recp.skill_used->name() );
            if( recp.practice_data->skill_limit < MAX_SKILL ) {
                panel.practice_gains += string_format( _( " up to level %d" ),
                                                       recp.practice_data->skill_limit );
            }
            panel.practice_gains_color = skill_lvl >= recp.practice_data->skill_limit ?
                                         c_brown : c_green;
            for( const recipe_proficiency &prof : recp.get_proficiencies() ) {
                if( !prof.required && !crafter.has_proficiency( prof.id ) ) {
                    panel.practice_trains.push_back( string_format( _( "Will train: %s" ),
                                                     prof.id->name() ) );
                }
            }
        }
    }

    // recipe
    if( recp.has_steps() && recp.steps().size() <= 1 ) {
        panel.single_step_name = recp.steps().front().name.translated();
    }
    if( recp.has_character_requirements() ) {
        const std::map<scaling_stat, int> &requirements = recp.get_character_requirements();
        for( const scaling_stat stat : character_requirement_display_order ) {
            const auto found = requirements.find( stat );
            if( found == requirements.end() ) {
                continue;
            }
            const int requirement = found->second;
            const int value = crafter.get_primary_stat_value( stat );
            panel.character_requirements.push_back( {
                character_requirement_text( stat, requirement, value ),
                value >= requirement ? c_green : c_red
            } );
        }
        // CRAFT-J06: the recipe's condition, and why it fails
        if( recp.has_condition() ) {
            if( recp.meets_condition( crafter ) ) {
                panel.character_requirements.push_back( {
                    _( "The conditions for this recipe are met." ), c_green
                } );
            } else {
                panel.character_requirements.push_back( { recp.condition_fail_text(), c_red } );
            }
        }
    }
    panel.components = panel_components( recp.simple_requirements(), crafting_inv,
                                         recp.get_component_filter(), batch_size,
                                         recp.has_flag( "NEED_FULL_MAGAZINE" ) );
    panel.tools = panel_tools( recp.simple_requirements(), crafting_inv, batch_size, 0 );
    if( cost_ctx && recp.has_steps() && recp.steps().size() > 1 ) {
        int tool_group_offset = 0;
        for( size_t si = 0; si < recp.steps().size(); ++si ) {
            const recipe_step &step = recp.steps()[si];
            recipe_panel_step &shown = panel.steps.emplace_back();
            const double step_moves = recp.step_budget_moves( crafter, si, batch_size, *cost_ctx );
            shown.name = step.name.translated();
            shown.time = to_string( time_duration::from_moves( static_cast<int64_t>( step_moves ) ) );
            shown.activity = display::activity_level_str( step.exertion );
            shown.activity_color = activity_level_color( step.exertion );
            shown.unattended = step.attention == step_attention::unattended;
            // per-step batch savings annotation
            if( batch_size > 1 ) {
                const double single_moves = recp.step_budget_moves( crafter, si, 1, *cost_ctx );
                if( step_moves < single_moves * batch_size * 0.99 ) {
                    shown.batch_note = string_format( _( " (x%d, saves %s)" ), batch_size,
                                                      to_string( time_duration::from_moves(
                                                              static_cast<int64_t>( single_moves * batch_size - step_moves ) ) ) );
                }
            }
            for( const recipe_proficiency &prof : step.proficiencies ) {
                const bool known = crafter.has_proficiency( prof.id );
                const nc_color pcol = prof.required ? ( known ? c_green : c_red ) :
                                      ( known ? c_dark_gray : c_yellow );
                std::string text = prof.id->name();
                if( prof.required && !known ) {
                    text += _( " (required)" );
                }
                shown.proficiencies.push_back( { text, pcol } );
            }
            shown.tools = panel_tools( step.requirements, crafting_inv, batch_size,
                                       tool_group_offset );
            tool_group_offset += step.requirements.get_tools().size();
        }
    }

    const character_resource_costs &resources = recp.get_character_resources();
    if( !resources.empty() ) {
        panel.resources_shown = true;
        const auto add_resource = [&]( const int amount, const int available,
        const std::string & name ) {
            if( amount == 0 ) {
                return;
            }
            const int total = amount * batch_size;
            panel.resources.push_back( {
                string_format( "  • %d %s", total, name ),
                available >= total ? c_white : c_yellow
            } );
        };
        add_resource( resources.mana,
                      crafter.craft_character_resource_available( magic_energy_type::mana ), _( "mana" ) );
        add_resource( resources.stamina,
                      crafter.craft_character_resource_available( magic_energy_type::stamina ),
                      _( "stamina" ) );
        for( const vitamin_resource_cost &resource : resources.vitamins ) {
            add_resource( resource.value, crafter.craft_vitamin_available( resource ),
                          resource.vitamin.obj().name() );
        }
    }

    // helpers who know this recipe
    if( !knows ) {
        std::vector<const Character *> helpers;
        for( const Character *h : ctx.crafting_group ) {
            if( crafter.getID() != h->getID() && h->knows_recipe( &recp ) ) {
                helpers.push_back( h );
            }
        }
        if( !helpers.empty() ) {
            const std::string helper_list = enumerate_as_string( helpers,
            []( const Character * h ) {
                return colorize( h->is_avatar() ? _( "You" ) : h->get_name(), c_cyan );
            } );
            panel.known_by = string_format( _( "Known by: %s" ), helper_list );
        }
    }

    // a nested group's children
    if( nested ) {
        for( const recipe_id &child_id : recp.nested_category_data ) {
            const recipe &child = child_id.obj();
            if( !ctx.available_recipes.contains( &child ) ) {
                continue;
            }
            const availability &child_avail = cached_availability( ctx.availability_cache, crafter,
                                              child, ctx.camp_crafting, ctx.inventory_override );
            panel.nested.push_back( { child.result_name( true ), child_avail.color() } );
        }
    }
    return panel;
}

bool recipe_panel_cache::key_type::operator==( const key_type &rhs ) const
{
    return rec == rhs.rec && batch_size == rhs.batch_size && crafter == rhs.crafter &&
           inventory == rhs.inventory && inventory_generation == rhs.inventory_generation &&
           inventory_override == rhs.inventory_override &&
           override_generation == rhs.override_generation && turn == rhs.turn &&
           moves == rhs.moves && pos == rhs.pos && lang_version == rhs.lang_version;
}

recipe_panel_cache::key_type recipe_panel_cache::key_for( const recipe &recp,
        const availability &avail, int batch_size, const recipe_panel_context &ctx )
{
    key_type k;
    k.rec = &recp;
    k.batch_size = batch_size;
    k.crafter = &ctx.crafter;
    // rebuilds the crafter's crafting inventory if it is stale, as the panel would
    const temp_crafting_inventory &inv = ctx.crafter.crafting_inventory();
    k.inventory = &inv;
    k.inventory_generation = inv.generation();
    k.inventory_override = avail.inv_override;
    k.override_generation = avail.inv_override ? avail.inv_override->generation() : 0;
    k.turn = calendar::turn;
    k.moves = ctx.crafter.get_moves();
    k.pos = ctx.crafter.pos_abs();
    k.lang_version = detail::get_current_language_version();
    return k;
}

const recipe_panel &recipe_panel_cache::get( const recipe &recp, const availability &avail,
        int batch_size, const recipe_panel_context &ctx )
{
    if( !key || !( *key == key_for( recp, avail, batch_size, ctx ) ) ) {
        panel = build_recipe_panel( recp, avail, batch_size, ctx );
        ++build_count;
        // taken after the build, in case the build refreshed the inventory itself
        key = key_for( recp, avail, batch_size, ctx );
    }
    return panel;
}

void recipe_panel_cache::invalidate()
{
    key.reset();
}

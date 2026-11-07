#include "recipe_dictionary.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_algo.h"
#include "cata_compiler_support.h"
#include "cata_utility.h"
#include "character.h"
#include "character_id.h"
#include "crafting_gui.h"
#include "debug.h"
#include "display.h"
#include "enums.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "init.h"
#include "input.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_location.h"
#include "itype.h"
#include "mapgen.h"
#include "math_parser_type.h"
#include "mod_tracker.h"
#include "output.h"
#include "requirements.h"
#include "ret_val.h"
#include "skill.h"
#include "string_formatter.h"
#include "subbodypart.h"
#include "temp_crafting_inventory.h"
#include "translation.h"
#include "translations.h"
#include "uistate.h"
#include "units.h"
#include "value_ptr.h"
#include "visitable.h"

static const flag_id json_flag_NO_UNCRAFT( "NO_UNCRAFT" );
static const flag_id json_flag_NUTRIENT_OVERRIDE( "NUTRIENT_OVERRIDE" );

static const itype_id itype_debug_item_search( "debug_item_search" );
static const itype_id itype_paper( "paper" );

static const material_id material_cardboard( "cardboard" );
static const material_id material_paper( "paper" );

static const requirement_id requirement_data_uncraft_book( "uncraft_book" );

recipe_dictionary recipe_dict;

namespace
{

// CRAFT-J03: a book disassembles into paper only when at least half of it is paper or
// cardboard (a stone tablet or a leather scroll is not).  One with no material, as before, is.
bool is_mostly_paper( const itype &type )
{
    if( type.mat_portion_total <= 0 ) {
        return true;
    }
    int paper = 0;
    for( const std::pair<const material_id, int> &mat : type.materials ) {
        if( mat.first == material_paper || mat.first == material_cardboard ) {
            paper += mat.second;
        }
    }
    return paper * 2 >= type.mat_portion_total;
}

void delete_if( std::map<recipe_id, recipe> &data,
                const std::function<bool( const recipe & )> &pred )
{
    for( auto it = data.begin(); it != data.end(); ) {
        if( pred( it->second ) ) {
            it = data.erase( it );
        } else {
            ++it;
        }
    }
}

} // namespace

static recipe null_recipe;
static std::set<const recipe *> null_match;

static DynamicDataLoader::deferred_json deferred;

template<>
const recipe &string_id<recipe>::obj() const
{
    const auto iter = recipe_dict.recipes.find( *this );
    if( iter != recipe_dict.recipes.end() ) {
        if( iter->second.obsolete ) {
            return null_recipe;
        }
        return iter->second;
    }
    if( *this != NULL_ID() ) {
        debugmsg( "invalid recipe id \"%s\"", c_str() );
    }
    return null_recipe;
}

template<>
bool string_id<recipe>::is_valid() const
{
    return recipe_dict.recipes.find( *this ) != recipe_dict.recipes.end();
}

const recipe &recipe_dictionary::get_uncraft( const itype_id &id )
{
    auto iter = recipe_dict.uncraft.find( recipe_id( id.str() ) );
    return iter != recipe_dict.uncraft.end() ? iter->second : null_recipe;
}

const recipe &recipe_dictionary::get_craft( const itype_id &id )
{
    auto iter = recipe_dict.recipes.find( recipe_id( id.str() ) );
    return iter != recipe_dict.recipes.end() ? iter->second : null_recipe;
}

// searches for left-anchored partial match in the relevant recipe requirements set
template <class group>
static bool search_reqs( const group &gp, std::string_view txt )
{
    return std::any_of( gp.begin(), gp.end(), [&]( const typename group::value_type & opts ) {
        return std::any_of( opts.begin(),
        opts.end(), [&]( const typename group::value_type::value_type & e ) {
            return lcmatch( e.to_string(), txt );
        } );
    } );
}
// template specialization to make component searches easier
template<>
bool search_reqs( const std::vector<std::vector<item_comp> > &gp,
                  std::string_view txt )
{
    return std::any_of( gp.begin(), gp.end(), [&]( const std::vector<item_comp> &opts ) {
        return std::any_of( opts.begin(), opts.end(), [&]( const item_comp & ic ) {
            return lcmatch( item::nname( ic.type ), txt );
        } );
    } );
}

std::vector<const recipe *> recipe_subset::favorite() const
{
    std::vector<const recipe *> res;

    std::copy_if( recipes.begin(), recipes.end(), std::back_inserter( res ), [&]( const recipe * r ) {
        if( !*r || r->obsolete ) {
            return false;
        }
        return uistate.favorite_recipes.find( r->ident() ) != uistate.favorite_recipes.end();
    } );

    return res;
}

std::vector<const recipe *> recipe_subset::hidden() const
{
    std::vector<const recipe *> res;

    std::copy_if( recipes.begin(), recipes.end(), std::back_inserter( res ), [&]( const recipe * r ) {
        if( !*r || r->obsolete ) {
            return false;
        }
        return uistate.hidden_recipes.find( r->ident() ) != uistate.hidden_recipes.end();
    } );

    return res;
}

std::vector<const recipe *> recipe_subset::expanded() const
{
    std::vector<const recipe *> res;

    std::copy_if( recipes.begin(), recipes.end(), std::back_inserter( res ), [&]( const recipe * r ) {
        if( !*r || r->obsolete ) {
            return false;
        }
        return uistate.expanded_recipes.find( r->ident() ) != uistate.expanded_recipes.end();
    } );

    return res;
}

std::vector<const recipe *> recipe_subset::recent() const
{
    std::vector<const recipe *> res;

    for( auto rec_id = uistate.recent_recipes.rbegin(); rec_id != uistate.recent_recipes.rend();
         ++rec_id ) {
        std::copy_if( recipes.begin(), recipes.end(), std::back_inserter( res ),
        [&rec_id]( const recipe * r ) {
            return *r && !( *rec_id != r->ident() || r->obsolete );
        } );
    }

    return res;
}

// Cache for the search function
static std::map<const itype_id, std::string> item_info_cache;
static time_point cache_valid_turn;

static std::string cached_item_info( const itype_id &item_type )
{
    if( cache_valid_turn != calendar::turn ) {
        cache_valid_turn = calendar::turn;
        item_info_cache.clear();
    }
    if( item_info_cache.count( item_type ) > 0 ) {
        return item_info_cache.at( item_type );
    }
    const item result( item_type );
    item_info_cache[item_type] = result.info( true );
    return item_info_cache.at( item_type );
}

// keep data for one search cycle
static itype filtered_fake_itype;
static item filtered_fake_item;
static std::unordered_set<bodypart_id> filtered_bodyparts;
static std::unordered_set<sub_bodypart_id> filtered_sub_bodyparts;
static std::unordered_set<layer_level> filtered_layers;

template<typename Unit, size_t N>
static Unit can_contain_filter( std::string_view hint, std::string_view txt, Unit max,
                                const std::array<std::pair<std::string_view, Unit>, N> &units )
{
    // TODO: LAMBDA_NORETURN_CLANG21x1 can be replaced with [[noreturn]] once we switch to C++23 on all compilers
    auto const error = [hint, txt]( char const *, size_t /* offset */ ) LAMBDA_NORETURN_CLANG21x1 {
        throw math::runtime_error( _( string_format( hint, txt ) ) );
    };
    // Start at max. On convert failure: results are empty and user knows it is unusable.
    Unit uni = max;
    try {
        uni = detail::read_from_json_string_common<Unit>( txt, units, error );
    } catch( math::runtime_error &err ) {
        popup( err.what() );
    }
    // copy the debug item template (itype)
    filtered_fake_itype = itype( *item_controller->find_template( itype_debug_item_search ) );
    return uni;
}

namespace
{
// CRAFT-B07: the `l:` filter, parsed once per search. "N" matches difficulty N, "A~B" (either
// side optional) the range between; anything else, or no digits, matches every recipe.
struct difficulty_filter {
    bool match_all = true;
    bool use_range = false;
    int start = 0;
    int end = INT_MAX;

    bool matches( int difficulty ) const {
        if( match_all ) {
            return true;
        }
        if( use_range ) {
            return difficulty == clamp( difficulty, start, end );
        }
        return difficulty == start;
    }
};

// Digits past INT_MAX saturate: std::stoi threw std::out_of_range here and ended the game.
int saturating_digits( std::string_view digits )
{
    int64_t value = 0;
    for( const char chr : digits ) {
        value = std::min<int64_t>( value * 10 + ( chr - '0' ), INT_MAX );
    }
    return static_cast<int>( value );
}

difficulty_filter parse_difficulty_filter( std::string_view txt )
{
    difficulty_filter filter;
    std::string range_start;
    std::string range_end;
    for( const char chr : txt ) {
        if( std::isdigit( static_cast<unsigned char>( chr ) ) ) {
            ( filter.use_range ? range_end : range_start ) += chr;
        } else if( chr == '~' ) {
            filter.use_range = true;
        } else {
            // unexpected character
            return difficulty_filter();
        }
    }
    if( range_start.empty() && range_end.empty() ) {
        return difficulty_filter();
    }
    filter.match_all = false;
    if( !range_start.empty() ) {
        filter.start = saturating_digits( range_start );
    }
    if( filter.use_range && !range_end.empty() ) {
        filter.end = saturating_digits( range_end );
    }
    if( filter.use_range && filter.start > filter.end ) {
        std::swap( filter.start, filter.end );
    }
    return filter;
}
} // namespace

std::vector<const recipe *> recipe_subset::search(
    std::string_view txt, const search_type key,
    const Character *crafter,
    const std::function<void( size_t, size_t )> &progress_callback ) const
{
    const difficulty_filter difficulty = key == search_type::difficulty ?
                                         parse_difficulty_filter( txt ) : difficulty_filter();
    auto predicate = [&]( const recipe * r ) {
        if( !*r || r->obsolete ) {
            return false;
        }
        switch( key ) {
            case search_type::name:
                return lcmatch( r->result_name(), txt );

            case search_type::exclude_name:
                return !lcmatch( r->result_name(), txt );

            case search_type::skill: {
                if( r->skill_used && lcmatch( r->skill_used->name(), txt ) ) {
                    return true;
                }
                const auto &skills = r->required_skills;
                return std::any_of( skills.begin(), skills.end(), [&]( const std::pair<skill_id, int> &e ) {
                    return lcmatch( e.first->name(), txt );
                } );
            }

            case search_type::primary_skill:
                return lcmatch( r->skill_used->name(), txt );

            case search_type::component:
                return search_reqs( r->simple_requirements().get_components(), txt );

            case search_type::tool:
                return search_reqs( r->simple_requirements().get_tools(), txt );

            case search_type::quality:
                return search_reqs( r->simple_requirements().get_qualities(), txt );

            case search_type::quality_result: {
                return item::find_type( r->result() )->has_any_quality( txt );
            }

            case search_type::length:
            case search_type::volume:
            case search_type::mass:
                return item( r->result() ).can_contain( filtered_fake_item ).success();

            case search_type::covers: {
                const item result_item( r->result() );
                return std::any_of( filtered_bodyparts.begin(), filtered_bodyparts.end(),
                [&result_item]( const bodypart_id & bp ) {
                    return result_item.covers( bp );
                } )
                || std::any_of( filtered_sub_bodyparts.begin(), filtered_sub_bodyparts.end(),
                [&result_item]( const sub_bodypart_id & sbp ) {
                    return result_item.covers( sbp );
                } );
            }

            case search_type::layer: {
                const std::vector<layer_level> layers = item( r->result() ).get_layer();
                return std::any_of( layers.begin(), layers.end(), []( layer_level l ) {
                    return filtered_layers.count( l );
                } );
            }

            case search_type::description_result: {
                if( r->is_practice() ) {
                    return lcmatch( r->description.translated(), txt );
                } else {
                    // Info is always rendered for avatar anyway, no need to cache per Character then.
                    return lcmatch( remove_color_tags( cached_item_info( r->result() ) ), txt );
                }
            }

            case search_type::proficiency:
                return lcmatch( r->recipe_proficiencies_string(), txt );

            case search_type::book: {
                if( crafter == nullptr ) {
                    debugmsg( "search_type::book requires a crafter to be provided, since it checks crafting group and crafters inventory" );
                    return false;
                }
                const Character &crafter_ref = *crafter;
                const temp_crafting_inventory &crafting_inventory = crafter_ref.crafting_inventory();

                bool found = false;
                crafting_inventory.visit_items(
                [&]( const item_location & node ) {
                    for( const auto &recipe : node->get_available_recipes( crafter_ref ) ) {
                        if( recipe.first == r && ( lcmatch( node->display_name(), txt ) ||
                                                   lcmatch( item::nname( node->typeId() ), txt ) ) ) {
                            found = true;
                            return VisitResponse::ABORT;
                        }
                    }
                    return VisitResponse::NEXT;
                }
                );

                if( found ) {
                    return true;
                }

                std::vector<const Character *> knowing_helpers;
                for( const Character *helper : crafter_ref.get_crafting_group() ) {
                    if( crafter_ref.getID() != helper->getID() && helper->knows_recipe( r ) ) {
                        knowing_helpers.push_back( helper );
                    }
                }

                return std::any_of( knowing_helpers.begin(), knowing_helpers.end(),
                [&txt]( const Character * helper ) {
                    return lcmatch( helper->is_avatar() ? _( "You" ) : helper->get_name(), txt );
                } );
            }

            case search_type::difficulty:
                return difficulty.matches( r->difficulty );

            case search_type::activity_level:
                return lcmatch( display::activity_level_str( r->exertion_level() ), txt );

            default:
                return false;
        }
    };

    // prepare search
    switch( key ) {
        case search_type::length: {
            units::length len = can_contain_filter(
                                    "Failed to convert '%s' to length.\nValid examples:\n122 cm\n1101mm\n2   meter",
                                    txt, units::length::max(), units::length_units );

            filtered_fake_itype.longest_side = len;
            filtered_fake_item = item( &filtered_fake_itype );
            // make the item hard, otherwise longest_side is ignored
            filtered_fake_item.set_flag( flag_HARD );
            break;
        }
        case search_type::volume: {
            units::volume vol = can_contain_filter(
                                    "Failed to convert '%s' to volume.\nValid examples:\n750 ml\n4L",
                                    txt, units::volume::max(), units::volume_units );

            filtered_fake_itype.volume = vol;
            filtered_fake_item = item( &filtered_fake_itype );
            break;
        }
        case search_type::mass: {
            units::mass mas = can_contain_filter(
                                  "Failed to convert '%s' to mass.\nValid examples:\n12 mg\n400g\n25  kg",
                                  txt, units::mass::max(), units::mass_units );

            filtered_fake_itype.weight = mas;
            filtered_fake_item = item( &filtered_fake_itype );
            break;
        }
        case search_type::covers: {
            filtered_bodyparts.clear();
            filtered_sub_bodyparts.clear();
            for( const body_part &bp : all_body_parts ) {
                const bodypart_str_id &bp_str_id = convert_bp( bp );
                if( lcmatch( body_part_name( bp_str_id, 1 ), txt )
                    || lcmatch( body_part_name( bp_str_id, 2 ), txt ) ) {
                    filtered_bodyparts.insert( bp_str_id->id );
                }
                for( const sub_bodypart_str_id &sbp : bp_str_id->sub_parts ) {
                    if( lcmatch( sbp->name.translated(), txt )
                        || lcmatch( sbp->name_multiple.translated(), txt ) ) {
                        filtered_sub_bodyparts.insert( sbp->id );
                    }
                }
            }
            break;
        }
        case search_type::layer: {
            filtered_layers.clear();
            for( layer_level layer = layer_level( 0 ); layer != layer_level::NUM_LAYER_LEVELS; ++layer ) {
                if( lcmatch( item::layer_to_string( layer ), txt ) ) {
                    filtered_layers.insert( layer );
                }
            }
            break;
        }
        default:
            break;
    }

    // search
    std::vector<const recipe *> res;
    size_t i = 0;
    ctxt.register_action( "QUIT" );
    std::chrono::steady_clock::time_point next_input_check = std::chrono::steady_clock::now();
    for( const recipe *r : recipes ) {
        if( !test_mode && std::chrono::steady_clock::now() > next_input_check ) {
            next_input_check = std::chrono::steady_clock::now() + std::chrono::milliseconds( 250 );
            if( ctxt.handle_input( 1 ) == "QUIT" ) {
                return res;
            }
        }
        if( progress_callback ) {
            progress_callback( i, recipes.size() );
        }
        if( predicate( r ) ) {
            res.push_back( r );
        }
        ++i;
    }

    return res;
}

recipe_subset::recipe_subset( const recipe_subset &src, const std::vector<const recipe *> &recipes )
{
    for( const recipe *elem : recipes ) {
        include( elem, src.get_custom_difficulty( elem ) );
    }
}

recipe_subset recipe_subset::reduce(
    std::string_view txt, const search_type key,
    const std::function<void( size_t, size_t )> &progress_callback ) const
{
    return recipe_subset( *this, search( txt, key, nullptr, progress_callback ) );
}

recipe_subset recipe_subset::reduce(
    std::string_view txt, const Character &crafter, const search_type key,
    const std::function<void( size_t, size_t )> &progress_callback ) const
{
    return recipe_subset( *this, search( txt, key, &crafter, progress_callback ) );
}
recipe_subset recipe_subset::intersection( const recipe_subset &subset ) const
{
    std::vector<const recipe *> intersection_result;
    std::set_intersection( this->begin(), this->end(), subset.begin(), subset.end(),
                           std::back_inserter( intersection_result ) );
    return recipe_subset( *this, intersection_result );
}
recipe_subset recipe_subset::difference( const recipe_subset &subset ) const
{
    return difference( subset.recipes );
}
recipe_subset recipe_subset::difference( const std::set<const recipe *> &recipe_set ) const
{
    std::vector<const recipe *> difference_result;
    std::set_difference( this->begin(), this->end(), recipe_set.begin(), recipe_set.end(),
                         std::back_inserter( difference_result ) );
    return recipe_subset( *this, difference_result );
}

std::vector<const recipe *> recipe_subset::recipes_that_produce( const itype_id &item ) const
{
    std::vector<const recipe *> res;

    std::copy_if( recipes.begin(), recipes.end(), std::back_inserter( res ), [&]( const recipe * r ) {
        return !r->obsolete && ( item == r->result() || r->in_byproducts( item ) );
    } );

    return res;
}

bool recipe_subset::empty_category( const crafting_category_id &cat,
                                    const std::string &subcat ) const
{
    if( subcat == "CSC_*_FAVORITE" ) {
        return uistate.favorite_recipes.empty();
    } else if( subcat == "CSC_*_RECENT" ) {
        return uistate.recent_recipes.empty();
    } else if( subcat == "CSC_*_HIDDEN" ) {
        return uistate.hidden_recipes.empty();
    } else if( cat->is_wildcard ) {
        //any other category in CC_* is populated
        return false;
    }

    auto iter = category.find( cat );
    if( iter != category.end() ) {
        if( subcat.empty() ) {
            return false;
        } else {
            for( const recipe *e : iter->second ) {
                if( e->subcategory == subcat ) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<const recipe *> recipe_subset::in_category( const crafting_category_id &cat,
        const std::string &subcat ) const
{
    std::vector<const recipe *> res;
    auto iter = category.find( cat );
    if( iter != category.end() ) {
        if( subcat.empty() ) {
            std::copy_if( iter->second.begin(), iter->second.end(),
            std::back_inserter( res ), [&]( const recipe * e ) {
                return !e->obsolete;
            } );
        } else {
            std::copy_if( iter->second.begin(), iter->second.end(),
            std::back_inserter( res ), [&subcat]( const recipe * e ) {
                if( e->obsolete ) {
                    return false;
                }
                return e->subcategory == subcat;
            } );
        }
    }
    return res;
}

const std::set<const recipe *> &recipe_subset::of_component( const itype_id &id ) const
{
    auto iter = component.find( id );
    return iter != component.end() ? iter->second : null_match;
}

void recipe_dictionary::load_recipe( const JsonObject &jo, const std::string &src )
{
    load( jo, src, recipe_dict.recipes );
}

void recipe_dictionary::load_uncraft( const JsonObject &jo, const std::string &src )
{
    load( jo, src, recipe_dict.uncraft );
}

void recipe_dictionary::load_practice( const JsonObject &jo, const std::string &src )
{
    load( jo, src, recipe_dict.recipes );
}

void recipe_dictionary::load_nested_category( const JsonObject &jo, const std::string &src )
{
    load( jo, src, recipe_dict.recipes );
}

recipe &recipe_dictionary::load( const JsonObject &jo, const std::string &src,
                                 std::map<recipe_id, recipe> &out )
{
    recipe r;

    // defer entries dependent upon as-yet unparsed definitions
    if( jo.has_string( "copy-from" ) ) {
        recipe_id base = recipe_id( jo.get_string( "copy-from" ) );
        if( !out.count( base ) ) {
            deferred.emplace_back( jo, src );
            jo.allow_omitted_members();
            return null_recipe;
        }
        r = out[ base ];
    }

    r.load( jo, src );
    mod_tracker::assign_src( r, src );
    r.was_loaded = true;

    // Check for duplicate recipe_ids before assigning it to the map
    auto duplicate = out.find( r.ident() );
    if( duplicate != out.end() ) {
        mod_tracker::check_duplicate_entries( r, duplicate->second );
    }
    // CRAFT-J05: a redefinition keeps the place of the id's first definition
    static int64_t loaded_count = 0;
    r.load_order = duplicate != out.end() ? duplicate->second.load_order : ++loaded_count;

    return out[ r.ident() ] = std::move( r );
}

size_t recipe_dictionary::size() const
{
    return recipes.size();
}

std::map<recipe_id, recipe>::const_iterator recipe_dictionary::begin() const
{
    return recipes.begin();
}

std::map<recipe_id, recipe>::const_iterator recipe_dictionary::end() const
{
    return recipes.end();
}

std::map<recipe_id, const recipe *> recipe_dictionary::find_obsoletes(
    const itype_id &item_id ) const
{
    std::map<recipe_id, const recipe *> ret;
    auto p = obsoletes.equal_range( item_id );
    for( auto it = p.first; it != p.second; ++it ) {
        const recipe *r = it->second;
        ret.emplace( r->ident(), r );
    }
    return ret;
}

bool recipe_dictionary::is_item_on_loop( const itype_id &i ) const
{
    return items_on_loops.count( i );
}

void recipe_dictionary::finalize_internal( std::map<recipe_id, recipe> &obj )
{
    for( auto &elem : obj ) {
        erase_if( elem.second.nested_category_data, [&]( const recipe_id & nest ) {
            return !nest.is_valid() || nest->will_be_blacklisted();
        } );
        elem.second.finalize();
        inp_mngr.pump_events();
    }
    // remove any blacklisted or invalid recipes...
    delete_if( []( const recipe & elem ) {
        if( elem.is_blacklisted() ) {
            return true;
        }

        if( elem.obsolete ) {
            return false;
        }

        const std::string error = elem.get_consistency_error();

        if( !error.empty() ) {
            debugmsg( "Recipe %s %s.", elem.ident().c_str(), error.c_str() );
        }

        return !error.empty();
    } );
}

void recipe_dictionary::find_items_on_loops()
{
    // Check for infinite recipe loops in food (which are problematic for
    // nutrient calculations).
    //
    // Start by building a directed graph of itypes to potential components of
    // those itypes.
    items_on_loops.clear();
    std::unordered_map<itype_id, std::vector<itype_id>> potential_components_of;
    for( const itype *i : item_controller->all() ) {
        if( !i->comestible || i->has_flag( json_flag_NUTRIENT_OVERRIDE ) ) {
            continue;
        }
        std::vector<itype_id> &potential_components = potential_components_of[i->get_id()];
        for( const recipe_id &rec : i->recipes ) {
            const requirement_data requirements = rec->simple_requirements();
            const requirement_data::alter_item_comp_vector &component_requirements =
                requirements.get_components();

            for( const std::vector<item_comp> &component_options : component_requirements ) {
                for( const item_comp &component_option : component_options ) {
                    potential_components.push_back( component_option.type );
                }
            }
        }
    }

    // Now check that graph for loops
    std::vector<std::vector<itype_id>> loops = cata::find_cycles( potential_components_of );
    for( const std::vector<itype_id> &loop : loops ) {
        std::string error_message =
            "loop in comestible recipes detected: " + loop.back().str();
        for( const itype_id &i : loop ) {
            error_message += " -> " + i.str();
            items_on_loops.insert( i );
        }
        error_message += ".  Such loops can be broken by either removing or altering "
                         "recipes or marking one of the items involved with the NUTRIENT_OVERRIDE "
                         "flag";
        debugmsg( error_message );
    }
}

void recipe_dictionary::finalize()
{
    DynamicDataLoader::get_instance().load_deferred( deferred );

    // remove abstract recipes
    delete_if( []( const recipe & element ) {
        return element.abstract;
    } );

    finalize_internal( recipe_dict.recipes );
    finalize_internal( recipe_dict.uncraft );

    // CRAFT-J05: the reversible recipe that gives each result its disassembly, when there is no
    // uncraft recipe for it.  The first one met used to win, and the map is in string-intern
    // order, which is not stable between runs (an id named anywhere earlier, in C++ or JSON,
    // moves up).  Now: the one marked "primary", else the first loaded, which is what that order
    // mostly was.
    const auto better_uncraft_source = []( const recipe & a, const recipe & b ) {
        if( a.uncraft_primary != b.uncraft_primary ) {
            return a.uncraft_primary;
        }
        if( a.load_order != b.load_order ) {
            return a.load_order < b.load_order;
        }
        // NOLINTNEXTLINE(cata-use-localized-sorting)
        return a.ident().str() < b.ident().str();
    };
    std::map<recipe_id, const recipe *> uncraft_sources;
    for( const auto &e : recipe_dict.recipes ) {
        const recipe &r = e.second;

        if( r.obsolete ) {
            continue;
        }

        for( const auto &bk : r.booksets ) {
            const itype *booktype = item::find_type( bk.first );
            int req = bk.second.skill_req > 0 ? bk.second.skill_req : std::max( booktype->book->req,
                      r.difficulty );
            islot_book::recipe_with_description_t desc{ &r, req, bk.second.alt_name, bk.second.hidden };
            const_cast<islot_book &>( *booktype->book ).recipes.insert( desc );
        }

        // if reversible and no specific uncraft recipe exists use this recipe

        const string_id<recipe> uncraft_id = recipe_id( r.result().str() );
        if( r.is_reversible() && !recipe_dict.uncraft.count( uncraft_id ) ) {
            const recipe *&source = uncraft_sources[uncraft_id];
            if( source != nullptr && source->uncraft_primary && r.uncraft_primary ) {
                debugmsg( "recipes %s and %s both make %s and are both the primary reversible "
                          "recipe", source->ident().str(), r.ident().str(), r.result().str() );
            }
            if( source == nullptr || better_uncraft_source( r, *source ) ) {
                source = &r;
            }
        }
    }

    for( const auto &[uncraft_id, source] : uncraft_sources ) {
        recipe &uncraft = recipe_dict.uncraft[ uncraft_id ];
        uncraft = *source;
        // CRAFT-B03: disassembly is one stretch of work, and time_to_craft_moves sums a step
        // recipe's steps without reading time.  So the copy drops its steps (their requirements
        // are merged into the recipe's already) and takes the time of the steps someone works
        // through, unless reversible gives a time.
        if( uncraft.has_steps() ) {
            int64_t worked = 0;
            for( const recipe_step &step : uncraft.steps_ ) {
                if( step.attention != step_attention::unattended ) {
                    worked += step.time;
                }
            }
            if( worked > 0 ) {
                uncraft.time = worked;
            }
            uncraft.proficiencies = uncraft.aggregate_proficiencies_;
            uncraft.steps_.clear();
            uncraft.aggregate_proficiencies_.clear();
        }
        if( source->uncraft_time > 0 ) {
            // If a specified uncraft time has been given, use that in the uncraft
            // recipe rather than the original.
            uncraft.time = source->uncraft_time;
        }
    }

    // add pseudo uncrafting recipes
    for( const itype *e : item_controller->all() ) {
        const itype_id id = e->get_id();
        const recipe_id rid = recipe_id( id.str() );

        // books that don't already have an uncrafting recipe
        if( e->book && !recipe_dict.uncraft.count( rid ) && e->weight > 0_gram &&
            !e->has_flag( json_flag_NO_UNCRAFT ) && is_mostly_paper( *e ) ) {
            const int pages = e->weight / itype_paper->weight;
            recipe &bk = recipe_dict.uncraft[rid];
            bk.id = rid;
            bk.result_ = id;
            bk.reversible = true;
            bk.requirements_ = *requirement_data_uncraft_book * pages;
            // TODO: allow specifying time in requirement_data
            bk.time = pages * 10;
        }
    }

    // Check for nested items without a category to make sure nothing is getting lost
    for( const auto &rec : recipe_dict.recipes ) {
        if( rec.second.subcategory == "CSC_*_NESTED" ) {
            bool found = false;
            for( const auto &nest : recipe_dict.recipes ) {
                if( nest.second.is_nested() ) {
                    if( find( nest.second.nested_category_data.begin(), nest.second.nested_category_data.end(),
                              rec.first ) != nest.second.nested_category_data.end() ) {
                        found = true;
                        break;
                    }
                }
            }
            if( !found ) {
                debugmsg( "recipe %s is nested but isn't in a nested group.  It will be impossible to path to by the player.",
                          rec.first.str() );
            }
        }
    }

    // Cache auto-learn recipes and blueprints
    for( const auto &e : recipe_dict.recipes ) {
        if( e.second.obsolete ) {
            recipe_dict.obsoletes.emplace( e.second.result(), &e.second );
        }
        if( e.second.autolearn ) {
            recipe_dict.autolearn.insert( &e.second );
        }
        if( e.second.is_nested() ) {
            recipe_dict.nested.insert( &e.second );
        }
        if( e.second.is_blueprint() ) {
            recipe_dict.blueprints.insert( &e.second );
        }
    }

    recipe_dict.find_items_on_loops();
}

// CRAFT-J05: recipe flags are plain strings, so a typo used to do nothing.  These are the ones
// the game reads (doc/JSON/JSON_FLAGS.md, "Recipes" and "Camp building recipes").
bool is_known_recipe_flag( const std::string_view flag )
{
    static const std::set<std::string, std::less<>> known = {
        "AFFECTED_BY_PAIN", "ALLOW_ROTTEN", "ALWAYS_START_ON_GROUND", "BLIND_EASY", "BLIND_HARD",
        "FULL_MAGAZINE", "NEED_FULL_MAGAZINE", "NO_BENCH", "NO_ENCHANTMENT", "NO_FOOD_REQ",
        "NO_MANIP", "NO_RESIZE", "SECRET"
    };
    if( known.find( flag ) != known.end() ) {
        return true;
    }
    // camp blueprints: MAP_ROTATE_90/180/270 and MAP_MIRROR_HORIZONTAL/VERTICAL, each optionally
    // _IF_ and a direction (or nothing, for the camp's core)
    static const std::array<std::string_view, 5> transforms = {
        "MAP_ROTATE_90", "MAP_ROTATE_180", "MAP_ROTATE_270", "MAP_MIRROR_HORIZONTAL",
        "MAP_MIRROR_VERTICAL"
    };
    static const std::set<std::string, std::less<>> directions = {
        "", "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    for( const std::string_view transform : transforms ) {
        if( flag == transform ) {
            return true;
        }
        const std::string conditional = std::string( transform ) + "_IF_";
        if( string_starts_with( flag, conditional ) &&
            directions.find( flag.substr( conditional.size() ) ) != directions.end() ) {
            return true;
        }
    }
    return false;
}

void recipe_dictionary::check_consistency()
{
    // CRAFT-J05: ids and flags nothing checked before
    const auto check_ids = []( const recipe & r ) {
        const std::string &rid = r.ident().str();
        for( const std::string &flag : r.flags ) {
            if( !is_known_recipe_flag( flag ) ) {
                debugmsg( "recipe %s has unknown flag %s", rid, flag );
            }
        }
        for( const std::vector<effect_on_condition_id> *eocs : {
                 &r.result_eocs, &r.result_item_eocs, &r.start_eocs, &r.fail_eocs,
                 &r.disassembly_eocs
             } ) {
            for( const effect_on_condition_id &eoc : *eocs ) {
                if( !eoc.is_valid() ) {
                    debugmsg( "recipe %s uses unknown effect_on_condition %s", rid, eoc.str() );
                }
            }
        }
        for( const std::map<skill_id, int> *skills : {
                 &r.autolearn_requirements, &r.learn_by_disassembly
             } ) {
            for( const std::pair<const skill_id, int> &sk : *skills ) {
                if( !sk.first.is_valid() ) {
                    debugmsg( "recipe %s uses unknown skill %s in autolearn or decomp_learn", rid,
                              sk.first.str() );
                }
            }
        }
        if( r.byproduct_group && !item_group::group_is_defined( *r.byproduct_group ) ) {
            debugmsg( "recipe %s has unknown byproduct_group %s", rid, r.byproduct_group->str() );
        }
        if( r.is_reversible() && r.result_mult > 1 ) {
            debugmsg( "recipe %s is reversible with result_mult %d: disassembling one result would "
                      "give back the components of all %d", rid, r.result_mult, r.result_mult );
        }
    };
    for( const auto &e : recipe_dict.recipes ) {
        check_ids( e.second );
    }
    for( const auto &e : recipe_dict.uncraft ) {
        // a reversible recipe's copy was checked as the recipe
        if( !recipe_dict.recipes.count( e.second.ident() ) ) {
            check_ids( e.second );
        }
    }

    for( const auto &e : recipe_dict.recipes ) {
        const recipe &r = e.second;

        if( r.category.str().empty() ) {
            if( !r.subcategory.empty() ) {
                debugmsg( "recipe %s has subcategory but no category", r.ident().str() );
            }

            continue;
        }

        if( !r.category.is_valid() ) {
            debugmsg( "recipe %s has invalid category %s", r.ident().str(), r.category.str() );
            continue;
        }

        const std::vector<std::string> &subcategories = r.category->subcategories;

        if( !r.subcategory.empty() ) {
            auto it = std::find( subcategories.begin(), subcategories.end(), r.subcategory );
            if( it == subcategories.end() ) {
                debugmsg(
                    "recipe %s has subcategory %s which is invalid or doesn't match category %s",
                    r.ident().str(), r.subcategory, r.category.str() );
            }
        }
    }

    for( const auto &e : recipe_dict.recipes ) {
        const recipe &r = e.second;

        if( !r.blueprint.is_empty() && !has_update_mapgen_for( r.blueprint ) ) {
            debugmsg( "recipe %s specifies invalid construction_blueprint %s; that should be a "
                      "defined update_mapgen_id but is not", r.ident().str(), r.blueprint.str() );
        }
    }
}

void recipe_dictionary::reset()
{
    recipe_dict.blueprints.clear();
    recipe_dict.autolearn.clear();
    recipe_dict.nested.clear();
    recipe_dict.obsoletes.clear();
    recipe_dict.recipes.clear();
    recipe_dict.uncraft.clear();
    recipe_dict.items_on_loops.clear();
    for( std::pair<JsonObject, std::string> &deferred_json : deferred ) {
        deferred_json.first.allow_omitted_members();
    }
    deferred.clear();
}

void recipe_dictionary::delete_if( const std::function<bool( const recipe & )> &pred )
{
    ::delete_if( recipe_dict.recipes, pred );
    ::delete_if( recipe_dict.uncraft, pred );
}

void recipe_subset::include( const recipe *r, int custom_difficulty )
{
    if( custom_difficulty < 0 ) {
        custom_difficulty = r->difficulty;
    }
    // We always prefer lower difficulty for the subset, but we save it only if it's not default
    if( recipes.count( r ) > 0 ) {
        const auto iter = difficulties.find( r );
        // See if we need to lower the difficulty of the existing recipe
        if( iter != difficulties.end() && custom_difficulty < iter->second ) {
            if( custom_difficulty != r->difficulty ) {
                iter->second = custom_difficulty; // Added again with lower difficulty
            } else {
                difficulties.erase( iter ); // No need to keep the default difficulty. Free some memory
            }
        } else if( custom_difficulty < r->difficulty ) {
            difficulties[r] = custom_difficulty; // Added again with lower difficulty
        }
    } else {
        // add recipe to category and component caches
        for( const auto &opts : r->simple_requirements().get_components() ) {
            for( const item_comp &comp : opts ) {
                component[comp.type].insert( r );
            }
        }
        category[r->category].insert( r );
        // Set the difficulty is it's not the default
        if( custom_difficulty != r->difficulty ) {
            difficulties[r] = custom_difficulty;
        }
        // insert the recipe
        recipes.insert( r );
    }
}

void recipe_subset::remove( const recipe *r )
{
    recipes.erase( r );
}

void recipe_subset::include( const recipe_subset &subset )
{
    for( const recipe * const &elem : subset ) {
        include( elem, subset.get_custom_difficulty( elem ) );
    }
}

int recipe_subset::get_custom_difficulty( const recipe *r ) const
{
    const auto iter = difficulties.find( r );
    if( iter != difficulties.end() ) {
        return iter->second;
    }
    return r->difficulty;
}

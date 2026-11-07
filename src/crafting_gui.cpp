#include "crafting_gui.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cata_imgui.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_id.h"
#include "color.h"
#include "crafting.h"
#include "crafting_gui_helpers.h"
#include "flat_set.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "generic_factory.h"
#include "input.h"
#include "input_context.h"
#include "input_popup.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_location.h"
#include "localized_comparator.h"
#include "options.h"
#include "output.h"
#include "point.h"
#include "popup.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "string_formatter.h"
#include "temp_crafting_inventory.h"
#include "text.h"
#include "translation.h"
#include "translation_cache.h"
#include "translations.h"
#include "type_id.h"
#include "ui_iteminfo.h"
#include "ui_manager.h"
#include "uilist.h"
#include "uistate.h"

namespace
{

generic_factory<crafting_category> craft_cat_list( "recipe_category" );

} // namespace

template<>
const crafting_category &string_id<crafting_category>::obj() const
{
    return craft_cat_list.obj( *this );
}

template<>
bool string_id<crafting_category>::is_valid() const
{
    return craft_cat_list.is_valid( *this );
}

/**
 * return index of newly chosen crafter.
 * return < 0 if error happens or nobody is choosen.
 */
static int choose_crafter( const std::vector<Character *> &crafting_group, int crafter_i,
                           const recipe *rec, bool rec_valid );

static std::string peek_related_recipe( const recipe *current, const recipe_subset &available,
                                        Character &crafter );
static int related_menu_fill( uilist &rmenu,
                              const std::vector<std::pair<itype_id, std::string>> &related_recipes,
                              const recipe_subset &available );
static void compare_recipe_with_item( const item &recipe_item, Character &crafter );
static void prioritize_components( const recipe &recipe, Character &crafter );
static void deprioritize_components( const recipe &recipe );

// Clickable text label -- mouse click or keyboard nav activation.
// nav_idx/nav_cursor/nav_activated support keyboard navigation:
//   nav_idx: this item's index in the nav sequence (-1 = not navigable)
//   nav_cursor: currently focused nav index
//   nav_activated: index that was activated by Enter (-1 = none)
// Returns true when activated (mouse click or keyboard).
static bool clickable_text( const char *label, nc_color col,
                            int nav_idx = -1, int nav_cursor = -1, int nav_activated = -1 )
{
    bool focused = nav_idx >= 0 && nav_idx == nav_cursor;
    if( focused ) {
        // Draw highlight background
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        float w = ImGui::CalcTextSize( label ).x;
        float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(
            cursor, ImVec2( cursor.x + w, cursor.y + h ),
            ImGui::GetColorU32( ImVec4( 0.3f, 0.3f, 0.5f, 0.5f ) ) );
    }
    ImGui::PushStyleColor( ImGuiCol_Text, cataimgui::imvec4_from_color( col ) );
    ImGui::TextUnformatted( label );
    ImGui::PopStyleColor();
    bool hovered = ImGui::IsItemHovered();
    if( hovered ) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        ImGui::GetForegroundDrawList()->AddLine(
            ImVec2( rmin.x, rmax.y ), rmax,
            ImGui::GetColorU32( cataimgui::imvec4_from_color( col ) ) );
    }
    bool mouse_click = hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left );
    bool kb_activate = nav_idx >= 0 && nav_idx == nav_activated;
    return mouse_click || kb_activate;
}

static std::string get_cat_unprefixed( std::string_view prefixed_name )
{
    return std::string( prefixed_name.substr( 3, prefixed_name.size() - 3 ) );
}

// Ceil-rounded human-readable craft time.  Input is game turns (seconds).

void load_recipe_category( const JsonObject &jsobj, const std::string &src )
{
    craft_cat_list.load( jsobj, src );
}

void crafting_category::finalize_all()
{
    craft_cat_list.finalize();
}

void crafting_category::load( const JsonObject &jo, std::string_view )
{
    // Ensure id is correct
    if( id.str().find( "CC_" ) != 0 ) {
        jo.throw_error( "Crafting category id has to be prefixed with 'CC_'" );
    }

    optional( jo, was_loaded, "is_hidden", is_hidden, false );
    optional( jo, was_loaded, "is_practice", is_practice, false );
    optional( jo, was_loaded, "is_building", is_building, false );
    optional( jo, was_loaded, "is_wildcard", is_wildcard, false );
    mandatory( jo, was_loaded, "recipe_subcategories", subcategories,
               auto_flags_reader<> {} );

    // Ensure subcategory ids are correct and remove dupes
    std::string cat_name = get_cat_unprefixed( id.str() );
    std::unordered_set<std::string> known;
    for( auto it = subcategories.begin(); it != subcategories.end(); ) {
        const std::string &subcat_id = *it;
        if( subcat_id.find( "CSC_" + cat_name + "_" ) != 0 && subcat_id != "CSC_ALL" ) {
            jo.throw_error( "Crafting sub-category id has to be prefixed with CSC_<category_name>_" );
        }
        if( known.find( subcat_id ) != known.end() ) {
            it = subcategories.erase( it );
            continue;
        }
        known.emplace( subcat_id );
        ++it;
    }
}

static std::string get_subcat_unprefixed( std::string_view cat,
        const std::string &prefixed_name )
{
    std::string prefix = "CSC_" + get_cat_unprefixed( cat ) + "_";

    if( prefixed_name.find( prefix ) == 0 ) {
        return prefixed_name.substr( prefix.size(), prefixed_name.size() - prefix.size() );
    }

    return prefixed_name == "CSC_ALL" ? translate_marker( "ALL" ) : translate_marker( "NONCRAFT" );
}

void reset_recipe_categories()
{
    craft_cat_list.reset();
}


static input_context make_crafting_context( bool highlight_unread_recipes )
{
    input_context ctxt( "CRAFTING" );
    ctxt.register_cardinal();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "SCROLL_RECIPE_INFO_UP" );
    ctxt.register_action( "SCROLL_RECIPE_INFO_DOWN" );
    ctxt.register_action( "PAGE_UP", to_translation( "Fast scroll up" ) );
    ctxt.register_action( "PAGE_DOWN", to_translation( "Fast scroll down" ) );
    ctxt.register_action( "HOME" );
    ctxt.register_action( "END" );
    ctxt.register_action( "SCROLL_ITEM_INFO_UP" );
    ctxt.register_action( "SCROLL_ITEM_INFO_DOWN" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "RESET_FILTER" );
    ctxt.register_action( "TOGGLE_FAVORITE" );
    ctxt.register_action( "HELP_RECIPE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "CYCLE_BATCH" );
    ctxt.register_action( "BATCH_SIZE_UP" );
    ctxt.register_action( "BATCH_SIZE_DOWN" );
    ctxt.register_action( "CHOOSE_CRAFTER" );
    ctxt.register_action( "RELATED_RECIPES" );
    ctxt.register_action( "HIDE_SHOW_RECIPE" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "TOGGLE_INFO_NAV" );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "SCROLL_DOWN" );
    ctxt.register_action( "COMPARE" );
    ctxt.register_action( "PRIORITIZE_MISSING_COMPONENTS" );
    ctxt.register_action( "DEPRIORITIZE_COMPONENTS" );
    if( highlight_unread_recipes ) {
        ctxt.register_action( "TOGGLE_RECIPE_UNREAD" );
        ctxt.register_action( "MARK_ALL_RECIPES_READ" );
        ctxt.register_action( "TOGGLE_UNREAD_RECIPES_FIRST" );
    }
    return ctxt;
}

namespace
{
class recipe_result_info_cache
{
        Character &crafter;
        std::vector<iteminfo> info;
        const recipe *last_recipe = nullptr;
        int last_panel_width = 0;
        int cached_batch_size = 0;
        bool cached_full_info = true;
        int lang_version = 0;
    public:
        explicit recipe_result_info_cache( Character &c ) : crafter( c ) {}
        // The info of `rec`'s result, by reference: the panel reads it on every redraw.
        const std::vector<iteminfo> &get_result_info( const recipe *rec, int batch_size,
                int &scroll_pos, int panel_width, bool full_info = true ) {
            if( lang_version == detail::get_current_language_version()
                && rec == last_recipe && rec != nullptr
                && panel_width == last_panel_width
                && batch_size == cached_batch_size
                && full_info == cached_full_info ) {
                return info;
            }
            lang_version = detail::get_current_language_version();
            last_recipe = rec;
            last_panel_width = panel_width;
            cached_batch_size = batch_size;
            cached_full_info = full_info;
            scroll_pos = 0;
            info = recipe_result_info( *rec, crafter, batch_size, panel_width, full_info );
            return info;
        }
        item_info_data get_result_data( const recipe *rec, int batch_size,
                                        int &scroll_pos, int panel_width,
                                        bool full_info = true ) {
            const std::vector<iteminfo> &result = get_result_info( rec, batch_size, scroll_pos,
                                                  panel_width, full_info );
            return item_info_data( "", "", result, {}, scroll_pos );
        }
};
} // namespace

std::pair<std::vector<const recipe *>, bool> recipes_from_cat( const recipe_subset
        &available_recipes, const crafting_category_id &cat, const std::string &subcat )
{
    if( subcat == "CSC_*_FAVORITE" ) {
        return std::make_pair( available_recipes.favorite(), false );
    } else if( subcat == "CSC_*_RECENT" ) {
        return std::make_pair( available_recipes.recent(), false );
    } else if( subcat == "CSC_*_HIDDEN" ) {
        return std::make_pair( available_recipes.hidden(), true );
    } else {
        return std::make_pair( available_recipes.in_category( cat, subcat != "CSC_ALL" ? subcat : "" ),
                               false );
    }
}


static void nested_toggle( recipe_id rec, bool &recalc, bool &keepline )
{
    auto loc = uistate.expanded_recipes.find( rec );
    if( loc != uistate.expanded_recipes.end() ) {
        uistate.expanded_recipes.erase( rec );
    } else {
        uistate.expanded_recipes.insert( rec );
    }
    recalc = true;
    keepline = true;
}

static bool selection_ok( const std::vector<const recipe *> &list, const int current_line,
                          const bool nested_acceptable )
{
    if( list.empty() || current_line < 0
        || current_line >= static_cast<int>( list.size() ) ) {
        popup( _( "Nothing selected!" ) );
    } else if( list[current_line]->is_nested() && !nested_acceptable ) {
        popup( _( "Select a recipe within this group" ) );
    } else {
        return true;
    }
    return false;
}

// returns false if the popup was cancelled
static bool filter_crafting_recipes( std::string &filterstring )
{
    int max_example_length = 0;
    for( const SearchPrefix &prefix : prefixes ) {
        max_example_length = std::max( max_example_length, utf8_width( prefix.example.translated() ) );
    }
    std::string spaces( max_example_length, ' ' );

    std::string description = filter_help_start.translated();

    {
        std::string example_name = _( "shirt" );
        int padding = max_example_length - utf8_width( example_name );
        description += string_format(
                           _( "  <color_white>%s</color>%.*s    %s\n" ),
                           example_name, padding, spaces,
                           _( "<color_cyan>name</color> of resulting item" ) );

        std::string example_exclude = _( "clean" );
        padding = max_example_length - utf8_width( example_exclude );
        description += string_format(
                           _( "  <color_yellow>-</color><color_white>%s</color>%.*s   %s\n" ),
                           example_exclude, padding, spaces,
                           _( "<color_cyan>names</color> to exclude" ) );
    }

    for( const SearchPrefix &prefix : prefixes ) {
        int padding = max_example_length - utf8_width( prefix.example.translated() );
        description += string_format(
                           _( "  <color_yellow>%c</color><color_white>:%s</color>%.*s  %s\n" ),
                           prefix.key, prefix.example, padding, spaces, prefix.description );
    }

    description +=
        _( "\nUse <color_red>up/down arrow</color> to go through your search history." );

    string_input_popup_imgui popup( 85, filterstring );
    popup.set_label( _( "Search:" ) );
    popup.set_description( description, c_light_gray, /*monofont=*/ true );
    popup.set_identifier( "craft_recipe_filter" );
    popup.use_uilist_history( false );
    filterstring = popup.query();

    return !popup.cancelled();
}

// ---------------------------------------------------------------------------
// ImGui crafting UI implementation
// ---------------------------------------------------------------------------

namespace
{
class crafting_ui_impl : public cataimgui::window
{
    public:
        crafting_ui_impl( Character *crafter, const recipe_id &goto_recipe,
                          std::string filterstring, bool camp_crafting,
                          temp_crafting_inventory *inventory_override );

        void process_action( const std::string &action_in, input_context &ctxt );
        void set_input_context( const input_context *ctxt ) {
            ctxt_ptr = ctxt;
        }

        bool is_done() const {
            return done;
        }
        bool is_info_nav() const {
            return info_nav_active;
        }
        Character *get_crafter() const {
            return crafter;
        }
        const recipe *get_chosen() const {
            return chosen;
        }
        int get_batch_size() const {
            return manual_batch;
        }

    protected:
        void draw_controls() override;
        cataimgui::bounds get_bounds() override;

    private:
        // --- Core state ---
        Character *crafter;
        const recipe *chosen = nullptr;
        bool done = false;
        bool camp_crafting;
        temp_crafting_inventory *inventory_override;
        const recipe_subset *available_recipes;
        std::vector<Character *> crafting_group;
        int crafter_i;

        // --- Recipe list ---
        std::vector<const recipe *> current;
        std::vector<int> indent_vec;
        std::vector<availability> available;
        int line = 0;
        int num_recipe = 0;
        size_t num_hidden = 0;

        // --- Tab state ---
        std::vector<std::string> crafting_cats;
        tab_list tab;
        tab_list subtab;
        std::string filterstring;

        // --- Mode flags ---
        bool recalc = true;
        bool keepline = false;
        bool batch = false;
        int batch_line = 0;
        int manual_batch = 1;
        bool show_hidden = false;
        bool is_wide = false;

        // --- Scroll state ---
        int line_item_info_popup = 0;
        int embedded_item_scroll_dummy = 0;
        cataimgui::scroll recipe_info_scroll = cataimgui::scroll::none;
        cataimgui::scroll item_info_scroll = cataimgui::scroll::none;

        // --- Pending UI intents ---
        int pending_tab_index = -1;
        int pending_subtab_index = -1;
        int pending_line_click = -1;
        int pending_batch_delta = 0;    // +1/-1 from inline batch buttons
        bool pending_enter_batch = false;
        bool pending_exit_batch = false;
        bool need_scroll_to_selected = false;
        bool need_scroll_to_selected_recipe_details = false;

        // One-frame flags: force ImGui to select a specific tab, then clear.
        // Set on programmatic tab changes (PREV_TAB/NEXT_TAB/LEFT/RIGHT).
        bool force_select_tab = true;   // true initially to sync ImGui with tab state
        bool force_select_subtab = true;

        // --- Input context ---
        const input_context *ctxt_ptr = nullptr;

        // --- Keybinding footer ---
        std::string keybinding_tips;

        // --- Unread tracking ---
        bool highlight_unread;
        bool unread_recipes_first = false;
        bool recalc_unread;
        bool user_moved_line = false;
        int last_line = -1;
        bool just_toggled_unread = false;
        std::map<std::string, bool> is_cat_unread;
        std::map<std::string, std::map<std::string, bool>> is_subcat_unread;
        bool is_filtered_unread = false;

        // --- Caches ---
        std::map<character_id, std::map<const recipe *, availability>> guy_availability_cache;
        std::map<const recipe *, availability> *availability_cache;
        std::unique_ptr<recipe_result_info_cache> result_info;

        // --- Tool group expansion state ---
        std::set<int> expanded_tool_groups;
        std::set<int> expanded_comp_groups;

        // What the info panel shows for the selection, built once per selection
        recipe_panel_cache panel_cache;

        // Cached because get_byproducts() randomizes via item_group each call
        const recipe *cached_byproduct_recipe = nullptr;
        std::map<itype_id, int> cached_byproducts;

        // --- Info panel keyboard navigation ---
        bool info_nav_active = false;
        int info_nav_cursor = 0;
        int info_nav_count = 0;    // set during draw, read during process_action
        int info_nav_activated = -1; // set by process_action, consumed by draw


        // --- Rendering helpers ---
        void draw_category_tabs();
        void draw_subcategory_tabs();
        void draw_recipe_list();
        void draw_recipe_info_panel();
        bool nav_clickable( const char *label, nc_color col );
        void draw_modifier_table( const recipe &recp, const recipe_panel &panel,
                                  int batch_size ) const;
        void draw_requirement_tools( const recipe_panel_requirement &tools );
        void draw_components( const recipe_panel_requirement &components );
        void draw_character_resources( const recipe_panel &panel ) const;
        void draw_item_info_panel();
        void draw_status_header();
        void draw_hidden_count();
        void draw_keybinding_footer();
        void rebuild_keybinding_tips();

        // --- State management ---
        void recalculate_recipes();
        void recalculate_unread();
        void invalidate_info_panels();
};
} // namespace

// --- Constructor ---

crafting_ui_impl::crafting_ui_impl( Character *crafter, const recipe_id &goto_recipe,
                                    std::string filterstring, bool camp_crafting,
                                    temp_crafting_inventory *inventory_override )
    : cataimgui::window( _( "Crafting" ),
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav ),
      crafter( crafter ),
      camp_crafting( camp_crafting ),
      inventory_override( inventory_override ),
      tab( crafting_cats ),
      subtab( crafting_cats ),
      filterstring( std::move( filterstring ) ),
      highlight_unread( get_option<bool>( "HIGHLIGHT_UNREAD_RECIPES" ) ),
      recalc_unread( highlight_unread )
{
    crafting_group = crafter->get_crafting_group();
    crafter_i = std::find( crafting_group.begin(), crafting_group.end(),
                           crafter ) - crafting_group.begin();

    available_recipes = &crafter->get_group_available_recipes( inventory_override );
    availability_cache = &guy_availability_cache[crafter->getID()];

    result_info = std::make_unique<recipe_result_info_cache>( *crafter );

    // Build category list (non-hidden categories that have known recipes)
    crafting_cats.reserve( craft_cat_list.size() );
    for( const crafting_category &cat : craft_cat_list.get_all() ) {
        if( !cat.is_hidden && !available_recipes->in_category( cat.id ).empty() ) {
            crafting_cats.emplace_back( cat.id.str() );
        }
    }
    tab = tab_list( crafting_cats );
    subtab = tab_list( crafting_category_id( tab.cur() )->subcategories );

    if( goto_recipe.is_valid() ) {
        const std::vector<const recipe *> &gotocat =
            available_recipes->in_category( goto_recipe->category );
        if( !gotocat.empty() ) {
            const auto gotorec = std::find_if( gotocat.begin(), gotocat.end(),
            [&goto_recipe]( const recipe * r ) {
                return r && r->ident() == goto_recipe;
            } );
            if( gotorec != gotocat.end() && ( *gotorec )->category.is_valid() ) {
                while( tab.cur() != goto_recipe->category.str() ) {
                    tab.next();
                }
                subtab = tab_list( crafting_category_id( tab.cur() )->subcategories );
                chosen = *gotorec;
                show_hidden = true;
                keepline = true;
                current = gotocat;
                line = gotorec - gotocat.begin();
            }
        }
    }
}

// --- get_bounds ---

cataimgui::bounds crafting_ui_impl::get_bounds()
{
    ImVec2 viewport = ImGui::GetMainViewport()->Size;
    float char_w = ImGui::CalcTextSize( "X" ).x;
    float full_screen_w = 80.f * char_w;

    float free_w = viewport.x - full_screen_w;
    bool wide = viewport.x > full_screen_w && free_w > 15.f * char_w;
    float width;
    if( wide ) {
        width = ( free_w > full_screen_w ) ? full_screen_w * 2.f : viewport.x;
    } else {
        width = full_screen_w;
    }

    float x = ( viewport.x - width ) / 2.f;
    return { x, 0.f, width, viewport.y };
}

// --- draw_controls ---

void crafting_ui_impl::draw_controls()
{
    hide_if_hidden();
    // Debug: set to true to show borders around all child regions and table cells
    static constexpr bool debug_layout = false;
    if( debug_layout ) {
        ImGui::PushStyleVar( ImGuiStyleVar_ChildBorderSize, 1.0f );
    }

    if( recalc_unread ) {
        recalculate_unread();
    }

    // Wide/narrow decision
    float char_w = ImGui::CalcTextSize( "X" ).x;
    float full_screen_width = 80.f * char_w;
    float window_width = ImGui::GetWindowWidth();
    float free_width = window_width - full_screen_width;
    is_wide = ( window_width > full_screen_width && free_width > 15.f * char_w );

    if( is_bounds_changed() || keybinding_tips.empty() ) {
        rebuild_keybinding_tips();
    }

    // Reserve a fixed height for the footer (separator + ~3 lines of tips)
    float line_height = ImGui::GetTextLineHeightWithSpacing();
    float footer_height = line_height * 4.f;

    // Main content area (above footer)
    ImGuiChildFlags content_flags = debug_layout ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    if( ImGui::BeginChild( "##CRAFT_CONTENT", ImVec2( 0, -footer_height ), content_flags ) ) {
        // Section 1: split header (category tabs left, status right)
        ImGuiTableFlags header_flags = debug_layout
                                       ? ImGuiTableFlags_Borders : ImGuiTableFlags_None;
        if( ImGui::BeginTable( "##HEADER_TOP", 2, header_flags ) ) {
            int window_chars = static_cast<int>( window_width / char_w );
            int status_chars = window_chars / 4;
            float status_width = static_cast<float>( status_chars ) * char_w;

            ImGui::TableSetupColumn( "tabs", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "status", ImGuiTableColumnFlags_WidthFixed, status_width );

            ImGui::TableNextColumn();
            draw_category_tabs();
            // Apply pending category change immediately so subcategory
            // tabs render with the correct list on the same frame
            if( pending_tab_index >= 0 ) {
                tab.set_index( pending_tab_index );
                subtab = tab_list( crafting_category_id( tab.cur() )->subcategories );
                recalc = true;
                pending_tab_index = -1;
                force_select_subtab = true;
            }
            draw_subcategory_tabs();
            // Apply pending subcategory change immediately
            if( pending_subtab_index >= 0 ) {
                subtab.set_index( pending_subtab_index );
                recalc = true;
                pending_subtab_index = -1;
            }

            ImGui::TableNextColumn();
            draw_status_header();

            ImGui::EndTable();
        }

        // Section 3: main content table (recipe list + info panels)
        float list_width = 28.f * char_w;
        float item_info_width = is_wide ? std::min( 40.f * char_w,
                                ( window_width - full_screen_width ) ) : 0.f;

        int num_columns = is_wide ? 3 : 2;
        ImGuiTableFlags main_flags = debug_layout
                                     ? ImGuiTableFlags_Borders : ImGuiTableFlags_None;
        if( ImGui::BeginTable( "##MAIN", num_columns, main_flags,
                               ImGui::GetContentRegionAvail() ) ) {
            ImGui::TableSetupColumn( "list", ImGuiTableColumnFlags_WidthFixed, list_width );
            ImGui::TableSetupColumn( "info", ImGuiTableColumnFlags_WidthStretch );
            if( is_wide ) {
                ImGui::TableSetupColumn( "item", ImGuiTableColumnFlags_WidthFixed, item_info_width );
            }
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            draw_recipe_list();

            ImGui::TableNextColumn();
            draw_recipe_info_panel();
            if( info_nav_active && info_nav_cursor >= info_nav_count ) {
                info_nav_cursor = std::max( 0, info_nav_count - 1 );
            }

            if( is_wide ) {
                ImGui::TableNextColumn();
                draw_item_info_panel();
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    // Footer
    draw_keybinding_footer();

    if( debug_layout ) {
        ImGui::PopStyleVar();
    }
}

// --- draw_category_tabs ---

void crafting_ui_impl::draw_category_tabs()
{
    if( batch ) {
        if( ImGui::BeginTabBar( "##BATCH_TAB" ) ) {
            if( cataimgui::BeginTabItem( _( "Batch" ), true ) ) {
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        return;
    }
    if( !filterstring.empty() ) {
        if( ImGui::BeginTabBar( "##FILTER_TAB" ) ) {
            std::string label = _( "Searched" );
            if( is_filtered_unread ) {
                label += " +";
            }
            if( cataimgui::BeginTabItem( label.c_str(), true ) ) {
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        return;
    }

    if( ImGui::BeginTabBar( "##CRAFT_CATS",
                            ImGuiTabBarFlags_FittingPolicyScroll ) ) {
        for( size_t i = 0; i < crafting_cats.size(); ++i ) {
            std::string label = _( get_cat_unprefixed( crafting_cats[i] ) );
            if( is_cat_unread[crafting_cats[i]] ) {
                label += " +";
            }
            // Stable tab ID so adding/removing " +" doesn't change identity
            label += "###cat_" + std::to_string( i );
            // Only force-select for one frame after programmatic tab change
            bool should_select = force_select_tab &&
                                 ( tab.cur_index() == static_cast<int>( i ) );
            if( cataimgui::BeginTabItem( label.c_str(), should_select ) ) {
                if( tab.cur_index() != static_cast<int>( i ) ) {
                    pending_tab_index = static_cast<int>( i );
                }
                ImGui::EndTabItem();
            }
        }
        force_select_tab = false;
        ImGui::EndTabBar();
    }
}

// --- draw_subcategory_tabs ---

void crafting_ui_impl::draw_subcategory_tabs()
{
    if( batch || !filterstring.empty() ) {
        return;
    }

    const crafting_category_id current_cat( tab.cur() );
    const auto &subcats = current_cat->subcategories;

    if( ImGui::BeginTabBar( "##CRAFT_SUBCATS",
                            ImGuiTabBarFlags_FittingPolicyScroll ) ) {
        for( size_t i = 0; i < subcats.size(); ++i ) {
            std::string label = _( get_subcat_unprefixed( tab.cur(), subcats[i] ) );
            bool is_empty = available_recipes->empty_category(
                                current_cat, subcats[i] != "CSC_ALL" ? subcats[i] : "" );

            if( is_subcat_unread[tab.cur()][subcats[i]] ) {
                label += " +";
            }
            // Stable tab ID so adding/removing " +" doesn't change identity
            label += "###subcat_" + std::to_string( i );

            if( is_empty ) {
                ImGui::BeginDisabled();
            }
            bool should_select = force_select_subtab &&
                                 ( subtab.cur_index() == static_cast<int>( i ) );
            if( cataimgui::BeginTabItem( label.c_str(), should_select ) ) {
                if( subtab.cur_index() != static_cast<int>( i ) ) {
                    pending_subtab_index = static_cast<int>( i );
                }
                ImGui::EndTabItem();
            }
            if( is_empty ) {
                ImGui::EndDisabled();
            }
        }
        force_select_subtab = false;
        ImGui::EndTabBar();
    }
}

// --- draw_recipe_list ---

void crafting_ui_impl::draw_recipe_list()
{
    if( current.empty() ) {
        return;
    }
    float avail_width = ImGui::GetContentRegionAvail().x;

    // Recipes / Batch toggle buttons
    {
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float btn_w = ( avail_width - spacing ) / 2.f;
        bool can_batch = !current.empty() && line >= 0 &&
                         line < static_cast<int>( current.size() ) &&
                         !current[line]->is_nested();

        if( !batch ) {
            ImGui::BeginDisabled();
            ImGui::Button( _( "Recipes" ), ImVec2( btn_w, 0 ) );
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled( !can_batch );
            if( ImGui::Button( _( "Batch" ), ImVec2( btn_w, 0 ) ) && can_batch ) {
                pending_enter_batch = true;
            }
            ImGui::EndDisabled();
        } else {
            if( ImGui::Button( _( "Recipes" ), ImVec2( btn_w, 0 ) ) ) {
                pending_exit_batch = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled();
            ImGui::Button( _( "Batch" ), ImVec2( btn_w, 0 ) );
            ImGui::EndDisabled();
        }
    }

    if( ImGui::BeginChild( "##RECIPES", ImVec2( avail_width,
                           ImGui::GetContentRegionAvail().y ),
                           ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoNav ) ) {
        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int>( current.size() ) );
        if( need_scroll_to_selected && line >= 0
            && line < static_cast<int>( current.size() ) ) {
            clipper.IncludeItemByIndex( line );
        }
        while( clipper.Step() ) {
            for( int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i ) {
                ImGui::PushID( i );

                float indent_px = static_cast<float>( indent_vec[i] ) *
                                  ImGui::CalcTextSize( " " ).x;
                if( indent_px > 0 ) {
                    ImGui::Indent( indent_px );
                }

                std::string name = current[i]->result_name( true );
                if( batch ) {
                    name = string_format( _( "%2dx %s" ), i + 1, name );
                }

                nc_color col = ( i == line )
                               ? available[i].selected_color()
                               : available[i].color();

                ImGui::PushStyleColor( ImGuiCol_Text,
                                       cataimgui::imvec4_from_color( col ) );
                ImGui::Selectable( name.c_str(), i == line );
                ImGui::PopStyleColor();

                if( ImGui::IsItemClicked() ) {
                    pending_line_click = i;
                }
                if( i == line && need_scroll_to_selected ) {
                    ImGui::SetScrollHereY();
                    need_scroll_to_selected = false;
                }

                if( highlight_unread &&
                    !uistate.read_recipes.count( current[i]->ident() ) ) {
                    const char *new_label = pgettext( "crafting gui", "NEW!" );
                    float label_w = ImGui::CalcTextSize( new_label ).x;
                    ImGui::SameLine( avail_width - label_w -
                                     ImGui::GetStyle().ItemSpacing.x );
                    ImGui::TextColored(
                        cataimgui::imvec4_from_color( c_light_green ),
                        "%s", new_label );
                }

                if( indent_px > 0 ) {
                    ImGui::Unindent( indent_px );
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

// --- draw_recipe_info_panel ---

// Wrapper: register a clickable_text with the info panel nav system.
// Returns true when activated (mouse or keyboard).
bool crafting_ui_impl::nav_clickable( const char *label, nc_color col )
{
    int idx = info_nav_active ? info_nav_count : -1;
    info_nav_count++;
    if( need_scroll_to_selected_recipe_details && info_nav_cursor == idx ) {
        ImGui::SetScrollHereY();
        need_scroll_to_selected_recipe_details = false;
    }
    return clickable_text( label, col, idx,
                           info_nav_active ? info_nav_cursor : -1,
                           info_nav_active ? info_nav_activated : -1 );
}

void crafting_ui_impl::draw_recipe_info_panel()
{
    if( current.empty() ) {
        return;
    }
    info_nav_count = 0;
    // info_nav_activated persists from process_action until consumed this frame
    const recipe &recp = *current[line];
    const availability &avail = available[line];
    // Batch size for all subsequent calculations
    const int batch_size = get_batch_size();
    const recipe_panel &panel = panel_cache.get( recp, avail, batch_size, {
        *crafter, crafting_group, *available_recipes, *availability_cache, camp_crafting,
        inventory_override
    } );

    if( ImGui::BeginChild( "##RECIPE_INFO", ImGui::GetContentRegionAvail(), false,
                           ImGuiWindowFlags_NoNav ) ) {
        cataimgui::set_scroll( recipe_info_scroll );

        // --- Summary ---

        // Line 1: recipe name with inline batch controls [-] name [xN] [+]
        {
            const std::string &recipe_name = panel.result_name;
            cataimgui::PushGuiFont1_5x();
            float region_w = ImGui::GetContentRegionAvail().x;
            float space = ImGui::CalcTextSize( " " ).x;

            bool show_batch_ui = !recp.is_nested();
            bool show_minus = show_batch_ui && manual_batch > 1;
            bool show_plus = show_batch_ui && manual_batch < 50;
            std::string mult_str;
            if( show_batch_ui && manual_batch > 1 ) {
                mult_str = string_format( " x%d", manual_batch );
            }

            // Measure total width for centering
            float total_w = ImGui::CalcTextSize( recipe_name.c_str() ).x;
            if( show_minus ) {
                total_w += ImGui::CalcTextSize( "-" ).x + space;
            }
            if( !mult_str.empty() ) {
                total_w += ImGui::CalcTextSize( mult_str.c_str() ).x;
            }
            if( show_plus ) {
                total_w += space + ImGui::CalcTextSize( "+" ).x;
            }
            if( total_w < region_w ) {
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( region_w - total_w ) / 2.f );
            }

            if( show_minus ) {
                if( clickable_text( "-", c_dark_gray ) ) {
                    pending_batch_delta = -1;
                }
                ImGui::SameLine( 0, space );
            }

            ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                                recipe_name.c_str() );

            if( !mult_str.empty() ) {
                ImGui::SameLine( 0, 0 );
                ImGui::TextColored( cataimgui::imvec4_from_color( c_cyan ), "%s",
                                    mult_str.c_str() );
            }

            if( show_plus ) {
                ImGui::SameLine( 0, space );
                if( clickable_text( "+", c_dark_gray ) ) {
                    pending_batch_delta = 1;
                }
            }

            cataimgui::PopGuiFont1_5x();
        }

        float region_w = ImGui::GetContentRegionAvail().x;

        {
            // Line 2: centered source attribution
            if( !panel.source_text.empty() ) {
                const std::string &source_text = panel.source_text;
                float text_w = ImGui::CalcTextSize( source_text.c_str() ).x;
                if( text_w < region_w ) {
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( region_w - text_w ) / 2.f );
                }
                cataimgui::draw_colored_text( source_text, c_dark_gray, region_w );
            }

            // Item flavor text: capped at 70% width, centered block.
            // Short text (fits in one line) is centered; long text wraps
            // left-aligned within the centered cap.
            if( !panel.description.empty() ) {
                const std::string &desc = panel.description;
                float cap_w = region_w * 0.7f;
                float desc_w = ImGui::CalcTextSize( desc.c_str() ).x;
                if( desc_w <= cap_w ) {
                    // Fits in one line: center it
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( region_w - desc_w ) / 2.f );
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_light_gray ), "%s",
                                        desc.c_str() );
                } else {
                    // Wraps: center the cap block, left-align text inside
                    float offset = ( region_w - cap_w ) / 2.f;
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + offset );
                    ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + cap_w );
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_light_gray ), "%s",
                                        desc.c_str() );
                    ImGui::PopTextWrapPos();
                }
            }

            // Line 3: centered natural-language stats
            if( !recp.is_nested() ) {
                float line_w = ImGui::CalcTextSize( panel.stats_plain.c_str() ).x;
                if( line_w < region_w ) {
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( region_w - line_w ) / 2.f );
                }
                cataimgui::draw_colored_text( panel.stats_colored, c_light_gray, region_w );
            }
        }

        // Line 4: lighting warning, if applicable
        if( !recp.is_nested() && panel.too_dark ) {
            const std::string lighting_str = _( "Lighting conditions are too poor to craft this." );
            float line_w = ImGui::CalcTextSize( lighting_str.c_str() ).x;
            if( line_w < region_w ) {
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( region_w - line_w ) / 2.f );
            }
            cataimgui::draw_colored_text( lighting_str, c_red, region_w );
        }
        ImGui::Separator();

        // Warnings (right after stats)
        if( avail.can_craft_recipe && avail.would_use_rotten ) {
            cataimgui::TextColoredParagraphNewline( c_red, _( "Will use rotten ingredients" ) );
        }
        if( avail.can_craft_recipe && avail.would_use_favorite ) {
            cataimgui::TextColoredParagraphNewline( c_red, _( "Will use favorited ingredients" ) );
        }
        if( !avail.can_craft_recipe && !avail.has_proficiencies ) {
            cataimgui::TextColoredParagraph( c_red,
                                             _( "Missing required proficiencies: " ) );
            const std::vector<std::string> &profs = panel.required_proficiencies;
            for( const std::string &prof : profs ) {
                cataimgui::TextColoredParagraph( c_red, prof );
                if( ( profs.size() > 1 ) && ( &prof != &profs.back() ) ) {
                    cataimgui::TextColoredParagraph( c_red, _( ", " ) );
                }
            }
            cataimgui::TextColoredParagraphNewline( c_red, "" );
        }
        if( !avail.crafter_has_primary_skill ) {
            cataimgui::TextColoredParagraphNewline( c_red,
                                                    recp.is_practice()
                                                    ? _( "Crafter lacks theoretical knowledge to practice this." )
                                                    : _( "Crafter lacks theoretical knowledge for this recipe." ) );
        }

        // Complex/overlapping requirement warnings
        if( !recp.is_nested() ) {
            if( avail.can_craft_recipe && panel.too_complex ) {
                cataimgui::TextColoredParagraphNewline( c_yellow,
                                                        _( "Due to the complex overlapping requirements, this recipe "
                                                                "may appear to be craftable when it is not." ) );
            }
            const bool npc_cant = panel.npc_cannot_craft;
            if( !avail.can_craft_recipe && avail.apparently_craftable
                && !npc_cant ) {
                cataimgui::TextColoredParagraphNewline( c_red,
                                                        _( "Cannot be crafted because the same item is needed "
                                                                "for multiple components." ) );
            }
            if( !avail.can_craft_recipe && npc_cant ) {
                cataimgui::TextColoredParagraphNewline( c_red, panel.npc_reason );
            }
            if( !avail.can_craft_recipe && !panel.condition_failure.empty() ) {
                cataimgui::TextColoredParagraphNewline( c_red, panel.condition_failure );
            }
        }

        // Batch time savings
        if( batch_size > 1 ) {
            const std::string &savings = panel.batch_savings;
            if( !savings.empty() ) {
                cataimgui::draw_colored_text(
                    string_format( _( "Batch time savings: %s" ),
                                   colorize( savings, c_cyan ) ), c_light_gray, region_w );
            }
        }

        // Centered "Details" button that opens the modifier table
        if( !recp.is_nested() ) {
            // Practice recipes always show details
            bool show_details = uistate.crafting_expand_details || recp.is_practice();
            if( !recp.is_practice() ) {
                const char *details_label = uistate.crafting_expand_details ? _( "- details -" ) :
                                            _( "+ details +" );
                float btn_w = ImGui::CalcTextSize( details_label ).x;
                float rgn_w = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( rgn_w - btn_w ) / 2.f );
                if( nav_clickable( details_label, c_cyan ) ) {
                    uistate.crafting_expand_details = !uistate.crafting_expand_details;
                }
            }
            if( show_details ) {
                draw_modifier_table( recp, panel, batch_size );

                // Practice-specific info after the modifier table
                if( recp.is_practice() && recp.practice_data ) {
                    ImGui::NewLine();
                    if( recp.skill_used ) {
                        if( !panel.practice_requirement.empty() ) {
                            ImGui::TextColored( cataimgui::imvec4_from_color( c_red ), "%s",
                                                panel.practice_requirement.c_str() );
                        }
                        ImGui::TextColored( cataimgui::imvec4_from_color( panel.practice_gains_color ),
                                            "%s", panel.practice_gains.c_str() );

                        // Proficiency gains
                        for( const std::string &trains : panel.practice_trains ) {
                            ImGui::TextColored( cataimgui::imvec4_from_color( c_green ), "%s",
                                                trains.c_str() );
                        }
                    }
                }
            }
        }

        // --- Recipe ---
        const bool has_recipe_content = !recp.is_nested() && (
                                            recp.has_character_requirements() ||
                                            !recp.simple_requirements().is_empty() ||
                                            recp.has_steps() ||
                                            !recp.get_character_resources().empty()
                                        );
        if( has_recipe_content ) {
            ImGui::NewLine();
            {
                const char *recipe_hdr = _( "Recipe" );
                float hdr_w = ImGui::CalcTextSize( recipe_hdr ).x;
                float rgn_w = ImGui::GetContentRegionAvail().x;
                if( hdr_w < rgn_w ) {
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( rgn_w - hdr_w ) / 2.f );
                }
                ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", recipe_hdr );
            }
            ImGui::Separator();

            // Single step recipes
            if( recp.has_steps() && ( recp.steps().size() <= 1 ) ) {
                ImGui::TextColored( cataimgui::imvec4_from_color( c_white ),
                                    "%s", panel.single_step_name.c_str() );
                ImGui::NewLine();
            }

            if( recp.has_character_requirements() ) {
                //~ Header shown above a recipe's required primary character stats.
                ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", _( "Character requirements:" ) );
                for( const recipe_panel_text &requirement : panel.character_requirements ) {
                    ImGui::TextColored( cataimgui::imvec4_from_color( requirement.color ), "•  %s",
                                        requirement.text.c_str() );
                }
                ImGui::Spacing();
            }

            // Components (always recipe-level)
            draw_components( panel.components );

            // Byproducts
            if( recp.has_byproducts() ) {
                if( cached_byproduct_recipe != &recp ) {
                    cached_byproduct_recipe = &recp;
                    cached_byproducts = recp.get_byproducts();
                }
                ImGui::Spacing();
                ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                                    _( "Byproducts:" ) );
                for( const auto &[bp_id, bp_count] : cached_byproducts ) {
                    const int bp_c = bp_count * batch_size;
                    ImGui::BulletText( "%s", string_format( "%s x%d",
                                                            item::nname( bp_id, bp_c ), bp_c ).c_str() );
                }
            }

            // Multi step details
            if( recp.has_steps() && ( recp.steps().size() > 1 ) ) {
                // Step details are collapsible; collapsed shows flat merged view
                {
                    const char *steps_label = uistate.crafting_expand_steps
                                              ? _( "- steps -" ) : _( "+ steps +" );
                    float btn_w = ImGui::CalcTextSize( steps_label ).x;
                    float rgn_w = ImGui::GetContentRegionAvail().x;
                    if( btn_w < rgn_w ) {
                        ImGui::SetCursorPosX( ImGui::GetCursorPosX() +
                                              ( rgn_w - btn_w ) / 2.f );
                    }
                    if( nav_clickable( steps_label, c_cyan ) ) {
                        uistate.crafting_expand_steps = !uistate.crafting_expand_steps;
                    }
                }

                if( uistate.crafting_expand_steps ) {
                    float step_indent = ImGui::CalcTextSize( "    " ).x;
                    float sub_indent = ImGui::CalcTextSize( "  " ).x;
                    for( size_t si = 0; si < panel.steps.size(); ++si ) {
                        const recipe_panel_step &step = panel.steps[si];

                        // Step header: "1. Name  time  activity  [batch note]"
                        ImGui::TextColored( cataimgui::imvec4_from_color( c_white ),
                                            "%d. %s", static_cast<int>( si + 1 ),
                                            step.name.c_str() );
                        ImGui::SameLine( 0, ImGui::CalcTextSize( "  " ).x );
                        ImGui::TextColored( cataimgui::imvec4_from_color( c_cyan ), "%s",
                                            step.time.c_str() );
                        if( !step.batch_note.empty() ) {
                            ImGui::SameLine( 0, 0 );
                            ImGui::TextColored( cataimgui::imvec4_from_color( c_light_green ), "%s",
                                                step.batch_note.c_str() );
                        }
                        ImGui::SameLine( 0, ImGui::CalcTextSize( "  " ).x );
                        ImGui::TextColored( cataimgui::imvec4_from_color( step.activity_color ), "%s",
                                            step.activity.c_str() );
                        if( step.unattended ) {
                            ImGui::SameLine( 0, ImGui::CalcTextSize( "  " ).x );
                            ImGui::TextColored( cataimgui::imvec4_from_color( c_yellow ),
                                                "%s", _( "[unattended]" ) );
                        }

                        ImGui::Indent( step_indent );

                        // Per-step proficiencies
                        if( !step.proficiencies.empty() ) {
                            ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                                                _( "Proficiencies:" ) );
                            ImGui::Indent( sub_indent );
                            for( const recipe_panel_text &prof : step.proficiencies ) {
                                ImGui::TextColored( cataimgui::imvec4_from_color( prof.color ), "%s",
                                                    prof.text.c_str() );
                            }
                            ImGui::Unindent( sub_indent );
                        }

                        // Per-step tools + qualities
                        draw_requirement_tools( step.tools );

                        ImGui::Unindent( step_indent );
                    }
                } else {
                    // Collapsed: flat merged tools view
                    draw_requirement_tools( panel.tools );
                }
            } else {
                // Legacy: flat tools + qualities
                draw_requirement_tools( panel.tools );
            }

            draw_character_resources( panel );

            // Helpers who know this recipe
            if( !panel.known_by.empty() ) {
                cataimgui::TextColoredParagraph( c_white, panel.known_by );
                ImGui::NewLine();
            }
        }

        // Nested recipe groups: clickable sub-recipe list in middle column
        if( recp.is_nested() ) {
            for( const recipe_panel_text &child : panel.nested ) {
                ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "  • " );
                ImGui::SameLine( 0, 0 );
                if( nav_clickable( child.text.c_str(), child.color ) ) {
                    filterstring = child.text;
                    recalc = true;
                }
            }
        }
    }
    ImGui::EndChild();
    info_nav_activated = -1;
}

// --- draw_modifier_table ---

void crafting_ui_impl::draw_modifier_table( const recipe &recp, const recipe_panel &panel,
        int batch_size ) const
{
    // Helper: time impact as percentage. Input is a time multiplier
    // (>1 = slower, <1 = faster). Shows "+50%" (red) or "-20%" (green).
    const auto time_pct_cell = [&]( float time_mult ) {
        float pct = ( time_mult - 1.0f ) * 100.f;
        ImGui::TableNextColumn();
        if( std::abs( pct ) < 0.5f ) {
            ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), "-" );
        } else {
            nc_color col = pct > 0.5f ? c_red : c_green;
            ImGui::TextColored( cataimgui::imvec4_from_color( col ), "%+.0f%%", pct );
        }
    };
    // Helper: effective skill modifier cell
    const auto skill_mod_cell = [&]( float val ) {
        ImGui::TableNextColumn();
        if( std::abs( val ) < 0.01f ) {
            ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), "-" );
        } else {
            nc_color col = val > 0.01f ? c_green : c_red;
            ImGui::TextColored( cataimgui::imvec4_from_color( col ), "%+.1f", val );
        }
    };
    const auto dash_cell = [&]() {
        ImGui::TableNextColumn();
        ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), "-" );
    };
    // Section label row: dimmed text in Source, empty other columns
    const auto section_label = [&]( const char *label ) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), "%s", label );
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
    };

    float row_indent = ImGui::CalcTextSize( "  " ).x;

    if( ImGui::BeginTable( "##MODIFIERS", 3, ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_SizingFixedFit ) ) {
        ImGui::TableSetupColumn( "source", ImGuiTableColumnFlags_WidthFixed );
        ImGui::TableSetupColumn( "time", ImGuiTableColumnFlags_WidthFixed );
        ImGui::TableSetupColumn( "eff_skill", ImGuiTableColumnFlags_WidthFixed );
        // Manual header row
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", _( "Source" ) );
        ImGui::TableNextColumn();
        ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", _( "Time" ) );
        ImGui::TableNextColumn();
        // NOLINTNEXTLINE(cata-text-style)
        ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", _( "Eff. skill" ) );

        // Base row
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if( batch_size > 1 ) {
                ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                                    _( "base (per item)" ) );
            } else {
                ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                                    pgettext( "Value without modifier", "base" ) );
            }
            ImGui::TableNextColumn();
            ImGui::TextColored( cataimgui::imvec4_from_color( c_light_gray ), "%s",
                                panel.base_time.c_str() );
            dash_cell();
        }

        // --- Skills ---
        {
            if( !panel.skills.empty() ) {
                section_label( _( "Skills" ) );
            }
            for( const recipe_panel_skill &skill : panel.skills ) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent( row_indent );
                ImGui::TextColored( cataimgui::imvec4_from_color( skill.color ), "%s %d/%d",
                                    skill.name.c_str(), skill.level, skill.required );
                if( !skill.suffix.empty() ) {
                    ImGui::SameLine( 0, 0 );
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), " %s",
                                        skill.suffix.c_str() );
                }
                ImGui::Unindent( row_indent );
                dash_cell();
                skill_mod_cell( static_cast<float>( skill.level - skill.required ) );
            }
        }

        // --- Proficiencies ---
        {
            if( !panel.proficiencies.empty() ) {
                section_label( _( "Proficiencies" ) );
            }

            for( const recipe_panel_proficiency &prof : panel.proficiencies ) {
                const bool known = prof.known;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent( row_indent );

                if( prof.required ) {
                    nc_color col = known ? c_dark_gray : c_red;
                    ImGui::TextColored( cataimgui::imvec4_from_color( col ), "%s",
                                        prof.name.c_str() );
                    ImGui::Unindent( row_indent );
                    if( known ) {
                        dash_cell();
                    } else {
                        ImGui::TableNextColumn();
                        ImGui::TextColored( cataimgui::imvec4_from_color( col ), "%s",
                                            _( "required" ) );
                    }
                    dash_cell();
                } else if( known ) {
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), "%s",
                                        prof.name.c_str() );
                    ImGui::Unindent( row_indent );
                    dash_cell();
                    dash_cell();
                } else {
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                                        prof.name.c_str() );
                    ImGui::Unindent( row_indent );
                    time_pct_cell( prof.time_multiplier );
                    skill_mod_cell( -prof.skill_penalty );
                }
            }
        }

        // --- Conditions ---
        {
            const float morale_mult = panel.morale_mult;
            const float light_mult = panel.light_mult;
            const float pain_mult = panel.pain_mult;
            const float limb_mult = panel.limb_mult;
            const float mut_mult = panel.mut_mult;
            const float mut_skill_bonus = panel.mut_skill_bonus;
            const float int_delta = panel.int_delta;
            const bool has_farsight = panel.has_farsight;

            bool has_conditions =
                std::abs( morale_mult - 1.0f ) > 0.01f ||
                light_mult <= 0.0f || std::abs( light_mult - 1.0f ) > 0.01f ||
                std::abs( pain_mult - 1.0f ) > 0.01f ||
                std::abs( limb_mult - 1.0f ) > 0.01f ||
                std::abs( mut_mult - 1.0f ) > 0.01f || std::abs( mut_skill_bonus ) > 0.01f ||
                std::abs( int_delta ) > 0.01f ||
                has_farsight;

            if( has_conditions ) {
                section_label( _( "Conditions" ) );

                // Start an indented condition row in the Source column
                const auto cond_source = [&]() {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Indent( row_indent );
                };
                // End the Source column indent (call before moving to Time/Eff.skill)
                const auto cond_source_end = [&]() {
                    ImGui::Unindent( row_indent );
                };

                if( std::abs( morale_mult - 1.0f ) > 0.01f ) {
                    cond_source();
                    ImGui::Text( "%s", _( "morale" ) );
                    cond_source_end();
                    time_pct_cell( 1.0f / morale_mult );
                    dash_cell();
                }

                if( light_mult <= 0.0f ) {
                    cond_source();
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_red ), "%s",
                                        _( "lighting" ) );
                    cond_source_end();
                    ImGui::TableNextColumn();
                    ImGui::TextColored( cataimgui::imvec4_from_color( i_red ), "%s",
                                        _( "blocked" ) );
                    dash_cell();
                } else if( std::abs( light_mult - 1.0f ) > 0.01f ) {
                    cond_source();
                    ImGui::Text( "%s", _( "lighting" ) );
                    cond_source_end();
                    time_pct_cell( 1.0f / light_mult );
                    dash_cell();
                }

                if( std::abs( pain_mult - 1.0f ) > 0.01f ) {
                    cond_source();
                    ImGui::Text( "%s", _( "pain" ) );
                    cond_source_end();
                    time_pct_cell( 1.0f / pain_mult );
                    dash_cell();
                }

                if( std::abs( limb_mult - 1.0f ) > 0.01f ) {
                    cond_source();
                    ImGui::Text( "%s", _( "limbs" ) );
                    cond_source_end();
                    time_pct_cell( 1.0f / limb_mult );
                    dash_cell();
                }

                if( std::abs( mut_mult - 1.0f ) > 0.01f || std::abs( mut_skill_bonus ) > 0.01f ) {
                    cond_source();
                    ImGui::Text( "%s", _( "mutations" ) );
                    cond_source_end();
                    if( std::abs( mut_mult - 1.0f ) > 0.01f ) {
                        time_pct_cell( 1.0f / mut_mult );
                    } else {
                        dash_cell();
                    }
                    if( std::abs( mut_skill_bonus ) > 0.01f ) {
                        skill_mod_cell( mut_skill_bonus );
                    } else {
                        dash_cell();
                    }
                }

                if( std::abs( int_delta ) > 0.01f ) {
                    cond_source();
                    ImGui::Text( "%s %d", panel.crafting_stat_name.c_str(), panel.intelligence );
                    cond_source_end();
                    dash_cell();
                    skill_mod_cell( int_delta );
                }

                if( has_farsight ) {
                    cond_source();
                    ImGui::Text( "%s", _( "farsightedness" ) );
                    cond_source_end();
                    dash_cell();
                    skill_mod_cell( -panel.vision_penalty );
                }
            }
        }

        // Separator + Total
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::TableNextColumn();
        ImGui::Separator();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", _( "Total" ) );

        // Total time
        ImGui::TableNextColumn();
        ImGui::TextColored( cataimgui::imvec4_from_color( c_cyan ), "%s", panel.total_time.c_str() );

        // Total success + catastrophic failure stacked in Eff. skill cell
        {
            const float success = panel.success;
            nc_color success_col = success < 25.f ? c_red :
                                   success < 50.f ? c_yellow :
                                   success < 75.f ? c_light_gray : c_green;
            ImGui::TableNextColumn();
            ImGui::TextColored( cataimgui::imvec4_from_color( success_col ), "%.1f%% %s",
                                success, _( "success" ) );

            const float cata_fail = panel.destruction;
            if( cata_fail > 0.01f ) {
                nc_color cata_col = cata_fail > 50.f ? i_red :
                                    cata_fail > 20.f ? c_red :
                                    cata_fail > 5.f ? c_yellow : c_light_gray;
                ImGui::TextColored( cataimgui::imvec4_from_color( cata_col ), "%.1f%% %s",
                                    cata_fail, _( "destroy" ) );
            }
        }

        ImGui::EndTable();
    }

    // Darkness and nearby count live under the Details spoiler
    ImGui::TextColored( cataimgui::imvec4_from_color( c_light_gray ), "%s",
                        string_format( _( "Crafting in the dark: %s" ), panel.darkness ).c_str() );

    if( recp.result() && panel.nearby > 0 ) {
        ImGui::TextColored( cataimgui::imvec4_from_color( c_yellow ), "%s",
                            string_format( _( "%d already nearby" ), panel.nearby ).c_str() );
    }
    ImGui::Spacing();
}

// --- draw_components ---

void crafting_ui_impl::draw_components( const recipe_panel_requirement &components )
{
    if( !components.shown ) {
        return;
    }

    ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                        _( "Components:" ) );

    for( const recipe_panel_group &group : components.groups ) {
        const int gi = group.index;
        const std::vector<recipe_panel_text> &sorted_alts = group.options;

        const bool is_expanded = expanded_comp_groups.count( gi ) > 0;
        const std::string bullet = "  • ";

        if( is_expanded ) {
            ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", bullet.c_str() );
            float indent = ImGui::CalcTextSize( "    " ).x;
            for( size_t i = 0; i < sorted_alts.size(); ++i ) {
                // Draw the first item on the same line as the bullet, then indent the rest
                if( i == 0 ) {
                    ImGui::SameLine( 0, 0 );
                }
                if( i == 1 ) {
                    ImGui::Indent( indent );
                }
                ImGui::TextColored( cataimgui::imvec4_from_color( sorted_alts[i].color ), "%s",
                                    sorted_alts[i].text.c_str() );
            }
            if( nav_clickable( _( "show less" ), c_dark_gray ) ) {
                expanded_comp_groups.erase( gi );
            }
            ImGui::Unindent( indent );
        } else {
            const float avail_width = ImGui::GetContentRegionAvail().x;
            const float or_w = ImGui::CalcTextSize( _( " OR " ) ).x;
            float used_w = ImGui::CalcTextSize( bullet.c_str() ).x;

            int fits = 0;
            for( size_t i = 0; i < sorted_alts.size(); ++i ) {
                float tw = ImGui::CalcTextSize( sorted_alts[i].text.c_str() ).x;
                float sep = ( i > 0 ) ? or_w : 0.f;
                int rem = static_cast<int>( sorted_alts.size() ) - fits - 1;
                float reserve = 0.f;
                if( rem > 0 ) {
                    std::string more = string_format( _( "or %d more" ), rem );
                    reserve = or_w + ImGui::CalcTextSize( more.c_str() ).x;
                }
                if( used_w + sep + tw + reserve > avail_width && fits > 0 ) {
                    break;
                }
                used_w += sep + tw;
                fits++;
            }

            ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", bullet.c_str() );
            ImGui::SameLine( 0, 0 );

            for( int i = 0; i < fits; ++i ) {
                if( i > 0 ) {
                    ImGui::SameLine( 0, 0 );
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), "%s",
                                        _( " or " ) );
                    ImGui::SameLine( 0, 0 );
                }
                ImGui::TextColored( cataimgui::imvec4_from_color( sorted_alts[i].color ), "%s",
                                    sorted_alts[i].text.c_str() );
            }

            int remaining = static_cast<int>( sorted_alts.size() ) - fits;
            if( remaining > 0 ) {
                ImGui::SameLine( 0, 0 );
                std::string more_text = string_format( _( " or %d more" ), remaining );
                if( nav_clickable( more_text.c_str(), c_cyan ) ) {
                    expanded_comp_groups.insert( gi );
                }
            }
        }
        ImGui::Dummy( ImVec2( 0, 0 ) );
    }
}

void crafting_ui_impl::draw_character_resources( const recipe_panel &panel ) const
{
    if( !panel.resources_shown ) {
        return;
    }

    ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s", _( "Character resources:" ) );
    for( const recipe_panel_text &resource : panel.resources ) {
        ImGui::TextColored( cataimgui::imvec4_from_color( resource.color ), "%s",
                            resource.text.c_str() );
    }
}

void crafting_ui_impl::draw_requirement_tools( const recipe_panel_requirement &tools )
{
    if( !tools.shown ) {
        return;
    }

    ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                        _( "Tools:" ) );

    // Quality requirements -- bullet per group, colored by availability
    for( const std::string &qualities : tools.qualities ) {
        ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "  • " );
        ImGui::SameLine( 0, 0 );
        const float avail_width = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        cataimgui::draw_colored_text( qualities, avail_width );
        ImGui::EndGroup();
    }

    // Tool groups -- bullet per group, with named req labels and expand/collapse
    for( const recipe_panel_group &group : tools.groups ) {
        const int global_gi = group.index;
        const bool is_expanded = expanded_tool_groups.count( global_gi ) > 0;
        const std::vector<recipe_panel_text> &unique_alts = group.options;
        const std::string &label = group.label;

        if( is_expanded ) {
            // Expanded: label line, then each tool as a bullet below
            ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "  %s",
                                label.c_str() );
            float indent = ImGui::CalcTextSize( "      " ).x;
            ImGui::Indent( indent );
            for( const recipe_panel_text &tc : unique_alts ) {
                ImGui::TextColored( cataimgui::imvec4_from_color( tc.color ), "%s", tc.text.c_str() );
            }
            if( nav_clickable( _( "show less" ), c_dark_gray ) ) {
                expanded_tool_groups.erase( global_gi );
            }
            ImGui::Unindent( indent );
        } else {
            // Collapsed: label + tools on one line, truncated
            float avail_width = ImGui::GetContentRegionAvail().x;
            float or_w = ImGui::CalcTextSize( _( " or " ) ).x;
            std::string bullet_label = "  " + label;
            float used_w = ImGui::CalcTextSize( bullet_label.c_str() ).x;

            // Pre-measure how many fit
            int fits = 0;
            for( size_t i = 0; i < unique_alts.size(); ++i ) {
                float tw = ImGui::CalcTextSize( unique_alts[i].text.c_str() ).x;
                float sep = ( i > 0 ) ? or_w : 0.f;
                int rem = static_cast<int>( unique_alts.size() ) - fits - 1;
                float reserve = 0.f;
                if( rem > 0 ) {
                    std::string more = string_format( _( "or %d more" ), rem );
                    reserve = or_w + ImGui::CalcTextSize( more.c_str() ).x;
                }
                if( used_w + sep + tw + reserve > avail_width && fits > 0 ) {
                    break;
                }
                used_w += sep + tw;
                fits++;
            }

            ImGui::TextColored( cataimgui::imvec4_from_color( c_white ), "%s",
                                bullet_label.c_str() );
            ImGui::SameLine( 0, 0 );

            for( int i = 0; i < fits; ++i ) {
                if( i > 0 ) {
                    ImGui::SameLine( 0, 0 );
                    ImGui::TextColored( cataimgui::imvec4_from_color( c_dark_gray ), "%s",
                                        _( " or " ) );
                    ImGui::SameLine( 0, 0 );
                }
                ImGui::TextColored( cataimgui::imvec4_from_color( unique_alts[i].color ), "%s",
                                    unique_alts[i].text.c_str() );
            }

            int remaining = static_cast<int>( unique_alts.size() ) - fits;
            if( remaining > 0 ) {
                ImGui::SameLine( 0, 0 );
                std::string more_text = string_format( _( " or %d more" ), remaining );
                if( nav_clickable( more_text.c_str(), c_cyan ) ) {
                    expanded_tool_groups.insert( global_gi );
                }
            }
        }
        ImGui::Dummy( ImVec2( 0, 0 ) );
    }
}

// --- draw_item_info_panel ---

void crafting_ui_impl::draw_item_info_panel()
{
    if( current.empty() || !is_wide ) {
        return;
    }
    const recipe *cur_recipe = current[line];
    const int batch_size = get_batch_size();

    if( ImGui::BeginChild( "##ITEM_INFO", ImGui::GetContentRegionAvail(), false,
                           ImGuiWindowFlags_NoNav ) ) {
        cataimgui::set_scroll( item_info_scroll );
        if( cur_recipe->is_nested() || cur_recipe->is_practice() ) {
            // Nested groups and practice recipes are shown in the middle column
        } else {
            // panel_width is only used for centering separator titles;
            // display_item_info handles pixel-based text wrapping.
            int panel_width = static_cast<int>( ImGui::GetContentRegionAvail().x /
                                                ImGui::CalcTextSize( "X" ).x );
            // read in place: the copy into an item_info_data was made on every redraw
            static const std::vector<iteminfo> no_compare;
            display_item_info( result_info->get_result_info(
                                   cur_recipe, batch_size, embedded_item_scroll_dummy,
                                   panel_width, false ), no_compare );
        }
    }
    ImGui::EndChild();
}

// --- draw_status_header ---

void crafting_ui_impl::draw_status_header()
{
    if( ImGui::BeginTable( "##STATUS", 1, ImGuiTableFlags_None ) ) {
        ImGui::TableSetupColumn( "info", ImGuiTableColumnFlags_WidthStretch );

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if( cataimgui::BeginRightAlign( "##CRAFTER_NAME" ) ) {
            ImGui::Text( "%s: %s", _( "Crafter" ),
                         crafter->name_and_maybe_activity().c_str() );
            cataimgui::EndRightAlign();
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if( !show_hidden ) {
            draw_hidden_count();
        }

        ImGui::EndTable();
    }
}

// --- draw_hidden_count ---

void crafting_ui_impl::draw_hidden_count()
{
    nc_color col;
    std::string text;

    if( num_hidden == 1 ) {
        col = c_red;
        text = string_format( _( "* %d hidden recipe - %d in category *" ),
                              num_hidden, num_recipe );
    } else if( num_hidden >= 2 ) {
        col = c_red;
        text = string_format( _( "* %d hidden recipes - %d in category *" ),
                              num_hidden, num_recipe );
    } else {
        col = c_green;
        text = string_format( _( "* No hidden recipe - %d in category *" ), num_recipe );
    }

    if( cataimgui::BeginRightAlign( "##HIDDEN_COUNT" ) ) {
        ImGui::TextColored( cataimgui::imvec4_from_color( col ), "%s", text.c_str() );
        cataimgui::EndRightAlign();
    }
}

// --- rebuild_keybinding_tips ---

void crafting_ui_impl::rebuild_keybinding_tips()
{
    if( !ctxt_ptr ) {
        return;
    }
    std::vector<std::string> act_descs;
    const auto add_action_desc = [&]( const std::string & act, const std::string & txt ) {
        act_descs.emplace_back(
            ctxt_ptr->get_desc( act, txt, input_context::allow_all_keys ) );
    };
    if( info_nav_active ) {
        add_action_desc( "CONFIRM", pgettext( "crafting gui", "Activate" ) );
        add_action_desc( "TOGGLE_INFO_NAV", pgettext( "crafting gui", "Exit nav" ) );
        add_action_desc( "QUIT", pgettext( "crafting gui", "Back" ) );
    } else {
        add_action_desc( "CONFIRM", pgettext( "crafting gui", "Craft" ) );
        add_action_desc( "HELP_RECIPE", pgettext( "crafting gui", "Describe" ) );
        add_action_desc( "FILTER", pgettext( "crafting gui", "Filter" ) );
        add_action_desc( "RESET_FILTER", pgettext( "crafting gui", "Reset filter" ) );
        if( highlight_unread ) {
            add_action_desc( "TOGGLE_RECIPE_UNREAD", pgettext( "crafting gui", "Read/unread" ) );
            add_action_desc( "MARK_ALL_RECIPES_READ", pgettext( "crafting gui", "Mark all as read" ) );
            add_action_desc( "TOGGLE_UNREAD_RECIPES_FIRST",
                             pgettext( "crafting gui", "Show unread recipes first" ) );
        }
        add_action_desc( "HIDE_SHOW_RECIPE", pgettext( "crafting gui", "Show/hide" ) );
        add_action_desc( "RELATED_RECIPES", pgettext( "crafting gui", "Related" ) );
        add_action_desc( "TOGGLE_FAVORITE", pgettext( "crafting gui", "Favorite" ) );
        add_action_desc( "CYCLE_BATCH", pgettext( "crafting gui", "Batch" ) );
        add_action_desc( "BATCH_SIZE_UP", pgettext( "crafting gui", "Batch +" ) );
        add_action_desc( "BATCH_SIZE_DOWN", pgettext( "crafting gui", "Batch -" ) );
        add_action_desc( "CHOOSE_CRAFTER", pgettext( "crafting gui", "Choose crafter" ) );
        add_action_desc( "PRIORITIZE_MISSING_COMPONENTS", pgettext( "crafting gui", "Prioritize" ) );
        add_action_desc( "DEPRIORITIZE_COMPONENTS", pgettext( "crafting gui", "Deprioritize" ) );
        add_action_desc( "TOGGLE_INFO_NAV", pgettext( "crafting gui", "Navigate details" ) );
        add_action_desc( "HELP_KEYBINDINGS", pgettext( "crafting gui", "Keybindings" ) );
    }

    keybinding_tips = enumerate_as_string( act_descs, enumeration_conjunction::none );
}

// --- draw_keybinding_footer ---

void crafting_ui_impl::draw_keybinding_footer()
{
    ImGui::Separator();
    cataimgui::TextColoredParagraph( c_dark_gray, keybinding_tips );
}

// --- recalculate_recipes ---

void crafting_ui_impl::recalculate_recipes()
{
    recalc = false;
    const recipe *prev_rcp = nullptr;
    if( keepline && line >= 0 && static_cast<size_t>( line ) < current.size() ) {
        prev_rcp = current[line];
    }
    show_hidden = false;
    available.clear();

    if( batch ) {
        current.clear();
        for( int i = 1; i <= 50; i++ ) {
            current.push_back( chosen );
            available.emplace_back( *crafter, chosen, i, camp_crafting, inventory_override );
        }
        indent_vec.assign( current.size(), 0 );
    } else {
        static_popup popup;
        std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
        static constexpr std::chrono::milliseconds update_interval( 500 );
        input_context dummy;
        dummy.register_action( "QUIT" );
        std::string cancel_btn = dummy.get_button_text( "QUIT", _( "Cancel" ) );
        std::function<void( size_t, size_t )> progress_callback =
        [&]( size_t at, size_t out_of ) {
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            if( now - last_update < update_interval ) {
                return;
            }
            last_update = now;
            double percent = 100.0 * at / out_of;
            popup.message( _( "Searching\u2026 %3.0f%%\n%s\n" ), percent, cancel_btn );
            ui_manager::redraw();
            refresh_display();
            inp_mngr.pump_events();
        };

        std::vector<const recipe *> picking;
        bool skip_hidden = false;
        if( !filterstring.empty() ) {
            std::string qry = trim( filterstring );
            recipe_subset filtered_recipes =
                filter_recipes( *available_recipes, qry, *crafter, progress_callback );
            picking.insert( picking.end(), filtered_recipes.begin(), filtered_recipes.end() );
        } else {
            const std::pair<std::vector<const recipe *>, bool> result = recipes_from_cat(
                        *available_recipes,
                        crafting_category_id( tab.cur() ), subtab.cur() );
            picking = result.first;
            skip_hidden = result.second;
            show_hidden = result.second;
        }

        const bool skip_sort = ( subtab.cur() == "CSC_*_RECENT" );
        num_recipe = picking.size();
        recipe_list_data list_result = build_recipe_list(
                                           std::move( picking ), skip_hidden, skip_sort,
                                           *crafter, camp_crafting, inventory_override,
                                           highlight_unread, unread_recipes_first,
                                           *availability_cache, *available_recipes );
        current = std::move( list_result.entries );
        indent_vec = std::move( list_result.indent );
        available = std::move( list_result.available );
        num_hidden = list_result.num_hidden;
    }

    line = 0;
    if( keepline && prev_rcp ) {
        int rcp_idx = 0;
        for( const recipe *const rcp : current ) {
            if( rcp == prev_rcp ) {
                line = rcp_idx;
                break;
            }
            ++rcp_idx;
        }
    }
    keepline = false;
    // In batch mode, sync line to manual_batch
    if( batch ) {
        line = std::clamp( manual_batch - 1, 0,
                           std::max( 0, static_cast<int>( current.size() ) - 1 ) );
    }
    invalidate_info_panels();
    recalc_unread = highlight_unread;
}

// --- recalculate_unread ---

void crafting_ui_impl::recalculate_unread()
{
    recalc_unread = false;
    if( !highlight_unread ) {
        return;
    }

    if( filterstring.empty() ) {
        for( const std::string &cat : crafting_cats ) {
            is_cat_unread[cat] = false;
            for( const std::string &sc : crafting_category_id( cat )->subcategories ) {
                is_subcat_unread[cat][sc] = false;
                const auto result = recipes_from_cat( *available_recipes,
                                                      crafting_category_id( cat ), sc );
                const std::vector<const recipe *> &recipes = result.first;
                const bool include_hidden = result.second;
                for( const recipe *const rcp : recipes ) {
                    const recipe_id &rcp_id = rcp->ident();
                    if( !include_hidden && uistate.hidden_recipes.count( rcp_id ) ) {
                        continue;
                    }
                    if( uistate.read_recipes.count( rcp_id ) ) {
                        continue;
                    }
                    is_cat_unread[cat] = true;
                    is_subcat_unread[cat][sc] = true;
                    break;
                }
            }
        }
    } else {
        is_filtered_unread = false;
        for( const recipe *const rcp : current ) {
            const recipe_id &rcp_id = rcp->ident();
            if( uistate.hidden_recipes.count( rcp_id ) ) {
                continue;
            }
            if( uistate.read_recipes.count( rcp_id ) ) {
                continue;
            }
            is_filtered_unread = true;
            break;
        }
    }
}

// --- invalidate_info_panels ---

void crafting_ui_impl::invalidate_info_panels()
{
    panel_cache.invalidate();
    recipe_info_scroll = cataimgui::scroll::begin;
    item_info_scroll = cataimgui::scroll::begin;
    need_scroll_to_selected = true;
    expanded_tool_groups.clear();
    expanded_comp_groups.clear();
    if( info_nav_active ) {
        info_nav_active = false;
        rebuild_keybinding_tips();
    }
}

// --- process_action ---

void crafting_ui_impl::process_action( const std::string &action_in,
                                       input_context &ctxt )
{
    std::string action = action_in;

    // Snapshot state before mutation
    int prev_line = line;
    const bool previously_toggled_unread = just_toggled_unread;
    just_toggled_unread = false;

    // Consume pending line click (tab/subtab intents are handled inline in draw_controls)
    if( pending_line_click >= 0 && info_nav_active ) {
        info_nav_active = false;
        rebuild_keybinding_tips();
    }
    if( pending_line_click >= 0 ) {
        if( pending_line_click == line ) {
            action = "CONFIRM";
        } else {
            if( !previously_toggled_unread ) {
                last_line = line;
            }
            line = pending_line_click;
            user_moved_line = highlight_unread;
        }
    }
    pending_line_click = -1;

    // Consume pending batch view toggles
    if( pending_enter_batch && !batch && !current.empty() &&
        line >= 0 && line < static_cast<int>( current.size() ) &&
        !current[line]->is_nested() ) {
        chosen = current[line];
        batch_line = line;
        batch = true;
        if( manual_batch < 2 ) {
            manual_batch = 2;
        }
        recalc = true;
    }
    pending_enter_batch = false;
    if( pending_exit_batch && batch ) {
        batch = false;
        manual_batch = 1;
        keepline = true;
        recalc = true;
    }
    pending_exit_batch = false;

    // Consume pending batch size change from +/- buttons
    if( pending_batch_delta != 0 ) {
        bool can_batch_here = !current.empty() &&
                              line >= 0 && line < static_cast<int>( current.size() ) &&
                              !current[line]->is_nested();
        if( can_batch_here ) {
            int new_batch = std::clamp( manual_batch + pending_batch_delta, 1, 50 );
            if( new_batch != manual_batch ) {
                // Auto-enter batch mode on first increment from x1
                if( manual_batch == 1 && new_batch > 1 && !batch ) {
                    chosen = current[line];
                    batch_line = line;
                    batch = true;
                    recalc = true;
                }
                manual_batch = new_batch;
                if( batch && !recalc ) {
                    line = manual_batch - 1;
                    need_scroll_to_selected = true;
                }
            }
        }
        pending_batch_delta = 0;
    }

    // Recalculate if needed
    if( recalc ) {
        recalculate_recipes();
    }

    // Info-nav mode: intercept list-moving and confirm actions.
    // Uses a flag instead of early return so the shared tail (clamp, batch-sync,
    // invalidate_info_panels) still runs.
    bool handled_by_nav = false;
    if( info_nav_active ) {
        handled_by_nav = true;
        if( action == "DOWN" ) {
            info_nav_cursor = std::min( info_nav_cursor + 1,
                                        std::max( 0, info_nav_count - 1 ) );
            need_scroll_to_selected_recipe_details = true;
        } else if( action == "UP" ) {
            info_nav_cursor = std::max( info_nav_cursor - 1, 0 );
            need_scroll_to_selected_recipe_details = true;
        } else if( action == "CONFIRM" ) {
            info_nav_activated = info_nav_cursor;
        } else if( action == "TOGGLE_INFO_NAV" || action == "QUIT" ) {
            info_nav_active = false;
            rebuild_keybinding_tips();
        } else if( action == "PAGE_UP" ) {
            recipe_info_scroll = cataimgui::scroll::page_up;
        } else if( action == "PAGE_DOWN" ) {
            recipe_info_scroll = cataimgui::scroll::page_down;
        } else if( action == "BATCH_SIZE_UP" ) {
            pending_batch_delta = 1;
        } else if( action == "BATCH_SIZE_DOWN" ) {
            pending_batch_delta = -1;
        }
    } else if( action == "TOGGLE_INFO_NAV" ) {
        handled_by_nav = true;
        info_nav_active = true;
        info_nav_cursor = 0;
        rebuild_keybinding_tips();
    }

    // Auto-mark-read on cursor movement
    if( !handled_by_nav && highlight_unread && !current.empty() && user_moved_line ) {
        user_moved_line = false;
        uistate.read_recipes.insert( current[line]->ident() );
        if( last_line != -1 ) {
            uistate.read_recipes.insert( current[last_line]->ident() );
            last_line = -1;
        }
        recalc_unread = true;
    }

    const int recmax = static_cast<int>( current.size() );
    const int scroll_rate = recmax > 20 ? 10 : 3;

    // Process keyboard actions (skipped when info-nav handled the action)
    if( handled_by_nav ) {
        // nothing -- fall through to shared tail
    } else if( action == "SCROLL_RECIPE_INFO_UP" ) {
        recipe_info_scroll = cataimgui::scroll::page_up;
    } else if( action == "SCROLL_RECIPE_INFO_DOWN" ) {
        recipe_info_scroll = cataimgui::scroll::page_down;
    } else if( action == "SCROLL_ITEM_INFO_UP" ) {
        item_info_scroll = cataimgui::scroll::page_up;
    } else if( action == "SCROLL_ITEM_INFO_DOWN" ) {
        item_info_scroll = cataimgui::scroll::page_down;
    } else if( action == "LEFT" ) {
        if( !batch && filterstring.empty() ) {
            std::string start = subtab.cur();
            do {
                subtab.prev();
            } while( subtab.cur() != start &&
                     available_recipes->empty_category( crafting_category_id( tab.cur() ),
                             subtab.cur() != "CSC_ALL" ? subtab.cur() : "" ) );
            recalc = true;
            force_select_subtab = true;
        }
    } else if( action == "RIGHT" ) {
        if( !batch && filterstring.empty() ) {
            std::string start = subtab.cur();
            do {
                subtab.next();
            } while( subtab.cur() != start &&
                     available_recipes->empty_category( crafting_category_id( tab.cur() ),
                             subtab.cur() != "CSC_ALL" ? subtab.cur() : "" ) );
            recalc = true;
            force_select_subtab = true;
        }
    } else if( action == "PREV_TAB" ) {
        tab.prev();
        subtab = tab_list( crafting_category_id( tab.cur() )->subcategories );
        recalc = true;
        force_select_tab = true;
        force_select_subtab = true;
    } else if( action == "NEXT_TAB" ) {
        tab.next();
        subtab = tab_list( crafting_category_id( tab.cur() )->subcategories );
        recalc = true;
        force_select_tab = true;
        force_select_subtab = true;
    } else if( action == "DOWN" ) {
        if( !previously_toggled_unread ) {
            last_line = line;
        }
        line++;
        user_moved_line = highlight_unread;
    } else if( action == "UP" ) {
        if( !previously_toggled_unread ) {
            last_line = line;
        }
        line--;
        user_moved_line = highlight_unread;
    } else if( action == "PAGE_UP" || action == "PAGE_DOWN" ) {
        if( recmax > 0 ) {
            line = inc_clamp( line, action == "PAGE_UP" ? -scroll_rate : scroll_rate, recmax - 1 );
        }
    } else if( action == "HOME" ) {
        line = 0;
        user_moved_line = highlight_unread;
    } else if( action == "END" ) {
        line = -1;
        user_moved_line = highlight_unread;
    } else if( action == "CONFIRM" && !info_nav_active ) {
        if( current.empty() ) {
            popup( _( "Nothing selected!" ) );
        } else if( current[line]->is_nested() ) {
            nested_toggle( current[line]->ident(), recalc, keepline );
        } else {
            const int bs = get_batch_size();
            const recipe crafting_rec = *current[line];
            craft_confirm_result confirm = can_start_craft(
                                               crafting_rec, available[line], *crafter, bs );
            switch( confirm ) {
                case craft_confirm_result::cannot_craft:
                    popup( _( "Crafter can't craft that!" ) );
                    break;
                case craft_confirm_result::too_many_results:
                    popup( string_format( _( "Batch would create too many items (%1$d).  The limit is %2$d." ),
                                          crafting_rec.makes_amount() * bs, MAX_ITEM_IN_SQUARE ) );
                    break;
                case craft_confirm_result::too_dark:
                    popup( _( "Crafter can't see!" ) );
                    break;
                case craft_confirm_result::ok:
                    if( available[line].inv_override == nullptr &&
                        !crafter->check_eligible_containers_for_crafting( *current[line], bs ) ) {
                        break;
                    }
                    chosen = current[line];
                    done = true;
                    uistate.read_recipes.insert( chosen->ident() );
                    break;
            }
        }
    } else if( action == "HELP_RECIPE" && selection_ok( current, line, false ) ) {
        uistate.read_recipes.insert( current[line]->ident() );
        recalc_unread = highlight_unread;

        int info_width = std::min( static_cast<int>( ImGui::GetMainViewport()->Size.x /
                                   ImGui::CalcTextSize( "X" ).x ), 80 );
        int info_height = static_cast<int>( ImGui::GetMainViewport()->Size.y /
                                            ImGui::GetTextLineHeightWithSpacing() );
        item_info_data data = result_info->get_result_data( current[line], 1,
                              line_item_info_popup, info_width );
        data.handle_scrolling = true;
        data.arrow_scrolling = true;
        iteminfo_window info_window( data,
                                     point::north_west, info_width, info_height );
        info_window.execute();
    } else if( action == "FILTER" ) {
        if( filter_crafting_recipes( filterstring ) ) {
            recalc = true;
            recalc_unread = highlight_unread;
            if( batch ) {
                batch = false;
                manual_batch = 1;
                line = batch_line;
            }
        }
    } else if( action == "RESET_FILTER" ) {
        filterstring.clear();
        recalc = true;
        recalc_unread = highlight_unread;
    } else if( action == "CYCLE_BATCH" && selection_ok( current, line, false ) ) {
        batch = !batch;
        if( batch ) {
            batch_line = line;
            chosen = current[batch_line];
            if( manual_batch < 2 ) {
                manual_batch = 2;
            }
            uistate.read_recipes.insert( chosen->ident() );
            recalc_unread = highlight_unread;
        } else {
            manual_batch = 1;
            keepline = true;
        }
        recalc = true;
    } else if( action == "CHOOSE_CRAFTER" ) {
        bool rec_valid = !current.empty();
        const recipe *rec = rec_valid ? current[line] : nullptr;
        int new_crafter_i = choose_crafter( crafting_group, crafter_i, rec, rec_valid );
        if( new_crafter_i >= 0 && new_crafter_i != crafter_i ) {
            crafter_i = new_crafter_i;
            crafter = crafting_group[crafter_i];
            available_recipes = &crafter->get_group_available_recipes( inventory_override );
            availability_cache = &guy_availability_cache[crafter->getID()];
            result_info = std::make_unique<recipe_result_info_cache>( *crafter );
            panel_cache.invalidate();

            recalc = true;
            keepline = true;
            recipe_info_scroll = cataimgui::scroll::begin;
            item_info_scroll = cataimgui::scroll::begin;
            need_scroll_to_selected = true;
        }
    } else if( action == "TOGGLE_FAVORITE" && selection_ok( current, line, true ) ) {
        keepline = true;
        recalc = filterstring.empty() && subtab.cur() == "CSC_*_FAVORITE";
        if( uistate.favorite_recipes.find( current[line]->ident() ) !=
            uistate.favorite_recipes.end() ) {
            uistate.favorite_recipes.erase( current[line]->ident() );
            if( recalc ) {
                if( static_cast<size_t>( line ) + 1 < current.size() ) {
                    line++;
                } else {
                    line--;
                }
            }
        } else {
            uistate.favorite_recipes.insert( current[line]->ident() );
            uistate.read_recipes.insert( current[line]->ident() );
        }
        recalc_unread = highlight_unread;
    } else if( action == "HIDE_SHOW_RECIPE" && selection_ok( current, line, true ) ) {
        if( show_hidden ) {
            uistate.hidden_recipes.erase( current[line]->ident() );
        } else {
            uistate.hidden_recipes.insert( current[line]->ident() );
            uistate.read_recipes.insert( current[line]->ident() );
        }
        recalc = true;
        recalc_unread = highlight_unread;
        keepline = true;
        if( static_cast<size_t>( line ) + 1 < current.size() ) {
            line++;
        } else {
            line--;
        }
    } else if( action == "TOGGLE_RECIPE_UNREAD" && selection_ok( current, line, true ) ) {
        const recipe_id rcp = current[line]->ident();
        if( uistate.read_recipes.count( rcp ) ) {
            for( const recipe_id &nested_rcp : rcp->nested_category_data ) {
                uistate.read_recipes.erase( nested_rcp );
            }
            uistate.read_recipes.erase( rcp );
        } else {
            for( const recipe_id &nested_rcp : rcp->nested_category_data ) {
                uistate.read_recipes.insert( nested_rcp );
            }
            uistate.read_recipes.insert( rcp );
        }
        recalc_unread = highlight_unread;
        just_toggled_unread = true;
    } else if( action == "MARK_ALL_RECIPES_READ" ) {
        bool current_list_has_unread = false;
        for( const recipe *const rcp : current ) {
            for( const recipe_id &nested_rcp : rcp->nested_category_data ) {
                if( !uistate.read_recipes.count( nested_rcp->ident() ) ) {
                    current_list_has_unread = true;
                    break;
                }
            }
            if( current_list_has_unread ) {
                break;
            }
            if( !uistate.read_recipes.count( rcp->ident() ) ) {
                current_list_has_unread = true;
                break;
            }
        }
        std::string query_str;
        if( !current_list_has_unread ) {
            query_str = _( "<color_yellow>/!\\</color> Mark all recipes as read?  "
                           // NOLINTNEXTLINE(cata-text-style): single spaced for symmetry
                           "This cannot be undone. <color_yellow>/!\\</color>" );
        } else if( filterstring.empty() ) {
            query_str = string_format( _( "Mark recipes in this tab as read?  This cannot be undone.  "
                                          "You can mark all recipes by choosing yes and pressing %s again." ),
                                       ctxt.get_desc( "MARK_ALL_RECIPES_READ" ) );
        } else {
            query_str = string_format( _( "Mark filtered recipes as read?  This cannot be undone.  "
                                          "You can mark all recipes by choosing yes and pressing %s again." ),
                                       ctxt.get_desc( "MARK_ALL_RECIPES_READ" ) );
        }
        if( query_yn( query_str ) ) {
            if( current_list_has_unread ) {
                for( const recipe *const rcp : current ) {
                    for( const recipe_id &nested_rcp : rcp->nested_category_data ) {
                        uistate.read_recipes.insert( nested_rcp->ident() );
                    }
                    uistate.read_recipes.insert( rcp->ident() );
                }
            } else {
                for( const recipe *const rcp : *available_recipes ) {
                    for( const recipe_id &nested_rcp : rcp->nested_category_data ) {
                        uistate.read_recipes.insert( nested_rcp->ident() );
                    }
                    uistate.read_recipes.insert( rcp->ident() );
                }
            }
        }
        recalc_unread = highlight_unread;
    } else if( action == "TOGGLE_UNREAD_RECIPES_FIRST" ) {
        unread_recipes_first = !unread_recipes_first;
        recalc = true;
        keepline = true;
    } else if( action == "RELATED_RECIPES" && selection_ok( current, line, false ) ) {
        uistate.read_recipes.insert( current[line]->ident() );
        recalc_unread = highlight_unread;

        std::string recipe_name = peek_related_recipe( current[line], *available_recipes, *crafter );
        if( !recipe_name.empty() ) {
            filterstring = recipe_name;
            recalc = true;
            recalc_unread = highlight_unread;
        }
    } else if( action == "COMPARE" && selection_ok( current, line, false ) ) {
        hide_ui = true;
        const item recipe_result = get_recipe_result_item( *current[line], *crafter );
        compare_recipe_with_item( recipe_result, *crafter );
        hide_ui = false;
    } else if( action == "PRIORITIZE_MISSING_COMPONENTS" && selection_ok( current, line, false ) ) {
        uistate.read_recipes.insert( current[line]->ident() );
        recalc_unread = highlight_unread;
        prioritize_components( *current[line], *crafter );
    } else if( action == "DEPRIORITIZE_COMPONENTS" && selection_ok( current, line, false ) ) {
        uistate.read_recipes.insert( current[line]->ident() );
        recalc_unread = highlight_unread;
        deprioritize_components( *current[line] );
    } else if( action == "BATCH_SIZE_UP" ) {
        pending_batch_delta = 1;
    } else if( action == "BATCH_SIZE_DOWN" ) {
        pending_batch_delta = -1;
    } else if( action == "QUIT" ) {
        if( batch ) {
            batch = false;
            manual_batch = 1;
            keepline = true;
            recalc = true;
        } else if( !filterstring.empty() ) {
            filterstring.clear();
            recalc = true;
        } else {
            chosen = nullptr;
            done = true;
        }
    } else if( action == "HELP_KEYBINDINGS" ) {
        // Rebuild keybinding tips after keybinding editor
        rebuild_keybinding_tips();
    }

    // Clamp line
    if( current.empty() || line >= static_cast<int>( current.size() ) ) {
        line = 0;
    } else if( line < 0 ) {
        line = static_cast<int>( current.size() ) - 1;
    }

    // Sync manual_batch with line when navigating in batch mode
    if( batch && line != prev_line && line >= 0 &&
        line < static_cast<int>( current.size() ) ) {
        manual_batch = line + 1;
    }

    // Invalidate info panels if selection changed
    if( line != prev_line ) {
        invalidate_info_panels();
    }
}

// ---------------------------------------------------------------------------
// Public entry point -- now uses crafting_ui_impl
// ---------------------------------------------------------------------------

std::pair<Character *, const recipe *> select_crafter_and_crafting_recipe( int &batch_size_out,
        const recipe_id &goto_recipe, Character *crafter, std::string filterstring, bool camp_crafting,
        temp_crafting_inventory *inventory_override )
{
    if( crafter == nullptr ) {
        return { nullptr, nullptr };
    }

    // nothing reachable from this menu changes what the query caches read
    temp_crafting_inventory::query_cache_scope cache_scope;
    crafting_ui_impl impl( crafter, goto_recipe, std::move( filterstring ),
                           camp_crafting, inventory_override );
    input_context ctxt = make_crafting_context(
                             get_option<bool>( "HIGHLIGHT_UNREAD_RECIPES" ) );
    ctxt.set_timeout( 10 );
    impl.set_input_context( &ctxt );

    while( !impl.is_done() ) {
        ui_manager::redraw_invalidated();
        std::string action = ctxt.handle_input();
        if( !impl.get_is_open() ) {
            return { nullptr, nullptr };
        }
        impl.process_action( action, ctxt );
    }

    batch_size_out = impl.get_batch_size();
    return { impl.get_crafter(), impl.get_chosen() };
}


int choose_crafter( const std::vector<Character *> &crafting_group, int crafter_i,
                    const recipe *rec, bool rec_valid )
{
    std::vector<std::string> header = { _( "Crafter" ) };
    if( rec_valid ) {
        header.emplace_back( rec->is_practice() ? _( "Can practice" ) : _( "Can craft" ) );
        header.emplace_back( _( "Missing" ) );
    }
    header.emplace_back( _( "Status" ) );
    uimenu choose_char_menu( static_cast<int>( header.size() ) );
    choose_char_menu.set_title( _( "Choose the crafter" ) );
    choose_char_menu.addentry( -1, false, header );

    int i = 0;
    for( Character *chara : crafting_group ) {
        std::vector<std::string> entry = { chara->name_and_maybe_activity() };
        if( rec_valid ) {
            availability avail = availability( *chara, rec );
            std::vector<std::string> reasons;

            bool has_stuff = rec->deduped_requirements().can_make_with_inventory(
                                 chara, chara->crafting_inventory(), rec->get_component_filter( recipe_filter_flags::none ), 1,
                                 craft_flags::start_only );
            if( !has_stuff ) {
                reasons.emplace_back( _( "stuff" ) );
            }
            if( !avail.crafter_has_primary_skill ) {
                reasons.emplace_back( _( "skill" ) );
            }
            if( !avail.has_proficiencies ) {  // this is required proficiency
                reasons.emplace_back( _( "proficiency" ) );
            }
            if( chara->lighting_craft_speed_multiplier( *rec ) <= 0.0f ) {
                reasons.emplace_back( _( "light" ) );
            }
            std::string dummy;
            if( chara->is_npc() && !rec->npc_can_craft( dummy ) ) {
                reasons.emplace_back( _( "is NPC" ) );
            }
            if( !rec->crafter_meets_condition( *chara ) ) {
                reasons.emplace_back( _( "condition" ) );
            }

            entry.emplace_back(
                // *INDENT-OFF* readable ternary operator
                rec->is_nested()
                    ? colorize( "-", c_yellow )
                    : reasons.empty()
                        ? colorize( _( "yes" ), c_green )
                        : colorize( _( "no" ), c_red ) );
                // *INDENT-ON* readable ternary operator
            entry.emplace_back( colorize( string_join( reasons, ", " ), c_red ) );
        }
        entry.emplace_back( chara->in_sleep_state()
                            ? colorize( _( "asleep" ), c_red )
                            : colorize( _( "awake" ), c_green ) );
        choose_char_menu.addentry( i, !chara->in_sleep_state(), entry );
        ++i;
    }

    choose_char_menu.set_selected( crafter_i + 1 );  // +1 for header entry
    return choose_char_menu.query();
}

std::string peek_related_recipe( const recipe *current, const recipe_subset &available,
                                 Character &crafter )
{
    auto compare_second =
        []( const std::pair<itype_id, std::string> &a,
    const std::pair<itype_id, std::string> &b ) {
        return localized_compare( a.second, b.second );
    };

    // current recipe components
    std::vector<std::pair<itype_id, std::string>> related_components;
    const requirement_data &req = current->simple_requirements();
    for( const std::vector<item_comp> &comp_list : req.get_components() ) {
        for( const item_comp &a : comp_list ) {
            related_components.emplace_back( a.type, item::nname( a.type, 1 ) );
        }
    }
    std::sort( related_components.begin(), related_components.end(), compare_second );
    // current recipe result
    std::vector<std::pair<itype_id, std::string>> related_results;
    item tmp( current->result() );
    // use this item
    const itype_id tid = tmp.typeId();
    const std::set<const recipe *> &known_recipes =
        crafter.get_learned_recipes().of_component( tid );
    for( const recipe * const &b : known_recipes ) {
        if( available.contains( b ) ) {
            related_results.emplace_back( b->result(), b->result_name( /*decorated=*/true ) );
        }
    }
    std::stable_sort( related_results.begin(), related_results.end(), compare_second );

    if( related_components.empty() && related_results.empty() ) {
        return "";
    }

    uilist rel_menu;
    int np_last = -1;
    if( !related_components.empty() ) {
        rel_menu.addentry( ++np_last, false, -1, _( "COMPONENTS" ) );
    }
    np_last = related_menu_fill( rel_menu, related_components, available );
    if( !related_results.empty() ) {
        rel_menu.addentry( ++np_last, false, -1, _( "RESULTS" ) );
    }

    related_menu_fill( rel_menu, related_results, available );

    rel_menu.settext( _( "Related recipes:" ) );
    rel_menu.query();
    if( rel_menu.ret != UILIST_CANCEL ) {

        // Grab the recipe name without our bullet point.
        std::string recipe = rel_menu.entries[rel_menu.ret].txt.substr( strlen( "─ " ) );

        // If the string is decorated as a favourite, return it without the star
        if( recipe.rfind( "* ", 0 ) == 0 ) {
            return recipe.substr( strlen( "* " ) );
        }

        return recipe;
    }

    return "";
}

int related_menu_fill( uilist &rmenu,
                       const std::vector<std::pair<itype_id, std::string>> &related_recipes,
                       const recipe_subset &available )
{
    const std::vector<uilist_entry> &entries = rmenu.entries;
    int np_last = entries.empty() ? -1 : entries.back().retval;

    if( related_recipes.empty() ) {
        return np_last;
    }

    std::string recipe_name_prev;
    for( const std::pair<itype_id, std::string> &p : related_recipes ) {

        // we have different recipes with the same names
        // list only one of them as we show and filter by name only
        std::string recipe_name = p.second;
        if( recipe_name == recipe_name_prev ) {
            continue;
        }
        recipe_name_prev = recipe_name;

        std::vector<const recipe *> current_part = available.recipes_that_produce( p.first );
        if( current_part.empty() ) {
            continue;
        }

        bool different_recipes = false;

        // 1st pass: check if we need to add group
        for( size_t recipe_n = 0; recipe_n < current_part.size(); recipe_n++ ) {
            if( current_part[recipe_n]->result_name( /*decorated=*/true ) != recipe_name ) {
                // add group
                rmenu.addentry( ++np_last, false, -1, recipe_name );
                different_recipes = true;
                break;
            } else if( recipe_n == current_part.size() - 1 ) {
                // only one result
                rmenu.addentry( ++np_last, true, -1, "─ " + recipe_name );
            }
        }
        if( !different_recipes ) {
            continue;
        }
        std::string prev_item_name;
        // 2nd pass: add different recipes
        for( size_t recipe_n = 0; recipe_n < current_part.size(); recipe_n++ ) {
            std::string cur_item_name = current_part[recipe_n]->result_name( /*decorated=*/true );
            if( cur_item_name != prev_item_name ) {
                std::string sym = recipe_n == current_part.size() - 1 ? "└ " : "├ ";
                rmenu.addentry( ++np_last, true, -1, sym + cur_item_name );
            }
            prev_item_name = cur_item_name;
        }
    }

    return np_last;
}

static void compare_recipe_with_item( const item &recipe_item, Character &crafter )
{
    inventory_pick_selector inv_s( crafter );

    inv_s.add_character_items( crafter );
    inv_s.set_title( _( "Compare" ) );
    inv_s.set_hint( _( "Select item to compare with." ) );

    if( inv_s.empty() ) {
        popup( std::string( _( "There are no items to compare." ) ), PF_GET_KEY );
        return;
    }

    do {
        const item_location to_compare = inv_s.execute();
        if( !to_compare ) {
            break;
        }
        game_menus::inv::compare_item_menu menu( recipe_item, *to_compare );
        menu.show();
    } while( true );
}

static void prioritize_components( const recipe &recipe, Character &crafter )
{
    int new_filters_count = 0;
    std::string added_filters;
    const requirement_data &req = recipe.simple_requirements();
    const temp_crafting_inventory &crafting_inv = crafter.crafting_inventory();
    for( const std::vector<item_comp> &comp_list : req.get_components() ) {
        for( const item_comp &i_comp : comp_list ) {
            std::string nname = item::nname( i_comp.type, 1 );
            int filter_pos = uistate.list_item_priority.find( nname );
            bool enough_materials = req.check_enough_materials(
                                        &crafter, i_comp, crafting_inv, recipe.get_component_filter(), 1
                                    );
            if( filter_pos == -1 && !enough_materials ) {
                new_filters_count++;
                added_filters += nname;
                added_filters += ",";
            }
        }
    }

    if( new_filters_count > 0 ) {
        if( !uistate.list_item_priority.empty()
            && !string_ends_with( uistate.list_item_priority, "," ) ) {
            uistate.list_item_priority += ",";
        }
        uistate.list_item_priority += added_filters;
        uistate.list_item_priority_active = true;
        std::vector<std::string> &hist = uistate.gethistory( "list_item_priority" );
        if( hist.empty() || hist[hist.size() - 1] != uistate.list_item_priority ) {
            hist.push_back( uistate.list_item_priority );
        }

        popup( string_format( _( "Added %d components to the priority filter.\nAdded: %s\nNew Filter: %s" ),
                              new_filters_count, added_filters, uistate.list_item_priority ) );
    } else {
        popup( string_format( _( "Did not find anything to add to the priority filter.\n\nFilter: %s" ),
                              uistate.list_item_priority ) );
    }
}

static void deprioritize_components( const recipe &recipe )
{
    int removed_filters_count = 0;
    std::string removed_filters;
    const requirement_data &req = recipe.simple_requirements();
    for( const std::vector<item_comp> &comp_list : req.get_components() ) {
        for( const item_comp &i_comp : comp_list ) {
            std::string nname = item::nname( i_comp.type, 1 );
            nname += ",";
            if( string_starts_with( uistate.list_item_priority, nname ) ) {
                removed_filters_count++;
                removed_filters += nname;
                uistate.list_item_priority.erase( 0, nname.length() );
            } else {
                std::string find_string = "," + nname;
                int filter_pos = uistate.list_item_priority.find( find_string );
                if( filter_pos > -1 ) {
                    removed_filters_count++;
                    removed_filters += nname;
                    uistate.list_item_priority.replace( filter_pos, find_string.length(), "," );
                }
            }
        }
    }

    if( !uistate.list_item_priority.empty() ) {
        uistate.list_item_priority_active = true;

        std::vector<std::string> &hist = uistate.gethistory( "list_item_priority" );
        if( hist.empty() || hist[hist.size() - 1] != uistate.list_item_priority ) {
            hist.push_back( uistate.list_item_priority );
        }
    } else {
        uistate.list_item_priority_active = false;
    }

    if( removed_filters_count > 0 ) {
        popup( string_format(
                   _( "Removed %d components from the priority filter.\nRemoved: %s\nNew Filter: %s" ),
                   removed_filters_count, removed_filters, uistate.list_item_priority ) );
    } else {
        popup( string_format( _( "Did not find anything to remove from the priority filter.\nFilter: %s" ),
                              uistate.list_item_priority ) );
    }
}



const std::vector<std::string> *subcategories_for_category( const std::string &category )
{
    crafting_category_id cat( category );
    if( !cat.is_valid() ) {
        return nullptr;
    }
    return &cat->subcategories;
}

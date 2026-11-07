#pragma once
#ifndef CATA_SRC_CRAFTING_GUI_HELPERS_H
#define CATA_SRC_CRAFTING_GUI_HELPERS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "color.h"
#include "coordinates.h"
#include "item.h"
#include "point.h"
#include "proficiency.h"
#include "translation.h"

class Character;
class recipe;
class recipe_subset;
class temp_crafting_inventory;
struct crafting_cost_context;
struct tool_comp;

// Returns true if the character cannot gain any skill or proficiency from this recipe.
// Used to mark practice recipes as "useless" when the crafter already exceeds
// the recipe's skill cap and has all used proficiencies.
bool cannot_gain_skill_or_prof( const Character &crafter, const recipe &recp );

// Computes whether a Character can craft a given recipe.
// Stores craftability flags, color-coding, and lazy-cached proficiency maluses.
struct availability {
        explicit availability( Character &_crafter, const recipe *recp, int batch_size = 1,
                               bool camp_crafting = false, temp_crafting_inventory *inventory_override = nullptr );
        Character &crafter;
        bool can_craft_recipe;
        // group can introduce recipe this crafter cannot craft because of low primary skill
        bool crafter_has_primary_skill;
        bool would_use_rotten;
        bool would_use_favorite;
        bool useless_practice;
        bool apparently_craftable;
        bool has_proficiencies;
        bool has_all_skills;
        bool is_nested_category;
        // Used as an indicator to see if crafting is called via camp.
        // If not nullptr, we must be camp crafting.
        temp_crafting_inventory *inv_override;
    private:
        const recipe *rec;
        mutable float proficiency_time_maluses = -1.0f;
        mutable float max_proficiency_time_maluses = -1.0f;
        mutable float proficiency_skill_maluses = -1.0f;
        mutable float max_proficiency_skill_maluses = -1.0f;
        mutable std::optional<book_proficiency_bonuses> cached_book_bonuses;
        const book_proficiency_bonuses &get_book_bonuses() const;
    public:
        float get_proficiency_time_maluses() const;
        float get_max_proficiency_time_maluses() const;
        float get_proficiency_skill_maluses() const;
        float get_max_proficiency_skill_maluses() const;

        nc_color selected_color() const;
        nc_color color( bool ignore_missing_skills = false ) const;

        static bool check_can_craft_nested( Character &_crafter, const recipe &r, bool camp_crafting,
                                            temp_crafting_inventory *inventory_override );
};

// The availability of `rec` within one menu session, built on first use.  The cache is keyed on
// the recipe alone, so one cache must serve one crafter with one inventory context.
const availability &cached_availability( std::map<const recipe *, availability> &cache,
        Character &crafter, const recipe &rec, bool camp_crafting,
        temp_crafting_inventory *inventory_override );

enum class craft_confirm_result {
    ok,
    cannot_craft,
    too_many_results,
    too_dark
};

// Checks craftability and lighting for the CONFIRM action.
// Does not check container eligibility.
craft_confirm_result can_start_craft(
    const recipe &rec,
    const availability &avail,
    const Character &crafter,
    int batch_size = 1 );

// Recipe sort comparator for the crafting menu recipe list.
// Comparison order:
//   1. Unread before read (when unread_first is true)
//   2. Craftable before uncraftable
//   3. Higher difficulty first
//   4. Alphabetical by result_name()
//   5. Longer craft time first (tiebreaker)
bool recipe_sort_compare(
    const recipe *a, const recipe *b,
    const availability &avail_a, const availability &avail_b,
    const Character &crafter, const crafting_cost_context &ctx,
    bool a_read, bool b_read,
    bool unread_first );

// Builds the recipe info text for the crafting menu info panel.
// Returns pre-folded lines at the given fold_width.
std::vector<std::string> recipe_info(
    const recipe &recp, const availability &avail,
    Character &guy, std::string_view qry_comps,
    int batch_size, int fold_width, const nc_color &color,
    const std::vector<Character *> &crafting_group );

// Builds the description text for practice recipes.
// Safe to call on non-practice recipes (returns just the base description).
std::string practice_recipe_description( const recipe &recp,
        const Character &crafter );

// Recipe filter engine -- parses comma-separated filter queries with prefix syntax.
// Prefix types: c: (component), t: (tool), s: (skill), p: (primary skill),
// Q: (quality req), q: (quality result), m: (memorized), b: (book source),
// l: (difficulty), r: (byproduct), etc.  Bare text matches result name;
// -prefix excludes by name.  Delegates each sub-query to recipe_subset::reduce().
recipe_subset filter_recipes( const recipe_subset &available_recipes,
                              std::string_view qry, const Character &crafter,
                              const std::function<void( size_t, size_t )> &progress_callback );

// Data for search prefix help display.
struct SearchPrefix {
    char key;
    translation example;
    translation description;
};

extern const std::vector<SearchPrefix> prefixes;
extern const translation filter_help_start;

// Creates a display-ready item from a recipe result.
// Handles VARSIZE/FIT sizing, variant, charges, recipe_exemplar var.
item get_recipe_result_item( const recipe &rec, Character &crafter );

// Generates iteminfo entries for the recipe result panel.
// Covers result item, container (if any), byproducts (if any),
// with section headers and quantity scaling for batch_size.
// When full_info is false, irrelevant sections are filtered based on
// the result item type (e.g. melee stats hidden for armor).
std::vector<iteminfo> recipe_result_info( const recipe &rec, Character &crafter,
        int batch_size, int panel_width, bool full_info = true );

// Return type for build_recipe_list().
struct recipe_list_data {
    std::vector<const recipe *> entries;
    std::vector<int> indent;
    std::vector<availability> available;
    size_t num_hidden = 0;
};

// Processes a raw recipe list from category lookup or filter:
// 1. Filters out hidden recipes (unless skip_hidden_filter is true)
// 2. Caches availability for each recipe in the provided cache
// 3. Sorts by craftability, difficulty, name (skipped when skip_sort is true)
// 4. Expands nested recipe categories
// 5. Builds the parallel availability vector
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
    const recipe_subset &available_recipes );

// --- Recipe info panel ---
// The crafting menu redraws about 100 times a second.  Everything its info panel shows that
// needs the crafter, the inventory or the map is built once per selection into a
// recipe_panel, and the draw code only reads it.

// What building a selection's panel needs from the menu.
struct recipe_panel_context {
    Character &crafter;
    // helpers who know the recipe are listed from this group
    const std::vector<Character *> &crafting_group;
    // a nested recipe group lists its children that are in here
    const recipe_subset &available_recipes;
    std::map<const recipe *, availability> &availability_cache;
    bool camp_crafting = false;
    temp_crafting_inventory *inventory_override = nullptr;
};

// One line of the panel, or one alternative of a group, with its colour.
struct recipe_panel_text {
    std::string text;
    nc_color color;
    bool operator==( const recipe_panel_text &rhs ) const;
};

// A component or tool group, its alternatives in the order shown: the ones at hand first.
struct recipe_panel_group {
    // the menu remembers expanded groups by this: the group's position in its requirement,
    // plus, for the tools of a step, the tool groups of the steps before it
    int index = 0;
    // tools only: "• <requirement name>: " or "• One of: "
    std::string label;
    std::vector<recipe_panel_text> options;
    bool operator==( const recipe_panel_group &rhs ) const;
};

// The "Components:" or "Tools:" block of one requirement.
struct recipe_panel_requirement {
    // the requirement has groups of this kind, so the block and its header are shown
    bool shown = false;
    // tools only: one line per quality group, its alternatives coloured and joined by " OR "
    std::vector<std::string> qualities;
    std::vector<recipe_panel_group> groups;
    bool operator==( const recipe_panel_requirement &rhs ) const;
};

// One step of a recipe with several steps.
struct recipe_panel_step {
    std::string name;
    std::string time;
    // " (xN, saves <time>)", or empty
    std::string batch_note;
    std::string activity;
    nc_color activity_color;
    bool unattended = false;
    std::vector<recipe_panel_text> proficiencies;
    recipe_panel_requirement tools;
    bool operator==( const recipe_panel_step &rhs ) const;
};

// A skill row of the details table.
struct recipe_panel_skill {
    std::string name;
    int level = 0;
    int required = 0;
    // the difficulty range of a practice recipe, or empty
    std::string suffix;
    nc_color color;
    bool operator==( const recipe_panel_skill &rhs ) const;
};

// A proficiency row of the details table.
struct recipe_panel_proficiency {
    std::string name;
    bool required = false;
    // the crafter or a helper has it
    bool known = false;
    float time_multiplier = 1.0f;
    float skill_penalty = 0.0f;
    bool operator==( const recipe_panel_proficiency &rhs ) const;
};

// Everything the info panel shows for one selection (recipe, batch size, crafter).
struct recipe_panel {
    // --- summary ---
    std::string result_name;
    // "as written in <book>", or how sure the crafter is of the recipe
    std::string source_text;
    std::string description;
    // not nested: the "<chance> chance you can make it in <time> of <activity>" line, plain
    // (for measuring) and coloured
    std::string stats_plain;
    std::string stats_colored;
    // not nested: the lighting multiplier is 0
    bool too_dark = false;
    // names of the recipe's required proficiencies
    std::vector<std::string> required_proficiencies;
    bool too_complex = false;
    bool npc_cannot_craft = false;
    std::string npc_reason;
    // the recipe's JSON condition fails: what it says (UIINV-J03)
    std::string condition_failure;
    // batches of more than one only
    std::string batch_savings;

    // --- details table (not nested) ---
    std::string base_time;
    std::vector<recipe_panel_skill> skills;
    std::vector<recipe_panel_proficiency> proficiencies;
    float morale_mult = 1.0f;
    float light_mult = 1.0f;
    float pain_mult = 1.0f;
    float limb_mult = 1.0f;
    float mut_mult = 1.0f;
    float mut_skill_bonus = 0.0f;
    // the primary skill's crafting stat (intelligence by default): its name, value, and what it
    // adds to the effective skill compared to a stat of 8
    std::string crafting_stat_name;
    int intelligence = 0;
    float int_delta = 0.0f;
    bool has_farsight = false;
    float vision_penalty = 0.0f;
    std::string total_time;
    // percent
    float success = 0.0f;
    float destruction = 0.0f;
    std::string darkness;
    // how many of the result are in the crafting inventory already
    int nearby = 0;
    // practice recipes with a skill: below the minimum skill, the skill and the
    // proficiencies it trains
    std::string practice_requirement;
    std::string practice_gains;
    nc_color practice_gains_color;
    std::vector<std::string> practice_trains;

    // --- recipe ---
    std::string single_step_name;
    std::vector<recipe_panel_text> character_requirements;
    recipe_panel_requirement components;
    // the recipe's tools and qualities together
    recipe_panel_requirement tools;
    // recipes with more than one step
    std::vector<recipe_panel_step> steps;
    bool resources_shown = false;
    std::vector<recipe_panel_text> resources;
    // "Known by: ..." when the crafter doesn't know the recipe and a helper does
    std::string known_by;
    // a nested group's children the crafter can see, coloured by availability
    std::vector<recipe_panel_text> nested;

    bool operator==( const recipe_panel &rhs ) const;
};

// Builds the panel of `recp` at `batch_size`.  `avail` is the selection's availability.
recipe_panel build_recipe_panel( const recipe &recp, const availability &avail, int batch_size,
                                 const recipe_panel_context &ctx );

// The panel of the current selection, rebuilt only when something it shows can have changed:
// another recipe, batch size, crafter or inventory, a change in the inventory's contents, game
// time passing (lighting, item ages), the crafter moving or acting, or another language.
class recipe_panel_cache
{
    public:
        const recipe_panel &get( const recipe &recp, const availability &avail, int batch_size,
                                 const recipe_panel_context &ctx );
        // Makes the next get() rebuild.
        void invalidate();
        // How many panels get() has built.
        int builds() const {
            return build_count;
        }
    private:
        struct key_type {
            const recipe *rec = nullptr;
            int batch_size = 0;
            const Character *crafter = nullptr;
            const temp_crafting_inventory *inventory = nullptr;
            uint64_t inventory_generation = 0;
            const temp_crafting_inventory *inventory_override = nullptr;
            uint64_t override_generation = 0;
            time_point turn;
            int moves = 0;
            tripoint_abs_ms pos;
            int lang_version = 0;
            bool operator==( const key_type &rhs ) const;
        };
        static key_type key_for( const recipe &recp, const availability &avail, int batch_size,
                                 const recipe_panel_context &ctx );
        std::optional<key_type> key;
        recipe_panel panel;
        int build_count = 0;
};

// Generates a colored hierarchical text description of nested recipe contents.
// Used by the item info panel when a nested category is selected.
std::string list_nested( Character &crafter, const recipe *rec,
                         const recipe_subset &available_recipes,
                         int indent_level = 0 );

// Ceil-rounded human-readable craft time string.  Input is game turns (seconds).
std::string approx_craft_time( int turns );

// Activity level color: green (none/light), gray (moderate), yellow (brisk/active), red (extreme).
nc_color activity_level_color( float exertion );

// Returns display name for a tool group if it matches a named requirement,
// or empty string if no match.  Lazily builds a cache on first call.
const std::string &find_tool_group_name( const std::vector<tool_comp> &alts );

#endif // CATA_SRC_CRAFTING_GUI_HELPERS_H

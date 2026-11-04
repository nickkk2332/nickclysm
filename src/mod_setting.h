#pragma once
#ifndef CATA_SRC_MOD_SETTING_H
#define CATA_SRC_MOD_SETTING_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cata_path.h"
#include "mod_setting_values.h"
#include "options.h"
#include "string_id.h"
#include "translation.h"
#include "type_id.h"

class JsonObject;
struct WORLD;
struct mod_setting;

using mod_setting_id = string_id<mod_setting>;

/** A setting a mod offers players, kept per world (doc/JSON/MOD_SETTINGS.md) */
struct mod_setting {
    enum class kind_t : int {
        boolean,
        number,
        choice,
        text,
    };
    struct choice_t {
        std::string id;
        translation name;
    };
    // The longest a text setting can be, in characters
    static constexpr int max_text_length = 200;

    mod_setting_id id;
    std::vector<std::pair<mod_setting_id, mod_id>> src;
    bool was_loaded = false;

    translation name;
    translation description;
    // The sub-heading inside the mod's section; empty for none
    translation group;
    kind_t kind = kind_t::boolean;
    mod_setting_value default_value = false;
    // number
    double min = 0.0;
    double max = 1.0;
    double step = 1.0;
    // choice
    std::vector<choice_t> choices;
    // text
    int max_length = max_text_length;
    // Whether the in-game options menu can change it; otherwise it's fixed once the world exists
    bool changeable_in_game = true;
    // Whether EOCs can change it
    bool eoc_writable = false;

    void load( const JsonObject &jo, std::string_view src );
    void check() const;

    /** The mod that first defined the setting */
    mod_id owner() const;
    /** "bool", "number", "choice" or "text" */
    std::string kind_name() const;
    /** The index of the choice @p choice_id, or -1 */
    int choice_index( std::string_view choice_id ) const;
    /**
     * @p value as the setting stores it: a number clamped to the range and snapped to the step,
     * text kept to one line and to max_length characters.  nullopt when it can't be: a value of
     * another kind, or an unknown choice; @p error then says why.
     */
    std::optional<mod_setting_value> validate( const mod_setting_value &value,
            std::string *error = nullptr ) const;
    /** @p value as reading it gives: validated, or the default when it isn't valid */
    mod_setting_value sanitize( const mod_setting_value &value ) const;
    bool is_default( const mod_setting_value &value ) const;
    /** The value as math reads it: a switch 0 or 1, a choice its index; nullopt for text */
    std::optional<double> as_number( const mod_setting_value &value ) const;
    /** The value as text: "true" or "false", the number, the choice id, or the text */
    std::string as_text( const mod_setting_value &value ) const;
    /**
     * @p value one step on, as the menus' arrow keys move it, @p direction 1 or -1: a switch
     * flipped, a choice the next or previous one, a number one step from its value, past either
     * end to the other.  Text stays.
     */
    mod_setting_value stepped( const mod_setting_value &value, int direction ) const;
};

namespace mod_settings
{
void load( const JsonObject &jo, const std::string &src );
void finalize_all();
void check_consistency();
void reset();
const std::vector<mod_setting> &get_all();

/** The value of @p setting in the active world; its default when no world is active */
mod_setting_value get( const mod_setting &setting );
/**
 * Sets @p setting in the active world, validated first (mod_setting::validate).  Returns why it
 * couldn't, changing nothing; "" when it's set.  A change of value sends mod_setting_changed.
 */
std::string set( const mod_setting &setting, const mod_setting_value &value );
/**
 * Whether a setting of the active world changed in game (options menu, EOC) since the game was
 * last saved.  The game save writes them (save_with_game); until then the world's options file
 * keeps the values of the last save, whatever else saves it.
 */
bool have_unsaved_changes();
void mark_saved();
/** The values WORLD::save writes for @p world: for the active world, those of the last game save */
const mod_setting_values &values_to_save( const WORLD &world );
/** Writes the active world's options with its current values; false when that failed */
bool save_with_game();

/** The settings of one mod, and of one group in it, as the menus show them */
struct section {
    mod_id mod;
    translation group;
    std::vector<const mod_setting *> settings;

    /** The mod's name, then the group's */
    std::string title() const;
};
/**
 * @p settings in sections: one per mod, in the order the mods first appear, split by group in
 * the order the groups first appear; settings without a group come first.
 */
std::vector<section> make_sections( const std::vector<const mod_setting *> &settings );
} // namespace mod_settings

/** What a mod settings menu shows and changes */
class mod_settings_editor
{
    public:
        /** The loaded settings with the active world's values, for the in-game menu */
        mod_settings_editor();
        /** @p settings with the values in @p values, for world creation */
        mod_settings_editor( const std::vector<const mod_setting *> &settings,
                             mod_setting_values &values );

        const std::vector<mod_settings::section> &sections() const {
            return sections_;
        }
        bool empty() const {
            return sections_.empty();
        }
        bool in_game() const {
            return values_ == nullptr;
        }
        /** Why @p setting can't be changed in this menu, translated; "" when it can */
        std::string locked_reason( const mod_setting &setting ) const;
        mod_setting_value get( const mod_setting &setting ) const;
        /**
         * Stores @p value for @p setting, validated first; returns why it couldn't, or "".  In
         * game, a change sends mod_setting_changed.
         */
        std::string set( const mod_setting &setting, const mod_setting_value &value );
    private:
        std::vector<mod_settings::section> sections_;
        // nullptr: the active world's
        mod_setting_values *values_ = nullptr;
};

/**
 * What the mods picked for a new world define that world creation shows: their mod settings,
 * option sliders and WORLD_OPTION defaults.  Read from the files the loader would read for those
 * mods, without loading them.
 */
struct mod_settings_preview {
    std::vector<mod_setting> settings;
    std::vector<option_slider> sliders;
    // WORLD_OPTION: world option name to default, as cOpt::setValue( std::string ) takes it
    std::map<std::string, std::string> world_option_defaults;
    // Problems found in those objects
    std::vector<std::string> errors;

    const mod_setting *find( std::string_view id ) const;
    std::vector<const mod_setting *> all_settings() const;
};

/**
 * The world options a player chose while creating a world, which WORLD_OPTION defaults leave
 * alone.  An option counts as chosen when the player changed it during world creation (World
 * Options tab, slider), or when world creation started with a value of its own: for a new world,
 * one the main menu's World Defaults set apart from the built-in default; for a copy, one that
 * differs from what the copied world's mods give it.
 */
class world_option_choices
{
    public:
        explicit world_option_choices( bool copy = false ) : copy( copy ) {}
        /**
         * Gives @p options the defaults @p defaults has for them (option name to a value as
         * cOpt::setValue( std::string ) takes it), or else the built-in ones, and moves the
         * options not chosen to them.
         */
        void apply( options_manager::options_container &options,
                    const std::map<std::string, std::string> &defaults );
        bool chosen( const std::string &option ) const {
            return chosen_.count( option ) > 0;
        }
    private:
        bool copy;
        bool started = false;
        std::set<std::string> chosen_;
        // the value world creation last gave each option
        options_manager::options_container given;
};

/**
 * Reads the mod_setting, option_slider and WORLD_OPTION objects of mods, for world creation.
 * Walks the files the loader would load for those mods, but parses only the ones that mention
 * one of those types.  Which files those are is remembered for the whole game (by path, time and
 * size), and which belong to each mod for the reader's life: reading again costs little.
 */
class mod_settings_reader
{
    public:
        mod_settings_preview read( const std::vector<mod_id> &mods );
        // the world options chosen in the world creation this reader serves
        world_option_choices option_choices;
    private:
        struct mod_files {
            std::vector<cata_path> data;
            // mod_interactions files: the mod each is for, and the file
            std::vector<std::pair<mod_id, cata_path>> interactions;
        };
        const mod_files &files_of( const mod_id &mod );
        std::map<mod_id, mod_files> scanned;
};

/** The settings of @p mods, read once (mod_settings_reader) */
mod_settings_preview pre_read_mod_settings( const std::vector<mod_id> &mods );

/**
 * Brings a world being created in line with a new mod list: drops the values of the settings
 * @p before has and @p after doesn't (their mod was deselected), keeps the others, and gives the
 * world options the WORLD_OPTION defaults of @p after, leaving those @p choices says the player
 * chose (without @p choices, those that differ from their default).
 */
void update_world_mod_settings( WORLD &world, const mod_settings_preview &before,
                                const mod_settings_preview &after, world_option_choices *choices = nullptr );

/** Why world creation can't show @p slider (too few levels, levels that don't check out); "" */
std::string world_creation_slider_error( const option_slider &slider );

/**
 * The option sliders world creation shows for @p context: core's, with those @p preview's mods
 * change or add.
 */
std::vector<const option_slider *> preview_sliders( const mod_settings_preview &preview,
        const std::string &context );

/**
 * Reads a WORLD_OPTION object, { "name", "default" }, into the option name and the default as
 * cOpt::setValue( std::string ) takes it.  Throws JsonError when it isn't valid.
 */
std::pair<std::string, std::string> read_world_option( const JsonObject &jo );

#endif // CATA_SRC_MOD_SETTING_H

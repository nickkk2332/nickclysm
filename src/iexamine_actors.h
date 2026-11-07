#pragma once
#ifndef CATA_SRC_IEXAMINE_ACTORS_H
#define CATA_SRC_IEXAMINE_ACTORS_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <functional>

#include "coords_fwd.h"
#include "enums.h"
#include "iexamine.h"
#include "dialogue_helpers.h"
#include "type_id.h"
#include "translation.h"
#include "units_fwd.h"

class Character;
class JsonValue;
class item;
class item_location;
class time_duration;

struct const_dialogue;
struct dialogue;
struct furn_t;

class appliance_convert_examine_actor : public iexamine_actor
{
    private:
        std::optional<furn_str_id> furn_set = std::nullopt;
        std::optional<ter_str_id> ter_set = std::nullopt;
        itype_id appliance_item;

    public:
        explicit appliance_convert_examine_actor( const std::string &type = "appliance_convert" )
            : iexamine_actor( type ) {}

        void load( const JsonObject &jo, const std::string & ) override;
        void call( Character &you, const tripoint_bub_ms &examp ) const override;
        void finalize() const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

class cardreader_examine_actor : public iexamine_actor
{
    private:
        std::vector<flag_id> allowed_flags;
        bool consume = true;
        bool allow_hacking = true;
        bool despawn_monsters = true;

        // Option 1: walk the map, do some stuff
        int radius = 3;
        std::map<ter_str_id, ter_str_id> terrain_changes;
        std::map<furn_str_id, furn_str_id> furn_changes;
        // Option 2: Regenerate entire current overmap tile
        update_mapgen_id mapgen_id;

        std::optional<int> omt_allowed_radius;

        bool map_regen = false;

        bool query = true;
        std::string query_msg;

        std::string success_msg;
        std::string redundant_msg;

        void consume_card( const std::vector<item_location> &cards ) const;
        std::vector<item_location> get_cards( Character &you, const tripoint_bub_ms &examp ) const;
        bool apply( const tripoint_bub_ms &examp ) const;

    public:
        explicit cardreader_examine_actor( const std::string &type = "cardreader" )
            : iexamine_actor( type ) {}

        void load( const JsonObject &jo, const std::string & ) override;
        void call( Character &you, const tripoint_bub_ms &examp ) const override;
        void finalize() const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

class eoc_examine_actor : public iexamine_actor
{
    private:
        std::vector<effect_on_condition_id> eocs;
    public:
        explicit eoc_examine_actor( const std::string &type = "effect_on_condition" )
            : iexamine_actor( type ) {}

        void load( const JsonObject &jo, const std::string &src ) override;
        void call( Character &you, const tripoint_bub_ms &examp ) const override;
        void call_with_context( Character &you, const tripoint_bub_ms &examp,
                                const map_data_common_t &data ) const override;
        void finalize() const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

// Which items a process takes. An item matches when it matches one of the lists given (any item
// when none is given), and is not rotten / is a CBM where `fresh` / `bionic` ask for it.
struct process_item_filter {
    std::set<material_id> materials;
    std::vector<flag_id> flags;
    std::vector<itype_id> items;
    bool fresh = false;
    bool bionic = false;

    bool matches( const item &it ) const;
    bool empty() const;
    void deserialize( const JsonObject &jo );
    void check( const std::string &owner ) const;
};

// A message for the log, of a kind ("good", "bad"...).
struct process_message {
    translation text;
    game_message_type type = m_neutral;

    process_message() = default;
    explicit process_message( game_message_type type ) : type( type ) {}

    // The text is a format string: the arguments fill its %s, %d...
    void print() const;
    void print( const std::string &arg ) const;
    void print( int arg ) const;
    void print( const std::string &arg1, const std::string &arg2 ) const;
    void deserialize( const JsonValue &jv );
};

// How a process says how long it still needs: the exact time in one message, or about how
// many hours or minutes.
struct process_time_message {
    // With %s for the time left.
    translation exact;
    // Plural forms with %d: they must be plural translations, or "str_pl" is a load error.
    translation hours{ translation::plural_tag() };
    translation less_than_an_hour;
    translation minutes{ translation::plural_tag() };

    void print( const time_duration &left ) const;
    void deserialize( const JsonValue &jv );
};

// Starts a batch process: checks the items on the tile, turns them into an intermediate item
// or leaves them to age, and switches the furniture to its working state.
class process_start_examine_actor : public iexamine_actor
{
    private:
        process_item_filter accepts;
        process_item_filter reject_if_contains;
        std::vector<process_message> contains_message;
        process_message reject_message = process_message( m_bad );
        process_message empty_message;
        bool requires_outside = false;
        process_message outside_message;
        std::optional<skill_id> skill;
        dbl_or_var yield_percent;
        itype_id intermediate = itype_id::NULL_ID();
        process_message too_small_message;
        std::optional<dbl_or_var> energy;
        process_message no_energy_message;
        std::optional<requirement_id> requirements;
        bool firestarter = false;
        process_message ready_message;
        translation query;
        bool reset_age = false;
        process_message success_message;
        furn_str_id furn_set;
        std::vector<effect_on_condition_id> eocs;

        dialogue make_dialogue( Character &you, const tripoint_bub_ms &examp,
                                const units::volume &total ) const;
        bool begin( Character &you, const tripoint_bub_ms &examp, bool ask ) const;

    public:
        explicit process_start_examine_actor( const std::string &type = "process_start" )
            : iexamine_actor( type ) {}

        // Checks the items on the tile and the place, and says what's wrong.
        bool prep( Character &you, const tripoint_bub_ms &examp ) const;
        // Starts the process without asking; what a lit firestarter does.
        bool fire( Character &you, const tripoint_bub_ms &examp ) const;
        bool needs_firestarter() const {
            return firestarter;
        }
        // The process_start action of the furniture that is lit with a firestarter, if any.
        static const process_start_examine_actor *firestarter_process( const furn_t &furn );

        void load( const JsonObject &jo, const std::string &src ) override;
        void call( Character &you, const tripoint_bub_ms &examp ) const override;
        void finalize() const override;
        void check( const std::string &owner ) const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

// Finishes a batch process once its time is up: turns the contents into the output and switches
// the furniture back.
class process_finish_examine_actor : public iexamine_actor
{
    private:
        duration_or_var duration;
        furn_str_id furn_set;
        process_message empty_message;
        process_item_filter accepts;
        process_message reject_message = process_message( m_bad );
        process_message examine_message;
        process_time_message time_left_message;
        itype_id output = itype_id::NULL_ID();
        std::vector<itype_id> output_from;
        bool convert_to_smoking_result = false;
        bool sterilize_cbms = false;
        process_message done_message;
        std::vector<effect_on_condition_id> eocs;

    public:
        explicit process_finish_examine_actor( const std::string &type = "process_finish" )
            : iexamine_actor( type ) {}

        void load( const JsonObject &jo, const std::string &src ) override;
        void call( Character &you, const tripoint_bub_ms &examp ) const override;
        void finalize() const override;
        void check( const std::string &owner ) const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

class mortar_examine_actor : public iexamine_actor
{
    private:
        std::vector<ammotype> ammo_type;
        int range;
        std::function<bool( const_dialogue const & )> condition;
        bool has_condition = false;
        translation condition_fail_msg;
        dbl_or_var aim_deviation;
        duration_or_var aim_duration;
        duration_or_var flight_time;
        std::vector<effect_on_condition_id> eocs;

    public:
        explicit mortar_examine_actor( const std::string &type = "mortar" ): iexamine_actor( type ) {}

        std::vector<ammotype> get_ammotypes() const;
        void load( const JsonObject &jo, const std::string &src ) override;
        void call( Character &you, const tripoint_bub_ms &examp ) const override;
        void finalize() const override;

        std::unique_ptr<iexamine_actor> clone() const override;
};

#endif // CATA_SRC_IEXAMINE_ACTORS_H

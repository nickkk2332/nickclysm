#pragma once

#include <optional>
#include <string>
#include <vector>

#include "calendar.h"
#include "item_location.h"
#include "map.h"
#include "type_id.h"

class Character;
class harvest_drop_type;
class item;
class player_activity;
struct mtype;

enum class butcher_type : int {
    BLEED,          // bleeding a corpse
    QUICK,          // quick butchery
    FULL,           // full workshop butchery
    FIELD_DRESS,    // field dressing a corpse
    SKIN,           // skinning a corpse
    QUARTER,        // quarter a corpse
    DISMEMBER,      // destroy a corpse
    DISSECT,        // dissect a corpse for CBMs
    NUM_TYPES       // always keep at the end, number of butchery types
};

struct butchery_data {
    item_location corpse;
    requirement_id req;
    butcher_type b_type;
    float req_speed_bonus = 0.0f;
    time_duration time_to_butcher;
    // when `progress` reaches `time_to_butcher`, the corpse is butchered
    time_duration progress;

    butchery_data() = default;
    butchery_data( item_location &corpse, const butcher_type &b_type ) : corpse( corpse ),
        b_type( b_type ) {}
};

int butcher_time_to_cut( Character &you, const item &corpse_item, butcher_type action );

std::string butcher_progress_var( butcher_type action );

std::string butcher_progress_time_var();

double butcher_get_progress( const item &corpse_item, butcher_type action );

butcher_type get_butcher_type( player_activity *act );

bool set_up_butchery( player_activity &act, Character &you, butchery_data bd );

int roll_butchery_dissect( int skill_level, int dex, int tool_quality );

// The skill level of `you` for drops of type `htype` (the average of its harvest skills,
// survival when there's none), with `tool_quality` added in for DISSECT only.
int butchery_dissect_skill_level( const Character &you, int tool_quality,
                                  const harvest_drop_type_id &htype, butcher_type action );

// What `roll` of a harvest entry of drop type `type` comes to after the drop type's rules for
// the action and the corpse's state; nullopt when the action leaves the entry alone.
std::optional<int> butchery_entry_yield( const harvest_drop_type &type, int roll,
        butcher_type action, const item &corpse_item, const mtype &mt, const Character &you );

// Is there something for field dressing, skinning or bleeding (as its drop types say) left on
// the corpse? Always true for the other actions.
bool butchery_has_target( const item &corpse_item, butcher_type action );

// Does quartering this creature need field dressing first?
bool butchery_needs_field_dressing( const mtype &mt );

// Practices the proficiencies the drop types of the creature train for the action.
void butchery_train_proficiencies( Character &you, const mtype &mt, butcher_type action,
                                   const time_duration &moves_total );

// Returns false if the calling function should abort
bool butchery_drops_harvest( butchery_data bt, Character &you );

void butchery_quarter( item *corpse_item, const Character &you );

void destroy_the_carcass( const butchery_data &bd, Character &you );

std::optional<butcher_type> butcher_submenu( const std::vector<map_stack::iterator> &corpses,
        int index = -1 );

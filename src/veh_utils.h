#pragma once
#ifndef CATA_SRC_VEH_UTILS_H
#define CATA_SRC_VEH_UTILS_H

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "color.h"
#include "coordinates.h"
#include "point.h"
#include "type_id.h"

class Character;
class map;
namespace cata
{
class event;
}  // namespace cata
class temp_crafting_inventory;
class time_duration;
class vehicle;
class vpart_info;
struct uilist_entry;
struct vehicle_part;

namespace veh_utils
{
/**
 * XP @p who earns in @p sk for an action on a vehicle part that needs @p skills (the part's
 * install, repair or removal skills) and takes @p time (unscaled by skill): none unless @p sk is
 * one of @p skills; 60 an hour up to one level above the requirement, falling with the square
 * of the levels above it.
 */
int calc_xp_gain( const std::map<skill_id, int> &skills, const time_duration &time,
                  const skill_id &sk, const Character &who );
/**
 * Whether a vehicle's NO_MODIFY_VEHICLE parts forbid players to install, replace, repair or
 * remove @p vpi on it (a SIMPLE_PART stays allowed).
 */
bool modification_forbidden( const vehicle &veh, const vpart_info &vpi );
/**
 * Why a player can't install @p vpi on @p veh, or replace a broken one with a new one: the
 * vehicle can't be modified (see modification_forbidden), or @p vpi is NO_INSTALL_PLAYER.
 * Empty when they can.
 */
std::string install_forbidden_reason( const vehicle &veh, const vpart_info &vpi );
/**
 * @return a pointer to vehicle_part repairable by \p who_arg
 * If no such part exists, returns a pointer to vehicle_part replaceable by \p whoarg
 * If no such part exists, returns a nullptr.
 */
vehicle_part *most_repairable_part( vehicle &veh, Character &who_arg );
/**
 * Repairs a given part on a given vehicle by given character.
 * Awards xp and consumes components.
 */
bool repair_part( map &here, vehicle &veh, vehicle_part &pt, Character &who );
/**
 * What @p who repairs a part with from @p pos: what they carry, and what lies within @p radius
 * of @p pos, along a clear path unless @p who is an NPC (which may use a welding rig it can't
 * see). repair_part() repairs with this from where @p who stands, within PICKUP_RANGE.
 */
const temp_crafting_inventory &repair_inventory( const Character &who, const tripoint_bub_ms &pos,
        int radius );

/**
 * Sends @p e (a vehicle event, VINT-J06) with @p veh as the beta talker; its EOCs' alpha is the
 * character @p e names, or the avatar for an event that names none.
 */
void send_vehicle_event( vehicle &veh, const cata::event &e );

/** Whether @p who could install @p vpart somewhere on @p veh, checked against @p inv. */
bool can_install_anywhere( const Character &who, const temp_crafting_inventory &inv,
                           const vehicle &veh, const vpart_info &vpart );

/** Parts the install menu may offer on @p veh, before the tile under the cursor filters them. */
struct install_candidates {
    /** Parts @p who could install, by localized name, followed by the rest, by localized name. */
    std::vector<const vpart_info *> parts;
    /** The members of @ref parts that @p who could install. */
    std::unordered_set<const vpart_info *> installable;
};
/** Lists every part the install menu may offer, checking each once against @p inv. */
install_candidates list_install_candidates( const Character &who,
        const temp_crafting_inventory &inv, const vehicle &veh );
} // namespace veh_utils

struct veh_menu_item {
    std::string _text;
    std::string _desc;
    int _symbol = 0;
    nc_color _symbol_color = get_all_colors().get( def_c_light_gray );
    nc_color _text_color = get_all_colors().get( def_c_light_gray );
    std::optional<tripoint> _location = std::nullopt;
    bool _enabled = true;
    bool _selected = true;
    bool _check_theft = true;
    bool _check_locked = true;
    bool _keep_menu_open = false;
    std::optional<char> _hotkey_char = std::nullopt;
    std::optional<std::string> _hotkey_action = std::nullopt;
    std::function<void()> _on_submit;
    std::function<void()> _on_select;

    veh_menu_item &text( const std::string &text );
    veh_menu_item &text_color( nc_color text_color );
    veh_menu_item &desc( const std::string &desc );
    veh_menu_item &symbol( int symbol );
    veh_menu_item &symbol_color( nc_color symbol_color );
    veh_menu_item &enable( bool enable );
    veh_menu_item &select( bool select );
    veh_menu_item &skip_theft_check( bool skip_theft_check = true );
    veh_menu_item &skip_locked_check( bool skip_locked_check = true );
    veh_menu_item &hotkey( char hotkey_char );
    veh_menu_item &hotkey( const std::string &action );
    veh_menu_item &hotkey_auto();
    veh_menu_item &on_submit( const std::function<void()> &on_submit );
    veh_menu_item &on_select( const std::function<void()> &on_select );
    veh_menu_item &keep_menu_open( bool keep_menu_open = true );
    veh_menu_item &location( const std::optional<tripoint> &location );
};

class veh_menu
{
    public:
        veh_menu( vehicle &veh, const std::string &title );
        veh_menu( vehicle *veh, const std::string &title );
        veh_menu_item &add( const std::string &txt );
        void reset( bool keep_last_selected = true );
        bool query();

        size_t get_items_size() const;
        std::vector<veh_menu_item> get_items() const;
        void sort( const std::function<int( const veh_menu_item &a, const veh_menu_item &b )> &comparer );

        int desc_lines_hint = 0;

        // don't allow allocating on heap
        static void *operator new( size_t ) = delete;
        static void *operator new[]( size_t ) = delete;
        static void  operator delete( void * )  = delete;
        static void  operator delete[]( void * )  = delete;

    private:
        std::vector<veh_menu_item> items;
        std::string title;
        vehicle &veh;

        std::vector<uilist_entry> get_uilist_entries() const;
        std::vector<tripoint> get_locations() const;

        std::optional<int> last_selected = std::nullopt;
};

#endif // CATA_SRC_VEH_UTILS_H

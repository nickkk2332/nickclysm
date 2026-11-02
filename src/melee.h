#pragma once
#ifndef CATA_SRC_MELEE_H
#define CATA_SRC_MELEE_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "coords_fwd.h"
#include "type_id.h"

class Character;
class Creature;
class item;
class item_location;
enum class technique_rejection : int;

/*
 * statistics data for melee attacks, used for test purposes
 */
struct melee_statistic_data {
    int attack_count = 0;
    int hit_count = 0;
    int double_crit_count = 0;
    int crit_count = 0;
    int actual_crit_count = 0;
    double double_crit_chance = 0.0;
    double crit_chance = 0.0;
    int damage_amount = 0;
};

/** How a melee attack picks its technique */
struct technique_choice {
    enum class mode : int {
        // The attack is called off (the technique menu was cancelled)
        cancel,
        // Chosen at random after the hit and crit rolls, as always
        automatic,
        // No martial-arts technique: a plain hit with the weapon, or unarmed, a basic punch, kick
        // or headbutt (the techniques marked "fallback") chosen as usual
        basic,
        // This technique, whatever the hit (EOC u_attack, counters, whip disarm)
        forced,
        // Picked in the technique menu: one that needs a crit becomes a basic attack without one
        chosen,
    };
    mode how = mode::automatic;
    matec_id tec;

    technique_choice() = default;
    explicit technique_choice( mode how, const matec_id &tec = matec_id() ) : how( how ),
        tec( tec ) {}
    // The force_technique of an attack: an empty id means an automatic choice
    static technique_choice from_forced( const matec_id &force_technique );
};

namespace melee
{

float melee_hit_range( float accuracy );
melee_statistic_data get_stats();
void clear_stats();
int blocking_ability( const item &shield );
extern melee_statistic_data melee_stats;

/** Where a line of the manual technique menu comes from; the menu's groups, in order */
enum class technique_menu_group : int {
    style,             // the selected martial art
    weapon_and_style,  // both the weapon and the martial art
    weapon,            // the wielded weapon
    equipment,         // a worn item
    unarmed,           // the body: basic punches and kicks, natural weapons
    mutation,          // mutation attacks, which happen on their own
};

/** A technique (or a mutation attack) in the manual technique menu */
struct technique_menu_entry {
    // tec_none for a mutation attack
    matec_id tec;
    std::string name;
    technique_menu_group group = technique_menu_group::style;
    // Why it can't be chosen now; `none` when it can (and for mutation attacks)
    technique_rejection why_not{};
    bool selectable = false;
    // Selectable, but it needs a critical hit: without one the attack is a basic attack
    bool crit_only = false;
    // Second column: why it can't be chosen, or that it needs a crit
    std::string reason;
    // Description panel
    std::string description;
};

/** What the technique menu offers besides the techniques */
struct technique_menu_options {
    // Only these of the attacker's techniques; null: all of them
    const std::vector<matec_id> *only = nullptr;
    bool include_basic = true;
    bool include_automatic = true;
};

/**
 * Does this attack open the manual technique menu? Only for the avatar in manual combat mode,
 * when the attack may use techniques, outside auto-move and any technique_menu_suppressor.
 */
bool wants_technique_menu( const Character &you, bool allow_special );

/** The technique menu's lines for an attack on target, in the order shown. Rolls nothing. */
std::vector<technique_menu_entry> technique_menu_entries( const Character &you,
        const Creature &target, const item_location &weapon,
        const std::vector<matec_id> *only = nullptr );

/** Shows the technique menu. Spends no moves and rolls nothing. */
technique_choice choose_technique( const Character &you, const Creature &target,
                                   const item_location &weapon,
                                   const technique_menu_options &options = {} );

/**
 * While one exists, no attack opens the technique menu: attacks the player didn't aim one by
 * one (autoattack, EOC attacks), and the attack of a choice already made.
 */
class technique_menu_suppressor
{
    public:
        technique_menu_suppressor();
        ~technique_menu_suppressor();
        technique_menu_suppressor( const technique_menu_suppressor & ) = delete;
        technique_menu_suppressor &operator=( const technique_menu_suppressor & ) = delete;
};
bool technique_menu_suppressed();

/**
 * Tests can't show menus: in test mode the technique menu returns this function's answer to its
 * lines, or cancels when it is unset.
 */
extern std::function<technique_choice( const std::vector<technique_menu_entry> & )>
technique_menu_test_answer;

/**
 * Why `attacker`'s technique can't knock `target` back toward `dest`; nullopt if it can. A
 * knockback goes across (not up or down), and never through the attacker.
 */
std::optional<std::string> knockback_aim_refusal( const Character &attacker,
        const Creature &target, const tripoint_bub_ms &dest );

/**
 * Tests can't aim: in test mode aiming a controlled knockback returns this function's answer
 * for the target and the technique's knockback distance (nullopt: cancelled), or cancels when
 * it is unset.
 */
extern std::function<std::optional<tripoint_bub_ms>( const Creature &, int )>
knockback_aim_test_answer;

} // namespace melee

#endif // CATA_SRC_MELEE_H

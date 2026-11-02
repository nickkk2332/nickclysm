#pragma once
#ifndef CATA_SRC_IEXAMINE_EOC_H
#define CATA_SRC_IEXAMINE_EOC_H

#include <optional>
#include <string>

#include "coords_fwd.h"

class Character;

// Examining a tile from an EOC (u_examine / npc_examine).
namespace iexamine
{

/**
 * Examines the tile at @p pos as @p who would, for u_examine. The tile must be within reach
 * (@p who's own tile or next to it).
 * - A door or a gate (anything with an open or close transform) opens if it is closed and
 *   closes if it is open, for the avatar and NPCs alike.
 * - Anything else runs the normal examine for the avatar, without picking items up.
 * - An NPC may only use what needs no menu: an examine action that runs EOCs, and the built-in
 *   actions listed in npc_examine_actions.
 * @returns why it didn't happen (nothing was done then), or nullopt
 */
std::optional<std::string> examine_from_eoc( Character &who, const tripoint_bub_ms &pos );

} // namespace iexamine

#endif // CATA_SRC_IEXAMINE_EOC_H

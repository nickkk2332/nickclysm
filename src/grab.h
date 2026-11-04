#pragma once
#ifndef CATA_SRC_GRAB_H
#define CATA_SRC_GRAB_H

#include "coordinates.h"

class avatar;
class map;

// Moves the vehicle the avatar holds to the level the avatar has just reached by stairs, by
// the rules of dragging it. dp takes the vehicle from where it is to the tile beside the avatar
// that the player picked. Returns true if it moved there.
bool drag_vehicle_across_stairs( map &here, avatar &you, const tripoint_rel_ms &dp );

#endif // CATA_SRC_GRAB_H

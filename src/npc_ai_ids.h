#pragma once
#ifndef CATA_SRC_NPC_AI_IDS_H
#define CATA_SRC_NPC_AI_IDS_H

#include <optional>
#include <string>
#include <string_view>

enum npc_attitude : int;
enum npc_mission : int;

// The ids EOCs and NPC templates name attitudes and missions by: "NPCATT_KILL" (as
// npc_attitude_id() gives them) and "GUARD".  An unknown id, or a legacy attitude's, gives
// nullopt.
std::optional<npc_attitude> npc_attitude_from_id( std::string_view id );
std::string npc_mission_id( npc_mission mission );
std::optional<npc_mission> npc_mission_from_id( std::string_view id );

#endif // CATA_SRC_NPC_AI_IDS_H

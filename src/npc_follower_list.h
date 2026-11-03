#pragma once
#ifndef CATA_SRC_NPC_FOLLOWER_LIST_H
#define CATA_SRC_NPC_FOLLOWER_LIST_H

class character_id;

// Changes to the avatar's follower list that recruit or dismiss no one, so send neither
// npc_recruited nor npc_dismissed: putting the list back together on load, swapping bodies with
// a follower.  game::add_npc_follower() and remove_npc_follower() send them.
void add_npc_follower_quietly( const character_id &id );
void remove_npc_follower_quietly( const character_id &id );

#endif // CATA_SRC_NPC_FOLLOWER_LIST_H

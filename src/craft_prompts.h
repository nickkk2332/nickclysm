#pragma once
#ifndef CATA_SRC_CRAFT_PROMPTS_H
#define CATA_SRC_CRAFT_PROMPTS_H

/**
 * While one exists, starting a craft or a disassembly, or going on with a craft in progress, asks
 * the player nothing, as when an EOC starts it (u_assign_activity, which also refuses a craft the
 * crafter can't start before this runs):
 * - components, tools and requirement alternatives are chosen as for an NPC;
 * - rotten components may be used, favorited ones never are, and going on with a craft uses up
 *   the components it is missing;
 * - the avatar's craft doesn't start when a component container still holds liquid;
 * - a step recipe gets its default plan;
 * - a craft the avatar can't hold without dropping its weapon is put down;
 * - an NPC works on the craft itself instead of passing it to the crafting zone job;
 * - neither the player's recraft recipe nor their recent recipes change.
 */
class craft_prompt_suppressor
{
    public:
        craft_prompt_suppressor();
        ~craft_prompt_suppressor();
        craft_prompt_suppressor( const craft_prompt_suppressor & ) = delete;
        craft_prompt_suppressor &operator=( const craft_prompt_suppressor & ) = delete;
};
bool craft_prompts_suppressed();

#endif // CATA_SRC_CRAFT_PROMPTS_H

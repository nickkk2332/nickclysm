#pragma once
#ifndef CATA_SRC_MOD_KEYBINDING_H
#define CATA_SRC_MOD_KEYBINDING_H

#include <string>

class Character;
class JsonObject;
class input_context;

// Hotkeys mods add to the main game with "type": "keybinding" objects: pressing one runs an
// effect_on_condition (doc/JSON/KEYBINDINGS.md). Their default keys go below the player's own
// keybindings, so a key the player rebinds stays theirs.
namespace mod_keybindings
{
void load( const JsonObject &jo, const std::string &src );
// Adds the loaded hotkeys to the default keybindings
void finalize();
void check();
// Takes the hotkeys of the unloaded mods out of the keybindings again
void reset();

// Lets the main game's input context (DEFAULTMODE) report the hotkeys
void register_actions( input_context &ctxt );
// Runs the effect_on_condition of the hotkey `action`, if it names one (its condition permitting);
// false if no mod defines that hotkey
bool run( const std::string &action, Character &you );
} // namespace mod_keybindings

#endif // CATA_SRC_MOD_KEYBINDING_H

# 01 · Effect-on-condition (EOC) effects, conditions & dialogue

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/effect_on_condition.cpp`, `src/condition.cpp`, `src/npctalk.cpp`, `src/npctalk_funcs.cpp`, `src/dialogue_helpers.cpp`, `src/dialogue_chatbin.cpp`, `src/dialogue_imgui.cpp`, `src/npctalk_rules.cpp`, `src/dialogue.h`, `src/global_vars.h`, `src/talker.h`

## Summary

The EOC/dialogue layer is broad, and most effects work for the common case: an avatar or NPC talker, constant arguments, content that follows the docs. The recurring weakness is silent failure. Unknown condition strings, foreach types, mod keys and topic ids load without error. Many effects dereference talker sub-objects without null checks. Several documented fields (var_val `g_`/`v_`, `max_mass`, body-part type filters, `failure_topic`, `set_fault` messages) do nothing or do the wrong thing. On the modding side, the main gaps are entity handles (spawned monsters, items and vehicles cannot be addressed afterwards), queued EOCs losing their beta and non-character alphas, and append-only talk topics. Performance is acceptable overall. The one medium item is the per-activation dialogue copying and the string-based callstack, which is also the root of the broken infinite-loop guard.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 1 | 12 | 6 | 19 |
| Bugs & quirks | 1 | 4 | 10 | 5 | 20 |
| Performance | 0 | 0 | 1 | 10 | 11 |
| **Total** | **1** | **5** | **23** | **21** | **50** |

**Fix first:**
1. **EOC-B01** · `u_spawn_monster ""` null-derefs on a hostile NPC: vanilla hallucination content can crash the game, and the fix is a one-line `is_monster()` check.
2. **EOC-B02** · Unknown string conditions silently evaluate to false: shipped vanilla dialogue and the EOC docs are already broken by it, and load-time validation is cheap.
3. **EOC-B03** · `map_is_outside` is inverted: a one-character fix (plus a data recheck) for a documented condition that every user currently gets backwards.
4. **EOC-B04** · `weighted_list_eocs` crashes when all weights are below 1: a trivial crash fix (`weighted_float_list` plus a null check).
5. **EOC-B05** · `u_consume_item_sum` never removes partial charge stacks and over-consumes multi-item sums: players get rewards for free or lose too many items in vanilla missions.

## 1. Missing JSON bindings & EOC features

### EOC-J01 · EOC variables cannot hold a handle to a monster, item or vehicle; spawn effects never expose what they spawned
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:7216`, `src/talker.h:91`, `src/talker_character.h:40`, `src/npctalk.cpp:3269`, `src/npctalk.cpp:6522`, `src/npctalk.cpp:7765`, `src/npctalk.cpp:7961`, `src/npctalk.cpp:7968`, `data/json/effects_on_condition/npc_eocs/hallucination_eocs.json:172`

`u_set_talker` stores `getID()`, but only `talker_character` overrides `getID`. For a monster, item or vehicle it silently stores `"0"`. `alpha_talker` / `beta_talker` resolve only character ids, and `alpha_loc` / `beta_loc` address a creature by tile, which breaks as soon as it moves. `u_spawn_monster` and `u_spawn_npc` run their `true_eocs` with the caller's dialogue, so nothing can act on the spawned entity directly: set vars on a spawned quest NPC, start an EOC on a summoned monster, or tag a spawned item. `spawn_monster` has `mon_variables`, but `spawn_npc` has no equivalent, and item spawning cannot set vars, charges or damage. Workarounds exist (`spawn_npc` `unique_id` plus `u_run_npc_eocs`; `mon_variables` plus `u_run_monster_eocs`), but they are indirect. The copy-hostile mode of `spawn_monster` also crashes on NPCs; that is reported separately as EOC-B01.

```cpp
// npctalk.cpp:7223-7225
int id = d.actor( is_npc )->getID().get_value();
write_var_value( type, var_name, &d, std::to_string( id ) );
// talker.h:91 default returns character_id(0); only talker_character.h:40 overrides it
```

**Fix:** Add `"spawned_eocs": [ ... ]` to `u_spawn_monster`, `u_spawn_npc`, `u_spawn_item` and `map_spawn_item`, run once per spawned entity with it as alpha and the caller as beta. Add `"result_var": { "context_val": "spawned" }` holding a stable handle. Give monsters a persistent UID so `u_set_talker` / `alpha_talker` work for them. Add `npc_variables` and `item_variables` fields.

### EOC-J02 · Documented but non-functional: var_val `g_`/`v_` prefixes, `time_until_eoc`, add_var `type`/`context`, `offer_mission` vars, `set_fault` message, `run_for_npcs` scope
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/dialogue_helpers.cpp:52`, `src/math_parser.cpp:994`, `src/math_parser_diag.cpp:1435`, `src/condition.cpp:170`, `src/npctalk.cpp:6221`, `src/npctalk.cpp:6283`, `src/npctalk.cpp:6297`, `src/effect_on_condition.cpp:378`, `doc/JSON/EFFECT_ON_CONDITION.md:130`, `doc/JSON/EFFECT_ON_CONDITION.md:2185`, `doc/JSON/EFFECT_ON_CONDITION.md:3912`, `doc/JSON/EFFECT_ON_CONDITION.md:5410`, `doc/JSON/NPCs.md:1472`, `data/json/monster_special_attacks/netherum_labyrinth.json:47`

Six documented behaviours do not exist in code:
- (a) `var_val` is documented to resolve `g_` and `v_` prefixes (EFFECT_ON_CONDITION.md:130-136, NPCs.md:1263-1270). `process_variable` handles only `u_`, `n_` and `_`, so `g_foo` reads a global literally named `g_foo` and `v_foo` does not indirect. The math parser also throws on `g_` (math_parser.cpp:994-1008).
- (b) `time_until_eoc` is documented as time until the next run. It returns the absolute fire turn, and it searches only the global queue.
- (c) The `u_add_var` / `u_lose_var` `type` / `context` fields are ignored by `get_talk_varname`. The add_var docs mark them deprecated, but the lose_var docs still list them as required.
- (d) `offer_mission` is documented to take variable objects, but an object value throws `Invalid input` at load.
- (e) The `message` field of `u/npc_set_fault` (default true) never prints. The holder is taken from the item talker, whose `get_character()` is always null. Vanilla is affected: netherum_labyrinth.json:47 expects "Out-of-phase goo sticks to the %s!".
- (f) `run_for_npcs` is documented to run for all NPCs but iterates `g->all_npcs()`, which covers loaded NPCs only.

```cpp
// math_parser_diag.cpp:1435 – absolute turn, not time remaining
return it != list.end() ? _time_in_unit( to_turn<double>( it->time ), ... ) : -1;
// npctalk.cpp:6289 – item talker has no character
const Character *holder = msg ? d.actor( is_npc )->get_character() : nullptr;
```

**Fix:** (a) Handle `g_` and `v_` in `process_variable` (or remove them from the docs). (b) Subtract `calendar::turn`, and search per-character queues too. (c) Remove the `type`/`context` rows from the docs, or concatenate them at load. (d) Read `offer_mission` as `str_or_var`. (e) Use `d.actor( !is_npc )->get_character()` or `it.carrier()` as the holder. (f) Document the reality-bubble limit.

### EOC-J03 · Dialogue references are never validated at load: topic ids, `first_topic`, trial skills and trial mod keys
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:9572`, `src/npctalk.cpp:1856`, `src/npctalk.cpp:3218`, `src/npctalk.cpp:2767`, `src/npc.cpp:552`, `src/talker_npc.cpp:242`, `tools/dialogue_validator.py:1`

`json_talk_topic::check_consistency()` is an empty TODO with no callers. A response's `topic` or `failure_topic`, or an NPC chatbin topic, can name a nonexistent topic, and this only shows up when a player reaches it ("I don't know what to say for %s"). `npc_template::check_consistency` does not check that `first_topic` exists. `skill_required` is a raw string that is never validated as a `skill_id`. Trial and `add_debt` `mod` keys are matched by string comparison in `parse_mod`, and unknown keys silently count as 0. CI runs `tools/dialogue_validator.py`, but only over `data/json/npcs`, it never fails the build, and it never runs for mods or at game load.

```cpp
// npctalk.cpp:9572-9576
void json_talk_topic::check_consistency() const {
    // TODO: check that all referenced topic actually exist. ...
}
```

**Fix:** After load, collect every topic id referenced in responses, `failure_topic`, inline topics, NPC chatbins and `npc_first_topic` effects, and validate against `json_talk_topics` plus the hardcoded topics. Validate `skill_required` as a `skill_id`. Validate `mod` keys against a fixed enum and replace the `npc_has_trait:` string hack with a condition-based mod.

### EOC-J04 · `deactivate_condition` is silently ignored outside RECURRING or next to `false_effect`; no JSON way to reactivate; EOC `check()` is empty
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:106`, `src/effect_on_condition.cpp:291`, `src/effect_on_condition.cpp:421`, `src/effect_on_condition.cpp:540`, `src/effect_on_condition.cpp:549`, `src/character.cpp:5948`, `src/bionics.cpp:2925`, `src/weather.cpp:1067`, `data/json/monster_special_attacks/void_spider_mechanics.json:195`

`deactivate_condition` is only evaluated in `process_eocs`, for RECURRING EOCs without a `false_effect`. Everywhere else it is loaded and never read, and since `effect_on_condition::check()` and `finalize()` are empty, nothing warns. Fifteen EOCs in `data/` put it on non-recurring or `false_effect` EOCs (e.g. `EOC_IN_VOID_SPIDER_LAIR`, `EOC_LUKE_RETRIEVE_TRUCK_END`). In those cases the dead field happens to be harmless because the main condition stops the requeue, but it misleads authors. Deactivated EOCs are only re-tested on mutation gain/loss, bionic add/remove and weather change, and no effect lets JSON reactivate or deactivate an EOC, so a `deactivate_condition` based on a variable, effect or location never comes back.

```cpp
// effect_on_condition.cpp:421
if( !has_deactivate_condition || has_false_effect ) { return false; }
// effect_on_condition.cpp:549-551
void effect_on_condition::check() const { }
```

**Fix:** In `check()`, error when `deactivate_condition` is set on a non-RECURRING EOC or together with `false_effect`, and warn about other meaningless combinations. Add `{ "u_reactivate_eocs": [ "EOC_X" ] }` / `{ "u_deactivate_eocs": [ ... ] }` plus global variants.

### EOC-J05 · `foreach` silently does nothing on unknown types or targets and can enumerate only four id kinds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:7297`, `src/npctalk.cpp:7316`, `src/npctalk.cpp:7334`

The `foreach` type and target are plain strings compared only at runtime. An unknown type (`"idss"`) or target (`"bodyparts"`), or a target given as a variable object, produces an empty list with no error at load or run time. `ids` covers only bodypart, flag, trait and vitamin. `item_group` and `monstergroup` targets must be literals. Nothing can enumerate a talker's own effects, bionics or spells (owned traits can be found by looping over all trait ids with `u_has_trait`).

```cpp
// npctalk.cpp:7316-7334
if( type == "ids" ) {
    if( target == "bodypart" ) {...} else if( target == "flag" ) {...}
    else if( target == "trait" ) {...} else if( target == "vitamin" ) {...}
} else if( type == "item_group" ) { item_group_id ig( target ); ...
```

**Fix:** Validate type and target at load and throw on unknown values. Accept `str_or_var` targets. Add effect/skill/spell/proficiency/martial_art/recipe/mtype id lists and talker-owned lists, e.g. `"foreach": "u_traits"`, `"u_effects"`, `"u_bionics"`, `"u_spells"`.

### EOC-J06 · No mod-defined events: EVENT EOCs and `trigger_event` are limited to the hardcoded `event_type` enum
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:8488`, `src/npctalk.cpp:8510`, `src/effect_on_condition.cpp:575`, `src/effect_on_condition.cpp:580`, `src/event_bus.cpp:79`, `src/event_bus.cpp:136`, `data/json/monsters/singularities.json:398`, `data/json/effects_on_condition/nether_eocs/portal_storm_effect_on_condition.json:218`

EVENT EOCs can only subscribe to the C++ `event_type` enum. `trigger_event`, the only way for JSON to raise an event, resolves the name with `io::string_to_enum<event_type>`, passes positional stringified args, uses `send()` instead of `send_with_talker` (so alpha comes from the `potential_alphas` heuristic and beta is always null), checks the argument count only at fire time, and is undocumented. Without a publish/subscribe channel, mods hook into each other by overriding whole EOCs; vanilla adds "prep" EOCs (singularities.json:398) just so mods can override them. The only generic channel today is abusing `trigger_event: "u_var_changed"` with a var name filter (portal_storm_effect_on_condition.json:218).

```cpp
// npctalk.cpp:8494, 8510
event_type type = io::string_to_enum<event_type>( type_str );
get_event_bus().send( cata::event::make_dyn( type, args_str ) );
```

**Fix:** Add string-keyed custom events: subscribe with `{ "eoc_type": "EVENT", "required_event": "custom", "custom_event": "mymod_boss_defeated" }`; raise with `{ "trigger_custom_event": "mymod_boss_defeated", "variables": { ... }, "alpha_talker": "u", "beta_talker": "npc" }`. Build a `map<string, vector<eoc_id>>` in finalize and dispatch with real talkers. For `trigger_event`, check the arg count at load, allow alpha/beta talkers, and document it.

### EOC-J07 · Talk topics can only be appended to; mods cannot replace or remove vanilla responses, and appending wipes `portrait_override`
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:9432`, `src/npctalk.cpp:9453`, `src/npctalk.cpp:9457`, `src/npctalk.cpp:9465`, `src/npctalk.cpp:9491`, `src/npctalk.cpp:9575`, `src/npctalk.cpp:1880`, `doc/JSON/NPCs.md:454`, `doc/JSON/NPCs.md:456`, `data/json/npcs/refugee_center/surface_staff/Smokes/free_merchant_shopkeep_talk_fluff.json:64`

`load_talk_topic` appends every response, speaker_effect and repeat_response to the existing topic. A mod that redefines a vanilla topic can add responses but cannot remove or replace one (`replace_built_in_responses` only hides the C++ responses), so overhaul mods must clone whole topic trees. Any redefinition without `portrait_override` resets it to `nullopt`. Vanilla hits this: Smokes' talk fluff sets `portrait_override` on an id array, and the later definitions of `AboutMe_Basic` and `AboutMe_Smokes2` wipe it. NPCs.md contradicts itself: line 454 says the last definition overrides, line 456 says responses are appended.

```cpp
// npctalk.cpp:9454-9466
if( jo.has_member( "portrait_override" ) ) {...} else { portrait_override = std::nullopt; }
...
for( JsonObject response : jo.get_array( "responses" ) ) { responses.emplace_back( response, src ); }
```

**Fix:** Support `"replace_responses": true` (clear before appending), and an optional response `"id"` so mods can `"delete_responses": [ "id" ]` or override by id. Keep `portrait_override` unless the field is present. Rewrite NPCs.md:454-456 to describe merge semantics.

### EOC-J08 · Monster talkers silently ignore most effects, and every base-talker setter is a silent no-op
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker.h:809`, `src/talker.h:936`, `src/talker_monster.h:85`, `src/npctalk.cpp:5919`, `src/npctalk.cpp:5948`, `src/npctalk.cpp:6014`, `src/npctalk.cpp:4328`, `src/npctalk.cpp:5582`, `src/talker_character.cpp:1485`, `src/condition.cpp:2472`

Mutating talker methods have empty default bodies in `talker.h` (809-937). `talker_monster` overrides only effects, vars, pain, anger, morale, friendly, die, hp and deal_damage. So `u_attack` with a monster attacker does nothing (`attack_target` is implemented only for characters), `u/npc_prevent_death` ignores monsters, `npc_change_faction` only works on NPCs, and math setters on unsupported talkers do nothing. `u_ranged_attack` dereferences a null character for a monster attacker, and crashes for an unarmed character too (see EOC-B13). Use cases blocked: forcing a boss to use a specific special attack, converting a monster to another faction, a monster cheat-death mechanic. Partial workarounds: math `friendly` (condition.cpp:2480) and `u_cast_spell` work for monsters. `u_message` ignoring non-avatars is intentional.

```cpp
// talker.h:936
virtual void attack_target( Creature &, bool, const matec_id &, bool, int ) {}
// npctalk.cpp:6016-6019
Character *ch = d.actor( is_npc )->get_character(); if( ch ) { ch->prevent_death(); }
```

**Fix:** Implement `talker_monster::attack_target` (melee plus forced mattack) and add `{ "u_use_special_attack": "SMASH" }`. Make `u_ranged_attack` work for monsters with gun mattacks, and run `false_eocs` when the attacker is not a Character. Add `{ "u_set_monster_faction": "player" }` and monster prevent-death. Have base-talker setters debugmsg (at least in debug mode) naming the effect and talker type.

### EOC-J09 · Mission EOC effects and conditions are hardwired to the avatar, have no giver, and cannot query mission history
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:6153`, `src/npctalk.cpp:6162`, `src/npctalk.cpp:6184`, `src/npctalk.cpp:6208`, `src/npctalk.cpp:6221`, `src/condition.cpp:582`, `doc/JSON/EFFECT_ON_CONDITION.md:2185`

`assign_mission` always assigns to `get_avatar()` and reserves the mission with an empty giver, so an EOC-assigned mission cannot be turned in to the NPC who gave it. `finish_mission`, `remove_active_mission` and `u_has_mission` scan only the avatar's active missions. No condition, math function or event reports whether mission X was completed or failed earlier, so quest chains must mirror state into variables by hand (mission start/end/fail effects are the current way). `offer_mission` accepts literals only (see EOC-J02 d). An NPC-given mission with a giver is possible via `offer_mission` plus the string `assign_mission` talk function.

```cpp
// npctalk.cpp:6162-6163
mission *new_mission = mission::reserve_new( mission_type, character_id() );
new_mission->assign( player_character );
```

**Fix:** Add `"giver": "npc"` and `"target": "u"` to `assign_mission`. Add `{ "u_mission_status": "MISSION_X", "status": "active|completed|failed" }` and math `u_mission_status('MISSION_X')`. Add `mission_completed` / `mission_failed` events. Accept `str_or_var` in `offer_mission`.

### EOC-J10 · Death hooks are asymmetric: PREVENT_DEATH gets no killer, NPC_DEATH is per-template only, monsters cannot be revived
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:512`, `src/effect_on_condition.cpp:532`, `src/npc.cpp:3319`, `src/npc.cpp:3327`, `src/game.cpp:2904`, `src/monster.cpp:3122`, `src/monster.cpp:3218`, `doc/JSON/EFFECT_ON_CONDITION.md:48`

PREVENT_DEATH EOCs run with a null beta, while AVATAR_DEATH receives the killer, so "revive unless killed by X" cannot see the killer. NPC_DEATH fires only from `death_eocs` on an NPC template and cannot be added at runtime. `character_dies` is sent without talkers or a killer field. Monsters have a per-type `death_function.eoc` hook, but `dead = true` is already set (monster.cpp:3122), so they cannot be revived. The docs say AVATAR_DEATH and NPC_DEATH EOCs run "until not dead", but the code runs them all (see EOC-B17), and `bury_screen()` is shown before AVATAR_DEATH EOCs run. A global hook on NPC deaths is possible today via an EVENT EOC on `character_dies`.

```cpp
// effect_on_condition.cpp:512 (PREVENT_DEATH) vs 532 (AVATAR_DEATH)
dialogue d( get_talker_for( player_character ), nullptr );
dialogue d( get_talker_for( get_avatar() ), klr == nullptr ? nullptr : get_talker_for( klr ) );
// game.cpp:2904-2905
bury_screen(); effect_on_conditions::avatar_death();
```

**Fix:** Pass the killer as beta to PREVENT_DEATH. Send `character_dies` with `send_with_talker( victim, killer )`. Add a `PREVENT_MONSTER_DEATH` type (or a monster revive path) and `{ "npc_add_death_eoc": "EOC_X" }`. Run `bury_screen()` after AVATAR_DEATH EOCs. Fix the doc wording.

### EOC-J11 · NPC social and training tuning is hardcoded and NPC personality/chatbin state is not exposed
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:564`, `src/npctalk.cpp:571`, `src/npctalk.cpp:599`, `src/npctalk.cpp:2291`, `src/npctalk.cpp:3215`, `src/npctalk.cpp:9595`, `src/npctalk.cpp:5543`, `src/talker_npc.cpp:242`

Training time and cost are C++ formulas (skill: 1 h + 30 min × level − 1 min × teacher level, cost 1000 × (1+lvl)²; martial art 30 min / 800; spell 100 × difficulty × level), and `Skill::load` has no override. Trial `difficulty` is read with `jo.get_int`, so no variables or math. Speech practice is hardcoded to (100−chance)/10 or /7. Unknown `mod` keys count as 0 (EOC-J03). Stranger-topic selection in `npc::pick_talk_topic` uses fixed personality thresholds and can set `NPCATT_MUG`. Personality values (aggression, bravery, altruism, collector) cannot be read or written by math or conditions. Of the chatbin topics, only `first_topic` is settable at runtime. Conditional dynamic lines and `npc_first_topic` cover greeting changes; a CONDITION trial gives a math-driven check without a meaningful displayed chance.

```cpp
// npctalk.cpp:567-578
return 1_hours + 30_minutes * student.get_skill_level( skill ) - 1_minutes * teacher.get_skill_level( skill );
return 1000 * ( 1 + skill_level ) * ( 1 + skill_level );
// npctalk.cpp:3215
difficulty = jo.get_int( "difficulty" );
```

**Fix:** Add optional `"training_time"` / `"training_cost"` math expressions to skills, martial arts and spells, defaulting to the current formulas. Make trial `difficulty` and mod values `dbl_or_var`. Add assignable math `n_personality('bravery')`. Add `{ "npc_set_chatbin_topic": "talk_friend", "topic": "TALK_X" }`. Move the stranger-topic thresholds into `npc_class` JSON.

### EOC-J12 · Queued EOCs (`time_in_future`) keep only a Character alpha; beta, monster, item and vehicle talkers are dropped
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/npctalk.cpp:6470`, `src/npctalk.cpp:6481`, `src/character.h:315`, `src/effect_on_condition.cpp:233`, `src/effect_on_condition.cpp:311`, `src/math_parser_diag.cpp:1423`, `src/math_parser_diag.cpp:1435`, `doc/JSON/EFFECT_ON_CONDITION.md:2219`

`queued_eoc` stores only `{eoc, time, context}`. `process_eoc` queues only when alpha is a Character (or the EOC is global); otherwise it does nothing, which the code comment admits is a silent failure. When a queued EOC fires it runs with `dialogue( owner, nullptr )`, so the beta is lost: a `run_eocs` + `time_in_future` from an item use, a dialogue or an attack drops the item/NPC/attacker, and any `npc_*` effect then debugmsgs "invalid beta talker" and falls back to alpha. Blocked patterns: an item that detonates 30 s after use, a mark on a monster that expires in an hour, an NPC retaliating a day later. Queued EOCs cannot be cancelled, and `time_until_eoc` only scans the global queue (and returns an absolute turn, EOC-J02 b). The limitation is documented (EFFECT_ON_CONDITION.md:2219). For characters, storing beta via `u_set_talker` in a context var and re-running with `beta_talker` works around it; items can use `countdown_action`. Monsters and vehicles have no workaround.

```cpp
// character.h:315-320
struct queued_eoc { effect_on_condition_id eoc; time_point time; global_variables::impl_t context; };
// effect_on_condition.cpp:311
dialogue d( get_talker_for( you ), nullptr );
```

**Fix:** Store serializable talker references in `queued_eoc` (`alpha_ref` / `beta_ref`: character_id, a persistent monster UID, item_location, vehicle position plus id) and rebuild them on activation, debugmsg if gone. Add a global entity queue for non-Character alphas. Add `{ "cancel_queued_eocs": [ "EOC_X" ], "scope": "u" }` and `u_time_until_eoc('EOC_X')` reading per-character queues.

### EOC-J13 · The furniture talker is really a computer wrapper and the vehicle talker only supports vars and effects
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/talker_furniture.h:19`, `src/computer.cpp:451`, `src/talker_vehicle.h:72`, `src/iexamine_actors.cpp:300`, `doc/JSON/EFFECT_ON_CONDITION.md:66`

`talker_furniture` is built only from a `computer*`, so "furniture" talkers exist only for terminals. Furniture `examine_action` EOCs get no beta, only the `this` and `pos` context strings. There is no per-tile talker or var store, so state on a piece of furniture (shrine charges, an altar's owner) must be faked with globals keyed by coordinates. The mutable `talker_vehicle` overrides only `set_value`, `remove_value`, `add_effect` and `remove_effect`; EOCs can read speed and facing but cannot damage or repair parts, refuel, toggle engines or lights, or change ownership (only `u_teleport` mutates a vehicle). Furniture can be swapped via `mapgen_update` / `u_transform_radius`.

```cpp
// talker_furniture.h:19
explicit talker_furniture_const( computer *new_me ): me_comp( new_me ) {};
```

**Fix:** Add a `talker_tile` (terrain/furniture at a tripoint, vars stored in the submap like item vars) and pass it as beta for `examine_action`, construction and smash hooks. Add vehicle effects: `{ "u_veh_damage_part": { "flag": "ENGINE" }, "amount": 50 }`, `{ "u_veh_set_fuel": "gasoline", "amount": ... }`, `{ "u_veh_set_owner": "your_followers" }`, `{ "u_veh_toggle": "engine" }`, and math `u_veh_part_hp('ENGINE')`.

### EOC-J14 · `u_know_recipe` ignores the talker and always checks the avatar; no `npc_know_recipe`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/condition.cpp:2041`, `src/condition.cpp:2047`, `src/condition.cpp:2644`, `src/talker.h:243`, `src/talker_character.cpp:300`, `doc/JSON/EFFECT_ON_CONDITION.md:1048`

`u_learn_recipe` and `npc_learn_recipe` exist, but the only check, `u_know_recipe`, calls `get_player_character()` whatever alpha is. In an NPC-alpha EOC it silently answers for the player, and nothing can check whether an NPC learned a recipe. The code comment is outdated: `const_talker::has_recipe` exists and `talker_character` implements it. The docs list the condition as avatar-only.

```cpp
// condition.cpp:2046-2047
// should be a talker function but recipes aren't in Character:: yet
return get_player_character().knows_recipe( &rep );
```

**Fix:** Register `{ "u_know_recipe", "npc_know_recipe", jarg::member, ... }` using `d.const_actor( is_npc )->has_recipe( recipe_id( ... ) )`. Optionally add math `u_knows_recipe('id')`.

### EOC-J15 · `u_var_changed` fires only from the legacy string `u_add_var`; math, `set_string_var` and `copy_var` never emit it
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3925`, `src/condition.cpp:288`, `src/event.h:968`, `doc/JSON/EFFECT_ON_CONDITION.md:1956`, `src/npctalk.cpp:8488`

The only sender of `u_var_changed` is `f_add_var` (deprecated string syntax, non-`time` branch). `npc_add_var` also emits it as `u_var_changed` with no scope field. `_write_var_value`, used by math assignment, `set_string_var`, `copy_var` and location vars, sends nothing, so "react when quest_stage changes" does not work with the recommended syntax. Content can emit the event manually via `trigger_event`, and firing on every math write would cost something on a hot path.

```cpp
// npctalk.cpp:3925 – only sender in src/
get_event_bus().send<event_type::u_var_changed>( var_base_name, possible_values[index] );
```

**Fix:** Send a `var_changed` event from `_write_var_value` with `{ scope, var, old_value, new_value }` and the owning talker as alpha, opt-in per variable, e.g. `"watch_vars": [ "quest_stage" ]` on the EVENT EOC.

### EOC-J16 · `u_run_npc/monster/vehicle/fixed_zone_eocs` drop the caller, take only literal ranges and filter monsters by id only
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:6845`, `src/npctalk.cpp:6887`, `src/npctalk.cpp:6923`, `src/npctalk.cpp:6967`, `src/npctalk.cpp:6980`, `src/npctalk.cpp:7004`, `src/npctalk.cpp:7018`, `src/npctalk.cpp:7046`, `src/npctalk.cpp:6605`

Each iterated entity becomes alpha with beta = nullptr, so the EOC cannot refer to whoever triggered it. `npc_range`, `monster_range`, `vehicle_range` and `zone_range` are read with `has_int` / `get_int`, while `z_min` / `z_max` in the same functions are `dbl_or_var`. Monsters are filtered only by `mtype_id`, and there is no `false_eocs` when nothing matched. Per-target conditions are possible in the run EOC itself, and the caller can be recovered via a stored location plus `run_eoc_with` `beta_loc`, so this is an ergonomics gap.

```cpp
// npctalk.cpp:6844-6845, 6967
if( jo.has_int( "npc_range" ) ) { npc_range = jo.get_int( "npc_range" ); }
dialogue newDialog( get_talker_for( target ), nullptr, d.get_conditionals(), d.get_context() );
```

**Fix:** Pass the caller as beta by default (`"caller_as": "beta" | "none"`). Make ranges `dbl_or_var`. Add `"species"`, `"monster_flags"`, `"attitude"`, `"friendly"` filters, and `"false_eocs"` when nothing matched.

### EOC-J17 · About 55 effect keys and 25 condition keys appear nowhere in `doc/`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:8609`, `src/npctalk.cpp:8784`, `src/npctalk.cpp:8946`, `src/condition.cpp:2582`, `src/condition.cpp:2689`, `doc/JSON/NPCs.md:927`

Grepping the parser tables against `doc/` finds many working but undocumented features. Effects: `trigger_event`, `copy_location`, `u/npc_run_vehicle_eocs`, `u/npc_lose_mutation_type`, `set_item_category_spawn_rates`, `npc_change_class`, `add_mission`, two `set_npc_cbm_*` rules, the string-dispatched `u_ranged_attack`, `u_wants_to_talk`, `npc_rules_menu`, and about 40 of 86 WRAP talk_functions. Conditions: exactly 25, including `u_need`, `u_service`, `u_is_riding`, `u_has_move_mode`, `u_is_bleeding`, `u_bodypart_is_bleeding`, `overmap_at_point`, `follower_present`, `u_monsters_in_direction`, `u_safe_mode_trigger`, `u_has_stolen_item`, `u_train_spells`, `npc_is_wearing`. NPCs.md:927 also documents `give_aid_all`; the real name is `give_all_aid`.

**Fix:** Document the missing keys and fix `give_aid_all`. Add a unit test that iterates `parsers` / `parsers_simple` / the effect tables and greps `doc/JSON`, so new keys cannot land undocumented. Document the npc-only talk_functions in one table, marked legacy.

### EOC-J18 · Hardcoded content in dialogue C++: service talk_functions, dead `buy_cow/chicken/horse`, PROF_FOODP and PROF_CHURL special cases
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk_funcs.cpp:90`, `src/npctalk_funcs.cpp:201`, `src/npctalk_funcs.cpp:215`, `src/npctalk_funcs.cpp:700`, `src/npctalk_funcs.cpp:736`, `src/npctalk_funcs.cpp:781`, `src/npctalk_funcs.cpp:863`, `src/npctalk.cpp:1109`, `src/npctalk.cpp:2189`, `src/npctalk.cpp:3241`, `src/npctalk.cpp:8815`, `src/character_health.cpp:1059`

`give_aid`, `lesser_give_aid` and `give_all_aid` hardcode heal amounts, removed effects, durations and follower radius. `buy_haircut` / `buy_shave` hardcode morale values; `repair_bionic_limbs` hardcodes prices (20 per HP, 100 per wound). `buy_chicken`, `buy_cow` and `buy_horse` hardcode `mon_*` ids, have zero uses in `data/`, and duplicate `u_buy_monster`. `game::chat` repeats the PROF_FOODP + mask check that `Character::is_mute()` already does. `dialogue::gen_responses` injects a PROF_CHURL response into every conversation. The npc-only talkfunction wrappers do nothing when beta exists but is not an NPC (a missing beta debugmsgs and falls back to alpha). Generic JSON replacements exist, so this is cleanup.

```cpp
// npctalk.cpp:2189-2192
if( player_character.has_trait( trait_PROF_CHURL ) && ...
    add_response( _( "Ho there, otherwyrldly devyl! ..." ), "TALK_CHURL_FRIENDLY" );
```

**Fix:** Re-implement the aid, haircut, shave and bionic-repair services as JSON EOCs (`u_heal`, `u_lose_effect`, `u_add_morale`, `u_assign_activity`) and delete `buy_chicken/cow/horse`. Replace the PROF_FOODP check with `is_mute()`. Add a global always-available response topic (e.g. `"TALK_GLOBAL"` merged into every topic with conditions) and move CHURL there.

### EOC-J19 · No dialogue start/end hooks; the shout menu and topic "back" grouping are hardcoded
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:1654`, `src/npctalk.cpp:1153`, `src/npctalk.cpp:1216`, `src/dialogue_imgui.cpp:58`, `src/dialogue_imgui.cpp:272`, `src/npctalk.cpp:5666`

`avatar::talk_to` opens and closes a conversation without sending any event or running any EOC, and `event_type` has no dialogue event, so mods cannot react globally to talking (only by appending speaker_effects to specific greeting topics). The C-menu (`game::chat`) is a fixed uilist. TALK_NONE "back" navigation pops by hardcoded topic categories; the code's own TODO says this should be a topic property. `u_message` `popup_w_interrupt_query` only understands `portal_storm_popup`.

```cpp
// dialogue_imgui.cpp:61
// TODO: ideally, this would be a property of the topic itself.
// npctalk.cpp:5666
if( interrupt_type.evaluate( d ) == "portal_storm_popup" ) {
```

**Fix:** Add `dialogue_started` / `dialogue_ended` events (alpha = avatar, beta = partner, context: topic, by_radio). Add a topic field `"category": "missions"` replacing the hardcoded sets. Add a JSON `chat_command` type `{ "id", "name", "condition", "eoc" }` listed in the C-menu. Map `interrupt_type` strings to every `distraction_type` via `io::string_to_enum`.

## 2. Bugs, oversights & quirks

### EOC-B01 · `u_spawn_monster ""` (copy hostile creature) null-derefs when the chosen creature is a hostile NPC
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:7832`, `src/npctalk.cpp:7842`, `src/npctalk.cpp:7845`, `src/npctalk.cpp:7858`, `src/npctalk.cpp:7890`, `src/npctalk.cpp:7920`, `src/game.cpp:10705`, `src/game.cpp:10824`, `src/character.cpp:7258`, `data/json/effects_on_condition/npc_eocs/hallucination_eocs.json:172`, `data/json/monster_special_attacks/spells.json:1312`

With an empty monster id, `f_spawn_monster` picks hostile creatures near the player using `g->get_creature_if` / `get_creatures_if`, which iterate `all_creatures()` and so include NPCs (`Character::attitude_to` returns HOSTILE for an enemy NPC). The pick is copied with `*copy->as_monster()`, which is null for an NPC. Vanilla reaches this: `creature_hallucinations` and `hallucination_swarm` (hallucination_eocs.json:172, 208, `target_range` 50) and `EOC_STARE_EFF2` (spells.json:1312). A hallucinating player within 50 tiles of a hostile NPC such as a bandit can segfault when the only or randomly chosen target is the NPC. The search is also centred on `get_player_character()` rather than the acting talker.

```cpp
// npctalk.cpp:7832-7842
Creature *copy = g->get_creature_if( [target_range]( const Creature & critter ) -> bool {
    ... get_player_character().attitude_to( critter ) == Creature::Attitude::HOSTILE; ...
target_monster = *copy->as_monster();
```

**Fix:** Add `critter.is_monster()` to both lambdas (7832, 7845). Use `d.actor( is_npc )->pos_bub()` and its attitude instead of the player. To hallucinate NPCs as well, branch to `spawn_npc` with the HALLUCINATION trait.

### EOC-B02 · Unknown string conditions compile to an always-false condition with no load error; vanilla content and the docs are affected
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/condition.cpp:2860`, `src/condition.cpp:2862`, `src/condition.cpp:2880`, `src/condition.cpp:2770`, `src/condition.cpp:2771`, `src/condition.cpp:2807`, `src/condition.cpp:372`, `src/condition.cpp:373`, `src/condition.cpp:2747`, `src/npctalk.cpp:8988`, `doc/JSON/EFFECT_ON_CONDITION.md:1549`, `doc/JSON/EFFECT_ON_CONDITION.md:1577`, `doc/JSON/EFFECT_ON_CONDITION.md:1599`, `data/json/encounters/randenc_refugee_center.json:35`, `data/json/npcs/robofac/NPC_Cranberry_Foster.json:191`, `data/json/npcs/common_chat/TALK_COMMON_OTHER.json:213`

`conditional_t( std::string_view )` searches `parsers_simple` and, if nothing matches, installs a lambda returning false without reporting anything. Every string-form condition goes through it: `"condition": "..."` (`read_condition`), string entries in `and`/`or` (`parse_array`), and `"not": "..."`. A typo makes the condition silently false; under `not` it is silently true. The object form and unknown effect strings (npctalk.cpp:8988) do throw at load. Already broken in shipped data and docs:
- The EOC doc uses `npc_see_you` / `npc_see_you_loc` (e.g. `{ "not": "npc_see_you" }`); the real keys are `npc_see_u` / `npc_see_u_loc`.
- `"is_night"` in randenc_refugee_center.json:35, 82 (only `is_day` exists).
- `{ "not": "has_many_assigned_mission" }` in NPC_Cranberry_Foster.json:191, 223, 490, 495 (real key `has_many_assigned_missions`), so always true.
- `"condition": "npc_has_destination"` in TALK_COMMON_OTHER.json:213 (no such key), so "How much further?" never shows.

```cpp
// condition.cpp:2880-2884
if( !found ) {
    condition = []( const_dialogue const & ) { return false; };
}
```

**Fix:** Validate string conditions at load: have `read_condition`, `parse_array` and the `not` branch check the string against `dialogue_data::simple_string_conds()` and call `jo.throw_error_at( member, "unknown condition string" )`, or pass a `JsonValue` into the string constructor so it can throw. Then fix the four data files and the doc keys (or register `npc_see_you` / `npc_see_you_loc` as aliases).

### EOC-B03 · `map_is_outside` returns the opposite of what it documents
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/condition.cpp:1870`, `src/map.cpp:3782`, `src/condition.cpp:1700`, `doc/JSON/EFFECT_ON_CONDITION.md:1420`, `data/mods/Xedra_Evolved/spells/changeling_seasonal_magic_winter_eocs.json:296`, `data/mods/Xedra_Evolved/mutations/playable_changeling_eocs.json:860`, `data/mods/Xedra_Evolved/eocs/eoc_strange_events.json:122`

`f_map_is_outside` returns `!here.is_outside( loc )`, but `map::is_outside` already returns true outdoors; the sibling `tile_is_outside` (1700) uses it un-negated, and the doc says it returns true when outside. All three Xedra uses behave inverted: the changeling hail attack fires only indoors, and the brownie "indoor blink" only targets outdoor tiles. Underground the outside cache is all false, so `EOC_BROWNIE_INDOOR_BLINK`'s condition never passes; its `false_effect` re-runs itself through `run_eocs`, which bypasses the loop guard (EOC-B14), and can recurse until stack overflow.

```cpp
// condition.cpp:1870
if( here.inbounds( loc ) ) { return !here.is_outside( loc ); }
```

**Fix:** Return `here.is_outside( loc )`, then recheck every `map_is_outside` use in `data/` (3 in Xedra_Evolved). Optionally support out-of-bubble locations by loading a tinymap, as `transform_radius` does.

### EOC-B04 · `weighted_list_eocs` dereferences a null pick when all weights are below 1; fractional weights truncate
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:7232`, `src/npctalk.cpp:7241`, `src/npctalk.cpp:7244`, `src/npctalk.cpp:7246`, `src/weighted_list.h:32`, `src/weighted_list.h:150`, `doc/JSON/EFFECT_ON_CONDITION.md:3221`

Weights are `dbl_or_var` but go into a `weighted_int_list`, so 0.3 becomes 0. `add()` drops weights ≤ 0, `pick()` returns nullptr when the total is 0, and the result is dereferenced unconditionally. Math-driven weights that all go to 0, or probabilities written as 0.3/0.7, crash the game. The doc example at line 3221 shows a bare inline-EOC entry, but the parser expects an array per element and throws. All 33 vanilla uses have safe weights, so this is a modder trap rather than a vanilla crash.

```cpp
// npctalk.cpp:7241-7246
weighted_int_list<effect_on_condition_id> eocs; ...
eocs.add( eoc_pair.first, eoc_pair.second.evaluate( d ) ); }
effect_on_condition_id picked_eoc = *eocs.pick();
```

**Fix:** Use `weighted_float_list<effect_on_condition_id>`. If `pick()` returns null, do nothing (or run an optional `false_eocs`). Either accept bare inline-EOC objects with weight 1 or fix the doc example.

### EOC-B05 · `u_consume_item_sum` never removes partial charges and over-consumes compared with `u_has_items_sum`
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:4283`, `src/npctalk.cpp:4284`, `src/npctalk.cpp:4269`, `src/npctalk.cpp:4262`, `src/condition.cpp:760`, `src/condition.cpp:783`, `doc/JSON/EFFECT_ON_CONDITION.md:4553`

Four problems in one effect:
1. When a charge stack is larger than the amount needed, `amount_desired` is zeroed before `mod_charges( -amount_desired )`, so nothing is consumed. Need 3 nails, carry 50: lose 0, still get the reward.
2. The matching condition is a weighted sum (1 of 3 sardines counts 0.33, entries sum to 1), but the effect resets `amount_desired` for each entry and removes up to the full amount of every listed item. In `MISSION_GODCO_KATHERINE_1` ("three cans of sardines, tuna, or salmon") a player carrying 3 of each loses all 9. This contradicts the doc's own blanket example.
3. The condition counts `crafting_inventory` within PICKUP_RANGE, but the effect only takes items `is_owned_by( *you )`, so unowned nearby items satisfy the check and are never taken.
4. `you` is not null-checked.

```cpp
// npctalk.cpp:4282-4285
} else {
    amount_desired = 0;
    it->mod_charges( -amount_desired );
    iter++;
}
```

**Fix:** Swap the two statements. Track one remaining "fraction of requirement" (1.0) across entries, as the condition does, and stop when it is used up. Use the same item source and ownership rule in condition and effect. Null-check the character.

### EOC-B06 · `test_eoc` on an EOC without a condition calls an empty `std::function`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/condition.cpp:1958`, `src/effect_on_condition.cpp:110`, `src/effect_on_condition.cpp:427`, `src/effect_on_condition.h:60`

`effect_on_condition::condition` is assigned only when the JSON has a `condition` member. `f_test_eoc` calls `tested->condition( d )` directly instead of `test_condition()`, which honours `has_condition`. `{ "test_eoc": "SOME_EOC" }` against an unconditional EOC throws `std::bad_function_call` and terminates. Per the docs it should return true. Vanilla targets all have conditions.

```cpp
// condition.cpp:1958
return tested->condition( d );
// effect_on_condition.cpp:427-429
bool effect_on_condition::test_condition( ... ) { return !has_condition || condition( d ); }
```

**Fix:** Call `tested->test_condition( d )`, or initialise `condition` to return true in `load()`.

### EOC-B07 · A queued EOC that re-queues itself with a zero or negative delay hangs the game
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:237`, `src/effect_on_condition.cpp:275`, `src/effect_on_condition.cpp:311`, `src/npctalk.cpp:6470`

`queue_effect_on_condition` pushes `calendar::turn + duration` into the same queue that `process_eocs` is draining with `while( top().time <= calendar::turn )`. Only RECURRING re-pushes are deferred. An ACTIVATION EOC that re-queues itself with a delay evaluating to 0 or less (e.g. an unset math var) is popped again by the same loop forever: a hard freeze with no guard. (The loss of beta for queued EOCs is covered in EOC-J12.)

```cpp
// effect_on_condition.cpp:237, 275-276
queued_eoc{ eoc, calendar::turn + duration, context }
while( !eoc_queue.empty() && eoc_queue.top().time <= calendar::turn )
```

**Fix:** Clamp the delay to at least 1 turn in `queue_effect_on_condition`, or collect pushes made during processing and push them after the loop, as already done for recurring EOCs.

### EOC-B08 · Location helpers: `copy_location` crashes on an ungenerated source; `near_om_location` reads the centre tile's mapgen args; `location_variable` NPC search is capped at one submap
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:5192`, `src/npctalk.cpp:5204`, `src/npctalk.cpp:5207`, `src/condition.cpp:1032`, `src/npctalk.cpp:4601`, `src/npctalk.cpp:4603`

1. `f_copy_location` generates missing destination submaps but looks up the source with `MAPBUFFER.lookup_submap`, which returns nullptr for never-generated OMTs, then calls `sm->get_revert_submap()`. Vanilla only copies the player's own location, but copying an unvisited template OMT is a natural mod use and crashes.
2. `f_near_om_location` passes the centre `omt_pos` instead of `curr_pos` to `mapgen_args`, so `FACTION_CAMP_START` checks on neighbouring tiles use the wrong arguments (latent in vanilla).
3. `f_location_variable`'s `npc` target uses `get_npcs_near( ..., 1 )` (1 submap) and compares `pos_bub()` against a `search_loc` that may come from a distant map, and re-queries per tile. Xedra_Evolved homullus_eocs.json:384 uses `target_max_radius` 30, so NPCs farther than about 12-24 tiles are missed.

```cpp
// npctalk.cpp:5204-5207
submap *sm = MAPBUFFER.lookup_submap( revert_sm ); ... sm->get_revert_submap()
// condition.cpp:1032
const std::optional<mapgen_arguments> *maybe_args = overmap_buffer.mapgen_args( omt_pos );
```

**Fix:** Load the source OMT via a tinymap when the submap is missing (or null-check and debugmsg). Use `curr_pos`. Derive the NPC search radius from `target_max_radius` and compare absolute positions (`pos_abs()`), querying once.

### EOC-B09 · `avatar::talk_to` loads the NPC's topics twice
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:1673`, `src/npctalk.cpp:1677`, `src/talker_npc.cpp:115`, `src/talker_npc.cpp:175`, `src/dialogue_imgui.cpp:271`, `src/dialogue_imgui.cpp:275`, `src/npctalk.cpp:5738`

The `get_topics` → `add_topic` loop appears twice in a row (inherited from upstream). The topic stack becomes `[T1..Tn, T1..Tn]`, so a TALK_NONE response from the opening topic re-shows the greeting (re-applying its speaker_effects) instead of ending. `get_topics` side effects run twice, mostly harmlessly (an extra second of NPC moves). For `open_dialogue` with a categorized topic the stack is `[T, T]`, and the category pop loop in `dialogue_imgui.cpp` can empty the stack and call `back()` on an empty vector; that can also happen with a single categorized topic, so a guard is needed regardless.

```cpp
// dialogue_imgui.cpp:273-275
do { topic_stack.pop_back(); } while( cat != -1 && topic_category( topic_stack.back() ) == cat );
```

**Fix:** Delete the second loop. Guard the pop loop with `!topic_stack.empty()`.

### EOC-B10 · `failure_topic` responses can never be selected; response flag vectors desync on front insertion and are read out of bounds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:9184`, `src/npctalk.cpp:2944`, `src/npctalk.cpp:2989`, `src/npctalk.cpp:2582`, `src/npctalk.cpp:9199`, `src/npctalk.cpp:2003`, `doc/JSON/NPCs.md:707`

1. A failed response with `failure_explanation` / `failure_topic` is added with exists=true, result=false, and `opt_imgui` resets any such selection to NONE outside debug mode. The documented redirect to the failure topic never happens; the vanilla use in NPC_old_guard_representative.json:479-480 is dead.
2. `add_gen_response` inserts at the front of `responses` when `insert_front` is true but always appends the condition flags, so selection checks read another response's flags. Line 9199 passes `!!condition` as `insert_front`, which looks like swapped arguments. Latent in vanilla; misfires when a topic mixes show_always/failure responses with front-inserted ones.
3. With an unmatched ANY_INPUT key, `response_ind == size` and the flag vector is read out of bounds (UB, benign in release).

```cpp
// npctalk.cpp:2582-2588
if( insert_front ) { responses.insert( responses.begin(), resp ); } else {...}
response_condition_exists.emplace_back( ... );
```

**Fix:** Add failure-explanation responses as selectable (exists=false). Store the condition flags inside `talk_response`, or insert flags at the same position. Bounds-check `response_ind`. Fix 9199 to `( result, true, !!condition, true )`.

### EOC-B11 · `u_pickup_items` `max_mass` is effectively 1 mg when set; documented variable support is ignored
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:8313`, `src/npctalk.cpp:8305`, `src/npctalk.cpp:8309`, `doc/JSON/EFFECT_ON_CONDITION.md:2652`

Copy-paste bug: `units::from_gram( jo.has_float( "max_mass" ) )` converts the boolean `true`, which ends up as a 1 mg limit. Any `max_mass` blocks picking up almost everything. `max_volume`, `max_mass` and `extra_moves_per_item` are read as literals only, though the docs say "int or variable object", so variables are silently ignored. No vanilla JSON uses this effect.

```cpp
// npctalk.cpp:8312-8314
if( jo.has_float( "max_mass" ) ) { max_mass = units::from_gram( jo.has_float( "max_mass" ) ); }
```

**Fix:** Read all three as `dbl_or_var` and evaluate them in the lambda (at minimum, use `get_float`).

### EOC-B12 · `u_pick_bodypart` `whitelist_type` / `blacklist_type` filters do nothing
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3756`, `src/npctalk.cpp:3770`

The type filters `continue` inside the inner loop over types, so they never skip the body part. `"whitelist_type": [ "arm" ]` still picks legs or the head, and the blacklist is ignored. Both are documented (EFFECT_ON_CONDITION.md:3278-3279). Combined with `u_add_wound` this puts wounds on excluded parts. No `data/` file uses these fields yet.

```cpp
// npctalk.cpp:3756-3761
for( const str_or_var &bp_type_var : whitelist_type ) {
    const bp_type type = ...;
    if( !bp_id->has_type( type ) ) { continue; }
}
```

**Fix:** Compute `ok = whitelist_type.empty() || any_of( ... has_type ... )` and `banned = any_of( blacklist ... )`, then `if( !ok || banned ) continue;` in the outer loop.

### EOC-B13 · Many EOC effects and conditions dereference talker sub-objects without null checks
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3584`, `src/npctalk.cpp:3614`, `src/npctalk.cpp:3843`, `src/npctalk.cpp:3867`, `src/npctalk.cpp:3891`, `src/npctalk.cpp:4162`, `src/npctalk.cpp:4263`, `src/npctalk.cpp:5283`, `src/npctalk.cpp:5323`, `src/npctalk.cpp:5464`, `src/npctalk.cpp:5515`, `src/npctalk.cpp:5734`, `src/npctalk.cpp:5753`, `src/npctalk.cpp:5953`, `src/npctalk.cpp:6289`, `src/npctalk.cpp:6302`, `src/npctalk.cpp:7616`, `src/condition.cpp:453`, `src/condition.cpp:468`, `src/condition.cpp:941`, `src/condition.cpp:1321`, `src/condition.cpp:1683`, `src/condition.cpp:1895`, `src/condition.cpp:2435`, `src/talker.h:785`

The default `get_character()`, `get_npc()`, `get_item()`, `get_creature()`, `get_faction()` and `get_bodypart()` return nullptr, and many lambdas dereference the result directly, so running them with the wrong talker type segfaults instead of debugmsg-ing. EOCs routinely run with monster, item, vehicle, furniture or zone talkers. Examples: `npc_lose_category` / `npc_lose_mutation_type` on a monster; `u_add_wound` on a missing body part; `open_dialogue` / `take_control` with a vehicle alpha; `u_ranged_attack` by a monster or an unarmed character (empty `item_location`); `u_set_fault` on a character; faction-trust effects/conditions on a faction-less beta (5323 dereferences right after a null check at 5317); `npc_player_see` on an item; `u_val('dodge')` on a monster; `u_bodypart_is_bleeding` on a monster. Sibling effects (`f_set_flag`, `f_add_trait`, `f_cast_spell`) do check, so the gaps are accidental. Most crashes need a talker type the docs list as invalid.

```cpp
// npctalk.cpp:5953-5954, 7616
item_location wielded = attacker->get_wielded_item(); if( wielded->is_gun() )
d.actor( true )->get_faction()->trusts_u += ...
```

**Fix:** Add a helper such as `Character *require_character( dialogue const &, bool is_npc, const char *effect )` that debugmsgs and returns null; use it at every site and return early (false for conditions). Check `get_part()` / `get_bodypart()` and wielded-item results.

### EOC-B14 · EOC infinite-loop guard misses `run_eocs` recursion and gives false positives on plain loops
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:359`, `src/npctalk.cpp:2748`, `src/npctalk.cpp:6629`, `src/npctalk.cpp:6512`, `src/npctalk.cpp:7206`, `src/npctalk.cpp:504`

`activate()` appends `"EOC: id"` to the caller's string callstack and prompts when its length exceeds 5000 characters. (1) `run_eocs` builds a new dialogue whose constructor does not copy the callstack, so the most common recursion form (an EOC that `run_eocs` itself, often via `false_effect`) never grows it and recurses to a stack overflow without warning. (2) Loops that reuse one dialogue keep appending: `run_eocs` with `iterations`/`condition`, `run_eoc_vector`, and `map_run_eocs` with `stop_at_first: false`. A 300-iteration loop over a 30-character id passes 5000 after about 140 iterations and then asks "Possible infinite loop" on every remaining iteration. (3) The threshold counts characters, not depth. See EOC-P01 for the cost side.

```cpp
// effect_on_condition.cpp:359-366
d.amend_callstack( "EOC: " + id.str() );
if( d.get_callstack().size() > 5000 ) { if( query_yn( ... "Possible infinite loop in EOC %s.  Stop execution?" ...
// npctalk.cpp:6629
dialogue newDialog{ std::move( alpha ), std::move( beta ), d.get_conditionals(), d.get_context() };
```

**Fix:** Track an integer depth that every dialogue-creating effect copies forward and `activate` increments on a copy (so siblings don't accumulate). Abort with one debugmsg at a fixed depth (e.g. 1000). Build the callstack text only for debug output.

### EOC-B15 · `take_control` / `control_npc` from a queued or recurring EOC swaps the EOC queue being iterated
**Severity:** Medium · **Effort:** Medium · **Verified:** plausible
**Where:** `src/effect_on_condition.cpp:277`, `src/effect_on_condition.cpp:297`, `src/effect_on_condition.cpp:301`, `src/npctalk.cpp:5757`, `src/avatar.cpp:172`, `src/character.cpp:608`, `src/character.cpp:423`

`process_eocs` holds a reference to `you.queued_effect_on_conditions` and a list iterator `it` for the running EOC. `control_npc` calls `swap_character`, which move-assigns Characters, and `queued_eocs` move-assign swaps the list and queue. After a mid-activation takeover, `eoc_queue` holds the NPC's queue while `it` belongs to the old body's list; `eoc_queue.list.erase( it )` erases a foreign node (UB, size corruption), and a recurring entry's iterator can be pushed into the wrong queue and later dangle. Global EOCs are safe. No vanilla or mod data queues an EOC that runs `take_control`, so reachability depends on modder content.

```cpp
// effect_on_condition.cpp:277-301
queued_eocs::storage_iter it = eoc_queue.queue.top(); ...
top.eoc->activate( nested_d ); ...
eoc_queue.list.erase( it );
```

**Fix:** Defer avatar switching: `take_control` sets a pending request that `game::do_turn` applies after EOC processing. Alternatively, compare the owner's `getID()` before and after `activate` and bail out without touching `it`.

### EOC-B16 · Underground "outdoor only" sounds are always heard (sign error in the depth roll)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:5652`, `src/npctalk.cpp:5804`, `src/weather.cpp:431`

`u_message` with `sound` + `outdoor_only` and `sound_effect` with `outdoor_event` attenuate underground with `one_in( max( roll_remainder( 2 * z / hearing ), 1 ) )`. z is negative underground, so the max is 1 and `one_in(1)` is always true: thunder and storm sounds always reach a player deep in a lab. About 28 vanilla entries use these flags. `weather_sound` has the same bug.

```cpp
// npctalk.cpp:5651-5653
one_in( std::max( roll_remainder( 2.0f * here.get_abs_sub().z() / target->hearing_ability() ), 1 ) )
```

**Fix:** Use `-2.0f * z` (or `std::abs`), via a shared helper also used by `weather_sound`.

### EOC-B17 · AVATAR_DEATH EOCs keep running after one has revived the player
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:522`, `src/effect_on_condition.cpp:508`, `src/game.cpp:2905`, `doc/JSON/EFFECT_ON_CONDITION.md:45`

The docs say AVATAR_DEATH EOCs run "until the player is not dead", and `prevent_death()` does break once alive, but `avatar_death()` runs every AVATAR_DEATH EOC. If one revives the player, other handlers still fire. Real case: vanilla `EOC_DEATH_SWITCH` and aftershock's `EOC_AFS_LEADERSHIP_SWITCH` both run `take_control_menu`; accepting the first re-prompts the "You have died" query aimed at the new, living body.

```cpp
// effect_on_condition.cpp:533-537
for( const effect_on_condition &eoc : get_all() ) {
    if( eoc.type == eoc_type::AVATAR_DEATH ) { eoc.activate( d ); }
}
```

**Fix:** Break once `!get_avatar().is_dead_state()`, matching `prevent_death`. Cache the PREVENT_DEATH / AVATAR_DEATH lists instead of scanning `get_all()`.

### EOC-B18 · `u_run_inv_eocs` `manual_mult` never runs `false_eocs` when the player cancels or selects nothing
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3397`, `src/npctalk.cpp:3413`, `src/npctalk.cpp:3394`, `src/npctalk.cpp:3373`, `src/npctalk.cpp:7076`, `src/npctalk.cpp:7168`

`manual` runs `false_eocs` when nothing was selected; `manual_mult` only does so when there were no candidate items at all, so cancelling or confirming an empty selection runs nothing, contrary to EFFECT_ON_CONDITION.md:2794/2962. An unknown option string (e.g. `"Random"`) is a silent no-op. The same runner backs `u_map_run_item_eocs`.

```cpp
// npctalk.cpp:3401-3415
for( item_location target : true_items ) { ... run_eoc( target, true_eocs ); }
if( true_items.empty() ) { run_eoc_vector( false_eocs, d ); }
```

**Fix:** Track whether any target ran and run `false_eocs` if not. Reject unknown constant option values at load.

### EOC-B19 · `u_spawn_item` with `container`: overflow items are lost; `count` on non-charge items becomes charges
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:4000`, `src/npctalk.cpp:4001`, `src/item_container.cpp:267`

The container branch sets `new_item.charges = count` even for items not counted by charges, so `{ "u_spawn_item": X, "container": Y, "count": 5 }` yields one X instead of five (NPCs.md:947). The `put_in` result is ignored: an item that does not fit is lost and the popup still announces the container. `put_in` does debugmsg (item_container.cpp:273), so it is not fully silent. All vanilla container+count uses are charge items.

```cpp
// npctalk.cpp:3999-4002
item container( container_id, calendar::turn );
new_item.charges = count;
container.put_in( new_item, pocket_type::CONTAINER );
```

**Fix:** For non-charge items, call `put_in` `count` times. Check the result and give or drop the overflow next to the container.

### EOC-B20 · EOC queue save-load: invalid ids debugmsg before the validity check; global queue skips legacy var migration
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:187`, `src/effect_on_condition.cpp:188`, `src/savegame.cpp:315`, `src/savegame_json.cpp:1212`

`process_new_eocs` reads `eoc_queue.top().eoc->global` (which debugmsgs on unknown ids) before checking `is_valid()` on the next line, so removing a mod with queued EOCs produces one debugmsg per stale entry (once, since they are then dropped). The global queue loader also lacks the `legacy_migrate_npctalk_var_prefix` call the character loader has, which only matters for pre-v36 saves.

```cpp
// effect_on_condition.cpp:187-188
if( global_queue == eoc_queue.top().eoc->global ) {
    if( eoc_queue.top().eoc.is_valid() ) {
```

**Fix:** Check `is_valid()` first and drop invalid entries quietly (or with one summary warning). Call `legacy_migrate_npctalk_var_prefix` in the global loader too.

## 3. Performance

### EOC-P01 · Every EOC activation deep-copies the dialogue 2-4 times and rebuilds a callstack string
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:360`, `src/effect_on_condition.cpp:361`, `src/effect_on_condition.cpp:369`, `src/effect_on_condition.cpp:281`, `src/npctalk.cpp:2653`, `src/npctalk.cpp:2662`, `src/npctalk.cpp:2707`, `src/npctalk.cpp:2728`, `src/npctalk.cpp:2748`, `src/npctalk.cpp:504`, `src/npctalk.cpp:6488`, `src/npctalk.cpp:6503`, `src/npctalk.cpp:6629`, `src/npctalk.cpp:6639`, `src/dialogue.h:284`

`effect_on_condition::activate` is the choke point for all EOC execution. Each call appends `"EOC: " + id` to a string callstack, calls `get_callstack()` (which returns `"Callstack: " + callstack` by value just to read `.size()`), and copies the dialogue. The dialogue copy const_clones and then clones both talkers (4 heap allocations) and copies the context map, the conditionals map and the callstack. Callers add more: `process_eocs` copies before `activate`; `f_run_eocs` builds a new dialogue, passes it by value to `run_eocs` / `queue_eocs`, and `activate` copies again (about 12 talker allocations per `run_eocs`). `run_eoc_vector` copies even when the vector is empty, which is the usual case for `true_eocs` / `false_eocs`. Because the callstack is never popped, loops grow it and copy it repeatedly (O(n²) in iterations); the correctness side is EOC-B14. Each activation adds only a handful of small allocations, likely outweighed by effect evaluation, hence medium.

```cpp
// effect_on_condition.cpp:360-369
d.amend_callstack( "EOC: " + id.str() );
if( d.get_callstack().size() > 5000 ) { ...
dialogue d_eoc( d );
// npctalk.cpp:6503
void run_eocs( const std::vector<eoc_entry> &eocs, dialogue &d, dialogue newDialog, ...
```

**Fix:** (a) Replace the string callstack with a small vector of ids plus an int depth, pushed/popped with a scope guard; build text only for debug output. (b) Take `dialogue &` (or move) in `run_eocs` / `queue_eocs`. (c) `if( eocs.empty() ) return;` in `run_eoc_vector` before copying. (d) Add an `activate` overload that skips the internal copy for callers that already copied. (e) Medium term, keep one talker per actor in `dialogue` instead of a separate const clone.

### EOC-P02 · `u_location_variable` allocates a full ~1.4 MB map on every call, even inside the bubble
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:4530`, `src/map.h:492`, `src/map.cpp:353`, `src/map.cpp:363`, `src/pathfinding.h:134`, `src/npctalk.cpp:5034`, `src/npctalk.cpp:3946`, `src/npctalk.cpp:3977`

`f_location_variable` calls `std::make_unique<map>()` before checking whether the target is in bounds. The default map constructor sizes the grid for 11×11×21 and allocates 21 zero-initialised 132×132 pathfinding caches, about 1.4 MB, on each call (about 0.1-0.3 ms). The effect has 438 uses, mostly in spawn/storm/encounter EOCs. Related cold paths: `f_transform_line` always loads a full map, and out-of-bubble `map_spawn_item` loads a tinymap per spawned item.

```cpp
// npctalk.cpp:4529-4534
map *here_ptr = &get_map();
std::unique_ptr<map> distant_map = std::make_unique<map>();
if( !here_ptr->inbounds( abs_ms ) ) { distant_map->load( ... )
```

**Fix:** Construct only in the out-of-bounds branch (`std::optional<map>` / `emplace()`), ideally as a smaller map sized to `target_max_radius`. In `f_transform_line`, use `reality_bubble()` when both endpoints are in bounds, as `f_transform_radius` does. In `receive_item`, load the tinymap once per call.

### EOC-P03 · `switch` effect deep-copies a `talk_effect_t` for every matching case on every run
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:7284`, `src/npctalk.cpp:7286`, `src/npctalk.cpp:7290`, `src/npctalk.cpp:8553`

The `switch` effect (458 uses) default-constructs a `talk_effect_t` and copy-assigns every case whose threshold is ≤ the value. Cases are usually ascending, so most are copied before the last match wins, and each copy duplicates a vector of `std::function` effects with large captures.

```cpp
// npctalk.cpp:7286-7293
talk_effect_t case_effect;
for( const std::pair<dbl_or_var, talk_effect_t> &case_pair : case_pairs ) {
    if( switch_int >= case_pair.first.evaluate( d ) ) { case_effect = case_pair.second; }
}
case_effect.apply( d );
```

**Fix:** Keep a `const talk_effect_t *chosen` and apply it. For the no-match case, apply a static default `talk_effect_t` to keep today's `add_opinion(0)` / hostility check behaviour.

### EOC-P04 · `u_spawn_monster` builds and copies whole monster objects just to read `type->id`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:7813`, `src/npctalk.cpp:7842`, `src/npctalk.cpp:7858`, `src/npctalk.cpp:7869`, `src/npctalk.cpp:7887`, `src/npctalk.cpp:7890`, `src/npctalk.cpp:7917`, `src/npctalk.cpp:7920`, `src/npctalk.cpp:7923`, `src/npctalk.cpp:7925`

`f_spawn_monster` default-constructs a `monster`, builds a new `monster(...)` per group spawn, or copy-assigns a live monster in copy mode, but only ever reads `target_monster.type->id`. Spawns are rare, so the cost is small.

```cpp
// npctalk.cpp:7887-7890, 7925
target_monster = monster( MonsterGroupManager::GetRandomMonsterFromGroup( target_mongroup ) );
target_monster = *copy->as_monster();
g->place_critter_at( target_monster.type->id, spawn_point );
```

**Fix:** Track an `mtype_id` instead of a `monster` object. This rewrite is also the natural place to add the `is_monster()` filter from EOC-B01.

### EOC-P05 · `u_run_inv_eocs` / `map_run_item_eocs` copy `item_search_data` and re-evaluate ids for every item
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctalk.cpp:3335`, `src/npctalk.cpp:346`, `src/npctalk.cpp:351`, `src/npctalk.cpp:472`, `src/npctalk.cpp:3307`, `src/npctalk.cpp:3353`, `src/npctalk.cpp:7070`, `src/npctalk.cpp:7073`, `src/npctalk.cpp:7124`, `src/npctalk.cpp:7146`

`is_true_item` iterates `item_search_data` by value for every visited item, including nested contents. The non-const `check()` re-evaluates every `str_or_var` list into fresh vectors per item, checks the cheap worn/wielded filters last, and builds a `const_dialogue` with two talker clones per item when a condition exists. The menu lambdas capture the dialogue by value and are stored as `std::function` members, costing several dialogue copies per call even in `all` / `random` mode. Hot mod example: aftershock jetpacks on `avatar_moves`, gated by a worn-flag check. A few allocations per item per call.

```cpp
// npctalk.cpp:3335, 7070
for( item_search_data datum : data ) { if( datum.check( guy, loc, d ) ) {
const auto f = [d, guy, title]( const item_location_filter & filter ) {
```

**Fix:** Iterate by `const &` and make `check()` const. Resolve the `str_or_var` lists once per call before visiting items. Order cheap checks first, and visit only worn/wielded items when those filters are set. Capture `&d` in the menu lambdas, or build them only for manual modes.

### EOC-P06 · `eoc_events::notify` rebuilds context and talkers per listener and copies whole EOCs into its cache
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/effect_on_condition.cpp:595`, `src/effect_on_condition.cpp:602`, `src/effect_on_condition.cpp:607`, `src/effect_on_condition.cpp:613`, `src/effect_on_condition.cpp:626`, `src/effect_on_condition.cpp:633`, `src/effect_on_condition.h:45`, `src/overmapbuffer.cpp:1534`, `src/overmap.cpp:3887`

Every event goes through `notify`, including `avatar_moves` and `character_gains_effect` / `character_loses_effect`. For each listening EOC it rebuilds the context map from the event payload and builds a dialogue from fresh talker clones, which `activate()` then copies again. When the event has no talker, the alpha is resolved by `g->find_npc` (linear over loaded overmaps), once per event. The cache stores full copies of each EVENT EOC in a `std::map` keyed by a dense enum. With realistic mod sets (a few dozen listeners), this is tens of microseconds per step.

```cpp
// effect_on_condition.cpp:626-633
global_variables::impl_t context;
for( const auto &val : e.data() ) { context[val.first] = diag_value{ val.second }; }
d = dialogue( alpha->clone(), beta ? beta->clone() : nullptr, {}, context );
```

**Fix:** Build the context, resolve the alpha (check `get_avatar().getID()` first, then the active-NPC tracker) and construct one dialogue before the listener loop. Store pointers or ids in a `std::array<std::vector<...>, num_event_types>`. Make `potential_alphas` a static array.

### EOC-P07 · `u_near_om_location` recomputes invariants for every OMT in radius
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/condition.cpp:1024`, `src/condition.cpp:1031`, `src/condition.cpp:1032`, `src/condition.cpp:1033`, `src/condition.cpp:1034`, `src/overmapbuffer.cpp:889`

For every point it re-evaluates `location` (an 18-character `exodii_base_x0y3z0` is a heap copy), calls `mapgen_args` (needed only for `FACTION_CAMP_START`, and called with the wrong position, see EOC-B08) and builds `omt_str`. Data uses `"range": 180` (about 130k points) in 12h/24h recurring EOCs, so these are cold; at that size `overmap_buffer.ter()`, which can generate missing overmaps, dominates.

```cpp
// condition.cpp:1029-1034
for( const tripoint_abs_omt &curr_pos : points_in_radius( omt_pos, range.evaluate( d ) ) ) {
    const oter_id &omt_ter = overmap_buffer.ter( curr_pos );
    const std::optional<mapgen_arguments> *maybe_args = overmap_buffer.mapgen_args( omt_pos );
    std::string location_value = location.evaluate( d );
```

**Fix:** Evaluate `location` once and branch outside the loop. Call `mapgen_args( curr_pos )` only in the camp branch. Compare resolved `oter_type` ids rather than strings. Consider `ter_existing` for large ranges to avoid generating overmaps.

### EOC-P08 · `u_has_item` and `repeat_responses` scan the inventory twice
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/condition.cpp:705`, `src/npctalk.cpp:9507`, `src/visitable.cpp:1300`, `src/visitable.cpp:1350`, `src/visitable.cpp:1366`, `src/visitable.cpp:1502`

Both evaluate `charges_of( id ) > 0 || has_amount( id, 1 )`. `charges_of` does a full traversal plus external pools, and for non-charge items returns 0, so `has_amount` traverses again. A repeat response with 54 items (godco foodguard.json) means 108 traversals per display. The condition also builds the `itype_id` twice. Both are interactive dialogue paths, so impact is unlikely to be noticeable.

```cpp
// condition.cpp:705-706
return actor->charges_of( itype_id( item_id.evaluate( d ) ) ) > 0 ||
       actor->has_amount( itype_id( item_id.evaluate( d ) ), 1 );
```

**Fix:** Evaluate the id once, then `item::count_by_charges( id ) ? has_charges( id, 1 ) : has_amount( id, 1 )`. For large `for_item` lists, collect present type ids in one `visit_items` pass.

### EOC-P09 · Dialogue UI redraws the whole UI stack every 10 ms and rebuilds sidebar text each frame
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/npctalk.cpp:2934`, `src/npctalk.cpp:2951`, `src/dialogue_imgui.cpp:261`, `src/dialogue_imgui.cpp:311`, `src/dialogue_imgui.cpp:320`, `src/dialogue_imgui.cpp:331`, `src/character.cpp:6659`, `src/talker_npc.cpp:795`, `src/ui_manager.cpp:603`

The dialogue sets a 10 ms input timeout, and every loop iteration, timeouts included, calls `invalidate_all_ui_adaptors()` + `redraw()`, so the uncovered game view and sidebar are redrawn about 100 times per second. The sidebar recomputes `short_description()` (item `tname()`s, visible mutations), `evaluation_by()` (with `calc_needs_rates()`), personality and opinion text each frame, though they only change when the conversation advances. Caveats: `redraw_invalidated()` redraws imgui adaptors every frame anyway, so only the non-imgui invalidation is extra, and a code comment says invalidate-all works around stacked windows. Cost is unmeasured.

```cpp
// npctalk.cpp:2934-2936
ui_manager::invalidate_all_ui_adaptors();
ui_manager::redraw();
action = ctxt.handle_input();
```

**Fix:** Cache the sidebar strings in `dialogue_imgui_impl` and refresh them once per `opt_imgui` call. Replace the per-tick invalidate-all with `redraw_invalidated()`, invalidating everything only on topic change; test for the stacked-window issue.

### EOC-P10 · `str_or_var` ids are copied and re-interned on every evaluation, often several times
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/condition.cpp:433`, `src/condition.cpp:501`, `src/condition.cpp:504`, `src/condition.cpp:705`, `src/condition.cpp:894`, `src/condition.cpp:967`, `src/npctalk.cpp:5591`, `src/npctalk.cpp:5643`, `src/dialogue_helpers.cpp:184`, `src/string_id.h:135`, `src/string_id.cpp:24`

Most conditions do `trait_id( x.evaluate( d ) )` (or `efftype_id`, `flag_id`, ...) per call. `evaluate()` returns a `std::string` by value, and interning goes through `unordered_map::emplace`, which in libstdc++ allocates a node before lookup, so each conversion costs a malloc/free even for known ids. Heavy users: `u_has_trait` (4,266 uses), `u_has_effect` (2,252), `u_has_flag` (865). Several lambdas evaluate the same value twice or more (`f_has_flag`, `f_has_item`, `f_has_effect`, `f_has_any_effect`, `f_need`; `f_message` up to 10 times). Each call is tens of ns and core has few high-frequency EOC paths.

```cpp
// condition.cpp:433, 501-504
return d.const_actor( is_npc )->has_trait( trait_id( trait_to_check.evaluate( d ) ) );
if( json_character_flag( trait_flag_to_check.evaluate( d ) ) == json_flag_MUTATION_THRESHOLD ) {...
return actor->has_flag( json_character_flag( trait_flag_to_check.evaluate( d ) ) );
```

**Fix:** Add an `id_or_var<Id>` helper that resolves constant strings to a typed id at load and only interns for variable inputs; migrate the factories site by site. Evaluate once into a local where a value is used twice. Map constant `f_message` types to `game_message_type` at load.

### EOC-P11 · Condition and effect JSON parsing probes hundreds of keys per object at load
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/condition.cpp:2766`, `src/condition.cpp:2815`, `src/condition.cpp:2823`, `src/condition.cpp:2844`, `src/condition.cpp:128`, `src/condition.cpp:2901`, `src/npctalk.cpp:8760`, `src/npctalk.cpp:235`, `src/npctalk.cpp:9333`

`conditional_t( JsonObject )` pre-scans ~170 `complex_conds` keys with `has_member` (skipped for and/or/not), then walks ~91 parsers with up to 4 lookups each for alpha and beta keys, then ~100 `simple_string_conds`. A `math` leaf (22,756 occurrences) matches at parser #87 after about 200 lookups; and/or/not nodes do about 270 lookups for nothing. `parse_sub_effect` and `dynamic_line_t` follow the same pattern. Estimated a few hundred ms per startup, a small fraction of total load time. The pre-scan also sets `found_sub_member` for non-math parsers, and table order defines precedence, so any rewrite must preserve both.

```cpp
// condition.cpp:2815-2831
for( const std::string &sub_member : dialogue_data::complex_conds() ) { if( jo.has_member( sub_member ) ) {
for( const condition_parser &p : parsers ) { if( p.has_beta ) { if( p.check( jo ) ) {... } else if( p.check( jo, true ) ) {
```

**Fix:** Build static `unordered_map<string_view, parser*>` tables (alpha and beta keys, table index). Iterate the object's own 1-3 members, look each up, keep the match with the lowest index, then check jarg types only for that match. Reuse the maps for `simple_string_conds` and `dynamic_line_t`.

## Appendix: considered and rejected

- Every NPC load/creation iterates all EOCs and builds a `std::map` of recurring ids – correct description, but the path is cold (NPC reload at most every 5 game minutes, 135 recurring EOCs) and dwarfed by the NPC catch-up and item generation around it.
- `process_effect_on_conditions` allocates talkers for every Character every turn even with nothing due – two small heap objects per active character per turn inside a `process_turn` that already does far heavier work; negligible.
- `compare_string` builds an `unordered_set<std::string>` on every evaluation – true, but almost all uses are in NPC dialogue or rare events, and the operands already allocate; negligible.
- `activate_activation_only` builds `std::string` temporaries for debug text on per-turn paths – the temporaries are noise next to the cost of `activate()` itself (covered by EOC-P01), and per-turn uses are few.

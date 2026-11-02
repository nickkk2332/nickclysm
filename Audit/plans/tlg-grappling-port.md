# Port plan: player grabs, throws and grapple martial arts from Cataclysm: The Last Generation

**Source.** Cataclysm: The Last Generation (TLG), <https://github.com/Cataclysm-TLG/Cataclysm-TLG>, read at commit `db141c6` (2026-11-01). The work is by worm-girl, mainly TLG PRs #26 (grappling and throwing, part 1), #58 (part 2), #281 (grapple techniques) and #428 (tuning and fallback attacks), plus later fixes (#1175, #1692, #1798, #1799, #1845, #1851, #2408, #3012). Both projects are CC BY-SA 3.0, so the code can be ported with credit. Each commit that ports TLG code should name the TLG PR and its author.

**Reference only.** Cataclysm-BN ported the same feature in BN PR #9108 and added a manual technique menu. BN's code base split from DDA in 2020, so it is used here for design ideas only.

The full inventory, with `file:line` references into TLG, BN and our tree, is in [tlg-grappling-research.md](tlg-grappling-research.md). This document is the plan.

## What TLG adds, from the player's side

- **Grab (G).** Next to a creature with empty hands, press G. That costs a turn and arm stamina, and needs at least 400 stamina. Then comes a melee hit roll the target can dodge. On a hit the target is held: it can't move or dodge, and it tries to break free every turn. The player gets -8 dodge while holding.
  - Grab strength scales with arm strength, grip, unarmed and melee skill, and size (about 18 for a fresh survivor; a zombie's grab is 20).
  - Neutral NPCs ask for confirmation. Followers and pets let you grab them.
  - Press G again to let go.
- **Hold.** The hold ends on any of these:
  - the target moves away, dies or teleports
  - you drop below 400 stamina, get winded or fall asleep
  - you wield something, mount a creature or are flung
  - the target breaks free; every break-free attempt costs you stamina
- **Drag.** Walking while holding drags the target into the tile you left. The odds depend on its weight against your grab strength and size. Followers who are walking with you are easier to drag.
- **Throw (t while holding).** You pick a tile on a targeting screen that shows the throw force. Range comes from arm strength, size difference, and unarmed and throwing skill. A short throw is a gentle shove; a full-range throw can break walls or kill. Thrown creatures fly over traps, set off a trap where they land, and can be thrown off roofs. Flying creatures recover in the air.
- **Martial arts.** Several techniques require holding the target:
  - Judo: throw, disarm and back throw.
  - Pankration: grab knee and grab disarm.
  - Zui Quan: a drunken throw that knocks you down too.
  - Brawling: a strangle that ignores armor.

  The default punch, kick and headbutt become fallbacks, used only when your style has nothing better. Headbutts happen only in a grapple, and you can't kick while holding someone.
- **Monsters.** A grab can pull you off a moving bike or out of a car without a seatbelt (#1175). A grabbed feral can't use two-handed or ranged weapons. Grab attacks respect a new `NO_GRAB` effect flag. A new `GRAB_IMMUNE` monster flag exists but nothing uses it yet.
- **Crowd crush.** Retuned. It needs more grabbers (4 around a medium character), counts only grabbers of a comparable size, and is skipped underwater. There is a small chance to resist, based on breathing, posture and some traits.

**TLG has no manual technique choice.** No menu, option or key lets the player pick a technique. You pick one indirectly, by grabbing first, which makes grab-only techniques eligible. The "manual combat" menu is BN's addition. Phase 9 covers it as our own feature.

TLG also has no automated tests for any of this, and some of its code should not be copied (see "Bugs not to copy").

## Where our tree stands

- **Grabs.** `ACTION_GRAB` only grabs furniture and vehicles; there is a `TODO: grab mob?` at `src/handle_action.cpp:754`. Monsters can grab characters (DDA's code), but nothing lets a character hold a creature.
- **Escape code** assumes every grabber is a monster (`src/character_escape.cpp`, `monster::move_effects`). As it stands, a player-held grab would be cleaned up as an orphan on the target's first turn.
- **Fling.** `game::fling_creature` is the audited version: it fixes the landing tile and the trap trigger, and treats characters as walls (LOOP-B01). TLG's version still has the landing-tile bug, so we keep ours and port only the additions.
- **Martial arts are mostly JSON already.** Grab-gated techniques are plain `condition`s in TLG. Our engine evaluates the same conditions and also has `post_hit_eocs` and MA event EOCs with the opponent as `npc_` (CMBT-J01). The C++ we still need is `fallback`, `basic`, and tech effects with `intensity` and a self target.
- **Missing pieces.** The `choking`, `strangulation` and `airborne` effects, the `NO_GRAB` and `GRAB_IMMUNE` flags, and the `EXPLOSIVE_EXERCISE` activity level do not exist here.
- **An existing bug in our tree** (also in TLG). `monster::remove_grab` re-adds `effect_grabbing` instead of removing it (`src/monster.cpp:2871-2877`).
- **Related open audit findings.**
  - CHAR-B09: grab escape treats non-adjacent grabbers as orphans.
  - CHAR-B05: `can_fly()` rolls grab escapes.
  - MONDT-B15: `longpull` divides by zero at grab strength 0.
  - CMBT-P10: technique checks deep-copy every technique.
  - CMBT-B02: tech effects without a condition were dead. It is already fixed, so TLG's unconditioned tech effects will work here.

## Decisions to make first

Each decision has a recommendation. Phases 1 to 3 need the first two settled.

1. **How a grab is stored.** TLG keeps a non-owning pointer on the grabber, never saves it, and matches "my grab" to the target's effect by equal intensity. That can collide with a monster grab of the same strength, and it dangles if the target is removed other than by dying.
   - **Recommended:** put `effect_source(grabber)` on the target's `grabbed` effect, and find the grabber by adjacency plus that source. Effects and their sources are already saved, so a grab survives save and load with no new save fields.
   - **Alternative:** a `grapple_state` on the grabber with a weak handle (the `mounted_creature` pattern).
2. **Unarmed only?** TLG requires empty hands; its own plan lists knives, chains and whips as future work.
   - **Recommended:** keep "unarmed only" at first, but write it as a JSON `grab_condition` so it can be loosened without C++.
3. **Activity level.** TLG adds `EXPLOSIVE_EXERCISE` and moves all melee to it. That is a global calorie and weariness rebalance.
   - **Recommended:** do not change it as part of this port. Grapple costs use `EXTRA_EXERCISE`; Phase 11 is optional.
4. **Crowd crush.** Our tests pin the current thresholds.
   - **Recommended:** use BN's world option (default on, so nothing changes) plus a `CROWD_CRUSH_RESIST` enchantment value. TLG's thresholds become JSON constants, and TLG's trait checks move into mutation JSON.
5. **Manual technique menu.**
   - **Recommended:** yes, as Phase 9, after the rest works. The menu opens *before* the hit roll; BN's opens after it, so cancelling there acts as a free re-roll.
6. **Fallback attacks.** Marking punch, kick and headbutt as `fallback` changes every unarmed fight, not just grappling. Aftershock and Isolation Protocol grant these techniques through their own body parts, so the change reaches them too.
   - **Recommended:** port it, and call it out in the changelog.

## Phases

Sizes are rough, and tests are counted separately. Labels: **[TLG]** ports TLG behavior, **[BN]** borrows BN's design, **[NEW]** is ours.

| # | Phase | Source | C++ | JSON / docs | Tests | Needs |
|---|---|---|---|---|---|---|
| 0 | Groundwork and existing grab bugs | NEW | 40 | – | 80 | – |
| 1 | Grab state, release paths, save/load | TLG + NEW | 250 | 20 | 150 | 0 |
| 2 | Player grab action | TLG | 180 | 30 + docs | 120 | 1 |
| 3 | Target reactions and break-free | TLG | 150 | – | 120 | 2 |
| 4 | Dragging | TLG | 120 | – | 80 | 3 |
| 5 | Throwing, and fling additions | TLG + BN | 350-450 | 30 | 150 | 3 |
| 6 | Martial-arts integration | TLG + NEW | 120 | 300 + 60 docs | 150 | 2 (JSON), none (C++) |
| 7 | Monster grab changes (vehicle yank, NO_GRAB) | TLG | 70 | 40 | 60 | 0 |
| 8 | Crowd crush | TLG + BN | 70 | 30 | 80 | 0 |
| 9 | Manual technique menu | BN design, our code | 400 | 10 | 100 | 6 (C++ part) |
| 10 | EOC and event surface | NEW | 200 | 80 docs | 100 | 1, 2, 5 |
| 11 | EXPLOSIVE activity level (optional) | TLG | 40 | – | 40 | – |

The total is about 2,000 lines of C++, 500 of JSON, 250 of docs and 1,300 of tests. Phases 0-6 form the playable feature. Phases 7 and 8 can run in parallel with them, and 9-11 come after.

### Phase 0: groundwork

- Fix `monster::remove_grab` so it removes `effect_grabbing`.
- Stop a monster's own tile from counting in the GRAB_FILTER persistence check.
- Give characters a real `grab_strength` for `u_val`/`n_val('grab_strength')`. Today only monsters return one, so techniques that compare against it read 0 for a character grabber.
- Fix CHAR-B09 (non-adjacent grabbers treated as orphans) here, since Phase 3 rewrites the same code.

### Phase 1: grab state and release

- `Character::release_grapple()`, per-turn upkeep, and every release hook:
  - the hold breaks: target not adjacent, either side incorporeal, stamina under the floor, winded or asleep, seizure, either side dead
  - the grabber acts: wields an item, mounts, swaps character, captures the target in a pet carrier
  - something moves a party: a fling, a knockback technique, a teleport
- The effect-source link from decision 1, with orphan cleanup, so a forgotten path degrades to "no target" instead of a dangling pointer.
- The release list, stamina floor and effect ids are JSON settings, not literals.

### Phase 2: the grab action

- TLG's creature branch of `grab()`:
  - ramp and water checks, hit roll, the aquatic penalty, the target's defensive reaction
  - confirmation prompts and who tolerates a grab
  - `NO_GRAB` and `GRAB_IMMUNE`
- Two TLG TODOs to finish:
  - skip body parts that are already held
  - refuse a second grabber on a monster that is already held, or roll against it
- **JSON.** Grab strength becomes a `jmath` function (u = grabber, n = target) whose default reproduces TLG's formula. A `grab_condition` lets mods add rules such as "no grabbing blobs" without C++.

### Phase 3: target reactions and break-free

- Escape code that recognizes character grabbers.
- TLG's minimum hold time, `1 + 100/speed` turns.
- A stamina cost to the grabber per escape attempt.
- Monster anger, morale and aggro.
- Followers never resist.
- A held monster has 0 dodge.
- Our escape code carries audit fixes, so this is a hand merge, not a paste.
- **JSON.** The hold time and the stamina cost formula become `jmath` functions. An optional monster `grab_escape` field gives the escape chance.

### Phase 4: dragging

- A drag step in `game::walk_move`, using TLG's drag odds plus the ally and pet discounts.
- Unlike TLG, move the target through the normal path, so traps, fields and vehicle boarding apply to it.
- Recompute the target's tile in absolute coordinates, because the player's move can shift the map.
- Holding a creature and holding an object stay mutually exclusive.

### Phase 5: throwing

- The creature branch of `plthrow`:
  - gates and confirmations
  - force scaled by the chosen distance
  - anger
  - targeting through a `ThrowCreature` target mode with a throw-force readout
- **Stamina.** Fix TLG's cost, which is a flat 50 because of a sign error. Use the intended weight-scaled cost, or BN's `clamp(force*10, 100, 1500)`.
- **Fling additions.** Release the holder, and add an `airborne` effect that skips trap prompts and forces monster movement. TLG's size-based collision damage to characters and its softer landing at low speed are optional, behind parameters.
  - `fling_creature` is shared with hulks, vehicles and spells, so every new behavior is opt-in, and the existing LOOP-B01 tests must keep passing.
- **JSON.** Throw force becomes a `jmath` function, and the "shove / toss / throw / hurl / launch" words and thresholds move to JSON.
- New `u_fling`/`npc_fling` EOC effects and a `fling` spell effect (TLG has `fling_away`) give modders the throw engine without grappling.

### Phase 6: martial arts

- **C++.**
  - `fallback` and `basic` on techniques, with TLG's three-pool `pick_technique`.
  - `intensity` on tech effects.
  - A tech-effect `"target": "self" | "target"`, accepting TLG's `hit_self` as an alias.
  - Load-time checks and docs.
- **JSON.**
  - The base punch, kick and headbutt as fallbacks with grapple conditions. TLG's headbutt condition covers both directions, so it replaces ours.
  - Judo throw, disarm and back throw; Pankration grab knee and grab disarm; the Zui Quan throw and restrictions.
  - The Brawling strangle, with the `choking` and `strangulation` effects.
  - The feral weapon gates.
- **[NEW]** Conditions `u_is_grabbing` / `u_is_grabbed_by` with an optional talker, because TLG's GRAB / GRAB_FILTER pair is ambiguous when someone else holds the target. Also `grab_strength` and `grab_part` context values for MA EOCs.

### Phase 7: monster grabs

- The vehicle yank, as a JSON option on the grab attack (`"pull_from_vehicle"` plus its formula), not hardcoded for every grab.
- `NO_GRAB` and incorporeal conditions on `grab`, `grab_2` and `grab_3`.
- TLG's shorter grab cooldown is a balance call, so it is left out by default.

### Phase 8: crowd crush

- TLG's rules, as JSON constants:
  - the underwater skip
  - size gating
  - walls count only past a threshold
  - fewer grabbers needed when the target is off the ground
- The resist chance becomes an enchantment value, with TLG's trait and posture modifiers moved into mutation and effect JSON. Our SUFFOCATION_IMMUNE fix stays.
- A world option turns it off.

### Phase 9: manual technique menu (our feature, BN's idea)

- **Mode.** A key toggles manual combat mode; BACKTAB is free in our default keymap. The mode is saved.
- **Menu.** When the player attacks in manual mode, a menu lists Basic attack, Automatic and every technique.
  - Unusable entries are greyed out with the reason, reusing `evaluate_technique`'s rejection checks, which today only print debug text.
  - Condition failures show the technique's `condition_desc`.
- **Timing.**
  - The menu opens before the hit roll, so cancelling reveals nothing and costs nothing.
  - A crit-only technique is either greyed out or "attempted", falling back to a basic attack if the crit doesn't happen.
- **Scope.** The menu never opens for NPCs, autoattack, auto-move or EOC `u_attack`; it uses BN's suppression-guard pattern.
- **Attack vector.** A forced technique gets a proper attack vector; today it gets none.
- **Optional.** Aiming knockback techniques with the Phase 5 targeting screen (BN's `controlled_knockback`).

### Phase 10: EOC surface

- **Effects.** `u_grab` / `npc_grab` (target, strength or formula, body part), `u_release_grab` and `u_throw_grabbed`.
- **Conditions.** `u_is_grabbing` and `u_is_grabbed`, with an optional other talker.
- **Events.** `character_grabs_creature`, `character_releases_grab` and `character_throws_creature`.
- With these, NPC grappling and mod martial arts can be written in JSON before any NPC AI work.

### Phase 11 (optional): EXPLOSIVE activity level

- Add the level, the diary column and the exertion maximum.
- Moving melee to it is a separate balance decision (decision 3).

## Bugs not to copy from TLG

1. **Throw stamina.** The cost is a flat 50: two sign errors (`avatar_action.cpp:1012`, `:1096`) disable the weight-based cost.
2. **Landing tile.** Flung monsters and NPCs land on, and trigger traps at, their *starting* tile; `pos` is only refreshed for the avatar. Our audited `fling_creature` already fixes this.
3. **Grab ownership by intensity.** "My grab" is matched by equal effect intensity, and the target pointer can dangle (see decision 1).
4. **Unread martial-arts state.** `forbid_effects_all` is never cleared on reload (the loader clears `_any` twice). `effect_pulled` is checked but never applied. `ma_technique::attack_override` is declared but never used.
5. **Self-counted grab.** The monster grab-persistence check counts the monster itself. This is fixed in Phase 0.

## Tests

TLG has none, so all of these are new. They go in `tests/grapple_test.cpp`, plus extensions to existing martial-arts and suffer tests.

**Determinism.** Each formula (grab strength, drag odds, throw force, escape chance, crowd thresholds) is a pure function or `jmath` id, tested with fixed inputs. Rolls are tested at inputs where the chance is 0 or 1. No test averages over many runs.

Per phase, briefly:

| Phase | Tests |
|---|---|
| 0 | `remove_grab` clears the effect, and a character's `grab_strength` reads correctly. |
| 1 | Every release path works, and a save/load round trip keeps the grab. |
| 2 | Each refusal leaves no state change, and a guaranteed hit sets both effects with the formula's strength. |
| 3 | The minimum hold time holds, and a follower never resists. |
| 4 | A drag across a map shift lands on the right tile. |
| 5 | A throw lands at the chosen distance, a trap triggers on the landing tile, and a ledge drop happens. |
| 6 | Fallbacks are picked only when nothing else is valid, and grab-only techniques only while holding. A self effect lands on the attacker. |
| 7 | Yank versus seatbelt. |
| 8 | Option off means no oxygen loss. |
| 9 | A reason string for each rejection, no menu for NPCs, autoattack or EOC attacks, and cancelling costs nothing. |
| 10 | Each EOC effect and condition, through TEST_DATA JSON. |

## Decisions (2026-11-02)

The owner decided the open questions as follows. Where a decision goes beyond the phases above, the design below is what gets built.

1. **Build everything, Phases 0-11, including the manual technique menu (Phase 9).** In addition, EOCs can open the technique menu:
   ```jsonc
   { "u_choose_technique": {
       "techniques": [ "tec_judo_throw", "tec_judo_disarm" ], // optional: limit the menu (strings or variables); default all of u's techniques
       "attack": true,                                         // default true: attack the beta talker with the chosen technique
       "store": { "context_val": "chosen_technique" },         // optional: write the chosen id (or "" when cancelled)
       "include_basic": true                                   // default true: offer "Basic attack"
     },
     "false_eocs": [ "EOC_ON_CANCEL" ] }                       // optional: run when the menu is cancelled
   ```
   The menu is shown only when u is the avatar. For an NPC, u's normal automatic choice is used, limited to `techniques` when given. `u_attack` keeps working as before.
2. **Any weapon, chosen per martial-art style in JSON.** Grappling stays unarmed by default. A style lists what may be held while grabbing:
   ```jsonc
   "grapple_weapons": { "categories": [ "KNIVES" ], "items": [ "chain" ], "any": false }
   ```
   While that style is active, a listed wielded weapon does not block a grab and does not release one. Grapple techniques can combine grab conditions with the usual weapon requirements, so knife-and-grab techniques are pure JSON. TEST_DATA and the docs carry an example style. Vanilla styles are unchanged unless a style is ported from TLG with its own grapple techniques.
3. **Fallback punch, kick and headbutt are opt-in.** The base techniques get `"fallback": true`. That only takes effect for an attacker with the character flag `MA_BASIC_FALLBACK`, which a style can grant through a static buff, as can a mutation or an effect. TLG's grapple rules for the base techniques (headbutt only in a grapple, no kick while holding) are written as JSON conditions guarded by the same flag. No vanilla style sets the flag, so vanilla unarmed fighting is unchanged.
4. **Crowd crush as recommended.** A world option, on by default. TLG's thresholds become JSON values, and resistance becomes an enchantment value (`CROWD_CRUSH_RESIST`). TLG's trait and posture modifiers move into mutation and effect JSON. Our SUFFOCATION_IMMUNE check stays.
5. **The EXPLOSIVE activity level is optional.** The level is always defined (constants, calorie diary). A world option, off by default, makes melee, dodging, grabbing and throwing use it as TLG does. With the option off, nothing changes.

Common to all phases:
- Grab ownership uses the effect-source model (decision 1 of "Decisions to make first").
- Tuning formulas are `jmath_function`s in `data/json/grappling.json`, with defaults that reproduce TLG's numbers.
- Every phase ships with deterministic Catch2 tests and docs.

## Status (2026-11-02): built

All phases, 0-11, are built, merged and tested; [the audit README](../README.md) ("Grappling and martial arts") lists what players and modders get. Where the build differs from the phases above:

- **Grab counters and style hooks (added on request).** Techniques can set `grab_counter`, which fires when something grabs the character, like `dodge_counter` and `block_counter`. Styles have `ongrab_*` and `ongrabbed_*` buffs and EOCs. Judo, Aikido and Krav Maga each have one grab counter.
- **`menu_visibility` (added on request).** It limits a technique's menu entry to grappling, being grappled, either, or a condition. Judo's throws, Pankration's grab knee and arm lock, and Zui Quan's drunken fall work whichever side holds the other.
- **Who holds whom.** The `u_is_grabbing` / `u_is_grabbed` conditions replace TLG's GRAB / GRAB_FILTER flag pairs wherever a technique means "this pair". That includes the 15 vanilla knockbacks that "may fail on enemies grabbing you".
- **Enemy NPCs.** They are not asked about before a grab (they are hostile already), but they don't tolerate it: the grab rolls to hit. TLG skipped the roll.
- **Throw stamina.** Opening the throw screen needs the stamina for a one-tile throw; the screen refuses distances you can't afford. TLG's rule that a throw must carry past one tile is kept.
- **Aimed knockback.** Only a technique chosen in the menu is aimed, never an automatic pick or a counter. That is stricter than BN.
- **Being held by a character.** It doesn't count as a hit on the held monster each turn (TLG called `on_hit`, which cast its hit_me spells and trained the grabber's weakpoint proficiencies every turn). Only its special defense may answer.
- **Not done.** Monster-on-monster grabs still have no source, so a monster victim's grabber is found by search, as before. NPC AI doesn't start grabs on its own; JSON can, with `u_grab`.

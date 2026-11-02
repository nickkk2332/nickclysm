# TLG grappling: inventory and research notes

These are the research notes behind [tlg-grappling-port.md](tlg-grappling-port.md). The code was read without building or editing anything.

The notes are a snapshot of the three trees at these revisions, taken before the port was built. Line references point into those revisions; `OURS:` references in particular are to `374ce76a`, so they no longer match the current tree, which has the port in it.

| Repo | Snapshot at |
| --- | --- |
| Cataclysm: The Last Generation (TLG), <https://github.com/Cataclysm-TLG/Cataclysm-TLG> | `db141c6` (2026-11-01) |
| This repository (`OURS`) | `374ce76a` |
| Cataclysm-BN, <https://github.com/cataclysmbn/Cataclysm-BN> (reference only) | `3634c6d` (2026-11-01) |

Paths below are relative to each repo root and prefixed `TLG:`, `OURS:` or `BN:`.
"Appears to" marks conclusions drawn from reading code that was not run.

---

## 0. Key findings

1. **TLG has no manual technique selection.** No uilist of techniques, no option, no keybinding.
   The only callers that force a technique are the `u_attack`/`npc_attack` EOC effect
   (TLG:src/npctalk.cpp:5457-5470) and reach attacks (TLG:src/melee.cpp:1102, 1205). The only
   martial-arts uilist is the style picker's description callback (TLG:src/martialarts.cpp:2243).
   TLG's keybindings add nothing for grappling: `grab` is still `G` (TLG:data/raw/keybindings.json:2643)
   and `throw` is still `t` (TLG:data/raw/keybindings.json:2871). In TLG, the player picks
   techniques only indirectly, by grabbing first, which makes grab-gated techniques eligible.
2. **The manual-combat technique menu comes from BN.** BN master has `manual_combat_mode`
   (BN:src/game.h:1211, saved in BN:src/savegame.cpp:98), which BACKTAB toggles
   (BN:data/raw/keybindings/keybindings.json:2574-2580). It also has a technique menu that greys out
   unusable entries and gives the reason (BN:src/melee.cpp:721-812, 893-1100). BACKTAB toggles the
   mode; it is not a target-selection key. Inside the targeting UI, BACKTAB is the ordinary
   `PREV_TARGET` key (BN:data/raw/keybindings/keybindings.json:1123-1130). See section 3.
3. TLG's grapple is mostly hardcoded C++ spread across about 15 files. State is one
   `Character::grab_data grab_1` (TLG:src/character.h:627-650). It holds a `std::shared_ptr<Creature>`
   with a no-op deleter (TLG:src/handle_action.cpp:697), so in practice it is a raw pointer.
   **It is never saved.** On load, `process_turn` clears the leftover effects
   (TLG:src/character.cpp:2682-2690).
4. TLG links "my grab" to "the victim's grabbed effect" by **equal effect intensity**
   (`eff.get_intensity() == grab_1.grab_strength`, TLG:src/character.cpp:1885, 2889;
   TLG:src/avatar_action.cpp:1085, 1091). Our repo already has `effect_source`, which is serialized
   with each effect (OURS:src/effect_source.h:19-44; OURS:src/creature.h:646-653). It is a sounder key:
   it survives save/load and does not collide.
5. Most of the martial-arts side is **JSON already in both codebases**. Grab-gated techniques are
   ordinary `condition`s on `u_has_flag: GRAB_FILTER` / `npc_has_flag: GRAB`. TLG adds only five
   small C++ fields: `fallback`, `basic`, and `tech_effects.{hit_self,intensity,req_flag}`. The rest
   of TLG's MA additions are unrelated to grappling (`forbidden_effects_*`, `natural_attack`) or
   unused (`attack_override` is declared at TLG:src/martialarts.h:206 and never read).
6. Our repo has only one piece of player-initiated grappling: monster grabs of characters, which are
   DDA's. `ACTION_GRAB` handles only vehicles and furniture (OURS:src/handle_action.cpp:650-756; see
   the `TODO: grab mob?` comment at 754). Our victim-side code assumes every grabber is a monster
   (OURS:src/character_escape.cpp:242, OURS:src/monster.cpp:2700). As it stands, a player-held grab
   would be removed as an "orphan" on the victim's first turn.
7. `EXPLOSIVE_EXERCISE` (12.0) does not exist in our repo (OURS:src/game_constants.h:145-170). TLG
   adds it (TLG:src/game_constants.h:156,166,177) and also switches all melee to it.
8. TLG has no Catch2 tests at all: its `tests/` holds only `catch/`, `data/`, `pch/` and `plot_speeds`
   (`git ls-files tests` returns 4 files). The test plan below is ours.
9. TLG bugs to avoid copying (details in 2.15):
   - The throw stamina cost is effectively a flat 50 (sign error).
   - Flung non-avatar creatures land on, and trigger traps at, their *start* tile.
   - `forbidden_effects_all` is never cleared on reload.
   - `effect_pulled` is checked but never applied.
   - The monster-grab persistence check counts the monster itself.
   - `monster::remove_grab` re-adds `effect_grabbing` instead of removing it. This one exists in
     both repos.

---

## 1. TLG inventory

### 1.1 Data structures and state

| Item | Where (TLG) | Notes |
| --- | --- | --- |
| `struct grab_data { shared_ptr<Creature> victim; efftype_id grab_effect_id; int grab_strength; bodypart_id grabbed_part; clear(); set(); }` | src/character.h:627-649 | `grab_effect_id` is declared but never set or read (grep shows no uses). |
| `grab_data grab_1;` member of **Character** (not Creature, not monster) | src/character.h:650 | NPCs could hold it, but nothing makes NPCs grab ("NPCs currently do not grapple", TLG:doc/CHANGELOG.md:48). |
| `void Character::release_grapple()` | decl src/character.h:1554; impl src/character.cpp:1879-1898 | Removes the victim's GRAB-flagged effects whose intensity equals `grab_strength`, removes all of the grabber's GRAB_FILTER effects, then calls `grab_1.clear()`. |
| `float Character::throwforce( Creature & ) const` | src/character.h:909; src/character.cpp:3329-3357 | Formula in 1.6. |
| `std::string Character::get_throw_descriptor( int )` | src/character.h:3452; src/character.cpp:1035-1052 | shove <30, toss <40, throw <60, hurl <70, otherwise launch (a 1-in-100 chance of "yeet"). |
| `bool Creature::grapple_drag( Creature *c )` | src/creature.h:522; src/creature.cpp:1753-1806 | Drag roll, see 1.5. |
| `int Creature::enum_size() const` | src/creature.cpp:262-270 | An identity map of the `creature_size` enum (medium = 3). |
| `void Creature::move_to( const tripoint_abs_ms & )` | src/creature.cpp:374-379 | Sets the position and calls `on_move`. Used to drag the victim. **Our repo already has the same function** (OURS:src/creature.cpp:309-314). |
| Victim marker | effect `grabbed` (flag `GRAB`); intensity = grab strength; bp = NULL for monsters, a random main part for characters | src/handle_action.cpp:759, 780 |
| Grabber marker | effect `grabbing` (flag `GRAB_FILTER`), intensity 1 | src/handle_action.cpp:760, 781 |
| Save/load | **Not saved.** `grab_1` appears nowhere in TLG:src/savegame*.cpp. After a load, the grabber's leftover `grabbing` effect is removed by `process_turn` (src/character.cpp:2682-2690). The victim's leftover `grabbed` effect is removed by the orphan check in `monster::move_effects` (src/monster.cpp:2615-2621) or `try_remove_grab` (src/character_escape.cpp:295-299). | |

### 1.2 Release paths (all TLG)

| Trigger | Where | Mechanism |
| --- | --- | --- |
| Press G again | src/handle_action.cpp:619-622 | `release_grapple()` |
| Target not adjacent, or either party incorporeal | src/character.cpp:2866-2874 (in `process_turn`) | `release_grapple()` |
| Victim null/dead, victim no longer carries a matching GRAB effect, or grabber stamina < 400 | src/character.cpp:2876-2912 | Removes GRAB_FILTER and clears `grab_1` (does not call `release_grapple`) |
| Orphan at turn start (victim null/dead) | src/character.cpp:2682-2690 | Same as above |
| Grabber winded or asleep | src/character.cpp:11758-11761 (in `process_effects`) | `release_grapple()` |
| Wielding any item | src/item.cpp:6562 (`item::on_wield`) | `release_grapple()` |
| Mounting a creature | src/character.cpp:1927 | `release_grapple()` |
| `swap_character` | src/character.cpp:704 | `release_grapple()` |
| Forced dismount while grabbing an object | src/character.cpp:2137 | `grab_1.clear()` (only inside the "grabbing an object" branch) |
| Grabber is flung | src/game.cpp:11641-11644 | `release_grapple()` |
| Knockback technique on the held victim | src/melee.cpp:2051-2054 | `release_grapple()` before each knockback step |
| Throwing the victim | src/avatar_action.cpp:1082-1121 | Removes both effects by intensity match, then `grab_1.clear()` |
| Victim breaks free (monster victim) | src/monster.cpp:2672-2679 | Removes the grabber's GRAB_FILTER and clears `grab_1` |
| Victim breaks free (character victim) | src/character_escape.cpp:395-397 | `grabber->grab_1.clear()` (the GRAB_FILTER effect is left for `process_turn` to clean) |
| Incorporeal victim (character) | src/character_escape.cpp:307-317 | `grab_1.clear()` |
| Victim dies (monster) | src/monster.cpp:3101-3127 | Finds an adjacent Character whose `grab_1.victim == this` and calls `release_grapple()` |
| Victim dies (NPC) | src/npc.cpp:3169-3194 | Finds any adjacent GRAB_FILTER creature and, if it is a Character, calls `release_grapple()` |
| Grabber dies (NPC) | src/npc.cpp:3196 | `release_grapple()` |
| Victim captured in a pet carrier | src/iuse.cpp:7430-7433 | `release_grapple()` |
| Myoclonic seizure (arm or hand) | src/player_hardcoded_effects.cpp:1753, 1756 | `release_grapple()` |
| Teleport | src/teleport.cpp:340-342, 380-381 | Strips GRAB effects from the teleported creature. The grabber's `grab_1` is released next turn by the adjacency check. |
| Vehicle boarding | No explicit hook | Dragging uses `Creature::move_to`, which neither boards nor unboards. A driven vehicle breaks adjacency, and the next `process_turn` releases the grab. |

### 1.3 Grab start: player presses G next to a creature

All of this is in `static void grab()`, TLG:src/handle_action.cpp:613-797:

- A ramp z-adjust applies to creatures as well (650-660).
- Hallucinations are skipped (661). A grab across a water surface fails with "You can't grab through the floor." (663-670).
- Gates, in order: armed → "You'll need your hands free for grappling." (671-674); `get_working_arm_count() < 1` or any effect flagged `NO_GRAB` (675-678); `get_stamina() < 400` (680-683); a monster with `GRAB_IMMUNE` (684-687).
- **Grab strength** (688-695):
  `grip = max(0.5, limb_score(grip))`
  `grab_strength = min(100, 1 + round(grip * (arm_str + int(unarmed + melee + enum_size()))))`
  Worked example: 10 arm strength, unarmed 2, melee 2, medium size gives 1 + (10+2+2+3) = 18. A zombie's
  `grab_strength` is 20, per TLG:doc/MONSTERS.md:58. Grip is the #1845 change.
- **Toleration** (699-722): allied, hostile, narcotized or legless NPCs, and pet monsters, "tolerate"
  the grab. Other NPCs prompt `"Really attack %s?"`. Non-hostile monsters go through
  `warn_player_maybe_anger_local_faction(true,false)`.
- **Cost** (724-728): `mod_moves(-100 - attack_speed(null_item)/weary_mult)`, with `weary_mult` taken at
  EXTRA_EXERCISE, then `burn_energy_arms(-120)`.
- **Monster target** (729-762): AQUATIC targets get a -8 accuracy penalty unless the player has
  PAWS_LARGE, CLAWS_TENTACLE, or THRESH_FISH while in water ("hand fishing"). The hit roll is
  `deal_melee_attack(you, hit_roll() + penalty)`, so dodging applies. A miss without toleration fails.
  On success the monster's `sp_defense` fires with `def_chance` probability. Then `grabbed`
  (permanent, 1 day, NULL bp, intensity = strength) goes on the victim, `grabbing` goes on the player,
  and `grab_1.set(...)`.
- **Character target** (763-788): only non-tolerating NPCs get a hit roll; a miss calls
  `on_attacked`. The body part is `random_body_part(true)`, with a TODO noting that already-grabbed
  parts are not checked (776-777). A non-tolerating NPC gets `on_attacked` after a successful grab too.

### 1.4 Maintenance, stamina and break-free

Grabber side (TLG:src/character.cpp:2866-2912):

- The grab persists only while the victim is adjacent and neither party is incorporeal.
- Stamina below 400 drops it with "You're too exhausted to maintain your hold."
- There is no passive per-turn drain (#2408).

Victim is a **monster** (`monster::move_effects`, TLG:src/monster.cpp:2599-2685):

- The grabber is found by any adjacent creature with GRAB_FILTER (2607-2613). Our repo additionally
  skips the monster's own tile (OURS:src/monster.cpp:2703-2706); TLG dropped that guard.
- A non-friendly monster calls `on_hit(grabber)`, gains anger for HURT and HOSTILE_CLOSE triggers,
  sets `aggro_character`, and loses morale for a HOSTILE_CLOSE fear trigger (2622-2639).
- **Minimum hold time**: the monster cannot try to escape until `1 + round(100/speed)` turns have
  passed (2641-2648).
- Escape roll: `x_in_y(melee_skill + melee_damage.total, grab_str)` (2649-2664). This is DDA's formula.
- **The grabber pays stamina for every attempt**:
  `clamp(max(1,100-grab_str) * (max(1, size_diff) + max(melee,dodge)), 100, 400)`, which also sets
  the grabber's activity level to EXPLOSIVE (2654-2662).
- On success the grabber's GRAB_FILTER is removed and `grab_1` cleared (2665-2681).

Victim is a **character** (`Character::try_remove_grab`, TLG:src/character_escape.cpp:227-420):

- The grabber is found among adjacent Characters by `guy->grab_1.grabbed_part == eff.get_bp()`
  (270-285), with the same minimum hold time (277-284). Monster grabbers are found as in DDA (286-291).
- Followers held by the avatar never resist (300-305).
- The escape formula is DDA's. Two small changes: slime gives 8% instead of 6% (355-364), and every
  attempt costs the grabber stamina as above (378-387).

Player held by monsters: DDA logic, with the TLG differences listed in 1.10.

### 1.5 Dragging

- Hook in `game::walk_move` (TLG:src/game.cpp:10399-10416), after moves and stamina are charged:
  - If the victim is dead, `grab_1.victim = nullptr` and the move fails.
  - If the victim is adjacent and `grapple_drag` fails, the move fails. The moves are already spent.
- After `place_player`, the victim is placed on the player's old tile with
  `victim->move_to(old_abs)` (TLG:src/game.cpp:10482-10485). That path does not run traps, fields,
  vehicle boarding or gravity for the victim.
- `Creature::grapple_drag` (TLG:src/creature.cpp:1753-1806):
  - The drag fails if the victim is tied, IMMOBILE or CANNOT_MOVE.
  - `odds = victim_kg / (grab_strength * grabber_size)`.
  - An ally or walking-with NPC that is not asleep cuts the odds by `run_cost/200` when that is
    under 1 (#1851, dragging friends). A pet monster halves the odds.
  - `threshold = clamp((odds+0.25)^2, 1, 1000)`; the drag succeeds on `x_in_y(1, threshold)`.
- Monsters dragging characters is DDA's `grab_data.drag_*` code, unchanged in substance.

### 1.6 Throwing a held creature (press `t` while grabbing)

The throw is in `avatar_action::plthrow`, TLG:src/avatar_action.cpp:995-1124. It runs before the
item-throw menu.

- `throwforce` (TLG:src/character.cpp:3329-3357):
  `strength_factor = arm_str > 10 ? arm_str/100 : 0`
  `size_factor = max(0, their_size - 3) * 15`
  `throwforce = 2 * (arm_str*(1.1 + strength_factor) + unarmed*2 + throw/4 + 2*(your_size - their_size) - size_factor)`
  - An active `bio_railgun` adds 16 against ROBOT/ROBOT_FLYING and 8 against CYBORG.
  - The result is multiplied by 0.25 if the victim stands in DEEP_WATER.
  - Worked example: 10 str, unarmed 2, throw 2, medium vs medium gives about 31, so range 3.
- `range = throwforce / 10` (1022).
- Gates:
  - `try_break_relax_gas` (1023-1026).
  - "can't muster the strength" if `range <= 1` or `their_size - your_size > arm_str/10` (1027-1032).
  - The stamina gate (1033-1036).
  - If the thrower is itself held by the victim, a `try_remove_grab()` must succeed first; failure
    costs moves (1037-1042).
- Targeting: `target_handler::mode_throw_creature(you, victim, range)` (1043-1047). A new
  `TargetMode::ThrowCreature` (TLG:src/ranged.cpp:227, 520-534) starts the cursor at the victim
  (3019-3023). The side panel shows a "throwforce" readout and Great/Good/Poor confidence
  (`draw_throwcreature_aim`, 2388-2425; `print_throwforce`, 2255). The move estimate is
  `100 + attack_speed` (2039-2040).
- **Force scaling**: `throwforce *= distance_chosen / range` (1052-1058). A short toss is a gentle shove.
- Reactions:
  - An NPC prompts "This will probably make %s angry" when force > 24 and the NPC is neither enemy
    nor ally, or when force > 60; the NPC then gets `on_attacked` (1062-1070, 1118-1120).
  - A non-friendly monster gets +15 anger with a HURT trigger and -15 morale with a fear trigger
    (1071-1081).
- Both grab effects are removed by intensity match (1082-1095). Then `fling_creature(victim, angle,
  throwforce, controlled=false)` (1115), `grab_1.clear()`, and moves and stamina are charged at EXPLOSIVE weariness
  (1122-1125).
- The stamina math at 1012-1013, 1033-1034 and 1096-1103 is broken; see 2.15. The intended design was
  weight-scaled.

### 1.7 `game::fling_creature` changes (#58, #1692)

TLG:src/game.cpp:11571-11812. Differences from our version (OURS:src/game.cpp:9143-9330):

| Aspect | TLG | Ours |
| --- | --- | --- |
| Airborne | Adds `airborne` (1 turn) at launch, removes it at `range <= 1` (11640, 11768-11770). `airborne` has the LEVITATION flag (TLG:data/json/effects.json:5672-5677). Consumers: monsters force-move (TLG:src/monmove.cpp:1913), the player skips trap and dangerous-tile prompts (TLG:src/game.cpp:10056, 12217), eye level (TLG:src/character.cpp:1460), boarding a fast vehicle (TLG:src/creature.cpp:291-295). | No `airborne` effect exists. |
| Grabber flung | Calls `release_grapple` (11641-11644). | n/a |
| Vehicle rotation | `obstructed_by_vehicle_rotation` steps through the corner (11659-11667). | Our `map` has no such function (checked). |
| Hitting a monster | Only if `flvel >= 30`. Stopping force `min(size*30, flvel)`. Flung creature takes `rng(force,2f)/6`. The obstacle takes `rng(flvel,2flvel)/6` minus its torso bash armor (11670-11690). | Force `min(1.5*hp, flvel)`. The obstacle takes `(damage - armor)*6`. |
| Hitting a character | Same as a monster: the character takes damage (11692-11711). | Treated like a wall with no damage to the character (an audit fix). |
| Walls | `min(rng(bash_resistance, bash_strength), flvel)`, divided by 6. | `min(bash_strength, flvel)`, divided by 9. |
| Path stepping | `pt = c->pos_bub() + tdir` (current position). | `pos` refreshed every step (OURS:src/game.cpp:9287-9289, audit fix loop-b01). |
| Landing tile | Uses `pos`, which is only refreshed for the avatar (11743-11752, 11779-11797). This appears to be a bug: monsters and NPCs land and trigger traps at their start tile. | Correct (audit fix). |
| Landing damage | Halved minus 5 if `controlled \|\| flvel < 30` (11787). | Only if `controlled`. |
| Water | SWIMMABLE tile → splash (11780). | DEEP_WATER. |
| Ledges | `thru && is_open_air(pos)` → `creature_on_trap` (11782-11783), the same as ours. With the stale-`pos` bug above this probably fails for monsters. | Correct. |

TLG also adds a `fling_away` spell effect (TLG:src/magic_spell_effect.cpp:1997-2010;
registered at TLG:src/magic.h:932). It is a JSON-reachable throw with force = spell damage. Our repo
has no equivalent (grep finds no `fling_away`). Our nearest JSON hook is the `u_knockback` EOC
(OURS:src/npctalk.cpp:7947, OURS:doc/JSON/EFFECT_ON_CONDITION.md:5336), which calls `g->knockback`,
a different code path.

### 1.8 Crowd crush

`suffer::while_grabbed` (TLG:src/suffer.cpp:342-440; called at 1829-1831 when the character has GRAB).
The same function exists in ours (OURS:src/suffer.cpp:355-424). TLG's retune:

| | TLG | Ours |
| --- | --- | --- |
| Underwater | Skipped (351-353) | Not skipped |
| Grabbers required | `max(2, size + 1)`, i.e. 4 for a medium character (358-360) | `max(1, size - 1)`, i.e. 2 for medium |
| Who counts | GROUP_BASH, not a hallucination, `your_size <= mon_size + 2`, `mon_size != tiny` (363-371) | Any GROUP_BASH |
| Walls | Count only if `crowd + walls > 6` (387-391) | Always count |
| Not on the ground | crowd - 1 (379-381) | n/a |
| Resist at minimum crowd | 5% base. Downed or prone without SLIMESPAWNER: -4. Load over 75%: -4. TRANSPIRATION or SLIMESPAWNER: +4. Active SHELL2/3: +20. Breathing limb score widens the roll (393-414). | None |
| SUFFOCATION_IMMUNE | Not checked | Checked (an audit fix) |

### 1.9 NPC and monster reactions

- NPC victims: the grab prompt, toleration and `on_attacked` are described in 1.3. Throw anger is in
  1.6. Followers never resist an avatar's grab (TLG:src/character_escape.cpp:300-305).
  Followers walking with the player are easier to drag (TLG:src/creature.cpp:1783-1789).
- Monster victims: anger and morale on every escape attempt, `aggro_character`, and the minimum hold
  time (1.4). The grab also triggers `sp_defense` (TLG:src/handle_action.cpp:756-758).
- No NPC AI was added to grab, break holds proactively, or throw.

### 1.10 Monsters grabbing players: TLG vs DDA (ours)

| Change | TLG | Ours |
| --- | --- | --- |
| Pulled off a vehicle by a grab (#1175) | The target is in the vehicle, the grabber is not and is not flying, and there is no seatbelt. If `rng(0, mph) + driver_ability < rng(0, grab_str)`, the target is unboarded, airborne, downed for 3 s, loses 1 s of moves, and takes impact if force > 15. `driver_ability` = driving + max(str, dex), plus swimming and athlete bonus for muscle engines. (TLG:src/mattack_actors.cpp:690-722) | Absent (OURS:src/mattack_actors.cpp:706-720) |
| Monster victim of a monster | Also adds `grabbing` to the grabber (725-727) | Same (OURS:716-719) |
| `effect_pulled` gate | Checked at 578-579 but never applied anywhere (dead code; the dummy effect is at TLG:data/json/effects.json:4128-4132) | Absent |
| Grab attack conditions | `grab`, `grab_2`, `grab_3` add `not u_has_flag NO_GRAB` and `not npc_has_effect incorporeal`; `grab` cooldown is 6 (TLG:data/json/monster_special_attacks/monster_attacks.json:62-135) | Cooldown 10, no NO_GRAB or incorporeal check (OURS:data/json/monster_special_attacks/monster_attacks.json:62-90) |
| NO_GRAB effect flag | json_flag defined (TLG:data/json/flags/flags.json:2496-2499). Checked by the player grab (handle_action.cpp:675), `nurse_operate` (monattack.cpp:2477) and the monster grab conditions. No effect in data/json carries it. | Absent |
| Grabbed ferals | Disarmed-feral weapon attacks require `not GRAB` / `not GRAB_FILTER`; fist and bite attacks fire while grappling (TLG:data/json/monster_special_attacks/feral_weapon_attacks.json, 46 GRAB references) | Our file has the `is_disarmed` gates without GRAB checks (OURS:data/json/monster_special_attacks/feral_weapon_attacks.json:4-34) |
| Monster dodge while grabbed | `get_dodge`: GRAB → 0, GRAB_FILTER → -5 (TLG:src/monster.cpp:2787-2793) | No GRAB handling (OURS:src/monster.cpp:2825-2842) |
| Character dodge while grabbed | `grabbed` effect has enchantment `DODGE_CHANCE -100`; `grabbing` has `DODGE_CHANCE -8` (TLG:data/json/effects.json:4113, 4141) | `base_mods dodge_mod -10` / `-8` (OURS:data/json/effects.json:4152, 4174). DODGE_CHANCE exists in ours (OURS:src/magic_enchantment.cpp:107). |

### 1.11 Restrictions on the grappled creature and the grabber

- Victim (characters only): limb-score mods are the same in both repos. TLG makes dodge impossible
  (-100). Monster victims: dodge 0 and cannot move until they break free (1.4).
- Grabber:
  - Must be unarmed (1.3); wielding releases the grab (1.2).
  - Dodge -8.
  - Kicks are forbidden while holding (`tech_base_kick` condition, TLG:data/json/techniques.json:47).
  - Headbutt is allowed *only* while in a grapple, in either direction (17-22).
  - Zui Quan fist and spin are forbidden while holding (TLG:data/json/techniques.json:3475, 3503).
- Monsters held by a monster are DDA behavior.

### 1.12 Martial-arts integration

**New C++ fields** (TLG:src/martialarts.h, loaded in TLG:src/martialarts.cpp):

| Field | Decl | Load | Use | Grapple-related? |
| --- | --- | --- | --- | --- |
| `ma_technique::fallback` | h:230 | cpp:314 | melee.cpp:1755-1777: a fallback only enters the pool when its negative-weighting roll *fails*. It is then used only if nothing else is possible, and it is allowed while unarmed even if the style needs a weapon. | Yes (#428) |
| `ma_technique::basic` | h:231 | cpp:315 | melee.cpp:1603-1615, 1622-1636: basics go into whichever pool wins (possible or fallback) and do not suppress fallbacks. | Indirect |
| `tech_effect_data::hit_self` | h:158 | cpp:225 | melee.cpp:1967-1969: the effect goes on the attacker | Yes (#281, Drunken Fall downs yourself) |
| `tech_effect_data::intensity` | h:156 | cpp:224 | melee.cpp:1968, 1971 | Generic |
| `tech_effect_data::req_flag` | h:161 | cpp:227 | melee.cpp:1966 | Generic. Ours already has a more general per-effect `condition` (OURS:src/martialarts.h:183-186). |
| `ma_requirements::forbid_effects_{all,any}` | h:128-129 | cpp:262-275 (bug: clears `_any` twice) | cpp:726-739 | No (used on buffs in TLG:data/json/martialarts.json). Ours can express it with `forbidden_char_flags`, because effect flags count as character flags. |
| `attack_vector::natural_attack` | h:87-88 | cpp:95 | cpp:1595, 1636-1676 | No (#2572) |
| `ma_technique::attack_override` | h:206 | never loaded | never used | No |

**Technique selection** (`Character::pick_technique`, TLG:src/melee.cpp:1583-1637;
`evaluate_technique`, 1640-1793). Ours is OURS:src/melee.cpp:1465-1496 and 1497-1620. There are
three pools: `possible`, `fallbacks` and `basics`. If `possible` is empty and `fallbacks` is not, the
choice is a random entry of fallbacks plus basics; otherwise possible plus basics. The selection is
otherwise identical to ours. Grapple gating lives entirely in JSON `condition`s, which the C++
evaluates via `const_dialogue(u = attacker, npc = target)` (TLG:src/melee.cpp:1660-1666; ours
1516-1523). Counter-technique exhaustion uses EXPLOSIVE in TLG (1705-1716) and EXTRA in ours (1558-1569).

**Fallback attacks**: JSON techniques granted per body part.
- `tech_base_headbutt` comes from head (TLG:data/json/body_parts.json:249).
- `tech_base_punch` comes from hands (600, 635) and mutated limbs (TLG:data/json/mutations/mutation_limbs.json:288 and others).
- `tech_base_kick` comes from feet (882, 917).
- Definitions: TLG:data/json/techniques.json:9-59. All three are `fallback: true` with negative weighting (-10/-8/-10).
- Ours: OURS:data/json/techniques.json:9-48. No fallback flag, positive default weighting. Headbutt requires a grabbed arm or hand on *you*.

**Grapple-gated technique JSON** (TLG:data/json/techniques.json):

| Id | Line | Requirement | Effect |
| --- | --- | --- | --- |
| `tec_judo_throw` | 1548 | Holding the target, same or +1 size, not downed, not blob/fish/snake, no flight | down 1, x1.25 bash, `vector_grasp` |
| `tec_judo_disarm` | 1574 | Same, unarmed 3 | Disarm, down 1 |
| `tec_judo_backthrow` | 1610 | Same, crit only, unarmed 2 | `side_switch`, down 1, stun 1, x1.5 bash |
| `tec_pankration_grabknee` | 2697 | Holding the target | x0.5 move cost, `vector_knee`. Ours requires a *downed* target instead (OURS:2586-2596). |
| `tec_pankration_grabdisarm` | 2710 | Holding the target, unarmed 4 | Disarm, arpen scaled by STR |
| `tec_zuiquan_throw` | 3514 | Holding the target, size +1 at most, not tiny | `downed` on **self** (`hit_self`), stagger on the target, down 3 |
| `tec_brawl_strangle` | 3616 | Holding, size +1 at most, a breathing mundane species | `choking` 100%, `strangulation` 50%, damage x0.2. Added to Brawling at TLG:data/json/martialarts.json:328. |
| Throws that can fail against grabbers (e.g. 198-212, 493-502) | — | `roll_contested` STR vs `n_val('grab_strength')` | Already present in ours (OURS:data/json/techniques.json:189-198 and others) |

`choking` and `strangulation` exist in TLG:data/json/effects.json:5432, 5450, but **not in ours**
(grep finds nothing). TLG:doc/EFFECT_ON_CONDITION.md:4895 mentions `tec_pankration_grabthrow`, which
is not defined anywhere in TLG's data. `n_val('grab_strength')` resolves through
`talker::get_grab_strength`. Only monsters override it, in both repos
(OURS:src/talker_monster.cpp:187; OURS:src/condition.cpp:2606), so a *character* grabber reads 0.

No TLG technique calls `fling_creature`. "Throw" techniques knock down or switch sides. Knockback
techniques release the hold first (TLG:src/melee.cpp:2051-2054). Knocking back your own grabber
removes its grab, which is DDA code also in ours (OURS:src/melee.cpp:1933).

### 1.13 Effects, flags, JSON, activity levels, keybindings, options, UI, docs and tests

- Effects (TLG:data/json/effects.json):
  - `grabbed` 4100-4127 (DODGE_CHANCE -100 enchantment).
  - `pulled` 4128-4132 (dummy, unused).
  - `grabbing`, `grabbing_2`, `grabbing_3` 4133-4162.
  - `airborne` 5672-5677.
  - `choking` 5432 and `strangulation` 5450.
- Flags:
  - `GRAB` and `GRAB_FILTER` exist in both repos.
  - `NO_GRAB` (json_flag, TLG:data/json/flags/flags.json:2496).
  - `GRAB_IMMUNE` monster flag (TLG:data/json/monsters/monster_flags.json:58; TLG:src/mtype.h:127, src/mtype.cpp:87, 209). No TLG monster uses it.
- Activity level: `EXPLOSIVE_EXERCISE = 12` (TLG:src/game_constants.h:156).
  - Added to the calorie diary (TLG:src/avatar.cpp:1601, 1678).
  - `maximum_exertion_level` returns it at weariness 0 (TLG:src/character.cpp:5538).
  - Melee, dodge (2267), grab maintenance and throwing use it.
  - Used in TLG:data/json/player_activities.json:137, 423, 430, 1127.
- Keybindings and options: none new (see section 0).
- UI and messages:
  - Grab: "You grab %s.", "You grab %1s by the %2s.", "You reach for %s, but fail to make contact!", "You're too exhausted to wrestle anything.", "You'll need your hands free for grappling.", "You can't grab %s.", "You release %s."
  - Maintenance: "You're too exhausted to maintain your hold."
  - Dragging: "You strain to drag %s."
  - Throwing: "You %1s %2s!" (descriptor), plus the railgun variant.
  - Fling: "You're almost sent flying, but you're held in place!"
  - Crowd crush: "You're being suffocated/crushed!".
  - Targeting panel: throwforce readout.
- Docs:
  - TLG:doc/CHANGELOG.md:46-48.
  - TLG:doc/MARTIALART_JSON.md:92-93 (`fallback`, `basic`) and 157 (`req_flag`).
  - No doc for `GRAB_IMMUNE`, `NO_GRAB` or `hit_self` (grep).
- Tests: none (section 0).

### 1.14 Repo-structure and API differences that affect a port

These are the differences observed:

- **Files split in our repo**:
  - TLG `character.cpp` pieces live in `character_health.cpp` (e.g. `can_recover_oxygen` OURS:259; melee stamina modifier OURS:104, 476), `character_body.cpp`, `character_inventory.cpp`.
  - `item::on_wield` is in OURS:src/item_gun_tool_ammo.cpp:639.
  - TLG line numbers will not map one to one.
- `Creature::enum_size()` does not exist in ours. Use `static_cast<int>(get_size())`; our enum starts at tiny = 1 (OURS:src/creature.h:75-86), the same as TLG's.
- Body-part type enum: TLG `body_part_type::type` vs ours `bp_type` (e.g. OURS:src/martialarts.h:112; OURS:src/suffer.cpp `get_random_body_part_of_type( bp_type::torso )`).
- `Creature::select_body_part`: TLG takes a `const Creature *you` first argument (TLG:src/creature.h:1314); ours does not (OURS:src/creature.h:1323).
- `monster::move_effects( bool, tripoint_bub_ms dest )` in TLG vs `move_effects( bool )` in ours (OURS:src/monster.cpp:2553).
- `item::reach_range()` returns a pair in ours (`.first`, OURS:src/melee.cpp:853).
- `cache_get_items_with` returns `item_location` in ours (OURS:src/martialarts.cpp:1501-1506) vs `const item *` in TLG.
- `tech_effect_data` is a positional constructor in TLG; ours is default-constructed with `condition` (OURS:src/martialarts.h:176-189; loader OURS:src/martialarts.cpp:237-253).
- `evaluate_technique` takes a `fallbacks` out-parameter in TLG; not in ours.
- MA event EOCs take `ma_event_context` in ours (OURS:src/martialarts.h:35-59, 393-417); TLG's don't.
- `map::obstructed_by_vehicle_rotation` exists in TLG (TLG:src/map.h:779), not in ours.
- No `EXPLOSIVE_EXERCISE` in ours.
- Identical in both: `burn_energy_arms`, `get_arm_str`, `exertion_adjusted_move_multiplier`, `attack_speed(const item&)`, `avatar::try_break_relax_gas`, `npc::is_walking_with`, `warn_player_maybe_anger_local_faction`, `random_body_part(bool)`, `Creature::is_adjacent(const Creature*, bool)`, `game::update_map(int&, int&)`, `map::creature_on_trap`, `map::is_open_air`, `mtype::sp_defense/def_chance`, `limb_score_grip`, `character_modifier_melee_stamina_cost_mod`.

### 1.15 TLG bugs and quirks not to port as-is

1. **The throw stamina cost is a flat 50.** At TLG:src/avatar_action.cpp:1012,
   `max(75, (weight_cost+50) * -1 * mod)` is always 75, because the second operand is negative.
   At 1096, `min(-50, 75 * throwforce/100)` is always -50. The check at 1027 (`max_stamina < -75`) can
   never trigger, and 1033 only requires 75 stamina. The weight-based cost never applies.
2. **Flung non-avatar creatures land at their start tile.** `pos` is refreshed only for the avatar
   (TLG:src/game.cpp:11743-11752), and landing damage, `is_open_air` and `creature_on_trap` use `pos`
   (11779-11797). Our repo already fixed this (OURS:src/game.cpp:9287-9289).
3. `forbid_effects_all` is never cleared on reload (TLG:src/martialarts.cpp:264 and 270 both clear
   `_any`).
4. `effect_pulled` is checked (TLG:src/mattack_actors.cpp:579) but never added.
5. Grab ownership is matched by intensity equality (see section 0, item 4). A monster grab on the same
   victim with the same strength is indistinguishable from the player's. The character-victim path
   matches grabbers by `grabbed_part` (TLG:src/character_escape.cpp:273). For monster victims both
   sides use the NULL body part, and a second grabber on the same part modifies the existing effect
   instead of adding one.
6. `shared_ptr` with a no-op deleter (TLG:src/handle_action.cpp:697). The pointer is not owning and
   cannot expire. TLG relies on the victim's `die()` to release the grab. Any other removal path, such
   as a debug kill, despawn or a mod spawning or removing creatures, leaves a dangling pointer that
   `process_turn` dereferences (TLG:src/character.cpp:2682, 2867).
7. Monster GRAB_FILTER persistence counts the monster itself (TLG:src/monster.cpp:2997-3004). This
   matters when it both holds and is held. The same code exists in ours (OURS:src/monster.cpp:3049-3066).
8. `monster::remove_grab` *adds* `effect_grabbing` when `grabbed_limbs` empties (TLG:src/monster.cpp:2829-2835;
   **also in ours**, OURS:src/monster.cpp:2871-2877). The intent is clearly `remove_effect`. Cleanup
   then falls to the per-turn persistence check.
9. The `as_npc()` dereference at TLG:src/character_escape.cpp:303 is only safe because the avatar is
   never held by the avatar.

---

## 2. What exists in our repo, piece by piece

| Piece | TLG | Ours | Gap |
| --- | --- | --- | --- |
| Grab action | Creatures, vehicles and furniture (TLG:src/handle_action.cpp:613-797) | Vehicles and furniture only (OURS:src/handle_action.cpp:650-756, dispatch 2617) | Creature branch, release-first, `GRAB_IMMUNE`, `NO_GRAB` |
| Grabber state | `grab_1` on Character | Nothing. `grab_point` / `object_type` exist for objects (OURS:src/character.h:609; OURS:src/avatar.h:285-286) | New state plus release hooks |
| Grab persistence | `process_turn` checks | Monster-only persistence (OURS:src/monster.cpp:3048-3066) | Character side |
| Victim break-free | Monster and Character grabbers | Monster grabbers only (OURS:src/character_escape.cpp:197-350; OURS:src/monster.cpp:2693-2734) | Find Character grabbers, minimum hold time, grabber stamina cost, anger |
| Death cleanup | Victim side and grabber side | GRAB_FILTER side only (OURS:src/monster.cpp:3155-3189); npc::die at OURS:src/npc.cpp:3364 | Release Character grabbers |
| Drag | `walk_move` and `grapple_drag` | Furniture/vehicle `grabbed_move` only (OURS:src/game.cpp:7812, 8995) | Creature drag |
| Throw | `plthrow` branch, `ThrowCreature` UI | `plthrow` items only (OURS:src/avatar_action.cpp:1018; ACTION_THROW OURS:src/handle_action.cpp:2736). Target modes have no creature throw (OURS:src/ranged.cpp:223-232); `mode_select_only(you, range)` exists (OURS:src/ranged.h:32). | Throw branch and targeting |
| Fling engine | `fling_creature` with airborne, release and collisions | Audited version (OURS:src/game.cpp:9143-9330); tests in OURS:tests/audit_game_loop_test.cpp:15-50 | Port only the deltas we want; keep our fixes |
| Hulk "smash" | `mattack::smash` (TLG:src/monattack.cpp:1349) uses `fling_creature` | Same DDA code | None needed |
| Crowd crush | Retuned | DDA version (OURS:src/suffer.cpp:355-424); tests OURS:tests/char_suffer_test.cpp:383-520 | Decide tuning; tests depend on the current numbers |
| Monster vehicle yank | Yes | No | Port |
| Grab attack JSON | NO_GRAB, incorporeal | Missing | JSON edit |
| `GRAB_IMMUNE` / `NO_GRAB` | Defined | Absent | Add flags |
| EXPLOSIVE activity | Yes | No | Optional |
| MA `fallback` / `basic` | Yes | No (OURS:src/martialarts.h:191-277) | Port |
| Tech effect on self / intensity | Yes | No. Ours has a per-effect `condition` instead of `req_flag`. | Port `hit_self` (or a `target` enum) and `intensity` |
| Grapple-gated techniques | JSON | Not present | JSON port |
| `choking` / `strangulation` effects | Yes | No | JSON port |
| Character `grab_strength` for math | No (returns 0) | No | Add (our improvement) |
| EOC hooks | Plain `eocs` | `eocs`, plus `post_hit_eocs` with an `ma_event_context` and MA event EOCs that get the opponent as `npc_` (OURS:src/martialarts.h:35-59, 276; OURS:doc/JSON/MARTIALART_JSON.md:85-115, 214-230); `u_attack` with a forced technique (OURS:src/npctalk.cpp:6090-6110) | Extend with grab and throw events and effects |
| Manual technique choice | None | None (only the `u_attack` EOC) | BN-style menu would be our addition |

---

## 3. Manual technique selection: TLG has none; BN reference

**TLG: none.** Evidence:

- `pick_technique` is called only from melee (TLG:src/melee.cpp:779), block counters (2424), dodge
  counters (TLG:src/character.cpp:2301) and the talker (TLG:src/talker_character.cpp:1347).
- `force_technique` is set only by EOCs and reach attacks.
- No keybinding or option mentions techniques (grep over data/raw/keybindings*, src/options.cpp,
  src/action.*).

**BN master** (attributed to BN PR #9108 per the BN PR page; a shallow clone cannot confirm which
commits added it):

- **State**: `game::manual_combat_mode` (BN:src/game.h:1211). Saved with the game, not the character
  (BN:src/savegame.cpp:98, 373-374). Reset on new game (BN:src/game.cpp:1036, 4254).
- **Toggle**: action `toggle_manual_combat_mode`, BACKTAB, in DEFAULTMODE
  (BN:data/raw/keybindings/keybindings.json:2574-2580; BN:src/action.cpp:147, 537; BN:src/handle_action.cpp:3197-3198).
  It prints "Manual combat mode ON!/OFF!" (BN:src/avatar_action.cpp:1000-1005). TAB stays `autoattack`.
- **Gating**: a thread-local suppression depth with an RAII guard (BN:src/melee.cpp:153, 1150-1163;
  BN:src/melee.h:45-66). The prompt shows when the attacker `is_player() && !is_auto_moving() &&
  !suppressed` (BN:src/melee.cpp:1609-1611). When manual mode is off, every player melee path installs
  the guard (`melee_attack_while_handling_manual_combat_mode`, BN:src/avatar_action.cpp:988-998,
  used by movement attacks at 183-186, BN:src/game.cpp:8343 and BN:src/monexamine.cpp:839, 1130).
  `autoattack` always suppresses (BN:src/avatar_action.cpp:971-975).
- **Menu** (`choose_melee_technique`, BN:src/melee.cpp:893-1100):
  - Entries "Basic attack" `n` and "Automatic" `a`.
  - Then techniques grouped as Weapon / Weapon + martial art / Martial art / Mutation attacks, sorted
    enabled-first, then by name, and de-duplicated by name.
  - Unusable entries are disabled, with the reason in the second column (`addentry_col`) and the
    description in the side panel.
  - F1 / HELP_KEYBINDINGS opens a scrollable "Technique Reference" with every entry's source,
    description and trigger or unavailable reason (BN:src/melee.cpp:814-891).
  - Mutation attacks are listed but cannot be selected ("automatic extra attack").
- **Reasons** (`technique_unavailable_reason`, BN:src/melee.cpp:721-812): internal, defensive only,
  needs a wall, dodge or block counter only, crit only / not on crits / normal hit only, target
  already downed, needs a downed or stunned target, no weapon to disarm, needs a valid weapon, human
  target only, unarmed only, requires a weapon, AoE reason, too slow right now, then a detailed
  requirement reason or "requirements not met".
  - The checks mirror `Character::get_valid_techniques` (BN:src/melee.cpp:188-262), which gained a
    `technique_query_options` struct with `use_weighting=false` and an `allow_counter_techniques`
    switch. That way negative weighting does not randomly hide entries.
- **`controlled_knockback`** (BN:src/martialarts.h:136; BN:src/melee.cpp:2629-2660): in manual mode
  the player aims the knockback with `mode_throw_creature(you, t, knockback_dist)`, and the target is
  flung with `fling_creature` at velocity `distance*10`.
- **A problem to avoid**: the prompt runs *after* the to-hit and crit rolls (BN:src/melee.cpp:1534,
  1600) and before any moves are spent (first `mod_moves` at 1769). Cancelling returns at 1611-1613.
  It appears a player can cancel a revealed non-crit hit at no cost and attack again, which turns the
  menu into a crit and hit oracle.

**BN's grab and throw model, for comparison only** (diverged code base):

- Stateless: `grabbing` and `grabbed` effects plus adjacency lookup (BN:src/handle_action.cpp:170-231;
  BN:src/avatar_action.cpp:230-244; BN:src/game.cpp:376-399).
- The grab never misses. Strength is `clamp(str/2, 1, 15)`, and stamina cost is `max(50, str*20)`
  (BN:src/handle_action.cpp:225-229).
- Drag check: `size_delta <= 0 || x_in_y(str, str + 4*size_delta)` (BN:src/game.cpp:395-399).
- Throw constants and limits are in `creature_throw.h`: minimum strength by size class 12/16/20,
  stamina `clamp(force*10, 100, 1500)`, velocity `distance*10`, 3-turn downed after a throw. The throw
  logic is BN:src/avatar_action.cpp:282-352.
- Crowd crush is a world option `CROWD_CRUSH`, default **off** (BN:src/options.cpp:2897-2900; gate at
  BN:src/suffer.cpp:1736). It sums grabbing strength from adjacent grabbers (BN:src/suffer.cpp:300-310).
  Resistance is a JSON enchantment value `CROWD_CRUSH_RESIST` (BN:src/suffer.cpp:313-318;
  BN:data/json/enchantment_values.json:480; used by mutations at BN:data/json/mutations/mutations.json:2397, 5399, 5979).

---

## 4. Draft port plan

Labels: **[TLG]** ports TLG behavior. **[BN]** borrows BN's design. **[NEW]** is our own addition.
Sizes are rough: C++ counts include headers, JSON excludes reformatting, tests are listed separately.

### Design decisions to make first

1. **Ownership model**. Recommendation: no raw or shared pointer. Two options:
   - A `struct grapple_state { weak_ptr_fast<monster> mon; character_id npc; bodypart_str_id part; efftype_id effect; int strength; }`.
     For monsters, `weak_ptr_fast` from `g->shared_from()` (OURS:src/game.h:409), the same pattern
     as `mounted_creature`. For NPCs, `character_id` resolved through `g->find_npc`.
   - Or, simpler and closer to BN: put `effect_source(grabber)` on the victim's `grabbed` effect
     (OURS:src/creature.h:650) and find the victim by adjacency plus the source's `character_id`.
     The effect, including its source, is already serialized, so grabs survive save/load for free.
     The catch: `effect_source` does not resolve monsters (OURS:src/effect_source.h:36-39), which is
     fine because monsters already use `grabbed_limbs`.
   Either way, drop TLG's intensity matching.
2. **Save behavior**. Recommendation: persist through the effect data, using the effect-source model
   above. Otherwise, release on load explicitly, as TLG does implicitly. No savegame version bump
   either way, because the new members are optional.
3. **EXPLOSIVE_EXERCISE**. Adding the level is cheap. Switching *all* melee from EXTRA to EXPLOSIVE is
   a global calorie and weariness change; tests reference EXTRA_EXERCISE in
   OURS:tests/activity_tracker_test.cpp, char_biometrics_test.cpp and widget_test.cpp. Recommendation:
   keep grapple costs at EXTRA unless we decide on that rebalance separately.
4. **Crowd crush tuning**. Our tests pin the current numbers (OURS:tests/char_suffer_test.cpp:468-520).
   Recommendation: port BN's world option plus a JSON enchantment value, and make TLG's thresholds
   JSON constants. That moves the decision out of C++.

### Phase 0: groundwork and existing bugs [NEW]

- Scope:
  - Fix `monster::remove_grab` to remove `effect_grabbing` (OURS:src/monster.cpp:2871-2877).
  - Exclude self in GRAB_FILTER persistence (OURS:src/monster.cpp:3049-3057).
  - Add `talker_character_const::get_grab_strength` so `n_val/u_val('grab_strength')` works for
    character grabbers (OURS:src/talker.h:258).
  - Add an `int Creature::size_class()` helper, or use `get_size()` directly.
- Files: src/monster.cpp, src/talker_character.{h,cpp}.
- Risks: the `remove_grab` fix changes when monsters stop counting as grabbers, which affects
  crowd-crush tests indirectly. Run `[grab]`, `[suffer]` and `[mattack]`.
- Size: about 40 C++ lines; tests about 80.

### Phase 1: grapple state, ownership and release paths [TLG + NEW]

- Scope:
  - The `grapple_state` member (or the effect-source model) and `Character::release_grapple()`.
  - Per-turn maintenance: adjacency, incorporeal, a stamina floor, orphan cleanup.
  - Every release hook from the table in 1.2: wield, mount, `swap_character`, winded or asleep,
    seizure, fling, knockback, capture, death of either party, teleport.
  - Effect JSON: TLG's `DODGE_CHANCE` enchantments or our `dodge_mod`. This is a balance choice.
- Ports: TLG:src/character.cpp:1879-1898, 2682-2690, 2866-2912, 11758-11761; plus the call sites in 1.2.
- Files:
  - src/character.h and src/character.cpp (`process_turn` OURS:2267)
  - src/character_health.cpp (`process_effects`, if it lives there)
  - src/item_gun_tool_ammo.cpp:639
  - src/character.cpp:618, 1519
  - src/monster.cpp:3126 (`die`), src/npc.cpp:3364 (`die`)
  - src/iuse.cpp (pet carrier), src/teleport.cpp, src/game.cpp (fling)
  - src/savegame_json.cpp (only for the pointer model)
  - data/json/effects.json
- Risks: forgotten release paths leave stale state. Use the weak handle so a forgotten path degrades
  to "no victim", not a use-after-free.
- JSON-driven: the stamina floor, the "release when winded" effect list, and the grabber/victim effect
  ids should come from a JSON settings object or `EXTERNAL_OPTION` (precedent: data/core/external_options.json).
- Size: about 250 C++, about 20 JSON; tests about 150.

### Phase 2: player grab action [TLG]

- Scope: the creature branch of `grab()`:
  - Ramp z handling, swim-under check, unarmed and arm checks, `NO_GRAB`, `GRAB_IMMUNE`.
  - Hit roll with the aquatic penalty, `sp_defense`, NPC prompts and faction warning, toleration.
  - Body-part choice for characters. Skip parts already holding a GRAB effect, which TLG left as a TODO.
  - Refuse, or roll against, a monster already held (NULL-bp collision).
- Ports: TLG:src/handle_action.cpp:613-797.
- Files: src/handle_action.cpp (OURS:650-756), src/mtype.{h,cpp} (`mon_flag_GRAB_IMMUNE`, the same style as OURS:src/mtype.h:136 and mtype.cpp:93, 234), data/json/monsters/monster_flags.json, data/json/flags/flags.json (`NO_GRAB`), doc/JSON/JSON_FLAGS.md, doc/JSON/MONSTERS.md.
- JSON-driven [NEW]:
  - Grab strength as a `jmath_function` id evaluated with u = grabber and n = target. The precedent
    is spell types' `*_formula_id` (OURS:src/magic_type.h:53-57; evaluation OURS:src/math_parser_jmath.cpp:79).
    The C++ default should reproduce TLG's formula.
  - Stamina and move costs and the stamina floor as settings.
  - The "tolerate" rules as a JSON condition, e.g. `grab_tolerated_condition`.
  - A `grab_condition` (u = grabber, npc = target) evaluated before the hit roll, so mods can add
    restrictions such as "no grabbing blobs".
- Size: about 180 C++, about 30 JSON plus docs; tests about 120.

### Phase 3: victim reactions and break-free [TLG]

- Scope:
  - Teach `monster::move_effects` and `Character::try_remove_grab` to recognize Character grabbers.
  - Minimum hold time `1 + 100/speed`.
  - Grabber stamina cost per escape attempt.
  - Monster anger, morale and `on_hit`.
  - Followers do not resist the avatar.
  - Monster `get_dodge` while held: GRAB → 0, GRAB_FILTER → -5.
- Ports: TLG:src/monster.cpp:2599-2685, 2775-2799; TLG:src/character_escape.cpp:227-420.
- Files: src/monster.cpp (OURS:2693-2734, 2825-2842), src/character_escape.cpp (OURS:197-350).
- Risk: our versions have audit fixes, such as the self-skip at OURS:src/monster.cpp:2703-2706. Merge
  by hand; do not paste.
- JSON-driven: the minimum hold time and the stamina cost formula as `jmath` functions. The escape
  chance for monsters could be a monster-type field such as `grab_escape`, defaulting to
  `melee_skill + damage`.
- Size: about 150 C++; tests about 120.

### Phase 4: dragging [TLG]

- Scope:
  - A hook in `game::walk_move` after the existing `grabbed_move` (OURS:src/game.cpp:7812) and around
    `place_player` (OURS:7934).
  - `Creature::grapple_drag` odds, with the ally and pet adjustments.
  - Move the victim with the proper path: monster `setpos`/`move_to` with traps, and `board_vehicle`
    or `unboard_vehicle` for NPCs. TLG's `Creature::move_to` bypasses traps and boarding.
  - Drag cost: TLG charges the full move cost even on a failed drag.
- Ports: TLG:src/game.cpp:10399-10416, 10482-10485; TLG:src/creature.cpp:1753-1806.
- Files: src/game.cpp, src/creature.{h,cpp}.
- Risks:
  - Map shifts. `place_player` can shift the bubble, so recompute the victim tile from absolute
    coordinates, as TLG does with `m.get_abs(oldpos)`.
  - z-level moves via ramps.
  - Interaction with an object grab: these must be mutually exclusive, as in TLG.
- JSON-driven: the drag odds formula as a `jmath` function.
- Size: about 120 C++; tests about 80.

### Phase 5: throwing and fling changes [TLG + BN ideas]

- Scope:
  - The `plthrow` creature branch with gates and confirmations.
  - Targeting: either TLG's `TargetMode::ThrowCreature` with the throwforce panel, or a lighter
    version on `mode_select_only` (OURS:src/ranged.h:32) with a custom origin.
  - Scale force by chosen distance.
  - Anger and aggro.
  - Fix the stamina cost (the intended weight-scaled cost, or BN's `clamp(force*10, 100, 1500)`).
  - `fling_creature` additions:
    - Release the grabber's hold on fling.
    - An `airborne` effect with its consumers (trap prompt skip, monster forced move).
    - Optionally TLG's size-based collision damage to characters (our audit made characters walls).
    - Optionally `obstructed_by_vehicle_rotation` (it must be ported to `map` first).
    - Optionally the softer landing for `flvel < 30`.
  - Keep our fixes for the step origin, landing `pos` and blocker handling.
- Ports: TLG:src/avatar_action.cpp:995-1124; TLG:src/character.cpp:1035-1052, 3329-3357; TLG:src/ranged.cpp:227, 520-534, 2039-2040, 2255, 2388-2425, 3019-3023, 4262; TLG:src/game.cpp:11571-11812 (selected parts).
- Files: src/avatar_action.cpp (OURS:1018), src/ranged.{h,cpp}, src/game.cpp (OURS:9143), src/character.{h,cpp}, data/json/effects.json (`airborne`), src/monmove.cpp, src/creature.cpp.
- Risks:
  - `fling_creature` is shared by hulk smash, vehicles and spells, so every change to it affects
    monster-on-player throws. Gate new behavior behind parameters, or keep our current behavior by
    default.
  - The landing semantics are covered by OURS:tests/audit_game_loop_test.cpp:17-50.
- JSON-driven [NEW]:
  - throwforce as a `jmath_function`.
  - The descriptor thresholds and messages as JSON.
  - A `fling` spell effect (TLG's `fling_away`, TLG:src/magic_spell_effect.cpp:1997-2010) and/or an
    EOC effect `u_fling`/`npc_fling` { force, direction or target_var, controlled } that wraps
    `fling_creature`. That gives modders the throw engine independently of grappling.
- Size: about 350-450 C++ (the UI is about 120 of that), about 30 JSON; tests about 150.

### Phase 6: martial-arts integration [TLG + NEW]

- Scope (C++):
  - `fallback` and `basic` on `ma_technique`, plus the three-pool `pick_technique` (TLG:src/melee.cpp:1583-1637, 1755-1777).
  - `tech_effects[].intensity`.
  - Effect target. Prefer `"target": "self"|"target"` over TLG's boolean `hit_self`, and accept
    `hit_self` as an alias.
  - Load-time validation in `ma_technique::check`.
  - Docs in doc/JSON/MARTIALART_JSON.md.
- Scope (JSON):
  - Base techniques as fallbacks, with grapple conditions for headbutt and kick
    (TLG:data/json/techniques.json:9-59). Reconcile with our headbutt-when-grabbed rule
    (OURS:data/json/techniques.json:11-27): TLG's condition covers both directions, so it subsumes ours.
  - Judo throw, disarm and backthrow; Pankration grab-knee and arm lock; Zui Quan fist, spin and throw.
  - `tec_brawl_strangle`, plus the `choking` and `strangulation` effects (TLG:data/json/effects.json:5432, 5450).
  - Feral weapon attack gates.
- Files: src/martialarts.{h,cpp} (OURS:176-277, 237-353), src/melee.cpp (OURS:1465-1620, 1819-1829), data/json/techniques.json, data/json/martialarts.json, data/json/effects.json, data/json/monster_special_attacks/feral_weapon_attacks.json, doc/JSON/MARTIALART_JSON.md.
- Risks:
  - `fallback` changes melee behavior for *all* unarmed characters. Today the base techniques compete
    equally with style techniques; after the change they appear only when nothing else is possible.
  - Existing tests in OURS:tests/martial_art_test.cpp:107-260 and OURS:tests/melee_test.cpp may rely
    on the current selection.
  - Mod JSON (in-repo mods, not inspected) may override `tech_base_*`.
- JSON-driven [NEW]:
  - Add condition shorthands `u_is_grabbing` / `npc_is_grabbed_by_u` (and the reverse), so mods need
    not repeat the GRAB / GRAB_FILTER pair. The pair is also ambiguous when *someone else* holds the
    target.
  - Expose context values `grab_strength` and `grab_part` to `post_hit_eocs` and the MA event EOCs
    through `ma_event_context`.
- Size: about 120 C++, about 300 JSON, about 60 doc lines; tests about 150.

### Phase 7: monster-side grab changes [TLG]

- Scope:
  - Pull the target out of a vehicle (TLG:src/mattack_actors.cpp:690-722).
  - `NO_GRAB` and incorporeal conditions on `grab`, `grab_2`, `grab_3`.
  - Optionally the `grab` cooldown of 6 instead of 10. That is a balance call.
  - Skip `effect_pulled` unless we actually apply it.
- Files: src/mattack_actors.cpp (OURS:706-720), data/json/monster_special_attacks/monster_attacks.json:62-140.
- JSON-driven: put the vehicle yank's `driver_ability` formula and yes/no switch in `grab_data`
  (e.g. `"pull_from_vehicle": true`, `"vehicle_pull_formula"`), not hardcoded for every grab.
- Size: about 70 C++, about 40 JSON; tests about 60.

### Phase 8: crowd crush [TLG thresholds, BN toggle and enchantment]

- Scope:
  - Underwater skip, size gating, not-on-ground and wall rules.
  - Resistance as an enchantment value `CROWD_CRUSH_RESIST` (BN design), with TLG's trait and state
    modifiers moved into mutation and effect JSON instead of hardcoded `has_trait` checks.
  - A world option to disable crowd crush.
  - Keep our SUFFOCATION_IMMUNE check.
- Files: src/suffer.cpp (OURS:355-424), src/options.cpp, src/magic_enchantment.{h,cpp}, data/json/enchantment_values.json (if ours has it), mutations JSON, tests/char_suffer_test.cpp.
- Size: about 70 C++, about 30 JSON; tests about 80 (rewrite of existing cases).

### Phase 9: manual combat mode [BN design, our implementation]

- Scope:
  - A per-avatar or per-game flag, saved, toggled by a keybind. Pick a key other than BACKTAB if
    BACKTAB conflicts in DEFAULTMODE in our keybindings. (Checked later: it doesn't; see section 8.)
  - A technique menu built from `get_all_techniques` plus our `evaluate_technique` refactored to
    *return a reason* instead of logging debug strings (OURS:src/melee.cpp:1497-1620 already has one
    debug message per rejection, so the reasons map directly).
  - Grey out unusable entries with the reason. Show `condition_desc` for condition failures.
  - Basic and Automatic entries.
  - Prompt **before** the hit roll, to avoid BN's cancel-to-reroll problem.
  - For crit-only techniques, either grey them out with "crit only" or make the chosen technique
    "attempted" and fall back to a basic attack if the crit does not happen.
  - Forced techniques need an attack vector. Today a forced technique gets
    `attack_vector_id::NULL_ID()` (OURS:src/melee.cpp:818-820). Call
    `martial_arts_data->choose_attack_vector` for the chosen technique.
  - Optional: `controlled_knockback`, which aims knockback techniques with the Phase 5 targeting UI.
- Files: src/melee.cpp, src/character.h, src/avatar.{h,cpp} or src/game.{h,cpp}, src/action.{h,cpp}, src/handle_action.cpp, data/raw/keybindings.json, src/savegame_json.cpp.
- Risks:
  - UI work in curses and tiles.
  - The prompt must never fire for NPCs, autoattack, auto-move or EOC `u_attack`. Use BN's
    suppression-guard pattern.
  - Interaction with `reach_attack`.
- JSON-driven:
  - Optional technique fields `manual_only` and `auto_only`.
  - A `menu_hint` string.
  - Use `condition_desc`; it already exists.
- Size: about 400 C++ (menu about 200, reason refactor about 100, plumbing about 100), about 10 JSON; tests about 100.

### Phase 10: EOC and event surface [NEW]

- Scope:
  - EOC effects `u_grab` / `npc_grab` (target_var, strength or formula, part, forced), `u_release_grab`,
    `u_throw_grabbed` (target_var, force).
  - Conditions `u_is_grabbing`, `u_is_grabbed`, with an optional `by` / `of` talker.
  - Math `u_val('grab_strength')` for characters (Phase 0).
  - Event-bus events `character_grabs_creature`, `character_releases_grab`, `character_throws_creature`
    so EOC `event` triggers and achievements can hook them.
  - With this, NPC grappling can be authored in JSON before any NPC AI work.
- Files: src/npctalk.cpp, src/condition.cpp, src/event.h/event_statistics (whichever our tree uses), doc/JSON/EFFECT_ON_CONDITION.md.
- Size: about 200 C++, docs about 80; tests about 100.

### Phase 11 (optional): EXPLOSIVE activity level [TLG]

- Scope: add the constant and map entries, the diary column, and `maximum_exertion_level`. Decide
  whether melee, dodge and grab use it.
- Files: src/game_constants.h, src/avatar.cpp, src/character*.cpp, tests listed in decision 3.
- Size: about 40 C++; test updates about 40.

### Dependency order and totals

0 → 1 → 2 → 3 → 4 → 5 → 6. Phase 6's C++ fields can land in parallel with Phase 1, but its
grapple-gated JSON is useless before Phase 2. Phases 7 and 8 are independent after Phase 0.
Phase 9 needs only Phase 6's `evaluate_technique` refactor. Phase 10 builds on 1, 2 and 5.

Rough totals: C++ about 1,900-2,100 lines; JSON about 500; docs about 250; tests about 1,300.

---

## 5. Balance knobs, listed so they can live in JSON

| Knob | TLG value or formula | Where |
| --- | --- | --- |
| Grab start stamina floor and cost | 400 floor; 120 arm stamina; moves 100 + unarmed attack speed | handle_action.cpp:680-728 |
| Grab strength | `1 + round(max(.5,grip) * (arm_str + unarmed + melee + size))`, cap 100 | handle_action.cpp:688-695 |
| Aquatic grab penalty | -8 hit | handle_action.cpp:735-747 |
| Maintenance floor | Stamina 400 | character.cpp:2898-2900 |
| Grabber cost per escape attempt | `clamp(max(1,100-str) * (max(1,Δsize) + skill or escape/10), 100, 400)` | monster.cpp:2655-2662; character_escape.cpp:379-386 |
| Minimum hold | `1 + round(100/speed)` turns | monster.cpp:2641-2648; character_escape.cpp:277-284 |
| Drag odds | `kg / (str*size)`, then `(odds+.25)^2` clamped 1..1000; ally `run_cost/200`; pet /2 | creature.cpp:1777-1796 |
| Throwforce | Section 1.6 formula; railgun +16/+8; water x0.25 | character.cpp:3329-3357 |
| Throw gates | range > 1, size gap at most arm_str/10 | avatar_action.cpp:1027-1032 |
| NPC anger thresholds | force > 24 (neutral), > 60 (any NPC) | avatar_action.cpp:1062-1065 |
| Monster anger and morale | +15 anger / -15 morale on throw; +5/+15 anger and -15 morale per escape attempt | avatar_action.cpp:1071-1081; monster.cpp:2622-2639 |
| Fling collision | `min(size*30, v)`, `/6`; walls `/6`; landing `/9` | game.cpp:11670-11797 |
| Vehicle yank | `rng(0,mph) + driver_ability < rng(0, grab_str)` | mattack_actors.cpp:690-722 |
| Crowd crush | Section 1.8 table | suffer.cpp:342-440 |
| Fallback weighting | headbutt -10, punch -8, kick -10 | techniques.json:16, 35, 46 |

---

## 6. Save compatibility

- TLG never serializes grapple state, so there are no TLG save formats to stay compatible with.
- Old saves of ours contain no grapple state. Every new field must be optional on read.
- With the effect-source model, the victim's `grabbed` effect already round-trips with its source
  (OURS:src/effect_source.h:41-42). On load, rebuild the grabber's view by adjacency plus source id.
  A missing or mismatched grabber is removed by the existing orphan-cleanup paths.
- If manual combat mode is stored, add an optional `manual_combat_mode` boolean, as in BN's savegame,
  or put it on the avatar.
- New effect ids (`airborne`, `choking`, `strangulation`) and flags (`NO_GRAB`, `GRAB_IMMUNE`) are
  additive. `airborne` must never be saved active (max 1 s). Consider `"remove_on_load"`-style
  handling if a save can happen mid-fling; not checked.

## 7. Test plan (Catch2, deterministic)

Infrastructure:
- `clear_map()`, `clear_avatar()`, `spawn_test_monster()` (OURS:tests/map_helpers_tests.h:17),
  `spawn_npc()` (OURS:tests/player_helpers.h:31).
- `override_option no_animations("ANIMATIONS","false")` (pattern at OURS:tests/audit_game_loop_test.cpp:21-23).
- TEST_DATA techniques (OURS:data/mods/TEST_DATA/martialarts.json).
- Make RNG-dependent rolls deterministic by testing the formula functions directly. Put each formula
  (grab strength, drag odds, throwforce, escape chance, crowd thresholds) behind a pure function or
  `jmath` id, and use extreme inputs where the probability is 0 or 1. Do not test averages over many
  runs.

| Phase | Tests (new file `tests/grapple_test.cpp` unless noted) |
| --- | --- |
| 0 | `monster::remove_grab` clears `grabbing` when the last limb is released. `u_val('grab_strength')` on a character grabber returns its strength. A self-held monster does not persist its own GRAB_FILTER. |
| 1 | Release on wield, mount, asleep, winded, fling, knockback-tech, victim death (monster and NPC), teleport, non-adjacency, stamina below the floor. A save/load round trip keeps or cleanly drops the grab per decision 2. No dangling handle after `g->remove_zombie`. |
| 2 | Armed, no arms, NO_GRAB effect and GRAB_IMMUNE target are refused with no state change: these refusals are checked before the grab pays anything. A grab that is tried pays its moves and arm stamina before the hit roll. A guaranteed grab sets both effects with the expected strength and body part. As built, it uses a target that tolerates the grab (a pet, or a follower), so there is no hit roll; a monster target must also have no special defense (`special_when_hit`), because the grab gives the monster's special defense its chance to answer (a zapback, acid and the like), and the grab fails if that leaves either side dead or out of reach. The tests use a pet cow, which has none. Strength formula values for fixed stats. A neutral NPC needs confirmation (use a query override, or test the confirmation predicate). |
| 3 | A monster cannot break free inside the minimum hold time. The grabber loses stamina per attempt. A follower never resists. A player held by an NPC is found as an NPC grabber, not an orphan. Monster dodge is 0 while held. |
| 4 | Dragging a light monster with high strength moves it to the vacated tile, including across a map shift (place near the bubble edge). An immobile victim blocks the move. The ally discount applies. The drag odds function matches known values. |
| 5 | throwforce for fixed stats and sizes. A throw at distance d lands at d on open ground (extend audit loop-b01). Force scales with d/range. The thrower's hold is released. A held victim survives a weak throw ("held in place"). Landing on open air triggers the ledge (z drop). Landing on a trap triggers it at the landing tile, which also guards against TLG bug 2. Stamina cost is weight-scaled (TLG bug 1). |
| 6 | Fallback only when no other technique is valid. Basics join the winning pool. A `hit_self` / `target: self` effect lands on the attacker with the given intensity. Grapple-gated JSON techniques are selected only while holding (extend OURS:tests/martial_art_test.cpp:169 conditional tests). `tec_brawl_strangle` applies `choking`. Kick is never chosen while holding. |
| 7 | Vehicle yank: a seated target without a seatbelt and with a high grab strength is unboarded and downed. With a seatbelt it is never unboarded. |
| 8 | Rewrite OURS:tests/char_suffer_test.cpp:468-520 to the chosen thresholds. The option off means no oxygen loss. The CROWD_CRUSH_RESIST enchantment at 100 means no loss at the minimum crowd. |
| 9 | Reason strings for each rejection category (pure function). The prompt is suppressed for NPCs, autoattack and EOC `u_attack`. A forced technique gets a valid attack vector. Cancelling before the hit roll costs nothing and rolls nothing. |
| 10 | Each EOC effect and condition round trip, using TEST_DATA EOC JSON (OURS:data/mods/TEST_DATA/EOC.json). |

## 8. Open questions and things that could not be determined

- Whether BN PR #9108 introduced exactly the BN code described in section 3. The shallow clone has no
  history, so this describes BN master.
- TLG's PR-by-PR attribution for each line. TLG's history is shallow too; the attributions in this
  document follow the PR list in the plan, matched on behavior.
- BACKTAB is free in our main game context (DEFAULTMODE). It is bound globally to PREV_TAB, and in the mod manager, targeting, vehicle interaction and inventory contexts, but a binding only acts in a context that registers its action, and DEFAULTMODE registers no action on BACKTAB but the manual combat toggle.
- In-repo mods: Aftershock and Isolation Protocol grant `tech_base_*` through their own body parts (`data/mods/aftershock_exoplanet/body_parts.json`, `data/mods/Isolation-Protocol/body_parts.json`), so the fallback change reaches them too. Mods overriding Judo or Pankration were not checked.
- How `airborne`'s LEVITATION flag interacts with our trap and gravity code. TLG relies on it; our
  equivalents were not traced.

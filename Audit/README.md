# nickclysm engine audit

**Follow-up:** [Twelve correctness fixes](correctness-fixes.md) address the concrete observations from the powers, interactivity and worldgen audits. This includes protection of non-root mutable-special pieces, previously left open under OMAP-B01. See that record for validation and scope.

**Executions:** the [implementation design](plans/executions/README.md) now has a
[first-milestone status](plans/executions/04-implementation-status.md) and a supported
[JSON authoring guide](../doc/JSON/EXECUTIONS.md).
The [narration follow-up](plans/executions/05-narration-milestone.md) adds owned message
appends, hearing audiences, and presentation controls.

This directory is a static audit of the nickclysm C++ engine at commit `a1a646d6`, dated 2026-11-01. It looks for three things: missing JSON bindings and EOC features (behaviour that content should be able to drive but cannot); bugs, oversights and quirks; and obvious performance improvements. Every finding cites the `file:line` locations it is based on, so each claim can be checked against the source.

## At a glance

The audit contains 1405 findings across 31 chapters: 1315 confirmed by a reviewer reading the code, and 90 plausible. Reviewers rejected another 80 candidate findings; those are listed in the chapter appendices and are not counted above.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 42 | 257 | 174 | 473 |
| Bugs & quirks | 8 | 74 | 261 | 209 | 552 |
| Performance | 0 | 33 | 136 | 211 | 380 |
| **Total** | **8** | **149** | **654** | **594** | **1405** |

All 8 critical findings are small-effort bugs (seven crashes or memory-safety errors and one softlock), and all of them can be reached from vanilla play or shipped mods. All 157 critical and high findings are confirmed, and 98 of them are rated small effort. On the modding side, the biggest problems are structural: JSON that loads silently when it is wrong, and engine behaviour keyed to hardcoded content ids (about 220 of the 473 JSON/EOC findings).

## How to read the chapters

**IDs.** Each finding has an ID of the form `<SYSTEM>-<lens><number>`, for example `EOC-B03`. The prefix names the chapter, and the lens letter says which kind of finding it is:

- `J`: missing JSON binding or EOC feature
- `B`: bug, oversight or quirk
- `P`: performance

Prefixes by chapter: EOC 01, MATH 02, EVT 03, DATA 04, LOOP 05, MAP 06, SPAT 07, MGEN 08, OMAP 09, ITEM 10, IINF 11, INV 12, USE 13, CHAR 14, HLTH 15, PROG 16, MUT 17, CMBT 18, MONAI 19, MONDT 20, NPC 21, CAMP 22, VEH 23, VINT 24, CRAFT 25, ACT 26, MAGIC 27, TERR 28, UIINV 29, UIHUD 30, REND 31.

**Chapter layout.** Each chapter starts with its scope (the source files covered) and a Summary with a lens-by-severity table and a short "Fix first" list. Then come three sections: 1. Missing JSON bindings & EOC features, 2. Bugs, oversights & quirks, 3. Performance (chapter 31's section 1 is marked "Not audited", because that system had no JSON/EOC pass). Findings within each section are ordered from most to least severe. Each finding gives Severity, Effort and Verified, a **Where:** line of `file:line` locations, the problem and its evidence (often a short code excerpt), and a **Fix:**.

**Severity.**

- Critical: a crash, save corruption, something game-breaking, or a massive cost on a hot path.
- High: wrong behaviour that players commonly hit, an important modding gap, or a significant cost.
- Medium: a real problem, but situational.
- Low: minor.

**Effort.**

- Small: under about 50 lines, and local.
- Medium: in between.
- Large: cross-cutting.

**Verified.**

- Confirmed: a reviewer verified the finding by reading the code.
- Plausible: consistent with the code, but reachability or impact is not proven.

**Rejected findings.** Every chapter except 06 and 08 (which had no rejections) ends with "Appendix: considered and rejected", which lists findings that reviewers dropped and says why. Check it before filing something that looks new.

## Chapters

| # | Chapter | JSON/EOC | Bugs | Perf | Critical | High | Summary |
|---|---|---|---|---|---|---|---|
| 01 | [EOC effects, conditions & dialogue](01-eoc-dialogue.md) | 19 | 20 | 11 | 1 | 5 | Broad and mostly working; the weakness is silent failure on unknown strings, null talkers and dead fields; no entity handles. |
| 02 | [Math expressions & the talker interface](02-math-talkers.md) | 15 | 19 | 9 | 1 | 1 | Assignable functions add instead of set, docs drift from the registry, non-character talkers are thin; one vanilla crash. |
| 03 | [Events, statistics, achievements & timed events](03-events-stats.md) | 15 | 17 | 10 | 0 | 3 | Death events repeat during deathcam, strict enum event names break saves, and every load pulls in all visited overmaps. |
| 04 | [JSON loading, mods, save/load & options](04-data-loading-saves.md) | 13 | 14 | 15 | 0 | 6 | Override/extend/delete rules are inconsistent; save bugs lose data permanently; vsync-blocked loading redraws cost about 3 s. |
| 05 | [Game loop, turn processing, actions, time & weather](05-game-loop-weather.md) | 18 | 21 | 13 | 0 | 6 | Weather is the weak spot: lossy conversions, repeated regeneration, player-relative effects. EOCs can read world state but change little of it. |
| 06 | [Map core, submaps, terrain/furniture data & fields](06-map-core-fields.md) | 10 | 18 | 16 | 0 | 7 | Field and terrain behaviour keyed to hardcoded ids, no destruction EOC, a vehicle double-free, repeated work on every submap crossing. |
| 07 | [Pathfinding, vision, lighting, sound & scent](07-pathfinding-vision-sound.md) | 18 | 20 | 14 | 0 | 5 | 3D Bresenham sees through walls, A* opens interactive stair prompts, sound ignores terrain; avoidable relights and `avoid()` calls. |
| 08 | [Mapgen & map extras](08-mapgen.md) | 11 | 17 | 13 | 0 | 2 | Mature JSON mapgen cut off from EOCs; several documented features silently do nothing; costly phase-change pass on every submap load. |
| 09 | [Overmap & world generation](09-overmap-worldgen.md) | 16 | 16 | 16 | 0 | 3 | Worldgen tuning fixed in C++, EOCs cannot write the overmap, many one-line bugs, expensive overmap search and special placement. |
| 10 | [Item core, item types, groups, materials & faults](10-items-core.md) | 14 | 18 | 16 | 0 | 3 | Append-only copy-from lists, hardcoded item behaviours, save RLE corruption, unrotted food; `Item_factory::init()` reruns per ITEM object. |
| 11 | [Item names/info, armor & guns/tools/ammo](11-item-info-armor-guns.md) | 18 | 17 | 12 | 0 | 2 | Combat-math bugs (double ablative plates, translated gunmod checks, left-only sided armor); logic tied to item ids and naming conventions. |
| 12 | [Containers, pockets, inventory & item locations](12-containers-inventory.md) | 14 | 15 | 15 | 0 | 6 | `pocket_data` accepts typos and cannot be patched, EOCs see little container state; `item_location` allocation and deep-copy load costs. |
| 13 | [Item use actions (iuse & iuse_actor)](13-item-use.md) | 17 | 19 | 8 | 1 | 5 | About 161 hardcoded iuse functions, an EOC use_action too limited to replace them; holster crash, null carriers, wrong formulas. |
| 14 | [Character, avatar & body](14-character-body.md) | 17 | 18 | 13 | 1 | 4 | Behaviour keyed to hardcoded ids, NPCs miss avatar-only per-turn logic; wound-progression crash, inverted frostbite; per-turn recompute. |
| 15 | [Health, needs, effects, morale & wounds](15-health-needs-effects.md) | 16 | 19 | 14 | 0 | 4 | Diets, sleep, addictions and effects hardcoded per id; hand-rolled loaders block overrides; several documented fields do nothing. |
| 16 | [Skills, proficiencies, professions & character creation](16-progression-chargen.md) | 16 | 19 | 9 | 0 | 3 | `chargen_allow_npc` and profession proficiencies broken; progression rules hardcoded; an EOC can set a skill to -1 and crash. |
| 17 | [Mutations & bionics](17-mutations-bionics.md) | 16 | 19 | 6 | 0 | 8 | Fragile state handling: activation use-after-free, stuck powered states and weapon CBMs; many hardcoded bionic and mutation ids. |
| 18 | [Combat: melee, ranged, martial arts, damage & explosions](18-combat.md) | 17 | 20 | 15 | 1 | 5 | Built around avatar-vs-monster; several documented features dead; one shipped-item crash; every pellet and shrapnel fragment simulated individually. |
| 19 | [Monster behavior & special attacks](19-monster-ai-attacks.md) | 13 | 16 | 16 | 0 | 6 | AI tuning hardcoded, EOCs cannot reach monster combat, effect `speed_mod` discarded; per-step work scales with the whole bubble. |
| 20 | [Monster data, creatures, hordes & butchery](20-monster-data-creatures.md) | 17 | 20 | 9 | 0 | 6 | Butchery, defenses and hordes hardcoded; NIGHT spawns never happen, hard presets lose time gating; hordes can delete zombies. |
| 21 | [NPC AI & factions](21-npc-ai-factions.md) | 15 | 19 | 15 | 0 | 9 | Half-migrated behavior tree; `npc_attack` bugs (no reload, ignored gun modes, blocked casters); evaluators scan every tile in range. |
| 22 | [Missions, trade & basecamps](22-missions-trade-camps.md) | 18 | 18 | 8 | 0 | 6 | Basecamps are weakest: closed C++ mission enum and accounting bugs (double food, inflated fuel, lost or duplicated seeds). |
| 23 | [Vehicle core & movement](23-vehicles-core.md) | 18 | 19 | 13 | 0 | 4 | Behaviour keyed to hardcoded ids, EOCs see little of vehicles; `UNMOUNT_ON_MOVE` crash, tanks never explode; hot-path allocations. |
| 24 | [Vehicle types, interaction, use & autodrive](24-vehicles-interaction.md) | 15 | 16 | 8 | 0 | 4 | Little is JSON-driven: hardcoded part toggles, lossy copy-from; engine-start bypass and a door-menu out-of-bounds read. |
| 25 | [Crafting, recipes & requirements](25-crafting-recipes.md) | 16 | 15 | 13 | 0 | 5 | Failure roll ignores difficulty, disassembly is weakest, copy-from drops extend blocks; menu and wait mode repeat heavy work. |
| 26 | [Activities & zones](26-activities-zones.md) | 18 | 15 | 14 | 1 | 8 | EOC layer lags the actor migration; butchery use-after-free, instant zone repair; zone multi-activity is the performance hotspot. |
| 27 | [Magic, spells & enchantments](27-magic-enchantments.md) | 18 | 21 | 9 | 1 | 8 | Fake and non-avatar casts are second-class, EOC spells lack context; HP-cost softlock, blasts through walls; validation almost absent. |
| 28 | [Examine actions, traps, computers & construction](28-examine-traps-construction.md) | 18 | 18 | 6 | 0 | 5 | Process furniture, traps, computers and constructions hardcoded to vanilla ids; weak JSON hooks; many small logic errors. |
| 29 | [Inventory, AIM & crafting UIs](29-inventory-crafting-ui.md) | 9 | 17 | 14 | 1 | 2 | AIM Move All crash and drag-and-drop check bypass; crafting menu reruns heavy logic about 100 times a second. |
| 30 | [HUD, widgets, menus & input](30-hud-menus-input.md) | 18 | 16 | 11 | 0 | 4 | Widgets are avatar-only and unvalidated (divide-by-zero crashes); much HUD/input hardcoded; safemode rules never saved; idle redraws. |
| 31 | [Rendering, tilesets & audio backend](31-rendering-tiles.md) | 0 | 16 | 19 | 0 | 4 | Tileset loader use-after-free, ortho map-memory gap, half-honoured tileset features; per-sprite string hashing every frame. |
| | **Total** | **473** | **552** | **380** | **8** | **149** | |

## Fix first

A cross-chapter shortlist, grouped by kind of impact. Format: ID · title · severity/effort · why.

### 1. Crashes & memory safety

- **[CHAR-B01](14-character-body.md)** · Wound progression dereferences nullptr when a wound has 2+ progression entries · critical/small · vanilla `bruise_bad` crashes the game on about 17.5% of ordinary 15-25 bash hits to a limb with one bad bruise; the fix is a `break` plus a null guard.
- **[USE-B01](13-item-use.md)** · `holster_actor::use` picks the wrong holstered item, or reads list `end()` · critical/small · drawing a revolver from a western gun belt with ammo in the loops segfaults, and multi-holster belts draw the wrong item.
- **[ACT-B01](26-activities-zones.md)** · Butchery `do_turn` uses a `butchery_data` pointer after `bd.pop_back()` destroys it · critical/small · use-after-free hit through butcher-all, auto-butcher and LOOT_CORPSE zones whenever a corpse that is not the last one fails setup.
- **[MATH-B01](02-math-talkers.md)** · `limb_score()` without 'type' throws a non-math exception and crashes the game · critical/small · vanilla speed-serum side-effect EOC throws an uncaught exception that ends the process.
- **[UIINV-B01](29-inventory-crafting-ui.md)** · AIM Move All crashes when the destination container pane became invalid · critical/small · reopening AIM two tiles away from a container (the saved layout is restored by default) and pressing Move All segfaults; one-line guard.
- **[MAGIC-B01](27-magic-enchantments.md)** · HP-cost spells can softlock: blood_magic menu loops forever when no limb qualifies · critical/small · Magiclysm healing spells cast while hurt hang the game in a menu you cannot leave, and NPC blood casters open it for the player.
- **[EOC-B01](01-eoc-dialogue.md)** · `u_spawn_monster ""` (copy hostile creature) null-derefs on a hostile NPC · critical/small · vanilla hallucination EOCs can segfault when a bandit or other hostile NPC is within 50 tiles; one `is_monster()` check fixes it.
- **[CMBT-B01](18-combat.md)** · Null-source crashes in `apply_ammo_effects`, `u_explosion`, `u_ranged_attack` · critical/small · a shipped Aftershock cluster round segfaults the game when it cooks off in a fire; a few null guards fix it.
- **[MUT-B01](17-mutations-bionics.md)** · Use-after-free: activation EOC that removes its own trait leaves dangling `trait_data&` · high/small · shipped Xedra wings, arms, tail and eyes write to freed memory every time they are dismissed.
- **[USE-B02](13-item-use.md)** · PAPR_BLOWER tick dereferences a null Character when the blower is on the ground · high/small · a running PAPR belt left on the ground (dropped, or worn by an NPC who dies) segfaults on the next tick once its filter is low or empty; a null guard fixes it.

### 2. Save / data loss & corruption

- **[DATA-B01](04-data-loading-saves.md)** · Overmap predecessors are thrown away on every load · high/small · every save/reload permanently loses predecessor terrain, so roads and specials over forest or swamp generate on the field fallback; one-line fix.
- **[ITEM-B02](10-items-core.md)** · Save RLE merges map items that aren't identical: uid, owner, birthday, snippet lost · high/medium · each save silently corrupts map items: shop ownership leaks (free theft or lost property), reservations break, and flyer text and corpse birthdays merge.
- **[LOOP-B04](05-game-loop-weather.md)** · `save_player_data` dereferences an empty `std::optional<zzip>` when the save can't be mapped · high/small · with compressed saves, a OneDrive or antivirus lock crashes autosave and loses the save; one-line check.
- **[EVT-B02](03-events-stats.md)** · Unknown `event_type` names make saves, memorials and achievement files unloadable · high/medium · one foreign memorial file breaks character creation and scores, and the fork cannot remove or rename any event until this is fixed.
- **[DATA-B02](04-data-loading-saves.md)** · World option sliders apply float values formatted with the UI locale · high/small · German or French players who use the difficulty slider create worlds with zero monster spawns and an item spawn rate silently reset to default; one-line `imbue`.
- **[LOOP-B03](05-game-loop-weather.md)** · Tutorial's forced 20 C temperature leaks into the next new game and is saved permanently · high/small · a new player's first real world can be stuck at 20 C forever (no winter); the fix is a reset in `setup()`.
- **[EVT-B01](03-events-stats.md)** · Avatar death events are re-sent on every `is_game_over()` call during deathcam · high/small · while the deathcam runs, the memorial, kill tracker and stats are inflated several times a second and death EOCs re-run; a once-only flag fixes it.

### 3. Gameplay-breaking logic bugs players hit in normal play

- **[USE-B04](13-item-use.md)** · `heal_actor` stop-bleed power is `bleed * skill bonus`, so it is 0 at First Aid 0 · high/small · default Survivor characters cannot stop bleeding with any item; one-formula change.
- **[CHAR-B02](14-character-body.md)** · Frostbite risk zones are inverted: more wind or colder air gives less frostbite · high/small · a core survival mechanic runs backwards (blizzards are safer than calm cold days); the fix swaps comparison signs.
- **[CHAR-B03](14-character-body.md)** · Forced move-mode changes are reverted on the next action (broken-legs prone loop) · high/small · with broken legs every step costs 150+ moves and a fall, and mounting, dismounting and waking up also revert.
- **[NPC-B01](21-npc-ai-factions.md)** · `npc_attack_gun::can_use` requires `has_ammo()`: NPCs never reload in combat or fire energy guns · high/small · armed NPCs and followers club enemies with an empty rifle mid-fight, and UPS energy guns never fire; one-line condition.
- **[IINF-B01](11-item-info-armor-guns.md)** · Ablative plates protect twice per hit, and still protect when the coverage roll misses · high/small · ablative plates act like 100%-coverage armor that sometimes counts double, which breaks ballistic balance; single-parameter fix.
- **[CRAFT-B01](25-crafting-recipes.md)** · Craft failure roll ignores difficulty, so failures almost never destroy components · high/small · at skill 2 or more the real component-loss rate is near 0 while the UI shows 9-30%; one-line fix.
- **[LOOP-B01](05-game-loop-weather.md)** · `fling_creature` never advances `pos` for NPCs and monsters · high/small · zombies hit by cars and companions thrown by hulks move only 1 tile and take damage from colliding with themselves; single-line fix.
- **[MONDT-B02](20-monster-data-creatures.md)** · `EVOLUTION_INVERSE_MULTIPLIER` truncated to int; hard presets remove all time gating · high/small · on every hard difficulty preset, late-game spawns appear from turn 0 and early-only spawns never end; int-to-double fix.
- **[MONAI-B01](19-monster-ai-attacks.md)** · Effect `speed_mod` is silently discarded for every monster · high/small · staggered, frenzy, venom and every other speed-changing effect has no effect on monster speed.

### 4. Performance quick wins

- **[DATA-P01](04-data-loading-saves.md)** · Every finalize/verify step does a full redraw and a vsync-blocked present (~190 frames) · high/small · about 3 s on every world load or new game; throttling presents in `loading_ui::show()` removes it.
- **[ITEM-P01](10-items-core.md)** · `items::load` re-runs `Item_factory::init()` for every ITEM JSON object · high/small · a one-line guard removes 2-3M redundant allocate/free pairs (likely several hundred ms) from every world load.
- **[MONAI-P01](19-monster-ai-attacks.md)** · Every target-less monster scans all 2,541 bubble submaps for moving vehicles per `plan()` step · high/small · a constant per-turn cost for every wandering monster (about 1.1M submap visits per turn with 300 idle monsters); reordering one test removes most of it.
- **[MONAI-P02](19-monster-ai-attacks.md)** · `anger_cub_threatened`'s early return never fires, so each monster walks the whole monster list · high/small · O(N^2) work with allocations every turn in horde fights; a one-character guard fix.
- **[SPAT-P01](07-pathfinding-vision-sound.md)** · A* calls the expensive `avoid()` callback before the closed check · high/small · swapping two `if` statements, plus a per-search memo bit, cuts `avoid()` calls 4-8x in every `route()` by monsters, NPCs and zone activities.
- **[SPAT-P02](07-pathfinding-vision-sound.md)** · Whole-level relight triggered by changes that cannot alter light · high/small · three small gates remove a 0.5-2 ms full-level relight from most player steps and combat turns.
- **[MAP-P01](06-map-core-fields.md)** · Every map shift and tinymap load re-walks all bubble items plus every inventory · high/small · removes a multi-millisecond stall from each submap crossing (every 12 tiles on foot, several per turn while driving).
- **[CRAFT-P02](25-crafting-recipes.md)** · Crafting menu redraws (~100 Hz) recompute book bonuses, tool speeds and helpers · high/small · an idle crafting menu repeats map-wide item scans about 100 times a second, which causes the menu lag in dense bases; memoize the cost context.

### 5. Highest-value modding gaps

- **[EOC-B02](01-eoc-dialogue.md)** · Unknown string conditions compile to always-false with no load error · high/small · every typo in a string-form condition silently fails (and becomes always-true under `not`), and shipped dialogue and the docs are already broken; load-time validation is cheap.
- **[INV-J01](12-containers-inventory.md)** · `pocket_data` loader ignores unknown and dropped keys · high/small · removing `allow_omitted_members()` exposes 30+ already-broken pocket definitions in core and mods and stops new typos from loading silently.
- **[ACT-J01](26-activities-zones.md)** · `activity_type` loader is not a `generic_factory` · high/small · lets mods override vanilla activities and attach do_turn/completion EOCs to reading, butchering and so on; today the hooks only work on mod-created activities.
- **[MAGIC-J01](27-magic-enchantments.md)** · `effect_on_condition` spell effect passes only `spell_location` · high/small · 559 EOC-driven spells cannot read their own level, damage or duration; a few context vars let them scale correctly for every kind of caster.
- **[EOC-J01](01-eoc-dialogue.md)** · EOC variables cannot hold a handle to a monster, item or vehicle; spawns expose nothing · high/medium · the main EOC gap: spawned quest NPCs, summons and items cannot be tagged or scripted afterwards without indirect workarounds.
- **[OMAP-J01](09-overmap-worldgen.md)** · EOCs can barely read and cannot write overmap state · high/medium · unlocks world-changing content (burnt OMTs, specials at a chosen point, map notes, overmap hordes); `overmapbuffer` already has the APIs, so the work is mostly thin wrappers.

## Fixes applied

Every item in "Fix first" groups 1-3 has been fixed on this branch, one commit per group of related findings. The two medium-effort items, ITEM-B02 and EVT-B02, were done in a second round.

| ID | Status | Commit | Regression test |
|---|---|---|---|
| CHAR-B01 | fixed | `4c4457f7` | `tests/audit_wounds_blood_magic_test.cpp` |
| USE-B01 | fixed | `0706a779` | none: the draw path always opens a `uilist`, which tests cannot drive |
| ACT-B01 | fixed | `1f5051d6` | `tests/audit_butchery_test.cpp` |
| MATH-B01 | fixed | `13dee1ed` | `tests/audit_math_limb_score_test.cpp` |
| UIINV-B01 | fixed | `d885c004` | `tests/audit_aim_move_all_test.cpp` |
| MAGIC-B01 | fixed | `4c4457f7` | `tests/audit_wounds_blood_magic_test.cpp` |
| EOC-B01 | fixed | `aace1613` | `tests/audit_eoc_combat_null_test.cpp` |
| CMBT-B01 | fixed | `aace1613` | `tests/audit_eoc_combat_null_test.cpp` (also fixes the `u_explosion` integer `shrapnel` shorthand) |
| MUT-B01 | fixed | `b329e834` | `tests/audit_mutation_activation_test.cpp` |
| USE-B02 | fixed | `0706a779` | `tests/audit_item_use_test.cpp` |
| DATA-B01 | fixed | `9a8b2c7b` | `tests/audit_saves_options_test.cpp` |
| ITEM-B02 | fixed | `abed57e8`, `2dc6bb71`, `dbd2a623` | `tests/audit_item_rle_test.cpp`. Runs are still saved as `[item, count]`, so older builds can read new saves; the copies' uids are stored inside the item as `"rle_uids"` |
| LOOP-B04 | fixed | `90c08ef9` | none: needs the OS to refuse to map the save file |
| EVT-B02 | fixed | `bc57ebfb`, `b2997abd` | `tests/audit_events_test.cpp`. Still open: renaming a `cata_variant_type` or achievement state still fails the save load (now with an error message instead of a crash) |
| DATA-B02 | fixed | `9a8b2c7b` | `tests/audit_saves_options_test.cpp` |
| LOOP-B03 | fixed | `90c08ef9` | none: `game::setup()` cannot run inside the test harness |
| EVT-B01 | fixed | `90c08ef9`, PR C | `tests/audit_gameplay_fixes_test.cpp`. The follower death switch works again (a guard added for executions refused it while the death was finalized), the AVATAR_DEATH EOCs stop once the player isn't dead, and `character_dies` is sent once per death: the body left behind doesn't send it again when it dies as an NPC |
| USE-B04 | fixed | `0706a779` | `tests/audit_item_use_test.cpp` |
| CHAR-B02 | fixed | `7ce602d6` | `tests/audit_frostbite_move_mode_test.cpp` |
| CHAR-B03 | fixed | `7ce602d6` | `tests/audit_frostbite_move_mode_test.cpp` |
| NPC-B01 | fixed | `1966b6c1`, PR C | `tests/audit_npc_attack_gun_test.cpp`, `tests/audit_gameplay_fixes_test.cpp`. NPCs rate and fire the CBM gun they hold (its NO_UNWIELD pseudo-item failed `can_wield`), and put a gun with no power left away |
| IINF-B01 | fixed | `1129cda8` | `tests/audit_ablative_armor_test.cpp` |
| CRAFT-B01 | fixed | `04e8cea9` | `tests/audit_craft_failure_test.cpp` |
| LOOP-B01 | fixed | `90c08ef9` | `tests/audit_game_loop_test.cpp` |
| MONDT-B02 | fixed | `ac239624` | `tests/audit_mongroup_evolution_test.cpp` |
| MONAI-B01 | fixed | `5bc22c4b` | `tests/audit_monster_speed_mod_test.cpp` |

**Group 4 (performance).** Seven of the eight quick wins are in; MAP-P01 was reverted because it broke a wakeup test and gained little. Before-and-after numbers are in [benchmarks/README.md](benchmarks/README.md).

**Group 5 (modding gaps).** All six are in. EOC-J01 and OMAP-J01 were finished later, in PR B and PR D.

| ID | Status | Commit | Regression test |
|---|---|---|---|
| EOC-B02 | fixed | `98907571`, `2266c297` | `tests/audit_string_conditions_test.cpp`. The new check found seven broken conditions in vanilla dialogue (refugee center, Cranberry Foster, a common NPC line), two wrong names in the docs, and one condition in Xedra_Evolved; all fixed. `u_has_active_trait` was added so the Xedra one could be written correctly |
| INV-J01 | fixed | `827993a0` | `tests/audit_pocket_keys_test.cpp`. Fixed 12 clothing pockets with `max_length`, plus pockets in hobo_stove, an Xedra tool and six monster guns. Bandoliers, pouches and quivers now get their `volume_encumber_modifier`, so they encumber less |
| ACT-J01 | fixed | `cbbf71f0` | `tests/audit_activity_type_test.cpp`. The new EOC check found a misspelled `completion_eoc` in Limb_WIP |
| MAGIC-J01 | fixed | `c9fdb7d1` | `tests/audit_spell_context_test.cpp`. Also fixes MAGIC-B19 (vehicle check on the wrong tile) |
| EOC-J01 | fixed | `3ce1e2f5`, PR B | `tests/audit_spawned_eocs_test.cpp`, `tests/audit_eoc_handles_test.cpp`. Adds `spawned_eocs`, `npc_variables` and `item_variables`, then `u_set_talker` handles to monsters, items and vehicles (vehicles get a persistent uid). Limit: most ways of moving an item copy it, and the copy has a new uid, so an item's handle stops finding it once it is picked up, dropped or wielded |
| OMAP-J01 | fixed | `917f2c3c`, PR D | `tests/audit_overmap_eoc_test.cpp`, `tests/audit_partial_eoc_test.cpp`. Adds `set_overmap_terrain`, `place_overmap_special`, `add_overmap_note`, `delete_overmap_note`, `reveal_map` levels, `map_in_overmap_special` and `map_omt_seen`. PR D adds `spawn_overmap_horde` (a monster group placed as an overmap horde at a location), `closest_city` filters (`min_size`, `max_size`, `name`, `max_distance`, with `true_eocs`/`false_eocs` and a `city_distance` context value; the defaults pick the same city as before) and the math functions `horde_size`, `omt_distance`, `city_distance` and `city_size`. `closest_city` also stored the city's overmap tile coordinates as if they were map squares; it now stores the city's center |

**Remaining small high-severity bugs.** All 48 small-effort critical and high bugs left after "Fix first" are in: 44 fixed, 4 partial. Agents fixed them in nine batches, each in its own worktree. A separate reviewer read each batch's diff, and review comments were applied before merging (10 of 48 needed changes). The batches were made against `master`, so they were cherry-picked onto this branch, with two conflicts resolved by hand: NPC-B02 with NPC-B01 in `npc_attack.cpp`, and LOOP-B02 with LOOP-P01 in `weather.cpp`. Running the new tests found two more real bugs, fixed here, and two test mistakes.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| EOC-B03 | fixed | `3edccab5` | `tests/audit_eoc_math_test.cpp` |
| EOC-B04 | fixed | `8e551898` | `tests/audit_eoc_math_test.cpp` |
| MATH-B02 | fixed | `8e063988` | `tests/audit_eoc_math_test.cpp` |
| PROG-B01 | fixed | `43d8122f` | `tests/audit_progression_test.cpp` |
| PROG-B02 | fixed | `811496ae` | `tests/audit_progression_test.cpp` |
| PROG-B03 | fixed | `2e3e06e6`, PR D | `tests/audit_progression_test.cpp`, `tests/audit_partial_data_test.cpp`. NPC hobbies now come from the generic scenario, as their professions do (the player's scenario hobby limits, such as Mind Over Matter's challenge scenarios, no longer apply to every NPC); the avatar keeps its own scenario |
| ITEM-B01 | fixed | `fe9050ad`, `494287b0` | `tests/audit_items_terrain_test.cpp` |
| IINF-B02 | fixed | `ff1bb2f9` | `tests/audit_items_terrain_test.cpp` |
| INV-B01 | fixed | `538d692f` | `tests/audit_items_terrain_test.cpp` |
| USE-B03 | fixed | `c6c22195` | `tests/audit_items_terrain_test.cpp` |
| TERR-B01 | fixed | `d43dce71` | `tests/audit_items_terrain_test.cpp` |
| TERR-B02 | fixed | `256d2225` | none: every path that sets or reads the digester's biogas timer runs through query_yn or uilist, which answer no or cancel in test mode, and the old read and write past map_stack::end() is undefined behaviour with no deterministic symptom |
| HLTH-B01 | fixed | `02d2e621` | `tests/audit_character_mutation_combat_test.cpp` |
| MUT-B02 | fixed | `11685f59` | `tests/audit_character_mutation_combat_test.cpp` |
| MUT-B03 | fixed | `9c2914c4` | `tests/audit_character_mutation_combat_test.cpp` |
| MUT-B04 | fixed | `45143af4` | `tests/audit_character_mutation_combat_test.cpp` |
| CMBT-B02 | fixed | `097c8831`, PR C | `tests/audit_character_mutation_combat_test.cpp`, `tests/audit_gameplay_fixes_test.cpp`. A technique effect with `on_damage` is applied once the hit is dealt, if damage got through armor and blocks |
| CMBT-B03 | fixed | `0631addd` | `tests/audit_character_mutation_combat_test.cpp` |
| MONAI-B02 | fixed | `1c6d9845`, `c2c461f7` | `tests/audit_monsters_test.cpp` |
| MONAI-B03 | fixed | `bd329db5`, `128718b5` | `tests/audit_monsters_test.cpp`. The test then showed absorb_meat never used the meat up (a range-0 flood fill returned no tiles); fixed in `128718b5` |
| MONDT-B01 | fixed | `d9408762` | `tests/audit_monsters_test.cpp` |
| MONDT-B03 | fixed | `9266be1f` | `tests/audit_monsters_test.cpp` |
| MONDT-B04 | fixed | `956ea6d4`, PR D | `tests/audit_monsters_test.cpp`, `tests/audit_partial_perf_ai_test.cpp`. A signal looks up only the horde buckets within its reach (volume - 1 submaps in x, y and z, clipped to the overmap) when that box holds fewer submaps than the map has buckets, and walks the map otherwise. `overmap::signal_hordes` walks only the zone-group columns within reach, in the same sorted order, so the same groups get the same random numbers. Every entity ends the same as before; when one signal wakes idle entities in several buckets, the order they join the active map can differ, as hash-map order already differs between platforms |
| NPC-B02 | fixed | `c3fdf56f`, `c7f4aafa` | `tests/audit_npc_magic_test.cpp` |
| NPC-B03 | fixed | `5320ff5d` | `tests/audit_npc_magic_test.cpp` |
| MAGIC-B02 | fixed | `75ebd188`, `a55bad1a` | `tests/audit_npc_magic_test.cpp` |
| MAGIC-B03 | fixed | `156be011`, `dcd374b6` | `tests/audit_npc_magic_test.cpp` |
| MAGIC-B04 | fixed | `f34f95f8` | `tests/audit_npc_magic_test.cpp` |
| MAP-B01 | fixed | `d9bf2f4a` | `tests/audit_map_spatial_weather_test.cpp` |
| MAP-B02 | fixed | `1bdc675c` | `tests/audit_map_spatial_weather_test.cpp` |
| SPAT-B01 | fixed | `cbece3f9` | `tests/audit_map_spatial_weather_test.cpp` |
| SPAT-B02 | fixed | `0ff19060` | `tests/audit_map_spatial_weather_test.cpp` |
| MGEN-B01 | fixed | `dcc95415` | `tests/audit_map_spatial_weather_test.cpp` |
| LOOP-B02 | fixed | `bb2f420d`, `faf42359` | `tests/audit_map_spatial_weather_test.cpp`. The test then showed the off-screen catch-up added at most one charge; fixed in `faf42359` |
| CAMP-B01 | fixed | `de881c51`, `52f6930a` | `tests/audit_basecamp_test.cpp` |
| CAMP-B02 | fixed | `8f728529`, `288de6f1` | `tests/audit_basecamp_test.cpp` |
| CAMP-B03 | fixed | `3f180ca6` | `tests/audit_basecamp_test.cpp` |
| CAMP-B04 | fixed | `997fd996` | `tests/audit_basecamp_test.cpp` |
| VEH-B01 | fixed | `f560f991` | `tests/audit_vehicles_activities_test.cpp` |
| VEH-B02 | fixed | `c618107c`, `93ff7b93` | `tests/audit_vehicles_activities_test.cpp` |
| VINT-B01 | fixed | `f77455b7` | none: the bug only happens inside uilist's interactive filter loop (uilist asserts !test_mode), and veh_menu_cb lives in an anonymous namespace |
| VINT-B02 | fixed | `38eacff3` | none: control_engines() is only reached through the interactive veh_menu (uilist asserts !test_mode); a testable helper would have meant changing vehicle.h |
| ACT-B02 | fixed | `d996679a`, PR D | `tests/audit_vehicles_activities_test.cpp`, `tests/audit_partial_activities_test.cpp`. The zone check now asks what `repair_part` repairs with (`veh_utils::repair_inventory`): next to the part, from where the worker stands, within `PICKUP_RANGE`, along a clear path unless it's an NPC. A part whose tools lie near it but out of the repair's reach is skipped, since the fetch counts them as there. The crafting inventory cache now tells clear-path inventories apart |
| UIHUD-B01 | fixed | `180c285f` | `tests/audit_ui_render_test.cpp` |
| UIHUD-B02 | fixed | `53f628dd` | `tests/audit_ui_render_test.cpp` |
| UIHUD-B03 | fixed | `1669b081` | none: draw_ascii() is a file-static function reachable only from the interactive overmap UI loop (needs input and a window); exposing it would mean changing overmap_ui.h, which 31 files include |
| REND-B01 | fixed | `d016c926` | `tests/audit_ui_render_tiles_test.cpp`. Tiles-only: compile-checked against SDL3 headers with `-DTILES`; the test runs only in a tiles build |
| REND-B02 | fixed | `15431373` | `tests/audit_ui_render_tiles_test.cpp`. Tiles-only: compile-checked against SDL3 headers with `-DTILES`; the test runs only in a tiles build |

Four fixes have no regression test, because the code is only reachable through interactive menus, or because the old bug was undefined behaviour with no reliable symptom. The two REND tests only run in a tiles build.

**Gameplay changes from these 48 fixes** (the list further down covers groups 1-3):

- Funnels collect rain at the actual rate. Light drizzle no longer gives a charge every turn, and an off-screen funnel gets the same rain as a watched one (LOOP-B02).
- In wind, fire and gas drift downwind instead of into the wind (MAP-B01).
- Frozen food left outside the reality bubble for more than two days rots according to the weather while you were away (ITEM-B01).
- Breaking a long stick costs stamina and time on every attempt; the infinite-stamina exploit is gone (USE-B03).
- Characters with DISTRIBUTED_DAMAGE (Omnicellular) take the full damage of every hit; hits under 12 used to do nothing (HLTH-B01).
- POWER_TRICKLE enchantments give their stated power, instead of scaling with the number of switched-off CBMs (MUT-B04).
- Martial arts techniques whose effects have no condition, including every disarm, now apply them (CMBT-B02). The MELEE_DAMAGE enchantment now affects melee attacks (CMBT-B03).
- NPCs fire whole bursts and use underbarrel gunmods (NPC-B02); hostile NPC casters can use loud spells (NPC-B03).
- Blast spells aimed past a wall explode on the near side (MAGIC-B02). Spell armor penetration works (MAGIC-B04). Killing an NPC with a split or multi-projectile spell credits the caster (MAGIC-B03).
- Monster group entries limited to NIGHT spawn at night (MONDT-B01). Off-screen hordes stop losing zombies (MONDT-B03). Meat-absorbing monsters use up the meat (MONAI-B03).
- Vehicle fuel tanks with explosive fuel can explode (VEH-B02). "Control individual engines" no longer starts a stopped vehicle without the normal start checks (VINT-B02).
- Zone vehicle repair takes time per damage level and fetches enough materials, and counts only the tools and materials the repair itself can reach: none behind a wall for the player, and everything within reach of where the worker stands (ACT-B02).
- Camp missions charge food once, about half what they did. Camp fuel no longer multiplies each time the board opens, planting takes the right seeds once, and leftover seeds come back once (CAMP-B01 to B04).
- New characters get every proficiency their profession or hobby lists (PROG-B02). Random NPCs no longer get player-only professions such as psionic knacks (PROG-B03).
- In math, `u_addiction_turns(...) =`, `u_pain('type': 'perceived') +=` and the stat bonus setters now set or add as written. Sleeping pill withdrawal is prolonged instead of shortened (MATH-B02).
- Per-character safemode rules are saved (UIHUD-B01).

**Second round: the remaining high-severity small and medium findings.** 18 findings: 13 fixed, 5 partial, made the same way (agent batches, separate review, central build and test). The performance numbers are in [benchmarks/README.md](benchmarks/README.md#round-3-remaining-high-severity-performance-findings). Integrating them needed one test fix (a new active item has to be registered before an NPC can see it) and a fix to the benchmark runner, which had been dropping results that followed a log line with no newline.

| ID | Status | Commits | Regression test or benchmark |
|---|---|---|---|
| CHAR-B04 | fixed | `a5a4b6e1` | `tests/audit_character_effects_test.cpp` |
| HLTH-J01 | fixed | `291add8b` | `tests/audit_character_effects_test.cpp` |
| CHAR-J01 | fixed | `30c665af`, PR C | `tests/audit_character_effects_test.cpp`, `tests/audit_modding_gaps_test.cpp`. `u_add_body_part` / `u_lose_body_part` (with npc_ forms): the change outlasts body rebuilds and saves; losing a part takes the parts connected through it, the root, vital parts and parts holding a bionic (unless `force`) can't be lost, and worn items left covering nothing fall off |
| MUT-B05 | fixed | `a9089239`, `846840db`, PR D | `tests/audit_mutation_towards_test.cpp`, `tests/audit_partial_gameplay_test.cpp`. Random mutation (`mutate`, its upgrade roll, `mutate_category`) no longer picks a trait behind a threshold the character lacks; a trait still missing a prerequisite stays a candidate, since picking it mutates towards the prerequisite. Such a pick used to fail at the threshold check (after straining against, or destroying, a CBM that conflicts with the trait), and with two picks per attempt, an attempt came to nothing when both landed on such traits; filtering alone would have sped mutation up, most just before a threshold. So the attempt still comes to nothing ("You feel something straining…") with that old chance, and the pace stays the same |
| MUT-J01 | fixed | `b65fd9a9`, PR C | `tests/audit_mutation_towards_test.cpp`, `tests/audit_gameplay_fixes_test.cpp`. Categories, `allowed_category` and dream categories are checked at load. Data: the Marloss traits lose their undefined MARLOSS category (and its dream, which could never play), Xedra defines GRACKEN, and the BombasticPerks and Isolation Protocol perks drop categories that were never defined |
| EOC-B05 | fixed | `2f8d333f`, `0aad6359` | `tests/audit_items_npc_loading_test.cpp` |
| UIINV-B02 | fixed | `edcb168d`, PR D | `tests/audit_partial_activities_test.cpp`. The drag and drop starts the insert activity AIM uses (`drag_drop_start_move`), which can now put the item in the pocket chosen; the move takes its time once the menu closes |
| ACT-B03 | fixed | `4d5bffa7`, `9c9df48a` | `tests/audit_items_npc_loading_test.cpp` |
| NPC-B04 | fixed | `67a6f426`, `4f9a1228` | `tests/audit_items_npc_loading_test.cpp` |
| DATA-B03 | fixed | `4862b868` | `tests/audit_items_npc_loading_test.cpp` |
| MAP-P02 | fixed | `56f3c41f`, `13d01ac1` | benchmark `temp_map_load` |
| VEH-P01 | fixed | `45efaeba`, `5d8a43e4` | benchmark `vehicle_collision_check` |
| CRAFT-P01 | fixed | `6fdaa74c`, `9805272f` | benchmark `craft_unattended_wait`. A tool or heat source lost during a waiting step is noticed at the next one-minute poll, as with other steps |
| ACT-P02 | fixed | `81616d1d`, `6917bd82`, `4ad151f9` | `tests/audit_perf_world_activities_test.cpp`; benchmark `farm_scan` can't show it |
| TERR-P01 | fixed | `a0fce99f`, `1d0f11f6` | benchmark `blueprint_build_reqs` |
| MAGIC-P01 | fixed | `d3f7852b`, `7738db12`, PR D | `tests/audit_perf_ui_test.cpp`, `tests/audit_partial_perf_ai_test.cpp`; benchmark `spellbook_damage_preview`. A cast with no caster builds one fake caster and shares it with every spell it casts in turn (a spell with two extra spells built nine). Once a spell's effect has run, the caster is put back as built (place, moves, hit points, pain, stamina, effects and variables) before the next spell uses it. Each fake avatar drew one random number for its blood type, so a casterless WONDER spell's picks follow another random sequence for the same seed |
| UIHUD-P01 | fixed | `5521c271` | none: needs a display |
| REND-P01 | fixed | `32187d3e` | none: tiles renderer; compile-checked with `-DTILES` |

**Gameplay changes from the second round:**

- NPCs feel their effects, pain, hunger and thirst like the player: drunk, poisoned or starving NPCs are weaker, and effect dodge and to-hit modifiers now apply to everyone (CHAR-B04, HLTH-J01).
- NPCs notice live grenades, dynamite and C4 near them and get away (NPC-B04).
- A failed mutation no longer strips the traits it would have replaced, and starting traits survive an unaffordable mutation (MUT-B05).
- Body parts from an effect or worn item (e.g. Xedra's Wings of Gossamer) go away when it ends (CHAR-J01).
- Missions that take items (GodCo, Isherwood, robofac and others) take exactly what they need, including part of a stack (EOC-B05).
- Zone sorting no longer carries off your own matching ammo, nails or pills (ACT-B03).
- Inventory drag-and-drop refuses moves that used to crash or duplicate (UIINV-B02). It moves the item through the insert activity, as the advanced inventory does: the inventory closes and the move takes its time.
- Previewing a spell scroll no longer teaches follow-up spells (MAGIC-P01).
- A mod's redefinition of a core item is no longer overwritten by the core version at load (DATA-B03).

Every mod in `data/mods` was loaded with these changes. All load without errors, except Limb_WIP (marked obsolete), which still reports armor and mutation-category warnings that predate this branch.

**Third round: modding round 1 and bug sweep 1.** 70 findings, made the same way as the second round (agent batches in worktrees, a separate reviewer, central build and test):

- **Bug sweep:** the 46 medium-severity bugs in the map, spatial, mapgen, overmap, item and health chapters. 39 fixed, 7 partial. Reviewers asked for changes to 5 of them.
- **Modding round:** the 24 medium-effort modding features. 5 complete, 19 partial; for most partial ones the core is in and the table says what is left. Each agent wrote a design first (JSON syntax with an example, semantics, edge cases), kept existing JSON working, documented the feature and tested it through TEST_DATA JSON. Reviewers also checked that shipped mods behave the same and asked for changes to 11 of the 24. Among other things, that review caught a reordering of activity hooks that would have broken Xedra Evolved's spirit quest and a Mind Over Matter power, and a monster-AI change that would have given the void spider a free extra hit every turn.

Merging the batches needed only documentation conflicts resolved by hand (the EOC talker and event tables in `EFFECT_ON_CONDITION.md`, and two new sections in `MUTATIONS.md`). The agents compile-checked their work but did not run it. Running the new tests found five mistakes in the tests themselves: TEST_DATA that failed to load (a same-mod redefinition and a missing mandatory `cooldown`), a venom effect whose intensity comes from its duration, a wall light whose placement asks the player for a direction, and wind checks that compared two different random rolls. It also found one real bug, fixed in `f4d64d6b`: with no known basher, MAP-J01 ran bash EOCs with no talker at all, which would have shown an error in game for every explosion or fire that hit such a tile. The SPAT-B05 test was re-run against a build with its fix reverted and fails there.

Bug sweep:

| ID | Status | Commits | Regression test |
|---|---|---|---|
| MAP-B03 | fixed | `48892eff` | `tests/audit_map_fields_items_test.cpp` |
| MAP-B04 | fixed | `04dee5de` | `tests/audit_map_fields_items_test.cpp` |
| MAP-B05 | fixed | `f6c96eeb` | `tests/audit_map_fields_items_test.cpp` |
| MAP-B06 | fixed | `4119cc83` | `tests/audit_map_fields_items_test.cpp` |
| MAP-B07 | fixed | `fdf96a44` | `tests/audit_map_fields_items_test.cpp` |
| MAP-B08 | fixed | `db007203`, `3b60f73a` | `tests/audit_map_fields_items_test.cpp` |
| SPAT-B03 | fixed | `6e3fc6d5` | `tests/audit_spatial_sweep_test.cpp` |
| SPAT-B04 | fixed | `b2ad29a7` | `tests/audit_spatial_sweep_test.cpp` |
| SPAT-B05 | fixed | `a078bbf4`, `87629287` | `tests/audit_spatial_sweep_test.cpp` |
| SPAT-B06 | fixed | `89c56b9a` | `tests/audit_spatial_sweep_test.cpp` |
| SPAT-B07 | fixed | `042a6fc0`, `0dacc71e` | `tests/audit_spatial_sweep_test.cpp` |
| SPAT-B08 | fixed | `cdaa9b92`, `0dacc71e` | `tests/audit_spatial_sweep_test.cpp` |
| SPAT-B09 | fixed | `9012c702` | `tests/audit_spatial_sweep_test.cpp` |
| SPAT-B10 | fixed | `0d52b78c`, PR D | `tests/audit_spatial_sweep_test.cpp`, `tests/audit_partial_data_test.cpp`. Adds `scent_neutralization_radius` (0 to 3, default 1) and a load error for scent neutralization on a field that doesn't spread as a gas; removed the dead values from Xedra Evolved's two fogs (no gas phase) and Desert Region's dust storm (no spread) |
| MGEN-B02 | fixed | `efa28db1`, `f99a8cbf` | `tests/audit_mapgen_test.cpp` |
| MGEN-B03 | fixed | `2bfdf130`, `f24378f3`, PR D | `tests/audit_mapgen_test.cpp`, `tests/audit_partial_data_test.cpp`. `remove_npcs` checks its class at load; the robofac intercom migration marks its two gone classes `"obsolete_class": true`, which is itself an error on a class that exists |
| MGEN-B04 | fixed | `30fae9f2` | `tests/audit_mapgen_test.cpp` |
| MGEN-B05 | fixed | `9ff01839`, `359d63d0`, `737dcfd6` | `tests/audit_mapgen_test.cpp` |
| MGEN-B06 | fixed | `d34e62ee`, PR D | `tests/audit_mapgen_test.cpp`, `tests/audit_partial_data_test.cpp`. The predecessor check covers variable-weight mapgens. `road_nesw_manhole` gets `REQUIRES_PREDECESSOR`, and a road needing a predecessor placed over another such road takes over its predecessor (as a road of the same type did), so the manhole doesn't generate the road under it twice. Aftershock's bridge and bridgehead get the flag vanilla's have (vanilla's bridge mapgens, weight 0 there, expect one) |
| MGEN-B07 | fixed | `38d80875`, `6755a3ea` | `tests/audit_mapgen_test.cpp` |
| OMAP-B01 | fixed in working tree | `5322035b` + follow-up | `tests/audit_overmap_worldgen_test.cpp` and `tests/audit_worldgen_correctness_test.cpp`. Non-root mutable pieces now honor unexplored-only placement; see [scope](correctness-fixes.md). |
| OMAP-B02 | fixed | `e1f926a7`, PR D | `tests/audit_overmap_worldgen_test.cpp`, `tests/audit_partial_data_test.cpp`. A special search allowed to make overmaps (the mission target retry) makes the missing overmaps next to the origin's, nearest first and only those within range (8 at most), when no existing one holds the special; `find_all` (random targets) too. A match on existing overmaps still makes none |
| OMAP-B03 | fixed | `ab91d69d` | `tests/audit_overmap_worldgen_test.cpp` |
| OMAP-B04 | fixed | `1d3f3350`, `3d7bce37` | `tests/audit_overmap_worldgen_test.cpp` |
| OMAP-B05 | fixed | `75c6455b` | `tests/audit_overmap_worldgen_test.cpp` |
| OMAP-B06 | fixed | `6b985104` | `tests/audit_overmap_worldgen_test.cpp` |
| OMAP-B07 | fixed | `c8f641b1` | `tests/audit_overmap_worldgen_test.cpp` |
| ITEM-B03 | fixed | `c85b1c60` | `tests/audit_items_sweep_test.cpp` |
| ITEM-B04 | fixed | `64984162` | `tests/audit_items_sweep_test.cpp` |
| ITEM-B05 | fixed | `2bc5d36f` | none: an iteration fix with no observable symptom in tests |
| ITEM-B06 | fixed | `6ece818f` | `tests/audit_items_sweep_test.cpp` |
| ITEM-B07 | fixed | `1e27a025`, `9f118c7f` | `tests/audit_items_sweep_test.cpp` |
| ITEM-B08 | fixed | `00a37ff1` | `tests/audit_items_sweep_test.cpp` |
| ITEM-B09 | fixed | `755dc3f8`, `3aba34c5` | `tests/audit_items_sweep_test.cpp` |
| IINF-B03 | fixed | `032ba713` | `tests/audit_items_sweep_test.cpp` |
| IINF-B04 | fixed | `fc6dbacb` | `tests/audit_items_sweep_test.cpp` |
| IINF-B05 | fixed | `459fe6a2`, PR D | `tests/audit_items_sweep_test.cpp`, `tests/audit_partial_data_test.cpp`. With several ammo pockets, `ammo_remaining()` counts only magazines loaded with an asked-for ammo type (no battery charge as rounds); one magazine always counts, since modular guns such as the robofac gun have a placeholder ammo type |
| HLTH-B02 | fixed | `dd7947b4` | `tests/audit_health_test.cpp` |
| HLTH-B03 | fixed | `69cc6806` | `tests/audit_health_test.cpp` |
| HLTH-B04 | fixed | `56649dbb` | `tests/audit_health_test.cpp` |
| HLTH-B05 | fixed | `7dbca179`, `87629287`, PR D | `tests/audit_health_test.cpp`, `tests/audit_partial_data_test.cpp`. A chance an effect gives works as written: 0 or less never fires, a positive one is at least 1; only a chance left out fires every time (documented). Changes TropiCataclysm's termite glue pain at intensity 68-74 (normal) and 64 (reduced) from every turn to never, as it already was above that, and vanilla fungus and Deadly Bites' zombie virus at intensity 4 (reduced) from never to 1 in 10015 |
| HLTH-B06 | fixed | `a6bbbcaa` | `tests/audit_health_test.cpp` |
| HLTH-B07 | fixed | `f83a3649` | `tests/audit_health_test.cpp` |
| HLTH-B08 | fixed | `99a20b73`, `f8b2a332`, `cd38b39a` | `tests/audit_health_test.cpp` |
| HLTH-B09 | fixed | `867d4cb1`, `f8b2a332` | `tests/audit_health_test.cpp` |
| HLTH-B10 | fixed | `474baa01`, `37f22ce8` | `tests/audit_health_test.cpp` |

Modding features:

| ID | Status | Commits | Regression test |
|---|---|---|---|
| ACT-J02 | fixed | `06f6c4a4`, `13973129`, PR D | `tests/audit_activities_crafting_test.cpp`, `tests/eoc_errands_activity_test.cpp`, `tests/audit_partial_activities_test.cpp`. Activities with a target: reading, butchery, disassembly, crafting, wearing, unloading and reloading, and now 38 more: items (eat, wield, drop, pick up, move, insert, first aid, gunmod removal, toolmod), places with an optional tool (pry, bolt cut, saw, torch, lockpick, dig, chop, fish, hack, crack a safe, clear rubble, harvest, plant, fertilize, pulp, build, tents, mop, forage, smash, fold a vehicle), recipe books, and pets and robots (`target_monster`: milk, shear, play, deactivate). The rest are listed in `doc/JSON/EFFECT_ON_CONDITION.md` with why they can't be started |
| ACT-J03 | fixed | `2d596bc0`, `b53594ec`, PR B | `tests/audit_activities_crafting_test.cpp`. `character_finished_activity` and the completion/cancel EOCs get `outcome` (`completed`, `canceled`, `interrupted`); `u_assign_activity` takes a saved `context` for the activity's EOCs |
| CRAFT-J02 | fixed | `e7222c8a`, PR B | `tests/audit_activities_crafting_test.cpp`. Recipe `start_eocs` and `fail_eocs`; events `character_crafts_item`, `character_disassembles_item` and `character_learns_recipe`. A failure has no event of its own; `fail_eocs` cover it per recipe |
| TERR-J01 | fixed | `33e6f99a`, `f93e4745`, PR D | `tests/audit_construction_recipe_camp_test.cpp`, `tests/audit_partial_loading_test.cpp`. Vanilla and the shipped mods no longer use the post_specials JSON can express: `done_appliance` (90 constructions) became the new `"place_appliance": true`; `done_window_curtains` became `byproducts` and a message; `done_extract_maybe_revert_to_dirt` became the shared `EOC_CONSTRUCTION_EXTRACT_MAYBE_REVERT_TO_DIRT`; the firewood and practice-target marks, ramps, wooden stairs and `remove_above` became `post_eocs` (`set_trap`, `set_terrain` on the tile above). Every post_special still loads for mods. The ones left in C++ (grave digging and burial, stairs dug into rock, vehicles, wiring, deconstruction, tree planks, roof and matching-down) do what JSON can't; doc/JSON/JSON_INFO.md says why for each |
| CRAFT-J01 | fixed | `b33ebcb7`, PR D | `tests/audit_construction_recipe_camp_test.cpp`, `tests/audit_partial_loading_test.cpp`. A recipe with `copy-from` inherits its parent's inline `components`, `tools` and `qualities` (an abstract parent's too) unless it gives its own; `extend` adds groups and `delete` removes ids from every group. A copy of itself replaces the stored groups; other redefinitions of an id still keep the earlier groups they leave out, as before. No shipped recipe relied on the clearing: every copy-from child either restated each group its parent has or the old load check reported it |
| CAMP-J01 | fixed | `bda202de` | `tests/audit_construction_recipe_camp_test.cpp` |
| MUT-J02 | fixed | `baea3c27`, PR D | `tests/audit_mutation_bionic_activation_test.cpp`, `tests/audit_partial_eoc_test.cpp`. PR D: an activation that its EOC cancels (turns the mutation back off) gets its kcal, thirst, sleepiness, mana or stamina cost and its cooldown back (documented in MUTATIONS.md; no shipped mutation that cancels this way has a cost), and `bio_remote` shows its menu first: the activation message only comes once a function is picked, and cancelling refunds the power and runs no activation EOCs |
| MUT-J03 | fixed | `eceb79dd`, `e3212448`, PR C | `tests/audit_mutation_bionic_activation_test.cpp`, `tests/audit_modding_gaps_test.cpp`. Math `bionic_count`, `has_active_bionic`, `bionic_charge`, `bionic_incapacitated`, `bionic_safe_fuel` (the last three assignable), `bionic_cost`, `bionic_power` (assignable), `bionic_power_max`; `u_activate_bionic`'s `ignore_cost`. Running costs (`react_cost`) are still paid |
| NPC-J01 | fixed | `90eb6d45`, PR D | `tests/audit_mutation_bionic_activation_test.cpp`, `tests/audit_partial_eoc_test.cpp`. PR D: `npc_use` can be `{ "when": ..., "priority": ... }`, and NPCs switch on the bionics of one use highest priority first; a spell CBM with an `npc_use` (or one switched on by `npc_activate_bionic`) is cast by the NPC at the creature it fights, or at itself for a spell that can target its caster, and fails before anything is paid when it can't be cast. No shipped bionic got an `npc_use`: for the toggled NPC-usable ones without one (the stat enhancers, `bio_speed`, `bio_metabolics`, `bio_torsionratchet`, `bio_nl_blood_factory`, `bio_creeyncaggii`, Aftershock's `bio_cold_absorber` and flame and shock blades, Mind Over Matter's `bio_metabolics_fifth_sun`) and for `bio_taser`, it would change how followers and hostile NPCs fight and spend power, a balance call for that content |
| CMBT-J01 | fixed | `7dfdc8e4`, PR B | `tests/audit_martial_art_events_test.cpp`. `ongethit_eocs` also run for projectile, shrapnel and explosion hits, with `hit_by`; technique `eocs` get the attack's context. Technique EOCs still can't change the hit's damage |
| HLTH-J03 | partly fixed | `79ed5939`, PR C | `tests/audit_diet_rules_test.cpp`, `tests/audit_gameplay_fixes_test.cpp`. VEGAN is an `eat_condition` now and RUMINANT uses the `HERBIVORE_DIET` flag; the six allergy traits and the animal thresholds already were data. Still C++, because the fields can't express them: PICKYEATER, GOURMAND, SAPROPHAGE, SAPROVORE, EATDEAD, PROBOSCIS, M_DEPENDENT and the Mycus traits, PROJUNK (doc/JSON/MUTATIONS.md lists why) |
| MAGIC-J02 | fixed | `5c4fb57a`, PR C | `tests/audit_spell_enchant_test.cpp`, `tests/audit_modding_gaps_test.cpp`. `spell_values` kinds `FAIL`, `DIFFICULTY`, `EXP` and `SOUND`; math `spell_fail`, `spell_casting_exp`, `spell_sound`, `spell_energy_cost`, `spell_cast_time`, `spell_exp_to_next_level` |
| INV-J03 | fixed | `35ca98d9` | `tests/audit_eoc_inventory_vehicle_test.cpp` |
| VEH-J01 | fixed | `ae836b32`, `398d4f09`, `673b3770`, PR C | `tests/audit_eoc_inventory_vehicle_test.cpp`, `tests/audit_modding_gaps_test.cpp`. `u_damage_vehicle_parts` (filters, and broken parts left alone unless `tear_off`, so the vehicle can't split mid-EOC by default; removal is cleaned up at the end of the turn), `u_set_vehicle_owner` and the `u_vehicle_owner` condition. Weight: VINT-J04 |
| VINT-J02 | fixed | `f28f99e1`, `743f2e40` | `tests/audit_eoc_inventory_vehicle_test.cpp` |
| LOOP-J01 | fixed | `a17a8639`, `ccfba742`, `374ce76a` | `tests/audit_weather_eoc_test.cpp` |
| MAP-J01 | fixed | `210feefe`, `85598110`, `f4d64d6b`, PR B | `tests/audit_bash_eoc_test.cpp`. Every bash has a `cause` (`smash`, `explosion`, `fire`, `vehicle`, `collapse`, `projectile`, `other`), and destroying terrain or furniture sends `tile_destroyed`. `character_smashes_tile` is unchanged: still sent before the bash, without its outcome |
| MONAI-J01 | fixed | `e08fb87c`, `54d15f8c`, `530d442b`, PR D | `tests/audit_monster_eoc_test.cpp`, `tests/audit_partial_eoc_test.cpp`. PR D adds `u_set_monster_destination` (with a `range`), `u_set_monster_target` (the creature at a location) and `u_clear_monster_goal`, with an optional `duration`. The goal is saved with the monster, and `monster::plan()` follows it instead of making its own plans until the destination is reached, the time is up or the target is gone |
| INV-J02 | fixed | `ca101f02`, `0dcb820a` | `tests/audit_mod_overrides_test.cpp` |
| HLTH-J02 | fixed | `c47dbf59`, `58e3fada`, PR C | `tests/audit_mod_overrides_test.cpp`, `tests/audit_gameplay_fixes_test.cpp`. The resist, remove and block lists are checked at load, after fixing the 16 stale ids shipped data had (core: cureall, rat_bite_fever, smoke_eyes, the star vampire effects; Magiclysm, MindOverMatter, Xedra Evolved and Aftershock) |
| NPC-J02 | fixed | `92f0de07`, PR D | `tests/audit_mod_overrides_test.cpp`, `tests/audit_partial_loading_test.cpp`. NPC templates load through `generic_factory` (the template no longer holds an `npc`, which can't be copied, but the fields it gives one). Faction templates do too, each in a factory entry: a `faction_template` is the base of the saved `faction` and its id is the live faction's id. Both get abstracts and `copy-from` of an object defined later; `mission_offered`, `death_eocs`, `price_rules`, `epilogues` and `relations` take `extend` and `delete` |
| MONDT-J01 | fixed | `eb828cdb`, PR C | `tests/audit_mod_validation_test.cpp`, `tests/audit_modding_gaps_test.cpp`. Group entry `spawn_data` takes `effects`, `variables` and `faction` (and mapgen's `patrol`, which nothing applied before); a mapgen group adds its own spawn data on top of the entry's instead of replacing it; a pending spawn keeps its spawn data in a save; entry conditions get `loc`, the spawn location, for mapgen spawns. Hordes and overmap spawns still ignore spawn_data |
| MAGIC-J03 | fixed | `9c38b68a`, `e15db0f0`, `12c7974f`, PR D | `tests/audit_mod_validation_test.cpp`, `tests/audit_partial_data_test.cpp`. Adds `ter_furn_transform` checks (every id, terrain and furniture flags nothing has, trap json flags) and enchantment checks (mutations, effects, emitter, skills, body parts, damage types, spells), both registered. Data fixed: the vitrified farm's doll and bookcase transforms (furniture listed as terrain, now work), dead `FLOWER`/`GRAZEABLE`/furniture `WALL` flags and terrain ids among furniture (vanilla, Magiclysm, Xedra Evolved, XedraWood), XedraWood's Magiclysm-only plants, Xedra's fungicide (`t_dirt`, Magiclysm's barren dirt through mod_interactions), Mind Over Matter's unused transform to undefined furniture, and typos `head`→`heat` (Mind Over Matter), `pierce`→`stab` (Aftershock), `computers`→`computer` and the revenant's `BLEED_IMMUNE` given as a mutation (now a flag; Xedra Evolved). Relic passive enchantments (not in the factory) are still unchecked |
| USE-J01 | fixed | `9ee18e2d`, `4cbf0b89`, PR D | `tests/audit_mod_validation_test.cpp`, `tests/audit_partial_eoc_test.cpp`. PR D adds the `_method` (the action's key among the item's use actions, as the use menu and `u_activate` name it) and `_item_pos` (where the item is: its carrier's tile when carried) context values. Items still have one use action of each type (USE-J02 is open), so `_method` is the action type, `effect_on_conditions`, until use actions get ids |

**Gameplay changes from the third round:**

- Rain washes fields away as documented: fires go out faster, and smoke and blood clear, also on roofs and upper floors (MAP-B03).
- Driving over a pile of items no longer shreds all of it, and thrown-off items can land in any direction (MAP-B06).
- Every copy of a batch craft placed at a workbench now rots, cools and gives off light (MAP-B07).
- A save that is missing a submap regenerates it instead of crashing (MAP-B08).
- Smoke and tear gas weaken scent by their strength instead of erasing it (SPAT-B10). Bashing monsters path through obstacles they can break (SPAT-B09). Teleports keep their minimum distance and telefrag the creature in the way (SPAT-B07, SPAT-B08).
- The second and third prison bus layouts can appear (MGEN-B02).
- No Hope's spawn-rate settings now thin out every kind of spawned item, not just some, and survive a save and load (MGEN-B05, ITEM-B09).
- City size follows the urbanity setting (OMAP-B04). Mission and EOC specials no longer overwrite explored map (OMAP-B01), and missions pick the nearest target (OMAP-B02).
- Sided armor goes on the side you choose (IINF-B04), recoil no longer doubles at an arm-strength threshold (IINF-B03), and multi-magazine guns with a battery can be reloaded (IINF-B05). Countdown items count down (ITEM-B08).
- Broken limbs mend at their own rate, heavier addictions slow fracture healing more, calorie burn scales smoothly with activity, morale stacks the way it is documented, and incorporeal characters keep items they cannot take off (HLTH-B02, B03, B04, B06, B07). Speed serum now changes breathing as written (HLTH-B10).
- Companions killed on camp missions die properly: their death EOCs run and they leave the camp and faction (CAMP-J01).
- NPCs use any weapon bionic flagged for them, such as Aftershock's monowhip (NPC-J01).
- Martial-art get-hit buffs work while asleep, winded or driving, and on-hit buffs trigger right after the hit (CMBT-J01).
- A few vanilla recipes now require the proficiencies their JSON adds (CRAFT-J01). Electronics menu toggles are sorted by name (VINT-J02).
- The Scarlet storm's wind gusts now actually raise the wind (LOOP-J01).
- Mod content that never worked now does: Godco's waiting and game dialogue (ACT-J02); about a dozen spells in Magiclysm, Xedra Evolved, Aftershock and TropiCataclysm that showed an error when cast (MAGIC-J03); Xedra Evolved's Snow-Fox's Stride and Footsteps in the Snow (HLTH-J02); Deadly Bites' antivirals and BombasticPerks' salt toss (USE-J01); No Hope's thug and bandit names, and mod factions that were reverting to vanilla values (NPC-J02).

**For modders**, each new field, effect, condition and math function is documented with an example in `doc/JSON/` (mainly `EFFECT_ON_CONDITION.md`, `JSON_INFO.md`, `MUTATIONS.md`, `MAGIC.md`, `MARTIALART_JSON.md`, `EFFECTS_JSON.md`, `NPCs.md`, `ITEM_CRAFT_AND_DISASSEMBLY.md`) and in `doc/PLAYER_ACTIVITY.md`. Several mistakes that used to load silently are now load errors: unknown activity ids in `u_assign_activity`, crafting EOCs that can never run, unknown monster group condition tags, wrong effect or EOC ids in spells, bad ids in mapgen `else_chunks`, computers, corpses and factions, and misused recipe `extend`/`delete`. Every mod in `data/mods` still loads with no errors or warnings.

**Grappling and martial arts: a port from Cataclysm: The Last Generation.** Not an audit finding: a feature the owner asked for, planned in [plans/tlg-grappling-port.md](plans/tlg-grappling-port.md) (with the source inventory in [plans/tlg-grappling-research.md](plans/tlg-grappling-research.md)) and built in five tracks, each in its own worktree, then merged and reviewed as a whole. The grab, drag, throw, crowd-crush and grapple-technique rules come from Cataclysm: The Last Generation (TLG, by worm-girl); the manual technique menu and aimed knockback follow Cataclysm: Bright Nights (PR #9108 and its `controlled_knockback`). Both are CC BY-SA 3.0, as is this project; each commit credits its source.

What players get:

- **Grabbing.** The grab action (`G`) on a creature grabs it, with a hit roll it can dodge (unless it goes along with it: an ally, a pet, a helpless NPC). A neutral NPC asks "Really attack?", a peaceful monster warns about its faction. Grab strength, the stamina floor and costs are JSON (`grapple_grab_strength`, `GRAPPLE_STAMINA_FLOOR`, `GRAPPLE_ARM_STAMINA_COST`). Unarmed by default; a style's `grapple_weapons` lists what may be held while grappling.
- **Holding.** A held creature can't walk away and struggles every turn; each struggle costs the holder stamina (`grapple_escape_stamina_cost`), and a fresh hold can't be broken at once (`grapple_min_hold_turns`). A hold ends on both sides however it ends: release, death, teleport, sleep, mounting, a style or weapon that no longer allows it, distance, or a knockback.
- **Dragging.** Walking while holding drags the creature into the tile you left (`grapple_drag_odds`); it moves the way it moves itself, so traps, fields and vehicles apply. A step it can't follow is refused and the hold kept.
- **Throwing.** The throw action throws what you hold, with its own aiming screen (force, word, moves, stamina, and what it will hit). A shorter throw is a gentler one; you need the stamina for the distance you pick. A thrown NPC may turn hostile (`GRAPPLE_THROW_ANGERS_*`). TLG's flat 50-stamina throw cost (a sign error) is replaced by the weight-scaled cost it meant.
- **Manual combat mode.** Shift+Tab toggles it (saved; the sidebar shows "(manual)"). Each melee attack then opens a menu of Basic attack, Automatic and every technique, with unusable ones greyed out and the reason given. It opens before the hit roll, so cancelling costs nothing and reveals nothing. A technique with `controlled_knockback` is aimed with the throw screen.
- **Grapple martial arts.** Judo's three throws, Pankration's grab knee and arm lock, Zui Quan's drunken fall and Brawling's strangle need a grapple with the target (Judo, Pankration and Zui Quan whichever side holds the other). Judo, Aikido and Krav Maga each get a grab counter that fires when something grabs you. Ferals can't swing long weapons while grappling.
- **Monster grabs.** A grab attack with `pull_from_vehicle` can yank a rider without a seatbelt out of a vehicle (the vanilla grab attacks have it). Crowd crush follows TLG's rules, as `CROWD_CRUSH_*` external options, with a `CROWD_CRUSH` world option (on by default) and a `CROWD_CRUSH_RESIST` enchantment value.
- **Explosive exertion.** A new EXPLOSIVE activity level and calorie-diary column. The `EXPLOSIVE_COMBAT_EXERTION` world option (off by default) makes melee, dodging, grappling and throwing use it as TLG does.

For modders, documented in `doc/JSON/` (`MARTIALART_JSON.md`, `EFFECT_ON_CONDITION.md`, `MONSTER_SPECIAL_ATTACKS.md`, `MAGIC.md`, `JSON_FLAGS.md`):

- **Techniques:** `grab_counter`, `controlled_knockback`, `menu_visibility` ("always", "grappling", "grappled", "grapple" or a condition), `fallback` (opt-in through the `MA_BASIC_FALLBACK` flag), and tech effects with `intensity` and `"target": "self"`. Styles: `grapple_weapons`, `ongrab_buffs`/`ongrab_eocs` and `ongrabbed_buffs`/`ongrabbed_eocs` (with `grab_strength` and `grab_part` in context).
- **EOC conditions:** `u_is_grabbing` and `u_is_grabbed` (`"any"` or the other talker: who holds whom, which the GRAB / GRAB_FILTER flags can't tell).
- **EOC effects:** `u_grab` (a direct hold, or an attempt with its cost and roll), `u_release_grab`, `u_throw_grabbed`, `u_fling` (the throw engine without a hold, also a `fling` spell effect) and `u_choose_technique` (the technique menu from an EOC; an NPC picks automatically).
- **Events:** `character_grabs_creature`, `character_releases_grab` and `character_throws_creature`, with the grabber as alpha and the victim as beta.
- **Data:** the formulas are `jmath_function`s in `data/json/grappling.json` and `grappling_world.json`; flags `NO_GRAB` (character) and `GRAB_IMMUNE` (monster); monster grab `pull_from_vehicle`.

Gameplay changes players will notice besides the new actions: Judo's throws need a grapple; Judo, Aikido and Krav Maga answer grabs; Zui Quan's spin and wild swing aren't available in a grapple; riders without a seatbelt can be pulled out of vehicles; crowd crush needs a bigger crowd (a standing medium character: 5 crushers, 4 once down) and drains breath more slowly; forced techniques (counters, `u_attack`) hit with a real fist, foot or weapon, so their damage changes; Wing Chun's knockback punches now step in after the target (`knockback_follow` never worked); the 15 knockbacks that "may fail on enemies grabbing you" only fail against the creature actually holding you; a held monster can't dodge; pressing `G` on a tile with a creature grabs the creature rather than the furniture under it.

Bugs fixed along the way: a monster that let go of a grab still counted as grabbing (`remove_grab` re-added the effect); monster grabs worked on incorporeal targets; `knockback_follow` never moved the attacker, and would have moved the avatar when an NPC used it; a technique that was two kinds of counter (Xedra Evolved's three devil tail counters, dodge and block) never fired, and now answers either. TLG bugs that were not copied: the flat throw cost, the landing tile's trap not going off, matching grabs by intensity instead of by grabber, a monster counting its own grab, a forbidden-effects list that was never cleared and a "pulled" effect that was never applied, and enemy NPCs that could be grabbed with no hit roll.

**NPC errands and EOC UI: modding queue sections A–G.** These were asked for by the owner after the grappling port and are planned in [plans/modding-queue.md](plans/modding-queue.md). That file records what was settled for each section. They were built in two worktrees, one for walking and items and one for activities, examine and UI. A separate agent reviewed each worktree, and the two were then merged. Section H, NPC behavior-tree goals from JSON ([plans/npc-behavior-design.md](plans/npc-behavior-design.md)), comes next and uses these effects.

For modders, everything is documented in `doc/JSON/EFFECT_ON_CONDITION.md` and `doc/PLAYER_ACTIVITY.md`:

- **Walking.** `u_walk_to` / `npc_walk_to` send a character to a location variable. Options:
  - `range`, `run`, `give_up_after`, `interruptible`
  - `true_eocs`/`false_eocs`, which run when the walk ends

  The walk is saved with the character. An NPC walks it through the existing "player order" node of its decision tree, so a fight or an urgent need still comes first. `u_cancel_walk` ends a walk. `u_is_walking_to` reads one.
- **Items.** `u_pickup_item`, `u_wield_item`, `u_wear_item` and `u_drop_item` act on the item talker, optionally walking to it first (`"walk": true`). The item talker follows the item, so "pick up, then wield" works in one EOC. Taking someone else's item counts as theft unless `allow_theft` is false.
- **Activities with a target** (ACT-J02). `u_assign_activity` takes `target_item`, `target_loc`, `target_monster`, `recipe` and `butcher_type` for reading, butchery, disassembly, crafting (new or in progress), wearing, unloading and reloading, and for 38 more activities on items, places and pets. `true_eocs`/`false_eocs` now work with every activity.
- **Examine.** `u_examine` / `npc_examine` operate an adjacent tile on the same level:
  - Doors, gates and hatches open or close.
  - The avatar otherwise gets the normal examine.
  - An NPC may use EOC examine actions and a short list of built-in ones that ask nothing, such as gate controls and harvesting.
- **UI.** `u_show_text` is a scrollable text window with buttons that run EOCs. `run_eoc_checklist` is a menu to tick several options.
- **Conditions.** `u_has_item_in_hand`, `u_can_reach` and `u_is_doing_activity`.
- **Results.** Every effect reports how it went: `true_eocs` or `false_eocs` run with a `reason` context value, such as `arrived`, `blocked`, `owned` or `no_room`. None of them opens a menu for an NPC. A targeted activity asks the player nothing either: a choice it would need, such as butchering human remains or unloading a gun with several things in it, is refused with a reason.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| NPC-B17 | fixed | `4bfe5f91`, `2dfcf68d` | `tests/npc_errand_test.cpp` |
| NPC-B14 | fixed | `b537b04e`, `2dfcf68d` | `tests/npc_errand_test.cpp` |
| ACT-J02 | fixed | see the third-round table above | `tests/eoc_errands_activity_test.cpp`, `tests/audit_partial_activities_test.cpp` |

Gameplay changes players will notice:
- **"Move there!" orders** (NPC-B14):
  - A follower keeps the order across a save and load.
  - With no way there, it says "I can't get there." and gives up.
  - It stops next to the spot if someone is standing on it.
  - "Follow me" cancels the order.
- **No more teleporting NPCs** (NPC-B17). An NPC whose path went stale no longer hops over the tiles in between; it re-routes or waits.
- **Resuming a craft.** A craft in progress that was missing components used them up and then stopped a turn later with "don't have the required components". It now keeps going.
- **NPC reloading and unloading.** Messages name the NPC instead of saying "You", and an NPC's reload no longer opens a menu.

With this batch the full suite passes, 2656 of 2656 test cases with seed 4242 and no errors logged, and every mod set still loads with no errors. The 52 new test cases are in `tests/npc_errand_test.cpp`, `tests/eoc_errands_activity_test.cpp` and `tests/eoc_errands_ui_test.cpp`.

**Performance: twelve more high-severity findings.** These were built in three worktrees: combat and NPC attacks, loading and saving, and world and inventory. Each benchmark scenario was committed before its fix. The combat and world worktrees first committed tests that pin the old behavior, passing against the old code; the load fixes carry their tests in the same commits. A separate agent reviewed each worktree, and its comments were applied before merging. The numbers are in [benchmarks/README.md](benchmarks/README.md#round-4-twelve-more-high-severity-performance-findings).

| ID | Status | Commits | Regression test or benchmark |
|---|---|---|---|
| NPC-P01 | fixed | `d07b5cfb` | `tests/audit_perf_combat_test.cpp`; benchmark `npc_spell_eval` |
| MAGIC-P02 | fixed | `d07b5cfb` | `tests/audit_perf_combat_test.cpp`; benchmark `npc_spell_eval`. Also covers MAGIC-P04 and MAGIC-P05 inside NPC spell rating; casting and the targeting UI still use the original shape functions |
| NPC-P02 | fixed | `ed1a52f8` | `tests/audit_perf_combat_test.cpp`; benchmark `npc_weapon_eval` |
| CMBT-P01 | fixed | `f7f5226a`, `384c6872` | `tests/audit_perf_combat_test.cpp`; benchmarks `shot_flockshot`, `shot_birdshot`. Changes the order of a shot's random numbers (see below) |
| CMBT-P02 | not changed | none | benchmark `shrapnel_armored`. A grenade among 25 armored zombies takes about 13 ms (about 170 fragment hits), so the full attack per fragment isn't worth replacing |
| EVT-P01 | fixed | `209d2263`, `b3fb580f` | `tests/audit_perf_load_test.cpp`; benchmark `stats_overmap_special_history` |
| MAP-P03 | fixed | `334b707a`, `74477812` | `tests/audit_perf_load_test.cpp`; benchmarks `mapbuffer_save_uniform`, `mapbuffer_lookup_missing` |
| INV-P01 | fixed | `baed3534`, `e0f68119`, `0555b3a8` | `tests/audit_perf_load_test.cpp`; benchmark `item_nested_deserialize` |
| INV-P02 | fixed | `445dec4e`, `380292c3`, PR D | `tests/audit_perf_world_inventory_test.cpp`, `tests/audit_partial_perf_ai_test.cpp`; benchmark `inventory_queries`. The visit APIs and `item_pocket::is_type` take a `pocket_type_set` bitmask, built from the same braced lists, so callers are unchanged; `get_pseudo_items()` returns the cached list by reference; `visit_items_raw()` is the public raw visitor, which `has_item` and `has_item_with` use |
| MGEN-P01 | fixed | `fdaaf2e6`, `8f06ed83` | `tests/audit_perf_world_inventory_test.cpp`; benchmarks `phase_change_lake_thawed`, `phase_change_lake_frozen` |
| OMAP-P02 | fixed | `e30b243a`, `5e472856` | `tests/audit_perf_world_inventory_test.cpp`; benchmarks `omt_find_closest_miss`, `omt_find_npc_goals` |
| ACT-P03 | fixed | `fbabc40e` | `tests/audit_perf_world_inventory_test.cpp`; benchmark `zone_job_missing_tool` |

The pinning tests compare each fast path with the old code. They cover every NPC spell and weapon rating against the full scan, including the random number state afterwards; the overmap searches against a copy of the old ones, including ties and which overmaps get generated; a year of lake loads against a copy of the old phase pass; and zone job scans with and without the tool. The shot tests compare damage and kill rates with the old code over many trials. The tests added after the world group's review were each checked by breaking the code they cover.

Gameplay changes players will notice:
- **Shotgun shots** (CMBT-P01). The pellets of a shot now fly together, a tile at a time, so their random numbers come in a different order and the same seed gives a different shot. Outcomes are statistically the same: for example, 00 buckshot at a 150 HP target averages 152.5 damage both ways. Effects that reach other tiles in the middle of a shot, such as a creature that splits or explodes on death, return fire or an on-hit EOC, can meet later pellets at a slightly different moment.
- **Saved item references** (INV-P01). Items inside containers keep their uids across a save and load, so a saved reference into a container no longer fails with "item_location UID not found in container contents". A reference into a gun or tool mod, such as an underbarrel gun's magazine, is now saved too. Characters made from a template get fresh item uids.
- **Achievements on "last overmap special entered"** (EVT-P01). Entries saved by older builds count as entering no special. Achievements already completed stay completed. An old history can no longer complete one on load.
- **Frozen water** (MGEN-P01). A tile that stays frozen is no longer thawed and refrozen on every load, so the avatar's memory of it is no longer cleared. It is re-memorized when next seen.
- **Map saves** (MAP-P03). A map segment whose quads are all sky or rock no longer gets an empty `.zzip` file. The save format is unchanged, and older builds read new saves.

For modders:
- `avatar_enters_omt` has a third field, `overmap_special`, empty when the OMT is in no special. A `trigger_event` for it needs three arguments. EOCs on the event get an `overmap_special` context variable. Older builds ignore the field in saves.
- The `overmap_special_at` field transformation only consults overmaps already in memory, so no statistic can load or generate an overmap.
- An item location saved into a mod pocket uses the new type `in_mod_pocket`. Older builds read it as `nowhere`, as they wrote it.

With this batch the full suite passes, 2688 of 2688 test cases with seed 4242 and no errors logged. The 32 new test cases are in `tests/audit_perf_combat_test.cpp`, `tests/audit_perf_load_test.cpp` and `tests/audit_perf_world_inventory_test.cpp`, and every mod set still loads with no errors.

**NPC behavior from JSON: modding queue section H (NPC-J03).** Mods can now add NPC behavior, not just rearrange the vanilla tree. The design was settled with the owner in [plans/npc-behavior-design.md](plans/npc-behavior-design.md). It was built in two worktrees: the tree (editing, conditions, scores and load checks) and the goals (categories, JSON goals, EOC reads and debugging). A separate agent reviewed each one. The two were merged, and the review fixes were made on the merged code.

For modders, everything is documented with examples in `doc/JSON/NPC_BEHAVIOR.md`:

- **Editing the tree.** `behavior` nodes support `copy-from`, `extend` and `delete`, and a child can be inserted `before` or `after` another. Built-in nodes, combat included, can be deleted, re-conditioned and re-scored. An edit that doesn't apply, such as two mods deleting the same child, is reported and skipped.
- **Conditions.** `"predicate": "condition"` takes any dialogue condition, with the NPC as `u`; `npc_` conditions read nothing there. New conditions `u_is_in_faction` / `npc_is_in_faction` let a branch apply to one faction's NPCs.
- **Scores.** A score can be a math expression, or a built-in score with a multiplier. `oracle('name')` reads a built-in score in math. `evaluate_every` caches a node's conditions and score per NPC.
- **Categories.** These are the `npc_decision_category` type. A mod adds its own `before` or `after` a built-in one, with its own hysteresis.
- **JSON goals.** A leaf node can carry a `category` and a `goal`:
  - `on_start` and `on_turn` EOCs
  - an `until` condition and a `timeout`
  - `on_end`, `on_interrupt` and `on_timeout` hooks, with context values `goal_id`, `turns_active` and `reason`

  A goal uses the errand effects, such as `u_walk_to`, `u_pickup_item` and targeted activities, to do its work. It is saved with the NPC. A goal with neither `until` nor `timeout` lasts while its node applies.
- **Reading goals.** `u_npc_goal`, `npc_goal_category`, `npc_goal_turns()` and the `npc_goal_changed` event. The event fires for built-in goals too, but statistics don't keep it, so saves don't grow.
- **Debugging.** Debug menu → Console → Creatures → "NPC decision" shows, for a chosen NPC:
  - each condition and score
  - the category order
  - the committed goal, and why it won
- **New load errors:**
  - unknown, duplicate or cyclic children, and a tree with no reachable goal
  - NPC and monster predicates mixed
  - a bad predicate argument, an unknown `oracle()` name, and typos in `extend` / `delete`
  - a JSON goal reusing a built-in goal's id, given two categories or an unknown category
  - a built-in goal left without a category

| ID | Status | Commits | Regression test or benchmark |
|---|---|---|---|
| NPC-J03 | fixed | `f2cc3d53`, `e2f62418`, `844f4cb0`, `a31114d8`, `950645fc`, `d22af887`, `52725bb8` | `tests/npc_behavior_tree_test.cpp`, `tests/npc_json_goal_test.cpp`; benchmark `npc_decision` |
| NPC-J05 | fixed | `e58bc280` | `tests/npc_json_goal_test.cpp`. An NPC's goal is readable. Attitude, personality and faction relations were done later (see the row below); PR C adds `u_has_fac_relation`, the relation between the two talkers' factions |

Vanilla NPCs decide as before:
- A test pins every node of `npc_decision`: its strategy, conditions, score and goal.
- The built-in categories keep today's order.
- The NPC suites pass unchanged.

The built-in end and interrupt rules (when a fight ends, the urgency margins between needs) are keyed by goal, so moving a built-in goal to another category changes only its rank. Running the new tests in the full suite also turned up two old test bugs. The NPC follower-rules tests left their NPCs in the overmap buffer, at an invalid position. Three horde tests left the reality bubble about 90 overmap tiles away, where the test date is below freezing, so a later test's water froze. Both now restore what they change.

Cost: the `npc_decision` benchmark (300 turns of the tree for 20 NPCs) takes 53 ms before and 55 ms after, medians of three interleaved rounds whose ranges overlap, so the vanilla tree costs the same within noise (about 9 µs per NPC per turn). Four condition and math nodes add about 4 ms, under 1 µs per NPC per turn, and `evaluate_every: 5 turns` takes back most of that. The numbers are in [benchmarks/README.md](benchmarks/README.md#section-h-the-npc-decision-tree).

With this batch the full suite passes, 2734 of 2734 test cases with seed 4242 and no errors logged. The 46 new test cases are mainly in `tests/npc_behavior_tree_test.cpp` and `tests/npc_json_goal_test.cpp`, and every mod set still loads with no errors.

**NPC follow-ups (NPC-J04, NPC-J05, NPC-J10) and bug sweep, slice 2.** These were built after section H, in five worktrees: the NPC follow-ups, and four bug batches covering chapters 1 to 5 (EOC and dialogue, math and talkers, events and data loading, game loop and weather). A separate agent reviewed each worktree, and its comments were fixed before merging. The batches built and passed their focused tests on their own, then were merged and run through the full suite once.

For modders (documented in `doc/JSON/EFFECT_ON_CONDITION.md`, `NPCs.md`, `NPC_BEHAVIOR.md` and `FACTIONS.md`):
- **NPC events.**
  - New events:
    - `npc_attitude_changed`, with a `cause`: `effect`, `angered`, `opinion`, `mutiny`, `faction`, `panic`, `theft` or `ai`.
    - `npc_recruited`, `npc_dismissed` and `npc_mutinies`.
  - `npc_becomes_hostile` now fires for every way an NPC turns hostile, once until it calms down again, and not at spawn.
  - A faction's `mutiny_faction` says where mutineers go.
- **Reading and changing NPCs.**
  - Math: `npc_personality()` (assignable), `npc_danger()`, `npc_panic()` and `npc_need_urgency()`.
  - Conditions: `npc_attitude`, `npc_mission`, `npc_on_shift` and `npc_at_post`.
  - Effects: `npc_set_attitude` and `npc_set_mission`, which go through the same code as the matching dialogue effects.
  - Factions: `faction_has_relation` and `set_faction_relation`.
- **AI tuning.** An `npc_class` can set `ai_tuning`: need thresholds, urgencies, starvation, camp meal size and sleep timings. Vanilla classes keep today's values exactly.
- **EOCs.**
  - About 20 effects and conditions no longer crash on the wrong kind of talker; they give one debug message instead.
  - An EOC chain stops at 250 levels deep, with one message.
  - An EOC queued with no delay from inside a queued EOC runs a turn later.
  - `failure_topic` responses can be picked.
  - `u_pickup_items` limits and `u_pick_bodypart` filters work.
  - The documented object form of `take_control` loads.
- **Math.**
  - `%` groups left to right; no shipped JSON changes result.
  - `time_since('noon')` and `time_until('noon')` are right before midday.
  - `time()`/`calories()` assignments honour `unit` and `format`.
  - `val('pos_x/y/z')` assignment moves like a teleport.
  - `time_until_eoc()` returns the time left.
- **Events.**
  - EVENT EOCs no longer fall back to the avatar when the event's character can't be found. For example, a monster hurt by bleeding is now `u`.
  - `trigger_event` arguments are checked.
  - Timed map changes happen in the dimension they were made in.
- **Data and mods.**
  - `mod_migration` renames work, including chains.
  - A plain-number weather `temperature_modifier` means °C.
  - When a world loads with a broken mod list (a missing dependency, a conflict or the wrong order), a prompt offers to fix it, load anyway or go back, as the owner chose.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| NPC-J04 | fixed | `c1e9b5fa`, `6ba4f89e`, `3b3f2823` | `tests/npc_ai_events_test.cpp` |
| NPC-J05 | fixed | `e58bc280` (section H), `e0be4807`, `dbaf7c8b` | `tests/npc_ai_eoc_test.cpp` |
| NPC-J10 | fixed | `28437d7e`, PR D | `tests/npc_ai_tuning_test.cpp`, `tests/audit_partial_perf_ai_test.cpp`. `ai_tuning` also sets `urgency.embark` and `urgency.embark_seated` (0.6 and 0.15), `cold_body_temperature` (31 C) and `sleep.restless_window` (30 minutes); vanilla classes keep these. The new type `npc_need` adds a need: an urgency (a number, a variable or math), the urgency it acts from, a condition and a JSON goal's hooks. Each becomes a leaf under `npc_needs` (or a parent it names) next to the built-in needs, which stay as they were, and `npc_need_urgency()` reads it |
| EOC-B06 to EOC-B15 | fixed | `8c0da604` to `ff0b0985`, follow-ups `dae7c28f` to `db21dfb9` | `tests/audit_eoc_dialogue_test.cpp` (EOC-B09's UI path and EOC-B10's hotkey read are checked by reading) |
| MATH-B03 to MATH-B11, LOOP-B10 | fixed | `eb2fd836` to `94250384`, follow-ups `242c90f2` to `a4106430` | `tests/audit_math_talkers_test.cpp` |
| MATH-B12 | already fixed | by LOOP-J01 (`a17a8639`, `e1b3f7ef`) | existing `loop-j01_eoc_weather_overrides_hold_until_they_run_out` |
| EVT-B03 to EVT-B10 | fixed | `6c203ba4` to `cbe83093`, follow-ups `3218e2c6`, `8c7b137f`, `5cc75dd7`, `94ea0bb4` | `tests/audit_events_sweep_test.cpp` (EVT-B06 needs a full game setup and is checked by reading) |
| DATA-B04, DATA-B06, DATA-B07, DATA-B08 | fixed | `ef1af991`, `70395ba2`, `5e336317`, `42771b3b`, follow-ups `28d1c58b`, `bbc9087d`, `423cafa9`, `bb3e39f4` | `tests/audit_data_sweep_test.cpp` |
| DATA-B05 | fixed | `108c2b82`, PR D | `tests/audit_data_sweep_test.cpp`, `tests/audit_partial_data_test.cpp`. Unloading data clears the mutation types and both removal blacklists |
| DATA-B09 | fixed | `3cbe8ccc`, `f67fe1f3` | `tests/audit_data_sweep_test.cpp` |
| LOOP-B05, LOOP-B12 | already fixed | by EVT-B01 (`90c08ef9`) and MAP-B03 | existing tests |
| LOOP-B06 to LOOP-B09, LOOP-B13 to LOOP-B15 | fixed | `12247a06` to `d27488aa`, follow-ups `858c62d4` to `e37c37f7` | `tests/audit_loop_sweep_test.cpp` (LOOP-B07 is a menu and is checked by reading) |
| LOOP-B11 | fixed | `6fcccdc6`, PR C | `tests/audit_loop_sweep_test.cpp`, `tests/audit_gameplay_fixes_test.cpp`. The avatar knocked into deep water lands in the first deep tile, swimming, wet, and sinking if unable to swim, as walking in does; no move cost, and the map isn't re-centred mid-knockback. NPCs knocked into water are still unhandled |

Gameplay changes players will notice:
- **Seasons for water and maple sap** were 81 days early.
  - Water is now warmest in early August and coldest in early February, so swimming at a default spring start is much colder: about 40 °F instead of 66 °F.
  - Maple sap runs from late February to early May.
- **Scarlet storms** now chill the surface by 2 to 30 °C. Their values had been read in a unit that made them do almost nothing. Underground is unaffected.
- **Weather is consistent:** the same time and place give the same weather, and only live weather updates turn the wind. Wind direction now lasts hours instead of changing almost every update. Weather keeps working while you're in a basement or underwater.
- **Alarms.**
  - "Set alarm for dawn" sets the alarm for dawn.
  - The motion alarm goes off at most once a turn, only for unfriendly monsters that moved or appeared in range.
- **Saves.**
  - A failed save no longer leaves the character file and the world out of step.
  - A failed trip to another dimension keeps your vehicle, passengers, items and NPCs. A tow line is left unhitched.
  - Mortar shells survive a save.
  - Lake and open-air areas keep partial constructions and spawns.
- **Dialogue.**
  - "Go back" in an NPC's opening topic stays in the conversation.
  - Responses with a `failure_topic` can be picked: for example, the Old Guard's "You don't have enough." now goes back instead of being greyed out.
- **NPCs.**
  - A new character no longer starts with the previous one's memorial.
  - The memorial logs "became hostile" exactly as before.
  - Knocked-back monsters drown in the deep water they cross, not the shallow water they started in.
- **Teleports.** A teleport that is refused (`u_teleport`, spells, `val('pos_*')`) changes nothing: you keep your seat, mount and grabs.
- **Mod content updated** to keep its behavior under the new rules:
  - Xedra's six paraclesian spell selectors, its brownie blink and the haunting spawn no longer recurse.
  - Magiclysm's and MindOverMatter's item-count EOCs no longer recurse.
  - Three MindOverMatter and Xedra damage EOCs need an attacker.

With these batches the full suite passes, 2834 of 2834 test cases with seed 4242 and no errors logged, and every mod set still loads with no errors. The 100 new test cases are mainly in `tests/npc_ai_*_test.cpp` and `tests/audit_{eoc_dialogue,math_talkers,events_sweep,data_sweep,loop_sweep}_test.cpp`. The tests of the changed mod content are hidden and run with those mods loaded (`--mods=no_npc_food,xedra_evolved,mindovermatter,magiclysm "[xedra_evolved],[mindovermatter]"`); they pass.

**Mod settings (DATA-J01) and bug sweep, slice 3.** Mod settings were built from the design in [plans/mod-settings-design.md](plans/mod-settings-design.md). The owner made its four main decisions and accepted its three proposed defaults. The sweep took the 51 medium bugs left in chapters 12 to 18 (inventory, item use, character, health, progression, mutations and bionics, combat), in four batches. All five were built in their own worktrees. A separate agent reviewed each, and the review fixes went in before one merge and one full suite.

For modders, mod settings are documented in the new `doc/JSON/MOD_SETTINGS.md`:
- **Declaring a setting.** A `mod_setting` is an on/off switch, a number with min, max and step, a choice from a list, or one line of text. It is stored per world in `worldoptions.json`; only values that differ from the default are kept, and older builds skip them safely.
- **Where players change them.** World creation has a "Mod settings" tab per mod. Settings marked `changeable_in_game` can also be changed on an in-game options page; changes made in game are saved with the game.
- **Using them from EOCs.**
  - Math: `mod_setting('id')`.
  - Conditions: `mod_setting_on` and `mod_setting_is`.
  - Effect: `set_mod_setting`. EOCs may write only settings marked `eoc_writable`.
  - Event: `mod_setting_changed`.
- **Changing built-in world options.** `WORLD_OPTION` now changes a built-in world option's default: an option the player set keeps its value, and copying a world keeps choices. Mods can add their own `option_slider`s.
- **Loading.** World creation reads only the selected mods' settings (about 0.2 s for the base game's data the first time, 0.02 s after). A `mod_setting` in `data/core` or in a world's own mods folder is a load error.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| DATA-J01 | fixed | `09292ab7` to `a856569b`, follow-ups `c740b900` to `21a3c996` | `tests/mod_settings_test.cpp` (the menus need a display; the data behind them is tested) |
| INV-B02 to INV-B06, INV-B08, INV-B09 | fixed | `33fcfd60` to `a13640bc`, follow-ups `f787c1b0`, `402e7eb6`, `98a55f2b` | `tests/audit_containers_inventory_test.cpp` |
| INV-B07 | already fixed | — | an item's quality includes its contents, so a pipe in a backpack counts |
| USE-B05 to USE-B07, USE-B09 to USE-B11 | fixed | `b2add1ad` to `2ac75c2c`, follow-ups `873961e2`, `a6d42c22`, `a615c6bd`, `03455f41` | `tests/audit_item_use_actions_test.cpp` (USE-B05 is a menu; checked by reading) |
| USE-B08 | fixed | `a11ea763`, PR D | `tests/audit_partial_gameplay_test.cpp`. `"consume": true` (or `_charges` above 0) uses up an item with no charges to spend: the item, carried or where it lies, or one of a stack counted by charges. The double consumption came from items whose EOC already removes them and that set `consume` anyway, while it did nothing: Sky Island's skyward beacon (removed when the warp completes, after the activity and the query) and flotation stone, the core serums, contact lenses and saline kits, Mind Over Matter's matrix crystals and Xedra Evolved's revenant effigy. They no longer set it, so nothing changes for them. Mind Over Matter's black (drained) crystal relied on `consume` alone and is now used up when gazed into |
| CHAR-B05 to CHAR-B08, CHAR-B10, HLTH-B11 | fixed | `e92cd5d2` to `b25844ab`, follow-ups `b721a3c3`, `f1be6837`, `4bf70127` | `tests/audit_char_sweep_test.cpp` |
| CHAR-B09 | already fixed | by the grappling port; follow-up `0b3b3d74` covers a grabber directly above or below | `grapple_monster_grab_at_range_is_not_an_orphan`, `tests/audit_char_sweep_test.cpp` |
| PROG-B04 to PROG-B11 | fixed | `a206241a` to `ab86dc0b`, follow-ups `0ce1d2ae`, `0bf913d6` | `tests/audit_progression_sweep_test.cpp` (PROG-B08's popups checked by reading) |
| MUT-B06 to MUT-B09, MUT-B11, MUT-B12 | fixed | `6a14aab0` to `83e9c0b9`, follow-ups `178141ee` to `e11c1867` | `tests/audit_mutations_bionics_sweep_test.cpp` (MUT-B12 is a menu; checked by reading) |
| MUT-B10 | fixed | `cdc146af`, `aa81f2c6`, `c8896f6f`, PR D | `tests/audit_mutations_bionics_sweep_test.cpp`, `tests/audit_partial_gameplay_test.cpp`. Each CBM saves the experience it added to raise a spell the character already knew (`raised_spell_xp` in the bionic's save data). Removing it takes back exactly that, keeping experience gained since; if another installed CBM teaches the spell at a higher level, the spell stays there and that CBM takes over the record. An upgrade takes over the record for the spells it also teaches. CBMs from older saves have no record and leave the raised level, as before |
| CMBT-B04, CMBT-B06 to CMBT-B12, CMBT-B14 to CMBT-B17 | fixed | `0b68fd71` to `ea35a4ad`, `e018a3ae`, follow-ups `08d4845b` to `c0c49dff` | `tests/audit_combat_test.cpp` |
| CMBT-B05 | already fixed | by the grappling port (`165f6fc8`) | `knockback_follow_steps_into_the_tile` |
| CMBT-B13 | fixed | `4f35c8ee`, PR D | `tests/audit_combat_test.cpp`, `tests/audit_partial_gameplay_test.cpp`. A character's gunshots (once a shot for each creature hit, however many pellets) and thrown hits cast its enchantments' `hit_you_effect` too, not against hallucinations; EOC spells get the kind of hit as the context value `hit_type`. Each spell can be limited with `"hit_types"` (`melee`, `ranged`, `thrown`; default all), and the 26 shipped spells that are effects of contact are marked melee-only: melee weapons' WIELD enchantments, Aftershock's melee optimization and electric bionic blade, Xedra's electric fist, claws, auras, burning weapon and erosion, and powers described as melee or strikes (Mind Over Matter, Magiclysm's mana siphon and Ice Elemental, Xedra's sylph and chronomancer). Now also on ranged and thrown hits, within their spell's range: Blinding Radiance and Blue Elixir (Mind Over Matter), the Blood, Glacier, Void and Wither Mage attunements and Electric Waves (Magiclysm). Monsters still cast theirs on melee hits only. A weapon from an older save carries its saved inline enchantment, without the melee-only mark, until a game is loaded with it wielded or worn |

The owner signed off on the balance changes: CMBT-B04, CMBT-B12, CMBT-B16 and the weak-point defaults.

Gameplay changes players will notice:
- **Combat.**
  - Weak points are found as intended: skilled attacks hit eyes, heads and vitals more often. At skill 10, thrown damage against zombies rises 16–33%.
  - Weak-point effects work as written. The brain's instant kill needs a damaging hit. Stinger effects and about 150 stagger, knockdown and blind effects actually happen.
  - HARDTOHIT works in melee (roll twice, keep the worse). Stacking martial-art accuracy buffs give their bonus per stack; Zui Quan's and Silat's dodge buffs get much stronger.
  - Melee from a moving vehicle loses 1 accuracy per mph, not 100.
  - NPC sweeps can hit the player and followers.
  - Disarms can't take bionic blades or handcuffs, and impale no longer crashes.
- **Character.**
  - Falls are never harmless at high DEX and dodge, except with feather fall or an enchantment.
  - Short hallucination doses (LSD, the taint stare) go through their stages.
  - Dragging a vehicle over stairs follows the dragging rules.
  - Character creation: switching profession drops excluded hobbies and traits, the skills tab shows real starting levels, and duplicate addictions merge.
  - Lab mutant NPCs and Aftershock's afterlife backstories now load.
- **Bionics.**
  - Self-installed CBMs need slots and a suitable body.
  - Removing a CBM keeps what you knew before.
  - Fuel bionics draw one source per turn.
  - Nurse-bot failures can hurt, and kill, the patient.
- **Items.**
  - The gas UPS burns about 7.5× less propane.
  - Stoves are charged once; the kerosene cooker, soda can stove and several mod stoves heat food on their own.
  - Auto pickup never opens sealed packs or spills liquids.
  - Salvage gives back crafted items' materials.
  - Cancelling a plank cut keeps the log.
  - Small isolated pools refuse fishing, ponds don't.
  - Magiclysm's bags of holding hasten rot again.

Merging the five branches surfaced two interactions, fixed in `65e62c13` and `b76d5258`:
- **Proficiency practice.** PROG-B10's learn-with-prerequisites cleared practice in progress, while MUT-B10 restores practice when a CBM is removed. Now only a learn with prerequisites clears it.
- **Polearm DPS test.** CMBT-B15 lowered NPCs' rating of polearms to the intended 0.8, so `expected_weapon_dps` takes the new values for 17 polearms.

Two older tests were updated to the new behavior: the plank log stays until the cut is done, and a test weight was given a realistic volume.

With this batch the full suite passes, 2923 of 2923 test cases with seed 4242 and no errors logged, and every mod set still loads with no errors. The 89 new test cases are mainly in `tests/mod_settings_test.cpp`, `tests/audit_containers_inventory_test.cpp`, `tests/audit_item_use_actions_test.cpp`, `tests/audit_char_sweep_test.cpp`, `tests/audit_progression_sweep_test.cpp`, `tests/audit_mutations_bionics_sweep_test.cpp` and `tests/audit_combat_test.cpp`.

**Verification.** Each fix was compile-checked with the project's `-Werror` flags and reviewed by a separate agent. The game and test suite were then built (`make TILES=0 RELEASE=1 PCH=0`) and run. All 35 `[audit]` test cases pass. The 31 from the first round all fail or crash on a build with their fix reverted, so they really catch the bugs; the 4 for ITEM-B02 and EVT-B02 were checked by review. The full suite passed then: 2313 of 2313 test cases with `--rng-seed 4242`.

After groups 4 and 5, the round 2 performance fixes and the 48 remaining bugs, all 88 `[audit]` test cases pass and the full suite passes: 2366 of 2366 test cases with `--rng-seed 4242` and with `--rng-seed 777`. After the PR review fixes that followed (a use-after-free in zone lookups from ACT-P01, save-load errors not aborting the load, stale crafting caches after a trade, John Bailey never leaving the refugee center), the suite passes again: 2366 of 2366 with seed 4242. After the second round of remaining high-severity findings, all 109 `[audit]` test cases pass and the full suite passes: 2387 of 2387 with seed 4242. Every mod set loads as before. After the third round (modding round 1 and bug sweep 1), all 228 `[audit]` test cases pass and the full suite passes: 2506 of 2506 with seed 4242, with no errors logged. Every mod set still loads with no errors. The newer audit tests were written to fail on the old code, but unlike the first round they were not each re-run against a reverted build.

Getting there needed four test fixes (commits `ae6b8464`, `2d15a016`). Five tests failed in full-suite runs but passed alone, because earlier tests left state behind. The main culprit: `clear_character()` did not reset the stat bonuses that hardcoded effects add, so after a frostbite test the avatar kept -2 INT, which slowed learning by 10% in later crafting tests. Three other tests (`memorials`, `available_reloadables`, `monster_speed_description`) now clear the map or the avatar themselves.

**Build note.** With the default precompiled header (`PCH=1`) and GCC 13, the build's dependency files list no project headers, so editing a `.h` file does not rebuild the files that include it. Build with `PCH=0` (or clean first) after any header change. A full test-suite run peaks around 10 GB of memory, so two runs at once do not fit in a 16 GB container.

**Gameplay changes players will notice.**

- Bandages and other stop-bleed items now work at First Aid 0, and stop-bleed power is higher at every skill level (USE-B04).
- Ablative plates protect only on hits that land on them, and only once, so torso damage from bullets rises for plated vests (IINF-B01).
- Craft failures now happen at the rate the crafting menu shows, so low-skill crafting loses components and progress more often (CRAFT-B01).
- Frostbite risk rises with wind and cold instead of falling (CHAR-B02).
- Creatures hit by vehicles or thrown by hulks fly the full distance and no longer take self-collision damage; other characters in the way stop them (LOOP-B01).
- Effects that change speed now change monster speed (MONAI-B01).
- On hard difficulty presets, monster group time gating works again (MONDT-B02).
- With broken legs you stay prone until you choose to stand, and running no longer turns itself back on after running out of stamina (CHAR-B03).
- NPCs reload mid-fight, fire UPS energy guns, and hold fire when a friend is in the line of fire (NPC-B01).

### Quick EOC/JSON fixes

A round of small fixes from the JSON and EOC chapters: crashes, documented features that silently did nothing, docs that had drifted from the code, and missing EOC reads. The regression tests are in `tests/audit_quick_fixes_{data,eoc,math,world,items}_test.cpp`, with their fixtures in `data/mods/TEST_DATA/audit_quick_fixes_*.json`. Review fixes are in `9c34f451`.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| EVT-J01 | fixed | `91724708` | `audit_quick_fixes_data_test.cpp`. The triffid heart's death spell now queues `roots_die` |
| MAGIC-J04 | fixed | `91724708` | `audit_quick_fixes_data_test.cpp`. `timed_event` names match in any case; unknown names are reported at load |
| DATA-J03 | fixed | `91724708` | `audit_quick_fixes_data_test.cpp` |
| CRAFT-J04 | fixed | `91724708` | `audit_quick_fixes_data_test.cpp` |
| EOC-J02 | fixed | `3fb1f946` | `audit_quick_fixes_eoc_test.cpp`. `run_for_npcs` still covers loaded NPCs only; the docs now say so |
| ITEM-J01 | fixed | `3fb1f946`, `9c34f451` | `audit_quick_fixes_eoc_test.cpp`. New `u_has_fault`, `u_has_fault_of_type`, `u_remove_fault`, `u_remove_fault_of_type` |
| INV-J04 | fixed | `3fb1f946`, `9c34f451` | `audit_quick_fixes_eoc_test.cpp` |
| EVT-J02 | fixed | `3fb1f946` | `audit_quick_fixes_eoc_test.cpp` |
| EVT-J03 | fixed | `3fb1f946` | `audit_quick_fixes_eoc_test.cpp` compares the doc table with `event.h` |
| MATH-J01 | fixed | `3fb1f946`, `9c34f451` | `audit_quick_fixes_math_test.cpp`. `spell_difficulty()` stays read-only (the docs point to `spellcasting_adjustment`); `val('weight')`/`val('volume')` were removed from the docs, `weight()`/`volume()` cover them |
| MATH-J02 | already covered | `3fb1f946` (doc link) | `npc_personality()` from NPC-J05 reads and sets it |
| MUT-J04 | fixed | `3fb1f946` | `audit_quick_fixes_math_test.cpp` |
| ACT-J04 | fixed | `3fb1f946` | `audit_quick_fixes_math_test.cpp`. New `activity_moves_left()` |
| VINT-J04 | fixed | `3fb1f946` | `audit_quick_fixes_math_test.cpp` |
| LOOP-J02 | partly fixed | `104c5f5b`, `9c34f451` | `audit_quick_fixes_world_test.cpp`. Hooks run on teleports and vehicle travel, with context, and stop after 8 crossings in a row. Still open: starting a game doesn't run the start OMT's `entry_eoc` |
| OMAP-J02 | fixed | `104c5f5b` | `audit_quick_fixes_world_test.cpp` |
| LOOP-J03 | fixed | `104c5f5b` | `audit_quick_fixes_world_test.cpp` |
| VINT-J03 | fixed | `104c5f5b` | `audit_quick_fixes_world_test.cpp` |
| USE-J03 | fixed | `8b086ee3` | `audit_quick_fixes_items_test.cpp` |
| USE-J04 | fixed | `8b086ee3` | `audit_quick_fixes_items_test.cpp` (`place_npc` radius); the doc examples were checked by loading them |
| MONDT-J03 | fixed | `8b086ee3`, `9c34f451` | `audit_quick_fixes_items_test.cpp` |
| MONDT-J05 | partly fixed | `8b086ee3` | `audit_quick_fixes_items_test.cpp`. The death EOC gets `loc` and `overkill`. Still open: it runs before the corpse exists, and `monster_takes_damage` has no damage type or body part |

**Behaviour changes.** `avatar_enters_omt` EVENT EOCs and OMT entry/exit EOCs now also run when a teleport or a vehicle moves the avatar into another OMT, so content keyed to entering a place (in Aftershock, the smuggler ravine's pirates turn hostile on entry unless you're allowed in) now reacts to driving in too. The triffid heart's death now removes the root walls and sends `destroys_triffid_grove`, as intended. A butchery requirement with too few creature sizes is a load error instead of a crash at the butcher menu.

### The last high-severity findings

The ten high-severity findings no earlier round had touched: eight modding gaps (all large) and two medium performance findings. Each was built in its own worktree on top of the partly-done round (PR D), then merged. The regression tests are in `tests/audit_high_*_test.cpp`, with fixtures in `data/mods/TEST_DATA/audit_high_*.json`.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| USE-J02 | fixed | PR E | `tests/audit_high_use_test.cpp`. Object-form use actions (use, tick, countdown and drop) take an optional `"id"` (default: the type) and a `"menu_text"`, and use actions and `ammo_scale` are keyed by it, so an item can have several actions of one type, each with its own name, ammo_scale, item action menu entry, `u_activate` id and EOC `_method`; `copy-from` `extend`/`delete` work by id. A type name still finds the first action of that type, so C++ and old saves keep working; id-only actions have no key binding, and a redefined inherited action keeps its inherited ammo_scale. |
| VINT-J01 | fixed | PR E | `tests/audit_high_vehicle_test.cpp`. Vehicle part math reads by part id (`'part'` kwarg) and in hit points (`'format': 'hp'`); new `u_vehicle_type` condition and mutator; new effects `u_repair_vehicle_parts`, `u_install_vehicle_part`, `u_remove_vehicle_parts` (no splits unless `split`, removal cleaned up at the end of the turn so the talker stays valid), `u_toggle_vehicle_parts` (engines included) and `u_add/remove_vehicle_chat_topic`; vehicle prototypes take `chat_topics` and get a "Talk to" menu entry. Fuel, battery, engine, speed and owner were already covered by VEH-J01. Not done: renaming, re-aiming installed lights, removing cables |
| DATA-J02 | fixed | PR E | `tests/audit_high_factory_test.cpp`. Vitamins, skills, mutation categories, vehicle groups, dreams and speech load through generic_factory (copy-from, abstract, a later mod's partial copy, extend/delete on their lists); talk topics can delete or replace earlier responses and keep their portrait; JSON_INHERITANCE.md lists every type from a registry a test checks. Left: effect_type keeps HLTH-J02's equivalent loader; vanilla speech lines have no ids, so mods can only add to them |
| MAP-J02 | fixed | PR E | `tests/audit_high_fields_test.cpp`. Field types pick their per-turn behaviours (`processors`: fire, fall, arc, push_items, vents...) and creature contact (`contact_processors`) in JSON, with `decay_mode`, `opaque_variant` and per-intensity `heat_radiation`, `hot_air_field`, `contact_damage` and `eoc_on_contact`; vanilla fields carry their old values, and electricity in smoke or walls (`fd_electricity_unlit`) now hurts creatures and monsters avoid it. Left in C++: NPC and monster fire perception, code that spawns or finds `fd_fire` by id, and monster web immunity |
| TERR-J02 | fixed | PR E | `tests/audit_high_process_test.cpp`. New `process_start`/`process_finish` examine actors (item filters, yield and UPS energy as math, intermediate and output items, requirements, firestarter, outside check, typed messages, EOC hooks); the kilns, the running arc furnace, the stook and the autoclave (vanilla and Aftershock) are JSON, and firestarters light any `firestarter` process. Smoking racks and mills keep their C++ (food menu, charcoal per food volume, a timer item that finishes on its own, no rot while running, recipe batches); the old iexamine names still work on the vanilla ids |
| MONDT-J02 | fixed | PR E | `tests/audit_high_process_test.cpp`. `harvest_drop_type` takes `action_yield`, `corpse_damage_yield`, `gib_yield`, `survives_splatter`, `enables_action`, `corpse_flag_yield`, `proficiency_penalties` and `trains_proficiencies`; the five built-ins carry the old rules and no code compares drop type ids. A drop type without rules keeps the old default, so Magiclysm's `bionic` entries still drop from field dressing and again from full butchery (a data change is proposed, not made) |
| CAMP-J02 | fixed | PR E | `tests/audit_high_camp_test.cpp`. Camp missions are faction_mission JSON: a mod can add one gated on camp provides, with its duration, companion skill, exertion, training, item- or monster-group results, encounter and start/return EOCs (behavior "generic"), or reuse a bespoke behavior (logging, hide sites, patrols, farming, recruiting, surveys, menial); vanilla gathering, firewood, foraging, trapping and hunting became generic missions with the old numbers, and missions in progress save their id (old saves map to the vanilla mission). Left: upgrades, crafting and fortifications stay C++, the bespoke behaviors keep their own time formulas and conditional messages, and per-region results and JSON encounters are CAMP-J09 |
| SPAT-J01 | fixed | PR E | `tests/audit_high_sound_test.cpp`, `tests/benchmark_scenarios_sound_test.cpp`. Terrain, furniture, field intensity levels and vehicle parts take a `sound_attenuation`, summed along the line to whoever hears it (monsters, sound traps, NPCs, the avatar's messages, markers and sound effects, `can_hear`), from a per-level cache that is skipped when nothing loaded muffles; the vertical constants are the external options `SOUND_Z_LEVEL_MULT`, `SOUND_UNDERGROUND_MULT` and `SOUND_DEEP_UNDERGROUND_MULT`. Vanilla sets no values (a proposed set is in the PR), and horde signals outside the bubble still ignore terrain. |
| UIINV-P01 | fixed | PR E | `tests/audit_high_ui_render_test.cpp`; benchmark `crafting_panel_redraws_rebuilt`/`_cached`. The crafting menu's info panel (time, chances, lighting, book source, helpers, details table, component, tool and step rows, item info) is built once per selection and only read by the ~100 redraws a second; it is rebuilt when the recipe, batch size, crafter, inventory contents, game turn, the crafter's moves or position, or the language change. The recipe list still builds row names every frame (UIINV-P02) |
| REND-P02 | partly fixed | PR E | `tests/audit_high_ui_render_test.cpp` (the lookup memo; the multitile links only in a tiles build). Each tiles renderer remembers its sprite lookups (misses and looks_like answers too) per season, category, id, variant and intensity, so variant, intensity and transparency ids are no longer built and looks_like chains no longer walked every frame; it forgets them when the tileset or the game data changes. Multitiles link their subtile ids at load. Left: a hit still hashes the id string once (no int-id tables, since memory and subtiles have only strings), and the multitile draw still recurses once |

**Behaviour changes.** Electricity inside smoke or walls (`fd_electricity_unlit`) now hurts creatures, and monsters that avoid electricity avoid it. Smokes' portrait now shows in two of his dialogue branches that a later definition used to clear. The camp mission menu lists fortifications, crafting and hide/reveal after the missions, and descriptions show the skill and exercise each mission really uses. The autoclave refuses to start if any CBM inside is filthy, not only when all are. No vanilla sound, butchery, field, use-action or vehicle numbers changed.

## Cross-cutting themes

Many findings in different chapters share a root cause. Fixing the cause once is usually cheaper than fixing each finding on its own, and it stops the same mistake from coming back.

### The JSON contract is not enforced: bad data loads silently, and documented fields do nothing

Loaders accept misspelled keys (e.g. `allow_omitted_members()` on `pocket_data`), unknown enum or condition strings turn into always-false or no-op values, id references go unvalidated, and existing `check()` functions are missing, cannot fail, or are never registered in `DynamicDataLoader::check_consistency`. In the other direction, many documented or even loaded fields are never applied, and the docs have drifted from the loaders. Mods break without any warning, and shipped core and mod data is already affected in many chapters.

**Structural fix:** one program of work fixes most of it: strict member consumption with no `allow_omitted_members`, enum parsing that throws on unknown values, typed id fields checked at finalize, a test that every check function is registered, and a CI test that loads every JSON example in `doc/`.

**Size:** About 120-140 findings in 30 chapters. A title grep for unknown/typo/unvalidated/check()/silently/drift/undocumented/never applied/does nothing matched 138 titles (92 J, 46 B; 1 critical and 13 high), with some false positives. 61 titles in 27 chapters are about validation or `check()` gaps, and 18 titles in 16 chapters say "drift".

**Examples:** [EOC-B02](01-eoc-dialogue.md), [INV-J01](12-containers-inventory.md), [OMAP-B14](09-overmap-worldgen.md), [DATA-J03](04-data-loading-saves.md), [CRAFT-J04](25-crafting-recipes.md), [MAGIC-J03](27-magic-enchantments.md), [UIHUD-J01](30-hud-menus-input.md), [MAGIC-B04](27-magic-enchantments.md), [EOC-J17](01-eoc-dialogue.md), [MONDT-J15](20-monster-data-creatures.md)

### copy-from / extend / delete behave differently per type; many lists are append-only

Override rules depend on whether a type uses `generic_factory` and on how each member is read. Some types (`npc_template`, `faction`, `effect_type`, `vitamin`, `skill`, `activity_type`) cannot be overridden, or the first definition wins. Arrays of objects (`pocket_data`, talk responses, `intensity_levels`, vehicle groups, palettes) can only be fully replaced or only appended to. `copy-from` silently drops or duplicates inherited fields (construction components, mutation enchantments, trap eocs, recipe `extend` blocks). Mods must restate whole objects, and vanilla data already loses inherited values.

**Structural fix:** move every type onto `generic_factory`, give arrays of objects id-keyed `extend`/`delete` readers, generate the JSON_INHERITANCE support table from one registry, and add a round-trip test that a field-less `copy-from` reproduces its parent exactly.

**Size:** About 40 findings in 22-23 chapters. A title grep for copy-from/extend/delete/append-only/generic_factory/redefinition/first-wins matched 41 titles (7 high), a few of them false positives. `copy-from` or `extend` is discussed in 107 finding bodies across 29 chapters.

**Examples:** [DATA-J02](04-data-loading-saves.md), [DATA-B03](04-data-loading-saves.md), [INV-J02](12-containers-inventory.md), [HLTH-J02](15-health-needs-effects.md), [CRAFT-J01](25-crafting-recipes.md), [MUT-J08](17-mutations-bionics.md), [TERR-J06](28-examine-traps-construction.md), [VINT-B08](24-vehicles-interaction.md), [MAGIC-B16](27-magic-enchantments.md), [NPC-J02](21-npc-ai-factions.md)

### Behaviour keyed to hardcoded content ids, closed C++ enums and compile-time constants

This is the largest J pattern. Engine logic compares against specific vanilla item, trait, effect, terrain, furniture, field, bionic and OMT ids, follows id naming conventions (`_on`, `f_`, substrings), or runs from closed C++ enums: camp missions, zone types, OMT flags, computer options, harvest drop types, the 25 vpart toggle flags, and 161 parameterless iuse functions. Often a suitable JSON flag or field already exists next to it. Modded content cannot take part, and new content silently misbehaves.

**Structural fix:** replace each id comparison with a JSON flag or field on the type, turn closed enums into JSON registries with EOC hooks, and track progress with a lint that counts `static const *_id foo("...")` literals per source file.

**Size:** About 220 of the 473 J findings, in all 30 chapters that have J findings. A title grep for hardcod/closed enum/tied to/keyed on/by id/item, trait or terrain ids matched 224 titles (10 high). About 80 of those titles name specific content ids; about 41 are hardcoded tuning numbers.

**Examples:** [MAP-J02](06-map-core-fields.md), [TERR-J02](28-examine-traps-construction.md), [MONDT-J02](20-monster-data-creatures.md), [CAMP-J02](22-missions-trade-camps.md), [MUT-J12](17-mutations-bionics.md), [HLTH-J13](15-health-needs-effects.md), [USE-J11](13-item-use.md), [VINT-J02](24-vehicles-interaction.md), [ACT-J12](26-activities-zones.md), [OMAP-J12](09-overmap-worldgen.md)

### Avatar-centric engine paths: NPCs and monsters are skipped, or read the avatar's state

Much per-turn and combat logic lives in avatar-only or player-relative code, or calls `get_avatar()` / `get_player_character()` where the acting creature was meant. As a result, about 229 vanilla effect stat mods never reach NPCs. Monsters lose effect `speed_mod`, `fling_creature` moves non-avatars only 1 tile, NPC trait flags and charged qualities read the avatar's state, and weather and field effects depend on the avatar's z-level. Content that works in playtesting fails silently for NPCs and monsters, and EOC hooks such as spell level-up and emit vars always use the avatar.

**Structural fix:** move `avatar::reset_stats`-style logic into `Character` or `Creature`, pass the acting creature explicitly, add a lint against `get_avatar()` inside `Character`/`Creature`/`item`/`map` methods, and add a test harness that runs the same scenario for avatar, NPC and monster.

**Size:** About 50-60 findings in about 25 chapters. A title grep for avatar's/avatar-only/player-relative/NPCs never/monsters ignore/Character-only matched 50 titles in 23 chapters (6 high), a few of them false positives. `get_avatar()`/`get_player_character()` is named in 70 finding bodies across 28 chapters.

**Examples:** [CHAR-B04](14-character-body.md), [HLTH-J01](15-health-needs-effects.md), [LOOP-B01](05-game-loop-weather.md), [MONAI-B01](19-monster-ai-attacks.md), [MUT-B06](17-mutations-bionics.md), [IINF-B06](11-item-info-armor-guns.md), [INV-B12](12-containers-inventory.md), [MAGIC-J05](27-magic-enchantments.md), [CMBT-J03](18-combat.md), [TERR-J14](28-examine-traps-construction.md)

### EOCs cannot reach engine state: thin non-character talkers, no entity handles, read/write asymmetry

EOC variables cannot hold a monster, item or vehicle, and spawn effects never return what they spawned. Monster, item, vehicle, zone and furniture talkers implement little, and base-talker setters are silent no-ops (`u_attack` on a monster does nothing). Large areas of state are readable but not writable, or the reverse: weather and temperature, overmap, camp, bionic, inventory capacity, gun and ammo, combat and NPC AI state. Modders cannot script follow-ups to their own spawns, and they cannot tell when an effect did nothing.

**Structural fix:** add a persistent uid-based entity-handle variable type returned by spawn effects, have base-talker setters `debugmsg` instead of no-op, and generate get/set math functions for every talker type from one declarative property table so access stays symmetric and documented.

**Size:** About 70 J findings in 27 chapters. A title grep for talker/handle/read-only/write-only/cannot read or write/no-op/not exposed/EOC access matched 70 titles (10 high). About 20 of them, in 10 chapters, are specifically about non-character talkers or handles.

**Examples:** [EOC-J01](01-eoc-dialogue.md), [EOC-J08](01-eoc-dialogue.md), [INV-J07](12-containers-inventory.md), [MATH-J05](02-math-talkers.md), [VINT-J01](24-vehicles-interaction.md), [MONAI-J01](19-monster-ai-attacks.md), [OMAP-J01](09-overmap-worldgen.md), [LOOP-J01](05-game-loop-weather.md), [MUT-J03](17-mutations-bionics.md), [ITEM-J04](10-items-core.md)

### Missing lifecycle events and hooks, and hooks that fire without context

Major gameplay moments have no `event_bus` event or JSON hook: tile destruction, OMT generation, construction, crafting start/fail, vehicle install/repair/engine start, pickup/drop, camp missions, weather changes, hearing, dodge/block, and effect gain/loss for monsters. `event_type` is a closed enum, so mods cannot define their own events. Where hooks do exist, they often get no beta or context: martial-art EOCs have no opponent, EOC spells get only `spell_location`, `result_eocs` get no item, activity completion carries no data, and queued EOCs keep only a Character alpha. Content therefore polls with recurring EOCs or stays in C++.

**Structural fix:** add open, mod-definable event types and standard per-type hook fields (`on_start`/`on_complete`/`on_fail`/`on_destroy`), all dispatched through one helper that always fills alpha, beta (target, item or vehicle) and a standard context-variable set.

**Size:** About 55 J findings in 26 chapters. A title grep for no event/hook/trigger, lifecycle, no context, no item/spell/vehicle talker, payloads lack, drops the caller matched 54 titles (8 high).

**Examples:** [EOC-J06](01-eoc-dialogue.md), [EVT-J06](03-events-stats.md), [MAP-J01](06-map-core-fields.md), [MGEN-J01](08-mapgen.md), [CAMP-J01](22-missions-trade-camps.md), [CRAFT-J02](25-crafting-recipes.md), [TERR-J01](28-examine-traps-construction.md), [VINT-J06](24-vehicles-interaction.md), [CMBT-J01](18-combat.md), [MAGIC-J01](27-magic-enchantments.md)

### Crash patterns: unchecked null talkers, pointers, optionals and end(); containers mutated while iterated; references held across callbacks

Talker getters return `nullptr` by default and many EOC lambdas dereference them directly. Code also dereferences empty optionals and `end()` iterators, erases from containers inside range-for loops, and keeps references (`trait_data&`, `butchery_data*`, overlay vehicles) across EOC calls or pops that destroy them. This family accounts for 7 of the 8 critical findings and many highs, and vanilla or shipped mod content reaches several of them.

**Structural fix:** add checked talker accessors (a `require_character()`-style helper that debugmsgs), re-look-up objects by id after running EOCs instead of holding references, use collect-then-mutate for containers, and add CI runs under ASan/UBSan plus a fuzz test that executes every EOC effect and condition with each talker type and a null beta.

**Size:** About 70 B findings in 29 chapters. A title grep for null/nullptr/use-after-free/dangling/end()/out of bounds/empty optional/erase while iterating/crash/destroyed matched 72 titles: 7 critical (all criticals except the MAGIC-B01 softlock) and 17 high. 100 B finding bodies in 29 chapters mention a dereference, use-after-free, out-of-bounds access or UB.

**Examples:** [EOC-B01](01-eoc-dialogue.md), [EOC-B13](01-eoc-dialogue.md), [CHAR-B01](14-character-body.md), [USE-B01](13-item-use.md), [CMBT-B01](18-combat.md), [ACT-B01](26-activities-zones.md), [MUT-B01](17-mutations-bionics.md), [MAP-B02](06-map-core-fields.md), [REND-B02](31-rendering-tiles.md), [UIINV-B01](29-inventory-crafting-ui.md)

### Numeric and representation hygiene: integer truncation, unit and convention mismatches, unguarded division

Per-tick rates and multipliers are truncated by int casts or integer division. Examples: 0.5 mm/h drizzle truncates to 0 and `one_in(0)` is true; `EVOLUTION_INVERSE_MULTIPLIER` becomes 0; DISTRIBUTED_DAMAGE gives near-immunity; zone repair is instant; sub-1.0 ammo use gives free shots. Values cross unit or convention boundaries wrongly (math angles vs compass bearings, degrees passed as radians, legacy body-temp units, `mph*100` in to-hit), and divisions by a possibly-zero value crash widgets and formulas. The few cases of locale or translated text used as data (float sliders, gunmod slot names compared by translated name, enchantment ids built from translated names) belong here too; with only about 4 findings they are too few for a theme of their own.

**Structural fix:** use `units::` types at JSON and API boundaries, a shared probabilistic-rounding helper for per-tick quantities, a safe-ratio helper, clang-tidy integer-division and narrowing checks, and a rule to compare ids via `str()` and parse with the classic locale.

**Size:** About 47-50 B findings in 23-27 chapters. A title grep for truncation/integer division/divide by zero/rounding/units/radians/NaN/off-by-one/locale matched 47 B titles in 23 chapters (10 high). Truncation or integer division is named in 28 finding bodies (21 chapters) and divide-by-zero in 20 (14 chapters). Translated or locale strings used in logic: only IINF-B02 (the same bug as UIINV-B04), VINT-B07 and DATA-B02.

**Examples:** [LOOP-B02](05-game-loop-weather.md), [HLTH-B01](15-health-needs-effects.md), [MONDT-B02](20-monster-data-creatures.md), [ACT-B02](26-activities-zones.md), [IINF-B08](11-item-info-armor-guns.md), [MAP-B01](06-map-core-fields.md), [LOOP-B09](05-game-loop-weather.md), [SPAT-B08](07-pathfinding-vision-sound.md), [UIHUD-B02](30-hud-menus-input.md), [IINF-B02](11-item-info-armor-guns.md)

### Work redone every turn or frame instead of cached or invalidated narrowly

Derived state is rebuilt on every turn, swing or effect: the enchantment cache (in 4 chapters), `trait_flag_cache`, `calc_encumbrance`, vehicle refresh and power grids, and weather forecasts. Caches are dirtied far more widely than the change requires (whole-level relight for changes that cannot affect light, all vehicles re-cached after one smoke tile). Many queries scan the whole bubble (2,541 submaps for vehicles) or all 21 z-levels. Idle UI loops redraw at 30-100 Hz and rerun game logic every frame (crafting menu, surroundings menu, faction menu, character creator, spellbook preview).

**Structural fix:** event-driven dirty flags with narrow scope, per-turn memoization of derived values, maintained indexes (per-level vehicle lists, creature position index), and a UI loop that redraws only on input or state change and memoizes per-selection data.

**Size:** About 180 of the 380 P titles (all 31 chapters, 17 high) match per-turn, per-frame or per-call recompute/rebuild/dirty/whole-level-scan wording; 46 say "every/per turn". 38 P findings in 18 chapters are redraw or frame loops (7 high). The enchantment-cache rebuild recurs as CHAR-P02, MAGIC-P03, CMBT-P15 and MONAI-P08.

**Examples:** [CHAR-P02](14-character-body.md), [CMBT-P15](18-combat.md), [MUT-P01](17-mutations-bionics.md), [SPAT-P02](07-pathfinding-vision-sound.md), [MAP-P04](06-map-core-fields.md), [VEH-P03](23-vehicles-core.md), [LOOP-P02](05-game-loop-weather.md), [MONAI-P01](19-monster-ai-attacks.md), [UIHUD-P01](30-hud-menus-input.md), [UIINV-P01](29-inventory-crafting-ui.md)

### Allocation churn in hot paths: string-keyed lookups, by-value copies, throwaway objects (including a fresh dialogue per condition)

Hot paths hash and concatenate strings: tile ids per sprite, vpart flags as `std::set<std::string>`, effect modifiers in a two-level string map, `get_option("NAME")`, and string ids re-interned on every EOC evaluation. Getters return heavy containers by value (zone point sets, special locations, relic enchantments). Code builds full temporary items, monsters, avatars, maps or `ter_t` copies just to answer a simple question. Every condition evaluation (in EOCs, widgets, weather classification, field emission, event dispatch, special vision) allocates a new dialogue with two heap talkers and a context-map copy.

**Structural fix:** resolve strings to `int_id` or flag bitsets at load, return const references, answer queries from type data instead of constructed objects, and make dialogue/talker evaluation allocation-free (stack talkers, borrowed context, arguments pre-interned at load).

**Size:** About 125 of the 380 P titles (30 chapters, 8 high) name copies, allocations or temporary objects. 27 P titles in 18 chapters, and about 50 P bodies in 22 chapters, name string-keyed lookups or interning. About 15 findings in 10+ chapters are per-evaluation dialogue/talker construction (EOC-P01, MATH-P02, MATH-P09, EVT-P06, EVT-P08, LOOP-P05, MONAI-P16, CAMP-P07, MAGIC-P07, TERR-P05, UIHUD-P05, REND-P15).

**Examples:** [REND-P02](31-rendering-tiles.md), [OMAP-P01](09-overmap-worldgen.md), [ACT-P01](26-activities-zones.md), [VINT-P04](24-vehicles-interaction.md), [HLTH-P06](15-health-needs-effects.md), [IINF-P04](11-item-info-armor-guns.md), [VEH-P06](23-vehicles-core.md), [EOC-P01](01-eoc-dialogue.md), [UIHUD-P05](30-hud-menus-input.md), [MATH-P03](02-math-talkers.md)

## Suggested order of work

1. **Small-effort crash, data-loss and gameplay fixes.** Start with groups 1-3 of "Fix first", then work through the remaining small-effort criticals and highs. Of the 157 critical and high findings, 98 are rated small effort, and most are a few lines each. Add a regression test or a written repro for each fix, and add an ASan/UBSan CI build early so the memory-safety fixes can be checked. Fix EVT-B02 in this phase, because any later change to event types depends on it.
2. **Performance quick wins, measured.** Before changing anything, build a benchmark save that exercises the hot paths: a dense base, a horde fight, driving across submaps, the crafting menu left open, and a timed world load. Profile it, then apply group 4 of "Fix first" and the other small-effort high P findings, and record the before/after numbers for each change. Larger caching and allocation work from the last two themes comes after these.
3. **Loader and validation infrastructure.** Enforce the JSON contract: strict member consumption, enum and condition parsing that throws, typed ids checked at finalize, a test that every `check()` is registered, and a CI job that loads every JSON example in `doc/`. Then move the remaining types onto `generic_factory` with id-keyed `extend`/`delete`. Expect this to expose broken shipped data (INV-J01 alone surfaces 30+ pocket definitions), so plan to fix the data in the same change. This one piece of work fixes whole classes of silent JSON failures, and later JSON features build on it.
4. **EOC surface expansion.** Add entity handles returned by spawn effects, open event types with standard `on_*` hooks that always carry alpha, beta and context, and get/set access for non-character talkers generated from one property table. Do the avatar-centric cleanup in the same phase (pass the acting creature explicitly), because these hooks need to work the same way for NPCs and monsters.
5. **Migrate hardcoded C++ behaviour to JSON.** Replace id comparisons with JSON flags or fields, and turn closed enums (camp missions, zone types, vpart toggles, iuse functions, harvest drop types) into JSON registries with EOC hooks. Track progress with the hardcoded-id lint described in the themes. This is the largest and most open-ended phase, so do it system by system, starting with the systems the fork's mods depend on most.

## Method and limitations

- **Scope.** 31 systems covering nearly all non-debug engine code in `src/`. Out of scope: debug tools (debug menu/console, editmap, wish), `tests/`, third-party code, build tooling, and auditing the JSON content itself (`data/` was consulted only as evidence).
- **Process.** For each system, three lens-specialized auditor agents (JSON/EOC, bugs, performance) read the code; the exception is chapter 31, which had no JSON/EOC pass, so its J count of 0 means "not audited". An extra pass covered any large file that none of them had examined. Adversarial reviewer agents then re-read the code for every finding and confirmed, downgraded to plausible, or rejected it, and a writer agent assembled the chapter.
- **Static reading only.** Nothing was compiled or run. Treat each finding as a well-evidenced lead, and confirm it with a test or a repro before fixing it.
- **Hand spot-check.** After the run, EOC-B03, MUT-B01, IINF-B02, VINT-B01 and DATA-B01 were re-checked against the source by hand, and all of them held up.
- **Line numbers** refer to commit `a1a646d6`. Upstream CDDA moves fast, so some findings may already be fixed upstream; check before porting a fix.
- **Authorship.** The audit was produced by AI agents (Claude Code).

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
| IINF-B05 | fixed | `459fe6a2`, PR D | `tests/audit_items_sweep_test.cpp`, `tests/audit_partial_data_test.cpp`. `is_reloadable()` reads the pocket itself (`459fe6a2`). The suggested ammo-type filter on the summed `ammo_remaining()` is not done, on purpose: the multimag design counts every well (`multimag_integral_pocket_skips_casings` pins it), and `ammo_remaining_in_pocket()` gives a per-type count. The PR D test pins both |
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
| HLTH-J03 | fixed | `79ed5939`, PR C, 2.0 | `tests/audit_diet_rules_test.cpp`, `tests/audit_gameplay_fixes_test.cpp`, `tests/audit_m_fu_left_test.cpp`. No diet trait is C++ any more: VEGAN, PICKYEATER, PROBOSCIS, M_DEPENDENT, MARLOSS_AVOID and THRESH_MYCUS are `eat_condition`s (an object or an array, with a `checked` stage that keeps the old message order), GOURMAND is a `food_fun`, PROJUNK and PROJUNK2 are `eat_eocs`, and RUMINANT, SAPROPHAGE, SAPROVORE and EATDEAD get their diets from the character flags `HERBIVORE_DIET`, `SAPROPHAGE_DIET`, `SAPROVORE_DIET` and `EATDEAD_DIET`. New condition `npc_comestible_type` and math `n_food_fun()`; the allergy traits and animal thresholds already were data |
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
| LOOP-J02 | fixed | `104c5f5b`, `9c34f451`, 2.0 | `audit_quick_fixes_world_test.cpp`, `tests/audit_m_fu_left_test.cpp`. Hooks run on teleports and vehicle travel, with context, and stop after 8 crossings in a row. A new game runs the start OMT's `entry_eoc` once, with the usual context and an empty `left_oter_id`, before the scenario's EOCs |
| OMAP-J02 | fixed | `104c5f5b` | `audit_quick_fixes_world_test.cpp` |
| LOOP-J03 | fixed | `104c5f5b` | `audit_quick_fixes_world_test.cpp` |
| VINT-J03 | fixed | `104c5f5b` | `audit_quick_fixes_world_test.cpp` |
| USE-J03 | fixed | `8b086ee3` | `audit_quick_fixes_items_test.cpp` |
| USE-J04 | fixed | `8b086ee3` | `audit_quick_fixes_items_test.cpp` (`place_npc` radius); the doc examples were checked by loading them |
| MONDT-J03 | fixed | `8b086ee3`, `9c34f451` | `audit_quick_fixes_items_test.cpp` |
| MONDT-J05 | fixed | `8b086ee3`, 2.0 | `audit_quick_fixes_items_test.cpp`, `tests/audit_m_fu_left_test.cpp`. The death EOC gets `loc`, `overkill` and `corpse_loc`, and runs once the corpse exists, before the drops (`npc_die` with `remove_corpse` still stops both). `monster_takes_damage` has `damage_type` (the type that dealt the most; `bash`/`cut` for falls, `heat` for burning, empty for untyped damage) and `bodypart` |

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
| REND-P02 | fixed | PR E, 2.0 | `tests/audit_high_ui_render_test.cpp`, `tests/audit_m_fu_render_test.cpp` (the draw comparison only in a tiles build). Behind a new graphics option, "Experimental: cache sprite lookups" (off by default, applies at once, empties the memo on change); off, the draw is the pre-memo code. On, each tiles renderer remembers its sprite lookups (misses and looks_like answers too) per season, category, id, variant and intensity, terrain, furniture, traps and fields by int id with no string hash, and a linked multitile subtile is drawn in place from per-season links made at load. Left: memory, overmap, item, monster and vehicle-part draws still hash the id once (they hold only strings), and an unlinked or fallback multitile still recurses |

**Behaviour changes.** Electricity inside smoke or walls (`fd_electricity_unlit`) now hurts creatures, and monsters that avoid electricity avoid it. Smokes' portrait now shows in two of his dialogue branches that a later definition used to clear. The camp mission menu lists fortifications, crafting and hide/reveal after the missions, and descriptions show the skill and exercise each mission really uses. The autoclave refuses to start if any CBM inside is filthy, not only when all are. No vanilla sound, butchery, field, use-action or vehicle numbers changed.

### The 2.0 round: medium findings

The 2.0 release round works through the medium-severity findings and finishes what earlier rounds left for the owner to decide. Each batch was built in its own worktree on top of `claude/2.0-release`, then merged; a finding an earlier round had already fixed is re-checked and listed as "not changed" unless the re-check found a gap. Ids with a suffix (SPAT-J01-values, NPC-J01-content and so on) are the owner's content decisions on findings whose code was done earlier, so mostly the data changes. A follow-up to a finding listed in an earlier table replaces its row there instead (REND-P02, HLTH-J03, LOOP-J02, MONDT-J05). The regression tests are in `tests/audit_m_*_test.cpp`. For the whole round the table has 407 rows: 245 fixed, 109 partly fixed and 53 not changed. The medium performance findings (the `-P` ids) were deferred by the owner to a later release and are not in this table.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| ACT-B04 | fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. A follower's fetch for a zone job (loot zones and camp storage) and NPC zone sorting leave the faction's NO_NPC_PICKUP zones alone, on the ground as on vehicles. Items already next to the work spot are still used in place |
| ACT-B05 | fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. Discarding zone changes removes every vehicle zone added since; the personal zone count is recounted on load and reset with the zones |
| ACT-B06 | fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. The zone manager reloads its zone types with the data, so a later world's mod zone types are offered and their zones kept |
| ACT-B07 | fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. The fetch for construction and disassembly plans and takes the vehicle cargo it counts; fetching from the ground under a cargo part no longer copies the item |
| ACT-B08 | partly fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. Study and disassembly zone scans no longer mark items; the job marks what it takes and the mark is cleared when it ends. Marks already in older saves stay until the item is picked up |
| ACT-J05 | fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. Mapgen zones take `"options"`: farm seed and fertilizer, blueprint construction, unload settings and `ignore_contents`, checked when the data loads |
| ACT-J06 | fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. A hacked card reader makes its card's changes and becomes its `hacked_ter`/`hacked_furn`; a hacked electronic safe becomes its `hack_result`. Some readers with another radius or door now open as their card does |
| ACT-J07 | fixed | 2.0 | `tests/audit_m_m26a_test.cpp`. Terrain and furniture take `lockpick_difficulty` and `lockpick_jam_result`; the alarm follows the `ALARMED` flag; the gun safe's jam is in its JSON |
| ACT-J08 | fixed | 2.0 | `tests/audit_m_m26b_test.cpp`. Done as USE-J06 (m13a): washing takes the JSON requirements `wash_water` and `wash_cleanser` per unit (mods `extend` them), and the menu and the activity share one check and one consume. This batch's duplicate `washing_*` requirements were dropped at the merge; its test checks a mod's own cleanser requirement. |
| ACT-J09 | fixed | 2.0 | `tests/audit_m_m26b_test.cpp`. New `u_in_zone`/`npc_in_zone`/`map_in_zone`, `create_zone`, `remove_zones`, `set_zones_enabled`, `u_zone_enabled` and `u_zone_faction`; `u_run_fixed_zone_eocs` and `u_location_variable` take a `faction`, and `"zone": ""` finds any zone. `create_zone` sets no plot/blueprint options. |
| ACT-J10 | partly fixed | 2.0 | `tests/audit_m_m26b_test.cpp`. Terrain `chop`, `till` and `plant_result` replace the trunk, stump, dirt-mound and greenhouse ids in the chop, till and plant code and in the zone jobs. Left: plank cutting (item `log`, 2x4 output) and camp chopping. |
| ACT-J11 | fixed | 2.0 | `tests/audit_m_m26b_test.cpp`. A furniture `smoker` object (lit/unlit furniture and capacity) replaces the smoking-rack ids in firestarting, the smoker menu and smoking; kilns were already JSON processes (TERR-J02). |
| ACT-J12 | partly fixed | 2.0 | `tests/audit_m_m26b_test.cpp`. Zone type `loot_source` (default: the id starts with LOOT) replaces the LOOT prefix checks; `LOOT_ZONE` is documented. Left: zone `options` from JSON, activity zone fields and JSON zone jobs. |
| CAMP-B05 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. One finished worker makes Recover possible whatever the list order, and camp returns also wait for each companion's own planned return time, so a short trip can no longer recall a long one early. |
| CAMP-B06 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. Asking the larder for nothing serves nothing without a debugmsg (0-hour missions set off without a meal), and an empty larder no longer divides by zero. A larder that never held food serves nothing without a debugmsg, as one eaten empty does. |
| CAMP-B07 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. Scavenging and hospital raids bring back every item they take (equal-looking items and differently filled backpacks are no longer merged), and the "kill zombies" step only kills hostile monsters. |
| CAMP-B08 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. The faction menu opens a camp board only for a follower on a camp mission, for the camp it is from, with this contact's radio flag; an outpost job gets a message instead of a phantom camp. |
| CAMP-B09 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. Trading some charges of a held stack takes them from the stack that was picked and priced, not the first stack of that type. |
| CAMP-B10 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. `recruit_class` is loaded, copied into missions and checked, so MGOAL_RECRUIT_NPC_CLASS wants that class; the "I brought 'em." turn-in matches either recruit goal. |
| CAMP-B11 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. `exclude_seen` and the replacement-terrain search were already fixed by OMAP-J06 (`63cee5c4`); update_mapgen reads `om_special` and searches from the target's z-level. |
| CAMP-B12 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. MGOAL_FIND_ITEM_GROUP counts each item once (a stack by its count) and turning in takes exactly `count`, one type at a time. Left: it consumes by type, so with `required_container` it may take a match outside one. A mission without `required_container` asked for an empty container id, which no item is in, so it could never be completed; unset is now none. |
| CAMP-B13 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. What an expansion is building comes from the companions out building it; that blueprint (any variant) and whatever it rules out can't be started meanwhile. The dead `in_progress` save list is gone (old saves load). |
| CAMP-J02-survey | fixed | 2.0 | `tests/audit_m_fu_content_test.cpp`. The field and expansion survey missions pick their companion by survival instead of marksmanship. |
| CAMP-J03 | fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. Camp crafts run their `result_eocs` per batch unit on return and train their `skill_used`; upgrades train the blueprint's skills and run `result_eocs` once; EOC-only recipes can be crafted at a camp. Left: `result_item_eocs` don't run at camps. |
| CAMP-J04 | partly fixed | 2.0 | `tests/audit_m_m22a_test.cpp`. Missions check every id field but `destination` and the fields each goal needs; `difficulty` is optional and an urgent difficulty-0 mission is pressed; two destination-less GO_TO_TYPE missions became MGOAL_GO_TO. Left: six shipped missions name invalid destination ids, so `destination` isn't checked yet; `good_fac_id`/`bad_fac_id` stay. |
| CAMP-J05 | fixed | 2.0 | `tests/audit_m_m22b_test.cpp`. Camp planting offers only seeds the plots take that are warm enough now (no buckwheat-only check), plants each only where its `required_terrain_flag` holds, and skips seeds with the new `NO_CAMP_FARMING` flag (Marloss, fungal) instead of hardcoded ids; harvests go to camp storage. |
| CAMP-J06 | fixed | 2.0 | `tests/audit_m_m22b_test.cpp`. A recruiting faction_mission's `"recruit"` sets weighted npc classes/templates, the find/convince skills and difficulties, the join difficulty, conditional score bonuses and `recruit_eocs`; vanilla recruiting picks its companion by speech and its description shows the mission's time and positions. Appeal haggling and the hidden vanilla bonuses stay C++. |
| CAMP-J07 | partly fixed | 2.0 | Doc only: BASECAMP.md's provides table now matches the code (no bed, tool_storage, pantry, dismantling or Fertilize; recipes come from provides named after recipe groups). Left: the hardcoded camp tunables (crafter cap, work day, larder rules, starvation penalties) wait on a decision about where camp settings live. |
| CAMP-J08 | partly fixed | 2.0 | `tests/audit_m_m22b_test.cpp`. `faction_food_supply()` can be assigned (calories or a vitamin, optional `expires_in`), and the math functions `camp_provides`, `camp_expansion_count`, `camp_assigned_npcs` and `camp_workers` read a camp. Left: effects that change a camp (provides, upgrades, expansions, sending companions). |
| CAMP-J09 | fixed | 2.0 | `tests/audit_m_m22b_test.cpp`. Generic camp missions take `regional_results` keyed by region, and an encounter can be an EOC (companion alpha) that kills the companion by setting `encounter_survived` to 0. |
| CAMP-J10 | partly fixed | 2.0 | `tests/audit_m_m22b_test.cpp`. Logging, clear-cutting and hide sites take `target_terrains`/`cleared_terrains`, and survey_field `field_terrains`/`field_overmap_terrain`, from the mission (vanilla values in JSON, extendable). Left: trunk and stump yields, trip estimates and the hide-site stamps. |
| CAMP-J11 | partly fixed | 2.0 | `tests/audit_m_m22b_test.cpp`. Outpost missions pay and charge in the giver faction's currency (merch if none, as Tacoma Ranch), and an unknown `companion_mission` role is reported. Left: a JSON job board for the roles (large). |
| CAMP-J12 | fixed | 2.0 | `tests/audit_m_m22b_test.cpp`. Shop and price-rule conditions see the item in context variables (`trade_item`, `trade_item_owner`, ...), `markup`/`premium`/`fixed_adj` take math, an `avatar_trades` event follows each trade, and shop lists and price rules check their ids (with data fixes in robofac, BEM, Defense_Mode and Magiclysm; the Magiclysm Ancient Ones' currency named an item that doesn't exist and was removed). |
| CHAR-B17 | fixed | 2.0 | Checked by reading. `anatomy::check` tested the anatomy's id instead of the part's and printed `main_part` for `connected_to`; `finalize` printed the part id twice. |
| CHAR-J02 | fixed | 2.0 | `tests/audit_m_m14a_test.cpp`. Wool allergy, vegan and squeamish refusals are mutation JSON (`unwearable_materials`, `unwearable_flags`, `unwearable_message`; an item type's flags count); the hardcoded horns/antennae/antlers helmet check is gone, their `restricts_gear` covers the whole head so `allowed_items` can let a horned helmet through; material `wet_warmth_retention` replaces the `wool` id test. |
| CHAR-J03 | partly fixed | 2.0 | `tests/audit_m_m14a_test.cpp`. NIGHT_VIS enchantments now apply with night vision goggles and the NIGHT_VISION flag (they stack, e.g. bionic night vision plus goggles); the Night Vision traits' ranges are the mutation field `night_vision_range` (the best counts, as before) and Avian Eyes' +1 a NIGHT_VIS enchantment. Sight caps and the shell state are still hardcoded. |
| CHAR-J04 | fixed | 2.0 | `tests/audit_m_m14a_test.cpp`. `scaling-stat` takes any skill id, a mod's too; the old short names still work (`drive` now means `driving`), an unknown one is a load error and adds nothing; descriptions name the skill. |
| CHAR-J05 | fixed | 2.0 | `tests/audit_m_m14a_test.cpp`. Anatomy `parts` support `extend`/`delete` (still required on first load, duplicates reported); body parts check `effects_on_hit`, `qualities`, `sub_parts`, `similar_bodypart`, and sub body parts get a check (four vanilla id typos fixed); CHAR-B17's slips in `anatomy::check`/`finalize` fixed too. Techniques aren't checked: vanilla `wing_l` names `tech_wing_slap`, which only mods define; the slime hands' undefined `TECH_SLIME_COMPLETE_PUNCH` is now `TECH_SLIME_PUNCH`. |
| CHAR-J06 | partly fixed | 2.0 | `tests/audit_m_m14a_test.cpp`. Effects shift body temperature (`body_temperature`) and warm like clothing (`warmth_per_intensity`): the flu, common cold and heating bionic are JSON; body part flag `FROSTBITE_VULNERABLE` and mutation `drying_time_mult` replace hardcoded ids. The `u_body_temp` math is CHAR-J10's; PYROMANIA stays hardcoded. |
| CHAR-J07 | fixed | 2.0 | `tests/audit_m_m14b_test.cpp`. Movement modes take a JSON `condition` and `switch_cost`, the `MOVE_MODE_SWITCH_COST` enchantment value scales switch costs, `u_set_move_mode` switches as choosing would, and `character_changes_move_mode` is sent; MOVE_MODE.md fixed and `u_has_move_mode` documented. DEFT/CLUMSY keep their C++ rule |
| CHAR-J08 | partly fixed | 2.0 | `tests/audit_m_m14b_test.cpp`. SPINES, QUILLS, THORNS and CF_HAIR are a mutation `retaliation` field with the same numbers; `hit_me_effect` takes `hit_types` (a character's goes off on ranged hits when they name it) and its EOCs get `hit_type` and `hit_bodypart`. Left: `character_takes_damage` has no damage type |
| CHAR-J09 | partly fixed | 2.0 | `tests/audit_m_m14b_test.cpp`. The respiration rate is kept current, the three effect mods are saved, and math reads `heart_rate()`, `blood_pressure()` and `respiration_rate()`; EFFECTS_JSON.md says what the fields do. Whether blood pressure and breathing should affect gameplay is the owner's call |
| CHAR-J10 | fixed | 2.0 | `tests/audit_m_m14b_test.cpp`. Math `wetness('bp')` and `body_temp('bp')` (read and write) and `frostbite('bp')`; `u_add_wet` with `bodypart` wets those parts at once; events `character_falls`, `character_climbs` and `character_moves` (NPC steps too) |
| CHAR-J11 | partly fixed | 2.0 | `tests/audit_m_m14b_test.cpp`. `max_height: 0` disables `down` as documented, aids can be up-only, an `up` block (move cost, needs_support, cost, message) picks the aid for climbing up, and `veh` aids check their flag. Left: dialogue-condition aids, the grapnel/rope/vine stair path and the WALL_CLING descent cost |
| CMBT-B17 | not changed | 2.0 | `tests/audit_combat_test.cpp`. Already fixed by an earlier round (impale targets, no adjacency check, early return without targets). |
| CMBT-J02 | fixed | 2.0 | `tests/audit_m_m18b_test.cpp`. An unknown technique `aoe` is a load error, and the docs name `spin`, `impale` and `wide`; `aoe` is still a string with no radius or target-count options. |
| CMBT-J03 | fixed | 2.0 | `tests/audit_m_m18b_test.cpp`. BRAWLER and GUNSHY grant the new `CANNOT_USE_RANGED` and `CANNOT_USE_FIREARMS` flags, checked with the hand flags by `Character::ranged_ban_reason` on every fire path and for NPCs (weapon rating, gun attacks); a GUNSHY turret ban now stops firearm turrets only. |
| CMBT-J04 | fixed | 2.0 | `tests/audit_m_m18b_test.cpp`. New events `character_dodges`, `character_blocks`, `character_melee_hits` (after the hit resolves), `character_fires_gun` (shots, hits) and `monster_melee_attacks_character`/`_monster`; statistics don't keep their damage fields. Monster special attacks still send none. |
| CMBT-J05 | partly fixed | 2.0 | `tests/audit_m_m18b_test.cpp`. One set of explosion defaults, `distance_factor` and `fragment_mass` validated at load, the blast hits every main body part and reaches monsters through `deal_damage`; the 60 mm shells keep their 0.005 g fragments explicitly. No blast damage type, field or EOC fields yet. |
| CMBT-J06 | fixed | 2.0 | `tests/audit_m_m18b_test.cpp`. Buffs take a `condition`; martial-art and technique EOC lists and `autolearn` follow copy-from/extend/delete; tech effect durations take duration strings; inline buffs keep their mod; unknown EOCs, attack vectors and autolearn skills are reported at load. |
| CMBT-J07 | fixed | 2.0 | `tests/audit_m_m18b_test.cpp`. Items take a `block` object (`bonus`, `elemental_mult`, per-type `effectiveness`; WBLOCK_n stay the defaults), the blocking thresholds are external options, and the enchantment values `BLOCK_EFFECTIVENESS` and `BLOCK_STAMINA_COST` exist. |
| CMBT-J08 | fixed | 2.0 | `tests/audit_m_m18c_test.cpp`. New enchantment `"skill_floor"` (part of the skill level everywhere, highest floor wins); the CQB bionic uses it and the flags NO_MELEE_PRACTICE and CQB_LEARNING, so it's a floor everywhere and no longer drops skilled fighters to 5. Martial arts `"arm_block_char_flags"`/`"leg_block_char_flags"`; the old bio armor fields add ARMORED_ARMS/ARMORED_LEGS, given by the protective mesh CBMs |
| CMBT-J09 | partly fixed | 2.0 | `tests/audit_m_m18c_test.cpp`. Gun and ammunition type `"fire_sound"` (volume, message, burst message), gun `"cycle_time"`, ammunition type `"satisfies_pyromania"`/`"pyromania_message"`; the shipped sounds and pyromania types moved to JSON unchanged. Overheat fault consequences are still keyed to fault ids |
| CMBT-J10 | fixed | 2.0 | `tests/audit_m_m18c_test.cpp`. Math functions `melee_to_hit()`, `dodge()`, `crit_chance()`, `weapon_dispersion()` and assignable `blocks_left()`, `dodges_left()`, `recoil()`, safe on monster talkers; `val('dodge')` was already guarded |
| CMBT-J11 | partly fixed | 2.0 | `tests/audit_m_m18c_test.cpp`. EMP drains and fries any character on the tile, NPCs too; monster `"emp_deactivate_one_in"`; flashbang immunity is FLASHBANGPROOF (the ROBOT species has it) and trait flags FLASH_RESIST_ERRATIC/RETRACTABLE. Card reader/console effects and the e-handcuffs case are still in C++ |
| CMBT-J12 | partly fixed | 2.0 | `tests/audit_m_m18c_test.cpp`. Damage type `"melee_crit"`, `"weakpoint_attack"` and `"embed_weight"`, with the old bash/cut/stab numbers in JSON. Bash strength scaling, the stab skill curve and explosion damage types are still hardcoded |
| CRAFT-B02 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. A ruined unattended craft tells its crafter and interrupts like a finished step; steps take `on_ruin` (result, count, message, EOCs) and every step member is read (the `qt_steel_staff` step's `"quality"` typo became `"qualities"`) |
| CRAFT-B03 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. A reversible step recipe disassembles in one go: `reversible.time`, else the time of its attended steps (dusters 50 m, not 8-10 h) |
| CRAFT-B04 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. Disassembly substitutes are mandatory groups of their own, emptied groups go, a repeated quality keeps the highest level; manual and zone disassembly both read groups as alternatives |
| CRAFT-B05 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. Disassembly adjusts the stored component itself, so parts of a filthy, refitted, faulty or stolen item come out filthy, fitted, faulty and owned by the disassembler |
| CRAFT-B06 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. A camp craft's tool charges split between the player and storage are drawn once: the player's draw (and carried UPS) is taken off before the map's |
| CRAFT-B07 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. The crafting search `l:` filter is parsed once with saturating digits; a long number no longer ends the game |
| CRAFT-J03 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. Only books at least half paper or cardboard disassemble into paper; the new `NO_UNCRAFT` flag opts any item out of disassembly. Custom yields stay an uncraft recipe |
| CRAFT-J05 | partly fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. Unknown recipe flags, EOC ids, autolearn/decomp_learn skills and byproduct groups, and reversible with `result_mult` > 1, are load errors; `"reversible": { "primary": true }`, else the first loaded, gives the disassembly. The long fur scarf's patchwork recipe is marked primary to keep the disassembly it had. XedraWood's practice recipe autolearned from an `occult` skill that doesn't exist (now `deduction`, the skill it trains). Left: uncraft component alternatives and `skills_required`/`proficiencies` (owner decision, ~40 data entries) |
| CRAFT-J06 | partly fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. `u_know_recipe` asks its talker and `npc_know_recipe` exists; recipes take `condition` and `condition_fail_message`. Left: autolearn by proficiency or condition, crafting math functions, a start-craft effect |
| CRAFT-J07 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. The disassembly tool and quality table is `disassembly_substitute` JSON on items and tool qualities; dropped tools such as the press's fire go whatever order the groups come in |
| CRAFT-J08 | fixed | 2.0 | `tests/audit_m_m25a_test.cpp`. Terrain and furniture take `crafting_pseudo_items`, read by the crafting inventory and the reservation providers; the brick oven's tool is JSON, trees keep theirs unless they give a list. Furniture's single ammo-fed `crafting_pseudo_item` stays |
| DATA-B04 | not changed | 2.0 | `tests/audit_data_sweep_test.cpp`. Already fixed (`ef1af991`, `bb3e39f4`): a mod_migration's new_id takes the old mod's place. |
| DATA-B06 | not changed | 2.0 | `tests/audit_data_sweep_test.cpp`. Already fixed (`70395ba2`, `28d1c58b`): submaps with constructions, spawns or reverts are saved in full. |
| DATA-B07 | not changed | 2.0 | `tests/audit_data_sweep_test.cpp`. Already fixed (`5e336317`, `bbc9087d`): the compression toggle can fail without losing data. |
| DATA-B08 | not changed | 2.0 | `tests/audit_data_sweep_test.cpp`. Already fixed (`42771b3b`, `423cafa9`): zzip_stack fails cleanly when a zzip won't open. |
| DATA-J02-speech | fixed | 2.0 | `tests/audit_m_fu_content_test.cpp`. The 547 vanilla speech lines have ids `speech_<first speaker>_<n>` (from `tools/json_tools/speech_ids.py`, same order), so mods can change or remove them; JSON_INFO.md documents the scheme. |
| DATA-J04 | fixed | 2.0 | `tests/audit_m_m04a_test.cpp`. A copy can use relative, proportional, extend and delete on an inherited mandatory member (points, difficulty, value...). |
| DATA-J05 | fixed | 2.0 | `tests/audit_m_m04a_test.cpp`. var_migration erases without "to", takes "scope" and "prefix", and also applies to item, vehicle and computer variables; the hardcoded removed item vars are vanilla var_migrations. |
| DATA-J06 | fixed | 2.0 | `tests/audit_m_m04a_test.cpp`. A mission whose inline update_mapgen names something not loaded yet is deferred and loaded whole at finalize, instead of half-loaded. |
| DATA-J07 | fixed | 2.0 | `tests/audit_m_m04a_test.cpp`. delete runs before extend; map extend replaces a value; numeric maps and weighted lists take proportional, weighted lists take relative. zombie_rust's harvest is restated with the same result. |
| DATA-J08 | fixed | 2.0 | `tests/audit_m_m04a_test.cpp`. New monster_migration, activity_migration (replacing the hardcoded obsolete list) and effect_on_condition_migration; an unloaded queued EOC is dropped without a debugmsg. |
| DATA-J09 | partly fixed | 2.0 | `tests/audit_m_m04a_test.cpp`. Scenario and profession blacklists add up and the last whitelist wins over them. A generic obsolete/delete for every type needs an owner decision. |
| EOC-B06 | not changed | 2.0 | Already fixed by `8c0da604`: `test_eoc` calls `test_condition()`; existing test `eoc-b06_test_eoc_on_an_eoc_without_a_condition`. |
| EOC-B07 | not changed | 2.0 | Already fixed by `a7594255`: a pass takes what is due before running it; existing test `eoc-b07_requeue_with_no_delay_waits_for_the_next_pass`. |
| EOC-B08 | not changed | 2.0 | Already fixed by `69d2d223`, `791c05ca`: all three location helpers; existing `eoc-b08_*` tests. |
| EOC-B09 | fixed | 2.0 | `tests/audit_m_m01a_test.cpp`. Follow-up to `af7ab7bb`/`dae7c28f`: `open_dialogue` with a talker that has no chat topics gives a debug message instead of opening the dialogue on an empty topic stack (a crash). |
| EOC-B10 | not changed | 2.0 | Already fixed by `e184f54e`, `06bcde32`: failure topics can be picked, flags follow their responses, hotkey read bounds-checked. |
| EOC-B11 | not changed | 2.0 | Already fixed by `e8232212`, `47457197`: `u_pickup_items` limits are read as numbers or variables and hold through the pickup. |
| EOC-B12 | not changed | 2.0 | Already fixed by `a66673db`: `u_pick_bodypart` type filters skip the body part. |
| EOC-B13 | fixed | 2.0 | `tests/audit_m_m01a_test.cpp`. Follow-up to `5bb17c04`: `u_spellcasting_adjustment` with a u that isn't a character is a math error instead of a crash; every other listed site was already guarded. |
| EOC-B14 | not changed | 2.0 | Already fixed by `7910df46`, `4d4aac20`, `9a4e959d`, `5677a350`, `db21dfb9`: nesting depth guard of 250 that stops only the endless chain. |
| EOC-B15 | not changed | 2.0 | Already fixed by `ff0b0985`, `86138481`: a queued EOC that takes control follows the queue into its new body. |
| EOC-J03 | partly fixed | 2.0 | `tests/audit_m_m01b_test.cpp`. Responses' topics, failure topics and skill-check skills, NPC chat topics and literal `npc_first_topic`/`open_dialogue` topics are checked at load; 35 broken references in vanilla and mods were fixed. Left: trial `mod` keys (33 entries in data use unknown keys today; the owner decides whether to correct or drop them, with MATH-J03) |
| EOC-J04 | fixed | 2.0 | `tests/audit_m_m01b_test.cpp`. A `deactivate_condition` on a non-RECURRING EOC or beside a `false_effect` is a load error (15 dead ones removed from data); `u_reactivate_eocs` puts a deactivated EOC back. No `u_deactivate_eocs` (unsafe while a queue pass runs) |
| EOC-J05 | fixed | 2.0 | `tests/audit_m_m01b_test.cpp`. `foreach` rejects unknown lists and id kinds at load, lists seven more id kinds, takes variable group targets and the talker's own traits, effects, bionics and spells; three Xedra healing EOCs that looped over nothing now heal |
| EOC-J06 | fixed | 2.0 | `tests/audit_m_m01b_test.cpp`. EVENT EOCs can run on a mod-defined `custom_event`, raised by `trigger_custom_event` with variables and talkers; `trigger_event` takes `with_talkers` |
| EOC-J07 | not changed | 2.0 | Already fixed by DATA-J02: `replace_responses`, `delete` by text/topic, and the portrait is kept |
| EOC-J08 | partly fixed | 2.0 | `tests/audit_m_m01b_test.cpp`. `u_prevent_death` works for monsters and `u_ranged_attack` fires a monster's gun attack (`u_attack` and monster factions were done earlier). Left: base-talker setters stay silent no-ops |
| EOC-J09 | partly fixed | 2.0 | `tests/audit_m_m01b_test.cpp`. `assign_mission` takes a `giver` the mission is turned in to; `u_mission_status` checks active, completed and failed missions. Mission events are EVT-J06's |
| EOC-J10 | fixed | 2.0 | `tests/audit_m_m01b_test.cpp`. PREVENT_DEATH gets the killer as beta, `PREVENT_MONSTER_DEATH` EOCs can stop a monster's death, and `u_add_death_eoc`/`u_remove_death_eoc` edit an NPC's death EOCs. `character_dies` talkers are left to EVT-J06 |
| EOC-J11 | partly fixed | 2.0 | `tests/audit_m_m01c_test.cpp`. Skills, styles and spells take `training_time`/`training_cost` math, trial `difficulty` takes math, speech trials take `speech_practice`, new `u/npc_set_chatbin_topic`, and an `npc_class` can set `stranger_topics`; personality was already math (NPC-J05). Still open: trial and `add_debt` `mod` values are ints (EOC-J03 rewrites that loader) |
| EOC-J12 | fixed | 2.0 | `tests/audit_m_m01c_test.cpp`. `run_eocs` `keep_talkers` queues for any u (monster, item, vehicle) and keeps the npc, across saves; new `u/npc_cancel_queued_eocs`; `time_until_eoc` sees both. Opt-in, so shipped data (void spider gore, monster-targeted spells) runs as before |
| EOC-J13 | not changed | 2.0 | Vehicle effects were done by VEH-J01 and VINT-J01. A tile talker needs MAP-J06's tile variables and TERR-J03's examine beta (this round); wrapping them is the follow-up |
| EVT-B03 | not changed | 2.0 | `tests/audit_events_sweep_test.cpp`. Already fixed by the events sweep (`6c203ba4`): EVENT EOCs no longer fall back to the avatar. |
| EVT-B04 | not changed | 2.0 | `tests/audit_events_sweep_test.cpp`. Already fixed (`b431999b`): timed explosions save their blast. |
| EVT-B05 | not changed | 2.0 | `tests/audit_events_sweep_test.cpp`. Already fixed (`55282064`): the diary names forgotten spells from their type. |
| EVT-B06 | not changed | 2.0 | Checked by reading. Already fixed (`294ddb72`): `game::setup()` clears the memorial. |
| EVT-B07 | not changed | 2.0 | `tests/audit_events_sweep_test.cpp`. Already fixed (`088a203f`): achievements earned while disabled aren't written. |
| EVT-B08 | not changed | 2.0 | `tests/audit_events_sweep_test.cpp`. Already fixed (`9df12c86`): minimum/maximum start from the first event. |
| EVT-B09 | not changed | 2.0 | `tests/audit_events_sweep_test.cpp`. Already fixed (`dd3b0636`): timed map changes happen in their own dimension. |
| EVT-B10 | not changed | 2.0 | `tests/audit_events_sweep_test.cpp`. Already fixed (`cbe83093`): `trigger_event` arguments are checked. |
| EVT-J04 | fixed | 2.0 | `tests/audit_m_m03a_test.cpp`. Effect types get `apply_eocs`, `intensity_change_eocs` and `remove_eocs`, run for any creature (u the holder, npc the cause if found), and held until effect loops end. `broken_bone` is sent for NPCs too; the avatar's limb statistics now require the avatar. Monster-cast spells still send no event. |
| EVT-J05 | fixed | 2.0 | `tests/audit_m_m03a_test.cpp`. `allow_late_tracking` on achievements and scores. `give_achievement` reports bad ids and conducts. New `fail_conduct` effect, and `achievement_state()`/`past_achievement()` math. `unlocks_all_content` replaces the Arcade Mode id checks in profession and scenario unlocks. |
| EVT-J06 | fixed | 2.0 | `tests/audit_m_m03b_test.cpp`. New `monster_dies` (any killer, monster alpha, killer beta) and `mission_ends` events; `character_dies` gets `killer`/`killer_type` and the killer as beta; a finished construction now runs ACT_BUILD's completion hooks (`completion_eoc`, `character_finished_activity`). `npc_recruited` and `character_finishes_construction` already existed |
| EVT-J07 | fixed | 2.0 | `tests/audit_m_m03b_test.cpp`. New `watched_variable` type and `var_changed {owner, scope, var, value}` event, sent for any write of a watched variable (math, `u_add_var`, any variable object) with its owner as alpha; `u_var_changed` is now only sent for the avatar's own variables |
| EVT-J08 | fixed | 2.0 | `tests/audit_m_m03b_test.cpp`. New fields: `damage_type` on the damage events, `victim_type`/`bodypart` on `monster_takes_damage`, `bodypart` on heals, `pos` on smash/trap/cut tree and `target` on spell casts; eat/consume carry the item as beta talker; new variant types `damage_type_id`, `item_category_id`, `material_id`. Statistics leave out the locations and `monster_takes_damage`'s new fields, to keep saves small |
| EVT-J09 | fixed | 2.0 | `tests/audit_m_m03b_test.cpp`. New stat types `count_distinct`, `average`, `first_time`, `last_time`; new transformations `category_of_itype`, `materials_of_itype`, `flags_of_monster`, `flags_of_furniture`, `flags_of_terrain_str`, `mutation_category_of_trait` (all listed in JSON_INFO.md) |
| EVT-J10 | partly fixed | 2.0 | `tests/audit_m_m03b_test.cpp`. ROBOT_ATTACK now sends `becomes_wanted` for content to answer; HELP no longer pops a debugmsg (and is no longer a spell timed_event name); new EOC condition `timed_event_queued` and effect `cancel_timed_events`. Still open: the scripted events (wyrms, amigara, temple, roots, DSA) are hardcoded C++, and the dead enum values need a save migration to remove |
| HLTH-B11 | not changed | 2.0 | `tests/audit_char_sweep_test.cpp`. Already fixed by the CHAR sweep (`e92cd5d2` to `b25844ab`): the hallucination stages follow the dose's own start and end |
| HLTH-J04 | partly fixed | 2.0 | `tests/audit_m_m15a_test.cpp`. Wounds are checked at load, `whitelist_flags` means any of them as documented, `"bleeding"` overrides the blood rule, and the new events `character_gains_wound`, `character_wound_progresses` and `character_loses_wound` come from hits, timers, healing, EOCs and treatments. Left: a periodic EOC while a wound lasts |
| HLTH-J05 | fixed | 2.0 | `tests/audit_m_m15a_test.cpp`. Vitamins also report min above max, unknown or self `decays_into` and tiers outside [min, max]; the beta blocker's two unreachable excess tiers are removed (no change in play) |
| HLTH-J06 | partly fixed | 2.0 | `tests/audit_m_m15a_test.cpp`. The outright overdose deaths read the external options `OVERDOSE_*` (same defaults; 0 turns the effect ones off). Left: the stimulant and painkiller symptom ladders |
| HLTH-J07 | partly fixed | 2.0 | `tests/audit_m_m15a_test.cpp`. Effects get `tick_eocs` every `tick_eocs_interval`, for any creature (apply, intensity and remove EOCs were done by EVT-J04). Left: a morale field on effects |
| HLTH-J08 | partly fixed | 2.0 | `tests/audit_m_m15a_test.cpp`. A morale type's `"permanent"` decides whether it can be permanent (the 8 core types have it) and `morale_value()` reads one type. Left: trait multipliers, clothing constraints and the masochist/radiophile formulas |
| HLTH-J09 | partly fixed | 2.0 | `tests/audit_m_m15a_test.cpp`. `u_expose_to_disease`, and disease body parts and ranges are checked. Left: cold and flu still hardcoded, contagion avatar-only |
| HLTH-J10 | partly fixed | 2.0 | `tests/audit_m_m15a_test.cpp`. Math `stomach()`, `wound_healing()` (read/write), `wound_pain()`, `morale_value()`, and `u_wake_up`. Left: extra fields on the eat events (the `addiction_intensity` assigner was added at the merge) |
| HLTH-J11 | partly fixed | 2.0 | `tests/audit_m_m15b_test.cpp`. addiction_type has `dose_satiation`, `recovery_time`, `intensity_decay`, `max_intensity` and `remove_below`; Addictive Personality and Addiction Resistant are the enchantment values `ADDICTION_STRENGTH`/`ADDICTION_SATIATION`/`ADDICTION_RECOVERY` (same numbers), and `pkill_addict_reduces` can name its addiction. `addiction_intensity()` is assignable, capped at `max_intensity` (added at the merge). Left: the seven builtins are still C++ |
| HLTH-J12 | partly fixed | 2.0 | `tests/audit_m_m15b_test.cpp`. Heavy Sleeper, Very Heavy Sleeper and Hibernation are the enchantment values `WAKE_RESISTANCE`, `WAKE_PAIN_THRESHOLD` and `SLEEP_LIGHT_TOLERANCE` (same numbers); Aqueous Repose takes its sleepiness off once, and only in water; comfort conditions can be any dialogue condition. Left: the disrupted_sleep/melatonin recovery multipliers (a data change to SLEEPINESS_REGEN is proposed), the Chloromorph/Mycus sleep blocks, the bionic and narcosis exemptions |
| HLTH-J13 | partly fixed | 2.0 | `tests/audit_m_m15b_test.cpp`. Effects can have JSON `periodic_messages`; tapeworm, bloodworms and paincysts are entirely JSON, and so are the brainworms and tetanus flavour lines. Left: the other hardcoded effects, best ported with HLTH-J07's tick EOCs |
| HLTH-J14 | partly fixed | 2.0 | `tests/audit_m_m15b_test.cpp`. Narcoleptic, Mood Swings and Shouter/Screamer/Howler are processed_eocs, with the new `u_shout` effect and `u_in_sleep_state` condition. Left: Vomitous (an activated trait) and the other suffer.cpp trait effects, radioactivity, photosynthesis, asthma relief and the mend slowdowns |
| IINF-B06 | fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. `get_min_str( who )` and the bionic branch of `ammo_capacity` use the holder (carrier parameter, avatar only as the UI fallback); callers pass the character, and the RELOAD_AND_SHOOT reload tires the shooter, not the avatar. |
| IINF-J01 | fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. `armor()` counts all combat protection by default (`'source': 'worn'` for the old number) and works on items with a coverage `'roll'`; Bombastic Perks Iron Arm keeps worn-only. |
| IINF-J02 | fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. Clothing mod values are worked out from the item's current flags, so any way of setting the flag counts and rebalances apply; the old saved vars are dropped on load. |
| IINF-J03 | fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. Armor `active_form` drives the "When active" preview (power armor falls back on its transform target or an existing `_on` item, rm13 uses the field); filter protection shows for any filter armor; new `NO_ACTIVE_SUFFIX` flag. |
| IINF-J04 | fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. Blacklist labels swapped back; gunmod info shows fire modes, ammo/energy per shot, heat, shot spread, barrel length, bayonet and install time; overheating guns show their heat numbers. |
| IINF-J05 | fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. Gunmod rules come from data (`blacklist_gun_flags`, `blacklist_message`, `installable`, `requires_unloaded`, gun `required_mod_targets`); the vanilla items carry the old hardcoded rules. |
| IINF-J06 | fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. Clothing mod values take any damage type and fall back through `derived_from`, so padding now helps against stab (0.8 of its cut value). |
| IINF-J07 | partly fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. Setting item power no longer unloads guns; new item math `ammo_remaining()` (assignable), `ammo_capacity()`, `has_gunmod()`, `gun_heat()` (assignable). The ammo/gunmod/gun-mode effects are not done. |
| IINF-J08 | partly fixed | 2.0 | `tests/audit_m_m11a_test.cpp`. New events for reload, unload, gunmod install/remove, armor damaged and plate transformed; ablative plates send `character_armor_destroyed`. `non_functional` for worn armor needs an owner decision (it would change `helmet_riding`). |
| INV-J05 | fixed | 2.0 | `tests/audit_m_m12a_test.cpp`. The code was fixed with EOC-B05; the docs now name the effect, mark `amount` mandatory and say that a consumed container drops its contents where it was |
| INV-J06 | partly fixed | 2.0 | `tests/audit_m_m12a_test.cpp`. Drawing from a holster or sheath applies `OBTAIN_COST_MULTIPLIER`, and the new `STORE_COST_MULTIPLIER` applies to putting items away; the electronic handcuffs' slow handling is the new `BINDS_HANDS` flag. Left: a pocket `insert_moves` and the other hardcoded handling numbers |
| INV-J07 | partly fixed | 2.0 | `tests/audit_m_m12a_test.cpp`. An item talker answers item, charge, flag and category queries and `remove_item_with` for its contents, `u_spawn_item` fills it, and `u_run_inv_eocs` offers its contents; `u_spawn_item` with a container gives `count` copies and hands over what doesn't fit. Left: monster and vehicle talkers, and new container effects |
| INV-J08 | partly fixed | 2.0 | `tests/audit_m_m12a_test.cpp`. New events `character_picks_up_item`, `character_drops_item` and `character_stores_item`, with the item or container as beta. Left: events for taking out, unsealing and spilling, and itype or pocket EOC hooks |
| INV-J09 | partly fixed | 2.0 | `tests/audit_m_m12a_test.cpp`. `sealed_data` can also set `watertight`, `airtight`, `transparent`, `insulation` and `moves`, which apply only while the pocket is sealed. Left: unseal tool, message and resealable |
| ITEM-B10 | fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. convert() reads count_by_charges before the type changes, and a stack that a countdown turns into single items becomes one item per charge, both carried and on the map. Top-level carried stacks and iuse/EOC transforms of a stack still become one item. |
| ITEM-J02 | fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. An explicit degradation_multiplier works on any item and copy-from no longer compounds it. Without one, only veh_parts degrade, as before. |
| ITEM-J03 | fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. "faults" and consumption_effect_on_conditions replace the inherited list, with extend and delete. The children that relied on appending now use extend (same lists), and "faults": [] now clears (coilgun, hk_g80, mp3_reverberation, 1cyl_combustion_small). Relic effects, fault mods, fault_group and clothing_mod lists still append. |
| ITEM-J04 | fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. New item math item_charges, item_temperature, item_freshness, item_wetness, item_relic_charges and item_countdown, all assignable, plus u_/npc_transform_item. |
| ITEM-J05 | fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. Relic activation sends activates_artifact (with a new itype field), and recharge_type "condition" recharges while recharge_eoc's condition holds. |
| ITEM-J06 | partly fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. Monsters get corpse_shelf_life, corpse_rot_away and revive_delay, and the FIELD_DRESS/MUSHY/IRRADIATED rot multipliers are now json_flag rot_multiplier in flags.json. Storage temperature profiles, carried-item warmth and the revival messages/traps are still C++. |
| ITEM-J07 | fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. Item group entries take a dialogue "condition", groups that copy from themselves can "delete" items and groups, and dirt is documented. There is no prob override or fit_chance. |
| ITEM-J08 | partly fixed | 2.0 | `tests/audit_m_m10a_test.cpp`. fault_fix gets "condition" and "effect_on_conditions", and every fault_group is now validated. Fault triggers, recovery and fault add/remove EOC hooks are still C++. |
| LOOP-B05 | not changed | 2.0 | Already fixed by EVT-B01 (`90c08ef9`): death events latch once (`avatar_death.cpp`), and `is_game_over()` returns early in deathcam. |
| LOOP-B06 | not changed | 2.0 | Already fixed (`12247a06` to `d27488aa`): water temperature and sap season count from the start of spring; `tests/audit_loop_sweep_test.cpp`. |
| LOOP-B07 | not changed | 2.0 | Already fixed: "Set alarm for dawn" uses its own retval (16). |
| LOOP-B08 | not changed | 2.0 | Already fixed: the motion alarm fires at most once a turn, for moved, unfriendly monsters; `tests/audit_loop_sweep_test.cpp`. |
| LOOP-B09 | not changed | 2.0 | Already fixed: a plain-number `temperature_modifier` is °C; `tests/audit_loop_sweep_test.cpp`. |
| LOOP-B10 | not changed | 2.0 | Already fixed with MATH-B03 to MATH-B11 (`noon()` and `time_since/time_until('noon')`); `tests/audit_math_talkers_test.cpp`. |
| LOOP-B12 | not changed | 2.0 | Already fixed by MAP-B03: rain field decay ages the real field (`field_entry &cur`). |
| LOOP-B13 | not changed | 2.0 | Already fixed: wind comes from seeded noise, so `get_weather` is pure; `tests/audit_loop_sweep_test.cpp`. |
| LOOP-B14 | not changed | 2.0 | Already fixed: the world is saved before the player file is committed, and dimension travel saves first and restores on failure; `tests/audit_loop_sweep_test.cpp`. |
| LOOP-B15 | not changed | 2.0 | Already fixed: weather effects run whatever the player's level, and `is_creature_outside` uses the creature's own z; `tests/audit_loop_sweep_test.cpp`. |
| LOOP-J04 | fixed | 2.0 | `tests/audit_m_m05a_test.cpp`. Snow-to-water ratio, melt rate, maximum depth and the character/monster move-cost table are now the region's weather_generator `snow` settings, with the old constants as defaults. Weather types can set `snow_rate`, and EOCs read/set depth with `weather('snow_depth', 'location': ...)` and read `weather('snow_rate')`. The sidebar/overlay snow labels still use fixed thresholds. |
| LOOP-J05 | fixed | 2.0 | `tests/audit_m_m05b_test.cpp`. Weather types can set `precip_rate`, `wetness`, `field_decay` and a `glare` list (defaults follow precip), and rain and glare now reach NPCs in the bubble as well as the avatar. |
| LOOP-J06 | fixed | 2.0 | `tests/audit_m_m05b_test.cpp`. Weather generators can set the climate shape, underground and water temperature, and wind direction weights (defaults give identical temperatures). The weather lists are checked weather type ids, and overmap terrains have `wind_factor` instead of the forest id check. |
| LOOP-J07 | partly fixed | 2.0 | `tests/audit_m_m05b_test.cpp`. A json_flag's `blocked_actions` blocks keyed actions with a message, and the handless-shapeshift table moved to flags.json. The shell, incorporeal, riding, BRAWLER and GUNSHY blocks and action events/veto hooks are still in code. |
| LOOP-J08 | partly fixed | 2.0 | `tests/audit_m_m05b_test.cpp`. New `CLIMB_SLIP_CHANCE` enchantment, and climbing aids with `down.sheer_drop` are offered on stairs that end in a sheer drop. The built-in web, vine, grapnel and rope branches and the BADKNEES/parkour rule are still hardcoded. |
| LOOP-J09 | partly fixed | 2.0 | `tests/audit_m_m05b_test.cpp`. Regions have `random_npcs` (radius, crowding, classes or common classes, spawn EOCs) and scenarios have `starting_npc`; vanilla is unchanged, and making common classes the default is the owner's call. |
| LOOP-J10 | partly fixed | 2.0 | `tests/audit_m_m05b_test.cpp`. Skills have `max_level` (up to 10) for practice, books, rust and character creation. Raising skills above 10, BIO_CQB_LEVEL and fridge/freezer set points are still hardcoded. |
| MAGIC-B05 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. Damage, accuracy, damage over time and field intensity stop at max_X whichever way they move with level, as range and aoe did. Spells with only min_damage now deal it (Wolfsbane pike, two Xedra weapons, MoM Force Shove and Gracken arms start working) |
| MAGIC-B06 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. Each defence above the spell's accuracy takes up to a third off by its margin; spell hits no longer run a real dodge (uncanny dodge, dodge attempts, the "conserve stamina" message) |
| MAGIC-B07 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. Spell migrations apply while the save loads, so a removed spell migrates with its XP, hotkey and favorite; chains work |
| MAGIC-B08 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. pain_split shares damage between the live main body parts only, never above a part's max, and tells only its caster |
| MAGIC-B09 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. Summons skip occupied tiles (the caster's) and tiles a monster won't take, without a debugmsg |
| MAGIC-B10 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. Mana regeneration carries fractions between ticks, so pools under 320 regenerate fully in 8 hours |
| MAGIC-B11 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. Channeling follows the spell (and level) whose cast started it, from any caster or source, and is saved |
| MAGIC-B12 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. Trait and bionic spell losses message and send events for their owner; losing an already-forgotten spell is quiet; trait grants stop at max level |
| MAGIC-B13 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. NPC targeted casts rate the spell they cast, known or not; monsters aim at their foe |
| MAGIC-B14 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. SWAP_POS does nothing without a creature on the target and swaps two monsters through the creature tracker |
| MAGIC-B15 | fixed | 2.0 | `tests/audit_m_m27a_test.cpp`. RANDOM_TARGET and RANDOM_CRITTER spells cast at their caster's tile (enchantments, relics, monster self casts, EOCs) pick a target in range; RANDOM_CRITTER works alone |
| MAGIC-B16 | fixed | 2.0 | `tests/audit_m_m27b_test.cpp`. A value or `ench_effects` effect listed twice in one enchantment is a load error (three shipped duplicates fixed; the hound ichor now sums its seven parts). Copy-from enchantments keep has/condition and override per key; hit_you/hit_me_effect, spell flags and magic_type cannot_cast_flags take extend/delete |
| MAGIC-J05 | fixed | 2.0 | `tests/audit_m_m27b_test.cpp`. New `character_levels_spell` event for every character; `learn_spells` teaches whoever leveled (NPCs too), and NPC spellbook adjustments cap at the NPC's own max level (NPC-B06's change, kept at the merge) |
| MAGIC-J06 | partly fixed | 2.0 | `tests/audit_m_m27b_test.cpp`. ter_furn_transform entries take `chance` and fields `intensity`; the field loop no longer changes the fields it walks. Checks and data were MAGIC-J03; per-entry conditions (no caster passed in) and field flags left |
| MAGIC-J07 | partly fixed | 2.0 | `tests/audit_m_m27b_test.cpp`. Spell values and conditions see the context vals `spell_id` and `spell_level` (the cast level), right for item, monster, EOC and copy-from casts; the target as beta talker left |
| MAGIC-J08 | fixed | 2.0 | `tests/audit_m_m27b_test.cpp`. Relic enchantments see their item as `npc` (n_ variables); effect, bionic and mutation enchantments get `source_effect`/`source_intensity`, `source_bionic`, `source_trait` |
| MAGIC-J09 | partly fixed | 2.0 | `tests/audit_m_m27b_test.cpp`. magic_type `encumbrance` sets the hand/leg/arm/mouth thresholds and costs (defaults unchanged); mana pool and regen, learn time and the psionic caps left |
| MAGIC-J10 | partly fixed | 2.0 | `tests/audit_m_m27b_test.cpp`. Math `spell_damage/range/aoe/duration/max_level` and `enchantment_value`; `level` on both cast events; new `monster_casts_spell`; per-spell `failure_eocs` with `spell_id`/`spell_level` context. spellcasting_adjustment reads and on_cast_eocs left |
| MAP-B09 | fixed | 2.0 | `tests/audit_m_m06a_test.cpp`. A field's `monster_spawn_count` is no longer halved, and monsters queued by fields, rotting food and births are placed every turn instead of at the next map shift. Overmap group and static spawns still count each monster twice; fixing that doubles spawn density and is left to the owner |
| MAP-B10 | fixed | 2.0 | `tests/audit_m_m06a_test.cpp`, `tests/audit_perf_world_inventory_test.cpp`. Building or smashing furniture (or a vehicle or web) on frozen water no longer freezes it for good, reloading leaves frozen tiles' damage and traps alone, and the reality bubble freezes and thaws at midnight, skipping tiles with a creature or vehicle |
| MAP-J03 | partly fixed | 2.0 | `tests/audit_m_m06a_test.cpp`. CARGO vehicle parts take a JSON `processing_cycle` (duration, item flags and transforms, messages, start EOC that can refuse, finish EOC). The vanilla washer, dishwasher, autoclave and weather station keep their C++ code |
| MAP-J04 | partly fixed | 2.0 | `tests/audit_m_m06a_test.cpp`. Terrain `produces_liquid` (the tapped maple's sap, unchanged), `rad_scorch`, and `item_temperature` on terrain and furniture (the root cellar). An `eoc_on_actualize` hook is left to the catch-up part of the grids design |
| MAP-J05 | partly fixed | 2.0 | `tests/audit_m_m06a_test.cpp`. TREE terrain `felled` trunk and stump, terrain `churned_into`, and the unused `hit_with_fire` removed. Acid results, the fire pit-edge check and the roof fallback are still hardcoded |
| MAP-J06 | partly fixed | 2.0 | `tests/audit_m_m06a_test.cpp`. `field_strength` is assignable, plus `field_age`, `map_radiation` and a `u_set_field` `mode` (add, set, max). Tile variables and a tile talker are left to the grids design |
| MATH-B03 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `eb2fd836`; re-checked in the current code (`_get_faction` throws on an unknown id). |
| MATH-B04 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `6447e6e2`, `242c90f2`; `time()`/`calories()` assignments honour `unit`/`format`. |
| MATH-B05 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `add9ea07`, `01e82cca`; time left, soonest match, u/n queues. |
| MATH-B06 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `8086866f`; `noon()` is today's noon. |
| MATH-B07 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `b32e8bbe`; `allow_hallucinations` kwarg, radius always applied. |
| MATH-B08 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `b7459927`; `%` groups left to right. |
| MATH-B09 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `207f356b`, `7433c2a8`; item/computer/zone talkers' own OMT. |
| MATH-B10 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `fc8480be`, `7433c2a8`; a dead item talker is guarded. |
| MATH-B11 | not changed | 2.0 | `tests/audit_math_talkers_test.cpp`. Already fixed by `94250384`, `8c0bb8a8`, `a4106430`; `val('pos_*')` moves as `u_teleport` does. |
| MATH-J03 | partly fixed | 2.0 | `tests/audit_m_m02a_test.cpp`. Trial `difficulty` is a number, variable or math, and `mod` entries can be math, with any talker. The base LIE/PERSUADE/INTIMIDATE formulas and a JSON trial type are left; dead legacy mod names in shipped trials (`OWED`, lowercase names) are proposed, not changed. |
| MATH-J04 | fixed | 2.0 | `tests/audit_m_m02a_test.cpp`. Literal ids in 65 id-taking dialogue functions are checked when data loads (a JSON error at the math; jmath with the EOC checks); `"optional_ids": true` skips another mod's. 20 typo'd ids fixed in vanilla (exodii `'social'` → `'speech'`), MoM, XedraWood, XE and Aftershock; cross-mod and unclear references marked `optional_ids`, behaviour unchanged. |
| MATH-J05 | partly fixed | 2.0 | `tests/audit_m_m02b_test.cpp`. New `wetness('bp')` (read and set), `warmth('bp')` can be set, new `u_activity` mutator; effect duration/intensity assignment and `u_has_activity` ids were done in `3fb1f946`. Left: character `morale()` assignment (owner decision: implement as a temporary morale point, keep it a no-op, or make it an error); speed bonus isn't settable because it is rebuilt every turn |
| MATH-J06 | partly fixed | 2.0 | `tests/audit_m_m02b_test.cpp`. `n_pain()`/`n_hp('bp')` assignment works on monsters; new `is_hallucination()` and `u_monster_faction` mutator; `monsters_nearby()` and friends skip hallucinations unless `allow_hallucinations`, and take `enemy`/`neutral` attitudes (`hostile` unchanged). Left: upgrade/reproduction timers, nickname, ammo |
| MATH-J07 | fixed | 2.0 | `tests/audit_m_m02b_test.cpp`. Item power is battery charge only (a gun's is 0) and keeps a mod's battery ammo; encumbrance uses the carrier; `val('count')` can be set; new `item_temp()`, `rot()`, `ammo_count()` and `u_loaded_ammo` |
| MATH-J08 | partly fixed | 2.0 | `tests/audit_m_m02b_test.cpp`. Zone talkers have a name and saved variables; `u_run_fixed_zone_eocs` takes `zone_types` and a variable `zone_range` and gives `_zone_start`/`_zone_end`; new `u_is_zone`, `u_zone_type`, `u_is_computer`. Left: a real furniture/terrain talker for examine_action EOCs, and renaming the computer talker |
| MATH-J09 | fixed | 2.0 | `tests/audit_m_m02b_test.cpp`. `u_is_driven` counts an NPC at the controls; fuel, battery, HP, cruise and weight were already covered by VEH-J01, VINT-J01 and VINT-J04 |
| MATH-J10 | partly fixed | 2.0 | `tests/audit_m_m02b_test.cpp`. New `dialogue_topic_override` type adds opening topics and can replace the built-in rules (`reboot`, `sedated`, `deaf`, `mute`, `churl`, `npc_noface`); the duplicate `get_topics()` call was already gone. Left: built-ins not ported to data; dead `TALK_NOFACE` (owner decision) |
| MATH-J11 | fixed | 2.0 | `tests/audit_m_m02b_test.cpp`. jmath functions take strings and arrays, `u_`/`n_` scopes and an `assign` body; `_N` past `num_args` and shadowing names are load errors |
| MGEN-B08 | fixed | 2.0 | `tests/audit_m_m08a_test.cpp`. A nested chunk turns by its `rotation` when it sets `"allow_rotation": true` (rows, set points, pieces, vehicle facing, the chunks it places); without it the rotation is ignored, as documented now, so the 1540 shipped chunks asking for `[0, 3]` are unchanged |
| MGEN-B09 | fixed | 2.0 | `tests/audit_m_m08a_test.cpp`. Mapgen parameters finalize bottom-up, so parameters declared by chunks nested in nested chunks reach the top mapgen and are rolled (75 in vanilla, no conflicts in any mod); a `"null"` choice no longer ends the merge |
| MGEN-B10 | fixed | 2.0 | `tests/audit_m_m08a_test.cpp`. `map::mirror` moves vehicles (and their caches), spawns, zones, NPCs and queued points as `map::rotate` does; a vehicle is turned, not mirrored part by part. Camp farm plots undo their turn and mirror in the right order |
| MGEN-J01 | fixed | 2.0 | `tests/audit_m_m08a_test.cpp`. `place_eocs` (and palette `eocs`) and map extra `eocs` queue EOCs with `loc`; `omt_generated` and `map_extra_spawned` events are sent at the start of the next turn; `place_variables` rolls its range |
| MGEN-J02 | fixed | 2.0 | `tests/audit_m_m08a_test.cpp`. EOC `mapgen_update` takes `parameters`, `rotation`, mirroring, `offset_from_target`, `ignore_vehicle_collision` and `success_eocs`/`failure_eocs` (with `reason`); a delayed update keeps its settings, in saves too |
| MGEN-J03 | partly fixed | 2.0 | `tests/audit_m_m08a_test.cpp`. Map extra `condition`, EOC `place_map_extra` and condition `map_has_extra`; the extra applied is the one picked. Collection weights are still plain numbers |
| MGEN-J04 | partly fixed | 2.0 | `tests/audit_m_m08a_test.cpp`. Region river, lake and ocean settings can set the built-in water mapgen's terrain (`mapgen_deep_water`, `mapgen_bank`...); vanilla is unchanged. The forest tuning and the subway layouts are still in C++ |
| MGEN-J05 | partly fixed | 2.0 | `tests/audit_m_m08b_test.cpp`. Post-process sub-generators take their content (`burnt_*_ter` for pre_burn, `field` for fire and blood, `ruin_*`/`wreckage_furn` for aftershock_ruin, Aftershock's by default) and a `condition`; a copy can `delete`/`extend` sub_generators; city roads run their region's `road_post_process` (default riot_damage_road). Left: a JSON replacement for aftershock_ruin, the legacy PP_GENERATE_* flags (Aftershock could move to `post_process_generators`), integer-day timing |
| MGEN-J06 | fixed | 2.0 | `tests/audit_m_m08b_test.cpp`. spawn_data `eocs` run on the placed monster and are kept in saves; patrol ranges are rolled for each monster instead of once at load (older saves' points load as fixed); place_npcs takes `variables` and `eocs`. Effects, variables, faction and saving came with MONDT-J01 |
| MGEN-J07 | partly fixed | 2.0 | `tests/audit_m_m08b_test.cpp`. Any `place_*` entry can have a `condition` (context `loc`, `oter_id`), checked for each repeat; a mapgen value can read an EOC variable (`{ "var": ..., "fallback": ... }`); variable chunk weights are documented. Left: dbl_or_var `chance`/`repeat`, location context for mapgen and chunk weights |
| MGEN-J08 | partly fixed | 2.0 | `tests/audit_m_m08b_test.cpp`. Any `place_*` entry can have a `place_on` filter (terrain ids/flags, no furniture, passable, near terrain, attempts) that rerolls x/y until a tile matches. Left: porting the C++ map extras to JSON, a content change for the owner |
| MONAI-B04 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. polymorph_special tests its `condition` (target as npc), needs `can_act`, and has `allow_no_target` (default true; Xedra's wendi sets false so it changes only while grabbing); an unknown `mon_id` is a load error, and the actor is documented |
| MONAI-B05 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. New monsters start with `last_updated` = now and no biosignature timer, and saves holding the old -1 load as unset, so a spawn's `on_load()` no longer zeroes special cooldowns or drops 51 items |
| MONAI-B06 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. Knockback kills an AQUATIC monster only when it lands off water, and a monster knocked toward the player bounces off them instead of sharing their tile |
| MONAI-B07 | not changed | 2.0 | `tests/audit_quick_fixes_items_test.cpp`. Already fixed as MONDT-J03 (`8b086ee3`, `9c34f451`): `refill_udders` keys on the starting-ammo liquid and finds the entry again after converting legacy milk |
| MONAI-B08 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. The guard and trigger check were already fixed; the debug message no longer dereferences `as_npc()` of a monster target, and the baby counts by its own distance and sight (it was ignored when farther from the player than its parent) |
| MONAI-B09 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. `absorb_ml_per_hp` is read per type (no function-local static) and must be above 0; `monster::can_absorb_item` is the one material rule for the attack and `items_available`, and the doc names are fixed |
| MONAI-B10 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. PUSH_VEH monsters shove a vehicle once, on the square they step to, only when an obstacle part blocks it, never while pacified or on their goal's own square |
| MONAI-J02 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. EOC attacks always enforce `range` (-1 or none is any distance) and get `move_cost`, `target_optional` and `no_adjacent`; spell attacks get `target_optional`; `turn_cost` works on any creature; the doc table is rewritten |
| MONAI-J03 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. Leap and gun conditions see the target, both actors get an `eoc`, melee EOCs get `bodypart`, `damage_<type>` and `eoc_on_miss`, and cooldowns read the target (or an empty npc) instead of the avatar; the gun EOC runs per attack, without per-shot hit data |
| MONAI-J04 | fixed | 2.0 | `tests/audit_m_m19a_test.cpp`. `hearing_mult`, `scent_sensitivity` and `footstep_volume` in monster JSON, with the flags as defaults; the HEARING_MULT and FOOTSTEP_NOISE enchantments apply to monsters, and footsteps can't go negative. The QUIETMOVES and SILENTMOVES flags had never been given their ids in C++, so no monster (or mount) was quieter for them; now they work. |
| MONAI-J05 | partly fixed | 2.0 | `tests/audit_m_m19b_test.cpp`. New events `monster_upgrades`, `monster_reproduces` and `monster_uses_special` (statistics skip the last), with the monster as alpha. Being hit and melee hits are covered by the damage events. Still missing: spawn, target and flee hooks, and moving the VENOM flags to `attack_effs` |
| MONAI-J06 | partly fixed | 2.0 | `tests/audit_m_m19b_test.cpp`. New mutation member `monster_relations` (species, faction or monster flag; anger, morale, min/max anger, FRIEND or IGNORE). The bee, mycus, pheromone, flower and terrifying rules are now in the mutations, with the same results. Only angering relations get past an ignoring monster. The new faction member `ignores_npcs_if_option` replaces the portal-storm override. The animal empathy and discord flags stay in C++ |
| MONAI-J07 | partly fixed | 2.0 | `tests/audit_m_m19b_test.cpp`, `tests/creature_effect_test.cpp`. New `effect_immunities` (with a chance) on species and monsters; the poison, venom and tpollen tables moved to species.json and to the mods that redefine those species. New `bleed_effect` (robots). Effect `immune_flags` are checked first, so zombies now resist the POISONOUS mutations' venom. The bodytype `downed` rules and the damage-immunity FIXMEs stay in C++ |
| MONAI-J08 | partly fixed | 2.0 | `tests/audit_m_m19b_test.cpp`. Trigger objects take `amount`, `range` and `cap` where the code reads them. New `CONDITION` triggers: a condition with the target as beta, a math amount and an optional `aggro_character`. BRIGHT_LIGHT documented, STALK corrected. HURT, SOUND and PLAYER_WEAK strengths stay fixed |
| MONAI-J09 | partly fixed | 2.0 | `tests/audit_m_m19b_test.cpp`. New mtype member `behavior`, `eoc:ID` and `special:NAME` goals, and the attack target as `npc` in monster tree conditions and scores. Flee, wander and follow goals, and the attack gate (MONAI-B12), are still missing |
| MONDT-B05 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. Partial cover keeps the target's profile fractional (`partial_cover_sight_range`), so crouching or prone behind cover, or being seen from another z-level, shortens the range a creature sees you from instead of always 30 tiles. |
| MONDT-B06 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. The DISTRIBUTED_DAMAGE ranged hit message gets its damage value (no more debugmsg on every hit), and a shot with no shooter (NULL_SOURCE ammo) no longer dereferences it. |
| MONDT-B07 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. TRUE_SEEING/TRUESIGHT see camouflaged and invisible creatures through the normal range, light and line-of-sight checks instead of through walls; INFRARED_VISION needs line of sight. Flag docs corrected, ALL_SEEING too. |
| MONDT-B08 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. Harvest entry faults are added (not removed) to liquid and discrete drops, so salvaged CBMs keep `fault_bionic_salvaged`; harvest lists check their entries' flag and fault ids. |
| MONDT-B09 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`, `tests/audit_high_process_test.cpp`. The `bone` drop type leaves a field-dressed corpse half to all of its bones, so dressing plus butchering gives on average the bones of butchering the whole corpse, not up to twice as many. |
| MONDT-B10 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`, `tests/harvest_test.cpp`. Butchery-time proficiency penalties are fractional, so skinning proficiency and partial butchering practice change the time; helpers are the butcher's own. |
| MONDT-B11 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. `mount_items.storage` fills the storage slot instead of overwriting the saddle. |
| MONDT-B12 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. The summoner is saved (old saves load with none) and can be a monster, so kill credit and COPY_SUMMONER_LOOK survive a reload and monster-cast summons copy their caster's look. |
| MONDT-B13 | fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. Extending a monster group keeps its `is_animal`, `is_safe` and replacement fields; the docs say what `is_safe` and `is_animal` do and that group replacement (`replace_monster_group`) is not implemented. |
| MONDT-B14 | partly fixed | 2.0 | `tests/audit_m_m20a_test.cpp`. `block` inherits what it leaves out, `upgrades` reads its own `multiple_spawns`/`spawn_range` (false drops an inherited range), swim is range-checked, an integer `bash_skill` works, `monster_adjustment` refuses unknown stats and takes a `monsters` filter, dead `death_map` removed. Still open: a missing `harvest` still means `human` (about 400 monsters rely on it; owner decision). |
| MONDT-B15 | fixed | 2.0 | `tests/audit_m_m20b_test.cpp`. `longpull` counts a STR or grab_strength of 0 as 1 instead of dividing by zero; a `pull_target` spell with damage pulls with that strength (the shipped pull spells have none, so are unchanged; giving them damage is proposed) |
| MONDT-B16 | fixed | 2.0 | `tests/audit_m_m20b_test.cpp`. Butchery adds tool quality to the skill level only for dissection, so survival drives yields again; low-skill yields from mass_ratio entries drop (0.29 instead of 0.79 of the maximum at survival 0 with a BUTCHER 20 knife), unchanged from survival 10. The weights are the external options `BUTCHERY_YIELD_SKILL_WEIGHT`/`_TOOL_WEIGHT`/`_DEX_WEIGHT` (0.5/0.3/0.2) for retuning |
| MONDT-J02-content | fixed | 2.0 | `tests/audit_m_fu_content_test.cpp`. The `bionic`, `mutagen` and their `_group` drop types take `action_yield` FULL 1, DISMEMBER 0, DISSECT 1, so organs and implants in a harvest list (Magiclysm's adrenal glands) no longer come from field dressing and again from full butchery. |
| MONDT-J04 | fixed | 2.0 | `tests/audit_m_m20b_test.cpp`. `dissect_only` drop types give nothing outside dissection (the vanilla organ and implant types lose the flag they never used, so Magiclysm and Aftershock harvests are unchanged; contradicting `action_yield` is reported at load); dissection prints the `msg_dissect_*` snippets |
| MONDT-J06 | fixed | 2.0 | `tests/audit_m_m20b_test.cpp`. Overmap horde membership is the `HORDE_ROAMER` monster flag (with HEARS), a species flag of ZOMBIE and FERAL in vanilla and in the three mods that redefine them |
| MONDT-J07 | fixed | 2.0 | `tests/audit_m_m20b_test.cpp`. `special_when_hit` also takes an array of objects: several built-ins or EOCs (`is_ranged`, `bodypart`), each with `chance`, `melee`, `ranged`; RETURN_FIRE and SHOT_RICOCHET documented. The defenses' own numbers are still hardcoded |
| MONDT-J08 | partly fixed | 2.0 | `tests/audit_m_m20b_test.cpp`. `mount_rules`, a `shearing` object (`seasons`, `regrow`, `quality_level`) and `mech_activation_item` come from JSON; PAY_BOT's cash card and price are still hardcoded |
| MONDT-J09 | not changed | 2.0 | Already fixed by EVT-J04 (`e1dee8ff`): effect_type `apply_eocs`, `intensity_change_eocs` and `remove_eocs` run for monsters (`tests/audit_m_m03a_test.cpp`) |
| MONDT-J10 | fixed | 2.0 | `tests/audit_m_m20b_test.cpp`. damage_type `pain_divisor`, `ignites`, `electrocutes` and `mon_immune_materials` replace the hardcoded bash, acid, heat and electric rules and the steel and stone heat immunity; `status_chance_multiplier` is not in EOC math yet |
| MONDT-J11 | fixed | 2.0 | `tests/audit_m_m20c_test.cpp`. Monsters take `blood_field`/`gib_field` and species `gibs` (MOLLUSK's moved to data); materials take `creature_flesh`, `breathes` and `creature_flammability`, from which `Creature::cmat_*` are built (vanilla sets unchanged; `mi-go_flesh` opts out as before). The dead `get_meat_*` functions are gone |
| MONDT-J12 | partly fixed | 2.0 | `tests/audit_m_m20c_test.cpp`. New global `set_monfaction_attitude` (saved with the game, followed by factions based on it, `DEFAULT` undoes it) and `u_monster_type` mutator; faction get/set and `u_is_mtype` already existed. Left: condition-based anger/fear/placate triggers (needs a decision on when to evaluate them) |
| MONDT-J13 | partly fixed | 2.0 | `tests/audit_m_m20c_test.cpp`. `character_butchered_corpse` is sent for every outcome (quartering, fatal failures) with a `success` field and the corpse as beta; the empathy morale/trauma/NPC opinion penalties are EXTERNAL_OPTIONs; per-harvest time was already moddable through `butchery_requirements` keys (now documented). Left: per-harvest/per-entry EOCs, and the size time table stays in C++ |
| MONDT-J14 | fixed | 2.0 | `tests/audit_m_m20c_test.cpp`. `pet_interactions` (monster or species) add pet menu entries with a condition and an effect; petfood takes `chance`, `condition`, `on_tame_eocs`, `on_fail_eocs` and `bite_on_feed` (replacing the `mon_dog_thing` id check); new `monster_tamed`, `monster_milked`, `monster_sheared` and `character_mounts_monster` events |
| MUT-B06 | not changed | 2.0 | `tests/audit_mutations_bionics_sweep_test.cpp`. Already fixed (`6a14aab0`): the active/inactive flags read the owner's own trait state |
| MUT-B07 | not changed | 2.0 | `tests/audit_mutations_bionics_sweep_test.cpp`. Already fixed (`bffe9b52`): switching a trait the character lacks does nothing |
| MUT-B08 | not changed | 2.0 | `tests/audit_mutations_bionics_sweep_test.cpp`. Already fixed (`187efe53`): self-installed CBMs need slots and a body they suit |
| MUT-B09 | not changed | 2.0 | `tests/audit_mutations_bionics_sweep_test.cpp`. Already fixed (`c0da0afa`, `1a3b2248`, `e11c1867`): a fuel bionic burns one unit from one source per turn |
| MUT-B11 | not changed | 2.0 | `tests/audit_mutations_bionics_sweep_test.cpp`. Already fixed (`83e9c0b9`, `d2d2a988`): nurse bot removals act on the patient and do the usual cleanup |
| MUT-B12 | not changed | 2.0 | Already fixed (`28a8cdf5`): the mutation selector leaves out traits an installed CBM conflicts with (a menu; checked by reading) |
| MUT-J05 | fixed | 2.0 | `tests/audit_m_m17a_test.cpp`. Any bionic with `known_ma_styles` opens the style menu when switched on and drops its style when switched off (unless known otherwise or another active bionic has it). The new `learned_ma_styles` teaches styles on install and takes back only those it taught on removal; Xedra's Muay Thai datacore uses it and now teaches Muay Thai. CQB's skill substitution stays keyed to `bio_cqb` (MUT-J12) |
| MUT-J06 | partly fixed | 2.0 | `tests/audit_m_m17a_test.cpp`. `builtin_activation` (web, sludge trail, long pull, burrow, slime split, vomit, spores, blossoms, echolocation, tree communion), `blood_type` and `bodypart_hp_multiplier` replace the trait ids in C++, and the shipped traits name theirs; a slime split or tree communion that fails gives the cost back. EOC effects for pull, burrow and tree communion are not added |
| MUT-J07 | partly fixed | 2.0 | `tests/audit_m_m17a_test.cpp`. `gains_mutation` is also sent for evolutions and thresholds; `u_add_trait`/`u_lose_trait` send the mutation events with `"send_event": true` (off by default so haircuts and marker traits don't fail Pure Blood), and `u_add_bionic`/`u_lose_bionic` act as a successful operation with `"as_surgery": true`. New events `activates_bionic`, `deactivates_bionic`, `activates_mutation`, `deactivates_mutation`. No central send from `set_mutation` (character creation, NPC generation, transforms) |
| MUT-J08 | fixed | 2.0 | `tests/audit_m_m17b_test.cpp`. A restated trait or bionic EOC list, `enchantments`, `integrated_armor`, `attacks`, `armor` or key-value list (`vitamin_rates`, `lumination`, ...) replaces the parent's instead of appending, with `extend`/`delete`; inline enchantments are `INLINE_ENCH_<id>_<n>` counted across definitions (bionics used their name), and a duplicate enchantment id is a load error. MindOverMatter's resonance override no longer doubles ARTIFACT_RESONANCE, Xedra's commoner changelings drop the noble's mana enchantment and Strix talons are worn once |
| MUT-J09 | partly fixed | 2.0 | `tests/audit_m_m17b_test.cpp`. The surgery bonus is the `BIONIC_SURGERY_SKILL` enchantment value and the complications cap the `SURGICAL_TRAINING` flag, both on the MD and Autodoc Specialist traits in JSON (same +3/+7); a bionic's `faulty_pool` picks what its failed installation leaves. Left: an anesthetic multiplier (the amount is computed in six places) and per-mode skill weights |
| MUT-J10 | partly fixed | 2.0 | `tests/audit_m_m17b_test.cpp`. Categories take `bad_mutation_chance_scale`, `bad_mutation_chance_cap`, `threshold_breach_min_level` and `threshold_message` (defaults as before), and `MUT_OUT_OF_TREE_INSTABILITY_MULT` changes what an out-of-tree mutation counts. Left for the owner: crossing more than one threshold, a lower cap for Robust Genetics |
| MUT-J11 | partly fixed | 2.0 | `tests/audit_m_m17b_test.cpp`. A `costs` object gives each resource its own amount, bionic power included; the mutations menu uses `can_power_mutation` like triggers and EOCs (it now allows 50-80% kcal and sleepiness up to Exhausted). Left: upkeep keeps its looser thresholds (documented), bionics take no `costs` |
| MUT-J12 | partly fixed | 2.0 | `tests/audit_m_m17b_test.cpp`. A bionic copied from one with coded effects keeps them (except `bio_cqb`, MUT-J05), `auto_deactivates` works for every bionic, `processed_eocs` get `trigger_cost`, and Aftershock's dopamine stimulators run from JSON. Left: porting the other coded bionics to JSON and the id checks outside bionics.cpp |
| NPC-B05 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. The barehanded attack is rated by an average punch (strength, unarmed skill, martial arts) and counts as in hand when unarmed, so unarmed NPCs punch instead of standing undecided or "wielding" their fists. |
| NPC-B06 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. NPC casters, not spell-less NPCs, send `opens_spellbook` before rating spells, at most once a turn, and the level cap uses the caster instead of the avatar. |
| NPC-B07 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. A live explosive is thrown to the best tile in range (clear of the thrower's and allies' blast radius, towards enemies, clear line) instead of the north-west corner, and never put off to find a clearer line. |
| NPC-B08 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. The gun wield penalty applies to a gun not yet wielded (the base gun for gunmod modes), so NPCs stop swapping identical guns every turn and fire. |
| NPC-B09 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. `assess_danger` commits the threat map before its no-enemy return, so nearby fire counts and old threats fade. |
| NPC-B10 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. `find_item` keeps the most valuable item it sees (the nearest of equals) instead of the farthest acceptable one. |
| NPC-B11 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. npc_class copy-from keeps unrestated bonuses, can change copied spell levels and `delete` copied spells, bionics, skills and mutation rounds; ids are checked after loading, chance and work_hours range-checked, fields documented in NPCs.md. |
| NPC-B12 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. Camp food days use 3000 kcal a person over your followers and you (or the faction size, never 0), and "Worst vitamin" shows the worst one. |
| NPC-B13 | fixed | 2.0 | `tests/audit_m_m21a_test.cpp`. `avoid_friendly_fire` with no friends no longer averages an empty list (0/0), and prefers a tile with a clear line of fire. |
| NPC-B15 | fixed | 2.0 | `handle_hostile` skips a null friend pointer instead of dereferencing it, like the other friend loops; not testable without a desynced creature tracker. |
| NPC-B16 | fixed | 2.0 | `tests/audit_m_m21b_test.cpp`. `assess_danger()` sorts NPCs and monsters by `attitude_to()`: a bandit no longer counts or targets a monster neither side is hostile to, a FRIENDLY NPC is a friend, and the unused `neutral_guys` list is gone (the dead cache lookup went in `d2298830`) |
| NPC-J01-content | fixed | 2.0 | `tests/audit_m_fu_content_test.cpp`. NPCs switch on the stat enhancers and synaptic accelerator in fights (priorities 5 and 10), the heart, power-source CBMs and cerebral booster during upkeep, the blood factory while sleeping, and the taser and Aftershock's blade conduits before melee attacks; Aftershock's cold absorber in fights. |
| NPC-J06 | partly fixed | 2.0 | `tests/audit_m_m21b_test.cpp`. Spell `npc_use` (`role` attack/self/ally/never, `condition`, `priority`): NPCs cast buffs and summons on or next to themselves and heals or buffs on friends (the avatar too), and a condition gates attacks. Left: `NPC_ACTIVATE` items still rate a flat 1 in an emergency, and support spells are weighed only in combat |
| NPC-J07 | fixed | 2.0 | `tests/audit_m_m21b_test.cpp`. Comestible `npc_use` (`pain`, `min_pain`, `max_pain`, `preference`, `condition`) replaces the 7 painkiller ids (same ranges; heroin now counts from pain 50, with a syringe), the effect flag `PAINKILLER` replaces 6 effect ids, and NPCs take other medicine while its condition holds |
| NPC-J08 | fixed | 2.0 | `tests/audit_m_m21b_test.cpp`. Heal `cures` (USE-J08's entries, which can also be a bare effect id) removes any effect for the player and NPCs, and NPC triage treats them on itself, the avatar and allies. First-aid thresholds are `ai_tuning` `first_aid` (defaults keep the capped 75/80/80; the owner may raise head and torso to the intended 95/85) |
| NPC-J09 | partly fixed | 2.0 | `tests/audit_m_m21b_test.cpp`. Terrain drinking takes any endless source of a liquid flagged `NPC_DRINKABLE` (`water_clean` and `water` in vanilla) that the NPC can drink. Sips bring its water and nutrients, and each charge runs its consumption EOCs and consume events. Left for the owner: the liquid's contamination (raw water's 1% bad_food) |
| OMAP-B08 | partly fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. City rules see the cities of existing neighbouring overmaps, and an open-ended `city_distance` minimum is enforced whatever the city sizes. Special road connections still target the special's own-overmap city. |
| OMAP-J03 | fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. Forest biome components are rolled in `(sequence, id)` order (vanilla thick forests now roll water last). The never-implemented `clear_types` is removed from the docs. |
| OMAP-J04 | fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. City layout (empty lots, size bounds and variations, spacing cap, block width, street shrink, branch chance, megacity size and seeds) and special density (sector size, attempts) are region settings, defaulting to the old constants. |
| OMAP-J05 | fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. `check()` for region settings, forest biomes, map extra collections, terrain/furniture sets, dimensions and region layouts, run with the consistency checks. desert_region's unused collection naming a missing map extra is removed. |
| OMAP-J06 | partly fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. `target_params` accepts `search_range` for closest searches, `create_if_necessary`, `min_z`/`max_z` and a list of `om_terrain`; `exclude_seen` works; the replacement retry no longer leaks; `u_location_variable` runs `false_eocs` on failure. OMT flag matching (needs J12) is left. |
| OMAP-J07 | partly fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. A special's EOC condition is tested once per overmap batch, and the effect runs after placement with `special_id`, `special_pos`, `rotation`, `overmap_pos` and city context; docs moved to the special table. No `overmap_special_placed` event yet. |
| OMAP-J08 | fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. Radio stations come from an `overmap_terrain` `"radio"` field (vanilla towers, LMOE and FEMA moved to JSON), and the EOC effects `add_radio_station`/`remove_radio_station` exist. |
| OMAP-J09 | partly fixed | 2.0 | `tests/audit_m_m09a_test.cpp`. `region_settings` `worldgen_monsters` sets the city horde, swamp, river, ocean and subway groups and their numbers per region. Not a generic rule list; the `DISABLE_ANIMAL_CLASH` gate is left for the owner. |
| OMAP-J10 | partly fixed | 2.0 | `tests/audit_m_m09b_test.cpp`. Forest, thick forest and swamp, ocean and ravine terrain ids and the forest and floodplain noise shapes are region settings, defaulting to the old values (the noise is identical, tested). Left: river pieces and lake/ocean noise. |
| OMAP-J11 | fixed | 2.0 | `tests/audit_m_m09b_test.cpp`. overmap_connection subtypes and overmap_location terrains/flags take copy-from extend/delete, and special connections take `target` (`city` or `nearest_existing`) and `max_length`, with a per-connection `special_target` default. Vanilla is unchanged: one data line (`special_target` on `forest_trail`) would give the trail specials their trails (proposed). |
| OMAP-J12 | fixed | 2.0 | `tests/audit_m_m09b_test.cpp`. An overmap terrain flag that isn't an engine flag is kept as the mod's own; overmap location flags, the new `FLAG` match type (missions, EOC target_params, start locations) and the new `map_omt_has_flag` condition test both kinds. |
| OMAP-J13 | partly fixed | 2.0 | `tests/audit_m_m09b_test.cpp`. Bridges (`bridge` rule), manhole sewers (`below` rule), travel ramps (`RAMP` flag) and city manhole chance and terrain come from data; vanilla, aftershock and railroads data give the same terrain. Left: sewer/subway connector terrains and subway stations. |
| PROG-J01 | fixed | 2.0 | `tests/audit_m_m16a_test.cpp`. Professions and scenarios can name their default background with `default_backgrounds` (`""` for none), and SKIP_DEFAULT_BACKGROUND now works on professions (the churl no longer gets a driving license); the creator swaps default hobbies when the profession changes, NO_BONUS_ITEMS also works on scenarios, and LONE_START starts no longer keep room for an NPC (checked by reading). |
| PROG-J02 | fixed | 2.0 | `tests/audit_m_m16a_test.cpp`. Loading reports a proficiency's unknown category or prerequisite, a learnable one without `time_to_learn`, and prerequisites that require each other in a circle. |
| PROG-J03 | fixed | 2.0 | `tests/audit_m_m16a_test.cpp`. The reading rules check flags (CANNIBAL with the new book flag ENJOYED_BY_CANNIBALS, SPIRITUAL, LOVES_BOOKS, HATES_BOOKS, ILLITERATE everywhere, NO_BOOK_COMPLEXITY_PENALTY) that the traits carry, and the new enchantment value BOOK_FUN changes book fun. |
| PROG-J04 | fixed | 2.0 | `tests/audit_m_m16b_test.cpp`. Hobbies now run their EOCs and give pets, martial arts, cash and trauma; their forbidden_traits are enforced, and items, vehicle and no_bonus on a hobby are load errors. A profession or hobby with `run_eocs_for_npcs` runs its EOCs for NPCs too, and random (NC_NONE) NPCs now go through load_new_character |
| PROG-J05 | fixed | 2.0 | `tests/audit_m_m16b_test.cpp`. The scenario docs now match the loader (no items or cbms, `map_extra`, string name, flags, the default profession, vehicle, surround_groups, add/blacklist_professions). forced_traits take variants and profession trait variants are kept; unknown members in a scenario's extend or delete are load errors (three no-op `map_special` extends removed from MindOverMatter and Xedra_Evolved) |
| PROG-J06 | fixed | 2.0 | `tests/audit_m_m16b_test.cpp`. New math functions `skill_knowledge`, `skill_knowledge_exp`, `skill_rust`, `skill_rusty`, `skill_time_since_practice`, and `skill_exp(..., 'practice': true)`. `gains_skill_level` gets a `type` field (practical/theoretical) and is sent for theory that rises by itself; new `loses_skill_level` event from rust; training no longer sends the event twice |
| PROG-J07 | fixed | 2.0 | `tests/audit_m_m16b_test.cpp`. Copy-from scenarios change only the date parts they name; scenario surround_groups and profession pets, spells and EOCs replace when given and take extend/delete; trait_group is documented as merging and takes `"override": true`. Skills were already on generic_factory (DATA-J02) |
| PROG-J08 | partly fixed | 2.0 | `tests/audit_m_m16b_test.cpp`. Skills take `training_multiplier`, `catchup_multiplier`, `book_learning_multiplier` and `rust` { enabled, grace_period, multiplier, max_levels }. Left: skill-keyed enchantments, and moving the PRED/PACIFIST/SAVANT special cases into data |
| PROG-J09 | fixed | 2.0 | `tests/audit_m_m16b_test.cpp`. Proficiencies take `enchantments` and `flags`, applied while known; `bonuses` keys and types are validated at load; `get_proficiency_bonus` returns a float, so 0.5 counts (the archery strength requirement rounds to the nearest point) |
| REND-B03 | fixed | 2.0 | `tests/audit_m_m31a_test.cpp`. Layering field_variants draw the drawn field's sprite and only when its context lists it (other fields draw as usual), flag contexts work for fields, the roll is seeded by absolute position, and the loader drops zero/negative-weight sprites and empty variants (logged, no `% 0`); the FIELD ASCII fallback checks the id. Tiles-only test |
| REND-B04 | fixed | 2.0 | `tests/audit_m_m31a_test.cpp`. The idle-animation pick (`idle_animation_pick`) is unsigned, so no frame is skipped, and bg-only or one-sprite animated tiles (portraits too) no longer divide by zero; `"animated"` is documented in TILESET.md. Tiles-only test |
| REND-B05 | fixed | 2.0 | `tests/audit_m_m31a_test.cpp`. Items and traps are seeded by absolute position; the player, NPCs, their overlays and monster overlays by who they are (character id / monster persistent id, also on the overmap and mounts); monsters by persistent id, so a variant survives a reload. Tiles-only test |
| REND-B06 | fixed | 2.0 | `tests/audit_m_m31a_test.cpp`. The SDL3_mixer stopped callback only records the end (and wakes the input wait); `poll()`/`poll_music()` run musicFinished on the main thread unless a play/stop superseded it, and musicFinished keeps the track's volume. SDL_SOUND-only test |
| REND-B07 | fixed | 2.0 | `tests/audit_m_m31a_test.cpp`. A tileset's overlay_ordering is kept on the tileset and published only by the main (TILES) context, also on a cache hit; distant, portrait and overmap loads no longer clear it. Tiles-only test |
| SPAT-B11 | fixed | 2.0 | `tests/audit_m_m07a_test.cpp`. The overmap search estimates each OMT left at the travel profile's cheapest cost (`overmap_path_params::min_cost()`: 24 on foot as before, 3 by car), and a node found again more cheaply is queued again, so its children get the lower cost. |
| SPAT-J01-values | fixed | 2.0 | `tests/audit_m_fu_content_test.cpp`, `tests/audit_high_sound_test.cpp`. Vanilla walls, doors, windows and vehicle hulls muffle by material (rock/earth and reinforced concrete 20 ... glass 4, vehicle boards 6, windshields 3, open or broken 0), mostly on the abstract walls; the external option `SOUND_ATTENUATION_PERCENT` (default 100, 0 off) scales every sum, and JSON_INFO.md lists the values and how mods change them. |
| SPAT-J02 | fixed | 2.0 | `tests/audit_m_m07a_test.cpp`. Monster `path_settings` load `allow_unlock_doors`, `avoid_rough_terrain`, `climb_cost` and `size`, the path size defaults to the monster's (so large monsters stop planning through SMALL_PASSAGE), `max_length < max_dist` is a load error, and `bash_strength` is documented as an object. |
| SPAT-J03 | fixed | 2.0 | `tests/audit_m_m07a_test.cpp`. The pathfinding cache takes CLIMBABLE and SHARP from furniture too; new terrain/furniture `max_creature_size` (SMALL_PASSAGE counts as MEDIUM) sets the `Restrict*` bits, and monster, avatar, mount, drag and leap movement honour it through `map::fits_creature_size`. |
| SPAT-J04 | fixed | 2.0 | `tests/audit_m_m07a_test.cpp`. One anchor check for the teleporter and for whoever is in the way: DIMENSIONAL_ANCHOR from traits, bionics, effects, worn items and a new monster flag, TELEPORT_LOCK on any creature; a new `creature_teleported` event; `teleports_into_wall` is sent for NPCs too. |
| SPAT-J05 | partly fixed | 2.0 | `tests/audit_m_m07a_test.cpp`. New `character_hears_sound` event, a `cause` on `character_wakes_up` (noise, alarm, damage, rested, other) and math `u_noise_level()`; monster hearing EOCs and a sound's source creature are left. |
| SPAT-J06 | partly fixed | 2.0 | `tests/audit_m_m07a_test.cpp`. Items and monsters take a `light_color`, and `light_color` is documented everywhere it is read; light from what a character carries or is, mutation and enchantment colours, the hardcoded on-fire/haslight values and vehicle cone widths are left. |
| SPAT-J07 | partly fixed | 2.0 | `tests/audit_m_m07a_test.cpp`. Assignable math `scent(loc)`; per-tile scent types, scent emitters, NPC deposits and scent tuning are left (they need a save-format decision). |
| TERR-B03 | fixed | 2.0 | `tests/audit_m_m28a_test.cpp`. Digging stairs and mining up/down charge the extra kcal, thirst and sleepiness to characters without the underground traits; the troglodyte traits now make it cheaper, as intended. |
| TERR-B04 | fixed | 2.0 | `tests/audit_m_m28a_test.cpp`. Kegs and bathtubs count only their loose liquid (new `iexamine::keg_liquid`), so a container of liquid lying in one no longer makes it "empty" or lets filling it delete the stored liquid; pouring into a keg with other items in it works. |
| TERR-B05 | fixed | 2.0 | none: the autodoc runs behind a uilist. Uninstalling needs the same `anesthetic` requirement as installing (body weight × difficulty), checked before the operation with a list of what's missing, and uses it up. |
| TERR-B06 | fixed | 2.0 | none: placement runs behind choose_adjacent and uilist. Every component group is chosen before any is consumed, so cancelling a later choice keeps the earlier components. |
| TERR-B07 | fixed | 2.0 | `tests/audit_m_m28a_test.cpp`. A trunk's 2-3 logs are one plank-cutting activity (new optional saved `logs` field) for the builder, NPC or avatar, yielding each log's planks; a zone construction resumes after it. |
| TERR-B08 | fixed | 2.0 | `tests/audit_m_m28a_test.cpp`. `eocs` traps are removed before their EOCs run and only without UNCONSUMED, so persistent EOC traps work and a trap the EOC sets at `trap_location` stays. |
| TERR-B09 | fixed | 2.0 | `tests/audit_m_m28a_test.cpp`. Tripwire, goo, sinkhole, glow and pit act on the mount's actual rider (`mounted_player`), so an NPC's ride no longer moves, dismounts or irradiates the avatar. |
| TERR-B10 | fixed | 2.0 | none: the gas station runs behind a uilist. The liters bought are clamped to what the cards and the tank cover. |
| TERR-B11 | fixed | 2.0 | `tests/audit_m_m28a_test.cpp`. Vending machines charge the cash cards first and the bank only the rest (new `iexamine::vending_pay`); a price equal to the card balance is paid, and nothing is taken when the price can't be covered. |
| TERR-B12 | fixed | 2.0 | `tests/audit_m_m28a_test.cpp`. A construction copy-from inherits its parent's components, tools and qualities, with `extend` and `delete` as for recipes; an array `using` replaces the inherited one. |
| TERR-J03 | fixed | 2.0 | `tests/audit_m_m28b_test.cpp`. The EOC examine actor takes `condition`, `condition_fail_msg` and `npc_usable`; examine actions that can't be used are greyed out, and an NPC uses the first one it may use. `this` on terrain was already right |
| TERR-J04 | fixed | 2.0 | `tests/audit_m_m28b_test.cpp`, `tests/audit_m_m26a_test.cpp`. Card readers honour `"query": false` and take `hack_difficulty` and `hack_eocs`; the hack's changes are ACT-J06's. A game saved during a hack loads again |
| TERR-J05 | fixed | 2.0 | `tests/audit_m_m28b_test.cpp`. Any construction that places its component (`place_appliance`, `done_appliance`, `done_vehicle`) keeps it until then, whatever its category or id; simple deconstruction is `dark_craftable` in JSON |
| TERR-J06 | fixed | 2.0 | `tests/audit_m_m28b_test.cpp`. The trap action checks for `eocs` and `spell_data` work; a copy-from child's `eocs` replace its parent's, so the portal, chasm, LIXA and tutorial traps no longer run their parent's EOC first |
| TERR-J07 | partly fixed | 2.0 | `tests/audit_m_m28b_test.cpp`. Traps take `item_eocs` (items and sounds), disarm EOCs and conditions (the telepad's anchor rule is JSON), the `map_trap_id` condition and a `monster_triggers_trap` event. No `trap_visibility` math function; NPCs still can't disarm |
| TERR-J08 | partly fixed | 2.0 | `tests/audit_m_m28b_test.cpp`. The beartrap, board and caltrops actions read `trigger_damage`, `tetanus_chance`, `break_weight` and `self_disarm_chance` from JSON, with the old numbers as defaults; a bear trap set off by an item gives back its `drops`. The other actions are still hardcoded |
| TERR-J09 | partly fixed | 2.0 | `tests/audit_m_m28b_test.cpp`. Computer options and failures take EOCs inside the normal session, mapgen computer EOCs can be inline, and `"eocs_mode": "on_login"` keeps login, options and failures. No security/alerts math on the computer talker; lock and unlock still act around the player |
| UIHUD-B04 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. New `Character::weight_carried_percent()` (100 at zero capacity) for the carry_weight widget var, the sidebar text, the ledge jump and the move mode switch cost; the medical UI's overburdened penalty guards it as recalc_speed_bonus does. |
| UIHUD-B05 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. A one-symbol graph draws empty cells and a graph with no width draws nothing instead of dividing by zero; symbols are counted in code points. |
| UIHUD-B06 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. The sidebar overmap cache is keyed on the turn and height too, and dropped when the overmap UI closes, a game loads or an EOC writes a note. |
| UIHUD-B07 | fixed | 2.0 | none: the keybinding editor is interactive only. A declined edit no longer clears earlier changes, so the save prompt comes up. |
| UIHUD-B08 | fixed | 2.0 | none: the context is built inside the interactive overmap loop. The overmap registers and handles TOGGLE_OVERMAP_ONLY_TRAVEL (x) instead of the unbound TOGGLE_FAST_TRAVEL. |
| UIHUD-B09 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. A saved layout this world lacks falls back silently to the default, the other layouts are still read, and unloaded layouts' settings are written back. |
| UIHUD-B10 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. Keybinding saves write only actions that differ from the defaults; files saved before keep their old snapshot. |
| UIHUD-J01 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. Widget load check: style, fill, arrange, clause_mode, graph symbols, breaks and body graphs for all widgets; child ids, clause templates and graph width for the widgets a sidebar draws. Vanilla and all mods pass unchanged. |
| UIHUD-J02 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. parse_tags works in default_clause and sym, a widget-level parse_tags expands string, `<context_val:widget>` is the body part, and `_val` tags print whole numbers in full. No precision tag. |
| UIHUD-J03 | partly fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. New `sidebar_section` (sidebars or "*", after) adds a widget to sidebars as its own section. EOC panel/layout toggles are left for the owner (panel options are global settings). |
| UIHUD-J04 | partly fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. On top of OMAP-J01's note effects: `overwrite`, `only_if_text`, `map_has_note` and `map_note_danger()`. Left: note shapes, a `<map_note>` tag, special autonotes. |
| UIHUD-J05 | fixed | 2.0 | `tests/audit_m_m30a_test.cpp`. `clause_mode` all/first/threshold; copies delete, replace and add clauses and bodyparts by id; breaks can be changed alone. |
| UIHUD-J06 | fixed | 2.0 | `tests/audit_m_m30b_test.cpp`. Widget field "beta" (driven_vehicle, wielded_item, aim_target, mount, none) gives clause conditions and custom vars a beta talker, inherited by child widgets; while the target is missing the widget is empty. |
| UIHUD-J07 | partly fixed | 2.0 | `tests/audit_m_m30b_test.cpp`. Monster "safemode" (always/ignore), safe mode rules by "id:"/"species:", assignable math safe_mode() and event avatar_spots_hostile. Per-direction counts are left to monsters_nearby(); no "first_time" field. |
| UIHUD-J08 | partly fixed | 2.0 | `tests/audit_m_m30b_test.cpp`. Math sun_irradiance() and conditions u_/npc_in_sunlight; DAYFEAR shown on the @ screen; the CQB lock was already JSON (CMBT-J08). A stats-tab widget slot is left: the stats grid is a fixed 10 rows. |
| UIHUD-J09 | partly fixed | 2.0 | `tests/audit_m_m30b_test.cpp`. Widgets evaluate for any Character (the shown one is alpha), with UI slots follower_needs and consume_menu_needs that mods define. The built-in needs lines stay in C++. Widget `parse_tags` text now also uses the shown character and the beta (wired up at the merge with UIHUD-J02). |
| UIHUD-J10 | fixed | 2.0 | `tests/audit_m_m30b_test.cpp`. JSON type "keybinding": a main-game hotkey that runs an EOC (optional condition); its keys rank below the player's own and keys already in use are left out. |
| UIINV-B03 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. AIM's Worn→Inventory move charges what the take-off menu charges (obtain cost), and nothing when the item can't be taken off |
| UIINV-B04 | not changed | 2.0 | Already fixed by IINF-B02: removal and install both call `islot_gunmod::needs_unloaded()`, which compares the location id (and reads `requires_unloaded`) |
| UIINV-B05 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. AIM's unload in a container pane unloads the highlighted item (or else the pane's container) |
| UIINV-B06 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. `Character::unload` asks before taking what isn't the avatar's, whichever menu or key started it; the unload menu no longer asks on its own, and EOC `ACT_UNLOAD` refuses others' items |
| UIINV-B07 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. AIM's sort by category orders by `sort_rank` then name, as the inventory menu does |
| UIINV-B08 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. Highlighting a hidden collated copy and the Eat menu's "next copy" check that the group's header is shown instead of dereferencing null |
| UIINV-J01 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. `u_run_inv_eocs` and `u_map_run_item_eocs` menus take `hint`, `none_message`, `denial` (condition + message, npc = the item) and `columns` (title + value); `manual_mult` passes the amount marked as `_count` |
| UIINV-J02 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. Effect `u_open_crafting_menu` (`filter`, `recipe`), workbench `recipe_filter` (furniture and vehicle parts), math `can_craft('recipe', 'batch': n)`. Choosing a category tab from JSON is not supported |
| UIINV-J03 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. Recipe `condition` is CRAFT-J06's (with `condition_fail_message`; the two versions were merged into it); this adds `hide_if_condition_fails`, `knowledge_ratio` (default 0.8, one function for both sites) and the `NO_NPC_CRAFT` flag, which `npc_can_craft` now reads |
| UIINV-J04 | fixed | 2.0 | `tests/audit_m_m29a_test.cpp`. Skill `vision_penalty` (electronics 2, tailoring 1), `crafting_stat` and `crafting_stat_divisor`; one `has_uncorrected_farsight()` that also takes a FIX_FARSIGHT character flag, used by all 10 sites; the crafting panel reads the roll's own functions |
| UPSTREAM-89419 | fixed | `ef842574`, 2.0 | `tests/audit_m_fu_left_test.cpp`. Ported from CleverRaven/Cataclysm-DDA#89419: dragging a container onto a tile with items swaps the two tiles' items instead of losing them; the test checks every item on both tiles |
| USE-J05 | fixed | 2.0 | `tests/audit_m_m13a_test.cpp`. consume_drug durations follow the new `DRUG_DURATION` enchantment value, which Substance Tolerance and Lightweight now give in JSON with the same x0.8/x1.2. `stat_adjustments` that can't last or don't exist are load errors, field and effect ids are checked, and `field_count`, `field_radius`, `remove_effects` and `consume_time` are new. The hardcoded drugs and alcohol still check the traits. |
| USE-J06 | fixed | 2.0 | `tests/audit_m_m13a_test.cpp`. Washing's water and cleanser are now the `wash_water` and `wash_cleanser` requirements (same items and amounts; mods `extend` them). A new `wash` use action sets what it washes, its own requirements, the amounts per volume and the time, and the wash activity saves its requirements. |
| USE-J07 | fixed | 2.0 | `tests/audit_m_m13a_test.cpp`. `Character::invoke_item` sends the new `character_activates_item` event (character, itype, method, charges) for every use that isn't cancelled. NPC AI first aid and ticks don't send it. `activates_artifact` stays unsent; removing it or sending it for the `artifacts` category is the owner's call. |
| USE-J08 | fixed | 2.0 | `tests/audit_m_m13b_test.cpp`. Heal actions take `skill`, `proficiencies` (time_mult, bonus), `cures` (effect, chance, replace_with, messages) and `finish_eocs` (u healer, npc patient, `_bodypart`); head/torso power goes by limb type (vanilla's mutated `big_head` now heals as a head) and a broken modded part gets a message instead of a debugmsg. Vanilla defaults keep the old First Aid and wound care formulas; no `power_by_part` map. |
| USE-J09 | fixed | 2.0 | `tests/audit_m_m13b_test.cpp`. New `smoke` use action (`lit_item`, `burn_time`, `hunger`, `thirst`) lights modded smokables wherever SMOKING worked, and a lit item's optional `smoke` block (`puff_effect`, `puff_duration`, `puff_eocs`, `burn_out_eocs`) replaces the hardcoded cig/weed_high puff. Vanilla cigarettes, cigars and joints still use SMOKING. |
| USE-J10 | fixed | 2.0 | `tests/audit_m_m13b_test.cpp`. New assignable math `item_active()`, `item_countdown()`, `item_counter()`, `item_ammo('id')` (`item_countdown()` and `item_charges()` are ITEM-J04's, which came first: -1 without a countdown, and a stack kept at 1 or more); item `power` is battery charges only and setting it no longer replaces a lamp's oil; `charge_count()` works on item talkers (INV-J07's version: what the item holds). |
| USE-J11 | partly fixed | 2.0 | `tests/audit_m_m13b_test.cpp`. C++ use functions can take declared JSON parameters (`iuse_params`), first FISH_ROD `duration` and MAKEMOUND `moves`; the dead `iuse::purify_water` is removed. The effect-only drugs and toys still need migrating to data, and the unused registered functions were kept for third-party mods. |
| VEH-B03 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. Wheel terrain modifiers don't depend on entry order: an override beats penalties and the largest penalty applies (no stacking, as the shipped wheels were tuned). Only the road roller drum changes: rough ground is no longer better than dirt. Docs rewritten |
| VEH-B04 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. Reactors spawn with their own fuel_type (a gasoline generator was given plutonium cells and came empty); a fueled REACTOR without a positive `power` is reported at load and can't divide by zero. REACTOR fields documented; renaming `power` is left optional |
| VEH-B05 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. Braking while towing with no acceleration (engine off, poor traction) reaches the brakes instead of returning at "struggles to pull" |
| VEH-B06 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. Armor that is itself the random target of a hit takes it once, not twice |
| VEH-B07 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. The power-grid merge size check uses the far corners, so the 24-tile cap holds when growing up or left too |
| VEH-B08 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. A locked spawned vehicle's immobilizer (half of them) goes on its engines; it was set on the alarm part, which can't take it |
| VEH-B09 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. A summoned vehicle's timer runs down once per turn (in `map::vehmove`), not once per tile moved; split-off pieces keep magic, the timer, the lock, effects and values |
| VEH-B10 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. Handling difficulty uses speed, so reversing is as hard as driving forward (it was negative) |
| VEH-B11 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. A vehicle with nobody at the controls (autopilot, following, patrolling) trains nobody and takes no pedal power from the player |
| VEH-B12 | fixed | 2.0 | `tests/audit_m_m23a_test.cpp`. An engine left on with an empty fuel uses none and builds no debt; any carried debt (old saves, connected batteries) is capped at one turn and one fuel unit |
| VEH-B13 | fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. Acceleration keeps watts per kilogram fractional (`vehicle::accel_from_power_ratio`) and truncates only the result; under 1 W/kg no longer reads as too heavy. Vehicles accelerate slightly more (heavy ones most); the vehicle_efficiency_test numbers of the ambulance, car, fire truck, humvee, motorcycle, quad bike, road roller, SUV and SWAT truck were regenerated |
| VEH-B14 | fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. Every engine makes its `noise_factor` noise and only combustion smokes; a coasting vehicle with its engines off makes its speed noise. Electric, muscle, animal and wind vehicles are no longer silent |
| VEH-B15 | fixed | 2.0 | `tests/audit_m_m23c_test.cpp`. `weight_on_wheels()` no longer applies TOTAL_WEIGHT on top of `total_mass()` (done with VEH-J09) |
| VEH-J02 | fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. Rail direction comes from the new terrain flags `RAIL_DIAGONAL` / `RAIL_STRAIGHT` (vanilla tracks tagged, the small and recessed ones included); a diagonal course counts its own wrong tiles (1 allowed, was 2) |
| VEH-J03 | fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. New events `vehicle_collides`, `vehicle_part_destroyed`, `vehicle_engine_stalls`; `fuel_tank_explodes` has the vehicle talker and a `fuel` field. Collision, damage and tank events are queued and sent once the vehicles have moved, so their EOCs can change the vehicle safely. Vpart EOC hooks left to VINT-J06 |
| VEH-J04 | partly fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. Fault fields `engine_power_mult`, `fuel_consumption_mult`, `exhaust_mult`, `steering_mult` (legacy flags count as 0.6 and 2); steering reads the wheel on a steering mount (tyre faults never hurt steering before). Left: alternator and backfire multipliers |
| VEH-J05 | fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. The wheel fault chain is fault data (`runover`, `while_moving`), with vanilla moved over unchanged; trap `vehicle_data` takes `runover_faults`, `wheel_faults` and `eocs` |
| VEH-J06 | fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. Vpart `collision_damage` (default cut for SHARP, bash otherwise) is dealt through `deal_damage` to characters (all main parts, as before) and monsters, so armor, immunities and ondamage_eocs apply. Characters hit by SHARP parts take cut now |
| VEH-J07 | partly fixed | 2.0 | `tests/audit_m_m23b_test.cpp`. `bonus` docs fixed; typed `funnel_radius`, `muffle_factor`, `seatbelt_strength`, `shock_absorption` with `bonus` as fallback. Left: light, horn, recharger, rotor and mirror still on `bonus`; the muffler's menu label shows the % left |
| VEH-J08 | fixed | 2.0 | `tests/audit_m_m23c_test.cpp`. `control_requirements` has a `water` key and per-mode `handling_skill`, `handling_proficiency`, `trains` and (air) `maintenance_proficiency`. Steering, skid recovery, driving practice and the aircraft-mechanic gate read them, falling back to driving, prof_driver/prof_boat_pilot and prof_aircraft_mechanic |
| VEH-J09 | partly fixed | 2.0 | `tests/audit_m_m23c_test.cpp`. New vehicle enchantment values VEH_ENGINE_POWER, VEH_MAX_VELOCITY, VEH_ACCELERATION, VEH_AIR_DRAG, VEH_ROLLING_RESISTANCE, VEH_WATER_DRAG, VEH_TRACTION, VEH_PART_DAMAGE_TAKEN, VEH_COLLISION_DAMAGE, VEH_RENEWABLE_OUTPUT and VEH_EPOWER_DRAIN; FUEL_USAGE now also in `basic_consumption()`. Not done: a noise value (after VEH-B14) and FUEL_USAGE in the reactor fuel draw (after VEH-B04) |
| VEH-J10 | fixed | 2.0 | `tests/audit_m_m23c_test.cpp`. Vehicle prototypes take `variables` and `spawn_eocs` (run with the vehicle as alpha when it is placed); chat topics were done by VINT-J01. No `tags` field: vehicle tags are internal markers, and variables cover it |
| VEH-J11 | partly fixed | 2.0 | `tests/audit_m_m23c_test.cpp`. Engines take `power_math` (alpha the vehicle, beta the rider or animal on its tile), `full_reverse` and `stall_message`, defaulting from the old fuel ids. Not done: the muscle-fuel, stamina, spawn, sound and message special cases (a `power_source` descriptor) |
| VINT-B03 | fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Autopilot Follow and Patrol start a stopped vehicle's engines through the player's start activity (new `vehicle::enable_follow`) |
| VINT-B04 | fixed | 2.0 | none: the Netherum boiler only runs in its dimension, which tests can't set. It erases its contents through the iterator |
| VINT-B05 | fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Replacing a broken part keeps the NO_INSTALL_PLAYER / NO_MODIFY_VEHICLE install gates (`veh_utils::install_forbidden_reason`); NPCs no longer repair NO_MODIFY_VEHICLE vehicles; the flag is registered |
| VINT-B06 | fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Install, repair and removal XP come from that action's own skills and time (repair time x levels; removal at half rate), so repair-only skills train and short repairs pay less |
| VINT-B07 | fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Inline vpart enchantments are `INLINE_ENCH_vpart_<id>_<n>`; a part's list replaces the copied one, with `extend` and `delete` |
| VINT-B08 | partly fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Prototype copies append items and zones (vanilla cars get their car kits back), delete parts and zones, `pseudo_tools: []` clears. Left: per-key requirement inheritance and the empty-repair check (needs hand_paddles data) |
| VINT-B09 | partly fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Empty groups spawn nothing and fail the load, spawn fuel/status optional and checked, no groups for abstract prototypes. Left: nested groups, placement/spawn delete |
| VINT-J05 | fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Reaper uses GROWTH_HARVEST, plant_data base, NO_MACHINE_HARVEST and a `reaper` yields field; planter sows by required_terrain_flag (greenhouses, planter boxes) and keeps seeds it can't sow |
| VINT-J06 | partly fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Events for part install, removal, repair, toggle, engine start and stop, and appliance placement, with the vehicle as a talker (with VEH-J03's). Left: vpart `eocs` hooks, hotwiring |
| VINT-J07 | not changed | 2.0 | Done as VEH-J07 (m23b). Its bonus docs gained the beeper, camera and reaper meanings |
| VINT-J08 | partly fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. `processing_cycle` (MAP-J03) takes `item_condition` / `item_condition_message`, enough to write the washer, dishwasher and autoclave in JSON. Left: converting those three |
| VINT-J09 | fixed | 2.0 | `tests/audit_m_m24a_test.cpp`. Any repair tool a vehicle provides is found on the vehicle by the repair, which lends it any ammo through `prepare_tool`; no dummy-location error |

**Behaviour changes.** Vanilla walls, doors, windows and vehicle hulls now muffle sound by material (rock and earth 20, so monsters and NPCs hear less through the ground and through closed buildings); the external option `SOUND_ATTENUATION_PERCENT` scales it, and 0 turns it off. NPCs switch on more of their CBMs. Field dressing no longer yields bionics or mutagen-type drops, which now come from full butchery and dissection only. The field and expansion surveys use survival. `open_dialogue` on a talker without chat topics gives a debug message instead of crashing.

**Behaviour changes (second merge).** A monster's death EOC now runs once its corpse exists (the death message and blood are already there), and a new game runs the start OMT's `entry_eoc`. NPCs get wet in the rain and are dazzled by strong sun like the avatar. Large monsters plan routes around small passages (open windows, window frames), climbing monsters route over climbable furniture, and the monsters that avoid sharp terrain avoid wreckage; car and direct-travel overmap routes are cheaper and their searches wider. Monsters that copy another and add `path_settings` (zombie masters, necromancers, summoners and predators, skeleton masters and necros, the lab security devourer, the Balthazar secubot) now plan routes at all: they had a `max_length` of 0. `monsters_nearby()` and friends no longer count hallucinations, and monster pain and HP assignments now take effect. Monsters that fields, rotting food, births and runtime update-mapgen queue appear the same or next turn instead of at the next map shift, Xedra's count-3 changeling field spawns 3, and the reality bubble freezes and thaws water at midnight. The mi-go scout tower's scouts each roll their own patrol route. Typo'd ids in dialogue math and talk topics now point where their authors meant: the exodii merchant's speech branches use speech, Xedra's blood-hunger addiction and paraclesian migration EOCs fire, Xedra's werewolf, shapeshifter and lilin heals work, and Aftershock's migo_2 and radiation_1 story lines are no longer overwritten; four dialogue options that led to missing topics are gone. `broken_bone` reaches EVENT EOCs for NPCs too (MindOverMatter and Aftershock end an NPC psion's powers when its limb breaks), `u_var_changed` is only sent for the avatar's own variables, a finished construction runs ACT_BUILD's completion hooks, and Aftershock's eyebot photographs log "Became wanted by the police!". `defense_mode_fortified` stays hidden with MindOverMatterNoKnacks or classic_zombies, a blacklist loaded after a total conversion's whitelist no longer wipes it, and a `var_migration` without `to` erases the variable.

**Behaviour changes (third merge).** **Butchery yields now follow survival (MONDT-B16):** at survival 0 with a good knife a character gets about 0.29 of an entry's maximum instead of 0.79 (unchanged from survival 10); the weights are the external options `BUTCHERY_YIELD_SKILL_WEIGHT`, `BUTCHERY_YIELD_TOOL_WEIGHT` and `BUTCHERY_YIELD_DEX_WEIGHT` (0.5, 0.3, 0.2). Butchery takes longer without the proficiencies, bones come from field-dressed corpses, and empathy costs and statistics now cover quartering and failures. The CQB bionic is a skill floor everywhere, melee included; EMP blasts hit NPCs; monster blast damage goes through armor and enchantments; padded clothing protects against stabs. Unarmed NPCs punch, NPCs no longer swap guns, hostile NPCs ignore wildlife, and NPCs treat the effects their healing items cure. Zombies resist the venoms, monsters with infrared or true sight no longer see through walls, and characters behind cover are harder to spot. Evolutions count as gained mutations (memorial, scores, `gains_mutation` EOCs). Aqueous Repose and Embrace of the Waters work only in water. `u_spawn_item` with a container gives `count` copies of a non-stacking item. Camp crafts and upgrades train the recipe's skills. Nested mapgen parameters now reach the top mapgen (MGEN-B09), and about 80 vanilla overmap specials keep their minimum city distance. The fully slimed hands use `TECH_SLIME_PUNCH` instead of an undefined technique.

**Behaviour changes (fourth merge).** Vehicles accelerate a little more (heavy ones most; VEH-B13), and electric, muscle, animal and wind vehicles make engine and speed noise (VEH-B14). Punctured front tyres now hurt steering, and characters rammed by sharp parts (blades, spiked plating) take cut damage (VEH-J04, VEH-J06). About 20 vanilla cars spawn their parents' car kits again (VINT-B08), and vehicle removal and repair XP follow the action's own skills and time (VINT-B06). Spells with only `min_damage` now deal it, so the Wolfsbane pike, two Xedra weapons, MoM Force Shove and the Gracken arms start working (MAGIC-B05); RANDOM_TARGET spells cast at the caster's tile pick a target, so the quadraphract smoke mortar lands up to 40 tiles away (MAGIC-B15); mana pools under 320 refill in 8 hours (MAGIC-B10). Autodoc uninstalls need the same anesthetic as installs (TERR-B05), and a trunk is cut into planks as 2-3 logs over 40-60 minutes (TERR-B07). Hacked card readers do what their card does, so a few radii and doors differ (ACT-J06). The robofac Hub now refuses five book types (CAMP-J12). Dusters disassemble in 50 minutes and about 9 non-paper books no longer give paper (CRAFT-B03, CRAFT-J03). Taking worn gear off in AIM costs time, and unloading someone else's item asks first (UIINV-B03, UIINV-B06).

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

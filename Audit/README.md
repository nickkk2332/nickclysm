# nickclysm engine audit

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
| EVT-B01 | partial | `90c08ef9` | none: `is_game_over()` is private and reached only through the input loop. Still open: `character_dies` can fire twice when a follower death-switch takes over the body |
| USE-B04 | fixed | `0706a779` | `tests/audit_item_use_test.cpp` |
| CHAR-B02 | fixed | `7ce602d6` | `tests/audit_frostbite_move_mode_test.cpp` |
| CHAR-B03 | fixed | `7ce602d6` | `tests/audit_frostbite_move_mode_test.cpp` |
| NPC-B01 | partial | `1966b6c1` | `tests/audit_npc_attack_gun_test.cpp`. Still open: NPCs cannot use wielded CBM pseudo-guns |
| IINF-B01 | fixed | `1129cda8` | `tests/audit_ablative_armor_test.cpp` |
| CRAFT-B01 | fixed | `04e8cea9` | `tests/audit_craft_failure_test.cpp` |
| LOOP-B01 | fixed | `90c08ef9` | `tests/audit_game_loop_test.cpp` |
| MONDT-B02 | fixed | `ac239624` | `tests/audit_mongroup_evolution_test.cpp` |
| MONAI-B01 | fixed | `5bc22c4b` | `tests/audit_monster_speed_mod_test.cpp` |

**Group 4 (performance).** Seven of the eight quick wins are in; MAP-P01 was reverted because it broke a wakeup test and gained little. Before-and-after numbers are in [benchmarks/README.md](benchmarks/README.md).

**Group 5 (modding gaps).** All six are in. EOC-J01 and OMAP-J01 cover the core of each finding; what is left is listed in each row.

| ID | Status | Commit | Regression test |
|---|---|---|---|
| EOC-B02 | fixed | `98907571`, `2266c297` | `tests/audit_string_conditions_test.cpp`. The new check found seven broken conditions in vanilla dialogue (refugee center, Cranberry Foster, a common NPC line), two wrong names in the docs, and one condition in Xedra_Evolved; all fixed. `u_has_active_trait` was added so the Xedra one could be written correctly |
| INV-J01 | fixed | `827993a0` | `tests/audit_pocket_keys_test.cpp`. Fixed 12 clothing pockets with `max_length`, plus pockets in hobo_stove, an Xedra tool and six monster guns. Bandoliers, pouches and quivers now get their `volume_encumber_modifier`, so they encumber less |
| ACT-J01 | fixed | `cbbf71f0` | `tests/audit_activity_type_test.cpp`. The new EOC check found a misspelled `completion_eoc` in Limb_WIP |
| MAGIC-J01 | fixed | `c9fdb7d1` | `tests/audit_spell_context_test.cpp`. Also fixes MAGIC-B19 (vehicle check on the wrong tile) |
| EOC-J01 | partial | `3ce1e2f5` | `tests/audit_spawned_eocs_test.cpp`. Adds `spawned_eocs`, `npc_variables` and `item_variables`. Still open: a stable handle to a monster, item or vehicle in an EOC variable |
| OMAP-J01 | partial | `917f2c3c` | `tests/audit_overmap_eoc_test.cpp`. Adds `set_overmap_terrain`, `place_overmap_special`, `add_overmap_note`, `delete_overmap_note`, `reveal_map` levels, `map_in_overmap_special` and `map_omt_seen`. Still open: overmap hordes, `closest_city` filters, overmap math functions |

**Remaining small high-severity bugs.** All 48 small-effort critical and high bugs left after "Fix first" are in: 44 fixed, 4 partial. Agents fixed them in nine batches, each in its own worktree. A separate reviewer read each batch's diff, and review comments were applied before merging (10 of 48 needed changes). The batches were made against `master`, so they were cherry-picked onto this branch, with two conflicts resolved by hand: NPC-B02 with NPC-B01 in `npc_attack.cpp`, and LOOP-B02 with LOOP-P01 in `weather.cpp`. Running the new tests found two more real bugs, fixed here, and two test mistakes.

| ID | Status | Commits | Regression test |
|---|---|---|---|
| EOC-B03 | fixed | `3edccab5` | `tests/audit_eoc_math_test.cpp` |
| EOC-B04 | fixed | `8e551898` | `tests/audit_eoc_math_test.cpp` |
| MATH-B02 | fixed | `8e063988` | `tests/audit_eoc_math_test.cpp` |
| PROG-B01 | fixed | `43d8122f` | `tests/audit_progression_test.cpp` |
| PROG-B02 | fixed | `811496ae` | `tests/audit_progression_test.cpp` |
| PROG-B03 | partial | `2e3e06e6` | `tests/audit_progression_test.cpp`. Still open: NPC professions and hobbies still come from different scenario sources |
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
| CMBT-B02 | partial | `097c8831` | `tests/audit_character_mutation_combat_test.cpp`. Still open: on_damage still checks damage before armor |
| CMBT-B03 | fixed | `0631addd` | `tests/audit_character_mutation_combat_test.cpp` |
| MONAI-B02 | fixed | `1c6d9845`, `c2c461f7` | `tests/audit_monsters_test.cpp` |
| MONAI-B03 | fixed | `bd329db5`, `128718b5` | `tests/audit_monsters_test.cpp`. The test then showed absorb_meat never used the meat up (a range-0 flood fill returned no tiles); fixed in `128718b5` |
| MONDT-B01 | fixed | `d9408762` | `tests/audit_monsters_test.cpp` |
| MONDT-B03 | fixed | `9266be1f` | `tests/audit_monsters_test.cpp` |
| MONDT-B04 | partial | `956ea6d4` | `tests/audit_monsters_test.cpp`. Still open: signals still visit every horde bucket (performance only) |
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
| ACT-B02 | partial | `d996679a` | `tests/audit_vehicles_activities_test.cpp`. Still open: repair_part's clear-path check is not mirrored in the zone check |
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
- Zone vehicle repair takes time per damage level and fetches enough materials (ACT-B02).
- Camp missions charge food once, about half what they did. Camp fuel no longer multiplies each time the board opens, planting takes the right seeds once, and leftover seeds come back once (CAMP-B01 to B04).
- New characters get every proficiency their profession or hobby lists (PROG-B02). Random NPCs no longer get player-only professions such as psionic knacks (PROG-B03).
- In math, `u_addiction_turns(...) =`, `u_pain('type': 'perceived') +=` and the stat bonus setters now set or add as written. Sleeping pill withdrawal is prolonged instead of shortened (MATH-B02).
- Per-character safemode rules are saved (UIHUD-B01).

**Second round: the remaining high-severity small and medium findings.** 18 findings: 13 fixed, 5 partial, made the same way (agent batches, separate review, central build and test). The performance numbers are in [benchmarks/README.md](benchmarks/README.md#round-3-remaining-high-severity-performance-findings). Integrating them needed one test fix (a new active item has to be registered before an NPC can see it) and a fix to the benchmark runner, which had been dropping results that followed a log line with no newline.

| ID | Status | Commits | Regression test or benchmark |
|---|---|---|---|
| CHAR-B04 | fixed | `a5a4b6e1` | `tests/audit_character_effects_test.cpp` |
| HLTH-J01 | fixed | `291add8b` | `tests/audit_character_effects_test.cpp` |
| CHAR-J01 | partial | `30c665af` | `tests/audit_character_effects_test.cpp`. Still open: `u_add_body_part` / `u_lose_body_part` EOC effects |
| MUT-B05 | partial | `a9089239`, `846840db` | `tests/audit_mutation_towards_test.cpp`. Still open: filtering mutation candidates with unmet thresholds (would change mutation pacing) |
| MUT-J01 | partial | `b65fd9a9` | `tests/audit_mutation_towards_test.cpp`. Still open: validating every trait's category at load (core Mycus traits and mods use undefined categories) |
| EOC-B05 | fixed | `2f8d333f`, `0aad6359` | `tests/audit_items_npc_loading_test.cpp` |
| UIINV-B02 | partial | `edcb168d` | none: the move goes through a uilist. Still open: routing the move through the AIM insert activity |
| ACT-B03 | fixed | `4d5bffa7`, `9c9df48a` | `tests/audit_items_npc_loading_test.cpp` |
| NPC-B04 | fixed | `67a6f426`, `4f9a1228` | `tests/audit_items_npc_loading_test.cpp` |
| DATA-B03 | fixed | `4862b868` | `tests/audit_items_npc_loading_test.cpp` |
| MAP-P02 | fixed | `56f3c41f`, `13d01ac1` | benchmark `temp_map_load` |
| VEH-P01 | fixed | `45efaeba`, `5d8a43e4` | benchmark `vehicle_collision_check` |
| CRAFT-P01 | fixed | `6fdaa74c`, `9805272f` | benchmark `craft_unattended_wait`. A tool or heat source lost during a waiting step is noticed at the next one-minute poll, as with other steps |
| ACT-P02 | fixed | `81616d1d`, `6917bd82`, `4ad151f9` | `tests/audit_perf_world_activities_test.cpp`; benchmark `farm_scan` can't show it |
| TERR-P01 | fixed | `a0fce99f`, `1d0f11f6` | benchmark `blueprint_build_reqs` |
| MAGIC-P01 | partial | `d3f7852b`, `7738db12` | `tests/audit_perf_ui_test.cpp`; benchmark `spellbook_damage_preview`. Still open: one shared fake caster for casterless spell casts |
| UIHUD-P01 | fixed | `5521c271` | none: needs a display |
| REND-P01 | fixed | `32187d3e` | none: tiles renderer; compile-checked with `-DTILES` |

**Gameplay changes from the second round:**

- NPCs feel their effects, pain, hunger and thirst like the player: drunk, poisoned or starving NPCs are weaker, and effect dodge and to-hit modifiers now apply to everyone (CHAR-B04, HLTH-J01).
- NPCs notice live grenades, dynamite and C4 near them and get away (NPC-B04).
- A failed mutation no longer strips the traits it would have replaced, and starting traits survive an unaffordable mutation (MUT-B05).
- Body parts from an effect or worn item (e.g. Xedra's Wings of Gossamer) go away when it ends (CHAR-J01).
- Missions that take items (GodCo, Isherwood, robofac and others) take exactly what they need, including part of a stack (EOC-B05).
- Zone sorting no longer carries off your own matching ammo, nails or pills (ACT-B03).
- Inventory drag-and-drop refuses moves that used to crash or duplicate (UIINV-B02).
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
| SPAT-B10 | partial | `0d52b78c` | `tests/audit_spatial_sweep_test.cpp`. Still open: an optional `scent_neutralization_radius`, and a load warning for scent-neutralizing fields that do not spread |
| MGEN-B02 | fixed | `efa28db1`, `f99a8cbf` | `tests/audit_mapgen_test.cpp` |
| MGEN-B03 | partial | `2bfdf130`, `f24378f3` | `tests/audit_mapgen_test.cpp`. Still open: validating `remove_npcs` classes (the robofac intercom map removes NPC classes that no longer exist on purpose, as a save migration) |
| MGEN-B04 | fixed | `30fae9f2` | `tests/audit_mapgen_test.cpp` |
| MGEN-B05 | fixed | `9ff01839`, `359d63d0`, `737dcfd6` | `tests/audit_mapgen_test.cpp` |
| MGEN-B06 | partial | `d34e62ee` | `tests/audit_mapgen_test.cpp`. Still open: the consistency check between variable-weight mapgens and their predecessors (`road_nesw_manhole` needs its own data fix first) |
| MGEN-B07 | fixed | `38d80875`, `6755a3ea` | `tests/audit_mapgen_test.cpp` |
| OMAP-B01 | partial | `5322035b` | `tests/audit_overmap_worldgen_test.cpp`. Still open: the non-root pieces of mutable specials still ignore "must be unexplored" |
| OMAP-B02 | partial | `e1f926a7` | `tests/audit_overmap_worldgen_test.cpp`. Still open: generating overmaps to find a mission target that does not exist yet (too costly) |
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
| IINF-B05 | partial | `459fe6a2` | `tests/audit_items_sweep_test.cpp`. Still open: filtering the summed `ammo_remaining()` by ammo type |
| HLTH-B02 | fixed | `dd7947b4` | `tests/audit_health_test.cpp` |
| HLTH-B03 | fixed | `69cc6806` | `tests/audit_health_test.cpp` |
| HLTH-B04 | fixed | `56649dbb` | `tests/audit_health_test.cpp` |
| HLTH-B05 | partial | `7dbca179`, `87629287` | `tests/audit_health_test.cpp`. Still open: clamping a defined chance to at least 1 (vanilla effects use negative chances to mean "never") |
| HLTH-B06 | fixed | `a6bbbcaa` | `tests/audit_health_test.cpp` |
| HLTH-B07 | fixed | `f83a3649` | `tests/audit_health_test.cpp` |
| HLTH-B08 | fixed | `99a20b73`, `f8b2a332`, `cd38b39a` | `tests/audit_health_test.cpp` |
| HLTH-B09 | fixed | `867d4cb1`, `f8b2a332` | `tests/audit_health_test.cpp` |
| HLTH-B10 | fixed | `474baa01`, `37f22ce8` | `tests/audit_health_test.cpp` |

Modding features:

| ID | Status | Commits | Regression test |
|---|---|---|---|
| ACT-J02 | partial | `06f6c4a4`, `13973129` | `tests/audit_activities_crafting_test.cpp`, `tests/eoc_errands_activity_test.cpp`. Activities with a target: reading, butchery, disassembly, crafting, wearing, unloading and reloading. Still open: the other activities that need a target |
| ACT-J03 | partial | `2d596bc0`, `b53594ec` | `tests/audit_activities_crafting_test.cpp`. Still open: an "outcome" field on the event, and a context object on `u_assign_activity` |
| CRAFT-J02 | partial | `e7222c8a` | `tests/audit_activities_crafting_test.cpp`. Still open: recipe start and fail hooks, and events for crafting, disassembly and recipe learning |
| TERR-J01 | partial | `33e6f99a`, `f93e4745` | `tests/audit_construction_recipe_camp_test.cpp`. Still open: moving the hardcoded vanilla `post_special` functions to JSON |
| CRAFT-J01 | partial | `b33ebcb7` | `tests/audit_construction_recipe_camp_test.cpp`. Still open: inheriting inline requirements and extending or deleting component, tool and quality groups |
| CAMP-J01 | fixed | `bda202de` | `tests/audit_construction_recipe_camp_test.cpp` |
| MUT-J02 | partial | `baea3c27` | `tests/audit_mutation_bionic_activation_test.cpp`. Still open: a refund channel for cancelled activations, and a menu pre-check before the activation message |
| MUT-J03 | partial | `eceb79dd`, `e3212448` | `tests/audit_mutation_bionic_activation_test.cpp`. Still open: bionic math functions, an ignore-cost option, and access to per-bionic state |
| NPC-J01 | partial | `90eb6d45` | `tests/audit_mutation_bionic_activation_test.cpp`. Still open: giving other toggled bionics an `npc_use` (a content decision), priorities, and NPC use of spell bionics |
| CMBT-J01 | partial | `7dfdc8e4` | `tests/audit_martial_art_events_test.cpp`. Still open: get-hit hooks for projectile and explosion hits, and context values for pre-hit technique EOCs |
| HLTH-J03 | partial | `79ed5939` | `tests/audit_diet_rules_test.cpp`. Still open: moving vanilla diet traits (vegan, gourmand, picky eater, ruminant and others) to these fields |
| MAGIC-J02 | partial | `5c4fb57a` | `tests/audit_spell_enchant_test.cpp`. Still open: fail chance, difficulty, spell XP and sound values |
| INV-J03 | fixed | `35ca98d9` | `tests/audit_eoc_inventory_vehicle_test.cpp` |
| VEH-J01 | partial | `ae836b32`, `398d4f09`, `673b3770` | `tests/audit_eoc_inventory_vehicle_test.cpp`. Still open: damaging vehicle parts from an EOC (it can split or delete the vehicle mid-EOC), owner, and weight (VINT-J04) |
| VINT-J02 | fixed | `f28f99e1`, `743f2e40` | `tests/audit_eoc_inventory_vehicle_test.cpp` |
| LOOP-J01 | fixed | `a17a8639`, `ccfba742`, `374ce76a` | `tests/audit_weather_eoc_test.cpp` |
| MAP-J01 | partial | `210feefe`, `85598110`, `f4d64d6b` | `tests/audit_bash_eoc_test.cpp`. Still open: a general "tile destroyed" event and a cause value for bashes with no creature (explosions, fire, vehicles) |
| MONAI-J01 | partial | `e08fb87c`, `54d15f8c`, `530d442b` | `tests/audit_monster_eoc_test.cpp`. Still open: forcing a monster's target or destination (the monster AI replaces both every turn) |
| INV-J02 | fixed | `ca101f02`, `0dcb820a` | `tests/audit_mod_overrides_test.cpp` |
| HLTH-J02 | partial | `c47dbf59`, `58e3fada` | `tests/audit_mod_overrides_test.cpp`. Still open: validating resist, remove and block lists in effects, because shipped data still has stale ids there |
| NPC-J02 | partial | `92f0de07` | `tests/audit_mod_overrides_test.cpp`. Still open: moving NPC templates and factions onto `generic_factory`, and `extend`/`delete` on their lists |
| MONDT-J01 | partial | `eb828cdb` | `tests/audit_mod_validation_test.cpp`. Still open: a location context value, and spawn data such as effects, variables and factions on group entries |
| MAGIC-J03 | partial | `9c38b68a`, `e15db0f0`, `12c7974f` | `tests/audit_mod_validation_test.cpp`. Still open: enchantment and terrain-transform consistency checks (MAGIC-J06) |
| USE-J01 | partial | `9ee18e2d`, `4cbf0b89` | `tests/audit_mod_validation_test.cpp`. Still open: `_method` and `_item_pos` context values (they need USE-J02, several use actions per item) |

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
- **Activities with a target** (ACT-J02). `u_assign_activity` takes `target_item`, `target_loc`, `recipe` and `butcher_type` for reading, butchery, disassembly, crafting (new or in progress), wearing, unloading and reloading. `true_eocs`/`false_eocs` now work with every activity.
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
| ACT-J02 | partial | see the third-round table above | `tests/eoc_errands_activity_test.cpp` |

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
| INV-P02 | partial | `445dec4e`, `380292c3` | `tests/audit_perf_world_inventory_test.cpp`; benchmark `inventory_queries`. Still open: the header-level steps (a bitmask pocket set in the visit APIs, `get_pseudo_items()` by reference, a public raw visitor). After this change they would gain little, and they need a near-full rebuild |
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

**Verification.** Each fix was compile-checked with the project's `-Werror` flags and reviewed by a separate agent. The game and test suite were then built (`make TILES=0 RELEASE=1 PCH=0`) and run. All 35 `[audit]` test cases pass. The 31 from the first round all fail or crash on a build with their fix reverted, so they really catch the bugs; the 4 for ITEM-B02 and EVT-B02 were checked by review. The full suite passed then: 2313 of 2313 test cases with `--rng-seed 4242`.

After groups 4 and 5, the round 2 performance fixes and the 48 remaining bugs, all 88 `[audit]` test cases pass and the full suite passes: 2366 of 2366 test cases with `--rng-seed 4242` and with `--rng-seed 777`. After the PR review fixes that followed (a use-after-free in zone lookups from ACT-P01, save-load errors not aborting the load, stale crafting caches after a trade, John Bailey never leaving the refugee center), the suite passes again: 2366 of 2366 with seed 4242. After the second round of remaining high-severity findings, all 109 `[audit]` test cases pass and the full suite passes: 2387 of 2387 with seed 4242. Every mod set loads as before. After the third round (modding round 1 and bug sweep 1), all 228 `[audit]` test cases pass and the full suite passes: 2506 of 2506 with seed 4242, with no errors logged. Every mod set still loads with no errors. The newer audit tests were written to fail on the old code, but unlike the first round they were not each re-run against a reverted build.

Getting there needed four test fixes (commits `ae6b8464`, `2d15a016`). Five tests failed in full-suite runs but passed alone, because earlier tests left state behind. The main culprit: `clear_character()` did not reset the stat bonuses that hardcoded effects add, so after a frostbite test the avatar kept -2 INT, which slowed learning by 10% in later crafting tests. Three other tests (`memorials`, `available_reloadables`, `monster_speed_description`) now clear the map or the avatar themselves.

**Build note.** With the default precompiled header (`PCH=1`) and GCC 13, the build's dependency files list no project headers, so editing a `.h` file does not rebuild the files that include it. Build with `PCH=0` (or clean first) after any header change. A full test-suite run peaks around 9 GB of memory, so two runs at once do not fit in a 16 GB container.

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

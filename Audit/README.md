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

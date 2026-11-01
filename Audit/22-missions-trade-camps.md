# 22 · Missions, trade & basecamps

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/npctrade.cpp`, `src/npctrade_utils.cpp`, `src/shop_cons_rate.cpp`, `src/trade_ui.cpp`, `src/mission.cpp`, `src/mission_start.cpp`, `src/mission_place.cpp`, `src/mission_util.cpp`, `src/mission_ui.cpp`, `src/missiondef.cpp`, `src/mission_companion.cpp`, `src/faction_camp.cpp`, `src/basecamp.cpp`, `src/faction_mission.cpp`, `src/faction_ui.cpp`

## Summary

Basecamps are the weakest part of this system. Camp missions are a closed C++ enum with hardcoded durations, yields, terrain ids and encounters, and the `faction_mission` JSON type only supplies display text. The camp code also has several accounting bugs that players hit in normal play: food is billed twice, the fuel inventory inflates every time the board opens, and seeds are destroyed or duplicated. Quest (`mission_definition`) and trade code is in better shape. Their problems are loader slips, missing validation, missing EOC visibility (no item in shop conditions, no mission or trade events), and some redundant work in the trade UI.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 10 | 6 | 18 |
| Bugs & quirks | 0 | 4 | 9 | 5 | 18 |
| Performance | 0 | 0 | 3 | 5 | 8 |
| **Total** | 0 | 6 | 22 | 16 | 44 |

**Fix first:**
1. **CAMP-B02** · Camp resources gain duplicate entries on every board open: a one-line `clear()` fixes a commonly hit fuel overcount (crafts approved against fuel that does not exist), and also a growing crafting inventory.
2. **CAMP-B01** · Camp missions charge food twice: small change, and every gathering, crafting and upgrade mission currently drains the larder at about double the intended rate.
3. **CAMP-B04** · `farm_return` duplicates leftover seeds: a free, repeatable item-duplication loop; the fix is deleting one loop.
4. **CAMP-B03** · Farm "Plant" destroys the unselected part of a seed stack: silent item loss on every partial selection or cancel; small fix.
5. **CAMP-J01** · No EOC hooks for camp/companion lifecycle; mission deaths skip `die()`: medium effort. It unlocks camp-reactive content, and it fixes companions that die on missions without running death EOCs or cleanup.

## 1. Missing JSON bindings & EOC features

### CAMP-J01 · No EOC hooks or events for camp and companion-mission lifecycle; mission deaths skip die()
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/event.h:37`, `src/event.h:289-297`, `src/basecamp.cpp:902`, `src/faction_camp.cpp:3706-3757`, `src/faction_camp.cpp:3879-3889`, `src/faction_camp.cpp:4553-4556`, `src/faction_camp.cpp:4365-4370`, `src/faction_camp.cpp:6288`, `src/mission_companion.cpp:1308`, `src/mission_companion.cpp:1699`, `src/mission_companion.cpp:1750`, `src/npc.cpp:3311-3327`, `src/character_health.cpp:494`

The only event in this subsystem is `camp_taken_over`, and it carries no camp location. Companions leaving or returning, camp upgrades or expansions completing, food distribution and camp founding all happen without an event_bus event or JSON hook, so EOCs, achievements and scores cannot react to them. Companions killed on simulated missions (random encounters, scavenging raids, caravans) are erased with `overmap_buffer.remove_npc()` instead of going through `npc::die()`. As a result their `death_eocs` never run, `character_dies` is never sent, and `mission::on_creature_death` never fails quests that NPC gave out. These paths also skip `remove_npc_follower` and `my_fac->remove_member`. The camp's `assigned_npcs` keeps the shared_ptr, because `validate_assignees` only checks `assigned_camp`.

```cpp
// faction_camp.cpp:6286-6289
popup( _( "Pieces of %s were found strewn across a few bushes." ), ... );
overmap_buffer.remove_npc( comp.getID() );
return false;
// mission_companion.cpp:1699
overmap_buffer.remove_npc( comp->getID() );
```

**Fix:** Add events `camp_upgrade_completed {camp_name, omt (tripoint), recipe_id, direction}`, `camp_expansion_added {camp_name, omt, expansion_type}`, `companion_mission_started` and `companion_mission_finished {npc, mission (string id), camp_name, success}`, and add `omt` to `camp_taken_over`. Route mission deaths through a helper that calls `comp.die( nullptr )` before removal, or that at least runs death_eocs, sends `character_dies` and removes the follower. Use case: set a global var when the camp well is finished to unlock dialogue, or run a mourning EOC when a companion dies on patrol.

### CAMP-J02 · Camp missions are a closed C++ enum; the faction_mission JSON type only supplies display text
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/mission_companion.h:38-82`, `src/faction_camp.cpp:1537-1771`, `src/faction_camp.cpp:1755`, `src/faction_camp.cpp:864-1006`, `src/faction_mission.cpp:40-62`, `src/faction_mission.cpp:126-180`, `src/faction_camp.h:100`, `src/generic_factory.h:314`, `data/json/faction_missions.json:28-36`, `doc/JSON/FACTION_MISSIONS.md:22`

Every camp mission (gather, firewood, forage, trap, hunt, log, hide site, scout, patrol, plow and so on) is a value of `enum mission_kind`. Each is gated on a literal provides string (`has_provides( "foraging", dir )`) and run by a large `switch` in `basecamp::handle_mission`. That switch hardcodes duration, the skill used to pick the companion, exertion, departure and return messages, and the training skill. The `faction_mission` JSON type loads only text. Of its fields only `positions` is enforced; `skill`, `activity`, `time`, `risk` and `difficulty` are printed by `describe()` and change nothing. The shipped JSON already disagrees with the code. camp_scouting says BRISK_EXERCISE but runs at MODERATE_EXERCISE. camp_collect_firewood and camp_survey_expansion list no skill, while C++ uses survival and gun. camp_menial says fabrication but trains `menial`. FACTION_MISSIONS.md:22 says `skill` is "checked and/or trained", which is false. `activity_level` has no initializer, and generic_factory default-initializes with `T def;`, so camp_menial (which omits `activity`) displays an indeterminate float. Modders cannot add, retune or remove a camp mission.

```cpp
// faction_camp.cpp:1705-1711
case Camp_Foraging:
    if( miss_id.ret ) { gathering_return( miss_id.id, 4_hours ); }
    else { start_mission( miss_id.id, 4_hours, true, _( "departs to search for edible plants…" ),
                          false, {}, skill_survival, 0, LIGHT_EXERCISE ); }
// faction_camp.cpp:1755
start_combat_mission( miss_id.id, MODERATE_EXERCISE );   // JSON says BRISK_EXERCISE
```

**Fix:** Make `faction_mission` the source of truth and dispatch on its data instead of the enum: `"required_provides": "foraging"`, `"duration": "4 h"`, `"repeatable": true`, `"exertion": "LIGHT_EXERCISE"`, `"companion_skill": ["survival", 0]`, `"train": { "skill": "survival", "difficulty": 1 }`, `"depart_message"`, `"return_message"`, `"results": { ... }` (see CAMP-J09), `"start_eocs"`, `"return_eocs"`. Keep the bespoke flows (hide site, farming, recruiting) as named `"behavior"` actors chosen from JSON. As an immediate fix, default `activity_level` to 0 and correct the doc line. Use case: a mod adds `camp_fishing`, gated on a `fishing_dock` provide, which returns a seasonal fish group after 5 h.

### CAMP-J03 · Camp crafting ignores recipe result_eocs and always trains "construction"; upgrades ignore the blueprint skills
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:1591-1619`, `src/faction_camp.cpp:3408-3423`, `src/faction_camp.cpp:3770-3778`, `src/faction_camp.cpp:3884-3889`, `src/faction_camp.cpp:1938-1949`, `src/crafting.cpp:3797-3802`, `src/recipe.cpp:259-263`, `data/json/skills.json`

Normal crafting runs `recipe::result_eocs` (crafting.cpp:3797). Camp crafting builds its results up front with `create_results()`/`create_byproducts()` and never runs result_eocs, so an EOC-only recipe produces nothing at a camp and side-effect EOCs are silently skipped. No shipped camp recipe_group currently contains a recipe with result_eocs, so for now only mods are affected. On return, every camp craft trains the `construction` companion-practice category (fabrication 70 / mechanics 20 / survival 10) at difficulty 2, whatever the recipe's `skill_used` and `difficulty`. Upgrades train the same category at `making.difficulty`, although `start_upgrade` picks companions by the blueprint's `bld_reqs.skills`.

```cpp
// faction_camp.cpp:1614-1616
crafting_mission_return( miss_id.id, msg, skill_construction.str(), 2 );
// faction_camp.cpp:3418-3423 (no result_eocs)
for( const item &results : making->create_results( num_to_make, &used ) ) {
    comp->companion_mission_inv.insert( results );
}
```

**Fix:** The recipe id is already in `miss_id.parameters`. In `crafting_mission_return`, train `making.skill_used` at `making.difficulty` and run `making.result_eocs` once per batch item, with the NPC as alpha and the player as beta. In `upgrade_return`, train the skills in `blueprint_build_reqs().skills` and run result_eocs (or a new `"blueprint_eocs"` field).

### CAMP-J04 · mission_definition: id fields go unvalidated, per-goal requirements are unchecked, several fields are dead
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/missiondef.cpp:127-205`, `src/missiondef.cpp:220-227`, `src/mission.cpp:47-72`, `src/mission.cpp:585-587`, `src/mission.cpp:696-697`, `src/mission.h:301`, `src/mission.h:311`, `src/mission.h:318-319`, `src/talker_npc.cpp:137`

`check_consistency` validates only `item`. Typos in `monster_type`, `monster_species`, `followup`, `item_group`, `required_container`, `empty_container` and `destination` load silently and produce a broken mission. Nothing checks per-goal requirements either: MGOAL_GO_TO_TYPE without `destination` never completes, and MGOAL_KILL_MONSTER_TYPE without `monster_type` leaves `kill_count_to_reach` at 0 and completes immediately. `difficulty` is mandatory but is only used to pick urgent missions, and since that comparison is `>` against a starting value of 0, an `urgent` mission with difficulty 0 is never chosen. `good_fac_id` and `bad_fac_id` are only set to -1 and serialized. (The missing `recruit_class` loader is reported separately as CAMP-B10.)

```cpp
// missiondef.cpp:222-225
for( const mission_type &m : get_all() ) {
    if( !m.item_id.is_empty() && !item::type_is_defined( m.item_id ) ) { debugmsg( ... ); }
}
// talker_npc.cpp:137
if( type.urgent && type.difficulty > most_difficult_mission ) {
```

**Fix:** Validate every id field in `check_consistency` and enforce per-goal required fields (e.g. `debugmsg( "MGOAL_GO_TO_TYPE requires destination" )`). Make `difficulty` optional and use `>=` for urgent selection. Remove `good_fac_id`/`bad_fac_id` or implement them.

### CAMP-J05 · Camp farming ignores seed JSON (terrain flag, temperature) and hardcodes exclusions and yield placement
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:1213-1218`, `src/faction_camp.cpp:3427-3430`, `src/faction_camp.cpp:3525-3545`, `src/faction_camp.cpp:3566-3580`, `src/faction_camp.cpp:3641-3644`, `src/iexamine.cpp:2724-2730`, `src/activity_item_handling.cpp:2502`, `data/json/items/comestibles/seed.json:527`, `data/json/items/comestibles/seed.json:556`

Whether Plant Fields is offered depends on `warm_enough_to_plant` for buckwheat, whatever seeds are actually given (the code marks this `FIXME/HACK`). Planting puts any seed onto any dirt mound as `f_plant_seed` and ignores `seed.required_terrain_flag`, which manual planting and the auto-farm activity both check. This affects vanilla content: `seed_cattail` and `seed_lotus` require SHALLOW_WATER, yet companions plant them on dirt. Marloss and fungal seeds are excluded by hardcoded itype ids rather than a flag. The harvest formula does apply the plant's `harvest_multiplier` like manual harvesting does, but the yield is dropped at the player's feet (3577) rather than in camp storage.

```cpp
// faction_camp.cpp:3429
return itm.is_seed() && itm.typeId() != itype_marloss_seed && itm.typeId() != itype_fungal_seeds;
// faction_camp.cpp:3545 (no required_terrain_flag check)
farm_map.set( pos, ter_t_dirt, furn_f_plant_seed );
```

**Fix:** Check `warm_enough_to_plant` and `required_terrain_flag` per seed while planting and skip seeds that fail. Replace the itype exclusions with an item flag such as `NO_CAMP_FARMING`, set in JSON on marloss and fungal seeds. Route harvest output through `place_results()`, and optionally put the yield range on the `camp_harvest` faction_mission.

### CAMP-J06 · Basecamp recruiting is fully hardcoded: classless random NPCs, fixed odds, secret bonuses
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:4220-4334`, `src/faction_camp.cpp:5325-5365`, `src/faction_camp.cpp:5376-5399`, `src/faction_camp.cpp:1741-1748`, `src/npc.h:904`

The recruit is created with `make_shared_fast<npc>(); normalize(); randomize();` and no npc_class or template, although `npc::randomize(const npc_class_id&)` accepts one. The find and convince thresholds (`rng(1,20)+skill > 17`, `> 19`, `>= 10`), the appeal range, the 4-day duration and the "secret" bonuses (5 farms; more than 10 bionics with discipline above 75; more than 10 NPC kills) are all C++. The description is one fixed string, and Camp_Recruiting has no faction_mission JSON entry at all. Every recruit also gets a random ORIGIN_ANY_NPC mission. A side quirk: line 1747 picks the companion with `skill_gun`, while the description says "Skill used: social".

```cpp
// faction_camp.cpp:4232-4235
if( rng( 1, 20 ) + skill > 17 ) {
    recruit = make_shared_fast<npc>();
    recruit->normalize();
    recruit->randomize();
```

**Fix:** Add a `camp_recruiting` faction_mission with `"recruit": { "npc_classes": [["NC_SCAVENGER", 5], ...] (or "npc_templates"), "find_chance": { "math": [...] }, "convince_chance": { "math": [...] }, "appeal_bonus": [ { "condition": ..., "bonus": 10 } ], "on_recruit_eocs": [...] }`. Use case: a Magiclysm camp that attracts spellcasters.

### CAMP-J07 · BASECAMP.md documents provides that are not implemented; other camp tuning is hardcoded
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/BASECAMP.md:65-82`, `src/faction_camp.cpp:1261`, `src/faction_camp.cpp:1312-1330`, `src/faction_camp.cpp:864-879`, `src/faction_camp.cpp:1184-1240`, `src/faction_camp.cpp:1106-1110`, `src/faction_camp.cpp:245-248`, `src/faction_camp.cpp:1597-1612`, `src/basecamp.cpp:348-352`, `src/basecamp.h:59-61`, `src/faction_camp.cpp:5693-5705`, `src/faction_camp.cpp:5485-5487`

The provides table says `bed` gates expansions, `tool_storage` enables Store Tools, `pantry` improves Distribute Food, `dismantling` enables Chop Shop, `farming` adds Fertilize Fields, and `gathering` enables Distribute Food and Reset Sort Points. None of this exists in `src`. Expansions are capped only by `directions.size() < 8`, Distribute Food is gated only on `!by_radio`, `gathering` enables only Gather Materials, and `farming` enables only plow/plant/harvest. The doc lines for kitchen, blacksmith and reseeding are also wrong: recipe visibility comes from provides ids that match recipe_group ids (basecamp.cpp:449-473), not from building_type. Other tunables are hardcoded too: a 3-worker cap per expansion (commented `// WTF?`), return messages keyed on BASE/COOK/FARM/SMITH, `has_water()` tied to `water_well`/`fbmh_well_north`, work-day lengths of 10/8/6 h, larder exclusion rules (fun < -6, alcohol, INEDIBLE), and starvation penalty divisors 1250/625.

```cpp
// faction_camp.cpp:1261
if( directions.size() < 8 ) {
// basecamp.cpp:351
return has_provides( "water_well" ) || has_provides( "fbmh_well_north" );
```

**Fix:** Either implement the documented provides (e.g. `max_expansions = 1 + provides("bed")/2`) or remove them from the doc, and fix the kitchen/blacksmith lines. Move the tunables into a `camp_settings` JSON singleton or per-camp-type fields: `work_day_hours`, `max_crafters_per_expansion`, `water_provides: [..]`, `larder_min_fun`, `larder_exclude_flag`, plus `return_message` on recipe_group.

### CAMP-J08 · Camp state cannot be read or changed by EOCs; faction_food_supply is read-only
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/math_parser_diag.cpp:403-413`, `src/math_parser_diag.cpp:1907`, `src/condition.cpp:980-990`, `src/condition.cpp:1984-2006`, `src/condition.cpp:2726-2727`, `src/basecamp.cpp:320-346`, `src/faction.h:135-140`, `src/npctalk.cpp:8810-8818`

The EOC surface for camps consists of `u_has_camp`, `npc_has_assigned_camp`, `at_om_location: FACTION_CAMP_*`, the talk functions start_camp / assign_camp / abandon_camp / basecamp_mission, and a read-only `faction_food_supply()`. EOCs cannot check whether a camp has a provide or upgrade, count expansions or assigned/working NPCs, add calories or vitamins to a larder (`add_kcal`/`add_to_food_supply` are C++ only), grant provides, complete an upgrade, add an expansion, or send or recall a companion.

```cpp
// math_parser_diag.cpp:1907 (assign slot empty, unlike faction_like/trust/wealth)
{ "faction_food_supply", { "g", 1, faction_food_supply_eval, {}, { "vitamin" } } },
```

**Fix:** Add an assigner, `faction_food_supply('your_followers') += 50000` (optional kwarg `expires_in: time('7 d')`). Add math functions `camp_provides('kitchen', 'camp': <loc var>, 'dir': 'N')`, `camp_expansion_count()`, `camp_assigned_npcs()`, `camp_workers('camp_foraging')`. Add the condition `{ "camp_has_provides": "water_well", "camp": { "u_val": "camp_loc" } }` and effects `camp_add_provides` and `camp_start_mission { "mission": "camp_foraging", "npc": ... }`. Use case: a quest whose reward is a 100k kcal shipment to the larder.

### CAMP-J09 · Gathering, trapping and hunting yields, danger and random encounters are hardcoded
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:3906-3980`, `src/faction_camp.cpp:4678-4722`, `src/faction_camp.cpp:5402-5404`, `src/faction_camp.cpp:6240-6293`, `src/faction_camp.cpp:340-347`

`gathering_return` hardcodes per-mission danger (1 in 20/15/10), favor, threat, checks_per_cycle, the skill formulas (`2*survival + per`, `1.5*gun + per/2`) and roll difficulty (15/30). The item and monster group ids are fixed: "forest", four seasonal forage groups, GROUP_CAMP_HUNTING/TRAPPING/HUNTING_LARGE. `gathering_description` always samples "forest". `survive_random_encounter` (marked `TODO: Convert to JSON`) is a fixed sequence of English popups that can permanently delete the companion. Group contents can already be overridden globally (Magiclysm and DinoMod do this), so what is truly missing is per-region and per-camp variety, the formulas, and the encounters.

```cpp
// faction_camp.cpp:3931-3938
} else if( miss_id.id == Camp_Hunting ) {
    task_description = _( "hunting for meat" ); danger = 10; favor = 0; skill_group = "hunting";
    skill = 1.5 * comp->get_skill_level( skill_gun ) + comp->get_per() / 2.0;
    threat = 12; checks_per_cycle = 2;
// faction_camp.cpp:3949
item_group_id itemlist( "forest" );
```

**Fix:** Add to faction_mission `"results": { "item_group": "forest", "seasonal_item_groups": { "spring": ..., ... }, "monster_groups": [ ["GROUP_CAMP_TRAPPING", 1] ], "checks_per_cycle": 12, "skill_score": { "math": ["2*u_skill('survival')+u_val('perception')"] }, "difficulty": 15 }` and `"encounter": { "one_in": 15, "eoc": "EOC_CAMP_ENCOUNTER_FORAGE" }`, where the encounter EOC runs with the companion as alpha. Use case: a desert mod that forages cacti and hunts rattlesnakes.

### CAMP-J10 · Logging, clear-cutting, field survey and hide sites hardcode terrain and overmap ids
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:2321-2355`, `src/faction_camp.cpp:2357-2363`, `src/faction_camp.cpp:2382-2385`, `src/faction_camp.cpp:4431-4436`, `src/faction_camp.cpp:4449`, `src/faction_camp.cpp:4835-4840`, `src/faction_camp.cpp:4968`, `src/faction_camp.cpp:5005`, `src/faction_mission.cpp:41-63`

Logging targets are a fixed set of 7 oter types, and forest-to-field conversion is a static map. `start_cut_logs`/`start_clearcut` only let you pick tiles from that set, so modded forest OMTs (Innawood, XedraWood, Magiclysm) cannot be selected at all. The "isn't a recognized terrain. Please file a bug report" popup is practically unreachable. Survey Field converts only tiles whose terrain is in a fixed list of 10 ter ids, and always sets the OMT to `field`. Trunk and stump processing ignores the terrain's own bash/deconstruct data and spawns 2-3 `log` on `t_dirt`, and trip estimates use `itype_log->weight * 7.5`. Hide sites always stamp `t_improvised_shelter` and set `faction_hide_site_0`.

```cpp
// faction_camp.cpp:2327
static const std::unordered_map<oter_type_str_id, oter_type_str_id> clear_cut_conversion = {
    { oter_type_forest, oter_type_field }, ... };
// faction_camp.cpp:4837-4839
if( t == ter_t_trunk || t == ter_t_stump ) {
    target_bay.ter_set( p, ter_t_dirt );
    target_bay.spawn_item( p, itype_log, rng( 2, 3 ), 0, calendar::turn );
```

**Fix:** Add overmap_terrain JSON `"camp_logging": { "cleared_into": "field" }` (or a CAMP_LOGGABLE flag plus `clearcut_result`) and derive the forest set from it. Add a terrain flag `CAMP_FIELD_CONVERTIBLE` for survey. Use the trunk terrain's `bash` drop group for log yield. Put the hide-site terrain and oter on the `camp_setup_hide_site` faction_mission.

### CAMP-J11 · Legacy outpost companion missions are tied to hardcoded role strings, Tacoma Ranch content and FMCNote pay
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mission_companion.cpp:467-510`, `src/mission_companion.cpp:1055-1166`, `src/mission_companion.cpp:1268-1288`, `src/mission_companion.cpp:1318-1319`, `src/mission_companion.cpp:1705-1721`, `src/mission_companion.cpp:1802`, `src/talker_npc.cpp:561-565`, `src/npctalk.cpp:3425-3433`

The `companion_mission` dialogue effect sets the role and calls `talk_function::companion_mission`. That function knows only "SCAVENGER", "COMMUNE CROPS", "FOREMAN", "REFUGEE MERCHANT", "PLANT FIELD" and "HARVEST FIELD"; any other role silently returns. Mission lists, durations, pay (`rng(20,75)`), unlocks (trait NPC_MISSION_LEV_1, var SCAVENGER_HOSPITAL_RAID), the caravan destination (evac_center_18), enemy templates (bandit, thug, commune_guard) and loot groups (`"npc_misc"`) are all C++. Pay is always `FMCNote`, even though factions define a JSON `currency` that shop restock already uses. Only Tacoma Ranch uses this today, but modders cannot give their own faction NPC a job board. A proper replacement is closer to large effort than medium.

```cpp
// mission_companion.cpp:474-506
if( role_id == "SCAVENGER" ) { ... } else if( role_id == "COMMUNE CROPS" ) { ... }
... } else { return; }
// mission_companion.cpp:1720-1721
item merch = item( itype_FMCNote );
get_player_character().i_add_or_drop( merch, merch_amount );
```

**Fix:** Add a JSON `companion_mission_board` keyed by role id that lists faction_mission-style entries: duration, danger, `"pay": { "min": .., "max": .. }` in the giver faction's `currency`, result item groups, enemy npc_templates, a `condition` in place of NPC_MISSION_LEV_1, and `return_eocs`. Make `companion_mission` look up the board and debugmsg on unknown roles.

### CAMP-J12 · Shop and price-rule conditions cannot see the traded item; no hook after a trade; rule values are fixed numbers
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/shop_cons_rate.cpp:32-40`, `src/shop_cons_rate.cpp:146-157`, `src/shop_cons_rate.cpp:187-218`, `src/faction.h:70-74`, `src/faction.cpp:96-103`, `src/npctrade.cpp:161-166`, `src/npctrade.cpp:205`, `src/npctrade.cpp:294-351`, `src/init.cpp:1016`

`icg_entry::matches` drives shopkeeper blacklists and whitelists, consumption rates, and faction/class price rules. It builds a dialogue of avatar and NPC only, so a `condition` cannot test the item (charges, damage, freshness, a `stolen` var). Price rules take plain doubles (`markup`, `premium`, `fixed_adj`), so prices can be tiered by trust step by step but cannot scale continuously with trust, season or scarcity. `npc_trading::trade` completes with no event or EOC. The trade adjustment formula (0.25 multiplier, 5% per INT, speech practice `value_you / 10000`) is fixed. Only cons_rates has a `check()`, and it validates no ids. Blacklists, whitelists and price rules have none, so a typo'd id silently matches nothing.

```cpp
// shop_cons_rate.cpp:34-35
const_dialogue temp( get_const_talker_for( get_avatar() ), get_const_talker_for( beta ) );
return ( !condition || condition( temp ) ) && ...
// faction.h:71-74
double markup = 1.0; double premium = 1.0; std::optional<double> fixed_adj ...
```

**Fix:** Pass the item to icg conditions as an item talker or context var (e.g. `{ "context_val": "trade_item_id" }`). Make `premium`, `markup` and `fixed_adj` `dbl_or_var`. Add an event `player_trades {npc, faction, value_given, value_received}` and optional `trade_eocs` on npc_class or faction. Add `check()` for blacklist, whitelist and price rules that validates ids. Use case: a fence who pays more for items flagged stolen.

### CAMP-J13 · Mission target and EOC loader slips and doc drift: search_range default, reward token, finish_mission, undocumented goals
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mission_util.cpp:385-387`, `src/mission_util.cpp:391-395`, `src/mission_util.cpp:244-246`, `src/mission_ui.cpp:406-414`, `src/npctalk.cpp:6173-6183`, `doc/JSON/MISSIONS_JSON.md:98`, `doc/JSON/MISSIONS_JSON.md:113-130`, `doc/JSON/MISSIONS_JSON.md:181`, `doc/JSON/MISSIONS_JSON.md:219`, `doc/JSON/EFFECT_ON_CONDITION.md:2155`, `doc/JSON/EFFECT_ON_CONDITION.md:2167`

(1) `exclude_seen` is undocumented and broken (details in CAMP-B11). (2) The docs give `search_range` a default of 2520; the code uses `OMAPX * 3` = 540, so distant targets are not found (a debugmsg fires, so this is not silent). (3) MISSIONS_JSON documents a `<reward_item:FMCNote>` description token, but only `<reward_count:...>` is substituted. (4) The EOC docs say `finish_mission`'s `success` is optional with default false, but the loader has no fallback, so the documented `{ "finish_mission": "DID_I_WIN" }` fails to load. (5) The doc heading says `origin`; the key is `origins`. MGOAL_FIND_ITEM_GROUP, MGOAL_KILL_NEMESIS and the `destination`, `place`, `value`, `item_group`/`count`/`required_container` fields are undocumented, even though `destination` is required for MGOAL_GO_TO_TYPE.

```cpp
// mission_util.cpp:395
optional( jo, false, "search_range", p.search_range, OMAPX * 3 );   // doc: "Default 2520"
// npctalk.cpp:6182
success = jo.get_bool( "success" );
```

**Fix:** Choose one search_range default and document it. Accept `<reward_item:...>` as an alias, or fix the doc. Use `jo.get_bool( "success", false )`. Fix the `origins` heading and add doc rows for the missing goals and fields.

### CAMP-J14 · Random mission pools cannot be conditioned; 'place' and 'start' are hardcoded C++ function maps
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/missiondef.cpp:13-33`, `src/missiondef.cpp:165-182`, `src/missiondef.cpp:243-256`, `src/mission.cpp:387-395`, `src/mission_start.cpp:293-295`, `src/mission_start.cpp:199`

Randomly handed-out missions (ORIGIN_ANY_NPC and ORIGIN_OPENER_NPC, camp recruits, debug spawns) are picked by `get_random_id`. It filters only by origin and a `place` function from a 3-entry map (never, always, near_town). There is no condition and no weight. Start functions are also a C++ map: `place_book` is an empty no-op that is still listed, and `place_deposit_box` is unused in data. Conditioned offering already exists through the `add_mission`/`assign_mission` talk effects and `mission_offered`, so only the random pool lacks gating.

```cpp
// missiondef.cpp:247-253
for( const mission_type &t : get_all() ) {
    if( std::find( t.origins.begin(), t.origins.end(), origin ) == t.origins.end() ) continue;
    if( t.place( p ) ) valid.push_back( t.id );
}
```

**Fix:** Add `"origin_condition"` (player as alpha, prospective giver as beta) and `"origin_weight"` to mission_definition, and evaluate them in `get_random_id` (pass the NPC in). Let `place` accept a condition or an om_location match. Retire the empty start functions.

### CAMP-J15 · Two-way radio and camp radio-tower ranges are hardcoded; item range and 'radio' provide amount are ignored
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_ui.cpp:71-145`, `src/basecamp.cpp:330-346`, `src/npcmove.cpp:5944-5945`

Radio contact uses a fixed `base_range = 200` submaps, a ±10% elevation factor per z-level, and `radio_tower_boost = 5`. Every TWO_WAY_RADIO item behaves the same, and the camp `radio` provide is effectively boolean because its amount is ignored. EOCs cannot query radio reach. A related inconsistency: npcmove.cpp:5944 reports an ally's arrival over radio with no range check at all.

```cpp
// faction_ui.cpp:100, 111, 120
const int base_range = 200;
const int radio_tower_boost = 5;
if( !i.camp->has_provides( "radio" ) ) { continue; }
```

**Fix:** Add an item field `"radio_range": 200` (the best carried item wins). Scale the camp boost by the provide amount (`"blueprint_provides": [{ "id": "radio", "amount": 2 }]`), or add `"radio_range_bonus"` on the blueprint.

### CAMP-J16 · EOCs cannot see mission state: no completed/failed check, no mission events, null beta in target search
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mission_util.cpp:520-529`, `src/mission_util.cpp:438-446`, `src/mission_util.cpp:148-155`, `src/mission_util.cpp:412`, `src/condition.cpp:582-593`, `src/mission.cpp:397-440`, `src/mission.cpp:473`, `src/mission.h:318-319`, `src/mission.cpp:993-994`, `src/npctalk_funcs.cpp:122-144`

The target-search dialogue has no beta (`dialogue d( get_talker_for( get_avatar() ), nullptr )`), so `npc_`-scoped var objects in `om_terrain`/`om_special` evaluate wrongly. `u_has_mission` checks only active missions; nothing can check whether mission X was completed or failed, although the avatar keeps both lists. There are no math functions for mission status, deadline or kill progress, and `assign()`, `fail()` and `wrap_up()` emit no event. `good_fac_id`/`bad_fac_id` are never set meaningfully. Workarounds exist and keep the severity low: `assign_mission_target` accepts a `var`, start effects run before funcs and can set it, faction rewards can be set with the faction_like/trust assigners, and completion can be tracked with hand-set vars in end/fail blocks.

```cpp
// condition.cpp:586
for( mission *miss_it : get_avatar().get_active_missions() )
// mission.cpp:429-440
void mission::fail() { status = mission_status::failure; ... type->fail( this ); }   // no event_bus
```

**Fix:** Construct the target dialogue with the giver NPC as beta. Expose the mission as context vars (`mission_id`, `mission_target`) to start, end and fail EOCs. Add conditions `u_completed_mission` / `u_failed_mission`, math `mission_status('ID')` (0-3), `mission_deadline('ID')`, `mission_kills_left('ID')`, and events `mission_assigned`, `mission_completed`, `mission_failed {mission_type_id, giver}`.

### CAMP-J17 · Mission goals are narrow: one monster type, species or destination, exact matching, no fail condition
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/mission.cpp:585-588`, `src/mission.cpp:696-700`, `src/missiondef.cpp:192-195`, `src/mission.cpp:856-867`

MGOAL_GO_TO_TYPE compares the exact oter_type against a single `destination` and ignores the `ot_match_type` support that `assign_mission_target` has. The kill goals take a single mtype or species. Missions fail only on deadline or giver death; there is no `fail_condition`. All of this can already be built from generic tools: MGOAL_CONDITION with OR'd `u_at_om_location`, `event_statistic()` with `equals_any`/`species_of_monster` for multi-type kill counts, and a recurring EOC calling `finish_mission` with `success: false`. This is an ergonomics gap, not a missing capability.

```cpp
// mission.cpp:587
return ( cur_ter->get_type_id() == oter_type_str_id( type->target_id.str() ) );
// mission.cpp:862-866
if( has_deadline() && calendar::turn > deadline ) { fail(); }
```

**Fix:** Accept arrays (`"monster_type": [..]`, `"monster_species": [..]`, `"destination": [..]` plus `"destination_match_type": "PREFIX"`), and add a `"fail_condition"` evaluated in `process()`.

### CAMP-J18 · Fortification lines (and the dead salt-water pipe) are special-cased by hardcoded mapgen ids and provides strings
**Severity:** Low · **Effort:** Large · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:209-224`, `src/faction_camp.cpp:1914-1920`, `src/faction_camp.cpp:3858-3862`, `src/faction_camp.cpp:2729-2741`, `src/faction_camp.cpp:2806-2844`, `src/faction_camp.cpp:2564-2577`, `src/faction_camp.cpp:2633-2647`, `data/json/obsoletion_and_migration_0.I/obsolete_salt_water_pipe.json:3`

Multi-OMT construction outside the camp exists only for two hardcoded features. Fortifications are tied to eight `faction_wall_level_{N,E,S,W}_{0,1}` mapgens, with the trench-before-spikes prerequisite in C++, so a modder cannot add a fence line, power line or canal using the same machinery. The salt-water pipe is triggered by four update_mapgen ids and coordinated through parsed `salt_water_pipe_<n>_scheduled` provides strings. That feature is dead: its recipes are obsolete, `never_learn`, and gated on a nonexistent requirement, and the data file notes that the #55026 C++ can be deleted. Reviewer note, likely a separate bug: in `apply_fortifications` (2564-2577) `is_spiked` selects `pits[0]/[1]` (the `*_0` mapgens, t_pit), while the trench mission selects `pits[2]/[3]` (the `*_1` mapgens, t_pit_spiked). If so, the cheap trench builds spiked pits and the spiked mission builds plain ones.

```cpp
// faction_camp.cpp:1914-1919
if( making.get_blueprint() == update_mapgen_faction_expansion_salt_water_pipe_swamp_N ) {
    start_salt_water_pipe( miss_id ); return;
} else if( making.get_blueprint() == update_mapgen_faction_expansion_salt_water_pipe_N ) { ... }
```

**Fix:** Delete the salt-water-pipe C++ path. Verify and fix the pit index swap. Generalize fortifications as blueprint JSON, for example `"blueprint_line": { "mapgen_pair": { "ns": [..], "ew": [..] }, "requires_existing": "faction_wall_level_N_0" }`, and dispatch on those fields instead of mapgen ids.

## 2. Bugs, oversights & quirks

### CAMP-B01 · Camp missions charge food twice: up front at start and again for elapsed time at return
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:3706`, `src/faction_camp.cpp:3720`, `src/faction_camp.cpp:3750`, `src/faction_camp.cpp:1811`, `src/faction_camp.cpp:1899`, `src/faction_camp.cpp:1626`, `src/faction_camp.cpp:3947`, `src/faction_camp.cpp:3412`, `src/faction_camp.cpp:3775`, `src/faction_camp.cpp:3888`, `src/faction_camp.cpp:3809`

`finish_return()` says missions that are not `fixed_time` "pay their food costs at the end, instead of up-front", and charges `time_to_food(elapsed)` when `fixed_time` is false. But those same missions already paid at start with `must_feed=true`. Gathering, firewood, foraging, trapping and hunting call `start_mission(..., must_feed=true)` and return through `gathering_return -> finish_return(false)`. Crafting goes `start_mission(work_days, true)` -> `crafting_mission_return -> finish_return(false)`. Upgrades are billed `work_days*N` in `start_multi_mission`, and then `upgrade_return` calls `finish_return(false)` for each worker. A 3-hour gathering trip collected on time bills the larder for 6 hours, and each upgrade worker pays for the whole job twice. `emergency_recall` charges again too, even with `cancel=true` (its own FIXME at 3809). Also, `companion_mission_travel_time` is only saved and loaded, never assigned, so the travel-time part of the end bill is always 0.

```cpp
// faction_camp.cpp:3720-3722
// Missions that are not fixed_time pay their food costs at the end, instead of up-front.
if( !fixed_time ) { need_food = time_to_food( mission_time, ...
// faction_camp.cpp:1811
if( must_feed ) { feed_workers( ..., camp_food_supply( total_time, exertion_level, travel_time ), ... );
```

**Fix:** Pick one billing model. Either pass `must_feed=false` (or set a stored "prepaid" flag) for non-fixed missions, or have `finish_return` bill only `max(0, elapsed - prepaid_time)`, with the prepaid duration stored on the NPC next to `companion_mission_exertion`. Assign `companion_mission_travel_time` in `start_mission`.

### CAMP-B02 · basecamp::resources gains duplicate entries every time the camp board opens, inflating fuel and the crafting inventory
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/basecamp.cpp:482`, `src/basecamp.cpp:488`, `src/basecamp.cpp:540`, `src/basecamp.cpp:553`, `src/basecamp.cpp:556`, `src/basecamp.cpp:726`, `src/basecamp.cpp:829`, `src/basecamp.cpp:1101`, `src/basecamp.h:529`, `src/faction_camp.cpp:780`, `src/faction_camp.cpp:1257`, `src/iexamine.cpp:2141`

Both the bulletin board (iexamine.cpp:2141) and the radio/NPC menu (faction_camp.cpp:780) call `get_available_missions()`, which calls `reset_camp_resources()` every time. That re-runs `update_resources()` for every provide and `add_resource()` for every pseudo item, and `add_resource()` always does `resources.emplace_back`. The vector is not serialized and is never cleared, so after K openings each resource appears K times. `form_crafting_inventory` then adds one pseudo tool per entry, each loaded with the full fuel stock, so craft and upgrade checks see K times the real fuel. `basecamp::use_charges` draws from each duplicate, and the consume loop calls `map::use_charges` for each duplicate's `consumed`, which removes only what exists, so the shortfall is silently forgiven. Players can start crafts needing far more charcoal or other fuel than they own. As a side effect, the camp `_inv` that every `can_make_with_inventory` check walks grows linearly over a session (the perf audit raised this separately). For legacy expansions with `cur_level >= 0`, the same function also re-runs `update_provides()` (`+=`) on every opening. That part only affects pre-migration saves, because loading sets `cur_level = -1`.

```cpp
// basecamp.cpp:488
resources.emplace_back( bcp_r );
// basecamp.cpp:552-557 (reset_camp_resources)
for( const auto &bp_provides : e_data.provides ) { update_resources( bp_provides.first ); }
for( itype_id &it : e.second.available_pseudo_items ) { add_resource( it ); }
// basecamp.cpp:838
camp_item = camp_item.ammo_set( bcp_f.ammo_id, bcp_f.available );
```

**Fix:** Call `resources.clear()` and `fuel_types.clear()` at the top of `reset_camp_resources`. Also make `add_resource` skip a `fake_id` that is already present, so the incremental callers (`add_expansion`, `upgrade_return`, the pipe returns) cannot duplicate entries either. Rebuild legacy provides from scratch instead of accumulating them.

### CAMP-B03 · Farm "Plant" mission destroys the unselected part of a seed stack, even when cancelled
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:3656`, `src/faction_camp.cpp:3658`, `src/faction_camp.cpp:3668`, `src/faction_camp.cpp:1819`, `src/faction_camp.cpp:1821`, `src/faction_camp.cpp:5110`, `src/mission_companion.cpp:1193`, `src/mission_companion.cpp:1196`

`start_farm_op()` sets `seed->charges = num_seeds` directly on the real camp item, before a companion is chosen or food is checked. Choose 10 from a 50-seed stack and the stack becomes 10. If `start_mission` then fails (not enough food, or you cancel the companion choice), the other 40 are gone. If it succeeds, `individual_mission` copies the 10 to the companion and also calls `comp->use_charges(type, 10)`, which consumes the companion's own seeds of that type. `start_mission` then calls `target_map.use_charges(src_set_pt, type, 10)`, which removes the truncated stack. Net result: 50 camp seeds plus the companion's own seeds are spent to plant 10. Separately, `start_mission` builds `src_set_pt` with `resize(N)` followed by `emplace_back`, so the list starts with N copies of bubble (0,0,0), and items on the camp map's corner tile can be consumed too.

```cpp
// faction_camp.cpp:3656-3657
seed.overflow( here );
if( seed->count_by_charges() ) { seed->charges = num_seeds; }
// faction_camp.cpp:1819, 1821
src_set_pt.resize( src_set.size() );
src_set_pt.emplace_back( target_map.get_bub( p ) );
```

**Fix:** Do not modify the source item: build `to_plant` from copies (`item tmp = *seed; tmp.charges = num_seeds;`) and take seeds from storage only after `start_mission` succeeds, through the item locations (split or `remove_item`) rather than a type-based `use_charges`. In `individual_mission`, do not call `use_charges`/`use_amount` on the companion. Change `resize()` to `reserve()`.

### CAMP-B04 · farm_return duplicates leftover seeds (given to the player and also re-placed at camp)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:4585`, `src/faction_camp.cpp:4592`, `src/faction_camp.cpp:3735`

`farm_return()` gives every `companion_mission_inv` item with charges > 0 to the player with `i_add`, but does not clear `companion_mission_inv`. It then calls `finish_return(comp, fixed_time=true, ...)` with `cancel` defaulting to false, and that function places every item still in `companion_mission_inv` at the camp dumping spot before clearing it. Unplanted seeds are duplicated: send 100 seeds to a 1-plot field, recover the planter, and 99 seeds land in your inventory while another 99 appear in camp storage. Repeating this is a free duplication loop for any seed type.

```cpp
// faction_camp.cpp:4586
for( const item &it : comp->companion_mission_inv ) { if( it.charges > 0 ) { player_character.i_add( it ); } }
// faction_camp.cpp:4592
finish_return( *comp, true, msg, skill_survival.str(), 2 );
// faction_camp.cpp:3735
if( !cancel ) { for( const item &it : comp.companion_mission_inv ) { ... place_results( it ); } }
```

**Fix:** Delete the `i_add` loop and let `finish_return` return the seeds to camp storage, or clear `companion_mission_inv` after handing them to the player.

### CAMP-B05 · update_time_left overwrites 'avail'; camp returns check a fixed minimum instead of time_ret, allowing early recall
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:492`, `src/faction_camp.cpp:498`, `src/faction_camp.cpp:505`, `src/faction_camp.cpp:1081`, `src/faction_camp.cpp:4338`, `src/faction_camp.cpp:3288`, `src/mission_companion.cpp:2652`

`update_time_left()` sets `avail = true` for a finished worker, but then resets `avail = has_trait(DEBUG_HS)`, normally false, for every unfinished worker after it. The result depends on list order: [done, not-done] disables Recover even though one worker is done, while [not-done, done] enables it. Once the entry is enabled, the return handlers pick companions with `companion_choose_return(miss_id, min_duration)`, which compares `companion_mission_time` to `turn - fixed minimum` and never looks at `companion_mission_time_ret`. Scouting and combat patrol (3 positions, time = route travel time) are affected. Send scout A on a 2-hour loop and scout B on a 2-day loop; once A finishes, B can be recalled after just 3 hours and still gets the full route revealed and full reserve-time training (`fixed_time=true`).

```cpp
// faction_camp.cpp:498
if( comp->companion_mission_time_ret < calendar::turn ) { ... avail = true; }
else { ... avail = player_character.has_trait( trait_DEBUG_HS ); }
// faction_camp.cpp:4338
npc_ptr comp = companion_choose_return( miss_id, 3_hours );
```

**Fix:** Use `avail |= ...` and OR in DEBUG_HS once at the end. In the return paths for missions that set `companion_mission_time_ret`, filter on `companion_mission_time_ret <= calendar::turn`: pass `calendar::before_time_starts` as `farm_return` already does, or add a dedicated check.

### CAMP-B06 · camp_food_supply: a zero-cost request throws a debugmsg; an empty larder divides by zero
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:5458`, `src/faction_camp.cpp:5468`, `src/faction_camp.cpp:1652`, `src/faction_camp.cpp:1813`, `src/faction_camp.cpp:4323`, `src/faction_camp.cpp:3750`, `src/faction_camp.cpp:5632`, `src/stomach.cpp:249`

(a) `camp_food_supply(nutrients)` raises a debugmsg when `change.calories >= 0`. Camp_Survey_Field starts with `start_mission(..., 0_hours, must_feed=true)`, and `time_to_food(0) == 0`, so every field survey pops "camp_food_supply cannot add to stockpile". `recruit_return` hits the same path when `food_desire` is 0. (b) When vitamins are empty, `percent_consumed = |kcal| / food_supply().calories`, which divides by zero if the larder holds 0 kcal. `finish_return` reaches this right after its "pantry is empty" popup. The NaN or inf is then converted to int64/int in `nutrients::operator*=`, which is undefined behaviour and feeds garbage into the food supply and the faction opinion penalty. (The `handle_takeover_by` path is effectively unreachable in vanilla.)

```cpp
// faction_camp.cpp:5458
if( change.calories >= 0 ) { debugmsg( "camp_food_supply cannot add to stockpile" );
// faction_camp.cpp:5468
double percent_consumed = std::abs( static_cast<double>( change.calories ) ) / fac()->food_supply().calories;
```

**Fix:** Return an empty portion silently when `change.calories == 0`. Guard the proportional-vitamin path with `if( supply.calories <= 0 )` and skip the scaling. Use `must_feed=false` for 0-hour missions.

### CAMP-B07 · loot_building's std::set<item> collapses equal items, and its "kill zombies" step kills any creature
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mission_companion.cpp:2688`, `src/mission_companion.cpp:2696`, `src/mission_companion.cpp:2751`, `src/mission_companion.cpp:2761`, `src/item.cpp:3604`, `src/mission_companion.cpp:1757`, `src/mission_companion.cpp:1865`, `src/creature_tracker.cpp:310`

Scavenging and hospital raids collect loot into `std::set<item>`. `item::operator<` treats two items as equal when they share category, type and charges (or category and name), and ignores contents. Every chosen item is erased from the map but only one copy is kept: ten identical cans become one, and two backpacks with different contents become one, losing the second backpack and everything in it. The callers' `all_returned_items` repeats the dedup across buildings. The same loop calls `creatures.creature_at(...)->die()` on every tile using the untemplated lookup, which also returns NPCs and the avatar, so any loaded follower or pet standing in the building is killed. That case is situational: the target is the house nearest the player, who is at the ranch.

```cpp
// mission_companion.cpp:2696, 2763
std::set<item> return_items; ...
return_items.insert( *it ); it = items.erase( it );
// mission_companion.cpp:2748
Creature *critter = creatures.creature_at( rebase_bub( p ) );
if( critter != nullptr ) { critter->die( bay.cast_to_map(), nullptr ); }
```

**Fix:** Use `std::vector<item>` (or `std::list<item>`) in `loot_building` and in the callers' `all_returned_items`. Restrict the kill step to `creature_at<monster>` with a hostility check, and never target the avatar or allies.

### CAMP-B08 · Faction menu founds a phantom basecamp for followers on non-camp companion missions
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_ui.cpp:212`, `src/faction_ui.cpp:213`, `src/faction_camp.cpp:644`, `src/faction_camp.cpp:659`, `src/basecamp.cpp:201`, `src/faction_camp.cpp:777`, `src/npctalk.cpp:1665`, `src/overmapbuffer.cpp:1575`

In the Your Followers tab, confirming on any follower with `has_companion_mission()` calls `talk_function::basecamp_mission(*follower)`, whatever the role. For an outpost job (Tacoma Ranch SCAVENGER, FOREMAN, crop work) the NPC is parked at a location with no player camp. `get_basecamp()` then calls `add_camp()` and `define_camp()` there. That forces a naming popup, retypes the OMT to `faction_base_camp_0`, adds the camp to the player's list and opens its management UI. Near the ranch `find_camp` may instead find the tacoma_commune camp and show an "isn't under your control" popup, which is also wrong. In the radio branch, `basecamp_mission` reads `get_avatar().dialogue_by_radio`, which is only set in `avatar::talk_to` and is stale here. `get_camp_map()` can therefore return the local reality bubble instead of the remote camp's map.

```cpp
// faction_ui.cpp:212
if( picked_follower->has_companion_mission() ) { talk_function::basecamp_mission( *picked_follower ); return true; }
// faction_camp.cpp:777
bcp->set_by_radio( get_avatar().dialogue_by_radio );
```

**Fix:** Call `basecamp_mission` only when the follower's mission role is `base_camps::id` and `overmap_buffer.find_camp()` succeeds; otherwise open `talk_function::companion_mission` or show a message. Pass the radio flag explicitly, computed as `radio_interactable && !interactable`.

### CAMP-B09 · Trading away a held count-by-charges item removes charges from an arbitrary same-type stack
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctrade.cpp:104`, `src/npctrade.cpp:106`, `src/npctrade.cpp:113`, `src/character.cpp:5829`, `src/character.h:3136`

For held count-by-charges items, `transfer_items()` calls `giver.use_charges(gift.typeId(), ip.second)`. That is a type-based search over the whole inventory in visit order and ignores the selected `item_location`. The receiver gets a copy of the selected stack, priced from that stack. Example: sell 20 portions of fresh meat while carrying a nearly rotten stack of the same type earlier in visit order. You are paid for fresh meat, keep the fresh meat, and lose the rotten stack. This happens in both directions (player selling and NPC selling). The map branch does it correctly by tagging the selected item.

```cpp
// npctrade.cpp:104-106
if( ip.first.held_by( giver ) ) {
    if( ip.first->count_by_charges() ) { giver.use_charges( gift.typeId(), ip.second );
// npctrade.cpp:113-114 (map path, correct)
ip.first.get_item()->set_var( "trade_charges", ip.second );
```

**Fix:** Reduce the selected stack directly (`ip.first->charges -= ip.second; if( ip.first->charges <= 0 ) ip.first.remove_item();`), then call `on_contents_changed` or invalidate the giver's weight cache.

### CAMP-B10 · MGOAL_RECRUIT_NPC_CLASS is broken: recruit_class is never loaded or copied
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mission.cpp:675`, `src/mission.cpp:678`, `src/mission.cpp:47-72`, `src/missiondef.cpp:127-205`, `src/mission.h:210`, `src/string_id_null_ids.cpp:39`, `src/talker_npc.cpp:137`, `data/json/npcs/common_chat/TALK_COMMON_MISSION.json:223`, `doc/JSON/MISSIONS_JSON.md:125`

`is_complete()` for MGOAL_RECRUIT_NPC_CLASS compares `npc->myclass` with `mission::recruit_class`. `mission_type::load` never reads `recruit_class`, and `create()` does not copy it, so it is only set by the constructor and by savegame code. The instance keeps the null id, which for npc_class is `NC_NONE`. So the goal ignores the intended class and completes for any following NC_NONE NPC within range. The documented goal cannot be used as designed; no shipped content uses it. Separately, the generic turn-in reply "I brought 'em." requires `mission_goal == MGOAL_RECRUIT_NPC` AND `MGOAL_RECRUIT_NPC_CLASS`, which can never both be true. Recruit missions such as MISSION_RECRUIT_TRACKER therefore fall through to "Mission success! I don't know what else to say."

```cpp
// mission.cpp:678
if( npc->myclass == recruit_class && npc->get_attitude() == NPCATT_FOLLOW )
// TALK_COMMON_MISSION.json:223
"and": [ "mission_complete", { "mission_goal": "MGOAL_RECRUIT_NPC" }, { "mission_goal": "MGOAL_RECRUIT_NPC_CLASS" } ]
```

**Fix:** Add `optional( jo, was_loaded, "recruit_class", recruit_class )` in `mission_type::load`, copy it in `create()`, validate it in `check_consistency`, and document it. Change the dialogue condition to `or`.

### CAMP-B11 · assign_mission_target parser/search bugs: 'exclude_seen' reads the wrong key; replacement terrain leaks into pass 2
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mission_util.cpp:385`, `src/mission_util.cpp:209`, `src/mission_util.cpp:213`, `src/mission_util.cpp:217`, `src/mission_util.cpp:232`, `src/mission_util.cpp:460`, `src/overmapbuffer.cpp:1411`

(1) `parse_mission_om_target` checks for `exclude_seen` but reads `get_bool("exclude")` into `p.random`, so using the key either throws a missing-member error or sets `random`. No data uses it yet. (2) In `find_or_create_om_terrain`, the `om_terrain_replace` branch overwrites the captured `find_params.types` with the replacement terrain. Pass 2 (`existing_only=false`, line 232) then searches only for the replacement terrain and never for the real target on new overmaps. A real case is DinoMod NC_Yoshimi.json:209 (dinozoo_1_1, replace "land", range 220), which fails as a result. (3) The inner retry is commented "allow this search to create new overmaps" but sets `existing_only = true`, which it already was, so it does nothing. (4) `update_mapgen`'s target lookup ignores `om_special`, although MISSIONS_JSON.md says it uses both. Forcing origin z=0 only affects distance ordering, since `find_closest` scans all z-levels.

```cpp
// mission_util.cpp:385
if( jo.has_member( "exclude_seen" ) ) { p.random = jo.get_bool( "exclude" ); }
// mission_util.cpp:213, 216-217
find_params.types.front().first = ( *params.replaceable_overmap_terrain ).evaluate( d );
// We didn't find it, so allow this search to create new overmaps and try again.
find_params.existing_only = true;
```

**Fix:** Map `exclude_seen` to `p.cant_see = jo.get_bool( "exclude_seen" )` (or remove it). Use a local copy of `find_params` for the replacement search. Set `existing_only = false` in the retry. Support `om_special` in update_mapgen and pass the target's z.

### CAMP-B12 · MGOAL_FIND_ITEM_GROUP double-counts nested items, then consumes every match of only one type
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mission.cpp:487`, `src/mission.cpp:508`, `src/mission.cpp:590`, `src/mission.cpp:740`, `src/temp_crafting_inventory.cpp:457`, `src/visitable.cpp:455`, `src/character.h:3878`, `src/crafting.cpp:4489`

Three defects compound. (1) `temp_crafting_inventory::dump()` already visits nested items, and `get_all_item_group_matches()` recurses into containers again, so an item in a backpack counts twice (two levels deep, three times); stacks count as 1. `is_complete()` can succeed with half the required items. (2) `wrap_up()` turns every match into `item_comp(type, count)`, so it tries to consume all matching items in reach, not `type->item_count`. (3) The list goes to `Character::consume_items(const std::vector<item_comp>&)`, which picks one alternative (possibly through a "use which component?" menu) and consumes only that type. `remove_container` likewise consumes `item_count` containers whatever was actually used. No shipped mission uses this goal and it is missing from the docs' goal table, so only third-party mods hit it.

```cpp
// mission.cpp:762
if( itm->is_container() && !itm->is_container_empty() ) { ... get_all_item_group_matches( content, ... ) }
// mission.cpp:503, 508
for( std::pair<const itype_id, int> &cnt : matches ) { comps.emplace_back( cnt.first, cnt.second ); }
player_character.consume_items( comps );
```

**Fix:** Collect only top-level items (or skip into children after a match) and count with `count()`/charges. In `wrap_up`, consume exactly `item_count` in total by calling `consume_items` once per type with a single-element vector until the quota is met, or consume the matched item_locations directly.

### CAMP-B13 · Upgrade 'in_progress' is tracked but never read or cleared; the same blueprint can be built twice in parallel
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/basecamp.cpp:401`, `src/basecamp.cpp:431`, `src/basecamp.cpp:520`, `src/faction_camp.cpp:841`, `src/faction_camp.cpp:1957`, `src/faction_camp.cpp:3786`, `src/mission_companion.cpp:398`, `src/recipe.cpp:561`

`available_upgrades()` computes `in_progress` from `blueprint_excludes` and stores it in `basecamp_upgrade`, but `get_available_missions_by_dir()` reads only `upgrade.avail`. `update_in_progress()` only ever increments. Workers are matched with `is_equal()`, which compares `mapgen_args`, so each palette variant of one blueprint is a separate mission. Example: start "build SE bedroom walls" in Log with one worker and in Rock with another; both consume components. When the first finishes, the recipe is in provides and is skipped by `available_upgrades()`, so the second worker's return entry no longer appears. That worker is stranded until Emergency Recall, and their materials are wasted. Mutually exclusive expansions (`blueprint_excludes`) can also both be started. `emergency_recall`'s own comment assumes only one instance per construction is active, but nothing enforces that.

```cpp
// faction_camp.cpp:841
bool can_upgrade = upgrade.avail;   // in_progress never consulted
// mission_companion.cpp:402
first.mapgen_args == second.mapgen_args &&
```

**Fix:** Disable start entries when `upgrade.in_progress` is true. Decrement `in_progress` in `upgrade_return` and when the last worker of an upgrade is recalled. Match workers for the exclusion check on parameters only, ignoring `mapgen_args`.

### CAMP-B14 · distribute_items_to_npc_zones returns leftovers that every caller ignores, so items vanish
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctrade.cpp:317`, `src/npctrade_utils.cpp:141`, `src/npctrade_utils.cpp:178`, `src/npc.cpp:2387`, `src/mission_companion.cpp:1823`, `src/mission_companion.cpp:1921`, `src/trade_ui.cpp:139`

`distribute_items_to_npc_zones()` places an item only where there is enough free volume and `add_item_or_charges(..., overflow=false)` succeeds, and returns what it could not place. All four callers discard that list: shopkeeper escrow after a trade, `npc::shop_restock`, and the scavenging and hospital raid returns. Items that do not fit are deleted. The impact is limited. The player has already been paid, so only the NPC's stock or loot share is lost. Trade adds a fallback zone. The raid NPC has mapgen-placed LOOT_UNSORTED zones, so full loss needs full or out-of-range zones. Losing restock overflow is harmless.

```cpp
// npctrade.cpp:317
distribute_items_to_npc_zones( escrow, np );   // return value dropped
// npctrade_utils.cpp:173, 178
if( leftover ) { leftovers.emplace_back( it ); } ... return leftovers;
```

**Fix:** Mark the function `[[nodiscard]]`. In the callers, drop leftovers at the NPC's feet with overflow, or put them in the NPC's inventory. Call `add_fallback_zone(p)` before distributing raid loot.

### CAMP-B15 · Shop consumption counts a whole stack as 1 unit and truncates elapsed time to whole days
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npctrade_utils.cpp:46`, `src/npctrade_utils.cpp:57`, `src/npctrade_utils.cpp:61`, `src/npctrade_utils.cpp:198`, `src/npc.cpp:2307`

Consumption rates are documented as units per day, but `_consume_item` decrements the budget once per `item_location`, and `remove_item()` deletes the whole item. A stack of 1000 nails counts as one unit, so `{ "category": "ammo", "rate": 10 }` deletes ten entire ammo stacks per day. The budget uses `to_days<int>(elapsed)`, which truncates. With the default 6-day restock interval this rarely matters, but intervals under a day give zero consumption.

```cpp
// npctrade_utils.cpp:57, 61, 199
int const rate_init = rate >= 0 ? rate * to_days<int>( elapsed ) : -1;
consumed.push_back( elem ); it->second--;
it.remove_item();
```

**Fix:** Track the budget in `count()`: for count-by-charges items, reduce charges by `min(budget, charges)` instead of removing the stack. Compute the budget from `to_days<double>(elapsed)` with probabilistic rounding.

### CAMP-B16 · faction_template::add_kcal is broken, so legacy integer food_supply saves lose their larder
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:5858`, `src/savegame_json.cpp:4169`, `src/savegame.cpp:1975`, `src/faction.cpp:35`

`add_kcal()` only changes the larder inside a loop over existing entries, and its two branches disagree on units (`kcal * 1000` versus `kcal`). Its only caller is the legacy save migration. There, `faction_manager::deserialize` default-constructs a faction whose `_food_supply` the constructor clears, so the loop body never runs and the migrated food is always dropped. Only very old saves are affected.

```cpp
// faction_camp.cpp:5861, 5866, 5871
for( auto it = _food_supply.begin(); it != _food_supply.end(); ++it ) { ...
    it->second.calories += kcal * 1000; ...
    add.second.calories = kcal;
```

**Fix:** Replace `add_kcal` with `add_to_food_supply({{ expires, nutrients{ calories } }})`, which already merges correctly, and delete `add_kcal`.

### CAMP-B17 · Outpost jobs pay for all elapsed time with no cap; declining the crop sale skips the worker fee
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mission_companion.cpp:1941`, `src/mission_companion.cpp:2000`, `src/mission_companion.cpp:2067`, `src/mission_companion.cpp:1624`, `src/mission_companion.cpp:1627`, `src/mission_companion.cpp:1641`

`labor_return`, `carpenter_return` and `forage_return` pay 3, 5 or 4 merch for every hour since departure and run `1 + minutes/10` skill checks, with no cap and no food or sleep cost. A follower left on Menial_Job for a season returns with thousands of merch and maxed checks. The UI advertises "N merch/hour", so the rate is intended; round-the-clock accrual is the quirk. In `field_harvest`, the player must have `number_plots` FMCNote, but if they decline to sell the crop, `use_amount` is never called and the wage is never charged.

```cpp
// mission_companion.cpp:1941
float hours = to_hours<float>( calendar::turn - comp->companion_mission_time ); int merch_amount = 3 * hours;
// mission_companion.cpp:1641-1646 (keep-crop branch)
else { ... player_character.i_add_or_drop( tmp, number_plants ); }   // no use_amount( itype_FMCNote, number_plots )
```

**Fix:** Clamp paid hours to a shift length per day (e.g. 8-10 h) and scale training the same way. Call `use_amount(itype_FMCNote, number_plots)` when the player keeps the crop.

### CAMP-B18 · om_carry_weight_to_trips divides by the NPC's free carry weight, which can be zero or negative
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/faction_camp.cpp:5033`, `src/faction_camp.cpp:5024`, `src/faction_camp.cpp:2403`, `src/faction_camp.cpp:2479`, `src/faction_camp.cpp:2546`

After `start_mission`, the trip count is recomputed with `max_m = weight_capacity() - weight_carried()`, unclamped. `units::mass` division is int64 division, so exactly zero would raise SIGFPE when cutting logs or running hide-site trips. Exactly zero is vanishingly rare. An overburdened follower is more likely; the negative divisor then gives a small or non-positive trip count that `std::max` with the volume-based count mostly hides, so the result is an underestimated trip time rather than a crash.

```cpp
// faction_camp.cpp:5033, 5024
units::mass max_m = comp ? comp->weight_capacity() - comp->weight_carried() : 60_kilogram;
int trips_m = ( total_mass + carry_mass - 1_milligram ) / carry_mass;
```

**Fix:** Clamp before dividing, e.g. `max_m = std::max( max_m, 5_kilogram )`, and do the same for `max_v`.

## 3. Performance

### CAMP-P01 · Camp place_results saves the whole reality bubble and sweeps 441 tiles for every returned item
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:6198`, `src/faction_camp.cpp:6222`, `src/faction_camp.cpp:6224`, `src/faction_camp.cpp:6227`, `src/faction_camp.cpp:3736`, `src/faction_camp.cpp:4665`, `src/faction_camp.cpp:4724`, `src/faction_camp.cpp:3976`, `src/basecamp.cpp:870`, `src/map.cpp:8941`, `src/map.cpp:9324`, `src/map.cpp:10052`

`basecamp::place_results(const item&)` is called once per item: by `finish_return` for each item in `companion_mission_inv`, by `search_results` per successful gather roll, and by `make_corpse_from_group` per corpse. Each call rebuilds the storage zones, sums the dump tile's item volume (twice), runs `apply_camp_ownership` over 21x21 tiles calling `set_owner` on every item there, and calls `target_bay.save()`. In the bubble that is 11x11x21 `saven` calls, each running `on_unload` (cheap lookups, plus 144 `passable()` calls for player-modified submaps). A 3-day foraging trip yields about 216 rolls and 100+ items, so returns are noticeably slow. The reviewer estimates about a millisecond per item, i.e. a sub-second hitch per return.

```cpp
// faction_camp.cpp:6198-6224
void basecamp::place_results( const item &result ) {
    map &target_bay = get_camp_map();
    form_storage_zones( target_bay, bb_pos );
    ...
    target_bay.add_item_or_charges( new_spot, result, true );
    apply_camp_ownership( target_bay, new_spot, 10 );
    target_bay.save();
```

**Fix:** Add a batch overload `place_results( const std::vector<item>& )`. It should form zones once, place all items, call `set_owner` on each placed copy instead of sweeping 441 tiles, and save once (ideally only the affected submaps). Have `finish_return`, `search_results` and `make_corpse_from_group` collect results first and call it once.

### CAMP-P02 · Every trade toggle re-prices the whole selection from scratch (acknowledged FIXME)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/trade_ui.cpp:221`, `src/trade_ui.cpp:226`, `src/inventory_ui.cpp:5409`, `src/inventory_ui.cpp:5451`, `src/inventory_ui.cpp:4411`, `src/inventory_ui.cpp:4570`

`trade_selector::on_toggle` runs on every toggle, every count increase/decrease keypress and every bulk toggle. It calls `recalc_values_cpane`, which copies the selection (`to_trade()` returns by value) and recomputes `npc_trading::trading_price` for every selected entry. Each price includes `wants_to_*` checks, price-rule matching (each building a const_dialogue) and recursion into contents. Selecting n items one by one costs O(n²) full price evaluations, and holding a count key on a large selection lags.

```cpp
// trade_ui.cpp:221-229
void trade_ui::recalc_values_cpane() {
    _trade_values[_cpane] = 0;
    for( entry_t const &it : _panes[_cpane]->to_trade() ) {
        // FIXME: cache trading_price
        _trade_values[_cpane] += npc_trading::trading_price( *_parties[-_cpane + 1], *_parties[_cpane], it );
```

**Fix:** Cache the unit price per entry (the UI already computes it for the price cell) and multiply by count, or apply a delta for only the toggled entry. Return `to_trade()` by const reference.

### CAMP-P03 · Opening the camp board loads 84-submap smallmaps and runs farm mapgen just to build menu text
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/faction_camp.cpp:1183`, `src/faction_camp.cpp:1203`, `src/faction_camp.cpp:1226`, `src/faction_camp.cpp:3460`, `src/faction_camp.cpp:3476`, `src/faction_camp.cpp:3479`, `src/faction_camp.cpp:867`, `src/faction_camp.cpp:5402`, `src/faction_mission.cpp:133`, `src/basecamp.cpp:681`, `src/map.cpp:353`

Each board open builds every description eagerly, because `faction_mission::describe` calls `get_description()` immediately and the plot count is read right afterwards. For each farm expansion, the Plow, Plant and Harvest entries each call `farm_description -> farm_action`, and each of those constructs and loads a `smallmap` (2x2x21 = 84 submaps). The plow estimate also allocates a `small_fake_map` (84 fresh submaps), runs the OMT mapgen and replays every built upgrade's update_mapgen to count plots. Because mapgen can roll randomly, the estimate may not match the real job. Every map construction also allocates 21 full-bubble pathfinding caches. Smaller costs: `gathering_description` rolls and names 6 forest items, and `get_mission_workers` copies `npc_companion_mission` per worker, because `get_companion_mission()` returns by value.

```cpp
// faction_camp.cpp:1188-1191 (same pattern at 1209 plant, 1232 harvest)
entry = faction_mission_camp_plow->describe( npc_list.size(), nullptr,
    std::function( [this, &dir, &plots]() { return farm_description( dir, plots, farm_ops::plow ); } ) );
// faction_camp.cpp:3460-3461, 3476-3479
smallmap farm_map; farm_map.load( omt_tgt, false );
farm_json = std::make_unique<small_fake_map>(); ... if( !run_mapgen_func( omt_id, dat ) )
```

**Fix:** Compute all three farm counts in one `farm_action` pass with a single smallmap load, or reuse `get_map()` when the OMT is inside the bubble. Cache the plow target map per expansion, keyed by its provides set, or defer farm text until the entry is highlighted. Make `get_companion_mission()` return a const reference.

### CAMP-P04 · faction_manager::get (behind faction_id::obj() and item ownership checks) is a linear scan of a std::map
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction.cpp:543`, `src/faction.cpp:548`, `src/faction_camp.cpp:665`, `src/faction_camp.cpp:670`, `src/item.cpp:1181`, `src/game.cpp:10522`

`string_id<faction>::obj()` and `basecamp::fac()` (both in faction_camp.cpp) resolve through `faction_manager::get`, which iterates the whole `std::map<faction_id, faction>` and compares keys instead of calling `find()`. The map grows by one `solo_*` faction per randomly spawned NPC. The same lookup runs in `item::validate_ownership()` for every owned item queried (trade UI, shop zone filters, crafting inventory, camp storage). Most items have a null owner, the key compare is an int compare, and the callers are per action, so the gain is small, but the fix is trivial.

```cpp
// faction.cpp:548-549
for( auto &elem : factions ) {
    if( elem.first == id ) { ...
```

**Fix:** Use `auto it = factions.find( id ); if( it != factions.end() ) { /* existing lazy-validate block */ return &it->second; }`, and do the same in `remove_faction`.

### CAMP-P05 · Faction menu recomputes and re-sorts all its lists about 60 times per second while open
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction_ui.cpp:194`, `src/faction_ui.cpp:274`, `src/faction_ui.cpp:406`, `src/faction_ui.cpp:544`, `src/faction_ui.cpp:619`, `src/faction_ui.cpp:796`, `src/faction_ui.cpp:636`, `src/ui_manager.cpp:482`

`faction_ui::execute` sets a 16 ms input timeout, and ui_manager redraws every imgui adaptor on each redraw, so the draw functions run about 60 times per second while idle. Each frame they rebuild unchanging data. The Creatures tab sorts every known monster with `localized_compare` on freshly translated names. The faction tab runs `validate_camps()`, `find_camp` per camp and sums the food supply several times. The Followers tab runs `populate_followers_vec`, `can_radio_contact` (two `get_camps_near` walks), a localized sort of all skills, and `best_combat_skill` twice. This is idle CPU confined to an open menu. `mission_ui.cpp:131` has the same pattern with cheaper work.

```cpp
// faction_ui.cpp:194, 796-798
ctxt.set_timeout( 16 );
std::sort( creatures.begin(), creatures.end(), []( const mtype_id * a, const mtype_id * b ) {
    return localized_compare( a->obj().nname(), b->obj().nname() ); } );
```

**Fix:** Cache the per-tab lists as `faction_ui` members, built in `execute()` or on tab switch and after CONFIRM actions. Cache the selected follower's derived strings until the selection changes. Precompute translated creature names once, and call `best_combat_skill` once.

### CAMP-P06 · Shop restock redoes zone queries and tile volume sums per item (O(n²))
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc.cpp:2338`, `src/npc.cpp:2387`, `src/npctrade_utils.cpp:141`, `src/npctrade_utils.cpp:152`, `src/npctrade_utils.cpp:80`, `src/item_stack.cpp:99`, `src/map.cpp:5922`, `src/clzones.cpp:1097`, `src/clzones.cpp:1405`

`npc::shop_restock` runs on NPC load or trade open, once per restock interval (6 days by default). Free Merchant currency alone pushes 112-337 separate FMCNote items. For each item, `distribute_items_to_npc_zones` runs zone-type and `get_near` queries (`get_point_set` returns the faction's point set by value, so each query copies it), builds and shuffles a destination vector, then calls `free_volume()`, and `add_item_or_charges` re-sums the volume of every item on the tile. All notes go to one LOOT_CURRENCY tile, so this is O(n²) `item::volume()` calls. The path is cold, so the cost is a one-off hitch of a few to tens of milliseconds.

```cpp
// npctrade_utils.cpp:152-159
for( item const &it : items ) {
    zone_type_id const zid = zmgr.get_near_zone_type_for_item( it, loc_abs, PICKUP_RANGE, fac_id );
    dest_t dest = zid.is_valid() ? _get_shuffled_point_set( zmgr.get_near( zid, loc_abs, PICKUP_RANGE, &it, fac_id ) ) : dest_t();
// npctrade_utils.cpp:82
if( here.can_put_items_ter_furn( dpoint_here ) && here.free_volume( dpoint_here ) >= it.volume() ) {
```

**Fix:** Memoize the destination list per itype (or zone type) within one call. Keep a local map of remaining free volume per tile, computed once and decremented as items are placed. Insert currency as a stack, or in bulk.

### CAMP-P07 · icg_entry::matches builds a const_dialogue and evaluates the condition before cheap filters
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/shop_cons_rate.cpp:32`, `src/shop_cons_rate.cpp:220`, `src/shop_cons_rate.cpp:233`, `src/npc_class.cpp:415`, `src/faction.cpp:443`, `src/npctrade.cpp:182`, `src/creature.cpp:3720`

`icg_entry::matches` backs blacklists, whitelists, consumption rates and every price rule checked from `adjusted_price`. Each call heap-allocates two talkers for a `const_dialogue` even when the entry has no condition, and evaluates the condition before the near-free itype and category checks. Rule counts are small (free_merchants has 5), so this costs a few milliseconds per trade open at most.

```cpp
// shop_cons_rate.cpp:32-36
bool icg_entry::matches( item const &it, npc const &beta ) const {
    const_dialogue temp( get_const_talker_for( get_avatar() ), get_const_talker_for( beta ) );
    return ( !condition || condition( temp ) ) &&
           ( itype.is_empty() || it.typeId() == itype ) && ...
```

**Fix:** Check itype, then category, then item_group, and only then the condition, constructing the dialogue lazily and only when `condition` is set. Conditions are const, so the result is identical.

### CAMP-P08 · Trade UI evaluates price and permission about three times per entry
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/trade_ui.cpp:61`, `src/trade_ui.cpp:77`, `src/trade_ui.cpp:155`, `src/npctrade.cpp:211`, `src/npc.cpp:2187`, `src/npc_class.cpp:197`, `src/shop_cons_rate.cpp:32`

The price cell calls `trading_price`. `get_denial` calls `trading_price` again, then `wants_to_sell`/`wants_to_buy` a third time. `_trading_price` itself calls `wants_to_*` and recurses into pocket contents. For shopkeepers, `wants_to_sell` loops every shop group, calling `can_sell` (a new const_dialogue, item-independent) and, for failing groups, a recursive `group_contains_item` with string compares. Items in follower vehicles anywhere in the bubble are also listed. The reviewer notes that shopkeepers have at most 12 groups (most have 1-2), and that cells and denials are cached per entry, so this is a one-time cost when trade opens.

```cpp
// trade_ui.cpp:60-63, 77-81
append_cell( [&]( item_location const & loc ) {
    return format_money( npc_trading::trading_price( _trader, _u, { loc, 1 } ) ); }
int const price = npc_trading::trading_price( _trader, _u, { loc, 1 } ); ... np.wants_to_sell( loc, price );
```

**Fix:** Compute the price once per entry and reuse it in `get_denial`, or memoize `trading_price` per item_location for the session. Evaluate `can_sell` for all groups once per trade session. Precompute a `unordered_set<itype_id>` per shop group at finalize. Restrict follower-vehicle items to vehicles within PICKUP_RANGE.

## Appendix: considered and rejected

- Per-turn mission checks deep-copy the crafting inventory and build temporary items – no shipped mission uses MGOAL_FIND_ITEM_GROUP, `process_all` skips giver-less FIND_ITEM missions unless the item is present, and `is_complete` returns early during activities, so the cost is negligible. The double-counting part is real and is covered by CAMP-B12.
- `basecamp::any_claim_at` and `get_basecamp` use `find_camp`, which scans every loaded overmap 36 times – `overmap::camps` is empty in almost every overmap, so each lookup is a null-root check costing microseconds on cold paths. Not worth an entry; the asymmetric -3..+2 search window is a separate correctness note.

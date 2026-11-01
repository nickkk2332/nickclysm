# 04 · JSON loading, mods, save/load & options

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/init.cpp`, `src/generic_factory.cpp`, `src/json.cpp`, `src/json_loader.cpp`, `src/flexbuffer_cache.cpp`, `src/flexbuffer_json.cpp`, `src/mod_manager.cpp`, `src/dependency_tree.cpp`, `src/savegame.cpp`, `src/savegame_json.cpp`, `src/savegame_legacy.cpp`, `src/worldfactory.cpp`, `src/string_id.cpp`, `src/string_id_null_ids.cpp`, `src/translation.cpp`, `src/translations.cpp`, `src/translation_manager_impl.cpp`, `src/filesystem.cpp`, `src/zzip.cpp`, `src/zzip_stack.cpp`, `src/mmap_file.cpp`, `src/options.cpp`, `src/cata_variant.cpp`, `src/units.cpp`, `src/tgz_archiver.cpp`, `src/cata_utility.cpp`, `src/generic_factory.h`, `src/string_id.h`, `src/int_id.h`, `src/type_id.h`, `src/json.h`

## Summary

The loading pipeline works, but the rules for overriding, extending and removing content are inconsistent. They depend on whether a type uses `generic_factory`, which container a member uses, and the file order inside core. As a result, mod overrides can be silently dropped or reverted. One of these (deferred copy-from objects overwriting later mods) already affects shipped content. Several save-path bugs damage data for good: overmap predecessors are lost on every load, partial constructions on uniform submaps are never saved, and world sliders are mis-parsed in non-English locales. Mod-facing features such as world settings, migrations, removal and lifecycle hooks are thin. On the performance side, the largest easy win is the vsync-blocked redraw after every loading step, which costs about 3 s per world load.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 7 | 4 | 13 |
| Bugs & quirks | 0 | 3 | 6 | 5 | 14 |
| Performance | 0 | 1 | 5 | 9 | 15 |
| **Total** | 0 | 6 | 18 | 18 | 42 |

**Fix first:**
1. **DATA-B01** · Overmap predecessors are thrown away on every load. A one-line fix stops permanent damage to every save's unexplored terrain.
2. **DATA-B02** · World option sliders format floats with the UI locale. A one-line `imbue` stops German and French players from creating worlds with zero spawns.
3. **DATA-P01** · Every loading step does a vsync-blocked present. A small throttle saves about 3 s on every world load.
4. **DATA-B03** · Deferred copy-from objects overwrite later mod redefinitions. This already silently discards XedraWood's artifact overrides.
5. **DATA-J03** · Validators exist but are never run. Registering three functions turns a runtime crash in the butchery menu into a load-time error.

## 1. Missing JSON bindings & EOC features

### DATA-J01 · Mods cannot ship player-tunable settings: world options are C++-only, external options are hidden and global
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/main_menu.cpp:534`, `src/game_io.cpp:218`, `src/worldfactory.cpp:1581`, `src/worldfactory.cpp:2059`, `src/worldfactory.cpp:2081`, `src/worldfactory.cpp:2093`, `src/options.cpp:318`, `src/options.cpp:354`, `src/math_parser_diag.cpp:1914`, `src/options.cpp:2911`

Only `data/core` is loaded at the main menu, so world creation never sees an `option_slider` or option defined by a mod. The `WORLDGEN` sliders can only come from core. `EXTERNAL_OPTION` entries are created with `COPT_ALWAYS_HIDE` on a pseudo-page `external_options`. `WORLD::load_options` keeps only `world_default` options, so external options are session-global and can only be changed by editing the mod's JSON. `WORLD_OPTION` is undocumented, unused, and can only force an existing option to `"true"`. `game_option()` is read-only in EOCs: it has no assign function. Every real world setting (SPAWN_DENSITY, SEASON_LENGTH, ...) is hardcoded in `add_options_world_default`. As a result, Magiclysm cannot offer "mana regen x0.5–x4" at world creation, and each mod builds its own EOC settings menu on global vars (hunvre `mod_settings`, aftershock `options.json`).

```cpp
g->load_core_data();                                   // main_menu.cpp:534
if( osl.context() == "WORLDGEN" ) {                    // worldfactory.cpp:1582
thisOpt.hide = COPT_ALWAYS_HIDE;                       // options.cpp:354
if( opts.has_option( name ) && opts.get_option( name ).getPage() == "world_default" )  // worldfactory.cpp:2066
{ "game_option", { "g", 1, option_eval } },            // math_parser_diag.cpp:1914, no _ass
```

**Fix:** Extend `EXTERNAL_OPTION` with an optional menu block:
```json
{ "type": "EXTERNAL_OPTION", "name": "MAGIC_REGEN_MULT", "stype": "float", "value": 1.0,
  "menu": { "page": "world_default", "label": "Mana regen", "tooltip": "...", "min": 0.25, "max": 4, "step": 0.25 } }
```
After mods are selected in world creation, pre-parse only `EXTERNAL_OPTION`/`option_slider` objects from those mods (or a dedicated per-mod `options.json`). Persist menu-enabled external options in `worldoptions.json`. Give `WORLD_OPTION` the shape `{"name": .., "default": ..}` so it changes the default instead of forcing a value. Allow `game_option('X') = v` only for options flagged `"eoc_writable": true`, stored per world.

### DATA-J02 · Many core types bypass generic_factory: overrides silently ignored, nondeterministic, or must restate everything
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/npc.cpp:347`, `src/npc.cpp:544`, `src/faction.cpp:53`, `src/faction.cpp:518`, `src/faction.cpp:529`, `src/faction.cpp:550`, `src/effect.cpp:1535`, `src/effect.cpp:1640`, `src/vitamin.cpp:52`, `src/skill.cpp:160`, `src/mutation_data.cpp:79`, `src/npctalk.cpp:9453`, `src/npctalk.cpp:9572`, `doc/JSON/JSON_INHERITANCE.md:191`

Override rules depend on the type, and these types have no copy-from, extend or delete support:
1. `npc_template` is stored with `std::map::emplace`, so the **first** definition wins. No_Hope redefines vanilla `thug` and `bandit` with suffixes "Thug" and "Bandit", and the change is silently dropped.
2. `faction_template` is a vector with no dedupe. `create_if_needed` takes the last duplicate, but `faction_manager::get` re-validation and `add_new_faction` take the first, so a mod's change to a vanilla faction is reverted on validation. This is latent: no current mod does this.
3. `effect_type`, `vitamin`, `skill` and `mutation_category` rebuild the object from scratch. Changing one field means copying the whole definition, as MindOverMatter's `effects_edited.json` does.
4. `talk_topic` always appends responses, cannot delete or replace them, resets `portrait_override` on each extension (FIXME), and has an empty `check_consistency`.

The documentation calls its support list non-exhaustive, and its example constructor (JSON_INHERITANCE.md:191) does not match `gates.cpp:90`.

```cpp
static std::map<string_id<npc_template>, npc_template> npc_templates;  // npc.cpp:347
npc_templates.emplace( guy.idz, std::move( tem ) );                    // npc.cpp:544
effect_type new_etype; ... effect_types[new_etype.id] = new_etype;     // effect.cpp:1537/1640
```

**Fix:** Move `npc_template`, faction, `effect_type`, `vitamin`, `skill`, `mutation_category`, dream, speech and `vehicle_group` onto `generic_factory`, with `load()` honoring `was_loaded` through `optional()`/`mandatory()`. As a small stopgap, use `insert_or_assign` for `npc_templates` and de-duplicate faction templates by id with last-wins. For `talk_topic`, add `"delete": { "responses": [ { "text": "..." } ] }` and `"replace_responses": true`, and overwrite `portrait_override` only when the member is present. Generate the JSON_INHERITANCE support table from a registry next to `init.cpp`'s `add()` calls.

### DATA-J03 · Validators exist but are never run; a missing butchery size crashes at runtime
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/init.cpp:954`, `src/butchery_requirements.cpp:59`, `src/butchery_requirements.cpp:92`, `src/butchery_requirements.cpp:116`, `src/magic_type.cpp:106`, `src/martialarts.cpp:360`, `src/martialarts.cpp:601`, `src/npctalk.cpp:9572`, `src/butchery.cpp:1197`, `src/activity_actor.cpp:13826`

`butchery_requirements::check_consistency`, `magic_type::check_consistency` and `ma_technique::verify_ma_techniques` are never registered in `DynamicDataLoader::check_consistency`, and nothing else calls them. `json_talk_topic::check_consistency` is an empty TODO. Concrete failure: `butchery_requirement::load` only rejects too **many** size entries. A modded requirement with 4 sizes loads, and `get_fastest_requirements` then calls `.at( size )`. That throws `std::out_of_range` when the butcher menu opens on a corpse of the missing size, and nothing catches it, so the game crashes. Magic-type formulas with the wrong parameter count and techniques without attack vectors also go unreported.

```cpp
if( riter.second.at( size ).at( butcher )->can_make_with_inventory(   // butchery_requirements.cpp:116
void ma_technique::verify_ma_techniques() { ma_techniques.check(); }  // martialarts.cpp:360, no caller
```

**Fix:** Add `{ _( "Butchery requirements" ), &butchery_requirements::check_consistency }`, `{ _( "Magic types" ), &magic_type::check_consistency }` and `{ _( "MA techniques" ), &ma_technique::verify_ma_techniques }` near `init.cpp:957`. Move the butchery size-count check into `load()` and throw there. Add a test that flags static check functions not referenced from `init.cpp`. Validate talk_topic `next_topic` ids against the JSON topics plus the hardcoded topics.

### DATA-J04 · mandatory() forbids relative/proportional even when the value was inherited via copy-from
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/generic_factory.h:665`, `src/generic_factory.cpp:9`, `src/mutation_data.cpp:304`, `src/profession.cpp:313`, `src/scenario.cpp:84`, `src/trap.cpp:124`, `src/missiondef.cpp:131`, `src/missiondef.cpp:132`, `src/mood_face.cpp:63`, `src/speed_description.cpp:57`

The reader-less `mandatory()` always calls `warn_disabled_feature` for extend, delete, relative and proportional, and that function throws, whether or not `was_loaded` is set. A copy-from object whose mandatory field is already inherited therefore cannot adjust that field. `{"id":"STRONG_X","copy-from":"X","relative":{"points":2}}` hard-errors for mutations, professions and scenarios (`points`), traps (`difficulty`), missions (`difficulty`, `value`), mood_face and speed_description (`value`). Modders must hard-code absolute values, which then drift when the base is rebalanced. The overload that takes a reader is not affected.

```cpp
warn_disabled_feature( jo, "relative", name, "disabled for mandatory" );   // generic_factory.h:668-671
```

**Fix:** In `mandatory()`, when `was_loaded` is true, take the `optional()` path (`jo.read || handle_proportional || handle_relative`, then `handle_extend` and `handle_delete`). Throw only when `!was_loaded`.

### DATA-J05 · var_migration only covers creature/global vars and leaks across worlds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame.cpp:1907`, `src/savegame.cpp:1779`, `src/savegame_json.cpp:4294`, `src/savegame_json.cpp:3291`, `src/savegame_json.cpp:3480`, `src/savegame_json.cpp:3854`, `src/global_vars.h:65`, `src/init.cpp:645`, `src/main.cpp:933`

`var_migration` is applied only in `Creature::load` and `global_variables::unserialize`. Item vars and vehicle `values` are never migrated. Items instead rely on a hardcoded `removed_item_vars` set and a manual `npctalk_var_` prefix loop. Omitting `"to"` renames the variable to a literal `"NULL_VALUE"` key instead of erasing it (documented, but still a wart; see DATA-B13). The migrations map is never cleared: it is absent from `unload_data`, and `g` lives for the whole session. A var_migration from a mod loaded earlier therefore rewrites another world's save. There is no scope selector and no prefix support. Example: a mod that renames item var `charge_level` to `mana_charge` has no JSON way to upgrade existing saves. Today only core uses var_migration, so the cross-world leak has little practical impact.

```cpp
const std::string to = jo.has_string( "to" ) ? jo.get_string( "to" ) : "NULL_VALUE";   // savegame.cpp:1910
std::map<std::string, std::string> migrations;                                            // global_vars.h:65, never reset
```

**Fix:**
```json
{ "type": "var_migration", "from": "foo", "to": "bar",
  "scope": [ "global", "character", "monster", "item", "vehicle", "faction" ] }
```
An absent `"to"` should erase the variable. Apply migrations in `item::io`, `vehicle::deserialize` and `faction::deserialize`. Move `removed_item_vars` into `data/json/obsoletion` as var_migration entries. Clear migrations in `unload_data`. Optionally accept `"prefix": true`.

### DATA-J06 · load_is_bool trait is broken, so mission_type deferral is dead code
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/generic_factory.h:288`, `src/generic_factory.h:293`, `src/generic_factory.h:310`, `src/generic_factory.h:353`, `src/generic_factory.h:365`, `src/missiondef.cpp:127`, `src/missiondef.cpp:179`, `src/mission_util.cpp:493`, `src/mapgen.cpp:7692`

`load_is_bool` tests `std::declval<LT &>().load()`, a call with no arguments. No type has such a function, so the trait is always false and `generic_factory` always uses the void overload. That overload discards `load()`'s return value and inserts the object. `mission_type::load` returns false to request deferral when an inline `update_mapgen` cannot resolve yet, but the half-loaded mission is inserted anyway. Its start/end/fail phases are left unset and the fields after `fail` are not parsed. Even with a matching signature, `void_t<is_same<...>>` is always well-formed, so the trait would be true for any return type. This is latent in core (no core mission hits a forward reference) but a real bug for mods. The failure branch at `mapgen.cpp:7692` also drops `defer`.

```cpp
struct load_is_bool<LT, std::void_t<std::is_same<decltype( std::declval<LT &>().load() ), bool>>> :  // :293
def.load( jo, src ); insert( def );                                                                    // :365-366
```

**Fix:** Use `std::enable_if_t<std::is_same_v<decltype( std::declval<LT&>().load( std::declval<const JsonObject&>(), std::declval<std::string_view>() ) ), bool>>` and add `static_assert( load_is_bool<mission_type>::value )`. Propagate `defer` in `add_mapgen_update_func`'s failure branch.

### DATA-J07 · extend/delete/relative/proportional on container members are inconsistent; no way to change one map entry
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/generic_factory.h:1236`, `src/generic_factory.h:1269`, `src/generic_factory.h:1336-1345`, `src/generic_factory.h:1513-1519`, `src/generic_factory.h:1586-1601`, `src/generic_factory.h:1710-1719`, `src/item_factory.cpp:3771`, `src/item_factory.cpp:3775-3776`, `src/stomach.h:108`

What extend does depends on the container. On `std::map`/`unordered_map` members, extending an existing key debugmsgs and then throws "insert failed". Weighted lists replace the entry silently through `add_or_replace`, and vectors append duplicates. The reader always applies relative, then extend, then delete, and this order is undocumented. So delete-plus-extend cannot replace an entry. On maps the extend throws first. On weighted lists the extend replaces the entry and the delete then removes it. `proportional` throws "not implemented" for every container. `relative` is unsupported for vectors, weighted lists, and map values without `+=`. Example: a "half ration" made with copy-from and `"proportional": {"vitamins": 0.5}` errors, and changing one vitamin of a vanilla food via extend or relative also errors, because the value type is `std::variant<int, mass>`. Numeric maps such as `addiction_type` can use `relative` as a workaround. Otherwise the whole member must be restated.

```cpp
if( !container.emplace( data ).second ) { debugmsg( "Insert of <%s, %s> failed, key already exists", // :1340
warn_disabled_feature( jo, "proportional", member_name, "not implemented" );                      // :1586
return !!container.add_or_replace( data.first, data.second );                                     // :1269
```

**Fix:** Run delete before extend, in both the container and non-container paths. Make map extend use `insert_or_assign`, or add `"replace": { "member": { key: value } }`. Implement `proportional` for maps and weighted lists whose value type supports `*= float`, accepting either a scalar or `{key: factor}`. Implement `relative` on weighted-list weights. Document the per-container behavior in JSON_INHERITANCE.md.

### DATA-J08 · No JSON migration for monsters, activities or EOCs; removed ids degrade badly
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:2392`, `src/savegame_json.cpp:419`, `src/savegame_json.cpp:1207`, `src/savegame.cpp:315`, `src/effect_on_condition.cpp:187`, `src/generic_factory.h:510`, `doc/JSON/OBSOLETION_AND_MIGRATION.md:197`, `doc/JSON/OBSOLETION_AND_MIGRATION.md:377`

Most content types have a migration type, but three do not:
- A monster with an unknown typeid becomes `mon_breather`. That fallback is intentional, but there is no rename path, so a mod renaming `mon_foo` turns every saved one into a breather.
- Activity obsoletion is a hardcoded `std::set`.
- Queued EOC ids are loaded unchecked, and `process_new_eocs` dereferences `eoc->global` before `is_valid()`, so every stale queued EOC from a removed mod raises a debugmsg on load.

The doc also contradicts itself: line 377 says spells have no migration infrastructure, but `spell_migration` exists (doc :181, `init.cpp:498`).

```cpp
if( global_queue == eoc_queue.top().eoc->global ) { if( eoc_queue.top().eoc.is_valid() ) {  // effect_on_condition.cpp:187
std::set<std::string> obs_activities { "ACT_EAT_MENU", ...                                  // savegame_json.cpp:419
```

**Fix:** Add `{"type":"monster_migration","from":"mon_foo","to":"mon_bar"}`, where `"to": null` despawns instead of creating a breather. Add `{"type":"activity_migration",...}` to replace `obs_activities`, and `{"type":"effect_on_condition_migration","from":"..","to":".."|null}`. Apply them in `monster::load`, `player_activity::deserialize` and both queued-EOC loaders. Check `is_valid()` before any dereference. Fix the spell paragraph in the doc.

### DATA-J09 · No generic way for mods to remove or disable an object; blacklists are per-type and some are last-one-wins
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/generic_factory.h:311`, `src/generic_factory.h:354`, `src/recipe.cpp:314`, `src/skill.cpp:214`, `src/scenario.cpp:363`, `src/profession.cpp:109`, `src/init.cpp:299`, `src/init.cpp:323`, `doc/MODDING.md:283`

`generic_factory::load` has no removal path. Only recipes, skills and MOD_INFO read `"obsolete"`. Items, monsters, traits, scenarios and professions each have their own blacklist format. Everything else (overmap_special, EOC, SPELL, bionic, martial_art, map_extra, start_location, ...) has to be neutered with copy-from hacks; MODDING.md:283 suggests setting `occurrences` to `[0,0]`. `SCENARIO_BLACKLIST` and `profession_blacklist` are single-instance: a later list silently resets the earlier one, logging only at D_INFO. Core ships a scenario blacklist that hides the placeholder `defense_mode_fortified`, and any mod blacklist (for example MindOverMatterNoKnacks) resets it, so the placeholder very likely becomes selectable. Whitelist mods (MA, Backrooms, aftershock) override each other.

```cpp
if( !scenarios.empty() ) { DebugLog( D_INFO, DC_ALL ) << "Attempted to load scenario blacklist with an existing scenario blacklist, resetting blacklist info"; reset_scenarios_blacklist(); }  // scenario.cpp:365
```

**Fix:** Add generic `generic_factory` support for `{"type":"X","id":"foo","obsolete":true}`, which keeps the object for save compatibility but marks it hidden and unspawnable (exposed as `T::obsolete`). Add `{"type":"X","id":"foo","delete":true}` as well, or instead, applied at finalize, with `check()` reporting dangling references. Make scenario and profession blacklists merge (union of blacklists, intersection of whitelists) and debugmsg when subtypes are mixed.

### DATA-J10 · EXTERNAL_OPTION and option_slider names are never validated; shipped mods set options nothing reads
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/worldfactory.cpp:2106`, `src/options.cpp:149`, `src/options.cpp:186`, `src/options.cpp:236`, `src/options.cpp:4379`, `src/options.cpp:1059`, `src/options.cpp:1102`, `src/worldfactory.cpp:346`, `src/timed_event.cpp:343`, `src/monattack.cpp:2703`, `data/core/external_options.json:137`, `data/core/external_options.json:221`, `doc/JSON/JSON_INFO.md:4588`, `doc/JSON/NPCs.md:1438`

`load_external_option` creates any unknown name, and nothing checks that the option is read anywhere. Four options are dead: CBM_SLOTS_ENABLED (core, XedraWood, aftershock_exoplanet), DISABLE_ROBOT_RESPONSE (classic_zombies), OVERMAP_PLACE_HIGHWAYS (rural_biome) and DEFAULT_REGION (Isolation-Protocol). All four are harmless in practice: the ROBOT_ATTACK event is itself a no-op, rural_biome is obsolete, and Isolation-Protocol sets its region through `dimensions.json`. `option_slider::check` validates only level numbering. `apply_opts` skips unknown option names, and out-of-range values silently snap to the default. The skip-hidden guard in `WORLD::save` tests `getDefaultText().empty()`, which is never true for typed options, so mod-forced values can be persisted (see DATA-B14). Two doc examples are stale: JSON_INFO.md:4588 uses the renamed MONSTER_UPGRADE_FACTOR, and NPCs.md:1438 uses the removed NPC_SPAWNTIME.

```cpp
if( !opts.has_option( name ) ) { opts.add_external( name, "external_options", stype ); }   // worldfactory.cpp:2106
auto iter = OPTIONS.find( opt._opt ); if( iter != OPTIONS.end() ) { iter->second.setValue( opt._val ); }  // options.cpp:190
```

**Fix:** Require declaration: only an `EXTERNAL_OPTION` with `"declare": true` (in core or the owning mod) may create a name, and any other unknown name is a load error. In `option_slider::check`, verify that each option exists, its type matches, its value is in range and its context is known, and apply `migrateOptionName`. Add a test that flags declared options never read by `get_option` or `game_option`. Implement or remove the four dead options and fix the two doc examples.

### DATA-J11 · Holidays are hardcoded (enum, date ranges, Easter table ending 2030) and invisible to EOCs
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/cata_utility.cpp:775`, `src/cata_utility.cpp:803`, `src/cata_utility.cpp:857`, `src/enums.h:108`, `src/item_factory.cpp:4982`, `src/mongroup.cpp:487`

The holiday set, its date windows (based on the real-world clock) and an Easter table for 2021–2030 are all in C++. After 2030 Easter is never detected. Mods cannot add holidays, and EOCs cannot query them. The only consumers are the `event` field of item groups and monster groups, and the title art.

```cpp
switch( year ) { case 2024: return day == 31; ... case 2030: return day == 21; default: break;   // cata_utility.cpp:777
```

**Fix:**
```json
{ "type": "holiday", "id": "halloween", "from": "10-19", "to": "10-31" }
{ "type": "holiday", "id": "easter", "rule": "easter" }
```
Compute `"rule": "easter"` with the computus algorithm. Add an EOC condition `{ "is_holiday": "halloween" }` and a math/string `holiday()`, and let `event` fields reference holiday ids.

### DATA-J12 · Pregenerated-overmap loader hardcodes lake/ocean/forest terrain ids instead of reading region settings
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/savegame.cpp:60`, `src/savegame.cpp:874`, `src/savegame.cpp:909`, `src/savegame.cpp:939`, `src/savegame.cpp:969`, `src/regional_settings.h:277`, `src/regional_settings.cpp:493`, `src/overmap_water.cpp:357`, `src/overmap_water.cpp:431`

`overmap::unserialize_omap` (used for `OVERMAP_PREGENERATED_PATH`) classifies and writes tiles with hardcoded `lake_*`, `ocean_*` and `forest*` ids. It ignores `region_settings_lake::surface/shore/interior/bed`, which the normal generator honors. A region with custom lake terrain loaded from a pregenerated map gets vanilla lakes. This is a legacy shim used only by the MA mod, which disables lakes and ocean, so no current consumer is affected. Ocean oters are hardcoded in normal generation too.

```cpp
ter_set( p, shore ? oter_lake_shore : oter_lake_surface );   // savegame.cpp:909
```

**Fix:** Use `settings_lake.surface/shore/interior/bed` for both classification and writing. Add the same four fields to `region_settings_ocean`, and `forest_oter`/`forest_thick_oter` to the forest settings, and use them here and in overmap generation.

### DATA-J13 · Mod metadata half-used: version is display-only, no version persistence or load-time re-validation
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mod_manager.cpp:311`, `src/mod_manager_ui.cpp:54`, `src/mod_manager.cpp:452`, `src/game_io.cpp:529`, `src/worldfactory.cpp:198`, `src/condition.cpp:2669`, `src/math_parser_diag.cpp:1927`, `src/game_io.cpp:463`

MOD_INFO `"version"` is read but only displayed. `mods.json` stores bare ids, so a save does not record which mod version created it. Dependencies and conflicts are enforced only at world creation and in `mod_ui::try_add`. `load_world_modfiles` explicitly ignores them (see also DATA-B09). There are no version constraints and no optional or load-after dependencies. EOCs can query `mod_is_loaded` and `mod_load_order`, and upgrade logic is possible through `game_load` plus a guard variable (shipped mods already do this), but no mod version is exposed to EOCs.

```cpp
optional( jo, false, "version", modfile.version );   // mod_manager.cpp:311, only consumer: mod_manager_ui.cpp:54
json.write( world->active_mod_order );              // mod_manager.cpp:452
```

**Fix:** Store `[ { "id": "..", "version": ".." } ]` in `mods.json` and keep reading the legacy format. At load, emit new event types `mod_added { mod_id }` and `mod_version_changed { mod_id, old_version, new_version }`, usable as an EOC `required_event`. Add a math/string `mod_version('id')`. Add MOD_INFO `"optional_dependencies"` and `"load_after"`. Re-validate dependencies and conflicts on world load, with a prompt.

## 2. Bugs, oversights & quirks

### DATA-B01 · Overmap predecessors are thrown away on every load (fork regression from ff91f4a3)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame.cpp:806-814`, `src/savegame.cpp:1533-1535`, `src/mapgen.cpp:6316-6327`, `src/overmap.cpp:412-419`, `src/mapgendata.cpp:73`

`overmap::unserialize` always inserts an empty vector for each `"predecessors"` entry. `om_predecessors` is empty when it is moved into the map, and the serialized list is moved into it only afterwards and then cleared. Commit ff91f4a3 ("remove 0.H compatibilities") deleted the block that filled the vector. Trigger: generate a world, save, reload. Every OMT that has not been generated yet loses its predecessor chain. When the player reaches a road, trail, stream or special using `expects_predecessor` (90+ mapgens), mapgen draws `fallback_predecessor_mapgen` (usually `field`) instead of forest, swamp or lake shore. The next save writes the empty list back, so the loss is permanent.

```cpp
std::vector<oter_id> om_predecessors;
for( auto& [p, serialized_predecessors] : flattened_predecessors ) {
    predecessors_.insert_or_assign( p, std::move( om_predecessors ) );   // always empty
    om_predecessors = std::move( serialized_predecessors );
    om_predecessors.clear();
}
```

**Fix:** Replace the loop body with `predecessors_.insert_or_assign( p, std::move( serialized_predecessors ) );`. Add a save/load round-trip test that checks `overmap::predecessors()` for a road placed over forest.

### DATA-B02 · World option sliders apply float values formatted with the UI locale (spawn density 0.8 becomes 0 in German)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/options.cpp:133-145`, `src/options.cpp:1125-1134`, `src/options.cpp:4418-4500`, `src/main.cpp:716-726`, `src/worldfactory.cpp:1847`, `data/core/world_option_sliders.json:16`

`option_slider_level::deserialize` converts slider values to text with a default `std::stringstream`, which uses the global locale. That locale is set to the UI language (de_DE, fr_FR, ru_RU, ...) before core data loads, so `0.8` is written as `"0,8"`. `cOpt::setValue(string)` parses with a classic-locale stream, reads `0`, and stops at the comma with the stream still good. Trigger: a German or French player moves the world difficulty slider. SPAWN_DENSITY becomes 0 (no monsters), EVOLUTION_INVERSE_MULTIPLIER 1.5 becomes 1, and ITEM_SPAWNRATE 0.75 becomes 0, which is out of range and resets to the default. Integers of 1000 or more would also break, because of thousands grouping.

```cpp
std::stringstream valss;
...
} else if( stype == "float" ) {
    valss << jobj.get_float( "val" );          // options.cpp:135-139
```

**Fix:** Call `valss.imbue( std::locale::classic() );` right after constructing the stream. Better, store typed values (or the raw JSON) in `opt_slider_option` so they never round-trip through text. Check the other in-scope `std::stringstream` producers for the same issue.

### DATA-B03 · Deferred copy-from objects re-insert at finalize and overwrite later mods' redefinitions; missing parent reported as "circular dependency"
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/generic_factory.h:256-265`, `src/generic_factory.h:392-412`, `src/generic_factory.h:417-420`, `src/init.cpp:186-215`, `src/mod_tracker.h:74-81`, `src/game_io.cpp:534`, `src/game_io.cpp:544-558`, `src/filesystem.cpp:323`, `data/json/artifact/artifact_item_types.json:281`, `data/mods/XedraWood/items/artifacts.json:3`

If an object's copy-from parent has not loaded yet, the JSON is deferred and replayed only in `finalize()`, after **all** mods have loaded. On replay `insert()` replaces any entry with the same id, and `check_duplicate_entries` only throws when both definitions come from the same source. So when core object X is deferred and mod B later fully redefines X, the core X overwrites B's version at finalize with no warning. This hits shipped content. Core is loaded breadth-first, so `data/json/artifact/` loads before `items/armor` and `items/melee`, and core `art_robe`, `art_spear`, `art_sword`, `art_gloves` and `art_boots` are deferred. XedraWood's redefinitions of those ids load first and are then overwritten, so XedraWood's changed descriptions are lost. About 535 core objects are deferred this way (about 218 ITEM, 147 MONSTER). Separately, a typo in a copy-from target keeps re-deferring and is finally reported as "JSON contains circular dependency, this object is discarded", without naming the missing id.

```cpp
def.was_loaded = false; deferred.emplace_back( jo, src ); jo.allow_omitted_members(); return false;  // generic_factory.h:262
virtual void finalize() { DynamicDataLoader::get_instance().load_deferred( deferred ); ...           // :417-420
if( n.src.back() == o.src.back() ) { throw mod_error(                                               // mod_tracker.h:78
```

**Fix:** Drain every factory's deferred list at the end of each pack in `game::load_packs`, so deferral never crosses a mod boundary, and report anything still unresolved. Alternatively, stamp each insert with a load sequence number and skip a deferred re-insert when a newer definition exists. In `load_deferred`, report `copy-from target "<id>" not found` when the parent is absent from both the map and `abstracts`.

### DATA-B04 · mod_migration "new_id" (mod rename) never takes effect: insert/erase on a stale iterator; doc uses wrong key
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mod_manager.cpp:117-131`, `src/mod_manager.cpp:483-489`, `src/mod_manager.cpp:508`, `src/game_io.cpp:546-549`, `doc/JSON/OBSOLETION_AND_MIGRATION.md:431-439`, `doc/JSON/OBSOLETION_AND_MIGRATION.md:453`

`check_mods_list` calls `amo.insert( check_it, new_id )`, which invalidates `check_it`. Without a reallocation the iterator now points at the new id, so `amo.erase( check_it )` removes the replacement and keeps the old invalid id. With a reallocation the behavior is undefined. `changed` is still set, so the unchanged list is saved without prompting, and `load_packs` then silently skips the invalid id: the renamed mod's content is missing from old worlds. `amo.erase( check_it-- )` also decrements `begin()` when the first entry is removed. This is latent, because no shipped `mod_migration` uses `new_id`, but the documented feature is broken outright. The doc example also uses `"removal_message"`, while the loader reads `"removal_reason"`.

```cpp
amo.insert( check_it, replace_it->second );
amo.erase( check_it );        // erases the just-inserted id
changed = true;               // mod_manager.cpp:484-489
```

**Fix:** Use `*check_it = replace_it->second;` (keeping the duplicate check). Rewrite the loop as `for( auto it = amo.begin(); it != amo.end(); )` with `it = amo.erase( it )` on removal. Add a TEST_DATA mod_migration with `new_id` and a unit test. Fix the doc key, or accept both keys.

### DATA-B05 · Several registries are never reset or finalized: mod content leaks between worlds, deferred copy-from objects vanish
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/init.cpp:645-800`, `src/game.cpp:695`, `src/game_io.cpp:222`, `src/profession_group.cpp:10-38`, `src/end_screen.cpp:24-43`, `src/fault.cpp:38-40`, `src/fault.cpp:91-108`, `src/overmap_worldgen.cpp:203-255`, `src/faction_mission.cpp:69`, `src/faction_mission.cpp:113-119`, `src/martialarts.cpp:95`, `src/martialarts.cpp:670`, `src/savegame_json.cpp:3224-3242`, `src/savegame.cpp:1907-1914`, `src/global_vars.h:65`, `src/mutation_type.cpp:12`, `src/generic_factory.h:419`

`unload_data` runs on every world load, but it does not reset:
- the `profession_group`, `end_screen`, `fault_group`, `dimension` and `dimension_region_layout` factories;
- `attack_vector` (its `reset()` exists but is never called);
- the var_migration map, `mutation_types`, and the `charge_removal_blacklist` and `temperature_removal_blacklist` tables.

`init.h:46-48` says every type must supply a reset. Visible effect: load a Magiclysm world and then a vanilla world in the same session. `finalize_martial_arts` iterates the leaked `vector_ravenfolk_wing_slap`, dereferences the now-reset `wing_ravenfolk_l` body part (debugmsg), and re-appends limbs on every reload. Xedra end screens, Aftershock profession groups and var renames (which modify the new save) also leak. Also, `profession_group`, `end_screen`, `fault_group`, `dimension_region_layout` and `faction_mission` are never `finalize()`d. Finalize is the only place deferred copy-from objects load, so any such object whose parent appears later is silently dropped, because `allow_omitted_members()` suppresses the error. That second part is latent: no shipped object of these types uses copy-from.

```cpp
DynamicDataLoader::get_instance().load_deferred( deferred );   // generic_factory.h:419, only in finalize()
get_globals().migrations.emplace( from, to );                  // savegame.cpp:1913, never cleared
```

**Fix:** Add `reset()` and `finalize()` calls for these factories in `unload_data` and `finalize_loaded_data`. Clear `get_globals().migrations`, `mutation_types` and both removal blacklists. To prevent recurrence, have each `generic_factory` register itself in a list that `DynamicDataLoader` resets and finalizes automatically, debugmsg if `deferred` is non-empty at reset, and add a test that loads TEST_DATA, unloads it and asserts every registry is empty.

### DATA-B06 · Partial constructions (and other submap state) on uniform submaps are never saved
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:5115-5119`, `src/map.cpp:7243-7256`, `src/mapbuffer.cpp:282-296`, `src/submap.h:291-294`, `src/construction.cpp:1458`, `src/mapgen.cpp:6864`

For a uniform submap, `submap::store` returns right after writing the terrain. Partial constructions, spawns, computers, camp, cosmetics and phase reverts are never written. `mapbuffer::save_quad` skips a quad entirely when all four submaps are uniform. `map::partial_con_set` and `map::add_spawn` never call `ensure_nonuniform`. Lake surface (`t_water_dp`) and open air are uniform OMTs. Trigger: start the 12-hour `constr_dock_deep_pile` on deep lake water, or `constr_skylight_frame_open_air`, then save or leave the area. The progress and the consumed components (beams, spikes, 2x4s, nails) are lost.

```cpp
if( is_uniform() ) {
    _write_rle_terrain( jsout, uniform_ter.id().str(), SEEX * SEEY );
    jsout.end_array();
    return;                                            // savegame_json.cpp:5115-5119
}
```

**Fix:** Call `current_submap->ensure_nonuniform()` in `partial_con_set`, `add_spawn`, `set_computer` and the camp/cosmetic setters. Alternatively, treat a submap as non-uniform for saving whenever any of those collections is non-empty.

### DATA-B07 · World compression toggle: dictionaries deleted mid-loop (crash), originals deleted after unchecked writes, half-converted worlds
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/worldfactory.cpp:2161-2163`, `src/worldfactory.cpp:2293-2294`, `src/worldfactory.cpp:2325`, `src/worldfactory.cpp:2426-2428`, `src/zzip.cpp:422-423`

Decompressing a world deletes `maps.dict`, `overmaps.dict` and `mmr.dict` inside the per-dimension loop. A later dimension (the root world folder is processed last) can then map a deleted dictionary. `zzip::load` dereferences the null result without a check and crashes, leaving the world half-decompressed. This needs a specific layout, because cached zstd contexts avoid the reload. Compressing copies the dictionaries in first, so a failure partway leaves `maps.dict` present: the world then reports itself as compressed, and the uncompressed map data is ignored and regenerated. `save_zzip->add_file()` is unchecked, and the original `.sav` is queued for deletion even if the write failed.

```cpp
std::shared_ptr<const mmap_file> dictionary_file = mmap_file::map_file( dictionary_path );
dictionary.resize( dictionary_file->len() );                // zzip.cpp:422-423, no null check
save_zzip->add_file( save_file_name, savefile_contents ); files_to_clean.push_back( save );  // worldfactory.cpp:2293
```

**Fix:** Move the dictionary removal after the dimension loop. Null-check `dictionary_file` in `zzip::load` and return `{}`. Copy the dictionaries into the world only after all compression has succeeded. Check `add_file`'s result before scheduling originals for deletion.

### DATA-B08 · zzip_stack dereferences an empty optional when a zzip fails to open
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/zzip_stack.cpp:148-149`, `src/zzip_stack.cpp:251-271`, `src/zzip_stack.cpp:336-348`, `src/zzip.cpp:355-371`, `src/game_io.cpp:613-615`

`zzip::load` returns an empty optional when the mmap fails. It already retries for the case where "OneDrive or antivirus is interfering". `zzip_stack::cold()`, `warm()` and `hot()` store the result and return `*cold_` (etc.) unchecked. When a map-memory file is locked or the disk is full, saving or loading map memory is undefined behavior, usually a crash, instead of an error. `game::save_player_data` also calls `z->add_file` on an unchecked optional, and `extract_to_folder` does not check the stack.

```cpp
zzip &zzip_stack::cold() const {
    if( !cold_ ) { cold_ = load_temp( file_temp::cold ); }
    return *cold_;                                          // zzip_stack.cpp:252-257
}
```

**Fix:** Make `cold`/`warm`/`hot` return `zzip *` (or throw `std::runtime_error` with the path), and propagate the failure in `add_file`, `get_file`, `compact` and `delete_files`. Check the optional in `game::save_player_data`.

### DATA-B09 · Mod dependency ordering is not a topological sort, and dependencies are never re-checked on world load
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/dependency_tree.cpp:156-194`, `src/mod_manager_ui.cpp:128-163`, `src/game_io.cpp:190-195`, `src/game_io.cpp:529-533`, `src/mod_manager.cpp:474-516`

`get_dependencies_as_nodes` records nodes in DFS pre-order and then reverses the list, which is not a valid topological order. Example: M depends on `["dda","A","B"]` and A depends on B. The result is `[A, dda, B]`, so `try_add` builds `[dda, A, B, M]` and A loads before its dependency B. `--check-mods` collects dependencies in a `std::set`, so they load alphabetically. On world load nothing is validated: `load_world_modfiles` assumes the order is correct, and `check_mods_list` handles only missing or migrated mods. A mod update that adds a dependency or conflict therefore loads silently into old worlds. No in-tree mod currently has an out-of-order non-core chain, but load order decides which override wins.

```cpp
std::set<mod_id> dep_set( dep_vector.begin(), dep_vector.end() );            // game_io.cpp:192
// this code does not care about mod dependencies, it assumes that those dependencies are static   // game_io.cpp:529
```

**Fix:** Implement a post-order DFS that emits each node after its parents. In `check_mods_list`, validate `active_mod_order`: add missing dependencies (with a prompt), warn on conflicts, and stable-reorder so every mod follows its dependencies. Use the ordered vector instead of a `std::set` in `--check-mods`.

### DATA-B10 · Obsolete-OMT migration mishandles RLE runs that wrap rows and copies a stale terrain
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame.cpp:513-517`, `src/savegame.cpp:524-527`, `src/savegame.cpp:533`, `src/savegame.cpp:861-865`, `src/savegame.cpp:873`, `src/savegame.cpp:1283-1301`, `src/overmap.cpp:327-331`, `src/overmap.cpp:1382-1397`

Overmap layers are RLE-encoded row-major, and runs carry over from one row into the next. For an obsolete id, load records migration points as `(p, j)` for `p` in `[i, i+count)` with no row wrap. Points with x ≥ 180 are dropped by `ter_set`'s bounds check, so the part of the run in later rows is never migrated. Those tiles keep `tmp_otid`, which was not updated for the obsolete run, so they get the previous run's terrain. The camp migration list and `unserialize_omap` have the same bug. This is latent: every shipped `oter_id_migration` targets buildings or specials, which practically never wrap. It matters only if someone migrates an area terrain such as a biome tile.

```cpp
for( int p = i; p < i + count; p++ ) {
    oter_id_migrations.emplace( tripoint_om_omt( p, j, z - OVERMAP_DEPTH ), tmp_ter );   // savegame.cpp:515
...
layer[z].terrain[i][j] = tmp_otid;   // stale for obsolete runs
```

**Fix:** Record a flat index `j*OMAPX + i + k` and convert it back with `% OMAPX` and `/ OMAPX`, or decode the RLE into a flat array before resolving ids. Set `tmp_otid` to a placeholder such as `oter_omt_obsolete` for obsolete runs.

### DATA-B11 · Mod translations ignore data/mods, and inactive mods' .mo files override core strings
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/translation_manager_impl.cpp:21-40`, `src/translation_manager_impl.cpp:55-83`, `src/mod_manager.cpp:190-191`, `src/path_info.cpp:348-351`, `src/path_info.cpp:404-407`

`ScanTranslationDocuments` looks for `LC_MESSAGES` only under `PATH_INFO::user_moddir()`, but mods are also loaded from `data/mods`. Mods installed there never get their translations, contrary to TRANSLATING_MOD.md. Every mod under `user/mods` is scanned whether or not it is active in the current world, and its documents are searched before core's. `LookupString` returns the first match, so a disabled mod's translation of a shared msgid beats the core translation. (The related bogus bucket entry is DATA-P13.)

```cpp
if( dir_exist( PATH_INFO::user_moddir() ) ) { ... get_files_from_path( "LC_MESSAGES", PATH_INFO::user_moddir(), true ) ...   // :59-62
```

**Fix:** Also scan `PATH_INFO::moddir()`. Load mod `.mo` files only for active mods (reloading on world load), and keep core strings authoritative unless the mod owns the msgid.

### DATA-B12 · Integer reads from JSON silently wrap out-of-range values
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/flexbuffer_json-inl.h:119-125`, `src/flexbuffer_json-inl.h:149-159`, `src/flexbuffer_json-inl.h:249-252`, `src/flexbuffer_json.cpp:148-191`, `src/json.cpp:1293-1303`

All game data and saves are parsed through the flexbuffer `JsonValue`. `operator int()` is a plain `static_cast<int>( AsInt64() )`. `operator unsigned()` rejects negative values but wraps values above UINT_MAX. The `read(char/short/...)` overloads assign `get_int()` directly ("TODO: test for overflow"). A modder's `3000000000` in an int field, or `70000` in a short field, wraps without an error. The legacy `TextJsonIn::get_int` rejects the same input, so behavior differs between the two paths.

```cpp
inline JsonValue::operator int() const { if( json_.IsIntOrUint() ) { return static_cast<int>( json_.AsInt64() ); }   // :119-123
```

**Fix:** Range-check against `std::numeric_limits<T>` in `operator int`/`operator unsigned` and in the narrow `read()` overloads, and `throw_error` with the offending value, as `TextJsonIn::get_int` does.

### DATA-B13 · Migration tables are first-wins, and var_migration without "to" leaves a junk key
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:5019`, `src/savegame_json.cpp:5025`, `src/savegame_json.cpp:5064`, `src/savegame_json.cpp:5089`, `src/savegame.cpp:1778-1785`, `src/savegame.cpp:1909-1914`, `src/savegame_json.cpp:4294-4300`, `src/global_vars.h:65`, `doc/JSON/OBSOLETION_AND_MIGRATION.md:352`

`ter_furn_migration`, `trap_migration`, `field_type_migration` and `var_migration` store entries with `insert`/`emplace`, so the first definition wins and later duplicates are silently ignored. A mod therefore cannot redirect a core migration. Effect, spell, trait, proficiency and vpart migrations are also first-wins, while item and oter migrations overwrite, so behavior is inconsistent across migration types. A var_migration without `"to"` renames the variable to the literal key `"NULL_VALUE"` (documented). That key persists in saves. When several such migrations hit the same object, `values.insert` fails on the existing key, so later variables are dropped silently.

```cpp
ter_migrations.insert( std::make_pair( from_ter, ... ) );   // savegame_json.cpp:5019
```

**Fix:** Use `insert_or_assign`, or debugmsg on duplicates with a different target, so later mods win. Treat a missing `"to"` as erase (see DATA-J05), and clear the var migration map in `unload_data` (see DATA-B05).

### DATA-B14 · EXTERNAL_OPTION "stub": true in a mod silently does nothing; mod-forced world options get persisted
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/worldfactory.cpp:2082-2091`, `src/worldfactory.cpp:2093-2127`, `src/worldfactory.cpp:2097-2104`, `src/options.cpp:4135-4140`, `src/options.cpp:4367-4391`, `data/mods/hunvre/json/mod_settings/external_options.json:16-31`, `data/mods/No_Hope/game_balance.json:83-89`

`"stub"` is a core-only mechanism (OPTIONS.md) that turns an entry into a no-op, but the loader honors it from any source without warning. hunvre's `ETERNAL_SEASON` and `ETERNAL_TIME_OF_DAY` entries are stubs, so they do nothing (whether that was intended cannot be told from the code). Non-stub mod overrides of `world_default` options, such as No_Hope's and XedraWood's `EVOLUTION_INVERSE_MULTIPLIER`, write into the active world's `WORLD_OPTIONS` through `get_option`. They are persisted to `worldoptions.json` if the player later saves an option change in game. `WORLD_OPTION` has the same persistence problem. The impact is low, because the mod reapplies its value on every load, and the stale value matters only after the mod is removed.

```cpp
if( stub ) { jo.allow_omitted_members(); return; }   // worldfactory.cpp:2101-2104
```

**Fix:** Debugmsg when `"stub"` appears outside the `core` source, and fix the hunvre data. Keep mod-forced world option values in a separate overlay rebuilt on each data load, so they are never saved.

## 3. Performance

### DATA-P01 · Startup: every finalize/verify step does a full redraw and a vsync-blocked present (~190 frames)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/init.cpp:943`, `src/init.cpp:1055`, `src/init.cpp:199`, `src/loading_ui.cpp:258`, `src/ui_manager.cpp:538`, `src/sdltiles.cpp:461`, `src/sdltiles.cpp:1212`, `src/sdl_wrappers.cpp:1213`, `src/game_io.cpp:495`, `src/game_io.cpp:551`, `src/game_io.cpp:565`

`finalize_loaded_data()` runs about 107 steps and `check_consistency()` about 76. Each step calls `loading_ui::show()`, which runs a full ImGui redraw and `RenderPresent`. The hardware renderer is always created with vsync on, and nothing throttles the presents. Most steps take well under 1 ms, so each costs about one frame: roughly 183 × 16.7 ms ≈ 3 s per world load or new game at 60 Hz. The same pattern costs one frame per mod and per save-load step, and `load_deferred` pumps SDL events for every deferred object.

```cpp
for( const named_entry &e : entries ) { loading_ui::show( _( "Finalizing" ), e.first ); e.second(); }  // init.cpp:943
return CreateRenderer( ::window, renderer_name.c_str(), false, true );                              // sdltiles.cpp:461 (vsync)
```

**Fix:** In `loading_ui::show()`, always call `update_state()`, but redraw and present only if 50–100 ms have passed since the last present (or the context changed). Alternatively, disable vsync while the loading UI is shown. Pump events every N deferred objects instead of every object. Put a timer around the two loops to confirm the gain.

### DATA-P02 · JsonOut emits every string one character at a time via std::ostream::put
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/json.cpp:2253`, `src/json.cpp:2363`, `src/json.cpp:2399`, `src/json.cpp:2422`, `src/json.h:825`

All save output, including every member name, goes through `JsonOut::write(string_view)`. That function calls `stream->put()` once per character, and each call builds an `ostream::sentry`. `write_indent` pushes spaces one at a time through `ostream_iterator`, and `write(const char*)` allocates a temporary `std::string`. This runs only at save time. With libstdc++ the waste is around 100 ms per tens of MB. On MSVC it is more when the target is an `ofstream`, because of the per-put lock.

```cpp
stream->put( '"' ); for( const char &i : val ) { ... else { stream->put( ch ); } }   // json.cpp:2363-2402
void write( const char *val ) { write( std::string( val ) ); }                        // json.h:825
```

**Fix:** Scan for the next character that needs escaping (`"`, `\`, < 0x20) and emit each clean run with one `stream->write(ptr, len)`. Write indentation from a static buffer of spaces. Forward `write(const char*)` to `std::string_view`.

### DATA-P03 · string_id interning allocates a hash node (and copies the key) even when the id already exists
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/string_id.cpp:24`, `src/string_id.cpp:27`, `src/string_id.h:206`, `src/flexbuffer_json-inl.h:288`, `src/mod_tracker.h:56`, `src/savegame_json.cpp:3250`, `src/savegame_json.cpp:4431`

`universal_string_id_intern()` calls `emplace(key, next_id)` without a prior `find`. libstdc++ builds the node, including the string copy, before checking for the key, then frees it on a hit, and almost every call is a hit. Callers include every string_id read from JSON (`JsonValue::read(string_id)` passes an lvalue, so the key is copied), `mod_tracker::assign_src` for every object, and per-item and per-RLE-run ids on save load. This adds millions of malloc/free pairs on GCC/MinGW builds. MSVC STL and libc++ look the key up before allocating, so they are unaffected. The estimated gain is a few tenths of a second.

```cpp
const auto &pair = get_intern_map().emplace( std::forward<S>( s ), next_id );   // string_id.cpp:27
```

**Fix:** `auto it = map.find( s ); if( it != map.end() ) return it->second;` and emplace only on a miss. Pass `std::move( tmp )` in `JsonValue::read(string_id/int_id)`. With C++20, use heterogeneous lookup to avoid building a `std::string` from a `string_view` on hits.

### DATA-P04 · optional(), mandatory() and the readers repeat 6–9 key lookups per member and rebuild feature JsonObjects
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/generic_factory.h:665`, `src/generic_factory.h:816`, `src/generic_factory.h:888`, `src/generic_factory.h:909`, `src/generic_factory.h:928`, `src/generic_factory.h:948`, `src/generic_factory.h:978`, `src/generic_factory.h:1568`, `src/generic_factory.h:1681`, `src/generic_factory.h:1697`, `src/generic_factory.cpp:9`

For each member of an object without copy-from, `optional()` performs:
- 4 `warn_disabled_feature` lookups (has_member for "relative", "proportional", "extend" and "delete");
- the read itself;
- `has_object("proportional")` and `has_object("relative")` on a miss;
- 1–2 more lookups in `handle_extend`/`handle_delete`.

That is up to 9 binary searches, each probe doing a `strlen`'d `AsKey`. When one of the feature blocks exists, a fresh `JsonObject` is built for it on every member read. The reader path and `read_normal` (has_member + get_member) repeat the pattern. This runs for every member of about 67k objects.

```cpp
if( !jo.has_member( feature ) ) return; JsonObject feat = jo.get_object( feature ); feat.allow_omitted_members();   // generic_factory.cpp:12-17
```

**Fix:** Compute once per `JsonObject` which of the 4 feature blocks exist (a lazily filled bitmask plus cached sub-objects, or a small load context passed down), so absent features cost a single bit test. Use `get_member_opt` instead of has_member + get_member. Semantics do not change. The 10–20% gain is an estimate; profile before and after.

### DATA-P05 · zzip::add_file rebuilds the whole footer and remaps the file twice for every file added
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/zzip.cpp:441`, `src/zzip.cpp:445`, `src/zzip.cpp:457`, `src/zzip.cpp:479`, `src/zzip.cpp:687`, `src/zzip.cpp:763`, `src/zzip.cpp:789`, `src/zzip.cpp:816`, `src/mmap_file.cpp:251`, `src/mapbuffer.cpp:257`, `src/mapbuffer.cpp:338`, `src/map_memory.cpp:517`

Each `add_file` does the following:
- copies the footer;
- grows the file (unmap, ftruncate, remap);
- rebuilds the entire footer (a string copy, a set insert and lookups for every existing entry);
- shrinks the file back to fit (a second unmap, ftruncate and remap).

Adding K files to an N-entry zzip therefore costs O(K·N) footer work plus 2K remaps. `mapbuffer::save` iterates every loaded submap, not just dirty ones, and calls `zzip::load` and `add_file` once per non-uniform quad. Map memory adds one file per region. `WORLD_COMPRESSION2` defaults to on, so this runs on every save of a new world.

```cpp
for( JsonMember entry : original_footer.get_object( kEntriesKey ) ) { std::string old_filename = entry.name(); ...   // zzip.cpp:789
```

**Fix:** Add a batch `add_files(vector<pair<path, string_view>>)` that grows the file once, writes all frames and rebuilds the footer once, and use it from mapbuffer (per segment) and the map-memory loop. Keep geometric slack capacity instead of shrinking after every write; shrink only in `compact_to` or on close.

### DATA-P06 · item::deserialize deep-copies every nested item once per nesting level
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:3564`, `src/savegame_json.cpp:3568`, `src/item_contents.cpp:725`, `src/item_contents.cpp:779`, `src/item_pocket.cpp:1549`, `src/item_pocket.cpp:2272`

Contents are deserialized into a temporary `item_contents`, and `combine()` then re-inserts each top-level item through `insert_item(const item&)`, which copies the full subtree. An item at depth d is therefore copied d times. With `ignore_contents=true`, `can_contain` still calls `weight()` and `volume()`, which walk the subtree again at each level. This affects the player, NPCs, vehicle cargo and every submap's items. Typical nesting is 2–4 levels.

```cpp
item_contents read_contents; data.read( "contents", read_contents ); ...
contents.combine( read_contents, false, true, false, true );          // savegame_json.cpp:3563-3568
```

**Fix:** Add `combine(item_contents &&)` and `item_pocket::insert_item(item &&)` overloads that move or splice `std::list` nodes out of the temporary. On the restore path, skip or hoist the capacity check.

### DATA-P07 · item::deserialize builds a 7-node std::set for every item loaded
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:3480`, `src/savegame_json.cpp:3488`, `src/savegame_json.cpp:3597`

`migrated_edevice_itypes` is a non-static local `std::set`, so every deserialized item (nested, submap and vehicle-part items included) allocates and frees 7 nodes. The neighboring `removed_item_vars` erase loop is nearly free, because the set is static and the map is usually empty.

```cpp
std::set<itype_id> migrated_edevice_itypes = { itype_camera, itype_camera_pro, ... };   // savegame_json.cpp:3597
```

**Fix:** Make it `static const`, or compare against the 7 ids directly. Optionally guard the erase loop with `if( !item_vars.empty() )`.

### DATA-P08 · translation::translated() caches one plural form keyed by the exact count
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/translation.cpp:296`, `src/translation.cpp:306`, `src/translation.cpp:313`, `src/itype.cpp:165`, `src/item.cpp:5015`, `src/inventory_ui.cpp:803`, `src/translation_manager_impl.cpp:178`

For plural strings the cache key includes `num`. Lists that show the same itype at different counts (for example via `display_name(count)` at `inventory_ui.cpp:803`) miss on every call. Each miss does a lookup and allocates through `make_value`, even in English with no `.mo` loaded. Most UI callers pass 1, so the impact is small.

```cpp
|| ( raw_pl && cached_num != num ) || !cached_translation ) { ... cached_num = num;   // translation.cpp:305-316
```

**Fix:** Key the cache on the plural-form index rather than `num`, or at least keep separate singular and plural slots. When no translation document is loaded, return `raw`/`*raw_pl` directly.

### DATA-P09 · vehicle_part and monster serializers always write members that hold default values
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame_json.cpp:2560`, `src/savegame_json.cpp:3735`, `src/savegame_json.cpp:3745`, `src/savegame_json.cpp:3781`, `src/savegame_json.cpp:4232`

`vehicle_part::serialize` writes about 18 members unconditionally (`open`, `blood`, `flags`, `items: []`, `tools: []`, `locked`, `hidden`, ...), and `monster::store` plus `Creature::store` write about 60, including for horde `monster_data`. The matching reads already tolerate missing members. This makes save files larger and submap and overmap I/O slower.

```cpp
json.member( "open", open ); json.member( "blood", blood ); json.member( "enabled", enabled ); ...   // savegame_json.cpp:3745-3782
```

**Fix:** Use the existing `JsonOut::member(name, value, default)` and skip empty containers. Audit `monster::load`/`Creature::load` before doing the same for monsters.

### DATA-P10 · overmap::unserialize does per-tile coordinate round-trips and id lookups for passability
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/savegame.cpp:513`, `src/savegame.cpp:518`, `src/savegame.cpp:524`, `src/savegame.cpp:531`, `src/overmap.cpp:851`

For each of the 680k tiles, the loop calls `set_passable( project_combine( loc, ... ), tmp_otid->get_type_id()->default_map_data )`. That projects the coordinate to absolute and back, compares it with `loc` (always true), and resolves two factories, even though the result changes only once per RLE run. The saving is a few to tens of ms per overmap load. Line 531 also uses `z - OVERMAP_HEIGHT` where line 526 uses `z - OVERMAP_DEPTH`. Both constants are 10 today, so this is harmless but fragile.

```cpp
set_passable( project_combine( loc, tripoint_om_omt( i, j, z - OVERMAP_HEIGHT ) ), tmp_otid->get_type_id()->default_map_data );   // :531
```

**Fix:** Resolve the oter id and placeholder pointer once per run, and assign `layer[z].map_cache` directly for each tile in it. Use `OVERMAP_DEPTH` consistently.

### DATA-P11 · Each JSON data file gets 3–4 separate existence/mtime checks during startup
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/json_loader.cpp:136`, `src/filesystem.cpp:122`, `src/flexbuffer_cache.cpp:331`, `src/flexbuffer_cache.cpp:363`, `src/flexbuffer_cache.cpp:476`, `src/init.cpp:545`

For each file that was just listed, `from_path_at_offset` calls `file_exist()` (`exists()` plus `is_directory()`). On a cache hit, the mtime is then read in `load_flexbuffer_if_not_stale` and again in `parse_and_cache`. With about 6.6k files that is roughly 20k extra metadata calls, tens of ms on Linux.

```cpp
std::filesystem::file_time_type mtime = get_file_mtime_millis( lexically_normal_json_source_path, ec );   // flexbuffer_cache.cpp:476
```

**Fix:** Return the mtime from `load_flexbuffer_if_not_stale` and reuse it. Drop the `file_exist` pre-check and treat an open or mmap failure as "missing".

### DATA-P12 · generic_factory::load copies each loaded object into the list; ter_t, furn_t and itype are not movable
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/generic_factory.h:283`, `src/generic_factory.h:366`, `src/generic_factory.h:379`, `src/generic_factory.h:392`, `src/generic_factory.h:403`, `src/generic_factory.h:409`, `src/mapdata.h:526`, `src/itype.h:1782`

`load()` builds `T def` and passes it to `insert(const T&)`, which copies it with `push_back` or copy-assignment, even though `def` is dead afterwards. Abstract objects are copied into `abstracts` the same way. `map_data_common_t` and `itype` declare virtual destructors, which suppress implicit moves, so vector growth also copies every `ter_t`, `furn_t` and `itype`. This is a startup-only cost, likely 100 ms or less.

```cpp
def.load( jo, src ); insert( def );   // generic_factory.h:365-366
virtual ~map_data_common_t() = default;   // mapdata.h:526
```

**Fix:** Add `insert(T&&)` and call `insert( std::move( def ) )`, and move into `abstracts` too. Declare defaulted `noexcept` move operations in `map_data_common_t` (and its bash/deconstruct info members) and in `itype`. Without them, `insert(T&&)` has no effect for these types.

### DATA-P13 · TranslationManager seeds every hash bucket with a bogus (0,0) entry
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/translation_manager_impl.cpp:32`, `src/translation_manager_impl.cpp:153`, `src/translation_manager_impl.cpp:154`, `src/translation_manager_impl.cpp:156`

`std::vector<...>( 1 )` creates a vector that already holds a value-initialized `(0,0)` entry, and the real entry is appended after it. Every lookup checks the bogus entry first. That entry is the MO header with an empty msgid, so the extra `strcmp` ends at the first byte. Each string also costs an extra allocation and 2 extra hash lookups at language load, plus about 16 bytes. Lookup results never change.

```cpp
if( strings.count( hash ) == 0 ) { strings[hash] = std::vector<std::pair<std::size_t, std::size_t>>( 1 ); }
strings[hash].emplace_back( document, i );    // translation_manager_impl.cpp:153-156
```

**Fix:** Replace both statements with `strings[hash].emplace_back( document, i );`.

### DATA-P14 · get_option<T>("NAME") builds a std::string and does 2 hash lookups plus string copies per call
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/options.h:511`, `src/options.cpp:713`, `src/options.cpp:776`, `src/options.cpp:794`, `src/options.cpp:4367`, `src/options.cpp:4381`, `src/worldfactory.cpp:2107`, `src/cata_tiles.cpp:2829`, `src/character_health.cpp:1299`, `src/character_health.cpp:1986`

Each call does the following:
- builds a `std::string` from a literal (which heap-allocates for names over 15 characters);
- hashes into `options`, then into `world_options`;
- on the world-options miss, which is the usual case for external balance options, copies `sPage` by value. `"external_options"` is 16 characters, so the copy also allocates.

For `std::string`, `value_as` calls `getValue(true)` before it checks `convert`. Each call costs about 100 ns. Most of the 626 call sites are not hot. The hottest real case is `cata_tiles.cpp:2829`, which runs per drawn tile while NV goggles are active.

```cpp
inline T get_option( const std::string &name, bool convert = false ) { return get_options().get_option( name ).value_as<T>( convert ); }   // options.h:511
```

**Fix:** Add a cached handle, for example `static const option_ref<float> x{ "NAME" };`, that resolves to a `cOpt*` once and is invalidated by a version counter bumped on options load and world switch. Take `std::string_view` with a transparent hash. Check `convert` before calling `_convert` in `value_as`. Return `const std::string&` from `getPage()`.

### DATA-P15 · lcmatch re-converts the query for every candidate; match_include_exclude re-splits the filter on every call
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/cata_utility.cpp:89`, `src/cata_utility.cpp:123`, `src/cata_utility.cpp:150`, `src/overmap_ui.cpp:1520`, `src/overmap_ui.cpp:1537`, `src/item_search.cpp:211`, `src/crafting_gui_helpers.cpp:722`

Every `lcmatch()` call converts both strings to `std::u32string`, lowercases both, and then runs `remove_accent` and a second search. `match_include_exclude` takes the filter by value and re-splits it with `substr` on every call. Callers are user-initiated searches: item filters, the crafting GUI, and overmap search over about 3.2M points. In those searches the surrounding per-candidate work (for example `remove_color_tags( tname() )`) usually costs more than the match, so the gain is minor.

```cpp
std::u32string u32_str = utf8_to_utf32( str ); std::u32string u32_qry = utf8_to_utf32( qry );   // cata_utility.cpp:99
bool match_include_exclude( std::string_view text, std::string filter )                       // :123
```

**Fix:** Add a precompiled matcher (`struct lc_filter { std::vector<std::pair<bool, std::u32string>> terms; bool match( std::string_view ) const; }`) built once per filter string, with an ASCII fast path. In overmap search, memoize the result per (oter type, vision).

## Appendix: considered and rejected

- NPC `companion_mission_travel_time` is saved and loaded under different keys – The key mismatch exists but has no observable effect. The field is never assigned except to its 0 default, because `basecamp::start_mission` never stores `travel_time` on the NPC. That missing store is the real latent issue, and the key mismatch should be fixed with it.
- `submap::store` and `mm_submap::deserialize` churn std::strings per tile or per run – The code is as described, but neither path is hot: `submap::store` runs only on save, and the deserialize runs once per region. The saving is microseconds per submap, far below the surrounding JSON and I/O cost.

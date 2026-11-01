# 13 · Item use actions (iuse & iuse_actor)

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/iuse.cpp`, `src/iuse_actor.cpp`, `src/item_action.cpp`, `src/iuse_software.cpp`

## Summary

The item-use layer is a mix of 41 data-driven actors and about 161 hardcoded C++ iuse functions. Many of the hardcoded functions bake in vanilla item ids, trait ids, construction ids and tuning numbers, and none of them take JSON parameters. The data-driven escape hatch, the `effect_on_conditions` actor, has no condition, charge or cancel control, and an item can carry only one use action of each actor type. Bugs cluster in a few areas: index and lifetime mistakes (a holster crash, a null carrier in tick functions), wrong signs or formulas (free stamina from breaking sticks, zero stop-bleed at First Aid 0), and items or charges lost on failure or cancel paths. Performance issues are mostly cold UI paths. The exception is the weather radio, which rebuilds a full forecast every turn.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 9 | 6 | 17 |
| Bugs & quirks | 1 | 3 | 7 | 8 | 19 |
| Performance | 0 | 0 | 3 | 5 | 8 |
| **Total** | **1** | **5** | **19** | **19** | **44** |

**Fix first:**
1. **USE-B01** · holster_actor::use draws the wrong item or reads `end()`: a deterministic crash in a common setup (western holster with a revolver and one ammo stack), and the fix is small.
2. **USE-B02** · PAPR_BLOWER tick dereferences a null Character: a segfault when an active PAPR belt sits on the ground. The fix is a one-line guard.
3. **USE-B03** · BREAK_STICK gives infinite stamina: a sign error plus a move refund on failure, exploitable in combat. The fix is trivial.
4. **USE-B04** · Stop-bleed power is 0 at First Aid 0: new characters cannot stop bleeding with any item. It is a one-formula change.
5. **USE-J03** · sew_advanced loads `clothing_mods` into `materials`: a one-word typo that breaks Innawood's shipped modify-clothing action.

## 1. Missing JSON bindings & EOC features

### USE-J01 · effect_on_conditions use_action is too limited to replace hardcoded iuse functions
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:5974`, `src/iuse_actor.cpp:5981`, `src/iuse_actor.cpp:6005`, `src/iuse_actor.cpp:6009`, `src/iuse_actor.cpp:6031`, `src/iuse_actor.cpp:6040`, `src/iuse_actor.h:1263`, `src/iuse_actor.cpp:437`, `src/item_action.cpp:161`, `src/item.cpp:4814`, `src/npctalk.cpp:6331`, `src/character.cpp:4576`

The EOC actor is how modders are supposed to move away from hardcoded iuse functions, but it is missing basic controls:
- **No `can_use()` override and no `condition` field.** `need_worn` and `need_wielding` are checked only inside `use()`, not in `can_use()` as transform does (`iuse_actor.cpp:437`). The item-action menu therefore lists the action as available. Because the checks run before the `if( p )` guard, a tick or countdown action with no carrier (`item.cpp:4814`) would dereference null. No shipped content combines these, so this is a latent trap rather than a live crash (see also USE-B08).
- **Fixed return value.** The actor returns 1 with `consume` and 0 otherwise. An EOC cannot say how many charges to use, and it cannot cancel. Moves are refunded only on `std::nullopt` (`character.cpp:4578`).
- **`id` is the only context variable.** The target tile that `u_activate` passes via `target_var` reaches `use()` as `pos`, but the actor uses it only to build a map_cursor when `p` is null.
- **Comestible check only at use time.** Using the actor on a comestible raises a debugmsg when used, not at load or finalize.
- **Not loaded through generic_factory.** A FIXME at 5981 notes that the EOCs bypass it.

```cpp
if( need_worn && !p->is_worn( it ) )                                // 6009, inside use(), no null check
write_var_value( var_type::context, "id", &d, it.typeId().str() );  // 6031, only context var
if( consume ) { ... return 1; } return 0;                           // 6040-6045
// npctalk.cpp:6331: guy->invoke_item( it->get_item(), method_str, here.get_bub( target_pos ) );  -> pos ignored
```

**Fix:** Add a `can_use()` that checks a `"condition"` (dialogue condition, u = user, npc = item) plus `need_worn`/`need_wielding`, with an optional `"failure_message"`. Before running, write context vars `_target` (location from `pos`), `_method` and `_item_pos`. After running, read `_charges` (int, multiplies ammo_required) and `_cancel` (bool; return `std::nullopt` so moves are refunded). Reject comestible + effect_on_conditions in `finalize()`.
```json
{ "type": "effect_on_conditions",
  "condition": { "math": [ "is_night()" ] },
  "failure_message": "The rune is dormant by day.",
  "effect_on_conditions": [ "EOC_RUNE" ] }
```
Inside `EOC_RUNE`: `{ "math": [ "_charges = 3" ] }`. Use cases: a remote detonator aimed through `u_activate` `target_var`; a wand that spends 1-3 charges depending on the spell level picked.

### USE-J02 · An item can have only one use_action of each actor type
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/itype.h:1512`, `src/itype.h:1404`, `src/itype.h:1518`, `src/item_factory.cpp:1986`, `src/item_factory.cpp:4537`, `src/generic_factory.h:1340`, `src/item_action.cpp:159`, `doc/JSON/ITEM.md:1572`

`use_methods`, `tick_action` and `ammo_scale` are maps keyed by the actor type string. Menu entries, keybindings, `u_activate` and `get_use` all look actions up by that key. A second `cast_spell`, `transform` or `effect_on_conditions` entry on the same item hits the "key already exists" debugmsg and is dropped. Modders cannot make a staff that casts two spells, an amulet with separate Pray and Curse actions, or a tool with low and high transform modes. The only workaround is one EOC that opens its own dialogue menu, which loses keybindings and per-action charge costs. `repair_item` already re-keys itself by `item_action_type` (`item_factory.cpp:1957`), so id-keyed actions exist in one place already.

```cpp
std::map<std::string, use_function> use_methods;   // itype.h:1512
if( !container.emplace( data ).second ) { debugmsg( "Insert of <%s, %s> failed, key already exists" ...  // generic_factory.h:1340
// ITEM.md:1572: "Multiple use actions of same type cannot be used at once."
```

**Fix:** Accept an optional `"id"` on object-form use actions and key the maps by it (defaulting to the type). Take the menu name from `menu_text`/`name`, or from item_actions.json via the type. Key `ammo_scale` by id and let `u_activate` accept the id.
```json
"use_action": [
  { "type": "cast_spell", "id": "staff_fire", "spell_id": "fireball", "level": 3, "no_fail": true },
  { "type": "cast_spell", "id": "staff_heal", "spell_id": "light_healing", "level": 2, "no_fail": true }
]
```

### USE-J03 · sew_advanced loads `clothing_mods` into `materials`, so the action is always empty (breaks Innawood)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:5697`, `src/iuse_actor.cpp:5726`, `src/iuse_actor.cpp:5786`, `src/iuse_actor.h:1248`, `data/mods/innawood/items/tool_tailoring.json:108`

The loader reads the documented `clothing_mods` array into the `materials` set. Reading a set clears it first, so the real material list is replaced by clothing-mod ids and the `clothing_mods` vector stays empty. The item filter `made_of_any( materials )` then matches no garment, and the menu loop over `clothing_mods` has nothing to add. The actor has no finalize or check, so nothing is reported at load. Innawood's tailoring kit is the only user, and its modify-clothing action never works.

```cpp
mandatory( obj, false, "materials", materials );
mandatory( obj, false, "clothing_mods", materials );   // should be clothing_mods
```

**Fix:** `mandatory( obj, false, "clothing_mods", clothing_mods );`, plus a `finalize()` that validates material and clothing_mod ids. Alternatively, delete the actor (an in-code FIXME says it has been unused since 0.G) and move Innawood to the vanilla tailoring flow.

### USE-J04 · Docs drift: ITEM.md use_action examples fail to load or are obsolete; JSON_FLAGS.md use_action list badly out of date
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `doc/JSON/ITEM.md:1221`, `doc/JSON/ITEM.md:1303`, `doc/JSON/ITEM.md:1365`, `doc/JSON/ITEM.md:1371`, `doc/JSON/ITEM.md:1442`, `doc/JSON/ITEM.md:1278`, `doc/JSON/ITEM.md:1159`, `doc/JSON/ITEM.md:1320`, `doc/JSON/JSON_FLAGS.md:591`, `doc/JSON/JSON_FLAGS.md:1583`, `src/item_transformation.cpp:36`, `src/iuse_actor.cpp:1130`, `src/iuse_actor.cpp:1798`, `src/iuse_actor.cpp:4845`

Unvisited JSON members are reported as errors, so copying the ITEM.md examples breaks loading:
- **transform:** the docs list `ammo_qty`, `random_ammo_qty` and `ammo_type`, but the code reads `target_charges`, `rand_target_charges` and `target_ammo`.
- **place_npc:** `radius` is documented but never read (see USE-B17).
- **salvage:** `material_whitelist` is documented, but `load()` reads only `cost` and `moves_per_part`.
- **Nonexistent actors:** `unpack` and `bandolier` do not exist; the real type is `ammobelt`.
- **consume_drug:** the example uses the invalid field id `cracksmoke`.
- **Artifacts:** the section still describes `"use_action": [ "ARTIFACT" ]` and AEA_* effects.
- **link_up:** the default for `targets` is described wrongly.
- **Undocumented actors:** eight types are missing (ammobelt, deploy_tent, saw_stock, install_bionic, detach_gunmods, modify_gunmods, weigh_self, play_instrument).

In JSON_FLAGS.md, 43 of the 107 listed use_action names do not exist (BANDAGE, CIG, FIRSTAID, GRENADE, MOLOTOV, TORCH, SEW, ...) and 97 of the 161 registered iuse names are missing. Several descriptions are wrong: DIG says "Clear rubble", PICKAXE says "Does nothing", NOISE_EMITTER_ON and RADIO_ON say "turn ... off", and ANTIBIOTIC says "removes infected". `transform_age` is an int of turns (`iuse_actor.h:296`); the doc is correct, but the field type is a quirk.

```cpp
"ammo_qty": 3,                                                   // ITEM.md:1221
optional( jo, false, "target_charges", ammo_qty, -1 );           // item_transformation.cpp:39
ammobelt_actor() : iuse_actor( "ammobelt" )                      // iuse_actor.h:821 vs ITEM.md:1442 "bandolier"
```

**Fix:** Regenerate the use_action reference from `Item_factory::init()` and give each actor a documented schema. Add a test that loads every ITEM.md jsonc example. Optionally convert `transform_age` to a `time_duration` that still accepts an int.

### USE-J05 · consume_drug: hardcoded TOLERANCE/LIGHTWEIGHT scaling, unvalidated stringly fields, `moves` not used for consume time
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:936`, `src/iuse_actor.cpp:948`, `src/iuse_actor.cpp:951`, `src/iuse_actor.cpp:864`, `src/iuse_actor.h:251`, `src/consumption.cpp:1952`, `src/iuse.cpp:780`, `src/character.cpp:751`, `src/creature.cpp:3285`, `doc/JSON/ITEM.md:1278`

- **Hardcoded drug sensitivity.** Duration scales only for two vanilla traits (x0.8 / x1.2). ANTICONVULSANT, WEED_CAKE, COKE and METH repeat the same checks (`iuse.cpp:780-847`), so mod mutations and enchantments cannot change drug sensitivity.
- **Unvalidated string maps.** `stat_adjustments` and `fields_produced` are `std::map<std::string,int>` and are never checked. A typo shows up only as a "nonexistent stat" debugmsg when the drug is consumed.
- **Stat adjustments do nothing.** str/dex/per/int adjustments go to per-turn bonuses that are reset.
- **Fixed field spawning.** Fields always spawn as 3 rolls within radius 2.
- **`moves` is not the consume time.** The consume duration comes from hardcoded tool ids in consumption.cpp (syringe 5 min, apparatus/dab_pen_on 30 s, otherwise 5 s). `moves` is only deducted afterwards.
- **No `remove_effects`.** This is partly why ANTIPARASITIC, INHALER and EYEDROPS remain in C++. `consumption_effect_on_conditions` with `u_lose_effect` already partly covers this.

```cpp
if( p->has_trait( trait_TOLERANCE ) ) { dur *= .8; } else if( p->has_trait( trait_LIGHTWEIGHT ) ) { dur *= 1.2; }
const field_type_id fid = field_type_id( field.first ); for( int i = 0; i < 3; i++ )
time = time_duration::from_minutes( 5 );//sterile injections take 5 minutes   // consumption.cpp:1960
```

**Fix:** Add an enchantment value or mutation field `DRUG_DURATION_MULT` and drop the trait ids. Type `stat_adjustments` as an enum, and validate stat ids and field types in `finalize()`/`check()`. New fields:
```json
"consume_time": "5 m",
"remove_effects": [ { "id": "asthma", "chance": 1.0 } ],
"fields_produced": [ { "field": "fd_weedsmoke", "intensity": 2, "count": 3, "radius": 2 } ]
```

### USE-J06 · Clothes washing hardcodes water and cleanser item ids and ratios
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:8322`, `src/iuse.cpp:8399`, `src/iuse.cpp:8403`, `src/iuse.cpp:8471`, `src/activity_actor.cpp:13404`, `src/activity_actor.cpp:13445`

The WASH_* iuse functions and `wash_activity_actor` accept only `water`/`water_clean` as water and `soap`/`detergent`/`liquid_soap` as cleanser. The ratios are fixed: 1 water per 125 ml, 1 cleanser per litre, and 10 s per 250 ml. A modded cleanser (lye soap, Innawood ash, a Magiclysm reagent) or water source cannot use the washing UI. The only workaround is a hand-built EOC that unsets FILTHY.

```cpp
int available_cleanser = std::max( { crafting_inv.charges_of( itype_soap ),
    crafting_inv.amount_of( itype_detergent ), crafting_inv.charges_of( itype_liquid_soap, INT_MAX, is_liquid ) } );
int water = divide_round_up( vol, 125_ml ); int cleanser = divide_round_up( vol, 1_liter );
```

**Fix:** Add a `wash` actor that references requirement ids, and pass the resolved requirement into `wash_activity_actor`:
```json
{ "type": "wash", "soft": true, "hard": true,
  "water_requirement": "wash_water", "cleanser_requirement": "wash_cleanser",
  "water_per": "125 ml", "cleanser_per": "1 L", "time_per": [ "10 s", "250 ml" ] }
```

### USE-J07 · No event_bus event when an item is activated; activates_artifact is never sent
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character.cpp:4525`, `src/character.cpp:4589`, `src/event.h:25`, `src/event.h:207`, `src/memorial_logger.cpp:441`, `src/iuse.cpp:490`, `src/item_factory.cpp:2169`

`Character::invoke_item` is the single path for activating items, but it sends no event. EVENT-type EOCs and achievements therefore cannot react to item use in general, for example "gain corruption each time you use a cursed relic" or "used 100 lockpicks". The `activates_artifact` event is declared and handled by the memorial logger, but nothing in `src` sends it. `eats_sewage` is sent only by `iuse::sewage`, and no item uses SEWAGE any more because `sewage` migrates to `water_sewage`, so it is never sent either. Per-item EOC actors cover many specific cases, so this is a convenience hook rather than a blocker.

```cpp
// grep event_type::activates_artifact -> only event.h, event.cpp, memorial_logger.cpp
// character.cpp:4576-4596 invoke() -> activation_consume(), no get_event_bus().send
```

**Fix:** Add `character_activates_item` {character, itype, method, charges} and send it from `invoke_item` after a non-nullopt result. Optionally add `character_activates_item_failed`. Remove `activates_artifact` or reuse it for relics.
```json
{ "type": "effect_on_condition", "eoc_type": "EVENT", "required_event": "character_activates_item",
  "condition": { "compare_string": [ "cursed_idol", { "context_val": "itype" } ] },
  "effect": [ { "math": [ "u_corruption++" ] } ] }
```

### USE-J08 · heal_actor hardcodes skill, proficiencies, treatable effects and vanilla body parts
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:3715`, `src/iuse_actor.cpp:3739`, `src/iuse_actor.cpp:3752`, `src/iuse_actor.cpp:3846`, `src/iuse_actor.cpp:3871`, `src/iuse_actor.cpp:3881`, `src/iuse_actor.cpp:3944`, `src/iuse_actor.cpp:3994`, `src/iuse_actor.cpp:3708`, `src/character_body.cpp:1155`

Healing items always scale with `firstaid` and with `prof_wound_care`/`prof_wound_care_expert`. They can cure only the bleed, bite and infected effects, and can only apply bandaged and disinfected. Healing power is selected with `bodypart_id("head")` and `bodypart_id("torso")`. The broken-limb branch knows only the four vanilla limbs and otherwise fires a debugmsg. That branch is dead code in practice, because `body_window` filters out 0-HP parts without curable effects. Modders cannot make an antivenom bandage that removes `venom`, a nano-paste that scales with a different skill, or a heal that runs an EOC when it finishes. The `effects` field only adds effects. The BANDAGE_BONUS, DISINFECTANT_BONUS and BLEED_STOP_BONUS enchantments are only partial hooks. Related bugs: USE-B04 and USE-B16.

```cpp
cost = p->has_proficiency( proficiency_prof_wound_care_expert ) ? cost / 2 : cost;
return round( heal_base + bonus_mult * healer.get_skill_level( skill_firstaid ) );
if( patient.has_effect( effect_bite, healed ) ) { if( x_in_y( bite, 1.0f ) )
```

**Fix:** New fields; replace the debugmsg with a generic "needs a splint" message that names the body part.
```json
"skill": "firstaid",
"proficiencies": [ { "id": "prof_wound_care", "time_mult": 0.5, "bonus": 1 } ],
"cures": [ { "effect": "bite", "chance": 0.95 }, { "effect": "venom", "chance": 1.0, "replace_with": "recover" } ],
"power_by_part": { "head": 7, "torso": 15 },
"finish_eocs": [ "EOC_AFTER_HEAL" ]
```
In `finish_eocs`, u = healer, npc = patient, and context var `_bodypart` holds the treated part.

### USE-J09 · Smoking is hardcoded to cig/cigar/joint item ids; modded smokables are rejected
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/iuse.cpp:517`, `src/iuse.cpp:526`, `src/iuse.cpp:546`, `src/item.cpp:4572`, `src/item.cpp:4560`, `src/item.cpp:453`, `src/consumption.cpp:952`, `src/consumption.cpp:1953`

`iuse::smoking` maps exactly four item ids to their lit versions and hardcodes burn time, hunger, thirst and painkiller changes. Any other item gets "Please let the devs know you should be able to smoke a %s". `process_litcig` hardcodes joint_lit's last puff, the weedsmoke field, the TOBACCO flag check and trait-based puff durations. Consumption code also special-cases the "SMOKING" use name. A modder cannot rebuild a burning item with `transform`, because `item::convert` resets `item_counter` to 0 (`item.cpp:453`) and the lit item burns out on its first tick. Instant smoking (pipes via `consume_drug` with `tools_needed`) already works in JSON; only the burn-over-time mechanic is hardcoded.

```cpp
if( it->typeId() == itype_cig || it->typeId() == itype_handrolled_cig ) { cig = item( itype_cig_lit, calendar::turn );
    cig.item_counter = to_turns<int>( 4_minutes );
if( typeId() == itype_joint_lit && carrier != nullptr ) {   // item.cpp:4572
```

**Fix:** Add a `smoke` actor, a matching block on the lit item, and have consumption.cpp check for the actor type instead of the string "SMOKING".
```json
{ "type": "smoke", "lit_item": "cig_lit", "burn_time": "4 m",
  "stat_adjustments": { "hunger": -3, "thirst": 2 }, "effect_on_conditions": [ ] }
"smoke": { "puff_effect": "cig", "puff_duration": "15 s", "field": "fd_cigsmoke", "last_puff_eocs": [ ] }
```

### USE-J10 · EOC item talker cannot read or write active state, countdown timer, or non-battery ammo
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/talker_item.cpp:100`, `src/talker_item.cpp:105`, `src/talker_item.cpp:161`, `src/talker_item.h:53`, `src/talker.h:383`, `src/math_parser_diag.cpp:223`, `src/npctalk.cpp:6357`, `src/condition.cpp:2436`, `src/condition.cpp:2523`, `src/item_gun_tool_ammo.cpp:287`

EOC use and tick actions run with the item as the beta talker, but EOCs can do little with the item's state:
- **Inconsistent power accessors.** `power_cur()` returns `ammo_remaining()` for any ammo type, labelled as mJ. `power_max()` returns only battery capacity, so `power_percentage` is 0 for fuel or arrow tools.
- **Destructive setter.** `set_power_cur()` always writes battery ammo. On a lamp-oil tool it clamps to a battery capacity of 0 and calls `ammo_unset()`, which deletes the lamp oil.
- **`charge_count()` is always 0.** Item talkers do not override `charges_of`.
- **No state accessors.** There is nothing for `active`, `countdown_point`, `item_counter` or a count setter. `transform_item` is the only way to change `active`.

An EOC oil torch, an EOC timed bomb, or a device that switches itself off cannot be written without C++.

```cpp
return 1_mJ * me_it_const->get_item()->ammo_remaining( );            // talker_item.cpp:102
return 1_mJ * me_it_const->get_item()->ammo_capacity( ammo_battery ); // :107
me_it->get_item()->ammo_set( itype_battery, ...                       // :163
```

**Fix:** Add assignable item math functions: `n_item_active()`, `n_item_countdown()` (seconds; starts countdown_action), `n_item_ammo('lamp_oil')` (via ammo_set/ammo_consume) and `n_item_charges()` for count_by_charges items. Make `power_max`/`power_cur` consistently battery-only, or deprecate them for items.

### USE-J11 · 161 hardcoded iuse functions accept no JSON parameters; many are trivially data-expressible or dead
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/item_factory.cpp:1885`, `src/item_factory.cpp:2040`, `src/iuse.cpp:501`, `src/iuse.cpp:994`, `src/iuse.cpp:8522`, `src/iuse.cpp:1141`, `src/iuse.cpp:8720`, `src/iuse.cpp:1524`, `src/iuse.cpp:1534`, `src/iuse.cpp:1589`, `src/iuse.cpp:1459`, `src/iuse.cpp:2575`, `src/iuse.cpp:2153`

`iuse_function_wrapper::load()` is empty, so none of the roughly 161 C++ iuse functions can take parameters. Any field in an object-form use_action is ignored or rejected as unvisited.
- **Effect and message only.** XANAX, FLUMED, FLUSLEEP, WEAK/STRONG_ANTIBIOTIC, PROZAC, CHEW, COIN_FLIP and MAGIC_8_BALL just add or remove effects and print a message.
- **Hardcoded tuning.** FISH_TRAP uses 3 h, GROUP_FISH, fixed thresholds and the avatar's Survival. FISH_ROD uses a 5 h activity. MAKEMOUND uses 18000 moves.
- **No JSON users.** SEWAGE, PACK_ITEM, ANTIFUNGAL, HOTPLATE_ATOMIC, WASH_SOFT_ITEMS and ADRENALINE_INJECTOR are registered but unused.
- **Dead code.** `iuse::purify_water` has no callers and converts to `water_purifying_active`, which is now a migration id.

Modders can already build equivalents with `consume_drug`, `consumption_effect_on_conditions` or EOC actors, so this is cleanup and tunability rather than a blocker.

```cpp
void load( const JsonObject &, const std::string & ) override {}   // item_factory.cpp:1885
if( it->age() > 3_hours )                                           // iuse.cpp:1524
MonsterGroupManager::GetMonstersFromGroup( GROUP_FISH, true )       // iuse.cpp:1589
```

**Fix:** Migrate the effect-only drugs and toys to `consume_drug` plus `consumption_effect_on_conditions`, or to the `message` actor with a new `"snippet_category"` field. Delete the dead entries and `iuse::purify_water`. For functions that must stay in C++, let the wrapper store the JsonObject members as a diag_value map the function can query:
```json
{ "type": "FISH_TRAP", "catch_group": "GROUP_FISH", "duration": "3 h", "skill": "survival" }
```

### USE-J12 · tick_action/countdown_action actors are never finalize()d; emit_actor id validation is commented out
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_factory.cpp:830`, `src/item_factory.cpp:4538`, `src/item_factory.cpp:2993`, `src/iuse_actor.cpp:514`, `src/iuse_actor.cpp:4363`, `src/map_field.cpp:2169`

`finalize()` runs only for `use_methods` and `drop_action`, and `check_definitions` validates only `use_methods`. Transform actors used as tick or countdown actions (three in data) therefore skip the "Invalid transform target" and link_up checks, as does any other finalize-time validation. `emit_actor::finalize` has its emit-id check commented out. `map::emit_field` returns silently on an invalid emit id, so a misspelled emit in a drop_action is never reported.

```cpp
for( auto &e : obj.use_methods ) { e.second.get_actor_ptr()->finalize( obj.id ); }
if( obj.drop_action.get_actor_ptr() != nullptr ) { ... }   // no tick_action / countdown_action
/* // TODO: This must be called after all finalization  for( const auto& e : emits ) ...  */
```

**Fix:** Loop over `tick_action` and `countdown_action` in the same finalize block. Move the emit validity check into `Item_factory::check_definitions`, which runs after all finalization.

### USE-J13 · SCBA, solar pack and pack_item build convert targets by id-string suffix; contracts never validated
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:2102`, `src/iuse.cpp:3496`, `src/iuse.cpp:3515`, `src/iuse.cpp:3559`, `src/iuse.cpp:3588-3590`, `src/iuse.cpp:3766`, `src/item_factory.cpp:3012`

*(Merged: a JSON-lens and a bug-lens finding described the same issue.)* Several iuse functions build their target itype by editing the id string:
- SCBA_MASK_ACTIVATE appends `_scba_on`.
- SCBA_TANK_ACTIVATE and SOLARPACK append `_on`.
- SOLARPACK_OFF cuts the last 3 characters without checking that the id ends in `_on`.
- PACK_ITEM strips `_on`, but it already guards with `string_ends_with` and no data uses it.

Nothing at load time checks that the derived id exists. A missing id is not a crash: `find_template` gives a debugmsg and an "undefined-" itype. SCBA_MASK calls `transform_into.value()` unchecked (see USE-B19). Every shipped item follows the naming convention, so only third-party mods are affected. The equivalent is already possible in JSON with an EOC use action, `u_has_item_with_flag` and `transform_item`.

```cpp
it->convert( itype_id( it->typeId().str() + "_scba_on" ), p );
it->convert( itype_id( it->typeId().str().substr( 0, it->typeId().str().size() - 3 ) ), p )
it->type->transform_into.value().transform( p, *it, true );
```

**Fix:** Replace these with `transform` actors that set an explicit `"target": "scba_mask_scba_on"`, plus a flag precondition such as `"requires_item_with_flag": "SCBA_TANK_ON"` (or the generic `condition` from USE-J01/USE-J17). Until then, add finalize checks for items that use these iuse names.

### USE-J14 · DIG/DIG_CHANNEL/FILL_PIT/CLEAR_RUBBLE: four copies of one function with hardcoded construction ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:2584`, `src/iuse.cpp:2608`, `src/iuse.cpp:2628`, `src/iuse.cpp:2648`, `src/iuse.cpp:152`

Each function looks up one or two hardcoded construction ids and calls `place_construction`. A tool cannot open its own construction group (trench, foxhole, a "raise earth" wand) without C++. Impact is small: any construction can require the DIG quality and be built from the normal menu, so these are only shortcuts. The unchecked `find_if` cannot crash in practice, because constructions cannot be removed from the factory (see USE-B19).

```cpp
auto const build = std::find_if( cnstr.begin(), cnstr.end(), []( const construction & it ) {
    return it.id == construction_constr_pit; } ); ... place_construction( { build->group, build_shallow->group } );
```

**Fix:** Add a generic actor, validated in `check()` after constructions load, and replace the four functions with it:
```json
{ "type": "place_construction", "constructions": [ "constr_pit", "constr_pit_shallow" ] }
```

### USE-J15 · Generic tools special-case vanilla furniture and item ids (firestarter, weather_tool, vibe); EOC can't read tile radiation
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:1491`, `src/iuse.cpp:7656`, `src/iuse.cpp:7677`, `src/iuse.cpp:4018`, `src/iuse.cpp:2850`, `src/iexamine.cpp:3066`, `src/iexamine.cpp:6402`, `src/iexamine.cpp:7047`

- **firestarter.** It switches into smoker or kiln mode only for five hardcoded furniture ids. `iexamine::kiln_prep`, `smoker_prep` and `smoker_options` hardcode the same ids, so fixing firestarter alone would not help a modded kiln.
- **WEATHER_TOOL.** It already uses flags for thermometer, hygrometer and barometer, but wind and chill readouts are tied to three item ids and radiation to one.
- **VIBE.** The underwater-breathing check lists three rebreather ids.
- **No map radiation for EOCs.** There is no math function for map radiation (only `u_val rad` and `item_rad`), so a modded radiation sensor cannot be built with EOCs. The GEIGER iuse can still be reused.

```cpp
const bool is_smoking_rack = f_id == furn_f_metal_smoking_rack || f_id == furn_f_smoking_rack;
if( ( it->typeId() == itype_weather_reader ) || ( it->typeId() == itype_mws_weather_sensor_array ) || ...
```

**Fix:** Detect smokers and kilns by furniture flags `SMOKER`/`KILN` (or examine_action), in both firestarter and iexamine. Add item flags `ANEMOMETER` and `RADIATION_SENSOR`. Use `can_breathe_underwater()` or a character flag in vibe. Add the math function `radiation( location )`, with an assignable variant.

### USE-J16 · Engine checks the names of hardcoded iuse functions instead of qualities or flags
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/iuse.cpp:2780`, `src/activity_item_handling.cpp:2222`, `src/activity_item_handling.cpp:3559`, `src/activity_item_handling.cpp:3574`, `src/iexamine.cpp:2051`, `src/consumption.cpp:1750`, `src/consumption.cpp:1953`, `src/character.cpp:8171`, `src/activity_item_handling.cpp:4570`, `src/activity_actor.cpp:4282`, `src/monster.cpp:948`, `src/game_inventory.cpp:1133`

Several systems decide behaviour by asking whether an item has a particular C++ iuse name:
- NPC mining zones and the dig tool check JACKHAMMER and PICKAXE.
- Lock examine checks PICK_LOCK, even though a LOCKPICK quality exists.
- Consumption checks PLANTBLECH, SMOKING and ADRENALINE_INJECTOR.
- PETFOOD, BLECH_BECAUSE_UNCLEAN and E_FILE_DEVICE are checked elsewhere.

A modded item can list these names as plain use_action strings and work today. The gap is only when the behaviour comes from an EOC or a custom actor.

```cpp
return it.type->get_use( "PICK_LOCK" ) != nullptr;                       // iexamine.cpp:2051
if( has_trait( trait_THRESH_PLANT ) && food.type->can_use( "PLANTBLECH" ) ) // consumption.cpp:1750
```

**Fix:** Replace the name checks with data, and document the flags in JSON_FLAGS.md:
- `"qualities": [ [ "LOCKPICK", 1 ] ]` for lock examine.
- Flags `MINING_TOOL` / `MINING_TOOL_NO_WALLS` for NPC mining.
- Comestible flags `UNCLEAN_WARNING` and `PLANT_FOOD` instead of the BLECH names.

### USE-J17 · Data-driven actors have no shared `condition` / on-success EOC hook
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/iuse.h:269`, `src/item_factory.cpp:1976`, `src/iuse_actor.cpp:1040`, `src/iuse_actor.cpp:1141`, `src/iuse_actor.cpp:340`, `src/iuse_actor.cpp:6001`, `src/npctalk.cpp:7765`, `src/npctalk.cpp:6357`

The `iuse_actor` base has only `type` and `cost`, and the shared reader adds only `ammo_scale`. Transform, place_monster, place_npc, deploy_furn and heal cannot be gated by a dialogue condition, and they cannot run an EOC on success with access to what they created. Much of this is already reachable through the EOC actor: `transform_item`, `u_spawn_monster` (with `mon_variables`, `true_eocs`), `u_spawn_npc`, and `u_activate` behind a condition. Only actor-specific behaviour is truly unreachable, such as the turret `init_from_item` ammo transfer or a hook after a multi-turn heal.

```cpp
newmon.init_from_item( item_location( *p, &it ) );   // iuse_actor.cpp:1047, no hook on newmon afterwards
```

**Fix:** Read two common optional fields in `use_function_reader_helper` for every actor:
- `"condition"`, checked in `use_function::can_call`, with `"condition_fail_msg"`.
- `"success_eocs"`, run on any non-nullopt result with u = user and npc = item. Context vars `_spawned_monster`, `_spawned_npc` and `_target` are set by the actor.

```json
{ "type": "place_monster", "monster_id": "mon_turret", "success_eocs": [ "EOC_TAG_TURRET" ] }
```

## 2. Bugs, oversights & quirks

### USE-B01 · holster_actor::use picks the wrong holstered item, or reads list end(), when one non-holster stack exists
**Severity:** Critical · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:2943`, `src/iuse_actor.cpp:2950-2963`, `src/item_contents.cpp:1252-1265`, `src/item_contents.cpp:1972-1977`

The menu has a "Holster item" prompt plus one entry per holstered item, so `opts.size() == 1 + H`. The prompt entry is skipped with `ret--` only when `opts.size() != it.num_item_stacks()`. However, `num_item_stacks()` counts every CONTAINER pocket, while `all_holstered_items()` counts only holster pockets. When exactly one stack sits in a non-holster pocket, `ret` is not decremented. Trigger: wear a western gun belt (holster pocket plus ammo loops) with a revolver holstered and one stack of .38 in the loops, activate it and choose "Draw revolver". `std::next( begin, 1 )` is `end()`, the sentinel is read as an `item*`, and `obtain_cost()` segfaults. With several holstered items (tool belt, police belt, tacvest, hiking backpack) choosing any entry silently draws the next item instead.

```cpp
opts.push_back( prompt ); ... // opts = 1 + H
if( opts.size() != it.num_item_stacks() ) { ret--; }
if( ret >= 0 ) { auto iter = std::next( all_items.begin(), ret ); internal_item = *iter; }
```

**Fix:** Always map menu index k >= 1 to `all_items[k-1]` (unconditional `ret--`), bounds-check against `all_items.size()`, and remove the `num_item_stacks()` comparison.

### USE-B02 · PAPR_BLOWER tick dereferences a null Character when the blower is on the ground
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:3715-3759`, `src/item_gun_tool_ammo.cpp:4346`, `src/item_gun_tool_ammo.cpp:4390`, `src/map.cpp:6236-6240`, `src/item_factory.cpp:1876-1879`

Tick actions receive `carrier == nullptr` when an active item is processed on the map. `papr_blower` calls `p->add_msg_player_or_npc` without a guard when the filter is below 10 and on every tick once it is empty. The sibling handlers gasmask, papr_mask and scba_mask all guard with `if( p && ... )`. Nothing deactivates an active PAPR belt on drop. Trigger: drop a running PAPR belt in a gas field with a low filter, unload its filter while it lies on the ground, or let an NPC wearing one die. The game segfaults on the next tick.

```cpp
if( it->ammo_remaining_in_pocket( "papr_blower_filter" ) < 10 ) { p->add_msg_player_or_npc( ... ); }
if( it->ammo_remaining_in_pocket( "papr_blower_filter" ) == 0 ) { p->add_msg_player_or_npc( ... ); it->deactivate(); }
```

**Fix:** Guard both messages with `if( p )` and call `it->deactivate( p )`. Consider a review rule that every iuse tick function must handle `p == nullptr`.

### USE-B03 · BREAK_STICK restores 5% stamina on every press and refunds the time on failure (infinite stamina)
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:8490-8503`, `src/character.cpp:4576-4580`, `src/character_health.cpp:2223-2253`

`break_stick` calls `mod_stamina( +5% of max )` before any check. `mod_stamina` adds its argument, so this is a gain. Both failure branches ("too weak", "won't break") return `nullopt`, and `invoke_item` then resets moves, refunding the 2 seconds. A character with STR < 5 (always fails) or STR 5-11 (fails (12-STR)/7 of the time) can spam activate on a stick for free stamina at zero time cost. Even a successful break restores stamina instead of costing it.

```cpp
p->mod_moves( -to_moves<int>( 2_seconds ) );
p->mod_stamina( static_cast<int>( 0.05f * p->get_stamina_max() ) );
if( p->get_str() < 5 ) { ...; return std::nullopt; }
```

**Fix:** Make the change negative and apply it after the strength checks. Charge the attempt time on failure too, for example by returning 0 instead of `nullopt`.

### USE-B04 · heal_actor stop-bleed power is bleed * skill bonus, so it is 0 at First Aid 0
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:3794-3810`, `src/iuse_actor.cpp:3846-3868`, `src/iuse_actor.cpp:3758-3792`, `src/iuse_actor.cpp:4115-4120`, `doc/JSON/ITEM.md:1457`

The bandage and disinfectant levels compute `base + scaling * bonus`, but `get_stopbleed_level` computes `bleed * prof_bonus` with `prof_bonus = firstaid / 2` (+1/+2 for proficiencies). At First Aid 0 the level is 0, so `pwr = 3 * 0` never exceeds even intensity 1, and every attempt prints "Your dressing is too ineffective for a bleeding of this extent". The default Survivor profession has no First Aid, so new characters cannot stop bleeding with any item. ITEM.md calls `"bleed"` the base value, and the item info "Actual effect on bleeding" shows 0. Both contradict this.

```cpp
float prof_bonus = healer.get_skill_level( skill_firstaid ) / 2; ...
float total_bonus = bleed * prof_bonus;
int pwr = 3 * get_stopbleed_level( healer ); if( pwr > patient.get_effect_int( effect_bleed, healed ) ) ...
```

**Fix:** Use `bleed + bleed_scaling * prof_bonus` with a new `bleed_scaling` JSON field defaulting to `0.25 * bleed`, like the other two levels. At minimum use `bleed * ( 1 + prof_bonus )`.

### USE-B05 · RC car arming checks volume OR weight and ignores put_in failure, destroying the item
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:6798-6803`, `src/item_container.cpp:267-277`, `data/json/items/tool/radio_tools.json:49-54`, `data/json/items/tool/lighting.json:53-76`, `src/iuse.cpp:1362-1366`

The RC car pocket limits volume to 1250 ml and weight to 2 kg, but the precheck uses `||`. The item is removed with `i_rem` and passed to `put_in()`, and the return value is ignored. Trigger: radio-mod an electric lantern (RADIO_MODABLE, 1527 ml, 550 g) and arm the RC car with it. The precheck passes on weight, `put_in` fails on volume with a debugmsg, the lantern is deleted, and "You armed your RC car with electric lantern" still prints.

```cpp
if( put.has_flag( flag_RADIOCARITEM ) && ( put.volume() <= 1250_ml || ( put.weight() <= 2_kilogram ) ) ) {
    ... it->put_in( p->i_rem( &put ), pocket_type::CONTAINER ); }
```

**Fix:** Test `it->can_contain( put )` instead of hardcoded numbers, and only `i_rem` after `put_in` succeeds (or re-add the item on failure).

### USE-B06 · LUMBER deletes the log before the plank activity; an interrupted cut loses the log
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:4230-4240`, `src/iuse.cpp:4254-4258`, `src/iuse.cpp:4276-4277`, `src/activity_actor.cpp:10520-10555`, `src/activity_actor_definitions.h:3570-3594`, `data/json/player_activities.json:810-816`

`iuse::lumber` removes the log and only then assigns `chop_planks_activity_actor`. The actor stores only `moves`, has no `canceled()` override, and spawns output only in `finish()`. ACT_CHOP_PLANKS has `"can_resume": false`. If the 20-minute cut is cancelled, or the player declines to continue after an interruption, the log is gone and nothing is produced. Even if resuming were allowed, using LUMBER again would delete a second log first.

```cpp
if( i.typeId() == itype_log ) { here.i_rem( p->pos_bub(), &i ); cut_log_into_planks( *p ); return 1; }
p->i_rem( &*loc ); cut_log_into_planks( *p );
```

**Fix:** Store the log's `item_location` in the actor and remove it in `finish()` (or spawn it back in `canceled()`). Allow resuming via `can_resume_with_internal`.

### USE-B07 · HOTPLATE charges the heater twice: once on activation and again when the heat activity finishes
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:4714-4727`, `src/iuse.cpp:8262-8279`, `src/iuse.cpp:8226-8232`, `src/activity_actor.cpp:12733-12743`, `src/activity_actor.cpp:12790-12797`, `data/json/items/tool/cooking.json:576-596`, `data/json/items/tool/cooking.json:668-686`, `data/json/items/tool/cooking.json:991-1009`

`iuse::hotplate` returns 1 after queuing `heat_activity_actor`, so `invoke_item` consumes `charges_per_use` immediately. `heat_activity_actor::finish` then consumes `requirements.ammo` from the same heater. The other heat entry points (heat_food, heat_*_items, heatpack) return 0. A heater with exactly enough fuel is drained up front, and `do_turn` then cancels with "You need more energy". The biogas, kerosene and soda-can stoves plus mod items are affected; the propane cooker uses HEAT_SOLID_ITEMS and is not. `oil_cooker` and `popcan_stove` also lack the HOTPLATE quality, so a fire or another hotplate does the heating, yet they are still charged.

```cpp
if( heat_solid_items( p, it, p->pos_bub() ) ) { return 1; }
// finish: heater_data.loc->activation_consume( requirements.ammo, heater_data.loc.pos_bub( here ), &p );
```

**Fix:** Return 0 from `iuse::hotplate` like the other heat entry points, and add the HOTPLATE quality to `oil_cooker` and `popcan_stove`.

### USE-B08 · EOC use_action: "consume" is a no-op for plain items, item location is always character-relative, p dereferenced unguarded
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:6009-6016`, `src/iuse_actor.cpp:6019-6028`, `src/iuse_actor.cpp:6040-6045`, `src/itype.cpp:202-210`, `src/item_gun_tool_ammo.cpp:4390`, `doc/JSON/ITEM.md:1500`, `data/mods/Sky_Island/items.json:457-474`, `data/json/items/comestibles/serums.json:14-27`, `data/json/effects_on_condition/medicine_eocs/regen_serum_eoc.json:13`

1. **`consume` removes nothing for plain items.** `"consume": true` only returns 1. `activation_consume` then consumes `1 * ammo_required()`, which is 0 for non-tool items, so nothing is removed unless the item is SINGLE_USE or has charges. ITEM.md says it "will attempt to consume the item". Core serums and Sky Island's warp beacon work around this with `u_consume_item` inside the EOC.
2. **Item location is always character-relative.** `loc` is always `item_location( *p, &it )`. That is wrong for ALLOWS_REMOTE_USE items on the map and for nested containers, so the item talker's remove and transform operations target the wrong location. The correct helper, `form_loc()`, already exists in the file (line 221).
3. **`p` is dereferenced unguarded.** `need_worn`/`need_wielding` dereference `p` before the null check. The actor is valid as a tick_action, so a tick EOC with `need_worn` would crash on the ground. No current content does this (see USE-J01).

```cpp
if( need_worn && !p->is_worn( it ) ) ...   // before if( p )
loc = item_location( *p->as_character(), &it );
if( consume ) { if( p && !p->has_item( it ) ) { return 0; } return 1; }
```

**Fix:** Implement consume explicitly by removing one item or charge via the item_location (or call `activation_consume` and fall back to removal). Build `loc` with `form_loc( *p, here, pos, it )`. Null-check `p` before the worn and wield checks.

### USE-B09 · Transforming a worn item in place never updates morale's worn-item bookkeeping
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_transformation.cpp:126-138`, `src/iuse_actor.cpp:413-420`, `src/iuse.cpp:3766`, `src/item.cpp:470`, `src/item_degrade.cpp:645`, `src/morale.cpp:992-996`, `src/morale.cpp:1033-1065`, `src/morale.cpp:1136-1159`, `data/json/items/tool_armor.json:177-216`

Every in-place caller passes `dont_take_off = true`: iuse_transform, scba_mask, the six item.cpp sites and item_degrade. That skips the whole `is_worn` block, so `calc_encumbrance`, `update_bodytemp` and `on_worn_item_transform` never run for transformed armor. `player_morale` keeps the old item's coverage and filth counts. Trigger: a VANITY character lowers a blindfold (eyes), raises it (head), then takes it off. The eyes counter stays at 1 and the "constrained" morale penalty persists until save/load. The same applies to FLOWERS with hoods and to FILTHY items for squeamish characters. Leak-level and inventory caches are not invalidated either.

```cpp
transform.transform( p, it, true );   // iuse_transform
if( !dont_take_off && carrier && carrier->is_worn( it ) ) { if( !it.is_armor() ) {takeoff} else {
    calc_encumbrance(); update_bodytemp(); on_worn_item_transform( obj_copy, it ); } }
```

**Fix:** Split the flag. Always run the armor bookkeeping (`on_worn_item_transform`, `calc_encumbrance`, cache invalidation) for worn items, and skip only the `takeoff()` copy when `dont_take_off` is set.

### USE-B10 · good_fishing_spot is missing a '!' on the river check, so the isolated-pond filter never fires
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:1406-1426`, `src/iuse.cpp:1438-1443`, `src/iuse.cpp:1490-1492`, `src/mapgen_functions.cpp:546-561`, `data/json/furniture_and_terrain/terrain-liquids.json:124`

The comment says an isolated body of water with no fish should be rejected. The condition instead requires the OMT to be a river and the tile to lack CURRENT. River water always has CURRENT, so the branch almost never fires. Ponds, swamps and cavern water with no fish monsters always pass, and the fish activity falls back to GROUP_FISH. Because `fishing_rod` calls this once per adjacent fishable tile, the failure message can repeat.

```cpp
if( fishables.empty() && !here.has_flag( ter_furn_flag::TFLAG_CURRENT, pos ) &&
    is_ot_match( "river_", cur_omt, ot_match_type::contains ) &&
    !cur_omt->is_lake() && !cur_omt->is_ocean() && ... ) { "You doubt you will have much luck..."; return false; }
```

**Fix:** Use `!is_ot_match( "river_", ... )` (or a proper water-body OMT check), and print the message once, outside the per-tile loop.

### USE-B11 · Salvage rounds yields down per component instance; crafted items often give 0 basic materials
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:2003-2008`, `src/iuse_actor.cpp:2026-2033`, `src/iuse_actor.cpp:2049-2059`, `src/iuse_actor.cpp:2087-2108`, `src/item_components.cpp:81-93`, `src/recipe.cpp:1251-1257`

`cut_up_component()` truncates each basic-component instance to `int` immediately. Crafted items from reversible recipes store non-charge components as separate item objects, so each call gets `num_adjusted = efficiency`. That is below 1 whenever any loss roll fires: the Fabrication < 5 roll, the dex roll, or any damage. Each instance then yields 0. Trigger: cut up a bellywrap (24 cotton patchworks) at Fabrication 0 and DEX 8. About 66% of the time you get nothing, otherwise all 24. Count-by-charges basic components ignore their charges and contribute at most 1. Found loot uses the uncraft-recipe path and loses less than 1 unit per group.

```cpp
if( curr.type->is_basic_component() ) { int num_actual = static_cast<int>( num_adjusted );
    salvage[curr.typeId()] += num_actual; return; }
for( const item &iter : tvp.second ) { cut_up_component( iter, num_adjusted ); }
```

**Fix:** Accumulate a float per output id, multiply count-by-charges components by `curr.charges`, and round once at the end with `roll_remainder()` so the expected value is preserved.

### USE-B12 · iuse_transform consumes need_fire charges before the underwater, damage and timer checks
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:360-368`, `src/iuse_actor.cpp:370-374`, `src/iuse_actor.cpp:379-386`, `src/character.cpp:5900-5927`

`use_charges_if_avail( itype_fire, need_fire )` runs first and spends a lighter or match charge. Only then does the code check `cant_do_underwater()`, `activation_success()` (fails on damaged items) and the `set_timer` prompt. All three return `nullopt`. Each failed attempt to light a damaged molotov or pipe bomb, or an attempt underwater, burns a fire charge. No shipped item combines `set_timer` and `need_fire`, so the cancelled-timer case only affects mods.

```cpp
if( need_fire ) { if( !p->use_charges_if_avail( itype_fire, need_fire ) ) {...} if( p->cant_do_underwater() ) { return std::nullopt; } }
if( !it.activation_success() ) { ...; return std::nullopt; }
```

**Fix:** Check the fire source with `has_charges()` up front, run all checks and prompts, and consume fire charges just before `transform.transform()`.

### USE-B13 · LUX_METER returns charges_to_use(), so each use drains charges_per_use squared
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:7711-7717`, `src/character.cpp:4589`, `src/item_gun_tool_ammo.cpp:1980-1986`, `src/item_gun_tool_ammo.cpp:1771-1787`, `data/json/items/tool/science.json:1458-1461`

A use function's return value is a use count that `activation_consume` multiplies by `ammo_required()`. `lux_meter` returns `charges_per_use` (10), so each reading costs 100 charges while `ammo_sufficient` checks for 10. With stock batteries the meter gives one reading either way, and the bug only wastes leftover charges. It drains 10x from a UPS or bionic power if a mod enables those.

```cpp
return it->type->charges_to_use();                    // lux_meter
return ammo_consume( qty * ammo_required(), pos, carrier );
```

**Fix:** Return 1, like the other measurement tools.

### USE-B14 · robotcontrol: higher Computers skill makes hacking slower; cancelling still spends a charge
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:5236-5238`, `src/iuse.cpp:5220-5223`, `src/iuse.cpp:5227-5230`, `src/iuse.cpp:5248`, `src/iuse.cpp:5265`, `src/iuse.cpp:5136-5142`

1. **Skill makes hacking slower.** The move cost expands to `1000 - 10*INT + 10*COMPUTER`, so each Computers level adds 10 moves, contradicting the `@EFFECT_COMPUTER` comment.
2. **Cancelling spends a charge.** "No enemy robots in range" and "Never mind" both return 1.
3. **Mismatched mode tests.** Passive mode tests species ROBOT, while combat mode tests `mon_flag_ELECTRONIC`. Data has 16 ROBOT-species monsters without ELECTRONIC (for example exodii turrets and fleshborgs) and 4 ELECTRONIC non-robots, so some allies can be made docile but never switched back.

Only Aftershock and MindOverMatter use ROBOTCONTROL.

```cpp
std::max( 100, 1000 - static_cast<int>( p->get_int() * 10 - p->get_skill_level( skill_computer ) * 10 ) )
if( mons.empty() ) { ...; return 1; }
```

**Fix:** Use `1000 - ( INT*10 + COMPUTER*10 )`, return `nullopt` on cancel or no target, and use species ROBOT in both mode toggles.

### USE-B15 · hackveh subtracts from item::charges on a battery-fed controller, so hacking costs no power
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:7002-7006`, `src/iuse.cpp:7023-7024`, `src/iuse.cpp:7144-7146`, `data/json/items/tool/radio_tools.json:258-270`, `src/savegame_json.cpp:3511-3529`

`remotevehcontrol` stores energy in a MAGAZINE_WELL battery, but `hackveh` does `it.charges -= effort` (and `-= 1` on a declined scan). The battery never drains. On a failed hack `remoteveh` returns 0, so nothing is spent at all. The negative `charges` value is reset to 0 on load, so the only effect is free hacking.

```cpp
it.charges -= 1;  ...  it.charges -= effort;
```

**Fix:** Use `it.ammo_consume( effort * it.ammo_required(), pos, &p )`, or return the effort as the use count from `remoteveh`.

### USE-B16 · First-aid 'precise' check is inverted: the wound care proficiency lowers precision
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:3962-3968`, `src/character_body.cpp:1199-1223`

Both terms test `prof_wound_care` and add +1/+2 when the healer *lacks* it, and `prof_wound_care_expert` is never tested. An untrained healer with PER 8 scores (0+1+2)*4+8 = 20 and is precise. After learning Wound Care the score drops to 8, and the body window stops showing exact HP when treating NPCs.

```cpp
( healer.has_proficiency( proficiency_prof_wound_care ) ? 0 : 1 ) +
( healer.has_proficiency( proficiency_prof_wound_care ) ? 0 : 2 ) ) * 4 + healer.get_per() >= 20
```

**Fix:** `( has_proficiency( prof_wound_care ) ? 1 : 0 ) + ( has_proficiency( prof_wound_care_expert ) ? 2 : 0 )`.

### USE-B17 · JSON fields that silently do nothing: place_npc radius, MOLLE moves, saw_barrel/saw_stock cost
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:1130-1136`, `src/iuse_actor.cpp:1139-1144`, `src/iuse_actor.h:388`, `doc/JSON/ITEM.md:1303`, `src/iuse_actor.cpp:4521-4525`, `src/iuse_actor.cpp:4528-4553`, `src/iuse_actor.cpp:4561-4594`, `doc/JSON/ITEM.md:1546-1553`, `src/iuse_actor.cpp:4381-4408`, `src/iuse_actor.cpp:4444-4471`

- **place_npc.** `load()` never reads the documented `"radius"`, so it is always 1. With `place_randomly = false`, a cancelled prompt falls back via `value_or` to the player's own tile and prints "There is no square to spawn NPC in!".
- **MOLLE.** `molle_attach` and `molle_detach` load `"moves"` but never spend them; the doc admits this.
- **saw_barrel and saw_stock.** Both load `"cost"` but return 0 (there is a Todo in the source).

```cpp
optional( obj, false, "place_randomly", place_randomly, false );  // no "radius"
molle_attach_actor::use(...) { ... it.get_contents().add_pocket( obj ); loc.remove_item(); return 0; }  // moves unused
```

**Fix:** Load `radius`. Apply `p->mod_moves( -moves )` in the MOLLE actors. Return `cost` from the saw actors. Return `nullopt` silently when `choose_adjacent` is cancelled.

### USE-B18 · Fish trap uses the avatar's Survival and XP no matter who placed it; at Survival 0 it never catches
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:1531-1540`, `src/iuse.cpp:1559-1564`, `src/iuse.cpp:1571`, `src/item_gun_tool_ammo.cpp:4385`

`fish_trap_tick` runs with `p == nullptr` (the trap is on the map) and always uses `get_avatar()` for the skill roll and `practice()`. The avatar is trained wherever they are in the reality bubble. Success starts at -50 and each attempt adds `rng( round(surv), round(surv*surv) )`. At Survival 0 that is always 0, so the trap never catches and all bait is consumed each cycle. At Survival 1 each attempt adds exactly 1, which is still nearly always a failure. Nothing makes NPCs place fish traps, so the "NPC placer" aspect is mostly theoretical.

```cpp
avatar &player = get_avatar(); int success = -50; const float surv = player.get_skill_level( skill_survival );
for(...) success += rng( round( surv ), round( surv * surv ) );
```

**Fix:** Store the placer's `character_id` on the trap and use that character's skill, or a fixed base rate. Give Survival 0 a non-zero chance. See also USE-J11 for making the tuning data-driven.

### USE-B19 · Unchecked lookups on modded or misconfigured content (constructions, transform_into)
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/iuse.cpp:2595-2603`, `src/iuse.cpp:2619-2624`, `src/iuse.cpp:2639-2644`, `src/iuse.cpp:2659-2664`, `src/iuse.cpp:3761-3769`, `src/iuse.cpp:4486-4502`, `src/player_activity.cpp:86-90`

1. **Construction lookups.** DIG, DIG_CHANNEL, FILL_PIT and CLEAR_RUBBLE dereference `build->group` without comparing `find_if` to `end()`. A dda-based mod cannot remove constructions, so only a total conversion without dda could hit this.
2. **SCBA_MASK `transform_into`.** SCBA_MASK calls `transform_into.value()` without `has_value()`. A mod item that uses this tick without `transform_into` throws `bad_optional_access`; vanilla items all set it.

The hacksaw part of the original report was rejected by the reviewer: `set_to_null` keeps the actor, so the `static_cast` is safe.

```cpp
auto const build = std::find_if( ... it.id == construction_constr_pit ); ... place_construction( { build->group, build_shallow->group } );
it->type->transform_into.value().transform( p, *it, true );
```

**Fix:** Check `find_if` results and report a clean error. Validate `transform_into` in finalize for SCBA_MASK users, or check `has_value()` (see USE-J13, USE-J14).

## 3. Performance

### USE-P01 · A radio tuned to a weather station rebuilds a 3-day forecast every turn to show 25 characters
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:2265`, `src/iuse.cpp:2278`, `src/iuse.cpp:2281`, `src/iuse.cpp:2303`, `src/iuse.cpp:2308`, `src/weather.cpp:546`, `src/weather.cpp:579`, `src/weather.cpp:581`, `src/weather_gen.cpp:219`, `src/weather_gen.cpp:247`, `src/overmapbuffer.cpp:1793`, `src/overmap.cpp:3674`, `src/item.cpp:4213`, `src/item_gun_tool_ammo.cpp:4390`

RADIO_TICK runs every turn for an active radio, whether carried or on the ground. One radio tower in three is a weather radio. For those, `radio_tick` calls `weather_forecast()` every turn, which does the following:
- copies the weather generator 6 times;
- makes 72 `get_weather()` calls and 72 `get_weather_conditions()` calls (each builds a dialogue and evaluates every weather type's JSON condition, about 1,900 condition evaluations);
- runs two `closest_city()` calls, each of which sorts every city in up to 9 overmaps.

Then `obscure_message` runs per character over the roughly 600-character text, and the result is folded, all to show one 25-character segment. This happens before the `activation_success()` check and whether or not anyone can hear. Eight hours of sleep with the radio on is about 54M condition evaluations. Side effect: `get_weather()` draws from the global RNG and can flip the static `current_winddir`, so an active weather radio changes the real wind direction.

```cpp
message = weather_forecast( tref.abs_sm_pos );                                   // iuse.cpp:2278
std::vector<std::string> segments = foldstring( message, RADIO_PER_TURN );       // :2303
for( int d = 0; d < 6; d++ ) { const weather_generator wgen = get_weather().get_cur_weather_gen();
    for( ... i += 1_hours ) { w_point w = wgen.get_weather( abs_ms_pos, i, g->get_seed() );
        forecast = std::max( forecast, wgen.get_weather_conditions( w ) );     // weather.cpp:579-585
```

**Fix:**
- Cache the folded, obscured segments per (tower, frequency, hour); the forecast only changes hourly.
- Use `std::min_element` instead of sort in `closest_city`.
- Bind the generator by const reference outside the loop.
- Use an RNG-free `get_weather` variant for forecasts.
- Skip building the message unless `activation_success()` holds and the avatar can hear the radio (`geiger_active` does this at `iuse.cpp:2926`).

This should remove about 99% of the per-turn cost and the wind side effect.

### USE-P02 · reveal_map does a separate full-area overmap scan for every terrain id on all 21 z-levels
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:1389`, `src/iuse_actor.cpp:1432`, `src/overmapbuffer.cpp:1498`, `src/overmapbuffer.cpp:1248`, `src/overmapbuffer.cpp:1268`, `src/point.cpp:188`, `src/overmap_terrain.cpp:334`, `data/json/items/book/maps.json:28`

Reading any of the 26 core map items calls `overmap_buffer.find_all` once per terrain entry per z-level (-10 to +10). Each call builds a `(2r+1)^2` closest-first vector (73,441 points at radius 135) and does `get_om_global` plus `is_ot_match` (a `strstr` for plain entries) per point. The military map has 10 terrains at radius 135, which is about 15M checks and 210 large allocations, mostly for targets that exist only at z = 0. The result is a hitch of roughly a second per map read.

```cpp
for( const auto &omt : omt_types ) { for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; z++ ) {
    reveal_targets( tripoint_abs_omt( center.xy(), z ), omt, 0 ); } }
```

**Fix:** Put all `omt_types` into one `omt_find_params.types` vector and call `find_all` once per z-level. `is_findable_location` already loops over `params.types`. Restrict z-levels carefully: lab, sewer, subway, bunker and silo maps target underground tiles, so add an optional JSON `"z_levels"` field rather than hardcoding z = 0. Expect at least a 10-20x speedup.

### USE-P03 · Salvage decomposition copies whole recipes and scans every recipe linearly per component
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:1950`, `src/iuse_actor.cpp:1959`, `src/iuse_actor.cpp:1964`, `src/iuse_actor.cpp:1998`, `src/iuse_actor.cpp:2040`, `src/recipe_dictionary.cpp:105`, `src/map.cpp:1322`, `src/map.cpp:4371`, `src/activity_actor.cpp:11535`

For each non-basic component without stored components (all spawned loot), `find_uncraft_recipe`:
- copies a full `recipe` out of `get_uncraft()`'s const reference, even when it is the null recipe;
- on a miss, walks the whole recipe map (about 5.6k recipes) with `find_if` and returns a copied `std::optional<recipe>`.

`simple_requirements()` (a const reference) is copied by value. The recursive `std::function<void( item, float )>` deep-copies its item argument at every level. This runs for each salvageable item a vehicle wheel crushes (`map::smash_items`), as well as in salvage and longsalvage. Driving through litter can trigger many such scans per turn.

```cpp
recipe uncraft = recipe_dictionary::get_uncraft( x.typeId() );   // returns const recipe&
auto iter = std::find_if( recipe_dict.begin(), recipe_dict.end(), ... ); if( iter != recipe_dict.end() ) { return iter->second; }
std::function<void( item, float )> cut_up_component =
const requirement_data requirements = uncraft->simple_requirements();
```

**Fix:** Build an `itype_id -> const recipe*` index of non-obsolete single-output recipes in `recipe_dictionary::finalize` and return `const recipe *`. Change the lambda signature to `void( const item &, float )` and bind `const requirement_data &`. This should be 10-100x cheaper per lookup.

### USE-P04 · PAPR and SCBA mask ticks scan the whole inventory every turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:3706`, `src/iuse.cpp:3764`, `src/iuse.cpp:3639`, `src/iuse.cpp:3700`, `src/character_inventory.cpp:2194`, `src/visitable.cpp:96`, `src/iuse.cpp:3756`, `src/item.cpp:473`, `src/character_inventory.cpp:2103`

PAPR_MASK and SCBA_MASK ticks call `has_item_with_flag` every turn for each worn active mask, on the avatar and every NPC. That is a full inventory visit with one heap-allocated `item_location` per node. `gasmask` and `papr_mask` also call `set_var( "overwrite_env_resist", ... )` every turn, which allocates a `std::string` and writes a map entry even when nothing changed. `gasmask` calls `ammo_remaining()` up to 3 times per tick. The cost is tens of microseconds per turn while such a mask is active.

```cpp
if( p && !p->has_item_with_flag( flag_PAPR_BLOWER ) ) {
it->set_var( "overwrite_env_resist", it->get_base_env_resist_w_filter() );
```

**Fix:** Use `cache_has_item_with_flag()`. **Caveat:** the cache does not re-check flags on cached entries, and `papr_blower` calls `it->deactivate()` without a Character, which does not clear the cache. Pass the carrier to `deactivate` (see USE-B02) or the mask keeps working after the blower turns off. Call `set_var` only when the value changes, with a static key, and keep `ammo_remaining()` in a local variable.

### USE-P05 · Item action menu repeats an inventory subtree walk for each of ~189 actions
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/item_action.cpp:129`, `src/item_action.cpp:151`, `src/item_action.cpp:154`, `src/item.cpp:3671`, `src/item_action.cpp:288`

`map_actions_to_items` adds all 189 registered actions to `unmapped_actions`. For each top-level item it then calls `get_usable_item( use )` per action, and each call walks the item's content tree with a map lookup per node. The cost is about 189 x N lookups per menu open, plus `can_use` calls (see USE-P08). Side effect: only the first match inside each top-level container is compared, so a cheaper or emptier duplicate deeper in the same bag is never chosen. This is a UI-time hitch only.

```cpp
for( const auto &ia_ptr : item_actions ) { unmapped_actions.insert( ia_ptr.first ); }
for( const item_action_id &use : unmapped_actions ) { item *actual_item = i->get_usable_item( use );
```

**Fix:** Invert the loop. Visit every item once, iterate its `type->use_methods` (usually 1-3), and look each one up in an `unordered_set` of unmapped actions. That is O(N x uses), and every candidate gets compared.

### USE-P06 · EMF detector tick allocates and scans 441 tiles every turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse.cpp:2415`, `src/iuse.cpp:2416`, `src/iuse.cpp:2423`, `src/coordinates.h:1138`, `src/point.cpp:164`, `src/creature_tracker.cpp:310`

Each turn, the active EMF_PASSIVE_ON tick calls `closest_points_first( pos, 10 )`. That builds a raw 441-point vector and a converted copy, then calls `creature_at` (a monster hash lookup plus a linear walk of active NPCs) and `field_at` on every point. Usually nothing is found, so all 441 points are checked. Only one niche item uses this.

```cpp
for( const tripoint_bub_ms &loc : closest_points_first( pos, max ) ) { const Creature *critter = creatures.creature_at( loc );
    ... for( const auto &fd : here.field_at( loc ) ) {
```

**Fix:** Use the allocation-free `find_point_closest_first` (`point.h:376`), which exits early. Alternatively, iterate the nearby monsters and NPCs directly and scan only submaps in range that have a non-zero `field_count`.

### USE-P07 · Instrument tick searches nested inventory each turn to decide 'held or worn'
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:2587`, `src/character_inventory.cpp:665`, `src/character_attire.cpp:2566`, `src/iuse_actor.cpp:2505`

The `musical_instrument_actor` tick (20 core items) calls `get_item_position( &it )` every turn while playing. That function recursively visits the wielded item and every worn item. Because the check recurses, an instrument stored inside a worn backpack also passes the "hold or wear" check. `can_use` uses the stricter `is_worn`/`is_wielding` test. The performance gain is negligible; the main value is the consistency fix.

```cpp
const int inv_pos = p->get_item_position( &it ); if( inv_pos >= 0 || inv_pos == INT_MIN ) {
```

**Fix:** `if( !p->is_worn( it ) && !p->is_wielding( it ) )`.

### USE-P08 · iuse_transform::can_use builds items and searches the whole inventory for the item's location on every call
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/iuse_actor.cpp:453`, `src/iuse_actor.cpp:236`, `src/iuse_actor.cpp:202`, `src/iuse_actor.cpp:187`, `src/iuse_actor.cpp:463`, `src/visitable.cpp:62`, `src/visitable.cpp:88`, `src/item_location.cpp:1016`, `src/game_inventory.cpp:1177`, `src/item_action.cpp:161`

Transform is the most common actor (497 core item types). `can_use` constructs a full `item result` before checking whether the item even has a parent. `transform_fits_parent_pocket` then rebuilds the item's location from a bare `item&`: `has_item` does a full visit, and `find_parent` does one visit per nesting level, each allocating an `item_location` per node. Worn items also build a second `item tmp` and copy the mutation list. With about 1,000 carried items, opening the activate menu costs a few hundred thousand allocations. That is O(N^2) in inventory size, but it is a one-time hitch per menu open, because the denial result is cached per entry.

```cpp
const item result = transform.container.is_empty() ? item( transform.target ) : item( transform.container );
ret_val<void> fits = transform_fits_parent_pocket( p, it, result, here, pos );
if( you.has_item( it ) ) { return form_loc_recursive( you, it ); }
```

**Fix:** Compute the location first and build `result` only after the `!loc->has_parent()` early return. Check OVERSIZE/INTEGRATED/SEMITANGIBLE on the target itype rather than building `tmp`. The proper fix is an `item_location`-aware `can_use`/`can_call` overload, since game_inventory, item_action and `avatar::invoke_item` already hold one. That would also remove the same `form_loc()` search from repair_item, holster and firestarter.

## Appendix: considered and rejected

- **Wood processing chain (LUMBER, CHOP_LOGS, planks) is fully hardcoded.** Constructions accept any `pre_terrain` with generic byproducts, recipes turn logs into planks, and tree felling uses the TFLAG_TREE flag. Mods can process their own trees and logs in pure JSON; only the tool shortcuts are hardcoded.
- **Literacy and spell-learning gates check trait ids (ILLITERATE, SORCERER) instead of flags.** `has_trait()` includes traits granted by enchantments, so effects, bionics and items can already grant ILLITERATE, and CANNOT_READ_SPELLBOOKS exists. One small leftover: `learn_spell_actor::use` checks the SORCERER trait rather than that flag, and the main read path skips it.
- **binder_add_recipe re-scans inventory and re-parses the binder per recipe row.** Accurate, but it is a one-shot UI menu build costing milliseconds once per use. Cold path.
- **Radio-signal button copies each item's use_methods map, cloning every actor.** The copy happens only for the few armed devices matched, once per button press. Negligible; a style cleanup at most.

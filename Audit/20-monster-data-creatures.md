# 20 · Monster data, creatures, hordes & butchery

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/mtype.cpp`, `src/monstergenerator.cpp`, `src/mondeath.cpp`, `src/mondefense.cpp`, `src/monexamine.cpp`, `src/monfaction.cpp`, `src/mongroup.cpp`, `src/creature.cpp`, `src/horde_map.cpp`, `src/horde_entity.cpp`, `src/harvest.cpp`, `src/butchery.cpp`, `src/shearing.cpp`

## Summary

The monster data layer loads a lot of JSON, but much of the behaviour behind it still lives in C++. Butchery yield rules key on five hardcoded drop-type ids. On-hit defenses, pet interactions, mount and shear rules, blood/gib fields and horde membership are fixed lists. Effects and deaths give EOCs very little to hook into. Monster-group spawning has the most serious correctness problems: the `NIGHT` condition can never be true, and hard difficulty presets truncate the evolution multiplier to 0, which removes all time gating. The overmap horde code can silently delete zombies, and it can leave state behind that crashes the game on save. The performance items are constant-factor costs in `Creature::sees`, turret target search and butchery item placement. None of them is an algorithmic disaster.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 2 | 12 | 3 | 17 |
| Bugs & quirks | 0 | 4 | 12 | 4 | 20 |
| Performance | 0 | 0 | 3 | 6 | 9 |
| **Total** | **0** | **6** | **27** | **13** | **46** |

**Fix first:**
1. **MONDT-B01** · `NIGHT` spawn condition can never be true. A one-line fix brings back 53 night-only spawn entries across core and mods.
2. **MONDT-B02** · `EVOLUTION_INVERSE_MULTIPLIER` truncated to int. Changing an `int` to `double` restores spawn time gating on every hard difficulty preset.
3. **MONDT-B03** · Horde entities moving to the same tile are deleted. Small fix; it stops hordes from evaporating as they converge on noise.
4. **MONDT-B04** · Empty horde buckets make the save loop dereference `end()`. A few lines remove a possible crash on save.
5. **MONDT-B06** · `DISTRIBUTED_DAMAGE` hit message has no argument. A one-line fix ends a debugmsg popup on every ranged hit for slime and undine characters.

## 1. Missing JSON bindings & EOC features

### MONDT-J01 · Monster-group spawn conditions are unvalidated strings; spawn_data minimal; docs say `name` not `id`
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mongroup.cpp:143`, `src/mongroup.cpp:459`, `src/mongroup.cpp:509`, `src/mongroup.cpp:533`, `src/mongroup.h:41`, `src/mapgen_primitives.h:42`, `doc/JSON/JSON_INFO.md:1849`, `data/core/world_option_sliders.json:53`

Entry `conditions` are free strings compared at spawn time against DAY/NIGHT/DUSK/DAWN and the four seasons. Unknown strings are ignored, so a typo like `"Night"` or `"SUMMMER"` makes the entry spawn at all times. Entries cannot be conditioned on terrain, weather, moon phase, global vars or player state, even though `revive_forms` already accepts dialogue conditions. `spawn_data` only supports ammo and patrol points: no effects, vars, faction, friendliness or name. The loader is hand-rolled rather than built on `generic_factory`, so there is no copy-from or delete. The group-definition table in the docs says the key is `name`, but the code requires `id`, and `auto_total`/`freq_total` are undocumented (`override` is documented in JSON_INHERITANCE.md). The reviewer raised this to high because of the `int upgrade_mult` truncation, which is written up in detail as MONDT-B02.

```cpp
// mongroup.cpp:145-166 – no else branch, no error
if( elem == "DAY" || elem == "NIGHT" || elem == "DUSK" || elem == "DAWN" ) { ... }
if( elem == "SUMMER" || ... )
// mongroup.cpp:459
g.id = mongroup_id( jo.get_string( "id" ) );   // docs: | name | Unique ID.
// mapgen_primitives.h:42
struct spawn_data { std::map<itype_id, jmapgen_int> ammo; std::vector<point_rel_ms> patrol_points_rel_ms; };
```

**Fix:** Validate `conditions` at load time and throw on unknown strings. Add a per-entry dialogue `"condition": { ... }` with context var `loc`. Extend `spawn_data` with `"effects"`, `"vars"`, `"faction"`, `"friendly"`, `"name"` and `"spawn_eocs"` (alpha = spawned monster). Fix the docs (`id`; document `auto_total`, `freq_total`). Use a floating-point multiplier for starts/ends (MONDT-B02).

### MONDT-J02 · Butchery yield rules keyed on 5 hardcoded harvest_drop_type ids; new drop types misbehave
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/butchery.cpp:500`, `src/butchery.cpp:728`, `src/butchery.cpp:744`, `src/butchery.cpp:754`, `src/butchery.cpp:772`, `src/butchery.cpp:803`, `src/butchery.cpp:967`, `src/butchery.cpp:1255`, `src/mondeath.cpp:166`, `src/harvest.cpp:116`, `data/mods/Magiclysm/harvest.json:13`

`harvest_drop_type` is a JSON type, but every rule that matters is a C++ comparison against `flesh`/`bone`/`skin`/`offal`/`blood`. Those comparisons decide the QUICK divisors, the corpse-damage multipliers, gib halving, proficiency penalties, FIELD_DRESS/BLEED/SKIN filtering, menu gating (`has_skin`/`has_organs`/`has_blood`), proficiency training and which drops survive a splatter death. A modder-defined type (`fat`, `chitin`, `venom_gland`) falls into the final `else continue` and gets nothing from QUICK butchery. FIELD_DRESS gives it at full yield, corpse damage never reduces it, and it cannot enable a menu entry. In-repo example: Magiclysm's `type: bionic` entries (adrenal glands, hairballs) in ordinary harvest lists drop at full yield from FIELD_DRESS and then again from FULL butchery of the same corpse. As a result, no mod defines its own drop type.

```cpp
// butchery.cpp:754-766
if( action == butcher_type::QUICK ) {
    if( entry.type == harvest_drop_flesh ) { roll /= 4; }
    else if( entry.type == harvest_drop_bone ) { roll /= 2; }
    ... else if( entry.type == harvest_drop_offal ) { roll /= 5; }
    else { continue; }
}
// mondeath.cpp:165-166  // only flesh and bones survive.
```

**Fix:** Move the behaviour into `harvest_drop_type` JSON: `"action_yield": { "QUICK": 0.25, "FULL": 1.0, "FIELD_DRESS": 0, "SKIN": 0, "BLEED": 0, "DISMEMBER": 0 }`, `"corpse_damage_yield": [1, 1, 0.9, 0.75, 0.5]`, `"gib_yield": 0.5`, `"survives_splatter": true`, `"enables_action": "SKIN"`, `"proficiency_penalties": [ { "proficiency": "prof_skinning_basic", "penalty": 2.0 } ]`. Set these values for the five built-ins in `data/json/harvest_drop_type.json` so current behaviour is unchanged, then delete the id comparisons.

### MONDT-J03 · Milking hardcodes milk_raw/milk; modded milk liquids dereference end() (innawood aphids)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:638`, `src/monster.cpp:648`, `src/monster.cpp:653`, `src/monster.cpp:659`, `src/monster.cpp:663`, `src/monster.cpp:4252`, `src/do_turn.cpp:371`, `src/monstergenerator.cpp:1400`, `src/monexamine.cpp:511`, `src/monexamine.cpp:520`, `data/mods/innawood/monsters/insect_spider.json:7`

MILKABLE reuses `starting_ammo` as the milk definition. `check_monster_definitions` accepts any single liquid there, but `refill_udders` only looks up `milk_raw` and then the legacy `milk`. For any other liquid both lookups return `end()`, and the code reads `current_milk->second`, which is undefined behaviour. This runs from `monster::on_load` and from the daily tick for every MILKABLE monster. The innawood mod ships exactly this case (aphids with `fresh_honeydew`). The legacy `milk` path also erases `current_milk` and then dereferences it. `milk_source` and `milk_activity_actor` already key off `starting_ammo.begin()->first`, so `refill_udders` is the only wrong function. On release libstdc++ this probably works by accident; builds with debug iterators would assert. Other tuning is hardcoded too: refill once per day, 30 s per unit, and only the first ammo entry counts.

```cpp
// monster.cpp:648-659
auto current_milk = ammo.find( itype_milk_raw );
if( current_milk == ammo.end() ) { current_milk = ammo.find( itype_milk ); ... }
// if we got here, we got milk.
if( current_milk->second == type->starting_ammo.begin()->second )
```

**Fix:** Short term, key `refill_udders` off `type->starting_ammo.begin()->first`, keeping milk -> milk_raw only as a migration. Long term, add a dedicated mtype object `"milking": { "item": "fresh_honeydew", "amount": 2, "refill": "1 d", "time_per_unit": "30 s", "skill": "survival", "eoc": "EOC_ON_MILKED" }`, validate it, and stop reusing `starting_ammo`.

### MONDT-J04 · Harvest JSON fields silently ignored: `dissect_only` and `msg_dissect_*`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/harvest.h:45`, `src/harvest.cpp:58`, `src/harvest.cpp:120`, `data/json/harvest_dissect.json:737`

`dissect_only` is loaded but never read anywhere in `src/`, so a mutagen or bionic drop type placed in a normal `harvest` list drops on ordinary butchery. `msg_dissect_fail`/`msg_dissect_success` are loaded and have an accessor, but `dissect_msg()` has no caller, so snippets such as `harvest_drop_tissue_dissect_failed` never show. (Entry `faults` are a third ignored field in some code paths. That part is tracked as MONDT-B08.)

Evidence: grepping `dissect_only()|dissect_msg(` outside `harvest.{h,cpp}` returns nothing. Only `field_dress_msg` and `butcher_msg` are used (`butchery.cpp:842-845`).

**Fix:** In `butchery_drops_harvest`, skip `dissect_only` entries unless `action == DISSECT`, and print `entry.type->dissect_msg( roll > 0 )` in the dissect path. Magiclysm relies on `bionic`-type drops in normal harvest lists, so enforcing `dissect_only` needs a data migration first.

### MONDT-J05 · Death EOC runs before the corpse exists; monster_takes_damage event is too thin
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mtype.h:299`, `src/monstergenerator.cpp:1604`, `src/monster.cpp:3218`, `src/monster.cpp:3249`, `src/mondeath.cpp:288`, `src/monster.cpp:2518`, `src/event.h:506`, `src/effect_on_condition.cpp:603`, `doc/JSON/EFFECT_ON_CONDITION.md:1886`

`death_function.eoc` is a single optional EOC, and it runs before `mdeath::normal`/`splatter`/`broken` creates the corpse. `make_mon_corpse` copies no monster vars, so the EOC cannot tag or modify the corpse. It also receives no context: no overkill damage, damage type or corpse location. (Suppressing the corpse already works through `npc_die { remove_corpse: true }`, but that also loses the item drops.) The `monster_takes_damage` event carries only `{damage, dies}`. When there is no source (bleeding, fields, falls) it is sent with plain `send( e )`. `eoc_events::notify` then falls back to the avatar as alpha, which contradicts the doc table, and any `u_` effect lands on the player when a monster bleeds.

```cpp
// mtype.h:299
std::optional<effect_on_condition_id> eoc;
// monster.cpp:2518-2522
cata::event e = cata::event::make<event_type::monster_takes_damage>( dam, hp < 1 );
if( source ) { send_with_talker( this, source, e ); } else { get_event_bus().send( e ); }
```

**Fix:** Run the death EOCs after the corpse is created (or add `"post_corpse_eoc"`) and expose context vars `corpse_loc`, `overkill` and `damage_type`. Optionally accept an array for `eoc`. For `monster_takes_damage`, always use `send_with_talker( this, source )` and add `mtype_id`, `damage_type` and `bodypart` fields.

### MONDT-J06 · Overmap horde membership hardcoded to ZOMBIE/FERAL species
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/horde_map.cpp:127`, `src/horde_map.cpp:143`, `src/horde_map.cpp:160`, `src/horde_map.cpp:242`, `src/horde_entity.cpp:12`, `src/overmap.cpp:1559`

Only ZOMBIE- or FERAL-species monsters with HEARS become "alert" horde entities that roam and respond to noise. Every other non-DORMANT monster goes into the immobile map, which signal and movement processing skip. Mutant packs, DinoMod herds or a new `BLOB` species can only join horde movement by claiming ZOMBIE/FERAL, and that brings in every other ZOMBIE special case. The tracking intensity for monsters with a destination is a magic `1000` with a TODO.

```cpp
// horde_map.cpp:127-131
static bool is_alert( const mtype &type ) {
    bool aggro = type.in_species( species_ZOMBIE ) || type.in_species( species_FERAL );
    return aggro && type.has_flag( mon_flag_HEARS );
}
```

**Fix:** Add a monster flag `HORDE_ROAMER`, or a species field `"overmap_horde": true` set on ZOMBIE and FERAL in species.json, and check it in `is_alert`. Optionally add an mtype field `"horde_signal_mult"` for noise responsiveness.

### MONDT-J07 · `special_when_hit` only accepts 5 hardcoded C++ defenses; no EOC/spell form, one per monster
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monstergenerator.cpp:714`, `src/monstergenerator.cpp:1051`, `src/mondefense.cpp:51`, `src/mondefense.cpp:99`, `src/mondefense.cpp:196`, `src/mondefense.cpp:234`, `src/monster.cpp:2131`, `src/monster.cpp:4038`, `doc/JSON/MONSTER_SPECIAL_ATTACKS.md:315`, `doc/JSON/EFFECT_ON_CONDITION.md:1886`

`special_when_hit` looks up one entry in a fixed map (NONE, ZAPBACK, ACIDSPLASH, RETURN_FIRE, SHOT_RICOCHET). The tuning is hardcoded: rng(1,5) electric damage to both arms, 4-6 acid drops, dispersion 150 for return fire, 1.5x for ricochet. `is_immune_damage` grants electric immunity by comparing the function pointer to `zapback`. The docs list only 3 of the 5 defenses. Global events (`monster_takes_damage`, `character_melee_attacks_monster`, `character_ranged_attacks_monster`) can already react to hits, so the real gap is a per-monster reaction without a global event filter, plus damage-type and bodypart context.

```cpp
// monstergenerator.cpp:1053
const auto iter = gen.defense_map.find( jsarr.get_string( 0 ) ); ... sp_defense = iter->second;
// monster.cpp:2131
if( dt == damage_electric && type->sp_defense == &mdefense::zapback ) { return true; }
```

**Fix:** Also accept an array of objects: `"special_when_hit": [ { "type": "eoc", "eoc": "EOC_SPORE_BURST", "chance": 50, "melee": true, "ranged": false, "condition": {...} }, { "type": "spell", "spell_data": { "id": "thorn_lash" }, "chance": 25 } ]`, with alpha = monster, beta = attacker and context vars `damage`, `is_ranged`, `bp`, `damage_type`. Move the hardcoded numbers into optional fields (e.g. `"zap_damage": [1,5]`). Replace the function-pointer immunity with a flag or `armor.electric`. Document RETURN_FIRE and SHOT_RICOCHET.

### MONDT-J08 · Mount/saddle/shear/pay-bot/mech-activation rules are hardcoded numbers and item ids
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monexamine.cpp:644`, `src/monexamine.cpp:748`, `src/monexamine.cpp:757`, `src/monexamine.cpp:766`, `src/monexamine.cpp:791`, `src/activity_actor.cpp:9046`, `src/monexamine.cpp:931`, `src/monexamine.cpp:967`, `src/monexamine.cpp:980`

Riding needs survival 1 with a saddle or survival 4 without, and saddling needs survival 1. The mount must be strictly larger than the rider. Shearing is blocked in WINTER for every animal, wool regrows after exactly one season, and only SHEAR quality 1 is checked. RIDEABLE_MECH activation always consumes `id_military`. PAY_BOT always charges `cash_card` at 10 per minute. Only `mountable_weight_ratio` and `mech_battery` are JSON fields. Mounts appear in 34 data files, so the mount and shear rules have real modding value. The pay-bot and mech-card hardcodes matter less.

```cpp
// monexamine.cpp:644-645
( critter.has_effect( effect_monster_saddled ) && get_skill_level( skill_survival ) >= 1 ) || get_skill_level( skill_survival ) >= 4
// monexamine.cpp:748
if( season_of_year( calendar::turn ) == WINTER ) {
```

**Fix:** Add mtype fields `"mount": { "skill": "survival", "level_saddled": 1, "level_unsaddled": 4, "proficiency": null, "min_size_diff": 1 }`. Make shearing an object, keeping the current array as `entries`: `"shearing": { "entries": [...], "regrow": "91 d", "seasons": ["SPRING","SUMMER","AUTUMN"], "quality_level": 1 }`. Add `"activation_item": "id_military"` for mechs and `"pay_bot": { "currency": "cash_card", "charges_per_minute": 10 }`.

### MONDT-J09 · Effect gain/loss/intensity events fire only for Characters; effects have no EOC hooks
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/creature.cpp:1980`, `src/creature.cpp:1997`, `src/creature.cpp:2080`, `src/creature.cpp:2262`, `src/effect.cpp:1538`, `src/event.h:350`

`Creature::add_effect`/`remove_effect` are shared by monsters and characters, but `character_gains_effect`, `character_loses_effect` and `character_effect_intensity_changed` are sent only inside `if( Character *ch = as_character() )`. The `effect_type` loader reads no EOC fields. Death events from `kill_roll` are sent only for the avatar. So nothing in EOC-land can notice a monster becoming `onfire`, `stunned`, `tied` or a modded `marked_for_death`, except by polling. Example: a curse that spawns a wraith when the cursed monster loses it.

```cpp
// creature.cpp:1997-1998
if( Character *ch = as_character() ) {
    get_event_bus().send<event_type::character_gains_effect>( ...
```

**Fix:** Add `effect_type` fields `"apply_eocs"`, `"remove_eocs"` and `"intensity_change_eocs"` (alpha = affected creature; context `bodypart`, `intensity`, `duration`). Alternatively add `monster_gains_effect`/`monster_loses_effect` events sent with `send_with_talker( this, source )` and fields `{ mtype_id, effect, bodypart, intensity }`.

### MONDT-J10 · Per-damage-type side effects hardcoded in `Creature::deal_damage_handle_type` (FIXME)
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/creature.cpp:1658`, `src/creature.cpp:1664`, `src/creature.cpp:1674`, `src/creature.cpp:1694`, `src/monster.cpp:2126`, `src/damage.cpp:122`

The pain divisor is 4, with bash at 5 and acid at 3. Heat rolls `onfire` with chance = damage %. Electric applies `zapped` scaled by `status_chance_multiplier`, the only use of that field. Steel and stone monsters are immune to heat, and ZAPBACK monsters to electric. New damage types can already apply statuses through `ondamage_eocs`, so the remaining gaps are the pain ratio, the core heat/electric behaviour and the material immunity.

```cpp
// creature.cpp:1658-1697
float div = 4.0f;
// FIXME: Hardcoded damage types
if( du.type == damage_bash ) { div = 5.0f; }
else if( du.type == damage_heat ) { if( rng( 0, 100 ) < adjusted_damage ) { add_effect( source, effect_onfire, ...
```

**Fix:** Add damage_type fields `"pain_divisor": 4.0` and `"status_effects": [ { "effect": "onfire", "chance_per_damage": 0.01, "duration": ["1 turns","3 turns"], "scales_with_status_chance": true } ]`, and set them in data for bash/heat/electric/acid. Move the material heat immunity into material JSON (`"immune_damage": ["heat"]`). Expose `status_chance_multiplier` to EOC math.

### MONDT-J11 · Monster blood/gib fields and creature flammability are hardcoded flag/material/species lists
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/mtype.cpp:448`, `src/mtype.cpp:473`, `src/mtype.cpp:501`, `src/mtype.cpp:534`, `src/creature.cpp:174`, `src/creature.cpp:1177`, `src/map_field.cpp:1931`, `src/map_field.cpp:1979`, `src/map_field.cpp:2038`, `src/trapfunc.cpp:1320`, `src/monster.cpp:2080`

`mtype::bloodType` checks fixed flags and materials before falling back to species `bleeds`. `gibType` is entirely hardcoded (MOLLUSK species, then veggy/iflesh/flesh), and neither mtype nor species has a `gibs` field. `Creature::cmat_flesh`/`cmat_fleshnveg`/`cmat_flammable`/`cmat_flameres` are static material sets. They drive incendiary ammo, fire and gas field damage, and traps, so a modded `chitin` or `ectoplasm` material can never count as flammable or flame-resistant (`map_field.cpp:1930` has a TODO saying this should be JSON). `get_meat_itype` and `get_meat_chunks_count` have no callers.

```cpp
// creature.cpp:180-186
const std::set<material_id> Creature::cmat_flammable{ material_paper, material_powder, material_wood, material_cotton, material_wool };
```

**Fix:** Add `"blood_field"` and `"gib_field"` on mtype and `"gibs"` on species, keeping the current logic as the fallback. Add material fields such as `"creature_flammability": "high"|"normal"|"resistant"` and `"breathes": true`, and build the `cmat_*` sets from them in finalize. Delete the dead `get_meat_*` functions.

### MONDT-J12 · Monster attitude is static: no EOC access to a monster's type/faction, faction relations or triggers
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/monfaction.cpp:136`, `src/monfaction.cpp:160`, `src/condition.cpp:2245`, `src/monster.cpp:278`, `src/monster.cpp:3799`, `src/mtype.h:46`, `src/talker_monster.h:61`, `src/monstergenerator.cpp:1193`

The faction attitude cache is built once in `monfactions::finalize`. A monster's faction is only ever set from `type->default_faction` or the internal `make_ally`. The `mon_faction` mutator only takes an mtype id. `talker_monster` has no type-id or faction accessor or setter (reading the type requires the radius-0 `monsters_nearby` trick), and no EOC effect changes a monster's faction or the attitude between factions. anger/fear/placate triggers are a closed 12-value enum.

```cpp
// condition.cpp:2245-2248
if( mutator == "mon_faction" ) { ... ( static_cast<mtype_id>( mtypeid.evaluate( d ) ) )->default_faction.str()
```

**Fix:** Add `{ "u_monster_type": "mon_zombie" }` (condition), `{ "mutator": "u_mon_faction" }`, `{ "u_set_monster_faction": "player_ally" }`, and a global `{ "set_monfaction_attitude": { "from": "mi-go", "to": "triffid", "attitude": "HATE" } }` that rebuilds the cache and is saved with the game. Add a JSON trigger form `"anger_triggers": [ { "condition": {...}, "amount": 5 } ]`.

### MONDT-J13 · Butchery event skipped for QUARTER, has no corpse talker; time/morale tuning hardcoded
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/butchery.cpp:1078`, `src/butchery.cpp:1089`, `src/butchery.cpp:1178`, `src/butchery.cpp:401`, `src/butchery.cpp:355`, `src/butchery.cpp:371`, `src/butchery.cpp:221`, `src/event.h:549`, `doc/JSON/EFFECT_ON_CONDITION.md:1863`

`character_butchered_corpse` is the only butchery hook. `destroy_the_carcass` returns for QUARTER before the event is sent, although the EOC docs list `ACT_QUARTER` as a value. Skipping the event on fatal failure matches the docs. The event has no corpse talker, so EOCs cannot read corpse vars or flags. Harvest lists and entries have no EOC fields. Butcher time by size (900-10800 moves), the action multipliers, cannibalism morale -50/-40, trauma 10/5 and NPC opinion ±5 are all hardcoded.

```cpp
// butchery.cpp:1078-1081 (the event send is at 1178)
if( action == butcher_type::QUARTER ) { butchery_quarter( &corpse_item, you ); return; }
```

**Fix:** Send the event via `send_with_talker( you, corpse_item_location )` for every outcome, with a `success` field. Add `"eocs"` on harvest_list and `"eoc"` on harvest_entry (alpha = butcher, beta = corpse, context `amount`). Add `"time_mult"` on harvest_list (or `"butcher_time"` on mtype), and move the size table and multipliers into butchery_requirements JSON. Make the cannibalism penalties a JSON object.

### MONDT-J14 · Pet interaction menu is closed C++; no JSON interactions or EOC hooks for tame/milk/shear/mount/cull
**Severity:** Medium · **Effort:** Large · **Verified:** confirmed
**Where:** `src/monexamine.cpp:649`, `src/monexamine.cpp:996`, `src/mtype.h:250`, `src/iuse.cpp:1242`, `src/iuse.cpp:1262`, `src/iuse.cpp:1296`, `src/monexamine.cpp:798`, `src/magic_spell_effect.cpp:1609`

The actions in `pet_menu` are a fixed enum gated by flags and effects. Petfood taming is instant and unconditional, and `mon_dog_thing` is special-cased by id. No tame or mount event exists. Some workarounds exist: `chat_topics` on any monster gives conditional, EOC-driven entries; milking and shearing fire the generic activity events (without a monster talker); a `charm_monster` spell with CHARM_PET can tame conditionally. Ranching and companion mods still have to go through dialogue topics for every interaction.

```cpp
// iuse.cpp:1296-1297
mon->friendly = -1; mon->add_effect( effect_pet, 1_turns, true );
```

**Fix:** Add an mtype/species field `"pet_interactions": [ { "id": "brush", "name": "Brush the %s", "key": "B", "condition": { "u_has_item_with_flag": "BRUSH" }, "time": "5 m", "effect": [ ... ] } ]` (alpha = player, beta = monster). Extend petfood with `"chance"`, `"condition"`, `"on_tame_eocs"`, `"on_fail_eocs"` and `"bite_on_feed": true` (replacing the `mon_dog_thing` check). Add events `monster_tamed`, `monster_milked`, `monster_sheared` and `character_mounts_monster` with the monster as beta.

### MONDT-J15 · MONSTERS.md drift: path_settings `allow_unlock_doors`/`avoid_rough_terrain` documented but never loaded
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monstergenerator.cpp:1198`, `src/monstergenerator.cpp:906`, `src/pathfinding.h:154`, `src/pathfinding.h:157`, `doc/JSON/MONSTERS.md:122`, `doc/JSON/MONSTERS.md:135`, `doc/JSON/MONSTERS.md:139`, `doc/JSON/MONSTERS.md:770`, `doc/JSON/MONSTERS.md:773`, `doc/MODDING.md:357`

The path_settings reader does not load `allow_unlock_doors` or `avoid_rough_terrain`, although both exist in `pathfinding_settings`, are honoured by the pathfinder and are documented. Using them gives a load-time unvisited-member error. Other drift: `tracking_distance` is documented as defaulting to 3 (the code default is 8, the minimum 3); `block` and `death_function` are documented as arrays but read as objects; MODDING.md omits the `bleed_rate` adjustment.

Evidence: `monstergenerator.cpp:1201-1208` reads only max_dist, max_length, bash_strength, allow_open_doors, avoid_traps, allow_climb_stairs, avoid_sharp and avoid_dangerous_fields.

**Fix:** Add `optional( jop, was_loaded, "allow_unlock_doors", ... )` and `"avoid_rough_terrain"`, and correct the defaults and types in MONSTERS.md. Consider a shared `pathfinding_settings::load` used by both monster and NPC code.

### MONDT-J16 · MIND_SEEING range is 5 plus four hardcoded bonus effect ids
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:629`, `src/creature.cpp:637`, `src/creature.cpp:702`, `doc/JSON/JSON_FLAGS.md:1226`

Mind-seeing has a fixed 5-tile base that can only grow by stacking `eff_mind_seeing_bonus_5/10/20/30`. It ignores `vision_day`/`vision_night`. MindOverMatter's `telepathic_ignorance` pair is hardcoded in core `Creature::sees`. EOCs can still apply the bonus effects, so this is a minor limitation.

```cpp
// creature.cpp:637-640
int mindsight_bonus_range = ( has_effect( effect_eff_mind_seeing_bonus_5 ) * 5 ) + ...;
int mindsight_vision = 5 + mindsight_bonus_range;
```

**Fix:** Add mtype `"vision_mind": 5` and an enchantment value `MIND_SIGHT_RANGE` to replace the four effects. Replace the telepathic_ignorance pair with an effect flag such as `HIDDEN_FROM_TELEPATHS`.

### MONDT-J17 · mdeath: broken-item id built by string manipulation; splatter/corpse-damage tuning hardcoded
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mondeath.cpp:213`, `src/mondeath.cpp:60`, `src/mondeath.cpp:128`, `src/mondeath.cpp:155`, `src/mondeath.cpp:51`, `tests/monster_test.cpp:619`

With an empty `broken_itype`, `mdeath::broken` strips `mon_` and prepends `broken_`. A unit test validates this for core, but nothing checks it at load time for third-party mods. Corpse damage (2.5 × overflow / max_hp), the pulverize threshold (>5), the 15% gib cap, gib distance 2-4 and the ZOMBIE death sound are all hardcoded. An always-splatter monster is already possible with `corpse_type: SPLATTER`, but there is no "never pulp" option: a pulverized NOGIB monster leaves no corpse.

```cpp
// mondeath.cpp:219-224
//FIXME Remove hardcoded id manipulation
if( item_id.compare( 0, 4, "mon_" ) == 0 ) { item_id.erase( 0, 4 ); }
item_id.insert( 0, "broken_" );
```

**Fix:** In `check_monster_definitions`, require `broken_itype` (or validate the derived id) for BROKEN monsters, then remove the string manipulation. Add `death_function` fields `"pulverize_threshold": 5.0`, `"corpse_damage_mult": 2.5`, `"max_gib_ratio": 0.15` and `"death_sound": "zombie_death"`. Use the per-drop-type `survives_splatter` from MONDT-J02.

## 2. Bugs, oversights & quirks

### MONDT-B01 · Monster-group `NIGHT` spawn condition can never be true; 53 night-only entries never spawn
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mongroup.cpp:148-149`, `src/mongroup.cpp:177`, `src/mongroup.cpp:207-208`, `src/calendar.cpp:258-261`, `src/calendar.cpp:296-324`

`is_spawn_valid()` builds the NIGHT window as `(sunset, sunrise)` and requires `turn > first && turn < second`. Both values are anchored to the solar noon of the current day, so sunrise (~06:00) always comes before sunset (~18:00), and the window is empty. Every NIGHT-only entry is skipped at every hour, and its weight goes to the group default. 53 entries use NIGHT, including core `mon_darkman` at standing stones, Magiclysm wisps and XedraWood night wolves. TEST_DATA only exercises DAY.

```cpp
// mongroup.cpp:148-149, 177
} else if( elem == "NIGHT" ) { valid_times_of_day.emplace_back( sunset, sunrise );
if( calendar::turn > elem.first && calendar::turn < elem.second ) {
```

**Fix:** Treat NIGHT as `turn > sunset || turn < sunrise` (or use `is_night()`). Build all windows with a helper that handles wrapping past midnight, since DUSK/DAWN can also cross day boundaries. Add a unit test that rolls a NIGHT-only group at 02:00.

### MONDT-B02 · `EVOLUTION_INVERSE_MULTIPLIER` truncated to int when scaling starts/ends; hard presets remove all time gating
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mongroup.cpp:453`, `src/mongroup.cpp:509-517`, `src/mongroup.h:41-43`, `data/core/world_option_sliders.json:53`

`LoadMonsterGroup` stores the float option in a `const int`. The difficulty presets 0.91, 0.88 and 0.75, and No_Hope's 0.667, all become 0, so every `starts` (409 in core) and `ends` (105) is multiplied by 0. Late spawns appear from turn 0. Because `ends <= 0` means `lasts_forever()`, early-only spawns never stop. The easy preset 1.5 truncates to 1 and does nothing. `monster.cpp:426/451` uses the same option as a float.

```cpp
// mongroup.cpp:509
const int upgrade_mult = mon_upgrade_factor > 0 ? mon_upgrade_factor : 1;
// mongroup.h:41-42
bool lasts_forever() const { return ends <= 0_turns; }
```

**Fix:** Use `const double upgrade_mult` and scale with `time_duration * upgrade_mult`. Clamp scaled values to at least 1 turn when the source was non-zero, so that `ends` can never become "forever".

### MONDT-B03 · Two horde entities moving to the same tile in one tick: the second is silently deleted
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/overmap.cpp:1584-1590`, `src/overmap.cpp:1628-1633`, `src/overmap.cpp:1646`, `src/overmap.cpp:804`, `src/horde_map.cpp:237-249`

`overmap::move_hordes` moves each entity into a local `migrating_hordes` map keyed by its new tile. `overmap::passable` only checks `hordes.entity_at()`, so a tile already claimed in `migrating_hordes` this tick still looks free. When a second entity picks the same tile, `insert` fails, and the discarded return value, which still owns the node and its monster copy, destroys the zombie. The reviewer traced a concrete case: A at (5,5) moves diagonally to (4,4), and B at (5,4) also picks (4,4). `horde_map::insert` (also used by `overmapbuffer::spawn_monster` and `alert_entity`) discards the result in the same way and returns void. Hordes shrink as they converge on noise.

```cpp
// overmap.cpp:1630-1632
auto monster_node = hordes.extract( moving_mon );
monster_node.key() = viable_candidates.front();
migrating_hordes.insert( std::move( monster_node ) );
```

**Fix:** In the candidate filter, treat keys already in `migrating_hordes` as blocked. Have `horde_map::insert` return the `insert_return_type` (or a bool), and on failure put the node back at its old key or the nearest free tile. Add a debugmsg for dropped nodes.

### MONDT-B04 · `signal_entities` leaves empty idle buckets, so the horde iterator can dereference `end()` while saving
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/horde_map.cpp:179`, `src/horde_map.cpp:191-201`, `src/horde_map.cpp:214-235`, `src/horde_map.cpp:302-319`, `src/horde_map.cpp:414`, `src/overmap.cpp:1788`, `src/sounds.cpp:493`, `src/savegame.cpp:1395`

On every sound louder than 60, `signal_sm()` extracts each idle entity in range with raw `std::unordered_map::extract` and never erases the emptied inner map (`horde_map::erase`/`extract` at :400-420 do cull empty maps). Empty buckets build up until the player loads that submap. `iterator::insure_valid()` skips empty outer maps but assumes the first bucket is non-empty, so `inner_iter = begin()` can equal `end()`. If an overmap's active map is empty when it saves and a leaked empty idle bucket comes first, the save loop dereferences it: undefined behaviour, a null-node crash on libstdc++. Separately, `signal_entities` scans every active and idle bucket on the overmap per sound, whatever the radius, and leaked buckets are rescanned forever. How often the crash happens is unproven, because stragglers left in the active map mask it.

```cpp
// horde_map.cpp:200-201 – bucket left empty
auto monster_node = sm_iter->second.extract( moving_mon );
migrating_hordes.insert( std::move( monster_node ) );
// horde_map.cpp:314-318
// ... horde_map::erase() insures that we cull empty maps ...
inner_iter = outer_iter->second.begin();
```

**Fix:** In `signal_entities`, erase idle buckets that become empty. Make `insure_valid()` reuse `operator++`'s skip loop, advancing while `inner_iter == end`. Optionally index buckets by coarse region so a signal only visits buckets within `volume` submaps.

### MONDT-B05 · `Creature::sees()`: partial-cover visibility uses integer division, so cover always gives a flat 30-tile range
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:732-770`

For crouching, prone or off-z targets, `coverage` and `profile` are both `int`. The branch only runs when `coverage < profile`, so `coverage / profile` is always 0 and `vision_modifier` is always 30. The intended gradient never happens (50% cover against profile 60 should give about 5 tiles). The 0.275 prone factor is also truncated (profile 33). This is a stealth balance problem rather than broken behaviour.

```cpp
// creature.cpp:767-769
if( coverage < profile ) {
    const int vision_modifier = std::max( 30 * ( 1 - coverage / profile ), 1 );
```

**Fix:** `std::max( static_cast<int>( 30.0f * ( 1.0f - static_cast<float>( coverage ) / profile ) ), 1 )`, and keep `profile` as a float.

### MONDT-B06 · `DISTRIBUTED_DAMAGE` hit message has `%2$d` with no argument (debugmsg on every ranged hit); possible null `source`
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:1384-1386`, `src/creature.cpp:1361`, `src/creature.cpp:1367`, `src/creature.h:1041-1047`, `src/string_formatter.cpp:34-42`, `src/ballistics.cpp:581`

When the avatar has DISTRIBUTED_DAMAGE (core SLIMESPAWNER mutation, Xedra undine) and takes ranged damage, the message format string has no argument. `fmt::vsprintf` throws, and `string_vprintf` turns that into a debugmsg popup on every ranged hit. The same function also calls `source->pos_bub( here )` before its own `source != nullptr` checks. `ballistics.cpp` passes nullptr for NULL_SOURCE ammo. In core that only reaches shrapnel with `print_messages=false`, but a modder who adds NULL_SOURCE to ammo would crash the game.

```cpp
// creature.cpp:1386
add_msg_if_player( m_bad, _( "You were hit for %2$d total damage." ) );
// creature.cpp:1361 then 1367
const tripoint_bub_ms source_pos = source->pos_bub( here );  ...  if( source != nullptr )
```

**Fix:** `add_msg_if_player( m_bad, _( "You were hit for %d total damage." ), total_damage );` and compute `source_pos` only inside the non-null branches.

### MONDT-B07 · TRUE_SEEING / TRUESIGHT (and INFRARED_VISION) return "seen" before any line-of-sight check
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:624-627`, `src/creature.cpp:651-656`, `src/creature.cpp:672-675`, `src/character.cpp:6436-6443`, `src/character.cpp:7288-7303`

The docs say TRUE_SEEING/TRUESIGHT let a creature see camouflaged or invisible creatures "normally". The code returns true before the range, light and `here.sees()` checks, so a Magiclysm dragon or nether huntsman tracks an invisible player through walls, and an avatar under TRUE_SEEING sees hidden monsters behind walls. That list feeds safe mode and the monster sidebar. INFRARED_VISION returns a pure distance check with no line of sight, which is plausibly unintended. ALL_SEEING ignoring obstacles is intentional; only its "same z-level" doc wording is wrong.

```cpp
// creature.cpp:651-656
if( ( has_flag( json_flag_TRUE_SEEING ) || has_flag( mon_flag_TRUESIGHT ) ) &&
    ( critter.has_flag( mon_flag_CAMOUFLAGE ) || critter.has_effect_with_flag( json_flag_INVISIBLE ) ... ) ) {
    return true;
}
```

**Fix:** Turn the TRUE_SEEING branch into a bypass of the later invisibility/camouflage rejections, so line of sight and range still apply. For INFRARED_VISION, pass the vision range into `sees( here, critter_pos, ..., range )`. Fix the ALL_SEEING doc wording.

### MONDT-B08 · Harvest entry `faults` are removed instead of added for discrete and liquid drops (salvaged-CBM exploit)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/butchery.cpp:622-624`, `src/butchery.cpp:865-867`, `src/butchery.cpp:897-899`, `src/monster.cpp:3491-3520`, `src/monster.cpp:3504`, `data/json/harvest_dissect.json:736-744`

JSON_INFO.md says entry faults "will be added". `create_charge_items` calls `set_fault`, but the liquid and discrete-item branches call `remove_fault` on a freshly created item, which does nothing. `spawn_dissectables_on_death` only resolves item groups, so single-itype `bionic` entries never get the fault there either. Dissecting `mon_broken_cyborg` therefore yields `bio_power_storage` without `fault_bionic_salvaged`, which skips the factory-reset requirement in `bionics.cpp:2423` and `iuse_actor.cpp:4629`.

```cpp
// butchery.cpp:865-867 (same at 897-899)
for( const fault_id &flt : entry.faults ) { obj.remove_fault( flt ); }
// butchery.cpp:622-624
for( const fault_id &flt : entry.faults ) { obj.set_fault( flt ); }
```

**Fix:** Replace both `remove_fault` calls with `set_fault`. Factor a single `make_harvest_item( entry, corpse )` helper (temperature, rot, flags, faults, activity var) shared by all three branches. Validate fault and flag ids in `harvest_list::check_consistency`.

### MONDT-B09 · Butchering a field-dressed corpse gives more bones than an undressed one (up to ~2x total)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/butchery.cpp:772-776`, `src/butchery.cpp:802-811`, `src/butchery.cpp:812-823`

Field dressing drops `rng(0, roll/2)` bones. A later FULL/QUICK butcher of the dressed corpse rolls bones as `roll/2 + rng(roll/2, roll)`, which is 1x to 1.5x of the undressed amount. Corpse weight is not reduced by dressing, so the base roll is the same. Total bones come to 1x-2x (about 1.5x on average), the opposite of what the comment says. Every field-dressed corpse hits this.

```cpp
// butchery.cpp:808-810
if( entry.type == harvest_drop_bone ) { roll = ( roll / 2 ) + rng( roll / 2, roll ); }
```

**Fix:** Subtract the share removed by dressing (store it as a corpse var), or use `roll = rng( roll / 2, roll )` so the total cannot exceed an undressed butcher.

### MONDT-B10 · Butchery-time proficiency penalties stored in int; skinning proficiency never matters
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/butchery.cpp:467-474`, `src/butchery.cpp:478-494`, `src/butchery.cpp:496`, `src/activity_actor.cpp:13831-13833`

`prof_skin_penalty = 0.5 * (1 - skin_basic)` is always truncated to 0, so skinning proficiency never changes SKIN/FULL/QUICK time. `prof_butch_penalty` can only be 0, 1 or 2, so partial practice is mostly ignored and the advanced proficiency does nothing once basic is learned. Line 496 also uses the avatar's crafting helpers even when an NPC is the one butchering.

```cpp
// butchery.cpp:473-474
int prof_butch_penalty = penalty_big * ( 1 - butch_basic ) + penalty_small * ( 1 - butch_adv );
int prof_skin_penalty = penalty_small * ( 1 - skin_basic );
```

**Fix:** Declare both penalties as `double`, and use `you.get_num_crafting_helpers( 3 )`.

### MONDT-B11 · `mount_items.storage` is written into `tack_item`: bag effect with no bag, saddle overwritten
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:315-320`, `src/monstergenerator.cpp:784-790`, `src/monexamine.cpp:257-283`

The storage branch adds `effect_has_bag` but assigns the item to `tack_item`. The pet menu offers "Place items into bag", and then reports that there is no container. "Remove a bag" says there is no bag. A tack item defined in the same block is overwritten and lost. This is latent in vanilla (the only `mount_items` user does not set storage) and affects mods.

```cpp
// monster.cpp:315-319
if( !type->mount_items.storage.is_empty() ) { ... add_effect( effect_has_bag, 1_turns, true );
    tack_item = cata::make_value<item>( storage_item_item ); }
```

**Fix:** `storage_item = cata::make_value<item>( storage_item_item );`. Add a test that spawns a mount with all four `mount_items` set.

### MONDT-B12 · `Creature::summoner` is never saved; summon kill credit and COPY_SUMMONER_LOOK are lost on reload
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.h:1250`, `src/savegame_json.cpp:4232-4275`, `src/creature.cpp:2427-2448`, `src/monster.cpp:3124-3128`, `src/cata_tiles.cpp:4386-4393`

`Creature::store`/`load` never serialize `summoner`. After a reload, permanent spell or EOC summons stop crediting kills to their caster and fall back to default tiles. `set_summoner(Creature*)` stores only Character ids, so monster-cast summons never have a summoner, and the `as_monster()` branch in `cata_tiles` is dead code.

```cpp
// creature.cpp:2440-2442
if( summoner->as_character() != nullptr ) { this->summoner = summoner->as_character()->getID(); }
```

**Fix:** Add `jsout.member( "summoner", summoner )` / `jsin.read( "summoner", summoner )`. If monster summoners should be supported, store a generic reference rather than a `character_id`.

### MONDT-B13 · Extending a monster group resets `is_animal`/`is_safe`; replacement fields parsed but unused
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/mongroup.cpp:461-465`, `src/mongroup.cpp:475`, `src/mongroup.cpp:547-553`, `src/mongroup.h:96-108`, `src/mapgen.cpp:308-313`, `src/mapgen.cpp:6583-6588`, `src/overmapbuffer.cpp:2289-2297`

A non-override re-declaration copies the old group and then sets `is_animal` and `is_safe` from the new JSON with a default of false. 24 in-repo mod extensions reset these on GROUP_FOREST, SWAMP, RIVER, CAVE and GROUP_SAFE. Mapgen chooses SPAWN_ANIMAL_DENSITY vs SPAWN_DENSITY from `is_animal`, so these groups change whenever the world's zombie-density slider is not 1.0. `is_safe` feeds `overmapbuffer::is_safe` (safe spaces, missions). `replace_monster_group`, `new_monster_group_id` and `replacement_time` are documented but never read.

```cpp
// mongroup.cpp:475, 553
g.is_animal = jo.get_bool( "is_animal", false );
g.is_safe = jo.get_bool( "is_safe", false );
```

**Fix:** Default to the inherited value: `jo.get_bool( "is_animal", g.is_animal )`, and the same for `is_safe`. Implement group replacement, or delete those fields and their doc rows and warn on load.

### MONDT-B14 · mtype loader quirks: block resets on copy-from, upgrades read the wrong object, swim unvalidated, harvest defaults to human, bash_skill int dropped
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monstergenerator.cpp:211`, `src/monstergenerator.cpp:291`, `src/monstergenerator.cpp:923`, `src/monstergenerator.cpp:1036`, `src/monstergenerator.cpp:1100-1108`, `src/monstergenerator.cpp:1183-1191`, `src/monstergenerator.cpp:1630-1646`, `src/monstergenerator.h:119`, `src/monster.cpp:1497-1514`, `src/monmove.cpp:1855-1858`, `src/monmove.cpp:1955`

Several hand-parsed fields have problems with inheritance or validation. (1) `block` uses `get_int(..., 0)`, so a copy-from child that sets only `chance` resets the inherited effectiveness and count to 0. (2) `upgrades` tests `jo.has_bool( "multiple_spawns" )` and `jo.get_int( "spawn_range" )` on the monster object instead of `up`. The "spawn_range mandatory" check never runs, a missing spawn_range silently disables multi-spawn, and `multiple_spawns: false` cannot clear an inherited range. (3) `move_skills` validates `dig` twice and `swim` never; out-of-range swim effectively clamps to 10. (4) Omitting `harvest` silently gives the `human` list (the FIXME says the null check can't fire), affecting about 5 core monsters with NORMAL corpses. (5) `monster_adjustment` ignores unknown stats and filters only by species. (6) `death_map`/`get_death_function` are dead code. (7) An integer `bash_skill` is stored only when <= 0: `30` is ignored, and `0` produces a bash that does 0 damage and wastes the turn (the integer form is undocumented and unused in data).

```cpp
// monstergenerator.cpp:1102, 1107
if( multi && jo.has_bool( "multiple_spawns" ) ) {
jo.get_int( "spawn_range", 0 ); // ignore if defined
// monstergenerator.cpp:1186-1187
if( skill <= 0 ) { bash_skill = {{{damage_bash, skill}}}; }
```

**Fix:** Give `monster_block_data` a `load()` with `was_loaded` semantics. Use `up.has_bool`/`up.get_int`, and clear `upgrade_multi_range` when `multiple_spawns` is false. Change the duplicate dig block to swim. Make a missing `harvest` an error, or default it to null. Throw on unknown adjustment stats, and allow `"monsters"`/`"categories"`/`"flags"` filters. For an integer `bash_skill`, store `{bash: skill}` when > 0 and clear it when <= 0. Delete `death_map`.

### MONDT-B15 · `Creature::longpull`: divide-by-zero at STR/grab_strength 0; spell pulls ignore spell power
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:1740-1742`, `src/magic_spell_effect.cpp:1536-1539`, `src/monstergenerator.cpp:918`, `src/mtype.h:511`, `src/character.cpp:3260-3263`, `data/mods/MindOverMatter/monsters/monsters_spells.json:767-783`, `data/mods/Magiclysm/Spells/alien_mages.json:120-133`

`odds = kg / (str * 3)` uses integer division, and `str` can be 0 (`get_str()` clamps at 0; `grab_strength` has a lower bound of 0), which raises SIGFPE. This is rare in shipped content. The bigger problem is that the `pull_target` spell effect ignores spell level and damage. MoM feral telekinetics and the mi-go nethermancer have `grab_strength` 1, so against a ~80 kg avatar `odds` is about 26 and success is roughly 1/676 per cast, while the message spam says the target resisted.

```cpp
// creature.cpp:1740-1742
const int str = ch != nullptr ? ch->get_str() : mon != nullptr ? mon->get_grab_strength() : 10;
const int odds = units::to_kilogram( c->get_weight() ) / ( str * 3 );
```

**Fix:** Use `std::max( 1, str )`. Give `longpull` an optional strength override, and pass spell damage or level from `pull_to_caster`.

### MONDT-B16 · Butchery adds tool quality to skill level, so survival skill barely affects yields
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/butchery.cpp:559-572`, `src/butchery.cpp:574-589`, `src/butchery.cpp:594-602`, `src/butchery.cpp:641-642`, `src/butchery.cpp:702-704`, `src/butchery.cpp:833`, `src/harvest.cpp:127-135`

`butchery_dissect_skill_level()` is commented as a DISSECT special case, but it runs for every butcher type, adding BUTCHER quality (8-25 for ordinary knives, -100..37 in data) to survival. `skill_score = skill_level / 10` saturates at 1.0 with any decent knife even at survival 0. Tool quality is then counted again in `tool_score` and in `roll_butchery_dissect`. Yields are driven almost entirely by the tool. A -100 quality tool pushes the result to about -98 and zeroes non-mass entries.

```cpp
// butchery.cpp:570
return round( ( sk_total + tool_quality ) / ( sk_count > 0 ? sk_count : 1 ) );
```

**Fix:** Add tool quality only for DISSECT (CUT_FINE), or normalise BUTCHER quality to a 0-5 band first. Keep `tool_score` as the single place tool quality affects yield.

### MONDT-B17 · Melee damage enchantments applied twice when a hit on a mount is redirected to the rider
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:1090`, `src/creature.cpp:1093-1102`, `src/melee.cpp:533-558`, `src/magic_enchantment.cpp:1329-1334`

`deal_melee_hit` modifies `dam` with the attacker's enchantments, then passes the modified value into `mounted_player->deal_melee_hit`, which applies them again. `(v + add) * mult` is not idempotent: a +50% enchantment becomes 2.25x against riders, and flat additions are added twice.

```cpp
// creature.cpp:1090, 1099
dam = source->modify_damage_dealt_with_enchantments( dam );
mons->mounted_player->deal_melee_hit( source, hit_spread, critical_hit, dam, dealt_dam, attack );
```

**Fix:** Apply enchantments after the rider redirect, or pass the original unmodified `dam` to the rider.

### MONDT-B18 · Cancelling the milk-container prompt leaves the animal tied permanently; untying gives a phantom leash
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monexamine.cpp:466-477`, `src/monexamine.cpp:507-576`, `src/monexamine.cpp:525-536`, `src/monexamine.cpp:720-726`, `src/handle_liquid.cpp:758-765`

`milk_source` adds a temporary `effect_tied` before asking for a liquid target. Escaping the prompt (LD_NULL) returns without removing it; the cleanup only exists in `milk_activity_actor`. Untying then runs `untie_pet`'s migration code on every untie, which adds `effect_leashed` with no `tied_item`, so "Remove the leash" returns nothing.

```cpp
// monexamine.cpp:526-536
bool temp_tie = !source_mon.has_effect( effect_tied );
if( temp_tie ) { source_mon.add_effect( effect_tied, 1_turns, true ); }
...
if( liquid_target.dest_opt == LD_NULL ) { return; }
```

**Fix:** Choose the liquid target before tying, or remove the tie on the LD_NULL path when `temp_tie` is set. Gate the untie migration on an old-save marker.

### MONDT-B19 · Death message printed twice for monsters with a death_function, and ignores QUIETDEATH
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monster.cpp:3197`, `src/monster.cpp:3250-3253`, `src/mondeath.cpp:45-57`, `src/monstergenerator.cpp:518-531`, `src/monstergenerator.cpp:1603-1609`, `src/messages.cpp:184`

A `death_function` without `message` defaults to "The %s dies!", which `monster::die` prints with no QUIETDEATH/`quiet_death` check. `mdeath::normal` then prints its own hardcoded copy (its comment admits this). 45 core monsters (`mon_zombie_child`, `mon_civilian_*`, ...) log "The X dies! x 2". The GEN_DORMANT pseudo-type of `mon_zombie_child` keeps the message and prints it when it silently turns into a corpse, despite having QUIETDEATH.

```cpp
// monster.cpp:3197
add_msg_if_player_sees( *this, m_good, type->mdeath_effect.death_message.translated(), name() );
```

**Fix:** Print the death message only in `monster::die`, gated by QUIETDEATH/`quiet_death`/`no_corpse_quiet`. Remove the hardcoded message from `mdeath::normal`, and clear `death_message` on generated pseudo_dormant types.

### MONDT-B20 · Pet "Swap positions" always fails when STR+DEX < 12, and fails 50% for an 8/8 character
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/monexamine.cpp:146-168`, `src/rng.cpp:116-119`

`swap()` succeeds only on `!one_in( (str + dex) / 6 )`, and `one_in(n <= 1)` is always true. Every attempt still costs 150 moves. Pet size and tied state are ignored. Walking into a pet already displaces it for free, so the impact is limited to this menu entry.

```cpp
// monexamine.cpp:154
if( !one_in( ( player_character.get_str() + player_character.get_dex() ) / 6 ) ) {
```

**Fix:** Use a failure chance that shrinks with stats and grows with relative pet size, e.g. `x_in_y( size_diff + 1, str + dex )`, and let friendly pets always swap unless they are tied, panicking or much larger.

## 3. Performance

### MONDT-P01 · `Creature::sees` runs costly observer flag checks and target effect scans before cheap checks
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:621`, `src/creature.cpp:651`, `src/creature.cpp:665`, `src/creature.cpp:785`, `src/monster.cpp:1373`, `src/string_id.cpp:24`, `src/character.cpp:7560`, `src/monmove.cpp:389`, `src/monmove.cpp:574`

`sees()` is called for every monster x candidate target per turn (`monster::plan` -> `rate_target`; `plan()` even checks the player twice) and for every monster on avatar redraws. Almost every pair evaluates the observer's `has_flag( json_flag_TRUE_SEEING )` before checking whether the target is hidden at all. For a monster, `monster::has_flag(flag_id)` interns a temporary string (node allocation), does a factory lookup and then scans the monster's effects. For a Character it scans effects, body parts and martial-arts buffs. The target's effects are scanned for INVISIBLE twice (652 and 665). `has_effect(effect_monster_locked_on)` comes before the trivial `is_avatar()` check. `sees(tripoint)` calls `sight_range` three times and `ambient_light_at` up to three times. This is a constant-factor saving; no profile was taken.

```cpp
// creature.cpp:651
if( ( has_flag( json_flag_TRUE_SEEING ) || has_flag( mon_flag_TRUESIGHT ) ) &&
    ( critter.has_flag( mon_flag_CAMOUFLAGE ) || critter.has_effect_with_flag( json_flag_INVISIBLE ) || ...
// monster.cpp:1375
mon_flag_str_id checked( f.c_str() );
```

**Fix:** Compute the target side once and first (`perm_inv`, `eff_inv`, `hidden`), and check the observer's TRUESIGHT/TRUE_SEEING only when `hidden`. Reuse `eff_inv` at 665. Test `is_avatar()` before `has_effect()` at 621. Cache `ambient_light_at(t)` in a local. In `monster::has_flag(flag_id)`, use a static json_flag -> mon_flag table built at finalize, or call `has_effect_with_flag` directly, so it never allocates.

### MONDT-P02 · `auto_find_hostile_target` runs full line of sight on every hostile before the range check
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:872`, `src/creature.cpp:885`, `src/creature.cpp:918`, `src/turret.cpp:820`, `src/vehicle.cpp:6777`, `src/mattack_actors.cpp:1353`

Every ready, enabled turret calls this each turn, and so do friendly gun-using monsters. It runs `sees()` on every hostile in the bubble, and for in-vehicle turrets a failed `sees()` also does `line_to`, a vector allocation, a temporary `setpos` and a second `sees()`. Only after all that does it reject targets beyond `range + 1` (~20 tiles). The cost scales with turrets x hostiles even when nothing is in range.

```cpp
// creature.cpp:885, 918
if( !sees( here, *m ) ) {
...
int dist = rl_dist( pos_abs(), m->pos_abs() ) + 1;
if( dist > range + 1 || dist < area ) { continue; }
```

**Fix:** Move the distance/area rejection (918-922) to the top of the loop; this preserves behaviour. Better, filter by distance inside the `get_creatures_if` predicate before the attitude call. Also skip candidates whose `( mon_rating + hostile_adj ) / dist <= best_target_rating` before calling `sees()`.

### MONDT-P03 · Butchery adds each yielded item separately, so free-volume rescans make it O(n²)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/butchery.cpp:903`, `src/butchery.cpp:949`, `src/map.cpp:5973`, `src/map.cpp:6040`, `src/map.cpp:6066`, `src/item_stack.cpp:99`, `src/item.cpp:2277`, `src/map.h:1465`, `src/mondeath.cpp:118`

Every non-charge drop, and every ruined chunk, is placed with one `add_item_or_charges_ret_loc` per unit. Each call runs `free_volume`, which sums `item::volume()` (with a `get_var("volume")` lookup) over every item already on the tile. A cow produces roughly 700 meat chunks plus bones and scraps on one tile. Once the tile is full, each further item also runs the overflow search with a `map::route()` per neighbour tile. The result is a noticeable hitch on a very common action.

```cpp
// butchery.cpp:903-904
for( int i = 0; i != roll; ++i ) {
    item_location loc = here.add_item_or_charges_ret_loc( corpse_loc, obj );
// item_stack.cpp:102
for( const item &it : *items ) { ret += it.volume(); }
```

**Fix:** Use the existing batched overload `map::add_item_or_charges( pos, obj, int &copies_remaining, overflow )` (map.cpp:6066), and collect item_locations afterwards by walking the target stacks. Apply the same change to the ruined-parts loop and to `mondeath.cpp` `scatter_chunks`.

### MONDT-P04 · `process_effects` re-evaluates `has_flag(FREEZE_EFFECTS)` for every effect instance every turn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:2243`, `src/creature.cpp:2246`, `src/creature.cpp:1593`, `src/monster.cpp:3757`, `src/character.cpp:7052`

The flag check does not depend on the loop variable, but it runs inside the per-instance loop. For characters that makes the flag work O(E²) per turn; for monsters it goes through the allocating `monster::has_flag(flag_id)` path. The absolute cost is small.

```cpp
// creature.cpp:2243-2246
for( auto &elem : *effects ) { for( auto &_it : elem.second ) {
    if( has_flag( json_flag_FREEZE_EFFECTS ) && !_it.second.has_flag( json_flag_FREEZE_EFFECTS ) ) {
```

**Fix:** Hoist `const bool frozen = has_flag( json_flag_FREEZE_EFFECTS );` and `is_avatar()` out of the loop. The `CANNOT_TAKE_DAMAGE` guard at 1593 benefits from the `monster::has_flag` fix in MONDT-P01.

### MONDT-P05 · Horde entities carrying a full monster are deep-copied at every despawn, load and respawn
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/horde_entity.cpp:19`, `src/horde_map.cpp:155`, `src/horde_map.cpp:169`, `src/savegame.cpp:660`, `src/overmapbuffer.cpp:2169`, `src/overmapbuffer.cpp:2203`, `src/overmap.cpp:1608`

`horde_entity(const monster &)` always copies, and `spawn_entity` only takes a const ref. The load path deserializes into a local and then copies it. Both respawn paths copy `*monster_data` instead of moving it. In `move_hordes`, a blocked entity makes and discards a fresh copy every turn. A few µs per monster per transition.

```cpp
// savegame.cpp:660-662
monster new_monster;
new_monster.deserialize( monster_map_json.next_object() );
result = hordes.spawn_entity( monster_location, new_monster );
```

**Fix:** Add `horde_entity( monster && )` and `spawn_entity( ..., monster && )` overloads, and use `std::move( new_monster )` on load. On respawn, move out of the node, but restore the data when placement fails (the node is reinserted at overmapbuffer.cpp:2178 and kept at overmap.cpp:1612-1615). The despawn critter is still iterated afterwards (game.cpp:10408-10419), so moving there needs care.

### MONDT-P06 · `GetResultFromGroup` recomputes sunrise/sunset, season and an option string for every spawned monster
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/mongroup.cpp:139`, `src/mongroup.cpp:143`, `src/mongroup.cpp:201`, `src/mongroup.cpp:207`, `src/mongroup.cpp:288`, `src/overmap.cpp:1528`, `src/overmap.cpp:3523`, `src/map.cpp:10223`, `src/calendar.cpp:294`

Each call, recursive calls included, recomputes `sunset`/`sunrise` (about two dozen trig operations), the season, and two copies of the `EVENT_SPAWNS` option string. Placing a large city's hordes calls this about 1280 times. All of these values are fixed for the turn. The saving is milliseconds per overmap generation, a small share of overmapgen.

```cpp
// mongroup.cpp:207-211
const time_point sunset = ::sunset( calendar::turn );
const time_point sunrise = ::sunrise( calendar::turn );
const season_type season = season_of_year( calendar::turn );
std::string opt = get_option<std::string>( "EVENT_SPAWNS" );
```

**Fix:** Parse `conditions` into a bitmask at load time (this pairs with the validation in MONDT-J01). Compute sunrise/sunset lazily, only for entries with a time-of-day bit. Cache them, the season and the event flag in a per-turn struct passed into the recursive calls.

### MONDT-P07 · `Creature::get_part` does two `std::map` lookups per body-part accessor call
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/creature.cpp:2557`, `src/creature.cpp:2567`, `src/creature.cpp:2612`, `src/character_body.cpp:574`

`get_part()` calls `get_part_id()` (one `body.find`) and then `body.find()` again on the result, with int_id/string_id round-trips in between. There are hundreds of accessor calls per character per turn. Each saved lookup is tens of ns.

```cpp
// creature.cpp:2557-2558
bodypart *Creature::get_part( const bodypart_id &id ) {
    auto found = body.find( get_part_id( id ).id() );
```

**Fix:** Try `body.find( id.id() )` first and fall back to `get_part_id()` only on a miss. Longer term, consider a flat array indexed by bodypart int_id.

### MONDT-P08 · `butcher_submenu` repeats tool-quality and helper scans 8 × N times when butchering a pile
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/butchery.cpp:394`, `src/butchery.cpp:496`, `src/butchery.cpp:1206`, `src/butchery.cpp:1209`, `src/visitable.cpp:398`, `src/butchery_requirements.cpp:110`, `src/game.cpp:6990`

With "butcher all", the menu loops over 8 butcher types × N corpses. Each pass calls `get_fastest_requirements` and `butcher_time_to_cut`, which runs `max_quality` over the whole inventory and crafting inventory and scans NPCs for helpers. None of this depends on the individual corpse. This is a one-shot UI path, so the cost (tens of ms) only shows for big piles in item-heavy bases.

```cpp
// butchery.cpp:394, 496
const int factor = you.max_quality( action == butcher_type::DISSECT ? qual_CUT_FINE : qual_BUTCHER, PICKUP_RANGE );
time_to_cut *= ( 1.0f - ( get_player_character().get_num_crafting_helpers( 3 ) / 10.0f ) );
```

**Fix:** Precompute a context struct (BUTCHER/CUT_FINE quality, helper count, proficiency practice values) once per menu open, and pass it into a cheap per-corpse `butcher_time_to_cut`. Cache `get_fastest_requirements` per (harvest, size, butcher type).

### MONDT-P09 · horde_map uses four parallel outer maps, so lookups do up to 4 hash probes
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/horde_map.cpp:18`, `src/horde_map.cpp:71`, `src/horde_map.cpp:90`, `src/overmap.cpp:804`, `src/overmap.cpp:1588`, `src/overmapbuffer.cpp:511`, `src/overmap_ui.cpp:2581`

Active, idle, dormant and immobile entities are kept in four separate outer maps, so `entity_at()` probes all four on a miss. That is the common case for `overmap::passable` during horde movement. `entity_group_at` builds and concatenates vectors just so `get_horde_size` can count them for the overmap UI. The file's own TODO (horde_map.cpp:74) describes the fix. The hot-path impact is likely small, because `move_hordes` only walks active entities with about three candidates each.

```cpp
// horde_map.cpp:74
// TODO: It might be worthwhile to have a single top level map of per-submap containers ...
```

**Fix:** Use one `unordered_map<tripoint_om_sm, std::array<inner_map, 4>>`. Add `count_at( omt, filter )` for `get_horde_size` and an `occupied()` helper for `overmap::passable`.

## Appendix: considered and rejected

- `add_effect` allocates a vector per existing effect instance and re-checks a JSON flag on every call – copying an empty vector and inserting an empty range do not allocate, and only a few dozen effect types have non-empty `removes_effects`/`blocks_effects`. The remaining cost is negligible, and the `has_flag` cost is covered by MONDT-P01.

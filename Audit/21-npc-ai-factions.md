# 21 · NPC AI & factions

> Engine audit – static code reading at commit `a1a646d6`; nothing was compiled or run. Each finding was checked against the code by a separate reviewer: **confirmed** = verified by reading the code; **plausible** = consistent with the code, but reachability or impact not proven. Line numbers refer to that commit.

**Scope:** `src/npc.cpp`, `src/npcmove.cpp`, `src/npc_attack.cpp`, `src/npc_class.cpp`, `src/npc_sleep.cpp`, `src/npc_trip.cpp`, `src/npc_arbitration.cpp`, `src/character_oracle.cpp`, `src/faction.cpp`

## Summary

The NPC AI is in the middle of a migration. A JSON behavior tree and need-arbitration layer sit on top of the older `npc::move` code, and much of the decision logic is still hardcoded: item, CBM and class ids, fixed urgency constants, a fixed set of needs, and goal names dispatched by string compare. The `npc_attack` combat evaluator has several real correctness bugs. NPCs never reload or fire energy guns mid-fight, they ignore burst and underbarrel modes, hostile casters are blocked by a follower-only rule, and NPCs don't notice live grenades. The same evaluators are also the main performance cost, because they scan every tile in weapon or spell range for every candidate item and spell on every combat move. The npc, npc_class and faction loaders don't handle mod overrides and `copy-from` correctly.

| Lens | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Missing JSON / EOC | 0 | 3 | 7 | 5 | 15 |
| Bugs & quirks | 0 | 4 | 12 | 3 | 19 |
| Performance | 0 | 2 | 7 | 6 | 15 |
| **Total** | 0 | 9 | 26 | 14 | 49 |

**Fix first:**
1. **NPC-B01** · `npc_attack_gun::can_use` requires `has_ammo()`: a one-line condition is why armed NPCs switch to melee once their magazine is empty.
2. **NPC-B02** · `npc_attack_gun::use` ignores the evaluated gun mode: a one-line fix makes burst, auto and underbarrel modes actually fire.
3. **NPC-B03** · The follower-only "silent weapons" rule blocks spells for every NPC: a one-clause fix that restores hostile caster NPCs in magic mods.
4. **NPC-B06** · Inverted `opens_spellbook` check: one character fixes NPC caster adjustments and stops useless EOC dispatches for every mundane NPC.
5. **NPC-B04** · NPCs never detect live grenades or dynamite: the whole explosive-escape logic is currently dead, and the fix touches only the cache registration and fuse check.

## 1. Missing JSON bindings & EOC features

### NPC-J01 · `BIONIC_NPC_USABLE` is documented and on 52 CBMs, but no code reads it
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npcmove.cpp:379`, `src/npcmove.cpp:389`, `src/npcmove.cpp:396`, `src/npcmove.cpp:2814`, `src/npcmove.cpp:2282`, `src/npcmove.cpp:3066`, `src/npcmove.cpp:3104`, `src/npcmove.cpp:4212`, `src/npcmove.cpp:6418`, `src/npcmove.cpp:7957`, `src/bionics.cpp:635`, `doc/JSON/JSON_FLAGS.md:317`, `doc/JSON/JSON_FLAGS.md:818`, `data/json/flags.json:194`, `data/json/bionics.json:1367`

JSON_FLAGS.md says the flag means "The NPC AI knows how to use this CBM", and its item-info text tells the player "A follower could make use of this CBM". 52 vanilla bionics carry it, as do mods such as aftershock_exoplanet. No C++ reads it. NPC CBM use comes from three fixed id lists (`defense_cbms`, `health_cbms`, `weapon_cbms`) plus five single hardcoded ids (`bio_nanobots`, `bio_painkiller`, `bio_hydraulics`, `bio_radscrubber`, `bio_soporific`). `check_or_use_weapon_cbm()` already works from the `BIONIC_GUN`/`BIONIC_WEAPON` flags, but `activate_combat_cbms` only passes it the 4 ids in `weapon_cbms`. `bio_taser` is flagged and appears in no list, so NPCs never use it. A mod-added bionic gun or defensive CBM is never activated by followers, even though the item info says it will be.

```cpp
// npcmove.cpp:396
const std::vector<bionic_id> weapon_cbms = { { bio_chain_lightning, bio_laser, bio_blade, bio_claws } };
// npcmove.cpp:2816
for( const bionic_id &cbm_id : defense_cbms ) { activate_bionic_by_id( cbm_id ); }
// grep -rn NPC_USABLE src/  -> no hits
```

**Fix:** Build the lists at finalize time from the flags: every `BIONIC_NPC_USABLE` bionic with `BIONIC_GUN`/`BIONIC_WEAPON` goes into the weapon pool. For toggled CBMs, add a bionic object and use it in place of the hardcoded defense/health/hydraulics/painkiller/radscrubber/soporific ids:
```json
"npc_use": { "when": "combat", "priority": 10, "deactivate_after": true }
```
(`when`: `combat` | `always` | `pain` | `wounds` | `radiation` | `before_sleep` | `melee`). Until then, correct the doc and the flag info text.

### NPC-J02 · npc and faction loaders can't be overridden by mods: duplicate ids are silently dropped or half-applied
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc.cpp:544`, `src/npc.cpp:354`, `src/faction.cpp:52`, `src/faction.cpp:518`, `src/faction.cpp:543`, `data/mods/No_Hope/NPC/npc.json:4`

Neither type uses `generic_factory`. `npc_template::load` ends with `std::map::emplace`, which does nothing when the key already exists. A mod that redefines a vanilla `"type": "npc"` id is silently ignored, and `copy-from`/`extend`/`delete` aren't supported. This affects shipped content: No_Hope's redefinitions of `thug` and `bandit` never take effect. `faction_template::load` pushes every definition with `emplace_back` and no duplicate check. `create_if_needed()` then lets the last definition win, but `faction_manager::get()` re-validates each faction on first access from the *first* matching template, which is the vanilla one. The vanilla values overwrite currency, price_rules, lone_wolf_faction, limited_area_claim, name, description, mon_faction and epilogues. The result is a half-applied mod faction: the mod's likes_u, power and wealth stay, while its name, currency and prices revert.

```cpp
npc_templates.emplace( guy.idz, std::move( tem ) );            // npc.cpp:544
npc_factions::all_templates.emplace_back( fac );               // faction.cpp:55
factions[fac_temp.id] = faction( fac_temp );                   // faction.cpp:524 (last wins)
for( const faction_template &fac_temp : npc_factions::all_templates ) {
    if( fac_temp.id == id ) { elem.second.currency = fac_temp.currency; ... break; } } // 551-566 (first wins)
```

**Fix:** Move both types to `generic_factory` so they get override, `copy-from`, `extend` and `delete`. At minimum, have a later definition replace the earlier one (`insert_or_assign`, and erase the old vector entry), and debugmsg on duplicate ids within the same mod.

### NPC-J03 · The NPC behavior tree is only half data-driven: goals, categories and predicates are hardcoded, and the type is undocumented
**Severity:** High · **Effort:** Large · **Verified:** confirmed
**Where:** `src/npcmove.cpp:1907`, `src/npcmove.cpp:2002`, `src/npcmove.cpp:302`, `src/npc_arbitration.cpp:324`, `src/behavior_oracle.cpp:39`, `src/behavior.cpp:178`, `src/character_oracle.cpp:404`, `src/npc_decision_category.h:10`, `src/npcmove.cpp:1806`

Modders can edit `npc_behavior.json`, but the tree can't express anything new:
1. Goals are dispatched with a chain of `new_goal == "..."` compares in `npc::move()`. Any new goal falls through to legacy `address_needs()`.
2. `bt_goal_to_category()` classes unknown goals as `unmodeled`, the lowest category. `should_interrupt()` compares categories by enum order, so a JSON goal can be pre-empted by anything and can never pre-empt anything.
3. Predicates and score functions come from fixed C++ registries, with no dialogue-condition predicate and no math score.
4. The `argument`/`score_argument` fields are parsed and then ignored: all 36 `character_oracle` functions take an unnamed `std::string_view`.
5. Every NPC ticks the global `npc_decision` node. Neither npc_class nor the npc template has a field to pick another tree.
6. No file in `doc/` documents the `behavior` type.

```cpp
} else { // Non-executor needs goal. Legacy address_needs fallback.
    action = address_needs(); }                                   // npcmove.cpp:2001-2004
float character_oracle_t::duty_urgency( std::string_view ) const  // ... return on ? 0.45f : 0.0f;
decision_tree.add( &behavior_node_t_npc_decision.obj() );         // npcmove.cpp:1806
```

**Fix:** Add a predicate `{ "predicate": "condition", "condition": { ... } }`, evaluated with the NPC as `u`, and a score `{ "score": "math", "math": "u_val('blood_need') / 100" }`. Let a leaf declare `"category": "needs"` and `"effect_on_condition": [ ... ]` with an optional `"until"` condition, so JSON goals can run EOCs while committed. Pass `argument` through to the oracle functions as a weight or threshold. Add `"behavior": "my_npc_decision"` to npc_class and npc templates. Document the type in `doc/JSON/NPCs.md`.

### NPC-J04 · Attitude and allegiance changes made by the AI fire no events
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc.cpp:4097`, `src/npc.cpp:1907`, `src/npc.cpp:1978`, `src/npc.cpp:1774`, `src/npcmove.cpp:1680`, `src/game.cpp:1765`, `src/npctalk_funcs.cpp:911`, `src/npctalk_funcs.cpp:975`, `doc/JSON/EFFECT_ON_CONDITION.md:1936`

Every attitude change passes through `npc::set_attitude()`, and it emits no event. The only allegiance event, `npc_becomes_hostile`, is sent only from the dialogue effect `talk_function::hostile`. None of these produce an event an EOC can react to: `make_angry` (the player hit the NPC), faction hostility (`guaranteed_hostile()` in `npc::move`), first-impression aggression (`form_opinion`), panic or flight (`NPCATT_FLEE_TEMP`), mutiny (which also moves the NPC to the hardcoded `amf` faction), recruitment (`talk_function::follow` / `game::add_npc_follower`) and dismissal (`remove_npc_follower`). Bounty, reputation and companion mods have nothing to hook.

```cpp
attitude = new_attitude;                                   // npc.cpp:4133, no event
g->remove_npc_follower( getID() ); set_fac( faction_amf ); // npc.cpp:1928-1936 (mutiny)
get_event_bus().send<event_type::npc_becomes_hostile>( p.getID(), p.name ); // only emitter
```

**Fix:** Send a new `npc_attitude_changed { npc, npc_name, old_attitude, new_attitude, cause }` from `set_attitude`, and send `npc_becomes_hostile` whenever the attitude group becomes hostile. Add `npc_recruited` / `npc_dismissed` from add/remove_npc_follower, and `npc_mutinies { npc, new_faction }` from `mutiny()`. Make the mutiny target data-driven (faction field `"mutiny_faction"`, snippet `<follower_mutiny>`).

### NPC-J05 · Core NPC AI state can't be read by EOCs: attitude, mission/post, combat state, goal, personality, faction relations
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/condition.cpp:2705`, `src/math_parser_diag.cpp:1960`, `src/talker_npc.cpp:255`, `src/npctalk.cpp:7431`, `src/talker_character.cpp:1023`, `src/npc.h:220`

Conditions expose only `npc_following`, `npc_friend` and `npc_hostile`. Math exposes `npc_anger`, `npc_fear`, `npc_value` and `npc_trust`. Nothing reads:
- the attitude enum (MUG, WAIT_FOR_LEAVE, FLEE_TEMP, ...)
- the mission (GUARD, GUARD_PATROL, SHOPKEEP, CAMP_RESIDENT)
- shift or post status
- danger or panic
- the committed BT goal
- need urgencies

Personality can only be read as a dialogue-trial `mod` keyword (`talker_npc.cpp:255-266`), and nothing can write it. `u_set_fac_relation` writes a relation only between the two talkers' factions, and no condition reads relations. Attitude and mission can already be *set* through named talk effects (`hostile`, `follow`, `assign_guard`, `start_mugging`), which is why this is medium rather than high.

```cpp
{ "npc_anger", ...}, { "npc_fear", ...}, { "npc_value", ...}, { "npc_trust", ...} // math_parser_diag.cpp:1960-1963
} else if( attribute == "AGGRESSION" ) { ... modifier = me_npc->personality.aggression; // trial-only
```

**Fix:**
- Math: `npc_personality('aggression')`, readable and assignable, clamped to -10..10; `npc_danger()`, `npc_panic()`, `npc_need_urgency('drink_water')`.
- Conditions: `{ "npc_attitude": "NPCATT_MUG" }`, `{ "npc_mission": "GUARD" }`, `"npc_on_shift"`, `"npc_at_post"`, `{ "npc_goal": "go_to_sleep" }`.
- Effects: `npc_set_attitude`, `npc_set_mission`.
- Factions: `{ "faction_has_relation": [ "free_merchants", "hells_raiders", "kill on sight" ] }` and `{ "set_faction_relation": [ a, b, rule ], "value": true }`.

### NPC-J06 · Spells and activated items are valued only by damage, so NPCs never cast support or summon spells
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:191`, `src/npc_attack.cpp:218`, `src/npc_attack.cpp:231`, `src/npc_attack.cpp:621`, `src/npc_attack.cpp:632`, `src/magic.cpp:716`, `src/npcmove.cpp:2648`

`evaluate_best_attack()` rates every known spell as an attack: `dps * attitude_mult - distance + 1` per affected creature. `spell::dps` returns 0 for any effect other than `attack`, so buff, summon, teleport and EOC-based spells score at most 1, and after the time penalty they are never chosen. Magiclysm's standard heals are attack spells with negative damage. Those do score positively on FRIENDLY targets and get cast, but EOC- or remove_effect-based cures are never cast. Spells have no `NPC_AVOID`-style opt-out flag. `NPC_ACTIVATE` items rate a flat `1`, and only during `emergency()`.

```cpp
damage = attack_spell.dps( source, *critter );                         // npc_attack.cpp:218
int potential = damage * attitude_mult - distance_to_me + 1;           // :231
const int emergency = source.emergency() ? 1 : 0;                      // :638-640
```

**Fix:** Add a spell object `"npc_use": { "role": "attack"|"heal_ally"|"buff_self"|"summon"|"never", "condition": { ... }, "priority": "<math>" }` and a spell flag `NPC_AVOID`. Add the same `npc_use` object for `NPC_ACTIVATE` items. Route non-attack roles through address_needs / heal_player rather than the attack evaluator.

### NPC-J07 · Painkiller use and self-medication are hardcoded to 7 item ids; other medicines are never used
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc.cpp:2684`, `src/npc.cpp:2704`, `src/npc.cpp:2736`, `src/npcmove.cpp:3102`, `src/npcmove.cpp:5369`, `src/npcmove.cpp:5420`

`has_painkiller()` and `most_appropriate_painkiller()` recognise only aspirin, acetaminophen, ibuprofen, codeine, oxycodone, tramadol and heroin, each with a fixed pain target. `took_painkiller()` checks 6 hardcoded effect ids. `rate_food()` rejects every `MED` comestible on purpose, and no other path self-administers medicine, so antibiotics, antifungals, rad pills and mod drugs are never taken. Heroin is scored in `most_appropriate_painkiller` but missing from `has_painkiller`, which gates `npc_use_painkiller`, so an NPC carrying only heroin never treats pain.

```cpp
if( ( pain <= 35 && ( id == itype_aspirin || id == itype_acetaminophen || id == itype_ibuprofen ) )
    || ( pain >= 50 && id == itype_oxycodone ) || id == itype_tramadol || id == itype_codeine )
if( food->comesttype == "MED" ) { return 0.0f; }   // npcmove.cpp:5420
```

**Fix:** Add a comestible field `"npc_use": { "treats": [ "pain" ], "pain_target": 30, "condition": { ... } }`, or derive the target from the consume effects. Allow `"treats": [ "effect:infected", "rad" ]` for other medicines. Have `took_painkiller()` test an effect flag (e.g. `PAINKILLER`) instead of ids.

### NPC-J08 · NPC injury triage only knows bleed, bite and infection; modded injuries are never treated
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npcmove.cpp:2986`, `src/npcmove.cpp:3002`, `src/npc.cpp:2615`, `src/npc.cpp:2657`, `src/iuse_actor.h`

`patient_assessment()` and `has_healing_options()`/`get_healing_item()` check only bleed, bite and infected, plus bandaged and disinfected below an HP threshold, and the matching `heal_actor` fields. Mod injuries (poisons, the fork's own `u_add_wound` wounds, curses) never make an NPC heal itself or the player. Part of the gap is in `heal_actor` itself: its `effects` list holds effects the item *applies*, and it has no way to cure arbitrary effects, for the player either. The HP thresholds are hardcoded, and `std::min( 80, part_threshold )` cuts the intended +20 head / +10 torso boost down to +5.

```cpp
if( c.has_effect( effect_bleed, part_id ) ) ... effect_bite ... effect_infected
int part_threshold = 75; if( head ) part_threshold += 20; ... part_threshold = std::min( 80, part_threshold );
```

**Fix:** Add an effect_type field `"npc_treatable": { "priority": 3 }` and a heal_actor field `"cures": [ "effect_id", ... ]`. patient_assessment collects the treatable effects present, and get_healing_item finds an item whose `cures` covers them. Move the HP thresholds into JSON or an external_option.

### NPC-J09 · NPC drinking from terrain bypasses the consume pipeline and accepts only 2 water ids
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npcmove.cpp:6595`, `src/npcmove.cpp:6771`, `src/npcmove.cpp:6782`, `src/npcmove.cpp:5499`, `src/handle_liquid.cpp:583`, `src/consumption.cpp:1384`

`is_allowed_water_source()` accepts terrain only if its `liquid_source` item is `water` or `water_clean`, so mod liquid sources are ignored. `drink_from_water_source()` then calls `stomach.ingest()` directly. That skips contamination rolls, consumption EOCs, vitamins, `will_eat()` and the `character_consumes_item`/`character_eats_item` events, all of which the player's `consume_activity_actor` path runs. The vanilla contamination is modest (a 1% bad_food chance). The larger gaps are that mod liquids are ignored and on-consume EOCs never fire for NPCs.

```cpp
static const std::set<itype_id> allowed = { itype_water, itype_water_clean };  // npcmove.cpp:6597
stomach.ingest( { intake, 0_ml, {} } );                                        // :6782
```

**Fix:** Build an item from `liquid_source_item_id` and consume a sip through the normal consume path, or apply its contamination, EOCs and events explicitly. Accept any drinkable comestible liquid source the NPC `will_eat()`, optionally gated by an `NPC_DRINKABLE` flag.

### NPC-J10 · Need thresholds, oracle urgencies and sleep tuning are compile-time; the need set is a fixed enum
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc_arbitration.cpp:99`, `src/npc_arbitration.h:194`, `src/character_oracle.cpp:105`, `src/character_oracle.cpp:404`, `src/npcmove.cpp:242`, `src/npc_sleep.cpp:28`, `src/npc.h:611`, `src/npcmove.cpp:3312`, `src/behavior_oracle.cpp:75`, `src/behavior.cpp:192`

All of these are hardcoded:
- need levels in `levels_for()`: thirst 520/800/900 on a 1200 scale, food 0.2/0.4/0.6, and sleepiness tiers
- `need_critical_urgency` (0.75)
- the oracle urgencies: duty 0.45-0.5, follow up to 0.6, goto 0.65, camp work 0.4, return 0.5, free time 0.35
- starvation at `hunger >= 300`
- `camp_routine_meal_kcal = 500`
- the sleep settle and stall timings

A node `score` can only name one of the 11 fixed `score_predicate_map` entries, and those ignore `score_argument`. `need_goal_id` is a 4-value enum (with a `// TODO: Mutation & trait related needs`), so mods can rewire the tree but can't tune urgencies or add a need (blood, mana, recharge).

```cpp
return { npc_thirst_act_level / 1200.0f, 800.0f / 1200.0f, need_critical_urgency }; // npc_arbitration.cpp:104
enum class need_goal_id : int { eat_food, drink_water, regulate_temperature, go_to_sleep };
```

**Fix:** Add an inheritable npc_class object, with the current values as defaults:
```json
"ai_tuning": { "needs": { "drink_water": { "act": 0.43, "insist": 0.67, "emergency": 0.75 } },
               "urgency": { "duty": 0.45, "follow_max": 0.6 }, "sleep": { "settle_timeout": "30 m" } }
```
Longer term, combine with NPC-J03 so custom needs can be declared as urgency math plus EOC executors.

### NPC-J11 · NPC complaints are hardcoded per effect while fields already have `npc_complain`; four documented snippets are never spoken
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:6362`, `src/field_type.cpp:283`, `src/map_field.cpp:1772`, `src/npc.cpp:456`, `src/npc.cpp:485`, `doc/JSON/NPCs.md:280`, `doc/JSON/NPCs.md:301`, `doc/JSON/NPCs.md:330`

`npc::complain()` is a hardcoded sequence over infected, bite, sleepiness, radiation, hunger, thirst, bleed and hypovolemia. `field_type` supports a JSON `npc_complain` object, but `effect_type` has no equivalent, so a mod effect can't make followers complain. `<camp_food_thanks>`, `<need_batteries>`, `<need_booze>` and `<need_fuel>` are loaded and documented as customisable, and NPCs.md's own example overrides `<camp_food_thanks>`, but nothing ever says them.

```cpp
if( has_effect( effect_infected ) ) { ... complain_about( infected_string, time_duration::from_hours( 4 - intensity ), ...
if( jo.has_object( "npc_complain" ) ) {   // field_type.cpp:283
```

**Fix:** Add an effect_type field `"npc_complain": { "speech": "<my_snippet>", "interval": "1 h", "min_intensity": 1, "force_at_intensity": 3 }`, and move the hardcoded effects to JSON. Either wire the four orphan snippets into camp meals and fuel/battery/booze needs, or remove them from the loader and docs.

### NPC-J12 · Starting gear and theft logic special-case class ids; the `<class>_<slot>` item-group convention is undocumented
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc.cpp:1023`, `src/npc.cpp:1366`, `src/npc.cpp:919`, `src/npc.cpp:4140`, `src/npcmove.cpp:2536`, `src/npc.cpp:961`

`starting_inv()` doubles ammo for NC_COWBOY/NC_BOUNTY_HUNTER, adds a molotov for NC_ARSONIST and raises the misc count for NC_EVAC_SHOPKEEP (`// TODO: Move to npc_class`). `starting_weapon()` maps only 9 skills to item groups. Gear comes from implicitly named `<class_id>_pants`/`_misc`/`_rifle`/... groups (265 exist), and NPCs.md never mentions the convention. `carry_override` and `weapon_override` already let a mod class bypass the gear special cases. The real gaps are the hardcoded theft hooks (NC_BOUNTY_HUNTER as guard, NC_EVAC_SHOPKEEP as "behind glass") and the missing docs. Side note: the `skill_throw` branch in `starting_weapon` is dead, because `WEAPONS_ONLY_NO_THROW` excludes throwing.

```cpp
int multiplier = ( type == NC_COWBOY || type == NC_BOUNTY_HUNTER ) ? 2 : 1;   // npc.cpp:1023-1032
item_group::item_from( item_group_id( type.str() + "_" + what ), calendar::turn ); // :922
if( guy->myclass == NC_BOUNTY_HUNTER ) { return 0;                              // :4140
```

**Fix:** Add npc_class fields `"ammo_multiplier": 2`, `"misc_items": [ 2, 6 ]`, `"extra_items": "<group>"`, `"weapon_groups": { "<skill_id>": "<group>" }` with a `<class>_<skill_id>` fallback, and flags `THEFT_WITNESS` / `IGNORES_THEFT`. Document the slot-group naming in NPCs.md.

### NPC-J13 · Template loader: attitude is a raw int and is validated then overwritten; references are unvalidated
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc.cpp:393`, `src/npc.cpp:401`, `src/npc.cpp:402`, `src/npc.cpp:535`, `src/npc.cpp:552`, `doc/JSON/NPCs.md:178`, `doc/JSON/NPCs.md:204`, `src/faction.cpp:118`, `doc/JSON/FACTIONS.md:82`, `src/faction.cpp:58`

Template `attitude` is a raw integer. The loader validates it and falls back to NPCATT_NULL, then line 401 applies the unvalidated value anyway. NPCs.md shows `"mission": 7`, but the loader reads only strings. The strict loader then reports an "Unread data" error rather than silently ignoring it. NPCs.md:204 also wrongly marks `mission` as mandatory. `personality` requires all four keys. `check_consistency()` checks only the class and `TALK_MISSION_OFFER`. It doesn't check the faction, the chat topic, mission ids or death_eocs, so typos surface only at spawn or in dialogue. For factions, FACTIONS.md lists `description` as optional, but the constructor requires it. Faction consistency checks validate only epilogues, not relation target ids, currency or mon_faction. `temp_suffix` and `talk_mission_inquire` are undocumented.

```cpp
if( allowed_attitudes.count( to_set ) > 0 ) { guy.set_attitude( to_set ); }
else { debugmsg(...); guy.set_attitude( NPCATT_NULL ); }
guy.set_attitude( static_cast<npc_attitude>( jsobj.get_int( "attitude" ) ) );  // npc.cpp:401
```

**Fix:** Accept `"attitude": "NPCATT_FOLLOW"` (keeping ints for compatibility) and delete line 401. Fix the doc's mission example and mandatory marker. Default missing personality keys to 0. Extend both `check_consistency` functions: faction validity, topic existence (allowing the built-in hardcoded topics), mission ids, EOC ids, relation keys, currency and mon_faction. Fix FACTIONS.md's optional/mandatory markers.

### NPC-J14 · First-impression opinion is hardcoded to SAPIOVORE, TERRIFYING and drunk/high, with no JSON or EOC hook
**Severity:** Low · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc.cpp:1774`, `src/npc.cpp:1785`, `src/npc.cpp:1803`, `src/npc.cpp:1827`, `src/npc.cpp:1844`, `src/character_health.cpp:2100`, `src/game.cpp:10532`, `src/player_difficulty.cpp:465`

`get_opinion_values()` builds fear and trust from fixed checks: weapon, STR, `SAPIOVORE` (+10 fear), `TERRIFYING` (+6), ugliness, stims and drunk/high effects. `form_opinion()` then maps the result to TALK, KILL or FLEE through hardcoded inequalities. The only JSON lever is ugliness. Reach is limited: `form_opinion` runs only for random NPCs, the starting NPC, camp recruits and pushes, and EOC math can adjust trust and fear later. Side bug: the low-health loop iterates `get_player_character()` instead of `you`. That only matters for `calc_social_value` in player_difficulty.cpp.

```cpp
if( you.has_flag( json_flag_SAPIOVORE ) ) { npc_values.fear += 10; }
if( you.has_trait( trait_TERRIFYING ) ) { npc_values.fear += 6; }
for( ... &elem : get_player_character().get_body() ) {   // npc.cpp:1827, should be `you`
```

**Fix:** Add enchantment values `NPC_FEAR_MOD`, `NPC_TRUST_MOD` and `NPC_VALUE_MOD` (or a mutation/effect field `"npc_opinion": { "fear": 6, "trust": -2 }`) and replace the SAPIOVORE/TERRIFYING checks with them. Add an `npc_forms_opinion` event or an `"on_first_meeting_eocs"` template/class field that runs before `set_attitude`. Use `you` in the HP loop.

### NPC-J15 · The combat threat model hardcodes damage types, fire-only hazards and the tuning constants
**Severity:** Low · **Effort:** Medium · **Verified:** plausible
**Where:** `src/npcmove.cpp:839`, `src/npcmove.cpp:981`, `src/npcmove.cpp:648`, `src/npc.h:79`, `src/npcmove.cpp:914`, `src/monstergenerator.cpp:900`, `src/npcmove.cpp:1714`

`estimate_armour()` sums only bash, cut, stab and bullet armour, so armour against mod damage types doesn't count. The threat-map hazard loop considers only `fd_fire`. Other dangerous fields are still avoided through other paths: escape rating, `could_move_onto`, pathing and the field-flee check. The tuning constants (`NPC_DANGER_VERY_LOW`, `NPC_COWARDICE_MODIFIER`, preferred ranges) are compile-time. The original claim that monster threat can't be tuned is wrong: the monster field `diff` already feeds `get_total_difficulty`, and docile monsters never reach `evaluate_monster`. What remains is a minor heuristic gap.

```cpp
armour_step += candidate.get_armor_type( damage_bash, ... ); ... damage_cut ... damage_stab ... damage_bullet
if( pt == pos_bub() || !here.get_field( pt, fd_fire ) || ...   // npcmove.cpp:981
```

**Fix:** Iterate all damage types, optionally weighted by a damage_type field `"npc_threat_weight"`. The existing `mon_difficulty` flag is a precedent. Add a field_type `"npc_threat": 50` in place of the fd_fire special case. Expose the constants as external_options or an npc_class `"combat_ai": { "cowardice": 0.25, "close_range": 3 }`.

## 2. Bugs, oversights & quirks

### NPC-B01 · `npc_attack_gun::can_use` requires `has_ammo()`: NPCs never reload in combat or fire energy guns
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:499`, `src/npc_attack.cpp:458`, `src/npcmove.cpp:2632`, `src/npcmove.cpp:2616`, `src/item_gun_tool_ammo.cpp:1988`, `src/npcmove.cpp:3514`, `src/npcmove.cpp:1752`, `data/json/items/gun/ups.json:132`, `data/json/items/gun/bio.json:52`

`evaluate_best_attack` builds a gun attack for an empty gun when the NPC can reload, and `npc_attack_gun::use` has a reload branch for that case. But `can_use()` also requires `gun.has_ammo()`. That is false for an empty gun and for `energy_drain` guns with no magazine, such as the UPS `laser_rifle` and `v29`. The reload branch is unreachable. An NPC that empties its rifle mid-fight with spare mags clubs enemies with the rifle until no hostile is within 8 tiles, and only then reloads through `address_needs` (with a 1/3 chance). `enough_time_to_reload()` has no caller. Bionic fake guns are also rejected earlier, at the `can_wield()` gate (npcmove.cpp:2616), because can_wield rejects `BIONIC_WEAPON`.

```cpp
return ( source.is_wielding( *gunmode ) || source.can_wield( *gunmode ).success() ) && gun.has_ammo(); // :502-503
if( it->shots_remaining( here, this ) > 0 || can_reload_current() ) { compare( std::make_shared<npc_attack_gun>( ... // npcmove.cpp:2632
```

**Fix:** Replace `gun.has_ammo()` with `gun.ammo_sufficient( &source ) || ( gun is the wielded weapon && source.can_reload_current() )`. `ammo_sufficient` goes through `shots_remaining`, which handles energy drain and UPS. Handle `BIONIC_WEAPON` guns separately at the `can_wield` gate. Call `enough_time_to_reload()` before the reload branch, or delete it.

### NPC-B02 · `npc_attack_gun::use` ignores the evaluated gun mode and always fires one shot of the base gun
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:492`, `src/npc_attack.cpp:571`, `src/npcmove.cpp:2627`, `src/character.h:3330`, `src/ranged.cpp:1093`, `src/item_gun_tool_ammo.cpp:3133`, `src/npcmove.cpp:2306`

`evaluate_best_attack` creates one `npc_attack_gun` per gun mode, including burst/auto and underbarrel gunmod modes, and rates each by `gunmode.target` damage times `gunmode.qty`. `use()` then calls `fire_gun( location )`, which fires `shots = 1` from the wielded base item. An NPC that picks AUTO fires one round per turn. An NPC that picks an underbarrel shotgun or launcher mode fires the rifle instead. `can_use` and `use` also check the base gun's ammo, never the gunmod's. The old `npc_shoot` path, which handled modes correctly, is dead: nothing assigns `npc_shoot`.

```cpp
source.fire_gun( location );                                    // npc_attack.cpp:492
int fire_gun( const tripoint_bub_ms &target, int shots = 1 );   // character.h:3330
const int damage = gun.gun_damage().total_damage() * gunmode.qty; // :571-572
```

**Fix:** In `use()`: `source.fire_gun( get_map(), location, gunmode.qty, *gunmode.target )`. For gunmods, check the mod's own ammo in `can_use`, or call `gun_set_mode` before firing so UI state and recoil follow. Remove the dead `npc_shoot` path or reuse it.

### NPC-B03 · The follower-only "silent weapons" rule blocks spellcasting for every NPC, including hostiles
**Severity:** High · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:169`, `src/npc.cpp:4165`, `src/npcmove.cpp:2630`, `src/magic.cpp:1557`, `src/melee.cpp:2742`, `src/bionics.cpp:687`

The `npc_follower_rules` constructor sets `use_silent` for every NPC. All other `use_silent` checks (guns, the weapon evaluator, CBM guns) are gated on `is_player_ally()`. `npc_attack_spell::can_use` isn't gated, so it rejects any spell with `sound_volume >= 5`, which means any non-SILENT spell doing about 15+ damage (`|damage| / 3`). Example: the Magiclysm stormshaper class has `lightning_bolt` at level 6 (13-32 damage) and can never cast it as a hostile. Allied casters are also blocked unless the player turns off a rule whose text doesn't mention magic.

```cpp
return attack_spell.can_cast( source ) &&
       !( source.rules.has_flag( ally_rule::use_silent ) && attack_spell.sound_volume( source ) >= 5 ); // :173-175
// guns, npcmove.cpp:2630:
rules.has_flag( ally_rule::use_silent ) && is_player_ally() && !mode.second->is_silent()
```

**Fix:** Gate the spell check like the gun one: `source.is_player_ally() && source.rules.has_flag( ally_rule::use_silent ) && ...`.

### NPC-B04 · NPCs never detect live grenades or dynamite: the explosive scan looks for an `explosion` use_action, but data uses `countdown_action`
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/active_item_cache.cpp:83`, `src/npcmove.cpp:613`, `src/npcmove.cpp:638`, `src/npcmove.cpp:1732`, `src/itype.cpp:242`, `src/item_factory.cpp:4539`, `data/json/items/tool/explosives.json:286`

`find_dangerous_explosives` feeds `npc_escape_explosion` and `in_harms_way`. It only sees items registered as `special_item_type::explosive`, and registration requires `get_use("explosion")`, which searches `use_methods` only. Every vanilla timed explosive (grenade, EMP grenade, dynamite, C4, tear gas: 19 countdown explosions in data/json) keeps its blast in `countdown_action`, a separate field. The only registered items are lit molotovs (power 0, safe_range 1). The fuse check also reads `charges` instead of `countdown_point`. When a live grenade lands next to followers, no NPC moves, warns or interrupts its meal or sleep, so the escape logic is effectively dead.

```cpp
if( it.get_use( "explosion" ) ) { special_items[special_item_type::explosive].emplace_back( ref ); } // active_item_cache.cpp:83
if( elem->charges > turns_to_evacuate ) { continue; }                                              // npcmove.cpp:638
```

**Fix:** In `active_item_cache`, also register items whose `type->countdown_action` is an `explosion_iuse`, or that carry `DANGEROUS`. In `find_dangerous_explosives`, read the actor from `countdown_action` and compute the remaining fuse as `countdown_point - calendar::turn`.

### NPC-B05 · The barehanded attack can never be chosen, and if chosen it "wields fists" instead of punching
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:2596`, `src/npc_attack.cpp:252`, `src/npc_attack.cpp:408`, `src/npc_attack.cpp:430`, `src/item_gun_tool_ammo.cpp:750`, `src/character_inventory.cpp:1314`, `src/visitable.cpp:666`

The fallback `npc_attack_melee( null_item_reference() )` is rated from `base_damage_melee()`, which returns 0 for a null item. `is_wielding()` compares addresses, so it is never true for the static null item and the 3-point wield penalty always applies. The rating is therefore always -3 and never passes `method_of_attack`'s `> 0` test. An unarmed or disarmed NPC with no positively rated item gets `npc_undecided` and only hits by bumping into things. Unarmed damage and martial arts are never considered. A second defect is latent: `use()` would call `wield( null )` (about 100 moves) and return without attacking. That only becomes reachable once the rating is fixed.

```cpp
return source.is_wielding( weapon ) ? 0 : npc_attack_constants::base_time_penalty;   // :410
if( is_null() ) { return 0; }                                                       // item_gun_tool_ammo.cpp:752
return &weapon == &target;                                                          // character_inventory.cpp:1316
```

**Fix:** Treat a null weapon as wielded when `!source.is_armed()`. Rate it with unarmed damage including the martial-arts bonus (e.g. `Character::melee_value`). In `use()`, call `melee_attack` directly when `weapon.is_null() && !is_armed()`.

### NPC-B06 · Inverted check: NPC casters never run `opens_spellbook` EOCs, while spell-less NPCs fire them on every combat move
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:2643`, `src/npcmove.cpp:2648`, `src/handle_action.cpp:1839`, `src/magic.cpp:2758`, `src/magic.cpp:2773`, `src/effect_on_condition.cpp:602`, `src/effect_on_condition.cpp:607`

*(Merges a bug-lens and a perf-lens finding.)* Before rating spells, `evaluate_best_attack` sends `opens_spellbook`, which applies caster_level/cost/casting_time adjustments from EOCs. The condition only fires when `magic->spells().empty()`. NPC casters therefore never get Mind over Matter matrix boosts, `EOC_PAIN_DISABLES_PSI` or Magiclysm/Xedra perk adjustments (about 91 subscriber references in mods). Meanwhile every mundane NPC in a fight dispatches the event on every combat decision. `eoc_events::notify` then allocates alpha-key vectors, searches NPCs and evaluates each subscribed EOC. In vanilla there are no subscribers, so only the bus cost applies. With magic mods, EOCs written for the player's spellbook evaluate for random bandits. `evaluate_opens_spellbook_data` also clamps levels against `get_player_character()` rather than the caster.

```cpp
if( magic->spells().empty() ) {                          // npcmove.cpp:2643
    magic->clear_opens_spellbook_data();
    get_event_bus().send<event_type::opens_spellbook>( getID() );
    magic->evaluate_opens_spellbook_data(); }
sp->get_max_level( get_player_character() )               // magic.cpp:2773
```

**Fix:** Change the condition to `!magic->spells().empty()`, fire at most once per turn per NPC, and hoist the `spells()` vector, which is currently built twice. Pass the caster into `evaluate_opens_spellbook_data`.

### NPC-B07 · The hot-potato throw always targets the NW corner of throw range
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:772`, `src/npc_attack.cpp:776`, `src/npc_attack.cpp:665`, `src/map.cpp:11863`, `src/item_factory.cpp:3994`

For `NPC_THROW_NOW` items (everything `DANGEROUS`, including a primed grenade), `npc_attack_throw::evaluate` returns from inside the loop on the first point of `points_in_radius`, which is always `(x-r, y-r)`. The distance and "+100 unobstructed" scoring never compares anything. If that corner is obstructed or a friend is in the line, `use()` sidesteps through `avoid_friendly_fire` instead of throwing. An NPC indoors can shuffle around with a live grenade until it goes off. Outdoors it throws north-west regardless of who is standing there. This is the only live hot-potato path; `npc::alt_attack` has no callers. It is rare, because NPCs seldom prime explosives.

```cpp
if( throw_now ) { ... int result = base_throw_now + distance_to_me;
    if( !has_obstruction(...) ) { result += 100; }
    return npc_attack_rating( result, potential ); }    // npc_attack.cpp:776-785, inside the per-point loop
```

**Fix:** Keep the best rating over all points. Prefer unobstructed tiles that pass `wont_hit_friend`, lie beyond the explosive's safe_range from self and allies, and are near enemies. Never take the `avoid_friendly_fire` branch while holding an `NPC_THROW_NOW` item.

### NPC-B08 · The gun wield penalty is inverted, which makes NPCs swap guns every turn
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:506`, `src/npc_attack.cpp:510`, `src/npc_attack.cpp:408`

`npc_attack_gun::base_time_penalty` adds the "must change weapon" penalty when the gun *is* wielded. That is the reverse of melee, throw and spell. The wielded item also skips the stack de-dup, so two identical guns are both scored. The unwielded one always wins by 3, `use()` wields it and returns, and the next turn reverses this. The NPC swaps every turn and never fires. For underbarrel modes `*gunmode` is the gunmod, which is never "wielded", so the penalty never applies there.

```cpp
if( source.is_wielding( weapon ) ) { time_penalty += npc_attack_constants::base_time_penalty; } // gun, :510-512
return source.is_wielding( weapon ) ? 0 : npc_attack_constants::base_time_penalty;              // melee, :410
```

**Fix:** `if( !source.is_wielding( gun ) )`, using the base gun rather than `*gunmode`. Add a small hysteresis bonus for the current weapon in `evaluate_best_attack`.

### NPC-B09 · The `assess_danger` early return drops fire threats and leaves the threat map stale
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:980`, `src/npcmove.cpp:1132`, `src/npcmove.cpp:1305`, `src/npcmove.cpp:556`, `src/npc.cpp:3303`

`cur_threat_map` is built from a decayed copy of last turn's map plus fire within 6 tiles. When no hostile is seen, the function returns before the commit at 1305-1311. The fire contribution is therefore discarded in the common no-enemies case, and `ai_cache.threat_map` keeps the last fight's values indefinitely, with no decay; only `reboot()` clears it. `good_escape_direction` uses that map, including when fleeing fire (1714-1723). Directional fire avoidance is lost, and a follower fleeing a burning room still avoids the direction a horde came from hours ago. Adjacent fire tiles are still avoided through `rate_pt`.

```cpp
if( mem_combat.assess_enemy == 0.0 && ai_cache.hostile_guys.empty() ) {
    ...; ai_cache.danger_assessment = mem_combat.assess_enemy; return; }          // :1132-1137
ai_cache.threat_map[threat_dir] = cur_threat_map[threat_dir] + 0.1f * ( ... ); // :1309, only with hostiles
```

**Fix:** Move the commit into a lambda and call it before the early return as well.

### NPC-B10 · `find_item` picks the farthest acceptable item, not the best one
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:4321`, `src/npcmove.cpp:4336`, `src/npcmove.cpp:4382`, `src/npcmove.cpp:4419`, `src/npcmove.cpp:4450`

`consider_item` writes `best_value` but never compares against it. Every item that passes `good_for_pickup` overwrites `wanted_item`/`wanted_item_pos`. Tiles are scanned nearest-first with no break, so the last acceptable item wins, which is the farthest within range 6. A follower walks past a valuable item at its feet to fetch a low-value one 6 tiles away, then rescans from there and can keep drifting outward (bounded by `pickup_reach`).

```cpp
if( ::good_for_pickup( it, *this, p ) ) {
    wanted_item_pos = p; best_value = has_item_whitelist() ? 1000 : value( it ); return true; } // :4347-4350
for( item &it : m_stack ) { if( consider_item( it, p ) ) { wanted_item = item_location{ ... }; } } // :4419-4423
```

**Fix:** Compute `itval` (1000 for a whitelist match, otherwise `value( it )`) and return false unless `itval > best_value`. Optionally weight by distance.

### NPC-B11 · npc_class `copy-from` silently resets inherited stat and personality bonuses to 0
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_class.cpp:179`, `src/npc_class.cpp:245`, `src/npc_class.cpp:98`, `src/npc_class.cpp:276`, `src/npc_class.cpp:290`, `src/npc_class.cpp:314`, `doc/JSON/NPCs.md:29`, `data/json/npcs/robofac/robofac_intercom/robofac_intercom.json:19`, `data/json/npcs/robofac/robofac_intercom/robofac_intercom.json:33`

*(Merges a JSON-lens and a bug-lens finding.)* `load()` assigns all 8 `bonus_*` fields from `load_distribution()`, which returns a constant-0 distribution when the key is absent. A `copy-from` child loses every bonus it doesn't restate. In shipped data, `NC_ROBOFAC_INTERCOM`, `_DAY` and `_NIGHT` lose `bonus_str` (rng 8-10), `bonus_int` and `bonus_per` from `NC_ROBOFAC_INTERCOM_BASE`. The gameplay effect there is small, but it is a trap for modders. Related loader gaps:
- `spells` uses `emplace`, so a child can't change an inherited spell level.
- spells, bionics, skills and mutation_rounds have no extend/delete support.
- `check_consistency()` doesn't validate spell, bionic or proficiency ids, or bionic chance ranges.
- mutation_rounds categories are checked at load time, so a category defined in a later file is reported "Unrecognized" and dropped.
- work_hours has no range check.
- NPCs.md doesn't document work_hours, spells, bionics, mutation_rounds or the skill `bonus` form.

```cpp
if( !jo.has_member( name ) ) { return distribution(); }   // npc_class.cpp:181-183
bonus_str = load_distribution( jo, "bonus_str" );         // :245, unconditional
debugmsg( "Unrecognized mutation category %s", ... ); continue; // :313-315
```

**Fix:** Assign only when present (`if( jo.has_member( "bonus_str" ) ) ...`, same for the other 7). Use `insert_or_assign` for spells and add `extend`/`delete` for spells, bionics, skills and traits. Move the mutation category check into `check_consistency()`, and validate spells, bionics, proficiencies, `0 <= chance <= 100` and work_hours in 0..24 there. Document the missing fields, including work_hours' midnight wrap.

### NPC-B12 · Camp food UI: the legacy `288` unit overstates days of food about 10x, and "Worst vitamin" shows the best
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction.cpp:352`, `src/faction.cpp:380`, `src/faction.cpp:422`, `src/faction_ui.cpp:368`

`food_supply_text`/`food_supply_color` compute days as `kcal / ( size * 288 )`. 288 is the old per-day nutrition unit, while `food_supply()` now returns kcal. `vitamin_stores` already uses about 3000 kcal/day. `size` for `your_followers` is the static JSON value 1 and doesn't track the follower count, so a 10-person camp with 3 person-days of food reads "Overflowing", and size 0 would be an integer division by zero. `vitamin_stores` sorts descending (`x.second > y.second`) although its comment says lowest-first, and the UI labels element 0 as "Worst vitamin"/"Worst toxin".

```cpp
int val = food_supply().kcal() / ( size * 288 );   // faction.cpp:351-352
return x.second > y.second;                        // :424, comment says lowest first
```

**Fix:** Use ~3000 kcal/day, count actual camp members instead of `size`, guard against 0, and flip the comparator to `<`.

### NPC-B13 · `avoid_friendly_fire` with no friends divides by zero (NaN to int is UB)
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:3963`, `src/npcmove.cpp:3976`, `src/coordinates.h:1118`, `src/npc_attack.cpp:467`, `src/npc_attack.cpp:591`

`avoid_friendly_fire` averages friend positions with `midpoint_round_to_nearest`, which divides by `locs.size()` as a float. With no friends that is 0/0 = NaN, stored into an int, which is undefined behaviour (in practice INT_MIN, then signed overflow in `rl_dist`). It is reachable: gun and throw `use()` call it whenever `has_obstruction()` is true, including pure terrain obstruction such as a closed window, and a lone hostile gunman has an empty friend list. In release builds it picks a junk direction rather than crashing. The original claim of a null `current_target()` dereference is not reachable.

```cpp
tripoint_bub_ms center = midpoint_round_to_nearest( fr_pts );          // npcmove.cpp:3976
float num = locs.size(); mid.x = std::round( mid.x / num );            // coordinates.h:1125-1126
```

**Fix:** If `fr_pts` is empty, use `pos_bub()` as the centre or sort only by distance to the target. For pure terrain obstruction, look for a tile with a clear line of fire rather than one "away from friends".

### NPC-B14 · Follower "go to position" order is lost on save/load and never gives up when unreachable
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc.h:1785`, `src/npctalk.cpp:1347`, `src/savegame_json.cpp:2311`, `src/npcmove.cpp:2484`, `src/character_oracle.cpp:518`, `data/json/npcs/npc_behavior.json:38`

`goto_to_this_pos`, set by "Move there!", isn't serialized, so saving and loading cancels the order. While it is set, its urgency (0.65) beats following (max 0.6), and "Follow me" doesn't clear it. The only clear is exact arrival. The command checks only `impassable()`. If no path exists, or a creature stands on the tile, the follower pauses every turn indefinitely with no feedback. The only workaround is to issue a new, reachable order.

```cpp
them->goto_to_this_pos = here.get_abs( *p );                          // npctalk.cpp:1358
if( pos_abs() == *goto_to_this_pos ) { goto_to_this_pos = std::nullopt; } // npcmove.cpp:2488-2490
return 0.65f;                                                         // character_oracle.cpp:529
```

**Fix:** Serialize `goto_to_this_pos`. Add a no-progress counter that clears the order and makes the NPC say "I can't get there". Accept arrival within 1 tile when the target is occupied. Clear the order on "Follow me".

### NPC-B15 · `handle_hostile` dereferences possibly-null friend weak_ptrs
**Severity:** Medium · **Effort:** Small · **Verified:** plausible
**Where:** `src/npcmove.cpp:1170`, `src/npcmove.cpp:1107`, `src/npcmove.cpp:1023`, `src/game.cpp:4388`

The monster loop and the ally loop in `assess_danger` both guard `guy.lock()`, with a HACK comment saying `shared_from` can return nullptr. The `handle_hostile` lambda iterates the same `ai_cache.friends` without the check. `game::shared_from` returns null for a monster when the creature tracker is out of sync (e.g. after `update_pos` refuses a move). If that coincides with a hostile NPC or player in view, the game crashes.

```cpp
if( guy.lock() ) { is_too_close |= too_close( ..., guy.lock()->pos_bub(), def_radius ); } // :1112
is_too_close |= too_close( foe.pos_bub(), guy.lock()->pos_bub(), def_radius );             // :1170, unguarded
```

**Fix:** Lock once into a local and `continue` if null. Better, never push null entries into `ai_cache.friends` (1005, 1011, 1023, 1267).

### NPC-B16 · `npc::attitude_to` cache lookup never matches, and the cache it would read is misclassified
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc.cpp:2900`, `src/npc.cpp:2902`, `src/npc.cpp:2906`, `src/npc.cpp:2916`, `src/npcmove.cpp:1006`, `src/npcmove.cpp:1027`, `src/npcmove.cpp:1028`, `src/npc.h:560`, `src/npc_attack.cpp:365`, `src/npc_attack.cpp:581`, `src/npc_arbitration.cpp:528`

*(Merges a bug-lens and a perf-lens finding.)* `same_as` returns `&lhs == &rhs`, which compares the addresses of two by-value parameters, so it is always false. The three loops over `friends`, `hostile_guys` and `neutral_guys` are dead weight on a very hot function: it runs per creature in every attack evaluator, plus `sees_hostile_within`, `guaranteed_hostile` and `move_to`. `neutral_guys` is filled every move through `g->shared_from` and read nowhere else. On libstdc++ `weak_ptr_fast` isn't atomic, so the direct cost is small. A naive fix (`lhs == rhs`) would expose wrong classifications in `assess_danger`:
- For `is_enemy()` NPCs, every non-friendly NEUTRAL monster (deer, cows) goes into `hostile_guys` (1027).
- Any NPC with attitude != NEUTRAL, including FRIENDLY, goes into `hostile_guys` (1006).

Even today, wildlife feeds bandits' `assess_enemy` and target selection, while `attitude_to` says NEUTRAL, so they can't rate an attack on it. This can make hostile NPCs reposition or flee with nothing to attack.

```cpp
const auto same_as = []( const Creature * lhs, const Creature * rhs ) { return &lhs == &rhs; }; // npc.cpp:2902
} else if( attitude_to( guy ) != Attitude::NEUTRAL && sees( here, guy.pos_bub( here ) ) ) {     // npcmove.cpp:1006
if( att != Attitude::HOSTILE && ( critter.friendly || !is_enemy() ) ) { neutral...; continue; } // :1027
```

**Fix:** Delete the three loops and `neutral_guys`. This keeps current behaviour and removes the cost. Then fix `assess_danger` to classify through `attitude_to` (HOSTILE only) for both NPCs and monsters. If a cache is still wanted, use an `unordered_map<const Creature*, Attitude>` rebuilt in `regen_ai_cache`.

### NPC-B17 · `move_to` teleports the NPC when `path[0]` is not adjacent (missing return)
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:3718`, `src/npcmove.cpp:3826`, `src/npcmove.cpp:3885`, `src/npcmove.cpp:4015`, `src/npcmove.cpp:2272`, `src/npcmove.cpp:5976`, `src/npcmove.cpp:6109`

When the destination is more than 1 tile away on the same z, `move_to` clears the path and pauses, but doesn't return. If the tile is passable, it then charges `run_cost` and calls `setpos( p )`, a multi-tile hop that skips the tiles in between. A stale non-adjacent `path[0]` happens when another NPC shoves this one (`move_away_from` doesn't clear the shoved NPC's path), and `npc_flee`, `reach_omt_destination` and `go_to_omt_destination` call `move_to_next` without `update_path`.

```cpp
if( rl_dist( pos, p ) > 1 && p.z() == pos.z() ) { path.clear(); move_pause(); }  // :3718-3722, no return
setpos( here, p );                                                               // :3885
```

**Fix:** Add `return;` after `move_pause()`. Clear `path` on a shoved NPC in `move_away_from`, or have `move_to_next` re-route when `path[0]` isn't adjacent.

### NPC-B18 · Attack-rating math errors: speed multiplies instead of divides, and the spell friendly-fire toggle is dead
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:187`, `src/npc_attack.cpp:856`, `src/npc_attack.cpp:221`, `src/npc.cpp:2922`

The spell time penalty and throw dps multiply the move cost by `speed_rating` (about speed/100), so faster NPCs are penalised more and undervalue spells and throws. The spell penalty is also truncated into an int. In spell `evaluate_tripoint`, `friendly_fire` implies `att == FRIENDLY`, so the `FRIENDLY` branch always wins and the "avoid friendly fire" rule has no effect. NPCs whose own faction has no self "watch your back" relation resolve to NEUTRAL toward themselves and take only a -1x penalty for AoE self-hits. Actually killing themselves is still blocked at :241.

```cpp
time_penalty += std::round( attack_spell.casting_time( source ) * source.speed_rating() ) / 100.0f; // :187
const float throw_mult = throw_cost( source, single_item ) * source_speed_rating / 100.0f;          // :856
if( att == FRIENDLY ) { attitude_mult = -10; } else if( att == NEUTRAL || friendly_fire ) { ... }   // :221-230
```

**Fix:** Divide by `speed_rating` in both places. Test `friendly_fire` before `FRIENDLY`. Treat `critter == &source` as FRIENDLY (-10).

### NPC-B19 · npc_class bionic `chance` is off by one: chance 0 still installs about 1% of the time
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc.cpp:766`, `src/npc.cpp:768`, `src/npc_class.cpp:339`

`rng( 0, 100 ) <= chance` has 101 inclusive outcomes, so chance N gives (N+1)/101. With no delete/extend support for class bionics, `chance: 0` is the only way to cancel an inherited entry, and it doesn't fully work. No vanilla data uses 0, and chance 100 behaves correctly. The `bionics` field also has no schema in NPCs.md.

```cpp
int chance = bl.second; if( rng( 0, 100 ) <= chance ) { add_bionic( bl.first ); }  // npc.cpp:767-770
```

**Fix:** Use `x_in_y( chance, 100 )` and document `"bionics": [ { "id": [ ... ], "chance": N } ]`.

## 3. Performance

### NPC-P01 · NPC spell evaluation checks every tile in spell range and rebuilds the AoE and dps for each tile
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:131`, `src/npc_attack.cpp:140`, `src/npc_attack.cpp:191`, `src/npc_attack.cpp:201`, `src/npc_attack.cpp:208`, `src/npc_attack.cpp:218`, `src/magic.cpp:716`, `src/magic.cpp:949`, `src/magic.cpp:968`, `src/magic_spell_effect.cpp:450`, `src/npcmove.cpp:1752`, `src/npcmove.cpp:2643`, `src/npcmove.cpp:2648`

Call chain: `npc::move` → `method_of_attack` (every move with a target) → `evaluate_best_attack` → `npc_attack_spell::evaluate` for every known spell. `spell::targetable_locations` walks `points_in_radius( range )`, and per tile builds a `line_to`, checks impassability along it and calls `sees()`. For every targetable tile, `evaluate_tripoint` calls `calculate_spell_effect_area`, which builds a `std::set` and another `line_to` for blast spells. For each creature it calls `attitude_to`, `sees` and `spell::dps`, which ignores its target argument but builds `const_dialogue` talkers and evaluates `dbl_or_var` expressions every time. Field spells allocate a `const_dialogue` per empty AoE tile. A range-20 spell is about 1,681 tiles × ~20-step lines plus AoE work, per spell, per caster, per combat move. With magic mods, this is likely the most expensive per-NPC combat step.

```cpp
const std::vector<tripoint_bub_ms> targetable_points = attack_spell.targetable_locations( source ); // :140
for( ... query : here.points_in_radius( char_pos, range( source ) ) ) { ... if( !source.sees( here, query ) ) // magic.cpp:968-975
float spell::dps( const Character &caster, const Creature & ) const                                       // magic.cpp:716
```

**Fix:** Compute dps, field intensity/chance and the time penalty once per `evaluate()`. Build candidate epicenters from `ai_cache.hostile_guys` within `range() + aoe()` (the creature tiles and their neighbours), skip the spell entirely when there are none, and run obstruction/sees only on those. Hoist `magic->spells()`, which is built twice. This turns O(range²) tile work into O(hostiles × aoe_area).

### NPC-P02 · Gun, melee and throw evaluators scan every tile in range for each candidate item and fire mode
**Severity:** High · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:349`, `src/npc_attack.cpp:363`, `src/npc_attack.cpp:430`, `src/npc_attack.cpp:520`, `src/npc_attack.cpp:535`, `src/npc_attack.cpp:568`, `src/npc_attack.cpp:741`, `src/npc_attack.cpp:772`, `src/creature_tracker.cpp:310`, `src/npcmove.cpp:2602`

On every combat move, `evaluate_best_attack` visits the whole inventory, including nested contents. For every distinct wieldable item it builds melee and throw attacks, plus a gun attack per gun mode. Each `evaluate()` calls `creature_at` on every tile of `points_in_radius`: radius 8 for melee (289 tiles), `throw_range` for throws, `gun_range()` for each gun mode (up to 60, i.e. 14,641 tiles). `creature_at` costs a hash find plus a linear scan of active NPCs per empty tile. A 3-mode rifle alone is about 44k lookups per NPC per combat move. Weapon-invariant values (`gun_damage()`, `base_damage_melee()`, `attack_time`, `reach_range`) are recomputed per creature found.

```cpp
return get_map().points_in_radius( source.pos_bub(), weapon.gun_range() );           // :523
for( ... targetable_point : targetable_points( source ) ) { if( creatures.creature_at( targetable_point ) ) { // :535-537
double damage{ weapon.base_damage_melee().total_damage() }; damage *= 100.0 / weapon.attack_time( source );  // :430-432
```

**Fix:** Build once per `evaluate_best_attack` a small vector of seen hostile creatures with positions and distances from `ai_cache.hostile_guys`. Pass it to every evaluator and filter by `rl_dist <= range`. Hoist the weapon-invariant values out of the per-target loops. This cuts per-candidate work from O(range²) to O(#hostiles).

### NPC-P03 · Weapon valuation (`average_dps` builds 3 temporary monsters) is redone per hostile and per inventory weapon
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:958`, `src/npcmove.cpp:1117`, `src/npcmove.cpp:1176`, `src/npc.cpp:3022`, `src/melee.cpp:2799`, `src/item_gun_tool_ammo.cpp:539`, `src/item_gun_tool_ammo.cpp:559`, `src/npcmove.cpp:4991`, `src/npcmove.cpp:5033`, `src/npcmove.cpp:2598`, `src/npcmove.cpp:4714`, `src/npc.cpp:2529`

`melee_value` → `item::average_dps` → `item::dps` constructs 3 full `monster` objects (with dialogue, talkers and rng for special-attack cooldowns) and runs `effective_dps` simulations. Only the wielded weapon's value is cached. Hot uncached callers:
- `assess_danger`'s `ok_by_rules` under the non-default WEAK engagement rule calls `average_damage_dealt()` per hostile, bypassing the cache.
- `evaluate_best_weapon` re-evaluates every melee or gun item and the fist on each combat move.
- `wants_take_that` evaluates the same item twice, through `value()` and then `evaluate_weapon()`.

Aside: `melee_value` (melee.cpp:2810) tests the member `weapon` instead of `weap` for POLEARM, which mis-values candidate items.

```cpp
case combat_engagement::WEAK: return c.get_hp() <= average_damage_dealt();     // npcmove.cpp:958-959
monster test_mon = monster( comp_mon.second.mon_id );                          // item_gun_tool_ammo.cpp:546
const double weapon_value = evaluate_weapon( *node );                          // npcmove.cpp:5033
```

**Fix:** Compute `average_damage_dealt()` lazily once per `assess_danger`. Add a per-turn item→value memo (invalidated with `cached_info`) for non-wielded weapons. Keep 3 static, lazily built reference monsters in `item::dps`. Fix the `weapon`/`weap` typo.

### NPC-P04 · `return_to_camp` recomputes the full overmap travel path on every move
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:1933`, `src/npcmove.cpp:1882`, `src/npcmove.cpp:6085`, `src/npcmove.cpp:6121`, `src/overmapbuffer.cpp:1100`, `src/simple_pathfinding.cpp:415`, `src/character_oracle.cpp:550`

While the committed goal is `return_to_camp`, `npc::move` calls `overmap_buffer.get_travel_path` on every move until the NPC is back in camp. `go_to_omt_destination` only needs `omt_path.back()`, and it already re-plans when the NPC strays more than 3 OMTs. Inside the bubble the search is usually cheap. An unreachable camp, though, makes `find_overmap_path` explore up to 100,000 nodes on every move.

```cpp
} else if( new_goal == "return_to_camp" ) { if( assigned_camp ) { goal = *assigned_camp; ...
    omt_path = overmap_buffer.get_travel_path( surface, *assigned_camp, overmap_path_params::for_npc() ).points; } // :1933-1941
```

**Fix:** Recompute only when `goal != *assigned_camp || omt_path.empty() || omt_path.front() != *assigned_camp`.

### NPC-P05 · Throw evaluation rebuilds the crafting inventory and deep-copies candidates; the reload check repeats per gun mode
**Severity:** Medium · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npc_attack.cpp:701`, `src/npc_attack.cpp:731`, `src/npc_attack.cpp:735`, `src/npc_attack.cpp:743`, `src/npc_attack.cpp:759`, `src/npc_attack.cpp:834`, `src/npcmove.cpp:2617`, `src/npcmove.cpp:2632`, `src/npcmove.cpp:2742`, `src/crafting.cpp:695`

`npc_attack_throw::can_use` deep-copies the item, contents included, *before* the cheap flag, rule and worn/armor rejections. `can_wield` doesn't reject worn items, so a worn loaded backpack is cloned on every combat move. `base_penalty`, `targetable_points` and `evaluate_tripoint` copy it again. `evaluate()` builds `crafting_inventory( zero, -1 )` before checking whether the item is `best_weapon`. The crafting cache is keyed on `moves`, so this rebuild happens every combat move, only to count one item type. `can_reload_current()` (a full inventory plus adjacent-tile ammo scan) runs per gun mode although it always checks the wielded weapon. Bug side effect: `base_penalty` calls `is_wielding()` on the local copy, so the comparison is always false and the penalty always applies.

```cpp
item single_item( thrown_item ); if( single_item.count_by_charges() ) { single_item.charges = 1; } // :701-704
const temp_crafting_inventory &available_weapons = source.crafting_inventory( tripoint_bub_ms::zero, -1 ); // :759
if( it->shots_remaining( here, this ) > 0 || can_reload_current() ) {                              // npcmove.cpp:2632
```

**Fix:** Do all the rejections first. Copy only `count_by_charges` items, or precompute the copy once in the constructor. Use `has_amount`/`charges_of`, and only when `&thrown_item == best_weapon`. Compute `can_reload_current()` once, lazily. Test `is_wielding( thrown_item )` in `base_penalty`.

### NPC-P06 · Need-option judging repeats failing A* searches, and the one-entry trip cache thrashes
**Severity:** Medium · **Effort:** Medium · **Verified:** plausible
**Where:** `src/npc_trip.cpp:165`, `src/npc_trip.cpp:212`, `src/npc_trip.cpp:233`, `src/npc_trip.cpp:245`, `src/npc_trip.cpp:117`, `src/pathfinding.cpp:553`, `src/character_oracle.cpp:190`, `src/character_oracle.cpp:207`, `src/npc_arbitration.cpp:432`, `src/npcmove.cpp:8031`, `src/npcmove.cpp:8192`

For NPCs with an `obligation_reach()` (followers, guards on post), food and water options are judged through `trip_cost` → `resolve_trip` → `route_trip`, which tries `map::route` per interaction tile (up to 9). A refused option then repeats the search without the reach constraint (the TODO at npc_trip.cpp:245 admits this). `ai_cache.last_trip` caches a single trip, so it is overwritten candidate by candidate. The BT predicates, the executor (twice) and `complain()` each re-route the same candidates every move. The original estimate was overstated. Goals the reach policy disallows are dropped before any A* runs, and `map::route` is bounded to the endpoints' box plus 16 tiles. The "up to 18 failing searches" case needs a seen but unreachable source.

```cpp
npc_trip trip = route_trip( spec, policy ? &*policy : nullptr );
if( policy && !trip.feasible ) { // TODO: a refused option pays both searches every turn it's judged
    trip = route_trip( spec, nullptr );                      // npc_trip.cpp:242-247
```

**Fix:** Make `last_trip` a small per-turn map keyed by (spec, reach snapshot) and clear it when the turn or position changes. Use one multi-goal search (`pathfinding_target::radius`) instead of one route per tile. Skip the unconstrained retry when it can't change the verdict. Negative-cache unroutable (spec, from) pairs for a few turns.

### NPC-P07 · Need predicates and executors rerun the same radius-6 item and terrain scans 3-5 times per move
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/character_oracle.cpp:183`, `src/character_oracle.cpp:200`, `src/character_oracle.cpp:215`, `src/npcmove.cpp:6605`, `src/npcmove.cpp:6628`, `src/npcmove.cpp:6655`, `src/npcmove.cpp:6675`, `src/npcmove.cpp:6991`, `src/npcmove.cpp:7036`, `src/npcmove.cpp:7087`, `src/npcmove.cpp:7336`, `src/npcmove.cpp:8012`, `src/npcmove.cpp:8029`, `src/npcmove.cpp:8157`, `src/npcmove.cpp:8170`, `src/npcmove.cpp:8189`, `src/npcmove.cpp:3135`, `src/behavior_strategy.cpp:65`, `src/behavior.cpp:44`

The BT utility strategy evaluates every needs child, so a needy NPC runs `find_food_candidates`, `find_water_candidates` (each also calling `find_nearby_harvestable`) and the temperature checks. Each is a 169-tile `closest_points_first( pos, 6 )` scan with `sees()`, zone checks and per-item scoring. The executor then does full scans again, through `find_nearby_food` / `find_nearby_water_sources`, only to keep adjacent entries, and then calls the candidate finder once more. `rate_food` runs twice per accepted item. The BT strategies take `children` by value, and `behavior.cpp:44` copies a `std::function` and a `std::string` per predicate per tick. This only applies while the NPC needs something badly, so the impact is moderate.

```cpp
for( scored_item &c : find_nearby_food( cf ) ) { if( square_dist( pos_bub(), c.loc.pos_bub( here ) ) <= 1 && ... // :8012-8013
const auto candidates = find_food_candidates();   // :8029, same call the BT predicate made this move
float w = rate_food( ... ); ... results.push_back( { loc, rate_food( ... ) } );  // :6655 / :6675
```

**Fix:** Add a radius parameter to the `find_nearby_*` helpers and pass 1 at the adjacency-only call sites. Memoize the candidate finders per (turn, position, filter) in `ai_cache` so the predicate, executor and `complain()` share one scan. Store the score instead of calling `rate_food` twice. Pass BT children and predicates by const reference.

### NPC-P08 · Every NPC re-estimates the armour and dodge of itself and every NPC friend or foe on every move
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npcmove.cpp:839`, `src/npcmove.cpp:854`, `src/npcmove.cpp:971`, `src/npcmove.cpp:1004`, `src/npcmove.cpp:1132`, `src/npcmove.cpp:1195`, `src/npcmove.cpp:1206`, `src/npcmove.cpp:1225`, `src/npcmove.cpp:1293`, `src/npcmove.cpp:1608`, `src/character_armor.cpp:76`, `src/bionics.cpp:2764`

When hostiles are visible, `assess_danger` runs `evaluate_character` for hostile NPCs, NPC friends (same-faction camp members without a `sees()` check), the player and itself. Each call runs `estimate_armour`. That makes 4 `get_armor_type` calls per main body part, each walking every worn item and copying `get_bionics()`, and it constructs `bodypart_id("head")`/`bodypart_id("torso")` from strings on every iteration. The result is observer-independent and changes only when clothes change. The author's comment says "this is a value we could easily cache". `cur_threat_map` is a heap-allocated `std::map` each call. The early return at 1132 skips all of this when no hostiles are visible, so the cost is combat-only.

```cpp
armour_step += candidate.get_armor_type( damage_bash, part_id ); ...
if( part_id == bodypart_id( "head" ) || part_id == bodypart_id( "torso" ) )  // npcmove.cpp:845-858
// this is a value we could easily cache.                                    // :867
```

**Fix:** Cache `estimate_armour` per Character, keyed on the turn or an outfit/bionic generation counter. Use the static `body_part_head`/`body_part_torso` ids. Cache the observer-independent part of `evaluate_character` per target per turn. Replace the threat maps with `std::array<float, 8>`.

### NPC-P09 · The sleep spot search scans 121×121 tiles, scoring comfort and routing for each improving tile
**Severity:** Medium · **Effort:** Medium · **Verified:** confirmed
**Where:** `src/npcmove.cpp:7827`, `src/npcmove.cpp:7842`, `src/npcmove.cpp:7843`, `src/npcmove.cpp:7918`, `src/npcmove.cpp:3641`, `src/npc_trip.cpp:233`, `src/npc_trip.cpp:213`, `src/character.cpp:7204`

Without an obligation reach, `find_sleep_candidates` scans `closest_points_first( pos, MAX_VIEW_DISTANCE )` (about 14.6k tiles). Each passable empty tile gets `get_comfort_at` (single-entry cache, so effectively recomputed), `ambient_light_at` and `floor_warmth`. Every tile that improves on the best so far runs `is_valid_sleep_candidate` → `resolve_trip` → A*. The early exit needs comfort >= 9, while the best vanilla furniture is 6, so the scan rarely ends early. It runs once per plan acquisition, so the cost is a spike per sleep search rather than per turn.

```cpp
search_positions = reach ? closest_points_first( ..., reach->radius ) : closest_points_first( pos_bub(), MAX_VIEW_DISTANCE ); // :7827
if( ( !best || rank > *best ) && eligible_here( p ) ) {   // :7843, eligible_here routes
```

**Fix:** Cap the free radius (12-20 tiles). Skip bare floor unless nothing better exists. Collect the top-K ranked tiles first and route them in rank order (or with one multi-target Dijkstra) instead of routing every intermediate best.

### NPC-P10 · Unreachable items or corpses cause a failing A* on every move, with no negative cache
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:4411`, `src/npcmove.cpp:4472`, `src/npcmove.cpp:4475`, `src/npcmove.cpp:4856`, `src/npcmove.cpp:4882`, `src/npcmove.cpp:3295`, `src/npcmove.cpp:2091`, `src/pathfinding.cpp:560`

`cache_tile` only records searched tiles when no item is wanted. When the path to the wanted item fails, `find_item` clears `fetching_item`/`wanted_item` and forgets the failure. The next idle move re-scores the same items (including weapon valuation) and runs the same failing route. `find_corpse_to_pulp` can make up to 4 failing `update_path` calls per call, again with no memory. The search is bounded to the endpoints' box plus a 16-tile pad, not the whole bubble, so this is a repeated small cost.

```cpp
if( wanted_item.get_item() == nullptr && !declined_dynamically ) { ai_cache.searched_tiles.insert( 1000, abs_p, num_items ); // :4411-4414
if( path.empty() && dist_to_item > 1 ) { fetching_item = false; wanted_item = {}; }                                       // :4472-4478
```

**Fix:** Keep a small per-NPC unreachable set (tile → expiry turn, 30-60 turns), or insert the failed tile into `searched_tiles`. Have `find_corpse_to_pulp` skip tiles in that set.

### NPC-P11 · `complain()` builds speech before its rate limit and re-runs need scans while waiting to complain
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:6362`, `src/npcmove.cpp:6383`, `src/npcmove.cpp:6397`, `src/npcmove.cpp:6430`, `src/npcmove.cpp:6453`, `src/npcmove.cpp:6474`, `src/npcmove.cpp:6346`, `src/npcmove.cpp:1966`, `src/npcmove.cpp:2366`, `src/npcmove.cpp:2511`, `src/npc_arbitration.cpp:416`

`complain()` runs on nearly every follower move. For infected, bitten or bleeding NPCs it translates a snippet, runs `parse_tags` and `string_format` before `complain_about` checks `complaint_due`, which is cheap but wasteful. The bigger repeated cost: the out-of-reach food and water branches call `best_need_view` (a radius-6 scan plus candidate judging) whenever the complaint is due. That stays true until a plea actually fires, so the scan reruns every move while the NPC is badly hungry or thirsty, usually with no candidate at all, duplicating the BT's own scan.

```cpp
std::string talktag = chat_snippets().snip_wound_infected.translated(); parse_tags( talktag, ... ); // :6386-6390
if( complaint_due( food_out_of_reach_string, 30_minutes ) ) { ... best_need_view( need_goal_id::eat_food ); // :6430-6433
```

**Fix:** Check `complaint_due` before building any speech (pass a text-building lambda). Record a "checked" timestamp for the out-of-reach pleas and re-check at most once per minute, or reuse this turn's memoized candidates (see NPC-P07).

### NPC-P12 · `faction_manager::get` scans every faction linearly instead of calling `find()`
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/faction.cpp:543`, `src/faction.cpp:548`, `src/faction.cpp:455`, `src/item.cpp:1169`, `src/item.cpp:1181`, `src/item.cpp:1090`, `src/avatar.cpp:1198`, `src/game.cpp:10526`

`get()` walks the whole `std::map<faction_id, faction>` comparing keys. `faction::has_relationship` does the same over `relations`, with string compares. They are called from item ownership checks (`validate_ownership`, `is_owned_by`, about 5 lookups per query), `attitude_to` and `assess_danger`. The impact is smaller than first claimed: `faction_id ==` is an int compare, null owners skip the lookup, and solo factions are removed when their member dies or is recruited, so the map holds about 25 templates plus live random NPCs.

```cpp
for( auto &elem : factions ) { if( elem.first == id ) {            // faction.cpp:548-549
for( const auto &rel_data : relations ) { if( rel_data.first == guy_id.c_str() ) { // :457-458
```

**Fix:** `factions.find( id )` and `relations.find( guy_id.str() )`, with the same semantics.

### NPC-P13 · `get_pathfinding_settings` rebuilds the `smash_ability` map for every path step it validates
**Severity:** Low · **Effort:** Small · **Verified:** plausible
**Where:** `src/npc.cpp:3805`, `src/npc.cpp:3807`, `src/npc_trip.cpp:106`, `src/npc_trip.cpp:111`, `src/npc_trip.cpp:191`, `src/npc_trip.cpp:233`, `src/character.cpp:3067`

`trip_step_ok` calls `get_pathfinding_settings()` per step, and that recomputes `smash_ability()` (`base_damage_melee`, enchantments and a fresh `std::map`). `route_trip`'s retained-path check runs it for every remaining step inside `std::all_of`. This only applies to the spec currently being walked, for NPCs with an obligation reach. The per-step `path_avoids`/`can_pass` calls cost about the same, so the real gain hasn't been measured. The original claims about `can_move_to` and `move_to` are wrong: those paths are cold.

```cpp
path_settings->bash_strength = !no_bashing ? smash_ability() : std::map<damage_type_id, int>(); // npc.cpp:3807
return !path_avoids( p, ignore_occupant ) && here.can_pass( from, p, get_pathfinding_settings( no_bashing ) ) && // npc_trip.cpp:111
```

**Fix:** Fetch the settings once before the `all_of` loop and pass them into a `trip_step_ok` overload. Optionally memoize `smash_ability()` per turn and wielded item.

### NPC-P14 · Idle camp workers rescan a 61×61 area for chairs every downtime move, with `find_camp` inside the loop
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/npcmove.cpp:4119`, `src/npcmove.cpp:4148`, `src/npcmove.cpp:4151`, `src/npcmove.cpp:4154`, `src/npcmove.cpp:1931`, `src/npcmove.cpp:1945`, `src/npcmove.cpp:2077`, `src/npcmove.cpp:2204`

`worker_downtime` scans `points_in_radius( pos, 30 )` (3,721 tiles) for `CAN_SIT` furniture whenever `chair_pos` is unset or occupied. It calls the loop-invariant `overmap_buffer.find_camp` per chair found. A failed search isn't remembered, and the scan comes before the `wander_pos` branch. With more idle workers than chairs, every idle worker rescans on every move.

```cpp
for( const tripoint_bub_ms &elem : here.points_in_radius( pos_bub(), 30 ) ) {
    if( here.has_flag_furn( ter_furn_flag::TFLAG_CAN_SIT, elem ) && ... ) {
        std::optional<basecamp *> camp = overmap_buffer.find_camp( assigned_camp->xy() );  // :4151-4156
```

**Fix:** Hoist `find_camp`. Restrict the scan to the camp's expansion OMTs. Remember a "no chair" result and retry every 10-30 minutes.

### NPC-P15 · BT camp predicates each call `npc_within_camp`, a 6×6 `find_camp` sweep over every loaded overmap
**Severity:** Low · **Effort:** Small · **Verified:** confirmed
**Where:** `src/character_oracle.cpp:53`, `src/character_oracle.cpp:60`, `src/character_oracle.cpp:532`, `src/character_oracle.cpp:550`, `src/character_oracle.cpp:559`, `src/character_oracle.cpp:573`, `src/character_oracle.cpp:583`, `src/character_oracle.cpp:594`, `src/overmapbuffer.cpp:1575`

For camp residents, each `npc_decision` tick calls `npc_within_camp` about 4-5 times. `overmapbuffer::find_camp` probes a 6×6 window in *every* loaded overmap, and overmaps are never unloaded during play, so the cost grows with exploration. Each probe is a cheap `std::map` find, so a call costs a few microseconds. Quirk: the window is asymmetric (-3..+2), and the function returns the first camp found, not necessarily the camp at `p`.

```cpp
std::optional<basecamp *> bcp = overmap_buffer.find_camp( n.assigned_camp->xy() );  // character_oracle.cpp:60
for( auto &it : overmaps ) { ... for( int x2 = p2.x() - 3; x2 < p2.x() + 3; x2++ ) { ... // overmapbuffer.cpp:1577-1581
```

**Fix:** Memoize `npc_within_camp` per oracle instance, or on the NPC keyed by `pos_abs_omt()`. In `find_camp`, look up only the overmap that owns each probed point (`get_existing_om_global`), and make the window symmetric.

## Appendix: considered and rejected

- NPC behaviour toggles are trait ids checked with `has_trait`, so effects, bionics and enchantments can't grant them – Rejected: `has_trait` reads `cached_mutations`, which includes traits granted by enchantment `mutations` collected from effects, bionics and gear, and EOCs can add traits with `u_add_trait`/`npc_add_trait`. What remains is a stylistic inconsistency with the `CANNOT_*` flags.

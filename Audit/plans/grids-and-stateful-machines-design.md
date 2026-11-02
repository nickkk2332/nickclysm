# Design: custom grid resources, machine EOC hooks, tile variables and catch-up

**Status:** design only. The owner asked for this on 2026-11-02 and filed it in the backlog as a strong modding addition, to be built when the related findings come up (see "Related audit findings"). It is not in the [queue](modding-queue.md).

**Goal:** let a mod build machines and magic systems from JSON. The test case is a Botania-style mod:
- flowers generate a resource from their surroundings
- pools store it
- connectors move it
- other blocks spend it on area effects
- items dropped into a pool are transformed
- all of it keeps working sensibly while the player is away

There are four parts. Each one is useful on its own, and later parts build on earlier ones:

| | Part | What it adds |
|---|---|---|
| A | Grid resources | Electricity becomes one resource among several; mods define more, with storage parts and connectors that carry them. |
| B | Machine parts with EOC hooks | Vehicle and appliance parts run EOCs when installed, switched or every N turns, keep per-part variables, and can generate or use resources by formula. |
| C | Tile and furniture variables, and furniture EOCs | Per-tile variables, a real tile talker, and furniture that runs EOCs on its own. |
| D | Catch-up | One rule for all of the above: a hook that missed time while unloaded is told how much, once. |

## How it works today

**Appliances are vehicles.** Each placed appliance is a one-tile `vehicle` with the `APPLIANCE` part flag.
- **Grids.** A grid is a set of vehicles linked by `POWER_TRANSFER` parts (wires, cables). `vehicle::search_connected_vehicles` (src/vehicle.h:930-978) walks the links and records a line-loss factor per vehicle.

**Only electricity is shared on a grid.**
- **Battery charge is pooled.** `vehicle::fuel_left` special-cases `fuel_type_battery` (src/vehicle.cpp:3869-3886) to sum the batteries of every connected vehicle. `charge_battery` and `discharge_battery` (src/vehicle.h:1672-1688) spread charge across the grid the same way.
- **Everything else stays local.** Every other fuel is counted only in the vehicle's own tanks.
- **What a battery is.** A part whose pocket holds the `battery` item as ammo. A "mana tank" today is a tank of a mana liquid, local to one vehicle.

**Generation is fixed per kind of part.**
- **Fixed rates.** A part has an `epower` rate (src/veh_type.h:460), plus the special generators: solar, wind, water wheel and funnel.
- **Hardcoded engine fuels.** Engines are tied to a few hardcoded pseudo-fuels (finding VEH-J11).
- **Switching.** Parts can be switched with JSON `vehicle_toggle` flags (VINT-J02, done in round 3). Enchantments with an `ACTIVE` condition apply while a part is on.

**Catch-up exists only for renewables.**
- `vehicle::update_time` (src/vehicle.cpp:8622, called from `vehicle::idle`) back-fills funnels, solar panels, wind turbines and water wheels for the time a vehicle was unloaded.
- `vehicle::catchup_off_map_renewables` (called from src/map.cpp:930) does the same for grid members that are off the map.
- Nothing else catches up.

**EOC access to vehicles.**
- The vehicle talker supports variables (src/vehicle.h:868, `values`).
- Round 3 added math functions for fuel, battery, engine, speed, lock and part counts (VEH-J01, partial).
- `u_run_vehicle_eocs` runs an EOC with each loaded vehicle as `u`.

**Furniture has no instance.**
- **Storage.** The map stores a furniture type id per tile, and every tile of a type shares one definition.
- **Existing per-tile data.** The submap already keeps sparse per-tile data next to the furniture grid (src/submap.h:306-337), saved with the submap:
  - signs and graffiti (`cosmetics`)
  - terminals (`computers`)
  - `partial_constructions`
  - tile damage (`ephemeral_data`)
  - `original_terrain`
- **Furniture talker.** The "furniture talker" (src/talker_furniture.h) wraps a `computer`; it is not furniture (EOC-J13, MATH-J08).
- **EOC hooks.** Furniture can run EOCs only through an `effect_on_condition` examine action.

**The reality bubble.** Vehicles, fields, active items and map scans are processed only near the player. When a submap loads, `map::actualize` applies a few content-specific catch-up rules (MAP-J04).

## Goals and non-goals

**Goals:**
- Mods define resources besides electricity, with storage, transfer and loss.
- Machine parts and furniture react to time and events through EOCs, keep their own state, and generate or spend resources by formula.
- Time spent unloaded is accounted for once, by the content that cares, with no per-turn replay.
- Electricity behaves exactly as today after the refactor, and old saves load unchanged.

**Non-goals:**
- Town-scale grids, or processing anything outside the reality bubble beyond catch-up math. `doc/FREQUENTLY_MADE_SUGGESTIONS.md` explains why a municipal grid is not feasible, and that still holds.
- Rewriting the vehicle power code in one step. Part A starts as a refactor with no behaviour change.

## A. Grid resources

### A1. The `grid_resource` type

```jsonc
{
  "type": "grid_resource",
  "id": "mana",
  "name": "mana",
  "unit": "MP",                 // shown in grid and examine screens
  "storage_item": "mana_charge", // the ammo item storage parts hold, as `battery` is for electricity
  "loss_per_link": 0.0,          // optional: fraction lost per connector link (electricity keeps its current line loss)
  "color": "light_blue"
}
```

Electricity becomes the built-in resource `electricity`, with `storage_item: battery`. Every `fuel_type_battery` special case moves behind "is this item a grid resource's storage item". The first commit of this part must change no behaviour, and the existing vehicle and appliance power tests prove that.

### A2. Storage parts

A part stores a resource the way a battery stores electricity: its pocket takes the resource's storage item as ammo.

```jsonc
{ "type": "vehicle_part", "id": "ap_mana_pool", "name": "mana pool",
  "flags": [ "APPLIANCE", "GRID_STORAGE" ],
  "pocket_data": [ { "pocket_type": "MAGAZINE", "ammo_restriction": { "mana_charge": 10000 } } ] }
```

- **Saves.** Storage is ammo in a part, which saves already handle.
- **Repair and damage.** These work as for batteries. A broken pool may leak, using the existing `leak_fuel` path for tanks.

### A3. Transfer

Connectors say what they carry. The default is `[ "electricity" ]`, so existing wires and cables are unchanged.

```jsonc
{ "type": "vehicle_part", "id": "ap_mana_spreader", "flags": [ "APPLIANCE", "POWER_TRANSFER" ],
  "carries": [ "mana" ] }                       // a part that links grids for mana only
```

- **Cable items.** `link_up` cable items gain the same `carries` field.
- **Search.** `search_connected_vehicles` takes a resource and follows only links that carry it, so one appliance can sit on an electric grid and a separate mana grid.
- **Loss.** Line loss is per resource.

### A4. Generation and consumption by formula

A part can generate or use any resource at a rate given by a number or by math. The math is evaluated with the vehicle as `u`, plus the part's location and variables (B3).

```jsonc
"resource_rates": [
  { "resource": "mana", "per": "1 minutes",
    "amount": { "math": [ "is_day() * (u_part_val('nearby_flowers') + 1) * 2" ] },
    "when": "always" },                          // or "enabled": only while switched on (vehicle_toggle)
  { "resource": "electricity", "per": "1 seconds", "amount": -5, "when": "enabled" }
]
```

- **Processing.** Rates are applied when the vehicle is processed, in place of the hardcoded `epower` handling, which becomes one case of this. A negative amount that the grid can't supply switches the part off, as a drained electric part does today, and its `on_disable` hook runs (B2).
- **Catch-up.** Part D covers it.

### A5. EOC and math access

| Function | Reads | Writes |
|---|---|---|
| `vehicle_resource('mana')` | the grid total | spreads across the grid's storage, like `charge_battery` |
| `vehicle_resource_capacity('mana')` | the grid's capacity | |
| `vehicle_resource_rate('mana')` | the current net rate | |

These sit next to the round 3 vehicle functions. `vehicle_battery()` becomes an alias for `vehicle_resource('electricity')`.

### A6. UI

Appliance examine screens and the vehicle power readout list every resource on the grid: stored, capacity and net rate. Items stay electric only, unless a later step lets `link_up` items draw another resource; that is left open below.

## B. Machine parts with EOC hooks

### B1. Hooks on vehicle parts

```jsonc
"eocs": {
  "on_install": [ "EOC_FLOWER_PLANTED" ],
  "on_remove": [ ],
  "on_enable": [ ], "on_disable": [ ],            // switched by vehicle_toggle, or drained (A4)
  "on_break": [ ],                                 // part destroyed (closes part of VEH-J03 / VINT-J06)
  "on_tick": { "interval": "10 seconds", "eocs": [ "EOC_FLOWER_TICK" ], "when": "always" }
}
```

- **Talkers and context.** The vehicle is `u`; the EOC has no beta. Context values:
  - `part_loc`: a location variable
  - `part_id`
  - `elapsed`: the time since this hook last ran (part D)
  - `catch_up`: true when the run covers unloaded time

  With `part_loc`, the existing location effects work around the part: `u_map_run_eocs`, `u_map_run_item_eocs`, `u_set_field`, `u_transform_radius`, `u_cast_spell` with a target location, and item spawns.
- **Scheduling.** `on_tick` runs from vehicle processing on its interval. The next-run time is kept per part and saved, so a vehicle with no ticking parts costs nothing extra.

### B2. Events

The hooks also send events, so EOCs can listen from anywhere. This closes VINT-J06 for appliances:
- `vehicle_part_installed`
- `vehicle_part_removed`
- `vehicle_part_toggled`
- `vehicle_part_broken`
- `appliance_placed`

### B3. Per-part variables

- **Storage.** `vehicle_part` gains variables like those the vehicle already has (src/vehicle.h:868). They are saved with the part and dropped when it is removed.
- **Access.** EOCs read and write them through the context `part_id`, as `u_part_val('name')` in math and `{ "u_part_val": "name" }` as a variable object.
- **Merging.** Merging two grids doesn't touch part variables.

## C. Tile and furniture variables, and furniture EOCs

### C1. Storage

- **Where.** The submap gains a sparse map from tile to variables, next to `cosmetics` and `computers`. It is saved as an optional section, so old saves load with none.
- **Two scopes per tile:**
  - **furniture variables** belong to the furniture on the tile and follow its life (C2)
  - **tile variables** belong to the spot and stay whatever is built there, for "this ground is consecrated" or "this spot was dug"

### C2. Lifetime rules

| Event | Furniture variables | Tile variables |
|---|---|---|
| Furniture dragged to another tile | move with it | stay |
| Furniture destroyed, smashed or deconstructed | `on_destroy` runs, then they are dropped | stay |
| Furniture replaced: `set_furniture`, construction, examine transform, `ter_furn_transform` | dropped, unless the new type or the transform says `"keep_vars": true` | stay |
| Mapgen places furniture | can set starting values (`"vars": { ... }` on furniture placements) | can be set with a tile placement |
| Mapgen rotation or mirroring | move with the tile | move with the tile |
| Submap unloaded and reloaded | saved and restored | saved and restored |

All of these go through a few chokepoints in `map` (furniture set, furniture move, destroy) rather than each caller.

### C3. The tile talker

- **What it is.** A new talker for a location: the tile, with its terrain and furniture. It replaces the computer wrapper as the "furniture talker"; terminals keep their own talker.
- **What it supports:**
  - variables in both scopes: `u_val` for furniture variables, a `tile_` prefix for tile variables
  - `pos`, the math that reads map ids and flags at its location, and location effects
- **What it refuses.** Effects that make no sense for a tile, such as `u_add_effect`, `u_attack` or `u_learn_recipe`, are refused at load when the EOC is attached to furniture, and are ignored with a debug message at runtime. This avoids the silent no-ops of EOC-J08.
- **How EOCs reach it:**
  - furniture hooks (C4) run with the tile as `u`
  - `u_map_run_eocs` gains `"tile_talker": true`, so each matched tile is the `npc_` talker
  - a variable object `{ "tile_val": "name", "loc": <location variable> }` reads or writes a tile variable from any EOC

### C4. Furniture hooks

```jsonc
{ "type": "furniture", "id": "f_daybloom",
  "eocs": {
    "on_place": [ "EOC_DAYBLOOM_INIT" ],           // built, placed by mapgen or EOC, or dragged in
    "on_destroy": [ "EOC_DAYBLOOM_DROP" ],         // with the destroyer as npc_ when known (MAP-J01)
    "on_tick": { "interval": "1 minutes", "eocs": [ "EOC_DAYBLOOM_TICK" ] }
  } }
```

- **Examine.** The existing `effect_on_condition` examine action stays, and runs with the tile talker as `npc_`.
- **No step hook yet.** An `on_step` hook (LOOP-J17) would fit here, but it is left for that finding.

### C5. Which tiles tick

- **Bookkeeping.** Only furniture types with `on_tick` are tracked. Each submap keeps a set of its ticking tiles, rebuilt from the furniture grid when the submap loads, so it needs no save data. The map keeps a set of submaps with ticking furniture, as it does for active items (src/map.h:2406).
- **Next-run times.** These are kept as hidden furniture variables, so they survive saves.
- **Cost.** Plain furniture costs nothing, and ticking tiles cost one check per interval.

## D. Catch-up

**One rule for every timed hook (B1, C4, and the A4 formulas):**
- A hook never replays missed turns.
- When something comes back into the bubble after being unloaded, each due hook runs once, with `elapsed` set to the whole missed time and `catch_up` true. The content scales its effect by `elapsed`.
- A type can cap this with `"max_catch_up": "7 days"`.
- The A4 rates do the scaling themselves: generation is the formula's rate times `elapsed`, clamped to storage capacity. This is the same approach `catchup_off_map_renewables` already takes for solar panels.

**Time of day.** A formula that depends on daylight or weather can't be integrated from one number. The proposal is to add math helpers that integrate over the missed span, using the same sun model as solar catch-up:
- `daylight_fraction(from, to)`
- `hours_outdoors_light(from, to)`

Hooks receive the start and end times in context. Everything else is the content's own approximation, documented as such.

**Off-map grid members.** These keep today's catch-up, extended from solar and wind to every resource rate whose formula doesn't need the map. A formula that reads the map, such as counting flowers around a part, is skipped off-map and catches up when its own submap loads.

**Order on load.** The submap is actualized, then furniture and part catch-up hooks run in a fixed order: storage first, then generators, then consumers. Results don't then depend on load order.

## Performance and limits

- **Cost scales with use.** It grows with the number of ticking things and their intervals, not with map size. Plain furniture, and parts without hooks, cost nothing new.
- **Grid searches** are cached per resource for each processing pass, as the battery search is meant to be (see the vehicle performance findings).
- **Catch-up is one call** per hook per reload, however long the absence.
- **Still bubble-only.** A mod should be designed around catch-up, not around running while the player is away.

## Saves and compatibility

- Old saves load with no tile variables, no part variables and no hook times; hooks then start fresh.
- New data is optional sections: submap tile variables, part variables, and part next-run times.
- Grid resources other than electricity exist only when a mod defines them. Removing such a mod leaves orphaned storage items, handled by the usual obsoletion and migration path for items.

## Phases

| Phase | Content | Tests |
|---|---|---|
| 1 | Electricity as a `grid_resource`; `fuel_type_battery` special cases moved behind it. No behaviour change. | The existing vehicle, appliance and battery tests pass unchanged; grid totals are identical before and after. |
| 2 | Custom resources, storage parts, connectors with `carries`, math access (A5), UI (A6). | Two grids sharing appliances keep their resources apart; line loss per resource; save and load. |
| 3 | Part hooks, events and per-part variables (B). Rate formulas (A4). | Every hook fires once per cause; drained parts switch off and run `on_disable`; ticks follow intervals. |
| 4 | Tile and furniture variables, lifetime rules, tile talker (C1–C3). | Each row of the C2 table; mapgen rotation; save and load; refused effects. |
| 5 | Furniture hooks and ticking (C4–C5). | Tick sets rebuilt on load; no cost for furniture without hooks; destroy and drag hooks. |
| 6 | Catch-up (D), including the daylight helpers and off-map rates. | A generator unloaded for a known span produces the expected amount; caps; load order. |
| 7 | A small Botania-style example in TEST_DATA (below), and docs: VEHICLES_JSON.md, EFFECT_ON_CONDITION.md, a new GRIDS.md. | The example works end to end in a test. |

## Example: a small Botania-style set

Each piece and how it maps onto this design:

| Piece | Built from |
|---|---|
| Daybloom (generator) | Furniture with `on_tick` every minute: if `is_day()` and outdoors, adds `elapsed`-scaled mana to the nearest connected pool (a tile variable naming the pool's location), or to its own furniture variable buffer. |
| Hydroangea (generator) | The same, counting water tiles in radius 2 with `u_map_run_eocs` and drying one source tile occasionally. |
| Mana pool (storage) | An appliance with a `mana` storage part. |
| Mana spreader (transfer) | An appliance connector with `carries: [ "mana" ]`, or a furniture "spreader" that moves mana between two pools' grids by EOC. |
| Agricarnation-style flower (consumer) | `on_tick`: spends mana from its grid to advance planted crops in radius 3. This also needs an EOC way to advance plant growth, which plant data has no hook for yet; add it with VINT-J05's plant work. |
| Infusion (crafting) | Pool part `on_tick`: `u_map_run_item_eocs` on its own tile, spends mana and runs `transform_item` per recipe. Recipes are a small JSON table read through variables, or a dedicated "infusion" recipe category later. |

## Related audit findings

Build these with this work, or note them as done when it lands:

| Finding | What it is | Covered by |
|---|---|---|
| EOC-J13 | The furniture talker is a computer wrapper | C3 |
| MATH-J08 | Zone and furniture talkers are stubs | C3 for furniture; zones stay separate |
| MAP-J06 | Tiles have no variables or talker | C1, C3 |
| MAP-J01 | Destroying terrain or furniture can't trigger an EOC | C4 `on_destroy`. Round 3 added bash EOCs on terrain and furniture types; this adds the per-tile hook and the destroyer. |
| MAP-J04 | Off-screen terrain behaviours are hardcoded; no actualize hook | D gives furniture and parts a catch-up hook; terrain catch-up could reuse it |
| LOOP-J17 | Furniture has no on-step hook | Natural extension of C4 |
| VEH-J03, VINT-J06 | No hooks or events for part install, remove, break or toggle | B1, B2 |
| VEH-J11 | Engine behaviour keyed to pseudo-fuel ids | A makes resources data; engines can move to A4 rates later |
| VINT-J01, MATH-J09 | Vehicle talker gaps | A5, B3 |
| VINT-J05 | Planter and reaper hardcode ids | Needed for the growth flower example |

## Open questions

1. **Items on other grids.** Should tools plugged in with `link_up` be able to run on a non-electric resource, for example a wand charged from a mana grid? The proposal is yes, through `link_up`'s resource field, in phase 2 or later.
2. **Pools as furniture.** Should storage also exist as furniture with a tile variable, without being a vehicle at all? It is cheaper, but it can't join a grid. The proposal is to allow it through C1, with grids staying vehicle-based.
3. **Visible transfer.** Should spreaders show a projectile or beam between pools? The proposal is a cosmetic field or spell effect from the content, not engine work.
4. **Per-tick hook budget.** Should there be a limit on how many hooks may run per turn, with a warning in debug mode? Mods with thousands of ticking tiles are the risk.

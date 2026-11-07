<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
*Contents*

- [JSON Inheritance](#json-inheritance)
  - [Examples](#examples)
  - [Support](#support)
  - [Behavior](#behavior)
  - [Guidelines](#guidelines)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# JSON Inheritance

To reduce duplication in the JSON data, it is possible for _some_ JSON `type`s to inherit from existing objects.  Some restraint should be used, see [Guidelines](#guidelines) section.  It is important to note that inheritance is not always completely standard between `type`s.  See the [Behavior](#behavior) section for further details.

## Examples

Items are one of the most common JSON types, so the "subtypes" field splits data into optional chunks. See [the item docs](ITEM.md#subtypes) for an explanation of how it works.

In the following example, `556` ammo is derived from `223` ammo via `copy-from`:

```jsonc
  {
    "id": "223",
    "type": "ITEM",
    "subtypes": [ "AMMO" ],
    "name": { "str_sp": ".223 Remington" },
    "description": ".223 Remington ammunition with a 55gr FMJ-BT bullet, patterned after the M193 loading.  The .223 round has been very popular with civilian shooters for almost a century, finding use in a wide variety of weapons.  The combination of high velocity with a thinly-jacketed, cannelured projectile led to unexpected yaw and fragmentation in soft tissue, increasing this round's performance.",
    "ascii_picture": "223",
    "weight": "12 g",
    "volume": "194 ml",
    "longest_side": "57 mm",
    "price": "2 USD 80 cent",
    "price_postapoc": "9 USD",
    "flags": [ "IRREPLACEABLE_CONSUMABLE" ],
    "material": [ "brass", "lead", "powder" ],
    "//": "`symbol` through `range` omitted for brevity",
    "damage": {
      "damage_type": "bullet",
      "amount": 39,
      "armor_penetration": 2,
      "barrels": [
        "... omitted for brevity"
      ]
    },
    "dispersion": 30,
    "dispersion_modifier": [ { "barrel_length": "337 mm", "dispersion": 30 }, { "barrel_length": "533 mm", "dispersion": 0 } ],
    "recoil": 1500,
    "effects": [ "COOKOFF" ]
  },
  {
    "id": "556",
    "copy-from": "223",
    "type": "ITEM",
    "subtypes": [ "AMMO" ],
    "name": { "str_sp": "5.56 NATO M855" },
    "material": [ "steel", "brass", "lead", "powder" ],
    "description": "5.56x45mm ammunition with a 62gr FMJ bullet containing a steel penetrator.  Originally designed for the M249 FN Minimi, M855 was designed to penetrate steel helmets and light cover at range, sacrificing terminal performance and accuracy in the process.  The limited yaw and fragmentation is worsened with shorter-barreled carbines shooting this round, and optimization of barrel twist for stabilization of the tracer variant of M855 has resulted in lackluster accuracy overall.",
    "ascii_picture": "556",
    "price": "2 USD 90 cent",
    "price_postapoc": "9 USD",
    "flags": [ "IRREPLACEABLE_CONSUMABLE" ],
    "relative": { 
      "damage": { "damage_type": "bullet", "amount": -3, "armor_penetration": 10 }, 
      "dispersion": 20 
    },
    "dispersion_modifier": [ { "barrel_length": "337 mm", "dispersion": 120 }, { "barrel_length": "533 mm", "dispersion": 0 } ],
    "proportional": { "recoil": 1.1 },
    "extend": { "effects": [ "INCENDIARY" ] }
  },
```

For `"type": "ITEM"`, the following rules apply:

* Missing fields, such as `weight`, `volume`, `material` and so on have the same value as the parent.
* Fields explicitly specified replace those of the parent type.  The above example replaces `name`, `description` and `price`.
* Numeric values may be specified `relative` to the parent.  For example `556` has less `damage` but more `armor_penetration` than `223` and will maintain this relationship if the definition for `223` is changed.
  * Note the syntax for fields that support objects: `damage` is still defined as an object for `relative`, with missing fields having the same value as the parent.
* Flags can be added via `extend`.  As an example, you can make it `INCENDIARY`.  Any existing flags specified from `223` are preserved.
* The entry you copied from must be of the same `type` as the item you added or changed.  Not all `type`s are supported, and not all are supported in the same way.  See [Support](#support) and [Behavior](#behavior) below).


Another example.  Reloaded ammo is derived from the factory equivalent but with a 10% penalty to `damage` and `dispersion` and. Additional rules apply:

```jsonc
  {
    "id": "reloaded_556",
    "copy-from": "556",
    "type": "ITEM",
    "subtypes": [ "AMMO" ],
    "name": { "str_sp": "5.56 NATO, reloaded" },
    "proportional": { 
      "price": 0.7, 
      "damage": { "damage_type": "bullet", "amount": 0.9 }, 
      "dispersion": 1.1 
    },
    "extend": { "effects": [ "RECYCLED" ] },
    "delete": { "flags": [ "IRREPLACEABLE_CONSUMABLE" ] }
  },
```

* Chained inheritance is possible.  For example, `reloaded_556` inherits from `556`, which is itself derived from `223`.
* Numeric values may be specified as `proportional` to the parent by via a decimal factor.  Here `reloaded_556` deals 90% of the damage of the factory equivalent with 110% dispersion.
* Flags can be deleted via `delete`.  It is not an error if the deleted flag does not exist in the parent.


Not all `type`s work the same.  For `"type": "MONSTER"`, `relative` uses different fields than `"type": "ITEM"`:

```jsonc
    "//": "MONSTER uses the same melee_damage field that ITEM does, but also has "melee_dice" and "melee_dice_sides",
    "relative": { 
      "melee_dice": 1, 
      "melee_dice_sides": 5, 
      "melee_damage": [ { "damage_type": "cut", "amount": 2 } ] 
	  },
```

Same as above, now with `proportional`:

```jsonc
    "proportional": { "hp": 1.5, "speed": 1.5, "attack_cost": 1.5, "melee_damage": [ { "damage_type": "cut", "amount": 0.8 } ] },
```


It is possible to define an `abstract` ID that exists only for other `type`s to inherit from and cannot itself be used in game.  This is done to facilitate maintenance and reduce line count.  In the following condensed example, `magazine_belt` provides values common to all implemented ammo belts:

```jsonc
{
    "abstract": "magazine_belt",
    "type": "ITEM",
    "subtypes": [ "ARMOR", "MAGAZINE" ],
    "name": { "str": "ammo belt" },
    "description": "An ammo belt consisting of metal linkages which disintegrate upon firing.",
    "volume": "0 ml",
    "price": "0 cent",
    "material": [ "steel" ],
    "symbol": "#",
    "color": "light_gray",
    "flags": [ "MAG_BELT", "MAG_DESTROY", "BELTED", "OVERSIZE", "WATER_FRIENDLY", "ZERO_WEIGHT" ],
    "armor": [
      {
        "material": [ { "type": "steel", "covered_by_mat": 100, "thickness": 0.1 } ],
        "encumbrance": 10,
        "coverage": 10,
        "covers": [ "torso" ],
        "specifically_covers": [ "torso_upper" ]
      }
    ]
  }
```

The following additional rules apply:

* Missing mandatory fields, such as `volume` do not result in errors as the `abstract` type is discarded after JSON loading completes.
* Missing optional fields are set to the usual defaults for that type.


## Support

The table lists every JSON `type` and how it takes inheritance and a second definition of an id (from a later file or mod):

* **copy-from**: `copy-from` and `abstract` work as described above, and a later definition of an id replaces the earlier one.  A later mod changes an object without restating it by copying it under the same id: `{ "type": "...", "id": "x", "copy-from": "x", ... }` keeps every member it doesn't name.  Which members take `extend`, `delete`, `relative` and `proportional` depends on the member (see the type's documentation); lists of ids and flags usually take `extend` and `delete`.
* **partial**: some `copy-from` support, with rules of the type's own (see [Behavior](#behavior)).
* **merges**: no `copy-from`; a later definition of an id adds to the earlier one (see [Behavior](#behavior)).
* **none**: no `copy-from`.  Either a later definition of an id replaces the earlier one (or is an error), or the type's objects have no ids (migrations, blacklists, options).

The table is checked against the game's own list, `DynamicDataLoader::inheritance_support()` in [init.cpp](../../src/init.cpp), which sits next to the calls that register each type; a test fails when a type is missing from either or they disagree.

| `type` | Support |
|---|---|
| `ability` | copy-from |
| `achievement` | copy-from |
| `activity_type` | copy-from |
| `addiction_type` | copy-from |
| `ammo_effect` | copy-from |
| `ammunition_type` | merges |
| `anatomy` | copy-from |
| `ascii_art` | copy-from |
| `attack_vector` | copy-from |
| `bash_damage_profile` | copy-from |
| `behavior` | copy-from |
| `bionic` | copy-from |
| `bionic_migration` | none |
| `body_graph` | copy-from |
| `body_part` | copy-from |
| `butchery_requirement` | copy-from |
| `camp_migration` | none |
| `character_mod` | copy-from |
| `charge_removal_blacklist` | none |
| `city` | copy-from |
| `city_building` | partial |
| `climbing_aid` | copy-from |
| `clothing_mod` | copy-from |
| `colordef` | none |
| `conduct` | copy-from |
| `connect_group` | none |
| `construction` | copy-from |
| `construction_category` | copy-from |
| `construction_group` | copy-from |
| `damage_info_order` | copy-from |
| `damage_rule` | copy-from |
| `damage_type` | copy-from |
| `dimension` | copy-from |
| `dimension_region_layout` | copy-from |
| `disease_type` | copy-from |
| `dream` | copy-from |
| `effect_migration` | none |
| `effect_on_condition` | copy-from |
| `effect_type` | copy-from |
| `emit` | none |
| `enchantment` | copy-from |
| `end_screen` | copy-from |
| `event_statistic` | copy-from |
| `event_transformation` | copy-from |
| `execution` | copy-from |
| `execution_trigger` | copy-from |
| `EXTERNAL_OPTION` | none |
| `faction` | copy-from |
| `faction_mission` | copy-from |
| `fault` | copy-from |
| `fault_fix` | copy-from |
| `fault_group` | copy-from |
| `field_type` | copy-from |
| `field_type_migration` | none |
| `forest_biome_component` | copy-from |
| `forest_biome_mapgen` | copy-from |
| `furniture` | copy-from |
| `gate` | copy-from |
| `harvest` | copy-from |
| `harvest_drop_type` | copy-from |
| `help` | none |
| `hit_range` | none |
| `ITEM` | copy-from |
| `item_action` | none |
| `ITEM_BLACKLIST` | none |
| `ITEM_CATEGORY` | copy-from |
| `item_group` | partial |
| `jmath_function` | copy-from |
| `json_flag` | copy-from |
| `limb_score` | copy-from |
| `LOOT_ZONE` | copy-from |
| `magic_type` | copy-from |
| `map_extra` | copy-from |
| `map_extra_collection` | copy-from |
| `mapgen` | none |
| `martial_art` | copy-from |
| `material` | copy-from |
| `MIGRATION` | none |
| `mission_definition` | copy-from |
| `MOD_INFO` | none |
| `mod_migration` | none |
| `mod_setting` | copy-from |
| `mod_tileset` | none |
| `MONSTER` | copy-from |
| `monster_adjustment` | none |
| `monster_attack` | none |
| `MONSTER_BLACKLIST` | none |
| `MONSTER_FACTION` | copy-from |
| `monster_flag` | copy-from |
| `MONSTER_WHITELIST` | none |
| `monstergroup` | merges |
| `mood_face` | copy-from |
| `morale_type` | copy-from |
| `movement_mode` | copy-from |
| `mutation` | copy-from |
| `mutation_category` | copy-from |
| `mutation_type` | none |
| `nested_category` | copy-from |
| `npc` | copy-from |
| `npc_class` | copy-from |
| `npc_decision_category` | copy-from |
| `npc_need` | copy-from |
| `omt_placeholder` | copy-from |
| `option_slider` | copy-from |
| `oter_id_migration` | none |
| `oter_vision` | copy-from |
| `overlay_order` | none |
| `overmap_connection` | copy-from |
| `overmap_land_use_code` | copy-from |
| `overmap_location` | copy-from |
| `overmap_special` | partial |
| `overmap_special_migration` | copy-from |
| `overmap_terrain` | copy-from |
| `palette` | none |
| `playlist` | none |
| `power_application` | copy-from |
| `power_cooldown` | copy-from |
| `power_projection` | copy-from |
| `power_resource` | copy-from |
| `power_summon` | copy-from |
| `pp_generator` | copy-from |
| `practice` | copy-from |
| `profession` | copy-from |
| `profession_blacklist` | none |
| `profession_group` | copy-from |
| `profession_item_substitutions` | none |
| `proficiency` | copy-from |
| `proficiency_category` | copy-from |
| `proficiency_migration` | none |
| `recipe` | copy-from |
| `recipe_category` | copy-from |
| `recipe_group` | copy-from |
| `region_settings` | copy-from |
| `region_settings_city` | copy-from |
| `region_settings_forest` | copy-from |
| `region_settings_forest_mapgen` | copy-from |
| `region_settings_forest_trail` | copy-from |
| `region_settings_highway` | copy-from |
| `region_settings_lake` | copy-from |
| `region_settings_map_extras` | copy-from |
| `region_settings_ocean` | copy-from |
| `region_settings_ravine` | copy-from |
| `region_settings_river` | copy-from |
| `region_settings_terrain_furniture` | copy-from |
| `region_terrain_furniture` | copy-from |
| `relic_procgen_data` | copy-from |
| `requirement` | partial |
| `rotatable_symbol` | none |
| `scenario` | copy-from |
| `SCENARIO_BLACKLIST` | none |
| `scent_type` | copy-from |
| `score` | copy-from |
| `shopkeeper_blacklist` | copy-from |
| `shopkeeper_consumption_rates` | copy-from |
| `shopkeeper_whitelist` | copy-from |
| `skill` | copy-from |
| `skill_display_type` | none |
| `snippet` | merges |
| `sound_effect` | none |
| `sound_effect_preload` | none |
| `SPECIES` | copy-from |
| `speech` | copy-from |
| `speed_description` | copy-from |
| `SPELL` | copy-from |
| `spell_migration` | none |
| `start_location` | copy-from |
| `sub_body_part` | copy-from |
| `talk_topic` | merges |
| `technique` | copy-from |
| `temperature_removal_blacklist` | none |
| `ter_furn_migration` | none |
| `ter_furn_transform` | copy-from |
| `terrain` | copy-from |
| `test_data` | none |
| `tool_quality` | copy-from |
| `TRAIT_BLACKLIST` | none |
| `trait_group` | none |
| `TRAIT_MIGRATION` | none |
| `trap` | copy-from |
| `trap_migration` | none |
| `uncraft` | copy-from |
| `var_migration` | none |
| `vehicle` | copy-from |
| `vehicle_group` | partial |
| `vehicle_part` | copy-from |
| `vehicle_part_category` | none |
| `vehicle_part_location` | copy-from |
| `vehicle_part_migration` | none |
| `vehicle_placement` | merges |
| `vehicle_spawn` | merges |
| `vitamin` | copy-from |
| `weakpoint_set` | copy-from |
| `weapon_category` | copy-from |
| `weather_generator` | copy-from |
| `weather_type` | copy-from |
| `widget` | copy-from |
| `WORLD_OPTION` | none |
| `wound` | copy-from |
| `wound_fix` | copy-from |

## Behavior

A common misconception is that every `type` that supports `copy-from` will also support `extend`, `delete`, `proportional` and `relative`, in the same way as one's most commonly seen object: `ITEM`.  As explained above, this is not the case; some types handle these fields manually (which is undesired), others use generic_factory.  `extend` and `delete` only apply to JSON members that have containers as their underlying C++ types, and `proportional`/`relative` handling must be implemented with corresponding C++ functions.  

In summary, there is no "default" JSON inheritance, only shared degrees of support; check the relevant C++ if `copy-from` behavior is uncertain, and looking for an existing (and functional!) example is always a good idea.  Types whose `copy-from` support, or whose redefinitions, follow rules of their own:
* `monstergroup` extends by default.  This prevents mods copying monstergroup definitions from replacing vanilla monstergroup lists.  To replace the *entire* list, add `"override": true` to the monstergroup object.
* `vehicle_group` takes `copy-from`, but a second definition of a group without `copy-from` adds its `vehicles` to the group, like `monstergroup`; `"override": true` makes it replace the group, and `extend` and `delete` of `vehicles` work in a copy or a second definition (see [Vehicle Groups](JSON_INFO.md#vehicle-groups)).
* `talk_topic` merges: each definition of a topic adds its responses, and `"replace_responses": true` or `"delete": { "responses": [ ... ] }` remove the earlier ones (see [NPCs.md](NPCs.md#changing-the-responses-of-an-earlier-definition)).
* `dream` and `speech` objects need an `id` to be copied or changed: one without an `id` is added as it always was, and nothing can change it later.  Vanilla dreams have ids (`dream_<category>_<strength>`); vanilla speech lines don't.
* `overmap_special` and `city_building` only partially support `copy-from`: mutable specials extend `check_for_locations_area` if `check_for_locations` is not specified.
* `item_group` takes `copy-from` of a group with the same id only, with `extend` adding entries.
* `requirement` takes no `copy-from`, but a second definition's `extend` adds components, tools and qualities to the first.
* `ammunition_type`, `vehicle_placement` and `vehicle_spawn` merge: a second definition adds locations or spawn types, or changes the members it names.  `snippet` categories gain the texts of every definition, and `"override": true` clears the category first.
* `SPELL` has limited support, given their behavior is governed by its spell `effect`.  Flags are not always inherited.  Testing is required to guarantee an inherited spell will behave as intended, use at your own risk.
* The `vitamins` field from `material` extends by default.
* An `ITEM`'s `pocket_data` is patched pocket by pocket: `relative`, `proportional`, `delete` and `extend` select pockets by `id` or index (see [Changing inherited pockets](ITEM.md#changing-inherited-pockets)).
* A `faction` copy adds to the `relations` of its parent instead of replacing them, and its `extend` and `delete` of `relations` and `epilogues` work by faction and snippet id (see [FACTIONS.md](FACTIONS.md#redefining-and-copying-factions)). `npc` lists are described in [NPCs.md](NPCs.md#npc-instance-definition).
* A `recipe` copy inherits its parent's `components`, `tools` and `qualities`; `extend` adds whole groups and `delete` removes ids from every group (see [ITEM_CRAFT_AND_DISASSEMBLY.md](ITEM_CRAFT_AND_DISASSEMBLY.md#inheriting-and-changing-components-tools-and-qualities-with-copy-from)).


## Guidelines

Contributors are encouraged to not overuse `copy-from`, as it can decrease the human readability of the JSON.  Chained inheritance is especially likely to become unwieldy, essentially recreating the level of redundancy we'd like to eliminate.

In general, there are two situations where `copy-from` should be used in the core game:

* Two things are _nearly identical_ variants of each other.
  * If they're pretty much identical (e.g. everything except for their descriptions can be kept, there are only decorative differences, there are no mechanical differences, or in the case of [guns](/doc/GUN_NAMING_AND_INCLUSION.md#difference-threshold) minor value differences), they can be handled as [variants](JSON_INFO.md#snippets) (scroll down to the variant example).
* A group of entities always (not almost always, always) shares some set of properties, then one or two levels of abstracts can set up a very shallow and narrow hierarchy.

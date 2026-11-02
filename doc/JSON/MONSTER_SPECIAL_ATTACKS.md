<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
*Contents*

- [Monster special attacks](#monster-special-attacks)
  - [Special attack selection logic](#special-attack-selection-logic)
  - [TODO](#todo)
  - [Adding `special_attacks` to monsters](#adding-special_attacks-to-monsters)
    - [Old style array](#old-style-array)
    - [New style object](#new-style-object)
    - [Combination](#combination)
  - [Hardcoded special attacks](#hardcoded-special-attacks)
  - [JSON special attacks](#json-special-attacks)
    - [`bite`](#bite)
    - [Grab attacks](#grab-attacks)
    - [`gun`](#gun)
    - ["spell" Monster Spells](#spell-monster-spells)
    - ["leap"](#leap)
  - [Monster defensive attacks](#monster-defensive-attacks)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# Monster special attacks

Monster creatures in C:DDA not only can do simple melee attacks, they have a wide array of attack-type actions.  

Depending on the intended effect, these can be valid `use_actions` for tools and weapons, hardcoded special attacks, any normal physical attack or even spells.  Also, depending on the kind of attack, these can be cooldown-based, conditioned or occur on death.

## Special attack selection logic

On each action the monster evaluates each special attack in alphabetical order, attempting to trigger each attack that is not in cooldown in turn until one is found whose preconditions are met or the monster runs out of candidates. This means "Early" attacks with low, or especially zero cooldowns will trigger very frequently unless they have preconditions preventing this. The cooldown is not set unless the attack is triggered.  Early attacks with low cooldowns can effecctively "starve" later special attacks and even the monster's default melee attack.


## TODO

- Descriptions for [Hardcoded special attacks](#hardcoded-special-attacks) could stand to be more descriptive of exactly what the attack does.


## Adding `special_attacks` to monsters

Monster special attacks can be defined either as old style arrays, new style objects, or a combination of both.  However, it is recommended to avoid the use of the old style in favor of the new style.


### Old style array

Generally [hardcoded special attacks](#hardcoded-special-attacks) are declared this way, although it can also be used for [JSON-declared attacks](/data/json/monster_special_attacks) too.  It contains 2 elements, the `id` of the attack and the cooldown:

```jsonc
"special_attacks": [ [ "ACID", 10 ] ]
```

### New style object

It contains either: 
* An `id` of a [hardcoded special attack](#hardcoded-special-attacks), or a [JSON-declared attacks](/data/json/monster_special_attacks).
* A `type` member (string) plus a `cooldown` member (integer or this can be a Variable Object, see the [doc](EFFECT_ON_CONDITION.md) for more info.) pair, for [partially hardcoded special attacks](#partially-hardcoded-special-attacks).
* A spell (see [MAGIC.md](MAGIC.md)).

Depending on the kind of attack, it may contain additional required members.  Example:

```jsonc
"special_attacks": [
    { "type": "leap", "cooldown": 10, "max_range": 4 }
]
```
In the case of separately defined attacks the object has to contain at least an `id` member.  In this case the attack will use the default attack data defined in `monster_attacks.json`, if a field is additionally defined it will overwrite those defaults.  These attacks have the common type `monster_attack`.  Example:

```jsonc
"special_attacks": [
    { "id": "impale" }
]
```


### Combination

`special_attacks` may contain a mixture of the old and new style:

```jsonc
"special_attacks": [
    [ "ACID", 10 ],
    { "type": "leap", "cooldown": 8, "max_range": 4 },
    { "id": "impale", "cooldown": 5, "min_mul": 1, "max_mul": 3 }
]
```
This monster can attempt an acid attack every ten turns, a leap with a maximum range of 4 every eight and an impale attack with 1-3x damage multiplier every five turns.


## Hardcoded special attacks

These special attacks are mostly hardcoded in C++ and are generally not configurable with JSON (unless otherwise documented).  JSON-declared replacements are mentioned when available.

- ```ABSORB_ITEMS``` Consumes objects it moves over to gain HP.  A movement cost per ml consumed can be enforced with `absorb_move_cost_per_ml` (default 0.025). The movement cost can have a minimum and maximum value specified by `absorb_move_cost_min` (default 1) and `absorb_move_cost_max` (default -1 for no limit). The volume in milliliters the monster must consume to gain 1 HP can be specified with `absorb_ml_per_hp` (default 250 ml). A list of materials that the monster can absorb can be specified with `absorb_materials` (can be string or string array, not specified = absorb all materials) and forbidden with `no_absorb_materials` (can be string or string array, not specified = absorb all materials specified by absorb_materials).
- ```ABSORB_MEAT``` Absorbs adjacent meat items (maximum absorbable item volume depends on the monster's volume), regenerating health in the process.
- ```ACID``` Spits acid.
- ```ACID_BARF``` Barfs corroding, blinding acid.
- ```BIO_OP_BIOJUTSU``` Attacks with any of the below martial art attacks.
- ```BIO_OP_DISARM``` Disarming attack, does no damage.
- ```BIO_OP_IMPALE``` Stabbing attack, deals heavy damage and has a chance to cause bleeding.
- ```BIO_OP_TAKEDOWN``` Takedown attack, bashes either the target's head or torso and inflicts `downed`.
- ```BLOW_WHISTLE``` Blow a whistle creating a sound of volume 40 from the position of the monster.
- ```BRANDISH``` Brandishes a knife at the player.
- ```BROWSE``` The monster will eat harvestable foods from BROWSABLE trees and plants when they're in season.
- ```BREATHE``` Spawns a `breather`.  Note: `breather hub` only!
- ```CALLBLOBS``` Calls 2/3 of nearby blobs to defend this monster, and sends 1/3 of nearby blobs after the player.
- ```COPBOT``` Cop-bot warns then tazes the player.
- ```DANCE``` Monster dances.
- ```DARKMAN``` Can cause darkness and wraiths to spawn.
- ```DISAPPEAR``` Hallucination (or other unusual monster) disappears.
- ```DOGTHING``` The dog _thing_ spawns into a tentacle dog.
- ```EAT_CARRION``` The monster will nibble on organic corpses, including zombies and plants, damaging them and filling its stomach if it has the EATS flag.
- ```EAT_CROP``` The monster eats an adjacent planted crop or CATTLE flagged comestible.
- ```EAT_FOOD``` The monster eats an adjacent non-seed food item (apart from their own eggs and food with fun < -20). If paired with the EATS flag, this will fill its stomach.
- ```EVOLVE_KILL_STRIKE``` Damages the target's torso (damage scales with monster's melee dice), if it succeeds in killing a fleshy target the monster will upgrade to its next evolution.
- ```FEAR_PARALYZE``` Paralyzes the player with fear.
- ```FLESH_GOLEM``` Attacks the player with 5-10 bash, has a chance to inflict `downed` if the attack connects.  Also roars menacingly for some reason.
- ```FLESH_TENDRIL``` Spawns gangrenous impalers or crawlers, pulls targets close when 4 > range > 1, either flings or grabs them when adjacent.
- ```FORMBLOB``` Attacks a neighboring tile, effect depends on the tile's inhabitant: spawns small slimes depending on its speed if empty, slimes players/NPCs, speeds up friendly slimes, heals brain slimes, converts nonfriendly flesh/veggy non-huge monsters to slimes of appropriate size.  Decreases in size if it did any of those and its current speed is below a threshold.
- ```FUNGUS``` Releases fungal spores and attempts to infect the player.
- ```FUNGUS_BIG_BLOSSOM``` Spreads fire suppressing fungal haze.
- ```FUNGUS_BRISTLE``` Performs a barbed tendril attack that can cause fungal infections.
- ```FUNGUS_CORPORATE``` Used solely by Crazy Cataclysm. This will cause runtime errors if used without, and spawns SpOreos on top of the creature.
- ```FUNGUS_FORTIFY``` Grows fungal hedgerows, and advances player on the Mycus threshold path.
- ```FUNGUS_GROWTH``` Grows a young fungaloid into an adult.
- ```FUNGUS_HAZE``` Spawns fungal fields.
- ```FUNGUS_INJECT``` Performs a needle attack that can cause fungal infections.
- ```FUNGUS_SPROUT``` Grows a fungal wall.
- ```FUNGAL_TRAIL``` Spreads fungal terrain.
- ```GRAZE``` The monster eats grass, shrubs and flowers.
- ```GENE_STING``` Shoots a dart at the player that causes a mutation if it connects.
- ```GENERATOR``` Regenerates health, hums.
- ```GRENADIER``` Deploys tear gas/pacification/flashbang/c4 hacks from its ammo.
- ```GRENADIER_ELITE``` Deploys grenade/flashbang/c4/mininuke hacks from its ammo.
- ```GROWPLANTS``` Spawns underbrush, or promotes it to `> young tree > tree`.  Can destroy bashable terrain or do damage if it hits something.
- ```GROW_VINE``` Grows creeper vines.
- ```HOWL``` "an ear-piercing howl!".
- ```JACKSON``` Converts zombies into zombie dancers (until its death).
- ```KAMIKAZE``` Detonates its defined ammo after a countdown (calculated automatically to hopefully almost catch up to a running player).
- ```LEECH_SPAWNER``` Spawns root runners or root drones, low chance of upgrading itself into a leech stalk.
- ```LUNGE``` Performs a jumping attack from some distance away, which can inflict `downed` to the target.
- ```MON_LEECH_EVOLUTION``` Evolves a leech plant into a leech blossom if no other blossoms are in sight.
- ```NONE``` No special attack.
- ```PAID_BOT```  For creature with the `PAY_BOT` flag, removes the ally status when the pet effect runs out.
- ```PARA_STING``` Shoots a paralyzing dart at the player.
- ```PARROT``` Parrots the speech defined in `speech.json`, picks one of the lines randomly.  `speaker` points to a monster id.
- ```PARROT_AT_DANGER``` Performs the same function as `PARROT`, but only if the creature sees an angry monster from a hostile faction.
- ```PHOTOGRAPH``` Photographs the player.  Causes a robot attack?
- ```PLANT``` Fungal spores take seed and grow into a fungaloid.
- ```PULL_METAL_WEAPON``` Pulls any weapon that's made of iron or steel from the player's hand.
- ```RATKING``` Inflicts disease `rat`.
- ```RATTLE``` "a sibilant rattling sound!".
- ```RESURRECT``` Revives the dead (again).
- ```RIOTBOT``` Sprays teargas or relaxation gas, can handcuff players, and can use a blinding flash.
- ```SEARCHLIGHT``` Tracks targets with a searchlight.
- ```SHOCKING_REVEAL``` Shoots bolts of lightning, and reveals a SHOCKING FACT! Very fourth-wall breaking.  Used solely by Crazy Cataclysm.
- ```SHOCKSTORM``` Shoots bolts of lightning.
- ```SHRIEK``` "a terrible shriek!".
- ```SHRIEK_ALERT``` "a very terrible shriek!", louder than `SHRIEK`.
- ```SHRIEK_STUN``` "a stunning shriek!", causes a small bash, can cause a stun.
- ```SLIMESPRING``` Can provide a morale boost to the player, and cure bite and bleed effects.
- ```SMASH``` Smashes the target, sending it flying for a number of tiles equal to (`melee_dice` * `melee_dice_sides` * 3) / 10.  JSON equivalent is `"smash"`.
- ```SPIT_SAP``` Spits sap (acid damage, 12 range).
- ```SPLIT``` Creates a copy of itself if it has twice the maximum HP that it should normally have.  This can be achieved by combining this with `ABSORB_ITEMS`.
- ```STARE``` Stares at the player and inflicts ramping debuffs (`taint>tindrift`).
- ```SUICIDE``` Dies after attacking.
- ```TAZER``` Shocks the player.
- ```TINDALOS_TELEPORT``` Spawns afterimages, teleports to corners nearer to its target.
- ```TRIFFID_GROWTH``` Young triffid grows into an adult.
- ```TRIFFID_HEARTBEAT``` Grows and crumbles root walls around the player, and spawns more monsters.
- ```UPGRADE``` Upgrades one of the non-hostile surrounding monsters, gets angry if it finds no targets to upgrade.
- ```VINE``` Attacks with vine.
- ```ZOMBIE_FUSE``` Absorbs an adjacent creature, healing and becoming less likely to fuse for 10 days.


## JSON special attacks

These special attacks are defined in [JSON](/data/json/monster_special_attacks), and belong to the `monster_attack` type, `melee` attack_type.  These don't have to be declared in the monster's attack data, the `id` of the desired attack can be used instead.  All fields beyond `id` and `cooldown` are optional.

| field                       | description
| ---                         | ---
| `cooldown`			      | Integer or Variable Object, see the [doc](EFFECT_ON_CONDITION.md) for more info, amount of turns between uses.
| `damage_max_instance`       | Array of objects.  See also [MONSTERS.md#melee_damage](MONSTERS.md#melee_damage).
| `min_mul`, `max_mul`        | Sets the bounds on the range of damage done.  For each attack, the above defined amount of damage will be multiplied by a
|						      | randomly rolled multiplier between the values `min_mul` and `max_mul`.  Default 0.5 and 1.0, meaning each attack will do at least half of the defined damage.
| `move_cost`                 | Integer, moves needed to complete special attack.  Default 100.
| `accuracy`                  | Integer, if defined the attack will use a different accuracy from monster's regular melee attack.
| `body_part_types`           | If used, the attack will pick a random body part from the list of all possible bodyparts of types used. For example, using `"body_part_types": [ [ "torso", 4 ], [ "arm", 3 ] ],` would pick torso with probability of 4, or any arm with probability of 3. Possible values are: head, torso, sensor, mouth, arm, hand, leg, foot, wing, tail, other.
| `condition`                 | Object, dialog conditions enabling the attack - see `NPCs.md` for the potential conditions - note that `u` refers to the monster, `npc` to the attack target, and for `x_has_flag` conditions targeting monsters only take effect flags into consideration, not monster flags.
| `attack_upper`		      | Boolean, default true. If false the attack can't target any bodyparts with the `UPPER_LIMB` flag with the regular attack rolls (provided the bodypart is not explicitly targeted).
| `range`       		      | Integer, range of the attack in tiles (Default 1, this equals melee range). Melee attacks require unobstructed straight paths.
| `grab`                      | Boolean, default false. Denotes this attack as a grabbing one. See `grabs` for further information
| `grab_data`                 | Array, grab data of the attack. Read only if `grab: true`, see `grabs` for the possible variables.
| `hitsize_min`               | Integer, lower bound of limb size this attack can target (if no bodypart targets are explicitly defined)
| `hitsize_max`               | Integer, upper bound of limb size this attack can target.
| `no_adjacent`			      | Boolean, default false. The attack can't target adjacent creatures.
| `dodgeable`                 | Boolean, default true. The attack can be dodged normally.
| `uncanny_dodgeable`         | Boolean, defaults to the value of `dodgeable`. The attack can be dodged by the Uncanny Dodge bionic or by characters having the `UNCANNY_DODGE` character flag.  Uncanny dodging takes precedence over normal dodging.
| `blockable`                 | Boolean, default true.  The attack can be blocked (after the dodge checks).
| `attack_amount`             | Pair of integers, default 1.  Specifies if this attack will hit you once, or multiple times, in multiple limbs (or multiple times in the same limb, if you are unlucky). The total damage of attack will be split evenly across all hits.
| `spread_damage`             | Boolean, default false.  If true, attack will damage all the limbs of character; The total damage of attack will be split evenly across all limbs
| `effects_require_dmg`       | Boolean, default true.  Effects will only be applied if the attack successfully damaged the target.
| `effects`				      | Array, defines additional effects for the attack to add.  See [MONSTERS.md](MONSTERS.md#attack_effs) for the exact syntax. Duration is in turns, not in movement points
| `self_effects_always`        | Array of `effects` the monster applies to itself when doing this attack.
| `self_effects_onhit`         | Array of `effects` the monster applies to itself when successfully hitting with the attack.
| `self_effects_ondmg`         | Array of `effects` the monster applies to itself when damaging its target.
| `throw_strength`		      | Integer, if larger than 0 the attack will attempt to throw the target, every 10 strength equals one tile of distance thrown.
| `eoc`                       | Array of ids, runs effect on condition, with `u` as attacker, and `npc` as victim, and `damage` as context variable with total damage dealt. See `NPCs.md`
| `miss_msg_u`			      | String, message for missed attack against the player.
| `miss_msg_npc`		      | String, message for missed attack against an NPC.
| `hit_dmg_u`                 | String, message for successful attack against the player.
| `hit_dmg_npc`			      | String, message for successful attack against an NPC.
| `no_dmg_msg_u`	          | String, message for a 0-damage attack against the player.
| `no_dmg_msg_npc`            | String, message for a 0-damage attack against an NPC.
| `throw_msg_u`		          | String, message for a flinging attack against the player.
| `throw_msg_npc`		      | String, message for a flinging attack against an NPC.


### `bite`

Under the hood an attack with `monster_attack` type, `bite` attack_type - if you want to define multiple separate bites for a monster you'll need to do a proper definition using an `id` as well. Makes monster use teeth to bite opponent, uses the same fields as "monster_attack" attacks. Monster bites can give infections, and for humanoid enemies (`human` bodytype) require the target being grabbed.
If `hitsize_min` is undefined it will default to 1 (disqualifying bites on the eyes and mouth).

| Field                       | Description                                                                          |
| ---                         | ---                                                                                  |
| `infection_chance`          | Chance to give infection in a percentage.  Exact chance is `infection_chance` / 100. |

### Grab attacks

Any melee/bite-type JSON attack can function as a grab, which leads to special behavior in addition to the base attack functions:  Grabs are not allowed to hit an already-grabbed bodypart, filtered by any effect on the BP having the `GRAB` flag, instead they will attempt to retarget to another limb.  A successful (not-dodged) grab will apply an instance of its `grab_effect` (which should have the `GRAB` effect flag) with the intensity defined by `grab_strength` and will gain the target bodypart's `grabbing_effect` to facilitate targeted grab removal (see later).
The attack's behavior is determined by the `grab_data` array, using the below variables:

| field                       | description
| ---                         | ---
| `grab_strength`             | Optional integer.  The strength of the grab effect applied on a successful grab, defaults to the monster's own `grab_strength`.
| `grab_effect`               | Optional string.  ID of the effect to apply on a successful grab (defaults to `grabbed`).  The effect id `null` will apply a null effect without a bug message, allowing for e.g. pull attacks without grab effects and is exempt from the usual retargeting/grab_filter application rules.
| `exclusive_grab`            | Optional bool, default `false`.  If true the attack will attempt to break all `GRAB`-flagged effect one by one ( on a `puller grab_strength / 2 in effect intensity` roll), and fail if it can't remove all - used for ranged pulls and grabbing.
| `drag_distance`             | optional int, default 0.  If higher the monster will attempt to drag on a successful attack - moving away and moving their target to its previous position as many times as the defined distance with it. This will be disabled by seatbelts unless `respect_seatbelts` is false.  Drag movement costs the same amount of move points as it would as normal movement, but happens in the same turn as part of the attack.
| `drag_deviation`            | optional int, default 0. Deviation of the monster's pathing from their target's opposite tile - 0 always drags directly opposite, 1 chooses randomly from the monster's and opposite tile's common neighbors, 2 lets the monster drag towards any of its neighboring tiles not occupied by itself or its target. Dragging direction is randomized every drag step.
| `drag_grab_break_distance`  | optional int, default 0. Drag steps after which the dragged character gets a chance to break the grab using the normal grab break calculations.
| `drag_movecost_mod`         | optional float, default 1.0. Move cost modifier for every step of dragging - per default the monster spends moves for each drag step, but will still complete the drag, leading to them "catching up" on their move debt afterwards. Internally a move cost of 0.0 will *prevent* movement, so use a slightly larger value.
| `respect_seatbelts`         | Optional bool, default true.  When false drags/pulls can tear you out of your seatbelt, damaging the part in question.
| `pull_from_vehicle`         | Optional bool, default false.  When true, a successful grab on a character riding in a vehicle can pull them out of their seat: they are unboarded, knocked down for 3 seconds and lose a second of moves, and at speed they take impact damage (when `speed in mph * rng(0.5, 1.25)` exceeds 15).  Only when the grabber is outside the vehicle (not on a vehicle tile) and not flying.  A working seatbelt on the rider's tile always prevents it, whatever `respect_seatbelts` says.  The vanilla `grab`, `grab_2` and `grab_3` attacks enable it.
| `vehicle_pull_formula`      | Optional string, default `grab_vehicle_pull_chance`.  ID of a `jmath_function` taking 4 arguments that returns the chance, from 0 to 1, that `pull_from_vehicle` succeeds.  `u` is the grabbing monster and `n` the grabbed character; `_0` is the grab strength, `_1` the vehicle's speed in mph, `_2` is 1 when the vehicle is muscle-powered (else 0) and `_3` the rider's athlete proficiency strength bonus.  The default, in `data/json/grappling_world.json`, is an opposed roll: `rand(_0)` must beat `rand(_1)` plus the rider's driving skill and the better of their strength and dexterity (plus swimming skill and `_3` when pedalling), so it returns 1 or 0.
| `pull_chance`               | Optional integer.  Percent chance for a connecting attack to initiate a pull, moving the target adjacent.  Pulls are prevented by seatbelts.
| `pull_weight_ratio`         | Optional float.  Ratio of weight the monster can successfully pull when succeeding the `pull_chance` roll or when dragging (requires a non-zero `drag_distance` ).  Default 0.75.
| `pull_msg_u/npc`            | Optional strings.  Message to print on a successful pull.
| `pull_fail_msg_u/npc`       | Optional strings.  Message to print on a failed pull attempt - either because of too high target weight or because of other grabs holding them back.

`GRAB`-flagged effects prevent movement for monsters and characters as well, and might have additional debuffs like every effect.  Grab break attempts happen effect by effect on non-attack movement or waiting in place, with the chance affected by limb scores, stats, and the existence of grab break MA techniques.  A successful grab removal removes the effect in question from the limb, as well as the bodypart's `grabbing_effect` from whichever monster has the latter.  If all grabs are broken movement is allowed on the same turn.  The grabbing monster is looked for within the range of its grab attacks and one z-level up or down, so a grab made from two tiles away is not lost as an orphan.  A monster held in any grab can't dodge.

### Characters grabbing creatures

A character can grab an adjacent creature with the grab action (`G`), ported from Cataclysm: The Last Generation.  The character needs free hands, a working arm, no `NO_GRAB` flag and enough stamina.  A grab costs moves and arm stamina, and the target can dodge it unless it tolerates the grab (followers, pets, hostile or helpless NPCs).  A character target is held by one body part that nobody holds yet.  A monster already held by someone else is pulled free on a `grab strength / 2 in the other hold's strength` roll, as an `exclusive_grab` does.  Pressing `G` again lets go.

The held creature gets a `grabbed` effect whose source is the grabbing character, and the character gets `grabbing`.  Each side finds the other by adjacency plus that source, so a hold is saved with the effects and needs no other data.  The hold ends when the two are no longer adjacent, when either is incorporeal or dead, when the grabber is winded, asleep or below the stamina floor, wields something, mounts a creature, is flung, has a seizure or swaps characters, and when either side is teleported or knocked back.  The held creature tries to break free like any other grab, but not within its minimum hold time, and followers never struggle against the player.  Every attempt costs the grabber stamina.  A held monster can't dodge, and gets angry.

Holding a creature and holding furniture or a vehicle exclude each other.

**Dragging.**  Walking while holding a creature drags it into the tile you left.  Each step rolls `grapple_drag_odds`; a failed roll costs the step but you stay put ("You strain to drag ...").  The creature moves the way it would move itself, so traps, fields, gravity and vehicle boarding apply to it, and its place is worked out in absolute coordinates, so a step that shifts the map doesn't lose it.  A step it can't follow is refused before anything is spent, and the hold is kept: a tied, harnessed, ridden, `IMMOBILE` or `CANNOT_MOVE` creature; another creature in the way (walking into the held pet itself just swaps places); a hold across a ramp, which would need the creature to change z-level; or a tile it can't enter (impassable for it, a small passage for a creature larger than medium, an impassable field, deep water for a monster that would drown).  Swimming, stairs, climbing and swapping places with a pet or NPC are refused while holding a creature, for the same reason.

**Throwing.**  The throw action (`t`) while holding a creature throws it instead of an item (an item picked from the inventory is still thrown as an item, which lets go of the creature).  The targeting screen starts on the held creature and counts range from it; range is `grapple_throw_force / 10` tiles, on the same z-level.  It shows the force of the throw for the tile under the cursor, with its word (shove, toss, throw, hurl, launch) and color, the moves and stamina it costs, and what the creature would hit on the way.  The force is scaled by the distance chosen over the full range, so a short throw is a gentle shove, and the creature flies from its tile toward the chosen tile and lands on it if nothing is in the way.  The throw is refused, before anything is spent, when it couldn't carry past the next tile, when the creature is more than `arm strength / 10` sizes larger than you, or when you lack the stamina for a full-force throw; a creature can't be thrown through your own tile.  If the creature holds you as well you must break free first, and a failed attempt costs the moves.  A non-hostile NPC asks for confirmation over force `GRAPPLE_THROW_ANGERS_NEUTRAL`, any NPC over `GRAPPLE_THROW_ANGERS_ANY`, and is then angry; a monster that isn't your friend takes it as an attack (anger with a `HURT` anger trigger, lost morale with a `HURT` fear trigger).  A throw that would take more than half your stamina asks first.  The hold ends, and the throw costs `100 +` your unarmed attack time in moves and `grapple_throw_stamina_cost` in stamina.  An active `bio_railgun` adds force against robots and cyborgs, at a tenth less stamina and the bionic's trigger power.

The throw goes through `game::fling_creature`, with two of TLG's options turned on: a landing below speed 30 is soft, and a character in the way is hit (by the flight's speed, the thrown creature by the character's size) rather than only stopping it.  Like every fling, it breaks the holds on the creature (unless one is stronger than half the force), and the creature lets go of whatever it held; it is `airborne` until it lands, so it doesn't fall, step on traps or get trap prompts on the way, then traps go off on the tile where it lands, and a creature that lands over open air drops to the level below.

These parts are data:

| What | Where
| ---  | ---
| `grapple_grab_strength` | `jmath_function` in `data/json/grappling.json`, `u` = grabber, `n` = target: the intensity of the target's `grabbed` effect, 1 to 100.
| `grapple_escape_stamina_cost` | `jmath_function`, `u` = grabber, `n` = target, `_0` = strength of the hold, `_1` = the target's escape skill: stamina the grabber spends on each escape attempt.
| `grapple_min_hold_turns` | `jmath_function`, `u` = grabber, `n` = target: turns before the target may try to break free.
| `grapple_drag_odds` | `jmath_function`, `u` = grabber, `n` = target, `_0` = strength of the hold, `_1` = the target's weight in kg, `_2` = the target's move cost for a step when it is an awake ally or an NPC walking with the grabber (else 0), `_3` = 1 when the target is a pet: the chance, 0 to 1, that a step drags it along.  TLG's formula: `odds = weight / ( strength * size )`, cut by `_2 / 200` (when under 1) for an ally and halved for a pet; the drag succeeds one time in `clamp( ( odds + 0.25 ) ^ 2, 1, 1000 )`.
| `grapple_throw_force` | `jmath_function`, `u` = thrower, `n` = thrown creature, `_0` = 1 when the creature stands in deep water, `_1` = the `bio_railgun` bonus (16 against `ROBOT` and `ROBOT_FLYING`, 8 against `CYBORG`; bionics and species are read in C++): the force of a full-range throw.  TLG's formula: `2 * ( arm strength * ( 1.1 + arm strength / 100 when over 10 ) + 2 * unarmed + throwing / 4 + 2 * size difference - 15 per size above medium )`, plus the railgun bonus, a quarter in deep water; 31 for strength 10 and skills 2, medium against medium.
| `grapple_throw_stamina_cost` | `jmath_function`, `u` = thrower, `n` = thrown creature, `_0` = its weight in kg, `_1` = the force of the throw, `_2` = the thrower's `melee_stamina_cost_mod`: the stamina a throw costs.  The weight-scaled cost TLG intended (its code came to a flat 50 from two sign errors): `max( 50, max( 75, ( weight in 20 g / ( 2 * thrower size / thrown size + ( throwing + unarmed ) / 20 ) + 50 ) * _2 ) * force / 100 )`.
| `grapple_throw_verb` | `jmath_function`, `u` = thrower, `n` = thrown creature, `_0` = force: how hard a throw is, 0 to 4 (TLG's thresholds 30, 40, 60 and 70).  The word in the messages and on the targeting screen comes from the `snippet` category `grapple_throw_verb_<n>` (shove, toss, throw, hurl, launch, and rarely yeet), so mods can change the thresholds, the words, or add synonyms; they are snippets so that they can be translated.
| `GRAPPLE_THROW_ANGERS_NEUTRAL`, `GRAPPLE_THROW_ANGERS_ANY` | `EXTERNAL_OPTION`s: the force above which a thrown NPC that is neither ally nor enemy (24), or any NPC (60), is angered, after the avatar confirms.
| `grab_condition` | `effect_on_condition` in `data/json/grappling.json` whose `condition` is tested (not run) before the grab roll, with `u` = grabber and `npc` = target.  If it is false, the grab is refused before any cost is paid.  By default it refuses incorporeal grabbers and targets.
| `GRAPPLE_STAMINA_FLOOR`, `GRAPPLE_ARM_STAMINA_COST` | `EXTERNAL_OPTION`s in `data/core/external_options.json`: the stamina needed to start or keep a hold (400), and the arm stamina a grab attempt costs (120).

### `gun`

The monster fires a gun at a target.  If the monster is friendly, it will avoid harming the player.

| Field                       | Description                                                                                                           |
| ---                         | --------------------------------------------------------------------------------------------------------------------- |
| `gun_type`                  | (Required) Valid item id of a gun that will be used to perform the attack.                                            |
| `ammo_type`                 | (Required) Valid **item** id (**not** `ammo_type`) of the ammo the gun will be loaded with.  Note: the monster itself should also have a `starting_ammo` field with this ammo.  For example: the monster is defined with `"starting_ammo": {"50bmg": 100}` so it can shoot `"ammo_type": "50bmg"` when using the gun attack. |
| `max_ammo`                  | Cap on ammo. If ammo goes above this value for any reason, a debug message will be printed.                           |
| `fake_str`                  | Strength stat of the fake NPC that will execute the attack.  8 if not specified.                                      |
| `fake_dex`                  | Dexterity stat of the fake NPC that will execute the attack.  8 if not specified.                                     |
| `fake_int`                  | Intelligence stat of the fake NPC that will execute the attack.  8 if not specified.                                  |
| `fake_per`                  | Perception stat of the fake NPC that will execute the attack.  8 if not specified.                                    |
| `fake_skills`               | Array of 2 element arrays of skill id and skill level pairs.                                                          |
| `move_cost`                 | Move cost of executing the attack.                                                                                    |
| `condition`                 | Object, dialogue conditions enabling the attack.  See `NPCs.md` for the possible conditions, `u` refers to the monster.
| `require_targeting_player`  | If true, the monster will need to "target" the player, wasting `targeting_cost` moves, putting the attack on cooldown and making warning sounds, unless it attacked something that needs to be targeted recently.  Gives "grace period" to player.                                                               |
| `require_targeting_npc`     | As above, but with NPCs.                                                                                              |
| `require_targeting_monster` | As above, but with monsters.                                                                                          |
| `target_moving_vehicles`    | If true, the monster will "target" moving vehicles even if it cannot see the player.
| `targeting_timeout`         | Targeting status will be applied for this many turns.  Note that targeting applies to turret, not targets.            |
| `targeting_timeout_extend`  | Successfully attacking will extend the targeting for this many turns.  Can be negative.                               |
| `targeting_cost`            | Move cost of targeting the player. Only applied if attacking the player and didn't target player within last 5 turns. |
| `laser_lock`                | If true and attacking a creature that isn't laser-locked but needs to be targeted, the monster will act as if it had no targeting status (and waste time targeting), the target will become laser-locked, and if the target is the player, it will cause a warning.  Laser-locking affects the target, but isn't tied to specific attacker. |
| `range`                     | Maximum range at which targets will be acquired.                                                                      |
| `range_no_burst`            | Maximum range at which targets will be attacked with a burst (if applicable).                                         |
| `description`               | Description of the attack being executed if seen by the player.                                                       |
| `targeting_sound`           | Description of the sound made when targeting.                                                                         |
| `targeting_volume`          | Volume of the sound made when targeting.                                                                              |
| `no_ammo_sound`             | Description of the sound made when out of ammo.                                                                       |

### "spell" Monster Spells


Casts a separately-defined spell at the monster's target.  Spells with `target_self: true` will only target the casting monster, and will still be casted only if the monster has a hostile target.

| Identifier                     | Description                                                                                             |
| ---                            | ------------------------------------------------------------------------------------------------------- |
| `spell_data`                   | List of spell properties for the attack.                                                                |
| `min_level`                    | The level at which the spell is cast. Spells cast by monsters do not gain levels like player spells.    |
| `cooldown `                    | Integer or a Variable Object, see the [doc](EFFECT_ON_CONDITION.md) for more info. How often the monster can cast this spell.
| `monster_message`              | Message to print when the spell is cast, replacing the `message` in the spell definition. Dynamic fields correspond to `<Monster Display Name> / <Spell Name> / <Target name>`. |
| `condition`                    | Object, dialogue conditions enabling the attack.  See `NPCs.md` for the possible conditions, `u` refers to the casting monster and `npc` to the target unless the spell allows no target (in which case only self-conditions can be defined).
| `allow_no_target`              | Bool, default `false`. If `true` the monster will cast it even without a hostile target.                |


### "leap"

Makes the monster leap a few tiles over passable terrain as long as it can see its destination. It supports the following additional properties:

| Field                   | Description                                                                                          |
| ---                     | ---------------------------------------------------------------------------------------------------- |
| `max_range`             | (Required) Float, maximal range of the jump.  Respects circular distance setting!                    |
| `min_range`             | (Required) Float, minimal range of the jump.  Respects circular distance setting!                    |
| `prefer_leap`           | Leap even when adjacent to target, will still choose the closest acceptable destination.             |
| `random_leap`           | Disregard target location entirely when leaping, leading to completely random jumps.                 |
| `ignore_dest_terrain`   | Leap even if the destination is terrain that it doesn't usually move on.                             |
| `ignore_dest_danger`    | Leap even if the destination is tiles that it would usually avoid, such as fire or traps.            |
| `allow_no_target`       | Default `false`.  Prevents the monster from using the ability without a hostile target at its destination.
| `move_cost`             | Moves needed to complete special attack. 100 move_cost with 100 speed is equal to 1 second/turn.     |
| `min_consider_range`    | Minimal distance to target to consider for using specific attack.                                    |
| `max_consider_range`    | Maximal distance to target to consider for using specific attack.        
| `condition`             | Object, dialogue conditions enabling the attack.  See `NPCs.md` for the possible conditions, `u` refers to the monster.
| `self_effects`          | Array of `effects` to apply after a successful leap.                                                 |
| `message`               | String, message to print when the player sees the monster jump (or land).                            |

## Monster defensive attacks

A special defense attack, triggered when the monster is attacked.  It should contain an array with the `id` of the defense (see a full list below) and the chance for that defense to be actually triggered.  Example:

```jsonc
"special_when_hit": [ "ZAPBACK", 100 ]
```

- ```ACIDSPLASH``` Splashes acid on the attacker.
- ```NONE``` No special attack to the attacker.
- ```ZAPBACK``` Shocks attacker on hit.

## EOC attack

Triggers the specified `effect_on_condition` with the monster as `alpha_talker` and its target as `beta_talker`, the attack requires vision of the target. 

| Field             | Description                                                                                                |
| ---               | ---------------------------------------------------------------------------------------------------------- |
| `range`           | (Required) Float, maximal range of the jump.  Respects circular distance setting!                          |
| `cooldown`        | Disregard target location entirely when leaping, leading to completely random jumps.                       |
| `eoc`             | (Required) Array of ids, the effect on condition . See `NPCs.md`                                           |
| `condition`       | Object, dialogue conditions enabling the attack.  See `NPCs.md` for the possible conditions, `u` refers to the monster.                |
| `allow_no_target` | Default `false`. If true the script will always run, the beta talker will be discarded.                    |

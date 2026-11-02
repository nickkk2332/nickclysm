<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
*Contents*

- [Martial arts and Techniques](#martial-arts-and-techniques)
  - [Martial arts](#martial-arts)
    - [Martial art event EOCs](#martial-art-event-eocs)
    - [Martial art buffs](#martial-art-buffs)
    - [Grappling](#grappling)
    - [Grab counters](#grab-counters)
  - [Techniques](#techniques)
    - [Fallback and basic techniques](#fallback-and-basic-techniques)
    - [The manual technique menu](#the-manual-technique-menu)
    - [Technique effects](#technique-effects)
  - [Bonuses](#bonuses)
    - [Examples](#examples)
  - [Attack vectors](#attack-vectors)
  - [How to use](#how-to-use)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# Martial arts and Techniques

A Martial art (MA for short) just like in real life, is a set of physical practices, skills and stances for combat and sport.  In CDDA, these represent different combat styles of the survivor.

These are defined in JSON as `martial_art`, which sets rules for usage and application, and importantly, the list of `techniques` that can be used.


## Martial arts

```jsonc
{
  "type": "martial_art",
  "id": "style_debug",                   // Unique ID. Must be one continuous word, use underscores if necessary
  "name": "Debug Mastery",               // In-game name displayed
  "description": "A secret martial art used only by developers and cheaters.",    // In-game description
  "initiate": [ "You stand ready.", "%s stands ready." ],    // Message shown when player or NPC selects this MA
  "autolearn": [ [ "unarmed", "2" ] ],   // A list of skill requirements that if met, automatically teach the player the MA
  "teachable": true,                     // Whether it's possible to teach this style between characters
  "allow_all_weapons": true,             // This MA always works, no matter what weapon is equipped (including no weapon)
  "force_unarmed": true,                 // You never use the equipped item to make attacks with this MA, and will use only your fist, legs or another limbs you have
  "prevent_weapon_blocking": true,       // You never block using weapon with this MA
  "strictly_melee": true,                // This style can only be used with weapons, it can't be used with bare hands
  "arm_block_with_bio_armor_arms": true, // Enables blocking damage using the Arms Alloy Plating CBM
  "leg_block_with_bio_armor_legs": true, // Enables blocking damage using the Legs Alloy Plating CBM
  "autolearn": [ [ "melee", 1 ] ],       // The style is autolearned once you reach this level in specific skill or skills
  "primary_skill": "bashing",            // The difficulty and effectiveness of this MA scales from this skill (Default is "unarmed")
  "learn_difficulty": 5,                 // Difficulty to learn the style from a book based on "primary_skill". Total chance to learn a style from a single read of the book is equal to one in (10 + learn_difficulty - primary_skill)
  "arm_block": 99,                       // Unarmed skill level at which arm blocking is unlocked
  "leg_block": 99,                       // Unarmed skill level at which arm blocking is unlocked
  "nonstandard_block": 99,               // Unarmed skill level at which blocking with "nonstandard" mutated limbs is unlocked
  "static_buffs": [ { "id": "debug_elem_resist" } ],    // List of buffs that are automatically applied every turn. Same syntax for the following fields with "_buffs" in their name. For further details, see Martial art buffs
  "onmove_buffs": [  ],                  // List of buffs that are automatically applied on movement
  "onpause_buffs": [  ],                 // List of buffs that are automatically applied when passing a turn
  "onattack_buffs": [  ],                // List of buffs that are automatically applied after any attack, hit or miss
  "onhit_buffs": [  ],                   // List of buffs that are automatically applied on successful hit
  "onmiss_buffs": [  ],                  // List of buffs that are automatically applied on a miss
  "oncrit_buffs": [  ],                  // List of buffs that are automatically applied on a crit
  "ongethit_buffs": [  ],                // List of buffs that are automatically applied on being hit
  "ondodge_buffs": [  ],                 // List of buffs that are automatically applied on successful dodge
  "onblock_buffs": [  ],                 // List of buffs that are automatically applied on successful block
  "onkill_buffs": [  ],                  // List of buffs that are automatically applied upon killing an enemy
  "ongrab_buffs": [  ],                  // List of buffs that are automatically applied when you get a hold on a creature
  "ongrabbed_buffs": [  ],               // List of buffs that are automatically applied when something gets a hold on you
  "static_eocs": [                       // List of eocs that are automatically triggered every turn
    "EOC_ID",                            // Syntax allows either eoc IDs or as inline blocks
    { "id": "INLINE_EOC_ID" }
  ],
  "onmove_eocs": [  ],                   // List of eocs that are automatically triggered on movement. The event EOCs below also get the opponent and context values, see Martial art event EOCs
  "onpause_eocs": [  ],                  // List of eocs that are automatically triggered when passing a turn
  "onattack_eocs": [  ],                 // List of eocs that are automatically triggered after any attack, hit or miss
  "onhit_eocs": [  ],                    // List of eocs that are automatically triggered on successful hit
  "onmiss_eocs": [  ],                   // List of eocs that are automatically triggered on a miss
  "oncrit_eocs": [  ],                   // List of eocs that are automatically triggered on a crit
  "ongethit_eocs": [  ],                 // List of eocs that are automatically triggered on being hit
  "ondodge_eocs": [  ],                  // List of eocs that are automatically triggered on successful dodge
  "onblock_eocs": [  ],                  // List of eocs that are automatically triggered on successful block
  "onkill_eocs": [  ],                   // List of eocs that are automatically triggered upon killing an enemy
  "ongrab_eocs": [  ],                   // List of eocs that are automatically triggered when you get a hold on a creature
  "ongrabbed_eocs": [  ],                // List of eocs that are automatically triggered when something gets a hold on you
  "techniques": [                        // List of techniques available when this MA is used
    "tec_debug_slow",
    "tec_debug_arpen"
  ],
  "weapons": [ "tonfa" ],                // List of weapons usable with this art
  "weapon_category": [ "MEDIUM_SWORDS" ], // List of weapons categories usable with this MA
  "grapple_weapons": {                   // What may stay in hand while grabbing a creature. Default: nothing, grappling is unarmed. See Grappling
    "categories": [ "KNIVES" ],          // Weapons in these weapon categories
    "items": [ "chain" ],                // These item types
    "any": false                         // true allows every weapon (then leave the lists out)
  }
}
```


### Martial art event EOCs

The event EOCs run with the martial artist as the alpha (`u_`) talker and the other creature in the exchange as the beta (`npc_`) talker: the target for attack events, the attacker for defensive events. They also get context values that describe the attack (read them with `{ "context_val": "damage" }` or `_damage` in math). `static_eocs`, `onmove_eocs` and `onpause_eocs` have no beta talker and no context values.

| Field | Beta (`npc_`) talker | Context values |
| --- | --- | --- |
| `onattack_eocs` | target | `weapon`, `hit`; after a hit also `technique`, `bp`, `crit`, `damage` and `killed` |
| `onhit_eocs` | target | `weapon`, `hit`, `technique`, `bp`, `crit`, `damage` |
| `oncrit_eocs` | target | same as `onhit_eocs` |
| `onkill_eocs` | target (now dead) | same as `onhit_eocs`, plus `killed` |
| `onmiss_eocs` | target | `weapon`, `hit` |
| `ongethit_eocs` | attacker, if there is one | `bp`, `damage` |
| `onblock_eocs` | attacker | `bp`, `damage` |
| `ondodge_eocs` | attacker, if there is one | none |
| `ongrab_eocs` | the creature you got a hold on | `grab_strength`, `grab_part` |
| `ongrabbed_eocs` | the grabber | `grab_strength`, `grab_part` |

- `weapon`: string, id of the weapon used for the attack, `null` when unarmed.
- `technique`: string, id of the technique used for the attack, `tec_none` when none.
- `bp`: string, body part hit. For attack events it is the target's; for `ongethit_eocs` it is your own body part about to be hit, and for `onblock_eocs` the body part that takes the blocked hit.
- `damage`: number. For attack events, the damage the hit dealt after armor. For `ongethit_eocs`, the incoming damage before blocking and armor; for `onblock_eocs`, the damage left after the block, before armor.
- `hit`, `crit`, `killed`: 1 or 0.
- `grab_strength`: number, the strength of the hold (1 to 100, the intensity of the `GRAB` effect).
- `grab_part`: string, the body part held: the victim's for `ongrab_eocs`, your own for `ongrabbed_eocs`. Not set when the creature held is a monster (a monster is held as a whole).

`ongrab_eocs` and `ongrabbed_eocs` run when a hold is established: your grab or an EOC's (anything that goes through the grappling code), and a monster's grab attack once the attack is over.  They run for every hold, also one you go along with (a friend's grab) or while you are asleep; a grab counter (see [Grab counters](#grab-counters)) comes after `ongrabbed_eocs`, so an `ongrabbed_buffs` buff can be what a counter requires.

`onhit_eocs` run after the hit is dealt, so they see the damage; they still run when the hit deals no damage (check `_damage`). `ongethit_eocs` run for every melee hit you take, also while you are asleep, winded or driving and can't block. The beta talker can be missing (for example a hit with no attacker), so check `has_beta` before using `npc_` effects.

```jsonc
"onkill_eocs": [
  {
    "id": "EOC_MY_STYLE_FEAST",
    "condition": { "and": [ "has_beta", { "math": [ "_damage >= 10" ] } ] },
    "effect": [ { "u_add_effect": "my_style_feast", "duration": "30 s", "intensity": { "math": [ "_damage / 10" ] } } ]
  }
],
"ondodge_eocs": [
  { "id": "EOC_MY_STYLE_RIPOSTE", "condition": "has_beta", "effect": [ { "npc_add_effect": "dazed", "duration": "2 s" } ] }
]
```


### Martial art buffs

```jsonc
...
  {
    "id": "debug_elem_resist",           // Unique ID. Must be one continuous word
    "name": "Elemental Resistance",      // In-game name displayed, would be shown in character menu
    "description": "You are a walking tank!\n\nBash armor increased by 100% of Strength (...)",    // In-game description, would be shown in character menu
    "buff_duration": 2,                  // Duration in turns that this buff lasts
    "persists": false,                   // Allow buff to remain when changing to a new style
    "unarmed_allowed": true,             // Effect is applied when you have no weapon equipped
    "melee_allowed": true,               // Effect is applied when you have some melee weapon equipped
    "strictly_unarmed": true,            // Effect is applied only when you have no weapon whatsoever, even unarmed weapons
    "wall_adjacent": true,               // Effect is applied when you stand near the wall
    "stealthy": true,                    // While you have this effect, your footsteps have half the normal sound
    "quiet": true,                       // While you have this effect, your attacks make no sound
    "melee_bash_damage_cap_bonus": true, // Your melee skill increases your maximum bashing damage
    "required_buffs_any": [ "mma_buff_tiger_claw_oncrit" ],    // This effect requires any of the named buffs to be applied
    "required_buffs_all": [ "mma_buff_panzer_static1" ],    // This effect requires all of the named buffs to be applied
    "forbidden_buffs_any": [ "mma_buff_hylian_static2", "mma_buff_hylian_static3" ],   // This effect is forbidden if any of the named buffs are applied
    "forbidden_buffs_all": [ "mma_buff_tiger_claw_onmove" ],   // This effect is forbidden if all of the named buffs are applied
    "weapon_categories_allowed": [ "BLADES", "KNIVES" ], // Restrict buff to only these categories of weapons. If omitted, all weapon categories are allowed
    "max_stacks": 8,                     // Maximum number of stacks on the buff. Buff bonuses are multiplied by current buff intensity
    "bonus_blocks": 1,                   // Extra blocks per turn
    "bonus_dodges": 1,                   // Extra dodges per turn
    "free_dodges": 1,                    // Dodges per turn that won't consume stamina. Can be higher than actual amount of dodges, but won't add more dodges.
    "flat_bonuses": [  ],                // Flat bonuses, see Bonuses below
    "mult_bonuses": [  ],                // Multiplicative bonuses, see Bonuses below
  }
...
```

Martial art buffs (`static_buffs`, `onmove_buffs`, etc.) are inlined in their parent `martial_art` as seen in the [previous example](MARTIALART_JSON.md#martial-arts), and thus lack their own `type`.

Additionally, when read externally (such as, by EOCs), their syntax is `mabuff:ID`:

```jsonc
  "condition": { "u_has_effect": "mabuff:buff_aikido_static1" },
```

A buff can also carry character flags in `"flags"`, which the martial artist has while the buff is active.  A static buff is the usual way for a style to grant `MA_BASIC_FALLBACK` (see [Fallback and basic techniques](#fallback-and-basic-techniques)).


### Grappling

Grappling techniques (ported from Cataclysm: The Last Generation) are ordinary techniques whose `condition` asks for a hold.  A creature that is held has an effect with the `GRAB` flag (`grabbed`), and the one holding it has an effect with the `GRAB_FILTER` flag (`grabbing`).  Monster grabs and a character's grab set the same effects, so in a technique condition (`u` is the martial artist, `npc` the target):

| You want | Condition |
| --- | --- |
| you hold the target | `{ "and": [ { "u_has_flag": "GRAB_FILTER" }, { "npc_has_flag": "GRAB" } ] }` |
| the target holds you | `{ "and": [ { "u_has_flag": "GRAB" }, { "npc_has_flag": "GRAB_FILTER" } ] }` |
| not while you hold anyone | `{ "not": { "u_has_flag": "GRAB_FILTER" } }` |

These flags say that *someone* holds or is held, not who: if a third creature holds the target, the first row is true as long as you hold anyone.  That is why "the target holds you" checks the target's own `GRAB_FILTER`: your `GRAB` alone could come from someone else.  A technique that works either way round ("held or holding") puts the two rows in an `or`.

Grappling is unarmed.  A style can let some weapons stay in hand with `grapple_weapons`: while the style is selected, a listed weapon neither stops you from grabbing nor makes you let go.  Weapons match by weapon category or by item type, or `"any": true` allows all of them.  Unknown categories and items are load errors.  `grapple_weapons` is read as a whole: a `copy-from` style either keeps its parent's list or replaces it.  The vanilla styles have none.

Grapple techniques combine a hold with the usual weapon requirements, so a knife-in-the-clinch style is pure JSON.  The TEST_DATA mod carries this example (`test_style_grapple`):

```jsonc
{
  "type": "technique",
  "id": "test_tech_grapple_knife",
  "name": "Knife in the Clinch",
  "melee_allowed": true,
  "weapon_categories_allowed": [ "KNIVES" ],                            // needs a knife in hand...
  "condition": { "and": [ { "npc_has_flag": "GRAB" }, { "u_has_flag": "GRAB_FILTER" } ] },    // ...and a hold on the target
  "condition_desc": "* Only works with a <info>knife</info> on a target <info>you are grabbing</info>",
  "messages": [ "You hold %s close and drive your knife in", "<npcname> holds %s close and drives a knife in" ],
  "mult_bonuses": [ { "stat": "damage", "type": "stab", "scale": 1.5 }, { "stat": "damage", "type": "cut", "scale": 1.5 } ],
  "attack_vectors": [ "vector_null" ]
},
{
  "type": "martial_art",
  "id": "test_style_grapple",
  "name": { "str": "Test Grappling" },
  "description": "Test grappling style: basic attacks are only fallbacks, and a knife or a chain may stay in hand while grabbing.",
  "initiate": [ "You get ready to grapple.", "%s gets ready to grapple." ],
  "static_buffs": [
    {
      "id": "test_buff_grapple_fallback",
      "name": "Test Grappler's Stance",
      "description": "Your basic punches, kicks and headbutts are only fallbacks.",
      "unarmed_allowed": true,
      "melee_allowed": true,
      "flags": [ "MA_BASIC_FALLBACK" ]                                  // basic attacks become fallbacks
    }
  ],
  "weapon_category": [ "KNIVES" ],                                      // knives are style weapons...
  "grapple_weapons": { "categories": [ "KNIVES" ], "items": [ "chain" ] },    // ...and may be held while grabbing
  "techniques": [ "test_tech_grapple_strike", "test_tech_grapple_basic", "test_tech_grapple_knife" ]
}
```

The vanilla grapple techniques show more patterns.  Judo's `tec_judo_throw`, `tec_judo_disarm` and `tec_judo_backthrow` need a grapple with the target, whoever holds whom, so a judoka also answers a grab.  Pankration's `tec_pankration_grabknee` and `tec_pankration_grabdisarm` work on a downed target or one in the clinch either way round.  Zui Quan's `tec_zuiquan_throw` (Drunken Fall) works held or holding and downs you as well as the target; its `tec_zuiquan_fist` and `tec_zuiquan_spin` are barred while grappling.  Brawling's `tec_brawl_strangle` needs your own hold (being held is not enough) and puts `choking` and `strangulation` on a breathing target, whose damage ignores armor.  All of them set `menu_visibility` so the manual technique menu lists them only when they could matter (see [The manual technique menu](#the-manual-technique-menu)).


### Grab counters

A technique with `"grab_counter": true` answers a grab the way a `dodge_counter` answers a dodge: when something gets a hold on you (a monster's grab attack, or a character's grab, also one started by an EOC), you attack the grabber with a grab counter picked as usual among those you can use.  First your style's `ongrabbed_buffs` and `ongrabbed_eocs` run, so a buff they add can be what the counter requires.  There is no counter when:

- you go along with the grab: the avatar grabs you and you are its ally, or a neutral too helpless to mind (`grapple::grab_tolerated`); an enemy always answers;
- you can't act: asleep, unconscious, paralyzed by fear, winded, driving, or unable to move or attack;
- the grabber is out of melee reach (a monster with a long grab), or dead;
- the counter would cost more moves than you have this turn (as for other counters).

Below a third of your stamina you only get "You try to counterattack, but you are too exhausted!".  The counter is an ordinary attack: it can miss, and its `condition` sees the grabber as `npc`.  A counter with a knockback (`knockback_dist`) breaks the hold, since a knockback makes its target let go; other effects leave the hold in place.  The passive `grab_break` of a defensive technique is separate: it still helps you struggle free later.

Judo's `tec_judo_grab_counter` (a throw, unarmed 1), Aikido's `tec_aikido_grab_counter` (a wrist turn that throws the grabber off with little harm, unarmed 3) and Krav Maga's `tec_krav_maga_grab_counter` (a strike that drives the grabber off, melee 3) are the vanilla grab counters.


## Techniques

Techniques are special attacks performed while using a given MA style.  These are automatically used according to their specific requirements, as defined in their JSON blocks:

```jsonc
{
  "type": "technique",
  "id": "tec_debug_arpen",               // Unique ID. Must be one continuous word
  "name": "phasing strike",              // In-game name displayed
  "attack_vectors": [ "vector_1", "vector_2" ],    // What attack vector would be used for this technique. For more info see Attack vectors below
  "unarmed_allowed": true,               // Can an unarmed character use this technique
  "weapon_categories_allowed": [ "BLADES", "KNIVES" ],    // Restrict technique to only these categories of weapons. If omitted, all weapon categories are allowed
  "melee_allowed": true,                 // Means that ANY melee weapon can be used, NOT just the MAs weapons
  "powerful_knockback": true,            // Currently not hooked up to code
  "skill_requirements": [ { "name": "melee", "level": 3 } ],    // Skills and their minimum levels required to use this technique. Can be any skill
  "weapon_damage_requirements": [ { "type": "bash", "min": 5 } ],    // Minimum weapon damage required to use this technique. Can be any damage type
  "required_buffs_any": [ "eskrima_hit_buff" ],    // This technique requires any of the named buffs to be active
  "required_buffs_all": [ "eskrima_hit_buff" ],    // This technique requires all of the named buffs to be active
  "forbidden_buffs_any": [ "eskrima_hit_buff" ],   // This technique is forbidden if any of the named buffs are active
  "forbidden_buffs_all": [ "eskrima_hit_buff" ],   // This technique is forbidden if all of the named buffs are active
  "req_flags": [ "" ],                   // List of item flags the used weapon needs to be eligible for the technique
  "required_char_flags": [ "" ],         // List of "character" (bionic, trait, effect or bodypart) flags the character needs to be able to use this technique
  "required_char_flags_all": [ "" ],     // This technique requires all of the listed character flags to trigger
  "forbidden_char_flags": [ "" ],        // List of character flags disabling this technique
  "needs_ammo": true,                    // Technique works only if weapon is loaded. Consumes 1 charge per attack 
  "crit_tec": true,                      // This technique only works on a critical hit
  "crit_ok": true,                       // This technique works on both normal and critical hits
  "reach_tec": true,                     // This technique only works on a reach attack hit
  "reach_ok": true,                      // This technique works on both normal and reach attack hits
  "condition": "u_is_outside",           // Array of dialog conditions the attack requires to trigger. Failing these will disqualify the tech from being selected. Same syntax as the eoc "condition" field
  "condition_desc": "Needs X",           // Description string describing the conditions of this attack (since dialog conditions can't be automatically evaluated). The manual technique menu shows it as the reason a technique can't be used when its condition fails, without a leading "* " and colors
  "menu_visibility": "grappling",        // When the manual technique menu lists this technique: "always" (default), "grappling", "grappled", "grapple" or a condition. See The manual technique menu
  "repeat_min": 1,                       // Technique's damage and any added effects are repeated rng(repeat_min, repeat_max) times. The target's armor and the effect's chances are applied for each repeat
  "repeat_max": 1,
  "knockback_dist": 1,                   // Distance target is knocked back
  "knockback_spread": 1,                 // The knockback may not send the target straight back
  "knockback_follow": true,              // Attacker will follow target if they are knocked back
  "controlled_knockback": true,          // In manual combat mode, you aim the knockback of this technique when you choose it in the technique menu. Needs knockback_dist. See The manual technique menu
  "stun_dur": 2,                         // Duration that target is stunned for
  "down_dur": 2,                         // Duration that target is downed for
  "side_switch": true,                   // Technique moves the target behind user
  "disarms": true,                       // This technique can disarm the opponent, triggers only against armed NPCs or monsters with the "WIELDED_WEAPON" flag
  "take_weapon": true,                   // Technique will disarm and equip target's weapon if hands are free
  "grab_break": true,                    // This technique may break a grab against the user
  "aoe": "spin",                         // This technique has an area-of-effect. Doesn't proc against solo targets. Options are `aoe` (attacks adjacent nearby enemies), `impale` (hits target and one enemy behind it), and `wide` (hits enemies in an arc near the target)
  "block_counter": true,                 // This technique may automatically counterattack on a successful block
  "dodge_counter": true,                 // This technique may automatically counterattack on a successful dodge
  "grab_counter": true,                  // This technique may automatically counterattack when something gets a hold on you. See Grab counters
  "weighting": 2,                        // Affects likelihood this technique will be selected when many are available. Negative weighting means the technique is only included in the list of possible techs once out of every `weighting` times ( 1/3 for a weighting of -3). For a fallback, see Fallback and basic techniques
  "fallback": true,                      // For an attacker with the MA_BASIC_FALLBACK flag, used only when no other technique is possible. Otherwise an ordinary technique. See Fallback and basic techniques
  "basic": true,                         // Joins whichever techniques get picked from, without keeping fallbacks out. See Fallback and basic techniques
  "defensive": true,                     // Game won't try to select this technique when attacking
  "miss_recovery": true,                 // Misses while attacking will use half as many moves
  "messages": [                          // What is printed when this technique is used by the player and by an npc. Should NOT end with punctuation mark, for it is then extended with ` but deal no damage` or ` and deal X damage`
    "You phase-strike %s",
    "<npcname> phase-strikes %s"
  ],
  "eocs": [ "EOC_ID", { "id": "INLINE_EOC_ID" } ],    // EOCs that trigger each time this technique does, with the attacker as the speaker and the target as the listener. Syntax allows either eoc IDs or as inline blocks. They run before the hit is dealt, once per repeat
  "post_hit_eocs": [ "EOC_ID" ],          // EOCs that run once after the hit is dealt, with the attacker as alpha and the target as beta. They get the same context values as a martial art's onkill_eocs (see Martial art event EOCs): `weapon`, `hit`, `technique`, `bp`, `crit`, `damage` (damage dealt after armor; 0 if the target died before the hit landed) and `killed` (1 if the target is dead)
  "flat_bonuses": <array>,               // Any bonuses, see Bonuses below
  "mult_bonuses": <array>,
  "tech_effects": <array>                // List of effects applied by this technique, see Bonuses below
}
```

All fields, except for the ID, type and name are optional.

Load-time checks report an unknown `post_hit_eocs` entry, an unknown `tech_effects` id, `fallback` or `basic` on a `dummy` or `defensive` technique (attacks never pick those), a technique that is more than one kind of counter (it could never be used), `grab_counter` on a `dummy`, `defensive` or `crit_tec` technique, and `controlled_knockback` without a `knockback_dist`.

A technique's knockback breaks the target's holds on both sides: a monster lets go of every grab it had, and every character hold the target is part of, holding or held, ends.

For example, a cut that makes the target bleed longer the harder it hit:

```jsonc
"post_hit_eocs": [
  {
    "id": "EOC_DEEP_CUT_BLEED",
    "condition": { "and": [ "has_beta", { "math": [ "_damage > 0 && _killed == 0" ] } ] },
    "effect": [ { "npc_add_effect": "bleed", "duration": { "math": [ "_damage * 10" ] }, "target_part": { "context_val": "bp" } } ]
  }
]
```


### Fallback and basic techniques

When you attack, every usable technique goes into one of three pools, and one entry is picked at random (a technique is entered once per point of positive `weighting`):

- **ordinary**: every technique that is neither of the below;
- **fallbacks**: techniques with `"fallback": true`, but only when the attacker has the `MA_BASIC_FALLBACK` character flag;
- **basics**: techniques with `"basic": true`.

If no ordinary technique is usable and some fallback is, the pick is from the fallbacks plus the basics.  Otherwise it is from the ordinary techniques plus the basics.  So a fallback is used only when the style has nothing better, and a basic technique (a natural attack, say) neither crowds out the style's techniques nor keeps the fallbacks out on its own.  With no usable technique at all, the attack is a plain hit.

The base punch, kick and headbutt (`tech_base_punch`, `tech_base_kick` and `tech_base_headbutt`, granted by hands, feet and the head) are fallbacks.  `MA_BASIC_FALLBACK` also switches on the grappling rules of the base techniques, written as conditions on that flag: a headbutt only in a grapple with the target, whoever holds whom, and no kicks while you hold someone.  Without the flag they behave as before: they compete with the style's techniques, and a headbutt needs an arm or hand of yours to be grabbed.  No vanilla style grants the flag; a style grants it with a static buff (see the [Grappling](#grappling) example), and a mutation, bionic or effect can grant it too.

For an attacker with the flag, a fallback also:

- stays usable while unarmed even if the selected style is `strictly_melee` (its other requirements still apply, and it never overrides a wielded weapon the style can't use);
- treats a negative `weighting` as a chance to compete as an ordinary technique this time (one in `-weighting`), instead of a chance to be discarded.  Cataclysm: The Last Generation gives its base attacks weightings of -10 (headbutt, kick) and -8 (punch).


### The manual technique menu

In manual combat mode (`toggle_manual_combat`, Shift+Tab by default) each melee attack of the player opens a menu of their techniques before anything is rolled or spent.  Besides "Automatic" (the usual random choice) and "Basic attack" (no technique; unarmed, a basic punch, kick or headbutt), it lists every technique of the weapon, the style, worn equipment and the body, grouped by where they come from, with the mutation attacks at the end (they happen on their own and can't be chosen).  An EOC can open the same menu with `u_choose_technique` (see [EFFECT_ON_CONDITION.md](EFFECT_ON_CONDITION.md#u_choose_technique-npc_choose_technique)).

The menu applies the same rules as the automatic choice, only nothing random: a negative `weighting` is not rolled, and an attack vector only has to exist.  A technique that can't be used now is greyed out with the reason in the second column, and its description, with that reason, in the panel below.  When the reason is the technique's `condition`, the menu shows its `condition_desc`, so write it as a short phrase after the bullet: `"* Only works on a <info>downed</info> target"` shows as "Only works on a downed target".  Without a `condition_desc` the menu says "conditions not met".  Technique conditions are evaluated on their own random numbers, so a condition that rolls (`roll_contested`, say) gives the same answer until the next turn.

A `crit_tec` technique can be chosen: it comes off if the hit is a critical hit, and otherwise the attack is a basic attack.  Counters (`dodge_counter`, `block_counter`, `grab_counter`) and `defensive` techniques are listed but can't be chosen; they still trigger on their own.  A chosen technique otherwise is used as if forced, whatever the hit (a technique that is not `crit_ok` is still used on a crit).

`menu_visibility` keeps a technique that only matters in a grapple out of the menu the rest of the time.  It only decides whether the menu lists the technique: the automatic choice, and whether the technique can be used, are unchanged (that is still the technique's `condition`).

| Value | Listed |
| --- | --- |
| `"always"` (default) | always; greyed out with the reason when it can't be used |
| `"grappling"` | while the attacker holds a creature (`GRAB_FILTER` on `u`) |
| `"grappled"` | while the attacker is held (`GRAB` on `u`) |
| `"grapple"` | while either is true |
| a condition object | while the condition is true (`u` is the attacker, `npc` the target, as in `condition`) |

Any other string is a load error.  For example, Pankration's arm lock is listed against a downed target or in a grapple:

```jsonc
"condition": {
  "or": [
    { "npc_has_effect": "downed" },
    { "and": [ { "npc_has_flag": "GRAB" }, { "u_has_flag": "GRAB_FILTER" } ] },
    { "and": [ { "npc_has_flag": "GRAB_FILTER" }, { "u_has_flag": "GRAB" } ] }
  ]
},
"condition_desc": "* Only works on a <info>downed</info> target, or one <info>you are grabbing</info> or <info>that is grabbing you</info>",
"menu_visibility": { "or": [ { "npc_has_effect": "downed" }, { "u_has_flag": "GRAB_FILTER" }, { "u_has_flag": "GRAB" } ] }
```

A technique with `"controlled_knockback": true` (from Cataclysm-BN) lets you aim its knockback when you choose it in the menu: once the hit lands, the creature-throw targeting screen opens on the target, with the technique's `knockback_dist` as its range, and the target is knocked back toward the tile you pick, as far as you pick.  It goes in any direction across, but not through you.  Cancelling knocks it back as usual.  The automatic choice, counters, NPCs and attacks outside manual combat mode are unchanged.  Aikido's counter and disarming throws and Pankration's Grab and Throw are aimable.


### Technique effects

```jsonc
...
  "tech_effects": [
    {
      "id": "eff_expl",                  // Unique ID. Must be one continuous word
      "chance": 100,                     // Percent chance to apply the effect on this attack
      "permanent": false,                // If true the effect won't decay (default: false)
      "duration": 15,                    // Duration of the effect in turns
      "intensity": 2,                    // Intensity to set the effect to, capped at its max_intensity (default: 0, the effect's normal starting intensity)
      "target": "target",                // Who gets the effect: "target" (default) or "self", the martial artist. "hit_self": true is accepted as "target": "self"
      "on_damage": true,                 // If true the effect will only be applied if the attack succeeded in doing damage (default: true)
      "condition": "ANY",                // conditions necessary for the technique to apply the effect. `u` is the martial artist, `npc` is the target, whoever gets the effect
      "message": "Example"               // The message to print if you successfully apply the effect, %s is replaced by the name of whoever gets it
    }
  ]
...
```

A self effect is how a technique costs the user something.  Zui Quan's Drunken Fall throws yourself down with the target you hold:

```jsonc
"down_dur": 3,
"tech_effects": [
  { "id": "downed", "chance": 100, "duration": 100, "target": "self" },
  { "id": "staggered", "chance": 100, "duration": 300 }
]
```

An unknown `target` value, a `hit_self` that contradicts `target`, or a negative `intensity` is a load error.

`tech_effects` are inlined in their parent `technique` as seen in the [previous example](MARTIALART_JSON.md#techniques), and thus lack their own `type`.


## Bonuses

The fields `flat_bonuses` and `mult_bonuses` can contain any number of bonus entries:

```jsonc
  "flat_bonuses": [
    { "stat": "dodge", "scale": 1.0 },
    { "stat": "damage", "type": "bash", "scaling-stat": "per", "scale": 0.15 }
  ]
```

Bonuses can be defined with the following values:

Field            | Description
----             |----
`stat`           | Affected statistic.  Can be any of: `hit`, `dodge`, `block`, `speed`, `movecost`, `damage`, `armor`, `arpen`, `crit_chance`, `block_effectiveness`.
`type`           | Damage type for the affected statistic (`bash`, `cut`, `heat`, etc.).   Only needed if the affected statistic is either `damage`, `armor`, or `arpen`.
`scale`          | Value of the bonus.
`scaling-stat`   | (Optional) Stat used for scaling.  Can be any of the four classic stats (`str`, `dex`, `int`, `per`) or any skill (`bashing`, `dodge`, `unarmed`, `rifles`, `spellcraft`, etc.).  If the scaling stat is specified, the value of the bonus is multiplied by the corresponding user stat/skill.

* Bonuses must be written in the correct order.
* Tokens of "useless" type will not cause an error, but will not have any effect.  For example, `speed` in a technique will have no effect (`movecost` should be used for techniques).
* Multiplicative bonuses only support `damage` and `movecost`.
* Flat bonuses are applied after multiplicative bonuses.


### Examples

Incoming bashing damage is decreased by 30% of strength value.  Only useful on buffs:

```jsonc
  "flat_bonuses": [ { "stat": "armor", "type": "bash", "scaling-stat": "str", "scale": 0.3 } ]
```


All cutting damage done is multiplied by 10% of dexterity:

```jsonc
  "mult_bonuses": [ { "stat": "damage", "type": "cut", "scaling-stat": "dex", "scale": 0.1 } ]
```


Move cost is decreased by 100% of strength value

```jsonc
  "flat_bonuses": [ { "stat": "movecost", "scaling-stat": "str", "scale": -1.0 } ]
```


## Attack vectors

Attack vectors define which (sub)bodypart are used for the attack in question.  It allows filtering eligible bodyparts and apply the relevant worn armor's unarmed damage to the attack.  The list is present at [attack_vectors.json](/data/json/attack_vectors.json)

Note: for the (sub)part to apply its unarmed damage it needs unrestricted natural attacks.

```jsonc
[ 
  {
    "type": "attack_vector",             // Always attack_vector
    "id": "vector_hand",                 // Unique ID. Must be one continuous word
    "limbs": [ "hand_l", "hand_r" ],     // List of bodyparts used in this attack (relevant for HP/encumbrance/flag filtering)
    "contact_area": [ "hand_fingers_l", "hand_fingers_r" ],    // List of subbodyparts that can be used as a strike surface in the attack using the subbodypart's armor or intrinsic unarmed damage
    "strict_limb_definition": false,     // Bool, default false. When true *only* the bodyparts defined above are used for the vector, otherwise similar bodyparts can be used as long as both the contact area and the defined limb are similar, see JSON_INFO.md/Bodyparts for bodypart similarity
    "armor_bonus": true,                 // Bool, defines if the vector takes the unarmed damage bonus of the armor worn on the contact area into account (default: true)
    "required_limb_flags": [ "foo", "bar" ],    // List of character flags required for the bodypart to be eligible for this vector
    "forbidden_limb_flags": [ "foo", "bar" ],   // List of character flags that disqualify a limb from being usable by this vector
    "encumbrance_limit": 15,             // Int, default 100, encumbrance of the limb above this will disqualify it from this vector
    "bp_hp_limit": 75 ,                  // Int, default 10, percent of bodypart limb HP necessary for the limbs to qualify for this vector. For minor (non-main) bodyparts the corresponding main part HP is taken into account
    "limb_req": [ [ "arm", 2] ]          // Array of pairs. Limb type requirements for this vector. The character must have this many limbs of the given type above the limb's health limit (See JSON_INFO.md:Bodyparts). Requirements must all be met

  }
]
```


## How to use

MA can be granted to characters in different ways.  They can be `autolearn`ed (as in the MA example), learned from [books](JSON_INFO.md#books) (as `martial_art`), be granted by traits (as `initial_ma_styles`), or dynamically by [eocs](EFFECT_ON_CONDITION.md#u_learn_martial_artnpc_learn_martial_art).


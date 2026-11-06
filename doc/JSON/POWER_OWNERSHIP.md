# Power resources, cooldowns and owned applications

These opt-in primitives extend [abilities](ABILITIES.md) and
[damage defenses](DAMAGE_TRANSACTIONS.md). Custom actor pools, charge groups and
application instances use shared accounting and saved identities. Existing
native spell, item, monster-special and physiological resource policies remain
with those callers.

## Resources

```json
{
  "type": "power_resource",
  "id": "focus",
  "name": "focus",
  "capacity": 100,
  "initial": 100,
  "regen_amount": 2,
  "regen_ticks": 1,
  "actor_kinds": [ "avatar", "npc", "monster" ]
}
```

`capacity` is 1–1,000,000; `initial` defaults to capacity and must be between
zero and capacity. `regen_amount` defaults to zero, range 0–1,000,000;
`regen_ticks` defaults to one, range 1–1,000,000 world seconds. `actor_kinds` is
an optional allowlist; empty means all three supported creature kinds. IDs
`none`, `mana` and `stamina` are reserved. Definitions support `copy-from`.

Each actor has an independent pool. `mana` adapts the real Character pool for
avatars and NPCs. `stamina` adapts the avatar's real pool; native NPC stamina is
a synthetic constant and is explicitly unsupported. Monsters can use declared
custom pools. A custom pool such as `combat_stamina` can supply real spendable,
regenerating power energy to NPCs without changing their native fatigue model.
No adapter borrows the avatar's pool for another actor.

Pools materialize on payment, reservation or explicit modification, including a
zero modification. Before that, queries return the declared initial balance;
they do not create a timer/account. This matters for initially empty pools:
initialize one explicitly if it should begin regenerating before its first
payment. Regeneration is arithmetic over elapsed world time, including time
unloaded, and saturates at capacity. A full pool cannot bank regeneration for a
later spend. Reads project the result without changing the saved account.

```json
{ "modify_power_resource": { "id": "focus", "actor": "u", "amount": -5 } }
```

`actor` is `u` (default) or `npc`; `id` accepts a string/variable; `amount`
accepts a number/variable/math but must evaluate to an integer −1,000,000 to
1,000,000. Grants stop at capacity; debits stop at available unreserved energy.
Explicit debits preserve unrelated native energy above a changed maximum.
Output: numeric `power_resource_changed` (actual signed delta), and `reason`.
No missing actor, unsupported adapter or invalid amount silently succeeds.

## Ability costs and reservations

An ability's existing `energy_cost.resource` can name a custom pool:

```json
{
  "energy_cost": { "resource": "focus", "amount": 20 },
  "reserve_energy": true,
  "cooldown": "force_strikes",
  "charge_cost": 1
}
```

`reserve_energy` defaults to false. With true, a full action claims its declared
energy on admission, without debiting it. Other power costs, shields and upkeep
see the remaining available balance. Release revalidates the actual pool and
commits energy, charge use and the normal move cost once, with no author callback
between their checks and accounting. Cancelling or interrupting a windup releases
the claim; it does not grant a refund of energy that was never spent.

Native consumers outside this accounting service can still drain native mana or
stamina. Such drains may make a reserved action unaffordable; it then interrupts
without restoring those externally spent points. Explicit power resource
modification respects reservations. A reservation is not a guarantee against
arbitrary native HP/resource assignments or changing JSON definitions.

Effect-only invocations retain their declared free policy: no energy claim,
energy/move payment or cooldown/charge use. Their ability must explicitly permit
`allow_effect_only`. Outer item charges and native special-attack cooldowns are
still independent costs. A committed effect exception does not refund payment.

## Cooldown and charge groups

```json
{
  "type": "power_cooldown",
  "id": "force_strikes",
  "name": "force strikes",
  "max_charges": 2,
  "initial_charges": 2,
  "recharge_ticks": 5,
  "cooldown_ticks": 1
}
```

Groups are actor-owned and shared by abilities that name the same ID.
`max_charges` is 1–1000; `initial_charges` defaults to maximum, range 0–maximum.
`recharge_ticks` defaults to one, range 1–1,000,000; charges recover sequentially,
one per interval, starting with the first spend. Spending another partially
recharging charge preserves that clock. At full capacity there is no banked
recharge. `cooldown_ticks` defaults to zero, range 0–1,000,000, and imposes a
separate minimum interval after a full action commits. `charge_cost` on the
ability defaults to one, range 1–1000, and cannot exceed the group's capacity.

A read never initializes a group. For a deliberately depleted initial group,
use an explicit recovery operation (zero is allowed) to start its recharge clock.
Natural recharge projects across time jumps/save loading without replaying EOCs.

```json
{ "recover_power_charge": { "id": "force_strikes", "actor": "u", "amount": 1 } }
```

`actor` and `id` follow resource-command conventions; `amount` defaults to one
and must be an integer 0–1000. It grants at most the missing charges, preserves
an ongoing partial recharge and does not waive a separate group cooldown.
Outputs: `power_charges_recovered` and `reason`.

## Owned applications and sustained powers

```json
{
  "type": "power_application",
  "id": "force_ward",
  "name": "force ward",
  "revision": 1,
  "magnitude": 5,
  "tags": [ "ward" ],
  "duration_ticks": 0,
  "interval_ticks": 1,
  "start_cost": { "resource": "focus", "amount": 5 },
  "upkeep": { "resource": "focus", "amount": 1 },
  "on_start": [ "EOC_WARD_START" ],
  "on_tick": [ "EOC_WARD_TICK" ],
  "on_end": [ "EOC_WARD_END" ]
}
```

Every application is independent, including two instances of the same definition
from different owners. It records an exact handle, owner, target, root/parent,
saved strength, timing, owned locals, actual start/upkeep spending and end reason.
It does not take control of either participant or occupy their action activity.

| Field | Contract |
| --- | --- |
| `revision` | 1–1,000,000, default 1. A changed saved revision ends the instance. |
| `magnitude` | Finite 0–1,000,000, default 1; saved on admission. |
| `tags` | Up to 32 tags, for contribution queries and source-aware dispelling. |
| `duration_ticks` | 0–1,000,000; zero (default) lasts until cancelled or another end condition. |
| `interval_ticks` | 1–3600, default 1; fixed world seconds independent of move speed. |
| `max_per_owner` | 1–128, default 8, for this definition across targets. |
| `start_cost`, `upkeep` | Same fixed resource/amount schema as ability energy costs. Upkeep is paid before each tick hook. |
| `interrupt_damage` | Default false; actual positive owner HP loss marks this instance and its children to end. |
| `interrupt_incapacitated` | Default false; owner unconsciousness/stun ends it. |
| `interrupt_displacement` | Default false; leaving the admission origin ends it. |
| Hooks | Arrays of up to 16 ACTIVATION EOCs (IDs/inline) each. |
| `locals` | Up to 64 declared numeric/string variables, saved and copied back by matching type; numeric outputs must stay finite. |

```json
{ "apply_power_application": { "id": "force_ward", "owner": "u", "target": "npc", "magnitude": 8 } }
{ "end_power_application": { "context_val": "power_application" } }
{ "dispel_power_applications": { "id": "force_ward", "owner": "u", "target": "npc", "tag": "ward" } }
```

Apply defaults owner/target to `u`; roles may be `u` or `npc`, and require actual
living tracked creatures. Optional request magnitude overrides the default.
Output `power_application` is the exact string handle (or `"0"`), plus `reason`.
Admission is queued; activation/start payment happen at a safe native pump.
An unaffordable start ends without that payment. End accepts an exact string
handle/variable. Dispel defaults owner/target to `u`; optional `id` and `tag`
restrict matching, and explicit `owner: "any"` permits all sources. It returns
`power_dispelled` and `reason`. Ending one source never deletes another's record.

Hooks use owner as alpha, target as beta. Context includes `power_application`
(the exact handle), `power_application_definition`, `power_application_root`,
`power_application_parent`, `power_application_depth`, `power_application_state`,
`power_application_reason`, `power_application_magnitude`, `power_application_ticks`,
`power_application_paid_start`, `power_application_paid_upkeep`, plus
`power_owner_present`/`power_target_present`.
End hooks may have missing roles; guard actor-dependent commands accordingly.
Re-resolve identities between hooks; never substitute a new occupant or avatar.

Applications created by application hooks are owned children. Parent ending
immediately removes their contributions, then the safe pump attempts their own
end hooks once. End hooks cannot recreate owned children. The runtime has 128
live applications, 64 admitted instances per family and maximum depth eight.
Terminal history is bounded to 32; an indefinite instance ends at one billion
executed upkeep ticks to keep counters and save states bounded. Callback exceptions keep real paid/world
changes, end managed membership, and do not replay completed cleanup on load.

Expiry happens before a tick at the same deadline. Maintenance runs in handle
order, before ability releases in the native power phase (after executions).
An application that cannot pay upkeep ends without that tick hook. Repeated
pumps in one world second do not charge or execute a tick again. A skipped world
second ends a sustained instance as `time_skipped`, without bulk replay of costs
or callbacks. Both participants must remain loaded and living; removal, death
and incompatible definitions end the application. World transitions settle
owned instances and prevent terminal hooks from queuing replacements.

## Contributions and pure queries

Applications own runtime membership, magnitude, locals and child instances.
Hook commands that modify native effects, items, stats or terrain retain those
commands' native ownership and cleanup semantics. This runtime does not convert
those changes into independently reversible native effect/enchantment grants.

Consumers can use pure membership/magnitude queries directly:

```json
{
  "type": "damage_rule",
  "id": "force_ward_defense",
  "required_application": "force_ward",
  "actions": [
    { "operation": "absorb", "resource": "none", "maximum": { "math": [ "u_power_application('force_ward', 'field': 'magnitude')" ] } }
  ]
}
```

`required_application` is an inexpensive opt-in gate, combined with any
`required_effect`. It requires an active contribution on the defender. Summed
magnitudes from independent sources compose; dispelling one subtracts only its
membership. Named resources in typed absorption/reflection use the same free
balance and reservations as abilities/upkeep.

| Read-only math function (`u_`/`n_`) | Fields / filters |
| --- | --- |
| `power_resource('id')` | `field`: `available` (default), `total`, `reserved`, `capacity`, `supported`. Unsupported/missing actors return zero. |
| `power_cooldown('id')` | `field`: `charges` (default), `capacity`, `remaining` (minimum cooldown seconds), `recharge` (next charge seconds), `supported`. |
| `power_application('id')` | `field`: `count` (default) or summed `magnitude`; `owner`: `any` (default), `u` or `npc`; optional `tag`. Empty ID matches all definitions. |

These reads do not run hooks, create accounts, debit charges, advance time or
draw randomness. Application queries exclude cancelled, expired or otherwise
invalid contributions before cleanup hooks finish. Recorded history and the
execution/powers inspector provide phase/reason, ownership, timing, claims,
actual payments, projected balances/charges and the latest 64 accounting changes.

## Persistence and bounds

The ability manager saves custom accounts, native/custom reservations, cooldown
clocks, active/pending applications, locals and bounded family budgets. Older
saves without the new `powers` member remain readable. Reservations must match
an owning saved ability; malformed/double-owned transaction states are refused.
Saving/loading during a partial dispatch is refused. An exception that leaves
owned cleanup pending must be followed by a safe cleanup pump before saving.

Each account registry is bounded to 4096 entries; new accounts refuse explicitly
at the bound. Finalized native deaths retire their pool/cooldown/claim records
without running power callbacks inside death cleanup. Unloaded actors retain
accounts for later use. Avatar accounts follow that avatar across dimension
snapshots; NPC/monster accounts remain dimension-bound. Application participants
are always dimension-bound and are ended for travel. Receipts/terminal history
are diagnostic and are not saved.

The `powers_demo` pack supplies independent ward/mirror focuses, a regenerating
focus pool, and two shared force-strike charges. Maintain both, toggle one off,
spend a strike and observe one charge recover; save/load preserves those states.
See the [milestone record](../../Audit/powers/08-owned-powers-milestone.md).

# Damage transactions and defenses

A `damage_rule` intercepts a complete typed packet in native `deal_damage`.
It can scale, cap, cancel, convert, absorb or reflect damage, and observe the
result. This is opt-in: ordinary damage without an applicable effect/rule or
explicit trace retains the native path. Existing melee, projectile, spell and
legacy EOC typed damage can encounter defenses; raw `apply_damage`, bulk health
changes and HP assignment bypass these hooks.

```json
{
  "type": "damage_rule",
  "id": "my_mana_shield",
  "required_effect": "my_mana_shield_effect",
  "defender_kinds": [ "avatar", "npc" ],
  "stage": "after_armor",
  "actions": [ { "operation": "absorb", "resource": "mana", "cost_per_damage": 1 } ]
}
```

With 10 mana, a 25-point eligible packet spends 10 mana, absorbs 10 points and
passes 15 onward. Mixed components share that same wallet. This is payment
against actual available resources during dispatch, without reservation across
turns. Enable/disable the rule with an ordinary effect; the effect's native
expiry and save behavior apply. Independent power applications and custom
resource pools are future work.

## Stages and selection

| Field | Contract |
| --- | --- |
| `id` | Required; generic `copy-from` supported. |
| `stage` | `after_armor` by default. `before_armor` runs before native armor/weakpoint mitigation and wear. `after_armor` runs after it, before native type effects, pain and HP commitment. `committed` observes the result and cannot edit it. |
| `priority` | Integer −1,000,000–1,000,000; higher first, lexical rule ID breaks ties. Stage order always takes precedence. |
| `required_effect` | Optional defender effect ID. Prefer this inexpensive opt-in gate to an always-applicable global condition. |
| `defender_kinds` | Optional allowlist: `avatar`, `npc`, `monster`. |
| `source_kinds` | Optional allowlist: those three plus `environment` (null source), `untracked` (not owned by the active world). |
| `damage_types` | Optional type allowlist. Edits operate on eligible components only. A committed observer matches types seen before/after armor, including completely absorbed ones. |
| `bodyparts` | Optional body part ID allowlist. Monsters have a contact part, rather than Character limb HP. |
| `spells` | Optional spell ID allowlist; matches the real spell effect's `damage_spell` metadata. |
| `condition` | Optional ordinary dialogue condition. Evaluated once per real rule attempt, never by inspection. |
| `actions` | Up to 16 typed edits, applied in order. |
| `mutate` | Up to 16 ACTIVATION EOCs (IDs or inline); approved outputs are read back before typed edits. |
| `on_resolved` | Up to 16 ACTIVATION EOCs (IDs or inline), after the primary HP write. |

An `after_armor` shield cannot undo armor wear. Dodge/block and other pre-hit
work have already happened. Native per-type special behavior (for example a
plastic monster's bash reduction), critical overkill limits and available HP
can further reduce the final HP write. Packet budgets and actual HP loss are
therefore reported separately. Effects outside typed damage, such as projectile
status payloads, are not universally cancelled by blocking its HP packet.

## Typed edits

Numeric parameters accept the usual number / variable / math object. Invalid,
non-finite or out-of-range parameters reject that edit and record a reason.

| `operation` | Parameters / behavior |
| --- | --- |
| `scale` | `value`, 0–100; multiplies eligible base amounts. |
| `cap` | `value`, 0–1,000,000; maximum combined eligible integer packet budget. |
| `cancel` | Removes all eligible integer damage. |
| `convert` | `from`, `to`, optional `fraction` (default 1, range 0–1). Before armor only; splits the base component while preserving its penetration/multipliers. The new type encounters its native armor/immunity. |
| `absorb` | `resource`: `mana`, `stamina`, `none`; optional positive integer `cost_per_damage` (default 1, maximum 1,000,000), optional `maximum` damage points (default 1,000,000). `maximum` is mandatory for `none`. |
| `reflect` | `value` fraction 0–1; optional `resource` (default `none`) and `cost_per_damage` as above. Removes the affordable integer portion and queues a new attributed packet to the actual source. |

The budget uses native per-component integer truncation of
`amount * damage_multiplier * unconditional_damage_mult`; immunity is excluded.
Absorption/caps allocate points by lexical damage type then original component
order, retaining native multipliers. Payment and publication of each validated
packet edit are adjacent, with no author callbacks between them. No points are
paid for when an exact valid edit cannot be represented. Explicit power costs
debit the resource pool directly: debug fatigue exemptions do not waive avatar
stamina payment, and payment does not discard unspent mana/stamina above a
changed maximum. NPC stamina is synthetic in the native engine, so NPC stamina
defenses report `unsupported_resource` without absorbing or paying. NPC mana
defenses use their real mana pool. A monster cannot spend
Character mana/stamina, and never borrows the avatar's resources. Free bounded
per-packet absorption works on monsters, but is not a shared persistent pool.

Reflection requires both living tracked participants at selection and dispatch.
A null/unavailable source leaves that portion of the incoming packet untouched
and unpaid. Reflection uses the defender as its new source/instigator, preserves
the family's original source, targets torso, and encounters the recipient's own
armor. Penetration is reset. It does not launch or reverse a physical projectile.
If an observer removes a participant after the incoming edit, the pending
reflection is skipped; previously committed edits/payments are not refunded.
Native death attribution uses the new source. This adds no new execution-trigger
channel for reflected damage.

## EOC actors and context

**Damage hooks use defender as alpha (`u`), actual source as beta (`npc`).**
Environmental/unavailable sources have no beta. This differs from legacy
damage-type EOCs, whose actor order is unchanged. There is no avatar fallback.
Participants are retained during callbacks, resolved by identity between hooks,
and remaining work stops when a required tracked participant disappears.

Every hook receives:

- Exact string handles `damage_transaction`, `damage_root`, `damage_parent`;
  numeric `damage_depth`; `damage_stage`, `damage_reason`, `damage_bodypart`.
- `damage_source_kind`, `damage_defender_kind`, numeric `damage_source_present`;
  string `damage_source_id`, `damage_instigator_id`, `damage_original_source_id`.
  The inspector/C++ report additionally retains the full dimension-bound refs.
- Integer budgets `damage_incoming`, `damage_after_armor`, `damage_remaining`,
  `damage_absorbed`, `damage_reflected`; captured `damage_hp_loss`.
- `damage_type_<id>` for the packet's known types, including zeroed ones.
- `damage_rule`; in `on_resolved`, `damage_rule_before`, `damage_rule_after`,
  `damage_rule_absorbed`, `damage_rule_reflected`, `damage_rule_paid_mana`,
  `damage_rule_paid_stamina`, `damage_rule_reason` for that rule.
- Available native attack facts, explicitly bound ability inputs and real
  `damage_spell`; reflection supplies `damage_reflection` and
  `damage_reflection_rule`. Unrelated callback casts use their own spell scope.

Each `mutate` EOC starts with numeric `damage_scale = 1`,
`damage_limit = 1000000`, `damage_cancel = 0`. Only these outputs are read back:
scale 0–100, cap 0–1,000,000, cancel exactly 0/1. They affect the rule's eligible
components. Changing `damage_hp_loss`, resource totals or other context facts
does not alter native results. Invalid output discards that hook's proposed
packet edit. EOC world effects remain real; the engine does not roll them back.
For example, inline `effect: [ { "math": [ "_damage_limit = 8" ] } ]` caps the
eligible packet at eight.

`on_resolved` runs after the original HP write (or complete blocking), once for
each eligible attempted rule. Committed-only observers cannot veto damage.
Actual HP loss is captured at the write, before healing or secondary HP-event
callbacks; it is not a later before/after subtraction. Hooks can themselves deal
damage, remove actors, or raise an exception. Committed payment/world effects
remain real; an exception produces a failed diagnostic and releases guards.

## Explicit packet EOC

```json
{
  "deal_damage_packet": {
    "target": "npc",
    "source": "u",
    "instigator": "u",
    "bodypart": "torso",
    "cause": "force_blast",
    "multiplier": 1,
    "damage": [
      { "damage_type": "bash", "amount": 12 },
      { "damage_type": "heat", "amount": 8 }
    ]
  }
}
```

`target` is `u`/`npc` (default `npc`); `source` is `u`/`npc`/`none` (default
`u`); `instigator` uses the same choices and defaults to source. Instigator is
metadata; native kill credit still follows the actual source. `bodypart` and
`cause` accept string variables, defaults `torso` and `eoc`. `multiplier` accepts
a number/variable/math, range 0–100. `damage` uses native fixed damage-unit JSON,
up to 32 components. This command uses direct native `deal_damage`, with armor
and defenses, without manufacturing melee/projectile contacts or rolling dodge.

Outputs on the caller dialogue: exact string `damage_transaction` (or `"0"`),
`reason`, `damage_packet_hp_loss`, `damage_packet_absorbed`,
`damage_packet_reflected`. They refer to this command's first packet, even when
reflection creates children. Missing actors, invalid bodyparts and invalid
multipliers refuse dispatch. Legacy `u_deal_damage`/`npc_deal_damage` still
encounter defenses through their existing typed path.

## Bounds, persistence and inspection

Each family allows 64 accepted packets and depth 0–8. A rule/defender identity
can attempt selection once along a branch; mark-before-condition prevents
recursive conditions from retrying themselves. Two reflectors may each respond
once before the return packet proceeds without reapplying an ancestor rule.
These bounds reject excess child packets before HP/resource commitment.

Managed packets allow 32 components and at most 1,000,000 total effective input;
invalid packets are refused. At most 64 matching rule attempts run per packet
across its stages; further matching rules are skipped and mark the report
truncated. Each report keeps up to 64 rule decisions and child handles. Debug → Info → **Inspect executions and abilities** shows active packets and the latest 32
completed reports. It copies recorded decisions without evaluating conditions,
formulas, or RNG. History is diagnostic and is not saved. Only the monotonically
increasing packet counter is persisted under the ability manager; older saves
without it remain readable.

Packets resolve synchronously; saving/loading/session replacement during a
partial transaction is refused. If a callback removes a lethal hit's source,
its native killer pointer is retained until ordinary fatal cleanup finishes;
a save must wait for that cleanup. This preserves native death order and credit.

The `powers_demo` mod supplies `power_demo_shield_focus` to toggle an ordinary
one-hour mana shield, alongside the shared charged force strike. It grants no
mana. See also [ABILITIES.md](ABILITIES.md) and the
[implementation record](../../Audit/powers/07-damage-transactions-milestone.md).

## Owned applications and shared budgets

A rule can require an active `required_application`, and absorption/reflection
can spend a named [custom resource](POWER_OWNERSHIP.md). Application queries
compose independent source magnitudes; available resources exclude managed
windup reservations. This adds owned runtime contributions and shared
accounting without changing the documented armor/HP boundary.

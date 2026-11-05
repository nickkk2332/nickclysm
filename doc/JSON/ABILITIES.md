# Ability lifecycle

An `ability` wraps an existing spell effect or ACTIVATION EOCs in an explicit,
saved action lifecycle. Player, NPC and monster requests use the same admission,
windup, target revalidation, interruption and payment rules. This is opt-in;
ordinary spellcasting, item charges, monster spell specials and `u_cast_spell`
retain their existing policies.

```json
{
  "type": "ability",
  "id": "charged_force_strike",
  "name": "Charged force strike",
  "windup_ticks": [ 2, 3 ],
  "target": "creature",
  "range": 5,
  "move_cost": 100,
  "energy_cost": { "resource": "stamina", "amount": 20 },
  "spell": { "id": "my_force_impact", "level": 1 },
  "on_start": [ "EOC_TELEGRAPH" ],
  "before_release": [ "EOC_RELEASE_GATE" ],
  "on_complete": [ "EOC_RELEASED" ],
  "on_interrupt": [ "EOC_INTERRUPTED" ],
  "on_end": [ "EOC_CLEANUP" ]
}
```

## Definition

| Field | Default / contract |
| --- | --- |
| `id`, `name` | Required identifier and translatable name; supports `copy-from`. |
| `revision` | `1`, integer 1–1,000,000. Bump when changing the contract of an in-progress action. A mismatching saved revision interrupts. |
| `windup_ticks` | `0`; fixed integer or inclusive `[min,max]`, 0–600. One tick is one world second, independent of caster speed. Sampled once on admission, saved, never rerolled by a query or load. |
| `target` | `"creature"`: follow the selected dimension-bound creature identity until commitment. `"point"`: keep an absolute map position. `"self"`: resolve the caster. A point's occupant is not retained as a participant. |
| `range` | `1`, integer 0–60. Checked from the current caster position at admission and release. |
| `line_of_sight` | `true`; check the map's optical trace, including fields. This is obstruction checking, not a promise that the caster perceives the target in darkness. |
| `interrupt_damage` | `true`; actual positive caster HP loss cancels pending/winding requests. Damaging only the target does not cancel a charge. |
| `interrupt_displacement` | `true`; moving the caster from its request origin interrupts. With `false`, range and obstruction still use the current position. |
| `move_cost` | `100`, integer 0–1,000,000, committed once at release. Can create normal action debt. |
| `energy_cost` | `{ "resource": "none", "amount": 0 }`. Also supports `mana` and `stamina`, integer amount 0–1,000,000. Native Character resource costs refuse monsters as `unsupported_resource`. NPC mana is supported; native NPC stamina costs refuse because that pool is synthetic. Named custom resources can support all actor kinds. No avatar fallback. |
| `allow_effect_only` | `false`. Explicit permission for the separate free-effect invocation described below. |
| `spell` | `{ "id": "spell_id", "level": 0 }`; fixed level 0–100, converted to spell XP once on admission. Calls the real caster's `cast_all_effects`. |
| `eocs` | Nonempty release command list; mutually exclusive with `spell`. Uses the ability's caster/target convention. Each command resolves participants again; losing a required participant before a later command stops the remainder. |
| Lifecycle hooks | `on_start`, `before_release`, `on_complete`, `on_interrupt`, `on_end`; arrays of up to 32 ACTIVATION EOC IDs or inline definitions each. |
| `locals` | Up to 128 named instance variables. Initialized from definition defaults, saved, and copied back after lifecycle hooks if their numeric/string type matches. Request context is otherwise an immutable input snapshot. |

Admission checks the ability's native eligibility and fixed costs. The spell
adapter additionally checks the spell's caster and target conditions during real
release. Queries do not run these potentially stateful conditions or evaluate
spell formulas. The native spell effect keeps its own filtering and formulas.

Explicit mana costs debit the current pool directly for avatars and NPCs;
stamina costs use the avatar's real pool. NPC stamina is synthetic in the native
engine, so nonzero NPC stamina costs refuse with `unsupported_resource`. Debug
fatigue exemptions do not waive a declared avatar power cost, and payment does
not clamp away unspent resources above a changed maximum.

The ability owns timing and payment. Its spell adapter does **not** learn the
spell, award XP, roll native casting failure, consume native components, spend the
spell's energy cost, or begin native channeling. Choose the ability's policies
deliberately. Calling a native full spell activity or charged item inside a
release EOC can add that command's own costs; the wrapper does not refund them.
Native casting gates such as verbal requirements or magic-type prohibition flags
are not automatically adopted; express power-specific restrictions in hooks.
Wrapping a spell also retains its native effect's actor limitations; choose
effects that support the intended caster. A shared request contract does not
make every Character-specific spell effect work for monsters.

## Requesting and cancelling

```json
{ "request_ability": { "id": "charged_force_strike" } }
```

Alpha (`u`) is the caster; beta (`npc`) is the selected creature. Both can be an
avatar, active NPC or tracked monster. No fake, hallucinated, dead or unloaded
caster is admitted. Unconsciousness, stun and `CANNOT_ATTACK` refuse admission
and interrupt a windup. Full actions require a free caster: another held ability,
execution control or Character activity refuses them. Different casters may
charge against the same target independently. Targets remain able to act.

The effect writes `ability_instance` (an exact string handle, or `"0"`) and
`reason` into its dialogue. Optional outer `true_eocs` / `false_eocs` run on
admission success/refusal. Admission is a queued request, not successful release;
use lifecycle hooks for eventual completion. EOC calls still copy their dialogue
as usual; use declared ability locals to retain lifecycle outputs.

Player content can request an interactive map cursor:

```json
{ "request_ability": { "id": "charged_force_strike", "targeted": true } }
```

This is avatar-only. Confirming an empty tile for a creature ability refuses the
request; cancelling the cursor returns `target_cancelled` without creating an
instance, sampling a windup or paying a release cost. The cursor itself runs no
spell conditions or spell previews. For a point ability, supply an absolute
location variable instead:

```json
{ "request_ability": { "id": "my_ground_blast", "loc": { "context_val": "marked_tile" } } }
```

`targeted` and `loc` are mutually exclusive. An item EOC can pass its native
`use_location` context as `loc`. If beta is an item, the request also records its
type and original UID as `ability_source_item` / `ability_source_item_uid`.
These are provenance only: there is no continued item ownership, charge
reservation, required-wielded check or automatic item consumption in this slice.

For NPCs, call the request from an EOC with the chosen participants. For monsters,
an existing `monster_attack` with `attack_type: "eoc"` can request it with the
monster as alpha and its native combat target as beta. The special's cooldown
retains its native behavior, including a rejected request; the ability system
does not choose AI tactics or refund an outer caller's costs/cooldowns.

```json
{ "cancel_ability": { "context_val": "ability_instance" } }
```

Cancellation schedules a safe terminal checkpoint, returning `reason: cancelled`
or `not_found`. It cannot revoke a release after commitment. The avatar's cancel
activity action cancels its charge. `u_in_ability` / `npc_in_ability` report an
active full-action control claim, excluding cancelled and releasing requests.

`{ "can_request_ability": { "id": "charged_force_strike" } }` queries native
admission with the same alpha/beta, optional `invocation` and `loc` inputs. It does
not open a cursor, run hooks/spell conditions, evaluate spell formulas, spend
resources or sample windup. Dynamic input variables use their normal EOC
evaluation rules; use side-effect-free inputs when polling from menus or AI.

An explicit free proc is:

```json
{ "request_ability": { "id": "proc_allowed_ability", "invocation": "effect_only" } }
```

Only definitions with `allow_effect_only: true` accept it. It skips windup,
voluntary-action ownership, move cost and ability energy cost. It still validates
participants/target and runs the lifecycle at a safe dispatch checkpoint. The
result identifies this mode and reports no cost commitment.

## Ordering, costs and interruption

1. Pure native eligibility check; on actual admission assign ID/ancestry, snapshot
   context/costs/XP and sample the windup.
2. At a safe checkpoint outside combat/callback dispatch, enter windup and call
   `on_start`. The avatar receives `ACT_ABILITY`; NPC/monster voluntary actions are
   held. Positive moves are discarded while held; existing action debt survives.
   Physiology, fields, attacks by other actors and world time continue normally.
  Range, obstruction, caster status, displacement and affordability are also
  revalidated at each windup tick, so an invalid charge can end before its due tick.
3. On the due world tick, run `before_release`. After arbitrary hooks and spell
   conditions, resolve identities, range, obstruction, status and affordability
   again. A target moving during its own spell validation refuses that release.
4. Commit the fixed release costs synchronously, then dispatch the spell or EOCs.
   There are no callbacks between the final check and payment. Optional
   `reserve_energy` coordinates managed consumers; external native drains can
   still make a claim unaffordable and interrupt the release.
5. Mark effect dispatch complete; run `on_complete`, then `on_end`. An ordinary
   pre-commit interruption runs `on_interrupt`, then `on_end`, once each.

Cancellation and interruption before commitment pay no release cost. Time and
voluntary actions already spent winding up are not refunded. After commitment,
partial effects and paid costs remain real; they are not rolled back. A callback
exception marks the request `failed / callback_failed`, releases its activity,
unwinds guards and propagates the exception. Remaining hooks are not replayed.
Lifecycle hooks are real EOCs: any world changes they perform remain real even
if the release adapter is later refused. They are not a rollback transaction.

Hooks may have missing actors after death/removal; guard actor-dependent commands.
They never receive a replacement avatar. A dead/unloaded selected identity does
not transfer to another creature of the same type on its former tile during
windup. At commitment the spell adapter freezes an absolute aim point. Native
projectile, area and callback ordering govern later contacts; moving creatures
and new occupants can change who is hit. Release EOC commands instead receive
the typed participants again before each command.

Requests made by hooks or effects retain root/parent/depth and wait for a later
pump; they never recursively execute the scheduler. A queued player action claims
an activity before the pump returns to input. A pending child can start at the
next world tick; its saved sampled duration begins when its start is dispatched.
There are at most 128 active instances, 64 accepted requests per root chain and
8 child levels. These budgets survive loading even if the original root finished.

## Context, spell inheritance and results

Ability hooks and release EOCs use caster alpha / target beta. Native
`effect_on_condition` **spell effects keep their existing target alpha / caster
beta convention**, and their existing `spell_*` fields take precedence.
When one of those spell EOCs requests another ability, use the existing
`run_eocs` talker override with `"alpha_talker": "npc"` and
`"beta_talker": "u"` to restore caster/target order for the request.

| Context | Value |
| --- | --- |
| `ability_instance`, `ability`, `ability_root`, `ability_parent`, `ability_depth` | Identity and causal ancestry; large IDs are strings. |
| `ability_state`, `ability_reason`, `ability_invocation` | Current phase, terminal reason, `action` or `effect_only`. |
| `ability_origin`, `ability_selected`, `ability_target_position` | Absolute request origin, selected position and current/released destination. |
| `ability_windup_ticks`, `ability_spell_level` | Saved sampled windup and configured root spell level. |
| `ability_costs_committed`, `ability_paid_moves`, `ability_paid_energy` | Commitment flag and actual ability payments. |
| `ability_effect_completed`, `ability_contact_count` | Adapter dispatch completion and recorded primary contact count. |

The spell adapter binds this context to its specific synchronous spell object.
Spell-instance formulas and caster/target conditions can read it. Declared extra
spells, including `WONDER`, explicitly inherit the context. A different spell
constructed by a callback/proc does not inherit it merely because it has the same
caster. Guards unwind on exceptions. Native random damage, target choices and
other effect rolls occur normally at real release; they are not previewed or
pre-rolled for the windup.

`ability_manager::check` and `inspect` are read-only engine APIs.
`ability_view` contains phase/reason, refs, origin/selection/release point,
deadlines, ancestry, sampled windup, payment and `effect_completed`. The native
contact list carries captured hit, HP loss, spell/channel and other available
facts for primary contacts. The same contact's fatal/living observations merge;
a fatal HP commit is not a promise of final death or absence of death prevention.
Capture is bounded to 64 observations and exposes a truncation flag. It reuses
the existing producer boundaries; arbitrary EOC damage, unrelated callbacks,
deferred/environmental damage and all possible spell side effects are not a
complete per-packet damage ledger. A successfully dispatched effect may miss,
filter out every target or perform noncombat work. Do not treat
`effect_completed` as `hit` or `killed`.
A clear truncation flag means the ability observer's own buffer did not overflow;
upstream producer limits and unsupported channels can still omit contacts.

## Saves, diagnostics and demonstration

Active pending/winding requests are saved in the game's optional `abilities`
section. Older saves need no section. Typed participant identities, dimension,
context, original origin/selection, sampled duration, deadline, progress,
ancestry/root budgets, costs and spell XP survive loading. `on_start` does not
replay for an active windup. Invalid saved transaction states are rejected.
Saving/loading or dimension switching inside an active dispatch is blocked.
Session replacement clears old ownership; dimension travel/game over cancels
active requests. Skipped world ticks interrupt rather than releasing overdue
attacks in a burst. This is a loaded-map scheduler, not offscreen simulation.

Debug → Info → **Inspect executions and abilities** (`X`, also searchable under
Data actions) shows active requests and the latest 32 terminal ability records.
It uses captured state and never reevaluates eligibility, conditions or rolls.
Terminal history/contact diagnostics are transient; saves retain active work.

Enable `powers_demo`, spawn `power_demo_focus` and activate it near a creature;
or spawn `mon_power_demo_brawler`. Both use `power_demo_charged_strike` with a
two-to-three-second telegraph. Interrupt a charge, move a selected target, save
during windup, and inspect the eventual payment and native contacts. NPC EOCs can
request that same ID without a different implementation.

[Power ownership](POWER_OWNERSHIP.md) adds custom resource pools, cooldown groups
and owned sustained applications; [damage rules](DAMAGE_TRANSACTIONS.md) add
typed defenses. Hold/release input charging, source-aware native grants,
summons/areas, general redirects, persistent beams and ability-selection AI
remain later [powers expansion stages](../../Audit/powers/05-expansion-plan.md).

## Typed damage defenses

Native typed damage released by an ability can encounter opt-in
[damage rules](DAMAGE_TRANSACTIONS.md), including exact-capacity shields and
reflection. Packet reports retain explicitly bound ability context, while
ability contact observations continue to report the native primary HP result.

## Shared resources and charges

Energy costs may name [custom power resources](POWER_OWNERSHIP.md). Optional
`reserve_energy` claims the declared budget during windup; `cooldown` and
`charge_cost` select a shared rechargeable group. These policies apply to full
actions. Effect-only invocations retain their explicitly permitted free policy.

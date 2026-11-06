# Source-owned effects, equipment and armor forms

[Power applications](POWER_OWNERSHIP.md) can declare native grants. Each accepted
source has its own application handle and cleanup. Ending one source preserves
other sources and ordinary effects. Existing EOC commands retain their existing
semantics; arbitrary scripted changes do not become owned automatically.

```json
{
  "type": "power_application",
  "id": "armored_form",
  "name": "armored form",
  "duration_ticks": 60,
  "start_cost": { "resource": "focus", "amount": 10 },
  "upkeep": { "resource": "focus", "amount": 3 },
  "grants": {
    "effects": [ { "id": "my_strength_effect", "intensity": 2 } ],
    "equipment": [
      { "item": "my_projected_armor", "slot": "worn" },
      { "item": "my_projected_blade", "slot": "wielded" }
    ],
    "form": { "stow_worn": true, "stow_weapon": true }
  }
}
```

Apply/end/dispel through existing application EOCs. The same definition can be
requested by an ability, item, NPC or monster EOC. Effect targets may be real
loaded avatars, NPCs or monsters; equipment and forms require a Character target
(avatar or NPC). The owner can be any supported actor. Grants require loaded-map
participants and use the application's finite/indefinite lifetime, upkeep,
interruption, parent ownership and world-transition policy.

## Definition and admission

`grants` is optional and supports `copy-from` inheritance. Specified `effects` or
`equipment` arrays replace the inherited array; unspecified fields retain their
inherited values. `form: false` removes inherited form behavior.

| Member | Meaning |
| --- | --- |
| `effects` | Up to 16 entries: `id`, optional `bodypart` (untargeted by default), `intensity` (default 1). |
| `equipment` | Up to eight entries: `item`, `slot` (`worn` default, or `wielded`). At most one wielded entry. |
| `form` | Optional object claiming an exclusive equipment form on the target. |
| `form.stow_worn` | Default true when a form object is present. Stow current top-level worn items. |
| `form.stow_weapon` | Default true when a form object is present. Stow the actual wielded item, even when a martial style forces unarmed combat. |

Effect intensities are fixed integers, 1–1,000,000 and within the native effect's
maximum. Bodypart grants require an existing part; native main-part normalization
applies. Duplicate effect/bodypart cells, including normalized aliases, refuse.
Immunity, `CANNOT_GAIN_EFFECTS`, bodypart immunity and native effect blockers apply.
`knocked_out` is not grantable; use the physiological knockout interfaces.

Equipment definitions must be valid nonconsumable armor/melee items. Charge stacks,
food, guns and tools are excluded in this first adapter; worn entries require
armor. Native wear/wield eligibility is checked during materialization. No items
are constructed by admission or inspector queries, so previews do not roll item
variants, assign item UIDs or spend resources. Admission cannot promise that every
piece of a complete outfit will fit after arbitrary callbacks.

Forms refuse conflicting active/pending-cleanup forms on the same target, native
removal locks and stowing another source's projected equipment, including nested
items. Ordinary equipment grants can coexist if native clothing/slot rules allow.
A standalone weapon grant requires an empty actual wielded slot. Queued starts
revalidate claims and removal permissions before payment.

Start cost commits before materialization. Real gear is stowed first, then effects
and equipment are installed in definition order, before application `on_start`.
Native callbacks may change or remove participants; subsequent grants revalidate
source and target. If materialization partially fails, the source ends, its grants
are cleaned up, and original gear is returned. Committed costs and world changes
are not refunded or rolled back.

## Native effects and independent sources

For each effect/bodypart cell the engine preserves an ordinary native component
and a set of owned fixed-intensity contributions. Visible intensity is their sum,
capped by the effect type's maximum. Contributions above the visible cap are
retained, so removing one source cannot erase another source's hidden strength.
Native flags, modifiers and effect processing see the aggregate.

The ordinary component keeps its own source, intensity, duration, permanence and
native decay behavior. An ordinary effect can expire underneath a held grant; a
new ordinary application can also be added while grants are active. Ending the
last grant restores the remaining ordinary component rather than an old snapshot.
Effects shared through copies detach their ownership state before mutation.

Native duration/intensity edits operate on the ordinary component, when present;
they do not rewrite held contributions. Native `remove_effect`/`clear_effects`
remain whole-effect cures, removing ordinary and owned components together.
A sustained application does not recreate a cured grant on its next upkeep tick.
End the exact application to remove only that source.

Owned contributions do not independently run the same native effect processor:
it processes the combined effect once. The aggregate uses a live ordinary source
when present, otherwise the earliest owned source. Native `effect_source`
limitations, including monster resolution and per-turn damage attribution, remain;
this is not separate damage/kill credit for every contributing caster.

Cancellation invalidates application contribution queries immediately. Physical
native effect/equipment changes settle at safe application boundaries, before
terminal application hooks. Parent cleanup also removes descendants' native
grants before the parent's end observer. This does not rewind a native attack
or action already being resolved.

## Projected equipment and ordinary contents

Granted gear is marked with its exact source application and dimension. Native
armor, melee statistics, item enchantments and wear/wield callbacks apply.
Ownership follows ordinary copies and transfers, including drops, loans to other
loaded characters and vehicle cargo. Cleanup removes matching loaded gear rather
than every item of the same type. Transferred copies expire with their source.

Native item processing removes orphaned/expired projected gear after it is loaded
again. It preserves ordinary inserted contents through native spilling; minted
nested items expire with their source. Normal crafting-component, salvage and
disassembly paths exclude projected equipment. Native transfers/spilling retain
their ordinary item identity rules; they may copy items and renew UIDs.

The marker does not automatically own arbitrary scripted side effects or new
products created by transformations, break replacements, construction or other
special consumers. Those need their own adapters. The supported grant itself
has a finite source lifetime; authored content should not depend on recovering
permanent materials from it. Native effect/equipment effects already committed
by a callback are not reversed by dispelling the source.

## Stowed gear and restoration

Forms move actual original items into saved source-owned storage, preserving
root and nested UIDs, damage, variables, contents and sidedness. They do not clone
or recreate the original outfit. Stored gear contributes no inventory access,
carried weight or encumbrance while the form holds it.

Stowing uses native bulk item-takeoff callbacks and cache updates. It is not the
ordinary manual takeoff/unwield action and does not manufacture those action
EOCs. Materialized and restored items use native wear/wield callbacks. Application
lifecycle EOCs are the explicit observations for entering and leaving a form.

Stored items run native item processing at most once per observed world second,
including their ordinary contents. Destroyed stored items are not recreated;
remaining contents spill normally. There is no bulk catch-up or offscreen item
simulation. Storage is outside the ordinary worn inventory and is not another
physical container that attacks can target.

On ending, the source's native effects/equipment are removed and original garments
are offered to native wear rules in stored order; the original weapon is offered
to an empty wielded slot. New or incompatible equipment is preserved. An original
item that cannot be re-equipped is placed at the target's feet through native
placement, with ordinary terrain/overflow and item-copy/UID consequences.
Restoration does not force body compatibility or overwrite an occupied slot.

When the exact target is unavailable, original gear is placed at its last observed
position once that position is loaded in the same dimension. Another actor at
that tile never receives the outfit automatically. If the location is unavailable,
a saved cleanup record retains the real items. No-gear cleanup can finish without
waiting for a location. There are at most 160 retained records including pending
cleanup and up to 32 transient terminal records; a form stores at most 64 garments
and one wielded item.

Transfers commit before restoration callbacks. If a callback removes an item or
throws, already transferred items are not recreated. Remaining cleanup can resume
at a later safe pump; terminal application hooks are attempted once. Saving,
loading or changing worlds from inside grant/native item dispatch is refused.

## Persistence, inspection and examples

Grant records save with power accounting; effect layers save with the native
effect, and stored items use native item serialization. Records belong to the
active character's ability-runtime save. A world-shared registry coordinating NPC forms
across different player saves is outside this milestone. Active saves resume
without replaying grants, start payments or clothing creation. Legacy saves need
no grant section. Missing/incompatible source definitions end the source and
clean up its grants; bump application `revision` when changing grant semantics.
Unloaded actors' orphaned layers are reconciled when they return to active
processing, while native projected-item processing handles detached gear.

The execution/powers inspector lists source/target identities, application,
active/cleanup/terminal state, effect/equipment counts, stored item identities/count
and restoration/drop receipts. Inspection is read-only and consumes no RNG.

Enable `powers_demo` and spawn:

- `power_demo_might_grant_focus`: independently owned strength buff.
- `power_demo_blade_grant_focus`: projected blade requiring empty hands.
- `power_demo_form_grant_focus`: one-second charged armor form, strength and blade,
  followed by return of the original outfit. Native removal locks may refuse it.

Activation requests a normal self-targeted ability and spends 100 moves; sustained
focus payment belongs to the application. Activate again to end your own matching
applications. See [grants.json](../../data/mods/POWERS_DEMO/grants.json).

This milestone supplies equipment/stat forms. Anatomy/species replacement,
source-owned mutation/loadout changes, arbitrary effect stacking policies,
parameterized grant formulas, gun/tool grants, offscreen simulation, and ownership
of every downstream item product remain separate adapters.

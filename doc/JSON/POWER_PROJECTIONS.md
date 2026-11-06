# Persistent power projectiles, beams and areas

`power_projection` is a saved, owned world object used by
[abilities](ABILITIES.md), EOCs and [power accounting](POWER_OWNERSHIP.md).
It supports real loaded avatar, NPC and monster owners. It does not change the
travel or accuracy of native bullets or existing instantaneous spell attacks.

```json
{
  "type": "power_projection",
  "id": "example_bolt",
  "name": "travelling force bolt",
  "kind": "projectile",
  "range": 10,
  "speed": 1,
  "strength": 20,
  "duration_ticks": 20,
  "damage": [ { "damage_type": "bash", "amount": 20 } ]
}
```

Definitions support `copy-from`. Time fields count world seconds, not actor moves.
An object is admitted immediately, then begins at a safe action boundary. Travel,
upkeep and payload pulses run at most once per world second. A zero-charge object
first travels or pulses on the following second. Charging objects release on the
saved deadline and can pulse that second. Expiry includes the charge period.

## Definition fields

| Field | Default | Meaning / bounds |
| --- | --- | --- |
| `revision` | 1 | 1–1,000,000; change to invalidate incompatible saved objects |
| `kind` | `projectile` | `projectile`, `beam`, `area` |
| `range` | 10 | 1–60; maximum selection distance and beam length / total projectile travel |
| `radius` | 1 | 0–10; area Chebyshev radius on one z level |
| `speed` | 1 | 1–10; projectile tiles per pulse |
| `strength` | 10 | 1–1,000,000; finite contest capacity, separate from damage |
| `duration_ticks` | 30 | 1–1,000,000; finite lifetime from admission |
| `charge_ticks` | 0 | 0–600 and less than duration |
| `interval_ticks` | 1 | 1–3,600; spacing between travel/payload/upkeep pulses |
| `max_per_owner` | 8 | 1–128; per owner and definition, including admitted pending objects |
| `track_target` | false | Follow the chosen identity; unavailable for areas |
| `follow_owner` | false | Area/beam origin follows its owner; unavailable for projectiles |
| `intercepts` | false | Area can contest hostile beams/projectiles; areas only |
| `contest` | true | Participate in strength contests |
| `line_of_sight` | true | Clip area cells by terrain LOS from the center |
| `affects` | `hostile` | `hostile`, `friendly` (includes owner), or `all` |
| `bodypart` | `torso` | Native bodypart for typed damage |
| `damage` | empty | Native typed damage packet, up to 16 components |
| `start_cost` | none / 0 | Resource and amount committed at admission |
| `upkeep` | none / 0 | Resource and amount paid before each pulse |
| `interrupt_damage` | false | Cancel on observed positive native damage to the owner |
| `interrupt_displacement` | false | Cancel if owner leaves its original position |
| `interrupt_incapacitated` | false | Cancel for unconsciousness, stun, sleep or narcosis |
| `tags` | empty | Up to 32 tags for selective dispelling |
| `locals` | empty | Up to 64 declared finite numbers, strings or positions |

Costs use `{ "resource": "my_pool", "amount": 5 }`; native resources and custom
pools follow shared reservation rules. Start payment remains spent after failure.
There is no implicit move or charge-group cost: use an enclosing full-action
ability for those. Upkeep failure ends the object before its payload. There is no
additional upkeep during charging. Damage components must be finite, nonnegative
and at most 1,000,000 each; the effective packet total is bounded to 1,000,000.
Barrel scaling is unsupported. Invalid hook/resource/bodypart IDs are diagnosed
when data is checked.

## Creation and ownership

```json
{ "create_power_projection": { "id": "example_bolt", "owner": "u", "target": "npc" } }
{ "create_power_projection": { "id": "my_domain", "target": "u" } }
{ "create_power_projection": { "id": "example_bolt", "loc": { "context_val": "aim_point" }, "strength": 30 } }
```

`owner` defaults to `u`, `target` to `npc`; both select real dialogue roles. `id`
accepts a string or string variable. `loc` is an absolute-position variable.
Without `loc`, the target position is selected, or the owner position when no
real target exists. Self-point selections are valid only for areas. Admission
requires an in-bounds point on the owner's z level within range. Beams and bolts
remain horizontal; a following area can follow its owner to another loaded z
level. `track_target` refuses an explicit point: it follows the saved identity
and never adopts a replacement at its old tile. `strength` optionally overrides
initial capacity using an integer expression within the definition bounds.

`targeted: true` opens avatar look-around selection, mutually exclusive with
`loc`; cancellation produces `target_cancelled` without admission/payment.
Other owners refuse interactive targeting. Use an ability's creature targeting
for ordinary player/NPC/monster combat invocation.

Creation sets `reason` and an exact **string** handle `power_projection` (`"0"`
on refusal). Accepted creation sets `reason` to `accepted`. Refusals include
`invalid_definition`, `owner_unavailable`,
`target_unavailable`, `tracked_point_conflict`, `invalid_geometry`,
`invalid_strength`, `invalid_application`, `instance_budget`, `context_budget`,
`ancestry_budget`, `terminal_parent`, resource admission reasons and world/session
transition restrictions. See the runtime for the exhaustive reason set.

An optional `application` string or string-variable handle binds the projection
to an existing owned power application of the same owner. It defaults to `"0"`;
arbitrary incoming context does not implicitly choose a source application.
Ending/invalidating that application immediately disables the projection's live
contribution and ends it at the next safe boundary. Descendants inherit their
parent's actual source application and cannot switch away from it.

```json
{ "end_power_projection": { "context_val": "saved_projection" } }
{ "dispel_power_projections": { "owner": "u", "id": "my_domain", "tag": "ward" } }
```

End acts on an exact handle. Dispel defaults to owner `u`; optional `id` and `tag`
filters are combined. Other owners' objects remain independent. End sets `reason`
to `dispelled` or `not_found`; dispel also sets numeric
`power_projections_dispelled`. Cancellation disables contribution immediately,
while terminal observations wait for a safe pump.

## Geometry, contact and contests

A projectile advances toward the selected point, or its tracked target, one to
`speed` cells each pulse. It stops at impassable terrain/vehicles, the first real
body other than its owner, its destination, or its total range. An unaffected body
still blocks it. Beams occupy the ray excluding their origin, stop at the first
impassable cell and can damage multiple qualifying actors once per pulse. Neither
bashes terrain automatically. An `on_hit` EOC can author world changes explicitly.

Areas have independent enter/stay/exit membership, reconciled at safe action
pumps. Movement into and out of an area between observations is not simulated as
a swept path. `on_stay` and damage occur only on pulse deadlines. Membership and
pulse target lists cap at 64 identities in stable order; the inspector exposes
truncation. Pure area contribution queries use live geometry and relation, so a
displaced target cannot retain a stale defense until the next membership event.
Overlapping areas remain independent. They do not install native effects/items
or automatically reverse arbitrary EOC side effects.

Active hostile-owner projectiles/beams contest on overlapping cells; an area
participates only with `intercepts`. Area/area pairs never contest. Same-owner and
non-hostile owners' objects coexist. Charging/pending shapes are telegraphs and
do not contest. Overlaps settle before pulse damage, and moving projectiles check
each crossed cell. In stable handle order, each pair subtracts the other's
current strength; equal capacities end both. Surviving damage scales by remaining
strength / initial strength. This is deterministic and consumes no RNG. It does
not model material-specific energy interactions or sustained beam pushing.

Payloads call native typed damage with the actual owner and bodypart. Existing
armor, damage rules, shields, reflection and damage events apply; Character kill
credit keeps the owner. Contact records report actual net HP loss, including
synchronous callback changes. These direct-contact packets have no automatic
accuracy/dodge roll or primary melee/gun execution channel. Authors can invoke
executions explicitly from their EOCs.

## Hooks and context

`on_start`, `on_release`, `on_enter`, `on_stay`, `on_exit`, `on_hit`, `on_clash`
and `on_end` each accept up to 16 named or inline ACTIVATION EOCs. Owner is alpha
(`u`); contact/member is beta (`npc`) when present. Terrain/end/clash hooks often
have no beta. Lost owners or departed members do not become a fallback avatar:
guard role-dependent code with `projection_owner_present` and
`projection_target_present`. Exit hooks describe prior observed membership even
when that identity is no longer available.

Context includes exact string handles `power_projection`, `projection_root`,
`projection_parent`, `projection_application`, `projection_other`; string
`projection_definition`, `projection_state`, `projection_reason`,
`projection_event`; absolute `projection_position`, `projection_origin`,
`projection_selected`; numeric `projection_strength`, `projection_tick`,
`projection_hp_loss`, and role-presence flags. `projection_hp_loss` is the current
contact observation, not total history. Hit events include `hit`, `terrain`,
`blocked` and `reached`; clashes expose the other projection handle, not a victim.

Incoming context is copied. Only declared `locals` of their original type are
copied back after a hook. Local names cannot be `power_projection` or start with
`projection_`. Children created inside hooks are queued, not recursively pumped.
Each family allows 64 total admissions and depth 8. Ancestry accounts for recursion;
it does not tie a child's physical lifetime to an already impacted parent.
Terminal parents cannot recreate their family. There are at most 128 nonterminal
objects, 32 terminal inspection records and 64 transient contact receipts.

On failure, committed costs, damage and world changes remain. Terminal state and
membership removal commit before exit/end hooks; each terminal observation is
attempted once. The exception propagates after cleanup attempts. Native damage
exceptions retain the observed HP loss in contact history. Hook-driven source
removal/cancellation stops later payload targets. Save/load and world replacement
inside dispatch are refused.

## Queries, defenses and presentation

```text
u_power_projection('my_beam', 'field': 'charging')
n_power_projection('my_beam', 'field': 'strength')
u_power_area('my_domain', 'field': 'strength', 'owner': 'any')
```

Owned queries count pending/charging/active valid objects; fields are `count`
(default), `strength` (sum), and `charging` (count). Area queries count active
matching areas affecting the queried actor; fields are `count` (default) or
`strength` (sum), and `owner` filters are `any` (default), `u`, `npc`. Empty ID
matches all definitions. Missing actors return zero. These reads do not run hooks,
create accounts/identities or consume RNG.

[Damage rules](DAMAGE_TRANSACTIONS.md) can use `required_area: "my_domain"` to
require a live affecting area. For example, an absorb action with resource `none`
and maximum `u_power_area('my_domain', 'field': 'strength')` adds the capacities of
independent wards. That maximum is a per-packet limit; absorbing damage does not
spend projection strength. Use a damage-rule resource for a consumable shield.
Projection strength is depleted by contests. The gate preserves the rule's other
conditions.

The execution/ability inspector lists shape cells, saved identities, membership,
charge/pulse/expiry deadlines, ancestry/source application, paid amounts, remaining
strength and net HP loss, plus recent contact/clash receipts. Map overlays show
visible charging paths and active bolts/beams/area outlines. Curses uses `+`, `*`,
`|`, `.`; tiles reuse existing field sprites as draw overrides. Real displayed
fields take precedence. These overlays add no world fields, lighting or mechanics.

## Saves, examples and current limits

Objects save in the optional ability-runtime `power_projections` section. Older saves
load with no projections. Identity, locals, membership, capacity, paid totals,
ancestry budgets and deadlines survive without replaying start/enter/payment.
Skipped world seconds end objects rather than catching up attacks or payments.
Owner loss, unavailable geometry, definition revision changes, expiry and session
transitions end them. There is no offscreen owner or projectile simulation.

Enable `powers_demo`. Spawn `power_demo_bolt_projection_focus`,
`power_demo_beam_projection_focus`, `power_demo_domain_projection_focus`, or
`power_demo_interceptor_projection_focus`. The first two request paid abilities;
the others toggle only your own aura/bubble. `mon_power_demo_beam_caster` uses the
same bolt/charged beam definitions through native ability selection. Watch a
charge, interrupt the caster, intercept a bolt, overlap two friendly domains,
save during charging, and inspect results. See
[projections.json](../../data/mods/POWERS_DEMO/projections.json).

This slice does not add arbitrary shape authoring, vertical trajectories, ordinary
bullet interception, continuous swept membership, offscreen simulation, moving
terrain constructs, or universal NPC planning. [Application grants](POWER_GRANTS.md) provide explicit
source-owned native effects and armor/melee equipment; arbitrary area hook
changes are not automatically converted into those grants.

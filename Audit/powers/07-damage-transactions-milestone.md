# Powers expansion: damage transactions and defenses

This is the next implementation slice after the
[shared ability lifecycle](06-ability-lifecycle-milestone.md). It addresses the
core of COMBAT-01 with synchronous typed damage transactions, rather than a new
casting engine. The original audit's pinned findings remain historical.

The implemented [JSON contract](../../doc/JSON/DAMAGE_TRANSACTIONS.md) provides:

- `damage_rule` selection by stage, ordered priority/ID, defender effect/kind,
  source kind, bodypart, damage types, spell and actual-dispatch condition.
- Before-armor and after-armor edits: scaling, caps, cancellation, pre-armor
  conversion, exact-capacity mana/stamina absorption, bounded free absorption,
  and attributed reflected packets.
- Validated EOC mutation outputs and committed observers, with explicit
  defender-alpha/source-beta actors and no avatar fallback.
- Whole mixed packets sharing current resource budgets, native per-type
  rounding/multipliers, adjacent validated edit/payment and actual HP capture
  before events can heal or apply secondary damage.
- Root/parent/depth, original source and per-rule resource ledgers, bounded
  recursion/history, actor retention/revalidation and partial-save guards.
- An explicit `deal_damage_packet` EOC, opt-in spell/ability metadata propagation,
  read-only inspector rows, and a runnable mana shield focus in `powers_demo`.

Resource accounting also corrects two traps in native modifier helpers:
NPC stamina is a synthetic fixed value and its fatigue modifier skips payment;
resource modifiers can also clamp away unspent capacity after a maximum changes.
Damage/ability costs now refuse NPC stamina explicitly, use NPCs' real mana,
and debit supported pools directly. NPC and above-maximum regressions cover this.

The acceptance example is concrete: ten mana absorbs ten from a 25-point eligible
packet, passes fifteen onward, and cannot be spent again on a later component or
hit. Monsters can use a bounded free shell; Character-only pools fail explicitly.
Two reflectors cannot bounce damage indefinitely. A removed lethal source stays
alive long enough for native fatal cleanup to use its real killer attribution.

## Implementation boundaries

The transaction sits inside native `Creature::deal_damage`, bracketing
`absorb_hit` and preceding per-type side effects/pain/HP. Pre-hit events and
projectile non-damage payloads remain outside that boundary. Post-armor budget
is not a guarantee of final HP loss: native innate special behavior, critical
overkill limits and available HP still apply. Armor wear already happened for
after-armor shields. Raw HP mutation bypasses typed rules.

Reflection is a new packet with new armor resolution; it is not a physical
projectile bounce, beam clash, interception field or new execution producer.
Source/instigator refs are recorded without rewriting every existing native
secondary-damage path. Hooks can cause real world effects, and their exceptions
do not roll back previously spent resources, damage or arbitrary commands.

The lifecycle manager owns the damage manager through its existing pimpl;
no new `game`, `Creature` or `spell` layout is required. Reports are ephemeral;
only the counter is saved. Required effects use native persistence. There is no
new independent application ownership or custom resource model in this slice.

## Validation

Native integration coverage lives in
[damage_transaction_test.cpp](../../tests/damage_transaction_test.cpp). It covers
Character/monster defenses, mixed packets, multipliers, resource exhaustion,
armor and conversion, ordered rules, validated outputs, observers, nested EOCs,
reflection/credit, explicit packet roles, spell/ability context, healing and raw
secondary writes, actor removal, killer retention, exceptions, bounded ancestry,
read-only inspection, counter persistence and schema refusal.

Final validation on 2026-11-05:

- Combined native-message regression: **395 cases / 399,795 assertions passed**,
  including all 33 new damage/resource cases and the 362 existing ability,
  execution, knockout, fatal-hit, projectile, combat, effect, NPC sleep, monster
  attack/grab, weakpoint and affected audit cases. Seed 41723; `dda`,
  `executions_demo`, `powers_demo`, and automatically supplied `test_data`.
  No initialization or test errors.
- Headless GCC C++17 engine archive, native-message test executable and explicit
  `cataclysm` target built with project warnings treated as errors. The final
  command-line `--help` smoke check passed.
- All 18 changed/new C++ formatting checks, three JSON formatting checks and
  translation extractions, five local document links and all 29 reviewed file
  hashes checked. Source/data stayed frozen throughout the passing run; this
  validation record was completed afterward.
- Focused selection for a standard test build:
  `./tests/cata_test '[damage_transactions]' --mods dda,executions_demo,powers_demo --rng-seed 41723`.

This was a selected native-message suite and headless smoke check. The full
repository suite and interactive graphical playtesting were not run.

## Remaining expansion work

Resource/cooldown ownership, a real NPC stamina backend, custom pools and
held/sustained powers are the next
foundation. Source-aware independent applications, protection of another actor,
general redirection, persistent contestable projectiles/beams, areas, summons,
physical props, transformations and AI selection remain separate work. This
slice does not close all of COMBAT-01 or all resource/ownership findings.

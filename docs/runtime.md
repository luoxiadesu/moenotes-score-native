# Deterministic runtime contract

`run_runtime` consumes prepared master/chart data, a resolved five-slot deck,
a scenario and explicit weighted branch choices. A missing choice returns
`NeedDecision` with the exact integer branch weights. No seed is required and
no Python process is used by the library. `compile_runtime` prepares immutable
card rules, timing, lottery tables and note/event indices once. Its returned
kernel runs independent per-path state and can be shared by analysis callbacks;
the prepared data must outlive it.

The first decision is a uniform permutation of all 120 ordinary member orders.
Probability conditions preserve master AND-condition order; an earlier failed
condition prevents later random decisions. Luck tables preserve integer weights.
Identical outcomes merge only after minimum-result support has redistributed
excluded weights over the original eligible table entries.

## Ideal play and time

Ordinary play is Perfect. Just missions use Just only where the assist-zero
master timing table permits it, with an exclusive mission-end boundary. The
same-ms simulator batch runs before recovery and skill conditions. Recovery is
visible to ordinary and Gekisou life conditions in the same update. The auxiliary
Solo judgement remains Perfect when Gekisou converts the primary judgement.

The declared update clock is 1 ms. An ordered event set skips updates whose
state cannot change; it does not allocate a duration-sized array. Ordinary skill
ends, Gekisou duration detection, previous-update Combo/Rush feedback, pending
Luck tickets and score-rank feedback preserve their next-ms observations.
Gekisou Complete is range end plus the master maximum after-window plus 500 ms;
Finish occurs one millisecond later. Ranges whose controller windows overlap
are rejected.

Counter commands preserve their original command timestamp and judgement
sequence. A duration removal observed after a same-time chord removes the
expired bonus from only the final chord member. Sustained Combo activation
similarly backdates to the final previously observed chord member. Cumulative
support scores use raw current-simulator Just counts or previous-update
Gekisou Combo counts, as appropriate. Native float32 conversions and
quantization are retained, including 45 basis points quantizing to 449 raw units.

## Supported effects

- Ordinary member: 2000 note, 2002 Combo, 2004 judgement factors; life conditions.
- Ordinary support: 15000 extension, 3001 recovery, 12006 conversion, 2000 rank
  score factors; attached-band conditions.
- Gekisou member: 12000 Combo, 13000 Just, 13002 cumulative Just, 11001 Luck gauge
  factor, 11003 instant gauge, 11002 lottery-result points; section, sustained
  Combo, life and probability conditions.
- Gekisou support: 2000 Rush score, 2001 cumulative score, 11005 minimum result,
  11003 lottery-result gauge, 13005 Just conversion. Timing expansion (4004),
  below-Perfect conversion (12006), and bad-judgement Combo protection (12004)
  are validated as ineffectual for the declared ideal input.

Unknown selected effects and unsupported condition/trigger combinations fail.
Rows not referenced by a selected card do not create runtime behavior. The
current master has ordinary rank-support rows but no card referencing them;
synthetic reference tests exercise this implementation. Ordinary effect 3003
is rejected if selected; no current card references it.

## Evidence and limits

The retained research harness compares C++ against independently executed
Python modules over 2,508 scenarios: every current member and skill level,
every current support and rank, both probability extremes and Luck extremes,
life thresholds, band mismatch, 100 mixed decks with two branch paths each,
two synthetic future rank-support references and all 120 skill-order
permutations, plus 488 complete real-chart paths for song 0069 expert
(874 judged notes across 11 operation types). Compared fields are all
shared/primary factor commands, every judgement and life value, mission totals,
and every Combo snapshot. Real-chart cases also compare primary and Solo
scores, every range score and the final rank-1 total. Another 244 real-chart
paths use complete randomly formed decks, all skill/support ranks and recorded
weighted random choices. These cover 15,615 decisions, both probability outcomes
and 440 Rush entries/exits. Forty additional complete-deck paths override all
three missions to Luck, checking range reset and cached-next behavior. All
comparisons passed.

The 488 extreme and 244 random real-chart fixtures also pass against both EN
and HK/TW/MO master data (1,464 additional executions). All 15 runtime rule, Luck, timing and settings
tables have identical canonical JSON hashes across KR, EN and HK/TW/MO in the
retained snapshot. This does not assert that arbitrary future regional data are
equivalent.

An additional independent ARM64 probe executes native CountReplay
(`0x55d66b8`) and cumulative Just (`0x55d8124`) for 540 range replays.
It derives commands from the master static rules, uses collection-access
shims, and supplies 270 member cases that match C++. Dynamic Combo activation
is excluded from this component proof.

The small standalone C++ suite additionally checks effect expiry at chords,
causal Combo activation, natural Just, pending Luck, ordered short-circuit
probability conditions, duplicate lottery outcomes and malformed inputs.

These are migration and recovered-component checks. The complete new joint
scheduler has not been independently compared to on-device LiveExecutor and
SkillExecutor execution. Runtime diagnostics explicitly retain
`joint_native_validation: "pending"`. Counters advance incrementally for ordinary note arrivals. A changed backdated
count command replays only its section history to retain the recovered timestamp
and judgement-sequence semantics. No per-frame diagnostic trace is stored by
default.

## Copyable continuation

`RuntimeMachine` is a separate stateful execution path built from the same
immutable compiled rules as `run_runtime`. It stores the completed event prefix,
current controller state, remaining event queue and exact output history.
`advance_until_decision()` commits deterministic events and parks at the next
weighted decision. Copying a parked machine and calling `choose()` creates an
independent branch. A random choice inside an ideal update retries only that
uncommitted update from its checkpoint; it never reruns completed song events.
This includes multiple lottery and cached-next decisions in one chord/update.

The state key is a versioned portable binary representation of every mutable
field, including command/note history, all counters and effect handles, future
events, pending choices and weights. Controller factors are stored as integer
raw units; float32 intermediates do not survive an event checkpoint. No pointer
or decimal float text occurs in the key. Immutable prepared/master/deck data
provide the owning session's namespace: keys must not be interned across
kernels. Copies share only immutable rule objects, so effect-rule pointers remain
valid. Progress strictly increases after each selected branch.

The first implementation retains full history in the key. This intentionally
limits state merging; it does not assume historical equivalence without a proof.
The retained-byte estimate includes state capacities and a conservative
allowance for fixed diagnostics; immutable compiled data is shared kernel memory.
It supports exact DAG probability propagation without claiming that all decks
will converge within the default budget.

All 2,508 Python oracle paths pass through the continuation, with cloning and
original-state immutability checked at every random decision. Dedicated tests
check same-event rollback, completed-prefix reuse, twin-instance key equality,
strict progress and cached-next branching. The original compiled whole-path
kernel remains available for efficient independent sampling.

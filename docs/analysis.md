# Analysis and proof contract

The analysis kernel maps a prefix of random choices to either the next finite
integer-weight decision or a complete score for every requested rank condition.
Zero-weight outcomes are excluded; equal outcomes may only be merged after their
weights have been summed. The same prefix must produce the same result.

## Exact enumeration and witnesses

The analyzer supports an explicit depth-first prefix frontier and a probability
DAG over immutable kernel states. Both retain normalized arbitrary-precision
rational probability. Prefix nodes store the full choice prefix. DAG states
store a representative witness prefix, a complete canonical state key and a
strictly increasing progress value.
Finished leaves add probability-weighted total, auxiliary, section and optional
contribution scores to rational accumulators. Only an empty frontier and exactly
unit explored probability produce an exact mean. The rational numerator and
denominator are authoritative; binary64 display endpoints are rounded outward,
so a nonrepresentable exact mean such as 1/3 can have adjacent unequal endpoints.
No low-probability cutoff is used. Cancellation, budget expiry and memory exhaustion retain unresolved mass.

Every minimum/maximum includes a complete legal path, including its choices and
all component scores. Separate rank conditions can have different witnesses.
An extreme is exact only after exhaustive enumeration, or when a witness meets
its proven bound. A sampled extreme is only a witness; observing no better sample
does not prove an optimum.

An optional internal prefix-bound callback can supply nested proven bounds.
The analyzer checks interval nesting and checks finished leaves against their
prefix bounds. The global minimum lower bound and maximum upper bound aggregate
all unresolved nodes. This can prove one extreme while the mean is unresolved.
It does not drop a node still needed for exact expectation.

The probability DAG processes states in increasing progress order. Since every
transition must increase progress, all predecessor masses arrive before a state
is evaluated. Equal progress and full canonical bytes intern to one node and
exact incoming masses are added. Each interned state is evaluated once, including
its terminal score. Different histories can merge only when the complete state
key, including accumulated scores, is identical. A hash is not used to decide
equality. Every edge is a separate bounded work step; partially expanded nodes
retain the unexpanded branch cursor on budget or memory exhaustion.

This is exact probability propagation over equivalent states, with separate
minimum/maximum witnesses per rank. Prefix DFS remains an independent fallback.
The full-history runtime key is intentionally conservative and can have a low
merge rate; it does not promise that large Luck trees finish within a short
budget. Optimized score-state reduction and branch-and-bound pruning remain
additional work.

## Score envelope

Preparation computes a nonnegative integer score range before analysis. For each
selected ordinary effect, it bounds all simultaneous positive additions; score
rank triggers use the maximum possible rank increase count. For each cumulative
or Rush support, at most one effect value is active at a time. Cumulative support
can replace its value at every integer millisecond of a Gekisou controller
range; no assumption about dynamic condition convergence is required. Rush
transition counts are bounded by total possible gauge inflow divided by the
smaller gauge maximum, including gauge skills and all notes.

An addition/removal pair uses equal opposite representable float32 increments.
The exact partial factor sum lies in a known interval. If `M` bounds the absolute
exact partial sum, `n` bounds the number of rounded updates, and `u = 2^-24`, the
absolute arithmetic residual is conservatively bounded by
`M * n*u / (1 - n*u)`. The implementation rejects `n*u >= 0.25`, widens arithmetic
outwards, and requires cancellation residuals to leave the final combo and note
multipliers positive. It checks quantized raw factors before narrowing.

The envelope then evaluates each note with monotone upper operands in the same
float32 multiplication/division order as the deterministic calculator, including
the second float32 cast after integer flooring. It sums section envelopes and
integer ranking bonuses. Inputs are rejected if this envelope cannot prove the
entire path fits the supported nonnegative int32 score domain. This is a
conservative acceptance rule: rejection does not prove the actual optimum would
overflow. Challenge skip has a single deterministic calculation and uses its
actual score as both bounds.

## Independent estimates

Sampling is separate from the enumeration search. Every sample is one complete
path drawn according to the original conditional probability weights. Search
leaves and extreme witnesses never enter the sample mean. Integer rejection
sampling avoids modulo bias and standard-library distribution differences.
Reported estimates use binary64 with floating contraction disabled. Total mean
and variance sufficient statistics use exact integers; auxiliary means remain
binary64 estimates.

The default target is a 95% simultaneous confidence guarantee for all requested
**total** means, with a relative half-width target of 0.1%, floored at one point.
Auxiliary, section and contribution means share the same samples but have no
separate confidence interval in this version. Means and intervals are published
at predetermined sample counts (32, 64, 128, ...); between checkpoints the last
paired checkpoint mean and interval are retained. Before the first checkpoint,
the full proven score range is reported with the provisional sample mean.

For rank count `r` and checkpoint number `k >= 1`, the failure allocation is
`(1-confidence) / (r*k*(k+1))`. Its sum over every rank and every checkpoint is at
most `1-confidence`. At each checkpoint with `n` samples, sample variance `v` and
proven range width `R`, the two-sided interval uses the conservative empirical
Bernstein radius

```
sqrt(2*v*log(4/delta)/n) + 7*R*log(4/delta)/(3*(n-1))
```

with certified outward arithmetic. Total-score sums and squared sums are exact
integers; the mean and unbiased variance are exact rationals, including when
large scores differ by only one point. To avoid relying on a platform libm error
bound, `log(x)` is bounded above by `ceil(log2(x)) * ln(2)_upper`. The logarithm
of two is bounded using 32 rational terms of `2*atanh(1/3)` plus a proved geometric
tail. A floating square-root candidate is checked by exact rational squaring and
moved upward if necessary. The final rational endpoints are converted to doubles
and checked against their exact binary64 values, widening outward as required.
This deliberately gives a slightly wider interval than the formula with an
exact logarithm. Intervals are clipped to the proven score range, never to
observed sample extrema. Fixed checkpoints and the
summable error allocation permit repeated inspection, cancellation and in-memory
continuation without restarting the confidence accounting. As with ordinary
Monte Carlo, the confidence interpretation assumes the pseudorandom draws model
independent samples; a fixed seed provides repeatability, not a proof about a
particular seed's error.

## Budgets and session lifecycle

Time and work limits apply to each `advance()` call. Scheduling is deterministic
for an equal count of work evaluations: three exact-search work steps followed
by one sampling evaluation, with fallback if one lane has stopped. In DAG mode,
checking a parked state and producing one child state each count as a work step;
in prefix mode a complete replay to its next missing choice counts as one. Wall-clock
slices on different machines can therefore finish different amounts of work.
The callback completes before the next cancellation or time check. A time slice
is cooperative, not a hard deadline for one expensive path.

The default retained analysis-state limit is 256 MiB; exact frontier and rational
state have a 64 MiB sublimit. Accounting includes vector capacity, rational limb
capacity and conservative allocation allowances. It is not a process RSS limit
and excludes prepared master/chart data, callback working storage, serialization
copies and transient arithmetic allocations. If exact state reaches its limit,
the original unresolved node remains and independent sampling can continue.

A kernel exception makes the session terminally failed. Its snapshot remains
available for diagnosis, but resume cannot make it valid again. Cancellation is
thread-safe; other access to one analyzer must be serialized. The browser Worker
provides this serialization between synchronous WASM calls. Checkpoints exist
only in the live process/Worker; persistent serialization is not implemented.

## State-kernel requirements and remaining optimization

The state-kernel interface has immutable opaque state handles, full canonical
key bytes, a strictly increasing progress value, retained-memory accounting,
and initial/evaluate/transition callbacks. A state is parked at a random decision
or a terminal result. A transition clones its source, applies one selected legal
outcome, and advances to the next parked state. The analyzer rejects equal or
backward progress; a transition must not mutate any already interned state.

A correct runtime key includes phase/time/cursors, permutation, life, pending
updates, all Luck fields (including the cached next draw), counters and history,
active effects, guarantee/conversion limits, support state, pending lottery
result events, and all emitted output history still needed by scoring. Float32
values are compared by exact bits; pointers and struct padding cannot enter a
portable key. Scores merely coinciding is insufficient to prove equivalence.

The runtime adapter uses checkpoints at complete ideal-update events. Missing
choices retry only the current uncommitted event, with its already chosen local
outcomes retained. The canonical key must include this local queue and the
checkpoint state. Consuming Luck decrements queued lots before its first draw,
and guarantee counters change inside draws, so a transaction cannot start after
these side effects unless they are explicitly represented in its state.

The next optimization is an incremental scorer with explicit primary/auxiliary
float32 state, future commands, owner contribution factors, section score totals,
and the history required by 40 ms frame lookback and score-rank triggers. Only
then can completed raw command/note history be omitted from a key. No state field
may be removed merely because existing fixtures do not change when it is
omitted; a future-independence argument is required. Tight per-state score bounds
and event-specific ordinary-skill envelopes also remain necessary to make the
0.1% target practical on high-variance Luck songs within a short budget. The
confidence guarantee does not imply that target is reached in 30 s.

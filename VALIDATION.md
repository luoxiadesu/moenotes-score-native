# Validation and release gates

This is a local development implementation (`0.1.0-dev.1`), not a fully accepted
release. The agreed gate requires current-card, all-level, condition and important
interaction coverage with independent native joint execution. **That gate is
not yet complete.** The available device is disconnected; the local ARM64
component harnesses provide useful but narrower evidence.

## Executed checks

The repository's CTest executables cover core arithmetic, power adapters,
runtime phases, probability analysis, score bounds and the public API. They use
synthetic fixtures and do not require proprietary assets or account data.
Local GCC/Clang release builds, ASan/UBSan, WebAssembly/Node equivalence and
headless Chromium Worker execution have passed during development. Browser
checks include immediate first-use preparation, cancellation, continuation and
disposal. A relocated installed CMake package was consumed by a C translation
unit linked with the C++ runtime. GitHub Actions [run36348268989](https://github.com/luoxiadesu/moenotes-score-native/actions/runs/36348268989)
passed Linux/GCC, Windows/MSVC, macOS/Apple Clang and WebAssembly at commit
`fcfed4ade092659f2d16cfac812539ba383c6d06`. Each native platform executed all seven
CTest suites; WASM matched the native synthetic API result.

The retained research workspace adds the following evidence. Original game
assets, recovered binaries and native harness results are not bundled here.

| Check | Result | What it establishes |
|---|---:|---|
| Synthetic resolved-power outputs |26 cases matched|Memory, VIP, instruments, leader and related input semantics|
| Public-master power sweep |3,620 matched|All current member awakening/rank and endpoint levels; support ranks and endpoint levels|
| Synthetic runtime migration |1,736 matched|All current cards/skill levels/ranks, probability/Luck extremes, thresholds, mixed decks and 120 skill orders|
| Real 874-note chart runtime |488 matched|Member/support endpoint-level scenarios, 11 operations, commands, judgements, life, counters and final scores|
| Random real-chart mixed decks |244 matched|Weighted intermediate outcomes, all cards/supports, 440 Rush entries/exits|
| Explicit three-Luck missions |40 matched|Task overrides, per-section reset, minimum guarantees and interleaved random decisions|
| Region rerun |732 each for EN and HK|488 endpoint and244 random cases agree with retained region tables; KR is the original run|
| Fixed-runtime full scoring |610 × 5 rank conditions matched|Every note, dual tracks, sections, ranking bonus, card contributions and residuals|
| Native ordinary-member components |900 C++ cases matched|60 cards × 5 skill levels × 3 life inputs, actual ARM64 trigger/duration/apply/controller instructions|
| Native static Gekisou count replay |540 ARM64 replays /270 C++ cases|Actual CounterReplay and cumulative helper; dynamic Combo members are excluded|
| Native cumulative-support applier |300 effect rows matched|450,900 update calls plus 3,900 additional lifecycle calls, actual ARM64 applier/controller chain|
| Conservative bound tests |1,734 synthetic and810 real-chart paths|At declared power1,000,000, executed legal scores remain within the proven domain envelopes|

Counts overlap across checks and must not be added as an independent coverage
percentage. The power sweep uses level endpoints; runtime sweep uses every
member skill level and support rank. Real-chart endpoint scenarios complement
the all-level synthetic scenarios.

Native ordinary-member tests still provide interfaces, target expansion and
condition inversion through fixtures. Cumulative-support tests provide phase,
accepted band conditions and cumulative observations as fixtures. CounterReplay
tests supply collection adapters. None independently validates every stage of
the complete joint LiveExecutor/SkillExecutor scheduler.

The 95% interval contract concerns the declared IID master-probability model and
primary totals. It is not evidence that an original game seed domain is uniformly
distributed in the same way. Small finite random trees are independently
enumerated to check probabilities, extrema, rational expectations and bounds.

## Remaining implementation and acceptance work

1. **Independent full joint native validation:** dynamic Combo, probability
   ordering, all support/member interactions and actual phase observations need
   a complete executor harness. The retained effective-rule matrix distinguishes
   component evidence from unverified joint behavior.
2. **Solver optimization:** the default exact computation uses a probability DAG
   over copyable runtime event checkpoints. Full canonical keys and topological
   probability propagation are implemented and checked against exhaustive trees.
   Incremental scoring, compact future-equivalence keys and optimized pruning
   remain separate improvements.
   A realistic Luck tree may stay bounded within practical budgets.
3. **Production event and nonempty Memory evidence:** explicit contexts and
   synthetic configurations are supported, but retained snapshots do not provide
   corresponding mature-account or active-Challenge observations.

The default 1-second first update and 30-second budget are responsiveness goals,
not deadlines for proving an extremum or reaching the mean-error target. Budget
exhaustion is valid partial output, never an exactness claim.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DMNS_SANITIZE=ON
cmake --build build-sanitize --parallel 4
ctest --test-dir build-sanitize --output-on-failure

# Optional retained-data power differential; no network is used:
python3 scripts/check_power.py --driver build/test_power_driver \
  --master /path/to/master/region --python-source /path/to/python-library/src
```

WebAssembly instructions are in the README. Development evidence outside the
repository is located in the research workspace stage `76_score-native`.

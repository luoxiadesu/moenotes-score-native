# MoeNotes Score Native

An independent C++20 library for ideal-play score analysis: global maximum and
minimum bounds, exact or estimated expectation, section scores and optional card
contributions. It provides a C ABI, a JSON CLI and a JS/TypeScript Web Worker
wrapper around the same WebAssembly implementation. Version `0.1.0-dev.1`.

This is an **experimental implementation**. Small finite problems are solved
exactly; realistic Luck songs generally return valid witnesses, conservative
bounds and estimated means within the requested budget. The current solver uses
copyable event checkpoints and an exact probability DAG with independent sampling.
Equal complete state keys merge incoming probability mass before evaluation.
The conservative keys retain history; optimized state reduction and branch
pruning remain future improvements. A whole native LiveExecutor/SkillExecutor validation remains open;
see [validation](VALIDATION.md) before treating results as verified game scores.

## Meaning of a result

The model fixes ordinary Perfect, natural best permitted Gekisou judgements,
master initial life and a declared 1 ms ideal update clock. Ordinary skill order,
probability skills and Luck follow independent master-weighted choices, retaining
minimum guarantees, cached next draws, Rush and state-dependent conditions. This
is a probability-rule model, not enumeration of original game seeds.

Each rank condition reports:

- `maximum`: a complete legal witness is the lower bound; the upper bound is a
  proven conservative envelope for all remaining paths.
- `minimum`: the lower bound covers every remaining path; the upper bound is a
  complete legal witness.
- `expectation`: exact rational values only after all probability mass has been
  processed, otherwise a sample estimate and an anytime-valid 95% interval for
  total score under the declared sampling model.

`pending`, `bounded`, `estimated` and `exact` are distinct. Only closed bounds
prove extrema. The default expected-score precision target is interval half-width
at most `max(1 point, 0.1% of mean)`. A budget ending does not imply this target was
met. Five default rank conditions share the confidence error budget; section and
contribution means do not carry separate confidence guarantees.

The objective is primary total score. Auxiliary Solo, sections and contributions
on an extremum belong to that same witness; their individually optimal values
are not added together. Contributions contain ten alternating member/support
values in formation order, ranking bonus, then rounding/unowned remainder.

## Build and run

Requires CMake 3.22+, a C++20 compiler and zlib. The chart parser and the required
Boost 1.83 header subset are included with their upstream licenses. There is no
Python, network, account or device dependency at runtime.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure

build/moenotes-score \
  --master-dir /path/to/moenotes-masterdata/hk-tw-mo \
  --chart /path/to/0069/0069_03.json \
  --request examples/battle.json \
  --budget-ms 30000 --progress
```

The CLI reads a region's local `Master*.json` files, excluding the manifest.
`--progress` emits NDJSON approximately once per second; `--output result.json`
saves the final snapshot. Inputs and account data are never fetched implicitly.
A runtime or allocation failure returns a nonzero CLI exit; a valid budget-limited
analysis returns success with its incomplete state in JSON.
Use your own matching master and chart; game assets are not bundled.

The new request format is intentionally independent of the old Python schemas:

```json
{
  "schema_version": 1,
  "calculation": {"music_score_id": 10006903, "mode": "battle"},
  "deck": {"members": [
    {"member_card_id": 1}, {"member_card_id": 2}, {"member_card_id": 3},
    {"member_card_id": 4}, {"member_card_id": 5}
  ]},
  "options": {"contributions": false}
}
```

Mode is `free`, `battle`, `arena`, `challenge`, `mission`, or `challenge_skip`.
Free/played Challenge default Gekisou off; Battle/Arena/Mission require it.
Battle/Arena produce auxiliary SoloScore. Solo's auxiliary value is zero.
Gekisou analysis defaults to five outputs for ranks `[1,1,1]` through `[5,5,5]`;
`options.ranks:[1,3,2]` requests one mixed condition. Unknown opponents are not
inferred. Skip is a separate deterministic Great/life100/combo0 projection.

`calculation.context.missions` explicitly overrides all three missions and is
used consistently by judgement, runtime and rank-table selection.
`calculation.context.music_type` supplies an explicit song-type override.
`deck_context` supplies event power rates and other declared bonuses. Selecting
an event-like mode does not automatically discover active event configurations.

For raw account input replace `deck` with `player_data` and `deck_id`. See
[deck input](docs/deck-input.md) for training, support, account, Memory and VIP
fields. Identifiers and credentials are excluded from deck reports. Source
fingerprints cover the master/chart, sanitized resolved deck and analysis config.

## C and CMake integration

```sh
cmake --install build --prefix /your/prefix
```

```cmake
find_package(moenotes-score-native 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE moenotes::score)
```

The public [header](include/moenotes_score.h) declares opaque prepared-data, deck
and analysis handles. Input JSON/bytes are borrowed for a call. Handles retain
their dependencies, so a prepared/deck handle can be released after constructing
an analysis. Every returned JSON string must be freed with `mns_string_free`.
Exceptions never cross the ABI; inspect `mns_last_error` after a failed operation.

Prepare once, resolve cards, create an analysis, call `mns_analysis_advance` in
bounded slices and read snapshots. Cancel/resume preserve state in the current
process. Session mutation is single-writer; cancellation may be requested from
another native thread. Prepared inputs are immutable and reusable. There is no
public gameplay rewind or touch-replay API.

## Browser

With Emscripten **6.0.10** activated and Node/npm available:

```sh
scripts/build_web.sh
node tests/wasm.mjs build-wasm/moenotes-score.mjs
```

Serve `dist/web` from the same origin as your application:

```js
import { ScoreAnalyzer } from "./score/index.js";
const analyzer = new ScoreAnalyzer(new URL("./score/worker.mjs", import.meta.url));
await analyzer.prepare({
  master: localMasterTables,
  chart: new Uint8Array(chartBytes),
  chartName: "0069_03.json",
  calculation: {schema_version: 1, music_score_id: 10006903, mode: "battle"},
  deck: {deck: myStructuredDeck},
  options: {contributions: true}
});
analyzer.onProgress = snapshot => render(snapshot);
const result = await analyzer.run(30000);
// await analyzer.resume(30000); continues the same analysis.
analyzer.dispose();
```

Worker slices are cooperative, not hard real-time deadlines; a complete path
may exceed a slice. Cancel an active run, await its promise, then resume.
No SharedArrayBuffer, cross-origin isolation or WASM threads are required.

## Resource and correctness contracts

Default analysis retained-state budget is 256 MiB, with at most 64 MiB used for
exact enumeration. These are accounting limits, not a whole-process RSS cap;
prepared data, kernel temporaries, JSON and runtime allocator overhead are
separate. Chart bytes are limited to 16 MiB, parser allocations to 128 MiB, JSON
input to 64 MiB and depth to 64. Chart time is limited to one hour and nodes to
one million. Conservative arithmetic envelopes can reject extreme custom data
that cannot be proven within supported score ranges.

Time-budget expiry or cancellation can resume both lanes. When
`exact_memory_limited` becomes true, that session preserves its unresolved
probability frontier but freezes exact exploration; further time only continues
sampling. A larger exact-memory budget requires a new session. More time alone
does not remove a memory limit.

Runtime binary32 evaluation preserves the recovered operation grouping and
rounding. Do not enable fast-math or FMA contraction. Output score, provenance,
diagnostic state and random-model version must be considered together.

See [runtime rules](docs/runtime.md), [analysis algorithms](docs/analysis.md),
[validation](VALIDATION.md) and [third-party notices](THIRD_PARTY.md).

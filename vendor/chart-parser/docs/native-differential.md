# External Native Differential Checks

The optional checker consumes independently produced native snapshots. It does
not execute game code or include any game binary, chart, or golden snapshot.
Use Python 3.9 or newer and a built CLI:

```sh
python3 tests/check_native.py build/moenotes-chart-parser /path/to/MusicScore /path/to/snapshots
```

Each `*.json` snapshot contains `file` (a corpus-relative path), `mirror` (bool),
and `native`. The parser runs independently on the original chart with relative
eighth Combo generation enabled. `.failure.json` records are counted and make
the check fail; an empty snapshot directory also fails. This command cannot
establish coverage beyond the snapshots supplied by the caller.

The `native` object has the following schema:

- `after`: final accepted note objects. Each object has `id`, `type`,
  `position: [bar, rhythm, unit, progress, time_ms]`,
  `geometry: [integer_left, integer_right, left, right, width]`, `line_ids`,
  `pair`, `critical`, `direction`, `slide_along`, and `ease: [left, right]`.
  Optional `fever` is the zero-based sorted Fever index, or -1.
- `origins`: native source ID to `[outer_source_index, node_index]`. Standalone
  notes use node index zero. The source token is captured before merging; it
  must not be inferred from the C parser's result.
- `branch_map`: native line ID to original outer source index.
- `lines`: native line ID to `{begin, end, members}` with native note IDs and
  final ordered members. Extra reference/view/cache fields are allowed.
- Optional `events`: `bpm` and `sig` arrays with `{position, value}`; `skill`
  with `{position}`; `fever` with `{position, end}`; `call` with
  `{position, rhythms}`. Every position uses the five-element layout above.
  When events are provided, also provide `bar_times` and `last_ids`.

Source nodes match by captured provenance and same-tick occurrence. Generated
nodes match by source branch, bar, and float32 progress bits. Native IDs are
mapped to library-local IDs; they are not compared numerically. Pair links,
line endpoints, and ordered members are compared after this mapping.
Generated rhythm/unit encoding and native absent-pair sentinels are deliberately
normalized. Floats compare by float32 bits, including stored right edges.

The checker is intended for the documented SS schema and source token contract;
it is not a general graph-isomorphism solver. A provenance mismatch must be
investigated instead of adding a broad geometry/time tolerance. Source alpha,
visibility, and native internal duplicate reference lists are outside this
public CLI comparison. The CLI exposes raw call values separately from their
native rhythm projection.

Snapshot provenance matters. A valid report should name the native version,
binary hash, executed entry points, runtime shims, tokenization boundary,
failed cases, modes, and normalization rules. An emulator with CLR/Newtonsoft
adapters establishes scoped offline evidence, not live-client or hotfix parity.

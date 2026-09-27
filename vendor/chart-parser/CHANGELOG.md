# Changelog

All notable project changes are recorded here. Versions use
`MAJOR.MINOR.PATCH`; see the [API compatibility policy](docs/api.md#version-and-stability).

## [Unreleased]

## [0.3.0] - 2026-09-24

**Behavioral migration from 0.2.x:** public notes now expose final auto geometry;
BPM segment anchors use cumulative nearest-even rounding; Fever and Call events
are time-sorted. Existing declaration layouts are retained. Rebuild cached output
and follow [the migration guide](docs/api.md#migration-from-v02x).

- Preserve pre-processing geometry through `moenotes_score_source_note_at` while
  final note and line-member accessors apply native Connection postprocessing.
- Keep generated Combo right edges directly instead of reconstructing them from width.
- Order line members by native position/creation order, including Hidden before
  automatic Connection at the same tick; retain global tick/ID enumeration.
- Fix shared guide starts to inherit the same first overlapping standalone note's
  type and Flick direction, including repeated guide branches.
- Keep the fractional cumulative BPM duration before rounding segment anchors;
  tick queries still floor local elapsed time. Avoid cumulative truncation drift.
- Add Call rhythm fractions, note-to-Fever association, bar-line positions, and
  final timing-group accessors. Expose them and source geometry in CLI JSON.
- Add native-derived synthetic cases, C API contract checks, and installed C++
  coverage for the new accessors. Real chart/native artifacts remain external.
- Add the portable external native-snapshot checker. All 356 charts in both
  normal/mirrored modes (712 runs, 412,674 nodes) match the compared native fields,
  ordered line membership, and event/assembly projections with documented runtime
  adapters and ID normalization. Release and sanitizer corpus regressions pass.

## [0.2.0] - 2026-09-24

**Behavioral migration from 0.1.x:** source-note times now use the creator's
float32 bar/progress clock; public mirror easing remains in source order;
pairing includes normal slide ends and retains the previous-candidate cache.
Existing C declarations and layouts are preserved. Rebuild cached chart output
and follow [the migration guide](docs/api.md#migration-from-v01x).

- Add `moenotes_score_note_position_at_tick`, keeping the existing tick-clock
  query for event anchors; check finite/range bounds before time conversion.
- Fix ComboSkip thresholds to query BPM by candidate time, use note-clock
  endpoints, and identify the first/last entries of the full candidate list.
- Correct asymmetric-easing Combo mirrors and equal-millisecond interpolation;
  add a separate experimental judgement-line sampling function.
- Apply begin/hidden/other creation priorities and all six pair-candidate types.
  Pair links can be asymmetric after later assignments.
- Add `moenotes_line_view_t` / `moenotes_score_line_at` and CLI `lines` output;
  preserve the origin of generated nodes on shared-begin branches.
- Index line members, reverse memberships, and judgement commands at parse time
  instead of rescanning the chart on each accessor call.
- Add native-derived synthetic regressions and extend corpus mirror checks to
  generated nodes and branch membership checks. Current 356 JSON inputs pass;
  all 340 song counts match the available master, without claiming full native
  equivalence. Shared graph warnings and unsupported legacy formats remain.

## [0.1.0] - 2026-09-23

First versioned release and public C API baseline. The interface is frozen for
the v0.1.x series; experimental derived values remain subject to corrections.

### Added

- Public `moenotes_*` API, version macros, and `moenotes_version_string()`.
- Line sampling, shared-endpoint memberships, warning flags, floating-point
  geometry, and source/generated metadata for preview renderers.
- Skill, fever, and call-event accessors, including call timing values.
- Experimental relative-eighth Combo/ComboSkip generation and explicit-slot
  Flick-hidden generation, both opt-in.
- Installable static library/header/CLI, CMake package, and the
  `moenotes::chart_parser` consumer target.
- CLI `--help` and `--version`, complete C example, API reference, compatibility
  documentation, and development/release guidance.
- API-contract and installed C/C++ consumer tests, expanded synthetic
  regressions, external-corpus runner, and optional Clang libFuzzer target.
- GitHub Actions checks for the public contract, packaging, and sanitizers.

### Changed

- **Breaking from the unversioned prototype:** renamed `music_score.h` to
  `moenotes_chart_parser.h`, `ms_*` to `moenotes_*`, and `MS_*` to `MOENOTES_*`,
  including CMake options. Public view structures gained fields. No aliases are
  retained; consumers must migrate and recompile.
- IDs are explicitly library-local, not native-client IDs. Count agreement is
  no longer presented as proof of gameplay equivalence.
- Legacy SUS-like text and unsupported combo units return an unsupported error.

### Fixed

- Compact time-signature parsing, timeline defaults, and relative combo placement.
- Tick-distance automatic-position interpolation, independent edge easing,
  width preservation, and mirror geometry.
- Flick-hidden association no longer attaches standalone Flicks to every
  overlapping long line.
- Allocation lifetime issues during note-array growth; gzip now uses caller
  allocator hooks and participates in allocation-failure tests.
- Unknown operation values no longer report themselves as judgement notes.
- Release builds retain regression assertions.

### Known Limitations

- Legacy SUS text is unsupported; 139 external JSON charts pass compatibility
  checks, while 64 legacy text charts are recognized as unsupported.
- Complex native graph ordering, pairing, exact derived numerical boundaries,
  and full native-converter differential verification remain incomplete.
- Windows and Android NDK builds are unvalidated. See
  [Compatibility](docs/compatibility.md) for the complete scope.

[Unreleased]: https://github.com/luoxiadesu/moenotes-chart-parser/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/luoxiadesu/moenotes-chart-parser/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/luoxiadesu/moenotes-chart-parser/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/luoxiadesu/moenotes-chart-parser/releases/tag/v0.1.0

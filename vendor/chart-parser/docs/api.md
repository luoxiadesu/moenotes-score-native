# Public API Reference

## Version and Stability

**v0.3.0 corrects BPM anchors, final auto geometry, and shared guide absorption.**
Existing function signatures and structure layouts are retained; see
[Migration from v0.2.x](#migration-from-v02x) before updating consumers. The supported
interface consists of the declarations in `include/moenotes_chart_parser.h`, the documented
behavior here, and the CMake target `moenotes::chart_parser`. The original build
target `moenotes_chart_parser` remains available for source-tree builds.
Private implementation details and vendored yyjson symbols are not public API.

Versions follow `MAJOR.MINOR.PATCH`:

- **PATCH**: compatible fixes and documentation changes. Existing symbols,
  signatures, enum values, field types/order/layouts, defaults, and ownership
  contracts are preserved within each minor series.
- **MINOR**: additive features are preferred. During 0.x development, an
  unavoidable incompatible change requires a new minor version, a prominent
  changelog entry, and a migration guide. A new minor version is not permission
  to rename or remove existing interfaces casually.
- **MAJOR**: starting at 1.0.0, incompatible public API changes require a new
  major version. Deprecation and an additive replacement should precede removal.

Do not append fields to existing public structures in a compatible release:
callers allocate these structures and the ABI has no size/version negotiation.
Prefer new functions and new structure types for additional data. Existing enum
values must not be renumbered. Ordinary additions belong in a minor release.
The installed CMake package accepts compatible versions in the same minor series.

This is a source compatibility contract, with structure layouts preserved for
the same compiler ABI within each minor series. It is not a cross-platform binary
ABI guarantee: only a static library is provided. Use matching headers and library,
and do not alter enum size, structure packing, or floating-point semantics.
Do not enable fast-math. C++ consumers can include the header directly.

Experimental Combo/ComboSkip, Flick-hidden, pairing, and native graph/timing
approximations may receive correctness fixes that change derived values, IDs,
or counts in a patch release. Such changes must be recorded in the changelog.
The API freeze does **not** certify game-client equivalence or identical chart
output across versions. Pin an exact version when caching generated output.

`MOENOTES_VERSION_MAJOR`, `MOENOTES_VERSION_MINOR`, `MOENOTES_VERSION_PATCH`, and
`MOENOTES_VERSION_STRING` describe the header version. `moenotes_version_string()`
returns a static string for the linked library version. Never free this string.

## Parsing and Ownership

```c
moenotes_result_t moenotes_score_parse(
    const void *data, size_t size,
    const moenotes_parse_options_t *options,
    const moenotes_allocator_t *allocator,
    moenotes_score_t **out_score,
    char *error_message, size_t error_message_size);
```

- `data`, a nonzero `size`, and `out_score` are required. Input need not be
  NUL-terminated. JSON or a single gzip member is detected automatically.
- Input is borrowed only during the call and is never modified. A successful
  score owns its parsed storage. It retains no pointers into input or options.
- `options == NULL` selects defaults. Otherwise initialize the entire structure
  with `moenotes_default_parse_options` before changing individual fields.
- `allocator == NULL` selects the C runtime allocator. Custom allocators must
  supply all three callbacks. The callback table is copied; its `ctx` and
  callable functions must remain valid until the score is freed.
- Allocator callbacks must provide normal malloc alignment. `realloc_fn` must
  accept NULL and leave the previous allocation valid on failure. All parser,
  JSON-reader, and gzip-decoder allocations use these callbacks.
- On success, `*out_score` is a new handle, even for an empty chart. On failure,
  it is NULL. Do not pass the only pointer to an existing live score as output:
  parsing clears the output and does not free a previously stored handle.
- The optional error buffer is cleared on success. On failure it receives a
  truncated, NUL-terminated diagnostic when non-NULL and capacity is nonzero.
  Diagnostic wording is not a stable machine-readable interface.
- `moenotes_score_free(score)` releases the handle. It accepts NULL. Accessors
  return value copies, which remain usable after the score is freed.
- There is no mutable global parser state. Separate parses and read-only
  accessors can run concurrently if allocator contexts and output buffers are
  used safely. Never free a score concurrently with readers.

### Options

| `moenotes_parse_options_t` field | Default | Contract |
| --- | --- | --- |
| `start_note_id` | `0` | Nonnegative first library-local ID. IDs must fit below `INT32_MAX`. |
| `slide_combo_unit` | `0` | `0` disables derived combos; `8` enables experimental relative eighths. Other values are unsupported. |
| `mirror` | `0` | Nonzero reflects geometry and left/right direction. |
| `add_flick_hidden` | `0` | Nonzero enables experimental explicit-slot hidden-node generation. |

`moenotes_default_parse_options(NULL)` is a no-op.

### Results

| Result | Value | Meaning |
| --- | --- | --- |
| `MOENOTES_OK` | 0 | Success. |
| `MOENOTES_ERR_INVALID_ARGUMENT` | 1 | Required pointer missing, empty input, invalid allocator, or negative start ID. |
| `MOENOTES_ERR_OUT_OF_MEMORY` | 2 | Allocation failed. |
| `MOENOTES_ERR_GZIP` | 3 | Invalid, truncated, concatenated, or trailing-data gzip. |
| `MOENOTES_ERR_JSON` | 4 | Invalid JSON syntax. |
| `MOENOTES_ERR_SCHEMA` | 5 | Invalid chart structure or field type. |
| `MOENOTES_ERR_RANGE` | 6 | Value, index, time, ID, or resource limit exceeded. |
| `MOENOTES_ERR_INTERNAL` | 7 | Internal failure; reserved for implementation errors. |
| `MOENOTES_ERR_UNSUPPORTED` | 8 | Unsupported format, option, or derived subdivision. |

`moenotes_result_string(result)` returns a static diagnostic string. Unknown
values map to an internal-error diagnostic. Do not free or match on its wording.

## Notes and Geometry

`moenotes_score_note_count(score)` returns the number of exposed, coalesced
notes. `moenotes_score_note_at(score, index, out_note)` copies a
`moenotes_note_view_t` with final geometry. `moenotes_score_source_note_at` uses
the same indices and IDs but returns geometry before final line processing.
Generated nodes are identical through both accessors. Enumeration is ascending by `tick`, then by `id`.
IDs can have gaps and are not array indices. They are deterministic for the same
input, options, and version, but are neither native IDs nor persistent identifiers.

| Field(s) | Meaning |
| --- | --- |
| `id`, `operate_type`, `tick` | Local ID, operation enum, and absolute tick. |
| `position` | Bar/progress/time representation described below. |
| `lane_count` | 24, also returned by `moenotes_score_lane_count(score)`. |
| `lane_start`, `lane_end` | Integer lane interval with an inclusive end. |
| `lane_start_float`, `lane_end_float`, `width` | Floating-point geometry with an inclusive right edge. Preserve the stored edge: reconstructing it from width can differ by one float32 ULP. |
| `critical`, `visible` | Source flags, independent of judgement classification. |
| `slide_along`, `pos_auto` | Automatic-position metadata for line nodes. |
| `direction` | `NORMAL=0`, `LEFT=1`, `RIGHT=2`; mirror swaps left/right. |
| `ease_left`, `ease_right` | Source edge easing: `LINEAR=0`, `OUT=1`, `IN=2`. Mirror preserves their order. Rendering sampling handles reflected edges internally. |
| `alpha` | `NONE=0`, `FADE_IN=1`, `FADE_OUT=2`; metadata, not an evaluated opacity. |
| `pair_note_id` | Creator-style cached partner, or -1. With three or more simultaneous candidates, older links can be asymmetric. |
| `parent_note_id` | Canonical begin of the primary line, including on the begin itself, or -1. Use line memberships and `line_at` for every shared branch. |
| `hidden_for_note_id` | Source Flick ID for a generated hidden node, otherwise -1. |
| `line_id` | Primary line ID, or -1 for a standalone note. |
| `line_index` | Reusable slot 0..19 per line kind, or -1 for standalone notes; not a line ID. |
| `source_index` | Zero-based outer source-note index; derived nodes retain their origin. |
| `generated` | 1 for derived bookkeeping/combo nodes, 0 for source records. |

Source integer geometry uses nearest-even rounding and a minimum integer width
of one. Final automatic Connection/ConnectionTrace geometry is interpolated
after line setup, uses the note clock and source-left easing, and has floor/ceil
integer coverage. Its source geometry remains available for rendering.
Float widths can be zero, and float geometry should be used for drawing.
Coordinates are not clamped to the 24-lane viewport. Generated combos instead
use floor/ceil integer coverage. Float fields expose some float32 calculations
as doubles; they do not promise full double-precision computation.

`moenotes_operate_type_name(type)` returns a static lowercase operation name,
or `"none"` for NONE/unknown values. Names correspond to the suffixes of the
`MOENOTES_OP_*` constants in the header.
`moenotes_operate_type_is_judgement(type)` returns 1 for known judgement types
and 0 otherwise. Non-judgement types are NONE, HIDDEN_SLIDE_BEGIN,
HIDDEN_SLIDE_END, GUIDE_BEGIN, GUIDE_END, COMBO_SKIP, HIDDEN, and INVALID_HIDDEN.
COMBO is a judgement type; COMBO_SKIP is not. Do not infer visibility from this
predicate or draw every generated record as a source-note sprite.

## Timeline and Events

Timing uses 480 ticks per quarter note, with 120 BPM and 4/4 defaults inserted
at tick zero when necessary. Ticks and times are nonnegative int32 values.
The BPM segment builder retains a double-precision cumulative elapsed time and
rounds each segment anchor to the nearest even integer. It does not accumulate
rounded anchors. Tick queries floor the local interval plus that stored anchor.

`moenotes_score_position_at_tick(score, tick, out_position)` returns a
`moenotes_position_t`: `bar` is zero-based, `rhythm` is the tick offset in the
bar, `rhythmic_unit` is that bar's length in ticks, `bar_progress` is its
fractional progress, and `time_ms` is elapsed integer milliseconds.
This function uses the **tick clock**, also used for event anchors.

`moenotes_score_note_position_at_tick(score, tick, out_position)` uses the same
bar/rhythm representation but calculates **source-note time** through the
creator's float32 bar/progress path. Source notes and commands use this clock.
It can differ from the tick clock, including at event boundaries.

The creator clock selects BPM and signature events strictly before the target
bar/progress and uses the latest elapsed-time anchor among them. The tick clock
selects segments at or before the target tick. The creator's internal initial
BPM is 160; the inserted tick-zero BPM default remains 120, and applies to
positive positions. Do not merge these two clocks or clamp their differences.
Unrepresentable or negative results return a range error.

**Experimental generated combo positions instead express `rhythm`/`rhythmic_unit`
on a relative-eighth subdivision grid**; use `bar_progress`, `tick`, and `time_ms` for a unified
timeline. Re-querying a generated tick need not reproduce its rounded position.

| Function | Output |
| --- | --- |
| `moenotes_score_bpm_count(score)` | Number of BPM entries, including inserted defaults. |
| `moenotes_score_bpm_at(score, index, out_event)` | `moenotes_bpm_event_t`: tick, BPM, position. |
| `moenotes_score_signature_count(score)` | Number of signature entries, including inserted defaults. |
| `moenotes_score_signature_at(score, index, out_event)` | `moenotes_signature_event_t`: tick, numerator, denominator, position. |
| `moenotes_score_event_count(score)` | Number of skill, fever, and call events. |
| `moenotes_score_event_at(score, index, out_event)` | `moenotes_event_t`: type, start/end tick and position, value count. |
| `moenotes_score_event_value_at(score, event_index, value_index, out_value)` | One raw int32 call-timing value. |

BPMs/signatures are tick-sorted; duplicate ticks retain source order and the
last entry at that tick is effective for the tick clock. Extra events are grouped by type
(`SKILL=0`, `FEVER=1`, `CALL=2`), not globally sorted. Skills retain source order;
Fever and Call events are sorted by start tick, with source order retained for ties.
For skill/call events, end equals start. Fever stores its explicit end.
Only call events have `value_count` entries. Raw call values are retained.
`moenotes_score_call_rhythm_count` / `moenotes_score_call_rhythm_at` expose the
native rhythm projection: each entry equal to 1 contributes float32
`(index + 1) / value_count`. Other values contribute nothing. Results are
fractions in `(0, 1]`, not absolute ticks or scheduled commands. A non-Call
event has count zero; querying one returns a range error.

`moenotes_score_note_fever_event(score, note_id, out_event_index)` returns the
first Fever event whose millisecond interval contains the note time, including
both boundaries. The output is an index into the grouped event API, or -1 when
there is no match. An unknown note ID returns a range error. Overlapping Fever
intervals use the first matching event in the sorted list.

`moenotes_score_bar_line_count` / `moenotes_score_bar_line_at` expose tick-clock
bar heads from bar zero through the greatest source-note bar, inclusive. An
empty score has bar zero. Unrepresentable bar-head time/tick queries return a
range error; the parser does not allocate one object for every bar.

`moenotes_score_last_timing_note_count` / `moenotes_score_last_timing_note_at`
return final notes sharing the greatest float32 `bar + bar_progress` key, in
library enumeration order. Empty scores have no last notes. These are all
notes at the final position, including non-judgement nodes.

## Lines and Warnings

| Function | Contract |
| --- | --- |
| `moenotes_score_line_count(score)` | Number of source long/guide lines. Valid IDs are 0..count-1. |
| `moenotes_score_line_at(score, index, out_line)` | Source-branch ID, slot, source index, canonical begin/end IDs, and guide flag. |
| `moenotes_score_note_line_count(score, note_id)` | Membership count; 0 for absent IDs or standalone notes. |
| `moenotes_score_note_line_at(score, note_id, index, out_line_id)` | Membership in ascending line-ID order; an unknown ID is a range error. |
| `moenotes_score_line_member_count(score, line_id)` | Number of exposed members; 0 for invalid lines. |
| `moenotes_score_line_member_at(score, line_id, index, out_note)` | Final member copy in float32 bar/progress order, with creator order breaking ties, including generated members. |
| `moenotes_score_sample_line(score, line_id, tick, out_sample)` | Source geometry in tick space over the inclusive first-to-last tick range. |
| `moenotes_score_sample_judgement_line(score, line_id, tick, out_sample)` | Experimental creator geometry at a source tick: note-clock ratio, one source-left easing, and bar/progress fallback for equal milliseconds. |
| `moenotes_score_warnings(score)` | Bitmask of nonfatal compatibility warnings. |

Shared endpoints can belong to multiple lines. A returned canonical note's
primary `line_id` can differ from the requested membership. Preserve all
memberships when drawing branches. Member enumeration is a timeline view, not
a guarantee of original source-node order on nonmonotonic inputs.

`moenotes_line_view_t` contains `id`, `line_index`, `source_index`,
`begin_note_id`, `end_note_id`, and `guide`. It describes a source branch, not a
mutable native `ConnectionNote.LineEndNote` field. Shared begins can have several
ends; shared ends can have several begins. Each canonical note appears once per
line. Line/member and reverse membership accessors use precomputed indices.

Sampling returns `moenotes_line_sample_t` with `lane_start`, inclusive
`lane_end`, and `width`. It interpolates between explicit anchors using separate
left/right eases, skipping interior auto nodes as anchors. This is a rendering
helper, **not gameplay judgement-area interpolation**. It does not evaluate
visibility or alpha. The judgement sampler uses source notes on the specified
branch, skips interior auto anchors, and clamps its eased ratio. Its input tick
is converted with the note clock; querying a rounded generated tick need not
reproduce a generated node's exact fractional position. It does not apply
judgement windows, hitbox offsets, or gameplay logic. Nonmonotonic and complex
shared-line behavior is not authoritative; see [Compatibility](compatibility.md).

| Warning | Value | Meaning |
| --- | --- | --- |
| `MOENOTES_WARNING_NONE` | 0 | No currently recognized warning; not proof of native equivalence. |
| `MOENOTES_WARNING_NONMONOTONIC_LINE` | 1 | Source node ticks decrease within at least one line. |
| `MOENOTES_WARNING_SHARED_ENDPOINT` | 2 | Endpoints were coalesced; preserve their multiple source branches. This remains a compatibility advisory for untested graph shapes. |

Treat this as a bitmask and tolerate future additional warning bits.

## Counts and Command Projections

`moenotes_score_full_combo_count(score, include_slide_combos,
include_hidden_flick_nodes)` counts judgement records, optionally excluding
COMBO and optionally including generated Flick-hidden nodes. Flags do not
generate missing records: the corresponding parse options must be enabled
first. COMBO_SKIP never contributes. This is not an authoritative game total.

`moenotes_score_command_count(score)` counts judgement records, including
generated COMBO but excluding hidden nodes. It matches the full-combo query
with flags `(1, 0)`.

`moenotes_score_command_at(score, index, score_type, current_life,
current_combo, out_command)` projects the indexed judgement note into
`moenotes_command_t`. It copies the note's time, ID, and operation; passes
through `score_type` and `current_life`; and sets the command's combo to
`current_combo + index`. Thus `current_combo` is a sequence base, not the
per-call current count. Integer overflow returns a range error. This function
does not judge input, update life, evaluate skills, or calculate a score.

## Accessor Errors

All `*_at`, position, and sampling functions require a live score and non-NULL
output pointer; otherwise they return `MOENOTES_ERR_INVALID_ARGUMENT`.
Invalid indices, missing memberships, negative ticks, out-of-line samples,
or unrepresentable times return `MOENOTES_ERR_RANGE`. Output contents are
unspecified on error and must not be used. Indices are zero-based.
Count queries, lane count, warnings, and full-combo count return zero for a NULL
score. No function accepts dangling pointers or synchronizes destruction.

## Migration from v0.2.x

- Rebuild cached output: BPM changes can shift events, notes, ComboSkip decisions,
  and sampled judgement geometry after tempo changes.
- `note_at`, `line_member_at`, and `last_timing_note_at` return final auto geometry.
  Use `source_note_at` or `sample_line` for the pre-processing rendering geometry.
- Preserve the returned Combo right edge instead of reconstructing it from width.
- All matching guide starts inherit the same first standalone overlap's type and
  Flick direction. Critical/easing/geometry still come from each guide start.
- Fever and Call event indices may change after sorting. Store the raw source
  separately if original event order is required.
- Line members use native position/creation ordering. At the same tick, Hidden
  precedes Connection even when its library-local ID is larger. Global note
  enumeration retains tick/ID order.
- Update CMake requirements to `find_package(moenotes-chart-parser 0.3.0 CONFIG REQUIRED)`.
  Existing public structure layouts and declarations are retained.

## Migration from v0.1.x

- Update CMake consumers to `find_package(moenotes-chart-parser 0.2.0 CONFIG REQUIRED)`.
  Same-minor package matching intentionally rejects an unchanged 0.1 requirement.
- Rebuild cached parsed data. Source note times, Combo/ComboSkip classifications,
  generated IDs, pairing, and mirrored derived geometry can change. Existing
  enum numbers, structure layouts, option defaults, and ownership remain unchanged.
- Keep `position_at_tick` for event/tick timing. Use `note_position_at_tick` to
  reproduce a source note's position; the old source-note/tick-clock equality
  is no longer a contract. Commands carry the corrected note time.
- Mirror no longer swaps the public `ease_left`/`ease_right` fields. Use
  `sample_line` for reflected rendering geometry and `sample_judgement_line`
  for the experimental creator interpolation; do not conflate these outputs.
- Pair candidates are Normal, SlideBegin, SlideEnd, Flick, SlideBeginFlick, and
  SlideEndFlick. Creator ordering puts begins before hidden nodes before other
  nodes within each float bar/progress bucket, with deterministic source-bucket
  order for ties. Pair links follow the previous-candidate cache and may be
  asymmetric after later assignments; remove disjoint-pair/symmetry assumptions.
- Use `line_at` and all note memberships to obtain shared-branch begin/end links.
  Generated `source_index` identifies its own branch even when the begin is shared.
  CLI JSON now includes a `lines` array with canonical endpoint/member IDs.

These corrections reflect bounded native analysis, not a full runtime graph
replay. Shared-endpoint warnings remain meaningful.

## Migration from the Unversioned Prototype

Replace `music_score.h` with `moenotes_chart_parser.h`, `ms_*` with `moenotes_*`,
and `MS_*` with `MOENOTES_*`, including CMake options. Recompile consumers:
public view structures gained fields. Old-name aliases are not exported.
The freeze starts at v0.1.0, not at the initial unversioned commit.

# MoeNotes Chart Parser

MoeNotes Chart Parser is an offline C17 library and command-line tool for reading
MoeNotes SS JSON charts. It provides note geometry, timing, line relationships,
and chart events for preview renderers and analysis tools.

Version **0.3.0** adds final note geometry and event projections, and corrects
BPM accumulation and shared guide starts.
See the [migration guide](docs/api.md#migration-from-v02x) for behavior changes.
This project is an independent format reconstruction, not a client-equivalent gameplay or
scoring engine. It does not download charts, connect to game services, or include
game assets.

## Supported Features

- JSON and gzip-compressed JSON, parsed directly from memory.
- Tap, flick, trace, long, and guide notes, including hidden control points.
- BPM changes, time signatures, and separate tick-clock and source-note-clock queries.
- Automatic positions, independent edge easing, floating-point widths, and mirroring.
- Visibility, alpha, critical flags, indexed line memberships, and source-branch endpoints.
- Separate rendering and experimental judgement-geometry line sampling.
- Source and final note geometry, including automatic-connection postprocessing.
- Skill, fever, and call events, raw timing arrays, Call rhythm fractions, and Fever membership.
- Bar-line timing and final timing-group queries.
- Explicit errors, copy-based accessors, and optional custom allocators.
- Opt-in, experimental Combo/ComboSkip and explicitly associated Flick-hidden nodes.

All 356 JSON charts in the current external corpus pass default, derived,
mirrored, repeated, and gzip compatibility checks. Four additional legacy
SUS-like text assets remain **unsupported**. The 356 charts also pass 712 normal
and mirrored native snapshot comparisons covering 412,674 nodes, with scoped
CLR/Newtonsoft adapters and ID normalization. See [Compatibility](docs/compatibility.md)
for the exact coverage and remaining boundaries.

## Installation

Requires a C17 compiler, CMake 3.16 or newer, and the zlib development package.
For example, on Debian or Ubuntu:

```sh
sudo apt install build-essential cmake zlib1g-dev
```

Build, test, and install from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build
```

This installs the static library, public header, CLI, CMake package, and
documentation. Add `$HOME/.local/bin` to `PATH` to run the CLI by name.
Use `-DMOENOTES_BUILD_CLI=OFF` or `-DMOENOTES_BUILD_TESTS=OFF` to omit those targets.
Linux with GCC 13 and Clang 18 is tested; Windows and Android NDK are not yet validated.

In a consuming CMake project:

```cmake
find_package(moenotes-chart-parser 0.3.0 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE moenotes::chart_parser)
```

Configure the consumer with `-DCMAKE_PREFIX_PATH="$HOME/.local"` when needed.
The imported target supplies the include path and zlib/math link dependencies.
For a vendored build, use `add_subdirectory(path/to/moenotes-chart-parser)` and
link the same `moenotes::chart_parser` target.

## Minimal C Example

The following complete program is also available as [examples/basic.c](examples/basic.c).
Passing `NULL` for the options and allocator selects the defaults.

```c
#include <moenotes_chart_parser.h>
#include <inttypes.h>
#include <stdio.h>

int main(void) {
    const char json[] = "{\"score\":{\"events\":{},\"notes\":["
                        "{\"type\":\"tap\",\"t\":480,\"pos\":2,\"size\":4}]}}";
    moenotes_score_t *score = NULL;
    char error[128];
    moenotes_result_t result = moenotes_score_parse(
        json, sizeof(json) - 1, NULL, NULL, &score, error, sizeof(error));
    if (result != MOENOTES_OK) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    moenotes_note_view_t note;
    result = moenotes_score_note_at(score, 0, &note);
    if (result == MOENOTES_OK) {
        printf("tick=%" PRId32 " time=%" PRId32 "ms left=%.1f width=%.1f\n",
               note.tick, note.position.time_ms, note.lane_start_float, note.width);
    }
    moenotes_score_free(score);
    return result == MOENOTES_OK ? 0 : 1;
}
```

Expected output: `tick=480 time=500ms left=2.0 width=4.0`.
The input buffer can be released after parsing; free the returned handle with
`moenotes_score_free`.

## CLI Usage

```sh
moenotes-chart-parser --version
moenotes-chart-parser --help
moenotes-chart-parser parse chart.json > parsed.json
moenotes-chart-parser parse chart.json.gz --mirror > mirrored.json
moenotes-chart-parser parse chart.json --combo-unit 8 --flick-hidden > derived.json
```

| Option | Effect |
| --- | --- |
| `--mirror` | Reflect lane geometry and left/right directions. |
| `--combo-unit 0` | Disable derived slide combos; this is the default. |
| `--combo-unit 8` | Enable experimental relative-eighth Combo/ComboSkip generation. |
| `--flick-hidden` | Enable experimental hidden nodes for explicit Flick line-slot associations. |

`parse` writes one JSON object to stdout, with notes, BPM/signature events,
additional events, source lines with canonical endpoints/members, warning flags,
source geometry, Call rhythm fractions, Fever indices, bar-line times, final
timing-group IDs, and counts. Diagnostics go to stderr.
Exit codes are `0` for success, `1` for read/parse failures, and `2` for invalid
arguments. `--flick-hidden` normally adds nothing to native SS JSON, whose
standalone Flicks do not carry line-slot associations.

## API Documentation

- [API reference and compatibility policy](docs/api.md): every public function,
  data structures, ownership, geometry conventions, and versioning rules.
- [Public header](include/moenotes_chart_parser.h): the C/C++ interface.
- [Compatibility](docs/compatibility.md): format extensions and unverified behavior.
- [Development](docs/development.md): tests, sanitizers, fuzzing, and release checks.
- [Native differential checks](docs/native-differential.md): external snapshot contract and runner.
- [Changelog](CHANGELOG.md): version history and migration notes.

Versions use `MAJOR.MINOR.PATCH`. Compatible fixes within a minor series
preserve existing declarations and structure layouts.
Experimental algorithm outputs may be corrected without changing the interface.
Breaking changes require an explicitly documented version boundary, never a
silent patch update. See the API reference for the full policy.

## License

Project code is licensed under the [MIT License](LICENSE).
Vendored yyjson is MIT licensed under its [upstream notice](third_party/LICENSE.yyjson).
The external zlib dependency uses the [zlib License](https://zlib.net/zlib_license.html).
These licenses do not grant rights to third-party charts or game assets.

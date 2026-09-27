# Development and public-source checks

Keep production source, synthetic fixtures, English documentation, dependency
locks and unchanged upstream notices in this repository. Do not commit real game
charts, master rows, APKs, recovered native code, account responses, credentials,
device traces, research harnesses, raw benchmarks, machine paths or build output.
Aggregate evidence counts and hashes are allowed; never confuse them with a
reproducible independent native test included in this repository.

Before a source commit intended for push:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DMNS_SANITIZE=ON
cmake --build build-sanitize --parallel 4
ctest --test-dir build-sanitize --output-on-failure
git diff --check
git diff --cached --check
```

Format owned C/C++ source and headers with clang-format23 using `.clang-format`;
never reformat vendored upstream files. Check both GCC and Clang locally where
available. Changes to the numerical runtime must preserve binary32 intermediate
rounding; fast-math and contraction are prohibited. Changed stochastic solvers
need exhaustive small-tree/DAG comparisons, invalid-input cases, memory stops and
slice/cancel reproducibility checks. Never refresh a failed golden automatically.

When changing the browser/ABI boundary, build with `scripts/build_web.sh`, run
`node tests/wasm.mjs`, and execute `tests/browser.py` using Playwright/Chromium.
Compare C ABI and WASM results at fixed work counts. Test the installed CMake
package with `tests/consumer`. Changes to input decoding should run the optional
Clang `MNS_BUILD_FUZZER` target with sanitizers.

Review the code and staged files independently after benchmarks. Update
CHANGELOG, public contracts and VALIDATION. Scan the exact staged tree and Git
history with a secret scanner and inspect every finding. Narrowly allowlisted
SHA256 manifest values must be independently recomputed, not accepted by name.
Verify that staging excludes local data, binaries, source caches and nested Git
metadata. Preserve third-party license notices and fixed-source manifests.

`scripts/generate_evidence_summary.py` refreshes aggregate evidence hashes when
the research workspace is available; refreshing hashes does not rerun a test.
Keep device/native acceptance gaps visible until independently closed. CI jobs
for unexecuted platforms are configuration, not validation evidence.

A public source push does not publish a tag, GitHub Release, binary package,
container or game asset. Those are separate actions. No implicit game-network
or device access belongs in this library or its repository tests.

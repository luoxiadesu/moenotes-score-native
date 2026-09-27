# Development

## Build and Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Assertions remain enabled in Release tests. The API contract test pins the
retained declarations and v0.3.0 additions:
enum values, public structure field types/offsets/sizes, function signatures,
defaults, null handling, and selected behavioral contracts. It is a regression
guard, not an exhaustive ABI or semantic proof. Do not change the baseline
merely to make an incompatible change pass.

GitHub Actions runs Release regressions, installed consumers, and a Clang
ASan/UBSan configuration on pushes and pull requests. The external game-chart
corpus is deliberately not uploaded or used by CI.

Test installed C and C++ consumers independently of source include paths:

```sh
cmake --install build --prefix "$PWD/build/install"
cmake -S tests/consumer -B build/consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/consumer --parallel
ctest --test-dir build/consumer --output-on-failure
```

An optional Python 3 runner accepts an external corpus. Do not add real charts
to this repository:

```sh
python3 tests/check_corpus.py build/moenotes-chart-parser /path/to/MusicScore
```

Independently produced native snapshots can also be checked without embedding
game materials in this repository:

```sh
python3 tests/check_native.py build/moenotes-chart-parser /path/to/MusicScore /path/to/snapshots
```

See [the snapshot contract](native-differential.md). Record native failures and
coverage separately; never replace native fields with parser output.

## Sanitizers and Fuzzing

```sh
cmake -S . -B build-sanitize -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

```sh
cmake -S . -B build-fuzz -DCMAKE_C_COMPILER=clang \
  -DMOENOTES_BUILD_FUZZER=ON -DMOENOTES_BUILD_CLI=OFF \
  -DMOENOTES_BUILD_TESTS=OFF
cmake --build build-fuzz --parallel
./build-fuzz/moenotes-chart-parser-fuzz -max_total_time=60
```

Fuzz and sanitizer success is robustness evidence, not gameplay equivalence.

## Release Checklist

1. Review changes against the [API policy](api.md#version-and-stability).
2. Update CMake project version and the four public version macros together;
   the contract test checks they agree. Use `MAJOR.MINOR.PATCH`.
3. Record additions, fixes, output changes, and migration requirements in
   `CHANGELOG.md`. Keep experimental behavior explicitly labeled.
4. Run regression tests, installed C/C++ consumer tests, sanitizer tests, and
   the available external-corpus runner. Compile the README example unchanged.
5. Inspect the staged diff for secrets, game charts, native binaries, local
   paths, and build artifacts. Preserve third-party notices.
6. Commit the release and create an annotated `vMAJOR.MINOR.PATCH` tag after
   verification. Push only the intended branch and tag; do not move old tags.

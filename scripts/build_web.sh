#!/usr/bin/env bash
set -euo pipefail
mns_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$mns_root/dist"
emcc --use-port=zlib -x c -c /dev/null -o "$mns_root/dist/zlib-check.o"
rm "$mns_root/dist/zlib-check.o"
emcmake cmake -S "$mns_root" -B "$mns_root/build-wasm" -DCMAKE_BUILD_TYPE=Release -DMNS_BUILD_TESTS=OFF -DMNS_BUILD_CLI=OFF
cmake --build "$mns_root/build-wasm" --parallel 4
(cd "$mns_root/web" && npm ci --ignore-scripts && npm run build)
cp "$mns_root/build-wasm/moenotes-score.mjs" "$mns_root/build-wasm/moenotes-score.wasm" "$mns_root/web/worker.mjs" "$mns_root/dist/web/"

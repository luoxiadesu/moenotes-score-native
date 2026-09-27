# Third-party sources

- `vendor/chart-parser`: MoeNotes Chart Parser 0.3.0, commit
  `482d489ce41442811e7ca1e278c7e0ce9788d67a`. Source copied without Git metadata or
  build artifacts. See its `LICENSE`. Includes yyjson under
  `third_party/LICENSE.yyjson`. yyjson is also used by the scoring JSON adapter.
  `src/parser_build.c` includes its unchanged source after system headers and
  scopes a private typedef rename to avoid Darwin's `sig_t` collision.
- `vendor/boost`: Boost 1.83.0 headers needed by Multiprecision/cpp_rational,
  including conditional compiler/library dependencies. Extracted from Ubuntu's
  `libboost1.83-dev 1.83.0-2.1ubuntu3.2` package. See `LICENSE_1_0.txt` and
  `COPYRIGHT`. No Boost binary library is linked.
- zlib: native builds link the system package. The pinned Emscripten 6.0.10 port
  supplies zlib 1.3.2 for WebAssembly. Preserve its license when distributing a
  linked binary.
- TypeScript 5.9.3 is a development dependency of the browser wrapper, pinned in
  `web/package-lock.json`, under Apache-2.0.

No license has been assigned to the new project source by this implementation.
Upstream notices do not grant a license to the new scoring code.

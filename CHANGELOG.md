# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [0.2.0] - 2026-10-21

The public API is backward compatible with 0.1.0: no function, type or field was changed or
removed.

### Added

- Windows support with MSVC: libcurl from vcpkg (`vcpkg.json` manifest), static library and DLL,
  clean at `/W4 /WX`. The DLL exports exactly the functions of `ollama.h`.
- Fuzzing with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer: `fuzz/fuzz_json.c`
  (JSON parser, every response parser, streaming responses) and `fuzz/fuzz_host.c`
  (`OLLAMA_HOST` parsing), a corpus of real Ollama responses and a dictionary. Enable them with
  `-DOLLAMA_BUILD_FUZZERS=ON` (Clang). The corpus and the inputs of past crashes are replayed by
  `ctest` with every compiler.
- Doxygen documentation of the whole API, published on GitHub Pages, and a `docs` CMake target.
- `OLLAMA_WERROR` CMake option to treat warnings as errors.
- CI: Windows (MSVC, static and DLL), fuzzing job, documentation job; Linux runs the tests in a
  locale with a decimal comma.

### Changed

- `ollama_client_new()` returns `NULL` for a host that the Python client rejects with a
  `ValueError` (unbalanced or invalid brackets, ports that are not numbers up to 65535), instead
  of building an invalid URL. With the default client (`NULL`), the functions return
  `OLLAMA_ERROR_REQUEST` and `ollama_last_error(NULL)` explains the problem.
- The CMake package is compatible with any 0.x version from 0.1 (`SameMajorVersion`).
- CI runners are pinned (`ubuntu-24.04`, `macos-15`, `windows-2025`) and use `actions/checkout@v5`.

### Fixed

- Streams: undefined behavior (pointer arithmetic on `NULL`) when a streaming response had an
  empty body. Found by fuzzing.
- JSON: negative zero lost its sign after a round trip (`-0.0` was written as `-0` and read back
  as the integer `0`). Found by fuzzing.
- `OLLAMA_HOST`: hosts with unbalanced brackets produced a URL that changed when parsed again.
  Found by fuzzing.
- Windows: `src/json.c` did not compile because a local variable named `small` collided with the
  `small` macro of the Windows headers (`rpcndr.h`, included through `curl.h`); the library now
  includes the Windows headers with `WIN32_LEAN_AND_MEAN` and `NOMINMAX`.
- Windows: configuring failed because the relocatable pkg-config file was computed with
  `file(RELATIVE_PATH)` on paths like `/lib/pkgconfig`, which are not absolute on Windows.

## [0.1.0] - 2026-10-21

First release: a C99 port of ollama-python with `generate`, `chat`, `embed`, `embeddings`, `pull`,
`push`, `create`, `create_blob`, `list`, `show`, `ps`, `copy`, `delete`, `web_search`,
`web_fetch` and `systemone`, streaming iterators, a JSON API, chat history and tool helpers,
CMake and pkg-config packages, examples and tests.

[0.2.0]: https://github.com/Topogamer0/Ollama-C/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Topogamer0/Ollama-C/releases/tag/v0.1.0

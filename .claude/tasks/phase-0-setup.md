# Phase 0 — Setup & Plugin Skeleton

**Goal**: A loadable but otherwise empty X-Plane 12 plugin. After this phase, X-Plane logs `[xp_swiss_vfr] *** xp_swiss_vfr v0.1.0 by thWelly ***` at startup, the plugin appears in the Plugins menu, and CI builds it green on macOS / Linux / Windows.

**Estimated effort**: 15–25 hours (one focused session if the SDK download cooperates).

**Out of scope**: any VFR data, any UI window, any waypoint registration, any ATC integration. Strictly the skeleton.

---

## Reference

This phase mirrors `../xp_pilot` 1:1, with names and identifiers adapted. Read these xp_pilot files **before writing anything** — they are the canonical blueprints:

| xp_pilot file | What we copy / adapt |
|---|---|
| `CMakeLists.txt` | Top-level CMake, replace `xp_pilot` → `xp_swiss_vfr`, drop `auto_qnh`/`flight_logger`/`html_report`/`logbook_ui` source list |
| `Makefile` | All targets (setup, build, lint, format, test, install, release) — adapt PROJECT_NAME |
| `.clang-format` | Copy verbatim |
| `.clang-tidy` | Copy verbatim |
| `.github/workflows/build.yml` | Job graph (lint → build-{macos,linux,windows} → release) — adapt artifact names |
| `setup.sh` | SDK + ImGui + json.hpp download script — extend to also fetch Catch2 v3 single-include |
| `src/main.cpp` | Plugin entry-point structure (`XPluginStart/Stop/Enable/Disable/ReceiveMessage`, draw callback registration, menu) — strip out feature-specific calls |
| `src/auto_qnh.{cpp,hpp}` | Namespace module pattern (`init()` / `stop()`) — vorbild for our `core::plugin` module |
| `.gitignore` | Already created in bootstrap session |

---

## Deliverables (file-by-file)

### Top-level

- [ ] `VERSION.txt` — single line `0.1.0`.
- [ ] `README.md` — one-page intro: feature description, install instructions (point to GitHub Releases ZIP), build instructions (`make setup && make build && make install`), GPLv3 attribution. Will be expanded in phase 8.
- [ ] `setup.sh` — POSIX shell script. Downloads:
  - X-Plane SDK 4.3 (`https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip`) → `sdk/`
  - Dear ImGui v1.91.9 → `vendor/imgui/`
  - nlohmann/json v3.11.3 single-header → `vendor/json.hpp`
  - Catch2 v3.x amalgamated single-include → `vendor/catch2/catch_amalgamated.{hpp,cpp}`
  - Each download guarded by a sentinel file so re-runs are idempotent.
- [ ] `Makefile` — copy from `xp_pilot/Makefile`. Substitute project name. Add a `test` target that runs `./build/xp_swiss_vfr_tests`.
- [ ] `CMakeLists.txt` — copy from `xp_pilot/CMakeLists.txt`. Changes:
  - `project(xp_swiss_vfr CXX)`
  - `XP_SWISS_VFR_VERSION` define from `VERSION.txt`
  - Source list reduced to skeleton: `src/main.cpp`, `src/core/plugin.cpp`
  - Add the test target (see "Tests" below) gated by `option(BUILD_TESTS "Build unit tests" ON)`.
- [ ] `.clang-format` — copy verbatim from xp_pilot.
- [ ] `.clang-tidy` — copy verbatim from xp_pilot.

### Source skeleton — `src/`

- [ ] `src/version.hpp`
  ```cpp
  #pragma once
  // Set by CMake from VERSION.txt; this is a fallback for IDE indexing.
  #ifndef XP_SWISS_VFR_VERSION
  #define XP_SWISS_VFR_VERSION "0.1.0-dev"
  #endif
  ```
- [ ] `src/core/plugin.hpp`
  ```cpp
  #pragma once
  namespace xpswissvfr::core {
      void init();
      void stop();
  }
  ```
- [ ] `src/core/plugin.cpp` — implements `init()` (logs banner) and `stop()` (logs shutdown). Use `XPLMDebugString` directly (no logging wrapper yet).
- [ ] `src/main.cpp` — entry points only. Calls `core::init()` from `XPluginStart`, `core::stop()` from `XPluginStop`. `XPluginEnable` returns 1, `XPluginDisable` and `XPluginReceiveMessage` are NOPs. Plugin signature: `thWelly.xp_swiss_vfr`.

### CI — `.github/workflows/build.yml`

Copy from `xp_pilot/.github/workflows/build.yml`. Required edits:
- Rename artifacts: `xp_pilot-macos` → `xp_swiss_vfr-macos`, etc.
- ZIP asset name: `xp_swiss_vfr.zip`
- Inside the release job, the bundle structure is:
  ```
  dist/xp_swiss_vfr/
  ├── mac_x64/xp_swiss_vfr.xpl
  ├── lin_x64/xp_swiss_vfr.xpl
  ├── win_x64/xp_swiss_vfr.xpl
  └── resources/                  # (empty in phase 0; populated from phase 1)
  ```
- Lint job runs `make lint` exactly as in xp_pilot (build with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` into `build-lint/`, then `clang-tidy -p build-lint src/**/*.cpp`).

### Tests — minimal smoke harness

Phase 0 only sets up the test infrastructure; real tests come in phase 1.

- [ ] `tests/CMakeLists.txt` — defines target `xp_swiss_vfr_tests`, links Catch2 amalgamated source, adds `tests/test_smoke.cpp`.
- [ ] `tests/test_smoke.cpp` — single trivial Catch2 test (`REQUIRE(1 + 1 == 2)`) so we can verify `make test` runs.
- The test target must NOT link the X-Plane SDK or any plugin source — it stays standalone.

---

## Verification

Run in order. Stop at first failure and fix before continuing.

1. `make setup` — completes with no errors. `sdk/XPLM/XPLMPlugin.h` and `vendor/json.hpp` and `vendor/catch2/catch_amalgamated.hpp` all exist.
2. `make build` — produces `build/xp_swiss_vfr.xpl`. `file build/xp_swiss_vfr.xpl` shows a Mach-O universal binary with both `arm64` and `x86_64` slices.
3. `make lint` — exits 0. Any `bugprone-*` finding is a hard error per `.clang-tidy`.
4. `make format` — leaves working tree clean (no diff).
5. `make test` — runs the Catch2 smoke test, prints `All tests passed (1 assertion in 1 test case)`.
6. `make install` — copies `build/xp_swiss_vfr.xpl` to `~/X-Plane 12/Resources/plugins/xp_swiss_vfr/mac_x64/xp_swiss_vfr.xpl`, codesigns it.
7. Launch X-Plane 12. `~/X-Plane 12/Log.txt` contains `[xp_swiss_vfr] *** xp_swiss_vfr v0.1.0 by thWelly ***`. The plugin shows up under Plugins menu.
8. Push a feature branch and open a PR. CI must run all jobs green: `lint`, `build-macos`, `build-linux`, `build-windows`. (No release job — that only fires on tags.)

If step 8 fails on Linux or Windows, debug the platform defines in `CMakeLists.txt` against xp_pilot's working version. The most common cause is mismatched `APL/IBM/LIN` defines or missing platform-specific link libraries.

---

## Acceptance criteria

- [ ] `git status` shows the new files; `git ls-files` does not include anything from `sdk/`, `vendor/`, `build/`, or `build-lint/`.
- [ ] All seven local verification steps pass.
- [ ] CI is green on macOS, Linux, and Windows.
- [ ] A naive new contributor can clone, run `make setup && make build`, and get a working `.xpl` without reading any other documentation.

## Hand-off to phase 1

Phase 1 will add the `data/` namespace, JSON loading, the first airport file (`LSZG_grenchen.json`), and real Catch2 tests. The plugin will then log `[xp_swiss_vfr] Loaded 1 VFR airport: LSZG (Grenchen)` at startup. See `phase-1-data-model.md`.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**xp_swiss_vfr** is a C++17 X-Plane 12 plugin that adds Swiss VFR approach procedures to the simulator. It fills the gap left by Navigraph CIFP, which only ships IFR procedures — Swiss sport airfields (Birrfeld, Schänis, Saanen, Grenchen, Lommis, Bex, Speck-Fehraltorf, …) have no procedure support out of the box. The plugin lets the pilot select an airfield and an approach sector (E/S/W/N), injects the mandatory reporting points as user waypoints, populates the active flight plan, and surfaces altitude restrictions and waypoint reminders.

License: GPLv3.

Status: Bootstrap. See `.claude/tasks/` for the per-phase implementation plans.

## Tech Stack

- **Language**: C++17
- **Build**: CMake 3.21+, GNU Make wrapper
- **Platforms**: macOS Universal (arm64 + x86_64), Linux x86_64, Windows x86_64 — all built in CI
- **X-Plane SDK**: 4.3 (XPSDK430), vendored under `sdk/` via `make setup`
- **Dependencies** (vendored under `vendor/` via `make setup`):
  - [Dear ImGui](https://github.com/ocornut/imgui) v1.91.9 — UI windows (used from phase 4)
  - [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 — JSON parsing for VFR data files
  - [Catch2](https://github.com/catchorg/Catch2) v3.x — unit tests for domain logic
- **Tooling**: clang-format, clang-tidy

## Commands

```bash
make setup          # download X-Plane SDK + ImGui + json.hpp + Catch2 into sdk/ and vendor/
make build          # CMake Release build → build/xp_swiss_vfr.xpl
make build-windows  # cross-targeted Windows build (used by CI)
make test           # build and run the Catch2 test executable (xp_swiss_vfr_tests)
make sanitize       # build and run the tests under ASan + UBSan (build-sanitize/, plugin .xpl excluded)
make lint           # clang-tidy against build-lint/ with compile_commands.json (mirrors xp_pilot)
make format         # clang-format -i src/**/*.{cpp,hpp}
make install        # codesign and copy the .xpl + resources/ into ~/X-Plane 12/Resources/plugins/xp_swiss_vfr/
make release VERSION=x.y.z   # bump VERSION.txt, tag, push
make clean          # remove build/, build-lint/ and build-sanitize/
```

The plugin is also tested manually inside X-Plane 12 — domain-logic unit tests do not cover SDK integration.

## Architecture

The plugin is split into namespaces, each owning one concern. The lifecycle pattern mirrors `xp_pilot`: every module exposes `init()` / `stop()` and is wired up from `main.cpp`.

```
src/
├── main.cpp                 # XPlugin* entry points, registers draw + flight loop callbacks
├── version.hpp              # XP_SWISS_VFR_VERSION constant
├── core/                    # plugin lifecycle, logging helpers, X-Plane resource paths
├── data/                    # VfrAirport / Waypoint / Runway domain model + JSON loader + validation
├── geometry/                # great-circle distance, bearing, position-in-radius checks
├── procedures/              # ActiveProcedure state machine, sector-to-flightplan resolution
├── waypoints/               # X-Plane user-waypoint registration / cleanup (phase 2+)
├── ui/                      # ImGui windows (phase 4+)
├── integration/             # optional XPLMSendMessageToPlugin bridge to xp_wellys_atc (phase 7+)
└── persistence/             # settings.json read/write
```

Data flow:

```
resources/airports/*.json  →  data::VfrAirportDatabase  →  procedures::ActiveProcedure
                                                            ↓
                                                      waypoints::register
                                                            ↓
                                                      X-Plane SDK (FMS + user waypoints)
```

Test code lives under `tests/` and links the same `data/`, `geometry/`, `procedures/` translation units as the plugin — but never the X-Plane SDK.

## Code Quality

All implementation in this repo must follow clean-code best practices. This applies to every change, now and in the future:

- **Readability over cleverness**: optimise for the next reader, not for shorter code.
- **Few branches**: prefer polymorphism, lookup tables (`std::map` / `std::unordered_map`), early-return guard clauses, and the strategy pattern over `if`/`switch` cascades. A long chain of `if (type == X)` is a smell — replace it with dispatch.
- **Single responsibility**: each function does one thing; each module owns one concern.
- **Small functions**: ~30 lines is a soft ceiling. Extract before you nest.
- **Meaningful names**: variables, functions, and types read as plain English — no abbreviations, no cryptic suffixes.
- **DRY, but not speculative**: extract once you have a third occurrence; don't abstract for hypothetical future needs.
- **Encapsulation**: keep statics/internals private to their translation unit; expose only what the header promises.
- **Separation of concerns**: UI code never touches file I/O directly; data modules never draw; SDK calls live in dedicated wrappers.
- **Errors as values**: parsing and validation return `std::variant<T, Error>` or `std::optional<T>`. Reserve exceptions for truly unrecoverable situations.
- **Minimal comments**: let the code explain itself. Add a comment only when the *why* is non-obvious (invariant, workaround, surprising constraint). Don't comment what the code already says.
- **Boundaries only for validation**: trust internal code; validate at the edges (JSON parsing, SDK callbacks, user input).

## Naming Conventions

- Functions and variables: `snake_case`
- Structs and classes: `PascalCase`
- Static / file-local variables: `s_` prefix (e.g. `s_database`, `s_window`)
- Constants and macros: `UPPER_SNAKE_CASE`
- Namespaces: `lower_snake_case` (`xpswissvfr::data`, `xpswissvfr::geometry`)
- Files: `snake_case.cpp` / `snake_case.hpp`

Code, comments, commit messages, and documentation are written in **English**. User-facing UI strings can be localised (DE/EN/CH-DE) from phase 4 onward.

## Testing

- `tests/` contains Catch2 test cases for everything that does not require a running X-Plane:
  - `data/` — JSON parsing and validation against fixture files under `tests/fixtures/`
  - `geometry/` — distance, bearing, radius checks
  - `procedures/` — state-machine transitions
- The `xp_swiss_vfr_tests` CMake target is built and executed by `make test` and by CI on every push.
- SDK-dependent code (`waypoints/`, `ui/`, `core/`) is verified manually inside X-Plane 12.

## Data Files

VFR airport data lives under `resources/airports/<ICAO>_<lowercase_name>.json`, one file per airfield. The JSON shape follows the schema in `resources/schemas/vfr_airport.schema.json`. `data::VfrAirportDatabase::load_from_directory` reads every file at plugin start, validates it, and either ingests it or logs a structured error and skips it (graceful degradation — one bad file never breaks plugin load).

The active resource directory is resolved relative to the plugin install location via `XPLMGetPluginInfo`, identical to `xp_pilot`.

## Inter-Plugin Integration

`XPluginReceiveMessage` is wired up but inert until phase 7. From phase 7 onward, `xp_swiss_vfr` will optionally publish events (`PROCEDURE_ACTIVATED`, `WAYPOINT_REACHED`, `APPROACH_COMPLETED`) to `xp_wellys_atc` via `XPLMSendMessageToPlugin`. The integration is failure-tolerant: if the ATC plugin is not loaded, the VFR plugin behaves identically.

## Build Output and Deployment

- Build output: `build/xp_swiss_vfr.xpl`
- Install layout (mirrors xp_pilot):
  ```
  ~/X-Plane 12/Resources/plugins/xp_swiss_vfr/
  ├── mac_x64/xp_swiss_vfr.xpl
  ├── lin_x64/xp_swiss_vfr.xpl
  ├── win_x64/xp_swiss_vfr.xpl
  └── resources/
      ├── airports/*.json
      └── schemas/vfr_airport.schema.json
  ```
- Compiler flags: `-Wall -Wextra -fvisibility=hidden`, `GL_SILENCE_DEPRECATION` on macOS, `/W3` on MSVC.

## Reference Project

`../xp_pilot` is the structural blueprint for this repo. When in doubt about layout, build flags, CI config, or module pattern — read the corresponding file there first.

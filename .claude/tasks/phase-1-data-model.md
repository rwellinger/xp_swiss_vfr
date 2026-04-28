# Phase 1 — Domain Model & JSON Loading

**Goal**: VFR airport data can be defined in JSON, loaded at plugin start, validated, and held in memory. The first real airport — **LSZG (Grenchen)** — exists as a fully populated data file. Catch2 unit tests cover parsing and validation. Phase 1 does **not** touch the X-Plane SDK beyond the existing `XPLMDebugString` log calls; that is phase 2.

**Estimated effort**: 20–30 hours.

**Prerequisites**: Phase 0 complete (skeleton plugin loads, build/test/lint/CI all green).

**Out of scope**: User waypoint registration in X-Plane (phase 2), procedure activation logic (phase 3), any UI (phase 4), more airports than LSZG (phase 5).

---

## Reference

- `../xp_pilot/src/flight_logger.cpp` — example of namespace-scoped data loading from JSON files via nlohmann/json.
- `../xp_pilot/data/flight_logger_profiles.json` — example of a vendor JSON layout.

The loader must avoid `if`/`switch` cascades. Pattern: each `from_json(json, T&)` is a single-purpose free function; validation is a vector of `Validator` lambdas iterated uniformly. See "Implementation style" below.

---

## Domain model — `src/data/`

All structs are plain aggregates. Member order matches the JSON layout for readability.

### `coordinate.hpp`
```cpp
#pragma once
namespace xpswissvfr::data {
    struct Coordinate {
        double lat;   // degrees, [-90, 90]
        double lon;   // degrees, [-180, 180]
    };
}
```

### `waypoint.hpp`
```cpp
#pragma once
#include <optional>
#include <string>
#include "coordinate.hpp"

namespace xpswissvfr::data {
    struct Waypoint {
        std::string          name;
        Coordinate           position;
        std::optional<int>   altitude_ft;
        std::optional<int>   altitude_ft_min;
        std::optional<int>   altitude_ft_max;
        bool                 mandatory_report = false;
    };
}
```

### `runway.hpp`
```cpp
#pragma once
#include <string>

namespace xpswissvfr::data {
    struct Runway {
        std::string designator;       // "06", "24L"
        double      heading_true;     // degrees
        int         length_m;
        std::string surface;          // "asphalt", "grass", "concrete"
        std::string circuit_pattern;  // "left", "right"
    };
}
```

### `approach_sector.hpp`
```cpp
#pragma once
#include <string>
#include <vector>
#include "waypoint.hpp"

namespace xpswissvfr::data {
    struct ApproachSector {
        std::string           id;                    // "E", "S", "W", "N"
        std::string           name;                  // "Sector East"
        Waypoint              entry_point;
        std::vector<Waypoint> transition_waypoints;
        std::string           joins_circuit_at;      // "downwind_24", "base_06", ...
    };
}
```

### `vfr_airport.hpp`
```cpp
#pragma once
#include <map>
#include <string>
#include <vector>
#include "approach_sector.hpp"
#include "coordinate.hpp"
#include "runway.hpp"

namespace xpswissvfr::data {
    struct CircuitPattern {
        int    altitude_ft_agl;
        double downwind_offset_nm;
    };

    struct AirportMetadata {
        std::string source;
        std::string last_updated;     // ISO-8601 date
        std::string verified_by;
        std::string notes;
    };

    struct VfrAirport {
        std::string                          icao;
        std::string                          name;
        int                                  elevation_ft;
        Coordinate                           arp;
        std::vector<Runway>                  runways;
        std::vector<ApproachSector>          approach_sectors;
        CircuitPattern                       circuit_pattern;
        std::map<std::string, std::string>   frequencies;   // {"info": "121.235", "ground": ""}
        AirportMetadata                      metadata;
    };
}
```

### `vfr_airport_database.hpp`
```cpp
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "vfr_airport.hpp"

namespace xpswissvfr::data {
    struct LoadResult {
        int loaded = 0;
        int failed = 0;
        std::vector<std::string> errors;   // human-readable, one per skipped file
    };

    class VfrAirportDatabase {
    public:
        LoadResult                  load_from_directory(const std::filesystem::path& dir);
        const VfrAirport*           find(const std::string& icao) const;
        std::vector<std::string>    list_icao_codes() const;
        std::size_t                 size() const { return airports_.size(); }

    private:
        std::map<std::string, VfrAirport> airports_;
    };
}
```

### `parse_error.hpp`
```cpp
#pragma once
#include <string>

namespace xpswissvfr::data {
    struct ParseError {
        std::string file;
        std::string message;
    };
}
```

---

## JSON loading — `src/data/json_loader.{hpp,cpp}`

**Public API**:
```cpp
namespace xpswissvfr::data {
    std::variant<VfrAirport, ParseError>
    parse_airport(const std::filesystem::path& file);
}
```

**Implementation style**:
- Use nlohmann/json `from_json(const json&, T&)` ADL hooks for every struct above. Each hook is a 5–15 line free function — no branching beyond `j.at(...).get_to(...)` and `j.value("optional_key", default)`.
- Wrap top-level parsing in a single try/catch around `nlohmann::json::exception` and convert it to `ParseError`. No exceptions cross the public API.
- The optional altitude fields use `j.contains("altitude_ft") ? j["altitude_ft"].get<int>() : std::nullopt` — exactly one ternary per optional, no nested branching.

---

## Validation — `src/data/validation.{hpp,cpp}`

**Public API**:
```cpp
namespace xpswissvfr::data {
    std::vector<std::string> validate(const VfrAirport& airport);  // empty = OK
}
```

**Implementation style — table-driven, no `if`-cascades**:
```cpp
using Validator = std::function<std::optional<std::string>(const VfrAirport&)>;

static const std::vector<Validator> VALIDATORS = {
    check_icao_format,
    check_arp_in_range,
    check_at_least_one_runway,
    check_at_least_one_sector,
    check_unique_sector_ids,
    check_runway_headings_in_range,
    check_altitude_bands_consistent,
    // ... add new rules by appending to this list
};

std::vector<std::string> validate(const VfrAirport& airport) {
    std::vector<std::string> errors;
    for (const auto& v : VALIDATORS) {
        if (auto err = v(airport)) errors.push_back(*err);
    }
    return errors;
}
```

Each `check_*` function is short, named for the rule, and returns `std::nullopt` on success.

**Rules to implement**:
| Rule | Detail |
|------|--------|
| ICAO format | exactly 4 uppercase letters |
| ARP in range | `lat ∈ [-90, 90]`, `lon ∈ [-180, 180]` |
| Runways present | `!runways.empty()` |
| Sectors present | `!approach_sectors.empty()` |
| Unique sector IDs | no duplicate `ApproachSector::id` |
| Runway heading | `0 ≤ heading_true < 360` |
| Altitude band consistent | for any waypoint with both `min` and `max`, `min ≤ max` |

---

## Resource layout

- [ ] `resources/schemas/vfr_airport.schema.json` — informative JSON Schema describing the format. Manually maintained, not enforced at runtime (the validator above is the runtime authority).
- [ ] `resources/airports/LSZG_grenchen.json` — first real data file.

### LSZG content (research checklist for the implementing session)

The implementing agent is expected to consult the **Swiss VFR Manual** (or equivalent VAC chart for LSZG) for these fields. Do not invent coordinates.

```json
{
  "icao": "LSZG",
  "name": "Grenchen",
  "elevation_ft": 1411,
  "arp": { "lat": 47.1816, "lon": 7.4172 },
  "runways": [
    { "designator": "06", "heading_true": 60.0, "length_m": 1200, "surface": "asphalt", "circuit_pattern": "left" },
    { "designator": "24", "heading_true": 240.0, "length_m": 1200, "surface": "asphalt", "circuit_pattern": "right" }
  ],
  "approach_sectors": [
    { "id": "W", "name": "Sector West",  "entry_point": { "name": "GRENCHEN-W", "lat": …, "lon": …, "altitude_ft_max": …, "altitude_ft_min": …, "mandatory_report": true }, "transition_waypoints": [], "joins_circuit_at": "downwind_24" },
    { "id": "N", "name": "Sector North", "entry_point": { "name": "GRENCHEN-N", "lat": …, "lon": …, "altitude_ft_max": …, "altitude_ft_min": …, "mandatory_report": true }, "transition_waypoints": [], "joins_circuit_at": "downwind_06" },
    { "id": "E", "name": "Sector East",  "entry_point": { "name": "GRENCHEN-E", "lat": …, "lon": …, "altitude_ft_max": …, "altitude_ft_min": …, "mandatory_report": true }, "transition_waypoints": [], "joins_circuit_at": "downwind_06" },
    { "id": "S", "name": "Sector South", "entry_point": { "name": "GRENCHEN-S", "lat": …, "lon": …, "altitude_ft_max": …, "altitude_ft_min": …, "mandatory_report": true }, "transition_waypoints": [], "joins_circuit_at": "downwind_24" }
  ],
  "circuit_pattern": { "altitude_ft_agl": 1000, "downwind_offset_nm": 0.7 },
  "frequencies": { "info": "121.235", "ground": "" },
  "metadata": {
    "source": "Swiss VFR Manual …",
    "last_updated": "2026-04-…",
    "verified_by": "thWelly",
    "notes": ""
  }
}
```

ARP coordinates above (47.1816 / 7.4172) are placeholder approximations from public sources and **must be re-verified against the VFR Manual** before commit.

---

## Plugin integration — extend `src/main.cpp` and `src/core/plugin.cpp`

In `XPluginStart` (after the existing banner log):
1. Resolve resource path via `XPLMGetPluginInfo` (same idiom as `xp_pilot/src/flight_logger.cpp:data_dir()`).
2. Construct `data::VfrAirportDatabase`, call `load_from_directory(plugin_root / "resources" / "airports")`.
3. Log: `[xp_swiss_vfr] Loaded N VFR airports: ICAO1, ICAO2, ...` (or `Loaded 0 VFR airports — no data files found.` if empty).
4. For each `LoadResult::error`, log a single line: `[xp_swiss_vfr] WARNING: skipped <file>: <message>`.

The database instance lives as a `static` inside `core::plugin.cpp`. Expose a `core::airport_database()` accessor for later phases.

---

## Tests — `tests/`

Replace the smoke test from phase 0 with real coverage. The test target must remain plugin-free (no SDK).

- [ ] `tests/fixtures/lszg_valid.json` — copy of the real LSZG file, used as the happy path.
- [ ] `tests/fixtures/missing_icao.json` — same data with the `icao` field deleted.
- [ ] `tests/fixtures/bad_lat.json` — same data with `arp.lat` set to 999.
- [ ] `tests/fixtures/duplicate_sector_id.json` — two sectors with `id: "E"`.
- [ ] `tests/fixtures/empty_runways.json` — `runways: []`.
- [ ] `tests/test_json_loader.cpp` — `parse_airport` returns `VfrAirport` for the valid fixture; returns `ParseError` for malformed JSON; populates every field of the valid fixture correctly.
- [ ] `tests/test_validation.cpp` — one `TEST_CASE` per validator rule, each pointing at a fixture that triggers exactly that rule. Also: a clean fixture returns an empty error list.
- [ ] `tests/test_database.cpp` — `load_from_directory` on a fixtures dir loads the valid file, skips the malformed ones, returns `LoadResult{loaded=1, failed=4, errors.size()=4}`. `find("LSZG")` returns non-null. `list_icao_codes()` is `["LSZG"]`.

CMake: add the new test files to `tests/CMakeLists.txt`. The fixtures directory is copied to the test working directory at build time via `add_custom_command(POST_BUILD ...)` or `configure_file`.

---

## Verification

1. `make test` — all Catch2 tests green.
2. `make lint` — no new clang-tidy findings.
3. `make build && make install` — plugin installs.
4. Launch X-Plane 12. `Log.txt` contains exactly:
   ```
   [xp_swiss_vfr] *** xp_swiss_vfr v0.1.0 by thWelly ***
   [xp_swiss_vfr] Loaded 1 VFR airport: LSZG (Grenchen)
   ```
5. Manually break `LSZG_grenchen.json` (e.g. delete a closing brace), reinstall, restart X-Plane.
   ```
   [xp_swiss_vfr] *** xp_swiss_vfr v0.1.0 by thWelly ***
   [xp_swiss_vfr] WARNING: skipped LSZG_grenchen.json: parse error at line ...
   [xp_swiss_vfr] Loaded 0 VFR airports — no data files found.
   ```
   Plugin still loads cleanly. Restore the file.
6. CI green: lint + 3 build jobs + tests on every platform.

---

## Acceptance criteria

- [ ] All eight `check_*` validator rules implemented and tested.
- [ ] LSZG coordinates and altitude bands cross-checked against an authoritative VFR source — placeholder values replaced before merge.
- [ ] Test fixtures cover at least: valid, malformed JSON, missing required field, out-of-range coordinate, duplicate sector ID, empty runway list.
- [ ] No `if`/`switch` chain longer than two arms in `validation.cpp` or `json_loader.cpp`.
- [ ] All four local verification steps pass; CI green.

## Hand-off to phase 2

Phase 2 will take this database and register the entry-point + transition waypoints as X-Plane user waypoints, so they become searchable in the G1000. That's the first phase that actually calls the SDK navigation API. See `phase-2-waypoints.md` (to be drafted before that phase starts).

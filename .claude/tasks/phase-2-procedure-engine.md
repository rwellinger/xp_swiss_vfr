# Phase 2 — Procedure Engine (Database-Driven Pattern Construction)

**Goal**: Replace the hardcoded `LSZG_RWY06_SECTOR_EAST[]` constant with a procedure that is **constructed at runtime** from `data::VfrAirportDatabase` plus geometric pattern rules. After Phase 2, activating an approach for any airport in the database produces a correct VFR pattern (VRP entry → downwind → base → final → threshold) without code changes — only data changes.

**Estimated effort**: 10–15 hours.

**Prerequisites**: Milestone 1.5 complete. The XPLM410 multi-FPL injection path against `xplm_Fpl_Pilot_Primary` is the validated FMS write strategy.

---

## Data-source strategy (legally and architecturally clean)

The plugin is GPLv3 and intended to be public. Navigraph data is commercially licensed and cannot be re-distributed. Resolution is a **two-layer hybrid**:

1. **Layer 1 — shipped defaults** (`resources/airports/<ICAO>_*.json`): hand-curated from publicly available sources only — Skyguide AIP / VAC, OpenAIP, pilot knowledge. These ship in the GitHub repo and define the authoritative VRP *names* and *plugin-author-supplied* coordinates. Production-quality coordinate verification is on the user / contributor.
2. **Layer 2 — Navigraph runtime override** (optional): if the user has a Navigraph subscription their `~/X-Plane 12/Custom Data/cycle_info.txt` exists. When detected at plugin start, `data::NavigraphSource` parses `Custom Data/earth_fix.dat` for `VP*` / VRP entries matching loaded ICAOs and **overrides Layer-1 coordinates** with the more precise Navigraph values. Navigraph data is read in-process from the user's local files only — never copied into the plugin's repo or distributed. Detection rule: `cycle_info.txt` present → Navigraph available; absent → fall back to Layer 1.

This keeps the plugin (a) fully functional out of the box, (b) precise for users who already pay for Navigraph, (c) legally clean — no Navigraph data ever enters the plugin's distribution path.

**Test platform**: macOS ARM64 (M1), X-Plane 12, default Cessna 172 G1000 (X1000).

---

## In scope

- New `src/geometry/` module: lat/lon distance, bearing, "offset by NM at bearing", great-circle math. SDK-free, fully unit-testable.
- New `procedures::Procedure` value type: a sequence of `Waypoint` that the runner can ingest. Replaces the hardcoded array.
- New `procedures::build_procedure(const VfrAirport&, sector_id, runway_designator) → Procedure`: pattern construction logic. Reads `circuit_pattern.downwind_offset_nm`, runway heading, and airfield elevation; emits the 5-waypoint sequence (sector entry → DW-BEG → DW-END → FAF → THR).
- Runner refactor: `inject_test_procedure()` becomes `activate(const Procedure&)`. The plugin menu still exposes a single test entry — but the test entry now calls `activate(build_procedure(database.find("LSZG"), "E", "06"))`, no constants in the hot path.
- Multi-sector and multi-runway support driven by the LSZG JSON: all 4 sectors × 2 runways covered. Plugin menu grows to expose the relevant combinations (or keeps a single "test" item; UI selection comes in Phase 4).
- Catch2 unit tests for `geometry::*` and `procedures::build_procedure`. Geometry tests use known LSZG numbers as fixtures.

## Out of scope

- ImGui UI for airport / sector / runway selection (Phase 4).
- Procedure state machine ARMED / ACTIVE / COMPLETED (Phase 3).
- Mandatory-reporting notifications, visual hints (Phase 3+).
- Wind / runway selection logic ("RWY 06 vs 24 depending on wind") — Phase 2 takes the runway as a parameter; the chooser is Phase 4 UI.
- Phase-1 refactor or schema changes.
- More airports than LSZG (Phase 5+; but the architecture must already support it).

---

## Architecture

```
data::VfrAirport           geometry::*
       │                        │
       └──────┬─────────────────┘
              ▼
   procedures::build_procedure
              │
              ▼
   procedures::Procedure  ──▶  procedures::activate(Procedure)
                                     │
                                     ▼
                              XPLM410 multi-FPL API
                              (xplm_Fpl_Pilot_Primary)
```

Key design rules:

- `geometry/` and `procedures/build_procedure.cpp` are **SDK-free** and live in the test target. `procedures/runner.cpp` (the SDK-coupled half) stays plugin-only.
- `Procedure` is a plain aggregate (`std::vector<Waypoint>` plus a small header). No state machine yet — Phase 3.
- `build_procedure` is pure: given the same `VfrAirport` + sector + runway, it produces the same `Procedure` byte-for-byte. Trivially unit-testable.
- The runner does not know how patterns are constructed. It only knows how to push a `Procedure` into the FMS and how to remove its tracked range.

---

## Module sketches

### `src/geometry/coordinate_math.hpp`

```cpp
namespace xpswissvfr::geometry {
    double distance_nm(const data::Coordinate& a, const data::Coordinate& b);
    double bearing_deg(const data::Coordinate& from, const data::Coordinate& to);
    data::Coordinate offset(const data::Coordinate& from, double bearing_deg, double distance_nm);
}
```

Implementation: equirectangular approximation is sufficient for VFR-pattern distances (<10 NM); great-circle accuracy is over-engineering. Document the approximation in a header comment.

### `src/procedures/procedure.hpp`

```cpp
namespace xpswissvfr::procedures {
    struct ProcedureWaypoint {
        std::string display_name;   // ≤ 6 chars to avoid X1000 truncation
        data::Coordinate position;
        std::optional<int> altitude_ft;
    };

    struct Procedure {
        std::string airport_icao;       // "LSZG"
        std::string sector_id;          // "E"
        std::string runway_designator;  // "06"
        std::vector<ProcedureWaypoint> waypoints;
    };
}
```

### `src/procedures/build_procedure.hpp`

```cpp
namespace xpswissvfr::procedures {
    std::optional<Procedure> build_procedure(
        const data::VfrAirport& airport,
        const std::string& sector_id,
        const std::string& runway_designator);
}
```

Returns `std::nullopt` when the inputs are inconsistent (sector unknown, runway unknown, sector doesn't join the requested runway's circuit). Errors are logged, not thrown.

### Runner refactor

```cpp
namespace xpswissvfr::procedures {
    void activate(const Procedure& p);
    void clear_active_procedure();
    bool is_active();
    const Procedure* active_procedure();   // optional, for UI in Phase 4
}
```

`activate(const Procedure&)` does the same FMS write loop as the spike, but iterates `p.waypoints` instead of a hardcoded array. The runner does **not** need to know whether a waypoint came from JSON or from a unit test.

---

## Pattern-construction rules (initial — refinable in Phase 3)

For a left-pattern runway with heading `rwy_heading_true` (e.g. 060° for RWY 06) at airport `arp` with `downwind_offset_nm = D`:

| Waypoint | Position | Altitude |
|---|---|---|
| Sector entry | `airport.sector(sector_id).entry_point` (verbatim from JSON) | `(altitude_ft_min + altitude_ft_max) / 2` |
| DW-BEG | runway-mid + D NM at `rwy_heading + 90° (right) for left circuit, then + 1 NM along reverse-runway-heading` | `pattern_alt + 400 ft` |
| DW-END | runway-mid + D NM lateral, − 1 NM along reverse-runway-heading | `pattern_alt` |
| FAF | THR landing-end + 1 NM along reverse-runway-heading (on the centerline) | `pattern_alt − 400 ft` |
| THR | THR landing-end (computed from ARP, runway heading, runway length) | `0` |

`pattern_alt = airport.elevation_ft + airport.circuit_pattern.altitude_ft_agl`.

For a right-pattern runway, mirror the lateral offset.

Display IDs: 6-char hard cap (X1000 truncates beyond that). `RWY06`, `DW-BEG`, etc.

---

## Implementation steps (in order)

1. Branch `phase-2-procedure-engine` from `main`.
2. **Geometry module** — `src/geometry/coordinate_math.{hpp,cpp}` with `distance_nm`, `bearing_deg`, `offset`. Unit tests: known LSZG distances, identity offsets, bearing-roundtrip.
3. **Procedure value type** — `src/procedures/procedure.hpp`. Header-only; no logic yet.
4. **`build_procedure`** — `src/procedures/build_procedure.{hpp,cpp}`. Unit tests: given LSZG fixture airport + sector "E" + runway "06", verify the 5 waypoints match expected coordinates / altitudes within tolerance.
5. **Runner refactor** — `src/procedures/procedure_runner.cpp`: replace the inline iteration over `LSZG_RWY06_SECTOR_EAST[]` with iteration over `Procedure::waypoints`. Menu callback now calls `build_procedure(database.find("LSZG"), "E", "06")` and forwards to `activate`.
6. **Delete the hardcoded array** — `src/procedures/test_procedure.{hpp,cpp}` retired. Tests for that constant are replaced by tests for `build_procedure` against the same expected output.
7. **Sim verification** — `make install`, restart X-Plane, activate the test menu item. Compare X1000 FPL page against the Phase-1.5 reference screenshot (`fms2.jpg`) — should be visually identical with the JSON-correct numbers (E-ENTRY at 47.180/7.550, downwind offset 0.7 NM, pattern alt 2411 ft).
8. **Multi-sector / multi-runway sweep** — extend the plugin menu to `Activate LSZG E/06`, `… N/06`, `… W/24`, `… S/24` (the four valid sector/runway pairs given the JSON). Each is just a different `build_procedure` call.

Steps 2–4 are the substantive work. Steps 5–6 are mechanical. Steps 7–8 are validation and surface area.

---

## Tests

| File | Coverage | Notes |
|---|---|---|
| `tests/test_geometry.cpp` | `distance_nm`, `bearing_deg`, `offset` | Known LSZG distances; identity property `offset(P, b, d) → P'` then `bearing(P, P') ≈ b` and `distance(P, P') ≈ d`. |
| `tests/test_build_procedure.cpp` | `build_procedure` against the loaded LSZG fixture | All 4 sectors × both runways; one negative case (unknown sector) returning `nullopt`. |
| `tests/test_procedure_data.cpp` | retired | Replaced by `test_build_procedure.cpp`. |

Coverage target: every code path in `geometry/` and `build_procedure` exercised; runner stays manually verified in-sim.

---

## Acceptance

- `make build && make test && make lint` all green.
- Activating "LSZG RWY 06 E" via the plugin menu produces the same 5-waypoint sequence in the X1000 FPL page that Milestone 1.5 ended with — but driven by the JSON, not by hardcoded literals.
- `build_procedure` returns valid `Procedure`s for all 4 sectors against LSZG.
- The findings open questions Q2 (geometry correctness) is closed by construction — the geometry now follows the JSON's `downwind_offset_nm` literally.
- Q3 (X1000 6-char truncation) is preserved: `build_procedure` produces IDs ≤ 6 chars.
- Q5 (final on THR vs ARP) is preserved: THR is the explicit endpoint, computed from runway data.

Phase 3 picks up procedure state-machine, mandatory-reporting hooks, and command-bindings.

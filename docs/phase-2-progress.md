# Phase 2 — Progress Snapshot (Session Handoff)

**Last update**: 2026-04-28
**Current branch**: `main` (Phase 2 work uncommitted on working tree)
**Plan**: [`.claude/tasks/phase-2-procedure-engine.md`](../.claude/tasks/phase-2-procedure-engine.md)
**Reference**: [`docs/milestone-1.5-findings.md`](milestone-1.5-findings.md) — Phase-1.5 spike findings still apply.

---

## Where we are

### ✅ Done in this session

1. **Geometry module** (`src/geometry/coordinate_math.{hpp,cpp}`) — `distance_nm`, `bearing_deg`, `offset`. Equirectangular approximation, self-consistent (offset is exact inverse of distance/bearing). 10 Catch2 cases / 32 assertions.
2. **Hybrid data-source strategy decided & documented** in the Phase 2 plan:
   - **Layer 1** (shipped, public): `resources/airports/*.json`, hand-curated from Skyguide AIP / OpenAIP / pilot knowledge. VRP *names* are real-world facts; *coordinates* are plugin-author approximations marked as "needs VAC verification".
   - **Layer 2** (runtime-only, optional): `data::NavigraphSource` (not yet implemented) detects Navigraph via `~/X-Plane 12/Custom Data/cycle_info.txt`, parses `earth_fix.dat` for VP-codes matching loaded ICAOs, and overrides Layer-1 coordinates at plugin start. **Navigraph data never enters this repo** — read-only at runtime, on the user's own subscription.
3. **Schema migration**: `approach_sectors` (semi-invented W/N/E/S) replaced by `vrps[]` (real Skyguide names) + `arrival_routes` (`{runway → [vrp_names]}`).
   - Removed: `src/data/approach_sector.hpp`.
   - Updated: `src/data/vfr_airport.hpp`, `src/data/json_loader.cpp`, `src/data/validation.cpp`.
   - Migrated: `resources/airports/LSZG_grenchen.json` to real AIP VRP names (E, E1, HE, HW, W, S, ABM ALTREU) + provisional arrival routes (`06: [E, E1]`, `24: [W, HW]`).
   - Migrated: 5 test fixtures (`lszg_valid`, `bad_lat`, `missing_icao`, `empty_runways`, `duplicate_vrp_name` — last one renamed from `duplicate_sector_id`).
   - Rewrote: `tests/test_validation.cpp` (16 cases) and updated `tests/test_json_loader.cpp` (vrps + arrival_routes sections).
4. **`procedures::Procedure` value type** (`src/procedures/procedure.hpp`) — header-only aggregate: `airport_icao`, `runway_designator`, `vector<ProcedureWaypoint>`. ProcedureWaypoint = display_name (≤6 chars), Coordinate, optional<int> altitude_ft.
5. **`build_procedure(VfrAirport, runway_designator) → optional<Procedure>`** (`src/procedures/build_procedure.{hpp,cpp}`) — pure, SDK-free. Reads runway + arrival_route + circuit_pattern from the airport JSON and emits: arrival-route VRPs (verbatim, name truncated to 6 chars) → DW-BEG → DW-END → FAF → THR. Uses `geometry::offset` throughout. Right circuit mirrors the lateral offset (heading + 90 vs heading − 90). Returns `nullopt` on unknown runway, missing arrival route, or arrival route referencing an unknown VRP. 8 test cases in `tests/test_build_procedure.cpp` cover RWY 06 (left), RWY 24 (right, mirror geometry), monotonic altitude step-down, name truncation (`ABM ALTREU` → `ABM AL`), and the three nullopt branches.
6. **Runner refactor** (`src/procedures/procedure_runner.{hpp,cpp}`) — replaced `inject_test_procedure()` with `activate(const Procedure&)`. Iterates `Procedure::waypoints` instead of a hardcoded array; always writes via `XPLMSetFMSFlightPlanEntryLatLonWithId` (the airport-navref branch — never used by the previous test data — was dropped). Menu callback `activate_lszg_06_test()` resolves `core::airport_database().find("LSZG")`, calls `build_procedure(*lszg, "06")`, and forwards on success; logs and returns on `nullopt` / unknown airport.
7. **Hardcoded data retired** — deleted `src/procedures/test_procedure.{hpp,cpp}` and `tests/test_procedure_data.cpp`; CMake entries removed.
   - **Build/test/lint all green**: 184 assertions / 44 cases; lint exit 0.

### 🔜 Next up (Phase 2 plan, in order)

8. **Sim verification** — `make install`, restart X-Plane, activate "Activate LSZG RWY 06 (test)". Compare X1000 FPL page against the reference (`fms2.jpg` from milestone 1.5). Should be visually identical for RWY 06 (with the Layer-1 coordinate approximations + the slightly different geometry that comes from the formula instead of the hand-tuned constants — DW-BEG/DW-END will sit a few hundred metres differently).
9. **Multi-runway** — extend the menu with `Activate LSZG RWY 24`. Trivial: one extra menu item, one extra `MenuItem` enum value, one extra dispatch case calling `build_procedure(*lszg, "24")`.
10. **Navigraph runtime override layer** (`src/data/navigraph_source.{hpp,cpp}`):
   - `bool is_available()` — checks `~/X-Plane 12/Custom Data/cycle_info.txt`.
   - `parse VP-lines from earth_fix.dat` for given ICAO list, return `map<vrp_name, Coordinate>`.
   - `apply_overrides(VfrAirportDatabase&)` — overwrites coords for matching ICAO+VRP-name pairs; logs which VRPs were upgraded.
   - Wired into `core::init()` after `database.load_from_directory(...)`.

---

## Open questions / pending verification

| # | Item | Where | Resolution |
|---|---|---|---|
| 1 | **Pattern-direction for 06 / 24** | `LSZG_grenchen.json` currently has `06: left, 24: right` (provisional, copied from pre-migration value). xp_welly_atc had it reversed (translated: `06: right, 24: left`). Need pilot confirmation. | Ask thWelly explicitly when next session resumes. |
| 2 | **VRP coordinates** | All 7 VRPs in `LSZG_grenchen.json` are author approximations. Real values must be verified against the Skyguide VAC LSZG. | Either resolved by Layer-2 override (if pilot uses Navigraph) or by hand from VAC. Mark JSON `metadata.notes` reflects the status. |
| 3 | **arrival_routes for 06 / 24** | Currently set to `06: [E, E1]` and `24: [W, HW]` — plausibility, not from VAC. | Verify against VAC. |
| 4 | **In-flight leg progression (H6 from milestone 1.5)** | Map rendering is confirmed; AP NAV-mode tracking and leg auto-switch through the pattern not yet flown. | Test in sim during Phase 2 step 7. |
| 5 | **Truncation of display IDs in non-X1000 avionics** | X1000 truncates to 6 chars (confirmed). GNS 430 / GTN 650 limits unknown — relevant for Phase 2 step 4 (`build_procedure` should hard-cap at 6 chars). | Pick 6 chars as the safe ceiling; revisit if other avionics tested. |

---

## Critical context for the next session

- **Architecture is now data-driven**: the runner will receive a `Procedure`, not a hardcoded array. Step 4 (`build_procedure`) is the linchpin.
- **Display-ID rule**: cap at **6 chars** (X1000 truncates `E-ENTRY` → `E-ENTR`).
- **FMS write path**: XPLM410 multi-FPL API against `xplm_Fpl_Pilot_Primary` (with `XPLMSetFMSFlightPlanEntryLatLonWithId` for lat/lon entries; `XPLMSetFMSFlightPlanEntryInfo` for airport navrefs). The legacy `XPLMSetFMSEntryLatLon` writes into a slot the X1000 doesn't display — do not regress.
- **Insert-without-clobber**: append at `XPLMCountFMSFlightPlanEntries(FPL)`; track our range; clear only that range.
- **GPLv3 vs Navigraph licence**: never commit Navigraph data into the repo. Layer-1 ships hand-curated public-source data; Layer-2 reads Navigraph at runtime from the user's `Custom Data/` directory only.
- **DataRefTool is installed** in the user's X-Plane and can serve as a second diagnostic source alongside `Log.txt` (for cross-checking FMS state against what the avionic actually shows).
- **Install path quirk** (already in auto-memory): `make install` deploys to `Resources/available plugins/`, not `Resources/plugins/` — XLauncher convention.

---

## Files map

| Concern | File(s) |
|---|---|
| Phase 2 plan (spec) | [`.claude/tasks/phase-2-procedure-engine.md`](../.claude/tasks/phase-2-procedure-engine.md) |
| This handoff (current state) | [`docs/phase-2-progress.md`](phase-2-progress.md) |
| Phase 1.5 findings (FMS spike, still authoritative for SDK rules) | [`docs/milestone-1.5-findings.md`](milestone-1.5-findings.md) |
| LSZG data (Layer 1) | `resources/airports/LSZG_grenchen.json` |
| Domain model | `src/data/vfr_airport.hpp`, `src/data/waypoint.hpp`, `src/data/runway.hpp`, `src/data/coordinate.hpp` |
| JSON loader | `src/data/json_loader.cpp` |
| Validation | `src/data/validation.cpp` |
| Geometry | `src/geometry/coordinate_math.{hpp,cpp}` |
| FMS runner (still hardcoded; will be refactored in step 5) | `src/procedures/procedure_runner.{hpp,cpp}` |
| Hardcoded data (will be deleted in step 6) | `src/procedures/test_procedure.{hpp,cpp}` |
| Test fixtures | `tests/fixtures/airports/*.json` |
| Tests | `tests/test_*.cpp` |

---

## Resume command for next session

> "Read `docs/phase-2-progress.md`. Phase 2 steps 1–7 are done. Next: I (the user) install via `make install`, restart X-Plane, fly RWY 06 from `Activate LSZG RWY 06 (test)`, and compare against `fms2.jpg`. Then we add RWY 24 to the menu (step 9) and start the Navigraph override layer (step 10)."

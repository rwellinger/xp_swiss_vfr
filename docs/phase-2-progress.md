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
8. **Sim verification (RWY 06 right-circuit)** — confirmed in-sim by thWelly: NAV-mode tracking and waypoint sequencing work end-to-end. The X1000's turn-anticipation arcs at DW-END/FAF render as overlapping at 0 KT (parked) but normalize at pattern speed. Aerobask's own MFD renders the polyline cleanly without anticipation. LNM round-trip confirms the geometry itself is correct.
9. **Pattern direction Q1 resolved** — pilot confirmation: real LSZG flies `06=right`, `24=left`. JSON updated; tests rewritten for the SE-side downwind. Metadata note cites the Aare-bend visual cue.
10. **Pattern dimensions tuned** — `downwind_offset_nm` 0.7 → 1.0 (real-world standard pattern width), and `final_distance_nm` exposed as JSON field on `circuit_pattern`, set to 1.5 NM for LSZG (3.8° glideslope from FAF). Validation rejects non-positive values for both.
11. **Phase 2.5 — Navigraph runtime override layer** (`src/data/navigraph_source.{hpp,cpp}`):
   - `navigraph_is_available(xplane_root)` — detects via `Custom Data/cycle_info.txt`.
   - `parse_navigraph_vrps(earth_fix.dat, icaos)` — earth_fix.dat 1200 format parser; matches lines with IDENT prefix `VP` and TERMINAL_AREA in the requested ICAO set; handles multi-word LONG_NAMEs (e.g. `ABM ALTREU`).
   - `apply_navigraph_overrides(database, overrides)` — replaces matching VRP positions; preserves altitude bands and `mandatory_report` flag; returns stats with per-change log.
   - Wired into `core::init()` after `database.load_from_directory(...)`. Logs are silent if Navigraph is not installed.
   - Tests: 9 cases against fixture `tests/fixtures/navigraph/earth_fix.dat`. Covers detection true/false, multi-airport filter, multi-word names, missing file, unknown ICAO, unknown VRP name, and the preserve-altitude-bands invariant.
   - **Build/test/lint all green**: 239 assertions / 55 cases; lint exit 0.

### 🔜 Next up (Phase 3)

Procedure state machine, mandatory-reporting hooks, command-bindings (per the original plan).

12. **`procedures::ProcedureState`** — IDLE / ARMED / ACTIVE / COMPLETED. Transitions: `activate()` → ARMED; flight-loop detects entry into the procedure airspace → ACTIVE; aircraft passes the last waypoint (RWY threshold) → COMPLETED; `clear()` → IDLE.
13. **Mandatory-reporting hooks** — flight-loop callback samples aircraft position every N seconds; when within X NM of a `mandatory_report` VRP and not yet reported, fires a `WAYPOINT_REPORTED` event (log-only for now; actual UI/ATC integration is Phase 4 / Phase 7).
14. **Command-bindings** — `XPLMCreateCommand` for `xpswissvfr/activate_lszg_06`, `xpswissvfr/clear_procedure`. User can bind to keyboard / joystick.

### 🔜 Out of scope for Phase 2/2.5

15. **Multi-runway menu** — add `Activate LSZG RWY 24`. Trivial; deferred to Phase 4 (where ImGui handles airport/runway selection properly).

---

## Open questions / pending verification

| # | Item | Where | Resolution |
|---|---|---|---|
| 1 | ~~**Pattern-direction for 06 / 24**~~ | ~~`LSZG_grenchen.json` currently has `06: left, 24: right` (provisional)~~ | ✅ **Resolved 2026-04-28**: thWelly confirmed from on-site observation: real-world LSZG flies **06=right, 24=left**. Traffic turns right onto downwind on the SE side over the Aare bend. JSON updated. |
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

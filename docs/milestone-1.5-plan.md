# Milestone 1.5 — FMS-Based Procedure Validation

**Goal**: Decide, with empirical evidence, whether `xp_swiss_vfr` can drive VFR sector approaches by writing waypoints directly into the active FMS flight plan via `XPLMSetFMSEntryLatLon`. Validate the mechanism across the avionics flavours we care about (Laminar X1000 / G1000, default GNS 430, default GTN 650 if available) before committing Phase 2 to this architecture.

**Estimated effort**: 5–8 hours total (3–5 h spike code, 1–2 h cross-avionics manual test, 1 h findings write-up).

**Prerequisites**: Phase 1 complete (LSZG JSON loads cleanly, plugin logs `Loaded 1 VFR airport: LSZG`).

**Test platform**: macOS ARM64 (M1), X-Plane 12, default Cessna 172 G1000.

---

## In scope

- SDK validation of `XPLMSetFMSEntryLatLon` (and friends: `XPLMClearFMSEntry`, `XPLMCountFMSEntries`, `XPLMGetFMSEntryInfo`).
- Cross-avionics behaviour for one hardcoded LSZG procedure (RWY 06, Sector East, ~5 waypoints).
- Altitude-restriction handling per waypoint.
- FPL cleanup (returning the FMS to a sane state after the procedure is "deactivated").
- Plugin-menu trigger ("Plugins → xp_swiss_vfr → Activate LSZG E test", "… → Clear FMS").
- Detailed structured logging around every SDK call.

## Out of scope

- Production UI (no ImGui window — menu items only).
- Procedure-state-machine (ARMED / ACTIVE / COMPLETED).
- Multi-airport support (LSZG hardcoded).
- Multi-procedure support (RWY 06 Sector East hardcoded).
- Reading the procedure from the JSON database (the spike uses constants; Phase 2 wires it to `VfrAirportDatabase`).
- Visual hints, mandatory-reporting-point notifications, ATC integration.
- Refactoring Phase 1 code.

## Pivot rationale (vs. M1.5 v1)

The previous M1.5 plan investigated `user_fix.dat` (persistent user waypoints registered at sim start). After review the FMS-injection path was preferred because it:

- works without sim restart,
- is avionics-agnostic (X1000, GNS 430, GTN 650, default GPS — anything that follows the FMS dataref),
- avoids any conflict with Navigraph's `earth_fix.dat`,
- supports per-waypoint altitude restrictions natively,
- maps cleanly to the user-flow "open plugin → pick airport → pick approach → activate".

The trade-off — and the central thing this milestone validates — is that `XPLMSetFMSEntryLatLon` writes lat/lon entries with **no waypoint identifier of our choosing**: avionics typically render them as a generic placeholder (`LATLON`, `USR`, or empty). Whether that's acceptable, and whether the newer XPLM400 flight-plan API offers a way around it, is one of the things the spike answers.

---

## Hypotheses

Each hypothesis: **Test** (what we do), **Expected** (what we predict), **Pass/Fail** (the binary outcome).

### H1 — `XPLMSetFMSEntryLatLon` inserts lat/lon waypoints into the active FPL

- **Test**: From a plugin-menu callback, call `XPLMSetFMSEntryLatLon(idx, lat, lon, alt)` for indices 0..4 with the LSZG RWY 06 Sector East waypoints. Read back via `XPLMGetFMSEntryInfo`.
- **Expected**: Subsequent `XPLMCountFMSEntries()` returns ≥ 5; `XPLMGetFMSEntryInfo` returns the same lat/lon for each index.
- **Pass**: All five entries present with matching lat/lon (within float precision).
- **Fail**: Crash, no-op, partial write, or coordinates rounded/clipped.

### H2 — Inserted entries are visible and navigable in the Laminar X1000 (default C172 G1000)

- **Test**: With H1 succeeded, open the X1000 MFD FPL page.
- **Expected**: All 5 entries shown in order; the magenta line and map symbology render along the sequence; "Direct-To" the next entry works.
- **Pass**: Sequence visible, map renders, Direct-To moves the active leg.
- **Fail**: Empty FPL page, partial display, no map line, Direct-To fails.

### H3 — Inserted entries are visible in the default Garmin GNS 430

- **Test**: Switch to a default C172 variant equipped with the GNS 430 (or another default aircraft with GNS), repeat injection.
- **Expected**: GNS FPL page shows the 5 entries, map renders the route.
- **Pass**: Same five entries visible, map line drawn.
- **Fail**: GNS doesn't pick up the entries, or only some.

### H4 — Inserted entries are visible in the X-Plane integrated GTN 650 (if available)

- **Test**: Conditional — only run if a default aircraft with the Laminar GTN-style unit is installed. Otherwise mark as "skipped: not applicable".
- **Expected**: GTN FPL page shows the same 5 entries.
- **Pass / Fail / Skipped** as above.

### H5 — Per-waypoint altitude restrictions are honoured

- **Test**: Inject the procedure with `inAltitude` set to (3500, 2800, 2400, 2000, 0) feet for the five waypoints. Inspect the FPL page's altitude column.
- **Expected**: Each waypoint shows its altitude as an "at" or "at-or-above" constraint. `inAltitude == 0` shows blank (no constraint).
- **Pass**: Altitudes match what we wrote.
- **Fail**: Wrong unit (e.g., metres rendered as feet), entries shifted, all blank, or rejected.

### H6 — A 5-entry sequence yields a sensible flight plan

- **Test**: With H1+H2 succeeded, set the destination to entry 4 (or the last entry). Activate the route. Manually fly the sequence.
- **Expected**: Magenta line tracks waypoint-to-waypoint, autoswitches legs as we pass each, finishes at the threshold.
- **Pass**: Smooth leg progression, no premature destination, no waypoint skipping.
- **Fail**: Stuck on first leg, leg-switch on wrong condition, FPL gets clobbered mid-flight.

### H7 — `XPLMClearFMSEntry` cleanly removes our injected entries

- **Test**: After injection, call `XPLMClearFMSEntry(idx)` for each index we wrote. Verify via `XPLMCountFMSEntries` and the avionics FPL page.
- **Expected**: FPL page is empty (or back to whatever was there before).
- **Pass**: Zero residual entries from our injection.
- **Fail**: Stuck entries, only cleared partially, avionics still showing the magenta line.

### H8 — Display label for lat/lon entries (the critical unknown)

- **Test**: After H1, observe what the avionics actually print in the identifier column for each lat/lon waypoint. Try the legacy single-FPL API (`XPLMSetFMSEntryLatLon`) **and**, time permitting, the XPLM400 multi-FPL API (`XPLMSetFlightPlanFMSEntryLatLonWithId` / similar — exact name to be confirmed against `XPLM_Navigation.h`).
- **Expected**: legacy API → generic `LATLON` / `USR` / blank; XPLM400 API → may accept a custom 5-char identifier.
- **Pass**: We end up with a clear decision rule: either accept generic labels, or use the XPLM400 path for nice labels, **or** confirm that custom labels are simply not achievable via SDK.
- **Fail**: Outcome ambiguous or differs unpredictably between avionics. (This is technically a discovery, not a binary fail — but it gates the Phase 2 UX decision.)

### H9 — Behaviour when the FPL is non-empty before injection

- **Test**: Set up a 2-leg FPL via the X1000 (e.g., LSZG → LSZH direct). Then trigger the spike. Observe.
- **Expected**: The spike is documented to clear-then-fill (destructive). Test that this is what actually happens.
- **Pass**: Old entries are gone, new entries are present, no zombie state. Or — if we choose insert-without-clear semantics — old entries are correctly preserved before/after our band.
- **Fail**: Crash, half-old/half-new state, or behaviour differs across avionics.

---

## Test strategy

### Phase A — Spike code in the existing plugin (3–5 h)

Add minimal modules to the existing `xp_swiss_vfr` so we can:

1. Trigger the test procedure injection from an X-Plane Plugins menu item.
2. Trigger the cleanup from a sibling menu item.
3. Read structured log lines around every SDK call.

Files added (proposed paths under `src/procedures/`):

| File | Responsibility | LOC (est.) |
|---|---|---|
| `src/procedures/test_procedure.hpp` | Hardcoded LSZG RWY 06 Sector East waypoint list (5 entries), as a `constexpr` array of `TestWaypoint{ display_name, lat, lon, altitude_ft }`. Pure data; no SDK. | 30–40 |
| `src/procedures/procedure_runner.hpp` | Public API: `init() / stop() / inject_test_procedure() / clear_active_procedure() / is_active()`. Owns plugin-menu state. | 25–35 |
| `src/procedures/procedure_runner.cpp` | Plugin-menu registration via `XPLMAppendMenuItem` + menu-handler callback, calls `XPLMSetFMSEntryLatLon` / `XPLMClearFMSEntry`, structured logging. | 120–160 |

Files modified:

| File | Change | LOC delta |
|---|---|---|
| `src/main.cpp` | Add `procedures::init()` in `XPluginStart`, `procedures::stop()` in `XPluginStop`. | +4 |
| `src/core/plugin.cpp` | (Optional) wire `procedures::init()` from `core::init()` instead of `main.cpp` if the project prefers single-entry. | +0–4 |
| `CMakeLists.txt` | Add the two new `procedures/` sources to `xp_swiss_vfr` target. Tests target does **not** link them (SDK-coupled). | +2 |

Tests:

| File | What it covers | LOC |
|---|---|---|
| `tests/test_procedure_data.cpp` | Catch2 unit tests on the `LSZG_RWY06_SECTOR_EAST` constant — sanity (5 entries, monotonically descending altitudes, lat/lon plausibly inside Switzerland). The runner itself is **not** unit-tested because it calls the SDK; it is verified manually in Phase B. | 40–60 |

Trigger mechanism: **plugin menu only** (no auto-fire, no flight-loop callback). Two items:

- `Plugins → xp_swiss_vfr → Activate LSZG RWY 06 E (test)`
- `Plugins → xp_swiss_vfr → Clear FMS entries`

Logging: keep the existing `[xp_swiss_vfr] …` prefix via `XPLMDebugString`. Around every SDK call, log a single structured line like:

```
[xp_swiss_vfr] FMS inject idx=0 lat=47.180000 lon=7.570000 alt=3500 -> count_after=1
[xp_swiss_vfr] FMS inject idx=1 lat=47.195000 lon=7.500000 alt=2800 -> count_after=2
...
[xp_swiss_vfr] FMS inject done; total_entries=5
```

This gives the Phase B tester a reproducible audit trail in `Log.txt` independent of what the avionics show.

Existing-FPL-handling for the spike (deliberately destructive, for predictability):

```
inject_test_procedure():
    n = XPLMCountFMSEntries()
    for i = n-1 down to 0:
        XPLMClearFMSEntry(i)
    for i in 0..N-1:
        XPLMSetFMSEntryLatLon(i, w.lat, w.lon, w.altitude_ft)
    s_active = true

clear_active_procedure():
    n = XPLMCountFMSEntries()
    for i = n-1 down to 0:
        XPLMClearFMSEntry(i)
    s_active = false
```

This is documented as "spike behaviour" in the code; Phase 2 will revisit it.

### Phase B — Cross-avionics manual verification (1–2 h)

For each avionic:

1. Boot X-Plane 12 with the test aircraft, position at LSZG.
2. Plugins → xp_swiss_vfr → Activate LSZG RWY 06 E (test).
3. Open FPL page on the avionic. Record:
   - count of entries shown
   - identifier column (per H8)
   - altitude column (per H5)
   - whether map line / magenta course renders
   - whether Direct-To the next entry works
4. Engage the AP in NAV mode (or fly manually) along the sequence; observe leg-switching (per H6).
5. Trigger Clear; verify nothing residual (per H7).
6. Test once more starting from a non-empty FPL (per H9).
7. Capture `Log.txt` excerpts and screenshots into `docs/milestone-1.5-screenshots/`.

Avionics matrix (rows filled in `milestone-1.5-findings.md`):

| Avionic | Aircraft | Run? |
|---|---|---|
| Laminar X1000 | Default C172 (G1000) | yes |
| GNS 430 | Default C172 (steam + GNS 430), if installed | conditional |
| GTN 650/750 | Any default aircraft with the integrated GTN, if installed | conditional |
| Default GPS panel | Any default aircraft with KAP/legacy panel | conditional |

### Phase C — Cleanup verification (folded into Phase B)

Already covered by H7 + H9 in the Phase B procedure. No separate sim session needed.

---

## Implementation proposal — Phase A in detail

Module sketch (no production code; types/signatures only, for review):

```cpp
// src/procedures/test_procedure.hpp
namespace xpswissvfr::procedures
{
struct TestWaypoint
{
    const char *display_name;   // human-readable, used only for our logs — FMS may not show it
    double      lat;
    double      lon;
    int         altitude_ft;    // 0 = no altitude restriction
};

extern const TestWaypoint LSZG_RWY06_SECTOR_EAST[];
constexpr std::size_t LSZG_RWY06_SECTOR_EAST_COUNT = 5;
} // namespace xpswissvfr::procedures
```

```cpp
// src/procedures/procedure_runner.hpp
namespace xpswissvfr::procedures
{
void init();                       // creates Plugins-menu items
void stop();                       // tears them down
void inject_test_procedure();      // menu callback target #1
void clear_active_procedure();     // menu callback target #2
bool is_active();                  // useful for Phase B observability and Phase 2 hand-off
} // namespace xpswissvfr::procedures
```

Plugin-menu wiring (in `procedure_runner.cpp`):

```cpp
static XPLMMenuID s_menu        = nullptr;
static int        s_root_idx    = -1;
static bool       s_active      = false;

enum MenuItem : intptr_t { ITEM_ACTIVATE = 1, ITEM_CLEAR = 2 };

static void menu_handler(void * /*menu_ref*/, void *item_ref)
{
    switch (reinterpret_cast<intptr_t>(item_ref))
    {
    case ITEM_ACTIVATE: inject_test_procedure(); break;
    case ITEM_CLEAR:    clear_active_procedure(); break;
    }
}

void init()
{
    s_root_idx = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "xp_swiss_vfr", nullptr, 0);
    s_menu     = XPLMCreateMenu("xp_swiss_vfr", XPLMFindPluginsMenu(), s_root_idx, &menu_handler, nullptr);
    XPLMAppendMenuItem(s_menu, "Activate LSZG RWY 06 E (test)", reinterpret_cast<void *>(ITEM_ACTIVATE), 0);
    XPLMAppendMenuItem(s_menu, "Clear FMS entries",             reinterpret_cast<void *>(ITEM_CLEAR),    0);
}
```

Refactor-readiness for Phase 2: the runner exposes only `init/stop/inject/clear/is_active`. Phase 2 will:

1. Replace the hardcoded `LSZG_RWY06_SECTOR_EAST[]` with a `Procedure` value derived from `data::VfrAirportDatabase`.
2. Replace `inject_test_procedure()` with `activate(const Procedure&)`.
3. Move trigger from menu to ImGui UI.

The runner's internal SDK-call sequence is the part we keep. The data source and the trigger are the parts that change. Keeping the runner SDK-coupled (and not in `data/`) preserves Phase 1's rule that domain logic stays SDK-free and unit-testable.

Estimated effort: **3–5 h** for code + local build + first inject in sim.

---

## Test data — LSZG RWY 06 Sector East

Five plausible waypoints. Coordinates are **not VFR-Manual-verified** — they are good enough to validate the SDK mechanic; data quality comes in Phase 2. Altitudes step down logically toward the runway.

| # | display_name (for logs) | lat | lon | altitude_ft |
|---|---|---|---|---|
| 0 | GRENCHEN-E (sector entry, ~6 NM E of ARP) | 47.180 | 7.570 | 3500 |
| 1 | Transition (~3 NM E, joining downwind) | 47.195 | 7.500 | 2800 |
| 2 | Downwind 06 (abeam THR 06, north of RWY) | 47.190 | 7.420 | 2400 |
| 3 | Final 06 (~1.5 NM SW of THR 06) | 47.167 | 7.380 | 2000 |
| 4 | THR 06 (runway threshold, no alt restriction) | 47.179 | 7.410 | 0 |

LSZG context for cross-checking:

- ARP: 47.1816 N, 7.4172 E (matches `resources/airports/LSZG_grenchen.json`)
- Runway 06/24, 1200 m, true heading 060° / 240°
- Field elevation 1411 ft
- Pattern altitude ~2400 ft AMSL (1000 ft AGL)

Waypoints 0..3 form a left-base / left-circuit join (RWY 06 has left circuit). Waypoint 4 is the threshold; passing through it ends the procedure.

---

## Decision tree for Phase 2

| Phase A/B outcome | Phase 2 architecture |
|---|---|
| All hypotheses pass cleanly (incl. H8 verdict) | Build the full Procedure-Engine + ImGui UI on top of `XPLMSetFMSEntryLatLon`. Acceptance of either generic `LATLON` labels or XPLM400 named entries (whichever H8 confirmed). |
| H1+H2+H7 pass; H5 fails (altitudes ignored) | Architecture pivot: altitudes go into a custom overlay (own ImGui side panel), not into the FMS. Procedure-engine still injects lat/lon. |
| H1 passes only on X1000; H3/H4 fail or differ | X1000 becomes the primary supported avionic; GNS / GTN labelled "best-effort" in Phase 2 docs. |
| H9 reveals destructive-clear is unacceptable | Change Phase 2 spec to "respect existing FPL: insert procedure entries in a contiguous range, restore on clear". Adds complexity to the runner. |
| H1 fails entirely (crash / no-op) | Re-evaluate. Fall-back candidates: (a) `user_fix.dat` from the original M1.5 v1 plan; (b) custom display overlay only (no FMS integration). |

---

## Open questions to resolve before Phase A coding starts

1. **Custom waypoint labels (H8)** — we already know `XPLMSetFMSEntryLatLon` does **not** take an identifier. Are we OK with avionics rendering the entries as `LATLON` / `USR` / blank, **or** must Phase A also probe the XPLM400 multi-flight-plan API (`XPLMSetFlightPlanFMSEntry…`-family) for named entries? The latter doubles Phase A scope — a clean answer here changes the LOC estimate.
2. **Trigger placement** — Plugins-menu only (current proposal), or also a debug command bound to a key/CommandRef so the user can re-trigger without going into the menu?
3. **Existing FPL behaviour** — destructive clear-then-fill (current proposal, simplest), or insert-without-clobber (more code, more correct, but a Phase 2 problem)?
4. **Cleanup semantics** — on `clear_active_procedure`, should we restore the user's pre-injection FPL state? That requires snapshotting before injection. Or just leave the FPL empty?
5. **Aircraft availability** — confirm which default aircraft you have installed: C172 G1000 (X1000) yes; C172 with GNS 430 (steam) — installed? Default GTN 650-equipped aircraft — installed?
6. **Altitude unit** — SDK header says `inAltitude` is feet. The plan assumes feet. Worth a sanity log (`got_back_alt=…`) on read-back via `XPLMGetFMSEntryInfo` to confirm no unit mismatch.
7. **Logging volume** — fine to log every per-index call (~5 lines per inject), or should the spike batch into one summary line? Phase A is the right time to be verbose.
8. **Test in `tests/`** — only the data constant gets unit-tested (the runner calls the SDK and isn't testable in our SDK-free harness). OK as plan?

---

## Step-by-step implementation plan (after plan approval)

1. Branch `m1.5-fms-spike` from `main`.
2. `src/procedures/test_procedure.{hpp,cpp}` — define and export `LSZG_RWY06_SECTOR_EAST[]` and its count constant.
3. `src/procedures/procedure_runner.{hpp,cpp}` — implement `init` / `stop` (menu wiring) and stubs for `inject_test_procedure` / `clear_active_procedure` / `is_active`.
4. `src/main.cpp` — call `procedures::init()` after the existing `core::init()`, and `procedures::stop()` before `core::stop()`.
5. `CMakeLists.txt` — add the two `.cpp` files to the plugin target. Tests target unchanged.
6. `tests/test_procedure_data.cpp` — Catch2 cases: count == 5, every lat in [46.0, 48.0], every lon in [6.0, 9.0], altitudes monotonically non-increasing, last altitude == 0.
7. `tests/CMakeLists.txt` — add `test_procedure_data.cpp` (it does **not** link `procedure_runner.cpp`).
8. Run `make build && make test && make lint`. Fix any issues.
9. Implement `inject_test_procedure` body — destructive clear, then 5× `XPLMSetFMSEntryLatLon`, with structured log lines around each call.
10. Implement `clear_active_procedure` body — destructive clear, log.
11. `make install`, restart X-Plane, run Phase B per the test strategy above.
12. Fill `docs/milestone-1.5-findings.md` row by row.
13. Open a draft PR with the spike code + filled findings; do **not** merge until the user reviews findings and the Phase 2 decision is made.

---

## Findings document

All test results, label observations, and final decisions are captured in [`milestone-1.5-findings.md`](milestone-1.5-findings.md). That document is the bridge into Phase 2.

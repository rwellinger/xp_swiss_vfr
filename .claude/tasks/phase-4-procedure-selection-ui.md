# Phase 4 — Procedure Selection UI

**Goal**: An ImGui window that shows nearby airports (within ~10–20 NM of the aircraft), lists their available approach patterns, and lets the pilot activate one with a single click. The window also surfaces the unambiguous warning that what the plugin draws is **a VFR reference pattern, not an autopilot-flyable procedure** — pilots must hand-fly the approach.

This replaces the test menu items from Phase 2/3 as the pilot-facing entry point. The menu and X-Plane commands stay around as a power-user fallback (and useful for testing without opening the UI), but the window becomes the primary way to interact with the plugin.

**Estimated effort**: 6–10 hours.

**Prerequisites**: Phase 2 + 2.5 + 3 part 1 complete (procedure injection works, state machine tracks lifecycle, Navigraph override available).

---

## Reference design

`xp_welly_atc/src/atc_ui.cpp` is the structural blueprint. Specifically:

- `draw_nearby_airports()` — ImGui section showing airports within a configurable range, refreshed at ~1 Hz, sorted by distance, click-to-select.
- `xplane_context::find_nearby_airports(max_nm, max_count)` — SDK-coupled query against X-Plane's airport database, returns `vector<NearbyAirport>`.
- `xplane_context::lock_airport(icao)` — a "selected airport" pattern that survives across frames; click-to-toggle behaviour.
- Window is toggled via a single command (`atc_ui::toggle()`); both keyboard binding and menu entry call into the same toggle.

We keep the same conventions: throttled refresh, sortable rows, lock/unlock for selection, and a single toggle entry point.

---

## In scope

- New `src/ui/` directory with `procedure_selection_window.{hpp,cpp}`.
- ImGui window with the structure:
  1. **Header** — bold red/orange "VFR REFERENCE ONLY — NOT FLYABLE BY AUTOPILOT" banner, always visible.
  2. **Aircraft state** — current latitude/longitude (small, debug-style).
  3. **Nearby airports list** — every loaded VFR airport within `max_nm` of the aircraft, sorted by distance. Each row shows: ICAO, name, distance NM, list of selectable runways (one button per available runway in `arrival_routes`).
  4. **Active procedure indicator** — when a procedure is active, a row at the top shows `Active: LSZG RWY 06 (state=ARMED)` plus a `Clear` button.
- Toggle the window via:
  - The existing plugin menu (replace the "Activate LSZG RWY 06 (test)" item with "Show procedure selector"; keep "Clear LSZG procedure").
  - A new X-Plane command `xpswissvfr/window/toggle`.
- Throttled refresh: position + nearby list update at ~1 Hz, not every frame.
- The `find_nearby_airports` query stays in `data/` and is fully unit-testable (takes aircraft position and max range as arguments — no SDK calls inside).
- Configurable max range, default 15 NM. Stored in plugin settings (Phase 5 will introduce a real settings file; for now, a constant).

## Out of scope

- Phase 3 part 2 work (flight-loop callback, auto-transitions, mandatory-reporting hooks). The window can show the current state (from `procedures::current_state()`) but does not drive transitions.
- Multi-aircraft / cockpit-sharing considerations.
- Window-position persistence across plugin reloads (Phase 5).
- Localisation (Phase 6).
- Procedure preview on the X-Plane map (drawn waypoints) — this is later UX polish.

---

## Architecture

```
data::VfrAirportDatabase   data::find_nearby_airports
        │                            │
        └─────────┬──────────────────┘
                  ▼
       ui::procedure_selection_window
                  │
                  ├──▶ procedures::build_procedure  (existing)
                  └──▶ procedures::activate          (existing)
                          │
                          ▼
                       FMS (existing)
```

Module layout:

```
src/ui/
├── procedure_selection_window.hpp    # public init/stop/toggle/draw
└── procedure_selection_window.cpp    # ImGui state + draw loop

src/data/
└── nearby_airports.{hpp,cpp}         # SDK-free, takes aircraft Coordinate + database

src/core/
└── plugin.cpp                         # wire up window init/stop, expose toggle command
```

Nothing in `procedures/` changes. The window is a *consumer* of the existing data + procedure API.

### `data::find_nearby_airports` signature

```cpp
struct NearbyAirport {
    std::string icao;
    std::string name;
    double      distance_nm;
    std::vector<std::string> available_runways;  // those with an entry in arrival_routes
};

std::vector<NearbyAirport> find_nearby_airports(
    const VfrAirportDatabase &db,
    const Coordinate         &aircraft_position,
    double                    max_distance_nm,
    std::size_t               max_count);
```

Pure, SDK-free. Reuses `geometry::distance_nm`. Unit tests cover: empty DB, all out of range, sort by distance, max_count cap, runway list correctness.

### `ui::procedure_selection_window` API

```cpp
namespace xpswissvfr::ui {
    void init();         // create XPLMWindow, register draw callback
    void stop();         // teardown
    void toggle();       // show/hide the window
    bool is_visible();
}
```

Internal state: ImGui window position/size, throttled `nearby_cache`, last refresh timestamp.

The plugin's existing menu changes from two test items to:

```
xp_swiss_vfr →
  ☐ Show procedure selector       (toggle the ImGui window)
  Clear active procedure          (existing)
  ──
  About / Help                    (Phase 5+)
```

The `xpswissvfr/activate/lszg/06` command stays for power users who want a one-press activation; the new `xpswissvfr/window/toggle` is the primary UX path.

---

## Mandatory warning text

The header banner is **load-bearing** and must not be hidden in collapsibles or sub-menus. Suggested text:

> ⚠ **VFR REFERENCE PATTERN ONLY**
> The waypoints are a visual reference for hand-flown VFR circuits.
> Garmin-style autopilots cannot track the 90° turns at pattern speed.
> Use as situational awareness, fly the circuit by hand.

Plus, in the activation flow (e.g. when clicking a runway button), consider a small confirmation prompt that re-iterates "VFR only — confirm?" the *first* time per session, then remembers consent.

(Keeping the warning friction-low so it doesn't get clicked through reflexively, but high enough that pilots cannot say they were not warned. Open question for Phase 5: should we log every activation with timestamp + aircraft type, for diagnosing user reports?)

---

## Implementation steps (in order)

1. **`data::find_nearby_airports`** + unit tests. SDK-free, easy.
2. **CMake/ImGui wiring** — pull in `vendor/imgui/`, add to plugin sources, add ImGui+OpenGL backend translation units. Verify a tiny "hello-world" ImGui window draws inside X-Plane (sanity check; no actual UI logic yet).
3. **`ui::procedure_selection_window` skeleton** — `XPLMCreateWindowEx`, ImGui new-frame in `XPLMRegisterDrawCallback` or per-frame, draw a placeholder window with the warning banner only.
4. **Nearby airports list** — wire up `find_nearby_airports`, render the rows with ICAO/name/distance, ~1 Hz refresh throttle.
5. **Runway buttons + activate** — for each row, render one button per `arrival_routes` key. Click → `build_procedure(*airport, runway)` → `activate(*procedure)`. Show error toast / log line on `nullopt`.
6. **Active-procedure indicator** — top section showing `procedures::current_state()` and a Clear button. Visible only when state ≠ IDLE.
7. **Menu integration** — replace test menu items, add `xpswissvfr/window/toggle` command.
8. **First-time-warning consent** — modal overlay on first activation per session.
9. **Manual sim verification** — spawn at LSZG, LSZB, LSZH; window should show different airports as you taxi/fly. Activate, clear, re-activate; state indicator should track.

Steps 1, 4, 6 are unit-testable. Steps 2, 3, 5, 7, 8, 9 require the X-Plane SDK and manual verification.

---

## Tests

| File | Coverage | Notes |
|---|---|---|
| `tests/test_nearby_airports.cpp` | `find_nearby_airports` | Empty DB, single in-range, all out of range, sorted by distance, max_count cap, available_runways matches `arrival_routes` keys. |
| (manual) | UI behaviour | Window opens/closes, list refreshes, activation works, clear works, state indicator updates, warning is unmissable. |

---

## Acceptance

- `make build && make test && make lint` green.
- `make install` + restart X-Plane: window toggles via menu and via the new command.
- Spawning at LSZG: list shows LSZG (and any other Swiss VFR airports loaded) sorted by distance, with `06` and `24` runway buttons. Clicking `06` activates the procedure exactly as before — same FMS injection, same X1000 display.
- Spawning 50 NM away: the list is empty (or shows a "no airports nearby" hint).
- The VFR-only warning is visible the entire time the window is open.
- Phase 3 part 2 work (state machine auto-transitions) is unaffected — the window only *displays* state, it does not drive it.

---

## Open questions / followups

1. **Window-pop-out behaviour** — should the window be poppable into its own OS window (`XPLM_WINDOW_POPOUT`)? The xp_welly_atc UI supports this. Probably yes, low cost.
2. **Settings persistence** — max range, last window position, "I have read the warning" flag. Defer to Phase 5 settings layer.
3. **Internationalisation** — the warning text and runway buttons are user-facing. Phase 6 introduces DE/EN/CH-DE; for Phase 4 we ship English only and design strings as gather-able constants.
4. **Procedure preview on the X-Plane map** — drawing the pattern as an overlay on the world map would be very useful but is significant additional work (`XPLMRegisterDrawCallback` for `xplm_Phase_LocalMap` etc.). Possible Phase 4.5.

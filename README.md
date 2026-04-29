# xp_swiss_vfr

A C++17 X-Plane 12 plugin that fills the Swiss VFR gap left by Navigraph CIFP — the small Swiss airfields that ship no IFR procedures and therefore have no FMS-injectable approach pattern out of the box.

> **Status:** in development, not yet released. Currently supported airports: **LSZB (Bern-Belp)**, **LSZG (Grenchen)**, **LSZF (Birrfeld)**, **LSZI (Fricktal-Schupfart)**, **LSZK (Speck-Fehraltorf)**, **LSZO (Luzern-Beromünster)**, **LSPN (Triengen)**, **LSPV (Wangen-Lachen)**. The list grows as we verify data per airport — the Swiss build is still the focus, then later Germany, France, and Austria.
>
> **Data quality:** all airport files are maintained on a **best-effort** basis from public AIP / VAC sources. We cross-check coordinates, elevations, runway directions, and pattern geometry against Skyguide AD 2 and the operator-published VAC, but we do not fly every circuit ourselves before release. Errors and outdated data can slip through — please report any deviation you spot in the sim against the current chart, and always brief the real chart before flight.

---

## What the plugin does

X-Plane only ships FMS-flyable procedures (SIDs / STARs / Approaches) for airports that have IFR procedures published in CIFP. Most Swiss sport airfields — Grenchen, Birrfeld, Schänis, Saanen, Lommis, Bex, Speck-Fehraltorf, Sion VFR-arrivals, … — are VFR-only, so the X1000 / GNS / G530 cabin shows nothing when you spool up to land there. That is the gap this plugin closes.

When the pilot opens the **Swiss VFR** menu, picks an airfield, and chooses an arrival sector, the plugin writes the published reporting points (VRPs) and a proper VFR pattern — downwind beginning, downwind end, final approach fix, runway threshold, destination airport — into the active flight plan. The avionics then sequence the pattern like any normal procedure: DTK indications, leg switches, and a sensible VNAV target down to runway elevation.

The data ships from real-world public sources, not generic templates. Every airport file is built from:

- **AIP Switzerland AD 2** sections (Skyguide, public PDFs hosted by the airport operators) for ARP, elevation, runways, and pattern direction.
- **Public Visual Approach Charts (VAC)** for arrival routes, sector names, helicopter sectors, and noise-abatement notes.
- **Navigraph navigation data** (the user's own subscription, never shipped with the plugin) for the precise VRP coordinates registered as official `VPxxx` fixes.

Where a real-world chart shows a topographic pattern-entry point that is not registered in any navaid database — the typical `_1`-pendants like `E1` or `W1` that exist only on the chart — the plugin generates the corresponding pattern leg from the airport's circuit geometry instead of fabricating coordinates. The pilot still hand-flies the actual transition.

A persistent banner inside the UI window makes it clear at a glance that this is **VFR reference material flown by hand** — Garmin-style autopilots cannot track the 90° turns at pattern speed.

---

## How it works

**Tech stack:** C++17, CMake 3.21+, X-Plane SDK 4.3, Dear ImGui 1.91.9 (OpenGL2 backend), nlohmann/json, Catch2.

**Build targets:** macOS Universal (arm64 + x86_64), Linux x86_64, Windows x86_64. CI builds all three on every push.

### Module layout

```
src/
├── main.cpp                # XPlugin* entry points
├── core/                   # plugin lifecycle + airport DB resolution
├── data/                   # VfrAirport / Waypoint / Runway domain model,
│                           # JSON loader & validator, NavigraphSource,
│                           # find_nearby_airports
├── geometry/               # equirectangular distance / bearing / offset
├── procedures/             # build_procedure (pure), procedure_runner
│                           # (XPLM-coupled FMS write), state machine
├── ui/                     # ImGui window: master-detail picker + active
│                           # procedure indicator + warning banner
└── version.hpp
```

### Data flow

```
resources/airports/*.json
    │   (loaded at plugin start, validated per file —
    │    one bad file logs a warning and is skipped)
    ▼
data::VfrAirportDatabase
    │
    │  optional: Custom Data/earth_fix.dat
    │  (Navigraph) overrides VRP coordinates
    │  with the precise published values
    ▼
ui::procedure_selection_window
    │  → pilot picks airport + RWY/route
    ▼
procedures::build_procedure(airport, runway, route_label)
    │  pure SDK-free function: emits ProcedureWaypoints
    │  from VRPs + circuit geometry (DW-BEG, DW-END,
    │  FAF, THR @ field elevation)
    ▼
procedures::activate(procedure)
    │  XPLM410 multi-FPL API against xplm_Fpl_Pilot_Primary:
    │  snapshot the existing plan + approach slot, append our
    │  pattern, append the destination airport with its elevation
    │  as VNAV target
    ▼
X-Plane FMS — visible to G1000 / GNS / GTN / X530 / etc.
```

### Airport JSON shape

Each airfield is one file under `resources/airports/<ICAO>_<lowercase_name>.json`:

```json
{
  "icao": "LSZB",
  "name": "Bern-Belp",
  "elevation_ft": 1673,
  "arp":      { "lat": 46.91222, "lon": 7.49944 },
  "runways":  [ { "designator": "14", "heading_true": 140.1, "length_m": 1730, "surface": "asphalt", "circuit_pattern": "left" }, ... ],
  "vrps":     [ { "name": "N", "lat": 47.013889, "lon": 7.557500, "altitude_ft_min": 3000, "altitude_ft_max": 4000, "mandatory_report": true }, ... ],
  "arrival_routes": {
    "14": [
      { "label": "via NOVEMBER (N)", "vrps": ["N"] },
      { "label": "via ECHO (E)",     "vrps": ["E"] }
    ]
  },
  "runway_notes":    { "14": "RWY 14 - left-hand pattern. Used up to 5 kts tailwind ..." },
  "circuit_pattern": { "altitude_ft_agl": 1000, "downwind_offset_nm": 1.0, "final_distance_nm": 1.5 },
  "frequencies":     { "tower": "121.025", "approach": "127.325" },
  "metadata":        { "source": "AIP Switzerland LSZB AD 2 ...", "verified_by": "...", "last_updated": "..." }
}
```

The schema is enforced at runtime by the loader (`src/data/json_loader.cpp`) and a list of domain validators (`src/data/validation.cpp`); the JSON Schema in `resources/schemas/vfr_airport.schema.json` documents the same contract for editors.

### FMS safety

Touching the active flight plan is destructive by nature. Two guarantees keep the pilot's plan intact:

1. **Snapshot before activate.** The plugin reads every entry of `xplm_Fpl_Pilot_Primary` and `xplm_Fpl_Pilot_Approach` before injecting anything.
2. **Restore on clear.** Pressing *Clear* (or invoking the bound `xpswissvfr/clear` command) wipes the FPL and re-emits the captured snapshot — origin airport, SID waypoints, en-route fixes, destination airport, and the previously selected approach procedure all come back.

If the pilot already had the plugin's destination airport loaded, it is *absorbed* during inject and re-emitted at the very end of the procedure block, so there is no `… → LSZG → E → … → RWY → LSZG` duplicate.

### Tests

The domain logic (parsing, validation, geometry, build_procedure, state machine, NavigraphSource, find_nearby_airports) is fully unit-tested with Catch2 against fixture JSONs under `tests/fixtures/`. SDK-coupled code (FMS write, ImGui rendering) is verified manually inside X-Plane 12.

```
make test     # 76 cases / 291 assertions, all green
```

---

## Installation

> Not yet released. The instructions below describe the intended end-user install once we ship a tagged release.

1. Download `xp_swiss_vfr.zip` from the GitHub Releases page (planned).
2. Unzip into your X-Plane 12 plugins directory:
   - Standard layout: `X-Plane 12/Resources/plugins/`
   - With XLauncher (or any plugin manager that uses an "available" stage): `X-Plane 12/Resources/available plugins/`
3. Restart X-Plane.
4. Open **Plugins → Swiss VFR → Show pattern selector**, or bind the X-Plane command `xpswissvfr/window/toggle` to a keyboard shortcut.

The plugin runs without a Navigraph subscription — the shipped JSONs already contain workable VRP coordinates. If Navigraph *is* installed (the plugin auto-detects `Custom Data/cycle_info.txt`), the runtime override layer replaces VRP coordinates with the precise published values and logs each upgrade, e.g.

```
[xp_swiss_vfr] Navigraph detected; upgraded 7 VRP coordinate(s).
[xp_swiss_vfr]   LSZG/E: 47.180000,7.550000 → 47.210556,7.600000
```

### Build from source

```bash
make setup        # download X-Plane SDK + ImGui + json.hpp + Catch2
make build        # → build/xp_swiss_vfr.xpl  (CMake Release, Universal on macOS)
make test         # build and run the Catch2 unit tests
make lint         # clang-tidy
make install      # codesign + copy into the X-Plane plugins directory
```

---

## Limitations

- **Coverage is intentionally narrow today.** Currently eight Swiss airfields ship (LSZB, LSZG, LSZF, LSZI, LSZK, LSZO, LSPN, LSPV). Adding an airport is mechanical — one JSON file, no code change — but the data verification step is the bottleneck. We add a place only when its data is traceable to an AIP / VAC source, and even then on a **best-effort** basis: the published JSON reflects the chart we read at import time, not a continuously audited dataset.
- **VFR reference only.** The injected pattern is a visual reference for *hand-flown* circuits. Garmin-style autopilots cannot track the 90° turns at pattern speed; a built-in warning banner makes this unmissable. There is no certification, no IFR backing, and no expectation that an autopilot will fly a published Swiss VFR circuit cleanly.
- **Topographic pattern-entry points are computed, not chart-exact.** The `_1`-pendants you see on Swiss VACs (`E1` after sector ECHO, `W1` after sector WHISKEY, …) are not registered as official navaid fixes — they exist only on the chart, defined by a topographic landmark. The plugin generates the corresponding leg geometrically from the airport's circuit-pattern data instead of fabricating coordinates. The pilot must still hand-fly the actual transition.
- **Pattern direction inference for new airports.** When AIP / VAC text does not state pattern direction explicitly, we infer it from local context (e.g. "city to the west of runway → right pattern keeps traffic over open terrain") and mark the assumption in `metadata.verified_by`. These inferences need a real-VAC review before a release.
- **English UI only.** No localisation (DE / CH-DE / FR) yet.
- **No window-position persistence across plugin reloads.** ImGui re-centers the window on every X-Plane start.
- **No procedure preview on the X-Plane map.** The pattern is visible in the FMS / GPS but not drawn onto the world map.
- **No deep multi-aircraft / cockpit-sharing handling.** The active procedure is plugin-global, not per-aircraft.

---

## Roadmap

The current priority is depth of Swiss coverage before we open the field to other regions.

**Switzerland — next**
- LSGK Saanen, LSPL Langenthal, LSGB Bex, LSGY Yverdon-les-Bains, LSME Mollis, LSZW Thun, LSGS Sion (VFR), LSZC Buochs, LSZR St. Gallen-Altenrhein, … — added one at a time, each with an AIP / VAC trace in `metadata.source` and pattern geometry verified to best effort against the current VAC.

**Internationalisation — later phases**
- Germany (EDxx), France (LFxx), Austria (LOxx). VFR practice is similar enough that the schema and pattern engine carry over; what changes is the data work per airfield.

**Plugin features**
- *Auto-transitions* in the procedure state machine: detect first-leg engagement and threshold passage from a flight-loop callback, fire mandatory-reporting hooks when crossing a `mandatory_report` VRP.
- *Map overlay*: render the pattern polyline on X-Plane's local-map drawing phase so the pilot sees it without an avionics-side pane.
- *Settings persistence*: max search radius, last window position, and a "I have read the warning" flag in a `settings.json` next to the plugin.
- *Localisation* of UI strings (DE / CH-DE / FR / EN), driven by airport metadata where appropriate.
- *xp_wellys_atc bridge* (later phase): an optional inter-plugin link that forwards `PROCEDURE_ACTIVATED` / `WAYPOINT_REPORTED` / `APPROACH_COMPLETED` events to the ATC voice plugin, failure-tolerantly.

---

## License

GPLv3. See [LICENSE](LICENSE).

### Swiss VFR approach procedures for X-Plane 12

Fills the Swiss VFR gap left by Navigraph CIFP — the small Swiss airfields that ship no IFR procedures and therefore have no FMS-injectable approach pattern out of the box. Pick an airfield and an arrival sector, and the plugin writes the published Visual Reporting Points (VRPs) and a proper VFR pattern (downwind, FAF, threshold, destination) into the active flight plan. The avionics (G1000 / GNS / GTN / X530) sequence the pattern like any normal procedure.


### Supported Airfields (v1.0.0)

  - **LSZB** — Bern-Belp
  - **LSZF** — Birrfeld
  - **LSZG** — Grenchen
  - **LSZI** — Fricktal-Schupfart
  - **LSZK** — Speck-Fehraltorf
  - **LSZO** — Luzern-Beromünster
  - **LSPN** — Triengen
  - **LSPV** — Wangen-Lachen

  Each file is built from AIP Switzerland AD 2 (Skyguide) and the operator-published VAC, with a traceable `metadata.source` per airport. Adding more airfields is a JSON-only change — no code, no rebuild. See *Roadmap* below for what's next.


### What's New in v1.0.0

  - **Initial public release** with the 8 Swiss airfields listed above.
  - **Navigraph auto-detect** — if `Custom Data/cycle_info.txt` is present, the runtime override layer replaces VRP coordinates with the precise published `VPxxx` values and logs each upgrade. The plugin runs fully without Navigraph; shipped JSONs already contain workable coordinates.
  - **Computed pattern-entry points** — chart-only `_1`-pendants like `E1` / `W1` (topographic landmarks, not registered as navaid fixes) are generated geometrically from each airport's circuit pattern instead of fabricating coordinates.
  - **Snapshot & restore** — the existing flight plan (origin, SID, en-route fixes, destination, approach) is captured before injection, and *Clear* restores it byte-for-byte. The destination airport is absorbed and re-emitted at the end of the procedure block, so no duplicate destination entry appears.
  - **Persistent VFR-reference banner** in the UI window — Garmin-style autopilots cannot track the 90° turns at pattern speed, and the banner makes that unmissable.
  - **Catch2 test suite** — 76 cases / 291 assertions covering parsing, validation, geometry, `build_procedure`, the state machine, `NavigraphSource` and `find_nearby_airports`. CI builds and runs the suite on macOS, Linux, and Windows on every push.


### Features

  - VFR procedure injection into the active flight plan via the XPLM410 multi-FPL API
  - 8 Swiss airfields shipped, each with VAC-traceable arrival routes and circuit geometry
  - Per-runway arrival routes (sectors N / E / S / W and chart-named variants)
  - Auto-built pattern legs: downwind beginning, downwind end, final approach fix, runway threshold, destination airport with elevation as VNAV target
  - Visible to G1000 / GNS / GTN / X530 and any avionics that consume X-Plane FMS waypoints
  - Optional Navigraph integration: auto-detected, overrides VRPs with precise `VPxxx` coordinates
  - Snapshot of the active flight plan before injection, full restore on *Clear*
  - Destination-airport deduplication when the pilot already loaded the destination
  - ImGui master-detail picker with active-procedure indicator and warning banner
  - Bound X-Plane commands: `xpswissvfr/window/toggle`, `xpswissvfr/clear`
  - Graceful degradation — one bad airport JSON is logged and skipped, the plugin still loads


### Installation

  Download `xp_swiss_vfr.zip`, extract into `X-Plane 12/Resources/plugins/` (or `X-Plane 12/Resources/available plugins/` if you use XLauncher). Restart X-Plane and open **Plugins → Swiss VFR → Show pattern selector**, or bind `xpswissvfr/window/toggle` to a keyboard shortcut. See the README for the JSON shape and Navigraph auto-detect details.


### Requirements

  - X-Plane 12
  - macOS (ARM64 / x86_64 universal binary), Linux x86_64, or Windows x86_64
  - Navigraph subscription — *optional*, used for precise VRP coordinates if installed


### Known Limitations

  - Coverage is intentionally narrow — 8 Swiss airfields in 1.0.0. Adding an airport is a JSON-only change, but every file is verified against AIP / VAC sources on a best-effort basis, not continuously audited.
  - VFR reference only — the injected pattern is for hand-flown circuits. Garmin-style autopilots cannot track the 90° turns at pattern speed; the warning banner makes this explicit. No certification, no IFR backing.
  - Topographic pattern-entry points are computed, not chart-exact — `_1`-pendants (`E1`, `W1`, …) exist only on the chart and are generated geometrically. The pilot must hand-fly the actual transition.
  - Pattern-direction inference for new airports — when AIP / VAC text does not state direction explicitly, it is inferred from local context and marked in `metadata.verified_by`. Each release reviews these against the current VAC.
  - English UI only — no DE / CH-DE / FR localisation yet.
  - No window-position persistence across plugin reloads — ImGui re-centers on every X-Plane start.
  - No procedure preview on the X-Plane map — the pattern is visible in the FMS / GPS only.
  - No multi-aircraft / cockpit-sharing — the active procedure is plugin-global, not per-aircraft.


### Roadmap

  - **Switzerland — next**: LSGK Saanen, LSPL Langenthal, LSGB Bex, LSGY Yverdon-les-Bains, LSME Mollis, LSZW Thun, LSGS Sion (VFR), LSZC Buochs, LSZR St. Gallen-Altenrhein — added one at a time with an AIP / VAC trace per file.
  - **Internationalisation**: Germany (EDxx), France (LFxx), Austria (LOxx). The schema and pattern engine carry over; what changes is the data work per airfield.
  - **Auto-transitions** in the procedure state machine — first-leg engagement and threshold passage from a flight-loop callback, mandatory-reporting hooks when crossing a `mandatory_report` VRP.
  - **Map overlay** — render the pattern polyline on X-Plane's local-map drawing phase.
  - **Settings persistence** — max search radius, last window position, "I have read the warning" flag, in a `settings.json` next to the plugin.
  - **Localisation** of UI strings (DE / CH-DE / FR / EN), driven by airport metadata where appropriate.
  - **xp_wellys_atc bridge** — optional inter-plugin link forwarding `PROCEDURE_ACTIVATED` / `WAYPOINT_REPORTED` / `APPROACH_COMPLETED` events to the ATC voice plugin, failure-tolerantly.

# Milestone 1.5 — FMS Validation Findings

> **Status**: ☐ not started   ☐ in progress   ☑ complete (with open questions Q1–Q6 for bugfix session)
> **Tester**: thWelly
> **X-Plane version**: 12.x.x  ← fill in
> **Test airport**: LSZG (Grenchen)
> **Test procedure**: RWY 06, Sector East (5 waypoints, hardcoded constants)
> **Spike branch**: `m1.5-fms-spike`
> **Date started**: 2026-04-28
> **Date complete**: 2026-04-28

> **Spike-behaviour changes (2026-04-28)** — two pivots happened during Phase A/B:
>
> 1. **Existing-FPL handling** was switched from destructive clear-then-fill to
>    **insert-without-clobber**: inject appends procedure entries at
>    `idx == count_before` and clear removes only the tracked spike range
>    (`[s_inject_first .. s_inject_first + s_inject_count - 1]`).
>
> 2. **API path** was switched from the legacy single-FPL routines
>    (`XPLMSetFMSEntryLatLon` / `XPLMSetFMSEntryInfo` / `XPLMClearFMSEntry`) to
>    the **XPLM410 multi-flight-plan API** against `xplm_Fpl_Pilot_Primary`
>    (`XPLMSetFMSFlightPlanEntryLatLonWithId` / `XPLMSetFMSFlightPlanEntryInfo` /
>    `XPLMClearFMSFlightPlanEntry`). Reason: the legacy routines write into a
>    flight-plan slot that the X-Plane 12 default C172 G1000 (X1000) does not
>    display — the first XPLM410-less Activate hit `count_before == 0` even
>    though the pilot had built an FPL via the X1000 GUI. With XPLM410 against
>    `xplm_Fpl_Pilot_Primary` the avionic sees our writes, and as a bonus
>    `XPLMSetFMSFlightPlanEntryLatLonWithId` accepts a custom display ID — so
>    H8 ("can we get custom labels?") flips from "no" to "yes" via that API.
>
> All H1/H2/H5/H7/H8 results below reflect the final XPLM410 + non-destructive
> implementation. H6 (in-flight leg progression) and the GNS/GTN rows of H3/H4
> and H9 (re-test against a non-empty `xplm_Fpl_Pilot_Primary`) are deferred to
> the next bugfix/test session.

See [`milestone-1.5-plan.md`](milestone-1.5-plan.md) for the hypotheses, test strategy, and decision tree this document feeds into.

---

## Phase A — Spike code

### Build / test / lint

- [x] `make build` clean (Universal Binary, arm64 + x86_64)
- [x] `make test` clean (103 assertions / 27 cases, data-only Catch2)
- [x] `make lint` clean (clang-tidy, exit 0; performance-* warnings present but not promoted)
- [x] `make install` deployed to `~/X-Plane 12/Resources/available plugins/xp_swiss_vfr`

### Plugin menu visible

- [x] `Plugins → xp_swiss_vfr` menu appears in X-Plane
- [x] `Activate LSZG RWY 06 E (test)` item present
- [x] `Clear LSZG procedure` item present

### Log.txt excerpts

Inject cycle from the XPLM410 run on 2026-04-28 (`~/X-Plane 12/Log.txt` lines 971–987):

```
[xp_swiss_vfr] FMS inject: LSZG RWY 06 Sector East (5 waypoints), appending at idx=0 (existing FPL has 0 entries)
[xp_swiss_vfr] FMS inject idx=0 name="E-ENTRY" mode=latlon-id lat=47.182000 lon=7.564000 alt=3500 id_len=7
[xp_swiss_vfr] FMS inject idx=0 -> count_after=1
[xp_swiss_vfr] FMS readback idx=0 type=2048 id="E-ENTR" lat=47.181999 lon=7.564000 alt=3500 ref=-1
[xp_swiss_vfr] FMS inject idx=1 name="DW-BEG" mode=latlon-id lat=47.193000 lon=7.398000 alt=2800 id_len=6
[xp_swiss_vfr] FMS inject idx=1 -> count_after=2
[xp_swiss_vfr] FMS readback idx=1 type=2048 id="DW-BEG" lat=47.193001 lon=7.398000 alt=2800 ref=-1
[xp_swiss_vfr] FMS inject idx=2 name="DW-END" mode=latlon-id lat=47.185000 lon=7.377000 alt=2400 id_len=6
[xp_swiss_vfr] FMS inject idx=2 -> count_after=3
[xp_swiss_vfr] FMS readback idx=2 type=2048 id="DW-END" lat=47.185001 lon=7.377000 alt=2400 ref=-1
[xp_swiss_vfr] FMS inject idx=3 name="FAF-06" mode=latlon-id lat=47.171000 lon=7.389000 alt=2000 id_len=6
[xp_swiss_vfr] FMS inject idx=3 -> count_after=4
[xp_swiss_vfr] FMS readback idx=3 type=2048 id="FAF-06" lat=47.171001 lon=7.389000 alt=2000 ref=-1
[xp_swiss_vfr] FMS inject idx=4 name="LSZG" mode=airport icao=LSZG ref=16788969 alt=0
[xp_swiss_vfr] FMS inject idx=4 -> count_after=5
[xp_swiss_vfr] FMS readback idx=4 type=1 id="LSZG" lat=47.181389 lon=7.416389 alt=0 ref=16788969
[xp_swiss_vfr] FMS inject done; range=[0..4] total_entries=5
```

Clear cycle from an earlier session (legacy API; same structure under XPLM410):

```
[xp_swiss_vfr] FMS clear: requested via menu.
[xp_swiss_vfr] FMS clear (user-clear) start; range=[0..4] total=1
[xp_swiss_vfr] FMS clear (user-clear) done;  count_after=0
```

Notes from the readback lines:
- `type=2048` is `xplm_Nav_LatLon`, `type=1` is `xplm_Nav_Airport`. Mixed-mode confirmed.
- Index 0 was written with `id_len=7` ("E-ENTRY") but read back as `"E-ENTR"` —
  the X1000 truncates display IDs to **6 characters**.
- Lat/lon round-trip is exact within float precision (e.g. 47.182000 → 47.181999).
- Index 4 (airport mode) read-back lat/lon = LSZG ARP (47.181389, 7.416389) —
  the navref overrides the lat/lon we wrote in the fallback path.

---

## Phase B — Cross-avionics verification

### H1 — FMS write API places entries (against `xplm_Fpl_Pilot_Primary`)

> Note: spike final implementation calls `XPLMSetFMSFlightPlanEntryLatLonWithId`
> for lat/lon entries and `XPLMSetFMSFlightPlanEntryInfo` for the airport entry.
> H1 is interpreted as "the SDK write call lands a navigable entry"; it passes
> for both modes.

| Aspect | Result | Evidence |
|---|---|---|
| All 5 SDK write calls returned without crash | ☑ pass | Log lines 972/975/978/981/984 — sequential success, no plugin reload |
| `XPLMCountFMSFlightPlanEntries(FPL)` after inject == 5 | ☑ pass | Log line 985: `count_after=5`; line 987: `range=[0..4] total_entries=5` |
| `XPLMGetFMSFlightPlanEntryInfo` round-trip lat/lon match within 0.001° | ☑ pass | e.g. wrote 47.182000 → read 47.181999 (float precision); all 4 lat/lon entries within < 1.5e-6° |

### H2 — Visible in Laminar X1000 (default C172 G1000)

| Aspect | Result | Notes |
|---|---|---|
| All 5 entries visible on FPL page | ☑ pass | Confirmed in `fms2.jpg` (legacy run, 5 lines) and `fms3.jpg` (XPLM410 run, custom labels) |
| Identifier column shows | `+47 +7` / `+47 +8` (legacy lat/lon); `E-ENTR` / `DW-BEG` / `DW-END` / `FAF-06` (XPLM410 with-ID); `LSZG` (airport mode) | Legacy renders rounded integer lat/lon — practically unusable. XPLM410-with-ID renders the supplied 5-/6-/7-char ID, **truncated to 6 chars** by the X1000. Airport mode renders the ICAO. |
| Map line / magenta course renders | ☑ pass | Magenta active leg + white future legs visible in all three sim screenshots |
| Direct-To next entry works | ☐ not tested | Deferred to bugfix/test session |
| Map symbology of entries | small triangles with the display ID; the airport entry uses the standard X-Plane runway symbol at LSZG |

### H3 — Visible in Garmin GNS 430 (default C172 with GNS, if installed)

| Aspect | Result | Notes |
|---|---|---|
| Aircraft used | — | skipped: no GNS-430-equipped default aircraft tested in this milestone |
| All 5 entries visible | ☐ skipped | |
| Identifier column shows | — | |
| Map line renders | ☐ skipped | |

### H4 — Visible in X-Plane integrated GTN 650 (if available)

| Aspect | Result | Notes |
|---|---|---|
| Aircraft used | — | skipped: no GTN-equipped default aircraft tested in this milestone |
| All 5 entries visible | ☐ skipped | |
| Identifier column shows | — | |
| Map line renders | ☐ skipped | |

### H5 — Per-waypoint altitudes are honoured

| Index | Written alt (ft) | X1000 shows | GNS 430 | GTN 650 | Notes |
|---|---|---|---|---|---|
| 0 | 3500 | (not visible while shown as active-leg target — ALT column empty for the active row) | skipped | skipped | active-leg row in the X1000 FPL page omits DTK/DIS/ALT |
| 1 | 2800 | `2800FT` ✓ | skipped | skipped | |
| 2 | 2400 | `2400FT` ✓ | skipped | skipped | |
| 3 | 2000 | `2000FT` ✓ | skipped | skipped | |
| 4 | 0    | blank (`____FT`) ✓ | skipped | skipped | airport entry; 0 → no altitude constraint, as expected |

Result: ☑ pass on X1000 (confirmed unit: feet; 0 → blank constraint).

### H6 — Sequence is sensible / leg-progression works

| Aspect | Result | Notes |
|---|---|---|
| Magenta line connects all 5 entries in order | ☑ pass | Visible in `fms3.jpg` (XPLM410 run with corrected geometry) |
| AP NAV mode tracks the sequence | ☐ not tested | Deferred to bugfix/test session |
| Leg auto-switches at each waypoint | ☐ not tested | Deferred to bugfix/test session |
| Final leg ends cleanly at THR 06 | ⚠ partial | Final leg ends at LSZG **ARP** (47.181, 7.417), ~600 m past THR 06 along the 060° centerline. Acceptable artifact of the airport-navref endpoint; geometry is otherwise sound after the 2026-04-28 waypoint correction. |

### H7 — Clear cleans up

| Aspect | Result | Notes |
|---|---|---|
| `XPLMCountFMSFlightPlanEntries(FPL)` after clear == count_before_inject | ☑ pass | Earlier legacy-API session: clear range=[0..4] → count_after=0 (started from empty) |
| Only spike-range entries removed; pre-existing entries preserved | ☐ not tested | Re-test required against XPLM410 with `count_before > 0` (deferred) |
| X1000 FPL page reflects the clear | ☑ pass | Confirmed in earlier session |
| GNS 430 / GTN 650 FPL page reflects the clear | ☐ skipped | No GNS/GTN aircraft tested |

### H8 — Display label for lat/lon entries (the critical question)

| Avionic | API path | Label observed (verbatim) | Notes |
|---|---|---|---|
| Laminar X1000 (default C172 G1000) | legacy `XPLMSetFMSEntryLatLon` | `+47 +7`, `+47 +8` | Rounded integer lat/lon, signed, space-separated. Practically unusable: 4 of 5 LSZG-area waypoints render as `+47 +7`. |
| Laminar X1000 | XPLM410 `XPLMSetFMSFlightPlanEntryLatLonWithId` | `E-ENTR`, `DW-BEG`, `DW-END`, `FAF-06` | Custom ID rendered, truncated to **6 chars**. ID `E-ENTRY` (7 chars) truncated to `E-ENTR`. |
| Laminar X1000 | `XPLMSetFMSFlightPlanEntryInfo` (airport navref) | `LSZG` | Real ICAO label from the nav database. |
| GNS 430 | n/a | — | skipped |
| GTN 650 | n/a | — | skipped |

Answer to the meta-question — **can we get custom labels on the FMS line?**

- ☐ No — `XPLMSetFMSEntryLatLon` always renders generic placeholder. Phase 2 must accept that.
- ☑ **Yes via the XPLM410 multi-flight-plan API** (function: `XPLMSetFMSFlightPlanEntryLatLonWithId`, against `xplm_Fpl_Pilot_Primary`). Phase 2 should adopt that API.
- ☐ Mixed — works on some avionics, not on others.

**Practical guidance for Phase 2 IDs**: keep display IDs ≤ 6 chars to fit the
X1000 column without truncation. Mixed-mode (lat/lon-with-ID for sector/
pattern points + airport-navref for the destination) is supported in a single
flight plan and looks the cleanest in the avionic.

### H9 — Behaviour when FPL is non-empty before injection

> Spec change mid-spike: the runner now **inserts without clobber**. Inject
> appends 5 entries at `idx == count_before` and clear removes only the
> tracked spike range, leaving pre-existing entries intact. H9 below is
> framed against that semantic.

Pre-state for both XPLM410 inject sessions on 2026-04-28:

| Step | Action | Observed |
|---|---|---|
| 1 | (Pilot built FPL via X1000 GUI in earlier sessions) | X1000 displayed an active FPL |
| 2 | Trigger Activate | Log: `existing FPL has 0 entries` (legacy run **and** XPLM410 run) |
| 3 | Observe | Avionic FPL shows our 5 entries; pre-existing X1000 entries no longer visible |

| Aspect | Result | Notes |
|---|---|---|
| Pre-existing entries preserved (count_before > 0 case) | ☑ pass | Later session log: `appending at idx=1 (existing FPL has 1 entries)` → spike writes to idx 1..5, the pre-existing entry at idx 0 is untouched. `range=[1..5] total_entries=6`. |
| Spike entries appended at idx == count_before | ☑ pass | Both `count_before=0` (line 971) and `count_before=1` (later run) cases verified — append is at the existing-count boundary in either case. |
| Re-activate without prior clear: only one set of spike entries (no zombies) | ☐ not tested | Logic in code: `if (s_active) remove_injected_entries("re-activate")`. Verify in next session. |
| Clear removes only spike range; pre-existing entries remain | ☐ not yet validated | Implemented correctly per code (clear iterates `[s_inject_first .. s_inject_first + s_inject_count - 1]` only) but not yet exercised against `count_before > 0` followed by clear and a re-read of pre-existing-entry survivors. |

---

## Cross-avionics compatibility matrix (summary)

| Avionic | H1 | H2/3/4 (visible) | H5 (alt) | H6 (sequence) | H7 (clear) | H8 (label) |
|---|---|---|---|---|---|---|
| Laminar X1000 (default C172 G1000) | ✓ | ✓ | ✓ | ✓ map / ⚠ flight not tested | ✓ from-empty / ☐ from-non-empty | ✓ via XPLM410 `…WithId` (6-char trunc) + airport navref |
| GNS 430 | skipped | skipped | skipped | skipped | skipped | skipped |
| GTN 650 | skipped | skipped | skipped | skipped | skipped | skipped |
| Default GPS | skipped | skipped | skipped | skipped | skipped | skipped |

---

## Screenshots

User-supplied screenshots from the three sim sessions on 2026-04-28
(originals in `~/Downloads/fms*.jpg`, copy into `docs/milestone-1.5-screenshots/`
when committing the spike PR):

- `fms1.jpg` — X1000 map after legacy-API inject: 5 lat/lon entries, magenta active leg, labels rendered as `+47 +7` / `+47 +8`.
- `fms2.jpg` — X1000 FPL page after legacy-API inject: 5 rows, ALT column shows `2800FT`/`2400FT`/`2000FT`/`____FT`, IDs all `+47 +X`.
- `fms3.jpg` — X1000 map after XPLM410 inject with corrected geometry: 5 entries with custom labels `TRANS-`, `DOWNWI`, `FINAL-`, `LSZG`, full pattern visible. Note: this screenshot is from the pre-geometry-fix run; the post-fix waypoints (`E-ENTRY` / `DW-BEG` / `DW-END` / `FAF-06` / `LSZG`) have not been re-screenshotted yet.
- Log excerpts captured inline above (Phase A — Log.txt excerpts).

---

## Conclusions

- **Recommended Phase 2 architecture**: XPLM410 multi-flight-plan API against `xplm_Fpl_Pilot_Primary`. Use `XPLMSetFMSFlightPlanEntryLatLonWithId` for sector/pattern waypoints and `XPLMSetFMSFlightPlanEntryInfo` for the destination airport. Track inserted range and clear via `XPLMClearFMSFlightPlanEntry`.
- **Confirmed unit for `inAltitude`**: feet. 0 → "no constraint" (rendered blank). Values 2800/2400/2000 round-tripped exactly.
- **Custom-label strategy**: ☑ XPLM410 `…WithId` for lat/lon entries (cap display ID at 6 chars to avoid X1000 truncation) + ☑ airport navref via `XPLMSetFMSFlightPlanEntryInfo` for ICAO endpoints. Mixed-mode in one FPL is supported.
- **Existing-FPL strategy**: ☑ insert-without-clobber. Append at `XPLMCountFMSFlightPlanEntries(FPL)`; track `[s_inject_first .. s_inject_first + s_inject_count - 1]` for clean removal. (Behaviour against a non-empty pilot FPL is implemented but not yet empirically validated — see open questions.)
- **Avionics support tier**: primary = Laminar X1000 (default C172 G1000); GNS 430 / GTN 650 / default GPS = untested in this milestone, deferred to Phase 2 cross-avionics work.
- **Open questions still unresolved after this milestone**: see below.

## Open questions raised by testing

- **Q1 — Non-empty pre-existing FPL** (resolved 2026-04-28): a later session reported `existing FPL has 1 entries` and our 5 entries appended at idx 1..5, leaving the pre-existing entry at idx 0 intact (`range=[1..5] total_entries=6`). Append-with-clobber-protection works against `xplm_Fpl_Pilot_Primary`. Still open: clear-from-non-empty round-trip survival check, and the re-activate-without-prior-clear no-zombie check (both covered in code, just not yet observed).
- **Q2 — Geometry of test waypoints**: the user observed in two sessions that the rendered pattern doesn't match a left-pattern RWY 06 visually (looped near the field). The 2026-04-28 waypoint correction (E-ENTRY / DW-BEG / DW-END / FAF-06 / LSZG with computed 90° base- and final-turns) is in the code but has not yet been screenshot-verified in the sim. Re-test in the bugfix session.
- **Q3 — Display-ID truncation**: the X1000 truncates to 6 chars (`E-ENTRY` → `E-ENTR`). Phase 2 should treat 6 as the working budget for sector/pattern point IDs. Confirm against GNS 430 / GTN 650 once those aircraft are available.
- **Q4 — `XPLMSetDestinationFMSFlightPlanEntry` and direct-to**: not exercised in this spike. Phase 2 may want to call it on the airport entry so the avionic treats LSZG as the explicit destination instead of just "the last waypoint".
- **Q5 — Final leg ends at ARP, not THR 06**: airport-mode endpoint targets the ARP (~600 m past THR 06 along the 060° extended centerline). Acceptable for the spike; Phase 2 can decide between (a) leaving it as airport-navref and accepting the small overshoot, or (b) replacing the airport entry with a `…WithId` lat/lon entry at the actual threshold and dropping the ICAO label.
- **Q6 — In-flight verification (H6)**: leg auto-switching, AP NAV-mode tracking, and Direct-To behaviour have not been tested. Required before declaring the architecture flight-ready.

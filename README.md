# xp_swiss_vfr

A C++17 X-Plane 12 plugin that adds Swiss VFR approach procedures — the kind that Navigraph CIFP does not ship because they are not IFR. Pick an airfield, pick a sector (E / S / W / N), and the plugin injects the mandatory reporting points as user waypoints, populates the active flight plan, and surfaces altitude restrictions and waypoint reminders.

Targets the small Swiss sport airfields that have no procedure support out of the box: Birrfeld, Schänis, Saanen, Grenchen, Lommis, Bex, Speck-Fehraltorf, …

## Status

Bootstrap. Phase 0 ships an empty but loadable skeleton — no VFR data yet. See `.claude/tasks/` for the per-phase implementation plan.

## Install

Download the latest `xp_swiss_vfr.zip` from [GitHub Releases](https://github.com/thWelly/xp_swiss_vfr/releases), unzip into `X-Plane 12/Resources/plugins/`, restart X-Plane.

## Build from source

```bash
make setup     # download X-Plane SDK, ImGui, json.hpp, Catch2 into sdk/ and vendor/
make build     # → build/xp_swiss_vfr.xpl
make test      # run unit tests
make install   # codesign + copy into ~/X-Plane 12/Resources/plugins/xp_swiss_vfr/
```

CI builds macOS Universal (arm64 + x86_64), Linux x86_64, and Windows x86_64 on every push.

## License

GPLv3. See [LICENSE](LICENSE).

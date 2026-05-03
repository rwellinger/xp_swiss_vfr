#pragma once

#include "data/vfr_airport.hpp"
#include "geometry/terrain.hpp"
#include "procedures/procedure.hpp"

#include <optional>
#include <string>

namespace xpswissvfr::procedures
{
// Constructs the VFR arrival pattern for the given runway and the named
// arrival route: arrival-route VRPs (verbatim) → optional descent helpers
// (D1, D2, ...) → DW-BEG → DW-END → FAF → THR.
//
// Descent helpers are inserted between the last route VRP and the pattern
// entry (DW-BEG) when the altitude difference exceeds MAX_DESCENT_PER_LEG_FT
// — this gives the pilot a stepped VNAV target instead of a single steep
// drop into the pattern. Helper positions are interpolated along the line
// VRP → ARP, altitudes linearly between VRP altitude and pattern-entry
// altitude.
//
// `terrain` clamps every synthetic waypoint (helpers and pattern legs) up
// to terrain + TERRAIN_MARGIN_FT when the source can answer for that
// position. JSON-defined VRPs are never clamped — they are AIP data and
// trusted as-is. Tests inject a NullTerrainSource (returns nullopt) to keep
// the math deterministic.
//
// Pure and SDK-free: same inputs always produce the same Procedure.
//
// Returns nullopt when the runway is unknown, has no arrival routes, the
// requested label is not found, or any route VRP is missing from `vrps`.
std::optional<Procedure> build_procedure(const data::VfrAirport &airport, const std::string &runway_designator,
                                         const std::string &route_label, const geometry::TerrainSource &terrain);

// Convenience overload that uses the default (SDK-backed) terrain source.
// In the test target the default source is a null source, so altitudes are
// not clamped — preserves the existing test expectations.
std::optional<Procedure> build_procedure(const data::VfrAirport &airport, const std::string &runway_designator,
                                         const std::string &route_label = "");
} // namespace xpswissvfr::procedures

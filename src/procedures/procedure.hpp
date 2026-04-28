#pragma once

#include "data/coordinate.hpp"

#include <optional>
#include <string>
#include <vector>

namespace xpswissvfr::procedures
{
// One waypoint inside a constructed procedure. The display name is what the
// avionic shows; X1000 truncates anything past 6 chars, so build_procedure
// caps the name at that length.
struct ProcedureWaypoint
{
    std::string        display_name;
    data::Coordinate   position;
    std::optional<int> altitude_ft;
};

// A complete VFR arrival pattern: arrival-route VRPs, downwind, base, final,
// threshold. Plain aggregate — the runner consumes this and writes it into
// the FMS without knowing how it was constructed.
struct Procedure
{
    std::string                    airport_icao;
    std::string                    runway_designator;
    std::vector<ProcedureWaypoint> waypoints;
};
} // namespace xpswissvfr::procedures

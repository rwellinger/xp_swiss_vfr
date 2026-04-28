#pragma once

#include "data/vfr_airport.hpp"
#include "procedures/procedure.hpp"

#include <optional>
#include <string>

namespace xpswissvfr::procedures
{
// Constructs the VFR arrival pattern for the given runway from the airport's
// JSON: arrival-route VRPs (verbatim) → DW-BEG → DW-END → FAF → THR.
//
// Pure and SDK-free: same input always produces the same Procedure.
//
// Returns nullopt when the runway is unknown, has no arrival route, or the
// arrival route references a VRP that is not in the airport's `vrps` list.
std::optional<Procedure> build_procedure(const data::VfrAirport &airport, const std::string &runway_designator);
} // namespace xpswissvfr::procedures

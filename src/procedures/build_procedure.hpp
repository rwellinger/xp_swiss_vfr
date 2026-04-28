#pragma once

#include "data/vfr_airport.hpp"
#include "procedures/procedure.hpp"

#include <optional>
#include <string>

namespace xpswissvfr::procedures
{
// Constructs the VFR arrival pattern for the given runway and the named
// arrival route: arrival-route VRPs (verbatim) → DW-BEG → DW-END → FAF → THR.
//
// `route_label` selects one of the routes published for the runway in the
// airport's `arrival_routes`. When omitted (empty string), the first route is
// used — that keeps simple "one-route-per-runway" airports working without
// callers having to know a label.
//
// Pure and SDK-free: same input always produces the same Procedure.
//
// Returns nullopt when the runway is unknown, has no arrival routes, the
// requested label is not found, or any route VRP is missing from `vrps`.
std::optional<Procedure> build_procedure(const data::VfrAirport &airport, const std::string &runway_designator,
                                         const std::string &route_label = "");
} // namespace xpswissvfr::procedures

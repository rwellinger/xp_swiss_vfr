#pragma once

#include "data/coordinate.hpp"

namespace xpswissvfr::geometry
{
// Equirectangular approximation. Sufficient for VFR-pattern distances (< 10 NM)
// and well below 60° latitude — the regime this plugin operates in. We trade
// great-circle accuracy for arithmetic that is simple enough to reason about
// when constructing patterns by hand from the JSON.

// Great-circle distance between two coordinates, in nautical miles.
double distance_nm(const data::Coordinate &a, const data::Coordinate &b);

// Bearing from `from` to `to`, in degrees [0, 360). 0° = north, 90° = east.
double bearing_deg(const data::Coordinate &from, const data::Coordinate &to);

// Coordinate that is `distance_nm` away from `from` along the given bearing.
data::Coordinate offset(const data::Coordinate &from, double bearing_deg, double distance_nm);
} // namespace xpswissvfr::geometry

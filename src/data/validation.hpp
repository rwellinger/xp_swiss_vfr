#pragma once

#include "vfr_airport.hpp"

#include <string>
#include <vector>

namespace xpswissvfr::data
{
// Returns an empty vector when the airport passes every rule. Each entry is a
// human-readable message describing exactly one rule violation.
std::vector<std::string> validate(const VfrAirport &airport);
} // namespace xpswissvfr::data

#pragma once

#include "data/coordinate.hpp"
#include "data/vfr_airport_database.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xpswissvfr::data
{

// (airport_icao, vrp_name) → precise Navigraph coordinate.
using NavigraphVrpOverrides = std::map<std::pair<std::string, std::string>, Coordinate>;

struct NavigraphOverrideStats
{
    int                      upgraded = 0;
    std::vector<std::string> upgraded_log; // human-readable, one per VRP
};

// True if Navigraph data is installed at the given X-Plane root. Detection
// rule: presence of `Custom Data/cycle_info.txt` — this file is shipped
// only by Navigraph, not by stock X-Plane.
bool navigraph_is_available(const std::filesystem::path &xplane_root);

// Read VP-prefixed VRP entries from the given earth_fix.dat for any of the
// supplied airport ICAOs. Returns an override map keyed by
// (airport_icao, vrp_long_name). Errors (missing file, malformed lines) are
// silently skipped — broken Navigraph data should not break plugin startup.
NavigraphVrpOverrides parse_navigraph_vrps(const std::filesystem::path &earth_fix_path,
                                           const std::set<std::string> &icaos);

// Apply the overrides to the database. For every (icao, vrp_name) pair in
// `overrides` that matches a loaded VRP, replace its position. Altitude bands
// and mandatory_report flag are preserved. Returns stats so the caller can log.
NavigraphOverrideStats apply_navigraph_overrides(VfrAirportDatabase &db, const NavigraphVrpOverrides &overrides);

} // namespace xpswissvfr::data

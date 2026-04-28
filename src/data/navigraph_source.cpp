#include "data/navigraph_source.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace xpswissvfr::data
{
namespace
{

// earth_fix.dat 1200 line layout for Navigraph data:
//   <lat>  <lon>  <ident>  <terminal_area>  <icao_region>  <usage_code>  <long_name...>
// Example:
//   47.122777778    7.512222222  VP063 LSZG LS 2105430 S
//   47.186111111    7.449444444  VP069 LSZG LS 2105430 ABM ALTREU
//
// Visual reporting points use the IDENT prefix "VP" and store the human-readable
// VRP name in the LONG_NAME field (which may contain spaces — "ABM ALTREU"). The
// TERMINAL_AREA field carries the airport ICAO; that's the join key for our
// override map.

constexpr const char *VRP_IDENT_PREFIX = "VP";

bool starts_with(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string trim(std::string s)
{
    auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

// Tokenize the first six whitespace-separated fields and return the rest of
// the line (long_name) verbatim — the long_name may contain spaces.
struct ParsedFix
{
    double      lat = 0.0;
    double      lon = 0.0;
    std::string ident;
    std::string terminal_area;
    std::string icao_region;
    std::string usage_code;
    std::string long_name;
    bool        ok = false;
};

ParsedFix parse_fix_line(const std::string &line)
{
    ParsedFix          p;
    std::istringstream stream(line);
    if (!(stream >> p.lat >> p.lon >> p.ident >> p.terminal_area >> p.icao_region >> p.usage_code))
        return p;

    std::string remainder;
    std::getline(stream, remainder);
    p.long_name = trim(std::move(remainder));
    p.ok        = !p.long_name.empty();
    return p;
}

} // namespace

bool navigraph_is_available(const std::filesystem::path &xplane_root)
{
    std::error_code ec;
    return std::filesystem::exists(xplane_root / "Custom Data" / "cycle_info.txt", ec);
}

NavigraphVrpOverrides parse_navigraph_vrps(const std::filesystem::path &earth_fix_path,
                                           const std::set<std::string> &icaos)
{
    NavigraphVrpOverrides overrides;
    std::ifstream         stream(earth_fix_path);
    if (!stream.is_open())
        return overrides;

    std::string line;
    while (std::getline(stream, line))
    {
        ParsedFix fix = parse_fix_line(line);
        if (!fix.ok)
            continue;
        if (!starts_with(fix.ident, VRP_IDENT_PREFIX))
            continue;
        if (icaos.count(fix.terminal_area) == 0)
            continue;

        overrides[{fix.terminal_area, fix.long_name}] = Coordinate{fix.lat, fix.lon};
    }
    return overrides;
}

NavigraphOverrideStats apply_navigraph_overrides(VfrAirportDatabase &db, const NavigraphVrpOverrides &overrides)
{
    NavigraphOverrideStats stats;
    for (const auto &[key, coord] : overrides)
    {
        const auto &[icao, vrp_name] = key;

        VfrAirport *airport = db.find_mutable(icao);
        if (airport == nullptr)
            continue;

        for (auto &vrp : airport->vrps)
        {
            if (vrp.name != vrp_name)
                continue;
            const Coordinate before = vrp.position;
            vrp.position            = coord;
            stats.upgraded++;
            stats.upgraded_log.push_back(icao + "/" + vrp_name + ": " + std::to_string(before.lat) + "," +
                                         std::to_string(before.lon) + " → " + std::to_string(coord.lat) + "," +
                                         std::to_string(coord.lon));
            break;
        }
    }
    return stats;
}

} // namespace xpswissvfr::data

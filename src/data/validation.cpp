#include "data/validation.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <set>
#include <string>

namespace xpswissvfr::data
{
namespace
{

using Validator = std::function<std::optional<std::string>(const VfrAirport &)>;

std::optional<std::string> check_icao_format(const VfrAirport &a)
{
    const bool ok = a.icao.size() == 4 &&
                    std::all_of(a.icao.begin(), a.icao.end(), [](unsigned char c) { return std::isupper(c) != 0; });
    return ok ? std::nullopt : std::optional<std::string>{"icao must be exactly 4 uppercase letters: '" + a.icao + "'"};
}

std::optional<std::string> check_arp_in_range(const VfrAirport &a)
{
    const bool lat_ok = a.arp.lat >= -90.0 && a.arp.lat <= 90.0;
    const bool lon_ok = a.arp.lon >= -180.0 && a.arp.lon <= 180.0;
    if (lat_ok && lon_ok)
        return std::nullopt;
    return "arp coordinate out of range: lat=" + std::to_string(a.arp.lat) + ", lon=" + std::to_string(a.arp.lon);
}

std::optional<std::string> check_at_least_one_runway(const VfrAirport &a)
{
    return a.runways.empty() ? std::optional<std::string>{"runways list is empty"} : std::nullopt;
}

std::optional<std::string> check_at_least_one_vrp(const VfrAirport &a)
{
    return a.vrps.empty() ? std::optional<std::string>{"vrps list is empty"} : std::nullopt;
}

std::optional<std::string> check_unique_vrp_names(const VfrAirport &a)
{
    std::set<std::string> seen;
    for (const auto &vrp : a.vrps)
    {
        if (!seen.insert(vrp.name).second)
            return "duplicate vrp name: '" + vrp.name + "'";
    }
    return std::nullopt;
}

std::optional<std::string> check_runway_headings_in_range(const VfrAirport &a)
{
    for (const auto &rwy : a.runways)
    {
        if (rwy.heading_true < 0.0 || rwy.heading_true >= 360.0)
            return "runway " + rwy.designator + " heading out of range: " + std::to_string(rwy.heading_true);
    }
    return std::nullopt;
}

std::optional<std::string> check_altitude_band(const Waypoint &w, const std::string &context)
{
    if (!w.altitude_ft_min || !w.altitude_ft_max)
        return std::nullopt;
    if (*w.altitude_ft_min <= *w.altitude_ft_max)
        return std::nullopt;
    return context + ": altitude_ft_min (" + std::to_string(*w.altitude_ft_min) + ") > altitude_ft_max (" +
           std::to_string(*w.altitude_ft_max) + ")";
}

std::optional<std::string> check_vrp_altitude_bands(const VfrAirport &a)
{
    for (const auto &vrp : a.vrps)
    {
        if (auto err = check_altitude_band(vrp, "vrp '" + vrp.name + "'"))
            return err;
    }
    return std::nullopt;
}

std::optional<std::string> check_circuit_pattern_dimensions(const VfrAirport &a)
{
    if (a.circuit_pattern.downwind_offset_nm <= 0.0)
        return "circuit_pattern.downwind_offset_nm must be > 0";
    if (a.circuit_pattern.final_distance_nm <= 0.0)
        return "circuit_pattern.final_distance_nm must be > 0";
    return std::nullopt;
}

std::optional<std::string> check_arrival_routes_reference_known_vrps(const VfrAirport &a)
{
    std::set<std::string> known_vrps;
    for (const auto &vrp : a.vrps)
        known_vrps.insert(vrp.name);

    std::set<std::string> known_runways;
    for (const auto &rwy : a.runways)
        known_runways.insert(rwy.designator);

    for (const auto &[runway_designator, vrp_sequence] : a.arrival_routes)
    {
        if (known_runways.count(runway_designator) == 0)
            return "arrival_routes references unknown runway: '" + runway_designator + "'";
        if (vrp_sequence.empty())
            return "arrival_routes['" + runway_designator + "'] is empty";
        for (const auto &vrp_name : vrp_sequence)
        {
            if (known_vrps.count(vrp_name) == 0)
                return "arrival_routes['" + runway_designator + "'] references unknown vrp: '" + vrp_name + "'";
        }
    }
    return std::nullopt;
}

const std::vector<Validator> &validators()
{
    static const std::vector<Validator> rules = {
        check_icao_format,
        check_arp_in_range,
        check_at_least_one_runway,
        check_at_least_one_vrp,
        check_unique_vrp_names,
        check_runway_headings_in_range,
        check_vrp_altitude_bands,
        check_circuit_pattern_dimensions,
        check_arrival_routes_reference_known_vrps,
    };
    return rules;
}

} // namespace

std::vector<std::string> validate(const VfrAirport &airport)
{
    std::vector<std::string> errors;
    for (const auto &rule : validators())
    {
        if (auto err = rule(airport))
            errors.push_back(*err);
    }
    return errors;
}

} // namespace xpswissvfr::data

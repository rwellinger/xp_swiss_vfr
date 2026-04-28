#include "data/json_loader.hpp"

#include <json.hpp>

#include <fstream>
#include <utility>

using json = nlohmann::json;

namespace xpswissvfr::data
{

// ── ADL from_json hooks ──────────────────────────────────────────────────────
// nlohmann::json finds these via ADL, so they must live in the same namespace
// as the target struct. Each hook is single-purpose: read fields, no branching.

static std::optional<int> read_optional_int(const json &j, const char *key)
{
    return j.contains(key) ? std::optional<int>{j.at(key).get<int>()} : std::nullopt;
}

void from_json(const json &j, Coordinate &c)
{
    j.at("lat").get_to(c.lat);
    j.at("lon").get_to(c.lon);
}

void from_json(const json &j, Waypoint &w)
{
    j.at("name").get_to(w.name);
    w.position.lat     = j.at("lat").get<double>();
    w.position.lon     = j.at("lon").get<double>();
    w.altitude_ft      = read_optional_int(j, "altitude_ft");
    w.altitude_ft_min  = read_optional_int(j, "altitude_ft_min");
    w.altitude_ft_max  = read_optional_int(j, "altitude_ft_max");
    w.mandatory_report = j.value("mandatory_report", false);
}

void from_json(const json &j, Runway &r)
{
    j.at("designator").get_to(r.designator);
    j.at("heading_true").get_to(r.heading_true);
    j.at("length_m").get_to(r.length_m);
    j.at("surface").get_to(r.surface);
    j.at("circuit_pattern").get_to(r.circuit_pattern);
}

void from_json(const json &j, CircuitPattern &c)
{
    j.at("altitude_ft_agl").get_to(c.altitude_ft_agl);
    j.at("downwind_offset_nm").get_to(c.downwind_offset_nm);
    j.at("final_distance_nm").get_to(c.final_distance_nm);
}

void from_json(const json &j, AirportMetadata &m)
{
    m.source       = j.value("source", "");
    m.last_updated = j.value("last_updated", "");
    m.verified_by  = j.value("verified_by", "");
    m.notes        = j.value("notes", "");
}

void from_json(const json &j, VfrAirport &a)
{
    j.at("icao").get_to(a.icao);
    j.at("name").get_to(a.name);
    j.at("elevation_ft").get_to(a.elevation_ft);
    j.at("arp").get_to(a.arp);
    j.at("runways").get_to(a.runways);
    j.at("vrps").get_to(a.vrps);
    a.arrival_routes = j.value("arrival_routes", std::map<std::string, std::vector<std::string>>{});
    a.runway_notes   = j.value("runway_notes", std::map<std::string, std::string>{});
    j.at("circuit_pattern").get_to(a.circuit_pattern);
    a.frequencies = j.value("frequencies", std::map<std::string, std::string>{});
    a.metadata    = j.value("metadata", AirportMetadata{});
}

// ── Public API ───────────────────────────────────────────────────────────────

std::variant<VfrAirport, ParseError> parse_airport(const std::filesystem::path &file)
{
    std::ifstream stream(file);
    if (!stream.is_open())
        return ParseError{file.filename().string(), "cannot open file"};

    try
    {
        json       parsed = json::parse(stream);
        VfrAirport airport;
        parsed.get_to(airport);
        return airport;
    }
    catch (const json::exception &e)
    {
        return ParseError{file.filename().string(), e.what()};
    }
}

} // namespace xpswissvfr::data

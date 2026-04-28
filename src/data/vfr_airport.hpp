#pragma once

#include "coordinate.hpp"
#include "runway.hpp"
#include "waypoint.hpp"

#include <map>
#include <string>
#include <vector>

namespace xpswissvfr::data
{
struct CircuitPattern
{
    int    altitude_ft_agl;
    double downwind_offset_nm;
    // Final-leg length: distance from FAF to landing threshold along the
    // extended centerline. Drives the glideslope from FAF altitude down to
    // touchdown — different airports publish different values depending on
    // terrain and obstacle clearance.
    double final_distance_nm;
};

struct AirportMetadata
{
    std::string source;
    std::string last_updated; // ISO-8601 date
    std::string verified_by;
    std::string notes;
};

struct VfrAirport
{
    std::string         icao;
    std::string         name;
    int                 elevation_ft;
    Coordinate          arp;
    std::vector<Runway> runways;
    // Visual reporting points (Sichtanflugpunkte) as named in the airport's
    // AIP entry. Coordinates ship from public sources (Skyguide / OpenAIP /
    // pilot knowledge); a Navigraph runtime override layer (Phase 2 step 6)
    // can refine them when the user has a Navigraph subscription installed.
    std::vector<Waypoint> vrps;
    // For each runway designator → ordered VRP names that form the published
    // arrival route to that runway. Keys must match a runway designator.
    // Values must reference VRP names that exist in `vrps`.
    std::map<std::string, std::vector<std::string>> arrival_routes;
    // Optional free-text per-runway notes, shown as a tooltip in the procedure
    // selector UI. Key = runway designator. Missing keys are rendered without
    // a tooltip — that is intentional, do not invent default text.
    std::map<std::string, std::string> runway_notes;
    CircuitPattern                     circuit_pattern;
    std::map<std::string, std::string> frequencies; // {"info": "121.235", "ground": ""}
    AirportMetadata                    metadata;
};
} // namespace xpswissvfr::data

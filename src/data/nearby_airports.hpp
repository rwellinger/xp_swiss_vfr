#pragma once

#include "coordinate.hpp"
#include "vfr_airport_database.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace xpswissvfr::data
{
// One row in the nearby-airport view rendered by the procedure-selection UI.
// Decoupled from `VfrAirport` so the UI does not need to know the full domain
// model — it only renders ICAO, name, distance, the runways the pilot can
// actually pick (i.e. those with an entry in `arrival_routes`), and an
// optional per-runway note text shown on hover.
struct NearbyAirport
{
    std::string              icao;
    std::string              name;
    double                   distance_nm;
    std::vector<std::string> available_runways;
    // Free-text per-runway notes, copied verbatim from the airport JSON's
    // `runway_notes` field. Sparse: a runway designator is only present here
    // when the JSON ships a description for it. Missing entries → no tooltip.
    std::map<std::string, std::string> runway_notes;
};

// Return all airports within `max_distance_nm` of `aircraft_position`, sorted
// nearest-first and capped at `max_count` rows. Pure function, SDK-free, fully
// unit-testable. The caller (UI) is expected to invoke this at ~1 Hz on the
// main thread; for the dataset sizes we expect (< 100 airports) the linear
// scan + sort is well below any noticeable cost.
std::vector<NearbyAirport> find_nearby_airports(const VfrAirportDatabase &db, const Coordinate &aircraft_position,
                                                double max_distance_nm, std::size_t max_count);
} // namespace xpswissvfr::data

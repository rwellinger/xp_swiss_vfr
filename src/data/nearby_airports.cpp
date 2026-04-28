#include "nearby_airports.hpp"

#include "geometry/coordinate_math.hpp"

#include <algorithm>

namespace xpswissvfr::data
{
namespace
{

std::vector<std::string> available_runways(const VfrAirport &airport)
{
    std::vector<std::string> runways;
    runways.reserve(airport.arrival_routes.size());
    for (const auto &[designator, _route] : airport.arrival_routes)
        runways.push_back(designator);
    return runways;
}

} // namespace

std::vector<NearbyAirport> find_nearby_airports(const VfrAirportDatabase &db, const Coordinate &aircraft_position,
                                                double max_distance_nm, std::size_t max_count)
{
    std::vector<NearbyAirport> result;

    if (max_count == 0 || max_distance_nm <= 0.0)
        return result;

    result.reserve(db.size());
    for (const auto &[icao, airport] : db)
    {
        double distance = geometry::distance_nm(aircraft_position, airport.arp);
        if (distance > max_distance_nm)
            continue;
        result.push_back({icao, airport.name, distance, available_runways(airport), airport.runway_notes});
    }

    std::sort(result.begin(), result.end(),
              [](const NearbyAirport &a, const NearbyAirport &b) { return a.distance_nm < b.distance_nm; });

    if (result.size() > max_count)
        result.resize(max_count);

    return result;
}

} // namespace xpswissvfr::data

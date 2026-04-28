#include "nearby_airports.hpp"

#include "geometry/coordinate_math.hpp"

#include <algorithm>

namespace xpswissvfr::data
{
namespace
{

std::vector<ArrivalOption> available_options(const VfrAirport &airport)
{
    std::vector<ArrivalOption> options;
    for (const auto &[designator, routes] : airport.arrival_routes)
    {
        for (const auto &route : routes)
            options.push_back({designator, route.label});
    }
    return options;
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
        result.push_back({icao, airport.name, distance, available_options(airport), airport.runway_notes});
    }

    std::sort(result.begin(), result.end(),
              [](const NearbyAirport &a, const NearbyAirport &b) { return a.distance_nm < b.distance_nm; });

    if (result.size() > max_count)
        result.resize(max_count);

    return result;
}

} // namespace xpswissvfr::data

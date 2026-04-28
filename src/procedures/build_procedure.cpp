#include "procedures/build_procedure.hpp"

#include "geometry/coordinate_math.hpp"

namespace xpswissvfr::procedures
{
namespace
{
constexpr int         DW_BEG_OFFSET_FT     = 400;  // DW-BEG sits above pattern altitude
constexpr int         FAF_OFFSET_FT        = -400; // FAF sits below pattern altitude
constexpr double      METERS_PER_NM        = 1852.0;
constexpr std::size_t DISPLAY_NAME_MAX_LEN = 6; // X1000 truncates beyond this

const data::Runway *find_runway(const data::VfrAirport &airport, const std::string &designator)
{
    for (const auto &runway : airport.runways)
    {
        if (runway.designator == designator)
            return &runway;
    }
    return nullptr;
}

const data::Waypoint *find_vrp(const data::VfrAirport &airport, const std::string &name)
{
    for (const auto &vrp : airport.vrps)
    {
        if (vrp.name == name)
            return &vrp;
    }
    return nullptr;
}

std::string truncate_display(std::string name)
{
    if (name.size() > DISPLAY_NAME_MAX_LEN)
        name.resize(DISPLAY_NAME_MAX_LEN);
    return name;
}

// Bearing from the runway centerline toward the downwind side of the pattern.
// Left circuit → downwind on the left of the direction of flight (heading − 90°).
// Right circuit → downwind on the right (heading + 90°).
double downwind_lateral_bearing(const data::Runway &runway)
{
    const double sign = runway.circuit_pattern == "right" ? 1.0 : -1.0;
    return runway.heading_true + sign * 90.0;
}

std::optional<int> vrp_altitude_ft(const data::Waypoint &vrp)
{
    if (vrp.altitude_ft_min && vrp.altitude_ft_max)
        return (*vrp.altitude_ft_min + *vrp.altitude_ft_max) / 2;
    return vrp.altitude_ft;
}
} // namespace

// Find the arrival route by label inside the runway's route list. Empty label
// = first route (the convenient default for single-route airports).
const data::ArrivalRoute *find_route(const std::vector<data::ArrivalRoute> &routes, const std::string &route_label)
{
    if (routes.empty())
        return nullptr;
    if (route_label.empty())
        return &routes.front();
    for (const auto &r : routes)
    {
        if (r.label == route_label)
            return &r;
    }
    return nullptr;
}

std::optional<Procedure> build_procedure(const data::VfrAirport &airport, const std::string &runway_designator,
                                         const std::string &route_label)
{
    const data::Runway *runway = find_runway(airport, runway_designator);
    if (runway == nullptr)
        return std::nullopt;

    const auto route_it = airport.arrival_routes.find(runway_designator);
    if (route_it == airport.arrival_routes.end() || route_it->second.empty())
        return std::nullopt;

    const data::ArrivalRoute *route = find_route(route_it->second, route_label);
    if (route == nullptr)
        return std::nullopt;

    Procedure procedure;
    procedure.airport_icao         = airport.icao;
    procedure.runway_designator    = runway_designator;
    procedure.route_label          = route->label;
    procedure.airport_elevation_ft = airport.elevation_ft;

    for (const auto &vrp_name : route->vrps)
    {
        const data::Waypoint *vrp = find_vrp(airport, vrp_name);
        if (vrp == nullptr)
            return std::nullopt;
        procedure.waypoints.push_back({truncate_display(vrp->name), vrp->position, vrp_altitude_ft(*vrp)});
    }

    const int    pattern_alt_ft     = airport.elevation_ft + airport.circuit_pattern.altitude_ft_agl;
    const double lateral_bearing    = downwind_lateral_bearing(*runway);
    const double half_runway_nm     = (runway->length_m / 2.0) / METERS_PER_NM;
    const double reverse_heading    = runway->heading_true + 180.0;
    const double downwind_offset_nm = airport.circuit_pattern.downwind_offset_nm;
    const double final_distance_nm  = airport.circuit_pattern.final_distance_nm;

    // Landing threshold (half a runway length opposite the runway heading) and
    // take-off threshold (half a runway length along the runway heading).
    const data::Coordinate thr_landing = geometry::offset(airport.arp, reverse_heading, half_runway_nm);
    const data::Coordinate thr_takeoff = geometry::offset(airport.arp, runway->heading_true, half_runway_nm);

    // FAF on the extended centerline, final_distance_nm beyond the landing threshold.
    const data::Coordinate faf_position = geometry::offset(thr_landing, reverse_heading, final_distance_nm);

    // DW-END abeam FAF on the downwind line: a clean 90° base leg from DW-END
    // to FAF. DW-BEG abeam the take-off threshold on the downwind line: pattern
    // entry point. Together this puts every turn (DW-BEG, DW-END, FAF) at 90°,
    // which the X1000 renders as straight legs instead of large anticipation
    // arcs that loop back over themselves.
    const data::Coordinate dw_end_position = geometry::offset(faf_position, lateral_bearing, downwind_offset_nm);
    const data::Coordinate dw_beg_position = geometry::offset(thr_takeoff, lateral_bearing, downwind_offset_nm);

    procedure.waypoints.push_back({"DW-BEG", dw_beg_position, pattern_alt_ft + DW_BEG_OFFSET_FT});
    procedure.waypoints.push_back({"DW-END", dw_end_position, pattern_alt_ft});
    procedure.waypoints.push_back({"FAF", faf_position, pattern_alt_ft + FAF_OFFSET_FT});
    // RWY waypoint sits at the landing threshold; its altitude is the airport
    // elevation so the X1000 has a sensible VNAV target (FAF → threshold).
    procedure.waypoints.push_back({truncate_display("RWY" + runway_designator), thr_landing, airport.elevation_ft});

    return procedure;
}
} // namespace xpswissvfr::procedures

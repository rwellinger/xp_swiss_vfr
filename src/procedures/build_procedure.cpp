#include "procedures/build_procedure.hpp"

#include "geometry/coordinate_math.hpp"
#include "geometry/terrain.hpp"

#include <cmath>
#include <string>

namespace xpswissvfr::procedures
{
namespace
{
constexpr int         DW_BEG_OFFSET_FT       = 400;  // DW-BEG sits above pattern altitude
constexpr int         FAF_OFFSET_FT          = -400; // FAF sits below pattern altitude
constexpr int         MAX_DESCENT_PER_LEG_FT = 400;  // helper threshold from VRP down to pattern entry
constexpr int         TERRAIN_MARGIN_FT      = 500;  // clamp synthetic waypoints to terrain + this
constexpr double      METERS_PER_NM          = 1852.0;
constexpr std::size_t DISPLAY_NAME_MAX_LEN   = 6; // X1000 truncates beyond this

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

// Inserts D1, D2, ... between the last route VRP and the pattern entry when
// the altitude delta is large enough to be uncomfortable as a single step.
// Helpers descend linearly from the last VRP altitude toward pattern_entry_alt
// along the line VRP → ARP — close enough to "fly toward the field while
// descending" for the X1000 to render a clean VNAV path.
void push_descent_helpers(Procedure &procedure, const data::VfrAirport &airport, int pattern_entry_alt_ft,
                          const geometry::TerrainSource &terrain)
{
    if (procedure.waypoints.empty())
        return;
    const ProcedureWaypoint last_vrp = procedure.waypoints.back();
    if (!last_vrp.altitude_ft)
        return;

    const int last_alt = *last_vrp.altitude_ft;
    const int delta    = last_alt - pattern_entry_alt_ft;
    if (delta <= MAX_DESCENT_PER_LEG_FT)
        return;

    const int n_helpers = static_cast<int>(std::ceil(static_cast<double>(delta) / MAX_DESCENT_PER_LEG_FT)) - 1;
    if (n_helpers <= 0)
        return;

    for (int i = 1; i <= n_helpers; ++i)
    {
        const double           f   = static_cast<double>(i) / (n_helpers + 1);
        const data::Coordinate pos = geometry::interpolate(last_vrp.position, airport.arp, f);
        const int linear_alt = last_alt - static_cast<int>(static_cast<double>(last_alt - pattern_entry_alt_ft) * f);
        const int safe_alt   = geometry::clamp_to_terrain(terrain, pos, linear_alt, TERRAIN_MARGIN_FT);
        procedure.waypoints.push_back({"D" + std::to_string(i), pos, safe_alt});
    }
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
                                         const std::string &route_label, const geometry::TerrainSource &terrain)
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

    const int    pattern_alt_ft       = airport.elevation_ft + airport.circuit_pattern.altitude_ft_agl;
    const int    pattern_entry_alt_ft = pattern_alt_ft + DW_BEG_OFFSET_FT;
    const double lateral_bearing      = downwind_lateral_bearing(*runway);
    const double half_runway_nm       = (runway->length_m / 2.0) / METERS_PER_NM;
    const double reverse_heading      = runway->heading_true + 180.0;
    const double downwind_offset_nm   = airport.circuit_pattern.downwind_offset_nm;
    const double final_distance_nm    = airport.circuit_pattern.final_distance_nm;

    push_descent_helpers(procedure, airport, pattern_entry_alt_ft, terrain);

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

    const int dw_beg_alt =
        geometry::clamp_to_terrain(terrain, dw_beg_position, pattern_entry_alt_ft, TERRAIN_MARGIN_FT);
    const int dw_end_alt = geometry::clamp_to_terrain(terrain, dw_end_position, pattern_alt_ft, TERRAIN_MARGIN_FT);
    const int faf_alt =
        geometry::clamp_to_terrain(terrain, faf_position, pattern_alt_ft + FAF_OFFSET_FT, TERRAIN_MARGIN_FT);

    procedure.waypoints.push_back({"DW-BEG", dw_beg_position, dw_beg_alt});
    procedure.waypoints.push_back({"DW-END", dw_end_position, dw_end_alt});
    procedure.waypoints.push_back({"FAF", faf_position, faf_alt});
    // RWY waypoint sits at the landing threshold; its altitude is the airport
    // elevation so the X1000 has a sensible VNAV target (FAF → threshold).
    procedure.waypoints.push_back({truncate_display("RWY" + runway_designator), thr_landing, airport.elevation_ft});

    return procedure;
}

std::optional<Procedure> build_procedure(const data::VfrAirport &airport, const std::string &runway_designator,
                                         const std::string &route_label)
{
    return build_procedure(airport, runway_designator, route_label, geometry::default_terrain_source());
}
} // namespace xpswissvfr::procedures

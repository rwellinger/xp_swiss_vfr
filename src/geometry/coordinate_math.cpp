#include "geometry/coordinate_math.hpp"

#include <cmath>

namespace xpswissvfr::geometry
{
namespace
{
constexpr double NM_PER_DEGREE_LAT = 60.0;
constexpr double PI                = 3.14159265358979323846;
constexpr double DEG_TO_RAD        = PI / 180.0;
constexpr double RAD_TO_DEG        = 180.0 / PI;

double nm_per_degree_lon(double lat_deg) { return NM_PER_DEGREE_LAT * std::cos(lat_deg * DEG_TO_RAD); }

double normalize_bearing(double bearing)
{
    double normalized = std::fmod(bearing, 360.0);
    if (normalized < 0.0)
    {
        normalized += 360.0;
    }
    return normalized;
}
} // namespace

double distance_nm(const data::Coordinate &a, const data::Coordinate &b)
{
    double mid_lat = 0.5 * (a.lat + b.lat);
    double dy      = (b.lat - a.lat) * NM_PER_DEGREE_LAT;
    double dx      = (b.lon - a.lon) * nm_per_degree_lon(mid_lat);
    return std::sqrt(dy * dy + dx * dx);
}

double bearing_deg(const data::Coordinate &from, const data::Coordinate &to)
{
    double mid_lat = 0.5 * (from.lat + to.lat);
    double dy      = (to.lat - from.lat) * NM_PER_DEGREE_LAT;
    double dx      = (to.lon - from.lon) * nm_per_degree_lon(mid_lat);
    return normalize_bearing(std::atan2(dx, dy) * RAD_TO_DEG);
}

data::Coordinate offset(const data::Coordinate &from, double bearing_deg, double distance_nm)
{
    double rad     = bearing_deg * DEG_TO_RAD;
    double dy      = distance_nm * std::cos(rad);
    double dx      = distance_nm * std::sin(rad);
    double new_lat = from.lat + dy / NM_PER_DEGREE_LAT;
    // Use the latitude midpoint for the longitude scaling so that offset is the
    // exact inverse of distance_nm + bearing_deg, which both sample at mid_lat.
    double mid_lat = 0.5 * (from.lat + new_lat);
    return {new_lat, from.lon + dx / nm_per_degree_lon(mid_lat)};
}
} // namespace xpswissvfr::geometry

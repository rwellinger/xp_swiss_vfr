#include "geometry/coordinate_math.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <cmath>

using xpswissvfr::data::Coordinate;
using xpswissvfr::geometry::bearing_deg;
using xpswissvfr::geometry::distance_nm;
using xpswissvfr::geometry::offset;

namespace
{
constexpr Coordinate LSZG_ARP{47.1816, 7.4172};
}

TEST_CASE("distance_nm: identical points → 0", "[geometry]")
{
    REQUIRE(distance_nm(LSZG_ARP, LSZG_ARP) == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("distance_nm: 1° latitude ≈ 60 NM", "[geometry]")
{
    Coordinate a{47.0, 7.0};
    Coordinate b{48.0, 7.0};
    REQUIRE(distance_nm(a, b) == Catch::Approx(60.0).margin(0.1));
}

TEST_CASE("distance_nm: 1° longitude at 47°N ≈ 60 × cos(47°) ≈ 40.92 NM", "[geometry]")
{
    Coordinate a{47.0, 7.0};
    Coordinate b{47.0, 8.0};
    double     expected = 60.0 * std::cos(47.0 * 3.14159265358979323846 / 180.0);
    REQUIRE(distance_nm(a, b) == Catch::Approx(expected).margin(0.05));
}

TEST_CASE("bearing_deg: due north → 0", "[geometry]")
{
    Coordinate from{47.0, 7.0};
    Coordinate to{48.0, 7.0};
    REQUIRE(bearing_deg(from, to) == Catch::Approx(0.0).margin(0.1));
}

TEST_CASE("bearing_deg: due east → 90", "[geometry]")
{
    Coordinate from{47.0, 7.0};
    Coordinate to{47.0, 8.0};
    REQUIRE(bearing_deg(from, to) == Catch::Approx(90.0).margin(0.1));
}

TEST_CASE("bearing_deg: due south → 180", "[geometry]")
{
    Coordinate from{48.0, 7.0};
    Coordinate to{47.0, 7.0};
    REQUIRE(bearing_deg(from, to) == Catch::Approx(180.0).margin(0.1));
}

TEST_CASE("bearing_deg: due west → 270", "[geometry]")
{
    Coordinate from{47.0, 8.0};
    Coordinate to{47.0, 7.0};
    REQUIRE(bearing_deg(from, to) == Catch::Approx(270.0).margin(0.1));
}

TEST_CASE("offset is the inverse of distance + bearing", "[geometry]")
{
    for (double b : {30.0, 60.0, 120.0, 240.0, 350.0})
    {
        for (double d : {0.5, 1.0, 3.0, 6.0})
        {
            INFO("bearing=" << b << " distance=" << d);
            Coordinate p2 = offset(LSZG_ARP, b, d);
            REQUIRE(distance_nm(LSZG_ARP, p2) == Catch::Approx(d).margin(0.001));
            REQUIRE(bearing_deg(LSZG_ARP, p2) == Catch::Approx(b).margin(0.5));
        }
    }
}

TEST_CASE("offset 6 NM east of LSZG ARP lands at the E-sector entry latitude", "[geometry]")
{
    Coordinate result = offset(LSZG_ARP, 90.0, 6.0);
    REQUIRE(result.lat == Catch::Approx(LSZG_ARP.lat).margin(0.001));
    REQUIRE(result.lon > LSZG_ARP.lon);
    REQUIRE(distance_nm(LSZG_ARP, result) == Catch::Approx(6.0).margin(0.005));
}

TEST_CASE("offset on RWY 06 final-line: THR 06 ≈ ARP + 0.32 NM at bearing 240°", "[geometry]")
{
    // RWY 06 is 1200 m long; THR 06 is half that, 600 m, southwest of the ARP
    // along the runway centerline (true heading 240° from mid-field).
    constexpr double half_runway_nm = 0.6 / 1.852; // 600 m → NM
    Coordinate       thr_06         = offset(LSZG_ARP, 240.0, half_runway_nm);
    REQUIRE(thr_06.lat == Catch::Approx(47.179).margin(0.002));
    REQUIRE(thr_06.lon == Catch::Approx(7.411).margin(0.002));
}

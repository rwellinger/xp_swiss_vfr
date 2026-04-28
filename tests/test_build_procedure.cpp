#include "data/vfr_airport.hpp"
#include "geometry/coordinate_math.hpp"
#include "procedures/build_procedure.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <cmath>

using xpswissvfr::data::CircuitPattern;
using xpswissvfr::data::Coordinate;
using xpswissvfr::data::Runway;
using xpswissvfr::data::VfrAirport;
using xpswissvfr::data::Waypoint;
using xpswissvfr::geometry::bearing_deg;
using xpswissvfr::procedures::build_procedure;
using xpswissvfr::procedures::Procedure;

namespace
{
// LSZG test airport built in code so tests stay independent of fixture JSON
// and so we can exercise multi-VRP arrival routes and long VRP names that the
// minimal lszg_valid.json fixture does not cover.
VfrAirport make_lszg_airport()
{
    VfrAirport a;
    a.icao         = "LSZG";
    a.name         = "Grenchen";
    a.elevation_ft = 1411;
    a.arp          = {47.1816, 7.4172};
    a.runways      = {
        {"06", 60.0, 1200, "asphalt", "left"},
        {"24", 240.0, 1200, "asphalt", "right"},
    };
    auto vrp = [](std::string name, double lat, double lon) {
        Waypoint w;
        w.name             = std::move(name);
        w.position         = {lat, lon};
        w.altitude_ft_min  = 2500;
        w.altitude_ft_max  = 3500;
        w.mandatory_report = false;
        return w;
    };
    a.vrps = {
        vrp("E", 47.1800, 7.5500),    vrp("E1", 47.1900, 7.4800), vrp("HE", 47.2100, 7.4800),
        vrp("HW", 47.1700, 7.3700),   vrp("W", 47.1300, 7.2300),  vrp("S", 47.1200, 7.5100),
        vrp("ABM ALTREU", 47.1860, 7.4490),
    };
    a.arrival_routes = {
        {"06", {"E", "E1"}},
        {"24", {"W", "HW"}},
    };
    a.circuit_pattern = CircuitPattern{1000, 0.7};
    return a;
}

// Computed pattern altitude for LSZG: elevation 1411 ft + circuit 1000 ft AGL.
constexpr int LSZG_PATTERN_ALT_FT = 2411;

// Tolerance for coordinate checks. The equirectangular formulas are not
// bit-exact across forward/inverse paths, but for VFR-pattern distances
// (< 10 NM) the agreement is well within 0.001° (~60 m).
constexpr double POSITION_TOL = 0.002;
} // namespace

TEST_CASE("build_procedure: unknown runway returns nullopt", "[build_procedure]")
{
    auto result = build_procedure(make_lszg_airport(), "13");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("build_procedure: missing arrival route returns nullopt", "[build_procedure]")
{
    VfrAirport a = make_lszg_airport();
    a.arrival_routes.erase("06");
    auto result = build_procedure(a, "06");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("build_procedure: arrival route referencing unknown VRP returns nullopt", "[build_procedure]")
{
    VfrAirport a               = make_lszg_airport();
    a.arrival_routes.at("06") = {"E", "DOES_NOT_EXIST"};
    auto result               = build_procedure(a, "06");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("build_procedure LSZG RWY 06: VRPs prepended verbatim", "[build_procedure]")
{
    auto p = build_procedure(make_lszg_airport(), "06");
    REQUIRE(p.has_value());
    REQUIRE(p->airport_icao == "LSZG");
    REQUIRE(p->runway_designator == "06");
    REQUIRE(p->waypoints.size() == 6);

    REQUIRE(p->waypoints[0].display_name == "E");
    REQUIRE(p->waypoints[0].position.lat == Catch::Approx(47.1800));
    REQUIRE(p->waypoints[0].position.lon == Catch::Approx(7.5500));
    REQUIRE(p->waypoints[0].altitude_ft.has_value());
    REQUIRE(*p->waypoints[0].altitude_ft == 3000);

    REQUIRE(p->waypoints[1].display_name == "E1");
    REQUIRE(p->waypoints[1].position.lat == Catch::Approx(47.1900));
    REQUIRE(p->waypoints[1].position.lon == Catch::Approx(7.4800));
    REQUIRE(*p->waypoints[1].altitude_ft == 3000);
}

TEST_CASE("build_procedure LSZG RWY 06: pattern geometry (left circuit)", "[build_procedure]")
{
    auto p = build_procedure(make_lszg_airport(), "06");
    REQUIRE(p.has_value());

    const auto &dw_beg = p->waypoints[2];
    const auto &dw_end = p->waypoints[3];
    const auto &faf    = p->waypoints[4];
    const auto &thr    = p->waypoints[5];

    SECTION("DW-BEG sits abeam the take-off threshold on the offset downwind line")
    {
        REQUIRE(dw_beg.display_name == "DW-BEG");
        REQUIRE(dw_beg.position.lat == Catch::Approx(47.1944).margin(POSITION_TOL));
        REQUIRE(dw_beg.position.lon == Catch::Approx(7.4155).margin(POSITION_TOL));
        REQUIRE(*dw_beg.altitude_ft == LSZG_PATTERN_ALT_FT + 400);
    }

    SECTION("DW-END sits abeam FAF on the offset downwind line")
    {
        REQUIRE(dw_end.display_name == "DW-END");
        REQUIRE(dw_end.position.lat == Catch::Approx(47.1807).margin(POSITION_TOL));
        REQUIRE(dw_end.position.lon == Catch::Approx(7.3805).margin(POSITION_TOL));
        REQUIRE(*dw_end.altitude_ft == LSZG_PATTERN_ALT_FT);
    }

    SECTION("FAF sits 1 NM SW of THR on the extended centerline")
    {
        REQUIRE(faf.display_name == "FAF");
        REQUIRE(faf.position.lat == Catch::Approx(47.1706).margin(POSITION_TOL));
        REQUIRE(faf.position.lon == Catch::Approx(7.3891).margin(POSITION_TOL));
        REQUIRE(*faf.altitude_ft == LSZG_PATTERN_ALT_FT - 400);
    }

    SECTION("RWY06 lands at THR 06, half a runway-length SW of the ARP")
    {
        REQUIRE(thr.display_name == "RWY06");
        REQUIRE(thr.position.lat == Catch::Approx(47.1789).margin(POSITION_TOL));
        REQUIRE(thr.position.lon == Catch::Approx(7.4103).margin(POSITION_TOL));
        REQUIRE(*thr.altitude_ft == 0);
    }
}

TEST_CASE("build_procedure LSZG RWY 06: every leg is parallel or perpendicular to the runway", "[build_procedure]")
{
    auto p = build_procedure(make_lszg_airport(), "06");
    REQUIRE(p.has_value());

    const auto &dw_beg = p->waypoints[2].position;
    const auto &dw_end = p->waypoints[3].position;
    const auto &faf    = p->waypoints[4].position;
    const auto &thr    = p->waypoints[5].position;

    constexpr double RUNWAY_HEADING_06 = 60.0;
    constexpr double TOL_DEG           = 1.0;

    auto delta_to_axis = [](double bearing, double axis) {
        double diff = std::fmod(std::abs(bearing - axis) + 180.0, 180.0);
        return std::min(diff, 180.0 - diff);
    };

    SECTION("downwind leg DW-BEG → DW-END is parallel to the runway (heading 240°, reverse of 60°)")
    {
        const double brg = bearing_deg(dw_beg, dw_end);
        REQUIRE(delta_to_axis(brg, RUNWAY_HEADING_06) == Catch::Approx(0.0).margin(TOL_DEG));
    }

    SECTION("base leg DW-END → FAF is perpendicular to the runway (heading 150°)")
    {
        const double brg = bearing_deg(dw_end, faf);
        REQUIRE(delta_to_axis(brg, RUNWAY_HEADING_06 + 90.0) == Catch::Approx(0.0).margin(TOL_DEG));
    }

    SECTION("final leg FAF → THR is parallel to the runway (heading 60°)")
    {
        const double brg = bearing_deg(faf, thr);
        REQUIRE(delta_to_axis(brg, RUNWAY_HEADING_06) == Catch::Approx(0.0).margin(TOL_DEG));
    }
}

TEST_CASE("build_procedure LSZG RWY 24: pattern is mirrored (right circuit)", "[build_procedure]")
{
    auto p = build_procedure(make_lszg_airport(), "24");
    REQUIRE(p.has_value());
    REQUIRE(p->runway_designator == "24");
    REQUIRE(p->waypoints.size() == 6);

    REQUIRE(p->waypoints[0].display_name == "W");
    REQUIRE(p->waypoints[1].display_name == "HW");

    // Right circuit on RWY 24 (heading 240°) puts the downwind on the same
    // physical NW-of-runway airspace as RWY 06's left circuit, but the aircraft
    // flies it in the opposite direction. So DW-BEG sits abeam THR 24 (= the
    // NE end, take-off threshold for RWY 24) and DW-END sits abeam FAF 24
    // (= NE of the field, on the extended centerline of RWY 24's approach).
    const auto &dw_beg = p->waypoints[2];
    const auto &dw_end = p->waypoints[3];
    REQUIRE(dw_beg.display_name == "DW-BEG");
    REQUIRE(dw_beg.position.lat == Catch::Approx(47.1890).margin(POSITION_TOL));
    REQUIRE(dw_beg.position.lon == Catch::Approx(7.4017).margin(POSITION_TOL));
    REQUIRE(dw_end.display_name == "DW-END");
    REQUIRE(dw_end.position.lat == Catch::Approx(47.2027).margin(POSITION_TOL));
    REQUIRE(dw_end.position.lon == Catch::Approx(7.4367).margin(POSITION_TOL));

    const auto &faf = p->waypoints[4];
    const auto &thr = p->waypoints[5];
    REQUIRE(faf.display_name == "FAF");
    REQUIRE(faf.position.lat == Catch::Approx(47.1926).margin(POSITION_TOL));
    REQUIRE(faf.position.lon == Catch::Approx(7.4453).margin(POSITION_TOL));
    REQUIRE(thr.display_name == "RWY24");
    REQUIRE(thr.position.lat == Catch::Approx(47.1843).margin(POSITION_TOL));
    REQUIRE(thr.position.lon == Catch::Approx(7.4241).margin(POSITION_TOL));
    REQUIRE(*thr.altitude_ft == 0);
}

TEST_CASE("build_procedure altitudes step down monotonically and end at 0", "[build_procedure]")
{
    auto p = build_procedure(make_lszg_airport(), "06");
    REQUIRE(p.has_value());

    for (std::size_t i = 1; i < p->waypoints.size(); ++i)
    {
        const auto &prev = p->waypoints[i - 1];
        const auto &cur  = p->waypoints[i];
        INFO("between index " << (i - 1) << " (" << prev.display_name << ") and " << i << " (" << cur.display_name
                              << ")");
        if (prev.altitude_ft && cur.altitude_ft)
            REQUIRE(*cur.altitude_ft <= *prev.altitude_ft);
    }
    REQUIRE(*p->waypoints.back().altitude_ft == 0);
}

TEST_CASE("build_procedure: VRP names longer than 6 chars are truncated", "[build_procedure]")
{
    VfrAirport a              = make_lszg_airport();
    a.arrival_routes.at("06") = {"ABM ALTREU"};
    auto p                    = build_procedure(a, "06");
    REQUIRE(p.has_value());
    REQUIRE(p->waypoints[0].display_name == "ABM AL");
}

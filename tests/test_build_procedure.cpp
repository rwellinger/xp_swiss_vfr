#include "data/vfr_airport.hpp"
#include "geometry/coordinate_math.hpp"
#include "geometry/terrain.hpp"
#include "procedures/build_procedure.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

using xpswissvfr::data::CircuitPattern;
using xpswissvfr::data::Coordinate;
using xpswissvfr::data::Runway;
using xpswissvfr::data::VfrAirport;
using xpswissvfr::data::Waypoint;
using xpswissvfr::geometry::bearing_deg;
using xpswissvfr::geometry::distance_nm;
using xpswissvfr::geometry::TerrainSource;
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
        {"06", 60.0, 1200, "asphalt", "right"},
        {"24", 240.0, 1200, "asphalt", "left"},
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
        {"06", {{"via E", {"E", "E1"}}}},
        {"24", {{"via W", {"W", "HW"}}}},
    };
    a.circuit_pattern = CircuitPattern{1000, 1.0, 1.5};
    return a;
}

// Computed pattern altitude for LSZG: elevation 1411 ft + circuit 1000 ft AGL.
constexpr int LSZG_PATTERN_ALT_FT = 2411;
// Field elevation, used as the runway-threshold altitude target so the X1000
// has a sensible VNAV descent target from FAF down to the threshold.
constexpr int LSZG_ELEVATION_FT = 1411;

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
    a.arrival_routes.at("06") = {{"via E", {"E", "DOES_NOT_EXIST"}}};
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

TEST_CASE("build_procedure LSZG RWY 06: pattern geometry (right circuit)", "[build_procedure]")
{
    auto p = build_procedure(make_lszg_airport(), "06");
    REQUIRE(p.has_value());

    const auto &dw_beg = p->waypoints[2];
    const auto &dw_end = p->waypoints[3];
    const auto &faf    = p->waypoints[4];
    const auto &thr    = p->waypoints[5];

    // Right circuit on RWY 06: downwind sits SE of the runway (over the Aare),
    // not NW. DW-BEG is abeam the take-off threshold (NE end), DW-END is
    // abeam FAF (SW end).
    SECTION("DW-BEG sits abeam the take-off threshold on the SE downwind line")
    {
        REQUIRE(dw_beg.display_name == "DW-BEG");
        REQUIRE(dw_beg.position.lat == Catch::Approx(47.1699).margin(POSITION_TOL));
        REQUIRE(dw_beg.position.lon == Catch::Approx(7.4363).margin(POSITION_TOL));
        REQUIRE(*dw_beg.altitude_ft == LSZG_PATTERN_ALT_FT + 400);
    }

    SECTION("DW-END sits abeam FAF on the SE downwind line")
    {
        REQUIRE(dw_end.display_name == "DW-END");
        REQUIRE(dw_end.position.lat == Catch::Approx(47.1520).margin(POSITION_TOL));
        REQUIRE(dw_end.position.lon == Catch::Approx(7.3907).margin(POSITION_TOL));
        REQUIRE(*dw_end.altitude_ft == LSZG_PATTERN_ALT_FT);
    }

    SECTION("FAF sits final_distance_nm SW of THR on the extended centerline")
    {
        REQUIRE(faf.display_name == "FAF");
        REQUIRE(faf.position.lat == Catch::Approx(47.1664).margin(POSITION_TOL));
        REQUIRE(faf.position.lon == Catch::Approx(7.3785).margin(POSITION_TOL));
        REQUIRE(*faf.altitude_ft == LSZG_PATTERN_ALT_FT - 400);
    }

    SECTION("RWY06 lands at THR 06, half a runway-length SW of the ARP")
    {
        REQUIRE(thr.display_name == "RWY06");
        REQUIRE(thr.position.lat == Catch::Approx(47.1789).margin(POSITION_TOL));
        REQUIRE(thr.position.lon == Catch::Approx(7.4103).margin(POSITION_TOL));
        REQUIRE(*thr.altitude_ft == LSZG_ELEVATION_FT);
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

TEST_CASE("build_procedure LSZG RWY 24: pattern is mirrored (left circuit)", "[build_procedure]")
{
    auto p = build_procedure(make_lszg_airport(), "24");
    REQUIRE(p.has_value());
    REQUIRE(p->runway_designator == "24");
    REQUIRE(p->waypoints.size() == 6);

    REQUIRE(p->waypoints[0].display_name == "W");
    REQUIRE(p->waypoints[1].display_name == "HW");

    // Left circuit on RWY 24 puts the downwind on the same physical SE-of-
    // runway airspace as RWY 06's right circuit, but the aircraft flies it
    // in the opposite direction. So DW-BEG sits abeam THR 24's take-off end
    // (= SW of the field, the RWY 06 landing threshold position) and DW-END
    // sits abeam FAF 24 (= NE of the field, on the extended centerline).
    const auto &dw_beg = p->waypoints[2];
    const auto &dw_end = p->waypoints[3];
    REQUIRE(dw_beg.display_name == "DW-BEG");
    REQUIRE(dw_beg.position.lat == Catch::Approx(47.1645).margin(POSITION_TOL));
    REQUIRE(dw_beg.position.lon == Catch::Approx(7.4226).margin(POSITION_TOL));
    REQUIRE(dw_end.display_name == "DW-END");
    REQUIRE(dw_end.position.lat == Catch::Approx(47.1824).margin(POSITION_TOL));
    REQUIRE(dw_end.position.lon == Catch::Approx(7.4682).margin(POSITION_TOL));

    const auto &faf = p->waypoints[4];
    const auto &thr = p->waypoints[5];
    REQUIRE(faf.display_name == "FAF");
    REQUIRE(faf.position.lat == Catch::Approx(47.1968).margin(POSITION_TOL));
    REQUIRE(faf.position.lon == Catch::Approx(7.4559).margin(POSITION_TOL));
    REQUIRE(thr.display_name == "RWY24");
    REQUIRE(thr.position.lat == Catch::Approx(47.1843).margin(POSITION_TOL));
    REQUIRE(thr.position.lon == Catch::Approx(7.4241).margin(POSITION_TOL));
    REQUIRE(*thr.altitude_ft == LSZG_ELEVATION_FT);
}

TEST_CASE("build_procedure altitudes step down monotonically and end at field elevation", "[build_procedure]")
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
    REQUIRE(*p->waypoints.back().altitude_ft == LSZG_ELEVATION_FT);
}

TEST_CASE("build_procedure: VRP names longer than 6 chars are truncated", "[build_procedure]")
{
    VfrAirport a              = make_lszg_airport();
    a.arrival_routes.at("06") = {{"via ABM", {"ABM ALTREU"}}};
    auto p                    = build_procedure(a, "06");
    REQUIRE(p.has_value());
    REQUIRE(p->waypoints[0].display_name == "ABM AL");
}

namespace
{
// LSZB-shaped airport for descent-helper tests: high VRPs (3500 ft) into a
// 1000 ft AGL pattern over a 1673 ft elevation field. Single VRP per route
// keeps the helper count predictable.
VfrAirport make_lszb_like_airport(int vrp_alt_min, int vrp_alt_max)
{
    VfrAirport a;
    a.icao         = "LSZB";
    a.name         = "Bern-Belp";
    a.elevation_ft = 1673;
    a.arp          = {46.91222, 7.49944};
    a.runways      = {
        {"14", 140.0, 1730, "asphalt", "left"},
        {"32", 320.0, 1730, "asphalt", "right"},
    };
    auto vrp = [&](std::string name, double lat, double lon) {
        Waypoint w;
        w.name             = std::move(name);
        w.position         = {lat, lon};
        w.altitude_ft_min  = vrp_alt_min;
        w.altitude_ft_max  = vrp_alt_max;
        w.mandatory_report = false;
        return w;
    };
    a.vrps           = {vrp("S", 46.843056, 7.498611), vrp("E", 47.000278, 7.646111)};
    a.arrival_routes = {
        {"14", {{"via S", {"S"}}, {"via E", {"E"}}}},
        {"32", {{"via S", {"S"}}}},
    };
    a.circuit_pattern = CircuitPattern{1000, 1.0, 1.5};
    return a;
}

int count_helpers(const Procedure &p)
{
    int n = 0;
    for (const auto &w : p.waypoints)
    {
        if (!w.display_name.empty() && w.display_name.front() == 'D' &&
            w.display_name.size() == 2 && std::isdigit(static_cast<unsigned char>(w.display_name[1])))
            ++n;
    }
    return n;
}
} // namespace

TEST_CASE("build_procedure: no descent helper when delta to pattern entry is small", "[build_procedure][helper]")
{
    // LSZG VRPs at 3000 ft, pattern entry at 2811 ft, delta 189 ft → no helper.
    auto p = build_procedure(make_lszg_airport(), "06");
    REQUIRE(p.has_value());
    REQUIRE(count_helpers(*p) == 0);
    REQUIRE(p->waypoints.size() == 6); // 2 VRPs + DW-BEG + DW-END + FAF + RWY
}

TEST_CASE("build_procedure: descent helper inserted on tight LSZB-shape sectors", "[build_procedure][helper]")
{
    auto a = make_lszb_like_airport(3000, 4000); // mid = 3500 ft, pattern entry 3073 ft, delta 427 ft
    auto p = build_procedure(a, "14", "via S");
    REQUIRE(p.has_value());
    REQUIRE(count_helpers(*p) == 1);
    // Layout: S → D1 → DW-BEG → DW-END → FAF → RWY
    REQUIRE(p->waypoints.size() == 6);
    REQUIRE(p->waypoints[0].display_name == "S");
    REQUIRE(p->waypoints[1].display_name == "D1");
    REQUIRE(p->waypoints[2].display_name == "DW-BEG");
}

TEST_CASE("build_procedure: helper position lies between VRP and ARP", "[build_procedure][helper]")
{
    auto a = make_lszb_like_airport(3000, 4000);
    auto p = build_procedure(a, "14", "via S");
    REQUIRE(p.has_value());

    const auto &vrp_s  = p->waypoints[0];
    const auto &helper = p->waypoints[1];

    const double vrp_to_arp     = distance_nm(vrp_s.position, a.arp);
    const double vrp_to_helper  = distance_nm(vrp_s.position, helper.position);
    const double helper_to_arp  = distance_nm(helper.position, a.arp);

    REQUIRE(vrp_to_helper < vrp_to_arp);
    REQUIRE(helper_to_arp < vrp_to_arp);
    // 1 helper out of n+1 segments → fraction 0.5 along VRP→ARP.
    REQUIRE(vrp_to_helper == Catch::Approx(vrp_to_arp * 0.5).epsilon(0.05));
}

TEST_CASE("build_procedure: helper altitude sits between VRP and pattern entry", "[build_procedure][helper]")
{
    auto a = make_lszb_like_airport(3000, 4000);
    auto p = build_procedure(a, "14", "via S");
    REQUIRE(p.has_value());

    const int vrp_alt           = *p->waypoints[0].altitude_ft;
    const int helper_alt        = *p->waypoints[1].altitude_ft;
    const int dw_beg_alt        = *p->waypoints[2].altitude_ft;
    constexpr int PATTERN_ENTRY = 1673 + 1000 + 400; // elevation + 1000 AGL + DW-BEG offset

    REQUIRE(vrp_alt == 3500);
    REQUIRE(dw_beg_alt == PATTERN_ENTRY);
    REQUIRE(helper_alt < vrp_alt);
    REQUIRE(helper_alt > dw_beg_alt);
}

TEST_CASE("build_procedure: huge VRP altitude triggers multiple helpers, monotonic descent", "[build_procedure][helper]")
{
    // VRP at 5000 ft, pattern entry 3073 ft, delta 1927 ft, threshold 400 → 4 helpers.
    auto a = make_lszb_like_airport(4500, 5500);
    auto p = build_procedure(a, "14", "via S");
    REQUIRE(p.has_value());
    REQUIRE(count_helpers(*p) == 4);

    REQUIRE(p->waypoints[1].display_name == "D1");
    REQUIRE(p->waypoints[2].display_name == "D2");
    REQUIRE(p->waypoints[3].display_name == "D3");
    REQUIRE(p->waypoints[4].display_name == "D4");

    for (std::size_t i = 1; i < p->waypoints.size(); ++i)
    {
        const auto &prev = p->waypoints[i - 1];
        const auto &cur  = p->waypoints[i];
        if (prev.altitude_ft && cur.altitude_ft)
            REQUIRE(*cur.altitude_ft <= *prev.altitude_ft);
    }
}

namespace
{
class FlatTerrainAt : public TerrainSource
{
  public:
    explicit FlatTerrainAt(int elevation_ft) : elevation_(elevation_ft) {}
    std::optional<int> elevation_ft_msl(const Coordinate &) const override { return elevation_; }

  private:
    int elevation_;
};
} // namespace

TEST_CASE("build_procedure: terrain source raises pattern altitudes below the safe floor", "[build_procedure][terrain]")
{
    // Terrain at 2700 ft (e.g. Belpberg) + 500 ft margin = 3200 ft floor. The
    // standard LSZB pattern legs (3073/2673/2273 ft) all sit below this and
    // should be lifted; the route VRP at 3500 ft must remain untouched.
    auto a = make_lszb_like_airport(3000, 4000);
    FlatTerrainAt terrain{2700};

    auto p = build_procedure(a, "14", "via S", terrain);
    REQUIRE(p.has_value());

    const auto find_alt = [&](const std::string &name) {
        for (const auto &w : p->waypoints)
            if (w.display_name == name)
                return w.altitude_ft;
        return std::optional<int>{};
    };

    REQUIRE(*find_alt("S") == 3500);          // VRP unchanged
    REQUIRE(*find_alt("DW-BEG") == 3200);     // clamped up from 3073
    REQUIRE(*find_alt("DW-END") == 3200);     // clamped up from 2673
    REQUIRE(*find_alt("FAF") == 3200);        // clamped up from 2273
}

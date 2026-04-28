#include "data/json_loader.hpp"
#include "data/validation.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <filesystem>
#include <variant>

namespace fs = std::filesystem;
using namespace xpswissvfr::data;

namespace
{

const fs::path FIXTURES = fs::path(XP_SWISS_VFR_TEST_FIXTURES_DIR);
const fs::path AIRPORTS = FIXTURES / "airports";

// Build a minimal but fully valid airport in code, so each test can mutate
// exactly the field its rule cares about without touching unrelated state.
VfrAirport make_valid_airport()
{
    VfrAirport a;
    a.icao         = "LSZG";
    a.name         = "Grenchen";
    a.elevation_ft = 1411;
    a.arp          = {47.1816, 7.4172};
    a.runways.push_back(Runway{"06", 60.0, 1200, "asphalt", "left"});

    Waypoint vrp_e;
    vrp_e.name             = "E";
    vrp_e.position         = {47.18, 7.55};
    vrp_e.altitude_ft_min  = 2500;
    vrp_e.altitude_ft_max  = 3500;
    vrp_e.mandatory_report = true;
    a.vrps.push_back(vrp_e);

    a.arrival_routes["06"] = {{"via E", {"E"}}};

    a.circuit_pattern = {1000, 0.7, 1.0};
    return a;
}

bool any_error_contains(const std::vector<std::string> &errors, const std::string &needle)
{
    return std::any_of(errors.begin(), errors.end(),
                       [&](const std::string &e) { return e.find(needle) != std::string::npos; });
}

} // namespace

TEST_CASE("validate accepts the LSZG fixture", "[validation]")
{
    auto       parsed  = parse_airport(AIRPORTS / "lszg_valid.json");
    const auto airport = std::get<VfrAirport>(parsed);
    REQUIRE(validate(airport).empty());
}

TEST_CASE("validate accepts the in-code valid airport", "[validation]")
{
    REQUIRE(validate(make_valid_airport()).empty());
}

TEST_CASE("ICAO format rule rejects non-uppercase or wrong-length codes", "[validation]")
{
    auto a = make_valid_airport();
    a.icao = "lszg";
    REQUIRE(any_error_contains(validate(a), "icao"));

    a.icao = "LSZ";
    REQUIRE(any_error_contains(validate(a), "icao"));

    a.icao = "LSZGX";
    REQUIRE(any_error_contains(validate(a), "icao"));
}

TEST_CASE("ARP range rule rejects out-of-range latitude", "[validation]")
{
    auto a    = make_valid_airport();
    a.arp.lat = 999.0;
    REQUIRE(any_error_contains(validate(a), "arp"));
}

TEST_CASE("ARP range rule rejects out-of-range longitude", "[validation]")
{
    auto a    = make_valid_airport();
    a.arp.lon = -200.0;
    REQUIRE(any_error_contains(validate(a), "arp"));
}

TEST_CASE("Runway-presence rule rejects empty runway list", "[validation]")
{
    auto a = make_valid_airport();
    a.runways.clear();
    REQUIRE(any_error_contains(validate(a), "runways"));
}

TEST_CASE("VRP-presence rule rejects empty vrps list", "[validation]")
{
    auto a = make_valid_airport();
    a.vrps.clear();
    a.arrival_routes.clear();
    REQUIRE(any_error_contains(validate(a), "vrps"));
}

TEST_CASE("Unique-VRP-name rule rejects duplicate VRP names", "[validation]")
{
    auto a = make_valid_airport();
    a.vrps.push_back(a.vrps[0]); // same name "E"
    REQUIRE(any_error_contains(validate(a), "duplicate"));
}

TEST_CASE("Runway-heading rule rejects values >= 360", "[validation]")
{
    auto a                    = make_valid_airport();
    a.runways[0].heading_true = 360.0;
    REQUIRE(any_error_contains(validate(a), "heading"));
}

TEST_CASE("Runway-heading rule rejects negative values", "[validation]")
{
    auto a                    = make_valid_airport();
    a.runways[0].heading_true = -1.0;
    REQUIRE(any_error_contains(validate(a), "heading"));
}

TEST_CASE("Altitude-band rule rejects min > max on a VRP", "[validation]")
{
    auto a                       = make_valid_airport();
    a.vrps[0].altitude_ft_min    = 4000;
    a.vrps[0].altitude_ft_max    = 3000;
    REQUIRE(any_error_contains(validate(a), "altitude_ft_min"));
}

TEST_CASE("Altitude-band rule accepts equal min and max", "[validation]")
{
    auto a                    = make_valid_airport();
    a.vrps[0].altitude_ft_min = 3000;
    a.vrps[0].altitude_ft_max = 3000;
    REQUIRE(validate(a).empty());
}

TEST_CASE("Altitude-band rule ignores half-bound waypoints", "[validation]")
{
    auto a                    = make_valid_airport();
    a.vrps[0].altitude_ft_min = 4000;
    a.vrps[0].altitude_ft_max.reset();
    REQUIRE(validate(a).empty());
}

TEST_CASE("arrival_routes rule rejects unknown runway designator", "[validation]")
{
    auto a                 = make_valid_airport();
    a.arrival_routes["99"] = {{"via E", {"E"}}};
    REQUIRE(any_error_contains(validate(a), "unknown runway"));
}

TEST_CASE("arrival_routes rule rejects unknown VRP name", "[validation]")
{
    auto a                 = make_valid_airport();
    a.arrival_routes["06"] = {{"via X", {"DOES-NOT-EXIST"}}};
    REQUIRE(any_error_contains(validate(a), "unknown vrp"));
}

TEST_CASE("arrival_routes rule rejects empty route list", "[validation]")
{
    auto a                 = make_valid_airport();
    a.arrival_routes["06"] = {};
    REQUIRE(any_error_contains(validate(a), "no routes"));
}

TEST_CASE("arrival_routes rule rejects empty VRP sequence in a route", "[validation]")
{
    auto a                 = make_valid_airport();
    a.arrival_routes["06"] = {{"empty", {}}};
    REQUIRE(any_error_contains(validate(a), "no VRPs"));
}

TEST_CASE("arrival_routes rule rejects empty route label", "[validation]")
{
    auto a                 = make_valid_airport();
    a.arrival_routes["06"] = {{"", {"E"}}};
    REQUIRE(any_error_contains(validate(a), "empty label"));
}

TEST_CASE("arrival_routes rule rejects duplicate route labels per runway", "[validation]")
{
    auto a                 = make_valid_airport();
    a.arrival_routes["06"] = {{"via E", {"E"}}, {"via E", {"E"}}};
    REQUIRE(any_error_contains(validate(a), "duplicate route label"));
}

TEST_CASE("circuit_pattern rule rejects non-positive downwind_offset_nm", "[validation]")
{
    auto a                                 = make_valid_airport();
    a.circuit_pattern.downwind_offset_nm   = 0.0;
    REQUIRE(any_error_contains(validate(a), "downwind_offset_nm"));
}

TEST_CASE("circuit_pattern rule rejects non-positive final_distance_nm", "[validation]")
{
    auto a                              = make_valid_airport();
    a.circuit_pattern.final_distance_nm = -0.5;
    REQUIRE(any_error_contains(validate(a), "final_distance_nm"));
}

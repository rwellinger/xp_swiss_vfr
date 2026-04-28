#include "data/json_loader.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <variant>

namespace fs = std::filesystem;
using namespace xpswissvfr::data;

namespace
{
const fs::path FIXTURES = fs::path(XP_SWISS_VFR_TEST_FIXTURES_DIR);
const fs::path AIRPORTS = FIXTURES / "airports";
} // namespace

TEST_CASE("parse_airport returns VfrAirport for valid LSZG fixture", "[json_loader]")
{
    auto result = parse_airport(AIRPORTS / "lszg_valid.json");
    REQUIRE(std::holds_alternative<VfrAirport>(result));
}

TEST_CASE("parse_airport populates every field of the valid LSZG fixture", "[json_loader]")
{
    auto       result  = parse_airport(AIRPORTS / "lszg_valid.json");
    const auto airport = std::get<VfrAirport>(result);

    SECTION("top-level scalars")
    {
        REQUIRE(airport.icao == "LSZG");
        REQUIRE(airport.name == "Grenchen");
        REQUIRE(airport.elevation_ft == 1411);
        REQUIRE(airport.arp.lat == Catch::Approx(47.1816));
        REQUIRE(airport.arp.lon == Catch::Approx(7.4172));
    }

    SECTION("runways")
    {
        REQUIRE(airport.runways.size() == 2);
        REQUIRE(airport.runways[0].designator == "06");
        REQUIRE(airport.runways[0].heading_true == Catch::Approx(60.0));
        REQUIRE(airport.runways[0].length_m == 1200);
        REQUIRE(airport.runways[0].surface == "asphalt");
        REQUIRE(airport.runways[0].circuit_pattern == "left");
        REQUIRE(airport.runways[1].designator == "24");
        REQUIRE(airport.runways[1].circuit_pattern == "right");
    }

    SECTION("vrps")
    {
        REQUIRE(airport.vrps.size() == 3);
        const auto &east = airport.vrps[0];
        REQUIRE(east.name == "E");
        REQUIRE(east.position.lat == Catch::Approx(47.1800));
        REQUIRE(east.position.lon == Catch::Approx(7.5500));
        REQUIRE(east.altitude_ft_min.has_value());
        REQUIRE(*east.altitude_ft_min == 2500);
        REQUIRE(east.altitude_ft_max.has_value());
        REQUIRE(*east.altitude_ft_max == 3500);
        REQUIRE_FALSE(east.altitude_ft.has_value());
        REQUIRE(east.mandatory_report);
    }

    SECTION("arrival_routes")
    {
        REQUIRE(airport.arrival_routes.size() == 2);
        REQUIRE(airport.arrival_routes.at("06") == std::vector<std::string>{"E"});
        REQUIRE(airport.arrival_routes.at("24") == std::vector<std::string>{"W"});
    }

    SECTION("circuit, frequencies, metadata")
    {
        REQUIRE(airport.circuit_pattern.altitude_ft_agl == 1000);
        REQUIRE(airport.circuit_pattern.downwind_offset_nm == Catch::Approx(0.7));
        REQUIRE(airport.circuit_pattern.final_distance_nm == Catch::Approx(1.0));
        REQUIRE(airport.frequencies.at("info") == "121.235");
        REQUIRE(airport.metadata.last_updated == "2026-04-28");
        REQUIRE(airport.metadata.verified_by == "tests");
    }
}

TEST_CASE("parse_airport returns ParseError for malformed JSON", "[json_loader]")
{
    auto result = parse_airport(FIXTURES / "malformed.json");
    REQUIRE(std::holds_alternative<ParseError>(result));
    const auto &err = std::get<ParseError>(result);
    REQUIRE(err.file == "malformed.json");
    REQUIRE_FALSE(err.message.empty());
}

TEST_CASE("parse_airport returns ParseError for missing required field", "[json_loader]")
{
    auto result = parse_airport(AIRPORTS / "missing_icao.json");
    REQUIRE(std::holds_alternative<ParseError>(result));
    const auto &err = std::get<ParseError>(result);
    REQUIRE(err.file == "missing_icao.json");
}

TEST_CASE("parse_airport returns ParseError for nonexistent file", "[json_loader]")
{
    auto result = parse_airport(FIXTURES / "does_not_exist.json");
    REQUIRE(std::holds_alternative<ParseError>(result));
}

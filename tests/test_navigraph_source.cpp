#include "data/navigraph_source.hpp"
#include "data/vfr_airport.hpp"
#include "data/vfr_airport_database.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using xpswissvfr::data::apply_navigraph_overrides;
using xpswissvfr::data::Coordinate;
using xpswissvfr::data::navigraph_is_available;
using xpswissvfr::data::NavigraphVrpOverrides;
using xpswissvfr::data::parse_navigraph_vrps;
using xpswissvfr::data::VfrAirportDatabase;

namespace
{
const fs::path FIXTURES         = fs::path(XP_SWISS_VFR_TEST_FIXTURES_DIR);
const fs::path NAVIGRAPH_FIXTURE = FIXTURES / "navigraph";
} // namespace

TEST_CASE("navigraph_is_available: true when cycle_info.txt exists", "[navigraph]")
{
    auto root = fs::temp_directory_path() / "xp_swiss_vfr_navigraph_pos";
    fs::create_directories(root / "Custom Data");
    std::ofstream(root / "Custom Data" / "cycle_info.txt") << "AIRAC cycle: 2604\n";

    REQUIRE(navigraph_is_available(root));
    fs::remove_all(root);
}

TEST_CASE("navigraph_is_available: false when cycle_info.txt missing", "[navigraph]")
{
    auto root = fs::temp_directory_path() / "xp_swiss_vfr_navigraph_neg";
    fs::create_directories(root / "Custom Data");

    REQUIRE_FALSE(navigraph_is_available(root));
    fs::remove_all(root);
}

TEST_CASE("parse_navigraph_vrps: extracts only VP-prefixed entries for requested ICAOs", "[navigraph]")
{
    auto overrides = parse_navigraph_vrps(NAVIGRAPH_FIXTURE / "earth_fix.dat", {"LSZG"});

    REQUIRE(overrides.size() == 7);
    REQUIRE(overrides.count({"LSZG", "S"}) == 1);
    REQUIRE(overrides.count({"LSZG", "W"}) == 1);
    REQUIRE(overrides.count({"LSZG", "HW"}) == 1);
    REQUIRE(overrides.count({"LSZG", "ABM ALTREU"}) == 1);
    REQUIRE(overrides.count({"LSZG", "E1"}) == 1);
    REQUIRE(overrides.count({"LSZG", "E"}) == 1);
    REQUIRE(overrides.count({"LSZG", "HE"}) == 1);

    SECTION("ABM ALTREU multi-word name preserved")
    {
        const auto &c = overrides.at({"LSZG", "ABM ALTREU"});
        REQUIRE(c.lat == Catch::Approx(47.186111).margin(1e-5));
        REQUIRE(c.lon == Catch::Approx(7.449444).margin(1e-5));
    }

    SECTION("non-VP fixes (ZG100, ZG201) are skipped")
    {
        REQUIRE(overrides.count({"LSZG", "ZG100"}) == 0);
        REQUIRE(overrides.count({"LSZG", "ZG201"}) == 0);
    }

    SECTION("non-LSZG VPs (LSZH N1/W1) are skipped when LSZG-only filter")
    {
        REQUIRE(overrides.count({"LSZH", "N1"}) == 0);
    }
}

TEST_CASE("parse_navigraph_vrps: multi-airport filter", "[navigraph]")
{
    auto overrides = parse_navigraph_vrps(NAVIGRAPH_FIXTURE / "earth_fix.dat", {"LSZG", "LSZH"});
    REQUIRE(overrides.count({"LSZH", "N1"}) == 1);
    REQUIRE(overrides.count({"LSZH", "W1"}) == 1);
}

TEST_CASE("parse_navigraph_vrps: missing file returns empty map", "[navigraph]")
{
    auto overrides = parse_navigraph_vrps(NAVIGRAPH_FIXTURE / "does_not_exist.dat", {"LSZG"});
    REQUIRE(overrides.empty());
}

TEST_CASE("parse_navigraph_vrps: empty ICAO set returns empty map", "[navigraph]")
{
    auto overrides = parse_navigraph_vrps(NAVIGRAPH_FIXTURE / "earth_fix.dat", {});
    REQUIRE(overrides.empty());
}

TEST_CASE("apply_navigraph_overrides: replaces matching VRP coords, preserves altitude/mandatory_report", "[navigraph]")
{
    VfrAirportDatabase db;
    db.load_from_directory(FIXTURES / "airports");

    auto *airport = db.find_mutable("LSZG");
    REQUIRE(airport != nullptr);

    // Inject extra fields on E to verify they are preserved across the override.
    auto &vrp_e             = airport->vrps[0]; // "E" in lszg_valid.json
    REQUIRE(vrp_e.name == "E");
    const auto original_min  = vrp_e.altitude_ft_min;
    const auto original_max  = vrp_e.altitude_ft_max;
    const bool original_mand = vrp_e.mandatory_report;

    NavigraphVrpOverrides overrides;
    overrides[{"LSZG", "E"}] = Coordinate{47.2105, 7.6000};
    overrides[{"LSZX", "E"}] = Coordinate{0.0, 0.0}; // unknown airport, ignored

    auto stats = apply_navigraph_overrides(db, overrides);

    REQUIRE(stats.upgraded == 1);
    REQUIRE(stats.upgraded_log.size() == 1);
    REQUIRE(stats.upgraded_log[0].find("LSZG/E") != std::string::npos);

    REQUIRE(vrp_e.position.lat == Catch::Approx(47.2105));
    REQUIRE(vrp_e.position.lon == Catch::Approx(7.6000));
    REQUIRE(vrp_e.altitude_ft_min == original_min);
    REQUIRE(vrp_e.altitude_ft_max == original_max);
    REQUIRE(vrp_e.mandatory_report == original_mand);
}

TEST_CASE("apply_navigraph_overrides: unknown VRP name on known airport leaves data untouched", "[navigraph]")
{
    VfrAirportDatabase db;
    db.load_from_directory(FIXTURES / "airports");

    NavigraphVrpOverrides overrides;
    overrides[{"LSZG", "DOES_NOT_EXIST"}] = Coordinate{0.0, 0.0};

    auto stats = apply_navigraph_overrides(db, overrides);
    REQUIRE(stats.upgraded == 0);
}

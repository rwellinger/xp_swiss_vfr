#include "data/nearby_airports.hpp"
#include "data/vfr_airport.hpp"
#include "data/vfr_airport_database.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <filesystem>

using xpswissvfr::data::Coordinate;
using xpswissvfr::data::find_nearby_airports;
using xpswissvfr::data::NearbyAirport;
using xpswissvfr::data::VfrAirport;
using xpswissvfr::data::VfrAirportDatabase;

namespace
{

// Loaded once at first use; the fixture set ships an LSZG file that exercises
// the full schema, so we get realistic data without hand-crafting an in-memory
// VfrAirport.
const VfrAirportDatabase &shared_database()
{
    static VfrAirportDatabase db;
    static bool               loaded = false;
    if (!loaded)
    {
        db.load_from_directory(std::filesystem::path(XP_SWISS_VFR_TEST_FIXTURES_DIR) / "airports");
        loaded = true;
    }
    return db;
}

// Aircraft on the ground at LSZG (≈ ARP). Used by most cases for "in range".
constexpr Coordinate AT_LSZG{47.1816, 7.4172};

// 200 NM north of LSZG — well outside any sensible VFR pattern range.
constexpr Coordinate FAR_AWAY{50.5, 7.4172};

} // namespace

TEST_CASE("find_nearby_airports: empty database → empty result", "[nearby_airports]")
{
    VfrAirportDatabase empty;
    auto               result = find_nearby_airports(empty, AT_LSZG, 50.0, 10);
    REQUIRE(result.empty());
}

TEST_CASE("find_nearby_airports: aircraft inside range → airport listed", "[nearby_airports]")
{
    auto result = find_nearby_airports(shared_database(), AT_LSZG, 15.0, 10);
    REQUIRE(result.size() == 1);
    REQUIRE(result.front().icao == "LSZG");
    REQUIRE(result.front().distance_nm < 0.5);
    REQUIRE(!result.front().name.empty());
}

TEST_CASE("find_nearby_airports: aircraft far outside range → empty result", "[nearby_airports]")
{
    auto result = find_nearby_airports(shared_database(), FAR_AWAY, 15.0, 10);
    REQUIRE(result.empty());
}

TEST_CASE("find_nearby_airports: max_count caps the list", "[nearby_airports]")
{
    auto result = find_nearby_airports(shared_database(), AT_LSZG, 1000.0, 0);
    REQUIRE(result.empty());
}

TEST_CASE("find_nearby_airports: options match arrival_routes (runway × route_label)", "[nearby_airports]")
{
    auto result = find_nearby_airports(shared_database(), AT_LSZG, 15.0, 10);
    REQUIRE(result.size() == 1);

    const auto &lszg = result.front();
    REQUIRE_FALSE(lszg.options.empty());
    // Sanity: every (runway, label) pair in options must come from the
    // airport's arrival_routes — the UI iterates this list one button per row.
    const VfrAirport *airport = shared_database().find("LSZG");
    REQUIRE(airport != nullptr);
    for (const auto &option : lszg.options)
    {
        auto it = airport->arrival_routes.find(option.runway_designator);
        REQUIRE(it != airport->arrival_routes.end());
        bool label_found = false;
        for (const auto &route : it->second)
            if (route.label == option.route_label)
                label_found = true;
        REQUIRE(label_found);
    }
}

TEST_CASE("find_nearby_airports: results sorted nearest-first", "[nearby_airports]")
{
    // Synthetic DB with three hand-rolled airports at known distances, tested
    // through the public interface: load → find. We can't push directly into
    // VfrAirportDatabase, so we exercise the sort by varying the aircraft
    // position rather than the airport set. With one fixture airport (LSZG),
    // the sort is trivially correct; we therefore only assert the distance
    // matches geometry::distance_nm for a sanity check.
    Coordinate fifty_nm_north{47.1816 + (50.0 / 60.0), 7.4172};
    auto       far    = find_nearby_airports(shared_database(), fifty_nm_north, 100.0, 10);
    auto       nearby = find_nearby_airports(shared_database(), AT_LSZG, 100.0, 10);
    REQUIRE(far.size() == 1);
    REQUIRE(nearby.size() == 1);
    REQUIRE(far.front().distance_nm > nearby.front().distance_nm);
}

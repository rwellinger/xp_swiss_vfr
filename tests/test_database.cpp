#include "data/vfr_airport_database.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <filesystem>

namespace fs = std::filesystem;
using namespace xpswissvfr::data;

namespace
{
const fs::path AIRPORTS = fs::path(XP_SWISS_VFR_TEST_FIXTURES_DIR) / "airports";
}

TEST_CASE("load_from_directory loads valid LSZG and skips the four broken files", "[database]")
{
    VfrAirportDatabase db;
    auto               result = db.load_from_directory(AIRPORTS);

    REQUIRE(result.loaded == 1);
    REQUIRE(result.failed == 4);
    REQUIRE(result.errors.size() == 4);
    REQUIRE(db.size() == 1);
}

TEST_CASE("find returns the loaded airport by ICAO", "[database]")
{
    VfrAirportDatabase db;
    db.load_from_directory(AIRPORTS);

    const auto *lszg = db.find("LSZG");
    REQUIRE(lszg != nullptr);
    REQUIRE(lszg->name == "Grenchen");

    REQUIRE(db.find("LSZH") == nullptr);
}

TEST_CASE("list_icao_codes returns exactly the loaded airports", "[database]")
{
    VfrAirportDatabase db;
    db.load_from_directory(AIRPORTS);

    const auto codes = db.list_icao_codes();
    REQUIRE(codes.size() == 1);
    REQUIRE(codes[0] == "LSZG");
}

TEST_CASE("load_from_directory on a nonexistent directory reports an error", "[database]")
{
    VfrAirportDatabase db;
    auto               result = db.load_from_directory("/nonexistent/path/to/nowhere");

    REQUIRE(result.loaded == 0);
    REQUIRE_FALSE(result.errors.empty());
    REQUIRE(db.size() == 0);
}

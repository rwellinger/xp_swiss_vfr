#include "data/coordinate.hpp"
#include "geometry/terrain.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <optional>

using xpswissvfr::data::Coordinate;
using xpswissvfr::geometry::clamp_to_terrain;
using xpswissvfr::geometry::default_terrain_source;
using xpswissvfr::geometry::TerrainSource;

namespace
{
class FakeTerrain : public TerrainSource
{
  public:
    explicit FakeTerrain(std::optional<int> elevation_ft) : elevation_(elevation_ft) {}
    std::optional<int> elevation_ft_msl(const Coordinate &) const override { return elevation_; }

  private:
    std::optional<int> elevation_;
};

constexpr Coordinate ANY_POSITION{47.0, 7.5};
} // namespace

TEST_CASE("clamp_to_terrain leaves desired altitude unchanged when terrain is unknown", "[terrain]")
{
    FakeTerrain unknown_terrain{std::nullopt};
    REQUIRE(clamp_to_terrain(unknown_terrain, ANY_POSITION, 3000, 500) == 3000);
}

TEST_CASE("clamp_to_terrain leaves desired altitude unchanged when ample margin to terrain", "[terrain]")
{
    FakeTerrain low_terrain{1000};
    REQUIRE(clamp_to_terrain(low_terrain, ANY_POSITION, 3000, 500) == 3000);
}

TEST_CASE("clamp_to_terrain raises altitude to terrain + margin when below safe floor", "[terrain]")
{
    FakeTerrain high_terrain{2700}; // e.g. Belpberg
    REQUIRE(clamp_to_terrain(high_terrain, ANY_POSITION, 2800, 500) == 3200);
}

TEST_CASE("clamp_to_terrain raises altitude when desired equals terrain", "[terrain]")
{
    FakeTerrain terrain{2000};
    REQUIRE(clamp_to_terrain(terrain, ANY_POSITION, 2000, 500) == 2500);
}

TEST_CASE("clamp_to_terrain returns desired exactly at the safe floor", "[terrain]")
{
    FakeTerrain terrain{2000};
    REQUIRE(clamp_to_terrain(terrain, ANY_POSITION, 2500, 500) == 2500);
}

TEST_CASE("default_terrain_source in test build is a null source", "[terrain]")
{
    // The test target compiles geometry/terrain.cpp with XP_SWISS_VFR_TERRAIN_NO_SDK,
    // which makes default_terrain_source() return a null source — no probe is
    // available, so clamp_to_terrain must be a no-op.
    REQUIRE(clamp_to_terrain(default_terrain_source(), ANY_POSITION, 1234, 500) == 1234);
}

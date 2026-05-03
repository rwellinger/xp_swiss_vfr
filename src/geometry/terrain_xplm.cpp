#include "geometry/terrain.hpp"

#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMScenery.h>

namespace xpswissvfr::geometry
{
namespace
{
constexpr double METERS_TO_FEET = 3.28084;
// X-Plane's local OpenGL coordinate system has its origin near the loaded
// scenery tile. We sample the terrain by casting a ray downward from a point
// high above the requested lat/lon — 5000 m clears every Swiss summit.
constexpr double PROBE_START_ALT_M = 5000.0;

class XplmTerrainSource : public TerrainSource
{
  public:
    XplmTerrainSource() { probe_ = XPLMCreateProbe(xplm_ProbeY); }

    ~XplmTerrainSource() override
    {
        if (probe_ != nullptr)
            XPLMDestroyProbe(probe_);
    }

    XplmTerrainSource(const XplmTerrainSource &)            = delete;
    XplmTerrainSource &operator=(const XplmTerrainSource &) = delete;

    std::optional<int> elevation_ft_msl(const data::Coordinate &position) const override
    {
        if (probe_ == nullptr)
            return std::nullopt;

        double sx = 0.0, sy = 0.0, sz = 0.0;
        XPLMWorldToLocal(position.lat, position.lon, PROBE_START_ALT_M, &sx, &sy, &sz);

        XPLMProbeInfo_t info{};
        info.structSize = sizeof(info);
        const auto result =
            XPLMProbeTerrainXYZ(probe_, static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz), &info);
        if (result != xplm_ProbeHitTerrain)
            return std::nullopt;

        double lat = 0.0, lon = 0.0, alt_m = 0.0;
        XPLMLocalToWorld(info.locationX, info.locationY, info.locationZ, &lat, &lon, &alt_m);
        return static_cast<int>(alt_m * METERS_TO_FEET);
    }

  private:
    XPLMProbeRef probe_ = nullptr;
};

XplmTerrainSource *s_source = nullptr;

class FallbackNullSource : public TerrainSource
{
  public:
    std::optional<int> elevation_ft_msl(const data::Coordinate &) const override { return std::nullopt; }
};

const FallbackNullSource s_fallback;
} // namespace

void terrain_init()
{
    if (s_source == nullptr)
        s_source = new XplmTerrainSource();
}

void terrain_stop()
{
    delete s_source;
    s_source = nullptr;
}

const TerrainSource &default_terrain_source()
{
    if (s_source != nullptr)
        return *s_source;
    return s_fallback;
}
} // namespace xpswissvfr::geometry

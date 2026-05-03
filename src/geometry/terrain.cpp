#include "geometry/terrain.hpp"

#include <cstdio>
#include <string>

#if !defined(XP_SWISS_VFR_TERRAIN_NO_SDK)
#include <XPLM/XPLMUtilities.h>
#endif

namespace xpswissvfr::geometry
{
namespace
{
class NullTerrainSource : public TerrainSource
{
  public:
    std::optional<int> elevation_ft_msl(const data::Coordinate &) const override { return std::nullopt; }
};

const NullTerrainSource s_null_source;

void log_warning(const std::string &message)
{
#if !defined(XP_SWISS_VFR_TERRAIN_NO_SDK)
    XPLMDebugString(message.c_str());
#else
    (void)message;
#endif
}

std::string format_position(const data::Coordinate &position)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "(%.4f, %.4f)", position.lat, position.lon);
    return buf;
}
} // namespace

int clamp_to_terrain(const TerrainSource &source, const data::Coordinate &position, int desired_alt_ft, int margin_ft)
{
    const auto terrain = source.elevation_ft_msl(position);
    if (!terrain)
        return desired_alt_ft;

    const int safe_floor = *terrain + margin_ft;
    if (desired_alt_ft >= safe_floor)
        return desired_alt_ft;

    log_warning("[xp_swiss_vfr] WARNING: terrain clamp at " + format_position(position) + " — requested " +
                std::to_string(desired_alt_ft) + " ft, terrain " + std::to_string(*terrain) + " ft, raised to " +
                std::to_string(safe_floor) + " ft\n");
    return safe_floor;
}

#if defined(XP_SWISS_VFR_TERRAIN_NO_SDK)
// Test build: the SDK probe is not linked, so default_terrain_source() returns
// a null source. terrain_init/stop are no-ops. The plugin build provides its
// own definitions in terrain_xplm.cpp.
const TerrainSource &default_terrain_source() { return s_null_source; }
void                 terrain_init() {}
void                 terrain_stop() {}
#endif
} // namespace xpswissvfr::geometry

#pragma once

#include "data/coordinate.hpp"

#include <optional>

namespace xpswissvfr::geometry
{
// Source of terrain elevations. The plugin uses an X-Plane SDK probe; tests
// inject fakes. nullopt = "I cannot answer for this position", which
// clamp_to_terrain interprets as "leave the desired altitude alone".
class TerrainSource
{
  public:
    virtual ~TerrainSource()                                                            = default;
    virtual std::optional<int> elevation_ft_msl(const data::Coordinate &position) const = 0;
};

// SDK-backed terrain source. Constructed once via terrain_init() and returned
// here. Returns a null source until init() runs (or in the test target where
// the SDK-coupled translation unit is not linked).
const TerrainSource &default_terrain_source();

// Lifecycle hooks. Must be called inside XPluginStart / XPluginStop on the
// main thread; XPLMCreateProbe is not safe from arbitrary contexts.
void terrain_init();
void terrain_stop();

// Returns max(desired_alt_ft, terrain + margin_ft) when the source can answer
// for `position` and the desired altitude is unsafe, otherwise returns
// desired_alt_ft unchanged. Logs a warning to XPLMDebugString when it clamps.
int clamp_to_terrain(const TerrainSource &source, const data::Coordinate &position, int desired_alt_ft, int margin_ft);
} // namespace xpswissvfr::geometry

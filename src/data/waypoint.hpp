#pragma once

#include "coordinate.hpp"

#include <optional>
#include <string>

namespace xpswissvfr::data
{
struct Waypoint
{
    std::string        name;
    Coordinate         position;
    std::optional<int> altitude_ft;
    std::optional<int> altitude_ft_min;
    std::optional<int> altitude_ft_max;
    bool               mandatory_report = false;
};
} // namespace xpswissvfr::data

#pragma once

#include <string>

namespace xpswissvfr::data
{
struct Runway
{
    std::string designator;   // "06", "24L"
    double      heading_true; // degrees
    int         length_m;
    std::string surface;         // "asphalt", "grass", "concrete"
    std::string circuit_pattern; // "left", "right"
};
} // namespace xpswissvfr::data

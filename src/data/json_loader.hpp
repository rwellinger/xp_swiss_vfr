#pragma once

#include "parse_error.hpp"
#include "vfr_airport.hpp"

#include <filesystem>
#include <variant>

namespace xpswissvfr::data
{
std::variant<VfrAirport, ParseError> parse_airport(const std::filesystem::path &file);
}

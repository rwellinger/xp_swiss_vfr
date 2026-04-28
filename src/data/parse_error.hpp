#pragma once

#include <string>

namespace xpswissvfr::data
{
struct ParseError
{
    std::string file;
    std::string message;
};
} // namespace xpswissvfr::data

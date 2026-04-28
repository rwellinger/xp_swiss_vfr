#pragma once

#include "vfr_airport.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace xpswissvfr::data
{
struct LoadResult
{
    int                      loaded = 0;
    int                      failed = 0;
    std::vector<std::string> errors; // human-readable, one per skipped file
};

class VfrAirportDatabase
{
  public:
    LoadResult               load_from_directory(const std::filesystem::path &dir);
    const VfrAirport        *find(const std::string &icao) const;
    VfrAirport              *find_mutable(const std::string &icao);
    std::vector<std::string> list_icao_codes() const;
    std::size_t              size() const { return airports_.size(); }

  private:
    std::map<std::string, VfrAirport> airports_;
};
} // namespace xpswissvfr::data

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
    using const_iterator = std::map<std::string, VfrAirport>::const_iterator;

    LoadResult               load_from_directory(const std::filesystem::path &dir);
    const VfrAirport        *find(const std::string &icao) const;
    VfrAirport              *find_mutable(const std::string &icao);
    std::vector<std::string> list_icao_codes() const;
    std::size_t              size() const { return airports_.size(); }

    // Read-only iteration over all loaded airports. Order is by ICAO (std::map
    // is sorted) — callers that need a different order must sort the result.
    const_iterator begin() const { return airports_.begin(); }
    const_iterator end() const { return airports_.end(); }

  private:
    std::map<std::string, VfrAirport> airports_;
};
} // namespace xpswissvfr::data

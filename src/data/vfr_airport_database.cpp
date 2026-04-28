#include "data/vfr_airport_database.hpp"

#include "data/json_loader.hpp"
#include "data/validation.hpp"

#include <algorithm>
#include <system_error>
#include <variant>

namespace xpswissvfr::data
{
namespace
{

bool is_json_file(const std::filesystem::directory_entry &entry)
{
    return entry.is_regular_file() && entry.path().extension() == ".json";
}

std::string format_validation_errors(const std::string &filename, const std::vector<std::string> &errors)
{
    std::string out = filename + ": validation failed";
    const char *sep = " — ";
    for (const auto &e : errors)
    {
        out += sep;
        out += e;
        sep = "; ";
    }
    return out;
}

} // namespace

LoadResult VfrAirportDatabase::load_from_directory(const std::filesystem::path &dir)
{
    LoadResult result;

    std::error_code ec;
    auto            iter = std::filesystem::directory_iterator(dir, ec);
    if (ec)
    {
        result.errors.push_back("cannot open directory: " + dir.string() + " (" + ec.message() + ")");
        return result;
    }

    std::vector<std::filesystem::path> files;
    for (const auto &entry : iter)
    {
        if (is_json_file(entry))
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto &file : files)
    {
        auto parsed = parse_airport(file);
        if (auto *err = std::get_if<ParseError>(&parsed))
        {
            result.failed++;
            result.errors.push_back(err->file + ": " + err->message);
            continue;
        }

        auto airport           = std::get<VfrAirport>(std::move(parsed));
        auto validation_issues = validate(airport);
        if (!validation_issues.empty())
        {
            result.failed++;
            result.errors.push_back(format_validation_errors(file.filename().string(), validation_issues));
            continue;
        }

        const std::string icao = airport.icao;
        airports_.emplace(icao, std::move(airport));
        result.loaded++;
    }
    return result;
}

const VfrAirport *VfrAirportDatabase::find(const std::string &icao) const
{
    auto it = airports_.find(icao);
    return it == airports_.end() ? nullptr : &it->second;
}

VfrAirport *VfrAirportDatabase::find_mutable(const std::string &icao)
{
    auto it = airports_.find(icao);
    return it == airports_.end() ? nullptr : &it->second;
}

std::vector<std::string> VfrAirportDatabase::list_icao_codes() const
{
    std::vector<std::string> codes;
    codes.reserve(airports_.size());
    for (const auto &[icao, _] : airports_)
        codes.push_back(icao);
    return codes;
}

} // namespace xpswissvfr::data

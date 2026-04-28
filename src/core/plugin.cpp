#include "core/plugin.hpp"

#include "data/navigraph_source.hpp"
#include "version.hpp"

#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace xpswissvfr::core
{
namespace
{

data::VfrAirportDatabase s_database;

// XPLMGetPluginInfo returns an HFS path on macOS (colon-separated, no slashes)
// when running inside X-Plane. Convert to POSIX so std::filesystem can use it.
std::string to_posix_path(std::string raw)
{
#if defined(__APPLE__)
    if (raw.find(':') != std::string::npos && raw.find('/') == std::string::npos)
    {
        auto        first_colon = raw.find(':');
        std::string posix       = raw.substr(first_colon + 1);
        for (char &c : posix)
        {
            if (c == ':')
                c = '/';
        }
        return "/" + posix;
    }
#endif
    return raw;
}

std::filesystem::path resolve_resources_dir()
{
    char raw[2048] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, raw, nullptr, nullptr);
    auto plugin_xpl = std::filesystem::path(to_posix_path(raw));
    // .../<plugin>/<platform>/<name>.xpl  →  .../<plugin>/resources
    return plugin_xpl.parent_path().parent_path() / "resources";
}

std::filesystem::path resolve_xplane_root()
{
    char raw[2048] = {};
    XPLMGetSystemPath(raw);
    return std::filesystem::path(to_posix_path(raw));
}

void log_line(const std::string &line) { XPLMDebugString(line.c_str()); }

std::string join_with_comma(const std::vector<std::string> &items)
{
    std::string out;
    const char *sep = "";
    for (const auto &i : items)
    {
        out += sep;
        out += i;
        sep = ", ";
    }
    return out;
}

void log_load_result(const data::LoadResult &result, const std::vector<std::string> &codes)
{
    for (const auto &err : result.errors)
        log_line("[xp_swiss_vfr] WARNING: skipped " + err + "\n");

    if (result.loaded == 0)
    {
        log_line("[xp_swiss_vfr] Loaded 0 VFR airports — no data files found.\n");
        return;
    }
    const char *noun = result.loaded == 1 ? "airport" : "airports";
    log_line("[xp_swiss_vfr] Loaded " + std::to_string(result.loaded) + " VFR " + noun + ": " + join_with_comma(codes) +
             "\n");
}

// Layer-2: if Navigraph is installed, override Layer-1 VRP coordinates with
// the precise Navigraph values. Skipped silently if not available — the
// shipped Layer-1 data must remain functional for users without a subscription.
void apply_navigraph_overrides_if_available()
{
    auto xplane_root = resolve_xplane_root();
    if (!data::navigraph_is_available(xplane_root))
    {
        log_line("[xp_swiss_vfr] Navigraph data not detected; using shipped VRP coordinates.\n");
        return;
    }

    const auto                  loaded_codes = s_database.list_icao_codes();
    const std::set<std::string> icao_set(loaded_codes.begin(), loaded_codes.end());
    auto overrides = data::parse_navigraph_vrps(xplane_root / "Custom Data" / "earth_fix.dat", icao_set);
    auto stats     = data::apply_navigraph_overrides(s_database, overrides);

    log_line("[xp_swiss_vfr] Navigraph detected; upgraded " + std::to_string(stats.upgraded) +
             " VRP coordinate(s).\n");
    for (const auto &line : stats.upgraded_log)
        log_line("[xp_swiss_vfr]   " + line + "\n");
}

} // namespace

void init()
{
    char banner[128];
    std::snprintf(banner, sizeof(banner), "[xp_swiss_vfr] *** xp_swiss_vfr v%s by thWelly ***\n", XP_SWISS_VFR_VERSION);
    XPLMDebugString(banner);

    auto airports_dir = resolve_resources_dir() / "airports";
    auto result       = s_database.load_from_directory(airports_dir);
    log_load_result(result, s_database.list_icao_codes());

    apply_navigraph_overrides_if_available();
}

void stop() { XPLMDebugString("[xp_swiss_vfr] Plugin unloaded.\n"); }

const data::VfrAirportDatabase &airport_database() { return s_database; }

} // namespace xpswissvfr::core

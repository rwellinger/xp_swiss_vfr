#include "persistence/settings.hpp"

#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>

#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace xpswissvfr::persistence
{
namespace
{

using json = nlohmann::json;

json        s_cfg;
std::string s_data_dir;

json default_config()
{
    return {
        {"window_x", -1.0},
        {"window_y", -1.0},
        {"window_w", -1.0},
        {"window_h", -1.0},
    };
}

// XPLMGetPluginInfo returns an HFS path on macOS (colon-separated) when called
// inside X-Plane. Mirrors the helper in core/plugin.cpp — kept local here so
// settings stays an isolated module.
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

std::filesystem::path resolve_data_dir()
{
    char raw[2048] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, raw, nullptr, nullptr);
    auto plugin_xpl = std::filesystem::path(to_posix_path(raw));
    // .../<plugin>/<platform>/<name>.xpl  →  .../<plugin>/data
    return plugin_xpl.parent_path().parent_path() / "data";
}

void merge_missing_defaults()
{
    json defaults = default_config();
    for (auto &[key, value] : defaults.items())
    {
        if (!s_cfg.contains(key))
            s_cfg[key] = value;
    }
}

} // namespace

void init()
{
    auto            dir = resolve_data_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    s_data_dir = dir.string();

    const auto    json_path = dir / "settings.json";
    std::ifstream in(json_path);
    if (in.good())
    {
        try
        {
            in >> s_cfg;
            merge_missing_defaults();
        }
        catch (...)
        {
            XPLMDebugString("[xp_swiss_vfr] WARNING: failed to parse settings.json, using defaults.\n");
            s_cfg = default_config();
        }
    }
    else
    {
        s_cfg = default_config();
        save();
    }
}

void stop() {}

void save()
{
    if (s_data_dir.empty())
        return;

    const std::string json_path = s_data_dir + "/settings.json";
    std::ofstream     out(json_path);
    if (out.good())
        out << s_cfg.dump(2) << '\n';
    else
        XPLMDebugString("[xp_swiss_vfr] ERROR: failed to write settings.json.\n");
}

float window_x() { return s_cfg.value("window_x", -1.0F); }
float window_y() { return s_cfg.value("window_y", -1.0F); }
float window_w() { return s_cfg.value("window_w", -1.0F); }
float window_h() { return s_cfg.value("window_h", -1.0F); }

void set_window_geometry(float x, float y, float w, float h)
{
    s_cfg["window_x"] = x;
    s_cfg["window_y"] = y;
    s_cfg["window_w"] = w;
    s_cfg["window_h"] = h;
}

} // namespace xpswissvfr::persistence

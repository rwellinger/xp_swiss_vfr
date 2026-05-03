#include "core/plugin.hpp"
#include "geometry/terrain.hpp"
#include "persistence/settings.hpp"
#include "procedures/procedure_runner.hpp"
#include "ui/procedure_selection_window.hpp"
#include "version.hpp"

#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>

#include <cstdio>
#include <cstring>

PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
    // Required for X-Plane installs on external volumes: without it the SDK returns
    // HFS paths that lose the /Volumes/<name>/ mount prefix, causing all plugin
    // file I/O (airport JSONs, settings.json, Navigraph lookup) to hit the
    // read-only system root. See xp_pilot/src/main.cpp for the original incident.
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    std::snprintf(outName, 255, "xp_swiss_vfr v%s", XP_SWISS_VFR_VERSION);
    std::strncpy(outSig, "thWelly.xp_swiss_vfr", 255);
    std::snprintf(outDesc, 255, "Swiss VFR approach procedures v%s", XP_SWISS_VFR_VERSION);

    xpswissvfr::core::init();
    xpswissvfr::geometry::terrain_init();
    xpswissvfr::persistence::init();
    xpswissvfr::ui::init();
    xpswissvfr::procedures::init();
    return 1;
}

PLUGIN_API void XPluginStop()
{
    xpswissvfr::procedures::stop();
    xpswissvfr::ui::stop();
    xpswissvfr::persistence::stop();
    xpswissvfr::geometry::terrain_stop();
    xpswissvfr::core::stop();
}

PLUGIN_API int XPluginEnable() { return 1; }

PLUGIN_API void XPluginDisable() {}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}

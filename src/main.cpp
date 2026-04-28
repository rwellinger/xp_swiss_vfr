#include "core/plugin.hpp"
#include "procedures/procedure_runner.hpp"
#include "ui/procedure_selection_window.hpp"
#include "version.hpp"

#include <XPLM/XPLMPlugin.h>

#include <cstdio>
#include <cstring>

PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
    std::snprintf(outName, 255, "xp_swiss_vfr v%s", XP_SWISS_VFR_VERSION);
    std::strncpy(outSig, "thWelly.xp_swiss_vfr", 255);
    std::snprintf(outDesc, 255, "Swiss VFR approach procedures v%s", XP_SWISS_VFR_VERSION);

    xpswissvfr::core::init();
    xpswissvfr::ui::init();
    xpswissvfr::procedures::init();
    return 1;
}

PLUGIN_API void XPluginStop()
{
    xpswissvfr::procedures::stop();
    xpswissvfr::ui::stop();
    xpswissvfr::core::stop();
}

PLUGIN_API int XPluginEnable() { return 1; }

PLUGIN_API void XPluginDisable() {}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}

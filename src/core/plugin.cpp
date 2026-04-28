#include "core/plugin.hpp"
#include "version.hpp"

#include <XPLM/XPLMUtilities.h>

#include <cstdio>

namespace xpswissvfr::core
{

void init()
{
    char banner[128];
    std::snprintf(banner, sizeof(banner), "[xp_swiss_vfr] *** xp_swiss_vfr v%s by thWelly ***\n", XP_SWISS_VFR_VERSION);
    XPLMDebugString(banner);
}

void stop() { XPLMDebugString("[xp_swiss_vfr] Plugin unloaded.\n"); }

} // namespace xpswissvfr::core

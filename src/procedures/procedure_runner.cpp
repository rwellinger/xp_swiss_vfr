#include "procedures/procedure_runner.hpp"

#include "core/plugin.hpp"
#include "procedures/build_procedure.hpp"

#include <XPLM/XPLMMenus.h>
#include <XPLM/XPLMNavigation.h>
#include <XPLM/XPLMUtilities.h>

#include <cstdint>
#include <cstdio>

namespace xpswissvfr::procedures
{
namespace
{

// We target the pilot's primary flight plan via the XPLM410 multi-FPL API.
// The legacy single-FPL routines (XPLMSetFMSEntryLatLon etc.) write to a
// different slot than the one X1000-style avionics expose to the pilot, so
// pre-existing user FPLs were invisible to us and our writes were invisible
// to the avionic. xplm_Fpl_Pilot_Primary is the slot the pilot edits.
constexpr XPLMNavFlightPlan FPL = xplm_Fpl_Pilot_Primary;

enum MenuItem : std::intptr_t
{
    ITEM_ACTIVATE_LSZG_06 = 1,
    ITEM_CLEAR            = 2,
};

XPLMMenuID s_menu     = nullptr;
int        s_root_idx = -1;
bool       s_active   = false;

// Range of FMS indices we own. [s_inject_first .. s_inject_first + s_inject_count - 1]
// Set on activate, used on clear so we only remove our own entries and leave any
// pre-existing pilot flight plan intact.
int s_inject_first = -1;
int s_inject_count = 0;

void log_line(const char *line) { XPLMDebugString(line); }

void remove_injected_entries(const char *context)
{
    if (s_inject_count <= 0)
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS clear (%s) skipped; no procedure entries tracked.\n",
                      context);
        log_line(buf);
        return;
    }

    int last = s_inject_first + s_inject_count - 1;

    char buf[200];
    std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS clear (%s) start; range=[%d..%d] total=%d\n", context,
                  s_inject_first, last, XPLMCountFMSFlightPlanEntries(FPL));
    log_line(buf);

    for (int idx = last; idx >= s_inject_first; --idx)
    {
        XPLMClearFMSFlightPlanEntry(FPL, idx);
    }

    int after = XPLMCountFMSFlightPlanEntries(FPL);
    std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS clear (%s) done;  count_after=%d\n", context, after);
    log_line(buf);

    s_inject_first = -1;
    s_inject_count = 0;
}

void write_entry(int idx, const ProcedureWaypoint &w)
{
    auto      id_len = static_cast<unsigned int>(w.display_name.size());
    const int alt    = w.altitude_ft.value_or(0);

    XPLMSetFMSFlightPlanEntryLatLonWithId(FPL, idx, static_cast<float>(w.position.lat),
                                          static_cast<float>(w.position.lon), alt, w.display_name.c_str(), id_len);

    char buf[256];
    std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS inject idx=%d name=\"%s\" lat=%.6f lon=%.6f alt=%d id_len=%u\n",
                  idx, w.display_name.c_str(), w.position.lat, w.position.lon, alt, id_len);
    log_line(buf);
}

void log_readback(int idx)
{
    XPLMNavType type         = 0;
    XPLMNavRef  ref          = XPLM_NAV_NOT_FOUND;
    int         alt_back     = -1;
    float       lat_back     = 0.0f;
    float       lon_back     = 0.0f;
    char        id_back[256] = {};

    XPLMGetFMSFlightPlanEntryInfo(FPL, idx, &type, id_back, &ref, &alt_back, &lat_back, &lon_back);

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[xp_swiss_vfr] FMS readback idx=%d type=%d id=\"%s\" lat=%.6f lon=%.6f alt=%d ref=%d\n", idx,
                  static_cast<int>(type), id_back, lat_back, lon_back, alt_back, static_cast<int>(ref));
    log_line(buf);
}

void activate_lszg_06_test()
{
    const data::VfrAirport *lszg = core::airport_database().find("LSZG");
    if (lszg == nullptr)
    {
        log_line("[xp_swiss_vfr] activate LSZG 06: airport not loaded.\n");
        return;
    }

    auto procedure = build_procedure(*lszg, "06");
    if (!procedure.has_value())
    {
        log_line("[xp_swiss_vfr] activate LSZG 06: build_procedure returned nullopt.\n");
        return;
    }

    activate(*procedure);
}

void menu_handler(void * /*menu_ref*/, void *item_ref)
{
    switch (reinterpret_cast<std::intptr_t>(item_ref))
    {
    case ITEM_ACTIVATE_LSZG_06:
        activate_lszg_06_test();
        break;
    case ITEM_CLEAR:
        clear_active_procedure();
        break;
    default:
        break;
    }
}

} // namespace

void init()
{
    s_root_idx = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "xp_swiss_vfr", nullptr, 0);
    s_menu     = XPLMCreateMenu("xp_swiss_vfr", XPLMFindPluginsMenu(), s_root_idx, &menu_handler, nullptr);

    XPLMAppendMenuItem(s_menu, "Activate LSZG RWY 06 (test)", reinterpret_cast<void *>(ITEM_ACTIVATE_LSZG_06), 0);
    XPLMAppendMenuItem(s_menu, "Clear LSZG procedure", reinterpret_cast<void *>(ITEM_CLEAR), 0);

    log_line("[xp_swiss_vfr] procedures::init — menu items registered.\n");
}

void stop()
{
    if (s_menu != nullptr)
    {
        XPLMDestroyMenu(s_menu);
        s_menu = nullptr;
    }
    if (s_root_idx >= 0)
    {
        XPLMRemoveMenuItem(XPLMFindPluginsMenu(), s_root_idx);
        s_root_idx = -1;
    }
    s_active = false;

    log_line("[xp_swiss_vfr] procedures::stop — menu torn down.\n");
}

void activate(const Procedure &procedure)
{
    if (s_active)
    {
        remove_injected_entries("re-activate");
    }

    int  existing = XPLMCountFMSFlightPlanEntries(FPL);
    int  start    = existing;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[xp_swiss_vfr] FMS inject: %s RWY %s (%zu waypoints), appending at idx=%d "
                  "(existing FPL has %d entries)\n",
                  procedure.airport_icao.c_str(), procedure.runway_designator.c_str(), procedure.waypoints.size(),
                  start, existing);
    log_line(buf);

    for (std::size_t i = 0; i < procedure.waypoints.size(); ++i)
    {
        const auto &w   = procedure.waypoints[i];
        int         idx = start + static_cast<int>(i);

        write_entry(idx, w);

        int count_after = XPLMCountFMSFlightPlanEntries(FPL);
        std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS inject idx=%d -> count_after=%d\n", idx, count_after);
        log_line(buf);

        log_readback(idx);
    }

    s_inject_first = start;
    s_inject_count = static_cast<int>(procedure.waypoints.size());
    s_active       = true;

    int total = XPLMCountFMSFlightPlanEntries(FPL);
    std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS inject done; range=[%d..%d] total_entries=%d\n", s_inject_first,
                  s_inject_first + s_inject_count - 1, total);
    log_line(buf);
}

void clear_active_procedure()
{
    log_line("[xp_swiss_vfr] FMS clear: requested via menu.\n");
    remove_injected_entries("user-clear");
    s_active = false;
}

bool is_active() { return s_active; }

} // namespace xpswissvfr::procedures

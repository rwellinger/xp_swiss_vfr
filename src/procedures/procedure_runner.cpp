#include "procedures/procedure_runner.hpp"

#include "core/plugin.hpp"
#include "procedures/build_procedure.hpp"
#include "procedures/procedure_state.hpp"
#include "ui/procedure_selection_window.hpp"

#include <XPLM/XPLMMenus.h>
#include <XPLM/XPLMNavigation.h>
#include <XPLM/XPLMPlugin.h>
#include <XPLM/XPLMUtilities.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

// FPL slots we snapshot/restore around an activate. The Primary slot is the
// main plan (origin → enroute → destination, including any SID/STAR waypoints
// the avionics inserted). The Approach slot holds the destination's approach
// procedure as chosen via the X1000 PROC menu — completely independent of
// Navigraph (it is filled by X-Plane from default CIFP, or by Navigraph
// overrides; either way the XPLM API exposes the same slot).
constexpr XPLMNavFlightPlan SNAPSHOT_SLOTS[] = {
    xplm_Fpl_Pilot_Primary,
    xplm_Fpl_Pilot_Approach,
};

const char *slot_name(XPLMNavFlightPlan slot)
{
    switch (slot)
    {
    case xplm_Fpl_Pilot_Primary:
        return "primary";
    case xplm_Fpl_Pilot_Approach:
        return "approach";
    default:
        return "?";
    }
}

enum MenuItem : std::intptr_t
{
    ITEM_TOGGLE_WINDOW    = 1,
    ITEM_CLEAR            = 2,
    ITEM_ACTIVATE_LSZG_06 = 3, // power-user fallback (no menu entry, command only)
};

XPLMMenuID s_menu     = nullptr;
int        s_root_idx = -1;
bool       s_active   = false;

// Identifies the currently active procedure. Populated on activate(), cleared
// on clear_active_procedure(). Used by the UI to display "Active: <ICAO> RWY
// <runway> / <route_label>".
std::string s_active_icao;
std::string s_active_runway;
std::string s_active_route_label;

ProcedureStateMachine s_state_machine;

// Command refs — registered in init(), unregistered in stop(). The handler
// dispatches based on the ref pointer.
XPLMCommandRef s_cmd_activate_lszg_06 = nullptr;
XPLMCommandRef s_cmd_clear            = nullptr;
XPLMCommandRef s_cmd_toggle_window    = nullptr;

// Range of FMS indices we own. [s_inject_first .. s_inject_first + s_inject_count - 1]
// Set on activate, used on clear so we only remove our own entries and leave any
// pre-existing pilot flight plan intact.
int s_inject_first = -1;
int s_inject_count = 0;

// Snapshot of every relevant FPL slot, captured immediately before the first
// activate. On clear we wipe each slot and re-emit its entries so the pilot's
// plan returns to exactly the state it was in before the procedure was
// injected — including any departure airport, SID/STAR waypoints, the
// destination airport (which we may have absorbed during inject), and the
// approach procedure stored in the X1000's separate Approach slot.
struct FmsEntry
{
    XPLMNavType type = 0;
    std::string id;
    XPLMNavRef  ref = XPLM_NAV_NOT_FOUND;
    int         alt = 0;
    float       lat = 0.0F;
    float       lon = 0.0F;
};
struct SlotSnapshot
{
    XPLMNavFlightPlan     slot = xplm_Fpl_Pilot_Primary;
    std::vector<FmsEntry> entries;
};
std::vector<SlotSnapshot> s_pre_activate_snapshots;

void log_line(const char *line) { XPLMDebugString(line); }

// Read every entry the X-Plane FMS API claims to have and write it to the
// plugin log. Used as a diagnostic before/after each activate + clear so any
// drift between what we expect and what the simulator has actually got is
// visible immediately. The API can be unreliable directly after a fresh
// session start (count==0 even when a Departure is visible in the avionics);
// dumping the whole slot lets us catch that and reason about the resulting
// inject offsets.
void dump_one_slot(const char *context, XPLMNavFlightPlan slot)
{
    int count = XPLMCountFMSFlightPlanEntries(slot);
    if (count <= 0)
        return; // skip empty slots to keep the log readable

    char buf[320];
    std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FPL dump (%s) slot=%s: count=%d\n", context, slot_name(slot),
                  count);
    log_line(buf);

    for (int i = 0; i < count; ++i)
    {
        XPLMNavType type   = 0;
        XPLMNavRef  ref    = XPLM_NAV_NOT_FOUND;
        int         alt    = 0;
        float       lat    = 0.0F;
        float       lon    = 0.0F;
        char        id[64] = {};
        XPLMGetFMSFlightPlanEntryInfo(slot, i, &type, id, &ref, &alt, &lat, &lon);

        std::snprintf(
            buf, sizeof(buf),
            "[xp_swiss_vfr] FPL dump (%s) slot=%s: idx=%d type=%d id=\"%s\" lat=%.6f lon=%.6f alt=%d ref=%d\n", context,
            slot_name(slot), i, static_cast<int>(type), id, lat, lon, alt, static_cast<int>(ref));
        log_line(buf);
    }
}

void dump_existing_fpl(const char *context)
{
    for (auto slot : SNAPSHOT_SLOTS)
        dump_one_slot(context, slot);
}

SlotSnapshot capture_one_slot(XPLMNavFlightPlan slot)
{
    SlotSnapshot snap;
    snap.slot       = slot;
    const int count = XPLMCountFMSFlightPlanEntries(slot);
    snap.entries.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        FmsEntry e;
        char     id[64] = {};
        XPLMGetFMSFlightPlanEntryInfo(slot, i, &e.type, id, &e.ref, &e.alt, &e.lat, &e.lon);
        e.id = id;
        snap.entries.push_back(std::move(e));
    }
    return snap;
}

void capture_snapshot()
{
    s_pre_activate_snapshots.clear();
    s_pre_activate_snapshots.reserve(sizeof(SNAPSHOT_SLOTS) / sizeof(SNAPSHOT_SLOTS[0]));

    char buf[200];
    for (auto slot : SNAPSHOT_SLOTS)
    {
        SlotSnapshot snap = capture_one_slot(slot);
        std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FPL snapshot captured: slot=%s, %zu entries\n", slot_name(slot),
                      snap.entries.size());
        log_line(buf);
        s_pre_activate_snapshots.push_back(std::move(snap));
    }
}

void restore_one_slot(const SlotSnapshot &snap)
{
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "[xp_swiss_vfr] FPL snapshot restore: slot=%s, clearing and re-emitting %zu entries\n",
                  slot_name(snap.slot), snap.entries.size());
    log_line(buf);

    const int count_before = XPLMCountFMSFlightPlanEntries(snap.slot);
    for (int idx = count_before - 1; idx >= 0; --idx)
        XPLMClearFMSFlightPlanEntry(snap.slot, idx);

    for (std::size_t i = 0; i < snap.entries.size(); ++i)
    {
        const auto &e   = snap.entries[i];
        const int   idx = static_cast<int>(i);

        // LatLon-only entries (type 2048) cannot be restored via NavRef
        // because their ref is -1; lat/lon-with-id is the only path. Same
        // fallback applies if any other type ended up with an unresolved ref.
        if (e.type == xplm_Nav_LatLon || e.ref == XPLM_NAV_NOT_FOUND)
        {
            XPLMSetFMSFlightPlanEntryLatLonWithId(snap.slot, idx, e.lat, e.lon, e.alt, e.id.c_str(),
                                                  static_cast<unsigned int>(e.id.size()));
        }
        else
        {
            XPLMSetFMSFlightPlanEntryInfo(snap.slot, idx, e.ref, e.alt);
        }
    }
}

// Wipe each captured slot and re-emit its entries, returning every snapshotted
// FPL to its pre-activate state. Empty snapshots are still replayed so a slot
// that was empty before activate ends up empty afterward (e.g. user added a
// PROC during the activated session — clear takes that out as well).
void restore_snapshot()
{
    for (const auto &snap : s_pre_activate_snapshots)
        restore_one_slot(snap);
    s_pre_activate_snapshots.clear();
}

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

// Write an airport-type entry so the X1000 recognises the entry as a departure
// (or destination) airport rather than a generic lat/lon point. The altitude
// passed in is the airport's published elevation — the X1000 uses it as a
// VNAV target so the descent profile from FAF to threshold makes sense.
//
// Returns true when the airport navref was found and written; false on lookup
// failure (caller can then proceed without the airport row).
bool write_airport_entry(int idx, const std::string &icao, int elevation_ft)
{
    XPLMNavRef ref = XPLMFindNavAid(nullptr, icao.c_str(), nullptr, nullptr, nullptr, xplm_Nav_Airport);

    char buf[256];
    if (ref == XPLM_NAV_NOT_FOUND)
    {
        std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS inject idx=%d airport=%s ref=NOT_FOUND; skipped.\n", idx,
                      icao.c_str());
        log_line(buf);
        return false;
    }

    XPLMSetFMSFlightPlanEntryInfo(FPL, idx, ref, elevation_ft);
    std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS inject idx=%d airport=%s elev=%d ref=%d\n", idx, icao.c_str(),
                  elevation_ft, static_cast<int>(ref));
    log_line(buf);
    return true;
}

// True when the existing FPL's last entry is an airport with the given ICAO.
// Used to decide whether we need to prepend the procedure airport: if the
// pilot already has e.g. `LSZB → … → LSZG` loaded and activates our LSZG
// procedure, LSZG is already present as destination — appending the pattern
// after it gives `LSZB → … → LSZG → E → E1 → … → RWY06`, which is exactly
// what the pilot wants. Without this check we would duplicate LSZG.
bool last_entry_is_airport_with_icao(int count, const std::string &icao)
{
    if (count <= 0)
        return false;

    XPLMNavType type        = 0;
    XPLMNavRef  ref         = XPLM_NAV_NOT_FOUND;
    int         alt         = 0;
    float       lat         = 0.0F;
    float       lon         = 0.0F;
    char        id_back[64] = {};

    XPLMGetFMSFlightPlanEntryInfo(FPL, count - 1, &type, id_back, &ref, &alt, &lat, &lon);
    return type == xplm_Nav_Airport && icao == id_back;
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
    case ITEM_TOGGLE_WINDOW:
        ui::toggle();
        break;
    case ITEM_CLEAR:
        clear_active_procedure();
        break;
    default:
        break;
    }
}

// X-Plane command handler: invoked when the bound command is fired (e.g. by a
// keyboard shortcut or joystick button). We act on `xplm_CommandBegin` only,
// returning 1 to mark the command as fully handled (no further plugins or
// X-Plane internals run for this command).
int command_handler(XPLMCommandRef cmd, XPLMCommandPhase phase, void * /*ref*/)
{
    if (phase != xplm_CommandBegin)
        return 1;

    if (cmd == s_cmd_activate_lszg_06)
        activate_lszg_06_test();
    else if (cmd == s_cmd_clear)
        clear_active_procedure();
    else if (cmd == s_cmd_toggle_window)
        ui::toggle();
    return 1;
}

} // namespace

void init()
{
    s_root_idx = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "Swiss VFR", nullptr, 0);
    s_menu     = XPLMCreateMenu("Swiss VFR", XPLMFindPluginsMenu(), s_root_idx, &menu_handler, nullptr);

    XPLMAppendMenuItem(s_menu, "Show pattern selector", reinterpret_cast<void *>(ITEM_TOGGLE_WINDOW), 0);
    XPLMAppendMenuItem(s_menu, "Clear active pattern", reinterpret_cast<void *>(ITEM_CLEAR), 0);

    // Register X-Plane commands. Pilots can bind these to keyboard or joystick
    // via Settings → Keyboard / Joystick → search "xp_swiss_vfr". The legacy
    // LSZG-06 activate command stays around as a power-user one-press fallback;
    // it has no menu entry anymore.
    s_cmd_activate_lszg_06 =
        XPLMCreateCommand("xpswissvfr/activate/lszg/06", "Activate LSZG RWY 06 VFR procedure (test)");
    s_cmd_clear         = XPLMCreateCommand("xpswissvfr/clear", "Clear active VFR procedure");
    s_cmd_toggle_window = XPLMCreateCommand("xpswissvfr/window/toggle", "Toggle the VFR procedure-selector window");
    XPLMRegisterCommandHandler(s_cmd_activate_lszg_06, &command_handler, /*inBefore=*/1, nullptr);
    XPLMRegisterCommandHandler(s_cmd_clear, &command_handler, /*inBefore=*/1, nullptr);
    XPLMRegisterCommandHandler(s_cmd_toggle_window, &command_handler, /*inBefore=*/1, nullptr);

    log_line("[xp_swiss_vfr] procedures::init — menu + commands registered.\n");
}

void stop()
{
    if (s_cmd_activate_lszg_06 != nullptr)
    {
        XPLMUnregisterCommandHandler(s_cmd_activate_lszg_06, &command_handler, /*inBefore=*/1, nullptr);
        s_cmd_activate_lszg_06 = nullptr;
    }
    if (s_cmd_clear != nullptr)
    {
        XPLMUnregisterCommandHandler(s_cmd_clear, &command_handler, /*inBefore=*/1, nullptr);
        s_cmd_clear = nullptr;
    }
    if (s_cmd_toggle_window != nullptr)
    {
        XPLMUnregisterCommandHandler(s_cmd_toggle_window, &command_handler, /*inBefore=*/1, nullptr);
        s_cmd_toggle_window = nullptr;
    }

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
    s_state_machine.on_clear();

    log_line("[xp_swiss_vfr] procedures::stop — menu + commands torn down.\n");
}

void activate(const Procedure &procedure)
{
    dump_existing_fpl("before-activate");

    if (s_active)
    {
        // Re-activate (e.g. user picked a different runway). Tear down our
        // previous range but keep the snapshot — a later clear should still
        // return all the way back to the state from BEFORE the very first
        // activate, not just before the most recent re-activate.
        remove_injected_entries("re-activate");
    }
    else
    {
        capture_snapshot();
    }

    int  existing = XPLMCountFMSFlightPlanEntries(FPL);
    char buf[256];

    // Strategy: always append the pattern waypoints to the end of the existing
    // FPL, then append the destination airport (LSZG) one slot further. The
    // X1000 then sees the pattern leading INTO the destination airport, not
    // ending on a raw runway-threshold waypoint.
    //
    // Edge case: if the FPL already ends with our destination airport (pilot
    // had LSZG pre-set as destination), absorb that entry — we'll re-emit it
    // at the very end so there is no "X → LSZG → E → … → RWY06 → LSZG"
    // duplicate. The clear range is widened accordingly so a later clear
    // restores the FPL to its pre-activation state.
    const bool absorb_existing_destination = last_entry_is_airport_with_icao(existing, procedure.airport_icao);
    int        waypoint_start              = existing;
    int        track_first                 = existing;
    if (absorb_existing_destination)
    {
        XPLMClearFMSFlightPlanEntry(FPL, existing - 1);
        waypoint_start = existing - 1;
        track_first    = existing - 1;
    }

    std::snprintf(buf, sizeof(buf),
                  "[xp_swiss_vfr] FMS inject: %s RWY %s (%zu waypoints), waypoints@idx=%d, existing FPL had %d "
                  "entries, absorbed-existing-destination=%s\n",
                  procedure.airport_icao.c_str(), procedure.runway_designator.c_str(), procedure.waypoints.size(),
                  waypoint_start, existing, absorb_existing_destination ? "yes" : "no");
    log_line(buf);

    for (std::size_t i = 0; i < procedure.waypoints.size(); ++i)
    {
        const auto &w   = procedure.waypoints[i];
        int         idx = waypoint_start + static_cast<int>(i);

        write_entry(idx, w);

        int count_after = XPLMCountFMSFlightPlanEntries(FPL);
        std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS inject idx=%d -> count_after=%d\n", idx, count_after);
        log_line(buf);

        log_readback(idx);
    }

    // Append destination airport at the very end so the procedure leads into
    // a proper airport entry. If the airport navref lookup fails (very rare),
    // we just skip — the runway threshold acts as a usable de-facto destination.
    const int  airport_idx   = waypoint_start + static_cast<int>(procedure.waypoints.size());
    const bool wrote_airport = write_airport_entry(airport_idx, procedure.airport_icao, procedure.airport_elevation_ft);

    s_inject_first       = track_first;
    s_inject_count       = static_cast<int>(procedure.waypoints.size()) + (wrote_airport ? 1 : 0);
    s_active             = true;
    s_active_icao        = procedure.airport_icao;
    s_active_runway      = procedure.runway_designator;
    s_active_route_label = procedure.route_label;
    s_state_machine.on_activate();

    int total = XPLMCountFMSFlightPlanEntries(FPL);
    std::snprintf(buf, sizeof(buf), "[xp_swiss_vfr] FMS inject done; range=[%d..%d] total_entries=%d state=%s\n",
                  s_inject_first, s_inject_first + s_inject_count - 1, total, state_name(s_state_machine.state()));
    log_line(buf);

    dump_existing_fpl("after-activate");
}

void clear_active_procedure()
{
    log_line("[xp_swiss_vfr] FMS clear: requested.\n");
    dump_existing_fpl("before-clear");

    if (s_active && !s_pre_activate_snapshots.empty())
    {
        // Full restore: replay every captured FPL slot exactly as it was
        // before the very first activate. This brings back the pilot's
        // departure airport, any SID/STAR waypoints, the destination airport
        // we absorbed during inject, AND any approach procedure (PROC) the
        // pilot had selected — none of which a range-only clear could recover.
        restore_snapshot();
    }
    else
    {
        // No snapshot available (plugin loaded mid-flight, or clear called
        // without an active procedure). Fall back to range-only removal.
        remove_injected_entries("user-clear");
    }

    s_inject_first = -1;
    s_inject_count = 0;
    s_active       = false;
    s_active_icao.clear();
    s_active_runway.clear();
    s_active_route_label.clear();
    s_state_machine.on_clear();

    dump_existing_fpl("after-clear");
}

bool  is_active() { return s_active; }
State current_state() { return s_state_machine.state(); }

std::optional<ActiveProcedureInfo> active_procedure_info()
{
    if (!s_active)
        return std::nullopt;
    return ActiveProcedureInfo{s_active_icao, s_active_runway, s_active_route_label};
}

} // namespace xpswissvfr::procedures

#pragma once

#include "procedures/procedure.hpp"
#include "procedures/procedure_state.hpp"

#include <optional>
#include <string>

namespace xpswissvfr::procedures
{
void init();
void stop();

// Inject the procedure's waypoints at the end of the pilot's primary flight
// plan. Re-activation clears any previously tracked range first.
void activate(const Procedure &procedure);
void clear_active_procedure();
bool is_active();

// Current lifecycle state of the active procedure (or IDLE if none).
State current_state();

// Identifies the currently active procedure for UI display. Empty when no
// procedure is active. The ICAO + runway pair is enough for the selector
// window's status row — the UI never needs the waypoint list.
struct ActiveProcedureInfo
{
    std::string airport_icao;
    std::string runway_designator;
    std::string route_label; // empty for legacy single-route airports
};
std::optional<ActiveProcedureInfo> active_procedure_info();
} // namespace xpswissvfr::procedures

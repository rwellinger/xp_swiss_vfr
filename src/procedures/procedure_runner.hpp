#pragma once

#include "procedures/procedure.hpp"

namespace xpswissvfr::procedures
{
void init();
void stop();

// Inject the procedure's waypoints at the end of the pilot's primary flight
// plan. Re-activation clears any previously tracked range first.
void activate(const Procedure &procedure);
void clear_active_procedure();
bool is_active();
} // namespace xpswissvfr::procedures

#pragma once

#include "data/vfr_airport_database.hpp"

namespace xpswissvfr::core
{
void init();
void stop();

const data::VfrAirportDatabase &airport_database();
} // namespace xpswissvfr::core

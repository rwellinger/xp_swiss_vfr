#pragma once

namespace xpswissvfr::persistence
{

// Resolves the plugin's data directory, loads settings.json into memory and
// merges any missing keys against the defaults. Safe to call multiple times.
void init();
void stop();

// Persist the in-memory cfg to disk. Callers that mutate settings frequently
// (e.g. window drag/resize) should debounce instead of calling save() every
// frame.
void save();

// Window geometry — sentinel value -1 means "not set, use default/center".
float window_x();
float window_y();
float window_w();
float window_h();
void  set_window_geometry(float x, float y, float w, float h);

} // namespace xpswissvfr::persistence

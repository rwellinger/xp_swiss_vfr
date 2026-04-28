#pragma once

namespace xpswissvfr::ui
{
// Lifecycle of the procedure-selection ImGui window. Mirrors the
// xp_welly_atc/atc_ui pattern: a single full-screen invisible XPLM window
// captures input, the actual ImGui rendering happens inside an
// XPLMRegisterDrawCallback hook on `xplm_Phase_Window`.
void init();
void stop();

// Show or hide the window. The XPLM capture window is created lazily on
// first show and reused thereafter; on hide we relinquish keyboard focus so
// X-Plane gets its key bindings (e.g. command shortcuts) back.
void toggle();
bool is_visible();

} // namespace xpswissvfr::ui

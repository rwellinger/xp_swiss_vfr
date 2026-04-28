#include "ui/procedure_selection_window.hpp"

#include "core/plugin.hpp"
#include "data/coordinate.hpp"
#include "data/nearby_airports.hpp"
#include "procedures/build_procedure.hpp"
#include "procedures/procedure_runner.hpp"
#include "procedures/procedure_state.hpp"

#include <XPLM/XPLMDataAccess.h>
#include <XPLM/XPLMDisplay.h>
#include <XPLM/XPLMGraphics.h>
#include <XPLM/XPLMUtilities.h>

#include <imgui.h>
#include <imgui_impl_opengl2.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace xpswissvfr::ui
{
namespace
{

// ── Tunables ─────────────────────────────────────────────────────────────────
constexpr double      MAX_RANGE_NM        = 25.0;
constexpr std::size_t MAX_NEARBY          = 10;
constexpr int         REFRESH_INTERVAL_MS = 1000;
constexpr float       DEFAULT_WIDTH       = 720.0F;
constexpr float       DEFAULT_HEIGHT      = 460.0F;

// ── State ────────────────────────────────────────────────────────────────────
// XPLM full-screen invisible capture window. Created lazily on first show so
// the plugin's input handling does not interfere with X-Plane until the user
// actually opens the UI.
XPLMWindowID s_window_id = nullptr;
bool         s_visible   = false;

// Frame timing for ImGui::IO.DeltaTime — updated on each draw_phase_cb call.
double s_last_frame_time = 0.0;

// Throttled cache of nearby airports — refreshed at ~1 Hz from the draw loop
// to avoid recomputing distance for the entire DB every frame.
std::vector<data::NearbyAirport>      s_nearby_cache;
std::chrono::steady_clock::time_point s_nearby_last_refresh{};

// Lazy DataRefs for aircraft position + sim time. Resolved on first use; held
// for the plugin's lifetime.
XPLMDataRef s_dr_lat  = nullptr;
XPLMDataRef s_dr_lon  = nullptr;
XPLMDataRef s_dr_time = nullptr;

double get_aircraft_lat()
{
    if (s_dr_lat == nullptr)
        s_dr_lat = XPLMFindDataRef("sim/flightmodel/position/latitude");
    return s_dr_lat != nullptr ? XPLMGetDatad(s_dr_lat) : 0.0;
}

double get_aircraft_lon()
{
    if (s_dr_lon == nullptr)
        s_dr_lon = XPLMFindDataRef("sim/flightmodel/position/longitude");
    return s_dr_lon != nullptr ? XPLMGetDatad(s_dr_lon) : 0.0;
}

double get_xp_time()
{
    if (s_dr_time == nullptr)
        s_dr_time = XPLMFindDataRef("sim/time/total_running_time_sec");
    return s_dr_time != nullptr ? static_cast<double>(XPLMGetDataf(s_dr_time)) : 0.0;
}

// ── ImGui rendering helpers ──────────────────────────────────────────────────

// Section header rendered in a soft accent color above a separator. The extra
// vertical padding before/after creates breathing room between sections so
// the window does not feel like one dense block of widgets.
void draw_section_header(const char *label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55F, 0.80F, 1.0F, 1.0F));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
}

void draw_warning_banner()
{
    // Strong red background, yellow headline, generous padding. The banner is
    // load-bearing per the phase doc — it must read clearly even at a glance.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.32F, 0.06F, 0.06F, 0.92F));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85F, 0.20F, 0.20F, 1.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));

    ImGui::BeginChild("warning_banner", ImVec2(0, 92), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.85F, 0.2F, 1.0F));
    ImGui::TextUnformatted("[!]  FLIGHT PLAN WILL BE MODIFIED  [!]");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::TextUnformatted("Activating injects VFR pattern waypoints into your active flight plan.");
    ImGui::TextUnformatted("Visual reference only - autopilots cannot fly the 90 deg pattern turns.");
    ImGui::TextUnformatted("Hand-fly the circuit. Use 'Clear' to remove the injected waypoints.");

    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void log_line(const std::string &line) { XPLMDebugString(line.c_str()); }

void activate_runway(const data::NearbyAirport &row, const std::string &runway)
{
    const data::VfrAirport *airport = core::airport_database().find(row.icao);
    if (airport == nullptr)
    {
        log_line("[xp_swiss_vfr] UI activate: airport " + row.icao + " not in database.\n");
        return;
    }

    auto procedure = procedures::build_procedure(*airport, runway);
    if (!procedure.has_value())
    {
        log_line("[xp_swiss_vfr] UI activate: build_procedure(" + row.icao + ", " + runway + ") returned nullopt.\n");
        return;
    }

    procedures::activate(*procedure);
}

ImVec4 state_color(procedures::State state)
{
    switch (state)
    {
    case procedures::State::ARMED:
        return ImVec4(1.0F, 0.85F, 0.2F, 1.0F);
    case procedures::State::ACTIVE:
        return ImVec4(0.4F, 1.0F, 0.4F, 1.0F);
    case procedures::State::COMPLETED:
        return ImVec4(0.6F, 0.6F, 0.6F, 1.0F);
    case procedures::State::IDLE:
    default:
        return ImVec4(0.8F, 0.8F, 0.8F, 1.0F);
    }
}

void draw_active_procedure_section()
{
    auto info = procedures::active_procedure_info();
    if (!info.has_value())
        return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08F, 0.22F, 0.10F, 0.85F));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30F, 0.65F, 0.35F, 1.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));

    ImGui::BeginChild("active_section", ImVec2(0, 60), true);

    procedures::State state = procedures::current_state();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80F, 1.00F, 0.80F, 1.0F));
    ImGui::TextUnformatted("ACTIVE PROCEDURE");
    ImGui::PopStyleColor();

    ImGui::Text("%s   RWY %s", info->airport_icao.c_str(), info->runway_designator.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("   ");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, state_color(state));
    ImGui::Text("[%s]", procedures::state_name(state));
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetWindowWidth() - 100);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.18F, 0.18F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75F, 0.25F, 0.25F, 1.0F));
    if (ImGui::Button("Clear##active_clear", ImVec2(80, 0)))
    {
        procedures::clear_active_procedure();
    }
    ImGui::PopStyleColor(2);

    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void refresh_nearby_if_due(const data::Coordinate &aircraft)
{
    auto       now    = std::chrono::steady_clock::now();
    const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_nearby_last_refresh).count();
    if (s_nearby_cache.empty() || age_ms >= REFRESH_INTERVAL_MS)
    {
        s_nearby_cache = data::find_nearby_airports(core::airport_database(), aircraft, MAX_RANGE_NM, MAX_NEARBY);
        s_nearby_last_refresh = now;
    }
}

void draw_runway_buttons(const data::NearbyAirport &row)
{
    if (row.available_runways.empty())
    {
        ImGui::TextDisabled("(no published runways)");
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 4));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.35F, 0.55F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30F, 0.50F, 0.75F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40F, 0.60F, 0.85F, 1.0F));

    for (std::size_t i = 0; i < row.available_runways.size(); ++i)
    {
        const auto &rwy = row.available_runways[i];
        char        label[64];
        std::snprintf(label, sizeof(label), "RWY %s##%s_%zu", rwy.c_str(), row.icao.c_str(), i);
        if (ImGui::Button(label))
        {
            activate_runway(row, rwy);
        }
        // Optional tooltip from the airport JSON's runway_notes — only shown
        // when the data file ships a description for this runway.
        if (ImGui::IsItemHovered())
        {
            auto it = row.runway_notes.find(rwy);
            if (it != row.runway_notes.end() && !it->second.empty())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
                ImGui::TextUnformatted(it->second.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        if (i + 1 < row.available_runways.size())
            ImGui::SameLine();
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

void draw_nearby_table()
{
    if (s_nearby_cache.empty())
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextDisabled("    (no supported airports within %.0f NM)", MAX_RANGE_NM);
        return;
    }

    constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 6));
    if (ImGui::BeginTable("nearby_table", 4, table_flags))
    {
        // No HeadersRow — the column titles look sortable but the table is
        // intentionally not sortable (rows are always nearest-first). Sortable
        // headers in ImGui require ImGuiTableFlags_Sortable + per-frame
        // SortSpecs handling, which buys nothing for a single-airport list.
        ImGui::TableSetupColumn("ICAO", ImGuiTableColumnFlags_WidthFixed, 60.0F);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 70.0F);
        ImGui::TableSetupColumn("Runways", ImGuiTableColumnFlags_WidthFixed, 220.0F);

        for (const auto &row : s_nearby_cache)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 0.6F, 1.0F));
            ImGui::TextUnformatted(row.icao.c_str());
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.name.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%5.1f NM", row.distance_nm);

            ImGui::TableSetColumnIndex(3);
            draw_runway_buttons(row);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void draw_position_footer(const data::Coordinate &aircraft)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55F, 0.55F, 0.55F, 1.0F));
    ImGui::Text("Aircraft: %.5f, %.5f    Search radius: %.0f NM", aircraft.lat, aircraft.lon, MAX_RANGE_NM);
    ImGui::PopStyleColor();
}

void draw_main_window()
{
    bool open = s_visible;
    ImGui::SetNextWindowSize(ImVec2(DEFAULT_WIDTH, DEFAULT_HEIGHT), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560, 360), ImVec2(1600, 1200));

    static const std::string title = std::string("xp_swiss_vfr - Procedure Selector##main");
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse))
    {
        draw_warning_banner();

        data::Coordinate aircraft{get_aircraft_lat(), get_aircraft_lon()};
        refresh_nearby_if_due(aircraft);

        draw_active_procedure_section();

        draw_section_header("Nearby Supported Airports");
        draw_nearby_table();

        draw_position_footer(aircraft);
    }
    ImGui::End();

    if (!open)
    {
        s_visible = false;
        if (s_window_id != nullptr)
        {
            XPLMSetWindowIsVisible(s_window_id, 0);
            XPLMTakeKeyboardFocus(nullptr);
        }
    }
}

// ── XPLM input-capture window callbacks (no rendering — that happens in the
// draw-phase callback below) ────────────────────────────────────────────────

void wnd_draw_cb(XPLMWindowID, void *) {}

bool imgui_wants_mouse_at(XPLMWindowID wnd, int x, int y)
{
    int left, top, right, bottom;
    XPLMGetWindowGeometry(wnd, &left, &top, &right, &bottom);
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x - left), static_cast<float>(top - y));
    return io.WantCaptureMouse;
}

int wnd_mouse_cb(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *)
{
    if (!imgui_wants_mouse_at(wnd, x, y))
        return 0;
    ImGuiIO &io = ImGui::GetIO();
    if (status == xplm_MouseDown)
        io.AddMouseButtonEvent(0, true);
    if (status == xplm_MouseUp)
        io.AddMouseButtonEvent(0, false);
    return 1;
}

int wnd_rclick_cb(XPLMWindowID wnd, int x, int y, XPLMMouseStatus status, void *)
{
    if (!imgui_wants_mouse_at(wnd, x, y))
        return 0;
    ImGuiIO &io = ImGui::GetIO();
    if (status == xplm_MouseDown)
        io.AddMouseButtonEvent(1, true);
    if (status == xplm_MouseUp)
        io.AddMouseButtonEvent(1, false);
    return 1;
}

int wnd_wheel_cb(XPLMWindowID wnd, int x, int y, int /*wheel*/, int clicks, void *)
{
    if (!imgui_wants_mouse_at(wnd, x, y))
        return 0;
    ImGui::GetIO().AddMouseWheelEvent(0.0F, static_cast<float>(clicks));
    return 1;
}

XPLMCursorStatus wnd_cursor_cb(XPLMWindowID, int, int, void *) { return xplm_CursorDefault; }

void wnd_key_cb(XPLMWindowID, char /*key*/, XPLMKeyFlags flags, char vkey, void *, int losing_focus)
{
    if (losing_focus != 0)
    {
        ImGui::GetIO().AddFocusEvent(false);
        return;
    }
    // Window has no text inputs; only intercept ESC to close.
    if ((flags & xplm_DownFlag) != 0 && vkey == XPLM_VK_ESCAPE)
    {
        s_visible = false;
        if (s_window_id != nullptr)
        {
            XPLMSetWindowIsVisible(s_window_id, 0);
            XPLMTakeKeyboardFocus(nullptr);
        }
    }
}

// ── Draw-phase callback: ImGui frame setup + render ─────────────────────────
//
// The capture window above only feeds input events; the actual ImGui rendering
// happens here, on `xplm_Phase_Window`, so the UI sits above the X-Plane HUD.
int draw_phase_cb(XPLMDrawingPhase, int, void *)
{
    if (!s_visible)
        return 1;

    int gl, gt, gr, gb;
    XPLMGetScreenBoundsGlobal(&gl, &gt, &gr, &gb);
    int sw = gr - gl;
    int sh = gt - gb;
    if (sw <= 0 || sh <= 0)
        return 1;

    if (s_window_id != nullptr)
    {
        int wl, wt, wr, wb;
        XPLMGetWindowGeometry(s_window_id, &wl, &wt, &wr, &wb);
        if (wl != gl || wb != gb || wr != gr || wt != gt)
            XPLMSetWindowGeometry(s_window_id, gl, gt, gr, gb);
    }

    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    glPushAttrib(GL_TRANSFORM_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT |
                 GL_TEXTURE_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, sw, sh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, sw, sh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    ImGuiIO &io       = ImGui::GetIO();
    double   now      = get_xp_time();
    io.DeltaTime      = static_cast<float>(std::max(now - s_last_frame_time, 0.001));
    s_last_frame_time = now;
    io.DisplaySize    = ImVec2(static_cast<float>(sw), static_cast<float>(sh));

    int gmx, gmy;
    XPLMGetMouseLocationGlobal(&gmx, &gmy);
    io.AddMousePosEvent(static_cast<float>(gmx - gl), static_cast<float>(gt - gmy));

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();

    draw_main_window();

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

    return 1;
}

void create_capture_window_if_missing()
{
    if (s_window_id != nullptr)
        return;

    int gl, gt, gr, gb;
    XPLMGetScreenBoundsGlobal(&gl, &gt, &gr, &gb);

    XPLMCreateWindow_t p{};
    p.structSize               = sizeof(p);
    p.left                     = gl;
    p.bottom                   = gb;
    p.right                    = gr;
    p.top                      = gt;
    p.visible                  = 1;
    p.drawWindowFunc           = wnd_draw_cb;
    p.handleMouseClickFunc     = wnd_mouse_cb;
    p.handleKeyFunc            = wnd_key_cb;
    p.handleCursorFunc         = wnd_cursor_cb;
    p.handleMouseWheelFunc     = wnd_wheel_cb;
    p.handleRightClickFunc     = wnd_rclick_cb;
    p.refcon                   = nullptr;
    p.decorateAsFloatingWindow = xplm_WindowDecorationNone;
    p.layer                    = xplm_WindowLayerFloatingWindows;
    s_window_id                = XPLMCreateWindowEx(&p);
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

void init()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io    = ImGui::GetIO();
    io.IniFilename = nullptr; // Phase 5 will introduce a real settings layer.
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();
    auto &style          = ImGui::GetStyle();
    style.WindowRounding = 6.0F;
    style.FrameRounding  = 3.0F;
    style.WindowPadding  = ImVec2(8, 6);

    ImGui_ImplOpenGL2_Init();
    s_last_frame_time = get_xp_time();

    XPLMRegisterDrawCallback(draw_phase_cb, xplm_Phase_Window, 1, nullptr);

    XPLMDebugString("[xp_swiss_vfr] ui::init — ImGui ready, draw callback registered.\n");
}

void stop()
{
    XPLMUnregisterDrawCallback(draw_phase_cb, xplm_Phase_Window, 1, nullptr);

    if (s_window_id != nullptr)
    {
        XPLMDestroyWindow(s_window_id);
        s_window_id = nullptr;
    }
    ImGui_ImplOpenGL2_Shutdown();
    ImGui::DestroyContext();

    s_nearby_cache.clear();
    s_visible = false;

    XPLMDebugString("[xp_swiss_vfr] ui::stop — ImGui torn down.\n");
}

void toggle()
{
    s_visible = !s_visible;

    if (s_visible)
    {
        create_capture_window_if_missing();
    }

    if (s_window_id != nullptr)
    {
        XPLMSetWindowIsVisible(s_window_id, s_visible ? 1 : 0);
        if (s_visible)
        {
            XPLMBringWindowToFront(s_window_id);
            XPLMTakeKeyboardFocus(s_window_id);
        }
        else
        {
            XPLMTakeKeyboardFocus(nullptr);
        }
    }
}

bool is_visible() { return s_visible; }

} // namespace xpswissvfr::ui

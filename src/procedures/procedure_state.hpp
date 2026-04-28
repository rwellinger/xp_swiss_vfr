#pragma once

#include <cstdint>

namespace xpswissvfr::procedures
{
// Lifecycle of an injected procedure as observed by the plugin.
//
//   IDLE       — nothing tracked; FMS contains only the pilot's own data
//                (or nothing). This is the initial and post-clear state.
//   ARMED      — `activate()` succeeded; waypoints are written into the FMS,
//                but the aircraft has not yet started flying the procedure.
//                Equivalent to "loaded but not engaged".
//   ACTIVE     — aircraft has crossed the first leg's start; the procedure is
//                being flown. (Auto-transition from ARMED is implemented in
//                Phase 3 part 2 via a flight-loop callback.)
//   COMPLETED  — aircraft has passed the last waypoint (runway threshold).
//                Re-activation re-enters ARMED.
enum class State : std::uint8_t
{
    IDLE,
    ARMED,
    ACTIVE,
    COMPLETED,
};

// Pure state machine — SDK-free, deterministic, fully unit-testable. The
// runner owns one instance and forwards observed events to it.
class ProcedureStateMachine
{
  public:
    State state() const { return state_; }

    // `activate()` may be called from IDLE or COMPLETED (re-arm after a
    // landing) or from ARMED/ACTIVE (re-activation while a procedure is
    // already loaded; the runner clears and re-injects). Result: ARMED.
    // Returns true when a transition happened.
    bool on_activate();

    // `clear()` may be called from any state. Result: IDLE. Returns true
    // when a transition happened (i.e. state was not already IDLE).
    bool on_clear();

    // Aircraft has started flying the procedure (first leg engaged). Only
    // valid from ARMED. Returns true on transition; ignored otherwise so
    // late or duplicate signals do not regress the state.
    bool on_first_leg_engaged();

    // Aircraft has passed the last waypoint (typically the threshold). Only
    // valid from ACTIVE. Returns true on transition.
    bool on_threshold_passed();

  private:
    State state_ = State::IDLE;
};

const char *state_name(State s);

} // namespace xpswissvfr::procedures

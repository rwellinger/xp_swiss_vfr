#include "procedures/procedure_state.hpp"

namespace xpswissvfr::procedures
{

bool ProcedureStateMachine::on_activate()
{
    state_ = State::ARMED;
    return true;
}

bool ProcedureStateMachine::on_clear()
{
    if (state_ == State::IDLE)
        return false;
    state_ = State::IDLE;
    return true;
}

bool ProcedureStateMachine::on_first_leg_engaged()
{
    if (state_ != State::ARMED)
        return false;
    state_ = State::ACTIVE;
    return true;
}

bool ProcedureStateMachine::on_threshold_passed()
{
    if (state_ != State::ACTIVE)
        return false;
    state_ = State::COMPLETED;
    return true;
}

const char *state_name(State s)
{
    switch (s)
    {
    case State::IDLE:
        return "IDLE";
    case State::ARMED:
        return "ARMED";
    case State::ACTIVE:
        return "ACTIVE";
    case State::COMPLETED:
        return "COMPLETED";
    }
    return "?";
}

} // namespace xpswissvfr::procedures

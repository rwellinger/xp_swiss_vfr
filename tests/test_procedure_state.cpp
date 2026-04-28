#include "procedures/procedure_state.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <string>

using xpswissvfr::procedures::ProcedureStateMachine;
using xpswissvfr::procedures::State;
using xpswissvfr::procedures::state_name;

TEST_CASE("State machine starts in IDLE", "[procedure_state]")
{
    ProcedureStateMachine sm;
    REQUIRE(sm.state() == State::IDLE);
}

TEST_CASE("activate from IDLE → ARMED", "[procedure_state]")
{
    ProcedureStateMachine sm;
    REQUIRE(sm.on_activate());
    REQUIRE(sm.state() == State::ARMED);
}

TEST_CASE("activate from ARMED stays ARMED (re-injection)", "[procedure_state]")
{
    ProcedureStateMachine sm;
    sm.on_activate();
    REQUIRE(sm.on_activate());
    REQUIRE(sm.state() == State::ARMED);
}

TEST_CASE("clear from IDLE returns false (no transition)", "[procedure_state]")
{
    ProcedureStateMachine sm;
    REQUIRE_FALSE(sm.on_clear());
    REQUIRE(sm.state() == State::IDLE);
}

TEST_CASE("clear from any non-IDLE state → IDLE", "[procedure_state]")
{
    SECTION("from ARMED")
    {
        ProcedureStateMachine sm;
        sm.on_activate();
        REQUIRE(sm.on_clear());
        REQUIRE(sm.state() == State::IDLE);
    }
    SECTION("from ACTIVE")
    {
        ProcedureStateMachine sm;
        sm.on_activate();
        sm.on_first_leg_engaged();
        REQUIRE(sm.on_clear());
        REQUIRE(sm.state() == State::IDLE);
    }
    SECTION("from COMPLETED")
    {
        ProcedureStateMachine sm;
        sm.on_activate();
        sm.on_first_leg_engaged();
        sm.on_threshold_passed();
        REQUIRE(sm.on_clear());
        REQUIRE(sm.state() == State::IDLE);
    }
}

TEST_CASE("first_leg_engaged: ARMED → ACTIVE", "[procedure_state]")
{
    ProcedureStateMachine sm;
    sm.on_activate();
    REQUIRE(sm.on_first_leg_engaged());
    REQUIRE(sm.state() == State::ACTIVE);
}

TEST_CASE("first_leg_engaged ignored when not ARMED", "[procedure_state]")
{
    ProcedureStateMachine sm;
    REQUIRE_FALSE(sm.on_first_leg_engaged());
    REQUIRE(sm.state() == State::IDLE);
}

TEST_CASE("first_leg_engaged from ACTIVE stays ACTIVE (idempotent)", "[procedure_state]")
{
    ProcedureStateMachine sm;
    sm.on_activate();
    sm.on_first_leg_engaged();
    REQUIRE_FALSE(sm.on_first_leg_engaged());
    REQUIRE(sm.state() == State::ACTIVE);
}

TEST_CASE("threshold_passed: ACTIVE → COMPLETED", "[procedure_state]")
{
    ProcedureStateMachine sm;
    sm.on_activate();
    sm.on_first_leg_engaged();
    REQUIRE(sm.on_threshold_passed());
    REQUIRE(sm.state() == State::COMPLETED);
}

TEST_CASE("threshold_passed ignored from ARMED (must be ACTIVE first)", "[procedure_state]")
{
    ProcedureStateMachine sm;
    sm.on_activate();
    REQUIRE_FALSE(sm.on_threshold_passed());
    REQUIRE(sm.state() == State::ARMED);
}

TEST_CASE("re-activate from COMPLETED → ARMED (start a new circuit)", "[procedure_state]")
{
    ProcedureStateMachine sm;
    sm.on_activate();
    sm.on_first_leg_engaged();
    sm.on_threshold_passed();
    REQUIRE(sm.on_activate());
    REQUIRE(sm.state() == State::ARMED);
}

TEST_CASE("state_name returns stable strings for logging", "[procedure_state]")
{
    REQUIRE(std::string(state_name(State::IDLE)) == "IDLE");
    REQUIRE(std::string(state_name(State::ARMED)) == "ARMED");
    REQUIRE(std::string(state_name(State::ACTIVE)) == "ACTIVE");
    REQUIRE(std::string(state_name(State::COMPLETED)) == "COMPLETED");
}

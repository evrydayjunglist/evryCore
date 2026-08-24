/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-rts-spike/src/RtsSwitchState.h"

namespace
{
    ObjectGuid PlayerGuid(ObjectGuid::LowType counter)
    {
        return ObjectGuid::Create<HighGuid::Player>(counter);
    }
}

TEST_CASE("RTS switch state requires two distinct players and one active transition", "[rts-spike][switch]")
{
    RtsSwitchState state;
    ObjectGuid const controller = PlayerGuid(1);
    ObjectGuid const subject = PlayerGuid(2);

    REQUIRE_FALSE(state.Begin(ObjectGuid::Empty, subject));
    REQUIRE_FALSE(state.Begin(controller, ObjectGuid::Empty));
    REQUIRE_FALSE(state.Begin(controller, controller));
    REQUIRE(state.Begin(controller, subject));
    REQUIRE_FALSE(state.Begin(PlayerGuid(3), PlayerGuid(4)));
    REQUIRE(state.Phase() == RtsSwitchPhase::Quiescing);
    REQUIRE(state.Contains(controller));
    REQUIRE(state.Contains(subject));
}

TEST_CASE("RTS switch state waits for quiescing and activates only the reserved pair", "[rts-spike][switch]")
{
    RtsSwitchState state;
    ObjectGuid const controller = PlayerGuid(10);
    ObjectGuid const subject = PlayerGuid(20);

    REQUIRE(state.Begin(controller, subject));
    REQUIRE_FALSE(state.AdvanceQuiescing(99, 100));
    REQUIRE(state.AdvanceQuiescing(1, 100));
    REQUIRE(state.AdvanceQuiescing(1'900, 100));
    REQUIRE(state.QuiesceElapsedMs() == 2'000);
    REQUIRE_FALSE(state.Activate(controller, PlayerGuid(21)));
    REQUIRE(state.Activate(controller, subject));
    REQUIRE(state.Phase() == RtsSwitchPhase::Active);

    state.Clear();
    REQUIRE(state.Phase() == RtsSwitchPhase::Idle);
    REQUIRE_FALSE(state.Contains(controller));
    REQUIRE(state.ControllerGuid().IsEmpty());
    REQUIRE(state.SubjectGuid().IsEmpty());
}

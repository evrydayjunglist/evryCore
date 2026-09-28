/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotUpdateCost.h"

TEST_CASE("A slow bot update names the steps that took the time", "[playerbots][scale]")
{
    PlayerbotUpdateCost cost;

    // Picking a stand spot that asked the navmesh twice, then a map pick on its own.
    cost.Enter();
    cost.Enter();
    cost.Leave(PlayerbotCostStep::NavmeshRoute, 900000);
    cost.Enter();
    cost.Leave(PlayerbotCostStep::NavmeshRoute, 100000);
    cost.Leave(PlayerbotCostStep::Approach, 1010000);
    cost.Enter();
    cost.Leave(PlayerbotCostStep::MapYellow, 20000);

    REQUIRE(cost.Micros(PlayerbotCostStep::NavmeshRoute) == 1000000);
    REQUIRE(cost.Calls(PlayerbotCostStep::NavmeshRoute) == 2);
    REQUIRE(cost.Calls(PlayerbotCostStep::Approach) == 1);
    // Only the outermost steps count against the whole update, so nested time is not taken off twice.
    REQUIRE(cost.OutsideMicros(1100000) == 70000);
    // A clock that read the whole update as shorter than its steps leaves nothing outside them.
    REQUIRE(cost.OutsideMicros(1000) == 0);

    REQUIRE(cost.Describe(1100000) == "stand spot picks 1010.0 ms (1), navmesh routes 1000.0 ms (2), map work picks 20.0 ms (1), "
        "and 70.0 ms in no timed step");

    cost.Clear();
    REQUIRE(cost.Describe(3000) == "3.0 ms in no timed step");
}

TEST_CASE("Every timed step has a name", "[playerbots][scale]")
{
    for (std::size_t index = 0; index < PlayerbotUpdateCost::StepCount; ++index)
    {
        char const* name = PlayerbotUpdateCost::Name(PlayerbotCostStep(index));
        REQUIRE(name != nullptr);
        REQUIRE(name[0] != '\0');
    }
}

TEST_CASE("A tick adds up its bot updates", "[playerbots][scale]")
{
    PlayerbotUpdateCost first;
    first.Enter();
    first.Leave(PlayerbotCostStep::FindTakeableQuest, 3000);
    PlayerbotUpdateCost second;
    second.Enter();
    second.Leave(PlayerbotCostStep::FindTakeableQuest, 2000);

    PlayerbotUpdateCost tick;
    tick.Add(first);
    tick.Add(second);
    REQUIRE(tick.Micros(PlayerbotCostStep::FindTakeableQuest) == 5000);
    REQUIRE(tick.Calls(PlayerbotCostStep::FindTakeableQuest) == 2);
    REQUIRE(tick.OutsideMicros(6000) == 1000);
}

TEST_CASE("A cost timer adds nothing when no bot update is being timed", "[playerbots][scale]")
{
    PlayerbotUpdateCost cost;
    REQUIRE(PlayerbotUpdateCost::Current() == nullptr);
    {
        PlayerbotCostTimer const timer(PlayerbotCostStep::Ground);
    }
    REQUIRE(cost.Calls(PlayerbotCostStep::Ground) == 0);

    PlayerbotUpdateCost::Current() = &cost;
    {
        PlayerbotCostTimer const outer(PlayerbotCostStep::Combat);
        PlayerbotCostTimer const inner(PlayerbotCostStep::Ground);
    }
    PlayerbotUpdateCost::Current() = nullptr;
    REQUIRE(cost.Calls(PlayerbotCostStep::Combat) == 1);
    REQUIRE(cost.Calls(PlayerbotCostStep::Ground) == 1);
}

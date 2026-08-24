/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotJump.h"
#include "../../modules/mod-playerbots/src/PlayerbotMovementRecovery.h"

TEST_CASE("Playerbot face recovery survives a short mmap rejoin", "[playerbots][movement]")
{
    PlayerbotFaceRecovery recovery;
    recovery.Begin(0.0f, 0.0f);
    recovery.BeginMmapRejoin(100.0f, 0.0f, 0.0f);

    REQUIRE_FALSE(recovery.AdvanceMmap(4.5f, 100.2f, 4.5f, 0.0f));
    REQUIRE(recovery.Refuse());
    REQUIRE(recovery.Active());
    REQUIRE_FALSE(recovery.Rejoining());
}

TEST_CASE("Playerbot face recovery clears after sustained useful mmap travel", "[playerbots][movement]")
{
    PlayerbotFaceRecovery recovery;
    recovery.Begin(0.0f, 0.0f);
    recovery.BeginMmapRejoin(100.0f, 0.0f, 0.0f);

    REQUIRE_FALSE(recovery.AdvanceMmap(7.9f, 98.0f, 7.9f, 0.0f));
    REQUIRE(recovery.AdvanceMmap(0.1f, 98.0f, 8.0f, 0.0f));
}

TEST_CASE("Playerbot face recovery does not clear on an mmap rejoin through visited ground", "[playerbots][movement]")
{
    PlayerbotFaceRecovery recovery;
    recovery.Begin(0.0f, 0.0f);
    for (int yard = 1; yard <= 8; ++yard)
        recovery.Advance(1.0f, float(yard), 0.0f);

    recovery.BeginMmapRejoin(100.0f, 8.0f, 0.0f);
    for (int yard = 7; yard >= 0; --yard)
        REQUIRE_FALSE(recovery.AdvanceMmap(1.0f, 90.0f, float(yard), 0.0f));

    REQUIRE(recovery.Active());
    REQUIRE(recovery.Rejoining());
}

TEST_CASE("Playerbot face recovery keeps following a boundary into new ground", "[playerbots][movement]")
{
    PlayerbotFaceRecovery recovery;
    recovery.Begin(0.0f, 0.0f);

    for (int yard = 1; yard <= 100; ++yard)
        recovery.Advance(1.0f, float(yard), 0.0f);

    REQUIRE_FALSE(recovery.Exhausted());
    REQUIRE(recovery.EpisodeYards() == Catch::Approx(100.0f));
    REQUIRE(recovery.VisitedGroundCells() >= 50);
    REQUIRE(recovery.StalledYards() < 2.0f);
}

TEST_CASE("Playerbot face recovery exhausts cycling after reaching its frontier", "[playerbots][movement]")
{
    PlayerbotFaceRecovery recovery;
    recovery.Begin(0.0f, 0.0f);

    recovery.Advance(3.0f, 3.0f, 0.0f);
    recovery.Advance(3.0f, 6.0f, 0.0f);
    for (int cycle = 0; cycle < 4; ++cycle)
        recovery.Advance(6.0f, cycle % 2 ? 3.0f : 6.0f, 0.0f);

    REQUIRE(recovery.Exhausted());
    REQUIRE(recovery.EpisodeYards() == Catch::Approx(30.0f));
    REQUIRE(recovery.StalledYards() == Catch::Approx(24.0f));
}

TEST_CASE("Playerbot face recovery remembers one jump attempt for the whole episode", "[playerbots][movement]")
{
    PlayerbotFaceRecovery recovery;
    recovery.Begin(0.0f, 0.0f);
    recovery.MarkJumpAttempted();
    recovery.BeginMmapRejoin(100.0f, 0.0f, 0.0f);
    REQUIRE(recovery.Refuse());

    REQUIRE(recovery.JumpAttempted());
    recovery.Reset();
    REQUIRE_FALSE(recovery.JumpAttempted());
}

TEST_CASE("Playerbot recovery goals are stable across stand positions but not actors", "[playerbots][movement]")
{
    PlayerbotRecoveryGoal const creature = PlayerbotRecoveryGoal::ForObject(
        PlayerbotRecoveryGoalKind::Creature, 1, 100, 200);
    PlayerbotRecoveryGoal const sameCreature = PlayerbotRecoveryGoal::ForObject(
        PlayerbotRecoveryGoalKind::Creature, 1, 100, 200);
    PlayerbotRecoveryGoal const otherCreature = PlayerbotRecoveryGoal::ForObject(
        PlayerbotRecoveryGoalKind::Creature, 1, 101, 200);
    PlayerbotRecoveryGoal const objective = PlayerbotRecoveryGoal::ForObjective(
        PlayerbotRecoveryGoalKind::MapCreatureObjective, 1, 42, 9001);

    REQUIRE(creature == sameCreature);
    REQUIRE(creature != otherCreature);
    REQUIRE(creature != objective);
}

TEST_CASE("Playerbot jump trajectory follows the normal ballistic arc", "[playerbots][movement]")
{
    PlayerbotJumpTrajectory trajectory;
    trajectory.HorizontalSpeed = 7.0f;
    trajectory.VerticalSpeed = 7.955547f;
    trajectory.Gravity = 19.291103f;

    REQUIRE(trajectory.ApexTime() == Catch::Approx(0.4124f).margin(0.0001f));
    REQUIRE(trajectory.ApexHeight() == Catch::Approx(1.6405f).margin(0.001f));

    float const landTime = trajectory.DescendingTimeToHeight(0.0f);
    REQUIRE(landTime == Catch::Approx(0.8248f).margin(0.0001f));
    REQUIRE(trajectory.HorizontalDistance(landTime) == Catch::Approx(5.7737f).margin(0.001f));
    REQUIRE(trajectory.DescendingTimeToHeight(-2.0f) > landTime);
    REQUIRE(trajectory.DescendingTimeToHeight(trajectory.ApexHeight() + 0.1f) == 0.0f);
}

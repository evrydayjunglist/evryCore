/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotVariety.h"
#include <cmath>
#include <set>

TEST_CASE("A bot keeps the same stand spot preference for the same target", "[playerbots][variety]")
{
    PlayerbotStandPreference const first = PickPlayerbotStandPreference(12, 0x1234, 0x5678);
    PlayerbotStandPreference const again = PickPlayerbotStandPreference(12, 0x1234, 0x5678);
    REQUIRE(first.AngleOffset == again.AngleOffset);
    REQUIRE(first.ExtraYards == again.ExtraYards);
}

TEST_CASE("Stand spot preferences stay inside the spread and the extra distance", "[playerbots][variety]")
{
    for (uint64 bot = 1; bot <= 2000; ++bot)
    {
        PlayerbotStandPreference const preference = PickPlayerbotStandPreference(bot, 0xF130000000001234ull, 0x0000ABCDull);
        REQUIRE(std::abs(preference.AngleOffset) <= PLAYERBOT_STAND_SPREAD_RADIANS);
        REQUIRE(preference.ExtraYards >= 0.0f);
        REQUIRE(preference.ExtraYards < PLAYERBOT_STAND_EXTRA_YARDS);
    }
}

TEST_CASE("Bots at one target spread over the whole spread and distance", "[playerbots][variety]")
{
    // Ten angle bins and four distance bins: a hundred bots at one quest giver should reach every one of them.
    std::set<int> angleBins;
    std::set<int> distanceBins;
    std::set<std::pair<int, int>> spots;
    for (uint64 bot = 1; bot <= 100; ++bot)
    {
        PlayerbotStandPreference const preference = PickPlayerbotStandPreference(bot, 777, 3);
        int const angleBin = int((preference.AngleOffset + PLAYERBOT_STAND_SPREAD_RADIANS) / (2.0f * PLAYERBOT_STAND_SPREAD_RADIANS) * 10.0f);
        int const distanceBin = int(preference.ExtraYards / PLAYERBOT_STAND_EXTRA_YARDS * 4.0f);
        angleBins.insert(angleBin);
        distanceBins.insert(distanceBin);
        spots.insert({ angleBin, distanceBin });
    }
    REQUIRE(angleBins.size() == 10);
    REQUIRE(distanceBins.size() == 4);
    // Most bots get a spot of their own even before the crowd check.
    REQUIRE(spots.size() >= 25);
}

TEST_CASE("One bot likes different sides of different targets", "[playerbots][variety]")
{
    std::set<float> offsets;
    for (uint64 target = 1; target <= 50; ++target)
        offsets.insert(PickPlayerbotStandPreference(42, target, 0).AngleOffset);
    REQUIRE(offsets.size() == 50);
}

TEST_CASE("Waits stay inside their bounds and differ between bots and between waits", "[playerbots][variety]")
{
    std::set<uint32> byBot;
    std::set<uint32> byCount;
    for (uint32 i = 0; i < 200; ++i)
    {
        uint32 const wait = PickPlayerbotWait(i + 1, PlayerbotWaitKind::QuestChainPause, 0,
            PLAYERBOT_QUEST_CHAIN_PAUSE_MIN_MS, PLAYERBOT_QUEST_CHAIN_PAUSE_MAX_MS);
        REQUIRE(wait >= PLAYERBOT_QUEST_CHAIN_PAUSE_MIN_MS);
        REQUIRE(wait <= PLAYERBOT_QUEST_CHAIN_PAUSE_MAX_MS);
        byBot.insert(wait);

        byCount.insert(PickPlayerbotWait(7, PlayerbotWaitKind::ReleaseSpirit, i,
            PLAYERBOT_RELEASE_WAIT_MIN_MS, PLAYERBOT_RELEASE_WAIT_MAX_MS));
    }
    // Two hundred bots arriving together do not share a handful of ticks.
    REQUIRE(byBot.size() >= 150);
    REQUIRE(byCount.size() >= 150);

    REQUIRE(PickPlayerbotWait(7, PlayerbotWaitKind::InviteAnswer, 3, 1000, 3000)
        == PickPlayerbotWait(7, PlayerbotWaitKind::InviteAnswer, 3, 1000, 3000));
    REQUIRE(PickPlayerbotWait(7, PlayerbotWaitKind::InviteAnswer, 3, 900, 900) == 900);
}

TEST_CASE("Every picked wait is above zero, so zero can mean not picked yet", "[playerbots][variety]")
{
    REQUIRE(PLAYERBOT_QUEST_CHAIN_PAUSE_MIN_MS > 0);
    REQUIRE(PLAYERBOT_RELEASE_WAIT_MIN_MS > 0);
    REQUIRE(PLAYERBOT_INVITE_ANSWER_MIN_MS > 0);
    REQUIRE(PLAYERBOT_STAND_UP_AFTER_REST_MIN_MS > 0);
    REQUIRE(PLAYERBOT_LOOK_AROUND_MIN_MS > 0);
}

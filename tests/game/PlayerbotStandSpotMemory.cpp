/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotStandSpotMemory.h"

#include <string>

namespace
{
    PlayerbotStandSpotMemory::Question Asking(uint64_t bot, uint64_t target)
    {
        PlayerbotStandSpotMemory::Question question;
        question.Bot = bot;
        question.TargetLow = target;
        question.TargetHigh = 7;
        question.MapId = 1;
        question.StandDistance = 2.5f;
        question.Feet = { 100.0f, 200.0f, 30.0f };
        question.Target = { 120.0f, 200.0f, 31.0f };
        return question;
    }

    PlayerbotStandSpotMemory::Answer Spot(float x)
    {
        PlayerbotStandSpotMemory::Answer answer;
        answer.Found = true;
        answer.X = x;
        answer.Y = 200.0f;
        answer.Z = 31.0f;
        answer.Orientation = 1.0f;
        return answer;
    }
}

TEST_CASE("A stand spot is remembered for the same question from the same feet", "[playerbots][scale]")
{
    PlayerbotStandSpotMemory memory;
    PlayerbotStandSpotMemory::Answer answer;
    REQUIRE_FALSE(memory.Recall(Asking(1, 50), 1000, answer));

    memory.Remember(Asking(1, 50), 1000, Spot(117.5f));
    REQUIRE(memory.Recall(Asking(1, 50), 1000 + PlayerbotStandSpotMemory::KeepMs, answer));
    REQUIRE(answer.Found);
    REQUIRE(answer.X == 117.5f);

    // A few centimetres of movement is the same feet.
    PlayerbotStandSpotMemory::Question nudged = Asking(1, 50);
    nudged.Feet.X += 0.3f;
    nudged.Target.Y += 0.2f;
    REQUIRE(memory.Recall(nudged, 1500, answer));

    // A "nowhere to stand" answer is remembered as well.
    memory.Remember(Asking(1, 51), 1000, PlayerbotStandSpotMemory::Answer{});
    REQUIRE(memory.Recall(Asking(1, 51), 1200, answer));
    REQUIRE_FALSE(answer.Found);
}

TEST_CASE("A stand spot is asked again when anything it depends on changed", "[playerbots][scale]")
{
    PlayerbotStandSpotMemory memory;
    PlayerbotStandSpotMemory::Answer answer;
    memory.Remember(Asking(1, 50), 1000, Spot(117.5f));

    REQUIRE_FALSE(memory.Recall(Asking(1, 50), 1001 + PlayerbotStandSpotMemory::KeepMs, answer));
    REQUIRE_FALSE(memory.Recall(Asking(2, 50), 1000, answer));
    REQUIRE_FALSE(memory.Recall(Asking(1, 52), 1000, answer));

    PlayerbotStandSpotMemory::Question walked = Asking(1, 50);
    walked.Feet.X += 1.0f;
    REQUIRE_FALSE(memory.Recall(walked, 1000, answer));

    PlayerbotStandSpotMemory::Question targetMoved = Asking(1, 50);
    targetMoved.Target.X += 0.5f;
    REQUIRE_FALSE(memory.Recall(targetMoved, 1000, answer));

    PlayerbotStandSpotMemory::Question closer = Asking(1, 50);
    closer.StandDistance = 2.0f;
    REQUIRE_FALSE(memory.Recall(closer, 1000, answer));

    PlayerbotStandSpotMemory::Question otherMap = Asking(1, 50);
    otherMap.MapId = 0;
    REQUIRE_FALSE(memory.Recall(otherMap, 1000, answer));

    PlayerbotStandSpotMemory::Question otherKind = Asking(1, 50);
    otherKind.TargetHigh = 8;
    REQUIRE_FALSE(memory.Recall(otherKind, 1000, answer));
}

TEST_CASE("Stand spot memory keeps a few answers per bot and forgets a bot that logged out", "[playerbots][scale]")
{
    PlayerbotStandSpotMemory memory;
    PlayerbotStandSpotMemory::Answer answer;
    for (uint64_t target = 0; target < PlayerbotStandSpotMemory::SlotsPerBot + 1; ++target)
        memory.Remember(Asking(1, target), 1000, Spot(float(target)));

    // The oldest answer made room for the newest.
    REQUIRE_FALSE(memory.Recall(Asking(1, 0), 1000, answer));
    REQUIRE(memory.Recall(Asking(1, PlayerbotStandSpotMemory::SlotsPerBot), 1000, answer));
    REQUIRE(answer.X == float(PlayerbotStandSpotMemory::SlotsPerBot));

    memory.Remember(Asking(2, 5), 1000, Spot(5.0f));
    REQUIRE(memory.BotsRemembered() == 2);
    memory.Forget(1);
    REQUIRE(memory.BotsRemembered() == 1);
    REQUIRE_FALSE(memory.Recall(Asking(1, PlayerbotStandSpotMemory::SlotsPerBot), 1000, answer));
    REQUIRE(memory.Recall(Asking(2, 5), 1000, answer));
}

TEST_CASE("The stand spot report counts picks, answers from memory and sides", "[playerbots][scale]")
{
    PlayerbotStandSpotMemory memory;
    PlayerbotStandSpotMemory::Answer answer;
    memory.Recall(Asking(1, 50), 1000, answer);
    memory.NoteSides(3, 5);
    memory.Remember(Asking(1, 50), 1000, Spot(117.5f));
    memory.Recall(Asking(1, 50), 1100, answer);

    std::string const text = memory.DescribeWindowAndClear();
    REQUIRE(text.find("2 stand spot pick(s), 1 answered") != std::string::npos);
    REQUIRE(text.find("asked about 3 side(s)") != std::string::npos);
    REQUIRE(text.find("ask about 5 that") != std::string::npos);
    REQUIRE(memory.DescribeWindowAndClear().find("0 stand spot pick(s), 0 answered") != std::string::npos);
}

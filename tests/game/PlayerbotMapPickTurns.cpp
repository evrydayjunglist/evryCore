/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotMapPickTurns.h"

TEST_CASE("Bots pick straight away while nobody is waiting and the tick has room", "[playerbots][scale]")
{
    PlayerbotMapPickTurns turns;
    turns.BeginTick(50);
    for (uint32_t bot = 0; bot < PLAYERBOT_MAP_PICKS_AT_ONCE; ++bot)
        REQUIRE(turns.MayPick(bot));
    REQUIRE(turns.Waiting() == 0);

    // Asking again in the same tick after a yes is yes and takes no second place.
    REQUIRE(turns.MayPick(0));

    // The tick is full: the next bot waits in line.
    uint32_t const late = PLAYERBOT_MAP_PICKS_AT_ONCE;
    REQUIRE_FALSE(turns.MayPick(late));
    REQUIRE(turns.InLine(late));

    // Next tick she is served first.
    turns.BeginTick(50);
    REQUIRE(turns.MayPick(late));
    REQUIRE_FALSE(turns.InLine(late));
}

TEST_CASE("Once picks have used the tick's time, the rest wait", "[playerbots][scale]")
{
    PlayerbotMapPickTurns turns;
    turns.BeginTick(50);

    // The first pick of a tick always runs, however long it takes.
    REQUIRE(turns.MayPick(1));
    turns.NoteSpent(PLAYERBOT_MAP_PICK_BUDGET_MICROS * 3);
    REQUIRE_FALSE(turns.MayPick(2));

    // A fresh tick has fresh time.
    turns.BeginTick(50);
    REQUIRE(turns.MayPick(2));
    turns.NoteSpent(100);
    REQUIRE(turns.MayPick(3));
}

TEST_CASE("Waiting bots are served in the order they asked, and newcomers do not jump the line", "[playerbots][scale]")
{
    PlayerbotMapPickTurns turns;
    turns.BeginTick(50);
    REQUIRE(turns.MayPick(100));
    turns.NoteSpent(PLAYERBOT_MAP_PICK_BUDGET_MICROS);

    // Bots 1 to 20 ask while the tick is used up.
    for (uint32_t bot = 1; bot <= 20; ++bot)
        REQUIRE_FALSE(turns.MayPick(bot));
    REQUIRE(turns.Waiting() == 20);

    // Next tick, a newcomer updated before them joins the back of the line.
    turns.BeginTick(50);
    REQUIRE_FALSE(turns.MayPick(500));

    // Bot 12 is updated first, but she is not near the front yet.
    REQUIRE_FALSE(turns.MayPick(12));
    for (uint32_t bot = 1; bot <= PLAYERBOT_MAP_PICKS_AT_ONCE; ++bot)
        REQUIRE(turns.MayPick(bot));
    for (uint32_t bot = PLAYERBOT_MAP_PICKS_AT_ONCE + 1; bot <= 20; ++bot)
        REQUIRE_FALSE(turns.MayPick(bot));
    REQUIRE_FALSE(turns.MayPick(500));
    REQUIRE(turns.Waiting() == 20 - PLAYERBOT_MAP_PICKS_AT_ONCE + 1);
}

TEST_CASE("A bot that stops asking loses her place in the pick line", "[playerbots][scale]")
{
    PlayerbotMapPickTurns turns;
    turns.BeginTick(50);
    REQUIRE(turns.MayPick(100));
    turns.NoteSpent(PLAYERBOT_MAP_PICK_BUDGET_MICROS);
    REQUIRE_FALSE(turns.MayPick(1));

    // She found other work and stops asking. While she is still in line, a newcomer waits behind her.
    turns.BeginTick(50);
    REQUIRE_FALSE(turns.MayPick(2));
    REQUIRE(turns.MayPick(2));
    REQUIRE(turns.InLine(1));

    for (uint64_t tick = 0; tick < PLAYERBOT_MAP_PICK_GONE_TICKS; ++tick)
        turns.BeginTick(50);
    REQUIRE_FALSE(turns.InLine(1));
    REQUIRE(turns.Waiting() == 0);

    // With nobody waiting, the next bot picks straight away.
    REQUIRE(turns.MayPick(3));
}

TEST_CASE("The pick report says how many picked and how long bots waited", "[playerbots][scale]")
{
    PlayerbotMapPickTurns asking;
    asking.BeginTick(50);
    REQUIRE(asking.MayPick(1));
    asking.NoteSpent(PLAYERBOT_MAP_PICK_BUDGET_MICROS);
    REQUIRE_FALSE(asking.MayPick(2));
    asking.BeginTick(1500);
    REQUIRE(asking.MayPick(2));

    REQUIRE(asking.DescribeWindowAndClear()
        == "2 map-work pick(s), at most 1 in one tick. 1 bot(s) had to wait for a turn; at most 1 were in line at once, "
           "and the longest wait was 1.5 s.");
    REQUIRE(asking.DescribeWindowAndClear()
        == "0 map-work pick(s), at most 0 in one tick. 0 bot(s) had to wait for a turn; at most 0 were in line at once, "
           "and the longest wait was 0.0 s.");
}

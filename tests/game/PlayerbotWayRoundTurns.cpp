/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotWayRoundTurns.h"

TEST_CASE("Bots take turns to map the ground in the order they stopped", "[playerbots][scale]")
{
    PlayerbotWayRoundTurns turns;
    turns.BeginTick();
    uint64_t const first = turns.Join("too steep");
    uint64_t const second = turns.Join("too steep");
    uint64_t const third = turns.Join("the navmesh had no route from her feet");
    REQUIRE(first != 0);
    REQUIRE(turns.Waiting() == 3);

    // The third bot is updated first in the tick, but the two in front of her are served.
    REQUIRE_FALSE(turns.IsHerTurn(third));
    REQUIRE(turns.IsHerTurn(first));
    REQUIRE(turns.IsHerTurn(second));

    // When the first finishes, the third moves up.
    turns.NoteEnd(PlayerbotWayRoundEnd::Found, 0);
    turns.Leave(first);
    turns.BeginTick();
    REQUIRE(turns.IsHerTurn(third));
    REQUIRE(turns.IsHerTurn(second));

    // Leaving twice, or with no ticket, does nothing.
    turns.Leave(first);
    turns.Leave(0);
    REQUIRE(turns.Waiting() == 2);
}

TEST_CASE("A bot that stops coming for her turn loses her place", "[playerbots][scale]")
{
    PlayerbotWayRoundTurns turns;
    turns.BeginTick();
    uint64_t const gone = turns.Join("too steep");
    uint64_t const other = turns.Join("too steep");
    uint64_t const waiting = turns.Join("too steep");
    REQUIRE(turns.IsHerTurn(gone));
    REQUIRE(turns.IsHerTurn(other));
    REQUIRE_FALSE(turns.IsHerTurn(waiting));

    // One tick without her is not enough to drop her: her update may simply have come after the others.
    turns.BeginTick();
    REQUIRE(turns.IsHerTurn(other));
    REQUIRE_FALSE(turns.IsHerTurn(waiting));

    // A bot waiting deep in line is not dropped for waiting, only one at the front who stops coming.
    for (int tick = 0; tick < 3; ++tick)
    {
        turns.BeginTick();
        turns.IsHerTurn(other);
        turns.IsHerTurn(waiting);
    }
    REQUIRE_FALSE(turns.InLine(gone));
    REQUIRE(turns.InLine(waiting));
    REQUIRE(turns.IsHerTurn(waiting));
}

TEST_CASE("The way-round report says why bots looked and how they did", "[playerbots][scale]")
{
    PlayerbotWayRoundTurns turns;
    turns.BeginTick();
    uint64_t const a = turns.Join("too steep");
    uint64_t const b = turns.Join("too steep");
    uint64_t const c = turns.Join("the navmesh had no route from her feet");
    turns.NoteEnd(PlayerbotWayRoundEnd::Found, 0, 812500, 23000);
    turns.Leave(a);
    turns.NoteEnd(PlayerbotWayRoundEnd::TookTooLong, 2500, 3000000, 90000);
    turns.Leave(b);

    REQUIRE(turns.DescribeWindowAndClear() ==
        "3 bot(s) stopped to look for a way round (the navmesh had no route from her feet 1, too steep 2). 1 found a way round "
        "or a way out, 0 found none, 1 took too long and 0 stopped looking. At most 3 bot(s) were in line at once, and the "
        "longest wait for a turn was 2.5 s. A finished map took 812.5 ms of mapping and held 23000 floors on average. Looks "
        "that did not finish had already spent 3000.0 ms mapping.");

    // The next window starts with the bot still in line.
    REQUIRE(turns.DescribeWindowAndClear() ==
        "0 bot(s) stopped to look for a way round. 0 found a way round or a way out, 0 found none, 0 took too long and 0 "
        "stopped looking. At most 1 bot(s) were in line at once, and the longest wait for a turn was 0.0 s.");
    turns.Leave(c);
}

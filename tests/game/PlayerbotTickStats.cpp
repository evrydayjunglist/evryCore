/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotTickStats.h"

TEST_CASE("Bot tick cost reports once per window", "[playerbots][scale]")
{
    PlayerbotTickStats stats;

    // 50 ms world ticks: the window closes on the tick that reaches a minute.
    uint32 const ticksPerWindow = PLAYERBOT_TICK_STATS_REPORT_MS / 50;
    for (uint32 tick = 1; tick < ticksPerWindow; ++tick)
    {
        stats.AddBotUpdate(40, 0);
        stats.AddBotUpdate(60, 1);
        REQUIRE_FALSE(stats.EndTick(120, 50).has_value());
    }

    stats.AddBotUpdate(40, 0);
    stats.AddBotUpdate(900, 1);
    std::optional<PlayerbotTickReport> report = stats.EndTick(1000, 50);
    REQUIRE(report.has_value());
    REQUIRE(report->Ticks == ticksPerWindow);
    REQUIRE(report->WindowMs == PLAYERBOT_TICK_STATS_REPORT_MS);
    REQUIRE(report->BotUpdates == uint64(ticksPerWindow) * 2);
    REQUIRE(report->MaxBotMicros == 900);
    REQUIRE(report->MaxBotKey == 1);
    REQUIRE(report->MaxTickMicros == 1000);
    REQUIRE(report->MaxWorldDiffMs == 50);
    REQUIRE(report->AverageWorldDiffMs() == Catch::Approx(50.0));
    // One slow tick in 1200 does not reach the slowest twentieth; 120 microseconds sits in the 250 bucket.
    REQUIRE(report->SlowTickMicros == 250);

    // The next window starts empty.
    stats.AddBotUpdate(10, 0);
    REQUIRE_FALSE(stats.EndTick(10, 50).has_value());
}

TEST_CASE("Bot tick cost slowest twentieth follows slow ticks", "[playerbots][scale]")
{
    PlayerbotTickStats stats;
    std::optional<PlayerbotTickReport> report;

    // A tenth of the ticks take 30 ms: the slowest twentieth is inside them.
    for (uint32 tick = 0; !report; ++tick)
        report = stats.EndTick(tick % 10 == 0 ? 30000 : 200, 100);

    REQUIRE(report->SlowTickMicros == 50000);
    REQUIRE(report->MaxTickMicros == 30000);
    REQUIRE(report->AverageTickMs() == Catch::Approx((30000.0 + 9 * 200.0) / 10 / 1000.0));
}

TEST_CASE("Bot tick cost past the top bucket reports the slowest tick", "[playerbots][scale]")
{
    PlayerbotTickStats stats;
    std::optional<PlayerbotTickReport> report;
    while (!report)
        report = stats.EndTick(900000, 1000);

    REQUIRE(report->SlowTickMicros == 900000);
}

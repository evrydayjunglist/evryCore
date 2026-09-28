/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_TICK_STATS_H
#define EVRY_PLAYERBOT_TICK_STATS_H

#include "Define.h"
#include <array>
#include <optional>

// How often the bot brains' share of the world tick is written to the log. A minute is long enough that the line
// costs nothing and short enough to see what adding bots did.
inline constexpr uint32 PLAYERBOT_TICK_STATS_REPORT_MS = 60000;

// What the bot brains cost the world thread over one report window. The brains all run one after another inside the
// world script update, so every microsecond here is a microsecond added to the world tick.
struct PlayerbotTickReport
{
    uint32 WindowMs = 0;
    uint32 Ticks = 0;
    // Every bot update timed in the window, and the time they took together.
    uint64 BotUpdates = 0;
    uint64 BotMicros = 0;
    // The whole playerbot update each tick, bots and the shared work around them.
    uint64 TickMicros = 0;
    uint64 MaxTickMicros = 0;
    // No tick in the slowest twentieth took less than this. It is the top of a bucket, so it reads high, never low.
    uint64 SlowTickMicros = 0;
    // The single slowest bot update, and which bot it was (the caller's key).
    uint64 MaxBotMicros = 0;
    uint32 MaxBotKey = 0;
    // The world tick, as the time the world passes to each update since the one before.
    uint64 WorldDiffMsTotal = 0;
    uint32 MaxWorldDiffMs = 0;

    double AverageTickMs() const { return Ticks ? double(TickMicros) / Ticks / 1000.0 : 0.0; }
    double AverageBotMicros() const { return BotUpdates ? double(BotMicros) / BotUpdates : 0.0; }
    double AverageWorldDiffMs() const { return Ticks ? double(WorldDiffMsTotal) / Ticks : 0.0; }
};

class PlayerbotTickStats
{
public:
    // Tops of the buckets ticks are counted in, in microseconds. A tick slower than the last one counts as the
    // slowest tick of the window.
    static constexpr std::array<uint64, 12> BucketTopsMicros = { 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000,
        100000, 250000, 500000 };

    void AddBotUpdate(uint64 micros, uint32 botKey)
    {
        ++_window.BotUpdates;
        _window.BotMicros += micros;
        if (micros > _window.MaxBotMicros || _window.BotUpdates == 1)
        {
            _window.MaxBotMicros = micros;
            _window.MaxBotKey = botKey;
        }
    }

    // Closes one world tick. Returns the finished window once it covers the report interval.
    std::optional<PlayerbotTickReport> EndTick(uint64 tickMicros, uint32 worldDiffMs)
    {
        ++_window.Ticks;
        _window.TickMicros += tickMicros;
        if (tickMicros > _window.MaxTickMicros)
            _window.MaxTickMicros = tickMicros;
        _window.WorldDiffMsTotal += worldDiffMs;
        if (worldDiffMs > _window.MaxWorldDiffMs)
            _window.MaxWorldDiffMs = worldDiffMs;
        _window.WindowMs += worldDiffMs;

        std::size_t bucket = 0;
        while (bucket < BucketTopsMicros.size() && tickMicros > BucketTopsMicros[bucket])
            ++bucket;
        ++_buckets[bucket];

        if (_window.WindowMs < PLAYERBOT_TICK_STATS_REPORT_MS)
            return std::nullopt;

        PlayerbotTickReport report = _window;
        report.SlowTickMicros = SlowTickTop(report);
        _window = PlayerbotTickReport();
        _buckets = {};
        return report;
    }

private:
    uint64 SlowTickTop(PlayerbotTickReport const& report) const
    {
        // The bucket that holds the tick nineteen twentieths of the way up.
        uint64 const wanted = (uint64(report.Ticks) * 19 + 19) / 20;
        uint64 seen = 0;
        for (std::size_t bucket = 0; bucket < BucketTopsMicros.size(); ++bucket)
        {
            seen += _buckets[bucket];
            if (seen >= wanted)
                return BucketTopsMicros[bucket];
        }
        return report.MaxTickMicros;
    }

    PlayerbotTickReport _window;
    std::array<uint32, BucketTopsMicros.size() + 1> _buckets = {};
};

#endif

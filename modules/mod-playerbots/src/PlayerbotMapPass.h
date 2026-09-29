/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_MAP_PASS_H
#define EVRY_PLAYERBOT_MAP_PASS_H

#include "PlayerbotMapPickTurns.h"
#include "PlayerbotStandSpotMemory.h"
#include "PlayerbotUpdateCost.h"
#include "PlayerbotWayRoundTurns.h"
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

// The bot brains run in passes. The world pass runs on the world thread after every map has updated; with
// Playerbots.MapThreadBrains on, each map also has its own pass, run on that map's thread at the end of its update,
// for the bots standing on it. What a brain shares with the other brains in its pass lives here, so two map threads
// never touch the same line of bots, the same stand spot memory, or the same cost counters. The world pass and the map
// passes never run at the same time: the world thread waits for every map thread before it goes on.
//
// No server dependencies, so it is tested with made-up bots.
struct PlayerbotMapPass
{
    uint32_t MapId = 0;
    uint32_t InstanceId = 0;
    bool World = false;

    // Bots mapping the ground round an obstacle take turns, and share this much mapping time a tick.
    PlayerbotWayRoundTurns WayRoundTurns;
    std::chrono::steady_clock::duration WayRoundSpentThisTick = std::chrono::steady_clock::duration::zero();
    // Stand spots picked in the last few seconds, by bot.
    PlayerbotStandSpotMemory StandSpots;
    // Bots take turns to pick their next map work, a few each tick.
    PlayerbotMapPickTurns MapPickTurns;

    // The steps of the bot update running now, summed over this tick's bot updates, and over the report window.
    PlayerbotUpdateCost UpdateCost;
    PlayerbotUpdateCost TickCost;
    PlayerbotUpdateCost WindowCost;
    uint64_t WindowBotsMicros = 0;
    // How long until another slow bot update, or another slow pass, may be written to the log.
    uint32_t SlowUpdateLogGapMs = 0;
    uint32_t SlowTickLogGapMs = 0;

    // Map passes only. Every bot update of this pass's last run, as (time, bot index), and their time together; the
    // world pass hands them to the tick stats after the maps have updated.
    std::vector<std::pair<uint64_t, uint32_t>> BotUpdates;
    uint64_t TickBotsMicros = 0;
    // How many times this pass ran in the report window, the time those runs took together and at most, and how many
    // bot updates they made.
    uint32_t WindowRuns = 0;
    uint64_t WindowRunMicros = 0;
    uint64_t WindowMaxRunMicros = 0;
    uint64_t WindowBotUpdates = 0;

    // Called once at the start of every run of the pass, with the time since the last one.
    void BeginTick(uint32_t diffMs)
    {
        WayRoundSpentThisTick = std::chrono::steady_clock::duration::zero();
        WayRoundTurns.BeginTick();
        MapPickTurns.BeginTick(diffMs);
        SlowUpdateLogGapMs = SlowUpdateLogGapMs > diffMs ? SlowUpdateLogGapMs - diffMs : 0;
        SlowTickLogGapMs = SlowTickLogGapMs > diffMs ? SlowTickLogGapMs - diffMs : 0;
        TickCost.Clear();
        BotUpdates.clear();
        TickBotsMicros = 0;
    }

    // Closes one run of a map pass that took runMicros.
    void EndRun(uint64_t runMicros)
    {
        ++WindowRuns;
        WindowRunMicros += runMicros;
        if (runMicros > WindowMaxRunMicros)
            WindowMaxRunMicros = runMicros;
        WindowBotUpdates += BotUpdates.size();
        WindowCost.Add(TickCost);
        WindowBotsMicros += TickBotsMicros;
    }

    void ClearWindow()
    {
        WindowRuns = 0;
        WindowRunMicros = 0;
        WindowMaxRunMicros = 0;
        WindowBotUpdates = 0;
        WindowCost.Clear();
        WindowBotsMicros = 0;
    }

    // The pass running on this thread now, or none.
    static PlayerbotMapPass*& Current()
    {
        thread_local PlayerbotMapPass* current = nullptr;
        return current;
    }

    // The world thread's pass. With Playerbots.MapThreadBrains off it is the only one.
    static PlayerbotMapPass& WorldPass()
    {
        static PlayerbotMapPass pass = []
        {
            PlayerbotMapPass world;
            world.World = true;
            return world;
        }();
        return pass;
    }

    // The pass whose shared things a brain on this thread uses. Work outside any pass (a login or map change hook) is on
    // the world thread.
    static PlayerbotMapPass& Here()
    {
        PlayerbotMapPass* current = Current();
        return current ? *current : WorldPass();
    }

    // True on a map thread while it runs its bots' brains.
    static bool OnMapThread()
    {
        PlayerbotMapPass const* current = Current();
        return current && !current->World;
    }
};

#endif

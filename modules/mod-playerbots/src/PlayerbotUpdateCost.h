/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_UPDATE_COST_H
#define EVRY_PLAYERBOT_UPDATE_COST_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

// A bot update slower than this writes one line saying which of its steps took the time.
inline constexpr uint64_t PLAYERBOT_SLOW_UPDATE_MICROS = 50000;
// A whole playerbot update slower than this, all bots together, writes one line saying where that tick went.
inline constexpr uint64_t PLAYERBOT_SLOW_TICK_MICROS = 250000;
// At most one such line of each kind a second for the whole server, so a bot that is slow every tick cannot flood the log.
inline constexpr uint32_t PLAYERBOT_SLOW_UPDATE_LOG_GAP_MS = 1000;

// The steps of a bot update that can cost a lot. Only these are timed; everything else in her update is counted as
// the rest. Keep the names in PlayerbotUpdateCost::Name in the same order.
enum class PlayerbotCostStep : uint8_t
{
    NavmeshRoute,       // a navmesh route question (PathGenerator::CalculatePath)
    Ground,             // a floor or collision look: the plant, the chest-height ray, the headroom ray
    WayRoundMap,        // mapping the ground round an obstacle
    ConnectivityProbe,  // the recovery look at whether the navmesh around her feet joins up
    Approach,           // picking a place to stand beside a target
    ImmediateWorld,     // talk, loot, use, and fights around her
    MapYellow,          // picking the next piece of map work
    FindObjectWork,     // inside a map work pick: quest objects to use
    FindUseItemWork,    // inside a map work pick: creatures to use a quest item on
    FindKillWork,       // inside a map work pick: quest monsters
    FindItemWork,       // inside a map work pick: quest items to loot
    FindTurnIn,         // inside a map work pick: finished quests to hand in
    FindTakeableQuest,  // inside a map work pick: a quest giver with a quest to take in this zone
    StartWork,          // inside a map work pick: starting the walk to what was picked
    Vendor,             // deciding on and finding a vendor
    ObjectiveSearch,    // looking for another spawn for the objective she is working on
    Combat,             // her fight: target checks, swing, and spell choice
    Death,              // dead, ghost, and sitting after death
    TurnInReport,       // explaining why a finished quest's turn-in was not picked
    Presence,           // keeping her session and character logged in
    ServerOrders,       // answering the server's movement orders
    Count
};

// What one bot update spent in each step. Steps can sit inside each other (a navmesh route inside picking a stand
// spot), so each step is the whole time spent inside it, and OutsideMicros is the time in no timed step at all.
class PlayerbotUpdateCost
{
public:
    static constexpr std::size_t StepCount = std::size_t(PlayerbotCostStep::Count);

    static char const* Name(PlayerbotCostStep step)
    {
        static constexpr std::array<char const*, StepCount> names = { "navmesh routes", "ground and collision looks",
            "way-round map", "navmesh join-up probes", "stand spot picks", "looks at the world around her",
            "map work picks", "quest object searches", "use-item creature searches", "quest monster searches",
            "quest item searches", "turn-in searches", "takeable quest searches", "starting picked work", "vendor picks", "same-objective searches", "fight", "death", "turn-in explanations", "staying logged in", "answers to server movement orders" };
        return names[std::size_t(step)];
    }

    void Clear()
    {
        _micros = {};
        _counts = {};
        _topLevelMicros = 0;
        _depth = 0;
    }

    void Enter() { ++_depth; }

    void Leave(PlayerbotCostStep step, uint64_t micros)
    {
        std::size_t const index = std::size_t(step);
        _micros[index] += micros;
        ++_counts[index];
        if (--_depth == 0)
            _topLevelMicros += micros;
    }

    // Adds another update's steps, for a whole tick or a whole report window.
    void Add(PlayerbotUpdateCost const& other)
    {
        for (std::size_t index = 0; index < StepCount; ++index)
        {
            _micros[index] += other._micros[index];
            _counts[index] += other._counts[index];
        }
        _topLevelMicros += other._topLevelMicros;
    }

    uint64_t Micros(PlayerbotCostStep step) const { return _micros[std::size_t(step)]; }
    uint32_t Calls(PlayerbotCostStep step) const { return _counts[std::size_t(step)]; }
    uint64_t OutsideMicros(uint64_t totalMicros) const { return totalMicros > _topLevelMicros ? totalMicros - _topLevelMicros : 0; }

    // "navmesh routes 1180.4 ms (3), ground and collision looks 12.0 ms (410), and 4.1 ms in no timed step", slowest
    // step first, steps that took no time left out.
    std::string Describe(uint64_t totalMicros) const
    {
        std::array<std::size_t, StepCount> order;
        for (std::size_t index = 0; index < StepCount; ++index)
            order[index] = index;
        std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) { return _micros[a] > _micros[b]; });

        std::string text;
        char buffer[128];
        for (std::size_t index : order)
        {
            if (!_counts[index])
                continue;
            std::snprintf(buffer, sizeof(buffer), "%s%s %.1f ms (%u)", text.empty() ? "" : ", ",
                Name(PlayerbotCostStep(index)), double(_micros[index]) / 1000.0, unsigned(_counts[index]));
            text += buffer;
        }
        std::snprintf(buffer, sizeof(buffer), "%s%.1f ms in no timed step", text.empty() ? "" : ", and ",
            double(OutsideMicros(totalMicros)) / 1000.0);
        text += buffer;
        return text;
    }

    // The update being timed now, or none. The world thread runs one bot update at a time, so one is enough, and the
    // walker and the finders can add to it without being handed it.
    static PlayerbotUpdateCost*& Current()
    {
        static PlayerbotUpdateCost* current = nullptr;
        return current;
    }

private:
    std::array<uint64_t, StepCount> _micros = {};
    std::array<uint32_t, StepCount> _counts = {};
    uint64_t _topLevelMicros = 0;
    uint32_t _depth = 0;
};

// Times one step of the bot update being timed. Costs nothing but a pointer check when no update is being timed.
class PlayerbotCostTimer
{
public:
    using Clock = std::chrono::steady_clock;

    explicit PlayerbotCostTimer(PlayerbotCostStep step) : _cost(PlayerbotUpdateCost::Current()), _step(step)
    {
        if (_cost)
        {
            _cost->Enter();
            _start = Clock::now();
        }
    }

    ~PlayerbotCostTimer()
    {
        if (_cost)
            _cost->Leave(_step, uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - _start).count()));
    }

    PlayerbotCostTimer(PlayerbotCostTimer const&) = delete;
    PlayerbotCostTimer& operator=(PlayerbotCostTimer const&) = delete;

private:
    PlayerbotUpdateCost* _cost;
    PlayerbotCostStep _step;
    Clock::time_point _start;
};

#endif

/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_MAP_PICK_TURNS_H
#define EVRY_PLAYERBOT_MAP_PICK_TURNS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

// At most this many bots pick their next map work in one world tick.
inline constexpr std::size_t PLAYERBOT_MAP_PICKS_AT_ONCE = 8;
// Once the picks in a world tick have taken this long, no more start in that tick. The first pick of a tick always runs,
// however long it takes, so a slow pick cannot stop every pick.
inline constexpr uint64_t PLAYERBOT_MAP_PICK_BUDGET_MICROS = 10000;
// A bot in the line that has not asked for her turn for this many world ticks has stopped waiting (she found other
// work, died, or logged out), and the bots behind her move up.
inline constexpr uint64_t PLAYERBOT_MAP_PICK_GONE_TICKS = 2;

// A map-work pick searches every loaded spawn on her map for each objective in her quest log, so it costs more as more
// of the map loads. When many bots want one in the same tick (after the same kind of failure, or all finishing a walk
// together), they take turns: a few pick each tick, and the rest wait in line in the order they asked and pick on a
// later tick. A bot is known by a number that stays the same while she waits. No server dependencies, so it is tested
// with made-up bots.
class PlayerbotMapPickTurns
{
public:
    // Called once at the start of every world tick, with the time since the last one.
    void BeginTick(uint32_t diffMs)
    {
        ++_tick;
        _nowMs += diffMs;
        _givenThisTick.clear();
        _spentMicros = 0;

        for (std::size_t place = 0; place < _line.size();)
        {
            uint32_t const bot = _line[place];
            if (_waiting[bot].LastAskedTick + PLAYERBOT_MAP_PICK_GONE_TICKS < _tick)
            {
                _waiting.erase(bot);
                _line.erase(_line.begin() + place);
                continue;
            }
            ++place;
        }
    }

    // True when she may pick her map work now. Asking again in the same tick after a yes is yes again and costs nothing.
    // A no puts her in line if she is not in it yet; she asks again on her next update.
    bool MayPick(uint32_t bot)
    {
        if (_givenThisTick.count(bot))
            return true;

        bool const open = _givenThisTick.size() < PLAYERBOT_MAP_PICKS_AT_ONCE
            && (_givenThisTick.empty() || _spentMicros < PLAYERBOT_MAP_PICK_BUDGET_MICROS);

        auto waiting = _waiting.find(bot);
        if (waiting != _waiting.end())
        {
            waiting->second.LastAskedTick = _tick;
            if (!open)
                return false;
            for (std::size_t place = 0; place < _line.size() && place < PLAYERBOT_MAP_PICKS_AT_ONCE; ++place)
            {
                if (_line[place] != bot)
                    continue;
                _window.LongestWaitMs = std::max(_window.LongestWaitMs, uint32_t(_nowMs - waiting->second.JoinedMs));
                _waiting.erase(waiting);
                _line.erase(_line.begin() + place);
                Give(bot);
                return true;
            }
            return false;
        }

        // Nobody waiting: straight through while the tick has room.
        if (_line.empty() && open)
        {
            Give(bot);
            return true;
        }

        _line.push_back(bot);
        _waiting[bot] = { _tick, _nowMs };
        ++_window.Waited;
        _window.LongestLine = std::max(_window.LongestLine, uint32_t(_line.size()));
        return false;
    }

    // Time a pick given this tick took.
    void NoteSpent(uint64_t micros) { _spentMicros += micros; }

    bool InLine(uint32_t bot) const { return _waiting.count(bot) != 0; }
    std::size_t Waiting() const { return _line.size(); }

    // "1640 map-work pick(s), at most 8 in one tick. 212 bot(s) had to wait for a turn; at most 90 were in line at once,
    // and the longest wait was 1.4 s." Then the window starts again.
    std::string DescribeWindowAndClear()
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
            "%u map-work pick(s), at most %u in one tick. %u bot(s) had to wait for a turn; at most %u were in line at once, "
            "and the longest wait was %.1f s.",
            unsigned(_window.Picks), unsigned(_window.MostInOneTick), unsigned(_window.Waited), unsigned(_window.LongestLine),
            double(_window.LongestWaitMs) / 1000.0);
        _window = {};
        _window.LongestLine = uint32_t(_line.size());
        return buffer;
    }

private:
    void Give(uint32_t bot)
    {
        _givenThisTick.insert(bot);
        ++_window.Picks;
        _window.MostInOneTick = std::max(_window.MostInOneTick, uint32_t(_givenThisTick.size()));
    }

    struct Wait
    {
        uint64_t LastAskedTick = 0;
        uint64_t JoinedMs = 0;
    };

    struct Window
    {
        uint32_t Picks = 0;
        uint32_t MostInOneTick = 0;
        uint32_t Waited = 0;
        uint32_t LongestLine = 0;
        uint32_t LongestWaitMs = 0;
    };

    std::deque<uint32_t> _line;
    std::unordered_map<uint32_t, Wait> _waiting;
    std::unordered_set<uint32_t> _givenThisTick;
    uint64_t _spentMicros = 0;
    uint64_t _tick = 0;
    uint64_t _nowMs = 0;
    Window _window;
};

#endif

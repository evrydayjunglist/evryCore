/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_WAY_ROUND_TURNS_H
#define EVRY_PLAYERBOT_WAY_ROUND_TURNS_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>

// How many bots at the front of the line map the ground in one world tick. Each gets a slice of the tick's shared
// mapping time, so this is that time divided by one slice.
inline constexpr std::size_t PLAYERBOT_WAY_ROUND_AT_ONCE = 2;
// A bot at the front of the line that has not come for her turn for this many world ticks has stopped looking (her
// walk ended some other way, or she logged out), and the bots behind her move up.
inline constexpr uint64_t PLAYERBOT_WAY_ROUND_GONE_TICKS = 2;

// How a way-round look ended, for the one-minute report.
enum class PlayerbotWayRoundEnd : uint8_t
{
    Found,      // she walks a way round or a way out
    FoundNone,  // the map was finished and had nothing she can walk
    TookTooLong,
    Abandoned   // she left the map, died, was teleported, or the server moved her
};

// Bots that stopped to map the ground round an obstacle take turns, in the order they stopped. Only the bots at the
// front of the line map in a world tick, so the ones being served finish instead of every bot getting a sliver and all
// of them running out of time together. No server dependencies, so it is tested with made-up bots.
class PlayerbotWayRoundTurns
{
public:
    // Called once at the start of every world tick.
    void BeginTick() { ++_tick; }

    // A place at the back of the line. The ticket is never 0, and no two lines hand out the same ticket, so a bot that
    // moved to another map's line cannot hold a ticket that means someone else there.
    uint64_t Join(char const* reason)
    {
        uint64_t const ticket = NextTicket().fetch_add(1);
        _line.push_back(ticket);
        _lastSeen[ticket] = _tick;
        ++_window.Started;
        ++_window.Reasons[reason ? reason : "no reason given"];
        _window.LongestLine = std::max(_window.LongestLine, uint32_t(_line.size()));
        return ticket;
    }

    // She stopped looking. A ticket already gone does nothing.
    void Leave(uint64_t ticket)
    {
        if (!ticket || !_lastSeen.erase(ticket))
            return;
        _line.erase(std::find(_line.begin(), _line.end(), ticket));
    }

    bool InLine(uint64_t ticket) const { return ticket && _lastSeen.count(ticket); }

    // Called by a bot in the line on each of her updates. True when she is at the front of the line and may map this
    // tick; false while she waits. A ticket that is no longer in the line (its bot was dropped for not coming for her
    // turn) is also false; the caller checks InLine and joins again.
    bool IsHerTurn(uint64_t ticket)
    {
        auto seen = _lastSeen.find(ticket);
        if (seen == _lastSeen.end())
            return false;
        seen->second = _tick;

        DropGoneFromFront();
        for (std::size_t place = 0; place < _line.size() && place < PLAYERBOT_WAY_ROUND_AT_ONCE; ++place)
            if (_line[place] == ticket)
                return true;
        return false;
    }

    std::size_t Waiting() const { return _line.size(); }

    // Her look ended. waitedMs is the time she spent in line before her first turn, mappingMicros the world-thread time
    // her map took, and floors how many floors it held.
    void NoteEnd(PlayerbotWayRoundEnd end, uint32_t waitedMs, uint64_t mappingMicros = 0, std::size_t floors = 0)
    {
        ++_window.Ends[std::size_t(end)];
        _window.LongestWaitMs = std::max(_window.LongestWaitMs, waitedMs);
        if (end == PlayerbotWayRoundEnd::Found || end == PlayerbotWayRoundEnd::FoundNone)
        {
            ++_window.FinishedMaps;
            _window.FinishedMappingMicros += mappingMicros;
            _window.FinishedFloors += floors;
        }
        else
            _window.UnfinishedMappingMicros += mappingMicros;
    }

    // Her small map had no way round and ground she can walk carried on past it, so she maps the full size.
    void NoteWidened() { ++_window.Widened; }

    // She stopped to look for a way round but walked to another spot of the same work instead of joining the line.
    void NoteWentElsewhere() { ++_window.WentElsewhere; }

    // "12 bot(s) stopped to look for a way round (the navmesh had no route from her feet 9, too steep 3). 10 found a
    // way round or a way out, 1 found none, 0 took too long and 1 stopped looking. At most 4 bot(s) were in line at
    // once, and the longest wait for a turn was 2.5 s." Then the window starts again.
    std::string DescribeWindowAndClear()
    {
        std::string reasons;
        for (auto const& [reason, count] : _window.Reasons)
        {
            if (!reasons.empty())
                reasons += ", ";
            reasons += reason + " " + std::to_string(count);
        }

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
            "%u bot(s) stopped to look for a way round%s%s%s. %u found a way round or a way out, %u found none, %u took too long "
            "and %u stopped looking. At most %u bot(s) were in line at once, and the longest wait for a turn was %.1f s.",
            unsigned(_window.Started), reasons.empty() ? "" : " (", reasons.c_str(), reasons.empty() ? "" : ")",
            unsigned(_window.Ends[std::size_t(PlayerbotWayRoundEnd::Found)]),
            unsigned(_window.Ends[std::size_t(PlayerbotWayRoundEnd::FoundNone)]),
            unsigned(_window.Ends[std::size_t(PlayerbotWayRoundEnd::TookTooLong)]),
            unsigned(_window.Ends[std::size_t(PlayerbotWayRoundEnd::Abandoned)]),
            unsigned(_window.LongestLine), double(_window.LongestWaitMs) / 1000.0);
        std::string text = buffer;

        if (_window.WentElsewhere)
        {
            std::snprintf(buffer, sizeof(buffer),
                " %u bot(s) walked to another spot of the same work instead of joining the line.", unsigned(_window.WentElsewhere));
            text += buffer;
        }
        if (_window.Widened)
        {
            std::snprintf(buffer, sizeof(buffer), " %u look(s) found nothing on the small map and mapped the full size.",
                unsigned(_window.Widened));
            text += buffer;
        }
        if (_window.FinishedMaps)
        {
            std::snprintf(buffer, sizeof(buffer), " A finished map took %.1f ms of mapping and held %.0f floors on average.",
                double(_window.FinishedMappingMicros) / 1000.0 / _window.FinishedMaps,
                double(_window.FinishedFloors) / _window.FinishedMaps);
            text += buffer;
        }
        if (_window.UnfinishedMappingMicros)
        {
            std::snprintf(buffer, sizeof(buffer), " Looks that did not finish had already spent %.1f ms mapping.",
                double(_window.UnfinishedMappingMicros) / 1000.0);
            text += buffer;
        }

        _window = {};
        _window.LongestLine = uint32_t(_line.size());
        return text;
    }

private:
    static std::atomic<uint64_t>& NextTicket()
    {
        static std::atomic<uint64_t> next{ 1 };
        return next;
    }

    void DropGoneFromFront()
    {
        std::size_t place = 0;
        while (place < _line.size() && place < PLAYERBOT_WAY_ROUND_AT_ONCE)
        {
            uint64_t const ticket = _line[place];
            if (_lastSeen[ticket] + PLAYERBOT_WAY_ROUND_GONE_TICKS < _tick)
            {
                _lastSeen.erase(ticket);
                _line.erase(_line.begin() + place);
                continue;
            }
            ++place;
        }
    }

    struct Window
    {
        uint32_t Started = 0;
        std::map<std::string, uint32_t> Reasons;
        uint32_t Ends[4] = {};
        uint32_t LongestLine = 0;
        uint32_t LongestWaitMs = 0;
        uint32_t Widened = 0;
        uint32_t WentElsewhere = 0;
        uint32_t FinishedMaps = 0;
        uint64_t FinishedMappingMicros = 0;
        uint64_t FinishedFloors = 0;
        uint64_t UnfinishedMappingMicros = 0;
    };

    std::deque<uint64_t> _line;
    // The world tick on which each bot in the line last came for her turn.
    std::unordered_map<uint64_t, uint64_t> _lastSeen;
    uint64_t _tick = 0;
    Window _window;
};

#endif

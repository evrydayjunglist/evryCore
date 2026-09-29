/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EVRY_MOD_PLAYERBOT_STAND_SPOT_MEMORY_H
#define EVRY_MOD_PLAYERBOT_STAND_SPOT_MEMORY_H

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

// Picking a place to stand beside a target asks the navmesh about up to eight sides of it, about a millisecond a pick.
// A bot standing still asks the same question again and again: her look around once a second, a walk refused at its
// start and tried again, the finders looking at the same targets in the next pick. The answer only depends on where
// her feet are, where the target is, and the navmesh, so for a few seconds after a pick the same question from the
// same feet about the same target, not moved, gets the same answer without asking the navmesh again.
//
// It also counts, for the one-minute report, how many picks there were, how many were answered from memory, and how
// many sides were asked about or not needed. It has no server dependencies so it can be tested with made-up places.
class PlayerbotStandSpotMemory
{
public:
    // How long an answer is kept. The navmesh does not change, but a door or a gameobject on the way can.
    static constexpr uint32_t KeepMs = 3000;
    // Her feet or the target moved further than this since the answer: ask again. Small enough that the route from
    // the new feet cannot pick a different side for any reason but a near tie.
    static constexpr float SameFeetYards = 0.5f;
    static constexpr float SameTargetYards = 0.25f;
    // Answers kept for each bot, the oldest replaced first. A pick looks at a handful of targets.
    static constexpr std::size_t SlotsPerBot = 8;

    struct Place
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
    };

    struct Question
    {
        uint64_t Bot = 0;
        uint64_t TargetLow = 0;
        uint64_t TargetHigh = 0;
        uint32_t MapId = 0;
        float StandDistance = 0.0f;
        Place Feet;
        Place Target;
    };

    struct Answer
    {
        bool Found = false;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Orientation = 0.0f;
    };

    bool Recall(Question const& question, uint32_t nowMs, Answer& out)
    {
        ++_asked;
        auto const bot = _bots.find(question.Bot);
        if (bot == _bots.end())
            return false;
        for (Slot const& slot : bot->second.Slots)
        {
            if (!slot.Used || !Same(slot.Asked, question) || nowMs - slot.AtMs > KeepMs)
                continue;
            out = slot.Given;
            ++_recalled;
            return true;
        }
        return false;
    }

    void Remember(Question const& question, uint32_t nowMs, Answer const& answer)
    {
        Slots& slots = _bots[question.Bot];
        Slot& slot = slots.Slots[slots.Next];
        slots.Next = (slots.Next + 1) % SlotsPerBot;
        slot.Used = true;
        slot.Asked = question;
        slot.Given = answer;
        slot.AtMs = nowMs;
    }

    // She logged out, changed map, or was moved by the server.
    void Forget(uint64_t bot) { _bots.erase(bot); }

    void NoteSides(uint32_t asked, uint32_t notNeeded)
    {
        _sidesAsked += asked;
        _sidesNotNeeded += notNeeded;
    }

    // One sentence for the minute report, then it starts counting again.
    std::string DescribeWindowAndClear()
    {
        char buffer[320];
        std::snprintf(buffer, sizeof(buffer), "%llu stand spot pick(s), %llu answered from the last few seconds' picks "
            "without asking the navmesh; the others asked about %llu side(s) and did not need to ask about %llu that could "
            "not have a shorter route.", static_cast<unsigned long long>(_asked),
            static_cast<unsigned long long>(_recalled), static_cast<unsigned long long>(_sidesAsked),
            static_cast<unsigned long long>(_sidesNotNeeded));
        std::string const text = buffer;
        _asked = 0;
        _recalled = 0;
        _sidesAsked = 0;
        _sidesNotNeeded = 0;
        return text;
    }

    std::size_t BotsRemembered() const { return _bots.size(); }

private:
    struct Slot
    {
        bool Used = false;
        Question Asked;
        Answer Given;
        uint32_t AtMs = 0;
    };

    struct Slots
    {
        std::array<Slot, SlotsPerBot> Slots;
        std::size_t Next = 0;
    };

    static float DistanceSq(Place const& a, Place const& b)
    {
        float const dx = a.X - b.X;
        float const dy = a.Y - b.Y;
        float const dz = a.Z - b.Z;
        return dx * dx + dy * dy + dz * dz;
    }

    static bool Same(Question const& a, Question const& b)
    {
        return a.TargetLow == b.TargetLow && a.TargetHigh == b.TargetHigh && a.MapId == b.MapId
            && std::fabs(a.StandDistance - b.StandDistance) < 0.01f
            && DistanceSq(a.Feet, b.Feet) <= SameFeetYards * SameFeetYards
            && DistanceSq(a.Target, b.Target) <= SameTargetYards * SameTargetYards;
    }

    std::unordered_map<uint64_t, Slots> _bots;
    uint64_t _asked = 0;
    uint64_t _recalled = 0;
    uint64_t _sidesAsked = 0;
    uint64_t _sidesNotNeeded = 0;
};

#endif

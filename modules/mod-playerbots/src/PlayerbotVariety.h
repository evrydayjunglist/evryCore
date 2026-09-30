/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_VARIETY_H
#define EVRY_PLAYERBOT_VARIETY_H

#include "Define.h"

// Small differences between bots, so a crowd of them does not stand on one spot or act on one tick. Every choice here
// comes from a hash of who is choosing and what about, not from a random roll each tick: the same bot asking the same
// question gets the same answer, so she does not change her mind while she walks.

// She stands up to this far round the target from the side she comes from, either way.
inline constexpr float PLAYERBOT_STAND_SPREAD_RADIANS = 3.14159265358979323846f / 3.0f;

// She stands up to this many yards farther from the target than the caller's stand distance.
inline constexpr float PLAYERBOT_STAND_EXTRA_YARDS = 2.0f;

// A spot this close to another player she can see standing there is taken.
inline constexpr float PLAYERBOT_STAND_CROWD_YARDS = 1.0f;

// How long she waits, in milliseconds, at the points where every bot used to wait the same time.
inline constexpr uint32 PLAYERBOT_QUEST_CHAIN_PAUSE_MIN_MS = 500;
inline constexpr uint32 PLAYERBOT_QUEST_CHAIN_PAUSE_MAX_MS = 2000;
inline constexpr uint32 PLAYERBOT_RELEASE_WAIT_MIN_MS = 2000;
inline constexpr uint32 PLAYERBOT_RELEASE_WAIT_MAX_MS = 6000;
inline constexpr uint32 PLAYERBOT_INVITE_ANSWER_MIN_MS = 1000;
inline constexpr uint32 PLAYERBOT_INVITE_ANSWER_MAX_MS = 3000;
inline constexpr uint32 PLAYERBOT_STAND_UP_AFTER_REST_MIN_MS = 250;
inline constexpr uint32 PLAYERBOT_STAND_UP_AFTER_REST_MAX_MS = 3000;
inline constexpr uint32 PLAYERBOT_LOOK_AROUND_MIN_MS = 800;
inline constexpr uint32 PLAYERBOT_LOOK_AROUND_MAX_MS = 1400;

// The waits a bot varies. Each one is its own stream, so rolling one does not change the next of another.
enum class PlayerbotWaitKind : uint32
{
    QuestChainPause = 1,
    ReleaseSpirit,
    InviteAnswer,
    StandUpAfterRest,
    LookAround
};

inline uint64 PlayerbotMix(uint64 value)
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

// A number from 0 up to but not including 1, from the hash.
inline float PlayerbotUnitFraction(uint64 hash)
{
    return float(hash >> 40) / float(uint64(1) << 24);
}

struct PlayerbotStandPreference
{
    // Added to the direction from the target to her, in radians.
    float AngleOffset = 0.0f;
    // Added to the caller's stand distance, in yards.
    float ExtraYards = 0.0f;
};

// Where round a target this bot likes to stand. The same bot and target always give the same answer.
inline PlayerbotStandPreference PickPlayerbotStandPreference(uint64 bot, uint64 targetLow, uint64 targetHigh)
{
    uint64 const seed = PlayerbotMix(PlayerbotMix(bot) ^ PlayerbotMix(targetLow ^ PlayerbotMix(targetHigh)));
    PlayerbotStandPreference preference;
    preference.AngleOffset = (PlayerbotUnitFraction(seed) * 2.0f - 1.0f) * PLAYERBOT_STAND_SPREAD_RADIANS;
    preference.ExtraYards = PlayerbotUnitFraction(PlayerbotMix(seed)) * PLAYERBOT_STAND_EXTRA_YARDS;
    return preference;
}

// How long this bot waits this time. `count` is how many waits of this kind she has had, so the next one differs.
inline uint32 PickPlayerbotWait(uint64 bot, PlayerbotWaitKind kind, uint32 count, uint32 minMs, uint32 maxMs)
{
    if (maxMs <= minMs)
        return minMs;
    uint64 const hash = PlayerbotMix(PlayerbotMix(bot) ^ PlayerbotMix((uint64(kind) << 32) | count));
    return minMs + uint32(hash % uint64(maxMs - minMs + 1));
}

#endif

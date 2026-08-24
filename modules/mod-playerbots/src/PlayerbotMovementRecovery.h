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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef EVRY_MOD_PLAYERBOT_MOVEMENT_RECOVERY_H
#define EVRY_MOD_PLAYERBOT_MOVEMENT_RECOVERY_H

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <unordered_set>

enum class PlayerbotRecoveryGoalKind : std::uint8_t
{
    None,
    Creature,
    GameObject,
    Corpse,
    MapQuest,
    MapCreatureObjective,
    MapGameObjectObjective,
    MapUseItemObjective,
    MapItemObjective,
    Graveyard
};

struct PlayerbotRecoveryGoal
{
    PlayerbotRecoveryGoalKind Kind = PlayerbotRecoveryGoalKind::None;
    std::uint32_t MapId = 0;
    std::uint64_t Primary = 0;
    std::uint64_t Secondary = 0;

    static PlayerbotRecoveryGoal ForObject(PlayerbotRecoveryGoalKind kind, std::uint32_t mapId,
        std::uint64_t rawLow, std::uint64_t rawHigh)
    {
        return { kind, mapId, rawLow, rawHigh };
    }

    static PlayerbotRecoveryGoal ForObjective(PlayerbotRecoveryGoalKind kind, std::uint32_t mapId,
        std::uint32_t questId, std::uint64_t objective)
    {
        return { kind, mapId, questId, objective };
    }

    bool Empty() const { return Kind == PlayerbotRecoveryGoalKind::None; }

    bool operator==(PlayerbotRecoveryGoal const& other) const
    {
        return Kind == other.Kind && MapId == other.MapId && Primary == other.Primary && Secondary == other.Secondary;
    }

    bool operator!=(PlayerbotRecoveryGoal const& other) const { return !(*this == other); }
};

inline char const* PlayerbotRecoveryGoalKindName(PlayerbotRecoveryGoalKind kind)
{
    switch (kind)
    {
        case PlayerbotRecoveryGoalKind::None:
            return "position";
        case PlayerbotRecoveryGoalKind::Creature:
            return "creature";
        case PlayerbotRecoveryGoalKind::GameObject:
            return "gameobject";
        case PlayerbotRecoveryGoalKind::Corpse:
            return "corpse";
        case PlayerbotRecoveryGoalKind::MapQuest:
            return "map quest";
        case PlayerbotRecoveryGoalKind::MapCreatureObjective:
            return "map creature objective";
        case PlayerbotRecoveryGoalKind::MapGameObjectObjective:
            return "map gameobject objective";
        case PlayerbotRecoveryGoalKind::MapUseItemObjective:
            return "map use-item objective";
        case PlayerbotRecoveryGoalKind::MapItemObjective:
            return "map item objective";
        case PlayerbotRecoveryGoalKind::Graveyard:
            return "graveyard";
    }

    return "unknown";
}

class PlayerbotFaceRecovery
{
public:
    static constexpr float ConfirmedRejoinYards = 8.0f;
    static constexpr float ConfirmedDestProgressYards = 1.0f;
    static constexpr float NewGroundCellYards = 2.0f;
    static constexpr float MaximumTravelWithoutNewGroundYards = 24.0f;

    void Reset()
    {
        _active = false;
        _rejoining = false;
        _jumpAttempted = false;
        _episodeYards = 0.0f;
        _travelAtNewGround = 0.0f;
        _rejoinYards = 0.0f;
        _rejoinStartDestDistance = 0.0f;
        _rejoinFoundNewGround = false;
        _connectivityChecked = false;
        _visitedGround.clear();
    }

    void Begin(float x, float y)
    {
        if (_active)
            return;

        _active = true;
        _jumpAttempted = false;
        _visitedGround.insert(GroundCell(x, y));
    }

    void BeginMmapRejoin(float destinationDistance, float x, float y)
    {
        Begin(x, y);
        _rejoining = true;
        _rejoinYards = 0.0f;
        _rejoinStartDestDistance = destinationDistance;
        _rejoinFoundNewGround = false;
    }

    // Returns true when this refusal ended a provisional mmap rejoin.
    bool Refuse()
    {
        bool const repeatedDuringRejoin = _active && _rejoining;
        _rejoining = false;
        _rejoinYards = 0.0f;
        _rejoinStartDestDistance = 0.0f;
        _rejoinFoundNewGround = false;
        return repeatedDuringRejoin;
    }

    // Returns true only after mmap has demonstrated that it really left the local face.
    bool AdvanceMmap(float yards, float destinationDistance, float x, float y)
    {
        if (!_active || !_rejoining)
            return false;

        bool const alreadyVisited = HasVisitedGround(x, y);
        Advance(yards, x, y);
        if (!alreadyVisited)
            _rejoinFoundNewGround = true;
        _rejoinYards += std::max(0.0f, yards);
        bool const madeDestProgress = destinationDistance + ConfirmedDestProgressYards < _rejoinStartDestDistance;
        return _rejoinYards >= ConfirmedRejoinYards && madeDestProgress && _rejoinFoundNewGround;
    }

    void Advance(float yards, float x, float y)
    {
        if (!_active)
            return;

        _episodeYards += std::max(0.0f, yards);
        if (_visitedGround.insert(GroundCell(x, y)).second)
            _travelAtNewGround = _episodeYards;
    }

    bool Active() const { return _active; }
    bool Rejoining() const { return _rejoining; }
    bool Exhausted() const { return StalledYards() >= MaximumTravelWithoutNewGroundYards; }
    float EpisodeYards() const { return _episodeYards; }
    float StalledYards() const { return std::max(0.0f, _episodeYards - _travelAtNewGround); }
    std::size_t VisitedGroundCells() const { return _visitedGround.size(); }
    float RejoinYards() const { return _rejoinYards; }
    bool HasVisitedGround(float x, float y) const { return _visitedGround.find(GroundCell(x, y)) != _visitedGround.end(); }
    bool JumpAttempted() const { return _jumpAttempted; }
    void MarkJumpAttempted() { _jumpAttempted = true; }
    bool ConnectivityChecked() const { return _connectivityChecked; }
    void MarkConnectivityChecked() { _connectivityChecked = true; }

private:
    static std::uint64_t GroundCell(float x, float y)
    {
        std::int32_t const cellX = std::int32_t(std::floor(x / NewGroundCellYards));
        std::int32_t const cellY = std::int32_t(std::floor(y / NewGroundCellYards));
        return (std::uint64_t(std::uint32_t(cellX)) << 32) | std::uint32_t(cellY);
    }

    bool _active = false;
    bool _rejoining = false;
    bool _jumpAttempted = false;
    float _episodeYards = 0.0f;
    float _travelAtNewGround = 0.0f;
    float _rejoinYards = 0.0f;
    float _rejoinStartDestDistance = 0.0f;
    bool _rejoinFoundNewGround = false;
    bool _connectivityChecked = false;
    std::unordered_set<std::uint64_t> _visitedGround;
};

#endif

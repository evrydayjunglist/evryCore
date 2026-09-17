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

#ifndef EVRY_MOD_PLAYERBOT_WALK_MAP_H
#define EVRY_MOD_PLAYERBOT_WALK_MAP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// How one step between neighbouring spots of a walk map turned out.
enum class PlayerbotWalkMapStep : std::uint8_t
{
    Untried,
    Legal,
    // Nothing to stand on within the most she may climb: a face, or a drop with no floor under it.
    NoFloor,
    SteepUp,
    TooFarDown,
    // Her chest-height ray met the world's own geometry.
    StaticCollision,
    // Her chest-height ray met a game object.
    DynamicCollision,
    InvalidPosition,
    // That ground is not loaded, so the map knows nothing about it.
    NotLoaded,
    // The step leaves the area the map covers.
    OutsideMap
};

inline constexpr std::size_t PLAYERBOT_WALK_MAP_STEP_KINDS = 10;

// One letter per step result, for the picture page.
inline char PlayerbotWalkMapStepLetter(PlayerbotWalkMapStep step)
{
    switch (step)
    {
        case PlayerbotWalkMapStep::Untried:
            return '.';
        case PlayerbotWalkMapStep::Legal:
            return 'L';
        case PlayerbotWalkMapStep::NoFloor:
            return 'F';
        case PlayerbotWalkMapStep::SteepUp:
            return 'U';
        case PlayerbotWalkMapStep::TooFarDown:
            return 'D';
        case PlayerbotWalkMapStep::StaticCollision:
            return 'S';
        case PlayerbotWalkMapStep::DynamicCollision:
            return 'G';
        case PlayerbotWalkMapStep::InvalidPosition:
            return 'I';
        case PlayerbotWalkMapStep::NotLoaded:
            return 'N';
        case PlayerbotWalkMapStep::OutsideMap:
            return 'E';
    }

    return '?';
}

// The step found a floor to put her feet on, whether or not she may take it.
inline bool PlayerbotWalkMapStepPlanted(PlayerbotWalkMapStep step)
{
    return step == PlayerbotWalkMapStep::Legal
        || step == PlayerbotWalkMapStep::SteepUp
        || step == PlayerbotWalkMapStep::TooFarDown
        || step == PlayerbotWalkMapStep::StaticCollision
        || step == PlayerbotWalkMapStep::DynamicCollision;
}

// Neighbours in the order north (+x), north-east, east (-y), south-east, south, south-west, west (+y), north-west.
inline constexpr std::array<std::array<std::int32_t, 2>, 8> PLAYERBOT_WALK_MAP_DIRECTIONS =
{{
    {{ 1, 0 }}, {{ 1, -1 }}, {{ 0, -1 }}, {{ -1, -1 }}, {{ -1, 0 }}, {{ -1, 1 }}, {{ 0, 1 }}, {{ 1, 1 }}
}};

struct PlayerbotWalkMapStepResult
{
    PlayerbotWalkMapStep Step = PlayerbotWalkMapStep::Untried;
    // Where the step put her feet, when it found a floor.
    float Z = 0.0f;
};

// What a walk map asks of the world. The server answers with the rules her walk uses; tests answer with made-up ground.
class PlayerbotWalkMapWorld
{
public:
    virtual ~PlayerbotWalkMapWorld() = default;

    virtual bool IsLoaded(float x, float y) = 0;
    // One grounded step from these feet to (x, y), planted and judged the way a walk heartbeat is.
    virtual PlayerbotWalkMapStepResult Step(float fromX, float fromY, float fromZ, float toX, float toY) = 0;
    // The height her feet would be planted at on (x, y) when looking down from z, if there is any floor.
    virtual bool GroundBelow(float x, float y, float z, float& outZ) = 0;
};

struct PlayerbotWalkMapSettings
{
    // Her feet. Every spot of the map is a whole number of steps north and west of them.
    float OriginX = 0.0f;
    float OriginY = 0.0f;
    float OriginZ = 0.0f;
    // Yards between neighbouring spots.
    float Spacing = 1.0f;
    float Radius = 60.0f;
    float MaxClimbDegrees = 35.0f;
    float MaxDropYards = 2.0f;
    // Floors in one spot closer together than this are the same floor.
    float LayerYards = 1.0f;
    std::size_t MaxSpots = 500000;
};

// One floor at one spot of the map. A spot can hold several floors, such as a cave floor under a hill.
struct PlayerbotWalkMapSpot
{
    PlayerbotWalkMapSpot()
    {
        Steps.fill(PlayerbotWalkMapStep::Untried);
        StepSpot.fill(-1);
    }

    // Whole steps north (+x) and west (+y) of her feet.
    std::int32_t I = 0;
    std::int32_t J = 0;
    float Z = 0.0f;
    std::int32_t NextInColumn = -1;
    // She can walk here from her feet.
    bool Reached = false;
    // She can walk from here to her feet.
    bool Returns = false;
    // Every step out of this floor has been tried.
    bool Expanded = false;
    std::array<PlayerbotWalkMapStep, 8> Steps;
    // The floor each planted step landed on, or -1.
    std::array<std::int32_t, 8> StepSpot;
};

struct PlayerbotWalkMapSummary
{
    std::size_t Spots = 0;
    std::size_t Reached = 0;
    std::size_t TwoWay = 0;
    // She can walk there but not back.
    std::size_t OneWayOut = 0;
    // She could walk from there to her feet but not there.
    std::size_t OneWayIn = 0;
    // Reached floors next to the edge of the map.
    std::size_t ReachedEdge = 0;
    std::size_t TwoWayEdge = 0;
    // Steps from reached floors into ground that is not loaded.
    std::size_t NotLoadedSteps = 0;
    // Refused steps from reached floors onto a spot with no reached floor at all, by result.
    std::array<std::size_t, PLAYERBOT_WALK_MAP_STEP_KINDS> BorderSteps{};
};

// Every floor she can walk to from her feet with one step at a time between neighbouring spots, and every floor from
// which she could walk back to them. Steps go only to the eight neighbours, so the map can miss a way that only a
// walk at another angle would find.
class PlayerbotWalkMap
{
public:
    explicit PlayerbotWalkMap(PlayerbotWalkMapSettings const& settings) : _settings(settings)
    {
        float const spacing = _settings.Spacing > 0.01f ? _settings.Spacing : 0.01f;
        _settings.Spacing = spacing;
        _half = std::int32_t(std::floor(_settings.Radius / spacing));
        if (_half < 1)
            _half = 1;
        _width = 2 * _half + 1;
        _columns.assign(std::size_t(_width) * std::size_t(_width), -1);
        _climbPerYard = std::tan(_settings.MaxClimbDegrees * 3.14159265358979f / 180.0f);
    }

    // Tries the steps out of up to maxSpots floors: first everything she can walk to, then everything that walks back
    // to her. True once the map is finished.
    bool Advance(PlayerbotWalkMapWorld& world, std::size_t maxSpots)
    {
        if (!_started)
        {
            _started = true;
            std::int32_t const start = AddSpot(0, 0, _settings.OriginZ);
            if (start < 0)
            {
                _phase = Phase::Done;
                return true;
            }
            _spots[start].Reached = true;
            _spots[start].Returns = true;
            _outward.push_back(start);
            _inward.push_back(start);
        }

        std::size_t done = 0;
        while (done < maxSpots && _phase != Phase::Done)
        {
            if (_phase == Phase::Outward)
            {
                if (_outward.empty())
                {
                    _phase = Phase::Inward;
                    continue;
                }

                std::int32_t const spot = _outward.front();
                _outward.pop_front();
                ExpandOutward(world, spot);
            }
            else
            {
                if (_inward.empty())
                {
                    _phase = Phase::Done;
                    break;
                }

                std::int32_t const spot = _inward.front();
                _inward.pop_front();
                ExpandInward(world, spot);
            }

            ++done;
        }

        return _phase == Phase::Done;
    }

    bool Finished() const { return _phase == Phase::Done; }
    bool HitSpotLimit() const { return _hitSpotLimit; }
    PlayerbotWalkMapSettings const& Settings() const { return _settings; }
    std::int32_t HalfWidth() const { return _half; }
    std::vector<PlayerbotWalkMapSpot> const& Spots() const { return _spots; }
    float WorldX(std::int32_t i) const { return _settings.OriginX + float(i) * _settings.Spacing; }
    float WorldY(std::int32_t j) const { return _settings.OriginY + float(j) * _settings.Spacing; }

    bool InMap(std::int32_t i, std::int32_t j) const
    {
        if (i < -_half || i > _half || j < -_half || j > _half)
            return false;

        float const x = float(i) * _settings.Spacing;
        float const y = float(j) * _settings.Spacing;
        return x * x + y * y <= _settings.Radius * _settings.Radius;
    }

    // The floor at this spot within LayerYards of z, or -1.
    std::int32_t FindSpot(std::int32_t i, std::int32_t j, float z) const
    {
        if (i < -_half || i > _half || j < -_half || j > _half)
            return -1;

        for (std::int32_t spot = _columns[ColumnIndex(i, j)]; spot >= 0; spot = _spots[spot].NextInColumn)
            if (std::fabs(_spots[spot].Z - z) <= _settings.LayerYards)
                return spot;

        return -1;
    }

    PlayerbotWalkMapSummary Summarize() const
    {
        PlayerbotWalkMapSummary summary;
        for (PlayerbotWalkMapSpot const& spot : _spots)
        {
            ++summary.Spots;
            if (spot.Reached && spot.Returns)
                ++summary.TwoWay;
            else if (spot.Reached)
                ++summary.OneWayOut;
            else if (spot.Returns)
                ++summary.OneWayIn;

            if (!spot.Reached)
                continue;

            ++summary.Reached;
            bool edge = false;
            for (std::size_t d = 0; d < PLAYERBOT_WALK_MAP_DIRECTIONS.size(); ++d)
            {
                PlayerbotWalkMapStep const step = spot.Steps[d];
                switch (step)
                {
                    case PlayerbotWalkMapStep::Untried:
                    case PlayerbotWalkMapStep::Legal:
                        break;
                    case PlayerbotWalkMapStep::OutsideMap:
                        edge = true;
                        break;
                    case PlayerbotWalkMapStep::NotLoaded:
                        ++summary.NotLoadedSteps;
                        break;
                    default:
                        if (!ColumnHasReachedFloor(spot.I + PLAYERBOT_WALK_MAP_DIRECTIONS[d][0], spot.J + PLAYERBOT_WALK_MAP_DIRECTIONS[d][1]))
                            ++summary.BorderSteps[std::size_t(step)];
                        break;
                }
            }

            if (edge)
            {
                ++summary.ReachedEdge;
                if (spot.Returns)
                    ++summary.TwoWayEdge;
            }
        }

        return summary;
    }

private:
    enum class Phase
    {
        Outward,
        Inward,
        Done
    };

    std::size_t ColumnIndex(std::int32_t i, std::int32_t j) const
    {
        return std::size_t(i + _half) * std::size_t(_width) + std::size_t(j + _half);
    }

    bool ColumnHasReachedFloor(std::int32_t i, std::int32_t j) const
    {
        if (i < -_half || i > _half || j < -_half || j > _half)
            return false;

        for (std::int32_t spot = _columns[ColumnIndex(i, j)]; spot >= 0; spot = _spots[spot].NextInColumn)
            if (_spots[spot].Reached)
                return true;

        return false;
    }

    std::int32_t AddSpot(std::int32_t i, std::int32_t j, float z)
    {
        if (_spots.size() >= _settings.MaxSpots)
        {
            _hitSpotLimit = true;
            return -1;
        }

        std::size_t const column = ColumnIndex(i, j);
        PlayerbotWalkMapSpot spot;
        spot.I = i;
        spot.J = j;
        spot.Z = z;
        spot.NextInColumn = _columns[column];
        _spots.push_back(spot);
        _columns[column] = std::int32_t(_spots.size() - 1);
        return _columns[column];
    }

    std::int32_t FindOrAddSpot(std::int32_t i, std::int32_t j, float z)
    {
        std::int32_t const found = FindSpot(i, j, z);
        return found >= 0 ? found : AddSpot(i, j, z);
    }

    float StepRun(std::size_t direction) const
    {
        return direction % 2 ? _settings.Spacing * 1.41421356f : _settings.Spacing;
    }

    void ExpandOutward(PlayerbotWalkMapWorld& world, std::int32_t from)
    {
        // Adding a floor can move the spots in memory, so copy what the steps need first.
        std::int32_t const i = _spots[from].I;
        std::int32_t const j = _spots[from].J;
        float const x = WorldX(i);
        float const y = WorldY(j);
        float const z = _spots[from].Z;
        _spots[from].Expanded = true;

        for (std::size_t d = 0; d < PLAYERBOT_WALK_MAP_DIRECTIONS.size(); ++d)
        {
            std::int32_t const toI = i + PLAYERBOT_WALK_MAP_DIRECTIONS[d][0];
            std::int32_t const toJ = j + PLAYERBOT_WALK_MAP_DIRECTIONS[d][1];
            if (!InMap(toI, toJ))
            {
                _spots[from].Steps[d] = PlayerbotWalkMapStep::OutsideMap;
                continue;
            }

            float const toX = WorldX(toI);
            float const toY = WorldY(toJ);
            if (!world.IsLoaded(toX, toY))
            {
                _spots[from].Steps[d] = PlayerbotWalkMapStep::NotLoaded;
                continue;
            }

            PlayerbotWalkMapStepResult result = world.Step(x, y, z, toX, toY);
            if (result.Step == PlayerbotWalkMapStep::Untried)
                result.Step = PlayerbotWalkMapStep::InvalidPosition;
            _spots[from].Steps[d] = result.Step;
            if (!PlayerbotWalkMapStepPlanted(result.Step))
                continue;

            std::int32_t const to = FindOrAddSpot(toI, toJ, result.Z);
            _spots[from].StepSpot[d] = to;
            if (result.Step == PlayerbotWalkMapStep::Legal && to >= 0 && !_spots[to].Reached)
            {
                _spots[to].Reached = true;
                _outward.push_back(to);
            }
        }
    }

    // Finds the floors next to this one that she could step onto it from.
    void ExpandInward(PlayerbotWalkMapWorld& world, std::int32_t to)
    {
        std::int32_t const i = _spots[to].I;
        std::int32_t const j = _spots[to].J;
        float const z = _spots[to].Z;

        for (std::size_t d = 0; d < PLAYERBOT_WALK_MAP_DIRECTIONS.size(); ++d)
        {
            std::int32_t const fromI = i + PLAYERBOT_WALK_MAP_DIRECTIONS[d][0];
            std::int32_t const fromJ = j + PLAYERBOT_WALK_MAP_DIRECTIONS[d][1];
            if (!InMap(fromI, fromJ))
                continue;

            float const fromX = WorldX(fromI);
            float const fromY = WorldY(fromJ);
            if (!world.IsLoaded(fromX, fromY))
                continue;

            // A floor she could step onto this one from is at most one drop above it and one climb below it.
            float const lowest = z - _climbPerYard * StepRun(d) - _settings.LayerYards;
            float probe = z + _settings.MaxDropYards + _settings.LayerYards;
            for (std::int32_t layer = 0; layer < 4; ++layer)
            {
                float floorZ = 0.0f;
                if (!world.GroundBelow(fromX, fromY, probe, floorZ) || floorZ < lowest)
                    break;

                TryInwardStep(world, to, (d + 4) % 8, fromI, fromJ, floorZ);
                probe = std::min(floorZ, probe) - _settings.LayerYards;
            }
        }
    }

    // Is there a legal step from the floor at (fromI, fromJ, floorZ), in this direction, onto spot to?
    void TryInwardStep(PlayerbotWalkMapWorld& world, std::int32_t to, std::size_t direction, std::int32_t fromI,
        std::int32_t fromJ, float floorZ)
    {
        std::int32_t from = FindSpot(fromI, fromJ, floorZ);
        bool legal = false;
        if (from >= 0 && _spots[from].Expanded)
            legal = _spots[from].Steps[direction] == PlayerbotWalkMapStep::Legal && _spots[from].StepSpot[direction] == to;
        else
        {
            float const fromZ = from >= 0 ? _spots[from].Z : floorZ;
            PlayerbotWalkMapStepResult const result = world.Step(WorldX(fromI), WorldY(fromJ), fromZ, WorldX(_spots[to].I),
                WorldY(_spots[to].J));
            legal = result.Step == PlayerbotWalkMapStep::Legal && FindSpot(_spots[to].I, _spots[to].J, result.Z) == to;
        }

        if (!legal)
            return;

        if (from < 0)
            from = AddSpot(fromI, fromJ, floorZ);
        if (from < 0 || _spots[from].Returns)
            return;

        _spots[from].Returns = true;
        _inward.push_back(from);
    }

    PlayerbotWalkMapSettings _settings;
    std::int32_t _half = 0;
    std::int32_t _width = 0;
    float _climbPerYard = 0.0f;
    std::vector<std::int32_t> _columns;
    std::vector<PlayerbotWalkMapSpot> _spots;
    std::deque<std::int32_t> _outward;
    std::deque<std::int32_t> _inward;
    Phase _phase = Phase::Outward;
    bool _started = false;
    bool _hitSpotLimit = false;
};

#endif

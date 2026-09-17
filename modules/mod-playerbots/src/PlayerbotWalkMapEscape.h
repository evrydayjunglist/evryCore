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

#ifndef EVRY_MOD_PLAYERBOT_WALK_MAP_ESCAPE_H
#define EVRY_MOD_PLAYERBOT_WALK_MAP_ESCAPE_H

#include "PlayerbotWalkMap.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// A way round an obstruction: the floors of a walk map she would walk, her feet first, to a spot closer to where she is
// going.
struct PlayerbotWalkMapWayRound
{
    std::vector<std::int32_t> Floors;
    std::int32_t Target = -1;
    // How much closer to the destination that spot is than her feet.
    float Gain = 0.0f;
    // Yards of walking to get there.
    float Yards = 0.0f;
    // She can walk back from that spot to her feet.
    bool CanWalkBack = false;

    bool Found() const { return Target >= 0 && Floors.size() >= 2; }
};

struct PlayerbotWalkMapWayRoundSettings
{
    float DestinationX = 0.0f;
    float DestinationY = 0.0f;
    float DestinationZ = 0.0f;
    // A spot is only worth walking to when it is at least this much closer to the destination than her feet are.
    float MinimumGain = 5.0f;
    // Every yard she would have to walk counts this much against a spot, so a small gain far away loses to a good one
    // close by.
    float YardsWeight = 0.25f;
    // Ways round that end near each other are the same way round. Keep the best and look further for the others.
    float SpreadYards = 8.0f;
    // How many ways round to hand back, best first.
    std::size_t Ways = 3;
};

// Every way round, best first. The best spot is the one closest to the destination once the walk to it is counted, and
// ground she can walk back from wins over ground she cannot. Empty when nothing she can reach is closer.
inline std::vector<PlayerbotWalkMapWayRound> FindPlayerbotWalkMapWaysRound(PlayerbotWalkMap const& map,
    PlayerbotWalkMapWayRoundSettings const& settings)
{
    std::vector<PlayerbotWalkMapSpot> const& spots = map.Spots();
    std::vector<PlayerbotWalkMapWayRound> ways;
    if (spots.empty())
        return ways;

    auto const distanceToDestination = [&](PlayerbotWalkMapSpot const& spot)
    {
        float const dx = map.WorldX(spot.I) - settings.DestinationX;
        float const dy = map.WorldY(spot.J) - settings.DestinationY;
        float const dz = spot.Z - settings.DestinationZ;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

    // Walk out from her feet over legal steps only, keeping where each floor was reached from and how far she walked.
    std::vector<std::int32_t> cameFrom(spots.size(), -1);
    std::vector<float> yards(spots.size(), -1.0f);
    std::deque<std::int32_t> queue;
    yards[0] = 0.0f;
    queue.push_back(0);
    while (!queue.empty())
    {
        std::int32_t const from = queue.front();
        queue.pop_front();
        for (std::size_t d = 0; d < PLAYERBOT_WALK_MAP_DIRECTIONS.size(); ++d)
        {
            if (spots[from].Steps[d] != PlayerbotWalkMapStep::Legal)
                continue;

            std::int32_t const to = spots[from].StepSpot[d];
            if (to < 0 || yards[to] >= 0.0f)
                continue;

            float const dx = map.WorldX(spots[to].I) - map.WorldX(spots[from].I);
            float const dy = map.WorldY(spots[to].J) - map.WorldY(spots[from].J);
            float const dz = spots[to].Z - spots[from].Z;
            cameFrom[to] = from;
            yards[to] = yards[from] + std::sqrt(dx * dx + dy * dy + dz * dz);
            queue.push_back(to);
        }
    }

    float const feetDistance = distanceToDestination(spots[0]);
    struct Candidate
    {
        std::int32_t Spot;
        float Score;
        float Gain;
    };
    std::vector<Candidate> canWalkBack;
    std::vector<Candidate> oneWay;
    for (std::size_t spot = 1; spot < spots.size(); ++spot)
    {
        if (yards[spot] < 0.0f)
            continue;

        float const gain = feetDistance - distanceToDestination(spots[spot]);
        if (gain < settings.MinimumGain)
            continue;

        Candidate const candidate{ std::int32_t(spot), distanceToDestination(spots[spot]) + yards[spot] * settings.YardsWeight, gain };
        (spots[spot].Returns ? canWalkBack : oneWay).push_back(candidate);
    }

    auto const byScore = [](Candidate const& left, Candidate const& right) { return left.Score < right.Score; };
    std::sort(canWalkBack.begin(), canWalkBack.end(), byScore);
    std::sort(oneWay.begin(), oneWay.end(), byScore);

    // Ground she can walk back from first, whatever the scores say: a drop she cannot climb is a last resort.
    std::vector<Candidate> ordered;
    ordered.reserve(canWalkBack.size() + oneWay.size());
    ordered.insert(ordered.end(), canWalkBack.begin(), canWalkBack.end());
    ordered.insert(ordered.end(), oneWay.begin(), oneWay.end());

    for (Candidate const& candidate : ordered)
    {
        if (ways.size() >= settings.Ways)
            break;

        bool tooClose = false;
        for (PlayerbotWalkMapWayRound const& taken : ways)
        {
            float const dx = map.WorldX(spots[candidate.Spot].I) - map.WorldX(spots[taken.Target].I);
            float const dy = map.WorldY(spots[candidate.Spot].J) - map.WorldY(spots[taken.Target].J);
            float const dz = spots[candidate.Spot].Z - spots[taken.Target].Z;
            if (std::sqrt(dx * dx + dy * dy + dz * dz) < settings.SpreadYards)
            {
                tooClose = true;
                break;
            }
        }
        if (tooClose)
            continue;

        PlayerbotWalkMapWayRound way;
        way.Target = candidate.Spot;
        way.Gain = candidate.Gain;
        way.Yards = yards[candidate.Spot];
        way.CanWalkBack = spots[candidate.Spot].Returns;
        for (std::int32_t floor = candidate.Spot; floor >= 0; floor = cameFrom[floor])
            way.Floors.push_back(floor);
        std::reverse(way.Floors.begin(), way.Floors.end());
        if (way.Found())
            ways.push_back(std::move(way));
    }

    return ways;
}

// The way round as points to walk, with the straight runs collapsed. Every heartbeat still plants and judges its own
// step, so a collapsed run is walked exactly as its single steps were.
inline void PlayerbotWalkMapWayRoundPoints(PlayerbotWalkMap const& map, PlayerbotWalkMapWayRound const& way,
    std::vector<std::array<float, 3>>& out)
{
    out.clear();
    if (!way.Found())
        return;

    std::vector<PlayerbotWalkMapSpot> const& spots = map.Spots();
    auto const point = [&](std::int32_t floor)
    {
        return std::array<float, 3>{ map.WorldX(spots[floor].I), map.WorldY(spots[floor].J), spots[floor].Z };
    };

    out.push_back(point(way.Floors.front()));
    for (std::size_t step = 1; step + 1 < way.Floors.size(); ++step)
    {
        std::int32_t const previous = way.Floors[step - 1];
        std::int32_t const here = way.Floors[step];
        std::int32_t const next = way.Floors[step + 1];
        bool const sameDirection = (spots[here].I - spots[previous].I) == (spots[next].I - spots[here].I)
            && (spots[here].J - spots[previous].J) == (spots[next].J - spots[here].J);
        if (!sameDirection)
            out.push_back(point(here));
    }
    out.push_back(point(way.Floors.back()));
}

#endif

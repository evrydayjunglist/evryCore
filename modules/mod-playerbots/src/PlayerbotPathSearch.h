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

#ifndef EVRY_MOD_PLAYERBOT_PATH_SEARCH_H
#define EVRY_MOD_PLAYERBOT_PATH_SEARCH_H

#include "PathGenerator.h"
#include "StringFormat.h"
#include <algorithm>
#include <string>
#include <vector>

// What the navmesh search and the smoothing did for one route, in words, so Playerbots.log says why a route came back
// the way it did: she stands on ground no route leaves, the search ran out of room, or the route was longer than a path
// can hold.
inline std::string DescribePathSearch(PathSearchReport const& search)
{
    std::string text;
    if (!search.Searched)
        text = search.CorridorPolygons == 1 ? "Both ends are on one navmesh polygon, so no search ran." : "No navmesh search gave a corridor.";
    else if (search.ReachedDestination)
        text = Trinity::StringFormat("The navmesh search reached the destination's polygon using {} of its {} search nodes.",
            search.NodesUsed, search.NodeLimit);
    else if (search.RanOutOfNodes)
        text = Trinity::StringFormat("The navmesh search used all {} of its search nodes before it reached the destination, so the route "
            "heads for the closest point it had seen.", search.NodeLimit);
    else
        text = Trinity::StringFormat("The navmesh search went through all the ground joined to her start ({} search nodes) and none of it "
            "reaches the destination.", search.NodesUsed);

    if (search.Searched)
    {
        if (search.CorridorCut)
            text += Trinity::StringFormat(" The corridor was longer than the {} polygons this path can hold, so only its first {} were kept.",
                search.CorridorLimit, search.CorridorPolygons);
        else if (!search.ReachedDestination && search.CorridorPolygons == 1)
            text += " The corridor is only the polygon under her start, so the path ends in a straight line to the destination.";
        else
            text += Trinity::StringFormat(" The corridor is {} polygons.", search.CorridorPolygons);
    }

    switch (search.SmoothingEnd)
    {
        case PathSmoothingEnd::NotRun:
            break;
        case PathSmoothingEnd::ReachedEnd:
            text += Trinity::StringFormat(" Smoothing followed it to the end in {} points.", search.SmoothedPoints);
            break;
        case PathSmoothingEnd::OutOfPoints:
            text += Trinity::StringFormat(" Smoothing filled all {} points this path can hold before the end, so the engine replaced the route "
                "with a straight line.", search.SmoothedPoints);
            break;
        case PathSmoothingEnd::NoSteerTarget:
            text += Trinity::StringFormat(" Smoothing found no corner ahead to steer toward after {} points.", search.SmoothedPoints);
            break;
        case PathSmoothingEnd::CorridorEmpty:
            text += Trinity::StringFormat(" Smoothing ran out of corridor after {} points.", search.SmoothedPoints);
            break;
        case PathSmoothingEnd::QueryFailed:
            text += " Smoothing stopped because a navmesh query failed.";
            break;
    }

    return text;
}

// The search got to the destination, but the route was longer than the path could hold, so the engine replaced it with
// a straight line. The destination can be reached; only this path could not carry the way there.
inline bool PathSearchFoundTooLongARoute(PathSearchReport const& search)
{
    return search.Searched && search.ReachedDestination && search.SmoothingEnd == PathSmoothingEnd::OutOfPoints;
}

// The search used all its search nodes before it reached the destination, so it cannot say whether the destination can
// be reached: the road may just be longer than it could follow. Ground joined to nothing is not this; there the search
// goes through all of it and stops with nodes to spare.
inline bool PathSearchRanOutOfRoom(PathSearchReport const& search)
{
    return search.Searched && search.RanOutOfNodes && !search.ReachedDestination;
}

// How one place to stand beside a target came out when the stand-spot picker asked for a route to it.
struct StandSpotLook
{
    enum class Outcome : uint8
    {
        Usable,         // a route she may walk, or one the short search could not settle for her walk to ask again
        InSpellFocus,   // inside the space kept clear around a spell focus object
        NotOnMap,       // the path builder refused the position
        Refused         // the route came back as a kind the walker refuses
    };

    Outcome What = Outcome::Usable;
    uint32 PathType = 0;
    uint32 Points = 0;
    bool PlayerNavMesh = false;
    PathSearchReport Search;
    std::string SpellFocus;
};

inline char const* DescribeRefusedPathType(uint32 type, uint32 points)
{
    if (type & PATHFIND_NOT_USING_PATH)
        return "a straight line built without the movement maps, which the path builder gives when there are none here or no tile is loaded under one end";
    if (type & PATHFIND_SHORTCUT)
        return "a straight line with no route";
    if (type & PATHFIND_NOPATH)
        return "no route";
    if (points < 2)
        return "a path of fewer than two points";
    return "a route the walker refuses";
}

// How the places to stand beside a target came out, in words: how many came out each way and, for routes the walker
// refuses, what the navmesh search and the smoothing did.
inline std::string DescribeStandSpots(std::vector<StandSpotLook> const& spots)
{
    if (spots.empty())
        return "There was no place to stand to ask about.";

    struct Group
    {
        std::string Key;
        StandSpotLook const* First = nullptr;
        size_t Count = 0;
    };

    std::vector<Group> groups;
    for (StandSpotLook const& spot : spots)
    {
        std::string key = Trinity::StringFormat("{}|{}|{}|{}|{}", uint32(spot.What), spot.PathType, spot.Points < 2,
            spot.PlayerNavMesh, spot.SpellFocus);
        if (spot.What == StandSpotLook::Outcome::Refused)
            key += DescribePathSearch(spot.Search);

        auto const found = std::find_if(groups.begin(), groups.end(), [&key](Group const& group) { return group.Key == key; });
        if (found != groups.end())
            ++found->Count;
        else
            groups.push_back({ std::move(key), &spot, 1 });
    }

    std::string text;
    for (Group const& group : groups)
    {
        if (!text.empty())
            text += ' ';

        bool const one = group.Count == 1;
        std::string const count = !one && group.Count == spots.size() ? Trinity::StringFormat("All {}", group.Count)
            : std::to_string(group.Count);
        StandSpotLook const& spot = *group.First;
        switch (spot.What)
        {
            case StandSpotLook::Outcome::Usable:
                text += Trinity::StringFormat("{} {} a route she may walk.", count, one ? "has" : "have");
                break;
            case StandSpotLook::Outcome::InSpellFocus:
                text += Trinity::StringFormat("{} {} inside the space kept clear around {}.", count, one ? "is" : "are",
                    spot.SpellFocus.empty() ? std::string("a spell focus object") : spot.SpellFocus);
                break;
            case StandSpotLook::Outcome::NotOnMap:
                text += Trinity::StringFormat("{} {} not a valid map position{}.", count, one ? "is" : "are", one ? "" : "s");
                break;
            case StandSpotLook::Outcome::Refused:
                text += Trinity::StringFormat("{} came back from the {} movement maps as {} (type 0x{:02X}). {}", count,
                    spot.PlayerNavMesh ? "player" : "creature", DescribeRefusedPathType(spot.PathType, spot.Points),
                    spot.PathType, DescribePathSearch(spot.Search));
                break;
        }
    }

    return text;
}

#endif

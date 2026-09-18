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
#include <string>

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
            text += Trinity::StringFormat(" The corridor was longer than the {} polygons a path can hold, so only its first {} were kept.",
                MAX_PATH_LENGTH, search.CorridorPolygons);
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
            text += Trinity::StringFormat(" Smoothing filled all {} points a path can hold before the end, so the engine replaced the route "
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

#endif

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

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotPathSearch.h"
#include <string>

namespace
{
    bool Says(std::string const& text, char const* words)
    {
        return text.find(words) != std::string::npos;
    }
}

// The numbers below are what the search did on 17 September 2026 at Opai's pocket in the Burning Blade Coven.

TEST_CASE("Playerbot path search names ground that no route leaves", "[playerbots][path-search]")
{
    PathSearchReport search;
    search.Searched = true;
    search.NodesUsed = 6;
    search.NodeLimit = 1024;
    search.CorridorPolygons = 1;
    search.SmoothingEnd = PathSmoothingEnd::ReachedEnd;
    search.SmoothedPoints = 3;

    std::string const text = DescribePathSearch(search);
    REQUIRE(Says(text, "went through all the ground joined to her start (6 search nodes) and none of it reaches the destination"));
    REQUIRE(Says(text, "only the polygon under her start, so the path ends in a straight line to the destination"));
    REQUIRE(Says(text, "followed it to the end in 3 points"));
}

TEST_CASE("Playerbot path search says when it ran out of search nodes", "[playerbots][path-search]")
{
    PathSearchReport search;
    search.Searched = true;
    search.RanOutOfNodes = true;
    search.NodesUsed = 1024;
    search.NodeLimit = 1024;
    search.CorridorPolygons = 33;
    search.SmoothingEnd = PathSmoothingEnd::ReachedEnd;
    search.SmoothedPoints = 60;

    std::string const text = DescribePathSearch(search);
    REQUIRE(Says(text, "used all 1024 of its search nodes before it reached the destination"));
    REQUIRE(Says(text, "The corridor is 33 polygons."));
    REQUIRE_FALSE(Says(text, "joined to her start"));
}

TEST_CASE("Playerbot path search says when a route it found was too long to keep", "[playerbots][path-search]")
{
    PathSearchReport search;
    search.Searched = true;
    search.ReachedDestination = true;
    search.CorridorCut = true;
    search.NodesUsed = 986;
    search.NodeLimit = 1024;
    search.CorridorPolygons = MAX_PATH_LENGTH;
    search.SmoothingEnd = PathSmoothingEnd::OutOfPoints;
    search.SmoothedPoints = MAX_POINT_PATH_LENGTH;

    std::string const text = DescribePathSearch(search);
    REQUIRE(Says(text, "reached the destination's polygon using 986 of its 1024 search nodes"));
    REQUIRE(Says(text, "longer than the 74 polygons a path can hold, so only its first 74 were kept"));
    REQUIRE(Says(text, "filled all 74 points a path can hold before the end, so the engine replaced the route with a straight line"));
}

TEST_CASE("Playerbot path search says when both ends are on one polygon", "[playerbots][path-search]")
{
    PathSearchReport search;
    search.ReachedDestination = true;
    search.CorridorPolygons = 1;
    search.SmoothingEnd = PathSmoothingEnd::ReachedEnd;
    search.SmoothedPoints = 2;

    std::string const text = DescribePathSearch(search);
    REQUIRE(Says(text, "Both ends are on one navmesh polygon, so no search ran."));
    REQUIRE_FALSE(Says(text, "corridor"));
}

TEST_CASE("Playerbot path search says when no corridor came back", "[playerbots][path-search]")
{
    REQUIRE(DescribePathSearch(PathSearchReport()) == "No navmesh search gave a corridor.");
}

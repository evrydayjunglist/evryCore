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
#include "MMapManager.h"
#include <string>
#include <vector>

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
    search.CorridorLimit = MAX_PATH_LENGTH;
    search.SmoothingEnd = PathSmoothingEnd::OutOfPoints;
    search.SmoothedPoints = MAX_POINT_PATH_LENGTH;

    std::string const text = DescribePathSearch(search);
    REQUIRE(Says(text, "reached the destination's polygon using 986 of its 1024 search nodes"));
    REQUIRE(Says(text, "longer than the 74 polygons this path can hold, so only its first 74 were kept"));
    REQUIRE(Says(text, "filled all 74 points this path can hold before the end, so the engine replaced the route with a straight line"));
}

// The road out of the pocket on the creature set, searched with room for a long route.
TEST_CASE("Playerbot path search reads out a long route in full", "[playerbots][path-search]")
{
    PathSearchReport search;
    search.Searched = true;
    search.ReachedDestination = true;
    search.NodesUsed = 5114;
    search.NodeLimit = uint32(MMAP::LONG_ROUTE_SEARCH_NODES);
    search.CorridorPolygons = 232;
    search.CorridorLimit = LONG_PATH_LENGTH;
    search.SmoothingEnd = PathSmoothingEnd::ReachedEnd;
    search.SmoothedPoints = 418;

    std::string const text = DescribePathSearch(search);
    REQUIRE(Says(text, "reached the destination's polygon using 5114 of its 16384 search nodes"));
    REQUIRE(Says(text, "The corridor is 232 polygons."));
    REQUIRE(Says(text, "followed it to the end in 418 points"));
}

TEST_CASE("Playerbot path search tells a route too long to hold from one it could not find", "[playerbots][path-search]")
{
    PathSearchReport tooLong;
    tooLong.Searched = true;
    tooLong.ReachedDestination = true;
    tooLong.CorridorCut = true;
    tooLong.SmoothingEnd = PathSmoothingEnd::OutOfPoints;
    REQUIRE(PathSearchFoundTooLongARoute(tooLong));

    // Joined to nothing: the search never got there, however the smoothing ended.
    PathSearchReport island = tooLong;
    island.ReachedDestination = false;
    REQUIRE_FALSE(PathSearchFoundTooLongARoute(island));

    // Got there and the path held all of it: nothing was cut.
    PathSearchReport held = tooLong;
    held.CorridorCut = false;
    held.SmoothingEnd = PathSmoothingEnd::ReachedEnd;
    REQUIRE_FALSE(PathSearchFoundTooLongARoute(held));

    // A failed query is not a long route.
    PathSearchReport failed = tooLong;
    failed.SmoothingEnd = PathSmoothingEnd::QueryFailed;
    REQUIRE_FALSE(PathSearchFoundTooLongARoute(failed));
}

// The Valley of Trials to Master Gadrin: the short search runs out before the 760-yard road, and the long one finds it.
TEST_CASE("Playerbot path search tells a search that ran out of room from ground joined to nothing", "[playerbots][path-search]")
{
    PathSearchReport outOfRoom;
    outOfRoom.Searched = true;
    outOfRoom.RanOutOfNodes = true;
    outOfRoom.CorridorCut = true;
    outOfRoom.NodesUsed = 1024;
    outOfRoom.NodeLimit = 1024;
    outOfRoom.SmoothingEnd = PathSmoothingEnd::OutOfPoints;
    REQUIRE(PathSearchRanOutOfRoom(outOfRoom));
    REQUIRE_FALSE(PathSearchFoundTooLongARoute(outOfRoom));

    // The cave shelf: the search went through all 6 polygons joined to her start and stopped with nodes to spare.
    PathSearchReport island;
    island.Searched = true;
    island.NodesUsed = 6;
    island.NodeLimit = 1024;
    island.CorridorPolygons = 4;
    island.SmoothingEnd = PathSmoothingEnd::ReachedEnd;
    REQUIRE_FALSE(PathSearchRanOutOfRoom(island));

    // A search that got there did not run out of room, however full it was.
    PathSearchReport reached = outOfRoom;
    reached.ReachedDestination = true;
    REQUIRE_FALSE(PathSearchRanOutOfRoom(reached));

    // No search ran at all.
    REQUIRE_FALSE(PathSearchRanOutOfRoom(PathSearchReport()));
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

namespace
{
    // What the short search did from the Valley of Trials at (-560.89, -4269.03) toward each side of Master Gadrin, 700
    // yards away in Sen'jin Village, on the player maps (the offline probe, 17 September 2026).
    StandSpotLook ValleyToGadrinSide()
    {
        StandSpotLook spot;
        spot.What = StandSpotLook::Outcome::Refused;
        spot.PathType = PATHFIND_NOPATH | PATHFIND_SHORTCUT;
        spot.Points = 2;
        spot.PlayerNavMesh = true;
        spot.Search.Searched = true;
        spot.Search.RanOutOfNodes = true;
        spot.Search.CorridorCut = true;
        spot.Search.NodesUsed = 1024;
        spot.Search.NodeLimit = 1024;
        spot.Search.CorridorPolygons = MAX_PATH_LENGTH;
        spot.Search.CorridorLimit = MAX_PATH_LENGTH;
        spot.Search.SmoothingEnd = PathSmoothingEnd::OutOfPoints;
        spot.Search.SmoothedPoints = MAX_POINT_PATH_LENGTH;
        return spot;
    }
}

TEST_CASE("Playerbot stand spots say once when every side came back the same way", "[playerbots][path-search]")
{
    std::vector<StandSpotLook> const spots(8, ValleyToGadrinSide());

    std::string const text = DescribeStandSpots(spots);
    REQUIRE(Says(text, "All 8 came back from the player movement maps as a straight line with no route (type 0x0A)."));
    REQUIRE(Says(text, "used all 1024 of its search nodes before it reached the destination"));
    REQUIRE(Says(text, "filled all 74 points this path can hold"));
    // One sentence for the eight sides, not eight.
    REQUIRE(text.find("came back") == text.rfind("came back"));
}

TEST_CASE("Playerbot stand spots count each way a side came out", "[playerbots][path-search]")
{
    std::vector<StandSpotLook> spots(5, ValleyToGadrinSide());

    StandSpotLook fire;
    fire.What = StandSpotLook::Outcome::InSpellFocus;
    fire.SpellFocus = "Campfire";
    spots.push_back(fire);
    spots.push_back(fire);

    StandSpotLook unloaded;
    unloaded.What = StandSpotLook::Outcome::Refused;
    unloaded.PathType = PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH;
    unloaded.Points = 2;
    spots.push_back(unloaded);

    std::string const text = DescribeStandSpots(spots);
    REQUIRE(Says(text, "5 came back from the player movement maps as a straight line with no route (type 0x0A)."));
    REQUIRE(Says(text, "2 are inside the space kept clear around Campfire."));
    REQUIRE(Says(text, "1 came back from the creature movement maps as a straight line built without the movement maps"));
    REQUIRE(Says(text, "(type 0x11)"));
    REQUIRE_FALSE(Says(text, "All "));
}

TEST_CASE("Playerbot stand spots say when there was nothing to ask about", "[playerbots][path-search]")
{
    REQUIRE(DescribeStandSpots({}) == "There was no place to stand to ask about.");
}

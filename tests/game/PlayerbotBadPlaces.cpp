/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotBadPlaces.h"

namespace
{
    struct Point
    {
        float x;
        float y;
        float z;
    };

    // Up the hillside where many bots failed in the 500-bot runs, going north-east.
    std::vector<Point> const UpTheHill = { { 280.0f, -4520.0f, 24.0f }, { 290.0f, -4511.0f, 25.5f }, { 305.0f, -4498.0f, 40.0f } };
}

TEST_CASE("A route up the slope where her walk failed meets that place", "[playerbots][movement]")
{
    PlayerbotBadPlaces places;
    places.Note(1, 293.62f, -4509.02f, 26.48f, 1.0f, 1.0f, 1000);

    PlayerbotBadPlace const* bad = places.RouteMeets(1, UpTheHill, 2000);
    REQUIRE(bad != nullptr);
    REQUIRE(bad->Failures == 1);

    // Another map with the same numbers is another place.
    REQUIRE(places.RouteMeets(0, UpTheHill, 2000) == nullptr);
}

TEST_CASE("The same slope walked down, or far to the side, is a different step", "[playerbots][movement]")
{
    PlayerbotBadPlaces places;
    places.Note(1, 293.62f, -4509.02f, 26.48f, 1.0f, 1.0f, 1000);

    std::vector<Point> const downTheHill(UpTheHill.rbegin(), UpTheHill.rend());
    REQUIRE(places.RouteMeets(1, downTheHill, 2000) == nullptr);

    std::vector<Point> const alongside = { { 280.0f, -4500.0f, 24.0f }, { 290.0f, -4491.0f, 25.5f } };
    REQUIRE(places.RouteMeets(1, alongside, 2000) == nullptr);

    // A cave far under the place is not the place.
    std::vector<Point> const under = { { 280.0f, -4520.0f, 10.0f }, { 305.0f, -4498.0f, 12.0f } };
    REQUIRE(places.RouteMeets(1, under, 2000) == nullptr);
}

TEST_CASE("She tries a bad place again after a while", "[playerbots][movement]")
{
    PlayerbotBadPlaces places;
    places.Note(1, 293.62f, -4509.02f, 26.48f, 1.0f, 1.0f, 1000);
    REQUIRE(places.RouteMeets(1, UpTheHill, 1000 + PLAYERBOT_BAD_PLACE_FORGET_MS - 1) != nullptr);
    REQUIRE(places.RouteMeets(1, UpTheHill, 1000 + PLAYERBOT_BAD_PLACE_FORGET_MS) == nullptr);
    REQUIRE(places.Size() == 0);
}

TEST_CASE("Failing at the same place again counts it and keeps it longer", "[playerbots][movement]")
{
    PlayerbotBadPlaces places;
    places.Note(1, 293.62f, -4509.02f, 26.48f, 1.0f, 1.0f, 1000);
    places.Note(1, 294.5f, -4508.0f, 27.0f, 0.8f, 1.0f, 5000);
    REQUIRE(places.Size() == 1);

    PlayerbotBadPlace const* bad = places.RouteMeets(1, UpTheHill, 1000 + PLAYERBOT_BAD_PLACE_FORGET_MS);
    REQUIRE(bad != nullptr);
    REQUIRE(bad->Failures == 2);
}

TEST_CASE("She keeps only the newest bad places", "[playerbots][movement]")
{
    PlayerbotBadPlaces places;
    for (uint32_t i = 0; i < PLAYERBOT_BAD_PLACES_KEPT + 2; ++i)
        places.Note(1, 100.0f * i, 0.0f, 0.0f, 1.0f, 0.0f, 1000 + i);
    REQUIRE(places.Size() == PLAYERBOT_BAD_PLACES_KEPT);

    // The first two were pushed out.
    std::vector<Point> const pastTheFirst = { { -5.0f, 0.0f, 0.0f }, { 5.0f, 0.0f, 0.0f } };
    REQUIRE(places.RouteMeets(1, pastTheFirst, 2000) == nullptr);
    std::vector<Point> const pastTheLast = { { 100.0f * (PLAYERBOT_BAD_PLACES_KEPT + 1) - 5.0f, 0.0f, 0.0f },
        { 100.0f * (PLAYERBOT_BAD_PLACES_KEPT + 1) + 5.0f, 0.0f, 0.0f } };
    REQUIRE(places.RouteMeets(1, pastTheLast, 2000) != nullptr);
}

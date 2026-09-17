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

#include "../../modules/mod-playerbots/src/PlayerbotWalkMapEscape.h"
#include <functional>
#include <vector>

namespace
{
    constexpr float Climb = 35.0f;
    constexpr float Drop = 2.0f;
    constexpr float SearchAbove = 0.5f;

    // Made-up ground, as in the walk map's own tests: every floor at a spot, highest first, and walls across steps.
    class TestWorld final : public PlayerbotWalkMapWorld
    {
    public:
        std::function<std::vector<float>(float, float)> Floors = [](float, float) { return std::vector<float>{ 0.0f }; };
        std::function<bool(float, float, float, float)> Wall = [](float, float, float, float) { return false; };

        bool IsLoaded(float, float) override { return true; }

        PlayerbotWalkMapStepResult Step(float fromX, float fromY, float fromZ, float toX, float toY) override
        {
            PlayerbotWalkMapStepResult result;
            float const run = std::sqrt((toX - fromX) * (toX - fromX) + (toY - fromY) * (toY - fromY));
            if (!Plant(toX, toY, fromZ + std::tan(Climb * 3.14159265f / 180.0f) * run, result.Z))
            {
                result.Step = PlayerbotWalkMapStep::NoFloor;
                return result;
            }

            float const rise = result.Z - fromZ;
            if (rise > 0.0f && std::atan2(rise, run) * 180.0f / 3.14159265f > Climb)
                result.Step = PlayerbotWalkMapStep::SteepUp;
            else if (rise <= 0.0f && -rise > Drop)
                result.Step = PlayerbotWalkMapStep::TooFarDown;
            else if (Wall(fromX, fromY, toX, toY))
                result.Step = PlayerbotWalkMapStep::StaticCollision;
            else
                result.Step = PlayerbotWalkMapStep::Legal;
            return result;
        }

        bool GroundBelow(float x, float y, float z, float& outZ) override { return Plant(x, y, z, outZ); }

    private:
        bool Plant(float x, float y, float searchZ, float& outZ)
        {
            for (float floor : Floors(x, y))
            {
                if (floor <= searchZ + SearchAbove)
                {
                    outZ = floor;
                    return true;
                }
            }
            return false;
        }
    };

    PlayerbotWalkMap Mapped(TestWorld& world, float radius, float originZ = 0.0f)
    {
        PlayerbotWalkMapSettings settings;
        settings.OriginZ = originZ;
        settings.Spacing = 0.7f;
        settings.Radius = radius;
        settings.MaxClimbDegrees = Climb;
        settings.MaxDropYards = Drop;
        PlayerbotWalkMap map(settings);
        for (int guard = 0; guard < 1000000 && !map.Advance(world, 4096); ++guard)
            ;
        return map;
    }

    PlayerbotWalkMapWayRoundSettings Toward(float x, float y, float z)
    {
        PlayerbotWalkMapWayRoundSettings settings;
        settings.DestinationX = x;
        settings.DestinationY = y;
        settings.DestinationZ = z;
        return settings;
    }

    // Every floor of the way round is one legal step from the one before it.
    bool StepsJoinUp(PlayerbotWalkMap const& map, PlayerbotWalkMapWayRound const& way)
    {
        std::vector<PlayerbotWalkMapSpot> const& spots = map.Spots();
        for (std::size_t step = 1; step < way.Floors.size(); ++step)
        {
            std::int32_t const from = way.Floors[step - 1];
            std::int32_t const to = way.Floors[step];
            bool joined = false;
            for (std::size_t d = 0; d < PLAYERBOT_WALK_MAP_DIRECTIONS.size(); ++d)
                if (spots[from].Steps[d] == PlayerbotWalkMapStep::Legal && spots[from].StepSpot[d] == to)
                    joined = true;
            if (!joined)
                return false;
        }
        return true;
    }
}

TEST_CASE("Playerbot way round leads through the gap in a wall", "[playerbots][walk-map]")
{
    // A wall along x = 5 with open ground past y = 10 on either side.
    TestWorld world;
    world.Wall = [](float fromX, float fromY, float toX, float toY)
    {
        return (fromX < 5.0f) != (toX < 5.0f) && std::fabs(fromY) < 10.0f && std::fabs(toY) < 10.0f;
    };
    PlayerbotWalkMap const map = Mapped(world, 30.0f);

    std::vector<PlayerbotWalkMapWayRound> const ways = FindPlayerbotWalkMapWaysRound(map, Toward(100.0f, 0.0f, 0.0f));
    REQUIRE_FALSE(ways.empty());

    PlayerbotWalkMapWayRound const& best = ways.front();
    CHECK(best.Found());
    CHECK(best.CanWalkBack);
    CHECK(best.Gain > 5.0f);
    CHECK(StepsJoinUp(map, best));
    CHECK(best.Floors.front() == 0);

    // It ends past the wall, and it crossed the wall's line where the wall is not.
    PlayerbotWalkMapSpot const& target = map.Spots()[best.Target];
    CHECK(map.WorldX(target.I) > 5.0f);
    bool crossedBesideTheWall = false;
    for (std::size_t step = 1; step < best.Floors.size(); ++step)
    {
        float const before = map.WorldX(map.Spots()[best.Floors[step - 1]].I);
        float const after = map.WorldX(map.Spots()[best.Floors[step]].I);
        if ((before < 5.0f) != (after < 5.0f))
            crossedBesideTheWall = std::fabs(map.WorldY(map.Spots()[best.Floors[step]].J)) >= 10.0f;
    }
    CHECK(crossedBesideTheWall);
}

TEST_CASE("Playerbot way round is empty when nothing she can reach is closer", "[playerbots][walk-map]")
{
    TestWorld world;
    PlayerbotWalkMap const map = Mapped(world, 12.0f);

    // Standing on the destination: no spot is closer than her feet.
    CHECK(FindPlayerbotWalkMapWaysRound(map, Toward(0.0f, 0.0f, 0.0f)).empty());

    // Closer, but not by the five yards that make a walk worth it.
    PlayerbotWalkMapWayRoundSettings settings = Toward(3.0f, 0.0f, 0.0f);
    settings.MinimumGain = 5.0f;
    CHECK(FindPlayerbotWalkMapWaysRound(map, settings).empty());
}

TEST_CASE("Playerbot way round prefers ground she can walk back from", "[playerbots][walk-map]")
{
    // South of her a slope she can walk down but not climb back, leading toward the destination.
    TestWorld world;
    world.Floors = [](float x, float)
    {
        if (x < -12.0f)
            return std::vector<float>{ std::max(-8.0f, (x + 12.0f) * 1.6f) };
        return std::vector<float>{ 0.0f };
    };
    PlayerbotWalkMap const map = Mapped(world, 25.0f);

    std::vector<PlayerbotWalkMapWayRound> const ways = FindPlayerbotWalkMapWaysRound(map, Toward(-60.0f, 0.0f, -8.0f));
    REQUIRE_FALSE(ways.empty());
    // The slope is closer to the destination, but she keeps to ground she can walk back from.
    CHECK(ways.front().CanWalkBack);
    CHECK(map.WorldX(map.Spots()[ways.front().Target].I) >= -12.7f);

    // Once only the slope is worth walking to, she takes the slope.
    PlayerbotWalkMapWayRoundSettings settings = Toward(-60.0f, 0.0f, -8.0f);
    settings.MinimumGain = 14.0f;
    std::vector<PlayerbotWalkMapWayRound> const down = FindPlayerbotWalkMapWaysRound(map, settings);
    REQUIRE_FALSE(down.empty());
    CHECK_FALSE(down.front().CanWalkBack);
    CHECK(map.WorldX(map.Spots()[down.front().Target].I) < -12.0f);
}

TEST_CASE("Playerbot ways round are spread apart and start at her feet", "[playerbots][walk-map]")
{
    TestWorld world;
    PlayerbotWalkMap const map = Mapped(world, 20.0f);
    PlayerbotWalkMapWayRoundSettings settings = Toward(100.0f, 0.0f, 0.0f);
    settings.Ways = 3;
    settings.SpreadYards = 8.0f;

    std::vector<PlayerbotWalkMapWayRound> const ways = FindPlayerbotWalkMapWaysRound(map, settings);
    REQUIRE(ways.size() == 3);
    for (PlayerbotWalkMapWayRound const& way : ways)
    {
        CHECK(way.Floors.front() == 0);
        CHECK(StepsJoinUp(map, way));
        CHECK(way.Yards > 0.0f);
    }
    for (std::size_t left = 0; left < ways.size(); ++left)
        for (std::size_t right = left + 1; right < ways.size(); ++right)
        {
            PlayerbotWalkMapSpot const& a = map.Spots()[ways[left].Target];
            PlayerbotWalkMapSpot const& b = map.Spots()[ways[right].Target];
            CHECK(std::hypot(map.WorldX(a.I) - map.WorldX(b.I), map.WorldY(a.J) - map.WorldY(b.J)) >= 8.0f);
        }
}

TEST_CASE("Playerbot way round collapses its straight runs into points", "[playerbots][walk-map]")
{
    // One spot wide, so the only way to the destination is straight north.
    TestWorld world;
    world.Floors = [](float, float y)
    {
        return std::fabs(y) <= 0.35f ? std::vector<float>{ 0.0f } : std::vector<float>{ 50.0f };
    };
    PlayerbotWalkMap const map = Mapped(world, 20.0f);
    std::vector<PlayerbotWalkMapWayRound> const ways = FindPlayerbotWalkMapWaysRound(map, Toward(100.0f, 0.0f, 0.0f));
    REQUIRE_FALSE(ways.empty());

    std::vector<std::array<float, 3>> points;
    PlayerbotWalkMapWayRoundPoints(map, ways.front(), points);
    // One straight run: her feet and the far end.
    CHECK(points.size() == 2);
    CHECK(points.size() < ways.front().Floors.size());

    PlayerbotWalkMapSpot const& start = map.Spots()[ways.front().Floors.front()];
    PlayerbotWalkMapSpot const& target = map.Spots()[ways.front().Target];
    CHECK(points.front()[0] == Catch::Approx(map.WorldX(start.I)));
    CHECK(points.front()[1] == Catch::Approx(map.WorldY(start.J)));
    CHECK(points.back()[0] == Catch::Approx(map.WorldX(target.I)));
    CHECK(points.back()[1] == Catch::Approx(map.WorldY(target.J)));
}

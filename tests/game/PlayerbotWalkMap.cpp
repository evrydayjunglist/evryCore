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

#include "../../modules/mod-playerbots/src/PlayerbotWalkMap.h"
#include "../../modules/mod-playerbots/src/PlayerbotWalkMapPage.h"
#include <functional>
#include <string>
#include <vector>

namespace
{
    constexpr float Climb = 35.0f;
    constexpr float Drop = 2.0f;
    // The floor search starts this far above the point it is asked from, as the server's does.
    constexpr float SearchAbove = 0.5f;

    // Made-up ground: every floor at a spot, highest first, and walls across steps.
    class TestWorld final : public PlayerbotWalkMapWorld
    {
    public:
        std::function<std::vector<float>(float, float)> Floors = [](float, float) { return std::vector<float>{ 0.0f }; };
        std::function<bool(float, float, float, float, float)> Wall = [](float, float, float, float, float) { return false; };
        std::function<bool(float, float)> Loaded = [](float, float) { return true; };

        bool IsLoaded(float x, float y) override { return Loaded(x, y); }

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
            else if (Wall(fromX, fromY, toX, toY, result.Z))
                result.Step = PlayerbotWalkMapStep::StaticCollision;
            else
                result.Step = PlayerbotWalkMapStep::Legal;
            return result;
        }

        bool GroundBelow(float x, float y, float z, float& outZ) override
        {
            return Plant(x, y, z, outZ);
        }

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

    PlayerbotWalkMapSettings Settings(float radius, float originZ = 0.0f)
    {
        PlayerbotWalkMapSettings settings;
        settings.OriginZ = originZ;
        settings.Spacing = 0.7f;
        settings.Radius = radius;
        settings.MaxClimbDegrees = Climb;
        settings.MaxDropYards = Drop;
        return settings;
    }

    void Finish(PlayerbotWalkMap& map, TestWorld& world, std::size_t slice = 1000000)
    {
        for (int guard = 0; guard < 1000000 && !map.Advance(world, slice); ++guard)
            ;
    }

    std::int32_t SpotAt(PlayerbotWalkMap const& map, float x, float y, float z)
    {
        float const spacing = map.Settings().Spacing;
        return map.FindSpot(std::int32_t(std::lround(x / spacing)), std::int32_t(std::lround(y / spacing)), z);
    }

    // A bowl 8 yards across with a cliff all round, and a chute on its east side too steep to climb but not too steep
    // to walk down.
    std::vector<float> BowlFloors(float x, float y)
    {
        float const r = std::sqrt(x * x + y * y);
        float z = r < 8.0f ? 0.0f : 6.0f;
        if (y < -6.0f && std::fabs(x) < 2.0f)
            z = std::min(z, std::max(0.0f, -y - 8.0f) * 1.6f);
        return { z };
    }
}

TEST_CASE("Playerbot walk map covers open flat ground both ways", "[playerbots][walk-map]")
{
    TestWorld world;
    PlayerbotWalkMap map(Settings(10.0f));
    Finish(map, world);

    REQUIRE(map.Finished());
    PlayerbotWalkMapSummary const summary = map.Summarize();
    CHECK(summary.Reached == summary.Spots);
    CHECK(summary.TwoWay == summary.Spots);
    CHECK(summary.OneWayOut == 0);
    CHECK(summary.OneWayIn == 0);
    CHECK(summary.ReachedEdge > 0);
    CHECK(summary.TwoWayEdge == summary.ReachedEdge);
    for (std::size_t steps : summary.BorderSteps)
        CHECK(steps == 0);

    // One floor at every spot inside the circle.
    std::size_t inside = 0;
    for (std::int32_t i = -map.HalfWidth(); i <= map.HalfWidth(); ++i)
        for (std::int32_t j = -map.HalfWidth(); j <= map.HalfWidth(); ++j)
            if (map.InMap(i, j))
                ++inside;
    CHECK(summary.Spots == inside);
}

TEST_CASE("Playerbot walk map shows a pocket she can walk down into but not out of", "[playerbots][walk-map]")
{
    TestWorld world;
    world.Floors = BowlFloors;
    PlayerbotWalkMap map(Settings(30.0f));
    Finish(map, world);

    PlayerbotWalkMapSummary const summary = map.Summarize();
    CHECK(summary.ReachedEdge == 0);
    CHECK(summary.OneWayOut == 0);
    CHECK(summary.OneWayIn > 0);
    CHECK(summary.BorderSteps[std::size_t(PlayerbotWalkMapStep::NoFloor)] > 0);

    std::vector<PlayerbotWalkMapSpot> const& spots = map.Spots();
    std::int32_t const start = SpotAt(map, 0.0f, 0.0f, 0.0f);
    REQUIRE(start == 0);
    CHECK(spots[start].Reached);
    CHECK(spots[start].Returns);

    // The ground above the cliff leads down the chute to her, and she cannot climb to it.
    std::int32_t const plateau = SpotAt(map, 0.0f, -20.3f, 6.0f);
    REQUIRE(plateau >= 0);
    CHECK_FALSE(spots[plateau].Reached);
    CHECK(spots[plateau].Returns);
    CHECK_FALSE(spots[plateau].Expanded);

    // Every floor she reached is inside the bowl or at the foot of the chute.
    for (PlayerbotWalkMapSpot const& spot : spots)
        if (spot.Reached)
            CHECK(spot.Z < 1.0f);
}

TEST_CASE("Playerbot walk map finds one-way ground she can walk down but not back up", "[playerbots][walk-map]")
{
    // She starts at the top of the chute this time, so she can walk down into the bowl and cannot come back.
    TestWorld world;
    world.Floors = BowlFloors;
    PlayerbotWalkMapSettings settings = Settings(30.0f, 6.0f);
    settings.OriginY = -14.0f;
    PlayerbotWalkMap map(settings);
    Finish(map, world);

    PlayerbotWalkMapSummary const summary = map.Summarize();
    CHECK(summary.OneWayOut > 0);
    CHECK(summary.ReachedEdge > 0);

    std::int32_t const bowl = map.FindSpot(0, std::int32_t(std::lround(14.0f / 0.7f)), 0.0f);
    REQUIRE(bowl >= 0);
    CHECK(map.Spots()[bowl].Reached);
    CHECK_FALSE(map.Spots()[bowl].Returns);
}

TEST_CASE("Playerbot walk map keeps a bridge apart from the ground under it", "[playerbots][walk-map]")
{
    // A 30 degree ramp north to a bridge five yards up, with open ground under the bridge.
    TestWorld world;
    world.Floors = [](float x, float y)
    {
        bool const onRamp = std::fabs(y) <= 1.5f && x >= 4.0f && x < 12.66f;
        bool const onBridge = std::fabs(y) <= 1.5f && x >= 12.66f && x <= 20.0f;
        if (onRamp)
            return std::vector<float>{ (x - 4.0f) * 0.57735f };
        if (onBridge)
            return std::vector<float>{ 5.0f, 0.0f };
        return std::vector<float>{ 0.0f };
    };
    PlayerbotWalkMap map(Settings(25.0f));
    Finish(map, world);

    std::int32_t const i = std::int32_t(std::lround(16.1f / 0.7f));
    std::int32_t const under = map.FindSpot(i, 0, 0.0f);
    std::int32_t const over = map.FindSpot(i, 0, 5.0f);
    REQUIRE(under >= 0);
    REQUIRE(over >= 0);
    CHECK(under != over);
    CHECK(map.Spots()[under].Reached);
    CHECK(map.Spots()[over].Reached);

    // East off the edge of the bridge is a five-yard drop.
    std::int32_t const edge = map.FindSpot(i, -2, 5.0f);
    REQUIRE(edge >= 0);
    CHECK(map.Spots()[edge].Steps[2] == PlayerbotWalkMapStep::TooFarDown);
}

TEST_CASE("Playerbot walk map refuses a step through a wall", "[playerbots][walk-map]")
{
    // A fence along x = 3 from y = -30 to y = 30 closes the whole map.
    TestWorld world;
    world.Wall = [](float fromX, float, float toX, float, float) { return (fromX < 3.0f) != (toX < 3.0f); };
    PlayerbotWalkMap map(Settings(20.0f));
    Finish(map, world);

    PlayerbotWalkMapSummary const summary = map.Summarize();
    CHECK(summary.BorderSteps[std::size_t(PlayerbotWalkMapStep::StaticCollision)] > 0);
    for (PlayerbotWalkMapSpot const& spot : map.Spots())
        if (spot.Reached)
            CHECK(map.WorldX(spot.I) < 3.0f);
}

TEST_CASE("Playerbot walk map does not step into ground that is not loaded", "[playerbots][walk-map]")
{
    TestWorld world;
    world.Loaded = [](float x, float) { return x < 5.0f; };
    PlayerbotWalkMap map(Settings(15.0f));
    Finish(map, world);

    PlayerbotWalkMapSummary const summary = map.Summarize();
    CHECK(summary.NotLoadedSteps > 0);
    for (PlayerbotWalkMapSpot const& spot : map.Spots())
        CHECK(map.WorldX(spot.I) < 5.0f);
}

TEST_CASE("Playerbot walk map stops at its spot limit", "[playerbots][walk-map]")
{
    TestWorld world;
    PlayerbotWalkMapSettings settings = Settings(30.0f);
    settings.MaxSpots = 50;
    PlayerbotWalkMap map(settings);
    Finish(map, world);

    CHECK(map.Finished());
    CHECK(map.HitSpotLimit());
    CHECK(map.Spots().size() == 50);
}

TEST_CASE("Playerbot walk map comes out the same in small slices", "[playerbots][walk-map]")
{
    TestWorld whole;
    whole.Floors = BowlFloors;
    PlayerbotWalkMap once(Settings(20.0f));
    Finish(once, whole);

    TestWorld sliced;
    sliced.Floors = BowlFloors;
    PlayerbotWalkMap pieces(Settings(20.0f));
    Finish(pieces, sliced, 3);

    REQUIRE(once.Spots().size() == pieces.Spots().size());
    for (std::size_t spot = 0; spot < once.Spots().size(); ++spot)
    {
        CHECK(once.Spots()[spot].Reached == pieces.Spots()[spot].Reached);
        CHECK(once.Spots()[spot].Returns == pieces.Spots()[spot].Returns);
        CHECK(once.Spots()[spot].Steps == pieces.Spots()[spot].Steps);
    }
}

TEST_CASE("Playerbot walk map page describes the pocket and keeps its data inside the page", "[playerbots][walk-map]")
{
    TestWorld world;
    world.Floors = BowlFloors;
    PlayerbotWalkMap map(Settings(30.0f));
    Finish(map, world);

    PlayerbotWalkMapReport report;
    report.Name = "Opai</script><b>";
    report.Place = "Burning Blade Coven, Durotar";
    report.MapId = 1;
    std::vector<std::string> const lines = DescribePlayerbotWalkMap(map, map.Summarize(), report);
    auto const said = [&](std::string const& words)
    {
        for (std::string const& line : lines)
            if (line.find(words) != std::string::npos)
                return true;
        return false;
    };
    CHECK(said("does not reach the edge of this map"));
    CHECK(said("no floor within her climb"));
    CHECK(said("No walk destination is on record"));

    std::string const page = BuildPlayerbotWalkMapPage(map, report, lines);
    std::size_t closings = 0;
    for (std::size_t at = page.find("</script>"); at != std::string::npos; at = page.find("</script>", at + 1))
        ++closings;
    CHECK(closings == 2);
    CHECK(page.find("Opai\\u003c/script\\u003e\\u003cb\\u003e") != std::string::npos);
}

TEST_CASE("Playerbot walk map names navmesh path types in words", "[playerbots][walk-map]")
{
    CHECK(PlayerbotWalkMapRouteWords(0x01) == "the navmesh found the whole way (path type 0x01)");
    CHECK(PlayerbotWalkMapRouteWords(0x04) == "the navmesh found only part of the way (path type 0x04)");
    CHECK(PlayerbotWalkMapRouteWords(0x08) == "the navmesh found no route (path type 0x08)");
    CHECK(PlayerbotWalkMapRouteWords(0x44) == "the navmesh found only part of the way; her feet are far from the navmesh (path type 0x44)");
}

/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "LeechAccumulator.h"
#include <limits>

TEST_CASE("Leech pays out without another spell cast", "[ClassAbilities][Leech]")
{
    LeechAccumulator leech;
    leech.Add(100, 10.0f);
    CHECK(leech.Update(1499, 1500) == 0);
    leech.Add(50, 10.0f);
    CHECK(leech.Update(1, 1500) == 15);
    CHECK(leech.Update(1500, 1500) == 0);
}

TEST_CASE("Leech preserves fractions and discards pending healing at death", "[ClassAbilities][Leech]")
{
    LeechAccumulator leech;
    leech.Add(3, 10.0f);
    CHECK(leech.Update(3000, 1500) == 0);
    leech.Add(12, 10.0f);
    CHECK(leech.Update(1500, 1500) == 1);
    leech.Add(5, 10.0f);
    CHECK(leech.Update(1500, 1500) == 1);
    leech.Add(500, 10.0f);
    CHECK(leech.Update(500, 1500) == 0);
    leech.Reset();
    CHECK(leech.Update(3000, 1500) == 0);
}

TEST_CASE("Leech handles haste, late updates and invalid amounts", "[ClassAbilities][Leech]")
{
    LeechAccumulator leech;
    leech.Add(100, -1.0f);
    leech.Add(100, std::numeric_limits<float>::infinity());
    leech.Add(100, std::numeric_limits<float>::quiet_NaN());
    CHECK(leech.Update(10000, 750) == 0);
    leech.Add(100, 10.0f);
    CHECK(leech.Update(749, 750) == 0);
    CHECK(leech.Update(10000, 750) == 10);
    CHECK(leech.Update(10000, 750) == 0);
    leech.Add(std::numeric_limits<std::uint32_t>::max(), 200.0f);
    CHECK(leech.Update(750, 750) == std::numeric_limits<std::uint32_t>::max());
}

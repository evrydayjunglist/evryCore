/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 * Licensed under the GNU General Public License, version 2 or later.
 */

#include "tc_catch2.h"
#include "../../src/server/scripts/Spells/EvokerSpellMath.h"
#include <algorithm>
#include <limits>
#include <numeric>

TEST_CASE("Time Dilation conserves delayed damage without unsigned underflow", "[ClassAbilities][Evoker]")
{
    for (std::uint32_t amount : { 0u, 1u, 2u, 3u, 4u, 5u, 7u, 100u, 10001u,
        std::numeric_limits<std::uint32_t>::max() })
    {
        auto ticks = EvokerSpellMath::SplitDelayedDamage(amount);
        CHECK(std::accumulate(ticks.begin(), ticks.end(), std::uint64_t(0)) == amount);
        CHECK(*std::max_element(ticks.begin(), ticks.end()) <= amount);
        CHECK(*std::max_element(ticks.begin(), ticks.end()) - *std::min_element(ticks.begin(), ticks.end()) <= 1);
    }
}

TEST_CASE("Stretch Time retains overlapping debt and stops after repayment", "[ClassAbilities][Evoker]")
{
    std::deque<std::uint64_t> ticks;
    EvokerSpellMath::AccumulateDelayedDamage(ticks, 13, 10);
    std::uint64_t paid = ticks.front();
    ticks.pop_front();
    EvokerSpellMath::AccumulateDelayedDamage(ticks, 2, 10);
    CHECK(ticks.size() == 10);
    CHECK(paid + std::accumulate(ticks.begin(), ticks.end(), std::uint64_t(0)) == 15);
    while (!ticks.empty())
    {
        paid += ticks.front();
        ticks.pop_front();
    }
    CHECK(paid == 15);
    CHECK(ticks.empty());
}

/* This file is part of the TrinityCore Project. See AUTHORS file for Copyright information.
 * Licensed under the GNU General Public License, version 2 or later. */
#include "../../modules/mod-coa/src/ReaperSpellMechanics.h"
#include <catch2/catch_test_macros.hpp>

using namespace Coa::Reaper;

TEST_CASE("Reaper empty resources complete two fragment infusion cycles", "[Reaper]")
{
    Souls state;
    for (int cycle = 0; cycle != 2; ++cycle)
    {
        for (int n = 1; n <= 9; ++n)
        {
            state = state.Gain(1, 0);
            CHECK(state.Fragments == n % 3);
            CHECK(state.Reaped == n / 3);
            CHECK(state.Infused() == (n == 9));
        }
        state = {};
    }
}

TEST_CASE("Reaper souls and fragments saturate without wrapping", "[Reaper]")
{
    auto state = Souls{2,2}.Gain(1,1);
    REQUIRE(state.Fragments == 0);
    REQUIRE(state.Reaped == 3);
    state = state.Gain(UINT32_MAX, UINT32_MAX);
    CHECK(state.Fragments < 3);
    CHECK(state.Reaped == 3);
    state = Souls{255,255}.Gain(UINT32_MAX,UINT32_MAX);
    CHECK(state.Fragments < 3);
    CHECK(state.Reaped == 3);
}

TEST_CASE("Reaper Reap rewards damage once and requires Soul Collector for fragments", "[Reaper]")
{
    CastRewards cast;
    auto reward = cast.Hit(Reap,true,false,1,true);
    CHECK(reward.Fragments == 1);
    CHECK(reward.ReapPower);
    CHECK_FALSE(cast.Hit(Reap,true,false,999,true).ReapPower);
    CastRewards noPassive;
    reward = noPassive.Hit(Reap,true,false,10,false);
    CHECK(reward.Fragments == 0);
    CHECK(reward.ReapPower);
}

TEST_CASE("Reaper absorb zero damage and immunity have distinct rewards", "[Reaper]")
{
    for (uint32_t spell : { Reap, Murder, SoulStrike })
    {
        CastRewards absorbed;
        auto reward = absorbed.Hit(spell,true,false,0,true);
        CHECK_FALSE(reward.ReapPower);
        CHECK(reward.Souls == (spell == Reap ? 0 : 1));
        CHECK(reward.Heal == (spell == SoulStrike));
        CastRewards immune;
        reward = immune.Hit(spell,true,true,0,true);
        CHECK(reward.Souls == 0);
        CHECK_FALSE(reward.Heal);
    }
}

TEST_CASE("Reaper missed and invalid hits produce no resource or healing", "[Reaper]")
{
    for (uint32_t spell : { Reap, Murder, SoulStrike, Soulrend })
    {
        CastRewards missed;
        auto reward = missed.Hit(spell,false,false,100,true);
        CHECK(reward.Fragments == 0);
        CHECK(reward.Souls == 0);
        CHECK_FALSE(reward.Heal);
        CHECK_FALSE(reward.Consume);
        CHECK_FALSE(reward.ReapPower);
        CHECK_FALSE(reward.RendPower);
    }
}

TEST_CASE("Reaper Murder does not grant talent souls without the passive", "[Reaper]")
{
    CastRewards cast;
    auto reward = cast.Hit(Murder,true,false,90,false);
    CHECK(reward.Souls == 0);
    CHECK(reward.Debuff);
}

TEST_CASE("Reaper Soulrend consumes on hit or immunity and energizes only on hit", "[Reaper]")
{
    for (bool immune : {false,true})
    {
        CastRewards cast;
        auto reward = cast.Hit(Soulrend,true,immune,0,true);
        CHECK(reward.Consume);
        CHECK(reward.RendPower == !immune);
        CHECK_FALSE(cast.Hit(Soulrend,true,immune,50,true).Consume);
    }
}

TEST_CASE("Reaper Soul Strike combines landed damage and missing health safely", "[Reaper]")
{
    CHECK(StrikeHealing(100,500,1000) == 130);
    CHECK(StrikeHealing(0,500,1000) == 50);
    CHECK(StrikeHealing(100,1000,1000) == 80);
    CHECK(StrikeHealing(1,999,1000) == 0);
    CHECK(StrikeHealing(100,1100,1000) == 80);
    CHECK(StrikeHealing(UINT32_MAX,0,UINT64_MAX) == INT32_MAX);
}

TEST_CASE("Reaper independent cast instances do not share rewards", "[Reaper]")
{
    CastRewards first, second;
    CHECK(first.Hit(Reap,true,false,1,true).ReapPower);
    CHECK(second.Hit(Reap,true,false,1,true).ReapPower);
    CHECK_FALSE(first.Hit(Reap,true,false,1,true).ReapPower);
}

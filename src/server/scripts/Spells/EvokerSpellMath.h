/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 * Licensed under the GNU General Public License, version 2 or later.
 */

#ifndef TRINITY_EVOKER_SPELL_MATH_H
#define TRINITY_EVOKER_SPELL_MATH_H

#include <array>
#include <cstdint>
#include <deque>

namespace EvokerSpellMath
{
    inline void AccumulateDelayedDamage(std::deque<std::uint64_t>& ticks, std::uint32_t amount, std::uint32_t count)
    {
        if (!count)
            count = 1;
        if (ticks.size() < count)
            ticks.resize(count);
        for (std::uint32_t i = 0; i < count; ++i)
            ticks[i] += amount / count + (i < amount % count ? 1 : 0);
    }

    constexpr std::array<std::uint32_t, 4> SplitDelayedDamage(std::uint32_t amount)
    {
        std::array<std::uint32_t, 4> ticks{};
        for (std::uint32_t i = 0; i < 4; ++i)
            ticks[i] = amount / 4 + (i < amount % 4 ? 1 : 0);
        return ticks;
    }
}

#endif

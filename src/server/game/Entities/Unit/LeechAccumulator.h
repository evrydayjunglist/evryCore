/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef TRINITY_LEECH_ACCUMULATOR_H
#define TRINITY_LEECH_ACCUMULATOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Damage and healing add to a pool that pays out on its own timer.
class LeechAccumulator
{
public:
    void Add(std::uint32_t amount, float percentage)
    {
        if (amount && std::isfinite(percentage) && percentage > 0.0f)
            _pending = std::min(_pending + double(amount) * percentage / 100.0,
                double(std::numeric_limits<std::uint32_t>::max()));
    }

    std::uint32_t Update(std::uint32_t elapsed, std::uint32_t interval)
    {
        if (_pending < 1.0)
        {
            _remaining = 0;
            return 0;
        }

        if (!_remaining)
            _remaining = std::max(interval, 1u);

        if (elapsed < _remaining)
        {
            _remaining -= elapsed;
            return 0;
        }

        std::uint32_t amount = static_cast<std::uint32_t>(_pending);
        _pending -= amount;
        _remaining = 0;
        return amount;
    }

    void Reset()
    {
        _pending = 0.0;
        _remaining = 0;
    }

private:
    double _pending = 0.0;
    std::uint32_t _remaining = 0;
};

#endif

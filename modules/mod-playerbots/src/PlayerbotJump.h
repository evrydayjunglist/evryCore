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

#ifndef EVRY_MOD_PLAYERBOT_JUMP_H
#define EVRY_MOD_PLAYERBOT_JUMP_H

#include <cmath>

struct PlayerbotJumpTrajectory
{
    float HorizontalSpeed = 0.0f;
    float VerticalSpeed = 0.0f;
    float Gravity = 0.0f;

    float HorizontalDistance(float timeSeconds) const
    {
        return HorizontalSpeed * timeSeconds;
    }

    float HeightOffset(float timeSeconds) const
    {
        return VerticalSpeed * timeSeconds - 0.5f * Gravity * timeSeconds * timeSeconds;
    }

    float ApexTime() const
    {
        return Gravity > 0.0f ? VerticalSpeed / Gravity : 0.0f;
    }

    float ApexHeight() const
    {
        float const time = ApexTime();
        return HeightOffset(time);
    }

    float DescendingTimeToHeight(float heightOffset) const
    {
        if (Gravity <= 0.0f)
            return 0.0f;

        float const discriminant = VerticalSpeed * VerticalSpeed - 2.0f * Gravity * heightOffset;
        if (discriminant < 0.0f)
            return 0.0f;

        return (VerticalSpeed + std::sqrt(discriminant)) / Gravity;
    }
};

#endif

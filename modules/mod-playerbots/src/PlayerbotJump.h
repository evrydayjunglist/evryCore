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

#include <algorithm>
#include <cmath>
#include <limits>

// How fast a falling player may drop, the same limits Movement::computeFallElevation uses.
constexpr float PLAYERBOT_TERMINAL_FALL_SPEED = 60.148003f;
constexpr float PLAYERBOT_FEATHER_FALL_TERMINAL_SPEED = 7.0f;

struct PlayerbotJumpTrajectory
{
    float HorizontalSpeed = 0.0f;
    // Upward speed at launch. Positive is up.
    float VerticalSpeed = 0.0f;
    float Gravity = 0.0f;
    // The fastest she falls. Zero leaves the plain arc, which a normal jump never falls long enough to leave.
    float TerminalVelocity = 0.0f;

    float HorizontalDistance(float timeSeconds) const
    {
        return HorizontalSpeed * timeSeconds;
    }

    // Seconds after launch when she reaches TerminalVelocity on the way down.
    float TerminalTime() const
    {
        if (TerminalVelocity <= 0.0f || Gravity <= 0.0f)
            return std::numeric_limits<float>::infinity();

        return std::max(0.0f, (VerticalSpeed + TerminalVelocity) / Gravity);
    }

    float HeightOffset(float timeSeconds) const
    {
        float const terminalTime = TerminalTime();
        if (timeSeconds <= terminalTime)
            return VerticalSpeed * timeSeconds - 0.5f * Gravity * timeSeconds * timeSeconds;

        // Launched already falling faster than the limit: she drops at the limit from the start.
        float const reached = terminalTime > 0.0f
            ? VerticalSpeed * terminalTime - 0.5f * Gravity * terminalTime * terminalTime
            : 0.0f;
        return reached - TerminalVelocity * (timeSeconds - terminalTime);
    }

    // Positive is up.
    float VerticalVelocity(float timeSeconds) const
    {
        if (timeSeconds >= TerminalTime())
            return -TerminalVelocity;

        return VerticalSpeed - Gravity * timeSeconds;
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

    // Plain arc only: this ignores TerminalVelocity, which a normal jump never reaches.
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

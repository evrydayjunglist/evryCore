/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_BAD_PLACES_H
#define EVRY_PLAYERBOT_BAD_PLACES_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// She remembers this many places where a walk of hers failed. A new one pushes out the oldest.
inline constexpr std::size_t PLAYERBOT_BAD_PLACES_KEPT = 8;
// A route meets a bad place when it passes this close to it, side to side.
inline constexpr float PLAYERBOT_BAD_PLACE_YARDS = 3.0f;
// ... and this close up and down, so a route over a bridge or through a cave under the place does not meet it.
inline constexpr float PLAYERBOT_BAD_PLACE_HEIGHT_YARDS = 4.0f;
// ... going within this many degrees of the way she was going when it refused her. The same slope walked down, or
// crossed sideways, is a different step.
inline constexpr float PLAYERBOT_BAD_PLACE_HEADING_DEGREES = 60.0f;
// After this long she tries a place again: a spawn may have moved, or she may come at it from new feet.
inline constexpr uint64_t PLAYERBOT_BAD_PLACE_FORGET_MS = 10 * 60 * 1000;

struct PlayerbotBadPlace
{
    uint32_t MapId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    // The way she was going when it refused her, flat and one yard long.
    float DirX = 0.0f;
    float DirY = 0.0f;
    uint64_t FailedAtMs = 0;
    // How many walks of hers have failed here.
    uint32_t Failures = 0;
};

// Where her walks failed after every way round had been tried: a lip, a face, or a wall she could not get past going
// that way. A player who has been stopped by the same hillside once does not walk straight back up it a minute later.
// A new route that would take her past one of these places, going the same way, is given up before she walks it, and
// her brain picks other work. These are her own memories, by map and place; they are not shared with other bots, and
// they are not the short skip list of targets that a quest click or a map-marker arrival wipes. No server
// dependencies, so it is tested with made-up routes.
class PlayerbotBadPlaces
{
public:
    // A walk failed at (x, y, z) going the way (dirX, dirY). The same place again adds to its count and starts its
    // time again.
    void Note(uint32_t mapId, float x, float y, float z, float dirX, float dirY, uint64_t nowMs)
    {
        float const length = std::sqrt(dirX * dirX + dirY * dirY);
        if (length < 0.0001f)
            return;
        dirX /= length;
        dirY /= length;

        Forget(nowMs);
        for (PlayerbotBadPlace& place : _places)
        {
            if (place.MapId != mapId || !Near(place, x, y, z) || !SameWay(place, dirX, dirY))
                continue;
            place.FailedAtMs = nowMs;
            ++place.Failures;
            return;
        }

        if (_places.size() >= PLAYERBOT_BAD_PLACES_KEPT)
            _places.erase(std::min_element(_places.begin(), _places.end(),
                [](PlayerbotBadPlace const& a, PlayerbotBadPlace const& b) { return a.FailedAtMs < b.FailedAtMs; }));

        PlayerbotBadPlace place;
        place.MapId = mapId;
        place.X = x;
        place.Y = y;
        place.Z = z;
        place.DirX = dirX;
        place.DirY = dirY;
        place.FailedAtMs = nowMs;
        place.Failures = 1;
        _places.push_back(place);
    }

    // The first bad place this route passes going the way that failed, or null. Point is anything with x, y and z,
    // such as a navmesh route point.
    template <typename Point>
    PlayerbotBadPlace const* RouteMeets(uint32_t mapId, std::vector<Point> const& route, uint64_t nowMs)
    {
        Forget(nowMs);
        if (_places.empty() || route.size() < 2)
            return nullptr;

        for (std::size_t i = 1; i < route.size(); ++i)
        {
            Point const& a = route[i - 1];
            Point const& b = route[i];
            float const dx = b.x - a.x;
            float const dy = b.y - a.y;
            float const flat = std::sqrt(dx * dx + dy * dy);
            if (flat < 0.0001f)
                continue;

            for (PlayerbotBadPlace const& place : _places)
            {
                if (place.MapId != mapId || !SameWay(place, dx / flat, dy / flat))
                    continue;

                float const along = std::clamp(((place.X - a.x) * dx + (place.Y - a.y) * dy) / (flat * flat), 0.0f, 1.0f);
                float const nearX = a.x + dx * along;
                float const nearY = a.y + dy * along;
                float const nearZ = a.z + (b.z - a.z) * along;
                float const sideX = place.X - nearX;
                float const sideY = place.Y - nearY;
                if (sideX * sideX + sideY * sideY <= PLAYERBOT_BAD_PLACE_YARDS * PLAYERBOT_BAD_PLACE_YARDS
                    && std::fabs(place.Z - nearZ) <= PLAYERBOT_BAD_PLACE_HEIGHT_YARDS)
                    return &place;
            }
        }

        return nullptr;
    }

    // Drop places she has not failed at for PLAYERBOT_BAD_PLACE_FORGET_MS.
    void Forget(uint64_t nowMs)
    {
        _places.erase(std::remove_if(_places.begin(), _places.end(), [nowMs](PlayerbotBadPlace const& place)
        {
            return nowMs >= place.FailedAtMs + PLAYERBOT_BAD_PLACE_FORGET_MS;
        }), _places.end());
    }

    void Clear() { _places.clear(); }
    std::size_t Size() const { return _places.size(); }

private:
    static bool Near(PlayerbotBadPlace const& place, float x, float y, float z)
    {
        float const dx = place.X - x;
        float const dy = place.Y - y;
        return dx * dx + dy * dy <= PLAYERBOT_BAD_PLACE_YARDS * PLAYERBOT_BAD_PLACE_YARDS
            && std::fabs(place.Z - z) <= PLAYERBOT_BAD_PLACE_HEIGHT_YARDS;
    }

    static bool SameWay(PlayerbotBadPlace const& place, float dirX, float dirY)
    {
        static float const minCos = std::cos(PLAYERBOT_BAD_PLACE_HEADING_DEGREES * 3.14159265f / 180.0f);
        return place.DirX * dirX + place.DirY * dirY >= minCos;
    }

    std::vector<PlayerbotBadPlace> _places;
};

#endif

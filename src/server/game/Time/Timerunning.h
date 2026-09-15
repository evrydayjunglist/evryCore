/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITY_TIMERUNNING_H
#define TRINITY_TIMERUNNING_H

#include "Define.h"
#include "Optional.h"
#include <mutex>
#include <string>
#include <string_view>

namespace Timerunning
{
enum class Season : int32
{
    None = 0,
    Pandaria = 1,
    Legion = 2
};

constexpr bool IsKnownSeason(int32 season)
{
    return season >= int32(Season::None) && season <= int32(Season::Legion);
}

// Each event is opened separately after its required content has loaded.
constexpr bool IsPlayableSeason(int32 season)
{
    return season == int32(Season::Pandaria);
}

constexpr bool CanShareGameplay(int32 firstSeason, int32 secondSeason)
{
    return IsKnownSeason(firstSeason) && firstSeason == secondSeason;
}

constexpr bool CanEnterMap(int32 season, uint32 mapId, bool isWorldMap, bool isGarrison)
{
    return season == int32(Season::None) ||
        (season == int32(Season::Pandaria) && mapId == 870 && isWorldMap && !isGarrison);
}

constexpr uint8 AllSeasonMask = 0x07;

constexpr bool CanUseSpawnGroup(int32 season, uint8 seasonMask)
{
    return IsKnownSeason(season) && !(seasonMask & ~AllSeasonMask) && (seasonMask & (1u << season));
}

// Ordinary worlds use instance 0, or faction instances 0 through 2. Keep those ids stable.
constexpr Optional<uint32> GetWorldInstanceId(int32 season, bool splitByFaction, uint32 teamId)
{
    if (!IsKnownSeason(season) || (splitByFaction && teamId > 2))
        return {};

    return uint32(season) * 3 + (splitByFaction ? teamId : 0);
}

struct State
{
    Season ActiveSeason = Season::None;
    int64 EndTime = 0;
    int32 RemainingSeconds = 0;
    bool ContentReady = false;

    bool IsEnabled() const { return ContentReady && IsPlayableSeason(int32(ActiveSeason)); }
    bool CanEnterWorld(int32 requestedSeason) const
    {
        return requestedSeason == int32(Season::None) ||
            (IsEnabled() && requestedSeason == int32(ActiveSeason) && RemainingSeconds > 0);
    }
    bool CanCreateCharacter(int32 requestedSeason) const { return CanEnterWorld(requestedSeason); }
};

class TC_GAME_API Schedule
{
public:
    // Invalid changes leave the previous schedule intact. Times are UTC, independent of local DST.
    bool Configure(std::string_view season, std::string_view startTime, std::string_view durationDays, std::string& error);
    State GetState(int64 now) const;
    Season GetConfiguredSeason() const { return _season; }

private:
    Season _season = Season::None;
    int64 _startTime = 0;
    int64 _endTime = 0;
};

class TC_GAME_API Manager
{
public:
    static Manager* instance();
    void LoadConfig();
    State GetState() const;
    void SetPandariaContentReady(bool ready);

private:
    mutable std::mutex _mutex;
    Schedule _schedule;
    bool _pandariaContentReady = false;
};
}

#define sTimerunningMgr Timerunning::Manager::instance()

#endif

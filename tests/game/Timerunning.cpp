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

#include "tc_catch2.h"
#include "CharacterCache.h"
#include "Group.h"
#include "RaceMask.h"
#include "Timerunning.h"
#include <limits>
#include <set>

using Timerunning::Schedule;
using Timerunning::Season;

TEST_CASE("Timerunning world copies preserve ordinary maps and never share a seasonal instance", "[Timerunning]")
{
    REQUIRE(Timerunning::GetWorldInstanceId(0, false, 0) == 0u);
    REQUIRE(Timerunning::GetWorldInstanceId(0, false, 1) == 0u);
    REQUIRE(Timerunning::GetWorldInstanceId(0, false, 2) == 0u);
    REQUIRE(Timerunning::GetWorldInstanceId(0, true, 0) == 0u);
    REQUIRE(Timerunning::GetWorldInstanceId(0, true, 1) == 1u);
    REQUIRE(Timerunning::GetWorldInstanceId(0, true, 2) == 2u);

    std::set<uint32> instances;
    for (int32 season : { 0, 1, 2 })
    {
        for (uint32 team : { 0u, 1u, 2u })
        {
            auto instance = Timerunning::GetWorldInstanceId(season, true, team);
            REQUIRE(instance);
            REQUIRE(instances.insert(*instance).second);
            REQUIRE(Timerunning::GetWorldInstanceId(season, false, team) == Timerunning::GetWorldInstanceId(season, false, 0));
        }
    }

    for (int32 invalid : { -1, 3, std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() })
    {
        REQUIRE_FALSE(Timerunning::GetWorldInstanceId(invalid, false, 0));
        REQUIRE_FALSE(Timerunning::GetWorldInstanceId(invalid, true, 1));
    }
    REQUIRE_FALSE(Timerunning::GetWorldInstanceId(1, true, 3));
    REQUIRE_FALSE(Timerunning::GetWorldInstanceId(0, true, std::numeric_limits<uint32>::max()));
}

TEST_CASE("Timerunning interactions require the same known character season", "[Timerunning]")
{
    for (int32 first : { -1, 0, 1, 2, 3, std::numeric_limits<int32>::max() })
        for (int32 second : { -1, 0, 1, 2, 3, std::numeric_limits<int32>::max() })
        {
            bool compatible = Timerunning::CanShareGameplay(first, second);
            CAPTURE(first, second);
            REQUIRE(compatible == Timerunning::CanShareGameplay(second, first));
            if (first >= 0 && first <= 2 && first == second)
                REQUIRE(compatible);
            else
                REQUIRE_FALSE(compatible);
        }
}

TEST_CASE("Timerunning group admission checks offline members and rechecks a changed season", "[Timerunning]")
{
    class TestGroup : public Group
    {
    public:
        void SetLeader(ObjectGuid guid) { m_leaderGuid = guid; }
        void AddOfflineMember(ObjectGuid guid)
        {
            MemberSlot member{};
            member.guid = guid;
            m_memberSlots.push_back(member);
        }
    };

    ObjectGuid leader = ObjectGuid::Create<HighGuid::Player>(1000000001);
    ObjectGuid member = ObjectGuid::Create<HighGuid::Player>(1000000002);
    REQUIRE_FALSE(sCharacterCache->GetCharacterCacheByGuid(leader));
    REQUIRE_FALSE(sCharacterCache->GetCharacterCacheByGuid(member));
    struct Cleanup
    {
        ObjectGuid Leader;
        ObjectGuid Member;
        ~Cleanup()
        {
            sCharacterCache->DeleteCharacterCacheEntry(Leader, "TimerunningTestLeader");
            sCharacterCache->DeleteCharacterCacheEntry(Member, "TimerunningTestMember");
        }
    } cleanup{ leader, member };

    sCharacterCache->AddCharacterCacheEntry(leader, 1, "TimerunningTestLeader", 0, 2, 1, 10, false, 1);
    sCharacterCache->AddCharacterCacheEntry(member, 1, "TimerunningTestMember", 0, 2, 1, 10, false, 1);

    TestGroup group;
    REQUIRE(group.CanJoinTimerunningSeason(0));
    REQUIRE(group.CanJoinTimerunningSeason(1));
    REQUIRE_FALSE(group.CanJoinTimerunningSeason(-1));
    group.SetLeader(leader);
    group.AddOfflineMember(member);
    REQUIRE(group.CanJoinTimerunningSeason(1));
    REQUIRE_FALSE(group.CanJoinTimerunningSeason(0));
    REQUIRE_FALSE(group.CanJoinTimerunningSeason(2));

    sCharacterCache->UpdateCharacterTimerunningSeason(member, 0);
    REQUIRE_FALSE(group.CanJoinTimerunningSeason(1));
    REQUIRE_FALSE(group.CanJoinTimerunningSeason(0));
    sCharacterCache->UpdateCharacterTimerunningSeason(leader, 0);
    REQUIRE(group.CanJoinTimerunningSeason(0));
    REQUIRE_FALSE(group.CanJoinTimerunningSeason(1));

    sCharacterCache->DeleteCharacterCacheEntry(member, "TimerunningTestMember");
    REQUIRE_FALSE(group.CanJoinTimerunningSeason(0));
}

namespace
{
    constexpr int64 Day = 86400;
    constexpr int64 Start = 1790812800; // 2026-10-01 00:00:00 UTC
}

TEST_CASE("Timerunning has no active season by default", "[Timerunning]")
{
    Schedule schedule;
    auto state = schedule.GetState(Start);
    REQUIRE(state.ActiveSeason == Season::None);
    REQUIRE(state.RemainingSeconds == 0);
    REQUIRE(state.EndTime == 0);
    REQUIRE(state.CanCreateCharacter(0));
    REQUIRE_FALSE(state.IsEnabled());
}

TEST_CASE("Timerunning observes the exact UTC season boundaries across restart", "[Timerunning]")
{
    Schedule schedule;
    std::string error;
    REQUIRE(schedule.Configure("Legion", "2026-10-01 00:00:00", "90", error));
    REQUIRE(error.empty());
    REQUIRE(schedule.GetState(Start - 1).ActiveSeason == Season::None);
    REQUIRE(schedule.GetState(Start).ActiveSeason == Season::Legion);
    REQUIRE(schedule.GetState(Start).RemainingSeconds == 90 * Day);
    REQUIRE(schedule.GetState(Start + 90 * Day - 1).RemainingSeconds == 1);
    REQUIRE(schedule.GetState(Start + 90 * Day).ActiveSeason == Season::None);
    REQUIRE(schedule.GetState(std::numeric_limits<int64>::max()).RemainingSeconds == 0);
    REQUIRE(schedule.GetState(std::numeric_limits<int64>::min()).ActiveSeason == Season::None);

    Schedule restarted;
    REQUIRE(restarted.Configure("Legion", "2026-10-01 00:00:00", "90", error));
    REQUIRE(restarted.GetState(Start + 12 * Day).EndTime == schedule.GetState(Start).EndTime);
    REQUIRE(restarted.GetState(Start + 12 * Day).RemainingSeconds == 78 * Day);
}

TEST_CASE("Timerunning rejects malformed configuration without replacing the schedule", "[Timerunning]")
{
    Schedule schedule;
    std::string error;
    REQUIRE(schedule.Configure("Pandaria", "2026-10-01 00:00:00", "1", error));

    for (std::string_view badSeason : { "", "3", "Unknown", "Legion Remix" })
    {
        CAPTURE(badSeason);
        REQUIRE_FALSE(schedule.Configure(badSeason, "2026-10-01 00:00:00", "90", error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(schedule.GetState(Start).ActiveSeason == Season::Pandaria);
    }

    for (std::string_view badStart : { "", "2026-02-29 00:00:00", "2026-04-31 00:00:00", "2026-13-01 00:00:00",
        "2026-10-00 00:00:00", "1969-12-31 23:59:59", "2026-10-01 24:00:00", "2026-10-01 00:60:00",
        "2026-10-01 00:00:60", "2026-10-01T00:00:00", "2026-10-01 00:00:00Z", "2026-+1-01 00:00:00" })
    {
        CAPTURE(badStart);
        REQUIRE_FALSE(schedule.Configure("Legion", badStart, "90", error));
        REQUIRE(schedule.GetState(Start).EndTime == Start + Day);
    }

    for (std::string_view badDuration : { "", "0", "-1", "1.5", "90days", "24856", "2147483648", " 90", "+90" })
    {
        CAPTURE(badDuration);
        REQUIRE_FALSE(schedule.Configure("Legion", "2026-10-01 00:00:00", badDuration, error));
        REQUIRE(schedule.GetState(Start).RemainingSeconds == Day);
    }
}

TEST_CASE("Timerunning supports leap days and a bounded wire countdown", "[Timerunning]")
{
    Schedule schedule;
    std::string error;
    REQUIRE(schedule.Configure("Pandaria", "2024-02-29 00:00:00", "1", error));
    REQUIRE(schedule.GetState(1709164800).RemainingSeconds == Day);
    REQUIRE(schedule.GetState(1709251200).ActiveSeason == Season::None);
    REQUIRE(schedule.Configure("Legion", "2026-10-01 00:00:00", "24855", error));
    REQUIRE(schedule.GetState(Start).RemainingSeconds == 24855 * Day);
}

TEST_CASE("Timerunning None stops the schedule without requiring stale dates to be valid", "[Timerunning]")
{
    Schedule schedule;
    std::string error;
    REQUIRE(schedule.Configure("Legion", "2026-10-01 00:00:00", "90", error));
    REQUIRE(schedule.Configure("None", "", "invalid", error));
    REQUIRE(error.empty());
    REQUIRE(schedule.GetConfiguredSeason() == Season::None);
    REQUIRE(schedule.GetState(Start).ActiveSeason == Season::None);
    REQUIRE(schedule.GetState(Start).RemainingSeconds == 0);
}

TEST_CASE("Timerunning schedules cannot enable unimplemented character gameplay", "[Timerunning]")
{
    for (std::string_view season : { "None", "none", "Pandaria", "pAnDaRiA", "Legion", "legion" })
    {
        Schedule schedule;
        std::string error;
        REQUIRE(schedule.Configure(season, "2026-10-01 00:00:00", "90", error));
        for (int64 now : { Start - 1, Start, Start + 90 * Day })
        {
            auto state = schedule.GetState(now);
            REQUIRE(state.CanCreateCharacter(0));
            REQUIRE_FALSE(state.IsEnabled());
            for (int32 requested : { -1, 1, 2, 3, std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max() })
            {
                CAPTURE(season, now, requested);
                REQUIRE_FALSE(state.CanCreateCharacter(requested));
                REQUIRE_FALSE(state.CanEnterWorld(requested));
            }
        }
    }
    REQUIRE(Timerunning::State{}.CanEnterWorld(0));
    REQUIRE(Timerunning::IsKnownSeason(1));
    REQUIRE(Timerunning::IsKnownSeason(2));
    REQUIRE_FALSE(Timerunning::IsKnownSeason(-1));
    REQUIRE_FALSE(Timerunning::IsKnownSeason(3));
}

TEST_CASE("Timerunning admission requires ready content and the matching unexpired season", "[Timerunning]")
{
    Schedule schedule;
    std::string error;
    REQUIRE(schedule.Configure("Pandaria", "2026-10-01 00:00:00", "1", error));
    for (int64 now : { Start - 1, Start, Start + Day - 1, Start + Day })
        for (bool ready : { false, true })
        {
            auto state = schedule.GetState(now);
            state.ContentReady = ready;
            REQUIRE(state.CanEnterWorld(0));
            REQUIRE(state.CanCreateCharacter(0));
            REQUIRE(state.CanEnterWorld(1) == (ready && now >= Start && now < Start + Day));
            REQUIRE(state.CanCreateCharacter(1) == state.CanEnterWorld(1));
            REQUIRE_FALSE(state.CanEnterWorld(2));
            REQUIRE_FALSE(state.CanCreateCharacter(-1));
            REQUIRE_FALSE(state.CanCreateCharacter(std::numeric_limits<int32>::max()));
        }
    REQUIRE(schedule.Configure("Legion", "2026-10-01 00:00:00", "1", error));
    auto state = schedule.GetState(Start);
    state.ContentReady = true;
    REQUIRE_FALSE(state.IsEnabled());
    REQUIRE_FALSE(state.CanEnterWorld(1));
    REQUIRE_FALSE(state.CanEnterWorld(2));
}

TEST_CASE("Timerunning spawn masks distinguish shared data from shared world state", "[Timerunning]")
{
    REQUIRE(Timerunning::CanUseSpawnGroup(0, 1));
    REQUIRE_FALSE(Timerunning::CanUseSpawnGroup(1, 1));
    REQUIRE(Timerunning::CanUseSpawnGroup(1, 2));
    REQUIRE_FALSE(Timerunning::CanUseSpawnGroup(0, 2));
    REQUIRE(Timerunning::CanUseSpawnGroup(0, 3));
    REQUIRE(Timerunning::CanUseSpawnGroup(1, 3));
    REQUIRE_FALSE(Timerunning::CanUseSpawnGroup(2, 3));
    for (int32 season : { -1, 0, 1, 2, 3, std::numeric_limits<int32>::max() })
    {
        REQUIRE_FALSE(Timerunning::CanUseSpawnGroup(season, 0));
        for (uint32 mask = 8; mask <= 255; ++mask)
            REQUIRE_FALSE(Timerunning::CanUseSpawnGroup(season, uint8(mask)));
    }
}

TEST_CASE("Timerunning map entry excludes ordinary continents and unimplemented instance modes", "[Timerunning]")
{
    REQUIRE(Timerunning::CanEnterMap(1, 870, true, false));
    REQUIRE_FALSE(Timerunning::CanEnterMap(1, 870, false, false));
    REQUIRE_FALSE(Timerunning::CanEnterMap(1, 870, true, true));
    for (uint32 mapId : { 0, 1, 870, 960, 2927 })
    {
        REQUIRE(Timerunning::CanEnterMap(0, mapId, false, false));
        REQUIRE_FALSE(Timerunning::CanEnterMap(-1, mapId, true, false));
        REQUIRE_FALSE(Timerunning::CanEnterMap(2, mapId, true, false));
        REQUIRE_FALSE(Timerunning::CanEnterMap(3, mapId, true, false));
        if (mapId != 870)
            REQUIRE_FALSE(Timerunning::CanEnterMap(1, mapId, true, false));
    }
}

TEST_CASE("Timerunning cache preserves a character season independently of schedule and deletion", "[Timerunning]")
{
    CharacterCache cache;
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(123456789);
    cache.AddCharacterCacheEntry(guid, 123, "Seasonal", 0, RACE_ORC, CLASS_WARRIOR, 10, false, 1);
    REQUIRE(cache.GetCharacterCacheByGuid(guid)->TimerunningSeasonId == 1);
    cache.UpdateCharacterInfoDeleted(guid, true, "");
    REQUIRE(cache.GetCharacterCacheByGuid(guid)->TimerunningSeasonId == 1);
    cache.UpdateCharacterInfoDeleted(guid, false, "Seasonal");
    REQUIRE(cache.GetCharacterCacheByGuid(guid)->TimerunningSeasonId == 1);
    cache.UpdateCharacterTimerunningSeason(guid, 2);
    REQUIRE(cache.GetCharacterCacheByGuid(guid)->TimerunningSeasonId == 2);
    cache.DeleteCharacterCacheEntry(guid, "Seasonal");
    cache.AddCharacterCacheEntry(guid, 123, "Standard", 0, RACE_ORC, CLASS_WARRIOR, 1, false);
    REQUIRE(cache.GetCharacterCacheByGuid(guid)->TimerunningSeasonId == 0);
    cache.DeleteCharacterCacheEntry(guid, "Standard");
}

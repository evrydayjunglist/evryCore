/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PlayerbotWalkMapper.h"
#include "Chat.h"
#include "DB2Stores.h"
#include "GridDefines.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotMovement.h"
#include "PlayerbotWalkMap.h"
#include "PlayerbotWalkMapPage.h"
#include "PlayerbotWalkMapServerWorld.h"
#include "Playerbots.h"
#include "StringFormat.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
    constexpr float WALK_MAP_MIN_SPACING = 0.5f;
    constexpr float WALK_MAP_MAX_SPACING = 2.0f;
    // Floors in one spot closer together than this are one floor.
    constexpr float WALK_MAP_LAYER_YARDS = 1.0f;
    constexpr std::size_t WALK_MAP_MAX_SPOTS = 600000;
    // World-thread time one tick may spend on the map.
    constexpr std::chrono::milliseconds WALK_MAP_SLICE{ 10 };
    constexpr std::size_t WALK_MAP_SPOTS_PER_CLOCK_CHECK = 8;

    std::string PlaceName(Player* player, float x, float y, float z)
    {
        uint32 zoneId = 0;
        uint32 areaId = 0;
        player->GetMap()->GetZoneAndAreaId(player->GetPhaseShift(), zoneId, areaId, x, y, z);
        LocaleConstant const locale = sWorld->GetDefaultDbcLocale();
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
        AreaTableEntry const* area = areaId != zoneId ? sAreaTableStore.LookupEntry(areaId) : nullptr;
        char const* const zoneText = zone ? zone->AreaName[locale] : nullptr;
        char const* const areaText = area ? area->AreaName[locale] : nullptr;
        std::string const zoneName = zoneText ? zoneText : "";
        std::string const areaName = areaText ? areaText : "";
        if (areaName.empty())
            return zoneName;
        if (zoneName.empty())
            return areaName;
        return areaName + ", " + zoneName;
    }

    std::string LocalTimeText(time_t time)
    {
        tm local{};
        localtime_r(&time, &local);
        return Trinity::StringFormat("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
            local.tm_hour, local.tm_min, local.tm_sec);
    }
}

struct PlayerbotWalkMapper::Job
{
    ObjectGuid Subject;
    ObjectGuid Requester;
    uint32 MapId = 0;
    uint32 InstanceId = 0;
    time_t Made = 0;
    std::chrono::steady_clock::time_point Started;
    std::chrono::steady_clock::duration Work = std::chrono::steady_clock::duration::zero();
    std::unique_ptr<PlayerbotWalkMap> Graph;
    PlayerbotWalkMapReport Report;
};

PlayerbotWalkMapper::PlayerbotWalkMapper() = default;

PlayerbotWalkMapper::~PlayerbotWalkMapper() = default;

bool PlayerbotWalkMapper::Start(Player* subject, Position const& feet, Position const* walkDestination, float radius,
    ObjectGuid requester, std::string& message)
{
    if (_job)
    {
        message = Trinity::StringFormat("A walk map of {} is still being made. Try again when it is done.", _job->Report.Name);
        return false;
    }

    if (!subject || !subject->IsInWorld() || !subject->FindMap())
    {
        message = "That player is not in the world.";
        return false;
    }

    if (subject->GetTransport() || subject->IsInFlight() || subject->GetVehicle())
    {
        message = Trinity::StringFormat("{} is on a transport, a flight path, or a vehicle, where her walk rules do not apply.",
            subject->GetName());
        return false;
    }

    float x = feet.GetPositionX();
    float y = feet.GetPositionY();
    float z = feet.GetPositionZ();
    if (!Trinity::IsValidMapCoord(x, y, z))
    {
        message = Trinity::StringFormat("{} is not standing at a valid position.", subject->GetName());
        return false;
    }

    // A walk starts from her feet planted this way.
    subject->UpdateAllowedPositionZ(x, y, z);
    if (!Trinity::IsValidMapCoord(x, y, z))
    {
        message = Trinity::StringFormat("There is no floor under {}.", subject->GetName());
        return false;
    }

    PlayerbotWalkMapSettings settings;
    settings.OriginX = x;
    settings.OriginY = y;
    settings.OriginZ = z;
    settings.Spacing = std::clamp(PlayerbotWalker::HeartbeatStepLength(subject), WALK_MAP_MIN_SPACING, WALK_MAP_MAX_SPACING);
    settings.Radius = std::clamp(radius, PLAYERBOT_WALK_MAP_MIN_YARDS, PLAYERBOT_WALK_MAP_MAX_YARDS);
    settings.MaxClimbDegrees = PlayerbotWalker::MaxWalkableSlopeDegrees;
    settings.MaxDropYards = PlayerbotWalker::MaxDownStepYards;
    settings.LayerYards = WALK_MAP_LAYER_YARDS;
    settings.MaxSpots = WALK_MAP_MAX_SPOTS;

    std::unique_ptr<Job> job = std::make_unique<Job>();
    job->Subject = subject->GetGUID();
    job->Requester = requester;
    job->MapId = subject->GetMapId();
    job->InstanceId = subject->GetInstanceId();
    job->Made = time(nullptr);
    job->Started = std::chrono::steady_clock::now();
    job->Graph = std::make_unique<PlayerbotWalkMap>(settings);
    job->Report.Name = subject->GetName();
    job->Report.Place = PlaceName(subject, x, y, z);
    job->Report.Made = LocalTimeText(job->Made);
    job->Report.MapId = subject->GetMapId();

    if (walkDestination)
    {
        job->Report.HasDestination = true;
        job->Report.Destination = { walkDestination->GetPositionX(), walkDestination->GetPositionY(), walkDestination->GetPositionZ() };
        // The same navmesh query her walks start from.
        PathGenerator path(subject);
        path.CalculatePath(x, y, z, walkDestination->GetPositionX(), walkDestination->GetPositionY(),
            walkDestination->GetPositionZ(), false);
        job->Report.RouteType = uint32(path.GetPathType());
        for (G3D::Vector3 const& point : path.GetPath())
            job->Report.Route.push_back({ point.x, point.y, point.z });
    }

    message = Trinity::StringFormat(
        "Making a walk map of {}: {:.0f} yards around ({:.1f}, {:.1f}, {:.1f}) in steps of {:.2f} yards. The result comes here when it is done, and the picture is written next to the server logs.",
        job->Report.Name, settings.Radius, x, y, z, settings.Spacing);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: walk map of {} started at ({:.2f}, {:.2f}, {:.2f}) on map {}: {:.0f} yards around, steps of {:.2f} yards.",
        job->Report.Name, x, y, z, job->MapId, settings.Radius, settings.Spacing);

    _job = std::move(job);
    return true;
}

void PlayerbotWalkMapper::Update(uint32 /*diff*/)
{
    if (!_job)
        return;

    Job& job = *_job;
    Player* subject = ObjectAccessor::FindPlayer(job.Subject);
    if (!subject || !subject->FindMap() || subject->GetMapId() != job.MapId || subject->GetInstanceId() != job.InstanceId)
    {
        std::string const text = Trinity::StringFormat("The walk map of {} stopped: she is no longer on the map it was being made on.",
            job.Report.Name);
        Tell(job, text);
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {}", text);
        _job.reset();
        return;
    }

    PlayerbotWalkMapServerWorld world(subject, subject->GetMap());
    std::chrono::steady_clock::time_point const sliceStart = std::chrono::steady_clock::now();
    bool finished = false;
    do
        finished = job.Graph->Advance(world, WALK_MAP_SPOTS_PER_CLOCK_CHECK);
    while (!finished && std::chrono::steady_clock::now() - sliceStart < WALK_MAP_SLICE);
    job.Work += std::chrono::steady_clock::now() - sliceStart;

    if (!finished)
        return;

    Finish(job);
    _job.reset();
}

void PlayerbotWalkMapper::Finish(Job& job)
{
    PlayerbotWalkMap const& map = *job.Graph;
    job.Report.Seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - job.Started).count();
    job.Report.WorkSeconds = std::chrono::duration<float>(job.Work).count();
    PlayerbotWalkMapSummary const summary = map.Summarize();
    std::vector<std::string> const lines = DescribePlayerbotWalkMap(map, summary, job.Report);

    std::string const fileName = Trinity::StringFormat("WalkMap-{}-{}.html", job.Report.Name, TimeToTimestampStr(job.Made));
    std::filesystem::path file = std::filesystem::path(sLog->GetLogsDir()) / fileName;
    std::error_code error;
    std::filesystem::path const absolute = std::filesystem::absolute(file, error);
    if (!error)
        file = absolute;

    std::string const page = BuildPlayerbotWalkMapPage(map, job.Report, lines);
    bool written = false;
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        if (stream)
        {
            stream.write(page.data(), std::streamsize(page.size()));
            written = bool(stream);
        }
    }

    std::string const where = written
        ? Trinity::StringFormat("The picture is {}", file.string())
        : Trinity::StringFormat("The picture could not be written to {}", file.string());

    // The first line repeats the rules and the last the timing; the chat reply leaves both to the page.
    Tell(job, Trinity::StringFormat("Walk map of {} finished.", job.Report.Name));
    for (std::size_t line = 1; line + 1 < lines.size(); ++line)
        Tell(job, lines[line]);
    Tell(job, where);

    std::string logText;
    for (std::string const& line : lines)
    {
        if (!logText.empty())
            logText += ' ';
        logText += line;
    }
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: walk map of {} finished. {} {}", job.Report.Name, logText, where);
}

void PlayerbotWalkMapper::Tell(Job const& job, std::string const& text)
{
    if (job.Requester.IsEmpty())
        return;

    Player* requester = ObjectAccessor::FindConnectedPlayer(job.Requester);
    if (!requester || !requester->GetSession())
        return;

    ChatHandler(requester->GetSession()).SendSysMessage(text);
}

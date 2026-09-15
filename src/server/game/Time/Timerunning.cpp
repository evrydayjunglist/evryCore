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

#include "Timerunning.h"
#include "Config.h"
#include "GameTime.h"
#include "Log.h"
#include "StringConvert.h"
#include <charconv>
#include <chrono>
#include <limits>

namespace
{
bool ParseNumber(std::string_view text, int32& number)
{
    if (text.empty())
        return false;

    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    return error == std::errc() && end == text.data() + text.size();
}

bool ParseUtcTime(std::string_view text, int64& timestamp)
{
    if (text.size() != 19 || text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != ':' || text[16] != ':')
        return false;

    for (std::size_t i = 0; i < text.size(); ++i)
        if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && (text[i] < '0' || text[i] > '9'))
            return false;

    int32 year, month, day, hour, minute, second;
    if (!ParseNumber(text.substr(0, 4), year) || !ParseNumber(text.substr(5, 2), month) ||
        !ParseNumber(text.substr(8, 2), day) || !ParseNumber(text.substr(11, 2), hour) ||
        !ParseNumber(text.substr(14, 2), minute) || !ParseNumber(text.substr(17, 2), second))
        return false;

    std::chrono::year_month_day date{ std::chrono::year{ year }, std::chrono::month{ uint32(month) }, std::chrono::day{ uint32(day) } };
    if (year < 1970 || !date.ok() || hour > 23 || minute > 59 || second > 59)
        return false;

    timestamp = std::chrono::duration_cast<Seconds>(std::chrono::sys_days{ date }.time_since_epoch() +
        Hours{ hour } + Minutes{ minute } + Seconds{ second }).count();
    return true;
}
}

bool Timerunning::Schedule::Configure(std::string_view season, std::string_view startTime, std::string_view durationDays, std::string& error)
{
    error.clear();
    Season selectedSeason;
    if (StringEqualI(season, "None"))
    {
        *this = Schedule{};
        return true;
    }
    if (StringEqualI(season, "Pandaria"))
        selectedSeason = Season::Pandaria;
    else if (StringEqualI(season, "Legion"))
        selectedSeason = Season::Legion;
    else
    {
        error = "Timerunning.Season must be None, Pandaria or Legion";
        return false;
    }

    int64 start;
    if (!ParseUtcTime(startTime, start))
    {
        error = "Timerunning.StartTime must be a valid UTC date in YYYY-MM-DD HH:MM:SS format, with year 1970 or later";
        return false;
    }

    constexpr int32 secondsPerDay = 24 * 60 * 60;
    int32 days;
    if (!ParseNumber(durationDays, days) || days < 1 || days > std::numeric_limits<int32>::max() / secondsPerDay)
    {
        error = "Timerunning.DurationDays must be a whole number from 1 to 24855";
        return false;
    }

    _season = selectedSeason;
    _startTime = start;
    _endTime = start + int64(days) * secondsPerDay;
    return true;
}

Timerunning::State Timerunning::Schedule::GetState(int64 now) const
{
    if (_season == Season::None || now < _startTime || now >= _endTime)
        return {};

    return { _season, _endTime, int32(_endTime - now) };
}

Timerunning::Manager* Timerunning::Manager::instance()
{
    static Manager instance;
    return &instance;
}

void Timerunning::Manager::LoadConfig()
{
    std::string season = sConfigMgr->GetStringDefault("Timerunning.Season", "None");
    std::string start = sConfigMgr->GetStringDefault("Timerunning.StartTime", "");
    std::string days = sConfigMgr->GetStringDefault("Timerunning.DurationDays", "90");
    std::string error;
    {
        std::lock_guard lock(_mutex);
        if (!_schedule.Configure(season, start, days, error))
        {
            TC_LOG_ERROR("server.loading", "{}. Keeping the previous Timerunning schedule (None on initial startup).", error);
            return;
        }
    }

    if (StringEqualI(season, "None"))
        TC_LOG_INFO("server.loading", "Timerunning: no season scheduled.");
    else
        TC_LOG_INFO("server.loading", "Timerunning: {} scheduled from {} UTC for {} days. Character admission also requires supported event content. Conversion remains unavailable.", season, start, days);
}

Timerunning::State Timerunning::Manager::GetState() const
{
    std::lock_guard lock(_mutex);
    State state = _schedule.GetState(GameTime::GetGameTime());
    state.ContentReady = state.ActiveSeason == Season::Pandaria && _pandariaContentReady;
    return state;
}

void Timerunning::Manager::SetPandariaContentReady(bool ready)
{
    std::lock_guard lock(_mutex);
    _pandariaContentReady = ready;
}

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

#ifndef EVRY_MOD_PLAYERBOTS_LOG_DETAIL_H
#define EVRY_MOD_PLAYERBOTS_LOG_DETAIL_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

inline constexpr char const* PLAYERBOTS_LOG_DETAIL = "Playerbots.LogDetail";

// Which bots write every step to Playerbots.log. The rest write only what they decide and what goes wrong, so a
// big population does not write gigabytes an hour. A human character (an RTS original character) is never quieted.
// No server dependencies, so the name matching is tested on its own; the world thread is the only caller.
class PlayerbotLogDetail
{
public:
    // A comma-separated list of character names, any case. "*" means every bot. Empty means none.
    static void SetNames(std::string_view list)
    {
        Names().clear();
        Everyone() = false;
        Quiet().clear();

        std::size_t start = 0;
        while (start <= list.size())
        {
            std::size_t end = list.find(',', start);
            if (end == std::string_view::npos)
                end = list.size();
            std::string name = Lower(Trim(list.substr(start, end - start)));
            if (name == "*")
                Everyone() = true;
            else if (!name.empty())
                Names().insert(std::move(name));
            start = end + 1;
        }
    }

    static bool IsNamed(std::string_view name)
    {
        return Everyone() || Names().count(Lower(Trim(name))) != 0;
    }

    // Called once when a bot enters the world, with her character guid as a number.
    static void NoteBot(uint64_t guid, std::string_view name)
    {
        if (IsNamed(name))
            Quiet().erase(guid);
        else
            Quiet().insert(guid);
    }

    static bool Wants(uint64_t guid)
    {
        return Quiet().empty() || Quiet().count(guid) == 0;
    }

private:
    static std::string_view Trim(std::string_view text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.remove_prefix(1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        return text;
    }

    static std::string Lower(std::string_view text)
    {
        std::string lowered(text);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        return lowered;
    }

    static std::set<std::string>& Names() { static std::set<std::string> names; return names; }
    static bool& Everyone() { static bool everyone = false; return everyone; }
    static std::unordered_set<uint64_t>& Quiet() { static std::unordered_set<uint64_t> quiet; return quiet; }
};

// Writes a step line only for a bot named in Playerbots.LogDetail (or a human character). The line is not even
// formatted for a quiet bot.
#define PLAYERBOT_LOG_DETAIL(player, ...)                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(player) || PlayerbotLogDetail::Wants((player)->GetGUID().GetCounter()))              \
            TC_LOG_INFO(PLAYERBOTS_LOG, __VA_ARGS__);                                              \
    } while (0)

#endif

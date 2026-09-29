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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EVRY_MOD_PLAYERBOT_WIPE_H
#define EVRY_MOD_PLAYERBOT_WIPE_H

#include <memory>
#include <string_view>

// The Battle.net email the module gives a bot it creates (PlayerbotFactory::MakeBattlenetEmail): PLAYERBOT, one or
// more digits, then @PLAYERBOTS.LOCAL, in upper case as the core stores it. The wipe deletes an account only when its
// email is exactly this. The SQL LIKE that finds candidates would also match PLAYERBOT_X@PLAYERBOTS.LOCAL or
// PLAYERBOTFAN@PLAYERBOTS.LOCAL, because % and _ are wildcards there; a human account like that is never touched.
inline bool IsPlayerbotBattlenetEmail(std::string_view email)
{
    constexpr std::string_view prefix = "PLAYERBOT";
    constexpr std::string_view suffix = "@PLAYERBOTS.LOCAL";
    if (email.size() <= prefix.size() + suffix.size())
        return false;
    if (email.substr(0, prefix.size()) != prefix || email.substr(email.size() - suffix.size()) != suffix)
        return false;

    std::string_view const digits = email.substr(prefix.size(), email.size() - prefix.size() - suffix.size());
    for (char c : digits)
        if (c < '0' || c > '9')
            return false;
    return true;
}

// Deletes every bot character, bot game account and bot Battle.net account, and what belongs to them, waiting until
// the database shows each step done before the next. It runs from the world update a slice at a time, so the world
// thread keeps ticking however many bots there are; no bot logs in while it runs.
class PlayerbotWipe
{
public:
    enum class State
    {
        Running,
        // Nothing of a bot is left.
        Done,
        // Something was left; the log says what.
        Failed
    };

    PlayerbotWipe();
    ~PlayerbotWipe();

    // One slice of the wipe: at most about 50 ms of work, then back to the world.
    State Update();

private:
    struct Data;
    std::unique_ptr<Data> _data;
};

#endif

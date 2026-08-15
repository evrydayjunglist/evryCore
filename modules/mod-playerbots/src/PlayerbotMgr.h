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

#ifndef EVRY_MOD_PLAYERBOT_MGR_H
#define EVRY_MOD_PLAYERBOT_MGR_H

#include "Playerbots.h"
#include <memory>
#include <unordered_set>
#include <vector>

class Player;
class WorldSession;

struct PlayerbotRecord
{
    PlayerbotAccount Account;
    std::unique_ptr<WorldSession> Session;
    bool FirstQuestQueued = false;
};

class PlayerbotMgr
{
public:
    static PlayerbotMgr* instance();

    PlayerbotMgr() = default;
    ~PlayerbotMgr();

    void Start();
    bool IsBotAccount(uint32 accountId) const;
    void OnBotLogin(Player* player);

private:
    void TryLogin(PlayerbotRecord& bot);

    std::vector<PlayerbotRecord> _bots;
    std::unordered_set<uint32> _accountIds;
};

#define sPlayerbotMgr PlayerbotMgr::instance()

#endif

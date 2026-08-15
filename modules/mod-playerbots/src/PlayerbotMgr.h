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

#ifndef EVRY_MOD_PLAYERBOT_MGR_H
#define EVRY_MOD_PLAYERBOT_MGR_H

#include "PlayerbotClient.h"
#include "PlayerbotMovement.h"
#include "Playerbots.h"
#include "ObjectGuid.h"
#include <unordered_set>
#include <vector>

class Player;

struct PlayerbotRecord
{
    PlayerbotAccount Account;
    bool EnumQueued = false;
    bool LoginQueued = false;
    bool ContinueLoginCalled = false;
    bool CinematicSkipped = false;
    bool InitMoverQueued = false;
    bool QuestInteractQueued = false;
    bool QuestSearchFailed = false;
    uint32 QuestArriveWaitMs = 0;
    uint32 QuestInteractWaitMs = 0;
    uint32 QuestSearchEmptyMs = 0;
    bool CombatSwingSent = false;
    PlayerbotClient::QuestTarget QuestTarget;
    PlayerbotClient::CombatTarget CombatTarget;
    std::unordered_set<ObjectGuid> UnreachableGuids;
    PlayerbotWalker Walker;
};

class PlayerbotMgr
{
public:
    static PlayerbotMgr* instance();

    PlayerbotMgr() = default;
    ~PlayerbotMgr();

    void Start();
    void Update(uint32 diff);
    bool IsBotAccount(uint32 accountId) const;
    void OnBotLogin(Player* player);

private:
    void TryLogin(PlayerbotRecord& bot);
    void UpdateLogin(PlayerbotRecord& bot);
    void UpdateWorld(PlayerbotRecord& bot, uint32 diff);
    void ReplyTimeSync(WorldSession* session);
    void ClearCombat(PlayerbotRecord& bot, Player* player);
    bool UpdateCombat(PlayerbotRecord& bot, Player* player);
    bool BeginQuestTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::QuestTarget const& target);

    std::vector<PlayerbotRecord> _bots;
    std::unordered_set<uint32> _accountIds;
};

#define sPlayerbotMgr PlayerbotMgr::instance()

#endif

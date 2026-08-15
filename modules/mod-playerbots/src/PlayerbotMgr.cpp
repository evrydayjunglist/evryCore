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

#include "PlayerbotMgr.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotClient.h"
#include "PlayerbotFactory.h"
#include "World.h"
#include "WorldSession.h"

PlayerbotMgr* PlayerbotMgr::instance()
{
    static PlayerbotMgr instance;
    return &instance;
}

PlayerbotMgr::~PlayerbotMgr() = default;

void PlayerbotMgr::Start()
{
    if (!sConfigMgr->GetBoolDefault(PLAYERBOTS_ENABLE, false))
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: disabled.");
        return;
    }

    int32 count = sConfigMgr->GetIntDefault(PLAYERBOTS_COUNT, 1);
    if (count < 1)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: count is {}; not starting bots.", count);
        return;
    }

    uint32 sessionsAtStartup = sWorld->GetActiveAndQueuedSessionCount();
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: starting {} bot(s). Active sessions at OnStartup: {}.",
        count, sessionsAtStartup);

    for (int32 i = 1; i <= count; ++i)
    {
        PlayerbotRecord bot;
        bot.Account.Index = uint32(i);
        if (!PlayerbotFactory::EnsureAccount(bot.Account) || !PlayerbotFactory::EnsureCharacter(bot.Account))
        {
            TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: bot {} was not created.", i);
            continue;
        }

        _accountIds.insert(bot.Account.AccountId);
        TryLogin(bot);
        _bots.push_back(std::move(bot));
    }
}

void PlayerbotMgr::Update(uint32 /*diff*/)
{
    for (PlayerbotRecord& bot : _bots)
        UpdateLogin(bot);
}

bool PlayerbotMgr::IsBotAccount(uint32 accountId) const
{
    return _accountIds.contains(accountId);
}

void PlayerbotMgr::OnBotLogin(Player* player)
{
    if (!player)
        return;

    for (PlayerbotRecord& bot : _bots)
    {
        if (bot.Account.CharacterGuid != player->GetGUID())
            continue;

        if (bot.FirstQuestQueued)
            return;

        bot.FirstQuestQueued = PlayerbotClient::TryAcceptFirstStarterQuest(player);
        return;
    }
}

void PlayerbotMgr::TryLogin(PlayerbotRecord& bot)
{
    std::unique_ptr<WorldSession> session = PlayerbotFactory::MakeSession(bot.Account);
    TC_LOG_INFO(PLAYERBOTS_LOG,
        "mod-playerbots: constructed WorldSession for account {} with an empty socket. Calling World::AddSession.",
        bot.Account.AccountId);
    sWorld->AddSession(session.release());
}

void PlayerbotMgr::UpdateLogin(PlayerbotRecord& bot)
{
    WorldSession* session = sWorld->FindSession(bot.Account.AccountId);
    if (!session)
        return;

    if (session->IsInQueue())
        return;

    if (!bot.EnumQueued)
    {
        PlayerbotClient::QueueEnumCharacters(session);
        bot.EnumQueued = true;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: account {} queued CMSG_ENUM_CHARACTERS.", bot.Account.AccountId);
        return;
    }

    if (!bot.LoginQueued)
    {
        if (!session->IsLegitCharacterForAccount(bot.Account.CharacterGuid))
            return;

        PlayerbotClient::QueuePlayerLogin(session, bot.Account.CharacterGuid);
        bot.LoginQueued = true;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: account {} queued CMSG_PLAYER_LOGIN for {}.",
            bot.Account.AccountId, bot.Account.CharacterGuid.ToString());
        return;
    }

    if (bot.ContinueLoginCalled)
        return;

    if (!session->PlayerLoading())
        return;

    session->HandleContinuePlayerLogin();
    bot.ContinueLoginCalled = true;
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: account {} called HandleContinuePlayerLogin.", bot.Account.AccountId);
}

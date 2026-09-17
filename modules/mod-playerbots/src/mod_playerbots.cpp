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

#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "PlayerbotWalkMapper.h"
#include "Playerbots.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

class PlayerbotsWorldScript : public WorldScript
{
public:
    PlayerbotsWorldScript() : WorldScript("mod_playerbots_WorldScript") { }

    void OnStartup() override
    {
        sPlayerbotMgr->Start();
    }

    void OnUpdate(uint32 diff) override
    {
        sPlayerbotMgr->Update(diff);
    }

    void OnShutdown() override
    {
        sPlayerbotMgr->Stop();
    }

    // Any thread. Bots read what the server sends them here; they still act only by queueing client packets.
    void OnSocketlessSessionPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        sPlayerbotMgr->OnSocketlessSessionPacketSend(session, packet);
    }
};

class PlayerbotsPlayerScript : public PlayerScript
{
public:
    PlayerbotsPlayerScript() : PlayerScript("mod_playerbots_PlayerScript") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player || !player->GetSession())
            return;

        if (!sPlayerbotMgr->IsBotAccount(player->GetSession()->GetAccountId()))
            return;

        sPlayerbotMgr->OnBotLogin(player);
    }

    void OnLogout(Player* player) override
    {
        sPlayerbotMgr->OnPlayerLogout(player);
    }

    void OnMapChanged(Player* player) override
    {
        sPlayerbotMgr->OnPlayerMapChanged(player);
    }
};

class PlayerbotsCommandScript : public CommandScript
{
public:
    PlayerbotsCommandScript() : CommandScript("mod_playerbots_CommandScript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable playerbotsTable =
        {
            { "walkmap", HandleWalkMapCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "playerbots", playerbotsTable },
        };
        return commandTable;
    }

    // .playerbots walkmap [yards] [name]: maps the ground that player can walk from her feet by a bot's walk rules and
    // writes a picture next to the server logs. Without a name it maps your target when that is a player, otherwise you.
    // Yards come first so a number is never read as a character.
    static bool HandleWalkMapCommand(ChatHandler* handler, Optional<uint32> yards, Optional<PlayerIdentifier> target)
    {
        if (!target)
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!target)
        {
            handler->SendSysMessage("Name a player or target one.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* subject = target->GetConnectedPlayer();
        if (!subject)
        {
            handler->PSendSysMessage("%s is not online.", target->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* requester = handler->GetPlayer();
        std::string message;
        bool const started = sPlayerbotMgr->StartWalkMap(subject, float(yards.value_or(uint32(PLAYERBOT_WALK_MAP_DEFAULT_YARDS))),
            requester ? requester->GetGUID() : ObjectGuid::Empty, message);
        handler->SendSysMessage(message);
        if (!started)
            handler->SetSentErrorMessage(true);
        return started;
    }
};

void Addmod_playerbotsScripts()
{
    new PlayerbotsWorldScript();
    new PlayerbotsPlayerScript();
    new PlayerbotsCommandScript();
}

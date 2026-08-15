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

#include "Player.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "ScriptMgr.h"

class PlayerbotsWorldScript : public WorldScript
{
public:
    PlayerbotsWorldScript() : WorldScript("mod_playerbots_WorldScript") { }

    void OnStartup() override
    {
        sPlayerbotMgr->Start();
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
};

void Addmod_playerbotsScripts()
{
    new PlayerbotsWorldScript();
    new PlayerbotsPlayerScript();
}

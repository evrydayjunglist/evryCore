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

#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

class ExampleWorldScript : public WorldScript
{
public:
    ExampleWorldScript() : WorldScript("mod_example_WorldScript") { }

    void OnStartup() override
    {
        TC_LOG_INFO("server.loading", "mod-example: loaded");
    }
};

class ExamplePlayerScript : public PlayerScript
{
public:
    ExamplePlayerScript() : PlayerScript("mod_example_PlayerScript") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!sConfigMgr->GetBoolDefault("Example.Enable", true))
            return;

        TC_LOG_INFO("server.worldserver", "mod-example: {} logged in", player->GetName());
    }
};

void Addmod_exampleScripts()
{
    new ExampleWorldScript();
    new ExamplePlayerScript();
}

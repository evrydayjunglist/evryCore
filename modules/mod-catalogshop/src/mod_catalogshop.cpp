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

#include "CatalogShopHttpService.h"
#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "World.h"

class CatalogShopWorldScript : public WorldScript
{
public:
    CatalogShopWorldScript() : WorldScript("mod_catalogshop_WorldScript") { }

    void OnConfigLoad(bool reload) override
    {
        bool const enabled = sConfigMgr->GetBoolDefault("CatalogShop.Enable", false);
        sWorld->setBoolConfig(CONFIG_BATTLE_PAY_SHOP2_ENABLED, enabled);

        if (enabled)
        {
            // Live path while the module is on. Empty disables Free Buy polling.
            std::string const dir = sConfigMgr->GetStringDefault("CatalogShop.FreeBuySignalDir", "temp/catalogshop-free-buy");
            sWorld->setCatalogShopFreeBuySignalDir(dir);
        }

        TC_LOG_INFO("module.catalogshop", "mod-catalogshop config{}: CatalogShop.Enable = {}",
            reload ? " reload" : "", enabled ? 1 : 0);

        if (!reload)
            return;

        if (enabled)
        {
            if (!sCatalogShop.StartFromConfig())
                TC_LOG_ERROR("server.worldserver", "CatalogShop HTTPS failed to start after config reload");
        }
        else
            sCatalogShop.Stop();
    }

    void OnStartup() override
    {
        if (!sCatalogShop.StartFromConfig())
            TC_LOG_ERROR("server.worldserver", "CatalogShop HTTPS failed to start — browse needs Enable=1, a localhost SAN cert, and an elevated process for port 443");
    }

    void OnShutdown() override
    {
        sCatalogShop.Stop();
    }
};

void Addmod_catalogshopScripts()
{
    new CatalogShopWorldScript();
}

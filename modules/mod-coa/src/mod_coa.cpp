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

#include "Log.h"
#include "ScriptMgr.h"

// The Conquest of Azeroth port. Every class we bring over from that server
// lives here, one class at a time. Reaper is the first and the only one so far.
//
// Today this module only carries data: the class rows under data/sql, one .sql
// file per class. The commands and the class scripts come later, as their own
// source files beside this one. This file stays the module's entry point.
//
// The module has to be built even while it is only data, because the database
// updater applies a module's SQL only when that module is enabled.

class CoaWorldScript : public WorldScript
{
public:
    CoaWorldScript() : WorldScript("mod_coa_WorldScript") { }

    void OnStartup() override
    {
        TC_LOG_INFO("server.loading", "mod-coa: loaded");
    }
};

void Addmod_coaScripts()
{
    new CoaWorldScript();
}

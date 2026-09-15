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

#include "TimerunningPandaria.h"
#include "DB2Stores.h"
#include "Log.h"
#include "ObjectMgr.h"

CharacterLoadoutEntry const* Timerunning::Pandaria::GetStartingLoadout(uint8 playerClass)
{
    if (!playerClass || playerClass >= StartingLoadouts.size())
        return nullptr;

    CharacterLoadoutEntry const* loadout = sCharacterLoadoutStore.LookupEntry(StartingLoadouts[playerClass]);
    if (!loadout || loadout->ChrClassID != playerClass || loadout->Purpose != 16 || loadout->ItemContext != 75 || !loadout->RaceMask.IsEmpty())
        return nullptr;

    return loadout;
}

bool Timerunning::Pandaria::ValidateStartingLoadouts()
{
    bool ready = true;
    for (uint8 playerClass = 1; playerClass < StartingLoadouts.size(); ++playerClass)
    {
        CharacterLoadoutEntry const* loadout = GetStartingLoadout(playerClass);
        if (!loadout)
        {
            TC_LOG_ERROR("server.loading", "Timerunning: missing or incompatible Pandaria starting loadout {} for class {}.", StartingLoadouts[playerClass], playerClass);
            ready = false;
            continue;
        }

        uint32 itemCount = 0;
        for (CharacterLoadoutItemEntry const* item : sCharacterLoadoutItemStore)
            if (item->CharacterLoadoutID == loadout->ID)
            {
                if (!sObjectMgr->GetItemTemplate(item->ItemID))
                {
                    TC_LOG_ERROR("server.loading", "Timerunning: Pandaria starting loadout {} requires missing item {}.", loadout->ID, item->ItemID);
                    ready = false;
                }
                ++itemCount;
            }

        if (!itemCount)
        {
            TC_LOG_ERROR("server.loading", "Timerunning: Pandaria starting loadout {} has no items.", loadout->ID);
            ready = false;
        }
    }
    return ready;
}

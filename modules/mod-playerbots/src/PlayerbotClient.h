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

#ifndef EVRY_MOD_PLAYERBOT_CLIENT_H
#define EVRY_MOD_PLAYERBOT_CLIENT_H

#include "ObjectGuid.h"

class Player;
class WorldSession;

namespace PlayerbotClient
{
    void QueueEnumCharacters(WorldSession* session);
    void QueuePlayerLogin(WorldSession* session, ObjectGuid characterGuid);
    void QueueQuestGiverAcceptQuest(WorldSession* session, ObjectGuid questGiverGuid, int32 questId);
    bool TryAcceptFirstStarterQuest(Player* player);
}

#endif

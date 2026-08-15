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

#include "PlayerbotClient.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <vector>

void PlayerbotClient::QueueEnumCharacters(WorldSession* session)
{
    if (!session)
        return;

    session->QueuePacket(WorldPacket(CMSG_ENUM_CHARACTERS));
}

void PlayerbotClient::QueuePlayerLogin(WorldSession* session, ObjectGuid characterGuid)
{
    if (!session || characterGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_PLAYER_LOGIN);
    packet << characterGuid;
    packet << float(433.0f);
    packet.WriteBit(false);
    packet.FlushBits();
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueQuestGiverAcceptQuest(WorldSession* session, ObjectGuid questGiverGuid, int32 questId)
{
    if (!session || questGiverGuid.IsEmpty() || !questId)
        return;

    WorldPacket packet(CMSG_QUEST_GIVER_ACCEPT_QUEST);
    packet << questGiverGuid;
    packet << int32(questId);
    packet.WriteBit(false); // StartCheat stays false
    packet.FlushBits();
    session->QueuePacket(std::move(packet));
}

bool PlayerbotClient::TryAcceptFirstStarterQuest(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetSession())
        return false;

    std::vector<Creature*> nearby;
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    player->GetCreatureListWithOptionsInGrid(nearby, 10.0f, options);

    for (Creature* creature : nearby)
    {
        if (!creature || !creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            continue;

        if (!player->CanInteractWithQuestGiver(creature))
            continue;

        player->PrepareQuestMenu(creature->GetGUID());
        QuestMenu& menu = player->PlayerTalkClass->GetQuestMenu();
        for (uint8 i = 0; i < menu.GetMenuItemCount(); ++i)
        {
            QuestMenuItem const& item = menu.GetItem(i);
            if (item.QuestIcon != 2)
                continue;

            Quest const* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
            if (!quest)
                continue;

            if (!player->CanTakeQuest(quest, false) || !player->CanAddQuest(quest, false))
                continue;

            QueueQuestGiverAcceptQuest(player->GetSession(), creature->GetGUID(), int32(item.QuestId));
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_QUEST_GIVER_ACCEPT_QUEST for quest {} from {}.",
                player->GetName(), item.QuestId, creature->GetGUID().ToString());
            return true;
        }
    }

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no starter quest in interact range. Movement is not designed yet; the quest is not skipped.",
        player->GetName());
    return false;
}

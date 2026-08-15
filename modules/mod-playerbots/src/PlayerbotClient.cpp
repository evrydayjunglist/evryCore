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

#include "PlayerbotClient.h"
#include "Creature.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "LootItemType.h"
#include "Log.h"
#include "MovementInfo.h"
#include "MovementPackets.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerbotMovement.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "UnitDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <limits>
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

void PlayerbotClient::QueueCompleteCinematic(WorldSession* session)
{
    if (!session)
        return;

    session->QueuePacket(WorldPacket(CMSG_COMPLETE_CINEMATIC));
}

void PlayerbotClient::QueueTimeSyncResponse(WorldSession* session, uint32 sequenceIndex, uint32 clientTime)
{
    if (!session)
        return;

    WorldPacket packet(CMSG_TIME_SYNC_RESPONSE);
    packet << uint32(sequenceIndex);
    packet << uint32(clientTime);
    packet.SetReceiveTime(GameTime::Now());
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueMoveInitActiveMoverComplete(WorldSession* session, uint32 ticks)
{
    if (!session)
        return;

    WorldPacket packet(CMSG_MOVE_INIT_ACTIVE_MOVER_COMPLETE);
    packet << uint32(ticks);
    packet.SetReceiveTime(GameTime::Now());
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueMovement(WorldSession* session, OpcodeClient opcode, MovementInfo const& movementInfo)
{
    if (!session)
        return;

    WorldPacket packet(opcode);
    packet << movementInfo;
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

void PlayerbotClient::QueueQuestGiverCompleteQuest(WorldSession* session, ObjectGuid questGiverGuid, int32 questId)
{
    if (!session || questGiverGuid.IsEmpty() || !questId)
        return;

    WorldPacket packet(CMSG_QUEST_GIVER_COMPLETE_QUEST);
    packet << questGiverGuid;
    packet << int32(questId);
    packet.WriteBit(false); // FromScript stays false
    packet.FlushBits();
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueQuestGiverChooseReward(WorldSession* session, ObjectGuid questGiverGuid, int32 questId)
{
    if (!session || questGiverGuid.IsEmpty() || !questId)
        return;

    WorldPacket packet(CMSG_QUEST_GIVER_CHOOSE_REWARD);
    packet << questGiverGuid;
    packet << int32(questId);
    packet.WriteBits(uint32(LootItemType::Item), 2);
    packet.WriteBit(false); // no ContextFlags
    packet << int32(0);     // ItemID
    packet.WriteBit(false); // no ItemBonus
    packet.FlushBits();
    packet.WriteBits(0, 7); // ItemModList size
    packet.FlushBits();
    packet << int32(0);     // Quantity
    session->QueuePacket(std::move(packet));
}

Optional<PlayerbotClient::QuestTarget> PlayerbotClient::FindNearbyQuestTarget(Player* player, float range)
{
    if (!player || !player->IsInWorld())
        return {};

    std::vector<Creature*> nearby;
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    player->GetCreatureListWithOptionsInGrid(nearby, range, options);

    QuestTarget bestTurnIn;
    QuestTarget bestAccept;
    float bestTurnInDist = std::numeric_limits<float>::max();
    float bestAcceptDist = std::numeric_limits<float>::max();

    for (Creature* creature : nearby)
    {
        if (!creature || !creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            continue;

        player->PrepareQuestMenu(creature->GetGUID());
        QuestMenu& menu = player->PlayerTalkClass->GetQuestMenu();
        float dist = player->GetExactDist(creature);
        bool pickedStand = false;
        Position standPos;
        bool skipCreature = false;

        for (uint8 i = 0; i < menu.GetMenuItemCount() && !skipCreature; ++i)
        {
            QuestMenuItem const& item = menu.GetItem(i);
            Quest const* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
            if (!quest)
                continue;

            bool const isTurnIn = item.QuestIcon == 4 && player->GetQuestStatus(item.QuestId) == QUEST_STATUS_COMPLETE;
            bool const isAccept = item.QuestIcon == 2 && player->CanTakeQuest(quest, false) && player->CanAddQuest(quest, false);
            if (!isTurnIn && !isAccept)
                continue;

            if (!pickedStand)
            {
                float const standDistance = creature->GetCombatReach() + 1.0f;
                if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
                {
                    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that npc.",
                        player->GetName(), creature->GetGUID().ToString());
                    skipCreature = true;
                    continue;
                }
                pickedStand = true;
            }

            if (isTurnIn && dist < bestTurnInDist)
            {
                bestTurnInDist = dist;
                bestTurnIn.NpcGuid = creature->GetGUID();
                bestTurnIn.Pos = standPos;
                bestTurnIn.StopDistance = 0.25f;
                bestTurnIn.QuestId = int32(item.QuestId);
                bestTurnIn.TurnIn = true;
            }
            else if (isAccept && dist < bestAcceptDist)
            {
                bestAcceptDist = dist;
                bestAccept.NpcGuid = creature->GetGUID();
                bestAccept.Pos = standPos;
                bestAccept.StopDistance = 0.25f;
                bestAccept.QuestId = int32(item.QuestId);
                bestAccept.TurnIn = false;
            }
        }
    }

    if (!bestTurnIn.NpcGuid.IsEmpty())
        return bestTurnIn;

    if (!bestAccept.NpcGuid.IsEmpty())
        return bestAccept;

    return {};
}

bool PlayerbotClient::TryInteractQuest(Player* player, QuestTarget const& target)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || target.NpcGuid.IsEmpty() || !target.QuestId)
        return false;

    Creature* creature = ObjectAccessor::GetCreature(*player, target.NpcGuid);
    if (!creature)
        return false;

    if (!player->CanInteractWithQuestGiver(creature))
        return false;

    if (target.TurnIn)
    {
        QueueQuestGiverCompleteQuest(player->GetSession(), target.NpcGuid, target.QuestId);
        QueueQuestGiverChooseReward(player->GetSession(), target.NpcGuid, target.QuestId);
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_QUEST_GIVER_COMPLETE_QUEST and CMSG_QUEST_GIVER_CHOOSE_REWARD for quest {} at {}.",
            player->GetName(), target.QuestId, target.NpcGuid.ToString());
        return true;
    }

    QueueQuestGiverAcceptQuest(player->GetSession(), target.NpcGuid, target.QuestId);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_QUEST_GIVER_ACCEPT_QUEST for quest {} from {}.",
        player->GetName(), target.QuestId, target.NpcGuid.ToString());
    return true;
}

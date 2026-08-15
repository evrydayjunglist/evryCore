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
#include "CreatureData.h"
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
#include "Unit.h"
#include "UnitDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <limits>
#include <unordered_set>
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

void PlayerbotClient::QueueSetSelection(WorldSession* session, ObjectGuid guid)
{
    if (!session)
        return;

    WorldPacket packet(CMSG_SET_SELECTION);
    packet << guid;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueAttackSwing(WorldSession* session, ObjectGuid victim)
{
    if (!session || victim.IsEmpty())
        return;

    WorldPacket packet(CMSG_ATTACK_SWING);
    packet << victim;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueAttackStop(WorldSession* session)
{
    if (!session)
        return;

    session->QueuePacket(WorldPacket(CMSG_ATTACK_STOP));
}

namespace
{
    bool CreatureGivesMonsterCredit(Creature const* creature, uint32 creditEntry)
    {
        if (!creature || !creditEntry)
            return false;

        if (creature->GetEntry() == creditEntry)
            return true;

        CreatureTemplate const* info = creature->GetCreatureTemplate();
        if (!info)
            return false;

        for (uint8 i = 0; i < MAX_KILL_CREDIT; ++i)
        {
            if (info->KillCredit[i] == creditEntry)
                return true;
        }

        return false;
    }

    struct IncompleteMonsterCredit
    {
        int32 QuestId = 0;
        uint32 CreditEntry = 0;
    };

    void CollectIncompleteMonsterCredits(Player* player, std::vector<IncompleteMonsterCredit>& out)
    {
        if (!player)
            return;

        for (auto const& [questId, status] : player->getQuestStatusMap())
        {
            if (status.Status != QUEST_STATUS_INCOMPLETE)
                continue;

            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;

            for (QuestObjective const& objective : quest->GetObjectives())
            {
                if (objective.Type != QUEST_OBJECTIVE_MONSTER || objective.ObjectID <= 0)
                    continue;
                if (objective.Flags & QUEST_OBJECTIVE_FLAG_OPTIONAL)
                    continue;
                if (objective.Flags & QUEST_OBJECTIVE_FLAG_HIDDEN)
                    continue;
                if (!player->IsQuestObjectiveCompletable(questId, objective.ID))
                    continue;
                if (player->IsQuestObjectiveComplete(questId, objective.ID))
                    continue;

                IncompleteMonsterCredit credit;
                credit.QuestId = int32(questId);
                credit.CreditEntry = uint32(objective.ObjectID);
                out.push_back(credit);
            }
        }
    }
}

Optional<PlayerbotClient::QuestTarget> PlayerbotClient::FindNearbyQuestTarget(Player* player, float range, QuestSearchKind kind)
{
    if (!player || !player->IsInWorld())
        return {};

    std::vector<Creature*> nearby;
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    player->GetCreatureListWithOptionsInGrid(nearby, range, options);

    QuestTarget best;
    float bestDist = std::numeric_limits<float>::max();

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
            if (kind == QuestSearchKind::TurnIn && !isTurnIn)
                continue;
            if (kind == QuestSearchKind::Accept && !isAccept)
                continue;
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

            if (dist >= bestDist)
                continue;

            bestDist = dist;
            best.NpcGuid = creature->GetGUID();
            best.Pos = standPos;
            best.StopDistance = 0.25f;
            best.QuestId = int32(item.QuestId);
            best.TurnIn = isTurnIn;
        }
    }

    if (best.NpcGuid.IsEmpty())
        return {};

    return best;
}

Optional<PlayerbotClient::CombatTarget> PlayerbotClient::FindNearbyMonsterObjectiveTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip)
{
    if (!player || !player->IsInWorld())
        return {};

    std::vector<IncompleteMonsterCredit> credits;
    CollectIncompleteMonsterCredits(player, credits);
    if (credits.empty())
        return {};

    std::vector<Creature*> nearby;
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    player->GetCreatureListWithOptionsInGrid(nearby, range, options);

    CombatTarget best;
    float bestDist = std::numeric_limits<float>::max();

    for (Creature* creature : nearby)
    {
        if (!creature || skip.contains(creature->GetGUID()))
            continue;
        if (!player->IsValidAttackTarget(creature))
            continue;

        IncompleteMonsterCredit const* matched = nullptr;
        for (IncompleteMonsterCredit const& credit : credits)
        {
            if (CreatureGivesMonsterCredit(creature, credit.CreditEntry))
            {
                matched = &credit;
                break;
            }
        }
        if (!matched)
            continue;

        float const dist = player->GetExactDist(creature);
        if (dist >= bestDist)
            continue;

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that creature.",
                player->GetName(), creature->GetGUID().ToString());
            continue;
        }

        bestDist = dist;
        best.CreatureGuid = creature->GetGUID();
        best.Pos = standPos;
        best.StopDistance = 0.25f;
        best.QuestId = matched->QuestId;
        best.CreditEntry = matched->CreditEntry;
    }

    if (best.CreatureGuid.IsEmpty())
        return {};

    return best;
}

bool PlayerbotClient::CombatTargetStillNeeded(Player* player, CombatTarget const& target)
{
    if (!player || target.QuestId <= 0 || !target.CreditEntry)
        return false;
    if (player->GetQuestStatus(uint32(target.QuestId)) != QUEST_STATUS_INCOMPLETE)
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(uint32(target.QuestId));
    if (!quest)
        return false;

    for (QuestObjective const& objective : quest->GetObjectives())
    {
        if (objective.Type != QUEST_OBJECTIVE_MONSTER)
            continue;
        if (uint32(objective.ObjectID) != target.CreditEntry)
            continue;
        if (!player->IsQuestObjectiveCompletable(uint32(target.QuestId), objective.ID))
            continue;
        if (player->IsQuestObjectiveComplete(uint32(target.QuestId), objective.ID))
            continue;
        return true;
    }

    return false;
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

bool PlayerbotClient::TryMeleeAttack(Player* player, ObjectGuid creatureGuid)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || creatureGuid.IsEmpty())
        return false;

    Creature* creature = ObjectAccessor::GetCreature(*player, creatureGuid);
    if (!creature || !creature->IsAlive())
        return false;

    if (!player->IsValidAttackTarget(creature))
        return false;

    if (!player->IsWithinMeleeRange(creature))
        return false;

    if (player->GetVictim() == creature && player->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
        return true;

    QueueSetSelection(player->GetSession(), creatureGuid);
    QueueAttackSwing(player->GetSession(), creatureGuid);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_SET_SELECTION and CMSG_ATTACK_SWING on {}.",
        player->GetName(), creatureGuid.ToString());
    return true;
}

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
#include "GameObject.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "LootItemType.h"
#include "Log.h"
#include "Map.h"
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
#include "SharedDefines.h"
#include "Unit.h"
#include "UnitDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
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

void PlayerbotClient::QueueGameObjUse(WorldSession* session, ObjectGuid guid)
{
    if (!session || guid.IsEmpty())
        return;

    WorldPacket packet(CMSG_GAME_OBJ_USE);
    packet << guid;
    session->QueuePacket(std::move(packet));
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
        uint32 ObjectiveId = 0;
        int8 StorageIndex = 0;
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
                credit.ObjectiveId = objective.ID;
                credit.StorageIndex = objective.StorageIndex;
                out.push_back(credit);
            }
        }
    }

    struct IncompleteGameObjectCredit
    {
        int32 QuestId = 0;
        uint32 GoEntry = 0;
        uint32 ObjectiveId = 0;
        int8 StorageIndex = 0;
    };

    void CollectIncompleteGameObjectCredits(Player* player, std::vector<IncompleteGameObjectCredit>& out)
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
                if (objective.Type != QUEST_OBJECTIVE_GAMEOBJECT || objective.ObjectID <= 0)
                    continue;
                if (objective.Flags & QUEST_OBJECTIVE_FLAG_OPTIONAL)
                    continue;
                if (objective.Flags & QUEST_OBJECTIVE_FLAG_HIDDEN)
                    continue;
                if (!player->IsQuestObjectiveCompletable(questId, objective.ID))
                    continue;
                if (player->IsQuestObjectiveComplete(questId, objective.ID))
                    continue;

                IncompleteGameObjectCredit credit;
                credit.QuestId = int32(questId);
                credit.GoEntry = uint32(objective.ObjectID);
                credit.ObjectiveId = objective.ID;
                credit.StorageIndex = objective.StorageIndex;
                out.push_back(credit);
            }
        }
    }

    struct ObjectivePoiBlob
    {
        std::vector<Position> Points;
        Position Centroid;
        float Radius = 40.0f;
    };

    // Finished quests put a ? on the map. That blob uses ObjectiveIndex -1.
    // 32 is the starter. A polygon of points is an incomplete objective, not turn-in.
    bool BlobMatchesIncompleteObjective(QuestPOIBlobData const& blob, uint32 mapId, uint32 objectiveId, int32 objectId, int8 storageIndex)
    {
        if (blob.MapID != int32(mapId) || blob.Points.empty())
            return false;
        if (blob.ObjectiveIndex == -1 || blob.ObjectiveIndex == 32)
            return false;
        if (blob.QuestObjectiveID == int32(objectiveId))
            return true;
        if (blob.QuestObjectID == objectId)
            return true;
        return blob.ObjectiveIndex == storageIndex;
    }

    void CollectObjectivePoiBlobs(int32 questId, uint32 mapId, uint32 objectiveId, int32 objectId, int8 storageIndex, std::vector<ObjectivePoiBlob>& out)
    {
        QuestPOIData const* poiData = sObjectMgr->GetQuestPOIData(questId);
        if (!poiData)
            return;

        for (QuestPOIBlobData const& blob : poiData->Blobs)
        {
            if (!BlobMatchesIncompleteObjective(blob, mapId, objectiveId, objectId, storageIndex))
                continue;

            ObjectivePoiBlob area;
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            for (QuestPOIBlobPoint const& point : blob.Points)
            {
                Position pos;
                pos.Relocate(float(point.X), float(point.Y), float(point.Z));
                area.Points.push_back(pos);
                x += pos.GetPositionX();
                y += pos.GetPositionY();
                z += pos.GetPositionZ();
            }

            float const count = float(area.Points.size());
            area.Centroid.Relocate(x / count, y / count, z / count);

            float radius = 0.0f;
            for (Position const& pos : area.Points)
                radius = std::max(radius, area.Centroid.GetExactDist(pos));
            area.Radius = std::max(radius, 40.0f);
            out.push_back(area);
        }
    }

    bool GameObjectIsUsableForObjective(Player const* player, GameObject const* go, uint32 entry)
    {
        if (!player || !go || !go->IsInWorld() || !go->isSpawned())
            return false;
        if (go->GetEntry() != entry)
            return false;

        GameObjectTemplate const* info = go->GetGOInfo();
        if (!info || info->IconName == "Point")
            return false;
        if (go->HasFlag(GO_FLAG_IN_USE) || go->HasFlag(GO_FLAG_NOT_SELECTABLE))
            return false;
        if (go->GetGoState() != GO_STATE_READY)
            return false;
        if (!player->InSamePhase(go))
            return false;
        if (go->IsPrivateObject() && !go->CheckPrivateObjectOwnerVisibility(player))
            return false;
        if (!player->CanSeeOrDetect(go))
            return false;
        if (!go->ActivateToQuest(player))
            return false;
        return true;
    }

    bool PositionIsInPoiArea(Position const& pos, std::vector<ObjectivePoiBlob> const& blobs)
    {
        if (blobs.empty())
            return false;

        for (ObjectivePoiBlob const& blob : blobs)
        {
            if (pos.GetExactDist(blob.Centroid) <= blob.Radius + 5.0f)
                return true;
        }

        return false;
    }

    float GameObjectStandDistance(GameObject const* go)
    {
        float size = 1.0f;
        if (go && go->GetGOInfo())
            size = std::max(go->GetGOInfo()->size, 1.0f);
        return size + 1.0f;
    }

    Optional<PlayerbotClient::GameObjectTarget> MakeGameObjectUseTarget(Player* player, GameObject* go, int32 questId)
    {
        if (!player || !go || !questId)
            return {};

        Position standPos;
        if (!PlayerbotWalker::PickApproachPosition(player, go, GameObjectStandDistance(go), standPos))
            return {};

        PlayerbotClient::GameObjectTarget target;
        target.GoGuid = go->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        target.QuestId = questId;
        target.GoEntry = go->GetEntry();
        return target;
    }

    Optional<PlayerbotClient::CombatTarget> MakeCombatTarget(Player* player, Creature* creature, int32 questId, uint32 creditEntry)
    {
        if (!player || !creature || !questId || !creditEntry)
            return {};

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
            return {};

        PlayerbotClient::CombatTarget target;
        target.CreatureGuid = creature->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        target.QuestId = questId;
        target.CreditEntry = creditEntry;
        return target;
    }

    void LoadPoiGrids(Map* map, std::vector<ObjectivePoiBlob> const& blobs)
    {
        if (!map)
            return;

        for (ObjectivePoiBlob const& blob : blobs)
        {
            for (Position const& point : blob.Points)
            {
                if (!map->IsGridLoaded(point))
                    map->LoadGrid(point.GetPositionX(), point.GetPositionY());
            }
        }
    }

    // Finished quests put a ? on the map. That blob uses ObjectiveIndex -1.
    // 32 is the starter. A polygon of points is an incomplete objective, not turn-in.
    Optional<Position> GetFinishedQuestMapMarker(uint32 questId, uint32 mapId, Position const& from)
    {
        QuestPOIData const* poiData = sObjectMgr->GetQuestPOIData(int32(questId));
        if (!poiData)
            return {};

        Optional<Position> best;
        float bestDist = std::numeric_limits<float>::max();

        for (QuestPOIBlobData const& blob : poiData->Blobs)
        {
            if (blob.ObjectiveIndex != -1 || blob.MapID != int32(mapId) || blob.Points.empty())
                continue;

            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            for (QuestPOIBlobPoint const& point : blob.Points)
            {
                x += float(point.X);
                y += float(point.Y);
                z += float(point.Z);
            }

            float const count = float(blob.Points.size());
            Position marker;
            marker.Relocate(x / count, y / count, z / count);
            float const dist = from.GetExactDist(marker);
            if (dist >= bestDist)
                continue;

            bestDist = dist;
            best = marker;
        }

        return best;
    }

    void CollectCreatureEnderEntries(uint32 questId, std::unordered_set<uint32>& out)
    {
        for (auto const& rel : sObjectMgr->GetCreatureQuestInvolvedRelationReverseBounds(questId))
            out.insert(rel.second);
    }

    bool CreatureIsUsableEnder(Player const* player, Creature const* creature, std::unordered_set<uint32> const& enderEntries)
    {
        if (!player || !creature || !creature->IsAlive())
            return false;
        if (!enderEntries.contains(creature->GetEntry()))
            return false;
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            return false;
        if (!player->InSamePhase(creature))
            return false;
        if (creature->IsPrivateObject() && !creature->CheckPrivateObjectOwnerVisibility(player))
            return false;
        return true;
    }

    Optional<PlayerbotClient::QuestTarget> MakeTurnInTarget(Player* player, Creature* creature, int32 questId)
    {
        if (!player || !creature || !questId)
            return {};

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
            return {};

        PlayerbotClient::QuestTarget target;
        target.NpcGuid = creature->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        target.QuestId = questId;
        target.TurnIn = true;
        return target;
    }

    Creature* FindLivingEnderOnMap(Player* player, std::unordered_set<uint32> const& enderEntries, Optional<Position> const& marker)
    {
        if (!player || !player->GetMap() || enderEntries.empty())
            return nullptr;

        Creature* best = nullptr;
        float bestDist = std::numeric_limits<float>::max();

        for (auto const& pair : player->GetMap()->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!CreatureIsUsableEnder(player, creature, enderEntries))
                continue;
            if (marker && creature->GetExactDist(*marker) > 40.0f)
                continue;

            float const dist = player->GetExactDist(creature);
            if (dist >= bestDist)
                continue;

            bestDist = dist;
            best = creature;
        }

        return best;
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

Optional<PlayerbotClient::QuestTarget> PlayerbotClient::FindLogCompleteTurnIn(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    Map* map = player->GetMap();
    QuestTarget best;
    float bestDist = std::numeric_limits<float>::max();

    for (auto const& [questId, status] : player->getQuestStatusMap())
    {
        if (status.Status != QUEST_STATUS_COMPLETE)
            continue;

        std::unordered_set<uint32> enderEntries;
        CollectCreatureEnderEntries(questId, enderEntries);
        if (enderEntries.empty())
            continue;

        Optional<Position> marker = GetFinishedQuestMapMarker(questId, map->GetId(), *player);
        if (marker && !map->IsGridLoaded(*marker))
            map->LoadGrid(marker->GetPositionX(), marker->GetPositionY());

        Creature* creature = FindLivingEnderOnMap(player, enderEntries, marker);
        if (!creature)
            continue;

        Optional<QuestTarget> target = MakeTurnInTarget(player, creature, int32(questId));
        if (!target)
            continue;

        float const dist = player->GetExactDist(target->Pos);
        if (dist >= bestDist)
            continue;

        bestDist = dist;
        best = *target;
    }

    if (best.NpcGuid.IsEmpty())
        return {};

    return best;
}

bool PlayerbotClient::HasLogCompleteTurnInOnThisMap(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return false;

    uint32 const mapId = player->GetMap()->GetId();

    for (auto const& [questId, status] : player->getQuestStatusMap())
    {
        if (status.Status != QUEST_STATUS_COMPLETE)
            continue;

        std::unordered_set<uint32> enderEntries;
        CollectCreatureEnderEntries(questId, enderEntries);
        if (enderEntries.empty())
            continue;

        if (GetFinishedQuestMapMarker(questId, mapId, *player))
            return true;

        if (FindLivingEnderOnMap(player, enderEntries, {}))
            return true;
    }

    return false;
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

        Optional<CombatTarget> target = MakeCombatTarget(player, creature, matched->QuestId, matched->CreditEntry);
        if (!target)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that creature.",
                player->GetName(), creature->GetGUID().ToString());
            continue;
        }

        bestDist = dist;
        best = *target;
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

Optional<PlayerbotClient::CombatTarget> PlayerbotClient::FindLogIncompleteMonsterTarget(Player* player, std::unordered_set<ObjectGuid> const& skip)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    Map* map = player->GetMap();
    uint32 const mapId = map->GetId();

    std::vector<IncompleteMonsterCredit> credits;
    CollectIncompleteMonsterCredits(player, credits);
    if (credits.empty())
        return {};

    CombatTarget bestCreatureTarget;
    float bestCreatureDist = std::numeric_limits<float>::max();
    bool haveCreature = false;
    Position bestMarker;
    IncompleteMonsterCredit const* bestMarkerCredit = nullptr;
    float bestMarkerDist = std::numeric_limits<float>::max();
    bool haveMarker = false;

    for (IncompleteMonsterCredit const& credit : credits)
    {
        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.CreditEntry), credit.StorageIndex, blobs);
        if (blobs.empty())
            continue;

        LoadPoiGrids(map, blobs);

        for (ObjectivePoiBlob const& blob : blobs)
        {
            for (Position const& point : blob.Points)
            {
                float const dist = player->GetExactDist(point);
                if (dist >= bestMarkerDist)
                    continue;

                bestMarkerDist = dist;
                bestMarker = point;
                bestMarkerCredit = &credit;
                haveMarker = true;
            }
        }

        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
            if (!creature->IsAlive())
                continue;
            if (!player->IsValidAttackTarget(creature))
                continue;
            if (!player->InSamePhase(creature))
                continue;
            if (creature->IsPrivateObject() && !creature->CheckPrivateObjectOwnerVisibility(player))
                continue;
            if (!CreatureGivesMonsterCredit(creature, credit.CreditEntry))
                continue;
            if (!PositionIsInPoiArea(*creature, blobs))
                continue;

            float const dist = player->GetExactDist(creature);
            if (dist >= bestCreatureDist)
                continue;

            Optional<CombatTarget> target = MakeCombatTarget(player, creature, credit.QuestId, credit.CreditEntry);
            if (!target)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that creature.",
                    player->GetName(), creature->GetGUID().ToString());
                continue;
            }

            bestCreatureDist = dist;
            bestCreatureTarget = *target;
            haveCreature = true;
        }
    }

    if (haveCreature)
        return bestCreatureTarget;

    if (!haveMarker || !bestMarkerCredit)
        return {};

    CombatTarget target;
    target.Pos = bestMarker;
    target.StopDistance = 0.25f;
    target.QuestId = bestMarkerCredit->QuestId;
    target.CreditEntry = bestMarkerCredit->CreditEntry;
    return target;
}

bool PlayerbotClient::HasLogIncompleteMonsterOnThisMap(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return false;

    uint32 const mapId = player->GetMap()->GetId();
    std::vector<IncompleteMonsterCredit> credits;
    CollectIncompleteMonsterCredits(player, credits);

    for (IncompleteMonsterCredit const& credit : credits)
    {
        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.CreditEntry), credit.StorageIndex, blobs);
        if (!blobs.empty())
            return true;
    }

    return false;
}

Optional<PlayerbotClient::GameObjectTarget> PlayerbotClient::FindLogIncompleteGameObjectTarget(Player* player, std::unordered_set<ObjectGuid> const& skip)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    Map* map = player->GetMap();
    uint32 const mapId = map->GetId();

    std::vector<IncompleteGameObjectCredit> credits;
    CollectIncompleteGameObjectCredits(player, credits);
    if (credits.empty())
        return {};

    GameObjectTarget bestGoTarget;
    float bestGoDist = std::numeric_limits<float>::max();
    bool haveGo = false;
    Position bestMarker;
    IncompleteGameObjectCredit const* bestMarkerCredit = nullptr;
    float bestMarkerDist = std::numeric_limits<float>::max();
    bool haveMarker = false;

    for (IncompleteGameObjectCredit const& credit : credits)
    {
        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.GoEntry), credit.StorageIndex, blobs);
        if (blobs.empty())
            continue;

        LoadPoiGrids(map, blobs);

        for (ObjectivePoiBlob const& blob : blobs)
        {
            for (Position const& point : blob.Points)
            {
                float const dist = player->GetExactDist(point);
                if (dist >= bestMarkerDist)
                    continue;

                bestMarkerDist = dist;
                bestMarker = point;
                bestMarkerCredit = &credit;
                haveMarker = true;
            }
        }

        for (auto const& pair : map->GetGameObjectBySpawnIdStore())
        {
            GameObject* go = pair.second;
            if (!go || skip.contains(go->GetGUID()))
                continue;
            if (!GameObjectIsUsableForObjective(player, go, credit.GoEntry))
                continue;
            if (!PositionIsInPoiArea(*go, blobs))
                continue;

            float const dist = player->GetExactDist(go);
            if (dist >= bestGoDist)
                continue;

            Optional<GameObjectTarget> target = MakeGameObjectUseTarget(player, go, credit.QuestId);
            if (!target)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that object.",
                    player->GetName(), go->GetGUID().ToString());
                continue;
            }

            bestGoDist = dist;
            bestGoTarget = *target;
            haveGo = true;
        }
    }

    if (haveGo)
        return bestGoTarget;

    if (!haveMarker || !bestMarkerCredit)
        return {};

    GameObjectTarget target;
    target.Pos = bestMarker;
    target.StopDistance = 0.25f;
    target.QuestId = bestMarkerCredit->QuestId;
    target.GoEntry = bestMarkerCredit->GoEntry;
    return target;
}

bool PlayerbotClient::HasLogIncompleteGameObjectOnThisMap(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return false;

    uint32 const mapId = player->GetMap()->GetId();
    std::vector<IncompleteGameObjectCredit> credits;
    CollectIncompleteGameObjectCredits(player, credits);

    for (IncompleteGameObjectCredit const& credit : credits)
    {
        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.GoEntry), credit.StorageIndex, blobs);
        if (!blobs.empty())
            return true;
    }

    return false;
}

bool PlayerbotClient::GameObjectTargetStillNeeded(Player* player, GameObjectTarget const& target)
{
    if (!player || target.QuestId <= 0 || !target.GoEntry)
        return false;
    if (player->GetQuestStatus(uint32(target.QuestId)) != QUEST_STATUS_INCOMPLETE)
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(uint32(target.QuestId));
    if (!quest)
        return false;

    for (QuestObjective const& objective : quest->GetObjectives())
    {
        if (objective.Type != QUEST_OBJECTIVE_GAMEOBJECT)
            continue;
        if (uint32(objective.ObjectID) != target.GoEntry)
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

bool PlayerbotClient::TryUseGameObject(Player* player, GameObjectTarget const& target)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || target.GoGuid.IsEmpty())
        return false;

    if (!player->GetGameObjectIfCanInteractWith(target.GoGuid))
        return false;

    QueueGameObjUse(player->GetSession(), target.GoGuid);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_GAME_OBJ_USE on {} for quest {}.",
        player->GetName(), target.GoGuid.ToString(), target.QuestId);
    return true;
}

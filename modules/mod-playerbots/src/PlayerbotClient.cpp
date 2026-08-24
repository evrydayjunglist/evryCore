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
#include "Common.h"
#include "ConditionMgr.h"
#include "Containers.h"
#include "Corpse.h"
#include "Creature.h"
#include "CreatureData.h"
#include "DBCEnums.h"
#include "DB2Stores.h"
#include "Duration.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Loot.h"
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
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellDefines.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "UnitDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>
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

namespace
{
    // Unit keeps the movement sequence on a protected counter. SendTeleportPacket writes that
    // value as SMSG_MOVE_TELEPORT SequenceIndex and then increments it.
    struct UnitMovementCounterAccess : Unit
    {
        UnitMovementCounterAccess() = delete;
        uint32 Read() const { return m_movementCounter; }
    };

    uint32 UnitMovementCounter(Unit const* unit)
    {
        return static_cast<UnitMovementCounterAccess const*>(unit)->Read();
    }
}

void PlayerbotClient::QueueMoveTeleportAck(Player* player)
{
    if (!player || !player->GetSession())
        return;

    Unit const* mover = player->GetUnitBeingMoved();
    if (!mover)
        mover = player;

    // Echo SequenceIndex. After SendTeleportPacket the counter is one past that value.
    uint32 const counter = UnitMovementCounter(player);
    int32 const ackIndex = counter ? int32(counter - 1) : 0;
    int32 const moveTime = int32(GameTime::GetGameTimeMS());

    WorldPacket packet(CMSG_MOVE_TELEPORT_ACK);
    packet << mover->GetGUID();
    packet << ackIndex;
    packet << moveTime;
    packet.SetReceiveTime(GameTime::Now());
    player->GetSession()->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueWorldPortResponse(WorldSession* session)
{
    if (!session)
        return;

    WorldPacket packet(CMSG_WORLD_PORT_RESPONSE);
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

void PlayerbotClient::SendMovementUpdate(WorldSession* session, MovementInfo const& movementInfo)
{
    if (!session)
        return;

    MovementInfo status = movementInfo;
    WorldPackets::Movement::MoveUpdate update;
    update.Status = &status;
    session->SendPacket(update.Write());
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

void PlayerbotClient::QueueUseItem(Player* player, Item* item, ObjectGuid unitTarget, uint32 spellId)
{
    if (!player || !player->GetSession() || !player->GetMap() || !item || unitTarget.IsEmpty() || !spellId)
        return;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, player->GetMap()->GetDifficultyID());
    int32 visualId = 0;
    if (spellInfo)
        visualId = int32(player->GetCastSpellXSpellVisualId(spellInfo));

    ObjectGuid const castId = ObjectGuid::Create<HighGuid::Cast>(SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), spellId,
        player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

    WorldPacket packet(CMSG_USE_ITEM);
    packet << uint8(item->GetBagSlot());
    packet << uint8(item->GetSlot());
    packet << item->GetGUID();

    packet << castId;
    packet << uint8(0);          // SendCastFlags
    packet << int32(0);          // Misc[0]
    packet << int32(0);          // Misc[1]
    packet << int32(0);          // Misc[2]
    packet << int32(spellId);
    packet << int32(visualId);   // SpellXSpellVisualID
    packet << int32(0);          // ScriptVisualID
    packet << float(0.0f);       // MissileTrajectory.Pitch
    packet << float(0.0f);       // MissileTrajectory.Speed
    packet << ObjectGuid();      // CraftingNPC
    packet << uint32(0);         // ExtraCurrencyCosts size
    packet << uint32(0);         // CraftingReagents size
    packet << uint32(0);         // RemovedReagents size
    packet << uint8(0);          // CraftingCastFlags

    packet.WriteBit(false);      // ReceiveTime
    packet.WriteBit(false);      // MoveUpdate
    packet.WriteBits(0, 2);      // Weight size
    packet.WriteBit(false);      // CraftingOrderID

    packet << uint32(TARGET_FLAG_UNIT);
    packet << unitTarget;
    packet << ObjectGuid();      // Item
    packet << ObjectGuid();      // HousingGUID
    packet.WriteBit(false);      // HousingIsResident
    packet.WriteBit(false);      // SrcLocation
    packet.WriteBit(false);      // DstLocation
    packet.WriteBit(false);      // Orientation
    packet.WriteBit(false);      // MapID
    packet.WriteBits(0, 7);      // Name length
    packet.FlushBits();
    player->GetSession()->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueCastSpell(Player* player, ObjectGuid unitTarget, uint32 spellId)
{
    if (!player || !player->GetSession() || !player->GetMap() || unitTarget.IsEmpty() || !spellId)
        return;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, player->GetMap()->GetDifficultyID());
    int32 visualId = 0;
    if (spellInfo)
        visualId = int32(player->GetCastSpellXSpellVisualId(spellInfo));

    ObjectGuid const castId = ObjectGuid::Create<HighGuid::Cast>(SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), spellId,
        player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

    WorldPacket packet(CMSG_CAST_SPELL);
    packet << castId;
    packet << uint8(0);          // SendCastFlags
    packet << int32(0);          // Misc[0]
    packet << int32(0);          // Misc[1]
    packet << int32(0);          // Misc[2]
    packet << int32(spellId);
    packet << int32(visualId);   // SpellXSpellVisualID
    packet << int32(0);          // ScriptVisualID
    packet << float(0.0f);       // MissileTrajectory.Pitch
    packet << float(0.0f);       // MissileTrajectory.Speed
    packet << ObjectGuid();      // CraftingNPC
    packet << uint32(0);         // ExtraCurrencyCosts size
    packet << uint32(0);         // CraftingReagents size
    packet << uint32(0);         // RemovedReagents size
    packet << uint8(0);          // CraftingCastFlags

    packet.WriteBit(false);      // ReceiveTime
    packet.WriteBit(false);      // MoveUpdate
    packet.WriteBits(0, 2);      // Weight size
    packet.WriteBit(false);      // CraftingOrderID

    packet << uint32(TARGET_FLAG_UNIT);
    packet << unitTarget;
    packet << ObjectGuid();      // Item
    packet << ObjectGuid();      // HousingGUID
    packet.WriteBit(false);      // HousingIsResident
    packet.WriteBit(false);      // SrcLocation
    packet.WriteBit(false);      // DstLocation
    packet.WriteBit(false);      // Orientation
    packet.WriteBit(false);      // MapID
    packet.WriteBits(0, 7);      // Name length
    packet.FlushBits();
    player->GetSession()->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueSetFacing(Player* player, WorldObject const* lookAt)
{
    if (!player || !player->GetSession() || !lookAt)
        return;

    MovementInfo info = player->m_movementInfo;
    info.guid = player->GetGUID();
    info.time = GameTime::GetGameTimeMS();
    info.pos = player->GetPosition();
    info.pos.SetOrientation(player->GetAbsoluteAngle(lookAt));
    QueueMovement(player->GetSession(), CMSG_MOVE_SET_FACING, info);
}

void PlayerbotClient::QueueLootUnit(WorldSession* session, ObjectGuid creatureGuid)
{
    if (!session || creatureGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_LOOT_UNIT);
    packet << creatureGuid;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueLootItem(WorldSession* session, ObjectGuid lootObj, uint8 lootListId)
{
    if (!session || lootObj.IsEmpty())
        return;

    WorldPacket packet(CMSG_LOOT_ITEM);
    packet << uint32(1);
    packet << lootObj;
    packet << uint8(lootListId);
    packet.WriteBit(false); // IsSoftInteract stays false
    packet.FlushBits();
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueLootMoney(WorldSession* session)
{
    if (!session)
        return;

    WorldPacket packet(CMSG_LOOT_MONEY);
    packet.WriteBit(false); // IsSoftInteract stays false
    packet.FlushBits();
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueLootRelease(WorldSession* session, ObjectGuid unitGuid)
{
    if (!session || unitGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_LOOT_RELEASE);
    packet << unitGuid;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueRepopRequest(WorldSession* session)
{
    if (!session)
        return;

    WorldPacket packet(CMSG_REPOP_REQUEST);
    packet.WriteBit(false); // CheckInstance stays false
    packet.FlushBits();
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueReclaimCorpse(WorldSession* session, ObjectGuid corpseGuid)
{
    if (!session || corpseGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_RECLAIM_CORPSE);
    packet << corpseGuid;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueSpiritHealerActivate(WorldSession* session, ObjectGuid healerGuid)
{
    if (!session || healerGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_SPIRIT_HEALER_ACTIVATE);
    packet << healerGuid;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueStandStateChange(WorldSession* session, UnitStandStateType standState)
{
    if (!session)
        return;

    WorldPacket packet(CMSG_STAND_STATE_CHANGE);
    packet << uint8(standState);
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueListInventory(WorldSession* session, ObjectGuid vendorGuid)
{
    if (!session || vendorGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_LIST_INVENTORY);
    packet << vendorGuid;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueSellAllJunkItems(WorldSession* session, ObjectGuid vendorGuid)
{
    if (!session || vendorGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_SELL_ALL_JUNK_ITEMS);
    packet << vendorGuid;
    session->QueuePacket(std::move(packet));
}

void PlayerbotClient::QueueRepairItem(WorldSession* session, ObjectGuid vendorGuid)
{
    if (!session || vendorGuid.IsEmpty())
        return;

    WorldPacket packet(CMSG_REPAIR_ITEM);
    packet << vendorGuid;
    packet << ObjectGuid();
    packet.WriteBit(false);
    packet.FlushBits();
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

    struct IncompleteItemCredit
    {
        int32 QuestId = 0;
        uint32 ItemId = 0;
        uint32 ObjectiveId = 0;
        int8 StorageIndex = 0;
    };

    void CollectIncompleteItemCredits(Player* player, std::vector<IncompleteItemCredit>& out)
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
                if (objective.Type != QUEST_OBJECTIVE_ITEM || objective.ObjectID <= 0)
                    continue;
                if (objective.Flags & QUEST_OBJECTIVE_FLAG_OPTIONAL)
                    continue;
                if (objective.Flags & QUEST_OBJECTIVE_FLAG_HIDDEN)
                    continue;
                // This Flags2 bit means the item is not stored in bags, not that it is not a world drop.
                if (!player->IsQuestObjectiveCompletable(questId, objective.ID))
                    continue;
                if (player->IsQuestObjectiveComplete(questId, objective.ID))
                    continue;

                IncompleteItemCredit credit;
                credit.QuestId = int32(questId);
                credit.ItemId = uint32(objective.ObjectID);
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

    bool PlayerCanSeeOrDetect(Player const* player, WorldObject const* obj)
    {
        if (!player || !obj)
            return false;
        return player->CanSeeOrDetect(obj);
    }

    // Terrain and spawned objects. This is the same ray the core uses for a player. It is not stealth or phase.
    bool PlayerHasLineOfSight(Player const* player, WorldObject const* obj)
    {
        if (!player || !obj)
            return false;
        return player->IsWithinLOSInMap(obj);
    }

    bool GameObjectIsSelectable(Player const* player, GameObject const* go)
    {
        if (!player || !go || !go->IsInWorld() || !go->isSpawned())
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
        if (!PlayerCanSeeOrDetect(player, go))
            return false;
        return true;
    }

    bool GameObjectIsUsableForObjective(Player const* player, GameObject const* go, uint32 entry)
    {
        if (!GameObjectIsSelectable(player, go))
            return false;
        if (go->GetEntry() != entry)
            return false;
        if (!go->ActivateToQuest(player))
            return false;
        return true;
    }

    bool GameObjectGivesQuestItem(GameObject const* go, uint32 itemId)
    {
        if (!go || !itemId)
            return false;

        std::vector<uint32> const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
        if (!items)
            return false;

        for (uint32 id : *items)
        {
            if (id == itemId)
                return true;
        }

        return false;
    }

    bool GameObjectLootStillHasQuestItem(Player const* player, GameObject const* go, uint32 itemId)
    {
        if (!player || !go || !itemId)
            return false;

        Loot const* loot = go->GetLootForPlayer(player);
        if (!loot)
            return true;

        for (LootItem const& item : loot->items)
        {
            if (item.is_looted || item.itemid != itemId)
                continue;
            return true;
        }

        return false;
    }

    bool GameObjectIsUsableForItemObjective(Player const* player, GameObject const* go, uint32 itemId)
    {
        if (!GameObjectIsSelectable(player, go))
            return false;
        if (!GameObjectGivesQuestItem(go, itemId))
            return false;
        if (!go->ActivateToQuest(player))
            return false;
        if (!GameObjectLootStillHasQuestItem(player, go, itemId))
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

    constexpr float SKIP_YELLOW_YARDS = 5.0f;

    bool BlobContains(ObjectivePoiBlob const& blob, Position const& pos)
    {
        return pos.GetExactDist(blob.Centroid) <= blob.Radius + 5.0f;
    }

    bool PointIsSkipped(Position const& point, PlayerbotClient::MapYellowFilter const& filter)
    {
        if (filter.SkipPos && point.GetExactDist(*filter.SkipPos) <= SKIP_YELLOW_YARDS)
            return true;

        if (filter.SkipPositions)
        {
            for (Position const& skip : *filter.SkipPositions)
            {
                if (point.GetExactDist(skip) <= SKIP_YELLOW_YARDS)
                    return true;
            }
        }

        return false;
    }

    bool KeepThisCredit(int32 questId, uint32 entry, PlayerbotClient::MapYellowFilter const& filter)
    {
        if (!filter.KeepQuest)
            return true;
        if (filter.QuestId && questId != filter.QuestId)
            return false;
        if (filter.Entry && entry != filter.Entry)
            return false;
        return true;
    }

    bool SkipAllMarkersForCredit(int32 questId, uint32 entry, PlayerbotClient::MapYellowFilter const& filter)
    {
        if (filter.KeepQuest || !filter.QuestId)
            return false;
        if (questId != filter.QuestId)
            return false;
        if (filter.Entry && entry != filter.Entry)
            return false;
        return true;
    }

    int MarkerBlobRank(ObjectivePoiBlob const& blob, PlayerbotClient::MapYellowFilter const& filter)
    {
        if (!filter.KeepQuest)
            return 1;

        if (filter.SkipPos && BlobContains(blob, *filter.SkipPos))
            return 0;

        if (filter.SkipPositions)
        {
            for (Position const& skip : *filter.SkipPositions)
            {
                if (BlobContains(blob, skip))
                    return 0;
            }
        }

        return 1;
    }

    bool BetterMarker(int rank, float dist, int bestRank, float bestDist, bool have)
    {
        if (!have)
            return true;
        if (rank != bestRank)
            return rank < bestRank;
        return dist < bestDist;
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
        if (!player || !creature)
            return {};

        PlayerbotClient::CombatTarget target;
        target.CreatureGuid = creature->GetGUID();
        target.StopDistance = 0.25f;
        target.QuestId = questId;
        target.CreditEntry = creditEntry;

        // Kill walks aim at the mob. A stand-beside pin is for talk and use, and it pathfinds from
        // her feet. Doing that for every spawn every tick skips a cave full of quest mobs and never
        // starts the walk. Melee range is still when she swings.
        if (player->IsWithinMeleeRange(creature))
            target.Pos = player->GetPosition();
        else
            target.Pos = creature->GetPosition();

        return target;
    }

    uint32 HeldQuestStartItem(Player* player, uint32 questId)
    {
        if (!player || !questId)
            return 0;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            return 0;

        uint32 const itemId = quest->GetSrcItemId();
        if (!itemId || !player->HasItemCount(itemId))
            return 0;

        return itemId;
    }

    uint32 GetItemOnUseSpellId(Item const* item)
    {
        if (!item)
            return 0;

        for (ItemEffectEntry const* effect : item->GetEffects())
        {
            if (effect && effect->SpellID > 0 && effect->TriggerType == ITEM_SPELLTRIGGER_ON_USE)
                return uint32(effect->SpellID);
        }

        return 0;
    }

    bool ItemSpellCanTargetCreature(Player* player, uint32 itemId, Creature const* creature)
    {
        if (!player || !itemId || !creature)
            return false;

        Item* item = player->GetItemByEntry(itemId);
        if (!item)
            return false;

        uint32 const spellId = GetItemOnUseSpellId(item);
        if (!spellId)
            return false;

        // Same caster / unit-target order Spell::CheckCast uses for CONDITION_SOURCE_TYPE_SPELL.
        return sConditionMgr->IsObjectMeetingNotGroupedConditions(CONDITION_SOURCE_TYPE_SPELL, spellId, player, creature);
    }

    char const* CombatSpellName(SpellInfo const* spellInfo)
    {
        if (spellInfo && spellInfo->SpellName)
        {
            char const* name = (*spellInfo->SpellName)[DEFAULT_LOCALE];
            if (name && name[0])
                return name;
        }

        return "unknown";
    }

    bool SpellHasCombatDamage(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return false;

        if (spellInfo->IsAutoRepeatRangedSpell() || spellInfo->HasAttribute(SPELL_ATTR0_CU_CHARGE))
            return true;

        if (spellInfo->HasAttribute(SPELL_ATTR0_USES_RANGED_SLOT) && !spellInfo->IsPositive())
            return true;

        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (!effect.IsEffect())
                continue;

            switch (effect.Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_ENVIRONMENTAL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                case SPELL_EFFECT_DAMAGE_FROM_MAX_HEALTH_PCT:
                case SPELL_EFFECT_ATTACK:
                    return true;
                case SPELL_EFFECT_APPLY_AURA:
                case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
                case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                    switch (effect.ApplyAuraName)
                    {
                        case SPELL_AURA_PERIODIC_DAMAGE:
                        case SPELL_AURA_PERIODIC_DAMAGE_PERCENT:
                        case SPELL_AURA_PERIODIC_LEECH:
                        case SPELL_AURA_PERIODIC_WEAPON_PERCENT_DAMAGE:
                            return true;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
        }

        return false;
    }

    bool CombatSpellIsUtility(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return true;

        if (spellInfo->IsProfession() || spellInfo->HasAttribute(SPELL_ATTR0_IS_TRADESKILL))
            return true;

        if (spellInfo->HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY_SPELLBOOK_AURA_ICON_COMBAT_LOG)
            || spellInfo->HasAttribute(SPELL_ATTR4_NOT_IN_SPELLBOOK))
            return true;

        if (spellInfo->HasAura(SPELL_AURA_MOUNTED) || spellInfo->HasAura(SPELL_AURA_MOD_SHAPESHIFT)
            || spellInfo->HasAura(SPELL_AURA_SCHOOL_ABSORB))
            return true;

        if (spellInfo->HasEffect(SPELL_EFFECT_INTERRUPT_CAST) || spellInfo->HasEffect(SPELL_EFFECT_SUMMON)
            || spellInfo->HasEffect(SPELL_EFFECT_SUMMON_PET) || spellInfo->HasEffect(SPELL_EFFECT_TELEPORT_UNITS)
            || spellInfo->HasEffect(SPELL_EFFECT_TRANS_DOOR) || spellInfo->HasEffect(SPELL_EFFECT_LEAP)
            || spellInfo->HasEffect(SPELL_EFFECT_JUMP) || spellInfo->HasEffect(SPELL_EFFECT_JUMP_DEST)
            || spellInfo->HasEffect(SPELL_EFFECT_LEAP_BACK) || spellInfo->HasEffect(SPELL_EFFECT_PICKPOCKET)
            || spellInfo->HasEffect(SPELL_EFFECT_TRADE_SKILL) || spellInfo->HasEffect(SPELL_EFFECT_ATTACK_ME))
            return true;

        if (spellInfo->HasAttribute(SPELL_ATTR0_CU_AURA_CC) || spellInfo->HasAttribute(SPELL_ATTR0_CU_PICKPOCKET))
            return true;

        if (spellInfo->HasAura(SPELL_AURA_MOD_STUN) || spellInfo->HasAura(SPELL_AURA_MOD_FEAR)
            || spellInfo->HasAura(SPELL_AURA_MOD_CONFUSE) || spellInfo->HasAura(SPELL_AURA_MOD_SILENCE)
            || spellInfo->HasAura(SPELL_AURA_MOD_ROOT) || spellInfo->HasAura(SPELL_AURA_MOD_ROOT_2))
            return true;

        if (spellInfo->HasAura(SPELL_AURA_MOD_DECREASE_SPEED)
            && !spellInfo->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE)
            && !spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE))
            return true;

        switch (spellInfo->GetSpellSpecific())
        {
            case SPELL_SPECIFIC_FOOD:
            case SPELL_SPECIFIC_DRINK:
            case SPELL_SPECIFIC_FOOD_AND_DRINK:
                return true;
            default:
                break;
        }

        return false;
    }

    bool CombatDamageSpellIsEligible(Player const* player, SpellInfo const* spellInfo)
    {
        if (!player || !spellInfo)
            return false;

        if (spellInfo->IsPassive() || spellInfo->IsPositive())
            return false;

        if (CombatSpellIsUtility(spellInfo) || !SpellHasCombatDamage(spellInfo))
            return false;

        return player->HasActiveSpell(spellInfo->Id);
    }

    bool CombatSpellAlreadyQueued(Player const* player, SpellInfo const* spellInfo)
    {
        if (!player || !spellInfo)
            return false;

        if (spellInfo->IsNextMeleeSwingSpell())
        {
            if (Spell const* melee = player->GetCurrentSpell(CURRENT_MELEE_SPELL))
                if (melee->GetSpellInfo() && melee->GetSpellInfo()->Id == spellInfo->Id)
                    return true;
        }

        if (Spell const* generic = player->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if (generic->GetSpellInfo() && generic->GetSpellInfo()->Id == spellInfo->Id)
                return true;

        if (spellInfo->IsAutoRepeatRangedSpell())
        {
            if (Spell const* repeat = player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                if (repeat->GetSpellInfo() && repeat->GetSpellInfo()->Id == spellInfo->Id)
                    return true;
        }

        return false;
    }

    bool CombatSpellIsReady(Player const* player, SpellInfo const* spellInfo)
    {
        if (!player || !spellInfo || !player->GetSpellHistory())
            return false;

        // Wait until GCD is actually up. CanRequestSpellCast allows a 400 ms early
        // queue; that is one pending press on a real client, not a press every tick.
        if (player->GetSpellHistory()->GetRemainingGlobalCooldown(spellInfo) > 0ms)
            return false;

        if (!player->GetSpellHistory()->IsReady(spellInfo))
            return false;

        return true;
    }

    SpellCastResult CheckCombatSpellCast(Player* player, Unit* target, SpellInfo const* spellInfo)
    {
        if (!player || !target || !spellInfo)
            return SPELL_FAILED_UNKNOWN;

        Spell* look = new Spell(player, spellInfo, TRIGGERED_NONE);
        look->m_fromClient = true;
        look->m_targets.SetUnitTarget(target);
        SpellCastResult const result = look->CheckCast(true);
        if (result != SPELL_CAST_OK)
        {
            delete look;
            return result;
        }

        // Spell::prepare fills m_powerCost before CheckCast. That member is not
        // writable from the module, so CheckPower inside CheckCast sees an empty
        // cost. Match that check here with the same CalcPowerCost.
        for (SpellPowerCost const& cost : spellInfo->CalcPowerCost(player, spellInfo->GetSchoolMask(), look))
        {
            if (cost.Power == POWER_HEALTH)
            {
                if (int64(player->GetHealth()) <= cost.Amount)
                {
                    delete look;
                    return SPELL_FAILED_CASTER_AURASTATE;
                }
                continue;
            }

            if (cost.Power >= MAX_POWERS || cost.Power == POWER_RUNES)
                continue;

            if (int32(player->GetPower(cost.Power)) < cost.Amount)
            {
                delete look;
                return SPELL_FAILED_NO_POWER;
            }
        }

        delete look;
        return SPELL_CAST_OK;
    }

    SpellCastResult CheckUseItemCast(Player* player, Item* item, Unit* target, SpellInfo const* spellInfo)
    {
        if (!player || !item || !target || !spellInfo)
            return SPELL_FAILED_UNKNOWN;

        Spell* look = new Spell(player, spellInfo, TRIGGERED_NONE);
        look->m_fromClient = true;
        look->m_CastItem = item;
        look->m_targets.SetUnitTarget(target);
        SpellCastResult const result = look->CheckCast(true);
        delete look;
        return result;
    }

    bool CreatureIsInInteractRange(Player const* player, Creature const* creature)
    {
        if (!player || !creature)
            return false;

        return player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f);
    }

    Optional<PlayerbotClient::UseItemOnUnitTarget> MakeUseItemOnUnitTarget(Player* player, Creature* creature, int32 questId, uint32 creditEntry, uint32 itemId)
    {
        if (!player || !creature || !questId || !itemId)
            return {};

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
        {
            if (!CreatureIsInInteractRange(player, creature))
                return {};

            standPos = player->GetPosition();
        }

        PlayerbotClient::UseItemOnUnitTarget target;
        target.CreatureGuid = creature->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        target.QuestId = questId;
        target.CreditEntry = creditEntry;
        target.ItemId = itemId;
        return target;
    }

    bool CreatureDropsQuestItem(Creature const* creature, uint32 itemId)
    {
        if (!creature || !itemId)
            return false;

        Difficulty difficulty = DIFFICULTY_NONE;
        if (Map const* map = creature->GetMap())
            difficulty = map->GetDifficultyID();

        auto hasItem = [itemId](std::vector<uint32> const* items)
        {
            if (!items)
                return false;
            for (uint32 id : *items)
            {
                if (id == itemId)
                    return true;
            }
            return false;
        };

        if (hasItem(sObjectMgr->GetCreatureQuestItemList(creature->GetEntry(), difficulty)))
            return true;
        if (difficulty != DIFFICULTY_NONE)
            return hasItem(sObjectMgr->GetCreatureQuestItemList(creature->GetEntry(), DIFFICULTY_NONE));
        return false;
    }

    bool CorpseHasQuestItemFor(Player const* player, Creature const* creature, uint32 itemId)
    {
        if (!player || !creature || !itemId)
            return false;
        if (!player->isAllowedToLoot(creature))
            return false;

        Loot const* loot = creature->GetLootForPlayer(player);
        if (!loot)
            return false;

        for (LootItem const& item : loot->items)
        {
            if (item.is_looted || item.itemid != itemId)
                continue;
            return true;
        }

        return false;
    }

    bool CreatureWouldPullIfAlive(Player* player, Creature const* creature)
    {
        if (!player || !creature || !creature->IsAlive())
            return false;
        if (creature->IsCivilian() || creature->IsNeutralToAll())
            return false;
        if (creature->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) || creature->HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC))
            return false;
        if (player->IsValidAttackTarget(creature))
            return true;
        return creature->IsHostileTo(player);
    }

    bool SpiritHealerIsUsable(Player const* player, Creature const* creature)
    {
        if (!player || !creature || !creature->IsAlive())
            return false;
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_SPIRIT_HEALER))
            return false;
        CreatureDifficulty const* difficulty = creature->GetCreatureDifficulty();
        if (!difficulty || !(difficulty->TypeFlags & CREATURE_TYPE_FLAG_VISIBLE_TO_GHOSTS))
            return false;
        if (!player->InSamePhase(creature))
            return false;
        if (creature->IsPrivateObject() && !creature->CheckPrivateObjectOwnerVisibility(player))
            return false;
        return true;
    }

    Optional<PlayerbotClient::SpiritHealerTarget> MakeSpiritHealerTarget(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return {};

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
            return {};

        PlayerbotClient::SpiritHealerTarget target;
        target.NpcGuid = creature->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        return target;
    }

    Optional<PlayerbotClient::ItemLootTarget> MakeItemLootTarget(Player* player, Creature* creature, int32 questId, uint32 itemId, bool lootCorpse)
    {
        if (!player || !creature || !questId || !itemId)
            return {};

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
            return {};

        PlayerbotClient::ItemLootTarget target;
        target.CreatureGuid = creature->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        target.QuestId = questId;
        target.ItemId = itemId;
        target.CreatureEntry = creature->GetEntry();
        target.LootCorpse = lootCorpse;
        return target;
    }

    Optional<PlayerbotClient::ItemLootTarget> MakeItemLootTargetFromGameObject(Player* player, GameObject* go, int32 questId, uint32 itemId)
    {
        if (!player || !go || !questId || !itemId)
            return {};

        Position standPos;
        if (!PlayerbotWalker::PickApproachPosition(player, go, GameObjectStandDistance(go), standPos))
            return {};

        PlayerbotClient::ItemLootTarget target;
        target.GoGuid = go->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        target.QuestId = questId;
        target.ItemId = itemId;
        target.GoEntry = go->GetEntry();
        return target;
    }

    bool CreatureInInteractRange(Player const* player, Creature const* creature)
    {
        if (!player || !creature)
            return false;

        return player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f);
    }

    bool LootSlotIsTakeable(Optional<LootSlotType> ui)
    {
        return ui && (*ui == LOOT_SLOT_TYPE_ALLOW_LOOT || *ui == LOOT_SLOT_TYPE_OWNER);
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

    constexpr float TAKEABLE_SEARCH_NEAR = 80.0f;
    constexpr float TAKEABLE_SEARCH_FAR = 150.0f;

    Optional<PlayerbotClient::QuestTarget> MakeQuestTarget(Player* player, Creature* creature, int32 questId, bool turnIn)
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
        target.TurnIn = turnIn;
        return target;
    }

    bool CreatureIsUsableQuestGiver(Player const* player, Creature const* creature)
    {
        if (!player || !creature || !creature->IsAlive())
            return false;
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            return false;
        if (!player->InSamePhase(creature))
            return false;
        if (creature->IsPrivateObject() && !creature->CheckPrivateObjectOwnerVisibility(player))
            return false;
        return true;
    }

    int32 FirstTakeableQuestId(Player* player, Creature* creature, int32 skipQuestId)
    {
        if (!player || !creature)
            return 0;

        player->PrepareQuestMenu(creature->GetGUID());
        QuestMenu& menu = player->PlayerTalkClass->GetQuestMenu();
        for (uint8 i = 0; i < menu.GetMenuItemCount(); ++i)
        {
            QuestMenuItem const& item = menu.GetItem(i);
            if (skipQuestId && int32(item.QuestId) == skipQuestId)
                continue;
            if (item.QuestIcon != 2)
                continue;

            Quest const* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
            if (!quest)
                continue;
            if (!player->CanTakeQuest(quest, false) || !player->CanAddQuest(quest, false))
                continue;

            return int32(item.QuestId);
        }

        return 0;
    }

    Optional<PlayerbotClient::QuestTarget> MakeTakeableTarget(Player* player, Creature* creature, int32 skipQuestId)
    {
        if (!CreatureIsUsableQuestGiver(player, creature))
            return {};

        int32 const questId = FirstTakeableQuestId(player, creature, skipQuestId);
        if (!questId)
            return {};

        return MakeQuestTarget(player, creature, questId, false);
    }

    Optional<PlayerbotClient::QuestTarget> FindTakeableQuestInRange(Player* player, float range, uint32 zoneId, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId)
    {
        if (!player || !player->IsInWorld())
            return {};

        std::vector<Creature*> nearby;
        FindCreatureOptions options;
        options.IsAlive = FindCreatureAliveState::Alive;
        player->GetCreatureListWithOptionsInGrid(nearby, range, options);

        Optional<PlayerbotClient::QuestTarget> best;
        float bestDist = std::numeric_limits<float>::max();

        for (Creature* creature : nearby)
        {
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
            if (creature->GetZoneId() != zoneId)
                continue;

            float const dist = player->GetExactDist(creature);
            if (dist >= bestDist)
                continue;

            Optional<PlayerbotClient::QuestTarget> target = MakeTakeableTarget(player, creature, skipQuestId);
            if (!target)
                continue;

            bestDist = dist;
            best = target;
        }

        return best;
    }

    Optional<PlayerbotClient::QuestTarget> FindTakeableQuestRestOfZone(Player* player, uint32 zoneId, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId, float minDist)
    {
        if (!player || !player->GetMap())
            return {};

        Optional<PlayerbotClient::QuestTarget> best;
        float bestDist = std::numeric_limits<float>::max();

        for (auto const& pair : player->GetMap()->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
            if (creature->GetZoneId() != zoneId)
                continue;

            float const dist = player->GetExactDist(creature);
            if (dist <= minDist || dist >= bestDist)
                continue;

            Optional<PlayerbotClient::QuestTarget> target = MakeTakeableTarget(player, creature, skipQuestId);
            if (!target)
                continue;

            bestDist = dist;
            best = target;
        }

        return best;
    }

    Creature* FindLivingEnderOnMap(Player* player, std::unordered_set<uint32> const& enderEntries, Optional<Position> const& marker, std::unordered_set<ObjectGuid> const& skip)
    {
        if (!player || !player->GetMap() || enderEntries.empty())
            return nullptr;

        Creature* best = nullptr;
        float bestDist = std::numeric_limits<float>::max();

        for (auto const& pair : player->GetMap()->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
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

Optional<PlayerbotClient::QuestTarget> PlayerbotClient::FindNearbyQuestTarget(Player* player, float range, QuestSearchKind kind, std::unordered_set<ObjectGuid> const& skip)
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
        if (!creature || skip.contains(creature->GetGUID()))
            continue;
        if (!PlayerCanSeeOrDetect(player, creature))
            continue;
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            continue;
        if (!PlayerHasLineOfSight(player, creature))
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
            if (kind == QuestSearchKind::Talk && !isTurnIn && !isAccept)
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

Optional<PlayerbotClient::QuestTarget> PlayerbotClient::FindLogCompleteTurnIn(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId)
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
        if (skipQuestId && int32(questId) == skipQuestId)
            continue;

        std::unordered_set<uint32> enderEntries;
        CollectCreatureEnderEntries(questId, enderEntries);
        if (enderEntries.empty())
            continue;

        Optional<Position> marker = GetFinishedQuestMapMarker(questId, map->GetId(), *player);
        if (marker && !map->IsGridLoaded(*marker))
            map->LoadGrid(marker->GetPositionX(), marker->GetPositionY());

        Creature* creature = FindLivingEnderOnMap(player, enderEntries, marker, skip);
        if (!creature)
            continue;

        Optional<QuestTarget> target = MakeQuestTarget(player, creature, int32(questId), true);
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

// Map work. No line of sight. This zone only, not the continent.
Optional<PlayerbotClient::QuestTarget> PlayerbotClient::FindTakeableQuestInZone(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    uint32 const zoneId = player->GetZoneId();
    if (Optional<QuestTarget> found = FindTakeableQuestInRange(player, TAKEABLE_SEARCH_NEAR, zoneId, skip, skipQuestId))
        return found;
    if (Optional<QuestTarget> found = FindTakeableQuestInRange(player, TAKEABLE_SEARCH_FAR, zoneId, skip, skipQuestId))
        return found;

    return FindTakeableQuestRestOfZone(player, zoneId, skip, skipQuestId, TAKEABLE_SEARCH_FAR);
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
        if (!PlayerCanSeeOrDetect(player, creature))
            continue;
        if (!player->IsValidAttackTarget(creature))
            continue;

        IncompleteMonsterCredit const* matched = nullptr;
        for (IncompleteMonsterCredit const& credit : credits)
        {
            if (HeldQuestStartItem(player, uint32(credit.QuestId)))
                continue;
            if (CreatureGivesMonsterCredit(creature, credit.CreditEntry))
            {
                matched = &credit;
                break;
            }
        }
        if (!matched)
            continue;
        if (!PlayerHasLineOfSight(player, creature))
            continue;

        float const dist = player->GetExactDist(creature);
        if (dist >= bestDist)
            continue;

        Optional<CombatTarget> target = MakeCombatTarget(player, creature, matched->QuestId, matched->CreditEntry);
        if (!target)
            continue;

        bestDist = dist;
        best = *target;
    }

    if (best.CreatureGuid.IsEmpty())
        return {};

    return best;
}

Optional<PlayerbotClient::CombatTarget> PlayerbotClient::FindAttackerTarget(Player* player)
{
    if (!player || !player->IsInWorld())
        return {};

    Creature* best = nullptr;
    float bestDist = std::numeric_limits<float>::max();

    for (Unit* attacker : player->getAttackers())
    {
        Creature* creature = attacker ? attacker->ToCreature() : nullptr;
        if (!creature || !creature->IsAlive())
            continue;
        if (!player->IsValidAttackTarget(creature))
            continue;

        float const dist = player->GetExactDist(creature);
        if (dist >= bestDist)
            continue;

        bestDist = dist;
        best = creature;
    }

    if (!best)
        return {};

    std::vector<IncompleteItemCredit> itemCredits;
    CollectIncompleteItemCredits(player, itemCredits);
    std::vector<IncompleteMonsterCredit> monsterCredits;
    CollectIncompleteMonsterCredits(player, monsterCredits);

    int32 questId = 0;
    uint32 creditEntry = 0;
    uint32 itemId = 0;
    for (IncompleteItemCredit const& credit : itemCredits)
    {
        if (!CreatureDropsQuestItem(best, credit.ItemId))
            continue;
        questId = credit.QuestId;
        itemId = credit.ItemId;
        creditEntry = best->GetEntry();
        break;
    }
    if (!itemId)
    {
        for (IncompleteMonsterCredit const& credit : monsterCredits)
        {
            if (!CreatureGivesMonsterCredit(best, credit.CreditEntry))
                continue;
            questId = credit.QuestId;
            creditEntry = credit.CreditEntry;
            break;
        }
    }

    Optional<CombatTarget> target = MakeCombatTarget(player, best, questId, creditEntry);
    if (!target)
    {
        CombatTarget fallback;
        fallback.CreatureGuid = best->GetGUID();
        fallback.Pos = player->GetPosition();
        fallback.StopDistance = 0.25f;
        fallback.QuestId = questId;
        fallback.CreditEntry = creditEntry;
        fallback.ItemId = itemId;
        return fallback;
    }

    target->ItemId = itemId;
    return target;
}

bool PlayerbotClient::CombatTargetStillNeeded(Player* player, CombatTarget const& target)
{
    if (!player)
        return false;
    if (target.QuestId <= 0)
        return true;
    if (player->GetQuestStatus(uint32(target.QuestId)) != QUEST_STATUS_INCOMPLETE)
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(uint32(target.QuestId));
    if (!quest)
        return false;

    if (target.ItemId)
    {
        for (QuestObjective const& objective : quest->GetObjectives())
        {
            if (objective.Type != QUEST_OBJECTIVE_ITEM)
                continue;
            if (uint32(objective.ObjectID) != target.ItemId)
                continue;
            if (!player->IsQuestObjectiveCompletable(uint32(target.QuestId), objective.ID))
                continue;
            if (player->IsQuestObjectiveComplete(uint32(target.QuestId), objective.ID))
                continue;
            return true;
        }
        return false;
    }

    if (!target.CreditEntry)
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

Optional<PlayerbotClient::CombatTarget> PlayerbotClient::FindLogIncompleteMonsterTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter)
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
    int bestMarkerRank = 2;
    bool haveMarker = false;

    for (IncompleteMonsterCredit const& credit : credits)
    {
        if (HeldQuestStartItem(player, uint32(credit.QuestId)))
            continue;
        if (!KeepThisCredit(credit.QuestId, credit.CreditEntry, filter))
            continue;

        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.CreditEntry), credit.StorageIndex, blobs);
        if (blobs.empty())
            continue;

        LoadPoiGrids(map, blobs);

        if (!SkipAllMarkersForCredit(credit.QuestId, credit.CreditEntry, filter))
        {
            for (ObjectivePoiBlob const& blob : blobs)
            {
                int const rank = MarkerBlobRank(blob, filter);
                for (Position const& point : blob.Points)
                {
                    if (PointIsSkipped(point, filter))
                        continue;

                    float const dist = player->GetExactDist(point);
                    if (!BetterMarker(rank, dist, bestMarkerRank, bestMarkerDist, haveMarker))
                        continue;

                    bestMarkerRank = rank;
                    bestMarkerDist = dist;
                    bestMarker = point;
                    bestMarkerCredit = &credit;
                    haveMarker = true;
                }
            }
        }

        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
            if (PointIsSkipped(*creature, filter))
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
                continue;

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

Optional<PlayerbotClient::ItemLootTarget> PlayerbotClient::MakeCorpseLootTarget(Player* player, Creature* creature)
{
    if (!player || !creature || creature->IsAlive() || !player->isAllowedToLoot(creature))
        return {};

    Position standPos;
    float const standDistance = creature->GetCombatReach() + 1.0f;
    if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
        return {};

    ItemLootTarget target;
    target.CreatureGuid = creature->GetGUID();
    target.Pos = standPos;
    target.StopDistance = 0.25f;
    target.CreatureEntry = creature->GetEntry();
    target.LootCorpse = true;
    return target;
}

Optional<PlayerbotClient::ItemLootTarget> PlayerbotClient::FindNearbyItemLootTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange)
{
    if (!player || !player->IsInWorld())
        return {};

    ItemLootTarget best;
    float bestDist = std::numeric_limits<float>::max();

    std::vector<Creature*> nearbyCreatures;
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Dead;
    player->GetCreatureListWithOptionsInGrid(nearbyCreatures, range, options);

    for (Creature* creature : nearbyCreatures)
    {
        if (!creature || skip.contains(creature->GetGUID()))
            continue;
        if (!player->isAllowedToLoot(creature))
            continue;
        if (mustBeInUseRange && !CreatureInInteractRange(player, creature))
            continue;

        float const dist = player->GetExactDist(creature);
        if (dist >= bestDist)
            continue;

        Optional<ItemLootTarget> target = MakeCorpseLootTarget(player, creature);
        if (!target)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that corpse.",
                player->GetName(), creature->GetGUID().ToString());
            continue;
        }

        bestDist = dist;
        best = *target;
    }

    std::vector<IncompleteItemCredit> credits;
    CollectIncompleteItemCredits(player, credits);
    if (!credits.empty())
    {
        std::vector<GameObject*> nearbyGo;
        player->GetGameObjectListWithOptionsInGrid(nearbyGo, range, {});

        for (GameObject* go : nearbyGo)
        {
            if (!go || skip.contains(go->GetGUID()))
                continue;

            IncompleteItemCredit const* matched = nullptr;
            for (IncompleteItemCredit const& credit : credits)
            {
                if (!GameObjectIsUsableForItemObjective(player, go, credit.ItemId))
                    continue;
                matched = &credit;
                break;
            }
            if (!matched)
                continue;
            if (!PlayerHasLineOfSight(player, go))
                continue;

            if (mustBeInUseRange && !player->GetGameObjectIfCanInteractWith(go->GetGUID()))
                continue;

            float const dist = player->GetExactDist(go);
            if (dist >= bestDist)
                continue;

            Optional<ItemLootTarget> target = MakeItemLootTargetFromGameObject(player, go, matched->QuestId, matched->ItemId);
            if (!target)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that object.",
                    player->GetName(), go->GetGUID().ToString());
                continue;
            }

            bestDist = dist;
            best = *target;
        }
    }

    if (best.CreatureGuid.IsEmpty() && best.GoGuid.IsEmpty())
        return {};

    return best;
}

Optional<PlayerbotClient::ItemLootTarget> PlayerbotClient::FindLogIncompleteItemTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    Map* map = player->GetMap();
    uint32 const mapId = map->GetId();

    std::vector<IncompleteItemCredit> credits;
    CollectIncompleteItemCredits(player, credits);
    if (credits.empty())
        return {};

    ItemLootTarget bestCorpse;
    float bestCorpseDist = std::numeric_limits<float>::max();
    bool haveCorpse = false;
    ItemLootTarget bestGo;
    float bestGoDist = std::numeric_limits<float>::max();
    bool haveGo = false;
    ItemLootTarget bestAlive;
    float bestAliveDist = std::numeric_limits<float>::max();
    bool haveAlive = false;
    Position bestMarker;
    IncompleteItemCredit const* bestMarkerCredit = nullptr;
    float bestMarkerDist = std::numeric_limits<float>::max();
    int bestMarkerRank = 2;
    bool haveMarker = false;

    for (IncompleteItemCredit const& credit : credits)
    {
        if (!KeepThisCredit(credit.QuestId, credit.ItemId, filter))
            continue;

        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.ItemId), credit.StorageIndex, blobs);
        if (blobs.empty())
            continue;

        LoadPoiGrids(map, blobs);

        if (!SkipAllMarkersForCredit(credit.QuestId, credit.ItemId, filter))
        {
            for (ObjectivePoiBlob const& blob : blobs)
            {
                int const rank = MarkerBlobRank(blob, filter);
                for (Position const& point : blob.Points)
                {
                    if (PointIsSkipped(point, filter))
                        continue;

                    float const dist = player->GetExactDist(point);
                    if (!BetterMarker(rank, dist, bestMarkerRank, bestMarkerDist, haveMarker))
                        continue;

                    bestMarkerRank = rank;
                    bestMarkerDist = dist;
                    bestMarker = point;
                    bestMarkerCredit = &credit;
                    haveMarker = true;
                }
            }
        }

        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
            if (PointIsSkipped(*creature, filter))
                continue;
            if (!player->InSamePhase(creature))
                continue;
            if (creature->IsPrivateObject() && !creature->CheckPrivateObjectOwnerVisibility(player))
                continue;
            if (!CreatureDropsQuestItem(creature, credit.ItemId))
                continue;
            if (!PositionIsInPoiArea(*creature, blobs))
                continue;

            float const dist = player->GetExactDist(creature);

            if (!creature->IsAlive())
            {
                if (!CorpseHasQuestItemFor(player, creature, credit.ItemId))
                    continue;
                if (dist >= bestCorpseDist)
                    continue;

                Optional<ItemLootTarget> target = MakeItemLootTarget(player, creature, credit.QuestId, credit.ItemId, true);
                if (!target)
                {
                    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that corpse.",
                        player->GetName(), creature->GetGUID().ToString());
                    continue;
                }

                bestCorpseDist = dist;
                bestCorpse = *target;
                haveCorpse = true;
                continue;
            }

            if (!player->IsValidAttackTarget(creature))
                continue;
            if (dist >= bestAliveDist)
                continue;

            Optional<ItemLootTarget> target = MakeItemLootTarget(player, creature, credit.QuestId, credit.ItemId, false);
            if (!target)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that creature.",
                    player->GetName(), creature->GetGUID().ToString());
                continue;
            }

            bestAliveDist = dist;
            bestAlive = *target;
            haveAlive = true;
        }

        for (auto const& pair : map->GetGameObjectBySpawnIdStore())
        {
            GameObject* go = pair.second;
            if (!go || skip.contains(go->GetGUID()))
                continue;
            if (PointIsSkipped(*go, filter))
                continue;
            if (!GameObjectIsUsableForItemObjective(player, go, credit.ItemId))
                continue;
            if (!PositionIsInPoiArea(*go, blobs))
                continue;

            float const dist = player->GetExactDist(go);
            if (dist >= bestGoDist)
                continue;

            Optional<ItemLootTarget> target = MakeItemLootTargetFromGameObject(player, go, credit.QuestId, credit.ItemId);
            if (!target)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that object.",
                    player->GetName(), go->GetGUID().ToString());
                continue;
            }

            bestGoDist = dist;
            bestGo = *target;
            haveGo = true;
        }
    }

    if (haveCorpse)
        return bestCorpse;
    if (haveGo)
        return bestGo;
    if (haveAlive)
        return bestAlive;

    if (!haveMarker || !bestMarkerCredit)
        return {};

    ItemLootTarget target;
    target.Pos = bestMarker;
    target.StopDistance = 0.25f;
    target.QuestId = bestMarkerCredit->QuestId;
    target.ItemId = bestMarkerCredit->ItemId;
    return target;
}

bool PlayerbotClient::ItemLootTargetStillNeeded(Player* player, ItemLootTarget const& target)
{
    if (!player)
        return false;

    if (!target.CreatureGuid.IsEmpty() && target.LootCorpse)
    {
        Creature* creature = ObjectAccessor::GetCreature(*player, target.CreatureGuid);
        return creature && player->isAllowedToLoot(creature);
    }

    if (target.QuestId <= 0 || !target.ItemId)
        return false;
    if (player->GetQuestStatus(uint32(target.QuestId)) != QUEST_STATUS_INCOMPLETE)
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(uint32(target.QuestId));
    if (!quest)
        return false;

    for (QuestObjective const& objective : quest->GetObjectives())
    {
        if (objective.Type != QUEST_OBJECTIVE_ITEM)
            continue;
        if (uint32(objective.ObjectID) != target.ItemId)
            continue;
        if (!player->IsQuestObjectiveCompletable(uint32(target.QuestId), objective.ID))
            continue;
        if (player->IsQuestObjectiveComplete(uint32(target.QuestId), objective.ID))
            continue;
        return true;
    }

    return false;
}

PlayerbotClient::CombatTarget PlayerbotClient::CombatTargetFromItemLoot(ItemLootTarget const& target)
{
    CombatTarget combat;
    combat.CreatureGuid = target.CreatureGuid;
    combat.Pos = target.Pos;
    combat.StopDistance = target.StopDistance;
    combat.QuestId = target.QuestId;
    combat.CreditEntry = target.CreatureEntry;
    combat.ItemId = target.ItemId;
    return combat;
}

Optional<PlayerbotClient::GameObjectTarget> PlayerbotClient::FindNearbyGameObjectObjectiveTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange)
{
    if (!player || !player->IsInWorld())
        return {};

    std::vector<IncompleteGameObjectCredit> credits;
    CollectIncompleteGameObjectCredits(player, credits);
    if (credits.empty())
        return {};

    std::vector<GameObject*> nearby;
    player->GetGameObjectListWithOptionsInGrid(nearby, range, {});

    GameObjectTarget best;
    float bestDist = std::numeric_limits<float>::max();

    for (GameObject* go : nearby)
    {
        if (!go || skip.contains(go->GetGUID()))
            continue;

        IncompleteGameObjectCredit const* matched = nullptr;
        for (IncompleteGameObjectCredit const& credit : credits)
        {
            if (!GameObjectIsUsableForObjective(player, go, credit.GoEntry))
                continue;
            matched = &credit;
            break;
        }
        if (!matched)
            continue;
        if (!PlayerHasLineOfSight(player, go))
            continue;

        if (mustBeInUseRange && !player->GetGameObjectIfCanInteractWith(go->GetGUID()))
            continue;

        float const dist = player->GetExactDist(go);
        if (dist >= bestDist)
            continue;

        Optional<GameObjectTarget> target = MakeGameObjectUseTarget(player, go, matched->QuestId);
        if (!target)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {} without standing in a spell focus. Skipping that object.",
                player->GetName(), go->GetGUID().ToString());
            continue;
        }

        bestDist = dist;
        best = *target;
    }

    if (best.GoGuid.IsEmpty())
        return {};

    return best;
}

Optional<PlayerbotClient::GameObjectTarget> PlayerbotClient::FindLogIncompleteGameObjectTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter)
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
    int bestMarkerRank = 2;
    bool haveMarker = false;

    for (IncompleteGameObjectCredit const& credit : credits)
    {
        if (!KeepThisCredit(credit.QuestId, credit.GoEntry, filter))
            continue;

        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.GoEntry), credit.StorageIndex, blobs);
        if (blobs.empty())
            continue;

        LoadPoiGrids(map, blobs);

        if (!SkipAllMarkersForCredit(credit.QuestId, credit.GoEntry, filter))
        {
            for (ObjectivePoiBlob const& blob : blobs)
            {
                int const rank = MarkerBlobRank(blob, filter);
                for (Position const& point : blob.Points)
                {
                    if (PointIsSkipped(point, filter))
                        continue;

                    float const dist = player->GetExactDist(point);
                    if (!BetterMarker(rank, dist, bestMarkerRank, bestMarkerDist, haveMarker))
                        continue;

                    bestMarkerRank = rank;
                    bestMarkerDist = dist;
                    bestMarker = point;
                    bestMarkerCredit = &credit;
                    haveMarker = true;
                }
            }
        }

        for (auto const& pair : map->GetGameObjectBySpawnIdStore())
        {
            GameObject* go = pair.second;
            if (!go || skip.contains(go->GetGUID()))
                continue;
            if (PointIsSkipped(*go, filter))
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

Optional<PlayerbotClient::UseItemOnUnitTarget> PlayerbotClient::FindNearbyUseItemOnUnitTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange)
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

    UseItemOnUnitTarget best;
    float bestDist = std::numeric_limits<float>::max();

    for (Creature* creature : nearby)
    {
        if (!creature || skip.contains(creature->GetGUID()))
            continue;
        if (!PlayerCanSeeOrDetect(player, creature))
            continue;

        IncompleteMonsterCredit const* matched = nullptr;
        uint32 itemId = 0;
        for (IncompleteMonsterCredit const& credit : credits)
        {
            uint32 const startItem = HeldQuestStartItem(player, uint32(credit.QuestId));
            if (!startItem)
                continue;
            if (!CreatureGivesMonsterCredit(creature, credit.CreditEntry))
                continue;
            matched = &credit;
            itemId = startItem;
            break;
        }
        if (!matched)
            continue;

        if (!ItemSpellCanTargetCreature(player, itemId, creature))
            continue;

        if (mustBeInUseRange && !CreatureIsInInteractRange(player, creature))
            continue;
        if (!PlayerHasLineOfSight(player, creature))
            continue;

        float const dist = player->GetExactDist(creature);
        if (dist >= bestDist)
            continue;

        Optional<UseItemOnUnitTarget> target = MakeUseItemOnUnitTarget(player, creature, matched->QuestId, matched->CreditEntry, itemId);
        if (!target)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {}. Skipping that creature.",
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

Optional<PlayerbotClient::UseItemOnUnitTarget> PlayerbotClient::FindLogIncompleteUseItemOnUnitTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    Map* map = player->GetMap();
    uint32 const mapId = map->GetId();

    std::vector<IncompleteMonsterCredit> credits;
    CollectIncompleteMonsterCredits(player, credits);
    if (credits.empty())
        return {};

    UseItemOnUnitTarget bestCreatureTarget;
    float bestCreatureDist = std::numeric_limits<float>::max();
    bool haveCreature = false;
    Position bestMarker;
    IncompleteMonsterCredit const* bestMarkerCredit = nullptr;
    uint32 bestMarkerItemId = 0;
    float bestMarkerDist = std::numeric_limits<float>::max();
    int bestMarkerRank = 2;
    bool haveMarker = false;

    for (IncompleteMonsterCredit const& credit : credits)
    {
        uint32 const itemId = HeldQuestStartItem(player, uint32(credit.QuestId));
        if (!itemId)
            continue;
        if (!KeepThisCredit(credit.QuestId, credit.CreditEntry, filter))
            continue;

        std::vector<ObjectivePoiBlob> blobs;
        CollectObjectivePoiBlobs(credit.QuestId, mapId, credit.ObjectiveId, int32(credit.CreditEntry), credit.StorageIndex, blobs);
        if (blobs.empty())
            continue;

        LoadPoiGrids(map, blobs);

        bool sawSpawnForCredit = false;
        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
            if (PointIsSkipped(*creature, filter))
                continue;
            if (!creature->IsAlive())
                continue;
            if (!player->InSamePhase(creature))
                continue;
            if (creature->IsPrivateObject() && !creature->CheckPrivateObjectOwnerVisibility(player))
                continue;
            if (!CreatureGivesMonsterCredit(creature, credit.CreditEntry))
                continue;
            if (!PositionIsInPoiArea(*creature, blobs))
                continue;

            sawSpawnForCredit = true;
            if (!ItemSpellCanTargetCreature(player, itemId, creature))
                continue;

            float const dist = player->GetExactDist(creature);
            if (dist >= bestCreatureDist)
                continue;

            Optional<UseItemOnUnitTarget> target = MakeUseItemOnUnitTarget(player, creature, credit.QuestId, credit.CreditEntry, itemId);
            if (!target)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside {}. Skipping that creature.",
                    player->GetName(), creature->GetGUID().ToString());
                continue;
            }

            bestCreatureDist = dist;
            bestCreatureTarget = *target;
            haveCreature = true;
        }

        // Markers are for a missing spawn. If matching creatures are up and the item would not take any of them, look for other work instead of camping the yellow.
        if (!SkipAllMarkersForCredit(credit.QuestId, credit.CreditEntry, filter) && !sawSpawnForCredit)
        {
            for (ObjectivePoiBlob const& blob : blobs)
            {
                int const rank = MarkerBlobRank(blob, filter);
                for (Position const& point : blob.Points)
                {
                    if (PointIsSkipped(point, filter))
                        continue;

                    float const dist = player->GetExactDist(point);
                    if (!BetterMarker(rank, dist, bestMarkerRank, bestMarkerDist, haveMarker))
                        continue;

                    bestMarkerRank = rank;
                    bestMarkerDist = dist;
                    bestMarker = point;
                    bestMarkerCredit = &credit;
                    bestMarkerItemId = itemId;
                    haveMarker = true;
                }
            }
        }
    }

    if (haveCreature)
        return bestCreatureTarget;

    if (!haveMarker || !bestMarkerCredit)
        return {};

    UseItemOnUnitTarget target;
    target.Pos = bestMarker;
    target.StopDistance = 0.25f;
    target.QuestId = bestMarkerCredit->QuestId;
    target.CreditEntry = bestMarkerCredit->CreditEntry;
    target.ItemId = bestMarkerItemId;
    return target;
}

bool PlayerbotClient::UseItemOnUnitTargetStillNeeded(Player* player, UseItemOnUnitTarget const& target)
{
    if (!player || target.QuestId <= 0 || !target.CreditEntry || !target.ItemId)
        return false;
    if (player->GetQuestStatus(uint32(target.QuestId)) != QUEST_STATUS_INCOMPLETE)
        return false;
    if (!player->HasItemCount(target.ItemId))
        return false;

    if (!target.CreatureGuid.IsEmpty())
    {
        Creature* creature = ObjectAccessor::GetCreature(*player, target.CreatureGuid);
        if (creature && !ItemSpellCanTargetCreature(player, target.ItemId, creature))
            return false;
    }

    Quest const* quest = sObjectMgr->GetQuestTemplate(uint32(target.QuestId));
    if (!quest || quest->GetSrcItemId() != target.ItemId)
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

bool PlayerbotClient::CombatSpellIsMelee(SpellInfo const* spellInfo)
{
    if (!spellInfo)
        return false;

    if (spellInfo->IsNextMeleeSwingSpell())
        return true;

    if (spellInfo->HasAttribute(SPELL_ATTR0_CU_CHARGE))
        return false;

    if (spellInfo->IsRangedWeaponSpell() || spellInfo->IsAutoRepeatRangedSpell())
        return false;

    return spellInfo->RangeEntry && (spellInfo->RangeEntry->Flags & SPELL_RANGE_MELEE);
}

float PlayerbotClient::CombatSpellMaxRange(Player const* player, Unit const* target, SpellInfo const* spellInfo)
{
    if (!player || !target || !spellInfo)
        return 0.0f;

    if (CombatSpellIsMelee(spellInfo))
        return player->GetMeleeRange(target);

    return spellInfo->GetMaxRange(false, player) + player->GetCombatReach() + target->GetCombatReach();
}

bool PlayerbotClient::CombatCastHasStarted(Player const* player, uint32 spellId)
{
    if (!player || !player->GetSpellHistory() || !player->GetMap() || !spellId)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, player->GetMap()->GetDifficultyID());
    if (!spellInfo)
        return false;

    if (player->GetSpellHistory()->GetRemainingGlobalCooldown(spellInfo) > 0ms)
        return true;

    return CombatSpellAlreadyQueued(player, spellInfo);
}

PlayerbotClient::CombatSpellPick PlayerbotClient::PickCombatDamageSpell(Player* player, Unit* target)
{
    CombatSpellPick pick;
    if (!player || !target || !player->GetMap())
        return pick;

    bool const casting = player->IsNonMeleeSpellCast(false, false, true);
    float approachRange = 0.0f;

    for (auto const& [spellId, playerSpell] : player->GetSpellMap())
    {
        if (playerSpell.state == PLAYERSPELL_REMOVED || !playerSpell.active || playerSpell.disabled)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, player->GetMap()->GetDifficultyID());
        if (!CombatDamageSpellIsEligible(player, spellInfo))
            continue;

        bool const melee = CombatSpellIsMelee(spellInfo);
        bool const inRange = melee ? player->IsWithinMeleeRange(target)
            : player->GetExactDist(target) <= CombatSpellMaxRange(player, target, spellInfo);

        if (casting || !CombatSpellIsReady(player, spellInfo) || CombatSpellAlreadyQueued(player, spellInfo))
        {
            if (inRange)
                pick.KnownInRange = true;
            continue;
        }

        if (melee && !inRange)
        {
            float const range = CombatSpellMaxRange(player, target, spellInfo);
            if (!pick.Approach || range > approachRange)
            {
                pick.Approach = spellInfo;
                approachRange = range;
            }
            pick.WalkCloser = true;
            continue;
        }

        SpellCastResult const result = CheckCombatSpellCast(player, target, spellInfo);
        if (result == SPELL_CAST_OK)
        {
            pick.Press = spellInfo;
            pick.KnownInRange = true;
            break;
        }

        if (inRange)
            pick.KnownInRange = true;

        if (result == SPELL_FAILED_UNIT_NOT_INFRONT && !pick.Face)
            pick.Face = spellInfo;
        else if (result == SPELL_FAILED_OUT_OF_RANGE)
        {
            float const range = CombatSpellMaxRange(player, target, spellInfo);
            if (!pick.Approach || range > approachRange)
            {
                pick.Approach = spellInfo;
                approachRange = range;
            }
        }
        else if (result == SPELL_FAILED_LINE_OF_SIGHT)
            pick.WalkCloser = true;
    }

    return pick;
}

bool PlayerbotClient::TryCombatCast(Player* player, ObjectGuid creatureGuid, uint32 spellId)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || !player->GetMap() || creatureGuid.IsEmpty() || !spellId)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, player->GetMap()->GetDifficultyID());
    if (!spellInfo)
        return false;

    if (player->GetTarget() != creatureGuid)
        QueueSetSelection(player->GetSession(), creatureGuid);

    QueueCastSpell(player, creatureGuid, spellId);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_CAST_SPELL {} ({}) on {}.",
        player->GetName(), CombatSpellName(spellInfo), spellId, creatureGuid.ToString());
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

PlayerbotClient::UseItemLook PlayerbotClient::LookUseItemOnUnit(Player* player, UseItemOnUnitTarget const& target)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || !player->GetMap()
        || target.CreatureGuid.IsEmpty() || !target.ItemId)
        return UseItemLook::Cannot;

    Creature* creature = ObjectAccessor::GetCreature(*player, target.CreatureGuid);
    if (!creature || !creature->IsAlive())
        return UseItemLook::Cannot;
    if (!CreatureIsInInteractRange(player, creature))
        return UseItemLook::Cannot;
    if (!ItemSpellCanTargetCreature(player, target.ItemId, creature))
        return UseItemLook::Cannot;

    Item* item = player->GetItemByEntry(target.ItemId);
    if (!item || player->CanUseItem(item) != EQUIP_ERR_OK)
        return UseItemLook::Cannot;

    uint32 const spellId = GetItemOnUseSpellId(item);
    SpellInfo const* spellInfo = spellId ? sSpellMgr->GetSpellInfo(spellId, player->GetMap()->GetDifficultyID()) : nullptr;
    if (!spellInfo)
        return UseItemLook::Cannot;

    if (player->IsNonMeleeSpellCast(false, false, true) || !CombatSpellIsReady(player, spellInfo)
        || CombatSpellAlreadyQueued(player, spellInfo) || !player->CanRequestSpellCast(spellInfo, player))
        return UseItemLook::Wait;

    SpellCastResult const result = CheckUseItemCast(player, item, creature, spellInfo);
    if (result == SPELL_CAST_OK)
        return UseItemLook::Press;
    if (result == SPELL_FAILED_UNIT_NOT_INFRONT)
        return UseItemLook::Face;
    if (result == SPELL_FAILED_OUT_OF_RANGE)
        return UseItemLook::Closer;
    if (result == SPELL_FAILED_SPELL_IN_PROGRESS || result == SPELL_FAILED_NOT_READY || result == SPELL_FAILED_MOVING)
        return UseItemLook::Wait;

    return UseItemLook::Cannot;
}

uint32 PlayerbotClient::TryUseItemOnUnit(Player* player, UseItemOnUnitTarget const& target)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || !player->GetMap()
        || target.CreatureGuid.IsEmpty() || !target.ItemId)
        return 0;

    Creature* creature = ObjectAccessor::GetCreature(*player, target.CreatureGuid);
    if (!creature || !creature->IsAlive())
        return 0;
    if (!CreatureIsInInteractRange(player, creature))
        return 0;
    if (!ItemSpellCanTargetCreature(player, target.ItemId, creature))
        return 0;

    Item* item = player->GetItemByEntry(target.ItemId);
    if (!item)
        return 0;
    if (player->CanUseItem(item) != EQUIP_ERR_OK)
        return 0;

    uint32 const spellId = GetItemOnUseSpellId(item);
    if (!spellId)
        return 0;

    if (player->GetTarget() != target.CreatureGuid)
        QueueSetSelection(player->GetSession(), target.CreatureGuid);

    QueueUseItem(player, item, target.CreatureGuid, spellId);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_USE_ITEM with {} on {} for quest {}.",
        player->GetName(), item->GetGUID().ToString(), target.CreatureGuid.ToString(), target.QuestId);
    return spellId;
}

bool PlayerbotClient::TryOpenLoot(Player* player, ObjectGuid creatureGuid)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || creatureGuid.IsEmpty())
        return false;

    Creature* creature = ObjectAccessor::GetCreature(*player, creatureGuid);
    if (!creature || creature->IsAlive())
        return false;
    if (!player->isAllowedToLoot(creature))
        return false;
    if (!player->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f))
        return false;

    QueueLootUnit(player->GetSession(), creatureGuid);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_LOOT_UNIT on {}.",
        player->GetName(), creatureGuid.ToString());
    return true;
}

bool PlayerbotClient::TryTakeQuestItemFromOpenLoot(Player* player, ObjectGuid lootOwner, uint32 itemId)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || lootOwner.IsEmpty() || !itemId)
        return false;

    for (std::pair<ObjectGuid const, Loot*> const& view : player->GetAELootView())
    {
        Loot* loot = view.second;
        if (!loot || loot->GetOwnerGUID() != lootOwner)
            continue;

        for (LootItem const& item : loot->items)
        {
            if (item.is_looted || item.itemid != itemId)
                continue;

            QueueLootItem(player->GetSession(), view.first, uint8(item.LootListId));
            QueueLootRelease(player->GetSession(), lootOwner);
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_LOOT_ITEM and CMSG_LOOT_RELEASE on {} for item {}.",
                player->GetName(), lootOwner.ToString(), itemId);
            return true;
        }
    }

    return false;
}

bool PlayerbotClient::TryTakeAllFromOpenLoot(Player* player, ObjectGuid lootOwner)
{
    if (!player || !player->GetSession() || lootOwner.IsEmpty())
        return false;
    if (!HasOpenLootOn(player, lootOwner))
        return false;

    bool anyGold = false;
    std::vector<std::pair<ObjectGuid, uint8>> take;
    std::unordered_set<ObjectGuid> owners;

    for (std::pair<ObjectGuid const, Loot*> const& view : player->GetAELootView())
    {
        Loot* loot = view.second;
        if (!loot)
            continue;

        owners.insert(loot->GetOwnerGUID());
        if (loot->gold > 0)
            anyGold = true;

        for (LootItem const& item : loot->items)
        {
            if (!LootSlotIsTakeable(item.GetUiTypeForPlayer(player, *loot)))
                continue;
            if (item.needs_quest)
                take.insert(take.begin(), { view.first, uint8(item.LootListId) });
            else
                take.push_back({ view.first, uint8(item.LootListId) });
        }
    }

    if (anyGold)
        QueueLootMoney(player->GetSession());

    if (!take.empty())
    {
        WorldPacket packet(CMSG_LOOT_ITEM);
        packet << uint32(take.size());
        for (std::pair<ObjectGuid, uint8> const& slot : take)
        {
            packet << slot.first;
            packet << slot.second;
        }
        packet.WriteBit(false);
        packet.FlushBits();
        player->GetSession()->QueuePacket(std::move(packet));
    }

    for (ObjectGuid const& owner : owners)
        QueueLootRelease(player->GetSession(), owner);

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued loot-all on {} ({} item slot(s){}).",
        player->GetName(), lootOwner.ToString(), uint32(take.size()), anyGold ? ", money" : "");
    return true;
}

bool PlayerbotClient::HasOpenLootOn(Player* player, ObjectGuid lootOwner)
{
    if (!player || lootOwner.IsEmpty())
        return false;

    for (std::pair<ObjectGuid const, Loot*> const& view : player->GetAELootView())
    {
        if (view.second && view.second->GetOwnerGUID() == lootOwner)
            return true;
    }

    return false;
}

bool PlayerbotClient::PlayerHasResurrectionSickness(Player const* player)
{
    if (!player)
        return false;

    ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(player->GetRace());
    if (!raceEntry || !raceEntry->ResSicknessSpellID)
        return false;

    return player->HasAura(uint32(raceEntry->ResSicknessSpellID));
}

bool PlayerbotClient::CorpseReclaimDelayFinished(Player const* player)
{
    if (!player)
        return false;

    Corpse const* corpse = player->GetCorpse();
    if (!corpse)
        return false;

    time_t const readyAt = time_t(corpse->GetGhostTime() + player->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP));
    return readyAt <= time_t(GameTime::GetGameTime());
}

bool PlayerbotClient::IsWithinCorpseReclaimRange(Player const* player)
{
    if (!player)
        return false;

    Corpse const* corpse = player->GetCorpse();
    if (corpse)
        return corpse->IsWithinDistInMap(player, CORPSE_RECLAIM_RADIUS, true);

    if (!player->HasCorpse())
        return false;

    WorldLocation const& loc = player->GetCorpseLocation();
    if (loc.GetMapId() != player->GetMapId())
        return false;

    return player->GetExactDist(loc) <= float(CORPSE_RECLAIM_RADIUS);
}

bool PlayerbotClient::HostilesWouldAggroAt(Player* player, Position const& at)
{
    if (!player || !player->GetMap())
        return false;

    Map* map = player->GetMap();
    if (!map->IsGridLoaded(at))
        map->LoadGrid(at.GetPositionX(), at.GetPositionY());

    for (auto const& pair : map->GetCreatureBySpawnIdStore())
    {
        Creature* creature = pair.second;
        if (!CreatureWouldPullIfAlive(player, creature))
            continue;

        float const pullRange = creature->GetAttackDistance(player) + creature->GetCombatReach();
        if (creature->GetExactDist(at) <= pullRange)
            return true;
    }

    return false;
}

Optional<Position> PlayerbotClient::PickCorpseStandPosition(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetMap() || !player->HasCorpse())
        return {};

    WorldLocation const& loc = player->GetCorpseLocation();
    if (loc.GetMapId() != player->GetMapId())
        return {};

    Map* map = player->GetMap();
    if (!map->IsGridLoaded(loc))
        map->LoadGrid(loc.GetPositionX(), loc.GetPositionY());

    Corpse* corpse = player->GetCorpse();
    bool const hot = HostilesWouldAggroAt(player, loc);
    float const sideDistances[] = { 32.0f, 35.0f, 28.0f, 24.0f };

    if (corpse)
    {
        if (hot)
        {
            for (float standDistance : sideDistances)
            {
                if (standDistance >= float(CORPSE_RECLAIM_RADIUS))
                    continue;

                Position standPos;
                if (!PlayerbotWalker::PickApproachPosition(player, corpse, standDistance, standPos))
                    continue;
                if (corpse->GetExactDist(standPos) > float(CORPSE_RECLAIM_RADIUS))
                    continue;
                if (HostilesWouldAggroAt(player, standPos))
                    continue;
                return standPos;
            }

            Position standPos;
            if (PlayerbotWalker::PickApproachPosition(player, corpse, sideDistances[0], standPos)
                && corpse->GetExactDist(standPos) <= float(CORPSE_RECLAIM_RADIUS))
                return standPos;
        }
        else
        {
            Position standPos;
            if (PlayerbotWalker::PickApproachPosition(player, corpse, 2.0f, standPos))
                return standPos;
        }
    }

    Position fallback;
    fallback.Relocate(loc);
    return fallback;
}

Optional<PlayerbotClient::SpiritHealerTarget> PlayerbotClient::FindSpiritHealer(Player* player, Position const& nearPos)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    Map* map = player->GetMap();
    if (!map->IsGridLoaded(nearPos))
        map->LoadGrid(nearPos.GetPositionX(), nearPos.GetPositionY());

    Creature* best = nullptr;
    float bestDist = std::numeric_limits<float>::max();

    for (auto const& pair : map->GetCreatureBySpawnIdStore())
    {
        Creature* creature = pair.second;
        if (!SpiritHealerIsUsable(player, creature))
            continue;

        float const dist = nearPos.GetExactDist(*creature);
        if (dist >= bestDist)
            continue;

        bestDist = dist;
        best = creature;
    }

    if (!best)
        return {};

    Optional<SpiritHealerTarget> target = MakeSpiritHealerTarget(player, best);
    if (!target)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside spirit healer {} without standing in a spell focus.",
            player->GetName(), best->GetGUID().ToString());
        return {};
    }

    return target;
}

bool PlayerbotClient::TrySpiritHealer(Player* player, ObjectGuid healerGuid)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || healerGuid.IsEmpty())
        return false;

    if (!player->GetNPCIfCanInteractWith(healerGuid, UNIT_NPC_FLAG_SPIRIT_HEALER, UNIT_NPC_FLAG_2_NONE))
        return false;

    QueueSetSelection(player->GetSession(), healerGuid);
    QueueSpiritHealerActivate(player->GetSession(), healerGuid);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_SET_SELECTION and CMSG_SPIRIT_HEALER_ACTIVATE on {}.",
        player->GetName(), healerGuid.ToString());
    return true;
}

namespace
{
    constexpr uint32 VENDOR_DURABILITY_PCT = 20;
    constexpr uint32 VENDOR_BAG_FREE_SLOTS = 1;
    constexpr uint32 VENDOR_LOAD_ATTEMPTS = 16;

    struct VendorSpawn
    {
        ObjectGuid::LowType SpawnId = 0;
        uint32 MapId = 0;
        uint32 Faction = 0;
        Position Pos;
        bool CanRepair = false;
        bool NoSell = false;
    };

    std::vector<VendorSpawn> g_vendorSpawns;
    bool g_vendorSpawnsReady = false;

    void EnsureVendorSpawns()
    {
        if (g_vendorSpawnsReady)
            return;

        g_vendorSpawnsReady = true;
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(data.id);
            if (!info)
                continue;

            uint64 const npcFlags = data.npcflag.value_or(info->npcflag);
            if (!(npcFlags & uint64(UNIT_NPC_FLAG_VENDOR)))
                continue;

            VendorSpawn spawn;
            spawn.SpawnId = spawnId;
            spawn.MapId = data.mapId;
            spawn.Faction = info->faction;
            spawn.Pos = data.spawnPoint;
            spawn.CanRepair = (npcFlags & uint64(UNIT_NPC_FLAG_REPAIR)) != 0;
            spawn.NoSell = (info->flags_extra & CREATURE_FLAG_EXTRA_NO_SELL_VENDOR) != 0;
            g_vendorSpawns.push_back(spawn);
        }
    }

    bool VendorIsUsable(Player const* player, Creature const* creature, bool needSell)
    {
        if (!player || !creature || !creature->IsAlive())
            return false;
        if (!creature->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
            return false;
        if (needSell)
        {
            CreatureTemplate const* info = creature->GetCreatureTemplate();
            if (info && (info->flags_extra & CREATURE_FLAG_EXTRA_NO_SELL_VENDOR))
                return false;
        }
        if (!player->InSamePhase(creature))
            return false;
        if (creature->IsPrivateObject() && !creature->CheckPrivateObjectOwnerVisibility(player))
            return false;
        if (creature->GetReactionTo(player) <= REP_UNFRIENDLY)
            return false;
        if (creature->IsInCombat() && !creature->IsInteractionAllowedInCombat())
            return false;
        return true;
    }

    Optional<PlayerbotClient::VendorTarget> MakeVendorTarget(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return {};

        Position standPos;
        float const standDistance = creature->GetCombatReach() + 1.0f;
        if (!PlayerbotWalker::PickApproachPosition(player, creature, standDistance, standPos))
            return {};

        PlayerbotClient::VendorTarget target;
        target.NpcGuid = creature->GetGUID();
        target.Pos = standPos;
        target.StopDistance = 0.25f;
        target.CanRepair = creature->HasNpcFlag(UNIT_NPC_FLAG_REPAIR);
        return target;
    }

    Creature* LoadVendorCreature(Player* player, VendorSpawn const& spawn, std::unordered_set<ObjectGuid> const& skip, bool needSell)
    {
        if (!player || !player->GetMap())
            return nullptr;

        Map* map = player->GetMap();
        if (!map->IsGridLoaded(spawn.Pos))
            map->LoadGrid(spawn.Pos.GetPositionX(), spawn.Pos.GetPositionY());

        for (auto const& pair : Trinity::Containers::MapEqualRange(map->GetCreatureBySpawnIdStore(), spawn.SpawnId))
        {
            Creature* creature = pair.second;
            if (!creature || skip.contains(creature->GetGUID()))
                continue;
            if (!VendorIsUsable(player, creature, needSell))
                continue;
            return creature;
        }

        return nullptr;
    }

    bool VendorFactionOk(Player const* player, uint32 faction)
    {
        FactionTemplateEntry const* fac = sFactionTemplateStore.LookupEntry(faction);
        return WorldObject::GetFactionReactionTo(fac, player) > REP_UNFRIENDLY;
    }
}

bool PlayerbotClient::HasSellableJunk(Player const* player)
{
    if (!player)
        return false;

    bool found = false;
    player->ForEachItem(ItemSearchLocation::Inventory, [player, &found](Item* item)
    {
        if (item->GetQuality() != ITEM_QUALITY_POOR)
            return ItemSearchCallbackResult::Continue;
        if (item->IsRefundable())
            return ItemSearchCallbackResult::Continue;
        if (item->GetTemplate() && item->GetTemplate()->GetClass() == ITEM_CLASS_QUEST)
            return ItemSearchCallbackResult::Continue;
        if (!player->CanSellItemToVendor(item, item->GetCount()))
            found = true;
        return found ? ItemSearchCallbackResult::Stop : ItemSearchCallbackResult::Continue;
    });
    return found;
}

bool PlayerbotClient::BagsNeedVendor(Player const* player)
{
    if (!player)
        return false;
    if (player->GetFreeInventorySlotCount(ItemSearchLocation::Inventory) > VENDOR_BAG_FREE_SLOTS)
        return false;
    return HasSellableJunk(player);
}

bool PlayerbotClient::EquippedGearNeedsRepair(Player const* player)
{
    if (!player)
        return false;

    bool need = false;
    player->ForEachItem(ItemSearchLocation::Equipment, [&need](Item* item)
    {
        uint32 const maxDurability = *item->m_itemData->MaxDurability;
        if (!maxDurability)
            return ItemSearchCallbackResult::Continue;

        uint32 const durability = *item->m_itemData->Durability;
        if (!durability || durability * 100 < maxDurability * VENDOR_DURABILITY_PCT)
        {
            need = true;
            return ItemSearchCallbackResult::Stop;
        }
        return ItemSearchCallbackResult::Continue;
    });
    return need;
}

bool PlayerbotClient::NeedsVendor(Player const* player)
{
    return BagsNeedVendor(player) || EquippedGearNeedsRepair(player);
}

Optional<PlayerbotClient::VendorTarget> PlayerbotClient::FindNearestVendor(Player* player, std::unordered_set<ObjectGuid> const& skip, bool preferRepair)
{
    if (!player || !player->IsInWorld() || !player->GetMap())
        return {};

    EnsureVendorSpawns();

    Map* map = player->GetMap();
    bool const needSell = BagsNeedVendor(player);
    std::vector<VendorSpawn const*> candidates;
    candidates.reserve(64);

    for (VendorSpawn const& spawn : g_vendorSpawns)
    {
        if (spawn.MapId != map->GetId())
            continue;
        if (needSell && spawn.NoSell)
            continue;
        if (!VendorFactionOk(player, spawn.Faction))
            continue;
        candidates.push_back(&spawn);
    }

    std::sort(candidates.begin(), candidates.end(), [player](VendorSpawn const* a, VendorSpawn const* b)
    {
        return player->GetExactDist(a->Pos) < player->GetExactDist(b->Pos);
    });

    auto tryLoad = [&](Optional<bool> mustRepair) -> Optional<VendorTarget>
    {
        uint32 attempts = 0;
        for (VendorSpawn const* spawn : candidates)
        {
            if (mustRepair && spawn->CanRepair != *mustRepair)
                continue;
            if (attempts >= VENDOR_LOAD_ATTEMPTS)
                break;
            ++attempts;

            Creature* creature = LoadVendorCreature(player, *spawn, skip, needSell);
            if (!creature)
                continue;

            Optional<VendorTarget> target = MakeVendorTarget(player, creature);
            if (!target)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot stand beside vendor {} without standing in a spell focus. Skipping that npc.",
                    player->GetName(), creature->GetGUID().ToString());
                continue;
            }
            return target;
        }
        return {};
    };

    if (preferRepair)
    {
        if (Optional<VendorTarget> repair = tryLoad(true))
            return repair;
        return tryLoad(false);
    }

    return tryLoad(Optional<bool>{});
}

bool PlayerbotClient::TryOpenVendor(Player* player, ObjectGuid vendorGuid)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || vendorGuid.IsEmpty())
        return false;

    if (!player->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR, UNIT_NPC_FLAG_2_NONE))
        return false;

    QueueSetSelection(player->GetSession(), vendorGuid);
    QueueListInventory(player->GetSession(), vendorGuid);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_SET_SELECTION and CMSG_LIST_INVENTORY on {}.",
        player->GetName(), vendorGuid.ToString());
    return true;
}

bool PlayerbotClient::TryVendorTrade(Player* player, ObjectGuid vendorGuid, bool repair)
{
    if (!player || !player->IsInWorld() || !player->GetSession() || vendorGuid.IsEmpty())
        return false;

    if (!player->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR, UNIT_NPC_FLAG_2_NONE))
        return false;

    QueueSellAllJunkItems(player->GetSession(), vendorGuid);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_SELL_ALL_JUNK_ITEMS at {}.",
        player->GetName(), vendorGuid.ToString());

    if (repair && player->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_REPAIR, UNIT_NPC_FLAG_2_NONE))
    {
        QueueRepairItem(player->GetSession(), vendorGuid);
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} queued CMSG_REPAIR_ITEM (repair all) at {}.",
            player->GetName(), vendorGuid.ToString());
    }

    return true;
}

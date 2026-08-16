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

#ifndef EVRY_MOD_PLAYERBOT_CLIENT_H
#define EVRY_MOD_PLAYERBOT_CLIENT_H

#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include "UnitDefines.h"
#include <unordered_set>

class Creature;
class Item;
class Player;
class WorldSession;
struct MovementInfo;
enum OpcodeClient : uint32;

namespace PlayerbotClient
{
    enum class QuestSearchKind
    {
        TurnIn,
        Accept,
        Talk
    };

    struct QuestTarget
    {
        ObjectGuid NpcGuid;
        Position Pos;
        float StopDistance = 0.25f;
        int32 QuestId = 0;
        bool TurnIn = false;
    };

    struct CombatTarget
    {
        ObjectGuid CreatureGuid;
        Position Pos;
        float StopDistance = 0.25f;
        int32 QuestId = 0;
        uint32 CreditEntry = 0;
        uint32 ItemId = 0;
    };

    struct GameObjectTarget
    {
        ObjectGuid GoGuid;
        Position Pos;
        float StopDistance = 0.25f;
        int32 QuestId = 0;
        uint32 GoEntry = 0;
    };

    struct UseItemOnUnitTarget
    {
        ObjectGuid CreatureGuid;
        Position Pos;
        float StopDistance = 0.25f;
        int32 QuestId = 0;
        uint32 CreditEntry = 0;
        uint32 ItemId = 0;
    };

    struct ItemLootTarget
    {
        ObjectGuid CreatureGuid;
        ObjectGuid GoGuid;
        Position Pos;
        float StopDistance = 0.25f;
        int32 QuestId = 0;
        uint32 ItemId = 0;
        uint32 CreatureEntry = 0;
        uint32 GoEntry = 0;
        bool LootCorpse = false;
    };

    struct SpiritHealerTarget
    {
        ObjectGuid NpcGuid;
        Position Pos;
        float StopDistance = 0.25f;
    };

    void QueueEnumCharacters(WorldSession* session);
    void QueuePlayerLogin(WorldSession* session, ObjectGuid characterGuid);
    void QueueCompleteCinematic(WorldSession* session);
    void QueueTimeSyncResponse(WorldSession* session, uint32 sequenceIndex, uint32 clientTime);
    void QueueMoveInitActiveMoverComplete(WorldSession* session, uint32 ticks);
    void QueueMovement(WorldSession* session, OpcodeClient opcode, MovementInfo const& movementInfo);
    void QueueQuestGiverAcceptQuest(WorldSession* session, ObjectGuid questGiverGuid, int32 questId);
    void QueueQuestGiverCompleteQuest(WorldSession* session, ObjectGuid questGiverGuid, int32 questId);
    void QueueQuestGiverChooseReward(WorldSession* session, ObjectGuid questGiverGuid, int32 questId);
    void QueueSetSelection(WorldSession* session, ObjectGuid guid);
    void QueueAttackSwing(WorldSession* session, ObjectGuid victim);
    void QueueAttackStop(WorldSession* session);
    void QueueGameObjUse(WorldSession* session, ObjectGuid guid);
    void QueueUseItem(Player* player, Item* item, ObjectGuid unitTarget, uint32 spellId);
    void QueueLootUnit(WorldSession* session, ObjectGuid creatureGuid);
    void QueueLootItem(WorldSession* session, ObjectGuid lootObj, uint8 lootListId);
    void QueueLootMoney(WorldSession* session);
    void QueueLootRelease(WorldSession* session, ObjectGuid unitGuid);
    void QueueRepopRequest(WorldSession* session);
    void QueueReclaimCorpse(WorldSession* session, ObjectGuid corpseGuid);
    void QueueSpiritHealerActivate(WorldSession* session, ObjectGuid healerGuid);
    void QueueStandStateChange(WorldSession* session, UnitStandStateType standState);

    Optional<QuestTarget> FindNearbyQuestTarget(Player* player, float range, QuestSearchKind kind);
    Optional<QuestTarget> FindLogCompleteTurnIn(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0);
    Optional<CombatTarget> FindAttackerTarget(Player* player);
    Optional<CombatTarget> FindNearbyMonsterObjectiveTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip);
    Optional<CombatTarget> FindLogIncompleteMonsterTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0, uint32 skipEntry = 0);
    bool CombatTargetStillNeeded(Player* player, CombatTarget const& target);
    Optional<ItemLootTarget> MakeCorpseLootTarget(Player* player, Creature* creature);
    Optional<ItemLootTarget> FindNearbyItemLootTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange = false);
    Optional<ItemLootTarget> FindLogIncompleteItemTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0, uint32 skipEntry = 0);
    bool ItemLootTargetStillNeeded(Player* player, ItemLootTarget const& target);
    CombatTarget CombatTargetFromItemLoot(ItemLootTarget const& target);
    bool TryOpenLoot(Player* player, ObjectGuid creatureGuid);
    bool TryTakeQuestItemFromOpenLoot(Player* player, ObjectGuid lootOwner, uint32 itemId);
    bool TryTakeAllFromOpenLoot(Player* player, ObjectGuid lootOwner);
    bool HasOpenLootOn(Player* player, ObjectGuid lootOwner);
    bool PlayerHasResurrectionSickness(Player const* player);
    bool CorpseReclaimDelayFinished(Player const* player);
    bool IsWithinCorpseReclaimRange(Player const* player);
    bool HostilesWouldAggroAt(Player* player, Position const& at);
    Optional<Position> PickCorpseStandPosition(Player* player);
    Optional<SpiritHealerTarget> FindSpiritHealer(Player* player, Position const& nearPos);
    bool TrySpiritHealer(Player* player, ObjectGuid healerGuid);
    Optional<GameObjectTarget> FindNearbyGameObjectObjectiveTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange = false);
    Optional<GameObjectTarget> FindLogIncompleteGameObjectTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0, uint32 skipEntry = 0);
    bool GameObjectTargetStillNeeded(Player* player, GameObjectTarget const& target);
    Optional<UseItemOnUnitTarget> FindNearbyUseItemOnUnitTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange = false);
    Optional<UseItemOnUnitTarget> FindLogIncompleteUseItemOnUnitTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0, uint32 skipEntry = 0);
    bool UseItemOnUnitTargetStillNeeded(Player* player, UseItemOnUnitTarget const& target);
    bool TryInteractQuest(Player* player, QuestTarget const& target);
    bool TryMeleeAttack(Player* player, ObjectGuid creatureGuid);
    bool TryUseGameObject(Player* player, GameObjectTarget const& target);
    bool TryUseItemOnUnit(Player* player, UseItemOnUnitTarget const& target);
}

#endif

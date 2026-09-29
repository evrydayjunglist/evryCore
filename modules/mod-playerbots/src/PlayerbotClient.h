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
#include <string>
#include <unordered_set>
#include <vector>

class Creature;
class Item;
class Map;
class Player;
class SpellInfo;
class Unit;
class WorldObject;
class WorldSession;
struct MovementInfo;
enum OpcodeClient : uint32;
enum class LootItemType : uint8;

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

    struct VendorTarget
    {
        ObjectGuid NpcGuid;
        Position Pos;
        float StopDistance = 0.25f;
        bool CanRepair = false;
    };

    struct CombatSpellPick
    {
        SpellInfo const* Press = nullptr;
        SpellInfo const* Face = nullptr;
        SpellInfo const* Approach = nullptr;
        bool KnownInRange = false;
        bool WalkCloser = false;
    };

    enum class UseItemLook
    {
        Cannot,
        Wait,
        Face,
        Closer,
        Press
    };

    // A map marker point she stood on with nothing there. She walks to it again once what belongs there has had time
    // to respawn.
    struct EmptyMarker
    {
        Position Pos;
        uint32 RetryAtMs = 0;
    };

    // KeepQuest: only this quest and entry. Otherwise QuestId/Entry skip that objective's map markers.
    struct MapYellowFilter
    {
        int32 QuestId = 0;
        uint32 Entry = 0;
        bool KeepQuest = false;
        Position const* SkipPos = nullptr;
        std::vector<Position> const* SkipPositions = nullptr;
        std::vector<EmptyMarker> const* EmptyMarkers = nullptr;
    };

    void QueueEnumCharacters(WorldSession* session);
    void QueuePlayerLogin(WorldSession* session, ObjectGuid characterGuid);
    void QueueCompleteCinematic(WorldSession* session);
    void QueueTimeSyncResponse(WorldSession* session, uint32 sequenceIndex, uint32 clientTime);
    void QueueMoveInitActiveMoverComplete(WorldSession* session, uint32 ticks);
    void QueueMoveTeleportAck(WorldSession* session, ObjectGuid mover, uint32 ackIndex);
    void QueueSuspendTokenResponse(WorldSession* session, uint32 sequenceIndex);
    void QueueWorldPortResponse(WorldSession* session);
    // Accept or Decline on the party invite window.
    void QueuePartyInviteResponse(WorldSession* session, bool accept);
    // Her client's movement status at pos. It keeps only the modes the server granted her from its record of her movement;
    // the caller adds what she is doing.
    void FillClientMovementInfo(Player const* player, Position const& pos, MovementInfo& out);
    // False, and nothing queued, while a teleport waits for its reply: a packet built from her old position would put her
    // back there once the reply is handled.
    bool QueueMovement(WorldSession* session, OpcodeClient opcode, MovementInfo const& movementInfo);
    void QueueMovementAck(WorldSession* session, OpcodeClient opcode, MovementInfo const& status, uint32 ackIndex);
    void QueueMoveKnockBackAck(WorldSession* session, MovementInfo const& status, uint32 ackIndex);
    void SendMovementUpdate(WorldSession* session, MovementInfo const& movementInfo);
    void QueueQuestGiverAcceptQuest(WorldSession* session, ObjectGuid questGiverGuid, int32 questId);
    void QueueQuestGiverCompleteQuest(WorldSession* session, ObjectGuid questGiverGuid, int32 questId);
    void QueueQuestGiverChooseReward(WorldSession* session, ObjectGuid questGiverGuid, int32 questId, LootItemType rewardType, uint32 rewardId);
    void QueueAutoEquipItemSlot(WorldSession* session, Item const* item, uint8 equipSlot);
    void QueueSetSelection(WorldSession* session, ObjectGuid guid);
    void QueueAttackSwing(WorldSession* session, ObjectGuid victim);
    void QueueAttackStop(WorldSession* session);
    void QueueGameObjUse(WorldSession* session, ObjectGuid guid);
    void QueueUseItem(Player* player, Item* item, ObjectGuid unitTarget, uint32 spellId);
    void QueueCastSpell(Player* player, ObjectGuid unitTarget, uint32 spellId);
    void QueueSetFacing(Player* player, WorldObject const* lookAt);
    void QueueLootUnit(WorldSession* session, ObjectGuid creatureGuid);
    void QueueLootItem(WorldSession* session, ObjectGuid lootObj, uint8 lootListId);
    void QueueLootMoney(WorldSession* session);
    void QueueLootRelease(WorldSession* session, ObjectGuid unitGuid);
    void QueueRepopRequest(WorldSession* session);
    void QueueReclaimCorpse(WorldSession* session, ObjectGuid corpseGuid);
    void QueueSpiritHealerActivate(WorldSession* session, ObjectGuid healerGuid);
    void QueueStandStateChange(WorldSession* session, UnitStandStateType standState);
    void QueueListInventory(WorldSession* session, ObjectGuid vendorGuid);
    void QueueSellAllJunkItems(WorldSession* session, ObjectGuid vendorGuid);
    void QueueSellItem(WorldSession* session, ObjectGuid vendorGuid, ObjectGuid itemGuid, uint32 amount);
    void QueueRepairItem(WorldSession* session, ObjectGuid vendorGuid);

    Optional<QuestTarget> FindNearbyQuestTarget(Player* player, float range, QuestSearchKind kind, std::unordered_set<ObjectGuid> const& skip);
    // A turn-in whose ? marker is in a grid that is not loaded comes back as a walk to that marker, with no quest giver.
    Optional<QuestTarget> FindLogCompleteTurnIn(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0,
        std::vector<Position> const* skipPositions = nullptr);
    // One sentence for each finished quest in her log whose turn-in FindLogCompleteTurnIn would not give her, naming the
    // step that dropped it and the facts that step used.
    // The turn-in of one finished quest, found the way FindLogCompleteTurnIn finds each of them.
    Optional<QuestTarget> FindTurnInFor(Player* player, uint32 questId, std::unordered_set<ObjectGuid> const& skip,
        std::vector<Position> const* skipPositions = nullptr);
    std::vector<std::string> ExplainUnpickedTurnIns(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0);
    Optional<QuestTarget> FindTakeableQuestInZone(Player* player, std::unordered_set<ObjectGuid> const& skip, int32 skipQuestId = 0);
    Optional<CombatTarget> FindAttackerTarget(Player* player);
    Optional<CombatTarget> FindNearbyMonsterObjectiveTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip);
    Optional<CombatTarget> FindLogIncompleteMonsterTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter = {});
    bool CombatTargetStillNeeded(Player* player, CombatTarget const& target);
    Optional<ItemLootTarget> MakeCorpseLootTarget(Player* player, Creature* creature);
    Optional<ItemLootTarget> FindNearbyItemLootTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange = false);
    Optional<ItemLootTarget> FindLogIncompleteItemTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter = {});
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
    bool HasSellableJunk(Player const* player);
    bool BagsNeedVendor(Player const* player);
    bool EquippedGearNeedsRepair(Player const* player);
    bool NeedsVendor(Player const* player);
    Optional<uint32> CreatureRespawnWaitMs(Map const* map, uint32 creditEntry, Position const& near);
    // mustBuy asks only for a vendor who buys items, as when she goes to make room in her bags.
    Optional<VendorTarget> FindNearestVendor(Player* player, std::unordered_set<ObjectGuid> const& skip, bool preferRepair, bool mustBuy = false);
    bool TryOpenVendor(Player* player, ObjectGuid vendorGuid);
    bool TryVendorTrade(Player* player, ObjectGuid vendorGuid, bool repair);
    // How many more free bag slots she needs before the server would hand her this quest's rewards (the fixed ones and
    // the choice she would click). 0 when they fit, or when the server would refuse the turn-in for another reason.
    uint32 BagSlotsShortForTurnIn(Player const* player, uint32 questId);
    // At an open shop, sells up to slots items to make room: the ones worth least at a vendor, never quest items, food
    // or drink, bags, gear better than what she wears, or the reward she is waiting to put on. One CMSG_SELL_ITEM each,
    // as a player drags them onto the vendor. Returns how many were queued.
    uint32 QueueSellForRoom(Player* player, ObjectGuid vendorGuid, uint32 slots, uint32 keepItemId);
    Optional<GameObjectTarget> FindNearbyGameObjectObjectiveTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange = false);
    Optional<GameObjectTarget> FindLogIncompleteGameObjectTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter = {});
    bool GameObjectTargetStillNeeded(Player* player, GameObjectTarget const& target);
    Optional<UseItemOnUnitTarget> FindNearbyUseItemOnUnitTarget(Player* player, float range, std::unordered_set<ObjectGuid> const& skip, bool mustBeInUseRange = false);
    Optional<UseItemOnUnitTarget> FindLogIncompleteUseItemOnUnitTarget(Player* player, std::unordered_set<ObjectGuid> const& skip, MapYellowFilter const& filter = {});
    bool UseItemOnUnitTargetStillNeeded(Player* player, UseItemOnUnitTarget const& target);
    UseItemLook LookUseItemOnUnit(Player* player, UseItemOnUnitTarget const& target);
    // On a turn-in, wearItemId gets the reward she chose when it is gear she should put on once it reaches her bags.
    bool TryInteractQuest(Player* player, QuestTarget const& target, uint32* wearItemId = nullptr);

    enum class WearLook
    {
        NotYet,  // not in her bags yet
        Queued,  // equip packet queued
        Dropped  // no longer an upgrade, or the server would refuse it
    };
    WearLook TryWearUpgrade(Player* player, uint32 itemId);
    bool TryMeleeAttack(Player* player, ObjectGuid creatureGuid);
    bool TryCombatCast(Player* player, ObjectGuid creatureGuid, uint32 spellId);
    CombatSpellPick PickCombatDamageSpell(Player* player, Unit* target);
    // A heal, shield, or damage cut she knows and could cast on herself now. Long cooldowns only when allowLongCooldown.
    SpellInfo const* PickSelfDefenceSpell(Player* player, bool allowLongCooldown);
    bool TrySelfCast(Player* player, uint32 spellId);
    // Whether a food or drink she used is working on her now.
    void RestAurasOnHer(Player const* player, bool& eating, bool& drinking);
    // Food (for health) or drink (for mana) in her bags that she could use now; skips the item entries in refused.
    Item* PickRestItem(Player* player, bool wantFood, bool wantDrink, std::unordered_set<uint32> const& refused);
    // Queues the CMSG_USE_ITEM a player sends by clicking that food or drink. Returns the item's on-use spell, or 0.
    uint32 TryUseRestItem(Player* player, Item* item);
    // Queues the CMSG_USE_ITEM a player sends by clicking her Hearthstone. Returns true when it went out; otherwise
    // whyNot says why (none in her bags, cooldown seconds left, or she cannot use it now).
    bool TryUseHearthstone(Player* player, std::string& whyNot);
    bool CombatCastHasStarted(Player const* player, uint32 spellId);
    bool CombatSpellIsMelee(SpellInfo const* spellInfo);
    float CombatSpellMaxRange(Player const* player, Unit const* target, SpellInfo const* spellInfo);
    bool TryUseGameObject(Player* player, GameObjectTarget const& target);
    uint32 TryUseItemOnUnit(Player* player, UseItemOnUnitTarget const& target);
}

#endif

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

#ifndef EVRY_MOD_PLAYERBOT_MGR_H
#define EVRY_MOD_PLAYERBOT_MGR_H

#include "PlayerbotClient.h"
#include "PlayerbotBridge.h"
#include "PlayerbotMovement.h"
#include "Playerbots.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <unordered_set>
#include <vector>

class Player;

enum class PlayerbotDeathWork
{
    None,
    WaitToRelease,
    WaitForGhost,
    WalkToCorpse,
    WaitToReclaim,
    WalkToHealer,
    WaitToHeal,
    SitRecover
};

struct PlayerbotRecord
{
    PlayerbotAccount Account;
    bool SessionQueued = false;
    bool EnumQueued = false;
    bool LoginQueued = false;
    bool ContinueLoginCalled = false;
    bool CinematicSkipped = false;
    bool InitMoverQueued = false;
    bool QuestInteractQueued = false;
    uint32 QuestArriveWaitMs = 0;
    uint32 QuestInteractWaitMs = 0;
    uint32 QuestSearchEmptyMs = 0;
    bool CombatSwingSent = false;
    uint32 CombatCastSpellId = 0;
    bool CombatCastPending = false;
    uint32 CombatCastWaitMs = 0;
    bool CombatFacingWait = false;
    bool UseItemCastPending = false;
    bool UseItemCastSeenGcd = false;
    bool UseItemFacingWait = false;
    uint32 UseItemCastSpellId = 0;
    uint32 UseItemCastWaitMs = 0;
    bool LootOpenSent = false;
    uint32 LootOpenWaitMs = 0;
    PlayerbotDeathWork Death = PlayerbotDeathWork::None;
    uint32 DeathWaitMs = 0;
    uint32 GhostMs = 0;
    uint32 CampedMs = 0;
    bool HadSickness = false;
    bool GhostSettled = false;
    bool RepopSent = false;
    bool ReclaimSent = false;
    bool HealerSent = false;
    bool SitSent = false;
    Position SpiritReleasePos;
    ObjectGuid SpiritHealerGuid;
    PlayerbotClient::QuestTarget QuestTarget;
    PlayerbotClient::CombatTarget CombatTarget;
    PlayerbotClient::GameObjectTarget GameObjectTarget;
    PlayerbotClient::UseItemOnUnitTarget UseItemOnUnitTarget;
    PlayerbotClient::ItemLootTarget ItemLootTarget;
    PlayerbotClient::VendorTarget VendorTarget;
    bool VendorListSent = false;
    bool VendorActed = false;
    uint32 VendorRetryMs = 0;
    std::unordered_set<ObjectGuid> UnreachableGuids;
    std::vector<Position> UnreachablePositions;
    bool LookedForOtherYellowOnFace = false;
    PlayerbotWalker Walker;
};

class PlayerbotMgr
{
public:
    static PlayerbotMgr* instance();

    PlayerbotMgr() = default;
    ~PlayerbotMgr();

    void Start();
    void Stop();
    void Update(uint32 diff);
    bool IsBotAccount(uint32 accountId) const;
    void OnBotLogin(Player* player);

private:
    void UpdateBridge();
    std::string HandleBridgeRequest(uint64 connectionId, std::string const& payload);
    bool TryLogin(PlayerbotRecord& bot);
    void UpdateLogin(PlayerbotRecord& bot);
    void UpdateWorld(PlayerbotRecord& bot, uint32 diff);
    void ReplyTimeSync(WorldSession* session);
    void ReplyTeleportAcks(Player* player);
    bool UpdateDeath(PlayerbotRecord& bot, Player* player, uint32 diff);
    void BeginDeath(PlayerbotRecord& bot, Player* player);
    void ClearDeath(PlayerbotRecord& bot);
    void ClearLivingWork(PlayerbotRecord& bot, Player* player);
    bool BeginCorpseWalk(PlayerbotRecord& bot, Player* player);
    bool BeginHealerWalk(PlayerbotRecord& bot, Player* player);
    bool UpdateSitRecover(PlayerbotRecord& bot, Player* player, uint32 diff);
    void RecoverFailedWalk(PlayerbotRecord& bot, Player* player);
    bool TryImmediateWorld(PlayerbotRecord& bot, Player* player, bool walking);
    bool TryClickFromHere(PlayerbotRecord& bot, Player* player);
    bool TryMapYellow(PlayerbotRecord& bot, Player* player, int32 skipQuestId = 0, uint32 skipEntry = 0);
    bool TrySameObjectiveYellow(PlayerbotRecord& bot, Player* player, int32 questId, uint32 entry, Position const& skipPos, ObjectGuid extraSkipGuid);
    bool TryLeaveFaceForOtherYellow(PlayerbotRecord& bot, Player* player);
    void ClearCombat(PlayerbotRecord& bot, Player* player);
    bool UpdateCombat(PlayerbotRecord& bot, Player* player, uint32 diff);
    void ClearItemLoot(PlayerbotRecord& bot);
    bool UpdateItemLoot(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool BeginQuestTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::QuestTarget const& target);
    bool BeginGameObjectTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::GameObjectTarget const& target);
    bool BeginUseItemOnUnitTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::UseItemOnUnitTarget const& target);
    bool UpdateUseItem(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool BeginCombatTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::CombatTarget const& target);
    bool BeginItemLootTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::ItemLootTarget const& target);
    bool BeginItemWork(PlayerbotRecord& bot, Player* player, PlayerbotClient::ItemLootTarget const& target);
    void ClearVendor(PlayerbotRecord& bot);
    bool BeginVendorTarget(PlayerbotRecord& bot, Player* player, PlayerbotClient::VendorTarget const& target);
    bool UpdateVendor(PlayerbotRecord& bot, Player* player, uint32 diff);
    bool TryBeginVendor(PlayerbotRecord& bot, Player* player);

    std::vector<PlayerbotRecord> _bots;
    std::unordered_set<uint32> _accountIds;
    std::unordered_set<uint64> _bridgeHandshakes;
    PlayerbotBridge _bridge;
    PlayerbotLoginMode _loginMode = PlayerbotLoginMode::Automatic;
    bool _bridgeStarted = false;
};

#define sPlayerbotMgr PlayerbotMgr::instance()

#endif

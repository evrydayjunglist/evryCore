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

#ifndef TRINITYCORE_BATTLE_PAY_MGR_H
#define TRINITYCORE_BATTLE_PAY_MGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
class WorldSession;

namespace BattlePay
{
    enum DistributionStatus : uint32
    {
        DIST_STATUS_NONE             = 0,
        DIST_STATUS_AVAILABLE        = 1,
        DIST_STATUS_ADD_TO_PROCESS   = 2,
        DIST_STATUS_PROCESS_COMPLETE = 3,
        DIST_STATUS_FINISHED         = 4
    };

    enum PurchaseUpdateStatus : uint32
    {
        PURCHASE_STATUS_FINISH = 3,
        PURCHASE_STATUS_READY  = 6
    };

    struct PendingDistribution
    {
        uint64 DistributionID = 0;
        uint64 PurchaseID = 0;
        uint32 ProductID = 0;
        uint32 Status = DIST_STATUS_AVAILABLE;
        ObjectGuid TargetCharacter;
        uint32 SpecId = 0;
    };

    struct PendingPurchase
    {
        uint64 PurchaseID = 0;
        uint64 DistributionID = 0;
        uint32 ProductID = 0;
        uint32 ServerToken = 0;
        uint32 ClientToken = 0;
        uint64 CurrentPriceFixedPoint = 0;
        bool AwaitingConfirm = false;
    };

    struct RecordedPurchase
    {
        uint64 PurchaseID = 0;
        uint32 ProductID = 0;
        uint32 Status = PURCHASE_STATUS_FINISH;
        uint32 ResultCode = 0;
    };
}

class TC_GAME_API BattlePayMgr
{
public:
    explicit BattlePayMgr(WorldSession* session);

    static bool IsEnabled();
    static uint32 GetL80ProductId();
    static bool IsFreeDeliverableProduct(uint32 productId);

    void SendProductList();
    void SendPurchaseList();
    void SendDistributionList();
    void SendAvailableL80Distributions();

    void HandleStartPurchase(uint32 clientToken, uint32 productId, ObjectGuid targetCharacter);
    void HandleConfirmPurchaseResponse(bool confirm, uint32 serverToken, uint64 clientCurrentPriceFixedPoint);
    bool HandleDistributionAssignToTarget(uint32 clientToken, uint64 distributionId, ObjectGuid targetCharacter, uint32 productChoice);

    void OverlayEnumExperienceLevel(ObjectGuid character, uint8& experienceLevel) const;
    bool ApplyPendingBoostOnLogin(Player* player);
    void OnCharacterDeleted(ObjectGuid character);

private:
    uint32 ResolveFreeBuyProductId(uint32 shopProductId) const;
    bool PrepareAvailableL80Distribution(BattlePay::PendingDistribution& out);
    void LoadFromDatabase();
    void PersistAvailableDistribution(BattlePay::PendingDistribution const& distribution);
    void PersistAssignedDistribution(BattlePay::PendingDistribution const& distribution);
    void PersistAppliedDistribution(uint64 distributionId);
    uint64 GenerateDistributionId();
    uint64 GeneratePurchaseId();
    uint32 GenerateServerToken();
    uint32 CountAvailableL80() const;
    void SendPurchaseUpdate(uint64 purchaseId, uint32 productId, uint32 status, uint32 resultCode);
    void SendConfirmPurchase(BattlePay::PendingPurchase const& purchase);
    void SendStartPurchaseResult(uint32 clientToken, uint64 purchaseId, uint32 purchaseResult);
    bool BeginFreePurchaseConfirm(uint32 productId, uint32 clientToken);
    void SendDistributionUpdate(BattlePay::PendingDistribution const& distribution);
    void ResurfaceRemainingAvailableL80Distributions();
    bool CanAssignToCharacter(ObjectGuid targetCharacter, uint32 productChoice) const;
    bool IsCharacterBoostedOrPending(ObjectGuid::LowType characterGuid) const;
    bool RelocateToBoostStart(Player* player) const;

    WorldSession* _session;
    std::unordered_map<uint64, BattlePay::PendingDistribution> _distributions;
    std::unordered_map<ObjectGuid::LowType, BattlePay::PendingDistribution> _pendingApply;
    std::unordered_set<ObjectGuid::LowType> _boostedCharacters;
    std::vector<BattlePay::RecordedPurchase> _purchases;
    BattlePay::PendingPurchase _pendingPurchase;
    uint32 _distributionCounter = 1;
    uint32 _purchaseCounter = 1;
    uint32 _serverTokenCounter = 1;
};

#endif

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

#include "BattlePayMgr.h"
#include "BattlePayBoost.h"
#include "BattlePayPackets.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Field.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringConvert.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

namespace
{
constexpr uint32 BATTLE_PAY_L80_PRODUCT_ID = 1161;
constexpr uint32 BATTLE_PAY_L80_SHOP_PRODUCT_INFO_ID = 1410;
constexpr uint32 BATTLE_PAY_L80_STAGING_MAP = 2552;
constexpr uint32 BATTLE_PAY_L80_STAGING_ZONE = 14771;
constexpr float BATTLE_PAY_L80_STAGING_X = 2633.37f;
constexpr float BATTLE_PAY_L80_STAGING_Y = -2591.66f;
constexpr float BATTLE_PAY_L80_STAGING_Z = 219.659f;
constexpr float BATTLE_PAY_L80_STAGING_O = 0.0f;
constexpr uint8 BATTLE_PAY_L80_LEVEL = 80;
constexpr int32 BATTLE_PAY_L80_BOOST_TYPE = 11;
constexpr int32 BATTLE_PAY_L80_LOADOUT_PURPOSE = 23;
constexpr ItemContext BATTLE_PAY_L80_ITEM_CONTEXT = ItemContext::Character_Boost_Shadowlands_50; // 62
constexpr uint64 BATTLE_PAY_L80_GOLD = 100000;
constexpr uint32 BATTLE_PAY_MAX_AVAILABLE_L80 = 16;

void FillL80DistributionObject(WorldPackets::BattlePay::BattlePayDistributionObject& object, BattlePay::PendingDistribution const& distribution, ObjectGuid accountGuid)
{
    object.DistributionID = distribution.DistributionID;
    object.Status = distribution.Status;
    object.ProductID = distribution.ProductID;
    object.AccountGUID = accountGuid;
    object.PurchaseID = distribution.PurchaseID;
    object.TargetPlayer = distribution.TargetCharacter;
    if (!object.TargetPlayer.IsEmpty())
    {
        object.TargetVirtualRealm = GetVirtualRealmAddress();
        object.TargetNativeRealm = GetVirtualRealmAddress();
    }

    WorldPackets::BattlePay::BattlePayProduct product;
    product.ProductID = distribution.ProductID;
    product.Type = 1;
    product.UnkInt4 = uint32(BATTLE_PAY_L80_BOOST_TYPE);
    object.Product = product;
}

WorldPackets::BattlePay::ProductDisplayInfo MakeL80DisplayInfo()
{
    WorldPackets::BattlePay::ProductDisplayInfo display;
    display.Name1 = "Enhanced Level 80 Boost";
    display.Name2 = "Instantly grant one character a level boost.";
    return display;
}

uint32 GetPreferredL80LoadoutId(uint32 specId)
{
    static std::unordered_map<uint32, uint32> const preferred = {
        { 251, 2180 }, { 250, 2188 }, { 252, 2196 },
        { 577, 2181 }, { 581, 2181 },
        { 105, 2176 }, { 102, 2183 }, { 104, 2190 }, { 103, 2203 },
        { 1467, 2178 }, { 1468, 2178 }, { 1473, 2178 },
        { 254, 2177 }, { 253, 2182 }, { 255, 2195 },
        { 62, 2185 }, { 63, 2185 }, { 64, 2185 },
        { 269, 2171 }, { 270, 2194 }, { 268, 2208 },
        { 70, 2174 }, { 66, 2179 }, { 65, 2204 },
        { 258, 2186 }, { 257, 2192 }, { 256, 2192 },
        { 260, 2172 }, { 259, 2184 }, { 261, 2184 },
        { 262, 2175 }, { 264, 2191 }, { 263, 2193 },
        { 265, 2197 }, { 266, 2197 }, { 267, 2197 },
        { 72, 2173 }, { 73, 2199 }, { 71, 2202 },
    };

    if (auto const itr = preferred.find(specId); itr != preferred.end())
        return itr->second;
    return 0;
}

bool IsL80BoostLoadout(CharacterLoadoutEntry const* loadout, uint8 classId, uint8 raceId)
{
    return loadout
        && loadout->Purpose == BATTLE_PAY_L80_LOADOUT_PURPOSE
        && ItemContext(loadout->ItemContext) == BATTLE_PAY_L80_ITEM_CONTEXT
        && uint8(loadout->ChrClassID) == classId
        && (loadout->RaceMask.IsEmpty() || loadout->RaceMask.HasRace(raceId));
}

CharacterLoadoutEntry const* SelectL80BoostLoadout(uint8 classId, uint8 raceId, uint32 specId)
{
    if (uint32 const preferredId = GetPreferredL80LoadoutId(specId))
        if (CharacterLoadoutEntry const* preferred = sCharacterLoadoutStore.LookupEntry(preferredId))
            if (IsL80BoostLoadout(preferred, classId, raceId))
                return preferred;

    for (CharacterLoadoutEntry const* loadout : sCharacterLoadoutStore)
        if (IsL80BoostLoadout(loadout, classId, raceId))
            return loadout;

    return nullptr;
}

void MailOfflineBoostItems(ObjectGuid character, std::span<BattlePay::BoostInventoryItem const> inventory,
    std::unordered_set<uint64> const& recovered, CharacterDatabaseTransaction trans)
{
    std::vector<uint64> items;
    for (auto const& item : inventory)
        if (recovered.contains(item.Guid))
            items.push_back(item.Guid);

    for (size_t offset = 0; offset < items.size();)
    {
        uint64 mailId = sObjectMgr->GenerateMailID();
        time_t now = GameTime::GetGameTime();
        auto* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_MAIL);
        stmt->setUInt64(0, mailId);
        stmt->setUInt8(1, MAIL_NORMAL);
        stmt->setInt8(2, MAIL_STATIONERY_DEFAULT);
        stmt->setUInt16(3, 0);
        stmt->setUInt64(4, 0);
        stmt->setUInt64(5, character.GetCounter());
        stmt->setString(6, "Level Boost - recovered items"sv);
        stmt->setString(7, "Items removed from your character while applying a Level 80 Boost."sv);
        stmt->setBool(8, true);
        stmt->setInt64(9, now + 30 * DAY);
        stmt->setInt64(10, now);
        stmt->setUInt64(11, 0);
        stmt->setUInt64(12, 0);
        stmt->setUInt8(13, MAIL_CHECK_MASK_COPIED);
        trans->Append(stmt);

        size_t end = std::min(offset + size_t(MAX_MAIL_ITEMS), items.size());
        for (; offset < end; ++offset)
        {
            Item::DeleteFromInventoryDB(trans, items[offset]);
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_MAIL_ITEM);
            stmt->setUInt64(0, mailId);
            stmt->setUInt64(1, items[offset]);
            stmt->setUInt64(2, character.GetCounter());
            trans->Append(stmt);
        }
    }
}

void WriteBoostEquipmentCache(ObjectGuid character, std::array<Item const*, EQUIPMENT_SLOT_END> const& equipped,
    CharacterDatabaseTransaction trans)
{
    auto* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_SELECT_EQUIPMENT_CACHE_CUSTOMIZATIONS);
    stmt->setUInt64(0, character.GetCounter());
    trans->Append(stmt);
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_SELECT_EQUIPMENT_CACHE_CUSTOMIZATIONS);
    stmt->setUInt64(0, character.GetCounter());
    for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        WorldPackets::Character::EnumCharactersResult::CharacterInfoBasic::VisualItemInfo visual;
        if (Item const* item = equipped[slot])
        {
            visual.ItemID = item->GetEntry();
            visual.TransmogrifiedItemID = item->GetEntry();
            visual.Subclass = item->GetTemplate()->GetSubClass();
            visual.InvType = item->GetTemplate()->GetInventoryType();
            if (ItemModifiedAppearanceEntry const* modified = item->GetItemModifiedAppearance())
                if (ItemAppearanceEntry const* appearance = sItemAppearanceStore.LookupEntry(modified->ItemAppearanceID))
                    visual.DisplayID = appearance->ItemDisplayInfoID;
        }
        uint8 base = 1 + slot * 8;
        stmt->setUInt32(base, visual.ItemID);
        stmt->setUInt32(base + 1, visual.TransmogrifiedItemID);
        stmt->setUInt8(base + 2, visual.Subclass);
        stmt->setUInt8(base + 3, visual.InvType);
        stmt->setUInt32(base + 4, visual.DisplayID);
        stmt->setUInt32(base + 5, 0);
        stmt->setInt32(base + 6, 0);
        stmt->setUInt8(base + 7, 0);
    }
    trans->Append(stmt);
}
}

BattlePayMgr::BattlePayMgr(WorldSession* session) : _session(session)
{
    LoadFromDatabase();
}

bool BattlePayMgr::IsEnabled()
{
    return sWorld->getBoolConfig(CONFIG_BATTLE_PAY_ENABLED);
}

uint32 BattlePayMgr::GetL80ProductId()
{
    return BATTLE_PAY_L80_PRODUCT_ID;
}

bool BattlePayMgr::IsFreeDeliverableProduct(uint32 productId)
{
    std::string const list = sConfigMgr->GetStringDefault("BattlePay.FreeProductIDs"sv, "1161"sv);
    if (list.empty())
        return false;

    for (std::string_view const token : Trinity::Tokenize(list, ',', false))
        if (Optional<uint32> const id = Trinity::StringTo<uint32>(token); id && *id == productId)
            return true;

    return false;
}

void BattlePayMgr::LoadFromDatabase()
{
    _distributions.clear();
    _pendingApply.clear();
    _boostedCharacters.clear();
    _purchases.clear();

    CharacterDatabasePreparedStatement* maxStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BATTLEPAY_DIST_MAX);
    maxStmt->setUInt32(0, _session->GetAccountId());
    if (PreparedQueryResult maxResult = CharacterDatabase.Query(maxStmt))
    {
        uint32 const maxLow = maxResult->Fetch()[0].GetUInt32();
        if (maxLow >= _distributionCounter)
            _distributionCounter = maxLow + 1;
    }

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BATTLEPAY_DISTRIBUTIONS);
    stmt->setUInt32(0, _session->GetAccountId());
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
        return;

    std::vector<ObjectGuid::LowType> missingTargets;
    do
    {
        Field* fields = result->Fetch();
        BattlePay::PendingDistribution distribution;
        distribution.DistributionID = fields[0].GetUInt64();
        distribution.PurchaseID = fields[1].GetUInt64();
        distribution.ProductID = fields[2].GetUInt32();
        distribution.Status = fields[3].GetUInt32();
        uint8 consumed = fields[4].GetUInt8();
        uint8 const applied = fields[5].GetUInt8();
        uint64 target = fields[6].GetUInt64();
        distribution.SpecId = fields[7].GetUInt32();

        // No character row has this guid any more, so a new character can get it after a restart.
        // Character deletion releases the boost; rows left over from before that are released here.
        if (consumed && target && fields[8].IsNull())
        {
            TC_LOG_INFO("network", "BattlePay: distribution {} targets deleted character {}; releasing it (account {})",
                distribution.DistributionID, target, _session->GetAccountId());
            missingTargets.push_back(target);
            if (!applied)
            {
                consumed = 0;
                distribution.SpecId = 0;
            }
            target = 0;
        }

        if (target)
            distribution.TargetCharacter = ObjectGuid::Create<HighGuid::Player>(target);

        uint32 const low = uint32(distribution.DistributionID & 0xFFFFFFFFu);
        if (low >= _distributionCounter)
            _distributionCounter = low + 1;

        uint32 const purchaseLow = uint32(distribution.PurchaseID & 0x3FFFFFFFu);
        if (purchaseLow >= _purchaseCounter)
            _purchaseCounter = purchaseLow + 1;

        BattlePay::RecordedPurchase purchase;
        purchase.PurchaseID = distribution.PurchaseID;
        purchase.ProductID = distribution.ProductID;
        purchase.Status = BattlePay::PURCHASE_STATUS_FINISH;
        purchase.ResultCode = 0;
        _purchases.push_back(purchase);

        if (!consumed)
        {
            distribution.Status = BattlePay::DIST_STATUS_AVAILABLE;
            _distributions[distribution.DistributionID] = distribution;
        }
        else if (!applied && target)
            _pendingApply[target] = distribution;
        else if (applied && target)
            _boostedCharacters.insert(target);
    } while (result->NextRow());

    if (missingTargets.empty())
        return;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (ObjectGuid::LowType const target : missingTargets)
    {
        CharacterDatabasePreparedStatement* releaseStmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_BATTLEPAY_DISTRIBUTION_RETURN_BY_TARGET);
        releaseStmt->setUInt64(0, target);
        trans->Append(releaseStmt);

        releaseStmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_BATTLEPAY_DISTRIBUTION_CLEAR_APPLIED_TARGET);
        releaseStmt->setUInt64(0, target);
        trans->Append(releaseStmt);
    }
    CharacterDatabase.DirectCommitTransaction(trans);
}

void BattlePayMgr::PersistAvailableDistribution(BattlePay::PendingDistribution const& distribution)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_BATTLEPAY_DISTRIBUTION);
    stmt->setUInt64(0, distribution.DistributionID);
    stmt->setUInt32(1, _session->GetAccountId());
    stmt->setUInt32(2, distribution.ProductID);
    stmt->setUInt64(3, distribution.PurchaseID);
    stmt->setUInt8(4, uint8(distribution.Status));
    stmt->setUInt8(5, 0);
    stmt->setUInt8(6, 0);
    stmt->setUInt64(7, 0);
    stmt->setUInt32(8, 0);
    trans->Append(stmt);
    CharacterDatabase.DirectCommitTransaction(trans);
}

uint32 BattlePayMgr::ResolveFreeBuyProductId(uint32 shopProductId) const
{
    uint32 const deliverableId = GetL80ProductId();
    if (!IsFreeDeliverableProduct(deliverableId))
        return 0;

    if (shopProductId == deliverableId || shopProductId == BATTLE_PAY_L80_SHOP_PRODUCT_INFO_ID)
        return deliverableId;

    return 0;
}

uint32 BattlePayMgr::CountAvailableL80() const
{
    uint32 const productId = GetL80ProductId();
    uint32 count = 0;
    for (auto const& [_, distribution] : _distributions)
        if (distribution.ProductID == productId && distribution.Status == BattlePay::DIST_STATUS_AVAILABLE)
            ++count;
    return count;
}

bool BattlePayMgr::PrepareAvailableL80Distribution(BattlePay::PendingDistribution& out)
{
    uint32 const productId = GetL80ProductId();
    if (!IsFreeDeliverableProduct(productId))
        return false;

    if (CountAvailableL80() >= BATTLE_PAY_MAX_AVAILABLE_L80)
    {
        TC_LOG_INFO("network", "BattlePay: Free Buy skipped — {} AVAILABLE 1161 already (account {})",
            BATTLE_PAY_MAX_AVAILABLE_L80, _session->GetAccountId());
        return false;
    }

    uint64 purchaseId = 0;
    for (auto const& [_, existing] : _distributions)
    {
        if (existing.ProductID == productId && existing.Status == BattlePay::DIST_STATUS_AVAILABLE)
        {
            purchaseId = existing.PurchaseID;
            break;
        }
    }
    if (!purchaseId)
        purchaseId = GeneratePurchaseId();

    out = {};
    out.DistributionID = GenerateDistributionId();
    out.PurchaseID = purchaseId;
    out.ProductID = productId;
    out.Status = BattlePay::DIST_STATUS_AVAILABLE;
    return true;
}

void BattlePayMgr::ResurfaceRemainingAvailableL80Distributions()
{
    uint32 const productId = GetL80ProductId();
    for (auto const& [_, distribution] : _distributions)
        if (distribution.ProductID == productId && distribution.Status == BattlePay::DIST_STATUS_AVAILABLE)
            SendDistributionUpdate(distribution);
}

uint64 BattlePayMgr::GenerateDistributionId()
{
    return (uint64(_session->GetAccountId()) << 32) | uint64(_distributionCounter++);
}

uint64 BattlePayMgr::GeneratePurchaseId()
{
    return (uint64(_session->GetAccountId()) << 32) | (uint64(0x40000000) | uint64(_purchaseCounter++));
}

uint32 BattlePayMgr::GenerateServerToken()
{
    return ++_serverTokenCounter;
}

void BattlePayMgr::SendPurchaseUpdate(uint64 purchaseId, uint32 productId, uint32 status, uint32 resultCode)
{
    WorldPackets::BattlePay::PurchaseUpdate update;
    WorldPackets::BattlePay::BattlePayPurchase& purchase = update.Purchases.emplace_back();
    purchase.PurchaseID = purchaseId;
    purchase.Status = status;
    purchase.ResultCode = resultCode;
    purchase.ProductID = productId;
    _session->SendPacket(update.Write());
}

void BattlePayMgr::SendConfirmPurchase(BattlePay::PendingPurchase const& purchase)
{
    WorldPackets::BattlePay::ConfirmPurchase confirm;
    confirm.PurchaseID = purchase.PurchaseID;
    confirm.CurrentPriceFixedPoint = purchase.CurrentPriceFixedPoint;
    confirm.ServerToken = purchase.ServerToken;
    _session->SendPacket(confirm.Write());
}

void BattlePayMgr::SendStartPurchaseResult(uint32 clientToken, uint64 purchaseId, uint32 purchaseResult)
{
    WorldPackets::BattlePay::StartPurchaseResponse response;
    response.ClientToken = clientToken;
    response.PurchaseID = purchaseId;
    response.PurchaseResult = purchaseResult;
    _session->SendPacket(response.Write());
}

bool BattlePayMgr::BeginFreePurchaseConfirm(uint32 productId, uint32 clientToken)
{
    if (!IsEnabled())
    {
        SendStartPurchaseResult(clientToken, 0, 13);
        return false;
    }

    uint32 const shopProductId = productId;
    uint32 const deliverableId = ResolveFreeBuyProductId(shopProductId);
    if (!deliverableId)
    {
        TC_LOG_INFO("network", "BattlePay: Free StartPurchase denied shopProduct {} (account {})",
            shopProductId, _session->GetAccountId());
        SendStartPurchaseResult(clientToken, 0, 3);
        SendPurchaseUpdate(0, shopProductId, BattlePay::PURCHASE_STATUS_FINISH, 3);
        return false;
    }

    if (shopProductId != deliverableId)
        TC_LOG_INFO("network", "BattlePay: Free Buy shopProduct {} maps to deliverable {} (account {})",
            shopProductId, deliverableId, _session->GetAccountId());

    BattlePay::PendingDistribution staged;
    if (!PrepareAvailableL80Distribution(staged))
    {
        SendDistributionList();
        SendPurchaseUpdate(0, deliverableId, BattlePay::PURCHASE_STATUS_FINISH, 3);
        return false;
    }

    _pendingPurchase = {};
    _pendingPurchase.PurchaseID = staged.PurchaseID;
    _pendingPurchase.DistributionID = staged.DistributionID;
    _pendingPurchase.ProductID = deliverableId;
    _pendingPurchase.ServerToken = GenerateServerToken();
    _pendingPurchase.ClientToken = clientToken;
    _pendingPurchase.CurrentPriceFixedPoint = 0;
    _pendingPurchase.AwaitingConfirm = true;

    SendStartPurchaseResult(clientToken, staged.PurchaseID, 0);
    SendPurchaseUpdate(staged.PurchaseID, deliverableId, BattlePay::PURCHASE_STATUS_READY, 0);
    SendConfirmPurchase(_pendingPurchase);
    return true;
}

void BattlePayMgr::SendProductList()
{
    WorldPackets::BattlePay::ProductListResponse response;
    response.Result = 0;
    response.CurrencyID = 1;

    if (IsEnabled() && IsFreeDeliverableProduct(GetL80ProductId()))
    {
        WorldPackets::BattlePay::ProductDisplayInfo display = MakeL80DisplayInfo();

        WorldPackets::BattlePay::ProductInfoStruct& info = response.ProductInfo.emplace_back();
        info.ProductID = BATTLE_PAY_L80_SHOP_PRODUCT_INFO_ID;
        info.NormalPriceFixedPoint = 0;
        info.CurrentPriceFixedPoint = 0;
        info.ProductIDs.push_back(GetL80ProductId());
        info.DisplayInfo = display;

        WorldPackets::BattlePay::BattlePayProduct& product = response.Products.emplace_back();
        product.ProductID = GetL80ProductId();
        product.Type = 1;
        product.UnkInt4 = uint32(BATTLE_PAY_L80_BOOST_TYPE);
        product.DisplayInfo = display;

        WorldPackets::BattlePay::BattlePayProductGroup& group = response.ProductGroups.emplace_back();
        group.GroupID = 1;
        group.Ordering = 1;
        group.Name = "Services";

        WorldPackets::BattlePay::BattlePayShopEntry& shop = response.Shop.emplace_back();
        shop.EntryID = 1;
        shop.GroupID = 1;
        shop.ProductID = BATTLE_PAY_L80_SHOP_PRODUCT_INFO_ID;
        shop.DisplayInfo = display;
    }

    _session->SendPacket(response.Write());
    SendAvailableL80Distributions();
}

void BattlePayMgr::SendPurchaseList()
{
    WorldPackets::BattlePay::PurchaseListResponse response;
    response.Result = 0;
    for (BattlePay::RecordedPurchase const& recorded : _purchases)
    {
        WorldPackets::BattlePay::BattlePayPurchase& purchase = response.Purchases.emplace_back();
        purchase.PurchaseID = recorded.PurchaseID;
        purchase.Status = recorded.Status;
        purchase.ResultCode = recorded.ResultCode;
        purchase.ProductID = recorded.ProductID;
    }
    _session->SendPacket(response.Write());
    SendDistributionList();
}

void BattlePayMgr::SendDistributionList()
{
    if (!IsEnabled())
        return;

    WorldPackets::BattlePay::DistributionListResponse response;
    response.Result = 0;
    for (auto const& [_, distribution] : _distributions)
        FillL80DistributionObject(response.Distributions.emplace_back(), distribution, _session->GetAccountGUID());
    _session->SendPacket(response.Write());
    TC_LOG_INFO("server.worldserver", "BattlePay: sent DistributionList count {} account {}",
        response.Distributions.size(), _session->GetAccountId());
}

void BattlePayMgr::SendAvailableL80Distributions()
{
    if (!IsEnabled())
        return;

    SendDistributionList();
    for (auto const& [_, distribution] : _distributions)
        if (distribution.Status == BattlePay::DIST_STATUS_AVAILABLE)
            SendDistributionUpdate(distribution);
}

void BattlePayMgr::SendDistributionUpdate(BattlePay::PendingDistribution const& distribution)
{
    WorldPackets::BattlePay::DistributionUpdate update;
    FillL80DistributionObject(update.Distribution, distribution, _session->GetAccountGUID());
    _session->SendPacket(update.Write());
    TC_LOG_INFO("server.worldserver", "BattlePay: sent DistributionUpdate product {} status {} dist {} account {}",
        distribution.ProductID, distribution.Status, distribution.DistributionID, _session->GetAccountId());
}

void BattlePayMgr::HandleStartPurchase(uint32 clientToken, uint32 productId, ObjectGuid /*targetCharacter*/)
{
    BeginFreePurchaseConfirm(productId, clientToken);
}

void BattlePayMgr::HandleConfirmPurchaseResponse(bool confirm, uint32 serverToken, uint64 clientCurrentPriceFixedPoint)
{
    if (!_pendingPurchase.AwaitingConfirm)
        return;

    uint64 const purchaseId = _pendingPurchase.PurchaseID;
    uint32 const productId = _pendingPurchase.ProductID;
    uint64 const distributionId = _pendingPurchase.DistributionID;

    if (!confirm || serverToken != _pendingPurchase.ServerToken
        || clientCurrentPriceFixedPoint != _pendingPurchase.CurrentPriceFixedPoint)
    {
        _pendingPurchase = {};
        SendPurchaseUpdate(purchaseId, productId, BattlePay::PURCHASE_STATUS_FINISH, 3);
        return;
    }

    if (!IsEnabled() || !IsFreeDeliverableProduct(productId))
    {
        _pendingPurchase = {};
        SendPurchaseUpdate(purchaseId, productId, BattlePay::PURCHASE_STATUS_FINISH, 3);
        return;
    }

    _pendingPurchase = {};

    if (CountAvailableL80() >= BATTLE_PAY_MAX_AVAILABLE_L80)
    {
        SendPurchaseUpdate(purchaseId, productId, BattlePay::PURCHASE_STATUS_FINISH, 3);
        return;
    }

    BattlePay::PendingDistribution pending;
    pending.DistributionID = distributionId;
    pending.PurchaseID = purchaseId;
    pending.ProductID = productId;
    pending.Status = BattlePay::DIST_STATUS_AVAILABLE;
    _distributions[distributionId] = pending;
    PersistAvailableDistribution(pending);

    BattlePay::RecordedPurchase recorded;
    recorded.PurchaseID = purchaseId;
    recorded.ProductID = productId;
    recorded.Status = BattlePay::PURCHASE_STATUS_FINISH;
    _purchases.push_back(recorded);

    SendPurchaseUpdate(pending.PurchaseID, productId, BattlePay::PURCHASE_STATUS_FINISH, 0);
    SendDistributionUpdate(pending);
    SendDistributionList();

    TC_LOG_INFO("network", "BattlePay: Free purchase complete product {} purchaseId {} dist {} account {}",
        productId, pending.PurchaseID, pending.DistributionID, _session->GetAccountId());
}

bool BattlePayMgr::IsCharacterBoostedOrPending(ObjectGuid::LowType characterGuid) const
{
    return _boostedCharacters.contains(characterGuid) || _pendingApply.contains(characterGuid);
}

bool BattlePayMgr::CanAssignToCharacter(ObjectGuid targetCharacter, uint32 productChoice, uint8& classId, uint8& raceId, uint8& backpackSlots) const
{
    if (!targetCharacter.IsPlayer())
        return false;

    if (ObjectAccessor::FindConnectedPlayer(targetCharacter))
    {
        TC_LOG_INFO("network", "BattlePay: refusing assign while {} is online", targetCharacter.ToString());
        return false;
    }

    if (_session->PlayerLoading() && _session->GetPlayerLoadingGuid() == targetCharacter)
    {
        TC_LOG_INFO("network", "BattlePay: refusing assign while {} is mid-login", targetCharacter.ToString());
        return false;
    }

    if (WorldSession* accountSession = sWorld->FindSession(_session->GetAccountId());
        accountSession && accountSession != _session && accountSession->PlayerLoading()
        && accountSession->GetPlayerLoadingGuid() == targetCharacter)
    {
        TC_LOG_INFO("network", "BattlePay: refusing assign while {} is mid-login on another session",
            targetCharacter.ToString());
        return false;
    }

    CharacterDatabasePreparedStatement* onlineStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ONLINE_BY_GUID);
    onlineStmt->setUInt64(0, targetCharacter.GetCounter());
    onlineStmt->setUInt32(1, _session->GetAccountId());
    PreparedQueryResult onlineResult = CharacterDatabase.Query(onlineStmt);
    if (!onlineResult)
        return false;
    if (onlineResult->Fetch()[0].GetUInt8() != 0)
    {
        TC_LOG_INFO("network", "BattlePay: refusing assign while {} is marked online", targetCharacter.ToString());
        return false;
    }

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BATTLEPAY_CHARACTER);
    stmt->setUInt64(0, targetCharacter.GetCounter());
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
        return false;

    Field* fields = result->Fetch();
    if (fields[0].GetUInt32() != _session->GetAccountId())
        return false;

    CharacterCacheEntry const* character = sCharacterCache->GetCharacterCacheByGuid(targetCharacter);
    if (!character || character->TimerunningSeasonId)
        return false;

    classId = fields[1].GetUInt8();
    uint8 const level = fields[2].GetUInt8();
    raceId = fields[3].GetUInt8();
    backpackSlots = fields[4].GetUInt8();
    if (level >= BATTLE_PAY_L80_LEVEL)
    {
        TC_LOG_INFO("network", "BattlePay: character {} already at or above L80; refusing boost", targetCharacter.ToString());
        return false;
    }

    ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(productChoice);
    if (!spec || spec->ClassID != classId)
    {
        TC_LOG_INFO("network", "BattlePay: invalid ProductChoice {} for class {} on {}", productChoice, classId, targetCharacter.ToString());
        return false;
    }

    if (!SelectL80BoostLoadout(classId, raceId, productChoice))
    {
        TC_LOG_ERROR("network", "BattlePay: no L80 loadout for class {} race {} spec {} on {}",
            classId, raceId, productChoice, targetCharacter.ToString());
        return false;
    }

    return true;
}

bool BattlePayMgr::HandleDistributionAssignToTarget(uint32 clientToken, uint64 distributionId, ObjectGuid targetCharacter, uint32 productChoice)
{
    WorldPackets::BattlePay::StartDistributionAssignToTargetResponse assignResponse;
    assignResponse.ClientToken = clientToken;
    assignResponse.DistributionID = distributionId;
    assignResponse.Result = 0;

    auto fail = [&](uint32 result)
    {
        assignResponse.Result = result;
        _session->SendPacket(assignResponse.Write());
        return false;
    };

    if (!IsEnabled())
        return fail(13);

    auto itr = _distributions.find(distributionId);
    if (itr == _distributions.end() || itr->second.Status != BattlePay::DIST_STATUS_AVAILABLE)
        return fail(3);

    if (itr->second.ProductID != GetL80ProductId())
        return fail(3);

    if (IsCharacterBoostedOrPending(targetCharacter.GetCounter()))
        return fail(3);

    BattlePay::PendingDistribution distribution = itr->second;
    distribution.TargetCharacter = targetCharacter;
    distribution.SpecId = productChoice;
    if (!ApplyOfflineBoost(distribution))
        return fail(3);

    WorldPackets::Character::CharacterUpgradeStarted upgradeStarted;
    upgradeStarted.CharacterGUID = targetCharacter;
    _session->SendPacket(upgradeStarted.Write());
    _session->SendPacket(assignResponse.Write());

    distribution.Status = BattlePay::DIST_STATUS_ADD_TO_PROCESS;
    SendDistributionUpdate(distribution);

    WorldPackets::Character::CharacterUpgradeComplete upgradeComplete;
    upgradeComplete.CharacterGUID = targetCharacter;
    _session->SendPacket(upgradeComplete.Write());

    WorldPackets::Misc::InvalidatePlayer invalidatePlayer;
    invalidatePlayer.Guid = targetCharacter;
    _session->SendPacket(invalidatePlayer.Write());

    distribution.Status = BattlePay::DIST_STATUS_PROCESS_COMPLETE;
    SendDistributionUpdate(distribution);

    distribution.Status = BattlePay::DIST_STATUS_FINISHED;
    SendDistributionUpdate(distribution);

    _distributions.erase(itr);
    ResurfaceRemainingAvailableL80Distributions();

    TC_LOG_INFO("network", "BattlePay: assigned L80 product {} to {} (spec {}) for account {}",
        GetL80ProductId(), targetCharacter.ToString(), productChoice, _session->GetAccountId());
    return true;
}

bool BattlePayMgr::ApplyOfflineBoost(BattlePay::PendingDistribution const& distribution)
{
    ObjectGuid character = distribution.TargetCharacter;
    uint8 classId = 0, raceId = 0, backpackSlots = 0;
    if (!CanAssignToCharacter(character, distribution.SpecId, classId, raceId, backpackSlots))
        return false;
    if (!sMapStore.LookupEntry(BATTLE_PAY_L80_STAGING_MAP))
        return false;

    // Recheck the durable entitlement before modifying a character, including old deferred grants.
    auto* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BATTLEPAY_DISTRIBUTION);
    stmt->setUInt64(0, distribution.DistributionID);
    stmt->setUInt32(1, _session->GetAccountId());
    PreparedQueryResult entitlement = CharacterDatabase.Query(stmt);
    if (!entitlement)
        return false;
    Field* fields = entitlement->Fetch();
    if (fields[0].GetUInt32() != GetL80ProductId() || fields[2].GetUInt8()
        || (fields[1].GetUInt8() && (fields[3].GetUInt64() != character.GetCounter() || fields[4].GetUInt32() != distribution.SpecId)))
        return false;

    CharacterLoadoutEntry const* loadout = SelectL80BoostLoadout(classId, raceId, distribution.SpecId);
    ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(distribution.SpecId);
    std::vector<BattlePay::BoostKitItem> kit;
    for (CharacterLoadoutItemEntry const* row : sCharacterLoadoutItemStore)
    {
        if (row->CharacterLoadoutID != loadout->ID)
            continue;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(row->ItemID);
        if (!proto || proto->GetArtifactID())
            return false;
        kit.push_back({ row->ItemID, uint8(proto->GetInventoryType()), uint8(proto->GetClass()),
            uint8(proto->GetSubClass()), proto->GetMaxCount() });
    }

    // The character row makes an empty inventory distinguishable from a failed query.
    QueryResult inventoryResult = CharacterDatabase.PQuery(
        "SELECT i.item, i.bag, i.slot, t.itemEntry, t.count, t.owner_guid FROM characters c "
        "LEFT JOIN character_inventory i ON i.guid = c.guid LEFT JOIN item_instance t ON t.guid = i.item "
        "WHERE c.guid = {} AND c.account = {}", character.GetCounter(), _session->GetAccountId());
    if (!inventoryResult)
        return false;
    std::vector<BattlePay::BoostInventoryItem> inventory;
    do
    {
        Field* item = inventoryResult->Fetch();
        if (item[0].IsNull())
            continue;
        if (item[3].IsNull() || !item[4].GetUInt32() || item[5].GetUInt64() != character.GetCounter())
            return false;
        inventory.push_back({ item[0].GetUInt64(), item[1].GetUInt64(), item[2].GetUInt8(), item[3].GetUInt32(), item[4].GetUInt32() });
    } while (inventoryResult->NextRow());

    auto plan = BattlePay::PlanBoostInventory(inventory, kit, backpackSlots,
        spec->GetFlags().HasFlag(ChrSpecializationFlag::DualWieldTwoHanded));
    if (!plan)
    {
        TC_LOG_ERROR("network", "BattlePay: cannot place the complete L80 kit for {}; no items or boost consumed", character.ToString());
        return false;
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    MailOfflineBoostItems(character, inventory, plan->RecoveredItems, trans);
    std::vector<std::unique_ptr<Item>> created;
    std::array<Item const*, EQUIPMENT_SLOT_END> equipped = { };
    for (size_t i = 0; i < kit.size(); ++i)
    {
        if (!plan->KitSlots[i])
            continue;
        uint8 slot = *plan->KitSlots[i];
        std::unique_ptr<Item> item(Item::CreateItem(kit[i].Entry, 1, BATTLE_PAY_L80_ITEM_CONTEXT));
        if (!item || item->ToAzeriteItem() || item->ToAzeriteEmpoweredItem())
            return false;
        item->SetOwnerGUID(character);
        if (item->GetBonding() == BIND_ON_ACQUIRE || item->GetBonding() == BIND_QUEST
            || (item->GetBonding() == BIND_ON_EQUIP && slot < REAGENT_BAG_SLOT_END))
            item->SetBinding(true);
        item->SaveToDB(trans);
        // INSERT must fail, rather than replacing someone else's item if a destination is occupied.
        trans->PAppend("INSERT INTO character_inventory (guid, bag, slot, item) VALUES ({}, 0, {}, {})",
            character.GetCounter(), uint32(slot), item->GetGUID().GetCounter());
        if (slot < EQUIPMENT_SLOT_END)
            equipped[slot] = item.get();
        created.push_back(std::move(item));
    }
    WriteBoostEquipmentCache(character, equipped, trans);

    trans->PAppend("UPDATE characters SET level = {}, xp = 0, money = GREATEST(money, {}), map = {}, zone = {}, "
        "position_x = {}, position_y = {}, position_z = {}, orientation = {}, instance_id = 0, "
        "trans_x = 0, trans_y = 0, trans_z = 0, trans_o = 0, transguid = 0, taxi_path = '', "
        "primarySpecialization = {}, activeTalentGroup = {}, logout_time = {} WHERE guid = {} AND account = {}",
        uint32(BATTLE_PAY_L80_LEVEL), BATTLE_PAY_L80_GOLD, BATTLE_PAY_L80_STAGING_MAP, BATTLE_PAY_L80_STAGING_ZONE,
        BATTLE_PAY_L80_STAGING_X, BATTLE_PAY_L80_STAGING_Y, BATTLE_PAY_L80_STAGING_Z, BATTLE_PAY_L80_STAGING_O,
        distribution.SpecId, uint32(spec->OrderIndex), GameTime::GetGameTime(), character.GetCounter(), _session->GetAccountId());
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_BATTLEPAY_DISTRIBUTION_ASSIGN);
    stmt->setUInt8(0, BattlePay::DIST_STATUS_FINISHED);
    stmt->setUInt64(1, character.GetCounter());
    stmt->setUInt32(2, distribution.SpecId);
    stmt->setUInt64(3, distribution.DistributionID);
    stmt->setUInt32(4, _session->GetAccountId());
    trans->Append(stmt);
    CharacterDatabase.DirectCommitTransaction(trans);

    // DirectCommitTransaction has no result value. Read back the completion marker from the same transaction.
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BATTLEPAY_DISTRIBUTION);
    stmt->setUInt64(0, distribution.DistributionID);
    stmt->setUInt32(1, _session->GetAccountId());
    PreparedQueryResult saved = CharacterDatabase.Query(stmt);
    if (!saved || !(*saved)[1].GetUInt8() || !(*saved)[2].GetUInt8()
        || (*saved)[3].GetUInt64() != character.GetCounter() || (*saved)[4].GetUInt32() != distribution.SpecId)
    {
        TC_LOG_ERROR("network", "BattlePay: offline L80 save not confirmed for {}; completion withheld", character.ToString());
        return false;
    }

    _boostedCharacters.insert(character.GetCounter());
    sCharacterCache->UpdateCharacterLevel(character, BATTLE_PAY_L80_LEVEL);
    ++_characterRevision;
    TC_LOG_INFO("network", "BattlePay: saved L80 kit {} ({} items), {} recovered items and boost {} for {} before login",
        loadout->ID, created.size(), plan->RecoveredItems.size(), distribution.DistributionID, character.ToString());
    return true;
}

// Boosts assigned before they were applied offline still wait for their character. One that cannot be
// applied now goes back to the account, so its character can still log in and the boost is offered again.
void BattlePayMgr::CompletePendingBoosts()
{
    bool returned = false;
    for (auto pending = _pendingApply.begin(); pending != _pendingApply.end(); pending = _pendingApply.erase(pending))
    {
        if (ApplyOfflineBoost(pending->second))
            continue;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_BATTLEPAY_DISTRIBUTION_RETURN_BY_TARGET);
        stmt->setUInt64(0, pending->first);
        trans->Append(stmt);
        CharacterDatabase.DirectCommitTransaction(trans);

        BattlePay::PendingDistribution distribution = pending->second;
        distribution.Status = BattlePay::DIST_STATUS_AVAILABLE;
        distribution.TargetCharacter.Clear();
        distribution.SpecId = 0;
        _distributions[distribution.DistributionID] = distribution;
        returned = true;

        TC_LOG_INFO("network", "BattlePay: could not apply distribution {} to character {}; returned it (account {})",
            distribution.DistributionID, pending->first, _session->GetAccountId());
    }

    if (returned)
        SendAvailableL80Distributions();
}

// Player::DeleteFromDB already released the rows. This keeps the open session in step, so an
// unused boost is offered again without logging out.
void BattlePayMgr::OnCharacterDeleted(ObjectGuid character)
{
    _boostedCharacters.erase(character.GetCounter());

    auto itr = _pendingApply.find(character.GetCounter());
    if (itr == _pendingApply.end())
        return;

    BattlePay::PendingDistribution distribution = itr->second;
    _pendingApply.erase(itr);

    distribution.Status = BattlePay::DIST_STATUS_AVAILABLE;
    distribution.TargetCharacter.Clear();
    distribution.SpecId = 0;
    _distributions[distribution.DistributionID] = distribution;
    SendAvailableL80Distributions();

    TC_LOG_INFO("network", "BattlePay: returned distribution {} after {} was deleted (account {})",
        distribution.DistributionID, character.ToString(), _session->GetAccountId());
}

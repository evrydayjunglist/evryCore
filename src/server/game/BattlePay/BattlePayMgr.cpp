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
#include "BattlePayPackets.h"
#include "Bag.h"
#include "CharacterPackets.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Field.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "Map.h"
#include "MapManager.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "StringConvert.h"
#include "Util.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "CatalogShopLicenseData.inc"

namespace
{
constexpr uint32 BATTLE_PAY_L80_PRODUCT_ID = 1161;
constexpr uint32 BATTLE_PAY_L80_SHOP_PRODUCT_INFO_ID = 1410;
constexpr uint32 BATTLE_PAY_L80_STAGING_MAP = 2552;
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
constexpr uint32 CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID = 1969052;
constexpr uint32 CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID = 1960794;
constexpr uint32 CATALOG_SHOP_L80_STANDARD_PRODUCT_ID = 1977499;

void SendRawServerOpcode(WorldSession* session, OpcodeServer opcode, uint8 const* bytes, size_t size)
{
    WorldPacket data(opcode, size);
    data.append(bytes, size);
    session->SendPacket(&data);
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

void MailRecoveredBoostItems(Player* player, std::vector<Item*> const& items)
{
    if (items.empty())
        return;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (size_t offset = 0; offset < items.size();)
    {
        size_t const batchEnd = std::min(offset + size_t(MAX_MAIL_ITEMS), items.size());
        MailDraft draft("Level Boost - recovered items",
            "Items removed from your character while applying a Level 80 Boost.");
        for (; offset < batchEnd; ++offset)
            draft.AddItem(items[offset]);
        draft.SendMailTo(trans, player, MailSender(MAIL_NORMAL, 0), MAIL_CHECK_MASK_COPIED);
    }
    CharacterDatabase.CommitTransaction(trans);
}

void MailOldKitForL80Boost(Player* player)
{
    std::vector<Item*> toMail;
    auto take = [&](uint8 bag, uint8 slot)
    {
        if (Item* item = player->GetItemByPos(bag, slot))
        {
            player->MoveItemFromInventory(bag, slot, true);
            toMail.push_back(item);
        }
    };

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        take(INVENTORY_SLOT_BAG_0, slot);

    for (uint8 slot = CHILD_EQUIPMENT_SLOT_START; slot < CHILD_EQUIPMENT_SLOT_END; ++slot)
        take(INVENTORY_SLOT_BAG_0, slot);

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        if (Bag* bag = player->GetBagByPos(bagSlot))
            for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                take(bagSlot, uint8(i));
        take(INVENTORY_SLOT_BAG_0, bagSlot);
    }

    for (uint8 bagSlot = REAGENT_BAG_SLOT_START; bagSlot < REAGENT_BAG_SLOT_END; ++bagSlot)
    {
        if (Bag* bag = player->GetBagByPos(bagSlot))
            for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                take(bagSlot, uint8(i));
        take(INVENTORY_SLOT_BAG_0, bagSlot);
    }

    MailRecoveredBoostItems(player, toMail);
}

bool GrantL80BoostKit(Player* player, uint8 classId, uint8 raceId, uint32 specId)
{
    CharacterLoadoutEntry const* loadout = SelectL80BoostLoadout(classId, raceId, specId);
    if (!loadout)
    {
        TC_LOG_ERROR("network", "BattlePay: no Purpose={} ItemContext={} loadout for class {} race {} spec {}",
            BATTLE_PAY_L80_LOADOUT_PURPOSE, uint32(BATTLE_PAY_L80_ITEM_CONTEXT), classId, raceId, specId);
        return false;
    }

    uint32 granted = 0;
    for (CharacterLoadoutItemEntry const* row : sCharacterLoadoutItemStore)
    {
        if (row->CharacterLoadoutID != loadout->ID)
            continue;
        if (player->StoreNewItemInBestSlots(row->ItemID, 1, BATTLE_PAY_L80_ITEM_CONTEXT))
            ++granted;
    }

    if (!granted)
    {
        TC_LOG_ERROR("network", "BattlePay: CharacterLoadout {} granted no items to {}", loadout->ID, player->GetGUID().ToString());
        return false;
    }

    TC_LOG_INFO("network", "BattlePay: granted L80 kit loadout {} ({} items) to {} (spec {})",
        loadout->ID, granted, player->GetGUID().ToString(), specId);
    return true;
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

bool BattlePayMgr::IsCatalogShopEnabled()
{
    return IsEnabled() && sWorld->getBoolConfig(CONFIG_BATTLE_PAY_SHOP2_ENABLED);
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

    do
    {
        Field* fields = result->Fetch();
        BattlePay::PendingDistribution distribution;
        distribution.DistributionID = fields[0].GetUInt64();
        distribution.PurchaseID = fields[1].GetUInt64();
        distribution.ProductID = fields[2].GetUInt32();
        distribution.Status = fields[3].GetUInt32();
        uint8 const consumed = fields[4].GetUInt8();
        uint8 const applied = fields[5].GetUInt8();
        uint64 const target = fields[6].GetUInt64();
        distribution.SpecId = fields[7].GetUInt32();
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

void BattlePayMgr::PersistAssignedDistribution(BattlePay::PendingDistribution const& distribution)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_BATTLEPAY_DISTRIBUTION_ASSIGN);
    stmt->setUInt8(0, uint8(BattlePay::DIST_STATUS_FINISHED));
    stmt->setUInt64(1, distribution.TargetCharacter.GetCounter());
    stmt->setUInt32(2, distribution.SpecId);
    stmt->setUInt64(3, distribution.DistributionID);
    stmt->setUInt32(4, _session->GetAccountId());
    trans->Append(stmt);
    CharacterDatabase.DirectCommitTransaction(trans);
}

void BattlePayMgr::PersistAppliedDistribution(uint64 distributionId)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_BATTLEPAY_DISTRIBUTION_APPLIED);
    stmt->setUInt64(0, distributionId);
    stmt->setUInt32(1, _session->GetAccountId());
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

    if (shopProductId == CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID
        || shopProductId == CATALOG_SHOP_L80_ENHANCED_CHILD_PRODUCT_ID
        || shopProductId == CATALOG_SHOP_L80_STANDARD_PRODUCT_ID)
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

bool BattlePayMgr::BeginFreePurchaseConfirm(uint32 productId, uint32 clientToken, bool sendStartPurchaseResponse)
{
    if (!IsEnabled())
    {
        if (sendStartPurchaseResponse)
            SendStartPurchaseResult(clientToken, 0, 13);
        return false;
    }

    uint32 const shopProductId = productId;
    uint32 const deliverableId = ResolveFreeBuyProductId(shopProductId);
    if (!deliverableId)
    {
        TC_LOG_INFO("network", "BattlePay: Free StartPurchase denied shopProduct {} (account {})",
            shopProductId, _session->GetAccountId());
        if (sendStartPurchaseResponse)
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
        if (sendStartPurchaseResponse)
            SendStartPurchaseResult(clientToken, 0, 3);
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

    if (sendStartPurchaseResponse)
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
    SendDistributionList();
    for (auto const& [_, distribution] : _distributions)
        if (distribution.Status == BattlePay::DIST_STATUS_AVAILABLE)
            SendDistributionUpdate(distribution);
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
    WorldPackets::BattlePay::DistributionListResponse response;
    response.Result = 0;
    for (auto const& [_, distribution] : _distributions)
    {
        WorldPackets::BattlePay::BattlePayDistributionObject& object = response.Distributions.emplace_back();
        object.DistributionID = distribution.DistributionID;
        object.Status = distribution.Status;
        object.ProductID = distribution.ProductID;
        object.PurchaseID = distribution.PurchaseID;
        object.TargetPlayer = distribution.TargetCharacter;
        if (!object.TargetPlayer.IsEmpty())
        {
            object.TargetVirtualRealm = GetVirtualRealmAddress();
            object.TargetNativeRealm = GetVirtualRealmAddress();
        }
    }
    _session->SendPacket(response.Write());
}

void BattlePayMgr::SendDistributionUpdate(BattlePay::PendingDistribution const& distribution)
{
    WorldPackets::BattlePay::DistributionUpdate update;
    update.Distribution.DistributionID = distribution.DistributionID;
    update.Distribution.Status = distribution.Status;
    update.Distribution.ProductID = distribution.ProductID;
    update.Distribution.PurchaseID = distribution.PurchaseID;
    update.Distribution.TargetPlayer = distribution.TargetCharacter;
    if (!update.Distribution.TargetPlayer.IsEmpty())
    {
        update.Distribution.TargetVirtualRealm = GetVirtualRealmAddress();
        update.Distribution.TargetNativeRealm = GetVirtualRealmAddress();
    }
    _session->SendPacket(update.Write());
}

void BattlePayMgr::HandleStartPurchase(uint32 clientToken, uint32 productId, ObjectGuid /*targetCharacter*/)
{
    BeginFreePurchaseConfirm(productId, clientToken);
}

void BattlePayMgr::SendCatalogShopObtainLicenses()
{
    if (!IsCatalogShopEnabled())
        return;

    // Retail shop-lists: two OBTAIN_LICENSE uint32 ids sent with shop2 chrome.
    static constexpr uint32 CatalogShopLicenseIds[] = { 0x11BD37u, 0x11BD35u };
    for (uint32 licenseId : CatalogShopLicenseIds)
    {
        WorldPackets::CatalogShop::ObtainLicense packet;
        packet.LicenseId = licenseId;
        _session->SendPacket(packet.Write());
    }
}

void BattlePayMgr::SendLastCatalogFetchResponse()
{
    if (!IsCatalogShopEnabled())
        return;

    WorldPackets::CatalogShop::LastCatalogFetchResponse response;
    response.LastFetchUnixTime = 0x6A658BEFull;
    _session->SendPacket(response.Write());
}

void BattlePayMgr::HandleCatalogShopLicenseGameDataRequest(uint32 requestSize)
{
    if (!IsCatalogShopEnabled())
        return;

    uint8 const* payload = nullptr;
    size_t payloadSize = 0;
    switch (requestSize)
    {
        case 872:
            payload = CatalogShopRetailLicenseData344;
            payloadSize = sizeof(CatalogShopRetailLicenseData344);
            break;
        case 136:
            payload = CatalogShopRetailLicenseData358;
            payloadSize = sizeof(CatalogShopRetailLicenseData358);
            break;
        case 32:
            payload = CatalogShopRetailLicenseData359;
            payloadSize = sizeof(CatalogShopRetailLicenseData359);
            break;
        case 20:
            payload = CatalogShopRetailLicenseData361;
            payloadSize = sizeof(CatalogShopRetailLicenseData361);
            break;
        default:
            TC_LOG_DEBUG("network", "BattlePay: CatalogShop LICENSE_GAME_DATA_REQUEST size {} — using primary companion stub",
                requestSize);
            payload = CatalogShopRetailLicenseData344;
            payloadSize = sizeof(CatalogShopRetailLicenseData344);
            break;
    }

    SendRawServerOpcode(_session, SMSG_CATALOG_SHOP_LICENSE_DATA, payload, payloadSize);
}

void BattlePayMgr::HandleOpenCheckout(uint32 checkoutRequestId)
{
    if (!IsCatalogShopEnabled())
        return;

    uint64 const issued = uint64(GameTime::GetGameTime());
    uint64 const expires = issued + 14400;
    // Local dummy token — retail shape XUS-{32hex}-{8hex} (45 chars), not Blizzard cloud SSO.
    // Account id is the first 8 hex after the prefix so local checkout can map Buy to this session.
    std::string const token = Trinity::StringFormat("XUS-{:08x}{:08x}{:08x}{:08x}-{:08x}",
        _session->GetAccountId(),
        checkoutRequestId,
        uint32(issued),
        uint32(expires),
        0x55867037u);

    WorldPackets::BattlePay::GenerateSSOTokenResponse response;
    response.CheckoutRequestID = checkoutRequestId;
    response.Unk0 = 0;
    response.IssuedUnixTime = issued;
    response.ExpiresUnixTime = expires;
    response.Token = token;

    _session->SendPacket(response.Write());
    TC_LOG_INFO("network", "BattlePay: OpenCheckout {} account {} → GENERATE_SSO_TOKEN_RESPONSE",
        checkoutRequestId, _session->GetAccountId());
}

void BattlePayMgr::TryConsumeCatalogShopFreeBuySignal(uint32 diff)
{
    if (!IsCatalogShopEnabled())
        return;

    std::string const& signalDir = sWorld->GetCatalogShopFreeBuySignalDir();
    if (signalDir.empty())
        return;

    constexpr uint32 kPollPeriodMs = 250;
    if (diff)
    {
        _freeBuySignalPollAccumMs += diff;
        if (_freeBuySignalPollAccumMs < kPollPeriodMs)
            return;
        _freeBuySignalPollAccumMs = 0;
    }

    std::filesystem::path const signalPath =
        std::filesystem::path(signalDir) / Trinity::StringFormat("{}.signal", _session->GetAccountId());

    std::error_code ec;
    if (!std::filesystem::is_regular_file(signalPath, ec))
        return;

    std::ifstream in(signalPath);
    std::string body;
    if (in)
        std::getline(in, body);
    in.close();
    std::filesystem::remove(signalPath, ec);

    uint32 shopProductId = 0;
    if (Optional<uint32> parsed = Trinity::StringTo<uint32>(body))
        shopProductId = *parsed;
    if (!shopProductId)
        shopProductId = CATALOG_SHOP_L80_ENHANCED_PRODUCT_ID;

    TC_LOG_INFO("network", "BattlePay: CatalogShop Free Buy signal shopProduct {} account {} → Free Confirm",
        shopProductId, _session->GetAccountId());

    if (!BeginFreePurchaseConfirm(shopProductId, shopProductId, false))
        return;

    if (_pendingPurchase.AwaitingConfirm)
        HandleConfirmPurchaseResponse(true, _pendingPurchase.ServerToken, 0);
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

bool BattlePayMgr::CanAssignToCharacter(ObjectGuid targetCharacter, uint32 productChoice) const
{
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
    if (PreparedQueryResult onlineResult = CharacterDatabase.Query(onlineStmt))
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

    uint8 const classId = fields[1].GetUInt8();
    uint8 const level = fields[2].GetUInt8();
    uint8 const raceId = fields[3].GetUInt8();
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

    if (!CanAssignToCharacter(targetCharacter, productChoice))
        return fail(3);

    BattlePay::PendingDistribution& distribution = itr->second;
    distribution.TargetCharacter = targetCharacter;
    distribution.SpecId = productChoice;

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

    PersistAssignedDistribution(distribution);
    _pendingApply[targetCharacter.GetCounter()] = distribution;
    _distributions.erase(itr);
    ResurfaceRemainingAvailableL80Distributions();

    TC_LOG_INFO("network", "BattlePay: assigned L80 product {} to {} (spec {}) for account {}",
        GetL80ProductId(), targetCharacter.ToString(), productChoice, _session->GetAccountId());
    return true;
}

void BattlePayMgr::OverlayEnumExperienceLevel(ObjectGuid character, uint8& experienceLevel) const
{
    if (_pendingApply.contains(character.GetCounter()))
        experienceLevel = BATTLE_PAY_L80_LEVEL;
}

bool BattlePayMgr::RelocateToBoostStart(Player* player) const
{
    if (!sMapStore.LookupEntry(BATTLE_PAY_L80_STAGING_MAP))
    {
        TC_LOG_ERROR("network", "BattlePay: map {} is missing from Map.db2; cannot relocate {}",
            BATTLE_PAY_L80_STAGING_MAP, player->GetGUID().ToString());
        return false;
    }

    if (player->IsInWorld())
        return player->TeleportTo(BATTLE_PAY_L80_STAGING_MAP, BATTLE_PAY_L80_STAGING_X, BATTLE_PAY_L80_STAGING_Y,
            BATTLE_PAY_L80_STAGING_Z, BATTLE_PAY_L80_STAGING_O);

    uint32 const oldMapId = player->GetMapId();
    Position const oldPos = player->GetPosition();
    Map* const oldMap = player->GetMap();

    player->ResetMap();
    player->WorldRelocate(BATTLE_PAY_L80_STAGING_MAP, BATTLE_PAY_L80_STAGING_X, BATTLE_PAY_L80_STAGING_Y,
        BATTLE_PAY_L80_STAGING_Z, BATTLE_PAY_L80_STAGING_O);
    if (Map* boostMap = sMapMgr->CreateMap(BATTLE_PAY_L80_STAGING_MAP, player))
    {
        player->SetMap(boostMap);
        player->UpdatePositionData();
        player->SetFallInformation(0, BATTLE_PAY_L80_STAGING_Z);
        return true;
    }

    player->WorldRelocate(oldMapId, oldPos);
    player->SetMap(oldMap);
    TC_LOG_ERROR("network", "BattlePay: CreateMap({}) failed for {}", BATTLE_PAY_L80_STAGING_MAP, player->GetGUID().ToString());
    return false;
}

bool BattlePayMgr::ApplyPendingBoostOnLogin(Player* player)
{
    if (!player)
        return false;

    auto itr = _pendingApply.find(player->GetGUID().GetCounter());
    if (itr == _pendingApply.end())
        return false;

    BattlePay::PendingDistribution pending = itr->second;
    if (player->GetLevel() >= BATTLE_PAY_L80_LEVEL)
    {
        TC_LOG_INFO("network", "BattlePay: {} already at or above L80 on login; marking boost applied without grant",
            player->GetGUID().ToString());
        PersistAppliedDistribution(pending.DistributionID);
        _boostedCharacters.insert(player->GetGUID().GetCounter());
        _pendingApply.erase(itr);
        return false;
    }

    MailOldKitForL80Boost(player);

    player->GiveLevel(BATTLE_PAY_L80_LEVEL);
    player->SetXP(0);

    if (!GrantL80BoostKit(player, player->GetClass(), player->GetRace(), pending.SpecId))
        TC_LOG_ERROR("network", "BattlePay: L80 kit grant failed for {} after GiveLevel", player->GetGUID().ToString());

    if (player->GetMoney() < BATTLE_PAY_L80_GOLD)
        player->SetMoney(BATTLE_PAY_L80_GOLD);

    if (ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(pending.SpecId))
        if (spec->ClassID == player->GetClass())
            player->ActivateTalentGroup(spec);

    bool const relocated = RelocateToBoostStart(player);
    PersistAppliedDistribution(pending.DistributionID);
    _boostedCharacters.insert(player->GetGUID().GetCounter());
    _pendingApply.erase(itr);

    TC_LOG_INFO("network", "BattlePay: applied L80 boost on login for {} (spec {}, relocated {})",
        player->GetGUID().ToString(), pending.SpecId, relocated);
    // Skip Catch Up even if relocate failed, so it cannot move a granted L80 to Arathi.
    return true;
}

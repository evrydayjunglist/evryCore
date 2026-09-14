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
#include "CharacterPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

void WorldSession::HandleBattlePayGetProductList(WorldPackets::BattlePay::GetProductList& /*packet*/)
{
    GetBattlePayMgr()->SendProductList();
}

void WorldSession::HandleBattlePayGetPurchaseList(WorldPackets::BattlePay::GetPurchaseList& /*packet*/)
{
    GetBattlePayMgr()->SendPurchaseList();
}

void WorldSession::HandleBattlePayStartPurchase(WorldPackets::BattlePay::StartPurchase& packet)
{
    GetBattlePayMgr()->HandleStartPurchase(packet.ClientToken, packet.ProductID, packet.TargetCharacter);
}

void WorldSession::HandleBattlePayOpenCheckout(WorldPackets::BattlePay::OpenCheckout& packet)
{
    GetBattlePayMgr()->HandleOpenCheckout(packet.CheckoutRequestID);
}

void WorldSession::HandleBattlePayConfirmPurchaseResponse(WorldPackets::BattlePay::ConfirmPurchaseResponse& packet)
{
    GetBattlePayMgr()->HandleConfirmPurchaseResponse(packet.ConfirmPurchase, packet.ServerToken, packet.ClientCurrentPriceFixedPoint);
}

void WorldSession::HandleBattlePayDistributionAssignToTarget(WorldPackets::BattlePay::DistributionAssignToTarget& packet)
{
    if (!GetBattlePayMgr()->HandleDistributionAssignToTarget(packet.ClientToken, packet.DistributionID, packet.TargetCharacter, packet.ProductChoice))
        return;

    // Same path as CMSG_ENUM_CHARACTERS so the plate shows the assigned boost level.
    RequestCharacterEnum();
    SendFeatureSystemStatusGlueScreen();
}

void WorldSession::HandleCharacterUpgradeStart(WorldPackets::Character::CharacterUpgradeStart& packet)
{
    GetBattlePayMgr()->HandleCharacterUpgradeStart(packet.CharacterGUID, packet.ProductChoice);
}

void WorldSession::HandleGetLastCatalogFetch(WorldPackets::CatalogShop::GetLastCatalogFetch& /*packet*/)
{
    GetBattlePayMgr()->SendLastCatalogFetchResponse();
}

void WorldSession::HandleUpdateLastCatalogFetch(WorldPackets::CatalogShop::UpdateLastCatalogFetch& /*packet*/)
{
    GetBattlePayMgr()->SendLastCatalogFetchResponse();
}

void WorldSession::HandleGetDecorRefundList(WorldPackets::CatalogShop::GetDecorRefundList& /*packet*/)
{
    GetBattlePayMgr()->SendDecorRefundListResponse();
}

void WorldSession::HandleGetAllLicensedDecorQuantities(WorldPackets::CatalogShop::GetAllLicensedDecorQuantities& /*packet*/)
{
    GetBattlePayMgr()->SendAllLicensedDecorQuantitiesResponse();
}

void WorldSession::HandleCatalogShopLicenseGameDataRequest(WorldPackets::CatalogShop::LicenseGameDataRequest& packet)
{
    GetBattlePayMgr()->HandleCatalogShopLicenseGameDataRequest(packet.RequestSize);
}

void WorldSession::RequestCharacterEnum()
{
    WorldPacket data(CMSG_ENUM_CHARACTERS);
    WorldPackets::Character::EnumCharacters enumCharacters(std::move(data));
    HandleCharEnumOpcode(enumCharacters);
}

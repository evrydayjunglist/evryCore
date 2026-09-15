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

#ifndef TRINITYCORE_BATTLE_PAY_PACKETS_H
#define TRINITYCORE_BATTLE_PAY_PACKETS_H

#include "ObjectGuid.h"
#include "Optional.h"
#include "Packet.h"
#include <string>
#include <vector>

namespace WorldPackets
{
    namespace BattlePay
    {
        class GetProductList final : public ClientPacket
        {
        public:
            explicit GetProductList(WorldPacket&& packet) : ClientPacket(CMSG_BATTLE_PAY_GET_PRODUCT_LIST, std::move(packet)) { }

            void Read() override { }
        };

        class GetPurchaseList final : public ClientPacket
        {
        public:
            explicit GetPurchaseList(WorldPacket&& packet) : ClientPacket(CMSG_BATTLE_PAY_GET_PURCHASE_LIST, std::move(packet)) { }

            void Read() override { }
        };

        class StartPurchase final : public ClientPacket
        {
        public:
            explicit StartPurchase(WorldPacket&& packet) : ClientPacket(CMSG_BATTLE_PAY_START_PURCHASE, std::move(packet)) { }

            void Read() override;

            uint32 ClientToken = 0;
            uint32 ProductID = 0;
            ObjectGuid TargetCharacter;
        };

        class OpenCheckout final : public ClientPacket
        {
        public:
            explicit OpenCheckout(WorldPacket&& packet) : ClientPacket(CMSG_BATTLE_PAY_OPEN_CHECKOUT, std::move(packet)) { }

            void Read() override;

            uint32 CheckoutRequestID = 0;
        };

        class ConfirmPurchaseResponse final : public ClientPacket
        {
        public:
            explicit ConfirmPurchaseResponse(WorldPacket&& packet) : ClientPacket(CMSG_BATTLE_PAY_CONFIRM_PURCHASE_RESPONSE, std::move(packet)) { }

            void Read() override;

            bool ConfirmPurchase = false;
            uint32 ServerToken = 0;
            uint64 ClientCurrentPriceFixedPoint = 0;
        };

        class DistributionAssignToTarget final : public ClientPacket
        {
        public:
            explicit DistributionAssignToTarget(WorldPacket&& packet) : ClientPacket(CMSG_BATTLE_PAY_DISTRIBUTION_ASSIGN_TO_TARGET, std::move(packet)) { }

            void Read() override;

            uint32 ClientToken = 0;
            uint64 DistributionID = 0;
            ObjectGuid TargetCharacter;
            uint32 ProductChoice = 0;
        };

        struct ProductDisplayCard
        {
            std::string Name;
            uint32 UnkInt1 = 0;
            uint32 UnkInt2 = 0;
            uint32 UnkInt3 = 0;
        };

        struct ProductDisplayInfo
        {
            Optional<uint32> CreatureDisplayInfoID;
            Optional<uint32> VisualsId;
            Optional<uint32> Flags;
            Optional<uint32> Flags2;
            Optional<uint32> Flags3;
            Optional<uint32> Flags4;
            std::string Name1;
            std::string Name2;
            std::string Name3;
            std::string Name4;
            std::string Name5;
            std::string Name6;
            std::string Name7;
            uint32 UnkInt1 = 0;
            uint32 UnkInt2 = 0;
            uint32 UnkInt3 = 0;
            std::vector<ProductDisplayCard> DisplayCards;
        };

        struct ProductItem
        {
            uint32 ID = 0;
            uint32 ItemID = 0;
            uint32 Quantity = 0;
            uint32 UnkInt1 = 0;
            uint32 UnkInt2 = 0;
            uint32 UnkInt3 = 0;
            bool HasPet = false;
            Optional<uint8> PetResult;
            Optional<ProductDisplayInfo> DisplayInfo;
        };

        struct BattlePayProduct
        {
            uint32 ProductID = 0;
            uint32 Type = 0;
            uint32 Flags = 0;
            uint32 UnkInt1 = 0;
            uint32 DisplayId = 0;
            uint32 ItemId = 0;
            uint32 UnkInt4 = 0;
            uint32 UnkInt5 = 0;
            uint32 UnkInt6 = 0;
            uint32 UnkInt7 = 0;
            uint32 UnkInt8 = 0;
            uint32 UnkInt9 = 0;
            uint32 UnkInt10 = 0;
            std::string UnkString;
            bool UnkBit = false;
            Optional<uint8> UnkBits;
            std::vector<ProductItem> Items;
            Optional<ProductDisplayInfo> DisplayInfo;
        };

        struct ProductInfoStruct
        {
            uint32 ProductID = 0;
            uint64 NormalPriceFixedPoint = 0;
            uint64 CurrentPriceFixedPoint = 0;
            uint32 UnkInt2 = 0;
            uint32 UnkInt3 = 0;
            uint32 UnkInt4 = 0;
            uint32 UnkInt5 = 0;
            uint64 UnkLong = 0;
            std::vector<uint32> ProductIDs;
            std::vector<uint32> UnkInts;
            Optional<ProductDisplayInfo> DisplayInfo;
        };

        struct BattlePayProductGroup
        {
            uint32 GroupID = 0;
            uint32 IconFileDataID = 0;
            uint8 DisplayType = 0;
            uint32 Ordering = 0;
            uint32 UnkInt = 0;
            uint32 UnkInt2 = 0;
            std::string Name;
            std::string IsAvailableDescription;
        };

        struct BattlePayShopEntry
        {
            uint32 EntryID = 0;
            uint32 GroupID = 0;
            uint32 ProductID = 0;
            int32 Ordering = 0;
            uint32 VasServiceType = 0;
            uint8 StoreDeliveryType = 0;
            Optional<ProductDisplayInfo> DisplayInfo;
        };

        struct BattlePayDistributionObject
        {
            uint64 DistributionID = 0;
            uint32 Status = 0;
            uint32 ProductID = 0;
            ObjectGuid AccountGUID;
            ObjectGuid TargetPlayer;
            uint32 TargetVirtualRealm = 0;
            uint32 TargetNativeRealm = 0;
            uint64 PurchaseID = 0;
            uint32 UnkInt = 0;
            Optional<BattlePayProduct> Product;
            bool Revoked = false;
        };

        struct BattlePayPurchase
        {
            uint64 PurchaseID = 0;
            uint32 Status = 0;
            uint32 ResultCode = 0;
            uint32 ProductID = 0;
            uint64 UnkLong = 0;
            uint64 UnkLong2 = 0;
            uint64 UnkLong3 = 0;
            std::string WalletName;
        };

        class ProductListResponse final : public ServerPacket
        {
        public:
            explicit ProductListResponse() : ServerPacket(SMSG_BATTLE_PAY_GET_PRODUCT_LIST_RESPONSE, 24) { }

            WorldPacket const* Write() override;

            uint32 Result = 0;
            uint32 CurrencyID = 1;
            std::vector<ProductInfoStruct> ProductInfo;
            std::vector<BattlePayProduct> Products;
            std::vector<BattlePayProductGroup> ProductGroups;
            std::vector<BattlePayShopEntry> Shop;
        };

        class PurchaseListResponse final : public ServerPacket
        {
        public:
            explicit PurchaseListResponse() : ServerPacket(SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE, 8) { }

            WorldPacket const* Write() override;

            uint32 Result = 0;
            std::vector<BattlePayPurchase> Purchases;
        };

        class DistributionListResponse final : public ServerPacket
        {
        public:
            explicit DistributionListResponse() : ServerPacket(SMSG_BATTLE_PAY_GET_DISTRIBUTION_LIST_RESPONSE, 8) { }

            WorldPacket const* Write() override;

            uint32 Result = 0;
            std::vector<BattlePayDistributionObject> Distributions;
        };

        class DistributionUpdate final : public ServerPacket
        {
        public:
            explicit DistributionUpdate() : ServerPacket(SMSG_BATTLE_PAY_DISTRIBUTION_UPDATE, 64) { }

            WorldPacket const* Write() override;

            BattlePayDistributionObject Distribution;
        };

        class StartPurchaseResponse final : public ServerPacket
        {
        public:
            explicit StartPurchaseResponse() : ServerPacket(SMSG_BATTLE_PAY_START_PURCHASE_RESPONSE, 16) { }

            WorldPacket const* Write() override;

            uint64 PurchaseID = 0;
            uint32 PurchaseResult = 0;
            uint32 ClientToken = 0;
        };

        class PurchaseUpdate final : public ServerPacket
        {
        public:
            explicit PurchaseUpdate() : ServerPacket(SMSG_BATTLE_PAY_PURCHASE_UPDATE, 4) { }

            WorldPacket const* Write() override;

            std::vector<BattlePayPurchase> Purchases;
        };

        class ConfirmPurchase final : public ServerPacket
        {
        public:
            explicit ConfirmPurchase() : ServerPacket(SMSG_BATTLE_PAY_CONFIRM_PURCHASE, 20) { }

            WorldPacket const* Write() override;

            uint64 PurchaseID = 0;
            uint64 CurrentPriceFixedPoint = 0;
            uint32 ServerToken = 0;
        };

        class StartDistributionAssignToTargetResponse final : public ServerPacket
        {
        public:
            explicit StartDistributionAssignToTargetResponse() : ServerPacket(SMSG_BATTLE_PAY_START_DISTRIBUTION_ASSIGN_TO_TARGET_RESPONSE, 16) { }

            WorldPacket const* Write() override;

            uint32 Result = 0;
            uint32 ClientToken = 0;
            uint64 DistributionID = 0;
        };

        // CatalogShop browse: OPEN_CHECKOUT is answered with this dummy local token, not Blizzard SSO.
        // Wire: uint32 requestId, uint32 unk0, uint64 issuedUnix, uint64 expiresUnix, 45-char token (no length).
        class GenerateSSOTokenResponse final : public ServerPacket
        {
        public:
            explicit GenerateSSOTokenResponse() : ServerPacket(SMSG_GENERATE_SSO_TOKEN_RESPONSE, 69) { }

            WorldPacket const* Write() override;

            uint32 CheckoutRequestID = 0;
            uint32 Unk0 = 0;
            uint64 IssuedUnixTime = 0;
            uint64 ExpiresUnixTime = 0;
            std::string Token;
        };
    }

    namespace CatalogShop
    {
        class GetLastCatalogFetch final : public ClientPacket
        {
        public:
            explicit GetLastCatalogFetch(WorldPacket&& packet) : ClientPacket(CMSG_GET_LAST_CATALOG_FETCH, std::move(packet)) { }

            void Read() override;
        };

        class UpdateLastCatalogFetch final : public ClientPacket
        {
        public:
            explicit UpdateLastCatalogFetch(WorldPacket&& packet) : ClientPacket(CMSG_UPDATE_LAST_CATALOG_FETCH, std::move(packet)) { }

            void Read() override;
        };

        class GetDecorRefundList final : public ClientPacket
        {
        public:
            explicit GetDecorRefundList(WorldPacket&& packet) : ClientPacket(CMSG_GET_DECOR_REFUND_LIST, std::move(packet)) { }

            void Read() override { }
        };

        class GetAllLicensedDecorQuantities final : public ClientPacket
        {
        public:
            explicit GetAllLicensedDecorQuantities(WorldPacket&& packet) : ClientPacket(CMSG_GET_ALL_LICENSED_DECOR_QUANTITIES, std::move(packet)) { }

            void Read() override { }
        };

        class LicenseGameDataRequest final : public ClientPacket
        {
        public:
            explicit LicenseGameDataRequest(WorldPacket&& packet) : ClientPacket(CMSG_CATALOG_SHOP_LICENSE_GAME_DATA_REQUEST, std::move(packet)) { }

            void Read() override;

            uint32 RequestSize = 0;
        };

        class LastCatalogFetchResponse final : public ServerPacket
        {
        public:
            explicit LastCatalogFetchResponse() : ServerPacket(SMSG_LAST_CATALOG_FETCH_RESPONSE, 8) { }

            WorldPacket const* Write() override;

            uint64 LastFetchUnixTime = 0;
        };

        class ObtainLicense final : public ServerPacket
        {
        public:
            explicit ObtainLicense() : ServerPacket(SMSG_CATALOG_SHOP_OBTAIN_LICENSE, 4) { }

            WorldPacket const* Write() override;

            uint32 LicenseId = 0;
        };
    }
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductDisplayInfo const& displayInfo);
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductDisplayCard const& card);
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductItem const& item);
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayProduct const& product);
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductInfoStruct const& info);
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayDistributionObject const& object);
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayPurchase const& purchase);

#endif // TRINITYCORE_BATTLE_PAY_PACKETS_H

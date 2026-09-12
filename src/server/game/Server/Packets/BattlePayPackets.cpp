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

#include "BattlePayPackets.h"
#include "PacketOperators.h"
#include <algorithm>
#include <cstring>

namespace WorldPackets::BattlePay
{
void DistributionAssignToTarget::Read()
{
    _worldPacket >> ClientToken;
    _worldPacket >> DistributionID;
    _worldPacket >> TargetCharacter;
    _worldPacket >> ProductChoice;
}

void StartPurchase::Read()
{
    _worldPacket >> ClientToken;
    _worldPacket >> ProductID;
    _worldPacket >> TargetCharacter;
    if (_worldPacket.rpos() < _worldPacket.size())
        _worldPacket.read_skip(_worldPacket.size() - _worldPacket.rpos());
}

void OpenCheckout::Read()
{
    _worldPacket >> CheckoutRequestID;
}

void ConfirmPurchaseResponse::Read()
{
    ConfirmPurchase = _worldPacket.ReadBit();
    _worldPacket >> ServerToken;
    _worldPacket >> ClientCurrentPriceFixedPoint;
}

WorldPacket const* ProductListResponse::Write()
{
    _worldPacket << uint32(Result);
    _worldPacket << uint32(CurrencyID);
    _worldPacket << Size<uint32>(ProductInfo);
    _worldPacket << Size<uint32>(Products);
    _worldPacket << Size<uint32>(ProductGroups);
    _worldPacket << Size<uint32>(Shop);

    for (ProductInfoStruct const& info : ProductInfo)
        _worldPacket << info;

    for (BattlePayProduct const& product : Products)
        _worldPacket << product;

    for (BattlePayProductGroup const& group : ProductGroups)
    {
        _worldPacket << uint32(group.GroupID);
        _worldPacket << uint32(group.IconFileDataID);
        _worldPacket << uint8(group.DisplayType);
        _worldPacket << uint32(group.Ordering);
        _worldPacket << uint32(group.UnkInt);
        _worldPacket << SizedString::BitsSize<8>(group.Name);
        _worldPacket << SizedString::BitsSize<24>(group.IsAvailableDescription);
        _worldPacket.FlushBits();
        _worldPacket << SizedString::Data(group.Name);
        _worldPacket << SizedString::Data(group.IsAvailableDescription);
    }

    for (BattlePayShopEntry const& entry : Shop)
    {
        _worldPacket << uint32(entry.EntryID);
        _worldPacket << uint32(entry.GroupID);
        _worldPacket << uint32(entry.ProductID);
        _worldPacket << int32(entry.Ordering);
        _worldPacket << uint32(entry.VasServiceType);
        _worldPacket << uint8(entry.StoreDeliveryType);
        _worldPacket << OptionalInit(entry.DisplayInfo);
        _worldPacket.FlushBits();
        if (entry.DisplayInfo)
            _worldPacket << *entry.DisplayInfo;
    }

    return &_worldPacket;
}

WorldPacket const* PurchaseListResponse::Write()
{
    _worldPacket << uint32(Result);
    _worldPacket << Size<uint32>(Purchases);
    for (BattlePayPurchase const& purchase : Purchases)
        _worldPacket << purchase;
    return &_worldPacket;
}

WorldPacket const* DistributionListResponse::Write()
{
    uint32 distributionCount = uint32(Distributions.size());
    _worldPacket << uint32(Result);
    _worldPacket << Bits<11>(distributionCount);
    _worldPacket.FlushBits();
    for (BattlePayDistributionObject const& distribution : Distributions)
        _worldPacket << distribution;
    return &_worldPacket;
}

WorldPacket const* DistributionUpdate::Write()
{
    _worldPacket << Distribution;
    return &_worldPacket;
}

WorldPacket const* StartPurchaseResponse::Write()
{
    _worldPacket << uint64(PurchaseID);
    _worldPacket << uint32(PurchaseResult);
    _worldPacket << uint32(ClientToken);
    return &_worldPacket;
}

WorldPacket const* PurchaseUpdate::Write()
{
    _worldPacket << Size<uint32>(Purchases);
    for (BattlePayPurchase const& purchase : Purchases)
        _worldPacket << purchase;
    return &_worldPacket;
}

WorldPacket const* ConfirmPurchase::Write()
{
    _worldPacket << uint64(PurchaseID);
    _worldPacket << uint64(CurrentPriceFixedPoint);
    _worldPacket << uint32(ServerToken);
    return &_worldPacket;
}

WorldPacket const* StartDistributionAssignToTargetResponse::Write()
{
    _worldPacket << uint32(Result);
    _worldPacket << uint32(ClientToken);
    _worldPacket << uint64(DistributionID);
    return &_worldPacket;
}

WorldPacket const* GenerateSSOTokenResponse::Write()
{
    _worldPacket << uint32(CheckoutRequestID);
    _worldPacket << uint32(Unk0);
    _worldPacket << uint64(IssuedUnixTime);
    _worldPacket << uint64(ExpiresUnixTime);
    // Retail length 69 with no string size prefix — raw 45-char XUS-… body.
    _worldPacket.append(Token.data(), Token.size());
    return &_worldPacket;
}
}

namespace WorldPackets::CatalogShop
{
void GetLastCatalogFetch::Read()
{
    if (_worldPacket.rpos() < _worldPacket.size())
        _worldPacket.read_skip(_worldPacket.size() - _worldPacket.rpos());
}

void LicenseGameDataRequest::Read()
{
    RequestSize = _worldPacket.size();
    _worldPacket.read_skip(_worldPacket.size() - _worldPacket.rpos());
}

WorldPacket const* LastCatalogFetchResponse::Write()
{
    _worldPacket << uint64(LastFetchUnixTime);
    return &_worldPacket;
}

WorldPacket const* ObtainLicense::Write()
{
    _worldPacket << uint32(LicenseId);
    return &_worldPacket;
}
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductDisplayInfo const& displayInfo)
{
    using namespace WorldPackets;

    data << OptionalInit(displayInfo.CreatureDisplayInfoID);
    data << OptionalInit(displayInfo.VisualsId);
    data << SizedString::BitsSize<10>(displayInfo.Name1);
    data << SizedString::BitsSize<10>(displayInfo.Name2);
    data << SizedString::BitsSize<13>(displayInfo.Name3);
    data << SizedString::BitsSize<13>(displayInfo.Name4);
    data << SizedString::BitsSize<13>(displayInfo.Name5);
    data << OptionalInit(displayInfo.Flags);
    data << OptionalInit(displayInfo.Flags2);
    data << OptionalInit(displayInfo.Flags3);
    data << OptionalInit(displayInfo.Flags4);
    data.FlushBits();

    data << uint32(0); // no nested visuals / display cards

    if (displayInfo.CreatureDisplayInfoID)
        data << uint32(*displayInfo.CreatureDisplayInfoID);
    if (displayInfo.VisualsId)
        data << uint32(*displayInfo.VisualsId);

    data << SizedString::Data(displayInfo.Name1);
    data << SizedString::Data(displayInfo.Name2);
    data << SizedString::Data(displayInfo.Name3);
    data << SizedString::Data(displayInfo.Name4);
    data << SizedString::Data(displayInfo.Name5);

    if (displayInfo.Flags)
        data << uint32(*displayInfo.Flags);
    if (displayInfo.Flags2)
        data << uint32(*displayInfo.Flags2);
    if (displayInfo.Flags3)
        data << uint32(*displayInfo.Flags3);
    if (displayInfo.Flags4)
        data << uint32(*displayInfo.Flags4);

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayProduct const& product)
{
    using namespace WorldPackets;

    data << uint32(product.ProductID);
    data << uint8(product.Type);
    data << uint32(product.Flags);
    data << uint32(product.UnkInt1);
    data << uint32(product.DisplayId);
    data << uint32(product.ItemId);
    data << uint32(product.UnkInt4);
    data << uint8(product.UnkInt5);

    bool const hasUnkBits = false;
    uint32 const itemCount = 0;
    bool const hasDisplayInfo = product.DisplayInfo.has_value();

    data << SizedString::BitsSize<8>(product.UnkString);
    data << Bits<1>(hasUnkBits);
    data << Bits<7>(itemCount);
    data << Bits<1>(hasDisplayInfo);
    data.FlushBits();
    data << SizedString::Data(product.UnkString);

    if (product.DisplayInfo)
        data << *product.DisplayInfo;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductInfoStruct const& info)
{
    using namespace WorldPackets;

    data << uint32(info.ProductID);
    data << uint64(info.NormalPriceFixedPoint);
    data << uint64(info.CurrentPriceFixedPoint);
    data << Size<uint32>(info.ProductIDs);
    data << uint32(info.UnkInt2);
    data << Size<uint32>(info.UnkInts);

    for (uint32 productId : info.ProductIDs)
        data << uint32(productId);
    for (uint32 value : info.UnkInts)
        data << uint32(value);

    data << Bits<7>(info.ChoiceType);
    data << OptionalInit(info.DisplayInfo);
    data.FlushBits();

    if (info.DisplayInfo)
        data << *info.DisplayInfo;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayDistributionObject const& object)
{
    using namespace WorldPackets;

    // Enhanced L80 Boost (1161): sniff-backed object body. AVAILABLE is 101 bytes
    // (header + 21-byte mid + 56-byte product nest). Assign statuses 2-4 add a 7-byte
    // zero trailer (108 bytes). Do not write nested display cards here.
    if (object.ProductID == 1161 && !object.Revoked && object.Status >= 1 && object.Status <= 4)
    {
        static constexpr uint8 L80ProductNest[] =
        {
            0x80, 0x89, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x54, 0x06, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        static_assert(sizeof(L80ProductNest) == 56);

        ByteBuffer mid;
        if (!object.TargetPlayer.IsEmpty())
        {
            mid << object.TargetPlayer;
            mid << uint32(object.TargetVirtualRealm);
            mid << uint32(object.TargetNativeRealm);
        }

        data << uint64(object.DistributionID);
        data << uint32(object.Status);
        data << uint32(object.ProductID);
        data << uint64(object.PurchaseID);
        uint8 midBytes[21] = {};
        if (mid.size())
            memcpy(midBytes, mid.data(), std::min(mid.size(), sizeof(midBytes)));
        data.append(midBytes, sizeof(midBytes));
        data.append(L80ProductNest, sizeof(L80ProductNest));
        if (object.Status >= 2 && object.Status <= 4)
        {
            uint8 const assignTrailer[7] = {};
            data.append(assignTrailer, sizeof(assignTrailer));
        }
        return data;
    }

    data << uint64(object.DistributionID);
    data << uint32(object.Status);
    data << uint32(object.ProductID);
    data << uint64(object.PurchaseID);
    data << object.TargetPlayer;
    data << uint32(object.TargetVirtualRealm);
    data << uint32(object.TargetNativeRealm);

    data << OptionalInit(object.Product);
    data << Bits<1>(object.Revoked);
    data.FlushBits();

    if (object.Product)
        data << *object.Product;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayPurchase const& purchase)
{
    using namespace WorldPackets;

    data << uint64(purchase.PurchaseID);
    data << uint32(purchase.Status);
    data << uint32(purchase.ResultCode);
    data << uint32(purchase.ProductID);
    data << uint64(purchase.UnkLong);
    data << uint64(purchase.UnkLong2);
    data << uint32(purchase.UnkInt);
    data << SizedString::BitsSize<8>(purchase.WalletName);
    data.FlushBits();
    data << SizedString::Data(purchase.WalletName);
    return data;
}

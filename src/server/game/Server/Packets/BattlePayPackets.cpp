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
#include <limits>

namespace
{
void CheckBattlePaySize(std::size_t size, std::size_t maximum, char const* field)
{
    if (size > maximum)
        throw ByteBufferInvalidValueException(field, std::to_string(size));
}
}

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

void ConfirmPurchaseResponse::Read()
{
    ConfirmPurchase = _worldPacket.ReadBit();
    _worldPacket >> ServerToken;
    _worldPacket >> ClientCurrentPriceFixedPoint;
}

WorldPacket const* ProductListResponse::Write()
{
    CheckBattlePaySize(ProductInfo.size(), std::numeric_limits<int32>::max(), "BattlePay product info count");
    CheckBattlePaySize(Products.size(), std::numeric_limits<int32>::max(), "BattlePay product count");
    CheckBattlePaySize(ProductGroups.size(), std::numeric_limits<int32>::max(), "BattlePay group count");
    CheckBattlePaySize(Shop.size(), std::numeric_limits<int32>::max(), "BattlePay shop entry count");
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
        CheckBattlePaySize(group.Name.size(), 255, "BattlePay group name length");
        CheckBattlePaySize(group.IsAvailableDescription.size(), 0xFFFFFE, "BattlePay group description length");
        if (group.IsAvailableDescription.find('\0') != std::string::npos)
            throw ByteBufferInvalidValueException("BattlePay group description", "embedded terminator");
        _worldPacket << uint32(group.GroupID);
        _worldPacket << uint32(group.IconFileDataID);
        _worldPacket << uint8(group.DisplayType);
        _worldPacket << uint32(group.Ordering);
        _worldPacket << uint32(group.UnkInt);
        _worldPacket << uint32(group.UnkInt2);
        _worldPacket << SizedString::BitsSize<8>(group.Name);
        _worldPacket << Bits<24>(group.IsAvailableDescription.empty() ? 0 : uint32(group.IsAvailableDescription.size() + 1));
        _worldPacket.FlushBits();
        _worldPacket << SizedString::Data(group.Name);
        _worldPacket << SizedCString::Data(group.IsAvailableDescription);
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
    CheckBattlePaySize(Purchases.size(), std::numeric_limits<int32>::max(), "BattlePay purchase count");
    _worldPacket << uint32(Result);
    _worldPacket << Size<uint32>(Purchases);
    for (BattlePayPurchase const& purchase : Purchases)
        _worldPacket << purchase;
    return &_worldPacket;
}

WorldPacket const* DistributionListResponse::Write()
{
    CheckBattlePaySize(Distributions.size(), 0x7FF, "BattlePay distribution count");
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
    CheckBattlePaySize(Purchases.size(), std::numeric_limits<int32>::max(), "BattlePay purchase update count");
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
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductDisplayInfo const& displayInfo)
{
    using namespace WorldPackets;

    // These limits include room for the terminators in the client's fixed buffers.
    CheckBattlePaySize(displayInfo.Name1.size(), 512, "BattlePay display name 1 length");
    CheckBattlePaySize(displayInfo.Name2.size(), 512, "BattlePay display name 2 length");
    CheckBattlePaySize(displayInfo.Name3.size(), 4096, "BattlePay display name 3 length");
    CheckBattlePaySize(displayInfo.Name4.size(), 4096, "BattlePay display name 4 length");
    CheckBattlePaySize(displayInfo.Name5.size(), 4096, "BattlePay display name 5 length");
    CheckBattlePaySize(displayInfo.Name6.size(), 4096, "BattlePay display name 6 length");
    CheckBattlePaySize(displayInfo.Name7.size(), 4000, "BattlePay display name 7 length");
    CheckBattlePaySize(displayInfo.DisplayCards.size(), std::numeric_limits<int32>::max(), "BattlePay display card count");

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
    data << SizedString::BitsSize<13>(displayInfo.Name6);
    data << SizedString::BitsSize<12>(displayInfo.Name7);
    data.FlushBits();

    data << Size<uint32>(displayInfo.DisplayCards);
    data << uint32(displayInfo.UnkInt1);
    data << uint32(displayInfo.UnkInt2);
    data << uint32(displayInfo.UnkInt3);

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

    data << SizedString::Data(displayInfo.Name6);
    data << SizedString::Data(displayInfo.Name7);
    for (WorldPackets::BattlePay::ProductDisplayCard const& card : displayInfo.DisplayCards)
        data << card;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductDisplayCard const& card)
{
    using namespace WorldPackets;

    CheckBattlePaySize(card.Name.size(), 512, "BattlePay display card name length");
    data << SizedString::BitsSize<10>(card.Name);
    data.FlushBits();
    data << uint32(card.UnkInt1);
    data << uint32(card.UnkInt2);
    data << uint32(card.UnkInt3);
    data << SizedString::Data(card.Name);
    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductItem const& item)
{
    using namespace WorldPackets;

    if (item.PetResult)
        CheckBattlePaySize(*item.PetResult, 15, "BattlePay pet result");
    data << uint32(item.ID);
    data << uint32(item.ItemID);
    data << uint32(item.Quantity);
    data << uint32(item.UnkInt1);
    data << uint32(item.UnkInt2);
    data << uint32(item.UnkInt3);
    data << Bits<1>(item.HasPet);
    data << OptionalInit(item.PetResult);
    data << OptionalInit(item.DisplayInfo);
    if (item.PetResult)
        data << Bits<4>(*item.PetResult);
    data.FlushBits();
    if (item.DisplayInfo)
        data << *item.DisplayInfo;
    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayProduct const& product)
{
    using namespace WorldPackets;

    CheckBattlePaySize(product.UnkString.size(), 255, "BattlePay product string length");
    CheckBattlePaySize(product.Items.size(), 127, "BattlePay product item count");
    if (product.UnkBits)
        CheckBattlePaySize(*product.UnkBits, 15, "BattlePay product bits");
    data << uint32(product.ProductID);
    data << uint32(product.Type);
    data << uint32(product.Flags);
    data << uint32(product.UnkInt1);
    data << uint32(product.DisplayId);
    data << uint32(product.ItemId);
    data << uint32(product.UnkInt4);
    data << uint32(product.UnkInt5);
    data << uint32(product.UnkInt6);
    data << uint32(product.UnkInt7);
    data << uint32(product.UnkInt8);
    data << uint32(product.UnkInt9);
    data << uint32(product.UnkInt10);

    data << SizedString::BitsSize<8>(product.UnkString);
    data << Bits<1>(product.UnkBit);
    data << OptionalInit(product.UnkBits);
    data << BitsSize<7>(product.Items);
    data << OptionalInit(product.DisplayInfo);
    if (product.UnkBits)
        data << Bits<4>(*product.UnkBits);
    data.FlushBits();
    data << SizedString::Data(product.UnkString);

    for (WorldPackets::BattlePay::ProductItem const& item : product.Items)
        data << item;
    if (product.DisplayInfo)
        data << *product.DisplayInfo;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::ProductInfoStruct const& info)
{
    using namespace WorldPackets;

    CheckBattlePaySize(info.ProductIDs.size(), std::numeric_limits<int32>::max(), "BattlePay product ID count");
    CheckBattlePaySize(info.UnkInts.size(), std::numeric_limits<int32>::max(), "BattlePay product info integer count");
    data << uint32(info.ProductID);
    data << uint64(info.NormalPriceFixedPoint);
    data << uint64(info.CurrentPriceFixedPoint);
    data << Size<uint32>(info.ProductIDs);
    data << uint32(info.UnkInt2);
    data << uint32(info.UnkInt3);
    data << uint32(info.UnkInt4);
    data << Size<uint32>(info.UnkInts);
    data << uint32(info.UnkInt5);
    data << uint64(info.UnkLong);

    for (uint32 productId : info.ProductIDs)
        data << uint32(productId);
    for (uint32 value : info.UnkInts)
        data << uint32(value);

    data << OptionalInit(info.DisplayInfo);
    data.FlushBits();

    if (info.DisplayInfo)
        data << *info.DisplayInfo;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayDistributionObject const& object)
{
    using namespace WorldPackets;

    data << uint64(object.DistributionID);
    data << uint32(object.Status);
    data << uint32(object.ProductID);
    // Both GUIDs are packed, so the product offset changes with the account and character.
    data << object.AccountGUID;
    data << object.TargetPlayer;
    data << uint32(object.TargetVirtualRealm);
    data << uint32(object.TargetNativeRealm);
    data << uint64(object.PurchaseID);
    data << uint32(object.UnkInt);
    data << OptionalInit(object.Product);
    data << Bits<1>(object.Revoked);
    data.FlushBits();

    if (!object.Product)
        return data;

    // This product body matches the retail L80 boost. Assignment only changes the header.
    if (object.ProductID == 1161 && object.Status >= 1 && object.Status <= 4)
    {
        static constexpr uint8 L80ProductNest[] =
        {
            0x89, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x54, 0x06, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        static_assert(sizeof(L80ProductNest) == 55);
        data.append(L80ProductNest, sizeof(L80ProductNest));
    }
    else
        data << *object.Product;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::BattlePay::BattlePayPurchase const& purchase)
{
    using namespace WorldPackets;

    CheckBattlePaySize(purchase.WalletName.size(), 200, "BattlePay wallet name length");
    data << uint64(purchase.PurchaseID);
    data << uint32(purchase.Status);
    data << uint32(purchase.ResultCode);
    data << uint32(purchase.ProductID);
    data << uint64(purchase.UnkLong);
    data << uint64(purchase.UnkLong2);
    data << uint64(purchase.UnkLong3);
    data << SizedString::BitsSize<8>(purchase.WalletName);
    data.FlushBits();
    data << SizedString::Data(purchase.WalletName);
    return data;
}

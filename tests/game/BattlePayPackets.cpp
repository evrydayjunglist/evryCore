/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "BattlePayPackets.h"

namespace
{
void CheckDistribution(ByteBuffer& packet, WorldPackets::BattlePay::BattlePayDistributionObject const& expected)
{
    CHECK(packet.read<uint64>() == expected.DistributionID);
    CHECK(packet.read<uint32>() == expected.Status);
    CHECK(packet.read<uint32>() == expected.ProductID);

    // The client reads two packed GUIDs before the realm addresses and purchase ID.
    ObjectGuid account;
    ObjectGuid character;
    packet >> account >> character;
    CHECK(account == expected.AccountGUID);
    CHECK(character == expected.TargetPlayer);
    CHECK(packet.read<uint32>() == expected.TargetVirtualRealm);
    CHECK(packet.read<uint32>() == expected.TargetNativeRealm);
    CHECK(packet.read<uint64>() == expected.PurchaseID);
    CHECK(packet.read<uint32>() == expected.UnkInt);
    REQUIRE(packet.ReadBit());
    CHECK_FALSE(packet.ReadBit());
    packet.ResetBitPos();

    // The unchanged L80 product body is 13 integers, an empty string and two flag bytes.
    CHECK(packet.read<uint32>() == 1161);
    CHECK(packet.read<uint32>() == 1);
    for (uint32 i = 0; i < 4; ++i)
        CHECK(packet.read<uint32>() == 0);
    CHECK(packet.read<uint32>() == 11);
    CHECK(packet.read<uint32>() == 1620);
    for (uint32 i = 0; i < 5; ++i)
        CHECK(packet.read<uint32>() == 0);
    for (uint32 i = 0; i < 3; ++i)
        CHECK(packet.read<uint8>() == 0);
}
}

TEST_CASE("Boost distributions retain their product with variable length account and character GUIDs", "[packets][battlepay]")
{
    uint64 const accountId = GENERATE(2, 0x12345678);
    uint64 const characterId = GENERATE(3, 0x102030405);
    uint32 const status = GENERATE(1, 2, 3, 4);
    CAPTURE(accountId, characterId, status);

    WorldPackets::BattlePay::DistributionUpdate update;
    auto& distribution = update.Distribution;
    distribution.DistributionID = 0x200000001;
    distribution.Status = status;
    distribution.ProductID = 1161;
    distribution.AccountGUID = ObjectGuid::Create<HighGuid::WowAccount>(accountId);
    distribution.PurchaseID = 0x240000001;
    distribution.Product.emplace();
    if (status != 1)
    {
        distribution.TargetPlayer.SetRawValue((uint64(HighGuid::Player) << 58) | (uint64(1) << 42), characterId);
        distribution.TargetVirtualRealm = 0x01030019;
        distribution.TargetNativeRealm = 0x0103001F;
    }

    ByteBuffer packet(*update.Write());
    CheckDistribution(packet, distribution);
    CHECK(packet.rpos() == packet.size());

    // A wrong object boundary also corrupts the following distribution in a list.
    WorldPackets::BattlePay::DistributionListResponse response;
    response.Distributions.push_back(distribution);
    distribution.DistributionID++;
    distribution.AccountGUID = ObjectGuid::Create<HighGuid::WowAccount>(0x10203);
    response.Distributions.push_back(distribution);
    ByteBuffer list(*response.Write());
    CHECK(list.read<uint32>() == 0);
    REQUIRE(list.ReadBits(11) == 2);
    list.ResetBitPos();
    for (auto const& expected : response.Distributions)
        CheckDistribution(list, expected);
    CHECK(list.rpos() == list.size());
}

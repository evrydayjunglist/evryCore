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

#include "tc_catch2.h"
#include "HousingBlueprintPackets.h"
#include "HousingPackets.h"
#include "PacketOperators.h"
#include <limits>
#include <string>
#include <vector>

using namespace WorldPackets::Housing;

TEST_CASE("Housing array counts reject absent elements before allocation", "[Housing][Packets]")
{
    SECTION("A count larger than the bytes left is refused and nothing is allocated")
    {
        ByteBuffer data;
        data << std::numeric_limits<uint32>::max();
        std::vector<uint32> values;
        REQUIRE_THROWS_AS(data >> WorldPackets::BoundedSize<uint32>(values), ByteBufferPositionException);
        REQUIRE(values.empty());
    }

    SECTION("The minimum element size is counted against the bytes left")
    {
        ByteBuffer data;
        data << uint32(2) << uint32(7);
        std::vector<uint32> values;
        REQUIRE_THROWS_AS((data >> WorldPackets::BoundedSize<uint32, sizeof(uint32)>(values)), ByteBufferPositionException);
        REQUIRE(values.empty());
    }

    SECTION("A count that fits is kept whole")
    {
        ByteBuffer data;
        data << uint32(2) << uint32(7) << uint32(8);
        std::vector<uint32> values;
        data >> WorldPackets::BoundedSize<uint32, sizeof(uint32)>(values);
        REQUIRE(values.size() == 2);
    }

    SECTION("A room theme packet with an impossible option count is refused")
    {
        WorldPacket wire(CMSG_HOUSING_ROOM_SET_COMPONENT_THEME);
        wire << ObjectGuid::Empty << std::numeric_limits<uint32>::max() << uint32(8);
        HousingRoomSetComponentTheme packet(std::move(wire));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferPositionException);
        REQUIRE(packet.OptionIDs.empty());
    }
}

TEST_CASE("Housing material packets keep the slot before the option array", "[Housing][Packets]")
{
    WorldPacket wire(CMSG_HOUSING_ROOM_APPLY_COMPONENT_MATERIALS);
    wire << ObjectGuid::Empty << uint32(2) << int32(-1) << uint32(40) << uint8(3);
    wire << uint32(420) << uint32(421);
    HousingRoomApplyComponentMaterials packet(std::move(wire));
    packet.Read();

    REQUIRE(packet.ColorOverride == -1);
    REQUIRE(packet.RoomComponentTextureID == 40);
    REQUIRE(packet.ComponentSlot == 3);
    REQUIRE(packet.OptionIDs == std::vector<uint32>{ 420, 421 });
    REQUIRE(packet.GetRawPacket()->rpos() == packet.GetSize());

    WorldPacket truncated(CMSG_HOUSING_ROOM_APPLY_COMPONENT_MATERIALS);
    truncated << ObjectGuid::Empty << uint32(2) << int32(-1) << uint32(40) << uint8(3);
    truncated << uint32(420);
    HousingRoomApplyComponentMaterials incomplete(std::move(truncated));
    REQUIRE_THROWS_AS(incomplete.Read(), ByteBufferPositionException);
}

TEST_CASE("Housing refund batches reject oversized and truncated selections", "[Housing][Packets]")
{
    SECTION("An oversized selection is rejected instead of refunding its first part")
    {
        WorldPacket wire(CMSG_BULK_REFUND);
        wire << uint32(501);
        for (uint32 i = 0; i < 501; ++i)
            wire << ObjectGuid::Empty;
        BulkRefund packet(std::move(wire));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferInvalidValueException);
        REQUIRE(packet.DecorGUIDs.empty());
    }

    SECTION("A truncated GUID list is rejected before allocation")
    {
        WorldPacket wire(CMSG_BULK_REFUND);
        wire << uint32(2) << ObjectGuid::Empty;
        BulkRefund packet(std::move(wire));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferPositionException);
        REQUIRE(packet.DecorGUIDs.empty());
    }

    SECTION("A complete small selection is accepted")
    {
        WorldPacket wire(CMSG_BULK_REFUND);
        wire << uint32(2) << ObjectGuid::Empty << ObjectGuid::Empty;
        BulkRefund packet(std::move(wire));
        packet.Read();
        REQUIRE(packet.DecorGUIDs.size() == 2);
        REQUIRE(packet.GetRawPacket()->rpos() == packet.GetSize());
    }
}

namespace
{
WorldPacket MakeBlueprintImport(std::string const& uuid, uint8 terminator = 0)
{
    WorldPacket wire(CMSG_HOUSING_BLUEPRINT_IMPORT);
    wire.WriteBits(uint32(uuid.size() + 1), 24);
    wire.WriteBit(true);
    wire.FlushBits();
    wire << uint8(2) << ObjectGuid::Empty << uint32(196);
    wire.WriteString(uuid);
    wire << terminator;
    return wire;
}
}

TEST_CASE("Housing blueprint UUID parsing respects byte lengths and terminators", "[Housing][Packets]")
{
    std::string const uuid = "12345678-1234-5678-9abc-123456789abc";

    SECTION("A UUID follows the flag, type, room and door fields")
    {
        HousingBlueprintImport packet(MakeBlueprintImport(uuid));
        packet.Read();
        REQUIRE(packet.Uuid == uuid);
        REQUIRE(packet.Flag);
        REQUIRE(packet.BlueprintType == 2);
        REQUIRE(packet.SourceRoomGuid.IsEmpty());
        REQUIRE(packet.TargetDoorComponentID == 196);
        REQUIRE(packet.GetRawPacket()->rpos() == packet.GetSize());
    }

    SECTION("An impossible advertised length is rejected without reserving a large string")
    {
        WorldPacket wire(CMSG_HOUSING_BLUEPRINT_IMPORT);
        wire.WriteBits(0xFFFFFF, 24);
        wire.FlushBits();
        HousingBlueprintImport packet(std::move(wire));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferInvalidValueException);
        REQUIRE(packet.Uuid.empty());
    }

    SECTION("A UUID longer than 36 characters is rejected")
    {
        HousingBlueprintImport packet(MakeBlueprintImport(uuid + "0"));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferInvalidValueException);
        REQUIRE(packet.Uuid.empty());
    }

    SECTION("A nonzero terminator is rejected")
    {
        HousingBlueprintImport packet(MakeBlueprintImport(uuid, 1));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferInvalidValueException);
    }

    SECTION("An embedded terminator is rejected")
    {
        std::string embedded = uuid;
        embedded[4] = '\0';
        HousingBlueprintImport packet(MakeBlueprintImport(embedded));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferInvalidValueException);
    }

    SECTION("A missing terminator is rejected")
    {
        WorldPacket wire = MakeBlueprintImport(uuid);
        wire.resize(wire.size() - 1);
        HousingBlueprintImport packet(std::move(wire));
        REQUIRE_THROWS_AS(packet.Read(), ByteBufferPositionException);
    }
}

TEST_CASE("Housing blueprint empty strings consume only their encoded bytes", "[Housing][Packets]")
{
    for (uint32 length : { 0u, 1u })
    {
        CAPTURE(length);
        WorldPacket wire(CMSG_HOUSING_BLUEPRINT_RENAME);
        wire << uint64(123);
        wire.WriteBits(length, 6);
        wire.FlushBits();
        if (length)
            wire << uint8(0);
        HousingBlueprintRename packet(std::move(wire));
        packet.Read();
        REQUIRE(packet.BlueprintID == 123);
        REQUIRE(packet.Name.empty());
        REQUIRE(packet.GetRawPacket()->rpos() == packet.GetSize());
    }
}

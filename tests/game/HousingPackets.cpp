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
#include <cstring>
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

namespace
{
std::vector<uint8> Bytes(WorldPacket const* packet)
{
    return std::vector<uint8>(packet->data(), packet->data() + packet->size());
}

ObjectGuid Guid(uint64 high, uint64 low)
{
    ObjectGuid guid;
    guid.SetRawValue(high, low);
    return guid;
}

// The GUIDs of the owner's retail session (hbcd3): her house, her Battle.net account, her character and her
// neighborhood on Razorwind Shores.
ObjectGuid const RetailHouse = Guid(UI64LIT(0xDC60000000008007), UI64LIT(0x000000000354769D));
ObjectGuid const RetailBnetAccount = Guid(UI64LIT(0x7800000000000000), UI64LIT(0x000000000354769D));
ObjectGuid const RetailCharacter = Guid(UI64LIT(0x0802880000000000), UI64LIT(0x000000000BE2FE88));
ObjectGuid const RetailNeighborhood = Guid(UI64LIT(0xDC80000200000000), UI64LIT(0x000000000000A584));

JamCliHouse RetailHouseEntry()
{
    JamCliHouse house;
    house.HouseGUID = RetailHouse;
    house.CosmeticOwnerGUID = RetailCharacter;
    house.NeighborhoodGUID = RetailNeighborhood;
    house.PlotID = 13;
    house.HouseSettingFlags = 32;
    return house;
}
}

TEST_CASE("Housing house entries are written in retail's layout", "[Housing][Packets]")
{
    SECTION("The buy reply matches hbcd3 1299763 byte for byte")
    {
        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.House = RetailHouseEntry();
        response.Result = 0;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0x0F, 0xE0, 0x88, 0xFE, 0xE2, 0x0B, 0x88, 0x02, 0x08,
            0x03, 0xD0, 0x84, 0xA5, 0x02, 0x80, 0xDC,
            0x0D, 0x20, 0x00, 0x00, 0x00, 0x00,
            0x00 });
    }

    SECTION("The houses info reply matches the 12.1.0.69382 capture line 160347")
    {
        HousingSvcsGetPlayerHousesInfoResponse response;
        response.Houses.push_back(RetailHouseEntry());
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x01, 0x00, 0x00, 0x00, 0x00,
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0x0F, 0xE0, 0x88, 0xFE, 0xE2, 0x0B, 0x88, 0x02, 0x08,
            0x03, 0xD0, 0x84, 0xA5, 0x02, 0x80, 0xDC,
            0x0D, 0x20, 0x00, 0x00, 0x00, 0x00 });
    }

    SECTION("An empty houses info reply is five bytes (hbcd3 218452)")
    {
        HousingSvcsGetPlayerHousesInfoResponse response;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{ 0x00, 0x00, 0x00, 0x00, 0x00 });
    }

    SECTION("A time follows the entry only when one is set")
    {
        JamCliHouse house;
        house.PlotID = 1;
        house.ReservationTime = 0x6A64F4C8;
        HousingGetCurrentHouseInfoResponse response;
        response.House = house;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00,
            0x80, 0xC8, 0xF4, 0x64, 0x6A, 0x00, 0x00, 0x00, 0x00,
            0x00 });
    }

    SECTION("The house settings reply is the result and a house entry")
    {
        // A 12.0.5 sample quoted by the imported code (31 bytes): plot 41, setting flags 32.
        HousingSvcsUpdateHouseSettingsResponse response;
        response.House.HouseGUID = Guid(UI64LIT(0xDC60000000008007), UI64LIT(0x000000000015310B));
        response.House.CosmeticOwnerGUID = Guid(UI64LIT(0x0800D40000000000), UI64LIT(0x000000000C610517));
        response.House.NeighborhoodGUID = Guid(UI64LIT(0xDC80000100000000), UI64LIT(0x0000000000006CF0));
        response.House.PlotID = 41;
        response.House.HouseSettingFlags = 32;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x00,
            0x07, 0xC3, 0x0B, 0x31, 0x15, 0x07, 0x80, 0x60, 0xDC,
            0x0F, 0xA0, 0x17, 0x05, 0x61, 0x0C, 0xD4, 0x08,
            0x03, 0xD0, 0xF0, 0x6C, 0x01, 0x80, 0xDC,
            0x29, 0x20, 0x00, 0x00, 0x00, 0x00 });
    }
}

TEST_CASE("Housing house status replies carry the locked decor and three edit-mode bits", "[Housing][Packets]")
{
    SECTION("On the plot the owner character is sent (hbcd3 1340681, 30 bytes)")
    {
        HousingHouseStatusResponse response;
        response.HouseGuid = RetailHouse;
        response.AccountGuid = RetailBnetAccount;
        response.OwnerPlayerGuid = RetailCharacter;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0x0F, 0x80, 0x9D, 0x76, 0x54, 0x03, 0x78,
            0x0F, 0xE0, 0x88, 0xFE, 0xE2, 0x0B, 0x88, 0x02, 0x08,
            0x00, 0x00,
            0x00,
            0x00 });
    }

    SECTION("Inside the interior the owner character is empty (hbcd3 1416647, 23 bytes)")
    {
        HousingHouseStatusResponse response;
        response.HouseGuid = RetailHouse;
        response.AccountGuid = RetailBnetAccount;
        REQUIRE(response.Write()->size() == 23);
    }

    SECTION("The edit modes are the top three bits of the last byte, decor first")
    {
        HousingHouseStatusResponse response;
        response.DecorEditModeEnabled = true;
        response.FixtureEditModeEnabled = true;
        std::vector<uint8> const bytes = Bytes(response.Write());
        REQUIRE(bytes.size() == 10);
        REQUIRE(bytes.back() == 0xA0);
    }
}

TEST_CASE("Housing storage replies answer success with new data pulled", "[Housing][Packets]")
{
    // hbcd3 351312, before the character owned a house: 00 00 00 80.
    HousingDecorRequestStorageResponse response;
    REQUIRE(Bytes(response.Write()) == std::vector<uint8>{ 0x00, 0x00, 0x00, 0x80 });

    // hbcd3 351306: the request carries the Battle.net account.
    WorldPacket wire(CMSG_HOUSING_DECOR_REQUEST_STORAGE);
    wire << RetailBnetAccount;
    HousingDecorRequestStorage request(std::move(wire));
    request.Read();
    REQUIRE(request.BnetAccountGuid == RetailBnetAccount);
}

TEST_CASE("Housing cornerstone replies match both retail samples", "[Housing][Packets]")
{
    SECTION("A vacant plot (hbcd3 1296244, 29 bytes)")
    {
        WorldPackets::Neighborhood::NeighborhoodOpenCornerstoneUIResponse response;
        response.PlotIndex = 13;
        response.NeighborhoodName = "94-2-87";
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x0D, 0x00, 0x00, 0x00,
            0x00, 0x00,
            0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00,
            0x00, 0x00,
            0x04, 0x00,
            '9', '4', '-', '2', '-', '8', '7', 0x00 });
    }

    SECTION("The player's own plot (hbcd3 1563345, 44 bytes)")
    {
        WorldPackets::Neighborhood::NeighborhoodOpenCornerstoneUIResponse response;
        response.PlotIndex = 13;
        response.PlotOwnerGuid = RetailCharacter;
        response.HouseGuid = RetailHouse;
        response.NeighborhoodName = "94-2-87";
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x0D, 0x00, 0x00, 0x00,
            0x0F, 0xE0, 0x88, 0xFE, 0xE2, 0x0B, 0x88, 0x02, 0x08,
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00,
            0x00, 0x00,
            0x04, 0x00,
            '9', '4', '-', '2', '-', '8', '7', 0x00 });
    }

    SECTION("The request names the cornerstone after the plot (hbcd3 1295070)")
    {
        ObjectGuid const cornerstone = Guid(UI64LIT(0x2C3CAD5601BE6D80), UI64LIT(0x0158580006E4ED8C));
        WorldPacket wire(CMSG_NEIGHBORHOOD_OPEN_CORNERSTONE_UI);
        wire << uint32(13) << cornerstone;
        WorldPackets::Neighborhood::NeighborhoodOpenCornerstoneUI request(std::move(wire));
        request.Read();
        REQUIRE(request.PlotIndex == 13);
        REQUIRE(request.CornerstoneGuid == cornerstone);
    }
}

TEST_CASE("Housing neighborhood name replies match the 12.1 capture", "[Housing][Packets]")
{
    // 12.1.0.69382 capture line 160391: 03 D0 84 A5 02 80 DC 80 07 '94-2-87'.
    QueryNeighborhoodNameResponse response;
    response.NeighborhoodGuid = RetailNeighborhood;
    response.Result = true;
    response.NeighborhoodName = "94-2-87";
    REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
        0x03, 0xD0, 0x84, 0xA5, 0x02, 0x80, 0xDC, 0x80, 0x07, '9', '4', '-', '2', '-', '8', '7' });
}

TEST_CASE("Housing initiative packets match retail", "[Housing][Packets]")
{
    SECTION("An empty request is answered with an empty GUID and no data (hbcd3 290809 and 293257)")
    {
        WorldPacket wire(CMSG_GET_PLAYER_INITIATIVE_INFO_REQUEST);
        wire << ObjectGuid::Empty;
        WorldPackets::Neighborhood::GetPlayerInitiativeInfoRequest request(std::move(wire));
        request.Read();
        REQUIRE(request.NeighborhoodGuid.IsEmpty());
        REQUIRE(request.GetRawPacket()->rpos() == request.GetSize());

        GetPlayerInitiativeInfoResult result;
        result.NeighborhoodGUID = request.NeighborhoodGuid;
        REQUIRE(Bytes(result.Write()) == std::vector<uint8>{ 0x00, 0x00, 0x00 });
    }

    SECTION("An activity log entry matches hbcd3 1305730 byte for byte")
    {
        uint32 const contributionBits = 0x400CF19E;
        float contribution = 0.0f;
        std::memcpy(&contribution, &contributionBits, sizeof(contribution));

        NICompletedTasksEntry entry;
        entry.BnetAccountGuid = Guid(UI64LIT(0x7800000000000000), UI64LIT(0x0000000004520ED5));
        entry.PlayerGuid = Guid(UI64LIT(0x08028C0000000000), UI64LIT(0x000000000C201282));
        entry.TaskID = 168;
        entry.CompletionTime = 0x6A64F4C8;
        entry.ContributionAmount = contribution;

        GetInitiativeActivityLogResult result;
        result.NeighborhoodGuid = RetailNeighborhood;
        result.CompletedTasks.push_back(entry);
        REQUIRE(Bytes(result.Write()) == std::vector<uint8>{
            0x03, 0xD0, 0x84, 0xA5, 0x02, 0x80, 0xDC,
            0x01, 0x00, 0x00, 0x00,
            0x0F, 0x80, 0xD5, 0x0E, 0x52, 0x04, 0x78,
            0x0F, 0xE0, 0x82, 0x12, 0x20, 0x0C, 0x8C, 0x02, 0x08,
            0xA8, 0x00, 0x00, 0x00,
            0xC8, 0xF4, 0x64, 0x6A, 0x00, 0x00, 0x00, 0x00,
            0x9E, 0xF1, 0x0C, 0x40 });
    }
}

namespace
{
WorldPacket Wire(OpcodeClient opcode, std::vector<uint8> const& bytes)
{
    WorldPacket wire(opcode);
    wire.append(bytes.data(), bytes.size());
    return wire;
}
}

TEST_CASE("Housing exterior edit packets are read in retail's layout", "[Housing][Packets]")
{
    SECTION("A house drag reads the house, the account and the root's pose in its room (hled1 645894)")
    {
        HouseExteriorCommitPosition packet(Wire(CMSG_HOUSE_EXTERIOR_SET_HOUSE_POSITION, {
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0x0F, 0x80, 0x9D, 0x76, 0x54, 0x03, 0x78,
            0x80, 0xA8, 0xEF, 0xC0, 0x00, 0x7A, 0xE2, 0x3F, 0x80, 0x58, 0xA8, 0x3C, 0xC0, 0x0F, 0xC9, 0x3F }));
        packet.Read();

        REQUIRE(packet.HouseGuid == RetailHouse);
        REQUIRE(packet.BnetAccountGuid == RetailBnetAccount);
        REQUIRE(packet.PositionX == Catch::Approx(-7.4893188f));
        REQUIRE(packet.PositionY == Catch::Approx(1.7693481f));
        REQUIRE(packet.PositionZ == Catch::Approx(0.0205500f));
        REQUIRE(packet.Facing == Catch::Approx(1.5707932f));
        REQUIRE(packet.GetRawPacket()->rpos() == packet.GetSize());
    }

    SECTION("An exterior lock reads the house, the owner's account, the character and the lock bit (hled1 645300, 645903)")
    {
        std::vector<uint8> bytes = {
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0x0F, 0x80, 0x9D, 0x76, 0x54, 0x03, 0x78,
            0x0F, 0xE0, 0x88, 0xFE, 0xE2, 0x0B, 0x88, 0x02, 0x08,
            0x80 };
        HouseExteriorLock lock(Wire(CMSG_HOUSE_EXTERIOR_LOCK, bytes));
        lock.Read();
        REQUIRE(lock.HouseGuid == RetailHouse);
        REQUIRE(lock.HouseOwnerAccountGuid == RetailBnetAccount);
        REQUIRE(lock.PlayerGuid == RetailCharacter);
        REQUIRE(lock.Locked);
        REQUIRE(lock.GetRawPacket()->rpos() == lock.GetSize());

        bytes.back() = 0x00;
        HouseExteriorLock unlock(Wire(CMSG_HOUSE_EXTERIOR_LOCK, bytes));
        unlock.Read();
        REQUIRE_FALSE(unlock.Locked);
    }

    SECTION("A fixture create reads the house, the piece owning the hook, the hook, the component and a byte (hled1 818926)")
    {
        HousingFixtureCreateFixture packet(Wire(CMSG_HOUSING_FIXTURE_CREATE_FIXTURE, {
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0xEF, 0xFE, 0xA7, 0x88, 0x65, 0x12, 0xEA, 0xC3, 0x01, 0xDB, 0x5C, 0x19, 0x56, 0xAD, 0x3C, 0xE0,
            0x71, 0x43, 0x00, 0x00, 0xCF, 0x03, 0x00, 0x00, 0x00 }));
        packet.Read();

        REQUIRE(packet.HouseGuid == RetailHouse);
        REQUIRE(packet.AttachParentGuid == Guid(UI64LIT(0xE03CAD56195CDB00), UI64LIT(0x01C3EA00126588A7)));
        REQUIRE(packet.ExteriorComponentHookID == 17265);
        REQUIRE(packet.ExteriorComponentID == 975);
        REQUIRE(packet.Flags == 0);
        REQUIRE(packet.GetRawPacket()->rpos() == packet.GetSize());
    }
}

TEST_CASE("Housing exterior edit replies are written in retail's layout", "[Housing][Packets]")
{
    SECTION("The lock reply names the house and the character (hled1 816934, 21 bytes)")
    {
        HouseExteriorLockResponse response;
        response.HouseGuid = RetailHouse;
        response.EditorPlayerGuid = RetailCharacter;
        response.Result = 0;
        response.Active = true;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0x0F, 0xE0, 0x88, 0xFE, 0xE2, 0x0B, 0x88, 0x02, 0x08,
            0x00, 0x80 });
    }

    SECTION("The house position reply is the result then the house (hled1 645911)")
    {
        HouseExteriorSetHousePositionResponse response;
        response.Result = 0;
        response.HouseGuid = RetailHouse;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x00, 0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC });
    }
}

TEST_CASE("Housing level and favor updates match retail byte for byte", "[Housing][Packets]")
{
    using HouseLevelFavor = HousingSvcsUpdateHousesLevelFavor::HouseLevelFavor;

    SECTION("After a purchase, the house's favor with change and reason -1 (hbcd3 1299772, Number 13869)")
    {
        HousingSvcsUpdateHousesLevelFavor update;
        update.Result = 0;
        update.ChangeAmount = uint32(-1);
        update.Reason = uint32(-1);
        HouseLevelFavor& house = update.Houses.emplace_back();
        house.HouseGUID = RetailHouse;
        house.HouseLevel = -1;
        house.FavorValue = 1080;
        REQUIRE(Bytes(update.Write()) == std::vector<uint8>{
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0xFF, 0xFF, 0xFF, 0xFF, 0x38, 0x04, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x80 });
    }

    SECTION("Then the favor as the change with reason 1, the house at -1 and -1 (hbcd3 1301305, Number 13925)")
    {
        HousingSvcsUpdateHousesLevelFavor update;
        update.Result = 0;
        update.ChangeAmount = 1080;
        update.Reason = 1;
        HouseLevelFavor& house = update.Houses.emplace_back();
        house.HouseGUID = RetailHouse;
        house.HouseLevel = -1;
        house.FavorValue = -1;
        REQUIRE(Bytes(update.Write()) == std::vector<uint8>{
            0x00, 0x38, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x0F, 0xC3, 0x9D, 0x76, 0x54, 0x03, 0x07, 0x80, 0x60, 0xDC,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x80 });
    }

    SECTION("A first acquisition bonus names only the Battle.net account (hbcd3 2106253, Number 25462)")
    {
        HousingSvcsUpdateHousesLevelFavor update;
        update.Result = 0;
        update.ChangeAmount = uint32(-1);
        update.Reason = uint32(-1);
        HouseLevelFavor& account = update.Houses.emplace_back();
        account.BnetAccount = RetailBnetAccount;
        account.HouseLevel = -1;
        account.FavorValue = 10;
        account.UpdateSource = 1;
        account.SourceDataDecorID = 1482;
        REQUIRE(Bytes(update.Write()) == std::vector<uint8>{
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00,
            0x0F, 0x80, 0x9D, 0x76, 0x54, 0x03, 0x78,
            0x00, 0x00, 0x00, 0x00,
            0xFF, 0xFF, 0xFF, 0xFF, 0x0A, 0x00, 0x00, 0x00,
            0x01, 0xCA, 0x05, 0x00, 0x00, 0x80 });
    }
}

TEST_CASE("Housing decor replies match retail", "[Housing][Packets]")
{
    SECTION("A first-time acquisition names the decor entry (hbcd3 1299364, Number 13844)")
    {
        HousingFirstTimeDecorAcquisition message;
        message.DecorEntryID = 1700;
        REQUIRE(Bytes(message.Write()) == std::vector<uint8>{ 0xA4, 0x06, 0x00, 0x00 });
    }

    SECTION("Add to chest is the success bit, a count and the pieces (hbcd3 1783648, Number 20365)")
    {
        HousingDecorAddToHouseChestResponse response;
        response.Success = true;
        response.DecorGuids.push_back(Guid(UI64LIT(0xDC2005A30000048B), UI64LIT(0x0000000091090083)));
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x80, 0x01, 0x00, 0x00, 0x00,
            0x0D, 0xF3, 0x83, 0x09, 0x91, 0x8B, 0x04, 0xA3, 0x05, 0x20, 0xDC });
    }

    // WowPacketParser decodes both redeem replies as the new piece's GUID, the result and the transaction id, and the
    // captured lengths (17 and 16 bytes) are what that layout gives for these two GUIDs.
    SECTION("A redeem answers the piece, the result and the transaction (hled1 788897, Number 18783, 17 bytes)")
    {
        HousingRedeemDeferredDecorResponse response;
        response.DecorGuid = Guid(UI64LIT(0xDC2005A30000020C), UI64LIT(0x00000000912C021A));
        response.Result = 0;
        response.SequenceIndex = 1;
        REQUIRE(Bytes(response.Write()) == std::vector<uint8>{
            0x0F, 0xF3, 0x1A, 0x02, 0x2C, 0x91, 0x0C, 0x02, 0xA3, 0x05, 0x20, 0xDC,
            0x00, 0x01, 0x00, 0x00, 0x00 });
    }

    SECTION("A redeem whose GUID packs shorter (hbcd3 1443667, Number 16642, 16 bytes)")
    {
        HousingRedeemDeferredDecorResponse response;
        response.DecorGuid = Guid(UI64LIT(0xDC2005A300002E9B), UI64LIT(0x000000009109007F));
        response.Result = 0;
        response.SequenceIndex = 1;
        REQUIRE(response.Write()->size() == 16);
    }
}

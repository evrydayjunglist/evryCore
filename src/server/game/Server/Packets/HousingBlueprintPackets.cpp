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

#include "HousingBlueprintPackets.h"
#include "PacketOperators.h"

namespace WorldPackets::Housing
{
namespace
{
// Reads the length of a client string, counted with its terminating zero. A
// length longer than the field allows, or longer than the bytes left in the
// packet, is refused before any memory is reserved for the string.
uint32 ReadCStringSize(WorldPacket& data, uint32 bits, uint32 maximumLength)
{
    uint32 length = data.ReadBits(bits);
    if (length > maximumLength + 1)
        throw ByteBufferInvalidValueException("housing blueprint string length", std::to_string(length));
    if (length > data.size() - data.rpos())
        data.OnInvalidPosition(data.rpos(), length);
    return length;
}

// Reads the string itself. A zero length means an empty string with no bytes.
// Otherwise the last byte must be the terminating zero and no other byte may be.
std::string ReadCString(WorldPacket& data, uint32 length)
{
    if (!length)
        return {};

    std::string result(data.ReadString(length - 1));
    if (data.read<char>() != '\0' || result.find('\0') != std::string::npos)
        throw ByteBufferInvalidValueException("housing blueprint string", "invalid terminator");
    return result;
}

// The client's senders write a zero length for an empty string (and no bytes); SizedCString::BitsSize would write 1.
void WriteCStringSize(WorldPacket& data, std::string const& value, uint32 bits)
{
    data.WriteBits(value.empty() ? 0 : uint32(value.size() + 1), bits);
}
}

// A blueprint name fills a 6-bit length, so at most 62 characters and the zero.
constexpr uint32 MaxBlueprintNameLength = 62;
// A blueprint UUID is written as 8-4-4-4-12 hex digits; HousingBlueprintMgr
// refuses any other length.
constexpr uint32 BlueprintUuidLength = 36;

void HousingBlueprintExport::Read()
{
    uint32 nameLength = ReadCStringSize(_worldPacket, 6, MaxBlueprintNameLength);
    _worldPacket.ResetBitPos();
    _worldPacket >> BlueprintType;
    _worldPacket >> RoomGuid;
    Name = ReadCString(_worldPacket, nameLength);
}

void HousingBlueprintRename::Read()
{
    _worldPacket >> BlueprintID;
    uint32 nameLength = ReadCStringSize(_worldPacket, 6, MaxBlueprintNameLength);
    _worldPacket.ResetBitPos();
    Name = ReadCString(_worldPacket, nameLength);
}

void HousingBlueprintDelete::Read()
{
    _worldPacket >> BlueprintID;
}

void HousingBlueprintImport::Read()
{
    uint32 uuidLength = ReadCStringSize(_worldPacket, 24, BlueprintUuidLength);
    _worldPacket >> Bits<1>(Flag);
    _worldPacket.ResetBitPos();
    _worldPacket >> BlueprintType;
    _worldPacket >> SourceRoomGuid;
    _worldPacket >> TargetDoorComponentID;
    Uuid = ReadCString(_worldPacket, uuidLength);
}

void HousingBlueprintRequestContents::Read()
{
    uint32 uuidLength = ReadCStringSize(_worldPacket, 24, BlueprintUuidLength);
    _worldPacket >> Bits<1>(Flag);
    _worldPacket.ResetBitPos();
    _worldPacket >> BlueprintType;
    _worldPacket >> TargetHouseGuid;
    Uuid = ReadCString(_worldPacket, uuidLength);
}

WorldPacket const* HousingBlueprintExportResponse::Write()
{
    _worldPacket << uint8(Result);
    _worldPacket << uint8(BlueprintType);
    WriteCStringSize(_worldPacket, Uuid, 24);
    _worldPacket.FlushBits();
    _worldPacket << SizedCString::Data(Uuid);

    return &_worldPacket;
}

WorldPacket const* HousingBlueprintCollection::Write()
{
    _worldPacket << uint8(Result);
    _worldPacket << uint32(Blueprints.size());
    for (JamHousingBlueprint const& blueprint : Blueprints)
    {
        _worldPacket << uint64(blueprint.ID);
        _worldPacket << uint8(blueprint.Type);
        _worldPacket << int64(blueprint.DateCreated);
        _worldPacket << int64(blueprint.DateDeleted);
        _worldPacket << uint8(blueprint.Flags);
        _worldPacket << uint8(blueprint.DeleteReason);
        WriteCStringSize(_worldPacket, blueprint.Uuid, 24);
        WriteCStringSize(_worldPacket, blueprint.Name, 24);
        _worldPacket.FlushBits();
        _worldPacket << SizedCString::Data(blueprint.Uuid);
        _worldPacket << SizedCString::Data(blueprint.Name);
    }

    return &_worldPacket;
}

WorldPacket const* HousingBlueprintRenameResponse::Write()
{
    _worldPacket << uint8(Result);
    _worldPacket << uint64(BlueprintID);
    WriteCStringSize(_worldPacket, Name, 6);
    _worldPacket.FlushBits();
    _worldPacket << SizedCString::Data(Name);

    return &_worldPacket;
}

WorldPacket const* HousingBlueprintDeleteResponse::Write()
{
    _worldPacket << uint8(Result);
    _worldPacket << uint64(BlueprintID);

    return &_worldPacket;
}

WorldPacket const* HousingBlueprintImportResponse::Write()
{
    _worldPacket << uint8(Result);
    _worldPacket << uint8(BlueprintType);
    _worldPacket << uint32(Unused);
    WriteCStringSize(_worldPacket, Uuid, 24);
    _worldPacket.FlushBits();
    _worldPacket << SizedCString::Data(Uuid);

    return &_worldPacket;
}

static void WriteContentLists(WorldPacket& data, JamBlueprintContentLists const& lists)
{
    // All four counts come first, then the four arrays.
    data << uint32(lists.Decor.size());
    data << uint32(lists.Dyes.size());
    data << uint32(lists.Rooms.size());
    data << uint32(lists.Fixtures.size());
    for (auto const& [id, count] : lists.Decor)
        data << uint32(id) << uint32(count);
    for (auto const& [id, count] : lists.Dyes)
        data << uint32(id) << uint32(count);
    for (uint32 id : lists.Rooms)
        data << uint32(id);
    for (uint32 id : lists.Fixtures)
        data << uint32(id);
}

static void WriteBudgetEntry(WorldPacket& data, JamBlueprintBudgetEntry const& entry)
{
    data << uint8(entry.BudgetType);
    data << int32(entry.Max);
    data << int32(entry.Current);
    data << int32(entry.Cost);
}

WorldPacket const* HousingBlueprintContents::Write()
{
    _worldPacket << uint8(Result);
    _worldPacket << uint8(BlueprintType);
    _worldPacket << TargetHouseGuid;
    _worldPacket << uint32(UnmetRequirementFlags);
    WriteContentLists(_worldPacket, Missing);
    WriteContentLists(_worldPacket, Invalid);
    _worldPacket << uint32(InteriorBudgets.size());
    _worldPacket << uint32(ExteriorBudgets.size());
    for (JamBlueprintBudgetEntry const& entry : InteriorBudgets)
        WriteBudgetEntry(_worldPacket, entry);
    for (JamBlueprintBudgetEntry const& entry : ExteriorBudgets)
        WriteBudgetEntry(_worldPacket, entry);
    _worldPacket << OptionalInit(Contents);
    _worldPacket.FlushBits();
    if (Contents)
        WriteContentLists(_worldPacket, *Contents);
    WriteCStringSize(_worldPacket, Uuid, 24);
    _worldPacket.FlushBits();
    _worldPacket << SizedCString::Data(Uuid);

    return &_worldPacket;
}

WorldPacket const* HousingHouseBudgetsUpdate::Write()
{
    // JamHouseBudgets: interiorBudgets@0, exteriorBudgets@24 (spec §2). HouseGuid is a
    // convenience prefix so the client can associate the update; framing is [INF].
    _worldPacket << HouseGuid;
    _worldPacket << uint32(InteriorBudgets.size());
    for (JamHouseBudgetEntry const& e : InteriorBudgets)
    {
        _worldPacket << uint32(e.BudgetType);
        _worldPacket << int32(e.Max);
        _worldPacket << int32(e.Current);
        _worldPacket << int32(e.Cost);
    }
    _worldPacket << uint32(ExteriorBudgets.size());
    for (JamHouseBudgetEntry const& e : ExteriorBudgets)
    {
        _worldPacket << uint32(e.BudgetType);
        _worldPacket << int32(e.Max);
        _worldPacket << int32(e.Current);
        _worldPacket << int32(e.Cost);
    }
    return &_worldPacket;
}
}

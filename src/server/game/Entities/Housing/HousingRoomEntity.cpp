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

#include "HousingRoomEntity.h"
#include "Log.h"
#include "Map.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "StringFormat.h"
#include "UpdateData.h"
#include "WorldSession.h"

HousingRoomEntity::HousingRoomEntity(HousingGridEntityRole role /*= HousingGridEntityRole::Room*/)
    : WorldObject(false), _role(role)
{
    m_objectTypeId = TYPEID_HOUSING_ENTITY; // 18 — retail objectType for housing entities

    // Object constructor adds CGObject (fragment 2) automatically. Retail's housing entities of object type 18 carry
    // none, so it is removed before the housing fragments are added.
    m_entityFragments.Remove(WowCS::EntityFragment::CGObject);

    switch (role)
    {
        case HousingGridEntityRole::Room:
            m_updateFlag.HasEntityPosition = true;
            m_updateFlag.Stationary = true;
            m_entityFragments.Add(WowCS::EntityFragment::FHousingRoom_C, false, WowCS::GetRawFragmentData(m_housingRoomData));
            m_entityFragments.Add(WowCS::EntityFragment::FMirroredPositionData_C, false, WowCS::GetRawFragmentData(m_mirroredPositionData));
            m_entityFragments.Add(WowCS::EntityFragment::Tag_HousingRoom, false);
            break;
        case HousingGridEntityRole::ExteriorRoot:
            m_updateFlag.HasEntityPosition = true;
            m_entityFragments.Add(WowCS::EntityFragment::FMirroredPositionData_C, false, WowCS::GetRawFragmentData(m_mirroredPositionData));
            m_entityFragments.Add(WowCS::EntityFragment::Tag_HouseExteriorPiece, false);
            m_entityFragments.Add(WowCS::EntityFragment::Tag_HouseExteriorRoot, false);
            break;
        case HousingGridEntityRole::AttachPoint:
            m_updateFlag.HasEntityPosition = true;
            m_entityFragments.Add(WowCS::EntityFragment::FMirroredPositionData_C, false, WowCS::GetRawFragmentData(m_mirroredPositionData));
            break;
        case HousingGridEntityRole::House:
            m_entityFragments.Add(WowCS::EntityFragment::FHousingPlayerHouse_C, false, WowCS::GetRawFragmentData(m_housingPlayerHouseData));
            break;
    }
}

bool HousingRoomEntity::Create(ObjectGuid guid, Map* map, Position const& pos, bool addToMap /*= true*/)
{
    _Create(guid);
    SetMap(map);
    Relocate(pos);
    SetObjectScale(1.0f);

    if (addToMap && !GetMap()->AddToMap(this))
        return false;

    // A room carries the plot's geobox through its component mesh, and the client's OutsidePlotBounds and IsInsidePlot
    // checks walk the room registry, so a room streams to every player on the map. The other roles follow the grid
    // like the house's meshes.
    if (_role == HousingGridEntityRole::Room)
    {
        setActive(true);
        SetFarVisible(true);
    }

    return true;
}

bool HousingRoomEntity::IsNeverVisibleFor(WorldObject const* seer, bool allowServersideObjects) const
{
    if (WorldObject::IsNeverVisibleFor(seer, allowServersideObjects))
        return true;

    if (_role == HousingGridEntityRole::House)
        if (Player const* player = seer->ToPlayer())
            if (WorldSession const* session = player->GetSession())
                return session->GetBattlenetAccountGUID() == *m_housingPlayerHouseData->BnetAccount;

    return false;
}

void HousingRoomEntity::AddToWorld()
{
    if (!IsInWorld())
    {
        GetMap()->GetObjectsStore().Insert<HousingRoomEntity>(this);
        WorldObject::AddToWorld();
    }
}

void HousingRoomEntity::RemoveFromWorld()
{
    if (IsInWorld())
    {
        WorldObject::RemoveFromWorld();
        GetMap()->GetObjectsStore().Remove<HousingRoomEntity>(this);
    }
}

void HousingRoomEntity::BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target) const
{
    if (!target)
        return;

    // HousingRoomEntity uses entity fragments (like BaseEntity) but is a WorldObject
    // for grid/visibility. Object::BuildCreateUpdateBlockForPlayer uses BuildValuesCreate
    // which expects CGObject fields. We override to use the entity fragment path instead.

    uint8 updateType = UPDATETYPE_CREATE_OBJECT;
    uint8 objectType = m_objectTypeId;
    CreateObjectBits flags = m_updateFlag;

    ByteBuffer& buf = data->GetBuffer();
    buf << uint8(updateType);
    buf << GetGUID();
    buf << uint8(objectType);

    BuildMovementUpdate(buf, flags, target);

    UF::UpdateFieldFlag fieldFlags = GetUpdateFieldFlagsFor(target);
    std::size_t sizePos = buf.wpos();
    buf << uint32(0);
    buf << uint8(fieldFlags);
    BuildEntityFragments(buf, m_entityFragments.GetIds());

    for (std::size_t i = 0; i < m_entityFragments.UpdateableCount; ++i)
    {
        WowCS::EntityFragment fragmentId = m_entityFragments.Updateable.Ids[i];
        if (WowCS::IsIndirectFragment(fragmentId))
            buf << uint8(1);

        WowCS::EntityFragmentInfo->SerializeCreate[static_cast<std::size_t>(m_entityFragments.Updateable.Ids[i])](
            m_entityFragments.Updateable.Data[i], fieldFlags, buf, target, this);
    }

    buf.put<uint32>(sizePos, buf.wpos() - sizePos - 4);
    data->AddUpdateBlock();

    // The start of the CREATE block in hex, to check it byte by byte against a capture.
    if (sLog->ShouldLog("network.opcode", LOG_LEVEL_DEBUG))
    {
        // Dump from start of this entity (approximate: back up from sizePos)
        std::size_t blockEnd = buf.wpos();
        std::size_t dumpStart = (sizePos > 40) ? sizePos - 40 : 0;
        std::string hex;
        for (std::size_t i = dumpStart; i < std::min(blockEnd, dumpStart + 80); ++i)
            hex += Trinity::StringFormat("{:02x} ", buf[i]);
        TC_LOG_DEBUG("network.opcode", "HousingRoomEntity::BuildCreate guid={} objectType={} "
            "fieldBlockSize={} pos=({:.1f},{:.1f},{:.1f}) HEX: {}",
            GetGUID().ToString(), uint32(m_objectTypeId),
            buf.wpos() - sizePos - 4,
            GetPositionX(), GetPositionY(), GetPositionZ(), hex);
    }
}

void HousingRoomEntity::BuildValuesCreate(UF::UpdateFieldFlag /*flags*/, ByteBuffer& /*data*/, Player const* /*target*/) const
{
    // Not used — BuildCreateUpdateBlockForPlayer handles everything via entity fragments.
}

void HousingRoomEntity::BuildValuesUpdate(UF::UpdateFieldFlag flags, ByteBuffer& data, Player const* target) const
{
    // VALUES updates use the standard fragment change mask system
}

std::string HousingRoomEntity::GetNameForLocaleIdx(LocaleConstant /*locale*/) const
{
    switch (_role)
    {
        case HousingGridEntityRole::ExteriorRoot:
            return "HousingExteriorRoot";
        case HousingGridEntityRole::AttachPoint:
            return "HousingAttachPoint";
        case HousingGridEntityRole::House:
            return "HousingHouse";
        default:
            return "HousingRoom";
    }
}

std::string HousingRoomEntity::GetDebugInfo() const
{
    return Trinity::StringFormat("{}\nType: HousingRoomEntity pos: ({:.1f}, {:.1f}, {:.1f})",
        Object::GetDebugInfo(),
        GetPositionX(), GetPositionY(), GetPositionZ());
}

UF::UpdateFieldFlag HousingRoomEntity::GetUpdateFieldFlagsFor(Player const* /*target*/) const
{
    return UF::UpdateFieldFlag::None;
}

bool HousingRoomEntity::AddToObjectUpdate()
{
    GetMap()->AddUpdateObject(this);
    return true;
}

void HousingRoomEntity::RemoveFromObjectUpdate()
{
    GetMap()->RemoveUpdateObject(this);
}

void HousingRoomEntity::SetHouseGUID(ObjectGuid houseGuid)
{
    SetUpdateFieldValue(m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData).ModifyValue(&UF::HousingRoomData::HouseGUID), houseGuid);
}

void HousingRoomEntity::SetHouseRoomID(int32 roomId)
{
    SetUpdateFieldValue(m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData).ModifyValue(&UF::HousingRoomData::HouseRoomID), roomId);
}

void HousingRoomEntity::SetFlags(int32 flags)
{
    SetUpdateFieldValue(m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData).ModifyValue(&UF::HousingRoomData::Flags), flags);
}

void HousingRoomEntity::SetFloorIndex(int32 floorIndex)
{
    // The room fragment retail sends has no floor number: in the room create block of the
    // hbcd3 capture (lines 515619-515668) WowPacketParser reads a nonsense floor number at
    // line 515655 and then runs off the end of the packet. The floor is kept here for the
    // interior map's own bookkeeping.
    _floorIndex = floorIndex;
}

void HousingRoomEntity::AddMeshObject(ObjectGuid meshObjectGuid)
{
    AddDynamicUpdateFieldValue(m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData)
        .ModifyValue(&UF::HousingRoomData::MeshObjects)) = meshObjectGuid;
}

void HousingRoomEntity::ReplaceMeshObjects(std::vector<ObjectGuid> const& newGuids)
{
    // Clear existing MeshObjects dynamic array and repopulate with new GUIDs.
    // Called after theme respawn to update the room entity's mesh list so the
    // client doesn't reference stale/destroyed mesh GUIDs (causes null deref crash).
    ClearDynamicUpdateFieldValues(m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData)
        .ModifyValue(&UF::HousingRoomData::MeshObjects));

    for (ObjectGuid const& guid : newGuids)
        AddDynamicUpdateFieldValue(m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData)
            .ModifyValue(&UF::HousingRoomData::MeshObjects)) = guid;
}

void HousingRoomEntity::AddDoor(int32 roomComponentID, Position const& offset, uint8 connectionType, ObjectGuid attachedRoomGuid)
{
    auto doorRef = AddDynamicUpdateFieldValue(
        m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData)
            .ModifyValue(&UF::HousingRoomData::Doors));
    doorRef.ModifyValue(&UF::HousingDoorData::RoomComponentID).SetValue(roomComponentID);
    doorRef.ModifyValue(&UF::HousingDoorData::RoomComponentOffset).SetValue(
        TaggedPosition<Position::XYZ>(offset.GetPositionX(), offset.GetPositionY(), offset.GetPositionZ()));
    doorRef.ModifyValue(&UF::HousingDoorData::RoomComponentType).SetValue(connectionType);
    doorRef.ModifyValue(&UF::HousingDoorData::AttachedRoomGUID).SetValue(attachedRoomGuid);
}

bool HousingRoomEntity::UpdateDoorConnection(int32 roomComponentID, ObjectGuid attachedRoomGuid)
{
    // Find the door with the matching component ID and update its AttachedRoomGUID.
    // Read from the const view, then modify the matching entry.
    UF::HousingRoomData const& roomData = *m_housingRoomData;
    for (uint32 i = 0; i < roomData.Doors.size(); ++i)
    {
        if (roomData.Doors[i].RoomComponentID == roomComponentID)
        {
            SetUpdateFieldValue(
                m_values.ModifyValue(&HousingRoomEntity::m_housingRoomData)
                    .ModifyValue(&UF::HousingRoomData::Doors, i)
                    .ModifyValue(&UF::HousingDoorData::AttachedRoomGUID),
                attachedRoomGuid);
            return true;
        }
    }
    return false;
}

void HousingRoomEntity::SetMirroredPosition(Position const& pos, QuaternionData const& rot,
    float scale, ObjectGuid attachParent, uint8 attachFlags)
{
    auto posData = m_values.ModifyValue(&HousingRoomEntity::m_mirroredPositionData)
        .ModifyValue(&UF::MirroredPositionData::PositionData);
    SetUpdateFieldValue(posData.ModifyValue(&UF::MirroredMeshObjectData::AttachParentGUID), attachParent);
    SetUpdateFieldValue(posData.ModifyValue(&UF::MirroredMeshObjectData::PositionLocalSpace),
        TaggedPosition<Position::XYZ>(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()));
    SetUpdateFieldValue(posData.ModifyValue(&UF::MirroredMeshObjectData::RotationLocalSpace), rot);
    SetUpdateFieldValue(posData.ModifyValue(&UF::MirroredMeshObjectData::ScaleLocalSpace), scale);
    SetUpdateFieldValue(posData.ModifyValue(&UF::MirroredMeshObjectData::AttachmentFlags), attachFlags);

    _attachParent = attachParent;
    _localPosition.Relocate(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
    _localRotation = rot;
}

void HousingRoomEntity::SetHouseData(ObjectGuid bnetAccount, ObjectGuid cosmeticOwner, int32 plotIndex, uint32 level, uint64 favor,
    uint32 interiorDecorBudget, uint32 exteriorDecorBudget, uint32 exteriorFixtureBudget, uint32 roomBudget, ObjectGuid entityGuid)
{
    auto houseData = m_values.ModifyValue(&HousingRoomEntity::m_housingPlayerHouseData);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::BnetAccount), bnetAccount);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::CosmeticOwner), cosmeticOwner);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::PlotIndex), plotIndex);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::Level), level);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::Favor), favor);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::InteriorDecorPlacementBudget), interiorDecorBudget);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::ExteriorDecorPlacementBudget), exteriorDecorBudget);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::ExteriorFixtureBudget), exteriorFixtureBudget);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::RoomPlacementBudget), roomBudget);
    SetUpdateFieldValue(houseData.ModifyValue(&UF::HousingPlayerHouseData::EntityGUID), entityGuid);
}

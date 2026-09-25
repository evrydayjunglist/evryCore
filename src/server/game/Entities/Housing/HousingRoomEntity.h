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

#ifndef TRINITYCORE_HOUSING_ROOM_ENTITY_H
#define TRINITYCORE_HOUSING_ROOM_ENTITY_H

#include "Object.h"
#include "GridObject.h"
#include "MapObject.h"
#include "QuaternionData.h"

// What a housing entity of object type 18 on a map's grid stands for. They all use this one class, so the grid and
// the visibility code keep one entry for object type 18.
enum class HousingGridEntityRole : uint8
{
    // A room (Housing/2 GUID, subtype 2, arg2 the HouseRoomID): FHousingRoom_C, FMirroredPositionData_C and
    // Tag_HousingRoom, with its stationary position in the movement block (hbcd3 1299598).
    Room,
    // A house's exterior root (an Entity with entry 0): FMirroredPositionData_C, Tag_HouseExteriorPiece and
    // Tag_HouseExteriorRoot, attached to the plot's room with the house's placement (hbcd3 1310328).
    ExteriorRoot,
    // A point a piece of the house carries something at, such as the Entity at an entry's EntryOffset that the front
    // door rides: FMirroredPositionData_C only (hbcd3 1310987).
    AttachPoint,
    // A house as the other accounts see it (Housing/3 GUID): FHousingPlayerHouse_C only, with no position in the
    // movement block (hbcd3 1310364). Characters of the house's own Battle.net account are never sent it; they hold
    // the house through their session's house entity, which has the same GUID.
    House,
};

// A housing entity of object type 18 that lives on a map's grid and reaches players through ordinary visibility.
// Retail sends none of them with CGObject.
class TC_GAME_API HousingRoomEntity final : public WorldObject, public GridObject<HousingRoomEntity>, public MapObject
{
public:
    explicit HousingRoomEntity(HousingGridEntityRole role = HousingGridEntityRole::Room);

    void AddToWorld() override;
    void RemoveFromWorld() override;

    // A room is added to the map here. The other roles set their fields first, so they pass addToMap false and add
    // themselves with Map::AddToMap afterwards.
    bool Create(ObjectGuid guid, Map* map, Position const& pos, bool addToMap = true);

    HousingGridEntityRole GetRole() const { return _role; }

    // Pure virtual overrides from WorldObject
    ObjectGuid GetCreatorGUID() const override { return ObjectGuid::Empty; }
    ObjectGuid GetOwnerGUID() const override { return ObjectGuid::Empty; }
    uint32 GetFaction() const override { return 0; }

    // A house entity is not sent to characters of its own Battle.net account.
    bool IsNeverVisibleFor(WorldObject const* seer, bool allowServersideObjects) const override;

    // Override to use entity fragment serialization (like BaseEntity) instead of
    // Object's BuildValuesCreate path which expects CGObject fields.
    void BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target) const override;
    void BuildValuesCreate(UF::UpdateFieldFlag flags, ByteBuffer& data, Player const* target) const override;
    void BuildValuesUpdate(UF::UpdateFieldFlag flags, ByteBuffer& data, Player const* target) const override;
    std::string GetNameForLocaleIdx(LocaleConstant locale) const override;
    std::string GetDebugInfo() const override;

    // Room data setters
    void SetHouseGUID(ObjectGuid houseGuid);
    void SetHouseRoomID(int32 roomId);
    void SetFlags(int32 flags);
    void SetFloorIndex(int32 floorIndex);
    int32 GetFloorIndex() const { return _floorIndex; }
    void AddMeshObject(ObjectGuid meshObjectGuid);
    void ReplaceMeshObjects(std::vector<ObjectGuid> const& newGuids);
    void AddDoor(int32 roomComponentID, Position const& offset, uint8 connectionType, ObjectGuid attachedRoomGuid = ObjectGuid::Empty);
    bool UpdateDoorConnection(int32 roomComponentID, ObjectGuid attachedRoomGuid);

    // Mirrored position data setters. The pose is kept too, so the map can work out where anything attached to this
    // entity stands.
    void SetMirroredPosition(Position const& pos, QuaternionData const& rot, float scale,
        ObjectGuid attachParent = ObjectGuid::Empty, uint8 attachFlags = 3);
    ObjectGuid GetAttachParentGUID() const { return _attachParent; }
    Position const& GetLocalPosition() const { return _localPosition; }
    QuaternionData const& GetLocalRotation() const { return _localRotation; }

    // House data (the House role). Budgets are the house level's maximums, as for the session's house entity.
    void SetHouseData(ObjectGuid bnetAccount, ObjectGuid cosmeticOwner, int32 plotIndex, uint32 level, uint64 favor,
        uint32 interiorDecorBudget, uint32 exteriorDecorBudget, uint32 exteriorFixtureBudget, uint32 roomBudget, ObjectGuid entityGuid);

    UF::UpdateField<UF::HousingRoomData, int32(WowCS::EntityFragment::FHousingRoom_C), 0> m_housingRoomData;
    UF::UpdateField<UF::MirroredPositionData, int32(WowCS::EntityFragment::FMirroredPositionData_C), 0> m_mirroredPositionData;
    UF::UpdateField<UF::HousingPlayerHouseData, int32(WowCS::EntityFragment::FHousingPlayerHouse_C), 0> m_housingPlayerHouseData;

protected:
    UF::UpdateFieldFlag GetUpdateFieldFlagsFor(Player const* target) const override;
    bool AddToObjectUpdate() override;
    void RemoveFromObjectUpdate() override;

private:
    HousingGridEntityRole _role;
    // Kept on the server only; retail's room fragment has no floor number.
    int32 _floorIndex = 0;
    ObjectGuid _attachParent;
    Position _localPosition;
    QuaternionData _localRotation;
};

#endif // TRINITYCORE_HOUSING_ROOM_ENTITY_H

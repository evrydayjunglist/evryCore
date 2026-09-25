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

#include "Housing.h"
#include "Account.h"
#include "HousingDecorStore.h"
#include "HousingPlayerHouseEntity.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "DBCEnums.h"
#include "GameTime.h"
#include "HousingMap.h"
#include "HousingMgr.h"
#include "Neighborhood.h"
#include "NeighborhoodMgr.h"
#include "HousingPackets.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include <algorithm>
#include "RealmList.h"
#include "WorldSession.h"
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>

namespace
{
    // M13: normalize a decor rotation quaternion to a unit quaternion before it
    // is stored. The client sends Euler angles which the handler converts to a
    // quaternion each place/move; normalizing removes any float drift so a decor
    // item at a cardinal angle (0/90/180/270) round-trips through the FLOAT
    // columns to the exact same orientation instead of subtly re-rotating on
    // reload (the retail rotation bug we must not replicate). A degenerate
    // (near-zero) quaternion falls back to identity.
    void NormalizeDecorRotation(float& x, float& y, float& z, float& w)
    {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (!std::isfinite(len) || len < 1e-6f)
        {
            x = y = z = 0.0f;
            w = 1.0f;
            return;
        }
        float inv = 1.0f / len;
        x *= inv; y *= inv; z *= inv; w *= inv;
    }
}

// Global DB ID generators — initialized from MAX(id) at server startup
std::atomic<uint64> Housing::s_nextHouseDbId{1};
std::atomic<uint64> Housing::s_nextDecorDbId{1};
std::atomic<uint64> Housing::s_nextRoomDbId{1};

std::mutex Housing::s_sharedStateLock;
std::unordered_map<uint64, std::weak_ptr<Housing::PersistentState>> Housing::s_houseStates;

Housing::Housing(Player* owner, uint64 databaseId, uint8 slot /*= 0*/) : _owner(owner)
{
    uint32 const bnetAccountId = owner && owner->GetSession() ? owner->GetSession()->GetBattlenetAccountId() : 0;
    ASSERT(bnetAccountId, "Housing needs a Battle.net account; the caller must refuse before constructing");

    if (!databaseId)
        databaseId = s_nextHouseDbId.fetch_add(1);

    // The decor belongs to the account: every house of it shares one store.
    _decorStore = HousingDecorStore::Acquire(bnetAccountId);

    std::lock_guard<std::mutex> registryGuard(s_sharedStateLock);

    std::weak_ptr<PersistentState>& sharedState = s_houseStates[databaseId];
    _state = sharedState.lock();
    if (!_state)
    {
        _state = std::make_shared<PersistentState>();
        _state->DatabaseId = databaseId;
        _state->OwnerAccountId = bnetAccountId;
        _state->Slot = slot;
        sharedState = _state;
    }
}

Housing::~Housing()
{
    std::lock_guard<std::mutex> registryGuard(s_sharedStateLock);

    uint64 const databaseId = _state->DatabaseId;
    _state.reset();

    // The last Housing object of a house takes its shared entry with it. The decor store keeps its own registry.
    if (auto itr = s_houseStates.find(databaseId); itr != s_houseStates.end() && itr->second.expired())
        s_houseStates.erase(itr);
}

std::scoped_lock<std::recursive_mutex, std::recursive_mutex> Housing::LockStateAndStore() const
{
    return std::scoped_lock<std::recursive_mutex, std::recursive_mutex>(_state->Lock, _decorStore->GetLock());
}

ObjectGuid Housing::MakeDecorGuid(uint32 decorEntryId, uint64 low)
{
    return ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 1, /*arg1*/ HOUSING_DECOR_GUID_ARG1, /*arg2*/ decorEntryId, low);
}

ObjectGuid Housing::NewDecorGuid(uint32 decorEntryId)
{
    return MakeDecorGuid(decorEntryId, s_nextDecorDbId.fetch_add(1));
}

void Housing::NoteDecorDbId(uint64 low)
{
    uint64 expected = s_nextDecorDbId.load();
    while (low >= expected && !s_nextDecorDbId.compare_exchange_weak(expected, low + 1))
        ;
}

uint32 Housing::GetStoredDecorCount(uint32 decorEntryId) const
{
    return _decorStore->CountStored(decorEntryId);
}

ObjectGuid Housing::MakeHouseGuid(uint8 slot, uint32 interiorNeighborhoodMapId, uint32 bnetAccountId)
{
    return ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 3, /*arg1*/ slot, /*arg2*/ interiorNeighborhoodMapId, uint64(bnetAccountId));
}

ObjectGuid Housing::MakeHouseGuid(uint8 slot, uint32 bnetAccountId)
{
    return MakeHouseGuid(slot, sHousingMgr.GetHouseInteriorNeighborhoodMapId(), bnetAccountId);
}

bool Housing::AccountOwnsHouseInDistrict(std::vector<int32> const& ownedHouseWorldMapIds, int32 districtWorldMapId)
{
    return std::find(ownedHouseWorldMapIds.begin(), ownedHouseWorldMapIds.end(), districtWorldMapId) != ownedHouseWorldMapIds.end();
}

uint8 Housing::FindFreeSlot(std::vector<uint8> const& usedSlots)
{
    for (uint32 slot = 1; slot <= std::numeric_limits<uint8>::max(); ++slot)
        if (std::find(usedSlots.begin(), usedSlots.end(), uint8(slot)) == usedSlots.end())
            return uint8(slot);
    return 0;
}

uint64 Housing::GetPurchasePrice(std::size_t accountHouseCount, uint64 plotCost)
{
    return accountHouseCount ? plotCost : 0;
}

void Housing::CountBattlenetAccounts(std::vector<GuildMemberAccount> const& members, uint32& accounts, uint32& activeAccounts)
{
    std::unordered_map<uint32, bool> active;
    for (GuildMemberAccount const& member : members)
    {
        if (!member.BnetAccountId)
            continue;
        bool& accountActive = active[member.BnetAccountId];
        accountActive = accountActive || member.Active;
    }

    accounts = uint32(active.size());
    activeAccounts = uint32(std::count_if(active.begin(), active.end(), [](auto const& account) { return account.second; }));
}

int32 Housing::ChoosePackedHouseToUnpack(std::vector<int32> const& packedHouseWorldMapIds, int32 districtWorldMapId, bool atHouseCap)
{
    for (std::size_t i = 0; i < packedHouseWorldMapIds.size(); ++i)
        if (packedHouseWorldMapIds[i] == districtWorldMapId)
            return int32(i);

    return atHouseCap && !packedHouseWorldMapIds.empty() ? 0 : -1;
}

void Housing::MoveDecorBetweenPlots(Position const& fromPlot, Position const& toPlot, PlacedDecor& decor)
{
    // Into the old plot's frame, then out of the new one's. The turn is a yaw, the same convention HousingMap uses
    // to place a house on its plot and a decor piece in its room.
    float const dx = decor.PosX - fromPlot.GetPositionX();
    float const dy = decor.PosY - fromPlot.GetPositionY();
    float const fromCos = std::cos(fromPlot.GetOrientation());
    float const fromSin = std::sin(fromPlot.GetOrientation());
    float const localX = fromCos * dx + fromSin * dy;
    float const localY = -fromSin * dx + fromCos * dy;

    float const toCos = std::cos(toPlot.GetOrientation());
    float const toSin = std::sin(toPlot.GetOrientation());
    decor.PosX = toPlot.GetPositionX() + toCos * localX - toSin * localY;
    decor.PosY = toPlot.GetPositionY() + toSin * localX + toCos * localY;
    decor.PosZ = toPlot.GetPositionZ() + (decor.PosZ - fromPlot.GetPositionZ());

    // The piece turns by the difference between the two plots' facings: a yaw quaternion times its rotation.
    float const halfTurn = (toPlot.GetOrientation() - fromPlot.GetOrientation()) * 0.5f;
    float const turnZ = std::sin(halfTurn);
    float const turnW = std::cos(halfTurn);
    float const x = decor.RotationX;
    float const y = decor.RotationY;
    float const z = decor.RotationZ;
    float const w = decor.RotationW;
    decor.RotationX = turnW * x - turnZ * y;
    decor.RotationY = turnW * y + turnZ * x;
    decor.RotationZ = turnW * z + turnZ * w;
    decor.RotationW = turnW * w - turnZ * z;
}

std::vector<Housing::AccountHouse> Housing::GetAccountHouses(uint32 bnetAccountId)
{
    // Take the states out of the registry first and lock each one after letting the registry go, so this never
    // holds the registry while it waits on a house.
    std::vector<std::shared_ptr<PersistentState>> states;
    {
        std::lock_guard<std::mutex> registryGuard(s_sharedStateLock);
        for (auto const& [databaseId, weakState] : s_houseStates)
            if (std::shared_ptr<PersistentState> state = weakState.lock())
                if (state->OwnerAccountId == bnetAccountId)
                    states.push_back(std::move(state));
    }

    std::vector<AccountHouse> houses;
    houses.reserve(states.size());
    for (std::shared_ptr<PersistentState> const& state : states)
    {
        std::lock_guard<std::recursive_mutex> stateGuard(state->Lock);
        if (!state->Loaded || state->Deleted)
            continue;

        AccountHouse& house = houses.emplace_back();
        house.DatabaseId = state->DatabaseId;
        house.Slot = state->Slot;
        house.HouseGuid = state->HouseGuid;
        house.NeighborhoodGuid = state->NeighborhoodGuid;
        house.FormerNeighborhoodGuid = state->FormerNeighborhoodGuid;
        house.Packed = state->Packed;
    }
    return houses;
}

bool Housing::IsOwnedBy(Player const* player) const
{
    return player && player->GetSession() && !_state->Deleted
        && player->GetSession()->GetBattlenetAccountId() == _state->OwnerAccountId;
}

void Housing::InitializeDbIdGenerators()
{
    // Initialize global ID generators from current MAX(id) in the database.
    // Must be called during server startup before any Housing objects are loaded.
    {
        QueryResult result = CharacterDatabase.Query("SELECT COALESCE(MAX(guid), 0) FROM character_housing");
        uint64 maxHouseId = result ? (*result)[0].GetUInt64() : 0;
        s_nextHouseDbId.store(maxHouseId + 1);
        TC_LOG_INFO("housing", "Housing::InitializeDbIdGenerators: House ID generator starting at {} (MAX in DB: {})",
            maxHouseId + 1, maxHouseId);
    }
    {
        // One counter for every account's decor, saved with each piece.
        QueryResult result = CharacterDatabase.Query("SELECT COALESCE(MAX(guid), 0) FROM account_housing_decor");
        uint64 maxDecorId = result ? (*result)[0].GetUInt64() : 0;
        s_nextDecorDbId.store(maxDecorId + 1);
        TC_LOG_INFO("housing", "Housing::InitializeDbIdGenerators: Decor ID generator starting at {} (MAX in DB: {})",
            maxDecorId + 1, maxDecorId);
    }
    {
        QueryResult result = CharacterDatabase.Query("SELECT COALESCE(MAX(id), 0) FROM character_housing_rooms");
        uint64 maxRoomId = result ? (*result)[0].GetUInt64() : 0;
        s_nextRoomDbId.store(maxRoomId + 1);
        TC_LOG_INFO("housing", "Housing::InitializeDbIdGenerators: Room ID generator starting at {} (MAX in DB: {})",
            maxRoomId + 1, maxRoomId);
    }
}

bool Housing::LoadFromDB(Field* house, std::vector<Field*> const& decor, std::vector<Field*> const& rooms,
    std::vector<Field*> const& fixtures)
{
    if (!house || !IsOwnedBy(_owner))
        return false;

    auto guard = LockState();

    // Another character of the account already has this house loaded, and may have changed it after these rows
    // were read. What it holds is newer than the rows, so the rows are not applied again.
    if (_state->Loaded)
    {
        SyncUpdateFields();
        return !_state->Deleted;
    }

    //          0     1           2                   3                 4          5           6       7
    // SELECT guid, slot, cosmeticOwnerGuid, neighborhoodGuid, plotIndex, houseLevel, favor, settingsFlags,
    //          8          9          10         11    12    13     14        15          16             17
    //        houseSize, houseType, createTime, posX, posY, posZ, facing, houseName, houseDescription, packed,
    //          18
    //        refundAmount
    // FROM character_housing WHERE bnetAccountId = ?
    Field* fields = house;
    _state->Slot = fields[1].GetUInt8();
    _state->HouseGuid = MakeHouseGuid(_state->Slot, _state->OwnerAccountId);
    if (uint64 cosmeticOwner = fields[2].GetUInt64())
        _state->CosmeticOwnerGuid = ObjectGuid::Create<HighGuid::Player>(cosmeticOwner);
    _state->Packed = fields[17].GetUInt8() != 0;
    _state->RefundAmount = fields[18].GetUInt64();
    // Take the neighborhood's real GUID from the manager rather than rebuilding it: arg1 is its
    // NeighborhoodMapID, and this GUID goes out to the client in house/neighborhood packets, so a wrong arg1
    // reproduces the client-side NeighborhoodMap.db2 miss on the house path too.
    // A packed house stands in no neighborhood; its row keeps the one it last stood in, for its district.
    ObjectGuid rowNeighborhoodGuid;
    if (Neighborhood const* neighborhood = sNeighborhoodMgr.GetNeighborhoodByCounter(fields[3].GetUInt64()))
        rowNeighborhoodGuid = neighborhood->GetGuid();
    else if (fields[3].GetUInt64())
        TC_LOG_ERROR("housing", "Housing::LoadFromDB: house references neighborhood counter {} which is not loaded",
            fields[3].GetUInt64());
    _state->NeighborhoodGuid = _state->Packed ? ObjectGuid::Empty : rowNeighborhoodGuid;
    _state->FormerNeighborhoodGuid = _state->Packed ? rowNeighborhoodGuid : ObjectGuid::Empty;
    // A packed house's row keeps the plot it last stood on as well.
    _state->PlotIndex = _state->Packed ? INVALID_PLOT_INDEX : fields[4].GetUInt8();
    _state->FormerPlotIndex = _state->Packed ? fields[4].GetUInt8() : INVALID_PLOT_INDEX;
    _state->Level = fields[5].GetUInt32();
    _state->Favor = fields[6].GetUInt32();
    // AddFavor, the house entity and the plot's copy of the house count from Favor64, so it starts at the saved favor.
    _state->Favor64 = _state->Favor;
    _state->SettingsFlags = fields[7].GetUInt32();
    _state->HouseSize = fields[8].GetUInt8();
    _state->HouseType = fields[9].GetUInt32();
    _state->CreateTime = fields[10].GetUInt32();

    TC_LOG_ERROR("housing", "Housing::LoadFromDB: Loaded house HouseGuid={} NeighborhoodGuid={} PlotIndex={} Level={} HouseType={} for player {}",
        _state->HouseGuid.ToString(), _state->NeighborhoodGuid.ToString(), _state->PlotIndex, _state->Level, _state->HouseType, _owner->GetGUID().ToString());
    _state->HousePosX = fields[11].GetFloat();
    _state->HousePosY = fields[12].GetFloat();
    _state->HousePosZ = fields[13].GetFloat();
    _state->HouseFacing = fields[14].GetFloat();
    // A house the owner turned without moving it still has a placement.
    _state->HasCustomPosition = (_state->HousePosX != 0.0f || _state->HousePosY != 0.0f || _state->HousePosZ != 0.0f
        || _state->HouseFacing != 0.0f);
    _state->HouseName = fields[15].GetString();
    _state->HouseDescription = fields[16].GetString();

    // Load rooms FIRST so decor can look up roomEntryId for GUID arg2
    //           0         1            2           3       4       5           6            7         8        9              10              11               12             13          14        15              16
    // SELECT roomGuid, roomEntryId, slotIndex, gridX, gridY, floorIndex, orientation, mirrored, themeId, wallTextureId, floorTextureId, ceilingTextureId, colorOverride, doorTypeId, doorSlot, ceilingTypeId, ceilingSlot
    // FROM character_housing_rooms, this house's rows
    for (Field* roomFields : rooms)
    {
        {
            fields = roomFields;

            uint64 roomDbId = fields[0].GetUInt64();
            uint32 roomEntryId = fields[1].GetUInt32();

            // Fix up roomDbId=0 from old saves that used ObjectGuid::Empty (subType=0 produced Empty GUID).
            // Without this, all rooms get the same GUID key and overwrite each other in _state->Rooms.
            if (roomDbId == 0)
                roomDbId = GenerateRoomDbId();

            // arg2=roomEntryId matches retail GUID format (sniff-verified: arg2=HouseRoomID)
            ObjectGuid roomGuid = ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 2, 0, roomEntryId, roomDbId);

            Room& room = _state->Rooms[roomGuid];
            room.Guid = roomGuid;
            room.RoomEntryId = roomEntryId;
            room.SlotIndex = fields[2].GetUInt32();
            room.GridX = fields[3].GetInt32();
            room.GridY = fields[4].GetInt32();
            room.FloorIndex = fields[5].GetInt32();
            // Backward compat: if gridX looks like old grid index (0-20), convert to yards
            if (room.GridX >= 0 && room.GridX <= 20 && room.GridX == static_cast<int32>(room.SlotIndex) && room.SlotIndex > 0)
                room.GridX = static_cast<int32>(room.SlotIndex) * static_cast<int32>(HOUSING_ROOM_GRID_SPACING);
            // Backward compat: previously FloorIndex was stored as a yard offset (e.g. 12
            // for upper stairwell partner). Retail treats it as a floor NUMBER (0, 1, 2…)
            // multiplied by 12 yards at spawn time. Convert legacy multiples-of-12 values.
            if (room.FloorIndex >= 12 && (room.FloorIndex % 12) == 0)
                room.FloorIndex /= 12;
            room.Orientation = fields[6].GetUInt8();
            room.Mirrored = fields[7].GetBool();
            room.ThemeId = fields[8].GetUInt32();
            room.WallTextureId = fields[9].GetUInt32();
            room.FloorTextureId = fields[10].GetUInt32();
            room.CeilingTextureId = fields[11].GetUInt32();
            room.ColorOverride = fields[12].GetInt32();
            room.DoorTypeId = fields[13].GetUInt32();
            room.DoorSlot = fields[14].GetUInt8();
            room.CeilingTypeId = fields[15].GetUInt32();
            room.CeilingSlot = fields[16].GetUInt8();
            room.WallThemeId = fields[17].GetUInt32();
            room.FloorThemeId = fields[18].GetUInt32();
            room.CeilingThemeId = fields[19].GetUInt32();
            // Legacy rows (pre-per-surface-theme migration) have all three = 0:
            // seed them from the single ThemeId so old houses keep their look.
            if (!room.WallThemeId && !room.FloorThemeId && !room.CeilingThemeId && room.ThemeId)
            {
                room.WallThemeId = room.ThemeId;
                room.FloorThemeId = room.ThemeId;
                room.CeilingThemeId = room.ThemeId;
            }

            // Advance global generator if needed (safety net)
            uint64 expected = s_nextRoomDbId.load();
            while (roomDbId >= expected && !s_nextRoomDbId.compare_exchange_weak(expected, roomDbId + 1))
                ;

        }
    }

    // Runtime fixup: ensure entry hall room (46) + correct visual room exist.
    // Due to the old subType=0 GUID bug, both rooms shared ObjectGuid::Empty
    // as their key and only the last one survived in the DB. Fix that first.
    {
        // Step 0: Migrate exterior geobox room (18) to entry hall room (46) in existing houses.
        // Room 18 was incorrectly used as the interior base room. It should only be used
        // for the exterior plot geobox (handled by SpawnRoomForPlot independently).
        uint32 entryHallEntry = sHousingMgr.GetEntryHallRoomEntryId();
        uint32 extGeoboxEntry = sHousingMgr.GetBaseRoomEntryId();
        if (entryHallEntry != extGeoboxEntry)
        {
            for (auto& [guid, room] : _state->Rooms)
            {
                if (room.RoomEntryId == extGeoboxEntry)
                {
                    TC_LOG_ERROR("housing", "Housing::LoadFromDB: Migrating interior base room {} -> {} "
                        "in slot {} for house {} (entry hall fixup)",
                        extGeoboxEntry, entryHallEntry, room.SlotIndex, _state->HouseGuid.ToString());
                    room.RoomEntryId = entryHallEntry;
                    break;
                }
            }
        }

        // Step 1: Ensure base room (entry hall) exists
        bool hasBaseRoom = false;
        for (auto const& [guid, room] : _state->Rooms)
        {
            if (sHousingMgr.IsBaseRoom(room.RoomEntryId))
            {
                hasBaseRoom = true;
                break;
            }
        }

        if (!hasBaseRoom)
        {
            uint32 entryHallRoomEntry = sHousingMgr.GetEntryHallRoomEntryId();
            HousingResult baseResult = PlaceRoom(entryHallRoomEntry, /*slotIndex*/ 0, /*orientation*/ 0, /*mirrored*/ false);
            TC_LOG_ERROR("housing", "Housing::LoadFromDB: Auto-placed entry hall room (entry {}) in slot 0 "
                "for house {} (migration fixup, result={})",
                entryHallRoomEntry, _state->HouseGuid.ToString(), baseResult);
        }

        // Step 2: Ensure correct visual room exists
        uint32 correctVisualRoom = sHousingMgr.GetDefaultVisualRoomEntry();
        bool hasVisualRoom = false;
        ObjectGuid wrongRoomGuid;

        for (auto const& [guid, room] : _state->Rooms)
        {
            if (sHousingMgr.IsBaseRoom(room.RoomEntryId))
                continue;

            if (room.RoomEntryId == correctVisualRoom)
            {
                hasVisualRoom = true;
                break;
            }

            // If not the correct one and it's the only non-base room, replace it
            if (wrongRoomGuid.IsEmpty())
                wrongRoomGuid = guid;
            else
                hasVisualRoom = true; // Multiple visual rooms — don't mess with them
        }

        // NOTE: Previously replaced non-Room-1 rooms with Room 1. This was too aggressive —
        // it replaced valid user placements (e.g., Stairwell) and lost gridX/gridY coordinates.
        // Rooms placed by the user are valid regardless of entry type. Only add a default
        // visual room if there are NO non-base rooms at all (empty house).

        // No visual room at all — add one
        if (!hasVisualRoom && wrongRoomGuid.IsEmpty() && correctVisualRoom)
        {
            // Find the next free slot (slot 0 is base room)
            uint32 nextSlot = 1;
            for (auto const& [guid, room] : _state->Rooms)
            {
                if (room.SlotIndex >= nextSlot)
                    nextSlot = room.SlotIndex + 1;
            }

            HousingResult placeResult = PlaceRoom(correctVisualRoom, nextSlot, /*orientation*/ 0, /*mirrored*/ false);
            TC_LOG_ERROR("housing", "Housing::LoadFromDB: Auto-placed visual room entry {} in slot {} "
                "for house {} (migration fixup, result={})",
                correctVisualRoom, nextSlot, _state->HouseGuid.ToString(), placeResult);
        }
    }

    // The account's pieces placed in this house (after the rooms, so each piece finds its room). A packed house keeps
    // them too.
    for (Field* decorFields : decor)
    {
        PlacedDecor placed = HousingDecorStore::ReadDecorRow(decorFields);
        if (uint64 roomDbId = decorFields[13].GetUInt64())
        {
            // Use the room's actual GUID key from _state->Rooms, not a reconstructed one.
            // Room migration (e.g. entry 18->46) changes RoomEntryId but not the GUID
            // key's arg2 field. Reconstructing with the migrated RoomEntryId would
            // produce a GUID that doesn't match the room's key, breaking AttachParentGUID.
            for (auto const& [rGuid, r] : _state->Rooms)
            {
                if (rGuid.GetCounter() == roomDbId)
                {
                    placed.RoomGuid = rGuid;
                    break;
                }
            }
        }

        ObjectGuid const decorGuid = placed.Guid;
        _state->PlacedDecorByGuid[decorGuid] = std::move(placed);
    }

    // Load fixtures
    //           0               1
    // SELECT fixturePointId, optionId
    // FROM character_housing_fixtures, this house's rows
    for (Field* fixtureFields : fixtures)
    {
        {
            fields = fixtureFields;

            uint32 fixturePointId = fields[0].GetUInt32();
            Fixture& fixture = _state->Fixtures[fixturePointId];
            fixture.FixturePointId = fixturePointId;
            fixture.OptionId = fields[1].GetUInt32();

        }

        // Log all loaded fixtures for debugging
        for (auto const& [pointId, fix] : _state->Fixtures)
        {
            TC_LOG_INFO("housing", "Housing::LoadFromDB: Fixture pointId={} optionId={}", fix.FixturePointId, fix.OptionId);
        }
    }

    // Migration: populate starter fixtures for houses created before persistence was added.
    // Also handles existing houses that have fixtures but are missing starter roots (Base/Roof) or door.
    bool hasBaseRoot = false, hasRoofRoot = false, hasDoor = false;
    for (auto const& [pointId, fix] : _state->Fixtures)
    {
        if (fix.OptionId == 0)
        {
            ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(fix.FixturePointId);
            if (!comp)
                continue;
            if (_state->HouseType != 0 && comp->HouseExteriorWmoDataID != static_cast<uint32>(_state->HouseType))
                continue;
            if (comp->Type == HOUSING_FIXTURE_TYPE_BASE) hasBaseRoot = true;
            if (comp->Type == HOUSING_FIXTURE_TYPE_ROOF) hasRoofRoot = true;
        }
        else
        {
            ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(fix.OptionId);
            if (comp && comp->Type == HOUSING_FIXTURE_TYPE_DOOR)
                hasDoor = true;
        }
    }

    if ((!hasBaseRoot || !hasRoofRoot || !hasDoor) && _state->HouseType != 0)
    {
        TC_LOG_INFO("housing", "Housing::LoadFromDB: Missing starter fixtures (base={}, roof={}, door={}) for house {} — populating (migration)",
            hasBaseRoot, hasRoofRoot, hasDoor, _state->HouseGuid.ToString());
        PopulateStarterFixtures(/*persistNow*/ true);
    }

    _state->Loaded = true;

    // Recalculate budget weights from loaded data
    RecalculateBudgets();

    // NOTE: FHousingStorage_C is NOT populated at login — retail flow confirms it is only sent
    // when the player enters edit mode or sends REQUEST_STORAGE. Populating it at login causes
    // client crashes (BLZ_ALLOC for HouseDecorGUID) because the client doesn't expect storage
    // data in the initial Account entity CREATE. Storage entries (placed and in storage) are
    // sent on demand by Player::PushHousingDecorStorage(), called from the REQUEST_STORAGE handler.

    SyncUpdateFields();

    TC_LOG_DEBUG("housing", "Housing::LoadFromDB: Loaded house for player {} (GUID {}): "
        "{} decor, {} rooms, {} fixtures (interior budget {}/{}, room budget {}/{})",
        _owner->GetName(), GetDatabaseId(),
        uint32(_state->PlacedDecorByGuid.size()), uint32(_state->Rooms.size()),
        uint32(_state->Fixtures.size()),
        _state->InteriorDecorWeightUsed, GetMaxInteriorDecorBudget(),
        _state->RoomWeightUsed, GetMaxRoomBudget());

    return true;
}

bool Housing::JoinLoadedState()
{
    if (!IsOwnedBy(_owner))
        return false;

    {
        auto guard = LockState();
        if (!_state->Loaded || _state->Deleted)
            return false;
    }

    SyncUpdateFields();
    return true;
}

void Housing::SaveToDB(CharacterDatabaseTransaction trans)
{
    auto guard = LockState();
    if (!_state->Loaded || _state->Deleted)
        return;

    ObjectGuid::LowType houseDatabaseId = GetDatabaseId();

    DeleteFromDB(houseDatabaseId, trans);

    // Save main housing record
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_HOUSING);
    uint8 houseIndex = 0;
    stmt->setUInt64(houseIndex++, houseDatabaseId);
    stmt->setUInt32(houseIndex++, _state->OwnerAccountId);
    stmt->setUInt8(houseIndex++, _state->Slot);
    stmt->setUInt64(houseIndex++, _state->CosmeticOwnerGuid.GetCounter());
    stmt->setUInt64(houseIndex++, (_state->Packed ? _state->FormerNeighborhoodGuid : _state->NeighborhoodGuid).GetCounter());
    stmt->setUInt8(houseIndex++, _state->Packed ? _state->FormerPlotIndex : _state->PlotIndex);
    stmt->setUInt32(houseIndex++, _state->Level);
    stmt->setUInt32(houseIndex++, _state->Favor);
    stmt->setUInt32(houseIndex++, _state->SettingsFlags);
    stmt->setUInt8(houseIndex++, _state->HouseSize);
    stmt->setUInt32(houseIndex++, _state->HouseType);
    stmt->setUInt32(houseIndex++, _state->CreateTime);
    stmt->setFloat(houseIndex++, _state->HousePosX);
    stmt->setFloat(houseIndex++, _state->HousePosY);
    stmt->setFloat(houseIndex++, _state->HousePosZ);
    stmt->setFloat(houseIndex++, _state->HouseFacing);
    stmt->setString(houseIndex++, _state->HouseName);
    stmt->setString(houseIndex++, _state->HouseDescription);
    stmt->setUInt8(houseIndex++, _state->Packed ? 1 : 0);
    stmt->setUInt64(houseIndex++, _state->RefundAmount);
    trans->Append(stmt);

    // The account's pieces placed in this house; its pieces in storage are saved by the account's decor store.
    for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
        HousingDecorStore::AppendDecorRow(trans, _state->OwnerAccountId, houseDatabaseId, decor);

    // Save rooms
    for (auto const& [guid, room] : _state->Rooms)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_HOUSING_ROOMS);
        uint8 index = 0;
        stmt->setUInt64(index++, houseDatabaseId);
        stmt->setUInt64(index++, guid.GetCounter());
        stmt->setUInt32(index++, room.RoomEntryId);
        stmt->setUInt32(index++, room.SlotIndex);
        stmt->setInt32(index++, room.GridX);
        stmt->setInt32(index++, room.GridY);
        stmt->setInt32(index++, room.FloorIndex);
        stmt->setUInt8(index++, uint8(room.Orientation));
        stmt->setBool(index++, room.Mirrored);
        stmt->setUInt32(index++, room.ThemeId);
        stmt->setUInt32(index++, room.WallTextureId);
        stmt->setUInt32(index++, room.FloorTextureId);
        stmt->setUInt32(index++, room.CeilingTextureId);
        stmt->setInt32(index++, room.ColorOverride);
        stmt->setUInt32(index++, room.DoorTypeId);
        stmt->setUInt8(index++, room.DoorSlot);
        stmt->setUInt32(index++, room.CeilingTypeId);
        stmt->setUInt8(index++, room.CeilingSlot);
        stmt->setUInt32(index++, room.WallThemeId);
        stmt->setUInt32(index++, room.FloorThemeId);
        stmt->setUInt32(index++, room.CeilingThemeId);
        trans->Append(stmt);
    }

    // Save fixtures
    for (auto const& [pointId, fixture] : _state->Fixtures)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_HOUSING_FIXTURES);
        uint8 index = 0;
        stmt->setUInt64(index++, houseDatabaseId);
        stmt->setUInt32(index++, fixture.FixturePointId);
        stmt->setUInt32(index++, fixture.OptionId);
        trans->Append(stmt);
    }
}

void Housing::DeleteFromDB(ObjectGuid::LowType houseDatabaseId, CharacterDatabaseTransaction trans)
{
    // The house's own rows, and the rows of the pieces placed in it; the pieces in storage stay.
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_HOUSING);
    stmt->setUInt64(0, houseDatabaseId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ACCOUNT_HOUSING_DECOR_BY_HOUSE);
    stmt->setUInt64(0, houseDatabaseId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_HOUSING_ROOMS);
    stmt->setUInt64(0, houseDatabaseId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_HOUSING_FIXTURES);
    stmt->setUInt64(0, houseDatabaseId);
    trans->Append(stmt);
}

void Housing::SetEditorMode(HousingEditorMode mode)
{
    _editorMode = mode;

    // Sniff-verified: retail sends EditorMode via UPDATE_OBJECT alongside
    // UNIT_FLAG_PACIFIED, UNIT_FLAG2_NO_ACTIONS and SilencedSchoolMask=127.
    // The client reads EditorMode from PlayerHouseInfoComponentData to set
    // the internal editor state (ClientHousingDecorSystem +329) which gates
    // ClickTarget (flag 16) for decor selection.
    if (_owner)
        _owner->SetHousingEditorModeUpdateField(mode == HOUSING_EDITOR_MODE_EXTERIOR_CUSTOMIZATION
            ? HOUSING_EDITOR_MODE_FIELD_FIXTURE_EDIT : static_cast<uint8>(mode));
}

HousingResult Housing::Create(ObjectGuid neighborhoodGuid, uint8 plotIndex, uint64 refundAmount)
{
    if (!IsOwnedBy(_owner))
        return HOUSING_RESULT_PERMISSION_DENIED;

    auto guard = LockState();
    if (_state->Loaded || !_state->HouseGuid.IsEmpty() || !_state->Slot)
        return HOUSING_RESULT_INVALID_HOUSE;

    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
        return HOUSING_RESULT_PLOT_NOT_FOUND;

    _state->NeighborhoodGuid = neighborhoodGuid;
    _state->PlotIndex = plotIndex;
    _state->Level = 1;
    // A new house starts with the starter favor, saved with the rest of the house by the purchase.
    _state->Favor64 = HOUSE_PURCHASE_STARTER_FAVOR;
    _state->Favor = static_cast<uint32>(HOUSE_PURCHASE_STARTER_FAVOR);
    _state->SettingsFlags = HOUSE_SETTING_DEFAULT;
    _editorMode = HOUSING_EDITOR_MODE_NONE;
    _state->ExteriorLockHolder.Clear();
    _state->HouseSize = HOUSING_FIXTURE_SIZE_SMALL;
    // Racial house style: Night Elf → 55, Blood Elf → 56, other Alliance → 9, other Horde → 87
    _state->HouseType = HousingMgr::GetRacialWmoDataID(_owner->GetRace(), _owner->GetTeam());
    _state->CreateTime = static_cast<uint32>(GameTime::GetGameTime());
    _state->HasCustomPosition = false;
    _state->HousePosX = _state->HousePosY = _state->HousePosZ = _state->HouseFacing = 0.0f;

    _state->Packed = false;
    _state->FormerNeighborhoodGuid.Clear();
    _state->FormerPlotIndex = INVALID_PLOT_INDEX;
    _state->RefundAmount = refundAmount;
    _state->HouseGuid = MakeHouseGuid(_state->Slot, _state->OwnerAccountId);
    // The buyer is shown as the owner until House Settings names another character of the account.
    _state->CosmeticOwnerGuid = _owner->GetGUID();
    _state->Loaded = true;

    TC_LOG_ERROR("housing", "Housing::Create: Player {} (BNetAcct {}) created house on plot {} in neighborhood {} — HouseGuid={}",
        _owner->GetName(), _state->OwnerAccountId, plotIndex, _state->NeighborhoodGuid.ToString(), _state->HouseGuid.ToString());

    SyncUpdateFields();

    // Every new house starts with an entry hall room (interior base room).
    // Room 18 is the EXTERIOR geobox only (handled by SpawnRoomForPlot independently).
    // Room 46 is the proper interior entry hall (sniff-verified: BASE_ROOM flag, door to visual room).
    PlaceRoom(sHousingMgr.GetEntryHallRoomEntryId(), /*slotIndex*/ 0, /*orientation*/ 0, /*mirrored*/ false);

    // Also place a default visual room so the interior renders walls/floor/ceiling.
    // Base room (18) only provides the geobox boundary — visual geometry needs a separate room.
    uint32 visualRoom = sHousingMgr.GetDefaultVisualRoomEntry();
    if (visualRoom)
    {
        // Entry door at +3, Room1 door at -12 → spacing = 3-(-12) = 15 yards
        HousingResult visualResult = PlaceRoom(visualRoom, /*slotIndex*/ 1, /*orientation*/ 0, /*mirrored*/ false, nullptr, /*gridX*/ 15, /*gridY*/ 0);
        if (visualResult == HOUSING_RESULT_SUCCESS)
        {
            TC_LOG_ERROR("housing", "Housing::Create: Auto-placed visual room entry {} in slot 1 for player {}",
                visualRoom, _owner->GetName());
        }
        else
        {
            TC_LOG_ERROR("housing", "Housing::Create: PlaceRoom FAILED for visual room entry {} — result={} — "
                "interior will be empty for player {}",
                visualRoom, visualResult, _owner->GetName());
        }
    }
    else
    {
        TC_LOG_ERROR("housing", "Housing::Create: No visual room entry found — interior will be empty for player {}",
            _owner->GetName());
    }

    // Populate starter fixtures: Base + Roof for the racial WMO style. They are saved with the rest of the house
    // in the purchase's transaction.
    PopulateStarterFixtures(/*persistNow*/ false);

    return HOUSING_RESULT_SUCCESS;
}

ObjectGuid Housing::GetPlotGuid() const
{
    // Deterministic PlotGUID: subType=2 encodes neighborhood + plot index
    return ObjectGuid::Create<HighGuid::Housing>(
        /*subType*/ 2,
        /*arg1*/ sRealmList->GetCurrentRealmId().Realm,
        /*arg2*/ _state->PlotIndex,
        _state->NeighborhoodGuid.GetCounter());
}

void Housing::SetNeighborhoodGuid(ObjectGuid guid)
{
    auto guard = LockState();
    _state->NeighborhoodGuid = guid;
}

void Housing::SetPlotIndex(uint8 plotIndex)
{
    auto guard = LockState();
    _state->PlotIndex = plotIndex;
}

void Housing::FillHouseEntry(WorldPackets::Housing::JamCliHouse& house) const
{
    auto guard = LockState();
    house.HouseGUID = _state->HouseGuid;
    house.CosmeticOwnerGUID = _state->CosmeticOwnerGuid;
    house.NeighborhoodGUID = _state->NeighborhoodGuid;
    house.PlotID = _state->PlotIndex;
    house.HouseSettingFlags = _state->SettingsFlags;
}

void Housing::SetCosmeticOwnerGuid(ObjectGuid guid)
{
    {
        auto guard = LockState();
        if (_state->CosmeticOwnerGuid == guid)
            return;

        _state->CosmeticOwnerGuid = guid;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_COSMETIC_OWNER);
        stmt->setUInt64(0, guid.GetCounter());
        stmt->setUInt64(1, GetDatabaseId());
        CharacterDatabase.Execute(stmt);
    }

    // The plot shows the cosmetic owner to everyone in the neighborhood.
    if (Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(_state->NeighborhoodGuid))
        neighborhood->UpdatePlotCosmeticOwnerByHouse(_state->HouseGuid, guid);

    SyncUpdateFields();
}

void Housing::Delete()
{
    auto guard = LockStateAndStore();

    // The rows are keyed by the house's own database id. The plot is found by the house, not by the character
    // acting: any character of the account may delete it, and the plot's roster entry may be another one's.
    // The decor placed in it belongs to the account, so it goes into the account's storage in the same transaction.
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    DeleteFromDB(GetDatabaseId(), trans);
    for (auto const& [decorGuid, decor] : _state->PlacedDecorByGuid)
        _decorStore->PutInStorage(decor, trans);
    if (!_state->NeighborhoodGuid.IsEmpty())
        if (Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(_state->NeighborhoodGuid))
            neighborhood->ReleasePlotByHouse(_state->HouseGuid, trans);
    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("housing", "Housing::Delete: Player {} deleted house {} (database id {}), {} placed decor went into storage",
        _owner->GetGUID().ToString(), _state->HouseGuid.ToString(), GetDatabaseId(), uint32(_state->PlacedDecorByGuid.size()));

    for (auto const& [decorGuid, decor] : _state->PlacedDecorByGuid)
        SetOwnerStorageEntry(decor, /*placed*/ false);

    _state->HouseGuid.Clear();
    _state->NeighborhoodGuid.Clear();
    _state->PlotIndex = INVALID_PLOT_INDEX;
    _state->Level = 1;
    _state->Favor = 0;
    _state->SettingsFlags = HOUSE_SETTING_DEFAULT;
    _editorMode = HOUSING_EDITOR_MODE_NONE;
    _state->ExteriorLockHolder.Clear();
    _state->HouseSize = HOUSING_FIXTURE_SIZE_SMALL;
    _state->HouseType = 0;
    _state->HasCustomPosition = false;
    _state->HousePosX = _state->HousePosY = _state->HousePosZ = _state->HouseFacing = 0.0f;
    _state->PlacedDecorByGuid.clear();
    _state->Rooms.clear();
    _state->Fixtures.clear();
    _state->Deleted = true;
}

void Housing::Pack(CharacterDatabaseTransaction trans)
{
    {
        auto guard = LockState();
        if (!_state->Loaded || _state->Deleted || _state->Packed)
            return;

        // Rooms, decor, fixtures, level, favor and settings stay as they are. Placed decor stays linked to the house
        // by its id while the house is packed.
        _state->FormerNeighborhoodGuid = _state->NeighborhoodGuid;
        _state->FormerPlotIndex = _state->PlotIndex;
        _state->NeighborhoodGuid.Clear();
        _state->PlotIndex = INVALID_PLOT_INDEX;
        _state->Packed = true;
        _editorMode = HOUSING_EDITOR_MODE_NONE;

        SavePlacement(trans);

        TC_LOG_INFO("housing", "Housing::Pack: house {} (database id {}) packed, it stood in neighborhood {}",
            _state->HouseGuid.ToString(), GetDatabaseId(), _state->FormerNeighborhoodGuid.ToString());
    }

    SyncUpdateFields();
}

HousingResult Housing::Unpack(ObjectGuid neighborhoodGuid, uint8 plotIndex, uint64 refundAmount)
{
    {
        auto guard = LockState();
        if (!_state->Loaded || _state->Deleted || !_state->Packed)
            return HOUSING_RESULT_INVALID_HOUSE;

        if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
            return HOUSING_RESULT_PLOT_NOT_FOUND;

        // Exterior decor is stored where it stood in the world, so it moves with the house to keep its place on the
        // plot. Both plots' frames come from NeighborhoodPlot through each neighborhood's map.
        Position fromPlot;
        Position toPlot;
        Neighborhood const* former = sNeighborhoodMgr.GetNeighborhood(_state->FormerNeighborhoodGuid);
        Neighborhood const* target = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
        if (former && target
            && sHousingMgr.GetPlotRoomAnchor(former->GetNeighborhoodMapID(), _state->FormerPlotIndex, fromPlot)
            && sHousingMgr.GetPlotRoomAnchor(target->GetNeighborhoodMapID(), plotIndex, toPlot))
        {
            if (former->GetNeighborhoodMapID() != target->GetNeighborhoodMapID() || _state->FormerPlotIndex != plotIndex)
                MoveExteriorDecorBetweenPlots(fromPlot, toPlot, nullptr);
        }
        else
        {
            TC_LOG_ERROR("housing", "Housing::Unpack: house {} (database id {}): the plot it stood on (plot {} of neighborhood {}) "
                "or the new one (plot {} of neighborhood {}) is not known, so its exterior decor keeps its old place",
                _state->HouseGuid.ToString(), GetDatabaseId(), _state->FormerPlotIndex, _state->FormerNeighborhoodGuid.ToString(),
                plotIndex, neighborhoodGuid.ToString());
        }

        _state->NeighborhoodGuid = neighborhoodGuid;
        _state->FormerNeighborhoodGuid.Clear();
        _state->FormerPlotIndex = INVALID_PLOT_INDEX;
        _state->PlotIndex = plotIndex;
        _state->Packed = false;
        // The house now stands on the plot just bought for it, so what it is worth is what was paid for that plot.
        _state->RefundAmount = refundAmount;
        // A position set on the old plot is a place on that plot, so the house stands where the new plot puts it.
        _state->HasCustomPosition = false;
        _state->HousePosX = _state->HousePosY = _state->HousePosZ = _state->HouseFacing = 0.0f;

        TC_LOG_INFO("housing", "Housing::Unpack: house {} (database id {}) unpacked onto plot {} of neighborhood {}",
            _state->HouseGuid.ToString(), GetDatabaseId(), plotIndex, neighborhoodGuid.ToString());
    }

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

void Housing::SavePlacement(CharacterDatabaseTransaction trans)
{
    auto guard = LockState();
    if (!_state->Loaded || _state->Deleted)
        return;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_PLACEMENT);
    stmt->setUInt64(0, (_state->Packed ? _state->FormerNeighborhoodGuid : _state->NeighborhoodGuid).GetCounter());
    stmt->setUInt8(1, _state->Packed ? _state->FormerPlotIndex : _state->PlotIndex);
    stmt->setUInt8(2, _state->Packed ? 1 : 0);
    stmt->setUInt64(3, GetDatabaseId());
    trans->Append(stmt);
}

void Housing::MoveExteriorDecorBetweenPlots(Position const& fromPlot, Position const& toPlot, CharacterDatabaseTransaction trans)
{
    auto guard = LockState();
    uint32 moved = 0;
    for (auto& [decorGuid, decor] : _state->PlacedDecorByGuid)
    {
        if (!IsExteriorDecorPlacement(decor.RoomGuid))
            continue;

        MoveDecorBetweenPlots(fromPlot, toPlot, decor);
        ++moved;

        if (!trans)
            continue;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_HOUSING_DECOR_POSITION);
        stmt->setFloat(0, decor.PosX);
        stmt->setFloat(1, decor.PosY);
        stmt->setFloat(2, decor.PosZ);
        stmt->setFloat(3, decor.RotationX);
        stmt->setFloat(4, decor.RotationY);
        stmt->setFloat(5, decor.RotationZ);
        stmt->setFloat(6, decor.RotationW);
        stmt->setFloat(7, decor.Scale);
        stmt->setUInt64(8, decorGuid.GetCounter());
        trans->Append(stmt);
    }

    TC_LOG_DEBUG("housing", "Housing::MoveExteriorDecorBetweenPlots: moved {} exterior decor of house {} from ({:.1f}, {:.1f}) to ({:.1f}, {:.1f})",
        moved, _state->HouseGuid.ToString(), fromPlot.GetPositionX(), fromPlot.GetPositionY(), toPlot.GetPositionX(), toPlot.GetPositionY());
}

void Housing::SetOwnerStorageEntry(PlacedDecor const& decor, bool placed) const
{
    if (!_owner || !_owner->GetSession())
        return;

    // Only once her client has the storage: before that the whole storage goes out when she asks for it.
    Battlenet::Account& account = _owner->GetSession()->GetBattlenetAccount();
    if (account.IsHousingDecorStorageSent())
        account.SetHousingDecorStorageEntry(decor.Guid, placed ? _state->HouseGuid : ObjectGuid::Empty, decor.SourceType, decor.SourceValue);
}

void Housing::OnDecorAcquired(Player* player, uint32 decorEntryId, bool firstOwned)
{
    if (!player)
        return;

    // An entry's first piece gives its FirstAcquisitionBonus as house experience to every house of the account: the
    // client's upgrade frame notes that "decor acquisition bonuses update all owned houses at once"
    // (Blizzard_HousingDashboardHouseUpgrade.lua), and retail's update for it (hbcd3 2106253, Number 25462) has one
    // entry naming only the Battle.net account, with level -1, +10 favor, source 1 and the decor, 1482.
    if (firstOwned)
    {
        HouseDecorData const* decorData = sHousingMgr.GetHouseDecorData(decorEntryId);
        if (decorData && decorData->FirstAcquisitionBonus > 0)
        {
            uint64 const bonus = uint64(decorData->FirstAcquisitionBonus);
            std::vector<Housing const*> const houses = player->GetAllHousings(/*includePacked*/ true);
            for (Housing const* house : houses)
                if (Housing* housing = player->GetHousingByGuid(house->GetHouseGuid()))
                    housing->AddFavor(bonus, HOUSING_FAVOR_SOURCE_DECOR_COLLECTION, /*emitUpdate*/ false);

            if (!houses.empty())
            {
                WorldPackets::Housing::HousingSvcsUpdateHousesLevelFavor update;
                update.Result = 0;
                update.ChangeAmount = uint32(-1);
                update.Reason = uint32(-1);
                WorldPackets::Housing::HousingSvcsUpdateHousesLevelFavor::HouseLevelFavor& account = update.Houses.emplace_back();
                account.BnetAccount = player->GetSession()->GetBattlenetAccountGUID();
                account.HouseLevel = -1;
                account.FavorValue = decorData->FirstAcquisitionBonus;
                account.UpdateSource = uint8(HOUSING_FAVOR_SOURCE_DECOR_COLLECTION);
                account.SourceDataDecorID = decorEntryId;
                account.IsAdditive = true;
                player->SendDirectMessage(update.Write());
            }
            else
                TC_LOG_DEBUG("housing", "Housing::OnDecorAcquired: {} first owned decor {}, but the account has no house to take its {} experience",
                    player->GetGUID().ToString(), decorEntryId, decorData->FirstAcquisitionBonus);
        }
    }

    // "Collect unique decor" is counted as the entries the account has owned. Retail's count is not known to be that:
    // criteria 109249 went from 1 to 109 at a house purchase that brought seven starter pieces, when the account had
    // 15 distinct entries (see CriteriaHandler::UpdateCriteria).
    player->UpdateCriteria(CriteriaType::CollectUniqueDecor, decorEntryId);
}

HousingResult Housing::PlaceDecorWithGuid(ObjectGuid decorGuid, float x, float y, float z,
    float rotX, float rotY, float rotZ, float rotW, ObjectGuid roomGuid)
{
    auto guard = LockStateAndStore();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(rotX) || !std::isfinite(rotY) || !std::isfinite(rotZ) || !std::isfinite(rotW))
        return HOUSING_RESULT_BOUNDS_FAILURE_ROOM;

    // The piece must be in the account's storage. A GUID the client made up, or one already placed, is refused.
    PlacedDecor const* stored = _decorStore->FindStored(decorGuid);
    if (!stored)
        return _state->PlacedDecorByGuid.contains(decorGuid) ? HOUSING_RESULT_INVALID_DECOR_ITEM : HOUSING_RESULT_DECOR_NOT_FOUND_IN_STORAGE;

    uint32 const decorEntryId = stored->DecorEntryId;

    HousingResult validationResult = sHousingMgr.ValidateDecorPlacement(decorEntryId, Position(x, y, z), _state->Level);
    if (validationResult != HOUSING_RESULT_SUCCESS)
        return validationResult;

    // The placed decor limit is the level's decor budget, checked below by weight: the client shows it as "Total Decor
    // Limit: used/budget" and says to "Level up your House to increase your Placement Budget" (GlobalStrings
    // HOUSING_DECOR_BUDGET_TOOLTIP_INDOOR). There is no separate count of pieces per level.

    // Retail semantics (verified via sniff build 66263, both alliance + horde):
    // the client ALWAYS sends a non-Empty RoomGuid in CMSG_HOUSING_DECOR_PLACE.
    // Exterior placements use Housing-2-arg2=<base-room-entry-id>-counter=X
    // (arg2=18 on 66263). Interior placements use Housing-2-arg2=<visual-room
    // -entry-id>-counter=Y (arg2=1 etc.). AttachParent is always Empty.
    //
    // Detect the plot exterior room identity and route it to the exterior
    // budget/skip the interior _state->Rooms lookup, while preserving the RoomGuid
    // as-sent so downstream consumers (DB row, move/remove round-trips) still
    // see what retail sends.
    bool const isExterior = IsExteriorDecorPlacement(roomGuid);

    // A4: enforce the outdoor "two lights cannot overlap" rule before charging.
    if (HousingResult overlap = CheckLightOverlap(decorEntryId, x, y, z, isExterior);
        overlap != HOUSING_RESULT_SUCCESS)
        return overlap;

    uint32 weightCost = sHousingMgr.GetDecorWeightCost(decorEntryId);
    if (isExterior)
    {
        if (_state->ExteriorDecorWeightUsed + weightCost > GetMaxExteriorDecorBudget())
            return HOUSING_RESULT_MAX_PLACED_DECOR_REACHED;
    }
    else
    {
        if (_state->InteriorDecorWeightUsed + weightCost > GetMaxInteriorDecorBudget())
            return HOUSING_RESULT_MAX_PLACED_DECOR_REACHED;
    }

    if (!isExterior)
    {
        auto roomItr = _state->Rooms.find(roomGuid);
        if (roomItr == _state->Rooms.end())
            return HOUSING_RESULT_ROOM_NOT_FOUND;

        uint32 roomDecorCount = 0;
        for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
        {
            if (decor.RoomGuid == roomGuid)
                ++roomDecorCount;
        }
        if (roomDecorCount >= MAX_HOUSING_DECOR_PER_ROOM)
            return HOUSING_RESULT_MAX_PLACED_DECOR_REACHED;
    }

    // M13: persist a normalized unit quaternion for a lossless cardinal round-trip.
    NormalizeDecorRotation(rotX, rotY, rotZ, rotW);

    // The piece leaves storage and stands in the house, with the same GUID, source and dyes.
    Optional<PlacedDecor> taken = _decorStore->TakeStored(decorGuid);
    if (!taken)
        return HOUSING_RESULT_DECOR_NOT_FOUND_IN_STORAGE;

    PlacedDecor& decor = _state->PlacedDecorByGuid[decorGuid];
    decor = std::move(*taken);
    decor.PosX = x;
    decor.PosY = y;
    decor.PosZ = z;
    decor.RotationX = rotX;
    decor.RotationY = rotY;
    decor.RotationZ = rotZ;
    decor.RotationW = rotW;
    decor.Scale = 1.0f;
    decor.RoomGuid = roomGuid;
    decor.Locked = false;
    decor.PlacementTime = GameTime::GetGameTime();

    // M2: charge the SAME budget the CHECK validated (exterior-plot rooms count
    // as exterior, not interior).
    if (isExterior)
        _state->ExteriorDecorWeightUsed += weightCost;
    else
        _state->InteriorDecorWeightUsed += weightCost;

    // The piece's row now names this house.
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        HousingDecorStore::AppendDecorRow(trans, _state->OwnerAccountId, GetDatabaseId(), decor);
        CharacterDatabase.CommitTransaction(trans);
    }

    SetOwnerStorageEntry(decor, /*placed*/ true);

    TC_LOG_DEBUG("housing", "Housing::PlaceDecorWithGuid: Player {} placed decor entry {} (GUID: {}) at ({}, {}, {}) in house {}",
        _owner->GetName(), decorEntryId, decorGuid.ToString(), x, y, z, _state->HouseGuid.ToString());

    // CriteriaType::PlaceDecor (270, "Place any decor"). miscValue1 = HouseDecor entry so decor-scoped
    // ModifierTree conditions can still discriminate; this is the single commit point for a placement.
    _owner->UpdateCriteria(CriteriaType::PlaceDecor, decorEntryId);

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

std::vector<Housing::AcquiredDecor> Housing::PlaceStarterDecor(CharacterDatabaseTransaction trans)
{
    std::vector<AcquiredDecor> announced;

    auto guard = LockStateAndStore();
    if (_state->HouseGuid.IsEmpty() || !_owner)
        return announced;

    // The house's entry hall (room slot 0) and its first room past it, as Housing::Create made them.
    Room const* entryHall = nullptr;
    Room const* visualRoom = nullptr;
    for (auto const& [guid, room] : _state->Rooms)
    {
        if (room.SlotIndex == 0)
            entryHall = &room;
        else if (!visualRoom && !sHousingMgr.IsBaseRoom(room.RoomEntryId))
            visualRoom = &room;
    }

    if (!entryHall || !visualRoom)
    {
        TC_LOG_ERROR("housing", "Housing::PlaceStarterDecor: house {} has no entry hall or no first room, so it gets no starter decor",
            _state->HouseGuid.ToString());
        return announced;
    }

    // A placed piece stores its place in the interior map's own coordinates, which is what the client's placement
    // packet carries and what HouseInteriorMap reads back; the table below is local to each room.
    float originX = -1000.0f, originY = -1000.0f, originZ = 0.1f;
    if (NeighborhoodMapData const* nmData = sHousingMgr.GetNeighborhoodMapDataForWorldMap(HOUSE_INTERIOR_MAP_ID))
    {
        originX = nmData->Origin[0];
        originY = nmData->Origin[1];
        originZ = nmData->Origin[2];
    }

    struct StarterPiece
    {
        uint32 DecorEntryId;
        bool InEntryHall;           // otherwise the first room
        bool HasPlace;              // a captured place; without one the piece goes into storage
        float X, Y, Z;
        float RotX, RotY, RotZ, RotW;
    };

    // Retail's Horde set, one piece per first-time message and in that order (hbcd3 1299364-1299534): 1700, 81, 2549,
    // 10952, 8910, 1700, 2549. All seven stood in the new house (hbcd3 1431714-1431809).
    //  - 81 stands at (0.0736084, 10.788147, 0.020065002) in the first room, turned by (0, 0, -0.7071047, 0.7071089)
    //    (hbcd3 1411644-1411650).
    //  - 10952 is the exit door's decor: (-2.2401733, 0.006225586, 0.019993) in the entry hall, not turned (hbcd3
    //    1402938-1402945). HouseInteriorMap stands the exit door on it.
    //  - The windows 1700, the crate 2549 and the chandelier 8910 use the places of the port's starter table, which
    //    has the fireplace at 81's captured place and turn to three decimals.
    //  - The second crate: no capture or source on hand gives its place, so it goes into the account's storage
    //    instead of the house.
    // The Alliance set is not captured. An Alliance house gets only its exit door's decor, 9144 (Founder's Point
    // Front Door, whose gameobject is the Alliance exit door 575017), and no first-time message.
    std::vector<StarterPiece> pieces;
    bool announce = false;
    Neighborhood const* neighborhood = sNeighborhoodMgr.GetNeighborhood(_state->NeighborhoodGuid);
    if (neighborhood && neighborhood->GetFactionRestriction() == NEIGHBORHOOD_FACTION_HORDE)
    {
        announce = true;
        pieces = {
            { 1700,  false, true,  11.458f,     7.588f,      2.984f,    0.0f, 0.0f, -0.9999962f, 0.0027621f },
            {   81,  false, true,   0.0736084f, 10.788147f,  0.020065f, 0.0f, 0.0f, -0.7071047f, 0.7071089f },
            { 2549,  false, true,   9.844f,    -8.013f,      0.020f,    0.0f, 0.0f,  0.9914417f, 0.1305500f },
            { 10952, true,  true,  -2.2401733f, 0.006225586f, 0.019993f, 0.0f, 0.0f, 0.0f,       1.0f },
            { 8910,  false, true,   6.836f,    -5.971f,      8.137f,    0.0f, 0.0f, -0.9999962f, 0.0027621f },
            { 1700,  false, true,  -7.528f,   -11.480f,      3.029f,    0.0f, 0.0f,  0.7071018f, 0.7071118f },
            { 2549,  false, false,  0.0f,       0.0f,        0.0f,      0.0f, 0.0f,  0.0f,       1.0f },
        };
    }
    else
        pieces = { { 9144, true, true, -2.2401733f, 0.006225586f, 0.019993f, 0.0f, 0.0f, 0.0f, 1.0f } };

    for (StarterPiece const& piece : pieces)
    {
        HouseDecorData const* decorData = sHousingMgr.GetHouseDecorData(piece.DecorEntryId);
        if (!decorData)
        {
            TC_LOG_ERROR("housing", "Housing::PlaceStarterDecor: decor {} is not in HouseDecor, so house {} goes without it",
                piece.DecorEntryId, _state->HouseGuid.ToString());
            continue;
        }

        AcquiredDecor acquired;
        acquired.DecorEntryId = piece.DecorEntryId;

        if (!piece.HasPlace)
        {
            _decorStore->CreateStored(piece.DecorEntryId, DECOR_SOURCE_STARTER, {}, acquired.FirstOwned, trans);
        }
        else
        {
            Room const& room = piece.InEntryHall ? *entryHall : *visualRoom;
            PlacedDecor decor;
            decor.Guid = NewDecorGuid(piece.DecorEntryId);
            decor.DecorEntryId = piece.DecorEntryId;
            decor.PosX = originX + float(room.GridX) + piece.X;
            decor.PosY = originY + float(room.GridY) + piece.Y;
            decor.PosZ = originZ + float(room.FloorIndex) * HOUSE_INTERIOR_FLOOR_HEIGHT + piece.Z;
            decor.RotationX = piece.RotX;
            decor.RotationY = piece.RotY;
            decor.RotationZ = piece.RotZ;
            decor.RotationW = piece.RotW;
            decor.RoomGuid = room.Guid;
            decor.PlacementTime = GameTime::GetGameTime();
            decor.SourceType = DECOR_SOURCE_STARTER;

            acquired.FirstOwned = _decorStore->MarkOwned(piece.DecorEntryId, trans);
            ObjectGuid const decorGuid = decor.Guid;
            _state->PlacedDecorByGuid[decorGuid] = std::move(decor);
        }

        // A starter piece is one of its entry's starting quantity, so the purchase counts it as redeemed and a later
        // redeem cannot make that copy again: 1700 (two), 81, 10952 and 9144 have a starting quantity, 2549 and 8910
        // none.
        if (HousingDecorStore::StarterPieceUsesStartingQuantity(decorData->StartingQuantity, decorData->Flags,
            _decorStore->GetRedeemed(piece.DecorEntryId)))
            _decorStore->AddRedeemed(piece.DecorEntryId, trans);

        acquired.Announced = announce;
        announced.push_back(acquired);
    }

    RecalculateBudgets();

    TC_LOG_DEBUG("housing", "Housing::PlaceStarterDecor: house {} got {} starter decor", _state->HouseGuid.ToString(), uint32(pieces.size()));
    return announced;
}

Housing::PlacedDecor const* Housing::FindExitDoorDecor() const
{
    for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
        if (HouseDecorData const* decorData = sHousingMgr.GetHouseDecorData(decor.DecorEntryId))
            if (decorData->GameObjectID == int32(INTERIOR_DOOR_GO_HORDE) || decorData->GameObjectID == int32(INTERIOR_DOOR_GO_ALLIANCE))
                return &decor;
    return nullptr;
}

HousingResult Housing::MoveDecor(ObjectGuid decorGuid, float x, float y, float z,
    float rotX, float rotY, float rotZ, float rotW, float scale /*= 1.0f*/)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    // Validate coordinate sanity
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(rotX) || !std::isfinite(rotY) || !std::isfinite(rotZ) || !std::isfinite(rotW))
        return HOUSING_RESULT_BOUNDS_FAILURE_ROOM;

    // Clamp scale to reasonable range (sniff shows values like 0.45 to 1.62)
    if (!std::isfinite(scale) || scale < 0.01f)
        scale = 1.0f;
    if (scale > 5.0f)
        scale = 5.0f;

    auto itr = _state->PlacedDecorByGuid.find(decorGuid);
    if (itr == _state->PlacedDecorByGuid.end())
        return HOUSING_RESULT_DECOR_NOT_FOUND;

    // M1: MoveDecor previously performed NO spatial validation. Route the move
    // target through the same room/plot AABB check as placement so a moved item
    // cannot be flung to arbitrary coordinates.
    HousingResult validationResult = sHousingMgr.ValidateDecorPlacement(itr->second.DecorEntryId, Position(x, y, z), _state->Level);
    if (validationResult != HOUSING_RESULT_SUCCESS)
        return validationResult;

    // A4: a moved light must also honour the "two lights cannot overlap" rule.
    // Exclude the decor being moved so an in-place nudge never collides with itself.
    if (HousingResult overlap = CheckLightOverlap(itr->second.DecorEntryId, x, y, z,
            IsExteriorDecorPlacement(itr->second.RoomGuid), decorGuid);
        overlap != HOUSING_RESULT_SUCCESS)
        return overlap;

    // M13: normalize the rotation quaternion for a lossless cardinal round-trip.
    NormalizeDecorRotation(rotX, rotY, rotZ, rotW);

    PlacedDecor& decor = itr->second;
    decor.PosX = x;
    decor.PosY = y;
    decor.PosZ = z;
    decor.RotationX = rotX;
    decor.RotationY = rotY;
    decor.RotationZ = rotZ;
    decor.RotationW = rotW;
    decor.Scale = scale;

    // Immediate persist for crash safety
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_HOUSING_DECOR_POSITION);
    stmt->setFloat(0, x);
    stmt->setFloat(1, y);
    stmt->setFloat(2, z);
    stmt->setFloat(3, rotX);
    stmt->setFloat(4, rotY);
    stmt->setFloat(5, rotZ);
    stmt->setFloat(6, rotW);
    stmt->setFloat(7, scale);
    stmt->setUInt64(8, decorGuid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "Housing::MoveDecor: Player {} moved decor {} to ({}, {}, {}) scale={:.2f} in house {}",
        _owner->GetName(), decorGuid.ToString(), x, y, z, scale, _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::RemoveDecor(ObjectGuid decorGuid)
{
    auto guard = LockStateAndStore();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->PlacedDecorByGuid.find(decorGuid);
    if (itr == _state->PlacedDecorByGuid.end())
        return HOUSING_RESULT_DECOR_NOT_FOUND;

    // Sniff-verified: Lock→Remove is a valid retail flow (packet #27117 LOCK then
    // #27139 REMOVE with Result=0). The house owner can always remove their own decor.
    // Lock only prevents OTHER editors from modifying — not the owner.

    // Refund WeightCost budget (route to correct budget based on room)
    uint32 decorEntryId = itr->second.DecorEntryId;
    uint32 weightCost = sHousingMgr.GetDecorWeightCost(decorEntryId);
    if (IsExteriorDecorPlacement(itr->second.RoomGuid))
    {
        if (_state->ExteriorDecorWeightUsed >= weightCost)
            _state->ExteriorDecorWeightUsed -= weightCost;
        else
            _state->ExteriorDecorWeightUsed = 0;
    }
    else
    {
        if (_state->InteriorDecorWeightUsed >= weightCost)
            _state->InteriorDecorWeightUsed -= weightCost;
        else
            _state->InteriorDecorWeightUsed = 0;
    }

    // The piece goes back into the account's storage with its GUID and source; retail keeps its storage entry with
    // an empty house (hbcd3 1436387 after the remove at 1436351).
    PlacedDecor decor = std::move(itr->second);
    _state->PlacedDecorByGuid.erase(itr);
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        _decorStore->PutInStorage(decor, trans);
        CharacterDatabase.CommitTransaction(trans);
    }

    SetOwnerStorageEntry(decor, /*placed*/ false);

    TC_LOG_DEBUG("housing", "Housing::RemoveDecor: Player {} removed decor {} from house {} into storage",
        _owner->GetName(), decorGuid.ToString(), _state->HouseGuid.ToString());

    // CriteriaType::RemoveDecor (271, "Remove any decor"). miscValue1 = the HouseDecor entry removed.
    _owner->UpdateCriteria(CriteriaType::RemoveDecor, decorEntryId);

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::CommitDecorDyes(ObjectGuid decorGuid, std::array<uint32, MAX_HOUSING_DYE_SLOTS> const& dyeSlots)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->PlacedDecorByGuid.find(decorGuid);
    if (itr == _state->PlacedDecorByGuid.end())
        return HOUSING_RESULT_DECOR_NOT_FOUND;

    // Validate each requested dye color exists in the palette (DyeColor.db2). A color of 0
    // clears that slot and is always allowed. Each dye slot maps to a shader channel on the
    // decor (per HousingDecorDyeSlot.channel) - for light-emitting decor a channel drives the
    // emitted light/glow color, so this is also how decor "lighting" is recolored.
    // NOTE: the client documents that dye slots "accept colors of any category"
    // (HousingDecorDyeSlot.dyeColorCategoryID has no functional use), so category is NOT
    // enforced here - only that the color is a real DyeColor record.
    // (Per-account dye OWNERSHIP is a separate, currently wire-unrecovered gate - see
    // HOUSING_DYE_SYSTEM_ANALYSIS_68275.md.)
    for (uint32 const dyeColorId : dyeSlots)
    {
        if (dyeColorId && !sDyeColorStore.LookupEntry(dyeColorId))
            return HOUSING_RESULT_MISSING_DYE; // color does not exist in DyeColor.db2
    }

    itr->second.DyeSlots = dyeSlots;

    // Immediate persist for crash safety
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_HOUSING_DECOR_DYES);
    stmt->setUInt32(0, dyeSlots[0]);
    stmt->setUInt32(1, dyeSlots[1]);
    stmt->setUInt32(2, dyeSlots[2]);
    stmt->setUInt64(3, decorGuid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "Housing::CommitDecorDyes: Player {} updated dyes on decor {} in house {}",
        _owner->GetName(), decorGuid.ToString(), _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::SetDecorLocked(ObjectGuid decorGuid, bool locked)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->PlacedDecorByGuid.find(decorGuid);
    if (itr == _state->PlacedDecorByGuid.end())
        return HOUSING_RESULT_DECOR_NOT_FOUND;

    itr->second.Locked = locked;

    // Immediate persist for crash safety
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_HOUSING_DECOR_LOCKED);
    stmt->setUInt8(0, locked ? 1 : 0);
    stmt->setUInt64(1, decorGuid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "Housing::SetDecorLocked: Player {} {} decor {} in house {}",
        _owner->GetName(), locked ? "locked" : "unlocked", decorGuid.ToString(), _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::SetDecorPet(ObjectGuid decorGuid, ObjectGuid petGuid, uint8 petFlag)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->PlacedDecorByGuid.find(decorGuid);
    if (itr == _state->PlacedDecorByGuid.end())
        return HOUSING_RESULT_DECOR_NOT_FOUND;

    itr->second.PetGuid = petGuid;
    itr->second.PetFlag = petFlag;

    // Immediate targeted persist for crash safety (mirrors SetDecorLocked).
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_HOUSING_DECOR_PET);
    stmt->setUInt64(0, petGuid.IsEmpty() ? 0 : petGuid.GetCounter());
    stmt->setUInt8(1, petFlag);
    stmt->setUInt64(2, decorGuid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "Housing::SetDecorPet: Player {} {} pet {} on decor {} in house {}",
        _owner->GetName(), petGuid.IsEmpty() ? "cleared" : "bound", petGuid.ToString(),
        decorGuid.ToString(), _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::ResetDecor(uint8 scope, uint32* outRemoved /*= nullptr*/)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    // HousingHouseScope: 1 = Interior, 2 = Exterior. Anything else is rejected.
    if (scope != 1 && scope != 2)
        return HOUSING_RESULT_GENERIC_FAILURE;

    bool wantExterior = (scope == 2);

    // Snapshot the matching guids first — RemoveDecor mutates _state->PlacedDecorByGuid.
    std::vector<ObjectGuid> toRemove;
    toRemove.reserve(_state->PlacedDecorByGuid.size());
    for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
    {
        if (IsExteriorDecorPlacement(decor.RoomGuid) == wantExterior)
            toRemove.push_back(guid);
    }

    uint32 removed = 0;
    for (ObjectGuid const& guid : toRemove)
    {
        // RemoveDecor refunds the budget and puts the piece into the account's storage.
        if (RemoveDecor(guid) == HOUSING_RESULT_SUCCESS)
            ++removed;
    }

    if (outRemoved)
        *outRemoved = removed;

    TC_LOG_INFO("housing", "Housing::ResetDecor: Player {} reset {} scope, removed {} decor from house {}",
        _owner->GetName(), wantExterior ? "exterior" : "interior", removed, _state->HouseGuid.ToString());

    return HOUSING_RESULT_SUCCESS;
}

Housing::PlacedDecor const* Housing::GetPlacedDecor(ObjectGuid decorGuid) const
{
    auto itr = _state->PlacedDecorByGuid.find(decorGuid);
    if (itr != _state->PlacedDecorByGuid.end())
        return &itr->second;

    return nullptr;
}

std::vector<Housing::PlacedDecor const*> Housing::GetAllPlacedDecor() const
{
    std::vector<PlacedDecor const*> result;
    result.reserve(_state->PlacedDecorByGuid.size());
    for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
        result.push_back(&decor);
    return result;
}

HousingResult Housing::PlaceRoom(uint32 roomEntryId, uint32 slotIndex, uint32 orientation, bool mirrored, ObjectGuid* outRoomGuid, int32 gridX, int32 gridY, int32 floorIndex)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    // Validate room entry exists
    HouseRoomData const* roomData = sHousingMgr.GetHouseRoomData(roomEntryId);
    if (!roomData)
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    // Validate orientation (0-3 for cardinal directions)
    if (orientation > 3)
        return HOUSING_RESULT_PLOT_NOT_FOUND;

    // First room placed must be a base room
    if (_state->Rooms.empty() && !roomData->IsBaseRoom())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    // Only one base room allowed
    if (roomData->IsBaseRoom())
    {
        for (auto const& [guid, existingRoom] : _state->Rooms)
        {
            HouseRoomData const* existingData = sHousingMgr.GetHouseRoomData(existingRoom.RoomEntryId);
            if (existingData && existingData->IsBaseRoom())
                return HOUSING_RESULT_ROOM_UPDATE_FAILED;
        }
    }

    // Non-base rooms require at least one existing room in the house
    if (!roomData->IsBaseRoom() && _state->Rooms.empty())
        return HOUSING_RESULT_ROOM_UPDATE_FAILED;

    // Check room count limit
    if (_state->Rooms.size() >= MAX_HOUSING_ROOMS_PER_HOUSE)
    {
        TC_LOG_ERROR("housing", "PlaceRoom: rejected entry {} - count limit ({}/{})",
            roomEntryId, uint32(_state->Rooms.size()), MAX_HOUSING_ROOMS_PER_HOUSE);
        return HOUSING_RESULT_GENERIC_FAILURE;
    }

    // Check WeightCost-based room budget
    uint32 roomWeightCost = sHousingMgr.GetRoomWeightCost(roomEntryId);
    if (_state->RoomWeightUsed + roomWeightCost > GetMaxRoomBudget())
    {
        TC_LOG_ERROR("housing", "PlaceRoom: rejected entry {} - weight budget exceeded (used={} + cost={} > max={})",
            roomEntryId, _state->RoomWeightUsed, roomWeightCost, GetMaxRoomBudget());
        return HOUSING_RESULT_GENERIC_FAILURE;
    }

    // Check for slot collision
    for (auto const& [guid, room] : _state->Rooms)
    {
        if (room.SlotIndex == slotIndex)
            return HOUSING_RESULT_PLOT_NOT_FOUND;
    }

    // NOTE: Doorway components (Type 7) are OPTIONAL in the DB2.
    // Standard rooms (1-15) have 0 doorway components — they use wall segments (Type 1) instead.
    // Only prefab/custom rooms (113+) have explicit doorway components.
    // Retail places rooms without doorways, so we don't enforce this check.

    // Generate a new room guid
    uint64 newDbId = GenerateRoomDbId();
    // arg2=roomEntryId matches retail GUID format (sniff-verified: arg2=HouseRoomID)
    ObjectGuid roomGuid = ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 2, 0, roomEntryId, newDbId);

    Room& room = _state->Rooms[roomGuid];
    room.Guid = roomGuid;
    room.RoomEntryId = roomEntryId;
    room.SlotIndex = slotIndex;
    room.GridX = gridX;
    room.GridY = gridY;
    room.FloorIndex = floorIndex;
    room.Orientation = orientation;
    room.Mirrored = mirrored;
    room.ThemeId = 0;

    if (outRoomGuid)
        *outRoomGuid = roomGuid;

    // Update room budget tracking
    _state->RoomWeightUsed += roomWeightCost;

    TC_LOG_DEBUG("housing", "Housing::PlaceRoom: Player {} placed room entry {} at slot {} in house {} (room budget {}/{})",
        _owner->GetName(), roomEntryId, slotIndex, _state->HouseGuid.ToString(),
        _state->RoomWeightUsed, GetMaxRoomBudget());

    // Account-level notification: room collection update
    if (_owner->GetSession())
    {
        WorldPackets::Housing::AccountRoomCollectionUpdate notif;
        notif.AddSingle(roomEntryId);
        _owner->GetSession()->SendPacket(notif.Write());
    }

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::RemoveRoom(ObjectGuid roomGuid)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    // Can't remove the base room
    HouseRoomData const* roomData = sHousingMgr.GetHouseRoomData(itr->second.RoomEntryId);
    if (roomData && roomData->IsBaseRoom())
        return HOUSING_RESULT_ROOM_UPDATE_FAILED;

    // Can't remove the last room
    if (_state->Rooms.size() <= 1)
        return HOUSING_RESULT_ROOM_UPDATE_FAILED;

    // The decor placed in this room goes into the account's storage first
    std::vector<ObjectGuid> decorToRemove;
    for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
    {
        if (decor.RoomGuid == roomGuid)
            decorToRemove.push_back(guid);
    }
    for (ObjectGuid const& decorGuid : decorToRemove)
    {
        TC_LOG_DEBUG("housing", "Housing::RemoveRoom: Auto-removing decor {} from room {} before deletion",
            decorGuid.ToString(), roomGuid.ToString());
        RemoveDecor(decorGuid);
    }

    // Verify remaining rooms stay connected after removal (BFS from base room)
    if (!IsRoomGraphConnectedWithout(roomGuid))
        return HOUSING_RESULT_ROOM_UPDATE_FAILED;

    // Refund room WeightCost budget
    uint32 roomWeightCost = sHousingMgr.GetRoomWeightCost(itr->second.RoomEntryId);
    if (_state->RoomWeightUsed >= roomWeightCost)
        _state->RoomWeightUsed -= roomWeightCost;
    else
        _state->RoomWeightUsed = 0;

    _state->Rooms.erase(itr);

    TC_LOG_DEBUG("housing", "Housing::RemoveRoom: Player {} removed room {} from house {}",
        _owner->GetName(), roomGuid.ToString(), _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::RotateRoom(ObjectGuid roomGuid, bool clockwise)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    Room& room = itr->second;
    if (clockwise)
        room.Orientation = (room.Orientation + 1) % 4;
    else
        room.Orientation = (room.Orientation + 3) % 4; // +3 mod 4 == -1 mod 4

    PersistRoomToDB(roomGuid, room);

    TC_LOG_DEBUG("housing", "Housing::RotateRoom: Player {} rotated room {} to orientation {} in house {}",
        _owner->GetName(), roomGuid.ToString(), room.Orientation, _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::MoveRoom(ObjectGuid roomGuid, uint32 newSlotIndex, ObjectGuid swapRoomGuid, uint32 /*swapSlotIndex*/)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    // If swapping with another room
    if (!swapRoomGuid.IsEmpty())
    {
        auto swapItr = _state->Rooms.find(swapRoomGuid);
        if (swapItr == _state->Rooms.end())
            return HOUSING_RESULT_ROOM_NOT_FOUND;

        // Swap slot indices
        uint32 tempSlot = itr->second.SlotIndex;
        itr->second.SlotIndex = swapItr->second.SlotIndex;
        swapItr->second.SlotIndex = tempSlot;

        PersistRoomToDB(roomGuid, itr->second);
        PersistRoomToDB(swapRoomGuid, swapItr->second);

        TC_LOG_DEBUG("housing", "Housing::MoveRoom: Player {} swapped room {} and room {} in house {}",
            _owner->GetName(), roomGuid.ToString(), swapRoomGuid.ToString(), _state->HouseGuid.ToString());
    }
    else
    {
        // Check that target slot is not occupied
        for (auto const& [guid, room] : _state->Rooms)
        {
            if (guid != roomGuid && room.SlotIndex == newSlotIndex)
                return HOUSING_RESULT_PLOT_NOT_FOUND;
        }

        itr->second.SlotIndex = newSlotIndex;

        PersistRoomToDB(roomGuid, itr->second);

        TC_LOG_DEBUG("housing", "Housing::MoveRoom: Player {} moved room {} to slot {} in house {}",
            _owner->GetName(), roomGuid.ToString(), newSlotIndex, _state->HouseGuid.ToString());
    }

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

ObjectGuid Housing::FindBaseRoomGuid() const
{
    for (auto const& [guid, room] : _state->Rooms)
    {
        HouseRoomData const* roomData = sHousingMgr.GetHouseRoomData(room.RoomEntryId);
        if (roomData && roomData->IsBaseRoom())
            return guid;
    }

    return ObjectGuid::Empty;
}

bool Housing::IsRoomGraphConnectedWithout(ObjectGuid excludeRoomGuid) const
{
    // If only the excluded room would remain (or nothing), the graph is trivially connected
    if (_state->Rooms.size() <= 2)
        return true;

    // Find the base room as BFS start
    ObjectGuid baseRoomGuid = FindBaseRoomGuid();
    if (baseRoomGuid.IsEmpty() || baseRoomGuid == excludeRoomGuid)
        return false; // No base room available after exclusion

    // Build slot-to-guid map for remaining rooms (excluding the removed one)
    std::unordered_map<uint32 /*slotIndex*/, ObjectGuid> slotToRoom;
    for (auto const& [guid, room] : _state->Rooms)
    {
        if (guid != excludeRoomGuid)
            slotToRoom[room.SlotIndex] = guid;
    }

    // BFS from the base room through adjacent slots
    // Adjacency: rooms with slot index difference of 1 are considered connected
    // This is a simplified model; the client validates geometric doorway alignment
    std::unordered_set<ObjectGuid> visited;
    std::queue<ObjectGuid> queue;

    visited.insert(baseRoomGuid);
    queue.push(baseRoomGuid);

    while (!queue.empty())
    {
        ObjectGuid currentGuid = queue.front();
        queue.pop();

        auto currentItr = _state->Rooms.find(currentGuid);
        if (currentItr == _state->Rooms.end())
            continue;

        uint32 currentSlot = currentItr->second.SlotIndex;

        // Check adjacent slots (slot ± 1)
        for (int32 offset : { -1, 1 })
        {
            uint32 adjacentSlot = currentSlot + offset;
            // Guard against underflow for slot 0 with offset -1
            if (offset < 0 && currentSlot == 0)
                continue;

            auto adjItr = slotToRoom.find(adjacentSlot);
            if (adjItr != slotToRoom.end() && visited.find(adjItr->second) == visited.end())
            {
                visited.insert(adjItr->second);
                queue.push(adjItr->second);
            }
        }
    }

    // All remaining rooms must be reachable from the base room
    return visited.size() == slotToRoom.size();
}

HousingResult Housing::ApplyRoomTheme(ObjectGuid roomGuid, uint32 themeSetId, std::vector<uint32> const& optionIds)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    // Classify component IDs by surface type so walls/floors/ceilings can
    // carry independent themes — otherwise dyeing the ceiling overwrites the
    // wall theme (single-ThemeId field) and wall style appears to "not save".
    bool anyWall = false, anyFloor = false, anyCeiling = false;
    for (uint32 compId : optionIds)
    {
        RoomComponentEntry const* compEntry = sRoomComponentStore.LookupEntry(compId);
        if (!compEntry)
            continue;
        switch (compEntry->Type)
        {
            case HOUSING_ROOM_COMPONENT_WALL:
            case HOUSING_ROOM_COMPONENT_DOORWAY_WALL:
                anyWall = true; break;
            case HOUSING_ROOM_COMPONENT_FLOOR:
                anyFloor = true; break;
            case HOUSING_ROOM_COMPONENT_CEILING:
                anyCeiling = true; break;
            default: break;
        }
    }

    if (anyWall)
        itr->second.WallThemeId = themeSetId;
    if (anyFloor)
        itr->second.FloorThemeId = themeSetId;
    if (anyCeiling)
        itr->second.CeilingThemeId = themeSetId;
    if (!anyWall && !anyFloor && !anyCeiling)
        itr->second.WallThemeId = themeSetId;  // unclassified → wall default

    // Keep the legacy single ThemeId in sync for callers that still read it.
    itr->second.ThemeId = themeSetId;

    PersistRoomToDB(roomGuid, itr->second);

    TC_LOG_DEBUG("housing", "Housing::ApplyRoomTheme: Player {} applied theme {} to room {} ({} options) in house {}",
        _owner->GetName(), themeSetId, roomGuid.ToString(), optionIds.size(), _state->HouseGuid.ToString());

    // Account-level notification: theme collection update
    if (_owner->GetSession())
    {
        WorldPackets::Housing::AccountRoomThemeCollectionUpdate notif;
        notif.AddSingle(themeSetId);
        _owner->GetSession()->SendPacket(notif.Write());
    }

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::ApplyRoomMaterial(ObjectGuid roomGuid, uint32 textureId, int32 colorOverride, std::vector<uint32> const& optionIds)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    // Determine which surface type(s) the component IDs target and store per-type.
    // The client sends RoomComponent DB2 IDs (not RoomComponentOption IDs).
    bool anyWall = false, anyFloor = false, anyCeiling = false;
    for (uint32 compId : optionIds)
    {
        RoomComponentEntry const* compEntry = sRoomComponentStore.LookupEntry(compId);
        if (!compEntry)
            continue;

        switch (compEntry->Type)
        {
            case HOUSING_ROOM_COMPONENT_WALL:
            case HOUSING_ROOM_COMPONENT_DOORWAY_WALL:
                anyWall = true;
                break;
            case HOUSING_ROOM_COMPONENT_FLOOR:
                anyFloor = true;
                break;
            case HOUSING_ROOM_COMPONENT_CEILING:
                anyCeiling = true;
                break;
            default:
                break;
        }
    }

    // Store texture in the appropriate per-type field
    if (anyWall)
        itr->second.WallTextureId = textureId;
    if (anyFloor)
        itr->second.FloorTextureId = textureId;
    if (anyCeiling)
        itr->second.CeilingTextureId = textureId;

    // If we couldn't classify any options (e.g., missing DB2 data), still apply as wall default
    if (!anyWall && !anyFloor && !anyCeiling)
        itr->second.WallTextureId = textureId;

    itr->second.ColorOverride = colorOverride;

    PersistRoomToDB(roomGuid, itr->second);

    TC_LOG_DEBUG("housing", "Housing::ApplyRoomMaterial: Player {} applied texture {} (color {}) to room {} "
        "({} options, wall={} floor={} ceiling={}) in house {}",
        _owner->GetName(), textureId, colorOverride, roomGuid.ToString(),
        optionIds.size(), anyWall, anyFloor, anyCeiling, _state->HouseGuid.ToString());

    // Account-level notification: material collection update
    if (_owner->GetSession())
    {
        WorldPackets::Housing::AccountRoomMaterialCollectionUpdate notif;
        notif.AddSingle(textureId);
        _owner->GetSession()->SendPacket(notif.Write());
    }

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::SetDoorType(ObjectGuid roomGuid, uint32 doorTypeId, uint8 doorSlot)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    itr->second.DoorTypeId = doorTypeId;
    itr->second.DoorSlot = doorSlot;

    PersistRoomToDB(roomGuid, itr->second);

    TC_LOG_DEBUG("housing", "Housing::SetDoorType: Player {} set door type {} (slot {}) on room {} in house {}",
        _owner->GetName(), doorTypeId, doorSlot, roomGuid.ToString(), _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::SetCeilingType(ObjectGuid roomGuid, uint32 ceilingTypeId, uint8 ceilingSlot)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return HOUSING_RESULT_ROOM_NOT_FOUND;

    itr->second.CeilingTypeId = ceilingTypeId;
    itr->second.CeilingSlot = ceilingSlot;

    PersistRoomToDB(roomGuid, itr->second);

    TC_LOG_DEBUG("housing", "Housing::SetCeilingType: Player {} set ceiling type {} (slot {}) on room {} in house {}",
        _owner->GetName(), ceilingTypeId, ceilingSlot, roomGuid.ToString(), _state->HouseGuid.ToString());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

std::vector<Housing::Room const*> Housing::GetRooms() const
{
    std::vector<Room const*> result;
    result.reserve(_state->Rooms.size());
    for (auto const& [guid, room] : _state->Rooms)
        result.push_back(&room);
    return result;
}

HousingResult Housing::SelectFixtureOption(uint32 fixturePointId, uint32 optionId, std::vector<uint32>* removedHookIDs /*= nullptr*/)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    // Root fixture selections (optionId == 0) use componentID as fixturePointId — skip hook validation
    if (optionId != 0)
    {
        // Validate hook exists in DB2
        ExteriorComponentHookEntry const* hookEntry = sExteriorComponentHookStore.LookupEntry(fixturePointId);
        if (!hookEntry)
        {
            TC_LOG_DEBUG("housing", "SelectFixtureOption: hookID {} not found in DB2", fixturePointId);
            return HOUSING_RESULT_FIXTURE_NOT_FOUND;
        }

        // Validate component exists in DB2
        ExteriorComponentEntry const* compEntry = sExteriorComponentStore.LookupEntry(optionId);
        if (!compEntry)
        {
            TC_LOG_DEBUG("housing", "SelectFixtureOption: componentID {} not found in DB2", optionId);
            return HOUSING_RESULT_FIXTURE_NOT_FOUND;
        }

        // Validate component type matches hook's expected type
        if (compEntry->Type != hookEntry->ExteriorComponentTypeID)
        {
            TC_LOG_DEBUG("housing", "SelectFixtureOption: type mismatch — component {} type {} vs hook {} expected type {}",
                optionId, compEntry->Type, fixturePointId, hookEntry->ExteriorComponentTypeID);
            return HOUSING_RESULT_GENERIC_FAILURE;
        }

        // Enforce one door (entrance) per house: if placing a door, remove any existing door at other hooks.
        // Also remove any existing fixture at the TARGET hook (only one fixture per hook).
        std::vector<uint32> conflictHooks;

        if (compEntry->Type == HOUSING_FIXTURE_TYPE_DOOR)
        {
            for (auto const& [pointId, fixture] : _state->Fixtures)
            {
                if (pointId == fixturePointId || fixture.OptionId == 0)
                    continue;
                ExteriorComponentEntry const* existingComp = sExteriorComponentStore.LookupEntry(fixture.OptionId);
                if (existingComp && existingComp->Type == HOUSING_FIXTURE_TYPE_DOOR)
                {
                    TC_LOG_INFO("housing", "SelectFixtureOption: replacing existing door at hook {} (comp {}) — moving entrance to hook {}",
                        pointId, fixture.OptionId, fixturePointId);
                    conflictHooks.push_back(pointId);
                }
            }
        }

        // If the target hook already has a different fixture, remove it
        auto existingAtHook = _state->Fixtures.find(fixturePointId);
        if (existingAtHook != _state->Fixtures.end() && existingAtHook->second.OptionId != 0
            && existingAtHook->second.OptionId != optionId)
        {
            TC_LOG_INFO("housing", "SelectFixtureOption: removing existing fixture (comp {}) at hook {} to place new comp {}",
                existingAtHook->second.OptionId, fixturePointId, optionId);
            conflictHooks.push_back(fixturePointId);
        }

        // Remove all conflicting fixtures (data + DB)
        for (uint32 conflictHookId : conflictHooks)
        {
            _state->Fixtures.erase(conflictHookId);
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_HOUSING_FIXTURE_SINGLE);
            stmt->setUInt64(0, GetDatabaseId());
            stmt->setUInt32(1, conflictHookId);
            CharacterDatabase.Execute(stmt);
            if (_state->FixtureWeightUsed > 0)
                --_state->FixtureWeightUsed;

            // Signal the removed hookID to the caller for mesh despawning
            if (removedHookIDs)
                removedHookIDs->push_back(conflictHookId);
        }
    }
    else
    {
        // Root fixture (optionId == 0): fixturePointId is a componentID.
        // Remove any existing root fixture of the SAME type to prevent accumulation.
        // E.g., switching Base from Stucco(142) to Cottage(3797) must remove the old 142 entry.
        ExteriorComponentEntry const* newComp = sExteriorComponentStore.LookupEntry(fixturePointId);
        if (newComp)
        {
            uint8 newType = newComp->Type;
            std::vector<uint32> toRemove;
            for (auto const& [pointId, fixture] : _state->Fixtures)
            {
                if (fixture.OptionId != 0 || pointId == fixturePointId)
                    continue;
                ExteriorComponentEntry const* oldComp = sExteriorComponentStore.LookupEntry(fixture.FixturePointId);
                if (oldComp && oldComp->Type == newType)
                {
                    TC_LOG_INFO("housing", "SelectFixtureOption: replacing root type {} — removing old comp {} in favor of new comp {}",
                        newType, pointId, fixturePointId);
                    toRemove.push_back(pointId);
                }
            }
            for (uint32 oldKey : toRemove)
            {
                _state->Fixtures.erase(oldKey);
                // Delete old entry from DB
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_HOUSING_FIXTURE_SINGLE);
                stmt->setUInt64(0, GetDatabaseId());
                stmt->setUInt32(1, oldKey);
                CharacterDatabase.Execute(stmt);
                if (_state->FixtureWeightUsed > 0)
                    --_state->FixtureWeightUsed;
            }
        }
    }

    bool isNew = _state->Fixtures.find(fixturePointId) == _state->Fixtures.end();
    if (isNew && _state->Fixtures.size() >= MAX_HOUSING_FIXTURES_PER_HOUSE)
        return HOUSING_RESULT_FIXTURE_NOT_FOUND;

    // Enforce fixture budget for new fixtures (WeightCost = 1 per fixture by default)
    uint32 const fixtureWeightCost = 1;
    if (isNew)
    {
        if (_state->FixtureWeightUsed + fixtureWeightCost > GetMaxFixtureBudget())
            return HOUSING_RESULT_GENERIC_FAILURE;
        _state->FixtureWeightUsed += fixtureWeightCost;
    }

    Fixture& fixture = _state->Fixtures[fixturePointId];
    fixture.FixturePointId = fixturePointId;
    fixture.OptionId = optionId;

    // Immediate persist — use REPLACE semantics (delete old + insert new)
    if (!isNew)
        PersistFixtureToDB(fixturePointId, optionId);
    else
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_HOUSING_FIXTURES);
        uint8 index = 0;
        stmt->setUInt64(index++, GetDatabaseId());
        stmt->setUInt32(index++, fixturePointId);
        stmt->setUInt32(index++, optionId);
        CharacterDatabase.Execute(stmt);
    }

    TC_LOG_DEBUG("housing", "Housing::SelectFixtureOption: Player {} set fixture point {} to option {} in house {} (budget: {}/{})",
        _owner->GetName(), fixturePointId, optionId, _state->HouseGuid.ToString(),
        _state->FixtureWeightUsed, GetMaxFixtureBudget());

    // No fixture collection update goes out for a placement: the captures hold none (hled1 818926-824177).
    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Housing::RemoveFixture(uint32 componentID, uint32* outHookID /*= nullptr*/)
{
    auto guard = LockState();
    if (_state->HouseGuid.IsEmpty())
        return HOUSING_RESULT_HOUSE_NOT_FOUND;

    // Try direct key lookup first (covers core fixtures where key == componentID)
    auto itr = _state->Fixtures.find(componentID);

    // If not found by key, search by OptionId (for hook-based fixtures, key=hookID, OptionId=componentID)
    if (itr == _state->Fixtures.end())
    {
        for (auto it = _state->Fixtures.begin(); it != _state->Fixtures.end(); ++it)
        {
            if (it->second.OptionId == componentID)
            {
                itr = it;
                break;
            }
        }
    }

    if (itr == _state->Fixtures.end())
        return HOUSING_RESULT_FIXTURE_NOT_FOUND;

    uint32 hookID = itr->first; // the key is either hookID or componentID for core fixtures
    if (outHookID)
        *outHookID = hookID;

    _state->Fixtures.erase(itr);

    // Immediate persist — delete single fixture from DB
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_HOUSING_FIXTURE_SINGLE);
        stmt->setUInt64(0, GetDatabaseId());
        stmt->setUInt32(1, hookID);
        CharacterDatabase.Execute(stmt);
    }

    // Refund fixture budget
    uint32 const fixtureWeightCost = 1;
    if (_state->FixtureWeightUsed >= fixtureWeightCost)
        _state->FixtureWeightUsed -= fixtureWeightCost;

    TC_LOG_DEBUG("housing", "Housing::RemoveFixture: Player {} removed fixture {} (hook {}) in house {} (budget: {}/{})",
        _owner->GetName(), componentID, hookID, _state->HouseGuid.ToString(),
        _state->FixtureWeightUsed, GetMaxFixtureBudget());

    SyncUpdateFields();
    return HOUSING_RESULT_SUCCESS;
}

std::vector<Housing::Fixture const*> Housing::GetFixtures() const
{
    std::vector<Fixture const*> result;
    result.reserve(_state->Fixtures.size());
    for (auto const& [pointId, fixture] : _state->Fixtures)
        result.push_back(&fixture);
    return result;
}

std::unordered_map<uint32, uint32> Housing::GetFixtureOverrideMap() const
{
    // Build override map from player's hook-based fixture selections.
    // These are fixtures at hooks (doors, windows, etc.) where OptionId != 0.
    // Root overrides (base, roof variants) are handled separately via GetRootComponentOverrides().
    std::unordered_map<uint32, uint32> result;

    for (auto const& [pointId, fixture] : _state->Fixtures)
    {
        if (fixture.OptionId != 0)
            result[fixture.FixturePointId] = fixture.OptionId;
    }
    return result;
}

std::unordered_map<uint8, uint32> Housing::GetRootComponentOverrides() const
{
    // Build override map for player-selected root components per type.
    // Core fixtures (OptionId == 0) represent the player's choice for a structural root type.
    // These include both base variants (ParentComponentID == 0) and color/style variants
    // (ParentComponentID != 0) — color variants are valid selections via SetCoreFixture.
    std::unordered_map<uint8, uint32> result;

    for (auto const& [pointId, fixture] : _state->Fixtures)
    {
        if (fixture.OptionId != 0)
            continue;

        ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(fixture.FixturePointId);
        if (!comp)
        {
            TC_LOG_DEBUG("housing", "GetRootComponentOverrides: fixturePointId={} — DB2 lookup failed", fixture.FixturePointId);
            continue;
        }
        // Only structural root types (Base=9, Roof=10) are valid here.
        // Fixture types (Door=11, Window=12, etc.) stored with OptionId=0 would be invalid.
        if (comp->Type != HOUSING_FIXTURE_TYPE_BASE && comp->Type != HOUSING_FIXTURE_TYPE_ROOF)
        {
            TC_LOG_DEBUG("housing", "GetRootComponentOverrides: comp={} type={} — not a structural root type, skipping",
                comp->ID, comp->Type);
            continue;
        }
        if (_state->HouseType != 0 && comp->HouseExteriorWmoDataID != static_cast<uint32>(_state->HouseType))
        {
            TC_LOG_DEBUG("housing", "GetRootComponentOverrides: comp={} type={} — wmo={} != houseType={} (wrong style)",
                comp->ID, comp->Type, comp->HouseExteriorWmoDataID, _state->HouseType);
            continue;
        }

        result[comp->Type] = fixture.FixturePointId;
        TC_LOG_DEBUG("housing", "GetRootComponentOverrides: type={} → comp={} (wmo={}, parentComp={})",
            comp->Type, fixture.FixturePointId, comp->HouseExteriorWmoDataID, comp->ParentComponentID);
    }

    TC_LOG_INFO("housing", "GetRootComponentOverrides: {} types resolved from {} fixtures (houseType={})",
        uint32(result.size()), uint32(_state->Fixtures.size()), _state->HouseType);
    return result;
}

uint32 Housing::GetCoreExteriorComponentID() const
{
    // The core fixture is the primary component set via SetCoreFixture (OptionId == 0).
    // It can be any root type — Base (9) for Alliance, or different types for Horde.
    for (auto const& [pointId, fixture] : _state->Fixtures)
    {
        if (fixture.OptionId == 0)
        {
            ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(fixture.FixturePointId);
            // Only return this fixture if it matches the current house type's WMO data.
            // When the player switches house type, old fixtures from the previous type
            // must not override the new type's default component.
            if (comp && comp->ParentComponentID == 0 && (_state->HouseType == 0 || comp->HouseExteriorWmoDataID == _state->HouseType))
                return fixture.FixturePointId;
        }
    }
    // No explicit core fixture set — find first default root component for this house's WMO data ID.
    if (_state->HouseType > 0)
    {
        auto const* roots = sHousingMgr.GetRootComponentsForWmoData(static_cast<uint32>(_state->HouseType));
        if (roots)
        {
            uint32 fallbackComp = 0;
            for (uint32 compID : *roots)
            {
                ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(compID);
                if (!comp)
                    continue;
                if (!fallbackComp)
                    fallbackComp = compID;
                if (comp->Flags & 0x1) // IsDefault
                    return compID;
            }
            if (fallbackComp)
                return fallbackComp;
        }
        TC_LOG_ERROR("housing", "Housing::GetCoreExteriorComponentID: No root component found for houseType={}", _state->HouseType);
    }
    else
    {
        TC_LOG_ERROR("housing", "Housing::GetCoreExteriorComponentID: No fixtures and houseType=0 — cannot determine base component");
    }
    return 0;
}

std::string Housing::GetHouseName() const
{
    auto guard = LockState();
    return _state->HouseName;
}

std::string Housing::GetHouseDescription() const
{
    auto guard = LockState();
    return _state->HouseDescription;
}

void Housing::AddLevel(uint32 amount)
{
    auto guard = LockState();
    uint32 newLevel = std::min(_state->Level + amount, MAX_HOUSE_LEVEL);
    if (newLevel == _state->Level)
        return;

    _state->Level = newLevel;

    TC_LOG_DEBUG("housing", "Housing::AddLevel: Player {} house leveled up to {} (added {}) in house {}",
        _owner->GetName(), _state->Level, amount, _state->HouseGuid.ToString());

    // Persist level/favor to DB immediately
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_LEVEL_FAVOR);
    stmt->setUInt32(0, _state->Level);
    stmt->setUInt32(1, _state->Favor);
    stmt->setUInt64(2, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    RecalculateBudgets();
    SyncUpdateFields();
    SendLevelFavorUpdate(int32(_state->Level), 0, HOUSING_FAVOR_SOURCE_UNKNOWN);
}

void Housing::AddFavor(uint64 amount, HousingFavorUpdateSource source /*= HOUSING_FAVOR_SOURCE_UNKNOWN*/, bool emitUpdate /*= true*/)
{
    auto guard = LockState();
    _state->Favor64 += amount;
    _state->Favor = static_cast<uint32>(std::min<uint64>(_state->Favor64, std::numeric_limits<uint32>::max()));

    TC_LOG_DEBUG("housing", "Housing::AddFavor: Player {} favor now {} (source {}) in house {}",
        _owner->GetName(), _state->Favor64, uint8(source), _state->HouseGuid.ToString());

    // Persist level/favor to DB immediately
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_LEVEL_FAVOR);
    stmt->setUInt32(0, _state->Level);
    stmt->setUInt32(1, _state->Favor);
    stmt->setUInt64(2, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    // A packed house has no house entity on the client; it keeps the favor for when it is unpacked.
    if (!_state->Packed)
        SyncUpdateFields();

    // Skipped when the caller sends its own level and favor packet.
    if (emitUpdate)
        SendLevelFavorUpdate(-1, static_cast<int32>(std::min<uint64>(amount, std::numeric_limits<int32>::max())), source);
}

void Housing::SendLevelFavorUpdate(int32 newLevel, int32 favorGained, HousingFavorUpdateSource source) const
{
    if (!_owner || !_owner->GetSession())
        return;

    // Retail's update for one house (hbcd3 1299772, Number 13869): change and reason -1, then one entry naming only the
    // house, its level as -1 when the level did not change, and the favor gained, flagged as added to the house's own.
    // No capture shows a level change; it is sent as the new level with no favor gained.
    WorldPackets::Housing::HousingSvcsUpdateHousesLevelFavor update;
    update.Result = 0;
    update.ChangeAmount = uint32(-1);
    update.Reason = uint32(-1);
    WorldPackets::Housing::HousingSvcsUpdateHousesLevelFavor::HouseLevelFavor& house = update.Houses.emplace_back();
    house.HouseGUID = _state->HouseGuid;
    house.HouseLevel = newLevel;
    house.FavorValue = favorGained;
    house.UpdateSource = uint8(source);
    house.IsAdditive = true;
    _owner->SendDirectMessage(update.Write());
}

void Housing::OnQuestCompleted(uint32 questId)
{
    auto guard = LockState();
    // The house takes the next level when the quest HouseLevelData lists for that level is completed. Those quests
    // are "[DNT] House Level N Room Award" (levels 2 to 6), whose reward spells grant the level's room or decor, so
    // retail probably completes them when the house reaches the level rather than the other way round. Nothing on
    // this server gives them yet.
    if (_state->Level >= MAX_HOUSE_LEVEL)
        return;

    uint32 nextLevelQuestId = sHousingMgr.GetQuestForLevel(_state->Level + 1);
    if (nextLevelQuestId == 0 || nextLevelQuestId != questId)
        return;

    _state->Level++;
    TC_LOG_DEBUG("housing", "Housing::OnQuestCompleted: Player {} house leveled up to {} (quest {}) in house {}",
        _owner->GetName(), _state->Level, questId, _state->HouseGuid.ToString());

    // Persist level change and recalculate budgets
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_LEVEL_FAVOR);
    stmt->setUInt32(0, _state->Level);
    stmt->setUInt32(1, _state->Favor);
    stmt->setUInt64(2, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    RecalculateBudgets();
    SyncUpdateFields();
    SendLevelFavorUpdate(int32(_state->Level), 0, HOUSING_FAVOR_SOURCE_QUEST);
}

uint32 Housing::GetMaxInteriorDecorBudget() const
{
    return sHousingMgr.GetInteriorDecorBudgetForLevel(_state->Level);
}

uint32 Housing::GetMaxExteriorDecorBudget() const
{
    return sHousingMgr.GetExteriorDecorBudgetForLevel(_state->Level);
}

uint32 Housing::GetMaxRoomBudget() const
{
    return sHousingMgr.GetRoomBudgetForLevel(_state->Level);
}

uint32 Housing::GetMaxFixtureBudget() const
{
    return sHousingMgr.GetFixtureBudgetForLevel(_state->Level);
}

bool Housing::IsExteriorDecorPlacement(ObjectGuid roomGuid)
{
    // No room → yard/exterior placement.
    if (roomGuid.IsEmpty())
        return true;

    // The plot's base (exterior) room identity: HighGuid::Housing, subType==2,
    // low 32 bits of the high word == the base room entry id. Retail always
    // sends this RoomGuid for exterior decor even though it is "on a room".
    return roomGuid.GetHigh() == HighGuid::Housing
        && uint32((roomGuid.GetRawValue(1) >> 53) & 0x1F) == 2
        && uint32(roomGuid.GetRawValue(1) & 0xFFFFFFFFULL) == sHousingMgr.GetBaseRoomEntryId();
}

HousingResult Housing::CheckLightOverlap(uint32 decorEntryId, float x, float y, float z,
    bool isExterior, ObjectGuid excludeGuid /*= ObjectGuid::Empty*/) const
{
    // A4 / 12.0.7 "two lights cannot overlap". The rule is scoped to the exterior
    // (outdoor-lighting) placement scope and only Lighting-category decor (cat 4)
    // participates — non-lights and interior placements pass through untouched so
    // ordinary decorating is never affected.
    if (!isExterior || !sHousingMgr.IsLightingDecor(decorEntryId))
        return HOUSING_RESULT_SUCCESS;

    float const radiusSq = HOUSING_LIGHT_OVERLAP_RADIUS * HOUSING_LIGHT_OVERLAP_RADIUS;
    for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
    {
        if (guid == excludeGuid)
            continue;
        // Compare only against other EXTERIOR lights.
        if (!IsExteriorDecorPlacement(decor.RoomGuid))
            continue;
        if (!sHousingMgr.IsLightingDecor(decor.DecorEntryId))
            continue;

        float const dx = decor.PosX - x;
        float const dy = decor.PosY - y;
        float const dz = decor.PosZ - z;
        if ((dx * dx + dy * dy + dz * dz) < radiusSq)
            return HOUSING_RESULT_INVALID_LIGHT_OVERLAP;
    }
    return HOUSING_RESULT_SUCCESS;
}

void Housing::RecalculateBudgets()
{
    auto guard = LockState();
    _state->InteriorDecorWeightUsed = 0;
    _state->ExteriorDecorWeightUsed = 0;
    _state->RoomWeightUsed = 0;
    _state->FixtureWeightUsed = 0;

    // Sum WeightCost of all placed decor, routing to interior or exterior budget
    for (auto const& [guid, decor] : _state->PlacedDecorByGuid)
    {
        uint32 weightCost = sHousingMgr.GetDecorWeightCost(decor.DecorEntryId);
        if (IsExteriorDecorPlacement(decor.RoomGuid))
            _state->ExteriorDecorWeightUsed += weightCost;
        else
            _state->InteriorDecorWeightUsed += weightCost;
    }

    // Sum WeightCost of all placed rooms
    for (auto const& [guid, room] : _state->Rooms)
    {
        uint32 weightCost = sHousingMgr.GetRoomWeightCost(room.RoomEntryId);
        _state->RoomWeightUsed += weightCost;
    }

    TC_LOG_DEBUG("housing", "Housing::RecalculateBudgets: Interior decor weight: {}/{}, Exterior decor weight: {}/{}, Room weight: {}/{}",
        _state->InteriorDecorWeightUsed, GetMaxInteriorDecorBudget(),
        _state->ExteriorDecorWeightUsed, GetMaxExteriorDecorBudget(),
        _state->RoomWeightUsed, GetMaxRoomBudget());
}

void Housing::SyncUpdateFields()
{
    auto guard = LockState();
    if (!_owner || !_owner->GetSession())
        return;

    if (_state->HouseGuid.IsEmpty())
        return;

    // FHousingPlayerHouse_C belongs on the house's own Housing/3 entity, NOT the BNetAccount entity.
    HousingPlayerHouseEntity& houseEntity = _owner->GetSession()->GetHousingPlayerHouseEntity(_state->HouseGuid);
    houseEntity.SetBnetAccount(_owner->GetSession()->GetBattlenetAccountGUID());
    houseEntity.SetCosmeticOwner(_state->CosmeticOwnerGuid);
    houseEntity.SetEntityGUID(GetHouseEntityTargetFor(_owner));
    // HouseType and HouseSize are NOT part of this fragment (IDA-verified).
    houseEntity.SetPlotIndex(static_cast<int32>(_state->PlotIndex));
    houseEntity.SetLevel(_state->Level);
    houseEntity.SetFavor(_state->Favor64);
    // Send MAX budgets — the client computes remaining locally by summing placed decor weight
    // from FHousingStorage_C entries. Sending (max - used) would cause double-subtraction.
    houseEntity.SetBudgets(
        GetMaxInteriorDecorBudget(),
        GetMaxExteriorDecorBudget(),
        GetMaxRoomBudget(),
        GetMaxFixtureBudget()
    );

    TC_LOG_DEBUG("housing", "Housing::SyncUpdateFields: EntityGUID={} BnetAccount={} PlotIndex={} Level={} Favor={} Budgets=[{},{},{},{}]",
        _state->HouseGuid.ToString(), _owner->GetSession()->GetBattlenetAccountGUID().ToString(),
        _state->PlotIndex, _state->Level, _state->Favor64,
        GetMaxInteriorDecorBudget(), GetMaxExteriorDecorBudget(), GetMaxRoomBudget(), GetMaxFixtureBudget());
}

void Housing::SaveSettings(uint32 settingsFlags)
{
    auto guard = LockState();
    _state->SettingsFlags = settingsFlags;

    // Immediate persist for crash safety
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_SETTINGS);
    stmt->setUInt32(0, _state->SettingsFlags);
    stmt->setUInt64(1, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    // Mirror onto the in-memory neighborhood plot so visitor permission checks
    // (CanVisitorAccessPlot) work correctly when the owner is offline.
    if (Neighborhood* nbh = sNeighborhoodMgr.GetNeighborhood(_state->NeighborhoodGuid))
        nbh->UpdatePlotSettingsFlagsByHouse(_state->HouseGuid, _state->SettingsFlags);

    SyncUpdateFields();

    TC_LOG_DEBUG("housing", "Housing::SaveSettings: Player {} updated house settings to {} in house {}",
        _owner->GetName(), settingsFlags, _state->HouseGuid.ToString());
}

void Housing::SetHouseNameDescription(std::string const& name, std::string const& desc)
{
    auto guard = LockState();
    _state->HouseName = name.substr(0, HOUSING_MAX_NAME_LENGTH);
    _state->HouseDescription = desc.substr(0, 256);

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_NAME_DESC);
    stmt->setString(0, _state->HouseName);
    stmt->setString(1, _state->HouseDescription);
    stmt->setUInt64(2, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    SyncUpdateFields();

    TC_LOG_DEBUG("housing", "Housing::SetHouseNameDescription: Player {} set house name='{}' desc='{}' in house {}",
        _owner->GetName(), _state->HouseName, _state->HouseDescription, _state->HouseGuid.ToString());
}

void Housing::SetExteriorLockHolder(ObjectGuid playerGuid)
{
    auto guard = LockState();
    _state->ExteriorLockHolder = playerGuid;
}

ObjectGuid Housing::GetExteriorLockHolder() const
{
    auto guard = LockState();
    return _state->ExteriorLockHolder;
}

bool Housing::ReleaseExteriorLock(ObjectGuid playerGuid)
{
    auto guard = LockState();
    if (playerGuid.IsEmpty() || _state->ExteriorLockHolder != playerGuid)
        return false;

    _state->ExteriorLockHolder.Clear();
    return true;
}

void Housing::SetHouseSize(uint8 size)
{
    auto guard = LockState();
    _state->HouseSize = size;

    // Immediate persist for crash safety
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_HOUSE_SIZE);
    stmt->setUInt8(0, size);
    stmt->setUInt64(1, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    SyncUpdateFields();

    TC_LOG_DEBUG("housing", "Housing::SetHouseSize: Player {} set house {} size to {}",
        _owner->GetName(), _state->HouseGuid.ToString(), size);
}

void Housing::SetHouseType(uint32 typeId)
{
    auto guard = LockState();
    _state->HouseType = typeId;

    // Immediate persist for crash safety
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_HOUSE_TYPE);
    stmt->setUInt32(0, typeId);
    stmt->setUInt64(1, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    SyncUpdateFields();

    TC_LOG_DEBUG("housing", "Housing::SetHouseType: Player {} set house {} type to {}",
        _owner->GetName(), _state->HouseGuid.ToString(), typeId);
}

ObjectGuid Housing::GetExteriorRootGuid() const
{
    if (IsPacked() || GetPlotIndex() == INVALID_PLOT_INDEX)
        return ObjectGuid::Empty;

    Neighborhood const* neighborhood = sNeighborhoodMgr.GetNeighborhood(GetNeighborhoodGuid());
    uint32 const worldMapId = neighborhood ? sHousingMgr.GetWorldMapIdByNeighborhoodMapId(neighborhood->GetNeighborhoodMapID()) : 0;
    if (!worldMapId)
        return ObjectGuid::Empty;

    return HousingMgr::MakeExteriorRootGuid(worldMapId, GetPlotIndex());
}

ObjectGuid Housing::GetHouseEntityTargetFor(Player const* viewer) const
{
    ObjectGuid const root = GetExteriorRootGuid();
    if (root.IsEmpty() || !viewer)
        return ObjectGuid::Empty;

    // Every neighborhood of a world map is its own instance of that map, and the root's GUID is made from the world
    // map and the plot alone, so the same GUID names another house's root in another neighborhood. Only the house's
    // own neighborhood counts.
    HousingMap const* housingMap = dynamic_cast<HousingMap const*>(viewer->FindMap());
    if (!housingMap || housingMap->GetId() != root.GetMapId() || !housingMap->GetNeighborhood()
        || housingMap->GetNeighborhood()->GetGuid() != GetNeighborhoodGuid())
        return ObjectGuid::Empty;

    return root;
}

void Housing::SetHousePosition(float x, float y, float z, float facing)
{
    auto guard = LockState();
    _state->HousePosX = x;
    _state->HousePosY = y;
    _state->HousePosZ = z;
    _state->HouseFacing = facing;
    _state->HasCustomPosition = true;

    // Immediate persist for crash safety
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_POSITION);
    stmt->setFloat(0, x);
    stmt->setFloat(1, y);
    stmt->setFloat(2, z);
    stmt->setFloat(3, facing);
    stmt->setUInt64(4, GetDatabaseId());
    CharacterDatabase.Execute(stmt);

    // The plot's copy of the house builds it while no character of the account is on the map.
    if (Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(_state->NeighborhoodGuid))
        neighborhood->UpdatePlotHousePlacementByHouse(_state->HouseGuid, Position(x, y, z, facing));

    TC_LOG_DEBUG("housing", "Housing::SetHousePosition: Player {} positioned house at ({}, {}, {}, {}) in house {}",
        _owner->GetName(), x, y, z, facing, _state->HouseGuid.ToString());
}

uint64 Housing::GenerateRoomDbId()
{
    return s_nextRoomDbId.fetch_add(1);
}

void Housing::PersistRoomToDB(ObjectGuid roomGuid, Room const& room)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_ROOM);
    uint8 index = 0;
    stmt->setUInt32(index++, room.SlotIndex);
    stmt->setInt32(index++, room.GridX);
    stmt->setInt32(index++, room.GridY);
    stmt->setInt32(index++, room.FloorIndex);
    stmt->setUInt8(index++, uint8(room.Orientation));
    stmt->setUInt8(index++, room.Mirrored ? 1 : 0);
    stmt->setUInt32(index++, room.ThemeId);
    stmt->setUInt32(index++, room.WallTextureId);
    stmt->setUInt32(index++, room.FloorTextureId);
    stmt->setUInt32(index++, room.CeilingTextureId);
    stmt->setInt32(index++, room.ColorOverride);
    stmt->setUInt32(index++, room.DoorTypeId);
    stmt->setUInt8(index++, room.DoorSlot);
    stmt->setUInt32(index++, room.CeilingTypeId);
    stmt->setUInt8(index++, room.CeilingSlot);
    stmt->setUInt32(index++, room.WallThemeId);
    stmt->setUInt32(index++, room.FloorThemeId);
    stmt->setUInt32(index++, room.CeilingThemeId);
    stmt->setUInt64(index++, GetDatabaseId());
    stmt->setUInt64(index++, roomGuid.GetCounter());
    CharacterDatabase.Execute(stmt);
}

void Housing::PersistFixtureToDB(uint32 fixturePointId, uint32 optionId)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_FIXTURE);
    stmt->setUInt32(0, optionId);
    stmt->setUInt64(1, GetDatabaseId());
    stmt->setUInt32(2, fixturePointId);
    CharacterDatabase.Execute(stmt);
}

void Housing::PopulateStarterFixtures(bool persistNow)
{
    auto guard = LockState();
    // Starter house = Base(9) + Roof(10) as root components.
    // Door(11) auto-resolves from hook system via GetDefaultFixtureForType.
    // Root components are stored as { FixturePointId = componentID, OptionId = 0 }.
    // Only add types that don't already have a valid root in _state->Fixtures.
    static constexpr uint8 starterTypes[] = { HOUSING_FIXTURE_TYPE_BASE, HOUSING_FIXTURE_TYPE_ROOF };

    // Determine which root types already exist
    std::unordered_set<uint8> existingRootTypes;
    for (auto const& [pointId, fix] : _state->Fixtures)
    {
        if (fix.OptionId != 0)
            continue;
        ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(fix.FixturePointId);
        if (!comp)
            continue;
        if (_state->HouseType != 0 && comp->HouseExteriorWmoDataID != static_cast<uint32>(_state->HouseType))
            continue;
        existingRootTypes.insert(comp->Type);
    }

    uint64 houseDatabaseId = GetDatabaseId();

    for (uint8 fixtureType : starterTypes)
    {
        if (existingRootTypes.count(fixtureType))
        {
            TC_LOG_INFO("housing", "Housing::PopulateStarterFixtures: type={} already has a root — skipping",
                fixtureType);
            continue;
        }

        uint32 compID = sHousingMgr.GetDefaultFixtureForType(fixtureType, _state->HouseType, _state->HouseSize);
        if (!compID)
        {
            TC_LOG_ERROR("housing", "Housing::PopulateStarterFixtures: No default component for type={} wmo={} size={} — skipping",
                fixtureType, _state->HouseType, _state->HouseSize);
            continue;
        }

        // Insert into in-memory map
        Fixture& fixture = _state->Fixtures[compID];
        fixture.FixturePointId = compID;
        fixture.OptionId = 0;

        if (persistNow)
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_HOUSING_FIXTURES);
            uint8 index = 0;
            stmt->setUInt64(index++, houseDatabaseId);
            stmt->setUInt32(index++, compID);
            stmt->setUInt32(index++, 0); // OptionId = 0 (default)
            CharacterDatabase.Execute(stmt);
        }

        TC_LOG_INFO("housing", "Housing::PopulateStarterFixtures: Added type={} compID={} wmo={} for player {}",
            fixtureType, compID, _state->HouseType, _owner->GetName());
    }

    // --- Starter door ---
    // Every new house starts with a door at the first door hook on the base component.
    // Check if a door fixture already exists (any hook-based fixture with a door component).
    bool hasDoor = false;
    for (auto const& [pointId, fix] : _state->Fixtures)
    {
        if (fix.OptionId == 0)
            continue;
        ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(fix.OptionId);
        if (comp && comp->Type == HOUSING_FIXTURE_TYPE_DOOR)
        {
            hasDoor = true;
            break;
        }
    }

    if (!hasDoor)
    {
        // Find the base component to get its door hooks
        uint32 baseCompID = 0;
        for (auto const& [pointId, fix] : _state->Fixtures)
        {
            if (fix.OptionId != 0)
                continue;
            ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(fix.FixturePointId);
            if (comp && comp->Type == HOUSING_FIXTURE_TYPE_BASE)
            {
                baseCompID = fix.FixturePointId;
                break;
            }
        }

        if (baseCompID)
        {
            auto const* hooks = sHousingMgr.GetHooksOnComponent(baseCompID);
            if (hooks)
            {
                // Find the first door hook (ExteriorComponentTypeID == 11)
                uint32 doorHookID = 0;
                for (ExteriorComponentHookEntry const* hook : *hooks)
                {
                    if (hook && hook->ExteriorComponentTypeID == HOUSING_FIXTURE_TYPE_DOOR)
                    {
                        doorHookID = hook->ID;
                        break;
                    }
                }

                if (doorHookID)
                {
                    uint32 doorCompID = sHousingMgr.GetDefaultFixtureForType(HOUSING_FIXTURE_TYPE_DOOR, _state->HouseType, _state->HouseSize);
                    if (doorCompID)
                    {
                        Fixture& doorFixture = _state->Fixtures[doorHookID];
                        doorFixture.FixturePointId = doorHookID;
                        doorFixture.OptionId = doorCompID;

                        if (persistNow)
                        {
                            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_HOUSING_FIXTURES);
                            uint8 index = 0;
                            stmt->setUInt64(index++, houseDatabaseId);
                            stmt->setUInt32(index++, doorHookID);
                            stmt->setUInt32(index++, doorCompID);
                            CharacterDatabase.Execute(stmt);
                        }

                        TC_LOG_INFO("housing", "Housing::PopulateStarterFixtures: Added starter door compID={} at hookID={} for player {}",
                            doorCompID, doorHookID, _owner->GetName());
                    }
                    else
                    {
                        TC_LOG_ERROR("housing", "Housing::PopulateStarterFixtures: No default door component for wmo={} size={}", _state->HouseType, _state->HouseSize);
                    }
                }
                else
                {
                    TC_LOG_ERROR("housing", "Housing::PopulateStarterFixtures: No door hooks found on base component {}", baseCompID);
                }
            }
        }
    }
}

Housing::Room const* Housing::GetRoom(ObjectGuid roomGuid) const
{
    auto itr = _state->Rooms.find(roomGuid);
    return itr != _state->Rooms.end() ? &itr->second : nullptr;
}

uint32 Housing::GetNextRoomSlotIndex() const
{
    uint32 nextSlot = 0;
    for (auto const& [guid, room] : _state->Rooms)
        nextSlot = std::max(nextSlot, room.SlotIndex + 1);
    return nextSlot;
}

void Housing::RemoveAllNonBaseRooms()
{
    auto guard = LockState();
    for (auto itr = _state->Rooms.begin(); itr != _state->Rooms.end();)
    {
        HouseRoomData const* roomData = sHousingMgr.GetHouseRoomData(itr->second.RoomEntryId);
        if (roomData && roomData->IsBaseRoom())
            ++itr;
        else
            itr = _state->Rooms.erase(itr);
    }

    RecalculateBudgets();
    SyncUpdateFields();
}

void Housing::SetRoomAppearance(ObjectGuid roomGuid, Room const& appearance)
{
    auto guard = LockState();
    auto itr = _state->Rooms.find(roomGuid);
    if (itr == _state->Rooms.end())
        return;

    Room& room = itr->second;
    room.ThemeId = appearance.ThemeId;
    room.WallThemeId = appearance.WallThemeId;
    room.FloorThemeId = appearance.FloorThemeId;
    room.CeilingThemeId = appearance.CeilingThemeId;
    room.WallTextureId = appearance.WallTextureId;
    room.FloorTextureId = appearance.FloorTextureId;
    room.CeilingTextureId = appearance.CeilingTextureId;
    room.ColorOverride = appearance.ColorOverride;
    room.DoorTypeId = appearance.DoorTypeId;
    room.DoorSlot = appearance.DoorSlot;
    room.CeilingTypeId = appearance.CeilingTypeId;
    room.CeilingSlot = appearance.CeilingSlot;
    PersistRoomToDB(roomGuid, room);
}

void Housing::ReplaceFixtures(std::vector<Fixture> const& fixtures)
{
    auto guard = LockState();
    _state->Fixtures.clear();
    for (Fixture const& fixture : fixtures)
        _state->Fixtures[fixture.FixturePointId] = fixture;

    SyncUpdateFields();
}

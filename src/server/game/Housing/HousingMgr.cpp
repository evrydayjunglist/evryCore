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

#include "HousingMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "GameObjectData.h"
#include "Group.h"
#include "Guild.h"
#include "Housing.h"
#include "HousingDefines.h"
#include "Log.h"
#include "Neighborhood.h"
#include "NeighborhoodMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuaternionData.h"
#include "RaceMask.h"
#include "Random.h"
#include "SharedDefines.h"
#include "SocialMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace
{
    char const* SafeStr(char const* str) { return str ? str : ""; }
}

HousingMgr::HousingMgr() = default;
HousingMgr::~HousingMgr() = default;

HousingMgr& HousingMgr::Instance()
{
    static HousingMgr instance;
    return instance;
}

void HousingMgr::Initialize()
{
    uint32 oldMSTime = getMSTime();

    LoadHouseDecorData();
    LoadHouseLevelData();
    LoadHouseRoomData();
    LoadHouseThemeData();
    LoadHouseDecorThemeSetData();
    LoadNeighborhoodMapData();
    LoadNeighborhoodPlotData();
    LoadNeighborhoodNameGenData();
    LoadHouseDecorMaterialData();
    LoadHouseExteriorWmoData();
    LoadHouseLevelRewardInfoData();
    LoadNeighborhoodInitiativeData();
    LoadRoomComponentData();
    LoadDecorCategoryData();
    LoadDecorSubcategoryData();
    LoadDecorDyeSlotData();
    LoadDecorXDecorSubcategoryData();
    BuildRoomComponentOptionIndex();
    BuildExteriorComponentIndexes();
    BuildRoomComponentTextureIndex();
    DumpExteriorComponentDiagnostics();
    DumpRoomComponentTextureDiagnostics();
    EnsureDoorGameObjectTemplates();

    // Initialize global DB ID generators from MAX(id) in character_housing_rooms/decor.
    // Must happen before any Housing objects are loaded to prevent cross-player ID collisions.
    Housing::InitializeDbIdGenerators();

    // Scan for base room entry from DB2 data (look for IsBaseRoom flag)
    for (auto const& [id, roomData] : _houseRoomStore)
    {
        if (roomData.IsBaseRoom())
        {
            _baseRoomEntryId = id;
            TC_LOG_INFO("housing", "HousingMgr::Initialize: Base room entry from DB2 flag: {} ('{}')",
                id, roomData.Name);
            break;
        }
    }
    if (!_baseRoomEntryId)
    {
        _baseRoomEntryId = 18; // fallback
        TC_LOG_WARN("housing", "HousingMgr::Initialize: No room with BASE_ROOM flag found, "
            "falling back to entry 18");
    }

    // Scan for interior entry hall room (second BASE_ROOM after exterior geobox).
    // Retail has two base rooms: Room 18 (exterior geobox for SpawnRoomForPlot) and
    // Room 46 (interior entry corridor connecting to the visual room via a door).
    for (auto const& [id, roomData] : _houseRoomStore)
    {
        if (roomData.IsBaseRoom() && id != _baseRoomEntryId)
        {
            _entryHallRoomEntryId = id;
            TC_LOG_INFO("housing", "HousingMgr::Initialize: Entry hall room entry from DB2: {} ('{}')",
                id, roomData.Name);
            break;
        }
    }
    if (!_entryHallRoomEntryId)
    {
        _entryHallRoomEntryId = _baseRoomEntryId;
        TC_LOG_WARN("housing", "HousingMgr::Initialize: No second BASE_ROOM found for entry hall, "
            "falling back to base room entry {}", _baseRoomEntryId);
    }

    // Room grid spacing (sniff-verified: ~24 yards between room centers, matching WMO bounding boxes)
    _roomGridSpacing = HOUSING_ROOM_GRID_SPACING;
    if (_baseRoomEntryId)
    {
        HouseRoomData const* baseRoom = GetHouseRoomData(_baseRoomEntryId);
        if (baseRoom)
        {
            RoomWmoDataEntry const* wmo = baseRoom->RoomWmoDataID
                ? sRoomWmoDataStore.LookupEntry(baseRoom->RoomWmoDataID) : nullptr;
            if (wmo)
            {
                float bbWidth = wmo->BoundingBoxMaxX - wmo->BoundingBoxMinX;
                float bbDepth = wmo->BoundingBoxMaxY - wmo->BoundingBoxMinY;
                TC_LOG_INFO("housing", "HousingMgr::Initialize: Room grid spacing = {:.1f}yd, "
                    "base room WMO bbox = ({:.1f},{:.1f},{:.1f})->({:.1f},{:.1f},{:.1f}), "
                    "width={:.1f} depth={:.1f}",
                    _roomGridSpacing,
                    wmo->BoundingBoxMinX, wmo->BoundingBoxMinY, wmo->BoundingBoxMinZ,
                    wmo->BoundingBoxMaxX, wmo->BoundingBoxMaxY, wmo->BoundingBoxMaxZ,
                    bbWidth, bbDepth);
            }
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded housing data: {} decor, {} levels, "
        "{} rooms, {} themes, {} decor materials, {} exterior wmos, {} level rewards, "
        "{} initiatives, {} neighborhood maps, {} neighborhood plots, "
        "{} decor categories, {} decor subcategories, {} decor dye slots, "
        "{} room component options in {}",
        uint32(_houseDecorStore.size()), uint32(_houseLevelDataStore.size()),
        uint32(_houseRoomStore.size()), uint32(_houseThemeStore.size()),
        uint32(_houseDecorMaterialStore.size()), uint32(_houseExteriorWmoStore.size()),
        uint32(_houseLevelRewardInfoStore.size()), uint32(_neighborhoodInitiativeStore.size()),
        uint32(_neighborhoodMapStore.size()), uint32(_neighborhoodPlotStore.size()),
        uint32(_decorCategoryStore.size()), uint32(_decorSubcategoryStore.size()),
        uint32(_decorDyeSlotStore.size()),
        uint32(sRoomComponentOptionStore.GetNumRows()),
        GetMSTimeDiffToNow(oldMSTime));
}

void HousingMgr::LoadHouseDecorData()
{
    for (HouseDecorEntry const* entry : sHouseDecorStore)
    {
        HouseDecorData& data = _houseDecorStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.InitialRotation[0] = entry->InitialRotation.X;
        data.InitialRotation[1] = entry->InitialRotation.Y;
        data.InitialRotation[2] = entry->InitialRotation.Z;
        data.GameObjectID = entry->GameObjectID;
        data.Flags = entry->Flags;
        data.Type = entry->Type;
        data.ModelType = entry->ModelType;
        data.ModelFileDataID = entry->ModelFileDataID;
        data.ThumbnailFileDataID = entry->ThumbnailFileDataID;
        data.WeightCost = entry->WeightCost > 0 ? entry->WeightCost : 1;
        data.ItemID = entry->ItemID;
        data.InitialScale = entry->InitialScale;
        data.FirstAcquisitionBonus = entry->FirstAcquisitionBonus;
        data.OrderIndex = entry->OrderIndex;
        data.Size = entry->Size;
        data.StartingQuantity = entry->StartingQuantity;
        data.UiModelSceneID = entry->UiModelSceneID;
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadHouseDecorData: Loaded {} HouseDecor entries", uint32(_houseDecorStore.size()));
}

void HousingMgr::LoadHouseLevelData()
{
    for (HouseLevelDataEntry const* entry : sHouseLevelDataStore)
    {
        HouseLevelData& data = _houseLevelDataStore[entry->ID];
        data.ID = entry->ID;
        data.Level = entry->Level;
        data.QuestID = entry->QuestID;
        // Budget values will be populated from HouseLevelRewardInfo DB2 (RewardType 38-41)
        // after LoadHouseLevelRewardInfoData(). Initialize to 0 here; fallbacks applied later.
        data.InteriorDecorPlacementBudget = 0;
        data.ExteriorDecorPlacementBudget = 0;
        data.RoomPlacementBudget = 0;
        data.ExteriorFixtureBudget = 0;
    }

    // Fallback defaults if no DB2 data available
    if (_houseLevelDataStore.empty())
    {
        for (uint32 level = 1; level <= 10; ++level)
        {
            HouseLevelData& data = _houseLevelDataStore[level];
            data.ID = level;
            data.Level = static_cast<int32>(level);
            data.QuestID = 0;
            data.InteriorDecorPlacementBudget = 0;
            data.ExteriorDecorPlacementBudget = 0;
            data.RoomPlacementBudget = 0;
            data.ExteriorFixtureBudget = 0;
        }
    }

    // Build level lookup index (indexed by Level value, not by DB2 row ID)
    for (auto& [id, entry] : _houseLevelDataStore)
        _levelDataByLevel[entry.Level] = &entry;

    TC_LOG_DEBUG("housing", "HousingMgr::LoadHouseLevelData: Loaded {} HouseLevelData entries", uint32(_houseLevelDataStore.size()));
}

void HousingMgr::LoadHouseRoomData()
{
    for (HouseRoomEntry const* entry : sHouseRoomStore)
    {
        HouseRoomData& data = _houseRoomStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.Size = entry->Size;
        data.Flags = entry->Flags;
        data.Field_002 = entry->Field_002;
        data.RoomWmoDataID = entry->RoomWmoDataID;
        data.UiTextureAtlasElementID = entry->UiTextureAtlasElementID;
        data.WeightCost = entry->WeightCost > 0 ? entry->WeightCost : 1;
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadHouseRoomData: Loaded {} HouseRoom entries", uint32(_houseRoomStore.size()));
}

void HousingMgr::LoadHouseThemeData()
{
    for (HouseThemeEntry const* entry : sHouseThemeStore)
    {
        HouseThemeData& data = _houseThemeStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.Flags = entry->Flags;
        data.ParentThemeID = entry->ParentThemeID;
    }

    // Log the theme hierarchy for diagnostic purposes
    for (auto const& [id, data] : _houseThemeStore)
    {
        if (data.ParentThemeID != 0)
            TC_LOG_DEBUG("housing", "  HouseTheme ID={} Name='{}' -> base={}", id, data.Name, data.ParentThemeID);
    }
    TC_LOG_DEBUG("housing", "HousingMgr::LoadHouseThemeData: Loaded {} HouseTheme entries", uint32(_houseThemeStore.size()));
}

void HousingMgr::LoadHouseDecorThemeSetData()
{
    for (HouseDecorThemeSetEntry const* entry : sHouseDecorThemeSetStore)
    {
        HouseDecorThemeSetData& data = _houseDecorThemeSetStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.HouseThemeID = entry->ThemeID;
        data.HouseDecorCategoryID = entry->IconFileDataID;
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadHouseDecorThemeSetData: Loaded {} HouseDecorThemeSet entries", uint32(_houseDecorThemeSetStore.size()));
}

void HousingMgr::LoadNeighborhoodMapData()
{
    for (NeighborhoodMapEntry const* entry : sNeighborhoodMapStore)
    {
        NeighborhoodMapData& data = _neighborhoodMapStore[entry->ID];
        data.ID = entry->ID;
        data.Origin[0] = entry->Position.X;
        data.Origin[1] = entry->Position.Y;
        data.Origin[2] = entry->Position.Z;
        data.MapID = entry->MapID;
        data.EntryRotation = entry->EntryRotation;
        data.UiTextureKitID = entry->UiTextureKitID;
        data.Flags = entry->Flags;
    }

    // Build reverse lookup: world MapID -> NeighborhoodMap ID
    for (auto const& [id, data] : _neighborhoodMapStore)
    {
        _worldMapToNeighborhoodMap[data.MapID] = id;
        // NeighborhoodMapFlags (IDA-confirmed): AlliancePurchasable=0x1, HordePurchasable=0x2, CanSystemGenerate=0x4
        TC_LOG_DEBUG("housing", "  NeighborhoodMap ID={} MapID={} EntryRotation={} UiTextureKitID={} Flags=0x{:X} (Alliance={} Horde={} SysGen={})",
            data.ID, data.MapID, data.EntryRotation, data.UiTextureKitID, data.Flags,
            (data.Flags & 0x1) != 0, (data.Flags & 0x2) != 0, (data.Flags & 0x4) != 0);
    }

    TC_LOG_INFO("housing", "HousingMgr::LoadNeighborhoodMapData: Loaded {} NeighborhoodMap entries", uint32(_neighborhoodMapStore.size()));
}

void HousingMgr::LoadNeighborhoodPlotData()
{
    for (NeighborhoodPlotEntry const* entry : sNeighborhoodPlotStore)
    {
        NeighborhoodPlotData& data = _neighborhoodPlotStore[entry->ID];
        data.ID = entry->ID;
        data.Cost = entry->Cost;
        data.Name = entry->Name ? entry->Name : "";
        data.HousePosition[0] = entry->HousePosition.X;
        data.HousePosition[1] = entry->HousePosition.Y;
        data.HousePosition[2] = entry->HousePosition.Z;
        data.HouseRotation[0] = entry->HouseRotation.X;
        data.HouseRotation[1] = entry->HouseRotation.Y;
        data.HouseRotation[2] = entry->HouseRotation.Z;
        data.CornerstonePosition[0] = entry->CornerstonePosition.X;
        data.CornerstonePosition[1] = entry->CornerstonePosition.Y;
        data.CornerstonePosition[2] = entry->CornerstonePosition.Z;
        data.CornerstoneRotation[0] = entry->CornerstoneRotation.X;
        data.CornerstoneRotation[1] = entry->CornerstoneRotation.Y;
        data.CornerstoneRotation[2] = entry->CornerstoneRotation.Z;
        data.TeleportPosition[0] = entry->TeleportPosition.X;
        data.TeleportPosition[1] = entry->TeleportPosition.Y;
        data.TeleportPosition[2] = entry->TeleportPosition.Z;
        data.NeighborhoodMapID = entry->NeighborhoodMapID;
        data.Field_010 = entry->Field_010;
        data.CornerstoneGameObjectID = entry->CornerstoneGameObjectID;
        data.PlotIndex = entry->PlotIndex;
        data.WorldState = entry->WorldState;
        data.PlotGameObjectID = entry->PlotGameObjectID;
        data.TeleportFacing = entry->TeleportFacing;
        data.Field_016 = entry->Field_016;
    }

    // Build map index
    for (auto const& [id, plot] : _neighborhoodPlotStore)
        _plotsByMap[plot.NeighborhoodMapID].push_back(&plot);

    TC_LOG_INFO("housing", "HousingMgr::LoadNeighborhoodPlotData: Loaded {} NeighborhoodPlot entries across {} maps",
        uint32(_neighborhoodPlotStore.size()), uint32(_plotsByMap.size()));

    // Dump per-map plot counts and sample GO entries for debugging
    for (auto const& [mapId, plotVec] : _plotsByMap)
    {
        uint32 hasForSale = 0, hasCornerstone = 0;
        for (auto const* p : plotVec)
        {
            if (p->PlotGameObjectID) ++hasForSale;
            if (p->CornerstoneGameObjectID) ++hasCornerstone;
        }
        TC_LOG_INFO("housing", "  NeighborhoodMapID={}: {} plots, {} with ForSaleGO, {} with CornerstoneGO",
            mapId, uint32(plotVec.size()), hasForSale, hasCornerstone);

        // Log all plots with their WorldState IDs
        for (auto const* p : plotVec)
        {
            TC_LOG_INFO("housing", "    Plot[{}]: ID={} ForSaleGO={} CornerstoneGO={} WorldState={} Cost={} HousePos=({:.4f}, {:.4f}, {:.4f}) HouseRot=({:.4f}, {:.4f}, {:.4f})",
                p->PlotIndex, p->ID, p->PlotGameObjectID, p->CornerstoneGameObjectID,
                p->WorldState, p->Cost,
                p->HousePosition[0], p->HousePosition[1], p->HousePosition[2],
                p->HouseRotation[0], p->HouseRotation[1], p->HouseRotation[2]);
        }
    }

    // Every cornerstone is the shared entry 457142, whose template comes from the world database. The plots'
    // CornerstoneGameObjectID values are not gameobject entries on Razorwind Shores (retail uses them only as the
    // cornerstone's CreatedBy counter), so no template is made up for them.
    if (!sObjectMgr->GetGameObjectTemplate(GAMEOBJECT_HOUSING_CORNERSTONE))
        TC_LOG_ERROR("housing", "HousingMgr::LoadNeighborhoodPlotData: gameobject_template {} (the plot cornerstone) is missing; "
            "neighborhoods will have no cornerstones", GAMEOBJECT_HOUSING_CORNERSTONE);

    // Validate that all PlotGameObjectID entries have matching gameobject_template entries.
    // Templates are loaded from GameObjects.db2 (CASC) + gameobject_template SQL table.
    uint32 missingPlotGO = 0;
    for (auto const& [id, plot] : _neighborhoodPlotStore)
    {
        if (plot.PlotGameObjectID)
        {
            uint32 entry = static_cast<uint32>(plot.PlotGameObjectID);
            if (!sObjectMgr->GetGameObjectTemplate(entry))
            {
                // Register missing plot marker template.
                // All plot markers are identical: type=5 (Generic), displayId=113004, scale=1.0
                GameObjectTemplate& got = const_cast<ObjectMgr*>(sObjectMgr)->GetGameObjectTemplateStoreForHotfix()[entry];
                got.entry = entry;
                got.type = 5; // GAMEOBJECT_TYPE_GENERIC
                got.displayId = 113004;
                got.name = Trinity::StringFormat("Plot {} Map {}", plot.PlotIndex, plot.NeighborhoodMapID);
                got.size = 1.0f;
                memset(got.raw.data, 0, sizeof(got.raw.data));
                got.raw.data[1] = 1; // Data1
                got.ContentTuningId = 0;
                got.RequiredLevel = 0;
                got.ScriptId = 0;
                got.InitializeQueryData();
                ++missingPlotGO;
            }
        }
    }

    if (missingPlotGO > 0)
        TC_LOG_ERROR("housing", "HousingMgr::LoadNeighborhoodPlotData: {} plot marker GO templates were missing from "
            "gameobject_template and GameObjects.db2 and were registered in memory.", missingPlotGO);
}

void HousingMgr::LoadNeighborhoodNameGenData()
{
    for (NeighborhoodNameGenEntry const* entry : sNeighborhoodNameGenStore)
    {
        NeighborhoodNameGenData data;
        data.ID = entry->ID;
        data.Prefix = SafeStr(entry->Prefix[sWorld->GetDefaultDbcLocale()]);
        data.Middle = SafeStr(entry->Middle[sWorld->GetDefaultDbcLocale()]);
        data.Suffix = SafeStr(entry->Suffix[sWorld->GetDefaultDbcLocale()]);
        data.NeighborhoodMapID = entry->NeighborhoodMapID;
        _nameGenByMap[entry->NeighborhoodMapID].push_back(std::move(data));
    }

    uint32 totalEntries = 0;
    for (auto const& [mapId, entries] : _nameGenByMap)
        totalEntries += static_cast<uint32>(entries.size());

    TC_LOG_INFO("housing", "HousingMgr::LoadNeighborhoodNameGenData: Loaded {} entries across {} maps from base DB2",
        totalEntries, uint32(_nameGenByMap.size()));
}

HouseDecorData const* HousingMgr::GetHouseDecorData(uint32 id) const
{
    auto itr = _houseDecorStore.find(id);
    if (itr != _houseDecorStore.end())
        return &itr->second;

    return nullptr;
}

HouseLevelData const* HousingMgr::GetLevelData(uint32 level) const
{
    auto itr = _levelDataByLevel.find(level);
    if (itr != _levelDataByLevel.end())
        return itr->second;

    return nullptr;
}

HouseRoomData const* HousingMgr::GetHouseRoomData(uint32 id) const
{
    auto itr = _houseRoomStore.find(id);
    if (itr != _houseRoomStore.end())
        return &itr->second;

    return nullptr;
}

HouseThemeData const* HousingMgr::GetHouseThemeData(uint32 id) const
{
    auto itr = _houseThemeStore.find(id);
    if (itr != _houseThemeStore.end())
        return &itr->second;

    return nullptr;
}

HouseDecorThemeSetData const* HousingMgr::GetHouseDecorThemeSetData(uint32 id) const
{
    auto itr = _houseDecorThemeSetStore.find(id);
    if (itr != _houseDecorThemeSetStore.end())
        return &itr->second;

    return nullptr;
}

NeighborhoodMapData const* HousingMgr::GetNeighborhoodMapData(uint32 id) const
{
    auto itr = _neighborhoodMapStore.find(id);
    if (itr != _neighborhoodMapStore.end())
        return &itr->second;

    return nullptr;
}

NeighborhoodMapData const* HousingMgr::GetNeighborhoodMapDataForWorldMap(uint32 mapId) const
{
    uint32 nmId = GetNeighborhoodMapIdByWorldMap(mapId);
    return nmId ? GetNeighborhoodMapData(nmId) : nullptr;
}

bool HousingMgr::IsNeighborhoodWorldMap(uint32 mapId) const
{
    return _worldMapToNeighborhoodMap.contains(static_cast<int32>(mapId));
}

uint32 HousingMgr::GetNeighborhoodMapIdByWorldMap(uint32 mapId) const
{
    auto itr = _worldMapToNeighborhoodMap.find(static_cast<int32>(mapId));
    if (itr != _worldMapToNeighborhoodMap.end())
        return itr->second;
    return 0;
}

uint32 HousingMgr::GetWorldMapIdByNeighborhoodMapId(uint32 neighborhoodMapId) const
{
    for (auto const& [worldMapId, nmId] : _worldMapToNeighborhoodMap)
    {
        if (nmId == neighborhoodMapId)
            return static_cast<uint32>(worldMapId);
    }
    return 0;
}

std::vector<NeighborhoodPlotData const*> HousingMgr::GetPlotsForMap(uint32 neighborhoodMapId) const
{
    auto itr = _plotsByMap.find(neighborhoodMapId);
    if (itr != _plotsByMap.end())
        return itr->second;

    TC_LOG_ERROR("housing", "HousingMgr::GetPlotsForMap: No plots found for neighborhoodMapId={}. Available map IDs:", neighborhoodMapId);
    for (auto const& [id, vec] : _plotsByMap)
        TC_LOG_ERROR("housing", "  neighborhoodMapId={} ({} plots)", id, uint32(vec.size()));

    return {};
}

/*static*/ bool HousingMgr::GetRoomAnchor(GameObjectsEntry const* plotRow, uint32 worldMapId, Position& position, QuaternionData& rotation)
{
    if (!plotRow || plotRow->OwnerID != worldMapId)
        return false;

    QuaternionData const rowRotation(plotRow->Rot[0], plotRow->Rot[1], plotRow->Rot[2], plotRow->Rot[3]);
    float rowOrientation = 0.0f, pitch = 0.0f, roll = 0.0f;
    rowRotation.toEulerAnglesZYX(rowOrientation, pitch, roll);

    // The rows of both neighborhoods turn about the vertical axis only, so the room is too.
    float const orientation = Position::NormalizeOrientation(rowOrientation + float(M_PI));
    position.Relocate(plotRow->Pos.X, plotRow->Pos.Y, plotRow->Pos.Z, orientation);
    rotation = QuaternionData::fromEulerAnglesZYX(orientation, 0.0f, 0.0f);
    return true;
}

bool HousingMgr::GetPlotRoomAnchor(uint32 neighborhoodMapId, uint8 plotIndex, Position& position, QuaternionData& rotation) const
{
    NeighborhoodPlotData const* plot = GetPlot(neighborhoodMapId, plotIndex);
    uint32 const worldMapId = GetWorldMapIdByNeighborhoodMapId(neighborhoodMapId);
    if (!plot || !worldMapId || plot->PlotGameObjectID <= 0)
        return false;

    return GetRoomAnchor(sGameObjectsStore.LookupEntry(uint32(plot->PlotGameObjectID)), worldMapId, position, rotation);
}

bool HousingMgr::GetPlotRoomAnchor(uint32 neighborhoodMapId, uint8 plotIndex, Position& position) const
{
    QuaternionData unusedRotation;
    return GetPlotRoomAnchor(neighborhoodMapId, plotIndex, position, unusedRotation);
}

/*static*/ void HousingMgr::ComposeAttachment(Position const& parentPos, QuaternionData const& parentRot,
    Position const& localPos, QuaternionData const& localRot, Position& worldPos, QuaternionData& worldRot)
{
    // The offset turned by the parent's rotation: v + 2w(q x v) + 2(q x (q x v)), with q the rotation's vector part.
    float const qx = parentRot.x, qy = parentRot.y, qz = parentRot.z, qw = parentRot.w;
    float const vx = localPos.GetPositionX(), vy = localPos.GetPositionY(), vz = localPos.GetPositionZ();
    float const tx = 2.0f * (qy * vz - qz * vy);
    float const ty = 2.0f * (qz * vx - qx * vz);
    float const tz = 2.0f * (qx * vy - qy * vx);
    float const ox = vx + qw * tx + (qy * tz - qz * ty);
    float const oy = vy + qw * ty + (qz * tx - qx * tz);
    float const oz = vz + qw * tz + (qx * ty - qy * tx);

    // The parent's rotation followed by the local one.
    worldRot = QuaternionData(
        qw * localRot.x + qx * localRot.w + qy * localRot.z - qz * localRot.y,
        qw * localRot.y - qx * localRot.z + qy * localRot.w + qz * localRot.x,
        qw * localRot.z + qx * localRot.y - qy * localRot.x + qz * localRot.w,
        qw * localRot.w - qx * localRot.x - qy * localRot.y - qz * localRot.z);

    float orientation = 0.0f, pitch = 0.0f, roll = 0.0f;
    worldRot.toEulerAnglesZYX(orientation, pitch, roll);
    worldPos.Relocate(parentPos.GetPositionX() + ox, parentPos.GetPositionY() + oy, parentPos.GetPositionZ() + oz,
        Position::NormalizeOrientation(orientation));
}

/*static*/ QuaternionData HousingMgr::GetHookRotation(ExteriorComponentHookEntry const& hook)
{
    constexpr float DegreesToRadians = float(M_PI / 180.0);
    return QuaternionData::fromEulerAnglesZYX(-hook.Rotation[2] * DegreesToRadians, -hook.Rotation[1] * DegreesToRadians,
        -hook.Rotation[0] * DegreesToRadians);
}

/*static*/ ObjectGuid HousingMgr::MakeExteriorRootGuid(uint32 worldMapId, uint8 plotIndex)
{
    return ObjectGuid::Create<HighGuid::Entity>(uint16(worldMapId), 0, HOUSING_EXTERIOR_ROOT_GUID_COUNTER_BASE + plotIndex);
}

/*static*/ bool HousingMgr::IsRootPlacementInRoom(Position const& placement)
{
    return placement.IsPositionValid() && std::isfinite(placement.GetOrientation())
        && std::fabs(placement.GetPositionX()) <= HOUSING_ROOT_MAX_LOCAL_X
        && std::fabs(placement.GetPositionY()) <= HOUSING_ROOT_MAX_LOCAL_Y
        && placement.GetPositionZ() >= HOUSING_ROOT_MIN_LOCAL_Z && placement.GetPositionZ() <= HOUSING_ROOT_MAX_LOCAL_Z;
}

/*static*/ WorldLocation HousingMgr::MakePlotArrival(NeighborhoodPlotData const& plot, uint32 worldMapId)
{
    return WorldLocation(worldMapId, plot.TeleportPosition[0], plot.TeleportPosition[1], plot.TeleportPosition[2],
        plot.CornerstoneRotation[2]);
}

bool HousingMgr::GetPlotArrival(uint32 neighborhoodMapId, uint8 plotIndex, WorldLocation& arrival) const
{
    uint32 worldMapId = GetWorldMapIdByNeighborhoodMapId(neighborhoodMapId);
    if (!worldMapId)
        return false;

    auto itr = _plotsByMap.find(neighborhoodMapId);
    if (itr == _plotsByMap.end())
        return false;

    for (NeighborhoodPlotData const* plot : itr->second)
    {
        if (!plot || plot->PlotIndex != int32(plotIndex))
            continue;

        arrival = MakePlotArrival(*plot, worldMapId);
        return true;
    }
    return false;
}

NeighborhoodPlotData const* HousingMgr::GetPlot(uint32 neighborhoodMapId, uint8 plotIndex) const
{
    auto itr = _plotsByMap.find(neighborhoodMapId);
    if (itr == _plotsByMap.end())
        return nullptr;

    for (NeighborhoodPlotData const* plot : itr->second)
        if (plot && plot->PlotIndex == int32(plotIndex))
            return plot;

    return nullptr;
}

/*static*/ ObjectGuid HousingMgr::MakeCornerstoneCreator(NeighborhoodPlotData const& plot, uint32 worldMapId)
{
    return ObjectGuid::Create<HighGuid::ClientActor>(HOUSING_CORNERSTONE_CREATOR_OWNER_TYPE, uint16(worldMapId),
        uint32(plot.CornerstoneGameObjectID));
}

namespace
{
    // Razorwind Shores plots whose NeighborhoodPlot cornerstone does not match what retail sends. Retail puts the
    // cornerstones of plots 12, 16, 27, 30, 43 and 49 6.8 to 61.4 yards away from the DB2's CornerstonePosition,
    // about three yards from the plot's TeleportPosition, the way every Founder's Point cornerstone in GameObjects.db2
    // stands. Plot 22 stands where the DB2 says but faces 0.22 radians away from CornerstoneRotation.z plus a half
    // turn. Each value is the same in every capture that shows that plot (hbcd3, hf1, hled1 and erhousing, except that
    // plot 49's cornerstone is not in erhousing); the lines are hbcd3's creates. The cornerstone's rotation in those
    // captures is a turn about the vertical axis only.
    struct CapturedCornerstone
    {
        uint32 WorldMapId;
        int32 PlotIndex;
        float X, Y, Z, Orientation;
    };

    constexpr CapturedCornerstone CapturedCornerstones[] =
    {
        { 2736, 12, 544.6215f, 637.1024f, 155.30049f, 0.567232f },      // hbcd3 457672
        { 2736, 16, 1482.4688f, -9.611112f, 78.111244f, 2.0943933f },   // hbcd3 439189
        { 2736, 22, 488.31598f, 304.342f, 99.204994f, 2.7052553f },     // hbcd3 457609
        { 2736, 27, 1166.5642f, 464.5434f, 154.02097f, 4.5553107f },    // hbcd3 453368
        { 2736, 30, 955.0191f, 477.25522f, 108.65986f, 4.878198f },     // hbcd3 453822
        { 2736, 43, 639.67365f, 708.3299f, 113.78312f, 5.777043f },     // hbcd3 457483
        { 2736, 49, 403.60764f, 192.3507f, 109.80048f, 1.4573486f },    // hbcd3 474886
    };
}

/*static*/ void HousingMgr::GetCornerstonePlacement(NeighborhoodPlotData const& plot, uint32 worldMapId,
    GameObjectsEntry const* clientRow, Position& position, QuaternionData& rotation)
{
    // Founder's Point: agatho's decode of the 12.0.1 captures (build 65940) puts all 55 cornerstones at their
    // GameObjects.db2 row's position. Only 22 of them also stand at the NeighborhoodPlot CornerstonePosition, and only
    // 16 of those face CornerstoneRotation.z plus a half turn.
    if (clientRow && clientRow->OwnerID == worldMapId)
    {
        rotation = QuaternionData(clientRow->Rot[0], clientRow->Rot[1], clientRow->Rot[2], clientRow->Rot[3]);
        float orientation = 0.0f, unusedY = 0.0f, unusedX = 0.0f;
        rotation.toEulerAnglesZYX(orientation, unusedY, unusedX);
        position.Relocate(clientRow->Pos.X, clientRow->Pos.Y, clientRow->Pos.Z, orientation);
        return;
    }

    for (CapturedCornerstone const& captured : CapturedCornerstones)
    {
        if (captured.WorldMapId != worldMapId || captured.PlotIndex != plot.PlotIndex)
            continue;

        position.Relocate(captured.X, captured.Y, captured.Z, captured.Orientation);
        rotation = QuaternionData::fromEulerAnglesZYX(captured.Orientation, 0.0f, 0.0f);
        return;
    }

    // Razorwind Shores: 49 of 55 cornerstones stand at CornerstonePosition (the height differs by up to 0.41 yards),
    // and 48 of those face CornerstoneRotation.z plus a half turn (hbcd3).
    float const orientation = Position::NormalizeOrientation(plot.CornerstoneRotation[2] + float(M_PI));
    position.Relocate(plot.CornerstonePosition[0], plot.CornerstonePosition[1], plot.CornerstonePosition[2], orientation);
    rotation = QuaternionData::fromEulerAnglesZYX(orientation, plot.CornerstoneRotation[1], plot.CornerstoneRotation[0]);
}

std::string HousingMgr::GenerateNeighborhoodName(uint32 neighborhoodMapId) const
{
    auto itr = _nameGenByMap.find(neighborhoodMapId);
    if (itr == _nameGenByMap.end() || itr->second.empty())
        return "Unnamed Neighborhood";

    std::vector<NeighborhoodNameGenData> const& nameGens = itr->second;
    uint32 count = static_cast<uint32>(nameGens.size());

    // Retail neighborhood names use hyphen-separated NeighborhoodNameGen entry IDs
    // (e.g., "75-78-61", "86-90-6"). The client resolves each token to localized
    // text from its local NeighborhoodNameGen.db2 (Prefix, Suffix, FullName fields).
    // Pick 3 random entries from this map's pool and combine their IDs.
    uint32 id1 = nameGens[urand(0, count - 1)].ID;
    uint32 id2 = nameGens[urand(0, count - 1)].ID;
    uint32 id3 = nameGens[urand(0, count - 1)].ID;

    return Trinity::StringFormat("{}-{}-{}", id1, id2, id3);
}

uint32 HousingMgr::GetMaxDecorForLevel(uint32 level) const
{
    // MaxDecorCount not in HouseLevelData DB2; use fallback formula
    return level * 25;
}

uint32 HousingMgr::GetQuestForLevel(uint32 level) const
{
    HouseLevelData const* levelData = GetLevelData(level);
    if (levelData && levelData->QuestID > 0)
        return static_cast<uint32>(levelData->QuestID);

    return 0;
}

// Retail-verified cumulative favor thresholds to REACH each level.
// Captured by running /script print(C_Housing.GetHouseLevelFavorForLevel(N))
// for N=2..9 on a live retail 12.0.1.66838 client. These values are NOT in
// any DB2 and NOT sent over the wire — the client keeps them in a C++
// binary-search table initialized at startup.
//
// Lua UI semantics (verified from Blizzard_HousingDashboardHouseUpgrade.lua):
//   - houseFavor stored on the Housing/3 entity is CUMULATIVE lifetime favor
//   - GetHouseLevelFavorForLevel(N) = cumulative favor needed to UNLOCK level N
//   - Progress bar = (currentFavor - threshold[level]) / (threshold[level+1] - threshold[level])
//   - CanUpgrade(level) = currentFavor >= threshold[level]
//
// Level-up gating is server-side and not client-callable — there is no
// C_Housing.UpgradeHouse API. For levels 2..6 the HouseLevelData DB2 has a
// QuestID, so the NPC most likely offers that quest once favor crosses the
// threshold (not yet verified from sniff). Levels 7..9 have QuestID=0 —
// gate unknown. Store-only for now; do NOT use to trigger level-up until
// the NPC/auto-level mechanism is sniff-verified.
uint32 HousingMgr::GetFavorThresholdForLevel(uint32 level) const
{
    //               L1  L2     L3     L4     L5     L6     L7      L8       L9
    static constexpr uint32 Thresholds[] = {
        /* L1 */ 0,     // starter — no favor needed
        /* L2 */ 10,
        /* L3 */ 1200,
        /* L4 */ 2400,
        /* L5 */ 3700,
        /* L6 */ 5700,
        /* L7 */ 7900,
        /* L8 */ 10300,
        /* L9 */ 12900,
    };
    constexpr uint32 MaxLevel = sizeof(Thresholds) / sizeof(Thresholds[0]) - 1;  // 9
    if (level <= MaxLevel)
        return Thresholds[level];
    // Above the verified range, hold at L9 — extrapolation would be a guess.
    return Thresholds[MaxLevel];
}

// Retail-verified budget tables for levels 1..7, decoded from every Housing/3
// CREATE block in dump_12.0.1.66838_2026-04-15_09-35-59 idx 9984 (n=47).
// Every block at a given level has the same 4 values — zero variance.
//   L1: Interior=910   Exterior=200  Room=1000  Fixture=19
//   L2: Interior=1155  Exterior=200  Room=2000  Fixture=24
//   L3: Interior=1450  Exterior=250  Room=3000  Fixture=30
//   L4: Interior=1745  Exterior=250  Room=4000  Fixture=36
//   L5: Interior=2050  Exterior=250  Room=5000  Fixture=43
//   L6: Interior=2360  Exterior=250  Room=5000  Fixture=50
//   L7: Interior=3180  Exterior=250  Room=5000  Fixture=68
// Levels above 7 extrapolated linearly until a sniff covers higher tiers.
// The HouseLevelData DB2 (hotfixes.house_level_data) only carries
// ID/Level/QuestID — no budget columns — so this fallback is the hot path.
//
// #16 Outdoor Lighting (A3): 12.0.7 raised the EXTERIOR decor limit alongside
// outdoor light placement — houses level 5-6 -> 300, levels 7+ -> 350 (per the
// small-activities blueprint; the 66838 dump predates 12.0.7 so these two tiers
// are DOCUMENTED-not-DB2-confirmed and flagged CAPTURE-BLOCKED until a 12.0.7
// CREATE block is sniffed). Interior/room/fixture values are unchanged. The
// exterior budget is now genuinely CHARGED on placement (see Housing.cpp M2), so
// these caps are enforced rather than cosmetic.
namespace {
    struct RetailBudget { uint32 interior, exterior, room, fixture; };
    static constexpr RetailBudget RetailBudgetByLevel[] = {
        /* 0 */ {   0,   0,    0,  0 },  // unused
        /* 1 */ { 910, 200, 1000, 19 },
        /* 2 */ {1155, 200, 2000, 24 },
        /* 3 */ {1450, 250, 3000, 30 },
        /* 4 */ {1745, 250, 4000, 36 },
        /* 5 */ {2050, 300, 5000, 43 },  // exterior 250->300 (12.0.7 #16, DOCUMENTED)
        /* 6 */ {2360, 300, 5000, 50 },  // exterior 250->300 (12.0.7 #16, DOCUMENTED)
        /* 7 */ {3180, 350, 5000, 68 },  // exterior 250->350 (12.0.7 #16, DOCUMENTED)
    };
    constexpr uint32 MAX_VERIFIED_LEVEL = 7;

    RetailBudget RetailBudgetFor(uint32 level)
    {
        if (level >= 1 && level <= MAX_VERIFIED_LEVEL)
            return RetailBudgetByLevel[level];
        if (level == 0)
            return RetailBudgetByLevel[1];
        // Above verified tier: hold at L7 values (conservative — bump once sniffed)
        return RetailBudgetByLevel[MAX_VERIFIED_LEVEL];
    }
}

uint32 HousingMgr::GetInteriorDecorBudgetForLevel(uint32 level) const
{
    HouseLevelData const* levelData = GetLevelData(level);
    if (levelData && levelData->InteriorDecorPlacementBudget > 0)
        return static_cast<uint32>(levelData->InteriorDecorPlacementBudget);
    return RetailBudgetFor(level).interior;
}

uint32 HousingMgr::GetExteriorDecorBudgetForLevel(uint32 level) const
{
    HouseLevelData const* levelData = GetLevelData(level);
    if (levelData && levelData->ExteriorDecorPlacementBudget > 0)
        return static_cast<uint32>(levelData->ExteriorDecorPlacementBudget);
    return RetailBudgetFor(level).exterior;
}

uint32 HousingMgr::GetRoomBudgetForLevel(uint32 level) const
{
    HouseLevelData const* levelData = GetLevelData(level);
    if (levelData && levelData->RoomPlacementBudget > 0)
        return static_cast<uint32>(levelData->RoomPlacementBudget);
    return RetailBudgetFor(level).room;
}

uint32 HousingMgr::GetFixtureBudgetForLevel(uint32 level) const
{
    HouseLevelData const* levelData = GetLevelData(level);
    if (levelData && levelData->ExteriorFixtureBudget > 0)
        return static_cast<uint32>(levelData->ExteriorFixtureBudget);
    return RetailBudgetFor(level).fixture;
}

uint32 HousingMgr::GetDecorWeightCost(uint32 decorEntryId) const
{
    HouseDecorData const* decorData = GetHouseDecorData(decorEntryId);
    if (decorData)
        return static_cast<uint32>(std::max<int32>(decorData->WeightCost, 1));

    return 1;
}

uint32 HousingMgr::GetRoomWeightCost(uint32 roomEntryId) const
{
    // Stairwell Room (Empty) is auto-spawned as the upper partner of a
    // Stairwell (Left/Right) placement. The player already paid the
    // stairwell's 7 weight once; charging another 5-7 for the sibling
    // would eat over half the 19-point budget on a single visible
    // stairwell. The budget is cumulative across all floors — we just
    // don't double-charge for the server-managed upper half.
    constexpr uint32 STAIRWELL_EMPTY_ROOM_ID = 48;
    if (roomEntryId == STAIRWELL_EMPTY_ROOM_ID)
        return 0;

    HouseRoomData const* roomData = GetHouseRoomData(roomEntryId);
    if (roomData)
        return static_cast<uint32>(std::max<int32>(roomData->WeightCost, 1));

    return 1;
}

uint32 HousingMgr::GetDecorIdForItem(uint32 itemId) const
{
    if (!itemId)
        return 0;

    for (auto const& [id, decor] : _houseDecorStore)
        if (decor.ItemID == int32(itemId))
            return id;
    return 0;
}

bool HousingMgr::CanVisitorAccessPlot(Player const* visitor, ObjectGuid ownerGuid, uint32 settingsFlags, bool isInterior) const
{
    if (!visitor || ownerGuid.IsEmpty())
        return false;

    if (visitor->GetGUID() == ownerGuid)
        return true;

    uint32 anyoneFlag    = isInterior ? HOUSE_SETTING_HOUSE_ACCESS_ANYONE    : HOUSE_SETTING_PLOT_ACCESS_ANYONE;
    uint32 neighborsFlag = isInterior ? HOUSE_SETTING_HOUSE_ACCESS_NEIGHBORS : HOUSE_SETTING_PLOT_ACCESS_NEIGHBORS;
    uint32 guildFlag     = isInterior ? HOUSE_SETTING_HOUSE_ACCESS_GUILD     : HOUSE_SETTING_PLOT_ACCESS_GUILD;
    uint32 friendsFlag   = isInterior ? HOUSE_SETTING_HOUSE_ACCESS_FRIENDS   : HOUSE_SETTING_PLOT_ACCESS_FRIENDS;
    uint32 partyFlag     = isInterior ? HOUSE_SETTING_HOUSE_ACCESS_PARTY     : HOUSE_SETTING_PLOT_ACCESS_PARTY;

    uint32 accessMask = isInterior
        ? (HOUSE_SETTING_HOUSE_ACCESS_ANYONE | HOUSE_SETTING_HOUSE_ACCESS_NEIGHBORS |
           HOUSE_SETTING_HOUSE_ACCESS_GUILD | HOUSE_SETTING_HOUSE_ACCESS_FRIENDS | HOUSE_SETTING_HOUSE_ACCESS_PARTY)
        : (HOUSE_SETTING_PLOT_ACCESS_ANYONE | HOUSE_SETTING_PLOT_ACCESS_NEIGHBORS |
           HOUSE_SETTING_PLOT_ACCESS_GUILD | HOUSE_SETTING_PLOT_ACCESS_FRIENDS | HOUSE_SETTING_PLOT_ACCESS_PARTY);

    if ((settingsFlags & accessMask) == 0)
        return true; // No restrictions configured — open to all

    if (settingsFlags & anyoneFlag)
        return true;

    Player* ownerPlayer = ObjectAccessor::FindPlayer(ownerGuid);

    if (settingsFlags & partyFlag)
    {
        // Party requires both online — same Group instance.
        if (ownerPlayer && visitor->GetGroup() && visitor->GetGroup() == ownerPlayer->GetGroup())
            return true;
    }

    if (settingsFlags & guildFlag)
    {
        ObjectGuid::LowType ownerGuildId = ownerPlayer
            ? ownerPlayer->GetGuildId()
            : sCharacterCache->GetCharacterGuildIdByGuid(ownerGuid);
        if (ownerGuildId != 0 && visitor->GetGuildId() == ownerGuildId)
            return true;
    }

    if (settingsFlags & friendsFlag)
    {
        // Friends are mutual on retail — visitor's social manager has the same record.
        if (visitor->GetSocial() && visitor->GetSocial()->HasFriend(ownerGuid))
            return true;
    }

    if (settingsFlags & neighborsFlag)
    {
        // Both are residents of the same neighborhood. Works offline because
        // neighborhood membership is stored on Neighborhood objects, not Player.
        for (Neighborhood const* nbh : sNeighborhoodMgr.GetNeighborhoodsForPlayer(ownerGuid))
            if (nbh->IsMember(visitor->GetGUID()))
                return true;
    }

    return false;
}

bool HousingMgr::CanVisitorExportBlueprint(Player const* visitor, ObjectGuid ownerGuid, uint32 settingsFlags) const
{
    if (!visitor || ownerGuid.IsEmpty())
        return false;

    if (visitor->GetGUID() == ownerGuid)
        return true;

    if (settingsFlags & HOUSE_SETTING_BLUEPRINT_EXPORT_ANYONE)
        return true;

    Player* ownerPlayer = ObjectAccessor::FindPlayer(ownerGuid);

    if ((settingsFlags & HOUSE_SETTING_BLUEPRINT_EXPORT_PARTY) && ownerPlayer && visitor->GetGroup()
        && visitor->GetGroup() == ownerPlayer->GetGroup())
        return true;

    if (settingsFlags & HOUSE_SETTING_BLUEPRINT_EXPORT_GUILD)
    {
        ObjectGuid::LowType ownerGuildId = ownerPlayer ? ownerPlayer->GetGuildId() : sCharacterCache->GetCharacterGuildIdByGuid(ownerGuid);
        if (ownerGuildId != 0 && visitor->GetGuildId() == ownerGuildId)
            return true;
    }

    if ((settingsFlags & HOUSE_SETTING_BLUEPRINT_EXPORT_FRIENDS) && visitor->GetSocial() && visitor->GetSocial()->HasFriend(ownerGuid))
        return true;

    if (settingsFlags & HOUSE_SETTING_BLUEPRINT_EXPORT_NEIGHBORS)
        for (Neighborhood const* nbh : sNeighborhoodMgr.GetNeighborhoodsForPlayer(ownerGuid))
            if (nbh->IsMember(visitor->GetGUID()))
                return true;

    return false;
}

HousingResult HousingMgr::ValidateDecorPlacement(uint32 decorId, Position const& pos, uint32 houseLevel) const
{
    HouseDecorData const* decorEntry = GetHouseDecorData(decorId);
    if (!decorEntry)
        return HOUSING_RESULT_DECOR_NOT_FOUND;

    // Validate position is finite / not obviously corrupt.
    if (!pos.IsPositionValid())
        return HOUSING_RESULT_BOUNDS_FAILURE_ROOM;

    // M1/A4: reject placements outside the plausible room/plot AABB. Decor
    // coordinates are local-space (room- or plot-relative), so a legitimate
    // target is always close to the origin; anything beyond HOUSING_MAX_DECOR_
    // LOCAL_EXTENT on any axis is arbitrary-coordinate GameObject spam and is
    // refused with a bounds-failure the client renders as "out of bounds".
    if (std::fabs(pos.GetPositionX()) > HOUSING_MAX_DECOR_LOCAL_EXTENT ||
        std::fabs(pos.GetPositionY()) > HOUSING_MAX_DECOR_LOCAL_EXTENT ||
        std::fabs(pos.GetPositionZ()) > HOUSING_MAX_DECOR_LOCAL_EXTENT)
        return HOUSING_RESULT_BOUNDS_FAILURE_PLOT;

    // Validate house level meets decor requirements (if any level restriction exists)
    // For now, all decor is available at any level; future DB2 fields may add restrictions
    (void)houseLevel;

    // #16 Outdoor Lighting: there is intentionally NO category gate here — 12.0.7
    // permits DecorCategory 4 "Lighting" decor in the exterior/plot scope, so it
    // flows through the same exterior placement path as any other yard decor. The
    // light-specific "two lights cannot overlap" rule (A4) is enforced with the
    // placed-decor context in Housing::CheckLightOverlap, not here (this validator
    // is per-decor and stateless). Client-side indoor/outdoor gating via
    // HouseDecor.Flags is tracked separately as audit item M10.

    return HOUSING_RESULT_SUCCESS;
}

// --- 7 new DB2 Load functions ---

void HousingMgr::LoadHouseDecorMaterialData()
{
    for (HouseDecorMaterialEntry const* entry : sHouseDecorMaterialStore)
    {
        HouseDecorMaterialData& data = _houseDecorMaterialStore[entry->ID];
        data.ID = entry->ID;
        data.WMOMaterialReference = entry->WMOMaterialReference;
        data.MaterialTextureIndex = entry->MaterialTextureIndex;
        data.HouseThemeID = entry->HouseThemeID;
        data.TextureAFileDataID = entry->TextureAFileDataID;
        data.TextureBFileDataID = entry->TextureBFileDataID;
    }

    // Build decor material index
    for (auto const& [id, mat] : _houseDecorMaterialStore)
        _materialsByTheme[mat.HouseThemeID].push_back(&mat);

    TC_LOG_DEBUG("housing", "HousingMgr::LoadHouseDecorMaterialData: Loaded {} HouseDecorMaterial entries", uint32(_houseDecorMaterialStore.size()));
}

void HousingMgr::LoadHouseExteriorWmoData()
{
    for (HouseExteriorWmoDataEntry const* entry : sHouseExteriorWmoDataStore)
    {
        HouseExteriorWmoData& data = _houseExteriorWmoStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.Flags = entry->Flags;
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadHouseExteriorWmoData: Loaded {} HouseExteriorWmoData entries", uint32(_houseExteriorWmoStore.size()));
}

void HousingMgr::LoadHouseLevelRewardInfoData()
{
    for (HouseLevelRewardInfoEntry const* entry : sHouseLevelRewardInfoStore)
    {
        HouseLevelRewardInfoData& data = _houseLevelRewardInfoStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.Description = SafeStr(entry->Description[sWorld->GetDefaultDbcLocale()]);
        data.HouseLevelDataID = entry->HouseLevelDataID;
        data.Field_4 = entry->Field_4;
        data.IconFileDataID = entry->IconFileDataID;
    }

    // Build level reward index
    for (auto const& [id, reward] : _houseLevelRewardInfoStore)
        _rewardsByLevel[reward.HouseLevelDataID].push_back(&reward);

    // HouseLevelRewardInfo DB2 fields verified from runtime data + IDA:
    //   Field_4 = HouseLevelRewardType enum: Value(0) or Object(1)
    //   IconFileDataID = actual FileData icon reference (values: 135769, 4217590, 7252953, 7487068)
    //   DB2 does NOT contain budget type (ExteriorDecor/InteriorDecor/Rooms/Fixtures) or budget values.
    //   Budget capacities come entirely from the hardcoded fallback table below.
    //   Client enum HouseLevelRewardValueType(0-3) is used in Lua UI, not stored in this DB2.
    uint32 budgetWired = 0;

    // Historical note: a load-time fallback here used to pre-fill every
    // HouseLevelData.{Interior,Exterior,Room,Fixture}Budget field with
    // hardcoded values when DB2 had 0. Those values had Room/Fixture
    // swapped (Room=19, Fixture=1000 for L1 — retail is Room=1000,
    // Fixture=19) and Interior L4 off-by-5 (1750 vs retail 1745).
    //
    // Because GetXxxBudgetForLevel() checks `levelData->XxxBudget > 0`
    // FIRST, the load-time fallback masked the per-call RetailBudgetFor()
    // table. Sniff-verified against dump_12.0.1.66838_2026-04-22_21-23-22
    // idx 298: server emitted Room=19, Fixture=1000 despite
    // commit 352ec7e3df fixing the per-call fallback.
    //
    // Removed: the per-call fallback in GetInteriorDecorBudgetForLevel /
    // GetExteriorDecorBudgetForLevel / GetRoomBudgetForLevel /
    // GetFixtureBudgetForLevel already handles zero/missing DB2 values
    // with the retail-verified RetailBudgetByLevel table.

    TC_LOG_INFO("housing", "HousingMgr::LoadHouseLevelRewardInfoData: Loaded {} HouseLevelRewardInfo entries, wired {} budget values from DB2",
        uint32(_houseLevelRewardInfoStore.size()), budgetWired);

    // Log final budget values per level for verification. When DB2 has no
    // budget rows, the fields here are 0 and the per-call GetXxxBudgetForLevel
    // fallback supplies the retail-verified values.
    for (auto const& [id, levelData] : _houseLevelDataStore)
    {
        TC_LOG_INFO("housing", "  Level {} (ID {}): DB2 Interior={} Exterior={} Room={} Fixture={} (resolved via GetXxxBudgetForLevel: {} {} {} {})",
            levelData.Level, id,
            levelData.InteriorDecorPlacementBudget, levelData.ExteriorDecorPlacementBudget,
            levelData.RoomPlacementBudget, levelData.ExteriorFixtureBudget,
            GetInteriorDecorBudgetForLevel(levelData.Level),
            GetExteriorDecorBudgetForLevel(levelData.Level),
            GetRoomBudgetForLevel(levelData.Level),
            GetFixtureBudgetForLevel(levelData.Level));
    }
}

void HousingMgr::LoadNeighborhoodInitiativeData()
{
    for (NeighborhoodInitiativeEntry const* entry : sNeighborhoodInitiativeStore)
    {
        NeighborhoodInitiativeData& data = _neighborhoodInitiativeStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.Description = SafeStr(entry->Description[sWorld->GetDefaultDbcLocale()]);
        data.InitiativeType = entry->InitiativeType;
        data.Duration = entry->Duration;
        data.RequiredParticipants = entry->RequiredParticipants;
        data.RewardCurrencyID = entry->RewardCurrencyID;
    }

    TC_LOG_INFO("housing", "HousingMgr::LoadNeighborhoodInitiativeData: Loaded {} NeighborhoodInitiative entries", uint32(_neighborhoodInitiativeStore.size()));
}

void HousingMgr::LoadRoomComponentData()
{
    uint32 doorwayCount = 0;
    uint32 totalCount = 0;

    for (RoomComponentEntry const* entry : sRoomComponentStore)
    {
        // Store all components indexed by RoomWmoDataID for room spawning
        RoomComponentData compData;
        compData.ID = entry->ID;
        compData.RoomWmoDataID = entry->RoomWmoDataID;
        compData.OffsetPos[0] = entry->OffsetPos.X;
        compData.OffsetPos[1] = entry->OffsetPos.Y;
        compData.OffsetPos[2] = entry->OffsetPos.Z;
        compData.OffsetRot[0] = entry->OffsetRot.X;
        compData.OffsetRot[1] = entry->OffsetRot.Y;
        compData.OffsetRot[2] = entry->OffsetRot.Z;
        compData.ModelFileDataID = entry->ModelFileDataID;
        compData.Type = entry->Type;
        compData.MeshStyleFilterID = entry->MeshStyleFilterID;
        compData.ConnectionType = entry->ConnectionType;
        compData.Flags = entry->Flags;

        _roomComponentsByWmoData[entry->RoomWmoDataID].push_back(compData);
        ++totalCount;

        // Also index doorway components separately for connectivity checks
        if (entry->Type == HOUSING_ROOM_COMPONENT_DOORWAY)
        {
            RoomDoorInfo door;
            door.RoomComponentID = entry->ID;
            door.OffsetPos[0] = entry->OffsetPos.X;
            door.OffsetPos[1] = entry->OffsetPos.Y;
            door.OffsetPos[2] = entry->OffsetPos.Z;
            door.OffsetRot[0] = entry->OffsetRot.X;
            door.OffsetRot[1] = entry->OffsetRot.Y;
            door.OffsetRot[2] = entry->OffsetRot.Z;
            door.ConnectionType = entry->ConnectionType;

            _roomDoorMap[entry->RoomWmoDataID].push_back(door);
            ++doorwayCount;
        }
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadRoomComponentData: Indexed {} total components ({} doorways) "
        "across {} room types from {} DB2 entries",
        totalCount, doorwayCount, uint32(_roomComponentsByWmoData.size()),
        uint32(sRoomComponentStore.GetNumRows()));

    // Diagnostic: log HouseRoom entries with their component counts
    for (auto const& [roomId, roomData] : _houseRoomStore)
    {
        auto const* comps = GetRoomComponents(roomData.RoomWmoDataID);
        uint32 compCount = comps ? uint32(comps->size()) : 0;

        // Count component types
        uint32 wallCount = 0, floorCount = 0, ceilCount = 0, doorwayCount2 = 0, otherCount = 0;
        if (comps)
        {
            for (auto const& c : *comps)
            {
                switch (c.Type)
                {
                    case HOUSING_ROOM_COMPONENT_WALL: ++wallCount; break;
                    case HOUSING_ROOM_COMPONENT_FLOOR: ++floorCount; break;
                    case HOUSING_ROOM_COMPONENT_CEILING: ++ceilCount; break;
                    case HOUSING_ROOM_COMPONENT_DOORWAY:
                    case HOUSING_ROOM_COMPONENT_DOORWAY_WALL: ++doorwayCount2; break;
                    default: ++otherCount; break;
                }
            }
        }

        TC_LOG_INFO("housing", "  HouseRoom [ID={} '{}' RoomWmoDataID={} Flags=0x{:X}{}] -> {} components "
            "({} wall, {} floor, {} ceiling, {} doorway, {} other)",
            roomId, roomData.Name, roomData.RoomWmoDataID, roomData.Flags,
            roomData.IsBaseRoom() ? " BASE_ROOM" : "",
            compCount, wallCount, floorCount, ceilCount, doorwayCount2, otherCount);
    }
}

std::vector<RoomComponentData> const* HousingMgr::GetRoomComponents(uint32 roomWmoDataId) const
{
    auto itr = _roomComponentsByWmoData.find(roomWmoDataId);
    if (itr != _roomComponentsByWmoData.end())
        return &itr->second;

    return nullptr;
}

bool HousingMgr::IsBaseRoom(uint32 roomEntryId) const
{
    HouseRoomData const* roomData = GetHouseRoomData(roomEntryId);
    return roomData && roomData->IsBaseRoom();
}

uint32 HousingMgr::GetRoomDoorCount(uint32 roomEntryId) const
{
    HouseRoomData const* roomData = GetHouseRoomData(roomEntryId);
    if (!roomData)
        return 0;

    auto itr = _roomDoorMap.find(roomData->RoomWmoDataID);
    if (itr != _roomDoorMap.end())
        return static_cast<uint32>(itr->second.size());

    return 0;
}

std::vector<RoomDoorInfo> const* HousingMgr::GetRoomDoors(uint32 roomWmoDataId) const
{
    auto itr = _roomDoorMap.find(roomWmoDataId);
    if (itr != _roomDoorMap.end())
        return &itr->second;

    return nullptr;
}

// --- 6 new ID-based accessors ---

HouseDecorMaterialData const* HousingMgr::GetHouseDecorMaterialData(uint32 id) const
{
    auto itr = _houseDecorMaterialStore.find(id);
    if (itr != _houseDecorMaterialStore.end())
        return &itr->second;

    return nullptr;
}

HouseExteriorWmoData const* HousingMgr::GetHouseExteriorWmoData(uint32 id) const
{
    auto itr = _houseExteriorWmoStore.find(id);
    if (itr != _houseExteriorWmoStore.end())
        return &itr->second;

    return nullptr;
}

HouseLevelRewardInfoData const* HousingMgr::GetHouseLevelRewardInfoData(uint32 id) const
{
    auto itr = _houseLevelRewardInfoStore.find(id);
    if (itr != _houseLevelRewardInfoStore.end())
        return &itr->second;

    return nullptr;
}

NeighborhoodInitiativeData const* HousingMgr::GetNeighborhoodInitiativeData(uint32 id) const
{
    auto itr = _neighborhoodInitiativeStore.find(id);
    if (itr != _neighborhoodInitiativeStore.end())
        return &itr->second;

    return nullptr;
}

// --- 2 indexed lookup accessors ---

std::vector<HouseDecorMaterialData const*> HousingMgr::GetMaterialsForTheme(uint32 houseThemeId) const
{
    auto itr = _materialsByTheme.find(houseThemeId);
    if (itr != _materialsByTheme.end())
        return itr->second;

    return {};
}

std::vector<HouseLevelRewardInfoData const*> HousingMgr::GetRewardsForLevel(uint32 houseLevelId) const
{
    auto itr = _rewardsByLevel.find(houseLevelId);
    if (itr != _rewardsByLevel.end())
        return itr->second;

    return {};
}

void HousingMgr::LoadDecorCategoryData()
{
    for (DecorCategoryEntry const* entry : sDecorCategoryStore)
    {
        DecorCategoryData& data = _decorCategoryStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.UiTextureAtlasElementID = entry->UiTextureAtlasElementID;
        data.OrderIndex = entry->OrderIndex;
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadDecorCategoryData: Loaded {} decor categories", uint32(_decorCategoryStore.size()));
}

void HousingMgr::LoadDecorSubcategoryData()
{
    for (DecorSubcategoryEntry const* entry : sDecorSubcategoryStore)
    {
        DecorSubcategoryData& data = _decorSubcategoryStore[entry->ID];
        data.ID = entry->ID;
        data.Name = SafeStr(entry->Name[sWorld->GetDefaultDbcLocale()]);
        data.UiTextureAtlasElementID = entry->UiTextureAtlasElementID;
        data.DecorCategoryID = entry->DecorCategoryID;
        data.OrderIndex = entry->OrderIndex;

        _subcategoriesByCategory[entry->DecorCategoryID].push_back(&_decorSubcategoryStore[entry->ID]);
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadDecorSubcategoryData: Loaded {} decor subcategories", uint32(_decorSubcategoryStore.size()));
}

void HousingMgr::LoadDecorDyeSlotData()
{
    for (DecorDyeSlotEntry const* entry : sDecorDyeSlotStore)
    {
        DecorDyeSlotData& data = _decorDyeSlotStore[entry->ID];
        data.ID = entry->ID;
        data.DyeColorCategoryID = entry->DyeColorCategoryID;
        data.HouseDecorID = entry->HouseDecorID;
        data.OrderIndex = entry->OrderIndex;
        data.Channel = entry->Channel;

        _dyeSlotsByDecor[entry->HouseDecorID].push_back(&_decorDyeSlotStore[entry->ID]);
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadDecorDyeSlotData: Loaded {} decor dye slots", uint32(_decorDyeSlotStore.size()));
}

void HousingMgr::LoadDecorXDecorSubcategoryData()
{
    uint32 count = 0;
    for (DecorXDecorSubcategoryEntry const* entry : sDecorXDecorSubcategoryStore)
    {
        _decorsBySubcategory[entry->DecorSubcategoryID].push_back(entry->HouseDecorID);
        // #16 Outdoor Lighting: build a decor -> parent-category reverse index so
        // the placement path can classify a decor (Lighting = category 4) in O(1)
        // without walking subcategories. DecorSubcategory rows are already loaded
        // (LoadDecorSubcategoryData runs before this pass).
        if (DecorSubcategoryData const* sub = GetDecorSubcategoryData(entry->DecorSubcategoryID))
            _categoryByDecor[entry->HouseDecorID] = uint32(sub->DecorCategoryID);
        ++count;
    }

    TC_LOG_DEBUG("housing", "HousingMgr::LoadDecorXDecorSubcategoryData: Loaded {} decor-to-subcategory links", count);
}

void HousingMgr::BuildRoomComponentOptionIndex()
{
    _roomCompOptionIndex.clear();
    uint32 count = 0;
    for (RoomComponentOptionEntry const* entry : sRoomComponentOptionStore)
    {
        if (!entry)
            continue;
        // Index by (MeshStyleFilterID, HouseThemeID).
        // Multiple options exist per key with different Types:
        //   Type=0 (Cosmetic) = normal solid wall (DEFAULT)
        //   Type=1 (DoorwayWall) = wall with sealed doorway frame
        //   Type=2 (Doorway) = open doorway passage
        // Prefer Type=0 (Cosmetic) as the default wall model. Type 1/2 are
        // used for doorway-capable walls based on connection state.
        uint64 key = (uint64(uint32(entry->MeshStyleFilterID)) << 32) | uint32(entry->HouseThemeID);
        auto existing = _roomCompOptionIndex.find(key);
        if (existing == _roomCompOptionIndex.end())
            _roomCompOptionIndex[key] = entry;
        else if (entry->Type == 0 && existing->second->Type != 0)
            _roomCompOptionIndex[key] = entry; // Replace non-Cosmetic with Cosmetic
        ++count;
    }
    TC_LOG_INFO("housing", "HousingMgr::BuildRoomComponentOptionIndex: Indexed {} RoomComponentOption entries", count);
}

void HousingMgr::BuildRoomComponentTextureIndex()
{
    _textureByOptionId.clear();
    _textureByComponentType.clear();

    // Build option→texture link from RoomComponentOptionTexture join table
    for (RoomComponentOptionTextureEntry const* link : sRoomComponentOptionTextureStore)
    {
        if (!link)
            continue;
        _textureByOptionId[link->RoomComponentOptionID] = link->RoomComponentTextureID;
    }

    // Build type→texture fallback from RoomComponentTexture
    // "Type" in RoomComponentTexture maps to component type (1=wall, 2=floor, 3=ceiling)
    for (RoomComponentTextureEntry const* tex : sRoomComponentTextureStore)
    {
        if (!tex || tex->Type <= 0)
            continue;
        uint8 compType = static_cast<uint8>(tex->Type);
        if (!_textureByComponentType.contains(compType))
            _textureByComponentType[compType] = static_cast<int32>(tex->ID);
    }

    TC_LOG_INFO("housing", "HousingMgr::BuildRoomComponentTextureIndex: "
        "{} option→texture links, {} type→texture fallbacks "
        "(RoomComponentTexture store: {} entries, RoomComponentOptionTexture store: {} entries)",
        uint32(_textureByOptionId.size()), uint32(_textureByComponentType.size()),
        sRoomComponentTextureStore.GetNumRows(), sRoomComponentOptionTextureStore.GetNumRows());
}

void HousingMgr::DumpRoomComponentTextureDiagnostics()
{
    TC_LOG_INFO("housing", "=== RoomComponentTexture Diagnostic Dump ===");
    TC_LOG_INFO("housing", "  RoomComponentTexture store:       {} entries", sRoomComponentTextureStore.GetNumRows());
    TC_LOG_INFO("housing", "  RoomComponentOptionTexture store:  {} entries", sRoomComponentOptionTextureStore.GetNumRows());

    for (RoomComponentTextureEntry const* tex : sRoomComponentTextureStore)
    {
        if (!tex)
            continue;
        TC_LOG_INFO("housing", "  Texture [{}] Name='{}' Type={} FileDataID={} Flags={} UiOrder={} RoomComponentID={}",
            tex->ID,
            SafeStr(tex->Name[sWorld->GetDefaultDbcLocale()]),
            tex->Type, tex->FileDataID, tex->Flags, tex->UiOrder, tex->RoomComponentID);
    }

    for (RoomComponentOptionTextureEntry const* link : sRoomComponentOptionTextureStore)
    {
        if (!link)
            continue;
        TC_LOG_INFO("housing", "  OptionTexture [{}] OptionID={} → TextureID={}",
            link->ID, link->RoomComponentOptionID, link->RoomComponentTextureID);
    }

    // Log the hardcoded values we're replacing and their DB2 equivalents
    TC_LOG_INFO("housing", "  --- Texture ID Resolution ---");
    TC_LOG_INFO("housing", "  Wall  (type=1): DB2={} (was hardcoded 24)",
        _textureByComponentType.contains(1) ? _textureByComponentType[1] : 0);
    TC_LOG_INFO("housing", "  Floor (type=2): DB2={} (was hardcoded 40)",
        _textureByComponentType.contains(2) ? _textureByComponentType[2] : 0);
    TC_LOG_INFO("housing", "  Ceil  (type=3): DB2={} (was hardcoded 54)",
        _textureByComponentType.contains(3) ? _textureByComponentType[3] : 0);
    TC_LOG_INFO("housing", "=== End RoomComponentTexture Dump ===");
}

int32 HousingMgr::GetTextureIdForComponentOption(int32 roomComponentOptionID) const
{
    auto itr = _textureByOptionId.find(roomComponentOptionID);
    return itr != _textureByOptionId.end() ? itr->second : 0;
}

int32 HousingMgr::GetTextureIdForComponentType(uint8 componentType) const
{
    auto itr = _textureByComponentType.find(componentType);
    return itr != _textureByComponentType.end() ? itr->second : 0;
}

void HousingMgr::EnsureDoorGameObjectTemplates()
{
    // Retail's door templates are known for three doors only, all Horde: the front doors 602705 and 602706 and the
    // exit door 587318 (hbcd3 1311088-1311140, and the Razorwind Shores world data built from the captures). Those come
    // from the world database and are never made here; without their rows the house has no usable door and an error
    // says so.
    //
    // Every other door an ExteriorComponent names, and the Alliance exit door 575017, has no template in the world
    // database. Those are made here in the shape of the Horde doors, so that using one casts the same goober spell as
    // the Horde door does: a goober with lock 4296 (Opening), closing again after 3000 ms, casting 1234192 (enter) for a
    // front door or 1234193 (Exit House) for an exit door, cast by the player, not fuzzy-hit, and an exit door open to
    // several players at once. This is not retail data
    // for those doors: no capture shows them, and their display is the Horde door's.
    static constexpr uint32 CapturedDoors[] = { 602705, 602706, INTERIOR_DOOR_GO_HORDE };
    static constexpr uint32 HordeFrontDoorDisplayId = 116974;   // 602705, hbcd3 1311088
    // 587318, from the Razorwind Shores world data built from the captures.
    static constexpr uint32 HordeExitDoorDisplayId = 114699;

    uint32 created = 0;
    auto makeDoor = [&](uint32 goEntry, std::string name, bool exitDoor)
    {
        if (sObjectMgr->GetGameObjectTemplate(goEntry))
            return;

        if (std::find(std::begin(CapturedDoors), std::end(CapturedDoors), goEntry) != std::end(CapturedDoors))
        {
            TC_LOG_ERROR("housing", "HousingMgr::EnsureDoorGameObjectTemplates: door {} has no gameobject_template row; "
                "it comes from the Razorwind Shores world data, and until it is there that house door cannot be used", goEntry);
            return;
        }

        GameObjectTemplate& goTemplate = sObjectMgr->GetGameObjectTemplateStoreForHotfix()[goEntry];
        goTemplate.entry = goEntry;
        goTemplate.type = GAMEOBJECT_TYPE_GOOBER;
        goTemplate.displayId = exitDoor ? HordeExitDoorDisplayId : HordeFrontDoorDisplayId;
        goTemplate.name = std::move(name);
        goTemplate.size = 1.0f;
        goTemplate.goober.open = 4296;
        goTemplate.goober.autoClose = 3000;
        goTemplate.goober.spell = exitDoor ? SPELL_HOUSING_EXIT_HOUSE : SPELL_HOUSING_ENTER_HOUSE;
        goTemplate.goober.AllowMultiInteract = exitDoor ? 1 : 0;
        goTemplate.goober.playerCast = 1;
        goTemplate.goober.NoFuzzyHit = 1;
        goTemplate.InitializeQueryData();

        ++created;
        TC_LOG_DEBUG("housing", "HousingMgr::EnsureDoorGameObjectTemplates: made template {} ('{}') in the Horde door's shape",
            goEntry, goTemplate.name);
    };

    for (ExteriorComponentEntry const* entry : sExteriorComponentStore)
        if (entry && entry->Type == HOUSING_FIXTURE_TYPE_DOOR && entry->GameObjectID > 0)
            makeDoor(uint32(entry->GameObjectID), entry->Name[DEFAULT_LOCALE] ? entry->Name[DEFAULT_LOCALE] : "Front Door", false);

    // The doors inside the house are not reachable from ExteriorComponent: HouseInteriorMap picks them by faction.
    makeDoor(INTERIOR_DOOR_GO_ALLIANCE, "Front Door", true);
    makeDoor(INTERIOR_DOOR_GO_HORDE, "Razorwind Shores Front Door", true);

    if (created)
        TC_LOG_INFO("server.loading", ">> Made {} house door templates that the world database lacks, in the Horde doors' shape", created);
}

void HousingMgr::BuildExteriorComponentIndexes()
{
    _hooksByExtComp.clear();
    _exitPointByExtComp.clear();
    _groupByExtComp.clear();
    _extCompsByGroup.clear();
    _childrenByExtComp.clear();
    _rootCompsByWmoDataId.clear();
    _defaultFixtureByTypeWmo.clear();

    // 1. Build hook index: which hooks are parented to each component.
    //    ExteriorComponentHook has IndexField=2 (ID in data) and ParentIndexField=4.
    //    Use the store's range-based iterator which correctly iterates unique entries.
    for (ExteriorComponentHookEntry const* hook : sExteriorComponentHookStore)
    {
        if (!hook)
            continue;
        _hooksByExtComp[hook->ExteriorComponentID].push_back(hook);
    }
    // 1a. Build child index and root-by-WMO index from ExteriorComponent.
    //     ParentComponentID > 0 → color/dye variant of that component.
    //     ParentComponentID == 0 → base variant, and if the Type is a structural root,
    //     it's indexed by HouseExteriorWmoDataID for independent spawning.
    //
    //     Structural root check: look up ExteriorComponentType by comp->Type.
    //     Only types with ParentComponentType == 0 are structural roots (Base=9, Roof=10).
    //     Types like Door(11), Window(12), Chimney(16) have ParentComponentType > 0
    //     and are spawned as hook children, NOT as independent roots.
    //
    //     ExteriorComponent uses ParentIndexField (HouseExteriorWmoDataID).
    //     Use range-based iterator, NOT LookupEntry(i), which maps by parent ID.
    std::unordered_set<uint8> structuralRootTypes;
    for (ExteriorComponentEntry const* comp : sExteriorComponentStore)
    {
        if (!comp)
            continue;

        if (comp->ParentComponentID > 0)
            _childrenByExtComp[static_cast<uint32>(comp->ParentComponentID)].push_back(comp->ID);

        if (comp->ParentComponentID == 0 && comp->ModelFileDataID > 0 && comp->HouseExteriorWmoDataID > 0)
        {
            // Check if this component's Type is a structural root.
            // ExteriorComponentType DB2: Base(9) and Roof(10) have ParentComponentType=0.
            // All fixture types (Door=11, Window=12, etc.) have ParentComponentType > 0.
            // Try DB2 lookup first; fall back to known root types if store is unreliable.
            bool isStructuralRoot = false;
            ExteriorComponentTypeEntry const* typeEntry = sExteriorComponentTypeStore.LookupEntry(comp->Type);
            if (typeEntry)
                isStructuralRoot = (typeEntry->ParentComponentType == 0);
            else
                isStructuralRoot = (comp->Type == 9 || comp->Type == 10); // Base, Roof

            if (isStructuralRoot)
            {
                _rootCompsByWmoDataId[comp->HouseExteriorWmoDataID].push_back(comp->ID);
                structuralRootTypes.insert(comp->Type);
            }
        }
    }
    TC_LOG_DEBUG("housing", "HousingMgr: structural root types: ({})",
        [&]() {
            std::string s;
            for (uint8 t : structuralRootTypes)
                s += (s.empty() ? "" : ",") + std::to_string(t);
            return s.empty() ? "none" : s;
        }());

    // 1c. Build group indexes from ExteriorComponentXGroup (for UI fixture panels)
    for (ExteriorComponentXGroupEntry const* xg : sExteriorComponentXGroupStore)
    {
        if (!xg)
            continue;
        uint32 compID = static_cast<uint32>(xg->ExteriorComponentID);
        int32 groupID = xg->ExteriorComponentGroupID;
        _groupByExtComp[compID] = groupID;
        _extCompsByGroup[groupID].push_back(compID);
    }

    // 2. Build fixture resolution index: (componentType, wmoDataID) → default component ID.
    //    For each hook on a parent component, the fixture that goes there is determined by:
    //      - Hook's ExteriorComponentTypeID (e.g., Door=11, Window=12, Chimney=16)
    //      - Parent component's HouseExteriorWmoDataID (e.g., 9=Human, 55=NightElf)
    //    The default fixture is the root component (ParentComponentID==0) with matching
    //    Type and WmoDataID that has Flags & 0x1 (IsDefault).
    //
    //    This replaces the old GroupXHook→Group→XGroup chain which was incorrect —
    //    groups are for UI organization, not fixture resolution.
    for (ExteriorComponentEntry const* comp : sExteriorComponentStore)
    {
        if (!comp || comp->ParentComponentID != 0 || comp->HouseExteriorWmoDataID == 0)
            continue;

        // Key includes size so different house sizes get the right defaults
        uint64 key = (uint64(comp->Type) << 40) | (uint64(comp->HouseExteriorWmoDataID) << 8) | comp->Size;
        bool isDefault = (comp->Flags & 0x1) != 0;

        auto existing = _defaultFixtureByTypeWmo.find(key);
        if (existing == _defaultFixtureByTypeWmo.end())
        {
            // First component for this (type, wmo, size) — insert it
            _defaultFixtureByTypeWmo[key] = comp->ID;
        }
        else if (isDefault)
        {
            // This component is the default — override any non-default already stored
            _defaultFixtureByTypeWmo[key] = comp->ID;
        }
    }

    TC_LOG_DEBUG("housing", "HousingMgr: Built _defaultFixtureByTypeWmo with {} entries", uint32(_defaultFixtureByTypeWmo.size()));

    // 3. Build exit point index
    for (ExteriorComponentExitPointEntry const* exitPt : sExteriorComponentExitPointStore)
    {
        if (!exitPt)
            continue;
        _exitPointByExtComp[exitPt->ExteriorComponentID] = exitPt;
    }

    TC_LOG_INFO("housing", "HousingMgr::BuildExteriorComponentIndexes: "
        "hooks={} fixtureByTypeWmo={} exitPoints={} groups={} compsInGroups={} parentChildren={} wmoRoots={}",
        uint32(_hooksByExtComp.size()), uint32(_defaultFixtureByTypeWmo.size()),
        uint32(_exitPointByExtComp.size()), uint32(_groupByExtComp.size()),
        uint32(_extCompsByGroup.size()), uint32(_childrenByExtComp.size()),
        uint32(_rootCompsByWmoDataId.size()));

}

std::vector<ExteriorComponentHookEntry const*> const* HousingMgr::GetHooksOnComponent(uint32 extCompID) const
{
    auto itr = _hooksByExtComp.find(extCompID);
    return itr != _hooksByExtComp.end() ? &itr->second : nullptr;
}

uint32 HousingMgr::GetDefaultFixtureForType(uint8 componentType, uint32 wmoDataID, uint8 houseSize /*= 0*/) const
{
    // Try exact size match first
    if (houseSize > 0)
    {
        uint64 key = (uint64(componentType) << 40) | (uint64(wmoDataID) << 8) | houseSize;
        auto itr = _defaultFixtureByTypeWmo.find(key);
        if (itr != _defaultFixtureByTypeWmo.end())
            return itr->second;
    }

    // Fallback: scan all sizes for this (type, wmo) — useful when caller doesn't know the size
    for (uint8 sz = 1; sz <= 4; ++sz)
    {
        if (sz == houseSize)
            continue; // already tried
        uint64 key = (uint64(componentType) << 40) | (uint64(wmoDataID) << 8) | sz;
        auto itr = _defaultFixtureByTypeWmo.find(key);
        if (itr != _defaultFixtureByTypeWmo.end())
            return itr->second;
    }

    // Also try size=0 in case any components have Size=0
    uint64 key = (uint64(componentType) << 40) | (uint64(wmoDataID) << 8);
    auto itr = _defaultFixtureByTypeWmo.find(key);
    return itr != _defaultFixtureByTypeWmo.end() ? itr->second : 0;
}

uint32 HousingMgr::GetRacialWmoDataID(uint8 race, uint32 teamId)
{
    switch (race)
    {
        case RACE_NIGHTELF: return 55;  // Woodland
        case RACE_BLOODELF: return 56;  // Engraved
        default:
            return (teamId == HORDE) ? 87 : 9; // Orc / Human
    }
}

ExteriorComponentExitPointEntry const* HousingMgr::GetExitPoint(uint32 extCompID) const
{
    auto itr = _exitPointByExtComp.find(extCompID);
    return itr != _exitPointByExtComp.end() ? itr->second : nullptr;
}

int32 HousingMgr::GetGroupForComponent(uint32 extCompID) const
{
    auto itr = _groupByExtComp.find(extCompID);
    return itr != _groupByExtComp.end() ? itr->second : 0;
}

std::vector<uint32> const* HousingMgr::GetChildComponents(uint32 parentCompID) const
{
    auto itr = _childrenByExtComp.find(parentCompID);
    return itr != _childrenByExtComp.end() ? &itr->second : nullptr;
}

std::vector<uint32> const* HousingMgr::GetRootComponentsForWmoData(uint32 wmoDataID) const
{
    auto itr = _rootCompsByWmoDataId.find(wmoDataID);
    return itr != _rootCompsByWmoDataId.end() ? &itr->second : nullptr;
}

std::vector<uint32> const* HousingMgr::GetComponentsInGroup(int32 groupID) const
{
    auto itr = _extCompsByGroup.find(groupID);
    return itr != _extCompsByGroup.end() ? &itr->second : nullptr;
}

void HousingMgr::DumpExteriorComponentDiagnostics()
{
    TC_LOG_INFO("housing", "=== ExteriorComponent Diagnostic Dump ===");
    TC_LOG_INFO("housing", "  ExteriorComponent store:        {} entries", sExteriorComponentStore.GetNumRows());
    TC_LOG_INFO("housing", "  ExteriorComponentHook store:    {} entries", sExteriorComponentHookStore.GetNumRows());
    TC_LOG_INFO("housing", "  ExteriorComponentExitPoint:     {} entries", sExteriorComponentExitPointStore.GetNumRows());
    TC_LOG_INFO("housing", "  ExteriorComponentGroup store:   {} entries", sExteriorComponentGroupStore.GetNumRows());
    TC_LOG_INFO("housing", "  ExteriorComponentGroupXHook:    {} entries", sExteriorComponentGroupXHookStore.GetNumRows());
    TC_LOG_INFO("housing", "  ExteriorComponentType store:    {} entries", sExteriorComponentTypeStore.GetNumRows());
    TC_LOG_INFO("housing", "  ExteriorComponentXGroup store:  {} entries", sExteriorComponentXGroupStore.GetNumRows());

    // Dump known components from both alliance and horde sniff data
    static constexpr uint32 knownCompIDs[] = {
        141, 1505, 3811, 1003, 1436, 1417, 1448, 1452, 976, 980, 2445, 2476, 1011
    };

    TC_LOG_INFO("housing", "  --- Known ExteriorComponents ---");
    for (uint32 compID : knownCompIDs)
    {
        ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(compID);
        if (!comp)
        {
            TC_LOG_INFO("housing", "    [{}] NOT FOUND in DB2", compID);
            continue;
        }
        TC_LOG_INFO("housing", "    [{}] Name='{}' ModelFileDataID={} Type={} Size={} Flags={} ParentCompID={} GameObjID={}",
            compID,
            SafeStr(comp->Name[sWorld->GetDefaultDbcLocale()]),
            comp->ModelFileDataID, comp->Type, comp->Size, comp->Flags, comp->ParentComponentID, comp->GameObjectID);
    }

    // Dump hooks parented to known components
    TC_LOG_INFO("housing", "  --- ExteriorComponentHooks parented to known components ---");
    for (ExteriorComponentHookEntry const* hook : sExteriorComponentHookStore)
    {
        if (!hook)
            continue;
        // Check if this hook belongs to a known component
        bool isKnown = false;
        for (uint32 compID : knownCompIDs)
            if (hook->ExteriorComponentID == compID)
            { isKnown = true; break; }

        if (isKnown)
        {
            TC_LOG_INFO("housing", "    Hook [{}] on comp={} pos=({:.2f},{:.2f},{:.2f}) "
                "rot=({:.2f},{:.2f},{:.2f}) typeID={}",
                hook->ID, hook->ExteriorComponentID,
                hook->Position[0], hook->Position[1], hook->Position[2],
                hook->Rotation[0], hook->Rotation[1], hook->Rotation[2],
                hook->ExteriorComponentTypeID);
        }
    }

    // Dump exit points for known components
    TC_LOG_INFO("housing", "  --- ExteriorComponentExitPoints for known components ---");
    for (ExteriorComponentExitPointEntry const* exitPt : sExteriorComponentExitPointStore)
    {
        if (!exitPt)
            continue;
        bool isKnown = false;
        for (uint32 compID : knownCompIDs)
            if (exitPt->ExteriorComponentID == compID)
            { isKnown = true; break; }

        if (isKnown)
        {
            TC_LOG_INFO("housing", "    ExitPoint [{}] on comp={} pos=({:.2f},{:.2f},{:.2f}) "
                "rot=({:.2f},{:.2f},{:.2f})",
                exitPt->ID, exitPt->ExteriorComponentID,
                exitPt->Position[0], exitPt->Position[1], exitPt->Position[2],
                exitPt->Rotation[0], exitPt->Rotation[1], exitPt->Rotation[2]);
        }
    }

    // Dump component→parent relationship (ParentComponentID)
    TC_LOG_INFO("housing", "  --- Component parent relationships ---");
    for (uint32 compID : knownCompIDs)
    {
        ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(compID);
        if (!comp || comp->ParentComponentID <= 0)
            continue;
        TC_LOG_INFO("housing", "    comp {} → parent comp {}", compID, comp->ParentComponentID);
    }

    // Dump ExteriorComponentXGroup mappings
    TC_LOG_INFO("housing", "  --- ExteriorComponentXGroup mappings ---");
    for (ExteriorComponentXGroupEntry const* xg : sExteriorComponentXGroupStore)
    {
        if (!xg)
            continue;
        bool isKnown = false;
        for (uint32 compID : knownCompIDs)
            if (static_cast<uint32>(xg->ExteriorComponentID) == compID)
            { isKnown = true; break; }

        if (isKnown)
        {
            TC_LOG_INFO("housing", "    XGroup [{}] comp={} → group={}",
                xg->ID, xg->ExteriorComponentID, xg->ExteriorComponentGroupID);
        }
    }

    TC_LOG_INFO("housing", "=== End ExteriorComponent Diagnostic Dump ===");
}

DecorCategoryData const* HousingMgr::GetDecorCategoryData(uint32 id) const
{
    auto itr = _decorCategoryStore.find(id);
    return itr != _decorCategoryStore.end() ? &itr->second : nullptr;
}

DecorSubcategoryData const* HousingMgr::GetDecorSubcategoryData(uint32 id) const
{
    auto itr = _decorSubcategoryStore.find(id);
    return itr != _decorSubcategoryStore.end() ? &itr->second : nullptr;
}

uint32 HousingMgr::GetDecorCategoryForDecor(uint32 decorId) const
{
    auto itr = _categoryByDecor.find(decorId);
    return itr != _categoryByDecor.end() ? itr->second : 0;
}

bool HousingMgr::IsLightingDecor(uint32 decorId) const
{
    return GetDecorCategoryForDecor(decorId) == HOUSING_DECOR_CATEGORY_LIGHTING;
}

std::vector<DecorSubcategoryData const*> HousingMgr::GetSubcategoriesForCategory(uint32 categoryId) const
{
    auto itr = _subcategoriesByCategory.find(categoryId);
    if (itr != _subcategoriesByCategory.end())
        return itr->second;

    return {};
}

std::vector<uint32> HousingMgr::GetDecorIdsForSubcategory(uint32 subcategoryId) const
{
    auto itr = _decorsBySubcategory.find(subcategoryId);
    if (itr != _decorsBySubcategory.end())
        return itr->second;

    return {};
}

std::vector<DecorDyeSlotData const*> HousingMgr::GetDyeSlotsForDecor(uint32 houseDecorId) const
{
    auto itr = _dyeSlotsByDecor.find(houseDecorId);
    if (itr != _dyeSlotsByDecor.end())
        return itr->second;

    return {};
}

int32 HousingMgr::GetFactionDefaultThemeID(int32 factionRestriction) const
{
    // Returns the BASE theme for DB2 RoomComponentOption lookups.
    // Folk=1 (Alliance), Rugged=2 (Horde). These are the root themes in HouseTheme
    // with ParentThemeID=0. The DB2 option entries are keyed by base theme.
    if (factionRestriction == NEIGHBORHOOD_FACTION_ALLIANCE)
        return 1; // Folk
    if (factionRestriction == NEIGHBORHOOD_FACTION_HORDE)
        return 2; // Rugged
    return 1;
}

int32 HousingMgr::GetDefaultSubThemeID(int32 baseThemeID) const
{
    // Converts a base theme (1=Folk, 2=Rugged) to the default sub-theme (6=Folk Medium,
    // 8=Rugged Medium) for writing to FHousingRoomComponentMesh_C.HouseThemeID.
    // Sniff-verified: new houses use "Medium" sub-theme by default.
    // Base themes with ParentThemeID=0 have child sub-themes:
    //   Folk(1) → Folk Medium(6), Folk Dark(7), Folk Light(20)
    //   Rugged(2) → Rugged Medium(8), Rugged Dark(9), Rugged Light(26)
    switch (baseThemeID)
    {
        case 1: return 6;  // Folk → Folk Medium
        case 2: return 8;  // Rugged → Rugged Medium
        case 3: return 3;  // Generic (no sub-themes)
        case 4: return 4;  // Bel'ameth (no sub-themes)
        case 5: return 5;  // Silvermoon (no sub-themes)
        default: return baseThemeID;
    }
}

int32 HousingMgr::GetBaseThemeID(int32 themeID) const
{
    // Convert a sub-theme (e.g., 20=Folk Light) to its base theme (1=Folk).
    // Sub-themes have ParentThemeID != 0 in HouseTheme DB2.
    // RoomComponentOption entries only exist for base themes (1-5).

    // 1) Try DB2 data (ParentThemeID field from CASC)
    auto itr = _houseThemeStore.find(themeID);
    if (itr != _houseThemeStore.end() && itr->second.ParentThemeID != 0)
        return itr->second.ParentThemeID;

    // Already a base theme (1-5)
    if (themeID >= 1 && themeID <= 5)
        return themeID;

    // 2) Hardcoded fallback: sub-theme → base theme mapping from DB2 build 66838.
    // This covers the case where ParentThemeID is not properly loaded from DB2
    // (e.g., hotfix table schema mismatch). Only needed for sub-themes (6-28).
    // Base themes: 1=Folk, 2=Rugged, 3=Generic, 4=Bel'ameth, 5=Silvermoon
    switch (themeID)
    {
        // Folk (1) sub-themes
        case 6:  case 7:  case 20: return 1;
        // Rugged (2) sub-themes
        case 8:  case 9:  case 26: return 2;
        // Generic (3) sub-themes
        case 10: case 27: return 3;
        // Bel'ameth (4) sub-themes
        case 11: case 12: case 28: return 4;
        // Silvermoon (5) sub-themes
        case 13: return 5;
        default: break;
    }

    TC_LOG_DEBUG("housing", "HousingMgr::GetBaseThemeID: Unknown themeID {} (no ParentThemeID, not in fallback)", themeID);
    return themeID;
}

RoomComponentOptionEntry const* HousingMgr::FindRoomComponentOption(int32 meshStyleFilterID, int32 houseThemeID) const
{
    // Returns the Type=0 (Cosmetic) option for the given (MSFID, theme).
    uint64 key = (uint64(uint32(meshStyleFilterID)) << 32) | uint32(houseThemeID);
    auto itr = _roomCompOptionIndex.find(key);
    return itr != _roomCompOptionIndex.end() ? itr->second : nullptr;
}

std::vector<RoomComponentOptionEntry const*> HousingMgr::FindAllRoomComponentOptions(int32 meshStyleFilterID, int32 houseThemeID) const
{
    // Returns ALL options for the given (MSFID, theme): Cosmetic, DoorwayWall, Doorway.
    // Alliance sniff shows corners have 2 Cosmetic options (SubType 0+1) and
    // doorway walls have DoorwayWall (Type=1) + Doorway (Type=2) options.
    std::vector<RoomComponentOptionEntry const*> results;
    for (RoomComponentOptionEntry const* entry : sRoomComponentOptionStore)
    {
        if (!entry)
            continue;
        if (entry->MeshStyleFilterID == meshStyleFilterID && entry->HouseThemeID == houseThemeID)
            results.push_back(entry);
    }
    return results;
}

uint32 HousingMgr::GetDefaultVisualRoomEntry() const
{
    // Sniff-verified: both alliance and horde use HouseRoomID=1 ("Square Room Small")
    // as the primary interior room. The faction theme (themeID 1=Folk/Alliance, 2=Rugged/Horde)
    // controls wall/floor textures via RoomComponentOption, not the room shape.
    // Pick the lowest-ID non-base room with UNLOCKED_BY_DEFAULT + visual components.
    uint32 bestId = 0;
    uint32 fallbackId = 0;

    for (auto const& [id, roomData] : _houseRoomStore)
    {
        if (roomData.IsBaseRoom())
            continue;

        auto const* comps = GetRoomComponents(roomData.RoomWmoDataID);
        if (!comps || comps->size() <= 1)
            continue;

        if (roomData.Flags & HOUSING_ROOM_FLAG_UNLOCKED_BY_DEFAULT)
        {
            // Pick lowest ID for determinism (room 1 = Square Room Small, the sniff default)
            if (!bestId || id < bestId)
                bestId = id;
        }
        else if (!fallbackId || id < fallbackId)
        {
            fallbackId = id;
        }
    }

    uint32 result = bestId ? bestId : fallbackId;
    TC_LOG_ERROR("housing", "HousingMgr::GetDefaultVisualRoomEntry: bestId={} fallbackId={} -> returning {}",
        bestId, fallbackId, result);
    return result;
}

// ---------------------------------------------------------------------------
// House-finder per-player ignore list
// (CMSG_HOUSING_SVCS_HOUSE_FINDER_IGNORE_NEIGHBORHOOD, build 12.1.0.69497)
// Lazily loaded from character_housing_ignored_neighborhood, cached, write-through.
// ---------------------------------------------------------------------------
std::unordered_set<ObjectGuid>& HousingMgr::EnsureIgnoredNeighborhoodsLoaded(ObjectGuid playerGuid)
{
    auto itr = _ignoredNeighborhoods.find(playerGuid);
    if (itr != _ignoredNeighborhoods.end())
        return itr->second;

    std::unordered_set<ObjectGuid>& set = _ignoredNeighborhoods[playerGuid];

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_HOUSING_IGNORED_NEIGHBORHOOD);
    stmt->setUInt64(0, playerGuid.GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        do
        {
            uint64 counter = (*result)[0].GetUInt64();
            // Neighborhood GUIDs are HighGuid::Housing subType 4; only the counter is stored.
            // Resolve the live neighborhood to reconstruct the exact full GUID for comparison.
            if (Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhoodByCounter(counter))
                set.insert(neighborhood->GetGuid());
        } while (result->NextRow());
    }

    return set;
}

bool HousingMgr::IsNeighborhoodIgnored(ObjectGuid playerGuid, ObjectGuid neighborhoodGuid)
{
    std::unordered_set<ObjectGuid> const& set = EnsureIgnoredNeighborhoodsLoaded(playerGuid);
    return set.find(neighborhoodGuid) != set.end();
}

void HousingMgr::AddIgnoredNeighborhood(ObjectGuid playerGuid, ObjectGuid neighborhoodGuid)
{
    std::unordered_set<ObjectGuid>& set = EnsureIgnoredNeighborhoodsLoaded(playerGuid);
    if (!set.insert(neighborhoodGuid).second)
        return; // already ignored

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_HOUSING_IGNORED_NEIGHBORHOOD);
    stmt->setUInt64(0, playerGuid.GetCounter());
    stmt->setUInt64(1, neighborhoodGuid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "HousingMgr::AddIgnoredNeighborhood: player {} ignores neighborhood {}",
        playerGuid.ToString(), neighborhoodGuid.ToString());
}

void HousingMgr::RemoveIgnoredNeighborhood(ObjectGuid playerGuid, ObjectGuid neighborhoodGuid)
{
    std::unordered_set<ObjectGuid>& set = EnsureIgnoredNeighborhoodsLoaded(playerGuid);
    if (!set.erase(neighborhoodGuid))
        return;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_HOUSING_IGNORED_NEIGHBORHOOD);
    stmt->setUInt64(0, playerGuid.GetCounter());
    stmt->setUInt64(1, neighborhoodGuid.GetCounter());
    CharacterDatabase.Execute(stmt);
}

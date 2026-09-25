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

#include "HousingMap.h"
#include "Account.h"
#include "HousingNeighborhoodMirrorEntity.h"
#include "HousingPlayerHouseEntity.h"
#include "HousingRoomEntity.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <set>
#include "AreaTrigger.h"
#include "EventProcessor.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "GameObject.h"
#include "GridDefines.h"
#include "Housing.h"
#include "HousingDefines.h"
#include "HousingMgr.h"
#include "ScriptMgr.h"
#include "HousingPackets.h"
#include "Log.h"
#include "MapManager.h"
#include "MeshObject.h"
#include "Neighborhood.h"
#include "NeighborhoodMgr.h"
#include "ObjectAccessor.h"
#include "ObjectGridLoader.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "QueryPackets.h"
#include "RealmList.h"
#include "SocialMgr.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellPackets.h"
#include "UpdateData.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldStateMgr.h"

HousingMap::HousingMap(uint32 id, time_t expiry, uint32 instanceId, Difficulty spawnMode, uint32 neighborhoodId)
    : Map(id, expiry, instanceId, spawnMode), _neighborhoodId(neighborhoodId), _neighborhood(nullptr)
{
    // Prevent the map from being unloaded — housing maps are persistent
    // Map::CanUnload() returns false when m_unloadTimer == 0
    m_unloadTimer = 0;
    HousingMap::InitVisibilityDistance();

    // Verify InstanceType is MAP_HOUSE_NEIGHBORHOOD (8).
    // The client's "Airlock" system sets field_32=2 ONLY when InstanceType==8.
    // Without this, IsInsidePlot() always returns false → OutsidePlotBounds on ALL decor placement.
    if (GetEntry()->InstanceType != MAP_HOUSE_NEIGHBORHOOD)
    {
        TC_LOG_ERROR("housing", "CRITICAL: HousingMap {} '{}' has InstanceType={}, expected {} (MAP_HOUSE_NEIGHBORHOOD). "
            "Client will NOT allow decor placement — OutsidePlotBounds will always fire!",
            id, GetEntry()->MapName[sWorld->GetDefaultDbcLocale()],
            GetEntry()->InstanceType, MAP_HOUSE_NEIGHBORHOOD);
    }
    else
    {
        TC_LOG_DEBUG("housing", "HousingMap::ctor: mapId={} neighborhoodId={} instanceId={} "
            "InstanceType={} (MAP_HOUSE_NEIGHBORHOOD) — OK",
            id, neighborhoodId, instanceId, GetEntry()->InstanceType);
    }
}

HousingMap::~HousingMap()
{
    _playerHousings.clear();
}

void HousingMap::InitVisibilityDistance()
{
    // With MAX_VISIBILITY_DISTANCE (533y) this map sent about twenty times as many creates
    // as retail: every player received CREATE_OBJECT for every decor / mesh / fixture on
    // the map regardless of where they stood.
    // Retail uses a bounded distance too, and it sends plot area triggers, cornerstones and a house's rooms out of range
    // like any other object (hbcd3: plot triggers at 850898 and 858758, a cornerstone first at 478045, a house's root
    // entity and rooms at 545900, 584813 and 584822). This core still marks those SetFarVisible(true) at spawn time
    // (SpawnPlotGameObjects, SpawnPlotAreaTrigger and HousingRoomEntity::Create), so they are seen from farther away than
    // the rest. That is our own choice, not retail's, made because the client finds the plot she stands on (IsInsidePlot)
    // and the cornerstones for the house finder by looking them up among the objects it holds; whether it is still
    // needed is for a playtest to show. Everything else (decor, fixtures, component meshes) streams via grid visibility
    // once the player is within 200y. 200y is wide enough that adjacent plots remain visible while keeping per-player
    // update traffic bounded.
    m_VisibleDistance = 200.0f;
    m_VisibilityNotifyPeriod = sWorld->getIntConfig(CONFIG_VISIBILITY_NOTIFY_PERIOD_INSTANCE);
}

void HousingMap::LoadGridObjects(NGridType* grid)
{
    Map::LoadGridObjects(grid);
}

void HousingMap::SpawnPlotGameObjects()
{
    if (!_neighborhood)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: _neighborhood is NULL for map {} instanceId {} neighborhoodId {}",
            GetId(), GetInstanceId(), _neighborhoodId);
        return;
    }

    uint32 neighborhoodMapId = _neighborhood->GetNeighborhoodMapID();
    std::vector<NeighborhoodPlotData const*> plots = sHousingMgr.GetPlotsForMap(neighborhoodMapId);

    TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: map={} instanceId={} neighborhoodMapId={} plotCount={}",
        GetId(), GetInstanceId(), neighborhoodMapId, uint32(plots.size()));

    if (plots.empty())
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: NO plots found for neighborhoodMapId={} (neighborhood='{}') - check DB2 NeighborhoodPlot data",
            neighborhoodMapId, _neighborhood->GetName());
        return;
    }

    uint32 goCount = 0;
    uint32 noEntryCount = 0;

    TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: {} plots on map {}:",
        uint32(plots.size()), neighborhoodMapId);
    for (NeighborhoodPlotData const* plot : plots)
    {
        TC_LOG_DEBUG("housing", "  DB2 ID={} PlotIndex={} CornerstoneGameObjectID={} Cost={} WorldState={} PlotGameObjectID={}",
            plot->ID, plot->PlotIndex, plot->CornerstoneGameObjectID, plot->Cost, plot->WorldState, plot->PlotGameObjectID);
    }

    for (NeighborhoodPlotData const* plot : plots)
    {
        Neighborhood::PlotInfo const* plotInfo = _neighborhood->GetPlotInfo(static_cast<uint8>(plot->PlotIndex));
        bool isOwned = plotInfo && !plotInfo->OwnerGuid.IsEmpty();

        if (!plot->CornerstoneGameObjectID)
        {
            TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: Plot {} has CornerstoneGameObjectID=0 - skipping",
                plot->PlotIndex);
            ++noEntryCount;
            continue;
        }

        // Every plot gets a cornerstone of the one shared entry. The plot's own CornerstoneGameObjectID only names
        // which plot it stands for, through CreatedBy.
        GameObjectsEntry const* clientRow = sGameObjectsStore.LookupEntry(static_cast<uint32>(plot->CornerstoneGameObjectID));
        Position pos;
        QuaternionData rot;
        HousingMgr::GetCornerstonePlacement(*plot, GetId(), clientRow, pos, rot);
        LoadGrid(pos.GetPositionX(), pos.GetPositionY());

        TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: Plot {} cornerstone at ({:.1f}, {:.1f}, {:.1f}) for {} (owned={})",
            plot->PlotIndex, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), plot->CornerstoneGameObjectID,
            isOwned ? "yes" : "no");

        // An owned plot's cornerstone has State 0 and a vacant plot's State 1, as in hbcd3.
        GOState plotState = isOwned ? GO_STATE_ACTIVE : GO_STATE_READY;

        GameObject* go = GameObject::CreateGameObject(GAMEOBJECT_HOUSING_CORNERSTONE, this, pos, rot, 255, plotState);
        if (!go)
        {
            TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: Failed to create cornerstone {} at ({}, {}, {}) for plot {} in neighborhood '{}'",
                GAMEOBJECT_HOUSING_CORNERSTONE, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), plot->PlotIndex,
                _neighborhood->GetName());
            continue;
        }

        // Retail sniff: all Cornerstone GOs have Flags=32 (GO_FLAG_NODESPAWN)
        go->SetFlag(GO_FLAG_NODESPAWN);

        // CreatedBy names the plot the way retail does. It is not an owner: the cornerstone is not a summon and is
        // not despawned with anything.
        go->SetCreatedByGUID(HousingMgr::MakeCornerstoneCreator(*plot, GetId()));

        // Housing objects are dynamically spawned (no DB spawn record), so they have no
        // phase_area association. Explicitly mark them as universally visible so they're
        // seen by players regardless of what phases the player has from area-based phasing.
        PhasingHandler::InitDbPhaseShift(go->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);

        // Populate the FJamHousingCornerstone_C entity fragment so the client
        // knows this is a Cornerstone and can render the "For Sale" / owned UI
        go->InitHousingCornerstoneData(plot->Cost, static_cast<int32>(plot->PlotIndex));

        if (!AddToMap(go))
        {
            delete go;
            TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: Failed to add the cornerstone to map for plot {} in neighborhood '{}'",
                plot->PlotIndex, _neighborhood->GetName());
            continue;
        }

        // Far visible, so a character sees cornerstones from farther away than the map's visibility distance. It is not an
        // active object: its grid is kept loaded by LockPlotGrids, and a cornerstone has nothing to do while nobody is near
        // it, so it does not have to be updated every tick with the cells around it.
        go->SetFarVisible(true);

        // Track the plot GO for later swap (purchase/eviction)
        _plotGameObjects[static_cast<uint8>(plot->PlotIndex)] = go->GetGUID();

        TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: Plot {} cornerstone guid={} createdBy={}",
            plot->PlotIndex, go->GetGUID().ToString(), go->GetCreatorGUID().ToString());

        ++goCount;

        // The plot's area trigger stands on owned plots only (SpawnPlotAreaTrigger says why).
        if (isOwned)
            SpawnPlotAreaTrigger(static_cast<uint8>(plot->PlotIndex));
    }

    // The per-plot WorldState from NeighborhoodPlot.db2 is a BINARY occupancy flag that retail
    // ships inside SMSG_INIT_WORLD_STATES (seen in the sniff dump_12.0.1.66838_2026-04-15:
    //   - interior map 2783: all 55 plot worldstates = 0 (nothing yet visible)
    //   - exterior map 2735: occupied plots = 1, empty plots = 0
    // The VALUE is not the owner-type enum — it's purely "is a house here?". Owner identity is
    // carried in NeighborhoodMirrorData.Houses, which is populated elsewhere.
    //
    // Setting the values through Map::SetWorldStateValue() at spawn time lands them in
    // _worldStateValues, so the next SMSG_INIT_WORLD_STATES already carries them. This
    // replaces the per-player SMSG_UPDATE_WORLD_STATE spam in SendPerPlayerPlotWorldStates()
    // with the correct channel.
    uint32 occupiedWs = 0;
    uint32 emptyWs = 0;
    for (NeighborhoodPlotData const* plot : plots)
    {
        uint8 plotIdx = static_cast<uint8>(plot->PlotIndex);
        // The plot's world state is NeighborhoodPlot.WorldState; a plot without one has none. A plot with no
        // cornerstone was already reported above, so it is not reported twice.
        if (plot->WorldState <= 0)
        {
            if (plot->CornerstoneGameObjectID)
                TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: Plot {} on neighborhood map {} has no WorldState in NeighborhoodPlot",
                    plot->PlotIndex, neighborhoodMapId);
            continue;
        }

        uint32 wsId = uint32(plot->WorldState);
        Neighborhood::PlotInfo const* pi = _neighborhood->GetPlotInfo(plotIdx);
        bool occupied = pi && pi->IsOccupied() && !pi->HouseGuid.IsEmpty();
        SetWorldStateValue(wsId, occupied ? 1 : 0, /*hidden*/ false);
        TC_LOG_DEBUG("housing", "  PlotWS[{}] WorldState={} value={} (owner={} house={})",
            plot->PlotIndex, wsId,
            occupied ? 1 : 0,
            pi ? pi->OwnerGuid.ToString() : "n/a",
            pi ? pi->HouseGuid.ToString() : "n/a");
        if (occupied) ++occupiedWs; else ++emptyWs;
    }

    TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: Spawned {} GOs, {} plots occupied / {} empty "
        "(worldstate binary) for {} plots in neighborhood '{}' (noEntry={})",
        goCount, occupiedWs, emptyWs, uint32(plots.size()), _neighborhood->GetName(), noEntryCount);

    // Spawn house structure GOs for owned plots
    uint32 houseCount = 0;
    uint32 houseSuccessCount = 0;
    for (NeighborhoodPlotData const* plot : plots)
    {
        uint8 plotIdx = static_cast<uint8>(plot->PlotIndex);
        Neighborhood::PlotInfo const* plotInfo = _neighborhood->GetPlotInfo(plotIdx);
        if (!plotInfo || plotInfo->OwnerGuid.IsEmpty())
            continue;

        TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: Plot {} is owned by {} - attempting house spawn",
            plotIdx, plotInfo->OwnerGuid.ToString());

        // Spawn data comes from the DB via Neighborhood::LoadFromDB regardless of
        // whether the plot's owner is currently online. The live `Housing*` is
        // only used for fields that Housing computes at runtime (custom position,
        // root-type overrides derived from fixture selection). When null, we fall
        // back to PlotInfo fields mirrored from the DB.
        Housing* housing = GetHousingForHouse(plotInfo->HouseGuid);

        int32 exteriorComponentID = 0;
        int32 houseExteriorWmoDataID = 0;
        FixtureOverrideMap fixtureOverrides;
        RootOverrideMap rootOverrides;

        if (housing)
        {
            exteriorComponentID = static_cast<int32>(housing->GetCoreExteriorComponentID());
            houseExteriorWmoDataID = static_cast<int32>(housing->GetHouseType());
            fixtureOverrides = housing->GetFixtureOverrideMap();
            rootOverrides = housing->GetRootComponentOverrides();
        }
        else if (plotInfo->HouseType != 0)
        {
            houseExteriorWmoDataID = static_cast<int32>(plotInfo->HouseType);

            // Apply the mirrored fixture overrides from PlotInfo.Fixtures. This
            // mirrors character_housing_fixtures at Neighborhood::LoadFromDB so
            // visitors see each neighbour's customised roof/doors/windows, not
            // the raw default.
            for (auto const& [pointId, optionId] : plotInfo->Fixtures)
                fixtureOverrides[pointId] = optionId;

            // Pick the core fixture for the house. Prefer an override with OptionId==0
            // (player-selected base, same rule Housing::GetCoreExteriorComponentID uses);
            // fall back to DB2 IsDefault, then the first root entry.
            for (auto const& [pointId, optionId] : plotInfo->Fixtures)
            {
                if (optionId != 0)
                    continue;
                ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(pointId);
                if (comp && comp->ParentComponentID == 0 && comp->HouseExteriorWmoDataID == static_cast<int32>(plotInfo->HouseType))
                {
                    exteriorComponentID = static_cast<int32>(pointId);
                    break;
                }
            }
            if (!exteriorComponentID)
            {
                auto const* roots = sHousingMgr.GetRootComponentsForWmoData(plotInfo->HouseType);
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
                        {
                            fallbackComp = compID;
                            break;
                        }
                    }
                    exteriorComponentID = static_cast<int32>(fallbackComp);
                }
            }

            TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: Plot {} owner {} (offline) — using mirrored HouseType={} ExtComp={} fixtures={} decor={} from PlotInfo",
                plotIdx, plotInfo->OwnerGuid.ToString(), houseExteriorWmoDataID, exteriorComponentID,
                uint32(plotInfo->Fixtures.size()), uint32(plotInfo->Decor.size()));
        }
        else
        {
            TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: Plot {} owned by {} but PlotInfo.HouseType=0 — cannot spawn house",
                plotIdx, plotInfo->OwnerGuid.ToString());
            continue;
        }

        if (!exteriorComponentID || !houseExteriorWmoDataID)
        {
            TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: Plot {} has invalid data: ExteriorComponentID={}, WmoDataID={}",
                plotIdx, exteriorComponentID, houseExteriorWmoDataID);
            continue;
        }
        TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: Plot {} using ExteriorComponentID={}, WmoDataID={}",
            plotIdx, exteriorComponentID, houseExteriorWmoDataID);

        FixtureOverrideMap const* overridesPtr = fixtureOverrides.empty() ? nullptr : &fixtureOverrides;
        RootOverrideMap const* rootOvrPtr = rootOverrides.empty() ? nullptr : &rootOverrides;

        // A house its owner moved stands where she put it; while no character of her account is on the map the plot's
        // copy of the house carries the placement.
        bool built = false;
        if (housing && housing->HasCustomPosition())
        {
            Position customPos = housing->GetHousePosition();
            built = SpawnHouseForPlot(plotIdx, &customPos, exteriorComponentID, houseExteriorWmoDataID, overridesPtr, rootOvrPtr);
        }
        else if (!housing && plotInfo->HasHousePlacement)
            built = SpawnHouseForPlot(plotIdx, &plotInfo->HousePlacement, exteriorComponentID, houseExteriorWmoDataID, overridesPtr, rootOvrPtr);
        else
            built = SpawnHouseForPlot(plotIdx, nullptr, exteriorComponentID, houseExteriorWmoDataID, overridesPtr, rootOvrPtr);
        ++houseCount;
        if (built)
            ++houseSuccessCount;
        else
            TC_LOG_ERROR("housing", "HousingMap::SpawnPlotGameObjects: FAILED to spawn house for plot {} owned by {}",
                plotIdx, plotInfo->OwnerGuid.ToString());

        // Spawn placed decor — live Housing path preferred (may include items
        // placed but not yet saved). Otherwise iterate PlotInfo.Decor from DB.
        if (housing)
        {
            SpawnAllDecorForPlot(plotIdx, housing);
        }
        else
        {
            uint32 spawnedDecor = 0;
            for (Housing::PlacedDecor const& decor : plotInfo->Decor)
            {
                if (!Housing::IsExteriorDecorPlacement(decor.RoomGuid))
                    continue; // exterior-only at preload
                if (SpawnDecorItem(plotIdx, decor, plotInfo->HouseGuid))
                    ++spawnedDecor;
            }
            _decorSpawnedPlots.insert(plotIdx);
            if (spawnedDecor || !plotInfo->Decor.empty())
                TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: Plot {} (offline owner) — spawned {} exterior decor from PlotInfo ({} total decor entries cached)",
                    plotIdx, spawnedDecor, uint32(plotInfo->Decor.size()));
        }
    }

    TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotGameObjects: House spawn results: {}/{} successful for neighborhood '{}'",
        houseSuccessCount, houseCount, _neighborhood->GetName());
}

void HousingMap::LockPlotGrids()
{
    if (!_neighborhood)
        return;

    uint32 neighborhoodMapId = _neighborhood->GetNeighborhoodMapID();
    std::vector<NeighborhoodPlotData const*> plots = sHousingMgr.GetPlotsForMap(neighborhoodMapId);
    std::set<std::pair<uint32, uint32>> lockedGrids;

    for (NeighborhoodPlotData const* plot : plots)
    {
        // Lock grid for cornerstone position
        GridCoord cornerstoneGrid = Trinity::ComputeGridCoord(plot->CornerstonePosition[0], plot->CornerstonePosition[1]);
        if (lockedGrids.insert({ cornerstoneGrid.x_coord, cornerstoneGrid.y_coord }).second)
            GridMarkNoUnload(cornerstoneGrid.x_coord, cornerstoneGrid.y_coord);

        // Lock grid for the plot's room anchor, where its house stands (may be a different grid)
        Position roomAnchor;
        if (sHousingMgr.GetPlotRoomAnchor(neighborhoodMapId, static_cast<uint8>(plot->PlotIndex), roomAnchor))
        {
            GridCoord houseGrid = Trinity::ComputeGridCoord(roomAnchor.GetPositionX(), roomAnchor.GetPositionY());
            if (lockedGrids.insert({ houseGrid.x_coord, houseGrid.y_coord }).second)
                GridMarkNoUnload(houseGrid.x_coord, houseGrid.y_coord);
        }
    }

    TC_LOG_DEBUG("housing", "HousingMap::LockPlotGrids: Locked {} grids for {} plots in neighborhood '{}'",
        lockedGrids.size(), plots.size(), _neighborhood->GetName());
}

bool HousingMap::SpawnPlotAreaTrigger(uint8 plotIndex)
{
    if (GetPlotAreaTrigger(plotIndex))
        return true;

    // Spawn plot AreaTrigger (entry 37358) above the plot's room anchor.
    // Sniff-verified: Box shape 35x30x94, DecalPropertiesId=621 (plot boundary visual),
    // SpellForVisuals=1282351.
    // The AT is required for the client to show the edit menu and plot boundary decal.
    //
    // OWNED PLOTS ONLY. Retail never creates this AreaTrigger for an unsold plot: across four
    // WowPacketParser-decoded housing sniffs (builds 65940 x2 and 11.2.7 x2, maps 2735 and 2736) there are
    // 55 CreateObject1 blocks for entry 37358 and HouseGUID is non-zero in 55 of 55 - none for an unsold
    // plot. The creation is caused by the purchase: CMSG_NEIGHBORHOOD_BUY_HOUSE -> worldstate 0->1 ->
    // cornerstone State 1->0 -> the AT appears -> HousingRoom appears. It is not a visibility artifact
    // either; the observed player was standing at the cornerstone, well inside the AT's 46 yd bounds.
    //
    // Spawning it unconditionally is what painted a 70x60 brown slab (DecalPropertiesId 621, half-extents
    // 35x30) across every empty plot. The 70x60 marker a player SHOULD see on an unsold plot is drawn by
    // the client itself from its own GameObjects.db2 row (PlotGameObjectID, DisplayID 113004, GeoBox
    // 70x60x0), gated on the plot worldstate - the server neither sends nor spawns it.
    //
    // So it is made whenever a plot becomes owned (SetPlotOwnershipState) and when the map is made for plots owned then,
    // and taken away when the plot is freed (DespawnPlotAreaTrigger).
    uint32 const neighborhoodMapId = _neighborhood ? _neighborhood->GetNeighborhoodMapID() : 0;
    Position roomAnchor;
    if (!sHousingMgr.GetPlotRoomAnchor(neighborhoodMapId, plotIndex, roomAnchor))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnPlotAreaTrigger: plot {} has no \"Plot - Plot N\" row in GameObjects.db2 on map {}, "
            "so it gets no plot area trigger", plotIndex, GetId());
        return false;
    }

    // Retail's trigger stands at the room anchor, turned like the room, HOUSING_PLOT_AREATRIGGER_HEIGHT higher.
    float hx = roomAnchor.GetPositionX();
    float hy = roomAnchor.GetPositionY();
    float hz = roomAnchor.GetPositionZ() + HOUSING_PLOT_AREATRIGGER_HEIGHT;

    LoadGrid(hx, hy);

    Position atPos(hx, hy, hz, roomAnchor.GetOrientation());
    // Create with addToMap=false so we can set up ALL housing data (entity
    // fragment, SpellForVisuals, SpellXSpellVisualID) BEFORE the CREATE_OBJECT
    // packet is sent. The client needs the visual fields and
    // DecalPropertiesId=621 in the initial create to render the plot border decal.
    AreaTrigger* plotAt = AreaTrigger::CreateStaticAreaTrigger({ .Id = 37358, .IsCustom = false }, this, atPos, -1, false);
    if (!plotAt)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnPlotAreaTrigger: Failed to create plot AT (entry 37358) for plot {} at ({:.1f},{:.1f},{:.1f})",
            plotIndex, hx, hy, hz);
        return false;
    }

    PhasingHandler::InitDbPhaseShift(plotAt->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);

    // 12.0.5: no per-AT housing fragment. Only set the AT's own visual fields
    // (SpellForVisuals, PeriodModifier, ExtraScaleCurve). Plot ownership is
    // now propagated via PlayerHouseInfoComponentData.CurrentHouse on the Player.
    plotAt->InitHousingPlotVisuals();

    if (!AddToMap(plotAt))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnPlotAreaTrigger: AddToMap failed for plot AT (entry 37358) plot {} at ({:.1f},{:.1f},{:.1f})",
            plotIndex, hx, hy, hz);
        delete plotAt;
        return false;
    }

    // Far visible, so a character's client holds the trigger from farther away than the map's visibility distance. The
    // client looks the trigger up among the objects it holds to decide whether she is inside a plot (IsInsidePlot), and
    // decor placement and the plot owner's menus need that. Retail does not do this: it sends plot triggers out of range
    // like other objects (hbcd3 850898, 858758, and her own bought plot's trigger at 1577921 and 2127635). It is this
    // core's choice until a playtest shows it can go.
    // Separately, the trigger stays an active object, updated every tick, so it notices a character who leaves it by a
    // teleport to a far part of the map, where no cell around her would update it any more.
    plotAt->setActive(true);
    plotAt->SetFarVisible(true);

    _plotAreaTriggers[plotIndex] = plotAt->GetGUID();

    Neighborhood::PlotInfo const* plotInfo = _neighborhood ? _neighborhood->GetPlotInfo(plotIndex) : nullptr;
    TC_LOG_DEBUG("housing", "HousingMap::SpawnPlotAreaTrigger: Plot {} AT entry=37358 guid={} at ({:.1f},{:.1f},{:.1f}) owner={} DecalPropertiesID=621",
        plotIndex, plotAt->GetGUID().ToString(), hx, hy, hz,
        plotInfo && !plotInfo->OwnerGuid.IsEmpty() ? plotInfo->OwnerGuid.ToString() : std::string("none"));
    return true;
}

void HousingMap::DespawnPlotAreaTrigger(uint8 plotIndex)
{
    auto itr = _plotAreaTriggers.find(plotIndex);
    if (itr == _plotAreaTriggers.end())
        return;

    ObjectGuid const atGuid = itr->second;
    // Taken out of the plot list first, so the trigger no longer stands for the plot while it is being removed.
    _plotAreaTriggers.erase(itr);

    // Whoever stands on the freed plot leaves it as she would by walking off it (at_housing_plot's OnUnitExit). The
    // trigger's own removal does not do this: the script only acts on a character who walks out of it.
    std::vector<ObjectGuid> onPlot;
    for (auto const& [playerGuid, currentPlot] : _playerCurrentPlot)
        if (currentPlot == plotIndex)
            onPlot.push_back(playerGuid);

    for (ObjectGuid const& playerGuid : onPlot)
    {
        ClearPlayerCurrentPlot(playerGuid);
        if (Player* player = GetPlayer(playerGuid))
        {
            SendPlotLeaveAuraRemoval(player);
            player->SetCurrentHouse(ObjectGuid::Empty);
        }
    }

    if (AreaTrigger* plotAt = GetAreaTrigger(atGuid))
        plotAt->Remove();

    TC_LOG_DEBUG("housing", "HousingMap::DespawnPlotAreaTrigger: plot {} lost its area trigger {} ({} character(s) taken off the plot)",
        plotIndex, atGuid.ToString(), uint32(onPlot.size()));
}

AreaTrigger* HousingMap::GetPlotAreaTrigger(uint8 plotIndex)
{
    auto itr = _plotAreaTriggers.find(plotIndex);
    if (itr == _plotAreaTriggers.end())
        return nullptr;

    return GetAreaTrigger(itr->second);
}

int8 HousingMap::GetPlotIndexForAreaTrigger(ObjectGuid atGuid) const
{
    for (auto const& [plotIdx, guid] : _plotAreaTriggers)
        if (guid == atGuid)
            return static_cast<int8>(plotIdx);
    return -1;
}

int8 HousingMap::GetPlotIndexForCornerstone(ObjectGuid cornerstoneGuid) const
{
    for (auto const& [plotIdx, guid] : _plotGameObjects)
        if (guid == cornerstoneGuid)
            return static_cast<int8>(plotIdx);
    return -1;
}

GameObject* HousingMap::GetPlotGameObject(uint8 plotIndex)
{
    auto itr = _plotGameObjects.find(plotIndex);
    if (itr == _plotGameObjects.end())
        return nullptr;

    return GetGameObject(itr->second);
}

void HousingMap::SetPlotOwnershipState(uint8 plotIndex, bool owned)
{
    if (!_neighborhood)
        return;

    // Toggle GOState on the existing Cornerstone GO.
    // GOState 0 (ACTIVE) = Owned/Claimed cornerstone, GOState 1 (READY) = ForSale sign
    GOState newState = owned ? GO_STATE_ACTIVE : GO_STATE_READY;

    auto itr = _plotGameObjects.find(plotIndex);
    if (itr != _plotGameObjects.end())
    {
        if (GameObject* go = GetGameObject(itr->second))
        {
            go->SetGoState(newState);

            TC_LOG_DEBUG("housing", "HousingMap::SetPlotOwnershipState: Plot {} GOState -> {} ({}) in neighborhood '{}'",
                plotIndex, uint32(newState), owned ? "owned" : "for-sale", _neighborhood->GetName());
        }
        else
        {
            TC_LOG_ERROR("housing", "HousingMap::SetPlotOwnershipState: Plot {} GO guid {} not found on map in neighborhood '{}'",
                plotIndex, itr->second.ToString(), _neighborhood->GetName());
        }
    }
    else
    {
        TC_LOG_ERROR("housing", "HousingMap::SetPlotOwnershipState: Plot {} has no tracked GO in neighborhood '{}'",
            plotIndex, _neighborhood->GetName());
    }

    // The area trigger carries no ownership. Plot ownership is communicated
    // via PlayerHouseInfoComponentData.CurrentHouse on each Player — updated by
    // the enter/leave-plot code paths (at_housing_plot).
    // An owned plot has the trigger and a free one has none, so a plot bought, unpacked onto or moved to while the map is
    // loaded gets it now, and a relinquished, evicted, packed or vacated plot loses it. A character already standing in
    // the new trigger's box enters it at its first update.
    if (owned)
        SpawnPlotAreaTrigger(plotIndex);
    else
        DespawnPlotAreaTrigger(plotIndex);

    // Update the plot's world state, which late joiners get in their INIT state and every
    // player already on the map gets as an UPDATE.
    uint32 neighborhoodMapId = _neighborhood->GetNeighborhoodMapID();
    std::vector<NeighborhoodPlotData const*> plots = sHousingMgr.GetPlotsForMap(neighborhoodMapId);

    for (NeighborhoodPlotData const* plotData : plots)
    {
        if (plotData->PlotIndex != static_cast<int32>(plotIndex))
            continue;

        // The plot's world state is NeighborhoodPlot.WorldState; a plot without one has none.
        if (plotData->WorldState <= 0)
            break;

        uint32 wsId = uint32(plotData->WorldState);

        // As on retail, the per-plot WorldState is a BINARY occupancy flag — 0 empty,
        // 1 occupied. `Map::SetWorldStateValue` stores the value (so future joins
        // get it in INIT_WORLD_STATES) AND broadcasts `SMSG_UPDATE_WORLD_STATE` to
        // every player currently on the map. No per-player enum override.
        SetWorldStateValue(wsId, owned ? 1 : 0, /*hidden*/ false);

        TC_LOG_DEBUG("housing", "SetPlotOwnershipState: ws={} value={} plot={} {} neighborhoodMap={}",
            wsId, owned ? 1 : 0, plotIndex, owned ? "occupied" : "empty", neighborhoodMapId);
        break;
    }
}

Housing* HousingMap::GetHousingForHouse(ObjectGuid houseGuid) const
{
    auto itr = _playerHousings.find(houseGuid);
    if (itr != _playerHousings.end())
        return itr->second;

    return nullptr;
}

void HousingMap::LoadNeighborhoodData()
{
    // Resolve by the persisted counter. Rebuilding the GUID here is not possible any more: arg1 is the
    // neighborhood's NeighborhoodMapID (see NeighborhoodMgr::GenerateNeighborhoodGuid), which this code does not
    // know, and guessing it wrong silently yields nullptr.
    _neighborhood = sNeighborhoodMgr.GetNeighborhoodByCounter(static_cast<uint64>(_neighborhoodId));

    if (!_neighborhood)
        TC_LOG_ERROR("housing", "HousingMap::LoadNeighborhoodData: Failed to load neighborhood {} for map {} instanceId {}",
            _neighborhoodId, GetId(), GetInstanceId());
    else
        TC_LOG_DEBUG("housing", "HousingMap::LoadNeighborhoodData: Loaded neighborhood '{}' (id: {}) for map {} instanceId {}",
            _neighborhood->GetName(), _neighborhoodId, GetId(), GetInstanceId());
}

bool HousingMap::AddPlayerToMap(Player* player, bool initPlayer /*= true*/)
{
    if (!_neighborhood)
    {
        TC_LOG_ERROR("housing", "HousingMap::AddPlayerToMap: No neighborhood loaded for map {} instanceId {}",
            GetId(), GetInstanceId());
        return false;
    }

    // Enforce max players on housing map
    if (GetPlayersCountExceptGMs() >= MAX_HOUSING_MAP_PLAYERS)
    {
        TC_LOG_DEBUG("housing", "HousingMap::AddPlayerToMap: Map {} full ({} players), rejecting player {}",
            GetId(), GetPlayersCountExceptGMs(), player->GetGUID().ToString());
        player->SendTransferAborted(GetId(), TRANSFER_ABORT_HOUSING_MAX_PLAYERS_IN_HOUSE);
        return false;
    }

    // Do NOT auto-add the player as a neighborhood member here.
    // Membership is granted when the player buys a plot or is invited.
    // Auto-adding causes the client to resolve neighborhoodOwnerType as
    // Self instead of None, which prevents the "For Sale" Cornerstone UI.

    // Track the house of the player's Battle.net account in this neighborhood, if it has one. Only a house whose
    // neighborhood is this one counts: a house's plot index alone says nothing about which neighborhood it is in.
    TC_LOG_DEBUG("housing", "HousingMap::AddPlayerToMap: Looking up housing for player {} in neighborhood '{}' (guid={})",
        player->GetGUID().ToString(), _neighborhood->GetName(), _neighborhood->GetGuid().ToString());

    Housing* housing = player->GetHousingForNeighborhood(_neighborhood->GetGuid());
    if (housing)
    {
        TC_LOG_DEBUG("housing", "HousingMap::AddPlayerToMap: Player {} has housing: plotIndex={} houseType={} houseGuid={}",
            player->GetGUID().ToString(), housing->GetPlotIndex(), housing->GetHouseType(), housing->GetHouseGuid().ToString());

        AddPlayerHousing(housing);

        // Ensure the neighborhood PlotInfo has the HouseGuid (may be missing after server restart
        // since LoadFromDB only populates it if character_housing row exists)
        uint8 plotIdx = housing->GetPlotIndex();
        ObjectGuid ownerBnetGuid = player->GetSession() ? player->GetSession()->GetBattlenetAccountGUID() : ObjectGuid::Empty;
        if (!housing->GetHouseGuid().IsEmpty())
        {
            _neighborhood->UpdatePlotHouseInfo(plotIdx, housing->GetHouseGuid(), ownerBnetGuid, housing->GetDatabaseId());

            // The area trigger carries no ownership; it reaches the client through
            // PlayerHouseInfoComponentData.CurrentHouse on the Player at plot entry.
        }

        // PlayerMirrorHouse.MapID stays the house interior's NeighborhoodMap row. Retail sends that value in every
        // entry, also in the sessions that opened the fixture editor on the plot (hled1 226129, 816931) and the decor
        // editor in the house (hbcd3 1431555), so the current map is not written into it here.

        // Build the house when it is not standing yet (its owner was offline when the map loaded, or it was bought
        // since).
        if (!IsHouseSpawned(plotIdx))
        {
            bool const built = SpawnHouseFromState(plotIdx, *housing);
            TC_LOG_DEBUG("housing", "HousingMap::AddPlayerToMap: SpawnHouseFromState result for plot {}: {}",
                plotIdx, built ? "spawned" : "FAILED");
        }

        // Spawn decor GOs if not already spawned for this plot
        SpawnAllDecorForPlot(plotIdx, housing);

        // The session's house entity names this house's exterior root in EntityGUID while she is on this map
        // (Housing::GetHouseEntityTargetFor).
    }
    else
    {
        TC_LOG_DEBUG("housing", "HousingMap::AddPlayerToMap: Player {} has NO housing in this neighborhood (no house will spawn)",
            player->GetGUID().ToString());
    }

    // The neighborhood mirror entity names the neighborhood of the map she is arriving on, with its data, before her own
    // create is built: retail creates it on the neighborhood map with the neighborhood's GUID (hbcd3 458453, Housing/
    // Neighborhood 0xDC80000200000000000000000000A584, the NeighborhoodGUID of the later house packets), whether or not
    // she has a house there. She is not in the world yet, so the entity is not either, and a far teleport has cleared the
    // client's objects, so a GUID she had before needs no destroy.
    if (WorldSession* session = player->GetSession())
    {
        HousingNeighborhoodMirrorEntity& mirrorEntity = session->GetHousingNeighborhoodMirrorEntity();
        if (!mirrorEntity.IsInWorld())
            mirrorEntity.ResetGuid(_neighborhood->GetGuid());

        // This neighborhood's houses and managers only ever go onto a mirror that names this neighborhood.
        if (mirrorEntity.GetGUID() == _neighborhood->GetGuid())
            _neighborhood->FillMirrorEntity(mirrorEntity);
        else
            TC_LOG_ERROR("housing", "HousingMap::AddPlayerToMap: the neighborhood mirror entity of {} is still in the world, so it keeps GUID {} and its data",
                player->GetGUID().ToString(), mirrorEntity.GetGUID().ToString());
    }

    if (!Map::AddPlayerToMap(player, initPlayer))
    {
        // She is not on the map, so no RemovePlayerFromMap will take her house out of the index.
        RemovePlayerHousing(player);
        return false;
    }

    // The neighborhood she is in, so that a later entry to this map without a neighborhood of her own brings her back
    // here instead of to another public one (MapManager::CreateMap). It lasts until she logs out.
    player->SetRecentInstance(GetId(), GetInstanceId());

    // Force immediate visibility update so all MeshObjects (house pieces, decor) get
    // CREATE_OBJECT sent to the player NOW, not deferred to the next map tick.
    // Map::AddPlayerToMap calls UpdateObjectVisibility(false) which only sets
    // NOTIFY_VISIBILITY_CHANGED — the actual grid visit is deferred until the next
    // relocation processing tick. Without forcing here, the client won't have MeshObject
    // entities when entering edit mode, causing an empty Placed Decor list.
    player->UpdateVisibilityForPlayer();

    // === DIAGNOSTIC: Report plot GO state when player enters ===
    {
        TC_LOG_DEBUG("housing", "=== HOUSING DIAGNOSTIC for player {} entering map {} ===", player->GetGUID().ToString(), GetId());
        TC_LOG_DEBUG("housing", "  Player position: ({:.1f}, {:.1f}, {:.1f})", player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
        TC_LOG_DEBUG("housing", "  _plotGameObjects.size={} Map InstanceType={} (expected {} for MAP_HOUSE_NEIGHBORHOOD)",
            uint32(_plotGameObjects.size()), GetEntry()->InstanceType, MAP_HOUSE_NEIGHBORHOOD);

        uint32 shown = 0;
        for (auto const& [plotIdx, goGuid] : _plotGameObjects)
        {
            if (shown >= 3) break;
            if (GameObject* go = GetGameObject(goGuid))
            {
                float dist = player->GetDistance(go);
                TC_LOG_DEBUG("housing", "  Plot[{}] GO: guid={} entry={} pos=({:.1f},{:.1f},{:.1f}) dist={:.1f}yd",
                    plotIdx, goGuid.ToString(), go->GetEntry(),
                    go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), dist);
            }
            ++shown;
        }
        TC_LOG_DEBUG("housing", "=== END HOUSING DIAGNOSTIC ===");
    }

    // As on retail, no housing server packet is sent at login without being asked for:
    // no SMSG_HOUSING_GET_CURRENT_HOUSE_INFO_RESPONSE, SMSG_HOUSING_CATALOG_STATE_SYNC,
    // SMSG_HOUSING_SVCS_GET_PLAYER_HOUSES_INFO_RESPONSE,
    // SMSG_HOUSING_SVCS_UPDATE_HOUSES_LEVEL_FAVOR or SMSG_INITIATIVE_SERVICE_STATUS.
    // Three retail 12.0.1 (build 66838) captures
    // (floorplan_editor_rotation 2026-04-10, wall_floor_ceiling_customize
    // 2026-04-12, interrior_exterrior_advanced_editor 2026-04-15) show no
    // housing packet the client did not ask for after the login verify world. The
    // client receives all housing state via the Player CREATE bundle's
    // UpdateField data and asks for anything else via CMSGs. The existing
    // reactive handlers (HandleHousingGetCurrentHouseInfo, HandleHousingSvcs
    // GetPlayerHousesInfo, HandleHousingHouseStatus, etc.) will respond
    // when queried.
    //
    // In the advanced editor sniff retail sends CATALOG_STATE_SYNC well after login, as a
    // time-delayed server push. Not at map-entry time. If the client
    // actively needs it before the delayed push, CMSG_HOUSING_DECOR_REQUEST_
    // STORAGE also triggers catalog dispatch via HandleHousingDecorRequestStorage.

    // ENTER_PLOT must be sent AFTER SMSG_UPDATE_OBJECT creates the AT on the client.
    // UPDATE_OBJECT is flushed after AddPlayerToMap returns, so sending ENTER_PLOT
    // here synchronously would reference an AT GUID the client doesn't know yet.
    // Solution: schedule a deferred event that sends ENTER_PLOT after a short delay,
    // giving the UPDATE_OBJECT time to flush to the client first.
    // The at_housing_plot AT overlap script also sends ENTER_PLOT when the player
    // physically enters the box, but on login the AT overlap check may not fire
    // on the first tick (player already inside the AT when it was created).
    // SetPlayerCurrentPlot is called here so the AT script's alreadyOnPlot guard
    // prevents duplicate ENTER_PLOT sends if both paths fire.
    if (housing)
    {
        uint8 plotIndex = housing->GetPlotIndex();
        SetPlayerCurrentPlot(player->GetGUID(), plotIndex);

        ObjectGuid playerGuid = player->GetGUID();
        ObjectGuid houseGuid = housing->GetHouseGuid();
        ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();
        uint8 deferredPlotIndex = plotIndex;
        uint32 const mapId = GetId();
        uint32 const instanceId = GetInstanceId();
        player->m_Events.AddEventAtOffset([playerGuid, deferredPlotIndex, houseGuid, neighborhoodGuid, mapId, instanceId]()
        {
            Player* p = ObjectAccessor::FindPlayer(playerGuid);
            if (!p || !p->IsInWorld())
                return;

            // The event runs on whichever map she is on by then; it belongs to this neighborhood only.
            HousingMap* hMap = dynamic_cast<HousingMap*>(p->GetMap());
            if (!hMap || hMap->GetId() != mapId || hMap->GetInstanceId() != instanceId)
                return;

            AreaTrigger* plotAt = hMap->GetPlotAreaTrigger(deferredPlotIndex);
            if (!plotAt)
            {
                TC_LOG_ERROR("housing", "HousingMap deferred ENTER_PLOT: No AT for plot {} player {}",
                    deferredPlotIndex, playerGuid.ToString());
                return;
            }

            // As retail does, send a VALUES update of the area trigger
            // at the same timestamp as ENTER_PLOT, ensuring the client entity table has fresh data.
            {
                UpdateData atUpdate(p->GetMapId());
                if (p->HaveAtClient(plotAt))
                    plotAt->BuildValuesUpdateBlockForPlayer(&atUpdate, p);
                else
                {
                    plotAt->BuildCreateUpdateBlockForPlayer(&atUpdate, p);
                    p->m_clientGUIDs.insert(plotAt->GetGUID());
                }
                if (atUpdate.HasData())
                {
                    WorldPacket atPacket;
                    atUpdate.BuildPacket(&atPacket);
                    p->SendDirectMessage(&atPacket);
                }
            }

            // REMOVED proactive SMSG_NEIGHBORHOOD_PLAYER_ENTER_PLOT (0x5C0000)
            // and SMSG_HOUSING_FIXTURE_CREATE_BASIC_HOUSE_RESPONSE (0x520001).
            // Sniff set-diff of 3 retail login captures shows retail never
            // emits either opcode at login / during the deferred-map-entry
            // window; both are strictly reactive to specific user actions
            // (AT overlap entry, fixture-editor click). Keeping the
            // proactive sends here put the client's housing-state machine
            // into pre-initialised state that suppressed the world-map
            // icon-picker refresh. The at_housing_plot AT script still
            // emits PLAYER_ENTER_PLOT (and HouseStatus+Permissions) on
            // actual plot overlap, which matches retail.
            TC_LOG_DEBUG("housing", "HousingMap deferred ENTER_PLOT: proactive PLAYER_ENTER_PLOT + FIXTURE_CREATE_BASIC_HOUSE suppressed for player {}",
                playerGuid.ToString());

            // The house's pieces are not created again here. They are grid objects and reached her through the map's
            // visibility, and retail never sends a second create for a GUID the client already holds.

            // Proactively populate FHousingStorage_C (decor list) and budget fields.
            // At login, PushHousingDecorStorage() is NOT called to avoid crashes when
            // storage data appears in the initial Account entity CREATE.
            //
            // CRITICAL: Must send Account as CREATE (not VALUES_UPDATE). The initial login
            // Account CREATE had an empty FHousingStorage_C.Decor MapUpdateField.
            // A VALUES_UPDATE for a MapUpdateField that was empty at CREATE time does NOT
            // properly convey new entries to the client — the client never processes them.
            // Sending a second CREATE with all current data works because the client handles
            // re-CREATE for an existing entity gracefully (replaces the old data).
            //
            // Also bundle ALL decor MeshObject CREATEs in the SAME UPDATE_OBJECT packet.
            // The client correlates MeshObject FHousingDecor_C.DecorGUID with Account
            // FHousingStorage_C entries to build the Placed Decor list. If they arrive in
            // separate packets, the client may not retroactively associate them.
            if (Housing* housing = p->GetHousingByGuid(houseGuid))
            {
                p->PushHousingDecorStorage();
                housing->SyncUpdateFields();

                // 12.0.5: write the player's own HouseGuid to PlayerHouseInfoComponent.CurrentHouse
                // so the client's HOUSE_PLOT_ENTERED field-change callback fires from the same
                // UPDATE_OBJECT bundle. Without this the AT enter event is the only path that
                // sets it, but at login the player is already inside the AT box so OnUnitEnter
                // doesn't trigger; the editor menu then never arms.
                p->SetCurrentHouse(housing->GetHouseGuid());

                // No house status, permissions or storage reply goes out here. Retail sends each of them only in
                // answer to the client's own request: every capture has as many replies as requests (hbcd3 five
                // status and five permissions requests, hled1 one of each, none in hf1 or hbst1), and nothing is
                // pushed on entering a plot.
                WorldSession* session = p->GetSession();

                // The account's decor storage and the house's budgets travel in update fields. They are sent here
                // together with the placed decor, so the client can match placed decor to storage entries.
                UpdateData storageUpdate(p->GetMapId());
                WorldPacket storagePacket;

                // Account entity as CREATE (includes full FHousingStorage_C with Decor map)
                session->GetBattlenetAccount().BuildCreateUpdateBlockForPlayer(&storageUpdate, p);
                p->m_clientSessionEntityGUIDs.insert(session->GetBattlenetAccount().GetGUID());

                // HousingPlayerHouseEntity (budgets)
                HousingPlayerHouseEntity& houseEntity = session->GetHousingPlayerHouseEntity(housing->GetHouseGuid());
                if (p->HaveAtClient(&houseEntity))
                    houseEntity.BuildValuesUpdateBlockForPlayer(&storageUpdate, p);
                else
                {
                    houseEntity.BuildCreateUpdateBlockForPlayer(&storageUpdate, p);
                    p->m_clientSessionEntityGUIDs.insert(houseEntity.GetGUID());
                }

                // Bundle ALL decor MeshObject CREATEs so the client can correlate
                // FHousingDecor_C.DecorGUID with FHousingStorage_C entries in one pass.
                uint32 meshCreateCount = 0;
                for (auto const& [decorGuid, meshObjGuid] : hMap->GetDecorGuidMap())
                {
                    MeshObject* meshObj = hMap->GetMeshObject(meshObjGuid);
                    if (!meshObj || !meshObj->IsInWorld())
                        continue;

                    meshObj->BuildCreateUpdateBlockForPlayer(&storageUpdate, p);
                    p->m_clientGUIDs.insert(meshObjGuid);
                    ++meshCreateCount;
                }

                storageUpdate.BuildPacket(&storagePacket);
                p->SendDirectMessage(&storagePacket);

                session->GetBattlenetAccount().ClearUpdateMask(true);
                houseEntity.ClearUpdateMask(true);

                // No PlayerHousesInfo reply is sent here: three retail 12.0.1 (build 66838)
                // login captures show none that the client did not ask for.

                TC_LOG_DEBUG("housing", "HousingMap deferred ENTER_PLOT: Sent Account CREATE + {} decor MeshObject CREATEs for player {}",
                    meshCreateCount, playerGuid.ToString());

                // The 500 ms defer sends no housing reply packets. Three retail
                // 12.0.1 (build 66838) login captures show ZERO housing packets
                // the client did not ask for after the login verify world, so no
                // neighborhood name reply, mirror VALUES_UPDATE or player name
                // reply is sent here. The CMSG handlers answer when the
                // client queries. The mirror state was populated synchronously
                // in Player::LoadFromDB and rides in the Player CREATE bundle.
                (void)session;

                // Simulate the edit-mode ON → OFF transition on the Player
                // entity (without actually entering edit mode). The user's
                // manual toggle is the only known way to unblock cornerstone
                // and door interactivity at login, and the single observable
                // effect of that toggle on the Player is:
                //   ON:  set UNIT_FLAG_PACIFIED / UNIT_FLAG2_NO_ACTIONS /
                //        silenced-school → push VALUES_UPDATE
                //   OFF: clear all three → push VALUES_UPDATE
                // Replicating the pair at plot-enter pushes the same flag
                // transition the client apparently needs to wire up housing
                // GO clicks.
                {
                    // "ON" push: set the edit-mode-ish flags and flush.
                    p->SetUnitFlag(UNIT_FLAG_PACIFIED);
                    p->SetUnitFlag2(UNIT_FLAG2_NO_ACTIONS);
                    p->ReplaceAllSilencedSchoolMask(SPELL_SCHOOL_MASK_ALL);
                    p->BuildUpdateChangesMask();
                    {
                        UpdateData onUpdate(p->GetMapId());
                        WorldPacket onPacket;
                        p->BuildValuesUpdateBlockForPlayer(&onUpdate, p);
                        if (onUpdate.HasData())
                        {
                            onUpdate.BuildPacket(&onPacket);
                            p->SendDirectMessage(&onPacket);
                        }
                        p->ClearUpdateMask(false);
                    }

                    // "OFF" push: put the three back to what her auras need and flush again. A silence, pacify or
                    // stun she brought onto the plot keeps working.
                    Housing::RestoreEditModeRestrictions(p);
                    p->BuildUpdateChangesMask();
                    {
                        UpdateData offUpdate(p->GetMapId());
                        WorldPacket offPacket;
                        p->BuildValuesUpdateBlockForPlayer(&offUpdate, p);
                        if (offUpdate.HasData())
                        {
                            offUpdate.BuildPacket(&offPacket);
                            p->SendDirectMessage(&offPacket);
                        }
                        p->ClearUpdateMask(false);
                    }
                }
            }

            // Post-tutorial + neighborhood-map-entry auras are now emitted
            // synchronously at the end of AddPlayerToMap (see below), immediately
            // after the initial UPDATE_OBJECT bundle flushes. They must not ride
            // on the 500 ms defer, which left a two-minute gap before the map and
            // its icons settled.
            // NOTE: SendPlotEnterSpellPackets is emitted by the plot AT's OnUnitEnter
            // hook (at_housing_plot.cpp) when the player physically overlaps the plot
            // AreaTrigger — the trigger retail uses, per the sniff (spells
            // "In Plot"/1239847 and "Visiting Neighbor"/469226 are plot-overlap, not
            // map-entry auras). The previous deferred emission here fired it on map
            // entry regardless of whether the player's spawn position was inside a
            // plot AT, which is wrong. Removed; AT hook remains the sole caller.

            // Diagnostic: print AT position vs player position for OutsidePlotBounds debugging
            float dist2d = p->GetExactDist2d(plotAt);
            float dist3d = p->GetExactDist(plotAt);
            bool inBox = p->IsWithinBox(*plotAt, 35.0f, 30.0f, 47.0f);  // half-extents from SQL ShapeData

            TC_LOG_DEBUG("housing", "HousingMap deferred ENTER_PLOT: player {} plot {} AT {}\n"
                "  AT pos: ({:.1f}, {:.1f}, {:.1f}, facing={:.3f})\n"
                "  Player pos: ({:.1f}, {:.1f}, {:.1f})\n"
                "  Dist2D={:.1f} Dist3D={:.1f} InBox={} HasPlayers={}",
                playerGuid.ToString(), deferredPlotIndex, plotAt->GetGUID().ToString(),
                plotAt->GetPositionX(), plotAt->GetPositionY(), plotAt->GetPositionZ(), plotAt->GetOrientation(),
                p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(),
                dist2d, dist3d, inBox,
                plotAt->HasAreaTriggerFlag(AreaTriggerFieldFlags::HasPlayers));
        }, Milliseconds(500));
    }

    // Retail sniff dump_12.0.1.66838_2026-04-15_09-35-59 emits the post-tutorial
    // aura trio and the 4-aura neighborhood-map-entry burst immediately after
    // the big UPDATE_OBJECT at map entry, not after a multi-second delay.
    // Emitting them synchronously here (instead of from the 500 ms deferred
    // ENTER_PLOT callback) removes a two-minute wait before the map settled and
    // fires the spell triples for visitors too (the deferred block was gated
    // on the player having a house).
    SendHousingPostTutorialAuras(player);
    SendNeighborhoodMapEntryAuras(player);

    // Send personalized per-plot WorldState values for this specific player.
    // The init world states (sent during Map::AddPlayerToMap) use map-global defaults
    // (STRANGER for occupied, NONE for unoccupied). This corrects them to SELF/FRIEND
    // based on the player's relationship to each plot owner.
    SendPerPlayerPlotWorldStates(player);

    // Comprehensive summary of all packets sent during map entry (for sniff comparison)
    TC_LOG_DEBUG("housing", "=== AddPlayerToMap COMPLETE for player {} ===\n"
        "  Map: {} InstanceType={} NeighborhoodId={}\n"
        "  HasHouse: {} PlotIndex: {}\n"
        "  Packets sent: CURRENT_HOUSE_INFO, 3xAURA+3xSTART+3xGO, "
        "deferred ENTER_PLOT (500ms), WorldState timer started, PerPlayerPlotWorldStates\n"
        "  Player pos: ({:.1f}, {:.1f}, {:.1f})",
        player->GetGUID().ToString(),
        GetId(), GetEntry()->InstanceType, _neighborhoodId,
        housing ? "yes" : "no",
        housing ? housing->GetPlotIndex() : 255,
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());

    return true;
}

void HousingMap::RemovePlayerFromMap(Player* player, bool remove)
{
    // Remove plot auras before removing housing data.
    if (Housing const* housing = player->GetHousingForNeighborhood(_neighborhood ? _neighborhood->GetGuid() : ObjectGuid::Empty))
    {
        // Remove all plot enter/presence auras (manual packets — spells not in DB2)
        SendPlotLeaveAuraRemoval(player);
    }

    // Plot tracking is cleared for EVERY player leaving, not just those who own a house
    // here. Otherwise a visitor's entry would outlive the visit and last as long as the
    // map instance - which, since housing maps never unload, means forever.
    ClearPlayerCurrentPlot(player->GetGUID());

    // Leaving the map, by a map change or a logout, ends her fixture edit and the exterior lock she holds. The edit's
    // aura ends by itself as she leaves the world.
    for (Housing const* ownedHousing : player->GetAllHousings())
    {
        if (Housing* housing = player->GetHousingByGuid(ownedHousing->GetHouseGuid()))
        {
            housing->ReleaseExteriorLock(player->GetGUID());
            if (housing->GetEditorMode() == HOUSING_EDITOR_MODE_EXTERIOR_CUSTOMIZATION)
                housing->SetEditorMode(HOUSING_EDITOR_MODE_NONE);
        }
    }

    RemovePlayerHousing(player);

    TC_LOG_DEBUG("housing", "HousingMap::RemovePlayerFromMap: Player {} leaving housing map {} instanceId {}",
        player->GetGUID().ToString(), GetId(), GetInstanceId());

    Map::RemovePlayerFromMap(player, remove);
}

void SendHousingPostTutorialAuras(Player* player)
{
    // Sniff-verified: After QUEST_HOUSING_TUTORIAL_COMPLETE turn-in, three "post-tutorial" auras
    // are applied at slots 8, 9, 50. These replace old tutorial-phase auras.
    // These persist for the rest of the session. Since they don't exist in DB2, we send
    // manual SMSG_AURA_UPDATE packets each time the player enters the housing map.
    // Slot 8: spell 1285428 (NoCaster, ActiveFlags=1)
    // Slot 9: spell 1285424 (NoCaster, ActiveFlags=1) — will be overwritten by plot enter aura
    // Slot 50: spell 1266699 (NoCaster|Scalable, ActiveFlags=1, Points=1) — overwritten by plot enter
    if (!player->GetQuestRewardStatus(QUEST_HOUSING_TUTORIAL_COMPLETE))
        return;

    // Spell 1285428 at slot 8
    {
        ObjectGuid castId = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), SPELL_HOUSING_TUTORIAL_DONE_1,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();

        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = 8;
        auraInfo.AuraData.emplace();
        auraInfo.AuraData->CastID = castId;
        auraInfo.AuraData->SpellID = SPELL_HOUSING_TUTORIAL_DONE_1;
        auraInfo.AuraData->Flags = AFLAG_SELF_CAST;
        auraInfo.AuraData->ActiveFlags = 1;
        auraInfo.AuraData->CastLevel = 36;
        auraInfo.AuraData->Applications = 0;
        auraUpdate.Auras.push_back(std::move(auraInfo));
        player->SendDirectMessage(auraUpdate.Write());

        WorldPackets::Spells::SpellStart spellStart;
        spellStart.Cast.CasterGUID = player->GetGUID();
        spellStart.Cast.CasterUnit = player->GetGUID();
        spellStart.Cast.CastID = castId;
        spellStart.Cast.SpellID = SPELL_HOUSING_TUTORIAL_DONE_1;
        spellStart.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4;  // 15
        spellStart.Cast.CastTime = 0;
        player->SendDirectMessage(spellStart.Write());

        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = player->GetGUID();
        spellGo.Cast.CasterUnit = player->GetGUID();
        spellGo.Cast.CastID = castId;
        spellGo.Cast.SpellID = SPELL_HOUSING_TUTORIAL_DONE_1;
        spellGo.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10;  // 781
        spellGo.Cast.CastFlagsEx = 16;
        spellGo.Cast.CastFlagsEx2 = 4;
        spellGo.Cast.CastTime = getMSTime();
        spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
        spellGo.Cast.HitTargets.push_back(player->GetGUID());
        spellGo.Cast.HitStatus.emplace_back(uint8(0));
        spellGo.LogData.Initialize(player);
        player->SendDirectMessage(spellGo.Write());
    }

    // Spell 1285424 at slot 9
    {
        ObjectGuid castId = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), SPELL_HOUSING_TUTORIAL_DONE_2,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();

        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = 9;
        auraInfo.AuraData.emplace();
        auraInfo.AuraData->CastID = castId;
        auraInfo.AuraData->SpellID = SPELL_HOUSING_TUTORIAL_DONE_2;
        auraInfo.AuraData->Flags = AFLAG_SELF_CAST;
        auraInfo.AuraData->ActiveFlags = 1;
        auraInfo.AuraData->CastLevel = 36;
        auraInfo.AuraData->Applications = 0;
        auraUpdate.Auras.push_back(std::move(auraInfo));
        player->SendDirectMessage(auraUpdate.Write());

        WorldPackets::Spells::SpellStart spellStart;
        spellStart.Cast.CasterGUID = player->GetGUID();
        spellStart.Cast.CasterUnit = player->GetGUID();
        spellStart.Cast.CastID = castId;
        spellStart.Cast.SpellID = SPELL_HOUSING_TUTORIAL_DONE_2;
        spellStart.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4;
        spellStart.Cast.CastTime = 0;
        player->SendDirectMessage(spellStart.Write());

        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = player->GetGUID();
        spellGo.Cast.CasterUnit = player->GetGUID();
        spellGo.Cast.CastID = castId;
        spellGo.Cast.SpellID = SPELL_HOUSING_TUTORIAL_DONE_2;
        spellGo.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10;
        spellGo.Cast.CastFlagsEx = 16;
        spellGo.Cast.CastFlagsEx2 = 4;
        spellGo.Cast.CastTime = getMSTime();
        spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
        spellGo.Cast.HitTargets.push_back(player->GetGUID());
        spellGo.Cast.HitStatus.emplace_back(uint8(0));
        spellGo.LogData.Initialize(player);
        player->SendDirectMessage(spellGo.Write());
    }

    // Spell 1266699 at slot 50 (same ID as SPELL_HOUSING_PLOT_ENTER_2, different slot + Points)
    {
        ObjectGuid castId = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), SPELL_HOUSING_TUTORIAL_DONE_3,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();

        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = 50;
        auraInfo.AuraData.emplace();
        auraInfo.AuraData->CastID = castId;
        auraInfo.AuraData->SpellID = SPELL_HOUSING_TUTORIAL_DONE_3;
        auraInfo.AuraData->Flags = AFLAG_SELF_CAST | AFLAG_SCALABLE;
        auraInfo.AuraData->ActiveFlags = 1;
        auraInfo.AuraData->CastLevel = 36;
        auraInfo.AuraData->Applications = 0;
        auraInfo.AuraData->Points.push_back(1.0f);
        auraUpdate.Auras.push_back(std::move(auraInfo));
        player->SendDirectMessage(auraUpdate.Write());

        WorldPackets::Spells::SpellStart spellStart;
        spellStart.Cast.CasterGUID = player->GetGUID();
        spellStart.Cast.CasterUnit = player->GetGUID();
        spellStart.Cast.CastID = castId;
        spellStart.Cast.SpellID = SPELL_HOUSING_TUTORIAL_DONE_3;
        spellStart.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4;
        spellStart.Cast.CastTime = 0;
        player->SendDirectMessage(spellStart.Write());

        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = player->GetGUID();
        spellGo.Cast.CasterUnit = player->GetGUID();
        spellGo.Cast.CastID = castId;
        spellGo.Cast.SpellID = SPELL_HOUSING_TUTORIAL_DONE_3;
        spellGo.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10;
        spellGo.Cast.CastFlagsEx = 16;
        spellGo.Cast.CastFlagsEx2 = 4;
        spellGo.Cast.CastTime = getMSTime();
        spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
        spellGo.Cast.HitTargets.push_back(player->GetGUID());
        spellGo.Cast.HitStatus.emplace_back(uint8(0));
        spellGo.LogData.Initialize(player);
        player->SendDirectMessage(spellGo.Write());
    }

    TC_LOG_DEBUG("housing", "SendPostTutorialAuras: Sent 3 post-tutorial aura sequences "
        "(1285428@s8, 1285424@s9, 1266699@s50) for player {}",
        player->GetGUID().ToString());
}

void HousingMap::SendNeighborhoodMapEntryAuras(Player* player)
{
    // Retail's map-entry aura burst — decoded from
    // dump_12.0.1.66838_2026-04-15_09-35-59.pkt idx 9988/9994/9997/10000
    // and cross-checked against dump_12.0.1.66838_2026-04-10_08-45-23.pkt
    // idx 15676/15682/15685/15688. Each entry is one AURA_UPDATE +
    // SPELL_START + SPELL_GO triple. All four fields (Slot, Flags,
    // ActiveFlags, Visual.SpellXSpellVisualID) are taken straight from the
    // sniff. CastLevel 84 on retail; we use player->getLevel() as a bonus
    // since the decoded value reflects whatever char captured the sniff.
    //
    // 431539 (Morning Star) and 1266699 (Sound Squisher) appeared in the
    // same retail burst but are character/ambient auras pre-existing before
    // map entry (first seen at idx 5762/5786, 127× + 8× before map entry).
    // Core TC aura re-sync on map change already handles those; we emit
    // only the four truly-new housing-specific auras here.

    if (!player)
        return;

    struct MapEntryAura
    {
        uint32 SpellID;
        uint16 Slot;
        uint16 Flags;
        uint32 ActiveFlags;
        uint32 VisualSpellXSpellVisualID;
    };
    constexpr std::array<MapEntryAura, 4> kAuras = {{
        { SPELL_HOUSING_MAP_ENTRY_FIXUP,    20,  AFLAG_SELF_CAST,                1, 0                                        },
        { SPELL_HOUSING_MAP_ENTRY_REACT,    22,  AFLAG_SELF_CAST,                1, 0                                        },
        { SPELL_HOUSING_MAP_ENTRY_ENDEAVOR, 53,  AFLAG_SELF_CAST,                1, 0                                        },
        { SPELL_HOUSING_MAP_ENTRY_NEIGHBOR, 121, uint16(AFLAG_SELF_CAST | AFLAG_POSITIVE), 3, VISUAL_HOUSING_MAP_ENTRY_NEIGHBOR },
    }};

    uint16 const castLevel = static_cast<uint16>(player->GetLevel());

    for (MapEntryAura const& a : kAuras)
    {
        ObjectGuid castId = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), a.SpellID,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();

        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = a.Slot;
        auraInfo.AuraData.emplace();
        auraInfo.AuraData->CastID = castId;
        auraInfo.AuraData->SpellID = a.SpellID;
        auraInfo.AuraData->Visual.SpellXSpellVisualID = a.VisualSpellXSpellVisualID;
        auraInfo.AuraData->Flags = a.Flags;
        auraInfo.AuraData->ActiveFlags = a.ActiveFlags;
        auraInfo.AuraData->CastLevel = castLevel;
        auraInfo.AuraData->Applications = 0;
        auraUpdate.Auras.push_back(std::move(auraInfo));
        player->SendDirectMessage(auraUpdate.Write());

        WorldPackets::Spells::SpellStart spellStart;
        spellStart.Cast.CasterGUID = player->GetGUID();
        spellStart.Cast.CasterUnit = player->GetGUID();
        spellStart.Cast.CastID = castId;
        spellStart.Cast.SpellID = a.SpellID;
        spellStart.Cast.Visual.SpellXSpellVisualID = a.VisualSpellXSpellVisualID;
        spellStart.Cast.CastFlags = CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_VISUAL_CHAIN;
        spellStart.Cast.CastTime = 0;
        player->SendDirectMessage(spellStart.Write());

        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = player->GetGUID();
        spellGo.Cast.CasterUnit = player->GetGUID();
        spellGo.Cast.CastID = castId;
        spellGo.Cast.SpellID = a.SpellID;
        spellGo.Cast.Visual.SpellXSpellVisualID = a.VisualSpellXSpellVisualID;
        spellGo.Cast.CastFlags = CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10 | CAST_FLAG_VISUAL_CHAIN;
        spellGo.Cast.CastFlagsEx = 16;
        spellGo.Cast.CastFlagsEx2 = 4;
        spellGo.Cast.CastTime = getMSTime();
        spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
        spellGo.Cast.HitTargets.push_back(player->GetGUID());
        spellGo.Cast.HitStatus.emplace_back(uint8(0));
        spellGo.LogData.Initialize(player);
        player->SendDirectMessage(spellGo.Write());
    }

    TC_LOG_DEBUG("housing", "SendNeighborhoodMapEntryAuras: Sent 4 map-entry aura triples "
        "(1272741@s20, 1263578@s22, 1276064@s53, 1227147@s121 vis={}) for player {}",
        VISUAL_HOUSING_MAP_ENTRY_NEIGHBOR, player->GetGUID().ToString());
}

void HousingMap::SendPlotEnterSpellPackets(Player* player, uint8 plotIndex)
{
    // PLOT AT OVERLAP event — NOT map-entry. Wowhead:
    //   1239847 = "[DNT] In Plot"           → applied when the player's unit
    //                                         is inside a plot's AreaTrigger box
    //   469226  = "[DNT] Visiting Neighbor" → applied when that plot is owned
    //                                         by someone else
    //   1266699 = "[DNT] Sound Squisher"    → audio mixer aura
    //
    // Invoked only from at_housing_plot.cpp's OnUnitEnter hook. The earlier
    // 500 ms-deferred invocation inside HousingMap::AddPlayerToMap was moved
    // out because it fired unconditionally on map entry regardless of whether
    // the player was actually inside a plot AT — the retail triggers are
    // strictly AT overlap, not map transition.
    //
    // Manual packets are required because these spell IDs don't exist in DB2
    // (CastSpell() fails silently for DNT spells).

    TC_LOG_DEBUG("housing", "SendPlotEnterSpellPackets: BEGIN for player {} plot {} map {}",
        player->GetGUID().ToString(), plotIndex, GetId());

    // 1. Spell 1239847 — plot enter tracking aura (slot 55)
    // Sniff-verified: retail sends to slot 55, ActiveFlags=1 (NOT slot 50 which is tutorial aura)
    {
        ObjectGuid castId = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), SPELL_HOUSING_PLOT_ENTER,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();

        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = 55;
        auraInfo.AuraData.emplace();
        auraInfo.AuraData->CastID = castId;
        auraInfo.AuraData->SpellID = SPELL_HOUSING_PLOT_ENTER;
        auraInfo.AuraData->Flags = AFLAG_SELF_CAST;
        auraInfo.AuraData->ActiveFlags = 1;
        auraInfo.AuraData->CastLevel = 36;
        auraInfo.AuraData->Applications = 0;
        auraUpdate.Auras.push_back(std::move(auraInfo));
        player->SendDirectMessage(auraUpdate.Write());

        WorldPackets::Spells::SpellStart spellStart;
        spellStart.Cast.CasterGUID = player->GetGUID();
        spellStart.Cast.CasterUnit = player->GetGUID();
        spellStart.Cast.CastID = castId;
        spellStart.Cast.SpellID = SPELL_HOUSING_PLOT_ENTER;
        spellStart.Cast.CastFlags = CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_VISUAL_CHAIN;  // 524302 = 0x8000E
        spellStart.Cast.CastTime = 0;
        player->SendDirectMessage(spellStart.Write());

        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = player->GetGUID();
        spellGo.Cast.CasterUnit = player->GetGUID();
        spellGo.Cast.CastID = castId;
        spellGo.Cast.SpellID = SPELL_HOUSING_PLOT_ENTER;
        spellGo.Cast.CastFlags = CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10 | CAST_FLAG_VISUAL_CHAIN;  // 525068 = 0x8030C
        spellGo.Cast.CastFlagsEx = 16;
        spellGo.Cast.CastFlagsEx2 = 4;
        spellGo.Cast.CastTime = getMSTime();
        spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
        spellGo.Cast.HitTargets.push_back(player->GetGUID());
        spellGo.Cast.HitStatus.emplace_back(uint8(0));
        spellGo.LogData.Initialize(player);
        player->SendDirectMessage(spellGo.Write());
    }

    // 2. Set HasPlayers flag (Flags=1024) on the plot AreaTrigger.
    // Sniff-verified: this UPDATE_OBJECT goes out between the first spell set (1239847)
    // and the second (469226). It tells the client that players are inside this AT.
    if (AreaTrigger* plotAt = GetPlotAreaTrigger(plotIndex))
    {
        plotAt->SetAreaTriggerFlag(AreaTriggerFieldFlags::HasPlayers);
        TC_LOG_DEBUG("housing", "SendPlotEnterSpellPackets: Set HasPlayers on AT {} for player {} plot {}",
            plotAt->GetGUID().ToString(), player->GetGUID().ToString(), plotIndex);
    }

    // 3. Spell 469226 — plot presence aura (slot 56)
    {
        ObjectGuid castId2 = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), SPELL_HOUSING_PLOT_PRESENCE,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();

        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = 56;
        auraInfo.AuraData.emplace();
        auraInfo.AuraData->CastID = castId2;
        auraInfo.AuraData->SpellID = SPELL_HOUSING_PLOT_PRESENCE;
        auraInfo.AuraData->Flags = AFLAG_SELF_CAST;
        auraInfo.AuraData->ActiveFlags = 1;
        auraInfo.AuraData->CastLevel = 36;
        auraInfo.AuraData->Applications = 0;
        auraUpdate.Auras.push_back(std::move(auraInfo));
        player->SendDirectMessage(auraUpdate.Write());

        WorldPackets::Spells::SpellStart spellStart;
        spellStart.Cast.CasterGUID = player->GetGUID();
        spellStart.Cast.CasterUnit = player->GetGUID();
        spellStart.Cast.CastID = castId2;
        spellStart.Cast.SpellID = SPELL_HOUSING_PLOT_PRESENCE;
        spellStart.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_24 | CAST_FLAG_UNKNOWN_30;  // 0x2080000B
        spellStart.Cast.CastFlagsEx = 0x2000200;
        spellStart.Cast.CastTime = 0;
        player->SendDirectMessage(spellStart.Write());

        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = player->GetGUID();
        spellGo.Cast.CasterUnit = player->GetGUID();
        spellGo.Cast.CastID = castId2;
        spellGo.Cast.SpellID = SPELL_HOUSING_PLOT_PRESENCE;
        spellGo.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10 | CAST_FLAG_UNKNOWN_24 | CAST_FLAG_UNKNOWN_30;  // 0x20800309
        spellGo.Cast.CastFlagsEx = 0x2000210;
        spellGo.Cast.CastFlagsEx2 = 4;
        spellGo.Cast.CastTime = getMSTime();
        spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
        spellGo.Cast.HitTargets.push_back(player->GetGUID());
        spellGo.Cast.HitStatus.emplace_back(uint8(0));
        spellGo.LogData.Initialize(player);
        player->SendDirectMessage(spellGo.Write());
    }

    // 4. Spell 1266699 — slot 9 replacement (preceded by slot 9 removal)
    // CastFlags: START=15, GO=781, GoEx=16, GoEx2=4
    {
        // Remove existing slot 9 aura
        WorldPackets::Spells::AuraUpdate auraRemove;
        auraRemove.UpdateAll = false;
        auraRemove.UnitGUID = player->GetGUID();
        WorldPackets::Spells::AuraInfo removeInfo;
        removeInfo.Slot = 9;
        auraRemove.Auras.push_back(std::move(removeInfo));
        player->SendDirectMessage(auraRemove.Write());

        ObjectGuid castId3 = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), SPELL_HOUSING_PLOT_ENTER_2,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        // Apply 1266699 at slot 9 (Flags=NoCaster|Scalable=9, PointsCount=1, Points[0]=1)
        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();
        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = 9;
        auraInfo.AuraData.emplace();
        auraInfo.AuraData->CastID = castId3;
        auraInfo.AuraData->SpellID = SPELL_HOUSING_PLOT_ENTER_2;
        auraInfo.AuraData->Flags = AFLAG_SELF_CAST | AFLAG_SCALABLE;
        auraInfo.AuraData->ActiveFlags = 1;
        auraInfo.AuraData->CastLevel = 36;
        auraInfo.AuraData->Applications = 0;
        auraInfo.AuraData->Points.push_back(1.0f);
        auraUpdate.Auras.push_back(std::move(auraInfo));
        player->SendDirectMessage(auraUpdate.Write());

        WorldPackets::Spells::SpellStart spellStart;
        spellStart.Cast.CasterGUID = player->GetGUID();
        spellStart.Cast.CasterUnit = player->GetGUID();
        spellStart.Cast.CastID = castId3;
        spellStart.Cast.SpellID = SPELL_HOUSING_PLOT_ENTER_2;
        spellStart.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4;  // 15
        spellStart.Cast.CastTime = 0;
        player->SendDirectMessage(spellStart.Write());

        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = player->GetGUID();
        spellGo.Cast.CasterUnit = player->GetGUID();
        spellGo.Cast.CastID = castId3;
        spellGo.Cast.SpellID = SPELL_HOUSING_PLOT_ENTER_2;
        spellGo.Cast.CastFlags = CAST_FLAG_PENDING | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10;  // 781
        spellGo.Cast.CastFlagsEx = 16;
        spellGo.Cast.CastFlagsEx2 = 4;
        spellGo.Cast.CastTime = getMSTime();
        spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
        spellGo.Cast.HitTargets.push_back(player->GetGUID());
        spellGo.Cast.HitStatus.emplace_back(uint8(0));
        spellGo.LogData.Initialize(player);
        player->SendDirectMessage(spellGo.Write());
    }

    TC_LOG_DEBUG("housing", "SendPlotEnterSpellPackets: END — sent 3 spell sequences "
        "(1239847@s50, 469226@s56, 1266699@s9) + AT HasPlayers flag for player {} plot {}",
        player->GetGUID().ToString(), plotIndex);
}

void HousingMap::SendPlotLeaveAuraRemoval(Player* player)
{
    // Remove all plot enter/presence auras (slots 50, 56, 9)
    // Send aura removal packets (empty AuraData = HasAura=False)
    for (uint8 slot : { uint8(50), uint8(56), uint8(9) })
    {
        WorldPackets::Spells::AuraUpdate auraUpdate;
        auraUpdate.UpdateAll = false;
        auraUpdate.UnitGUID = player->GetGUID();

        WorldPackets::Spells::AuraInfo auraInfo;
        auraInfo.Slot = slot;
        auraUpdate.Auras.push_back(std::move(auraInfo));

        player->SendDirectMessage(auraUpdate.Write());
    }
    TC_LOG_DEBUG("housing", "HousingMap::SendPlotLeaveAuraRemoval: Removed auras (slots 50, 56, 9) for player {}",
        player->GetGUID().ToString());
}

HousingPlotOwnerType HousingMap::GetPlotOwnerTypeForPlayer(Player const* player, uint8 plotIndex) const
{
    if (!_neighborhood || !player)
        return HOUSING_PLOT_OWNER_NONE;

    Neighborhood::PlotInfo const* plotInfo = _neighborhood->GetPlotInfo(plotIndex);
    if (!plotInfo || plotInfo->OwnerGuid.IsEmpty())
        return HOUSING_PLOT_OWNER_NONE;

    // Check if the player owns this plot
    if (plotInfo->OwnerGuid == player->GetGUID())
        return HOUSING_PLOT_OWNER_SELF;

    // Check if it's an alt on the same BNet account (same person, different character)
    if (!plotInfo->OwnerBnetGuid.IsEmpty() && player->GetSession())
    {
        if (plotInfo->OwnerBnetGuid == player->GetSession()->GetBattlenetAccountGUID())
            return HOUSING_PLOT_OWNER_SELF;
    }

    // Check character-level friendship
    if (PlayerSocial* social = player->GetSocial())
    {
        if (social->HasFriend(plotInfo->OwnerGuid))
            return HOUSING_PLOT_OWNER_FRIEND;
    }

    return HOUSING_PLOT_OWNER_STRANGER;
}

void HousingMap::SendPerPlayerPlotWorldStates(Player* player)
{
    // Nothing to send, as on retail: the per-plot occupancy worldstate is carried inside
    // SMSG_INIT_WORLD_STATES (set by SpawnPlotGameObjects via SetWorldStateValue),
    // and the regular-map icon + hover info come from the JamCliHouse[] array
    // in SMSG_HOUSING_SVCS_GET_HOUSE_FINDER_NEIGHBORHOOD_RESPONSE plus the
    // Housing/4 NeighborhoodMirrorData.Houses entries. No per-player
    // SMSG_UPDATE_WORLD_STATE spam — retail doesn't do it, and sending enum
    // values here was the regression that broke the icons.
    //
    // Retained as an extension hook for genuine per-player overrides.
    (void)player;
}

void HousingMap::AddPlayerHousing(Housing* housing)
{
    if (!housing || housing->GetHouseGuid().IsEmpty())
    {
        TC_LOG_ERROR("housing", "HousingMap::AddPlayerHousing: Attempted to add a house without a GUID on map {} instanceId {}",
            GetId(), GetInstanceId());
        return;
    }

    _playerHousings[housing->GetHouseGuid()] = housing;

    TC_LOG_DEBUG("housing", "HousingMap::AddPlayerHousing: Added house {} on map {} instanceId {} (total: {})",
        housing->GetHouseGuid().ToString(), GetId(), GetInstanceId(), static_cast<uint32>(_playerHousings.size()));
}

void HousingMap::RemovePlayerHousing(Player* player)
{
    for (auto itr = _playerHousings.begin(); itr != _playerHousings.end();)
    {
        if (itr->second->GetOwner() != player)
        {
            ++itr;
            continue;
        }

        // Another character of the same Battle.net account may still be here with the same house open.
        ObjectGuid houseGuid = itr->first;
        itr = _playerHousings.erase(itr);
        HandPlayerHousingToAnotherCharacter(houseGuid, player);

        TC_LOG_DEBUG("housing", "HousingMap::RemovePlayerHousing: Player {} left with house {} on map {} instanceId {} (remaining: {})",
            player->GetGUID().ToString(), houseGuid.ToString(), GetId(), GetInstanceId(), static_cast<uint32>(_playerHousings.size()));
        // _playerHousings may have been rehashed by the re-add above; start over.
        itr = _playerHousings.begin();
    }
}

void HousingMap::ForgetHousing(Housing const* housing)
{
    if (!housing)
        return;

    // Compare pointers only: the entry is found by the object that is about to go away, never by reading another one.
    auto itr = std::find_if(_playerHousings.begin(), _playerHousings.end(),
        [housing](std::pair<ObjectGuid const, Housing*> const& entry) { return entry.second == housing; });
    if (itr == _playerHousings.end())
        return;

    ObjectGuid houseGuid = itr->first;
    _playerHousings.erase(itr);
    HandPlayerHousingToAnotherCharacter(houseGuid, housing->GetOwner());

    TC_LOG_DEBUG("housing", "HousingMap::ForgetHousing: House {} dropped from map {} instanceId {} (remaining: {})",
        houseGuid.ToString(), GetId(), GetInstanceId(), static_cast<uint32>(_playerHousings.size()));
}

void HousingMap::DropHouse(ObjectGuid houseGuid)
{
    if (_playerHousings.erase(houseGuid))
        TC_LOG_DEBUG("housing", "HousingMap::DropHouse: House {} no longer listed on map {} instanceId {} (remaining: {})",
            houseGuid.ToString(), GetId(), GetInstanceId(), static_cast<uint32>(_playerHousings.size()));
}

void HousingMap::DespawnHouseFromPlot(Neighborhood const* neighborhood, uint8 plotIndex, ObjectGuid houseGuid)
{
    if (!neighborhood || plotIndex == INVALID_PLOT_INDEX)
        return;

    uint32 const worldMapId = sHousingMgr.GetWorldMapIdByNeighborhoodMapId(neighborhood->GetNeighborhoodMapID());
    HousingMap* housingMap = dynamic_cast<HousingMap*>(sMapMgr->FindMap(worldMapId, uint32(neighborhood->GetGuid().GetCounter())));
    if (!housingMap || housingMap->GetNeighborhood() != neighborhood)
        return;

    housingMap->DespawnAllDecorForPlot(plotIndex);
    housingMap->DespawnAllMeshObjectsForPlot(plotIndex);
    housingMap->DespawnRoomForPlot(plotIndex);
    housingMap->DespawnHouseForPlot(plotIndex);
    housingMap->SetPlotOwnershipState(plotIndex, false);
    housingMap->DropHouse(houseGuid);
}

void HousingMap::HandPlayerHousingToAnotherCharacter(ObjectGuid houseGuid, Player const* leaving)
{
    for (auto const& reference : GetPlayers())
    {
        Player* other = reference.GetSource();
        if (other == leaving)
            continue;

        if (Housing* otherHousing = other->GetHousingByGuid(houseGuid))
        {
            _playerHousings[houseGuid] = otherHousing;
            return;
        }
    }
}

// ============================================================
// House Structure GO Management
// ============================================================

bool HousingMap::SpawnHouseFromState(uint8 plotIndex, Housing const& housing)
{
    int32 const exteriorComponentID = static_cast<int32>(housing.GetCoreExteriorComponentID());
    int32 const houseExteriorWmoDataID = static_cast<int32>(housing.GetHouseType());
    if (!exteriorComponentID || !houseExteriorWmoDataID)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnHouseFromState: house {} on plot {} has no core component ({}) or no style ({}), so it "
            "is not built", housing.GetHouseGuid().ToString(), plotIndex, exteriorComponentID, houseExteriorWmoDataID);
        return false;
    }

    // The fixture choices carry the pieces on hooks, the front door among them (a new house gets its starter door in
    // Housing::Create), and the root choices say which structural pieces the house has.
    FixtureOverrideMap const fixtureOverrides = housing.GetFixtureOverrideMap();
    RootOverrideMap const rootOverrides = housing.GetRootComponentOverrides();
    Position const placement = housing.GetHousePosition();
    return SpawnHouseForPlot(plotIndex, housing.HasCustomPosition() ? &placement : nullptr,
        exteriorComponentID, houseExteriorWmoDataID,
        fixtureOverrides.empty() ? nullptr : &fixtureOverrides,
        rootOverrides.empty() ? nullptr : &rootOverrides);
}

bool HousingMap::SpawnHouseForPlot(uint8 plotIndex, Position const* customPos,
    int32 exteriorComponentID, int32 houseExteriorWmoDataID,
    FixtureOverrideMap const* fixtureOverrides /*= nullptr*/,
    RootOverrideMap const* rootOverrides /*= nullptr*/)
{
    if (!_neighborhood)
        return false;

    Neighborhood::PlotInfo const* plotInfo = _neighborhood->GetPlotInfo(plotIndex);
    if (!plotInfo || plotInfo->HouseGuid.IsEmpty())
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnHouseForPlot: plot {} of neighborhood '{}' holds no house",
            plotIndex, _neighborhood->GetName());
        return false;
    }

    Position anchorPos;
    QuaternionData anchorRot;
    if (!sHousingMgr.GetPlotRoomAnchor(_neighborhood->GetNeighborhoodMapID(), plotIndex, anchorPos, anchorRot))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnHouseForPlot: plot {} of neighborhood map {} has no \"Plot - Plot N\" row in "
            "GameObjects.db2 on map {}, so its house has no room to stand in",
            plotIndex, _neighborhood->GetNeighborhoodMapID(), GetId());
        return false;
    }

    // The house's placement is its exterior root's pose inside the room. No DB2 field is known that gives the pose
    // retail picks for a new house: after the purchase on plot 13 the root arrived at (-3.557434, 4.4353027, 0) turned
    // 1.774 radians (hbcd3 1310359), and none of plot 13's NeighborhoodPlot or GameObjects.db2 fields gives that. The
    // other root Entities in the captures (plots 1, 5, 31, 36, 44, 48, 53 and 54) stand where their owners put them.
    // Until a source is found, a house without a saved placement stands at the room's centre, turned with the room.
    Position rootLocalPos;
    QuaternionData rootLocalRot(0.0f, 0.0f, 0.0f, 1.0f);
    if (customPos)
    {
        if (HousingMgr::IsRootPlacementInRoom(*customPos))
        {
            rootLocalPos.Relocate(customPos->GetPositionX(), customPos->GetPositionY(), customPos->GetPositionZ());
            rootLocalRot = QuaternionData::fromEulerAnglesZYX(customPos->GetOrientation(), 0.0f, 0.0f);
        }
        else
            TC_LOG_ERROR("housing", "HousingMap::SpawnHouseForPlot: the saved placement ({:.2f}, {:.2f}, {:.2f}) of house {} lies outside "
                "the room of plot {}; the house stands at the default placement", customPos->GetPositionX(), customPos->GetPositionY(),
                customPos->GetPositionZ(), plotInfo->HouseGuid.ToString(), plotIndex);
    }

    LoadGrid(anchorPos.GetPositionX(), anchorPos.GetPositionY());

    // A house whose core piece is unknown gets nothing, not a room and a root with no pieces on them, which would
    // count as a standing house.
    ExteriorComponentEntry const* coreComp = sExteriorComponentStore.LookupEntry(uint32(exteriorComponentID));
    if (!coreComp || coreComp->ModelFileDataID <= 0 || coreComp->HouseExteriorWmoDataID <= 0)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnHouseForPlot: ExteriorComponent {} of house {} is missing or has no model, so the "
            "house on plot {} is not built", exteriorComponentID, plotInfo->HouseGuid.ToString(), plotIndex);
        return false;
    }

    // Retail's order after a purchase: the room (hbcd3 Number 13864), then the exterior root and the house (Number
    // 14127), then the door and the house's pieces (Number 14144).
    if (!SpawnRoomForPlot(plotIndex, anchorPos, anchorRot, plotInfo->HouseGuid))
        return false;

    Position rootWorldPos;
    QuaternionData rootWorldRot;
    HousingMgr::ComposeAttachment(anchorPos, anchorRot, rootLocalPos, rootLocalRot, rootWorldPos, rootWorldRot);
    ObjectGuid const rootGuid = SpawnExteriorRoot(plotIndex, rootLocalPos, rootLocalRot, rootWorldPos);
    if (rootGuid.IsEmpty())
        return false;

    SpawnPlotHouseEntity(plotIndex, *plotInfo, rootGuid, rootWorldPos);

    SpawnFullHouseMeshObjects(plotIndex, rootGuid, rootWorldPos, rootWorldRot, plotInfo->HouseGuid,
        exteriorComponentID, houseExteriorWmoDataID, fixtureOverrides, rootOverrides);

    TC_LOG_DEBUG("housing", "HousingMap::SpawnHouseForPlot: house {} on plot {}: room at ({:.2f}, {:.2f}, {:.2f}) facing {:.3f}, "
        "root at local ({:.2f}, {:.2f}, {:.2f}), world ({:.2f}, {:.2f}, {:.2f}) facing {:.3f}",
        plotInfo->HouseGuid.ToString(), plotIndex, anchorPos.GetPositionX(), anchorPos.GetPositionY(), anchorPos.GetPositionZ(),
        anchorPos.GetOrientation(), rootLocalPos.GetPositionX(), rootLocalPos.GetPositionY(), rootLocalPos.GetPositionZ(),
        rootWorldPos.GetPositionX(), rootWorldPos.GetPositionY(), rootWorldPos.GetPositionZ(), rootWorldPos.GetOrientation());
    return true;
}

bool HousingMap::IsHouseSpawned(uint8 plotIndex) const
{
    return _exteriorRootGuids.contains(plotIndex);
}

ObjectGuid HousingMap::GetExteriorRootGuid(uint8 plotIndex) const
{
    auto itr = _exteriorRootGuids.find(plotIndex);
    return itr != _exteriorRootGuids.end() ? itr->second : ObjectGuid::Empty;
}

bool HousingMap::MoveHouseRoot(uint8 plotIndex, Position const& placement)
{
    HousingRoomEntity* root = GetHousingRoomEntity(GetExteriorRootGuid(plotIndex));
    if (!root)
        return false;

    // The root's pose in the room is all the client needs; it places everything hanging on the root from it. In hled1
    // 645924 only the root's PositionLocalSpace and RotationLocalSpace change; retail also lists the three pieces and
    // the Entity the door rides in that update, with no changed fields.
    root->SetMirroredPosition(Position(placement.GetPositionX(), placement.GetPositionY(), placement.GetPositionZ()),
        QuaternionData::fromEulerAnglesZYX(placement.GetOrientation(), 0.0f, 0.0f), 1.0f, root->GetAttachParentGUID(),
        HOUSING_ATTACHMENT_FLAGS_PIECE);

    // The server's own positions follow, so reach and distance checks use where the house now stands. A small move
    // stays near the cell each object was added to, which is only where the grid looks for it.
    Position worldPos;
    QuaternionData worldRot;
    if (GetWorldPose(root->GetGUID(), worldPos, worldRot))
    {
        root->Relocate(worldPos);
        if (auto itr = _houseEntityGuids.find(plotIndex); itr != _houseEntityGuids.end())
            if (HousingRoomEntity* house = GetHousingRoomEntity(itr->second))
                house->Relocate(worldPos);
    }

    if (auto itr = _meshObjects.find(plotIndex); itr != _meshObjects.end())
        for (ObjectGuid const& guid : itr->second)
            if (MeshObject* mesh = GetMeshObject(guid))
                if (GetWorldPose(guid, worldPos, worldRot))
                    mesh->Relocate(worldPos);

    if (auto itr = _doorAttachPointGuids.find(plotIndex); itr != _doorAttachPointGuids.end())
    {
        if (HousingRoomEntity* attachPoint = GetHousingRoomEntity(itr->second))
        {
            if (GetWorldPose(attachPoint->GetGUID(), worldPos, worldRot))
            {
                attachPoint->Relocate(worldPos);
                // The door rides the Entity at position zero with no turn of its own.
                // Its stationary position is the one a character coming near later is sent (hbcd3 1310816).
                if (GameObject* door = GetHouseGameObject(plotIndex))
                {
                    door->RelocateStationaryPosition(worldPos);
                    GameObjectRelocation(door, worldPos.GetPositionX(), worldPos.GetPositionY(), worldPos.GetPositionZ(),
                        worldPos.GetOrientation());
                }
            }
        }
    }

    return true;
}

void HousingMap::HoldBack(ObjectGuid guid, bool isDoorObject)
{
    if (!_heldBackCreates || !_heldBackCreates->Viewer)
        return;

    _heldBackCreates->Viewer->m_clientGUIDs.insert(guid);
    (isDoorObject ? _heldBackCreates->DoorObjects : _heldBackCreates->Pieces).push_back(guid);
}

void HousingMap::ForgetHeldBack(ObjectGuid guid)
{
    if (!_heldBackCreates || !_heldBackCreates->Viewer)
        return;

    _heldBackCreates->Viewer->m_clientGUIDs.erase(guid);
    for (std::vector<ObjectGuid>* list : { &_heldBackCreates->Pieces, &_heldBackCreates->DoorObjects })
        list->erase(std::remove(list->begin(), list->end(), guid), list->end());
}

void HousingMap::SendHeldBackCreates(HeldBackCreates const& creates, std::vector<ObjectGuid> const& destroyed)
{
    Player* viewer = creates.Viewer;
    if (!viewer)
        return;

    // Builds an object's create for her when she can see it, and otherwise forgets that she was said to have it.
    auto buildCreate = [viewer](WorldObject* object, ObjectGuid guid, UpdateData& data)
    {
        if (object && object->IsInWorld() && viewer->CanSeeOrDetect(object, { .DistanceCheck = true }))
            object->BuildCreateUpdateBlockForPlayer(&data, viewer);
        else
            viewer->m_clientGUIDs.erase(guid);
    };

    // The destroys and the new pieces go out together (hled1 819008: the old door, entry and Entity destroyed, the new
    // entry created, in one update).
    UpdateData pieces(GetId());
    for (ObjectGuid const& guid : destroyed)
        if (viewer->m_clientGUIDs.erase(guid))
            pieces.AddDestroyObject(guid);

    for (ObjectGuid const& guid : creates.Pieces)
        buildCreate(GetMeshObject(guid), guid, pieces);

    if (pieces.HasData())
    {
        WorldPacket packet;
        pieces.BuildPacket(&packet);
        viewer->SendDirectMessage(&packet);
    }

    // The new door and its Entity follow in their own update, as retail's did (hled1 819290, after the create at
    // 819082). Retail's came about half a second later; this one goes at once.
    UpdateData door(GetId());
    for (ObjectGuid const& guid : creates.DoorObjects)
    {
        WorldObject* object = guid.IsGameObject() ? static_cast<WorldObject*>(GetGameObject(guid))
            : static_cast<WorldObject*>(GetHousingRoomEntity(guid));
        buildCreate(object, guid, door);
    }

    if (door.HasData())
    {
        WorldPacket packet;
        door.BuildPacket(&packet);
        viewer->SendDirectMessage(&packet);
    }
}

ObjectGuid HousingMap::SpawnExteriorRoot(uint8 plotIndex, Position const& localPos, QuaternionData const& localRot, Position const& worldPos)
{
    // hbcd3 1310328-1310363: one Entity with entry 0, attached to the room with the house's placement, attachment
    // flags 3, carrying Tag_HouseExteriorPiece and Tag_HouseExteriorRoot.
    ObjectGuid const rootGuid = HousingMgr::MakeExteriorRootGuid(GetId(), plotIndex);
    RemoveHousingEntityNow(rootGuid);
    _exteriorRootGuids.erase(plotIndex);

    HousingRoomEntity* root = new HousingRoomEntity(HousingGridEntityRole::ExteriorRoot);
    if (!root->Create(rootGuid, this, worldPos, false))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnExteriorRoot: the exterior root of plot {} could not be created", plotIndex);
        delete root;
        return ObjectGuid::Empty;
    }

    root->SetMirroredPosition(localPos, localRot, 1.0f, GetRoomIdentityGuid(plotIndex), HOUSING_ATTACHMENT_FLAGS_PIECE);
    PhasingHandler::InitDbPhaseShift(root->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);

    if (!AddToMap(root))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnExteriorRoot: the exterior root of plot {} could not be added to the map", plotIndex);
        delete root;
        return ObjectGuid::Empty;
    }

    _exteriorRootGuids[plotIndex] = rootGuid;
    return rootGuid;
}

void HousingMap::SpawnPlotHouseEntity(uint8 plotIndex, Neighborhood::PlotInfo const& plot, ObjectGuid rootGuid, Position const& worldPos)
{
    // hbcd3 1310364-1310395: the house entity has no position; its EntityGUID names the exterior root. The captures
    // hold house entities of eight Battle.net accounts, sent to a player as she comes near their houses.
    RemoveHousingEntityNow(plot.HouseGuid);
    _houseEntityGuids.erase(plotIndex);

    HousingRoomEntity* house = new HousingRoomEntity(HousingGridEntityRole::House);
    if (!house->Create(plot.HouseGuid, this, worldPos, false))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnPlotHouseEntity: the house entity of plot {} could not be created", plotIndex);
        delete house;
        return;
    }

    uint32 const level = std::max<uint32>(1, plot.HouseLevel);
    house->SetHouseData(plot.OwnerBnetGuid, plot.OwnerGuid, int32(plotIndex), level, plot.HouseFavor,
        sHousingMgr.GetInteriorDecorBudgetForLevel(level), sHousingMgr.GetExteriorDecorBudgetForLevel(level),
        sHousingMgr.GetFixtureBudgetForLevel(level), sHousingMgr.GetRoomBudgetForLevel(level), rootGuid);
    PhasingHandler::InitDbPhaseShift(house->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);

    if (!AddToMap(house))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnPlotHouseEntity: the house entity of plot {} could not be added to the map", plotIndex);
        delete house;
        return;
    }

    _houseEntityGuids[plotIndex] = plot.HouseGuid;
}

void HousingMap::RemoveHousingEntityNow(ObjectGuid guid)
{
    if (guid.IsEmpty())
        return;

    if (HousingRoomEntity* entity = GetHousingRoomEntity(guid))
        RemoveFromMap(entity, true);
}

bool HousingMap::GetWorldPose(ObjectGuid guid, Position& position, QuaternionData& rotation, uint32 depth /*= 0*/)
{
    // A house is a short chain (room, root, piece, piece on a hook, entry offset); a longer one is a loop.
    if (depth > 16 || guid.IsEmpty())
        return false;

    ObjectGuid parent;
    Position localPos;
    QuaternionData localRot;
    if (MeshObject* mesh = GetMeshObject(guid))
    {
        parent = mesh->GetAttachParentGUID();
        localPos = mesh->GetLocalPosition();
        localRot = mesh->GetLocalRotation();
        if (parent.IsEmpty())
        {
            position = mesh->GetPosition();
            rotation = localRot;
            return true;
        }
    }
    else if (HousingRoomEntity* entity = GetHousingRoomEntity(guid))
    {
        parent = entity->GetAttachParentGUID();
        localPos = entity->GetLocalPosition();
        localRot = entity->GetLocalRotation();
        // A room hangs on nothing: its mirrored position is its place in the world.
        if (parent.IsEmpty())
        {
            position = entity->GetPosition();
            rotation = localRot;
            return true;
        }
    }
    else
        return false;

    Position parentPos;
    QuaternionData parentRot;
    if (!GetWorldPose(parent, parentPos, parentRot, depth + 1))
        return false;

    HousingMgr::ComposeAttachment(parentPos, parentRot, localPos, localRot, position, rotation);
    return true;
}

bool HousingMap::SpawnRoomForPlot(uint8 plotIndex, Position const& anchorPos,
    QuaternionData const& anchorRot, ObjectGuid houseGuid)
{
    // The retail client requires a Room entity with an attached MeshObject that has a Geobox
    // (axis-aligned bounding box) to define the plot's placement boundary. Without these,
    // the client reports "OutsidePlotBounds" for ALL decor placement attempts.
    //
    // Sniff-verified structure (12.0.1 retail):
    // 1. Room entity:      FHousingRoom_C fragment with HouseGUID, HouseRoomID=18, Flags=1
    // 2. Room MeshObject:  FHousingRoomComponentMesh_C + Geobox attached to room at local (0,0,0)
    //    Geobox bounds are identical for ALL factions/themes: (-35,-30,-1.01)→(35,30,125.01)
    //
    // In retail, the Room entity is a lightweight entity (type 18) without CGObject/FMeshObjectData_C.
    // Our implementation uses MeshObjects for both, adding room data as extra entity fragments.
    // The client processes fragments independently, so the extra fragments are harmless.

    int32 HOUSE_ROOM_ID = static_cast<int32>(sHousingMgr.GetBaseRoomEntryId());
    static constexpr int32 ROOM_FLAGS    = 1;     // BASE_ROOM
    static constexpr int32 ROOM_COMPONENT_ID = 196; // Sniff-verified: same for alliance+horde

    // Clean up any existing room entities for this plot
    DespawnRoomForPlot(plotIndex);

    // --- DB2 lookups for room component data ---

    // 1. HouseRoom → RoomWmoDataID
    HouseRoomEntry const* houseRoomEntry = sHouseRoomStore.LookupEntry(HOUSE_ROOM_ID);
    int32 roomWmoDataID = houseRoomEntry ? houseRoomEntry->RoomWmoDataID : 0;

    // 2. RoomWmoData → Geobox bounds (bounding box for OutsidePlotBounds check)
    //    Sniff fallback: (-35,-30,-1.01)→(35,30,125.01)
    float geoMinX = -35.0f, geoMinY = -30.0f, geoMinZ = -1.01f;
    float geoMaxX =  35.0f, geoMaxY =  30.0f, geoMaxZ = 125.01f;
    RoomWmoDataEntry const* wmoData = roomWmoDataID ? sRoomWmoDataStore.LookupEntry(roomWmoDataID) : nullptr;
    if (wmoData)
    {
        geoMinX = wmoData->BoundingBoxMinX;
        geoMinY = wmoData->BoundingBoxMinY;
        geoMinZ = wmoData->BoundingBoxMinZ;
        geoMaxX = wmoData->BoundingBoxMaxX;
        geoMaxY = wmoData->BoundingBoxMaxY;
        geoMaxZ = wmoData->BoundingBoxMaxZ;
    }
    else
    {
        TC_LOG_WARN("housing", "HousingMap::SpawnRoomForPlot: No RoomWmoData for roomWmoDataID={} "
            "(plot {}), using fallback geobox (-35,-30,-1.01)->(35,30,125.01)",
            roomWmoDataID, plotIndex);
    }

    // 3. RoomComponent → ModelFileDataID, Type
    //    Sniff fallback: FileDataID=6322976, Type=2
    int32 fileDataID = 6322976;
    uint8 roomComponentType = 2;
    RoomComponentEntry const* compEntry = sRoomComponentStore.LookupEntry(ROOM_COMPONENT_ID);
    if (compEntry)
    {
        if (compEntry->ModelFileDataID > 0)
            fileDataID = compEntry->ModelFileDataID;
        roomComponentType = compEntry->Type;
    }

    // 4. RoomComponentOption → theme-specific cosmetic data (varies by faction/house style)
    //    Alliance sniff: optionID=874, themeID=1 (Folk), field24=0, textureID=3
    //    Horde sniff:    optionID=420, themeID=2 (Rugged), field24=0, textureID=40
    //    These are cosmetic only (don't affect Geobox/bounds check).
    int32 roomComponentOptionID = 0;
    int32 houseThemeID = 0;
    int32 roomComponentTextureID = 0;
    int32 field24 = 0;

    // Use faction-aware theme lookup via MeshStyleFilterID
    int32 factionThemeID = _neighborhood
        ? sHousingMgr.GetFactionDefaultThemeID(_neighborhood->GetFactionRestriction())
        : 1; // Folk (Alliance default)
    int32 compMeshStyleFilterID = compEntry ? compEntry->MeshStyleFilterID : 48;
    RoomComponentOptionEntry const* optEntry = sHousingMgr.FindRoomComponentOption(compMeshStyleFilterID, factionThemeID);
    if (optEntry)
    {
        roomComponentOptionID = static_cast<int32>(optEntry->ID);
        houseThemeID = optEntry->HouseThemeID;
        field24 = static_cast<int32>(optEntry->SubType);
    }
    // If no DB2 entry found, use sniff-verified alliance defaults
    if (roomComponentOptionID == 0)
    {
        roomComponentOptionID = 874;
        houseThemeID = 1;
        field24 = 0;
        roomComponentTextureID = 3;
    }

    TC_LOG_DEBUG("housing", "HousingMap::SpawnRoomForPlot: plot={} DB2 lookup: "
        "roomWmoDataID={} geobox=({:.2f},{:.2f},{:.2f})→({:.2f},{:.2f},{:.2f}) "
        "fileDataID={} compType={} optionID={} themeID={} field24={} textureID={}",
        plotIndex, roomWmoDataID,
        geoMinX, geoMinY, geoMinZ, geoMaxX, geoMaxY, geoMaxZ,
        fileDataID, roomComponentType,
        roomComponentOptionID, houseThemeID, field24, roomComponentTextureID);

    // --- Create the entities as retail does ---
    //
    // Retail, packet idx 9984 of dump_12.0.1.66838_2026-04-15_09-35-59:
    //   - Housing/2 identity entity (HighGuid::Housing subType=2, objType=18)
    //     fragments: [FHousingRoom_C, FMirroredPositionData_C, Tag_HousingRoom]
    //     carries HouseGUID, HouseRoomID=18, Flags, FloorIndex, Doors, MeshObjects
    //   - Component MeshObject (HighGuid::MeshObject, objType=14)
    //     fragments: [CGObject, FMeshObjectData_C, FHousingRoomComponentMesh_C,
    //                 FMirroredPositionData_C, Tag_MeshObject]
    //     MovementUpdate.TransportGuid = Housing/2 identity GUID
    //     carries the Geobox (used by client for OutsidePlotBounds check)
    //
    // Two MeshObjects (a room MeshObject with FHousingRoom_C AND a componentMesh with
    // FHousingRoomComponentMesh_C) would put two entities at the plot position both
    // claiming HouseRoomID=18. The client's room registry accepts only the first and
    // drops the other's Geobox chain, which breaks OutsidePlotBounds and ownership.

    // 1. Housing/2 identity entity — the single authoritative room for this plot. Its GUID's counter is the plot
    // index: plot 13's room is 0xDC40000000000012 / 0x0D (hbcd3 1299598).
    ObjectGuid roomIdentityGuid = ObjectGuid::Create<HighGuid::Housing>(
        /*subType*/ 2,
        /*arg1*/ 0,
        /*arg2*/ static_cast<uint32>(HOUSE_ROOM_ID),
        /*counter*/ static_cast<ObjectGuid::LowType>(plotIndex));

    HousingRoomEntity* roomIdentity = new HousingRoomEntity();
    PhasingHandler::InitDbPhaseShift(roomIdentity->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);
    if (!roomIdentity->Create(roomIdentityGuid, this, anchorPos))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnRoomForPlot: Failed to create Housing/2 identity room for plot {}", plotIndex);
        delete roomIdentity;
        return false;
    }

    roomIdentity->SetHouseGUID(houseGuid);
    roomIdentity->SetHouseRoomID(HOUSE_ROOM_ID);
    roomIdentity->SetFlags(ROOM_FLAGS);
    roomIdentity->SetFloorIndex(0);

    // Important (a byte-for-byte comparison with retail's packet idx 9984):
    // When AttachParent is Empty, retail sets PositionLocalSpace = the room's
    // WORLD position (not zero) and RotationLocalSpace = the house's facing
    // quaternion. The client uses this as the root of the attach chain — any
    // child entity (decor, mirrors) resolves its world pos as
    //   worldPos = room.PositionLocalSpace + rotate(child.localPos, room.rot)
    // If we set room.PositionLocalSpace=(0,0,0), every attached decor ends up
    // near world origin (invisible) and the client's OutsidePlotBounds check
    // fails for every placement because the player's world position is far
    // from the "plot center" the client derives from the room chain.
    roomIdentity->SetMirroredPosition(anchorPos, anchorRot,
        /*scale*/ 1.0f, ObjectGuid::Empty, /*attachFlags*/ 3);

    // 1b. Doors on the identity entity (not on the component mesh).
    // Client's HousingRoomSystem DoorConnection handler reads this array.
    if (std::vector<RoomDoorInfo> const* doors = sHousingMgr.GetRoomDoors(roomWmoDataID))
    {
        for (RoomDoorInfo const& door : *doors)
        {
            Position doorOffset(door.OffsetPos[0], door.OffsetPos[1], door.OffsetPos[2], 0.0f);
            roomIdentity->AddDoor(static_cast<int32>(door.RoomComponentID), doorOffset,
                /*connectionType*/ static_cast<uint8>(7) /*HOUSING_ROOM_COMPONENT_DOORWAY*/,
                /*attachedRoomGuid*/ ObjectGuid::Empty);
        }
        TC_LOG_DEBUG("housing", "HousingMap::SpawnRoomForPlot: plot={} added {} door entries to Housing/2 identity from roomWmoDataID={}",
            plotIndex, uint32(doors->size()), roomWmoDataID);
    }
    else
    {
        TC_LOG_WARN("housing", "HousingMap::SpawnRoomForPlot: plot={} no door data for roomWmoDataID={} — fixture hookpoints will show 'None'",
            plotIndex, roomWmoDataID);
    }

    // 2. Component MeshObject — attaches to Housing/2 identity. Carries the
    // Geobox via FHousingRoomComponentMesh_C. Does NOT carry FHousingRoom_C
    // (that's the identity's job). Retail-verified: component mesh fragment
    // list is [CGObject, FMeshObjectData_C, FHousingRoomComponentMesh_C,
    // FMirroredPositionData_C, Tag_MeshObject] — no FHousingRoom_C.
    Position componentPos(0.0f, 0.0f, 0.0f, 0.0f);
    QuaternionData componentRot;
    componentRot.x = 0.0f;
    componentRot.y = 0.0f;
    componentRot.z = 0.0f;
    componentRot.w = 1.0f;

    MeshObject* componentMesh = MeshObject::CreateMeshObject(this, componentPos, componentRot, 1.0f,
        fileDataID, /*isWMO*/ true,
        /*attachParent*/ roomIdentityGuid, /*attachFlags*/ 3, &anchorPos);

    if (!componentMesh)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnRoomForPlot: Failed to create room component mesh for plot {}", plotIndex);
        RemoveFromMap(roomIdentity, true);
        return false;
    }

    PhasingHandler::InitDbPhaseShift(componentMesh->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);
    componentMesh->InitHousingRoomComponentData(roomIdentityGuid,
        roomComponentOptionID, ROOM_COMPONENT_ID,
        roomComponentType, field24, /*field20*/ 0,
        houseThemeID, roomComponentTextureID,
        /*roomComponentTypeParam*/ 0,
        geoMinX, geoMinY, geoMinZ,
        geoMaxX, geoMaxY, geoMaxZ);

    // 3. Link: add component GUID to identity's MeshObjects array.
    roomIdentity->AddMeshObject(componentMesh->GetGUID());

    // 4. Add component to map. Identity was already AddToMap'd via its Create().
    if (!AddToMap(componentMesh))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnRoomForPlot: Failed to add room component mesh to map for plot {}", plotIndex);
        RemoveFromMap(roomIdentity, true);
        delete componentMesh;
        return false;
    }

    _roomIdentityGuids[plotIndex] = roomIdentityGuid;
    _roomComponentMeshes[plotIndex] = componentMesh->GetGUID();
    // There is no separate room MeshObject; _roomEntities holds the same identity GUID
    // for callers that read it. New code should read _roomIdentityGuids.
    _roomEntities[plotIndex] = roomIdentityGuid;

    TC_LOG_DEBUG("housing", "HousingMap::SpawnRoomForPlot: plot={} identity={} component={} "
        "at ({:.1f},{:.1f},{:.1f}) geobox=({:.2f},{:.2f},{:.2f})->({:.2f},{:.2f},{:.2f})",
        plotIndex, roomIdentityGuid.ToString(), componentMesh->GetGUID().ToString(),
        anchorPos.GetPositionX(), anchorPos.GetPositionY(), anchorPos.GetPositionZ(),
        geoMinX, geoMinY, geoMinZ, geoMaxX, geoMaxY, geoMaxZ);
    return true;
}

void HousingMap::DespawnRoomForPlot(uint8 plotIndex)
{
    // Component mesh first (it attaches to the identity).
    auto compItr = _roomComponentMeshes.find(plotIndex);
    if (compItr != _roomComponentMeshes.end())
    {
        if (MeshObject* mesh = GetMeshObject(compItr->second))
            mesh->AddObjectToRemoveList();
        _roomComponentMeshes.erase(compItr);
    }

    // Housing/2 identity entity. Its GUID is fixed by the plot and a rebuilt house makes it again straight away, so
    // it leaves the map at once rather than at the end of the update.
    auto identItr = _roomIdentityGuids.find(plotIndex);
    if (identItr != _roomIdentityGuids.end())
    {
        RemoveHousingEntityNow(identItr->second);
        _roomIdentityGuids.erase(identItr);
    }

    // _roomEntities points at the same identity GUID, which was removed above; just clear it.
    _roomEntities.erase(plotIndex);
}

void HousingMap::SpawnFullHouseMeshObjects(uint8 plotIndex, ObjectGuid rootGuid, Position const& rootWorldPos,
    QuaternionData const& rootWorldRot, ObjectGuid houseGuid,
    int32 exteriorComponentID, int32 houseExteriorWmoDataID,
    FixtureOverrideMap const* fixtureOverrides /*= nullptr*/,
    RootOverrideMap const* rootOverrides /*= nullptr*/)
{
    // The house is built from the ExteriorComponent tree. Its structural pieces (Base type 9, Roof type 10) all hang on
    // the exterior root at local zero, and each piece's hooks carry the pieces the owner chose for them. Retail's
    // starter Horde house is the wall 1004 and the roof 3811 on the root and the entry 976 on the wall's hook 17262
    // (hbcd3 1310863-1311080).
    //
    // Root selection per type:
    //   1. Check rootOverrides (player's explicit choice for that type)
    //   2. Use coreExtCompID for the core type
    //   3. Fall back to DB2 default (Flags & 0x1 IsDefault)

    uint32 coreExtCompID = static_cast<uint32>(exteriorComponentID);
    ExteriorComponentEntry const* coreComp = sExteriorComponentStore.LookupEntry(coreExtCompID);
    if (!coreComp || coreComp->ModelFileDataID <= 0 || coreComp->HouseExteriorWmoDataID <= 0)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnFullHouseMeshObjects: ExteriorComponent {} is missing or has no model, so the house "
            "on plot {} has no pieces", exteriorComponentID, plotIndex);
        return;
    }

    uint32 wmoDataID = coreComp->HouseExteriorWmoDataID;
    auto const* rootComps = sHousingMgr.GetRootComponentsForWmoData(wmoDataID);
    uint32 totalSpawned = 0;

    if (rootComps)
    {
        // Group roots by type, filtering to match the house's Size
        uint8 houseSize = coreComp->Size;
        std::unordered_map<uint8 /*type*/, std::vector<uint32>> rootsByType;
        for (uint32 rootID : *rootComps)
        {
            ExteriorComponentEntry const* rc = sExteriorComponentStore.LookupEntry(rootID);
            if (rc && rc->ModelFileDataID > 0 && rc->Size == houseSize)
                rootsByType[rc->Type].push_back(rootID);
        }

        for (auto const& [type, compIDs] : rootsByType)
        {
            uint32 selectedCompID = 0;

            // 1. Check player's root overrides for this type
            if (rootOverrides)
            {
                auto ovrItr = rootOverrides->find(type);
                if (ovrItr != rootOverrides->end())
                    selectedCompID = ovrItr->second;
                else
                {
                    // Type not in fixtures DB → not unlocked yet, skip it
                    TC_LOG_DEBUG("housing", "SpawnFullHouseMeshObjects: Skipping root type={} — not in fixtures",
                        type);
                    continue;
                }
            }

            // 2. For the core type, use the player's selected coreExtCompID
            if (!selectedCompID && type == coreComp->Type)
                selectedCompID = coreExtCompID;

            // 3. Fall back to DB2 default for this type + wmoDataID
            if (!selectedCompID)
            {
                uint32 defaultID = sHousingMgr.GetDefaultFixtureForType(type, wmoDataID);
                if (defaultID)
                    selectedCompID = defaultID;
            }

            // 4. Last resort: first available in the list
            if (!selectedCompID && !compIDs.empty())
                selectedCompID = compIDs[0];

            if (selectedCompID)
                totalSpawned += SpawnExtCompTree(plotIndex, selectedCompID, Position(), QuaternionData(0.0f, 0.0f, 0.0f, 1.0f),
                    houseGuid, houseExteriorWmoDataID, rootGuid, rootWorldPos, rootWorldRot, 0, fixtureOverrides);
        }
    }

    if (!totalSpawned)
        TC_LOG_ERROR("housing", "HousingMap::SpawnFullHouseMeshObjects: no piece of HouseExteriorWmoData {} (core component {}) could "
            "be built for plot {}", wmoDataID, coreExtCompID, plotIndex);
    else
        TC_LOG_DEBUG("housing", "HousingMap::SpawnFullHouseMeshObjects: plot {} HouseExteriorWmoData {} core component {}: {} pieces",
            plotIndex, wmoDataID, coreExtCompID, totalSpawned);
}

uint32 HousingMap::SpawnExtCompTree(uint8 plotIndex, uint32 extCompID,
    Position const& localPos, QuaternionData const& localRot,
    ObjectGuid houseGuid, int32 houseExteriorWmoDataID,
    ObjectGuid parentGuid, Position const& parentWorldPos, QuaternionData const& parentWorldRot,
    int32 depth /*= 0*/, FixtureOverrideMap const* fixtureOverrides /*= nullptr*/, int32 hookID /*= -1*/)
{
    if (depth > 10) // safety limit against infinite recursion
        return 0;

    ExteriorComponentEntry const* comp = sExteriorComponentStore.LookupEntry(extCompID);
    if (!comp)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnExtCompTree: ExteriorComponent {} not found", extCompID);
        return 0;
    }

    if (comp->ModelFileDataID <= 0)
    {
        TC_LOG_WARN("housing", "HousingMap::SpawnExtCompTree: ExteriorComponent {} has no ModelFileDataID", extCompID);
        return 0;
    }

    // Where the piece stands in the world: the server places it on the grid there, and its children and door are
    // placed from it. The client places it from its attachment.
    Position worldPos;
    QuaternionData worldRot;
    HousingMgr::ComposeAttachment(parentWorldPos, parentWorldRot, localPos, localRot, worldPos, worldRot);
    LoadGrid(worldPos.GetPositionX(), worldPos.GetPositionY());

    MeshObject* mesh = MeshObject::CreateMeshObject(this, localPos, localRot, 1.0f, comp->ModelFileDataID, /*isWMO*/ true,
        parentGuid, HOUSING_ATTACHMENT_FLAGS_PIECE, &worldPos);
    if (!mesh)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnExtCompTree: Failed to spawn mesh for comp {} "
            "(ModelFileDataID={}) at depth {}",
            extCompID, comp->ModelFileDataID, depth);
        return 0;
    }

    // A door piece carries its door: the Entity at the piece's EntryOffset and the door game object riding it. They
    // are made before the piece's fixture data, so the piece names the real door (hbcd3 1310976-1310985).
    HousingRoomEntity* doorAttachPoint = nullptr;
    GameObject* door = nullptr;
    if (comp->Type == HOUSING_FIXTURE_TYPE_DOOR && comp->GameObjectID > 0)
        door = CreateHouseDoor(plotIndex, mesh->GetGUID(), *comp, worldPos, worldRot, houseGuid, doorAttachPoint);

    // Size and Field_59 come from the component: retail sent Size 2 and Field_59 2 on the wall 1004 and Size 1,
    // Field_59 2 on the entry 976 (hbcd3 1310914-1310985), the current client's ExteriorComponent rows' Size and
    // Field_7. What Field_59 means is not known.
    mesh->InitHousingFixtureData(houseGuid, parentGuid, static_cast<int32>(extCompID), houseExteriorWmoDataID,
        comp->Type, comp->Field_7, comp->Size, hookID, door ? door->GetGUID() : ObjectGuid::Empty,
        comp->Type == HOUSING_FIXTURE_TYPE_BASE && hookID < 0);

    HoldBack(mesh->GetGUID(), false);
    if (!AddToMap(mesh))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnExtCompTree: AddToMap failed for plot {} component {}", plotIndex, extCompID);
        ForgetHeldBack(mesh->GetGUID());
        delete mesh;
        delete door;
        delete doorAttachPoint;
        return 0;
    }

    _meshObjects[plotIndex].push_back(mesh->GetGUID());

    if (door)
        AddHouseDoor(plotIndex, door, doorAttachPoint);

    uint32 count = 1;
    ObjectGuid meshGuid = mesh->GetGUID();

    // Spawn child components at the hooks the owner filled.
    if (auto const* hooks = sHousingMgr.GetHooksOnComponent(extCompID))
    {
        for (ExteriorComponentHookEntry const* hook : *hooks)
        {
            if (!hook || !fixtureOverrides)
                continue;

            auto overrideItr = fixtureOverrides->find(hook->ID);
            if (overrideItr == fixtureOverrides->end())
                continue;

            ExteriorComponentEntry const* childComp = sExteriorComponentStore.LookupEntry(overrideItr->second);
            if (!childComp)
                continue;

            TC_LOG_DEBUG("housing", "SpawnExtCompTree: parent={} hook={} (type={}) → child comp {} (ModelFDID={})",
                extCompID, hook->ID, hook->ExteriorComponentTypeID, childComp->ID, childComp->ModelFileDataID);

            // The hook's position and turn are the child's pose on this piece.
            Position hookPos(hook->Position[0], hook->Position[1], hook->Position[2]);
            count += SpawnExtCompTree(plotIndex, childComp->ID, hookPos, HousingMgr::GetHookRotation(*hook),
                houseGuid, houseExteriorWmoDataID, meshGuid, worldPos, worldRot, depth + 1, fixtureOverrides,
                static_cast<int32>(hook->ID));
        }
    }

    // NOTE: ExteriorComponent.ParentComponentID links are color/dye variants of the same shape,
    // NOT structural children. They share the same mesh with a different dye (Field_011).
    // These are NOT spawned as additional meshes — the player's fixture selection determines
    // which variant is used, handled via GetRootComponentOverrides() / fixture overrides.

    return count;
}

GameObject* HousingMap::CreateHouseDoor(uint8 plotIndex, ObjectGuid entryGuid, ExteriorComponentEntry const& entry,
    Position const& entryWorldPos, QuaternionData const& entryWorldRot, ObjectGuid houseGuid, HousingRoomEntity*& attachPoint)
{
    attachPoint = nullptr;

    uint32 const doorEntry = static_cast<uint32>(entry.GameObjectID);
    if (!sObjectMgr->GetGameObjectTemplate(doorEntry))
    {
        TC_LOG_ERROR("housing", "HousingMap::CreateHouseDoor: door {} of component {} has no gameobject template; plot {} gets no door",
            doorEntry, entry.ID, plotIndex);
        return nullptr;
    }

    // One door per house.
    DespawnDoorGO(plotIndex);

    // The Entity the door rides stands at the entry's EntryOffset, attachment flags 3 (hbcd3 1310987-1311020: 976's
    // (-4.4534, 0.3277, 2.4228)); the door's stationary position is the whole chain composed (hbcd3 1310816).
    Position const offsetLocalPos(entry.Position[0], entry.Position[1], entry.Position[2]);
    QuaternionData const identity(0.0f, 0.0f, 0.0f, 1.0f);
    Position doorWorldPos;
    QuaternionData doorWorldRot;
    HousingMgr::ComposeAttachment(entryWorldPos, entryWorldRot, offsetLocalPos, identity, doorWorldPos, doorWorldRot);

    attachPoint = new HousingRoomEntity(HousingGridEntityRole::AttachPoint);
    if (!attachPoint->Create(ObjectGuid::Create<HighGuid::Entity>(GetId(), 0, GenerateLowGuid<HighGuid::Entity>()), this, doorWorldPos, false))
    {
        TC_LOG_ERROR("housing", "HousingMap::CreateHouseDoor: the Entity for the door of plot {} could not be created", plotIndex);
        delete attachPoint;
        attachPoint = nullptr;
        return nullptr;
    }
    attachPoint->SetMirroredPosition(offsetLocalPos, identity, 1.0f, entryGuid, HOUSING_ATTACHMENT_FLAGS_PIECE);
    PhasingHandler::InitDbPhaseShift(attachPoint->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);

    // The door: identity rotation, riding the Entity at position zero, CreatedBy the house, Flags 0 (hbcd3
    // 1310790-1310861). Its stationary orientation is the chain's turn.
    LoadGrid(doorWorldPos.GetPositionX(), doorWorldPos.GetPositionY());
    GameObject* door = GameObject::CreateGameObject(doorEntry, this, doorWorldPos, identity, 255, GO_STATE_READY);
    if (!door)
    {
        TC_LOG_ERROR("housing", "HousingMap::CreateHouseDoor: door {} of plot {} could not be created", doorEntry, plotIndex);
        delete attachPoint;
        attachPoint = nullptr;
        return nullptr;
    }

    PhasingHandler::InitDbPhaseShift(door->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);
    door->SetCreatedByGUID(houseGuid);
    door->InitHousingDecorProxy(attachPoint->GetGUID());
    return door;
}

bool HousingMap::AddHouseDoor(uint8 plotIndex, GameObject* door, HousingRoomEntity* attachPoint)
{
    // The entry piece already names the door; without the door it must name nothing.
    ObjectGuid const entryGuid = attachPoint->GetAttachParentGUID();
    auto forgetDoorOnEntry = [this, entryGuid]()
    {
        if (MeshObject* entryMesh = GetMeshObject(entryGuid))
            entryMesh->SetFixtureGameObjectGUID(ObjectGuid::Empty);
    };

    // Retail sent the door before the Entity it rides (hled1 819290).
    ObjectGuid const doorGuid = door->GetGUID();
    ObjectGuid const attachPointGuid = attachPoint->GetGUID();
    HoldBack(doorGuid, true);
    HoldBack(attachPointGuid, true);

    if (!AddToMap(attachPoint))
    {
        TC_LOG_ERROR("housing", "HousingMap::AddHouseDoor: the Entity for the door of plot {} could not be added to the map", plotIndex);
        ForgetHeldBack(doorGuid);
        ForgetHeldBack(attachPointGuid);
        delete attachPoint;
        delete door;
        forgetDoorOnEntry();
        return false;
    }
    _doorAttachPointGuids[plotIndex] = attachPoint->GetGUID();

    if (!AddToMap(door))
    {
        TC_LOG_ERROR("housing", "HousingMap::AddHouseDoor: door {} of plot {} could not be added to the map", door->GetEntry(), plotIndex);
        ForgetHeldBack(doorGuid);
        ForgetHeldBack(attachPointGuid);
        delete door;
        // The Entity the door would have ridden leaves with it.
        _doorAttachPointGuids.erase(plotIndex);
        RemoveHousingEntityNow(attachPoint->GetGUID());
        forgetDoorOnEntry();
        return false;
    }
    _houseGameObjects[plotIndex] = door->GetGUID();

    TC_LOG_DEBUG("housing", "HousingMap::AddHouseDoor: door {} of plot {} at ({:.3f}, {:.3f}, {:.3f}) facing {:.4f}, riding {}",
        door->GetGUID().ToString(), plotIndex, door->GetPositionX(), door->GetPositionY(), door->GetPositionZ(),
        door->GetOrientation(), attachPoint->GetGUID().ToString());
    return true;
}

void HousingMap::DespawnAllMeshObjectsForPlot(uint8 plotIndex)
{
    auto itr = _meshObjects.find(plotIndex);
    if (itr == _meshObjects.end())
        return;

    for (ObjectGuid const& guid : itr->second)
    {
        if (MeshObject* mesh = GetMeshObject(guid))
            mesh->AddObjectToRemoveList();
    }

    TC_LOG_DEBUG("housing", "HousingMap::DespawnAllMeshObjectsForPlot: Despawned {} MeshObject(s) for plot {}",
        itr->second.size(), plotIndex);
    _meshObjects.erase(itr);
}

MeshObject* HousingMap::FindMeshObjectByHookID(uint8 plotIndex, int32 hookID)
{
    auto itr = _meshObjects.find(plotIndex);
    if (itr == _meshObjects.end())
        return nullptr;

    for (ObjectGuid const& guid : itr->second)
    {
        if (MeshObject* mesh = GetMeshObject(guid))
        {
            if (mesh->GetExteriorComponentHookID() == hookID)
                return mesh;
        }
    }
    return nullptr;
}

MeshObject* HousingMap::GetPlotMeshObject(uint8 plotIndex, ObjectGuid meshGuid)
{
    auto itr = _meshObjects.find(plotIndex);
    if (itr == _meshObjects.end() || std::find(itr->second.begin(), itr->second.end(), meshGuid) == itr->second.end())
        return nullptr;

    return GetMeshObject(meshGuid);
}

void HousingMap::DespawnSingleMeshObject(uint8 plotIndex, ObjectGuid meshGuid, std::vector<ObjectGuid>* removed /*= nullptr*/)
{
    auto itr = _meshObjects.find(plotIndex);
    if (itr == _meshObjects.end())
        return;

    // Also remove any children attached to this mesh (recursive)
    std::vector<ObjectGuid> toRemove;
    toRemove.push_back(meshGuid);

    // Find children (meshes whose AttachParentGUID == meshGuid)
    for (ObjectGuid const& guid : itr->second)
    {
        if (MeshObject* mesh = GetMeshObject(guid))
        {
            if (mesh->GetAttachParentGUID() == meshGuid)
                toRemove.push_back(guid);
        }
    }

    // The door rides an Entity on its entry; it goes with the entry.
    std::vector<ObjectGuid> doorObjects;
    if (auto doorItr = _doorAttachPointGuids.find(plotIndex); doorItr != _doorAttachPointGuids.end())
        if (HousingRoomEntity const* attachPoint = GetHousingRoomEntity(doorItr->second))
            if (std::find(toRemove.begin(), toRemove.end(), attachPoint->GetAttachParentGUID()) != toRemove.end())
                DespawnDoorGO(plotIndex, &doorObjects);

    // The door first, then the pieces, then the Entity the door rode.
    if (removed && !doorObjects.empty())
        removed->push_back(doorObjects.front());

    for (ObjectGuid const& guid : toRemove)
    {
        if (MeshObject* mesh = GetMeshObject(guid))
        {
            mesh->AddObjectToRemoveList();
            if (removed)
                removed->push_back(guid);
        }

        auto& vec = itr->second;
        vec.erase(std::remove(vec.begin(), vec.end(), guid), vec.end());
    }

    if (removed && doorObjects.size() > 1)
        removed->insert(removed->end(), doorObjects.begin() + 1, doorObjects.end());

    TC_LOG_DEBUG("housing", "HousingMap::DespawnSingleMeshObject: Removed {} mesh(es) for plot {} (root {})",
        toRemove.size(), plotIndex, meshGuid.ToString());
}

MeshObject* HousingMap::SpawnFixtureAtHook(uint8 plotIndex, uint32 hookID, uint32 componentID,
    ObjectGuid houseGuid, int32 houseExteriorWmoDataID, ObjectGuid attachParentGuid)
{
    ExteriorComponentHookEntry const* hookEntry = sExteriorComponentHookStore.LookupEntry(hookID);
    if (!hookEntry)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnFixtureAtHook: Hook {} not found in DB2", hookID);
        return nullptr;
    }

    // The piece the client named must be one of this plot's and the one that owns the hook.
    MeshObject* parentMesh = GetPlotMeshObject(plotIndex, attachParentGuid);
    if (!parentMesh || parentMesh->GetExteriorComponentID() != static_cast<int32>(hookEntry->ExteriorComponentID))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnFixtureAtHook: {} is not the piece of plot {} that owns hook {} (component {})",
            attachParentGuid.ToString(), plotIndex, hookID, hookEntry->ExteriorComponentID);
        return nullptr;
    }

    // The hook's position and turn are the new piece's pose on the parent; the parent's place in the world comes
    // from its attachment chain.
    Position hookPos(hookEntry->Position[0], hookEntry->Position[1], hookEntry->Position[2]);
    Position parentWorldPos;
    QuaternionData parentWorldRot;
    if (!GetWorldPose(parentMesh->GetGUID(), parentWorldPos, parentWorldRot))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnFixtureAtHook: the attachment chain of the piece owning hook {} on plot {} is broken",
            hookID, plotIndex);
        return nullptr;
    }

    // Spawn the component tree at this hook (may have sub-hooks/children).
    // Pass hookID as the override so the top-level mesh gets ExteriorComponentHookID = hookID
    // (the actual hook point where we're installing it, not the component's native HookID from DB2).
    uint32 spawned = SpawnExtCompTree(plotIndex, componentID,
        hookPos, HousingMgr::GetHookRotation(*hookEntry),
        houseGuid, houseExteriorWmoDataID,
        parentMesh->GetGUID(), parentWorldPos, parentWorldRot, 0, nullptr,
        static_cast<int32>(hookID));

    TC_LOG_DEBUG("housing", "HousingMap::SpawnFixtureAtHook: Spawned {} mesh(es) for hook {} component {} on plot {}",
        spawned, hookID, componentID, plotIndex);

    // Return the first (root) mesh at the hook
    return FindMeshObjectByHookID(plotIndex, static_cast<int32>(hookID));
}

void HousingMap::DespawnHouseForPlot(uint8 plotIndex)
{
    // The door and the pieces first, then what they hang on.
    DespawnDoorGO(plotIndex);
    DespawnAllMeshObjectsForPlot(plotIndex);

    // The house entity and the exterior root have fixed GUIDs and are built again straight away when a house is
    // rebuilt, so they leave the map at once rather than at the end of the update.
    if (auto itr = _houseEntityGuids.find(plotIndex); itr != _houseEntityGuids.end())
    {
        RemoveHousingEntityNow(itr->second);
        _houseEntityGuids.erase(itr);
    }

    if (auto itr = _exteriorRootGuids.find(plotIndex); itr != _exteriorRootGuids.end())
    {
        RemoveHousingEntityNow(itr->second);
        _exteriorRootGuids.erase(itr);
    }

    DespawnRoomForPlot(plotIndex);

    TC_LOG_DEBUG("housing", "HousingMap::DespawnHouseForPlot: Despawned the house on plot {}", plotIndex);
}

HousingRoomEntity* HousingMap::GetRoomIdentityEntity(uint8 plotIndex) const
{
    auto itr = _roomIdentityGuids.find(plotIndex);
    if (itr == _roomIdentityGuids.end())
        return nullptr;
    return const_cast<HousingMap*>(this)->GetObjectsStore().Find<HousingRoomEntity>(itr->second);
}

ObjectGuid HousingMap::GetRoomIdentityGuid(uint8 plotIndex) const
{
    auto itr = _roomIdentityGuids.find(plotIndex);
    return itr != _roomIdentityGuids.end() ? itr->second : ObjectGuid::Empty;
}

void HousingMap::DespawnDoorGO(uint8 plotIndex, std::vector<ObjectGuid>* removed /*= nullptr*/)
{
    if (auto itr = _houseGameObjects.find(plotIndex); itr != _houseGameObjects.end())
    {
        if (GameObject* go = GetGameObject(itr->second))
        {
            go->AddObjectToRemoveList();
            if (removed)
                removed->push_back(itr->second);
        }

        TC_LOG_DEBUG("housing", "HousingMap::DespawnDoorGO: Removed door GO {} for plot {}",
            itr->second.ToString(), plotIndex);
        _houseGameObjects.erase(itr);
    }

    if (auto itr = _doorAttachPointGuids.find(plotIndex); itr != _doorAttachPointGuids.end())
    {
        if (HousingRoomEntity* attachPoint = GetHousingRoomEntity(itr->second))
        {
            attachPoint->AddObjectToRemoveList();
            if (removed)
                removed->push_back(itr->second);
        }
        _doorAttachPointGuids.erase(itr);
    }
}

void HousingMap::RespawnDoorGOAtHook(uint8 plotIndex, uint32 hookID, uint32 doorComponentID, Housing const* housing)
{
    ExteriorComponentEntry const* doorComp = sExteriorComponentStore.LookupEntry(doorComponentID);
    if (!doorComp || doorComp->GameObjectID <= 0 || !housing)
    {
        TC_LOG_ERROR("housing", "HousingMap::RespawnDoorGOAtHook: No GameObjectID for door comp {} at hook {}", doorComponentID, hookID);
        return;
    }

    MeshObject* entryMesh = FindMeshObjectByHookID(plotIndex, static_cast<int32>(hookID));
    if (!entryMesh)
    {
        TC_LOG_ERROR("housing", "HousingMap::RespawnDoorGOAtHook: plot {} has no piece on hook {}, so its door has nothing to ride",
            plotIndex, hookID);
        return;
    }

    // Building the entry already made its door; nothing more to do when the door rides this entry.
    if (GetHouseGameObject(plotIndex))
        if (auto itr = _doorAttachPointGuids.find(plotIndex); itr != _doorAttachPointGuids.end())
            if (HousingRoomEntity const* attachPoint = GetHousingRoomEntity(itr->second))
                if (attachPoint->GetAttachParentGUID() == entryMesh->GetGUID())
                    return;

    Position entryWorldPos;
    QuaternionData entryWorldRot;
    if (!GetWorldPose(entryMesh->GetGUID(), entryWorldPos, entryWorldRot))
    {
        TC_LOG_ERROR("housing", "HousingMap::RespawnDoorGOAtHook: the attachment chain of the entry on hook {} of plot {} is broken",
            hookID, plotIndex);
        return;
    }

    HousingRoomEntity* attachPoint = nullptr;
    GameObject* door = CreateHouseDoor(plotIndex, entryMesh->GetGUID(), *doorComp, entryWorldPos, entryWorldRot,
        housing->GetHouseGuid(), attachPoint);
    if (!door)
        return;

    entryMesh->SetFixtureGameObjectGUID(door->GetGUID());
    AddHouseDoor(plotIndex, door, attachPoint);
}

GameObject* HousingMap::GetHouseGameObject(uint8 plotIndex)
{
    auto itr = _houseGameObjects.find(plotIndex);
    if (itr == _houseGameObjects.end())
        return nullptr;

    return GetGameObject(itr->second);
}

int8 HousingMap::GetPlotIndexForHouseGO(ObjectGuid goGuid) const
{
    for (auto const& [plotIndex, guid] : _houseGameObjects)
    {
        if (guid == goGuid)
            return static_cast<int8>(plotIndex);
    }
    return -1;
}

GameObject* HousingMap::FindHouseDoorInReach(Player const* player, uint32 gooberSpellId, uint8& plotIndex)
{
    GameObject* nearest = nullptr;
    float nearestDistSq = 0.0f;
    for (auto const& [doorPlotIndex, doorGuid] : _houseGameObjects)
    {
        GameObject* door = GetGameObject(doorGuid);
        if (!door || door->GetGoType() != GAMEOBJECT_TYPE_GOOBER || door->GetGOInfo()->goober.spell != gooberSpellId)
            continue;

        if (!door->IsAtInteractDistance(player))
            continue;

        float distSq = door->GetExactDistSq(player);
        if (nearest && distSq >= nearestDistSq)
            continue;

        nearest = door;
        nearestDistSq = distSq;
        plotIndex = doorPlotIndex;
    }

    if (nearest && _neighborhood)
    {
        ObjectGuid createdBy = nearest->GetCreatorGUID();
        if (!createdBy.IsEmpty())
            if (Neighborhood::PlotInfo const* plot = _neighborhood->GetPlotInfoByHouse(createdBy))
                plotIndex = plot->PlotIndex;
    }

    return nearest;
}

// ============================================================
// Decor Management
// ============================================================
// Functional decor (HouseDecorData.GameObjectID > 0 AND gameobject_template
// exists) spawns as a real interactive GameObject with FHousingDecor_C +
// FMirroredPositionData_C fragments, retaining normal GO behavior (sit on
// chairs, open chests, mail UI on mailboxes, etc.). Retail sniff-verified
// fragment set: [CGObject, FHousingDecor_C, FMirroredPositionData_C, Tag_GameObject].
//
// Visual-only decor (ModelFileDataID-only) spawns as a MeshObject with
// fragments [CGObject, FMeshObjectData_C, FHousingDecor_C, FMirroredPositionData_C,
// Tag_MeshObject] — also retail-verified.

bool HousingMap::SpawnDecorItem(uint8 plotIndex, Housing::PlacedDecor const& decor, ObjectGuid houseGuid)
{
    // A piece stands on the map once: a copy still standing from before, which a missed take-down would leave behind,
    // goes first.
    if (_decorGuidToGoGuid.contains(decor.Guid))
    {
        auto plotItr = _decorGuidToPlotIndex.find(decor.Guid);
        DespawnDecorItem(plotItr != _decorGuidToPlotIndex.end() ? plotItr->second : plotIndex, decor.Guid);
    }

    HouseDecorData const* decorData = sHousingMgr.GetHouseDecorData(decor.DecorEntryId);
    if (!decorData)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnDecorItem: No HouseDecorData for entry {} (decorGuid={})",
            decor.DecorEntryId, decor.Guid.ToString());
        return false;
    }

    // Look up the room entity we attach to (the Housing/sub2 plot room).
    ObjectGuid roomEntityGuid = ObjectGuid::Empty;
    Position roomWorldPos;
    if (HousingRoomEntity* roomId = GetRoomIdentityEntity(plotIndex))
    {
        roomEntityGuid = roomId->GetGUID();
        roomWorldPos = roomId->GetPosition();
    }

    float worldX = decor.PosX;
    float worldY = decor.PosY;
    float worldZ = decor.PosZ;
    LoadGrid(worldX, worldY);

    QuaternionData rot(decor.RotationX, decor.RotationY, decor.RotationZ, decor.RotationW);

    // World → room-local (inverse room rotation). Matches MeshObject decor path.
    float localX = worldX;
    float localY = worldY;
    float localZ = worldZ;
    if (!roomEntityGuid.IsEmpty())
    {
        float dx = worldX - roomWorldPos.GetPositionX();
        float dy = worldY - roomWorldPos.GetPositionY();
        float roomFacing = roomWorldPos.GetOrientation();
        float cosF = std::cos(roomFacing);
        float sinF = std::sin(roomFacing);
        localX =  cosF * dx + sinF * dy;
        localY = -sinF * dx + cosF * dy;
        localZ = worldZ - roomWorldPos.GetPositionZ();
    }

    Position localPos(localX, localY, localZ);
    Position worldPos(worldX, worldY, worldZ);
    float decorScale = decor.Scale > 0.01f ? decor.Scale : 1.0f;
    uint8 attachFlags = roomEntityGuid.IsEmpty() ? uint8(0) : uint8(3);

    // ---------- Functional decor branch (real GameObject) ----------
    // Retail: chairs, chests, mailboxes, chandeliers, fireplaces, etc. spawn as
    // real GameObjects so the client treats them as interactive (sittable/
    // openable/usable). We require both GameObjectID in DB2 AND a matching
    // gameobject_template to fall into this path; missing templates fall back
    // to visual-only MeshObject.
    if (decorData->GameObjectID > 0)
    {
        uint32 goEntry = static_cast<uint32>(decorData->GameObjectID);
        if (GameObjectTemplate const* goTemplate = sObjectMgr->GetGameObjectTemplate(goEntry))
        {
            // Derive orientation from the quaternion for GO world rotation.
            // GameObject::SetLocalRotation uses the packed quat for rendering;
            // the orientation-from-quat is used for stationary direction.
            float orientation = 2.0f * std::atan2(rot.z, rot.w);
            Position goWorldPos(worldX, worldY, worldZ, orientation);

            GameObject* go = GameObject::CreateGameObject(goEntry, this, goWorldPos, rot,
                255 /*animProgress*/, GO_STATE_READY, 0 /*artKit*/);
            if (!go)
            {
                TC_LOG_ERROR("housing", "HousingMap::SpawnDecorItem: CreateGameObject failed for decor entry={} "
                    "goEntry={} at ({:.1f},{:.1f},{:.1f}) — falling back to MeshObject",
                    decor.DecorEntryId, goEntry, worldX, worldY, worldZ);
            }
            else
            {
                PhasingHandler::InitDbPhaseShift(go->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);
                go->SetObjectScale(decorScale);

                // Keep template default flags — chairs need CHAIR type behavior, chests need
                // CHEST interaction, mailboxes need MAILBOX UI. Don't override with door-style
                // flags; the GO's normal on-use handler is what we want.

                // Retail wire fragments: FHousingDecor_C + FMirroredPositionData_C.
                // Order matches the sniff-verified fragment list.
                go->InitHousingDecorData(decor.Guid, houseGuid, decor.Locked ? 1 : 0,
                    roomEntityGuid, DECOR_SOURCE_NONE, std::string());
                go->InitHousingDecorMirroredPosition(localPos, rot, decorScale, roomEntityGuid, attachFlags);
                // It rides the room it stands in, at its place and turn in that room (hbcd3 1411570-1411650).
                if (!roomEntityGuid.IsEmpty())
                    go->SetHousingTransport(roomEntityGuid, Position(localX, localY, localZ,
                        Position::NormalizeOrientation(orientation - roomWorldPos.GetOrientation())));

                if (!AddToMap(go))
                {
                    TC_LOG_ERROR("housing", "HousingMap::SpawnDecorItem: AddToMap failed for GO decor "
                        "entry={} goEntry={} decorGuid={}",
                        decor.DecorEntryId, goEntry, decor.Guid.ToString());
                    delete go;
                    return false;
                }

                _decorGameObjects[plotIndex].push_back(go->GetGUID());
                _decorGuidToGoGuid[decor.Guid] = go->GetGUID();
                _decorGuidToPlotIndex[decor.Guid] = plotIndex;

                TC_LOG_DEBUG("housing", "HousingMap::SpawnDecorItem: Spawned functional-decor GameObject "
                    "entry={} goEntry={} goType={} goGuid={} decorGuid={} "
                    "at world({:.1f},{:.1f},{:.1f}) local({:.1f},{:.1f},{:.1f}) scale={:.2f} "
                    "room={} plot={}",
                    decor.DecorEntryId, goEntry, uint32(goTemplate->type), go->GetGUID().ToString(),
                    decor.Guid.ToString(), worldX, worldY, worldZ, localX, localY, localZ,
                    decorScale, roomEntityGuid.ToString(), plotIndex);
                return true;
            }
        }
        else
        {
            TC_LOG_DEBUG("housing", "HousingMap::SpawnDecorItem: GameObjectID={} referenced by decor entry={} "
                "is not in gameobject_template — falling back to MeshObject (visual-only)",
                goEntry, decor.DecorEntryId);
        }
    }

    // ---------- Visual-only branch (MeshObject) ----------
    int32 fileDataID = decorData->ModelFileDataID;
    if (fileDataID <= 0 && decorData->GameObjectID > 0)
    {
        // Fallback: derive FileDataID from the GO template displayInfo when the
        // DB2 entry's ModelFileDataID is 0 but GameObjectID points at a valid template.
        if (GameObjectTemplate const* goTemplate = sObjectMgr->GetGameObjectTemplate(
                static_cast<uint32>(decorData->GameObjectID)))
        {
            if (GameObjectDisplayInfoEntry const* displayInfo =
                    sGameObjectDisplayInfoStore.LookupEntry(goTemplate->displayId))
            {
                if (displayInfo->FileDataID > 0)
                    fileDataID = displayInfo->FileDataID;
            }
        }
    }

    if (fileDataID <= 0)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnDecorItem: Cannot derive FileDataID for decor entry {} "
            "(GameObjectID={}, ModelFileDataID={}), skipping",
            decor.DecorEntryId, decorData->GameObjectID, decorData->ModelFileDataID);
        return false;
    }

    MeshObject* mesh = MeshObject::CreateMeshObject(this, localPos, rot, decorScale,
        fileDataID, /*isWMO*/ false, roomEntityGuid, attachFlags, &worldPos);

    if (!mesh)
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnDecorItem: Failed to create decor MeshObject fileDataID={} for decor {}",
            fileDataID, decor.Guid.ToString());
        return false;
    }

    PhasingHandler::InitDbPhaseShift(mesh->GetPhaseShift(), PHASE_USE_FLAGS_ALWAYS_VISIBLE, 0, 0);
    mesh->InitHousingDecorData(decor.Guid, houseGuid, decor.Locked ? 1 : 0, roomEntityGuid, DECOR_SOURCE_NONE, std::string());

    if (!AddToMap(mesh))
    {
        TC_LOG_ERROR("housing", "HousingMap::SpawnDecorItem: Failed to add decor MeshObject to map for decor {}", decor.Guid.ToString());
        delete mesh;
        return false;
    }

    _decorGameObjects[plotIndex].push_back(mesh->GetGUID());
    _decorGuidToGoGuid[decor.Guid] = mesh->GetGUID();
    _decorGuidToPlotIndex[decor.Guid] = plotIndex;

    TC_LOG_DEBUG("housing", "HousingMap::SpawnDecorItem: Spawned decor MeshObject fileDataID={} meshGuid={} decorGuid={} "
        "at world({:.1f},{:.1f},{:.1f}) local({:.1f},{:.1f},{:.1f}) scale={:.2f} room={} plot={}",
        fileDataID, mesh->GetGUID().ToString(), decor.Guid.ToString(),
        worldX, worldY, worldZ, localX, localY, localZ, decorScale,
        roomEntityGuid.ToString(), plotIndex);
    return true;
}

void HousingMap::DespawnDecorItem(uint8 plotIndex, ObjectGuid decorGuid)
{
    auto itr = _decorGuidToGoGuid.find(decorGuid);
    if (itr == _decorGuidToGoGuid.end())
        return;

    ObjectGuid objGuid = itr->second;
    // Decor may be either a functional-decor GameObject or a visual-only MeshObject.
    if (objGuid.IsGameObject())
    {
        if (GameObject* go = GetGameObject(objGuid))
            go->AddObjectToRemoveList();
    }
    else if (MeshObject* mesh = GetMeshObject(objGuid))
        mesh->AddObjectToRemoveList();

    auto& plotDecor = _decorGameObjects[plotIndex];
    plotDecor.erase(std::remove(plotDecor.begin(), plotDecor.end(), objGuid), plotDecor.end());
    _decorGuidToGoGuid.erase(itr);
    _decorGuidToPlotIndex.erase(decorGuid);

    TC_LOG_DEBUG("housing", "HousingMap::DespawnDecorItem: Despawned decor {} for decorGuid={} plot={}",
        objGuid.ToString(), decorGuid.ToString(), plotIndex);
}

void HousingMap::DespawnAllDecorForPlot(uint8 plotIndex)
{
    auto itr = _decorGameObjects.find(plotIndex);
    if (itr == _decorGameObjects.end())
        return;

    for (ObjectGuid const& objGuid : itr->second)
    {
        if (objGuid.IsGameObject())
        {
            if (GameObject* go = GetGameObject(objGuid))
                go->AddObjectToRemoveList();
        }
        else if (MeshObject* mesh = GetMeshObject(objGuid))
            mesh->AddObjectToRemoveList();
    }

    // Clean up all tracking for this plot's decor
    std::vector<ObjectGuid> decorGuidsToRemove;
    for (auto const& [decorGuid, pIdx] : _decorGuidToPlotIndex)
    {
        if (pIdx == plotIndex)
            decorGuidsToRemove.push_back(decorGuid);
    }
    for (ObjectGuid const& decorGuid : decorGuidsToRemove)
    {
        _decorGuidToGoGuid.erase(decorGuid);
        _decorGuidToPlotIndex.erase(decorGuid);
    }

    itr->second.clear();
    _decorSpawnedPlots.erase(plotIndex);

    TC_LOG_DEBUG("housing", "HousingMap::DespawnAllDecorForPlot: Despawned all decor MeshObjects for plot {}", plotIndex);
}

void HousingMap::SpawnAllDecorForPlot(uint8 plotIndex, Housing const* housing)
{
    if (!housing)
        return;

    if (_decorSpawnedPlots.count(plotIndex))
    {
        TC_LOG_DEBUG("housing", "HousingMap::SpawnAllDecorForPlot: Plot {} already in _decorSpawnedPlots — skipping respawn "
            "(decorGuidMap.size={} decorGOs[{}].size={})",
            plotIndex, uint32(_decorGuidToGoGuid.size()),
            plotIndex, _decorGameObjects.count(plotIndex) ? uint32(_decorGameObjects[plotIndex].size()) : 0);
        return; // Already spawned
    }

    ObjectGuid houseGuid = housing->GetHouseGuid();
    uint32 spawnCount = 0;
    uint32 exteriorCount = 0;
    uint32 failCount = 0;
    for (auto const& [decorGuid, decor] : housing->GetPlacedDecorMap())
    {
        // Skip interior decor — those are spawned by HouseInteriorMap::SpawnInteriorDecor. A yard piece placed this
        // session still carries the plot's room GUID the client sent, so the yard test is not an empty room.
        if (!Housing::IsExteriorDecorPlacement(decor.RoomGuid))
            continue;

        ++exteriorCount;
        if (SpawnDecorItem(plotIndex, decor, houseGuid))
            ++spawnCount;
        else
            ++failCount;
    }

    _decorSpawnedPlots.insert(plotIndex);

    TC_LOG_DEBUG("housing", "HousingMap::SpawnAllDecorForPlot: Spawned {}/{} exterior decor for plot {} "
        "(failed={}, neighborhood='{}')",
        spawnCount, exteriorCount, plotIndex, failCount,
        _neighborhood ? _neighborhood->GetName() : "?");
}

void HousingMap::UpdateDecorPosition(uint8 plotIndex, ObjectGuid decorGuid, Position const& pos, QuaternionData const& rot, float scale /*= 1.0f*/)
{
    auto itr = _decorGuidToGoGuid.find(decorGuid);
    if (itr == _decorGuidToGoGuid.end())
        return;

    ObjectGuid objGuid = itr->second;
    if (objGuid.IsGameObject())
    {
        if (GameObject* go = GetGameObject(objGuid))
        {
            go->Relocate(pos);
            go->SetLocalRotation(rot.x, rot.y, rot.z, rot.w);
            if (std::abs(go->GetObjectScale() - scale) > 0.001f)
                go->SetObjectScale(scale);
            TC_LOG_DEBUG("housing", "HousingMap::UpdateDecorPosition: Moved decor GameObject {} to ({:.1f}, {:.1f}, {:.1f}) scale={:.2f} for plot {}",
                decorGuid.ToString(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), scale, plotIndex);
        }
    }
    else if (MeshObject* mesh = GetMeshObject(objGuid))
    {
        mesh->Relocate(pos);
        if (std::abs(mesh->GetLocalScale() - scale) > 0.001f)
            mesh->UpdateLocalScale(scale);
        TC_LOG_DEBUG("housing", "HousingMap::UpdateDecorPosition: Moved decor MeshObject {} to ({:.1f}, {:.1f}, {:.1f}) scale={:.2f} for plot {}",
            decorGuid.ToString(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), scale, plotIndex);
    }
}

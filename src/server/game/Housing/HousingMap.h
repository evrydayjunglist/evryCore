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

#ifndef HousingMap_h__
#define HousingMap_h__

#include "Housing.h"
#include "Map.h"
#include "Neighborhood.h"
#include <memory>
#include <unordered_set>

class AreaTrigger;
class Housing;
class HousingRoomEntity;
class MeshObject;
class Neighborhood;
struct ExteriorComponentEntry;
class Player;

class TC_GAME_API HousingMap : public Map
{
public:
    HousingMap(uint32 id, time_t expiry, uint32 instanceId, Difficulty spawnMode, uint32 neighborhoodId);
    ~HousingMap();

    void InitVisibilityDistance() override;
    void LoadGridObjects(NGridType* grid) override;
    bool AddPlayerToMap(Player* player, bool initPlayer = true) override;
    void RemovePlayerFromMap(Player* player, bool remove) override;

    // The live Housing of a house whose account has a character on this map, or nothing.
    Housing* GetHousingForHouse(ObjectGuid houseGuid) const;
    AreaTrigger* GetPlotAreaTrigger(uint8 plotIndex);
    // Returns the plot index whose plot-bounds AT equals `atGuid`, or -1 when none.
    int8 GetPlotIndexForAreaTrigger(ObjectGuid atGuid) const;
    // Returns the plot whose cornerstone this map spawned with `cornerstoneGuid`, or -1 when none.
    int8 GetPlotIndexForCornerstone(ObjectGuid cornerstoneGuid) const;
    GameObject* GetPlotGameObject(uint8 plotIndex);
    void SetPlotOwnershipState(uint8 plotIndex, bool owned);
    HousingPlotOwnerType GetPlotOwnerTypeForPlayer(Player const* player, uint8 plotIndex) const;
    void SendPerPlayerPlotWorldStates(Player* player);
    Neighborhood* GetNeighborhood() const { return _neighborhood; }
    uint32 GetNeighborhoodId() const { return _neighborhoodId; }

    void LoadNeighborhoodData();
    void SpawnPlotGameObjects();
    void LockPlotGrids();

    // Live houses of the characters on this map, by house GUID. The index holds raw pointers to Housing objects
    // that the characters own, so an entry must never outlive its Housing: a character leaving the map drops her
    // entries, and a Housing that is destroyed while its character is still here (a relinquished, reset, evicted or
    // forgotten house) must be dropped with ForgetHousing first. Either way the house is handed to another character
    // of the account on this map who holds it too, if there is one.
    void AddPlayerHousing(Housing* housing);
    void RemovePlayerHousing(Player* player);
    void ForgetHousing(Housing const* housing);
    // A packed house stands on no plot of this map any more: stop listing it, without handing it to anyone.
    void DropHouse(ObjectGuid houseGuid);

    // Fixture override map: hookID → ExteriorComponentID from player's fixture selections.
    // When provided, SpawnExtCompTree uses these instead of the DB2 default component at each hook.
    using FixtureOverrideMap = std::unordered_map<uint32 /*hookID*/, uint32 /*extCompID*/>;

    // Root override map: componentType → componentID from player's root fixture selections
    // (e.g., player chose a specific roof variant). When provided, SpawnFullHouseMeshObjects
    // uses these instead of the DB2 default root for that type.
    using RootOverrideMap = std::unordered_map<uint8 /*componentType*/, uint32 /*compID*/>;

    // Builds the house on a plot: its room at the plot's room anchor, the exterior root Entity in the room at the
    // house's placement, the house entity other accounts see, and the pieces hanging on the root, with the front door
    // riding the Entity at its entry's EntryOffset (hbcd3 1299598-1311080). customPos is the saved placement, the
    // root's pose inside the room; without one the root stands at the room's centre. False when the plot holds no house
    // or has no room anchor.
    bool SpawnHouseForPlot(uint8 plotIndex, Position const* customPos,
        int32 exteriorComponentID, int32 houseExteriorWmoDataID,
        FixtureOverrideMap const* fixtureOverrides = nullptr,
        RootOverrideMap const* rootOverrides = nullptr);
    // Builds a house on its plot from the house's own saved state: its core component and style, its fixture and root
    // choices, and its placement. The purchase, the move to another plot and a character's arrival all build the same
    // house this way.
    bool SpawnHouseFromState(uint8 plotIndex, Housing const& housing);
    void DespawnHouseForPlot(uint8 plotIndex);
    void RespawnDoorGOAtHook(uint8 plotIndex, uint32 hookID, uint32 doorComponentID, Housing const* housing, Player* player = nullptr);
    void DespawnDoorGO(uint8 plotIndex);
    GameObject* GetHouseGameObject(uint8 plotIndex);
    int8 GetPlotIndexForHouseGO(ObjectGuid goGuid) const;
    // The house front door the player is using: of this map's house doors whose goober spell is gooberSpellId, the
    // nearest one she can reach, with the plot of its house. The house is the one the door's CreatedBy names when it
    // names one, otherwise the plot the door was spawned for. Null when she stands at none.
    GameObject* FindHouseDoorInReach(Player const* player, uint32 gooberSpellId, uint8& plotIndex);
    uint32 GetHouseGameObjectCount() const { return static_cast<uint32>(_houseGameObjects.size()); }

    // Whether the house on a plot is standing (its exterior root is on the map).
    bool IsHouseSpawned(uint8 plotIndex) const;
    ObjectGuid GetExteriorRootGuid(uint8 plotIndex) const;
    // Where a housing object of this map stands in the world, worked out along its attachment chain (a piece, the
    // exterior root, an attach point or a room). False when the chain is broken.
    bool GetWorldPose(ObjectGuid guid, Position& position, QuaternionData& rotation, uint32 depth = 0);

    // Lightweight Housing/2 identity room entity (HighGuid::Housing subType=2,
    // objectType=18) — the authoritative room. Retail-verified architecture:
    // one Housing/2 identity per plot + one component MeshObject attached to
    // it (carries the Geobox). The house's exterior root attaches here.
    HousingRoomEntity* GetRoomIdentityEntity(uint8 plotIndex) const;
    ObjectGuid GetRoomIdentityGuid(uint8 plotIndex) const;

    // The house's structural pieces, each hanging on the exterior root at local zero, and the pieces on their hooks.
    void SpawnFullHouseMeshObjects(uint8 plotIndex, ObjectGuid rootGuid, Position const& rootWorldPos,
        QuaternionData const& rootWorldRot, ObjectGuid houseGuid,
        int32 exteriorComponentID, int32 houseExteriorWmoDataID,
        FixtureOverrideMap const* fixtureOverrides = nullptr,
        RootOverrideMap const* rootOverrides = nullptr);
    // One piece, attached to parentGuid at localPos and localRot, and the pieces on its hooks. parentWorldPos and
    // parentWorldRot are where the parent stands in the world. hookID is the hook the piece occupies, -1 for a
    // structural piece.
    uint32 SpawnExtCompTree(uint8 plotIndex, uint32 extCompID,
        Position const& localPos, QuaternionData const& localRot,
        ObjectGuid houseGuid, int32 houseExteriorWmoDataID,
        ObjectGuid parentGuid, Position const& parentWorldPos, QuaternionData const& parentWorldRot,
        int32 depth = 0, FixtureOverrideMap const* fixtureOverrides = nullptr, int32 hookID = -1);
    void DespawnAllMeshObjectsForPlot(uint8 plotIndex);

    // Targeted fixture mesh operations (no full house rebuild)
    // Finds and removes the MeshObject at the given hookID for a plot, sends DESTROY to nearby players.
    MeshObject* FindMeshObjectByHookID(uint8 plotIndex, int32 hookID);
    void DespawnSingleMeshObject(uint8 plotIndex, ObjectGuid meshGuid);
    // Spawn a single fixture component at a hook and send CREATE to a specific player.
    MeshObject* SpawnFixtureAtHook(uint8 plotIndex, uint32 hookID, uint32 componentID,
        ObjectGuid houseGuid, int32 houseExteriorWmoDataID, Player* target);

    // Room entity management (provides Geobox for client OutsidePlotBounds check)
    bool SpawnRoomForPlot(uint8 plotIndex, Position const& anchorPos,
        QuaternionData const& anchorRot, ObjectGuid houseGuid);
    void DespawnRoomForPlot(uint8 plotIndex);

    // Decor management. Functional decor (HouseDecorData.GameObjectID > 0) spawns
    // as an interactive GameObject with FHousingDecor_C + FMirroredPositionData_C
    // fragments — retains sit/open/use behavior. Visual-only decor spawns as a
    // MeshObject. Returns true on success.
    bool SpawnDecorItem(uint8 plotIndex, Housing::PlacedDecor const& decor, ObjectGuid houseGuid);
    void DespawnDecorItem(uint8 plotIndex, ObjectGuid decorGuid);
    void DespawnAllDecorForPlot(uint8 plotIndex);
    void SpawnAllDecorForPlot(uint8 plotIndex, Housing const* housing);
    void UpdateDecorPosition(uint8 plotIndex, ObjectGuid decorGuid, Position const& pos, QuaternionData const& rot, float scale = 1.0f);

    // Track which plot a player is currently visiting (set by at_housing_plot)
    void SetPlayerCurrentPlot(ObjectGuid playerGuid, uint8 plotIndex) { _playerCurrentPlot[playerGuid] = plotIndex; }
    void ClearPlayerCurrentPlot(ObjectGuid playerGuid) { _playerCurrentPlot.erase(playerGuid); }
    int8 GetPlayerCurrentPlot(ObjectGuid playerGuid) const
    {
        auto itr = _playerCurrentPlot.find(playerGuid);
        return itr != _playerCurrentPlot.end() ? static_cast<int8>(itr->second) : -1;
    }

    // Accessor for diagnostic logging (decor GUID → MeshObject GUID map)
    std::unordered_map<ObjectGuid, ObjectGuid> const& GetDecorGuidMap() const { return _decorGuidToGoGuid; }

    // Accessor for fixture MeshObjects (plotIndex → vector of MeshObject GUIDs)
    std::unordered_map<uint8, std::vector<ObjectGuid>> const& GetPlotMeshObjects() const { return _meshObjects; }

    // Manual spell packet helpers — called from AddPlayerToMap and at_housing_plot AT script.
    // These spells don't exist in DB2, so CastSpell() silently fails; manual packets are required.
    void SendPlotEnterSpellPackets(Player* player, uint8 plotIndex);
    void SendPlotLeaveAuraRemoval(Player* player);

    // Blizzlike neighborhood-map-entry aura burst. Sniff-decoded from
    // dump_12.0.1.66838_2026-04-15_09-35-59.pkt at idx 9985-10000 (and
    // cross-checked against the 2026-04-10 capture). Emits the four
    // housing-specific AURA_UPDATE+SPELL_START+SPELL_GO triples that retail
    // sends immediately after the big UPDATE_OBJECT batch: Housing Fixup
    // (1272741 slot 20), Player Action React (1263578 slot 22), Endeavor
    // Cover (1276064 slot 53), In Your Neighborhood (1227147 slot 121 with
    // SpellXSpellVisualID 503683). Each aura has ActiveFlags and Flags
    // sniff-verified per slot. Non-housing pre-existing character auras
    // (e.g. class talents) are intentionally excluded — core TC aura
    // resync handles those.
    void SendNeighborhoodMapEntryAuras(Player* player);

private:
    uint32 _neighborhoodId;
    Neighborhood* _neighborhood;
    // Puts a house back in the index through another character on this map who holds it, skipping `leaving`.
    void HandPlayerHousingToAnotherCharacter(ObjectGuid houseGuid, Player const* leaving);

    ObjectGuid SpawnExteriorRoot(uint8 plotIndex, Position const& localPos, QuaternionData const& localRot, Position const& worldPos);
    void SpawnPlotHouseEntity(uint8 plotIndex, Neighborhood::PlotInfo const& plot, ObjectGuid rootGuid, Position const& worldPos);
    // Makes a house door and the Entity it rides at the entry's EntryOffset, neither yet on the map; AddHouseDoor adds
    // both. Null when the door has no template.
    GameObject* CreateHouseDoor(uint8 plotIndex, ObjectGuid entryGuid, ExteriorComponentEntry const& entry,
        Position const& entryWorldPos, QuaternionData const& entryWorldRot, ObjectGuid houseGuid, HousingRoomEntity*& attachPoint);
    bool AddHouseDoor(uint8 plotIndex, GameObject* door, HousingRoomEntity* attachPoint);
    // Takes a housing entity of this map off it at once. Rooms, exterior roots and house entities have GUIDs fixed by
    // the plot or the house, and a rebuilt house makes them again in the same update, while the map's object store
    // holds one object per GUID.
    void RemoveHousingEntityNow(ObjectGuid guid);

    std::unordered_map<ObjectGuid /*houseGuid*/, Housing*> _playerHousings;
    std::unordered_map<uint8, ObjectGuid> _plotAreaTriggers;
    std::unordered_map<uint8, ObjectGuid> _plotGameObjects;

    // House structure GO tracking (plotIndex -> house GO GUID)
    std::unordered_map<uint8, ObjectGuid> _houseGameObjects;

    // The exterior root Entity, the house entity other accounts see, and the Entity the front door rides, per plot.
    std::unordered_map<uint8, ObjectGuid> _exteriorRootGuids;
    std::unordered_map<uint8, ObjectGuid> _houseEntityGuids;
    std::unordered_map<uint8, ObjectGuid> _doorAttachPointGuids;

    // Lightweight Housing/2 identity room GUID per plot. The entity itself is
    // a WorldObject owned by the map's object store; we only track its GUID.
    std::unordered_map<uint8, ObjectGuid> _roomIdentityGuids;

    // MeshObject tracking (plotIndex -> vector of MeshObject GUIDs)
    std::unordered_map<uint8, std::vector<ObjectGuid>> _meshObjects;

    // Room entity tracking (plotIndex -> room/component MeshObject GUIDs)
    std::unordered_map<uint8, ObjectGuid> _roomEntities;        // room "entity" MeshObject
    std::unordered_map<uint8, ObjectGuid> _roomComponentMeshes; // room component MeshObject (has Geobox)

    // Decor GO tracking
    std::unordered_map<uint8, std::vector<ObjectGuid>> _decorGameObjects;         // plotIndex -> decor GO GUIDs
    std::unordered_map<ObjectGuid, ObjectGuid> _decorGuidToGoGuid;                // decor GUID -> GO GUID
    std::unordered_map<ObjectGuid, uint8> _decorGuidToPlotIndex;                  // decor GUID -> plotIndex
    std::unordered_set<uint8> _decorSpawnedPlots;                                 // plots whose decor has been spawned
    std::unordered_map<ObjectGuid, uint8> _playerCurrentPlot;                    // player GUID -> current visited plot index
};

#endif // HousingMap_h__

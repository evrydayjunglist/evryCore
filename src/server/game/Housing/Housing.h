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

#ifndef Housing_h__
#define Housing_h__

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "HousingDefines.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Map;
class Player;

namespace WorldPackets
{
    namespace Housing
    {
        class HousingCatalogStateSync;
        struct JamCliHouse;
    }
}

class TC_GAME_API Housing
{
public:
    struct PlacedDecor
    {
        ObjectGuid Guid;
        uint32 DecorEntryId = 0;
        float PosX = 0.0f;
        float PosY = 0.0f;
        float PosZ = 0.0f;
        float RotationX = 0.0f;
        float RotationY = 0.0f;
        float RotationZ = 0.0f;
        float RotationW = 1.0f;
        float Scale = 1.0f;
        std::array<uint32, MAX_HOUSING_DYE_SLOTS> DyeSlots = {};
        ObjectGuid RoomGuid;
        bool Locked = false;
        time_t PlacementTime = 0;
        uint8 SourceType = DECOR_SOURCE_STANDARD;
        std::string SourceValue;
        ObjectGuid PetGuid;         // battle-pet bound to this decor slot (empty = none)
        uint8 PetFlag = 0;          // client-sent flag accompanying the pet binding
    };

    struct Room
    {
        ObjectGuid Guid;
        uint32 RoomEntryId = 0;
        uint32 SlotIndex = 0;
        int32 GridX = 0;        // 2D grid position (yard offsets from origin)
        int32 GridY = 0;
        int32 FloorIndex = 0;  // 0=ground, 1+=upper floors
        uint32 Orientation = 0;
        bool Mirrored = false;
        uint32 ThemeId = 0;           // Legacy single-theme; kept for back-compat
        uint32 WallThemeId = 0;       // Per-surface theme (HouseTheme ID)
        uint32 FloorThemeId = 0;
        uint32 CeilingThemeId = 0;
        uint32 WallTextureId = 0;     // RoomComponentTexture ID for walls
        uint32 FloorTextureId = 0;    // RoomComponentTexture ID for floors
        uint32 CeilingTextureId = 0;  // RoomComponentTexture ID for ceilings
        int32 ColorOverride = -1;     // Shared color override (-1 = default)
        uint32 DoorTypeId = 0;
        uint8 DoorSlot = 0;
        uint32 CeilingTypeId = 0;
        uint8 CeilingSlot = 0;
    };

    struct Fixture
    {
        uint32 FixturePointId = 0;
        uint32 OptionId = 0;
    };

    struct CatalogEntry
    {
        uint32 DecorEntryId = 0;
        uint32 Count = 0;
        uint8 SourceType = DECOR_SOURCE_STANDARD;
        std::string SourceValue;
    };

    // A house belongs to a Battle.net account. Every character of that account that is online holds its own
    // Housing object for the house (its own editor state), and all of them share one stored state per house and
    // one decor catalog per account, so an edit made by one character is what the others see and save.
    // databaseId 0 takes a new id for a house that is being bought; slot is only used for a new house.
    // The owner's session must have a Battle.net account: callers refuse before constructing otherwise.
    Housing(Player* owner, uint64 databaseId, uint8 slot = 0);
    ~Housing();

    Housing(Housing const&) = delete;
    Housing& operator=(Housing const&) = delete;

    // The house GUID retail sends: subtype 3, the account's house slot, the NeighborhoodMap row of the house
    // interior map, and the Battle.net account id as the counter (hbcd3 1340682: 0xDC60000000008007 /
    // 0x0354769D is slot 1, row 7, account 55867037). The two houses of one account differ only in the slot.
    static ObjectGuid MakeHouseGuid(uint8 slot, uint32 interiorNeighborhoodMapId, uint32 bnetAccountId);
    static ObjectGuid MakeHouseGuid(uint8 slot, uint32 bnetAccountId);

    // One house per district: an account may own one house in each neighborhood world map (Founder's Point and
    // Razorwind Shores). True when one of the world maps of the account's houses is the world map of the district
    // being bought in.
    static bool AccountOwnsHouseInDistrict(std::vector<int32> const& ownedHouseWorldMapIds, int32 districtWorldMapId);

    // The slot a new house of an account takes: the first one from 1 that none of its houses uses, packed ones
    // included. 0 when every slot is taken.
    static uint8 FindFreeSlot(std::vector<uint8> const& usedSlots);

    // What a purchase costs: nothing for the account's first house, the plot's cost once the account has any house,
    // standing or packed.
    static uint64 GetPurchasePrice(std::size_t accountHouseCount, uint64 plotCost);

    // Which packed house a purchase in a district unpacks, as an index into packedHouseWorldMapIds (the world map of
    // the district each packed house last stood in), or -1 to build a new house. The packed house from the same
    // district is unpacked; when the account may not have another house, any packed house is.
    static int32 ChoosePackedHouseToUnpack(std::vector<int32> const& packedHouseWorldMapIds, int32 districtWorldMapId, bool atHouseCap);

    // Exterior decor is stored in world coordinates, so a house that changes plot has to take it along. This moves one
    // piece so that it keeps its place and turn relative to the plot: fromPlot and toPlot are each plot's house
    // position and facing (HousingMgr::GetPlotHouseFrame).
    static void MoveDecorBetweenPlots(Position const& fromPlot, Position const& toPlot, PlacedDecor& decor);

    // One house of an account as the shared states hold it.
    struct AccountHouse
    {
        uint64 DatabaseId = 0;
        uint8 Slot = 0;
        ObjectGuid HouseGuid;
        ObjectGuid NeighborhoodGuid;
        // For a packed house, the neighborhood it last stood in; its district decides where it is unpacked.
        ObjectGuid FormerNeighborhoodGuid;
        bool Packed = false;
    };

    // Every house of a Battle.net account that is loaded and not deleted. Every online character of the account
    // holds all of its houses: they are loaded at login and handed to the others when one is bought. So while a
    // character of the account is online this is the account's whole set, also when another game account of the
    // same Battle.net account bought a house after this character logged in.
    static std::vector<AccountHouse> GetAccountHouses(uint32 bnetAccountId);

    uint64 GetDatabaseId() const { return _state->DatabaseId; }
    uint32 GetOwnerAccountId() const { return _state->OwnerAccountId; }
    uint8 GetSlot() const { return _state->Slot; }
    // True when the character's Battle.net account owns this house, which is what lets any of its characters
    // enter it as owner and edit it.
    bool IsOwnedBy(Player const* player) const;
    bool IsDeleted() const { return _state->Deleted; }
    bool IsPacked() const { return _state->Packed; }
    // The copper paid for the house, which is what relinquishing it pays back. 0 for a free first house.
    uint64 GetRefundAmount() const { return _state->RefundAmount; }
    // For a packed house, the neighborhood it stood in before it was packed.
    ObjectGuid GetFormerNeighborhoodGuid() const { return _state->FormerNeighborhoodGuid; }

    // Global DB ID generators — must be called once during server startup
    // before any Housing objects are loaded, to prevent cross-player ID collisions.
    static void InitializeDbIdGenerators();

    // house is the house's row from CHAR_SEL_ACCOUNT_HOUSING; decor, rooms and fixtures are that house's rows from
    // the account-wide login queries, already split by house; catalog is the account's catalog.
    bool LoadFromDB(Field* house, std::vector<Field*> const& decor, std::vector<Field*> const& rooms,
        std::vector<Field*> const& fixtures, PreparedQueryResult catalog);
    // For a character of the account whose house another online character already holds: takes that house's
    // loaded state. False when no character holds it loaded, or it has been deleted.
    bool JoinLoadedState();
    void SaveToDB(CharacterDatabaseTransaction trans);
    static void DeleteFromDB(ObjectGuid::LowType houseDatabaseId, CharacterDatabaseTransaction trans);

    // Builds a new house in memory only. The purchase saves it with SaveToDB in the same transaction as the plot
    // claim and the money, so a crash cannot leave one without the others.
    HousingResult Create(ObjectGuid neighborhoodGuid, uint8 plotIndex, uint64 refundAmount);
    // Deletes the house's rows and frees its plot in one transaction.
    void Delete();
    // Relinquishing packs a house instead of deleting it: rooms, decor, fixtures, level and favor are kept, the
    // house leaves its plot, and the next purchase unpacks it. Pack appends the row change to trans; Unpack only
    // changes memory, and the purchase saves the whole house in its transaction.
    void Pack(CharacterDatabaseTransaction trans);
    HousingResult Unpack(ObjectGuid neighborhoodGuid, uint8 plotIndex, uint64 refundAmount);
    // Appends the house row's neighborhood, plot and packed flag, for a move.
    void SavePlacement(CharacterDatabaseTransaction trans);
    // Moves the placed exterior decor from one plot to another (MoveDecorBetweenPlots). Each piece's new position is
    // appended to trans when one is given; an unpacked house is saved whole by the purchase instead.
    void MoveExteriorDecorBetweenPlots(Position const& fromPlot, Position const& toPlot, CharacterDatabaseTransaction trans);

    // Getters
    Player* GetOwner() const { return _owner; }
    ObjectGuid GetHouseGuid() const { return _state->HouseGuid; }
    ObjectGuid GetNeighborhoodGuid() const { return _state->NeighborhoodGuid; }
    void SetNeighborhoodGuid(ObjectGuid guid);
    ObjectGuid GetPlotGuid() const;
    uint8 GetPlotIndex() const { return _state->PlotIndex; }
    void SetPlotIndex(uint8 plotIndex);
    uint32 GetCreateTime() const { return _state->CreateTime; }
    uint32 GetLevel() const { return _state->Level; }
    uint32 GetFavor() const { return _state->Favor; }
    uint32 GetSettingsFlags() const { return _state->SettingsFlags; }
    // The character shown as the house's owner: the buyer until House Settings names another character of the
    // same account (hf1 1075094-1075101: a second character of the account is sent the buyer as CosmeticOwner).
    ObjectGuid GetCosmeticOwnerGuid() const { return _state->CosmeticOwnerGuid; }
    void SetCosmeticOwnerGuid(ObjectGuid guid);
    // The house as the houses info, current house info and buy replies list it: GUID, cosmetic owner, neighborhood,
    // plot and house setting flags.
    void FillHouseEntry(WorldPackets::Housing::JamCliHouse& house) const;

    // Editor mode
    void SetEditorMode(HousingEditorMode mode);
    HousingEditorMode GetEditorMode() const { return _editorMode; }

    // Interior state tracking (set by door script, cleared on leave)
    void SetInInterior(bool interior) { _isInInterior = interior; }
    bool IsInInterior() const { return _isInInterior; }

    // Decor operations — StartPlacingNewDecor creates a pending placement, PlaceDecorWithGuid commits it
    // Mint a decor GUID from the global generator. Every decor GUID must come
    // from here or from StartPlacingNewDecor/PlaceDecor, all of which draw the
    // counter from s_nextDecorDbId, so ids stay unique across players and across
    // a reload. The deferred-redeem path used to compute its own from
    // (playerGuid * 100000 + entryId * 100 + index), which overflows its band
    // into another character's range as soon as the entry id passes 999.
    ObjectGuid GenerateDecorGuid(uint32 decorEntryId);
    ObjectGuid StartPlacingNewDecor(uint32 catalogEntryId, HousingResult& result);
    uint32 GetPendingPlacementEntryId(ObjectGuid decorGuid) const;
    void CancelPendingPlacement(ObjectGuid decorGuid);
    HousingResult PlaceDecorWithGuid(ObjectGuid decorGuid, uint32 decorEntryId, float x, float y, float z,
        float rotX, float rotY, float rotZ, float rotW, ObjectGuid roomGuid);
    HousingResult PlaceDecor(uint32 decorEntryId, float x, float y, float z,
        float rotX, float rotY, float rotZ, float rotW, ObjectGuid roomGuid);
    HousingResult MoveDecor(ObjectGuid decorGuid, float x, float y, float z,
        float rotX, float rotY, float rotZ, float rotW, float scale = 1.0f);
    HousingResult RemoveDecor(ObjectGuid decorGuid);
    // M2: single source of truth for exterior-vs-interior decor budget routing.
    // A placement is exterior (charged to the yard budget) when it has no room
    // (empty RoomGuid) OR its RoomGuid is the plot's base/exterior room identity
    // (HighGuid::Housing subType==2 whose low arg2 == base room entry id). Every
    // budget CHECK, CHARGE, refund and RecalculateBudgets classifies through this
    // so they can never disagree (the old bug: CHECK counted exterior-plot rooms
    // as exterior but CHARGE routed them to interior → exterior budget unlimited).
    static bool IsExteriorDecorPlacement(ObjectGuid roomGuid);
    HousingResult CommitDecorDyes(ObjectGuid decorGuid, std::array<uint32, MAX_HOUSING_DYE_SLOTS> const& dyeSlots);
    HousingResult SetDecorLocked(ObjectGuid decorGuid, bool locked);
    // Bind (or, with an empty petGuid, clear) a battle pet on a placed decor slot.
    HousingResult SetDecorPet(ObjectGuid decorGuid, ObjectGuid petGuid, uint8 petFlag);
    // Wipe placed decor for a HousingHouseScope (1=Interior, 2=Exterior). Returns the count removed.
    HousingResult ResetDecor(uint8 scope, uint32* outRemoved = nullptr);
    PlacedDecor const* GetPlacedDecor(ObjectGuid decorGuid) const;
    std::vector<PlacedDecor const*> GetAllPlacedDecor() const;
    uint32 GetDecorCount() const { return static_cast<uint32>(_state->PlacedDecorByGuid.size()); }

    // Auto-place starter decor in the visual room (called after catalog is populated).
    // Sniff-verified: retail pre-places starter items at fixed positions in Room 1.
    // The "Welcome Home" quest requires the player to remove 3 of these items.
    uint32 PlaceStarterDecor();

    // Room operations
    HousingResult PlaceRoom(uint32 roomEntryId, uint32 slotIndex, uint32 orientation, bool mirrored, ObjectGuid* outRoomGuid = nullptr, int32 gridX = 0, int32 gridY = 0, int32 floorIndex = 0);
    HousingResult RemoveRoom(ObjectGuid roomGuid);
    HousingResult RotateRoom(ObjectGuid roomGuid, bool clockwise);
    HousingResult MoveRoom(ObjectGuid roomGuid, uint32 newSlotIndex, ObjectGuid swapRoomGuid, uint32 swapSlotIndex);
    HousingResult ApplyRoomTheme(ObjectGuid roomGuid, uint32 themeSetId, std::vector<uint32> const& optionIds);
    HousingResult ApplyRoomMaterial(ObjectGuid roomGuid, uint32 textureId, int32 colorOverride, std::vector<uint32> const& optionIds);
    HousingResult SetDoorType(ObjectGuid roomGuid, uint32 doorTypeId, uint8 doorSlot);
    HousingResult SetCeilingType(ObjectGuid roomGuid, uint32 ceilingTypeId, uint8 ceilingSlot);
    std::vector<Room const*> GetRooms() const;
    Room const* GetRoom(ObjectGuid roomGuid) const;
    ObjectGuid GetBaseRoomGuid() const { return FindBaseRoomGuid(); }
    uint32 GetNextRoomSlotIndex() const;
    // Blueprint imports: drop every room but the base room (their decor must already be removed), copy a room's theme,
    // material, door and ceiling choices, replace every fixture choice.
    void RemoveAllNonBaseRooms();
    void SetRoomAppearance(ObjectGuid roomGuid, Room const& appearance);
    void ReplaceFixtures(std::vector<Fixture> const& fixtures);
    std::unordered_map<ObjectGuid, Room> const& GetRoomsMap() const { return _state->Rooms; }

    // Fixture operations
    HousingResult SelectFixtureOption(uint32 fixturePointId, uint32 optionId, std::vector<uint32>* removedHookIDs = nullptr);
    HousingResult RemoveFixture(uint32 componentID, uint32* outHookID = nullptr);
    std::vector<Fixture const*> GetFixtures() const;
    std::unordered_map<uint32, uint32> GetFixtureOverrideMap() const;
    uint32 GetCoreExteriorComponentID() const;
    std::unordered_map<uint8, uint32> GetRootComponentOverrides() const;

    // Catalog operations
    HousingResult AddToCatalog(uint32 decorEntryId, uint8 sourceType = DECOR_SOURCE_STANDARD, std::string sourceValue = {});
    HousingResult RemoveFromCatalog(uint32 decorEntryId);
    HousingResult DestroyAllCopies(uint32 decorEntryId);
    // Copies, taken under the catalog's lock: spells and initiative rewards add to the account's catalog from the
    // map a character is on, and two characters of the account can be on maps updated at the same time.
    std::vector<CatalogEntry> GetCatalogEntries() const;

    // House level and favor
    void AddLevel(uint32 amount);
    void AddFavor(uint64 amount, HousingFavorUpdateSource source = HOUSING_FAVOR_SOURCE_UNKNOWN, bool emitUpdate = true);
    uint64 GetFavor64() const { return _state->Favor64; }
    uint32 GetMaxDecorCount() const;

    // Budget tracking (WeightCost-based)
    uint32 GetInteriorDecorWeightUsed() const { return _state->InteriorDecorWeightUsed; }
    uint32 GetExteriorDecorWeightUsed() const { return _state->ExteriorDecorWeightUsed; }
    uint32 GetRoomWeightUsed() const { return _state->RoomWeightUsed; }
    uint32 GetFixtureWeightUsed() const { return _state->FixtureWeightUsed; }
    uint32 GetMaxInteriorDecorBudget() const;
    uint32 GetMaxExteriorDecorBudget() const;
    uint32 GetMaxRoomBudget() const;
    uint32 GetMaxFixtureBudget() const;
    void RecalculateBudgets();

    // Level progression (QuestID-based)
    void OnQuestCompleted(uint32 questId);

    // UpdateField synchronization
    void SyncUpdateFields();

    // Settings
    void SaveSettings(uint32 settingsFlags);

    // House name and description
    // Copies, taken under the house's lock: a character on another map may rename the house at the same time.
    std::string GetHouseName() const;
    std::string GetHouseDescription() const;
    void SetHouseNameDescription(std::string const& name, std::string const& desc);

    // Exterior lock state
    void SetExteriorLocked(bool locked);
    bool IsExteriorLocked() const { return _state->ExteriorLocked; }

    // Photo sharing authorization (per-session, volatile)
    void SetPhotoSharingAuthorized(bool authorized) { _photoSharingAuthorized = authorized; }
    bool IsPhotoSharingAuthorized() const { return _photoSharingAuthorized; }

    // House size (HousingFixtureSize enum)
    void SetHouseSize(uint8 size);
    uint8 GetHouseSize() const { return _state->HouseSize; }

    // House type (HouseExteriorWmoData ID)
    void SetHouseType(uint32 typeId);
    uint32 GetHouseType() const { return _state->HouseType; }

    // House position persistence (player can reposition house on plot)
    bool HasCustomPosition() const { return _state->HasCustomPosition; }
    Position GetHousePosition() const { return Position(_state->HousePosX, _state->HousePosY, _state->HousePosZ, _state->HouseFacing); }
    void SetHousePosition(float x, float y, float z, float facing);

    // Direct access to placed decor map (for GO spawning)
    std::unordered_map<ObjectGuid, PlacedDecor> const& GetPlacedDecorMap() const { return _state->PlacedDecorByGuid; }
    bool IsStoragePopulated() const { return _storagePopulated; }
    void ResetStoragePopulated() { _storagePopulated = false; }

    // Populate ALL decor entries (placed + catalog) into the Account entity's FHousingStorage_C.
    // Called on-demand by REQUEST_STORAGE handler. Retail does NOT populate storage at login —
    // FHousingStorage_C is only sent when the player enters edit mode or requests storage.
    void PopulateCatalogStorageEntries();

    // Build the per-character HousingCatalog ownership snapshot broadcast via
    // SMSG_HOUSING_CATALOG_STATE_SYNC on every housing-map entry. Aggregates across:
    //   - Placed decor instances -> OwnedModifiedStack entries keyed by DecorEntryId
    //   - Storage-only stacks (catalog.Count minus placed count) -> OwnedUnmodifiedStack
    //   - Placed rooms                                           -> Room entries (isRoom=1)
    // PackedState bit layout matches the IDA-decoded format for ClientMirrorSystem 0x56000E:
    //   bits 0-1 subtype | bit 3 isRoom | bit 4 catalog-visible flag (always set)
    void BuildCatalogStateSync(WorldPackets::Housing::HousingCatalogStateSync& packet) const;

private:
    uint64 GenerateDecorDbId();
    uint64 GenerateRoomDbId();

    // Room connectivity helpers
    ObjectGuid FindBaseRoomGuid() const;
    bool IsRoomGraphConnectedWithout(ObjectGuid excludeRoomGuid) const;

    // Immediate DB persistence helpers
    void PersistRoomToDB(ObjectGuid roomGuid, Room const& room);
    void PersistFixtureToDB(uint32 fixturePointId, uint32 optionId);

    // Populate starter fixtures (Base + Roof) on house creation. persistNow writes each one at once, for a house
    // loaded without them; a new house is saved whole by the purchase instead.
    void PopulateStarterFixtures(bool persistNow);

    // #16 Outdoor Lighting (A4): enforce the 12.0.7 "two lights cannot overlap"
    // rule. Only applies when placing/moving a Lighting-category decor on the
    // exterior/plot scope; rejects with HOUSING_RESULT_INVALID_LIGHT_OVERLAP if
    // another exterior light sits within HOUSING_LIGHT_OVERLAP_RADIUS. excludeGuid
    // skips the decor being moved so an in-place move never collides with itself.
    HousingResult CheckLightOverlap(uint32 decorEntryId, float x, float y, float z,
        bool isExterior, ObjectGuid excludeGuid = ObjectGuid::Empty) const;

    // What is stored for one house. Two game accounts of one Battle.net account can be online at the same time on
    // different maps, whose updates run on different threads, so every change and every save takes Lock.
    // The decor, room and fixture maps, and the pointers and references the getters above hand out into them, are
    // read without the lock. That holds because only the housing packet handlers change them (directly or through
    // the blueprint code), and those all run on the world thread, which never runs while maps update; map code only
    // reads them. A change made from map code (a spell, a quest, an initiative reward) may only touch the level,
    // the favor and the catalog, which it does under the lock.
    struct PersistentState
    {
        std::recursive_mutex Lock;
        uint64 DatabaseId = 0;
        uint32 OwnerAccountId = 0;
        uint8 Slot = 0;
        bool Loaded = false;
        bool Deleted = false;
        bool Packed = false;
        ObjectGuid HouseGuid;
        ObjectGuid NeighborhoodGuid;
        ObjectGuid FormerNeighborhoodGuid;
        uint64 RefundAmount = 0;
        uint8 PlotIndex = INVALID_PLOT_INDEX;
        // For a packed house, the plot it last stood on, so that unpacking can move its exterior decor to the new plot.
        uint8 FormerPlotIndex = INVALID_PLOT_INDEX;
        uint32 Level = 1;
        uint32 Favor = 0;
        uint64 Favor64 = 0;
        uint32 SettingsFlags = HOUSE_SETTING_DEFAULT;
        bool ExteriorLocked = false;
        uint8 HouseSize = HOUSING_FIXTURE_SIZE_SMALL;
        uint32 HouseType = 0;
        uint32 CreateTime = 0;
        std::string HouseName;
        std::string HouseDescription;
        float HousePosX = 0.0f;
        float HousePosY = 0.0f;
        float HousePosZ = 0.0f;
        float HouseFacing = 0.0f;
        ObjectGuid CosmeticOwnerGuid;
        bool HasCustomPosition = false;
        uint32 InteriorDecorWeightUsed = 0;
        uint32 ExteriorDecorWeightUsed = 0;
        uint32 RoomWeightUsed = 0;
        uint32 FixtureWeightUsed = 0;
        std::unordered_map<ObjectGuid, PlacedDecor> PlacedDecorByGuid;
        std::unordered_map<ObjectGuid, Room> Rooms;
        std::unordered_map<uint32 /*fixturePointId*/, Fixture> Fixtures;
    };

    // The decor catalog of one Battle.net account, shared by all of its houses and characters.
    struct AccountCatalog
    {
        std::recursive_mutex Lock;
        bool Loaded = false;
        std::unordered_map<uint32 /*decorEntryId*/, CatalogEntry> Entries;
    };

    using StateLock = std::unique_lock<std::recursive_mutex>;
    StateLock LockState() const { return StateLock(_state->Lock); }
    // Always takes the house before the catalog, so two threads never wait on each other.
    std::scoped_lock<std::recursive_mutex, std::recursive_mutex> LockStateAndCatalog() const
    {
        return std::scoped_lock<std::recursive_mutex, std::recursive_mutex>(_state->Lock, _catalog->Lock);
    }

    Player* _owner;
    std::shared_ptr<PersistentState> _state;
    std::shared_ptr<AccountCatalog> _catalog;

    // Per character: each character that has the house open has its own editor state.
    HousingEditorMode _editorMode = HOUSING_EDITOR_MODE_NONE;
    bool _isInInterior = false;
    bool _storagePopulated = false; // True after PopulateCatalogStorageEntries() — gates Account entity updates
    bool _photoSharingAuthorized = false; // Per-session photo sharing authorization state
    std::unordered_map<ObjectGuid, uint32 /*decorEntryId*/> _pendingPlacements;

    // Shared states by house database id and catalogs by Battle.net account. An entry lives as long as one Housing
    // object holds it; the last one to go erases it, so a later login reads the saved rows again.
    static std::mutex s_sharedStateLock;
    static std::unordered_map<uint64, std::weak_ptr<PersistentState>> s_houseStates;
    static std::unordered_map<uint32, std::weak_ptr<AccountCatalog>> s_accountCatalogs;

    // Global DB ID generators (atomic, shared across all Housing instances)
    static std::atomic<uint64> s_nextHouseDbId;
    static std::atomic<uint64> s_nextDecorDbId;
    static std::atomic<uint64> s_nextRoomDbId;
};

#endif // Housing_h__

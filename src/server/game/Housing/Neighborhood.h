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

#ifndef TRINITYCORE_NEIGHBORHOOD_H
#define TRINITYCORE_NEIGHBORHOOD_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "Housing.h"
#include "HousingDefines.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class WorldPacket;

namespace WorldPackets::Neighborhood
{
    class NeighborhoodGetRosterResponse;
}

namespace WorldPackets::Housing
{
    struct JamCliHouse;
}

class TC_GAME_API Neighborhood
{
public:
    struct Member
    {
        ObjectGuid PlayerGuid;
        ObjectGuid HouseGuid;
        uint8 Role = 0;        // NeighborhoodMemberRole
        uint32 JoinTime = 0;
        uint8 PlotIndex = INVALID_PLOT_INDEX;
        uint8 StatusFlags = 0;
    };

    struct PlotInfo
    {
        uint8 PlotIndex = INVALID_PLOT_INDEX;
        ObjectGuid PlotGuid;
        // The character shown as the house's owner (its cosmetic owner). Ownership itself is OwnerBnetGuid.
        ObjectGuid OwnerGuid;
        ObjectGuid HouseGuid;
        ObjectGuid OwnerBnetGuid;
        // character_housing.guid of the house on this plot, 0 until the house row exists.
        uint64 HouseDatabaseId = 0;

        // A house belongs to a Battle.net account, so every character of that account owns this plot.
        bool IsOwnedByAccount(ObjectGuid bnetAccountGuid) const { return IsOccupied() && !bnetAccountGuid.IsEmpty() && OwnerBnetGuid == bnetAccountGuid; }

        // Mirrored from character_housing for tooltip display of OTHER players' houses
        // on the neighborhood map (hover info). Set by Neighborhood::LoadFromDB; refreshed
        // on ownership / level / favor changes.
        uint8 HouseLevel = 1;
        uint64 HouseFavor = 0;
        std::string HouseName;

        // Mirrored from character_housing so the neighborhood map can spawn the
        // correct WMO geometry for EVERY occupied plot at preload. The data is
        // always in the DB regardless of whether the owner is currently online —
        // PlotInfo carries enough of it to build the exterior visual for every
        // plot at map init without needing a live Housing object.
        uint32 HouseType = 0;

        // Mirrored from character_housing_fixtures. Key = FixturePointId (DB2
        // ExteriorComponentHook slot), value = FixtureOptionId (DB2
        // ExteriorComponent override). Drives the correct roof/doors/windows
        // on every plot's exterior, not just the logged-in owner's.
        std::unordered_map<uint32, uint32> Fixtures;

        // Mirrored from character_housing_decor. Every placed decor item
        // (exterior AND interior) for this plot's owner. HousingMap uses the
        // exterior entries (RoomGuid.IsEmpty()) at preload so visitors see
        // neighbours' placed decor even when the owner is offline. Interior
        // entries are reused when a visitor opens the owner's interior map.
        std::vector<Housing::PlacedDecor> Decor;

        // Mirrored from character_housing_rooms. Interior-room layout for the
        // plot owner. Used by HouseInteriorMap to spawn the owner's actual
        // rooms when a visitor enters their house — independent of whether
        // the owner is currently online.
        std::vector<Housing::Room> Rooms;

        // Mirrored from character_housing.settingsFlags so visitor permission
        // checks (CanVisitorAccess) work when the plot owner is offline.
        // Refreshed when an online owner mutates their Housing settings.
        uint32 HouseSettingsFlags = 0;

        // The house's placement inside the plot's room (character_housing posX, posY, posZ and facing), so a house
        // its owner moved stands where she put it while no character of her account is on the map.
        bool HasHousePlacement = false;
        Position HousePlacement;

        bool IsOccupied() const { return PlotIndex != INVALID_PLOT_INDEX; }
    };

    struct PendingInvite
    {
        ObjectGuid InviteeGuid;
        ObjectGuid InviterGuid;
        uint32 InviteTime = 0;
    };

    struct PendingOwnershipTransfer
    {
        ObjectGuid TargetGuid;
        uint32 OfferTime = 0;
    };

    // A plot taken for a purchase in memory, before the purchase's transaction is committed.
    struct PlotClaim
    {
        ObjectGuid PlayerGuid;
        uint8 PlotIndex = INVALID_PLOT_INDEX;
        // The buyer was not on the roster and joins it as a resident with this purchase.
        bool NewMember = false;
        uint32 JoinTime = 0;
    };

    explicit Neighborhood(ObjectGuid guid);

    // DB persistence
    bool LoadFromDB(PreparedQueryResult neighborhood, PreparedQueryResult members, PreparedQueryResult invites,
        PreparedQueryResult houses = nullptr, PreparedQueryResult memberFixtures = nullptr, PreparedQueryResult memberDecor = nullptr,
        PreparedQueryResult memberRooms = nullptr);
    void SaveToDB(CharacterDatabaseTransaction trans);
    static void DeleteFromDB(ObjectGuid::LowType guid, CharacterDatabaseTransaction trans);

    // Accessors
    ObjectGuid GetGuid() const { return _guid; }
    std::string const& GetName() const { return _name; }
    uint32 GetNeighborhoodMapID() const { return _neighborhoodMapID; }
    ObjectGuid GetOwnerGuid() const { return _ownerGuid; }
    int32 GetFactionRestriction() const { return _factionRestriction; }
    void SetFactionRestriction(int32 faction) { _factionRestriction = faction; }
    bool IsPublic() const { return _isPublic; }
    uint32 GetCreateTime() const { return _createTime; }

    // Guild association
    uint32 GetGuildId() const { return _guildId; }
    void SetGuildId(uint32 guildId) { _guildId = guildId; }

    // Plot reservations (in-memory, time-limited holds before purchase)
    bool ReservePlot(ObjectGuid playerGuid, uint8 plotIndex);
    bool ClearReservation(ObjectGuid playerGuid);
    bool HasReservation(ObjectGuid playerGuid) const;
    uint8 GetReservedPlot(ObjectGuid playerGuid) const;
    // Returns the reserver's GUID if `plotIndex` is currently locked by another
    // player, or ObjectGuid::Empty when the plot is free / locked by `viewerGuid`.
    // Sweeps expired reservations as a side-effect (same 5-minute window as ReservePlot).
    ObjectGuid GetPlotReserverOther(uint8 plotIndex, ObjectGuid viewerGuid);

    // Management
    void SetName(std::string const& name);
    void SetPublic(bool isPublic);
    HousingResult AddManager(ObjectGuid playerGuid);
    HousingResult RemoveManager(ObjectGuid playerGuid);
    HousingResult InviteResident(ObjectGuid inviterGuid, ObjectGuid inviteeGuid);
    HousingResult CancelInvitation(ObjectGuid inviteeGuid);
    HousingResult AcceptInvitation(ObjectGuid playerGuid);
    HousingResult DeclineInvitation(ObjectGuid playerGuid);
    HousingResult EvictPlayer(ObjectGuid plotGuid);
    HousingResult TransferOwnership(ObjectGuid newOwnerGuid);
    HousingResult OfferOwnership(ObjectGuid targetGuid);
    HousingResult AcceptOwnershipTransfer(ObjectGuid acceptorGuid);
    HousingResult RejectOwnershipTransfer(ObjectGuid rejectorGuid);
    bool HasPendingTransfer() const { return _pendingTransfer.has_value(); }
    Optional<PendingOwnershipTransfer> const& GetPendingTransfer() const { return _pendingTransfer; }

    // Plot management
    // A purchase takes its plot in three steps, so the plot, the money and the house are saved as one unit.
    // TryReservePlot checks the plot and takes it in memory (joining the buyer to the roster when joinAsResident is
    // set and she is not on it yet); AppendPlotClaim adds the roster rows to the purchase's transaction; UndoPlotClaim
    // gives the plot back when the purchase fails before it is committed. After the commit, CompletePlotClaim drops
    // the invite the buyer joined with.
    HousingResult TryReservePlot(ObjectGuid playerGuid, uint8 plotIndex, bool joinAsResident, PlotClaim& claim);
    void AppendPlotClaim(PlotClaim const& claim, CharacterDatabaseTransaction trans) const;
    void UndoPlotClaim(PlotClaim const& claim);
    void CompletePlotClaim(PlotClaim const& claim);
    void UpdatePlotHouseInfo(uint8 plotIndex, ObjectGuid houseGuid, ObjectGuid ownerBnetGuid, uint64 houseDatabaseId = 0);
    // Copies what neighbours see of a house onto its plot (owner shown, level, favor, name, type, settings, fixtures,
    // rooms and decor), for a house that was just bought or unpacked onto it.
    void UpdatePlotHouseMirror(Housing const& housing);
    // Keep the plot's copy of a house's settings and shown owner current, found by the house, not by a character.
    void UpdatePlotSettingsFlagsByHouse(ObjectGuid houseGuid, uint32 settingsFlags);
    void UpdatePlotCosmeticOwnerByHouse(ObjectGuid houseGuid, ObjectGuid cosmeticOwnerGuid);
    void UpdatePlotHousePlacementByHouse(ObjectGuid houseGuid, Position const& placement);
    // Moves a house to another vacant plot of this neighborhood. The roster entry that held the old plot is
    // updated in trans; moverGuid is the character asking, whose House Finder hold on the plot is allowed.
    HousingResult MoveHouse(ObjectGuid houseGuid, ObjectGuid moverGuid, uint8 newPlotIndex, CharacterDatabaseTransaction trans);
    void SetPlotAreaTriggerGuid(uint8 plotIndex, ObjectGuid atGuid);
    // Frees the plot a house stands on, found by the house rather than by a character, so it is vacant and can be
    // bought again at once. The roster entry that held the plot leaves the roster, except the neighborhood's owner,
    // who only loses the plot. Its rows change in trans. Returns the character whose roster entry held the plot.
    ObjectGuid ReleasePlotByHouse(ObjectGuid houseGuid, CharacterDatabaseTransaction trans);

    PlotInfo const* GetPlotInfo(uint8 plotIndex) const
    {
        return (plotIndex < MAX_NEIGHBORHOOD_PLOTS && _plots[plotIndex].IsOccupied())
            ? &_plots[plotIndex] : nullptr;
    }

    PlotInfo const* GetPlotInfoByHouse(ObjectGuid houseGuid) const;

    // How a character may enter the plot or the house standing on it. Any character of the house's Battle.net account
    // enters as owner; anyone else is a visitor, checked against the house's settings.
    struct HouseEntry
    {
        bool Allowed = false;
        bool IsOwner = false;
        ObjectGuid HouseGuid;
        uint32 SettingsFlags = 0;
    };
    HouseEntry CheckHouseEntry(Player const* player, uint8 plotIndex, bool interior) const;
    // The house's current settings: from a live Housing of its account when one is online, else the plot's copy.
    uint32 GetHouseSettingsFlags(PlotInfo const& plot) const;
    // The house standing on a plot as the neighborhood lists list it, for owners who may be offline.
    void FillPlotHouseEntry(PlotInfo const& plot, WorldPackets::Housing::JamCliHouse& house) const;

    std::array<PlotInfo, MAX_NEIGHBORHOOD_PLOTS> const& GetPlots() const { return _plots; }
    uint32 GetOccupiedPlotCount() const;

    // Members
    HousingResult AddResident(ObjectGuid playerGuid);
    Member const* GetMember(ObjectGuid playerGuid) const;
    std::vector<Member> const& GetMembers() const { return _members; }
    bool IsMember(ObjectGuid playerGuid) const;
    bool IsManager(ObjectGuid playerGuid) const;
    bool IsOwner(ObjectGuid playerGuid) const;
    uint32 GetMemberCount() const { return static_cast<uint32>(_members.size()); }

    // Invites
    bool HasPendingInvite(ObjectGuid playerGuid) const;
    std::vector<PendingInvite> const& GetPendingInvites() const { return _pendingInvites; }

    // Broadcast
    void BroadcastPacket(WorldPacket const* packet, ObjectGuid excludeGuid = ObjectGuid::Empty) const;
    // The roster as SMSG_NEIGHBORHOOD_GET_ROSTER_RESPONSE carries it.
    void BuildRosterResponse(WorldPackets::Neighborhood::NeighborhoodGetRosterResponse& response) const;
    // A member joined, left or moved house: every online member's bulletin board needs the whole roster again (the status
    // update can only change members the client already lists).
    void BroadcastRoster(ObjectGuid excludeGuid = ObjectGuid::Empty) const;
    // A member's resident type or online state changed (SMSG_NEIGHBORHOOD_ROSTER_RESIDENT_UPDATE).
    void BroadcastMemberStatus(ObjectGuid playerGuid, bool isOnline) const;
    void BroadcastMemberStatus(ObjectGuid playerGuid) const;

    // Rebuild NeighborhoodMirrorData on every online member's Account entity.
    // Call after any mutation to name, owner, managers, or houses.
    void RefreshMirrorDataForOnlineMembers() const;

private:
    ObjectGuid _guid;
    std::string _name;
    uint32 _neighborhoodMapID = 0;
    ObjectGuid _ownerGuid;
    int32 _factionRestriction = 0;
    bool _isPublic = false;
    uint32 _createTime = 0;
    uint32 _guildId = 0;

    std::vector<Member> _members;
    std::array<PlotInfo, MAX_NEIGHBORHOOD_PLOTS> _plots{};
    std::vector<PendingInvite> _pendingInvites;
    Optional<PendingOwnershipTransfer> _pendingTransfer;

    // In-memory plot reservations: playerGuid -> {plotIndex, reserveTime}
    struct PlotReservation
    {
        uint8 PlotIndex = 0;
        uint32 ReserveTime = 0;
    };
    std::unordered_map<ObjectGuid, PlotReservation> _plotReservations;
};

#endif // TRINITYCORE_NEIGHBORHOOD_H

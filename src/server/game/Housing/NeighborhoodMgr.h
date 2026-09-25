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

#ifndef TRINITYCORE_NEIGHBORHOOD_MGR_H
#define TRINITYCORE_NEIGHBORHOOD_MGR_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Neighborhood;
class Player;

class TC_GAME_API NeighborhoodMgr
{
public:
    static NeighborhoodMgr& Instance();

    NeighborhoodMgr(NeighborhoodMgr const&) = delete;
    NeighborhoodMgr(NeighborhoodMgr&&) = delete;
    NeighborhoodMgr& operator=(NeighborhoodMgr const&) = delete;
    NeighborhoodMgr& operator=(NeighborhoodMgr&&) = delete;

    void Initialize();
    void LoadFromDB();
    void Update(uint32 diff);

    // Neighborhood lifecycle
    Neighborhood* CreateNeighborhood(ObjectGuid ownerGuid, std::string const& name, uint32 neighborhoodMapID, int32 factionRestriction, bool isPublic = false, uint32 guildId = 0);
    Neighborhood* CreateGuildNeighborhood(ObjectGuid ownerGuid, std::string const& name, uint32 neighborhoodMapID, uint32 factionID, uint32 guildId);
    void DeleteNeighborhood(ObjectGuid neighborhoodGuid);
    Neighborhood* GetNeighborhood(ObjectGuid neighborhoodGuid);
    Neighborhood const* GetNeighborhood(ObjectGuid neighborhoodGuid) const;

    // Resolve a neighborhood GUID that may be in client format (GO GUID of bulletin board)
    // Falls back to the player's current HousingMap neighborhood when direct lookup fails
    Neighborhood* ResolveNeighborhood(ObjectGuid guid, Player* player);

    // Queries
    Neighborhood* GetNeighborhoodByOwner(ObjectGuid ownerGuid);
    Neighborhood* GetNeighborhoodByGuildId(uint32 guildId);
    std::vector<Neighborhood*> GetAllNeighborhoods() const;
    std::vector<Neighborhood*> GetPublicNeighborhoods() const;
    std::vector<Neighborhood*> GetNeighborhoodsForPlayer(ObjectGuid playerGuid) const;
    // The character's own neighborhood memberships plus every neighborhood where a house of its Battle.net account
    // stands: every character of the account lives in the account's houses, members of the roster or not.
    std::vector<Neighborhood*> GetNeighborhoodsForAccount(Player const* player) const;
    std::vector<Neighborhood*> GetNeighborhoodsByBnetAccount(ObjectGuid bnetAccountGuid) const;
    std::string GetNeighborhoodName(ObjectGuid neighborhoodGuid) const;
    Neighborhood* FindNeighborhoodWithPendingInvite(ObjectGuid playerGuid);

    // Find or create a public neighborhood for a faction (no membership changes)
    Neighborhood* FindOrCreatePublicNeighborhood(uint32 teamId);

    // Find a public neighborhood on the given map (for visitors, no membership change)
    Neighborhood* FindPublicNeighborhoodForMap(uint32 neighborhoodMapId) const;
    // One of the server's public neighborhoods on the map with at least one plot nobody owns, picked at random.
    Neighborhood* FindRandomServerPublicNeighborhoodWithFreePlot(uint32 neighborhoodMapId) const;

    // Resolve by the counter that is persisted in the DB (neighborhoods.guid and every FK to it).
    Neighborhood* GetNeighborhoodByCounter(uint64 counter) const;

    // Expansion
    void CheckAndExpandNeighborhoods();

    // Charter support.
    // neighborhoodMapID becomes arg1 of the GUID: the 12.0.7 client slices that 16-bit field straight out of
    // the high qword and uses it as the record ID into NeighborhoodMap.db2 (both
    // C_Housing.DoesFactionMatchNeighborhood @ RVA 0xF7C1B0 and C_Housing.GetUIMapIDForNeighborhood @ 0xF80C90
    // do `shr rax,0x20; movzx edx,ax` and look up the store at data RVA 0x486F5C0). It is NOT a realm id.
    ObjectGuid GenerateNeighborhoodGuid(uint32 neighborhoodMapID);

    // Startup guarantee
    void VerifyNeighborhoodFactions();
    void EnsurePublicNeighborhoods();

    // Regenerate names for public neighborhoods using base DB2 entry IDs
    void RegenerateNeighborhoodNames();

    // A character is being deleted (Player::DeleteFromDB). Every house she is shown as owner of passes to another
    // character of her Battle.net account who may own it where it stands, and her roster entry holding its plot goes
    // with it. With no such character a standing house is packed, as a relinquished house is, and its plot is free.
    // Rows change in trans. World thread only.
    void OnCharacterDeleted(ObjectGuid characterGuid, CharacterDatabaseTransaction trans);
    // A character left or was removed from a guild (Guild::DeleteMember, not when the guild disbands). A house she is
    // shown as owner of in one of that guild's neighborhoods is packed and its plot is free, since only a guild member
    // may own a house there. trans may be empty; the rows are then saved on their own. World thread only.
    void OnGuildMemberRemoved(uint32 guildId, ObjectGuid characterGuid, CharacterDatabaseTransaction trans);
    // A character's rows are being removed for good (Player::DeleteFromDB with the remove method, after
    // OnCharacterDeleted), and a new character can get her guid after a restart. Her charter that was never finalized
    // and its signatures are dropped, and so are her signatures on other charters, the invites naming her, and her
    // roster entries. A roster entry that makes her a neighborhood's owner stays: who takes over a neighborhood is not
    // decided. Rows change in trans. World thread only.
    void OnCharacterRemoved(ObjectGuid characterGuid, CharacterDatabaseTransaction trans);

    // Packs a house that stands on a plot of the neighborhood, when it is not given up by choice: its owner was deleted
    // or left the guild, or its plot was evicted. It works through a character of its account that is online and holds
    // it, else in its rows and on the plot directly, and puts everyone inside it out on the plot first.
    // newCosmeticOwner is who is shown as its owner afterwards when that changes, or Empty to keep the current one;
    // clearCosmeticOwner leaves nobody shown, for a house whose account has no character left. What was paid for the
    // house goes by mail to refundRecipient when that character exists, else to nobody. The account's online clients
    // reload their housing data, and the guild the house was listed in is told it is gone. World thread only.
    void PackHouseForOwnerLoss(Neighborhood* neighborhood, uint8 plotIndex, ObjectGuid houseGuid, uint64 houseDatabaseId,
        uint32 bnetAccountId, ObjectGuid newCosmeticOwner, bool clearCosmeticOwner, ObjectGuid refundRecipient,
        CharacterDatabaseTransaction trans);

private:
    NeighborhoodMgr() = default;

    std::unordered_map<ObjectGuid, std::unique_ptr<Neighborhood>> _neighborhoods;
    // Counter -> neighborhood. _neighborhoods is keyed by the FULL ObjectGuid, so now that arg1 varies per
    // neighborhood map, nothing may rebuild a lookup GUID from a persisted counter alone - the persisted tables
    // store only GetCounter(). Every such site resolves through GetNeighborhoodByCounter instead.
    std::unordered_map<uint64, Neighborhood*> _neighborhoodsByCounter;
    std::unordered_map<ObjectGuid, ObjectGuid> _ownerToNeighborhood; // owner guid -> neighborhood guid
    uint64 _nextGuid = 1;
    uint32 _expansionCheckTimer = 0;
};

#define sNeighborhoodMgr NeighborhoodMgr::Instance()

#endif // TRINITYCORE_NEIGHBORHOOD_MGR_H

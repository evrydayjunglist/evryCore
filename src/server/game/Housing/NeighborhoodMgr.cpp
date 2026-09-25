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

#include "NeighborhoodMgr.h"
#include "CharacterCache.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "HouseInteriorMap.h"
#include "HousingDefines.h"
#include "HousingMap.h"
#include "HousingMgr.h"
#include "HousingPackets.h"
#include "Log.h"
#include "Mail.h"
#include "Map.h"
#include "Neighborhood.h"
#include "NeighborhoodCharter.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RealmList.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>

NeighborhoodMgr& NeighborhoodMgr::Instance()
{
    static NeighborhoodMgr instance;
    return instance;
}

void NeighborhoodMgr::Initialize()
{
    TC_LOG_INFO("server.loading", "Initializing NeighborhoodMgr...");
    LoadFromDB();
    VerifyNeighborhoodFactions();
    EnsurePublicNeighborhoods();
    RegenerateNeighborhoodNames();
}

void NeighborhoodMgr::Update(uint32 diff)
{
    // Periodic neighborhood expansion check (every 60 seconds)
    // Ensures new instances are created even if no one is actively purchasing plots
    static constexpr uint32 EXPANSION_CHECK_INTERVAL = 60 * IN_MILLISECONDS;

    _expansionCheckTimer += diff;
    if (_expansionCheckTimer >= EXPANSION_CHECK_INTERVAL)
    {
        _expansionCheckTimer = 0;
        CheckAndExpandNeighborhoods();
    }
}

void NeighborhoodMgr::LoadFromDB()
{
    uint32 oldMSTime = getMSTime();

    _neighborhoods.clear();
    _ownerToNeighborhood.clear();

    //                                                     0     1       2                3          4                    5         6
    QueryResult result = CharacterDatabase.Query("SELECT guid, name, neighborhoodMapID, ownerGuid, factionRestriction, isPublic, createTime FROM neighborhoods ORDER BY guid ASC");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 neighborhoods. DB table `neighborhoods` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint64 guidLow = fields[0].GetUInt64();

        // Track highest guid for generation
        if (guidLow >= _nextGuid)
            _nextGuid = guidLow + 1;

        // Rebuild exactly what GenerateNeighborhoodGuid minted: arg1 = neighborhoodMapID (fields[2]).
        ObjectGuid neighborhoodGuid = ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 4, /*arg1*/ fields[2].GetUInt32(), /*arg2*/ 0, guidLow);

        auto neighborhood = std::make_unique<Neighborhood>(neighborhoodGuid);

        // Load members for this neighborhood
        CharacterDatabasePreparedStatement* memberStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_MEMBERS);
        memberStmt->setUInt64(0, guidLow);
        PreparedQueryResult memberResult = CharacterDatabase.Query(memberStmt);

        // Load invites for this neighborhood
        CharacterDatabasePreparedStatement* inviteStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_INVITES);
        inviteStmt->setUInt64(0, guidLow);
        PreparedQueryResult inviteResult = CharacterDatabase.Query(inviteStmt);

        // The houses standing in this neighborhood, which fill its plots.
        CharacterDatabasePreparedStatement* houseStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_HOUSES);
        houseStmt->setUInt64(0, guidLow);
        PreparedQueryResult houseResult = CharacterDatabase.Query(houseStmt);

        // Load per-house exterior/interior state needed to render every
        // occupied plot's house without requiring the owner to be online.
        CharacterDatabasePreparedStatement* fixStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_MEMBER_FIXTURES);
        fixStmt->setUInt64(0, guidLow);
        PreparedQueryResult fixtureResult = CharacterDatabase.Query(fixStmt);

        CharacterDatabasePreparedStatement* decorStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_MEMBER_DECOR);
        decorStmt->setUInt64(0, guidLow);
        PreparedQueryResult decorResult = CharacterDatabase.Query(decorStmt);

        CharacterDatabasePreparedStatement* roomStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_MEMBER_ROOMS);
        roomStmt->setUInt64(0, guidLow);
        PreparedQueryResult roomResult = CharacterDatabase.Query(roomStmt);

        // Wrap the neighborhood row as a PreparedQueryResult by passing the raw result
        // LoadFromDB expects PreparedQueryResult for the first param but we have the raw fields;
        // so we call LoadFromDB with the data directly already parsed from fields above.
        // Since LoadFromDB needs the PreparedQueryResult format, we use a query per neighborhood.
        CharacterDatabasePreparedStatement* neighborhoodStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD);
        neighborhoodStmt->setUInt64(0, guidLow);
        PreparedQueryResult neighborhoodResult = CharacterDatabase.Query(neighborhoodStmt);

        if (!neighborhood->LoadFromDB(neighborhoodResult, memberResult, inviteResult, houseResult, fixtureResult, decorResult, roomResult))
        {
            TC_LOG_ERROR("housing", "NeighborhoodMgr::LoadFromDB: Failed to load neighborhood guid {}. Skipping.", guidLow);
            continue;
        }

        ObjectGuid ownerGuid = neighborhood->GetOwnerGuid();
        _ownerToNeighborhood[ownerGuid] = neighborhoodGuid;
        _neighborhoods[neighborhoodGuid] = std::move(neighborhood);
        _neighborhoodsByCounter[guidLow] = _neighborhoods[neighborhoodGuid].get();
        ++count;

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} neighborhoods in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

    // Debug dump all neighborhoods and their plot states
    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        TC_LOG_DEBUG("housing", "NEIGHBORHOOD_DUMP: guid={} name='{}' mapId={} faction={} public={} "
            "members={} occupiedPlots={} owner={}",
            guid.ToString(), neighborhood->GetName(), neighborhood->GetNeighborhoodMapID(),
            neighborhood->GetFactionRestriction(), neighborhood->IsPublic(),
            neighborhood->GetMemberCount(), neighborhood->GetOccupiedPlotCount(),
            neighborhood->GetOwnerGuid().ToString());

        for (auto const& member : neighborhood->GetMembers())
        {
            TC_LOG_DEBUG("housing", "  MEMBER: player={} role={} plotIndex={} houseGuid={}",
                member.PlayerGuid.ToString(), member.Role, member.PlotIndex, member.HouseGuid.ToString());
        }

        for (uint8 i = 0; i < MAX_NEIGHBORHOOD_PLOTS; ++i)
        {
            auto const& plot = neighborhood->GetPlots()[i];
            if (plot.IsOccupied())
            {
                TC_LOG_DEBUG("housing", "  PLOT[{}]: owner={} house={} bnet={}",
                    i, plot.OwnerGuid.ToString(), plot.HouseGuid.ToString(), plot.OwnerBnetGuid.ToString());
            }
        }
    }
}

Neighborhood* NeighborhoodMgr::CreateNeighborhood(ObjectGuid ownerGuid, std::string const& name, uint32 neighborhoodMapID, int32 factionRestriction, bool isPublic /*= false*/, uint32 guildId /*= 0*/)
{
    // Check if owner already has a neighborhood
    if (_ownerToNeighborhood.find(ownerGuid) != _ownerToNeighborhood.end())
    {
        TC_LOG_DEBUG("housing", "NeighborhoodMgr::CreateNeighborhood: Owner {} already has a neighborhood",
            ownerGuid.ToString());
        return nullptr;
    }

    ObjectGuid neighborhoodGuid = GenerateNeighborhoodGuid(neighborhoodMapID);

    auto neighborhood = std::make_unique<Neighborhood>(neighborhoodGuid);

    // Set up initial state via the member data structures
    // We need to persist and then reload to go through LoadFromDB, or set internal state directly.
    // For creation, we persist first and then load.

    uint32 createTime = static_cast<uint32>(GameTime::GetGameTime());

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // Insert the neighborhood row
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD);
    uint8 index = 0;
    stmt->setUInt64(index++, neighborhoodGuid.GetCounter());
    stmt->setString(index++, name);
    stmt->setUInt32(index++, neighborhoodMapID);
    stmt->setUInt64(index++, ownerGuid.GetCounter());
    stmt->setInt32(index++, factionRestriction);
    stmt->setBool(index++, isPublic);
    stmt->setUInt32(index++, createTime);
    stmt->setUInt32(index++, guildId); // saved at creation so the reload below fills _guildId
    trans->Append(stmt);

    // Insert the owner as a member with OWNER role
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_MEMBER);
    index = 0;
    stmt->setUInt64(index++, neighborhoodGuid.GetCounter());
    stmt->setUInt64(index++, ownerGuid.GetCounter());
    stmt->setUInt8(index++, NEIGHBORHOOD_ROLE_OWNER);
    stmt->setUInt32(index++, createTime);
    stmt->setUInt8(index++, INVALID_PLOT_INDEX);
    trans->Append(stmt);

    CharacterDatabase.DirectCommitTransaction(trans);

    // Now load from DB to populate all internal structures properly
    CharacterDatabasePreparedStatement* selStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD);
    selStmt->setUInt64(0, neighborhoodGuid.GetCounter());
    PreparedQueryResult neighborhoodResult = CharacterDatabase.Query(selStmt);

    selStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_MEMBERS);
    selStmt->setUInt64(0, neighborhoodGuid.GetCounter());
    PreparedQueryResult memberResult = CharacterDatabase.Query(selStmt);

    selStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_INVITES);
    selStmt->setUInt64(0, neighborhoodGuid.GetCounter());
    PreparedQueryResult inviteResult = CharacterDatabase.Query(selStmt);

    if (!neighborhood->LoadFromDB(neighborhoodResult, memberResult, inviteResult))
    {
        TC_LOG_ERROR("housing", "NeighborhoodMgr::CreateNeighborhood: Failed to load newly created neighborhood '{}'", name);
        return nullptr;
    }

    Neighborhood* result = neighborhood.get();
    _ownerToNeighborhood[ownerGuid] = neighborhoodGuid;
    _neighborhoods[neighborhoodGuid] = std::move(neighborhood);
    _neighborhoodsByCounter[neighborhoodGuid.GetCounter()] = result;

    TC_LOG_DEBUG("housing", "NeighborhoodMgr::CreateNeighborhood: Created neighborhood '{}' (guid: {}) for owner {}",
        name, neighborhoodGuid.ToString(), ownerGuid.ToString());

    return result;
}

Neighborhood* NeighborhoodMgr::CreateGuildNeighborhood(ObjectGuid ownerGuid, std::string const& name, uint32 neighborhoodMapID, uint32 factionID, uint32 guildId)
{
    int32 factionRestriction = NEIGHBORHOOD_FACTION_NONE;
    if (factionID == HORDE)
        factionRestriction = NEIGHBORHOOD_FACTION_HORDE;
    else if (factionID == ALLIANCE)
        factionRestriction = NEIGHBORHOOD_FACTION_ALLIANCE;

    // Save the guild→neighborhood link so GetNeighborhoodByGuildId
    // resolves this neighborhood (across restarts, via LoadFromDB).
    Neighborhood* neighborhood = CreateNeighborhood(ownerGuid, name, neighborhoodMapID, factionRestriction, /*isPublic*/ false, guildId);
    if (neighborhood)
        neighborhood->SetGuildId(guildId);
    return neighborhood;
}

void NeighborhoodMgr::DeleteNeighborhood(ObjectGuid neighborhoodGuid)
{
    auto it = _neighborhoods.find(neighborhoodGuid);
    if (it == _neighborhoods.end())
    {
        TC_LOG_DEBUG("housing", "NeighborhoodMgr::DeleteNeighborhood: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    ObjectGuid ownerGuid = it->second->GetOwnerGuid();

    // Delete from DB
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    Neighborhood::DeleteFromDB(neighborhoodGuid.GetCounter(), trans);
    CharacterDatabase.CommitTransaction(trans);

    // Remove from maps
    _ownerToNeighborhood.erase(ownerGuid);
    _neighborhoodsByCounter.erase(it->first.GetCounter());
    _neighborhoods.erase(it);

    TC_LOG_DEBUG("housing", "NeighborhoodMgr::DeleteNeighborhood: Deleted neighborhood {}",
        neighborhoodGuid.ToString());
}

Neighborhood* NeighborhoodMgr::GetNeighborhood(ObjectGuid neighborhoodGuid)
{
    auto it = _neighborhoods.find(neighborhoodGuid);
    if (it != _neighborhoods.end())
        return it->second.get();

    return nullptr;
}

Neighborhood const* NeighborhoodMgr::GetNeighborhood(ObjectGuid neighborhoodGuid) const
{
    auto it = _neighborhoods.find(neighborhoodGuid);
    if (it != _neighborhoods.end())
        return it->second.get();

    return nullptr;
}

Neighborhood* NeighborhoodMgr::ResolveNeighborhood(ObjectGuid guid, Player* player)
{
    // Try direct lookup first (correct Housing GUID format)
    if (Neighborhood* neighborhood = GetNeighborhood(guid))
        return neighborhood;

    // If the GUID lookup fails (e.g., client sent a bulletin board GO GUID),
    // fall back to the player's current housing map neighborhood
    if (player)
    {
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
        {
            if (Neighborhood* neighborhood = housingMap->GetNeighborhood())
            {
                TC_LOG_DEBUG("housing", "NeighborhoodMgr::ResolveNeighborhood: Resolved client GUID {} to neighborhood '{}' via housing map fallback",
                    guid.ToString(), neighborhood->GetName());
                return neighborhood;
            }
        }
    }

    return nullptr;
}

Neighborhood* NeighborhoodMgr::GetNeighborhoodByOwner(ObjectGuid ownerGuid)
{
    auto it = _ownerToNeighborhood.find(ownerGuid);
    if (it != _ownerToNeighborhood.end())
        return GetNeighborhood(it->second);

    return nullptr;
}

Neighborhood* NeighborhoodMgr::GetNeighborhoodByGuildId(uint32 guildId)
{
    if (guildId == 0)
        return nullptr;

    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->GetGuildId() == guildId)
            return neighborhood.get();
    }
    return nullptr;
}

std::vector<Neighborhood*> NeighborhoodMgr::GetAllNeighborhoods() const
{
    std::vector<Neighborhood*> result;
    result.reserve(_neighborhoods.size());
    for (auto const& [guid, neighborhood] : _neighborhoods)
        result.push_back(neighborhood.get());
    return result;
}

std::vector<Neighborhood*> NeighborhoodMgr::GetPublicNeighborhoods() const
{
    std::vector<Neighborhood*> result;
    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->IsPublic())
            result.push_back(neighborhood.get());
    }
    return result;
}

std::vector<Neighborhood*> NeighborhoodMgr::GetNeighborhoodsForPlayer(ObjectGuid playerGuid) const
{
    std::vector<Neighborhood*> result;
    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->IsMember(playerGuid))
            result.push_back(neighborhood.get());
    }
    return result;
}

std::vector<Neighborhood*> NeighborhoodMgr::GetNeighborhoodsForAccount(Player const* player) const
{
    std::vector<Neighborhood*> result;
    if (!player)
        return result;

    ObjectGuid const bnetAccountGuid = player->GetSession() ? player->GetSession()->GetBattlenetAccountGUID() : ObjectGuid::Empty;
    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->IsMember(player->GetGUID()))
        {
            result.push_back(neighborhood.get());
            continue;
        }

        for (Neighborhood::PlotInfo const& plot : neighborhood->GetPlots())
        {
            if (plot.IsOwnedByAccount(bnetAccountGuid))
            {
                result.push_back(neighborhood.get());
                break;
            }
        }
    }
    return result;
}

std::vector<Neighborhood*> NeighborhoodMgr::GetNeighborhoodsByBnetAccount(ObjectGuid bnetAccountGuid) const
{
    std::vector<Neighborhood*> result;
    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        for (auto const& plot : neighborhood->GetPlots())
        {
            if (plot.IsOccupied() && plot.OwnerBnetGuid == bnetAccountGuid)
            {
                result.push_back(neighborhood.get());
                break; // Only add each neighborhood once
            }
        }
    }
    return result;
}

std::string NeighborhoodMgr::GetNeighborhoodName(ObjectGuid neighborhoodGuid) const
{
    Neighborhood const* neighborhood = GetNeighborhood(neighborhoodGuid);
    if (neighborhood)
        return neighborhood->GetName();

    return "";
}

Neighborhood* NeighborhoodMgr::FindNeighborhoodWithPendingInvite(ObjectGuid playerGuid)
{
    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->HasPendingInvite(playerGuid))
            return neighborhood.get();
    }
    return nullptr;
}

Neighborhood* NeighborhoodMgr::FindOrCreatePublicNeighborhood(uint32 teamId)
{
    // Determine the correct NeighborhoodMapID for the faction
    uint32 targetMapId = 0;

    for (auto const& [id, data] : sHousingMgr.GetAllNeighborhoodMapData())
    {
        int32 flags = data.Flags;
        bool isAlliance = (flags & 0x1) != 0;
        bool isHorde = (flags & 0x2) != 0;
        bool canSystemGenerate = (flags & 0x4) != 0;

        if (!canSystemGenerate)
            continue;

        if ((teamId == ALLIANCE && isAlliance) || (teamId == HORDE && isHorde))
        {
            targetMapId = id;
            break;
        }
    }

    if (targetMapId == 0)
    {
        char const* factionName = (teamId == ALLIANCE) ? "Alliance" : (teamId == HORDE) ? "Horde" : "unknown";
        uint32 wantBit = (teamId == ALLIANCE) ? 0x1 : 0x2;
        // Do NOT fabricate a map that does not exist — return nullptr, but make the
        // reason and the fix unmistakable: this is a full housing lockout for the faction.
        TC_LOG_ERROR("housing",
            "FindOrCreatePublicNeighborhood: HOUSING LOCKOUT for {} — NeighborhoodMap has no system-generatable "
            "row (Flags bit 0x4) carrying the {} flag (0x{:X}). Players of this faction cannot enter housing. "
            "Check NeighborhoodMap.db2 and the hotfixes table neighborhood_map: "
            "Alliance = ID 1 / MapID 2735 / FactionRestriction 5 (0x1|0x4), "
            "Horde = ID 2 / MapID 2736 / FactionRestriction 6 (0x2|0x4).",
            factionName, factionName, wantBit);
        return nullptr;
    }

    // Look for an existing public neighborhood — no membership changes
    Neighborhood* found = FindPublicNeighborhoodForMap(targetMapId);
    if (found)
        return found;

    // None exists yet — EnsurePublicNeighborhoods should have created them at startup.
    // Force-run it now as a fallback, then retry.
    TC_LOG_WARN("housing", "FindOrCreatePublicNeighborhood: No public neighborhood for map {}, running EnsurePublicNeighborhoods", targetMapId);
    EnsurePublicNeighborhoods();

    return FindPublicNeighborhoodForMap(targetMapId);
}

Neighborhood* NeighborhoodMgr::GetNeighborhoodByCounter(uint64 counter) const
{
    auto itr = _neighborhoodsByCounter.find(counter);
    return itr != _neighborhoodsByCounter.end() ? itr->second : nullptr;
}

Neighborhood* NeighborhoodMgr::FindPublicNeighborhoodForMap(uint32 neighborhoodMapId) const
{
    // Return the least-loaded public neighborhood on this map.
    // When multiple instances exist (after expansion), we want to distribute
    // players evenly rather than always returning the first-created instance.
    Neighborhood* best = nullptr;
    uint32 bestOccupancy = MAX_NEIGHBORHOOD_PLOTS + 1;

    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->GetNeighborhoodMapID() == neighborhoodMapId && neighborhood->IsPublic())
        {
            uint32 occupancy = neighborhood->GetOccupiedPlotCount();
            if (occupancy < bestOccupancy)
            {
                best = neighborhood.get();
                bestOccupancy = occupancy;
            }
        }
    }
    return best;
}

Neighborhood* NeighborhoodMgr::FindRandomServerPublicNeighborhoodWithFreePlot(uint32 neighborhoodMapId) const
{
    std::vector<Neighborhood*> candidates;
    for (auto const& [guid, neighborhood] : _neighborhoods)
        if (neighborhood->GetNeighborhoodMapID() == neighborhoodMapId && neighborhood->IsServerPublic()
            && neighborhood->GetOccupiedPlotCount() < MAX_NEIGHBORHOOD_PLOTS)
            candidates.push_back(neighborhood.get());

    if (candidates.empty())
        return nullptr;

    return Trinity::Containers::SelectRandomContainerElement(candidates);
}

void NeighborhoodMgr::VerifyNeighborhoodFactions()
{
    // Verify that each public neighborhood's factionRestriction matches its NeighborhoodMap's faction flags.
    // This fixes data inconsistencies from earlier code that may have assigned wrong faction values.
    // Must run BEFORE EnsurePublicNeighborhoods.

    auto const& allMaps = sHousingMgr.GetAllNeighborhoodMapData();
    uint32 fixedCount = 0;

    for (auto& [guid, nb] : _neighborhoods)
    {
        if (!nb->IsPublic())
            continue;

        uint32 mapId = nb->GetNeighborhoodMapID();
        auto it = allMaps.find(mapId);
        if (it == allMaps.end())
            continue;

        int32 mapFlags = it->second.Flags;
        bool mapIsAlliance = (mapFlags & 0x1) != 0;
        bool mapIsHorde = (mapFlags & 0x2) != 0;

        // Determine the correct faction restriction from the NeighborhoodMap flags
        int32 correctFaction = NEIGHBORHOOD_FACTION_NONE;
        if (mapIsAlliance && !mapIsHorde)
            correctFaction = NEIGHBORHOOD_FACTION_ALLIANCE;
        else if (mapIsHorde && !mapIsAlliance)
            correctFaction = NEIGHBORHOOD_FACTION_HORDE;

        if (correctFaction == NEIGHBORHOOD_FACTION_NONE)
            continue; // Ambiguous or no faction — skip

        int32 currentFaction = nb->GetFactionRestriction();
        if (currentFaction == correctFaction)
            continue; // Already correct

        TC_LOG_INFO("server.loading", ">> Fixing neighborhood '{}' (guid={}) factionRestriction: {} -> {} (based on NeighborhoodMap {} flags)",
            nb->GetName(), guid.ToString(), currentFaction, correctFaction, mapId);

        // Update in DB
        CharacterDatabase.DirectExecute(
            Trinity::StringFormat("UPDATE neighborhoods SET factionRestriction = {} WHERE guid = {}",
                correctFaction, guid.GetCounter()).c_str());

        // Update in memory
        nb->SetFactionRestriction(correctFaction);
        ++fixedCount;
    }

    if (fixedCount > 0)
        TC_LOG_INFO("server.loading", ">> Fixed factionRestriction for {} neighborhood(s)", fixedCount);
}

void NeighborhoodMgr::EnsurePublicNeighborhoods()
{
    // Ensure at least one public neighborhood exists per faction
    // This guarantees players always have a neighborhood to enter via the tutorial flow

    bool hasAlliancePublic = false;
    bool hasHordePublic = false;

    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (!neighborhood->IsPublic())
            continue;

        int32 faction = neighborhood->GetFactionRestriction();
        if (faction == NEIGHBORHOOD_FACTION_ALLIANCE)
            hasAlliancePublic = true;
        else if (faction == NEIGHBORHOOD_FACTION_HORDE)
            hasHordePublic = true;
    }

    // Find system-generatable NeighborhoodMap entries for missing factions
    for (auto const& [id, data] : sHousingMgr.GetAllNeighborhoodMapData())
    {
        int32 flags = data.Flags;
        bool isAlliance = (flags & 0x1) != 0;
        bool isHorde = (flags & 0x2) != 0;
        bool canSystemGenerate = (flags & 0x4) != 0;

        if (!canSystemGenerate)
            continue;

        if (!hasAlliancePublic && isAlliance)
        {
            // Create a system-owned Alliance neighborhood (owner guid = empty)
            // Use a sentinel owner guid so the neighborhood has a valid owner
            ObjectGuid systemOwner = ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 4, /*arg1*/ sRealmList->GetCurrentRealmId().Realm, /*arg2*/ 0, uint64(0));
            std::string allianceName = sHousingMgr.GenerateNeighborhoodName(id);
            Neighborhood* neighborhood = CreateNeighborhood(systemOwner, allianceName, id, NEIGHBORHOOD_FACTION_ALLIANCE, /*isPublic*/ true);
            if (neighborhood)
            {
                hasAlliancePublic = true;
                TC_LOG_INFO("server.loading", ">> Created default public Alliance neighborhood '{}' (map {})", allianceName, id);
            }
        }

        if (!hasHordePublic && isHorde)
        {
            ObjectGuid systemOwner = ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 4, /*arg1*/ sRealmList->GetCurrentRealmId().Realm, /*arg2*/ 1, uint64(0));
            std::string hordeName = sHousingMgr.GenerateNeighborhoodName(id);
            Neighborhood* neighborhood = CreateNeighborhood(systemOwner, hordeName, id, NEIGHBORHOOD_FACTION_HORDE, /*isPublic*/ true);
            if (neighborhood)
            {
                hasHordePublic = true;
                TC_LOG_INFO("server.loading", ">> Created default public Horde neighborhood '{}' (map {})", hordeName, id);
            }
        }
    }

    if (hasAlliancePublic && hasHordePublic)
    {
        TC_LOG_INFO("server.loading", ">> Public neighborhoods verified for both factions");
        return;
    }

    // If either faction still lacks a public neighborhood, the NeighborhoodMap data
    // has no system-generatable (Flags bit 0x4) row carrying that faction's flag.
    // That faction's players cannot enter housing at all — this is a hard data error,
    // not a warning. Report each missing faction independently with the exact fix.
    // (Deliberately NOT falling back to the both-faction purchasable maps ID 4/ID 7:
    //  they are not system-generatable and the client tutorial routes each faction to
    //  its own DB2 ID — Alliance->ID1, Horde->ID2 — so a public neighborhood hosted on
    //  ID 4/7 would remain unreachable and would not resolve the lockout. The correct
    //  and only safe remedy is to seed the missing system-generatable row.)
    if (!hasAlliancePublic)
        TC_LOG_ERROR("server.loading",
            ">> HOUSING LOCKOUT: no public Alliance neighborhood exists and none could be created. "
            "NeighborhoodMap has no system-generatable map with the Alliance flag (0x1|0x4). "
            "Check NeighborhoodMap.db2 and the hotfixes table neighborhood_map: "
            "ID 1 must be MapID 2735 with FactionRestriction 5 (0x1 Alliance | 0x4 SystemGenerate).");
    if (!hasHordePublic)
        TC_LOG_ERROR("server.loading",
            ">> HOUSING LOCKOUT: no public Horde neighborhood exists and none could be created. "
            "NeighborhoodMap has no system-generatable map with the Horde flag (0x2|0x4). "
            "Check NeighborhoodMap.db2 and the hotfixes table neighborhood_map: "
            "ID 2 must be MapID 2736 with FactionRestriction 6 (0x2 Horde | 0x4 SystemGenerate).");
}

void NeighborhoodMgr::RegenerateNeighborhoodNames()
{
    // Neighborhood names are stored as "ID1-ID2-ID3" tokens referencing NeighborhoodNameGen
    // entry IDs from the base DB2. Old names (from hotfix-era data) may contain text
    // where the values are the TEXT content of overwritten entries, not actual DB2 entry IDs.
    // Validate each public neighborhood's name: parse tokens, verify each is a real entry.
    uint32 regenerated = 0;
    for (auto& [guid, neighborhood] : _neighborhoods)
    {
        if (!neighborhood->IsPublic())
            continue;

        std::string const& name = neighborhood->GetName();
        bool needsRegeneration = false;

        // Validate: name must be "ID1-ID2-ID3" where each ID is a valid NeighborhoodNameGen entry
        std::vector<std::string> tokens;
        std::string token;
        for (char c : name)
        {
            if (c == '-')
            {
                if (!token.empty())
                    tokens.push_back(token);
                token.clear();
            }
            else
                token += c;
        }
        if (!token.empty())
            tokens.push_back(token);

        if (tokens.size() != 3)
        {
            needsRegeneration = true;
        }
        else
        {
            for (std::string const& t : tokens)
            {
                // Must be purely numeric
                bool allDigits = !t.empty();
                for (char c : t)
                {
                    if (c < '0' || c > '9')
                    {
                        allDigits = false;
                        break;
                    }
                }
                if (!allDigits)
                {
                    needsRegeneration = true;
                    break;
                }

                // Must reference a valid NeighborhoodNameGen entry in the base DB2
                uint32 entryId = std::stoul(t);
                if (!sNeighborhoodNameGenStore.LookupEntry(entryId))
                {
                    needsRegeneration = true;
                    break;
                }
            }
        }

        if (!needsRegeneration)
            continue;

        std::string newName = sHousingMgr.GenerateNeighborhoodName(neighborhood->GetNeighborhoodMapID());
        if (newName == "Unnamed Neighborhood")
            continue;

        TC_LOG_INFO("server.loading", ">> Regenerating neighborhood (guid={}) name: '{}' -> '{}'",
            guid.ToString(), name, newName);
        neighborhood->SetName(newName);
        ++regenerated;
    }

    if (regenerated > 0)
        TC_LOG_INFO("server.loading", ">> Regenerated {} neighborhood name(s) using base DB2 entry IDs", regenerated);
    else
        TC_LOG_INFO("server.loading", ">> Public neighborhood names verified");
}

void NeighborhoodMgr::CheckAndExpandNeighborhoods()
{
    // For each faction, check if all public neighborhoods are at or above 50% occupation
    // If so, create a new one to accommodate future players

    // Group public neighborhoods by faction
    std::unordered_map<int32, std::vector<Neighborhood*>> factionNeighborhoods;
    for (auto const& [guid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->IsPublic())
            factionNeighborhoods[neighborhood->GetFactionRestriction()].push_back(neighborhood.get());
    }

    // Check each faction
    for (auto const& [faction, neighborhoods] : factionNeighborhoods)
    {
        if (faction == NEIGHBORHOOD_FACTION_NONE)
            continue;

        bool hasCapacity = false;
        for (Neighborhood* neighborhood : neighborhoods)
        {
            uint32 occupiedPlots = neighborhood->GetOccupiedPlotCount();
            uint32 memberCount = neighborhood->GetMemberCount();

            // Check both occupied plots AND member count — either can be the bottleneck.
            // A neighborhood might have many members (invited/added) but few plots occupied,
            // or vice versa. Use the higher of the two for capacity assessment.
            uint32 usage = std::max(occupiedPlots, memberCount);

            // If any neighborhood is below 50% usage, there's still capacity
            if (usage < MAX_NEIGHBORHOOD_PLOTS / 2)
            {
                hasCapacity = true;
                break;
            }
        }

        if (hasCapacity)
            continue;

        // All public neighborhoods for this faction are at or above 50% — create a new one
        // Find the correct NeighborhoodMapID for this faction
        uint32 targetMapId = 0;
        for (auto const& [id, data] : sHousingMgr.GetAllNeighborhoodMapData())
        {
            int32 flags = data.Flags;
            bool isAlliance = (flags & 0x1) != 0;
            bool isHorde = (flags & 0x2) != 0;
            bool canSystemGenerate = (flags & 0x4) != 0;

            if (!canSystemGenerate)
                continue;

            if (faction == NEIGHBORHOOD_FACTION_ALLIANCE && isAlliance)
                { targetMapId = id; break; }
            else if (faction == NEIGHBORHOOD_FACTION_HORDE && isHorde)
                { targetMapId = id; break; }
        }

        if (targetMapId == 0)
            continue;

        std::string name = sHousingMgr.GenerateNeighborhoodName(targetMapId);
        // Use a unique system owner per neighborhood to avoid the "owner already has a neighborhood" check
        // Offset by the number of existing neighborhoods for this faction
        ObjectGuid systemOwner = ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 4, /*arg1*/ sRealmList->GetCurrentRealmId().Realm,
            /*arg2*/ static_cast<uint32>(neighborhoods.size()), uint64(0));

        Neighborhood* newNeighborhood = CreateNeighborhood(systemOwner, name, targetMapId, faction, /*isPublic*/ true);
        if (newNeighborhood)
        {
            TC_LOG_INFO("housing", "CheckAndExpandNeighborhoods: Created new {} neighborhood '{}' (all existing at 50%+ capacity)",
                faction == NEIGHBORHOOD_FACTION_ALLIANCE ? "Alliance" : "Horde", name);
        }
    }
}

ObjectGuid NeighborhoodMgr::GenerateNeighborhoodGuid(uint32 neighborhoodMapID)
{
    if (_nextGuid >= 0xFFFFFFFFFFFFFFFE)
    {
        TC_LOG_ERROR("housing", "Neighborhood guid overflow! Cannot continue, shutting down server.");
        World::StopNow(ERROR_EXIT_CODE);
    }

    uint64 counter = _nextGuid++;
    // arg1 MUST be the NeighborhoodMap.db2 record id, not the realm id. The client slices this 16-bit field out
    // of the GUID and uses it as that store's key; with a realm id in it the lookup misses and the client both
    // reports "wrong faction" (DoesFactionMatchNeighborhood returns false on a miss) and never resolves a UI map
    // (GetUIMapIDForNeighborhood -> nil), which shows as a House Finder that lists the neighborhood but refuses
    // it and spins forever. Realm 3 made this visible; a realm whose id happened to equal a real
    // NeighborhoodMap id (e.g. 1 = the Alliance map) masked it for Alliance characters and would still have
    // failed for Horde.
    return ObjectGuid::Create<HighGuid::Housing>(/*subType*/ 4, /*arg1*/ neighborhoodMapID, /*arg2*/ 0, counter);
}

namespace
{
    // A character of the account that is online and holds the house. Every online character of a Battle.net account
    // holds all of its houses and they share one stored state, so a change made through one of them reaches the
    // others and is what they save.
    Player* FindOnlineHouseHolder(uint32 bnetAccountId, ObjectGuid houseGuid)
    {
        if (!bnetAccountId || houseGuid.IsEmpty())
            return nullptr;

        for (auto const& [accountId, session] : sWorld->GetAllSessions())
            if (session && session->GetBattlenetAccountId() == bnetAccountId)
                if (Player* player = session->GetPlayer(); player && player->GetHousingByGuid(houseGuid))
                    return player;

        return nullptr;
    }

    // The characters of a Battle.net account, on every one of its game accounts, that are not deleted, apart from
    // excludedGuid; oldest first, the order the House Settings owner list shows them in. Faction and guild come from
    // the character cache, which follows guild changes at once.
    std::vector<Housing::OwnerCandidate> LoadOwnerCandidates(uint32 bnetAccountId, ObjectGuid excludedGuid)
    {
        std::vector<Housing::OwnerCandidate> candidates;
        if (!bnetAccountId)
            return candidates;

        LoginDatabasePreparedStatement* loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT_IDS);
        loginStmt->setUInt32(0, bnetAccountId);
        PreparedQueryResult gameAccounts = LoginDatabase.Query(loginStmt);
        if (!gameAccounts)
            return candidates;

        std::vector<ObjectGuid::LowType> characterGuids;
        do
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARS_BY_ACCOUNT_ID);
            stmt->setUInt32(0, gameAccounts->Fetch()[0].GetUInt32());
            if (PreparedQueryResult characters = CharacterDatabase.Query(stmt))
            {
                do
                    characterGuids.push_back(characters->Fetch()[0].GetUInt64());
                while (characters->NextRow());
            }
        } while (gameAccounts->NextRow());

        std::sort(characterGuids.begin(), characterGuids.end());
        for (ObjectGuid::LowType lowGuid : characterGuids)
        {
            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
            if (guid == excludedGuid)
                continue;

            CharacterCacheEntry const* character = sCharacterCache->GetCharacterCacheByGuid(guid);
            if (!character || character->IsDeleted)
                continue;

            candidates.push_back({ guid, uint32(Player::TeamForRace(character->Race)), character->GuildId });
        }

        return candidates;
    }

    // Shows a new owner for a house: through the Housing a character of the account holds while one is online (it
    // updates the plot), otherwise on its plot. Either way the row is written in the given transaction, so the new
    // owner is saved together with the delete or pack it belongs to.
    void SetHouseCosmeticOwner(Player* holder, Neighborhood* neighborhood, ObjectGuid houseGuid, uint64 houseDatabaseId,
        ObjectGuid cosmeticOwnerGuid, CharacterDatabaseTransaction trans)
    {
        if (Housing* housing = holder ? holder->GetHousingByGuid(houseGuid) : nullptr)
        {
            housing->SetCosmeticOwnerGuid(cosmeticOwnerGuid, trans);
            holder->SyncAccountHouseOnOtherCharacters(houseGuid);
            return;
        }

        if (houseDatabaseId)
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_COSMETIC_OWNER);
            stmt->setUInt64(0, cosmeticOwnerGuid.GetCounter());
            stmt->setUInt64(1, houseDatabaseId);
            trans->Append(stmt);
        }

        if (neighborhood)
            neighborhood->UpdatePlotCosmeticOwnerByHouse(houseGuid, cosmeticOwnerGuid);
    }

    // A character that exists and is not waiting in the list of deleted characters.
    bool IsLivingCharacter(ObjectGuid guid)
    {
        if (guid.IsEmpty())
            return false;
        CharacterCacheEntry const* character = sCharacterCache->GetCharacterCacheByGuid(guid);
        return character && !character->IsDeleted;
    }

    // What was paid for a house, which relinquishing it pays back: from the Housing a character of its account holds
    // while one is online, else from its row.
    uint64 GetHouseRefundAmount(Player* holder, ObjectGuid houseGuid, uint64 houseDatabaseId)
    {
        if (Housing const* housing = holder ? holder->GetHousingByGuid(houseGuid) : nullptr)
            return housing->GetRefundAmount();

        if (!houseDatabaseId)
            return 0;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_HOUSING_REFUND_AMOUNT);
        stmt->setUInt64(0, houseDatabaseId);
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            return (*result)[0].GetUInt64();
        return 0;
    }

    // No capture shows the refund of a house packed without its owner giving it up, so this text is the server's own.
    constexpr char HOUSE_PACKED_REFUND_MAIL_SUBJECT[] = "Your house was packed up";
    constexpr char HOUSE_PACKED_REFUND_MAIL_BODY[] = "Your house was packed up and its plot is free. Your house layout has been saved; "
        "purchase a new house to import it. Enclosed is what was paid for the house.";
}

void NeighborhoodMgr::PackHouseForOwnerLoss(Neighborhood* neighborhood, uint8 plotIndex, ObjectGuid houseGuid, uint64 houseDatabaseId,
    uint32 bnetAccountId, ObjectGuid newCosmeticOwner, bool clearCosmeticOwner, ObjectGuid refundRecipient, CharacterDatabaseTransaction trans)
{
    // Packed the way relinquishing packs a house: rooms, decor, fixtures, level and favor are kept, the row keeps the
    // neighborhood and plot it stood on, and the plot is free at once. What was paid for the house is paid back, as a
    // relinquish pays it back, since the house was not given up by choice. The money goes by mail because the
    // character it goes to may be offline; players report a housing refund arriving by mail after a move.
    Player* holder = FindOnlineHouseHolder(bnetAccountId, houseGuid);

    // Who was shown as the owner before anything changes, for the guild's list of member houses.
    ObjectGuid shownOwner;
    if (Neighborhood::PlotInfo const* plot = neighborhood->GetPlotInfo(plotIndex); plot && plot->HouseGuid == houseGuid)
        shownOwner = plot->OwnerGuid;
    uint32 guildId = neighborhood->GetGuildId();
    if (!guildId && !shownOwner.IsEmpty())
        guildId = uint32(sCharacterCache->GetCharacterGuildIdByGuid(shownOwner));

    uint64 const refund = GetHouseRefundAmount(holder, houseGuid, houseDatabaseId);

    // Everyone inside is put out on the plot first: once the house is packed, Exit House finds no plot to put them on.
    HouseInteriorMap::PutCharactersOut(neighborhood, plotIndex, houseDatabaseId, houseGuid);

    HousingMap::DespawnHouseFromPlot(neighborhood, plotIndex, houseGuid);
    neighborhood->ReleasePlotByHouse(houseGuid, trans);

    if (holder)
        holder->PackHousing(houseGuid, trans);
    else if (houseDatabaseId)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_HOUSING_PLACEMENT);
        stmt->setUInt64(0, neighborhood->GetGuid().GetCounter());
        stmt->setUInt8(1, plotIndex);
        stmt->setUInt8(2, 1);
        stmt->setUInt64(3, houseDatabaseId);
        trans->Append(stmt);
    }
    else
        TC_LOG_ERROR("housing", "NeighborhoodMgr::PackHouseForOwnerLoss: house {} on plot {} of neighborhood '{}' has no row yet; only its plot was freed",
            houseGuid.ToString(), plotIndex, neighborhood->GetName());

    if (!newCosmeticOwner.IsEmpty() || clearCosmeticOwner)
        SetHouseCosmeticOwner(holder, nullptr, houseGuid, houseDatabaseId, newCosmeticOwner, trans);

    bool const paid = refund && IsLivingCharacter(refundRecipient);
    if (paid)
    {
        MailDraft(HOUSE_PACKED_REFUND_MAIL_SUBJECT, HOUSE_PACKED_REFUND_MAIL_BODY)
            .AddMoney(refund)
            .SendMailTo(trans, MailReceiver(ObjectAccessor::FindConnectedPlayer(refundRecipient), refundRecipient.GetCounter()),
                MailSender(MAIL_NORMAL, UI64LIT(0), MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
    }

    neighborhood->RefreshMirrorDataForOnlineMembers();

    // The house leaves the guild's list of member houses, as it does when a member relinquishes one.
    if (Guild* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr)
    {
        WorldPackets::Housing::HousingSvcsGuildRemoveHouseNotification notification;
        notification.House.HouseGUID = houseGuid;
        notification.House.CosmeticOwnerGUID = shownOwner;
        guild->BroadcastPacket(notification.Write());
    }

    // The account's online characters move their active endeavor off the packed house, and their clients reload the
    // housing data, as a relinquish asks the client that gave the house up to.
    for (auto const& [accountId, session] : sWorld->GetAllSessions())
    {
        if (!session || session->GetBattlenetAccountId() != bnetAccountId)
            continue;

        if (Player* player = session->GetPlayer(); player && player->IsInWorld())
        {
            player->UpdateInitiativeComponent();

            WorldPackets::Housing::HousingSvcRequestPlayerReloadData reloadData;
            session->SendPacket(reloadData.Write());
        }
    }

    TC_LOG_INFO("housing", "NeighborhoodMgr::PackHouseForOwnerLoss: house {} (database id {}) of Battle.net account {} is packed and plot {} of neighborhood '{}' is free; shown owner {}; {} copper paid back to {}",
        houseGuid.ToString(), houseDatabaseId, bnetAccountId, plotIndex, neighborhood->GetName(),
        clearCosmeticOwner ? std::string("none") : newCosmeticOwner.IsEmpty() ? std::string("unchanged") : newCosmeticOwner.ToString(),
        paid ? refund : UI64LIT(0), paid ? refundRecipient.ToString() : std::string("nobody"));
}

void NeighborhoodMgr::OnCharacterDeleted(ObjectGuid characterGuid, CharacterDatabaseTransaction trans)
{
    struct AffectedHouse
    {
        uint64 DatabaseId = 0;
        uint32 BnetAccountId = 0;
        ObjectGuid HouseGuid;
    };
    std::vector<AffectedHouse> houses;
    auto noteHouse = [&houses](uint64 databaseId, uint32 bnetAccountId, ObjectGuid houseGuid)
    {
        if (houseGuid.IsEmpty() || std::any_of(houses.begin(), houses.end(), [&houseGuid](AffectedHouse const& house) { return house.HouseGuid == houseGuid; }))
            return;
        houses.push_back({ databaseId, bnetAccountId, houseGuid });
    };

    // The houses whose row shows her as owner, standing or packed.
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_HOUSING_BY_COSMETIC_OWNER);
    stmt->setUInt64(0, characterGuid.GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        do
        {
            Field* fields = result->Fetch();
            // SELECT guid, bnetAccountId, slot
            uint32 const bnetAccountId = fields[1].GetUInt32();
            noteHouse(fields[0].GetUInt64(), bnetAccountId, Housing::MakeHouseGuid(fields[2].GetUInt8(), bnetAccountId));
        } while (result->NextRow());
    }

    // The plots that show her as owner, or whose roster entry is hers (the buyer keeps the entry when House Settings
    // names another owner). Memory is ahead of the rows while a save is still on its way.
    for (auto const& [neighborhoodGuid, neighborhood] : _neighborhoods)
        for (Neighborhood::PlotInfo const& plot : neighborhood->GetPlots())
            if (plot.IsOccupied() && (plot.OwnerGuid == characterGuid || neighborhood->GetPlotHolder(plot.PlotIndex) == characterGuid))
                noteHouse(plot.HouseDatabaseId, uint32(plot.OwnerBnetGuid.GetCounter()), plot.HouseGuid);

    if (houses.empty())
        return;

    std::unordered_map<uint32, std::vector<Housing::OwnerCandidate>> candidatesByAccount;
    for (AffectedHouse const& house : houses)
    {
        auto candidatesIt = candidatesByAccount.find(house.BnetAccountId);
        if (candidatesIt == candidatesByAccount.end())
            candidatesIt = candidatesByAccount.emplace(house.BnetAccountId, LoadOwnerCandidates(house.BnetAccountId, characterGuid)).first;
        std::vector<Housing::OwnerCandidate> const& candidates = candidatesIt->second;

        // Where the house stands and who is shown as its owner: from the Housing an online character of the account
        // holds, else from its plot, else it is a packed house whose row names her.
        Player* holder = FindOnlineHouseHolder(house.BnetAccountId, house.HouseGuid);
        Housing const* housing = holder ? holder->GetHousingByGuid(house.HouseGuid) : nullptr;
        Neighborhood* neighborhood = nullptr;
        uint8 plotIndex = INVALID_PLOT_INDEX;
        ObjectGuid cosmeticOwner = characterGuid;
        if (housing)
        {
            cosmeticOwner = housing->GetCosmeticOwnerGuid();
            if (!housing->IsPacked())
            {
                neighborhood = GetNeighborhood(housing->GetNeighborhoodGuid());
                plotIndex = housing->GetPlotIndex();
            }
        }
        else
        {
            for (auto const& [neighborhoodGuid, candidateNeighborhood] : _neighborhoods)
            {
                if (Neighborhood::PlotInfo const* plot = candidateNeighborhood->GetPlotInfoByHouse(house.HouseGuid))
                {
                    neighborhood = candidateNeighborhood.get();
                    plotIndex = plot->PlotIndex;
                    cosmeticOwner = plot->OwnerGuid;
                    break;
                }
            }
        }

        // A shown owner who is not one of the account's other living characters is replaced as well: it is her, or a
        // character deleted before houses passed on.
        bool const ownerGone = std::none_of(candidates.begin(), candidates.end(),
            [&cosmeticOwner](Housing::OwnerCandidate const& candidate) { return candidate.Guid == cosmeticOwner; });
        bool const holdsPlot = neighborhood && neighborhood->GetPlotHolder(plotIndex) == characterGuid;
        if (!ownerGone && !holdsPlot)
            continue;

        // A packed house stands in no neighborhood, so any character of the account may be shown as its owner; with
        // none left, nobody is, and whoever unpacks it next becomes its owner.
        ObjectGuid const anyCharacter = Housing::ChooseNextCosmeticOwner(candidates, NEIGHBORHOOD_FACTION_NONE, 0);
        if (!neighborhood)
        {
            SetHouseCosmeticOwner(holder, nullptr, house.HouseGuid, house.DatabaseId, anyCharacter, trans);

            TC_LOG_INFO("housing", "NeighborhoodMgr::OnCharacterDeleted: packed house {} of deleted {} now shows owner {}",
                house.HouseGuid.ToString(), characterGuid.ToString(), anyCharacter.IsEmpty() ? std::string("none") : anyCharacter.ToString());
            continue;
        }

        // A standing house passes to the first character of the account who may own it where it stands: of the
        // neighborhood's faction in one of the server's public neighborhoods, in the guild in a guild neighborhood.
        int32 const factionRestriction = neighborhood->IsServerPublic() ? neighborhood->GetFactionRestriction() : NEIGHBORHOOD_FACTION_NONE;
        ObjectGuid const newOwner = ownerGone
            ? Housing::ChooseNextCosmeticOwner(candidates, factionRestriction, neighborhood->GetGuildId())
            : cosmeticOwner;
        if (newOwner.IsEmpty())
        {
            // Nobody left may own it there, so it is packed, as Blizzard Watch says a deleted owner's house is. Its
            // refund goes to the character now shown as its owner, or to nobody when the account has none left.
            PackHouseForOwnerLoss(neighborhood, plotIndex, house.HouseGuid, house.DatabaseId, house.BnetAccountId,
                anyCharacter, anyCharacter.IsEmpty(), anyCharacter, trans);
            continue;
        }

        if (ownerGone)
            SetHouseCosmeticOwner(holder, neighborhood, house.HouseGuid, house.DatabaseId, newOwner, trans);
        if (holdsPlot)
            neighborhood->MovePlotHolder(plotIndex, newOwner, trans);
        neighborhood->RefreshMirrorDataForOnlineMembers();

        TC_LOG_INFO("housing", "NeighborhoodMgr::OnCharacterDeleted: house {} on plot {} of neighborhood '{}' passes from deleted {} to {}",
            house.HouseGuid.ToString(), plotIndex, neighborhood->GetName(), characterGuid.ToString(), newOwner.ToString());
    }
}

void NeighborhoodMgr::OnGuildMemberRemoved(uint32 guildId, ObjectGuid characterGuid, CharacterDatabaseTransaction trans)
{
    if (!guildId || characterGuid.IsEmpty())
        return;

    struct GuildHouse
    {
        Neighborhood* HouseNeighborhood = nullptr;
        uint8 PlotIndex = INVALID_PLOT_INDEX;
        ObjectGuid HouseGuid;
        uint64 DatabaseId = 0;
        uint32 BnetAccountId = 0;
    };
    std::vector<GuildHouse> houses;
    for (auto const& [neighborhoodGuid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->GetGuildId() != guildId)
            continue;

        for (Neighborhood::PlotInfo const& plot : neighborhood->GetPlots())
            if (plot.IsOccupied() && plot.OwnerGuid == characterGuid)
                houses.push_back({ neighborhood.get(), plot.PlotIndex, plot.HouseGuid, plot.HouseDatabaseId, uint32(plot.OwnerBnetGuid.GetCounter()) });
    }

    if (houses.empty())
        return;

    // Players who leave or are removed from the guild lose the plot, and the house is packed up with its layout saved
    // (Wowhead and Blizzard Watch). A house in a guild neighborhood may only be owned by a guild member (the 12.1
    // client's COSMETIC_OWNER_NOT_IN_GUILD). She stays its shown owner, and its refund goes to her.
    bool const ownTransaction = !trans;
    if (ownTransaction)
        trans = CharacterDatabase.BeginTransaction();

    for (GuildHouse const& house : houses)
        PackHouseForOwnerLoss(house.HouseNeighborhood, house.PlotIndex, house.HouseGuid, house.DatabaseId, house.BnetAccountId,
            ObjectGuid::Empty, false, characterGuid, trans);

    if (ownTransaction)
        CharacterDatabase.CommitTransaction(trans);
}

void NeighborhoodMgr::OnCharacterRemoved(ObjectGuid characterGuid, CharacterDatabaseTransaction trans)
{
    if (characterGuid.IsEmpty())
        return;

    // Her charter, if it was never finalized (a finalized charter's row is gone): its id is her guid counter, and only
    // her guid can edit or finalize it, so every account that signed it could never sign another charter. It is
    // dropped with its signatures, and each signer online is told, as a charter edit that drops signatures tells them.
    uint64 const charterId = characterGuid.GetCounter();
    std::vector<ObjectGuid> signers;
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER_SIGNATURES);
    stmt->setUInt64(0, charterId);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        do
            signers.push_back(ObjectGuid::Create<HighGuid::Player>(result->Fetch()[0].GetUInt64()));
        while (result->NextRow());
    }
    NeighborhoodCharter::DeleteFromDB(charterId, trans);

    ObjectGuid const charterGuid = ObjectGuid::Create<HighGuid::Housing>(0, 0, 0, charterId);
    for (ObjectGuid const& signer : signers)
    {
        if (Player* signerPlayer = ObjectAccessor::FindConnectedPlayer(signer))
        {
            WorldPackets::Neighborhood::NeighborhoodCharterSignatureRemovedNotification removed;
            removed.CharterGuid = charterGuid;
            signerPlayer->SendDirectMessage(removed.Write());
        }
    }

    // Her signatures on other charters, which would otherwise keep her Battle.net account from signing another one.
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_CHARTER_SIGNATURES_BY_SIGNER);
    stmt->setUInt64(0, characterGuid.GetCounter());
    trans->Append(stmt);

    // Her roster entries and the invites naming her. OnCharacterDeleted has already passed on or packed every house she
    // was shown as owner of, and the plots she held with them.
    for (auto const& [neighborhoodGuid, neighborhood] : _neighborhoods)
    {
        if (neighborhood->RemoveDeletedCharacter(characterGuid, trans))
            neighborhood->RefreshMirrorDataForOnlineMembers();

        if (neighborhood->IsOwner(characterGuid))
            TC_LOG_INFO("housing", "NeighborhoodMgr::OnCharacterRemoved: deleted {} still owns neighborhood '{}'; its owner is not changed",
                characterGuid.ToString(), neighborhood->GetName());
    }

    TC_LOG_DEBUG("housing", "NeighborhoodMgr::OnCharacterRemoved: removed the charter, signatures, invites and roster entries of {} ({} signer(s) of her charter)",
        characterGuid.ToString(), uint32(signers.size()));
}

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

#include "WorldSession.h"
#include "Account.h"
#include "HousingNeighborhoodMirrorEntity.h"
#include "QueryPackets.h"
#include "HousingPlayerHouseEntity.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Housing.h"
#include "HousingDecorStore.h"
#include "HousingDefines.h"
#include "HousingMap.h"
#include "HousingMgr.h"
#include "HousingPackets.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "InitiativeManager.h"
#include "Neighborhood.h"
#include "NeighborhoodCharter.h"
#include "NeighborhoodMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "GameTime.h"
#include "UpdateData.h"
#include "World.h"
#include <unordered_set>

namespace
{
    std::string HexDumpPacket(WorldPacket const* packet, size_t maxBytes = 128)
    {
        if (!packet || packet->size() == 0)
            return "(empty)";
        size_t len = std::min(packet->size(), maxBytes);
        std::string result;
        result.reserve(len * 3 + 32);
        uint8 const* raw = packet->data();
        for (size_t i = 0; i < len; ++i)
        {
            if (i > 0 && i % 32 == 0)
                result += "\n  ";
            else if (i > 0)
                result += ' ';
            result += fmt::format("{:02X}", raw[i]);
        }
        if (len < packet->size())
            result += fmt::format(" ...({} more)", packet->size() - len);
        return result;
    }

    std::string GuidHex(ObjectGuid const& guid)
    {
        return fmt::format("lo={:016X} hi={:016X}", guid.GetRawValue(0), guid.GetRawValue(1));
    }

    // Finds the plot of the cornerstone the player is actually standing at: a
    // real cornerstone game object on the player's housing map, within reach
    // and in line of sight. A GUID the client names, or a plot it remembers,
    // is not enough on its own.
    NeighborhoodPlotData const* ResolveCornerstone(Player* player, ObjectGuid cornerstoneGuid, Neighborhood*& neighborhood)
    {
        neighborhood = nullptr;
        if (!player)
            return nullptr;

        if (!cornerstoneGuid.IsGameObject())
        {
            TC_LOG_DEBUG("housing", "Cornerstone refused for player {}: {} is not a game object",
                player->GetGUID().ToString(), cornerstoneGuid.ToString());
            return nullptr;
        }

        HousingMap* map = dynamic_cast<HousingMap*>(player->GetMap());
        if (!map || !map->GetNeighborhood())
        {
            TC_LOG_DEBUG("housing", "Cornerstone refused for player {}: map {} is not a housing map with a neighborhood (cornerstone {})",
                player->GetGUID().ToString(), player->GetMapId(), cornerstoneGuid.ToString());
            return nullptr;
        }

        GameObject* cornerstone = player->GetGameObjectIfCanInteractWith(cornerstoneGuid);
        if (!cornerstone)
        {
            TC_LOG_DEBUG("housing", "Cornerstone refused for player {}: {} is not on this map, not in this phase or out of reach",
                player->GetGUID().ToString(), cornerstoneGuid.ToString());
            return nullptr;
        }

        // Only static world geometry (walls, buildings, terrain) is checked.
        // An unowned plot's cornerstone has collision, and a game object has no
        // hit sphere, so a ray that included game objects would end inside the
        // cornerstone's own model and always be blocked by it.
        if (!player->IsWithinLOSInMap(cornerstone, LINEOFSIGHT_CHECK_VMAP))
        {
            TC_LOG_DEBUG("housing", "Cornerstone refused for player {}: no line of sight to {} (distance {:.1f})",
                player->GetGUID().ToString(), cornerstoneGuid.ToString(), player->GetExactDist(cornerstone));
            return nullptr;
        }

        // Every cornerstone shares one entry, so the plot comes from the cornerstone this map spawned for it.
        int8 const plotIndex = map->GetPlotIndexForCornerstone(cornerstone->GetGUID());
        NeighborhoodPlotData const* plot = plotIndex >= 0 && plotIndex < int8(MAX_NEIGHBORHOOD_PLOTS)
            ? sHousingMgr.GetPlot(map->GetNeighborhood()->GetNeighborhoodMapID(), uint8(plotIndex))
            : nullptr;
        if (!plot)
        {
            TC_LOG_DEBUG("housing", "Cornerstone refused for player {}: {} is not a plot cornerstone of neighborhood map {}",
                player->GetGUID().ToString(), cornerstoneGuid.ToString(), map->GetNeighborhood()->GetNeighborhoodMapID());
            return nullptr;
        }

        neighborhood = map->GetNeighborhood();
        return plot;
    }
}

// ============================================================
// Neighborhood Charter System
// ============================================================

void WorldSession::HandleNeighborhoodCharterOpenConfirmationUI(WorldPackets::Neighborhood::NeighborhoodCharterOpenConfirmationUI const& /*neighborhoodCharterOpenConfirmationUI*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_CHARTER_OPEN_CONFIRMATION_UI received for player {}",
        player->GetGUID().ToString());

    // If the player already has a charter in flight, push its full state first.
    // SMSG_NEIGHBORHOOD_CHARTER_OPEN_UI_RESPONSE (0x5B0001) carries the same body as
    // SMSG_NEIGHBORHOOD_CHARTER_UPDATE_RESPONSE (0x5B0000) — Result + CharterGuid + MapID +
    // SignatureCount + Signers + required-signature count + name — i.e. everything the charter
    // panel renders. Today that state is only ever sent as the reply to CHARTER_CREATE/EDIT, so
    // a player who relogs (or reopens the panel) with a pending charter gets an empty panel and
    // no charter GUID, which also makes the follow-up CHARTER_FINALIZE unreachable from the UI.
    // Only sent when a charter actually exists; the no-charter case is unchanged.
    uint64 charterId = static_cast<uint64>(player->GetGUID().GetCounter());

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER);
    stmt->setUInt64(0, charterId);
    PreparedQueryResult charterResult = CharacterDatabase.Query(stmt);
    if (charterResult)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER_SIGNATURES);
        stmt->setUInt64(0, charterId);
        PreparedQueryResult sigResult = CharacterDatabase.Query(stmt);

        NeighborhoodCharter charter(charterId, ObjectGuid::Empty);
        if (charter.LoadFromDB(charterResult, sigResult))
        {
            WorldPackets::Neighborhood::NeighborhoodCharterOpenUIResponse openUI;
            openUI.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
            openUI.CharterGuid = ObjectGuid::Create<HighGuid::Housing>(0, 0, 0, charterId);
            openUI.MapID = charter.GetNeighborhoodMapID();
            openUI.SignatureCount = charter.GetSignatureCount();
            openUI.Signers = charter.GetSignatures();
            openUI.Unknown = MIN_CHARTER_SIGNATURES;
            openUI.NeighborhoodName = charter.GetName();
            SendPacket(openUI.Write());

            TC_LOG_DEBUG("housing", "Sent NeighborhoodCharterOpenUIResponse for charter {} ('{}', {}/{} signatures) to player {}",
                charterId, charter.GetName(), charter.GetSignatureCount(), MIN_CHARTER_SIGNATURES,
                player->GetGUID().ToString());
        }
        else
        {
            // A charter row exists but will not load — report the failure instead of pretending
            // the panel is empty, so the client shows an error rather than a blank charter.
            WorldPackets::Neighborhood::NeighborhoodCharterOpenUIResponse openUI;
            openUI.Result = static_cast<uint8>(HOUSING_RESULT_DB_ERROR);
            SendPacket(openUI.Write());

            TC_LOG_ERROR("housing", "HandleNeighborhoodCharterOpenConfirmationUI: charter {} exists but failed to load for player {}",
                charterId, player->GetGUID().ToString());
        }
    }

    // Client requests to open the charter creation confirmation UI
    // The client handles UI display; server acknowledges readiness
    WorldPackets::Neighborhood::NeighborhoodCharterOpenConfirmationUIResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "Sent NeighborhoodCharterOpenConfirmationUIResponse (SUCCESS) to player {}",
        player->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodCharterCreate(WorldPackets::Neighborhood::NeighborhoodCharterCreate const& neighborhoodCharterCreate)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_CHARTER_NEIGHBORHOOD))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_CHARTER_CREATE NeighborhoodMapID: {}, FactionFlags: {}, Name: {}",
        neighborhoodCharterCreate.NeighborhoodMapID, neighborhoodCharterCreate.FactionFlags,
        neighborhoodCharterCreate.Name);

    // Validate name
    if (neighborhoodCharterCreate.Name.empty() || neighborhoodCharterCreate.Name.length() > HOUSING_MAX_NAME_LENGTH)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_INVALID_NEIGHBORHOOD_NAME);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterCreate: Invalid name length for player {}",
            player->GetGUID().ToString());
        return;
    }

    if (!ObjectMgr::IsValidCharterName(neighborhoodCharterCreate.Name) || sObjectMgr->IsReservedName(neighborhoodCharterCreate.Name))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_FILTER_REJECTED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterCreate: Name rejected by filter for player {}",
            player->GetGUID().ToString());
        return;
    }

    // The client names the district, and only one her faction may found a neighborhood in is taken. The neighborhood's
    // faction is hers; the client's faction flags are not kept, as what the client sends there was never captured.
    if (HousingResult mapCheck = sHousingMgr.CheckNeighborhoodFoundingMap(neighborhoodCharterCreate.NeighborhoodMapID, player->GetTeam());
        mapCheck != HOUSING_RESULT_SUCCESS)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(mapCheck);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterCreate: {} may not found a neighborhood on NeighborhoodMap {} (result {})",
            player->GetGUID().ToString(), neighborhoodCharterCreate.NeighborhoodMapID, uint32(mapCheck));
        return;
    }

    // Create charter object. The creator's Battle.net account may not sign it.
    uint64 charterId = static_cast<uint64>(player->GetGUID().GetCounter());
    NeighborhoodCharter charter(charterId, player->GetGUID(), GetBattlenetAccountId());
    charter.SetName(neighborhoodCharterCreate.Name);
    charter.SetNeighborhoodMapID(neighborhoodCharterCreate.NeighborhoodMapID);
    charter.SetFactionFlags(uint32(HousingMgr::GetNeighborhoodFactionForTeam(player->GetTeam())));
    charter.SetIsGuild(false);

    // The creator does not count toward MIN_CHARTER_SIGNATURES; whether retail counts her is not known.

    // Saved before the next packet is handled, as the edit, the finalize and every signature are: a queued save could
    // run after a signature added in between and delete it without telling the signer, and an edit or a signature
    // handled first would find no charter.
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    charter.SaveToDB(trans);
    CharacterDatabase.DirectCommitTransaction(trans);

    // Fill the success response so the client's charter panel shows the charter
    // GUID, name and signature progress; with only Result set the panel is blank and
    // the client never learns the charter GUID. CharterGuid is built the same way as
    // in the sign-request path. `Unknown` carries the required signature count
    // (server policy MIN_CHARTER_SIGNATURES; record 14982 of a retail capture that came
    // with the imported housing source, not one of ours, shows 0x0a).
    // NOTE: leadByte/Result semantics left as documented (uint8 Result, client
    // tests != 0 for error); the sniff's 0x42 leadByte is unverified for the
    // success path and intentionally not hardcoded here.
    WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.CharterGuid = ObjectGuid::Create<HighGuid::Housing>(0, 0, 0, charterId);
    response.MapID = neighborhoodCharterCreate.NeighborhoodMapID;
    response.SignatureCount = charter.GetSignatureCount();
    response.Signers = charter.GetSignatures();
    response.Unknown = MIN_CHARTER_SIGNATURES;
    response.NeighborhoodName = neighborhoodCharterCreate.Name;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "Player {} created neighborhood charter '{}' (ID: {}, MapID: {})",
        player->GetGUID().ToString(), neighborhoodCharterCreate.Name, charterId,
        neighborhoodCharterCreate.NeighborhoodMapID);
}

void WorldSession::HandleNeighborhoodCharterEdit(WorldPackets::Neighborhood::NeighborhoodCharterEdit const& neighborhoodCharterEdit)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_CHARTER_NEIGHBORHOOD))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_CHARTER_EDIT NeighborhoodMapID: {}, FactionFlags: {}, Name: {}",
        neighborhoodCharterEdit.NeighborhoodMapID, neighborhoodCharterEdit.FactionFlags,
        neighborhoodCharterEdit.Name);

    // Validate name
    if (neighborhoodCharterEdit.Name.empty() || neighborhoodCharterEdit.Name.length() > HOUSING_MAX_NAME_LENGTH)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_INVALID_NEIGHBORHOOD_NAME);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterEdit: Invalid name length for player {}",
            player->GetGUID().ToString());
        return;
    }

    if (!ObjectMgr::IsValidCharterName(neighborhoodCharterEdit.Name) || sObjectMgr->IsReservedName(neighborhoodCharterEdit.Name))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_FILTER_REJECTED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterEdit: Name rejected by filter for player {}",
            player->GetGUID().ToString());
        return;
    }

    // The same district check as a new charter; the faction stays hers.
    if (HousingResult mapCheck = sHousingMgr.CheckNeighborhoodFoundingMap(neighborhoodCharterEdit.NeighborhoodMapID, player->GetTeam());
        mapCheck != HOUSING_RESULT_SUCCESS)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(mapCheck);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterEdit: {} may not found a neighborhood on NeighborhoodMap {} (result {})",
            player->GetGUID().ToString(), neighborhoodCharterEdit.NeighborhoodMapID, uint32(mapCheck));
        return;
    }

    uint32 const factionRestriction = uint32(HousingMgr::GetNeighborhoodFactionForTeam(player->GetTeam()));

    // Edit updates the charter with new parameters (same charter ID, re-saved)
    uint64 charterId = static_cast<uint64>(player->GetGUID().GetCounter());
    ObjectGuid charterGuid = ObjectGuid::Create<HighGuid::Housing>(0, 0, 0, charterId);

    // Changing the charter's settings removes its signatures (GlobalStrings HOUSING_CREATENEIGHBORHOOD_SETTINGS_WARNING);
    // an edit that changes nothing keeps them. Each signer whose signature is dropped is told with
    // SMSG_NEIGHBORHOOD_CHARTER_SIGNATURE_REMOVED, so her charter panel does not keep a stale "signed" state.
    NeighborhoodCharter charter(charterId, player->GetGUID(), GetBattlenetAccountId());
    charter.SetName(neighborhoodCharterEdit.Name);
    charter.SetNeighborhoodMapID(neighborhoodCharterEdit.NeighborhoodMapID);
    charter.SetFactionFlags(factionRestriction);
    charter.SetIsGuild(false);

    std::vector<ObjectGuid> droppedSigners;
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER);
        stmt->setUInt64(0, charterId);
        PreparedQueryResult oldCharterResult = CharacterDatabase.Query(stmt);
        if (oldCharterResult)
        {
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER_SIGNATURES);
            stmt->setUInt64(0, charterId);
            PreparedQueryResult oldSigResult = CharacterDatabase.Query(stmt);

            NeighborhoodCharter oldCharter(charterId, ObjectGuid::Empty);
            if (oldCharter.LoadFromDB(oldCharterResult, oldSigResult))
            {
                if (oldCharter.HasSameSettings(neighborhoodCharterEdit.Name, neighborhoodCharterEdit.NeighborhoodMapID, factionRestriction))
                    charter.CopySignaturesFrom(oldCharter);
                else
                    droppedSigners = oldCharter.GetSignatures();
            }
        }
    }

    // Re-persist. Saved before the next packet is handled, as NeighborhoodCharter::AddSignature saves a signature:
    // a queued save could run after a signature added in between and delete it without telling the signer.
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    NeighborhoodCharter::DeleteFromDB(charterId, trans);
    charter.SaveToDB(trans);
    CharacterDatabase.DirectCommitTransaction(trans);

    // Tell every co-signer whose signature the edit just wiped, so their charter panel drops
    // the stale "signed" state instead of holding it until relog.
    for (ObjectGuid const& signer : droppedSigners)
    {
        if (Player* signerPlayer = ObjectAccessor::FindPlayer(signer))
        {
            WorldPackets::Neighborhood::NeighborhoodCharterSignatureRemovedNotification removed;
            removed.CharterGuid = charterGuid;
            signerPlayer->SendDirectMessage(removed.Write());
        }
    }

    WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.CharterGuid = charterGuid;
    response.MapID = neighborhoodCharterEdit.NeighborhoodMapID;
    response.SignatureCount = charter.GetSignatureCount();
    response.Signers = charter.GetSignatures();
    response.Unknown = MIN_CHARTER_SIGNATURES;
    response.NeighborhoodName = neighborhoodCharterEdit.Name;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "Player {} edited neighborhood charter '{}' (ID: {}, MapID: {}), dropped {} co-signature(s)",
        player->GetGUID().ToString(), neighborhoodCharterEdit.Name, charterId,
        neighborhoodCharterEdit.NeighborhoodMapID, uint32(droppedSigners.size()));
}

void WorldSession::HandleNeighborhoodCharterFinalize(WorldPackets::Neighborhood::NeighborhoodCharterFinalize const& /*neighborhoodCharterFinalize*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_CHARTER_NEIGHBORHOOD))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_CHARTER_FINALIZE received for player {}",
        player->GetGUID().ToString());

    // Load charter from DB using player's GUID counter as charter ID
    uint64 charterId = static_cast<uint64>(player->GetGUID().GetCounter());

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER);
    stmt->setUInt64(0, charterId);
    PreparedQueryResult charterResult = CharacterDatabase.Query(stmt);

    if (!charterResult)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_GENERIC_FAILURE);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterFinalize: No charter found for player {}",
            player->GetGUID().ToString());
        return;
    }

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER_SIGNATURES);
    stmt->setUInt64(0, charterId);
    PreparedQueryResult sigResult = CharacterDatabase.Query(stmt);

    NeighborhoodCharter charter(charterId, ObjectGuid::Empty);
    if (!charter.LoadFromDB(charterResult, sigResult))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_DB_ERROR);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterFinalize: Failed to load charter {} from DB",
            charterId);
        return;
    }

    // Verify enough signatures
    if (!charter.HasEnoughSignatures())
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_MORE_SIGNATURES_NEEDED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterFinalize: Charter {} has only {}/{} signatures",
            charterId, charter.GetSignatureCount(), MIN_CHARTER_SIGNATURES);
        return;
    }

    // The district is checked again for her faction, which may have changed since the charter was written, and the
    // neighborhood takes her faction.
    if (HousingResult mapCheck = sHousingMgr.CheckNeighborhoodFoundingMap(charter.GetNeighborhoodMapID(), player->GetTeam());
        mapCheck != HOUSING_RESULT_SUCCESS)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(mapCheck);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterFinalize: {} may not found a neighborhood on NeighborhoodMap {} (result {})",
            player->GetGUID().ToString(), charter.GetNeighborhoodMapID(), uint32(mapCheck));
        return;
    }

    // Create neighborhood from charter data
    Neighborhood* neighborhood = sNeighborhoodMgr.CreateNeighborhood(
        player->GetGUID(),
        charter.GetName(),
        charter.GetNeighborhoodMapID(),
        HousingMgr::GetNeighborhoodFactionForTeam(player->GetTeam())
    );

    if (neighborhood)
    {
        // Clean up charter from DB, straight away for the same reason as the edit: its signers are free to sign
        // another charter as soon as this one is gone.
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        NeighborhoodCharter::DeleteFromDB(charterId, trans);
        CharacterDatabase.DirectCommitTransaction(trans);

        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "Player {} finalized charter '{}', created neighborhood {}",
            player->GetGUID().ToString(), charter.GetName(),
            neighborhood->GetGuid().ToString());
    }
    else
    {
        WorldPackets::Neighborhood::NeighborhoodCharterUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_DB_ERROR);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "Player {} failed to finalize charter '{}' - neighborhood creation failed",
            player->GetGUID().ToString(), charter.GetName());
    }
}

void WorldSession::HandleNeighborhoodCharterAddSignature(WorldPackets::Neighborhood::NeighborhoodCharterAddSignature const& neighborhoodCharterAddSignature)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_CHARTER_NEIGHBORHOOD))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterAddSignatureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_CHARTER_ADD_SIGNATURECharterGuid: {}",
        neighborhoodCharterAddSignature.CharterGuid.ToString());

    // CharterGuid counter maps to charter DB ID
    uint64 charterId = neighborhoodCharterAddSignature.CharterGuid.GetCounter();

    // Only sign a charter this session was invited to sign. Session-scoped, so a
    // relog means the requester has to ask again - a signature request is an
    // in-the-moment offer, and nothing about it is persisted.
    if (!HasPendingCharterSignatureRequest(charterId))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterAddSignatureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterAddSignature: Player {} was never asked to sign charter {}",
            player->GetGUID().ToString(), charterId);
        return;
    }

    // Load charter from DB
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER);
    stmt->setUInt64(0, charterId);
    PreparedQueryResult charterResult = CharacterDatabase.Query(stmt);

    if (!charterResult)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterAddSignatureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_GENERIC_FAILURE);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterAddSignature: Charter {} not found in DB",
            charterId);
        return;
    }

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER_SIGNATURES);
    stmt->setUInt64(0, charterId);
    PreparedQueryResult sigResult = CharacterDatabase.Query(stmt);

    NeighborhoodCharter charter(charterId, ObjectGuid::Empty);
    if (!charter.LoadFromDB(charterResult, sigResult))
    {
        WorldPackets::Neighborhood::NeighborhoodCharterAddSignatureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_DB_ERROR);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterAddSignature: Failed to load charter {} from DB",
            charterId);
        return;
    }

    // One signature per Battle.net account, never the creator's, and one open charter per account.
    bool signedAnotherCharter = false;
    {
        CharacterDatabasePreparedStatement* signedStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_NEIGHBORHOOD_CHARTER_SIGNED_BY_ACCOUNT);
        signedStmt->setUInt32(0, GetBattlenetAccountId());
        if (PreparedQueryResult signedResult = CharacterDatabase.Query(signedStmt))
        {
            do
            {
                if (signedResult->Fetch()[0].GetUInt64() != charterId)
                    signedAnotherCharter = true;
            } while (signedResult->NextRow());
        }
    }

    HousingResult signResult = charter.AddSignature(player->GetGUID(), GetBattlenetAccountId(), signedAnotherCharter);
    if (signResult != HOUSING_RESULT_SUCCESS)
    {
        WorldPackets::Neighborhood::NeighborhoodCharterAddSignatureResponse response;
        response.Result = static_cast<uint8>(signResult);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCharterAddSignature: Player {} could not sign charter {} (result {})",
            player->GetGUID().ToString(), charterId, uint32(signResult));
        return;
    }

    // One invitation, one signature. Without consuming it, a signer whose
    // signature is later dropped by a charter edit could re-sign unasked.
    ClearPendingCharterSignatureRequest(charterId);

    WorldPackets::Neighborhood::NeighborhoodCharterAddSignatureResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.CharterGuid = neighborhoodCharterAddSignature.CharterGuid;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "Player {} signed charter {} ({}/{} signatures)",
        player->GetGUID().ToString(), charterId,
        charter.GetSignatureCount(), MIN_CHARTER_SIGNATURES);
}

void WorldSession::HandleNeighborhoodCharterSendSignatureRequest(WorldPackets::Neighborhood::NeighborhoodCharterSendSignatureRequest const& neighborhoodCharterSendSignatureRequest)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_CHARTER_NEIGHBORHOOD))
    {
        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure response;
        response.FailureType = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_CHARTER_SEND_SIGNATURE_REQUEST TargetPlayerGuid: {}",
        neighborhoodCharterSendSignatureRequest.TargetPlayerGuid.ToString());

    // Validate target player is online and reachable
    Player* targetPlayer = ObjectAccessor::FindPlayer(neighborhoodCharterSendSignatureRequest.TargetPlayerGuid);
    if (!targetPlayer)
    {
        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure response;
        response.FailureType = static_cast<uint8>(HOUSING_RESULT_PLAYER_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Send signature request notification to the target player's client
    uint64 charterId = static_cast<uint64>(player->GetGUID().GetCounter());
    WorldPackets::Neighborhood::NeighborhoodCharterSignRequest signRequest;
    signRequest.CharterGuid = ObjectGuid::Create<HighGuid::Housing>(0, 0, 0, charterId);
    targetPlayer->SendDirectMessage(signRequest.Write());

    // Record that this player was actually asked. ADD_SIGNATURE takes the charter
    // id straight from the client and charter ids are creator GUID counters, so without
    // this the invite step is decorative and any charter can be signed by anyone who
    // enumerates ids.
    if (WorldSession* targetSession = targetPlayer->GetSession())
        targetSession->AddPendingCharterSignatureRequest(charterId);

    // Acknowledge to the requester that the signature request was sent
    WorldPackets::Neighborhood::NeighborhoodCharterAddSignatureResponse ackResponse;
    ackResponse.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(ackResponse.Write());

    TC_LOG_DEBUG("housing", "Player {} requested signature from {} for charter {}",
        player->GetGUID().ToString(), targetPlayer->GetGUID().ToString(), charterId);
}

// ============================================================
// Neighborhood Management System
// ============================================================

void WorldSession::HandleNeighborhoodUpdateName(WorldPackets::Neighborhood::NeighborhoodUpdateName const& neighborhoodUpdateName)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_UPDATE_NAME NeighborhoodGuid: {}, NewName: {}",
        neighborhoodGuid.ToString(), neighborhoodUpdateName.NewName);

    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodUpdateName: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    // Only owner or manager can rename
    if (!neighborhood->IsOwner(player->GetGUID()) && !neighborhood->IsManager(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodUpdateName: Player {} lacks permission for neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGuid.ToString());
        return;
    }

    // Validate name
    if (neighborhoodUpdateName.NewName.empty() || neighborhoodUpdateName.NewName.length() > HOUSING_MAX_NAME_LENGTH)
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_INVALID_NEIGHBORHOOD_NAME);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodUpdateName: Invalid name length");
        return;
    }

    if (!ObjectMgr::IsValidCharterName(neighborhoodUpdateName.NewName) || sObjectMgr->IsReservedName(neighborhoodUpdateName.NewName))
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_FILTER_REJECTED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodUpdateName: Name rejected by filter");
        return;
    }

    // One rename per neighborhood in HOUSING_NEIGHBORHOOD_RENAME_COOLDOWN seconds, so renames cannot keep the server
    // sending the packets below to everyone who knows the neighborhood.
    time_t const now = GameTime::GetGameTime();
    if (neighborhood->GetLastRenameTime() && now < neighborhood->GetLastRenameTime() + time_t(HOUSING_NEIGHBORHOOD_RENAME_COOLDOWN))
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_TOO_MANY_REQUESTS);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodUpdateName: neighborhood {} was renamed less than {} seconds ago",
            neighborhoodGuid.ToString(), HOUSING_NEIGHBORHOOD_RENAME_COOLDOWN);
        return;
    }

    neighborhood->SetName(neighborhoodUpdateName.NewName);
    neighborhood->SetLastRenameTime(now);

    // SMSG_INVALIDATE_NEIGHBORHOOD (0x5F0008) is the neighborhood twin of SMSG_INVALIDATE_PLAYER (0x5F0007): the
    // 12.0.7 dispatcher reads one PackedGUID and drops the client's cached record for it. It goes to the players who
    // hold that record because they belong to the neighborhood or stand in it, not to the whole realm; anyone else
    // asks for the name the next time they need it (HandleQueryNeighborhoodInfo).
    WorldPackets::Housing::InvalidateNeighborhood invalidateRecord;
    invalidateRecord.NeighborhoodGuid = neighborhoodGuid;
    WorldPacket const* invalidatePacket = invalidateRecord.Write();
    std::unordered_set<ObjectGuid> invalidated;

    // Broadcast name invalidation and update notification to ALL neighborhood members
    for (auto const& member : neighborhood->GetMembers())
    {
        if (Player* memberPlayer = ObjectAccessor::FindPlayer(member.PlayerGuid))
        {
            if (invalidated.insert(memberPlayer->GetGUID()).second)
                memberPlayer->SendDirectMessage(invalidatePacket);

            WorldPackets::Housing::InvalidateNeighborhoodName invalidate;
            invalidate.NeighborhoodGuid = neighborhoodGuid;
            memberPlayer->SendDirectMessage(invalidate.Write());

            // 12.0.5: moved from SMSG_NEIGHBORHOOD_UPDATE_NAME_NOTIFICATION (0x5C0004)
            // to SMSG_HOUSING_SVCS_NEIGHBORHOOD_UPDATE_NAME_NOTIFICATION (0x540023).
            // IDA-verified wire (sub_7FF75C1EA710 case 0x540023): ObjectGuid + string.
            WorldPackets::Housing::HousingSvcsNeighborhoodUpdateNameNotification nameNotification;
            nameNotification.NeighborhoodGuid = neighborhoodGuid;
            nameNotification.NewName = neighborhoodUpdateName.NewName;
            memberPlayer->SendDirectMessage(nameNotification.Write());
        }
    }

    // Visitors standing in the neighborhood hold its name too.
    uint32 const worldMapId = sHousingMgr.GetWorldMapIdByNeighborhoodMapId(neighborhood->GetNeighborhoodMapID());
    if (Map* neighborhoodMap = sMapMgr->FindMap(worldMapId, uint32(neighborhoodGuid.GetCounter())))
        for (MapReference const& ref : neighborhoodMap->GetPlayers())
            if (Player* visitor = ref.GetSource())
                if (invalidated.insert(visitor->GetGUID()).second)
                    visitor->SendDirectMessage(invalidatePacket);

    WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    // The guild's rename notification, when this is the guild's neighborhood.
    if (Guild* guild = neighborhood->GetGuildId() ? sGuildMgr->GetGuildById(neighborhood->GetGuildId()) : nullptr)
    {
        WorldPackets::Housing::HousingSvcsGuildRenameNeighborhoodNotification guildNotification;
        guildNotification.NewName = neighborhoodUpdateName.NewName;
        guild->BroadcastPacket(guildNotification.Write());
    }

    // Refresh NeighborhoodMirrorData on all online members' Account entities
    neighborhood->RefreshMirrorDataForOnlineMembers();

    TC_LOG_DEBUG("housing", "Neighborhood {} renamed to '{}' by player {}",
        neighborhoodGuid.ToString(), neighborhoodUpdateName.NewName,
        player->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodSetPublicFlag(WorldPackets::Neighborhood::NeighborhoodSetPublicFlag const& neighborhoodSetPublicFlag)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_SET_PUBLIC_FLAGNeighborhoodGuid: {}, IsPublic: {}",
        neighborhoodSetPublicFlag.NeighborhoodGuid.ToString(), neighborhoodSetPublicFlag.IsPublic);

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(neighborhoodSetPublicFlag.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodSetPublicFlag: Neighborhood {} not found",
            neighborhoodSetPublicFlag.NeighborhoodGuid.ToString());
        return;
    }

    // Only owner or manager can change visibility
    if (!neighborhood->IsOwner(player->GetGUID()) && !neighborhood->IsManager(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodSetPublicFlag: Player {} lacks permission for neighborhood {}",
            player->GetGUID().ToString(), neighborhoodSetPublicFlag.NeighborhoodGuid.ToString());
        return;
    }

    neighborhood->SetPublic(neighborhoodSetPublicFlag.IsPublic);

    WorldPackets::Neighborhood::NeighborhoodUpdateNameResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "Neighborhood {} set to {} by player {}",
        neighborhoodSetPublicFlag.NeighborhoodGuid.ToString(),
        neighborhoodSetPublicFlag.IsPublic ? "public" : "private",
        player->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodAddSecondaryOwner(WorldPackets::Neighborhood::NeighborhoodAddSecondaryOwner const& neighborhoodAddSecondaryOwner)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Neighborhood::NeighborhoodAddSecondaryOwnerResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_ADD_SECONDARY_OWNER NeighborhoodGuid: {}, PlayerGuid: {}",
        neighborhoodGuid.ToString(), neighborhoodAddSecondaryOwner.PlayerGuid.ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodAddSecondaryOwnerResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodAddSecondaryOwner: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    // Only owner can add managers
    if (!neighborhood->IsOwner(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodAddSecondaryOwnerResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodAddSecondaryOwner: Player {} is not owner of neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGuid.ToString());
        return;
    }

    HousingResult result = neighborhood->AddManager(neighborhoodAddSecondaryOwner.PlayerGuid);

    WorldPackets::Neighborhood::NeighborhoodAddSecondaryOwnerResponse response;
    response.PlayerGuid = neighborhoodAddSecondaryOwner.PlayerGuid;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    // Broadcast roster update and refresh mirror data for all online members
    if (result == HOUSING_RESULT_SUCCESS)
    {
        neighborhood->BroadcastMemberStatus(neighborhoodAddSecondaryOwner.PlayerGuid);

        neighborhood->RefreshMirrorDataForOnlineMembers();
    }

    TC_LOG_DEBUG("housing", "AddManager result: {} for player {} in neighborhood {}",
        uint32(result), neighborhoodAddSecondaryOwner.PlayerGuid.ToString(),
        neighborhoodGuid.ToString());
}

void WorldSession::HandleNeighborhoodRemoveSecondaryOwner(WorldPackets::Neighborhood::NeighborhoodRemoveSecondaryOwner const& neighborhoodRemoveSecondaryOwner)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Neighborhood::NeighborhoodRemoveSecondaryOwnerResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_REMOVE_SECONDARY_OWNER NeighborhoodGuid: {}, PlayerGuid: {}",
        neighborhoodGuid.ToString(), neighborhoodRemoveSecondaryOwner.PlayerGuid.ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodRemoveSecondaryOwnerResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodRemoveSecondaryOwner: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    // Only owner can remove managers
    if (!neighborhood->IsOwner(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodRemoveSecondaryOwnerResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodRemoveSecondaryOwner: Player {} is not owner of neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGuid.ToString());
        return;
    }

    HousingResult result = neighborhood->RemoveManager(neighborhoodRemoveSecondaryOwner.PlayerGuid);

    WorldPackets::Neighborhood::NeighborhoodRemoveSecondaryOwnerResponse response;
    response.PlayerGuid = neighborhoodRemoveSecondaryOwner.PlayerGuid;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    // Broadcast roster update and refresh mirror data for all online members
    if (result == HOUSING_RESULT_SUCCESS)
    {
        neighborhood->BroadcastMemberStatus(neighborhoodRemoveSecondaryOwner.PlayerGuid);

        neighborhood->RefreshMirrorDataForOnlineMembers();
    }

    TC_LOG_DEBUG("housing", "RemoveManager result: {} for player {} in neighborhood {}",
        uint32(result), neighborhoodRemoveSecondaryOwner.PlayerGuid.ToString(),
        neighborhoodGuid.ToString());
}

void WorldSession::HandleNeighborhoodInviteResident(WorldPackets::Neighborhood::NeighborhoodInviteResident const& neighborhoodInviteResident)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INVITE_RESIDENT NeighborhoodGuid: {}, PlayerGuid: {}",
        neighborhoodGuid.ToString(), neighborhoodInviteResident.PlayerGuid.ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodInviteResident: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    // Only owner or manager can invite
    if (!neighborhood->IsOwner(player->GetGUID()) && !neighborhood->IsManager(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodInviteResident: Player {} lacks permission for neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGuid.ToString());
        return;
    }

    HousingResult result = neighborhood->InviteResident(player->GetGUID(), neighborhoodInviteResident.PlayerGuid);

    WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
    response.Result = static_cast<uint8>(result);
    response.InviteeGuid = neighborhoodInviteResident.PlayerGuid;
    SendPacket(response.Write());

    // Notify the invitee that they received a neighborhood invite
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (Player* invitee = ObjectAccessor::FindPlayer(neighborhoodInviteResident.PlayerGuid))
        {
            WorldPackets::Neighborhood::NeighborhoodInviteNotification notification;
            notification.NeighborhoodGuid = neighborhoodGuid;
            invitee->SendDirectMessage(notification.Write());
        }
    }

    TC_LOG_DEBUG("housing", "InviteResident result: {} for player {} in neighborhood {}",
        uint32(result), neighborhoodInviteResident.PlayerGuid.ToString(),
        neighborhoodGuid.ToString());
}

void WorldSession::HandleNeighborhoodCancelInvitation(WorldPackets::Neighborhood::NeighborhoodCancelInvitation const& neighborhoodCancelInvitation)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Neighborhood::NeighborhoodCancelInvitationResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_CANCEL_INVITATION NeighborhoodGuid: {}, InviteeGuid: {}",
        neighborhoodGuid.ToString(), neighborhoodCancelInvitation.InviteeGuid.ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodCancelInvitationResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCancelInvitation: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    // Only owner or manager can cancel invitations
    if (!neighborhood->IsOwner(player->GetGUID()) && !neighborhood->IsManager(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodCancelInvitationResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodCancelInvitation: Player {} lacks permission for neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGuid.ToString());
        return;
    }

    HousingResult result = neighborhood->CancelInvitation(neighborhoodCancelInvitation.InviteeGuid);

    WorldPackets::Neighborhood::NeighborhoodCancelInvitationResponse response;
    response.Result = static_cast<uint8>(result);
    response.InviteeGuid = neighborhoodCancelInvitation.InviteeGuid;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CancelInvitation result: {} for invitee {} in neighborhood {}",
        uint32(result), neighborhoodCancelInvitation.InviteeGuid.ToString(),
        neighborhoodGuid.ToString());
}

void WorldSession::HandleNeighborhoodPlayerDeclineInvite(WorldPackets::Neighborhood::NeighborhoodPlayerDeclineInvite const& neighborhoodPlayerDeclineInvite)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_PLAYER_DECLINE_INVITE NeighborhoodGuid: {}",
        neighborhoodPlayerDeclineInvite.NeighborhoodGuid.ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(neighborhoodPlayerDeclineInvite.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodDeclineInvitationResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodPlayerDeclineInvite: Neighborhood {} not found",
            neighborhoodPlayerDeclineInvite.NeighborhoodGuid.ToString());
        return;
    }

    HousingResult result = neighborhood->DeclineInvitation(player->GetGUID());

    WorldPackets::Neighborhood::NeighborhoodDeclineInvitationResponse response;
    response.Result = static_cast<uint8>(result);
    response.NeighborhoodGuid = neighborhoodPlayerDeclineInvite.NeighborhoodGuid;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "DeclineInvitation result: {} for player {} in neighborhood {}",
        uint32(result), player->GetGUID().ToString(),
        neighborhoodPlayerDeclineInvite.NeighborhoodGuid.ToString());
}

void WorldSession::HandleNeighborhoodPlayerGetInvite(WorldPackets::Neighborhood::NeighborhoodPlayerGetInvite const& /*neighborhoodPlayerGetInvite*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_PLAYER_GET_INVITE for player {}",
        player->GetGUID().ToString());

    // Client sends empty packet — search all neighborhoods for a pending invite to this player
    Neighborhood* foundNeighborhood = sNeighborhoodMgr.FindNeighborhoodWithPendingInvite(player->GetGUID());
    Neighborhood::PendingInvite const* foundInvite = nullptr;

    if (foundNeighborhood)
    {
        for (auto const& invite : foundNeighborhood->GetPendingInvites())
        {
            if (invite.InviteeGuid == player->GetGUID())
            {
                foundInvite = &invite;
                break;
            }
        }
    }

    WorldPackets::Neighborhood::NeighborhoodPlayerGetInviteResponse response;
    if (foundNeighborhood && foundInvite)
    {
        response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
        response.Entry.Timestamp = foundInvite->InviteTime;
        response.Entry.PlayerGuid = foundInvite->InviterGuid;
        response.Entry.HouseGuid = foundNeighborhood->GetGuid();
    }
    else
    {
        response.Result = static_cast<uint8>(HOUSING_RESULT_GENERIC_FAILURE);
    }
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "Player {} {} a pending invite",
        player->GetGUID().ToString(), foundNeighborhood ? "has" : "does not have");
}

void WorldSession::HandleNeighborhoodGetInvites(WorldPackets::Neighborhood::NeighborhoodGetInvites const& /*neighborhoodGetInvites*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_GET_INVITES for player {}",
        player->GetGUID().ToString());

    // Client sends empty packet — derive neighborhood from player's housing context
    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Neighborhood::NeighborhoodGetInvitesResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();
    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodGetInvitesResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodGetInvites: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    // Only owner or manager can view all pending invites
    if (!neighborhood->IsOwner(player->GetGUID()) && !neighborhood->IsManager(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodGetInvitesResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodGetInvites: Player {} lacks permission for neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGuid.ToString());
        return;
    }

    std::vector<Neighborhood::PendingInvite> const& invites = neighborhood->GetPendingInvites();

    WorldPackets::Neighborhood::NeighborhoodGetInvitesResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.Invites.reserve(invites.size());
    for (auto const& invite : invites)
    {
        WorldPackets::Housing::InviteEntry entry;
        entry.Timestamp = invite.InviteTime;
        entry.PlayerGuid = invite.InviteeGuid;
        entry.HouseGuid = ObjectGuid::Empty; // invitees don't have houses yet
        response.Invites.push_back(entry);
    }
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "Neighborhood {} has {} pending invites sent",
        neighborhoodGuid.ToString(), uint32(invites.size()));
}

void WorldSession::HandleNeighborhoodBuyHouse(WorldPackets::Neighborhood::NeighborhoodBuyHouse const& neighborhoodBuyHouse)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_BUY_HOUSE))
    {
        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_BUY_HOUSE CornerstoneGuid: {}, HouseGuid: {}",
        neighborhoodBuyHouse.CornerstoneGuid.ToString(), neighborhoodBuyHouse.HouseGuid.ToString());

    // The buy packet names only the cornerstone. The plot comes from that
    // cornerstone, which must be within the player's reach, and it must be the
    // same cornerstone and plot the purchase window was opened on.
    Neighborhood* neighborhood = nullptr;
    NeighborhoodPlotData const* plot = ResolveCornerstone(player, neighborhoodBuyHouse.CornerstoneGuid, neighborhood);
    if (!plot || _lastCornerstoneGuid != neighborhoodBuyHouse.CornerstoneGuid
        || _lastClientPlotIndex != uint32(plot->PlotIndex))
    {
        TC_LOG_DEBUG("housing", "HandleNeighborhoodBuyHouse: refused for player {}: cornerstone {} (plot {}), window was opened on cornerstone {} (plot {}){}",
            player->GetGUID().ToString(), neighborhoodBuyHouse.CornerstoneGuid.ToString(),
            plot ? std::to_string(plot->PlotIndex) : std::string("none"),
            _lastCornerstoneGuid.ToString(), _lastClientPlotIndex,
            plot ? ", which is not the same cornerstone and plot" : ", and the cornerstone check failed");

        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_INVALID_INTERACTION);
        SendPacket(response.Write());
        return;
    }

    uint8 const resolvedPlotIndex = uint8(plot->PlotIndex);

    // Refuse a plot another player holds before joining the neighborhood.
    if (!neighborhood->GetPlotReserverOther(resolvedPlotIndex, player->GetGUID()).IsEmpty())
    {
        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PLOT_RESERVED);
        SendPacket(response.Write());
        return;
    }

    auto worldMapOf = [](Neighborhood const* n) -> int32
    {
        NeighborhoodMapData const* mapData = n ? sHousingMgr.GetNeighborhoodMapData(n->GetNeighborhoodMapID()) : nullptr;
        return mapData ? mapData->MapID : 0;
    };
    int32 const districtWorldMapId = worldMapOf(neighborhood);

    // One house per district: a Battle.net account may own one house in Founder's Point and one in Razorwind Shores,
    // two in all (Blizzard Watch, https://blizzardwatch.com/2025/12/02/get-house-world-warcraft/; the wiki's Housing
    // page). A district is a neighborhood world map; guild and charter neighborhoods are on the same two maps.
    // The rule and the cap below count every house of the account, including one another game account of the same
    // Battle.net account bought after this character logged in. A packed house stands in no district.
    std::vector<Housing::AccountHouse> const accountHouses = Housing::GetAccountHouses(GetBattlenetAccountId());
    {
        std::vector<int32> ownedHouseWorldMapIds;
        for (Housing::AccountHouse const& owned : accountHouses)
            if (!owned.Packed)
                ownedHouseWorldMapIds.push_back(worldMapOf(sNeighborhoodMgr.GetNeighborhood(owned.NeighborhoodGuid)));

        if (Housing::AccountOwnsHouseInDistrict(ownedHouseWorldMapIds, districtWorldMapId))
        {
            WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
            response.Result = static_cast<uint8>(HOUSING_RESULT_MORE_HOUSE_SLOTS_NEEDED);
            SendPacket(response.Write());

            TC_LOG_DEBUG("housing", "HandleNeighborhoodBuyHouse: Player {}'s account already owns a house in the district of neighborhood {}",
                player->GetGUID().ToString(), neighborhood->GetGuid().ToString());
            return;
        }
    }

    // A relinquished house was packed with its layout, and the next purchase unpacks it onto the new plot: the
    // relinquish dialog says "Your layout will be saved, and you can purchase a new House to automatically import it"
    // (GlobalStrings HOUSING_HOUSE_SETTINGS_ABANDON_DESCRIPTION), and the cornerstone has an Import purchase mode.
    // The account's packed house from this district is the one unpacked here.
    // Housing.MaxHousesPerAccount is a safety cap kept under the district rule, which already limits an account to
    // two houses. 0 = no cap. Packed houses count, as each keeps its slot. At the cap, a packed house from the other
    // district is unpacked here instead of building a new one, because unpacking adds no house.
    uint32 const maxHouses = sWorld->getIntConfig(CONFIG_HOUSING_MAX_HOUSES_PER_ACCOUNT);
    bool const atHouseCap = maxHouses && accountHouses.size() >= maxHouses;
    Housing::AccountHouse const* packedHouse = nullptr;
    {
        std::vector<Housing::AccountHouse const*> packedHouses;
        std::vector<int32> packedHouseWorldMapIds;
        for (Housing::AccountHouse const& owned : accountHouses)
        {
            if (!owned.Packed)
                continue;
            packedHouses.push_back(&owned);
            packedHouseWorldMapIds.push_back(worldMapOf(sNeighborhoodMgr.GetNeighborhood(owned.FormerNeighborhoodGuid)));
        }

        int32 const unpack = Housing::ChoosePackedHouseToUnpack(packedHouseWorldMapIds, districtWorldMapId, atHouseCap);
        if (unpack >= 0)
            packedHouse = packedHouses[unpack];
    }

    if (!packedHouse && atHouseCap)
    {
        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_MORE_HOUSE_SLOTS_NEEDED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodBuyHouse: Player {} at the account house cap ({}/{})",
            player->GetGUID().ToString(), accountHouses.size(), maxHouses);
        return;
    }

    // The first house of a Battle.net account is free: in the retail capture an established account bought its first
    // house and its money did not drop (hbcd3 191596 and 399489: 15842437 copper; the next change, 1324289:
    // 15873337, is the +30900 quest reward). Every other purchase costs the plot's price (the wiki: 1000 gold for a
    // second house; NeighborhoodPlot Cost is 10,000,000 copper on every district plot). An account that has any
    // house row, standing or packed, pays: buying back a packed first house costs the plot's price, as the owner
    // chose, so buying and relinquishing cannot be repeated to gain anything. The first house is also recorded for
    // the account, so one that is gone does not make the account a first buyer again.
    HousingDecorStore* decorStore = player->GetHousingDecorStore();
    bool const firstHouse = decorStore && Housing::IsFirstHouse(decorStore->HasHadFirstHouse(), accountHouses.size());
    uint64 const price = Housing::GetPurchasePrice(firstHouse, plot->Cost);
    if (!player->HasEnoughMoney(price))
    {
        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_CANNOT_AFFORD);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodBuyHouse: Player {} cannot afford house (need {} copper, has {})",
            player->GetGUID().ToString(), price, player->GetMoney());
        return;
    }

    // Buying a plot joins its neighborhood. AddResident performs no faction or invite checks (unlike
    // InviteResident), so they are made here, before a wrong-faction or uninvited player could join a private or
    // faction-locked neighborhood simply by buying a plot.
    bool const joinAsResident = !neighborhood->IsMember(player->GetGUID());
    if (joinAsResident)
    {
        HousingResult const joinResult = Neighborhood::CheckResidentJoin(neighborhood->IsServerPublic(), neighborhood->IsPublic(),
            neighborhood->GetFactionRestriction(), neighborhood->GetGuildId(), player->GetTeam(), player->GetGuildId(),
            neighborhood->HasPendingInvite(player->GetGUID()) || neighborhood->IsManager(player->GetGUID()) || neighborhood->IsOwner(player->GetGUID()));
        if (joinResult != HOUSING_RESULT_SUCCESS)
        {
            WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
            response.Result = static_cast<uint8>(joinResult);
            SendPacket(response.Write());

            TC_LOG_DEBUG("housing", "HandleNeighborhoodBuyHouse: Player {} may not join neighborhood '{}' (result {})",
                player->GetGUID().ToString(), neighborhood->GetName(), uint32(joinResult));
            return;
        }
    }

    // Every check is done. Take the plot and build the house in memory first; nothing is written until the plot,
    // the money and the house are saved together in one transaction below.
    Neighborhood::PlotClaim claim;
    HousingResult result = neighborhood->TryReservePlot(player->GetGUID(), resolvedPlotIndex, joinAsResident, claim);
    if (result != HOUSING_RESULT_SUCCESS)
    {
        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(result);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodBuyHouse: plot {} refused for player {} (result {})",
            resolvedPlotIndex, player->GetGUID().ToString(), uint32(result));
        return;
    }

    // Use the server's canonical neighborhood GUID, NOT the client-supplied GUID.
    Housing* housing = packedHouse
        ? player->UnpackHousing(packedHouse->HouseGuid, neighborhood->GetGuid(), resolvedPlotIndex, price)
        : player->CreateHousing(neighborhood->GetGuid(), resolvedPlotIndex, price);
    if (!housing)
    {
        neighborhood->UndoPlotClaim(claim);

        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_GENERIC_FAILURE);
        SendPacket(response.Write());

        TC_LOG_ERROR("housing", "HandleNeighborhoodBuyHouse: could not {} a house for player {} on plot {} of neighborhood {}",
            packedHouse ? "unpack" : "build", player->GetGUID().ToString(), resolvedPlotIndex, neighborhood->GetGuid().ToString());
        return;
    }

    // The plot, the money, the house and the first house's starter decor as one unit, so a crash cannot leave the plot
    // taken without a house, or the money gone without either. The starter decor comes with the account's first
    // house only; an unpacked house brings its own decor.
    neighborhood->ClearReservation(player->GetGUID());
    player->ModifyMoney(-static_cast<int64>(price));
    std::vector<Housing::AcquiredDecor> starterDecor;
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        neighborhood->AppendPlotClaim(claim, trans);
        player->SaveInventoryAndGoldToDB(trans);
        if (firstHouse && !packedHouse)
        {
            starterDecor = housing->PlaceStarterDecor(trans);
            decorStore->RecordFirstHouse(trans);
        }
        housing->SaveToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
    }
    neighborhood->CompletePlotClaim(claim);

    neighborhood->UpdatePlotHouseInfo(resolvedPlotIndex, housing->GetHouseGuid(), GetBattlenetAccountGUID(), housing->GetDatabaseId());
    neighborhood->UpdatePlotHouseMirror(*housing);

    // Retail's order after a purchase (hbcd3 Numbers 13844-13869): the first-time decor messages, the plot's world
    // state, one update with the cornerstone and the house's room, the House Purchase Cover Spell, the buy reply, then
    // the level and favor.
    // One first-time message per starter piece, repeats included, in the order the pieces were made (hbcd3
    // 1299364-1299534: 1700, 81, 2549, 10952, 8910, 1700, 2549). No capture shows the Alliance set, so an Alliance
    // purchase sends none.
    for (Housing::AcquiredDecor const& acquired : starterDecor)
    {
        if (acquired.Announced)
        {
            WorldPackets::Housing::HousingFirstTimeDecorAcquisition firstTime;
            firstTime.DecorEntryID = acquired.DecorEntryId;
            SendPacket(firstTime.Write());
        }
        Housing::OnDecorAcquired(player, acquired.DecorEntryId, acquired.FirstOwned);
    }

    // Two places differ from retail here. The cornerstone's new state goes out with the map's next object update,
    // after the buy reply, not in the same update as the house. And game's force cast runs the cover spell's child
    // spells (and 1253555's refusal) before the buy reply, where retail sent them after the reply and the first
    // level and favor packet (Numbers 13870-13917).
    if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()); housingMap && housingMap->GetNeighborhood() == neighborhood)
    {
        // NeighborhoodPlot.WorldState to 1 (29729 for plot 13, hbcd3 Number 13863) and the cornerstone to owned.
        housingMap->SetPlotOwnershipState(resolvedPlotIndex, true);

        // The house is built from its own fixtures, as a character's arrival builds it: a new house has the starter
        // pieces and front door Housing::Create gave it, and an unpacked house brings its own. An unpacked house also
        // brings its placed decor.
        bool const built = housingMap->SpawnHouseFromState(resolvedPlotIndex, *housing);
        if (packedHouse)
            housingMap->SpawnAllDecorForPlot(resolvedPlotIndex, housing);
        TC_LOG_DEBUG("housing", "HandleNeighborhoodBuyHouse: SpawnHouseFromState for plot {}: {}",
            resolvedPlotIndex, built ? "built" : "FAILED");

        // Every piece of the house reaches the characters near the plot through ordinary visibility as it is added to
        // the map, parents before the door. Retail sent the door and the pieces in one update, door first (hbcd3
        // Number 14144); that order is not reproduced yet.
    }
    else
    {
        TC_LOG_ERROR("housing", "HandleNeighborhoodBuyHouse: Player {} is not on the map of neighborhood {}; the house exterior is not spawned",
            player->GetGUID().ToString(), neighborhood->GetGuid().ToString());
    }

    // Retail casts 1253572 on the buyer (hbcd3 Numbers 13866-13867). Its effects cast 1248306 "[DNT] House Purchased"
    // (kill credit 248858, the "Acquire a house" objective of "My First Home", 91863), 1253658 (quest complete 92486),
    // 1253555 (refused by its script, as retail refused it) and the scene 1260705.
    player->CastSpell(player, SPELL_HOUSE_PURCHASE_COVER, true);

    // Deliberately NOT marking the 256 server tutorial flags as seen here (it used to set all of them).
    // Buying a house is precisely when the housing tutorial should START, so suppressing every tutorial at
    // that moment was backwards. The client tracks its own progress via CMSG_TUTORIAL.

    {
        WorldPackets::Neighborhood::NeighborhoodBuyHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
        // The bought house: plot and setting flags as retail's reply (0D 20: plot 13, flags 32; hbcd3 1299763).
        housing->FillHouseEntry(response.House);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "SMSG_NEIGHBORHOOD_BUY_HOUSE_RESPONSE Result={}, PlotId={}, HouseGuid={}, CosmeticOwner={}, Settings=0x{:X}",
            uint32(response.Result), response.House.PlotID, response.House.HouseGUID.ToString(),
            response.House.CosmeticOwnerGUID.ToString(), response.House.HouseSettingFlags);
    }

    // Level and favor in retail's two packets (hbcd3 1299772, Number 13869, and 1301305, Number 13925): first
    // change -1 and reason -1 with the house at level -1 and its favor, then the favor as the change with reason 1
    // and the house at -1 and -1. Retail carried 1080 as the favor; where that number comes from is not known, so the
    // house's own favor is sent in its place.
    {
        int32 const favor = static_cast<int32>(housing->GetFavor());

        WorldPackets::Housing::HousingSvcsUpdateHousesLevelFavor favorState;
        favorState.Result = 0;
        favorState.ChangeAmount = uint32(-1);
        favorState.Reason = uint32(-1);
        auto& stateHouse = favorState.Houses.emplace_back();
        stateHouse.HouseGUID = housing->GetHouseGuid();
        stateHouse.HouseLevel = -1;
        stateHouse.FavorValue = favor;
        SendPacket(favorState.Write());

        WorldPackets::Housing::HousingSvcsUpdateHousesLevelFavor favorChange;
        favorChange.Result = 0;
        favorChange.ChangeAmount = uint32(favor);
        favorChange.Reason = 1;
        auto& changeHouse = favorChange.Houses.emplace_back();
        changeHouse.HouseGUID = housing->GetHouseGuid();
        changeHouse.HouseLevel = -1;
        changeHouse.FavorValue = -1;
        SendPacket(favorChange.Write());
    }

    // The buyer now has a house on a plot: the other members' rosters need it.
    neighborhood->BroadcastRoster(player->GetGUID());

    // Send guild notification for house addition
    if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
    {
        WorldPackets::Housing::HousingSvcsGuildAddHouseNotification notification;
        housing->FillHouseEntry(notification.House);
        guild->BroadcastPacket(notification.Write());
    }

    // Refresh NeighborhoodMirrorData (Houses[] changed) on all online members
    neighborhood->RefreshMirrorDataForOnlineMembers();

    // The account's decor storage and the new house's budgets, in update fields. No storage reply goes with them:
    // retail sent one only in answer to CMSG_HOUSING_DECOR_REQUEST_STORAGE (hbcd3 Numbers 3263/3265 and 19645/19647),
    // and none in the purchase (Numbers 13842-13926).
    {
        player->PushHousingDecorStorage();
        housing->SyncUpdateFields();

        // Send Account + HousingPlayerHouseEntity together so budget data
        // accompanies storage data for the client's decor count display.
        HousingPlayerHouseEntity& houseEntity = GetHousingPlayerHouseEntity(housing->GetHouseGuid());
        GetBattlenetAccount().BuildUpdateChangesMask();
        houseEntity.BuildUpdateChangesMask();

        UpdateData updateData(player->GetMapId());
        WorldPacket updatePacket;

        if (player->HaveAtClient(&GetBattlenetAccount()))
            GetBattlenetAccount().BuildValuesUpdateBlockForPlayer(&updateData, player);
        else
        {
            GetBattlenetAccount().BuildCreateUpdateBlockForPlayer(&updateData, player);
            player->m_clientSessionEntityGUIDs.insert(GetBattlenetAccount().GetGUID());
        }

        if (player->HaveAtClient(&houseEntity))
            houseEntity.BuildValuesUpdateBlockForPlayer(&updateData, player);
        else
        {
            houseEntity.BuildCreateUpdateBlockForPlayer(&updateData, player);
            player->m_clientSessionEntityGUIDs.insert(houseEntity.GetGUID());
        }

        updateData.BuildPacket(&updatePacket);
        player->SendDirectMessage(&updatePacket);

        GetBattlenetAccount().ClearUpdateMask(true);
        houseEntity.ClearUpdateMask(true);
    }

    // Her first house makes its neighborhood her active one (hbcd3 1305140: NeighborhoodGUID and the endeavor there
    // are set right after the purchase).
    player->UpdateInitiativeComponent();

    TC_LOG_INFO("housing", "Player {} {} a house on plot {} in neighborhood '{}' for {} copper",
        player->GetGUID().ToString(), packedHouse ? "unpacked" : "bought", resolvedPlotIndex, neighborhood->GetName(), price);

    // Check if neighborhoods need expansion after plot purchase
    sNeighborhoodMgr.CheckAndExpandNeighborhoods();
}

void WorldSession::HandleNeighborhoodMoveHouse(WorldPackets::Neighborhood::NeighborhoodMoveHouse const& neighborhoodMoveHouse)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_MOVE_HOUSE))
    {
        WorldPackets::Neighborhood::NeighborhoodMoveHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_MOVE_HOUSE CornerstoneGuid: {}, HouseGuid: {}",
        neighborhoodMoveHouse.CornerstoneGuid.ToString(), neighborhoodMoveHouse.HouseGuid.ToString());

    // The move packet names the cornerstone of the destination plot. It must be
    // within the player's reach, and it must be the same cornerstone and plot
    // the window was opened on.
    Neighborhood* neighborhood = nullptr;
    NeighborhoodPlotData const* targetPlot = ResolveCornerstone(player, neighborhoodMoveHouse.CornerstoneGuid, neighborhood);
    if (!targetPlot || _lastCornerstoneGuid != neighborhoodMoveHouse.CornerstoneGuid
        || _lastClientPlotIndex != uint32(targetPlot->PlotIndex))
    {
        TC_LOG_DEBUG("housing", "HandleNeighborhoodMoveHouse: refused for player {}: cornerstone {} (plot {}), window was opened on cornerstone {} (plot {}){}",
            player->GetGUID().ToString(), neighborhoodMoveHouse.CornerstoneGuid.ToString(),
            targetPlot ? std::to_string(targetPlot->PlotIndex) : std::string("none"),
            _lastCornerstoneGuid.ToString(), _lastClientPlotIndex,
            targetPlot ? ", which is not the same cornerstone and plot" : ", and the cornerstone check failed");

        WorldPackets::Neighborhood::NeighborhoodMoveHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_INVALID_INTERACTION);
        SendPacket(response.Write());
        return;
    }

    // The house the packet names, when the character's Battle.net account owns it and it stands in this
    // neighborhood — a client may not relocate another account's house.
    Housing* housing = player->GetHousingByGuid(neighborhoodMoveHouse.HouseGuid);
    if (!housing || housing->GetNeighborhoodGuid() != neighborhood->GetGuid())
    {
        WorldPackets::Neighborhood::NeighborhoodMoveHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodMoveHouse: Player {} HouseGuid mismatch — owns {}, CMSG sent {}",
            player->GetGUID().ToString(),
            housing ? housing->GetHouseGuid().ToString() : "<no house>",
            neighborhoodMoveHouse.HouseGuid.ToString());
        return;
    }

    uint8 const targetPlotIndex = uint8(targetPlot->PlotIndex);

    // Reject moving to the same plot the house already stands on (no-op).
    uint8 oldPlotIndex = housing->GetPlotIndex();
    if (oldPlotIndex == targetPlotIndex)
    {
        WorldPackets::Neighborhood::NeighborhoodMoveHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PLOT_NOT_VACANT);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodMoveHouse: target plot {} is the player's current plot",
            targetPlotIndex);
        return;
    }

    // A move charges nothing and pays nothing. Blizzard's 12.1 cornerstone code hides the cost in move mode because
    // moves are always free, and says the move job would have to change to take a cost
    // (Blizzard_HousingCornerstone.lua 130-131). The house keeps what was paid for it, since it is the same house.
    // There is no cooldown on moving again: its length is not known.
    // The roster entry's plot and the house row's plot are saved in one transaction.
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    HousingResult result = neighborhood->MoveHouse(housing->GetHouseGuid(), player->GetGUID(), targetPlotIndex, trans);

    WorldPackets::Neighborhood::NeighborhoodMoveHouseResponse response;
    response.Result = static_cast<uint8>(result);
    if (result == HOUSING_RESULT_SUCCESS)
    {
        // The 5-minute hold the player may have placed via the House Finder is
        // consumed by the actual move; clear it so the lock is released early.
        neighborhood->ClearReservation(player->GetGUID());

        // Update Housing::_plotIndex so all subsequent responses (HouseStatus,
        // HouseInfo, SyncUpdateFields, etc.) use the correct DB2 PlotIndex.
        {
            housing->SetPlotIndex(targetPlotIndex);
            housing->SavePlacement(trans);

            // Exterior decor is stored where it stands in the world, so it moves with the house to keep its place on
            // the plot, in the same transaction.
            Position fromPlot;
            Position toPlot;
            if (sHousingMgr.GetPlotRoomAnchor(neighborhood->GetNeighborhoodMapID(), oldPlotIndex, fromPlot)
                && sHousingMgr.GetPlotRoomAnchor(neighborhood->GetNeighborhoodMapID(), targetPlotIndex, toPlot))
                housing->MoveExteriorDecorBetweenPlots(fromPlot, toPlot, trans);
            else
                TC_LOG_ERROR("housing", "HandleNeighborhoodMoveHouse: plot {} or plot {} of neighborhood {} is not in NeighborhoodPlot, so the exterior decor of house {} keeps its old place",
                    oldPlotIndex, targetPlotIndex, neighborhood->GetGuid().ToString(), housing->GetHouseGuid().ToString());

            CharacterDatabase.CommitTransaction(trans);
            // The plot's copy of the house (used when the map loads the plot) takes the moved decor.
            neighborhood->UpdatePlotHouseMirror(*housing);
            housing->SyncUpdateFields();
            // Push the Housing/3 entity (HousingPlayerHouseEntity) to the client
            // as CREATE — the regular world-map plot icon resolves via entity
            // registry lookup of HouseGUID and the icon "self/friend/stranger"
            // chooser only re-evaluates when CREATE_OBJECT arrives. Without an
            // explicit re-push here, SyncUpdateFields just flips dirty bits and
            // the client's local entity copy stays at the old PlotIndex —
            // the new plot's icon stays "unowned" until the player re-logs.
            GetHousingPlayerHouseEntity(housing->GetHouseGuid()).SendCreateToPlayer(player);
        }

        // Despawn entities at old plot, respawn at new plot
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
        {
            if (oldPlotIndex != INVALID_PLOT_INDEX)
            {
                housingMap->DespawnAllDecorForPlot(oldPlotIndex);
                housingMap->DespawnAllMeshObjectsForPlot(oldPlotIndex);
                housingMap->DespawnRoomForPlot(oldPlotIndex);
                housingMap->DespawnHouseForPlot(oldPlotIndex);
                housingMap->SetPlotOwnershipState(oldPlotIndex, false);
            }

            housingMap->SetPlotOwnershipState(targetPlotIndex, true);
            if (Housing const* h = housing)
            {
                housingMap->SpawnHouseFromState(targetPlotIndex, *h);

                // Re-spawn the player's exterior decor at the new plot. DespawnAllDecorForPlot
                // (called above for the old plot) only removes the in-world entities — the
                // PlacedDecor records in the Housing object are preserved. Without this
                // matching SpawnAllDecorForPlot the decor stays gone after a move.
                // Decor is NOT returned to the chest; it follows the house.
                housingMap->SpawnAllDecorForPlot(targetPlotIndex, h);
            }
            else
            {
                TC_LOG_ERROR("housing", "HandleNeighborhoodMoveHouse: No Housing object for player — cannot spawn house at plot {}", targetPlotIndex);
            }
        }

        housing->FillHouseEntry(response.House);

        // The house moved to another plot: the other members' rosters need the new plot.
        neighborhood->BroadcastRoster(player->GetGUID());

        // Refresh NeighborhoodMirrorData (Houses[] changed — plot moved)
        neighborhood->RefreshMirrorDataForOnlineMembers();
    }
    // Sniff-verified (12.0.5 packet #13402, 40-byte SMSG_NEIGHBORHOOD_MOVE_HOUSE_RESPONSE):
    // the trailing PackedGUID is a copy of the moved house's HouseGuid, not an empty
    // transaction GUID. Re-emitting the same HouseGuid lets the client correlate the
    // response with its locally-tracked move-in-progress entry.
    response.MoveTransactionGuid = housing->GetHouseGuid();
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "MoveHouse result: {} from plot {} to plot {} via cornerstone {}",
        uint32(result), oldPlotIndex, targetPlotIndex,
        neighborhoodMoveHouse.CornerstoneGuid.ToString());
}

void WorldSession::HandleNeighborhoodOpenCornerstoneUI(WorldPackets::Neighborhood::NeighborhoodOpenCornerstoneUI const& neighborhoodOpenCornerstoneUI)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_OPEN_CORNERSTONE_UI PlotIndex(raw): {}, CornerstoneGuid: {}",
        neighborhoodOpenCornerstoneUI.PlotIndex, neighborhoodOpenCornerstoneUI.CornerstoneGuid.ToString());

    // The packet's GUID is the cornerstone the player clicked. Forget the last
    // window first, so a refused open cannot leave an older plot to buy.
    _lastCornerstoneGuid.Clear();
    _lastClientPlotIndex = INVALID_PLOT_INDEX;

    // Retail's client sends the plot of the cornerstone it clicked (PlotID 13
    // with cornerstone entry 457142 on Razorwind Shores, hbcd3 1295070-1295072),
    // so a plot that does not match the cornerstone in reach is refused.
    Neighborhood* neighborhood = nullptr;
    NeighborhoodPlotData const* plot = ResolveCornerstone(player, neighborhoodOpenCornerstoneUI.CornerstoneGuid, neighborhood);
    if (!plot || neighborhoodOpenCornerstoneUI.PlotIndex != uint32(plot->PlotIndex))
    {
        if (plot)
            TC_LOG_DEBUG("housing", "HandleNeighborhoodOpenCornerstoneUI: refused for player {}: client sent plot {} but cornerstone {} is plot {}",
                player->GetGUID().ToString(), neighborhoodOpenCornerstoneUI.PlotIndex,
                neighborhoodOpenCornerstoneUI.CornerstoneGuid.ToString(), plot->PlotIndex);
        else
            TC_LOG_DEBUG("housing", "HandleNeighborhoodOpenCornerstoneUI: refused for player {}: client sent plot {}, and the cornerstone check on {} failed",
                player->GetGUID().ToString(), neighborhoodOpenCornerstoneUI.PlotIndex,
                neighborhoodOpenCornerstoneUI.CornerstoneGuid.ToString());

        WorldPackets::Neighborhood::NeighborhoodOpenCornerstoneUIResponse response;
        response.PlotIndex = neighborhoodOpenCornerstoneUI.PlotIndex;
        response.CanPurchase = false;
        SendPacket(response.Write());
        return;
    }

    // Remembered for the buy or move that follows; neither packet carries a plot.
    uint32 const plotIndex = uint32(plot->PlotIndex);
    _lastClientPlotIndex = plotIndex;
    _lastCornerstoneGuid = neighborhoodOpenCornerstoneUI.CornerstoneGuid;

    // No neighborhood name goes out before this reply: retail sends it only after the client's
    // CMSG_QUERY_NEIGHBORHOOD_INFO, which follows the reply (hbcd3 1296244, 1296252, 1296259).

    // Look up ownership from the Neighborhood's plot info
    uint8 plotIdx = static_cast<uint8>(plotIndex);
    Neighborhood::PlotInfo const* plotInfo = neighborhood->GetPlotInfo(plotIdx);
    bool isOwned = plotInfo && !plotInfo->OwnerGuid.IsEmpty();

    // Values as in both retail replies (hbcd3 1296244, a vacant plot, 29 bytes; hbcd3 1563345, the player's own plot,
    // 44 bytes): the owner character and the house only when the plot is owned, cost 0, status 0, no cornerstone GUID,
    // every bit off and no alternate price. Both are the tutorial plot of Razorwind Shores; what retail sends for other
    // plots is not captured.
    WorldPackets::Neighborhood::NeighborhoodOpenCornerstoneUIResponse response;
    response.PlotIndex = plotIndex;
    response.NeighborhoodName = neighborhood->GetName();

    if (isOwned)
    {
        response.PlotOwnerGuid = plotInfo->OwnerGuid;
        response.HouseGuid = plotInfo->HouseGuid;
    }
    else
    {
        // Another player's reservation marks the plot with PurchaseStatus HOUSING_RESULT_PLOT_RESERVED (73); the
        // reserving player still gets 0. The 12.0.7 captures have no reserved plot, so this case is not checked
        // against retail.
        ObjectGuid otherReserver = neighborhood->GetPlotReserverOther(plotIdx, player->GetGUID());
        if (!otherReserver.IsEmpty())
        {
            response.PurchaseStatus = static_cast<uint8>(HOUSING_RESULT_PLOT_RESERVED);
            TC_LOG_DEBUG("housing",
                "OpenCornerstoneUI: plot {} is reserved by {}; marking PurchaseStatus=PLOT_RESERVED for viewer {}",
                plotIdx, otherReserver.ToString(), player->GetGUID().ToString());
        }
    }

    // If the player already owns a house in this neighborhood, embed it in the
    // response. The cornerstone Lua reads this as "you have a house here" and
    // flips the action button from Buy to Move. Without this the button stays
    // on Buy, which then routes to BUY_HOUSE and gets rejected by HandleNeighborhoodBuyHouse
    // (HOUSING_RESULT_INVALID_HOUSE — "player already has a house in neighborhood").
    // Only embed when the plot is actually actionable for this player (not owned
    // by anyone else and not reserved by anyone else). Neither retail reply covers
    // this case: the vacant plot was clicked before that player owned a house.
    if (!isOwned && response.PurchaseStatus == 0)
    {
        if (Housing const* myHousing = player->GetHousingForNeighborhood(neighborhood->GetGuid()))
            myHousing->FillHouseEntry(response.ExistingHouse.emplace());
    }
    WorldPacket const* pkt = response.Write();
    SendPacket(pkt);

    TC_LOG_DEBUG("network.opcode", "=== SMSG_NEIGHBORHOOD_OPEN_CORNERSTONE_UI_RESPONSE (0x5C000A) ===\n"
        "  PlotIndex={}, Cost={}, PurchaseStatus={}, CanPurchase={}, IsPlotOwned={}\n"
        "  PlotOwnerGuid: {} ({})\n"
        "  HouseGuid: {} ({})\n"
        "  CornerstoneGuid: {} ({})\n"
        "  NeighborhoodName='{}' (len={})\n"
        "  Packet size={} bytes, hex:\n  {}",
        response.PlotIndex, response.Cost, uint32(response.PurchaseStatus), response.CanPurchase, response.IsPlotOwned,
        response.PlotOwnerGuid.ToString(), GuidHex(response.PlotOwnerGuid),
        response.HouseGuid.ToString(), GuidHex(response.HouseGuid),
        response.CornerstoneGuid.ToString(), GuidHex(response.CornerstoneGuid),
        response.NeighborhoodName, response.NeighborhoodName.size(),
        pkt->size(), HexDumpPacket(pkt));
}

void WorldSession::HandleNeighborhoodOfferOwnership(WorldPackets::Neighborhood::NeighborhoodOfferOwnership const& neighborhoodOfferOwnership)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Neighborhood::NeighborhoodOfferOwnershipResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_OFFER_OWNERSHIP NeighborhoodGuid: {}, NewOwnerGuid: {}",
        neighborhoodGuid.ToString(), neighborhoodOfferOwnership.NewOwnerGuid.ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodOfferOwnershipResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodOfferOwnership: Neighborhood {} not found",
            neighborhoodGuid.ToString());
        return;
    }

    // Only owner can transfer ownership
    if (!neighborhood->IsOwner(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodOfferOwnershipResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodOfferOwnership: Player {} is not owner of neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGuid.ToString());
        return;
    }

    // Create a pending ownership transfer instead of instant transfer
    HousingResult result = neighborhood->OfferOwnership(neighborhoodOfferOwnership.NewOwnerGuid);

    WorldPackets::Neighborhood::NeighborhoodOfferOwnershipResponse response;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    // Notify the target player about the offer
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (Player* newOwner = ObjectAccessor::FindPlayer(neighborhoodOfferOwnership.NewOwnerGuid))
        {
            WorldPackets::Housing::HousingSvcsNeighborhoodOwnershipTransferredResponse transferNotification;
            transferNotification.Result = static_cast<uint8>(result);
            transferNotification.OwnerGUID = neighborhoodOfferOwnership.NewOwnerGuid;
            newOwner->SendDirectMessage(transferNotification.Write());
        }
    }

    TC_LOG_DEBUG("housing", "OfferOwnership result: {} to player {} for neighborhood {}",
        uint32(result), neighborhoodOfferOwnership.NewOwnerGuid.ToString(),
        neighborhoodGuid.ToString());
}

void WorldSession::HandleNeighborhoodGetRoster(WorldPackets::Neighborhood::NeighborhoodGetRoster const& neighborhoodGetRoster)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_GET_ROSTER NeighborhoodGuid: {}",
        neighborhoodGetRoster.NeighborhoodGuid.ToString());

    // ResolveNeighborhood handles both Housing GUIDs and bulletin board GO GUIDs
    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(neighborhoodGetRoster.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodGetRosterResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodGetRoster: Neighborhood {} not found",
            neighborhoodGetRoster.NeighborhoodGuid.ToString());
        return;
    }

    // Must be a member to view roster
    if (!neighborhood->IsMember(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodGetRosterResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodGetRoster: Player {} is not a member of neighborhood {}",
            player->GetGUID().ToString(), neighborhoodGetRoster.NeighborhoodGuid.ToString());
        return;
    }

    WorldPackets::Neighborhood::NeighborhoodGetRosterResponse response;
    neighborhood->BuildRosterResponse(response);

    // No neighborhood name goes out with the roster: retail sends the name only in answer to the client's
    // CMSG_QUERY_NEIGHBORHOOD_INFO (every capture has as many name replies as queries).
    WorldPacket const* rosterPkt = response.Write();
    SendPacket(rosterPkt);

    // Populate the Housing/4 entity with this neighborhood's mirror data so the
    // client's internal house list stays in sync for plot resolution, while she is on this neighborhood's map; the entity
    // names that neighborhood only (Neighborhood::SendMirrorTo).
    neighborhood->SendMirrorTo(player);

    // Pre-push player names for all plot owners so the client can format
    // plot names via HOUSING_HOUSE_NAME_FORMAT without waiting for async name queries.
    {
        WorldPackets::Query::QueryPlayerNamesResponse nameResponse;
        for (auto const& plot : neighborhood->GetPlots())
        {
            if (!plot.IsOccupied() || plot.OwnerGuid.IsEmpty())
                continue;

            WorldPackets::Query::NameCacheLookupResult& entry = nameResponse.Players.emplace_back();
            BuildNameQueryData(plot.OwnerGuid, entry);
        }
        if (!nameResponse.Players.empty())
        {
            SendPacket(nameResponse.Write());
            TC_LOG_DEBUG("housing", "HandleNeighborhoodGetRoster: pre-pushed {} plot owner names to NameCache",
                nameResponse.Players.size());
        }
    }

    TC_LOG_DEBUG("network.opcode", "=== SMSG_NEIGHBORHOOD_GET_ROSTER_RESPONSE (0x5C000F) [handler] ===\n"
        "  Result={}, Members={}, NeighborhoodName='{}'\n"
        "  GroupNeighborhoodGuid: {} ({})\n"
        "  GroupOwnerGuid: {} ({})\n"
        "  Packet size={} bytes, hex:\n  {}",
        uint32(response.Result), response.Members.size(), response.NeighborhoodName,
        response.GroupNeighborhoodGuid.ToString(), GuidHex(response.GroupNeighborhoodGuid),
        response.GroupOwnerGuid.ToString(), GuidHex(response.GroupOwnerGuid),
        rosterPkt->size(), HexDumpPacket(rosterPkt, 256));
}

void WorldSession::HandleNeighborhoodEvictPlot(WorldPackets::Neighborhood::NeighborhoodEvictPlot const& neighborhoodEvictPlot)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_EVICT_PLOT PlotIndex(raw): {}, NeighborhoodGuid: {}",
        neighborhoodEvictPlot.PlotIndex, neighborhoodEvictPlot.NeighborhoodGuid.ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(neighborhoodEvictPlot.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodEvictPlotResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodEvictPlot: Neighborhood {} not found",
            neighborhoodEvictPlot.NeighborhoodGuid.ToString());
        return;
    }

    uint32 plotIndex = neighborhoodEvictPlot.PlotIndex;

    // Only owner or manager can evict
    if (!neighborhood->IsOwner(player->GetGUID()) && !neighborhood->IsManager(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodEvictPlotResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "HandleNeighborhoodEvictPlot: Player {} lacks permission for neighborhood {}",
            player->GetGUID().ToString(), neighborhoodEvictPlot.NeighborhoodGuid.ToString());
        return;
    }

    uint8 const plotIdx = static_cast<uint8>(plotIndex);
    Neighborhood::PlotInfo const* plotInfo = plotIndex < MAX_NEIGHBORHOOD_PLOTS ? neighborhood->GetPlotInfo(plotIdx) : nullptr;
    if (!plotInfo)
    {
        WorldPackets::Neighborhood::NeighborhoodEvictPlotResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PLOT_NOT_FOUND);
        response.NeighborhoodGuid = neighborhoodEvictPlot.NeighborhoodGuid;
        SendPacket(response.Write());
        return;
    }

    // The character evicted is the one whose roster entry holds the plot; the character shown as the house's owner can
    // be another character of the same account.
    ObjectGuid const evictedOwnerGuid = plotInfo->OwnerGuid;
    ObjectGuid const plotGuid = plotInfo->PlotGuid;
    ObjectGuid const evictedHouseGuid = plotInfo->HouseGuid;
    uint64 const evictedHouseDatabaseId = plotInfo->HouseDatabaseId;
    uint32 const evictedBnetAccountId = uint32(plotInfo->OwnerBnetGuid.GetCounter());
    ObjectGuid evictedPlayerGuid = neighborhood->GetPlotHolder(plotIdx);
    if (evictedPlayerGuid.IsEmpty())
        evictedPlayerGuid = evictedOwnerGuid;

    HousingResult const result = neighborhood->CheckEviction(player->GetGUID(), evictedPlayerGuid);

    WorldPackets::Neighborhood::NeighborhoodEvictPlotResponse response;
    response.Result = static_cast<uint8>(result);
    response.NeighborhoodGuid = neighborhoodEvictPlot.NeighborhoodGuid;
    SendPacket(response.Write());

    if (result == HOUSING_RESULT_SUCCESS)
    {
        // The evicted house is packed, not deleted: "Your House Layout has been saved. Please purchase a new House to
        // import it" (GlobalStrings HOUSING_BULLETINBOARD_EVICTED_CONFIRMATION_TEXT). That keeps its rooms, fixtures and
        // placed decor whether or not anyone of its account is online, puts everyone inside it out on the plot, frees the
        // plot and pays back what was paid for the house to the character shown as its owner, as a relinquish would.
        // Freeing the plot takes its roster holder off the roster. When the plot had no roster holder, the character
        // shown as the house's owner is taken off instead, unless she is the owner or holds another plot.
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            sNeighborhoodMgr.PackHouseForOwnerLoss(neighborhood, plotIdx, evictedHouseGuid, evictedHouseDatabaseId,
                evictedBnetAccountId, ObjectGuid::Empty, false, evictedOwnerGuid, trans);
            neighborhood->EvictPlayer(evictedPlayerGuid, trans);
            CharacterDatabase.CommitTransaction(trans);
        }

        // The character shown as the owner hears of the eviction when she is online.
        if (!evictedOwnerGuid.IsEmpty())
        {
            if (Player* evictedPlayer = ObjectAccessor::FindPlayer(evictedOwnerGuid))
            {
                WorldPackets::Neighborhood::NeighborhoodEvictPlotNotice notice;
                notice.PlotId = plotIndex;
                notice.NeighborhoodGuid = neighborhoodEvictPlot.NeighborhoodGuid;
                notice.PlotGuid = plotGuid;
                evictedPlayer->SendDirectMessage(notice.Write());
            }
        }

        // SMSG_NEIGHBORHOOD_EVICT_PLAYER (0x5C0000). The 12.0.7 client handler (case 6029312)
        // does not decode any field — it consumes the remaining bytes as a blob and then fires
        // three neighborhood-view refreshes (codes 2, 3, 1). It is a "the roster you are showing
        // is stale, rebuild it" notification, which is exactly the state after an eviction, so it
        // goes to everyone whose view just changed: the remaining members and the evicted player.
        WorldPackets::Neighborhood::NeighborhoodEvictPlayerResponse evictNotification;
        evictNotification.PlayerGuid = evictedPlayerGuid;
        WorldPacket const* evictPkt = evictNotification.Write();

        neighborhood->BroadcastPacket(evictPkt);
        if (Player* evictedPlayer = ObjectAccessor::FindPlayer(evictedPlayerGuid))
            evictedPlayer->SendDirectMessage(evictPkt);
    }

    TC_LOG_DEBUG("housing", "EvictPlayer result: {} for plot index {} in neighborhood {}",
        uint32(result), plotIndex, neighborhoodEvictPlot.NeighborhoodGuid.ToString());
}

// ============================================================
// Neighborhood Initiative System
// ============================================================

void WorldSession::HandleNeighborhoodInitiativeServiceStatusCheck(WorldPackets::Neighborhood::NeighborhoodInitiativeServiceStatusCheck const& /*packet*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_SERVICE_STATUS_CHECK for player {}",
        player->GetGUID().ToString());

    // Send initiative service status (retail-observed: this CMSG's only response)
    sInitiativeManager.SendInitiativeServiceStatus(this, true);

    // No SMSG_GET_PLAYER_INITIATIVE_INFO_RESULT here: retail sends it only in answer to
    // CMSG_GET_PLAYER_INITIATIVE_INFO_REQUEST (hbcd3 290809, answered at 293257).
}

namespace
{
    // SMSG_GET_PLAYER_INITIATIVE_INFO_RESULT for the neighborhood a request names. Without one the reply is the
    // requested GUID and a zero flag byte: retail answered an empty request with 00 00 00 (hbcd3 293257).
    void SendPlayerInitiativeInfoFor(WorldSession* session, Player* player, ObjectGuid requestedNeighborhood)
    {
        Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(requestedNeighborhood, player);
        if (!neighborhood)
        {
            WorldPackets::Housing::GetPlayerInitiativeInfoResult response;
            response.NeighborhoodGUID = requestedNeighborhood;
            session->SendPacket(response.Write());
            return;
        }

        ObjectGuid nhObjGuid = neighborhood->GetGuid();
        sInitiativeManager.SendPlayerInitiativeInfo(session, nhObjGuid, nhObjGuid.GetCounter());
    }
}

void WorldSession::HandleGetAvailableInitiativeRequest(WorldPackets::Neighborhood::GetAvailableInitiativeRequest const& getAvailableInitiativeRequest)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    SendPlayerInitiativeInfoFor(this, player, getAvailableInitiativeRequest.NeighborhoodGuid);

    TC_LOG_DEBUG("housing", "CMSG_GET_AVAILABLE_INITIATIVE_REQUEST NeighborhoodGuid: {}, Player: {}",
        getAvailableInitiativeRequest.NeighborhoodGuid.ToString(), player->GetGUID().ToString());
}

// Retail's client asks with this opcode (hbcd3 290809 and 1305387, twice at once when the panel opens) and gets one
// SMSG_GET_PLAYER_INITIATIVE_INFO_RESULT for each request (hbcd3 293257, 1305712 and 1305721).
void WorldSession::HandleGetPlayerInitiativeInfoRequest(WorldPackets::Neighborhood::GetPlayerInitiativeInfoRequest const& getPlayerInitiativeInfoRequest)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    SendPlayerInitiativeInfoFor(this, player, getPlayerInitiativeInfoRequest.NeighborhoodGuid);

    TC_LOG_DEBUG("housing", "CMSG_GET_PLAYER_INITIATIVE_INFO_REQUEST NeighborhoodGuid: {}, Player: {}",
        getPlayerInitiativeInfoRequest.NeighborhoodGuid.ToString(), player->GetGUID().ToString());
}

void WorldSession::HandleGetInitiativeActivityLogRequest(WorldPackets::Neighborhood::GetInitiativeActivityLogRequest const& getInitiativeActivityLogRequest)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(getInitiativeActivityLogRequest.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        // No neighborhood: emit empty log. Wire is just GUID + uint32(0).
        // Real failures route via SMSG_HOUSING_SVCS_NOTIFY_PERMISSIONS_FAILURE.
        WorldPackets::Housing::GetInitiativeActivityLogResult response;
        response.NeighborhoodGuid = getInitiativeActivityLogRequest.NeighborhoodGuid;
        SendPacket(response.Write());
        return;
    }

    ObjectGuid nhObjGuid = neighborhood->GetGuid();
    uint64 nhGuid = nhObjGuid.GetCounter();
    sInitiativeManager.SendActivityLog(this, nhObjGuid, nhGuid);

    TC_LOG_DEBUG("housing", "CMSG_GET_INITIATIVE_ACTIVITY_LOG_REQUEST NeighborhoodGuid: {}, Player: {}",
        getInitiativeActivityLogRequest.NeighborhoodGuid.ToString(), player->GetGUID().ToString());
}

void WorldSession::HandleInitiativeUpdateActiveNeighborhood(WorldPackets::Neighborhood::InitiativeUpdateActiveNeighborhood const& initiativeUpdateActiveNeighborhood)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_INITIATIVE_UPDATE_ACTIVE_NEIGHBORHOOD NeighborhoodGuid: {}, Player: {}",
        initiativeUpdateActiveNeighborhood.NeighborhoodGuid.ToString(), player->GetGUID().ToString());

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(initiativeUpdateActiveNeighborhood.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        TC_LOG_DEBUG("housing", "CMSG_INITIATIVE_UPDATE_ACTIVE_NEIGHBORHOOD: Neighborhood not found for Player: {}",
            player->GetGUID().ToString());
        return;
    }

    ObjectGuid nhObjGuid = neighborhood->GetGuid();
    uint64 nhGuid = nhObjGuid.GetCounter();

    // A character has one active endeavor, in a neighborhood where her account has a house (Wowhead's endeavors guide).
    // The choice is kept for her, and her deeds and the house experience they give go there from now on. A neighborhood
    // without a house of the account is only answered with its endeavor, as before.
    Housing const* house = player->GetHousingForNeighborhood(nhObjGuid);
    if (house && !house->IsPacked())
        player->SetHousingActiveNeighborhood(nhObjGuid);
    else
        TC_LOG_DEBUG("housing", "CMSG_INITIATIVE_UPDATE_ACTIVE_NEIGHBORHOOD: {}'s account has no house in neighborhood {}; the active one is unchanged",
            player->GetGUID().ToString(), nhObjGuid.ToString());

    // Send initiative service status to confirm the service is active
    sInitiativeManager.SendInitiativeServiceStatus(this, true);

    // Send current initiative info for the active neighborhood (with real task progress)
    sInitiativeManager.SendPlayerInitiativeInfo(this, nhObjGuid, nhGuid);

    TC_LOG_DEBUG("housing", "SMSG_INITIATIVE_SERVICE_STATUS + SMSG_GET_PLAYER_INITIATIVE_INFO_RESULT sent for NeighborhoodGuid: {}",
        initiativeUpdateActiveNeighborhood.NeighborhoodGuid.ToString());
}

// ============================================================================
// 0x38xxxx NeighborhoodInitiative — generic Op-XX handlers
// ============================================================================
//
// The 12.0.5 client (build 67186) sends these 12 requests in the wire formats read from
// it. Which client function sends each one needs a capture: vtable indirection in the
// client (hash 0xBA8F5C5BC59E8E8E = INITIATIVE_TASKS_TRACKED_LIST_CHANGED) hides it from
// a reading of the binary. What the reading suggests:
//   - 0x380001, 0x38000C: candidates for SetActiveNeighborhood / SetViewingNeighborhood
//   - 0x380007, 0x38000A, 0x38000B: candidates for AddTrackedInitiativeTask /
//     RemoveTrackedInitiativeTask (uint32 taskID); the user-callable
//     APIs flush via the BATCH path 0x38000E rather than these direct uint32 senders.
//   - 0x380006, 0x380008: empty-payload candidates for RequestInitiativeActivityLog /
//     RequestNeighborhoodInitiativeInfo (the dedicated 0x380003/0x380004 opcodes also
//     match those Lua APIs — multiple paths exist).
//   - 0x380005: uint32+GUID, possibly task-state mutation tied to a neighborhood
//   - 0x380009: float, debug or rate-progress sender
//   - 0x38000D: pair-array progress submission with bit flag
//   - 0x38000E: bulk uint32 array (likely tracked-task ID list flush)
//   - 0x38000F: bulk quad-int records (likely milestone/claim batch)
//
// Until sniff data confirms exact semantics, each handler:
//   - parses the wire format successfully (doesn't error/disconnect)
//   - logs the request for diagnostic capture
//   - returns silently (no SMSG response)
// This matches how the client's other "fire-and-forget" senders behave in retail.
//
// Left out of the build with the 12 NeighborhoodInitiativeOpXX classes (HousingPackets.h): the 12.1
// opcode list has none of the CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_* requests, and guessed values would
// put made-up opcodes on the wire.
#if 0
void WorldSession::HandleNeighborhoodInitiativeOp01(WorldPackets::Neighborhood::NeighborhoodInitiativeOp01 const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_01 NeighborhoodGuid: {} (player: {})",
        packet.NeighborhoodGuid.ToString(), GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp05(WorldPackets::Neighborhood::NeighborhoodInitiativeOp05 const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_05 Field1: {} NeighborhoodGuid: {} (player: {})",
        packet.Field1, packet.NeighborhoodGuid.ToString(), GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp06(WorldPackets::Neighborhood::NeighborhoodInitiativeOp06 const& /*packet*/)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_06 (player: {})", GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp07(WorldPackets::Neighborhood::NeighborhoodInitiativeOp07 const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_07 Value: {} (player: {})",
        packet.Value, GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp08(WorldPackets::Neighborhood::NeighborhoodInitiativeOp08 const& /*packet*/)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_08 (player: {})", GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp09(WorldPackets::Neighborhood::NeighborhoodInitiativeOp09 const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_09 Value: {} (player: {})",
        packet.Value, GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp0A(WorldPackets::Neighborhood::NeighborhoodInitiativeOp0A const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_0A Value: {} (player: {})",
        packet.Value, GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp0B(WorldPackets::Neighborhood::NeighborhoodInitiativeOp0B const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_0B Value: {} (player: {})",
        packet.Value, GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp0C(WorldPackets::Neighborhood::NeighborhoodInitiativeOp0C const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_0C NeighborhoodGuid: {} (player: {})",
        packet.NeighborhoodGuid.ToString(), GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp0D(WorldPackets::Neighborhood::NeighborhoodInitiativeOp0D const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_0D Header: {} Pairs: {} Flag: {} (player: {})",
        packet.Header, uint32(packet.Pairs.size()), packet.Flag, GetPlayer()->GetGUID().ToString());
}

void WorldSession::HandleNeighborhoodInitiativeOp0E(WorldPackets::Neighborhood::NeighborhoodInitiativeOp0E const& packet)
{
    if (!GetPlayer())
        return;
    // Per the IDA doc this is the bulk-flush path for tracked-task list mutations.
    // Persist the new tracked-task list into the player's initiative state.
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_0E TaskIDs[{}] (player: {})",
        uint32(packet.TaskIDs.size()), GetPlayer()->GetGUID().ToString());
    for (uint32 id : packet.TaskIDs)
        TC_LOG_TRACE("housing", "  task: {}", id);
}

void WorldSession::HandleNeighborhoodInitiativeOp0F(WorldPackets::Neighborhood::NeighborhoodInitiativeOp0F const& packet)
{
    if (!GetPlayer())
        return;
    TC_LOG_DEBUG("housing", "CMSG_NEIGHBORHOOD_INITIATIVE_OPCODE_0F Records[{}] (player: {})",
        uint32(packet.Records.size()), GetPlayer()->GetGUID().ToString());
    for (auto const& r : packet.Records)
        TC_LOG_TRACE("housing", "  record: ({}, {}, {}, {})", r.A, r.B, r.C, r.D);
}
#endif




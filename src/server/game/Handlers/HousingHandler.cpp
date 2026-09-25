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
#include "BattlePetMgr.h"
#include "MovementPackets.h"
#include "HousingNeighborhoodMirrorEntity.h"
#include "HousingPlayerHouseEntity.h"
#include <cmath>
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "RealmList.h"
#include "AreaTrigger.h"
#include "DB2Stores.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "HouseInteriorMap.h"
#include "Housing.h"
#include "HousingDecorStore.h"
#include "HousingDefines.h"
#include "HousingMap.h"
#include "HousingMgr.h"
#include "HousingBlueprintMgr.h"
#include "MapManager.h"
#include "MeshObject.h"
#include "HousingPackets.h"
#include "HousingBlueprintPackets.h"
#include "Log.h"
#include "Neighborhood.h"
#include "NeighborhoodCharter.h"
#include "NeighborhoodMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "CharacterCache.h"
#include "SocialMgr.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "GossipDef.h"
#include "QuestDef.h"
#include "StringFormat.h"
#include "QueryCallback.h"
#include "SpellPackets.h"
#include "UpdateData.h"
#include "World.h"
#include "WorldStatePackets.h"
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

    // Authoritative server-side check for every decor, fixture and room edit. Only the house's owners may edit it,
    // and every character of the house's Battle.net account is an owner (editing is not shared with other accounts).
    // Returns true only when the player edits a house of her account from where a player can edit it:
    //   * inside that house's interior, or
    //   * standing on that house's plot on the neighborhood map.
    // Any other case (visitor on a host plot, off-plot, another house of the account) is rejected.
    bool PlayerCanEditHousing(Player* player, Housing const* housing)
    {
        if (!player || !housing || !housing->IsOwnedBy(player))
            return false;

        Map* map = player->GetMap();

        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(map))
            return interiorMap->GetHouseGuid() == housing->GetHouseGuid();

        // Neighborhood exterior: require the player to be standing on the plot of the house being edited.
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(map))
        {
            Neighborhood* neighborhood = housingMap->GetNeighborhood();
            if (!neighborhood)
                return false;

            int8 plotIndex = housingMap->GetPlayerCurrentPlot(player->GetGUID());
            if (plotIndex < 0)
                return false;

            Neighborhood::PlotInfo const* plotInfo = neighborhood->GetPlotInfo(static_cast<uint8>(plotIndex));
            if (!plotInfo)
                return false;

            return plotInfo->IsOwnedByAccount(player->GetSession()->GetBattlenetAccountGUID())
                && plotInfo->HouseGuid == housing->GetHouseGuid();
        }

        return false;
    }

    // The house a request names by GUID, when the character's Battle.net account owns it. A request whose GUID is
    // empty names the house the character stands in or on.
    Housing* ResolveRequestedHousing(Player* player, ObjectGuid houseGuid)
    {
        return houseGuid.IsEmpty() ? player->GetHousing() : player->GetHousingByGuid(houseGuid);
    }

    // The house status reply's three edit-mode bits, from the editor this character has her house in.
    void FillHouseStatusEditModes(WorldPackets::Housing::HousingHouseStatusResponse& response, Housing const* housing)
    {
        if (!housing)
            return;

        switch (housing->GetEditorMode())
        {
            case HOUSING_EDITOR_MODE_BASIC_DECOR:
            case HOUSING_EDITOR_MODE_EXPERT_DECOR:
                response.DecorEditModeEnabled = true;
                break;
            case HOUSING_EDITOR_MODE_LAYOUT:
                response.LayoutEditModeEnabled = true;
                break;
            case HOUSING_EDITOR_MODE_EXTERIOR_CUSTOMIZATION:
                response.FixtureEditModeEnabled = true;
                break;
            default:
                break;
        }
    }

    void SendGuildRemoveHouseNotification(Player* player, ObjectGuid houseGuid, ObjectGuid cosmeticOwnerGuid)
    {
        if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
        {
            WorldPackets::Housing::HousingSvcsGuildRemoveHouseNotification notification;
            notification.House.HouseGUID = houseGuid;
            notification.House.CosmeticOwnerGUID = cosmeticOwnerGuid;
            guild->BroadcastPacket(notification.Write());
        }
    }

    // Tear a house down for CMSG_HOUSING_RESET_KIOSK_MODE: despawn everything it owns on the map, free the plot by
    // the house, drop the plot holder from the roster and delete the rows. Returns the house GUID that was destroyed
    // (empty if there was nothing to destroy) so the caller can fill its response. Relinquishing does not come here:
    // it packs the house instead.
    ObjectGuid DestroyPlayerHousing(Player* player, Housing const* housing)
    {
        if (!housing)
            return ObjectGuid::Empty;

        ObjectGuid houseGuid = housing->GetHouseGuid();
        ObjectGuid neighborhoodGuid = housing->GetNeighborhoodGuid();
        ObjectGuid cosmeticOwnerGuid = housing->GetCosmeticOwnerGuid();
        uint8 plotIndex = housing->GetPlotIndex();

        Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(neighborhoodGuid);

        // Despawn map entities BEFORE the housing data goes away.
        HousingMap::DespawnHouseFromPlot(neighborhood, plotIndex, houseGuid);

        // Housing::Delete frees the plot by the house and drops the plot holder from the roster, in the same
        // transaction as the rows.
        player->DeleteHousing(houseGuid);

        if (neighborhood)
            neighborhood->RefreshMirrorDataForOnlineMembers();

        if (!houseGuid.IsEmpty())
            SendGuildRemoveHouseNotification(player, houseGuid, cosmeticOwnerGuid);

        TC_LOG_INFO("housing", "DestroyPlayerHousing: Player {} destroyed house {} on plot {} in neighborhood {}",
            player->GetGUID().ToString(), houseGuid.ToString(), plotIndex, neighborhoodGuid.ToString());

        return houseGuid;
    }

    // Sends manual SMSG_AURA_UPDATE + SMSG_SPELL_START + SMSG_SPELL_GO for a housing
    // spell that doesn't exist in our DB2/spell data. The sniff shows these spells use:
    //   AURA_UPDATE: CastID matches SPELL_START/SPELL_GO CastID
    //   SPELL_START: Target.Flags=0 (Self), CastTime=0
    //   SPELL_GO: Target.Flags=2 (Unit), HitTargets={self}, CastTime=getMSTime(), LogData filled
    void SendManualHousingSpellPackets(Player* player, uint32 spellId, uint8 auraSlot,
        uint8 auraActiveFlags, uint32 spellStartCastFlags, uint32 spellGoCastFlags,
        uint32 spellGoCastFlagsEx = 16, uint32 spellGoCastFlagsEx2 = 4)
    {
        // Generate a CastID GUID shared across AURA_UPDATE, SPELL_START, and SPELL_GO
        ObjectGuid castId = ObjectGuid::Create<HighGuid::Cast>(
            SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), spellId,
            player->GetMap()->GenerateLowGuid<HighGuid::Cast>());

        // 1. SMSG_AURA_UPDATE — apply the aura (CastID must match spell packets)
        {
            WorldPackets::Spells::AuraUpdate auraUpdate;
            auraUpdate.UpdateAll = false;
            auraUpdate.UnitGUID = player->GetGUID();

            WorldPackets::Spells::AuraInfo auraInfo;
            auraInfo.Slot = auraSlot;
            auraInfo.AuraData.emplace();
            auraInfo.AuraData->CastID = castId;
            auraInfo.AuraData->SpellID = spellId;
            auraInfo.AuraData->Flags = AFLAG_SELF_CAST;
            auraInfo.AuraData->ActiveFlags = auraActiveFlags;
            auraInfo.AuraData->CastLevel = 36;
            auraInfo.AuraData->Applications = 0;
            auraUpdate.Auras.push_back(std::move(auraInfo));

            player->SendDirectMessage(auraUpdate.Write());
        }

        // 2. SMSG_SPELL_START
        {
            WorldPackets::Spells::SpellStart spellStart;
            spellStart.Cast.CasterGUID = player->GetGUID();
            spellStart.Cast.CasterUnit = player->GetGUID();
            spellStart.Cast.CastID = castId;
            spellStart.Cast.SpellID = spellId;
            spellStart.Cast.CastFlags = spellStartCastFlags;
            spellStart.Cast.CastTime = 0;
            // Target.Flags = 0 (Self) — default

            player->SendDirectMessage(spellStart.Write());
        }

        // 3. SMSG_SPELL_GO (CombatLogServerPacket — has LogData)
        {
            WorldPackets::Spells::SpellGo spellGo;
            spellGo.Cast.CasterGUID = player->GetGUID();
            spellGo.Cast.CasterUnit = player->GetGUID();
            spellGo.Cast.CastID = castId;
            spellGo.Cast.SpellID = spellId;
            spellGo.Cast.CastFlags = spellGoCastFlags;
            spellGo.Cast.CastFlagsEx = spellGoCastFlagsEx;
            spellGo.Cast.CastFlagsEx2 = spellGoCastFlagsEx2;
            spellGo.Cast.CastTime = getMSTime();
            spellGo.Cast.Target.Flags = TARGET_FLAG_UNIT;
            spellGo.Cast.HitTargets.push_back(player->GetGUID());
            spellGo.Cast.HitStatus.emplace_back(uint8(0));
            spellGo.LogData.Initialize(player);

            player->SendDirectMessage(spellGo.Write());
        }

        TC_LOG_DEBUG("housing", "  Sent manual AURA_UPDATE + SPELL_START + SPELL_GO for spell {} (slot {}, CastID={})",
            spellId, auraSlot, castId.ToString());
    }

    // Refreshes all room MeshObjects in the player's interior instance after a room
    // data change (add, remove, rotate, move, theme, material, door, ceiling).
    void RefreshInteriorRoomVisuals(Player* player, Housing* housing)
    {
        HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap());
        if (!interiorMap)
            return;

        // Use player's team for faction theme, matching HouseInteriorMap::AddPlayerToMap pattern.
        // NeighborhoodMapData::FactionRestriction is a bitmask (3 = both factions) and doesn't
        // map to the enum values expected by GetFactionDefaultThemeID().
        int32 faction = (player->GetTeamId() == TEAM_ALLIANCE)
            ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;

        interiorMap->DespawnAllRoomMeshObjects();
        interiorMap->SpawnRoomMeshObjects(housing, faction);
    }


    // Checks whether the player is eligible for housing features.
    // Returns a bitmask of HousingWarningFlag reasons if restrictions apply.
    uint32 ShouldShowHousingWarning(Player const* player)
    {
        uint32 warnings = HOUSING_WARNING_NONE;

        // Check expansion access: housing needs Midnight
        if (player->GetSession()->GetExpansion() < HOUSING_REQUIRED_EXPANSION)
            warnings |= HOUSING_WARNING_EXPANSION_REQUIRED;

        // Check minimum level
        if (player->GetLevel() < HOUSING_MIN_PLAYER_LEVEL)
            warnings |= HOUSING_WARNING_LEVEL_TOO_LOW;

        return warnings;
    }

    // After a house's settings change, anyone the new settings no longer let in is removed, as Blizzard's preview
    // describes ("it will remove anyone whose permissions are no longer valid", https://worldofwarcraft.blizzard.com/en-us/news/24221516;
    // a July 2025 preview of work in progress, so the best rule found rather than confirmed live behaviour). A visitor
    // inside the house is put out at the plot's arrival point in the house's neighborhood, where Exit House puts her.
    // A visitor on the plot is taken off it the way a refused plot entry leaves her: no longer counted as on the plot.
    // World thread only, because it touches characters on other maps.
    void RemoveVisitorsWithoutAccess(Housing const& housing)
    {
        Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(housing.GetNeighborhoodGuid());
        if (!neighborhood || housing.IsPacked())
            return;

        uint8 const plotIndex = housing.GetPlotIndex();
        if (!neighborhood->GetPlotInfo(plotIndex))
            return;

        uint32 const worldMapId = sHousingMgr.GetWorldMapIdByNeighborhoodMapId(neighborhood->GetNeighborhoodMapID());

        if (housing.GetDatabaseId() <= std::numeric_limits<uint32>::max())
        {
            if (HouseInteriorMap* interior = dynamic_cast<HouseInteriorMap*>(sMapMgr->FindMap(HOUSE_INTERIOR_MAP_ID, uint32(housing.GetDatabaseId()))))
            {
                WorldLocation arrival;
                bool const hasArrival = sHousingMgr.GetPlotArrival(neighborhood->GetNeighborhoodMapID(), plotIndex, arrival);
                // The house's own neighborhood instance, as in Exit House (spell_housing_exit_house).
                Optional<uint32> instanceId;
                uint32 const neighborhoodInstanceId = uint32(neighborhood->GetGuid().GetCounter());
                if (hasArrival && sMapMgr->FindMap(arrival.GetMapId(), neighborhoodInstanceId))
                    instanceId = neighborhoodInstanceId;
                std::vector<Player*> removed;
                for (MapReference const& ref : interior->GetPlayers())
                {
                    Player* visitor = ref.GetSource();
                    if (visitor && !neighborhood->CheckHouseEntry(visitor, plotIndex, true).Allowed)
                        removed.push_back(visitor);
                }

                for (Player* visitor : removed)
                {
                    if (!hasArrival || !visitor->TeleportTo(arrival, TELE_TO_SPELL, instanceId))
                        TC_LOG_ERROR("housing", "RemoveVisitorsWithoutAccess: {} lost access to house {} but could not be put out of it",
                            visitor->GetGUID().ToString(), housing.GetHouseGuid().ToString());
                    else
                        TC_LOG_DEBUG("housing", "RemoveVisitorsWithoutAccess: {} lost access to house {} and was put out on plot {}",
                            visitor->GetGUID().ToString(), housing.GetHouseGuid().ToString(), plotIndex);
                }
            }
        }

        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(sMapMgr->FindMap(worldMapId, uint32(neighborhood->GetGuid().GetCounter()))))
        {
            for (MapReference const& ref : housingMap->GetPlayers())
            {
                Player* visitor = ref.GetSource();
                if (!visitor || housingMap->GetPlayerCurrentPlot(visitor->GetGUID()) != int8(plotIndex))
                    continue;

                if (neighborhood->CheckHouseEntry(visitor, plotIndex, false).Allowed)
                    continue;

                housingMap->SendPlotLeaveAuraRemoval(visitor);
                housingMap->ClearPlayerCurrentPlot(visitor->GetGUID());
                visitor->SetCurrentHouse(ObjectGuid::Empty);

                TC_LOG_DEBUG("housing", "RemoveVisitorsWithoutAccess: {} lost access to plot {} of house {}",
                    visitor->GetGUID().ToString(), plotIndex, housing.GetHouseGuid().ToString());
            }
        }
    }

    // Counts the distinct Battle.net accounts among a guild's members, and those with a member active in the last
    // GUILD_NEIGHBORHOOD_ACTIVE_DAYS days (online now, or logged out since then).
    struct GuildAccountCounts
    {
        uint32 Accounts = 0;
        uint32 ActiveAccounts = 0;
    };

    GuildAccountCounts CountGuildBattlenetAccounts(Guild const& guild)
    {
        time_t const activeSince = GameTime::GetGameTime() - time_t(GUILD_NEIGHBORHOOD_ACTIVE_DAYS) * DAY;
        std::unordered_map<uint32 /*gameAccountId*/, bool /*active*/> gameAccounts;
        for (auto const& [guid, member] : guild.GetMembers())
        {
            bool const active = member.IsOnline() || time_t(member.GetLogoutTime()) >= activeSince;
            bool& entry = gameAccounts[member.GetAccountId()];
            entry = entry || active;
        }

        std::vector<Housing::GuildMemberAccount> members;
        if (!gameAccounts.empty())
        {
            std::string ids;
            for (auto const& [gameAccountId, active] : gameAccounts)
            {
                if (!ids.empty())
                    ids += ',';
                ids += std::to_string(gameAccountId);
            }

            // The game accounts' Battle.net accounts, in one query; the ids are numbers read from the guild.
            std::unordered_map<uint32, uint32> bnetByGameAccount;
            if (QueryResult result = LoginDatabase.Query(Trinity::StringFormat("SELECT id, battlenet_account FROM account WHERE id IN ({})", ids).c_str()))
            {
                do
                {
                    Field* fields = result->Fetch();
                    bnetByGameAccount[fields[0].GetUInt32()] = fields[1].GetUInt32();
                } while (result->NextRow());
            }

            for (auto const& [gameAccountId, active] : gameAccounts)
            {
                auto bnet = bnetByGameAccount.find(gameAccountId);
                members.push_back({ bnet != bnetByGameAccount.end() ? bnet->second : 0, active });
            }
        }

        GuildAccountCounts counts;
        Housing::CountBattlenetAccounts(members, counts.Accounts, counts.ActiveAccounts);
        return counts;
    }
}

// ============================================================
// Decline Neighborhood Invites
// ============================================================

void WorldSession::HandleDeclineNeighborhoodInvites(WorldPackets::Housing::DeclineNeighborhoodInvites const& declineNeighborhoodInvites)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (declineNeighborhoodInvites.Allow)
        player->SetPlayerFlagEx(PLAYER_FLAGS_EX_AUTO_DECLINE_NEIGHBORHOOD);
    else
        player->RemovePlayerFlagEx(PLAYER_FLAGS_EX_AUTO_DECLINE_NEIGHBORHOOD);
}

// ============================================================
// House Exterior System
// ============================================================

void WorldSession::HandleHouseExteriorSetHousePosition(WorldPackets::Housing::HouseExteriorCommitPosition const& houseExteriorCommitPosition)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    WorldPackets::Housing::HouseExteriorSetHousePositionResponse response;
    auto reply = [&](HousingResult result)
    {
        response.Result = static_cast<uint8>(result);
        SendPacket(response.Write());
    };

    // hled1 645894: the house and its owner's Battle.net account, then the root's new pose in the room.
    Housing* housing = player->GetHousingByGuid(houseExteriorCommitPosition.HouseGuid);
    if (!housing)
        return reply(HOUSING_RESULT_HOUSE_NOT_FOUND);

    response.HouseGuid = housing->GetHouseGuid();

    if (houseExteriorCommitPosition.BnetAccountGuid != GetBattlenetAccountGUID() || !PlayerCanEditHousing(player, housing))
        return reply(HOUSING_RESULT_PERMISSION_DENIED);

    HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
    if (!housingMap || !housingMap->IsHouseSpawned(housing->GetPlotIndex()))
        return reply(HOUSING_RESULT_INVALID_MAP);

    float const x = houseExteriorCommitPosition.PositionX;
    float const y = houseExteriorCommitPosition.PositionY;
    float const z = houseExteriorCommitPosition.PositionZ;
    float const facing = houseExteriorCommitPosition.Facing;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(facing))
        return reply(HOUSING_RESULT_BOUNDS_FAILURE_PLOT);

    Position const placement(x, y, z, facing);
    if (!HousingMgr::IsRootPlacementInRoom(placement))
    {
        TC_LOG_DEBUG("housing", "CMSG_HOUSE_EXTERIOR_SET_HOUSE_POSITION: {} asked to put house {} at ({:.2f}, {:.2f}, {:.2f}), outside "
            "the room of plot {}", player->GetGUID().ToString(), housing->GetHouseGuid().ToString(), x, y, z, housing->GetPlotIndex());
        return reply(HOUSING_RESULT_BOUNDS_FAILURE_PLOT);
    }

    // The house moves where it stands: the exterior root takes the new pose and nothing is built again.
    if (!housingMap->MoveHouseRoot(housing->GetPlotIndex(), placement))
        return reply(HOUSING_RESULT_INVALID_MAP);

    housing->SetHousePosition(placement.GetPositionX(), placement.GetPositionY(), placement.GetPositionZ(), placement.GetOrientation());
    reply(HOUSING_RESULT_SUCCESS);

    TC_LOG_DEBUG("housing", "CMSG_HOUSE_EXTERIOR_SET_HOUSE_POSITION: {} put house {} at ({:.3f}, {:.3f}, {:.3f}) facing {:.4f} in its room",
        player->GetGUID().ToString(), housing->GetHouseGuid().ToString(), x, y, z, placement.GetOrientation());
}

void WorldSession::HandleHouseExteriorLock(WorldPackets::Housing::HouseExteriorLock const& houseExteriorLock)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // The client locks the exterior around a drag of the house and unlocks it after (hled1 645300-645918). The reply
    // names the house and the character; nothing else is sent.
    WorldPackets::Housing::HouseExteriorLockResponse response;
    response.EditorPlayerGuid = player->GetGUID();
    response.Active = houseExteriorLock.Locked;
    auto reply = [&](HousingResult result)
    {
        response.Result = static_cast<uint8>(result);
        SendPacket(response.Write());
    };

    Housing* housing = player->GetHousingByGuid(houseExteriorLock.HouseGuid);
    if (!housing)
        return reply(HOUSING_RESULT_HOUSE_NOT_FOUND);

    response.HouseGuid = housing->GetHouseGuid();

    if (houseExteriorLock.HouseOwnerAccountGuid != ObjectGuid::Create<HighGuid::BNetAccount>(housing->GetOwnerAccountId())
        || !PlayerCanEditHousing(player, housing))
        return reply(HOUSING_RESULT_PERMISSION_DENIED);

    // The exterior is on the neighborhood map; inside the house there is none to lock.
    if (!dynamic_cast<HousingMap*>(player->GetMap()))
        return reply(houseExteriorLock.Locked ? HOUSING_RESULT_LOCK_OPERATION_FAILED : HOUSING_RESULT_UNLOCK_OPERATION_FAILED);

    if (houseExteriorLock.Locked)
        housing->SetExteriorLockHolder(player->GetGUID());
    else
        housing->ReleaseExteriorLock(player->GetGUID());

    reply(HOUSING_RESULT_SUCCESS);

    TC_LOG_DEBUG("housing", "CMSG_HOUSE_EXTERIOR_LOCK: {} {} the exterior of house {}", player->GetGUID().ToString(),
        houseExteriorLock.Locked ? "locked" : "unlocked", housing->GetHouseGuid().ToString());
}

// ============================================================
// House Interior System
// ============================================================

void WorldSession::HandleHouseInteriorLeaveHouse(WorldPackets::Housing::HouseInteriorLeaveHouse const& /*houseInteriorLeaveHouse*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // A visitor may not own a house of their own, and this handler still has
    // to let them leave. The character's own housing is used only to clear the
    // editor mode and the interior state, and to give the fallback plot for the
    // way out. Where the character comes out is taken from the HouseInteriorMap's stored
    // source fields.
    HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap());
    // The house being left, when the character's account owns it; a visitor has none here.
    Housing* housing = interiorMap ? player->GetHousingByGuid(interiorMap->GetHouseGuid()) : player->GetHousing();
    bool isVisit = interiorMap && !interiorMap->IsHouseOwner(player);

    // Clear editing mode and interior state — only own housing carries that
    // state (visitors can't be in edit mode in someone else's house anyway).
    if (housing)
    {
        housing->SetEditorMode(HOUSING_EDITOR_MODE_NONE);
        housing->SetInInterior(false);
    }

    // 12.0.5: SMSG_HOUSE_INTERIOR_LEAVE_HOUSE_RESPONSE no longer exists.
    // The client reacts to the PlayerHouseInfoComponent.CurrentHouse field being
    // cleared via UPDATE_OBJECT on the player (SetCurrentHouse(Empty) elsewhere).
    if (Player* p = GetPlayer())
        p->SetCurrentHouse(ObjectGuid::Empty);

    // No house status goes out here: retail sends it only in answer to the client's own request (every status
    // reply in the captures follows a CMSG_HOUSING_HOUSE_STATUS).

    // Teleport player back to the neighborhood map at the plot's visitor landing point.
    // Try to use the HouseInteriorMap's stored source info first (most reliable),
    // then fall back to resolving from the Housing object's neighborhood.
    uint32 worldMapId = 0;
    uint8 plotIndex = housing ? housing->GetPlotIndex() : INVALID_PLOT_INDEX;
    uint32 neighborhoodMapId = 0;

    // Preferred path: get the source neighborhood from the HouseInteriorMap itself
    if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
    {
        worldMapId = interiorMap->GetSourceNeighborhoodMapId();
        plotIndex = interiorMap->GetSourcePlotIndex();
    }

    // The exit route belongs to the house being LEFT, which for a visitor is the
    // host's house, not their own. `housing` is null for a player who owns none
    // (Player::GetHousing returns nullptr on an empty _housings), so every use
    // below has to tolerate that — resolving it here keeps the null in one place.
    Housing const* exitHousing = housing;
    if (isVisit && interiorMap)
        exitHousing = interiorMap->GetOwnerHousing();

    // Fallback: resolve from the Housing object's neighborhood GUID
    if (worldMapId == 0 && exitHousing)
    {
        Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(exitHousing->GetNeighborhoodGuid(), player);
        if (neighborhood)
        {
            neighborhoodMapId = neighborhood->GetNeighborhoodMapID();
            worldMapId = sHousingMgr.GetWorldMapIdByNeighborhoodMapId(neighborhoodMapId);
        }
    }

    // Last resort fallback
    if (worldMapId == 0)
    {
        worldMapId = 2735; // Alliance Founder's Point default
        TC_LOG_ERROR("housing", "CMSG_HOUSE_INTERIOR_LEAVE_HOUSE: Could not resolve neighborhood world map, "
            "falling back to {}", worldMapId);
    }

    // Resolve the NeighborhoodMapId for the world map to look up plot data
    if (neighborhoodMapId == 0)
        neighborhoodMapId = sHousingMgr.GetNeighborhoodMapIdByWorldMap(worldMapId);

    // Where she comes out: the plot's arrival point, NeighborhoodPlot.TeleportPosition facing the cornerstone, which is
    // where retail put her after Exit House (hbcd3 1456426), the same spot as after Teleport Home.
    float exitX = 0.0f, exitY = 0.0f, exitZ = 0.0f, exitO = 0.0f;
    bool foundExitPoint = false;

    if (neighborhoodMapId != 0 && plotIndex != INVALID_PLOT_INDEX)
    {
        if (NeighborhoodPlotData const* plot = sHousingMgr.GetPlot(neighborhoodMapId, plotIndex))
        {
            WorldLocation const arrival = HousingMgr::MakePlotArrival(*plot, worldMapId);
            exitX = arrival.GetPositionX();
            exitY = arrival.GetPositionY();
            exitZ = arrival.GetPositionZ();
            exitO = arrival.GetOrientation();
            foundExitPoint = true;
        }
    }

    if (!foundExitPoint)
    {
        // Last resort: use neighborhood center
        NeighborhoodMapData const* mapData = sHousingMgr.GetNeighborhoodMapData(neighborhoodMapId);
        if (mapData)
        {
            exitX = mapData->Origin[0];
            exitY = mapData->Origin[1];
            exitZ = mapData->Origin[2];
        }
        TC_LOG_WARN("housing", "CMSG_HOUSE_INTERIOR_LEAVE_HOUSE: No exit point for plotIndex {}, "
            "using neighborhood center", plotIndex);
    }

    // Into the neighborhood instance where the house stands, as Exit House does (spell_housing_exit_house), so she
    // comes out beside the house and not on the same plot of another neighborhood.
    Optional<uint32> instanceId;
    if (interiorMap)
    {
        for (Neighborhood const* candidate : sNeighborhoodMgr.GetAllNeighborhoods())
        {
            if (!candidate->GetPlotInfoByHouse(interiorMap->GetHouseGuid()))
                continue;

            uint32 const neighborhoodInstanceId = uint32(candidate->GetGuid().GetCounter());
            if (sHousingMgr.GetWorldMapIdByNeighborhoodMapId(candidate->GetNeighborhoodMapID()) == worldMapId
                && sMapMgr->FindMap(worldMapId, neighborhoodInstanceId))
                instanceId = neighborhoodInstanceId;
            break;
        }
    }

    player->TeleportTo(worldMapId, exitX, exitY, exitZ, exitO, TELE_TO_NONE, instanceId);

    TC_LOG_DEBUG("housing", "CMSG_HOUSE_INTERIOR_LEAVE_HOUSE: Player {} teleporting back to map {} at ({:.1f}, {:.1f}, {:.1f})",
        player->GetGUID().ToString(), worldMapId, exitX, exitY, exitZ);
}

// ============================================================
// Decor System
// ============================================================

bool WorldSession::CheckHousingDecorThrottle()
{
    uint32 now = GameTime::GetGameTimeMS();
    // getMSTimeDiff-safe: GetGameTimeMS wraps ~49.7 days; treat a wrap or an
    // elapsed window as a fresh window.
    uint32 elapsed = now - _housingDecorThrottleWindowStart;
    if (_housingDecorThrottleWindowStart == 0 || elapsed >= HOUSING_DECOR_THROTTLE_WINDOW_MS)
    {
        _housingDecorThrottleWindowStart = now;
        _housingDecorThrottleCount = 1;
        return true;
    }

    if (_housingDecorThrottleCount >= HOUSING_DECOR_THROTTLE_BURST)
        return false;

    ++_housingDecorThrottleCount;
    return true;
}

void WorldSession::HandleHousingDecorSetEditMode(WorldPackets::Housing::HousingDecorSetEditMode const& housingDecorSetEditMode)
{
    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_DECOR_SET_EDIT_MODE Active={}", housingDecorSetEditMode.Active);

    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        TC_LOG_DEBUG("housing", "HandleHousingDecorSetEditMode: GetHousing() returned null for player {}",
            player->GetGUID().ToString());
        WorldPackets::Housing::HousingDecorSetEditModeResponse response;
        response.Result = HOUSING_RESULT_HOUSE_NOT_FOUND;
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may enter edit mode. Leaving edit mode (Active=false) is always
    // allowed, so a visitor or a player who walked off the plot can never be stuck editing.
    if (housingDecorSetEditMode.Active && !PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingDecorSetEditModeResponse response;
        response.Result = HOUSING_RESULT_NOT_ON_OWNED_PLOT;
        SendPacket(response.Write());
        return;
    }

    HousingEditorMode targetMode = housingDecorSetEditMode.Active ? HOUSING_EDITOR_MODE_BASIC_DECOR : HOUSING_EDITOR_MODE_NONE;

    TC_LOG_DEBUG("housing", "  HouseGuid={} PlotGuid={} NeighborhoodGuid={}",
        housing->GetHouseGuid().ToString(), housing->GetPlotGuid().ToString(), housing->GetNeighborhoodGuid().ToString());

    if (!player->m_playerHouseInfoComponentData.has_value())
    {
        TC_LOG_ERROR("housing", "HandleHousingDecorSetEditMode: PlayerHouseInfoComponentData NOT initialized for player {}",
            player->GetGUID().ToString());
        WorldPackets::Housing::HousingDecorSetEditModeResponse response;
        response.HouseGuid = housing->GetHouseGuid();
        response.BNetAccountGuid = GetBattlenetAccountGUID();
        response.Result = HOUSING_RESULT_HOUSE_NOT_FOUND;
        SendPacket(response.Write());
        return;
    }

    {
        UF::PlayerHouseInfoComponentData const& phData = *player->m_playerHouseInfoComponentData;
        TC_LOG_DEBUG("housing", "  BEFORE SetEditorMode: EditorMode={} HouseCount={}",
            uint32(*phData.EditorMode), phData.Houses.size());
        for (uint32 i = 0; i < phData.Houses.size(); ++i)
        {
            TC_LOG_DEBUG("housing", "    Houses[{}]: HouseGUID={} MapID={} PlotID={} Level={} NeighborhoodGUID={}",
                i, phData.Houses[i].HouseGUID.ToString(), phData.Houses[i].MapID,
                phData.Houses[i].PlotID, phData.Houses[i].Level,
                phData.Houses[i].NeighborhoodGUID.ToString());
        }
    }

    // Set edit mode via UpdateField — client needs both the UpdateField change AND the SMSG response
    housing->SetEditorMode(targetMode);

    // Wire format: PackedGUID HouseGuid + PackedGUID BNetAccountGuid
    // + uint32 AllowedEditor.size() + uint8 Result + [PackedGUID AllowedEditors...]
    WorldPackets::Housing::HousingDecorSetEditModeResponse response;
    response.HouseGuid = housing->GetHouseGuid();
    response.BNetAccountGuid = GetBattlenetAccountGUID();
    response.Result = HOUSING_RESULT_SUCCESS;

    if (housingDecorSetEditMode.Active)
    {
        // --- Edit mode ENTER ---
        // Packet order: AURA_UPDATE(1263303) → SPELL_START(1263303) → SPELL_GO(1263303)
        //   → EDIT_MODE_RESPONSE → UPDATE_OBJECT(EditorMode=1 + BNetAccount/FHousingStorage_C)

        // 1. Apply edit mode aura + spell cast packets (spell 1263303)
        if (sSpellMgr->GetSpellInfo(SPELL_HOUSING_EDIT_MODE_AURA, DIFFICULTY_NONE))
        {
            player->CastSpell(player, SPELL_HOUSING_EDIT_MODE_AURA, true);
        }
        else
        {
            // Spell not in DB2 — send manual AURA_UPDATE + SPELL_START + SPELL_GO
            SendManualHousingSpellPackets(player, SPELL_HOUSING_EDIT_MODE_AURA,
                /*auraSlot=*/51, /*auraActiveFlags=*/15,
                /*spellStartCastFlags=*/CAST_FLAG_PENDING | CAST_FLAG_HAS_TRAJECTORY | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4,  // 15
                /*spellGoCastFlags=*/CAST_FLAG_PENDING | CAST_FLAG_UNKNOWN_3 | CAST_FLAG_UNKNOWN_4 | CAST_FLAG_UNKNOWN_9 | CAST_FLAG_UNKNOWN_10);  // 781
        }

        // 2. Build response with AllowedEditor containing the player
        response.AllowedEditor.push_back(player->GetGUID());

        // 3. Send the edit mode response BEFORE the UpdateObject
        WorldPacket const* editModePkt = response.Write();
        TC_LOG_DEBUG("network.opcode", "<<< SMSG_HOUSING_DECOR_SET_EDIT_MODE_RESPONSE ({} bytes): {}",
            editModePkt->size(), HexDumpPacket(editModePkt));
        TC_LOG_DEBUG("housing", "    HouseGuid={} BNetAccountGuid={} AllowedEditors={} Result={}",
            response.HouseGuid.ToString(), response.BNetAccountGuid.ToString(),
            uint32(response.AllowedEditor.size()), response.Result);
        SendPacket(editModePkt);

        // Sniff-verified: retail sets UNIT_FLAG_PACIFIED, UNIT_FLAG2_NO_ACTIONS,
        // and SilencedSchoolMask=127 during edit mode. These are sent in the same
        // UPDATE_OBJECT that carries EditorMode=1. The client's housing editor
        // specifically expects these flags alongside EditorMode.
        player->SetUnitFlag(UNIT_FLAG_PACIFIED);
        player->SetUnitFlag2(UNIT_FLAG2_NO_ACTIONS);
        player->ReplaceAllSilencedSchoolMask(SPELL_SCHOOL_MASK_ALL);

        // 4. The account's saved pieces on the Account entity (hbcd3 1431674-1431809: the edit-mode update carries the
        // whole storage). The client matches each placed piece's FHousingDecor_C.DecorGUID against these entries to
        // build its placed decor list for targeting.
        player->PushHousingDecorStorage();

        // 4b. Refresh budget values on the HousingPlayerHouseEntity so the client
        // receives up-to-date max budgets alongside the storage data.
        housing->SyncUpdateFields();

        // 5. Send Player + Account + HousingPlayerHouseEntity in a SINGLE SMSG_UPDATE_OBJECT.
        // Sniff-verified: retail sends EditorMode=1, FHousingStorage_C, and budget data
        // in the same UPDATE_OBJECT. The client reads EditorMode from PlayerHouseInfoComponentData
        // to gate ClickTarget (flag 16) in ClientHousingDecorSystem and reads budgets +
        // storage entries together to compute placed/remaining decor counts.
        // NOTE: BaseEntity::SendUpdateToPlayer is const and does NOT call
        // BuildUpdateChangesMask(), so ContentsChangedMask would be 0 and the
        // VALUES_UPDATE empty. We must compute masks explicitly before building.
        {
            player->BuildUpdateChangesMask();
            GetBattlenetAccount().BuildUpdateChangesMask();
            GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildUpdateChangesMask();

            UpdateData updateData(player->GetMapId());
            WorldPacket updatePacket;

            // Player VALUES_UPDATE (EditorMode=1 + UNIT_FLAG_PACIFIED + UNIT_FLAG2_NO_ACTIONS)
            player->BuildValuesUpdateBlockForPlayer(&updateData, player);

            // Account entity: ALWAYS send CREATE (not VALUES_UPDATE) when entering edit mode.
            // The initial Account CREATE (during login's SendInitSelf) has NO FHousingStorage_C
            // data. PushHousingDecorStorage() added Decor map entries above, and sending
            // a VALUES_UPDATE for a MapUpdateField that was empty at CREATE time may not
            // properly convey the new entries to the client. CREATE includes all current values.
            // The client handles receiving a second CREATE for an existing entity gracefully.
            GetBattlenetAccount().BuildCreateUpdateBlockForPlayer(&updateData, player);
            player->m_clientSessionEntityGUIDs.insert(GetBattlenetAccount().GetGUID());

            // ALWAYS send CREATE for HousingPlayerHouseEntity when entering edit mode.
            // Same reasoning as Account entity above: the initial CREATE during login may
            // not have had budget values populated yet, and VALUES_UPDATE only includes
            // changed fields — which may be empty if the values haven't changed since last
            // sync. CREATE includes ALL current field values (budgets, level, favor, etc.).
            GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildCreateUpdateBlockForPlayer(&updateData, player);
            player->m_clientSessionEntityGUIDs.insert(GetHousingPlayerHouseEntity(housing->GetHouseGuid()).GetGUID());

            // Include CREATE for ALL decor MeshObjects in this same UPDATE_OBJECT packet.
            // The client correlates MeshObject FHousingDecor_C.DecorGUID with Account
            // FHousingStorage_C entries to build the Placed Decor list. MeshObjects that
            // were CREATEd via normal grid visibility (separate earlier packet) arrived
            // BEFORE FHousingStorage_C was populated, so the client doesn't associate them
            // with decor entries. Re-sending CREATE in this packet (alongside the Account
            // entity) ensures the client has all data in the same context.
            {
                uint32 meshCreateCount = 0;

                // Exterior map: decor from GetDecorGuidMap()
                if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
                {
                    for (auto const& [decorGuid, meshObjGuid] : housingMap->GetDecorGuidMap())
                    {
                        MeshObject* meshObj = housingMap->GetMeshObject(meshObjGuid);
                        if (!meshObj || !meshObj->IsInWorld())
                            continue;

                        // A duplicate CREATE for a GUID the client already holds kills it
                        // (ACCESS_VIOLATION, null read) - refresh via a values update instead,
                        // same as the fixture path below.
                        if (player->HaveAtClient(meshObj))
                        {
                            meshObj->BuildValuesUpdateBlockForPlayer(&updateData, player);
                            continue;
                        }

                        meshObj->BuildCreateUpdateBlockForPlayer(&updateData, player);
                        player->m_clientGUIDs.insert(meshObjGuid);
                        ++meshCreateCount;
                    }
                }
                // Interior map: decor from interior _decorGuidToObjGuid
                else if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
                {
                    for (auto const& [decorGuid, meshObjGuid] : interiorMap->GetDecorGuidMap())
                    {
                        MeshObject* meshObj = interiorMap->GetMeshObject(meshObjGuid);
                        if (!meshObj || !meshObj->IsInWorld())
                            continue;

                        // A duplicate CREATE for a GUID the client already holds kills it
                        // (ACCESS_VIOLATION, null read) - refresh via a values update instead,
                        // same as the fixture path below.
                        if (player->HaveAtClient(meshObj))
                        {
                            meshObj->BuildValuesUpdateBlockForPlayer(&updateData, player);
                            continue;
                        }

                        meshObj->BuildCreateUpdateBlockForPlayer(&updateData, player);
                        player->m_clientGUIDs.insert(meshObjGuid);
                        ++meshCreateCount;
                    }
                }

                if (meshCreateCount > 0)
                    TC_LOG_DEBUG("housing", "  EditMode: Force-sent {} decor MeshObject CREATEs to player {}", meshCreateCount, player->GetGUID().ToString());
            }

            updateData.BuildPacket(&updatePacket);
            player->SendDirectMessage(&updatePacket);

            // Clear change masks AND remove from _updateObjects to prevent duplicate
            // VALUES_UPDATE on next map tick (causes "Object update failed" on client).
            player->ClearUpdateMask(false);
            GetBattlenetAccount().ClearUpdateMask(true);
            GetHousingPlayerHouseEntity(housing->GetHouseGuid()).ClearUpdateMask(true);
        }

        // Diagnostic: log placed decor GUIDs from Housing vs what's on spawned MeshObjects.
        // This helps identify mismatches between MeshObject FHousingDecor_C.DecorGUID
        // and Account FHousingStorage_C.Decor map keys that prevent click targeting.
        {
            uint32 meshDecorCount = 0;
            uint32 meshInWorld = 0;
            uint32 meshHasFrag = 0;
            uint32 meshAtClient = 0;

            // Collect the decor GUID map from whichever map type the player is on
            std::unordered_map<ObjectGuid, ObjectGuid> const* decorMap = nullptr;
            Map* playerMap = player->GetMap();
            if (HousingMap* housingMap = dynamic_cast<HousingMap*>(playerMap))
                decorMap = &housingMap->GetDecorGuidMap();
            else if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(playerMap))
                decorMap = &interiorMap->GetDecorGuidMap();

            if (decorMap)
            {
                for (auto const& [decorGuid, meshObjGuid] : *decorMap)
                {
                    MeshObject* meshObj = playerMap->GetMeshObject(meshObjGuid);
                    bool inWorld = meshObj && meshObj->IsInWorld();
                    bool hasFrag = meshObj && meshObj->HasHousingDecorData();
                    bool atClient = player->m_clientGUIDs.count(meshObjGuid) > 0;
                    if (inWorld) ++meshInWorld;
                    if (hasFrag) ++meshHasFrag;
                    if (atClient) ++meshAtClient;

                    TC_LOG_DEBUG("housing", "  [DIAG] MeshDecor: decorKey={} meshGuid={} inWorld={} hasFrag={} atClient={}",
                        decorGuid.ToString(), meshObjGuid.ToString(), inWorld, hasFrag, atClient);
                    ++meshDecorCount;
                }
            }

            // Also log the placed decor GUIDs from the Housing object (what's in the Account storage)
            uint32 totalPlaced = 0;
            uint32 matchCount = 0;
            for (auto const& [decorGuid, decor] : housing->GetPlacedDecorMap())
            {
                bool hasMeshObject = decorMap && decorMap->count(decorGuid) > 0;
                if (hasMeshObject) ++matchCount;
                ++totalPlaced;
                TC_LOG_DEBUG("housing", "  [DIAG] HousingDecor: decorGuid={} entryId={} room={} hasMesh={}",
                    decorGuid.ToString(), decor.DecorEntryId, decor.RoomGuid.ToString(), hasMeshObject);
            }

            TC_LOG_DEBUG("housing", "  [DIAG] Summary: meshTracked={} meshInWorld={} meshHasFrag={} meshAtClient={} "
                "housingPlaced={} matched={} storageSent={}",
                meshDecorCount, meshInWorld, meshHasFrag, meshAtClient,
                totalPlaced, matchCount, GetBattlenetAccount().IsHousingDecorStorageSent());
        }

        // Play the plot boundary spell visual on the player's plot AT.
        // This activates the glowing border decal around the plot when in edit mode.
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
        {
            if (AreaTrigger* plotAt = housingMap->GetPlotAreaTrigger(housing->GetPlotIndex()))
                plotAt->PlaySpellVisual(510142);
        }

        TC_LOG_DEBUG("housing", "  EditMode ENTER: PlayerGUID={} BNetAccountGuid={}",
            player->GetGUID().ToString(), response.BNetAccountGuid.ToString());
    }
    else
    {
        // --- Edit mode EXIT ---
        // Packet order: AURA_UPDATE → EDIT_MODE_RESPONSE → UPDATE_OBJECT

        // 1. Remove edit mode aura
        if (sSpellMgr->GetSpellInfo(SPELL_HOUSING_EDIT_MODE_AURA, DIFFICULTY_NONE))
        {
            player->RemoveAurasDueToSpell(SPELL_HOUSING_EDIT_MODE_AURA);
        }
        else
        {
            // Spell not in DB2 — send aura removal manually (empty AuraData = HasAura=False)
            WorldPackets::Spells::AuraUpdate auraUpdate;
            auraUpdate.UpdateAll = false;
            auraUpdate.UnitGUID = player->GetGUID();

            WorldPackets::Spells::AuraInfo auraInfo;
            auraInfo.Slot = 51;
            auraUpdate.Auras.push_back(std::move(auraInfo));

            player->SendDirectMessage(auraUpdate.Write());
        }

        // 2. Clear unit flags set during edit mode enter
        player->RemoveUnitFlag(UNIT_FLAG_PACIFIED);
        player->RemoveUnitFlag2(UNIT_FLAG2_NO_ACTIONS);
        player->ReplaceAllSilencedSchoolMask(SpellSchoolMask(0));

        // 3. Send the edit mode response (empty AllowedEditor = exit)
        WorldPacket const* exitModePkt = response.Write();
        TC_LOG_DEBUG("network.opcode", "<<< SMSG_HOUSING_DECOR_SET_EDIT_MODE_RESPONSE EXIT ({} bytes): {}",
            exitModePkt->size(), HexDumpPacket(exitModePkt));
        TC_LOG_DEBUG("housing", "    HouseGuid={} BNetAccountGuid={} AllowedEditors=0 Result={}",
            response.HouseGuid.ToString(), response.BNetAccountGuid.ToString(), response.Result);
        SendPacket(exitModePkt);

        // 4. Send Player UPDATE_OBJECT with EditorMode=0 + cleared unit flags immediately.
        // Must call BuildUpdateChangesMask() since BaseEntity::SendUpdateToPlayer is const.
        {
            player->BuildUpdateChangesMask();

            UpdateData updateData(player->GetMapId());
            WorldPacket updatePacket;
            player->BuildValuesUpdateBlockForPlayer(&updateData, player);
            updateData.BuildPacket(&updatePacket);
            player->SendDirectMessage(&updatePacket);

            player->ClearUpdateMask(false);
        }

        // Clear Account entity dirty state on EXIT. During edit mode, decor operations
        // (place/move/remove) modify FHousingStorage_C which marks the Account dirty.
        // Without this, Map::SendObjectUpdates() sends a stale VALUES_UPDATE on the
        // next tick, which the client rejects ("Object update failed for BNetAccount").
        GetBattlenetAccount().ClearUpdateMask(true);
        GetHousingPlayerHouseEntity(housing->GetHouseGuid()).ClearUpdateMask(true);

        TC_LOG_DEBUG("housing", "  EditMode EXIT: BNetAccountGuid={}",
            response.BNetAccountGuid.ToString());
    }

    if (player->m_playerHouseInfoComponentData.has_value())
    {
        UF::PlayerHouseInfoComponentData const& phData = *player->m_playerHouseInfoComponentData;
        TC_LOG_DEBUG("housing", "  AFTER SetEditMode: EditorMode={} (target={}), HouseCount={}",
            uint32(*phData.EditorMode), uint32(targetMode), phData.Houses.size());
    }
}

void WorldSession::HandleHousingDecorPlace(WorldPackets::Housing::HousingDecorPlace const& housingDecorPlace)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingDecorPlaceResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may place decor.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingDecorPlaceResponse response;
        response.PlayerGuid = player->GetGUID();
        response.DecorGuid = housingDecorPlace.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    // Too many decor edits from this session too quickly are refused.
    if (!CheckHousingDecorThrottle())
    {
        WorldPackets::Housing::HousingDecorPlaceResponse response;
        response.PlayerGuid = player->GetGUID();
        response.DecorGuid = housingDecorPlace.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_TOO_MANY_REQUESTS);
        SendPacket(response.Write());
        return;
    }

    // The client names the piece by the GUID of its storage entry: a redeem's reply (hbcd3 1443667 then the place at
    // 1443983), or any piece the storage lists. Housing::PlaceDecorWithGuid takes it out of the account's storage.
    // Client sends Euler angles (via TaggedPosition<XYZ> Rotation) — convert to quaternion
    float yaw = housingDecorPlace.Rotation.Pos.GetPositionX();
    float pitch = housingDecorPlace.Rotation.Pos.GetPositionY();
    float roll = housingDecorPlace.Rotation.Pos.GetPositionZ();
    float halfYaw = yaw * 0.5f, halfPitch = pitch * 0.5f, halfRoll = roll * 0.5f;
    float cy = std::cos(halfYaw), sy = std::sin(halfYaw);
    float cp = std::cos(halfPitch), sp = std::sin(halfPitch);
    float cr = std::cos(halfRoll), sr = std::sin(halfRoll);
    float rotW = cy * cp * cr + sy * sp * sr;
    float rotX = cy * cp * sr - sy * sp * cr;
    float rotY = sy * cp * sr + cy * sp * cr;
    float rotZ = sy * cp * cr - cy * sp * sr;

    float posX = housingDecorPlace.Position.Pos.GetPositionX();
    float posY = housingDecorPlace.Position.Pos.GetPositionY();
    float posZ = housingDecorPlace.Position.Pos.GetPositionZ();

    // On the interior map, if the client sends empty RoomGuid, assign to the first
    // visual room so the decor is tracked as interior and SpawnSingleInteriorDecor works.
    ObjectGuid roomGuid = housingDecorPlace.RoomGuid;
    if (roomGuid.IsEmpty() && dynamic_cast<HouseInteriorMap*>(player->GetMap()))
    {
        for (Housing::Room const* room : housing->GetRooms())
        {
            HouseRoomData const* rd = sHousingMgr.GetHouseRoomData(room->RoomEntryId);
            if (rd && !rd->IsBaseRoom())
            {
                roomGuid = room->Guid;
                break;
            }
        }
    }


    HousingResult result = housing->PlaceDecorWithGuid(housingDecorPlace.DecorGuid,
        posX, posY, posZ, rotX, rotY, rotZ, rotW, roomGuid);

    // CRITICAL: Send PLACE_RESPONSE BEFORE spawning the MeshObject.
    // The client's placement state machine needs the response to finalize the current
    // placement before receiving the MeshObject CREATE. Wrong order causes the preview
    // to snap to camera on subsequent placements ("flies to camera" bug).
    WorldPackets::Housing::HousingDecorPlaceResponse response;
    response.PlayerGuid = player->GetGUID();
    response.Field_09 = 0;
    response.DecorGuid = housingDecorPlace.DecorGuid;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_PLACE DecorGuid={} Result={}",
        housingDecorPlace.DecorGuid.ToString(), uint32(result));

    // THEN spawn the MeshObject + update Account entity
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (Housing::PlacedDecor const* newDecor = housing->GetPlacedDecor(housingDecorPlace.DecorGuid))
        {
            if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
                housingMap->SpawnDecorItem(housing->GetPlotIndex(), *newDecor, housing->GetHouseGuid());
            else if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
                interiorMap->SpawnSingleInteriorDecor(*newDecor, housing->GetHouseGuid());
        }

        if (GetBattlenetAccount().IsHousingDecorStorageSent())
            GetBattlenetAccount().SendUpdateToPlayer(player);
    }
}

void WorldSession::HandleHousingDecorMove(WorldPackets::Housing::HousingDecorMove const& housingDecorMove)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingDecorMoveResponse response;
        response.PlayerGuid = player->GetGUID();
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may move decor.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingDecorMoveResponse response;
        response.PlayerGuid = player->GetGUID();
        response.DecorGuid = housingDecorMove.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    // Too many decor edits from this session too quickly are refused.
    if (!CheckHousingDecorThrottle())
    {
        WorldPackets::Housing::HousingDecorMoveResponse response;
        response.PlayerGuid = player->GetGUID();
        response.DecorGuid = housingDecorMove.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_TOO_MANY_REQUESTS);
        SendPacket(response.Write());
        return;
    }

    // Client sends Euler angles (via TaggedPosition<XYZ> Rotation) — convert to quaternion
    float yaw = housingDecorMove.Rotation.Pos.GetPositionX();
    float pitch = housingDecorMove.Rotation.Pos.GetPositionY();
    float roll = housingDecorMove.Rotation.Pos.GetPositionZ();
    float halfYaw = yaw * 0.5f, halfPitch = pitch * 0.5f, halfRoll = roll * 0.5f;
    float cy = std::cos(halfYaw), sy = std::sin(halfYaw);
    float cp = std::cos(halfPitch), sp = std::sin(halfPitch);
    float cr = std::cos(halfRoll), sr = std::sin(halfRoll);
    float rotW = cy * cp * cr + sy * sp * sr;
    float rotX = cy * cp * sr - sy * sp * cr;
    float rotY = sy * cp * sr + cy * sp * cr;
    float rotZ = sy * cp * cr - cy * sp * sr;

    float posX = housingDecorMove.Position.Pos.GetPositionX();
    float posY = housingDecorMove.Position.Pos.GetPositionY();
    float posZ = housingDecorMove.Position.Pos.GetPositionZ();

    float scale = housingDecorMove.Scale;

    HousingResult result = housing->MoveDecor(housingDecorMove.DecorGuid,
        posX, posY, posZ, rotX, rotY, rotZ, rotW, scale);

    // Update decor MeshObject position + scale on the map
    if (result == HOUSING_RESULT_SUCCESS)
    {
        Position newPos(posX, posY, posZ);
        QuaternionData newRot(rotX, rotY, rotZ, rotW);
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
            housingMap->UpdateDecorPosition(housing->GetPlotIndex(), housingDecorMove.DecorGuid, newPos, newRot, scale);
        else if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
            interiorMap->UpdateDecorPosition(housingDecorMove.DecorGuid, newPos, newRot, scale);
    }

    WorldPackets::Housing::HousingDecorMoveResponse response;
    response.PlayerGuid = player->GetGUID();
    response.DecorGuid = housingDecorMove.DecorGuid;
    response.Result = static_cast<uint8>(result);
    WorldPacket const* movePkt = response.Write();
    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_DECOR_MOVE DecorGuid={} Pos=({:.3f},{:.3f},{:.3f}) Rot=({:.3f},{:.3f},{:.3f}) Scale={:.2f} RoomGuid={} AttachParent={}",
        housingDecorMove.DecorGuid.ToString(), posX, posY, posZ, yaw, pitch, roll,
        housingDecorMove.Scale, housingDecorMove.RoomGuid.ToString(), housingDecorMove.AttachParentGuid.ToString());
    TC_LOG_DEBUG("network.opcode", "<<< SMSG_HOUSING_DECOR_MOVE_RESPONSE ({} bytes): {}",
        movePkt->size(), HexDumpPacket(movePkt));
    TC_LOG_DEBUG("housing", "    PlayerGuid={} DecorGuid={} Result={}", response.PlayerGuid.ToString(), response.DecorGuid.ToString(), response.Result);
    SendPacket(movePkt);
}

void WorldSession::HandleHousingDecorRemove(WorldPackets::Housing::HousingDecorRemove const& housingDecorRemove)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingDecorRemoveResponse response;
        response.DecorGuid = housingDecorRemove.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may remove decor.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingDecorRemoveResponse response;
        response.DecorGuid = housingDecorRemove.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    // Too many decor edits from this session too quickly are refused.
    if (!CheckHousingDecorThrottle())
    {
        WorldPackets::Housing::HousingDecorRemoveResponse response;
        response.DecorGuid = housingDecorRemove.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_TOO_MANY_REQUESTS);
        SendPacket(response.Write());
        return;
    }

    uint8 plotIndex = housing->GetPlotIndex();
    ObjectGuid decorGuid = housingDecorRemove.DecorGuid;

    HousingResult result = housing->RemoveDecor(decorGuid);

    // Despawn the decor GO from the map and update Account entity
    if (result == HOUSING_RESULT_SUCCESS)
    {
        // Support both exterior (HousingMap) and interior (HouseInteriorMap)
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
            housingMap->DespawnDecorItem(plotIndex, decorGuid);
        else if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
            interiorMap->DespawnDecorItem(decorGuid);

        // The piece's storage entry stays, with an empty house (hbcd3 1436387); RemoveDecor set it.
        if (GetBattlenetAccount().IsHousingDecorStorageSent())
            GetBattlenetAccount().SendUpdateToPlayer(player);
    }

    // Wire format: PackedGUID DecorGUID + PackedGUID UnkGUID + uint32 Field_13 + uint8 Result
    WorldPackets::Housing::HousingDecorRemoveResponse response;
    response.DecorGuid = decorGuid;
    // UnkGUID and Field_13 stay at defaults (empty/0)
    response.Result = static_cast<uint8>(result);
    WorldPacket const* removePkt = response.Write();
    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_DECOR_REMOVE DecorGuid={}", decorGuid.ToString());
    TC_LOG_DEBUG("network.opcode", "<<< SMSG_HOUSING_DECOR_REMOVE_RESPONSE ({} bytes): {}",
        removePkt->size(), HexDumpPacket(removePkt));
    TC_LOG_DEBUG("housing", "    DecorGuid={} Result={}", decorGuid.ToString(), uint32(result));
    SendPacket(removePkt);
}

void WorldSession::HandleHousingDecorLock(WorldPackets::Housing::HousingDecorLock const& housingDecorLock)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingDecorLockResponse response;
        response.DecorGuid = housingDecorLock.DecorGuid;
        response.PlayerGuid = player->GetGUID();
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may lock or unlock decor.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingDecorLockResponse response;
        response.DecorGuid = housingDecorLock.DecorGuid;
        response.PlayerGuid = player->GetGUID();
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    // Use client's requested lock state (not toggle)
    Housing::PlacedDecor const* decor = housing->GetPlacedDecor(housingDecorLock.DecorGuid);
    if (!decor)
    {
        WorldPackets::Housing::HousingDecorLockResponse response;
        response.DecorGuid = housingDecorLock.DecorGuid;
        response.PlayerGuid = player->GetGUID();
        response.Result = static_cast<uint8>(HOUSING_RESULT_DECOR_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    HousingResult result = housing->SetDecorLocked(housingDecorLock.DecorGuid, housingDecorLock.Locked);

    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_DECOR_LOCK DecorGuid={} Locked={} (entry: {})",
        housingDecorLock.DecorGuid.ToString(), housingDecorLock.Locked, decor->DecorEntryId);

    // Wire format: DecorGUID + PlayerGUID + uint32 Field_16 + uint8 Result + Bits(Locked, Field_17)
    WorldPackets::Housing::HousingDecorLockResponse response;
    response.DecorGuid = housingDecorLock.DecorGuid;
    response.PlayerGuid = player->GetGUID();
    response.Result = static_cast<uint8>(result);
    response.Locked = (result == HOUSING_RESULT_SUCCESS) && housingDecorLock.Locked;
    response.Field_17 = true;
    WorldPacket const* lockPkt = response.Write();
    TC_LOG_DEBUG("network.opcode", "<<< SMSG_HOUSING_DECOR_LOCK_RESPONSE ({} bytes): {}",
        lockPkt->size(), HexDumpPacket(lockPkt));
    TC_LOG_DEBUG("housing", "    DecorGuid={} PlayerGuid={} Field_16={} Result={} Locked={} Field_17={}",
        response.DecorGuid.ToString(), response.PlayerGuid.ToString(),
        response.Field_16, response.Result, response.Locked, response.Field_17);
    SendPacket(lockPkt);
}

void WorldSession::HandleHousingDecorSetDyeSlots(WorldPackets::Housing::HousingDecorSetDyeSlots const& housingDecorSetDyeSlots)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingDecorSystemSetDyeSlotsResponse response;
        response.DecorGuid = housingDecorSetDyeSlots.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may dye decor.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingDecorSystemSetDyeSlotsResponse response;
        response.DecorGuid = housingDecorSetDyeSlots.DecorGuid;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    std::array<uint32, MAX_HOUSING_DYE_SLOTS> dyeSlots = {};
    for (size_t i = 0; i < housingDecorSetDyeSlots.DyeColorID.size() && i < MAX_HOUSING_DYE_SLOTS; ++i)
        dyeSlots[i] = static_cast<uint32>(housingDecorSetDyeSlots.DyeColorID[i]);

    HousingResult result = housing->CommitDecorDyes(housingDecorSetDyeSlots.DecorGuid, dyeSlots);

    WorldPackets::Housing::HousingDecorSystemSetDyeSlotsResponse response;
    response.DecorGuid = housingDecorSetDyeSlots.DecorGuid;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_COMMIT_DYES DecorGuid: {}, Result: {}",
        housingDecorSetDyeSlots.DecorGuid.ToString(), uint32(result));
}

void WorldSession::HandleHousingDecorDeleteFromStorage(WorldPackets::Housing::HousingDecorDeleteFromStorage const& housingDecorDeleteFromStorage)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // The storage belongs to the account and needs no house.
    HousingDecorStore* store = player->GetHousingDecorStore();
    if (!store)
    {
        WorldPackets::Housing::HousingDecorDeleteFromStorageResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_DECOR_NOT_FOUND_IN_STORAGE);
        SendPacket(response.Write());
        return;
    }

    // The per-session decoration throttle is charged per GUID rather than per
    // packet. Place, move and remove each cost one operation against the 40-per-10s
    // budget; this opcode removes up to 31 decor in a single packet. Charging it once
    // would let a client sustain many times the rate the throttle was written to permit.
    // Only pieces in storage can be destroyed; a placed piece is not in storage.
    HousingResult result = HOUSING_RESULT_SUCCESS;
    Battlenet::Account& account = GetBattlenetAccount();
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    for (ObjectGuid const& decorGuid : housingDecorDeleteFromStorage.DecorGuids)
    {
        if (!CheckHousingDecorThrottle())
        {
            result = HOUSING_RESULT_TOO_MANY_REQUESTS;
            break;
        }

        if (!store->DestroyStored(decorGuid, trans))
        {
            result = HOUSING_RESULT_DECOR_NOT_FOUND_IN_STORAGE;
            continue;
        }

        if (account.IsHousingDecorStorageSent())
            account.RemoveHousingDecorStorageEntry(decorGuid);
    }
    CharacterDatabase.CommitTransaction(trans);

    WorldPackets::Housing::HousingDecorDeleteFromStorageResponse response;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    if (account.IsHousingDecorStorageSent())
        account.SendUpdateToPlayer(player);

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_DELETE_FROM_STORAGE Count: {}, Result: {}",
        uint32(housingDecorDeleteFromStorage.DecorGuids.size()), uint32(result));
}

void WorldSession::HandleHousingDecorRequestStorage(WorldPackets::Housing::HousingDecorRequestStorage const& housingDecorRequestStorage)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_REQUEST_STORAGE: Player {} BnetAccountGuid {}",
        player->GetGUID().ToString(), housingDecorRequestStorage.BnetAccountGuid.ToString());

    // Retail answers with success and the new-data flag, with or without a house (hbcd3 351312 before any house:
    // 00 00 00 80, an empty GUID, result 0 and flag 0x80). The storage itself travels in the Battle.net account's
    // update fields. The house list is not part of this answer: the client asks for it with its own
    // CMSG_HOUSING_SVCS_GET_PLAYER_HOUSES_INFO (hbcd3 Numbers 3262 and 3263), and a storage request alone gets no
    // house list (hbcd3 1742117 and 1742161).
    WorldPackets::Housing::HousingDecorRequestStorageResponse response;
    response.ResultCode = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    // Then the account's storage, saved piece by piece, whether or not the account has a house (hbcd3 352093-352181:
    // 12 pieces before the first purchase).
    player->PushHousingDecorStorage();
    Housing* housing = player->GetAccountCatalogHousing();
    if (housing)
        housing->SyncUpdateFields();
    {
        UpdateData updateData(player->GetMapId());
        WorldPacket updatePacket;

        // Account as CREATE (full FHousingStorage_C with Decor map)
        GetBattlenetAccount().BuildCreateUpdateBlockForPlayer(&updateData, player);
        player->m_clientSessionEntityGUIDs.insert(GetBattlenetAccount().GetGUID());

        // HousingPlayerHouseEntity (budgets)
        if (housing)
        {
            if (player->HaveAtClient(&GetHousingPlayerHouseEntity(housing->GetHouseGuid())))
                GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildValuesUpdateBlockForPlayer(&updateData, player);
            else
            {
                GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildCreateUpdateBlockForPlayer(&updateData, player);
                player->m_clientSessionEntityGUIDs.insert(GetHousingPlayerHouseEntity(housing->GetHouseGuid()).GetGUID());
            }
        }

        // Bundle ALL decor MeshObject CREATEs
        uint32 meshCreateCount = 0;
        Map* playerMap = player->GetMap();

        std::unordered_map<ObjectGuid, ObjectGuid> const* decorMap = nullptr;
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(playerMap))
            decorMap = &housingMap->GetDecorGuidMap();
        else if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(playerMap))
            decorMap = &interiorMap->GetDecorGuidMap();

        if (decorMap)
        {
            for (auto const& [decorGuid, meshObjGuid] : *decorMap)
            {
                MeshObject* meshObj = playerMap->GetMeshObject(meshObjGuid);
                if (!meshObj || !meshObj->IsInWorld())
                    continue;

                // A duplicate CREATE for a GUID the client already holds kills it
                // (ACCESS_VIOLATION, null read) - refresh via a values update instead,
                // same as the fixture path below.
                if (player->HaveAtClient(meshObj))
                {
                    meshObj->BuildValuesUpdateBlockForPlayer(&updateData, player);
                    continue;
                }

                meshObj->BuildCreateUpdateBlockForPlayer(&updateData, player);
                player->m_clientGUIDs.insert(meshObjGuid);
                ++meshCreateCount;
            }
        }

        updateData.BuildPacket(&updatePacket);
        player->SendDirectMessage(&updatePacket);

        GetBattlenetAccount().ClearUpdateMask(true);
        if (housing)
            GetHousingPlayerHouseEntity(housing->GetHouseGuid()).ClearUpdateMask(true);

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_REQUEST_STORAGE: Sent Account CREATE + {} decor MeshObject CREATEs", meshCreateCount);
    }
}

void WorldSession::HandleHousingDecorRedeemDeferredDecor(WorldPackets::Housing::HousingDecorRedeemDeferredDecor const& housingDecorRedeemDeferredDecor)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    uint32 const decorEntryId = housingDecorRedeemDeferredDecor.DeferredDecorID;
    uint32 const transactionId = housingDecorRedeemDeferredDecor.RedemptionToken;

    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_DECOR_REDEEM_DEFERRED DecorID={} TransactionID={}", decorEntryId, transactionId);

    HouseDecorData const* decorData = sHousingMgr.GetHouseDecorData(decorEntryId);
    HousingDecorStore* store = player->GetHousingDecorStore();
    if (!decorData || !store)
    {
        FinishHousingDecorRedeem(decorEntryId, transactionId, 0);
        return;
    }

    // What the account is owed of this entry: its starting quantity, and one copy per RetroactiveDecorReward row whose
    // achievement or quest the account has done. Only when the starting quantity is used up are the rewards counted.
    uint32 const redeemed = store->GetRedeemed(decorEntryId);
    std::vector<HousingDecorStore::RetroactiveReward> rewards = HousingDecorStore::GetRetroactiveRewards(decorEntryId);
    if (HousingDecorStore::GetOwedCount(decorData->StartingQuantity, decorData->Flags, 0, redeemed) > 0 || rewards.empty())
    {
        FinishHousingDecorRedeem(decorEntryId, transactionId, 0);
        return;
    }

    // This character first; she is the one in memory.
    uint32 const earnedHere = HousingDecorStore::CountEarnedRetroactiveRewards(rewards,
        [player](uint32 achievementId) { return player->HasAchieved(achievementId); },
        [player](uint32 questId) { return player->IsQuestRewarded(questId); });
    if (HousingDecorStore::GetOwedCount(decorData->StartingQuantity, decorData->Flags, earnedHere, redeemed) > 0)
    {
        FinishHousingDecorRedeem(decorEntryId, transactionId, earnedHere);
        return;
    }

    // Then every character of the Battle.net account, from the database. That lookup costs two database queries, so
    // it is charged against the same per-session budget as placing and moving decor.
    if (!CheckHousingDecorThrottle())
    {
        WorldPackets::Housing::HousingRedeemDeferredDecorResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_TOO_MANY_REQUESTS);
        response.SequenceIndex = transactionId;
        SendPacket(response.Write());
        return;
    }

    std::string achievementIds;
    std::string questIds;
    for (HousingDecorStore::RetroactiveReward const& reward : rewards)
    {
        for (auto const& [achievementId, questId] : reward.Criteria)
        {
            if (achievementId > 0)
                achievementIds += (achievementIds.empty() ? "" : ",") + std::to_string(achievementId);
            if (questId > 0)
                questIds += (questIds.empty() ? "" : ",") + std::to_string(questId);
        }
    }
    if (achievementIds.empty())
        achievementIds = "0";
    if (questIds.empty())
        questIds = "0";

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT_IDS);
    stmt->setUInt32(0, GetBattlenetAccountId());
    GetQueryProcessor().AddCallback(LoginDatabase.AsyncQuery(stmt)
        .WithChainingPreparedCallback([achievementIds, questIds](QueryCallback& chain, PreparedQueryResult gameAccounts)
        {
            std::string accountIds;
            if (gameAccounts)
            {
                do
                {
                    if (!accountIds.empty())
                        accountIds += ',';
                    accountIds += std::to_string(gameAccounts->Fetch()[0].GetUInt32());
                } while (gameAccounts->NextRow());
            }

            // No game account: ask for nothing that can match.
            if (accountIds.empty())
                accountIds = "0";

            chain.SetNextQuery(CharacterDatabase.AsyncQuery(Trinity::StringFormat(
                "SELECT 0, a.achievement FROM character_achievement a JOIN characters c ON c.guid = a.guid "
                "WHERE c.account IN ({0}) AND a.achievement IN ({1}) "
                "UNION SELECT 1, r.quest FROM character_queststatus_rewarded r JOIN characters c ON c.guid = r.guid "
                "WHERE c.account IN ({0}) AND r.quest IN ({2})", accountIds, achievementIds, questIds).c_str()));
        })
        .WithCallback([this, decorEntryId, transactionId, rewards](QueryResult done)
        {
            std::unordered_set<uint32> achievements;
            std::unordered_set<uint32> quests;
            if (done)
            {
                do
                {
                    Field* fields = done->Fetch();
                    (fields[0].GetUInt32() == 0 ? achievements : quests).insert(fields[1].GetUInt32());
                } while (done->NextRow());
            }

            Player* current = GetPlayer();
            uint32 const earned = HousingDecorStore::CountEarnedRetroactiveRewards(rewards,
                [&achievements, current](uint32 achievementId) { return achievements.contains(achievementId) || (current && current->HasAchieved(achievementId)); },
                [&quests, current](uint32 questId) { return quests.contains(questId) || (current && current->IsQuestRewarded(questId)); });
            FinishHousingDecorRedeem(decorEntryId, transactionId, earned);
        }));
}

void WorldSession::FinishHousingDecorRedeem(uint32 decorEntryId, uint32 transactionId, uint32 earnedRetroactiveRewards)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    HouseDecorData const* decorData = sHousingMgr.GetHouseDecorData(decorEntryId);
    HousingDecorStore* store = player->GetHousingDecorStore();

    // A redeem turns one owed copy into a piece; with nothing owed it is refused, echoing the transaction.
    uint32 const owed = decorData && store
        ? HousingDecorStore::GetOwedCount(decorData->StartingQuantity, decorData->Flags, earnedRetroactiveRewards, store->GetRedeemed(decorEntryId))
        : 0;
    if (!owed)
    {
        WorldPackets::Housing::HousingRedeemDeferredDecorResponse response;
        response.Result = static_cast<uint8>(decorData ? HOUSING_RESULT_DECOR_CANNOT_BE_REDEEMED : HOUSING_RESULT_DECOR_NOT_FOUND);
        response.SequenceIndex = transactionId;
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_REDEEM_DEFERRED: {} is owed no decor {}, refused", player->GetGUID().ToString(), decorEntryId);
        return;
    }

    bool firstOwned = false;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    Housing::PlacedDecor const piece = store->CreateStored(decorEntryId, DECOR_SOURCE_REDEEMED, {}, firstOwned, trans);
    store->AddRedeemed(decorEntryId, trans);

    // Retail's order (hled1 788897-789026, 790913-790928): the reply with the new piece's GUID, then the account's
    // update with that piece in storage (source 3, no house, no value). No add-to-chest and no first-time message.
    WorldPackets::Housing::HousingRedeemDeferredDecorResponse response;
    response.DecorGuid = piece.Guid;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.SequenceIndex = transactionId;
    SendPacket(response.Write());

    // The first update of a session carries the whole storage (hled1 788973-789026); later ones only the new piece.
    Battlenet::Account& account = GetBattlenetAccount();
    if (account.IsHousingDecorStorageSent())
        account.SetHousingDecorStorageEntry(piece.Guid, ObjectGuid::Empty, piece.SourceType, piece.SourceValue);
    else
        player->PushHousingDecorStorage();
    account.SendUpdateToPlayer(player);

    // Then the piece is saved with the GUID that was sent.
    CharacterDatabase.CommitTransaction(trans);

    // Nothing else follows a redeem. Retail sent no house experience update and no "collect unique decor" criteria
    // update for any of three redeems of entries that carry a first-acquisition bonus (hbcd3 Numbers 16637-16738: the
    // reply and the account update only). The saved piece already counts the entry as owned.

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_REDEEM_DEFERRED: {} redeemed decor {} as {} (transaction {}, {} owed before)",
        player->GetGUID().ToString(), decorEntryId, piece.Guid.ToString(), transactionId, owed);
}

// ============================================================
// Fixture System
// ============================================================

// Sniff-verified helper: After any fixture mutation (SetHouseType, SetCoreFixture,
// CreateFixture, etc.), retail sends an UPDATE_OBJECT carrying the changed MeshObject
// and house entity data. This sends it inline so the client gets it immediately
// rather than waiting for the next map tick.
void WorldSession::SendFixtureUpdateObject(Player* player, Housing* housing)
{
    if (!player || !housing)
        return;

    player->BuildUpdateChangesMask();
    GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildUpdateChangesMask();

    UpdateData updateData(player->GetMapId());
    WorldPacket updatePacket;

    // Player VALUES_UPDATE (editor mode / flags)
    player->BuildValuesUpdateBlockForPlayer(&updateData, player);

    // House entity VALUES_UPDATE (budget/type fields)
    if (player->HaveAtClient(&GetHousingPlayerHouseEntity(housing->GetHouseGuid())))
        GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildValuesUpdateBlockForPlayer(&updateData, player);
    else
    {
        GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildCreateUpdateBlockForPlayer(&updateData, player);
        player->m_clientSessionEntityGUIDs.insert(GetHousingPlayerHouseEntity(housing->GetHouseGuid()).GetGUID());
    }

    // Include CREATE for any new MeshObjects spawned by the mutation
    if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
    {
        uint8 plotIndex = housing->GetPlotIndex();
        auto const& meshMap = housingMap->GetPlotMeshObjects();
        auto meshItr = meshMap.find(plotIndex);
        if (meshItr != meshMap.end())
        {
            for (ObjectGuid const& meshGuid : meshItr->second)
            {
                MeshObject* meshObj = housingMap->GetMeshObject(meshGuid);
                if (!meshObj || !meshObj->IsInWorld())
                    continue;

                if (player->HaveAtClient(meshObj))
                    meshObj->BuildValuesUpdateBlockForPlayer(&updateData, player);
                else
                {
                    meshObj->BuildCreateUpdateBlockForPlayer(&updateData, player);
                    player->m_clientGUIDs.insert(meshGuid);
                }
            }
        }
    }

    updateData.BuildPacket(&updatePacket);
    player->SendDirectMessage(&updatePacket);

    player->ClearUpdateMask(false);
    GetHousingPlayerHouseEntity(housing->GetHouseGuid()).ClearUpdateMask(true);
}

void WorldSession::HandleHousingFixtureSetEditMode(WorldPackets::Housing::HousingFixtureSetEditMode const& housingFixtureSetEditMode)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingFixtureSetEditModeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only a character of the house's account, on its plot or inside it, may enter fixture edit. Leaving is always
    // allowed.
    if (housingFixtureSetEditMode.Active && !PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingFixtureSetEditModeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    bool const entering = housingFixtureSetEditMode.Active;

    // Retail's order (hled1 816931-817196 entering, 826232-826289 leaving): the exterior lock reply, then on the way in
    // the pet leaving, then the fixture editor's aura or its removal, which roots her in the air with the hover
    // animation, pacifies and silences her and stops her actions, then the edit-mode reply, then her own update with
    // the new editor mode.
    housing->SetEditorMode(entering ? HOUSING_EDITOR_MODE_EXTERIOR_CUSTOMIZATION : HOUSING_EDITOR_MODE_NONE);

    // Prepare catalog/storage data before sending packets (enter only).
    if (entering)
    {
        player->PushHousingDecorStorage();
        housing->SyncUpdateFields();
    }

    // CRITICAL: Clear Account entity dirty state BEFORE sending any packets.
    // PushHousingDecorStorage() modifies FHousingStorage_C which marks the
    // Account entity dirty. Unlike decor edit mode, fixture edit mode doesn't send
    // the Account entity as CREATE here. If we leave it dirty, Map::SendObjectUpdates()
    // will send a VALUES_UPDATE on the next tick, which the client rejects because
    // MapUpdateField entries can't be added via VALUES_UPDATE when initially empty.
    // Also clear on EXIT to prevent any lingering dirty state from decor operations.
    GetBattlenetAccount().ClearUpdateMask(true);

    // The lock reply names the house (hled1 816934, 826239). It goes out only on the plot's exterior, where the house
    // can be locked; inside the house retail's edit modes send none. Leaving answers the lock entering took.
    if (entering ? dynamic_cast<HousingMap*>(player->GetMap()) != nullptr : housing->GetExteriorLockHolder() == player->GetGUID())
    {
        if (entering)
            housing->SetExteriorLockHolder(player->GetGUID());
        else
            housing->ReleaseExteriorLock(player->GetGUID());

        WorldPackets::Housing::HouseExteriorLockResponse lockResponse;
        lockResponse.HouseGuid = housing->GetHouseGuid();
        lockResponse.EditorPlayerGuid = player->GetGUID();
        lockResponse.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
        lockResponse.Active = entering;
        SendPacket(lockResponse.Write());
    }

    if (entering)
    {
        // Retail sent the pet away as she entered (hled1 816940-817351: no pet GUIDs, the pet's spells cleared, the pet
        // destroyed) and did not bring it back when she left, in the 55 seconds the capture ran on.
        player->UnsummonPetTemporaryIfAny();

        if (sSpellMgr->GetSpellInfo(SPELL_HOUSING_FIXTURE_EDITOR_LOCKOUT, DIFFICULTY_NONE))
            player->CastSpell(player, SPELL_HOUSING_FIXTURE_EDITOR_LOCKOUT, CastSpellExtraArgs(TRIGGERED_FULL_MASK));
        else
            TC_LOG_ERROR("housing", "HandleHousingFixtureSetEditMode: spell {} is missing from the spell data, so {} is not held in place "
                "while she edits fixtures", SPELL_HOUSING_FIXTURE_EDITOR_LOCKOUT, player->GetGUID().ToString());
    }
    else
        player->RemoveAurasDueToSpell(SPELL_HOUSING_FIXTURE_EDITOR_LOCKOUT);

    // The house is always empty; the character is named on the way in and empty on the way out (hled1 817176, 826284).
    WorldPackets::Housing::HousingFixtureSetEditModeResponse response;
    if (entering)
        response.EditorPlayerGuid = player->GetGUID();
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    // Her update with the flags the aura set and the editor mode follows the reply (hled1 817196: EditorMode 3, in the
    // same update as her pet's values). The map's own object update carries it, so everyone who can see her also gets
    // the flags the aura set.

    TC_LOG_DEBUG("housing", "HandleHousingFixtureSetEditMode: {} {} fixture edit of house {}", player->GetGUID().ToString(),
        entering ? "entered" : "left", housing->GetHouseGuid().ToString());
}

void WorldSession::HandleHousingFixtureSetCoreFixture(WorldPackets::Housing::HousingFixtureSetCoreFixture const& housingFixtureSetCoreFixture)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingFixtureSetCoreFixtureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may change fixtures.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingFixtureSetCoreFixtureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    // Validate ExteriorComponentID against DB2 store
    uint32 componentID = housingFixtureSetCoreFixture.ExteriorComponentID;

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_CORE_FIXTURE: FixtureGuid={} ExteriorComponentID={} (store has {} entries)",
        housingFixtureSetCoreFixture.FixtureGuid.ToString(), componentID, sExteriorComponentStore.GetNumRows());

    ExteriorComponentEntry const* componentEntry = sExteriorComponentStore.LookupEntry(componentID);
    if (!componentEntry)
    {
        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_CORE_FIXTURE ExteriorComponentID {} not found in DB2 (store has {} entries)",
            componentID, sExteriorComponentStore.GetNumRows());
        WorldPackets::Housing::HousingFixtureSetCoreFixtureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_FIXTURE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_CORE_FIXTURE DB2 lookup OK: ExteriorComponentID={}, Name='{}', Type={}, Size={}, Flags={}, ParentCompID={}, WmoDataID={}",
        componentID, componentEntry->Name[DEFAULT_LOCALE] ? componentEntry->Name[DEFAULT_LOCALE] : "",
        componentEntry->Type, componentEntry->Size, componentEntry->Flags,
        componentEntry->ParentComponentID, componentEntry->HouseExteriorWmoDataID);

    std::vector<uint32> removedHookIDs;
    HousingResult result = housing->SelectFixtureOption(componentID, 0, &removedHookIDs);

    WorldPackets::Housing::HousingFixtureSetCoreFixtureResponse response;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    if (result == HOUSING_RESULT_SUCCESS)
    {
        WorldPackets::Housing::AccountExteriorFixtureCollectionUpdate collectionUpdate;
        collectionUpdate.AddSingle(componentID);
        SendPacket(collectionUpdate.Write());

        // Respawn house visuals so the new fixture is visible immediately.
        // DespawnHouseForPlot removes ALL MeshObjects (house + decor), so we
        // must also respawn decor after rebuilding the house structure.
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
        {
            uint8 plotIndex = housing->GetPlotIndex();
            auto fixtureOverrides = housing->GetFixtureOverrideMap();
            auto rootOverrides = housing->GetRootComponentOverrides();
            // A house the owner moved stays where they put it, as HousingMap::SpawnPlotGameObjects spawns it.
            Position const housePos = housing->GetHousePosition();
            housingMap->DespawnAllDecorForPlot(plotIndex);
            housingMap->DespawnHouseForPlot(plotIndex);
            housingMap->SpawnHouseForPlot(plotIndex, housing->HasCustomPosition() ? &housePos : nullptr,
                static_cast<int32>(housing->GetCoreExteriorComponentID()),
                static_cast<int32>(housing->GetHouseType()),
                fixtureOverrides.empty() ? nullptr : &fixtureOverrides,
                rootOverrides.empty() ? nullptr : &rootOverrides);
            housingMap->SpawnAllDecorForPlot(plotIndex, housing);
        }

        // Sniff-verified: UPDATE_OBJECT follows the response
        SendFixtureUpdateObject(player, housing);
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_CORE_FIXTURE FixtureGuid={} ExteriorComponentID={} Result={}",
        housingFixtureSetCoreFixture.FixtureGuid.ToString(), componentID, uint32(result));
}

void WorldSession::HandleHousingFixtureCreateFixture(WorldPackets::Housing::HousingFixtureCreateFixture const& housingFixtureCreateFixture)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    WorldPackets::Housing::HousingFixtureCreateFixtureResponse response;
    auto reply = [&](HousingResult result)
    {
        response.Result = static_cast<uint8>(result);
        SendPacket(response.Write());
    };

    // hled1 818926: the house, the piece that owns the hook, the hook, the component, and a byte of unknown meaning.
    Housing* housing = player->GetHousingByGuid(housingFixtureCreateFixture.HouseGuid);
    if (!housing)
        return reply(HOUSING_RESULT_HOUSE_NOT_FOUND);

    if (!PlayerCanEditHousing(player, housing))
        return reply(HOUSING_RESULT_NOT_ON_OWNED_PLOT);

    uint32 const hookID = housingFixtureCreateFixture.ExteriorComponentHookID;
    uint32 const componentID = housingFixtureCreateFixture.ExteriorComponentID;

    ExteriorComponentHookEntry const* hookEntry = sExteriorComponentHookStore.LookupEntry(hookID);
    ExteriorComponentEntry const* compEntry = sExteriorComponentStore.LookupEntry(componentID);
    if (!hookEntry || !compEntry)
    {
        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_CREATE_FIXTURE: hook {} or component {} is not in the client data", hookID, componentID);
        return reply(HOUSING_RESULT_FIXTURE_NOT_FOUND);
    }

    // The exterior's pieces stand on the neighborhood map, and the piece the client names must be the one of this
    // house that owns the hook: the wall 1004 for hook 17265, the roof 3811 for hook 17222 (hled1 818926, 824099).
    HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
    if (!housingMap)
        return reply(HOUSING_RESULT_INVALID_MAP);

    uint8 const plotIndex = housing->GetPlotIndex();
    MeshObject const* attachParent = housingMap->GetPlotMeshObject(plotIndex, housingFixtureCreateFixture.AttachParentGuid);
    if (!attachParent || attachParent->GetExteriorComponentID() != static_cast<int32>(hookEntry->ExteriorComponentID))
    {
        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_CREATE_FIXTURE: {} is not the piece of house {} that owns hook {} (component {})",
            housingFixtureCreateFixture.AttachParentGuid.ToString(), housing->GetHouseGuid().ToString(), hookID,
            hookEntry->ExteriorComponentID);
        return reply(HOUSING_RESULT_HOOK_NOT_CHILD_OF_FIXTURE);
    }

    std::vector<uint32> removedHookIDs;
    HousingResult result = housing->SelectFixtureOption(hookID, componentID, &removedHookIDs);
    if (result != HOUSING_RESULT_SUCCESS)
        return reply(result);

    // The piece that was on the hook goes, with the door when it was an entry, and a door on another hook goes too:
    // a house has one door (hled1 818926-819008, where the entry moved from hook 17262 to 17265).
    std::vector<ObjectGuid> destroyed;
    removedHookIDs.push_back(hookID);
    for (uint32 removedHook : removedHookIDs)
        if (MeshObject* oldMesh = housingMap->FindMeshObjectByHookID(plotIndex, static_cast<int32>(removedHook)))
            housingMap->DespawnSingleMeshObject(plotIndex, oldMesh->GetGUID(), &destroyed);

    // The new piece, and its door when it is an entry, are kept back from her while they are added; everyone else
    // gets them as usual.
    HousingMap::HeldBackCreates creates;
    creates.Viewer = player;
    housingMap->BeginHoldingBackCreates(&creates);
    MeshObject* newMesh = housingMap->SpawnFixtureAtHook(plotIndex, hookID, componentID, housing->GetHouseGuid(),
        static_cast<int32>(housing->GetHouseType()), housingFixtureCreateFixture.AttachParentGuid);
    if (newMesh && compEntry->Type == HOUSING_FIXTURE_TYPE_DOOR)
        housingMap->RespawnDoorGOAtHook(plotIndex, hookID, componentID, housing);
    housingMap->EndHoldingBackCreates();

    if (!newMesh)
        TC_LOG_ERROR("housing", "CMSG_HOUSING_FIXTURE_CREATE_FIXTURE: component {} was saved on hook {} of house {} but could not be built",
            componentID, hookID, housing->GetHouseGuid().ToString());

    // The reply names the new piece as soon as it exists (hled1 818935), then one update destroys what went and
    // creates the new piece, and the new door follows.
    response.FixtureGuid = newMesh ? newMesh->GetFixtureGuid() : ObjectGuid::Empty;
    reply(HOUSING_RESULT_SUCCESS);
    housingMap->SendHeldBackCreates(creates, destroyed);

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_CREATE_FIXTURE: {} put component {} on hook {} of house {} ({} objects removed)",
        player->GetGUID().ToString(), componentID, hookID, housing->GetHouseGuid().ToString(), uint32(destroyed.size()));
}

void WorldSession::HandleHousingFixtureDeleteFixture(WorldPackets::Housing::HousingFixtureDeleteFixture const& housingFixtureDeleteFixture)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingFixtureDeleteFixtureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may remove fixtures.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingFixtureDeleteFixtureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    uint32 componentID = housingFixtureDeleteFixture.ExteriorComponentID;
    uint32 originalID = componentID; // preserve original for RemoveFixture key lookup

    // The client may send either an ExteriorComponentID or an ExteriorComponentHookID
    // depending on the fixture type. Try the ExteriorComponent store first, then fall back
    // to resolving via ExteriorComponentHook → ExteriorComponent for DB2 validation.
    ExteriorComponentEntry const* componentEntry = sExteriorComponentStore.LookupEntry(componentID);
    if (!componentEntry)
    {
        // Try as a HookID — resolve to the parent ExteriorComponentID for validation only.
        // Keep originalID as the hookID for RemoveFixture (fixtures are keyed by hookID).
        ExteriorComponentHookEntry const* hookEntry = sExteriorComponentHookStore.LookupEntry(componentID);
        if (hookEntry)
        {
            componentEntry = sExteriorComponentStore.LookupEntry(hookEntry->ExteriorComponentID);
            if (componentEntry)
            {
                TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_DELETE_FIXTURE: Resolved hookID {} → ExteriorComponentID {} (using hookID as key)",
                    componentID, hookEntry->ExteriorComponentID);
                // DON'T overwrite componentID — keep the hookID for RemoveFixture
            }
        }
    }

    if (!componentEntry)
    {
        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_DELETE_FIXTURE ExteriorComponentID/HookID {} not found in DB2", componentID);
        WorldPackets::Housing::HousingFixtureDeleteFixtureResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_FIXTURE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_DELETE_FIXTURE DB2 lookup: ExteriorComponentID={}, Name='{}', Type={}, Flags={}",
        componentID, componentEntry->Name[DEFAULT_LOCALE] ? componentEntry->Name[DEFAULT_LOCALE] : "", componentEntry->Type, componentEntry->Flags);

    // Use originalID (hookID when client sent a hook, componentID otherwise) for fixture lookup.
    // RemoveFixture searches by key first (hookID), then by OptionId (componentID).
    uint32 removedHookID = 0;
    HousingResult result = housing->RemoveFixture(originalID, &removedHookID);

    WorldPackets::Housing::HousingFixtureDeleteFixtureResponse response;
    response.Result = static_cast<uint8>(result);
    response.FixtureGuid = housingFixtureDeleteFixture.FixtureGuid;
    SendPacket(response.Write());

    if (result == HOUSING_RESULT_SUCCESS)
    {
        // Targeted fixture mesh removal: only despawn the mesh at the removed hook,
        // then spawn the default component back. No full house rebuild needed.
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
        {
            uint8 plotIndex = housing->GetPlotIndex();

            // Remove the player's custom mesh at this hook
            if (MeshObject* oldMesh = housingMap->FindMeshObjectByHookID(plotIndex, static_cast<int32>(removedHookID)))
            {
                TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_DELETE_FIXTURE: Despawning mesh {} at hook {}",
                    oldMesh->GetGUID().ToString(), removedHookID);
                housingMap->DespawnSingleMeshObject(plotIndex, oldMesh->GetGUID());
            }

            // Do NOT spawn a default fixture back — the user selected "None" to remove it.
            // The hook point should remain empty so the client shows the fixture point UI again.
            // If this was a door, remove the door GO — but only if no other door
            // override remains. When the client MOVES a door (CREATE-at-new-hook
            // immediately followed by DELETE-at-old-hook), the new GO is already
            // on the map; blindly despawning here would wipe it.
            if (componentEntry && componentEntry->Type == HOUSING_FIXTURE_TYPE_DOOR)
            {
                bool anotherDoorExists = false;
                for (auto const& [hookID, compID] : housing->GetFixtureOverrideMap())
                {
                    ExteriorComponentEntry const* otherComp = sExteriorComponentStore.LookupEntry(compID);
                    if (otherComp && otherComp->Type == HOUSING_FIXTURE_TYPE_DOOR)
                    {
                        anotherDoorExists = true;
                        break;
                    }
                }
                if (!anotherDoorExists)
                    housingMap->DespawnDoorGO(plotIndex);
            }
        }

        // Sniff-verified: UPDATE_OBJECT follows the response
        SendFixtureUpdateObject(player, housing);
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_DELETE_FIXTURE FixtureGuid={} ExteriorComponentID={} Hook={} Result={}",
        housingFixtureDeleteFixture.FixtureGuid.ToString(), componentID, removedHookID, uint32(result));
}

void WorldSession::HandleHousingFixtureSetHouseSize(WorldPackets::Housing::HousingFixtureSetHouseSize const& housingFixtureSetHouseSize)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = ResolveRequestedHousing(player, housingFixtureSetHouseSize.HouseGuid);
    if (!housing)
    {
        WorldPackets::Housing::HousingFixtureSetHouseSizeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may change the house's size.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingFixtureSetHouseSizeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    // Validate house size against HousingFixtureSize enum (1=Any, 2=Small, 3=Medium, 4=Large)
    uint8 requestedSize = housingFixtureSetHouseSize.Size;
    if (requestedSize < HOUSING_FIXTURE_SIZE_ANY || requestedSize > HOUSING_FIXTURE_SIZE_LARGE)
    {
        WorldPackets::Housing::HousingFixtureSetHouseSizeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_EXTERIOR_SIZE_NOT_AVAILABLE);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_HOUSE_SIZE HouseGuid: {}, Size: {} REJECTED (invalid size)",
            housingFixtureSetHouseSize.HouseGuid.ToString(), requestedSize);
        return;
    }

    // Reject if already that size
    if (requestedSize == housing->GetHouseSize())
    {
        WorldPackets::Housing::HousingFixtureSetHouseSizeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_EXTERIOR_ALREADY_THAT_SIZE);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_HOUSE_SIZE HouseGuid: {}, Size: {} REJECTED (already that size)",
            housingFixtureSetHouseSize.HouseGuid.ToString(), requestedSize);
        return;
    }

    // Persist the new house size
    housing->SetHouseSize(requestedSize);

    // Respawn house MeshObjects with updated size (must also respawn decor since DespawnHouseForPlot removes all MeshObjects)
    if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
    {
        uint8 plotIndex = housing->GetPlotIndex();
        auto fixtureOverrides = housing->GetFixtureOverrideMap();
        auto rootOverrides = housing->GetRootComponentOverrides();
        // A house the owner moved stays where they put it, as HousingMap::SpawnPlotGameObjects spawns it.
        Position const housePos = housing->GetHousePosition();
        housingMap->DespawnAllDecorForPlot(plotIndex);
        housingMap->DespawnHouseForPlot(plotIndex);
        housingMap->SpawnHouseForPlot(plotIndex, housing->HasCustomPosition() ? &housePos : nullptr,
            static_cast<int32>(housing->GetCoreExteriorComponentID()),
            static_cast<int32>(housing->GetHouseType()),
            fixtureOverrides.empty() ? nullptr : &fixtureOverrides,
            rootOverrides.empty() ? nullptr : &rootOverrides);
        housingMap->SpawnAllDecorForPlot(plotIndex, housing);
    }

    WorldPackets::Housing::HousingFixtureSetHouseSizeResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.Size = requestedSize;
    SendPacket(response.Write());

    // Sniff-verified: UPDATE_OBJECT follows the response
    SendFixtureUpdateObject(player, housing);

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_HOUSE_SIZE HouseGuid={} Size={} Result=SUCCESS",
        housingFixtureSetHouseSize.HouseGuid.ToString(), requestedSize);
}

void WorldSession::HandleHousingFixtureSetHouseType(WorldPackets::Housing::HousingFixtureSetHouseType const& housingFixtureSetHouseType)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = ResolveRequestedHousing(player, housingFixtureSetHouseType.HouseGuid);
    if (!housing)
    {
        WorldPackets::Housing::HousingFixtureSetHouseTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Only an owner, on the house's plot or inside it, may change the house's style.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingFixtureSetHouseTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    uint32 wmoDataID = housingFixtureSetHouseType.HouseExteriorWmoDataID;

    // Validate the requested house type exists in the HouseExteriorWmoData DB2 store
    HouseExteriorWmoData const* wmoData = sHousingMgr.GetHouseExteriorWmoData(wmoDataID);
    if (!wmoData)
    {
        WorldPackets::Housing::HousingFixtureSetHouseTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_EXTERIOR_TYPE_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_HOUSE_TYPE HouseGuid: {}, WmoDataID: {} REJECTED (not found in DB2)",
            housingFixtureSetHouseType.HouseGuid.ToString(), wmoDataID);
        return;
    }

    // Reject if already that type
    if (wmoDataID == housing->GetHouseType())
    {
        WorldPackets::Housing::HousingFixtureSetHouseTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_EXTERIOR_ALREADY_THAT_TYPE);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_HOUSE_TYPE HouseGuid: {}, WmoDataID: {} REJECTED (already that type)",
            housingFixtureSetHouseType.HouseGuid.ToString(), wmoDataID);
        return;
    }

    // Persist the new house type
    housing->SetHouseType(wmoDataID);

    // Respawn house MeshObjects with updated type (must also respawn decor since DespawnHouseForPlot removes all MeshObjects)
    if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
    {
        uint8 plotIndex = housing->GetPlotIndex();
        auto fixtureOverrides = housing->GetFixtureOverrideMap();
        auto rootOverrides = housing->GetRootComponentOverrides();
        // A house the owner moved stays where they put it, as HousingMap::SpawnPlotGameObjects spawns it.
        Position const housePos = housing->GetHousePosition();
        housingMap->DespawnAllDecorForPlot(plotIndex);
        housingMap->DespawnHouseForPlot(plotIndex);
        housingMap->SpawnHouseForPlot(plotIndex, housing->HasCustomPosition() ? &housePos : nullptr,
            static_cast<int32>(housing->GetCoreExteriorComponentID()),
            static_cast<int32>(wmoDataID),
            fixtureOverrides.empty() ? nullptr : &fixtureOverrides,
            rootOverrides.empty() ? nullptr : &rootOverrides);
        housingMap->SpawnAllDecorForPlot(plotIndex, housing);
    }

    // Sniff-verified packet order: SMSG response → UPDATE_OBJECT (~228B)
    WorldPackets::Housing::HousingFixtureSetHouseTypeResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.HouseExteriorTypeID = wmoDataID;
    SendPacket(response.Write());

    // Notify account of house type collection update
    WorldPackets::Housing::AccountHouseTypeCollectionUpdate collectionUpdate;
    collectionUpdate.AddSingle(wmoDataID);
    SendPacket(collectionUpdate.Write());

    // Sniff-verified: UPDATE_OBJECT follows the response, carrying updated MeshObject
    // data for the new house type. Send inline so client gets it immediately.
    SendFixtureUpdateObject(player, housing);

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_FIXTURE_SET_HOUSE_TYPE HouseGuid={} WmoDataID={} Result=SUCCESS",
        housingFixtureSetHouseType.HouseGuid.ToString(), wmoDataID);
}

// ============================================================
// Room System
// ============================================================

void WorldSession::HandleHousingRoomSetLayoutEditMode(WorldPackets::Housing::HousingRoomSetLayoutEditMode const& housingRoomSetLayoutEditMode)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomSetLayoutEditModeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomSetLayoutEditModeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    housing->SetEditorMode(housingRoomSetLayoutEditMode.Active ? HOUSING_EDITOR_MODE_LAYOUT : HOUSING_EDITOR_MODE_NONE);

    // Sniff-verified: retail sets UNIT_FLAG_PACIFIED, UNIT_FLAG2_NO_ACTIONS,
    // and SilencedSchoolMask=127 during layout edit mode. These prevent casting/actions
    // and are included in the same UpdateObject that carries EditorMode.
    if (housingRoomSetLayoutEditMode.Active)
    {
        player->SetUnitFlag(UNIT_FLAG_PACIFIED);
        player->SetUnitFlag2(UNIT_FLAG2_NO_ACTIONS);
        player->ReplaceAllSilencedSchoolMask(SPELL_SCHOOL_MASK_ALL);
    }
    else
    {
        player->RemoveUnitFlag(UNIT_FLAG_PACIFIED);
        player->RemoveUnitFlag2(UNIT_FLAG2_NO_ACTIONS);
        player->ReplaceAllSilencedSchoolMask(SpellSchoolMask(0));
    }

    // Play/remove the plot boundary spell visual on the player's plot AT.
    // Sniff-verified: the glowing border decal is visible in ALL edit modes (decor, fixture, room).
    if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
    {
        if (AreaTrigger* plotAt = housingMap->GetPlotAreaTrigger(housing->GetPlotIndex()))
        {
            if (housingRoomSetLayoutEditMode.Active)
                plotAt->PlaySpellVisual(510142);
        }
    }

    // Sniff-verified (build 66838) wire format (10B both enter and exit):
    //   Enter: [PackedGUID PlayerGuid] 00 80
    //   Exit:  [PackedGUID PlayerGuid] 00 00
    WorldPackets::Housing::HousingRoomSetLayoutEditModeResponse response;
    response.PlayerGuid = player->GetGUID(); // Sniff-verified: retail sends Player GUID
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.Active = housingRoomSetLayoutEditMode.Active;
    SendPacket(response.Write());

    // Sync entity data for layout mode (enter needs budget values)
    if (housingRoomSetLayoutEditMode.Active)
    {
        player->PushHousingDecorStorage();
        housing->SyncUpdateFields();
    }

    // Sniff-verified: UPDATE_OBJECT (~56B) follows response for BOTH enter AND exit.
    // This carries EditorMode + UNIT_FLAG_PACIFIED + UNIT_FLAG2_NO_ACTIONS + SilencedSchoolMask.
    // On enter, also include account/entity data for the layout editor budgets.
    {
        player->BuildUpdateChangesMask();

        UpdateData updateData(player->GetMapId());
        WorldPacket updatePacket;
        player->BuildValuesUpdateBlockForPlayer(&updateData, player);

        if (housingRoomSetLayoutEditMode.Active)
        {
            GetBattlenetAccount().BuildUpdateChangesMask();
            GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildUpdateChangesMask();

            if (player->HaveAtClient(&GetBattlenetAccount()))
                GetBattlenetAccount().BuildValuesUpdateBlockForPlayer(&updateData, player);
            else
            {
                GetBattlenetAccount().BuildCreateUpdateBlockForPlayer(&updateData, player);
                player->m_clientSessionEntityGUIDs.insert(GetBattlenetAccount().GetGUID());
            }

            if (player->HaveAtClient(&GetHousingPlayerHouseEntity(housing->GetHouseGuid())))
                GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildValuesUpdateBlockForPlayer(&updateData, player);
            else
            {
                GetHousingPlayerHouseEntity(housing->GetHouseGuid()).BuildCreateUpdateBlockForPlayer(&updateData, player);
                player->m_clientSessionEntityGUIDs.insert(GetHousingPlayerHouseEntity(housing->GetHouseGuid()).GetGUID());
            }
        }

        updateData.BuildPacket(&updatePacket);
        player->SendDirectMessage(&updatePacket);

        player->ClearUpdateMask(false);
        if (housingRoomSetLayoutEditMode.Active)
        {
            GetBattlenetAccount().ClearUpdateMask(true);
            GetHousingPlayerHouseEntity(housing->GetHouseGuid()).ClearUpdateMask(true);
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_SET_LAYOUT_EDIT_MODE Active={}", housingRoomSetLayoutEditMode.Active);
}

void WorldSession::HandleHousingRoomAdd(WorldPackets::Housing::HousingRoomAdd const& housingRoomAdd)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomAddResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomAddResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid newRoomGuid;
    HousingResult result = AddHousingRoomAtDoor(housing, housingRoomAdd.TargetDoorComponentID, housingRoomAdd.HouseRoomID, &newRoomGuid,
        [&](HousingResult placeResult)
    {
        // Sniff order: the response goes out before the new room's objects.
        WorldPackets::Housing::HousingRoomAddResponse response;
        response.Result = static_cast<uint8>(placeResult);
        response.PlayerGuid = player->GetGUID(); // Sniff-verified: retail sends Player GUID, not room GUID
        SendPacket(response.Write());
    });

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_ADD DoorComponentID: {}, HouseRoomID: {}, Room: {}, Result: {}",
        housingRoomAdd.TargetDoorComponentID, housingRoomAdd.HouseRoomID, newRoomGuid.ToString(), uint32(result));
}

HousingResult WorldSession::AddHousingRoomAtDoor(Housing* housing, uint32 targetDoorComponentID, uint32 houseRoomID, ObjectGuid* outRoomGuid,
    std::function<void(HousingResult)> const& onPlaced /*= nullptr*/)
{
    // The CMSG sends TargetDoorComponentID — find which room owns this door,
    // determine the door's direction, and compute the 2D grid position for the new room.
    int32 newGridX = 0, newGridY = 0, newFloorIndex = 0;
    uint32 nextSlot = 0;
    {
        // Find the source room that owns the target door component
        uint32 doorCompId = targetDoorComponentID;
        for (auto const& [guid, room] : housing->GetRoomsMap())
        {
            if (room.SlotIndex >= nextSlot)
                nextSlot = room.SlotIndex + 1;

            // Check if this room contains the target door component
            HouseRoomData const* rd = sHousingMgr.GetHouseRoomData(room.RoomEntryId);
            if (!rd) continue;
            std::vector<RoomComponentData> const* comps = sHousingMgr.GetRoomComponents(rd->RoomWmoDataID);
            if (!comps) continue;

            for (auto const& comp : *comps)
            {
                if (comp.ID == doorCompId)
                {
                    // Compute new room position from door offsets.
                    // newCenter = sourceCenter + sourceDoorOffset - newDoorOffset
                    // where newDoorOffset is the OPPOSITE door of the new room.
                    // Source door at +12 → new room's left door at -12 → spacing = 24.
                    // Source door at +3 → new room's left door at -12 → spacing = 15.
                    float sourceDoorOffset = 0.0f;
                    float newDoorOffset = 0.0f;

                    // Find the new room's wall in the opposite direction.
                    // Don't filter by ConnectionType — stairwell walls have CT=0.
                    // Use the LARGEST offset wall (furthest boundary) for spacing.
                    HouseRoomData const* newRd = sHousingMgr.GetHouseRoomData(houseRoomID);
                    std::vector<RoomComponentData> const* newComps = newRd ? sHousingMgr.GetRoomComponents(newRd->RoomWmoDataID) : nullptr;

                    // Helper: find the most extreme wall offset in a direction
                    auto findMaxWallOffset = [&](std::vector<RoomComponentData> const* cs, int axis, bool negative) -> float
                    {
                        float best = 0.0f;
                        if (!cs) return best;
                        for (auto const& nc : *cs)
                        {
                            if (nc.Type != 1) continue; // walls only
                            float v = (axis == 0) ? nc.OffsetPos[0] : nc.OffsetPos[1];
                            if (negative && v < -0.5f && v < best) best = v;
                            if (!negative && v > 0.5f && v > best) best = v;
                        }
                        return best;
                    };

                    if (comp.OffsetPos[0] > 0.5f) // source door faces +X
                    {
                        sourceDoorOffset = comp.OffsetPos[0];
                        newDoorOffset = findMaxWallOffset(newComps, 0, true); // -X wall
                        newGridX = room.GridX + static_cast<int32>(sourceDoorOffset - newDoorOffset);
                        newGridY = room.GridY;
                    }
                    else if (comp.OffsetPos[0] < -0.5f) // source door faces -X
                    {
                        sourceDoorOffset = comp.OffsetPos[0];
                        newDoorOffset = findMaxWallOffset(newComps, 0, false); // +X wall
                        newGridX = room.GridX + static_cast<int32>(sourceDoorOffset - newDoorOffset);
                        newGridY = room.GridY;
                    }
                    else if (comp.OffsetPos[1] > 0.5f) // source door faces +Y
                    {
                        sourceDoorOffset = comp.OffsetPos[1];
                        newDoorOffset = findMaxWallOffset(newComps, 1, true); // -Y wall
                        newGridX = room.GridX;
                        newGridY = room.GridY + static_cast<int32>(sourceDoorOffset - newDoorOffset);
                    }
                    else if (comp.OffsetPos[1] < -0.5f) // source door faces -Y
                    {
                        sourceDoorOffset = comp.OffsetPos[1];
                        newDoorOffset = findMaxWallOffset(newComps, 1, false); // +Y wall
                        newGridX = room.GridX;
                        newGridY = room.GridY + static_cast<int32>(sourceDoorOffset - newDoorOffset);
                    }

                    // FloorIndex = floor NUMBER (0=ground, 1=floor2, ...).
                    // Sniff-verified: the client's editor uses FloorIndex as a small
                    // integer for its floor selector UI; world Z is computed as
                    // FloorIndex × FLOOR_HEIGHT_Y (12 yards) during room spawn.
                    // Stairwell room sits on the CURRENT floor; a room attached via a
                    // stairwell's CEILING door (Z>1) goes one floor up.
                    if (std::abs(comp.OffsetPos[2]) > 1.0f)
                        newFloorIndex = room.FloorIndex + 1;
                    else
                        newFloorIndex = room.FloorIndex;

                    TC_LOG_DEBUG("housing", "ROOM_ADD: sourceDoor={:.1f} newDoor={:.1f} doorZ={:.1f} -> gridX={} gridY={} floorZ={}",
                        sourceDoorOffset, newDoorOffset, comp.OffsetPos[2], newGridX, newGridY, newFloorIndex);
                    goto foundDoor;
                }
            }
        }
        // Fallback: stack linearly if door not found
        newGridX = static_cast<int32>(nextSlot);
        newGridY = 0;
        foundDoor:;
    }

    ObjectGuid newRoomGuid;
    HousingResult result = housing->PlaceRoom(houseRoomID, nextSlot,
        /*orientation*/ 0, /*mirrored*/ false, &newRoomGuid, newGridX, newGridY, newFloorIndex);
    if (outRoomGuid)
        *outRoomGuid = newRoomGuid;

    if (onPlaced)
        onPlaced(result);

    if (result == HOUSING_RESULT_SUCCESS)
    {
        // As on retail, a stairwell is one physical unit split across TWO room entities
        // at the same XY — the stairwell room at current floor + an "Empty Stairwell
        // Room" (ID=48) 12 yards above. Each has its own geobox so decor can be
        // placed on BOTH floors independently, and the upper room's ceiling sits
        // at world Z=24 (2×floor height), matching sniff observations.
        HouseRoomData const* addedRoom = sHousingMgr.GetHouseRoomData(houseRoomID);
        if (addedRoom && addedRoom->HasStairs())
        {
            constexpr uint32 STAIRWELL_EMPTY_ROOM_ID = 48;
            ObjectGuid upperRoomGuid;
            HousingResult upperRes = housing->PlaceRoom(STAIRWELL_EMPTY_ROOM_ID, nextSlot + 1,
                /*orientation*/ 0, /*mirrored*/ false, &upperRoomGuid,
                newGridX, newGridY, newFloorIndex + 1);
            TC_LOG_DEBUG("housing", "ROOM_ADD: stairwell partner (ID={}) placed at (gridX={}, gridY={}, floor={}) result={}",
                STAIRWELL_EMPTY_ROOM_ID, newGridX, newGridY, newFloorIndex + 1, uint32(upperRes));
        }

        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(GetPlayer()->GetMap()))
        {
            int32 faction = (GetPlayer()->GetTeamId() == TEAM_ALLIANCE)
                ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;

            // Spawn only the NEW room's entities (incremental).
            // SpawnRoomMeshObjects skips rooms with existing MeshObjects/RoomEntities.
            interiorMap->SpawnRoomMeshObjects(housing, faction);

            // Update the source room's wall at the connecting door:
            // 1. Replace the Cosmetic wall MeshObject with DoorwayWall+Doorway pair
            // 2. Update the HousingRoomEntity's door AttachedRoomGUID
            if (!newRoomGuid.IsEmpty())
            {
                // Find the source room that owns the door component
                for (auto const& [guid, rm] : housing->GetRoomsMap())
                {
                    if (guid == newRoomGuid) continue;
                    HouseRoomData const* rd = sHousingMgr.GetHouseRoomData(rm.RoomEntryId);
                    if (!rd) continue;
                    std::vector<RoomComponentData> const* cs = sHousingMgr.GetRoomComponents(rd->RoomWmoDataID);
                    if (!cs) continue;
                    for (auto const& c : *cs)
                    {
                        if (c.ID == targetDoorComponentID)
                        {
                            // Replace wall with doorway — stairwell rooms connect
                            // HORIZONTALLY through walls, same as any other room.
                            interiorMap->ReplaceWallWithDoorway(guid, targetDoorComponentID,
                                faction, rm, newRoomGuid);
                            // Update door connection data
                            for (HousingRoomEntity* re : interiorMap->GetRoomEntities())
                            {
                                if (re && re->IsInWorld())
                                    re->UpdateDoorConnection(targetDoorComponentID, newRoomGuid);
                            }
                            goto doneUpdate;
                        }
                    }
                }
                doneUpdate:;
            }
        }

        WorldPackets::Housing::AccountRoomCollectionUpdate roomUpdate;
        roomUpdate.AddSingle(houseRoomID);
        SendPacket(roomUpdate.Write());
    }

    return result;
}

void WorldSession::HandleHousingRoomRemove(WorldPackets::Housing::HousingRoomRemove const& housingRoomRemove)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomRemoveResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomRemoveResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    // Collect info BEFORE RemoveRoom erases data
    std::vector<ObjectGuid> roomDecorGuids;
    for (auto const* decor : housing->GetAllPlacedDecor())
    {
        if (decor && decor->RoomGuid == housingRoomRemove.RoomGuid)
            roomDecorGuids.push_back(decor->Guid);
    }

    // Find adjacent rooms that had their connecting wall skipped because this room existed.
    // Those walls need to be restored after this room is removed.
    // Rule: parent rooms (lower slotIndex) skip their wall when a child (higher slot) exists.
    // So we look for neighbors with LOWER slotIndex — they need wall restoration.
    struct WallRestore { ObjectGuid roomGuid; uint32 doorCompID; };
    std::vector<WallRestore> wallsToRestore;
    {
        auto removedItr = housing->GetRoomsMap().find(housingRoomRemove.RoomGuid);
        if (removedItr != housing->GetRoomsMap().end())
        {
            Housing::Room const& removedRoom = removedItr->second;
            HouseRoomData const* removedRd = sHousingMgr.GetHouseRoomData(removedRoom.RoomEntryId);
            if (removedRd)
            {
                // For each neighbor with lower slot, find which of THEIR door components
                // was skipped because this room was connected
                for (auto const& [nGuid, nRoom] : housing->GetRoomsMap())
                {
                    if (nGuid == housingRoomRemove.RoomGuid)
                        continue;
                    if (nRoom.SlotIndex >= removedRoom.SlotIndex)
                        continue; // only restore walls on rooms with LOWER slot

                    HouseRoomData const* nRd = sHousingMgr.GetHouseRoomData(nRoom.RoomEntryId);
                    if (!nRd) continue;
                    std::vector<RoomComponentData> const* nComps = sHousingMgr.GetRoomComponents(nRd->RoomWmoDataID);
                    if (!nComps) continue;

                    for (auto const& nc : *nComps)
                    {
                        if (nc.ConnectionType == 0) continue;
                        // Check if this door faces the removed room's position
                        float doorWorldX = nRoom.GridX + nc.OffsetPos[0];
                        float doorWorldY = nRoom.GridY + nc.OffsetPos[1];
                        float dx = static_cast<float>(removedRoom.GridX) - doorWorldX;
                        float dy = static_cast<float>(removedRoom.GridY) - doorWorldY;
                        if (std::abs(dx) < 15.0f && std::abs(dy) < 15.0f)
                        {
                            wallsToRestore.push_back({ nGuid, nc.ID });
                            break;
                        }
                    }
                }
            }
        }
    }

    // Stairwells are stacked pairs — if removing a base stairwell, also remove
    // the partner room directly above (same XY, FloorIndex+1). Vice versa for
    // partner removal. Without this, whichever one stays behind is orphaned and
    // the graph-connectivity check blocks future removals.
    ObjectGuid pairedRoomGuid;
    {
        auto itr = housing->GetRoomsMap().find(housingRoomRemove.RoomGuid);
        if (itr != housing->GetRoomsMap().end())
        {
            Housing::Room const& rm = itr->second;
            HouseRoomData const* rd = sHousingMgr.GetHouseRoomData(rm.RoomEntryId);
            if (rd && rd->HasStairs())
            {
                for (auto const& [gGuid, gRm] : housing->GetRoomsMap())
                {
                    if (gGuid == housingRoomRemove.RoomGuid) continue;
                    if (gRm.GridX != rm.GridX || gRm.GridY != rm.GridY) continue;
                    if (std::abs(gRm.FloorIndex - rm.FloorIndex) != 1) continue;
                    HouseRoomData const* gRd = sHousingMgr.GetHouseRoomData(gRm.RoomEntryId);
                    if (gRd && gRd->HasStairs())
                    {
                        pairedRoomGuid = gGuid;
                        break;
                    }
                }
            }
        }
    }

    // Collect decor and despawn info for the paired room BEFORE removal
    std::vector<ObjectGuid> pairedDecorGuids;
    if (!pairedRoomGuid.IsEmpty())
    {
        for (auto const* decor : housing->GetAllPlacedDecor())
            if (decor && decor->RoomGuid == pairedRoomGuid)
                pairedDecorGuids.push_back(decor->Guid);
        // Remove the partner first so the main removal's graph-connectivity
        // check doesn't flag the leftover as unreachable.
        housing->RemoveRoom(pairedRoomGuid);
    }

    HousingResult result = housing->RemoveRoom(housingRoomRemove.RoomGuid);

    WorldPackets::Housing::HousingRoomRemoveResponse response;
    response.Result = static_cast<uint8>(result);
    response.RoomGuid = housingRoomRemove.RoomGuid;
    SendPacket(response.Write());

    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
        {
            // Despawn decor visuals
            for (ObjectGuid const& decorGuid : roomDecorGuids)
                interiorMap->DespawnDecorItem(decorGuid);
            for (ObjectGuid const& decorGuid : pairedDecorGuids)
                interiorMap->DespawnDecorItem(decorGuid);

            // Despawn room entities (including paired stairwell partner, if any)
            interiorMap->DespawnRoomEntities(housingRoomRemove.RoomGuid);
            if (!pairedRoomGuid.IsEmpty())
                interiorMap->DespawnRoomEntities(pairedRoomGuid);

            // Restore walls on adjacent rooms that were skipped
            int32 faction = (player->GetTeamId() == TEAM_ALLIANCE)
                ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;
            for (auto const& restore : wallsToRestore)
            {
                auto roomItr = housing->GetRoomsMap().find(restore.roomGuid);
                if (roomItr == housing->GetRoomsMap().end())
                    continue;
                // Respawn just the wall component that was skipped
                std::vector<uint32> compIDs = { restore.doorCompID };
                interiorMap->RespawnRoomComponentsForTheme(restore.roomGuid, faction,
                    roomItr->second, &compIDs, static_cast<int32>(roomItr->second.ThemeId));

                TC_LOG_DEBUG("housing", "ROOM_REMOVE: Restored wall compID={} on room {} (was skipped for removed room)",
                    restore.doorCompID, restore.roomGuid.ToString());
            }
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_REMOVE_ROOM RoomGuid: {}, Result: {}",
        housingRoomRemove.RoomGuid.ToString(), uint32(result));
}

void WorldSession::HandleHousingRoomRotate(WorldPackets::Housing::HousingRoomRotate const& housingRoomRotate)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    HousingResult result = housing->RotateRoom(housingRoomRotate.RoomGuid, housingRoomRotate.Clockwise);

    // Stairwell pair: if the rotated room is part of a stairwell stack, rotate
    // its partner too so both rooms stay aligned (same orientation, same XY).
    ObjectGuid pairedRoomGuid;
    if (result == HOUSING_RESULT_SUCCESS)
    {
        auto itr = housing->GetRoomsMap().find(housingRoomRotate.RoomGuid);
        if (itr != housing->GetRoomsMap().end())
        {
            Housing::Room const& rm = itr->second;
            HouseRoomData const* rd = sHousingMgr.GetHouseRoomData(rm.RoomEntryId);
            if (rd && rd->HasStairs())
            {
                for (auto const& [gGuid, gRm] : housing->GetRoomsMap())
                {
                    if (gGuid == housingRoomRotate.RoomGuid) continue;
                    if (gRm.GridX != rm.GridX || gRm.GridY != rm.GridY) continue;
                    if (std::abs(gRm.FloorIndex - rm.FloorIndex) != 1) continue;
                    HouseRoomData const* gRd = sHousingMgr.GetHouseRoomData(gRm.RoomEntryId);
                    if (gRd && gRd->HasStairs())
                    {
                        pairedRoomGuid = gGuid;
                        break;
                    }
                }
            }
        }
        if (!pairedRoomGuid.IsEmpty())
            housing->RotateRoom(pairedRoomGuid, housingRoomRotate.Clockwise);
    }

    WorldPackets::Housing::HousingRoomUpdateResponse response;
    response.Result = static_cast<uint8>(result);
    response.RoomGuid = housingRoomRotate.RoomGuid;
    SendPacket(response.Write());

    if (result == HOUSING_RESULT_SUCCESS)
    {
        // Sniff-verified (build 66838): retail UPDATE_OBJECT after rotation contains CREATE/UPDATE
        // blocks for ALL child mesh objects (walls, floor, ceiling), not just the parent room entity.
        // Despawn the rotated room's entities then re-spawn them with the new orientation.
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
        {
            interiorMap->DespawnRoomEntities(housingRoomRotate.RoomGuid);
            if (!pairedRoomGuid.IsEmpty())
                interiorMap->DespawnRoomEntities(pairedRoomGuid);

            int32 faction = (player->GetTeamId() == TEAM_ALLIANCE)
                ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;
            interiorMap->SpawnRoomMeshObjects(housing, faction);
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_ROTATE RoomGuid: {}, Clockwise: {}, Result: {}",
        housingRoomRotate.RoomGuid.ToString(), housingRoomRotate.Clockwise, uint32(result));
}

void WorldSession::HandleHousingRoomMoveRoom(WorldPackets::Housing::HousingRoomMoveRoom const& housingRoomMoveRoom)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomUpdateResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    HousingResult result = housing->MoveRoom(housingRoomMoveRoom.RoomGuid, housingRoomMoveRoom.TargetSlotIndex,
        housingRoomMoveRoom.TargetGuid, housingRoomMoveRoom.FloorIndex);

    WorldPackets::Housing::HousingRoomUpdateResponse response;
    response.Result = static_cast<uint8>(result);
    response.RoomGuid = housingRoomMoveRoom.RoomGuid;
    SendPacket(response.Write());

    // RefreshInteriorRoomVisuals crashes on same-GUID DESTROY+CREATE; the response
    // packet alone is enough for the client to update its layout, full visual refresh
    // happens on relog.
    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_MOVE RoomGuid: {}, TargetSlotIndex: {}, Result: {}",
        housingRoomMoveRoom.RoomGuid.ToString(), housingRoomMoveRoom.TargetSlotIndex, uint32(result));
}

void WorldSession::HandleHousingRoomSetComponentTheme(WorldPackets::Housing::HousingRoomSetComponentTheme const& housingRoomSetComponentTheme)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomSetComponentThemeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomSetComponentThemeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    HousingResult result = housing->ApplyRoomTheme(housingRoomSetComponentTheme.RoomGuid,
        housingRoomSetComponentTheme.HouseThemeID, housingRoomSetComponentTheme.OptionIDs);

    WorldPackets::Housing::HousingRoomSetComponentThemeResponse response;
    response.Result = static_cast<uint8>(result);
    response.RoomGuid = housingRoomSetComponentTheme.RoomGuid;
    response.ThemeSetID = housingRoomSetComponentTheme.HouseThemeID;
    response.OptionIDs = housingRoomSetComponentTheme.OptionIDs;
    SendPacket(response.Write());

    // Theme changes require different 3D models (FileDataIDs), so DESTROY old meshes
    // and CREATE new ones with new GUIDs. Sniff shows walls disappearing/reappearing.
    // Filter compIDs by type: only respawn components that match the dominant type in the
    // request. "Apply wall style to all" sends all compIDs including floor/ceiling, but
    // only wall components should change. We detect this by checking component types.
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
        {
            int32 faction = (player->GetTeamId() == TEAM_ALLIANCE)
                ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;
            auto const& rooms = housing->GetRoomsMap();
            auto roomItr = rooms.find(housingRoomSetComponentTheme.RoomGuid);
            if (roomItr != rooms.end())
            {
                // Filter: only respawn wall-type components from the list.
                // The client's "apply to all walls" sends ALL compIDs including
                // floor/ceiling, but those should keep their current theme.
                std::vector<uint32> wallOnlyCompIDs;
                for (uint32 cid : housingRoomSetComponentTheme.OptionIDs)
                {
                    RoomComponentEntry const* compEntry = sRoomComponentStore.LookupEntry(cid);
                    if (compEntry && (compEntry->Type == HOUSING_ROOM_COMPONENT_WALL
                        || compEntry->Type == HOUSING_ROOM_COMPONENT_DOORWAY_WALL
                        || compEntry->Type == HOUSING_ROOM_COMPONENT_DOORWAY))
                        wallOnlyCompIDs.push_back(cid);
                }

                interiorMap->RespawnRoomComponentsForTheme(housingRoomSetComponentTheme.RoomGuid, faction,
                    roomItr->second, wallOnlyCompIDs.empty() ? &housingRoomSetComponentTheme.OptionIDs : &wallOnlyCompIDs,
                    housingRoomSetComponentTheme.HouseThemeID);
            }
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_SET_COMPONENT_THEME RoomGuid: {}, HouseThemeID: {}, OptionCount: {}, Result: {}",
        housingRoomSetComponentTheme.RoomGuid.ToString(), housingRoomSetComponentTheme.HouseThemeID,
        housingRoomSetComponentTheme.OptionIDs.size(), uint32(result));
}

void WorldSession::HandleHousingRoomApplyComponentMaterials(WorldPackets::Housing::HousingRoomApplyComponentMaterials const& housingRoomApplyComponentMaterials)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomApplyComponentMaterialsResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomApplyComponentMaterialsResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    HousingResult result = housing->ApplyRoomMaterial(housingRoomApplyComponentMaterials.RoomGuid,
        housingRoomApplyComponentMaterials.RoomComponentTextureID,
        housingRoomApplyComponentMaterials.ColorOverride,
        housingRoomApplyComponentMaterials.OptionIDs);

    WorldPackets::Housing::HousingRoomApplyComponentMaterialsResponse response;
    response.Result = static_cast<uint8>(result);
    response.RoomGuid = housingRoomApplyComponentMaterials.RoomGuid;
    response.RoomComponentTextureID = housingRoomApplyComponentMaterials.RoomComponentTextureID;
    response.OptionIDs = housingRoomApplyComponentMaterials.OptionIDs;
    SendPacket(response.Write());

    // Material/texture changes: UPDATE_OBJECT with new textureID (no model change)
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
        {
            int32 textureID = static_cast<int32>(housingRoomApplyComponentMaterials.RoomComponentTextureID);
            auto const& rooms = housing->GetRoomsMap();
            auto roomItr = rooms.find(housingRoomApplyComponentMaterials.RoomGuid);
            if (roomItr != rooms.end())
                interiorMap->UpdateRoomComponentTextures(housingRoomApplyComponentMaterials.RoomGuid,
                    roomItr->second, &housingRoomApplyComponentMaterials.OptionIDs, textureID);
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_APPLY_COMPONENT_MATERIALS RoomGuid: {}, TextureID: {}, ColorOverride: {}, OptionCount: {}, Result: {}",
        housingRoomApplyComponentMaterials.RoomGuid.ToString(), housingRoomApplyComponentMaterials.RoomComponentTextureID,
        housingRoomApplyComponentMaterials.ColorOverride, housingRoomApplyComponentMaterials.OptionIDs.size(), uint32(result));
}

void WorldSession::HandleHousingRoomSetDoorType(WorldPackets::Housing::HousingRoomSetDoorType const& housingRoomSetDoorType)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomSetDoorTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomSetDoorTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    HousingResult result = housing->SetDoorType(housingRoomSetDoorType.RoomGuid,
        housingRoomSetDoorType.ThemeOptionID, housingRoomSetDoorType.DoorType);

    WorldPackets::Housing::HousingRoomSetDoorTypeResponse response;
    response.Result = static_cast<uint8>(result);
    response.RoomGuid = housingRoomSetDoorType.RoomGuid;
    response.ComponentID = housingRoomSetDoorType.ThemeOptionID;
    response.DoorType = housingRoomSetDoorType.DoorType;
    SendPacket(response.Write());

    // Door type selects between door model variants via RoomCompID.
    // Different FileDataIDs per variant, so respawn with correct model.
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
        {
            int32 faction = (player->GetTeamId() == TEAM_ALLIANCE)
                ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;
            auto const& rooms = housing->GetRoomsMap();
            auto roomItr = rooms.find(housingRoomSetDoorType.RoomGuid);
            if (roomItr != rooms.end())
            {
                std::vector<uint32> compIDs = { housingRoomSetDoorType.ThemeOptionID };
                interiorMap->RespawnRoomComponentsForTheme(housingRoomSetDoorType.RoomGuid, faction,
                    roomItr->second, &compIDs, static_cast<int32>(roomItr->second.ThemeId),
                    -1, housingRoomSetDoorType.DoorType);
            }
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_SET_DOOR_TYPE RoomGuid: {}, ThemeOptionID: {}, DoorType: {}, Result: {}",
        housingRoomSetDoorType.RoomGuid.ToString(), housingRoomSetDoorType.ThemeOptionID, housingRoomSetDoorType.DoorType, uint32(result));
}

void WorldSession::HandleHousingRoomSetCeilingType(WorldPackets::Housing::HousingRoomSetCeilingType const& housingRoomSetCeilingType)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        WorldPackets::Housing::HousingRoomSetCeilingTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Rooms and the house exterior may only be changed by an owner standing on the house's plot or inside it.
    if (!PlayerCanEditHousing(player, housing))
    {
        WorldPackets::Housing::HousingRoomSetCeilingTypeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        SendPacket(response.Write());
        return;
    }

    HousingResult result = housing->SetCeilingType(housingRoomSetCeilingType.RoomGuid,
        housingRoomSetCeilingType.ThemeOptionID, housingRoomSetCeilingType.CeilingType);

    WorldPackets::Housing::HousingRoomSetCeilingTypeResponse response;
    response.Result = static_cast<uint8>(result);
    response.RoomGuid = housingRoomSetCeilingType.RoomGuid;
    response.ComponentID = housingRoomSetCeilingType.ThemeOptionID;
    response.CeilingType = housingRoomSetCeilingType.CeilingType;
    SendPacket(response.Write());

    // Ceiling type selects between model variants (normal=RoomCompID 0, vaulted=RoomCompID 1).
    // Different FileDataIDs per variant, so we must respawn with the correct model.
    // overrideRoomCompID filters which option to spawn (only the one matching CeilingType).
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
        {
            int32 faction = (player->GetTeamId() == TEAM_ALLIANCE)
                ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;
            auto const& rooms = housing->GetRoomsMap();
            auto roomItr = rooms.find(housingRoomSetCeilingType.RoomGuid);
            if (roomItr != rooms.end())
            {
                std::vector<uint32> compIDs = { housingRoomSetCeilingType.ThemeOptionID };
                interiorMap->RespawnRoomComponentsForTheme(housingRoomSetCeilingType.RoomGuid, faction,
                    roomItr->second, &compIDs, static_cast<int32>(roomItr->second.ThemeId),
                    -1, housingRoomSetCeilingType.CeilingType);
            }
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_ROOM_SET_CEILING_TYPE RoomGuid: {}, ThemeOptionID: {}, CeilingType: {}, Result: {}",
        housingRoomSetCeilingType.RoomGuid.ToString(), housingRoomSetCeilingType.ThemeOptionID, housingRoomSetCeilingType.CeilingType, uint32(result));
}

// ============================================================
// Housing Services System
// ============================================================

void WorldSession::HandleHousingSvcsGuildCreateNeighborhood(WorldPackets::Housing::HousingSvcsGuildCreateNeighborhood const& housingSvcsGuildCreateNeighborhood)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_CREATE_GUILD_NEIGHBORHOOD))
    {
        WorldPackets::Housing::HousingSvcsCreateCharterNeighborhoodResponse response;
        response.TrailingResult = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    // Validate name (profanity/length check using charter name rules)
    if (!ObjectMgr::IsValidCharterName(housingSvcsGuildCreateNeighborhood.NeighborhoodName))
    {
        WorldPackets::Housing::HousingSvcsCreateCharterNeighborhoodResponse response;
        response.TrailingResult = static_cast<uint8>(HOUSING_RESULT_FILTER_REJECTED);
        SendPacket(response.Write());
        return;
    }

    // Only the guild master creates the guild's neighborhood (the wiki's Housing page: "The guild leader can create the
    // neighborhood"; Icy Veins: the guild master talks to the housing steward).
    Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId());
    auto refuse = [&](HousingResult result)
    {
        WorldPackets::Housing::HousingSvcsCreateCharterNeighborhoodResponse response;
        response.TrailingResult = static_cast<uint8>(result);
        SendPacket(response.Write());
    };

    if (!guild)
    {
        refuse(HOUSING_RESULT_INVALID_GUILD);
        return;
    }

    if (guild->GetLeaderGUID() != player->GetGUID())
    {
        refuse(HOUSING_RESULT_PERMISSION_DENIED);
        TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_GUILD_CREATE_NEIGHBORHOOD: {} is not the master of guild {}",
            player->GetGUID().ToString(), guild->GetId());
        return;
    }

    // At least 10 Battle.net accounts among the members, and 10 of them active.
    GuildAccountCounts const counts = CountGuildBattlenetAccounts(*guild);
    if (counts.Accounts < GUILD_NEIGHBORHOOD_MIN_ACCOUNTS)
    {
        refuse(HOUSING_RESULT_GUILD_MORE_ACCOUNTS_NEEDED);
        TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_GUILD_CREATE_NEIGHBORHOOD: guild {} has {} Battle.net accounts, {} needed",
            guild->GetId(), counts.Accounts, GUILD_NEIGHBORHOOD_MIN_ACCOUNTS);
        return;
    }

    if (counts.ActiveAccounts < GUILD_NEIGHBORHOOD_MIN_ACTIVE_ACCOUNTS)
    {
        refuse(HOUSING_RESULT_GUILD_MORE_ACTIVE_PLAYERS_NEEDED);
        TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_GUILD_CREATE_NEIGHBORHOOD: guild {} has {} active Battle.net accounts, {} needed",
            guild->GetId(), counts.ActiveAccounts, GUILD_NEIGHBORHOOD_MIN_ACTIVE_ACCOUNTS);
        return;
    }

    // The client also refuses a guild over a size limit ("You exceed the maximum guild member limit. Please create a
    // Private Neighborhood.", GlobalStrings HOUSING_CREATENEIGHBORHOOD_ERROR_OVERSIZED_GUILD); no source gives the
    // number, so no limit is applied here.

    // Per binary RE (see HousingPackets.h), the second numeric field on the wire is
    // SecondaryID (likely HouseStyle/Theme ID) and not a faction ID. Derive the
    // faction restriction from the player's team instead.
    Neighborhood* neighborhood = sNeighborhoodMgr.CreateGuildNeighborhood(
        player->GetGUID(), housingSvcsGuildCreateNeighborhood.NeighborhoodName,
        housingSvcsGuildCreateNeighborhood.NeighborhoodTypeID,
        player->GetTeam(),
        player->GetGuildId()); // saved, so the neighborhood stays linked to the guild

    WorldPackets::Housing::HousingSvcsCreateCharterNeighborhoodResponse response;
    response.TrailingResult = static_cast<uint8>(neighborhood ? HOUSING_RESULT_SUCCESS : HOUSING_RESULT_GENERIC_FAILURE);
    if (neighborhood)
    {
        response.Neighborhood.NeighborhoodGUID = neighborhood->GetGuid();
        response.Neighborhood.OwnerGUID = player->GetGUID();
        response.Neighborhood.Name = housingSvcsGuildCreateNeighborhood.NeighborhoodName;
    }
    SendPacket(response.Write());

    // Send guild notification to all guild members
    if (neighborhood)
    {
        if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
        {
            WorldPackets::Housing::HousingSvcsGuildCreateNeighborhoodNotification notification;
            notification.NeighborhoodGuid = neighborhood->GetGuid();
            notification.Name = housingSvcsGuildCreateNeighborhood.NeighborhoodName;
            guild->BroadcastPacket(notification.Write());
        }
    }

    TC_LOG_INFO("housing", "CMSG_HOUSING_SVCS_GUILD_CREATE_NEIGHBORHOOD Name: {}, Result: {}",
        housingSvcsGuildCreateNeighborhood.NeighborhoodName, neighborhood ? "success" : "failed");
}

void WorldSession::HandleHousingSvcsNeighborhoodReservePlot(WorldPackets::Housing::HousingSvcsNeighborhoodReservePlot const& housingSvcsNeighborhoodReservePlot)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Reservation = a 5-minute hold on a plot. Per retail behavior:
    //   - any player can reserve a plot (even if they already own a house elsewhere)
    //   - reservation just blocks OTHER players from buying/reserving for 5 minutes
    //   - the actual purchase/move is a separate action via the cornerstone UI
    //     (CMSG_NEIGHBORHOOD_BUY_HOUSE / CMSG_NEIGHBORHOOD_MOVE_HOUSE)
    //
    // An earlier implementation bought the plot here, which permanently assigned
    // the plot AND created a Housing object — the wrong semantics for a reservation.
    // The whole buy-side flow (the house, the plot spawn, the guild notification,
    // the House Purchase Cover Spell and the replies) belongs in
    // HandleNeighborhoodBuyHouse, not here.

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_BUY_HOUSE))
    {
        WorldPackets::Housing::HousingSvcsNeighborhoodReservePlotResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    uint32 housingWarnings = ShouldShowHousingWarning(player);
    if (housingWarnings != HOUSING_WARNING_NONE)
    {
        HousingResult failReason = HOUSING_RESULT_GENERIC_FAILURE;
        if (housingWarnings & HOUSING_WARNING_EXPANSION_REQUIRED)
            failReason = HOUSING_RESULT_MISSING_EXPANSION_ACCESS;

        WorldPackets::Housing::HousingSvcsNeighborhoodReservePlotResponse response;
        response.Result = static_cast<uint8>(failReason);
        SendPacket(response.Write());
        return;
    }

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(housingSvcsNeighborhoodReservePlot.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Housing::HousingSvcsNeighborhoodReservePlotResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    uint8 plotIndex = housingSvcsNeighborhoodReservePlot.PlotIndex;

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_NEIGHBORHOOD_RESERVE_PLOT PlotIndex={} NeighborhoodGuid={}",
        plotIndex, housingSvcsNeighborhoodReservePlot.NeighborhoodGuid.ToString());

    // ReservePlot returns false when the plot is already permanently occupied
    // OR currently reserved by someone else. Map both to clear error codes.
    HousingResult result;
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
        result = HOUSING_RESULT_PLOT_NOT_FOUND;
    else if (neighborhood->GetPlots()[plotIndex].IsOccupied())
        result = HOUSING_RESULT_PLOT_NOT_VACANT;
    else if (!neighborhood->ReservePlot(player->GetGUID(), plotIndex))
        result = HOUSING_RESULT_PLOT_RESERVATION_COOLDOWN;
    else
        result = HOUSING_RESULT_SUCCESS;

    WorldPackets::Housing::HousingSvcsNeighborhoodReservePlotResponse response;
    response.Result = static_cast<uint8>(result);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_NEIGHBORHOOD_RESERVE_PLOT PlotIndex: {}, Result: {}",
        plotIndex, uint32(result));
}

void WorldSession::HandleHousingSvcsRelinquishHouse(WorldPackets::Housing::HousingSvcsRelinquishHouse const& housingSvcsRelinquishHouse)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_DELETE_HOUSE))
    {
        WorldPackets::Housing::HousingSvcsRelinquishHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    // The house the packet names (Blizzard's 12.1 RelinquishHouse takes one house GUID), when the character's
    // Battle.net account owns it and it still stands on a plot.
    Housing* housing = player->GetHousingByGuid(housingSvcsRelinquishHouse.HouseGuid);
    if (!housing || housing->IsPacked())
    {
        WorldPackets::Housing::HousingSvcsRelinquishHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Relinquishing packs the house rather than deleting it: "Your layout will be saved, and you can purchase a new
    // House to automatically import it" (GlobalStrings HOUSING_HOUSE_SETTINGS_ABANDON_DESCRIPTION). Rooms, decor,
    // fixtures, level and favor are kept. As the owner chose, the plot is free at once, with no time to move back in
    // and no cooldown on buying again, since no source gives either length. What was paid for the house is paid
    // back, which the 12.1 relinquish dialog shows (C_Housing.GetCurrentHouseRefundAmount); a free first house pays
    // back nothing.
    ObjectGuid const houseGuid = housing->GetHouseGuid();
    ObjectGuid const cosmeticOwnerGuid = housing->GetCosmeticOwnerGuid();
    uint8 const plotIndex = housing->GetPlotIndex();
    uint64 const refund = housing->GetRefundAmount();
    Neighborhood* neighborhood = sNeighborhoodMgr.GetNeighborhood(housing->GetNeighborhoodGuid());

    // A refund that would take her over the gold limit is refused before anything changes (ModifyMoney sends the
    // too-much-gold error).
    if (refund && !player->ModifyMoney(static_cast<int64>(refund)))
    {
        WorldPackets::Housing::HousingSvcsRelinquishHouseResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_GENERIC_FAILURE);
        response.HouseGuid = houseGuid;
        SendPacket(response.Write());
        return;
    }

    HousingMap::DespawnHouseFromPlot(neighborhood, plotIndex, houseGuid);

    // The plot, the packed house and the refund as one unit.
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (neighborhood)
            neighborhood->ReleasePlotByHouse(houseGuid, trans);
        player->PackHousing(houseGuid, trans);
        player->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
    }

    if (neighborhood)
        neighborhood->RefreshMirrorDataForOnlineMembers();

    // With the house packed, her active endeavor moves to the account's other house, or to none.
    player->UpdateInitiativeComponent();

    SendGuildRemoveHouseNotification(player, houseGuid, cosmeticOwnerGuid);

    WorldPackets::Housing::HousingSvcsRelinquishHouseResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.HouseGuid = houseGuid;
    SendPacket(response.Write());

    // Request client to reload housing data
    WorldPackets::Housing::HousingSvcRequestPlayerReloadData reloadData;
    SendPacket(reloadData.Write());

    TC_LOG_INFO("housing", "CMSG_HOUSING_SVCS_RELINQUISH_HOUSE: Player {} relinquished house {}; it is packed, plot {} is free, {} copper paid back",
        player->GetGUID().ToString(), houseGuid.ToString(), plotIndex, refund);
}

void WorldSession::HandleHousingSvcsUpdateHouseSettings(WorldPackets::Housing::HousingSvcsUpdateHouseSettings const& housingSvcsUpdateHouseSettings)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // The house the packet names; with two houses the settings of either may be saved (only the account's own).
    Housing* housing = player->GetHousingByGuid(housingSvcsUpdateHouseSettings.HouseGuid);
    if (!housing)
    {
        WorldPackets::Housing::HousingSvcsUpdateHouseSettingsResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        response.House.HouseGUID = housingSvcsUpdateHouseSettings.HouseGuid;
        SendPacket(response.Write());
        return;
    }

    // A new cosmetic owner must be one of the characters the potential-owners reply listed for this house without
    // an error: a character of this Battle.net account that passes the faction and guild checks. The client only
    // offers those (Blizzard_HousingHouseSettings.lua disables the other entries).
    if (housingSvcsUpdateHouseSettings.NewOwnerGuid && !housingSvcsUpdateHouseSettings.NewOwnerGuid->IsEmpty()
        && *housingSvcsUpdateHouseSettings.NewOwnerGuid != housing->GetCosmeticOwnerGuid())
    {
        ObjectGuid newOwnerGuid = *housingSvcsUpdateHouseSettings.NewOwnerGuid;
        auto listed = _housingPotentialOwnersHouse == housing->GetHouseGuid() ? _housingPotentialOwners.find(newOwnerGuid) : _housingPotentialOwners.end();
        CharacterCacheEntry const* newOwner = sCharacterCache->GetCharacterCacheByGuid(newOwnerGuid);
        if (listed == _housingPotentialOwners.end() || listed->second != HOUSE_OWNER_ERROR_NONE || !newOwner || newOwner->IsDeleted)
        {
            WorldPackets::Housing::HousingSvcsUpdateHouseSettingsResponse response;
            response.Result = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
            housing->FillHouseEntry(response.House);
            SendPacket(response.Write());

            TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_UPDATE_HOUSE_SETTINGS: Player {} may not make {} the owner of house {}",
                player->GetGUID().ToString(), newOwnerGuid.ToString(), housing->GetHouseGuid().ToString());
            return;
        }

        housing->SetCosmeticOwnerGuid(newOwnerGuid);
        // The account's other online characters hold the same house and show its owner too.
        player->SyncAccountHouseOnOtherCharacters(housing->GetHouseGuid());
        if (Neighborhood const* neighborhood = sNeighborhoodMgr.GetNeighborhood(housing->GetNeighborhoodGuid()))
            neighborhood->RefreshMirrorDataForOnlineMembers();
    }

    bool settingsChanged = false;
    if (housingSvcsUpdateHouseSettings.PlotSettingsID)
    {
        uint32 newFlags = *housingSvcsUpdateHouseSettings.PlotSettingsID & HOUSE_SETTING_VALID_MASK;
        settingsChanged = newFlags != housing->GetSettingsFlags();
        housing->SaveSettings(newFlags);
    }

    // Visitors the new settings no longer let in are removed.
    if (settingsChanged)
        RemoveVisitorsWithoutAccess(*housing);

    WorldPackets::Housing::HousingSvcsUpdateHouseSettingsResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    housing->FillHouseEntry(response.House);
    SendPacket(response.Write());

    // Settings changes (visibility, permissions) require house finder data refresh
    WorldPackets::Housing::HousingSvcsHouseFinderForceRefresh forceRefresh;
    SendPacket(forceRefresh.Write());

    // Other players on the map are not sent this house's current house info: retail sends that reply only to a
    // client that asked for it (every capture has as many replies as CMSG_HOUSING_GET_CURRENT_HOUSE_INFO requests).

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_UPDATE_HOUSE_SETTINGS HouseGuid: {} NewFlags: 0x{:03X} Owner: {}",
        housingSvcsUpdateHouseSettings.HouseGuid.ToString(), housing->GetSettingsFlags(), housing->GetCosmeticOwnerGuid().ToString());
}

void WorldSession::HandleHousingSvcsPlayerViewHousesByPlayer(WorldPackets::Housing::HousingSvcsPlayerViewHousesByPlayer const& housingSvcsPlayerViewHousesByPlayer)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Look up neighborhoods the target player belongs to and return their houses
    std::vector<Neighborhood*> neighborhoods = sNeighborhoodMgr.GetNeighborhoodsForPlayer(housingSvcsPlayerViewHousesByPlayer.PlayerGuid);

    WorldPackets::Housing::HousingSvcsPlayerViewHousesResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    for (Neighborhood const* neighborhood : neighborhoods)
    {
        for (auto const& plot : neighborhood->GetPlots())
        {
            if (!plot.IsOccupied() || plot.OwnerGuid != housingSvcsPlayerViewHousesByPlayer.PlayerGuid)
                continue;
            WorldPackets::Housing::JamCliHouse& house = response.Houses.emplace_back();
            neighborhood->FillPlotHouseEntry(plot, house);
        }
    }
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_PLAYER_VIEW_HOUSES_BY_PLAYER PlayerGuid: {}, FoundHouses: {}",
        housingSvcsPlayerViewHousesByPlayer.PlayerGuid.ToString(), uint32(response.Houses.size()));
}

void WorldSession::HandleHousingSvcsPlayerViewHousesByBnetAccount(WorldPackets::Housing::HousingSvcsPlayerViewHousesByBnetAccount const& housingSvcsPlayerViewHousesByBnetAccount)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Find all neighborhoods where the queried BNet account has a plot (owns a house)
    std::vector<Neighborhood*> neighborhoods = sNeighborhoodMgr.GetNeighborhoodsByBnetAccount(housingSvcsPlayerViewHousesByBnetAccount.BnetAccountGuid);

    WorldPackets::Housing::HousingSvcsPlayerViewHousesResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    for (Neighborhood const* neighborhood : neighborhoods)
    {
        for (auto const& plot : neighborhood->GetPlots())
        {
            // Only the queried account's own houses. Without the second condition this would return every occupied
            // plot in every neighborhood that account lives in: the full roster of its neighbours, house GUID and
            // owner GUID included.
            if (!plot.IsOccupied() || plot.OwnerBnetGuid != housingSvcsPlayerViewHousesByBnetAccount.BnetAccountGuid)
                continue;
            WorldPackets::Housing::JamCliHouse& house = response.Houses.emplace_back();
            neighborhood->FillPlotHouseEntry(plot, house);
        }
    }
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_PLAYER_VIEW_HOUSES_BY_BNET_ACCOUNT BnetAccountGuid: {}, FoundHouses: {}",
        housingSvcsPlayerViewHousesByBnetAccount.BnetAccountGuid.ToString(), uint32(response.Houses.size()));
}

void WorldSession::HandleHousingSvcsGetPlayerHousesInfo(WorldPackets::Housing::HousingSvcsGetPlayerHousesInfo const& /*housingSvcsGetPlayerHousesInfo*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_SVCS_GET_PLAYER_HOUSES_INFO received (Player: {})", player->GetGUID().ToString());

    // Every house of the Battle.net account, each with its cosmetic owner, whichever character asks (hbst1 291702).
    WorldPackets::Housing::HousingSvcsGetPlayerHousesInfoResponse response;
    for (Housing const* housing : player->GetAllHousings())
        housing->FillHouseEntry(response.Houses.emplace_back());
    for (WorldPackets::Housing::JamCliHouse const& h : response.Houses)
        TC_LOG_DEBUG("housing", "  house {} cosmetic owner {} neighborhood {} plot {} settings 0x{:X}",
            h.HouseGUID.ToString(), h.CosmeticOwnerGUID.ToString(), h.NeighborhoodGUID.ToString(), h.PlotID, h.HouseSettingFlags);

    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "SMSG_HOUSING_SVCS_GET_PLAYER_HOUSES_INFO_RESPONSE sent (HouseCount: {})",
        uint32(response.Houses.size()));
}

void WorldSession::HandleHousingSvcsTeleportToPlot(WorldPackets::Housing::HousingSvcsTeleportToPlot const& housingSvcsTeleportToPlot)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Leaving a house interior this way is handled when she leaves the interior map (HouseInteriorMap::
    // RemovePlayerFromMap), once the cast has finished, not here: the cast can still be interrupted.

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(housingSvcsTeleportToPlot.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure response;
        response.FailureType = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Access check: verify the player has permission to visit this neighborhood.
    // Members and characters whose Battle.net account has a house here are always allowed;
    // anyone else needs a public neighborhood and the house settings of the plot.
    bool livesHere = neighborhood->IsMember(player->GetGUID()) || player->GetHousingForNeighborhood(neighborhood->GetGuid());
    if (!livesHere)
    {
        // Non-member: check if the neighborhood is public
        if (!neighborhood->IsPublic())
        {
            WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure response;
            response.FailureType = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
            SendPacket(response.Write());
            TC_LOG_DEBUG("housing", "HandleHousingSvcsTeleportToPlot: Player {} denied access to private neighborhood {}",
                player->GetGUID().ToString(), neighborhood->GetGuid().ToString());
            return;
        }
    }

    // The client sends NeighborhoodPlot.PlotIndex (0-54 on each map).
    uint32 plotIndex = housingSvcsTeleportToPlot.PlotIndex;

    TC_LOG_DEBUG("housing", "HandleHousingSvcsTeleportToPlot: PlotIndex={} HouseGuid={} TeleportType={} NeighborhoodGUID={}",
        plotIndex, housingSvcsTeleportToPlot.HouseGuid.ToString(), housingSvcsTeleportToPlot.TeleportType,
        housingSvcsTeleportToPlot.NeighborhoodGuid.ToString());

    WorldLocation arrival;
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS || !sHousingMgr.GetPlotArrival(neighborhood->GetNeighborhoodMapID(), uint8(plotIndex), arrival))
    {
        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure response;
        response.FailureType = static_cast<uint8>(HOUSING_RESULT_PLOT_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    // Per-house access check: verify visitor has permission to access this plot.
    // Owner can be offline — fall back to the persisted plotInfo->HouseSettingsFlags
    // (mirrored from character_housing.settingsFlags at neighborhood preload).
    if (!livesHere)
    {
        Neighborhood::HouseEntry entry = neighborhood->CheckHouseEntry(player, static_cast<uint8>(plotIndex), false);
        if (!entry.HouseGuid.IsEmpty() && !entry.Allowed)
        {
            WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure denied;
            denied.FailureType = static_cast<uint8>(HOUSING_RESULT_PERMISSION_DENIED);
            SendPacket(denied.Write());
            TC_LOG_DEBUG("housing", "HandleHousingSvcsTeleportToPlot: Player {} denied access to plot {} (settingsFlags=0x{:X})",
                player->GetGUID().ToString(), plotIndex, entry.SettingsFlags);
            return;
        }
    }

    // Retail casts Teleport Home on her, 10 seconds, with the plot's arrival point and the neighborhood as its target
    // (hbcd3 2044258). The spell's teleport effect has no destination of its own.
    SpellCastTargets targets;
    targets.SetServerChosenDst(arrival);
    targets.SetHousingTarget(neighborhood->GetGuid());
    SpellCastResult result = player->CastSpell(CastSpellTargetArg(std::move(targets)), SPELL_HOUSING_TELEPORT_HOME);

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_TELEPORT_TO_PLOT: {} casts {} towards plot {} on map {} ({:.4f}, {:.4f}, {:.4f}, facing {:.7f}): result {}",
        player->GetGUID().ToString(), SPELL_HOUSING_TELEPORT_HOME, plotIndex, arrival.GetMapId(), arrival.GetPositionX(),
        arrival.GetPositionY(), arrival.GetPositionZ(), arrival.GetOrientation(), uint32(result));
}

void WorldSession::HandleHousingSvcsStartTutorial(WorldPackets::Housing::HousingSvcsStartTutorial const& /*housingSvcsStartTutorial*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Housing warning gate — check expansion access, level requirements
    uint32 housingWarnings = ShouldShowHousingWarning(player);
    if (housingWarnings != HOUSING_WARNING_NONE)
    {
        HousingResult failReason = HOUSING_RESULT_GENERIC_FAILURE;
        if (housingWarnings & HOUSING_WARNING_EXPANSION_REQUIRED)
            failReason = HOUSING_RESULT_MISSING_EXPANSION_ACCESS;

        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure failResponse;
        failResponse.FailureType = static_cast<uint8>(failReason);
        SendPacket(failResponse.Write());

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_START_TUTORIAL: Player {} blocked by housing warning (flags=0x{:X})",
            player->GetGUID().ToString(), housingWarnings);
        return;
    }

    if (!sWorld->getBoolConfig(CONFIG_HOUSING_TUTORIALS_ENABLED))
    {
        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure failResponse;
        failResponse.FailureType = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(failResponse.Write());
        return;
    }

    // The district needs a public neighborhood for her to arrive in. This does not make her a member; buying a plot
    // does. It is made sure of here, because this handler runs on the world thread and finding one can create one;
    // the Warband query below answers on whichever thread updates her session, which can be a map's. Which
    // neighborhood she lands in is picked when she arrives (MapManager::CreateMap), not here.
    Neighborhood* neighborhood = sNeighborhoodMgr.FindOrCreatePublicNeighborhood(player->GetTeam());
    if (!neighborhood)
    {
        TC_LOG_ERROR("housing", "CMSG_HOUSING_SVCS_START_TUTORIAL: Failed to find/create tutorial neighborhood for player {}",
            player->GetGUID().ToString());

        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure failResponse;
        failResponse.FailureType = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(failResponse.Write());
        return;
    }

    ObjectGuid neighborhoodGuid = neighborhood->GetGuid();
    std::string neighborhoodName = neighborhood->GetName();

    // Blizzard's dashboard offers Start Tutorial only while "My First Home" is not completed anywhere on the
    // account (Blizzard_HousingTutorialsUtil.lua, line 54), so a request after that is refused. Her own turn-in may
    // not be saved yet, so it is checked here first.
    if (player->GetQuestRewardStatus(QUEST_HOUSING_MY_FIRST_HOME))
    {
        StartHousingTutorial(true, neighborhoodGuid, neighborhoodName);
        return;
    }

    QueryWarbandQuestRewarded(QUEST_HOUSING_MY_FIRST_HOME, [this, neighborhoodGuid, neighborhoodName](bool rewarded)
    {
        StartHousingTutorial(rewarded, neighborhoodGuid, neighborhoodName);
    });
}

void WorldSession::StartHousingTutorial(bool warbandCompletedMyFirstHome, ObjectGuid const& neighborhoodGuid, std::string const& neighborhoodName)
{
    Player* player = GetPlayer();
    if (!player || !player->IsInWorld())
        return;

    if (warbandCompletedMyFirstHome)
    {
        WorldPackets::Housing::HousingSvcsNotifyPermissionsFailure failResponse;
        failResponse.FailureType = static_cast<uint8>(HOUSING_RESULT_GENERIC_FAILURE);
        SendPacket(failResponse.Write());

        TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_START_TUTORIAL: refused for {}, the Warband has completed quest {}",
            player->GetGUID().ToString(), QUEST_HOUSING_MY_FIRST_HOME);
        return;
    }

    // Retail sent nothing between the request and the move to the district: the 10 second cast, then NEW_WORLD
    // (hbcd3 352402-356147). The cast takes her destination from the database; without the spell or that row there
    // is no retail way there, so she stays where she is.
    uint32 spellId = player->GetTeam() == HORDE ? SPELL_HOUSING_TELEPORT_TO_RAZORWIND_SHORES : SPELL_HOUSING_TELEPORT_TO_FOUNDERS_POINT;
    if (!sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
    {
        TC_LOG_ERROR("housing", "CMSG_HOUSING_SVCS_START_TUTORIAL: spell {} is missing, {} stays where she is",
            spellId, player->GetGUID().ToString());
        return;
    }

    if (!sSpellMgr->GetSpellTargetPosition(spellId, EFFECT_0))
    {
        TC_LOG_ERROR("housing", "CMSG_HOUSING_SVCS_START_TUTORIAL: spell {} has no spell_target_position row for effect 0, {} stays where she is",
            spellId, player->GetGUID().ToString());
        return;
    }

    SpellCastResult result = player->CastSpell(player, spellId);

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_START_TUTORIAL: {} ({}) casts {} towards her district, where public "
        "neighborhood '{}' ({}) is ready; the neighborhood she arrives in is picked on arrival: result {}",
        player->GetGUID().ToString(), player->GetTeam() == HORDE ? "Horde" : "Alliance", spellId, neighborhoodName,
        neighborhoodGuid.ToString(), uint32(result));
}

void WorldSession::QueryWarbandQuestRewarded(uint32 questId, std::function<void(bool)>&& callback)
{
    // A character's rewarded quests are in character_queststatus_rewarded, keyed by character; the Battle.net account
    // reaches its characters through its game accounts.
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT_IDS);
    stmt->setUInt32(0, GetBattlenetAccountId());
    std::shared_ptr<std::function<void(bool)>> done = std::make_shared<std::function<void(bool)>>(std::move(callback));
    GetQueryProcessor().AddCallback(LoginDatabase.AsyncQuery(stmt)
        .WithChainingPreparedCallback([questId, done](QueryCallback& chain, PreparedQueryResult gameAccounts)
        {
            std::string accountIds;
            if (gameAccounts)
            {
                do
                {
                    if (!accountIds.empty())
                        accountIds += ',';
                    accountIds += std::to_string(gameAccounts->Fetch()[0].GetUInt32());
                } while (gameAccounts->NextRow());
            }

            // No game account: ask for nothing that can match, so the answer is "not turned in".
            if (accountIds.empty())
                accountIds = "0";

            chain.SetNextQuery(CharacterDatabase.AsyncQuery(Trinity::StringFormat(
                "SELECT 1 FROM character_queststatus_rewarded r JOIN characters c ON c.guid = r.guid "
                "WHERE r.quest = {} AND r.active = 1 AND c.account IN ({}) LIMIT 1", questId, accountIds).c_str()));
        })
        .WithCallback([done](QueryResult rewarded)
        {
            (*done)(rewarded != nullptr);
        }));
}

void WorldSession::OfferHousingBreadcrumbQuest()
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // The same gates as Start Tutorial: no offer while housing tutorials are switched off, or while her account or
    // level keeps her out of housing.
    if (!sWorld->getBoolConfig(CONFIG_HOUSING_TUTORIALS_ENABLED) || ShouldShowHousingWarning(player) != HOUSING_WARNING_NONE)
        return;

    if (!sObjectMgr->GetQuestTemplate(QUEST_HOUSING_A_HOUSE_FOR_YOU))
        return;

    // Only a character who has neither quest yet; the Warband check below covers the other characters.
    if (player->GetQuestStatus(QUEST_HOUSING_A_HOUSE_FOR_YOU) != QUEST_STATUS_NONE
        || player->GetQuestStatus(QUEST_HOUSING_MY_FIRST_HOME) != QUEST_STATUS_NONE)
        return;

    // How retail hands out "A House For You" is not captured: the captured character already held "My First Home"
    // (hbcd3 187000). It is offered at login, the way a quest-start spell offers an auto-accept quest, while no
    // character of the Warband has turned in "My First Home". Entering either district completes it, and "My First
    // Home" follows through its RewardNextQuest.
    QueryWarbandQuestRewarded(QUEST_HOUSING_MY_FIRST_HOME, [this](bool rewarded)
    {
        Player* player = GetPlayer();
        if (rewarded || !player || !player->IsInWorld())
            return;

        Quest const* quest = sObjectMgr->GetQuestTemplate(QUEST_HOUSING_A_HOUSE_FOR_YOU);
        if (!quest || player->GetQuestStatus(QUEST_HOUSING_A_HOUSE_FOR_YOU) != QUEST_STATUS_NONE
            || player->GetQuestStatus(QUEST_HOUSING_MY_FIRST_HOME) != QUEST_STATUS_NONE || !player->CanTakeQuest(quest, false))
            return;

        if (quest->IsAutoAccept() && player->CanAddQuest(quest, false))
        {
            player->AddQuestAndCheckCompletion(quest, player);
            player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, player->GetGUID(), true, true);
        }
        else
            player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, player->GetGUID(), true, false);

        TC_LOG_DEBUG("housing", "Offered quest {} to {} at login", QUEST_HOUSING_A_HOUSE_FOR_YOU, player->GetGUID().ToString());
    });
}

// The client has no tutorial request other than Start Tutorial; the tutorial quest 91863 is completed through the
// normal quest reward path when the player finishes it.

void WorldSession::HandleHousingSvcsAcceptNeighborhoodOwnership(WorldPackets::Housing::HousingSvcsAcceptNeighborhoodOwnership const& housingSvcsAcceptNeighborhoodOwnership)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(housingSvcsAcceptNeighborhoodOwnership.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Housing::HousingSvcsAcceptNeighborhoodOwnershipResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    ObjectGuid previousOwnerGuid = neighborhood->GetOwnerGuid();
    HousingResult result = neighborhood->AcceptOwnershipTransfer(player->GetGUID());

    WorldPackets::Housing::HousingSvcsAcceptNeighborhoodOwnershipResponse response;
    response.Result = static_cast<uint8>(result);
    response.NeighborhoodGuid = housingSvcsAcceptNeighborhoodOwnership.NeighborhoodGuid;
    SendPacket(response.Write());

    if (result == HOUSING_RESULT_SUCCESS)
    {
        // Broadcast ownership transfer to all members
        WorldPackets::Housing::HousingSvcsNeighborhoodOwnershipTransferredResponse transferNotification;
        transferNotification.Result = static_cast<uint8>(result);
        transferNotification.OwnerGUID = player->GetGUID();
        transferNotification.HouseGUID = ObjectGuid::Empty;
        transferNotification.AccountGUID = GetAccountGUID();
        transferNotification.HouseLevel = 0;
        neighborhood->BroadcastPacket(transferNotification.Write(), player->GetGUID());

        // Both resident types changed.
        neighborhood->BroadcastMemberStatus(player->GetGUID());
        neighborhood->BroadcastMemberStatus(previousOwnerGuid);

        // Ownership change is a major data change — request client to reload housing data
        WorldPackets::Housing::HousingSvcRequestPlayerReloadData reloadData;
        SendPacket(reloadData.Write());

        // Previous owner also needs to reload
        if (Player* prevOwner = ObjectAccessor::FindPlayer(previousOwnerGuid))
        {
            WorldPackets::Housing::HousingSvcRequestPlayerReloadData prevReload;
            prevOwner->SendDirectMessage(prevReload.Write());
        }
    }

    TC_LOG_INFO("housing", "CMSG_HOUSING_SVCS_ACCEPT_NEIGHBORHOOD_OWNERSHIP: Result={} NeighborhoodGuid={} PreviousOwner={}",
        uint32(result), housingSvcsAcceptNeighborhoodOwnership.NeighborhoodGuid.ToString(),
        previousOwnerGuid.ToString());
}

void WorldSession::HandleHousingSvcsRejectNeighborhoodOwnership(WorldPackets::Housing::HousingSvcsRejectNeighborhoodOwnership const& housingSvcsRejectNeighborhoodOwnership)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(housingSvcsRejectNeighborhoodOwnership.NeighborhoodGuid, player);
    HousingResult result = HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND;
    if (neighborhood)
        result = neighborhood->RejectOwnershipTransfer(player->GetGUID());

    WorldPackets::Housing::HousingSvcsRejectNeighborhoodOwnershipResponse response;
    response.Result = static_cast<uint8>(result);
    response.NeighborhoodGuid = housingSvcsRejectNeighborhoodOwnership.NeighborhoodGuid;
    SendPacket(response.Write());

    // Notify the original owner that the transfer was rejected
    if (result == HOUSING_RESULT_SUCCESS && neighborhood)
    {
        if (Player* owner = ObjectAccessor::FindPlayer(neighborhood->GetOwnerGuid()))
        {
            WorldPackets::Housing::HousingSvcsRejectNeighborhoodOwnershipResponse ownerNotify;
            ownerNotify.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
            ownerNotify.NeighborhoodGuid = housingSvcsRejectNeighborhoodOwnership.NeighborhoodGuid;
            owner->SendDirectMessage(ownerNotify.Write());
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_REJECT_NEIGHBORHOOD_OWNERSHIP: Player {} rejected ownership of neighborhood {} (result={})",
        player->GetGUID().ToString(), housingSvcsRejectNeighborhoodOwnership.NeighborhoodGuid.ToString(), uint32(result));
}

void WorldSession::HandleHousingSvcsGetPotentialHouseOwners(WorldPackets::Housing::HousingSvcsGetPotentialHouseOwners const& housingSvcsGetPotentialHouseOwners)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = ResolveRequestedHousing(player, housingSvcsGetPotentialHouseOwners.HouseGuid);
    if (!housing)
    {
        WorldPackets::Housing::HousingSvcsGetPotentialHouseOwnersResponse response;
        SendPacket(response.Write()); // empty array — no Result byte in wire format
        return;
    }

    // The candidates are every character of the requesting Battle.net account: hled1 699944 lists 24 characters,
    // the account's characters across its realms in hled1 3323-8698. On this single-realm server that is every
    // character of every game account under the Battle.net account.
    ObjectGuid houseGuid = housing->GetHouseGuid();
    int32 factionRestriction = NEIGHBORHOOD_FACTION_NONE;
    uint32 guildId = 0;
    if (Neighborhood const* neighborhood = sNeighborhoodMgr.GetNeighborhood(housing->GetNeighborhoodGuid()))
    {
        // Only the server's public neighborhoods keep to one faction; guild and charter ones take both.
        if (neighborhood->IsServerPublic())
            factionRestriction = neighborhood->GetFactionRestriction();
        guildId = neighborhood->GetGuildId();
    }

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT_IDS);
    stmt->setUInt32(0, GetBattlenetAccountId());
    GetQueryProcessor().AddCallback(LoginDatabase.AsyncQuery(stmt)
        .WithChainingPreparedCallback([this, houseGuid, factionRestriction, guildId](QueryCallback& callback, PreparedQueryResult gameAccounts)
        {
            if (!gameAccounts)
            {
                SendHousingPotentialHouseOwners(houseGuid, factionRestriction, guildId, nullptr);
                return;
            }

            std::string accountIds;
            do
            {
                if (!accountIds.empty())
                    accountIds += ',';
                accountIds += std::to_string(gameAccounts->Fetch()[0].GetUInt32());
            } while (gameAccounts->NextRow());

            callback.SetNextQuery(CharacterDatabase.AsyncQuery(Trinity::StringFormat(
                "SELECT c.guid, c.name, c.race, c.class, COALESCE(gm.guildid, 0) FROM characters c LEFT JOIN guild_member gm ON gm.guid = c.guid "
                "WHERE c.account IN ({}) AND c.deleteInfos_Name IS NULL ORDER BY c.guid", accountIds).c_str()));
        })
        .WithCallback([this, houseGuid, factionRestriction, guildId](QueryResult characters)
        {
            SendHousingPotentialHouseOwners(houseGuid, factionRestriction, guildId, std::move(characters));
        }));
}

void WorldSession::SendHousingPotentialHouseOwners(ObjectGuid houseGuid, int32 factionRestriction, uint32 guildId, QueryResult characters)
{
    // Sniff-verified format: PlayerName is "<CharacterName>-<RealmNormalizedName>"
    // (cross-realm display name format). Examples from retail sniff:
    //   "Anondk-AltarofStorms", "Dahuntermon-AltarofStorms", "Insanedk-Trollbane",
    //   "Pewpewer-Khadgar", "Insanee-Gul'dan".
    // Falls back to the plain character name if the realm record is unavailable.
    std::string realmSuffix;
    if (std::shared_ptr<Realm const> currentRealm = sRealmList->GetCurrentRealm())
        realmSuffix = "-" + currentRealm->NormalizedName;

    _housingPotentialOwnersHouse = houseGuid;
    _housingPotentialOwners.clear();

    WorldPackets::Housing::HousingSvcsGetPotentialHouseOwnersResponse response;
    if (characters)
    {
        do
        {
            Field* fields = characters->Fetch();

            //         0        1       2       3        4
            // SELECT guid, name, race, class, guildid
            WorldPackets::Housing::HousingSvcsGetPotentialHouseOwnersResponse::PotentialOwnerData ownerData;
            ownerData.PlayerGuid = ObjectGuid::Create<HighGuid::Player>(fields[0].GetUInt64());
            ownerData.CharacterName = fields[1].GetString() + realmSuffix;
            ownerData.ClassID = fields[3].GetUInt8();

            // The owner of a house in a faction's neighborhood must be of that faction, and the owner of a house in a
            // guild neighborhood must be in that guild (HouseOwnerError Faction and Guild in the 12.1 client).
            ownerData.Error = Housing::GetHouseOwnerError(factionRestriction, guildId, Player::TeamForRace(fields[2].GetUInt8()),
                fields[4].GetUInt64());

            _housingPotentialOwners[ownerData.PlayerGuid] = ownerData.Error;
            response.PotentialOwners.push_back(std::move(ownerData));
        } while (characters->NextRow());
    }

    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "SMSG_HOUSING_SVCS_GET_POTENTIAL_HOUSE_OWNERS_RESPONSE: {} characters for house {}",
        uint32(response.PotentialOwners.size()), houseGuid.ToString());
}

void WorldSession::HandleHousingSvcsGetHouseFinderInfo(WorldPackets::Housing::HousingSvcsGetHouseFinderInfo const& /*housingSvcsGetHouseFinderInfo*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Return list of public neighborhoods available through the finder, filtered by faction
    std::vector<Neighborhood*> publicNeighborhoods = sNeighborhoodMgr.GetPublicNeighborhoods();
    uint32 playerTeam = player->GetTeam();

    WorldPackets::Housing::HousingSvcsGetHouseFinderInfoResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.Entries.reserve(publicNeighborhoods.size());
    for (Neighborhood* neighborhood : publicNeighborhoods)
    {
        // Faction filter: the server's public neighborhoods show only to their own faction; a charter neighborhood
        // opened to the public takes both factions and shows to both.
        if (!Neighborhood::IsFactionAllowed(neighborhood->IsServerPublic(), neighborhood->GetFactionRestriction(), playerTeam))
            continue;

        // Ignore filter: skip neighborhoods the player hid via the house finder
        // (CMSG_HOUSING_SVCS_HOUSE_FINDER_IGNORE_NEIGHBORHOOD).
        if (sHousingMgr.IsNeighborhoodIgnored(player->GetGUID(), neighborhood->GetGuid()))
            continue;

        WorldPackets::Housing::JamCliHouseFinderNeighborhood entry;
        entry.NeighborhoodGUID = neighborhood->GetGuid();
        entry.OwnerGUID = neighborhood->GetOwnerGuid();
        entry.Name = neighborhood->GetName();
        // Field1 is the occupied-plot bitmask (1 << plotIndex). Proven against the retail capture: in all three
        // house-bearing records, set-bits(Field1) == sorted(the per-house uint8), so that uint8 is PlotIndex and
        // Field1 indexes the same plot space. Field2 is NOT a continuation of it - retail sends Field2=0 in 6 of 7
        // list entries and 0 in both detail responses (one entry carries 0x10000, i.e. "plot 80", which cannot
        // exist in a 55-plot neighborhood), so the old "client ORs Field1|Field2 at offset 520,
        // then checks (1LL << plotIndex) & bitmask to determine if plot is occupied on the finder map).
        // Include both permanently-occupied plots AND plots currently held by ANOTHER player's
        // 5-minute reservation, so the user can't keep clicking Reserve on the same plot
        // when someone else has already locked it. The viewer's own reservation stays
        // marked-available so they can still act on it via the cornerstone.
        uint64 occupiedBitmask = 0;
        for (auto const& plot : neighborhood->GetPlots())
        {
            if (plot.IsOccupied() && plot.PlotIndex < 64)
                occupiedBitmask |= (uint64(1) << plot.PlotIndex);
        }
        for (uint8 plotIdx = 0; plotIdx < 64; ++plotIdx)
        {
            if (occupiedBitmask & (uint64(1) << plotIdx))
                continue; // already counted as permanently occupied
            if (!neighborhood->GetPlotReserverOther(plotIdx, player->GetGUID()).IsEmpty())
                occupiedBitmask |= (uint64(1) << plotIdx);
        }
        entry.Field1 = occupiedBitmask;
        entry.Field2 = 0;
        entry.ExtraFlags = 0x20; // Retail sniff: finder list entries always have ExtraFlags=0x20

        // Retail LIST response has an EMPTY Houses array — the client only needs houses in
        // the DETAIL response (HandleHousingSvcsGetHouseFinderNeighborhood). Populating
        // Houses here causes the client to not render occupied plot markers on the finder map.

        response.Entries.push_back(std::move(entry));
    }

    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_GET_HOUSE_FINDER_INFO: player={} team={} total_public={} sent={}",
        player->GetName(), playerTeam, uint32(publicNeighborhoods.size()), uint32(response.Entries.size()));
    for (auto const& entry : response.Entries)
    {
        TC_LOG_DEBUG("housing", "  FINDER_LIST entry: nbGuid={} owner={} name='{}' occupiedBitmask=0x{:016X} "
            "houses={} extraFlags=0x{:02X}",
            entry.NeighborhoodGUID.ToString(), entry.OwnerGUID.ToString(), entry.Name,
            entry.Field1, uint32(entry.Houses.size()), entry.ExtraFlags);
    }
}

void WorldSession::HandleHousingSvcsGetHouseFinderNeighborhood(WorldPackets::Housing::HousingSvcsGetHouseFinderNeighborhood const& housingSvcsGetHouseFinderNeighborhood)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Neighborhood const* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(housingSvcsGetHouseFinderNeighborhood.NeighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Housing::HousingSvcsGetHouseFinderNeighborhoodResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_GET_HOUSE_FINDER_NEIGHBORHOOD: '{}' guid={} MapID:{} Members:{} Public:{} OccupiedPlots:{}",
        neighborhood->GetName(), neighborhood->GetGuid().ToString(),
        neighborhood->GetNeighborhoodMapID(),
        neighborhood->GetMemberCount(), neighborhood->IsPublic(),
        neighborhood->GetOccupiedPlotCount());

    // Dump all plot states for debugging
    for (uint8 i = 0; i < MAX_NEIGHBORHOOD_PLOTS; ++i)
    {
        auto const& plot = neighborhood->GetPlots()[i];
        if (plot.IsOccupied())
        {
            TC_LOG_DEBUG("housing", "  PLOT[{}]: occupied owner={} house={} bnet={} level={} favor={} name='{}'",
                i, plot.OwnerGuid.ToString(), plot.HouseGuid.ToString(), plot.OwnerBnetGuid.ToString(),
                plot.HouseLevel, plot.HouseFavor, plot.HouseName);
        }
    }

    // Build single JamCliHouseFinderNeighborhood with houses array for occupied plots
    WorldPackets::Housing::HousingSvcsGetHouseFinderNeighborhoodResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.Neighborhood.NeighborhoodGUID = neighborhood->GetGuid();
    response.Neighborhood.OwnerGUID = neighborhood->GetOwnerGuid();
    response.Neighborhood.Name = neighborhood->GetName();

    // Field1 | Field2 is a BITMASK of occupied plot indices (IDA: client ORs them at offset 520,
    // then checks (1LL << plotIndex) & bitmask to determine if plot is occupied on the finder map).
    uint64 occupiedBitmask = 0;
    for (auto const& plot : neighborhood->GetPlots())
    {
        if (plot.IsOccupied() && plot.PlotIndex < 64)
            occupiedBitmask |= (uint64(1) << plot.PlotIndex);
    }
    response.Neighborhood.Field1 = occupiedBitmask;
    response.Neighborhood.Field2 = 0;
    // ExtraFlags is Enum.HouseFinderSuggestionReason (None=0 .. Random=32, HomeOwner=64), i.e. list-context
    // metadata rather than a neighborhood property: the retail capture
    // dump_12.0.5.67186_2026-04-24_13-23-54 sends 0x40/0x20 in the LIST but 0x00 in BOTH detail responses -
    // including for the same neighborhood 0x6CCE, which is 0x40 in the list and 0x00 in the detail. The old
    // comment claiming "finder detail always has ExtraFlags=0x20" was the wrong way round.
    response.Neighborhood.ExtraFlags = 0x00;

    TC_LOG_DEBUG("housing", "  DETAIL: occupiedBitmask=0x{:016X} occupiedCount={}",
        occupiedBitmask, neighborhood->GetOccupiedPlotCount());

    for (auto const& plot : neighborhood->GetPlots())
    {
        if (!plot.IsOccupied() || plot.OwnerGuid.IsEmpty())
            continue;

        // The uint8 in each house entry is the plot, which is why every plot needs its own value there.
        WorldPackets::Housing::JamCliHouse& house = response.Neighborhood.Houses.emplace_back();
        neighborhood->FillPlotHouseEntry(plot, house);

        TC_LOG_DEBUG("housing", "  DETAIL_HOUSE: plotIndex={} houseGuid={} ownerGuid={}",
            plot.PlotIndex, plot.HouseGuid.ToString(), plot.OwnerGuid.ToString());
    }

    TC_LOG_DEBUG("housing", "  DETAIL: sending {} houses in Houses[] array", uint32(response.Neighborhood.Houses.size()));
    SendPacket(response.Write());

    // Populate the Housing/4 entity with this neighborhood's mirror data so the
    // client's internal house list stays in sync for plot resolution.
    HousingNeighborhoodMirrorEntity& mirrorEntity = GetHousingNeighborhoodMirrorEntity();
    mirrorEntity.SetName(neighborhood->GetName());
    mirrorEntity.SetOwnerGUID(neighborhood->GetOwnerGuid());

    mirrorEntity.ClearHouses();
    for (auto const& plot : neighborhood->GetPlots())
    {
        if (plot.IsOccupied() && !plot.HouseGuid.IsEmpty())
            mirrorEntity.AddHouse(plot.HouseGuid, plot.OwnerGuid);
        else
            mirrorEntity.AddHouse(ObjectGuid::Empty, ObjectGuid::Empty);
    }

    // Count what we're sending on the mirror entity
    uint32 mirrorOccupied = 0;
    uint32 mirrorEmpty = 0;
    for (auto const& plot : neighborhood->GetPlots())
    {
        if (plot.IsOccupied() && !plot.HouseGuid.IsEmpty())
            ++mirrorOccupied;
        else
            ++mirrorEmpty;
    }
    TC_LOG_DEBUG("housing", "  MIRROR: sending {} occupied + {} empty = {} total slots",
        mirrorOccupied, mirrorEmpty, mirrorOccupied + mirrorEmpty);

    mirrorEntity.ClearManagers();
    for (auto const& member : neighborhood->GetMembers())
    {
        if (member.Role == NEIGHBORHOOD_ROLE_MANAGER || member.Role == NEIGHBORHOOD_ROLE_OWNER)
        {
            ObjectGuid bnetGuid;
            if (Player* mgr = ObjectAccessor::FindPlayer(member.PlayerGuid))
                bnetGuid = mgr->GetSession()->GetBattlenetAccountGUID();
            mirrorEntity.AddManager(bnetGuid, member.PlayerGuid);
        }
    }
    // Wholesale re-push; retail uses CREATE for this (sniff-verified).
    mirrorEntity.SendCreateToPlayer(player);

    TC_LOG_DEBUG("housing", "  MIRROR: update sent to player {}", player->GetName());
}

void WorldSession::HandleHousingSvcsGetBnetFriendNeighborhoods(WorldPackets::Housing::HousingSvcsGetBnetFriendNeighborhoods const& housingSvcsGetBnetFriendNeighborhoods)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    PlayerSocial* social = player->GetSocial();
    if (!social)
    {
        WorldPackets::Housing::HousingSvcsGetBnetFriendNeighborhoodsResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
        SendPacket(response.Write());
        return;
    }

    // Build response using JamHousingSearchResult format (same as HouseFinderInfo).
    // Iterate all neighborhoods and check if any plot owner is on the player's friend list.
    WorldPackets::Housing::HousingSvcsGetBnetFriendNeighborhoodsResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);

    std::vector<Neighborhood*> allNeighborhoods = sNeighborhoodMgr.GetAllNeighborhoods();
    for (Neighborhood const* neighborhood : allNeighborhoods)
    {
        bool hasFriend = false;
        for (auto const& plot : neighborhood->GetPlots())
        {
            if (!plot.IsOccupied() || plot.OwnerGuid.IsEmpty())
                continue;

            if (!social->HasFriend(plot.OwnerGuid))
                continue;

            hasFriend = true;
            break;
        }

        if (!hasFriend)
            continue;

        WorldPackets::Housing::JamCliHouseFinderNeighborhood entry;
        entry.NeighborhoodGUID = neighborhood->GetGuid();
        entry.OwnerGUID = neighborhood->GetOwnerGuid();
        entry.Name = neighborhood->GetName();
        auto plotsForMap = sHousingMgr.GetPlotsForMap(neighborhood->GetNeighborhoodMapID());
        uint32 totalPlots = !plotsForMap.empty() ? static_cast<uint32>(plotsForMap.size()) : MAX_NEIGHBORHOOD_PLOTS;
        uint32 availPlots = totalPlots - neighborhood->GetOccupiedPlotCount();
        entry.SetPlotCounts(availPlots, totalPlots);
        entry.Field2 = static_cast<uint64>(neighborhood->GetNeighborhoodMapID());

        for (auto const& plot2 : neighborhood->GetPlots())
        {
            if (!plot2.IsOccupied() || plot2.OwnerGuid.IsEmpty())
                continue;
            neighborhood->FillPlotHouseEntry(plot2, entry.Houses.emplace_back());
        }
        response.Entries.push_back(std::move(entry));
    }

    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_GET_BNET_FRIEND_NEIGHBORHOODS BnetAccountGuid: {}, FriendNeighborhoods: {}",
        housingSvcsGetBnetFriendNeighborhoods.BnetAccountGuid.ToString(), uint32(response.Entries.size()));
}

void WorldSession::HandleHousingSvcsDeleteAllNeighborhoodInvites(WorldPackets::Housing::HousingSvcsDeleteAllNeighborhoodInvites const& /*housingSvcsDeleteAllNeighborhoodInvites*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Decline all pending neighborhood invitations through the house finder
    // This sets the auto-decline flag so no new invites are received
    player->SetPlayerFlagEx(PLAYER_FLAGS_EX_AUTO_DECLINE_NEIGHBORHOOD);

    WorldPackets::Housing::HousingSvcsDeleteAllNeighborhoodInvitesResponse response;
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_DELETE_ALL_NEIGHBORHOOD_INVITES: Player {} declined all invitations",
        player->GetGUID().ToString());
}

// ============================================================
// Housing Misc
// ============================================================

void WorldSession::HandleHousingHouseStatus(WorldPackets::Housing::HousingHouseStatus const& /*housingHouseStatus*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // CRITICAL TRACE: log that the client polled for housing status.
    // If this line never appears in the log after interior entry, the client's
    // housing system context was never activated (missing 0x56000E init packet).
    bool isInterior = player->GetMap() && dynamic_cast<HouseInteriorMap*>(player->GetMap());
    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_HOUSE_STATUS: CLIENT POLLED! player={} map={} isInterior={} pos=({:.1f},{:.1f},{:.1f})",
        player->GetGUID().ToString(), player->GetMapId(), isInterior,
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());

    WorldPackets::Housing::HousingHouseStatusResponse response;

    // The house the reply is about: the one whose interior she is in, else the plot she stands on, else her own.
    // Retail sent the cosmetic owner as the owner character on the plot (hbcd3 1340681) and left it empty inside the
    // interior (hbcd3 1416647). LockedDecorGuid stays empty: nothing here holds a decor locked for one player.
    ObjectGuid const bnetAccountGuid = GetBattlenetAccountGUID();
    Housing const* ownHousing = nullptr;
    int8 visitedPlot = -1;
    if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
    {
        response.HouseGuid = interiorMap->GetHouseGuid();
        ownHousing = player->GetHousingByGuid(interiorMap->GetHouseGuid());
        if (ownHousing)
            response.AccountGuid = bnetAccountGuid;
        else if (Housing const* hostHousing = interiorMap->GetOwnerHousing())
            response.AccountGuid = ObjectGuid::Create<HighGuid::BNetAccount>(hostHousing->GetOwnerAccountId());
    }
    else if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
    {
        visitedPlot = housingMap->GetPlayerCurrentPlot(player->GetGUID());
        Neighborhood* neighborhood = housingMap->GetNeighborhood();
        Neighborhood::PlotInfo const* plotInfo = (neighborhood && visitedPlot >= 0) ? neighborhood->GetPlotInfo(static_cast<uint8>(visitedPlot)) : nullptr;
        if (plotInfo)
        {
            response.HouseGuid = plotInfo->HouseGuid;
            response.AccountGuid = plotInfo->OwnerBnetGuid;
            response.OwnerPlayerGuid = plotInfo->OwnerGuid;
            ownHousing = player->GetHousingByGuid(plotInfo->HouseGuid);
        }
        else if ((ownHousing = player->GetHousing()))
        {
            response.HouseGuid = ownHousing->GetHouseGuid();
            response.AccountGuid = bnetAccountGuid;
            response.OwnerPlayerGuid = ownHousing->GetCosmeticOwnerGuid();
        }
    }
    else if ((ownHousing = player->GetHousing()))
    {
        response.HouseGuid = ownHousing->GetHouseGuid();
        response.AccountGuid = bnetAccountGuid;
        response.OwnerPlayerGuid = ownHousing->GetCosmeticOwnerGuid();
    }

    // Only her own house can be in an editor for her.
    FillHouseStatusEditModes(response, ownHousing);

    TC_LOG_DEBUG("housing", "<<< SMSG_HOUSING_HOUSE_STATUS_RESPONSE visitedPlot {} house {} account {} owner {} decor {} layout {} fixture {}",
        visitedPlot, response.HouseGuid.ToString(), response.AccountGuid.ToString(), response.OwnerPlayerGuid.ToString(),
        response.DecorEditModeEnabled, response.LayoutEditModeEnabled, response.FixtureEditModeEnabled);
    SendPacket(response.Write());
}

void WorldSession::HandleHousingGetPlayerPermissions(WorldPackets::Housing::HousingGetPlayerPermissions const& housingGetPlayerPermissions)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_GET_PLAYER_PERMISSIONS received (HouseGuid: {})",
        housingGetPlayerPermissions.HouseGuid.has_value() ? housingGetPlayerPermissions.HouseGuid->ToString() : "none");

    // The client names the house it asks about (hbcd3: HasHouseGUID True, the house GUID). Every character of the
    // house's Battle.net account is its owner.
    Housing* housing = ResolveRequestedHousing(player, housingGetPlayerPermissions.HouseGuid.value_or(ObjectGuid::Empty));
    Housing* standingHousing = housing ? housing : player->GetHousing();

    WorldPackets::Housing::HousingGetPlayerPermissionsResponse response;
    if (standingHousing)
    {
        response.HouseGuid = standingHousing->GetHouseGuid();
        bool isOwner = housing != nullptr;

        if (isOwner)
        {
            // House owner gets full permissions
            // Sniff-verified: owner permissions are 0xE0 (bits 5,6,7)
            response.ResultCode = 0;
            response.PermissionFlags = 0xE0;
        }
        else
        {
            // Visitor on another player's plot — check stored settings
            response.ResultCode = 0;
            response.PermissionFlags = 0x00;

            HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
            if (housingMap)
            {
                int8 visitedPlot = housingMap->GetPlayerCurrentPlot(player->GetGUID());
                if (visitedPlot >= 0)
                {
                    Neighborhood* neighborhood = housingMap->GetNeighborhood();
                    if (neighborhood)
                    {
                        Neighborhood::PlotInfo const* plotInfo = neighborhood->GetPlotInfo(static_cast<uint8>(visitedPlot));
                        if (plotInfo && plotInfo->IsOccupied())
                        {
                            // The same check as the door and the plot area trigger.
                            Neighborhood::HouseEntry entry = neighborhood->CheckHouseEntry(player, static_cast<uint8>(visitedPlot), false);
                            if (!entry.HouseGuid.IsEmpty())
                            {
                                response.HouseGuid = entry.HouseGuid;
                                response.PermissionFlags = entry.Allowed ? 0x40 : 0x00;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        response.ResultCode = 0;
        response.PermissionFlags = 0;
    }
    WorldPacket const* permPkt = response.Write();
    TC_LOG_DEBUG("network.opcode", "<<< SMSG_HOUSING_GET_PLAYER_PERMISSIONS_RESPONSE ({} bytes): {}",
        permPkt->size(), HexDumpPacket(permPkt));
    TC_LOG_DEBUG("housing", "    HouseGuid={} ResultCode={} PermissionFlags=0x{:02X}",
        response.HouseGuid.ToString(), response.ResultCode, response.PermissionFlags);
    SendPacket(permPkt);
}

void WorldSession::HandleHousingGetCurrentHouseInfo(WorldPackets::Housing::HousingGetCurrentHouseInfo const& /*housingGetCurrentHouseInfo*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", ">>> CMSG_HOUSING_GET_CURRENT_HOUSE_INFO received");

    HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
    bool isInterior = player->GetMap() && dynamic_cast<HouseInteriorMap*>(player->GetMap());
    int8 currentPlot = -1;
    if (housingMap)
        currentPlot = housingMap->GetPlayerCurrentPlot(player->GetGUID());
    else if (isInterior)
    {
        if (Housing* housing = player->GetHousing())
            currentPlot = static_cast<int8>(housing->GetPlotIndex());
    }

    WorldPackets::Housing::HousingGetCurrentHouseInfoResponse response;

    if (currentPlot >= 0 && housingMap && housingMap->GetNeighborhood())
    {
        // Player is on a specific plot — return info about THAT plot's house
        Neighborhood* neighborhood = housingMap->GetNeighborhood();
        Neighborhood::PlotInfo const* plotInfo = neighborhood->GetPlotInfo(static_cast<uint8>(currentPlot));

        if (plotInfo && plotInfo->IsOccupied())
        {
            // The house standing on this plot, with its cosmetic owner and settings (hbcd3 1340691).
            neighborhood->FillPlotHouseEntry(*plotInfo, response.House);
        }
        else
        {
            // On an unoccupied plot
            response.House.CosmeticOwnerGUID = player->GetGUID();
            response.House.NeighborhoodGUID = neighborhood->GetGuid();
            response.House.PlotID = static_cast<uint8>(currentPlot);
        }
    }
    else if (Housing* housing = player->GetHousing())
    {
        // Not on any tracked plot: the house the character is in, or the account's only house. Inside the interior
        // retail still sends the cosmetic owner, plot and settings (hbcd3 1424655).
        housing->FillHouseEntry(response.House);
    }
    else if (housingMap)
    {
        // No house, no tracked plot
        response.House.CosmeticOwnerGUID = player->GetGUID();
        if (Neighborhood* neighborhood = housingMap->GetNeighborhood())
            response.House.NeighborhoodGUID = neighborhood->GetGuid();
    }
    response.Result = 0;
    TC_LOG_DEBUG("housing", "<<< SMSG_HOUSING_GET_CURRENT_HOUSE_INFO_RESPONSE currentPlot={} HouseGuid={} CosmeticOwner={} NeighborhoodGuid={} Plot={} Settings=0x{:X}",
        currentPlot, response.House.HouseGUID.ToString(), response.House.CosmeticOwnerGUID.ToString(),
        response.House.NeighborhoodGUID.ToString(), response.House.PlotID, response.House.HouseSettingFlags);
    SendPacket(response.Write());
}

void WorldSession::HandleHousingResetKioskMode(WorldPackets::Housing::HousingResetKioskMode const& /*housingResetKioskMode*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // This destroys a house, so it answers to the same switch as CMSG_HOUSING_SVCS_RELINQUISH_HOUSE; otherwise a
    // realm with house deletion switched off would still lose houses through this opcode.
    if (!sWorld->getBoolConfig(CONFIG_HOUSING_ENABLE_DELETE_HOUSE))
    {
        WorldPackets::Housing::HousingResetKioskModeResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_SERVICE_NOT_AVAILABLE);
        SendPacket(response.Write());
        return;
    }

    // Full teardown: despawn the structure, free the plot, drop the plot holder from the roster, delete the rows.
    // This used to call DeleteHousing() alone, which left the ten MeshObjects and the door GO standing on a plot the
    // server then considered vacant and re-purchasable.
    // The kiosk request names no house: it acts on the house the character stands in or on.
    ObjectGuid destroyedHouseGuid = DestroyPlayerHousing(player, player->GetHousing());

    WorldPackets::Housing::HousingResetKioskModeResponse response;
    response.Result = static_cast<uint8>(destroyedHouseGuid.IsEmpty()
        ? HOUSING_RESULT_HOUSE_NOT_FOUND : HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    if (!destroyedHouseGuid.IsEmpty())
    {
        WorldPackets::Housing::HousingSvcRequestPlayerReloadData reloadData;
        SendPacket(reloadData.Write());
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_RESET_KIOSK_MODE processed for player {}",
        player->GetGUID().ToString());
}

// CMSG_HOUSING_RESET_HOUSE (0x370008) — wire: uint8 ResetScope (HousingHouseScope: 1=Interior, 2=Exterior).
// Wipes all placed decor for the given scope, returns each item to the player's decor storage,
// persists, despawns the visuals, and replies SMSG_HOUSING_RESET_HOUSE_RESPONSE { uint32 Result }
// which drives the client HOUSE_RESET_COMPLETED (Result==0) / HOUSE_RESET_FAILED events.
void WorldSession::HandleHousingResetHouse(WorldPackets::Housing::HousingResetHouse const& housingResetHouse)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    auto sendResult = [this](HousingResult r)
    {
        WorldPackets::Housing::HousingResetHouseResponse response;
        response.Result = static_cast<uint32>(r);
        SendPacket(response.Write());
    };

    Housing* housing = player->GetHousing();
    if (!housing)
    {
        sendResult(HOUSING_RESULT_HOUSE_NOT_FOUND);
        return;
    }

    // Only the house owner may reset it.
    if (!PlayerCanEditHousing(player, housing))
    {
        sendResult(HOUSING_RESULT_NOT_ON_OWNED_PLOT);
        return;
    }

    uint8 scope = housingResetHouse.ResetScope;
    if (scope != 1 && scope != 2) // HousingHouseScope::Interior / ::Exterior
    {
        sendResult(HOUSING_RESULT_GENERIC_FAILURE);
        return;
    }

    bool wantExterior = (scope == 2);
    uint8 plotIndex = housing->GetPlotIndex();

    // The pieces to take out, so their visuals can be despawned afterwards. ResetDecor puts each into the account's
    // storage.
    std::vector<ObjectGuid> removedList;
    for (auto const& [guid, decor] : housing->GetPlacedDecorMap())
        if (Housing::IsExteriorDecorPlacement(decor.RoomGuid) == wantExterior)
            removedList.push_back(guid);

    uint32 removed = 0;
    HousingResult result = housing->ResetDecor(scope, &removed);

    if (result == HOUSING_RESULT_SUCCESS)
    {
        HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap());
        HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap());
        for (ObjectGuid const& decorGuid : removedList)
        {
            if (housingMap)
                housingMap->DespawnDecorItem(plotIndex, decorGuid);
            else if (interiorMap)
                interiorMap->DespawnDecorItem(decorGuid);
        }
        if (GetBattlenetAccount().IsHousingDecorStorageSent())
            GetBattlenetAccount().SendUpdateToPlayer(player);
    }

    sendResult(result);

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_RESET_HOUSE player={} scope={} removed={} result={}",
        player->GetGUID().ToString(), uint32(scope), removed, uint32(result));
}

// CMSG_HOUSING_DECOR_SET_PET (0x320003) — wire: PackedGUID DecorGUID + PackedGUID PetGUID + uint8 Flag.
// Binds (or, with an empty PetGUID, clears) a battle pet on a placed decor slot and persists it.
// The client updates its local decor-instance info optimistically; there is no dedicated response
// opcode in the 12.1 protocol (the DECOR response range 0x55xxxx has no SET_PET member), so the
// server acknowledges by refreshing the owner's account decor storage entity.
void WorldSession::HandleHousingDecorSetPet(WorldPackets::Housing::HousingDecorSetPet const& housingDecorSetPet)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    if (!housing)
        return;

    // Only the house owner may modify decor.
    if (!PlayerCanEditHousing(player, housing))
        return;

    // The target decor instance must exist in this house.
    if (!housing->GetPlacedDecor(housingDecorSetPet.DecorGuid))
        return;

    // When binding a pet (non-empty GUID), it must belong to this player's battle-pet journal.
    ObjectGuid petGuid = housingDecorSetPet.PetGuid;
    if (!petGuid.IsEmpty())
    {
        BattlePets::BattlePetMgr* petMgr = GetBattlePetMgr();
        if (!petMgr || !petMgr->GetPet(petGuid))
        {
            TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_SET_PET: player {} tried to bind unowned pet {}",
                player->GetGUID().ToString(), petGuid.ToString());
            return;
        }
    }

    HousingResult result = housing->SetDecorPet(housingDecorSetPet.DecorGuid, petGuid, housingDecorSetPet.Flag);
    if (result == HOUSING_RESULT_SUCCESS)
    {
        // Refresh the owner's account decor storage so the client's decor-instance info
        // (GetDecorAssignedPetName) reflects the new binding.
        GetBattlenetAccount().SendUpdateToPlayer(player);
    }

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_DECOR_SET_PET player={} decor={} pet={} flag={} result={}",
        player->GetGUID().ToString(), housingDecorSetPet.DecorGuid.ToString(),
        petGuid.ToString(), uint32(housingDecorSetPet.Flag), uint32(result));
}

// CMSG_HOUSING_SVCS_HOUSE_FINDER_IGNORE_NEIGHBORHOOD (0x350026) — wire: PackedGUID NeighborhoodGuid.
// Records a per-player ignored neighborhood so the house finder excludes it, then replies
// SMSG_HOUSING_SVCS_IGNORE_NEIGHBORHOOD_INVITE_RESPONSE { bool Success, PackedGUID NeighborhoodGuid },
// which drives the client IGNORE_NEIGHBORHOOD_RESPONSE event.
void WorldSession::HandleHousingSvcsHouseFinderIgnoreNeighborhood(WorldPackets::Housing::HousingSvcsHouseFinderIgnoreNeighborhood const& housingSvcsHouseFinderIgnoreNeighborhood)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    ObjectGuid neighborhoodGuid = housingSvcsHouseFinderIgnoreNeighborhood.NeighborhoodGuid;
    bool success = false;
    if (!neighborhoodGuid.IsEmpty())
    {
        // The client may send a bulletin-board GO GUID; resolve to the real neighborhood GUID
        // so the stored ignore matches the finder's GetGuid() comparison.
        if (Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(neighborhoodGuid, player))
            neighborhoodGuid = neighborhood->GetGuid();

        sHousingMgr.AddIgnoredNeighborhood(player->GetGUID(), neighborhoodGuid);
        success = true;
    }

    WorldPackets::Housing::HousingSvcsIgnoreNeighborhoodInviteResponse response;
    response.Success = success;
    response.NeighborhoodGuid = neighborhoodGuid;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_SVCS_HOUSE_FINDER_IGNORE_NEIGHBORHOOD player={} neighborhood={} success={}",
        player->GetGUID().ToString(), neighborhoodGuid.ToString(), success);
}

// ============================================================
// Other Housing CMSG
// ============================================================

void WorldSession::HandleQueryNeighborhoodInfo(WorldPackets::Housing::QueryNeighborhoodInfo const& queryNeighborhoodInfo)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    WorldPackets::Housing::QueryNeighborhoodNameResponse response;

    Neighborhood const* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(queryNeighborhoodInfo.NeighborhoodGuid, player);
    if (neighborhood)
    {
        // Use the canonical neighborhood GUID, not the client's (which may be a GO GUID or empty)
        response.NeighborhoodGuid = neighborhood->GetGuid();
        response.Result = true;
        response.NeighborhoodName = neighborhood->GetName();
    }
    else
    {
        response.NeighborhoodGuid = queryNeighborhoodInfo.NeighborhoodGuid;
        response.Result = false;
    }

    WorldPacket const* namePkt = response.Write();
    SendPacket(namePkt);

    TC_LOG_DEBUG("housing", "SMSG_QUERY_NEIGHBORHOOD_NAME_RESPONSE Result={}, Name='{}', NeighborhoodGuid: {}",
        response.Result, response.NeighborhoodName, response.NeighborhoodGuid.ToString());
}

void WorldSession::HandleInvitePlayerToNeighborhood(WorldPackets::Housing::InvitePlayerToNeighborhood const& invitePlayerToNeighborhood)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // 12.0.7 (build 68275): the CMSG carries only the invitee's NAME (RE 0x40019b). Resolve the
    // inviter's own neighborhood (their house's neighborhood) and look the invitee up by name.
    ObjectGuid neighborhoodGuid;
    if (Housing const* housing = player->GetHousing())
        neighborhoodGuid = housing->GetNeighborhoodGuid();

    Neighborhood* neighborhood = sNeighborhoodMgr.ResolveNeighborhood(neighborhoodGuid, player);
    if (!neighborhood)
    {
        WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    if (!neighborhood->IsManager(player->GetGUID()) && !neighborhood->IsOwner(player->GetGUID()))
    {
        WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_GENERIC_FAILURE);
        SendPacket(response.Write());
        return;
    }

    // The client API is C_HousingNeighborhood.InvitePlayerToNeighborhood(playerName), so this
    // opcode is a name -> GUID resolution as much as an invite. Resolve connected players first,
    // then fall back to the character cache so offline characters resolve too.
    ObjectGuid inviteeGuid;
    if (Player* invitee = ObjectAccessor::FindConnectedPlayerByName(invitePlayerToNeighborhood.PlayerName))
        inviteeGuid = invitee->GetGUID();
    else
        inviteeGuid = sCharacterCache->GetCharacterGuidByName(invitePlayerToNeighborhood.PlayerName);

    // SMSG_NEIGHBORHOOD_INVITE_NAME_LOOKUP_RESULT (0x5C0011) reports the outcome of that
    // resolution. Client handler (68275, case 6029329) reads uint8 Result then a PackedGUID and
    // only raises its Lua event when the GUID's HighGuid type field is non-zero — so "not found"
    // is encoded as an empty GUID, which is what the failure path below sends.
    WorldPackets::Neighborhood::NeighborhoodInviteNameLookupResult lookupResult;
    lookupResult.Result = static_cast<uint8>(inviteeGuid.IsEmpty()
        ? HOUSING_RESULT_PLAYER_NOT_FOUND : HOUSING_RESULT_SUCCESS);
    lookupResult.PlayerGuid = inviteeGuid;
    SendPacket(lookupResult.Write());

    if (inviteeGuid.IsEmpty())
    {
        WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
        response.Result = static_cast<uint8>(HOUSING_RESULT_PLAYER_NOT_FOUND);
        SendPacket(response.Write());

        TC_LOG_DEBUG("housing", "CMSG_INVITE_PLAYER_TO_NEIGHBORHOOD: name '{}' did not resolve to a character",
            invitePlayerToNeighborhood.PlayerName);
        return;
    }

    HousingResult result = neighborhood->InviteResident(player->GetGUID(), inviteeGuid);

    WorldPackets::Neighborhood::NeighborhoodInviteResidentResponse response;
    response.Result = static_cast<uint8>(result);
    response.InviteeGuid = inviteeGuid;
    SendPacket(response.Write());

    // Notify the invitee that they received a neighborhood invite
    if (result == HOUSING_RESULT_SUCCESS)
    {
        if (Player* invitee = ObjectAccessor::FindPlayer(inviteeGuid))
        {
            WorldPackets::Neighborhood::NeighborhoodInviteNotification notification;
            notification.NeighborhoodGuid = neighborhood->GetGuid();
            invitee->SendDirectMessage(notification.Write());
        }
    }

    TC_LOG_DEBUG("housing", "CMSG_INVITE_PLAYER_TO_NEIGHBORHOOD PlayerName: '{}', Result: {}",
        invitePlayerToNeighborhood.PlayerName, uint32(result));
}

void WorldSession::HandleGuildGetOthersOwnedHouses(WorldPackets::Housing::GuildGetOthersOwnedHouses const& guildGetOthersOwnedHouses)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    // Look up houses owned by the specified player (typically a guild member)
    std::vector<Neighborhood*> neighborhoods = sNeighborhoodMgr.GetNeighborhoodsForPlayer(guildGetOthersOwnedHouses.PlayerGuid);

    // PLAN_B4 C.1: CMSG_GUILD_GET_OTHERS_OWNED_HOUSES is answered by the dedicated
    // SMSG_GUILD_OTHERS_OWNED_HOUSES_RESULT (0x510047) -- a FLAT house list plus the querying
    // player's guild GUID -- not the neighborhood-grouped HousingSvcsGuildGetHousingInfoResponse
    // (0x580016) that was sent before. That grouped packet is a different opcode entirely, so the
    // client never matched it to this request (the handler produced no visible effect). Wire is
    // IDA-verified for build 67186; JamCliHouse element layout is the 12.0.7/68275 order.
    // UNVERIFIED against a live 69404 sniff -- re-check when a capture is available.
    WorldPackets::Housing::GuildOthersOwnedHousesResult response;
    if (Guild const* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
        response.GuildGuid = guild->GetGUID();
    for (Neighborhood* neighborhood : neighborhoods)
    {
        for (auto const& plot : neighborhood->GetPlots())
        {
            if (!plot.IsOccupied())
                continue;
            neighborhood->FillPlotHouseEntry(plot, response.Houses.emplace_back());
        }
    }
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_GUILD_GET_OTHERS_OWNED_HOUSES PlayerGuid: {}, FoundNeighborhoods: {}",
        guildGetOthersOwnedHouses.PlayerGuid.ToString(), uint32(neighborhoods.size()));
}

// ============================================================
// Photo Sharing Authorization
// ============================================================

void WorldSession::HandleHousingPhotoSharingCompleteAuthorization(WorldPackets::Housing::HousingPhotoSharingCompleteAuthorization const& /*packet*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    WorldPackets::Housing::HousingPhotoSharingAuthorizationResult response;

    // The result byte is NOT a HousingResult - it is the authorized flag: a capture from the 68974 test client
    // shows retail answering a successful completion with 01, and the
    // sibling SMSG_HOUSING_PHOTO_SHARING_AUTHORIZATION_CLEARED_RESULT is a single bool-u8 as well.
    if (!housing || housing->GetHouseGuid().IsEmpty())
    {
        response.Result = 0;
        SendPacket(response.Write());
        return;
    }

    // Track authorization state on the Housing object (per-session, volatile).
    // Actual screenshot hosting requires an external CDN — server only tracks the auth grant.
    housing->SetPhotoSharingAuthorized(true);
    response.Result = 1;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_PHOTO_SHARING_COMPLETE_AUTHORIZATION Player: {} authorized photo sharing for house {}",
        player->GetGUID().ToString(), housing->GetHouseGuid().ToString());
}

void WorldSession::HandleHousingPhotoSharingClearAuthorization(WorldPackets::Housing::HousingPhotoSharingClearAuthorization const& /*packet*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Housing* housing = player->GetHousing();
    WorldPackets::Housing::HousingPhotoSharingAuthorizationClearedResult response;

    if (!housing || housing->GetHouseGuid().IsEmpty())
    {
        response.Result = static_cast<uint8>(HOUSING_RESULT_HOUSE_NOT_FOUND);
        SendPacket(response.Write());
        return;
    }

    housing->SetPhotoSharingAuthorized(false);
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_PHOTO_SHARING_CLEAR_AUTHORIZATION Player: {} cleared photo sharing for house {}",
        player->GetGUID().ToString(), housing->GetHouseGuid().ToString());
}

// ============================================================
// Decor Licensing / Refund Handlers
// ============================================================

void WorldSession::HandleGetAllLicensedDecorQuantities(WorldPackets::Housing::GetAllLicensedDecorQuantities const& /*getAllLicensedDecorQuantities*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_GET_ALL_LICENSED_DECOR_QUANTITIES Player: {}", player->GetGUID().ToString());

    WorldPackets::Housing::GetAllLicensedDecorQuantitiesResponse response;

    // One entry per HouseDecor id the account holds under a shop license (source 7 or 8): how many of those pieces
    // are placed and how many are in storage (hbcd3 1431934; hled1 789147-789152 reads 1 and 1 for 12247 once one is
    // placed). Starter, redeemed and item decor are not licensed and are not listed.
    auto isLicensed = [](uint8 sourceType) { return sourceType == DECOR_SOURCE_SHOP_LICENSE || sourceType == DECOR_SOURCE_SHOP_LICENSE_2; };
    std::map<uint32, WorldPackets::Housing::JamLicensedDecorQuantity> quantities;
    if (HousingDecorStore* store = player->GetHousingDecorStore())
    {
        for (Housing::PlacedDecor const& decor : store->GetStored())
        {
            if (!isLicensed(decor.SourceType))
                continue;
            WorldPackets::Housing::JamLicensedDecorQuantity& qty = quantities[decor.DecorEntryId];
            qty.HouseDecorID = decor.DecorEntryId;
            ++qty.StoredQuantity;
        }
    }
    // A packed house keeps its pieces placed in it, so they count as placed, as the storage lists them.
    for (Housing const* housing : player->GetAllHousings(/*includePacked*/ true))
    {
        if (housing->IsDeleted())
            continue;

        for (auto const& [decorGuid, decor] : housing->GetPlacedDecorMap())
        {
            if (!isLicensed(decor.SourceType))
                continue;
            WorldPackets::Housing::JamLicensedDecorQuantity& qty = quantities[decor.DecorEntryId];
            qty.HouseDecorID = decor.DecorEntryId;
            ++qty.PlacedQuantity;
        }
    }
    for (auto const& [decorEntryId, qty] : quantities)
        response.Quantities.push_back(qty);

    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "SMSG_GET_ALL_LICENSED_DECOR_QUANTITIES_RESPONSE sent to Player: {} with {} quantities",
        player->GetGUID().ToString(), response.Quantities.size());
}

void WorldSession::HandleGetDecorRefundList(WorldPackets::Housing::GetDecorRefundList const& /*getDecorRefundList*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_GET_DECOR_REFUND_LIST Player: {}", player->GetGUID().ToString());

    WorldPackets::Housing::GetDecorRefundListResponse response;

    Housing* housing = player->GetAccountCatalogHousing();
    if (housing)
    {
        time_t now = GameTime::GetGameTime();
        constexpr time_t REFUND_WINDOW = 2 * HOUR;

        for (auto const& [guid, decor] : housing->GetPlacedDecorMap())
        {
            if (decor.PlacementTime > 0 && (now - decor.PlacementTime) < REFUND_WINDOW)
            {
                WorldPackets::Housing::JamClientRefundableDecor refund;
                refund.DecorID = decor.DecorEntryId;
                refund.RefundPrice = 0;
                refund.ExpiryTime = static_cast<uint64>(decor.PlacementTime + REFUND_WINDOW);
                refund.Flags = 0;
                response.Decors.push_back(refund);
            }
        }
    }

    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "SMSG_GET_DECOR_REFUND_LIST_RESPONSE sent to Player: {} with {} decors",
        player->GetGUID().ToString(), response.Decors.size());
}

void WorldSession::HandleBulkRefund(WorldPackets::Housing::BulkRefund const& bulkRefund)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    TC_LOG_DEBUG("housing", "CMSG_BULK_REFUND Player: {} DecorGUIDs: {}",
        player->GetGUID().ToString(), bulkRefund.DecorGUIDs.size());

    Housing* housing = player->GetAccountCatalogHousing();
    if (!housing)
    {
        WorldPackets::Housing::BulkRefundResponse response;
        response.Result = static_cast<uint8>(BULK_REFUND_RESULT_INVALID_REQUEST);
        SendPacket(response.Write());
        return;
    }

    if (bulkRefund.DecorGUIDs.empty())
    {
        WorldPackets::Housing::BulkRefundResponse response;
        response.Result = static_cast<uint8>(BULK_REFUND_RESULT_INVALID_REQUEST);
        SendPacket(response.Write());
        return;
    }

    // Validate all GUIDs exist and are within the refund window before refunding any.
    // This is atomic: if any GUID fails validation, the entire batch fails.
    time_t now = GameTime::GetGameTime();
    constexpr time_t REFUND_WINDOW = 2 * HOUR;

    for (ObjectGuid const& decorGuid : bulkRefund.DecorGUIDs)
    {
        Housing::PlacedDecor const* placedDecor = housing->GetPlacedDecor(decorGuid);
        if (!placedDecor)
        {
            TC_LOG_DEBUG("housing", "CMSG_BULK_REFUND: DecorGUID {} not found in placed decor", decorGuid.ToString());
            WorldPackets::Housing::BulkRefundResponse response;
            response.Result = static_cast<uint8>(BULK_REFUND_RESULT_INVALID_REQUEST);
            SendPacket(response.Write());
            return;
        }

        if (placedDecor->PlacementTime == 0 || (now - placedDecor->PlacementTime) >= REFUND_WINDOW)
        {
            TC_LOG_DEBUG("housing", "CMSG_BULK_REFUND: DecorGUID {} outside refund window (placed {}s ago)",
                decorGuid.ToString(), placedDecor->PlacementTime > 0 ? (now - placedDecor->PlacementTime) : -1);
            WorldPackets::Housing::BulkRefundResponse response;
            response.Result = static_cast<uint8>(BULK_REFUND_RESULT_REFUND_WINDOW_EXPIRED);
            SendPacket(response.Write());
            return;
        }
    }

    // All GUIDs validated — proceed with refund.
    // Each decor is removed and returned to catalog (same as individual RemoveDecor).
    uint8 plotIndex = housing->GetPlotIndex();
    uint32 refundedCount = 0;

    for (ObjectGuid const& decorGuid : bulkRefund.DecorGUIDs)
    {
        HousingResult result = housing->RemoveDecor(decorGuid);
        if (result != HOUSING_RESULT_SUCCESS)
        {
            TC_LOG_WARN("housing", "CMSG_BULK_REFUND: RemoveDecor failed for {} with result {} (after validation passed)",
                decorGuid.ToString(), uint32(result));
            continue;
        }

        // Despawn the decor entity from the map
        if (HousingMap* housingMap = dynamic_cast<HousingMap*>(player->GetMap()))
            housingMap->DespawnDecorItem(plotIndex, decorGuid);
        else if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(player->GetMap()))
            interiorMap->DespawnDecorItem(decorGuid);

        ++refundedCount;
    }

    // Send single batch update to client after all removals
    if (refundedCount > 0 && GetBattlenetAccount().IsHousingDecorStorageSent())
        GetBattlenetAccount().SendUpdateToPlayer(player);

    WorldPackets::Housing::BulkRefundResponse response;
    response.Result = static_cast<uint8>(BULK_REFUND_RESULT_SUCCESS);
    SendPacket(response.Write());

    TC_LOG_INFO("housing", "CMSG_BULK_REFUND: Player {} refunded {}/{} decors",
        player->GetName(), refundedCount, bulkRefund.DecorGUIDs.size());
}

void WorldSession::HandleGetLastCatalogFetch(WorldPackets::Housing::GetLastCatalogFetch const& /*getLastCatalogFetch*/)
{
    // Sniff-verified (build 66337): retail DOES respond with SMSG_LAST_CATALOG_FETCH_RESPONSE
    // containing a uint64 Unix timestamp. This corrects the earlier finding that "retail never
    // responds" — that was from an older build. Build 66337 sends it 5-6 times per session.
    TC_LOG_DEBUG("housing", "CMSG_GET_LAST_CATALOG_FETCH from player {}",
        GetPlayer() ? GetPlayer()->GetGUID().ToString() : "null");

    // The time stored for the account, or 0 before any update. Retail answered the same stored value, 0x6A64F75E, to
    // requests twenty minutes apart (hled1 99487, 422569, 617088), not the current time.
    WorldPackets::Housing::LastCatalogFetchResponse response;
    if (Player* player = GetPlayer())
        if (HousingDecorStore* store = player->GetHousingDecorStore())
            response.Timestamp = store->GetLastCatalogFetch();
    SendPacket(response.Write());
}

void WorldSession::HandleUpdateLastCatalogFetch(WorldPackets::Housing::UpdateLastCatalogFetch const& /*updateLastCatalogFetch*/)
{
    // Sniff-verified (build 66337): retail responds with SMSG_LAST_CATALOG_FETCH_RESPONSE
    // to BOTH GetLastCatalogFetch AND UpdateLastCatalogFetch. 8-byte timestamp payload.
    TC_LOG_DEBUG("housing", "CMSG_UPDATE_LAST_CATALOG_FETCH from player {}",
        GetPlayer() ? GetPlayer()->GetGUID().ToString() : "null");

    // The reply carries the time stored before this update; the update then stores now, for every character of the
    // account and across sessions.
    WorldPackets::Housing::LastCatalogFetchResponse response;
    if (Player* player = GetPlayer())
        if (HousingDecorStore* store = player->GetHousingDecorStore())
            response.Timestamp = store->ExchangeLastCatalogFetch(uint64(GameTime::GetGameTime()));
    SendPacket(response.Write());
}

// ============================================================================
// Housing blueprints (12.1.0.69587). Wire layouts and meanings: HousingBlueprintPackets.h.
// ============================================================================

namespace
{
    // The house the player is in (interior) or on (plot), whoever owns it. Visitors can only export a house whose owner is
    // online, because only then is the house loaded.
    Housing* FindBlueprintContextHouse(Player* player, bool& inInterior)
    {
        inInterior = false;
        Map* map = player->GetMap();
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(map))
        {
            inInterior = true;
            return interiorMap->GetOwnerHousing();
        }

        HousingMap* housingMap = dynamic_cast<HousingMap*>(map);
        if (!housingMap || !housingMap->GetNeighborhood())
            return nullptr;

        int8 const plotIndex = housingMap->GetPlayerCurrentPlot(player->GetGUID());
        if (plotIndex < 0)
            return nullptr;

        Neighborhood::PlotInfo const* plotInfo = housingMap->GetNeighborhood()->GetPlotInfo(uint8(plotIndex));
        if (!plotInfo || plotInfo->OwnerGuid.IsEmpty())
            return nullptr;

        Player* owner = ObjectAccessor::FindConnectedPlayer(plotInfo->OwnerGuid);
        Housing* housing = owner ? owner->GetHousingForNeighborhood(housingMap->GetNeighborhood()->GetGuid()) : nullptr;
        return housing && housing->GetHouseGuid() == plotInfo->HouseGuid ? housing : nullptr;
    }

    bool IsImportableBlueprintType(uint8 type)
    {
        return type >= uint8(HousingBlueprintType::House) && type <= uint8(HousingBlueprintType::Exterior);
    }
}

void WorldSession::HandleHousingBlueprintRequestCollection(WorldPackets::Housing::HousingBlueprintRequestCollection const& /*packet*/)
{
    WorldPackets::Housing::HousingBlueprintCollection response;
    response.Result = HOUSING_RESULT_SUCCESS;
    for (HousingBlueprint const* blueprint : sHousingBlueprintMgr.GetCollection(GetBattlenetAccountId()))
    {
        WorldPackets::Housing::JamHousingBlueprint& jam = response.Blueprints.emplace_back();
        jam.ID = blueprint->Id;
        jam.Uuid = blueprint->Uuid;
        jam.Name = blueprint->Name;
        jam.Type = uint8(blueprint->Type);
        jam.DateCreated = blueprint->CreateTime;
        jam.Flags = blueprint->Flags;
    }
    SendPacket(response.Write());
}

void WorldSession::HandleHousingBlueprintExport(WorldPackets::Housing::HousingBlueprintExport const& packet)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    WorldPackets::Housing::HousingBlueprintExportResponse response;
    response.BlueprintType = packet.BlueprintType;

    auto result = [&]() -> HousingResult
    {
        if (!IsImportableBlueprintType(packet.BlueprintType))
            return HOUSING_RESULT_BLUEPRINT_TYPE_INVALID;

        HousingBlueprintType const type = HousingBlueprintType(packet.BlueprintType);
        if (!HousingBlueprintMgr::IsValidName(packet.Name))
            return HOUSING_RESULT_BLUEPRINT_NAME_INVALID;

        bool inInterior = false;
        Housing* housing = FindBlueprintContextHouse(player, inInterior);
        if (!housing || !housing->GetOwner())
            return HOUSING_RESULT_BLUEPRINT_LOCATION_INVALID;

        if (type == HousingBlueprintType::Room && !inInterior)
            return HOUSING_RESULT_BLUEPRINT_TYPE_LOCATION_INVALID;

        if (!sHousingMgr.CanVisitorExportBlueprint(player, housing->GetOwner()->GetGUID(), housing->GetSettingsFlags()))
            return HOUSING_RESULT_PERMISSION_DENIED;

        if (sHousingBlueprintMgr.GetPlayerMadeCount(GetBattlenetAccountId()) >= HOUSING_BLUEPRINTS_MAX_PER_BNET_ACCOUNT)
            return HOUSING_RESULT_BLUEPRINT_STORAGE_LIMIT;

        HousingBlueprintContent content;
        if (HousingResult snapshot = HousingBlueprintMgr::Snapshot(*housing, type, packet.RoomGuid, content); snapshot != HOUSING_RESULT_SUCCESS)
            return snapshot;

        HousingBlueprint const* blueprint = sHousingBlueprintMgr.Create(GetBattlenetAccountId(), player->GetGUID().GetCounter(), packet.Name,
            type, HOUSING_BLUEPRINT_FLAG_NONE, std::move(content));
        if (!blueprint)
            return HOUSING_RESULT_BLUEPRINT_GENERIC_EXPORT_ERROR;

        response.Uuid = blueprint->Uuid;
        return HOUSING_RESULT_SUCCESS;
    }();

    response.Result = uint8(result);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_BLUEPRINT_EXPORT player={} type={} name='{}' room={} result={} uuid={}",
        player->GetGUID().ToString(), packet.BlueprintType, packet.Name, packet.RoomGuid.ToString(), uint32(result), response.Uuid);
}

void WorldSession::HandleHousingBlueprintRename(WorldPackets::Housing::HousingBlueprintRename const& packet)
{
    WorldPackets::Housing::HousingBlueprintRenameResponse response;
    response.BlueprintID = packet.BlueprintID;
    response.Result = uint8(sHousingBlueprintMgr.Rename(GetBattlenetAccountId(), packet.BlueprintID, packet.Name));
    if (response.Result == HOUSING_RESULT_SUCCESS)
        response.Name = packet.Name;
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_BLUEPRINT_RENAME account={} blueprint={} name='{}' result={}",
        GetBattlenetAccountId(), packet.BlueprintID, packet.Name, uint32(response.Result));
}

void WorldSession::HandleHousingBlueprintDelete(WorldPackets::Housing::HousingBlueprintDelete const& packet)
{
    WorldPackets::Housing::HousingBlueprintDeleteResponse response;
    response.BlueprintID = packet.BlueprintID;
    response.Result = uint8(sHousingBlueprintMgr.Delete(GetBattlenetAccountId(), packet.BlueprintID));
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_BLUEPRINT_DELETE account={} blueprint={} result={}",
        GetBattlenetAccountId(), packet.BlueprintID, uint32(response.Result));
}

void WorldSession::HandleHousingBlueprintRequestContents(WorldPackets::Housing::HousingBlueprintRequestContents const& packet)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    WorldPackets::Housing::HousingBlueprintContents response;
    response.BlueprintType = packet.BlueprintType;
    response.TargetHouseGuid = packet.TargetHouseGuid;
    response.Uuid = packet.Uuid;

    HousingBlueprint const* blueprint = sHousingBlueprintMgr.GetByUuid(packet.Uuid);
    if (HousingBlueprintMgr::NormalizeUuid(packet.Uuid).empty())
        response.Result = HOUSING_RESULT_BLUEPRINT_CODE_INVALID;
    else if (!blueprint)
        response.Result = HOUSING_RESULT_BLUEPRINT_NOT_FOUND;
    else if (uint8(blueprint->Type) != packet.BlueprintType)
        response.Result = HOUSING_RESULT_BLUEPRINT_TYPE_INVALID;
    else
    {
        // Evaluated against the house the client names, which has to be one of the player's.
        Housing const* target = nullptr;
        if (!packet.TargetHouseGuid.IsEmpty())
            for (Housing const* housing : player->GetAllHousings())
                if (housing && housing->GetHouseGuid() == packet.TargetHouseGuid)
                    target = housing;

        HousingBlueprintEvaluation evaluation;
        HousingBlueprintMgr::Evaluate(*blueprint, target, player->GetHousingDecorStore(), evaluation);

        response.Result = HOUSING_RESULT_SUCCESS;
        response.UnmetRequirementFlags = evaluation.UnmetRequirementFlags;
        response.Missing = std::move(evaluation.Missing);
        response.Invalid = std::move(evaluation.Invalid);
        response.InteriorBudgets = std::move(evaluation.InteriorBudgets);
        response.ExteriorBudgets = std::move(evaluation.ExteriorBudgets);
        response.Contents = std::move(evaluation.Totals);
        if (!target)
            response.TargetHouseGuid.Clear();
    }

    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_BLUEPRINT_REQUEST_CONTENTS player={} uuid={} type={} house={} result={} unmet=0x{:X}",
        player->GetGUID().ToString(), packet.Uuid, packet.BlueprintType, packet.TargetHouseGuid.ToString(), uint32(response.Result),
        response.UnmetRequirementFlags);
}

void WorldSession::HandleHousingBlueprintImport(WorldPackets::Housing::HousingBlueprintImport const& packet)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    WorldPackets::Housing::HousingBlueprintImportResponse response;
    response.BlueprintType = packet.BlueprintType;
    response.Uuid = packet.Uuid;

    HousingBlueprintApplyResult applied;
    Housing* housing = player->GetHousing();

    auto result = [&]() -> HousingResult
    {
        if (HousingBlueprintMgr::NormalizeUuid(packet.Uuid).empty())
            return HOUSING_RESULT_BLUEPRINT_CODE_INVALID;

        HousingBlueprint const* blueprint = sHousingBlueprintMgr.GetByUuid(packet.Uuid);
        if (!blueprint)
            return HOUSING_RESULT_BLUEPRINT_NOT_FOUND;

        if (!IsImportableBlueprintType(packet.BlueprintType) || uint8(blueprint->Type) != packet.BlueprintType)
            return HOUSING_RESULT_BLUEPRINT_TYPE_INVALID;

        // Imports only ever change the player's own house, from inside it or its plot.
        if (!housing || !PlayerCanEditHousing(player, housing))
            return HOUSING_RESULT_BLUEPRINT_LOCATION_INVALID;

        if (blueprint->Type != HousingBlueprintType::Room)
            return sHousingBlueprintMgr.ApplyLayout(player, housing, *blueprint, applied);

        // A room goes onto the door the player picked in layout mode.
        if (!dynamic_cast<HouseInteriorMap*>(player->GetMap()))
            return HOUSING_RESULT_BLUEPRINT_TYPE_LOCATION_INVALID;

        if (!housing->GetRoom(packet.SourceRoomGuid) || blueprint->Content.Rooms.empty())
            return HOUSING_RESULT_BLUEPRINT_ROOM_PLACEMENT_REQUIRED;

        HousingBlueprintEvaluation evaluation;
        HousingBlueprintMgr::Evaluate(*blueprint, housing, player->GetHousingDecorStore(), evaluation);
        if (evaluation.IsBlocked())
            return HOUSING_RESULT_BLUEPRINT_REQUIREMENTS_UNMET;

        ObjectGuid roomGuid;
        HousingResult roomResult = AddHousingRoomAtDoor(housing, packet.TargetDoorComponentID, blueprint->Content.Rooms.front().RoomEntryId, &roomGuid);
        if (roomResult != HOUSING_RESULT_SUCCESS)
            return roomResult;

        HousingBlueprintMgr::ApplyRoomDecor(player, housing, *blueprint, roomGuid, applied);
        return HOUSING_RESULT_SUCCESS;
    }();

    if (result == HOUSING_RESULT_SUCCESS && housing)
    {
        RespawnHousingAfterBlueprintImport(player, housing, applied.InteriorChanged, applied.ExteriorChanged, applied.RemovedDecor);
        player->SaveToDB();
    }

    response.Result = uint8(result);
    SendPacket(response.Write());

    TC_LOG_DEBUG("housing", "CMSG_HOUSING_BLUEPRINT_IMPORT player={} uuid={} type={} room={} door={} result={} placed={} skipped={}",
        player->GetGUID().ToString(), packet.Uuid, packet.BlueprintType, packet.SourceRoomGuid.ToString(), packet.TargetDoorComponentID,
        uint32(result), applied.PlacedDecor, applied.SkippedDecor);
}

void WorldSession::RespawnHousingAfterBlueprintImport(Player* player, Housing* housing, bool interiorChanged, bool exteriorChanged,
    std::vector<ObjectGuid> const& removedDecor)
{
    // Rebuild every loaded copy of the house, not only the map the importer stands on: the interior instance is keyed
    // by owner and may hold visitors, and the plot is on a neighborhood map others are watching. Session packets are
    // processed between map updates, so touching another map here is safe.
    ObjectGuid const ownerGuid = player->GetGUID();
    ObjectGuid const neighborhoodGuid = housing->GetNeighborhoodGuid();
    int32 const faction = player->GetTeamId() == TEAM_ALLIANCE ? NEIGHBORHOOD_FACTION_ALLIANCE : NEIGHBORHOOD_FACTION_HORDE;

    sMapMgr->DoForAllMaps([&](Map* map)
    {
        if (HouseInteriorMap* interiorMap = dynamic_cast<HouseInteriorMap*>(map))
        {
            if (!interiorChanged || interiorMap->GetOwnerGuid() != ownerGuid)
                return;

            for (ObjectGuid const& decorGuid : removedDecor)
                interiorMap->DespawnDecorItem(decorGuid);

            // Rooms, their meshes and every decor item hang off each other: rebuild the whole interior.
            interiorMap->DespawnAllRoomMeshObjects();
            interiorMap->SpawnRoomMeshObjects(housing, faction);
            interiorMap->SpawnInteriorDecor(housing);

            TC_LOG_DEBUG("housing", "RespawnHousingAfterBlueprintImport: rebuilt interior instance {} of {}", map->GetInstanceId(), ownerGuid.ToString());
            return;
        }

        HousingMap* housingMap = dynamic_cast<HousingMap*>(map);
        if (!exteriorChanged || !housingMap || !housingMap->GetNeighborhood() || housingMap->GetNeighborhood()->GetGuid() != neighborhoodGuid)
            return;

        uint8 const plotIndex = housing->GetPlotIndex();
        auto fixtureOverrides = housing->GetFixtureOverrideMap();
        auto rootOverrides = housing->GetRootComponentOverrides();
        Position const housePos = housing->GetHousePosition();
        housingMap->DespawnAllDecorForPlot(plotIndex);
        housingMap->DespawnHouseForPlot(plotIndex);
        housingMap->SpawnHouseForPlot(plotIndex, housing->HasCustomPosition() ? &housePos : nullptr,
            static_cast<int32>(housing->GetCoreExteriorComponentID()),
            static_cast<int32>(housing->GetHouseType()),
            fixtureOverrides.empty() ? nullptr : &fixtureOverrides,
            rootOverrides.empty() ? nullptr : &rootOverrides);
        housingMap->SpawnAllDecorForPlot(plotIndex, housing);

        TC_LOG_DEBUG("housing", "RespawnHousingAfterBlueprintImport: rebuilt plot {} on neighborhood map {} instance {}", uint32(plotIndex),
            map->GetId(), map->GetInstanceId());
    });

    // The importer's own client gets the house entity and mesh CREATEs inline, as after any fixture change.
    if (exteriorChanged && dynamic_cast<HousingMap*>(player->GetMap()))
        SendFixtureUpdateObject(player, housing);

    GetBattlenetAccount().SendUpdateToPlayer(player);
}

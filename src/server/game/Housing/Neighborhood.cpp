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

#include "Neighborhood.h"
#include "HousingNeighborhoodMirrorEntity.h"
#include "BattlenetAccountMgr.h"
#include "DatabaseEnv.h"
#include "HousingMgr.h"
#include "HousingPackets.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RealmList.h"
#include "WorldSession.h"
#include <algorithm>

Neighborhood::Neighborhood(ObjectGuid guid) : _guid(guid)
{
}

bool Neighborhood::LoadFromDB(PreparedQueryResult neighborhood, PreparedQueryResult members, PreparedQueryResult invites,
    PreparedQueryResult houses /*= nullptr*/, PreparedQueryResult memberFixtures /*= nullptr*/, PreparedQueryResult memberDecor /*= nullptr*/,
    PreparedQueryResult memberRooms /*= nullptr*/)
{
    if (!neighborhood)
        return false;

    Field* fields = neighborhood->Fetch();

    //          0     1       2            3                    4         5
    // SELECT guid, name, neighborhoodMapID, ownerGuid, factionRestriction, isPublic,
    //          6          7
    //        createTime, guildId FROM neighborhoods WHERE guid = ?

    // _guid is already set in constructor
    _name               = fields[1].GetString();
    _neighborhoodMapID  = fields[2].GetUInt32();
    {
        uint64 ownerCounter = fields[3].GetUInt64();
        _ownerGuid = ownerCounter ? ObjectGuid::Create<HighGuid::Player>(ownerCounter) : ObjectGuid::Empty;
    }
    _factionRestriction = fields[4].GetInt32();
    _isPublic           = fields[5].GetBool();
    _createTime         = fields[6].GetUInt32();
    // The guild the neighborhood belongs to (0 = not a guild neighborhood). Without this
    // GetNeighborhoodByGuildId would find nothing after a restart.
    _guildId            = fields[7].GetUInt32();

    TC_LOG_DEBUG("housing", "Neighborhood::LoadFromDB: Loaded neighborhood '{}' (guid: {}), owner: {}, mapId: {}, members: loading...",
        _name, _guid.ToString(), _ownerGuid.ToString(), _neighborhoodMapID);

    // Load members
    if (members)
    {
        do
        {
            Field* memberFields = members->Fetch();

            //          0         1       2         3
            // SELECT playerGuid, role, joinTime, plotIndex FROM neighborhood_members WHERE neighborhoodGuid = ?

            Member member;
            member.PlayerGuid   = ObjectGuid::Create<HighGuid::Player>(memberFields[0].GetUInt64());
            member.Role         = memberFields[1].GetUInt8();
            member.JoinTime     = memberFields[2].GetUInt32();
            member.PlotIndex    = memberFields[3].GetUInt8();

            _members.push_back(member);
        } while (members->NextRow());
    }

    TC_LOG_DEBUG("housing", "Neighborhood::LoadFromDB: Loaded {} members for neighborhood '{}'",
        _members.size(), _name);

    // The houses standing in this neighborhood fill the plots. The owner shown on a plot is the house's cosmetic
    // owner; the plot belongs to the house's Battle.net account. Plot ownership reaches the client through
    // PlayerHouseInfoComponentData.CurrentHouse and NeighborhoodMirrorData.Houses, not through the area trigger.
    if (houses)
    {
        do
        {
            Field* houseFields = houses->Fetch();

            //        0         1             2           3                 4          5          6        7          8           9
            // SELECT guid, bnetAccountId, slot, cosmeticOwnerGuid, plotIndex, houseLevel, favor, houseName, houseType, settingsFlags,
            //        10    11    12     13
            //        posX, posY, posZ, facing
            // FROM character_housing WHERE neighborhoodGuid = ? AND packed = 0
            uint8 plotIndex = houseFields[4].GetUInt8();
            if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
            {
                TC_LOG_ERROR("housing", "Neighborhood::LoadFromDB: house {} in neighborhood '{}' stands on plot {}, which does not exist",
                    houseFields[0].GetUInt64(), _name, plotIndex);
                continue;
            }

            PlotInfo& plot = _plots[plotIndex];
            if (plot.IsOccupied())
            {
                TC_LOG_ERROR("housing", "Neighborhood::LoadFromDB: houses {} and {} in neighborhood '{}' both stand on plot {}; keeping the first",
                    plot.HouseDatabaseId, houseFields[0].GetUInt64(), _name, plotIndex);
                continue;
            }

            uint32 bnetAccountId = houseFields[1].GetUInt32();
            plot.PlotIndex        = plotIndex;
            plot.HouseDatabaseId  = houseFields[0].GetUInt64();
            plot.OwnerBnetGuid    = ObjectGuid::Create<HighGuid::BNetAccount>(bnetAccountId);
            plot.HouseGuid        = Housing::MakeHouseGuid(houseFields[2].GetUInt8(), bnetAccountId);
            if (uint64 cosmeticOwner = houseFields[3].GetUInt64())
                plot.OwnerGuid    = ObjectGuid::Create<HighGuid::Player>(cosmeticOwner);
            plot.HouseLevel       = static_cast<uint8>(std::max<uint32>(1, houseFields[5].GetUInt32()));
            plot.HouseFavor       = houseFields[6].GetUInt32();
            plot.HouseName        = houseFields[7].GetString();
            plot.HouseType        = houseFields[8].GetUInt32();
            plot.HouseSettingsFlags = houseFields[9].GetUInt32();
            plot.HousePlacement.Relocate(houseFields[10].GetFloat(), houseFields[11].GetFloat(), houseFields[12].GetFloat(),
                houseFields[13].GetFloat());
            // The same test Housing::LoadFromDB makes: a saved placement is any that is not all zero.
            plot.HasHousePlacement = plot.HousePlacement.GetPositionX() != 0.0f || plot.HousePlacement.GetPositionY() != 0.0f
                || plot.HousePlacement.GetPositionZ() != 0.0f || plot.HousePlacement.GetOrientation() != 0.0f;

            TC_LOG_DEBUG("housing", "Neighborhood::LoadFromDB plot[{}] house={} owner={} lvl={} favor={} name='{}'",
                plotIndex, plot.HouseGuid.ToString(), plot.OwnerGuid.ToString(), plot.HouseLevel, plot.HouseFavor, plot.HouseName);
        } while (houses->NextRow());
    }

    // Load pending invites
    if (invites)
    {
        do
        {
            Field* inviteFields = invites->Fetch();

            //          0           1          2
            // SELECT inviteeGuid, inviterGuid, inviteTime
            //        FROM neighborhood_invites WHERE neighborhoodGuid = ?

            PendingInvite invite;
            invite.InviteeGuid  = ObjectGuid::Create<HighGuid::Player>(inviteFields[0].GetUInt64());
            invite.InviterGuid  = ObjectGuid::Create<HighGuid::Player>(inviteFields[1].GetUInt64());
            invite.InviteTime   = inviteFields[2].GetUInt32();

            _pendingInvites.push_back(invite);
        } while (invites->NextRow());
    }

    TC_LOG_DEBUG("housing", "Neighborhood::LoadFromDB: Loaded {} pending invites for neighborhood '{}'",
        _pendingInvites.size(), _name);

    // Resolve a house database id to its plot (for the per-house fixture, decor and room result sets below).
    auto findPlotByHouse = [this](uint64 houseDatabaseId) -> PlotInfo*
    {
        for (PlotInfo& p : _plots)
            if (p.IsOccupied() && p.HouseDatabaseId == houseDatabaseId)
                return &p;
        return nullptr;
    };

    // Load fixture overrides for every occupied plot's owner. Drives exterior
    // customisation (roof / doors / windows) in HousingMap::SpawnPlotGameObjects
    // for plots whose owners aren't currently online.
    uint32 fixtureCount = 0;
    if (memberFixtures)
    {
        do
        {
            Field* f = memberFixtures->Fetch();
            //   0           1                 2
            // houseGuid, fixturePointId, fixtureOptionId
            PlotInfo* plot = findPlotByHouse(f[0].GetUInt64());
            if (!plot)
                continue;
            plot->Fixtures[f[1].GetUInt32()] = f[2].GetUInt32();
            ++fixtureCount;
        } while (memberFixtures->NextRow());
    }
    TC_LOG_DEBUG("housing", "Neighborhood::LoadFromDB: Loaded {} fixture overrides across neighborhood '{}'",
        fixtureCount, _name);

    // Load placed decor for every occupied plot's owner. SpawnPlotGameObjects
    // only spawns exterior entries (saved with room 0, so no RoomGuid here); interior entries are
    // also kept so visitors can see a neighbour's interior layout when they
    // enter the owner's interior map.
    uint32 decorCount = 0;
    if (memberDecor)
    {
        do
        {
            Field* d = memberDecor->Fetch();
            //   0      1            2            3     4     5     6     7     8     9       10     11        12        13       14        15      16            17           18
            // guid, houseGuid, houseDecorId, posX, posY, posZ, rotX, rotY, rotZ, rotW, scale, dyeSlot0, dyeSlot1, dyeSlot2, roomGuid, locked, placementTime, sourceType, sourceValue
            PlotInfo* plot = findPlotByHouse(d[1].GetUInt64());
            if (!plot)
                continue;

            Housing::PlacedDecor decor;
            // The piece's own saved GUID, built the same way as the house's own load builds it, so the pieces spawned
            // from this list answer to the GUIDs the client moves and removes.
            decor.Guid          = Housing::MakeDecorGuid(d[2].GetUInt32(), d[0].GetUInt64());
            decor.DecorEntryId  = d[2].GetUInt32();
            decor.PosX          = d[3].GetFloat();
            decor.PosY          = d[4].GetFloat();
            decor.PosZ          = d[5].GetFloat();
            decor.RotationX     = d[6].GetFloat();
            decor.RotationY     = d[7].GetFloat();
            decor.RotationZ     = d[8].GetFloat();
            decor.RotationW     = d[9].GetFloat();
            decor.Scale         = d[10].GetFloat();
            decor.DyeSlots[0]   = d[11].GetUInt32();
            decor.DyeSlots[1]   = d[12].GetUInt32();
            decor.DyeSlots[2]   = d[13].GetUInt32();
            if (uint64 roomCounter = d[14].GetUInt64())
                decor.RoomGuid  = ObjectGuidFactory::CreateHousing(/*subType*/ 2, /*realmId*/ 0, /*arg2*/ 0, roomCounter);
            decor.Locked        = d[15].GetUInt8() != 0;
            decor.PlacementTime = static_cast<time_t>(d[16].GetUInt64());
            decor.SourceType    = d[17].GetUInt8();
            decor.SourceValue   = d[18].GetString();
            plot->Decor.push_back(std::move(decor));
            ++decorCount;
        } while (memberDecor->NextRow());
    }
    TC_LOG_DEBUG("housing", "Neighborhood::LoadFromDB: Loaded {} placed decor items across neighborhood '{}'",
        decorCount, _name);

    // Load interior room layout per owner so HouseInteriorMap can spawn
    // neighbours' actual rooms (not the default base layout) when a visitor
    // enters their house, regardless of the owner being online.
    uint32 roomCount = 0;
    if (memberRooms)
    {
        do
        {
            Field* r = memberRooms->Fetch();
            //   0         1       2              3           4      5      6             7            8         9          10             11              12               13              14          15        16             17           18            19             20
            // houseGuid, id, houseRoomId, slotIndex, gridX, gridY, floorIndex, orientation, mirrored, themeId, wallTextureId, floorTextureId, ceilingTextureId, colorOverride, doorTypeId, doorSlot, ceilingTypeId, ceilingSlot, wallThemeId, floorThemeId, ceilingThemeId
            PlotInfo* plot = findPlotByHouse(r[0].GetUInt64());
            if (!plot)
                continue;

            Housing::Room room;
            room.Guid             = ObjectGuidFactory::CreateHousing(/*subType*/ 2, /*realmId*/ 0, /*arg2*/ 0, r[1].GetUInt64());
            room.RoomEntryId      = r[2].GetUInt32();
            room.SlotIndex        = r[3].GetUInt32();
            room.GridX            = r[4].GetInt32();
            room.GridY            = r[5].GetInt32();
            room.FloorIndex       = r[6].GetInt32();
            room.Orientation      = r[7].GetUInt8();
            room.Mirrored         = r[8].GetUInt8() != 0;
            room.ThemeId          = r[9].GetUInt32();
            room.WallTextureId    = r[10].GetUInt32();
            room.FloorTextureId   = r[11].GetUInt32();
            room.CeilingTextureId = r[12].GetUInt32();
            room.ColorOverride    = r[13].GetInt32();
            room.DoorTypeId       = r[14].GetUInt32();
            room.DoorSlot         = r[15].GetUInt8();
            room.CeilingTypeId    = r[16].GetUInt32();
            room.CeilingSlot      = r[17].GetUInt8();
            room.WallThemeId      = r[18].GetUInt32();
            room.FloorThemeId     = r[19].GetUInt32();
            room.CeilingThemeId   = r[20].GetUInt32();
            plot->Rooms.push_back(std::move(room));
            ++roomCount;
        } while (memberRooms->NextRow());
    }
    TC_LOG_DEBUG("housing", "Neighborhood::LoadFromDB: Loaded {} placed rooms across neighborhood '{}'",
        roomCount, _name);

    return true;
}

void Neighborhood::SaveToDB(CharacterDatabaseTransaction trans)
{
    // Update the main neighborhood row
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_NEIGHBORHOOD);
    uint8 index = 0;
    stmt->setUInt64(index++, _guid.GetCounter());
    stmt->setString(index++, _name);
    stmt->setUInt32(index++, _neighborhoodMapID);
    stmt->setUInt64(index++, _ownerGuid.GetCounter());
    stmt->setInt32(index++, _factionRestriction);
    stmt->setBool(index++, _isPublic);
    stmt->setUInt32(index++, _createTime);
    stmt->setUInt32(index++, _guildId); // M8
    trans->Append(stmt);

    // Delete all members and re-insert
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_MEMBERS);
    stmt->setUInt64(0, _guid.GetCounter());
    trans->Append(stmt);

    for (Member const& member : _members)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_MEMBER);
        index = 0;
        stmt->setUInt64(index++, _guid.GetCounter());
        stmt->setUInt64(index++, member.PlayerGuid.GetCounter());
        stmt->setUInt8(index++, member.Role);
        stmt->setUInt32(index++, member.JoinTime);
        stmt->setUInt8(index++, member.PlotIndex);
        trans->Append(stmt);
    }

    // Delete all invites and re-insert
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITES);
    stmt->setUInt64(0, _guid.GetCounter());
    trans->Append(stmt);

    for (PendingInvite const& invite : _pendingInvites)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_INVITE);
        index = 0;
        stmt->setUInt64(index++, _guid.GetCounter());
        stmt->setUInt64(index++, invite.InviteeGuid.GetCounter());
        stmt->setUInt64(index++, invite.InviterGuid.GetCounter());
        stmt->setUInt32(index++, invite.InviteTime);
        trans->Append(stmt);
    }

    TC_LOG_DEBUG("housing", "Neighborhood::SaveToDB: Saved neighborhood '{}' with {} members and {} invites",
        _name, _members.size(), _pendingInvites.size());
}

/*static*/ void Neighborhood::DeleteFromDB(ObjectGuid::LowType guid, CharacterDatabaseTransaction trans)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITES);
    stmt->setUInt64(0, guid);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_MEMBERS);
    stmt->setUInt64(0, guid);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD);
    stmt->setUInt64(0, guid);
    trans->Append(stmt);

    TC_LOG_DEBUG("housing", "Neighborhood::DeleteFromDB: Deleted neighborhood guid {}", guid);
}

void Neighborhood::SetName(std::string const& name)
{
    _name = name;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_NAME);
    stmt->setString(0, _name);
    stmt->setUInt64(1, _guid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "Neighborhood::SetName: Neighborhood {} renamed to '{}'",
        _guid.ToString(), _name);
}

void Neighborhood::SetPublic(bool isPublic)
{
    _isPublic = isPublic;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_PUBLIC);
    stmt->setBool(0, _isPublic);
    stmt->setUInt64(1, _guid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "Neighborhood::SetPublic: Neighborhood '{}' public status set to {}",
        _name, _isPublic);
}

HousingResult Neighborhood::AddManager(ObjectGuid playerGuid)
{
    // Count current managers
    uint32 managerCount = 0;
    Member* targetMember = nullptr;

    for (Member& member : _members)
    {
        if (member.Role == NEIGHBORHOOD_ROLE_MANAGER)
            ++managerCount;

        if (member.PlayerGuid == playerGuid)
            targetMember = &member;
    }

    if (!targetMember)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AddManager: Player {} is not a member of neighborhood '{}'",
            playerGuid.ToString(), _name);
        return HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND;
    }

    if (targetMember->Role == NEIGHBORHOOD_ROLE_OWNER)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AddManager: Player {} is already owner of neighborhood '{}'",
            playerGuid.ToString(), _name);
        return HOUSING_RESULT_PERMISSION_DENIED;
    }

    if (targetMember->Role == NEIGHBORHOOD_ROLE_MANAGER)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AddManager: Player {} is already a manager in neighborhood '{}'",
            playerGuid.ToString(), _name);
        return HOUSING_RESULT_SUCCESS;
    }

    if (managerCount >= MAX_NEIGHBORHOOD_MANAGERS)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AddManager: Neighborhood '{}' has reached max managers ({})",
            _name, MAX_NEIGHBORHOOD_MANAGERS);
        // Not PLOT_NOT_VACANT, which the client shows as an unrelated plot error.
        // HousingResult (build 68275) has no dedicated "too many managers"
        // value, so use PERMISSION_DENIED (the promotion is refused) until a
        // retail sniff confirms the exact enum for the manager-cap condition.
        return HOUSING_RESULT_PERMISSION_DENIED;
    }

    targetMember->Role = NEIGHBORHOOD_ROLE_MANAGER;

    // Persist role change to DB
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_ROLE);
    stmt->setUInt8(0, NEIGHBORHOOD_ROLE_MANAGER);
    stmt->setUInt64(1, _guid.GetCounter());
    stmt->setUInt64(2, playerGuid.GetCounter());
    CharacterDatabase.Execute(stmt);

    TC_LOG_DEBUG("housing", "Neighborhood::AddManager: Player {} promoted to manager in neighborhood '{}'",
        playerGuid.ToString(), _name);

    // The roster broadcast happens in the handler layer
    // (HandleNeighborhoodAddSecondaryOwner), which also refreshes mirror data.
    // Broadcasting here too produced two deltas per promote.
    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::RemoveManager(ObjectGuid playerGuid)
{
    for (Member& member : _members)
    {
        if (member.PlayerGuid == playerGuid)
        {
            if (member.Role == NEIGHBORHOOD_ROLE_OWNER)
            {
                TC_LOG_DEBUG("housing", "Neighborhood::RemoveManager: Cannot demote owner {} in neighborhood '{}'",
                    playerGuid.ToString(), _name);
                return HOUSING_RESULT_PERMISSION_DENIED;
            }

            if (member.Role != NEIGHBORHOOD_ROLE_MANAGER)
            {
                TC_LOG_DEBUG("housing", "Neighborhood::RemoveManager: Player {} is not a manager in neighborhood '{}'",
                    playerGuid.ToString(), _name);
                return HOUSING_RESULT_PERMISSION_DENIED;
            }

            member.Role = NEIGHBORHOOD_ROLE_RESIDENT;

            // Persist role change to DB
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_ROLE);
            stmt->setUInt8(0, NEIGHBORHOOD_ROLE_RESIDENT);
            stmt->setUInt64(1, _guid.GetCounter());
            stmt->setUInt64(2, playerGuid.GetCounter());
            CharacterDatabase.Execute(stmt);

            TC_LOG_DEBUG("housing", "Neighborhood::RemoveManager: Player {} demoted to resident in neighborhood '{}'",
                playerGuid.ToString(), _name);

            // The roster broadcast happens in the handler layer
            // (HandleNeighborhoodRemoveSecondaryOwner), which also refreshes
            // mirror data. Broadcasting here too produced two deltas per demote.
            return HOUSING_RESULT_SUCCESS;
        }
    }

    TC_LOG_DEBUG("housing", "Neighborhood::RemoveManager: Player {} is not a member of neighborhood '{}'",
        playerGuid.ToString(), _name);
    return HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND;
}

HousingResult Neighborhood::InviteResident(ObjectGuid inviterGuid, ObjectGuid inviteeGuid)
{
    // Check inviter is owner or manager
    bool inviterHasPermission = false;
    for (Member const& member : _members)
    {
        if (member.PlayerGuid == inviterGuid)
        {
            if (member.Role == NEIGHBORHOOD_ROLE_OWNER || member.Role == NEIGHBORHOOD_ROLE_MANAGER)
                inviterHasPermission = true;
            break;
        }
    }

    if (!inviterHasPermission)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Inviter {} lacks permission in neighborhood '{}'",
            inviterGuid.ToString(), _name);
        return HOUSING_RESULT_PERMISSION_DENIED;
    }

    // Check invite limit
    if (_pendingInvites.size() >= MAX_PENDING_INVITES)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Neighborhood '{}' has reached max pending invites ({})",
            _name, MAX_PENDING_INVITES);
        return HOUSING_RESULT_TOO_MANY_REQUESTS;
    }

    // Check if neighborhood has available plots (rough check: members + pending >= totalPlots)
    if (GetOccupiedPlotCount() + _pendingInvites.size() >= MAX_NEIGHBORHOOD_PLOTS)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Neighborhood '{}' has no available plots",
            _name);
        return HOUSING_RESULT_PLOT_NOT_VACANT;
    }

    // Check invitee is not already a member
    for (Member const& member : _members)
    {
        if (member.PlayerGuid == inviteeGuid)
        {
            TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Player {} is already a member of neighborhood '{}'",
                inviteeGuid.ToString(), _name);
            return HOUSING_RESULT_GENERIC_FAILURE;
        }
    }

    // Check invitee does not already have a pending invite
    for (PendingInvite const& invite : _pendingInvites)
    {
        if (invite.InviteeGuid == inviteeGuid)
        {
            TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Player {} already has a pending invite to neighborhood '{}'",
                inviteeGuid.ToString(), _name);
            return HOUSING_RESULT_GENERIC_FAILURE;
        }
    }

    // Only the server's public neighborhoods keep to one faction; a charter or guild neighborhood takes both, so an
    // invitation to one is not refused for her faction (the same rule as CheckResidentJoin).
    if (Player* invitee = ObjectAccessor::FindPlayer(inviteeGuid))
    {
        if (!IsFactionAllowed(IsServerPublic(), _factionRestriction, invitee->GetTeam()))
        {
            TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Player {} faction mismatch for neighborhood '{}'",
                inviteeGuid.ToString(), _name);
            return HOUSING_RESULT_INCORRECT_FACTION;
        }
    }

    // Honour the invitee's auto-decline-neighborhood-invites flag
    // (PLAYER_FLAGS_EX_AUTO_DECLINE_NEIGHBORHOOD). If the invitee is online
    // with the flag set, skip the invite entirely and tell the inviter it was
    // auto-declined (their filter rejected it). Offline invitees fall through
    // (the flag is only observable while online).
    if (Player* invitee = ObjectAccessor::FindPlayer(inviteeGuid))
    {
        if (invitee->HasPlayerFlagEx(PLAYER_FLAGS_EX_AUTO_DECLINE_NEIGHBORHOOD))
        {
            TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Player {} auto-declines neighborhood invites; invite to '{}' suppressed",
                inviteeGuid.ToString(), _name);
            return HOUSING_RESULT_FILTER_REJECTED;
        }
    }

    // Create the pending invite
    PendingInvite invite;
    invite.InviteeGuid  = inviteeGuid;
    invite.InviterGuid  = inviterGuid;
    invite.InviteTime   = static_cast<uint32>(GameTime::GetGameTime());
    _pendingInvites.push_back(invite);

    // Persist to DB immediately
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_INVITE);
    uint8 index = 0;
    stmt->setUInt64(index++, _guid.GetCounter());
    stmt->setUInt64(index++, inviteeGuid.GetCounter());
    stmt->setUInt64(index++, inviterGuid.GetCounter());
    stmt->setUInt32(index++, invite.InviteTime);
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("housing", "Neighborhood::InviteResident: Player {} invited to neighborhood '{}' by {}",
        inviteeGuid.ToString(), _name, inviterGuid.ToString());

    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::CancelInvitation(ObjectGuid inviteeGuid)
{
    auto it = std::find_if(_pendingInvites.begin(), _pendingInvites.end(),
        [&inviteeGuid](PendingInvite const& invite) { return invite.InviteeGuid == inviteeGuid; });

    if (it == _pendingInvites.end())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::CancelInvitation: No pending invite for {} in neighborhood '{}'",
            inviteeGuid.ToString(), _name);
        return HOUSING_RESULT_PLAYER_NOT_FOUND;
    }

    _pendingInvites.erase(it);

    // Remove from DB
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITE);
    stmt->setUInt64(0, _guid.GetCounter());
    stmt->setUInt64(1, inviteeGuid.GetCounter());
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("housing", "Neighborhood::CancelInvitation: Invitation for {} cancelled in neighborhood '{}'",
        inviteeGuid.ToString(), _name);

    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::AcceptInvitation(ObjectGuid playerGuid)
{
    auto it = std::find_if(_pendingInvites.begin(), _pendingInvites.end(),
        [&playerGuid](PendingInvite const& invite) { return invite.InviteeGuid == playerGuid; });

    if (it == _pendingInvites.end())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AcceptInvitation: No pending invite for {} in neighborhood '{}'",
            playerGuid.ToString(), _name);
        return HOUSING_RESULT_PLAYER_NOT_FOUND;
    }

    // Check neighborhood not full
    if (_members.size() >= MAX_NEIGHBORHOOD_PLOTS)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AcceptInvitation: Neighborhood '{}' is full ({} members)",
            _name, _members.size());
        return HOUSING_RESULT_PLOT_NOT_VACANT;
    }

    // Add as resident
    Member newMember;
    newMember.PlayerGuid    = playerGuid;
    newMember.Role          = NEIGHBORHOOD_ROLE_RESIDENT;
    newMember.JoinTime      = static_cast<uint32>(GameTime::GetGameTime());
    newMember.PlotIndex     = INVALID_PLOT_INDEX;
    _members.push_back(newMember);

    // Remove the invite
    _pendingInvites.erase(it);

    // Persist both changes to DB
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_MEMBER);
    uint8 index = 0;
    stmt->setUInt64(index++, _guid.GetCounter());
    stmt->setUInt64(index++, newMember.PlayerGuid.GetCounter());
    stmt->setUInt8(index++, newMember.Role);
    stmt->setUInt32(index++, newMember.JoinTime);
    stmt->setUInt8(index++, newMember.PlotIndex);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITE);
    stmt->setUInt64(0, _guid.GetCounter());
    stmt->setUInt64(1, playerGuid.GetCounter());
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("housing", "Neighborhood::AcceptInvitation: Player {} joined neighborhood '{}' as resident",
        playerGuid.ToString(), _name);

    // A new resident: every online member's bulletin board gets the whole roster again.
    BroadcastRoster();

    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::AddResident(ObjectGuid playerGuid)
{
    // Already a member?
    if (IsMember(playerGuid))
        return HOUSING_RESULT_SUCCESS;

    // Check neighborhood not full
    if (_members.size() >= MAX_NEIGHBORHOOD_PLOTS)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AddResident: Neighborhood '{}' is full ({} members)",
            _name, _members.size());
        return HOUSING_RESULT_PLOT_NOT_VACANT;
    }

    Member newMember;
    newMember.PlayerGuid    = playerGuid;
    newMember.Role          = NEIGHBORHOOD_ROLE_RESIDENT;
    newMember.JoinTime      = static_cast<uint32>(GameTime::GetGameTime());
    newMember.PlotIndex     = INVALID_PLOT_INDEX;
    _members.push_back(newMember);

    // Persist to DB
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_MEMBER);
    uint8 index = 0;
    stmt->setUInt64(index++, _guid.GetCounter());
    stmt->setUInt64(index++, newMember.PlayerGuid.GetCounter());
    stmt->setUInt8(index++, newMember.Role);
    stmt->setUInt32(index++, newMember.JoinTime);
    stmt->setUInt8(index++, newMember.PlotIndex);
    CharacterDatabase.Execute(stmt);

    // Clear any pending invite for this player now that they've joined
    auto inviteIt = std::find_if(_pendingInvites.begin(), _pendingInvites.end(),
        [&playerGuid](PendingInvite const& invite) { return invite.InviteeGuid == playerGuid; });
    if (inviteIt != _pendingInvites.end())
    {
        CharacterDatabasePreparedStatement* delStmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITE);
        delStmt->setUInt64(0, _guid.GetCounter());
        delStmt->setUInt64(1, playerGuid.GetCounter());
        CharacterDatabase.Execute(delStmt);
        _pendingInvites.erase(inviteIt);
    }

    TC_LOG_DEBUG("housing", "Neighborhood::AddResident: Player {} joined neighborhood '{}' as resident",
        playerGuid.ToString(), _name);

    // A new resident: every online member's bulletin board gets the whole roster again.
    BroadcastRoster();

    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::DeclineInvitation(ObjectGuid playerGuid)
{
    auto it = std::find_if(_pendingInvites.begin(), _pendingInvites.end(),
        [&playerGuid](PendingInvite const& invite) { return invite.InviteeGuid == playerGuid; });

    if (it == _pendingInvites.end())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::DeclineInvitation: No pending invite for {} in neighborhood '{}'",
            playerGuid.ToString(), _name);
        return HOUSING_RESULT_PLAYER_NOT_FOUND;
    }

    _pendingInvites.erase(it);

    // Remove from DB
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITE);
    stmt->setUInt64(0, _guid.GetCounter());
    stmt->setUInt64(1, playerGuid.GetCounter());
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("housing", "Neighborhood::DeclineInvitation: Player {} declined invite to neighborhood '{}'",
        playerGuid.ToString(), _name);

    // The invite was erased, so return SUCCESS. The response Result byte is a
    // HousingResult enum (uint8) the client compares against
    // Enum.HousingResult.Success(0); GENERIC_FAILURE would make the client show a
    // successful decline as failed (CancelInvitation returns SUCCESS too).
    return HOUSING_RESULT_SUCCESS;
}

/*static*/ bool Neighborhood::CanEvict(uint8 actorRole, uint8 targetRole)
{
    if (targetRole == NEIGHBORHOOD_ROLE_OWNER)
        return false;
    if (actorRole == NEIGHBORHOOD_ROLE_OWNER)
        return true;
    return actorRole == NEIGHBORHOOD_ROLE_MANAGER && targetRole == NEIGHBORHOOD_ROLE_RESIDENT;
}

HousingResult Neighborhood::CheckEviction(ObjectGuid actorGuid, ObjectGuid targetGuid) const
{
    auto roleOf = [this](ObjectGuid guid) -> uint8
    {
        if (!guid.IsEmpty() && guid == _ownerGuid)
            return NEIGHBORHOOD_ROLE_OWNER;
        Member const* member = GetMember(guid);
        return member ? member->Role : uint8(NEIGHBORHOOD_ROLE_RESIDENT);
    };

    if (!CanEvict(roleOf(actorGuid), roleOf(targetGuid)))
    {
        TC_LOG_DEBUG("housing", "Neighborhood::CheckEviction: {} may not evict {} from neighborhood '{}'",
            actorGuid.ToString(), targetGuid.ToString(), _name);
        return HOUSING_RESULT_PERMISSION_DENIED;
    }

    return HOUSING_RESULT_SUCCESS;
}

void Neighborhood::EvictPlayer(ObjectGuid playerGuid, CharacterDatabaseTransaction trans)
{
    auto it = std::find_if(_members.begin(), _members.end(),
        [&playerGuid](Member const& member) { return member.PlayerGuid == playerGuid; });
    if (it == _members.end() || it->Role == NEIGHBORHOOD_ROLE_OWNER)
        return;

    // A character who still holds a plot keeps her roster entry: without it that plot's house would have nobody on the
    // roster. The caller has already freed the evicted plot, so this is a different one.
    if (it->PlotIndex != INVALID_PLOT_INDEX)
    {
        TC_LOG_ERROR("housing", "Neighborhood::EvictPlayer: {} still holds plot {} of neighborhood '{}'; her roster entry stays",
            playerGuid.ToString(), it->PlotIndex, _name);
        return;
    }

    _members.erase(it);

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_MEMBER);
    stmt->setUInt64(0, _guid.GetCounter());
    stmt->setUInt64(1, playerGuid.GetCounter());
    trans->Append(stmt);

    TC_LOG_DEBUG("housing", "Neighborhood::EvictPlayer: Player {} evicted from neighborhood '{}'",
        playerGuid.ToString(), _name);

    // The evicted player is gone from the roster: every remaining member gets it again.
    BroadcastRoster();
}

bool Neighborhood::RemoveDeletedCharacter(ObjectGuid characterGuid, CharacterDatabaseTransaction trans)
{
    bool rosterChanged = false;

    auto memberIt = std::find_if(_members.begin(), _members.end(),
        [&characterGuid](Member const& member) { return member.PlayerGuid == characterGuid; });
    if (memberIt != _members.end() && memberIt->Role != NEIGHBORHOOD_ROLE_OWNER && characterGuid != _ownerGuid)
    {
        if (memberIt->PlotIndex != INVALID_PLOT_INDEX)
            TC_LOG_ERROR("housing", "Neighborhood::RemoveDeletedCharacter: deleted {} still held plot {} of neighborhood '{}'; the plot keeps its house without a roster entry",
                characterGuid.ToString(), memberIt->PlotIndex, _name);

        _members.erase(memberIt);

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_MEMBER);
        stmt->setUInt64(0, _guid.GetCounter());
        stmt->setUInt64(1, characterGuid.GetCounter());
        trans->Append(stmt);
        rosterChanged = true;
    }

    auto inviteIt = std::find_if(_pendingInvites.begin(), _pendingInvites.end(),
        [&characterGuid](PendingInvite const& invite) { return invite.InviteeGuid == characterGuid; });
    if (inviteIt != _pendingInvites.end())
    {
        _pendingInvites.erase(inviteIt);

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITE);
        stmt->setUInt64(0, _guid.GetCounter());
        stmt->setUInt64(1, characterGuid.GetCounter());
        trans->Append(stmt);
    }

    _plotReservations.erase(characterGuid);

    if (_pendingTransfer && _pendingTransfer->TargetGuid == characterGuid)
        _pendingTransfer.reset();

    if (rosterChanged)
        BroadcastRoster();

    return rosterChanged;
}

ObjectGuid Neighborhood::ReleasePlotByHouse(ObjectGuid houseGuid, CharacterDatabaseTransaction trans)
{
    if (houseGuid.IsEmpty())
        return ObjectGuid::Empty;

    uint8 plotIndex = INVALID_PLOT_INDEX;
    for (uint8 i = 0; i < MAX_NEIGHBORHOOD_PLOTS; ++i)
    {
        if (_plots[i].IsOccupied() && _plots[i].HouseGuid == houseGuid)
        {
            plotIndex = i;
            break;
        }
    }

    if (plotIndex == INVALID_PLOT_INDEX)
        return ObjectGuid::Empty;

    _plots[plotIndex] = PlotInfo{};

    // The roster entry that bought the plot may be any character of the house's account, not the one acting.
    ObjectGuid holderGuid;
    auto it = std::find_if(_members.begin(), _members.end(),
        [plotIndex](Member const& member) { return member.PlotIndex == plotIndex; });
    if (it != _members.end())
    {
        holderGuid = it->PlayerGuid;
        if (it->Role == NEIGHBORHOOD_ROLE_OWNER)
        {
            it->PlotIndex = INVALID_PLOT_INDEX;

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_PLOT);
            stmt->setUInt8(0, INVALID_PLOT_INDEX);
            stmt->setUInt64(1, _guid.GetCounter());
            stmt->setUInt64(2, holderGuid.GetCounter());
            trans->Append(stmt);
        }
        else
        {
            _members.erase(it);

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_MEMBER);
            stmt->setUInt64(0, _guid.GetCounter());
            stmt->setUInt64(1, holderGuid.GetCounter());
            trans->Append(stmt);
        }
    }

    TC_LOG_DEBUG("housing", "Neighborhood::ReleasePlotByHouse: plot {} of house {} is free in neighborhood '{}' (roster entry {})",
        plotIndex, houseGuid.ToString(), _name, holderGuid.ToString());

    // The plot's holder left the roster, or lost the plot: every online member's bulletin board needs it again.
    BroadcastRoster();

    return holderGuid;
}

ObjectGuid Neighborhood::GetPlotHolder(uint8 plotIndex) const
{
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
        return ObjectGuid::Empty;

    auto it = std::find_if(_members.begin(), _members.end(),
        [plotIndex](Member const& member) { return member.PlotIndex == plotIndex; });
    return it != _members.end() ? it->PlayerGuid : ObjectGuid::Empty;
}

void Neighborhood::MovePlotHolder(uint8 plotIndex, ObjectGuid newHolderGuid, CharacterDatabaseTransaction trans)
{
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS || newHolderGuid.IsEmpty())
        return;

    auto oldIt = std::find_if(_members.begin(), _members.end(),
        [plotIndex](Member const& member) { return member.PlotIndex == plotIndex; });
    if (oldIt == _members.end() || oldIt->PlayerGuid == newHolderGuid)
        return;

    auto newIt = std::find_if(_members.begin(), _members.end(),
        [&newHolderGuid](Member const& member) { return member.PlayerGuid == newHolderGuid; });
    if (newIt != _members.end() && newIt->PlotIndex != INVALID_PLOT_INDEX)
    {
        // A character holds one plot per neighborhood, and an account has one house per district, so this does not
        // happen; the roster is left as it is rather than taking her other plot from her.
        TC_LOG_ERROR("housing", "Neighborhood::MovePlotHolder: {} already holds plot {} in neighborhood '{}', so plot {} stays with {}",
            newHolderGuid.ToString(), newIt->PlotIndex, _name, plotIndex, oldIt->PlayerGuid.ToString());
        return;
    }

    ObjectGuid const oldHolderGuid = oldIt->PlayerGuid;
    uint32 const joinTime = oldIt->JoinTime;

    // The old entry first, while its iterator is still good.
    if (oldIt->Role == NEIGHBORHOOD_ROLE_OWNER)
    {
        oldIt->PlotIndex = INVALID_PLOT_INDEX;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_PLOT);
        stmt->setUInt8(0, INVALID_PLOT_INDEX);
        stmt->setUInt64(1, _guid.GetCounter());
        stmt->setUInt64(2, oldHolderGuid.GetCounter());
        trans->Append(stmt);
    }
    else
    {
        _members.erase(oldIt);

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_MEMBER);
        stmt->setUInt64(0, _guid.GetCounter());
        stmt->setUInt64(1, oldHolderGuid.GetCounter());
        trans->Append(stmt);
    }

    newIt = std::find_if(_members.begin(), _members.end(),
        [&newHolderGuid](Member const& member) { return member.PlayerGuid == newHolderGuid; });
    if (newIt != _members.end())
    {
        newIt->PlotIndex = plotIndex;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_PLOT);
        stmt->setUInt8(0, plotIndex);
        stmt->setUInt64(1, _guid.GetCounter());
        stmt->setUInt64(2, newHolderGuid.GetCounter());
        trans->Append(stmt);
    }
    else
    {
        Member& newMember = _members.emplace_back();
        newMember.PlayerGuid = newHolderGuid;
        newMember.Role = NEIGHBORHOOD_ROLE_RESIDENT;
        newMember.JoinTime = joinTime;
        newMember.PlotIndex = plotIndex;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_MEMBER);
        uint8 index = 0;
        stmt->setUInt64(index++, _guid.GetCounter());
        stmt->setUInt64(index++, newHolderGuid.GetCounter());
        stmt->setUInt8(index++, newMember.Role);
        stmt->setUInt32(index++, newMember.JoinTime);
        stmt->setUInt8(index++, newMember.PlotIndex);
        trans->Append(stmt);

        // Joining uses up an invite she had to this neighborhood.
        auto inviteIt = std::find_if(_pendingInvites.begin(), _pendingInvites.end(),
            [&newHolderGuid](PendingInvite const& invite) { return invite.InviteeGuid == newHolderGuid; });
        if (inviteIt != _pendingInvites.end())
        {
            _pendingInvites.erase(inviteIt);

            stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITE);
            stmt->setUInt64(0, _guid.GetCounter());
            stmt->setUInt64(1, newHolderGuid.GetCounter());
            trans->Append(stmt);
        }
    }

    TC_LOG_DEBUG("housing", "Neighborhood::MovePlotHolder: plot {} of neighborhood '{}' passed from {} to {}",
        plotIndex, _name, oldHolderGuid.ToString(), newHolderGuid.ToString());

    BroadcastRoster();
}

HousingResult Neighborhood::TransferOwnership(ObjectGuid newOwnerGuid)
{
    Member* oldOwner = nullptr;
    Member* newOwner = nullptr;

    for (Member& member : _members)
    {
        if (member.PlayerGuid == _ownerGuid)
            oldOwner = &member;
        if (member.PlayerGuid == newOwnerGuid)
            newOwner = &member;
    }

    if (!newOwner)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::TransferOwnership: New owner {} is not a member of neighborhood '{}'",
            newOwnerGuid.ToString(), _name);
        return HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND;
    }

    if (!oldOwner)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::TransferOwnership: Current owner {} not found in member list of neighborhood '{}'",
            _ownerGuid.ToString(), _name);
        return HOUSING_RESULT_RPC_FAILURE;
    }

    // Promote new owner, demote old owner to manager
    newOwner->Role = NEIGHBORHOOD_ROLE_OWNER;
    oldOwner->Role = NEIGHBORHOOD_ROLE_MANAGER;
    ObjectGuid previousOwnerGuid = _ownerGuid;
    _ownerGuid = newOwnerGuid;

    // Persist the ownership change to the database
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // Update the neighborhood's ownerGuid
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_OWNER);
    stmt->setUInt64(0, newOwnerGuid.GetCounter());
    stmt->setUInt64(1, _guid.GetCounter());
    trans->Append(stmt);

    // Update the old owner's role to manager
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_ROLE);
    stmt->setUInt8(0, NEIGHBORHOOD_ROLE_MANAGER);
    stmt->setUInt64(1, _guid.GetCounter());
    stmt->setUInt64(2, previousOwnerGuid.GetCounter());
    trans->Append(stmt);

    // Update the new owner's role to owner
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_ROLE);
    stmt->setUInt8(0, NEIGHBORHOOD_ROLE_OWNER);
    stmt->setUInt64(1, _guid.GetCounter());
    stmt->setUInt64(2, newOwnerGuid.GetCounter());
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("housing", "Neighborhood::TransferOwnership: Ownership of neighborhood '{}' transferred from {} to {}",
        _name, previousOwnerGuid.ToString(), newOwnerGuid.ToString());

    // Both resident types changed.
    BroadcastMemberStatus(previousOwnerGuid);
    BroadcastMemberStatus(newOwnerGuid);

    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::OfferOwnership(ObjectGuid targetGuid)
{
    if (_pendingTransfer.has_value())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::OfferOwnership: Ownership transfer already pending in neighborhood '{}'",
            _name);
        return HOUSING_RESULT_GENERIC_FAILURE;
    }

    if (!IsMember(targetGuid))
    {
        TC_LOG_DEBUG("housing", "Neighborhood::OfferOwnership: Target {} is not a member of neighborhood '{}'",
            targetGuid.ToString(), _name);
        return HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND;
    }

    PendingOwnershipTransfer transfer;
    transfer.TargetGuid = targetGuid;
    transfer.OfferTime = static_cast<uint32>(GameTime::GetGameTime());
    _pendingTransfer = transfer;

    TC_LOG_DEBUG("housing", "Neighborhood::OfferOwnership: Ownership offered to {} in neighborhood '{}'",
        targetGuid.ToString(), _name);

    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::AcceptOwnershipTransfer(ObjectGuid acceptorGuid)
{
    if (!_pendingTransfer.has_value())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AcceptOwnershipTransfer: No pending transfer in neighborhood '{}'",
            _name);
        return HOUSING_RESULT_NO_NEIGHBORHOOD_OWNERSHIP_REQUESTS;
    }

    if (_pendingTransfer->TargetGuid != acceptorGuid)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AcceptOwnershipTransfer: Player {} is not the transfer target in neighborhood '{}'",
            acceptorGuid.ToString(), _name);
        return HOUSING_RESULT_PERMISSION_DENIED;
    }

    // Check timeout (5 minutes)
    uint32 now = static_cast<uint32>(GameTime::GetGameTime());
    if (now - _pendingTransfer->OfferTime > 300)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::AcceptOwnershipTransfer: Transfer expired in neighborhood '{}'",
            _name);
        _pendingTransfer.reset();
        return HOUSING_RESULT_TIMEOUT_LIMIT;
    }

    _pendingTransfer.reset();
    return TransferOwnership(acceptorGuid);
}

HousingResult Neighborhood::RejectOwnershipTransfer(ObjectGuid rejectorGuid)
{
    if (!_pendingTransfer.has_value())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::RejectOwnershipTransfer: No pending transfer in neighborhood '{}'",
            _name);
        return HOUSING_RESULT_NO_NEIGHBORHOOD_OWNERSHIP_REQUESTS;
    }

    if (_pendingTransfer->TargetGuid != rejectorGuid)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::RejectOwnershipTransfer: Player {} is not the transfer target in neighborhood '{}'",
            rejectorGuid.ToString(), _name);
        return HOUSING_RESULT_PERMISSION_DENIED;
    }

    _pendingTransfer.reset();

    TC_LOG_DEBUG("housing", "Neighborhood::RejectOwnershipTransfer: Transfer rejected by {} in neighborhood '{}'",
        rejectorGuid.ToString(), _name);

    return HOUSING_RESULT_SUCCESS;
}

HousingResult Neighborhood::TryReservePlot(ObjectGuid playerGuid, uint8 plotIndex, bool joinAsResident, PlotClaim& claim)
{
    claim = PlotClaim{};

    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::TryReservePlot: Invalid plot index {} in neighborhood '{}'",
            plotIndex, _name);
        return HOUSING_RESULT_PLOT_NOT_FOUND;
    }

    // Another player's House Finder hold keeps the plot for them until it runs out.
    if (!GetPlotReserverOther(plotIndex, playerGuid).IsEmpty())
        return HOUSING_RESULT_PLOT_RESERVED;

    if (_plots[plotIndex].IsOccupied())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::TryReservePlot: Plot {} is already occupied in neighborhood '{}' (house {})",
            plotIndex, _name, _plots[plotIndex].HouseGuid.ToString());
        return HOUSING_RESULT_PLOT_NOT_VACANT;
    }

    Member* buyer = nullptr;
    for (Member& member : _members)
    {
        if (member.PlayerGuid == playerGuid)
        {
            buyer = &member;
            break;
        }
    }

    if (buyer && buyer->PlotIndex != INVALID_PLOT_INDEX)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::TryReservePlot: Player {} already holds plot {} in neighborhood '{}'; requested plot {}",
            playerGuid.ToString(), buyer->PlotIndex, _name, plotIndex);
        return HOUSING_RESULT_PLOT_NOT_VACANT;
    }

    if (!buyer)
    {
        if (!joinAsResident)
        {
            TC_LOG_DEBUG("housing", "Neighborhood::TryReservePlot: Player {} is not a member of neighborhood '{}'",
                playerGuid.ToString(), _name);
            return HOUSING_RESULT_NEIGHBORHOOD_NOT_FOUND;
        }

        if (_members.size() >= MAX_NEIGHBORHOOD_PLOTS)
        {
            TC_LOG_DEBUG("housing", "Neighborhood::TryReservePlot: Neighborhood '{}' is full ({} members)",
                _name, _members.size());
            return HOUSING_RESULT_PLOT_NOT_VACANT;
        }

        Member& newMember = _members.emplace_back();
        newMember.PlayerGuid = playerGuid;
        newMember.Role = NEIGHBORHOOD_ROLE_RESIDENT;
        newMember.JoinTime = static_cast<uint32>(GameTime::GetGameTime());
        buyer = &newMember;
        claim.NewMember = true;
        claim.JoinTime = newMember.JoinTime;
    }

    buyer->PlotIndex = plotIndex;
    _plots[plotIndex].PlotIndex = plotIndex;
    _plots[plotIndex].OwnerGuid = playerGuid;

    claim.PlayerGuid = playerGuid;
    claim.PlotIndex = plotIndex;
    return HOUSING_RESULT_SUCCESS;
}

void Neighborhood::AppendPlotClaim(PlotClaim const& claim, CharacterDatabaseTransaction trans) const
{
    if (claim.NewMember)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_NEIGHBORHOOD_MEMBER);
        uint8 index = 0;
        stmt->setUInt64(index++, _guid.GetCounter());
        stmt->setUInt64(index++, claim.PlayerGuid.GetCounter());
        stmt->setUInt8(index++, NEIGHBORHOOD_ROLE_RESIDENT);
        stmt->setUInt32(index++, claim.JoinTime);
        stmt->setUInt8(index++, claim.PlotIndex);
        trans->Append(stmt);

        // Joining uses up the buyer's invite to this neighborhood, if she had one.
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NEIGHBORHOOD_INVITE);
        stmt->setUInt64(0, _guid.GetCounter());
        stmt->setUInt64(1, claim.PlayerGuid.GetCounter());
        trans->Append(stmt);
        return;
    }

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_PLOT);
    stmt->setUInt8(0, claim.PlotIndex);
    stmt->setUInt64(1, _guid.GetCounter());
    stmt->setUInt64(2, claim.PlayerGuid.GetCounter());
    trans->Append(stmt);
}

void Neighborhood::UndoPlotClaim(PlotClaim const& claim)
{
    if (claim.PlotIndex >= MAX_NEIGHBORHOOD_PLOTS)
        return;

    _plots[claim.PlotIndex] = PlotInfo{};

    auto it = std::find_if(_members.begin(), _members.end(),
        [&claim](Member const& member) { return member.PlayerGuid == claim.PlayerGuid; });
    if (it == _members.end())
        return;

    if (claim.NewMember)
        _members.erase(it);
    else
        it->PlotIndex = INVALID_PLOT_INDEX;
}

void Neighborhood::CompletePlotClaim(PlotClaim const& claim)
{
    if (!claim.NewMember)
        return;

    auto inviteIt = std::find_if(_pendingInvites.begin(), _pendingInvites.end(),
        [&claim](PendingInvite const& invite) { return invite.InviteeGuid == claim.PlayerGuid; });
    if (inviteIt != _pendingInvites.end())
        _pendingInvites.erase(inviteIt);

    TC_LOG_DEBUG("housing", "Neighborhood::CompletePlotClaim: Player {} joined neighborhood '{}' as resident with plot {}",
        claim.PlayerGuid.ToString(), _name, claim.PlotIndex);
}

void Neighborhood::UpdatePlotHouseInfo(uint8 plotIndex, ObjectGuid houseGuid, ObjectGuid ownerBnetGuid, uint64 houseDatabaseId /*= 0*/)
{
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS || !_plots[plotIndex].IsOccupied())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::UpdatePlotHouseInfo: Plot {} not found in neighborhood '{}'",
            plotIndex, _name);
        return;
    }

    // A plot that already names a house is only refreshed for that house. A character can still hold a house that
    // no longer stands here (evicted, or moved away), and its plot index must not hand this plot to her account.
    if (_plots[plotIndex].HouseDatabaseId && _plots[plotIndex].HouseDatabaseId != houseDatabaseId)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::UpdatePlotHouseInfo: Plot {} in neighborhood '{}' holds house {}, not house {} ({})",
            plotIndex, _name, _plots[plotIndex].HouseDatabaseId, houseDatabaseId, houseGuid.ToString());
        return;
    }

    _plots[plotIndex].HouseGuid = houseGuid;
    _plots[plotIndex].OwnerBnetGuid = ownerBnetGuid;
    if (houseDatabaseId)
        _plots[plotIndex].HouseDatabaseId = houseDatabaseId;

    TC_LOG_DEBUG("housing", "Neighborhood::UpdatePlotHouseInfo: Plot {} updated with HouseGuid {} and BnetGuid {} in neighborhood '{}'",
        plotIndex, houseGuid.ToString(), ownerBnetGuid.ToString(), _name);
}

void Neighborhood::UpdatePlotHouseMirror(Housing const& housing)
{
    uint8 plotIndex = housing.GetPlotIndex();
    if (housing.GetNeighborhoodGuid() != _guid || plotIndex >= MAX_NEIGHBORHOOD_PLOTS || !_plots[plotIndex].IsOccupied()
        || _plots[plotIndex].HouseGuid != housing.GetHouseGuid())
        return;

    PlotInfo& plot = _plots[plotIndex];
    plot.OwnerGuid = housing.GetCosmeticOwnerGuid();
    plot.HouseLevel = static_cast<uint8>(std::max<uint32>(1, housing.GetLevel()));
    plot.HouseFavor = housing.GetFavor64();
    plot.HouseName = housing.GetHouseName();
    plot.HouseType = housing.GetHouseType();
    plot.HouseSettingsFlags = housing.GetSettingsFlags();
    plot.Fixtures = housing.GetFixtureOverrideMap();
    plot.HasHousePlacement = housing.HasCustomPosition();
    plot.HousePlacement = housing.GetHousePosition();

    plot.Rooms.clear();
    for (Housing::Room const* room : housing.GetRooms())
        plot.Rooms.push_back(*room);

    plot.Decor.clear();
    for (Housing::PlacedDecor const* decor : housing.GetAllPlacedDecor())
        plot.Decor.push_back(*decor);
}

void Neighborhood::UpdatePlotSettingsFlagsByHouse(ObjectGuid houseGuid, uint32 settingsFlags)
{
    for (PlotInfo& plot : _plots)
    {
        if (plot.IsOccupied() && !houseGuid.IsEmpty() && plot.HouseGuid == houseGuid)
        {
            plot.HouseSettingsFlags = settingsFlags;
            TC_LOG_DEBUG("housing", "Neighborhood::UpdatePlotSettingsFlagsByHouse: plot {} house {} settings=0x{:X} in '{}'",
                plot.PlotIndex, houseGuid.ToString(), settingsFlags, _name);
            return;
        }
    }
}

void Neighborhood::UpdatePlotCosmeticOwnerByHouse(ObjectGuid houseGuid, ObjectGuid cosmeticOwnerGuid)
{
    for (PlotInfo& plot : _plots)
    {
        if (plot.IsOccupied() && !houseGuid.IsEmpty() && plot.HouseGuid == houseGuid)
        {
            plot.OwnerGuid = cosmeticOwnerGuid;
            return;
        }
    }
}

void Neighborhood::UpdatePlotHousePlacementByHouse(ObjectGuid houseGuid, Position const& placement)
{
    for (PlotInfo& plot : _plots)
    {
        if (plot.IsOccupied() && !houseGuid.IsEmpty() && plot.HouseGuid == houseGuid)
        {
            plot.HasHousePlacement = true;
            plot.HousePlacement = placement;
            return;
        }
    }
}

uint32 Neighborhood::GetHouseSettingsFlags(PlotInfo const& plot) const
{
    if (Player* shownOwner = ObjectAccessor::FindPlayer(plot.OwnerGuid))
        if (Housing const* housing = shownOwner->GetHousingByGuid(plot.HouseGuid))
            return housing->GetSettingsFlags();

    // Housing::SaveSettings keeps the plot's copy current, so it is right when no character of the account is online.
    return plot.HouseSettingsFlags;
}

/*static*/ bool Neighborhood::IsFactionAllowed(bool serverPublic, int32 factionRestriction, uint32 team)
{
    if (!serverPublic)
        return true;

    return !((factionRestriction == NEIGHBORHOOD_FACTION_HORDE && team != HORDE)
        || (factionRestriction == NEIGHBORHOOD_FACTION_ALLIANCE && team != ALLIANCE));
}

/*static*/ HousingResult Neighborhood::CheckResidentJoin(bool serverPublic, bool isPublic, int32 factionRestriction,
    uint32 neighborhoodGuildId, uint32 team, uint32 playerGuildId, bool invitedOrRunsIt)
{
    if (serverPublic)
        return IsFactionAllowed(serverPublic, factionRestriction, team) ? HOUSING_RESULT_SUCCESS : HOUSING_RESULT_INCORRECT_FACTION;

    // The guild roster decides who lives in a guild neighborhood ("For guilds, this is via the Guild roster", Blizzard's
    // preview https://worldofwarcraft.blizzard.com/en-us/news/24221516).
    if (neighborhoodGuildId)
        return playerGuildId == neighborhoodGuildId ? HOUSING_RESULT_SUCCESS : HOUSING_RESULT_INVALID_GUILD;

    if (!isPublic && !invitedOrRunsIt)
        return HOUSING_RESULT_MISSING_PRIVATE_NEIGHBORHOOD_INVITE;

    return HOUSING_RESULT_SUCCESS;
}

Neighborhood::HouseEntry Neighborhood::CheckHouseEntry(Player const* player, uint8 plotIndex, bool interior) const
{
    HouseEntry entry;
    PlotInfo const* plot = GetPlotInfo(plotIndex);
    if (!player || !plot || plot->HouseGuid.IsEmpty())
        return entry;

    entry.HouseGuid = plot->HouseGuid;
    entry.IsOwner = player->GetSession() && plot->IsOwnedByAccount(player->GetSession()->GetBattlenetAccountGUID());
    if (Housing const* ownHousing = player->GetHousingByGuid(plot->HouseGuid))
        entry.SettingsFlags = ownHousing->GetSettingsFlags();
    else
        entry.SettingsFlags = GetHouseSettingsFlags(*plot);

    entry.Allowed = entry.IsOwner || sHousingMgr.CanVisitorAccessPlot(player, plot->OwnerBnetGuid, plot->OwnerGuid, this, entry.SettingsFlags, interior);
    return entry;
}

Neighborhood::PlotInfo const* Neighborhood::GetPlotInfoByHouse(ObjectGuid houseGuid) const
{
    if (houseGuid.IsEmpty())
        return nullptr;

    for (PlotInfo const& plot : _plots)
        if (plot.IsOccupied() && plot.HouseGuid == houseGuid)
            return &plot;

    return nullptr;
}

HousingResult Neighborhood::MoveHouse(ObjectGuid houseGuid, ObjectGuid moverGuid, uint8 newPlotIndex, CharacterDatabaseTransaction trans)
{
    if (newPlotIndex >= MAX_NEIGHBORHOOD_PLOTS)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::MoveHouse: Invalid target plot index {} in neighborhood '{}'",
            newPlotIndex, _name);
        return HOUSING_RESULT_PLOT_NOT_FOUND;
    }

    // Another player's House Finder hold keeps the plot for them until it runs out.
    if (!GetPlotReserverOther(newPlotIndex, moverGuid).IsEmpty())
        return HOUSING_RESULT_PLOT_RESERVED;

    // Check destination is not occupied
    if (_plots[newPlotIndex].IsOccupied())
    {
        TC_LOG_DEBUG("housing", "Neighborhood::MoveHouse: Target plot {} is occupied in neighborhood '{}'",
            newPlotIndex, _name);
        return HOUSING_RESULT_PLOT_NOT_VACANT;
    }

    // The source plot is the one the house stands on, whichever character of its account asks.
    uint8 oldPlotIndex = INVALID_PLOT_INDEX;
    for (uint8 i = 0; i < MAX_NEIGHBORHOOD_PLOTS; ++i)
    {
        if (_plots[i].IsOccupied() && !houseGuid.IsEmpty() && _plots[i].HouseGuid == houseGuid)
        {
            oldPlotIndex = i;
            break;
        }
    }

    if (oldPlotIndex == INVALID_PLOT_INDEX)
    {
        TC_LOG_DEBUG("housing", "Neighborhood::MoveHouse: House {} stands on no plot in neighborhood '{}'",
            houseGuid.ToString(), _name);
        return HOUSING_RESULT_PLOT_NOT_FOUND;
    }

    // Move plot data: copy to new slot, clear old slot
    _plots[newPlotIndex] = _plots[oldPlotIndex];
    _plots[newPlotIndex].PlotIndex = newPlotIndex;
    _plots[oldPlotIndex] = PlotInfo{};

    // The roster entry that held the old plot holds the new one.
    for (Member& member : _members)
    {
        if (member.PlotIndex == oldPlotIndex)
        {
            member.PlotIndex = newPlotIndex;

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NEIGHBORHOOD_MEMBER_PLOT);
            stmt->setUInt8(0, newPlotIndex);
            stmt->setUInt64(1, _guid.GetCounter());
            stmt->setUInt64(2, member.PlayerGuid.GetCounter());
            trans->Append(stmt);
            break;
        }
    }

    TC_LOG_DEBUG("housing", "Neighborhood::MoveHouse: house {} moved from plot {} to plot {} in neighborhood '{}' by {}",
        houseGuid.ToString(), oldPlotIndex, newPlotIndex, _name, moverGuid.ToString());

    return HOUSING_RESULT_SUCCESS;
}

uint32 Neighborhood::GetOccupiedPlotCount() const
{
    uint32 count = 0;
    for (auto const& plot : _plots)
        if (plot.IsOccupied())
            ++count;
    return count;
}

void Neighborhood::SetPlotAreaTriggerGuid(uint8 plotIndex, ObjectGuid atGuid)
{
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
        return;

    _plots[plotIndex].PlotGuid = atGuid;
}

Neighborhood::Member const* Neighborhood::GetMember(ObjectGuid playerGuid) const
{
    for (Member const& member : _members)
        if (member.PlayerGuid == playerGuid)
            return &member;

    return nullptr;
}

bool Neighborhood::IsMember(ObjectGuid playerGuid) const
{
    return GetMember(playerGuid) != nullptr;
}

bool Neighborhood::IsManager(ObjectGuid playerGuid) const
{
    Member const* member = GetMember(playerGuid);
    return member && (member->Role == NEIGHBORHOOD_ROLE_MANAGER || member->Role == NEIGHBORHOOD_ROLE_OWNER);
}

bool Neighborhood::IsOwner(ObjectGuid playerGuid) const
{
    return _ownerGuid == playerGuid;
}

bool Neighborhood::HasPendingInvite(ObjectGuid playerGuid) const
{
    return std::any_of(_pendingInvites.begin(), _pendingInvites.end(),
        [&playerGuid](PendingInvite const& invite) { return invite.InviteeGuid == playerGuid; });
}

void Neighborhood::BroadcastPacket(WorldPacket const* packet, ObjectGuid excludeGuid /*= ObjectGuid::Empty*/) const
{
    for (auto const& member : _members)
    {
        if (member.PlayerGuid == excludeGuid)
            continue;
        if (Player* player = ObjectAccessor::FindPlayer(member.PlayerGuid))
            player->SendDirectMessage(packet);
    }
}

void Neighborhood::FillPlotHouseEntry(PlotInfo const& plot, WorldPackets::Housing::JamCliHouse& house) const
{
    house.HouseGUID = plot.HouseGuid;
    house.CosmeticOwnerGUID = plot.OwnerGuid;
    house.NeighborhoodGUID = GetGuid();
    house.PlotID = plot.PlotIndex;
    house.HouseSettingFlags = GetHouseSettingsFlags(plot);
}

void Neighborhood::BuildRosterResponse(WorldPackets::Neighborhood::NeighborhoodGetRosterResponse& response) const
{
    response.Result = static_cast<uint8>(HOUSING_RESULT_SUCCESS);
    response.GroupNeighborhoodGuid = GetGuid();
    response.GroupOwnerGuid = GetOwnerGuid();
    response.NeighborhoodName = GetName();
    response.Members.reserve(_members.size());
    for (Member const& member : _members)
    {
        WorldPackets::Neighborhood::NeighborhoodGetRosterResponse::RosterMemberData& data = response.Members.emplace_back();
        data.PlayerGuid = member.PlayerGuid;
        data.PlotIndex = member.PlotIndex;
        data.JoinTime = member.JoinTime;
        data.ResidentType = member.Role;
        data.IsOnline = ObjectAccessor::FindPlayer(member.PlayerGuid) != nullptr;
        // PlotInfo mirrors character_housing, so offline residents' houses are listed too.
        if (member.PlotIndex != INVALID_PLOT_INDEX)
        {
            if (PlotInfo const* plotInfo = GetPlotInfo(member.PlotIndex))
            {
                data.HouseGuid = plotInfo->HouseGuid;
                data.HouseCosmeticOwnerGuid = plotInfo->OwnerGuid;
                data.HouseSettingFlags = GetHouseSettingsFlags(*plotInfo);
            }
        }
    }
}

void Neighborhood::BroadcastRoster(ObjectGuid excludeGuid /*= ObjectGuid::Empty*/) const
{
    WorldPackets::Neighborhood::NeighborhoodGetRosterResponse response;
    BuildRosterResponse(response);
    BroadcastPacket(response.Write(), excludeGuid);
}

void Neighborhood::BroadcastMemberStatus(ObjectGuid playerGuid, bool isOnline) const
{
    Member const* member = GetMember(playerGuid);
    if (!member)
        return;

    WorldPackets::Neighborhood::NeighborhoodRosterResidentUpdate update;
    update.Residents.push_back({ playerGuid, member->Role, isOnline });
    BroadcastPacket(update.Write());
}

void Neighborhood::BroadcastMemberStatus(ObjectGuid playerGuid) const
{
    BroadcastMemberStatus(playerGuid, ObjectAccessor::FindPlayer(playerGuid) != nullptr);
}

void Neighborhood::RefreshMirrorDataForOnlineMembers() const
{
    for (auto const& member : _members)
    {
        Player* player = ObjectAccessor::FindPlayer(member.PlayerGuid);
        if (!player || !player->GetSession())
            continue;

        // FNeighborhoodMirrorData_C belongs on the Housing/4 entity, NOT the BNetAccount entity.
        HousingNeighborhoodMirrorEntity& mirrorEntity = player->GetSession()->GetHousingNeighborhoodMirrorEntity();

        // Name + Owner
        mirrorEntity.SetName(_name);
        mirrorEntity.SetOwnerGUID(_ownerGuid);

        // Houses — rebuild from plots. Add ALL 55 entries so Houses[i] = PlotIndex i.
        // The client uses the array index as the plot identifier; skipping empty slots
        // causes the client to show the wrong plots as occupied.
        mirrorEntity.ClearHouses();
        for (auto const& plot : _plots)
        {
            if (plot.IsOccupied() && !plot.HouseGuid.IsEmpty())
                mirrorEntity.AddHouse(plot.HouseGuid, plot.OwnerGuid);
            else
                mirrorEntity.AddHouse(ObjectGuid::Empty, ObjectGuid::Empty);
        }

        // Managers
        mirrorEntity.ClearManagers();
        for (auto const& m : _members)
        {
            if (m.Role == NEIGHBORHOOD_ROLE_MANAGER || m.Role == NEIGHBORHOOD_ROLE_OWNER)
            {
                ObjectGuid bnetGuid;
                if (Player* mgr = ObjectAccessor::FindPlayer(m.PlayerGuid))
                    bnetGuid = mgr->GetSession()->GetBattlenetAccountGUID();
                mirrorEntity.AddManager(bnetGuid, m.PlayerGuid);
            }
        }

        // Push the rebuilt fields to the client. Set/Add methods only flip dirty
        // bits on the in-memory entity; without an explicit Send the client keeps
        // the previous state and the in-world neighborhood map stays stale until
        // an unrelated update arrives (e.g. opening the roster UI). Re-sending as
        // CREATE matches retail behaviour for a wholesale Houses/Managers replace
        // — incremental UPDATE_OBJECT also works but the client's map-icon refresh
        // path only re-runs on CREATE.
        mirrorEntity.SendCreateToPlayer(player);
    }
}

// --- Plot Reservation System ---

bool Neighborhood::ReservePlot(ObjectGuid playerGuid, uint8 plotIndex)
{
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
        return false;

    // Plot must be unoccupied
    if (_plots[plotIndex].IsOccupied())
        return false;

    // Reservations expire after 5 minutes (retail behavior). Sweep stale entries
    // before checking, so an old reservation never blocks a new player forever.
    constexpr uint32 RESERVATION_EXPIRY_SECONDS = 5 * MINUTE;
    uint32 now = static_cast<uint32>(GameTime::GetGameTime());
    for (auto it = _plotReservations.begin(); it != _plotReservations.end(); )
    {
        if (now >= it->second.ReserveTime + RESERVATION_EXPIRY_SECONDS)
        {
            TC_LOG_DEBUG("housing", "Neighborhood::ReservePlot: expired reservation by {} on plot {} cleared",
                it->first.ToString(), it->second.PlotIndex);
            it = _plotReservations.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Check if someone else already reserved this plot
    for (auto const& [guid, res] : _plotReservations)
    {
        if (res.PlotIndex == plotIndex && guid != playerGuid)
            return false;
    }

    PlotReservation& reservation = _plotReservations[playerGuid];
    reservation.PlotIndex = plotIndex;
    reservation.ReserveTime = now;

    TC_LOG_DEBUG("housing",
        "Neighborhood::ReservePlot: Player {} reserved plot {} in neighborhood '{}' (guid {}, expires in {}s)",
        playerGuid.ToString(), plotIndex, _name, _guid.ToString(), RESERVATION_EXPIRY_SECONDS);
    return true;
}

bool Neighborhood::ClearReservation(ObjectGuid playerGuid)
{
    auto it = _plotReservations.find(playerGuid);
    if (it == _plotReservations.end())
        return false;

    TC_LOG_DEBUG("housing", "Neighborhood::ClearReservation: Player {} cleared reservation for plot {} in neighborhood {}",
        playerGuid.ToString(), it->second.PlotIndex, _guid.ToString());
    _plotReservations.erase(it);
    return true;
}

bool Neighborhood::HasReservation(ObjectGuid playerGuid) const
{
    return _plotReservations.find(playerGuid) != _plotReservations.end();
}

uint8 Neighborhood::GetReservedPlot(ObjectGuid playerGuid) const
{
    auto it = _plotReservations.find(playerGuid);
    if (it != _plotReservations.end())
        return it->second.PlotIndex;
    return INVALID_PLOT_INDEX;
}

ObjectGuid Neighborhood::GetPlotReserverOther(uint8 plotIndex, ObjectGuid viewerGuid)
{
    if (plotIndex >= MAX_NEIGHBORHOOD_PLOTS)
        return ObjectGuid::Empty;

    constexpr uint32 RESERVATION_EXPIRY_SECONDS = 5 * MINUTE;
    uint32 now = static_cast<uint32>(GameTime::GetGameTime());

    // Sweep stale entries first so a long-expired reservation doesn't paint
    // a plot as "reserved" forever in the cornerstone UI.
    for (auto it = _plotReservations.begin(); it != _plotReservations.end(); )
    {
        if (now >= it->second.ReserveTime + RESERVATION_EXPIRY_SECONDS)
            it = _plotReservations.erase(it);
        else
            ++it;
    }

    for (auto const& [guid, res] : _plotReservations)
        if (res.PlotIndex == plotIndex && guid != viewerGuid)
            return guid;

    return ObjectGuid::Empty;
}

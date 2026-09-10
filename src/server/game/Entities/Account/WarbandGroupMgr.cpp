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

#include "WarbandGroupMgr.h"
#include "CharacterPackets.h"
#include "CollectionMgr.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "WorldSession.h"
#include <algorithm>

char const* GetReplaceGroupsResultName(ReplaceGroupsResult result)
{
    switch (result)
    {
        case ReplaceGroupsResult::Ok: return "Ok";
        case ReplaceGroupsResult::TooManyGroups: return "TooManyGroups";
        case ReplaceGroupsResult::DuplicateGroupId: return "DuplicateGroupId";
        case ReplaceGroupsResult::TooManyMembers: return "TooManyMembers";
        case ReplaceGroupsResult::UnownedWarbandScene: return "UnownedWarbandScene";
        case ReplaceGroupsResult::InvalidName: return "InvalidName";
        case ReplaceGroupsResult::OrderIndexOutOfRange: return "OrderIndexOutOfRange";
        case ReplaceGroupsResult::DuplicateOrderIndex: return "DuplicateOrderIndex";
        case ReplaceGroupsResult::InvalidMemberGuid: return "InvalidMemberGuid";
        case ReplaceGroupsResult::DuplicateMemberGuid: return "DuplicateMemberGuid";
        default: return "Unknown";
    }
}

WarbandGroupMgr::WarbandGroupMgr(WorldSession* owner) : _owner(owner)
{
}

void WarbandGroupMgr::LoadAccountGroups(PreparedQueryResult groupsResult, PreparedQueryResult membersResult)
{
    _groups.clear();

    if (groupsResult)
    {
        do
        {
            Field* fields = groupsResult->Fetch();
            StoredGroup& group = _groups.emplace_back();
            group.GroupID = fields[0].GetUInt64();
            group.OrderIndex = fields[1].GetUInt8();
            group.WarbandSceneID = fields[2].GetUInt32();
            group.Flags = fields[3].GetUInt32();
            group.ContentSetID = fields[4].GetInt32();
            group.Name = fields[5].GetString();
        } while (groupsResult->NextRow());
    }

    if (membersResult)
    {
        do
        {
            Field* fields = membersResult->Fetch();
            uint64 groupId = fields[0].GetUInt64();
            StoredGroup* group = nullptr;
            for (StoredGroup& storedGroup : _groups)
            {
                if (storedGroup.GroupID == groupId)
                {
                    group = &storedGroup;
                    break;
                }
            }

            if (!group)
                continue;

            StoredMember& member = group->Members.emplace_back();
            member.Guid = ObjectGuid::Create<HighGuid::Player>(fields[1].GetUInt64());
            member.WarbandScenePlacementID = fields[2].GetUInt32();
            member.Type = fields[3].GetInt32();
            member.ContentSetID = fields[4].GetInt32();
        } while (membersResult->NextRow());
    }
}

uint32 WarbandGroupMgr::GetDefaultWarbandSceneId()
{
    uint32 firstId = 0;
    for (WarbandSceneEntry const* warbandScene : sWarbandSceneStore)
    {
        if (!firstId)
            firstId = warbandScene->ID;

        if (warbandScene->GetFlags().HasFlag(WarbandSceneFlags::IsDefault))
            return warbandScene->ID;
    }

    return firstId;
}

std::vector<uint32> WarbandGroupMgr::GetDefaultPlacementIdsForScene(uint32 warbandSceneId, uint32 memberCount)
{
    std::vector<uint32> result;
    std::vector<WarbandScenePlacementEntry const*> const* placements = sDB2Manager.GetWarbandScenePlacementsForScene(warbandSceneId);
    if (!placements)
        return result;

    for (WarbandScenePlacementEntry const* placement : *placements)
    {
        if (placement->SlotType != 0)
            continue;

        if (result.size() >= memberCount)
            break;

        result.push_back(placement->ID);
    }

    return result;
}

void WarbandGroupMgr::EnsureDefaultGroup(std::span<ObjectGuid const> accountCharacterGuids)
{
    if (!_groups.empty())
        return;

    StoredGroup& group = _groups.emplace_back();
    group.GroupID = 1;
    group.OrderIndex = 0;
    group.WarbandSceneID = GetDefaultWarbandSceneId();
    group.Flags = 0;
    group.ContentSetID = 0;
    group.Name = "Favorites";

    uint32 memberCount = std::min<uint32>(uint32(accountCharacterGuids.size()), MaxWarbandGroupMembers);
    std::vector<uint32> placementIds = GetDefaultPlacementIdsForScene(group.WarbandSceneID, memberCount);
    for (uint32 i = 0; i < memberCount; ++i)
    {
        StoredMember& member = group.Members.emplace_back();
        member.Type = 0;
        member.ContentSetID = 0;
        member.Guid = accountCharacterGuids[i];
        member.WarbandScenePlacementID = i < placementIds.size() ? placementIds[i] : 0;
    }

    SaveToDB();
}

ReplaceGroupsResult WarbandGroupMgr::ReplaceGroups(std::vector<WorldPackets::Character::SetupWarbandGroup> const& groups, CollectionMgr const& collectionMgr, std::unordered_set<ObjectGuid> const& validCharacterGuids)
{
    if (groups.size() > MaxWarbandGroups)
    {
        TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} sent {} groups (max {})", _owner->GetBattlenetAccountId(), groups.size(), MaxWarbandGroups);
        return ReplaceGroupsResult::TooManyGroups;
    }

    std::unordered_set<uint64> ownedGroupIds;
    std::unordered_set<uint64> usedGroupIds;
    for (StoredGroup const& storedGroup : _groups)
    {
        ownedGroupIds.insert(storedGroup.GroupID);
        usedGroupIds.insert(storedGroup.GroupID);
    }

    std::unordered_set<uint64> keptOwnedGroupIds;
    std::unordered_set<uint8> seenOrderIndexes;
    std::unordered_set<ObjectGuid> seenMemberGuids;

    std::vector<StoredGroup> newGroups;
    newGroups.reserve(groups.size());

    for (WorldPackets::Character::SetupWarbandGroup const& packetGroup : groups)
    {
        if (packetGroup.Members.size() > MaxWarbandGroupMembers)
        {
            TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} group {} has {} members (max {})", _owner->GetBattlenetAccountId(), packetGroup.GroupID, packetGroup.Members.size(), MaxWarbandGroupMembers);
            return ReplaceGroupsResult::TooManyMembers;
        }

        if (!collectionMgr.HasWarbandScene(packetGroup.WarbandSceneID))
        {
            TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} group {} uses unowned warband scene {}", _owner->GetBattlenetAccountId(), packetGroup.GroupID, packetGroup.WarbandSceneID);
            return ReplaceGroupsResult::UnownedWarbandScene;
        }

        if (packetGroup.Name.empty() || packetGroup.Name.size() > 255)
        {
            TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} group {} has invalid name length {}", _owner->GetBattlenetAccountId(), packetGroup.GroupID, packetGroup.Name.size());
            return ReplaceGroupsResult::InvalidName;
        }

        if (packetGroup.OrderIndex >= MaxWarbandGroups)
        {
            TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} group {} orderIndex {} out of range", _owner->GetBattlenetAccountId(), packetGroup.GroupID, packetGroup.OrderIndex);
            return ReplaceGroupsResult::OrderIndexOutOfRange;
        }

        if (!seenOrderIndexes.insert(packetGroup.OrderIndex).second)
        {
            TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} duplicate orderIndex {}", _owner->GetBattlenetAccountId(), packetGroup.OrderIndex);
            return ReplaceGroupsResult::DuplicateOrderIndex;
        }

        uint64 groupId;
        if (packetGroup.GroupID != 0 && ownedGroupIds.contains(packetGroup.GroupID))
        {
            if (!keptOwnedGroupIds.insert(packetGroup.GroupID).second)
            {
                TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} duplicate groupId {}", _owner->GetBattlenetAccountId(), packetGroup.GroupID);
                return ReplaceGroupsResult::DuplicateGroupId;
            }

            groupId = packetGroup.GroupID;
        }
        else
        {
            groupId = 1;
            while (usedGroupIds.contains(groupId))
                ++groupId;
            usedGroupIds.insert(groupId);
        }

        StoredGroup& group = newGroups.emplace_back();
        group.GroupID = groupId;
        group.OrderIndex = packetGroup.OrderIndex;
        group.WarbandSceneID = packetGroup.WarbandSceneID;
        group.Flags = packetGroup.Flags;
        group.ContentSetID = packetGroup.ContentSetID;
        group.Name = packetGroup.Name;

        for (WorldPackets::Character::WarbandGroupMember const& packetMember : packetGroup.Members)
        {
            if (packetMember.Type == 0)
            {
                if (!packetMember.Guid)
                    continue;

                if (!validCharacterGuids.contains(packetMember.Guid))
                {
                    TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} rejected guid {} for group {}", _owner->GetBattlenetAccountId(), packetMember.Guid.ToString(), packetGroup.GroupID);
                    return ReplaceGroupsResult::InvalidMemberGuid;
                }

                if (!seenMemberGuids.insert(packetMember.Guid).second)
                {
                    TC_LOG_DEBUG("network", "WarbandGroupMgr::ReplaceGroups: account {} duplicate member guid {} in setup", _owner->GetBattlenetAccountId(), packetMember.Guid.ToString());
                    return ReplaceGroupsResult::DuplicateMemberGuid;
                }
            }

            StoredMember& member = group.Members.emplace_back();
            member.WarbandScenePlacementID = packetMember.WarbandScenePlacementID;
            member.Type = packetMember.Type;
            member.ContentSetID = packetMember.ContentSetID;
            member.Guid = packetMember.Guid;
        }
    }

    _groups = std::move(newGroups);
    SaveToDB();
    return ReplaceGroupsResult::Ok;
}

bool WarbandGroupMgr::RemoveMember(ObjectGuid guid)
{
    if (!guid)
        return false;

    bool removed = false;
    for (StoredGroup& group : _groups)
    {
        auto memberItr = group.Members.begin();
        while (memberItr != group.Members.end())
        {
            if (memberItr->Type == 0 && memberItr->Guid == guid)
            {
                memberItr = group.Members.erase(memberItr);
                removed = true;
            }
            else
                ++memberItr;
        }
    }

    if (removed)
        SaveToDB();

    return removed;
}

bool WarbandGroupMgr::PruneInvalidMembers(std::unordered_set<ObjectGuid> const& validCharacterGuids)
{
    bool changed = false;
    for (StoredGroup& group : _groups)
    {
        auto memberItr = group.Members.begin();
        while (memberItr != group.Members.end())
        {
            if (memberItr->Type == 0 && !memberItr->Guid.IsEmpty() && !validCharacterGuids.contains(memberItr->Guid))
            {
                memberItr = group.Members.erase(memberItr);
                changed = true;
            }
            else
                ++memberItr;
        }
    }

    if (changed)
        SaveToDB();

    return changed;
}

std::vector<WorldPackets::Character::WarbandGroup> WarbandGroupMgr::BuildEnumGroups() const
{
    std::vector<WorldPackets::Character::WarbandGroup> result;
    result.reserve(_groups.size());

    for (StoredGroup const& storedGroup : _groups)
    {
        WorldPackets::Character::WarbandGroup& group = result.emplace_back();
        group.GroupID = storedGroup.GroupID;
        group.OrderIndex = storedGroup.OrderIndex;
        group.WarbandSceneID = storedGroup.WarbandSceneID;
        group.Flags = storedGroup.Flags;
        group.ContentSetID = storedGroup.ContentSetID;
        group.Name = storedGroup.Name;
        group.Members.reserve(storedGroup.Members.size());

        for (StoredMember const& storedMember : storedGroup.Members)
        {
            WorldPackets::Character::WarbandGroupMember& member = group.Members.emplace_back();
            member.WarbandScenePlacementID = storedMember.WarbandScenePlacementID;
            member.Type = storedMember.Type;
            member.ContentSetID = storedMember.ContentSetID;
            member.Guid = storedMember.Guid;
        }
    }

    return result;
}

void WarbandGroupMgr::SaveToDB()
{
    // Battle.net account tables all foreign-key on battlenetAccountId. Nothing to save when that id is 0.
    if (!_owner->GetBattlenetAccountId())
        return;

    LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_BNET_WARBAND_GROUP_MEMBERS);
    stmt->setUInt32(0, _owner->GetBattlenetAccountId());
    trans->Append(stmt);

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_BNET_WARBAND_GROUPS);
    stmt->setUInt32(0, _owner->GetBattlenetAccountId());
    trans->Append(stmt);

    for (StoredGroup const& group : _groups)
    {
        stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_BNET_WARBAND_GROUP);
        stmt->setUInt32(0, _owner->GetBattlenetAccountId());
        stmt->setUInt64(1, group.GroupID);
        stmt->setUInt8(2, group.OrderIndex);
        stmt->setUInt32(3, group.WarbandSceneID);
        stmt->setUInt32(4, group.Flags);
        stmt->setInt32(5, group.ContentSetID);
        stmt->setString(6, group.Name);
        trans->Append(stmt);

        for (StoredMember const& member : group.Members)
        {
            if (member.Type == 0 && member.Guid.IsEmpty())
                continue;

            stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_BNET_WARBAND_GROUP_MEMBER);
            stmt->setUInt32(0, _owner->GetBattlenetAccountId());
            stmt->setUInt64(1, group.GroupID);
            stmt->setUInt64(2, member.Guid.GetCounter());
            stmt->setUInt32(3, member.WarbandScenePlacementID);
            stmt->setInt32(4, member.Type);
            stmt->setInt32(5, member.ContentSetID);
            trans->Append(stmt);
        }
    }

    LoginDatabase.CommitTransaction(trans);
}

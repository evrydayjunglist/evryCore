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

#include "HousingDecorStore.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Log.h"
#include "StringFormat.h"
#include <algorithm>

std::mutex HousingDecorStore::s_registryLock;
std::unordered_map<uint32, std::weak_ptr<HousingDecorStore>> HousingDecorStore::s_stores;

std::shared_ptr<HousingDecorStore> HousingDecorStore::Acquire(uint32 bnetAccountId)
{
    std::lock_guard<std::mutex> registryGuard(s_registryLock);

    std::weak_ptr<HousingDecorStore>& shared = s_stores[bnetAccountId];
    if (std::shared_ptr<HousingDecorStore> store = shared.lock())
        return store;

    // The last holder to let go erases the account's entry, unless a new store took its place meanwhile.
    std::shared_ptr<HousingDecorStore> store(new HousingDecorStore(bnetAccountId), [](HousingDecorStore* released)
    {
        {
            std::lock_guard<std::mutex> guard(s_registryLock);
            auto itr = s_stores.find(released->GetBnetAccountId());
            if (itr != s_stores.end() && itr->second.expired())
                s_stores.erase(itr);
        }
        delete released;
    });
    shared = store;
    return store;
}

bool HousingDecorStore::IsLoaded() const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    return _loaded;
}

void HousingDecorStore::LoadFromDB(std::vector<Field*> const& storedDecor, PreparedQueryResult entries, PreparedQueryResult catalogFetch,
    PreparedQueryResult firstHouse)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (_loaded)
        return;

    for (Field* fields : storedDecor)
    {
        Housing::PlacedDecor decor = ReadDecorRow(fields);
        // A piece whose house is gone is in storage now; the next save writes it so. The house is the row's last
        // column, 0 for a piece already in storage.
        if (fields[20].GetUInt64())
            _unsaved = true;
        decor.PosX = decor.PosY = decor.PosZ = 0.0f;
        decor.RotationX = decor.RotationY = decor.RotationZ = 0.0f;
        decor.RotationW = 1.0f;
        decor.Scale = 1.0f;
        decor.RoomGuid.Clear();
        decor.Locked = false;
        decor.PetGuid.Clear();
        decor.PetFlag = 0;
        _stored[decor.Guid] = std::move(decor);
    }

    //           0             1          2
    // SELECT houseDecorId, redeemed, firstOwnedTime FROM account_housing_decor_entry WHERE bnetAccountId = ?
    if (entries)
    {
        do
        {
            Field* fields = entries->Fetch();
            OwnedEntry& entry = _owned[fields[0].GetUInt32()];
            entry.Redeemed = fields[1].GetUInt32();
            entry.FirstOwnedTime = fields[2].GetUInt64();
        } while (entries->NextRow());
    }

    // A piece loaded without its entry row (written by an older save) still counts as owned.
    for (auto const& [guid, decor] : _stored)
        if (_owned.try_emplace(decor.DecorEntryId).second)
            _unsaved = true;

    if (catalogFetch)
        _lastCatalogFetch = (*catalogFetch)[0].GetUInt64();

    // SELECT purchaseTime FROM account_housing_first_house WHERE bnetAccountId = ?
    if (firstHouse)
        _firstHouseTime = std::max<uint64>((*firstHouse)[0].GetUInt64(), 1);

    _loaded = true;

    TC_LOG_DEBUG("housing", "HousingDecorStore::LoadFromDB: Battle.net account {} has {} decor in storage and has owned {} decor entries",
        _bnetAccountId, uint32(_stored.size()), uint32(_owned.size()));
}

void HousingDecorStore::SaveToDB(CharacterDatabaseTransaction trans)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (!_loaded || !_unsaved)
        return;

    _unsaved = false;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ACCOUNT_HOUSING_DECOR_STORED);
    stmt->setUInt32(0, _bnetAccountId);
    trans->Append(stmt);

    for (auto const& [guid, decor] : _stored)
        AppendDecorRow(trans, _bnetAccountId, 0, decor);

    for (auto const& [decorEntryId, entry] : _owned)
        AppendEntryRow(trans, decorEntryId, entry);
}

Housing::PlacedDecor HousingDecorStore::CreateStored(uint32 decorEntryId, uint8 sourceType, std::string sourceValue, bool& firstOwned,
    CharacterDatabaseTransaction trans)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    Housing::PlacedDecor decor;
    decor.Guid = Housing::NewDecorGuid(decorEntryId);
    decor.DecorEntryId = decorEntryId;
    decor.SourceType = sourceType;
    decor.SourceValue = std::move(sourceValue);

    firstOwned = MarkOwned(decorEntryId, trans);
    AppendDecorRow(trans, _bnetAccountId, 0, decor);
    _stored[decor.Guid] = decor;
    return decor;
}

bool HousingDecorStore::MarkOwned(uint32 decorEntryId, CharacterDatabaseTransaction trans)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    auto [itr, inserted] = _owned.try_emplace(decorEntryId);
    if (!inserted)
        return false;

    itr->second.FirstOwnedTime = uint64(GameTime::GetGameTime());
    AppendEntryRow(trans, decorEntryId, itr->second);
    return true;
}

Housing::PlacedDecor const* HousingDecorStore::FindStored(ObjectGuid decorGuid) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _stored.find(decorGuid);
    return itr != _stored.end() ? &itr->second : nullptr;
}

Optional<Housing::PlacedDecor> HousingDecorStore::TakeStored(ObjectGuid decorGuid)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _stored.find(decorGuid);
    if (itr == _stored.end())
        return {};

    Housing::PlacedDecor decor = std::move(itr->second);
    _stored.erase(itr);
    return decor;
}

void HousingDecorStore::PutInStorage(Housing::PlacedDecor decor, CharacterDatabaseTransaction trans)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    decor.PosX = decor.PosY = decor.PosZ = 0.0f;
    decor.RotationX = decor.RotationY = decor.RotationZ = 0.0f;
    decor.RotationW = 1.0f;
    decor.Scale = 1.0f;
    decor.RoomGuid.Clear();
    decor.Locked = false;
    decor.PlacementTime = 0;
    decor.PetGuid.Clear();
    decor.PetFlag = 0;

    auto [entry, inserted] = _owned.try_emplace(decor.DecorEntryId);
    if (inserted)
    {
        entry->second.FirstOwnedTime = uint64(GameTime::GetGameTime());
        AppendEntryRow(trans, decor.DecorEntryId, entry->second);
    }
    AppendDecorRow(trans, _bnetAccountId, 0, decor);
    ObjectGuid const guid = decor.Guid;
    _stored[guid] = std::move(decor);
}

bool HousingDecorStore::DestroyStored(ObjectGuid decorGuid, CharacterDatabaseTransaction trans)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (!_stored.erase(decorGuid))
        return false;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ACCOUNT_HOUSING_DECOR);
    stmt->setUInt64(0, decorGuid.GetCounter());
    trans->Append(stmt);
    return true;
}

std::vector<Housing::PlacedDecor> HousingDecorStore::GetStored() const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    std::vector<Housing::PlacedDecor> stored;
    stored.reserve(_stored.size());
    for (auto const& [guid, decor] : _stored)
        stored.push_back(decor);
    return stored;
}

uint32 HousingDecorStore::CountStored(uint32 decorEntryId) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    return uint32(std::count_if(_stored.begin(), _stored.end(),
        [decorEntryId](std::pair<ObjectGuid const, Housing::PlacedDecor> const& stored) { return stored.second.DecorEntryId == decorEntryId; }));
}

ObjectGuid HousingDecorStore::FindStoredOfEntry(uint32 decorEntryId) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    for (auto const& [guid, decor] : _stored)
        if (decor.DecorEntryId == decorEntryId)
            return guid;
    return ObjectGuid::Empty;
}

bool HousingDecorStore::HasOwned(uint32 decorEntryId) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    return _owned.contains(decorEntryId);
}

uint32 HousingDecorStore::CountOwnedEntries(std::function<bool(uint32)> const& filter) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    uint32 count = 0;
    for (auto const& [decorEntryId, entry] : _owned)
        if (!filter || filter(decorEntryId))
            ++count;
    return count;
}

uint32 HousingDecorStore::GetRedeemed(uint32 decorEntryId) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _owned.find(decorEntryId);
    return itr != _owned.end() ? itr->second.Redeemed : 0;
}

void HousingDecorStore::AddRedeemed(uint32 decorEntryId, CharacterDatabaseTransaction trans)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto [itr, inserted] = _owned.try_emplace(decorEntryId);
    if (inserted)
        itr->second.FirstOwnedTime = uint64(GameTime::GetGameTime());
    ++itr->second.Redeemed;
    AppendEntryRow(trans, decorEntryId, itr->second);
}

uint64 HousingDecorStore::GetLastCatalogFetch() const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    return _lastCatalogFetch;
}

uint64 HousingDecorStore::ExchangeLastCatalogFetch(uint64 now)
{
    uint64 previous = 0;
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        previous = _lastCatalogFetch;
        _lastCatalogFetch = now;
    }

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_ACCOUNT_HOUSING_CATALOG_FETCH);
    stmt->setUInt32(0, _bnetAccountId);
    stmt->setUInt64(1, now);
    CharacterDatabase.Execute(stmt);
    return previous;
}

bool HousingDecorStore::HasHadFirstHouse() const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    return _firstHouseTime != 0;
}

void HousingDecorStore::RecordFirstHouse(CharacterDatabaseTransaction trans)
{
    uint64 now = 0;
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (_firstHouseTime)
            return;
        now = std::max<uint64>(GameTime::GetGameTime(), 1);
        _firstHouseTime = now;
    }

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ACCOUNT_HOUSING_FIRST_HOUSE);
    stmt->setUInt32(0, _bnetAccountId);
    stmt->setUInt64(1, now);
    trans->Append(stmt);
}

void HousingDecorStore::AppendDecorRow(CharacterDatabaseTransaction trans, uint32 bnetAccountId, uint64 houseDatabaseId,
    Housing::PlacedDecor const& decor)
{
    // guid, bnetAccountId, houseDecorId, sourceType, sourceValue, houseGuid, posX, posY, posZ, rotX, rotY, rotZ, rotW,
    // scale, dyeSlot0, dyeSlot1, dyeSlot2, roomGuid, locked, placementTime, petGuid, petFlag
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_ACCOUNT_HOUSING_DECOR);
    uint8 index = 0;
    stmt->setUInt64(index++, decor.Guid.GetCounter());
    stmt->setUInt32(index++, bnetAccountId);
    stmt->setUInt32(index++, decor.DecorEntryId);
    stmt->setUInt8(index++, decor.SourceType);
    stmt->setString(index++, decor.SourceValue);
    stmt->setUInt64(index++, houseDatabaseId);
    stmt->setFloat(index++, decor.PosX);
    stmt->setFloat(index++, decor.PosY);
    stmt->setFloat(index++, decor.PosZ);
    stmt->setFloat(index++, decor.RotationX);
    stmt->setFloat(index++, decor.RotationY);
    stmt->setFloat(index++, decor.RotationZ);
    stmt->setFloat(index++, decor.RotationW);
    stmt->setFloat(index++, decor.Scale);
    stmt->setUInt32(index++, decor.DyeSlots[0]);
    stmt->setUInt32(index++, decor.DyeSlots[1]);
    stmt->setUInt32(index++, decor.DyeSlots[2]);
    stmt->setUInt64(index++, decor.RoomGuid.IsEmpty() ? 0 : decor.RoomGuid.GetCounter());
    stmt->setUInt8(index++, decor.Locked ? 1 : 0);
    stmt->setUInt64(index++, uint64(decor.PlacementTime));
    stmt->setUInt64(index++, decor.PetGuid.IsEmpty() ? 0 : decor.PetGuid.GetCounter());
    stmt->setUInt8(index++, decor.PetFlag);
    trans->Append(stmt);
}

Housing::PlacedDecor HousingDecorStore::ReadDecorRow(Field* fields)
{
    //          0        1             2     3     4     5     6     7     8     9       10        11        12
    // SELECT guid, houseDecorId, posX, posY, posZ, rotX, rotY, rotZ, rotW, scale, dyeSlot0, dyeSlot1, dyeSlot2,
    //          13        14       15             16          17           18       19       20
    //        roomGuid, locked, placementTime, sourceType, sourceValue, petGuid, petFlag, houseGuid
    // FROM account_housing_decor WHERE bnetAccountId = ?
    Housing::PlacedDecor decor;
    uint64 const low = fields[0].GetUInt64();
    decor.DecorEntryId = fields[1].GetUInt32();
    decor.Guid = Housing::MakeDecorGuid(decor.DecorEntryId, low);
    Housing::NoteDecorDbId(low);
    decor.PosX = fields[2].GetFloat();
    decor.PosY = fields[3].GetFloat();
    decor.PosZ = fields[4].GetFloat();
    decor.RotationX = fields[5].GetFloat();
    decor.RotationY = fields[6].GetFloat();
    decor.RotationZ = fields[7].GetFloat();
    decor.RotationW = fields[8].GetFloat();
    decor.Scale = fields[9].GetFloat();
    if (decor.Scale < 0.01f)
        decor.Scale = 1.0f;
    decor.DyeSlots[0] = fields[10].GetUInt32();
    decor.DyeSlots[1] = fields[11].GetUInt32();
    decor.DyeSlots[2] = fields[12].GetUInt32();
    decor.Locked = fields[14].GetUInt8() != 0;
    decor.PlacementTime = time_t(fields[15].GetUInt64());
    decor.SourceType = fields[16].GetUInt8();
    decor.SourceValue = fields[17].GetString();
    if (uint64 petCounter = fields[18].GetUInt64())
        decor.PetGuid = ObjectGuid::Create<HighGuid::BattlePet>(petCounter);
    decor.PetFlag = fields[19].GetUInt8();
    return decor;
}

void HousingDecorStore::AppendEntryRow(CharacterDatabaseTransaction trans, uint32 decorEntryId, OwnedEntry const& entry) const
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_ACCOUNT_HOUSING_DECOR_ENTRY);
    stmt->setUInt32(0, _bnetAccountId);
    stmt->setUInt32(1, decorEntryId);
    stmt->setUInt32(2, entry.Redeemed);
    stmt->setUInt64(3, entry.FirstOwnedTime);
    trans->Append(stmt);
}

uint32 HousingDecorStore::GetOwedCount(int32 startingQuantity, int32 houseDecorFlags, uint32 earnedRetroactiveRewards, uint32 redeemed)
{
    uint32 owed = earnedRetroactiveRewards;
    if (houseDecorFlags != HOUSE_DECOR_FLAGS_DO_NOT_USE && startingQuantity > 0)
        owed += uint32(startingQuantity);
    return owed > redeemed ? owed - redeemed : 0;
}

bool HousingDecorStore::StarterPieceUsesStartingQuantity(int32 startingQuantity, int32 houseDecorFlags, uint32 redeemed)
{
    return GetOwedCount(startingQuantity, houseDecorFlags, 0, redeemed) > 0;
}

bool HousingDecorStore::IsRetroactiveRewardEarned(int32 rewardFlags, std::vector<std::pair<int32, int32>> const& criteria,
    std::function<bool(uint32)> const& hasAchievement, std::function<bool(uint32)> const& hasQuest)
{
    if (criteria.empty())
        return false;

    auto met = [&](std::pair<int32, int32> const& row)
    {
        if (row.first > 0 && hasAchievement(uint32(row.first)))
            return true;
        return row.second > 0 && hasQuest(uint32(row.second));
    };

    if (rewardFlags & RETROACTIVE_DECOR_REWARD_FLAG_ALL_CRITERIA_REQUIRED)
        return std::all_of(criteria.begin(), criteria.end(), met);
    return std::any_of(criteria.begin(), criteria.end(), met);
}

std::vector<HousingDecorStore::RetroactiveReward> HousingDecorStore::GetRetroactiveRewards(uint32 decorEntryId)
{
    std::vector<RetroactiveReward> rewards;
    for (RetroactiveDecorRewardEntry const* entry : sRetroactiveDecorRewardStore)
    {
        if (entry->HouseDecorID != decorEntryId)
            continue;

        RetroactiveReward& reward = rewards.emplace_back();
        reward.Flags = entry->Flags;
        for (RetroactiveDecorRewardCriteriaEntry const* criteria : sRetroactiveDecorRewardCriteriaStore)
            if (criteria->RetroactiveDecorRewardID == entry->ID)
                reward.Criteria.emplace_back(criteria->AchievementID, criteria->QuestID);
        if (reward.Criteria.empty() && (entry->AchievementID > 0 || entry->QuestID > 0))
            reward.Criteria.emplace_back(entry->AchievementID, entry->QuestID);
    }
    return rewards;
}

uint32 HousingDecorStore::CountEarnedRetroactiveRewards(std::vector<RetroactiveReward> const& rewards,
    std::function<bool(uint32)> const& hasAchievement, std::function<bool(uint32)> const& hasQuest)
{
    uint32 earned = 0;
    for (RetroactiveReward const& reward : rewards)
        if (IsRetroactiveRewardEarned(reward.Flags, reward.Criteria, hasAchievement, hasQuest))
            ++earned;
    return earned;
}

std::string HousingDecorStore::MakeItemSourceValue(uint32 realmId, uint64 itemLowGuid)
{
    return Trinity::StringFormat("{}-0-{:X}", realmId, itemLowGuid);
}

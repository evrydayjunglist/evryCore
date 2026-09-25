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

#ifndef HousingDecorStore_h__
#define HousingDecorStore_h__

#include "Housing.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The decor of one Battle.net account. Every piece is an instance with its own GUID, source and, once placed, the house
// it stands in. The pieces placed in a house live with that house (Housing::PlacedDecor); this store holds the pieces
// in storage, which is where every new piece starts, and what the account is owed and has owned. It needs no house:
// an account with none still gets, redeems and keeps decor.
//
// Every character of the account online at once, on any of its game accounts, shares one store. Spells and rewards on
// maps updated at the same time change it, so every access takes its lock.
class TC_GAME_API HousingDecorStore
{
public:
    explicit HousingDecorStore(uint32 bnetAccountId) : _bnetAccountId(bnetAccountId) { }

    HousingDecorStore(HousingDecorStore const&) = delete;
    HousingDecorStore& operator=(HousingDecorStore const&) = delete;

    // The store of an account, shared while any character of it holds it. The last holder to let go takes it away, so
    // the next login reads the saved rows again.
    static std::shared_ptr<HousingDecorStore> Acquire(uint32 bnetAccountId);

    uint32 GetBnetAccountId() const { return _bnetAccountId; }
    std::recursive_mutex& GetLock() const { return _lock; }

    // The rows of the account's pieces that stand in none of its houses, and the account's entry and catalog fetch
    // rows. Only the first character of the account to log in loads them; later ones find the store loaded.
    bool IsLoaded() const;
    void LoadFromDB(std::vector<Field*> const& storedDecor, PreparedQueryResult entries, PreparedQueryResult catalogFetch,
        PreparedQueryResult firstHouse);
    // Every change is written when it is made. This writes the pieces in storage and what the account has owned and
    // redeemed again only when loading changed something no row says yet: a piece whose house is gone, or an owned
    // entry with no row of its own. The houses save their placed pieces.
    void SaveToDB(CharacterDatabaseTransaction trans);

    // A new piece in storage, with a GUID from the one counter every account shares. The entry is recorded as owned;
    // firstOwned says whether the account had never owned it before. The rows go into trans.
    Housing::PlacedDecor CreateStored(uint32 decorEntryId, uint8 sourceType, std::string sourceValue, bool& firstOwned,
        CharacterDatabaseTransaction trans);
    // Records an entry as owned, for a piece created straight into a house. True the first time.
    bool MarkOwned(uint32 decorEntryId, CharacterDatabaseTransaction trans);

    Housing::PlacedDecor const* FindStored(ObjectGuid decorGuid) const;
    // Takes a piece out of storage to place it. The caller writes the piece's row with its house.
    Optional<Housing::PlacedDecor> TakeStored(ObjectGuid decorGuid);
    // Puts a piece taken out of a house into storage; its place, room, lock and pet go, its dyes stay.
    void PutInStorage(Housing::PlacedDecor decor, CharacterDatabaseTransaction trans);
    // Destroys a piece in storage. False when no piece in storage has that GUID.
    bool DestroyStored(ObjectGuid decorGuid, CharacterDatabaseTransaction trans);
    std::vector<Housing::PlacedDecor> GetStored() const;
    uint32 CountStored(uint32 decorEntryId) const;
    // A piece of that entry in storage, or an empty GUID.
    ObjectGuid FindStoredOfEntry(uint32 decorEntryId) const;

    bool HasOwned(uint32 decorEntryId) const;
    // How many entries the account has ever owned that the filter accepts.
    uint32 CountOwnedEntries(std::function<bool(uint32 /*decorEntryId*/)> const& filter) const;
    uint32 GetRedeemed(uint32 decorEntryId) const;
    // One more owed copy of the entry turned into a piece.
    void AddRedeemed(uint32 decorEntryId, CharacterDatabaseTransaction trans);

    // The time the client last said it fetched the decor catalog, kept across sessions; 0 when it never did.
    uint64 GetLastCatalogFetch() const;
    // Stores now and returns what was stored before, which is what the update's reply carries.
    uint64 ExchangeLastCatalogFetch(uint64 now);

    // Whether the account has had its first house, which is free and comes with the starter decor once. It stays
    // recorded when that house is packed or gone.
    bool HasHadFirstHouse() const;
    // Records the first house in the purchase's transaction.
    void RecordFirstHouse(CharacterDatabaseTransaction trans);

    // Appends the row of a piece: houseDatabaseId is the house it stands in, 0 while it is in storage.
    static void AppendDecorRow(CharacterDatabaseTransaction trans, uint32 bnetAccountId, uint64 houseDatabaseId,
        Housing::PlacedDecor const& decor);
    // A piece from a CHAR_SEL_ACCOUNT_HOUSING_DECOR row, without its room, which only the house can resolve.
    static Housing::PlacedDecor ReadDecorRow(Field* fields);

    // What an account is owed of one entry. StartingQuantity copies of every HouseDecor row but the "[DNT] ... DO NOT
    // USE" platforms, plus one per RetroactiveDecorReward row the account has earned, less what it redeemed already.
    static uint32 GetOwedCount(int32 startingQuantity, int32 houseDecorFlags, uint32 earnedRetroactiveRewards, uint32 redeemed);
    // Whether a starter piece made at a house purchase is one of its entry's starting quantity, so the purchase counts
    // it as redeemed: true while any of that starting quantity is still owed. Retail's purchase made exactly the
    // starting quantity of 1700 (two), 81 and 10952 (one each) (hbcd3 1299364-1299534, 1431714-1431809), and the
    // account's later storage holds no redeemed copy of them (hled1 788960-789160).
    static bool StarterPieceUsesStartingQuantity(int32 startingQuantity, int32 houseDecorFlags, uint32 redeemed);
    // Whether a RetroactiveDecorReward row is earned. Each criteria row is an achievement or a quest; with
    // AllCriteriaRequired every row must be met, otherwise one is enough. A reward with no criteria rows is never earned.
    static bool IsRetroactiveRewardEarned(int32 rewardFlags, std::vector<std::pair<int32 /*achievementId*/, int32 /*questId*/>> const& criteria,
        std::function<bool(uint32)> const& hasAchievement, std::function<bool(uint32)> const& hasQuest);
    // One RetroactiveDecorReward row of a decor entry: its flags and its (achievement, quest) criteria pairs.
    struct RetroactiveReward
    {
        int32 Flags = 0;
        std::vector<std::pair<int32 /*achievementId*/, int32 /*questId*/>> Criteria;
    };
    // The rows owing the entry. RetroactiveDecorRewardCriteria holds the pairs of every reward; rows 1-245 repeat
    // theirs on the reward itself, so the reward's own pair is only used when it has no criteria rows.
    static std::vector<RetroactiveReward> GetRetroactiveRewards(uint32 decorEntryId);
    static uint32 CountEarnedRetroactiveRewards(std::vector<RetroactiveReward> const& rewards,
        std::function<bool(uint32)> const& hasAchievement, std::function<bool(uint32)> const& hasQuest);

    // The source value of a piece granted by an item's spell: the realm, 0 and the item GUID's low part in hex. Decor
    // 1163 came from item 0x0C028800000000004000000A7D89D45C on realm 162 (hbcd3 1783383) and reads
    // "162-0-4000000A7D89D45C" after a relog (hled1 789057-789062). The middle number is 0 in every captured value;
    // what it stands for is not known.
    static std::string MakeItemSourceValue(uint32 realmId, uint64 itemLowGuid);

private:
    struct OwnedEntry
    {
        uint32 Redeemed = 0;
        uint64 FirstOwnedTime = 0;
    };

    void AppendEntryRow(CharacterDatabaseTransaction trans, uint32 decorEntryId, OwnedEntry const& entry) const;

    uint32 _bnetAccountId;
    mutable std::recursive_mutex _lock;
    bool _loaded = false;
    // Loading changed something that no row says yet; the next SaveToDB writes the store.
    bool _unsaved = false;
    std::unordered_map<ObjectGuid, Housing::PlacedDecor> _stored;
    std::unordered_map<uint32 /*decorEntryId*/, OwnedEntry> _owned;
    uint64 _lastCatalogFetch = 0;
    // Unix time the account bought its first house, 0 before it did.
    uint64 _firstHouseTime = 0;

    static std::mutex s_registryLock;
    static std::unordered_map<uint32, std::weak_ptr<HousingDecorStore>> s_stores;
};

#endif // HousingDecorStore_h__

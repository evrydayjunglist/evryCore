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
#include <unordered_set>
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
    // The room value saved with a piece: the interior room's database id, or 0 for a piece in the yard. The client
    // places yard decor on the plot's room, whose GUID counter is the plot index, so saving that counter would read
    // back as an interior room id.
    static uint64 GetSavedRoomValue(ObjectGuid roomGuid, uint32 baseRoomEntryId);
    // A redeem: when the account is still owed a copy of the entry, makes the piece in storage, counts the copy as
    // redeemed and queues the save, all under the store's lock, so two game accounts of one Battle.net account
    // redeeming at once on different map threads cannot both take the last owed copy, and their saves of the redeemed
    // count are queued in the order the count went up. owedBefore is what was owed before this redeem.
    Optional<Housing::PlacedDecor> RedeemOwed(uint32 decorEntryId, int32 startingQuantity, int32 houseDecorFlags,
        uint32 earnedRetroactiveRewards, uint32& owedBefore);

    // What an account is owed of one entry. StartingQuantity copies of every HouseDecor row but the "[DNT] ... DO NOT
    // USE" platforms, plus one per RetroactiveDecorReward row the account has earned, less what it redeemed already.
    static uint32 GetOwedCount(int32 startingQuantity, int32 houseDecorFlags, uint32 earnedRetroactiveRewards, uint32 redeemed);
    // Whether a starter piece made at a house purchase is one of its entry's starting quantity, so the purchase counts
    // it as redeemed: true while any of that starting quantity is still owed. Retail's purchase made exactly the
    // starting quantity of 1700 (two), 81 and 10952 (one each) (hbcd3 1299364-1299534, 1431714-1431809), and the
    // account's later storage holds no redeemed copy of them (hled1 788960-789160).
    static bool StarterPieceUsesStartingQuantity(int32 startingQuantity, int32 houseDecorFlags, uint32 redeemed);
    // Whether a RetroactiveDecorReward row is earned. Each criteria row is an achievement or a quest, and meeting any
    // one of them is enough, as the owner chose: the rewards with two rows pair a quest of each faction (rewards 251,
    // 252 and 253), so needing both would lock a one-faction account out. Every row carries the flag the client calls
    // "all criteria required", so that flag is not read. A reward with no criteria rows is never earned.
    static bool IsRetroactiveRewardEarned(std::vector<std::pair<int32 /*achievementId*/, int32 /*questId*/>> const& criteria,
        std::function<bool(uint32)> const& hasAchievement, std::function<bool(uint32)> const& hasQuest);
    // One RetroactiveDecorReward row of a decor entry: its (achievement, quest) criteria pairs.
    struct RetroactiveReward
    {
        std::vector<std::pair<int32 /*achievementId*/, int32 /*questId*/>> Criteria;
    };
    // The rows owing the entry. RetroactiveDecorRewardCriteria holds the pairs of every reward; rows 1-245 repeat
    // theirs on the reward itself, so the reward's own pair is only used when it has no criteria rows.
    static std::vector<RetroactiveReward> GetRetroactiveRewards(uint32 decorEntryId);
    static uint32 CountEarnedRetroactiveRewards(std::vector<RetroactiveReward> const& rewards,
        std::function<bool(uint32)> const& hasAchievement, std::function<bool(uint32)> const& hasQuest);
    // Every achievement and every quest any RetroactiveDecorReward row asks for.
    static void GetRetroactiveCriteriaIds(std::vector<uint32>& achievementIds, std::vector<uint32>& questIds);
    // The decor entries an account is owed through the RetroactiveDecorReward rows it has earned, each entry once,
    // without the "[DNT] ... DO NOT USE" platforms.
    static std::vector<uint32> GetEarnedRetroactiveEntries(std::function<bool(uint32)> const& hasAchievement,
        std::function<bool(uint32)> const& hasQuest);

    // What the account's first house purchase credits: the owed retroactive entries the account has never owned, which
    // become owned there, and the favor their first acquisition gives the new house. Retail's first purchase raised
    // "collect unique decor" (criteria 109249) from 1 to 109 and gave the new house 1080 favor, 108 entries at the
    // FirstAcquisitionBonus of 10 that 242 of the 245 retroactive entries carry (hbcd3 147482, 1299379-1299576,
    // 1310396; the favor packets at 1299772 and 1301305), and earned achievements 61309 and 61310 (1299585-1299666).
    // Retail sent no favor update for each of them, and later redeems of such entries gave neither favor nor a
    // criteria update (hbcd3 Numbers 16637-16738).
    struct FirstHouseCredit
    {
        std::vector<uint32> NewlyOwned;
        uint64 Favor = 0;
    };
    static FirstHouseCredit GetFirstHouseCredit(std::vector<uint32> const& owedEntries, std::function<bool(uint32)> const& alreadyOwned,
        std::function<int32(uint32)> const& firstAcquisitionBonus);

    // The achievements and quests of the account's characters that RetroactiveDecorReward rows ask for, as the
    // database held them when they were read at login. The first house purchase adds what the buyer has done since,
    // from her own character. StartRetroactiveProgressLoad is true only for the one caller that should read them; the
    // rows arrive later through SetRetroactiveProgress.
    bool StartRetroactiveProgressLoad();
    void SetRetroactiveProgress(std::unordered_set<uint32> achievements, std::unordered_set<uint32> quests);
    bool IsRetroactiveProgressLoaded() const;
    bool AccountHasRetroactiveAchievement(uint32 achievementId) const;
    bool AccountHasRetroactiveQuest(uint32 questId) const;

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
    bool _retroactiveProgressRequested = false;
    bool _retroactiveProgressLoaded = false;
    std::unordered_set<uint32> _retroactiveAchievements;
    std::unordered_set<uint32> _retroactiveQuests;

    static std::mutex s_registryLock;
    static std::unordered_map<uint32, std::weak_ptr<HousingDecorStore>> s_stores;
};

#endif // HousingDecorStore_h__

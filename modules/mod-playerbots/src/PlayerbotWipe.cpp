/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PlayerbotWipe.h"
#include "Playerbots.h"
#include "AccountMgr.h"
#include "DatabaseEnv.h"
#include "Duration.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "StringFormat.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{
    // How much of one world tick the wipe may use. It always does at least one delete per tick.
    constexpr Milliseconds WIPE_SLICE = 50ms;
    // How often a wait reads its count from the database.
    constexpr Milliseconds WIPE_POLL_EVERY = 250ms;
    // How long a count may stay the same, with nothing queued for either database, before the wait gives up. This
    // is not a limit on how long the deletes may take: while rows keep going, or work is queued, the wait goes on.
    constexpr Seconds WIPE_STALL_LIMIT = 60s;
    constexpr Seconds WIPE_PROGRESS_LOG_EVERY = 10s;

    // Tables in the login database that hold one Battle.net account's own collections and settings, with the column
    // that names the account. Rows here belong to that account alone. battle_pet_declinedname is keyed by pet and is
    // cleared through battle_pets first.
    struct AccountTable
    {
        char const* Table;
        char const* Column;
    };

    constexpr AccountTable BATTLENET_LOGIN_TABLES[] =
    {
        { "battle_pets", "battlenetAccountId" },
        { "battle_pet_slots", "battlenetAccountId" },
        { "battlenet_account_bans", "id" },
        { "battlenet_account_heirlooms", "accountId" },
        { "battlenet_account_mounts", "battlenetAccountId" },
        { "battlenet_account_player_data_element", "battlenetAccountId" },
        { "battlenet_account_player_data_flag", "battlenetAccountId" },
        { "battlenet_account_toys", "accountId" },
        { "battlenet_account_transmog_illusions", "battlenetAccountId" },
        { "battlenet_account_transmog_outfits", "battlenetAccountId" },
        { "battlenet_account_warband_scenes", "battlenetAccountId" },
        { "battlenet_account_warband_group_members", "battlenetAccountId" },
        { "battlenet_account_warband_groups", "battlenetAccountId" },
        { "battlenet_account_currency", "battlenetAccountId" },
        { "battlenet_item_appearances", "battlenetAccountId" },
        { "battlenet_item_favorite_appearances", "battlenetAccountId" },
    };

    // Housing rows in the characters database that belong to one Battle.net account alone.
    constexpr AccountTable BATTLENET_CHARACTER_TABLES[] =
    {
        { "account_housing_decor", "bnetAccountId" },
        { "account_housing_decor_entry", "bnetAccountId" },
        { "account_housing_catalog_fetch", "bnetAccountId" },
        { "account_housing_first_house", "bnetAccountId" },
        { "account_housing_blueprint", "bnetAccountId" },
    };

    // Housing rows that are shared with other players: a house on a neighborhood plot, and a share of a
    // neighborhood's initiative. Bots cannot do housing yet, so these should be empty. A Battle.net account that
    // still has any is kept, with a log line, rather than cutting into a neighborhood other players live in.
    constexpr AccountTable BATTLENET_SHARED_HOUSING_TABLES[] =
    {
        { "character_housing", "bnetAccountId" },
        { "neighborhood_initiative_contributions", "bnetAccountId" },
        { "neighborhood_initiative_reward_claims", "bnetAccountId" },
    };

    // Rows in the characters database that belong to one game account alone and that AccountMgr::DeleteAccount leaves.
    constexpr AccountTable GAME_ACCOUNT_CHARACTER_TABLES[] =
    {
        { "battlepay_account_distribution", "accountId" },
        { "account_currency_transfer_log", "accountId" },
    };

    std::string JoinIds(std::vector<uint32> const& ids)
    {
        std::string joined;
        for (uint32 id : ids)
        {
            if (!joined.empty())
                joined += ',';
            joined += std::to_string(id);
        }
        return joined;
    }

    template <class T>
    uint64 Count(DatabaseWorkerPool<T>& database, std::string const& sql)
    {
        QueryResult result = database.Query(sql.c_str());
        return result ? (*result)[0].GetUInt64() : 0;
    }

    bool DatabasesIdle()
    {
        return !CharacterDatabase.QueueSize() && !LoginDatabase.QueueSize();
    }

    TimePoint Now()
    {
        return std::chrono::steady_clock::now();
    }

    struct BotAccount
    {
        uint32 BattlenetId = 0;
        std::string Email;
        std::vector<uint32> GameAccounts;
    };

    struct Character
    {
        ObjectGuid Guid;
        uint32 Account = 0;
    };

    enum class Step
    {
        Find,
        DeleteCharacters,
        WaitCharacters,
        DeleteGameAccounts,
        WaitGameAccounts,
        DeleteBattlenetAccounts,
        WaitAccountRows,
        Finished
    };

    // Waits, a poll each WIPE_POLL_EVERY, until a count read from the database itself is zero. It reads the database
    // rather than trusting a queue, because a queue that is empty can still have a transaction running. It gives up
    // only when the count has stood still for WIPE_STALL_LIMIT with nothing queued for either database.
    class WaitUntilGone
    {
    public:
        enum class Result { Waiting, Gone, Stalled };

        void Begin(char const* what, std::function<uint64()> count)
        {
            _what = what;
            _count = std::move(count);
            _left = UINT64_MAX;
            _lastChange = _lastLog = _lastPoll = Now();
            _polled = false;
        }

        Result Update()
        {
            TimePoint const time = Now();
            if (_polled && time - _lastPoll < WIPE_POLL_EVERY)
                return Result::Waiting;

            _polled = true;
            _lastPoll = time;
            uint64 const now = _count();
            if (now != _left)
            {
                _left = now;
                _lastChange = time;
            }
            if (!_left)
                return Result::Gone;

            if (time - _lastLog >= WIPE_PROGRESS_LOG_EVERY)
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: waiting for the database to finish deleting {}: {} left.", _what, _left);
                _lastLog = time;
            }

            if (time - _lastChange >= WIPE_STALL_LIMIT && DatabasesIdle())
            {
                TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: {} {} are still in the database after {} seconds with nothing "
                    "queued for it. The wipe stops here and deletes nothing more.", _left, _what, uint32(WIPE_STALL_LIMIT.count()));
                return Result::Stalled;
            }
            return Result::Waiting;
        }

    private:
        char const* _what = "";
        std::function<uint64()> _count;
        uint64 _left = 0;
        TimePoint _lastChange;
        TimePoint _lastLog;
        TimePoint _lastPoll;
        bool _polled = false;
    };
}

struct PlayerbotWipe::Data
{
    Step Current = Step::Find;
    TimePoint Started = Now();
    uint32 Ticks = 0;

    std::vector<BotAccount> Bots;
    std::vector<uint32> GameAccounts;
    std::vector<Character> Characters;
    std::set<uint32> KeepForHousing;
    std::string BattlenetList;
    std::string AccountList;
    std::string BotCharacters;
    std::size_t Next = 0;
    WaitUntilGone Wait;

    uint32 Lookalikes = 0;
    uint32 GameAccountsDeleted = 0;
    uint64 MailBackToHumans = 0;
    uint64 GuildsWithHumans = 0;
    uint64 GuildsOnlyBots = 0;
    uint64 Auctions = 0;

    // Finds the bot accounts and their characters, and counts what ties a bot to a human player. False when there is
    // nothing to delete or nothing may be deleted.
    bool Find(State& result);
    // Runs work(index) for Next onwards until the list is done or this tick's slice is spent. True when done.
    bool Slice(std::size_t size, std::function<void(std::size_t)> const& work);
    void FindHousingToKeep();
    void DeleteGameAccount(uint32 account);
    void DeleteBattlenetAccount(BotAccount const& bot);
    State Report();
};

PlayerbotWipe::PlayerbotWipe() : _data(std::make_unique<Data>()) { }

PlayerbotWipe::~PlayerbotWipe() = default;

bool PlayerbotWipe::Data::Find(State& result)
{
    // The LIKE only narrows the search; each email must then match exactly.
    if (QueryResult found = LoginDatabase.Query(
        "SELECT id, email FROM battlenet_accounts WHERE email LIKE 'PLAYERBOT%@PLAYERBOTS.LOCAL'"))
    {
        do
        {
            std::string email = (*found)[1].GetString();
            if (!IsPlayerbotBattlenetEmail(email))
            {
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: Battle.net account {} is not a bot account; it is not touched.", email);
                ++Lookalikes;
                continue;
            }
            Bots.push_back({ (*found)[0].GetUInt32(), std::move(email), {} });
        } while (found->NextRow());
    }

    if (Bots.empty())
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: there are no bot accounts. Nothing to delete.");
        result = State::Done;
        return false;
    }

    std::vector<uint32> battlenetIds;
    for (BotAccount& bot : Bots)
    {
        battlenetIds.push_back(bot.BattlenetId);
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT_IDS);
        stmt->setUInt32(0, bot.BattlenetId);
        if (PreparedQueryResult games = LoginDatabase.Query(stmt))
        {
            do
            {
                bot.GameAccounts.push_back((*games)[0].GetUInt32());
                GameAccounts.push_back(bot.GameAccounts.back());
            } while (games->NextRow());
        }
    }

    BattlenetList = JoinIds(battlenetIds);
    AccountList = GameAccounts.empty() ? std::string("0") : JoinIds(GameAccounts);
    // Characters still on a bot account, and characters an earlier delete unlinked from one.
    BotCharacters = Trinity::StringFormat(
        "SELECT guid FROM characters WHERE account IN ({0}) OR deleteInfos_Account IN ({0})", AccountList);

    // What ties a bot to a human player, counted before anything goes, so the log says what the core delete did
    // with it. Bots have no guild, mail or auction verbs, so these are normally zero.
    MailBackToHumans = Count(CharacterDatabase, Trinity::StringFormat(
        "SELECT COUNT(*) FROM mail WHERE receiver IN ({0}) AND messageType = 0 AND sender NOT IN ({0})", BotCharacters));
    GuildsWithHumans = Count(CharacterDatabase, Trinity::StringFormat(
        "SELECT COUNT(*) FROM guild g WHERE g.leaderguid IN ({0}) AND EXISTS "
        "(SELECT 1 FROM guild_member m WHERE m.guildid = g.guildid AND m.guid NOT IN ({0}))", BotCharacters));
    GuildsOnlyBots = Count(CharacterDatabase, Trinity::StringFormat(
        "SELECT COUNT(*) FROM guild g WHERE g.leaderguid IN ({0}) AND NOT EXISTS "
        "(SELECT 1 FROM guild_member m WHERE m.guildid = g.guildid AND m.guid NOT IN ({0}))", BotCharacters));
    Auctions = Count(CharacterDatabase, Trinity::StringFormat(
        "SELECT COUNT(*) FROM auctionhouse WHERE owner IN ({0}) OR bidder IN ({0})", BotCharacters));

    if (QueryResult found = CharacterDatabase.Query(Trinity::StringFormat(
        "SELECT guid, account, deleteInfos_Account FROM characters WHERE account IN ({0}) OR deleteInfos_Account IN ({0})",
        AccountList).c_str()))
    {
        do
        {
            uint32 const account = (*found)[1].GetUInt32();
            Characters.push_back({ ObjectGuid::Create<HighGuid::Player>((*found)[0].GetUInt64()),
                account ? account : (*found)[2].GetUInt32() });
        } while (found->NextRow());
    }

    for (Character const& character : Characters)
    {
        if (ObjectAccessor::FindPlayer(character.Guid))
        {
            TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: bot character {} is in the world, so no bot is deleted. "
                "The wipe runs at startup before any bot logs in; this should not happen.", character.Guid.ToString());
            result = State::Failed;
            return false;
        }
    }

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: deleting {} bot character(s), {} bot game account(s) and {} bot Battle.net account(s).",
        Characters.size(), GameAccounts.size(), Bots.size());
    return true;
}

bool PlayerbotWipe::Data::Slice(std::size_t size, std::function<void(std::size_t)> const& work)
{
    TimePoint const start = Now();
    while (Next < size)
    {
        work(Next++);
        if (Now() - start >= WIPE_SLICE)
            break;
    }
    if (Next < size)
        return false;

    Next = 0;
    return true;
}

void PlayerbotWipe::Data::DeleteGameAccount(uint32 account)
{
    AccountOpResult const result = AccountMgr::DeleteAccount(account);
    if (result != AccountOpResult::AOR_OK)
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: AccountMgr::DeleteAccount failed for game account {} (result {}).",
            account, uint32(result));
    else
        ++GameAccountsDeleted;

    LoginDatabase.PExecute("DELETE FROM account_last_played_character WHERE accountId = {}", account);
    for (AccountTable const& table : GAME_ACCOUNT_CHARACTER_TABLES)
        CharacterDatabase.PExecute("DELETE FROM {} WHERE {} = {}", table.Table, table.Column, account);
}

void PlayerbotWipe::Data::FindHousingToKeep()
{
    for (AccountTable const& table : BATTLENET_SHARED_HOUSING_TABLES)
    {
        if (QueryResult result = CharacterDatabase.Query(Trinity::StringFormat(
            "SELECT DISTINCT {1} FROM {0} WHERE {1} IN ({2})", table.Table, table.Column, BattlenetList).c_str()))
        {
            do
            {
                uint32 const id = (*result)[0].GetUInt32();
                KeepForHousing.insert(id);
                TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: bot Battle.net account {} still has rows in {}, which is shared "
                    "with other players' neighborhoods. That Battle.net account is kept; look at it by hand.", id, table.Table);
            } while (result->NextRow());
        }
    }
}

void PlayerbotWipe::Data::DeleteBattlenetAccount(BotAccount const& bot)
{
    if (KeepForHousing.contains(bot.BattlenetId))
        return;

    CharacterDatabaseTransaction characterTrans = CharacterDatabase.BeginTransaction();
    for (AccountTable const& table : BATTLENET_CHARACTER_TABLES)
        characterTrans->Append(Trinity::StringFormat("DELETE FROM {} WHERE {} = {}", table.Table, table.Column, bot.BattlenetId).c_str());
    CharacterDatabase.DirectCommitTransaction(characterTrans);

    LoginDatabaseTransaction loginTrans = LoginDatabase.BeginTransaction();
    loginTrans->Append(Trinity::StringFormat("DELETE d FROM battle_pet_declinedname d INNER JOIN battle_pets p ON p.guid = d.guid "
        "WHERE p.battlenetAccountId = {}", bot.BattlenetId).c_str());
    for (AccountTable const& table : BATTLENET_LOGIN_TABLES)
        loginTrans->Append(Trinity::StringFormat("DELETE FROM {} WHERE {} = {}", table.Table, table.Column, bot.BattlenetId).c_str());
    loginTrans->Append(Trinity::StringFormat("DELETE FROM battlenet_accounts WHERE id = {}", bot.BattlenetId).c_str());
    LoginDatabase.DirectCommitTransaction(loginTrans);
}

PlayerbotWipe::State PlayerbotWipe::Data::Report()
{
    uint64 const battlenetLeft = Count(LoginDatabase, Trinity::StringFormat(
        "SELECT COUNT(*) FROM battlenet_accounts WHERE id IN ({})", BattlenetList));

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: deleted {} bot character(s), {} bot game account(s) and {} of {} bot Battle.net "
        "account(s) in {} seconds over {} world tick(s). {} human mail(s) to bots went back to the sender with their items and money. "
        "{} guild(s) led by a bot with human members passed to the highest-ranked member left, and {} guild(s) of bots only were "
        "disbanded. {} auction(s) with a bot as seller or bidder were left to finish as usual. {} lookalike Battle.net account(s) "
        "were not touched.",
        Characters.size(), GameAccountsDeleted, uint64(Bots.size()) - battlenetLeft, Bots.size(),
        uint32(std::chrono::duration_cast<Seconds>(Now() - Started).count()), Ticks,
        MailBackToHumans, GuildsWithHumans, GuildsOnlyBots, Auctions, Lookalikes);

    return battlenetLeft == 0 && GameAccountsDeleted == GameAccounts.size() ? State::Done : State::Failed;
}

PlayerbotWipe::State PlayerbotWipe::Update()
{
    Data& d = *_data;
    ++d.Ticks;
    switch (d.Current)
    {
        case Step::Find:
        {
            State result = State::Running;
            if (!d.Find(result))
            {
                d.Current = Step::Finished;
                return result;
            }
            d.Current = Step::DeleteCharacters;
            return State::Running;
        }
        case Step::DeleteCharacters:
            // The same path as a delete from the character list, finally rather than unlinked whatever
            // CharDelete.Method says.
            if (d.Slice(d.Characters.size(), [&](std::size_t i)
                { Player::DeleteFromDB(d.Characters[i].Guid, d.Characters[i].Account, false, true); }))
            {
                d.Wait.Begin("bot characters", [&d] { return Count(CharacterDatabase, "SELECT COUNT(*) FROM (" + d.BotCharacters + ") c"); });
                d.Current = Step::WaitCharacters;
            }
            return State::Running;
        case Step::WaitCharacters:
            // The game accounts are deleted only once the characters are gone: AccountMgr::DeleteAccount deletes any
            // character it still finds, and a character deleted twice would send a human's mail back twice.
            switch (d.Wait.Update())
            {
                case WaitUntilGone::Result::Waiting: return State::Running;
                case WaitUntilGone::Result::Stalled: d.Current = Step::Finished; return State::Failed;
                case WaitUntilGone::Result::Gone: d.Current = Step::DeleteGameAccounts; return State::Running;
            }
            break;
        case Step::DeleteGameAccounts:
            if (d.Slice(d.GameAccounts.size(), [&](std::size_t i) { d.DeleteGameAccount(d.GameAccounts[i]); }))
            {
                d.Wait.Begin("bot game accounts", [&d] { return Count(LoginDatabase, Trinity::StringFormat(
                    "SELECT COUNT(*) FROM account WHERE id IN ({0}) OR battlenet_account IN ({1})", d.AccountList, d.BattlenetList)); });
                d.Current = Step::WaitGameAccounts;
            }
            return State::Running;
        case Step::WaitGameAccounts:
            // A Battle.net account row cannot go while a game account still points at it.
            switch (d.Wait.Update())
            {
                case WaitUntilGone::Result::Waiting: return State::Running;
                case WaitUntilGone::Result::Stalled: d.Current = Step::Finished; return State::Failed;
                case WaitUntilGone::Result::Gone:
                    d.FindHousingToKeep();
                    d.Current = Step::DeleteBattlenetAccounts;
                    return State::Running;
            }
            break;
        case Step::DeleteBattlenetAccounts:
            if (d.Slice(d.Bots.size(), [&](std::size_t i) { d.DeleteBattlenetAccount(d.Bots[i]); }))
            {
                // What AccountMgr::DeleteAccount and the lines after it queued without waiting must be in the database
                // before worldserver stops.
                d.Wait.Begin("rows of bot game accounts", [&d]
                {
                    std::string accountRows;
                    for (AccountTable const& table : GAME_ACCOUNT_CHARACTER_TABLES)
                        accountRows += Trinity::StringFormat(" + (SELECT COUNT(*) FROM {} WHERE {} IN ({}))", table.Table, table.Column, d.AccountList);
                    return Count(CharacterDatabase, Trinity::StringFormat("SELECT (SELECT COUNT(*) FROM account_data WHERE accountId IN ({0})) "
                            "+ (SELECT COUNT(*) FROM account_tutorial WHERE accountId IN ({0})){1}", d.AccountList, accountRows))
                        + Count(LoginDatabase, Trinity::StringFormat(
                            "SELECT COUNT(*) FROM account_last_played_character WHERE accountId IN ({})", d.AccountList));
                });
                d.Current = Step::WaitAccountRows;
            }
            return State::Running;
        case Step::WaitAccountRows:
            if (d.Wait.Update() == WaitUntilGone::Result::Waiting)
                return State::Running;
            // A stall here is already in the log; the report still says what is left.
            d.Current = Step::Finished;
            return d.Report();
        case Step::Finished:
            break;
    }
    return State::Failed;
}

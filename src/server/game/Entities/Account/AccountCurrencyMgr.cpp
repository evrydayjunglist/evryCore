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

#include "AccountCurrencyMgr.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "WorldSession.h"
#include "WorldStateMgr.h"
#include <algorithm>

namespace
{
uint32 GetMigrationCurrencyMaxCap(CurrencyTypesEntry const* currency, Player const* capPlayer, uint32 increasedCapQuantity)
{
    if (!currency->HasMaxQuantity(false, false))
        return 0;

    uint32 maxQuantity = currency->MaxQty;
    // LoadFromDB consolidate is pre-SetMap; GetMap() ASSERTs. GetValue allows null.
    if (currency->MaxQtyWorldStateID && capPlayer)
        maxQuantity = WorldStateMgr::GetValue(currency->MaxQtyWorldStateID, capPlayer->FindMap());

    if (currency->GetFlags().HasFlag(CurrencyTypesFlags::DynamicMaximum))
        maxQuantity += increasedCapQuantity;

    return maxQuantity;
}
}

AccountCurrencyMgr::AccountCurrencyMgr(WorldSession* owner) : _owner(owner)
{
}

void AccountCurrencyMgr::LoadFromDB(PreparedQueryResult result)
{
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint16 currencyId = fields[0].GetUInt16();
        if (!sCurrencyTypesStore.LookupEntry(currencyId))
            continue;

        PlayerCurrency cur;
        cur.state = PLAYERCURRENCY_UNCHANGED;
        cur.Quantity = fields[1].GetUInt32();
        cur.WeeklyQuantity = fields[2].GetUInt32();
        cur.TrackedQuantity = fields[3].GetUInt32();
        cur.IncreasedCapQuantity = fields[4].GetUInt32();
        cur.EarnedQuantity = fields[5].GetUInt32();
        cur.Flags = CurrencyDbFlags(fields[6].GetUInt8());

        _currencies[currencyId] = cur;
    } while (result->NextRow());
}

void AccountCurrencyMgr::SaveToDB(LoginDatabaseTransaction trans)
{
    uint32 const battlenetAccountId = _owner->GetBattlenetAccountId();

    // Battle.net account tables foreign-key on battlenetAccountId. Nothing to save when that id is 0.
    if (!battlenetAccountId)
        return;

    for (auto itr = _currencies.begin(); itr != _currencies.end();)
    {
        PlayerCurrency& currency = itr->second;

        if (currency.Quantity == 0 && currency.state != PLAYERCURRENCY_UNCHANGED)
        {
            if (currency.state != PLAYERCURRENCY_NEW)
            {
                LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_BNET_ACCOUNT_CURRENCY);
                stmt->setUInt32(0, battlenetAccountId);
                stmt->setUInt16(1, itr->first);
                trans->Append(stmt);
            }

            itr = _currencies.erase(itr);
            continue;
        }

        switch (currency.state)
        {
            case PLAYERCURRENCY_NEW:
            {
                LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_BNET_ACCOUNT_CURRENCY);
                stmt->setUInt32(0, battlenetAccountId);
                stmt->setUInt16(1, itr->first);
                stmt->setUInt32(2, currency.Quantity);
                stmt->setUInt32(3, currency.WeeklyQuantity);
                stmt->setUInt32(4, currency.TrackedQuantity);
                stmt->setUInt32(5, currency.IncreasedCapQuantity);
                stmt->setUInt32(6, currency.EarnedQuantity);
                stmt->setUInt8(7, AsUnderlyingType(currency.Flags));
                trans->Append(stmt);
                break;
            }
            case PLAYERCURRENCY_CHANGED:
            {
                LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_BNET_ACCOUNT_CURRENCY);
                stmt->setUInt32(0, currency.Quantity);
                stmt->setUInt32(1, currency.WeeklyQuantity);
                stmt->setUInt32(2, currency.TrackedQuantity);
                stmt->setUInt32(3, currency.IncreasedCapQuantity);
                stmt->setUInt32(4, currency.EarnedQuantity);
                stmt->setUInt8(5, AsUnderlyingType(currency.Flags));
                stmt->setUInt32(6, battlenetAccountId);
                stmt->setUInt16(7, itr->first);
                trans->Append(stmt);
                break;
            }
            default:
                break;
        }

        currency.state = PLAYERCURRENCY_UNCHANGED;
        ++itr;
    }
}

bool AccountCurrencyMgr::SaveToDBImmediate()
{
    LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();
    SaveToDB(trans);
    if (!trans->GetSize())
        return true;

    LoginDatabase.DirectCommitTransaction(trans);
    return true;
}

PlayerCurrency const* AccountCurrencyMgr::GetCurrency(uint32 currencyId) const
{
    auto itr = _currencies.find(currencyId);
    if (itr == _currencies.end())
        return nullptr;

    return &itr->second;
}

PlayerCurrency* AccountCurrencyMgr::GetOrCreateCurrency(uint32 currencyId)
{
    auto itr = _currencies.find(currencyId);
    if (itr != _currencies.end())
        return &itr->second;

    PlayerCurrency cur;
    cur.state = PLAYERCURRENCY_NEW;
    cur.Quantity = 0;
    cur.WeeklyQuantity = 0;
    cur.TrackedQuantity = 0;
    cur.IncreasedCapQuantity = 0;
    cur.EarnedQuantity = 0;
    cur.Flags = CurrencyDbFlags(0);

    auto emplaceResult = _currencies.emplace(currencyId, cur);
    return &emplaceResult.first->second;
}

void AccountCurrencyMgr::MergeMigrationCurrency(CurrencyTypesEntry const* currency, PlayerCurrency const& aggregatedFromCharacters, Player const* capPlayer, uint32 authQuantityBeforeMerge)
{
    if (!currency)
        return;

    PlayerCurrency* accountCurrency = GetOrCreateCurrency(currency->ID);

    accountCurrency->WeeklyQuantity = std::max(accountCurrency->WeeklyQuantity, aggregatedFromCharacters.WeeklyQuantity);
    accountCurrency->TrackedQuantity = std::max(accountCurrency->TrackedQuantity, aggregatedFromCharacters.TrackedQuantity);
    accountCurrency->IncreasedCapQuantity = std::max(accountCurrency->IncreasedCapQuantity, aggregatedFromCharacters.IncreasedCapQuantity);
    accountCurrency->EarnedQuantity = std::max(accountCurrency->EarnedQuantity, aggregatedFromCharacters.EarnedQuantity);
    accountCurrency->Flags = CurrencyDbFlags(std::max(AsUnderlyingType(accountCurrency->Flags), AsUnderlyingType(aggregatedFromCharacters.Flags)));

    // Leftover character rows and the Battle.net pool can both have a quantity when a
    // previous login already migrated those rows and the delete has not landed yet.
    // Keep the larger amount instead of adding, so that leftover is not counted twice
    // and a larger leftover is not thrown away.
    uint32 mergedQuantity = accountCurrency->Quantity;
    if (authQuantityBeforeMerge > 0 && aggregatedFromCharacters.Quantity > 0)
        mergedQuantity = std::max(mergedQuantity, aggregatedFromCharacters.Quantity);
    else
        mergedQuantity += aggregatedFromCharacters.Quantity;

    if (capPlayer)
    {
        uint32 const maxCap = GetMigrationCurrencyMaxCap(currency, capPlayer, accountCurrency->IncreasedCapQuantity);
        if (maxCap && mergedQuantity > maxCap)
            mergedQuantity = maxCap;
    }

    accountCurrency->Quantity = mergedQuantity;

    if (accountCurrency->state != PLAYERCURRENCY_NEW)
        accountCurrency->state = PLAYERCURRENCY_CHANGED;
}

bool AccountCurrencyMgr::ModifyCurrency(Player* owner, CurrencyTypesEntry const* currency, int32& amount, bool ignoreCaps, bool /*isGainOnRefund*/, bool isUpdatingVersion)
{
    if (!owner || !currency || !amount)
        return false;

    PlayerCurrency* accountCurrency = GetOrCreateCurrency(currency->ID);

    uint32 weeklyCap = owner->GetCurrencyWeeklyCap(currency);
    if (!ignoreCaps)
    {
        if (weeklyCap && amount > 0 && (accountCurrency->WeeklyQuantity + amount) > weeklyCap)
            amount = weeklyCap - accountCurrency->WeeklyQuantity;

        uint32 maxCap = owner->GetCurrencyMaxQuantity(currency, false, isUpdatingVersion);
        if (maxCap && amount > 0 && (accountCurrency->Quantity + amount) > maxCap)
            amount = maxCap - accountCurrency->Quantity;
    }

    if (amount < 0 && uint32(std::abs(amount)) > accountCurrency->Quantity)
        amount = accountCurrency->Quantity * -1;

    if (!amount)
        return false;

    if (accountCurrency->state != PLAYERCURRENCY_NEW)
        accountCurrency->state = PLAYERCURRENCY_CHANGED;

    accountCurrency->Quantity += amount;

    if (amount > 0 && !ignoreCaps)
    {
        if (weeklyCap)
            accountCurrency->WeeklyQuantity += amount;

        if (currency->IsTrackingQuantity())
            accountCurrency->TrackedQuantity += amount;

        if (currency->HasTotalEarned())
            accountCurrency->EarnedQuantity += amount;
    }

    return true;
}

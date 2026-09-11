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

#ifndef TRINITYCORE_ACCOUNT_CURRENCY_MGR_H
#define TRINITYCORE_ACCOUNT_CURRENCY_MGR_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "Player.h"
#include <unordered_map>

class WorldSession;
struct CurrencyTypesEntry;

class TC_GAME_API AccountCurrencyMgr
{
public:
    explicit AccountCurrencyMgr(WorldSession* owner);

    void LoadFromDB(PreparedQueryResult result);
    void SaveToDB(LoginDatabaseTransaction trans);
    bool SaveToDBImmediate();

    PlayerCurrency const* GetCurrency(uint32 currencyId) const;
    PlayerCurrency* GetOrCreateCurrency(uint32 currencyId);
    std::unordered_map<uint32, PlayerCurrency> const& GetCurrencies() const { return _currencies; }

    void MergeMigrationCurrency(CurrencyTypesEntry const* currency, PlayerCurrency const& aggregatedFromCharacters, class Player const* capPlayer, uint32 authQuantityBeforeMerge);

    bool ModifyCurrency(class Player* owner, CurrencyTypesEntry const* currency, int32& amount, bool ignoreCaps, bool isGainOnRefund, bool isUpdatingVersion);

private:
    WorldSession* _owner;
    std::unordered_map<uint32, PlayerCurrency> _currencies;
};

#endif // TRINITYCORE_ACCOUNT_CURRENCY_MGR_H

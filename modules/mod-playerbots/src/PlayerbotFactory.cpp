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

#include "PlayerbotFactory.h"
#include "AccountMgr.h"
#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "ClientBuildInfo.h"
#include "Common.h"
#include "Containers.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "DBCEnums.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RaceMask.h"
#include "RealmList.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <ranges>
#include <vector>

namespace
{
struct RaceClassSex
{
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 Sex = 0;
};

std::string MakePassword()
{
    std::array<uint8, 16> bytes = Trinity::Crypto::GetRandomBytes<16>();
    return ByteArrayToHexStr(bytes);
}

uint32 GetRealmBuild()
{
    if (std::shared_ptr<Realm const> realm = sRealmList->GetCurrentRealm())
        return realm->Build;

    return 0;
}

ClientBuild::VariantId GetClientBuildVariant()
{
    return
    {
        ClientBuild::Platform::Win_x64,
        ClientBuild::Arch::x64,
        ClientBuild::Type::Retail
    };
}

bool FillDefaultCustomizations(WorldSession* session, WorldPackets::Character::CharacterCreateInfo& createInfo)
{
    std::vector<ChrCustomizationOptionEntry const*> const* options = sDB2Manager.GetCustomiztionOptions(createInfo.Race, createInfo.Sex);
    if (!options)
        return false;

    Races race = Races(createInfo.Race);
    Classes playerClass = Classes(createInfo.Class);

    for (ChrCustomizationOptionEntry const* option : *options)
    {
        if (option->ChrCustomizationReqID)
            if (ChrCustomizationReqEntry const* req = sChrCustomizationReqStore.LookupEntry(option->ChrCustomizationReqID))
                if (!session->MeetsChrCustomizationReq(req, race, playerClass, false,
                    MakeChrCustomizationChoiceRange(createInfo.Customizations)))
                    continue;

        std::vector<ChrCustomizationChoiceEntry const*> const* choices = sDB2Manager.GetCustomiztionChoices(option->ID);
        if (!choices || choices->empty())
            continue;

        for (ChrCustomizationChoiceEntry const* choiceEntry : *choices)
        {
            if (choiceEntry->ChrCustomizationReqID)
                if (ChrCustomizationReqEntry const* req = sChrCustomizationReqStore.LookupEntry(choiceEntry->ChrCustomizationReqID))
                    if (!session->MeetsChrCustomizationReq(req, race, playerClass, true,
                        MakeChrCustomizationChoiceRange(createInfo.Customizations)))
                        continue;

            WorldPackets::Character::ChrCustomizationChoice choice;
            choice.ChrCustomizationOptionID = option->ID;
            choice.ChrCustomizationChoiceID = choiceEntry->ID;
            createInfo.Customizations.push_back(choice);
            break;
        }
    }

    std::ranges::sort(createInfo.Customizations, std::ranges::less(),
        &WorldPackets::Character::ChrCustomizationChoice::ChrCustomizationOptionID);

    return session->ValidateAppearance(race, playerClass, Gender(createInfo.Sex),
        MakeChrCustomizationChoiceRange(createInfo.Customizations));
}

bool PassesCreateChecks(WorldSession* session, uint8 race, uint8 playerClass)
{
    if (!sChrClassesStore.LookupEntry(playerClass) || !sChrRacesStore.LookupEntry(race))
        return false;

    if (!sObjectMgr->GetPlayerInfo(race, playerClass))
        return false;

    RaceUnlockRequirement const* raceUnlock = sObjectMgr->GetRaceUnlockRequirement(race);
    if (!raceUnlock)
        return false;

    if (raceUnlock->Expansion > session->GetAccountExpansion())
        return false;

    if (ClassAvailability const* raceClass = sObjectMgr->GetClassExpansionRequirement(race, playerClass))
    {
        if (raceClass->ActiveExpansionLevel > session->GetExpansion() || raceClass->AccountExpansionLevel > session->GetAccountExpansion())
            return false;
    }
    else if (ClassAvailability const* classFallback = sObjectMgr->GetClassExpansionRequirementFallback(playerClass))
    {
        if (classFallback->MinActiveExpansionLevel > session->GetExpansion() || classFallback->AccountExpansionLevel > session->GetAccountExpansion())
            return false;
    }
    else
        return false;

    if (uint32 mask = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED))
    {
        switch (Player::TeamIdForRace(race))
        {
            case TEAM_ALLIANCE:
                if (mask & (1 << 0))
                    return false;
                break;
            case TEAM_HORDE:
                if (mask & (1 << 1))
                    return false;
                break;
            case TEAM_NEUTRAL:
                if (mask & (1 << 2))
                    return false;
                break;
            default:
                break;
        }
    }

    Trinity::RaceMask<uint64> raceMaskDisabled{ sWorld->GetUInt64Config(CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK) };
    if (raceMaskDisabled.HasRace(race))
        return false;

    uint32 classMaskDisabled = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK);
    if ((1 << (playerClass - 1)) & classMaskDisabled)
        return false;

    return true;
}

bool IsFirstFactoryClass(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_DEATH_KNIGHT:
        case CLASS_DEMON_HUNTER:
        case CLASS_EVOKER:
        case CLASS_ADVENTURER:
        case CLASS_TRAVELER:
        case CLASS_NONE:
            return false;
        default:
            return true;
    }
}

bool IsFirstFactoryRace(ChrRacesEntry const* raceEntry)
{
    if (!raceEntry)
        return false;

    if (raceEntry->GetFlags().HasFlag(ChrRacesFlag::NPCOnly))
        return false;

    if (raceEntry->GetFlags().HasFlag(ChrRacesFlag::IsAlliedRace))
        return false;

    if (Player::TeamForRace(raceEntry->ID) != HORDE)
        return false;

    if (raceEntry->StartingLevel > int32(sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL)))
        return false;

    return true;
}

void AppendNameGenRace(std::vector<uint8>& races, uint8 race)
{
    if (!race)
        return;

    if (std::ranges::find(races, race) == races.end())
        races.push_back(race);
}

// NameGen.db2 has no rows for Horde/Alliance pandaren (26/25). Those names are stored
// under pandaren (24). Follow NeutralRaceID and RaceRelated so we use the same name
// table a create-screen random name would need, without inventing names.
std::vector<uint8> NameGenRaces(uint8 race)
{
    std::vector<uint8> races;
    AppendNameGenRace(races, race);
    if (ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race))
    {
        if (raceEntry->NeutralRaceID > 0)
            AppendNameGenRace(races, uint8(raceEntry->NeutralRaceID));
        if (raceEntry->RaceRelated > 0)
            AppendNameGenRace(races, uint8(raceEntry->RaceRelated));
    }

    return races;
}

std::string RandomNameGenName(uint8 race, uint8 sex)
{
    for (uint8 nameRace : NameGenRaces(race))
    {
        std::string name = sDB2Manager.GetNameGenEntry(nameRace, sex);
        if (!name.empty())
            return name;
    }

    return {};
}

std::vector<RaceClassSex> CollectCombos(WorldSession* session)
{
    std::vector<RaceClassSex> combos;

    for (ChrRacesEntry const* raceEntry : sChrRacesStore)
    {
        if (!IsFirstFactoryRace(raceEntry))
            continue;

        for (ChrClassesEntry const* classEntry : sChrClassesStore)
        {
            if (!classEntry || !IsFirstFactoryClass(classEntry->ID))
                continue;

            if (!PassesCreateChecks(session, raceEntry->ID, classEntry->ID))
                continue;

            for (uint8 sex : { uint8(GENDER_MALE), uint8(GENDER_FEMALE) })
            {
                if (RandomNameGenName(uint8(raceEntry->ID), sex).empty())
                    continue;

                WorldPackets::Character::CharacterCreateInfo probe;
                probe.Race = raceEntry->ID;
                probe.Class = classEntry->ID;
                probe.Sex = sex;
                if (!FillDefaultCustomizations(session, probe))
                    continue;

                combos.push_back({ uint8(raceEntry->ID), uint8(classEntry->ID), sex });
            }
        }
    }

    return combos;
}

bool PickName(uint8 race, uint8 sex, std::string& name)
{
    for (uint32 attempt = 0; attempt < 40; ++attempt)
    {
        name = RandomNameGenName(race, sex);
        if (name.empty())
            continue;

        if (!normalizePlayerName(name))
            continue;

        if (ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) != CHAR_NAME_SUCCESS)
            continue;

        if (sObjectMgr->IsReservedName(name))
            continue;

        if (sCharacterCache->GetCharacterCacheByName(name))
            continue;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
        stmt->setString(0, name);
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            continue;

        return true;
    }

    return false;
}

bool CreateCharacter(WorldSession* session, PlayerbotAccount& account)
{
    std::vector<RaceClassSex> combos = CollectCombos(session);
    if (combos.empty())
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: no Horde level-1 race/class combo passed the same checks HandleCharCreateOpcode uses.");
        return false;
    }

    RaceClassSex const& pick = Trinity::Containers::SelectRandomContainerElement(combos);

    WorldPackets::Character::CharacterCreateInfo createInfo;
    createInfo.Race = pick.Race;
    createInfo.Class = pick.Class;
    createInfo.Sex = pick.Sex;
    if (!FillDefaultCustomizations(session, createInfo))
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: picked combo {}/{} sex {} failed appearance checks.",
            uint32(pick.Race), uint32(pick.Class), uint32(pick.Sex));
        return false;
    }

    if (!PickName(createInfo.Race, createInfo.Sex, createInfo.Name))
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: could not find a free character name for race {} sex {}.",
            uint32(createInfo.Race), uint32(createInfo.Sex));
        return false;
    }

    CharacterDatabasePreparedStatement* countStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_SUM_CHARS);
    countStmt->setUInt32(0, session->GetAccountId());
    if (PreparedQueryResult countResult = CharacterDatabase.Query(countStmt))
        createInfo.CharCount = uint8((*countResult)[0].GetUInt64());

    if (createInfo.CharCount >= sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM))
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: account {} is at the realm character limit.", session->GetAccountId());
        return false;
    }

    std::unique_ptr<Player, void(*)(Player*)> newChar(new Player(session), [](Player* player)
    {
        player->CleanupsBeforeDelete();
        delete player;
    });
    newChar->GetMotionMaster()->Initialize();
    if (!newChar->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &createInfo))
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: Player::Create failed for {}.", createInfo.Name);
        return false;
    }

    newChar->SetAtLoginFlag(AT_LOGIN_FIRST);

    CharacterDatabaseTransaction characterTransaction = CharacterDatabase.BeginTransaction();
    LoginDatabaseTransaction loginTransaction = LoginDatabase.BeginTransaction();
    newChar->SaveToDB(loginTransaction, characterTransaction, true);
    createInfo.CharCount += 1;

    LoginDatabasePreparedStatement* realmChars = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
    realmChars->setUInt32(0, createInfo.CharCount);
    realmChars->setUInt32(1, session->GetAccountId());
    realmChars->setUInt32(2, sRealmList->GetCurrentRealmId().Realm);
    loginTransaction->Append(realmChars);

    TransactionCallback characterCommit = CharacterDatabase.AsyncCommitTransaction(characterTransaction);
    if (!characterCommit.m_future.get())
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: character save failed for {}.", createInfo.Name);
        return false;
    }

    LoginDatabase.CommitTransaction(loginTransaction);

    sScriptMgr->OnPlayerCreate(newChar.get());
    sCharacterCache->AddCharacterCacheEntry(newChar->GetGUID(), session->GetAccountId(), newChar->GetName(),
        newChar->GetNativeGender(), newChar->GetRace(), newChar->GetClass(), newChar->GetLevel(), false);

    account.CharacterGuid = newChar->GetGUID();
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: created {} {} on account {} ({}).",
        newChar->GetName(), newChar->GetGUID().ToString(), session->GetAccountId(), account.BattlenetEmail);
    return true;
}

bool LoadExistingCharacter(PlayerbotAccount& account)
{
    QueryResult result = CharacterDatabase.Query(Trinity::StringFormat(
        "SELECT guid FROM characters WHERE account = {} AND deleteDate IS NULL", account.AccountId).c_str());
    if (!result)
        return false;

    account.CharacterGuid = ObjectGuid::Create<HighGuid::Player>((*result)[0].GetUInt64());
    CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(account.CharacterGuid);
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: loaded existing character {} {} on account {} ({}).",
        cache ? cache->Name : std::string("<unknown>"), account.CharacterGuid.ToString(),
        account.AccountId, account.BattlenetEmail);
    return true;
}
}

std::string PlayerbotFactory::MakeBattlenetEmail(uint32 index)
{
    std::string email = Trinity::StringFormat("PLAYERBOT{}@PLAYERBOTS.LOCAL", index);
    Utf8ToUpperOnlyLatin(email);
    return email;
}

std::unique_ptr<WorldSession> PlayerbotFactory::MakeSession(PlayerbotAccount const& account)
{
    std::string accountName = account.AccountName;
    std::string email = account.BattlenetEmail;
    std::unique_ptr<WorldSession> session = std::make_unique<WorldSession>(
        account.AccountId,
        std::move(accountName),
        account.BattlenetAccountId,
        std::move(email),
        std::shared_ptr<WorldSocket>{},
        SEC_PLAYER,
        account.Expansion,
        time_t(0),
        std::string("Wn64"),
        Minutes{ 0 },
        GetRealmBuild(),
        GetClientBuildVariant(),
        LOCALE_enUS,
        0,
        false);
    session->LoadPermissions();
    return session;
}

bool PlayerbotFactory::EnsureAccount(PlayerbotAccount& account)
{
    account.BattlenetEmail = MakeBattlenetEmail(account.Index);
    account.Expansion = uint8(sWorld->getIntConfig(CONFIG_EXPANSION));

    if (uint32 existingBnet = Battlenet::AccountMgr::GetId(account.BattlenetEmail))
    {
        account.BattlenetAccountId = existingBnet;
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT_LIST_SMALL);
        stmt->setString(0, account.BattlenetEmail);
        PreparedQueryResult result = LoginDatabase.Query(stmt);
        if (!result)
        {
            TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: Battlenet account {} has no game account.", account.BattlenetEmail);
            return false;
        }

        account.AccountId = (*result)[0].GetUInt32();
        account.AccountName = (*result)[1].GetString();
        return true;
    }

    std::string gameAccountName;
    AccountOpResult created = Battlenet::AccountMgr::CreateBattlenetAccount(account.BattlenetEmail, MakePassword(), true, &gameAccountName);
    if (created != AccountOpResult::AOR_OK)
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: CreateBattlenetAccount failed for {} (result {}).",
            account.BattlenetEmail, uint32(created));
        return false;
    }

    account.BattlenetAccountId = Battlenet::AccountMgr::GetId(account.BattlenetEmail);
    account.AccountName = gameAccountName;
    account.AccountId = AccountMgr::GetId(account.AccountName);
    if (!account.BattlenetAccountId || !account.AccountId)
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: created Battlenet account {} but could not read ids.", account.BattlenetEmail);
        return false;
    }

    LoginDatabase.DirectPExecute("UPDATE account SET expansion = {} WHERE id = {}",
        uint32(account.Expansion), account.AccountId);

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: created Battlenet account {} with game account {} (id {}).",
        account.BattlenetEmail, account.AccountName, account.AccountId);
    return true;
}

bool PlayerbotFactory::EnsureCharacter(PlayerbotAccount& account)
{
    if (LoadExistingCharacter(account))
        return true;

    std::unique_ptr<WorldSession> session = MakeSession(account);
    if (!session)
        return false;

    return CreateCharacter(session.get(), account);
}

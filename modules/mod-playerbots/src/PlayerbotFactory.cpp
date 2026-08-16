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
#include "Config.h"
#include "Containers.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "DBCEnums.h"
#include "Duration.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
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
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
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

std::string NormalizeListName(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
        if (c == ' ' || c == '-' || c == '\'' || c == '_')
            continue;
        out.push_back(charToLower(c));
    }
    return out;
}

std::string_view TrimNameToken(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

bool LocalizedNameMatches(LocalizedString const& loc, std::string const& needle)
{
    char const* en = loc.Str[LOCALE_enUS];
    if (!en || !*en)
        return false;
    return NormalizeListName(en) == needle;
}

std::vector<std::string> ParseNameList(std::string const& raw)
{
    std::vector<std::string> names;
    for (std::string_view token : Trinity::Tokenize(raw, ',', false))
    {
        std::string_view trimmed = TrimNameToken(token);
        if (!trimmed.empty())
            names.emplace_back(trimmed);
    }
    return names;
}

struct CreateFilters
{
    bool LimitRaces = false;
    bool LimitClasses = false;
    std::unordered_set<uint8> Races;
    std::unordered_set<uint8> Classes;
};

CreateFilters LoadCreateFilters()
{
    CreateFilters filter;

    std::vector<std::string> raceNames = ParseNameList(sConfigMgr->GetStringDefault(PLAYERBOTS_RACES, ""));
    if (!raceNames.empty())
    {
        filter.LimitRaces = true;
        for (std::string const& name : raceNames)
        {
            std::string const needle = NormalizeListName(name);
            std::vector<ChrRacesEntry const*> hits;
            for (ChrRacesEntry const* raceEntry : sChrRacesStore)
            {
                if (!raceEntry)
                    continue;
                bool const match = LocalizedNameMatches(raceEntry->Name, needle)
                    || LocalizedNameMatches(raceEntry->NameFemale, needle)
                    || LocalizedNameMatches(raceEntry->NameLowercase, needle)
                    || (raceEntry->ClientFileString && NormalizeListName(raceEntry->ClientFileString) == needle);
                if (match)
                    hits.push_back(raceEntry);
            }

            if (hits.empty())
            {
                TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: unknown race name '{}'. Skipping it.", name);
                continue;
            }

            bool accepted = false;
            for (ChrRacesEntry const* raceEntry : hits)
            {
                if (!IsFirstFactoryRace(raceEntry))
                    continue;
                filter.Races.insert(uint8(raceEntry->ID));
                accepted = true;
            }
            if (!accepted)
                TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: race '{}' is not a Horde level-1 race this factory creates. Skipping it.", name);
        }
    }

    std::vector<std::string> classNames = ParseNameList(sConfigMgr->GetStringDefault(PLAYERBOTS_CLASSES, ""));
    if (!classNames.empty())
    {
        filter.LimitClasses = true;
        for (std::string const& name : classNames)
        {
            std::string const needle = NormalizeListName(name);
            std::vector<ChrClassesEntry const*> hits;
            for (ChrClassesEntry const* classEntry : sChrClassesStore)
            {
                if (!classEntry)
                    continue;
                bool const match = LocalizedNameMatches(classEntry->Name, needle)
                    || LocalizedNameMatches(classEntry->NameMale, needle)
                    || LocalizedNameMatches(classEntry->NameFemale, needle)
                    || (classEntry->Filename && NormalizeListName(classEntry->Filename) == needle);
                if (match)
                    hits.push_back(classEntry);
            }

            if (hits.empty())
            {
                TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: unknown class name '{}'. Skipping it.", name);
                continue;
            }

            bool accepted = false;
            for (ChrClassesEntry const* classEntry : hits)
            {
                if (!IsFirstFactoryClass(classEntry->ID))
                    continue;
                filter.Classes.insert(classEntry->ID);
                accepted = true;
            }
            if (!accepted)
                TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: class '{}' is not a level-1 class this factory creates. Skipping it.", name);
        }
    }

    return filter;
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

std::vector<RaceClassSex> CollectCombos(WorldSession* session, CreateFilters const& filter)
{
    std::vector<RaceClassSex> combos;

    for (ChrRacesEntry const* raceEntry : sChrRacesStore)
    {
        if (!IsFirstFactoryRace(raceEntry))
            continue;
        if (filter.LimitRaces && !filter.Races.contains(uint8(raceEntry->ID)))
            continue;

        for (ChrClassesEntry const* classEntry : sChrClassesStore)
        {
            if (!classEntry || !IsFirstFactoryClass(classEntry->ID))
                continue;
            if (filter.LimitClasses && !filter.Classes.contains(classEntry->ID))
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
    CreateFilters const filter = LoadCreateFilters();
    if (filter.LimitRaces || filter.LimitClasses)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: generation is limited by Playerbots.Races and Playerbots.Classes. Those keys do not change an existing bot character.");

    std::vector<RaceClassSex> combos = CollectCombos(session, filter);
    if (combos.empty())
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: no legal Horde level-1 race/class combo to create. Check Playerbots.Races and Playerbots.Classes in modules/mod-playerbots.conf next to the server (empty means no filter). Those keys only apply when a new bot character is created.");
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

uint32 PlayerbotFactory::DeleteAllBotCharacters()
{
    QueryResult accounts = LoginDatabase.Query(
        "SELECT email FROM battlenet_accounts WHERE email LIKE 'PLAYERBOT%@PLAYERBOTS.LOCAL'");
    if (!accounts)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: no PLAYERBOTn@PLAYERBOTS.LOCAL accounts. Nothing to delete.");
        return 0;
    }

    uint32 deleted = 0;
    do
    {
        std::string email = (*accounts)[0].GetString();
        LoginDatabasePreparedStatement* loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_GAME_ACCOUNT_LIST_SMALL);
        loginStmt->setString(0, email);
        PreparedQueryResult games = LoginDatabase.Query(loginStmt);
        if (!games)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: Battlenet account {} has no game account.", email);
            continue;
        }

        do
        {
            uint32 accountId = (*games)[0].GetUInt32();
            CharacterDatabasePreparedStatement* charStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARS_BY_ACCOUNT_ID);
            charStmt->setUInt32(0, accountId);
            PreparedQueryResult chars = CharacterDatabase.Query(charStmt);
            if (!chars)
                continue;

            do
            {
                ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>((*chars)[0].GetUInt64());
                if (ObjectAccessor::FindPlayer(guid))
                {
                    TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: {} is in the world; not deleting that character.",
                        guid.ToString());
                    continue;
                }

                CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(guid);
                std::string name = cache ? cache->Name : std::string("<unknown>");
                Player::DeleteFromDB(guid, accountId, true, true);
                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: deleted {} {} on account {} ({}).",
                    name, guid.ToString(), accountId, email);
                ++deleted;
            } while (chars->NextRow());
        } while (games->NextRow());
    } while (accounts->NextRow());

    for (uint32 i = 0; i < 200; ++i)
    {
        if (!CharacterDatabase.QueueSize() && !LoginDatabase.QueueSize())
            break;
        std::this_thread::sleep_for(Milliseconds(50));
    }

    if (CharacterDatabase.QueueSize() || LoginDatabase.QueueSize())
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: character delete is still queued after waiting. The process is stopping anyway.");

    return deleted;
}

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
#include "PlayerbotCreateFilter.h"
#include "PlayerbotNameGenerator.h"
#include "PlayerbotWipe.h"
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
#include "Random.h"
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
#include <unordered_set>
#include <vector>

namespace
{
struct RaceClassSex
{
    uint8 Race = 0;
    uint8 Class = 0;
    uint8 Sex = 0;
    PlayerbotFaction Faction = PlayerbotFaction::Horde;
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

PlayerbotClassKind ClassKind(uint8 playerClass)
{
    switch (playerClass)
    {
        case CLASS_DEATH_KNIGHT:
            return PlayerbotClassKind::DeathKnight;
        case CLASS_DEMON_HUNTER:
            return PlayerbotClassKind::DemonHunter;
        case CLASS_EVOKER:
            return PlayerbotClassKind::Evoker;
        case CLASS_HERO:
            return PlayerbotClassKind::Hero;
        case CLASS_REAPER:
            return PlayerbotClassKind::ConquestOfAzeroth;
        default:
            break;
    }

    if (playerClass == CLASS_NONE || playerClass > 32 || !((1u << (playerClass - 1)) & CLASSMASK_ALL_PLAYABLE))
        return PlayerbotClassKind::NotPlayable;

    return PlayerbotClassKind::Ordinary;
}

PlayerbotFaction FactionForTeam(uint32 team)
{
    switch (team)
    {
        case HORDE:
            return PlayerbotFaction::Horde;
        case ALLIANCE:
            return PlayerbotFaction::Alliance;
        default:
            return PlayerbotFaction::Neutral;
    }
}

PlayerbotRaceFacts RaceFacts(ChrRacesEntry const* raceEntry)
{
    PlayerbotRaceFacts facts;
    facts.Id = uint8(raceEntry->ID);
    facts.NpcOnly = raceEntry->GetFlags().HasFlag(ChrRacesFlag::NPCOnly);
    facts.AlliedRace = raceEntry->GetFlags().HasFlag(ChrRacesFlag::IsAlliedRace);
    facts.Dracthyr = raceEntry->ID == RACE_DRACTHYR_ALLIANCE || raceEntry->ID == RACE_DRACTHYR_HORDE;
    facts.Worgen = raceEntry->ID == RACE_WORGEN;
    if (!facts.NpcOnly)
        facts.Faction = FactionForTeam(Player::TeamForRace(raceEntry->ID));
    if (raceEntry->NeutralRaceID > 0 && uint32(raceEntry->NeutralRaceID) != raceEntry->ID
        && Player::TeamForRace(uint8(raceEntry->NeutralRaceID)) == PANDARIA_NEUTRAL)
        facts.NeutralStartRace = uint8(raceEntry->NeutralRaceID);
    return facts;
}

PlayerbotCreateSettings LoadCreateSettings()
{
    PlayerbotCreateSettings settings;
    for (std::size_t i = 0; i < PLAYERBOT_CREATE_SWITCH_COUNT; ++i)
        settings.Switches[i] = sConfigMgr->GetBoolDefault(PLAYERBOT_CREATE_SWITCH_KEYS[i], PLAYERBOT_CREATE_SWITCH_DEFAULTS[i]);
    settings.HordePercent = sConfigMgr->GetIntDefault(PLAYERBOTS_HORDE_PERCENT, PLAYERBOT_HORDE_PERCENT_DEFAULT);
    return settings;
}

PlayerbotCreateRules LoadCreateRules()
{
    PlayerbotCreateRules rules;
    rules.MinLevelForDemonHunter = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_MIN_LEVEL_FOR_DEMON_HUNTER);
    rules.MinLevelForEvoker = sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_MIN_LEVEL_FOR_EVOKER);
    rules.EvokersPerRealm = int32(sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_EVOKERS_PER_REALM));
    return rules;
}

// The keys are read again for every new bot, so each of these is said once per run, not once per bot.
void WarnAboutSettingsOnce(PlayerbotCreateSettings const& settings)
{
    static std::array<bool, PLAYERBOT_CREATE_SWITCH_COUNT> unsupportedSaid{};
    static bool bothOffSaid = false;
    static bool clampSaid = false;
    static bool oneSidedSaid = false;
    static bool pandarenSaid = false;
    static bool allianceSaid = false;
    static bool worgenSaid = false;
    static bool alliedSaid = false;
    static bool deathKnightSaid = false;
    static bool demonHunterSaid = false;
    static bool evokerSaid = false;
    static bool dracthyrSaid = false;

    if (settings.Opens(PlayerbotCreateSwitch::Pandaren) && !pandarenSaid)
    {
        pandarenSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new Pandaren bots start on the Wandering Isle. The bot brain cannot "
            "yet do most of that zone's quests (balance poles, clicking a spirit or a cart, the balloon, escorts), so they will "
            "probably stop partway and not reach the faction choice.", PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Pandaren));
    }

    if (settings.Opens(PlayerbotCreateSwitch::Alliance) && !allianceSaid)
    {
        allianceSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new bots may be Alliance. The bot brain cannot yet do the Dwarf "
            "start's gyrocopter ride or the Gnome start's spellclick, gossip and teleport, so those bots will probably stop partway.",
            PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Alliance));
    }

    if (settings.Opens(PlayerbotCreateSwitch::Worgen) && settings.Opens(PlayerbotCreateSwitch::Alliance) && !worgenSaid)
    {
        worgenSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new Worgen bots start in Gilneas. The bot brain cannot yet do that "
            "start's vehicle and pet-bar steps, so they will probably stop partway.", PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Worgen));
    }

    if (settings.Opens(PlayerbotCreateSwitch::AlliedRaces) && !alliedSaid)
    {
        alliedSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new bots may be allied races, Earthen or Haranir of an open faction. "
            "They start at StartAlliedRacePlayerLevel in their own home, as a player does. The bot brain cannot yet do the Earthen "
            "start's extra action button and gossip or the Haranir start's gossip and spellclick, and several allied-race first "
            "quests have no quest giver in the world database, so those bots will probably stop partway.",
            PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::AlliedRaces));
    }

    if (settings.Opens(PlayerbotCreateSwitch::DeathKnight) && !deathKnightSaid)
    {
        deathKnightSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new bots may be Death Knights, at StartDeathKnightPlayerLevel. Most "
            "races start in Acherus, whose quests ride the Eye of Acherus, horses, a mine cart, a cannon and a dragon; Pandaren and "
            "allied races start on map 2297, which needs an extra action button and gossip. The bot brain cannot yet do those, so "
            "they will probably stop partway.", PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::DeathKnight));
    }

    if (settings.Opens(PlayerbotCreateSwitch::DemonHunter) && !demonHunterSaid)
    {
        demonHunterSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new bots may be Demon Hunters, at StartDemonHunterPlayerLevel in "
            "Mardum. Mardum's phasing moves on only when the client says the intro scene finished, and its quests need gossip, a "
            "player choice and vehicles. The bot brain cannot yet do those, so they will probably stop partway.",
            PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::DemonHunter));
    }

    if (settings.Opens(PlayerbotCreateSwitch::Evoker) && !evokerSaid)
    {
        evokerSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new bots may be Evokers. The Forbidden Reach intro moves on only when "
            "the client says the intro scene finished, which the bot brain does not say yet, so they will probably stay in the intro "
            "room. An empowered spell is pressed once and the server lets it go at its last stage.",
            PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Evoker));
    }

    if (settings.Opens(PlayerbotCreateSwitch::Dracthyr) && !dracthyrSaid)
    {
        dracthyrSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, so new bots may be Dracthyr of an open faction. A Dracthyr Evoker also "
            "needs {}; a Dracthyr of another class starts on map 2785. Neither start has been playtested, so those bots may stop "
            "partway.", PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Dracthyr), PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Evoker));
    }

    for (std::size_t i = 0; i < PLAYERBOT_CREATE_SWITCH_COUNT; ++i)
    {
        PlayerbotCreateSwitch const which = PlayerbotCreateSwitch(i);
        if (!settings.IsOn(which) || settings.IsSupported(which) || unsupportedSaid[i])
            continue;
        unsupportedSaid[i] = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is 1, but bots of that kind have not passed their playtest yet, so it makes "
            "no bots. Existing bots are not changed.", PlayerbotCreateSwitchKey(which));
    }

    bool const horde = settings.IsOn(PlayerbotCreateSwitch::Horde);
    bool const alliance = settings.IsOn(PlayerbotCreateSwitch::Alliance);
    if (!horde && !alliance && !bothOffSaid)
    {
        bothOffSaid = true;
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: {} and {} are both 0, so no new bot character can be made. Existing bots still "
            "log in.", PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Horde), PlayerbotCreateSwitchKey(PlayerbotCreateSwitch::Alliance));
    }

    int32 const percent = ClampPlayerbotHordePercent(settings.HordePercent);
    if (percent != settings.HordePercent && !clampSaid)
    {
        clampSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is {}; it must be 0 to 100, so {} is used.", PLAYERBOTS_HORDE_PERCENT,
            settings.HordePercent, percent);
    }

    if (horde && alliance && (percent == 0 || percent == 100) && !oneSidedSaid)
    {
        oneSidedSaid = true;
        TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: {} is {}, so every new bot is {} and {} has no effect.", PLAYERBOTS_HORDE_PERCENT,
            percent, percent == 0 ? "Alliance" : "Horde",
            PlayerbotCreateSwitchKey(percent == 0 ? PlayerbotCreateSwitch::Horde : PlayerbotCreateSwitch::Alliance));
    }
}

// Every key that decides which bot is made, with its value, for the error when they leave nothing to make.
std::string DescribeCreateKeys(PlayerbotCreateSettings const& settings)
{
    std::string text;
    for (std::size_t i = 0; i < PLAYERBOT_CREATE_SWITCH_COUNT; ++i)
        text += Trinity::StringFormat("{} = {}, ", PLAYERBOT_CREATE_SWITCH_KEYS[i], settings.Switches[i] ? 1 : 0);
    text += Trinity::StringFormat("{} = \"{}\", {} = \"{}\"", PLAYERBOTS_RACES, sConfigMgr->GetStringDefault(PLAYERBOTS_RACES, ""),
        PLAYERBOTS_CLASSES, sConfigMgr->GetStringDefault(PLAYERBOTS_CLASSES, ""));
    return text;
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

CreateFilters LoadCreateFilters(PlayerbotCreateSettings const& settings, PlayerbotCreateRules const& rules)
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

            // One English name can match several rows (an NPC copy, or Pandaren for each faction). The name is only
            // refused when none of them may be made, and the reason given is the first one that is not an NPC row.
            bool accepted = false;
            std::optional<PlayerbotCreateVerdict> refusal;
            for (ChrRacesEntry const* raceEntry : hits)
            {
                PlayerbotCreateVerdict const verdict = JudgePlayerbotRace(RaceFacts(raceEntry), settings, rules);
                if (verdict.Allowed())
                {
                    filter.Races.insert(uint8(raceEntry->ID));
                    accepted = true;
                }
                else if (!refusal || refusal->Refusal == PlayerbotCreateRefusal::NotPlayableRace)
                    refusal = verdict;
            }
            if (!accepted)
                TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: race '{}' in {} is not made for new bots: {}. Skipping it.", name,
                    PLAYERBOTS_RACES, DescribePlayerbotCreateRefusal(*refusal));
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
            std::optional<PlayerbotCreateVerdict> refusal;
            for (ChrClassesEntry const* classEntry : hits)
            {
                PlayerbotCreateVerdict const verdict = JudgePlayerbotClass(ClassKind(classEntry->ID), settings, rules);
                if (verdict.Allowed())
                {
                    filter.Classes.insert(classEntry->ID);
                    accepted = true;
                }
                else if (!refusal || refusal->Refusal == PlayerbotCreateRefusal::NotPlayableClass)
                    refusal = verdict;
            }
            if (!accepted)
                TC_LOG_WARN(PLAYERBOTS_LOG, "mod-playerbots: class '{}' in {} is not made for new bots: {}. Skipping it.", name,
                    PLAYERBOTS_CLASSES, DescribePlayerbotCreateRefusal(*refusal));
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

std::vector<RaceClassSex> CollectCombos(WorldSession* session, CreateFilters const& filter, PlayerbotCreateSettings const& settings,
    PlayerbotCreateRules const& rules)
{
    std::vector<RaceClassSex> combos;

    for (ChrRacesEntry const* raceEntry : sChrRacesStore)
    {
        if (!raceEntry)
            continue;
        PlayerbotRaceFacts const race = RaceFacts(raceEntry);
        if (!JudgePlayerbotRace(race, settings, rules).Allowed())
            continue;
        if (filter.LimitRaces && !filter.Races.contains(uint8(raceEntry->ID)))
            continue;

        for (ChrClassesEntry const* classEntry : sChrClassesStore)
        {
            if (!classEntry || !JudgePlayerbotClass(ClassKind(classEntry->ID), settings, rules).Allowed())
                continue;
            if (filter.LimitClasses && !filter.Classes.contains(classEntry->ID))
                continue;

            if (!PassesCreateChecks(session, raceEntry->ID, classEntry->ID))
                continue;

            for (uint8 sex : { uint8(GENDER_MALE), uint8(GENDER_FEMALE) })
            {
                WorldPackets::Character::CharacterCreateInfo probe;
                probe.Race = raceEntry->ID;
                probe.Class = classEntry->ID;
                probe.Sex = sex;
                if (!FillDefaultCustomizations(session, probe))
                    continue;

                combos.push_back({ uint8(raceEntry->ID), uint8(classEntry->ID), sex, race.Faction });
            }
        }
    }

    return combos;
}

// The game's own name list for her race is tried first. Once those draws keep landing on names that are taken (a
// few hundred bots of one race use it up), a made-up syllable name takes over, so any race can have as many bots as
// the server allows.
constexpr uint32 NAME_LIST_ATTEMPTS = 20;
constexpr uint32 MADE_UP_NAME_ATTEMPTS = 200;

bool IsFreeName(std::string& name)
{
    if (name.empty())
        return false;

    if (!normalizePlayerName(name))
        return false;

    if (ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) != CHAR_NAME_SUCCESS)
        return false;

    if (sObjectMgr->IsReservedName(name))
        return false;

    if (sCharacterCache->GetCharacterCacheByName(name))
        return false;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
    stmt->setString(0, name);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        return false;

    return true;
}

bool PickName(uint8 race, uint8 sex, std::string& name)
{
    for (uint32 attempt = 0; attempt < NAME_LIST_ATTEMPTS; ++attempt)
    {
        name = RandomNameGenName(race, sex);
        if (IsFreeName(name))
            return true;
    }

    auto random = [](uint32 count) { return urand(0, count - 1); };
    for (uint32 attempt = 0; attempt < MADE_UP_NAME_ATTEMPTS; ++attempt)
    {
        name = MakePlayerbotName(random, sex == GENDER_FEMALE);
        if (IsFreeName(name))
            return true;
    }

    return false;
}

// Counts the bot characters already in the database by faction, once per start, so a new bot can go to the faction
// furthest below Playerbots.HordePercent. Only asked when both factions can be made.
void CountRosterFactions(PlayerbotRosterFactions& roster)
{
    roster = {};
    roster.Counted = true;

    std::string accounts;
    // The LIKE only narrows the search; each email must then match exactly.
    if (QueryResult found = LoginDatabase.Query("SELECT a.id, b.email FROM account a JOIN battlenet_accounts b ON a.battlenet_account = b.id "
        "WHERE b.email LIKE 'PLAYERBOT%@PLAYERBOTS.LOCAL'"))
    {
        do
        {
            if (!IsPlayerbotBattlenetEmail((*found)[1].GetString()))
                continue;
            if (!accounts.empty())
                accounts += ',';
            accounts += std::to_string((*found)[0].GetUInt32());
        } while (found->NextRow());
    }

    if (!accounts.empty())
    {
        if (QueryResult characters = CharacterDatabase.Query(Trinity::StringFormat(
            "SELECT race FROM characters WHERE deleteDate IS NULL AND account IN ({})", accounts).c_str()))
        {
            do
            {
                switch (Player::TeamForRace((*characters)[0].GetUInt8()))
                {
                    case HORDE:
                        ++roster.Horde;
                        break;
                    case ALLIANCE:
                        ++roster.Alliance;
                        break;
                    default:
                        break;
                }
            } while (characters->NextRow());
        }
    }

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: the roster has {} Horde and {} Alliance bot character(s); new bots move it toward {} = {}.",
        roster.Horde, roster.Alliance, PLAYERBOTS_HORDE_PERCENT, ClampPlayerbotHordePercent(sConfigMgr->GetIntDefault(PLAYERBOTS_HORDE_PERCENT,
        PLAYERBOT_HORDE_PERCENT_DEFAULT)));
}

bool CreateCharacter(WorldSession* session, PlayerbotAccount& account, PlayerbotRosterFactions& roster)
{
    PlayerbotCreateSettings const settings = LoadCreateSettings();
    PlayerbotCreateRules const rules = LoadCreateRules();
    WarnAboutSettingsOnce(settings);

    CreateFilters const filter = LoadCreateFilters(settings, rules);
    if (filter.LimitRaces || filter.LimitClasses)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: generation is limited by Playerbots.Races and Playerbots.Classes. Those keys do not change an existing bot character.");

    std::vector<RaceClassSex> const combos = CollectCombos(session, filter, settings, rules);
    if (combos.empty())
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: no legal race and class is left to make a new bot with {}. Check those keys in "
            "modules/mod-playerbots.conf next to the server (an empty list means no filter). They only apply when a new bot character "
            "is created.", DescribeCreateKeys(settings));
        return false;
    }

    // A neutral race chooses its faction at the end of its start, by the same split, so it may be made for either side
    // that is open to her.
    std::vector<RaceClassSex> hordeCombos;
    std::vector<RaceClassSex> allianceCombos;
    for (RaceClassSex const& combo : combos)
    {
        if (combo.Faction != PlayerbotFaction::Alliance && (combo.Faction != PlayerbotFaction::Neutral || settings.Opens(PlayerbotCreateSwitch::Horde)))
            hordeCombos.push_back(combo);
        if (combo.Faction != PlayerbotFaction::Horde && (combo.Faction != PlayerbotFaction::Neutral || settings.Opens(PlayerbotCreateSwitch::Alliance)))
            allianceCombos.push_back(combo);
    }

    if (!hordeCombos.empty() && !allianceCombos.empty() && !roster.Counted)
        CountRosterFactions(roster);

    std::optional<PlayerbotFaction> const faction = PickPlayerbotFaction(!hordeCombos.empty(), !allianceCombos.empty(),
        settings.HordePercent, roster.Horde, roster.Alliance);
    std::vector<RaceClassSex> const& pool = faction == PlayerbotFaction::Alliance ? allianceCombos : hordeCombos;
    if (pool.empty())
    {
        TC_LOG_ERROR(PLAYERBOTS_LOG, "mod-playerbots: no faction is open to the races left to make a new bot with {}.",
            DescribeCreateKeys(settings));
        return false;
    }
    RaceClassSex const& pick = Trinity::Containers::SelectRandomContainerElement(pool);

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

    if (roster.Counted)
        ++(faction == PlayerbotFaction::Alliance ? roster.Alliance : roster.Horde);

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

bool PlayerbotFactory::EnsureCharacter(PlayerbotAccount& account, PlayerbotRosterFactions& roster)
{
    if (LoadExistingCharacter(account))
        return true;

    std::unique_ptr<WorldSession> session = MakeSession(account);
    if (!session)
        return false;

    return CreateCharacter(session.get(), account, roster);
}

void PlayerbotFactory::CheckCreateSettings()
{
    WarnAboutSettingsOnce(LoadCreateSettings());
}

std::optional<PlayerbotFaction> PlayerbotFactory::PickNeutralStartFaction(uint32 hordeBots, uint32 allianceBots)
{
    return PickPlayerbotNeutralStartFaction(LoadCreateSettings(), hordeBots, allianceBots);
}

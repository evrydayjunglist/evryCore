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

// Makes and removes a Reaper without the character creation screen.
//
// The stock retail client will not offer a Reaper on that screen, because a
// compiled check in the client rejects the class id. Widening it is a memory
// patch applied to the running game from evryLoader, so this command is the way
// to put a Reaper in the world without one. `.coa reaper make` builds it on
// your own account through the same Player::Create the real creation path uses,
// so the character is a normal character in every way except that no unpatched
// client could have made it.
//
// `.coa reaper unmake` removes it again, and runs from the server console as
// well as in game. That matters: if a Reaper stops the client from drawing the
// character list, you cannot log in to undo it, and the console is the way
// back. It only removes a character whose stored class is the current Reaper
// class, so a character left behind at an older class id has to be removed by
// the server that still knows that id.

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "DB2Stores.h"
#include "DBCEnums.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "RaceMask.h"
#include "RealmList.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <memory>

using namespace Trinity::ChatCommands;

namespace
{
    struct ReaperRace
    {
        std::string_view Name;
        uint8 Race;
    };

    // The five races Conquest of Azeroth ships officially. This list only
    // decides what the command will accept as a word; whether the combination
    // is legal is still the server's own check further down.
    constexpr std::array<ReaperRace, 6> ReaperRaces =
    { {
        { "human",    RACE_HUMAN      },
        { "undead",   RACE_UNDEAD_PLAYER },
        { "scourge",  RACE_UNDEAD_PLAYER },
        { "troll",    RACE_TROLL      },
        { "bloodelf", RACE_BLOODELF   },
        { "draenei",  RACE_DRAENEI    },
    } };

    // Undead when nothing is given. The owner plays Horde, and of the Horde
    // three it is the one the class reads as.
    constexpr uint8 DefaultReaperRace = RACE_UNDEAD_PLAYER;

    bool ResolveRace(std::string const& word, uint8& race)
    {
        std::string wanted;
        wanted.reserve(word.size());
        for (char c : word)
            if (c != ' ' && c != '_' && c != '-')
                wanted.push_back(char(std::tolower(unsigned char(c))));

        for (ReaperRace const& candidate : ReaperRaces)
        {
            if (candidate.Name == wanted)
            {
                race = candidate.Race;
                return true;
            }
        }

        return false;
    }

    // Picks the first appearance the server will accept for this race, sex and
    // class, the same way the bot factory does. Character creation refuses a
    // character whose appearance does not validate, and nothing had ever picked
    // one for Reaper before, so this is the step most likely to say no.
    bool FillDefaultCustomizations(WorldSession* session, WorldPackets::Character::CharacterCreateInfo& createInfo)
    {
        std::vector<ChrCustomizationOptionEntry const*> const* options =
            sDB2Manager.GetCustomiztionOptions(createInfo.Race, createInfo.Sex);
        if (!options)
            return false;

        Races const race = Races(createInfo.Race);
        Classes const playerClass = Classes(createInfo.Class);

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
}

class coa_reaper_commandscript : public CommandScript
{
public:
    coa_reaper_commandscript() : CommandScript("coa_reaper_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable reaperTable =
        {
            // Making one needs your session, so it is in game only.
            { "make",   HandleReaperMakeCommand,   rbac::RBAC_PERM_COMMAND_CHARACTER_CUSTOMIZE, Console::No  },
            // Removing one must work from the console, because a broken
            // character list would otherwise lock you out of your own account.
            { "unmake", HandleReaperUnmakeCommand, rbac::RBAC_PERM_COMMAND_CHARACTER_ERASE,     Console::Yes },
        };
        static ChatCommandTable coaTable =
        {
            { "reaper", reaperTable },
        };
        static ChatCommandTable commandTable =
        {
            { "coa", coaTable },
        };
        return commandTable;
    }

    static bool HandleReaperMakeCommand(ChatHandler* handler, std::string name, Optional<std::string> raceWord)
    {
        WorldSession* session = handler->GetSession();
        if (!session)
        {
            handler->SendSysMessage("Making a Reaper needs a logged in account, so this only works in game. Use it from your own client.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint8 race = DefaultReaperRace;
        if (raceWord && !ResolveRace(*raceWord, race))
        {
            handler->PSendSysMessage("%s is not a Reaper race. Pick one of Human, Undead, Troll, BloodElf or Draenei.", raceWord->c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!normalizePlayerName(name))
        {
            handler->SendSysMessage("That name cannot be used for a character.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (ObjectMgr::CheckPlayerName(name, session->GetSessionDbcLocale(), true) != CHAR_NAME_SUCCESS)
        {
            handler->PSendSysMessage("%s is not a name the server will accept.", name.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (sObjectMgr->IsReservedName(name) || !sCharacterCache->GetCharacterGuidByName(name).IsEmpty())
        {
            handler->PSendSysMessage("%s is already taken or reserved. Pick another name.", name.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Without these the create would fail deeper in, with a less useful
        // message. They are exactly the rows the world SQL for this class adds.
        if (!sObjectMgr->GetPlayerInfo(race, CLASS_REAPER))
        {
            handler->PSendSysMessage("There is no playercreateinfo row for race %u and class %u, so a Reaper of that race has nowhere to start.", uint32(race), uint32(CLASS_REAPER));
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!sObjectMgr->GetClassExpansionRequirement(race, CLASS_REAPER))
        {
            handler->PSendSysMessage("There is no class_expansion_requirement row for race %u and class %u, so creation would be refused.", uint32(race), uint32(CLASS_REAPER));
            handler->SetSentErrorMessage(true);
            return false;
        }

        WorldPackets::Character::CharacterCreateInfo createInfo;
        createInfo.Name = name;
        createInfo.Race = race;
        createInfo.Class = CLASS_REAPER;
        createInfo.Sex = GENDER_MALE;
        createInfo.UseNPE = false;

        if (!FillDefaultCustomizations(session, createInfo))
        {
            handler->PSendSysMessage("No appearance the server accepts could be built for race %u as a Reaper. That is worth writing down: it means the customization data refuses class %u.", uint32(race), uint32(CLASS_REAPER));
            handler->SetSentErrorMessage(true);
            return false;
        }

        CharacterDatabasePreparedStatement* countStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_SUM_CHARS);
        countStmt->setUInt32(0, session->GetAccountId());
        if (PreparedQueryResult countResult = CharacterDatabase.Query(countStmt))
            createInfo.CharCount = uint8((*countResult)[0].GetUInt64());

        if (createInfo.CharCount >= sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM))
        {
            handler->SendSysMessage("Your account is already at the realm character limit. Delete a character first.");
            handler->SetSentErrorMessage(true);
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
            handler->SendSysMessage("Player::Create refused the Reaper. Server.log has the reason, and that reason is the answer this experiment wanted.");
            handler->SetSentErrorMessage(true);
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
            handler->SendSysMessage("Saving the Reaper failed. Nothing was created.");
            handler->SetSentErrorMessage(true);
            return false;
        }

        LoginDatabase.CommitTransaction(loginTransaction);

        sScriptMgr->OnPlayerCreate(newChar.get());
        sCharacterCache->AddCharacterCacheEntry(newChar->GetGUID(), session->GetAccountId(), newChar->GetName(),
            newChar->GetNativeGender(), newChar->GetRace(), newChar->GetClass(), newChar->GetLevel(), false);

        handler->PSendSysMessage("Made the Reaper %s (%s) on your account. Log out to the character list to see what the client does with it.",
            newChar->GetName().c_str(), newChar->GetGUID().ToString().c_str());
        handler->PSendSysMessage("If the client will not draw the character list, run: .coa reaper unmake %s from the server console.", newChar->GetName().c_str());

        TC_LOG_INFO("server.worldserver", "mod-coa: made Reaper {} {} on account {}",
            newChar->GetName(), newChar->GetGUID().ToString(), session->GetAccountId());
        return true;
    }

    static bool HandleReaperUnmakeCommand(ChatHandler* handler, PlayerIdentifier player)
    {
        // Only ever a Reaper. This command exists to undo the experiment, not
        // to delete characters, and it must not become a second .character
        // erase that happens to have a shorter name.
        std::string cachedName;
        uint8 characterClass = CLASS_NONE;
        if (!sCharacterCache->GetCharacterNameAndClassByGUID(player, cachedName, characterClass))
        {
            handler->PSendSysMessage("There is no character called %s.", player.GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (characterClass != CLASS_REAPER)
        {
            handler->PSendSysMessage("%s is class %u, not a Reaper. This command only removes Reapers.",
                player.GetName().c_str(), uint32(characterClass));
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (player.IsConnected())
        {
            handler->PSendSysMessage("%s is online. Log that character out first; this command will not touch a character that is in the world.",
                player.GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 const accountId = sCharacterCache->GetCharacterAccountIdByGuid(player);
        std::string accountName;
        AccountMgr::GetName(accountId, accountName);

        Player::DeleteFromDB(player, accountId, true, true);

        handler->PSendSysMessage("Removed the Reaper %s (%s) from account %s (%u).",
            player.GetName().c_str(), player.GetGUID().ToString().c_str(), accountName.c_str(), accountId);
        TC_LOG_INFO("server.worldserver", "mod-coa: removed Reaper {} {} from account {}",
            player.GetName(), player.GetGUID().ToString(), accountId);
        return true;
    }
};

void AddCoaReaperCommandScripts()
{
    new coa_reaper_commandscript();
}

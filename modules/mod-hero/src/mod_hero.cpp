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

#include "ChatPackets.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Hero, class 16, and the progression that will eventually fill it.
//
// Hero comes from Conquest of Azeroth, but it does not live in mod-coa beside
// Reaper, and that is deliberate. Reaper is a class with a kit. Hero is a
// container for a character who has no class: what a Hero can do is meant to
// come from Free Pick and Wildcard, an economy of ability and talent essence
// spent on abilities borrowed from every other class. That system will be
// larger than everything in mod-coa, it needs settings of its own, and it has
// nothing to do with Reaper. Keeping it separate also means Hero's data can be
// turned off on its own, because the updater applies a module's SQL only while
// that module is enabled.
//
// One deliberate difference from Conquest of Azeroth: there, Hero is what you
// are on a classless realm and it replaces the class system. Here it is a
// sixteenth class you pick instead of Paladin or Mage. That is a product
// decision, not an oversight, and it is why there is no realm-wide classless
// mode anywhere in this module.
//
// The class rows live under data/sql. There are no commands on purpose:
// character creation for a class beyond the client's own fifteen is proven, so
// a Hero can be made on the creation screen the way any character is, and
// `.character erase` already removes one from the server console if a Hero ever
// stops the client drawing the character list.
//
//
// The eleven resources the client cannot see.
//
// A class may hold at most ten resources, because that is how many slots the
// client keeps per class and how many the update fields carry. Hero's ten are
// in data/sql/db-hotfixes and they are full: Mana, Rage, Focus, Energy, Combo
// Points, Soul Shards, and the four alternate bars. Twenty-one power types are
// live, so eleven are left over, and those eleven are what this part of the
// module is for: Runes, Runic Power, Lunar Power, Holy Power, Maelstrom, Chi,
// Insanity, Arcane Charges, Fury, Pain and Essence.
//
// The split of work is that the core holds those values and spends them, and
// this module decides everything else. The core learns, from
// DB2Manager::SetExtraPowersForClass below, that a power can live somewhere
// other than the ten wire slots; after that Unit::GetPower and its three
// companions answer from that store, spells check and take from it, and it
// regenerates. This module owns which powers are extra, what their maximums
// are, when they are saved and loaded, and what a panel on screen is told.
//
// The client is never told about them. It has no slot to put them in, it will
// not draw a bar for one, and UnitPower will not return one. What it does do,
// with the cost gate patch from evryLoader applied to the running process, is
// stop refusing a cast whose cost is in a power its class table lacks, so the
// cast reaches the server and the server decides. Nothing here is free: a Hero
// pays real Runic Power for Death Coil out of Runic Power it built.
//
// What a panel on screen is told. Every 500 milliseconds this module looks at
// each logged-in character whose class has extra powers and sends what changed
// down the addon channel, as a whisper in the addon language carrying the
// prefix in PanelPrefix below. The addon must register that prefix. Each
// message is one record, semicolon separated, with no newline in it:
//
//   P;<powerType>;<current>;<maximum>     one power, by power type number
//   R;<readyRunes>;<totalRunes>           the rune state, when the character has runes
//
// Every power the character holds is sent, the ten the client knows about as
// well as the eleven it does not, so a panel can draw all twenty-one the same
// way from one source. Only records whose numbers changed are sent, and every
// ten seconds all of them go out again so a panel that loaded late catches up.
// The numbers are the server's own, so a type whose PowerType row has a display
// modifier, such as Runic Power at ten, arrives as 1000 rather than 100; the
// panel divides.

namespace
{
    // The world table that says which powers a class holds beside the ten the client knows about.
    // It is a world table and never a hotfix table, because the client must never see it.
    constexpr char const* ExtraPowerQuery = "SELECT `ClassID`, `PowerType` FROM `class_extra_power` ORDER BY `ClassID`, `PowerType`";

    // The prefix a panel addon registers to hear the message described above.
    constexpr char const* PanelPrefix = "evryHeroPower";

    // How often a character's panel may be told anything, and how often everything is sent again
    // whether it changed or not.
    constexpr uint32 PanelIntervalMs = 500;
    constexpr uint32 PanelFullRefreshMs = 10000;

    // One bit per power type, indexed by class. This is what the core was told, read back from it,
    // so a power that already holds one of the ten slots has been dropped from it.
    std::array<uint32, MAX_CLASSES> ExtraPowersByClass = { };

    // No row in character_hero_power for that power.
    constexpr int32 NoSavedValue = std::numeric_limits<int32>::min();

    bool ClassHasExtraPowers(uint8 classId)
    {
        return classId < MAX_CLASSES && ExtraPowersByClass[classId] != 0;
    }

    template<typename Action>
    void ForEachExtraPower(uint8 classId, Action action)
    {
        if (classId >= MAX_CLASSES)
            return;

        uint32 mask = ExtraPowersByClass[classId];
        for (uint32 power = 0; power < MAX_POWERS; ++power)
            if (mask & (1u << power))
                action(Powers(power));
    }

    void LoadExtraPowers()
    {
        ExtraPowersByClass.fill(0);

        uint32 rowsRead = 0;
        if (QueryResult result = WorldDatabase.Query(ExtraPowerQuery))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 classId = fields[0].GetUInt8();
                uint32 powerType = fields[1].GetUInt8();

                if (classId >= MAX_CLASSES)
                {
                    TC_LOG_ERROR("server.loading", "mod-hero: class_extra_power names class {}, which this server does not have. Row skipped.", classId);
                    continue;
                }

                if (powerType >= MAX_POWERS || !sDB2Manager.GetPowerTypeEntry(Powers(powerType)))
                {
                    TC_LOG_ERROR("server.loading", "mod-hero: class_extra_power names power type {} for class {}, which this client does not have. Row skipped.", powerType, classId);
                    continue;
                }

                ExtraPowersByClass[classId] |= 1u << powerType;
                ++rowsRead;
            }
            while (result->NextRow());
        }

        for (uint32 classId = 0; classId < MAX_CLASSES; ++classId)
        {
            DB2Manager::SetExtraPowersForClass(classId, ExtraPowersByClass[classId]);

            // Read back what the core kept. It drops any power that already holds one of the ten
            // slots, so from here on this module and the core agree on what an extra is.
            ExtraPowersByClass[classId] = DB2Manager::GetExtraPowersForClass(classId);
        }

        TC_LOG_INFO("server.loading", "mod-hero: read {} rows from class_extra_power", rowsRead);

        for (uint32 classId = 0; classId < MAX_CLASSES; ++classId)
        {
            if (!ExtraPowersByClass[classId])
                continue;

            std::string names;
            uint32 count = 0;
            ForEachExtraPower(uint8(classId), [&](Powers power)
            {
                PowerTypeEntry const* entry = sDB2Manager.GetPowerTypeEntry(power);
                if (!names.empty())
                    names += ", ";
                names += Trinity::StringFormat("{} ({})", entry->NameGlobalStringTag, AsUnderlyingType(power));
                ++count;
            });

            TC_LOG_INFO("server.loading", "mod-hero: class {} holds {} resources beside the ten the client knows about: {}", classId, count, names);

            // What each of them will actually do, read out of the client's own PowerType row, so it
            // can be seen without starting a game. A negative out-of-combat rate means the resource
            // drains to nothing when it is not being built, which is how most of these are meant to
            // work.
            ForEachExtraPower(uint8(classId), [&](Powers power)
            {
                PowerTypeEntry const* entry = sDB2Manager.GetPowerTypeEntry(power);
                TC_LOG_INFO("server.loading", "mod-hero:   {} ({}) has a maximum of {}, regenerates {} a second out of combat and {} in combat, and is shown divided by {}",
                    entry->NameGlobalStringTag, AsUnderlyingType(power), entry->MaxBasePower, entry->RegenPeace, entry->RegenCombat, entry->DisplayModifier);
            });
        }
    }

    // The maximum for each extra power. This is the arithmetic Player::UpdateMaxPower does, and it
    // works unchanged here because UnitMods is indexed by power type rather than by slot.
    // GetCreatePowerValue is the PowerType row's own MaxBasePower for everything except Mana, and
    // all eleven of Hero's extras have a non-zero one, so nothing falls back to anything.
    void ApplyExtraMaxPowers(Player* player)
    {
        ForEachExtraPower(player->GetClass(), [player](Powers power)
        {
            UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + AsUnderlyingType(power));

            float value = player->GetFlatModifierValue(unitMod, BASE_VALUE) + player->GetCreatePowerValue(power);
            value *= player->GetPctModifierValue(unitMod, BASE_PCT);
            value += player->GetFlatModifierValue(unitMod, TOTAL_VALUE);
            value *= player->GetPctModifierValue(unitMod, TOTAL_PCT);

            player->SetMaxPower(power, int32(std::lroundf(value)));
        });
    }

    // Runes are the one extra with machinery of its own. Player::InitRunes runs during login, before
    // this module has said how many runes the character has, so it comes out of that with a rune
    // store built but no runes in it. Building it again now that the maximum is set leaves every
    // rune ready, and the ones that were not saved as ready then go on cooldown.
    void RestoreRunes(Player* player, int32 readyRunes)
    {
        player->InitRunes();

        int32 totalRunes = std::min(player->GetMaxPower(POWER_RUNES), int32(MAX_RUNES));
        readyRunes = std::clamp(readyRunes, 0, totalRunes);

        uint32 cooldown = player->GetRuneBaseCooldown();
        for (int32 rune = readyRunes; rune < totalRunes; ++rune)
            player->SetRuneCooldown(uint8(rune), cooldown);
    }

    // What an extra power starts at for a character that has never saved one. This is what the core
    // does for the powers a class holds in a slot: the ones whose PowerType row says so start full,
    // and everything else starts at nothing and builds from play.
    int32 InitialPowerValue(Player* player, Powers power)
    {
        PowerTypeEntry const* entry = sDB2Manager.GetPowerTypeEntry(power);
        if (entry && entry->GetFlags().HasFlag(PowerTypeFlags::SetToMaxOnInitialLogIn))
            return player->GetMaxPower(power);

        return 0;
    }

    void ReadSavedPowers(Player* player, std::array<int32, MAX_POWERS>& saved)
    {
        saved.fill(NoSavedValue);

        QueryResult result = CharacterDatabase.Query(Trinity::StringFormat(
            "SELECT `power`, `value` FROM `character_hero_power` WHERE `guid` = {}", player->GetGUID().GetCounter()).c_str());
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            uint32 power = fields[0].GetUInt8();
            if (power >= MAX_POWERS)
                continue;

            saved[power] = fields[1].GetInt32();
        }
        while (result->NextRow());
    }

    // The maximum is recomputed rather than restored, so the saved one is only a record of what it
    // was. The current value is set after it, because SetPower clamps to the maximum.
    void RestoreExtraPowers(Player* player)
    {
        if (!ClassHasExtraPowers(player->GetClass()))
            return;

        ApplyExtraMaxPowers(player);

        std::array<int32, MAX_POWERS> saved;
        ReadSavedPowers(player, saved);

        ForEachExtraPower(player->GetClass(), [&](Powers power)
        {
            int32 value = saved[power] != NoSavedValue ? saved[power] : InitialPowerValue(player, power);

            if (power == POWER_RUNES)
                RestoreRunes(player, value);
            else
                player->SetPower(power, value);
        });
    }

    // Every table and column name in this file is written between backticks, and has to stay that
    // way. `maxvalue` is a reserved word in MySQL, so without them this statement is a syntax error,
    // and a statement the core cannot parse takes the whole server down rather than failing the one
    // save. That happened once, on a Hero's first save on 20 September.
    void SaveExtraPowers(Player* player)
    {
        if (!ClassHasExtraPowers(player->GetClass()))
            return;

        CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
        ForEachExtraPower(player->GetClass(), [&](Powers power)
        {
            transaction->Append(Trinity::StringFormat(
                "REPLACE INTO `character_hero_power` (`guid`, `power`, `value`, `maxvalue`) VALUES ({}, {}, {}, {})",
                player->GetGUID().GetCounter(), AsUnderlyingType(power), player->GetPower(power), player->GetMaxPower(power)).c_str());
        });
        CharacterDatabase.CommitTransaction(transaction);
    }

    // What was last sent to one character's panel, so only changes go out.
    struct PanelState
    {
        PanelState()
        {
            LastValue.fill(NoSavedValue);
            LastMaxValue.fill(NoSavedValue);
        }

        std::array<int32, MAX_POWERS> LastValue;
        std::array<int32, MAX_POWERS> LastMaxValue;
        int32 LastReadyRunes = NoSavedValue;
        int32 LastTotalRunes = NoSavedValue;
    };

    std::unordered_map<ObjectGuid, PanelState> PanelStates;
    uint32 PanelTimer = 0;
    uint32 PanelRefreshTimer = 0;

    void SendPanelRecord(Player* player, std::string const& record)
    {
        WorldPackets::Chat::Chat chat;
        chat.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, record, 0, "", LOCALE_enUS, PanelPrefix);
        player->SendDirectMessage(chat.Write());
    }

    uint32 SendPanel(Player* player, PanelState& state, bool fullRefresh)
    {
        uint32 sent = 0;

        auto sendPower = [&](Powers power)
        {
            int32 type = AsUnderlyingType(power);
            int32 value = player->GetPower(power);
            int32 maxValue = player->GetMaxPower(power);

            if (!fullRefresh && state.LastValue[type] == value && state.LastMaxValue[type] == maxValue)
                return;

            state.LastValue[type] = value;
            state.LastMaxValue[type] = maxValue;
            SendPanelRecord(player, Trinity::StringFormat("P;{};{};{}", type, value, maxValue));
            ++sent;
        };

        for (Powers power : player->GetPowerTypes())
            sendPower(power);

        ForEachExtraPower(player->GetClass(), sendPower);

        if (!player->HasRunes())
            return sent;

        int32 totalRunes = std::min(player->GetMaxPower(POWER_RUNES), int32(MAX_RUNES));
        int32 readyRunes = 0;
        for (int32 rune = 0; rune < totalRunes; ++rune)
            if (player->GetRuneCooldown(uint8(rune)) == 0)
                ++readyRunes;

        if (!fullRefresh && state.LastReadyRunes == readyRunes && state.LastTotalRunes == totalRunes)
            return sent;

        state.LastReadyRunes = readyRunes;
        state.LastTotalRunes = totalRunes;
        SendPanelRecord(player, Trinity::StringFormat("R;{};{}", readyRunes, totalRunes));
        return sent + 1;
    }

    void UpdatePanels(uint32 diff)
    {
        PanelTimer += diff;
        if (PanelTimer < PanelIntervalMs)
            return;

        PanelRefreshTimer += PanelTimer;
        PanelTimer = 0;

        bool fullRefresh = PanelRefreshTimer >= PanelFullRefreshMs;
        if (fullRefresh)
            PanelRefreshTimer = 0;

        std::unordered_set<ObjectGuid> stillHere;
        for (auto const& sessionEntry : sWorld->GetAllSessions())
        {
            Player* player = sessionEntry.second->GetPlayer();
            if (!player || !player->IsInWorld())
                continue;

            if (!ClassHasExtraPowers(player->GetClass()))
                continue;

            stillHere.insert(player->GetGUID());

            // The first send to a character goes out in full. Saying so here is the only way to tell a
            // panel that is not listening from one that was never sent anything.
            bool firstSend = !PanelStates.contains(player->GetGUID());
            uint32 sent = SendPanel(player, PanelStates[player->GetGUID()], fullRefresh);
            if (firstSend)
                TC_LOG_INFO("server.loading", "mod-hero: sent {} resources to {} on the {} addon channel", sent, player->GetName(), PanelPrefix);
        }

        std::erase_if(PanelStates, [&](auto const& entry) { return !stillHere.contains(entry.first); });
    }
}

class HeroWorldScript : public WorldScript
{
public:
    HeroWorldScript() : WorldScript("mod_hero_WorldScript") { }

    void OnStartup() override
    {
        LoadExtraPowers();
        TC_LOG_INFO("server.loading", "mod-hero: loaded");
    }

    void OnUpdate(uint32 diff) override
    {
        UpdatePanels(diff);
    }
};

class HeroPlayerScript : public PlayerScript
{
public:
    HeroPlayerScript() : PlayerScript("mod_hero_PlayerScript") { }

    void OnCreate(Player* player) override
    {
        // A new character has nothing saved, so this takes the starting value of each resource, and
        // then writes the rows a first login will read back.
        RestoreExtraPowers(player);
        SaveExtraPowers(player);
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        RestoreExtraPowers(player);
    }

    void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!ClassHasExtraPowers(player->GetClass()))
            return;

        // The maximum can move with level, and the core has already raised the powers that live in a
        // slot. Keep the current value where it is unless the new maximum is below it, which
        // SetMaxPower handles, and fill the ones whose PowerType row says a level up fills them.
        ApplyExtraMaxPowers(player);

        ForEachExtraPower(player->GetClass(), [player](Powers power)
        {
            PowerTypeEntry const* entry = sDB2Manager.GetPowerTypeEntry(power);
            if (entry && entry->GetFlags().HasFlag(PowerTypeFlags::SetToMaxOnLevelUp))
                player->SetPower(power, player->GetMaxPower(power));
        });
    }

    void OnSave(Player* player) override
    {
        SaveExtraPowers(player);
    }

    void OnLogout(Player* player) override
    {
        SaveExtraPowers(player);
        PanelStates.erase(player->GetGUID());
    }

    void OnDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        CharacterDatabase.Execute(Trinity::StringFormat(
            "DELETE FROM `character_hero_power` WHERE `guid` = {}", guid.GetCounter()).c_str());
    }
};

void Addmod_heroScripts()
{
    new HeroWorldScript();
    new HeroPlayerScript();
}

/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotCreateFilter.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    using Switch = PlayerbotCreateSwitch;

    PlayerbotRaceFacts Race(uint8 id, PlayerbotFaction faction, bool allied = false, bool dracthyr = false,
        bool npcOnly = false, uint8 neutralStartRace = 0)
    {
        PlayerbotRaceFacts race;
        race.Id = id;
        race.Faction = faction;
        race.AlliedRace = allied;
        race.Dracthyr = dracthyr;
        race.NpcOnly = npcOnly;
        race.NeutralStartRace = neutralStartRace;
        return race;
    }

    PlayerbotRaceFacts const NEUTRAL_PANDAREN = Race(24, PlayerbotFaction::Neutral);
    PlayerbotRaceFacts const HORDE_PANDAREN = Race(26, PlayerbotFaction::Horde, false, false, false, 24);
    PlayerbotRaceFacts const ALLIANCE_PANDAREN = Race(25, PlayerbotFaction::Alliance, false, false, false, 24);

    PlayerbotRaceFacts Worgen()
    {
        PlayerbotRaceFacts race = Race(22, PlayerbotFaction::Alliance);
        race.Worgen = true;
        return race;
    }

    PlayerbotRaceFacts const WORGEN = Worgen();

    // Made-up rows, one of each kind the factory meets. Ids are only labels here.
    std::vector<PlayerbotRaceFacts> const RACES =
    {
        Race(2, PlayerbotFaction::Horde),                                 // Orc
        Race(1, PlayerbotFaction::Alliance),                              // Human
        WORGEN,
        NEUTRAL_PANDAREN,
        HORDE_PANDAREN,
        ALLIANCE_PANDAREN,
        Race(28, PlayerbotFaction::Horde, true),                          // Highmountain Tauren
        Race(29, PlayerbotFaction::Alliance, true),                       // Void Elf
        Race(70, PlayerbotFaction::Horde, false, true),                   // Dracthyr (Horde), no allied flag
        Race(52, PlayerbotFaction::Alliance, true, true),                 // Dracthyr (Alliance), allied flag too
        Race(99, PlayerbotFaction::Horde, false, false, true),            // a monster race
    };

    std::vector<PlayerbotClassKind> const CLASSES =
    {
        PlayerbotClassKind::Ordinary,
        PlayerbotClassKind::DeathKnight,
        PlayerbotClassKind::DemonHunter,
        PlayerbotClassKind::Evoker,
        PlayerbotClassKind::Hero,
        PlayerbotClassKind::ConquestOfAzeroth,
        PlayerbotClassKind::NotPlayable,
    };

    PlayerbotCreateSettings AllSupported(uint32 switchBits)
    {
        PlayerbotCreateSettings settings;
        for (std::size_t i = 0; i < PLAYERBOT_CREATE_SWITCH_COUNT; ++i)
        {
            settings.Switches[i] = (switchBits >> i) & 1;
            settings.Supported[i] = true;
        }
        return settings;
    }

    bool On(uint32 bits, Switch which) { return (bits >> uint32(which)) & 1; }

    // What the contract says, written out separately from the code under test.
    bool Expected(PlayerbotRaceFacts const& race, PlayerbotClassKind kind, uint32 bits)
    {
        if (race.NpcOnly || race.NeutralStartRace || kind == PlayerbotClassKind::NotPlayable)
            return false;
        if (race.Faction == PlayerbotFaction::Neutral)
        {
            if (!On(bits, Switch::Pandaren) || (!On(bits, Switch::Horde) && !On(bits, Switch::Alliance)))
                return false;
        }
        else if (!On(bits, race.Faction == PlayerbotFaction::Horde ? Switch::Horde : Switch::Alliance))
            return false;
        if (race.Dracthyr && !On(bits, Switch::Dracthyr))
            return false;
        if (!race.Dracthyr && race.AlliedRace && !On(bits, Switch::AlliedRaces))
            return false;
        if (race.Worgen && !On(bits, Switch::Worgen))
            return false;
        switch (kind)
        {
            case PlayerbotClassKind::DeathKnight: return On(bits, Switch::DeathKnight);
            case PlayerbotClassKind::DemonHunter: return On(bits, Switch::DemonHunter);
            case PlayerbotClassKind::Evoker: return On(bits, Switch::Evoker);
            case PlayerbotClassKind::Hero: return On(bits, Switch::HeroClass);
            case PlayerbotClassKind::ConquestOfAzeroth: return On(bits, Switch::CoAClasses);
            default: return true;
        }
    }

    // Neutral stands for no answer.
    PlayerbotFaction NeutralChoice(PlayerbotCreateSettings const& settings, uint32 horde, uint32 alliance)
    {
        return PickPlayerbotNeutralStartFaction(settings, horde, alliance).value_or(PlayerbotFaction::Neutral);
    }

    uint32 HordeAfter(uint32 bots, int32 percent, uint32 horde = 0, uint32 alliance = 0)
    {
        for (uint32 i = 0; i < bots; ++i)
        {
            if (PickPlayerbotFaction(true, true, percent, horde, alliance) == PlayerbotFaction::Horde)
                ++horde;
            else
                ++alliance;
        }
        return horde;
    }
}

TEST_CASE("The switch keys and defaults match the conf", "[playerbots][create]")
{
    REQUIRE(std::string(PlayerbotCreateSwitchKey(Switch::Horde)) == "Playerbots.Horde");
    REQUIRE(std::string(PlayerbotCreateSwitchKey(Switch::CoAClasses)) == "Playerbots.CoAClasses");
    REQUIRE(std::string(PlayerbotCreateSwitchKey(Switch::Pandaren)) == "Playerbots.Pandaren");
    REQUIRE(std::string(PlayerbotCreateSwitchKey(Switch::Worgen)) == "Playerbots.Worgen");
    PlayerbotCreateSettings const defaults;
    REQUIRE(defaults.IsOn(Switch::Horde));
    for (std::size_t i = 1; i < PLAYERBOT_CREATE_SWITCH_COUNT; ++i)
        REQUIRE_FALSE(defaults.Switches[i]);
    REQUIRE(defaults.HordePercent == 50);
}

TEST_CASE("Default keys make only Horde races with ordinary classes, as before", "[playerbots][create]")
{
    PlayerbotCreateSettings const defaults;
    PlayerbotCreateRules const rules;
    for (PlayerbotRaceFacts const& race : RACES)
        for (PlayerbotClassKind kind : CLASSES)
        {
            bool const coreHorde = race.Faction == PlayerbotFaction::Horde && !race.AlliedRace && !race.Dracthyr && !race.NpcOnly
                && !race.NeutralStartRace;
            REQUIRE(JudgePlayerbotRaceClass(race, kind, defaults, rules).Allowed() == (coreHorde && kind == PlayerbotClassKind::Ordinary));
        }
}

TEST_CASE("Every switch combination allows exactly the combinations its switches name", "[playerbots][create]")
{
    PlayerbotCreateRules const rules;
    for (uint32 bits = 0; bits < (1u << PLAYERBOT_CREATE_SWITCH_COUNT); ++bits)
    {
        PlayerbotCreateSettings const settings = AllSupported(bits);
        for (PlayerbotRaceFacts const& race : RACES)
            for (PlayerbotClassKind kind : CLASSES)
            {
                INFO("switches " << bits << " race " << uint32(race.Id) << " class kind " << uint32(kind));
                REQUIRE(JudgePlayerbotRaceClass(race, kind, settings, rules).Allowed() == Expected(race, kind, bits));
            }
    }
}

TEST_CASE("Monster races and unplayable classes are never made", "[playerbots][create]")
{
    PlayerbotCreateSettings const everything = AllSupported((1u << PLAYERBOT_CREATE_SWITCH_COUNT) - 1);
    PlayerbotCreateRules const rules;
    REQUIRE(JudgePlayerbotRace(Race(99, PlayerbotFaction::Horde, false, false, true), everything, rules).Refusal
        == PlayerbotCreateRefusal::NotPlayableRace);
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::NotPlayable, everything, rules).Refusal == PlayerbotCreateRefusal::NotPlayableClass);
}

TEST_CASE("A Pandaren bot is never made as a Horde or Alliance Pandaren", "[playerbots][create][pandaren]")
{
    for (uint32 pandaren : { 0u, 1u })
    {
        PlayerbotCreateSettings const everything = AllSupported(((1u << PLAYERBOT_CREATE_SWITCH_COUNT) - 1)
            & ~(pandaren ? 0u : 1u << uint32(Switch::Pandaren)));
        PlayerbotCreateRules const rules;
        REQUIRE(JudgePlayerbotRace(HORDE_PANDAREN, everything, rules).Refusal == PlayerbotCreateRefusal::StartsNeutral);
        REQUIRE(JudgePlayerbotRace(ALLIANCE_PANDAREN, everything, rules).Refusal == PlayerbotCreateRefusal::StartsNeutral);
    }
}

TEST_CASE("Playerbots.Pandaren is off by default and names itself when it refuses", "[playerbots][create][pandaren]")
{
    PlayerbotCreateRules const rules;
    PlayerbotCreateSettings settings;
    REQUIRE_FALSE(settings.IsOn(Switch::Pandaren));
    REQUIRE(settings.IsSupported(Switch::Pandaren));
    PlayerbotCreateVerdict const verdict = JudgePlayerbotRace(NEUTRAL_PANDAREN, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Pandaren);

    settings.Switches[std::size_t(Switch::Pandaren)] = true;
    REQUIRE(JudgePlayerbotRace(NEUTRAL_PANDAREN, settings, rules).Allowed());
}

TEST_CASE("A neutral Pandaren needs one faction open to choose", "[playerbots][create][pandaren]")
{
    PlayerbotCreateRules const rules;
    for (uint32 bits = 0; bits < 4; ++bits)
    {
        PlayerbotCreateSettings const settings = AllSupported(bits | (1u << uint32(Switch::Pandaren)));
        INFO("horde " << On(bits, Switch::Horde) << " alliance " << On(bits, Switch::Alliance));
        REQUIRE(JudgePlayerbotRace(NEUTRAL_PANDAREN, settings, rules).Allowed() == (bits != 0));
    }

    // Alliance on but not yet through its playtest does not count as open.
    PlayerbotCreateSettings settings;
    settings.Supported[std::size_t(Switch::Alliance)] = false;
    settings.Switches[std::size_t(Switch::Pandaren)] = true;
    settings.Switches[std::size_t(Switch::Horde)] = false;
    settings.Switches[std::size_t(Switch::Alliance)] = true;
    PlayerbotCreateVerdict const verdict = JudgePlayerbotRace(NEUTRAL_PANDAREN, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Horde);
}

TEST_CASE("At the end of her start she chooses by the roster split and the open factions", "[playerbots][create][pandaren]")
{
    PlayerbotCreateSettings settings;
    // Default keys: only the Horde is open.
    REQUIRE(NeutralChoice(settings, 100, 0) == PlayerbotFaction::Horde);

    // Alliance switched on but not through its playtest: still the Horde.
    settings.Supported[std::size_t(Switch::Alliance)] = false;
    settings.Switches[std::size_t(Switch::Alliance)] = true;
    REQUIRE(NeutralChoice(settings, 100, 0) == PlayerbotFaction::Horde);

    // Both open: the short side, by HordePercent.
    settings.Supported[std::size_t(Switch::Alliance)] = true;
    REQUIRE(NeutralChoice(settings, 10, 0) == PlayerbotFaction::Alliance);
    REQUIRE(NeutralChoice(settings, 0, 10) == PlayerbotFaction::Horde);
    settings.HordePercent = 100;
    REQUIRE(NeutralChoice(settings, 10, 0) == PlayerbotFaction::Horde);

    // Neither open: no answer.
    settings.Switches[std::size_t(Switch::Horde)] = false;
    settings.Switches[std::size_t(Switch::Alliance)] = false;
    REQUIRE(NeutralChoice(settings, 0, 0) == PlayerbotFaction::Neutral);
}

TEST_CASE("The faction choice byte is the one the client's window sends", "[playerbots][create][pandaren]")
{
    // The server's handler turns 0 into a Horde Pandaren and 1 into an Alliance Pandaren.
    STATIC_REQUIRE(PlayerbotNeutralFactionChoiceByte(PlayerbotFaction::Horde) == 0);
    STATIC_REQUIRE(PlayerbotNeutralFactionChoiceByte(PlayerbotFaction::Alliance) == 1);
}

TEST_CASE("A switch that is on but has not passed its playtest makes no bots", "[playerbots][create]")
{
    PlayerbotCreateSettings settings;
    settings.Switches[std::size_t(Switch::HeroClass)] = true;
    settings.Switches[std::size_t(Switch::CoAClasses)] = true;
    PlayerbotCreateRules const rules;

    PlayerbotCreateVerdict const hero = JudgePlayerbotClass(PlayerbotClassKind::Hero, settings, rules);
    REQUIRE(hero.Refusal == PlayerbotCreateRefusal::NotYetSupported);
    REQUIRE(hero.Switch == Switch::HeroClass);

    PlayerbotCreateVerdict const coa = JudgePlayerbotClass(PlayerbotClassKind::ConquestOfAzeroth, settings, rules);
    REQUIRE(coa.Refusal == PlayerbotCreateRefusal::NotYetSupported);
    REQUIRE(coa.Switch == Switch::CoAClasses);

    // Only the Horde has passed so far; the owner may turn every other switch on to playtest it, except the custom classes.
    for (std::size_t i = 0; i < PLAYERBOT_CREATE_SWITCH_COUNT; ++i)
        REQUIRE(PLAYERBOT_CREATE_SWITCH_SUPPORTED[i] == (Switch(i) != Switch::HeroClass && Switch(i) != Switch::CoAClasses));
}

TEST_CASE("Death Knight, Demon Hunter, Evoker and Dracthyr each need their own switch", "[playerbots][create][heroclass]")
{
    PlayerbotCreateRules const rules;
    PlayerbotCreateSettings settings;
    PlayerbotRaceFacts const orc = Race(2, PlayerbotFaction::Horde);
    PlayerbotRaceFacts const dracthyr = Race(70, PlayerbotFaction::Horde, false, true);

    // Default keys: each is off and names itself, so a fresh install makes the same bots as before.
    for (PlayerbotClassKind kind : { PlayerbotClassKind::DeathKnight, PlayerbotClassKind::DemonHunter, PlayerbotClassKind::Evoker })
    {
        PlayerbotCreateVerdict const verdict = JudgePlayerbotRaceClass(orc, kind, settings, rules);
        REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    }
    PlayerbotCreateVerdict verdict = JudgePlayerbotRace(dracthyr, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Dracthyr);

    settings.Switches[std::size_t(Switch::DeathKnight)] = true;
    REQUIRE(JudgePlayerbotRaceClass(orc, PlayerbotClassKind::DeathKnight, settings, rules).Allowed());
    REQUIRE(JudgePlayerbotRaceClass(orc, PlayerbotClassKind::DemonHunter, settings, rules).Switch == Switch::DemonHunter);

    settings.Switches[std::size_t(Switch::DemonHunter)] = true;
    REQUIRE(JudgePlayerbotRaceClass(orc, PlayerbotClassKind::DemonHunter, settings, rules).Allowed());

    // A Dracthyr Evoker needs both; a Dracthyr of an ordinary class needs only Dracthyr.
    settings.Switches[std::size_t(Switch::Dracthyr)] = true;
    REQUIRE(JudgePlayerbotRaceClass(dracthyr, PlayerbotClassKind::Ordinary, settings, rules).Allowed());
    verdict = JudgePlayerbotRaceClass(dracthyr, PlayerbotClassKind::Evoker, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Evoker);
    settings.Switches[std::size_t(Switch::Evoker)] = true;
    REQUIRE(JudgePlayerbotRaceClass(dracthyr, PlayerbotClassKind::Evoker, settings, rules).Allowed());

    // Dracthyr still needs her faction open.
    settings.Switches[std::size_t(Switch::Horde)] = false;
    verdict = JudgePlayerbotRace(dracthyr, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Horde);
}

TEST_CASE("Playerbots.Alliance makes the six core Alliance races, Worgen only with Playerbots.Worgen", "[playerbots][create][alliance]")
{
    PlayerbotCreateRules const rules;
    PlayerbotCreateSettings settings;
    PlayerbotRaceFacts const human = Race(1, PlayerbotFaction::Alliance);

    // Default keys: Alliance is off and says so.
    PlayerbotCreateVerdict verdict = JudgePlayerbotRace(human, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Alliance);

    settings.Switches[std::size_t(Switch::Alliance)] = true;
    REQUIRE(JudgePlayerbotRaceClass(human, PlayerbotClassKind::Ordinary, settings, rules).Allowed());
    // Allied Alliance races still need their own switch.
    REQUIRE(JudgePlayerbotRace(Race(29, PlayerbotFaction::Alliance, true), settings, rules).Switch == Switch::AlliedRaces);

    verdict = JudgePlayerbotRace(WORGEN, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Worgen);

    settings.Switches[std::size_t(Switch::Worgen)] = true;
    REQUIRE(JudgePlayerbotRace(WORGEN, settings, rules).Allowed());

    // Worgen without Alliance names Alliance first.
    settings.Switches[std::size_t(Switch::Alliance)] = false;
    verdict = JudgePlayerbotRace(WORGEN, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Alliance);
}

TEST_CASE("With Alliance open, a neutral Pandaren may choose the Alliance by default settings", "[playerbots][create][alliance][pandaren]")
{
    PlayerbotCreateSettings settings;
    settings.Switches[std::size_t(Switch::Alliance)] = true;
    REQUIRE(NeutralChoice(settings, 10, 0) == PlayerbotFaction::Alliance);
    REQUIRE(NeutralChoice(settings, 0, 10) == PlayerbotFaction::Horde);
}

TEST_CASE("An off switch is named before a switch that is not yet supported", "[playerbots][create]")
{
    PlayerbotCreateSettings settings; // Horde on, Dracthyr off
    PlayerbotCreateRules const rules;
    PlayerbotCreateVerdict verdict = JudgePlayerbotRace(Race(70, PlayerbotFaction::Horde, false, true), settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Dracthyr);

    settings.Switches[std::size_t(Switch::Horde)] = false;
    settings.Switches[std::size_t(Switch::Dracthyr)] = true;
    verdict = JudgePlayerbotRace(Race(70, PlayerbotFaction::Horde, false, true), settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Horde);
}

TEST_CASE("Both factions off makes no bot of any race", "[playerbots][create]")
{
    PlayerbotCreateSettings settings = AllSupported((1u << PLAYERBOT_CREATE_SWITCH_COUNT) - 1);
    settings.Switches[std::size_t(Switch::Horde)] = false;
    settings.Switches[std::size_t(Switch::Alliance)] = false;
    PlayerbotCreateRules const rules;
    for (PlayerbotRaceFacts const& race : RACES)
        REQUIRE_FALSE(JudgePlayerbotRace(race, settings, rules).Allowed());
    REQUIRE_FALSE(PickPlayerbotFaction(false, false, 50, 3, 4).has_value());
}

TEST_CASE("Playerbots.AlliedRaces makes allied races of each open faction, with the server's create rules", "[playerbots][create][allied]")
{
    PlayerbotCreateRules const rules; // the server's defaults: StartPlayerLevel 1 does not refuse a level-10 start
    PlayerbotCreateSettings settings;
    PlayerbotRaceFacts const highmountain = Race(28, PlayerbotFaction::Horde, true);
    PlayerbotRaceFacts const voidElf = Race(29, PlayerbotFaction::Alliance, true);
    // Earthen and Haranir carry the allied flag, so the same switch makes them. The Alliance Haranir row says it starts
    // at level 1 and the Horde one at 10; the server starts both at the allied level, and the filter treats them alike.
    PlayerbotRaceFacts const haranirAlliance = Race(86, PlayerbotFaction::Alliance, true);
    PlayerbotRaceFacts const haranirHorde = Race(91, PlayerbotFaction::Horde, true);

    // Default keys: off, and it names itself.
    REQUIRE_FALSE(settings.IsOn(Switch::AlliedRaces));
    REQUIRE(settings.IsSupported(Switch::AlliedRaces));
    PlayerbotCreateVerdict verdict = JudgePlayerbotRace(highmountain, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::AlliedRaces);

    settings.Switches[std::size_t(Switch::AlliedRaces)] = true;
    REQUIRE(JudgePlayerbotRaceClass(highmountain, PlayerbotClassKind::Ordinary, settings, rules).Allowed());
    REQUIRE(JudgePlayerbotRace(haranirHorde, settings, rules).Allowed());

    // The Alliance ones still need the Alliance switch.
    verdict = JudgePlayerbotRace(voidElf, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Alliance);
    REQUIRE(JudgePlayerbotRace(haranirAlliance, settings, rules).Switch == Switch::Alliance);
    settings.Switches[std::size_t(Switch::Alliance)] = true;
    REQUIRE(JudgePlayerbotRace(voidElf, settings, rules).Allowed());
    REQUIRE(JudgePlayerbotRace(haranirAlliance, settings, rules).Allowed());

    // Dracthyr is not part of it, even with the allied flag.
    verdict = JudgePlayerbotRace(Race(52, PlayerbotFaction::Alliance, true, true), settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::Dracthyr);

    // An allied Death Knight still needs the Death Knight switch.
    verdict = JudgePlayerbotRaceClass(highmountain, PlayerbotClassKind::DeathKnight, settings, rules);
    REQUIRE(verdict.Refusal == PlayerbotCreateRefusal::SwitchOff);
    REQUIRE(verdict.Switch == Switch::DeathKnight);
}

TEST_CASE("The server's Demon Hunter and Evoker create rules refuse a bot as they would a new player", "[playerbots][create]")
{
    PlayerbotCreateSettings const settings = AllSupported((1u << PLAYERBOT_CREATE_SWITCH_COUNT) - 1);
    PlayerbotCreateRules rules;
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::DemonHunter, settings, rules).Allowed());
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::Evoker, settings, rules).Allowed());

    rules.MinLevelForDemonHunter = 10;
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::DemonHunter, settings, rules).Refusal == PlayerbotCreateRefusal::HeroClassLevelRule);
    // The player's handler checks both levels for either class.
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::Evoker, settings, rules).Refusal == PlayerbotCreateRefusal::HeroClassLevelRule);
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::DeathKnight, settings, rules).Allowed());

    rules = {};
    rules.MinLevelForEvoker = 58;
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::Evoker, settings, rules).Refusal == PlayerbotCreateRefusal::HeroClassLevelRule);

    rules = {};
    rules.EvokersPerRealm = 0; // no limit
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::Evoker, settings, rules).Allowed());
    rules.EvokersPerRealm = -1;
    REQUIRE(JudgePlayerbotClass(PlayerbotClassKind::Evoker, settings, rules).Refusal == PlayerbotCreateRefusal::EvokerLimit);
}

TEST_CASE("A refusal names the switch that caused it", "[playerbots][create]")
{
    std::string const off = DescribePlayerbotCreateRefusal({ PlayerbotCreateRefusal::SwitchOff, Switch::AlliedRaces });
    REQUIRE(off.find("Playerbots.AlliedRaces") != std::string::npos);
    std::string const unsupported = DescribePlayerbotCreateRefusal({ PlayerbotCreateRefusal::NotYetSupported, Switch::DeathKnight });
    REQUIRE(unsupported.find("Playerbots.DeathKnight") != std::string::npos);
}

TEST_CASE("From no bots, the faction split lands within one bot of HordePercent", "[playerbots][create]")
{
    for (int32 percent : { 0, 25, 50, 75, 100 })
        for (uint32 bots : { 1u, 2u, 3u, 7u, 10u, 40u, 101u })
        {
            INFO("percent " << percent << " bots " << bots);
            int64 const horde = HordeAfter(bots, percent);
            int64 const wanted100 = int64(bots) * percent; // hundredths of a bot
            REQUIRE(std::abs(horde * 100 - wanted100) <= 100);
        }
}

TEST_CASE("A lopsided roster gets new bots on the short side until it reaches its share", "[playerbots][create]")
{
    // Twenty Horde and no Alliance at 50: the next twenty are all Alliance, then they alternate.
    uint32 horde = 20;
    uint32 alliance = 0;
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE((PickPlayerbotFaction(true, true, 50, horde, alliance) == PlayerbotFaction::Alliance));
        ++alliance;
    }
    REQUIRE((PickPlayerbotFaction(true, true, 50, horde, alliance) == PlayerbotFaction::Horde));
}

TEST_CASE("HordePercent is clamped and ignored with one faction", "[playerbots][create]")
{
    REQUIRE(ClampPlayerbotHordePercent(-5) == 0);
    REQUIRE(ClampPlayerbotHordePercent(150) == 100);
    REQUIRE(ClampPlayerbotHordePercent(30) == 30);
    REQUIRE(HordeAfter(10, 150) == 10);
    REQUIRE(HordeAfter(10, -20) == 0);

    REQUIRE((PickPlayerbotFaction(true, false, 0, 0, 0) == PlayerbotFaction::Horde));
    REQUIRE((PickPlayerbotFaction(false, true, 100, 0, 0) == PlayerbotFaction::Alliance));
}

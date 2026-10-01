/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_CREATE_FILTER_H
#define EVRY_PLAYERBOT_CREATE_FILTER_H

#include "Define.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>

// Which race and class a new bot character may have. The factory turns each DB2 race and class row into the facts
// below and asks these functions; they know nothing about the server, so the tests feed them made-up rows. A race and
// class that pass here still have to pass the same create checks a player gets (the factory's PassesCreateChecks and
// the appearance check). None of this touches a bot that already exists.

// The on/off switches in modules/mod-playerbots.conf. A bot is made only when every switch she needs is on.
enum class PlayerbotCreateSwitch : uint8
{
    Horde,
    Alliance,
    AlliedRaces,
    Dracthyr,
    DeathKnight,
    DemonHunter,
    Evoker,
    HeroClass,
    CoAClasses,
    Pandaren,
    Worgen
};

inline constexpr std::size_t PLAYERBOT_CREATE_SWITCH_COUNT = 11;

inline constexpr std::array<char const*, PLAYERBOT_CREATE_SWITCH_COUNT> PLAYERBOT_CREATE_SWITCH_KEYS =
{
    "Playerbots.Horde",
    "Playerbots.Alliance",
    "Playerbots.AlliedRaces",
    "Playerbots.Dracthyr",
    "Playerbots.DeathKnight",
    "Playerbots.DemonHunter",
    "Playerbots.Evoker",
    "Playerbots.HeroClass",
    "Playerbots.CoAClasses",
    "Playerbots.Pandaren",
    "Playerbots.Worgen"
};

// Every new switch is off unless the owner turns it on. Horde is on so a fresh install makes the same bots as before.
inline constexpr std::array<bool, PLAYERBOT_CREATE_SWITCH_COUNT> PLAYERBOT_CREATE_SWITCH_DEFAULTS =
{
    true, false, false, false, false, false, false, false, false, false, false
};

// Which switches have passed their playtest. A switch that is on but not here yet makes no bots, and the factory says
// so once. Raise one only when bots of that kind have been played through their starting zone. Pandaren is the owner's
// to turn on (30 September 2026) even though the bot brain cannot yet do most of the Wandering Isle's quests (balance
// poles, clicking a spirit or a cart, the balloon, escorts); the factory says so once when it is on. Alliance and Worgen
// are the owner's to turn on too (30 September 2026) so their starts can be playtested; the bot brain cannot yet do the
// Dwarf gyrocopter ride, the Gnome start's spellclick, gossip and teleport, or Gilneas's vehicle and pet-bar steps, and
// the factory says so once when each is on. Allied races are open for a playtest the same way (1 October 2026); the bot
// brain cannot yet do the Earthen start's extra action button and gossip, or the Haranir start's gossip and spellclick.
inline constexpr std::array<bool, PLAYERBOT_CREATE_SWITCH_COUNT> PLAYERBOT_CREATE_SWITCH_SUPPORTED =
{
    true, true, true, false, false, false, false, false, false, true, true
};

inline constexpr char const* PLAYERBOTS_HORDE_PERCENT = "Playerbots.HordePercent";
inline constexpr int32 PLAYERBOT_HORDE_PERCENT_DEFAULT = 50;

inline constexpr char const* PlayerbotCreateSwitchKey(PlayerbotCreateSwitch which)
{
    return PLAYERBOT_CREATE_SWITCH_KEYS[std::size_t(which)];
}

enum class PlayerbotFaction : uint8
{
    Horde,
    Alliance,
    Neutral
};

struct PlayerbotRaceFacts
{
    uint8 Id = 0;
    PlayerbotFaction Faction = PlayerbotFaction::Neutral;
    bool NpcOnly = false;    // ChrRaces NPCOnly: a monster or companion race, never a character
    bool AlliedRace = false; // ChrRaces IsAlliedRace
    bool Dracthyr = false;   // Dracthyr have their own switch, whatever the allied flag says
    bool Worgen = false;     // Worgen have their own switch as well as the Alliance one: the Gilneas start needs verbs bots lack
    // ChrRaces NeutralRaceID when that race is neutral: a player of this race starts as that race and chooses her faction
    // in its starting zone (the Horde and Alliance Pandaren). 0 for every other race.
    uint8 NeutralStartRace = 0;
};

enum class PlayerbotClassKind : uint8
{
    Ordinary,          // Warrior, Paladin, Hunter, Rogue, Priest, Shaman, Mage, Warlock, Monk, Druid
    DeathKnight,
    DemonHunter,
    Evoker,
    Hero,              // evryCore's custom Hero class
    ConquestOfAzeroth, // Reaper and any later class from mod-coa
    NotPlayable        // Adventurer, Traveler, no class
};

// The server's own create rules that decide something for a bot. Each bot account holds only her, so a rule that
// needs another character on the account (a level for Demon Hunter or Evoker) refuses her as it would a new player.
// Her start level is not one of them: the server gives her the level a player of her race and class gets
// (Player::GetStartLevel, from StartPlayerLevel, StartAlliedRacePlayerLevel and the class start levels).
struct PlayerbotCreateRules
{
    uint32 MinLevelForDemonHunter = 0;  // CharacterCreating.MinLevelForDemonHunter
    uint32 MinLevelForEvoker = 0;       // CharacterCreating.MinLevelForEvoker
    int32 EvokersPerRealm = 1;          // CharacterCreating.EvokersPerRealm (0 means no limit)
};

struct PlayerbotCreateSettings
{
    std::array<bool, PLAYERBOT_CREATE_SWITCH_COUNT> Switches = PLAYERBOT_CREATE_SWITCH_DEFAULTS;
    std::array<bool, PLAYERBOT_CREATE_SWITCH_COUNT> Supported = PLAYERBOT_CREATE_SWITCH_SUPPORTED;
    int32 HordePercent = PLAYERBOT_HORDE_PERCENT_DEFAULT;

    bool IsOn(PlayerbotCreateSwitch which) const { return Switches[std::size_t(which)]; }
    bool IsSupported(PlayerbotCreateSwitch which) const { return Supported[std::size_t(which)]; }
    bool Opens(PlayerbotCreateSwitch which) const { return IsOn(which) && IsSupported(which); }
};

enum class PlayerbotCreateRefusal : uint8
{
    None,
    NotPlayableRace,
    NotPlayableClass,
    StartsNeutral,        // a player of this race starts as its neutral race, so a bot is made as that race
    SwitchOff,
    NotYetSupported,
    HeroClassLevelRule,   // MinLevelForDemonHunter or MinLevelForEvoker needs another character on the account
    EvokerLimit
};

struct PlayerbotCreateVerdict
{
    PlayerbotCreateRefusal Refusal = PlayerbotCreateRefusal::None;
    PlayerbotCreateSwitch Switch = PlayerbotCreateSwitch::Horde; // which switch, for SwitchOff and NotYetSupported

    bool Allowed() const { return Refusal == PlayerbotCreateRefusal::None; }
};

inline PlayerbotCreateSwitch PlayerbotFactionSwitch(PlayerbotFaction faction)
{
    return faction == PlayerbotFaction::Alliance ? PlayerbotCreateSwitch::Alliance : PlayerbotCreateSwitch::Horde;
}

// Off comes before not yet supported, so the owner hears about the switch they have not turned on first.
inline std::optional<PlayerbotCreateVerdict> JudgePlayerbotSwitches(PlayerbotCreateSettings const& settings,
    std::initializer_list<PlayerbotCreateSwitch> needed)
{
    for (PlayerbotCreateSwitch which : needed)
        if (!settings.IsOn(which))
            return PlayerbotCreateVerdict{ PlayerbotCreateRefusal::SwitchOff, which };
    for (PlayerbotCreateSwitch which : needed)
        if (!settings.IsSupported(which))
            return PlayerbotCreateVerdict{ PlayerbotCreateRefusal::NotYetSupported, which };
    return std::nullopt;
}

inline PlayerbotCreateVerdict JudgePlayerbotRace(PlayerbotRaceFacts const& race, PlayerbotCreateSettings const& settings,
    PlayerbotCreateRules const& /*rules*/)
{
    if (race.NpcOnly)
        return { PlayerbotCreateRefusal::NotPlayableRace };
    if (race.NeutralStartRace != 0)
        return { PlayerbotCreateRefusal::StartsNeutral };

    PlayerbotCreateSwitch const faction = PlayerbotFactionSwitch(race.Faction);
    std::optional<PlayerbotCreateVerdict> refused;
    if (race.Faction == PlayerbotFaction::Neutral)
    {
        refused = JudgePlayerbotSwitches(settings, { PlayerbotCreateSwitch::Pandaren });
        // She chooses the Horde or the Alliance at the end of her start, so one of them must be open to her.
        if (!refused && !settings.Opens(PlayerbotCreateSwitch::Horde) && !settings.Opens(PlayerbotCreateSwitch::Alliance))
            refused = JudgePlayerbotSwitches(settings, { PlayerbotCreateSwitch::Horde });
    }
    else if (race.Dracthyr)
        refused = JudgePlayerbotSwitches(settings, { faction, PlayerbotCreateSwitch::Dracthyr });
    else if (race.AlliedRace)
        refused = JudgePlayerbotSwitches(settings, { faction, PlayerbotCreateSwitch::AlliedRaces });
    else if (race.Worgen)
        refused = JudgePlayerbotSwitches(settings, { faction, PlayerbotCreateSwitch::Worgen });
    else
        refused = JudgePlayerbotSwitches(settings, { faction });
    if (refused)
        return *refused;
    return {};
}

inline PlayerbotCreateVerdict JudgePlayerbotClass(PlayerbotClassKind kind, PlayerbotCreateSettings const& settings,
    PlayerbotCreateRules const& rules)
{
    std::optional<PlayerbotCreateVerdict> refused;
    switch (kind)
    {
        case PlayerbotClassKind::Ordinary:
            return {};
        case PlayerbotClassKind::NotPlayable:
            return { PlayerbotCreateRefusal::NotPlayableClass };
        case PlayerbotClassKind::DeathKnight:
            refused = JudgePlayerbotSwitches(settings, { PlayerbotCreateSwitch::DeathKnight });
            break;
        case PlayerbotClassKind::DemonHunter:
            refused = JudgePlayerbotSwitches(settings, { PlayerbotCreateSwitch::DemonHunter });
            break;
        case PlayerbotClassKind::Evoker:
            refused = JudgePlayerbotSwitches(settings, { PlayerbotCreateSwitch::Evoker });
            break;
        case PlayerbotClassKind::Hero:
            refused = JudgePlayerbotSwitches(settings, { PlayerbotCreateSwitch::HeroClass });
            break;
        case PlayerbotClassKind::ConquestOfAzeroth:
            refused = JudgePlayerbotSwitches(settings, { PlayerbotCreateSwitch::CoAClasses });
            break;
    }
    if (refused)
        return *refused;

    // The player's create handler checks both levels for either class, so a bot does too.
    if (kind == PlayerbotClassKind::DemonHunter || kind == PlayerbotClassKind::Evoker)
        if (rules.MinLevelForDemonHunter != 0 || rules.MinLevelForEvoker != 0)
            return { PlayerbotCreateRefusal::HeroClassLevelRule };

    // A new bot account has no Evoker yet, so only a limit below one refuses her.
    if (kind == PlayerbotClassKind::Evoker && rules.EvokersPerRealm != 0 && rules.EvokersPerRealm < 1)
        return { PlayerbotCreateRefusal::EvokerLimit };

    return {};
}

inline PlayerbotCreateVerdict JudgePlayerbotRaceClass(PlayerbotRaceFacts const& race, PlayerbotClassKind kind,
    PlayerbotCreateSettings const& settings, PlayerbotCreateRules const& rules)
{
    PlayerbotCreateVerdict const raceVerdict = JudgePlayerbotRace(race, settings, rules);
    if (!raceVerdict.Allowed())
        return raceVerdict;
    return JudgePlayerbotClass(kind, settings, rules);
}

inline std::string DescribePlayerbotCreateRefusal(PlayerbotCreateVerdict const& verdict)
{
    switch (verdict.Refusal)
    {
        case PlayerbotCreateRefusal::None:
            return "allowed";
        case PlayerbotCreateRefusal::NotPlayableRace:
            return "it is not a race a player can make";
        case PlayerbotCreateRefusal::NotPlayableClass:
            return "it is not a class a player can make";
        case PlayerbotCreateRefusal::StartsNeutral:
            return "a player of this race starts as its neutral race and chooses a faction at the end of that start, so a bot is made "
                "as the neutral race or not at all";
        case PlayerbotCreateRefusal::SwitchOff:
            return std::string(PlayerbotCreateSwitchKey(verdict.Switch)) + " is 0";
        case PlayerbotCreateRefusal::NotYetSupported:
            return std::string(PlayerbotCreateSwitchKey(verdict.Switch)) + " is on, but bots of that kind have not passed their playtest yet";
        case PlayerbotCreateRefusal::HeroClassLevelRule:
            return "CharacterCreating.MinLevelForDemonHunter or CharacterCreating.MinLevelForEvoker needs another character on the account, and a bot account has only her";
        case PlayerbotCreateRefusal::EvokerLimit:
            return "CharacterCreating.EvokersPerRealm allows no Evoker";
    }
    return "unknown";
}

inline int32 ClampPlayerbotHordePercent(int32 percent)
{
    return std::clamp(percent, 0, 100);
}

// With both factions open, the new bot goes to whichever faction is furthest below its share of the roster once she
// is counted, so the whole roster moves toward the split, not only the new bots. A tie goes to the Horde. With one
// faction open the split does not matter; with none, no bot is made.
inline std::optional<PlayerbotFaction> PickPlayerbotFaction(bool hordeOpen, bool allianceOpen, int32 hordePercent,
    uint32 hordeBots, uint32 allianceBots)
{
    if (!hordeOpen && !allianceOpen)
        return std::nullopt;
    if (!allianceOpen)
        return PlayerbotFaction::Horde;
    if (!hordeOpen)
        return PlayerbotFaction::Alliance;

    int64 const percent = ClampPlayerbotHordePercent(hordePercent);
    int64 const total = int64(hordeBots) + int64(allianceBots) + 1;
    // Each shortfall is in hundredths of a bot: the share after she is made, less the bots that faction has.
    int64 const hordeShort = percent * total - 100 * int64(hordeBots);
    int64 const allianceShort = (100 - percent) * total - 100 * int64(allianceBots);
    return hordeShort >= allianceShort ? PlayerbotFaction::Horde : PlayerbotFaction::Alliance;
}

// The faction a neutral bot chooses when her start offers the choice: the same split as a new bot, over the factions
// whose switch is on and has passed its playtest. None when neither is open; then she leaves the choice unanswered.
inline std::optional<PlayerbotFaction> PickPlayerbotNeutralStartFaction(PlayerbotCreateSettings const& settings, uint32 hordeBots,
    uint32 allianceBots)
{
    return PickPlayerbotFaction(settings.Opens(PlayerbotCreateSwitch::Horde), settings.Opens(PlayerbotCreateSwitch::Alliance),
        settings.HordePercent, hordeBots, allianceBots);
}

// What the client's faction choice window sends in CMSG_NEUTRAL_PLAYER_SELECT_FACTION: 0 for the Horde, 1 for the Alliance.
inline constexpr uint8 PlayerbotNeutralFactionChoiceByte(PlayerbotFaction faction)
{
    return faction == PlayerbotFaction::Alliance ? 1 : 0;
}

#endif

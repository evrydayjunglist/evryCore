/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_GEAR_SCORE_H
#define EVRY_PLAYERBOT_GEAR_SCORE_H

#include "Define.h"
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// How a bot tells which of two pieces of gear is better for her: each stat the item gives her is multiplied by what
// that stat is worth to her specialization, and the sums are compared. The module brings a starting table for every
// specialization; Playerbots.Gear.Weights.<specialization id> in the conf changes any number in it. Nothing here
// knows about the server, so the rules are tested with made-up items.

inline constexpr char const* PLAYERBOTS_GEAR_WEIGHTS_PREFIX = "Playerbots.Gear.Weights.";
inline constexpr char const* PLAYERBOTS_GEAR_UPGRADE_MARGIN_PCT = "Playerbots.Gear.UpgradeMarginPct";

// An item has to score at least this many percent more than what it would replace before she changes. About one
// item level at retail stat budgets.
inline constexpr float PLAYERBOT_GEAR_DEFAULT_UPGRADE_MARGIN_PCT = 1.0f;

enum class PlayerbotGearStat : uint8
{
    Strength,
    Agility,
    Intellect,
    Stamina,
    CriticalStrike,
    Haste,
    Mastery,
    Versatility,
    Leech,
    Avoidance,
    Speed,
    Armor,      // the armor value on the item, and bonus armor
    WeaponDps,  // the weapon's damage per second
    Count
};

inline constexpr std::size_t PLAYERBOT_GEAR_STAT_COUNT = std::size_t(PlayerbotGearStat::Count);

using PlayerbotGearStats = std::array<float, PLAYERBOT_GEAR_STAT_COUNT>;

inline float& GearStat(PlayerbotGearStats& stats, PlayerbotGearStat stat) { return stats[std::size_t(stat)]; }
inline float GearStat(PlayerbotGearStats const& stats, PlayerbotGearStat stat) { return stats[std::size_t(stat)]; }

inline char const* PlayerbotGearStatName(PlayerbotGearStat stat)
{
    switch (stat)
    {
        case PlayerbotGearStat::Strength: return "Strength";
        case PlayerbotGearStat::Agility: return "Agility";
        case PlayerbotGearStat::Intellect: return "Intellect";
        case PlayerbotGearStat::Stamina: return "Stamina";
        case PlayerbotGearStat::CriticalStrike: return "CriticalStrike";
        case PlayerbotGearStat::Haste: return "Haste";
        case PlayerbotGearStat::Mastery: return "Mastery";
        case PlayerbotGearStat::Versatility: return "Versatility";
        case PlayerbotGearStat::Leech: return "Leech";
        case PlayerbotGearStat::Avoidance: return "Avoidance";
        case PlayerbotGearStat::Speed: return "Speed";
        case PlayerbotGearStat::Armor: return "Armor";
        case PlayerbotGearStat::WeaponDps: return "WeaponDps";
        case PlayerbotGearStat::Count: break;
    }
    return "Unknown";
}

namespace PlayerbotGearDetail
{
    inline std::string Squeeze(std::string_view text)
    {
        std::string out;
        for (char c : text)
            if (!std::isspace(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    }
}

// A stat name as written in the conf. Spaces, capitals, '-' and '_' do not matter, and the usual short names work.
inline std::optional<PlayerbotGearStat> ParsePlayerbotGearStat(std::string_view text)
{
    std::string const name = PlayerbotGearDetail::Squeeze(text);
    for (std::size_t i = 0; i < PLAYERBOT_GEAR_STAT_COUNT; ++i)
        if (name == PlayerbotGearDetail::Squeeze(PlayerbotGearStatName(PlayerbotGearStat(i))))
            return PlayerbotGearStat(i);

    if (name == "str") return PlayerbotGearStat::Strength;
    if (name == "agi") return PlayerbotGearStat::Agility;
    if (name == "int") return PlayerbotGearStat::Intellect;
    if (name == "sta" || name == "stam") return PlayerbotGearStat::Stamina;
    if (name == "crit") return PlayerbotGearStat::CriticalStrike;
    if (name == "mast") return PlayerbotGearStat::Mastery;
    if (name == "vers") return PlayerbotGearStat::Versatility;
    if (name == "avoid") return PlayerbotGearStat::Avoidance;
    if (name == "dps") return PlayerbotGearStat::WeaponDps;
    return std::nullopt;
}

// Changes the weights named in text, such as "Strength=1, Haste=0.6, WeaponDps=2". Stats not named keep what they had.
// False, with the reason in error and weights untouched, when any part cannot be read.
inline bool ParsePlayerbotGearWeights(std::string_view text, PlayerbotGearStats& weights, std::string& error)
{
    PlayerbotGearStats changed = weights;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t end = text.find_first_of(",;", start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view const part = text.substr(start, end - start);
        start = end + 1;

        if (PlayerbotGearDetail::Squeeze(part).empty())
            continue;

        std::size_t const equals = part.find_first_of("=:");
        if (equals == std::string_view::npos)
        {
            error = "'" + std::string(part) + "' has no '='";
            return false;
        }

        std::optional<PlayerbotGearStat> const stat = ParsePlayerbotGearStat(part.substr(0, equals));
        if (!stat)
        {
            error = "'" + std::string(part.substr(0, equals)) + "' is not a stat name";
            return false;
        }

        std::string number;
        for (char c : part.substr(equals + 1))
            if (!std::isspace(static_cast<unsigned char>(c)))
                number.push_back(c);
        float value = 0.0f;
        auto const [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (number.empty() || ec != std::errc() || ptr != number.data() + number.size() || !std::isfinite(value) || value < 0.0f)
        {
            error = "'" + std::string(part.substr(equals + 1)) + "' is not a number of 0 or more";
            return false;
        }

        GearStat(changed, *stat) = value;
    }

    weights = changed;
    return true;
}

inline std::string DescribePlayerbotGearWeights(PlayerbotGearStats const& weights)
{
    std::string out;
    for (std::size_t i = 0; i < PLAYERBOT_GEAR_STAT_COUNT; ++i)
    {
        if (weights[i] == 0.0f)
            continue;
        if (!out.empty())
            out += ", ";
        char number[32];
        auto const [ptr, ec] = std::to_chars(number, number + sizeof(number), weights[i]);
        out += PlayerbotGearStatName(PlayerbotGearStat(i));
        out += '=';
        out.append(number, ec == std::errc() ? ptr : number);
    }
    return out;
}

inline float ScorePlayerbotGear(PlayerbotGearStats const& stats, PlayerbotGearStats const& weights)
{
    float score = 0.0f;
    for (std::size_t i = 0; i < PLAYERBOT_GEAR_STAT_COUNT; ++i)
        score += stats[i] * weights[i];
    return score;
}

enum class PlayerbotGearPrimary : uint8
{
    Strength,
    Agility,
    Intellect
};

enum class PlayerbotGearRole : uint8
{
    Damage,
    Tank,
    Healer
};

// The starting weights for a specialization the module has no row for (the one every class starts in before it picks,
// and any new one): her main stat, a little stamina, the four secondary stats alike, and weapon damage for fighters
// who hit with their weapon.
inline PlayerbotGearStats PlayerbotGearArchetypeWeights(PlayerbotGearPrimary primary, PlayerbotGearRole role)
{
    PlayerbotGearStats weights{};
    switch (primary)
    {
        case PlayerbotGearPrimary::Strength: GearStat(weights, PlayerbotGearStat::Strength) = 1.0f; break;
        case PlayerbotGearPrimary::Agility: GearStat(weights, PlayerbotGearStat::Agility) = 1.0f; break;
        case PlayerbotGearPrimary::Intellect: GearStat(weights, PlayerbotGearStat::Intellect) = 1.0f; break;
    }

    bool const tank = role == PlayerbotGearRole::Tank;
    GearStat(weights, PlayerbotGearStat::Stamina) = tank ? 0.15f : 0.05f;
    GearStat(weights, PlayerbotGearStat::CriticalStrike) = 0.4f;
    GearStat(weights, PlayerbotGearStat::Haste) = 0.4f;
    GearStat(weights, PlayerbotGearStat::Mastery) = 0.4f;
    GearStat(weights, PlayerbotGearStat::Versatility) = 0.4f;
    GearStat(weights, PlayerbotGearStat::Leech) = 0.1f;
    GearStat(weights, PlayerbotGearStat::Avoidance) = tank ? 0.1f : 0.05f;
    GearStat(weights, PlayerbotGearStat::Speed) = 0.05f;
    GearStat(weights, PlayerbotGearStat::Armor) = tank ? 0.15f : 0.02f;
    if (primary != PlayerbotGearPrimary::Intellect)
        GearStat(weights, PlayerbotGearStat::WeaponDps) = tank ? 1.0f : 1.5f;
    return weights;
}

// The module's starting weights for each specialization, by its id in ChrSpecialization. The main stat is worth 1.
// These are a sensible start for levelling, not a simulation result; the conf can change any of them.
struct PlayerbotGearSpecRow
{
    uint32 SpecId;
    char const* Name;
    PlayerbotGearPrimary Primary;
    PlayerbotGearRole Role;
    float Crit;
    float Haste;
    float Mastery;
    float Versatility;
};

inline constexpr std::array<PlayerbotGearSpecRow, 39> PLAYERBOT_GEAR_SPEC_ROWS =
{ {
    { 71, "Warrior Arms", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Damage, 0.45f, 0.35f, 0.40f, 0.35f },
    { 72, "Warrior Fury", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Damage, 0.40f, 0.45f, 0.40f, 0.35f },
    { 73, "Warrior Protection", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Tank, 0.35f, 0.45f, 0.30f, 0.40f },
    { 65, "Paladin Holy", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Healer, 0.45f, 0.40f, 0.35f, 0.35f },
    { 66, "Paladin Protection", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Tank, 0.30f, 0.45f, 0.35f, 0.40f },
    { 70, "Paladin Retribution", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Damage, 0.40f, 0.40f, 0.45f, 0.35f },
    { 253, "Hunter Beast Mastery", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.45f, 0.40f, 0.35f },
    { 254, "Hunter Marksmanship", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.45f, 0.35f, 0.40f, 0.35f },
    { 255, "Hunter Survival", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.45f, 0.35f, 0.35f },
    { 259, "Rogue Assassination", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.35f, 0.45f, 0.35f },
    { 260, "Rogue Outlaw", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.40f, 0.30f, 0.45f },
    { 261, "Rogue Subtlety", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.30f, 0.45f, 0.40f },
    { 256, "Priest Discipline", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Healer, 0.40f, 0.45f, 0.35f, 0.35f },
    { 257, "Priest Holy", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Healer, 0.40f, 0.35f, 0.40f, 0.35f },
    { 258, "Priest Shadow", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.35f, 0.45f, 0.40f, 0.35f },
    { 250, "Death Knight Blood", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Tank, 0.35f, 0.45f, 0.30f, 0.40f },
    { 251, "Death Knight Frost", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Damage, 0.40f, 0.35f, 0.45f, 0.35f },
    { 252, "Death Knight Unholy", PlayerbotGearPrimary::Strength, PlayerbotGearRole::Damage, 0.35f, 0.40f, 0.45f, 0.35f },
    { 262, "Shaman Elemental", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.40f, 0.45f, 0.40f, 0.35f },
    { 263, "Shaman Enhancement", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.35f, 0.45f, 0.40f, 0.35f },
    { 264, "Shaman Restoration", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Healer, 0.45f, 0.35f, 0.35f, 0.40f },
    { 62, "Mage Arcane", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.35f, 0.40f, 0.45f, 0.35f },
    { 63, "Mage Fire", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.45f, 0.45f, 0.30f, 0.35f },
    { 64, "Mage Frost", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.40f, 0.45f, 0.35f, 0.35f },
    { 265, "Warlock Affliction", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.35f, 0.40f, 0.45f, 0.35f },
    { 266, "Warlock Demonology", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.35f, 0.45f, 0.40f, 0.35f },
    { 267, "Warlock Destruction", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.40f, 0.45f, 0.40f, 0.35f },
    { 268, "Monk Brewmaster", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Tank, 0.40f, 0.35f, 0.35f, 0.45f },
    { 269, "Monk Windwalker", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.40f, 0.35f, 0.40f },
    { 270, "Monk Mistweaver", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Healer, 0.40f, 0.45f, 0.35f, 0.35f },
    { 102, "Druid Balance", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.35f, 0.40f, 0.45f, 0.35f },
    { 103, "Druid Feral", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.35f, 0.45f, 0.35f },
    { 104, "Druid Guardian", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Tank, 0.30f, 0.35f, 0.40f, 0.45f },
    { 105, "Druid Restoration", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Healer, 0.30f, 0.45f, 0.40f, 0.35f },
    { 577, "Demon Hunter Havoc", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Damage, 0.40f, 0.40f, 0.35f, 0.40f },
    { 581, "Demon Hunter Vengeance", PlayerbotGearPrimary::Agility, PlayerbotGearRole::Tank, 0.35f, 0.45f, 0.30f, 0.40f },
    { 1467, "Evoker Devastation", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.45f, 0.35f, 0.40f, 0.35f },
    { 1468, "Evoker Preservation", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Healer, 0.40f, 0.35f, 0.45f, 0.35f },
    { 1473, "Evoker Augmentation", PlayerbotGearPrimary::Intellect, PlayerbotGearRole::Damage, 0.35f, 0.40f, 0.45f, 0.35f },
} };

inline PlayerbotGearSpecRow const* FindPlayerbotGearSpecRow(uint32 specId)
{
    for (PlayerbotGearSpecRow const& row : PLAYERBOT_GEAR_SPEC_ROWS)
        if (row.SpecId == specId)
            return &row;
    return nullptr;
}

inline PlayerbotGearStats PlayerbotGearRowWeights(PlayerbotGearSpecRow const& row)
{
    PlayerbotGearStats weights = PlayerbotGearArchetypeWeights(row.Primary, row.Role);
    GearStat(weights, PlayerbotGearStat::CriticalStrike) = row.Crit;
    GearStat(weights, PlayerbotGearStat::Haste) = row.Haste;
    GearStat(weights, PlayerbotGearStat::Mastery) = row.Mastery;
    GearStat(weights, PlayerbotGearStat::Versatility) = row.Versatility;
    // A cat or a bear hits with the form's claws; the weapon's damage counts for less.
    if (row.SpecId == 103 || row.SpecId == 104)
        GearStat(weights, PlayerbotGearStat::WeaponDps) = 1.0f;
    if (row.SpecId == 104)
        GearStat(weights, PlayerbotGearStat::Armor) = 0.2f;
    return weights;
}

// Better by the margin: strictly more, and at least marginPct percent more than what it replaces.
inline bool PlayerbotGearIsUpgrade(float newScore, float oldScore, float marginPct)
{
    if (newScore <= oldScore)
        return false;
    return newScore >= oldScore * (1.0f + marginPct / 100.0f);
}

// How much better, for choosing between several upgrades: the share gained over what it replaces. Filling an empty
// place comes first.
inline float PlayerbotGearGain(float newScore, float oldScore, bool replacesNothing)
{
    if (replacesNothing)
        return 1000.0f + newScore;
    if (oldScore <= 0.0f)
        return 100.0f + newScore;
    return (newScore - oldScore) / oldScore;
}

// Armor kinds by item subclass: cloth 1, leather 2, mail 3, plate 4. Her class armor is the heaviest her class can
// wear. A lighter piece may fill an empty slot or replace another piece lighter than her class armor, but never a
// piece of her class armor.
inline constexpr uint8 PLAYERBOT_GEAR_ARMOR_CLOTH = 1;
inline constexpr uint8 PLAYERBOT_GEAR_ARMOR_PLATE = 4;

inline uint8 PlayerbotGearClassArmor(uint32 armorTypeMask)
{
    for (uint8 kind = PLAYERBOT_GEAR_ARMOR_PLATE; kind >= PLAYERBOT_GEAR_ARMOR_CLOTH; --kind)
        if (armorTypeMask & (1u << kind))
            return kind;
    return 0;
}

// wornArmor 0 means the slot is empty or holds no body armor.
inline bool PlayerbotGearArmorKindAllows(uint8 classArmor, uint8 itemArmor, uint8 wornArmor)
{
    if (!classArmor || !itemArmor || itemArmor >= classArmor)
        return true;
    return wornArmor < classArmor;
}

// What a weapon or off-hand item takes off her hands when it goes on.
enum class PlayerbotGearHand : uint8
{
    NotAHand,   // any other slot
    TwoHand,    // takes both hands: compared against what is in both
    MainHand,   // a one-hand weapon in the main hand: compared against the main hand
    OffHand     // compared against the off hand; cannot go on while a two-hand weapon fills both hands
};

struct PlayerbotGearWorn
{
    bool Has = false;
    float Score = 0.0f;
};

struct PlayerbotGearReplaced
{
    bool Allowed = true;        // false: a two-hand weapon fills both hands
    bool Nothing = true;        // nothing is worn where it goes
    float Score = 0.0f;         // what it would replace
};

inline PlayerbotGearReplaced PlayerbotGearWhatItReplaces(PlayerbotGearHand hand, PlayerbotGearWorn slot,
    PlayerbotGearWorn mainHand, bool mainIsTwoHand, PlayerbotGearWorn offHand)
{
    PlayerbotGearReplaced out;
    switch (hand)
    {
        case PlayerbotGearHand::TwoHand:
            out.Nothing = !mainHand.Has && !offHand.Has;
            out.Score = (mainHand.Has ? mainHand.Score : 0.0f) + (offHand.Has ? offHand.Score : 0.0f);
            break;
        case PlayerbotGearHand::MainHand:
            out.Nothing = !mainHand.Has;
            out.Score = mainHand.Has ? mainHand.Score : 0.0f;
            break;
        case PlayerbotGearHand::OffHand:
            if (mainHand.Has && mainIsTwoHand)
            {
                out.Allowed = false;
                break;
            }
            out.Nothing = !offHand.Has;
            out.Score = offHand.Has ? offHand.Score : 0.0f;
            break;
        case PlayerbotGearHand::NotAHand:
            out.Nothing = !slot.Has;
            out.Score = slot.Has ? slot.Score : 0.0f;
            break;
    }
    return out;
}

// A hunter's main hand holds either a bow, gun, or crossbow or a melee weapon, and her specialization says which she
// fights with. The other kind never replaces the kind she wants; the kind she wants replaces the other whatever it scores,
// the way a player swaps a stand-in out as soon as she has the real thing.
enum class PlayerbotGearReach : uint8
{
    Any,     // no preference, or not a weapon
    Melee,
    Ranged
};

enum class PlayerbotGearReachFit : uint8
{
    Compare,   // judge it by score as usual
    Refused,   // the wrong kind against the kind she wants
    Replaces   // the kind she wants against the wrong kind: as good as an empty hand
};

inline PlayerbotGearReachFit PlayerbotGearReachFits(PlayerbotGearReach wanted, PlayerbotGearReach item, PlayerbotGearReach worn)
{
    if (wanted == PlayerbotGearReach::Any || item == PlayerbotGearReach::Any || worn == PlayerbotGearReach::Any || item == worn)
        return PlayerbotGearReachFit::Compare;
    return item == wanted ? PlayerbotGearReachFit::Replaces : PlayerbotGearReachFit::Refused;
}

#endif

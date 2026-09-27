/* This file is part of the TrinityCore Project. See AUTHORS file for Copyright information.
 * Licensed under the GNU General Public License, version 2 or later. */

#ifndef COA_REAPER_SPELL_MECHANICS_H
#define COA_REAPER_SPELL_MECHANICS_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Coa::Reaper
{
enum Spells : std::uint32_t
{
    Reap = 600000,
    Murder = 600001,
    SoulStrike = 600002,
    Soulrend = 600003,
    SoulCollector = 600004,
    SoulFragment = 600005,
    ReapedSoul = 600006,
    SoulInfusion = 600007,
    ReapPower = 600008,
    SoulrendPower = 600009,
    SoulStrikeHeal = 600010,
    MurderDebuff = 600011,
    EquipmentReceipt = 600012
};

constexpr std::uint32_t ClassSkill = 1311;
constexpr std::uint32_t StarterWeapon = 2092;
constexpr std::uint8_t SoulCap = 3;
constexpr std::uint8_t FragmentsPerSoul = 3;

// The auras on the caster are the state. This value is only a snapshot used
// to calculate one synchronous transition on the caster's normal update thread.
struct Souls
{
    std::uint8_t Fragments = 0;
    std::uint8_t Reaped = 0;

    bool Infused() const { return Reaped == SoulCap; }

    Souls Gain(std::uint32_t fragments, std::uint32_t souls) const
    {
        std::uint64_t total = std::uint64_t(Fragments) + fragments;
        return { std::uint8_t(total % FragmentsPerSoul),
            std::uint8_t(std::min<std::uint64_t>(SoulCap, std::uint64_t(Reaped) + souls + total / FragmentsPerSoul)) };
    }
};

inline std::int32_t StrikeHealing(std::uint32_t damage, std::uint64_t health, std::uint64_t maximum)
{
    std::uint64_t missing = maximum - std::min(health, maximum);
    // Divide before multiplying the unbounded health value. Damage is the
    // native post-mitigation hit amount, including overkill, as in the source.
    std::uint64_t heal = std::uint64_t(damage) * 80 / 100 + missing / 10;
    return std::int32_t(std::min<std::uint64_t>(heal, std::numeric_limits<std::int32_t>::max()));
}

struct Reward
{
    std::uint8_t Fragments = 0;
    std::uint8_t Souls = 0;
    bool ReapPower = false;
    bool RendPower = false;
    bool Heal = false;
    bool Debuff = false;
    bool Consume = false;
};

// One instance belongs to one native SpellScript, never to a global player map.
class CastRewards
{
public:
    Reward Hit(std::uint32_t spell, bool hostileHit, bool immune, std::uint32_t damage, bool collector)
    {
        if (_handled || !hostileHit)
            return {};
        _handled = true;
        if (spell == Soulrend)
            return { 0, 0, false, !immune, false, false, true };
        if (immune)
            return {};
        switch (spell)
        {
            case Reap: return { std::uint8_t(collector && damage > 0), 0, damage > 0 };
            case Murder: return { 0, std::uint8_t(collector), false, false, false, true };
            case SoulStrike: return { 0, 1, false, false, true };
            default: return {};
        }
    }

private:
    bool _handled = false;
};
}
#endif

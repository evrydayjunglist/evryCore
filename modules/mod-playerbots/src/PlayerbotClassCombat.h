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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EVRY_MOD_PLAYERBOT_CLASS_COMBAT_H
#define EVRY_MOD_PLAYERBOT_CLASS_COMBAT_H

#include "DB2Stores.h"
#include "Define.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include <array>
#include <atomic>
#include <span>
#include <string>

// The core attack spells of each class for the first twenty levels, in the order she presses them (owner's pick,
// 1 October 2026). In a fight she presses the first one she knows that the server would let her cast now; when none
// can be, the general picker looks at every damage spell she knows, as before. A spell she has not learned is passed
// over, and nothing here learns, grants or skips anything: every press is the same CMSG_CAST_SPELL a player sends.
// The first listed spell that reaches from where she stands is also her pull.
struct PlayerbotClassSpell
{
    uint32 SpellId;
    // A damage over time: not pressed again while her own aura from it (or from the spell it triggers) is on the target.
    bool WhileNotOnTarget = false;
};

inline std::span<PlayerbotClassSpell const> PlayerbotClassAttackSpells(uint8 classId)
{
    static constexpr PlayerbotClassSpell Warrior[] = { { 100 }, { 34428 }, { 163201 }, { 23922 }, { 1464 } };
    static constexpr PlayerbotClassSpell Paladin[] = { { 20271 }, { 35395 }, { 53600 } };
    static constexpr PlayerbotClassSpell Hunter[] = { { 34026 }, { 185358 }, { 56641 }, { 75 } };
    static constexpr PlayerbotClassSpell Rogue[] = { { 196819 }, { 1752 } };
    static constexpr PlayerbotClassSpell Priest[] = { { 589, true }, { 8092 }, { 585 } };
    static constexpr PlayerbotClassSpell DeathKnight[] = { { 49998 }, { 47541 } };
    static constexpr PlayerbotClassSpell Shaman[] = { { 188389, true }, { 73899 }, { 188196 } };
    static constexpr PlayerbotClassSpell Mage[] = { { 319836 }, { 116 } };
    static constexpr PlayerbotClassSpell Warlock[] = { { 172, true }, { 686 }, { 234153 } };
    static constexpr PlayerbotClassSpell Monk[] = { { 107428 }, { 100784 }, { 100780 } };
    static constexpr PlayerbotClassSpell Druid[] = { { 8921, true }, { 5176 } };
    static constexpr PlayerbotClassSpell DemonHunter[] = { { 162794 }, { 162243 }, { 185123 } };
    static constexpr PlayerbotClassSpell Evoker[] = { { 361469 }, { 362969 } };

    switch (classId)
    {
        case CLASS_WARRIOR: return Warrior;
        case CLASS_PALADIN: return Paladin;
        case CLASS_HUNTER: return Hunter;
        case CLASS_ROGUE: return Rogue;
        case CLASS_PRIEST: return Priest;
        case CLASS_DEATH_KNIGHT: return DeathKnight;
        case CLASS_SHAMAN: return Shaman;
        case CLASS_MAGE: return Mage;
        case CLASS_WARLOCK: return Warlock;
        case CLASS_MONK: return Monk;
        case CLASS_DRUID: return Druid;
        case CLASS_DEMON_HUNTER: return DemonHunter;
        case CLASS_EVOKER: return Evoker;
        default: return {};
    }
}

// A finishing move (Eviscerate) waits for this many combo points, or for as many as she can hold when that is fewer.
constexpr int32 PLAYERBOT_FINISHER_COMBO_POINTS = 4;

// The longest class list above. Counts for each listed spell are kept by its place on the list.
constexpr std::size_t PLAYERBOT_CLASS_SPELLS_MAX = 5;

// What bots of each class did in fights since the last report. Bot brains can run on map threads, so every count is
// an atomic and the report takes and clears them.
class PlayerbotClassCombatStats
{
public:
    static PlayerbotClassCombatStats& Instance()
    {
        static PlayerbotClassCombatStats stats;
        return stats;
    }

    void NoteAttackPress(uint8 classId, uint32 spellId)
    {
        if (classId >= MAX_CLASSES)
            return;
        _attackPresses[classId].fetch_add(1, std::memory_order_relaxed);
        std::span<PlayerbotClassSpell const> const listed = PlayerbotClassAttackSpells(classId);
        for (std::size_t i = 0; i < listed.size() && i < PLAYERBOT_CLASS_SPELLS_MAX; ++i)
            if (listed[i].SpellId == spellId)
                _listedPresses[classId][i].fetch_add(1, std::memory_order_relaxed);
    }

    void NoteSelfPress(uint8 classId)
    {
        if (classId < MAX_CLASSES)
            _selfPresses[classId].fetch_add(1, std::memory_order_relaxed);
    }

    void NoteDeath(uint8 classId)
    {
        if (classId < MAX_CLASSES)
            _deaths[classId].fetch_add(1, std::memory_order_relaxed);
    }

    // One part per class with a bot in the world or anything counted, and clears the counts.
    std::string DescribeAndClear(std::array<uint32, MAX_CLASSES> const& inWorld)
    {
        std::string text;
        for (uint8 classId = 1; classId < MAX_CLASSES; ++classId)
        {
            uint32 const attacks = _attackPresses[classId].exchange(0, std::memory_order_relaxed);
            uint32 const selves = _selfPresses[classId].exchange(0, std::memory_order_relaxed);
            uint32 const deaths = _deaths[classId].exchange(0, std::memory_order_relaxed);
            std::string listedText;
            uint32 listedTotal = 0;
            std::span<PlayerbotClassSpell const> const listed = PlayerbotClassAttackSpells(classId);
            for (std::size_t i = 0; i < PLAYERBOT_CLASS_SPELLS_MAX; ++i)
            {
                uint32 const presses = _listedPresses[classId][i].exchange(0, std::memory_order_relaxed);
                if (i >= listed.size() || !presses)
                    continue;
                listedTotal += presses;
                listedText += Trinity::StringFormat("{}spell {} x{}", listedText.empty() ? "" : ", ", listed[i].SpellId, presses);
            }

            if (!inWorld[classId] && !attacks && !selves && !deaths)
                continue;

            if (!text.empty())
                text += "; ";
            ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId);
            std::string const name = entry ? std::string(entry->Name[DEFAULT_LOCALE]) : Trinity::StringFormat("class {}", classId);
            text += Trinity::StringFormat("{} ({} in the world): {} attack press(es), {} from her class list{}{}{}, "
                "{} on herself, {} death(s)", name, inWorld[classId], attacks, listedTotal, listedText.empty() ? "" : " (",
                listedText, listedText.empty() ? "" : ")", selves, deaths);
        }
        return text.empty() ? std::string("no bot of any class was in the world") : text;
    }

private:
    std::array<std::atomic<uint32>, MAX_CLASSES> _attackPresses{};
    std::array<std::array<std::atomic<uint32>, PLAYERBOT_CLASS_SPELLS_MAX>, MAX_CLASSES> _listedPresses{};
    std::array<std::atomic<uint32>, MAX_CLASSES> _selfPresses{};
    std::array<std::atomic<uint32>, MAX_CLASSES> _deaths{};
};

#endif

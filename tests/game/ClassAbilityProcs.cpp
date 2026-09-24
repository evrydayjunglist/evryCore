/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "DB2Structure.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

TEST_CASE("Class ability procs respect family filters at cast end", "[ClassAbilities][Procs]")
{
    SpellNameEntry spellName{};
    spellName.ID = 1;
    SpellInfo spell(&spellName, DIFFICULTY_NONE, std::vector<SpellEffectEntry>{});
    spell.SpellFamilyName = SPELLFAMILY_WARLOCK;
    spell.SpellFamilyFlags[0] = 0x4;
    DamageInfo damage(nullptr, nullptr, 0, &spell, SPELL_SCHOOL_MASK_FIRE, SPELL_DIRECT_DAMAGE, BASE_ATTACK);

    for (ProcFlagsInit flags : { ProcFlagsInit(PROC_FLAG_CAST_ENDED), ProcFlagsInit(PROC_FLAG_NONE, PROC_FLAG_2_CAST_SUCCESSFUL), ProcFlagsInit(PROC_FLAG_DEAL_HARMFUL_SPELL) })
    {
        SpellProcEntry entry{};
        entry.ProcFlags = flags;
        entry.SpellFamilyName = SPELLFAMILY_WARLOCK;
        entry.SpellFamilyMask[0] = 0x4;
        entry.SpellPhaseMask = PROC_SPELL_PHASE_HIT;
        ProcEventInfo event(nullptr, nullptr, flags, PROC_SPELL_TYPE_DAMAGE, PROC_SPELL_PHASE_HIT, PROC_HIT_NORMAL, nullptr, &damage, nullptr);
        CHECK(SpellMgr::CanSpellTriggerProcOnEvent(entry, event));

        entry.SpellFamilyMask[0] = 0x8;
        CHECK_FALSE(SpellMgr::CanSpellTriggerProcOnEvent(entry, event));
        entry.SpellFamilyMask[0] = 0x4;
        entry.SpellFamilyName = SPELLFAMILY_MAGE;
        CHECK_FALSE(SpellMgr::CanSpellTriggerProcOnEvent(entry, event));
    }
}

TEST_CASE("Ordinary melee procs still allow events without spell data", "[ClassAbilities][Procs]")
{
    SpellProcEntry entry{};
    entry.ProcFlags = PROC_FLAG_DEAL_MELEE_SWING;
    DamageInfo damage(nullptr, nullptr, 10, nullptr, SPELL_SCHOOL_MASK_NORMAL, DIRECT_DAMAGE, BASE_ATTACK);
    ProcEventInfo event(nullptr, nullptr, PROC_FLAG_DEAL_MELEE_SWING, PROC_SPELL_TYPE_NONE, PROC_SPELL_PHASE_NONE,
        PROC_HIT_NORMAL, nullptr, &damage, nullptr);
    CHECK(SpellMgr::CanSpellTriggerProcOnEvent(entry, event));
}

/* This file is part of the TrinityCore Project. See AUTHORS file for Copyright information.
 * Licensed under the GNU General Public License, version 2 or later. */

#include "ReaperSpellMechanics.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellScript.h"

namespace
{
using namespace Coa::Reaper;

uint8 Stacks(Unit* caster, uint32 spell)
{
    if (Aura* aura = caster->GetAura(spell, caster->GetGUID()))
        return aura->GetStackAmount();
    return 0;
}

void SetStacks(Unit* caster, uint32 spell, uint8 count)
{
    if (!count)
    {
        caster->RemoveAurasDueToSpell(spell, caster->GetGUID());
        return;
    }
    Aura* aura = caster->GetAura(spell, caster->GetGUID());
    if (!aura)
        aura = caster->AddAura(spell, caster);
    if (aura)
    {
        aura->SetStackAmount(count);
        aura->RefreshDuration();
    }
}

void ClearSouls(Unit* caster)
{
    // Removal hooks can repeat these removals. The core detaches an aura
    // before its remove hook, so there is no recursive cycle or second owner.
    caster->RemoveAurasDueToSpell(SoulInfusion, caster->GetGUID());
    caster->RemoveAurasDueToSpell(ReapedSoul, caster->GetGUID());
    caster->RemoveAurasDueToSpell(SoulFragment, caster->GetGUID());
}

void GainSouls(Unit* caster, uint8 fragments, uint8 souls)
{
    Souls current{ Stacks(caster, SoulFragment), Stacks(caster, ReapedSoul) };
    Souls next = current.Gain(fragments, souls);
    if (fragments)
        SetStacks(caster, SoulFragment, next.Fragments);
    if (souls || current.Fragments + fragments >= FragmentsPerSoul)
        SetStacks(caster, ReapedSoul, next.Reaped);
    if (next.Infused() && !Stacks(caster, SoulInfusion))
        SetStacks(caster, SoulInfusion, 1);
}

class spell_coa_reaper_combat : public SpellScript
{
    PrepareSpellScript(spell_coa_reaper_combat);

    CastRewards _rewards;
    ObjectGuid _landedTarget;

    bool Validate(SpellInfo const*) override
    {
        return ValidateSpellInfo({ SoulCollector, SoulFragment, ReapedSoul, SoulInfusion,
            ReapPower, SoulrendPower, SoulStrikeHeal, MurderDebuff });
    }

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player || player->GetClass() != CLASS_REAPER)
            return SPELL_FAILED_SPELL_UNAVAILABLE;
        if (GetSpellInfo()->Id == Soulrend &&
            (Stacks(player, ReapedSoul) != SoulCap || !player->HasAura(SoulInfusion, player->GetGUID())))
            return SPELL_FAILED_CASTER_AURASTATE;
        return SPELL_CAST_OK;
    }

    void Landed(SpellEffIndex)
    {
        // EffectHitTarget is not called on a miss, immunity or an interrupted
        // cast. It still runs when subsequent damage is completely absorbed.
        if (Unit* target = GetHitUnit(); target && target != GetCaster() &&
            !GetCaster()->IsFriendlyTo(target))
            _landedTarget = target->GetGUID();
    }

    void RollDamage(SpellEffectInfo const& effect, Unit*, int32& damage, int32&, float&)
    {
        // CoA uses an integer 1..2 die. Retail's floating variance followed by
        // truncation cannot express the same two equally likely base values.
        if (effect.EffectIndex == EFFECT_0 && (GetSpellInfo()->Id == Murder || GetSpellInfo()->Id == Soulrend))
            damage += int32(urand(0, 1));
    }

    void AfterHitTarget()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target || target->GetGUID() != _landedTarget || !caster->IsAlive())
            return;
        uint32 damage = uint32(std::max(0, GetHitDamage()));
        Reward reward = _rewards.Hit(GetSpellInfo()->Id, true,
            target->IsImmunedToDamage(caster, GetSpellInfo()), damage,
            caster->HasAura(SoulCollector, caster->GetGUID()));
        if (reward.Consume)
            ClearSouls(caster);
        if (reward.ReapPower)
            caster->CastSpell(caster, ReapPower, CastSpellExtraArgs(TRIGGERED_FULL_MASK)
                .AddSpellBP0(int32(urand(70, 130))));
        if (reward.RendPower)
            caster->CastSpell(caster, SoulrendPower, true);
        if (reward.Fragments || reward.Souls)
            GainSouls(caster, reward.Fragments, reward.Souls);
        if (reward.Heal)
            caster->CastSpell(caster, SoulStrikeHeal, CastSpellExtraArgs(TRIGGERED_FULL_MASK)
                .AddSpellBP0(StrikeHealing(damage, caster->GetHealth(), caster->GetMaxHealth())));
        if (reward.Debuff && target->IsAlive())
            caster->CastSpell(target, MurderDebuff, true);
    }

    void AfterCastSpell()
    {
        // Soulrend is immediate. Its source consumes infusion on immunity,
        // but not on miss/dodge/parry. Whole-spell immunity has no hit hook.
        if (GetSpellInfo()->Id != Soulrend)
            return;
        Unit* caster = GetCaster();
        Unit* target = GetExplTargetUnit();
        if (target && caster->IsAlive() && !caster->IsFriendlyTo(target) &&
            target->IsImmunedToSpell(GetSpellInfo(), 1, caster))
            if (_rewards.Hit(Soulrend, true, true, 0, false).Consume)
                ClearSouls(caster);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_coa_reaper_combat::CheckCast);
        CalcDamage += SpellCalcDamageFn(spell_coa_reaper_combat::RollDamage);
        OnEffectHitTarget += SpellEffectFn(spell_coa_reaper_combat::Landed, EFFECT_0, SPELL_EFFECT_ANY);
        AfterHit += SpellHitFn(spell_coa_reaper_combat::AfterHitTarget);
        AfterCast += SpellCastFn(spell_coa_reaper_combat::AfterCastSpell);
    }
};

class spell_coa_reaper_souls : public AuraScript
{
    PrepareAuraScript(spell_coa_reaper_souls);

    void Removed(AuraEffect const*, AuraEffectHandleModes)
    {
        Unit* target = GetTarget();
        if (GetCasterGUID() != target->GetGUID())
            return;
        if (GetId() == SoulInfusion)
            ClearSouls(target);
        else
            target->RemoveAurasDueToSpell(SoulInfusion, target->GetGUID());
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_coa_reaper_souls::Removed, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class CoaReaperPlayerScript : public PlayerScript
{
public:
    CoaReaperPlayerScript() : PlayerScript("mod_coa_ReaperPlayerScript") { }

    void OnLogin(Player* player, bool) override
    {
        if (player->GetClass() != CLASS_REAPER)
            return;
        // Combat counters start empty after reconnect. They are non-saveable
        // auras, so logout cannot bank infusion or restart an expired fragment.
        ClearSouls(player);
    }
};
}

void AddSC_reaper_spell_scripts()
{
    RegisterSpellScript(spell_coa_reaper_combat);
    RegisterSpellScript(spell_coa_reaper_souls);
    new CoaReaperPlayerScript();
}

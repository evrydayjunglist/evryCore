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

#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"

namespace
{
enum FelRushSpells
{
    SPELL_FEL_RUSH_DAMAGE = 192611,
    SPELL_FEL_RUSH_GROUND = 197922,
    SPELL_FEL_RUSH_AIR = 197923,
    SPELL_GLIDE = 131347,
    SPELL_GLIDE_DURATION = 197154
};
}

// 195072 - Fel Rush
class spell_dh_fel_rush : public SpellScript
{
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ SPELL_FEL_RUSH_GROUND, SPELL_FEL_RUSH_AIR, SPELL_FEL_RUSH_DAMAGE,
            SPELL_GLIDE, SPELL_GLIDE_DURATION })
            && ValidateSpellEffect({ { spellInfo->Id, EFFECT_0 }, { spellInfo->Id, EFFECT_1 } });
    }

    SpellCastResult CheckCast()
    {
        if (GetCaster()->HasUnitState(UNIT_STATE_ROOT))
            return SPELL_FAILED_ROOTED;

        return SPELL_CAST_OK;
    }

    void CastPathDamage() const
    {
        Unit* caster = GetCaster();
        float dashDistance = float(GetEffectInfo(EFFECT_0).CalcValue(caster));
        if (dashDistance <= 0.0f)
            return;

        // Resolve the native line targets before the dash disables spell casting.
        caster->CastSpell(caster->GetFirstCollisionPosition(dashDistance, 0.0f), SPELL_FEL_RUSH_DAMAGE, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_FULL_MASK,
            .TriggeringSpell = GetSpell()
        });
    }

    void PrepareDash(Unit* caster) const
    {
        caster->RemoveAurasDueToSpell(SPELL_GLIDE);
        caster->RemoveAurasDueToSpell(SPELL_GLIDE_DURATION);
        caster->m_movementInfo.inertia.reset();
        caster->m_movementInfo.advFlying.reset();

        if (Player* player = caster->ToPlayer())
        {
            player->GetSpellHistory()->StartCooldown(sSpellMgr->AssertSpellInfo(SPELL_GLIDE, GetCastDifficulty()), 0, nullptr, false, 250ms);
            player->UpdateSpeed(MOVE_FLIGHT);
        }
    }

    void HandleGroundDash(SpellEffIndex /*effIndex*/) const
    {
        Unit* caster = GetCaster();
        if (caster->IsFalling() && !caster->IsInWater())
            return;

        CastPathDamage();
        PrepareDash(caster);
        caster->CastSpell(caster, SPELL_FEL_RUSH_GROUND, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_FULL_MASK,
            .TriggeringSpell = GetSpell()
        });
    }

    void HandleAirDash(SpellEffIndex /*effIndex*/) const
    {
        Unit* caster = GetCaster();
        if (!caster->IsFalling() || caster->IsInWater())
            return;

        CastPathDamage();
        PrepareDash(caster);
        caster->CastSpell(caster, SPELL_FEL_RUSH_AIR, CastSpellExtraArgsInit{
            .TriggerFlags = TRIGGERED_FULL_MASK,
            .TriggeringSpell = GetSpell()
        });
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_dh_fel_rush::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_dh_fel_rush::HandleGroundDash, EFFECT_0, SPELL_EFFECT_DUMMY);
        OnEffectHitTarget += SpellEffectFn(spell_dh_fel_rush::HandleAirDash, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

// 197922 - Fel Rush (ground)
// 197923 - Fel Rush (air)
class spell_dh_fel_rush_aura : public AuraScript
{
    void CalcImmunityAmount(AuraEffect const* /*aurEff*/, SpellEffectValue& amount, bool& /*canBeRecalculated*/) const
    {
        amount -= 100.0f;
    }

    void ChangeRunBackSpeed(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/) const
    {
        Unit* target = GetTarget();
        if (!target->IsDeferringDashMovementSpeedUpdates())
            target->SetSpeed(MOVE_RUN_BACK, target->GetSpeed(MOVE_RUN));
    }

    void RestoreRunBackSpeed(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/) const
    {
        Unit* target = GetTarget();
        if (!target->IsDeferringDashMovementSpeedUpdates())
            target->UpdateSpeed(MOVE_RUN_BACK);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_dh_fel_rush_aura::ChangeRunBackSpeed, EFFECT_4, SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_dh_fel_rush_aura::RestoreRunBackSpeed, EFFECT_4, SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED, AURA_EFFECT_HANDLE_REAL);

        // The air bundle's mechanic immunity occupies a different effect.
        if (m_scriptSpellId == SPELL_FEL_RUSH_GROUND)
            DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_dh_fel_rush_aura::CalcImmunityAmount, EFFECT_5, SPELL_AURA_MECHANIC_IMMUNITY);
    }
};

void AddSC_demon_hunter_fel_rush_spell_scripts()
{
    RegisterSpellScript(spell_dh_fel_rush);
    RegisterSpellScript(spell_dh_fel_rush_aura);
}

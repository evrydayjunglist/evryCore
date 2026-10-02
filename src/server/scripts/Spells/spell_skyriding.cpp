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

/*
 * Scripts for Skyriding abilities.
 * Scriptnames of files in this file should be prefixed with "spell_skyriding_".
 *
 * The client flies a Skyriding mount on its own physics. The server pushes it with
 * SMSG_MOVE_ADD_IMPULSE, which the client acknowledges like any other movement order.
 * The impulse vectors below come from retail packet captures: Lift Off (0, 0, 45),
 * Skyward Ascent 12.25 along the facing plus 49 up, Surge Forward 18
 * and Whirling Surge 60 along the facing and pitch.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "SpellScript.h"
#include "Unit.h"
#include <cmath>

enum SkyridingSpells
{
    SPELL_SKYRIDING_LAUNCH_BOOST            = 392752,
    SPELL_SKYRIDING_FLIGHT_STYLE_STEADY     = 404468
};

namespace
{
constexpr float SKYWARD_ASCENT_IMPULSE_FORWARD  = 12.25f;
constexpr float SKYWARD_ASCENT_IMPULSE_UP       = 49.0f;
constexpr float SURGE_FORWARD_IMPULSE           = 18.0f;
constexpr float WHIRLING_SURGE_IMPULSE          = 60.0f;

// Push along where the rider is looking: the facing and the pitch of the mount.
void SendFacingAndPitchImpulse(Unit* caster, float speed)
{
    float const orientation = caster->GetOrientation();
    float const pitch = caster->m_movementInfo.pitch;
    float const forward = std::cos(pitch) * speed;
    caster->SendAddImpulse(Position(std::cos(orientation) * forward, std::sin(orientation) * forward, std::sin(pitch) * speed));
}
}

// 374763 - Lift Off
class spell_skyriding_lift_off : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SKYRIDING_LAUNCH_BOOST });
    }

    void HandleLaunch(SpellEffIndex /*effIndex*/) const
    {
        Unit* caster = GetCaster();

        // The effect's base points are tenths of the upward speed: 450 is the (0, 0, 45) retail sends.
        caster->SendAddImpulse(Position(0.0f, 0.0f, float(GetEffectValue() / 10.0)));

        // Retail casts Launch Boost as part of the Lift Off cast; no spell effect in the data triggers it.
        caster->CastSpell(caster, SPELL_SKYRIDING_LAUNCH_BOOST, CastSpellExtraArgs(TRIGGERED_FULL_MASK)
            .SetTriggeringSpell(GetSpell()));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_skyriding_lift_off::HandleLaunch, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 372610 - Skyward Ascent
class spell_skyriding_skyward_ascent : public SpellScript
{
    void HandleAscent(SpellEffIndex /*effIndex*/) const
    {
        // Retail sends the same upward push whatever the pitch, with a little forward speed along the facing.
        Unit* caster = GetCaster();
        float const orientation = caster->GetOrientation();
        caster->SendAddImpulse(Position(std::cos(orientation) * SKYWARD_ASCENT_IMPULSE_FORWARD,
            std::sin(orientation) * SKYWARD_ASCENT_IMPULSE_FORWARD, SKYWARD_ASCENT_IMPULSE_UP));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_skyriding_skyward_ascent::HandleAscent, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 372608 - Surge Forward
class spell_skyriding_surge_forward : public SpellScript
{
    void HandleSurge(SpellEffIndex /*effIndex*/) const
    {
        SendFacingAndPitchImpulse(GetCaster(), SURGE_FORWARD_IMPULSE);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_skyriding_surge_forward::HandleSurge, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 361584 - Whirling Surge
class spell_skyriding_whirling_surge : public SpellScript
{
    void HandleWhirl() const
    {
        SendFacingAndPitchImpulse(GetCaster(), WHIRLING_SURGE_IMPULSE);
    }

    void Register() override
    {
        // Every effect of this spell applies an aura, so there is no dummy effect to hook; retail sends the push with the cast.
        OnCast += SpellCastFn(spell_skyriding_whirling_surge::HandleWhirl);
    }
};

// 436854 - Switch Flight Style
class spell_skyriding_switch_flight_style : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SKYRIDING_FLIGHT_STYLE_STEADY });
    }

    void HandleSwitch(SpellEffIndex /*effIndex*/) const
    {
        // Steady Flight is the "Flight Style: Steady" aura. The Skyriding mount capabilities require it to be absent,
        // so checking the mount again moves a mount the rider is already on to the new style.
        Unit* caster = GetCaster();
        if (caster->HasAura(SPELL_SKYRIDING_FLIGHT_STYLE_STEADY))
            caster->RemoveAurasDueToSpell(SPELL_SKYRIDING_FLIGHT_STYLE_STEADY);
        else
            caster->CastSpell(caster, SPELL_SKYRIDING_FLIGHT_STYLE_STEADY, CastSpellExtraArgs(TRIGGERED_FULL_MASK)
                .SetTriggeringSpell(GetSpell()));

        caster->UpdateMountCapability();
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_skyriding_switch_flight_style::HandleSwitch, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

void AddSC_skyriding_spell_scripts()
{
    RegisterSpellScript(spell_skyriding_lift_off);
    RegisterSpellScript(spell_skyriding_skyward_ascent);
    RegisterSpellScript(spell_skyriding_surge_forward);
    RegisterSpellScript(spell_skyriding_whirling_surge);
    RegisterSpellScript(spell_skyriding_switch_flight_style);
}

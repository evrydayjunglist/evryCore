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

#include "DB2Stores.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "Unit.h"

enum EasternKingdomsTranslocation
{
    SPELL_TRANSLOCATION_DUSKWITHER_SPIRE_UP       = 26566,
    SPELL_TRANSLOCATION_DUSKWITHER_SPIRE_DOWN     = 26572,
    SPELL_TRANSLOCATION_SILVERMOON_TO_UNDERCITY   = 25649,
    SPELL_TRANSLOCATION_UNDERCITY_TO_SILVERMOON   = 35730
};

// 34448 - Translocate
class spell_eastern_kingdoms_duskwither_spire_up : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TRANSLOCATION_DUSKWITHER_SPIRE_UP });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_TRANSLOCATION_DUSKWITHER_SPIRE_UP);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_eastern_kingdoms_duskwither_spire_up::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 34452 - Translocate
class spell_eastern_kingdoms_duskwither_spire_down : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TRANSLOCATION_DUSKWITHER_SPIRE_DOWN });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_TRANSLOCATION_DUSKWITHER_SPIRE_DOWN);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_eastern_kingdoms_duskwither_spire_down::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 35376 - Translocate
class spell_eastern_kingdoms_silvermoon_to_undercity : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TRANSLOCATION_SILVERMOON_TO_UNDERCITY });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_TRANSLOCATION_SILVERMOON_TO_UNDERCITY);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_eastern_kingdoms_silvermoon_to_undercity::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 35727 - Translocate
class spell_eastern_kingdoms_undercity_to_silvermoon : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TRANSLOCATION_UNDERCITY_TO_SILVERMOON });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_TRANSLOCATION_UNDERCITY_TO_SILVERMOON);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_eastern_kingdoms_undercity_to_silvermoon::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 11532: Distraction at the Dead Scar / 11533: The Air Strikes Must Continue
######*/

enum DeadScarBombingRun
{
    SOUND_ID_BOMBING_RUN       = 12318
};

// 45071 - Quest - Sunwell Daily - Dead Scar Bombing Run
class spell_eastern_kingdoms_dead_scar_bombing_run : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sSoundKitStore.LookupEntry(SOUND_ID_BOMBING_RUN);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->PlayDirectSound(SOUND_ID_BOMBING_RUN);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_eastern_kingdoms_dead_scar_bombing_run::HandleScript, EFFECT_2, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum DawnbladeAttack
{
    SPELL_DAWNBLADE_ATTACK     = 45189
};

// 45188 - Dawnblade Attack
class spell_eastern_kingdoms_dawnblade_attack : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DAWNBLADE_ATTACK });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), SPELL_DAWNBLADE_ATTACK);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_eastern_kingdoms_dawnblade_attack::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 2203: Badlands Reagent Run II
######*/

enum BadlandsReagentRun
{
    SPELL_THAUMATURGY_CHANNEL    = 21029
};

// 9712 - Thaumaturgy Channel
class spell_eastern_kingdoms_thaumaturgy_channel : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_THAUMATURGY_CHANNEL });
    }

    void HandleEffectPeriodic(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();
        if (Unit* caster = GetCaster())
            caster->CastSpell(caster, SPELL_THAUMATURGY_CHANNEL, false);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_eastern_kingdoms_thaumaturgy_channel::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

void AddSC_arathi_highlands_rpe();

void AddSC_eastern_kingdoms()
{
    RegisterSpellScript(spell_eastern_kingdoms_duskwither_spire_up);
    RegisterSpellScript(spell_eastern_kingdoms_duskwither_spire_down);
    RegisterSpellScript(spell_eastern_kingdoms_silvermoon_to_undercity);
    RegisterSpellScript(spell_eastern_kingdoms_undercity_to_silvermoon);
    RegisterSpellScript(spell_eastern_kingdoms_dead_scar_bombing_run);
    RegisterSpellScript(spell_eastern_kingdoms_dawnblade_attack);
    RegisterSpellScript(spell_eastern_kingdoms_thaumaturgy_channel);
    AddSC_arathi_highlands_rpe();
}

enum ArathiRpe
{
    MAP_ARATHI_RPE                      = 2927,

    QUEST_TO_GOSHEK_FARM                = 90883,
    QUEST_MY_BEAUTIFUL_PUMPKINS         = 90885,
    QUEST_CATAPULT_BOMBARDMENT          = 90895,

    NPC_CREDIT_ARATHI_RPE_MOUNT         = 239009,
    NPC_RPE_THRALL_PAD                 = 244642,
    NPC_RPE_JAINA_PAD                  = 244643,
    NPC_ARATHI_RPE_PRIZED_PUMPKIN      = 244956,
    NPC_ARATHI_RPE_WORN_CATAPULT        = 249269,

    SPELL_ARATHI_RPE_SCENE_AMBIENT     = 1237116, // SceneId 3692
    SPELL_ARATHI_RPE_SCENE_STASIS      = 1248494  // SceneId 3749
};

// Quest 90883 credits when the player mounts on map 2927. Retail sends the credit with an empty
// victim after the mount spell. Keep this out of HandleAuraMounted so it does not fire for every mount.
class player_arathi_rpe_mount_credit : public PlayerScript
{
public:
    player_arathi_rpe_mount_credit() : PlayerScript("player_arathi_rpe_mount_credit") { }

    void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (player->GetMapId() != MAP_ARATHI_RPE)
            return;

        if (player->GetQuestStatus(QUEST_TO_GOSHEK_FARM) != QUEST_STATUS_INCOMPLETE)
            return;

        if (!spell->GetSpellInfo()->HasAura(SPELL_AURA_MOUNTED))
            return;

        player->KilledMonsterCredit(NPC_CREDIT_ARATHI_RPE_MOUNT);
    }
};

// Pad PLAY_SCENE auras (sniff J). Cast on the player; SPELL_AURA_PLAY_SCENE only plays for a player target.
class player_arathi_rpe_pad_scenes : public PlayerScript
{
public:
    player_arathi_rpe_pad_scenes() : PlayerScript("player_arathi_rpe_pad_scenes") { }

    void OnMapChanged(Player* player) override
    {
        if (player->GetMapId() != MAP_ARATHI_RPE)
        {
            player->RemoveAurasDueToSpell(SPELL_ARATHI_RPE_SCENE_AMBIENT);
            player->RemoveAurasDueToSpell(SPELL_ARATHI_RPE_SCENE_STASIS);
            return;
        }

        if (!player->HasAura(SPELL_ARATHI_RPE_SCENE_AMBIENT))
            player->CastSpell(player, SPELL_ARATHI_RPE_SCENE_AMBIENT, true);
        if (!player->HasAura(SPELL_ARATHI_RPE_SCENE_STASIS))
            player->CastSpell(player, SPELL_ARATHI_RPE_SCENE_STASIS, true);
    }
};

// Pad Thrall and Jaina stand together. Shared quests 90882/90883 are offered only by the player's own leader.
struct npc_arathi_rpe_leader : public ScriptedAI
{
    npc_arathi_rpe_leader(Creature* creature) : ScriptedAI(creature) { }

    bool IsWrongFactionLeaderFor(Player const* player) const
    {
        switch (me->GetEntry())
        {
            case NPC_RPE_JAINA_PAD:
                return player->GetTeamId() != TEAM_ALLIANCE;
            case NPC_RPE_THRALL_PAD:
                return player->GetTeamId() != TEAM_HORDE;
            default:
                return false;
        }
    }

    Optional<QuestGiverStatus> GetDialogStatus(Player const* player) override
    {
        if (IsWrongFactionLeaderFor(player))
            return QuestGiverStatus::None;
        return {};
    }

    bool OnGossipHello(Player* player) override
    {
        if (IsWrongFactionLeaderFor(player))
        {
            CloseGossipMenuFor(player);
            return true;
        }
        return false;
    }
};

// Prized Pumpkin (244956): spellclick for 90885. Credit is the pumpkin entry; the Recovering spell is the click visual.
struct npc_arathi_rpe_prized_pumpkin : public ScriptedAI
{
    npc_arathi_rpe_prized_pumpkin(Creature* creature) : ScriptedAI(creature) { }

    void OnSpellClick(Unit* clicker, bool /*spellClickHandled*/) override
    {
        Player* player = clicker ? clicker->ToPlayer() : nullptr;
        if (!player || player->GetQuestStatus(QUEST_MY_BEAUTIFUL_PUMPKINS) != QUEST_STATUS_INCOMPLETE)
            return;

        player->KilledMonsterCredit(NPC_ARATHI_RPE_PRIZED_PUMPKIN);
        me->DespawnOrUnsummon(0s, 120s);
    }
};

// Worn Catapult (249269): spellclick for 90895. Credit only; the siege fire loop is later work.
struct npc_arathi_rpe_worn_catapult : public ScriptedAI
{
    npc_arathi_rpe_worn_catapult(Creature* creature) : ScriptedAI(creature) { }

    void OnSpellClick(Unit* clicker, bool /*spellClickHandled*/) override
    {
        Player* player = clicker ? clicker->ToPlayer() : nullptr;
        if (!player || player->GetQuestStatus(QUEST_CATAPULT_BOMBARDMENT) != QUEST_STATUS_INCOMPLETE)
            return;

        player->KilledMonsterCredit(NPC_ARATHI_RPE_WORN_CATAPULT);
    }
};

void AddSC_arathi_highlands_rpe()
{
    new player_arathi_rpe_mount_credit();
    new player_arathi_rpe_pad_scenes();
    RegisterCreatureAI(npc_arathi_rpe_leader);
    RegisterCreatureAI(npc_arathi_rpe_prized_pumpkin);
    RegisterCreatureAI(npc_arathi_rpe_worn_catapult);
}

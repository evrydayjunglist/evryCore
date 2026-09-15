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
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "Timerunning.h"
#include "TimerunningPandaria.h"
#include "World.h"
#include "WorldSession.h"

namespace
{
constexpr uint32 QuestItsAboutTime = 79432;
constexpr uint32 QuestWhatsHoursIsYours = 79435;
constexpr uint32 QuestKnotMyProblem = 79437;
constexpr uint32 QuestRecallingTheWar = 79440;
constexpr uint32 NpcEternus = 216591;
constexpr uint32 NpcHoros = 217051;
constexpr uint32 NpcMomentus = 217668;
constexpr uint32 NpcUnstableRift = 217666;
constexpr uint32 NpcTimeRift = 217782;
constexpr uint32 ItemChronobadge = 215438;
constexpr uint32 ItemQuestCloak = 215442;
constexpr uint32 ItemCloak = 210333;
constexpr uint32 ItemChronostabilizer = 215110;
constexpr uint32 SpellCloak = 431760;
constexpr uint32 SpellAdvantage = 440393;

bool IsTimerunner(Player const* player)
{
    return player && player->GetTimerunningSeasonId() == int32(Timerunning::Season::Pandaria) &&
        player->GetMapId() == Timerunning::Pandaria::MapId &&
        player->GetMap()->GetTimerunningSeasonId() == int32(Timerunning::Season::Pandaria);
}

bool CanUseGuide(Player* player, Creature* guide)
{
    return IsTimerunner(player) && player->GetNPCIfCanInteractWith(guide->GetGUID(), UNIT_NPC_FLAG_GOSSIP, UNIT_NPC_FLAG_2_NONE) == guide;
}

bool HasValidatedSpellScript(uint32 spellId, std::string_view name)
{
    auto [begin, end] = sObjectMgr->GetSpellScriptsBounds(spellId);
    for (auto itr = begin; itr != end; ++itr)
        if (itr->second.second && sObjectMgr->GetScriptName(itr->second.first) == name)
            return true;
    return false;
}
}

// Placement and dialogue are reconstructed. Objectives and rewards use the existing quests.
struct npc_timerunning_pandaria_guide : ScriptedAI
{
    using ScriptedAI::ScriptedAI;

    bool OnGossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);
        if (!CanUseGuide(player, me))
            return true;

        player->PrepareQuestMenu(me->GetGUID());
        switch (me->GetEntry())
        {
            case NpcUnstableRift:
                if (player->GetQuestStatus(QuestItsAboutTime) == QUEST_STATUS_INCOMPLETE)
                    AddGossipItemFor(player, GossipOptionNpc::None, "Investigate the unstable rift.", GOSSIP_SENDER_MAIN, 1);
                break;
            case NpcHoros:
                if (player->GetQuestStatus(QuestWhatsHoursIsYours) == QUEST_STATUS_INCOMPLETE)
                    AddGossipItemFor(player, GossipOptionNpc::Vendor, GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_SENDER_MAIN, 2);
                break;
            case NpcMomentus:
                if (player->GetQuestStatus(QuestWhatsHoursIsYours) == QUEST_STATUS_INCOMPLETE && player->HasItemCount(ItemChronobadge, 1))
                    AddGossipItemFor(player, GossipOptionNpc::None, "Please forge my Cloak of Infinite Potential.", GOSSIP_SENDER_MAIN, 3);
                break;
            case NpcEternus:
                if (player->GetQuestStatus(QuestRecallingTheWar) == QUEST_STATUS_INCOMPLETE)
                {
                    AddGossipItemFor(player, GossipOptionNpc::None, "Tell me about the war in Pandaria.", GOSSIP_SENDER_MAIN, 4);
                    if (player->GetQuestObjectiveData(QuestRecallingTheWar, 446403))
                        AddGossipItemFor(player, GossipOptionNpc::None, "I am ready to see how it began.", GOSSIP_SENDER_MAIN, 5);
                }
                if (player->GetQuestRewardStatus(QuestRecallingTheWar))
                    AddGossipItemFor(player, GossipOptionNpc::None, "Take me to the beginning of the campaign.", GOSSIP_SENDER_MAIN, 6);
                break;
            default:
                break;
        }
        SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        ClearGossipMenuFor(player);
        CloseGossipMenuFor(player);
        if (!CanUseGuide(player, me))
            return true;

        switch (action)
        {
            case 1:
                if (me->GetEntry() == NpcUnstableRift && player->GetQuestStatus(QuestItsAboutTime) == QUEST_STATUS_INCOMPLETE)
                    player->KilledMonsterCredit(219712);
                break;
            case 2:
                if (me->GetEntry() == NpcHoros && player->GetQuestStatus(QuestWhatsHoursIsYours) == QUEST_STATUS_INCOMPLETE)
                    player->GetSession()->SendListInventory(me->GetGUID());
                break;
            case 3:
                if (me->GetEntry() == NpcMomentus && player->GetQuestStatus(QuestWhatsHoursIsYours) == QUEST_STATUS_INCOMPLETE &&
                    player->HasItemCount(ItemChronobadge, 1) && !player->HasItemCount(ItemQuestCloak, 1))
                {
                    // Storage must succeed before consuming the purchased clasp.
                    if (player->AddItem(ItemQuestCloak, 1))
                        player->DestroyItemCount(ItemChronobadge, 1, true);
                }
                break;
            case 4:
                if (me->GetEntry() == NpcEternus && player->GetQuestStatus(QuestRecallingTheWar) == QUEST_STATUS_INCOMPLETE)
                {
                    me->Whisper("The war brought the Alliance and Horde to Pandaria. We will follow those events through the timeways, and learn from what happened.", LANG_UNIVERSAL, player);
                    player->KilledMonsterCredit(217115);
                }
                break;
            case 5:
                if (me->GetEntry() == NpcEternus && player->GetQuestStatus(QuestRecallingTheWar) == QUEST_STATUS_INCOMPLETE &&
                    player->GetQuestObjectiveData(QuestRecallingTheWar, 446403))
                {
                    player->KilledMonsterCredit(217538);
                    me->Whisper("Return to me when your preparations are complete.", LANG_UNIVERSAL, player);
                }
                break;
            case 6:
                if (me->GetEntry() == NpcEternus && player->GetQuestRewardStatus(QuestRecallingTheWar))
                    BeginCampaign(player);
                break;
            default:
                break;
        }
        return true;
    }

    void OnQuestReward(Player* player, Quest const* quest, LootItemType /*type*/, uint32 /*option*/) override
    {
        if (quest->GetQuestId() == QuestRecallingTheWar && me->GetEntry() == NpcEternus && IsTimerunner(player))
            BeginCampaign(player);
    }

private:
    void BeginCampaign(Player* player)
    {
        if (player->IsInCombat() || player->IsBeingTeleported() || (player->GetTeam() != HORDE && player->GetTeam() != ALLIANCE))
            return;

        // Eternus is the event's entry into this history. The normal capital-city
        // breadcrumb is not required, but the campaign's quest and scene still run.
        uint32 questId = player->GetTeam() == HORDE ? 29690 : 29548;
        if (player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest || !player->CanAddQuest(quest, true))
                return;
            player->AddQuestAndCheckCompletion(quest, me);
        }
        // Choosing Eternus' journey replaces securing the Alliance's capital-city flight.
        // The native arrival scene supplies the remaining discovery / admiral credit.
        if (player->GetTeam() == ALLIANCE)
            player->KilledMonsterCredit(170714);
        player->CastSpell(player, player->GetTeam() == HORDE ? 121545 : 131057, true);
    }
};

// The real item starts a channel around the caster. Only an eligible rift may receive it.
class spell_timerunning_pandaria_chronostabilizer : public SpellScript
{
    SpellCastResult CheckCast()
    {
        Player* player = GetCaster()->ToPlayer();
        if (!IsTimerunner(player) || !GetCastItem() || GetCastItem()->GetEntry() != ItemChronostabilizer ||
            player->GetQuestStatus(QuestKnotMyProblem) != QUEST_STATUS_INCOMPLETE)
            return SPELL_FAILED_BAD_TARGETS;

        Creature* rift = player->FindNearestCreature(NpcTimeRift, 8.0f);
        if (!rift || !player->IsWithinLOSInMap(rift))
            return SPELL_FAILED_BAD_TARGETS;
        return SPELL_CAST_OK;
    }

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Player* player = GetCaster()->ToPlayer();
        targets.remove_if([player](WorldObject* object)
        {
            Creature* rift = object->ToCreature();
            return !IsTimerunner(player) || !rift || rift->GetEntry() != NpcTimeRift || !rift->IsAlive() ||
                !player->IsWithinDistInMap(rift, 8.0f) || !player->IsWithinLOSInMap(rift);
        });
        if (targets.size() > 1)
        {
            targets.sort([player](WorldObject* first, WorldObject* second) { return player->GetDistance(first) < player->GetDistance(second); });
            targets.erase(std::next(targets.begin()), targets.end());
        }
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_timerunning_pandaria_chronostabilizer::CheckCast);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_timerunning_pandaria_chronostabilizer::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

class aura_timerunning_pandaria_chronostabilizer : public AuraScript
{
    void HandleTick(AuraEffect const* effect)
    {
        PreventDefaultAction();
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        Creature* rift = GetTarget()->ToCreature();
        if (!IsTimerunner(player) || !rift || rift->GetEntry() != NpcTimeRift ||
            player->GetQuestStatus(QuestKnotMyProblem) != QUEST_STATUS_INCOMPLETE ||
            !player->IsWithinDistInMap(rift, 8.0f) || !player->IsWithinLOSInMap(rift))
            return;

        // The spell's first three-second pulse closes one rift. Cancelling before it earns no credit.
        if (effect->GetTickNumber() != 1 || _closed)
            return;
        _closed = true;
        player->KilledMonsterCredit(NpcTimeRift, rift->GetGUID());
        rift->DespawnOrUnsummon(1ms, 30s);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(aura_timerunning_pandaria_chronostabilizer::HandleTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }

    bool _closed = false;
};

class aura_timerunning_pandaria_advantage : public AuraScript
{
    void CalculateAmount(AuraEffect const* effect, SpellEffectValue& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = true;
        Player const* player = GetTarget()->ToPlayer();
        amount = 0;
        if (!IsTimerunner(player) || !player->HasItemOrGemWithIdEquipped(ItemCloak, 1))
            return;

        // The item's real thread spells persist these character currencies.
        // Both XP effects use the same earned experience-thread count.
        constexpr std::array<uint32, 10> currencies = { 2853,2854,2855,2856,2857,2858,2859,2860,3001,3001 };
        if (uint32 index = effect->GetEffIndex(); index < currencies.size())
            amount = player->GetCurrencyQuantity(currencies[index]);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(aura_timerunning_pandaria_advantage::CalculateAmount, EFFECT_ALL, SPELL_AURA_ANY);
    }
};

class spell_timerunning_pandaria_thread : public SpellScript
{
    SpellCastResult CheckCast()
    {
        Player const* player = GetCaster()->ToPlayer();
        return IsTimerunner(player) && player->GetQuestRewardStatus(QuestWhatsHoursIsYours) ? SPELL_CAST_OK : SPELL_FAILED_BAD_TARGETS;
    }

    void AfterThread()
    {
        if (Player* player = GetHitPlayer())
            if (Aura* advantage = player->GetAura(SpellAdvantage))
                for (uint8 i = 0; i < 10; ++i)
                    if (AuraEffect* effect = advantage->GetEffect(i))
                        effect->RecalculateAmount();
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_timerunning_pandaria_thread::CheckCast);
        AfterHit += SpellHitFn(spell_timerunning_pandaria_thread::AfterThread);
    }
};

class player_timerunning_pandaria : public PlayerScript
{
public:
    player_timerunning_pandaria() : PlayerScript("player_timerunning_pandaria") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!IsTimerunner(player))
            return;
        player->CastSpell(player, 436658, true); // Learn the event's utility spells.
    }
};

class world_timerunning_pandaria : public WorldScript
{
public:
    world_timerunning_pandaria() : WorldScript("world_timerunning_pandaria") { }

    void OnStartup() override
    {
        bool ready = Timerunning::Pandaria::ValidateStartingLoadouts();
        auto require = [&ready](bool present, std::string_view kind, uint32 id)
        {
            if (!present)
            {
                TC_LOG_ERROR("server.loading", "Timerunning: Pandaria introduction requires valid {} {}.", kind, id);
                ready = false;
            }
        };
        require(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL) >= Timerunning::Pandaria::StartLevel, "maximum player level of at least", Timerunning::Pandaria::StartLevel);
        for (uint32 quest : { 79432,79433,79434,79435,80380,79437,79438,79440,29548,29690 })
            require(sObjectMgr->GetQuestTemplate(quest) != nullptr, "quest", quest);
        for (uint32 spell : { 431760,440393,436658,435393,428022,436007,436533,121545,131057 })
            require(sSpellMgr->GetSpellInfo(spell, DIFFICULTY_NONE) != nullptr, "spell", spell);
        for (uint32 item : { 210333,213631,213571,215110,215438,215442,210982 })
            require(sObjectMgr->GetItemTemplate(item) != nullptr, "item", item);
        for (uint32 loot : { 217564,217557,217190 })
            require(LootTemplates_Creature.HaveLootFor(loot), "creature loot", loot);
        for (uint32 tree : { 156743,154600 })
            require(sCriteriaTreeStore.LookupEntry(tree) != nullptr, "criteria tree", tree);
        require(sItemExtendedCostStore.LookupEntry(8512) != nullptr, "item extended cost", 8512);
        require(sCurrencyTypesStore.LookupEntry(2778) != nullptr, "currency", 2778);

        for (ObjectGuid::LowType guid = 11801064; guid <= 11801079; ++guid)
        {
            CreatureData const* spawn = sObjectMgr->GetCreatureData(guid);
            require(spawn && spawn->mapId == Timerunning::Pandaria::MapId &&
                spawn->spawnGroupData->groupId == Timerunning::Pandaria::SpawnGroupId && spawn->spawnGroupData->timerunningSeasonMask == 2,
                "event-only opening spawn", uint32(guid));
        }
        require(HasValidatedSpellScript(435393, "spell_timerunning_pandaria_chronostabilizer"), "spell_timerunning_pandaria_chronostabilizer binding for", 435393);
        require(HasValidatedSpellScript(435393, "aura_timerunning_pandaria_chronostabilizer"), "aura_timerunning_pandaria_chronostabilizer binding for", 435393);
        require(HasValidatedSpellScript(428022, "spell_timerunning_pandaria_thread"), "spell_timerunning_pandaria_thread binding for", 428022);
        require(HasValidatedSpellScript(440393, "aura_timerunning_pandaria_advantage"), "aura_timerunning_pandaria_advantage binding for", 440393);

        sTimerunningMgr->SetPandariaContentReady(ready);
        if (ready)
            TC_LOG_INFO("server.loading", "Timerunning: Pandaria introduction content is ready. Admission follows the configured schedule. Dungeon entry and conversion remain unavailable.");
        else
            TC_LOG_ERROR("server.loading", "Timerunning: Pandaria introduction is unavailable. Check the Timerunning world updates, starter kits, opening spawns, quests and spell-script validation errors. Creation remains disabled.");
    }
};

void AddSC_timerunning_pandaria()
{
    RegisterCreatureAI(npc_timerunning_pandaria_guide);
    RegisterSpellScript(spell_timerunning_pandaria_chronostabilizer);
    RegisterSpellScript(aura_timerunning_pandaria_chronostabilizer);
    RegisterSpellScript(aura_timerunning_pandaria_advantage);
    RegisterSpellScript(spell_timerunning_pandaria_thread);
    new player_timerunning_pandaria();
    new world_timerunning_pandaria();
}

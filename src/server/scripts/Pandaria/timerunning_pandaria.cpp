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

#include "ConditionMgr.h"
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
constexpr uint32 QuestSeekingExpertAdvice = 79433;
constexpr uint32 QuestWeaveItToMe = 79434;
constexpr uint32 QuestWhatsHoursIsYours = 79435;
constexpr uint32 QuestKnotMyProblem = 79437;
constexpr uint32 QuestRecallingTheWar = 79440;
constexpr uint32 ObjectiveForgeCloak = 446826;
constexpr uint32 NpcEternus = 216591;
constexpr uint32 NpcMoratari = 216594;
constexpr uint32 NpcMomentus = 217668;
constexpr uint32 NpcTimeRift = 217782;
constexpr uint32 ItemChronobadge = 215438;
constexpr uint32 ItemQuestCloak = 215442;
constexpr uint32 ItemCloak = 210333;
constexpr uint32 ItemChronostabilizer = 215110;
constexpr uint32 SpellAdvantage = 440393;
constexpr uint32 GameObjectUnstableRift = 423343;
constexpr ObjectGuid::LowType UnstableRiftSpawnId = 11801080;
constexpr uint32 PlayerConditionUnstableRift = 119615;
constexpr uint32 DisplayTimeRift = 118479;
constexpr uint32 GossipMenuMomentus = 90104;
constexpr uint32 GossipOptionForgeCloak = 0;
constexpr float ArrivalRemarkDistance = 20.0f;

enum EternusTexts
{
    SAY_ETERNUS_ARRIVAL                 = 0,
    SAY_ETERNUS_ITS_ABOUT_TIME          = 1,
    SAY_ETERNUS_SEEKING_EXPERT_ADVICE   = 2
};

enum MoratariTexts
{
    SAY_MORATARI_CLEAN_UP               = 0
};

enum ArchaiosTexts
{
    SAY_ARCHAIOS_ENGAGE                 = 0,
    SAY_ARCHAIOS_DEATH                  = 1
};

// Eternus' campaign options are added by the script, after her database menu.
enum EternusGossipActions
{
    ACTION_RECENT_EVENTS                = 1001,
    ACTION_SHOW_PAST                    = 1002,
    ACTION_BEGIN_CAMPAIGN               = 1003
};

bool IsTimerunner(Player const* player)
{
    // A player who is still being created or loaded may not have a map yet.
    Map const* map = player ? player->FindMap() : nullptr;
    return map && player->GetTimerunningSeasonId() == int32(Timerunning::Season::Pandaria) &&
        map->GetId() == Timerunning::Pandaria::MapId &&
        map->GetTimerunningSeasonId() == int32(Timerunning::Season::Pandaria);
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

// Placements and the lines without a Blizzard text record are reconstructed. Objectives and rewards use the existing quests.
struct npc_timerunning_pandaria_guide : ScriptedAI
{
    using ScriptedAI::ScriptedAI;

    void MoveInLineOfSight(Unit* who) override
    {
        ScriptedAI::MoveInLineOfSight(who);

        if (me->GetEntry() != NpcEternus)
            return;

        // Eternus remarks once on a Timerunner who arrives before taking her first quest.
        Player* player = who->ToPlayer();
        if (!IsTimerunner(player) || player->IsLoading() || !me->IsWithinDistInMap(player, ArrivalRemarkDistance) ||
            player->GetQuestStatus(QuestItsAboutTime) != QUEST_STATUS_NONE || !_remarkedArrivals.insert(player->GetGUID()).second)
            return;

        Talk(SAY_ETERNUS_ARRIVAL, player);
    }

    bool OnGossipHello(Player* player) override
    {
        if (!IsTimerunner(player))
        {
            CloseGossipMenuFor(player);
            return true;
        }

        if (me->GetEntry() != NpcEternus)
            return false;

        player->PrepareGossipMenu(me, player->GetGossipMenuForSource(me), true);
        if (player->GetQuestStatus(QuestRecallingTheWar) == QUEST_STATUS_INCOMPLETE)
        {
            AddGossipItemFor(player, GossipOptionNpc::None, "Tell me about the war in Pandaria.", GOSSIP_SENDER_MAIN, ACTION_RECENT_EVENTS);
            if (player->GetQuestObjectiveData(QuestRecallingTheWar, 446403))
                AddGossipItemFor(player, GossipOptionNpc::None, "I am ready to see how it began.", GOSSIP_SENDER_MAIN, ACTION_SHOW_PAST);
        }
        if (player->GetQuestRewardStatus(QuestRecallingTheWar))
            AddGossipItemFor(player, GossipOptionNpc::None, "Take me to the beginning of the campaign.", GOSSIP_SENDER_MAIN, ACTION_BEGIN_CAMPAIGN);
        player->SendPreparedGossip(me);
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
    {
        if (!IsTimerunner(player))
        {
            CloseGossipMenuFor(player);
            return true;
        }

        switch (me->GetEntry())
        {
            case NpcMomentus:
                if (menuId != GossipMenuMomentus || gossipListId != GossipOptionForgeCloak)
                    return false;
                CloseGossipMenuFor(player);
                ForgeCloak(player);
                return true;
            case NpcEternus:
                break;
            default:
                // Vendor options are handled by the core.
                return false;
        }

        uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        ClearGossipMenuFor(player);
        CloseGossipMenuFor(player);
        switch (action)
        {
            case ACTION_RECENT_EVENTS:
                if (player->GetQuestStatus(QuestRecallingTheWar) == QUEST_STATUS_INCOMPLETE)
                {
                    me->Whisper("The war brought the Alliance and Horde to Pandaria. We will follow those events through the timeways, and learn from what happened.", LANG_UNIVERSAL, player);
                    player->KilledMonsterCredit(217115);
                }
                break;
            case ACTION_SHOW_PAST:
                if (player->GetQuestStatus(QuestRecallingTheWar) == QUEST_STATUS_INCOMPLETE &&
                    player->GetQuestObjectiveData(QuestRecallingTheWar, 446403))
                {
                    player->KilledMonsterCredit(217538);
                    me->Whisper("Return to me when your preparations are complete.", LANG_UNIVERSAL, player);
                }
                break;
            case ACTION_BEGIN_CAMPAIGN:
                if (player->GetQuestRewardStatus(QuestRecallingTheWar))
                    BeginCampaign(player);
                break;
            default:
                break;
        }
        return true;
    }

    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        if (!IsTimerunner(player))
            return;

        switch (me->GetEntry())
        {
            case NpcEternus:
                if (quest->GetQuestId() == QuestItsAboutTime)
                    Talk(SAY_ETERNUS_ITS_ABOUT_TIME, player);
                else if (quest->GetQuestId() == QuestSeekingExpertAdvice)
                    Talk(SAY_ETERNUS_SEEKING_EXPERT_ADVICE, player);
                break;
            case NpcMoratari:
            {
                // Weave It To Me and Knot My Problem are offered together. She speaks when the first of them is taken.
                uint32 otherQuest = 0;
                if (quest->GetQuestId() == QuestWeaveItToMe)
                    otherQuest = QuestKnotMyProblem;
                else if (quest->GetQuestId() == QuestKnotMyProblem)
                    otherQuest = QuestWeaveItToMe;
                if (otherQuest && player->GetQuestStatus(otherQuest) == QUEST_STATUS_NONE)
                    Talk(SAY_MORATARI_CLEAN_UP, player);
                break;
            }
            default:
                break;
        }
    }

    void OnQuestReward(Player* player, Quest const* quest, LootItemType /*type*/, uint32 /*option*/) override
    {
        if (quest->GetQuestId() == QuestRecallingTheWar && me->GetEntry() == NpcEternus && IsTimerunner(player))
            BeginCampaign(player);
    }

private:
    void ForgeCloak(Player* player)
    {
        if (player->GetQuestStatus(QuestWhatsHoursIsYours) != QUEST_STATUS_INCOMPLETE || !player->HasItemCount(ItemChronobadge, 1) ||
            !player->IsQuestObjectiveCompletable(QuestWhatsHoursIsYours, ObjectiveForgeCloak) ||
            player->IsQuestObjectiveComplete(QuestWhatsHoursIsYours, ObjectiveForgeCloak))
            return;

        // The forged cloak belongs to the objective and never goes into bags. The badge is
        // consumed only once the objective has counted it.
        player->ItemAddedQuestCheck(ItemQuestCloak, 1, true);
        if (player->IsQuestObjectiveComplete(QuestWhatsHoursIsYours, ObjectiveForgeCloak))
            player->DestroyItemCount(ItemChronobadge, 1, true);
    }

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

    GuidUnorderedSet _remarkedArrivals;
};

// Archaios stands beside the rift for now. Being summoned by the rift is a later step.
struct npc_timerunning_pandaria_archaios : ScriptedAI
{
    using ScriptedAI::ScriptedAI;

    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_ARCHAIOS_ENGAGE, who);
    }

    void JustDied(Unit* killer) override
    {
        Talk(SAY_ARCHAIOS_DEATH, killer);
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
        // The aura target is not available while amounts are calculated; the owner is.
        Player const* player = GetUnitOwner() ? GetUnitOwner()->ToPlayer() : nullptr;
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
        for (uint32 spell : { 431760,440393,435393,428022,436007,436533,439809,384798,121545,131057 })
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

        // Without the time rift model the side quest's rifts cannot be created.
        require(sObjectMgr->GetCreatureModelInfo(DisplayTimeRift) != nullptr, "creature model info for time rift display", DisplayTimeRift);

        // The optional objective is credited by investigating the rift object. Its interaction
        // condition is not in the client data and must come from the conditions table.
        require(sObjectMgr->GetGameObjectTemplate(GameObjectUnstableRift) != nullptr, "gameobject", GameObjectUnstableRift);
        GameObjectData const* rift = sObjectMgr->GetGameObjectData(UnstableRiftSpawnId);
        require(rift && rift->id == GameObjectUnstableRift && rift->mapId == Timerunning::Pandaria::MapId &&
            rift->spawnGroupData->groupId == Timerunning::Pandaria::SpawnGroupId && rift->spawnGroupData->timerunningSeasonMask == 2,
            "event-only opening object spawn", uint32(UnstableRiftSpawnId));
        require(sConditionMgr->HasConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_PLAYER_CONDITION, PlayerConditionUnstableRift),
            "database conditions for player condition", PlayerConditionUnstableRift);

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
    RegisterCreatureAI(npc_timerunning_pandaria_archaios);
    RegisterSpellScript(spell_timerunning_pandaria_chronostabilizer);
    RegisterSpellScript(aura_timerunning_pandaria_chronostabilizer);
    RegisterSpellScript(aura_timerunning_pandaria_advantage);
    RegisterSpellScript(spell_timerunning_pandaria_thread);
    new world_timerunning_pandaria();
}

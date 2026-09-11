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

#include "WorldSession.h"
#include "ChromieTimePackets.h"
#include "Creature.h"
#include "CreatureTextMgr.h"
#include "DB2Stores.h"
#include "GossipDef.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "Unit.h"

namespace
{
// Female Chromie BroadcastText ids from hotfix rows (Text1). Shadowlands (Ui 14) has no id in that set.
uint32 GetChromieTimeSelectBroadcastTextId(uint32 uiExpansionId)
{
    switch (uiExpansionId)
    {
        case 5:  return 195758; // Cataclysm
        case 6:  return 195193; // Burning Crusade
        case 7:  return 194977; // Wrath of the Lich King
        case 8:  return 195172; // Mists of Pandaria
        case 9:  return 195151; // Warlords of Draenor
        case 10: return 195757; // Legion
        case 15: return 266403; // Battle for Azeroth
        case 16: return 266404; // Dragonflight
        default: return 0;
    }
}

void SpeakChromieTimeSelectLine(Creature* chromie, Player* player, uint32 uiExpansionId)
{
    uint32 const textId = GetChromieTimeSelectBroadcastTextId(uiExpansionId);
    if (!textId)
        return;

    chromie->Talk(textId, CHAT_MSG_MONSTER_SAY, CreatureTextMgr::GetRangeForChatType(CHAT_MSG_MONSTER_SAY), player);
}

void PushChromieTimeBreadcrumbQuest(Player* player, uint32 uiExpansionId)
{
    ChromieTimeExpansionQuest const* mapping = sObjectMgr->GetChromieTimeExpansionQuest(uiExpansionId);
    if (!mapping)
        return;

    uint32 questId = player->GetTeamId() == TEAM_ALLIANCE ? mapping->AllianceQuestId : mapping->HordeQuestId;
    if (!questId)
        return;

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    // Skip if rewarded, already in the log, or the race cannot take it (same checks as SPELL_EFFECT_QUEST_START)
    if (!player->CanTakeQuest(quest, false))
        return;

    if (quest->IsAutoAccept() && player->CanAddQuest(quest, false))
    {
        player->AddQuestAndCheckCompletion(quest, player);
        player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, player->GetGUID(), true, true);
    }
    else
        player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, player->GetGUID(), true, false);
}
}

void WorldSession::HandleChromieTimeSelectExpansion(WorldPackets::ChromieTime::ChromieTimeSelectExpansion& selectExpansion)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    Creature* chromie = player->GetNPCIfCanInteractWith(selectExpansion.GUID, UNIT_NPC_FLAG_GOSSIP, UNIT_NPC_FLAG_2_NONE);
    if (!chromie)
        return;

    if (!player->PlayerTalkClass->GetInteractionData().IsInteractingWith(selectExpansion.GUID, PlayerInteractionType::ChromieTime))
        return;

    UIChromieTimeExpansionInfoEntry const* expansionInfo = sUIChromieTimeExpansionInfoStore.LookupEntry(selectExpansion.Expansion);
    if (!expansionInfo || !expansionInfo->SpellID)
        return;

    // Start and re-enter from the present lock at 68; already in a campaign may change until the end level.
    if (!player->CanSelectChromieTimeExpansion())
        return;

    // CompletedPlayerConditionID means already done / no longer offer (0 on current Ui rows).
    // Do not use ShowPlayerConditionID here: those ModifierTrees are PlayerIsInChromieTime
    // for that Ui (type 300) — a client list filter, not "may select while in the present."
    // MeetPlayerCondition on ShowPlayerConditionID blocked every first-time select (silent no-op).
    if (expansionInfo->CompletedPlayerConditionID && player->MeetPlayerCondition(uint32(expansionInfo->CompletedPlayerConditionID)))
        return;

    // Player self-casts the expansion SpellID (effect 277 → SetChromieTimeExpansion,
    // which sends SMSG_SET_CTR_OPTIONS and the update field). Leave and kick reuse SetChromieTimeExpansion.
    player->CastSpell(player, uint32(expansionInfo->SpellID), true);

    WorldPackets::ChromieTime::ChromieTimeSelectExpansionSuccess success;
    SendPacket(success.Write());

    SpeakChromieTimeSelectLine(chromie, player, selectExpansion.Expansion);

    // Auto-launched QUEST_GIVER_QUEST_DETAILS after select (separate from spell 325400)
    PushChromieTimeBreadcrumbQuest(player, selectExpansion.Expansion);
}

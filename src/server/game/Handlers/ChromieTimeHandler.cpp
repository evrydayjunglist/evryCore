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
#include "DB2Stores.h"
#include "GossipDef.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "Unit.h"

namespace
{
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

    // Skip rewarded / in-log / race-gated (same gate as SPELL_EFFECT_QUEST_START)
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

    UIChromieTimeExpansionInfoEntry const* expansionInfo = sUIChromieTimeExpansionInfoStore.LookupEntry(selectExpansion.Expansion);
    if (!expansionInfo || !expansionInfo->SpellID)
        return;

    UF::CTROptions const& current = *player->m_playerData->CtrOptions;
    UF::CTROptions next = player->BuildCtrOptionsForChromieTime(selectExpansion.Expansion);

    WorldPackets::ChromieTime::SetCtrOptions setCtrOptions;
    setCtrOptions.From.ConditionalFlags = current.ConditionalFlags;
    setCtrOptions.From.FactionGroup = int8(current.FactionGroup);
    setCtrOptions.From.ChromieTimeExpansionMask = current.ChromieTimeExpansionMask;
    setCtrOptions.To.ConditionalFlags = next.ConditionalFlags;
    setCtrOptions.To.FactionGroup = int8(next.FactionGroup);
    setCtrOptions.To.ChromieTimeExpansionMask = next.ChromieTimeExpansionMask;
    SendPacket(setCtrOptions.Write());

    // CT-A: player self-casts expansion SpellID (effect 277 sets UF) — no quest-start effect
    player->CastSpell(player, uint32(expansionInfo->SpellID), true);

    WorldPackets::ChromieTime::ChromieTimeSelectExpansionSuccess success;
    SendPacket(success.Write());

    // CT-A: AutoLaunched QUEST_GIVER_QUEST_DETAILS after select fan-out (separate from spell 325400)
    PushChromieTimeBreadcrumbQuest(player, selectExpansion.Expansion);
}

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
#include "AdventureJournalPackets.h"
#include "DB2Stores.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"

void WorldSession::HandleAdventureJournalOpenQuest(WorldPackets::AdventureJournal::AdventureJournalOpenQuest& openQuest)
{
    if (ChrClassUIDisplayEntry const* uiDisplay = sDB2Manager.GetUiDisplayForClass(Classes(_player->GetClass())))
        if (!_player->MeetPlayerCondition(uiDisplay->AdvGuidePlayerConditionID))
            return;

    AdventureJournalEntry const* adventureJournal = sAdventureJournalStore.LookupEntry(openQuest.AdventureJournalID);
    if (!adventureJournal)
        return;

    if (!_player->MeetPlayerCondition(adventureJournal->PlayerConditionID))
        return;

    Quest const* quest = sObjectMgr->GetQuestTemplate(adventureJournal->QuestID);
    if (!quest)
        return;

    if (_player->CanTakeQuest(quest, true))
        _player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, _player->GetGUID(), true, false);
}

void WorldSession::HandleAdventureJournalUpdateSuggestions(WorldPackets::AdventureJournal::AdventureJournalUpdateSuggestions& updateSuggestions)
{
    if (ChrClassUIDisplayEntry const* uiDisplay = sDB2Manager.GetUiDisplayForClass(Classes(_player->GetClass())))
        if (!_player->MeetPlayerCondition(uiDisplay->AdvGuidePlayerConditionID))
            return;

    WorldPackets::AdventureJournal::AdventureJournalDataResponse response;
    response.OnLevelUp = updateSuggestions.OnLevelUp;

    for (AdventureJournalEntry const* adventureJournal : sAdventureJournalStore)
    {
        if (_player->MeetPlayerCondition(adventureJournal->PlayerConditionID))
        {
            WorldPackets::AdventureJournal::AdventureJournalEntry adventureJournalData;
            adventureJournalData.AdventureJournalID = int32(adventureJournal->ID);
            adventureJournalData.Priority = int32(adventureJournal->PriorityMax);
            response.Entries.push_back(adventureJournalData);
        }
    }

    SendPacket(response.Write());
}

void WorldSession::HandleEncounterJournalStartArathiRpe(WorldPackets::AdventureJournal::EncounterJournalStartArathiRpe& /*packet*/)
{
    Player* player = GetPlayer();
    if (!player)
        return;

    constexpr uint32 ARATHI_RPE_MAP_ID = 2927;
    constexpr uint32 ARATHI_RPE_LAUNCH_SPELL = 1260320;
    constexpr uint8 ARATHI_RPE_JOURNAL_MIN_LEVEL = 20;

    if (player->GetMapId() == ARATHI_RPE_MAP_ID)
        return;

    // Adventure Guide Catch Up is available without the inactivity window. Character-select login still uses that window.
    if (player->GetLevel() < ARATHI_RPE_JOURNAL_MIN_LEVEL)
        return;

    if (!sSpellMgr->GetSpellInfo(ARATHI_RPE_LAUNCH_SPELL, DIFFICULTY_NONE))
    {
        TC_LOG_ERROR("network", "Player {} requested Arathi Catch Up from the journal but spell {} is missing",
            player->GetGUID().ToString(), ARATHI_RPE_LAUNCH_SPELL);
        return;
    }

    player->CastSpell(player, ARATHI_RPE_LAUNCH_SPELL);
}

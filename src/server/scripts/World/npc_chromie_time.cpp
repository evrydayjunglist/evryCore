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

#include "CreatureAIImpl.h"
#include "GossipDef.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"

#include <vector>

namespace
{
enum ChromieTimeGossip
{
    GOSSIP_MENU_CHROMIE              = 25426,
    // npc_text → BroadcastText 269086 (Radiant Song / Isle of Dorn). ct-start sniff 12.0.7.68887.
    NPC_TEXT_CHROMIE_DORN_REFUSE     = 40348,
    // npc_text → BroadcastText 206524 ("little more experience"). ct-lvl3 sniff 12.0.7.68887.
    NPC_TEXT_CHROMIE_LOW_LEVEL       = 40349,
};

// Present + select-lock (68+) or past end band: Dorn funnel, no FAQ / no select (ct-start).
bool IsChromieTimeDornRefuse(Player const* player)
{
    if (player->m_activePlayerData->UiChromieTimeExpansionID != 0)
        return false;

    uint32 const level = player->GetLevel();
    return level >= Player::GetChromieTimeSelectLockLevel()
        || level >= Player::GetChromieTimeEndLevel();
}

void StripChromieTimeSelectOptions(GossipMenu& menu)
{
    GossipMenuItemContainer keep;
    keep.reserve(menu.GetMenuItemCount());
    for (GossipMenuItem const& item : menu.GetMenuItems())
        if (item.OptionNpc != GossipOptionNpc::ChromieTimeNpc)
            keep.push_back(item);

    menu.ClearMenu();
    for (GossipMenuItem const& item : keep)
        menu.AddMenuItem(item.GossipOptionID, int32(item.OrderIndex), item.OptionNpc, item.OptionText,
            item.Language, item.Flags, item.GossipNpcOptionID, item.ActionMenuID, item.ActionPoiID,
            item.BoxCoded, item.BoxMoney, item.BoxText, item.SpellID, item.OverrideIconID,
            item.Sender, item.Action);
}
}

struct npc_chromie_time : public ScriptedAI
{
    npc_chromie_time(Creature* creature) : ScriptedAI(creature) { }

    bool OnGossipHello(Player* player) override
    {
        // Hard refuse (ct-start): present + ineligible for Chromie — Dorn text, 0 options.
        // FAQ is available on every other path (ct-lvl3 + owner model).
        if (IsChromieTimeDornRefuse(player))
        {
            ClearGossipMenuFor(player);
            player->PlayerTalkClass->GetGossipMenu().SetMenuId(GOSSIP_MENU_CHROMIE);
            SendGossipMenuFor(player, NPC_TEXT_CHROMIE_DORN_REFUSE, me);
            return true;
        }

        player->PrepareGossipMenu(me, GOSSIP_MENU_CHROMIE, true);

        // Soft refuse (ct-lvl3): present + below start / no ER — FAQ stays, ChromieTimeNpc hidden,
        // BT 206524. FAQ is also present when CanSelect (normal menu text 40347).
        bool const inPresent = player->m_activePlayerData->UiChromieTimeExpansionID == 0;
        if (!player->CanSelectChromieTimeExpansion())
        {
            StripChromieTimeSelectOptions(player->PlayerTalkClass->GetGossipMenu());
            if (inPresent)
            {
                SendGossipMenuFor(player, NPC_TEXT_CHROMIE_LOW_LEVEL, me);
                return true;
            }
        }

        player->SendPreparedGossip(me);
        return true;
    }
};

void AddSC_npc_chromie_time()
{
    RegisterCreatureAI(npc_chromie_time);
}

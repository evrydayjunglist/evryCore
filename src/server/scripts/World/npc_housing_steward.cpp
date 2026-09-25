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

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GossipDef.h"
#include "Log.h"
#include "Player.h"
#include "ScriptedGossip.h"

enum HousingTutorialData
{
    // Quest "My First Home" (91863): kill credit for its objective "Greet the steward"
    NPC_KILL_CREDIT_GREET_STEWARD   = 249851,

    // Tocho's menu 40498 and its option "(Quest) Let's go!" (hbcd3 715686)
    GOSSIP_MENU_TOCHO_LETS_GO       = 40498,
    GOSSIP_OPTION_LETS_GO           = 135740,

    // "[DNT] Tutorial Guardian Accepted": kill credit 248857 ("Ask the steward to join you"), 233708 and 233063
    SPELL_TUTORIAL_GUARDIAN_ACCEPTED = 1250436,
    // "[DNT] Tocho Guardian": summons the player's own Tocho, 249848, with SummonProperties 6478
    SPELL_TOCHO_GUARDIAN            = 1250470,
};

// Lyssabel Dawnpetal (233063) and Tocho (233708), the stewards of the housing tutorial.
struct npc_housing_steward : public CreatureAI
{
    npc_housing_steward(Creature* creature) : CreatureAI(creature) { }

    void UpdateAI(uint32 /*diff*/) override { }

    bool OnGossipHello(Player* player) override
    {
        // Retail gave the "Greet the steward" credit through a spell: after the player talked to the steward she cast
        // 1250475 on herself, and the credit followed that cast (hbcd3 688462-688657). What that spell's effect does
        // in this client has not been checked, so the credit is given directly until it is. Talking also counts for
        // the steward's talk-to objective of "My First Home". The menu itself comes from the database.
        player->KilledMonsterCredit(NPC_KILL_CREDIT_GREET_STEWARD);
        player->TalkedToCreature(me->GetEntry(), me->GetGUID());
        return false;
    }

    bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
    {
        if (menuId != GOSSIP_MENU_TOCHO_LETS_GO)
            return false;

        GossipMenuItem const* item = player->PlayerTalkClass->GetGossipMenu().GetItemByIndex(gossipListId);
        if (!item || item->GossipOptionID != GOSSIP_OPTION_LETS_GO)
            return false;

        // Retail: after "(Quest) Let's go!" the player casts 1250436, then 1248279, 1248280, 1282579 and 1266699,
        // then 1250470 (hbcd3 715686-716335). What the four middle spells do for the tutorial is not known, so only
        // the credit and the summon are cast.
        CloseGossipMenuFor(player);
        player->CastSpell(player, SPELL_TUTORIAL_GUARDIAN_ACCEPTED, true);
        player->CastSpell(player, SPELL_TOCHO_GUARDIAN, true);

        TC_LOG_DEBUG("housing", "npc_housing_steward: {} took {} along (spells {} and {})",
            player->GetGUID().ToString(), me->GetEntry(), SPELL_TUTORIAL_GUARDIAN_ACCEPTED, SPELL_TOCHO_GUARDIAN);
        return true;
    }
};

void AddSC_npc_housing_steward()
{
    RegisterCreatureAI(npc_housing_steward);
}

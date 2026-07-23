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
#include "Player.h"
#include "Unit.h"

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

    // CT-A: player self-casts expansion SpellID (effect 277 sets UF)
    player->CastSpell(player, uint32(expansionInfo->SpellID), true);

    WorldPackets::ChromieTime::ChromieTimeSelectExpansionSuccess success;
    SendPacket(success.Write());
}

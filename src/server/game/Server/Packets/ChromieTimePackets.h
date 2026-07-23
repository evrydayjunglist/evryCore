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

#ifndef TRINITYCORE_CHROMIE_TIME_PACKETS_H
#define TRINITYCORE_CHROMIE_TIME_PACKETS_H

#include "ObjectGuid.h"
#include "Packet.h"
#include "PartyPackets.h"

namespace WorldPackets
{
    namespace ChromieTime
    {
        class ChromieTimeSelectExpansion final : public ClientPacket
        {
        public:
            explicit ChromieTimeSelectExpansion(WorldPacket&& packet) : ClientPacket(CMSG_CHROMIE_TIME_SELECT_EXPANSION, std::move(packet)) { }

            void Read() override;

            ObjectGuid GUID;
            uint32 Expansion = 0;
        };

        class ChromieTimeSelectExpansionSuccess final : public ServerPacket
        {
        public:
            explicit ChromieTimeSelectExpansionSuccess() : ServerPacket(SMSG_CHROMIE_TIME_SELECT_EXPANSION_SUCCESS, 0) { }

            WorldPacket const* Write() override;
        };

        // CT-A: two Party::CTROptions blocks (from → to), Length 26 for typical 1-flag transition
        class SetCtrOptions final : public ServerPacket
        {
        public:
            explicit SetCtrOptions() : ServerPacket(SMSG_SET_CTR_OPTIONS, 26) { }

            WorldPacket const* Write() override;

            Party::CTROptions From;
            Party::CTROptions To;
        };
    }
}

#endif // TRINITYCORE_CHROMIE_TIME_PACKETS_H

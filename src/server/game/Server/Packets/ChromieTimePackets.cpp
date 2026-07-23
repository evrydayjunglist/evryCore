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

#include "ChromieTimePackets.h"
#include "PacketOperators.h"

namespace WorldPackets::ChromieTime
{
void ChromieTimeSelectExpansion::Read()
{
    _worldPacket >> GUID;
    _worldPacket >> Expansion;
}

WorldPacket const* ChromieTimeSelectExpansionSuccess::Write()
{
    return &_worldPacket;
}

WorldPacket const* SetCtrOptions::Write()
{
    auto writeCtrOptions = [this](Party::CTROptions const& ctrOptions)
    {
        _worldPacket << Size<uint32>(ctrOptions.ConditionalFlags);
        _worldPacket << int8(ctrOptions.FactionGroup);
        _worldPacket << uint32(ctrOptions.ChromieTimeExpansionMask);
        if (!ctrOptions.ConditionalFlags.empty())
            _worldPacket.append(ctrOptions.ConditionalFlags.data(), ctrOptions.ConditionalFlags.size());
    };

    writeCtrOptions(From);
    writeCtrOptions(To);
    return &_worldPacket;
}
}

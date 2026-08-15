/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EVRY_MOD_PLAYERBOTS_H
#define EVRY_MOD_PLAYERBOTS_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

inline constexpr char const* PLAYERBOTS_ENABLE = "Playerbots.Enable";
inline constexpr char const* PLAYERBOTS_COUNT = "Playerbots.Count";
inline constexpr char const* PLAYERBOTS_LOG = "module.playerbots";

struct PlayerbotAccount
{
    uint32 Index = 0;
    uint32 AccountId = 0;
    uint32 BattlenetAccountId = 0;
    uint8 Expansion = 0;
    std::string AccountName;
    std::string BattlenetEmail;
    ObjectGuid CharacterGuid;
};

#endif

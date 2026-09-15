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

#ifndef TRINITY_TIMERUNNING_PANDARIA_H
#define TRINITY_TIMERUNNING_PANDARIA_H

#include "Define.h"
#include "Position.h"
#include <array>

struct CharacterLoadoutEntry;

namespace Timerunning::Pandaria
{
inline constexpr uint32 MapId = 870;
inline constexpr uint8 StartLevel = 10;
inline constexpr uint8 MaxLevel = 70;
inline constexpr uint32 SpawnGroupId = 1286;
inline constexpr WorldLocation StartLocation{ MapId, -930.0f, -4730.0f, 1.81421f, 1.570796f };
inline constexpr uint32 StartAreaId = 6832;

// These installed CharacterLoadout rows contain the class-specific Pandaria Timerunner kits.
inline constexpr std::array<uint32, 14> StartingLoadouts = { 0, 1868, 1873, 1877, 1871, 1872, 1880, 1870, 1875, 1869, 1874, 1878, 1879, 1876 };

TC_GAME_API CharacterLoadoutEntry const* GetStartingLoadout(uint8 playerClass);
TC_GAME_API bool ValidateStartingLoadouts();
}

#endif

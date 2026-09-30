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

#ifndef EVRY_MOD_PLAYERBOT_FACTORY_H
#define EVRY_MOD_PLAYERBOT_FACTORY_H

#include "PlayerbotCreateFilter.h"
#include "Playerbots.h"
#include <memory>
#include <optional>

class WorldSession;

// How many bot characters of each faction exist, counted from the database the first time a new bot could be either
// faction, then kept up as bots are made. Reset at each start.
struct PlayerbotRosterFactions
{
    bool Counted = false;
    uint32 Horde = 0;
    uint32 Alliance = 0;
};

namespace PlayerbotFactory
{
    std::string MakeBattlenetEmail(uint32 index);
    std::unique_ptr<WorldSession> MakeSession(PlayerbotAccount const& account);
    bool EnsureAccount(PlayerbotAccount& account);
    bool EnsureCharacter(PlayerbotAccount& account, PlayerbotRosterFactions& roster);
    // Says once what the creation switches will do (a switch not yet supported, both factions off, a split out of range).
    void CheckCreateSettings();
    // The faction a neutral bot chooses at the end of her start, by the switches and Playerbots.HordePercent as they are
    // now, given the Horde and Alliance bots in the world. None when neither faction is open.
    std::optional<PlayerbotFaction> PickNeutralStartFaction(uint32 hordeBots, uint32 allianceBots);
}

#endif

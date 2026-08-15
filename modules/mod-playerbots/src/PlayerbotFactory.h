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

#include "Playerbots.h"
#include <memory>

class WorldSession;

namespace PlayerbotFactory
{
    std::string MakeBattlenetEmail(uint32 index);
    std::unique_ptr<WorldSession> MakeSession(PlayerbotAccount const& account);
    bool EnsureAccount(PlayerbotAccount& account);
    bool EnsureCharacter(PlayerbotAccount& account);
    // Delete every character on PLAYERBOTn@PLAYERBOTS.LOCAL. Leaves the login accounts.
    uint32 DeleteAllBotCharacters();
}

#endif

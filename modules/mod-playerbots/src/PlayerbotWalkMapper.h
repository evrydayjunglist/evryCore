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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EVRY_MOD_PLAYERBOT_WALK_MAPPER_H
#define EVRY_MOD_PLAYERBOT_WALK_MAPPER_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <memory>
#include <string>

class Player;

inline constexpr float PLAYERBOT_WALK_MAP_DEFAULT_YARDS = 60.0f;
inline constexpr float PLAYERBOT_WALK_MAP_MIN_YARDS = 10.0f;
inline constexpr float PLAYERBOT_WALK_MAP_MAX_YARDS = 150.0f;

// Makes one walk map at a time: the ground a player can walk from her feet by the rules a bot's walk uses, written as a
// page next to the server logs. It only reads the world. The work runs a few milliseconds per world tick, after the maps
// have updated, so a large map does not stall the server.
class PlayerbotWalkMapper
{
public:
    PlayerbotWalkMapper();
    ~PlayerbotWalkMapper();

    // False, with the reason in message, when a map is already being made or she cannot be mapped where she is.
    bool Start(Player* subject, Position const& feet, Position const* walkDestination, float radius, ObjectGuid requester,
        std::string& message);
    void Update(uint32 diff);

private:
    struct Job;

    void Finish(Job& job);
    static void Tell(Job const& job, std::string const& text);

    std::unique_ptr<Job> _job;
};

#endif

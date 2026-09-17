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

#ifndef EVRY_MOD_PLAYERBOT_WALK_MAP_SERVER_WORLD_H
#define EVRY_MOD_PLAYERBOT_WALK_MAP_SERVER_WORLD_H

#include "GridDefines.h"
#include "Map.h"
#include "MapDefines.h"
#include "PlayerbotMovement.h"
#include "PlayerbotWalkMap.h"
#include "Player.h"
#include "Position.h"
#include <cmath>

// Answers a walk map with this player's own walk: the same step a heartbeat takes, and the same plant. It never asks
// about ground that is not loaded, because asking for a height there would load it.
class PlayerbotWalkMapServerWorld final : public PlayerbotWalkMapWorld
{
public:
    PlayerbotWalkMapServerWorld(Player* player, Map* map) : _player(player), _map(map) { }

    bool IsLoaded(float x, float y) override
    {
        return Trinity::IsValidMapCoord(x, y) && _map->IsGridLoaded(x, y);
    }

    PlayerbotWalkMapStepResult Step(float fromX, float fromY, float fromZ, float toX, float toY) override
    {
        float const orientation = Position::NormalizeOrientation(std::atan2(toY - fromY, toX - fromX));
        Position from;
        from.Relocate(fromX, fromY, fromZ, orientation);
        Position planted;
        PlayerbotWalker::GroundedStepFailure const failure = PlayerbotWalker::ClassifyGroundedStep(_player, from, toX, toY,
            orientation, planted);

        PlayerbotWalkMapStepResult result;
        result.Z = planted.GetPositionZ();
        switch (failure)
        {
            case PlayerbotWalker::GroundedStepFailure::None:
                result.Step = PlayerbotWalkMapStep::Legal;
                break;
            case PlayerbotWalker::GroundedStepFailure::NoFloor:
                result.Step = PlayerbotWalkMapStep::NoFloor;
                break;
            case PlayerbotWalker::GroundedStepFailure::SteepUp:
                result.Step = PlayerbotWalkMapStep::SteepUp;
                break;
            case PlayerbotWalker::GroundedStepFailure::TooFarDown:
                result.Step = PlayerbotWalkMapStep::TooFarDown;
                break;
            case PlayerbotWalker::GroundedStepFailure::StaticCollision:
                result.Step = PlayerbotWalkMapStep::StaticCollision;
                break;
            case PlayerbotWalker::GroundedStepFailure::DynamicCollision:
                result.Step = PlayerbotWalkMapStep::DynamicCollision;
                break;
            case PlayerbotWalker::GroundedStepFailure::NoPath:
            case PlayerbotWalker::GroundedStepFailure::InvalidPosition:
                result.Step = PlayerbotWalkMapStep::InvalidPosition;
                break;
        }

        return result;
    }

    bool GroundBelow(float x, float y, float z, float& outZ) override
    {
        // The plant a walk heartbeat uses: a floor must be found, then her body's allowed height there.
        if (!Trinity::IsValidMapCoord(x, y, z) || _player->GetMapHeight(x, y, z) <= INVALID_HEIGHT)
            return false;

        float planted = z;
        _player->UpdateAllowedPositionZ(x, y, planted);
        if (planted <= INVALID_HEIGHT)
            return false;

        outZ = planted;
        return true;
    }

private:
    Player* _player;
    Map* _map;
};

#endif

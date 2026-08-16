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

#ifndef EVRY_MOD_PLAYERBOT_MOVEMENT_H
#define EVRY_MOD_PLAYERBOT_MOVEMENT_H

#include "Position.h"
#include <G3D/Vector3.h>
#include <vector>

class Player;
class WorldObject;

class PlayerbotWalker
{
public:
    bool Start(Player* player, Position const& destination, float stopDistance);
    void Update(Player* player, uint32 diff);
    void Stop(Player* player);
    void Reset();

    // Stand next to the target. Max interact range can land in a campfire on the way.
    static bool PickApproachPosition(Player* player, WorldObject const* target, float standDistance, Position& out);

    bool IsIdle() const { return _state == State::Idle; }
    bool IsMoving() const { return _state == State::Moving; }
    bool HasArrived() const { return _state == State::Arrived; }
    bool HasFailed() const { return _state == State::Failed; }

private:
    enum class State
    {
        Idle,
        Moving,
        Arrived,
        Failed
    };

    void QueueMove(Player* player, Position const& pos, bool moving, bool start);
    Position Advance(float distance);
    void Fail(Player* player, char const* reason);

    State _state = State::Idle;
    std::vector<G3D::Vector3> _path;
    size_t _pointIndex = 0;
    float _segmentProgress = 0.0f;
    Position _destination;
    float _stopDistance = 0.0f;
    uint32 _heartbeatMs = 0;
    uint32 _stuckMs = 0;
    uint32 _logMs = 0;
    Position _lastProgressPos;
};

#endif

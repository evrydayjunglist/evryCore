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
    bool StartedOnAFace() const { return _startedOnAFace; }

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
    bool PeekGroundedStep(Player* player, float distance, Position& out);
    bool FirstGroundedStepIsLegal(Player* player);
    bool MmapLookIsLegal(Player* player);
    bool StepTowardDestIsLegal(Player* player) const;
    void NoteLipDestProgress();
    void NoteLipOrigin();
    bool LeaveFaceExceeded() const;
    bool ContourShouldStop(Player* player) const;
    bool TryLeaveFace(Player* player, bool alreadyMoving);
    bool BuildMmapPath(Player* player, Position const& from, Position const& destination, std::vector<G3D::Vector3>& outPath);
    bool TryCommitMmap(Player* player, Position const& from, bool alreadyMoving);
    bool FindLipSidestep(Player* player, Position& out) const;
    bool ContinueContour(Player* player, bool alreadyMoving);
    bool WalkLegalDestStep(Player* player, bool alreadyMoving);
    void ApplyContourPath(Player* player, Position const& side, bool alreadyMoving);
    void RefuseSteepStep(Player* player, Position const& attempted);
    void Fail(Player* player, char const* reason);
    void FailNoLegalRing(Player* player);

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
    Position _lastGrounded;
    bool _contouring = false;
    bool _startedOnAFace = false;
    uint32 _lipSteps = 0;
    float _lipDestDist = 0.0f;
    Position _lipOrigin;
    bool _haveLipOrigin = false;
    bool _destPokeActive = false;
    float _contourDirX = 0.0f;
    float _contourDirY = 0.0f;
};

#endif

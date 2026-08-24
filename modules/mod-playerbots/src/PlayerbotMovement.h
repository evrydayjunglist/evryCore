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

#include "PlayerbotJump.h"
#include "PlayerbotMovementRecovery.h"
#include "Position.h"
#include <G3D/Vector3.h>
#include <vector>

class Player;
class WorldObject;
enum OpcodeClient : uint32;

class PlayerbotWalker
{
public:
    bool Start(Player* player, Position const& destination, float stopDistance, PlayerbotRecoveryGoal const& goal = {});
    void Update(Player* player, uint32 diff);
    void Stop(Player* player);
    void Reset();
    void SetOwningClientMovementMirror(bool enabled) { _mirrorOwningClientMovement = enabled; }

    static void StopAtFeet(Player* player);

    // Stand next to the target. Max interact range can land in a campfire on the way.
    static bool PickApproachPosition(Player* player, WorldObject const* target, float standDistance, Position& out);

    bool IsIdle() const { return _state == State::Idle; }
    bool IsMoving() const
    {
        return _state == State::Moving || _state == State::Jumping || _state == State::AwaitingClientSync;
    }
    bool IsJumping() const { return _state == State::Jumping; }
    bool HasArrived() const { return _state == State::Arrived; }
    bool HasFailed() const { return _state == State::Failed; }
    bool StartedOnAFace() const { return _startedOnAFace; }

private:
    enum class GroundedStepFailure
    {
        None,
        NoPath,
        NoFloor,
        SteepUp,
        TooFarDown,
        StaticCollision,
        DynamicCollision,
        InvalidPosition
    };

    enum class State
    {
        Idle,
        Moving,
        Jumping,
        AwaitingClientSync,
        Arrived,
        Failed
    };

    struct JumpPlan
    {
        Position Launch;
        Position Landing;
        PlayerbotJumpTrajectory Trajectory;
        float DirectionX = 0.0f;
        float DirectionY = 0.0f;
        uint32 DurationMs = 0;
    };

    struct MmapPathEvidence
    {
        bool Calculated = false;
        uint32 Type = 0;
        float Length = 0.0f;
        G3D::Vector3 ActualEnd = G3D::Vector3(0.0f, 0.0f, 0.0f);
        std::vector<G3D::Vector3> Prefix;
    };

    void QueueMove(Player* player, Position const& pos, bool moving, bool start);
    void QueueJumpMove(Player* player, OpcodeClient opcode, Position const& pos, uint32 fallTime);
    void FinishGroundedArrival(Player* player, Position const& pos);
    void UpdateOwningClientSync(Player* player, uint32 diff);
    Position Advance(float distance);
    bool PeekGroundedStep(Player* player, float distance, Position& out);
    GroundedStepFailure PeekGroundedStepFailure(Player* player, float distance, Position& out);
    GroundedStepFailure ClassifyGroundedStep(Player* player, Position const& from, float x, float y, float orientation,
        Position& out) const;
    bool FirstGroundedStepIsLegal(Player* player);
    bool MmapLookIsLegal(Player* player);
    bool StepTowardDestIsLegal(Player* player) const;
    void NoteLipDestProgress();
    void NoteLipOrigin();
    bool LeaveFaceExceeded() const;
    bool ContourShouldStop(Player* player) const;
    bool TryLeaveFace(Player* player, bool alreadyMoving);
    bool BuildMmapPath(Player* player, Position const& from, Position const& destination,
        std::vector<G3D::Vector3>& outPath, MmapPathEvidence* evidence = nullptr);
    bool TryCommitMmap(Player* player, Position const& from, bool alreadyMoving);
    bool RejoinPathReachesNewGround(Position const& from, std::vector<G3D::Vector3> const& path) const;
    void LogRecoveryMmap(Player* player, char const* decision, MmapPathEvidence const& evidence) const;
    void LogStartConnectivity(Player* player, Position const& from);
    bool FindLipSidestep(Player* player, Position& out) const;
    bool ContinueContour(Player* player, bool alreadyMoving);
    bool WalkLegalDestStep(Player* player, bool alreadyMoving);
    void ApplyContourPath(Player* player, Position const& side, bool alreadyMoving);
    bool BeginFaceRecovery(Player* player, GroundedStepFailure failure, Position const& attempted);
    void ClearFaceRecovery();
    void NoteMmapRejoinProgress(Player* player, Position const& previousFeet);
    void RefuseStep(Player* player, GroundedStepFailure failure, Position const& attempted);
    bool TryStartJump(Player* player);
    bool BuildJumpPlan(Player* player, JumpPlan& out, char const*& reason);
    bool JumpMovementIsAllowed(Player const* player, char const*& reason) const;
    void UpdateJump(Player* player, uint32 diff);
    void FinishJump(Player* player);
    void ResetNow();
    static char const* GroundedStepFailureName(GroundedStepFailure failure);
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
    uint32 _owningClientSyncMs = 0;
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
    PlayerbotFaceRecovery _faceRecovery;
    GroundedStepFailure _lastGroundedStepFailure = GroundedStepFailure::None;
    Position _lastRefusedStep;
    Position _lastRequestedStep;
    PlayerbotRecoveryGoal _recoveryGoal;
    JumpPlan _jump;
    uint32 _jumpElapsedMs = 0;
    uint32 _jumpHeartbeatMs = 0;
    uint32 _jumpMapId = 0;
    bool _stopAfterJump = false;
    bool _mirrorOwningClientMovement = false;
};

#endif

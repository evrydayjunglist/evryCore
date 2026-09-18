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

#include "PathGenerator.h"
#include "PlayerbotJump.h"
#include "PlayerbotMovementRecovery.h"
#include "PlayerbotWalkMapEscape.h"
#include "Position.h"
#include <G3D/Vector3.h>
#include <limits>
#include <memory>
#include <vector>

class Player;
class WorldObject;
enum OpcodeClient : uint32;

class PlayerbotWalker
{
public:
    // Steeper than this uphill in one step is refused.
    static constexpr float MaxWalkableSlopeDegrees = 35.0f;
    // One heartbeat. A curb, stair, or house slab. Longer than this is a cliff.
    static constexpr float MaxDownStepYards = 2.0f;

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

    bool Start(Player* player, Position const& destination, float stopDistance, PlayerbotRecoveryGoal const& goal = {});
    void Update(Player* player, uint32 diff);
    void Stop(Player* player);
    void Reset();
    // Forget the walk, and any arc in the air, without sending a packet: the server moved her or took over her movement.
    void Abandon();
    // The server rooted or stunned her. A walk on the ground stops once where her last step put her. An arc in the air
    // cannot stop, so it loses its sideways speed and falls from where it is.
    void HoldForRoot(Player* player);
    // The server knocked her back. Fly that arc from her client's feet as client falling movement and land where it meets
    // the floor. False, with the old walk dropped, when her movement is not ordinary falling movement.
    bool StartKnockback(Player* player, Position const& feet, float directionX, float directionY, float horizontalSpeed,
        float verticalSpeed, char const*& reason);
    // Where her client last put her: the last step or arc point she sent while walking or in the air, otherwise the
    // server's position.
    Position ClientFeet(Player const* player) const;
    void SetOwningClientMovementMirror(bool enabled) { _mirrorOwningClientMovement = enabled; }

    static void StopAtFeet(Player* player);

    // Stand next to the target. Max interact range can land in a campfire on the way.
    static bool PickApproachPosition(Player* player, WorldObject const* target, float standDistance, Position& out);

    // One grounded step from these feet toward (x, y), planted and judged exactly as a walk heartbeat is: the floor
    // search from her feet plus her climb, the slope and drop limits, and the chest-height ray. out holds the planted
    // feet whenever a floor was found.
    static GroundedStepFailure ClassifyGroundedStep(Player* player, Position const& from, float x, float y, float orientation,
        Position& out);
    // How far she moves in one walk heartbeat at her current run speed.
    static float HeartbeatStepLength(Player const* player);
    // Starts this world tick's shared budget for mapping the ground around bots that are looking for a way round.
    static void BeginWorldTick();
    // The destination of the last walk she started, and its map. It is kept after that walk ends so a diagnostic can
    // still show where she was going.
    bool LastWalkDestination(Position& out, uint32& mapId) const;

    bool IsIdle() const { return _state == State::Idle; }
    bool IsMoving() const
    {
        return _state == State::Moving || _state == State::Jumping || _state == State::AwaitingClientSync
            || _state == State::LookingForAWayRound;
    }
    bool IsJumping() const { return _state == State::Jumping; }
    bool HasArrived() const { return _state == State::Arrived; }
    bool HasFailed() const { return _state == State::Failed; }
    bool StartedOnAFace() const { return _startedOnAFace; }

private:
    enum class State
    {
        Idle,
        Moving,
        Jumping,
        AwaitingClientSync,
        // Standing still, mapping the ground around her feet to find a way round what refused her.
        LookingForAWayRound,
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
        // From here she moves straight down: the arc met a wall, or a root took her sideways speed.
        uint32 SidewaysStopMs = std::numeric_limits<uint32>::max();
        // From here she falls from rest: her head met something above her on the way up.
        uint32 CeilingMs = std::numeric_limits<uint32>::max();
        // The server threw her. No key is held, and she replans after landing.
        bool Knockback = false;
        // The arc ends where she touches water deep enough to swim in, with the swim-start packet instead of a landing.
        bool EndsInWater = false;
        // No floor caught her before the bottom of the world.
        bool EndsBelowWorld = false;
    };

    struct MmapPathEvidence
    {
        bool Calculated = false;
        // How long the navmesh took to build the route, in milliseconds.
        float BuildMs = 0.0f;
        uint32 Type = 0;
        float Length = 0.0f;
        G3D::Vector3 ActualEnd = G3D::Vector3(0.0f, 0.0f, 0.0f);
        std::vector<G3D::Vector3> Prefix;
        // Which set of movement maps answered: the one built for a player's body, or the creature one.
        bool PlayerNavMesh = false;
        // What the navmesh search and the smoothing did for this route.
        PathSearchReport Search;
    };

    void QueueMove(Player* player, Position const& pos, bool moving, bool start);
    void QueueJumpMove(Player* player, OpcodeClient opcode, Position const& pos, uint32 fallTime);
    void FinishGroundedArrival(Player* player, Position const& pos);
    void FinishShortOfDestination(Player* player, Position const& pos);
    void UpdateOwningClientSync(Player* player, uint32 diff);
    Position Advance(float distance);
    bool PeekGroundedStep(Player* player, float distance, Position& out);
    GroundedStepFailure PeekGroundedStepFailure(Player* player, float distance, Position& out);
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
    void LogConnectivity(Player* player, Position const& from, char const* place) const;
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
    bool FlightMovementIsAllowed(Player const* player, char const*& reason) const;
    static Position ArcPosition(JumpPlan const& plan, uint32 timeMs);
    bool SampleFlight(Player* player, JumpPlan& plan, uint32 fromMs, char const*& reason);
    void UpdateJump(Player* player, uint32 diff);
    void FinishJump(Player* player);
    // Stop and start mapping the ground around her feet. False when she has already looked on this approach or her
    // movement does not allow it; the caller then gives the walk up as before.
    bool BeginWayRound(Player* player, char const* reason);
    void UpdateWayRound(Player* player, uint32 diff);
    // Walk the best way round the map found toward the destination. False when nothing she can reach is closer.
    bool StartWayRoundWalk(Player* player);
    // Ask the navmesh for a route from the next way out of this ground. True once she starts walking to one.
    bool ProbeOneWayOut(Player* player);
    bool WalkTheWayRound(Player* player, PlayerbotWalkMapWayRound const& way, char const* what);
    // A step of a way round was refused. Walk the rest of it from here, on the map she already has.
    bool RepairWayRound(Player* player);
    // Are the first yards of this route walkable from there by her step rules?
    static bool RouteStartsWalkable(Player* player, Position const& from, std::vector<G3D::Vector3> const& path);
    void ClearWayRound();
    void ResetNow();
    static char const* GroundedStepFailureName(GroundedStepFailure failure);
    void Fail(Player* player, char const* reason);
    void FailNoLegalRing(Player* player);

    State _state = State::Idle;
    std::vector<G3D::Vector3> _path;
    uint32 _pathType = 0;
    // Where walks of this approach already stopped short of the destination.
    std::vector<Position> _shortStops;
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
    // The arc started during this tick, so this tick's time was spent before it existed.
    bool _skipNextJumpDiff = false;
    bool _mirrorOwningClientMovement = false;
    Position _lastWalkDestination;
    uint32 _lastWalkDestinationMapId = 0;
    bool _hasLastWalkDestination = false;
    // While she stands and looks, she maps the ground first and then asks the navmesh from the ways out of it.
    enum class WayRoundPhase
    {
        Mapping,
        Probing
    };

    // The ground she mapped. It is kept while she walks a way round, so a refused step can walk the rest from there.
    std::unique_ptr<PlayerbotWalkMap> _wayRoundMap;
    std::vector<PlayerbotWalkMapWayRound> _wayRoundWaysOut;
    size_t _wayRoundProbe = 0;
    int32 _wayRoundTarget = -1;
    uint32 _wayRoundRepairs = 0;
    WayRoundPhase _wayRoundPhase = WayRoundPhase::Mapping;
    uint32 _wayRoundMs = 0;
    uint32 _wayRoundMapId = 0;
    // The path she is walking is a way round the map found, not a navmesh route.
    bool _walkingAWayRound = false;
    // One look per approach. A second one would only find the same ground.
    bool _lookedForAWayRound = false;
};

#endif

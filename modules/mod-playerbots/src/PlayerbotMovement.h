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
#include "PlayerbotBadPlaces.h"
#include "PlayerbotJump.h"
#include "PlayerbotMovementRecovery.h"
#include "PlayerbotPathSearch.h"
#include "PlayerbotWalkMapEscape.h"
#include "PlayerbotWayRoundTurns.h"
#include "Position.h"
#include <G3D/Vector3.h>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
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
        // Her body does not fit standing on that floor: something low is over it.
        NoHeadroom,
        // A step down steeper than she may climb back, onto ground whose route on is refused or goes nowhere.
        NoWayOut,
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

    // Stand next to the target. Max interact range can land in a campfire on the way. With look, also say how each place
    // it asked about came out, so a log line can say why none would do.
    static bool PickApproachPosition(Player* player, WorldObject const* target, float standDistance, Position& out,
        std::vector<StandSpotLook>* look = nullptr);
    // Drop the stand spots she picked in the last few seconds: she logged out.
    static void ForgetStandSpots(Player const* player);
    // One sentence on the stand spot picks since the last call: how many, how many were answered from memory, and how
    // many sides were asked about. Then it starts counting again.
    static std::string DescribeStandSpotsAndClear();

    // One grounded step from these feet toward (x, y), planted and judged exactly as a walk heartbeat is: the floor
    // search from her feet plus her climb, the slope and drop limits, and the chest-height ray. out holds the planted
    // feet whenever a floor was found.
    static GroundedStepFailure ClassifyGroundedStep(Player* player, Position const& from, float x, float y, float orientation,
        Position& out);
    // The plant a walk heartbeat uses at (x, y), looking down from searchZ: false when there is no floor, otherwise her
    // body's allowed height there.
    static bool PlantAt(Player* player, float x, float y, float searchZ, float& outZ);
    // How far she moves in one walk heartbeat at her current run speed.
    static float HeartbeatStepLength(Player const* player);
    // Starts this world tick's shared budget for mapping the ground around bots that are looking for a way round.
    static void BeginWorldTick();
    // One sentence on the way-round looks since the last call: how many started and why, how they ended, and how long
    // bots waited for their turn. Then it starts counting again.
    static std::string DescribeWayRoundLooksAndClear();
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
    // Walking a route on her heartbeats: not in the air, not standing still to look for a way round.
    bool IsWalkingARoute() const { return _state == State::Moving; }
    bool HasArrived() const { return _state == State::Arrived; }
    bool HasFailed() const { return _state == State::Failed; }
    // The walk failed because the ground she mapped around her feet has no spot she can step to, whatever she was
    // walking to. The place she stands is the problem, not the target.
    bool FailedAtHerFeet() const { return _state == State::Failed && _failedAtHerFeet; }
    // The last walk she was asked to start was given up at once because its route went past a place where a walk of
    // hers failed. Kept after Reset so the caller can still ask.
    bool LastStartMetABadPlace() const { return _startMetBadPlace; }
    bool StartedOnAFace() const { return _startedOnAFace; }
    // She stopped to look for a way round and has not joined the line of bots taking turns to map yet. Her brain may
    // send her somewhere else first; otherwise she joins the line on her next update or when told to.
    bool WantsToLookForAWayRound() const { return _state == State::LookingForAWayRound && _wayRoundJoinReason; }
    void JoinWayRoundLine(Player* player);
    // She stopped to look for a way round and went to another spot of the same work instead, for the one-minute report.
    static void NoteWentElsewhereInsteadOfAWayRound();

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
        // She walked off the edge of ground she could not step off any other way.
        bool WalkOff = false;
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
    // Her body does not fit on these feet. Stand her on the surface over her and move feet there. False when there is
    // room, or no surface over her with room for her body.
    bool StandUpOutOfPocket(Player* player, Position& feet);
    void QueueJumpMove(Player* player, OpcodeClient opcode, Position const& pos, uint32 fallTime);
    void FinishGroundedArrival(Player* player, Position const& pos);
    void FinishShortOfDestination(Player* player, Position const& pos);
    void UpdateOwningClientSync(Player* player, uint32 diff);
    Position Advance(float distance);
    bool PeekGroundedStep(Player* player, float distance, Position& out);
    GroundedStepFailure PeekGroundedStepFailure(Player* player, float distance, Position& out);
    // She just stepped down steeper than she may climb, so she could not walk back up that step. Walk the next yards of
    // this route from there on paper, a heartbeat at a time. True when one of them is refused, or the route ends there
    // with no step she may take, so this step would leave her somewhere she cannot walk out of.
    bool StepDownLeadsNowhere(Player* player, Position const& landing, float stepLen, GroundedStepFailure& aheadFailure,
        Position& aheadAt);
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
    // She cannot step anywhere from her feet. Walk off an edge whose drop a player would walk off, as a player does,
    // with the forward key held, and fall with ordinary falling movement. Only when the landing leaves her above half
    // health, or is deep water, has room for her body, and has ground she can step on. False when there is no such edge.
    bool TryWalkOffLedge(Player* player);
    void UpdateJump(Player* player, uint32 diff);
    void FinishJump(Player* player);
    // Stop and start mapping the ground around her feet. False when she has already looked on this approach or her
    // movement does not allow it; the caller then gives the walk up as before.
    bool BeginWayRound(Player* player, char const* reason);
    void UpdateWayRound(Player* player, uint32 diff);
    void StartWayRoundMap(Player* player, float yards);
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
    // Her look is over: give up her place in the line of bots taking turns to map, and count how it ended.
    void EndWayRoundTurn(PlayerbotWayRoundEnd end);
    void ResetNow();
    static char const* GroundedStepFailureName(GroundedStepFailure failure);
    void Fail(Player* player, char const* reason);
    void FailNoLegalRing(Player* player);
    // This walk failed after every way round was tried: remember where it first refused her and which way she was going.
    void NoteBadPlace(Player* player);
    // She cannot step anywhere from her feet: say, once for this place, what refuses each of the eight steps round her.
    void ExplainStuckFeet(Player* player, Position const& feet);

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
    // How far around her feet that map reaches: the small circle first, then the full one.
    float _wayRoundYards = 0.0f;
    std::vector<PlayerbotWalkMapWayRound> _wayRoundWaysOut;
    size_t _wayRoundProbe = 0;
    int32 _wayRoundTarget = -1;
    uint32 _wayRoundRepairs = 0;
    WayRoundPhase _wayRoundPhase = WayRoundPhase::Mapping;
    // Time spent mapping since her first turn. Time waiting in line for it does not count against her.
    uint32 _wayRoundMs = 0;
    uint32 _wayRoundMapId = 0;
    // Her place in the line of bots taking turns to map the ground, or 0.
    uint64 _wayRoundTicket = 0;
    // Why she stopped, until she joins the line. A string that lives as long as the program.
    char const* _wayRoundJoinReason = nullptr;
    uint32 _wayRoundWaitMs = 0;
    // World-thread time her look has taken so far, mapping and asking the navmesh from the ways out.
    std::chrono::steady_clock::duration _wayRoundWork = std::chrono::steady_clock::duration::zero();
    bool _wayRoundHadATurn = false;
    // The path she is walking is a way round the map found, not a navmesh route.
    bool _walkingAWayRound = false;
    // One look per approach. A second one would only find the same ground.
    bool _lookedForAWayRound = false;
    // This look found a way round or a way out and her first step onto it was refused.
    bool _wayRoundFirstStepRefused = false;
    bool _failedAtHerFeet = false;
    // The first step refused on this approach, where its navmesh route met the obstacle, and her feet then.
    Position _approachFeet;
    Position _approachRefused;
    bool _haveApproachRefusal = false;
    // The step that began the obstacle recovery going on now, and her feet then.
    Position _episodeFeet;
    Position _episodeRefused;
    bool _haveEpisodeRefusal = false;
    // Places where her walks failed. Kept across walks and resets; see PlayerbotBadPlaces.
    PlayerbotBadPlaces _badPlaces;
    bool _startMetBadPlace = false;
    // The feet whose refused steps were last written to the log, so a bot stuck there says it once.
    Position _stuckFeetExplained;
    bool _haveStuckFeetExplained = false;
    // The feet where she last said why she did not walk off an edge.
    Position _walkOffExplained;
    bool _haveWalkOffExplained = false;
};

#endif

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

#include "PlayerbotMovement.h"
#include "Common.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridDefines.h"
#include "Log.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MovementInfo.h"
#include "Object.h"
#include "Opcodes.h"
#include "PathGenerator.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "PlayerbotClient.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Unit.h"
#include "UnitDefines.h"
#include "VMapFactory.h"
#include "VMapManager.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    constexpr uint32 HEARTBEAT_INTERVAL_MS = 100;
    constexpr uint32 STUCK_TIMEOUT_MS = 3000;
    constexpr uint32 HEARTBEAT_LOG_INTERVAL_MS = 1000;
    constexpr uint32 REFUSED_PATH_TYPES = PATHFIND_NOPATH | PATHFIND_SHORTCUT | PATHFIND_NOT_USING_PATH;
    constexpr float SPELL_FOCUS_AVOID_RADIUS = 2.5f;
    constexpr float VIA_EXTRA_CLEARANCE = 1.0f;
    constexpr float MAX_WALKABLE_SLOPE_DEGREES = 35.0f;
    // One heartbeat. A curb, stair, or house slab. Longer than this is a cliff.
    constexpr float MAX_DOWN_STEP_YARDS = 2.0f;
    // Small patch around her feet. Living tiles mark 55° as ordinary ground; mmap walks her into a face a player would step around.
    constexpr float LIP_LOOK_RADIUS = 4.0f;
    constexpr float LIP_LOOK_CELL = 1.0f;
    constexpr float LIP_MIN_DEST_DOT = -0.15f;
    constexpr float LIP_DEST_PROGRESS_YARDS = 1.0f;
    constexpr uint32 LIP_MAX_STEPS = 8;
    constexpr int32 LIP_LOOK_DIRECTIONS = 16;
    // Walk off a face onto open ground, then mmap. This is not a path around a mountain.
    constexpr float LEAVE_FACE_MAX_YARDS = 24.0f;

    float HeartbeatStepLen(Player const* player)
    {
        if (!player)
            return LIP_LOOK_CELL;

        return player->GetSpeed(MOVE_RUN) * (float(HEARTBEAT_INTERVAL_MS) / 1000.0f);
    }

    struct GroundedStep
    {
        float run = 0.0f;
        float rise = 0.0f;
        float degrees = 0.0f;
    };

    GroundedStep MeasureGroundedStep(Position const& from, Position const& to)
    {
        GroundedStep step;
        float const dx = to.GetPositionX() - from.GetPositionX();
        float const dy = to.GetPositionY() - from.GetPositionY();
        step.run = std::sqrt(dx * dx + dy * dy);
        step.rise = to.GetPositionZ() - from.GetPositionZ();
        step.degrees = std::atan2(std::fabs(step.rise), step.run) * (180.0f / float(M_PI));
        return step;
    }

    bool GroundedStepIsLegal(GroundedStep const& step)
    {
        if (step.rise > 0.0f)
            return step.degrees <= MAX_WALKABLE_SLOPE_DEGREES;

        return -step.rise <= MAX_DOWN_STEP_YARDS;
    }

    // Same rays as WorldObject::MovePositionToFirstCollision: static vmap, then dynamic gameobject.
    // Chest height, not feet, so floors, ramps, doorways, and stairs are not walls. Do not relocate.
    bool StepHitsWorldCollision(Player const* player, Position const& from, Position const& to)
    {
        if (!player || !player->IsInWorld())
            return false;

        Map* map = player->FindMap();
        if (!map)
            return false;

        float const fromX = from.GetPositionX();
        float const fromY = from.GetPositionY();
        float const fromZ = from.GetPositionZ();
        float destX = to.GetPositionX();
        float destY = to.GetPositionY();
        float destZ = to.GetPositionZ();
        if (!Trinity::IsValidMapCoord(fromX, fromY, fromZ) || !Trinity::IsValidMapCoord(destX, destY, destZ))
            return true;

        float const dx = destX - fromX;
        float const dy = destY - fromY;
        if ((dx * dx + dy * dy) < 0.0001f)
            return false;

        float const halfHeight = player->GetCollisionHeight() * 0.5f;
        float hitX = destX;
        float hitY = destY;
        float hitZ = destZ;
        if (VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(
            PhasingHandler::GetTerrainMapId(player->GetPhaseShift(), player->GetMapId(), map->GetTerrain(), fromX, fromY),
            fromX, fromY, fromZ + halfHeight,
            destX, destY, destZ + halfHeight,
            hitX, hitY, hitZ, -0.5f))
            return true;

        if (map->getObjectHitPos(player->GetPhaseShift(),
            fromX, fromY, fromZ + halfHeight,
            destX, destY, destZ + halfHeight,
            hitX, hitY, hitZ, -0.5f))
            return true;

        return false;
    }

    bool GroundedStepIsWalkable(Player const* player, Position const& from, Position const& to)
    {
        if (!GroundedStepIsLegal(MeasureGroundedStep(from, to)))
            return false;

        return !StepHitsWorldCollision(player, from, to);
    }

    // Dirt is this plant, not the chest-height wall ray. Search from last feet plus the most she may climb this step.
    // Do not search from a navmesh chord that already went through a hill. No floor is a face: refuse it.
    bool PlantWalkZ(Player* player, float x, float y, float lastGroundedZ, float run, float& outZ)
    {
        if (!player || !player->IsInWorld())
            return false;
        if (!Trinity::IsValidMapCoord(x, y, lastGroundedZ))
            return false;

        float const maxRise = std::tan(MAX_WALKABLE_SLOPE_DEGREES * (float(M_PI) / 180.0f)) * std::max(run, 0.05f);
        float const searchZ = lastGroundedZ + maxRise;
        if (player->GetMapHeight(x, y, searchZ) <= INVALID_HEIGHT)
            return false;

        float z = searchZ;
        player->UpdateAllowedPositionZ(x, y, z);
        if (z <= INVALID_HEIGHT)
            return false;

        outZ = z;
        return true;
    }

    bool PlantFromFeet(Player* player, Position const& feet, float x, float y, float orientation, Position& out)
    {
        float const dx = x - feet.GetPositionX();
        float const dy = y - feet.GetPositionY();
        float const run = std::sqrt(dx * dx + dy * dy);
        float z = 0.0f;
        if (!PlantWalkZ(player, x, y, feet.GetPositionZ(), run, z))
            return false;

        out.Relocate(x, y, z, orientation);
        return true;
    }

    // One heartbeat can sit on the toe of a ridge. Sample the local look toward a heading, cell by cell.
    bool LookAheadIsWalkable(Player* player, Position const& feet, float dirX, float dirY, float lookDist)
    {
        if (!player || lookDist < 0.05f)
            return false;

        float const stepLen = HeartbeatStepLen(player);
        if (stepLen < 0.05f)
            return false;

        Position prev = feet;
        float sampled = 0.0f;
        while (sampled + 0.05f < lookDist)
        {
            float const nextDist = std::min(lookDist, sampled + (sampled < 0.01f ? stepLen : LIP_LOOK_CELL));
            float const x = feet.GetPositionX() + dirX * nextDist;
            float const y = feet.GetPositionY() + dirY * nextDist;
            Position cell;
            if (!PlantFromFeet(player, prev, x, y, 0.0f, cell))
                return false;
            if (!GroundedStepIsWalkable(player, prev, cell))
                return false;
            prev = cell;
            sampled = nextDist;
        }

        return true;
    }

    struct AvoidCircle
    {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
        std::string name;
    };

    void CollectSpellFocusAvoids(WorldObject const* source, float range, std::vector<AvoidCircle>& out)
    {
        std::vector<GameObject*> gameObjects;
        FindGameObjectOptions options;
        options.GameObjectType = GAMEOBJECT_TYPE_SPELL_FOCUS;
        source->GetGameObjectListWithOptionsInGrid(gameObjects, range, options);

        for (GameObject* go : gameObjects)
        {
            if (!go)
                continue;

            float radius = SPELL_FOCUS_AVOID_RADIUS;
            if (GameObjectTemplate const* info = go->GetGOInfo())
                radius = std::max(radius, info->size * 1.5f);

            AvoidCircle circle;
            circle.x = go->GetPositionX();
            circle.y = go->GetPositionY();
            circle.radius = radius;
            circle.name = go->GetName();
            out.push_back(circle);
        }
    }

    bool IsInsideAvoid(float x, float y, std::vector<AvoidCircle> const& avoids)
    {
        for (AvoidCircle const& circle : avoids)
        {
            float const dx = x - circle.x;
            float const dy = y - circle.y;
            if ((dx * dx + dy * dy) < (circle.radius * circle.radius))
                return true;
        }

        return false;
    }

    bool CircleContains(AvoidCircle const& circle, float x, float y)
    {
        float const dx = x - circle.x;
        float const dy = y - circle.y;
        return (dx * dx + dy * dy) < (circle.radius * circle.radius);
    }

    bool SegmentHitsCircle(float x1, float y1, float x2, float y2, AvoidCircle const& circle)
    {
        float const dx = x2 - x1;
        float const dy = y2 - y1;
        float const fx = circle.x - x1;
        float const fy = circle.y - y1;
        float const len2 = dx * dx + dy * dy;
        float t = 0.0f;
        if (len2 > 0.0001f)
            t = (fx * dx + fy * dy) / len2;
        if (t < 0.0f)
            t = 0.0f;
        else if (t > 1.0f)
            t = 1.0f;

        float const px = x1 + dx * t - circle.x;
        float const py = y1 + dy * t - circle.y;
        return (px * px + py * py) < (circle.radius * circle.radius);
    }

    bool PathHitsAvoid(Movement::PointsArray const& path, std::vector<AvoidCircle> const& avoids)
    {
        if (path.empty())
            return false;

        for (AvoidCircle const& circle : avoids)
        {
            // Already standing in it: walking out is required. Do not treat that circle as a hit.
            if (CircleContains(circle, path[0].x, path[0].y))
                continue;

            if (path.size() < 2)
            {
                if (CircleContains(circle, path[0].x, path[0].y))
                    return true;
                continue;
            }

            for (size_t i = 0; i + 1 < path.size(); ++i)
            {
                if (SegmentHitsCircle(path[i].x, path[i].y, path[i + 1].x, path[i + 1].y, circle))
                    return true;
            }
        }

        return false;
    }

    AvoidCircle const* FirstHitAvoid(Movement::PointsArray const& path, std::vector<AvoidCircle> const& avoids)
    {
        if (path.empty())
            return nullptr;

        for (AvoidCircle const& circle : avoids)
        {
            if (CircleContains(circle, path[0].x, path[0].y))
                continue;

            if (path.size() < 2)
            {
                if (CircleContains(circle, path[0].x, path[0].y))
                    return &circle;
                continue;
            }

            for (size_t i = 0; i + 1 < path.size(); ++i)
            {
                if (SegmentHitsCircle(path[i].x, path[i].y, path[i + 1].x, path[i + 1].y, circle))
                    return &circle;
            }
        }

        return nullptr;
    }

    bool PathIsWalkable(PathGenerator const& generator)
    {
        return !(generator.GetPathType() & REFUSED_PATH_TYPES) && generator.GetPath().size() >= 2;
    }
}

void PlayerbotWalker::Stop(Player* player)
{
    if (_state == State::Moving && player && player->GetSession())
        QueueMove(player, player->GetPosition(), false, false);

    Reset();
}

void PlayerbotWalker::Reset()
{
    _state = State::Idle;
    _path.clear();
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _destination.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _stopDistance = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _lastProgressPos.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _lastGrounded.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _contouring = false;
    _startedOnAFace = false;
    _lipSteps = 0;
    _lipDestDist = 0.0f;
    _lipOrigin.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _haveLipOrigin = false;
    _destPokeActive = false;
    _contourDirX = 0.0f;
    _contourDirY = 0.0f;
}

bool PlayerbotWalker::PickApproachPosition(Player* player, WorldObject const* target, float standDistance, Position& out)
{
    if (!player || !target)
        return false;

    std::vector<AvoidCircle> avoids;
    CollectSpellFocusAvoids(player, 50.0f, avoids);
    CollectSpellFocusAvoids(target, 20.0f, avoids);

    float const fromTargetToPlayer = target->GetAbsoluteAngle(player);
    float const offsets[] =
    {
        float(M_PI) / 2.0f,
        -float(M_PI) / 2.0f,
        float(M_PI),
        3.0f * float(M_PI) / 4.0f,
        -3.0f * float(M_PI) / 4.0f,
        float(M_PI) / 4.0f,
        -float(M_PI) / 4.0f,
        0.0f
    };

    Position best;
    float bestLen = std::numeric_limits<float>::max();
    bool found = false;
    bool foundClear = false;

    for (float offset : offsets)
    {
        float const angle = Position::NormalizeOrientation(fromTargetToPlayer + offset);
        float x = target->GetPositionX() + std::cos(angle) * standDistance;
        float y = target->GetPositionY() + std::sin(angle) * standDistance;
        float z = target->GetPositionZ();
        player->UpdateAllowedPositionZ(x, y, z);

        if (IsInsideAvoid(x, y, avoids))
            continue;

        PathGenerator generator(player);
        if (!generator.CalculatePath(x, y, z, false) || !PathIsWalkable(generator))
            continue;

        bool const hits = PathHitsAvoid(generator.GetPath(), avoids);
        float const len = generator.GetPathLength();
        if (!hits)
        {
            if (!foundClear || len < bestLen)
            {
                best.Relocate(x, y, z, Position::NormalizeOrientation(angle + float(M_PI)));
                bestLen = len;
                foundClear = true;
                found = true;
            }
        }
        else if (!foundClear && (!found || len < bestLen))
        {
            best.Relocate(x, y, z, Position::NormalizeOrientation(angle + float(M_PI)));
            bestLen = len;
            found = true;
        }
    }

    if (!found)
        return false;

    out = best;
    return true;
}

bool PlayerbotWalker::Start(Player* player, Position const& destination, float stopDistance)
{
    if (!player || !player->IsInWorld() || !player->GetSession())
    {
        _state = State::Failed;
        return false;
    }

    bool const sameDest = _destination.GetExactDist(destination) < 1.0f;
    // Already walking this dest. Do not rebuild mmap from the same feet.
    if (_state == State::Moving && sameDest)
        return true;

    uint32 const savedLipSteps = sameDest ? _lipSteps : 0;
    float const savedLipDestDist = sameDest ? _lipDestDist : 0.0f;
    Position const savedLipOrigin = sameDest ? _lipOrigin : Position();
    bool const savedHaveLipOrigin = sameDest && _haveLipOrigin;
    bool const savedDestPokeActive = sameDest && _destPokeActive;
    float const savedCx = sameDest ? _contourDirX : 0.0f;
    float const savedCy = sameDest ? _contourDirY : 0.0f;

    Reset();
    _lipSteps = savedLipSteps;
    _lipDestDist = savedLipDestDist;
    _lipOrigin = savedLipOrigin;
    _haveLipOrigin = savedHaveLipOrigin;
    _destPokeActive = savedDestPokeActive;
    _contourDirX = savedCx;
    _contourDirY = savedCy;

    float x = player->GetPositionX();
    float y = player->GetPositionY();
    float z = player->GetPositionZ();
    player->UpdateAllowedPositionZ(x, y, z);
    Position from;
    from.Relocate(x, y, z, player->GetOrientation());

    _destination = destination;
    _stopDistance = stopDistance;
    _lastGrounded = from;

    if (from.GetExactDist(destination) <= stopDistance)
    {
        _state = State::Arrived;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is already in range of the walk destination.", player->GetName());
        return true;
    }

    std::vector<G3D::Vector3> path;
    if (!BuildMmapPath(player, from, destination, path))
    {
        _lastProgressPos = from;
        if (TryLeaveFace(player, false))
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path from these feet. Walking off this face, then mmap.",
                player->GetName());
            return true;
        }

        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path to the destination. The bot is standing still.",
            player->GetName());
        _state = State::Failed;
        return false;
    }

    _path = std::move(path);
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _lastProgressPos = from;
    _contouring = false;
    _startedOnAFace = false;

    if (FirstGroundedStepIsLegal(player))
    {
        _lipSteps = 0;
        _lipDestDist = 0.0f;
        _haveLipOrigin = false;
        _destPokeActive = false;
        _contourDirX = 0.0f;
        _contourDirY = 0.0f;
        _state = State::Moving;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} starting walk. {} points, length to destination {:.1f} yards.",
            player->GetName(), uint32(_path.size()), from.GetExactDist(destination));
        QueueMove(player, from, true, true);
        return true;
    }

    // Mmap's first step is a face or a wall. That is not unreachable. Walk legal ground beside it.
    // A wall is not a hill: look for another same-objective yellow only when this first step is steep.
    Position first;
    bool const planted = PeekGroundedStep(player, HeartbeatStepLen(player), first);
    _startedOnAFace = !planted || !GroundedStepIsLegal(MeasureGroundedStep(_lastGrounded, first));
    if (TryLeaveFace(player, false))
        return true;

    FailNoLegalRing(player);
    return false;
}

void PlayerbotWalker::Update(Player* player, uint32 diff)
{
    if (_state != State::Moving || !player || !player->IsInWorld() || !player->GetSession())
        return;

    if (!player->movespline->Finalized())
    {
        Fail(player, "a server spline is still running; client move packets would be ignored");
        return;
    }

    _heartbeatMs += diff;
    _stuckMs += diff;
    _logMs += diff;

    float const speed = player->GetSpeed(MOVE_RUN);
    while (_heartbeatMs >= HEARTBEAT_INTERVAL_MS && _state == State::Moving)
    {
        _heartbeatMs -= HEARTBEAT_INTERVAL_MS;
        float const stepLen = speed * (float(HEARTBEAT_INTERVAL_MS) / 1000.0f);
        Position const next = Advance(stepLen);
        Position grounded;
        if (!PlantFromFeet(player, _lastGrounded, next.GetPositionX(), next.GetPositionY(), next.GetOrientation(), grounded)
            || !GroundedStepIsWalkable(player, _lastGrounded, grounded))
        {
            RefuseSteepStep(player, grounded);
            return;
        }

        _lastGrounded = grounded;

        bool const atDest = grounded.GetExactDist(_destination) <= _stopDistance;
        bool const pathDone = _pointIndex + 1 >= _path.size();
        if (atDest)
        {
            QueueMove(player, grounded, false, false);
            _state = State::Arrived;
            _contouring = false;
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} stopped at ({:.2f}, {:.2f}, {:.2f}).",
                player->GetName(), grounded.GetPositionX(), grounded.GetPositionY(), grounded.GetPositionZ());
            return;
        }

        if (_contouring && pathDone)
        {
            // Do not mmap from the pocket: MmapLookIsLegal refuses a prefix that is still a face.
            // Dest may get farther while she walks off the face onto open ground. That is the leave, not a fail.
            if (TryCommitMmap(player, _lastGrounded, true))
                return;
            if (!TryLeaveFace(player, true))
                FailNoLegalRing(player);
            return;
        }

        if (pathDone)
        {
            QueueMove(player, grounded, false, false);
            _state = State::Arrived;
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} stopped at ({:.2f}, {:.2f}, {:.2f}).",
                player->GetName(), grounded.GetPositionX(), grounded.GetPositionY(), grounded.GetPositionZ());
            return;
        }

        QueueMove(player, grounded, true, false);

        if (grounded.GetExactDist2d(_lastProgressPos) > 0.25f)
        {
            _lastProgressPos = grounded;
            _stuckMs = 0;
        }
    }

    if (_logMs >= HEARTBEAT_LOG_INTERVAL_MS && _state == State::Moving)
    {
        _logMs = 0;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking at ({:.2f}, {:.2f}, {:.2f}).",
            player->GetName(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    }

    if (_stuckMs >= STUCK_TIMEOUT_MS)
        Fail(player, "position stopped advancing");
}

void PlayerbotWalker::QueueMove(Player* player, Position const& pos, bool moving, bool start)
{
    MovementInfo info = player->m_movementInfo;
    info.guid = player->GetGUID();
    info.time = GameTime::GetGameTimeMS();
    info.pos = pos;

    if (moving)
        info.AddMovementFlag(MOVEMENTFLAG_FORWARD);
    else
        info.RemoveMovementFlag(MOVEMENTFLAG_FORWARD);

    OpcodeClient opcode = CMSG_MOVE_HEARTBEAT;
    if (start)
        opcode = CMSG_MOVE_START_FORWARD;
    else if (!moving)
        opcode = CMSG_MOVE_STOP;

    PlayerbotClient::QueueMovement(player->GetSession(), opcode, info);
}

Position PlayerbotWalker::Advance(float distance)
{
    if (_path.size() < 2)
        return _destination;

    size_t index = _pointIndex;
    float progress = _segmentProgress;
    float remaining = distance;

    while (remaining > 0.0f && index + 1 < _path.size())
    {
        G3D::Vector3 const& from = _path[index];
        G3D::Vector3 const& to = _path[index + 1];
        float const segLen = (to - from).length();
        float const left = segLen - progress;
        if (left <= 0.0001f)
        {
            ++index;
            progress = 0.0f;
            continue;
        }

        if (remaining >= left)
        {
            remaining -= left;
            ++index;
            progress = 0.0f;
        }
        else
        {
            progress += remaining;
            remaining = 0.0f;
        }
    }

    G3D::Vector3 point;
    float orientation = 0.0f;
    if (index + 1 >= _path.size())
    {
        point = _path.back();
        if (_path.size() >= 2)
        {
            G3D::Vector3 const delta = _path.back() - _path[_path.size() - 2];
            orientation = Position::NormalizeOrientation(std::atan2(delta.y, delta.x));
        }
    }
    else
    {
        G3D::Vector3 const& from = _path[index];
        G3D::Vector3 const& to = _path[index + 1];
        G3D::Vector3 const delta = to - from;
        float const segLen = delta.length();
        float const t = segLen > 0.0f ? progress / segLen : 0.0f;
        point = from + delta * t;
        orientation = Position::NormalizeOrientation(std::atan2(delta.y, delta.x));
    }

    _pointIndex = index;
    _segmentProgress = progress;

    Position pos;
    pos.Relocate(point.x, point.y, point.z, orientation);
    return pos;
}

bool PlayerbotWalker::PeekGroundedStep(Player* player, float distance, Position& out)
{
    size_t const savedIndex = _pointIndex;
    float const savedProgress = _segmentProgress;
    Position next = Advance(distance);
    _pointIndex = savedIndex;
    _segmentProgress = savedProgress;

    if (!player)
        return false;

    return PlantFromFeet(player, _lastGrounded, next.GetPositionX(), next.GetPositionY(), next.GetOrientation(), out);
}

bool PlayerbotWalker::FirstGroundedStepIsLegal(Player* player)
{
    if (!player)
        return false;

    Position first;
    if (!PeekGroundedStep(player, HeartbeatStepLen(player), first))
        return false;

    return GroundedStepIsWalkable(player, _lastGrounded, first);
}

bool PlayerbotWalker::MmapLookIsLegal(Player* player)
{
    if (!player)
        return false;

    float const stepLen = HeartbeatStepLen(player);
    if (stepLen < 0.05f)
        return false;

    Position prev = _lastGrounded;
    for (float d = stepLen; d <= LIP_LOOK_RADIUS + 0.01f; d += LIP_LOOK_CELL)
    {
        Position planted;
        if (!PeekGroundedStep(player, d, planted))
            return false;
        if (planted.GetExactDist(prev) < 0.05f)
            break;
        if (!GroundedStepIsWalkable(player, prev, planted))
            return false;
        prev = planted;
    }

    return true;
}

void PlayerbotWalker::NoteLipDestProgress()
{
    float const destDist = _lastGrounded.GetExactDist(_destination);
    if (_lipDestDist <= 0.0f)
        _lipDestDist = destDist;
    else if (destDist + LIP_DEST_PROGRESS_YARDS < _lipDestDist)
    {
        _lipSteps = 0;
        _lipDestDist = destDist;
    }
}

void PlayerbotWalker::NoteLipOrigin()
{
    if (_haveLipOrigin)
        return;

    _lipOrigin = _lastGrounded;
    _haveLipOrigin = true;
}

bool PlayerbotWalker::LeaveFaceExceeded() const
{
    if (!_haveLipOrigin)
        return false;

    return _lastGrounded.GetExactDist2d(_lipOrigin) > LEAVE_FACE_MAX_YARDS;
}

bool PlayerbotWalker::ContourShouldStop(Player* player) const
{
    // Dest is open in front: old poke cap. Leaving a face may take more than eight steps.
    if (StepTowardDestIsLegal(player))
        return _lipSteps >= LIP_MAX_STEPS;

    return LeaveFaceExceeded();
}

bool PlayerbotWalker::TryLeaveFace(Player* player, bool alreadyMoving)
{
    if (StepTowardDestIsLegal(player))
    {
        if (!_destPokeActive)
        {
            _lipSteps = 0;
            _destPokeActive = true;
        }
        if (WalkLegalDestStep(player, alreadyMoving))
            return true;
    }
    else
        _destPokeActive = false;

    return ContinueContour(player, alreadyMoving);
}

bool PlayerbotWalker::StepTowardDestIsLegal(Player* player) const
{
    if (!player)
        return false;

    Position const& feet = _lastGrounded;
    float dx = _destination.GetPositionX() - feet.GetPositionX();
    float dy = _destination.GetPositionY() - feet.GetPositionY();
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len <= _stopDistance + 0.01f)
        return true;
    if (len < 0.01f)
        return true;

    dx /= len;
    dy /= len;
    float const lookDist = std::min(LIP_LOOK_RADIUS, std::max(0.0f, len - _stopDistance));
    return LookAheadIsWalkable(player, feet, dx, dy, lookDist);
}

bool PlayerbotWalker::BuildMmapPath(Player* player, Position const& from, Position const& destination, std::vector<G3D::Vector3>& outPath)
{
    outPath.clear();
    if (!player)
        return false;

    std::vector<AvoidCircle> avoids;
    CollectSpellFocusAvoids(player, 50.0f, avoids);

    PathGenerator generator(player);
    bool const calculated = generator.CalculatePath(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(),
        destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ(), false);
    if (!calculated || !PathIsWalkable(generator))
        return false;

    Movement::PointsArray path = generator.GetPath();
    if (AvoidCircle const* hit = FirstHitAvoid(path, avoids))
    {
        bool detoured = false;
        float dx = destination.GetPositionX() - from.GetPositionX();
        float dy = destination.GetPositionY() - from.GetPositionY();
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.01f)
        {
            float const px = -dy / len;
            float const py = dx / len;
            float const offset = hit->radius + VIA_EXTRA_CLEARANCE;
            std::vector<Position> vias;
            for (int32 i = 0; i < 2; ++i)
            {
                float const sign = i == 0 ? 1.0f : -1.0f;
                float x = hit->x + px * offset * sign;
                float y = hit->y + py * offset * sign;
                Position viaPos;
                if (!PlantFromFeet(player, from, x, y, 0.0f, viaPos))
                    continue;
                vias.push_back(viaPos);
            }

            for (Position const& via : vias)
            {
                if (IsInsideAvoid(via.GetPositionX(), via.GetPositionY(), avoids))
                    continue;

                PathGenerator toVia(player);
                if (!toVia.CalculatePath(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(),
                    via.GetPositionX(), via.GetPositionY(), via.GetPositionZ(), false) || !PathIsWalkable(toVia))
                    continue;
                if (PathHitsAvoid(toVia.GetPath(), avoids))
                    continue;

                PathGenerator toDest(player);
                if (!toDest.CalculatePath(via.GetPositionX(), via.GetPositionY(), via.GetPositionZ(),
                    destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ(), false)
                    || !PathIsWalkable(toDest))
                    continue;
                if (PathHitsAvoid(toDest.GetPath(), avoids))
                    continue;

                path = toVia.GetPath();
                Movement::PointsArray const& second = toDest.GetPath();
                if (second.size() > 1)
                    path.insert(path.end(), second.begin() + 1, second.end());

                TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} walking around {} instead of through it.",
                    player->GetName(), hit->name);
                detoured = true;
                break;
            }
        }

        if (!detoured)
        {
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walk around {} without going through it. The bot is standing still.",
                player->GetName(), hit->name);
            return false;
        }
    }

    if (path.size() < 2)
        return false;

    // Walk from her legal feet, not a navmesh snap that can sit on the face.
    path[0].x = from.GetPositionX();
    path[0].y = from.GetPositionY();
    path[0].z = from.GetPositionZ();

    outPath.assign(path.begin(), path.end());
    return outPath.size() >= 2;
}

bool PlayerbotWalker::TryCommitMmap(Player* player, Position const& from, bool alreadyMoving)
{
    if (!player || !player->GetSession())
        return false;

    std::vector<G3D::Vector3> path;
    if (!BuildMmapPath(player, from, _destination, path))
        return false;

    std::vector<G3D::Vector3> savedPath = _path;
    size_t const savedIndex = _pointIndex;
    float const savedProgress = _segmentProgress;
    _path = std::move(path);
    _pointIndex = 0;
    _segmentProgress = 0.0f;

    if (!MmapLookIsLegal(player))
    {
        _path = std::move(savedPath);
        _pointIndex = savedIndex;
        _segmentProgress = savedProgress;
        return false;
    }

    _contouring = false;
    _startedOnAFace = false;
    _lipSteps = 0;
    _lipDestDist = 0.0f;
    _haveLipOrigin = false;
    _destPokeActive = false;
    _contourDirX = 0.0f;
    _contourDirY = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _lastProgressPos = from;
    _state = State::Moving;

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} starting walk. {} points, length to destination {:.1f} yards.",
        player->GetName(), uint32(_path.size()), from.GetExactDist(_destination));

    Position pose = from;
    if (_path.size() >= 2)
        pose.SetOrientation(Position::NormalizeOrientation(std::atan2(_path[1].y - _path[0].y, _path[1].x - _path[0].x)));
    QueueMove(player, pose, true, !alreadyMoving);
    return true;
}

bool PlayerbotWalker::FindLipSidestep(Player* player, Position& out) const
{
    if (!player)
        return false;

    Position const& feet = _lastGrounded;
    float destDx = _destination.GetPositionX() - feet.GetPositionX();
    float destDy = _destination.GetPositionY() - feet.GetPositionY();
    float const destLen = std::sqrt(destDx * destDx + destDy * destDy);
    if (destLen < 0.01f)
        return false;

    destDx /= destLen;
    destDy /= destLen;

    float const stepLen = HeartbeatStepLen(player);
    if (stepLen < 0.05f)
        return false;

    bool const destStepLegal = StepTowardDestIsLegal(player);
    bool const haveHeading = (_contourDirX * _contourDirX + _contourDirY * _contourDirY) > 0.01f;
    int const rings = int(LIP_LOOK_RADIUS / LIP_LOOK_CELL);

    struct LipCell
    {
        Position pos;
        float score = 0.0f;
        float cross = 0.0f;
    };
    std::vector<LipCell> cells;

    for (int32 ring = 1; ring <= rings; ++ring)
    {
        float const dist = float(ring) * LIP_LOOK_CELL;
        for (int32 dir = 0; dir < LIP_LOOK_DIRECTIONS; ++dir)
        {
            float const ang = float(dir) * (2.0f * float(M_PI)) / float(LIP_LOOK_DIRECTIONS);
            float const c = std::cos(ang);
            float const s = std::sin(ang);

            float const firstDist = std::min(stepLen, dist);
            float fx = feet.GetPositionX() + c * firstDist;
            float fy = feet.GetPositionY() + s * firstDist;
            Position first;
            if (!PlantFromFeet(player, feet, fx, fy, 0.0f, first))
                continue;
            if (!GroundedStepIsWalkable(player, feet, first))
                continue;

            float x = feet.GetPositionX() + c * dist;
            float y = feet.GetPositionY() + s * dist;
            Position cell;
            if (!PlantFromFeet(player, feet, x, y, ang, cell))
                continue;
            if (dist > firstDist + 0.01f && !GroundedStepIsWalkable(player, feet, cell))
                continue;

            float const destDot = c * destDx + s * destDy;
            // Dest look is a face: still take legal cells toward dest (that is often the way out of a nook).
            // Dest may get farther while she walks around. Do not require destDot toward dest.
            if (destStepLegal && destDot < LIP_MIN_DEST_DOT)
                continue;
            if (haveHeading && (c * _contourDirX + s * _contourDirY) < 0.0f)
                continue;
            if (dist < 0.5f)
                continue;

            float const side = 1.0f - std::fabs(destDot);
            // Prefer a couple of yards off the face, not a one-yard poke mmap will rewind.
            float offsetPref = 0.15f;
            if (ring == 2 || ring == 3)
                offsetPref = 1.0f;
            else if (ring >= 4)
                offsetPref = 0.55f;
            float headingBonus = 0.0f;
            if (haveHeading)
                headingBonus = c * _contourDirX + s * _contourDirY;

            LipCell found;
            found.pos = cell;
            found.cross = destDx * s - destDy * c;
            found.score = side * 2.0f + offsetPref * 1.5f + headingBonus * 1.0f;
            found.score += destDot * 0.2f;
            cells.push_back(found);
        }
    }

    if (cells.empty())
        return false;

    if (!haveHeading)
    {
        int32 left = 0;
        int32 right = 0;
        for (LipCell const& cell : cells)
        {
            if (cell.cross >= 0.0f)
                ++left;
            else
                ++right;
        }
        bool const preferLeft = left >= right;
        for (LipCell& cell : cells)
        {
            if ((cell.cross >= 0.0f) == preferLeft)
                cell.score += 2.0f;
        }
    }

    LipCell const* best = &cells[0];
    for (LipCell const& cell : cells)
    {
        if (cell.score > best->score)
            best = &cell;
    }

    out = best->pos;
    return true;
}

void PlayerbotWalker::ApplyContourPath(Player* player, Position const& side, bool alreadyMoving)
{
    NoteLipOrigin();

    _path.clear();
    _path.push_back(G3D::Vector3(_lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ()));
    _path.push_back(G3D::Vector3(side.GetPositionX(), side.GetPositionY(), side.GetPositionZ()));
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _heartbeatMs = 0;
    _logMs = 0;
    _contouring = true;
    _state = State::Moving;

    float dx = side.GetPositionX() - _lastGrounded.GetPositionX();
    float dy = side.GetPositionY() - _lastGrounded.GetPositionY();
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.01f)
    {
        _contourDirX = dx / len;
        _contourDirY = dy / len;
    }

    Position pose = _lastGrounded;
    pose.SetOrientation(Position::NormalizeOrientation(std::atan2(dy, dx)));
    _lastGrounded.SetOrientation(pose.GetOrientation());
    // Already moving: turn onto the ring. Do not stop and start from the same feet.
    QueueMove(player, pose, true, !alreadyMoving);
}

bool PlayerbotWalker::ContinueContour(Player* player, bool alreadyMoving)
{
    if (!player || !player->GetSession())
        return false;

    NoteLipDestProgress();

    if (ContourShouldStop(player))
        return false;

    Position side;
    if (!FindLipSidestep(player, side))
        return false;
    if (side.GetExactDist(_lastGrounded) < 0.5f)
        return false;

    ApplyContourPath(player, side, alreadyMoving);
    if (!FirstGroundedStepIsLegal(player))
    {
        _contouring = false;
        _path.clear();
        return false;
    }

    if (_lipSteps == 0)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} local look found a way around.", player->GetName());

    ++_lipSteps;
    return true;
}

bool PlayerbotWalker::WalkLegalDestStep(Player* player, bool alreadyMoving)
{
    if (!player || !player->GetSession())
        return false;

    NoteLipDestProgress();
    if (ContourShouldStop(player))
        return false;

    Position const& feet = _lastGrounded;
    float dx = _destination.GetPositionX() - feet.GetPositionX();
    float dy = _destination.GetPositionY() - feet.GetPositionY();
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f)
        return false;

    dx /= len;
    dy /= len;
    float const stepLen = std::max(HeartbeatStepLen(player), LIP_LOOK_CELL);
    float x = feet.GetPositionX() + dx * stepLen;
    float y = feet.GetPositionY() + dy * stepLen;
    Position toward;
    if (!PlantFromFeet(player, feet, x, y, Position::NormalizeOrientation(std::atan2(dy, dx)), toward))
        return false;
    if (!GroundedStepIsWalkable(player, feet, toward))
        return false;

    ApplyContourPath(player, toward, alreadyMoving);
    if (!FirstGroundedStepIsLegal(player))
    {
        _contouring = false;
        _path.clear();
        return false;
    }

    ++_lipSteps;
    return true;
}

void PlayerbotWalker::RefuseSteepStep(Player* player, Position const& /*attempted*/)
{
    // Keep FORWARD. A player turns onto the flat beside a face or a wall; they do not stop and start on the same toes.
    if (TryLeaveFace(player, true))
        return;

    FailNoLegalRing(player);
}

void PlayerbotWalker::FailNoLegalRing(Player* player)
{
    if (_state == State::Moving && player && player->GetSession())
        QueueMove(player, _lastGrounded, false, false);

    _state = State::Failed;
    _contouring = false;
    if (player)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no legal ring around this steep ground or this wall. Looking for other work.",
            player->GetName());
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: no legal ring around this steep ground or this wall. Looking for other work.");
}

void PlayerbotWalker::Fail(Player* player, char const* reason)
{
    if (_state == State::Moving && player && player->GetSession())
        QueueMove(player, player->GetPosition(), false, false);

    _state = State::Failed;
    _contouring = false;
    if (player)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} stopped walking: {}.", player->GetName(), reason);
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: walk failed: {}.", reason);
}

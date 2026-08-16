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
#include "Log.h"
#include "MoveSpline.h"
#include "MovementInfo.h"
#include "Object.h"
#include "Opcodes.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotClient.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Unit.h"
#include "UnitDefines.h"
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
    constexpr uint32 LIP_MAX_STEPS = 8;
    constexpr int32 LIP_LOOK_DIRECTIONS = 16;

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
    _lipSteps = 0;
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
    uint32 const savedLipSteps = (_destination.GetExactDist(destination) < 1.0f) ? _lipSteps : 0;
    bool const startFromLastGrounded = _contouring && _state == State::Moving;
    Position from;
    if (startFromLastGrounded)
        from = _lastGrounded;

    Reset();
    _lipSteps = savedLipSteps;

    if (!player || !player->IsInWorld() || !player->GetSession())
    {
        _state = State::Failed;
        return false;
    }

    if (!startFromLastGrounded)
    {
        float x = player->GetPositionX();
        float y = player->GetPositionY();
        float z = player->GetPositionZ();
        player->UpdateAllowedPositionZ(x, y, z);
        from.Relocate(x, y, z, player->GetOrientation());
    }

    _destination = destination;
    _stopDistance = stopDistance;
    _lastGrounded = from;

    if (from.GetExactDist(destination) <= stopDistance)
    {
        _state = State::Arrived;
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} is already in range of the walk destination.", player->GetName());
        return true;
    }

    std::vector<AvoidCircle> avoids;
    CollectSpellFocusAvoids(player, 50.0f, avoids);

    PathGenerator generator(player);
    bool const calculated = generator.CalculatePath(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(),
        destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ(), false);
    if (!calculated || !PathIsWalkable(generator))
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path to the destination (type {}). The bot is standing still.",
            player->GetName(), uint32(generator.GetPathType()));
        _state = State::Failed;
        return false;
    }

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
            Position vias[2];
            for (int32 i = 0; i < 2; ++i)
            {
                float const sign = i == 0 ? 1.0f : -1.0f;
                float x = hit->x + px * offset * sign;
                float y = hit->y + py * offset * sign;
                float z = from.GetPositionZ();
                player->UpdateAllowedPositionZ(x, y, z);
                vias[i].Relocate(x, y, z);
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
            _state = State::Failed;
            return false;
        }
    }

    if (path.size() < 2)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} path has {} point(s); not walking through geometry.",
            player->GetName(), uint32(path.size()));
        _state = State::Failed;
        return false;
    }

    _path.assign(path.begin(), path.end());
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _lastProgressPos = from;
    _contouring = false;

    if (!FirstGroundedStepIsLegal(player))
    {
        if (TryLipDetour(player))
            return true;

        FailNoLegalRing(player);
        return false;
    }

    _lipSteps = 0;
    _state = State::Moving;

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} starting walk. {} points, length to destination {:.1f} yards.",
        player->GetName(), uint32(_path.size()), from.GetExactDist(destination));

    QueueMove(player, from, true, true);
    return true;
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

        float z = next.GetPositionZ();
        player->UpdateAllowedPositionZ(next.GetPositionX(), next.GetPositionY(), z);

        Position grounded;
        grounded.Relocate(next.GetPositionX(), next.GetPositionY(), z, next.GetOrientation());

        // Last grounded feet to this tick's grounded feet. 35° up, short down, including talk.
        GroundedStep const groundedStep = MeasureGroundedStep(_lastGrounded, grounded);
        if (!GroundedStepIsLegal(groundedStep))
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
            Position const destination = _destination;
            float const stopDistance = _stopDistance;
            if (!Start(player, destination, stopDistance))
                return;
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

Position PlayerbotWalker::PeekGroundedStep(Player* player, float distance)
{
    size_t const savedIndex = _pointIndex;
    float const savedProgress = _segmentProgress;
    Position next = Advance(distance);
    _pointIndex = savedIndex;
    _segmentProgress = savedProgress;

    float z = next.GetPositionZ();
    player->UpdateAllowedPositionZ(next.GetPositionX(), next.GetPositionY(), z);

    Position grounded;
    grounded.Relocate(next.GetPositionX(), next.GetPositionY(), z, next.GetOrientation());
    return grounded;
}

bool PlayerbotWalker::FirstGroundedStepIsLegal(Player* player)
{
    if (!player)
        return false;

    Position const first = PeekGroundedStep(player, HeartbeatStepLen(player));
    return GroundedStepIsLegal(MeasureGroundedStep(_lastGrounded, first));
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

    Position best;
    float bestScore = -std::numeric_limits<float>::max();
    bool found = false;
    int const rings = int(LIP_LOOK_RADIUS / LIP_LOOK_CELL);

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
            float fz = feet.GetPositionZ();
            player->UpdateAllowedPositionZ(fx, fy, fz);
            Position first;
            first.Relocate(fx, fy, fz);
            if (!GroundedStepIsLegal(MeasureGroundedStep(feet, first)))
                continue;

            float x = feet.GetPositionX() + c * dist;
            float y = feet.GetPositionY() + s * dist;
            float z = feet.GetPositionZ();
            player->UpdateAllowedPositionZ(x, y, z);
            Position cell;
            cell.Relocate(x, y, z, ang);
            if (dist > firstDist + 0.01f && !GroundedStepIsLegal(MeasureGroundedStep(feet, cell)))
                continue;

            float const destDot = c * destDx + s * destDy;
            if (destDot < LIP_MIN_DEST_DOT)
                continue;

            float const side = 1.0f - std::fabs(destDot);
            float const near = (ring == 1) ? 1.0f : (1.0f - float(ring - 1) / float(rings));
            float const score = destDot * 1.5f + side * 1.0f + near * 0.75f;
            if (!found || score > bestScore)
            {
                best = cell;
                bestScore = score;
                found = true;
            }
        }
    }

    if (!found)
        return false;

    out = best;
    return true;
}

bool PlayerbotWalker::TryLipDetour(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    if (_lipSteps >= LIP_MAX_STEPS)
        return false;

    Position side;
    if (!FindLipSidestep(player, side))
        return false;

    _path.clear();
    _path.push_back(G3D::Vector3(_lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ()));
    _path.push_back(G3D::Vector3(side.GetPositionX(), side.GetPositionY(), side.GetPositionZ()));
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _lastProgressPos = _lastGrounded;
    _contouring = true;

    if (!FirstGroundedStepIsLegal(player))
    {
        _contouring = false;
        _path.clear();
        return false;
    }

    ++_lipSteps;
    _state = State::Moving;
    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} local look found a way around.", player->GetName());
    QueueMove(player, _lastGrounded, true, true);
    return true;
}

void PlayerbotWalker::RefuseSteepStep(Player* player, Position const& /*attempted*/)
{
    QueueMove(player, _lastGrounded, false, false);

    if (TryLipDetour(player))
        return;

    FailNoLegalRing(player);
}

void PlayerbotWalker::FailNoLegalRing(Player* player)
{
    _state = State::Failed;
    _contouring = false;
    if (player)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no legal ring around this steep ground. Looking for other work.",
            player->GetName());
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: no legal ring around this steep ground. Looking for other work.");
}

void PlayerbotWalker::Fail(Player* player, char const* reason)
{
    if (_state == State::Moving && player && player->GetSession())
        QueueMove(player, player->GetPosition(), false, false);

    _state = State::Failed;
    if (player)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} stopped walking: {}.", player->GetName(), reason);
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: walk failed: {}.", reason);
}

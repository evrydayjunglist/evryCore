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
#include "MapDefines.h"
#include "MoveSpline.h"
#include "MovementInfo.h"
#include "MovementTypedefs.h"
#include "Object.h"
#include "Opcodes.h"
#include "PathGenerator.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "PlayerbotClient.h"
#include "PlayerbotLogDetail.h"
#include "PlayerbotPathSearch.h"
#include "PlayerbotServerMovement.h"
#include "PlayerbotStandSpotMemory.h"
#include "PlayerbotUpdateCost.h"
#include "PlayerbotWalkMapEscape.h"
#include "PlayerbotWalkMapServerWorld.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Unit.h"
#include "UnitDefines.h"
#include "VMapFactory.h"
#include "VMapManager.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr uint32 HEARTBEAT_INTERVAL_MS = 100;
    constexpr uint32 STUCK_TIMEOUT_MS = 3000;
    constexpr uint32 HEARTBEAT_LOG_INTERVAL_MS = 1000;
    constexpr uint32 OWNING_CLIENT_SYNC_TIMEOUT_MS = 2000;
    constexpr float OWNING_CLIENT_SYNC_DISTANCE = 0.75f;
    constexpr uint32 REFUSED_PATH_TYPES = PATHFIND_NOPATH | PATHFIND_SHORTCUT | PATHFIND_NOT_USING_PATH;
    constexpr float SPELL_FOCUS_AVOID_RADIUS = 2.5f;
    constexpr float VIA_EXTRA_CLEARANCE = 1.0f;
    constexpr float MAX_WALKABLE_SLOPE_DEGREES = PlayerbotWalker::MaxWalkableSlopeDegrees;
    constexpr float MAX_DOWN_STEP_YARDS = PlayerbotWalker::MaxDownStepYards;
    // Half a heartbeat at run speed. A step shorter than this is judged over this much ground.
    constexpr float MIN_SLOPE_RUN_YARDS = 0.35f;
    // Small patch around her feet. Living tiles mark 55° as ordinary ground; mmap walks her into a face a player would step around.
    constexpr float LIP_LOOK_RADIUS = 4.0f;
    constexpr float LIP_LOOK_CELL = 1.0f;
    constexpr float LIP_MIN_DEST_DOT = -0.15f;
    constexpr float LIP_DEST_PROGRESS_YARDS = 1.0f;
    constexpr uint32 LIP_MAX_STEPS = 8;
    constexpr int32 LIP_LOOK_DIRECTIONS = 16;
    constexpr float NORMAL_JUMP_VERTICAL_SPEED = 7.955547f;
    constexpr float JUMP_ARC_SAMPLE_SECONDS = 0.025f;
    constexpr float JUMP_FLOOR_SEARCH_ABOVE_FEET = 0.5f;
    constexpr float JUMP_FOOT_CLEARANCE = 0.05f;
    constexpr float JUMP_ASCENT_GROUND_TOLERANCE = 0.05f;
    constexpr float JUMP_MAX_LANDING_RISE = 1.25f;
    constexpr float JUMP_MAX_LANDING_DROP = 2.0f;
    constexpr float JUMP_MIN_CLEARANCE_PAST_FACE = 0.5f;
    constexpr uint32 FLIGHT_SAMPLE_MS = 25;
    // The longest fall followed before giving up on finding a floor under her.
    constexpr uint32 FLIGHT_MAX_MS = 20000;
    // How far the server's copy of her may trail the arc point she last sent before the arc is treated as lost.
    constexpr float FLIGHT_MIN_ALLOWED_DRIFT = 10.0f;
    constexpr float FLIGHT_DRIFT_SECONDS = 0.3f;
    constexpr float RECOVERY_CONNECTIVITY_PROBE_YARDS[] = { 60.0f, 120.0f };
    constexpr int32 RECOVERY_CONNECTIVITY_DIRECTIONS = 8;
    constexpr float RECOVERY_PROBE_ENDPOINT_YARDS = 8.0f;
    // How far around her feet she maps the ground when a walk is refused and the local look cannot get her round. She maps
    // the small circle first, and the full one only when the small one has no way round and ground she can walk carries
    // on past its edge. Most obstacles are passed inside the small circle, which holds about a ninth of the floors.
    constexpr float WAY_ROUND_FIRST_YARDS = 20.0f;
    constexpr float WAY_ROUND_YARDS = 60.0f;
    // A spot is only worth walking to when it is at least this much closer to where she is going.
    constexpr float WAY_ROUND_MIN_GAIN_YARDS = 5.0f;
    // Ways round to try, best first, in case her first step refuses one of them.
    constexpr size_t WAY_ROUND_WAYS = 3;
    // When nothing she can reach is closer, these are the ways out of this ground she asks the navmesh about.
    constexpr size_t WAY_ROUND_WAYS_OUT = 6;
    constexpr float WAY_ROUND_WAYS_OUT_SPREAD_YARDS = 15.0f;
    // A way round is walked on ground the map judged from its own grid, and her heartbeats land between those spots.
    // When one of them is refused she walks the rest of the way from where she is, this many times.
    constexpr uint32 WAY_ROUND_MAX_REPAIRS = 3;
    constexpr float WAY_ROUND_LAYER_YARDS = 1.0f;
    constexpr size_t WAY_ROUND_MAX_SPOTS = 400000;
    // Her heartbeat step is the distance between neighbouring spots, kept inside these bounds whatever her speed is.
    constexpr float WAY_ROUND_MIN_SPACING = 0.5f;
    constexpr float WAY_ROUND_MAX_SPACING = 2.0f;
    // World-thread time one tick may spend mapping the ground for one bot.
    constexpr std::chrono::milliseconds WAY_ROUND_SLICE{ 5 };
    constexpr size_t WAY_ROUND_SPOTS_PER_CLOCK_CHECK = 8;
    // The map has to finish inside this, counted from her first turn, or she gives the walk up instead of standing there.
    constexpr uint32 WAY_ROUND_TIMEOUT_MS = 20000;
    // However many bots are looking at once, this is all the world-thread time one tick spends mapping ground for them.
    // The bots at the front of the line share it; the rest wait for their turn.
    constexpr std::chrono::milliseconds WAY_ROUND_TICK_BUDGET{ 10 };
    std::chrono::steady_clock::duration WayRoundSpentThisTick = std::chrono::steady_clock::duration::zero();
    PlayerbotWayRoundTurns WayRoundTurns;
    // Stand spots picked in the last few seconds, by bot, so the same question from the same feet is not asked again.
    PlayerbotStandSpotMemory StandSpots;
    // A navmesh route is never shorter than the straight line from her feet to its end. Its ends sit on the navmesh
    // rather than exactly at her feet and the stand spot, so the straight line is taken this much shorter before a side
    // is judged unable to beat the best route found.
    constexpr float STAND_SPOT_ROUTE_SLACK_YARDS = 2.0f;
    // Stopping short again near an earlier short stop on the same approach, and not a yard closer, means mmap does not lead closer.
    constexpr float SHORT_STOP_REPEAT_YARDS = 5.0f;
    constexpr float SHORT_STOP_PROGRESS_YARDS = 1.0f;
    constexpr size_t SHORT_STOP_MEMORY = 8;
    // The headroom ray starts this far above her feet, so the floor she stands on is not the thing over her head.
    constexpr float HEADROOM_FROM_FEET_YARDS = 0.2f;
    // Standing up out of a pocket looks for the surface over her this far above the top of her body.
    constexpr float STAND_UP_SEARCH_ABOVE_BODY_YARDS = 1.0f;
    // Walking off an edge when she cannot step anywhere: the headings she tries, how finely she feels for the edge, how
    // much health the fall must leave her, and how many of the shortest drops are asked for a navmesh route onward.
    constexpr int32 WALK_OFF_DIRECTIONS = 16;
    constexpr float WALK_OFF_EDGE_STEP_YARDS = 0.25f;
    constexpr uint32 WALK_OFF_EDGE_REFINE_STEPS = 4;
    constexpr uint32 WALK_OFF_MIN_HEALTH_PCT_AFTER = 50;
    constexpr size_t WALK_OFF_ROUTE_CHECKS = 4;
    // After a step down steeper than she may climb back, how far along the route she looks for a step she may not take.
    constexpr float STEP_DOWN_LOOK_AHEAD_YARDS = 6.0f;
    // Player::HandleFall does no damage for a fall shorter than this.
    constexpr float FALL_DAMAGE_MIN_YARDS = 14.57f;

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
        // At a corner of a path, or on its last stub, one heartbeat covers only a few centimetres of ground. A bump
        // there is not a cliff, so the slope of a step that short is read over half a heartbeat.
        step.degrees = std::atan2(std::fabs(step.rise), std::max(step.run, MIN_SLOPE_RUN_YARDS)) * (180.0f / float(M_PI));
        return step;
    }

    bool GroundedStepIsLegal(GroundedStep const& step)
    {
        if (step.rise > 0.0f)
            return step.degrees <= MAX_WALKABLE_SLOPE_DEGREES;

        return -step.rise <= MAX_DOWN_STEP_YARDS;
    }

    enum class StepWorldCollision
    {
        None,
        Static,
        Dynamic,
        InvalidPosition
    };

    // A segment with no sideways length is no wall for a step. Pass alongOnly false to ray straight up or down as well.
    StepWorldCollision GetSegmentWorldCollision(Player const* player, Position const& from, Position const& to, float heightOffset,
        bool alongOnly = true)
    {
        PlayerbotCostTimer const cost(PlayerbotCostStep::CollisionRay);
        if (!player || !player->IsInWorld())
            return StepWorldCollision::None;

        Map* map = player->FindMap();
        if (!map)
            return StepWorldCollision::None;

        float const fromX = from.GetPositionX();
        float const fromY = from.GetPositionY();
        float const fromZ = from.GetPositionZ();
        float destX = to.GetPositionX();
        float destY = to.GetPositionY();
        float destZ = to.GetPositionZ();
        if (!Trinity::IsValidMapCoord(fromX, fromY, fromZ) || !Trinity::IsValidMapCoord(destX, destY, destZ))
            return StepWorldCollision::InvalidPosition;

        float const dx = destX - fromX;
        float const dy = destY - fromY;
        float const dz = alongOnly ? 0.0f : destZ - fromZ;
        if ((dx * dx + dy * dy + dz * dz) < 0.0001f)
            return StepWorldCollision::None;

        float hitX = destX;
        float hitY = destY;
        float hitZ = destZ;
        if (VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(
            PhasingHandler::GetTerrainMapId(player->GetPhaseShift(), player->GetMapId(), map->GetTerrain(), fromX, fromY),
            fromX, fromY, fromZ + heightOffset,
            destX, destY, destZ + heightOffset,
            hitX, hitY, hitZ, -0.5f))
            return StepWorldCollision::Static;

        if (map->getObjectHitPos(player->GetPhaseShift(),
            fromX, fromY, fromZ + heightOffset,
            destX, destY, destZ + heightOffset,
            hitX, hitY, hitZ, -0.5f))
            return StepWorldCollision::Dynamic;

        return StepWorldCollision::None;
    }

    // Same rays as WorldObject::MovePositionToFirstCollision: static vmap, then dynamic gameobject.
    // Chest height, not feet, so floors, ramps, doorways, and stairs are not walls. Do not relocate.
    StepWorldCollision GetStepWorldCollision(Player const* player, Position const& from, Position const& to)
    {
        float const halfHeight = player ? player->GetCollisionHeight() * 0.5f : 0.0f;
        return GetSegmentWorldCollision(player, from, to, halfHeight);
    }

    bool JumpSegmentIsClear(Player const* player, Position const& from, Position const& to)
    {
        if (!player)
            return false;

        float const dx = to.GetPositionX() - from.GetPositionX();
        float const dy = to.GetPositionY() - from.GetPositionY();
        float const horizontal = std::sqrt(dx * dx + dy * dy);
        if (horizontal < 0.0001f)
            return false;

        float const collisionHeight = player->GetCollisionHeight();
        float const heights[] =
        {
            std::min(0.1f, collisionHeight),
            std::min(collisionHeight * 0.25f, collisionHeight),
            collisionHeight * 0.5f,
            std::max(collisionHeight * 0.5f, collisionHeight - 0.1f)
        };
        float const radius = std::max(0.0f, player->GetBoundingRadius());
        float const sideX = -dy / horizontal;
        float const sideY = dx / horizontal;
        float const lateralOffsets[] = { -radius, 0.0f, radius };

        for (float lateral : lateralOffsets)
        {
            Position shiftedFrom = from;
            shiftedFrom.Relocate(from.GetPositionX() + sideX * lateral, from.GetPositionY() + sideY * lateral,
                from.GetPositionZ(), from.GetOrientation());
            Position shiftedTo = to;
            shiftedTo.Relocate(to.GetPositionX() + sideX * lateral, to.GetPositionY() + sideY * lateral,
                to.GetPositionZ(), to.GetOrientation());

            for (float height : heights)
                if (GetSegmentWorldCollision(player, shiftedFrom, shiftedTo, height) != StepWorldCollision::None)
                    return false;
        }

        return true;
    }

    // The top of her body, from one arc point to the next, meets something above her.
    bool FlightHeadHits(Player const* player, Position const& from, Position const& to)
    {
        if (!player)
            return false;

        float const headHeight = std::max(0.1f, player->GetCollisionHeight());
        return GetSegmentWorldCollision(player, from, to, headHeight, false) != StepWorldCollision::None;
    }

    // Her whole body fits standing on these feet: straight up from just above them to the top of her body meets no
    // model and no game object. A player's client does not let them into a pocket under something lower than that; it
    // stands them on top of it or stops them.
    StepWorldCollision GetHeadroomCollision(Player const* player, Position const& feet)
    {
        if (!player)
            return StepWorldCollision::None;

        float const bodyTop = std::max(HEADROOM_FROM_FEET_YARDS + 0.1f, player->GetCollisionHeight());
        Position top = feet;
        top.Relocate(feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ() + bodyTop - HEADROOM_FROM_FEET_YARDS);
        return GetSegmentWorldCollision(player, feet, top, HEADROOM_FROM_FEET_YARDS, false);
    }

    bool HasHeadroom(Player const* player, Position const& feet)
    {
        return GetHeadroomCollision(player, feet) == StepWorldCollision::None;
    }

    // The damage Player::HandleFall would do to her for landing this far below where she left the ground, worked out the
    // same way from her auras and the server's fall damage rate.
    uint32 PredictedFallDamage(Player const* player, float drop)
    {
        if (!player || drop < FALL_DAMAGE_MIN_YARDS || player->IsGameMaster() || player->HasAuraType(SPELL_AURA_HOVER)
            || player->HasAuraType(SPELL_AURA_FEATHER_FALL) || player->HasAuraType(SPELL_AURA_FLY)
            || player->IsImmunedToDamage(SPELL_SCHOOL_MASK_NORMAL))
            return 0;

        float const safeFall = float(player->GetTotalAuraModifier(SPELL_AURA_SAFE_FALL));
        float const damagePct = 0.018f * (drop - safeFall) - 0.2426f;
        if (damagePct <= 0.0f)
            return 0;

        float damage = float(uint32(damagePct * player->GetMaxHealth() * sWorld->getRate(RATE_DAMAGE_FALL)));
        damage *= player->GetTotalAuraMultiplier(SPELL_AURA_MODIFY_FALL_DAMAGE_PCT);
        return std::min(uint32(damage), uint32(player->GetMaxHealth()));
    }

    bool GroundedStepIsWalkable(Player const* player, Position const& from, Position const& to)
    {
        if (!GroundedStepIsLegal(MeasureGroundedStep(from, to)))
            return false;

        if (GetStepWorldCollision(player, from, to) != StepWorldCollision::None)
            return false;

        return HasHeadroom(player, to);
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
        return PlayerbotWalker::PlantAt(player, x, y, lastGroundedZ + maxRise, outZ);
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
    // She is already standing while she looks around; there is nothing to stop.
    if (_state == State::LookingForAWayRound)
    {
        ResetNow();
        return;
    }

    if (_state == State::Jumping)
    {
        if (player && player->IsAlive() && player->IsInWorld() && !player->IsBeingTeleported())
        {
            // A player cannot stop in mid-air. Finish the validated arc, then stop on the landing.
            _stopAfterJump = true;
            return;
        }

        ResetNow();
        return;
    }

    // Stop where her last step put her. The server's position can still be a heartbeat behind that step.
    if (_state == State::Moving && player && player->GetSession())
        QueueMove(player, _lastGrounded, false, false);

    ResetNow();
}

void PlayerbotWalker::StopAtFeet(Player* player)
{
    if (!player || !player->GetSession())
        return;

    MovementInfo info;
    PlayerbotClient::FillClientMovementInfo(player, player->GetPosition(), info);
    PlayerbotClient::QueueMovement(player->GetSession(), CMSG_MOVE_STOP, info);
}

void PlayerbotWalker::Reset()
{
    if (_state == State::Jumping)
    {
        _stopAfterJump = true;
        return;
    }

    ResetNow();
}

void PlayerbotWalker::Abandon()
{
    ResetNow();
}

void PlayerbotWalker::HoldForRoot(Player* player)
{
    if (_state == State::Jumping)
    {
        if (!player || !player->IsAlive() || !player->IsInWorld() || player->IsBeingTeleported())
        {
            ResetNow();
            return;
        }

        // Nobody stops in mid-air. She lands, then stays put.
        _stopAfterJump = true;
        uint32 const nowMs = std::min(_jumpElapsedMs, _jump.DurationMs);
        if (_jump.SidewaysStopMs <= nowMs || _jump.CeilingMs <= nowMs)
            return;

        JumpPlan held = _jump;
        held.SidewaysStopMs = nowMs;
        char const* reason = "the arc could not be followed";
        if (!SampleFlight(player, held, nowMs, reason))
        {
            PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} was rooted in the air and keeps the arc she had: {}.",
                player->GetName(), reason);
            return;
        }

        _jump = held;
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} was rooted in the air; falling straight down to ({:.2f}, {:.2f}, {:.2f}).",
            player->GetName(), _jump.Landing.GetPositionX(), _jump.Landing.GetPositionY(), _jump.Landing.GetPositionZ());
        return;
    }

    if (_state == State::Moving && player && player->IsAlive() && player->GetSession())
        QueueMove(player, _lastGrounded, false, false);

    ResetNow();
}

Position PlayerbotWalker::ClientFeet(Player const* player) const
{
    if (_state == State::Moving || _state == State::Jumping || _state == State::AwaitingClientSync)
        return _lastGrounded;

    return player ? player->GetPosition() : Position();
}

bool PlayerbotWalker::StartKnockback(Player* player, Position const& feet, float directionX, float directionY,
    float horizontalSpeed, float verticalSpeed, char const*& reason)
{
    if (!FlightMovementIsAllowed(player, reason))
    {
        ResetNow();
        return false;
    }

    JumpPlan plan;
    plan.Launch = feet;
    float const length = std::sqrt(directionX * directionX + directionY * directionY);
    if (length > 0.0001f && horizontalSpeed > 0.0f)
    {
        plan.DirectionX = directionX / length;
        plan.DirectionY = directionY / length;
        plan.Trajectory.HorizontalSpeed = horizontalSpeed;
    }
    // The packet's vertical speed is negative going up.
    plan.Trajectory.VerticalSpeed = -verticalSpeed;
    plan.Trajectory.Gravity = Movement::gravity;
    plan.Trajectory.TerminalVelocity = player->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FALLING_SLOW)
        ? PLAYERBOT_FEATHER_FALL_TERMINAL_SPEED
        : PLAYERBOT_TERMINAL_FALL_SPEED;
    plan.Knockback = true;

    if (!SampleFlight(player, plan, 0, reason))
    {
        ResetNow();
        return false;
    }

    ResetNow();
    _jump = plan;
    _lastGrounded = feet;
    _jumpMapId = player->GetMapId();
    _skipNextJumpDiff = true;
    _state = State::Jumping;

    char const* ending = plan.EndsInWater ? "touches deep water" : plan.EndsBelowWorld ? "leaves the world" : "lands";
    TC_LOG_INFO(PLAYERBOTS_LOG,
        "mod-playerbots: {} was knocked back from ({:.2f}, {:.2f}, {:.2f}) at {:.1f} yards per second sideways and {:.1f} up; she {} at ({:.2f}, {:.2f}, {:.2f}) after {} ms.",
        player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(), plan.Trajectory.HorizontalSpeed,
        plan.Trajectory.VerticalSpeed, ending, plan.Landing.GetPositionX(), plan.Landing.GetPositionY(), plan.Landing.GetPositionZ(),
        plan.DurationMs);
    return true;
}

void PlayerbotWalker::ResetNow()
{
    _state = State::Idle;
    _path.clear();
    _pathType = 0;
    _shortStops.clear();
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _destination.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _stopDistance = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _owningClientSyncMs = 0;
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
    _faceRecovery.Reset();
    _lastGroundedStepFailure = GroundedStepFailure::None;
    _lastRefusedStep.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _lastRequestedStep.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _recoveryGoal = {};
    _jump = {};
    _jumpElapsedMs = 0;
    _jumpHeartbeatMs = 0;
    _jumpMapId = 0;
    _stopAfterJump = false;
    _skipNextJumpDiff = false;
    ClearWayRound();
    _walkingAWayRound = false;
    _lookedForAWayRound = false;
    _wayRoundFirstStepRefused = false;
    _failedAtHerFeet = false;
    _haveApproachRefusal = false;
    _haveEpisodeRefusal = false;
}

void PlayerbotWalker::ClearWayRound()
{
    EndWayRoundTurn(PlayerbotWayRoundEnd::Abandoned);
    _wayRoundJoinReason = nullptr;
    _wayRoundMap.reset();
    _wayRoundWaysOut.clear();
    _wayRoundProbe = 0;
    _wayRoundTarget = -1;
    _wayRoundRepairs = 0;
    _wayRoundPhase = WayRoundPhase::Mapping;
    _wayRoundMs = 0;
    _wayRoundMapId = 0;
    _wayRoundWaitMs = 0;
    _wayRoundWork = std::chrono::steady_clock::duration::zero();
    _wayRoundHadATurn = false;
}

void PlayerbotWalker::EndWayRoundTurn(PlayerbotWayRoundEnd end)
{
    if (!_wayRoundTicket)
        return;
    WayRoundTurns.NoteEnd(end, _wayRoundWaitMs,
        uint64(std::chrono::duration_cast<std::chrono::microseconds>(_wayRoundWork).count()),
        _wayRoundMap ? _wayRoundMap->Spots().size() : 0);
    WayRoundTurns.Leave(_wayRoundTicket);
    _wayRoundTicket = 0;
}

void PlayerbotWalker::JoinWayRoundLine(Player* player)
{
    if (!WantsToLookForAWayRound())
        return;
    _wayRoundTicket = WayRoundTurns.Join(_wayRoundJoinReason);
    _wayRoundJoinReason = nullptr;
    PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} waits for her turn to map the ground; {} bot(s) are in line ahead of her.",
        player->GetName(), uint32(WayRoundTurns.Waiting() - 1));
}

void PlayerbotWalker::NoteWentElsewhereInsteadOfAWayRound()
{
    WayRoundTurns.NoteWentElsewhere();
}

// One world tick's mapping time is shared by the bots at the front of the line looking for a way round.
void PlayerbotWalker::BeginWorldTick()
{
    WayRoundSpentThisTick = std::chrono::steady_clock::duration::zero();
    WayRoundTurns.BeginTick();
}

std::string PlayerbotWalker::DescribeWayRoundLooksAndClear()
{
    return WayRoundTurns.DescribeWindowAndClear();
}

bool PlayerbotWalker::PickApproachPosition(Player* player, WorldObject const* target, float standDistance, Position& out,
    std::vector<StandSpotLook>* look)
{
    PlayerbotCostTimer const cost(PlayerbotCostStep::Approach);
    if (!player || !target)
        return false;

    PlayerbotStandSpotMemory::Question question;
    question.Bot = player->GetGUID().GetCounter();
    question.TargetLow = target->GetGUID().GetRawValue(0);
    question.TargetHigh = target->GetGUID().GetRawValue(1);
    question.MapId = player->GetMapId();
    question.StandDistance = standDistance;
    question.Feet = { player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() };
    question.Target = { target->GetPositionX(), target->GetPositionY(), target->GetPositionZ() };
    uint32 const nowMs = GameTime::GetGameTimeMS();

    // A look for a log line wants every side, so it always asks the navmesh.
    PlayerbotStandSpotMemory::Answer answer;
    if (!look && StandSpots.Recall(question, nowMs, answer))
    {
        if (!answer.Found)
            return false;
        out.Relocate(answer.X, answer.Y, answer.Z, answer.Orientation);
        return true;
    }

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

    struct Side
    {
        float X;
        float Y;
        float Z;
        float Angle;
        float FromFeet;
    };
    std::array<Side, std::size(offsets)> sides;
    std::size_t sideCount = 0;

    for (float offset : offsets)
    {
        float const angle = Position::NormalizeOrientation(fromTargetToPlayer + offset);
        float x = target->GetPositionX() + std::cos(angle) * standDistance;
        float y = target->GetPositionY() + std::sin(angle) * standDistance;
        float z = target->GetPositionZ();
        player->UpdateAllowedPositionZ(x, y, z);

        auto const avoid = std::find_if(avoids.begin(), avoids.end(), [x, y](AvoidCircle const& circle) { return CircleContains(circle, x, y); });
        if (avoid != avoids.end())
        {
            if (look)
            {
                StandSpotLook& spot = look->emplace_back();
                spot.What = StandSpotLook::Outcome::InSpellFocus;
                spot.SpellFocus = avoid->name;
            }
            continue;
        }

        sides[sideCount++] = { x, y, z, angle, player->GetExactDist(x, y, z) };
    }

    // Nearest sides first. A route is never shorter than the straight line to its end, so once a clear route is found
    // no shorter than the straight line to the next side, no side left can have a shorter route and none is asked about.
    // A look for a log line keeps the old order and asks about every side.
    if (!look)
        std::stable_sort(sides.begin(), sides.begin() + sideCount, [](Side const& a, Side const& b) { return a.FromFeet < b.FromFeet; });

    Position best;
    float bestLen = std::numeric_limits<float>::max();
    bool found = false;
    bool foundClear = false;
    uint32 asked = 0;
    uint32 notNeeded = 0;

    for (std::size_t i = 0; i < sideCount; ++i)
    {
        Side const& side = sides[i];
        if (!look && foundClear && side.FromFeet - STAND_SPOT_ROUTE_SLACK_YARDS >= bestLen)
        {
            notNeeded = uint32(sideCount - i);
            break;
        }

        // Asking about eight sides of the target keeps the short search, so a far target stays cheap to look at: the work
        // finders ask this of many targets in one pick. A side the short search could not settle is still a side she may
        // reach: it found the route and a short path could not hold it, or it ran out of search nodes on the way. The walk
        // itself asks the long search for that route, and a target no route reaches fails that walk the usual way.
        ++asked;
        PathGenerator generator(player, NavMeshChoice::PlayerBody);
        PlayerbotCostTimer const cost(PlayerbotCostStep::NavmeshRoute);
        if (!generator.CalculatePath(side.X, side.Y, side.Z, false))
        {
            if (look)
                look->emplace_back().What = StandSpotLook::Outcome::NotOnMap;
            continue;
        }

        PathSearchReport const& search = generator.GetSearchReport();
        bool const usable = PathIsWalkable(generator) || PathSearchFoundTooLongARoute(search) || PathSearchRanOutOfRoom(search);
        if (look)
        {
            StandSpotLook& spot = look->emplace_back();
            spot.What = usable ? StandSpotLook::Outcome::Usable : StandSpotLook::Outcome::Refused;
            spot.PathType = uint32(generator.GetPathType());
            spot.Points = uint32(generator.GetPath().size());
            spot.PlayerNavMesh = generator.UsedPlayerNavMesh();
            spot.Search = search;
        }
        if (!usable)
            continue;

        bool const hits = PathHitsAvoid(generator.GetPath(), avoids);
        float const len = generator.GetPathLength();
        if (!hits)
        {
            if (!foundClear || len < bestLen)
            {
                best.Relocate(side.X, side.Y, side.Z, Position::NormalizeOrientation(side.Angle + float(M_PI)));
                bestLen = len;
                foundClear = true;
                found = true;
            }
        }
        else if (!foundClear && (!found || len < bestLen))
        {
            best.Relocate(side.X, side.Y, side.Z, Position::NormalizeOrientation(side.Angle + float(M_PI)));
            bestLen = len;
            found = true;
        }
    }

    StandSpots.NoteSides(asked, notNeeded);
    answer = {};
    answer.Found = found;
    if (found)
    {
        answer.X = best.GetPositionX();
        answer.Y = best.GetPositionY();
        answer.Z = best.GetPositionZ();
        answer.Orientation = best.GetOrientation();
    }
    StandSpots.Remember(question, nowMs, answer);

    if (!found)
        return false;

    out = best;
    return true;
}

void PlayerbotWalker::ForgetStandSpots(Player const* player)
{
    if (player)
        StandSpots.Forget(player->GetGUID().GetCounter());
}

std::string PlayerbotWalker::DescribeStandSpotsAndClear()
{
    return StandSpots.DescribeWindowAndClear();
}

bool PlayerbotWalker::Start(Player* player, Position const& destination, float stopDistance, PlayerbotRecoveryGoal const& goal)
{
    _startMetBadPlace = false;
    if (!player || !player->IsInWorld() || !player->GetSession())
    {
        _state = State::Failed;
        return false;
    }

    // Finish the current client jump before accepting another ground path.
    if (_state == State::Jumping)
        return true;

    _lastWalkDestination = destination;
    _lastWalkDestinationMapId = player->GetMapId();
    _hasLastWalkDestination = true;

    bool const sameDest = _destination.GetExactDist(destination) < 1.0f;
    bool const sameGoal = !goal.Empty() && !_recoveryGoal.Empty() && goal == _recoveryGoal;
    bool const sameWalk = sameDest && (goal.Empty() ? _recoveryGoal.Empty() : sameGoal);
    // Already walking this dest. Do not rebuild mmap from the same feet.
    if (_state == State::Moving && sameWalk)
        return true;
    // Standing still looking for a way round to this same dest. Let her finish looking.
    if (_state == State::LookingForAWayRound && sameWalk)
        return true;

    bool const preserveEpisode = _faceRecovery.Active() && (sameGoal || (goal.Empty() && sameDest));
    bool const preserveCourse = preserveEpisode && sameDest;
    uint32 const savedLipSteps = preserveCourse ? _lipSteps : 0;
    float const savedLipDestDist = preserveCourse ? _lipDestDist : 0.0f;
    Position const savedLipOrigin = preserveEpisode ? _lipOrigin : Position();
    bool const savedHaveLipOrigin = preserveEpisode && _haveLipOrigin;
    bool const savedDestPokeActive = preserveCourse && _destPokeActive;
    float const savedCx = preserveCourse ? _contourDirX : 0.0f;
    float const savedCy = preserveCourse ? _contourDirY : 0.0f;
    PlayerbotFaceRecovery const savedFaceRecovery = preserveEpisode ? _faceRecovery : PlayerbotFaceRecovery();
    GroundedStepFailure const savedStepFailure = preserveEpisode ? _lastGroundedStepFailure : GroundedStepFailure::None;
    Position const savedRefusedStep = preserveEpisode ? _lastRefusedStep : Position();
    bool const savedHaveEpisodeRefusal = preserveEpisode && _haveEpisodeRefusal;
    Position const savedEpisodeFeet = _episodeFeet;
    Position const savedEpisodeRefused = _episodeRefused;
    // A new stand spot for the same goal is still the same approach, so it keeps the short stops.
    bool const sameApproach = sameGoal || (goal.Empty() && _recoveryGoal.Empty() && sameDest);
    bool const savedLookedForAWayRound = sameApproach && _lookedForAWayRound;
    bool const savedHaveApproachRefusal = sameApproach && _haveApproachRefusal;
    Position const savedApproachFeet = _approachFeet;
    Position const savedApproachRefused = _approachRefused;
    std::vector<Position> savedShortStops;
    if (sameApproach)
        savedShortStops.swap(_shortStops);

    Reset();
    _shortStops.swap(savedShortStops);
    _haveApproachRefusal = savedHaveApproachRefusal;
    _approachFeet = savedApproachFeet;
    _approachRefused = savedApproachRefused;
    _lookedForAWayRound = savedLookedForAWayRound;
    _lipSteps = savedLipSteps;
    _lipDestDist = savedLipDestDist;
    _lipOrigin = savedLipOrigin;
    _haveLipOrigin = savedHaveLipOrigin;
    _destPokeActive = savedDestPokeActive;
    _contourDirX = savedCx;
    _contourDirY = savedCy;
    _faceRecovery = savedFaceRecovery;
    _lastGroundedStepFailure = savedStepFailure;
    _lastRefusedStep = savedRefusedStep;
    _haveEpisodeRefusal = savedHaveEpisodeRefusal;
    _episodeFeet = savedEpisodeFeet;
    _episodeRefused = savedEpisodeRefused;
    _recoveryGoal = goal;

    float x = player->GetPositionX();
    float y = player->GetPositionY();
    float z = player->GetPositionZ();
    player->UpdateAllowedPositionZ(x, y, z);
    Position from;
    from.Relocate(x, y, z, player->GetOrientation());
    StandUpOutOfPocket(player, from);

    _destination = destination;
    _stopDistance = stopDistance;
    _lastGrounded = from;

    if (from.GetExactDist(destination) <= stopDistance)
    {
        _state = State::Arrived;
        ClearFaceRecovery();
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} is already in range of the walk destination.", player->GetName());
        return true;
    }

    std::vector<G3D::Vector3> path;
    MmapPathEvidence mmapEvidence;
    if (!BuildMmapPath(player, from, destination, path, &mmapEvidence))
    {
        _lastProgressPos = from;
        _lastRequestedStep = destination;
        BeginFaceRecovery(player, GroundedStepFailure::NoPath, from);
        LogRecoveryMmap(player, "path rejected", mmapEvidence);
        LogStartConnectivity(player, from);
        if (TryLeaveFace(player, false))
        {
            PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} has no walkable path from these feet. Walking off this face, then mmap.",
                player->GetName());
            return true;
        }

        if (BeginWayRound(player, "the navmesh had no route from her feet"))
            return true;

        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no walkable path to the destination. The bot is standing still.",
            player->GetName());
        _state = State::Failed;
        return false;
    }

    // This route goes past a place where a walk of hers failed after every way round, going the same way. It would fail
    // there again, so she does not walk it and her brain picks other work. A walk back to her body or after her party
    // leader has no other work to pick instead, so it walks and tries, and so does a walk in a fight, which is to the
    // monster she is fighting.
    if (goal.Kind != PlayerbotRecoveryGoalKind::Corpse && goal.Kind != PlayerbotRecoveryGoalKind::Graveyard
        && goal.Kind != PlayerbotRecoveryGoalKind::PartyLeader && !player->IsInCombat())
    {
        uint64 const nowMs = uint64(GameTime::GetGameTime()) * IN_MILLISECONDS;
        if (PlayerbotBadPlace const* bad = _badPlaces.RouteMeets(player->GetMapId(), path, nowMs))
        {
            TC_LOG_INFO(PLAYERBOTS_LOG,
                "mod-playerbots: {} will not walk this route: it goes past ({:.2f}, {:.2f}, {:.2f}) the way a walk of hers failed there {} time(s), last {} seconds ago. Looking for other work.",
                player->GetName(), bad->X, bad->Y, bad->Z, bad->Failures, (nowMs - bad->FailedAtMs) / IN_MILLISECONDS);
            _startMetBadPlace = true;
            _state = State::Failed;
            return false;
        }
    }

    _path = std::move(path);
    _pathType = mmapEvidence.Type;
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _lastProgressPos = from;
    _contouring = false;
    _startedOnAFace = false;

    Position first;
    GroundedStepFailure const firstFailure = PeekGroundedStepFailure(player, HeartbeatStepLen(player), first);
    if (firstFailure == GroundedStepFailure::None)
    {
        if (_faceRecovery.Active())
        {
            if (!RejoinPathReachesNewGround(from, _path))
            {
                LogRecoveryMmap(player, "rejoin rejected because its first yards only revisit recovery ground", mmapEvidence);
                if (TryLeaveFace(player, false))
                    return true;
                if (TryStartJump(player))
                    return true;
                FailNoLegalRing(player);
                return false;
            }

            _faceRecovery.BeginMmapRejoin(from.GetExactDist(_destination), from.GetPositionX(), from.GetPositionY());
            LogRecoveryMmap(player, "testing provisional rejoin", mmapEvidence);
        }
        else
            ClearFaceRecovery();
        _state = State::Moving;
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} starting walk on the {} movement maps. {} points, length to destination {:.1f} yards. {} The route took {:.2f} ms to build.",
            player->GetName(), mmapEvidence.PlayerNavMesh ? "player" : "creature", uint32(_path.size()), from.GetExactDist(destination),
            DescribePathSearch(mmapEvidence.Search), mmapEvidence.BuildMs);
        QueueMove(player, from, true, true);
        return true;
    }

    // Mmap's first step is a face or a wall. That is not unreachable. Walk legal ground beside it.
    // A wall is not a hill: look for another same-objective yellow only when this first step is steep.
    _startedOnAFace = firstFailure == GroundedStepFailure::NoFloor
        || firstFailure == GroundedStepFailure::SteepUp
        || firstFailure == GroundedStepFailure::TooFarDown;
    BeginFaceRecovery(player, firstFailure, first);
    LogRecoveryMmap(player, "mmap prefix refused", mmapEvidence);
    if (TryLeaveFace(player, false))
        return true;
    if (TryStartJump(player))
        return true;
    if (BeginWayRound(player, GroundedStepFailureName(firstFailure)))
        return true;

    FailNoLegalRing(player);
    return false;
}

// Her body does not fit where she stands: she is in a pocket under something low, which a player's client never lets
// them into. It would stand a player on the surface over that pocket, so she stands there too, once, and walks on from
// there with ordinary steps.
bool PlayerbotWalker::StandUpOutOfPocket(Player* player, Position& feet)
{
    if (!player || player->GetTransport() || player->IsFlying() || HasHeadroom(player, feet))
        return false;

    float const bodyTop = std::max(HEADROOM_FROM_FEET_YARDS + 0.1f, player->GetCollisionHeight());
    float const searchZ = feet.GetPositionZ() + bodyTop + STAND_UP_SEARCH_ABOVE_BODY_YARDS;
    float const topZ = player->GetMapHeight(feet.GetPositionX(), feet.GetPositionY(), searchZ);
    if (topZ <= INVALID_HEIGHT || topZ <= feet.GetPositionZ() + HEADROOM_FROM_FEET_YARDS)
    {
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} has no room for her body at ({:.2f}, {:.2f}, {:.2f}) and found no surface over her within {:.1f} yards to stand on.",
            player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(), bodyTop + STAND_UP_SEARCH_ABOVE_BODY_YARDS);
        return false;
    }

    Position top;
    top.Relocate(feet.GetPositionX(), feet.GetPositionY(), topZ, feet.GetOrientation());
    if (!HasHeadroom(player, top))
    {
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} has no room for her body at ({:.2f}, {:.2f}, {:.2f}), and none on the surface over her at {:.2f} either.",
            player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(), topZ);
        return false;
    }

    TC_LOG_INFO(PLAYERBOTS_LOG,
        "mod-playerbots: {} has no room for her body at ({:.2f}, {:.2f}, {:.2f}), so she stands up onto the surface over her at {:.2f}, {:.2f} yards up.",
        player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(), topZ, topZ - feet.GetPositionZ());
    QueueMove(player, top, false, false);
    feet = top;
    return true;
}

void PlayerbotWalker::Update(Player* player, uint32 diff)
{
    if ((_state != State::Moving && _state != State::Jumping && _state != State::AwaitingClientSync
        && _state != State::LookingForAWayRound)
        || !player || !player->IsInWorld() || !player->GetSession())
        return;

    if (_state == State::AwaitingClientSync)
    {
        UpdateOwningClientSync(player, diff);
        return;
    }

    if (_state == State::Jumping)
    {
        UpdateJump(player, diff);
        return;
    }

    if (_state == State::LookingForAWayRound)
    {
        UpdateWayRound(player, diff);
        return;
    }

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
        _lastRequestedStep = next;
        Position const previousGrounded = _lastGrounded;
        Position grounded;
        GroundedStepFailure const failure = ClassifyGroundedStep(player, previousGrounded,
            next.GetPositionX(), next.GetPositionY(), next.GetOrientation(), grounded);
        if (failure != GroundedStepFailure::None)
        {
            RefuseStep(player, failure, grounded);
            return;
        }

        // She may step down further than she may climb, so a step down that steep is one she cannot take back. Take it
        // only when the route carries on from there: that is how bots walked down into cracks and hollows with walls
        // too steep to climb, and stood there for good.
        GroundedStep const down = MeasureGroundedStep(previousGrounded, grounded);
        if (down.rise < 0.0f && down.degrees > MAX_WALKABLE_SLOPE_DEGREES)
        {
            GroundedStepFailure aheadFailure = GroundedStepFailure::None;
            Position aheadAt;
            if (StepDownLeadsNowhere(player, grounded, stepLen, aheadFailure, aheadAt))
            {
                std::string const why = aheadFailure == GroundedStepFailure::None
                    ? std::string("the route ends there with no step she may take")
                    : Trinity::StringFormat("the route on from there is refused ({})", GroundedStepFailureName(aheadFailure));
                PLAYERBOT_LOG_DETAIL(player,
                    "mod-playerbots: {} does not step down {:.2f} yards to ({:.2f}, {:.2f}, {:.2f}): she could not climb back, and {} at ({:.2f}, {:.2f}, {:.2f}).",
                    player->GetName(), -down.rise, grounded.GetPositionX(), grounded.GetPositionY(), grounded.GetPositionZ(),
                    why, aheadAt.GetPositionX(), aheadAt.GetPositionY(), aheadAt.GetPositionZ());
                RefuseStep(player, GroundedStepFailure::NoWayOut, grounded);
                return;
            }
        }

        _lastGrounded = grounded;
        if (_faceRecovery.Rejoining())
            NoteMmapRejoinProgress(player, previousGrounded);
        else if (_faceRecovery.Active())
            _faceRecovery.Advance(previousGrounded.GetExactDist2d(_lastGrounded),
                _lastGrounded.GetPositionX(), _lastGrounded.GetPositionY());

        // Every step legal and still covering ground she has already walked: the route she was given is going nowhere.
        if (_faceRecovery.Rejoining() && _faceRecovery.Exhausted())
        {
            TC_LOG_INFO(PLAYERBOTS_LOG,
                "mod-playerbots: {} walked {:.1f} yards of this route without reaching new ground, so that route is going nowhere.",
                player->GetName(), _faceRecovery.StalledYards());
            _faceRecovery.Refuse();
            if (BeginWayRound(player, "the route she was walking went nowhere"))
                return;
            // She already looked once on this approach. Leaving the face again only rejoins a route that has
            // already gone nowhere, and this check would refuse it again on the next heartbeat.
            if (_lookedForAWayRound || !TryLeaveFace(player, true))
                FailNoLegalRing(player);
            return;
        }

        bool const atDest = grounded.GetExactDist(_destination) <= _stopDistance;
        bool const pathDone = _pointIndex + 1 >= _path.size();
        if (atDest)
        {
            FinishGroundedArrival(player, grounded);
            return;
        }

        if (_walkingAWayRound && pathDone)
        {
            _walkingAWayRound = false;
            ClearWayRound();
            PLAYERBOT_LOG_DETAIL(player,
                "mod-playerbots: {} walked the way round and is asking the navmesh again from ({:.2f}, {:.2f}, {:.2f}).",
                player->GetName(), _lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ());
            if (TryCommitMmap(player, _lastGrounded, true))
                return;
            if (TryLeaveFace(player, true))
                return;
            FailNoLegalRing(player);
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
            // Mmap can end short of the destination: a partial route, or a destination off the mesh.
            if (grounded.GetExactDist2d(_destination) > _stopDistance)
                FinishShortOfDestination(player, grounded);
            else
                FinishGroundedArrival(player, grounded);
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
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} walking at ({:.2f}, {:.2f}, {:.2f}).",
            player->GetName(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    }

    if (_stuckMs >= STUCK_TIMEOUT_MS)
        Fail(player, "position stopped advancing");
}

void PlayerbotWalker::QueueMove(Player* player, Position const& pos, bool moving, bool start)
{
    MovementInfo info;
    PlayerbotClient::FillClientMovementInfo(player, pos, info);
    if (moving)
        info.AddMovementFlag(MOVEMENTFLAG_FORWARD);

    OpcodeClient opcode = CMSG_MOVE_HEARTBEAT;
    if (start)
        opcode = CMSG_MOVE_START_FORWARD;
    else if (!moving)
        opcode = CMSG_MOVE_STOP;

    if (!PlayerbotClient::QueueMovement(player->GetSession(), opcode, info))
        return;

    if (_mirrorOwningClientMovement)
        // Trinity omits the apparent sender from the normal movement broadcast. The
        // connected original did not originate this queued packet, so show it the same
        // state while the queued CMSG remains the authoritative world action.
        PlayerbotClient::SendMovementUpdate(player->GetSession(), info);
}

void PlayerbotWalker::QueueJumpMove(Player* player, OpcodeClient opcode, Position const& pos, uint32 fallTime)
{
    if (!player || !player->GetSession())
        return;

    MovementInfo info;
    PlayerbotClient::FillClientMovementInfo(player, pos, info);
    // A jump keeps the forward key it launched with. A player the server threw is not holding one.
    if (!_jump.Knockback)
        info.AddMovementFlag(MOVEMENTFLAG_FORWARD);
    if (opcode == CMSG_MOVE_START_SWIM)
        info.AddMovementFlag(MOVEMENTFLAG_SWIMMING);
    else if (opcode != CMSG_MOVE_FALL_LAND)
        info.AddMovementFlag(MOVEMENTFLAG_FALLING);

    bool const movingSideways = fallTime < std::min(_jump.SidewaysStopMs, _jump.CeilingMs);
    info.jump.fallTime = fallTime;
    info.jump.zspeed = -_jump.Trajectory.VerticalSpeed;
    info.jump.sinAngle = _jump.DirectionY;
    info.jump.cosAngle = _jump.DirectionX;
    info.jump.xyspeed = movingSideways ? _jump.Trajectory.HorizontalSpeed : 0.0f;
    if (!PlayerbotClient::QueueMovement(player->GetSession(), opcode, info))
        return;

    if (_mirrorOwningClientMovement)
        PlayerbotClient::SendMovementUpdate(player->GetSession(), info);
}

void PlayerbotWalker::FinishGroundedArrival(Player* player, Position const& pos)
{
    QueueMove(player, pos, false, false);
    _lastGrounded = pos;
    _contouring = false;
    ClearFaceRecovery();

    if (_mirrorOwningClientMovement)
    {
        _state = State::AwaitingClientSync;
        _owningClientSyncMs = 0;
        return;
    }

    _state = State::Arrived;
    PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} stopped at ({:.2f}, {:.2f}, {:.2f}).",
        player->GetName(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
}

// The first stop short of the destination is still an arrival: the caller may click from here or walk on.
// Stopping short again beside an earlier short stop of this approach, and no closer, is not progress. Recovery and
// the next mmap path would only bring her back to the same end, and every stop there would start a fresh episode.
void PlayerbotWalker::FinishShortOfDestination(Player* player, Position const& pos)
{
    float const shortYards = pos.GetExactDist2d(_destination);
    auto const earlier = std::find_if(_shortStops.begin(), _shortStops.end(), [&](Position const& stop)
    {
        return stop.GetExactDist(pos) <= SHORT_STOP_REPEAT_YARDS
            && shortYards + SHORT_STOP_PROGRESS_YARDS > stop.GetExactDist2d(_destination);
    });

    if (earlier != _shortStops.end())
    {
        _lastGrounded = pos;
        _contouring = false;
        PLAYERBOT_LOG_DETAIL(player,
            "mod-playerbots: {} stopped short of the walk destination again at ({:.2f}, {:.2f}, {:.2f}), {:.1f} yards from an earlier short stop on this approach and no closer: goal={} map={} key={:016X}:{:016X}, type=0x{:02X}, yardsShort={:.1f}, earlierYardsShort={:.1f}, shortStops={}. Mmap does not lead closer from here; this approach is exhausted.",
            player->GetName(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), earlier->GetExactDist(pos),
            PlayerbotRecoveryGoalKindName(_recoveryGoal.Kind), _recoveryGoal.MapId, _recoveryGoal.Secondary, _recoveryGoal.Primary,
            _pathType, shortYards, earlier->GetExactDist2d(_destination), _shortStops.size());
        LogConnectivity(player, pos, "this stop");
        // The stop packet goes out either way: the look sends it before she stands and maps the ground.
        if (BeginWayRound(player, "this route keeps ending short in the same place"))
            return;

        QueueMove(player, pos, false, false);
        _state = State::Failed;
        return;
    }

    if (_shortStops.size() >= SHORT_STOP_MEMORY)
        _shortStops.erase(_shortStops.begin());
    _shortStops.push_back(pos);

    FinishGroundedArrival(player, pos);
    PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} is {:.1f} yards short of the walk destination where this mmap path ends (type=0x{:02X}).",
        player->GetName(), shortYards, _pathType);
}

void PlayerbotWalker::UpdateOwningClientSync(Player* player, uint32 diff)
{
    _owningClientSyncMs += diff;
    float const distance = player->GetPosition().GetExactDist(_lastGrounded);
    if (distance <= OWNING_CLIENT_SYNC_DISTANCE)
    {
        _state = State::Arrived;
        _owningClientSyncMs = 0;
        PLAYERBOT_LOG_DETAIL(player,
            "mod-playerbots: {} owning client synchronized at commanded feet ({:.2f}, {:.2f}, {:.2f}).",
            player->GetName(), _lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ());
        return;
    }

    if (_owningClientSyncMs < OWNING_CLIENT_SYNC_TIMEOUT_MS)
        return;

    TC_LOG_INFO(PLAYERBOTS_LOG,
        "mod-playerbots: {} owning client did not synchronize commanded movement: expected ({:.2f}, {:.2f}, {:.2f}), actual ({:.2f}, {:.2f}, {:.2f}), distance {:.2f}.",
        player->GetName(), _lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ(),
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), distance);
    Fail(player, "owning client movement synchronization timed out");
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

PlayerbotWalker::GroundedStepFailure PlayerbotWalker::PeekGroundedStepFailure(Player* player, float distance, Position& out)
{
    size_t const savedIndex = _pointIndex;
    float const savedProgress = _segmentProgress;
    Position const next = Advance(distance);
    _lastRequestedStep = next;
    _pointIndex = savedIndex;
    _segmentProgress = savedProgress;

    if (!player)
        return GroundedStepFailure::InvalidPosition;

    return ClassifyGroundedStep(player, _lastGrounded, next.GetPositionX(), next.GetPositionY(), next.GetOrientation(), out);
}

bool PlayerbotWalker::StepDownLeadsNowhere(Player* player, Position const& landing, float stepLen,
    GroundedStepFailure& aheadFailure, Position& aheadAt)
{
    aheadFailure = GroundedStepFailure::None;
    aheadAt = landing;
    if (!player || stepLen <= 0.0f)
        return false;

    // Advance has already moved the route to the landing. Walk on from there on paper and put it back afterwards.
    size_t const savedIndex = _pointIndex;
    float const savedProgress = _segmentProgress;
    Position from = landing;
    bool nowhere = false;
    bool ended = _pointIndex + 1 >= _path.size();
    for (float looked = 0.0f; !ended && looked < STEP_DOWN_LOOK_AHEAD_YARDS; looked += stepLen)
    {
        Position const next = Advance(stepLen);
        Position out;
        GroundedStepFailure const failure = ClassifyGroundedStep(player, from, next.GetPositionX(), next.GetPositionY(),
            next.GetOrientation(), out);
        if (failure != GroundedStepFailure::None)
        {
            aheadFailure = failure;
            aheadAt = out;
            nowhere = true;
            break;
        }
        from = out;
        ended = _pointIndex + 1 >= _path.size();
    }
    _pointIndex = savedIndex;
    _segmentProgress = savedProgress;
    if (nowhere || !ended)
        return nowhere;

    // The route ends within those yards. Ending there is fine while she can still step somewhere from it.
    aheadAt = from;
    for (uint32 i = 0; i < 8; ++i)
    {
        float const angle = float(i) * float(M_PI) / 4.0f;
        Position out;
        if (ClassifyGroundedStep(player, from, from.GetPositionX() + std::cos(angle) * stepLen,
            from.GetPositionY() + std::sin(angle) * stepLen, angle, out) == GroundedStepFailure::None)
            return false;
    }
    return true;
}

PlayerbotWalker::GroundedStepFailure PlayerbotWalker::ClassifyGroundedStep(Player* player, Position const& from,
    float x, float y, float orientation, Position& out)
{
    out.Relocate(x, y, from.GetPositionZ(), orientation);
    if (!Trinity::IsValidMapCoord(x, y, from.GetPositionZ()))
        return GroundedStepFailure::InvalidPosition;
    if (!PlantFromFeet(player, from, x, y, orientation, out))
        return GroundedStepFailure::NoFloor;

    GroundedStep const step = MeasureGroundedStep(from, out);
    if (step.rise > 0.0f && step.degrees > MAX_WALKABLE_SLOPE_DEGREES)
        return GroundedStepFailure::SteepUp;
    if (step.rise <= 0.0f && -step.rise > MAX_DOWN_STEP_YARDS)
        return GroundedStepFailure::TooFarDown;

    switch (GetStepWorldCollision(player, from, out))
    {
        case StepWorldCollision::Static:
            return GroundedStepFailure::StaticCollision;
        case StepWorldCollision::Dynamic:
            return GroundedStepFailure::DynamicCollision;
        case StepWorldCollision::InvalidPosition:
            return GroundedStepFailure::InvalidPosition;
        case StepWorldCollision::None:
            break;
    }

    switch (GetHeadroomCollision(player, out))
    {
        case StepWorldCollision::None:
            return GroundedStepFailure::None;
        case StepWorldCollision::InvalidPosition:
            return GroundedStepFailure::InvalidPosition;
        case StepWorldCollision::Static:
        case StepWorldCollision::Dynamic:
            return GroundedStepFailure::NoHeadroom;
    }

    return GroundedStepFailure::InvalidPosition;
}

bool PlayerbotWalker::PlantAt(Player* player, float x, float y, float searchZ, float& outZ)
{
    PlayerbotCostTimer const cost(PlayerbotCostStep::Floor);
    if (!player || !Trinity::IsValidMapCoord(x, y, searchZ))
        return false;

    // UpdateAllowedPositionZ looks for the floor from the same height as GetMapHeight and reports it, so one look answers
    // both whether there is a floor and where her body stands on it. When it found none it leaves the reported floor at
    // the search height (and on a transport it never looks), and only then is the floor asked for on its own.
    float z = searchZ;
    float groundZ = searchZ;
    player->UpdateAllowedPositionZ(x, y, z, &groundZ);
    if (groundZ == searchZ)
    {
        if (player->GetMapHeight(x, y, searchZ) <= INVALID_HEIGHT)
            return false;
    }
    else if (groundZ <= INVALID_HEIGHT)
        return false;

    if (z <= INVALID_HEIGHT)
        return false;

    outZ = z;
    return true;
}

float PlayerbotWalker::HeartbeatStepLength(Player const* player)
{
    return HeartbeatStepLen(player);
}

bool PlayerbotWalker::LastWalkDestination(Position& out, uint32& mapId) const
{
    if (!_hasLastWalkDestination)
        return false;

    out = _lastWalkDestination;
    mapId = _lastWalkDestinationMapId;
    return true;
}

bool PlayerbotWalker::FirstGroundedStepIsLegal(Player* player)
{
    if (!player)
        return false;

    Position first;
    return PeekGroundedStepFailure(player, HeartbeatStepLen(player), first) == GroundedStepFailure::None;
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
    return _faceRecovery.Exhausted();
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

bool PlayerbotWalker::BuildMmapPath(Player* player, Position const& from, Position const& destination,
    std::vector<G3D::Vector3>& outPath, MmapPathEvidence* evidence)
{
    outPath.clear();
    if (evidence)
        *evidence = {};
    if (!player)
        return false;

    std::vector<AvoidCircle> avoids;
    CollectSpellFocusAvoids(player, 50.0f, avoids);

    // Every route she walks is asked of the set of movement maps built for a player's body. Where that set has not
    // been generated the engine answers from the creature one instead, exactly as it always did. Either way she searches
    // with room for a long route: a road out of a cave and round the hills can be three times the straight line.
    PathGenerator generator(player, NavMeshChoice::PlayerBody, PathReach::Long);
    std::chrono::steady_clock::time_point const started = std::chrono::steady_clock::now();
    bool calculated = false;
    {
        PlayerbotCostTimer const cost(PlayerbotCostStep::NavmeshRoute);
        calculated = generator.CalculatePath(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(),
            destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ(), false);
    }
    std::chrono::duration<float, std::milli> const took = std::chrono::steady_clock::now() - started;
    if (evidence)
    {
        evidence->Calculated = calculated;
        evidence->BuildMs = took.count();
        evidence->PlayerNavMesh = generator.UsedPlayerNavMesh();
        evidence->Search = generator.GetSearchReport();
        evidence->Type = uint32(generator.GetPathType());
        evidence->Length = generator.GetPathLength();
        evidence->ActualEnd = generator.GetActualEndPosition();
        Movement::PointsArray const& generated = generator.GetPath();
        size_t const prefixSize = std::min<size_t>(generated.size(), 5);
        evidence->Prefix.assign(generated.begin(), generated.begin() + prefixSize);
    }
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

                PathGenerator toVia(player, NavMeshChoice::PlayerBody, PathReach::Long);
                PlayerbotCostTimer const cost(PlayerbotCostStep::NavmeshRoute);
                if (!toVia.CalculatePath(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(),
                    via.GetPositionX(), via.GetPositionY(), via.GetPositionZ(), false) || !PathIsWalkable(toVia))
                    continue;
                if (PathHitsAvoid(toVia.GetPath(), avoids))
                    continue;

                PathGenerator toDest(player, NavMeshChoice::PlayerBody, PathReach::Long);
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

                PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} walking around {} instead of through it.",
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

bool PlayerbotWalker::RejoinPathReachesNewGround(Position const& from, std::vector<G3D::Vector3> const& path) const
{
    if (!_faceRecovery.Active())
        return true;
    if (path.size() < 2)
        return false;

    float checked = 0.0f;
    float const limit = PlayerbotFaceRecovery::ConfirmedRejoinYards;
    float const sampleYards = PlayerbotFaceRecovery::NewGroundCellYards * 0.5f;
    G3D::Vector3 previous(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ());

    for (size_t i = 1; i < path.size() && checked < limit; ++i)
    {
        G3D::Vector3 const delta = path[i] - previous;
        float const segmentYards = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (segmentYards < 0.01f)
        {
            previous = path[i];
            continue;
        }

        float const available = std::min(segmentYards, limit - checked);
        for (float along = std::min(sampleYards, available); along <= available + 0.01f; along += sampleYards)
        {
            float const t = along / segmentYards;
            float const x = previous.x + delta.x * t;
            float const y = previous.y + delta.y * t;
            if (!_faceRecovery.HasVisitedGround(x, y))
                return true;
        }

        checked += available;
        previous = path[i];
    }

    return false;
}

void PlayerbotWalker::LogRecoveryMmap(Player* player, char const* decision, MmapPathEvidence const& evidence) const
{
    if (!player || !_faceRecovery.Active())
        return;

    std::string prefix;
    for (G3D::Vector3 const& point : evidence.Prefix)
    {
        if (!prefix.empty())
            prefix += " -> ";
        prefix += Trinity::StringFormat("({:.2f},{:.2f},{:.2f})", point.x, point.y, point.z);
    }
    if (prefix.empty())
        prefix = "none";

    PLAYERBOT_LOG_DETAIL(player,
        "mod-playerbots: {} recovery mmap {}: goal={} map={} key={:016X}:{:016X}, mesh={}, calculated={}, type=0x{:02X}, actualEnd=({:.2f}, {:.2f}, {:.2f}), pathLength={:.1f}, firstPoints=[{}], episodeYards={:.1f}, visitedCells={}. {} The route took {:.2f} ms to build.",
        player->GetName(), decision, PlayerbotRecoveryGoalKindName(_recoveryGoal.Kind), _recoveryGoal.MapId,
        _recoveryGoal.Secondary, _recoveryGoal.Primary, evidence.PlayerNavMesh ? "player" : "creature",
        evidence.Calculated, evidence.Type,
        evidence.ActualEnd.x, evidence.ActualEnd.y, evidence.ActualEnd.z, evidence.Length, prefix,
        _faceRecovery.EpisodeYards(), _faceRecovery.VisitedGroundCells(), DescribePathSearch(evidence.Search), evidence.BuildMs);
}

void PlayerbotWalker::LogStartConnectivity(Player* player, Position const& from)
{
    if (!player || !_faceRecovery.Active() || _faceRecovery.ConnectivityChecked())
        return;

    _faceRecovery.MarkConnectivityChecked();
    LogConnectivity(player, from, "the start");
}

void PlayerbotWalker::LogConnectivity(Player* player, Position const& from, char const* place) const
{
    PlayerbotCostTimer const cost(PlayerbotCostStep::ConnectivityProbe);
    if (!player)
        return;

    bool connected = false;
    float connectedRadius = 0.0f;
    int32 connectedDirection = -1;
    float connectedEndpointGap = 0.0f;

    for (float radius : RECOVERY_CONNECTIVITY_PROBE_YARDS)
    {
        for (int32 direction = 0; direction < RECOVERY_CONNECTIVITY_DIRECTIONS; ++direction)
        {
            float const angle = float(direction) * (2.0f * float(M_PI)) / float(RECOVERY_CONNECTIVITY_DIRECTIONS);
            float const x = from.GetPositionX() + std::cos(angle) * radius;
            float const y = from.GetPositionY() + std::sin(angle) * radius;
            float z = from.GetPositionZ();
            if (!Trinity::IsValidMapCoord(x, y, z))
                continue;
            player->UpdateAllowedPositionZ(x, y, z);
            if (!Trinity::IsValidMapCoord(x, y, z) || z <= INVALID_HEIGHT)
                continue;

            // These probes are 60 and 120 yards out and there can be 32 of them in a row, so they keep the short search.
            PathGenerator probe(player, NavMeshChoice::PlayerBody);
            PlayerbotCostTimer const cost(PlayerbotCostStep::NavmeshRoute);
            if (!probe.CalculatePath(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(), x, y, z, false))
                continue;

            uint32 const type = uint32(probe.GetPathType());
            if ((type & REFUSED_PATH_TYPES) || (type & PATHFIND_FARFROMPOLY_START) || probe.GetPath().size() < 2)
                continue;

            G3D::Vector3 const& endpoint = probe.GetActualEndPosition();
            connectedEndpointGap = std::sqrt((endpoint.x - x) * (endpoint.x - x) + (endpoint.y - y) * (endpoint.y - y));
            if ((type & PATHFIND_INCOMPLETE) && connectedEndpointGap > RECOVERY_PROBE_ENDPOINT_YARDS)
                continue;

            connected = true;
            connectedRadius = radius;
            connectedDirection = direction;
            break;
        }

        if (connected)
            break;
    }

    if (connected)
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} recovery diagnosis: {} has an honest mmap route to the {:.0f}-yard probe at direction {} (endpoint gap {:.1f}); this is a bad destination or route leg, not an isolated pocket.",
            player->GetName(), place, connectedRadius, connectedDirection, connectedEndpointGap);
    else
        PLAYERBOT_LOG_DETAIL(player,
            "mod-playerbots: {} recovery diagnosis: no honest mmap route from {} reached any of 16 probes at 60/120 yards. The bot may be standing in a disconnected or local navmesh pocket; this is diagnostic only and does not permit a teleport.",
            player->GetName(), place);
}

bool PlayerbotWalker::TryCommitMmap(Player* player, Position const& from, bool alreadyMoving)
{
    if (!player || !player->GetSession())
        return false;

    std::vector<G3D::Vector3> path;
    MmapPathEvidence mmapEvidence;
    if (!BuildMmapPath(player, from, _destination, path, &mmapEvidence))
    {
        if (_faceRecovery.Active())
        {
            LogRecoveryMmap(player, "rejoin path rejected", mmapEvidence);
            LogStartConnectivity(player, from);
        }
        return false;
    }

    bool const testingFaceRejoin = _faceRecovery.Active();
    if (testingFaceRejoin && !RejoinPathReachesNewGround(from, path))
    {
        LogRecoveryMmap(player, "rejoin rejected because its first yards only revisit recovery ground", mmapEvidence);
        return false;
    }

    std::vector<G3D::Vector3> savedPath = _path;
    size_t const savedIndex = _pointIndex;
    float const savedProgress = _segmentProgress;
    _path = std::move(path);
    _pointIndex = 0;
    _segmentProgress = 0.0f;

    if (!MmapLookIsLegal(player))
    {
        if (testingFaceRejoin)
            LogRecoveryMmap(player, "rejoin rejected because its first four yards still meet the obstruction", mmapEvidence);
        _path = std::move(savedPath);
        _pointIndex = savedIndex;
        _segmentProgress = savedProgress;
        return false;
    }

    // She is on the navmesh again, so the ground she mapped for a way round is stale from here.
    if (_walkingAWayRound)
        ClearWayRound();
    _walkingAWayRound = false;
    _pathType = mmapEvidence.Type;
    _contouring = false;
    _startedOnAFace = false;
    if (testingFaceRejoin)
    {
        _faceRecovery.BeginMmapRejoin(from.GetExactDist(_destination), from.GetPositionX(), from.GetPositionY());
        _destPokeActive = false;
    }
    else
        ClearFaceRecovery();
    _heartbeatMs = 0;
    _stuckMs = 0;
    if (!testingFaceRejoin)
        _logMs = 0;
    _lastProgressPos = from;
    _state = State::Moving;

    if (testingFaceRejoin)
    {
        LogRecoveryMmap(player, "testing provisional rejoin", mmapEvidence);
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} is testing an mmap rejoin. {} points, length to destination {:.1f} yards; obstacle recovery remains active.",
            player->GetName(), uint32(_path.size()), from.GetExactDist(_destination));
    }
    else
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} starting walk on the {} movement maps. {} points, length to destination {:.1f} yards. {} The route took {:.2f} ms to build.",
            player->GetName(), mmapEvidence.PlayerNavMesh ? "player" : "creature", uint32(_path.size()), from.GetExactDist(_destination),
            DescribePathSearch(mmapEvidence.Search), mmapEvidence.BuildMs);

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

    // The way round is over once she walks something else; the ground she mapped for it is stale from here.
    if (_walkingAWayRound)
        ClearWayRound();
    _walkingAWayRound = false;
    _path.clear();
    _path.push_back(G3D::Vector3(_lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ()));
    _path.push_back(G3D::Vector3(side.GetPositionX(), side.GetPositionY(), side.GetPositionZ()));
    _pathType = 0;
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _heartbeatMs = 0;
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
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} local look found a way around.", player->GetName());

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

char const* PlayerbotWalker::GroundedStepFailureName(GroundedStepFailure failure)
{
    switch (failure)
    {
        case GroundedStepFailure::None:
            return "legal";
        case GroundedStepFailure::NoPath:
            return "no mmap path";
        case GroundedStepFailure::NoFloor:
            return "no floor";
        case GroundedStepFailure::SteepUp:
            return "steep uphill";
        case GroundedStepFailure::TooFarDown:
            return "drop too far";
        case GroundedStepFailure::StaticCollision:
            return "vmap collision";
        case GroundedStepFailure::DynamicCollision:
            return "gameobject collision";
        case GroundedStepFailure::NoHeadroom:
            return "too low for her body";
        case GroundedStepFailure::NoWayOut:
            return "a step down she could not walk back out of";
        case GroundedStepFailure::InvalidPosition:
            return "invalid position";
    }

    return "unknown obstruction";
}

bool PlayerbotWalker::BeginFaceRecovery(Player* player, GroundedStepFailure failure, Position const& attempted)
{
    bool const newEpisode = !_faceRecovery.Active();
    float const rejoinYards = _faceRecovery.RejoinYards();
    bool const repeatedDuringRejoin = _faceRecovery.Refuse();
    bool const reasonChanged = !newEpisode && failure != _lastGroundedStepFailure;

    if (newEpisode)
    {
        _faceRecovery.Begin(_lastGrounded.GetPositionX(), _lastGrounded.GetPositionY());
        NoteLipOrigin();
        _episodeFeet = _lastGrounded;
        _episodeRefused = attempted;
        _haveEpisodeRefusal = true;
        if (!_haveApproachRefusal)
        {
            _approachFeet = _lastGrounded;
            _approachRefused = attempted;
            _haveApproachRefusal = true;
        }
    }

    _lastGroundedStepFailure = failure;
    _lastRefusedStep = attempted;

    if (!player || (!newEpisode && !repeatedDuringRejoin && !reasonChanged))
        return repeatedDuringRejoin;

    GroundedStep const step = MeasureGroundedStep(_lastGrounded, attempted);
    char const* floorKind = failure == GroundedStepFailure::NoFloor ? "none" : "planted";
    if (repeatedDuringRejoin)
        PLAYERBOT_LOG_DETAIL(player,
            "mod-playerbots: {} met the same local obstruction after {:.1f} yards of a provisional mmap rejoin ({}; rise {:.2f}, slope {:.1f} degrees). Floor/layer evidence: feetZ={:.2f}, mmapZ={:.2f}, {}Z={:.2f}. Continuing goal {} with {:.1f} episode yards across {} visited cells.",
            player->GetName(), rejoinYards, GroundedStepFailureName(failure), step.rise, step.degrees,
            _lastGrounded.GetPositionZ(), _lastRequestedStep.GetPositionZ(), floorKind, attempted.GetPositionZ(),
            PlayerbotRecoveryGoalKindName(_recoveryGoal.Kind), _faceRecovery.EpisodeYards(), _faceRecovery.VisitedGroundCells());
    else
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} began local obstacle recovery for goal {} ({} at ({:.2f}, {:.2f}, {:.2f}); rise {:.2f}, slope {:.1f} degrees). Floor/layer evidence: feetZ={:.2f}, mmapZ={:.2f}, {}Z={:.2f}.",
            player->GetName(), PlayerbotRecoveryGoalKindName(_recoveryGoal.Kind), GroundedStepFailureName(failure),
            attempted.GetPositionX(), attempted.GetPositionY(), attempted.GetPositionZ(), step.rise, step.degrees,
            _lastGrounded.GetPositionZ(), _lastRequestedStep.GetPositionZ(), floorKind, attempted.GetPositionZ());

    return repeatedDuringRejoin;
}

void PlayerbotWalker::ClearFaceRecovery()
{
    _faceRecovery.Reset();
    _lastGroundedStepFailure = GroundedStepFailure::None;
    _lastRefusedStep.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
    _haveEpisodeRefusal = false;
    _startedOnAFace = false;
    _lipSteps = 0;
    _lipDestDist = 0.0f;
    _haveLipOrigin = false;
    _destPokeActive = false;
    _contourDirX = 0.0f;
    _contourDirY = 0.0f;
}

void PlayerbotWalker::NoteMmapRejoinProgress(Player* player, Position const& previousFeet)
{
    if (!_faceRecovery.Rejoining())
        return;

    float const stepYards = previousFeet.GetExactDist2d(_lastGrounded);
    float const destinationDistance = _lastGrounded.GetExactDist(_destination);
    if (!_faceRecovery.AdvanceMmap(stepYards, destinationDistance,
        _lastGrounded.GetPositionX(), _lastGrounded.GetPositionY()))
        return;

    float const rejoinYards = _faceRecovery.RejoinYards();
    if (player)
        PLAYERBOT_LOG_DETAIL(player,
            "mod-playerbots: {} left the local obstruction after {:.1f} yards of legal mmap travel. Obstacle recovery is clear.",
            player->GetName(), rejoinYards);
    ClearFaceRecovery();
}

void PlayerbotWalker::RefuseStep(Player* player, GroundedStepFailure failure, Position const& attempted)
{
    // A step of a way round was refused. The ground she mapped is still good: walk the rest of it from here.
    if (_walkingAWayRound && RepairWayRound(player))
        return;

    bool const repeatedDuringRejoin = BeginFaceRecovery(player, failure, attempted);
    bool triedJump = false;

    if (repeatedDuringRejoin)
    {
        triedJump = true;
        if (TryStartJump(player))
            return;
    }

    if (_faceRecovery.Exhausted())
    {
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} still meets the local obstruction after {:.1f} yards without reaching new ground ({:.1f} total recovery yards across {} local cells). The bot is standing still.",
            player->GetName(), _faceRecovery.StalledYards(), _faceRecovery.EpisodeYards(), _faceRecovery.VisitedGroundCells());
        if (BeginWayRound(player, "the local look ran out of new ground"))
            return;

        FailNoLegalRing(player);
        return;
    }

    // Keep FORWARD. A player turns onto the flat beside a face or a wall; they do not stop and start on the same toes.
    if (TryLeaveFace(player, true))
        return;

    // With no legal contour, one validated normal jump may still clear a short uphill lip or low static obstacle.
    if (!triedJump && TryStartJump(player))
        return;

    // The four-yard look found nothing. Map the ground around her and look for a way round out there.
    if (BeginWayRound(player, GroundedStepFailureName(failure)))
        return;

    FailNoLegalRing(player);
}

bool PlayerbotWalker::BeginWayRound(Player* player, char const* reason)
{
    // One look per approach. A second map of the same ground would only find the same ways round.
    if (_lookedForAWayRound || !player || !player->IsInWorld() || !player->GetSession() || !player->IsAlive())
        return false;
    if (player->IsBeingTeleported() || !player->movespline->Finalized())
        return false;

    Map* map = player->FindMap();
    if (!map)
        return false;

    Position const feet = _state == State::Moving ? _lastGrounded : player->GetPosition();
    float x = feet.GetPositionX();
    float y = feet.GetPositionY();
    float z = feet.GetPositionZ();
    if (!Trinity::IsValidMapCoord(x, y, z))
        return false;

    // A walk starts from her feet planted this way, and so does the map of the ground around them.
    player->UpdateAllowedPositionZ(x, y, z);
    if (!Trinity::IsValidMapCoord(x, y, z))
        return false;

    // Stop where her last step put her, then stand and look.
    if (_state == State::Moving)
        QueueMove(player, _lastGrounded, false, false);

    _lastGrounded.Relocate(x, y, z, _lastGrounded.GetOrientation());
    StartWayRoundMap(player, WAY_ROUND_FIRST_YARDS);
    _wayRoundWaysOut.clear();
    _wayRoundProbe = 0;
    _wayRoundTarget = -1;
    _wayRoundRepairs = 0;
    _wayRoundPhase = WayRoundPhase::Mapping;
    _wayRoundMs = 0;
    _wayRoundMapId = player->GetMapId();
    EndWayRoundTurn(PlayerbotWayRoundEnd::Abandoned);
    // She joins the line on her next update, unless her brain sends her to another spot of the same work first.
    _wayRoundJoinReason = reason ? reason : "no reason given";
    _wayRoundWaitMs = 0;
    _wayRoundWork = std::chrono::steady_clock::duration::zero();
    _wayRoundHadATurn = false;
    _walkingAWayRound = false;
    _lookedForAWayRound = true;
    _wayRoundFirstStepRefused = false;
    _state = State::LookingForAWayRound;
    PLAYERBOT_LOG_DETAIL(player,
        "mod-playerbots: {} stopped to look for a way round ({}). She will map the ground she can walk within {:.0f} yards of ({:.2f}, {:.2f}, {:.2f}).",
        player->GetName(), _wayRoundJoinReason, _wayRoundYards, x, y, z);
    return true;
}

// A fresh map of the ground within this many yards of the feet she stands on while she looks.
void PlayerbotWalker::StartWayRoundMap(Player* player, float yards)
{
    PlayerbotWalkMapSettings settings;
    settings.OriginX = _lastGrounded.GetPositionX();
    settings.OriginY = _lastGrounded.GetPositionY();
    settings.OriginZ = _lastGrounded.GetPositionZ();
    settings.Spacing = std::clamp(HeartbeatStepLen(player), WAY_ROUND_MIN_SPACING, WAY_ROUND_MAX_SPACING);
    settings.StepYards = HeartbeatStepLen(player);
    settings.Radius = yards;
    settings.MaxClimbDegrees = MaxWalkableSlopeDegrees;
    settings.MaxDropYards = MaxDownStepYards;
    settings.LayerYards = WAY_ROUND_LAYER_YARDS;
    settings.MaxSpots = WAY_ROUND_MAX_SPOTS;

    _wayRoundMap = std::make_unique<PlayerbotWalkMap>(settings);
    _wayRoundYards = yards;
}

void PlayerbotWalker::UpdateWayRound(Player* player, uint32 diff)
{
    PlayerbotCostTimer const cost(PlayerbotCostStep::WayRoundMap);
    Map* map = player->FindMap();
    if (!_wayRoundMap || !map || player->GetMapId() != _wayRoundMapId || !player->IsAlive() || player->IsBeingTeleported()
        || !player->movespline->Finalized())
    {
        ClearWayRound();
        Fail(player, "she could not finish looking for a way round");
        return;
    }

    // Nothing sent her elsewhere since she stopped, so she takes her place in the line.
    if (_wayRoundJoinReason)
        JoinWayRoundLine(player);

    // She was dropped from the line for missing her turn, which only happens when her updates stopped for a while.
    if (!WayRoundTurns.InLine(_wayRoundTicket))
        _wayRoundTicket = WayRoundTurns.Join("back in line after missing her turn");

    // Bots map in the order they stopped. Waiting in line does not count against her own time limit.
    if (!WayRoundTurns.IsHerTurn(_wayRoundTicket))
    {
        if (!_wayRoundHadATurn)
            _wayRoundWaitMs += diff;
        return;
    }
    if (!_wayRoundHadATurn)
    {
        _wayRoundHadATurn = true;
        if (_wayRoundWaitMs)
            PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} waited {:.1f} s for her turn to map the ground.",
                player->GetName(), _wayRoundWaitMs / 1000.0f);
    }
    else
        _wayRoundMs += diff;

    // The other bot at the front of the line may already have spent this tick's mapping time. Then she maps on the next.
    if (WayRoundSpentThisTick >= WAY_ROUND_TICK_BUDGET)
        return;

    PlayerbotWalkMapServerWorld world(player, map);
    std::chrono::steady_clock::time_point const sliceStart = std::chrono::steady_clock::now();
    bool finished = false;
    do
        finished = _wayRoundMap->Advance(world, WAY_ROUND_SPOTS_PER_CLOCK_CHECK);
    while (!finished && std::chrono::steady_clock::now() - sliceStart < WAY_ROUND_SLICE);
    std::chrono::steady_clock::duration const slice = std::chrono::steady_clock::now() - sliceStart;
    WayRoundSpentThisTick += slice;
    _wayRoundWork += slice;

    if (!finished)
    {
        if (_wayRoundMs >= WAY_ROUND_TIMEOUT_MS)
        {
            EndWayRoundTurn(PlayerbotWayRoundEnd::TookTooLong);
            ClearWayRound();
            Fail(player, "mapping the ground around her took too long");
        }
        return;
    }

    // The map is done. Something closer is worth walking to on its own; otherwise ask the navmesh from the ways out.
    if (_wayRoundPhase == WayRoundPhase::Mapping)
    {
        if (StartWayRoundWalk(player))
            return;

        // Nothing closer inside the small circle. Ground she can walk that carries on past it may lead round further out,
        // so she maps the full circle, keeping her turn. When all she can reach is inside, the full map would be the same.
        if (_wayRoundYards < WAY_ROUND_YARDS && PlayerbotWalkMapWorthWidening(*_wayRoundMap))
        {
            PLAYERBOT_LOG_DETAIL(player,
                "mod-playerbots: {} has nothing {:.0f} yards closer within {:.0f} yards, and the ground she can walk carries on past it, so she maps {:.0f} yards.",
                player->GetName(), WAY_ROUND_MIN_GAIN_YARDS, _wayRoundYards, WAY_ROUND_YARDS);
            WayRoundTurns.NoteWidened();
            StartWayRoundMap(player, WAY_ROUND_YARDS);
            return;
        }

        PlayerbotWalkMapWayRoundSettings waysOut;
        waysOut.DestinationX = _destination.GetPositionX();
        waysOut.DestinationY = _destination.GetPositionY();
        waysOut.DestinationZ = _destination.GetPositionZ();
        waysOut.Ways = WAY_ROUND_WAYS_OUT;
        waysOut.SpreadYards = WAY_ROUND_WAYS_OUT_SPREAD_YARDS;
        _wayRoundWaysOut = FindPlayerbotWalkMapWaysOut(*_wayRoundMap, waysOut);
        _wayRoundProbe = 0;
        _wayRoundPhase = WayRoundPhase::Probing;
        PLAYERBOT_LOG_DETAIL(player,
            "mod-playerbots: {} has nothing closer than {:.0f} yards within reach, so she is asking the navmesh for a route from {} way(s) out of this ground.",
            player->GetName(), WAY_ROUND_MIN_GAIN_YARDS, uint32(_wayRoundWaysOut.size()));
    }

    // One navmesh question per tick: each one is a route of hundreds of yards.
    if (_wayRoundProbe < _wayRoundWaysOut.size())
    {
        if (ProbeOneWayOut(player))
            return;
        ++_wayRoundProbe;
        return;
    }

    PlayerbotWalkMapSummary const summary = _wayRoundMap->Summarize();
    size_t const waysOut = _wayRoundWaysOut.size();
    EndWayRoundTurn(PlayerbotWayRoundEnd::FoundNone);
    ClearWayRound();
    _state = State::Failed;
    _contouring = false;
    // The only spot she can walk to is the one she stands on, or every way she found was refused at her first step, so
    // no destination could have been reached from here.
    _failedAtHerFeet = summary.Reached <= 1 || _wayRoundFirstStepRefused;
    if (!_failedAtHerFeet)
        NoteBadPlace(player);
    else if (summary.Reached <= 1)
        ExplainStuckFeet(player, _lastGrounded);
    if (_wayRoundFirstStepRefused)
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} found no way round: the map found {} spots she can walk to within {:.0f} yards, but every way round or way out she tried was refused at her first step. She cannot step anywhere from where she stands.",
            player->GetName(), summary.Reached, _wayRoundYards);
    else
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} found no way round: none of the {} spots she can walk to within {:.0f} yards is {:.0f} yards closer to where she is going, and the navmesh had no route she can start from any of the {} way(s) out. {}",
            player->GetName(), summary.Reached, _wayRoundYards, WAY_ROUND_MIN_GAIN_YARDS, uint32(waysOut),
            _failedAtHerFeet ? "She cannot step anywhere from where she stands." : "Looking for other work.");

    // A player standing here would walk off the edge and fall. She does the same when the fall is safe.
    if (_failedAtHerFeet)
        TryWalkOffLedge(player);
}

bool PlayerbotWalker::StartWayRoundWalk(Player* player)
{
    if (!_wayRoundMap || !player || !player->GetSession())
        return false;

    PlayerbotWalkMapWayRoundSettings settings;
    settings.DestinationX = _destination.GetPositionX();
    settings.DestinationY = _destination.GetPositionY();
    settings.DestinationZ = _destination.GetPositionZ();
    settings.MinimumGain = WAY_ROUND_MIN_GAIN_YARDS;
    settings.Ways = WAY_ROUND_WAYS;

    for (PlayerbotWalkMapWayRound const& way : FindPlayerbotWalkMapWaysRound(*_wayRoundMap, settings))
        if (WalkTheWayRound(player, way, "found a way round"))
            return true;

    return false;
}

bool PlayerbotWalker::ProbeOneWayOut(Player* player)
{
    if (!_wayRoundMap || !player || _wayRoundProbe >= _wayRoundWaysOut.size())
        return false;

    PlayerbotWalkMapWayRound const& way = _wayRoundWaysOut[_wayRoundProbe];
    PlayerbotWalkMapSpot const& spot = _wayRoundMap->Spots()[way.Target];
    Position out;
    out.Relocate(_wayRoundMap->WorldX(spot.I), _wayRoundMap->WorldY(spot.J), spot.Z, 0.0f);

    std::chrono::steady_clock::time_point const started = std::chrono::steady_clock::now();
    std::vector<G3D::Vector3> route;
    bool const routed = BuildMmapPath(player, out, _destination, route) && RouteStartsWalkable(player, out, route);
    std::chrono::steady_clock::duration const spent = std::chrono::steady_clock::now() - started;
    WayRoundSpentThisTick += spent;
    _wayRoundWork += spent;
    if (!routed)
        return false;

    return WalkTheWayRound(player, way, "found a way out with a navmesh route she can walk from");
}

bool PlayerbotWalker::WalkTheWayRound(Player* player, PlayerbotWalkMapWayRound const& way, char const* what)
{
    if (!_wayRoundMap || !player || !player->GetSession())
        return false;

    std::vector<std::array<float, 3>> points;
    PlayerbotWalkMapWayRoundPoints(*_wayRoundMap, way, points);
    if (points.size() < 2)
        return false;

    State const wasIn = _state;
    // Walk from her own feet, not from the nearest spot of the map's grid.
    _path.clear();
    _path.push_back(G3D::Vector3(_lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ()));
    for (size_t point = 1; point < points.size(); ++point)
        _path.push_back(G3D::Vector3(points[point][0], points[point][1], points[point][2]));

    _pathType = 0;
    _pointIndex = 0;
    _segmentProgress = 0.0f;
    _heartbeatMs = 0;
    _stuckMs = 0;
    _logMs = 0;
    _lastProgressPos = _lastGrounded;
    _contouring = false;
    _startedOnAFace = false;
    _state = State::Moving;
    Position first;
    GroundedStepFailure const firstFailure = PeekGroundedStepFailure(player, HeartbeatStepLen(player), first);
    if (firstFailure != GroundedStepFailure::None)
    {
        _path.clear();
        _state = wasIn;
        // Read by the look that is still going on. A refused step while she already walks a way round is not a look.
        if (wasIn == State::LookingForAWayRound)
            _wayRoundFirstStepRefused = true;
        PLAYERBOT_LOG_DETAIL(player,
            "mod-playerbots: {} did not take a way round ({}): her first step from ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f}) is refused ({}).",
            player->GetName(), what, _lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(), _lastGrounded.GetPositionZ(),
            first.GetPositionX(), first.GetPositionY(), first.GetPositionZ(), GroundedStepFailureName(firstFailure));
        return false;
    }

    bool const alreadyMoving = wasIn == State::Moving;
    if (wasIn == State::LookingForAWayRound)
        EndWayRoundTurn(PlayerbotWayRoundEnd::Found);
    _walkingAWayRound = true;
    _wayRoundTarget = way.Target;
    // This walk is her own, not a rejoin of the route that refused her.
    ClearFaceRecovery();
    Position pose = _lastGrounded;
    pose.SetOrientation(Position::NormalizeOrientation(std::atan2(_path[1].y - _path[0].y, _path[1].x - _path[0].x)));
    _lastGrounded.SetOrientation(pose.GetOrientation());
    QueueMove(player, pose, true, !alreadyMoving);
    PLAYERBOT_LOG_DETAIL(player,
        "mod-playerbots: {} {}: {:.1f} yards of walking to ({:.2f}, {:.2f}, {:.2f}), {:.1f} yards closer to where she is going{}.",
        player->GetName(), what, way.Yards, points.back()[0], points.back()[1], points.back()[2], way.Gain,
        way.CanWalkBack ? "" : ", on ground she cannot walk back from");
    return true;
}

bool PlayerbotWalker::RepairWayRound(Player* player)
{
    if (!_wayRoundMap || !player || _wayRoundTarget < 0 || _wayRoundRepairs >= WAY_ROUND_MAX_REPAIRS)
        return false;

    std::int32_t const from = _wayRoundMap->FindSpotAt(_lastGrounded.GetPositionX(), _lastGrounded.GetPositionY(),
        _lastGrounded.GetPositionZ());
    PlayerbotWalkMapWayRound way;
    if (!FindPlayerbotWalkMapRoute(*_wayRoundMap, from, _wayRoundTarget, way))
        return false;

    ++_wayRoundRepairs;
    return WalkTheWayRound(player, way, "kept going round the other way");
}

// Her heartbeats land between the spots the map judged, so a way round can still meet a step she may not take. Walking
// the rest of it from where she stands uses the map she already has.
bool PlayerbotWalker::RouteStartsWalkable(Player* player, Position const& from, std::vector<G3D::Vector3> const& path)
{
    if (!player || path.size() < 2)
        return false;

    float const stepLen = std::max(HeartbeatStepLen(player), 0.05f);
    Position feet = from;
    float walked = 0.0f;
    size_t point = 1;
    float progress = 0.0f;
    while (walked + 0.01f < LIP_LOOK_RADIUS && point < path.size())
    {
        G3D::Vector3 const& start = path[point - 1];
        G3D::Vector3 const& end = path[point];
        float const segment = std::sqrt((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y));
        if (segment <= progress + 0.01f)
        {
            ++point;
            progress = 0.0f;
            continue;
        }

        float const take = std::min(stepLen, segment - progress);
        progress += take;
        float const along = progress / segment;
        Position planted;
        if (ClassifyGroundedStep(player, feet, start.x + (end.x - start.x) * along, start.y + (end.y - start.y) * along,
            0.0f, planted) != GroundedStepFailure::None)
            return false;

        feet = planted;
        walked += take;
    }

    return walked > 0.0f;
}

bool PlayerbotWalker::JumpMovementIsAllowed(Player const* player, char const*& reason) const
{
    if (!player || !player->GetSession() || !player->IsInWorld() || !player->IsAlive())
    {
        reason = "the player is not alive in the world";
        return false;
    }
    if (player->IsBeingTeleported() || !player->movespline->Finalized())
    {
        reason = "another movement owner is active";
        return false;
    }
    if (player->HasUnitState(UNIT_STATE_NOT_MOVE))
    {
        reason = "the player cannot move";
        return false;
    }
    if (player->IsInFlight() || player->IsFlying() || player->IsInWater() || player->GetTransport() || player->GetVehicle())
    {
        reason = "the player is not using ordinary grounded movement";
        return false;
    }
    if (player->IsMounted())
    {
        reason = "mounted jump recovery is not validated";
        return false;
    }

    static constexpr MovementFlags refusedFlags = MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR | MOVEMENTFLAG_SWIMMING
        | MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_ROOT | MOVEMENTFLAG_HOVER;
    if (player->m_movementInfo.HasMovementFlag(refusedFlags))
    {
        reason = "the client movement flags are not grounded";
        return false;
    }
    if (std::fabs(player->m_movementInfo.gravityModifier - 1.0f) > 0.001f)
    {
        reason = "modified gravity is not validated";
        return false;
    }

    return true;
}

bool PlayerbotWalker::BuildJumpPlan(Player* player, JumpPlan& out, char const*& reason)
{
    if (!player)
    {
        reason = "there is no player";
        return false;
    }

    Position const launch = _lastGrounded;
    float baseDirX = _lastRefusedStep.GetPositionX() - launch.GetPositionX();
    float baseDirY = _lastRefusedStep.GetPositionY() - launch.GetPositionY();
    float const dirLen = std::sqrt(baseDirX * baseDirX + baseDirY * baseDirY);
    if (dirLen < 0.05f)
    {
        reason = "the rejected step has no forward direction";
        return false;
    }
    baseDirX /= dirLen;
    baseDirY /= dirLen;

    PlayerbotJumpTrajectory trajectory;
    trajectory.HorizontalSpeed = player->GetSpeed(MOVE_RUN);
    trajectory.VerticalSpeed = NORMAL_JUMP_VERTICAL_SPEED;
    trajectory.Gravity = Movement::gravity;
    if (trajectory.HorizontalSpeed < 0.05f)
    {
        reason = "the player has no run speed";
        return false;
    }

    float const maxTime = trajectory.DescendingTimeToHeight(-JUMP_MAX_LANDING_DROP);
    float const apexTime = trajectory.ApexTime();
    float const refusedDistance = launch.GetExactDist2d(_lastRefusedStep);
    float const headingOffsetsDegrees[] = { 0.0f, 15.0f, -15.0f, 30.0f, -30.0f };
    char const* firstReason = "the normal jump has no safe landing";
    bool haveFirstReason = false;

    // A player can angle a normal jump around the high side of a lip. Every candidate still needs a clear body arc,
    // a verified floor, ordinary ground after landing, and an mmap continuation to the original destination.
    for (float headingOffsetDegrees : headingOffsetsDegrees)
    {
        float const headingOffset = headingOffsetDegrees * (float(M_PI) / 180.0f);
        float const rotateCos = std::cos(headingOffset);
        float const rotateSin = std::sin(headingOffset);
        float const dirX = baseDirX * rotateCos - baseDirY * rotateSin;
        float const dirY = baseDirX * rotateSin + baseDirY * rotateCos;
        float const orientation = Position::NormalizeOrientation(std::atan2(dirY, dirX));
        char const* candidateReason = "the normal jump has no safe landing";
        bool rejected = false;
        Position previous = launch;
        Position landing;
        float landingTime = 0.0f;

        for (float time = JUMP_ARC_SAMPLE_SECONDS; time <= maxTime + 0.001f; time += JUMP_ARC_SAMPLE_SECONDS)
        {
            float const horizontal = trajectory.HorizontalDistance(time);
            float const x = launch.GetPositionX() + dirX * horizontal;
            float const y = launch.GetPositionY() + dirY * horizontal;
            float const arcZ = launch.GetPositionZ() + trajectory.HeightOffset(time);
            if (!Trinity::IsValidMapCoord(x, y, arcZ))
            {
                candidateReason = "the jump arc leaves valid map coordinates";
                rejected = true;
                break;
            }

            float const floorZ = player->GetMapHeight(x, y, arcZ + JUMP_FLOOR_SEARCH_ABOVE_FEET);
            if (floorZ <= INVALID_HEIGHT)
            {
                candidateReason = "the jump arc has no verified floor";
                rejected = true;
                break;
            }
            if (floorZ - launch.GetPositionZ() > JUMP_MAX_LANDING_RISE
                || launch.GetPositionZ() - floorZ > JUMP_MAX_LANDING_DROP)
            {
                candidateReason = "the jump arc crosses an unsafe rise or drop";
                rejected = true;
                break;
            }

            Position arc;
            arc.Relocate(x, y, arcZ, orientation);
            if (!JumpSegmentIsClear(player, previous, arc))
            {
                candidateReason = "the full-body jump sweep hits world collision";
                rejected = true;
                break;
            }

            float const footClearance = arcZ - floorZ;
            if (time <= apexTime)
            {
                if (footClearance < -JUMP_ASCENT_GROUND_TOLERANCE)
                {
                    if (headingOffsetDegrees == 0.0f)
                        PLAYERBOT_LOG_DETAIL(player,
                            "mod-playerbots: {} direct jump sweep met uphill ground at {:.3f} seconds and {:.2f} yards (arc Z {:.2f}, floor Z {:.2f}, clearance {:.2f}). Trying nearby headings.",
                            player->GetName(), time, horizontal, arcZ, floorZ, footClearance);
                    candidateReason = "the uphill face intersects the ascending jump arc";
                    rejected = true;
                    break;
                }
            }
            else if (footClearance <= JUMP_FOOT_CLEARANCE)
            {
                if (footClearance < -0.25f)
                {
                    candidateReason = "the descending jump passed below the landing floor";
                    rejected = true;
                    break;
                }
                landing.Relocate(x, y, floorZ, arc.GetOrientation());
                landingTime = time;
                break;
            }

            previous = arc;
        }

        if (!rejected && landingTime <= 0.0f)
        {
            candidateReason = "the normal jump has no safe landing";
            rejected = true;
        }
        if (!rejected && launch.GetExactDist2d(landing) < refusedDistance + JUMP_MIN_CLEARANCE_PAST_FACE)
        {
            candidateReason = "the landing does not clear the rejected step";
            rejected = true;
        }

        if (!rejected)
        {
            LiquidData liquid;
            ZLiquidStatus const liquidStatus = player->GetMap()->GetLiquidStatus(player->GetPhaseShift(),
                landing.GetPositionX(), landing.GetPositionY(), landing.GetPositionZ(), {}, &liquid, player->GetCollisionHeight());
            if (liquidStatus & MAP_LIQUID_STATUS_IN_CONTACT)
            {
                candidateReason = "the landing is in liquid";
                rejected = true;
            }
        }

        if (!rejected && !LookAheadIsWalkable(player, landing, dirX, dirY, LIP_LOOK_RADIUS))
        {
            candidateReason = "there is no ordinary ground beyond the landing";
            rejected = true;
        }

        std::vector<G3D::Vector3> continuation;
        if (!rejected && !BuildMmapPath(player, landing, _destination, continuation))
        {
            candidateReason = "mmap has no continuation from the landing";
            rejected = true;
        }

        if (rejected)
        {
            if (!haveFirstReason)
            {
                firstReason = candidateReason;
                haveFirstReason = true;
            }
            continue;
        }

        out.Launch = launch;
        out.Launch.SetOrientation(orientation);
        out.Landing = landing;
        out.Trajectory = trajectory;
        out.DirectionX = dirX;
        out.DirectionY = dirY;
        out.DurationMs = uint32(std::ceil(landingTime * 1000.0f));
        if (headingOffsetDegrees != 0.0f)
            PLAYERBOT_LOG_DETAIL(player,
                "mod-playerbots: {} found a clear normal-jump heading {:.0f} degrees beside the rejected step.",
                player->GetName(), headingOffsetDegrees);
        return true;
    }

    reason = firstReason;
    return false;
}

bool PlayerbotWalker::TryStartJump(Player* player)
{
    bool const jumpableFailure = _lastGroundedStepFailure == GroundedStepFailure::SteepUp
        || _lastGroundedStepFailure == GroundedStepFailure::StaticCollision;
    if (!jumpableFailure || !_faceRecovery.Active() || _faceRecovery.JumpAttempted())
        return false;

    char const* reason = "the jump was not valid";
    if (!JumpMovementIsAllowed(player, reason))
        return false;

    JumpPlan plan;
    if (!BuildJumpPlan(player, plan, reason))
    {
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} did not jump this uphill lip: {}.", player->GetName(), reason);
        return false;
    }

    _faceRecovery.MarkJumpAttempted();
    _jump = plan;
    _jumpElapsedMs = 0;
    _jumpHeartbeatMs = 0;
    _jumpMapId = player->GetMapId();
    _stopAfterJump = false;
    _contouring = false;
    bool const alreadyMoving = _state == State::Moving;
    _state = State::Jumping;
    if (!alreadyMoving)
        QueueMove(player, _jump.Launch, true, true);
    QueueJumpMove(player, CMSG_MOVE_JUMP, _jump.Launch, 0);
    PLAYERBOT_LOG_DETAIL(player,
        "mod-playerbots: {} queued a normal jump over the uphill lip ({:.1f} yards, {} ms) with a verified landing and continuation.",
        player->GetName(), _jump.Launch.GetExactDist2d(_jump.Landing), _jump.DurationMs);
    return true;
}

bool PlayerbotWalker::FlightMovementIsAllowed(Player const* player, char const*& reason) const
{
    if (!player || !player->GetSession() || !player->IsInWorld() || !player->IsAlive())
    {
        reason = "the player is not alive in the world";
        return false;
    }
    if (player->IsBeingTeleported())
    {
        reason = "a teleport is waiting for its reply";
        return false;
    }
    if (!player->movespline->Finalized())
    {
        reason = "a server spline is moving the player";
        return false;
    }
    if (player->IsInFlight() || player->GetTransport() || player->GetVehicle())
    {
        reason = "the player is not using ordinary movement";
        return false;
    }

    static constexpr MovementFlags refusedFlags = MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FLYING
        | MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_HOVER;
    if (player->m_movementInfo.HasMovementFlag(refusedFlags))
    {
        reason = "swimming, flying, and hovering arcs are not validated";
        return false;
    }
    if (std::fabs(player->m_movementInfo.gravityModifier - 1.0f) > 0.001f)
    {
        reason = "modified gravity is not validated";
        return false;
    }

    return true;
}

Position PlayerbotWalker::ArcPosition(JumpPlan const& plan, uint32 timeMs)
{
    uint32 const sidewaysMs = std::min({ timeMs, plan.SidewaysStopMs, plan.CeilingMs });
    float const horizontal = plan.Trajectory.HorizontalDistance(float(sidewaysMs) / 1000.0f);

    float height = 0.0f;
    if (timeMs <= plan.CeilingMs)
        height = plan.Trajectory.HeightOffset(float(timeMs) / 1000.0f);
    else
    {
        PlayerbotJumpTrajectory fromRest;
        fromRest.Gravity = plan.Trajectory.Gravity;
        fromRest.TerminalVelocity = plan.Trajectory.TerminalVelocity;
        height = plan.Trajectory.HeightOffset(float(plan.CeilingMs) / 1000.0f)
            + fromRest.HeightOffset(float(timeMs - plan.CeilingMs) / 1000.0f);
    }

    Position pos;
    pos.Relocate(plan.Launch.GetPositionX() + plan.DirectionX * horizontal,
        plan.Launch.GetPositionY() + plan.DirectionY * horizontal,
        plan.Launch.GetPositionZ() + height, plan.Launch.GetOrientation());
    return pos;
}

// Follows the arc from fromMs until she lands on a floor, touches water deep enough to swim in, or falls out of the world.
// A wall stops her sideways movement and she keeps falling beside it. A ceiling stops her rise and she falls from there.
bool PlayerbotWalker::SampleFlight(Player* player, JumpPlan& plan, uint32 fromMs, char const*& reason)
{
    Map* map = player ? player->FindMap() : nullptr;
    if (!map)
    {
        reason = "the player is not on a map";
        return false;
    }

    Position previous = ArcPosition(plan, fromMs);
    if (!Trinity::IsValidMapCoord(previous.GetPositionX(), previous.GetPositionY(), previous.GetPositionZ()))
    {
        reason = "the arc does not start at a valid position";
        return false;
    }

    float const collisionHeight = player->GetCollisionHeight();
    plan.EndsInWater = false;
    plan.EndsBelowWorld = false;

    uint32 timeMs = fromMs + FLIGHT_SAMPLE_MS;
    for (; timeMs <= FLIGHT_MAX_MS; timeMs += FLIGHT_SAMPLE_MS)
    {
        uint32 const previousMs = timeMs - FLIGHT_SAMPLE_MS;
        Position arc = ArcPosition(plan, timeMs);

        bool const rising = timeMs <= plan.CeilingMs
            && plan.Trajectory.VerticalVelocity(float(previousMs) / 1000.0f) > 0.0f;
        if (rising && FlightHeadHits(player, previous, arc))
        {
            plan.CeilingMs = previousMs;
            arc = ArcPosition(plan, timeMs);
        }

        bool const sideways = timeMs <= std::min(plan.SidewaysStopMs, plan.CeilingMs)
            && plan.Trajectory.HorizontalDistance(float(FLIGHT_SAMPLE_MS) / 1000.0f) > 0.001f;
        if (sideways && (!Trinity::IsValidMapCoord(arc.GetPositionX(), arc.GetPositionY(), arc.GetPositionZ())
            || !JumpSegmentIsClear(player, previous, arc)))
        {
            plan.SidewaysStopMs = previousMs;
            arc = ArcPosition(plan, timeMs);
        }

        if (!Trinity::IsValidMapCoord(arc.GetPositionX(), arc.GetPositionY(), arc.GetPositionZ()))
            break;

        LiquidData liquid;
        ZLiquidStatus const liquidStatus = map->GetLiquidStatus(player->GetPhaseShift(),
            arc.GetPositionX(), arc.GetPositionY(), arc.GetPositionZ(), {}, &liquid, collisionHeight);
        if ((liquidStatus & MAP_LIQUID_STATUS_IN_CONTACT) && PlayerbotWaterIsSwimDepth(liquid.level, liquid.depth_level, collisionHeight))
        {
            plan.Landing.Relocate(arc.GetPositionX(), arc.GetPositionY(), liquid.level, arc.GetOrientation());
            plan.DurationMs = timeMs;
            plan.EndsInWater = true;
            return true;
        }

        float const floorZ = player->GetMapHeight(arc.GetPositionX(), arc.GetPositionY(),
            arc.GetPositionZ() + JUMP_FLOOR_SEARCH_ABOVE_FEET);
        bool const descending = timeMs > plan.CeilingMs
            || plan.Trajectory.VerticalVelocity(float(timeMs) / 1000.0f) <= 0.0f;
        if (floorZ > INVALID_HEIGHT
            && (arc.GetPositionZ() < floorZ || (descending && arc.GetPositionZ() <= floorZ + JUMP_FOOT_CLEARANCE)))
        {
            plan.Landing.Relocate(arc.GetPositionX(), arc.GetPositionY(), floorZ, arc.GetOrientation());
            plan.DurationMs = timeMs;
            return true;
        }

        previous = arc;
        if (arc.GetPositionZ() < map->GetMinHeight(player->GetPhaseShift(), arc.GetPositionX(), arc.GetPositionY()))
        {
            timeMs += FLIGHT_SAMPLE_MS;
            break;
        }
    }

    // No floor caught her. Her last arc point is below the world or the fall ran past the longest one followed; the
    // server ends a fall below the world the way it does for any player.
    plan.Landing = previous;
    plan.DurationMs = std::max(fromMs, timeMs - FLIGHT_SAMPLE_MS);
    plan.EndsBelowWorld = true;
    return true;
}

bool PlayerbotWalker::TryWalkOffLedge(Player* player)
{
    char const* reason = "walking off was not valid";
    if (!JumpMovementIsAllowed(player, reason))
    {
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} does not walk off an edge: {}.", player ? player->GetName() : "", reason);
        return false;
    }

    struct Candidate
    {
        JumpPlan Plan;
        float Drop = 0.0f;
        uint32 Damage = 0;
    };

    Position const feet = _lastGrounded;
    float const stepLen = HeartbeatStepLen(player);
    // The first heartbeat each way was refused, so an edge worth walking off is inside it.
    float const maxEdge = std::max(stepLen, WALK_OFF_EDGE_STEP_YARDS);
    uint32 const health = player->GetHealth();
    uint32 const maxHealth = player->GetMaxHealth();
    std::vector<Candidate> candidates;
    // Why each edge she found was not walked off, for the log.
    std::string refused;
    auto refuse = [&](float angle, char const* why, Position const* landing)
    {
        if (!refused.empty())
            refused += ", ";
        refused += Trinity::StringFormat("{:.0f} degrees {}", angle * 180.0f / float(M_PI), why);
        if (landing)
            refused += Trinity::StringFormat(" (lands at {:.2f}, {:.2f}, {:.2f})", landing->GetPositionX(), landing->GetPositionY(),
                landing->GetPositionZ());
    };

    for (int32 i = 0; i < WALK_OFF_DIRECTIONS; ++i)
    {
        float const angle = float(i) * 2.0f * float(M_PI) / float(WALK_OFF_DIRECTIONS);
        float const dirX = std::cos(angle);
        float const dirY = std::sin(angle);

        // Feel along the ground for where it ends in a drop she may not step down.
        Position edge = feet;
        float walked = 0.0f;
        float over = 0.0f;
        while (walked + 0.001f < maxEdge)
        {
            float const next = std::min(maxEdge, walked + WALK_OFF_EDGE_STEP_YARDS);
            Position out;
            GroundedStepFailure const failure = ClassifyGroundedStep(player, edge, feet.GetPositionX() + dirX * next,
                feet.GetPositionY() + dirY * next, angle, out);
            if (failure == GroundedStepFailure::None)
            {
                edge = out;
                walked = next;
                continue;
            }
            if (failure == GroundedStepFailure::TooFarDown)
                over = next;
            break;
        }
        if (over <= 0.0f)
            continue;

        // Close in on the edge, so the fall starts where the ground ends and not on the ledge.
        for (uint32 k = 0; k < WALK_OFF_EDGE_REFINE_STEPS; ++k)
        {
            float const mid = (walked + over) * 0.5f;
            Position out;
            GroundedStepFailure const failure = ClassifyGroundedStep(player, edge, feet.GetPositionX() + dirX * mid,
                feet.GetPositionY() + dirY * mid, angle, out);
            if (failure == GroundedStepFailure::None)
            {
                edge = out;
                walked = mid;
            }
            else if (failure == GroundedStepFailure::TooFarDown)
                over = mid;
            else
                break;
        }

        Candidate candidate;
        JumpPlan& plan = candidate.Plan;
        plan.Launch = edge;
        plan.Launch.SetOrientation(Position::NormalizeOrientation(angle));
        plan.DirectionX = dirX;
        plan.DirectionY = dirY;
        plan.Trajectory.HorizontalSpeed = player->GetSpeed(MOVE_RUN);
        plan.Trajectory.VerticalSpeed = 0.0f;
        plan.Trajectory.Gravity = Movement::gravity;
        plan.Trajectory.TerminalVelocity = player->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FALLING_SLOW)
            ? PLAYERBOT_FEATHER_FALL_TERMINAL_SPEED
            : PLAYERBOT_TERMINAL_FALL_SPEED;
        plan.WalkOff = true;
        char const* flightReason = "the fall could not be followed";
        if (!SampleFlight(player, plan, 0, flightReason))
        {
            refuse(angle, flightReason, nullptr);
            continue;
        }
        if (plan.EndsBelowWorld)
        {
            refuse(angle, "has no floor under the fall", nullptr);
            continue;
        }

        candidate.Drop = edge.GetPositionZ() - plan.Landing.GetPositionZ();
        if (!plan.EndsInWater)
        {
            // Caught again on the ground she stands on: that is not off the ledge.
            if (candidate.Drop <= MAX_DOWN_STEP_YARDS)
            {
                refuse(angle, "lands on the ledge again", &plan.Landing);
                continue;
            }
            if (!HasHeadroom(player, plan.Landing))
            {
                refuse(angle, "lands where her body has no room", &plan.Landing);
                continue;
            }

            // Do not fall into a pocket as tight as this one.
            bool canStep = false;
            for (uint32 j = 0; j < 8 && !canStep; ++j)
            {
                float const stepAngle = float(j) * float(M_PI) / 4.0f;
                Position out;
                canStep = ClassifyGroundedStep(player, plan.Landing, plan.Landing.GetPositionX() + std::cos(stepAngle) * stepLen,
                    plan.Landing.GetPositionY() + std::sin(stepAngle) * stepLen, stepAngle, out) == GroundedStepFailure::None;
            }
            if (!canStep)
            {
                refuse(angle, "lands where she cannot step anywhere either", &plan.Landing);
                continue;
            }

            candidate.Damage = PredictedFallDamage(player, candidate.Drop);
        }

        if (candidate.Damage >= health
            || uint64(health - candidate.Damage) * 100 <= uint64(maxHealth) * WALK_OFF_MIN_HEALTH_PCT_AFTER)
        {
            refuse(angle, "would leave her at half health or less", &plan.Landing);
            continue;
        }

        candidates.push_back(candidate);
    }

    if (candidates.empty())
    {
        // Once for this place, unless her steps are logged.
        bool const again = _haveWalkOffExplained && _walkOffExplained.GetExactDist(feet) < 1.0f;
        if (again && !PlayerbotLogDetail::IsNamed(player->GetName()))
            return false;
        _walkOffExplained = feet;
        _haveWalkOffExplained = true;

        if (refused.empty())
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} has no edge to walk off at ({:.2f}, {:.2f}, {:.2f}): no heading ends in a drop she may not step down.",
                player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ());
        else
            TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} does not walk off an edge at ({:.2f}, {:.2f}, {:.2f}). Each heading that ends in a drop: {}.",
                player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(), refused);
        return false;
    }

    // The shortest drop that the navmesh can route on from; otherwise the shortest drop.
    std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.Drop < b.Drop; });
    Candidate const* chosen = &candidates.front();
    bool routeOn = false;
    for (size_t i = 0; i < candidates.size() && i < WALK_OFF_ROUTE_CHECKS; ++i)
    {
        std::vector<G3D::Vector3> continuation;
        if (BuildMmapPath(player, candidates[i].Plan.Landing, _destination, continuation))
        {
            chosen = &candidates[i];
            routeOn = true;
            break;
        }
    }

    _jump = chosen->Plan;
    _jumpElapsedMs = 0;
    _jumpHeartbeatMs = 0;
    _jumpMapId = player->GetMapId();
    _stopAfterJump = false;
    _skipNextJumpDiff = true;
    _contouring = false;
    _failedAtHerFeet = false;
    _state = State::Jumping;

    // Forward from her feet, one heartbeat on the last ground before the drop, then falling heartbeats from there.
    Position start = feet;
    start.SetOrientation(_jump.Launch.GetOrientation());
    QueueMove(player, start, true, true);
    if (_jump.Launch.GetExactDist2d(start) > 0.01f)
        QueueMove(player, _jump.Launch, true, false);
    _lastGrounded = _jump.Launch;

    char const* ending = _jump.EndsInWater ? "into deep water" : "onto the ground";
    TC_LOG_INFO(PLAYERBOTS_LOG,
        "mod-playerbots: {} cannot step anywhere from ({:.2f}, {:.2f}, {:.2f}), so she walks off the edge at ({:.2f}, {:.2f}, {:.2f}) and falls {:.1f} yards {} at ({:.2f}, {:.2f}, {:.2f}), expecting {} fall damage of {} health. {}",
        player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(), _jump.Launch.GetPositionX(),
        _jump.Launch.GetPositionY(), _jump.Launch.GetPositionZ(), chosen->Drop, ending, _jump.Landing.GetPositionX(),
        _jump.Landing.GetPositionY(), _jump.Landing.GetPositionZ(), chosen->Damage, health,
        routeOn ? "The navmesh has a route on from there." : "The navmesh has no route on from there; she looks again after landing.");
    return true;
}

void PlayerbotWalker::UpdateJump(Player* player, uint32 diff)
{
    float const allowedDrift = std::max(FLIGHT_MIN_ALLOWED_DRIFT, _jump.Trajectory.HorizontalSpeed * FLIGHT_DRIFT_SECONDS);
    if (!player || !player->GetSession() || !player->IsAlive() || !player->IsInWorld()
        || player->IsBeingTeleported() || player->GetMapId() != _jumpMapId
        || !player->movespline->Finalized() || player->GetExactDist2d(_lastGrounded) > allowedDrift)
    {
        ResetNow();
        return;
    }

    if (_skipNextJumpDiff)
    {
        _skipNextJumpDiff = false;
        return;
    }

    uint32 const remainingMs = _jump.DurationMs > _jumpElapsedMs ? _jump.DurationMs - _jumpElapsedMs : 0;
    uint32 const advanceMs = std::min(diff, remainingMs);
    _jumpElapsedMs += advanceMs;
    _jumpHeartbeatMs += advanceMs;

    while (_jumpHeartbeatMs >= HEARTBEAT_INTERVAL_MS)
    {
        _jumpHeartbeatMs -= HEARTBEAT_INTERVAL_MS;
        uint32 const heartbeatTimeMs = _jumpElapsedMs - _jumpHeartbeatMs;
        if (heartbeatTimeMs >= _jump.DurationMs)
            break;

        Position const airborne = ArcPosition(_jump, heartbeatTimeMs);
        QueueJumpMove(player, CMSG_MOVE_HEARTBEAT, airborne, heartbeatTimeMs);
        _lastGrounded = airborne;
    }

    if (_jumpElapsedMs >= _jump.DurationMs)
        FinishJump(player);
}

void PlayerbotWalker::FinishJump(Player* player)
{
    Position const landing = _jump.Landing;
    uint32 const durationMs = _jump.DurationMs;
    bool const stopAfterLanding = _stopAfterJump;

    if (_jump.EndsBelowWorld)
    {
        QueueJumpMove(player, CMSG_MOVE_HEARTBEAT, landing, durationMs);
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} found no floor under her fall and sent her last falling heartbeat at ({:.2f}, {:.2f}, {:.2f}).",
            player->GetName(), landing.GetPositionX(), landing.GetPositionY(), landing.GetPositionZ());
        ResetNow();
        return;
    }

    if (_jump.Knockback || _jump.EndsInWater)
    {
        bool const water = _jump.EndsInWater;
        QueueJumpMove(player, water ? CMSG_MOVE_START_SWIM : CMSG_MOVE_FALL_LAND, landing, durationMs);
        if (water)
            PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} touched deep water at ({:.2f}, {:.2f}, {:.2f}) after {} ms and queued CMSG_MOVE_START_SWIM.",
                player->GetName(), landing.GetPositionX(), landing.GetPositionY(), landing.GetPositionZ(), durationMs);
        else
            PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} landed the knockback at ({:.2f}, {:.2f}, {:.2f}) after {} ms and queued CMSG_MOVE_FALL_LAND.",
                player->GetName(), landing.GetPositionX(), landing.GetPositionY(), landing.GetPositionZ(), durationMs);
        ResetNow();
        return;
    }

    QueueJumpMove(player, CMSG_MOVE_FALL_LAND, landing, durationMs);
    _lastGrounded = landing;
    _jumpElapsedMs = 0;
    _jumpHeartbeatMs = 0;
    _jumpMapId = 0;
    _stopAfterJump = false;
    _state = State::Moving;

    if (_jump.WalkOff)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} landed at ({:.2f}, {:.2f}, {:.2f}) after walking off the edge and queued CMSG_MOVE_FALL_LAND; the server applies any fall damage.",
            player->GetName(), landing.GetPositionX(), landing.GetPositionY(), landing.GetPositionZ());
    else
        PLAYERBOT_LOG_DETAIL(player, "mod-playerbots: {} landed the obstacle-recovery jump at ({:.2f}, {:.2f}, {:.2f}).",
            player->GetName(), landing.GetPositionX(), landing.GetPositionY(), landing.GetPositionZ());

    if (stopAfterLanding)
    {
        QueueMove(player, landing, false, false);
        ResetNow();
        return;
    }
    if (landing.GetExactDist(_destination) <= _stopDistance)
    {
        FinishGroundedArrival(player, landing);
        _jump = {};
        return;
    }
    if (TryCommitMmap(player, landing, true))
    {
        _jump = {};
        return;
    }
    if (TryLeaveFace(player, true))
    {
        _jump = {};
        return;
    }

    _jump = {};
    FailNoLegalRing(player);
}

void PlayerbotWalker::NoteBadPlace(Player* player)
{
    if (!player)
        return;

    uint64 const nowMs = uint64(GameTime::GetGameTime()) * IN_MILLISECONDS;
    auto note = [&](Position const& feet, Position const& refused)
    {
        _badPlaces.Note(player->GetMapId(), refused.GetPositionX(), refused.GetPositionY(), refused.GetPositionZ(),
            refused.GetPositionX() - feet.GetPositionX(), refused.GetPositionY() - feet.GetPositionY(), nowMs);
    };

    // Where the navmesh route first met the obstacle, and where the recovery that just failed began, which is often a
    // step of a way round beside it.
    if (_haveApproachRefusal)
        note(_approachFeet, _approachRefused);
    if (_haveEpisodeRefusal && _episodeRefused.GetExactDist(_approachRefused) > PLAYERBOT_BAD_PLACE_YARDS)
        note(_episodeFeet, _episodeRefused);

    if (_haveApproachRefusal)
        TC_LOG_INFO(PLAYERBOTS_LOG,
            "mod-playerbots: {} remembers ({:.2f}, {:.2f}, {:.2f}) as a place she could not get past going this way, and takes no route past it that way for {} minutes.",
            player->GetName(), _approachRefused.GetPositionX(), _approachRefused.GetPositionY(), _approachRefused.GetPositionZ(),
            PLAYERBOT_BAD_PLACE_FORGET_MS / MINUTE / IN_MILLISECONDS);
}

void PlayerbotWalker::ExplainStuckFeet(Player* player, Position const& feet)
{
    if (!player)
        return;

    bool const again = _haveStuckFeetExplained && _stuckFeetExplained.GetExactDist(feet) < 1.0f;
    if (again && !PlayerbotLogDetail::IsNamed(player->GetName()))
        return;
    _stuckFeetExplained = feet;
    _haveStuckFeetExplained = true;

    static char const* const directions[] = { "east", "north-east", "north", "north-west", "west", "south-west", "south", "south-east" };
    float const stepLen = HeartbeatStepLen(player);
    std::string steps;
    for (uint32 i = 0; i < 8; ++i)
    {
        float const angle = float(i) * float(M_PI) / 4.0f;
        Position out;
        GroundedStepFailure const failure = ClassifyGroundedStep(player, feet, feet.GetPositionX() + std::cos(angle) * stepLen,
            feet.GetPositionY() + std::sin(angle) * stepLen, angle, out);
        if (!steps.empty())
            steps += ", ";
        steps += Trinity::StringFormat("{} {}", directions[i],
            failure == GroundedStepFailure::None ? "walkable" : GroundedStepFailureName(failure));
        if (failure != GroundedStepFailure::NoFloor)
            steps += Trinity::StringFormat(" (floor {:.2f})", out.GetPositionZ());
    }

    TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} cannot step from ({:.2f}, {:.2f}, {:.2f}), her body {} room there. One step each way: {}.",
        player->GetName(), feet.GetPositionX(), feet.GetPositionY(), feet.GetPositionZ(),
        HasHeadroom(player, feet) ? "has" : "has no", steps);
}

void PlayerbotWalker::FailNoLegalRing(Player* player)
{
    if (_state == State::Moving && player && player->GetSession())
        QueueMove(player, _lastGrounded, false, false);

    NoteBadPlace(player);
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
        QueueMove(player, _lastGrounded, false, false);

    _state = State::Failed;
    _contouring = false;
    if (player)
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: {} stopped walking: {}.", player->GetName(), reason);
    else
        TC_LOG_INFO(PLAYERBOTS_LOG, "mod-playerbots: walk failed: {}.", reason);
}

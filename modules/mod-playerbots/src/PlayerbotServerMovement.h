/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef EVRY_MOD_PLAYERBOT_SERVER_MOVEMENT_H
#define EVRY_MOD_PLAYERBOT_SERVER_MOVEMENT_H

#include "ByteBuffer.h"
#include "MovementInfo.h"
#include "ObjectGuid.h"
#include "Opcodes.h"
#include "Position.h"
#include "WorldPacket.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// A movement order the server sent her client, which a real client answers.
enum class PlayerbotServerOrderKind : std::uint8_t
{
    Root,
    Unroot,
    KnockBack,
    Teleport,
    SuspendToken,
    NewWorld
};

inline char const* PlayerbotServerOrderKindName(PlayerbotServerOrderKind kind)
{
    switch (kind)
    {
        case PlayerbotServerOrderKind::Root:
            return "root";
        case PlayerbotServerOrderKind::Unroot:
            return "unroot";
        case PlayerbotServerOrderKind::KnockBack:
            return "knockback";
        case PlayerbotServerOrderKind::Teleport:
            return "teleport";
        case PlayerbotServerOrderKind::SuspendToken:
            return "suspend token";
        case PlayerbotServerOrderKind::NewWorld:
            return "new world";
    }

    return "unknown";
}

struct PlayerbotServerOrder
{
    PlayerbotServerOrderKind Kind = PlayerbotServerOrderKind::Root;
    // The unit whose movement the order is for. Empty for the map change packets, which name no unit.
    ObjectGuid Mover;
    // The number her reply must echo.
    std::uint32_t SequenceIndex = 0;
    // Came inside the combined movement state packet the server sends after login or a map change.
    bool FromCompoundState = false;
    // Knockback: the push direction and both speeds, as sent. The vertical speed is negative going up.
    float DirectionX = 0.0f;
    float DirectionY = 0.0f;
    float HorizontalSpeed = 0.0f;
    float VerticalSpeed = 0.0f;
    // Teleport: where the server is putting her.
    Position Destination;
};

namespace PlayerbotServerMovementDetail
{
    constexpr std::uint32_t MaxCompoundStateChanges = 64;

    // Skips the optional data of one entry in the combined movement state packet. False when that data cannot be
    // skipped without knowing more of the packet, so reading has to stop there.
    inline bool SkipStateChangeData(ByteBuffer& data, bool hasSpeed, bool hasRange, bool hasKnockBack, bool hasVehicleRecId,
        bool hasCollisionHeight, bool hasMovementForce, bool hasMovementForceGuid, bool hasInertiaId, bool hasInertiaLifetime,
        bool hasDriveCapability)
    {
        if (hasSpeed)
            data.read_skip<float>();
        if (hasRange)
            data.read_skip(2 * sizeof(float));
        if (hasKnockBack)
            data.read_skip(4 * sizeof(float));
        if (hasVehicleRecId)
            data.read_skip<std::int32_t>();
        if (hasCollisionHeight)
            data.read_skip(2 * sizeof(float) + sizeof(std::uint8_t));
        if (hasMovementForce)
            return false;
        if (hasMovementForceGuid)
        {
            ObjectGuid ignored;
            data >> ignored;
        }
        if (hasInertiaId)
            data.read_skip<std::int32_t>();
        if (hasInertiaLifetime)
            data.read_skip<std::uint32_t>();
        if (hasDriveCapability)
            data.read_skip<std::int32_t>();
        return true;
    }

    inline void ReadCompoundState(ByteBuffer& data, std::vector<PlayerbotServerOrder>& out)
    {
        ObjectGuid mover;
        data >> mover;
        std::uint32_t const count = data.read<std::uint32_t>();
        if (count > MaxCompoundStateChanges)
            return;

        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t const messageId = data.read<std::uint32_t>();
            std::uint32_t const sequenceIndex = data.read<std::uint32_t>();
            bool const hasSpeed = data.ReadBit();
            bool const hasRange = data.ReadBit();
            bool const hasKnockBack = data.ReadBit();
            bool const hasVehicleRecId = data.ReadBit();
            bool const hasCollisionHeight = data.ReadBit();
            bool const hasMovementForce = data.ReadBit();
            bool const hasMovementForceGuid = data.ReadBit();
            bool const hasInertiaId = data.ReadBit();
            bool const hasInertiaLifetime = data.ReadBit();
            bool const hasDriveCapability = data.ReadBit();

            if (messageId == SMSG_MOVE_ROOT || messageId == SMSG_MOVE_UNROOT)
            {
                PlayerbotServerOrder order;
                order.Kind = messageId == SMSG_MOVE_ROOT ? PlayerbotServerOrderKind::Root : PlayerbotServerOrderKind::Unroot;
                order.Mover = mover;
                order.SequenceIndex = sequenceIndex;
                order.FromCompoundState = true;
                out.push_back(order);
            }

            if (!SkipStateChangeData(data, hasSpeed, hasRange, hasKnockBack, hasVehicleRecId, hasCollisionHeight,
                hasMovementForce, hasMovementForceGuid, hasInertiaId, hasInertiaLifetime, hasDriveCapability))
                return;
        }
    }
}

// Reads the movement orders a client has to answer from one packet the server sent. Any other packet adds nothing, and so
// does a packet that cannot be read. Only a handful of opcodes are copied; every other packet returns at the switch.
inline void ReadPlayerbotServerOrders(WorldPacket const& packet, std::vector<PlayerbotServerOrder>& out)
{
    switch (packet.GetOpcode())
    {
        case SMSG_MOVE_ROOT:
        case SMSG_MOVE_UNROOT:
        case SMSG_MOVE_KNOCK_BACK:
        case SMSG_MOVE_TELEPORT:
        case SMSG_SUSPEND_TOKEN:
        case SMSG_NEW_WORLD:
        case SMSG_MOVE_SET_COMPOUND_STATE:
            break;
        default:
            return;
    }

    std::size_t const firstNew = out.size();
    WorldPacket data(packet);
    data.rpos(0);

    try
    {
        PlayerbotServerOrder order;
        switch (data.GetOpcode())
        {
            case SMSG_MOVE_ROOT:
            case SMSG_MOVE_UNROOT:
                order.Kind = data.GetOpcode() == SMSG_MOVE_ROOT ? PlayerbotServerOrderKind::Root : PlayerbotServerOrderKind::Unroot;
                data >> order.Mover;
                order.SequenceIndex = data.read<std::uint32_t>();
                out.push_back(order);
                break;
            case SMSG_MOVE_KNOCK_BACK:
                order.Kind = PlayerbotServerOrderKind::KnockBack;
                data >> order.Mover;
                order.SequenceIndex = data.read<std::uint32_t>();
                order.DirectionX = data.read<float>();
                order.DirectionY = data.read<float>();
                order.HorizontalSpeed = data.read<float>();
                order.VerticalSpeed = data.read<float>();
                out.push_back(order);
                break;
            case SMSG_MOVE_TELEPORT:
            {
                order.Kind = PlayerbotServerOrderKind::Teleport;
                data >> order.Mover;
                order.SequenceIndex = data.read<std::uint32_t>();
                float const x = data.read<float>();
                float const y = data.read<float>();
                float const z = data.read<float>();
                float const facing = data.read<float>();
                order.Destination.Relocate(x, y, z, facing);
                out.push_back(order);
                break;
            }
            case SMSG_SUSPEND_TOKEN:
                order.Kind = PlayerbotServerOrderKind::SuspendToken;
                order.SequenceIndex = data.read<std::uint32_t>();
                out.push_back(order);
                break;
            case SMSG_NEW_WORLD:
                order.Kind = PlayerbotServerOrderKind::NewWorld;
                out.push_back(order);
                break;
            case SMSG_MOVE_SET_COMPOUND_STATE:
                PlayerbotServerMovementDetail::ReadCompoundState(data, out);
                break;
            default:
                break;
        }
    }
    catch (ByteBufferException const&)
    {
        out.resize(firstNew);
    }
}

// One reply the server waits for. She sends it when the order arrives. If the server is somehow still waiting after
// RetryMs, she sends the same reply again.
class PlayerbotServerReply
{
public:
    static constexpr std::uint32_t RetryMs = 2000;

    void Arm(std::uint32_t sequenceIndex, ObjectGuid mover = ObjectGuid::Empty)
    {
        _armed = true;
        _sequenceIndex = sequenceIndex;
        _mover = mover;
        _waitMs = 0;
    }

    void Clear()
    {
        _armed = false;
        _sequenceIndex = 0;
        _mover.Clear();
        _waitMs = 0;
    }

    bool Armed() const { return _armed; }
    std::uint32_t SequenceIndex() const { return _sequenceIndex; }
    ObjectGuid Mover() const { return _mover; }

    // Call once a tick after the first reply is queued. The reply is dropped once the server stops waiting.
    bool ResendDue(bool serverWaiting, std::uint32_t diff)
    {
        if (!_armed)
            return false;

        if (!serverWaiting)
        {
            Clear();
            return false;
        }

        _waitMs += diff;
        if (_waitMs < RetryMs)
            return false;

        _waitMs = 0;
        return true;
    }

private:
    bool _armed = false;
    std::uint32_t _sequenceIndex = 0;
    ObjectGuid _mover;
    std::uint32_t _waitMs = 0;
};

// A 1.12 client starts swimming once the water above its feet is deeper than three quarters of its height. Retail's
// rule may differ; this stands until she learns to swim.
constexpr float PLAYERBOT_SWIM_DEPTH_FRACTION = 0.75f;

inline bool PlayerbotWaterIsSwimDepth(float surfaceZ, float bottomZ, float collisionHeight)
{
    return surfaceZ - bottomZ > PLAYERBOT_SWIM_DEPTH_FRACTION * collisionHeight;
}

// Modes the server grants her, which her client keeps repeating in every movement packet. Keys she presses, falling,
// swimming, and the root are not copied from the server's record of her movement: each packet says what she is doing now.
constexpr MovementFlags PLAYERBOT_CLIENT_MODE_FLAGS = MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_CAN_FLY
    | MOVEMENTFLAG_WATERWALKING | MOVEMENTFLAG_FALLING_SLOW | MOVEMENTFLAG_HOVER | MOVEMENTFLAG_DISABLE_COLLISION
    | MOVEMENTFLAG_CANNOT_SWIM | MOVEMENTFLAG_CAN_SWIM_TO_FLY_TRANS | MOVEMENTFLAG_CAN_TURN_WHILE_FALLING
    | MOVEMENTFLAG_IGNORE_MOVEMENT_FORCES | MOVEMENTFLAG_CAN_DOUBLE_JUMP | MOVEMENTFLAG_CAN_ADV_FLY
    | MOVEMENTFLAG_DISABLE_INERTIA | MOVEMENTFLAG_NO_STRAFE | MOVEMENTFLAG_NO_JUMPING | MOVEMENTFLAG_FULL_SPEED_TURNING
    | MOVEMENTFLAG_FULL_SPEED_PITCHING | MOVEMENTFLAG_ALWAYS_ALLOW_PITCHING;

inline MovementFlags PlayerbotClientModeFlags(MovementFlags serverFlags)
{
    return serverFlags & PLAYERBOT_CLIENT_MODE_FLAGS;
}

#endif

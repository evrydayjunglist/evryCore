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

#include "tc_catch2.h"

#include "MiscPackets.h"
#include "MovementPackets.h"
#include "MovementTypedefs.h"
#include "Timer.h"
#include "../../modules/mod-playerbots/src/PlayerbotJump.h"
#include "../../modules/mod-playerbots/src/PlayerbotServerMovement.h"
#include <thread>

namespace
{
    ObjectGuid PlayerGuid(uint64 counter)
    {
        return ObjectGuid::Create<HighGuid::Player>(counter);
    }

    std::vector<PlayerbotServerOrder> Read(WorldPacket const* packet)
    {
        std::vector<PlayerbotServerOrder> orders;
        ReadPlayerbotServerOrders(*packet, orders);
        return orders;
    }
}

TEST_CASE("Playerbot reads root and unroot orders with their sequence numbers", "[playerbots][server-movement]")
{
    WorldPackets::Movement::MoveSetFlag root(SMSG_MOVE_ROOT);
    root.MoverGUID = PlayerGuid(7);
    root.SequenceIndex = 42;
    std::vector<PlayerbotServerOrder> orders = Read(root.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::Root);
    REQUIRE(orders[0].Mover == PlayerGuid(7));
    REQUIRE(orders[0].SequenceIndex == 42);
    REQUIRE_FALSE(orders[0].FromCompoundState);

    WorldPackets::Movement::MoveSetFlag unroot(SMSG_MOVE_UNROOT);
    unroot.MoverGUID = PlayerGuid(7);
    unroot.SequenceIndex = 43;
    orders = Read(unroot.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::Unroot);
    REQUIRE(orders[0].SequenceIndex == 43);
}

TEST_CASE("Playerbot reads a knockback in the order the server writes it", "[playerbots][server-movement]")
{
    WorldPackets::Movement::MoveKnockBack knockBack;
    knockBack.MoverGUID = PlayerGuid(9);
    knockBack.SequenceIndex = 11;
    knockBack.Direction = Position(0.6f, 0.8f);
    knockBack.Speeds.HorzSpeed = 12.5f;
    knockBack.Speeds.VertSpeed = -7.25f;

    std::vector<PlayerbotServerOrder> const orders = Read(knockBack.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::KnockBack);
    REQUIRE(orders[0].Mover == PlayerGuid(9));
    REQUIRE(orders[0].SequenceIndex == 11);
    REQUIRE(orders[0].DirectionX == Catch::Approx(0.6f));
    REQUIRE(orders[0].DirectionY == Catch::Approx(0.8f));
    REQUIRE(orders[0].HorizontalSpeed == Catch::Approx(12.5f));
    REQUIRE(orders[0].VerticalSpeed == Catch::Approx(-7.25f));
}

TEST_CASE("Playerbot reads a same-map teleport destination and sequence", "[playerbots][server-movement]")
{
    WorldPackets::Movement::MoveTeleport teleport;
    teleport.MoverGUID = PlayerGuid(3);
    teleport.SequenceIndex = 5;
    teleport.Pos = Position(-618.5f, -4251.25f, 38.75f);
    teleport.Facing = 1.5f;

    std::vector<PlayerbotServerOrder> const orders = Read(teleport.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::Teleport);
    REQUIRE(orders[0].Mover == PlayerGuid(3));
    REQUIRE(orders[0].SequenceIndex == 5);
    REQUIRE(orders[0].Destination.GetPositionX() == Catch::Approx(-618.5f));
    REQUIRE(orders[0].Destination.GetPositionY() == Catch::Approx(-4251.25f));
    REQUIRE(orders[0].Destination.GetPositionZ() == Catch::Approx(38.75f));
    REQUIRE(orders[0].Destination.GetOrientation() == Catch::Approx(1.5f));
}

TEST_CASE("Playerbot reads the map change packets", "[playerbots][server-movement]")
{
    WorldPackets::Movement::SuspendToken suspend;
    suspend.SequenceIndex = 77;
    suspend.Reason = 1;
    std::vector<PlayerbotServerOrder> orders = Read(suspend.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::SuspendToken);
    REQUIRE(orders[0].SequenceIndex == 77);
    REQUIRE(orders[0].Mover.IsEmpty());

    WorldPackets::Movement::NewWorld newWorld;
    newWorld.MapID = 1;
    orders = Read(newWorld.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::NewWorld);
}

TEST_CASE("Playerbot reads one order per time sync request, even when a number repeats", "[playerbots][server-movement]")
{
    WorldPackets::Misc::TimeSyncRequest request;
    request.SequenceIndex = 15;
    std::vector<PlayerbotServerOrder> const orders = Read(request.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::TimeSync);
    REQUIRE(orders[0].SequenceIndex == 15);
    REQUIRE(orders[0].Mover.IsEmpty());

    // The server counts from 0 again after a map change that is not seamless, so a request can repeat an earlier number.
    // It is still a new request and still gets its own reply.
    std::vector<PlayerbotServerOrder> answered;
    for (std::uint32_t sequenceIndex : { 0u, 1u, 0u })
    {
        WorldPackets::Misc::TimeSyncRequest next;
        next.SequenceIndex = sequenceIndex;
        ReadPlayerbotServerOrders(*next.Write(), answered);
    }
    REQUIRE(answered.size() == 3);
    REQUIRE(answered[0].SequenceIndex == 0);
    REQUIRE(answered[1].SequenceIndex == 1);
    REQUIRE(answered[2].SequenceIndex == 0);
}

TEST_CASE("Playerbot stamps her time sync and init mover replies after the server registered the request", "[playerbots][server-movement]")
{
    // A world tick caches the clock when it starts. Later in that tick the server registers a request with its live clock,
    // and when the reply arrives it takes the registration away from the reply's receive time.
    TimePoint const tickStart = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(2ms);
    uint32 const registered = getMSTime();

    WorldPackets::Misc::TimeSyncResponse timeSync(PlayerbotTimeSyncResponse(15, 1234));
    timeSync.Read();
    REQUIRE(timeSync.SequenceIndex == 15);
    REQUIRE(timeSync.ClientTime == 1234);
    REQUIRE(getMSTimeDiff(registered, timeSync.GetReceivedTime()) < 1000);

    WorldPackets::Movement::MoveInitActiveMoverComplete initMover(PlayerbotMoveInitActiveMoverComplete(5678));
    initMover.Read();
    REQUIRE(initMover.Ticks == 5678);
    REQUIRE(getMSTimeDiff(registered, initMover.GetRawPacket()->GetReceivedTime()) < 1000);

    // A reply stamped with the time cached when the tick started would be earlier than the registration, and the
    // subtraction would wrap.
    REQUIRE(getMSTimeDiff(registered, tickStart) > 0x7FFFFFFFu);
}

TEST_CASE("Playerbot finds a root inside the combined movement state packet", "[playerbots][server-movement]")
{
    WorldPackets::Movement::MoveSetCompoundState state;
    state.MoverGUID = PlayerGuid(4);
    state.StateChanges.emplace_back(SMSG_MOVE_SET_FEATHER_FALL, 20);
    WorldPackets::Movement::MoveStateChange& withSpeed = state.StateChanges.emplace_back(SMSG_MOVE_SET_RUN_SPEED, 21);
    withSpeed.Speed = 9.0f;
    state.StateChanges.emplace_back(SMSG_MOVE_ROOT, 22);
    state.StateChanges.emplace_back(SMSG_MOVE_SET_WATER_WALK, 23);

    std::vector<PlayerbotServerOrder> const orders = Read(state.Write());
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].Kind == PlayerbotServerOrderKind::Root);
    REQUIRE(orders[0].Mover == PlayerGuid(4));
    REQUIRE(orders[0].SequenceIndex == 22);
    REQUIRE(orders[0].FromCompoundState);
}

TEST_CASE("Playerbot ignores other packets and packets it cannot read", "[playerbots][server-movement]")
{
    WorldPackets::Movement::MoveSetSpeed speed(SMSG_MOVE_SET_RUN_SPEED);
    speed.MoverGUID = PlayerGuid(1);
    speed.SequenceIndex = 1;
    speed.Speed = 7.0f;
    REQUIRE(Read(speed.Write()).empty());

    WorldPacket truncated(SMSG_MOVE_KNOCK_BACK);
    truncated << PlayerGuid(1);
    REQUIRE(Read(&truncated).empty());

    WorldPacket emptyTimeSync(SMSG_TIME_SYNC_REQUEST);
    REQUIRE(Read(&emptyTimeSync).empty());
}

TEST_CASE("Playerbot sends a server reply once and again only while the server still waits", "[playerbots][server-movement]")
{
    PlayerbotServerReply reply;
    REQUIRE_FALSE(reply.ResendDue(true, PlayerbotServerReply::RetryMs));

    reply.Arm(8, PlayerGuid(2));
    REQUIRE(reply.Armed());
    REQUIRE(reply.SequenceIndex() == 8);
    REQUIRE(reply.Mover() == PlayerGuid(2));
    REQUIRE_FALSE(reply.ResendDue(true, 10));
    REQUIRE_FALSE(reply.ResendDue(true, PlayerbotServerReply::RetryMs - 20));
    REQUIRE(reply.ResendDue(true, 10));
    REQUIRE_FALSE(reply.ResendDue(true, 10));

    REQUIRE_FALSE(reply.ResendDue(false, PlayerbotServerReply::RetryMs));
    REQUIRE_FALSE(reply.Armed());
}

TEST_CASE("Playerbot knockback arc falls like the engine's fall model", "[playerbots][server-movement]")
{
    PlayerbotJumpTrajectory arc;
    arc.VerticalSpeed = 7.955547f;
    arc.Gravity = Movement::gravity;
    arc.TerminalVelocity = PLAYERBOT_TERMINAL_FALL_SPEED;

    for (float seconds : { 0.1f, 0.4f, 1.0f, 3.0f, 3.5f, 6.0f, 10.0f })
        REQUIRE(arc.HeightOffset(seconds) == Catch::Approx(-Movement::computeFallElevation(seconds, false, -arc.VerticalSpeed)).margin(0.05f));

    PlayerbotJumpTrajectory featherFall = arc;
    featherFall.TerminalVelocity = PLAYERBOT_FEATHER_FALL_TERMINAL_SPEED;
    for (float seconds : { 0.2f, 0.8f, 1.5f, 4.0f })
        REQUIRE(featherFall.HeightOffset(seconds) == Catch::Approx(-Movement::computeFallElevation(seconds, true, -featherFall.VerticalSpeed)).margin(0.05f));

    REQUIRE(arc.VerticalVelocity(0.0f) == Catch::Approx(arc.VerticalSpeed));
    REQUIRE(arc.VerticalVelocity(20.0f) == Catch::Approx(-PLAYERBOT_TERMINAL_FALL_SPEED));

    PlayerbotJumpTrajectory alreadyFast;
    alreadyFast.VerticalSpeed = -80.0f;
    alreadyFast.Gravity = Movement::gravity;
    alreadyFast.TerminalVelocity = PLAYERBOT_TERMINAL_FALL_SPEED;
    REQUIRE(alreadyFast.HeightOffset(1.0f) == Catch::Approx(-PLAYERBOT_TERMINAL_FALL_SPEED));

    PlayerbotJumpTrajectory plainJump = arc;
    plainJump.TerminalVelocity = 0.0f;
    REQUIRE(plainJump.HeightOffset(0.5f) == Catch::Approx(arc.HeightOffset(0.5f)));
}

TEST_CASE("Playerbot swims only in water deeper than three quarters of her height", "[playerbots][server-movement]")
{
    float const height = 2.0f;
    REQUIRE_FALSE(PlayerbotWaterIsSwimDepth(10.0f, 9.0f, height));
    REQUIRE_FALSE(PlayerbotWaterIsSwimDepth(10.0f, 8.5f, height));
    REQUIRE(PlayerbotWaterIsSwimDepth(10.0f, 8.4f, height));
    REQUIRE(PlayerbotWaterIsSwimDepth(10.0f, -30.0f, height));
}

TEST_CASE("Playerbot movement packets keep granted modes and drop pressed keys, falling, and root", "[playerbots][server-movement]")
{
    MovementFlags const server = MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_WALKING
        | MOVEMENTFLAG_ROOT | MOVEMENTFLAG_FALLING | MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_WATERWALKING
        | MOVEMENTFLAG_FALLING_SLOW | MOVEMENTFLAG_CAN_FLY;

    REQUIRE(PlayerbotClientModeFlags(server)
        == (MOVEMENTFLAG_WATERWALKING | MOVEMENTFLAG_FALLING_SLOW | MOVEMENTFLAG_CAN_FLY));
}

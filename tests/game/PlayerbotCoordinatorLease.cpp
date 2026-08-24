/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotCoordinatorLease.h"

TEST_CASE("Playerbot coordinator loss expires after its grace period", "[playerbots][coordinator]")
{
    PlayerbotCoordinatorLease lease;
    lease.Ensure(1);

    REQUIRE(lease.Disconnect(1));
    REQUIRE(lease.ConnectionId() == 0);
    REQUIRE(lease.DisconnectGraceMs() == PLAYERBOT_COORDINATOR_DISCONNECT_GRACE_MS);
    REQUIRE_FALSE(lease.Update(PLAYERBOT_COORDINATOR_DISCONNECT_GRACE_MS - 1));
    REQUIRE(lease.Update(1));
    REQUIRE_FALSE(lease.Update(1));
}

TEST_CASE("A replacement coordinator makes the old disconnect harmless", "[playerbots][coordinator]")
{
    PlayerbotCoordinatorLease lease;
    lease.Ensure(10);
    lease.Ensure(11);

    REQUIRE_FALSE(lease.Disconnect(10));
    REQUIRE(lease.ConnectionId() == 11);
    REQUIRE(lease.DisconnectGraceMs() == 0);
}

TEST_CASE("A coordinator reconnect during the grace keeps bots online", "[playerbots][coordinator]")
{
    PlayerbotCoordinatorLease lease;
    lease.Ensure(20);
    REQUIRE(lease.Disconnect(20));
    REQUIRE_FALSE(lease.Update(1000));

    lease.Ensure(21);
    REQUIRE(lease.ConnectionId() == 21);
    REQUIRE(lease.DisconnectGraceMs() == 0);
    REQUIRE_FALSE(lease.Update(PLAYERBOT_COORDINATOR_DISCONNECT_GRACE_MS));
}

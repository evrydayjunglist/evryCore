/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotSessionPresence.h"

TEST_CASE("A kicked bot session is noticed as lost", "[playerbots][presence]")
{
    bool const sessionQueued = true;
    bool const sessionSeen = true;
    bool const sessionExists = false;
    bool const moduleLogoutRequested = false;

    REQUIRE(PlayerbotSessionWasLost(sessionQueued, sessionSeen, sessionExists, moduleLogoutRequested));
}

TEST_CASE("A bot session the world has not added yet is not lost", "[playerbots][presence]")
{
    REQUIRE_FALSE(PlayerbotSessionWasLost(true, false, false, false));
}

TEST_CASE("A coordinator-loss logout is not a lost session", "[playerbots][presence]")
{
    REQUIRE_FALSE(PlayerbotSessionWasLost(true, true, false, true));
}

TEST_CASE("A live or never-started bot session is not lost", "[playerbots][presence]")
{
    REQUIRE_FALSE(PlayerbotSessionWasLost(true, true, true, false));
    REQUIRE_FALSE(PlayerbotSessionWasLost(false, false, false, false));
    REQUIRE_FALSE(PlayerbotSessionWasLost(false, true, false, false));
}

TEST_CASE("Automatic login keeps bots online and Coordinator login needs an owning coordinator", "[playerbots][presence]")
{
    REQUIRE(PlayerbotPresenceKeepsOnline(PlayerbotLoginMode::Automatic, false));
    REQUIRE(PlayerbotPresenceKeepsOnline(PlayerbotLoginMode::Automatic, true));
    REQUIRE_FALSE(PlayerbotPresenceKeepsOnline(PlayerbotLoginMode::Coordinator, false));
    REQUIRE(PlayerbotPresenceKeepsOnline(PlayerbotLoginMode::Coordinator, true));
}

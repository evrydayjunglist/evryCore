/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotWipe.h"

TEST_CASE("Bot Battle.net emails the wipe deletes", "[playerbots][wipe]")
{
    REQUIRE(IsPlayerbotBattlenetEmail("PLAYERBOT1@PLAYERBOTS.LOCAL"));
    REQUIRE(IsPlayerbotBattlenetEmail("PLAYERBOT0@PLAYERBOTS.LOCAL"));
    REQUIRE(IsPlayerbotBattlenetEmail("PLAYERBOT12345@PLAYERBOTS.LOCAL"));
}

TEST_CASE("Human accounts the SQL LIKE would also find are not bots", "[playerbots][wipe]")
{
    // Each of these matches LIKE 'PLAYERBOT%@PLAYERBOTS.LOCAL'.
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOT@PLAYERBOTS.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOTFAN@PLAYERBOTS.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOT_1@PLAYERBOTS.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOT1X@PLAYERBOTS.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOT1@EVIL@PLAYERBOTS.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOT 1@PLAYERBOTS.LOCAL"));
}

TEST_CASE("Similar human accounts are not bots", "[playerbots][wipe]")
{
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOT1@PLAYERBOTS.LOCAL.COM"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("XPLAYERBOT1@PLAYERBOTS.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOTS1@PLAYERBOTS.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("playerbot1@playerbots.local"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("PLAYERBOT1@PLAYERBOT.LOCAL"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail("OWNER@EXAMPLE.COM"));
    REQUIRE_FALSE(IsPlayerbotBattlenetEmail(""));
}

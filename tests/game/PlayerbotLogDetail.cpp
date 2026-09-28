/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotLogDetail.h"

TEST_CASE("Only named bots write step lines", "[playerbots][scale]")
{
    PlayerbotLogDetail::SetNames(" Sora , opai,");
    REQUIRE(PlayerbotLogDetail::IsNamed("sora"));
    REQUIRE(PlayerbotLogDetail::IsNamed("Opai"));
    REQUIRE_FALSE(PlayerbotLogDetail::IsNamed("Magey"));
    REQUIRE_FALSE(PlayerbotLogDetail::IsNamed(""));

    PlayerbotLogDetail::NoteBot(1, "Sora");
    PlayerbotLogDetail::NoteBot(2, "Grukk");
    REQUIRE(PlayerbotLogDetail::Wants(1));
    REQUIRE_FALSE(PlayerbotLogDetail::Wants(2));
    // A character that is not a bot (a human's RTS original character) keeps every line.
    REQUIRE(PlayerbotLogDetail::Wants(99));
}

TEST_CASE("An empty list quiets every bot and a star names every bot", "[playerbots][scale]")
{
    PlayerbotLogDetail::SetNames("");
    PlayerbotLogDetail::NoteBot(1, "Sora");
    REQUIRE_FALSE(PlayerbotLogDetail::Wants(1));

    PlayerbotLogDetail::SetNames("*");
    PlayerbotLogDetail::NoteBot(1, "Sora");
    PlayerbotLogDetail::NoteBot(2, "Grukk");
    REQUIRE(PlayerbotLogDetail::Wants(1));
    REQUIRE(PlayerbotLogDetail::Wants(2));
    PlayerbotLogDetail::SetNames("");
}

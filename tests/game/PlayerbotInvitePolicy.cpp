/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotInvitePolicy.h"

namespace
{
    PlayerbotInviteFacts Facts(uint8 inviterLevel, uint8 botLevel, bool gameMaster = false)
    {
        PlayerbotInviteFacts facts;
        facts.InviterIsGameMaster = gameMaster;
        facts.InviterLevel = inviterLevel;
        facts.BotLevel = botLevel;
        facts.MaxLevel = 80;
        return facts;
    }

    // A name that fails to parse reads as Nobody, the same as the server falls back to.
    PlayerbotInvitePolicy Parsed(std::string_view text)
    {
        return ParsePlayerbotInvitePolicy(text).value_or(PlayerbotInvitePolicy::Nobody);
    }
}

TEST_CASE("Invite policy names read from the conf", "[playerbots][invite]")
{
    REQUIRE(ParsePlayerbotInvitePolicy("Nobody").has_value());
    REQUIRE(Parsed("Nobody") == PlayerbotInvitePolicy::Nobody);
    REQUIRE(Parsed(" game master ") == PlayerbotInvitePolicy::GameMaster);
    REQUIRE(Parsed("GM") == PlayerbotInvitePolicy::GameMaster);
    REQUIRE(Parsed("peer") == PlayerbotInvitePolicy::Peer);
    REQUIRE(Parsed("ANYONE") == PlayerbotInvitePolicy::Anyone);
    REQUIRE_FALSE(ParsePlayerbotInvitePolicy("friends").has_value());
}

TEST_CASE("Nobody declines even a game master", "[playerbots][invite]")
{
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Nobody, Facts(10, 10, true)) == PlayerbotInviteVerdict::DeclinedByPolicy);
}

TEST_CASE("GameMaster accepts only a game master", "[playerbots][invite]")
{
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::GameMaster, Facts(60, 10, true)) == PlayerbotInviteVerdict::Accept);
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::GameMaster, Facts(10, 10)) == PlayerbotInviteVerdict::NotGameMaster);
}

TEST_CASE("Peer accepts a player within five levels and any game master", "[playerbots][invite]")
{
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, Facts(15, 10)) == PlayerbotInviteVerdict::Accept);
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, Facts(5, 10)) == PlayerbotInviteVerdict::Accept);
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, Facts(16, 10)) == PlayerbotInviteVerdict::LevelTooFar);
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, Facts(4, 10)) == PlayerbotInviteVerdict::LevelTooFar);
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, Facts(60, 10, true)) == PlayerbotInviteVerdict::Accept);
}

TEST_CASE("Peer compares item level only when both are at the top level", "[playerbots][invite]")
{
    PlayerbotInviteFacts facts = Facts(80, 80);
    facts.InviterItemLevel = 600.0f;
    facts.BotItemLevel = 580.0f;
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, facts) == PlayerbotInviteVerdict::ItemLevelTooFar);

    facts.BotItemLevel = 590.0f;
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, facts) == PlayerbotInviteVerdict::Accept);

    // Below the top level a big item-level gap does not matter; level already says how close they are.
    PlayerbotInviteFacts levelling = Facts(40, 40);
    levelling.InviterItemLevel = 200.0f;
    levelling.BotItemLevel = 100.0f;
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Peer, levelling) == PlayerbotInviteVerdict::Accept);
}

TEST_CASE("Anyone accepts every invite the server allows", "[playerbots][invite]")
{
    REQUIRE(JudgePlayerbotInvite(PlayerbotInvitePolicy::Anyone, Facts(80, 1)) == PlayerbotInviteVerdict::Accept);
}

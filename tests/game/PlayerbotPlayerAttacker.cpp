/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/PlayerbotPlayerAttacker.h"

namespace
{
    // A player on her map, alive, in sight, whom the server lets her attack, but who is not attacking her.
    PlayerbotPlayerAttackerFacts Bystander()
    {
        PlayerbotPlayerAttackerFacts facts;
        facts.OnHerMap = true;
        facts.Alive = true;
        facts.SheSeesThem = true;
        facts.ValidAttackTarget = true;
        return facts;
    }

    // Faction group bits as the game data has them: 2 is the Alliance, 4 the Horde.
    constexpr uint32 ALLIANCE = 2;
    constexpr uint32 HORDE = 4;
}

TEST_CASE("A bot never answers a player who is not attacking her", "[playerbots][pvp]")
{
    REQUIRE(JudgePlayerbotPlayerAttacker(Bystander()) == PlayerbotPlayerAttackerVerdict::NotAttackingHer);

    // Having her selected is not an attack, and neither is a fight with her they are no longer pressing.
    PlayerbotPlayerAttackerFacts selecting = Bystander();
    selecting.TargetingHer = true;
    REQUIRE(JudgePlayerbotPlayerAttacker(selecting) == PlayerbotPlayerAttackerVerdict::NotAttackingHer);

    PlayerbotPlayerAttackerFacts lookedAway = Bystander();
    lookedAway.InCombatWithHer = true;
    REQUIRE(JudgePlayerbotPlayerAttacker(lookedAway) == PlayerbotPlayerAttackerVerdict::NotAttackingHer);
}

TEST_CASE("A bot answers a player who swings at her or casts at her", "[playerbots][pvp]")
{
    PlayerbotPlayerAttackerFacts swinging = Bystander();
    swinging.SwingingAtHer = true;
    REQUIRE(JudgePlayerbotPlayerAttacker(swinging) == PlayerbotPlayerAttackerVerdict::Answer);

    PlayerbotPlayerAttackerFacts casting = Bystander();
    casting.InCombatWithHer = true;
    casting.TargetingHer = true;
    REQUIRE(JudgePlayerbotPlayerAttacker(casting) == PlayerbotPlayerAttackerVerdict::Answer);
}

TEST_CASE("A bot stops answering a player she may no longer fight", "[playerbots][pvp]")
{
    PlayerbotPlayerAttackerFacts facts = Bystander();
    facts.SwingingAtHer = true;

    PlayerbotPlayerAttackerFacts gone = facts;
    gone.OnHerMap = false;
    REQUIRE(JudgePlayerbotPlayerAttacker(gone) == PlayerbotPlayerAttackerVerdict::Gone);

    PlayerbotPlayerAttackerFacts dead = facts;
    dead.Alive = false;
    REQUIRE(JudgePlayerbotPlayerAttacker(dead) == PlayerbotPlayerAttackerVerdict::Dead);

    PlayerbotPlayerAttackerFacts vanished = facts;
    vanished.SheSeesThem = false;
    REQUIRE(JudgePlayerbotPlayerAttacker(vanished) == PlayerbotPlayerAttackerVerdict::Unseen);

    PlayerbotPlayerAttackerFacts refused = facts;
    refused.ValidAttackTarget = false;
    REQUIRE(JudgePlayerbotPlayerAttacker(refused) == PlayerbotPlayerAttackerVerdict::NotAValidTarget);
}

TEST_CASE("Enemy land is land the other faction holds", "[playerbots][pvp]")
{
    // A Horde bot: friends with the Horde group, enemies with the Alliance group.
    REQUIRE(PlayerbotAreaIsEnemyLand(ALLIANCE, HORDE, ALLIANCE, false));
    REQUIRE_FALSE(PlayerbotAreaIsEnemyLand(HORDE, HORDE, ALLIANCE, false));
    REQUIRE_FALSE(PlayerbotAreaIsEnemyLand(ALLIANCE | HORDE, HORDE, ALLIANCE, false));
    REQUIRE_FALSE(PlayerbotAreaIsEnemyLand(0, HORDE, ALLIANCE, false));
    REQUIRE_FALSE(PlayerbotAreaIsEnemyLand(ALLIANCE, HORDE, ALLIANCE, true));

    // An Alliance bot the other way round.
    REQUIRE(PlayerbotAreaIsEnemyLand(HORDE, ALLIANCE, HORDE, false));
    REQUIRE_FALSE(PlayerbotAreaIsEnemyLand(ALLIANCE, ALLIANCE, HORDE, false));
}

TEST_CASE("A bot walks after a player only near where they attacked her and out of enemy land", "[playerbots][pvp]")
{
    REQUIRE(JudgePlayerbotPlayerChase(false, 0.0f) == PlayerbotPlayerChaseVerdict::Walk);
    REQUIRE(JudgePlayerbotPlayerChase(false, PLAYERBOT_PLAYER_CHASE_YARDS) == PlayerbotPlayerChaseVerdict::Walk);
    REQUIRE(JudgePlayerbotPlayerChase(false, PLAYERBOT_PLAYER_CHASE_YARDS + 0.5f) == PlayerbotPlayerChaseVerdict::TooFar);
    REQUIRE(JudgePlayerbotPlayerChase(true, 1.0f) == PlayerbotPlayerChaseVerdict::EnemyLand);
}

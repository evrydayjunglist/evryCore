/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "tc_catch2.h"

#include "../../modules/mod-playerbots/src/CommandablePlayerState.h"

namespace
{
    ObjectGuid PlayerGuid(uint64 counter)
    {
        return ObjectGuid::Create<HighGuid::Player>(counter);
    }
}

TEST_CASE("Commandable player acquisition immediately holds and excludes another controller", "[playerbots][commandable]")
{
    CommandablePlayerState state;
    ObjectGuid const subject = PlayerGuid(1);
    CommandableControllerIdentity const builtin = { CommandableControllerKind::Builtin, subject };
    CommandableControllerIdentity const commander = { CommandableControllerKind::Rts, PlayerGuid(2) };
    CommandableControllerIdentity const intruder = { CommandableControllerKind::Rts, PlayerGuid(3) };

    CommandablePlayerResult acquired = state.Acquire(subject, builtin, commander);
    REQUIRE(acquired);
    REQUIRE(state.Directive() == CommandablePlayerDirective::Hold);
    REQUIRE(state.Generation() == 1);
    REQUIRE_FALSE(state.Acquire(subject, builtin, intruder));
    REQUIRE(state.Acquire(PlayerGuid(4), builtin, commander).Code == CommandablePlayerResultCode::InvalidSubject);
    REQUIRE(state.Controller() == commander);
}

TEST_CASE("Commandable player redirects and distinguishes hold from release", "[playerbots][commandable]")
{
    CommandablePlayerState state;
    ObjectGuid const subject = PlayerGuid(10);
    CommandableControllerIdentity const builtin = { CommandableControllerKind::Builtin, subject };
    CommandableControllerIdentity const commander = { CommandableControllerKind::Rts, PlayerGuid(11) };
    uint64 const generation = state.Acquire(subject, builtin, commander).Generation;

    CommandablePlayerRequest request = { commander, subject, generation, CommandablePlayerOperation::Move, {} };
    REQUIRE(state.Apply(request));
    REQUIRE(state.Directive() == CommandablePlayerDirective::Move);

    request.Destination.Relocate(20.0f, 30.0f, 40.0f);
    REQUIRE(state.Apply(request));
    REQUIRE(state.Generation() == generation);

    request.Operation = CommandablePlayerOperation::Hold;
    REQUIRE(state.Apply(request));
    REQUIRE(state.Active());
    REQUIRE(state.Directive() == CommandablePlayerDirective::Hold);

    request.Operation = CommandablePlayerOperation::Release;
    CommandablePlayerResult released = state.Apply(request);
    REQUIRE(released);
    REQUIRE_FALSE(state.Active());
    REQUIRE(state.Controller() == builtin);
    REQUIRE(released.Generation == generation + 1);
}

TEST_CASE("Commandable player authorizes the exact controller subject and generation", "[playerbots][commandable]")
{
    CommandablePlayerState state;
    ObjectGuid const subject = PlayerGuid(12);
    CommandableControllerIdentity const builtin = { CommandableControllerKind::Builtin, subject };
    CommandableControllerIdentity const commander = { CommandableControllerKind::Rts, PlayerGuid(13) };
    CommandableControllerIdentity const otherCommander = { CommandableControllerKind::Rts, PlayerGuid(14) };
    uint64 const generation = state.Acquire(subject, builtin, commander).Generation;

    CommandablePlayerRequest request = { otherCommander, subject, generation, CommandablePlayerOperation::Move, {} };
    REQUIRE(state.Apply(request).Code == CommandablePlayerResultCode::ControllerMismatch);

    request.Controller = commander;
    request.Subject = PlayerGuid(15);
    REQUIRE(state.Apply(request).Code == CommandablePlayerResultCode::InvalidSubject);

    request.Subject = subject;
    request.Generation = generation - 1;
    REQUIRE(state.Apply(request).Code == CommandablePlayerResultCode::StaleGeneration);

    request.Generation = generation;
    REQUIRE(state.Apply(request));
}

TEST_CASE("Commandable player rejects stale work after ownership and lifecycle barriers", "[playerbots][commandable]")
{
    CommandablePlayerState state;
    ObjectGuid const subject = PlayerGuid(20);
    CommandableControllerIdentity const human = { CommandableControllerKind::Human, subject };
    CommandableControllerIdentity const commander = { CommandableControllerKind::Rts, subject };
    uint64 generation = state.Acquire(subject, human, commander).Generation;
    CommandablePlayerRequest move = { commander, subject, generation, CommandablePlayerOperation::Move, {} };

    REQUIRE(state.SuspendForTeleport());
    REQUIRE(state.Apply(move).Code == CommandablePlayerResultCode::StaleGeneration);
    REQUIRE(state.FinishTeleportRevalidation());
    generation = state.Generation();
    move.Generation = generation;
    REQUIRE(state.Apply(move));

    REQUIRE(state.SuspendForDeath());
    REQUIRE(state.Directive() == CommandablePlayerDirective::Recovering);
    REQUIRE(state.Apply(move).Code == CommandablePlayerResultCode::StaleGeneration);
    REQUIRE(state.FinishDeathRecovery());
    REQUIRE(state.Directive() == CommandablePlayerDirective::Hold);

    CommandablePlayerRequest freshMove = move;
    freshMove.Generation = state.Generation();
    REQUIRE(state.Apply(freshMove));
    REQUIRE(state.Invalidate());
    REQUIRE(state.Apply(freshMove).Code == CommandablePlayerResultCode::NoClaim);
}

TEST_CASE("Releasing the original character restores its human controller with a new generation", "[playerbots][commandable]")
{
    CommandablePlayerState state;
    ObjectGuid const original = PlayerGuid(30);
    CommandableControllerIdentity const human = { CommandableControllerKind::Human, original };
    CommandableControllerIdentity const commander = { CommandableControllerKind::Rts, original };
    CommandablePlayerResult acquired = state.Acquire(original, human, commander);

    REQUIRE(acquired);
    CommandablePlayerResult released = state.Release(commander, acquired.Generation);
    REQUIRE(released);
    REQUIRE_FALSE(state.Active());
    REQUIRE(state.Controller() == human);
    REQUIRE(released.Generation > acquired.Generation);
}

TEST_CASE("Release restores the stored managed-player baseline controller", "[playerbots][commandable]")
{
    ObjectGuid const subject = PlayerGuid(40);
    CommandableControllerIdentity const commander = { CommandableControllerKind::Rts, PlayerGuid(41) };
    for (CommandableControllerKind kind : { CommandableControllerKind::Reserve, CommandableControllerKind::Builtin,
        CommandableControllerKind::External })
    {
        CommandablePlayerState state;
        CommandableControllerIdentity const baseline = { kind, subject };
        CommandablePlayerResult acquired = state.Acquire(subject, baseline, commander);
        REQUIRE(acquired);
        REQUIRE(state.Release(commander, acquired.Generation));
        REQUIRE(state.Controller() == baseline);
    }
}

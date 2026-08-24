/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_COMMANDABLE_PLAYER_H
#define EVRY_COMMANDABLE_PLAYER_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <vector>

enum class CommandableControllerKind : uint8
{
    Human,
    Reserve,
    Builtin,
    External,
    Rts
};

struct CommandableControllerIdentity
{
    CommandableControllerKind Kind = CommandableControllerKind::Human;
    ObjectGuid Owner;

    bool operator==(CommandableControllerIdentity const&) const = default;
};

enum class CommandablePlayerOperation : uint8
{
    Move,
    Hold,
    Release
};

enum class CommandablePlayerDirective : uint8
{
    None,
    Hold,
    Move,
    Recovering
};

struct CommandablePlayerRequest
{
    CommandableControllerIdentity Controller;
    ObjectGuid Subject;
    uint64 Generation = 0;
    CommandablePlayerOperation Operation = CommandablePlayerOperation::Hold;
    Position Destination;
};

enum class CommandablePlayerResultCode : uint8
{
    Accepted,
    InvalidController,
    InvalidSubject,
    NotGroupLeader,
    GroupAlreadyCommanded,
    NotEligible,
    NoClaim,
    ControllerMismatch,
    StaleGeneration,
    Suspended,
    InvalidDestination,
    MovementRefused,
    OriginalReleaseRequiresExit,
    IncompatibleMapOrInstance
};

struct CommandablePlayerResult
{
    CommandablePlayerResultCode Code = CommandablePlayerResultCode::NoClaim;
    uint64 Generation = 0;

    explicit operator bool() const { return Code == CommandablePlayerResultCode::Accepted; }
};

struct CommandablePlayerSnapshot
{
    ObjectGuid Subject;
    uint64 Generation = 0;
    CommandablePlayerDirective Directive = CommandablePlayerDirective::None;
    bool OriginalCharacter = false;
};

struct CommandableRtsEnterResult
{
    CommandablePlayerResultCode Code = CommandablePlayerResultCode::InvalidController;
    ObjectGuid Group;
    std::vector<CommandablePlayerSnapshot> Subjects;

    explicit operator bool() const { return Code == CommandablePlayerResultCode::Accepted; }
};

char const* CommandablePlayerResultCodeName(CommandablePlayerResultCode code);
char const* CommandablePlayerDirectiveName(CommandablePlayerDirective directive);

#endif

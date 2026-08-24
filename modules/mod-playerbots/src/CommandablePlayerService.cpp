/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include "CommandablePlayerService.h"
#include "PlayerbotMgr.h"

CommandablePlayerService* CommandablePlayerService::instance()
{
    static CommandablePlayerService instance;
    return &instance;
}

CommandableRtsEnterResult CommandablePlayerService::EnterRts(Player* commander)
{
    return sPlayerbotMgr->EnterRts(commander);
}

CommandablePlayerResult CommandablePlayerService::Submit(CommandablePlayerRequest const& request)
{
    return sPlayerbotMgr->SubmitCommand(request);
}

CommandablePlayerResult CommandablePlayerService::ExitRts(ObjectGuid commander)
{
    return sPlayerbotMgr->ExitRts(commander);
}

std::vector<CommandablePlayerSnapshot> CommandablePlayerService::GetRtsSubjects(ObjectGuid commander) const
{
    return sPlayerbotMgr->GetRtsSubjects(commander);
}

char const* CommandablePlayerResultCodeName(CommandablePlayerResultCode code)
{
    switch (code)
    {
        case CommandablePlayerResultCode::Accepted: return "accepted";
        case CommandablePlayerResultCode::InvalidController: return "invalid controller";
        case CommandablePlayerResultCode::InvalidSubject: return "invalid subject";
        case CommandablePlayerResultCode::NotGroupLeader: return "controller is not the current group leader";
        case CommandablePlayerResultCode::GroupAlreadyCommanded: return "the group already has an RTS commander";
        case CommandablePlayerResultCode::NotEligible: return "subject is not eligible";
        case CommandablePlayerResultCode::NoClaim: return "subject has no RTS claim";
        case CommandablePlayerResultCode::ControllerMismatch: return "controller does not own the subject";
        case CommandablePlayerResultCode::StaleGeneration: return "request generation is stale";
        case CommandablePlayerResultCode::Suspended: return "subject is suspended for death or teleport";
        case CommandablePlayerResultCode::InvalidDestination: return "destination is invalid";
        case CommandablePlayerResultCode::MovementRefused: return "the player-like walker refused the destination";
        case CommandablePlayerResultCode::OriginalReleaseRequiresExit: return "the original character is released only by exiting RTS";
        case CommandablePlayerResultCode::IncompatibleMapOrInstance: return "subject is on an incompatible map or instance";
    }

    return "unknown refusal";
}

char const* CommandablePlayerDirectiveName(CommandablePlayerDirective directive)
{
    switch (directive)
    {
        case CommandablePlayerDirective::None: return "baseline";
        case CommandablePlayerDirective::Hold: return "hold";
        case CommandablePlayerDirective::Move: return "move";
        case CommandablePlayerDirective::Recovering: return "recovering";
    }

    return "unknown";
}

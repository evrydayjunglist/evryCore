/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_COMMANDABLE_PLAYER_SERVICE_H
#define EVRY_COMMANDABLE_PLAYER_SERVICE_H

#include "CommandablePlayer.h"

class Player;

class CommandablePlayerService
{
public:
    static CommandablePlayerService* instance();

    CommandableRtsEnterResult EnterRts(Player* commander);
    CommandablePlayerResult Submit(CommandablePlayerRequest const& request);
    CommandablePlayerResult ExitRts(ObjectGuid commander);
    std::vector<CommandablePlayerSnapshot> GetRtsSubjects(ObjectGuid commander) const;
};

#define sCommandablePlayerService CommandablePlayerService::instance()

#endif

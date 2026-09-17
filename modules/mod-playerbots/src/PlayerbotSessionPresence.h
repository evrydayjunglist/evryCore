/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_SESSION_PRESENCE_H
#define EVRY_PLAYERBOT_SESSION_PRESENCE_H

#include "Playerbots.h"

// How long a bot waits to log in again after her session ends without this module, about as long as a player takes
// to reconnect. It also keeps a kick that repeats from turning into a login every tick.
inline constexpr uint32 PLAYERBOT_RELOG_WAIT_MS = 30000;

// Automatic login keeps the roster online. Coordinator login keeps it online only while a connected
// playerbots.exe owns the roster.
inline bool PlayerbotPresenceKeepsOnline(PlayerbotLoginMode mode, bool coordinatorOwnsRoster)
{
    return mode == PlayerbotLoginMode::Automatic || coordinatorOwnsRoster;
}

// A session this module started and saw in the world is gone, and this module did not log it out, for example
// after a GM kick, an AntiDOS kick, or a ban. A session still waiting to be added to the world is not lost.
inline bool PlayerbotSessionWasLost(bool sessionQueued, bool sessionSeen, bool sessionExists, bool moduleLogoutRequested)
{
    return sessionQueued && sessionSeen && !sessionExists && !moduleLogoutRequested;
}

#endif

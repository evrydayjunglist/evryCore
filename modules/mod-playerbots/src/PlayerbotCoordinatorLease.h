/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_MOD_PLAYERBOT_COORDINATOR_LEASE_H
#define EVRY_MOD_PLAYERBOT_COORDINATOR_LEASE_H

#include "Define.h"

inline constexpr uint32 PLAYERBOT_COORDINATOR_DISCONNECT_GRACE_MS = 5000;

class PlayerbotCoordinatorLease
{
public:
    void Ensure(uint64 connectionId)
    {
        if (!connectionId)
            return;

        _connectionId = connectionId;
        _disconnectGraceMs = 0;
    }

    bool Disconnect(uint64 connectionId)
    {
        if (!connectionId || connectionId != _connectionId)
            return false;

        _connectionId = 0;
        _disconnectGraceMs = PLAYERBOT_COORDINATOR_DISCONNECT_GRACE_MS;
        return true;
    }

    bool Update(uint32 diff)
    {
        if (!_disconnectGraceMs)
            return false;

        if (diff < _disconnectGraceMs)
        {
            _disconnectGraceMs -= diff;
            return false;
        }

        _disconnectGraceMs = 0;
        return true;
    }

    uint64 ConnectionId() const { return _connectionId; }
    uint32 DisconnectGraceMs() const { return _disconnectGraceMs; }

private:
    uint64 _connectionId = 0;
    uint32 _disconnectGraceMs = 0;
};

#endif

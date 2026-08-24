/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_MOD_PLAYERBOT_BRIDGE_H
#define EVRY_MOD_PLAYERBOT_BRIDGE_H

#include "Define.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct PlayerbotBridgeRequest
{
    uint64 ConnectionId = 0;
    std::string Payload;
    std::function<void(std::string)> Reply;
};

class PlayerbotBridge
{
public:
    PlayerbotBridge();
    ~PlayerbotBridge();

    PlayerbotBridge(PlayerbotBridge const&) = delete;
    PlayerbotBridge(PlayerbotBridge&&) = delete;
    PlayerbotBridge& operator=(PlayerbotBridge const&) = delete;
    PlayerbotBridge& operator=(PlayerbotBridge&&) = delete;

    bool Start(uint16 port);
    void Stop();
    std::vector<PlayerbotBridgeRequest> TakeRequests();

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

#endif

/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_PLAYERBOT_INVITE_POLICY_H
#define EVRY_PLAYERBOT_INVITE_POLICY_H

#include "Define.h"
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

inline constexpr char const* PLAYERBOTS_INVITE_POLICY = "Playerbots.InvitePolicy";

// Who a bot says yes to when a player invites her to a party. Each step lets in everyone the step before it does,
// so a game master is always accepted unless invites are turned off.
enum class PlayerbotInvitePolicy
{
    Nobody,     // she declines every invite
    GameMaster, // only a player whose account is game master or above
    Peer,       // a game master, or a player close to her in level (and in item level once both are at the top level)
    Anyone      // anyone the server lets invite her
};

// A player within this many levels of her is close enough to play alongside her.
inline constexpr uint8 PLAYERBOT_INVITE_PEER_LEVEL_GAP = 5;

// Once both are at the top level, level says nothing, so average equipped item level decides instead.
inline constexpr float PLAYERBOT_INVITE_PEER_ITEM_LEVEL_GAP = 15.0f;

struct PlayerbotInviteFacts
{
    bool InviterIsGameMaster = false;
    uint8 InviterLevel = 0;
    uint8 BotLevel = 0;
    uint8 MaxLevel = 0;
    float InviterItemLevel = 0.0f;
    float BotItemLevel = 0.0f;
};

enum class PlayerbotInviteVerdict
{
    Accept,
    DeclinedByPolicy,  // invites are turned off
    NotGameMaster,     // the policy wants a game master
    LevelTooFar,       // more than the level gap apart
    ItemLevelTooFar    // both at the top level, but more than the item-level gap apart
};

inline std::optional<PlayerbotInvitePolicy> ParsePlayerbotInvitePolicy(std::string_view text)
{
    std::string name;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c)))
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (name == "nobody" || name == "none")
        return PlayerbotInvitePolicy::Nobody;
    if (name == "gamemaster" || name == "gm")
        return PlayerbotInvitePolicy::GameMaster;
    if (name == "peer")
        return PlayerbotInvitePolicy::Peer;
    if (name == "anyone")
        return PlayerbotInvitePolicy::Anyone;
    return std::nullopt;
}

inline char const* PlayerbotInvitePolicyName(PlayerbotInvitePolicy policy)
{
    switch (policy)
    {
        case PlayerbotInvitePolicy::Nobody: return "Nobody";
        case PlayerbotInvitePolicy::GameMaster: return "GameMaster";
        case PlayerbotInvitePolicy::Peer: return "Peer";
        case PlayerbotInvitePolicy::Anyone: return "Anyone";
    }
    return "Nobody";
}

inline PlayerbotInviteVerdict JudgePlayerbotInvite(PlayerbotInvitePolicy policy, PlayerbotInviteFacts const& facts)
{
    if (policy == PlayerbotInvitePolicy::Nobody)
        return PlayerbotInviteVerdict::DeclinedByPolicy;
    if (policy == PlayerbotInvitePolicy::Anyone || facts.InviterIsGameMaster)
        return PlayerbotInviteVerdict::Accept;
    if (policy == PlayerbotInvitePolicy::GameMaster)
        return PlayerbotInviteVerdict::NotGameMaster;

    uint8 const levelGap = facts.InviterLevel > facts.BotLevel ? facts.InviterLevel - facts.BotLevel : facts.BotLevel - facts.InviterLevel;
    if (levelGap > PLAYERBOT_INVITE_PEER_LEVEL_GAP)
        return PlayerbotInviteVerdict::LevelTooFar;

    bool const bothAtTop = facts.MaxLevel > 0 && facts.InviterLevel >= facts.MaxLevel && facts.BotLevel >= facts.MaxLevel;
    if (bothAtTop && std::abs(facts.InviterItemLevel - facts.BotItemLevel) > PLAYERBOT_INVITE_PEER_ITEM_LEVEL_GAP)
        return PlayerbotInviteVerdict::ItemLevelTooFar;

    return PlayerbotInviteVerdict::Accept;
}

#endif

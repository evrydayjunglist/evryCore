/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef EVRY_MOD_PLAYERBOT_GEAR_H
#define EVRY_MOD_PLAYERBOT_GEAR_H

#include "PlayerbotGearScore.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include <string>
#include <unordered_set>

class Item;
class Player;
struct ItemTemplate;

// Which gear a bot wears. She judges an item the way a player reads its tooltip against her character sheet: whether she
// can use it at all (the server's own checks on her class, level, and the weapon and armor skills she has learned),
// whether the game marks it for her specialization, her class armor first, and her stat weights. The only way she puts
// anything on is one CMSG_AUTO_EQUIP_ITEM_SLOT, and the server checks it again.
namespace PlayerbotGear
{
    // Reads Playerbots.Gear.UpgradeMarginPct and Playerbots.Gear.Weights.* over the module's starting weights. Once, at
    // startup, before any bot thinks; only read afterwards.
    void LoadConfig();

    float UpgradeMarginPct();

    // Her weights for the specialization she is in now.
    PlayerbotGearStats const& WeightsFor(Player const* player);

    // What an item is worth to her by her weights, where she would put it, and whether that is better by the margin
    // than what she has on there.
    struct Upgrade
    {
        uint8 Slot = 0;
        float Score = 0.0f;
        float Replaced = 0.0f;
        bool ReplacesNothing = false;
        float Gain = 0.0f;
    };

    // A quest reward she has not got yet: the same judgement from the item's template at her level.
    Optional<Upgrade> UpgradeFromTemplate(Player const* player, ItemTemplate const* proto);

    // An item in her bags. whyNot, when given, gets the reason when it is not an upgrade.
    Optional<Upgrade> UpgradeFromBags(Player const* player, Item const* item, std::string* whyNot = nullptr);

    // Whether the server would let her use an item of this template at all: class, race, level, and the weapon or armor
    // skill she has learned.
    bool CanUseTemplate(Player const* player, ItemTemplate const* proto);

    enum class Look
    {
        Nothing,  // nothing in her bags is an upgrade
        Queued    // one equip packet queued; look again once the server has handled it
    };

    // Looks through her bags once and puts on the one item that gains her the most, if any. judged holds the items she
    // already said she keeps at this level, so each is named once; refused holds the ones the server would not put on.
    // lastQueued is the item she queued last time: still in her bags now means the server did not put it on.
    Look TryWearBestFromBags(Player* player, std::unordered_set<ObjectGuid>& judged, std::unordered_set<ObjectGuid>& refused,
        ObjectGuid& lastQueued);
}

#endif

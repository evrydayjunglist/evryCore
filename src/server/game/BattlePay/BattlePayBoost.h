/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef TRINITYCORE_BATTLE_PAY_BOOST_H
#define TRINITYCORE_BATTLE_PAY_BOOST_H

#include "Define.h"
#include <array>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace BattlePay
{
struct BoostInventoryItem
{
    uint64 Guid;
    uint64 Bag;
    uint8 Slot;
    uint32 Entry;
    uint32 Count;
};

struct BoostKitItem
{
    uint32 Entry;
    uint8 InventoryType;
    uint8 Class;
    uint8 Subclass;
    uint32 MaxCount;
};

struct BoostInventoryPlan
{
    std::unordered_set<uint64> RecoveredItems;
    // One destination for each loadout row. An empty destination means an owned unique item is retained.
    std::vector<std::optional<uint8>> KitSlots;
};

TC_GAME_API std::optional<BoostInventoryPlan> PlanBoostInventory(std::span<BoostInventoryItem const> inventory,
    std::span<BoostKitItem const> kit, uint8 backpackSlots, bool dualWieldTwoHanded);
}

#endif

/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "BattlePayBoost.h"
#include "ItemTemplate.h"
#include "Player.h"
#include <unordered_map>

std::optional<BattlePay::BoostInventoryPlan> BattlePay::PlanBoostInventory(std::span<BoostInventoryItem const> inventory,
    std::span<BoostKitItem const> kit, uint8 backpackSlots, bool dualWieldTwoHanded)
{
    if (kit.empty() || backpackSlots > INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START)
        return std::nullopt;

    BoostInventoryPlan plan;
    for (BoostInventoryItem const& item : inventory)
        if (!item.Bag && (item.Slot < EQUIPMENT_SLOT_END
            || (item.Slot >= INVENTORY_SLOT_BAG_START && item.Slot < REAGENT_BAG_SLOT_END)
            || (item.Slot >= CHILD_EQUIPMENT_SLOT_START && item.Slot < CHILD_EQUIPMENT_SLOT_END)))
            plan.RecoveredItems.insert(item.Guid);

    // Move the contents with every recovered bag, without touching bank bags or the backpack.
    bool foundContents;
    do
    {
        foundContents = false;
        for (BoostInventoryItem const& item : inventory)
            if (item.Bag && plan.RecoveredItems.contains(item.Bag))
                foundContents |= plan.RecoveredItems.insert(item.Guid).second;
    } while (foundContents);

    std::array<bool, 256> usedSlots = { };
    std::unordered_map<uint32, uint64> retainedCounts;
    for (BoostInventoryItem const& item : inventory)
    {
        if (plan.RecoveredItems.contains(item.Guid))
            continue;
        retainedCounts[item.Entry] += item.Count;
        if (!item.Bag)
            usedSlots[item.Slot] = true;
    }

    bool bothHandsUsed = false;
    auto take = [&](uint8 slot) -> std::optional<uint8>
    {
        if (usedSlots[slot] || (slot == EQUIPMENT_SLOT_OFFHAND && bothHandsUsed))
            return std::nullopt;
        usedSlots[slot] = true;
        return slot;
    };
    auto pair = [&](uint8 first, uint8 second)
    {
        auto slot = take(first);
        return slot ? slot : take(second);
    };

    for (BoostKitItem const& item : kit)
    {
        if (item.MaxCount && retainedCounts[item.Entry] >= item.MaxCount)
        {
            // A retained Hearthstone or other unique supply already satisfies the loadout.
            if (item.InventoryType != INVTYPE_NON_EQUIP)
                return std::nullopt;
            plan.KitSlots.emplace_back();
            continue;
        }

        std::optional<uint8> slot;
        bool worn = true;
        switch (item.InventoryType)
        {
            case INVTYPE_HEAD: slot = take(EQUIPMENT_SLOT_HEAD); break;
            case INVTYPE_NECK: slot = take(EQUIPMENT_SLOT_NECK); break;
            case INVTYPE_SHOULDERS: slot = take(EQUIPMENT_SLOT_SHOULDERS); break;
            case INVTYPE_BODY: slot = take(EQUIPMENT_SLOT_BODY); break;
            case INVTYPE_CHEST:
            case INVTYPE_ROBE: slot = take(EQUIPMENT_SLOT_CHEST); break;
            case INVTYPE_WAIST: slot = take(EQUIPMENT_SLOT_WAIST); break;
            case INVTYPE_LEGS: slot = take(EQUIPMENT_SLOT_LEGS); break;
            case INVTYPE_FEET: slot = take(EQUIPMENT_SLOT_FEET); break;
            case INVTYPE_WRISTS: slot = take(EQUIPMENT_SLOT_WRISTS); break;
            case INVTYPE_HANDS: slot = take(EQUIPMENT_SLOT_HANDS); break;
            case INVTYPE_FINGER: slot = pair(EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2); break;
            case INVTYPE_TRINKET: slot = pair(EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2); break;
            case INVTYPE_CLOAK: slot = take(EQUIPMENT_SLOT_BACK); break;
            case INVTYPE_TABARD: slot = take(EQUIPMENT_SLOT_TABARD); break;
            case INVTYPE_WEAPON: slot = pair(EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND); break;
            case INVTYPE_2HWEAPON:
                slot = dualWieldTwoHanded ? pair(EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND) : take(EQUIPMENT_SLOT_MAINHAND);
                break;
            case INVTYPE_WEAPONMAINHAND:
            case INVTYPE_RANGED:
            case INVTYPE_RANGEDRIGHT: slot = take(EQUIPMENT_SLOT_MAINHAND); break;
            case INVTYPE_WEAPONOFFHAND:
            case INVTYPE_SHIELD:
            case INVTYPE_HOLDABLE: slot = take(EQUIPMENT_SLOT_OFFHAND); break;
            case INVTYPE_BAG:
                if (item.Class == ITEM_CLASS_CONTAINER && item.Subclass == ITEM_SUBCLASS_REAGENT_CONTAINER)
                    slot = take(REAGENT_BAG_SLOT_START);
                else
                    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END && !slot; ++bag)
                        slot = take(bag);
                break;
            default: worn = false; break;
        }

        if (worn && !slot)
            return std::nullopt;

        if (!worn)
            for (uint8 pack = INVENTORY_SLOT_ITEM_START; pack < INVENTORY_SLOT_ITEM_START + backpackSlots && !slot; ++pack)
                slot = take(pack);
        if (!slot)
            return std::nullopt;

        if (*slot == EQUIPMENT_SLOT_MAINHAND)
        {
            bothHandsUsed = (item.InventoryType == INVTYPE_2HWEAPON && !dualWieldTwoHanded)
                || item.InventoryType == INVTYPE_RANGED
                || (item.InventoryType == INVTYPE_RANGEDRIGHT && item.Subclass != ITEM_SUBCLASS_WEAPON_WAND);
            if (bothHandsUsed && usedSlots[EQUIPMENT_SLOT_OFFHAND])
                return std::nullopt;
        }
        ++retainedCounts[item.Entry];
        plan.KitSlots.push_back(slot);
    }
    return plan;
}

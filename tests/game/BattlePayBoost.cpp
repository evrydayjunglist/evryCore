/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "BattlePayBoost.h"
#include "ItemTemplate.h"
#include "Player.h"

namespace
{
BattlePay::BoostKitItem Kit(uint32 entry, InventoryType type, uint8 subclass = 0, uint32 maxCount = 0)
{
    return { entry, uint8(type), uint8(type == INVTYPE_BAG ? ITEM_CLASS_CONTAINER : ITEM_CLASS_WEAPON), subclass, maxCount };
}
}

TEST_CASE("Offline boost preserves equipment and bag contents without overwriting the backpack or bank", "[battlepay][boost]")
{
    std::array<BattlePay::BoostInventoryItem, 9> inventory = {{
        { 1, 0, EQUIPMENT_SLOT_CHEST, 100, 1 },
        { 2, 0, INVENTORY_SLOT_BAG_START, 101, 1 },
        { 3, 2, 0, 102, 9 },
        { 4, 0, REAGENT_BAG_SLOT_START, 103, 1 },
        { 5, 4, 0, 104, 10 },
        { 6, 0, INVENTORY_SLOT_ITEM_START, 105, 4 },
        { 7, 0, BANK_SLOT_BAG_START, 106, 1 },
        { 8, 7, 0, 107, 20 },
        { 9, 0, CHILD_EQUIPMENT_SLOT_START, 108, 1 }
    }};
    auto before = inventory;
    std::array kit = { Kit(200, INVTYPE_ROBE), Kit(201, INVTYPE_BAG),
        Kit(202, INVTYPE_BAG, ITEM_SUBCLASS_REAGENT_CONTAINER), Kit(203, INVTYPE_NON_EQUIP) };
    auto plan = BattlePay::PlanBoostInventory(inventory, kit, 16, false);
    REQUIRE(plan.has_value());
    CHECK(plan->RecoveredItems == std::unordered_set<uint64>{1, 2, 3, 4, 5, 9});
    REQUIRE(plan->KitSlots.size() == 4);
    CHECK(plan->KitSlots[0] == EQUIPMENT_SLOT_CHEST);
    CHECK(plan->KitSlots[1] == INVENTORY_SLOT_BAG_START);
    CHECK(plan->KitSlots[2] == REAGENT_BAG_SLOT_START);
    CHECK(plan->KitSlots[3] == INVENTORY_SLOT_ITEM_START + 1);
    for (size_t i = 0; i < inventory.size(); ++i)
    {
        CHECK(inventory[i].Guid == before[i].Guid);
        CHECK(inventory[i].Bag == before[i].Bag);
        CHECK(inventory[i].Slot == before[i].Slot);
        CHECK(inventory[i].Count == before[i].Count);
    }
}

TEST_CASE("Offline boost fails the whole plan when the full kit cannot fit", "[battlepay][boost]")
{
    std::vector<BattlePay::BoostInventoryItem> inventory;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_START + 16; ++slot)
        inventory.push_back({uint64(slot), 0, slot, 100, 1});
    std::array kit = { Kit(200, INVTYPE_ROBE), Kit(201, INVTYPE_NON_EQUIP) };
    CHECK_FALSE(BattlePay::PlanBoostInventory(inventory, kit, 16, false).has_value());
    // Slots beyond this character's backpack capacity must not be used.
    auto plan = BattlePay::PlanBoostInventory(inventory, kit, 20, false);
    REQUIRE(plan.has_value());
    CHECK(plan->KitSlots[1] == INVENTORY_SLOT_ITEM_START + 16);
    CHECK_FALSE(BattlePay::PlanBoostInventory({}, {}, 16, false).has_value());
    CHECK_FALSE(BattlePay::PlanBoostInventory({}, kit, 255, false).has_value());
}

TEST_CASE("Offline boost retains a unique supply already in the backpack", "[battlepay][boost]")
{
    std::array<BattlePay::BoostInventoryItem, 1> inventory = {{{1, 0, INVENTORY_SLOT_ITEM_START, 6948, 1}}};
    std::array kit = { Kit(100, INVTYPE_ROBE), Kit(6948, INVTYPE_NON_EQUIP, 0, 1) };
    auto plan = BattlePay::PlanBoostInventory(inventory, kit, 16, false);
    REQUIRE(plan.has_value());
    CHECK(plan->RecoveredItems.empty());
    CHECK(plan->KitSlots[0] == EQUIPMENT_SLOT_CHEST);
    CHECK_FALSE(plan->KitSlots[1]);
}

TEST_CASE("Offline boost assigns paired items and rejects conflicting weapon loadouts", "[battlepay][boost]")
{
    SECTION("Paired rings, trinkets and one handed weapons")
    {
        std::array kit = { Kit(1, INVTYPE_FINGER), Kit(1, INVTYPE_FINGER),
            Kit(2, INVTYPE_TRINKET), Kit(3, INVTYPE_TRINKET), Kit(4, INVTYPE_WEAPON), Kit(4, INVTYPE_WEAPON) };
        auto plan = BattlePay::PlanBoostInventory({}, kit, 16, false);
        REQUIRE(plan.has_value());
        CHECK(plan->KitSlots == std::vector<std::optional<uint8>>{
            EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2, EQUIPMENT_SLOT_TRINKET1,
            EQUIPMENT_SLOT_TRINKET2, EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND });
    }
    SECTION("Fury two handed weapons")
    {
        std::array kit = { Kit(1, INVTYPE_2HWEAPON), Kit(1, INVTYPE_2HWEAPON) };
        CHECK_FALSE(BattlePay::PlanBoostInventory({}, kit, 16, false).has_value());
        auto plan = BattlePay::PlanBoostInventory({}, kit, 16, true);
        REQUIRE(plan.has_value());
        CHECK(plan->KitSlots[0] == EQUIPMENT_SLOT_MAINHAND);
        CHECK(plan->KitSlots[1] == EQUIPMENT_SLOT_OFFHAND);
    }
    SECTION("Staff and offhand conflict in either row order")
    {
        std::array kit = { Kit(1, INVTYPE_2HWEAPON), Kit(2, INVTYPE_HOLDABLE) };
        CHECK_FALSE(BattlePay::PlanBoostInventory({}, kit, 16, false).has_value());
        std::swap(kit[0], kit[1]);
        CHECK_FALSE(BattlePay::PlanBoostInventory({}, kit, 16, false).has_value());
    }
    SECTION("Wand and offhand")
    {
        std::array kit = { Kit(1, INVTYPE_RANGEDRIGHT, ITEM_SUBCLASS_WEAPON_WAND), Kit(2, INVTYPE_HOLDABLE) };
        REQUIRE(BattlePay::PlanBoostInventory({}, kit, 16, false).has_value());
    }
    SECTION("Four bags and a reagent bag")
    {
        std::array kit = { Kit(1, INVTYPE_BAG), Kit(1, INVTYPE_BAG), Kit(1, INVTYPE_BAG),
            Kit(1, INVTYPE_BAG), Kit(2, INVTYPE_BAG, ITEM_SUBCLASS_REAGENT_CONTAINER) };
        auto plan = BattlePay::PlanBoostInventory({}, kit, 16, false);
        REQUIRE(plan.has_value());
        CHECK(plan->KitSlots == std::vector<std::optional<uint8>>{ 30, 31, 32, 33, 34 });
    }
}

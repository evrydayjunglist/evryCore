-- Demon Hunter Mardum unique TreasurePicker pools (Wowhead + warcraft.wiki.gg).
-- Each picker belongs to one quest. Shared picker 3711 is not filled: thousands of
-- unrelated quests point at it and Wowhead lists no items for those samples.
-- Alliance recolour is the 1289xx id, Horde recolour is the 1333xx id. Core hides
-- the row this character cannot use (race / ITEM_FLAG2_FACTION_*).
-- No sniff for Flags, IsChoice, Gold, BonusListID, or Context; those stay 0.
-- Wowhead "choose one" on these pages is the two faction twins, not a player choice.

DELETE FROM `treasure_picker` WHERE `TreasurePickerID` IN (3588, 3688, 3689, 3692, 3697);
INSERT INTO `treasure_picker` (`TreasurePickerID`, `Flags`, `IsChoice`, `Gold`, `VerifiedBuild`) VALUES
(3588, 0, 0, 0, 0), -- 38765 Enter the Illidari: Shivarra
(3688, 0, 0, 0, 0), -- 40077 The Invasion Begins
(3689, 0, 0, 0, 0), -- 38728 The Keystone
(3692, 0, 0, 0, 0), -- 40222 The Imp Mother's Tome
(3697, 0, 0, 0, 0); -- 38759 Set Them Free

DELETE FROM `treasure_picker_items` WHERE `TreasurePickerID` IN (3588, 3688, 3689, 3692, 3697);
INSERT INTO `treasure_picker_items` (`TreasurePickerID`, `Idx`, `ItemID`, `ItemQuantity`, `BonusListID`, `Context`, `VerifiedBuild`) VALUES
-- 3688 / 40077 — Treads of Illidari Supremacy
(3688, 0, 128953, 1, 0, 0, 0), -- Alliance recolour
(3688, 1, 133317, 1, 0, 0, 0), -- Horde recolour
-- 3697 / 38759 — Torment Ender's Chestguard
(3697, 0, 128952, 1, 0, 0, 0), -- Alliance recolour
(3697, 1, 133312, 1, 0, 0, 0), -- Horde recolour
-- 3588 / 38765 — Leggings of Sacrifice
(3588, 0, 128951, 1, 0, 0, 0), -- Alliance recolour
(3588, 1, 133316, 1, 0, 0, 0), -- Horde recolour
-- 3692 / 40222 — Power Handler's Gloves
(3692, 0, 128954, 1, 0, 0, 0), -- Alliance recolour
(3692, 1, 133314, 1, 0, 0, 0), -- Horde recolour
-- 3689 / 38728 — The Brood Queen's Veil
(3689, 0, 128955, 1, 0, 0, 0), -- Alliance (Night Elf listing)
(3689, 1, 133310, 1, 0, 0, 0); -- Horde (Blood Elf listing)

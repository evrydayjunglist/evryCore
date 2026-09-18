-- Chromie Time: campaign start breadcrumbs, and Silithus Wound for current timelines.

-- Breadcrumbs after select (QUEST_GIVER_QUEST_DETAILS, not a silent add).
-- Cata keeps Onward to Adventure. BfA Alliance stays Tides of War (46727), not Time to Onboard (53370).
-- TBC/WotLK/MoP/WoD/Legion replace the Onward pair with the expansion start quests.
DELETE FROM `chromie_time_expansion_quest` WHERE `UiExpansionId` IN (5, 6, 7, 8, 9, 10, 14, 15, 16);
INSERT INTO `chromie_time_expansion_quest` (`UiExpansionId`, `AllianceQuestId`, `HordeQuestId`, `VerifiedBuild`) VALUES
(5, 60891, 60887, 67808),  -- Cata: Onward to Adventure: Eastern Kingdoms / Kalimdor
(6, 60120, 60123, 0),     -- TBC: To Outland!
(7, 60096, 60097, 0),     -- WotLK: To Northrend!
(8, 60125, 60126, 0),     -- MoP: To Pandaria!
(9, 36881, 34398, 0),     -- WoD: The Dark Portal (Alliance 36881 / Horde 34398)
(10, 40519, 43926, 0),    -- Legion: The Legion Returns
(14, 60545, 61874, 67808), -- SL: A Chilling Summons
(15, 46727, 51443, 67808), -- BfA: Tides of War / Mission Statement
(16, 65436, 65435, 67808); -- DF: The Dragon Isles Await

-- Silithus: The Wound (map 1817). Stock gated this at level >= 110, which cannot succeed at current max level.
-- Show the Wound unless the player is in Cataclysm through Legion Chromie Time (Ui 5–10).
-- Same ElseGroup (AND). CONDITION_CHROMIE_TIME = 61. Present, Shadowlands, BfA, and Dragonflight still see the Wound.
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 25 AND `SourceEntry` = 1817;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(25, 0, 1817, 0, 0, 61, 0, 5, 0, 0, '', 1, 0, 0, '', 'TerrainSwap 1817: hide when UiChromieTimeExpansionID is Cataclysm (5)'),
(25, 0, 1817, 0, 0, 61, 0, 6, 0, 0, '', 1, 0, 0, '', 'TerrainSwap 1817: hide when UiChromieTimeExpansionID is Burning Crusade (6)'),
(25, 0, 1817, 0, 0, 61, 0, 7, 0, 0, '', 1, 0, 0, '', 'TerrainSwap 1817: hide when UiChromieTimeExpansionID is Wrath (7)'),
(25, 0, 1817, 0, 0, 61, 0, 8, 0, 0, '', 1, 0, 0, '', 'TerrainSwap 1817: hide when UiChromieTimeExpansionID is Mists (8)'),
(25, 0, 1817, 0, 0, 61, 0, 9, 0, 0, '', 1, 0, 0, '', 'TerrainSwap 1817: hide when UiChromieTimeExpansionID is Warlords (9)'),
(25, 0, 1817, 0, 0, 61, 0, 10, 0, 0, '', 1, 0, 0, '', 'TerrainSwap 1817: hide when UiChromieTimeExpansionID is Legion (10)');

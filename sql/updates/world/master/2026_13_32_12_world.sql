-- Chromie Time: Expansion (UIChromieTimeExpansionInfo) → faction breadcrumb quests
-- UIChromieTimeExpansionInfo has no QuestID column; world map supplies later xpac rows.
-- Phase 2 V1: BC Ui ID 6 → Alliance 60959 / Horde 60961 (CT-A AutoLaunched details).
-- Flow 2R dual-check PASS: NN=12 free on master (no 2026_13_32 world) + evry (max 09) + tip (10/11).

DROP TABLE IF EXISTS `chromie_time_expansion_quest`;
CREATE TABLE `chromie_time_expansion_quest` (
  `UiExpansionId` int unsigned NOT NULL COMMENT 'UIChromieTimeExpansionInfo.ID',
  `AllianceQuestId` int unsigned NOT NULL DEFAULT '0',
  `HordeQuestId` int unsigned NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`UiExpansionId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM `chromie_time_expansion_quest` WHERE `UiExpansionId` = 6;
INSERT INTO `chromie_time_expansion_quest` (`UiExpansionId`, `AllianceQuestId`, `HordeQuestId`, `VerifiedBuild`) VALUES
(6, 60959, 60961, 68887);

-- Dark Portal EK <-> Outland (world_safe_locs 3736/3737 already present; teleport rows missing in TDB)
-- CT-A travel: Blasted Lands DP then map 530. Not WoD assault scripts.
DELETE FROM `areatrigger_teleport` WHERE `ID` IN (4352, 4354);
INSERT INTO `areatrigger_teleport` (`ID`, `PortLocID`, `Name`) VALUES
(4352, 3736, 'Dark Portal - E. Kingdoms Target'),
(4354, 3737, 'Dark Portal - Outland Target');

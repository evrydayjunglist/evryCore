-- Chromie Time: empty hotfix shell for UIChromieTimeExpansionInfo (data from client .db2)
-- Flow 2R dual-check PASS: NN=01 free on master+evry (evry has 2026_13_32_00_hotfixes.sql)

CREATE TABLE IF NOT EXISTS `ui_chromie_time_expansion_info` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `AllianceOverrideDesc` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `HordeOverrideDesc` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `SpellID` int NOT NULL DEFAULT '0',
  `MapAtlasElement` int NOT NULL DEFAULT '0',
  `PreviewAtlasElement` int NOT NULL DEFAULT '0',
  `ShowPlayerConditionID` int NOT NULL DEFAULT '0',
  `ExpansionMask` int NOT NULL DEFAULT '0',
  `ContentTuningID` int NOT NULL DEFAULT '0',
  `CompletedPlayerConditionID` int NOT NULL DEFAULT '0',
  `SortPriority` int NOT NULL DEFAULT '0',
  `RecommendPlayerConditionID` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `ui_chromie_time_expansion_info_locale` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `locale` varchar(4) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Description_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `AllianceOverrideDesc_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `HordeOverrideDesc_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`locale`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

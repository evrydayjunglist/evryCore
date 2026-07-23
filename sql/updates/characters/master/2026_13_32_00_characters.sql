-- Chromie Time: persist selected UIChromieTimeExpansionInfo ID across relog
-- Flow 2R dual-check PASS: NN=00 free on master+evry for characters

CREATE TABLE IF NOT EXISTS `character_chromie_time` (
  `guid` bigint unsigned NOT NULL,
  `uiExpansionId` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

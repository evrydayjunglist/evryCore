-- Separate event applicability from upstream spawn-group definitions.
-- Bit 0: ordinary world, bit 1: Pandaria, bit 2: Legion. No row means ordinary only.
CREATE TABLE `spawn_group_timerunning` (
  `GroupId` int unsigned NOT NULL,
  `SeasonMask` tinyint unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (`GroupId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

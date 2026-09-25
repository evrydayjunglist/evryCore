-- Housing and neighborhood tables, folded from the source branch's schema file and its later changes.
-- Nothing here drops, alters or fills an existing table.
--
-- A house belongs to a Battle.net account, not to a character: every character of the account sees and
-- edits it, and the character who bought it is only shown as its owner. character_housing.guid is the
-- house's own database id; the decor, room and fixture rows point at it through houseGuid, and the decor
-- catalog is kept per Battle.net account.

CREATE TABLE IF NOT EXISTS `character_housing` (
    `guid` BIGINT UNSIGNED NOT NULL COMMENT 'House database id',
    `bnetAccountId` INT UNSIGNED NOT NULL COMMENT 'Battle.net account that owns the house',
    `slot` TINYINT UNSIGNED NOT NULL COMMENT 'Which of the account''s houses this is: 1 for the first, 2 for the second',
    `cosmeticOwnerGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Character shown as the owner',
    `neighborhoodGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `plotIndex` TINYINT UNSIGNED NOT NULL DEFAULT 255,
    `houseLevel` INT UNSIGNED NOT NULL DEFAULT 1,
    `favor` INT UNSIGNED NOT NULL DEFAULT 0,
    `settingsFlags` INT UNSIGNED NOT NULL DEFAULT 0,
    `exteriorLocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `houseSize` TINYINT UNSIGNED NOT NULL DEFAULT 2,
    `houseType` INT UNSIGNED NOT NULL DEFAULT 0,
    `createTime` INT UNSIGNED NOT NULL DEFAULT 0,
    `posX` FLOAT NOT NULL DEFAULT 0,
    `posY` FLOAT NOT NULL DEFAULT 0,
    `posZ` FLOAT NOT NULL DEFAULT 0,
    `facing` FLOAT NOT NULL DEFAULT 0,
    `houseName` VARCHAR(64) NOT NULL DEFAULT '',
    `houseDescription` VARCHAR(256) NOT NULL DEFAULT '',
    `packed` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 while the house is packed up and stands on no plot',
    `activeNeighborhoodGuid` BIGINT UNSIGNED AS (IF(`packed` = 0, `neighborhoodGuid`, NULL)) STORED COMMENT 'The neighborhood while the house stands on a plot, NULL while it is packed',
    PRIMARY KEY (`guid`),
    UNIQUE KEY `idx_account_slot` (`bnetAccountId`, `slot`),
    UNIQUE KEY `idx_neighborhood_plot` (`activeNeighborhoodGuid`, `plotIndex`),
    KEY `idx_neighborhood` (`neighborhoodGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_housing_decor` (
    `houseGuid` BIGINT UNSIGNED NOT NULL COMMENT 'character_housing.guid',
    `id` BIGINT UNSIGNED NOT NULL,
    `houseDecorId` INT UNSIGNED NOT NULL,
    `posX` FLOAT NOT NULL DEFAULT 0,
    `posY` FLOAT NOT NULL DEFAULT 0,
    `posZ` FLOAT NOT NULL DEFAULT 0,
    `rotX` FLOAT NOT NULL DEFAULT 0,
    `rotY` FLOAT NOT NULL DEFAULT 0,
    `rotZ` FLOAT NOT NULL DEFAULT 0,
    `rotW` FLOAT NOT NULL DEFAULT 1,
    `scale` FLOAT NOT NULL DEFAULT 1,
    `dyeSlot0` INT UNSIGNED NOT NULL DEFAULT 0,
    `dyeSlot1` INT UNSIGNED NOT NULL DEFAULT 0,
    `dyeSlot2` INT UNSIGNED NOT NULL DEFAULT 0,
    `roomGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `locked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `placementTime` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `sourceType` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sourceValue` VARCHAR(128) NOT NULL DEFAULT '',
    `petGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `petFlag` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`houseGuid`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_housing_rooms` (
    `houseGuid` BIGINT UNSIGNED NOT NULL COMMENT 'character_housing.guid',
    `id` BIGINT UNSIGNED NOT NULL,
    `houseRoomId` INT UNSIGNED NOT NULL,
    `slotIndex` INT UNSIGNED NOT NULL DEFAULT 0,
    `gridX` INT NOT NULL DEFAULT 0,
    `gridY` INT NOT NULL DEFAULT 0,
    `floorIndex` INT NOT NULL DEFAULT 0,
    `orientation` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `mirrored` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `themeId` INT UNSIGNED NOT NULL DEFAULT 0,
    `wallTextureId` INT UNSIGNED NOT NULL DEFAULT 0,
    `floorTextureId` INT UNSIGNED NOT NULL DEFAULT 0,
    `ceilingTextureId` INT UNSIGNED NOT NULL DEFAULT 0,
    `colorOverride` INT NOT NULL DEFAULT -1,
    `doorTypeId` INT UNSIGNED NOT NULL DEFAULT 0,
    `doorSlot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `ceilingTypeId` INT UNSIGNED NOT NULL DEFAULT 0,
    `ceilingSlot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `wallThemeId` INT UNSIGNED NOT NULL DEFAULT 0,
    `floorThemeId` INT UNSIGNED NOT NULL DEFAULT 0,
    `ceilingThemeId` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`houseGuid`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_housing_fixtures` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `houseGuid` BIGINT UNSIGNED NOT NULL COMMENT 'character_housing.guid',
    `fixturePointId` INT UNSIGNED NOT NULL,
    `fixtureOptionId` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    INDEX `idx_house` (`houseGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_housing_catalog` (
    `bnetAccountId` INT UNSIGNED NOT NULL COMMENT 'Battle.net account that owns the decor',
    `houseDecorId` INT UNSIGNED NOT NULL,
    `quantity` INT UNSIGNED NOT NULL DEFAULT 1,
    `acquiredTime` INT UNSIGNED NOT NULL DEFAULT 0,
    `sourceType` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sourceValue` VARCHAR(128) NOT NULL DEFAULT '',
    PRIMARY KEY (`bnetAccountId`, `houseDecorId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhoods` (
    `guid` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `name` VARCHAR(64) NOT NULL,
    `neighborhoodMapId` INT UNSIGNED NOT NULL,
    `ownerGuid` BIGINT UNSIGNED NOT NULL,
    `factionRestriction` INT NOT NULL DEFAULT 0,
    `isPublic` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `createTime` INT UNSIGNED NOT NULL DEFAULT 0,
    `guildId` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`),
    INDEX `idx_owner` (`ownerGuid`),
    INDEX `idx_map` (`neighborhoodMapId`),
    INDEX `idx_guild` (`guildId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_members` (
    `neighborhoodGuid` BIGINT UNSIGNED NOT NULL,
    `playerGuid` BIGINT UNSIGNED NOT NULL,
    `role` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `joinTime` INT UNSIGNED NOT NULL DEFAULT 0,
    `plotIndex` TINYINT UNSIGNED NOT NULL DEFAULT 255,
    PRIMARY KEY (`neighborhoodGuid`, `playerGuid`),
    INDEX `idx_player` (`playerGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_invites` (
    `neighborhoodGuid` BIGINT UNSIGNED NOT NULL,
    `inviteeGuid` BIGINT UNSIGNED NOT NULL,
    `inviterGuid` BIGINT UNSIGNED NOT NULL,
    `inviteTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`neighborhoodGuid`, `inviteeGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_charters` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `creatorGuid` BIGINT UNSIGNED NOT NULL,
    `name` VARCHAR(64) NOT NULL,
    `neighborhoodMapId` INT UNSIGNED NOT NULL,
    `factionFlags` INT UNSIGNED NOT NULL DEFAULT 0,
    `isGuild` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `createTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    INDEX `idx_creator` (`creatorGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_charter_signatures` (
    `charterId` BIGINT UNSIGNED NOT NULL,
    `signerGuid` BIGINT UNSIGNED NOT NULL,
    `signTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`charterId`, `signerGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiatives` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `neighborhoodGuid` BIGINT UNSIGNED NOT NULL,
    `initiativeId` INT UNSIGNED NOT NULL,
    `startTime` INT UNSIGNED NOT NULL DEFAULT 0,
    `progress` FLOAT NOT NULL DEFAULT 0,
    `completed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    INDEX `idx_neighborhood` (`neighborhoodGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative_task_progress` (
    `initiativeDbId` BIGINT UNSIGNED NOT NULL,
    `taskId` INT UNSIGNED NOT NULL,
    `progress` INT UNSIGNED NOT NULL DEFAULT 0,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`initiativeDbId`, `taskId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative_milestones` (
    `initiativeDbId` BIGINT UNSIGNED NOT NULL,
    `milestoneIndex` INT UNSIGNED NOT NULL,
    `reached` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `reachedTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`initiativeDbId`, `milestoneIndex`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative_reward_claims` (
    `initiativeDbId` BIGINT UNSIGNED NOT NULL,
    `milestoneIndex` INT UNSIGNED NOT NULL,
    `playerGuid` BIGINT UNSIGNED NOT NULL,
    `claimTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`initiativeDbId`, `milestoneIndex`, `playerGuid`),
    INDEX `idx_player` (`playerGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative_contributions` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `initiativeDbId` BIGINT UNSIGNED NOT NULL,
    `playerGuid` BIGINT UNSIGNED NOT NULL,
    `taskId` INT UNSIGNED NOT NULL,
    `amount` INT UNSIGNED NOT NULL DEFAULT 0,
    `lastUpdated` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    UNIQUE INDEX `idx_initiative_player_task` (`initiativeDbId`, `playerGuid`, `taskId`),
    INDEX `idx_player` (`playerGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_housing_ignored_neighborhood` (
    `ownerGuid` BIGINT UNSIGNED NOT NULL,
    `neighborhoodGuid` BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (`ownerGuid`, `neighborhoodGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `account_housing_blueprint` (
    `id` BIGINT UNSIGNED NOT NULL,
    `uuid` CHAR(36) NOT NULL,
    `bnetAccountId` INT UNSIGNED NOT NULL,
    `exporterGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Character that exported it',
    `name` VARCHAR(64) NOT NULL DEFAULT '',
    `type` TINYINT UNSIGNED NOT NULL COMMENT 'HousingBlueprintType: 1 House, 2 Room, 3 Interior, 4 Exterior',
    `flags` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'HousingBlueprintFlag: 1 AutomaticBackup',
    `createTime` BIGINT NOT NULL DEFAULT 0,
    `content` MEDIUMBLOB NOT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `idx_uuid` (`uuid`),
    KEY `idx_bnetAccountId` (`bnetAccountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

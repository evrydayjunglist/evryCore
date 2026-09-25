-- Housing and neighborhood tables, folded from the source branch's schema file and its later changes.
-- Nothing here drops, alters or fills an existing table.
--
-- A house belongs to a Battle.net account, not to a character: every character of the account sees and
-- edits it, and the character who bought it is only shown as its owner. character_housing.guid is the
-- house's own database id; the room and fixture rows point at it through houseGuid.
--
-- Decor belongs to the Battle.net account as well, one row per piece with its own GUID. A piece placed in a
-- house names that house in houseGuid, which it keeps while the house is packed; a piece in storage has 0.

CREATE TABLE IF NOT EXISTS `character_housing` (
    `guid` BIGINT UNSIGNED NOT NULL COMMENT 'House database id',
    `bnetAccountId` INT UNSIGNED NOT NULL COMMENT 'Battle.net account that owns the house',
    `slot` TINYINT UNSIGNED NOT NULL COMMENT 'Which of the account''s houses this is: 1 for the first, 2 for the second',
    `cosmeticOwnerGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Character shown as the owner',
    `neighborhoodGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'The neighborhood the house stands in; for a packed house, the one it last stood in',
    `plotIndex` TINYINT UNSIGNED NOT NULL DEFAULT 255 COMMENT 'The plot the house stands on; for a packed house, the one it last stood on',
    `houseLevel` INT UNSIGNED NOT NULL DEFAULT 1,
    `favor` INT UNSIGNED NOT NULL DEFAULT 0,
    `settingsFlags` INT UNSIGNED NOT NULL DEFAULT 0,
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
    `refundAmount` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Copper paid for the house, paid back when it is relinquished',
    `activeNeighborhoodGuid` BIGINT UNSIGNED AS (IF(`packed` = 0, `neighborhoodGuid`, NULL)) STORED COMMENT 'The neighborhood while the house stands on a plot, NULL while it is packed',
    PRIMARY KEY (`guid`),
    UNIQUE KEY `idx_account_slot` (`bnetAccountId`, `slot`),
    UNIQUE KEY `idx_neighborhood_plot` (`activeNeighborhoodGuid`, `plotIndex`),
    KEY `idx_neighborhood` (`neighborhoodGuid`),
    KEY `idx_cosmetic_owner` (`cosmeticOwnerGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `account_housing_decor` (
    `guid` BIGINT UNSIGNED NOT NULL COMMENT 'Low part of the decor GUID, from one counter shared by every account',
    `bnetAccountId` INT UNSIGNED NOT NULL COMMENT 'Battle.net account that owns the decor',
    `houseDecorId` INT UNSIGNED NOT NULL,
    `sourceType` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'How the account got it: 2 starter, 3 redeemed, 6 item, 7 and 8 shop licenses',
    `sourceValue` VARCHAR(128) NOT NULL DEFAULT '',
    `houseGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'character_housing.guid of the house it is placed in, 0 while it is in storage',
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
    `roomGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'character_housing_rooms.id of the interior room it stands in, 0 in the yard or in storage',
    `locked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `placementTime` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `petGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `petFlag` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`),
    KEY `idx_account` (`bnetAccountId`),
    KEY `idx_house` (`houseGuid`)
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

CREATE TABLE IF NOT EXISTS `account_housing_decor_entry` (
    `bnetAccountId` INT UNSIGNED NOT NULL,
    `houseDecorId` INT UNSIGNED NOT NULL COMMENT 'A decor entry the account has owned at least once',
    `redeemed` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Owed copies of this entry already turned into decor',
    `firstOwnedTime` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`bnetAccountId`, `houseDecorId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `account_housing_catalog_fetch` (
    `bnetAccountId` INT UNSIGNED NOT NULL,
    `lastFetchTime` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time the client last said it fetched the decor catalog',
    PRIMARY KEY (`bnetAccountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- A Battle.net account's first house is free and comes with the starter decor, once. The row stays when that house is
-- packed or gone, so a later purchase pays the plot's price and brings no second starter set.
CREATE TABLE IF NOT EXISTS `account_housing_first_house` (
    `bnetAccountId` INT UNSIGNED NOT NULL,
    `purchaseTime` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time the account bought its first house',
    PRIMARY KEY (`bnetAccountId`)
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

-- A charter is signed once per Battle.net account, never by the creator's account, and an account signs one open
-- charter at a time.
CREATE TABLE IF NOT EXISTS `neighborhood_charters` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `creatorGuid` BIGINT UNSIGNED NOT NULL,
    `creatorBnetAccountId` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Battle.net account of the creator, which may not sign',
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
    `signerGuid` BIGINT UNSIGNED NOT NULL COMMENT 'Character that signed',
    `signerBnetAccountId` INT UNSIGNED NOT NULL COMMENT 'Battle.net account of that character',
    `signTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`charterId`, `signerGuid`),
    UNIQUE KEY `idx_signer_account` (`signerBnetAccountId`)
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
    `completionTime` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'When the task was completed, shown in the activity log',
    PRIMARY KEY (`initiativeDbId`, `taskId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative_milestones` (
    `initiativeDbId` BIGINT UNSIGNED NOT NULL,
    `milestoneIndex` INT UNSIGNED NOT NULL,
    `reached` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `reachedTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`initiativeDbId`, `milestoneIndex`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Endeavor contributions and coffer claims belong to the Battle.net account: tasks, their progress and the coffer
-- are shared by the Warband.
CREATE TABLE IF NOT EXISTS `neighborhood_initiative_reward_claims` (
    `initiativeDbId` BIGINT UNSIGNED NOT NULL,
    `milestoneIndex` INT UNSIGNED NOT NULL,
    `bnetAccountId` INT UNSIGNED NOT NULL,
    `claimTime` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`initiativeDbId`, `milestoneIndex`, `bnetAccountId`),
    INDEX `idx_account` (`bnetAccountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `neighborhood_initiative_contributions` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `initiativeDbId` BIGINT UNSIGNED NOT NULL,
    `bnetAccountId` INT UNSIGNED NOT NULL,
    `playerGuid` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'The character of the account that contributed last, named in the activity log',
    `taskId` INT UNSIGNED NOT NULL,
    `amount` FLOAT NOT NULL DEFAULT 0 COMMENT 'Fractional, as the activity log shows it',
    `lastUpdated` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    UNIQUE INDEX `idx_initiative_account_task` (`initiativeDbId`, `bnetAccountId`, `taskId`),
    INDEX `idx_account` (`bnetAccountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_housing_active_neighborhood` (
    `guid` BIGINT UNSIGNED NOT NULL COMMENT 'Character',
    `neighborhoodGuid` BIGINT UNSIGNED NOT NULL COMMENT 'The neighborhood she chose for her active endeavor',
    PRIMARY KEY (`guid`)
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

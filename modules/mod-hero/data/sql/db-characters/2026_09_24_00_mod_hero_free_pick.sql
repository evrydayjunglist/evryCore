-- Hero starter adaptation: eight Ability Essence once, with full paid-cost refunds.
-- A revision update changes the existing wallet; it must never create another allocation.

CREATE TABLE IF NOT EXISTS `character_hero_freepick` (
  `realm` INT UNSIGNED NOT NULL,
  `guid` BIGINT UNSIGNED NOT NULL,
  `profile` VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `mode` SMALLINT UNSIGNED NOT NULL DEFAULT 601,
  `rules_revision` INT UNSIGNED NOT NULL DEFAULT 1,
  `version` INT UNSIGNED NOT NULL DEFAULT 0,
  `previous_version` INT UNSIGNED NOT NULL DEFAULT 0,
  `balance` INT UNSIGNED NOT NULL DEFAULT 8,
  `source_epoch` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`realm`,`guid`,`profile`),
  CONSTRAINT `hero_freepick_balance` CHECK (`balance` <= 8),
  CONSTRAINT `hero_freepick_mode` CHECK (`mode` = 601),
  CONSTRAINT `hero_freepick_version` CHECK (
    (`version` = 0 AND `previous_version` = 0) OR
    (CAST(`version` AS UNSIGNED) = CAST(`previous_version` AS UNSIGNED) + 1))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `character_hero_freepick_owned` (
  `realm` INT UNSIGNED NOT NULL,
  `guid` BIGINT UNSIGNED NOT NULL,
  `profile` VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `catalog` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `advancement` INT UNSIGNED NOT NULL,
  `spell` INT UNSIGNED NOT NULL,
  `paid` INT UNSIGNED NOT NULL,
  `rules_revision` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`realm`,`guid`,`profile`,`catalog`,`advancement`),
  CONSTRAINT `hero_freepick_paid` CHECK (`paid` BETWEEN 1 AND 8),
  CONSTRAINT `hero_freepick_owned_profile` FOREIGN KEY (`realm`,`guid`,`profile`)
    REFERENCES `character_hero_freepick` (`realm`,`guid`,`profile`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Immutable server session tokens let ordinary saves persist provenance before
-- the asynchronous login read completes, without reordering earlier sessions.
CREATE TABLE IF NOT EXISTS `character_hero_source_session` (
  `realm` INT UNSIGNED NOT NULL,
  `guid` BIGINT UNSIGNED NOT NULL,
  `profile` VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `token` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `epoch` BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (`realm`,`guid`,`profile`,`token`),
  CONSTRAINT `hero_source_session_profile` FOREIGN KEY (`realm`,`guid`,`profile`)
    REFERENCES `character_hero_freepick` (`realm`,`guid`,`profile`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `character_hero_freepick_request` (
  `realm` INT UNSIGNED NOT NULL,
  `guid` BIGINT UNSIGNED NOT NULL,
  `profile` VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `request_id` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `expected_version` INT UNSIGNED NOT NULL,
  `desired_mask` SMALLINT UNSIGNED NOT NULL,
  `result` TINYINT UNSIGNED NOT NULL,
  `committed_version` INT UNSIGNED NULL,
  `created` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`realm`,`guid`,`profile`,`request_id`),
  UNIQUE KEY `hero_freepick_one_commit` (`realm`,`guid`,`profile`,`committed_version`),
  CONSTRAINT `hero_freepick_mask` CHECK (`desired_mask` <= 1023),
  CONSTRAINT `hero_freepick_request_profile` FOREIGN KEY (`realm`,`guid`,`profile`)
    REFERENCES `character_hero_freepick` (`realm`,`guid`,`profile`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `character_hero_spell_source` (
  `realm` INT UNSIGNED NOT NULL,
  `guid` BIGINT UNSIGNED NOT NULL,
  `spell` INT UNSIGNED NOT NULL,
  `independent` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `revision` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `epoch` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`realm`,`guid`,`spell`),
  CONSTRAINT `hero_spell_independent` CHECK (`independent` IN (0,1))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- The tombstone prevents an already queued profile creation from undoing deletion.
CREATE TABLE IF NOT EXISTS `character_hero_freepick_deleted` (
  `realm` INT UNSIGNED NOT NULL,
  `guid` BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (`realm`,`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

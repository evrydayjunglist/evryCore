-- Where a character's resources are kept when the client has no slot for them.
--
-- The ten resources a class holds are saved with the character, in the powers
-- columns of the characters table, because they live in the update fields the
-- client reads. The ones held beside those ten are not in any update field, so
-- they need somewhere of their own. One row per character per resource.
--
-- mod-hero writes these rows when a character is created, saved or logged out,
-- reads them back on login, and deletes them when the character is deleted.
-- The maximum is written for the record only: it is worked out again from the
-- resource's own PowerType row every time the character logs in, because that
-- is what the core does for the ten held ones.
--
-- This goes with 2026_09_20_11_mod_hero_extra_powers.sql in db-world, which
-- says which resources those are.
--
-- The guid column is BIGINT UNSIGNED to match characters.guid and every other
-- character table in this database, all of which were read on 20 September.

CREATE TABLE IF NOT EXISTS `character_hero_power` (
  `guid` BIGINT UNSIGNED NOT NULL,
  `power` TINYINT UNSIGNED NOT NULL,
  `value` INT NOT NULL DEFAULT 0,
  `maxvalue` INT NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`,`power`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Resources a character holds that have no slot among the ten the client knows about';

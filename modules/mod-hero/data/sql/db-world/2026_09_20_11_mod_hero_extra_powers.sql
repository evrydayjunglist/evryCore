-- The eleven resources a Hero holds that the client has nowhere to put.
--
-- A class may hold at most ten resources. That is how many slots the client
-- keeps per class and how many the update fields carry, and it is not enforced
-- kindly at either end: the server loads the rows into a fixed ten entry array
-- without a bounds check, and the client's own table builder ends the whole
-- build on an eleventh row for any class, leaving that class and every class
-- after it with no slots at all. Hero's ten are in
-- 2026_09_20_00_mod_hero_class.sql and they stay exactly as they are. Nothing
-- here adds a row to chr_classes_x_power_types.
--
-- These eleven live beside those ten instead. The server holds them, spells
-- check and take them, and they regenerate; the client is never told they
-- exist, because there is no slot to tell it about. mod-hero reads this table
-- at startup and hands the core a mask per class.
--
-- This is a world table and never a hotfix table, on purpose. A hotfix row is
-- pushed to the client, and the client must not see any of this.
--
-- Seventeen real resources and four alternate bars are live, twenty-one in all.
-- Hero already holds Mana, Rage, Focus, Energy, Combo Points, Soul Shards and
-- the four alternate bars, which leaves exactly these eleven. Types 14, 15 and
-- 20 to 22 are obsolete and are not resources anyone can hold.

CREATE TABLE IF NOT EXISTS `class_extra_power` (
  `ClassID` TINYINT UNSIGNED NOT NULL,
  `PowerType` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`ClassID`,`PowerType`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Powers a class holds that have no slot among the ten the client knows about';

INSERT INTO `class_extra_power` (`ClassID`,`PowerType`)
SELECT 16,`PowerType` FROM (
             SELECT  5 AS `PowerType`  -- Runes
    UNION ALL SELECT  6                -- Runic Power
    UNION ALL SELECT  8                -- Lunar Power
    UNION ALL SELECT  9                -- Holy Power
    UNION ALL SELECT 11                -- Maelstrom
    UNION ALL SELECT 12                -- Chi
    UNION ALL SELECT 13                -- Insanity
    UNION ALL SELECT 16                -- Arcane Charges
    UNION ALL SELECT 17                -- Fury
    UNION ALL SELECT 18                -- Pain
    UNION ALL SELECT 19                -- Essence
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `class_extra_power` `dst` WHERE `dst`.`ClassID`=16 AND `dst`.`PowerType`=`src`.`PowerType`);

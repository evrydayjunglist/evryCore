-- Restore Hero world rows that a greenfield apply can lose, and put back
-- Reaper races the same apply can steal.
--
-- The companion to 2026_09_23_00_mod_hero_restore_class.sql in hotfixes. The
-- updater sorts by filename only, so on a fresh world database the order is:
--
--   2026_09_19_01_mod_reaper_world.sql
--   2026_09_20_01_mod_hero_world.sql                 -- hero < reaper
--   2026_09_20_01_mod_reaper_world_class_id_17.sql
--   2026_09_20_03_mod_hero_every_race_world.sql
--
-- Reaper inserts start positions, level stats and expansion rows at class 16
-- first. Hero's first world file then skips any race Reaper already holds, and
-- skips `player_classlevelstats` entirely because that table is keyed by class
-- and level, not race. The Reaper remap is unscoped (`UPDATE ... SET class=17
-- WHERE class=16`, and the same for ClassID), so it moves every class-16 row,
-- including the Hero start positions Hero did manage to insert (Orc, Dwarf,
-- Night Elf, Tauren, Gnome). The later every-race file then adds start
-- positions and expansion rows at class 16 for the twenty-one extra races,
-- with no level-1 stat line for that class.
--
-- `ObjectMgr::LoadPlayerInfo` aborts if a `playercreateinfo` row has no level 1
-- `player_classlevelstats` for the same class, once `chr_classes` 16 exists.
-- After the hotfix restore puts Hero back, a greenfield world that still lacks
-- those stats stops the server at boot. The same hole also leaves Human, Orc,
-- Dwarf, Night Elf, Undead, Tauren, Gnome, Troll, Blood Elf and Draenei with
-- no Hero creation path, and leaves Reaper creatable as five races it was
-- never given.
--
-- A realm that applied the files as they landed is fine. Do not edit the
-- already-applied remap to scope it: the updater re-applies a file whose hash
-- changed, and `UPDATE playercreateinfo SET class=17 WHERE class=16 AND race
-- IN (1,5,8,10,11)` would steal the Hero rows that already live at those
-- races on a migrated database.
--
-- This file is new, so both kinds of database run it. Inserts are guarded.
-- The five deletes only match rows Reaper was never given.

-- Level stats, the same Demon Hunter line the first Hero file used. These have
-- to exist before a Hero `playercreateinfo` row is loadable.
INSERT INTO `player_classlevelstats` (`class`,`level`,`str`,`agi`,`sta`,`inte`,`spi`,`VerifiedBuild`)
SELECT 16,`level`,`str`,`agi`,`sta`,`inte`,`spi`,0 FROM (
    SELECT `level`,`str`,`agi`,`sta`,`inte`,`spi`
    FROM `player_classlevelstats` WHERE `class`=12
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `player_classlevelstats` `dst` WHERE `dst`.`class`=16 AND `dst`.`level`=`src`.`level`);

-- Start positions for every race Hero is allowed, copied from that race's
-- Warrior row. The first Hero file covered the original ten; the every-race
-- file covered the rest. Doing all thirty-one here fills whichever set a
-- greenfield apply dropped.
INSERT INTO `playercreateinfo` (`race`,`class`,`map`,`position_x`,`position_y`,`position_z`,`orientation`)
SELECT `race`,16,`map`,`position_x`,`position_y`,`position_z`,`orientation` FROM (
    SELECT `race`,`map`,`position_x`,`position_y`,`position_z`,`orientation`
    FROM `playercreateinfo`
    WHERE `class`=1 AND `race` IN (1,2,3,4,5,6,7,8,9,10,11,22,24,25,26,27,28,29,30,31,32,34,35,36,37,52,70,84,85,86,91)
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo` `dst` WHERE `dst`.`class`=16 AND `dst`.`race`=`src`.`race`);

-- No expansion is required to be a Hero, for any of those races.
INSERT INTO `class_expansion_requirement` (`ClassID`,`RaceID`,`ActiveExpansionLevel`,`AccountExpansionLevel`)
SELECT 16,`RaceID`,0,0 FROM (
             SELECT  1 AS `RaceID`  -- Human
    UNION ALL SELECT  2             -- Orc
    UNION ALL SELECT  3             -- Dwarf
    UNION ALL SELECT  4             -- Night Elf
    UNION ALL SELECT  5             -- Undead
    UNION ALL SELECT  6             -- Tauren
    UNION ALL SELECT  7             -- Gnome
    UNION ALL SELECT  8             -- Troll
    UNION ALL SELECT  9             -- Goblin
    UNION ALL SELECT 10             -- Blood Elf
    UNION ALL SELECT 11             -- Draenei
    UNION ALL SELECT 22             -- Worgen
    UNION ALL SELECT 24             -- Pandaren, neutral
    UNION ALL SELECT 25             -- Pandaren, Alliance
    UNION ALL SELECT 26             -- Pandaren, Horde
    UNION ALL SELECT 27             -- Nightborne
    UNION ALL SELECT 28             -- Highmountain Tauren
    UNION ALL SELECT 29             -- Void Elf
    UNION ALL SELECT 30             -- Lightforged Draenei
    UNION ALL SELECT 31             -- Zandalari Troll
    UNION ALL SELECT 32             -- Kul Tiran
    UNION ALL SELECT 34             -- Dark Iron Dwarf
    UNION ALL SELECT 35             -- Vulpera
    UNION ALL SELECT 36             -- Mag'har Orc
    UNION ALL SELECT 37             -- Mechagnome
    UNION ALL SELECT 52             -- Dracthyr, Alliance
    UNION ALL SELECT 70             -- Dracthyr, Horde
    UNION ALL SELECT 84             -- Earthen, Alliance
    UNION ALL SELECT 85             -- Earthen, Horde
    UNION ALL SELECT 86             -- Haranir, Alliance
    UNION ALL SELECT 91             -- Haranir, Horde
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `class_expansion_requirement` `dst`
                  WHERE `dst`.`ClassID`=16 AND `dst`.`RaceID`=`src`.`RaceID`);

-- Give Reaper back the five races it does not have. On a greenfield apply the
-- unscoped remap moved Hero's Orc, Dwarf, Night Elf, Tauren and Gnome rows
-- onto class 17. Reaper was only ever Human, Undead, Troll, Blood Elf and
-- Draenei; a migrated database never has these five at 17, so the deletes
-- are a no-op there.
DELETE FROM `playercreateinfo` WHERE `class`=17 AND `race` IN (2,3,4,6,7);
DELETE FROM `class_expansion_requirement` WHERE `ClassID`=17 AND `RaceID` IN (2,3,4,6,7);

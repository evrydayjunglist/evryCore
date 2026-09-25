-- Hero's expansion rows for Dracthyr, Earthen and Haranir, put back after
-- TrinityCore's 2026_09_20_00_world.sql.
--
-- That update deletes every class_expansion_requirement row for races 52, 70,
-- 84, 85, 86 and 91 and inserts again only the classes TrinityCore knows, so it
-- removes the Hero rows 2026_09_20_03_mod_hero_every_race_world.sql added for
-- those six races. Without them a Hero of those races cannot be created, and
-- the class list sent to the client leaves Hero out for them.
--
-- The updater orders core and module files together by file name, so this runs
-- after TrinityCore's file on an existing database. On a new database the
-- mod-hero file runs after it too, and this one then finds its rows already
-- there. Both cases are covered by only inserting rows that are missing.
--
-- This is a new file rather than an edit to 2026_09_20_03, because that one has
-- already been applied and the updater keys on file name.
INSERT INTO `class_expansion_requirement` (`ClassID`,`RaceID`,`ActiveExpansionLevel`,`AccountExpansionLevel`)
SELECT 16,`RaceID`,0,0 FROM (
             SELECT 52 AS `RaceID`  -- Dracthyr, Alliance
    UNION ALL SELECT 70             -- Dracthyr, Horde
    UNION ALL SELECT 84             -- Earthen, Alliance
    UNION ALL SELECT 85             -- Earthen, Horde
    UNION ALL SELECT 86             -- Haranir, Alliance
    UNION ALL SELECT 91             -- Haranir, Horde
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `class_expansion_requirement` `dst`
                  WHERE `dst`.`ClassID`=16 AND `dst`.`RaceID`=`src`.`RaceID`);

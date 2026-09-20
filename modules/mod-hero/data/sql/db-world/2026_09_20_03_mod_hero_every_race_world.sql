-- The world rows for the twenty-one races Hero gained.
--
-- The companion to 2026_09_20_02_mod_hero_every_race.sql in the hotfixes
-- database. A start position without an expansion row makes creation fail, so
-- both go in together.
--
-- player_classlevelstats is not touched: it is keyed by class and level, not by
-- race, so the rows the first Hero file added already cover every race.
--
-- This is a new file rather than an edit to 2026_09_20_01_mod_hero_world.sql,
-- because that one has already been applied and the updater keys on filename.

-- Where a Hero of each new race starts: that race's own Warrior start position,
-- so nobody begins somewhere invented. Every one of these races has a Warrior
-- row to copy, including the four that have no Warrior expansion row.
INSERT INTO `playercreateinfo` (`race`,`class`,`map`,`position_x`,`position_y`,`position_z`,`orientation`)
SELECT `race`,16,`map`,`position_x`,`position_y`,`position_z`,`orientation` FROM (
    SELECT `race`,`map`,`position_x`,`position_y`,`position_z`,`orientation`
    FROM `playercreateinfo`
    WHERE `class`=1 AND `race` IN (9,22,24,25,26,27,28,29,30,31,32,34,35,36,37,52,70,84,85,86,91)
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo` `dst` WHERE `dst`.`class`=16 AND `dst`.`race`=`src`.`race`);

-- No expansion is required to be a Hero, for any race. This table also feeds
-- the class list the client is told about.
--
-- Four of these races have no Warrior row here at all: both Dracthyr (52, 70)
-- and both Haranir (86, 91). Hero gets its own rows for them rather than
-- inheriting Warrior's absence, which is what makes them creatable as a Hero
-- when they are not creatable as a Warrior. That is the owner's decision and it
-- is the least proven part of this change.
INSERT INTO `class_expansion_requirement` (`ClassID`,`RaceID`,`ActiveExpansionLevel`,`AccountExpansionLevel`)
SELECT 16,`RaceID`,0,0 FROM (
             SELECT  9 AS `RaceID`  -- Goblin
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

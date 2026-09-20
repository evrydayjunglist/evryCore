-- The world rows a Hero needs before one can be made.
--
-- These three go together on purpose. A playercreateinfo row without a level 1
-- player_classlevelstats row for the same class stops the server at boot, and a
-- missing class_expansion_requirement row makes creation fail, so adding any
-- one of them alone is worse than adding none.
--
-- Class 16 must already exist in the core and in the hotfixes database.

-- Where a Hero starts. Each of the ten races gets its own Warrior start
-- position rather than an invented spawn, the same way Reaper does. Warrior is
-- the one class every race can be, so there is a row to copy for all ten. The
-- new player experience columns are left empty, so a Hero begins in its own
-- racial starting zone and the experiment has one fewer moving part.
INSERT INTO `playercreateinfo` (`race`,`class`,`map`,`position_x`,`position_y`,`position_z`,`orientation`)
SELECT `race`,16,`map`,`position_x`,`position_y`,`position_z`,`orientation` FROM (
    SELECT `race`,`map`,`position_x`,`position_y`,`position_z`,`orientation`
    FROM `playercreateinfo` WHERE `class`=1 AND `race` IN (1,2,3,4,5,6,7,8,10,11)
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo` `dst` WHERE `dst`.`class`=16 AND `dst`.`race`=`src`.`race`);

-- Level stats, copied from Demon Hunter. A Hero has no main stat: it shows a
-- Mana bar, so the class row reads as intellect, but a Hero who buys Warrior
-- abilities wants strength and one who buys Rogue abilities wants agility.
-- Demon Hunter's line is the one that feeds all three rather than leaving two
-- of them far behind, which is the same reason Reaper uses it. These are
-- placeholders until Free Pick decides what a Hero actually is.
--
-- Base mana does not come from this table. It comes from a client game table
-- with one fixed column per class, where Hero reads Druid's column, because
-- Death Knight's is zero at every level and would give a Hero a Mana bar that
-- maxes at nothing.
INSERT INTO `player_classlevelstats` (`class`,`level`,`str`,`agi`,`sta`,`inte`,`spi`,`VerifiedBuild`)
SELECT 16,`level`,`str`,`agi`,`sta`,`inte`,`spi`,0 FROM (
    SELECT `level`,`str`,`agi`,`sta`,`inte`,`spi`
    FROM `player_classlevelstats` WHERE `class`=12
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `player_classlevelstats` `dst` WHERE `dst`.`class`=16 AND `dst`.`level`=`src`.`level`);

-- No expansion is required to be a Hero. This table also feeds the class list
-- the client is told about, so a missing row here is not only a creation
-- failure.
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
    UNION ALL SELECT 10             -- Blood Elf
    UNION ALL SELECT 11             -- Draenei
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `class_expansion_requirement` `dst` WHERE `dst`.`ClassID`=16 AND `dst`.`RaceID`=`src`.`RaceID`);

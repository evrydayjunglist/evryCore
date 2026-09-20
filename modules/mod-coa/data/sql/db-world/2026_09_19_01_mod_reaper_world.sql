-- The world rows a Reaper needs before one can be made.
--
-- These three go together on purpose. A playercreateinfo row without a level 1
-- player_classlevelstats row for the same class stops the server at boot, and
-- a missing class_expansion_requirement row makes creation fail, so adding any
-- one of them alone is worse than adding none.
--
-- Class 16 must already exist in the core and in the hotfixes database.

-- Where a Reaper starts. Each race gets its own Warrior start position rather
-- than an invented spawn. The new player experience columns are left empty, so
-- a Reaper begins in its own racial starting zone and the experiment has one
-- fewer moving part.
INSERT INTO `playercreateinfo` (`race`,`class`,`map`,`position_x`,`position_y`,`position_z`,`orientation`)
SELECT `race`,16,`map`,`position_x`,`position_y`,`position_z`,`orientation` FROM (
    SELECT `race`,`map`,`position_x`,`position_y`,`position_z`,`orientation`
    FROM `playercreateinfo` WHERE `class`=1 AND `race` IN (1,5,8,10,11)
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo` `dst` WHERE `dst`.`class`=16 AND `dst`.`race`=`src`.`race`);

-- Level stats, copied from Demon Hunter rather than Death Knight. Reaper takes
-- a different main stat in each spec, so it needs a class whose stat line feeds
-- all three: at level 90 Demon Hunter carries 515 strength, 620 agility and 620
-- intellect, while Death Knight's 335 intellect would leave the Soul spec far
-- behind the other two. These are placeholders until the class is tuned.
INSERT INTO `player_classlevelstats` (`class`,`level`,`str`,`agi`,`sta`,`inte`,`spi`,`VerifiedBuild`)
SELECT 16,`level`,`str`,`agi`,`sta`,`inte`,`spi`,0 FROM (
    SELECT `level`,`str`,`agi`,`sta`,`inte`,`spi`
    FROM `player_classlevelstats` WHERE `class`=12
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `player_classlevelstats` `dst` WHERE `dst`.`class`=16 AND `dst`.`level`=`src`.`level`);

-- No expansion is required to be a Reaper. This table also feeds the class
-- list the client is told about, so a missing row here is not only a creation
-- failure.
INSERT INTO `class_expansion_requirement` (`ClassID`,`RaceID`,`ActiveExpansionLevel`,`AccountExpansionLevel`)
SELECT 16,`RaceID`,0,0 FROM (
             SELECT  1 AS `RaceID`  -- Human
    UNION ALL SELECT  5             -- Undead
    UNION ALL SELECT  8             -- Troll
    UNION ALL SELECT 10             -- Blood Elf
    UNION ALL SELECT 11             -- Draenei
) AS `src`
WHERE NOT EXISTS (SELECT 1 FROM `class_expansion_requirement` `dst` WHERE `dst`.`ClassID`=16 AND `dst`.`RaceID`=`src`.`RaceID`);

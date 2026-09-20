-- Move the Reaper world rows from class 16 to class 17.
--
-- The companion to 2026_09_20_00_mod_reaper_class_id_17.sql in the hotfixes
-- database. These three tables go together: a start position with no level 1
-- stat line stops the server at boot, and a missing expansion row makes
-- creation fail, so all three move at once or none of them do.
--
-- This is a new file rather than an edit to 2026_09_19_01_mod_reaper_world.sql,
-- because that one has already been applied and the updater keys on filename.

-- Where a Reaper starts, one row per playable race.
UPDATE `playercreateinfo` SET `class`=17 WHERE `class`=16;

-- The level 1 to 90 stat line, copied from Demon Hunter when it was written.
UPDATE `player_classlevelstats` SET `class`=17 WHERE `class`=16;

-- No expansion is required to be a Reaper. This table also feeds the class list
-- the client is told about, so it has to follow the class id.
UPDATE `class_expansion_requirement` SET `ClassID`=17 WHERE `ClassID`=16;

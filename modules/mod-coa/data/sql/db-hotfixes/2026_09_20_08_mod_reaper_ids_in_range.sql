-- Move Reaper's remaining custom row ids inside the range the client ships.
--
-- Same reason and same shape as Hero's half of this change,
-- 2026_09_20_07_mod_hero_ids_in_range.sql in mod-hero, which explains the rule
-- in full. In short: on 20 September a specialisation id of 9101 was proved to
-- break a client-side lookup that is sized by the real range, and moving it to
-- 1479 fixed it. A custom row whose id sits far above the client's own highest
-- id for that table is therefore a hazard, whatever the table.
--
-- Nothing is currently misbehaving because of the four tables below. This is
-- hardening against a failure mode that has been proved once, not a fix for a
-- live break.
--
-- Hero takes the first block in each of the free runs listed in that file and
-- Reaper takes the next, packed with no gap, so the ids stay in class order and
-- the next Conquest of Azeroth class can carry on from there. Every id below
-- was checked free against both the client's DB2 and the live hotfix tables.
--
--   chr_classes_x_power_types   9001..9005  ->  100..104   next class starts at 105
--   char_base_info              9001..9005  ->  461..465   next class starts at 466
--   skill_line                        9001  -> 1311        next class starts at 1312
--   skill_race_class_info             9001  -> 1217        next class starts at 1218
--
-- Reaper's four specialisations already moved to 610 to 613 in
-- 2026_09_20_01_mod_reaper_spec_in_range.sql. The specialisation page still
-- raises when a Reaper opens the spellbook, and this file does not fix that:
-- the client's Lua holds a table of format strings keyed by specialisation id
-- and no custom id is in it at any magnitude. That is its own job.
--
-- This is a new file rather than an edit to the Reaper files already applied,
-- because the updater keys on filename and every one of those has run.
--
-- The rows are copied from the ones they replace rather than retyped, and each
-- id map is spelled out rather than calculated. Each move reads a count into a
-- variable first: asking inside the INSERT would mean naming the table being
-- inserted into, which MySQL does not always allow, and reading it before the
-- DELETE lets the delete refuse unless every replacement row is there, so a
-- half-applied file cannot leave Reaper with nothing.

-- The five power types. Only the row id changes.
SET @ReaperPowersAlreadyMoved := (SELECT COUNT(*) FROM `chr_classes_x_power_types` WHERE `ID`=100);

INSERT INTO `chr_classes_x_power_types` (`ID`,`PowerType`,`ClassID`,`VerifiedBuild`)
SELECT `map`.`NewID`,`old`.`PowerType`,17,0
FROM `chr_classes_x_power_types` `old`
JOIN (
             SELECT 9001 AS `OldID`, 100 AS `NewID`  -- Runic Power, the one Reaper spends
    UNION ALL SELECT 9002, 101                       -- Alternate
    UNION ALL SELECT 9003, 102                       -- Alternate, quest
    UNION ALL SELECT 9004, 103                       -- Alternate, encounter
    UNION ALL SELECT 9005, 104                       -- Alternate, mount
) AS `map` ON `map`.`OldID` = `old`.`ID`
WHERE `old`.`ClassID` = 17 AND @ReaperPowersAlreadyMoved = 0;

SET @ReaperPowersMoved := (SELECT COUNT(*) FROM `chr_classes_x_power_types` WHERE `ID` BETWEEN 100 AND 104 AND `ClassID`=17);
DELETE FROM `chr_classes_x_power_types`
    WHERE `ID` BETWEEN 9001 AND 9005 AND `ClassID`=17 AND @ReaperPowersMoved = 5;

-- The five races that may be a Reaper. The race each row names and the
-- other-faction race it pairs with come across unchanged. Nothing in any
-- database points at a char_base_info id, which was checked against every
-- column in the characters, world and hotfixes schemas, and the core only ever
-- looks this table up by race and class.
SET @ReaperRacesAlreadyMoved := (SELECT COUNT(*) FROM `char_base_info` WHERE `ID`=461);

INSERT INTO `char_base_info` (`ID`,`RaceID`,`ClassID`,`OtherFactionRaceID`,`VerifiedBuild`)
SELECT `map`.`NewID`,`old`.`RaceID`,17,`old`.`OtherFactionRaceID`,0
FROM `char_base_info` `old`
JOIN (
             SELECT 9001 AS `OldID`, 461 AS `NewID`  -- Human
    UNION ALL SELECT 9002, 462                       -- Undead
    UNION ALL SELECT 9003, 463                       -- Troll
    UNION ALL SELECT 9004, 464                       -- Blood Elf
    UNION ALL SELECT 9005, 465                       -- Draenei
) AS `map` ON `map`.`OldID` = `old`.`ID`
WHERE `old`.`ClassID` = 17 AND @ReaperRacesAlreadyMoved = 0;

SET @ReaperRacesMoved := (SELECT COUNT(*) FROM `char_base_info` WHERE `ID` BETWEEN 461 AND 465 AND `ClassID`=17);
DELETE FROM `char_base_info`
    WHERE `ID` BETWEEN 9001 AND 9005 AND `ClassID`=17 AND @ReaperRacesMoved = 5;

-- The class skill line. Its id is a real skill id that other rows point at: the
-- skill_race_class_info row below names it, and two Reapers already have it
-- stored in characters.character_skills. A skill_race_class_info row whose
-- SkillID names no skill_line row is dropped while the core loads, so the
-- SkillID has to move in the same file. The stored character rows move in
-- 2026_09_20_10_mod_reaper_ids_in_range_characters.sql, which has to be applied
-- with this one.
SET @ReaperSkillLineAlreadyMoved := (SELECT COUNT(*) FROM `skill_line` WHERE `ID`=1311);

INSERT INTO `skill_line` (`DisplayName`,`AlternateVerb`,`Description`,`HordeDisplayName`,
    `OverrideSourceInfoDisplayName`,`ID`,`CategoryID`,`SpellIconFileID`,`CanLink`,
    `ParentSkillLineID`,`ParentTierIndex`,`Flags`,`SpellBookSpellID`,
    `ExpansionNameSharedStringID`,`HordeExpansionNameSharedStringID`,`VerifiedBuild`)
SELECT `DisplayName`,`AlternateVerb`,`Description`,`HordeDisplayName`,
    `OverrideSourceInfoDisplayName`,1311,`CategoryID`,`SpellIconFileID`,`CanLink`,
    `ParentSkillLineID`,`ParentTierIndex`,`Flags`,`SpellBookSpellID`,
    `ExpansionNameSharedStringID`,`HordeExpansionNameSharedStringID`,0
FROM `skill_line` WHERE `ID`=9001 AND @ReaperSkillLineAlreadyMoved = 0;

SET @ReaperSkillLineMoved := (SELECT COUNT(*) FROM `skill_line` WHERE `ID`=1311);
DELETE FROM `skill_line` WHERE `ID`=9001 AND @ReaperSkillLineMoved = 1;

-- The one row that grants that skill line to class 17. Class mask 65536 is bit
-- 17, which this row took when Reaper moved off class 16. Its SkillID follows
-- the skill line above.
--
-- Lowering this id cannot change which row the core picks. It walks this table
-- in row id order and keeps the first row matching the race and class, and no
-- row the client ships matches class 17 at all.
SET @ReaperSkillAlreadyMoved := (SELECT COUNT(*) FROM `skill_race_class_info` WHERE `ID`=1217);

INSERT INTO `skill_race_class_info` (`ID`,`SkillID`,`ClassMask`,`Flags`,`Availability`,
    `MinLevel`,`SkillTierID`,`RaceMask1`,`RaceMask2`,`VerifiedBuild`)
SELECT 1217,1311,`ClassMask`,`Flags`,`Availability`,`MinLevel`,`SkillTierID`,`RaceMask1`,`RaceMask2`,0
FROM `skill_race_class_info` WHERE `ID`=9001 AND `ClassMask`=65536 AND @ReaperSkillAlreadyMoved = 0;

SET @ReaperSkillMoved := (SELECT COUNT(*) FROM `skill_race_class_info` WHERE `ID`=1217);
DELETE FROM `skill_race_class_info` WHERE `ID`=9001 AND `ClassMask`=65536 AND @ReaperSkillMoved = 1;

-- One push carrying all of it: twelve rows exist at their new ids and twelve
-- are gone from their old ones. Status 1 is valid and status 2 is
-- RecordRemoved, which is also what makes the server drop the old record from
-- its own store while it starts.
--
-- The push id must be higher than every push before it, because the core reads
-- them in id order and the last word about a record wins; the old ids were
-- declared valid in earlier pushes and this has to overrule that. Taking the
-- next free id gives that, which is 111756 as this is written, with Hero's file
-- taking 111755 just before it. Their records do not overlap, so which of the
-- two runs first does not matter.
--
-- The unique id is fixed rather than random so re-running this file cannot
-- quietly change it. 1609394640 was checked against the live table and is free.
-- 1609394509, 1609394510, 1609394601, 1609394602, 1609394604, 1609394610,
-- 1609394620 and Hero's 1609394630 are taken, and reusing one would make the
-- guard below skip this whole push in silence.
--
-- 1609394600 and 1609394603 are burnt too, even though neither is in the live
-- table. The Hero class file writes 1609394600, and the spell-family probe on
-- 20 September moved that push to 1609394603 and then to 1609394604 by hand so
-- the client would re-download it. A new file taking either would pass the
-- guard here but collide on a database rebuilt from the files.
--
-- Without a new unique id a client that has already played a Reaper keeps every
-- row it cached.
SET @ReaperRangePush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394640 LIMIT 1);
SET @ReaperRangePush := COALESCE(@ReaperRangePush, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @ReaperRangePush,1609394640,`TableHash`,`RecordId`,`Status` FROM (
    -- ChrClassesXPowerTypes 0xC0315ACF, the new rows then the old ones
              SELECT 3224459983 AS `TableHash`, 100 AS `RecordId`, 1 AS `Status`
    UNION ALL SELECT 3224459983,  101, 1 UNION ALL SELECT 3224459983,  102, 1
    UNION ALL SELECT 3224459983,  103, 1 UNION ALL SELECT 3224459983,  104, 1
    UNION ALL SELECT 3224459983, 9001, 2 UNION ALL SELECT 3224459983, 9002, 2
    UNION ALL SELECT 3224459983, 9003, 2 UNION ALL SELECT 3224459983, 9004, 2
    UNION ALL SELECT 3224459983, 9005, 2
    -- CharBaseInfo 0x3067A8F8
    UNION ALL SELECT  812099832,  461, 1 UNION ALL SELECT  812099832,  462, 1
    UNION ALL SELECT  812099832,  463, 1 UNION ALL SELECT  812099832,  464, 1
    UNION ALL SELECT  812099832,  465, 1
    UNION ALL SELECT  812099832, 9001, 2 UNION ALL SELECT  812099832, 9002, 2
    UNION ALL SELECT  812099832, 9003, 2 UNION ALL SELECT  812099832, 9004, 2
    UNION ALL SELECT  812099832, 9005, 2
    -- SkillLine 0xB53DC9D6
    UNION ALL SELECT 3040725462, 1311, 1
    UNION ALL SELECT 3040725462, 9001, 2
    -- SkillRaceClassInfo 0x06ADE420
    UNION ALL SELECT  112059424, 1217, 1
    UNION ALL SELECT  112059424, 9001, 2
) AS `push`
WHERE NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId`=1609394640);

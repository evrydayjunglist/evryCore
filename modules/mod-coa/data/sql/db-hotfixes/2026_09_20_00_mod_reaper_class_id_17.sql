-- Move the Reaper class from id 16 to id 17.
--
-- Class 16 is being freed for Hero, which the owner wants ahead of every
-- Conquest of Azeroth class. Nothing about Reaper changes except its number.
--
-- This is a new file rather than an edit to 2026_09_19_00_mod_reaper_class.sql,
-- because that one has already been applied and the updater keys on filename.
--
-- The core must already know class 17 before this runs. `MAX_CLASSES` is 18 and
-- `CLASS_REAPER` is 17 in `SharedDefines.h`; the server asserts on the class id
-- of these rows while it loads them, so a server that still thinks the highest
-- class is 16 stops at boot. The rebuilt worldserver applies this file at
-- startup and then loads the stores, so one restart does both in the right
-- order.
--
-- Delete the two Reaper characters before the rebuilt server boots. Use
-- `.coa reaper unmake Grimscythe` and `.coa reaper unmake Reaper` on the server
-- that is still running with Reaper at 16, because the rebuilt one only
-- recognises a Reaper at 17 and will refuse them.

-- Which push told the client about the Reaper rows. Both unique ids are
-- accepted so that re-running this file finds the push either way.
SET @ReaperPush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId` IN (1609394509, 1609394510) LIMIT 1);

-- The class itself.
UPDATE `chr_classes` SET `ID`=17 WHERE `ID`=16;

-- The five power types, the four specialisations and the five race pairings.
-- Each of these keeps its own id; only the class it points at changes.
UPDATE `chr_classes_x_power_types` SET `ClassID`=17 WHERE `ClassID`=16;
UPDATE `chr_specialization` SET `ClassID`=17 WHERE `ClassID`=16;
UPDATE `char_base_info` SET `ClassID`=17 WHERE `ClassID`=16;

-- The class skill line's class mask. Bit 16 (32768) becomes bit 17 (65536).
-- This column is a signed 32 bit integer, so bit 17 fits with room to spare.
UPDATE `skill_race_class_info` SET `ClassMask`=65536 WHERE `ID`=9001 AND `ClassMask`=32768;

-- The push row that names the ChrClasses record. Every other row in the push
-- points at an id of ours that did not move, so only this one changes.
UPDATE `hotfix_data` SET `RecordId`=17
    WHERE `Id`=@ReaperPush AND `TableHash`=4119371148 AND `RecordId`=16;

-- Tell the client that ChrClasses record 16 is gone. Status 2 is
-- RecordRemoved. Without this the client can keep the class 16 row it cached
-- from an earlier session and draw a second Reaper at the old id, which matters
-- because the widened class gate now accepts both 16 and 17.
--
-- When Hero becomes class 16, its own push will carry a valid ChrClasses record
-- 16 under a higher push id. The server reads these rows in push order and the
-- last word wins, so Hero's row will simply supersede this one. Do not delete
-- this row to make room for Hero; add Hero's push after it.
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @ReaperPush,1609394510,4119371148,16,2
WHERE NOT EXISTS (
    SELECT 1 FROM `hotfix_data` `existing`
    WHERE `existing`.`Id`=@ReaperPush AND `existing`.`TableHash`=4119371148 AND `existing`.`RecordId`=16);

-- Give the whole push a new unique id. The client is told which pushes exist by
-- their push id and unique id together, and only asks for the contents of a
-- pair it has not already cached. Leaving the unique id alone would let a
-- client that has played a Reaper before keep every row below at class 16.
-- 1609394510 is one more than the original, fixed rather than random so that
-- re-running this file cannot quietly change it.
UPDATE `hotfix_data` SET `UniqueId`=1609394510 WHERE `Id`=@ReaperPush;

-- Move Reaper's four specialisations inside the range the client ships.
--
-- Same fault and same fix as Hero. A class whose specialisation id sits outside
-- the range this client ships draws every spell its owner knows as spell
-- 205523, Brewmaster Monk's Blackout Kick: wrong name, wrong description, wrong
-- icon, wrong animation, and the client refuses the cast for not holding that
-- spell's axe. Tested on a Reaper on 20 September with `.learn 116`, which drew
-- Blackout Kick exactly as a Hero's did. Moving Hero from 9101 to 1479 fixed it
-- outright, with nothing patched on the client.
--
-- Every specialisation this client ships has an id between 62 and 1480, sixty
-- of them. Reaper's are 9001 to 9004. These four move to 610 to 613.
--
-- Why 610 and not something near the top. Blizzard allocates specialisation ids
-- upward from whatever the current maximum is. Read in order, the shipped ids
-- are 62 to 81, then 102 to 105, then 250 to 270, then 535 to 581, then 1444 to
-- 1456, then 1465 to 1480: every expansion's new specialisations land above
-- everything that came before. The gap from 582 to 1443 is 862 ids that
-- Blizzard has skipped over for twenty years and never come back to, so it is
-- the safest place to sit. The ids just under 1480 are the opposite: that is
-- exactly where the next ones will land.
--
-- Hero's 1479 is in that growth zone and should move to 600 when Hero's
-- specialisations are next rewritten for Free Pick and Wildcard. It works today
-- and is not urgent; do not open a file just for it.
--
-- The four replacements are copied from the rows they replace, so only the id
-- changes. Retyping them by hand would have dropped the descriptions Harvest,
-- Soul and Domination carry, which the client shows on the specialisation page.
--
-- The id map is spelled out rather than calculated, so a row holding something
-- unexpected is left alone instead of being moved somewhere wrong. Order
-- indexes come across unchanged: 0, 1 and 2 for the three visible ones, 4 for
-- Initial, which is the index character creation asks for by name.
--
-- Whether this file has already run is read into a variable first. Asking
-- inside the INSERT would mean naming the table being inserted into, which
-- MySQL does not always allow.
SET @ReaperSpecsAlreadyMoved := (SELECT COUNT(*) FROM `chr_specialization` WHERE `ID`=610);

INSERT INTO `chr_specialization` (`Name`,`FemaleName`,`Description`,`ID`,`ClassID`,`OrderIndex`,
    `PetTalentType`,`Role`,`Flags`,`SpellIconFileID`,`PrimaryStatPriority`,`AnimReplacements`,
    `MasterySpellID1`,`MasterySpellID2`,`VerifiedBuild`)
SELECT `old`.`Name`,`old`.`FemaleName`,`old`.`Description`,`map`.`NewID`,17,`old`.`OrderIndex`,
    `old`.`PetTalentType`,`old`.`Role`,`old`.`Flags`,`old`.`SpellIconFileID`,
    `old`.`PrimaryStatPriority`,`old`.`AnimReplacements`,`old`.`MasterySpellID1`,
    `old`.`MasterySpellID2`,0
FROM `chr_specialization` `old`
JOIN (
             SELECT 9001 AS `OldID`, 610 AS `NewID`   -- Harvest
    UNION ALL SELECT 9002, 611                        -- Soul
    UNION ALL SELECT 9003, 612                        -- Domination
    UNION ALL SELECT 9004, 613                        -- Initial, the required one
) AS `map` ON `map`.`OldID` = `old`.`ID`
WHERE `old`.`ClassID` = 17 AND @ReaperSpecsAlreadyMoved = 0;

-- The old rows go. Leaving them would give class 17 eight specialisations, two
-- of them at order index 4.
DELETE FROM `chr_specialization` WHERE `ID` IN (9001,9002,9003,9004) AND `ClassID`=17;

-- The class row points at the new first specialisation.
UPDATE `chr_classes` SET `DefaultSpec`=610 WHERE `ID`=17;

-- One push carrying all nine facts: four specialisations exist, four are gone,
-- and the class row changed. Status 1 is valid, status 2 is RecordRemoved.
--
-- The push id must be higher than every push before it, because the core reads
-- them in id order and the last word about a record wins. The next free id is
-- 111754 as this is written.
--
-- 1609394620 was checked against the live table and is free. 1609394510,
-- 1609394601, 1609394602, 1609394604 and 1609394610 are taken by the Reaper and
-- Hero files already applied, and reusing one would make the guard below skip
-- this whole push in silence. Without a new unique id a client that has already
-- played a Reaper would keep every row it cached, including the old ids.
SET @ReaperSpecPush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394620 LIMIT 1);
SET @ReaperSpecPush := COALESCE(@ReaperSpecPush, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @ReaperSpecPush,1609394620,`TableHash`,`RecordId`,`Status` FROM (
             SELECT 2685374048 AS `TableHash`,  610 AS `RecordId`, 1 AS `Status`  -- ChrSpecialization 0xA00F8E60
    UNION ALL SELECT 2685374048,  611, 1
    UNION ALL SELECT 2685374048,  612, 1
    UNION ALL SELECT 2685374048,  613, 1
    UNION ALL SELECT 2685374048, 9001, 2                                          -- the old ids are gone
    UNION ALL SELECT 2685374048, 9002, 2
    UNION ALL SELECT 2685374048, 9003, 2
    UNION ALL SELECT 2685374048, 9004, 2
    UNION ALL SELECT 4119371148,   17, 1                                          -- ChrClasses 0xF5889D8C, DefaultSpec changed
) AS `push`
WHERE NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId`=1609394620);

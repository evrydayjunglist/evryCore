-- Give Hero one block of specialisation ids: 600 is Initial, the hidden
-- specialisation every Hero starts on, and 601, 602 and 603 are the three
-- modes a Hero can pick.
--
-- Why this block. The client breaks when a specialisation id sits far above
-- the ones it ships, which run from 62 to 1480; that is the spellbook failure
-- in evryLoader\docs\SPELLBOOK_FALLBACK_69814.md. Nothing the client ships
-- uses 582 to 1443. Initial used to be 1479, which works but sits just under
-- the client's highest id, where Blizzard adds new ones. Reaper already has
-- 610 to 613. 600 to 603 were checked against the pinned client's own
-- ChrSpecialization table and every specialisation column in the hotfix and
-- characters databases, and nothing else uses them.
--
-- Initial keeps order index 4, which is where character creation and a reset
-- look for it, and every value the 1479 row had except the id. The modes use
-- order indexes 0 to 2, so the client lists them in that order. They grant
-- nothing yet: no spells, no talent tree, no currency. Role 2 and primary
-- stat priority 0 match Initial.
--
-- The saved Heroes move with it in
-- db-characters\2026_09_22_01_mod_hero_specialization_ids_characters.sql.

START TRANSACTION;

-- Stop and change nothing if another class holds one of these ids, if Hero
-- has a specialisation this file does not know about, or if the push's
-- unique id already carries something else.
CREATE TEMPORARY TABLE `hero_spec_ids_guard` (`ok` TINYINT NOT NULL CHECK (`ok`=1));
INSERT INTO `hero_spec_ids_guard`
SELECT IF(
    NOT EXISTS (SELECT 1 FROM `chr_specialization` WHERE `ID` IN (600,601,602,603) AND `ClassID`<>16)
    AND NOT EXISTS (SELECT 1 FROM `chr_specialization` WHERE `ClassID`=16 AND `ID` NOT IN (600,601,602,603,1479))
    AND NOT EXISTS (SELECT 1 FROM `hotfix_blob` WHERE `TableHash`=2685374048 AND `RecordId` IN (600,601,602,603))
    AND NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId`=1609394670
        AND NOT (`TableHash`=2685374048 AND `RecordId` IN (600,601,602,603,1479))
        AND NOT (`TableHash`=4119371148 AND `RecordId`=16)), 1, 0);

-- Replace all of Hero's rows at once, so a database that ran an earlier
-- version of this file ends up the same as a new one.
DELETE FROM `chr_specialization` WHERE `ClassID`=16 AND `ID` IN (600,601,602,603,1479);

INSERT INTO `chr_specialization` (`Name`,`FemaleName`,`Description`,`ID`,`ClassID`,`OrderIndex`,
    `PetTalentType`,`Role`,`Flags`,`SpellIconFileID`,`PrimaryStatPriority`,`AnimReplacements`,
    `MasterySpellID1`,`MasterySpellID2`,`VerifiedBuild`) VALUES
('Initial','','',600,16,4,0,2,64,135771,0,0,0,0,0),
('Free Pick','',
    'Choose your path freely across all classes. Selecting this mode currently preserves your learned abilities; acquisition arrives later.',
    601,16,0,0,2,0,135771,0,0,0,0,0),
('Free Pick — Seasonal','',
    'Choose your path freely across all classes with the seasonal ruleset. Selecting this mode currently preserves your learned abilities; seasonal acquisition arrives later.',
    602,16,1,0,2,0,135771,0,0,0,0,0),
('Wildcard','',
    'Build around random ability choices. Selecting this mode currently preserves your learned abilities; rolls and cards arrive later.',
    603,16,2,0,2,0,135771,0,0,0,0,0);

-- The class row names its default specialisation, which the client reads.
UPDATE `chr_classes` SET `DefaultSpec`=600 WHERE `ID`=16;

-- One push carrying every change, so a client that cached the old rows
-- replaces them: the four rows as they are now, 1479 removed, and the class
-- row's new default. Status 1 is valid and status 2 is RecordRemoved, the
-- marker the move from 9101 to 1479 used. The push id has to be higher than
-- every push before it, because the core reads them in id order and the last
-- word about a record wins. The unique id is fixed so that running this file
-- again reuses the same push. Give it a new one whenever the rows change: a
-- client keeps the rows it cached for a unique id it has already seen.
SET @HeroSpecPush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394670 LIMIT 1);
SET @HeroSpecPush := COALESCE(@HeroSpecPush, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroSpecPush,1609394670,`TableHash`,`RecordId`,`Status` FROM (
              SELECT 2685374048 AS `TableHash`, 600 AS `RecordId`, 1 AS `Status`
    UNION ALL SELECT 2685374048, 601, 1
    UNION ALL SELECT 2685374048, 602, 1
    UNION ALL SELECT 2685374048, 603, 1
    UNION ALL SELECT 2685374048, 1479, 2
    UNION ALL SELECT 4119371148, 16, 1
) AS `push`
WHERE NOT EXISTS (SELECT 1 FROM `hotfix_data` h WHERE h.`UniqueId`=1609394670
    AND h.`TableHash`=`push`.`TableHash` AND h.`RecordId`=`push`.`RecordId`);

DROP TEMPORARY TABLE `hero_spec_ids_guard`;
COMMIT;

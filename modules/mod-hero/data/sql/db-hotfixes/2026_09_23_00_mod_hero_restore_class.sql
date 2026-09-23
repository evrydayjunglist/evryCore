-- Restore Hero hotfix rows that a greenfield apply can lose.
--
-- The updater keys updates by filename only and sorts that name as a string
-- (UpdateFetcher::PathCompare). On a database that has never seen these files,
-- that order is:
--
--   2026_09_19_00_mod_reaper_class.sql
--   2026_09_20_00_mod_hero_class.sql          -- hero < reaper
--   2026_09_20_00_mod_reaper_class_id_17.sql
--
-- Reaper is inserted at class 16 first. Hero's `WHERE NOT EXISTS (... ID=16)`
-- and `WHERE NOT EXISTS (... ClassID=16)` then skip the class, its ten power
-- types, its specialisation and its first ten races. The Reaper remaps that
-- follow are unscoped (`UPDATE ... SET ID=17 WHERE ID=16`, and the same for
-- ClassID), so they move Reaper to 17 and leave nothing at 16. Later Hero
-- files that copy 9101-block rows onto the in-range ids find nothing to copy
-- for those four tables.
--
-- A realm that applied the files as they landed is fine: Reaper moved to 17
-- before Hero was added, so Hero's inserts saw an empty class 16. Do not edit
-- the already-applied remap files to "fix" the order. The updater re-applies a
-- file whose hash changed, and a scoped world remap would steal the Hero
-- playercreateinfo rows that already live at class 16.
--
-- This file is new, so both kinds of database run it. Every statement is a
-- no-op when the in-range Hero rows are already there. The values are the
-- ones the later Hero files already settled on: specialisation 1479, power
-- types 90 to 99, races 430 to 460, skill line 1310, skill rows 1200 to 1216.
--
-- Do not add an eleventh power type. The core writes those rows into a fixed
-- ten-entry array with no bounds check.

-- The class itself. DefaultSpec is 1479, the id the spec-in-range file moved
-- it to, not the 9101 the first Hero file used.
INSERT INTO `chr_classes` (`Name`,`Filename`,`NameMale`,`NameFemale`,`PetNameToken`,`Description`,
    `RoleInfoString`,`DisabledString`,`HyphenatedNameMale`,`HyphenatedNameFemale`,
    `CreateScreenFileDataID`,`SelectScreenFileDataID`,`IconFileDataID`,`LowResScreenFileDataID`,
    `Flags`,`StartingLevel`,`SpellTextureBlobFileDataID`,`ArmorTypeMask`,`CharStartKitUnknown901`,
    `MaleCharacterCreationVisualFallback`,`MaleCharacterCreationIdleVisualFallback`,
    `FemaleCharacterCreationVisualFallback`,`FemaleCharacterCreationIdleVisualFallback`,
    `CharacterCreationIdleGroundVisualFallback`,`CharacterCreationGroundVisualFallback`,
    `AlteredFormCharacterCreationIdleVisualFallback`,`CharacterCreationAnimLoopWaitTimeMsFallback`,
    `CinematicSequenceID`,`DefaultSpec`,`ID`,`HasStrengthBonus`,`PrimaryStatPriority`,`DisplayPower`,
    `RangedAttackPowerPerAgility`,`AttackPowerPerAgility`,`AttackPowerPerStrength`,`SpellClassSet`,
    `ClassColorR`,`ClassColorG`,`ClassColorB`,`RolesMask`,`DamageBonusStat`,`HasRelicSlot`,`VerifiedBuild`)
SELECT 'Hero','HERO','Hero','Hero','PET',
    'A Hero has no class. Nothing is given to you at the start and nothing is ruled out: what you can do is bought, one ability at a time, from every class in the world.',
    'Roles: |cffffffffTank, Healer or Damage|r','You must choose a different race to be this class.','Hero','Hero',
    0,0,135771,0,
    2,1,791558,127,0,
    0,0,
    0,0,
    0,0,
    0,15000,
    0,1479,16,0,0,0,
    0,1,1,37,
    255,214,36,14,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `chr_classes` WHERE `ID`=16);

-- The ten slotted power types, already at the in-range ids.
INSERT INTO `chr_classes_x_power_types` (`ID`,`PowerType`,`ClassID`,`VerifiedBuild`)
SELECT `ID`,`PowerType`,16,0 FROM (
             SELECT 90 AS `ID`, 0 AS `PowerType`   -- Mana, and the bar Hero displays
    UNION ALL SELECT 91, 1                          -- Rage
    UNION ALL SELECT 92, 2                          -- Focus
    UNION ALL SELECT 93, 3                          -- Energy
    UNION ALL SELECT 94, 4                          -- Combo Points
    UNION ALL SELECT 95, 7                          -- Soul Shards
    UNION ALL SELECT 96, 10                         -- Alternate
    UNION ALL SELECT 97, 23                         -- Alternate, quest
    UNION ALL SELECT 98, 24                         -- Alternate, encounter
    UNION ALL SELECT 99, 25                         -- Alternate, mount
) AS `power`
WHERE NOT EXISTS (SELECT 1 FROM `chr_classes_x_power_types` `existing` WHERE `existing`.`ID`=`power`.`ID`);

-- The hidden initial specialisation. Character creation asks for OrderIndex 4.
INSERT INTO `chr_specialization` (`Name`,`FemaleName`,`Description`,`ID`,`ClassID`,`OrderIndex`,
    `PetTalentType`,`Role`,`Flags`,`SpellIconFileID`,`PrimaryStatPriority`,`AnimReplacements`,
    `MasterySpellID1`,`MasterySpellID2`,`VerifiedBuild`)
SELECT 'Initial','','',1479,16,4,0,2,64,135771,0,0,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `chr_specialization` WHERE `ID`=1479);

UPDATE `chr_classes` SET `DefaultSpec`=1479 WHERE `ID`=16 AND `Filename`='HERO' AND `DefaultSpec`<>1479;

-- All thirty-one races, at the in-range ids, with the Warrior other-faction
-- pairing (Orc pairs with Dwarf, not Human).
INSERT INTO `char_base_info` (`ID`,`RaceID`,`ClassID`,`OtherFactionRaceID`,`VerifiedBuild`)
SELECT `ID`,`RaceID`,16,`OtherFactionRaceID`,0 FROM (
             SELECT 430 AS `ID`,  1 AS `RaceID`,  5 AS `OtherFactionRaceID`  -- Human
    UNION ALL SELECT 431,  2,  3                                             -- Orc
    UNION ALL SELECT 432,  3,  2                                             -- Dwarf
    UNION ALL SELECT 433,  4,  8                                             -- Night Elf
    UNION ALL SELECT 434,  5,  1                                             -- Undead
    UNION ALL SELECT 435,  6, 11                                             -- Tauren
    UNION ALL SELECT 436,  7,  9                                             -- Gnome
    UNION ALL SELECT 437,  8,  4                                             -- Troll
    UNION ALL SELECT 438, 10,  1                                             -- Blood Elf
    UNION ALL SELECT 439, 11,  6                                             -- Draenei
    UNION ALL SELECT 440,  9,  7                                             -- Goblin
    UNION ALL SELECT 441, 22,  8                                             -- Worgen
    UNION ALL SELECT 442, 24, 24                                             -- Pandaren, neutral
    UNION ALL SELECT 443, 25, 26                                             -- Pandaren, Alliance
    UNION ALL SELECT 444, 26, 25                                             -- Pandaren, Horde
    UNION ALL SELECT 445, 27,  4                                             -- Nightborne
    UNION ALL SELECT 446, 28, 11                                             -- Highmountain Tauren
    UNION ALL SELECT 447, 29, 10                                             -- Void Elf
    UNION ALL SELECT 448, 30,  6                                             -- Lightforged Draenei
    UNION ALL SELECT 449, 31, 32                                             -- Zandalari Troll
    UNION ALL SELECT 450, 32, 31                                             -- Kul Tiran
    UNION ALL SELECT 451, 34, 36                                             -- Dark Iron Dwarf
    UNION ALL SELECT 452, 35, 37                                             -- Vulpera
    UNION ALL SELECT 453, 36, 34                                             -- Mag'har Orc
    UNION ALL SELECT 454, 37, 35                                             -- Mechagnome
    UNION ALL SELECT 455, 52, 70                                             -- Dracthyr, Alliance
    UNION ALL SELECT 456, 70, 52                                             -- Dracthyr, Horde
    UNION ALL SELECT 457, 84, 85                                             -- Earthen, Alliance
    UNION ALL SELECT 458, 85, 84                                             -- Earthen, Horde
    UNION ALL SELECT 459, 86, 91                                             -- Haranir, Alliance
    UNION ALL SELECT 460, 91, 86                                             -- Haranir, Horde
) AS `pair`
WHERE NOT EXISTS (SELECT 1 FROM `char_base_info` `existing` WHERE `existing`.`ID`=`pair`.`ID`);

-- A greenfield apply can leave the 9111-9131 race rows behind: the in-range
-- copy only deletes the old block when all thirty-one replacements landed,
-- and the first ten never existed to copy. Drop them once 430-460 are present.
SET @HeroRacesRestored := (SELECT COUNT(*) FROM `char_base_info` WHERE `ID` BETWEEN 430 AND 460 AND `ClassID`=16);
DELETE FROM `char_base_info`
    WHERE `ID` BETWEEN 9101 AND 9131 AND `ClassID`=16 AND @HeroRacesRestored = 31;

-- The class skill line, already at the in-range id.
INSERT INTO `skill_line` (`DisplayName`,`AlternateVerb`,`Description`,`HordeDisplayName`,
    `OverrideSourceInfoDisplayName`,`ID`,`CategoryID`,`SpellIconFileID`,`CanLink`,
    `ParentSkillLineID`,`ParentTierIndex`,`Flags`,`SpellBookSpellID`,
    `ExpansionNameSharedStringID`,`HordeExpansionNameSharedStringID`,`VerifiedBuild`)
SELECT 'Hero','','Hero skills.','','',1310,7,135771,0,0,0,1048,0,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `skill_line` WHERE `ID`=1310);

-- That skill line plus the sixteen weapon proficiencies.
INSERT INTO `skill_race_class_info` (`ID`,`SkillID`,`ClassMask`,`Flags`,`Availability`,
    `MinLevel`,`SkillTierID`,`RaceMask1`,`RaceMask2`,`VerifiedBuild`)
SELECT `ID`,`SkillID`,32768,`Flags`,1,`MinLevel`,0,-1,-1,0 FROM (
             SELECT 1200 AS `ID`, 1310 AS `SkillID`, 1048 AS `Flags`, 0 AS `MinLevel`  -- the Hero class skill line
    UNION ALL SELECT 1201,   43, 128, 0                                               -- Swords
    UNION ALL SELECT 1202,   44, 128, 0                                               -- Axes
    UNION ALL SELECT 1203,   45, 128, 0                                               -- Bows
    UNION ALL SELECT 1204,   46, 128, 0                                               -- Guns
    UNION ALL SELECT 1205,   54, 128, 0                                               -- Maces
    UNION ALL SELECT 1206,   55, 128, 0                                               -- Two-Handed Swords
    UNION ALL SELECT 1207,  118, 146, 1                                               -- Dual Wield
    UNION ALL SELECT 1208,  136, 128, 0                                               -- Staves
    UNION ALL SELECT 1209,  160, 128, 0                                               -- Two-Handed Maces
    UNION ALL SELECT 1210,  172, 128, 0                                               -- Two-Handed Axes
    UNION ALL SELECT 1211,  173, 128, 0                                               -- Daggers
    UNION ALL SELECT 1212,  226, 128, 0                                               -- Crossbows
    UNION ALL SELECT 1213,  228, 146, 0                                               -- Wands
    UNION ALL SELECT 1214,  229, 128, 0                                               -- Polearms
    UNION ALL SELECT 1215,  473, 130, 0                                               -- Fist Weapons
    UNION ALL SELECT 1216, 2152, 144, 0                                               -- Warglaives
) AS `skill`
WHERE NOT EXISTS (SELECT 1 FROM `skill_race_class_info` `existing` WHERE `existing`.`ID`=`skill`.`ID`);

-- One new push so a client that cached Reaper's "class 16 removed" marker, or
-- that cached the in-range ids before the rows existed, asks for the contents
-- again. 1609394650 is free of every unique id the Hero and Reaper files have
-- used or burnt.
SET @HeroRestorePush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394650 LIMIT 1);
SET @HeroRestorePush := COALESCE(@HeroRestorePush, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroRestorePush,1609394650,`TableHash`,`RecordId`,1 FROM (
             SELECT 4119371148 AS `TableHash`,   16 AS `RecordId`  -- ChrClasses 0xF5889D8C
    UNION ALL SELECT 3224459983, 90                                -- ChrClassesXPowerTypes 0xC0315ACF
    UNION ALL SELECT 3224459983, 91
    UNION ALL SELECT 3224459983, 92
    UNION ALL SELECT 3224459983, 93
    UNION ALL SELECT 3224459983, 94
    UNION ALL SELECT 3224459983, 95
    UNION ALL SELECT 3224459983, 96
    UNION ALL SELECT 3224459983, 97
    UNION ALL SELECT 3224459983, 98
    UNION ALL SELECT 3224459983, 99
    UNION ALL SELECT 2685374048, 1479                              -- ChrSpecialization 0xA00F8E60
    UNION ALL SELECT  812099832, 430                               -- CharBaseInfo 0x3067A8F8
    UNION ALL SELECT  812099832, 431
    UNION ALL SELECT  812099832, 432
    UNION ALL SELECT  812099832, 433
    UNION ALL SELECT  812099832, 434
    UNION ALL SELECT  812099832, 435
    UNION ALL SELECT  812099832, 436
    UNION ALL SELECT  812099832, 437
    UNION ALL SELECT  812099832, 438
    UNION ALL SELECT  812099832, 439
    UNION ALL SELECT  812099832, 440
    UNION ALL SELECT  812099832, 441
    UNION ALL SELECT  812099832, 442
    UNION ALL SELECT  812099832, 443
    UNION ALL SELECT  812099832, 444
    UNION ALL SELECT  812099832, 445
    UNION ALL SELECT  812099832, 446
    UNION ALL SELECT  812099832, 447
    UNION ALL SELECT  812099832, 448
    UNION ALL SELECT  812099832, 449
    UNION ALL SELECT  812099832, 450
    UNION ALL SELECT  812099832, 451
    UNION ALL SELECT  812099832, 452
    UNION ALL SELECT  812099832, 453
    UNION ALL SELECT  812099832, 454
    UNION ALL SELECT  812099832, 455
    UNION ALL SELECT  812099832, 456
    UNION ALL SELECT  812099832, 457
    UNION ALL SELECT  812099832, 458
    UNION ALL SELECT  812099832, 459
    UNION ALL SELECT  812099832, 460
    UNION ALL SELECT 3040725462, 1310                              -- SkillLine 0xB53DC9D6
    UNION ALL SELECT  112059424, 1200                              -- SkillRaceClassInfo 0x06ADE420
    UNION ALL SELECT  112059424, 1201
    UNION ALL SELECT  112059424, 1202
    UNION ALL SELECT  112059424, 1203
    UNION ALL SELECT  112059424, 1204
    UNION ALL SELECT  112059424, 1205
    UNION ALL SELECT  112059424, 1206
    UNION ALL SELECT  112059424, 1207
    UNION ALL SELECT  112059424, 1208
    UNION ALL SELECT  112059424, 1209
    UNION ALL SELECT  112059424, 1210
    UNION ALL SELECT  112059424, 1211
    UNION ALL SELECT  112059424, 1212
    UNION ALL SELECT  112059424, 1213
    UNION ALL SELECT  112059424, 1214
    UNION ALL SELECT  112059424, 1215
    UNION ALL SELECT  112059424, 1216
) AS `push`
WHERE NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId`=1609394650);

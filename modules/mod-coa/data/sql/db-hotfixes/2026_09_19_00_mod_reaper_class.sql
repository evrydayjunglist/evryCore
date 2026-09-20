-- The Reaper class, ported from Conquest of Azeroth.
--
-- This file adds the class itself, the powers it uses, its three specs plus
-- the initial spec the server needs at character creation, the races that may
-- be one, and its class skill line. It adds no starting kit, no level stats
-- and no spell, so nothing can be created as a Reaper yet.
--
-- Class id 16 must already exist in the core, with MAX_CLASSES raised to 17.
-- The core asserts on the class id of these rows while it loads them, so
-- applying this file against a server that does not know class 16 stops it at
-- boot.
--
-- Where the values come from:
--   Colour 10/135/107 is #0A876B, read from CoA's own SharedConstants.lua.
--   Mail armour is CoA's own class description, "Medium Armor (Mail)".
--   The main stat is per spec, as CoA's interface script has it: Harvest
--   agility, Soul intellect, Domination strength. The server reads the spec's
--   value first and falls back to the class row, so all three work.
--   Runic Power is display power 6, the same power Death Knights use.
--   The art is borrowed from Death Knight until Reaper art exists.
--   Every table hash below was read from the WDC5 header of the server's own
--   DB2 extract, not copied from anywhere.
--
-- Every id this file adds starts at 9001, well clear of what the client ships.
-- VerifiedBuild 0 puts these rows in the custom bucket, which the core loads
-- last so they win over anything verified.

-- The class.
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
SELECT 'Reaper','REAPER','Reaper','Reaper','PET',
    'Reapers are masters of the Shadowlands and bringers of death. They are able to turn ethereal and hide in the shadows, waiting for their time to strike. Utilizing the souls of their fallen foes they are able to rip and tear at the very life force of Azeroth in a way never before seen in this world, or the next.',
    'Roles: |cffffffffTank or Melee Damage|r','You must choose a different race to be this class.','Reaper','Reaper',
    0,0,135771,0,
    0,1,791558,41,0,
    0,0,
    0,0,
    0,0,
    0,15000,
    0,9001,16,1,3,6,
    0,1,1,36,
    10,135,107,10,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `chr_classes` WHERE `ID`=16);

-- The powers. Runic Power is the one the class spends; the rest are the four
-- every live class carries, so a Reaper on a quest vehicle or a mount with its
-- own bar has somewhere to put that power.
INSERT INTO `chr_classes_x_power_types` (`ID`,`PowerType`,`ClassID`,`VerifiedBuild`)
SELECT `ID`,`PowerType`,16,0 FROM (
             SELECT 9001 AS `ID`, 6 AS `PowerType`   -- Runic Power
    UNION ALL SELECT 9002, 10                        -- Alternate
    UNION ALL SELECT 9003, 23                        -- Alternate, quest
    UNION ALL SELECT 9004, 24                        -- Alternate, encounter
    UNION ALL SELECT 9005, 25                        -- Alternate, mount
) AS `power`
WHERE NOT EXISTS (SELECT 1 FROM `chr_classes_x_power_types` WHERE `ClassID`=16);

-- The specs. Role 0 is tank and 2 is damage. Flags 4 is melee and 64 is the
-- one the client marks as recommended. PrimaryStatPriority 5 reads as
-- strength, 2 or 3 as agility, and anything lower as intellect.
-- The spec at OrderIndex 4 is the initial one. It is not optional: creating a
-- character asks for it by that index, and resetting a specialisation asserts
-- on it, so a class without one crashes character creation.
INSERT INTO `chr_specialization` (`Name`,`FemaleName`,`Description`,`ID`,`ClassID`,`OrderIndex`,
    `PetTalentType`,`Role`,`Flags`,`SpellIconFileID`,`PrimaryStatPriority`,`AnimReplacements`,
    `MasterySpellID1`,`MasterySpellID2`,`VerifiedBuild`)
SELECT `Name`,'',`Description`,`ID`,16,`OrderIndex`,0,`Role`,`Flags`,`SpellIconFileID`,`PrimaryStatPriority`,0,0,0,0 FROM (
             SELECT 'Harvest' AS `Name`, 9001 AS `ID`, 0 AS `OrderIndex`, 2 AS `Role`, 4 AS `Flags`, 135773 AS `SpellIconFileID`, 3 AS `PrimaryStatPriority`,
                    'Steal the life force of enemies and spend it to strike foes with powerful two-handed attacks.' AS `Description`
    UNION ALL SELECT 'Soul',       9002, 1, 2,  4, 135775, 0,
                    'Infuse your weapons to assassinate key targets from stealth with dual weapons and banish them to the Shadowlands.'
    UNION ALL SELECT 'Domination', 9003, 2, 0,  4, 135770, 5,
                    'Fortify yourself with the souls of your enemies to deflect powerful attacks and absorb their life force.'
    UNION ALL SELECT 'Initial',    9004, 4, 2, 68, 135771, 3, ''
) AS `spec`
WHERE NOT EXISTS (SELECT 1 FROM `chr_specialization` WHERE `ClassID`=16);

-- The races that may be a Reaper: the five CoA ships officially. The other
-- faction race is the one every existing row for that race already names.
INSERT INTO `char_base_info` (`ID`,`RaceID`,`ClassID`,`OtherFactionRaceID`,`VerifiedBuild`)
SELECT `ID`,`RaceID`,16,`OtherFactionRaceID`,0 FROM (
             SELECT 9001 AS `ID`,  1 AS `RaceID`, 5 AS `OtherFactionRaceID`  -- Human
    UNION ALL SELECT 9002,  5, 1                                             -- Undead
    UNION ALL SELECT 9003,  8, 4                                             -- Troll
    UNION ALL SELECT 9004, 10, 1                                             -- Blood Elf
    UNION ALL SELECT 9005, 11, 6                                             -- Draenei
) AS `pair`
WHERE NOT EXISTS (SELECT 1 FROM `char_base_info` WHERE `ClassID`=16);

-- The class skill line. Category 7 is the class category, and flags 1048 is
-- what every other class skill line carries.
INSERT INTO `skill_line` (`DisplayName`,`AlternateVerb`,`Description`,`HordeDisplayName`,
    `OverrideSourceInfoDisplayName`,`ID`,`CategoryID`,`SpellIconFileID`,`CanLink`,
    `ParentSkillLineID`,`ParentTierIndex`,`Flags`,`SpellBookSpellID`,
    `ExpansionNameSharedStringID`,`HordeExpansionNameSharedStringID`,`VerifiedBuild`)
SELECT 'Reaper','','Reaper skills.','','',9001,7,135771,0,0,0,1048,0,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `skill_line` WHERE `ID`=9001);

-- That skill line belongs to class 16. Class mask 32768 is bit 16. The race
-- masks are -1, meaning every race, the same as every other class skill line;
-- which races may actually be a Reaper is decided by char_base_info above.
INSERT INTO `skill_race_class_info` (`ID`,`SkillID`,`ClassMask`,`Flags`,`Availability`,
    `MinLevel`,`SkillTierID`,`RaceMask1`,`RaceMask2`,`VerifiedBuild`)
SELECT 9001,9001,32768,1048,1,0,0,-1,-1,0
WHERE NOT EXISTS (SELECT 1 FROM `skill_race_class_info` WHERE `ID`=9001);

-- One push telling the client about every row above. All of them share a
-- single push id, which is what the core expects: it groups hotfix_data rows
-- by that id. Status 1 is valid. The unique id is fixed rather than random so
-- that re-running this file cannot quietly change it.
SET @ReaperHotfixId := (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`);
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @ReaperHotfixId,1609394509,`TableHash`,`RecordId`,1 FROM (
             SELECT 4119371148 AS `TableHash`,   16 AS `RecordId`  -- ChrClasses 0xF5889D8C
    UNION ALL SELECT 3224459983, 9001                              -- ChrClassesXPowerTypes 0xC0315ACF
    UNION ALL SELECT 3224459983, 9002
    UNION ALL SELECT 3224459983, 9003
    UNION ALL SELECT 3224459983, 9004
    UNION ALL SELECT 3224459983, 9005
    UNION ALL SELECT 2685374048, 9001                              -- ChrSpecialization 0xA00F8E60
    UNION ALL SELECT 2685374048, 9002
    UNION ALL SELECT 2685374048, 9003
    UNION ALL SELECT 2685374048, 9004
    UNION ALL SELECT  812099832, 9001                              -- CharBaseInfo 0x3067A8F8
    UNION ALL SELECT  812099832, 9002
    UNION ALL SELECT  812099832, 9003
    UNION ALL SELECT  812099832, 9004
    UNION ALL SELECT  812099832, 9005
    UNION ALL SELECT 3040725462, 9001                              -- SkillLine 0xB53DC9D6
    UNION ALL SELECT  112059424, 9001                              -- SkillRaceClassInfo 0x06ADE420
) AS `push`
WHERE NOT EXISTS (
    SELECT 1 FROM `hotfix_data` `existing`
    WHERE `existing`.`TableHash`=`push`.`TableHash` AND `existing`.`RecordId`=`push`.`RecordId`);

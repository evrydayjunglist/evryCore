-- The Hero class, ported from Conquest of Azeroth as class 16.
--
-- Hero is a container for a character with no class. It gets no kit here: no
-- spells, no starting abilities, no talent tree. What a Hero can do is meant to
-- come later, from Free Pick and Wildcard. This file only makes the class
-- exist, so that one can be created and logged in.
--
-- Class id 16 must already exist in the core, with MAX_CLASSES at 18. The core
-- asserts on the class id of these rows while it loads them, so applying this
-- file against a server that does not know class 16 stops it at boot.
--
-- Every id this file adds starts at 9101. Reaper's rows use the 9001 block, so
-- the two classes never collide. VerifiedBuild 0 puts these rows in the custom
-- bucket, which the core loads last so they win over anything verified.
--
-- Where the values come from. Everything marked as read was read, not recalled:
--   Display power 0 (Mana), flags 2 and the filename HERO are Conquest of
--   Azeroth's own ChrClasses row for Hero, read from its decoded client.
--   The colour #FFD624 is CreateColor(1, 0.84, 0.14) from that client's
--   SharedConstants.lua, which is 255, 214, 36.
--   The ten races are exactly the ten that client's CharBaseInfo gives Hero:
--   1, 2, 3, 4, 5, 6, 7, 8, 10 and 11. Goblin is not among them.
--   Every table hash below is the one Reaper's file already uses, which was
--   read from the WDC5 header of this server's own DB2 extract.

-- The class.
--
-- ArmorTypeMask 127 is bits 0 to 6: miscellaneous, cloth, leather, mail, plate,
-- cosmetic and shield. A Hero can wear anything, which is the owner's decision
-- and follows from Hero having no fixed role. No class the client ships has all
-- seven, though Traveler already carries 63, every weight without the shield,
-- so most of this shape is not new to the client. The bits above 6 are the old
-- relic families (libram, idol, totem, sigil, relic) and are deliberately left
-- off, because they go with HasRelicSlot, which Hero does not have.
--
-- SpellClassSet 37 is a free family. Conquest of Azeroth gives Hero 7, which is
-- Druid on retail, so keeping it would put every Hero spell in Druid's family.
-- All 26406 rows of the client's own SpellClassOptions were read: the families
-- in use are 0, 1, 3 to 13, 15, 17, 50, 53, 57, 66, 71, 78, 91, 100, 107, 110,
-- 224 and 227. Neither 36 nor 37 appears, and Reaper already took 36, so Hero
-- takes 37 and our two custom families sit together. Traveler is the precedent
-- for a class carrying a family no spell uses.
--
-- PrimaryStatPriority 0 reads as intellect, which is what goes with a Mana bar.
-- A Hero who buys Warrior abilities will want strength instead; there is no
-- value meaning "all", and no spec to vary it per build, so this is the honest
-- pick for the bar the class actually shows and will need revisiting when Free
-- Pick exists. HasStrengthBonus 0 follows Conquest of Azeroth's own row.
--
-- RolesMask 14 is tank, healer and damage together, the same as Paladin, Druid
-- and Monk. A Hero can be built into any of them.
--
-- The art is borrowed, the same way Reaper's is, until Hero art exists. Expect
-- a blank class icon on the creation screen: that button asks for an atlas
-- named after the class, and retail ships none for Hero.
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
    0,9101,16,0,0,0,
    0,1,1,37,
    255,214,36,14,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `chr_classes` WHERE `ID`=16);

-- The powers a Hero may spend.
--
-- A class may hold at most ten, and that limit is not enforced kindly: the core
-- loads these rows into a fixed ten entry array and increments its own index
-- without a bounds check, so an eleventh row for one class writes past the end
-- of it while the server is starting. Do not add one.
--
-- The owner's decision is that a Hero can use every resource. Seventeen real
-- resources exist across the fifteen classes the client ships, so all of them
-- will not fit. Six of the ten go to real resources and four to the alternate
-- bars, which were read out of the client's own ChrClassesXPowerTypes: all
-- thirteen live classes carry the same four, and they are where quest vehicles,
-- encounters and mounts put their power. Dropping them would make Hero the only
-- class in the game with nowhere to put it.
--
-- Of the six, Combo Points is there because Energy abilities are useless
-- without it, and Soul Shards because a Warlock's shard spenders are paid for
-- with Mana, which a Hero already has, so one slot buys a whole working kit.
-- Runic Power was considered and rejected for the opposite reason: without
-- Runes, which will not fit, a Hero could spend a Death Knight's resource but
-- never build it.
INSERT INTO `chr_classes_x_power_types` (`ID`,`PowerType`,`ClassID`,`VerifiedBuild`)
SELECT `ID`,`PowerType`,16,0 FROM (
             SELECT 9101 AS `ID`, 0 AS `PowerType`   -- Mana, and the bar Hero displays
    UNION ALL SELECT 9102, 1                         -- Rage
    UNION ALL SELECT 9103, 2                         -- Focus
    UNION ALL SELECT 9104, 3                         -- Energy
    UNION ALL SELECT 9105, 4                         -- Combo Points
    UNION ALL SELECT 9106, 7                         -- Soul Shards
    UNION ALL SELECT 9107, 10                        -- Alternate
    UNION ALL SELECT 9108, 23                        -- Alternate, quest
    UNION ALL SELECT 9109, 24                        -- Alternate, encounter
    UNION ALL SELECT 9110, 25                        -- Alternate, mount
) AS `power`
WHERE NOT EXISTS (SELECT 1 FROM `chr_classes_x_power_types` WHERE `ClassID`=16);

-- The one specialisation, and it is hidden.
--
-- Hero has no specialisations on Conquest of Azeroth and the owner's decision
-- is to keep it that way, so this is the only one. It is not optional: creating
-- a character asks for the spec at OrderIndex 4 by that index, and resetting a
-- specialisation asserts on it, so a class without one crashes creation.
--
-- Flags 64 is the bit the client marks as recommended. Reaper's initial spec
-- also carries 4, which means melee; Hero drops it because a Hero is not melee
-- by default. If character creation misbehaves in a way that points at this
-- row, that bit is the first thing to put back.
INSERT INTO `chr_specialization` (`Name`,`FemaleName`,`Description`,`ID`,`ClassID`,`OrderIndex`,
    `PetTalentType`,`Role`,`Flags`,`SpellIconFileID`,`PrimaryStatPriority`,`AnimReplacements`,
    `MasterySpellID1`,`MasterySpellID2`,`VerifiedBuild`)
SELECT 'Initial','','',9101,16,4,0,2,64,135771,0,0,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `chr_specialization` WHERE `ClassID`=16);

-- The ten races that may be a Hero, exactly the ten Conquest of Azeroth gives
-- it. OtherFactionRaceID only matters for a faction change, and every value
-- here is one the client's own CharBaseInfo already names for that race; the
-- five Reaper also uses keep Reaper's choice, and the other five are the
-- opposite faction's counterpart.
INSERT INTO `char_base_info` (`ID`,`RaceID`,`ClassID`,`OtherFactionRaceID`,`VerifiedBuild`)
SELECT `ID`,`RaceID`,16,`OtherFactionRaceID`,0 FROM (
             SELECT 9101 AS `ID`,  1 AS `RaceID`,  5 AS `OtherFactionRaceID`  -- Human
    UNION ALL SELECT 9102,  2,  1                                             -- Orc
    UNION ALL SELECT 9103,  3,  2                                             -- Dwarf
    UNION ALL SELECT 9104,  4,  8                                             -- Night Elf
    UNION ALL SELECT 9105,  5,  1                                             -- Undead
    UNION ALL SELECT 9106,  6, 11                                             -- Tauren
    UNION ALL SELECT 9107,  7,  9                                             -- Gnome
    UNION ALL SELECT 9108,  8,  4                                             -- Troll
    UNION ALL SELECT 9109, 10,  1                                             -- Blood Elf
    UNION ALL SELECT 9110, 11,  6                                             -- Draenei
) AS `pair`
WHERE NOT EXISTS (SELECT 1 FROM `char_base_info` WHERE `ClassID`=16);

-- The class skill line. Category 7 is the class category, and flags 1048 is
-- what every other class skill line carries.
INSERT INTO `skill_line` (`DisplayName`,`AlternateVerb`,`Description`,`HordeDisplayName`,
    `OverrideSourceInfoDisplayName`,`ID`,`CategoryID`,`SpellIconFileID`,`CanLink`,
    `ParentSkillLineID`,`ParentTierIndex`,`Flags`,`SpellBookSpellID`,
    `ExpansionNameSharedStringID`,`HordeExpansionNameSharedStringID`,`VerifiedBuild`)
SELECT 'Hero','','Hero skills.','','',9101,7,135771,0,0,0,1048,0,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `skill_line` WHERE `ID`=9101);

-- That skill line belongs to class 16. Class mask 32768 is bit 16, which is the
-- mask Reaper used to carry and gave up when it moved to 17. The race masks are
-- -1, meaning every race, the same as every other class skill line; which races
-- may actually be a Hero is decided by char_base_info above.
INSERT INTO `skill_race_class_info` (`ID`,`SkillID`,`ClassMask`,`Flags`,`Availability`,
    `MinLevel`,`SkillTierID`,`RaceMask1`,`RaceMask2`,`VerifiedBuild`)
SELECT 9101,9101,32768,1048,1,0,0,-1,-1,0
WHERE NOT EXISTS (SELECT 1 FROM `skill_race_class_info` WHERE `ID`=9101);

-- One push telling the client about every row above. All of them share a single
-- push id, which is what the core expects: it groups hotfix_data rows by that
-- id. Status 1 is valid. The unique id is fixed rather than random so that
-- re-running this file cannot quietly change it, and it is well clear of the
-- 1609394509 and 1609394510 that Reaper's two files use.
--
-- This push must have a higher id than the one Reaper's renumber left behind,
-- and taking the next free id gives it one. That renumber marked ChrClasses
-- record 16 removed, to stop a client that had cached a class 16 Reaper from
-- drawing a second one. The core reads these rows in push id order and the last
-- word about a record wins, so Hero's valid record 16 below supersedes that
-- marker. Put Hero's push before it and the server would erase Hero's own class
-- row while it starts.
--
-- The guard below is scoped to this push on purpose. A guard that only asked
-- whether any push anywhere already mentions ChrClasses record 16 would find
-- Reaper's removal marker and quietly skip Hero's most important row.
SET @HeroHotfixId := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394600 LIMIT 1);
SET @HeroHotfixId := COALESCE(@HeroHotfixId, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroHotfixId,1609394600,`TableHash`,`RecordId`,1 FROM (
             SELECT 4119371148 AS `TableHash`,   16 AS `RecordId`  -- ChrClasses 0xF5889D8C
    UNION ALL SELECT 3224459983, 9101                              -- ChrClassesXPowerTypes 0xC0315ACF
    UNION ALL SELECT 3224459983, 9102
    UNION ALL SELECT 3224459983, 9103
    UNION ALL SELECT 3224459983, 9104
    UNION ALL SELECT 3224459983, 9105
    UNION ALL SELECT 3224459983, 9106
    UNION ALL SELECT 3224459983, 9107
    UNION ALL SELECT 3224459983, 9108
    UNION ALL SELECT 3224459983, 9109
    UNION ALL SELECT 3224459983, 9110
    UNION ALL SELECT 2685374048, 9101                              -- ChrSpecialization 0xA00F8E60
    UNION ALL SELECT  812099832, 9101                              -- CharBaseInfo 0x3067A8F8
    UNION ALL SELECT  812099832, 9102
    UNION ALL SELECT  812099832, 9103
    UNION ALL SELECT  812099832, 9104
    UNION ALL SELECT  812099832, 9105
    UNION ALL SELECT  812099832, 9106
    UNION ALL SELECT  812099832, 9107
    UNION ALL SELECT  812099832, 9108
    UNION ALL SELECT  812099832, 9109
    UNION ALL SELECT  812099832, 9110
    UNION ALL SELECT 3040725462, 9101                              -- SkillLine 0xB53DC9D6
    UNION ALL SELECT  112059424, 9101                              -- SkillRaceClassInfo 0x06ADE420
) AS `push`
WHERE NOT EXISTS (
    SELECT 1 FROM `hotfix_data` `existing`
    WHERE `existing`.`Id`=@HeroHotfixId
      AND `existing`.`TableHash`=`push`.`TableHash` AND `existing`.`RecordId`=`push`.`RecordId`);

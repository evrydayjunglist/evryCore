-- Move Hero's remaining custom row ids inside the range the client ships.
--
-- Why this exists. On 20 September a Hero's specialisation id, 9101, was found
-- to break the client: every spell the character knew drew in the spellbook as
-- spell 205523, Brewmaster Monk's Blackout Kick, and the client refused to cast
-- it for not holding an axe. The server was right throughout. Moving that one
-- id to 1479, inside the 62 to 1480 range this client's own specialisations
-- occupy, fixed it outright with nothing patched on the client. So a custom row
-- whose id sits far above the client's own highest id for that table can break
-- a client-side lookup that is sized by the real range.
--
-- Nothing is currently misbehaving because of the four tables below. Races,
-- power types, weapon proficiencies and the class skill line all work in play
-- today. This file is hardening against a failure mode that has been proved
-- once, not fixing a live break.
--
-- Where the new ids come from. The client's own DB2 files were read for each
-- table, the free runs below its highest id were listed, and the largest run
-- was taken. Ids just under a ceiling are the wrong place to sit: Blizzard
-- allocates upward from the current maximum, so that is exactly where the next
-- expansion's rows land. A long-standing gap in the middle of the range is
-- safer, and each run below is one.
--
--   ChrClassesXPowerTypes  highest shipped id  262   largest free run    89..194
--   CharBaseInfo           highest shipped id  897   largest free run  424..545
--   SkillLine              highest shipped id 3004   largest free run 1306..1747
--   SkillRaceClassInfo     highest shipped id 2548   largest free run 1198..1644
--
-- Hero takes the first block in each run and Reaper takes the next, packed with
-- no gap, so the ids stay in class order and the next Conquest of Azeroth class
-- can simply carry on. Reaper's half of this move is
-- 2026_09_20_08_mod_reaper_ids_in_range.sql in mod-coa. Every id below was
-- checked free against both the client's DB2 and the live hotfix tables.
--
--   chr_classes_x_power_types   9101..9110  ->   90..99    next class starts at 105
--   char_base_info              9101..9131  ->  430..460   next class starts at 466
--   skill_line                        9101  ->  1310       next class starts at 1312
--   skill_race_class_info       9101..9117  -> 1200..1216  next class starts at 1218
--
-- Hero's own specialisation stays at 1479 for now. That id works but sits in
-- the growth zone just under the ceiling, and it should move to around 600 the
-- next time Hero's specialisations are rewritten for Free Pick. It is not worth
-- a file of its own.
--
-- This is a new file rather than an edit to the Hero files already applied,
-- because the updater keys on filename and every one of those has run.
--
-- The rows are copied from the ones they replace rather than retyped, so only
-- the id changes and nothing can be dropped by hand. Each id map is spelled out
-- rather than calculated, so a row holding something unexpected is left alone
-- instead of being moved somewhere wrong.
--
-- Each move reads a count into a variable first, for two reasons. Asking the
-- question inside the INSERT would mean naming the table being inserted into,
-- which MySQL does not always allow; and reading it before the DELETE lets the
-- delete refuse to run unless every replacement row is actually there, so a
-- half-applied file cannot leave Hero with nothing.

-- The ten power types a Hero may spend. Only the row id changes; the power
-- values, and the order the core sorts them into, are untouched.
SET @HeroPowersAlreadyMoved := (SELECT COUNT(*) FROM `chr_classes_x_power_types` WHERE `ID`=90);

INSERT INTO `chr_classes_x_power_types` (`ID`,`PowerType`,`ClassID`,`VerifiedBuild`)
SELECT `map`.`NewID`,`old`.`PowerType`,16,0
FROM `chr_classes_x_power_types` `old`
JOIN (
             SELECT 9101 AS `OldID`, 90 AS `NewID`   -- Mana, and the bar Hero displays
    UNION ALL SELECT 9102, 91                        -- Rage
    UNION ALL SELECT 9103, 92                        -- Focus
    UNION ALL SELECT 9104, 93                        -- Energy
    UNION ALL SELECT 9105, 94                        -- Combo Points
    UNION ALL SELECT 9106, 95                        -- Soul Shards
    UNION ALL SELECT 9107, 96                        -- Alternate
    UNION ALL SELECT 9108, 97                        -- Alternate, quest
    UNION ALL SELECT 9109, 98                        -- Alternate, encounter
    UNION ALL SELECT 9110, 99                        -- Alternate, mount
) AS `map` ON `map`.`OldID` = `old`.`ID`
WHERE `old`.`ClassID` = 16 AND @HeroPowersAlreadyMoved = 0;

SET @HeroPowersMoved := (SELECT COUNT(*) FROM `chr_classes_x_power_types` WHERE `ID` BETWEEN 90 AND 99 AND `ClassID`=16);
DELETE FROM `chr_classes_x_power_types`
    WHERE `ID` BETWEEN 9101 AND 9110 AND `ClassID`=16 AND @HeroPowersMoved = 10;

-- The thirty-one races that may be a Hero. The race each row names and the
-- other-faction race it pairs with come across unchanged; only the row id
-- moves. Nothing else in any database points at a char_base_info id, which was
-- checked against every column in the characters, world and hotfixes schemas
-- before this was written, and the core only ever looks this table up by race
-- and class.
SET @HeroRacesAlreadyMoved := (SELECT COUNT(*) FROM `char_base_info` WHERE `ID`=430);

INSERT INTO `char_base_info` (`ID`,`RaceID`,`ClassID`,`OtherFactionRaceID`,`VerifiedBuild`)
SELECT `map`.`NewID`,`old`.`RaceID`,16,`old`.`OtherFactionRaceID`,0
FROM `char_base_info` `old`
JOIN (
             SELECT 9101 AS `OldID`, 430 AS `NewID`  -- Human
    UNION ALL SELECT 9102, 431                       -- Orc
    UNION ALL SELECT 9103, 432                       -- Dwarf
    UNION ALL SELECT 9104, 433                       -- Night Elf
    UNION ALL SELECT 9105, 434                       -- Undead
    UNION ALL SELECT 9106, 435                       -- Tauren
    UNION ALL SELECT 9107, 436                       -- Gnome
    UNION ALL SELECT 9108, 437                       -- Troll
    UNION ALL SELECT 9109, 438                       -- Blood Elf
    UNION ALL SELECT 9110, 439                       -- Draenei
    UNION ALL SELECT 9111, 440                       -- Goblin
    UNION ALL SELECT 9112, 441                       -- Worgen
    UNION ALL SELECT 9113, 442                       -- Pandaren, neutral
    UNION ALL SELECT 9114, 443                       -- Pandaren, Alliance
    UNION ALL SELECT 9115, 444                       -- Pandaren, Horde
    UNION ALL SELECT 9116, 445                       -- Nightborne
    UNION ALL SELECT 9117, 446                       -- Highmountain Tauren
    UNION ALL SELECT 9118, 447                       -- Void Elf
    UNION ALL SELECT 9119, 448                       -- Lightforged Draenei
    UNION ALL SELECT 9120, 449                       -- Zandalari Troll
    UNION ALL SELECT 9121, 450                       -- Kul Tiran
    UNION ALL SELECT 9122, 451                       -- Dark Iron Dwarf
    UNION ALL SELECT 9123, 452                       -- Vulpera
    UNION ALL SELECT 9124, 453                       -- Mag'har Orc
    UNION ALL SELECT 9125, 454                       -- Mechagnome
    UNION ALL SELECT 9126, 455                       -- Dracthyr, Alliance
    UNION ALL SELECT 9127, 456                       -- Dracthyr, Horde
    UNION ALL SELECT 9128, 457                       -- Earthen, Alliance
    UNION ALL SELECT 9129, 458                       -- Earthen, Horde
    UNION ALL SELECT 9130, 459                       -- Haranir, Alliance
    UNION ALL SELECT 9131, 460                       -- Haranir, Horde
) AS `map` ON `map`.`OldID` = `old`.`ID`
WHERE `old`.`ClassID` = 16 AND @HeroRacesAlreadyMoved = 0;

SET @HeroRacesMoved := (SELECT COUNT(*) FROM `char_base_info` WHERE `ID` BETWEEN 430 AND 460 AND `ClassID`=16);
DELETE FROM `char_base_info`
    WHERE `ID` BETWEEN 9101 AND 9131 AND `ClassID`=16 AND @HeroRacesMoved = 31;

-- The class skill line. This is the dangerous one, because its id is a real
-- skill id that other rows point at: the skill_race_class_info row below names
-- it, and four Heroes already have it stored in characters.character_skills.
-- A skill_race_class_info row whose SkillID names no skill_line row is dropped
-- on the floor while the core loads (DB2Stores.cpp, the LookupEntry guard when
-- filling _skillRaceClassInfoBySkill), so the SkillID has to move in the same
-- file. The stored character rows move in
-- 2026_09_20_09_mod_hero_ids_in_range_characters.sql, which has to be applied
-- with this one.
SET @HeroSkillLineAlreadyMoved := (SELECT COUNT(*) FROM `skill_line` WHERE `ID`=1310);

INSERT INTO `skill_line` (`DisplayName`,`AlternateVerb`,`Description`,`HordeDisplayName`,
    `OverrideSourceInfoDisplayName`,`ID`,`CategoryID`,`SpellIconFileID`,`CanLink`,
    `ParentSkillLineID`,`ParentTierIndex`,`Flags`,`SpellBookSpellID`,
    `ExpansionNameSharedStringID`,`HordeExpansionNameSharedStringID`,`VerifiedBuild`)
SELECT `DisplayName`,`AlternateVerb`,`Description`,`HordeDisplayName`,
    `OverrideSourceInfoDisplayName`,1310,`CategoryID`,`SpellIconFileID`,`CanLink`,
    `ParentSkillLineID`,`ParentTierIndex`,`Flags`,`SpellBookSpellID`,
    `ExpansionNameSharedStringID`,`HordeExpansionNameSharedStringID`,0
FROM `skill_line` WHERE `ID`=9101 AND @HeroSkillLineAlreadyMoved = 0;

SET @HeroSkillLineMoved := (SELECT COUNT(*) FROM `skill_line` WHERE `ID`=1310);
DELETE FROM `skill_line` WHERE `ID`=9101 AND @HeroSkillLineMoved = 1;

-- The class skill line's own row and Hero's sixteen weapon proficiencies. The
-- first row is the one whose SkillID has to follow the skill line above; the
-- other sixteen name real Blizzard skills whose ids do not change, so the map
-- carries the new SkillID explicitly rather than working it out.
--
-- Lowering these ids cannot change which row the core picks. It walks this
-- table in row id order and keeps the first row that matches the race and the
-- class, and no row the client ships matches class 16 at all: all eighteen
-- skills were read out of the client's own SkillRaceClassInfo and not one
-- shipped row carries class mask 0, -1, or bit 16.
SET @HeroSkillsAlreadyMoved := (SELECT COUNT(*) FROM `skill_race_class_info` WHERE `ID`=1200);

INSERT INTO `skill_race_class_info` (`ID`,`SkillID`,`ClassMask`,`Flags`,`Availability`,
    `MinLevel`,`SkillTierID`,`RaceMask1`,`RaceMask2`,`VerifiedBuild`)
SELECT `map`.`NewID`,`map`.`NewSkillID`,`old`.`ClassMask`,`old`.`Flags`,`old`.`Availability`,
    `old`.`MinLevel`,`old`.`SkillTierID`,`old`.`RaceMask1`,`old`.`RaceMask2`,0
FROM `skill_race_class_info` `old`
JOIN (
             SELECT 9101 AS `OldID`, 1200 AS `NewID`, 1310 AS `NewSkillID`  -- the Hero class skill line itself
    UNION ALL SELECT 9102, 1201,   43                                       -- Swords
    UNION ALL SELECT 9103, 1202,   44                                       -- Axes
    UNION ALL SELECT 9104, 1203,   45                                       -- Bows
    UNION ALL SELECT 9105, 1204,   46                                       -- Guns
    UNION ALL SELECT 9106, 1205,   54                                       -- Maces
    UNION ALL SELECT 9107, 1206,   55                                       -- Two-Handed Swords
    UNION ALL SELECT 9108, 1207,  118                                       -- Dual Wield
    UNION ALL SELECT 9109, 1208,  136                                       -- Staves
    UNION ALL SELECT 9110, 1209,  160                                       -- Two-Handed Maces
    UNION ALL SELECT 9111, 1210,  172                                       -- Two-Handed Axes
    UNION ALL SELECT 9112, 1211,  173                                       -- Daggers
    UNION ALL SELECT 9113, 1212,  226                                       -- Crossbows
    UNION ALL SELECT 9114, 1213,  228                                       -- Wands
    UNION ALL SELECT 9115, 1214,  229                                       -- Polearms
    UNION ALL SELECT 9116, 1215,  473                                       -- Fist Weapons
    UNION ALL SELECT 9117, 1216, 2152                                       -- Warglaives
) AS `map` ON `map`.`OldID` = `old`.`ID`
WHERE `old`.`ClassMask` = 32768 AND @HeroSkillsAlreadyMoved = 0;

SET @HeroSkillsMoved := (SELECT COUNT(*) FROM `skill_race_class_info` WHERE `ID` BETWEEN 1200 AND 1216);
DELETE FROM `skill_race_class_info`
    WHERE `ID` BETWEEN 9101 AND 9117 AND `ClassMask`=32768 AND @HeroSkillsMoved = 17;

-- One push carrying all of it: fifty-nine rows exist at their new ids and
-- fifty-nine are gone from their old ones. Status 1 is valid and status 2 is
-- RecordRemoved, which is also what makes the server drop the old record from
-- its own store while it starts.
--
-- The push id must be higher than every push before it, because the core reads
-- them in id order and the last word about a record wins; the old ids were
-- declared valid in pushes 111749 to 111752 and this has to overrule that.
-- Taking the next free id gives that, which is 111755 as this is written.
--
-- The unique id is fixed rather than random so re-running this file cannot
-- quietly change it. 1609394630 was checked against the live table and is free.
-- 1609394509, 1609394510, 1609394600, 1609394601, 1609394602, 1609394604,
-- 1609394610 and 1609394620 belong to the Hero and Reaper files already
-- applied, and reusing one would make the guard below skip this whole push in
-- silence. Without a new unique id a client that has already played a Hero
-- keeps every row it cached, including all of the old ids.
SET @HeroRangePush := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394630 LIMIT 1);
SET @HeroRangePush := COALESCE(@HeroRangePush, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroRangePush,1609394630,`TableHash`,`RecordId`,`Status` FROM (
    -- ChrClassesXPowerTypes 0xC0315ACF, the new rows then the old ones
              SELECT 3224459983 AS `TableHash`, 90 AS `RecordId`, 1 AS `Status`
    UNION ALL SELECT 3224459983,   91, 1 UNION ALL SELECT 3224459983,   92, 1
    UNION ALL SELECT 3224459983,   93, 1 UNION ALL SELECT 3224459983,   94, 1
    UNION ALL SELECT 3224459983,   95, 1 UNION ALL SELECT 3224459983,   96, 1
    UNION ALL SELECT 3224459983,   97, 1 UNION ALL SELECT 3224459983,   98, 1
    UNION ALL SELECT 3224459983,   99, 1
    UNION ALL SELECT 3224459983, 9101, 2 UNION ALL SELECT 3224459983, 9102, 2
    UNION ALL SELECT 3224459983, 9103, 2 UNION ALL SELECT 3224459983, 9104, 2
    UNION ALL SELECT 3224459983, 9105, 2 UNION ALL SELECT 3224459983, 9106, 2
    UNION ALL SELECT 3224459983, 9107, 2 UNION ALL SELECT 3224459983, 9108, 2
    UNION ALL SELECT 3224459983, 9109, 2 UNION ALL SELECT 3224459983, 9110, 2
    -- CharBaseInfo 0x3067A8F8
    UNION ALL SELECT  812099832,  430, 1 UNION ALL SELECT  812099832,  431, 1
    UNION ALL SELECT  812099832,  432, 1 UNION ALL SELECT  812099832,  433, 1
    UNION ALL SELECT  812099832,  434, 1 UNION ALL SELECT  812099832,  435, 1
    UNION ALL SELECT  812099832,  436, 1 UNION ALL SELECT  812099832,  437, 1
    UNION ALL SELECT  812099832,  438, 1 UNION ALL SELECT  812099832,  439, 1
    UNION ALL SELECT  812099832,  440, 1 UNION ALL SELECT  812099832,  441, 1
    UNION ALL SELECT  812099832,  442, 1 UNION ALL SELECT  812099832,  443, 1
    UNION ALL SELECT  812099832,  444, 1 UNION ALL SELECT  812099832,  445, 1
    UNION ALL SELECT  812099832,  446, 1 UNION ALL SELECT  812099832,  447, 1
    UNION ALL SELECT  812099832,  448, 1 UNION ALL SELECT  812099832,  449, 1
    UNION ALL SELECT  812099832,  450, 1 UNION ALL SELECT  812099832,  451, 1
    UNION ALL SELECT  812099832,  452, 1 UNION ALL SELECT  812099832,  453, 1
    UNION ALL SELECT  812099832,  454, 1 UNION ALL SELECT  812099832,  455, 1
    UNION ALL SELECT  812099832,  456, 1 UNION ALL SELECT  812099832,  457, 1
    UNION ALL SELECT  812099832,  458, 1 UNION ALL SELECT  812099832,  459, 1
    UNION ALL SELECT  812099832,  460, 1
    UNION ALL SELECT  812099832, 9101, 2 UNION ALL SELECT  812099832, 9102, 2
    UNION ALL SELECT  812099832, 9103, 2 UNION ALL SELECT  812099832, 9104, 2
    UNION ALL SELECT  812099832, 9105, 2 UNION ALL SELECT  812099832, 9106, 2
    UNION ALL SELECT  812099832, 9107, 2 UNION ALL SELECT  812099832, 9108, 2
    UNION ALL SELECT  812099832, 9109, 2 UNION ALL SELECT  812099832, 9110, 2
    UNION ALL SELECT  812099832, 9111, 2 UNION ALL SELECT  812099832, 9112, 2
    UNION ALL SELECT  812099832, 9113, 2 UNION ALL SELECT  812099832, 9114, 2
    UNION ALL SELECT  812099832, 9115, 2 UNION ALL SELECT  812099832, 9116, 2
    UNION ALL SELECT  812099832, 9117, 2 UNION ALL SELECT  812099832, 9118, 2
    UNION ALL SELECT  812099832, 9119, 2 UNION ALL SELECT  812099832, 9120, 2
    UNION ALL SELECT  812099832, 9121, 2 UNION ALL SELECT  812099832, 9122, 2
    UNION ALL SELECT  812099832, 9123, 2 UNION ALL SELECT  812099832, 9124, 2
    UNION ALL SELECT  812099832, 9125, 2 UNION ALL SELECT  812099832, 9126, 2
    UNION ALL SELECT  812099832, 9127, 2 UNION ALL SELECT  812099832, 9128, 2
    UNION ALL SELECT  812099832, 9129, 2 UNION ALL SELECT  812099832, 9130, 2
    UNION ALL SELECT  812099832, 9131, 2
    -- SkillLine 0xB53DC9D6
    UNION ALL SELECT 3040725462, 1310, 1
    UNION ALL SELECT 3040725462, 9101, 2
    -- SkillRaceClassInfo 0x06ADE420
    UNION ALL SELECT  112059424, 1200, 1 UNION ALL SELECT  112059424, 1201, 1
    UNION ALL SELECT  112059424, 1202, 1 UNION ALL SELECT  112059424, 1203, 1
    UNION ALL SELECT  112059424, 1204, 1 UNION ALL SELECT  112059424, 1205, 1
    UNION ALL SELECT  112059424, 1206, 1 UNION ALL SELECT  112059424, 1207, 1
    UNION ALL SELECT  112059424, 1208, 1 UNION ALL SELECT  112059424, 1209, 1
    UNION ALL SELECT  112059424, 1210, 1 UNION ALL SELECT  112059424, 1211, 1
    UNION ALL SELECT  112059424, 1212, 1 UNION ALL SELECT  112059424, 1213, 1
    UNION ALL SELECT  112059424, 1214, 1 UNION ALL SELECT  112059424, 1215, 1
    UNION ALL SELECT  112059424, 1216, 1
    UNION ALL SELECT  112059424, 9101, 2 UNION ALL SELECT  112059424, 9102, 2
    UNION ALL SELECT  112059424, 9103, 2 UNION ALL SELECT  112059424, 9104, 2
    UNION ALL SELECT  112059424, 9105, 2 UNION ALL SELECT  112059424, 9106, 2
    UNION ALL SELECT  112059424, 9107, 2 UNION ALL SELECT  112059424, 9108, 2
    UNION ALL SELECT  112059424, 9109, 2 UNION ALL SELECT  112059424, 9110, 2
    UNION ALL SELECT  112059424, 9111, 2 UNION ALL SELECT  112059424, 9112, 2
    UNION ALL SELECT  112059424, 9113, 2 UNION ALL SELECT  112059424, 9114, 2
    UNION ALL SELECT  112059424, 9115, 2 UNION ALL SELECT  112059424, 9116, 2
    UNION ALL SELECT  112059424, 9117, 2
) AS `push`
WHERE NOT EXISTS (SELECT 1 FROM `hotfix_data` WHERE `UniqueId`=1609394630);

-- Open Hero to every race a Warrior can be.
--
-- The first Hero file gave the class the ten races Conquest of Azeroth gives
-- it. That list is not a decision about Hero: Ascension's client is from Wrath,
-- where Goblin was not playable yet and neither was anything added after it, so
-- there were only ten rows in its data to copy. On this client a Warrior can be
-- thirty-one races, and a Hero has no class to gate it by, so it gets all of
-- them. The owner decided this, including both Dracthyr and both Haranir.
--
-- This is a new file rather than an edit to 2026_09_20_00_mod_hero_class.sql,
-- because that one has already been applied and the updater keys on filename.
--
-- Two of these thirty-one are worth watching. Neither Dracthyr nor Haranir has
-- a class_expansion_requirement row for Warrior in the world database, so the
-- server does not treat them as ordinary any-class races today; the companion
-- world file gives Hero its own rows for them. Dracthyr also have their own
-- forms and starting zone. If a race misbehaves, those four are where to look
-- first, and removing a race is one delete from each of the three tables.
--
-- The rule for OtherFactionRaceID is now simply whatever the client's own
-- Warrior row uses for that race, read from its CharBaseInfo rather than
-- chosen. That column only matters for a faction change.

-- The twenty-one races Hero did not have. Ids carry on from the 9101 block the
-- first file used, so 9111 to 9131.
INSERT INTO `char_base_info` (`ID`,`RaceID`,`ClassID`,`OtherFactionRaceID`,`VerifiedBuild`)
SELECT `ID`,`RaceID`,16,`OtherFactionRaceID`,0 FROM (
             SELECT 9111 AS `ID`,  9 AS `RaceID`,  7 AS `OtherFactionRaceID`  -- Goblin
    UNION ALL SELECT 9112, 22,  8                                             -- Worgen
    UNION ALL SELECT 9113, 24, 24                                             -- Pandaren, neutral
    UNION ALL SELECT 9114, 25, 26                                             -- Pandaren, Alliance
    UNION ALL SELECT 9115, 26, 25                                             -- Pandaren, Horde
    UNION ALL SELECT 9116, 27,  4                                             -- Nightborne
    UNION ALL SELECT 9117, 28, 11                                             -- Highmountain Tauren
    UNION ALL SELECT 9118, 29, 10                                             -- Void Elf
    UNION ALL SELECT 9119, 30,  6                                             -- Lightforged Draenei
    UNION ALL SELECT 9120, 31, 32                                             -- Zandalari Troll
    UNION ALL SELECT 9121, 32, 31                                             -- Kul Tiran
    UNION ALL SELECT 9122, 34, 36                                             -- Dark Iron Dwarf
    UNION ALL SELECT 9123, 35, 37                                             -- Vulpera
    UNION ALL SELECT 9124, 36, 34                                             -- Mag'har Orc
    UNION ALL SELECT 9125, 37, 35                                             -- Mechagnome
    UNION ALL SELECT 9126, 52, 70                                             -- Dracthyr, Alliance
    UNION ALL SELECT 9127, 70, 52                                             -- Dracthyr, Horde
    UNION ALL SELECT 9128, 84, 85                                             -- Earthen, Alliance
    UNION ALL SELECT 9129, 85, 84                                             -- Earthen, Horde
    UNION ALL SELECT 9130, 86, 91                                             -- Haranir, Alliance
    UNION ALL SELECT 9131, 91, 86                                             -- Haranir, Horde
) AS `pair`
WHERE NOT EXISTS (SELECT 1 FROM `char_base_info` `existing`
                  WHERE `existing`.`ClassID`=16 AND `existing`.`RaceID`=`pair`.`RaceID`);

-- One of the original ten did not follow the rule above. Hero paired Orc with
-- Human; the client's own Warrior row pairs Orc with Dwarf. Bring it into line
-- so every race follows one rule.
UPDATE `char_base_info` SET `OtherFactionRaceID`=3 WHERE `ID`=9102 AND `ClassID`=16 AND `RaceID`=2;

-- Tell the client about the new rows, and about the Orc row that changed.
--
-- This push must have a higher id than the one the first Hero file made, for
-- the same reason that one had to outrank Reaper's renumber: the core reads
-- pushes in id order and the last word about a record wins. Record 9102 is
-- named here as well as there, so the client is told its content again rather
-- than keeping what it cached under the earlier push.
SET @HeroRacesId := (SELECT `Id` FROM `hotfix_data` WHERE `UniqueId`=1609394601 LIMIT 1);
SET @HeroRacesId := COALESCE(@HeroRacesId, (SELECT COALESCE(MAX(`Id`),0)+1 FROM `hotfix_data`));
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`)
SELECT @HeroRacesId,1609394601,812099832,`RecordId`,1 FROM (         -- CharBaseInfo 0x3067A8F8
             SELECT 9102 AS `RecordId`                               -- the corrected Orc row
    UNION ALL SELECT 9111 UNION ALL SELECT 9112 UNION ALL SELECT 9113
    UNION ALL SELECT 9114 UNION ALL SELECT 9115 UNION ALL SELECT 9116
    UNION ALL SELECT 9117 UNION ALL SELECT 9118 UNION ALL SELECT 9119
    UNION ALL SELECT 9120 UNION ALL SELECT 9121 UNION ALL SELECT 9122
    UNION ALL SELECT 9123 UNION ALL SELECT 9124 UNION ALL SELECT 9125
    UNION ALL SELECT 9126 UNION ALL SELECT 9127 UNION ALL SELECT 9128
    UNION ALL SELECT 9129 UNION ALL SELECT 9130 UNION ALL SELECT 9131
) AS `push`
WHERE NOT EXISTS (
    SELECT 1 FROM `hotfix_data` `existing`
    WHERE `existing`.`Id`=@HeroRacesId
      AND `existing`.`TableHash`=812099832 AND `existing`.`RecordId`=`push`.`RecordId`);
